#include "ramulator2.hh"

#include <fmt/core.h>
#include "base/base.h"
#include "base/config.h"
#include "base/request.h"
#include "frontend/frontend.h"
#include "memory_system/memory_system.h"

void Ramulator2::init() {
  cycle_count = 0;
  num_reads = 0;
  num_writes = 0;
  num_reqs = 0;
  tot_reads = 0;
  tot_writes = 0;
  YAML::Node config =
      Ramulator::Config::parse_config_file(config_path, {});
  ramulator2_frontend = Ramulator::Factory::create_frontend(config);
  ramulator2_memorysystem = Ramulator::Factory::create_memory_system(config);
  ramulator2_frontend->connect_memory_system(ramulator2_memorysystem);
  ramulator2_memorysystem->connect_frontend(ramulator2_frontend);
  const YAML::Node& ifce_config = config["MemorySystem"]["DRAM"];
  std::string impl_name = ifce_config["impl"].as<std::string>("");
  std_name = impl_name + "-CH_" + std::to_string(memory_id);
}

bool Ramulator2::full() const {
  return request_queue.size() >= 256;
}

void Ramulator2::push(mem_fetch* mf) {
  request_queue.push(mf);
}

mem_fetch* Ramulator2::return_queue_top() const {
  if(return_queue.empty()) return NULL;
  return return_queue.front();
}

mem_fetch* Ramulator2::return_queue_pop() {
  mem_fetch* mf = return_queue.front();
  return_queue.pop();
  return mf;
}

void Ramulator2::finish() {
  ramulator2_frontend->finalize(memory_id == 0);
  ramulator2_memorysystem->finalize(memory_id == 0);
  if (cycle_count == 0)
    return;

  double bytes_per_cycle =
      (double)(tot_reads + tot_writes) * req_size / cycle_count;
  double bandwidth_gbs = bytes_per_cycle * freq_mhz / 1000.0;
  double utilization_pct =
      (double)(tot_reads + tot_writes) * 100.0 * nbl / (2.0 * cycle_count);
  spdlog::info("{}: avg BW {} GB/s, {}% ({} reads, {} writes)", std_name,
               fmt::format("{:.2f}", bandwidth_gbs),
               fmt::format("{:.2f}", utilization_pct), tot_reads, tot_writes);
  num_reads = 0;
  num_writes = 0;
}

void Ramulator2::cycle() {
  if (!request_queue.empty()) {
    mem_fetch* mf = request_queue.front();
    auto callback = [this, mf](Ramulator::Request& req) {
      if (req.type_id == Ramulator::Request::Type::Read) {
        num_reads++;
        tot_reads++;
      } else {
        num_writes++;
        tot_writes++;
      }
      mf->set_reply();
      return_queue.push(mf);
    };
    bool success = ramulator2_frontend->receive_external_requests(
        mf->is_write() ? 1 : 0, mf->get_addr(), 0, callback);
    if(success)
      request_queue.pop();
  }
  ramulator2_memorysystem->tick();
  if(cycle_count % log_interval == 0) {
    double bytes_per_cycle =
        (double)(num_reads + num_writes) * req_size / log_interval;
    double bandwidth_gbs = bytes_per_cycle * freq_mhz / 1000.0;
    double utilization_pct =
        (double)(num_reads + num_writes) * 100.0 * nbl / (2.0 * log_interval);
    if(memory_id == 0)
      spdlog::info("{}: BW {} GB/s, {}% ({} reads, {} writes)",
                  std_name, fmt::format("{:.2f}", bandwidth_gbs),
                  fmt::format("{:.2f}", utilization_pct), num_reads, num_writes);
    else
      spdlog::debug("{}: BW {} GB/s, {}% ({} reads, {} writes)",
                  std_name, fmt::format("{:.2f}", bandwidth_gbs),
                  fmt::format("{:.2f}", utilization_pct), num_reads, num_writes);
    num_reads = 0;
    num_writes = 0;
  }
  cycle_count++;
}

void Ramulator2::print(FILE* fp) {
  finish();
}
