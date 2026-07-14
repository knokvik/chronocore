#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "chronocore/correlation_engine.hpp"
#include "chronocore/shared_ring.hpp"

namespace chronocore {

class SharedMarkerSource {
 public:
  SharedMarkerSource(CorrelationEngine& engine, const std::string& shared_memory_name);
  ~SharedMarkerSource();
  void start();
  void stop();
 private:
  void run();
  CorrelationEngine& engine_;
  SharedMarkerRing ring_;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

}  // namespace chronocore
