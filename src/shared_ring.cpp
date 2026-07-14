#include "chronocore/shared_ring.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace chronocore {
namespace {
constexpr std::uint64_t kMagic = 0x4348524f4e4f5245ULL;  // "CHRONORE"
constexpr std::uint32_t kVersion = 1;
void require_valid_name(const std::string& name) {
  if (name.empty() || name.front() != '/') throw std::invalid_argument("shared-memory name must begin with '/'");
}
}  // namespace

struct alignas(64) SharedMarkerRing::Header {
  std::uint64_t magic{};
  std::uint32_t version{};
  std::uint32_t capacity{};
  std::atomic<std::uint64_t> write_index{};
  std::atomic<std::uint64_t> read_index{};
};

SharedMarkerRing::SharedMarkerRing(int descriptor, void* mapping, std::size_t mapping_size)
    : descriptor_(descriptor), mapping_(mapping), mapping_size_(mapping_size) {}

SharedMarkerRing SharedMarkerRing::create(const std::string& name, std::size_t capacity) {
  require_valid_name(name);
  if (capacity < 2 || capacity > UINT32_MAX) throw std::invalid_argument("invalid shared-ring capacity");
  const int descriptor = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
  if (descriptor < 0) throw std::runtime_error("cannot create shared ring " + name);
  const auto size = sizeof(Header) + capacity * sizeof(AppMarker);
  if (ftruncate(descriptor, static_cast<off_t>(size)) != 0) { close(descriptor); shm_unlink(name.c_str()); throw std::runtime_error("cannot size shared ring"); }
  void* mapping = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
  if (mapping == MAP_FAILED) { close(descriptor); shm_unlink(name.c_str()); throw std::runtime_error("cannot map shared ring"); }
  std::memset(mapping, 0, size);
  auto* header = static_cast<Header*>(mapping);
  header->magic = kMagic;
  header->version = kVersion;
  header->capacity = static_cast<std::uint32_t>(capacity);
  return {descriptor, mapping, size};
}

SharedMarkerRing SharedMarkerRing::open(const std::string& name) {
  require_valid_name(name);
  const int descriptor = shm_open(name.c_str(), O_RDWR, 0);
  if (descriptor < 0) throw std::runtime_error("cannot open shared ring " + name);
  struct stat status {};
  if (fstat(descriptor, &status) != 0 || status.st_size < static_cast<off_t>(sizeof(Header))) { close(descriptor); throw std::runtime_error("invalid shared ring"); }
  void* mapping = mmap(nullptr, static_cast<std::size_t>(status.st_size), PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
  if (mapping == MAP_FAILED) { close(descriptor); throw std::runtime_error("cannot map shared ring"); }
  const auto* header = static_cast<const Header*>(mapping);
  const auto minimum_size = static_cast<off_t>(sizeof(Header) + header->capacity * sizeof(AppMarker));
  if (header->magic != kMagic || header->version != kVersion || status.st_size < minimum_size) {
    const auto reason = "incompatible shared ring (magic=" + std::to_string(header->magic) +
        ", version=" + std::to_string(header->version) + ", capacity=" + std::to_string(header->capacity) +
        ", size=" + std::to_string(status.st_size) + ")";
    munmap(mapping, static_cast<std::size_t>(status.st_size)); close(descriptor); throw std::runtime_error(reason);
  }
  return {descriptor, mapping, static_cast<std::size_t>(status.st_size)};
}

bool SharedMarkerRing::unlink(const std::string& name) { require_valid_name(name); return shm_unlink(name.c_str()) == 0; }
SharedMarkerRing::~SharedMarkerRing() { reset(); }
SharedMarkerRing::SharedMarkerRing(SharedMarkerRing&& other) noexcept { *this = std::move(other); }
SharedMarkerRing& SharedMarkerRing::operator=(SharedMarkerRing&& other) noexcept {
  if (this != &other) { reset(); descriptor_ = other.descriptor_; mapping_ = other.mapping_; mapping_size_ = other.mapping_size_; other.descriptor_ = -1; other.mapping_ = nullptr; other.mapping_size_ = 0; }
  return *this;
}
void SharedMarkerRing::reset() { if (mapping_) munmap(mapping_, mapping_size_); if (descriptor_ >= 0) close(descriptor_); mapping_ = nullptr; descriptor_ = -1; mapping_size_ = 0; }

bool SharedMarkerRing::publish(const AppMarker& marker) const {
  auto* header = static_cast<Header*>(mapping_);
  const auto write = header->write_index.load(std::memory_order_relaxed);
  if (write - header->read_index.load(std::memory_order_acquire) >= header->capacity) return false;
  auto* markers = reinterpret_cast<AppMarker*>(header + 1);
  markers[write % header->capacity] = marker;
  header->write_index.store(write + 1, std::memory_order_release);
  return true;
}

bool SharedMarkerRing::consume(AppMarker& marker) const {
  auto* header = static_cast<Header*>(mapping_);
  const auto read = header->read_index.load(std::memory_order_relaxed);
  if (read == header->write_index.load(std::memory_order_acquire)) return false;
  const auto* markers = reinterpret_cast<const AppMarker*>(header + 1);
  marker = markers[read % header->capacity];
  header->read_index.store(read + 1, std::memory_order_release);
  return true;
}

std::size_t SharedMarkerRing::capacity() const { return static_cast<const Header*>(mapping_)->capacity; }
AppEvent to_app_event(const AppMarker& marker) { return {marker.sequence, marker.timestamp_ns, marker.function, marker.latency_ns, marker.process_id, marker.thread_id, marker.cpu}; }

}  // namespace chronocore
