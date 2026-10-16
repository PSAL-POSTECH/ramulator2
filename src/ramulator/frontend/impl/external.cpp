#include "ramulator/base/param.h"
#include "ramulator/frontend/i_frontend.h"

namespace Ramulator {

class ExternalFrontEnd : public IFrontEnd, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IFrontEnd, ExternalFrontEnd, "External")

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_clock_ratio, unsigned int, "clock_ratio").required();
  }

  void tick() override {}

  bool is_finished() override { return false; }

  bool receive_external_requests(int req_type_id, Addr_t addr, int source_id,
                                 std::function<void(Request&)> callback,
                                 int size_bytes) override {
    return receive_external_requests(req_type_id, addr, source_id, -1,
                                     std::move(callback), size_bytes);
  }

  bool receive_external_requests(int req_type_id, Addr_t addr, int source_id,
                                 int ingress_id,
                                 std::function<void(Request&)> callback,
                                 int size_bytes) override {
    Request req(addr, req_type_id, source_id, std::move(callback));
    req.ingress_id = ingress_id;
    req.size_bytes = size_bytes;
    if (req.addr_vec.empty()) {
      req.addr_vec.push_back(0);
    }
    return m_memory_system->send(req);
  }
};

}  // namespace Ramulator
