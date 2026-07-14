#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "chronocore/correlation_engine.hpp"

namespace chronocore {

// The identity must include at least the target build ID, CPU model, and
// collector configuration. A mismatched baseline is ignored, never blended.
class BaselineStore {
 public:
  BaselineStore(std::filesystem::path path, std::string compatibility_key);
  [[nodiscard]] std::optional<std::vector<BaselineRecord>> load() const;
  void save(const std::vector<BaselineRecord>& records) const;

 private:
  std::filesystem::path path_;
  std::string compatibility_key_;
};

}  // namespace chronocore
