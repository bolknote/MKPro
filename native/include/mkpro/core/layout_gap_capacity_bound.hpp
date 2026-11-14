#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <span>
#include <vector>

namespace mkpro::core {

// Optimistic padding bound for indivisible, nonnegative component lengths.
// Each gap may independently use any subset of the remaining components.
// Sharing the same component between hypothetical gaps makes the result a
// lower bound, not a feasibility proof. Physical gap identities are preserved.
class LayoutGapCapacityBound {
public:
  LayoutGapCapacityBound(std::span<const int> lengths, int maximum_gap) {
    const int total = std::accumulate(lengths.begin(), lengths.end(), 0);
    const int limit = std::min(std::max(0, maximum_gap), total);
    suffix_fill_.assign(lengths.size() + 1U,
                        std::vector<int>(static_cast<std::size_t>(limit) + 1U, 0));
    suffix_eligible_cells_.assign(
        lengths.size() + 1U,
        std::vector<std::int64_t>(static_cast<std::size_t>(limit) + 1U, 0));
    for (std::size_t index = lengths.size(); index > 0U; --index) {
      const int length = lengths[index - 1U];
      auto& row = suffix_fill_.at(index - 1U);
      const auto& suffix = suffix_fill_.at(index);
      auto& eligible = suffix_eligible_cells_.at(index - 1U);
      const auto& eligible_suffix = suffix_eligible_cells_.at(index);
      for (int capacity = 0; capacity <= limit; ++capacity) {
        const auto slot = static_cast<std::size_t>(capacity);
        row.at(slot) = suffix.at(slot);
        eligible.at(slot) = eligible_suffix.at(slot);
        if (length <= capacity) {
          eligible.at(slot) += length;
          row.at(slot) = std::max(
              row.at(slot),
              length + suffix.at(static_cast<std::size_t>(capacity - length)));
        }
      }
    }
  }

  std::int64_t minimum_padding(std::size_t remaining_index,
                               std::span<const int> capacities,
                               std::span<const int> filled = {}) const {
    const auto& suffix = suffix_fill_.at(remaining_index);
    std::int64_t padding = 0;
    for (std::size_t gap = 0; gap < capacities.size(); ++gap) {
      const int residual = capacities[gap] - (filled.empty() ? 0 : filled[gap]);
      const std::size_t slot = std::min(
          static_cast<std::size_t>(residual), suffix.size() - 1U);
      padding += static_cast<std::int64_t>(residual) - suffix.at(slot);
    }
    return padding;
  }

  // Components are indivisible and cannot fill more than one gap. For each
  // residual capacity t, only components of length <= t can fill gaps whose
  // residual capacity is <= t. Combine that deficit with the independent
  // lower bounds for the other gaps; deadlines are deliberately ignored.
  std::int64_t minimum_single_use_padding(
      std::size_t remaining_index, std::span<const int> capacities,
      std::span<const int> filled = {}) const {
    const auto& suffix = suffix_fill_.at(remaining_index);
    const auto& eligible = suffix_eligible_cells_.at(remaining_index);
    const auto residual_at = [&](std::size_t gap) {
      return capacities[gap] - (filled.empty() ? 0 : filled[gap]);
    };
    const auto gap_bound = [&](int residual) -> std::int64_t {
      const auto slot = std::min(static_cast<std::size_t>(residual),
                                 suffix.size() - 1U);
      return static_cast<std::int64_t>(residual) - suffix.at(slot);
    };
    std::int64_t independent = 0;
    for (std::size_t gap = 0; gap < capacities.size(); ++gap)
      independent += gap_bound(residual_at(gap));
    std::int64_t result = independent;
    for (std::size_t threshold_gap = 0; threshold_gap < capacities.size();
         ++threshold_gap) {
      const int threshold = residual_at(threshold_gap);
      if (threshold <= 0)
        continue;
      std::int64_t small_capacity = 0;
      std::int64_t small_independent = 0;
      for (std::size_t gap = 0; gap < capacities.size(); ++gap) {
        const int residual = residual_at(gap);
        if (residual <= threshold) {
          small_capacity += residual;
          small_independent += gap_bound(residual);
        }
      }
      const auto slot = std::min(static_cast<std::size_t>(threshold),
                                 eligible.size() - 1U);
      const auto small_bound = std::max(small_independent,
                                        small_capacity - eligible.at(slot));
      result = std::max(result, independent - small_independent + small_bound);
    }
    return result;
  }

private:
  std::vector<std::vector<int>> suffix_fill_;
  std::vector<std::vector<std::int64_t>> suffix_eligible_cells_;
};

} // namespace mkpro::core

