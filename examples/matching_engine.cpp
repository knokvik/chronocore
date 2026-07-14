#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

#include <unistd.h>

#include "chronocore/shared_ring.hpp"

int main(int argc, char** argv) {
  const std::string name = argc > 1 ? argv[1] : "/chronocore-example";
  const auto regression_after_ms = argc > 2 ? std::stoull(argv[2]) : 10'000ULL;
  chronocore::SharedMarkerRing::unlink(name);
  auto ring = chronocore::SharedMarkerRing::create(name);
  // 64 MiB is intentionally larger than common LLC sizes. Random cache-line
  // reads make cache-miss sampling observable without synthetic counter data.
  std::vector<std::uint64_t> working_set(8 * 1024 * 1024);
  std::mt19937_64 random{42};
  for (auto& value : working_set) value = random();
  volatile std::uint64_t sink = 0;
  const auto launched = std::chrono::steady_clock::now();
  std::cout << "publishing application markers to " << name << "; workload regression begins after "
            << regression_after_ms << " ms (Ctrl-C to stop)\n";
  for (std::uint64_t sequence = 0;; ++sequence) {
    const auto started = std::chrono::steady_clock::now();
    const auto regressed = std::chrono::duration_cast<std::chrono::milliseconds>(started - launched).count() >=
        static_cast<long long>(regression_after_ms);
    const auto reads = regressed ? 768U : 64U;
    for (unsigned int read = 0; read < reads; ++read) sink ^= working_set[random() % working_set.size()];
    const auto finished = std::chrono::steady_clock::now();
    chronocore::AppMarker marker{};
    marker.sequence = sequence;
    marker.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(finished.time_since_epoch()).count();
    marker.latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count();
    marker.process_id = static_cast<std::uint32_t>(getpid());
    marker.thread_id = marker.process_id;
    std::strncpy(marker.function, "OrderBook::insert", sizeof(marker.function) - 1);
    while (!ring.publish(marker)) std::this_thread::yield();
  }
}
