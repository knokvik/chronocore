#include "chronocore/perf_collector.hpp"

#include <stdexcept>

namespace chronocore {
class PerfCollector::Impl {
 public:
  Impl(CorrelationEngine&, PerfCollectorConfig) {}
  void start() { throw std::runtime_error("perf collection requires Linux perf_event_open"); }
  void stop() {}
};
PerfCollector::PerfCollector(CorrelationEngine& engine, PerfCollectorConfig config) : impl_(std::make_unique<Impl>(engine, config)) {}
PerfCollector::~PerfCollector() = default;
PerfCollector::PerfCollector(PerfCollector&&) noexcept = default;
PerfCollector& PerfCollector::operator=(PerfCollector&&) noexcept = default;
void PerfCollector::start() { impl_->start(); }
void PerfCollector::stop() { impl_->stop(); }
}  // namespace chronocore
