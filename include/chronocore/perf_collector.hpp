#pragma once

#include <cstdint>
#include <memory>

#include "chronocore/correlation_engine.hpp"

namespace chronocore {

struct PerfCollectorConfig {
  std::uint32_t target_pid{};
  std::uint64_t sample_period{10'000};
  std::uint8_t requested_precise_ip{2};
};

// Linux implementation streams mmap'd perf samples into the core. It uses the
// generic cache-miss event; PMU-specific raw events must be selected only after
// probing the target CPU and recording the resulting collector identity.
class PerfCollector {
 public:
  PerfCollector(CorrelationEngine& engine, PerfCollectorConfig config);
  ~PerfCollector();
  PerfCollector(PerfCollector&&) noexcept;
  PerfCollector& operator=(PerfCollector&&) noexcept;
  PerfCollector(const PerfCollector&) = delete;
  PerfCollector& operator=(const PerfCollector&) = delete;
  void start();
  void stop();
 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace chronocore
