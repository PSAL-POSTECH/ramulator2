#ifndef __RAMULATOR2_HH__
#define __RAMULATOR2_HH__

#include <cstdint>
#include <functional>
#include <ostream>
#include <queue>
#include <string>
#include <vector>

#include "Memfetch.h"

namespace Ramulator {
class IFrontEnd;
class IMemorySystem;
}  // namespace Ramulator

class Ramulator2 {
 public:
  Ramulator2() {}
  Ramulator2(unsigned memory_id, unsigned num_channels,
             std::string ramulator_config, std::string out, int log_interval, int nbl,
             unsigned req_size, unsigned freq_mhz)
      : memory_id(memory_id), num_channels(num_channels),
        config_path(ramulator_config),
        log_interval(log_interval), nbl(nbl),
        req_size(req_size), freq_mhz(freq_mhz) {
     init();
  }
  ~Ramulator2() {}

  void init();
  bool full() const;
  void cycle();
  void finalize_once();
  void print_stats_yaml(std::ostream& os);
  void finish();
  void print(FILE* fp = NULL);

  void push(class mem_fetch* mf);
  mem_fetch* return_queue_top() const;
  mem_fetch* return_queue_pop();
  void return_queue_push_back(mem_fetch* mf);
  bool returnq_full() const;

  int interval_reads() const { return num_reads; }
  int interval_writes() const { return num_writes; }
  void reset_interval_bw_counters() {
    num_reads = 0;
    num_writes = 0;
  }
  int total_reads() const { return tot_reads; }
  int total_writes() const { return tot_writes; }

 private:
  std::string std_name;
  std::string config_path;
  bool finish_called_ = false;
  std::queue<mem_fetch*> request_queue;
  std::queue<mem_fetch*> return_queue;
  Ramulator::IFrontEnd* ramulator2_frontend;
  Ramulator::IMemorySystem* ramulator2_memorysystem;
  int memory_id;
  int num_channels;
  uint64_t cycle_count = 0;
  int log_interval = 10000;
  int num_reqs;
  int num_reads;
  int num_writes;
  int nbl;
  unsigned req_size = 0;
  unsigned freq_mhz = 0;
  int tot_reqs;
  int tot_reads;
  int tot_writes;
};

#endif  // __RAMULATOR2_HH__
