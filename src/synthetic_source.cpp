#include "chronocore/synthetic_source.hpp"

#include <array>
#include <chrono>
#include <random>

namespace chronocore {
namespace {
Nanoseconds now_ns() {
  return static_cast<Nanoseconds>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
}  // namespace

SyntheticSource::SyntheticSource(CorrelationEngine& engine, std::uint64_t regression_after_ms)
    : engine_(engine), regression_after_ms_(regression_after_ms) {}
SyntheticSource::~SyntheticSource() { stop(); }
void SyntheticSource::start() { running_ = true; thread_ = std::thread(&SyntheticSource::run, this); }
void SyntheticSource::stop() { running_ = false; if (thread_.joinable()) thread_.join(); }

void SyntheticSource::run() {
  const std::array<std::string, 3> functions{"OrderBook::insert", "Matcher::match", "Risk::validate"};
  std::mt19937_64 rng{42};
  std::normal_distribution<double> jitter{0., 9.};
  std::uint64_t sequence = 0;
  const auto started = std::chrono::steady_clock::now();
  while (running_) {
    const auto index = sequence % functions.size();
    const bool regression = index == 0 && std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count() >= static_cast<long long>(regression_after_ms_);
    const auto timestamp = now_ns();
    const auto latency = static_cast<Nanoseconds>(std::max(10., 70. + jitter(rng) + (regression ? 180. : 0.)));
    engine_.ingest_event({sequence++, timestamp, functions[index], latency, 1, 1, 0});
    engine_.ingest_counter({timestamp + latency, static_cast<std::uint64_t>(regression ? 4 : 1),
                            static_cast<std::uint64_t>(index == 1 ? 2 : 0), 1, 1, 0, 0});
    std::this_thread::sleep_for(std::chrono::microseconds(700));
  }
}

}  // namespace chronocore
