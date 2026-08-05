#include "ramulator2.hh"

#include <iostream>

#include "ramulator/base/base.h"
#include "ramulator/base/config.h"
#include "ramulator/base/config_node.h"
#include "ramulator/base/request.h"
#include "ramulator/frontend/i_frontend.h"
#include "ramulator/memory_system/i_memory_system.h"

void Ramulator2::init() {
  cycle_count = 0;
  num_reads = 0;
  num_writes = 0;
  num_reqs = 0;
  tot_reads = 0;
  tot_writes = 0;
  tot_reqs = 0;

  Ramulator::ConfigNode config = Ramulator::Config::parse_config_file(config_path);

  // Override frontend to ExternalFrontEnd which exposes receive_external_requests().
  // PyTorchSim drives the simulation externally, so GEM5/trace frontends must not be used.
  // Override frontend to ExternalFrontEnd which exposes receive_external_requests().
  // PyTorchSim drives the simulation externally.
  Ramulator::ConfigNode frontend_config;
  frontend_config.set("impl", std::string("External"));
  frontend_config.set("clock_ratio", 1);
  config.set("frontend", frontend_config);

  ramulator2_frontend = Ramulator::Factory::create_frontend(config);
  ramulator2_memorysystem = Ramulator::Factory::create_memory_system(config);
  ramulator2_frontend->connect_memory_system(ramulator2_memorysystem);
  ramulator2_memorysystem->connect_frontend(ramulator2_frontend);

  // Extract memory system impl name for logging
  std::string impl_name =
      config["memory_system"]["impl"].as<std::string>(std::string("DRAM"));
  std_name = impl_name + "-CH_" + std::to_string(memory_id);
}

bool Ramulator2::full() const {
  return request_queue.size() >= 256;
}

void Ramulator2::push(mem_fetch* mf) {
  request_queue.push(mf);
}

mem_fetch* Ramulator2::return_queue_top() const {
  if (return_queue.empty()) return NULL;
  return return_queue.front();
}

mem_fetch* Ramulator2::return_queue_pop() {
  mem_fetch* mf = return_queue.front();
  return_queue.pop();
  return mf;
}

void Ramulator2::return_queue_push_back(mem_fetch* mf) {
  return_queue.push(mf);
}

bool Ramulator2::returnq_full() const {
  return return_queue.size() >= 256;
}

void Ramulator2::finalize_once() {
  if (finish_called_) {
    return;
  }
  finish_called_ = true;
  ramulator2_frontend->finalize();
  ramulator2_memorysystem->finalize();
}

void Ramulator2::print_stats_yaml(std::ostream& os) {
  ramulator2_frontend->print_stats(os);
  ramulator2_memorysystem->print_stats(os);
}

namespace {
/** Walk a collect_stats() tree, summing row_misses + row_conflicts wherever they appear. */
uint64_t sum_row_activations(const Ramulator::ConfigNode& node) {
  uint64_t acts = 0;
  if (node.is_sequence()) {
    for (const auto& child : node.seq())
      acts += sum_row_activations(child);
    return acts;
  }
  if (!node.is_map())
    return 0;
  for (const auto& [key, child] : node.map()) {
    if (child.is_map() || child.is_sequence())
      acts += sum_row_activations(child);
    else if (key == "row_misses" || key == "row_conflicts")
      acts += child.as<uint64_t>(uint64_t{0});
  }
  return acts;
}
}  // namespace

uint64_t Ramulator2::row_activations() const {
  if (ramulator2_memorysystem == nullptr)
    return 0;
  return sum_row_activations(ramulator2_memorysystem->collect_stats());
}

void Ramulator2::finish() {
  finalize_once();
  print_stats_yaml(std::cout);
  std::cout.flush();
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
        mf->is_write() ? 1 : 0, mf->get_addr(), 0, callback, static_cast<int>(req_size));
    if (success)
      request_queue.pop();
  }

  ramulator2_memorysystem->tick();
  cycle_count++;
}

void Ramulator2::print(FILE* fp) {
  finish();
}
