#include <cassert>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <cstring>
#include <unistd.h>

#include "chronocore/baseline_store.hpp"
#include "chronocore/correlation_engine.hpp"
#include "chronocore/online_statistics.hpp"
#include "chronocore/shared_ring.hpp"

int main() {
  chronocore::Welford stats;
  for (double value : {2., 4., 4., 4., 5., 5., 7., 9.}) stats.add(value);
  assert(stats.count() == 8);
  assert(std::abs(stats.mean() - 5.) < 1e-9);
  assert(std::abs(stats.variance() - 4.5714285714) < 1e-6);

  chronocore::CorrelationEngine engine({.correlation_window_ns = 100, .baseline_min_samples = 30});
  for (std::uint64_t i = 0; i < 31; ++i) {
    engine.ingest_event({i, 1'000 + i * 1'000, "OrderBook::insert", 50});
    engine.ingest_counter({1'050 + i * 1'000, 1, 0});
  }
  for (std::uint64_t i = 0; i < 3; ++i) {
    engine.ingest_event({99 + i, 99'000 + i * 1'000, "OrderBook::insert", 90});
    engine.ingest_counter({99'090 + i * 1'000, 4, 0});
  }
  const auto metrics = engine.metrics();
  assert(metrics.size() == 1 && metrics[0].samples == 34);
  assert(!engine.recent_alerts().empty());

  // Markers-only path (no PMU counters) must still produce latency metrics/alerts.
  chronocore::CorrelationEngine markers_only({.correlation_window_ns = 100, .baseline_min_samples = 30});
  for (std::uint64_t i = 0; i < 31; ++i) {
    markers_only.ingest_event({i, 1'000 + i * 1'000, "OrderBook::insert", 50});
  }
  for (std::uint64_t i = 0; i < 3; ++i) {
    markers_only.ingest_event({99 + i, 99'000 + i * 1'000, "OrderBook::insert", 90});
  }
  const auto marker_metrics = markers_only.metrics();
  assert(marker_metrics.size() == 1 && marker_metrics[0].samples == 34);
  assert(!markers_only.recent_alerts().empty());

  const auto fixture = std::filesystem::temp_directory_path() / "chronocore-baseline-test";
  chronocore::BaselineStore store(fixture, "build-a|cpu-a|collector-a");
  store.save(engine.baseline_records());
  const auto loaded = store.load();
  assert(loaded.has_value() && loaded->size() == 1 && loaded->front().count == 30);
  chronocore::CorrelationEngine restored({.baseline_min_samples = 30});
  restored.restore_baselines(*loaded);
  assert(restored.metrics().front().baseline_ready);
  std::filesystem::remove(fixture);

  const auto ring_name = "/chronocore-test-" + std::to_string(getpid());
  chronocore::SharedMarkerRing::unlink(ring_name);
  auto producer = chronocore::SharedMarkerRing::create(ring_name, 2);
  auto consumer = chronocore::SharedMarkerRing::open(ring_name);
  chronocore::AppMarker marker{};
  marker.sequence = 7;
  marker.timestamp_ns = 123;
  marker.latency_ns = 77;
  marker.process_id = 42;
  marker.thread_id = 4;
  std::strncpy(marker.function, "Matcher::match", sizeof(marker.function) - 1);
  assert(producer.publish(marker));
  chronocore::AppMarker received{};
  assert(consumer.consume(received));
  const auto converted = chronocore::to_app_event(received);
  assert(converted.sequence == 7 && converted.latency_ns == 77 && converted.function == "Matcher::match");
  assert(chronocore::SharedMarkerRing::unlink(ring_name));
  std::cout << "all tests passed\n";
}
