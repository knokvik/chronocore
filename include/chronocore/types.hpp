#pragma once

#include <cstdint>
#include <string>

namespace chronocore {

using Nanoseconds = std::uint64_t;

struct AppEvent {
  std::uint64_t sequence{};
  Nanoseconds timestamp_ns{};
  std::string function;
  // Duration is measured by the instrumented application. Counter samples are
  // attribution evidence, not a replacement for an application latency clock.
  Nanoseconds latency_ns{};
  std::uint32_t process_id{};
  std::uint32_t thread_id{};
  std::uint32_t cpu{};
};

struct CounterSample {
  Nanoseconds timestamp_ns{};
  std::uint64_t l3_misses{};
  std::uint64_t branch_misses{};
  std::uint32_t process_id{};
  std::uint32_t thread_id{};
  std::uint32_t cpu{};
  std::uint64_t instruction_pointer{};
};

struct CorrelatedSample {
  AppEvent event;
  CounterSample counters;
  Nanoseconds correlation_distance_ns{};
};

struct Alert {
  std::string function;
  double observed_latency_ns{};
  double baseline_mean_ns{};
  double baseline_sigma_ns{};
  Nanoseconds timestamp_ns{};
  std::uint64_t sequence{};
};

}  // namespace chronocore
