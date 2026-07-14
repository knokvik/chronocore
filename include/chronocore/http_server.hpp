#pragma once

#include <cstdint>

#include "chronocore/correlation_engine.hpp"

namespace chronocore {

class HttpServer {
 public:
  HttpServer(const CorrelationEngine& engine, std::uint16_t port);
  void serve_forever() const;
 private:
  const CorrelationEngine& engine_;
  std::uint16_t port_;
};

}  // namespace chronocore
