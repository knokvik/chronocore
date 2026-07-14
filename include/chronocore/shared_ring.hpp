#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "chronocore/types.hpp"

namespace chronocore {

// Fixed-size, cache-line-sized marker passed from one application producer to
// one daemon consumer through POSIX shared memory.
struct alignas(64) AppMarker {
  std::uint64_t sequence{};
  Nanoseconds timestamp_ns{};
  Nanoseconds latency_ns{};
  std::uint32_t process_id{};
  std::uint32_t thread_id{};
  std::uint32_t cpu{};
  char function[24]{};
};
static_assert(sizeof(AppMarker) == 64);

class SharedMarkerRing {
 public:
  static SharedMarkerRing create(const std::string& name, std::size_t capacity = 4096);
  static SharedMarkerRing open(const std::string& name);
  static bool unlink(const std::string& name);
  SharedMarkerRing() = default;
  ~SharedMarkerRing();
  SharedMarkerRing(SharedMarkerRing&& other) noexcept;
  SharedMarkerRing& operator=(SharedMarkerRing&& other) noexcept;
  SharedMarkerRing(const SharedMarkerRing&) = delete;
  SharedMarkerRing& operator=(const SharedMarkerRing&) = delete;
  [[nodiscard]] bool publish(const AppMarker& marker) const;
  [[nodiscard]] bool consume(AppMarker& marker) const;
  [[nodiscard]] std::size_t capacity() const;

 private:
  struct Header;
  SharedMarkerRing(int descriptor, void* mapping, std::size_t mapping_size);
  void reset();
  int descriptor_{-1};
  void* mapping_{};
  std::size_t mapping_size_{};
};

AppEvent to_app_event(const AppMarker& marker);

}  // namespace chronocore
