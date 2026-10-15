#include <algorithm>
#include <vector>

#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/scheduler/i_scheduler.h"

namespace Ramulator {

// FRFCFS that prioritizes row hits, as ramulator1's FRFCFS_PriorHit does.
// A ready row hit is issued first, oldest among them; otherwise FRFCFS runs
// over the rest, skipping any request that would close a row with a hit waiting.
class FRFCFSPriorHitScheduler : public IScheduler, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IScheduler, FRFCFSPriorHitScheduler, "FRFCFS-PriorHit")

  ControllerBase* m_ctrl = nullptr;

  std::vector<bool> m_bank_has_hit;

  void init() override {
    m_ctrl = cast_parent<ControllerBase>();
    m_bank_has_hit.assign(m_ctrl->m_device.m_bank_nodes.size(), false);
  }

  ReqBuffer::iterator get_best_request(ReqBuffer& buffer, RequestFilterRef filter) override {
    if (buffer.size() == 0) {
      return buffer.end();
    }

    std::fill(m_bank_has_hit.begin(), m_bank_has_hit.end(), false);

    // Pass 1: resolve commands and mark every bank with a row hit waiting, of any age.
    for (auto it = buffer.begin(); it != buffer.end(); it++) {
      it->command = m_ctrl->get_preq_command(it->final_command, it->addr_vec);
      if (is_row_hit(*it)) {
        const int bank_id = bank_of(*it);
        if (bank_id >= 0) {
          m_bank_has_hit[bank_id] = true;
        }
      }
    }

    // Pass 2: the oldest ready row hit wins outright.
    auto best_hit = buffer.end();
    for (auto it = buffer.begin(); it != buffer.end(); it++) {
      if (filter && !filter(*it)) {
        continue;
      }
      if (!is_row_hit(*it) || !m_ctrl->check_timing(it->command, it->addr_vec)) {
        continue;
      }
      if (best_hit == buffer.end() || it->arrive < best_hit->arrive) {
        best_hit = it;
      }
    }
    if (best_hit != buffer.end()) {
      return best_hit;
    }

    // Pass 3: FRFCFS over the requests that would not close a row someone still hits.
    auto candidate = buffer.end();
    bool cand_timing_ok = false;
    for (auto it = buffer.begin(); it != buffer.end(); it++) {
      if (filter && !filter(*it)) {
        continue;
      }
      if (would_close_hit_row(*it)) {
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

  bool is_row_hit(const Request& req) const {
    return m_ctrl->m_device.check_rowbuffer_hit(req.final_command, req.addr_vec, m_ctrl->m_clk);
  }

  int bank_of(const Request& req) const {
    const int bank_id = m_ctrl->m_device.get_flat_bank_id(req.addr_vec);
    if (bank_id < 0 || bank_id >= static_cast<int>(m_bank_has_hit.size())) {
      return -1;
    }
    return bank_id;
  }

  bool would_close_hit_row(const Request& req) const {
    const auto& spec = *m_ctrl->m_device.m_spec;
    if (!spec.command_meta[req.command].is_closing || req.command == req.final_command) {
      return false;
    }
    if (spec.bank_targets[req.command] != BankTarget::Single) {
      return false;
    }
    const int bank_id = bank_of(req);
    return bank_id >= 0 && m_bank_has_hit[bank_id];
  }
};

}  // namespace Ramulator
