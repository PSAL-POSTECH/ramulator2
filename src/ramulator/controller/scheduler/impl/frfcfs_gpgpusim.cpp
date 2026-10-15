#include <limits>
#include <unordered_map>
#include <vector>

#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/scheduler/i_scheduler.h"

namespace Ramulator {

// The row selection of GPGPU-Sim's frfcfs_scheduler (dram_sched.cc), and only that.
// Each bank works on one selected row until no request for it is left, newly
// arrived ones included; it then takes the open row if anything still hits it,
// else the row of its oldest request. Which eligible request's command goes next
// stays this controller's FRFCFS (ready first, then oldest): GPGPU-Sim's
// column-first, dual-issue command loop belongs to its DRAM model (dram.cc).
class FRFCFSGPGPUSimScheduler : public IScheduler, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IScheduler, FRFCFSGPGPUSimScheduler, "FRFCFS-GPGPUSim")

  static constexpr int kNoRow = -1;

  ControllerBase* m_ctrl = nullptr;
  int m_row_level = -1;
  size_t m_num_banks = 0;

  // The selected row of each bank, kept per buffer: the controller asks about
  // its active, read and write buffers in turn, and each has its own requests.
  std::unordered_map<const ReqBuffer*, std::vector<int>> m_sel_row;

  // Per-call scratch, one entry a bank.
  std::vector<bool> m_has_req, m_sel_alive, m_open_requested;
  std::vector<int> m_open_row, m_oldest_row;
  std::vector<Clk_t> m_oldest_arrive;

  void init() override {
    m_ctrl = cast_parent<ControllerBase>();
    const auto& spec = *m_ctrl->m_device.m_spec;
    m_row_level = spec.has_level("row") ? spec.get_level_id("row") : spec.get_level_id("Row");
    m_num_banks = m_ctrl->m_device.m_bank_nodes.size();
  }

  ReqBuffer::iterator get_best_request(ReqBuffer& buffer, RequestFilterRef filter) override {
    if (buffer.size() == 0) {
      return buffer.end();
    }

    auto& sel = m_sel_row[&buffer];
    if (sel.size() != m_num_banks) {
      sel.assign(m_num_banks, kNoRow);
    }
    m_has_req.assign(m_num_banks, false);
    m_sel_alive.assign(m_num_banks, false);
    m_open_requested.assign(m_num_banks, false);
    m_open_row.assign(m_num_banks, kNoRow);
    m_oldest_row.assign(m_num_banks, kNoRow);
    m_oldest_arrive.assign(m_num_banks, std::numeric_limits<Clk_t>::max());

    // Pass 1: commands, and per bank whether its selected row still has a
    // request, whether the open row is requested, and its oldest request's row.
    for (auto it = buffer.begin(); it != buffer.end(); it++) {
      it->command = m_ctrl->get_preq_command(it->final_command, it->addr_vec);
      const int bank = bank_of(*it);
      if (bank < 0) {
        continue;
      }
      const int row = it->addr_vec[m_row_level];
      m_has_req[bank] = true;
      if (row == sel[bank]) {
        m_sel_alive[bank] = true;
      }
      if (m_ctrl->m_device.check_rowbuffer_hit(it->final_command, it->addr_vec, m_ctrl->m_clk)) {
        m_open_requested[bank] = true;
        m_open_row[bank] = row;
      }
      if (it->arrive < m_oldest_arrive[bank]) {
        m_oldest_arrive[bank] = it->arrive;
        m_oldest_row[bank] = row;
      }
    }

    // A bank whose selected row ran dry picks the next one.
    for (size_t b = 0; b < m_num_banks; b++) {
      if (!m_has_req[b]) {
        sel[b] = kNoRow;
      } else if (!m_sel_alive[b]) {
        sel[b] = m_open_requested[b] ? m_open_row[b] : m_oldest_row[b];
      }
    }

    // Pass 2: FRFCFS over the requests to a selected row.
    auto candidate = buffer.end();
    bool cand_timing_ok = false;
    for (auto it = buffer.begin(); it != buffer.end(); it++) {
      const int bank = bank_of(*it);
      if (bank >= 0 && it->addr_vec[m_row_level] != sel[bank]) {
        continue;
      }
      if (filter && !filter(*it)) {
        continue;
      }
      if (candidate == buffer.end()) {
        candidate = it;
        cand_timing_ok = m_ctrl->check_timing(it->command, it->addr_vec);
        continue;
      }
      bool it_timing_ok = m_ctrl->check_timing(it->command, it->addr_vec);
      if (cand_timing_ok != it_timing_ok) {
        if (it_timing_ok) {
          candidate = it;
          cand_timing_ok = true;
        }
      } else if (it->arrive < candidate->arrive) {
        candidate = it;
      }
    }

    return candidate;
  }

  int bank_of(const Request& req) const {
    const int bank_id = m_ctrl->m_device.get_flat_bank_id(req.addr_vec);
    if (bank_id < 0 || bank_id >= static_cast<int>(m_num_banks)) {
      return -1;
    }
    return bank_id;
  }
};

}  // namespace Ramulator
