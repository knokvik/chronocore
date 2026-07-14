#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
#include <string_view>

#include "chronocore/baseline_store.hpp"
#include "chronocore/correlation_engine.hpp"
#include "chronocore/http_server.hpp"
#include "chronocore/perf_collector.hpp"
#include "chronocore/shared_marker_source.hpp"
#include "chronocore/synthetic_source.hpp"

namespace {
void usage() {
  std::cout << "Usage: chronocore-daemon [--port 8080] [--demo-regression-after-ms 5000] "
               "[--shm NAME --target-pid PID] [--baseline PATH --baseline-key KEY]\n";
}
}  // namespace

int main(int argc, char** argv) {
  std::uint16_t port = 8080;
  std::uint64_t regression_after_ms = 5000;
  std::optional<std::filesystem::path> baseline_path;
  std::optional<std::string> shared_memory_name;
  std::optional<std::uint32_t> target_pid;
  std::string baseline_key;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--help") { usage(); return 0; }
    if ((arg == "--port" || arg == "--demo-regression-after-ms") && i + 1 < argc) {
      const auto value = std::stoull(argv[++i]);
      if (arg == "--port") port = static_cast<std::uint16_t>(value); else regression_after_ms = value;
    } else if ((arg == "--baseline" || arg == "--baseline-key") && i + 1 < argc) {
      const std::string_view value(argv[++i]);
      if (arg == "--baseline") baseline_path = value; else baseline_key = value;
    } else if ((arg == "--shm" || arg == "--target-pid") && i + 1 < argc) {
      const std::string_view value(argv[++i]);
      if (arg == "--shm") shared_memory_name = std::string(value); else target_pid = static_cast<std::uint32_t>(std::stoul(std::string(value)));
    } else { usage(); return 2; }
  }
  if (baseline_path.has_value() != !baseline_key.empty()) {
    std::cerr << "--baseline and --baseline-key must be supplied together\n";
    return 2;
  }
  try {
    auto engine = std::make_shared<chronocore::CorrelationEngine>();
    std::shared_ptr<chronocore::BaselineStore> baseline_store;
    if (baseline_path) {
      baseline_store = std::make_shared<chronocore::BaselineStore>(*baseline_path, baseline_key);
      if (const auto records = baseline_store->load()) engine->restore_baselines(*records);
    }
    std::optional<chronocore::SyntheticSource> synthetic_source;
    std::optional<chronocore::SharedMarkerSource> shared_marker_source;
    if (shared_memory_name) {
      shared_marker_source.emplace(*engine, *shared_memory_name);
      shared_marker_source->start();
    } else {
      synthetic_source.emplace(*engine, regression_after_ms);
      synthetic_source->start();
    }
    std::optional<chronocore::PerfCollector> perf_collector;
    if (target_pid) {
      perf_collector.emplace(*engine, chronocore::PerfCollectorConfig{.target_pid = *target_pid});
      perf_collector->start();
    }
    if (baseline_store) {
      std::thread([engine, baseline_store] {
        while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
          baseline_store->save(engine->baseline_records());
        }
      }).detach();
    }
    chronocore::HttpServer(*engine, port).serve_forever();
  } catch (const std::exception& error) {
    std::cerr << "chronocore-daemon: " << error.what() << '\n';
    return 1;
  }
}
