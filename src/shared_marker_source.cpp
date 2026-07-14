#include "chronocore/shared_marker_source.hpp"

#include <chrono>

namespace chronocore {
SharedMarkerSource::SharedMarkerSource(CorrelationEngine& engine, const std::string& shared_memory_name)
    : engine_(engine), ring_(SharedMarkerRing::open(shared_memory_name)) {}
SharedMarkerSource::~SharedMarkerSource() { stop(); }
void SharedMarkerSource::start() { running_ = true; thread_ = std::thread(&SharedMarkerSource::run, this); }
void SharedMarkerSource::stop() { running_ = false; if (thread_.joinable()) thread_.join(); }
void SharedMarkerSource::run() {
  while (running_) {
    AppMarker marker;
    if (ring_.consume(marker)) engine_.ingest_event(to_app_event(marker));
    else std::this_thread::sleep_for(std::chrono::microseconds(50));
  }
}
}  // namespace chronocore
