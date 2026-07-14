#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#include "chronocore/correlation_engine.hpp"

namespace chronocore {

// Development-only source. A Linux collector can feed the same two ingest APIs.
class SyntheticSource {
 public:
  SyntheticSource(CorrelationEngine& engine, std::uint64_t regression_after_ms);
  ~SyntheticSource();
  void start();
  void stop();
 private:
  void run();
  CorrelationEngine& engine_;
  std::uint64_t regression_after_ms_;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

}  // namespace chronocore
