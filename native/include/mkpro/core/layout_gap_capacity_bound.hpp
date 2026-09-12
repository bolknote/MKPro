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
    for (std::size_t index = lengths.size(); index > 0U; --index) {
      const int length = lengths[index - 1U];
      auto& row = suffix_fill_.at(index - 1U);
      const auto& suffix = suffix_fill_.at(index);
      for (int capacity = 0; capacity <= limit; ++capacity) {
        const auto slot = static_cast<std::size_t>(capacity);
        row.at(slot) = suffix.at(slot);
        if (length <= capacity) {
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

private:
  std::vector<std::vector<int>> suffix_fill_;
};

} // namespace mkpro::core

