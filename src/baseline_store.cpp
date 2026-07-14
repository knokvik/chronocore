#include "chronocore/baseline_store.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace chronocore {
namespace {
constexpr std::string_view kFormat = "chronocore-baseline-v1";
}

BaselineStore::BaselineStore(std::filesystem::path path, std::string compatibility_key)
    : path_(std::move(path)), compatibility_key_(std::move(compatibility_key)) {}

std::optional<std::vector<BaselineRecord>> BaselineStore::load() const {
  std::ifstream input(path_);
  if (!input.good()) return std::nullopt;
  std::string line;
  if (!std::getline(input, line) || line != kFormat) return std::nullopt;
  if (!std::getline(input, line) || !line.starts_with("key\t")) return std::nullopt;
  if (line.substr(4) != compatibility_key_) return std::nullopt;

  std::vector<BaselineRecord> records;
  while (std::getline(input, line)) {
    std::istringstream row(line);
    BaselineRecord record;
    std::string count;
    std::string mean;
    std::string m2;
    if (!std::getline(row, record.function, '\t') || !std::getline(row, count, '\t') ||
        !std::getline(row, mean, '\t') || !std::getline(row, m2, '\t')) {
      throw std::runtime_error("malformed baseline record in " + path_.string());
    }
    record.count = std::stoull(count);
    record.mean_ns = std::stod(mean);
    record.m2 = std::stod(m2);
    records.push_back(std::move(record));
  }
  return records;
}

void BaselineStore::save(const std::vector<BaselineRecord>& records) const {
  if (records.empty()) return;
  if (path_.has_parent_path()) std::filesystem::create_directories(path_.parent_path());
  const auto temporary = path_.string() + ".tmp";
  std::ofstream output(temporary, std::ios::trunc);
  if (!output.good()) throw std::runtime_error("cannot write baseline " + temporary);
  output << kFormat << '\n' << "key\t" << compatibility_key_ << '\n';
  for (const auto& record : records) {
    output << record.function << '\t' << record.count << '\t' << record.mean_ns << '\t' << record.m2 << '\n';
  }
  output.close();
  std::filesystem::rename(temporary, path_);
}

}  // namespace chronocore
