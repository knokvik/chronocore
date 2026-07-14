#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

#include "chronocore/online_statistics.hpp"
#include "chronocore/types.hpp"

namespace chronocore {

struct FunctionMetrics {
  std::string function;
  std::size_t samples{};
  double mean_latency_ns{};
  double p99_latency_ns{};
  double l3_misses_per_event{};
  double branch_misses_per_event{};
  bool baseline_ready{};
};

struct BaselineRecord {
  std::string function;
  std::size_t count{};
  double mean_ns{};
  double m2{};
};

struct CorrelationConfig {
  Nanoseconds correlation_window_ns{500};
  std::size_t baseline_min_samples{100};
  std::size_t sustained_high_samples{3};
  double alert_sigma{3.};
};

class CorrelationEngine {
 public:
  explicit CorrelationEngine(CorrelationConfig config = {});
  void ingest_event(AppEvent event);
  std::optional<CorrelatedSample> ingest_counter(CounterSample sample);
  [[nodiscard]] std::vector<FunctionMetrics> metrics() const;
  [[nodiscard]] std::vector<Alert> recent_alerts() const;
  [[nodiscard]] std::vector<BaselineRecord> baseline_records() const;
  void restore_baselines(const std::vector<BaselineRecord>& records);
  [[nodiscard]] std::uint64_t generation() const;
  bool wait_for_update(std::uint64_t previous_generation, std::chrono::milliseconds timeout) const;

 private:
  struct FunctionState {
    Welford latency;
    Welford l3_misses;
    Welford branch_misses;
    P2Quantile p99{0.99};
    Welford baseline_latency;
    bool baseline_ready{};
    std::size_t consecutive_high_samples{};
    bool incident_open{};
  };
  void remove_expired_events(Nanoseconds latest_timestamp);
  void notify_update();

  const CorrelationConfig config_;
  mutable std::mutex mutex_;
  mutable std::condition_variable update_condition_;
  std::uint64_t generation_{};
  std::deque<AppEvent> pending_events_;
  std::map<std::string, FunctionState, std::less<>> states_;
  std::deque<Alert> alerts_;
};

}  // namespace chronocore
