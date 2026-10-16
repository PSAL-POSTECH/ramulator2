#ifndef RAMULATOR_CONTROLLER_IMPL_HBM_CONTROLLER_BASE_H
#define RAMULATOR_CONTROLLER_IMPL_HBM_CONTROLLER_BASE_H

#include <optional>

#include "ramulator/controller/controller_base.h"

namespace Ramulator {

class HBMControllerBase : public ControllerBase {
 public:
  void init() override;
  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override;
  void tick() override;

 protected:
  enum class SlotType : int { ColumnBus = 0, RowBus = 1 };

  struct IssuedCommand {
    SlotType slot;
    int command = -1;
    AddrVec_t addr_vec;
    Clk_t clk = -1;
  };

  int m_refresh_scope_levels = -1;

  HBMControllerBase(const ConfigNode& config, Implementation* parent)
      : ControllerBase(config, parent) {
  }

  virtual bool slot_matches(const Request& req, SlotType slot) const;
  // Whether a pending priority request (a refresh, an injected precharge) names `req`'s address: its -1 levels
  // cover everything below them. A read or write waits only for the priority requests that cover it.
  bool covered_by_priority(const Request& req) const;
  // Whether `req` would open a row beside a pending per-bank refresh: in its scope (the levels above the bank, its
  // bank group and its SID), where each ACT pushes the refresh back by ACT-to-REFpb and a stream of them starves it.
  bool opens_row_beside_refresh(const Request& req) const;

  void hbm_tick_prologue();
  void hbm_tick_epilogue();
  std::optional<IssuedCommand> try_issue_slot(SlotType slot);
};

}  // namespace Ramulator

#endif  // RAMULATOR_CONTROLLER_IMPL_HBM_CONTROLLER_BASE_H
