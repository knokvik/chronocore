#include "chronocore/correlation_engine.hpp"

#include <algorithm>
#include <cmath>

namespace chronocore {

CorrelationEngine::CorrelationEngine(CorrelationConfig config) : config_(config) {}

void CorrelationEngine::ingest_event(AppEvent event) {
  std::scoped_lock lock(mutex_);
  remove_expired_events(event.timestamp_ns);
  // Application-measured latency is the primary signal. Hardware counters only
  // attribute causes; without a PMU the markers-only path must still populate
  // metrics and fire 3σ alerts.
  auto& state = states_[event.function];
  const auto observed = static_cast<double>(event.latency_ns);
  if (!state.baseline_ready && state.latency.count() >= config_.baseline_min_samples) {
    state.baseline_latency = state.latency;
    state.baseline_ready = true;
  }
  const bool high = state.baseline_ready && state.baseline_latency.is_high_outlier(
      observed, config_.baseline_min_samples, config_.alert_sigma);
  state.consecutive_high_samples = high ? state.consecutive_high_samples + 1 : 0;
  // A 3σ point by itself is expected occasionally in any long-running stream.
  // Open one incident only after a short sustained excursion, then require the
  // signal to recover before a later regression may open another incident.
  if (high && !state.incident_open && state.consecutive_high_samples >= config_.sustained_high_samples) {
    alerts_.push_back({event.function, observed, state.baseline_latency.mean(), state.baseline_latency.stddev(),
                       event.timestamp_ns, event.sequence});
    if (alerts_.size() > 32) alerts_.pop_front();
    state.incident_open = true;
  } else if (state.incident_open && observed <= state.baseline_latency.mean() + state.baseline_latency.stddev()) {
    state.incident_open = false;
    state.consecutive_high_samples = 0;
  }
  state.latency.add(observed);
  state.p99.add(observed);
  pending_events_.push_back(std::move(event));
  notify_update();
}

std::optional<CorrelatedSample> CorrelationEngine::ingest_counter(CounterSample sample) {
  std::scoped_lock lock(mutex_);
  remove_expired_events(sample.timestamp_ns);
  auto best = pending_events_.end();
  Nanoseconds best_distance = config_.correlation_window_ns + 1;
  for (auto it = pending_events_.rbegin(); it != pending_events_.rend(); ++it) {
    // An application marker is emitted at completion. A PMU overflow can occur
    // anywhere in [completion - measured_latency, completion], rather than at
    // the final marker timestamp. The small window only covers clock/record
    // boundary uncertainty around that measured span.
    const auto start = it->timestamp_ns > it->latency_ns ? it->timestamp_ns - it->latency_ns : 0;
    const auto end = it->timestamp_ns;
    const auto distance = sample.timestamp_ns < start ? start - sample.timestamp_ns
        : sample.timestamp_ns > end ? sample.timestamp_ns - end : 0;
    const bool same_process = sample.process_id == 0 || it->process_id == 0 || sample.process_id == it->process_id;
    const bool same_thread = sample.thread_id == 0 || it->thread_id == 0 || sample.thread_id == it->thread_id;
    if (same_process && same_thread && distance <= config_.correlation_window_ns && distance < best_distance) {
      best = std::prev(it.base());
      best_distance = distance;
    }
    // Events are completion-time ordered. Once this event ends well before the
    // sample, all older events are farther away too.
    if (end + config_.correlation_window_ns < sample.timestamp_ns) break;
  }
  if (best == pending_events_.end()) return std::nullopt;

  CorrelatedSample correlated{*best, sample, best_distance};
  auto& state = states_[best->function];
  // Latency/alerts already recorded on ingest_event; counters only add attribution.
  state.l3_misses.add(static_cast<double>(sample.l3_misses));
  state.branch_misses.add(static_cast<double>(sample.branch_misses));
  pending_events_.erase(best);
  notify_update();
  return correlated;
}

std::vector<FunctionMetrics> CorrelationEngine::metrics() const {
  std::scoped_lock lock(mutex_);
  std::vector<FunctionMetrics> result;
  result.reserve(states_.size());
  for (const auto& [function, state] : states_) {
    result.push_back({function, state.latency.count(), state.latency.mean(), state.p99.value(),
                      state.l3_misses.mean(), state.branch_misses.mean(), state.baseline_ready});
  }
  return result;
}

std::vector<BaselineRecord> CorrelationEngine::baseline_records() const {
  std::scoped_lock lock(mutex_);
  std::vector<BaselineRecord> result;
  for (const auto& [function, state] : states_) {
    if (state.baseline_ready) result.push_back({function, state.baseline_latency.count(), state.baseline_latency.mean(), state.baseline_latency.m2()});
  }
  return result;
}

void CorrelationEngine::restore_baselines(const std::vector<BaselineRecord>& records) {
  std::scoped_lock lock(mutex_);
  for (const auto& record : records) {
    auto& state = states_[record.function];
    state.baseline_latency.restore(record.count, record.mean_ns, record.m2);
    state.baseline_ready = record.count >= config_.baseline_min_samples;
  }
  notify_update();
}

std::uint64_t CorrelationEngine::generation() const {
  std::scoped_lock lock(mutex_);
  return generation_;
}

bool CorrelationEngine::wait_for_update(std::uint64_t previous_generation, std::chrono::milliseconds timeout) const {
  std::unique_lock lock(mutex_);
  return update_condition_.wait_for(lock, timeout, [&] { return generation_ != previous_generation; });
}

std::vector<Alert> CorrelationEngine::recent_alerts() const {
  std::scoped_lock lock(mutex_);
  return {alerts_.begin(), alerts_.end()};
}

void CorrelationEngine::remove_expired_events(Nanoseconds latest_timestamp) {
  while (!pending_events_.empty() &&
      pending_events_.front().timestamp_ns + config_.pending_event_retention_ns < latest_timestamp) {
    pending_events_.pop_front();
  }
}

void CorrelationEngine::notify_update() {
  ++generation_;
  update_condition_.notify_all();
}

}  // namespace chronocore
