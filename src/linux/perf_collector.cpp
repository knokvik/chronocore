#include "chronocore/perf_collector.hpp"

#include <asm/unistd.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <linux/perf_event.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

namespace chronocore {
namespace {
int perf_event_open(perf_event_attr* attributes, pid_t pid) {
  return static_cast<int>(syscall(__NR_perf_event_open, attributes, pid, -1, -1, PERF_FLAG_FD_CLOEXEC));
}

template <typename T>
T read_ring(const std::byte* ring, std::size_t size, std::uint64_t position) {
  T value{};
  const auto offset = static_cast<std::size_t>(position % size);
  const auto first = std::min(sizeof(T), size - offset);
  std::memcpy(&value, ring + offset, first);
  if (first < sizeof(T)) std::memcpy(reinterpret_cast<std::byte*>(&value) + first, ring, sizeof(T) - first);
  return value;
}

struct SamplePayload {
  std::uint64_t instruction_pointer;
  std::uint32_t process_id;
  std::uint32_t thread_id;
  std::uint64_t timestamp_ns;
  std::uint32_t cpu;
  std::uint32_t reserved;
  std::uint64_t period;
};
static_assert(sizeof(SamplePayload) == 40);
}  // namespace

class PerfCollector::Impl {
 public:
  Impl(CorrelationEngine& engine, PerfCollectorConfig config) : engine_(engine), config_(config) {}
  ~Impl() { stop(); }
  void start() {
    if (running_) return;
    perf_event_attr attributes{};
    attributes.size = sizeof(attributes);
    attributes.type = PERF_TYPE_HARDWARE;
    attributes.config = PERF_COUNT_HW_CACHE_MISSES;
    attributes.sample_period = config_.sample_period;
    attributes.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD;
    attributes.precise_ip = config_.requested_precise_ip;
    attributes.exclude_kernel = 1;
    attributes.exclude_hv = 1;
    attributes.wakeup_events = 1;
    descriptor_ = perf_event_open(&attributes, static_cast<pid_t>(config_.target_pid));
    if (descriptor_ < 0) throw std::runtime_error("perf_event_open failed: " + std::string(std::strerror(errno)) +
        " (verify perf_event_paranoid, permissions, and PMU precise-IP support)");
    const long page_size = sysconf(_SC_PAGESIZE);
    mapping_size_ = static_cast<std::size_t>(page_size) * 9;
    mapping_ = mmap(nullptr, mapping_size_, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor_, 0);
    if (mapping_ == MAP_FAILED) { close(descriptor_); descriptor_ = -1; throw std::runtime_error("cannot mmap perf ring"); }
    if (ioctl(descriptor_, PERF_EVENT_IOC_RESET, 0) != 0 || ioctl(descriptor_, PERF_EVENT_IOC_ENABLE, 0) != 0) {
      stop(); throw std::runtime_error("cannot enable perf event");
    }
    running_ = true;
    worker_ = std::thread(&Impl::run, this);
  }
  void stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
    if (descriptor_ >= 0) ioctl(descriptor_, PERF_EVENT_IOC_DISABLE, 0);
    if (mapping_ && mapping_ != MAP_FAILED) munmap(mapping_, mapping_size_);
    mapping_ = nullptr; mapping_size_ = 0;
    if (descriptor_ >= 0) close(descriptor_);
    descriptor_ = -1;
  }
 private:
  void run() {
    auto* metadata = static_cast<perf_event_mmap_page*>(mapping_);
    auto* ring = reinterpret_cast<std::byte*>(mapping_) + metadata->data_offset;
    const auto ring_size = static_cast<std::size_t>(metadata->data_size);
    while (running_) {
      const auto head = metadata->data_head;
      std::atomic_thread_fence(std::memory_order_acquire);
      auto tail = metadata->data_tail;
      while (tail < head) {
        const auto header = read_ring<perf_event_header>(ring, ring_size, tail);
        if (header.size < sizeof(header) || header.size > ring_size) { tail = head; break; }
        if (header.type == PERF_RECORD_SAMPLE && header.size >= sizeof(header) + sizeof(SamplePayload)) {
          const auto payload = read_ring<SamplePayload>(ring, ring_size, tail + sizeof(header));
          engine_.ingest_counter({payload.timestamp_ns, payload.period, 0, payload.process_id, payload.thread_id,
                                  payload.cpu, payload.instruction_pointer});
        }
        tail += header.size;
      }
      std::atomic_thread_fence(std::memory_order_release);
      metadata->data_tail = tail;
      std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
  }
  CorrelationEngine& engine_;
  PerfCollectorConfig config_;
  std::atomic<bool> running_{false};
  std::thread worker_;
  int descriptor_{-1};
  void* mapping_{};
  std::size_t mapping_size_{};
};

PerfCollector::PerfCollector(CorrelationEngine& engine, PerfCollectorConfig config) : impl_(std::make_unique<Impl>(engine, config)) {}
PerfCollector::~PerfCollector() = default;
PerfCollector::PerfCollector(PerfCollector&&) noexcept = default;
PerfCollector& PerfCollector::operator=(PerfCollector&&) noexcept = default;
void PerfCollector::start() { impl_->start(); }
void PerfCollector::stop() { impl_->stop(); }
}  // namespace chronocore
