#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace chronocore {

// P² estimates a quantile in constant memory. It is intentionally independent
// from the alerting baseline, which uses the exact Welford moments below.
class P2Quantile {
 public:
  explicit P2Quantile(double quantile) : quantile_(quantile) {}

  void add(double value) {
    if (initial_count_ < initial_.size()) {
      initial_[initial_count_++] = value;
      if (initial_count_ == initial_.size()) {
        std::sort(initial_.begin(), initial_.end());
        q_ = initial_;
        n_ = {1., 2., 3., 4., 5.};
        np_ = {1., 1. + 2. * quantile_, 1. + 4. * quantile_, 3. + 2. * quantile_, 5.};
        dn_ = {0., quantile_ / 2., quantile_, (1. + quantile_) / 2., 1.};
      }
      return;
    }

    std::size_t k = 0;
    if (value < q_[0]) {
      q_[0] = value;
    } else if (value >= q_[4]) {
      q_[4] = value;
      k = 3;
    } else {
      while (k < 3 && value >= q_[k + 1]) ++k;
    }
    for (std::size_t i = k + 1; i < 5; ++i) n_[i] += 1.;
    for (std::size_t i = 0; i < 5; ++i) np_[i] += dn_[i];
    for (std::size_t i = 1; i < 4; ++i) adjust(i);
  }

  [[nodiscard]] double value() const {
    if (initial_count_ == 0) return 0.;
    if (initial_count_ < initial_.size()) {
      std::array<double, 5> copy = initial_;
      std::sort(copy.begin(), copy.begin() + static_cast<std::ptrdiff_t>(initial_count_));
      const auto index = static_cast<std::size_t>(std::ceil(quantile_ * initial_count_)) - 1;
      return copy[std::min(index, initial_count_ - 1)];
    }
    return q_[2];
  }

 private:
  void adjust(std::size_t i) {
    const double d = np_[i] - n_[i];
    if (!((d >= 1. && n_[i + 1] - n_[i] > 1.) || (d <= -1. && n_[i - 1] - n_[i] < -1.))) return;
    const double sign = d > 0. ? 1. : -1.;
    const double parabolic = q_[i] + sign / (n_[i + 1] - n_[i - 1]) *
        ((n_[i] - n_[i - 1] + sign) * (q_[i + 1] - q_[i]) / (n_[i + 1] - n_[i]) +
         (n_[i + 1] - n_[i] - sign) * (q_[i] - q_[i - 1]) / (n_[i] - n_[i - 1]));
    if (q_[i - 1] < parabolic && parabolic < q_[i + 1]) {
      q_[i] = parabolic;
    } else {
      const auto direction = sign > 0. ? i + 1 : i - 1;
      q_[i] += sign * (q_[direction] - q_[i]) / (n_[direction] - n_[i]);
    }
    n_[i] += sign;
  }

  double quantile_;
  std::array<double, 5> initial_{};
  std::size_t initial_count_{};
  std::array<double, 5> q_{};
  std::array<double, 5> n_{};
  std::array<double, 5> np_{};
  std::array<double, 5> dn_{};
};

class Welford {
 public:
  void add(double value) {
    ++count_;
    const double delta = value - mean_;
    mean_ += delta / static_cast<double>(count_);
    m2_ += delta * (value - mean_);
  }
  [[nodiscard]] std::size_t count() const { return count_; }
  [[nodiscard]] double mean() const { return mean_; }
  [[nodiscard]] double variance() const { return count_ > 1 ? m2_ / static_cast<double>(count_ - 1) : 0.; }
  [[nodiscard]] double stddev() const { return std::sqrt(variance()); }
  [[nodiscard]] double m2() const { return m2_; }
  void restore(std::size_t count, double mean, double m2) {
    count_ = count;
    mean_ = mean;
    m2_ = m2;
  }
  [[nodiscard]] bool is_high_outlier(double value, std::size_t min_samples = 30, double sigmas = 3.) const {
    return count_ >= min_samples && stddev() > std::numeric_limits<double>::epsilon() && value > mean_ + sigmas * stddev();
  }
 private:
  std::size_t count_{};
  double mean_{};
  double m2_{};
};

}  // namespace chronocore
