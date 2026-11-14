#pragma once

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mkpro::core::detail {

// A bounded set of ranks. Each upper level marks the nonempty words of
// the level below it. Insertion and minimum extraction need no per-key
// allocation, and can revisit ranks in any order.
class DenseOrderedWorklist {
 public:
  explicit DenseOrderedWorklist(std::size_t capacity) : capacity_(capacity) {
    for (auto size = capacity; size > 0;) {
      const auto words = size / 64U + (size % 64U != 0);
      levels_.emplace_back(words);
      if (words == 1U) break;
      size = words;
    }
  }

  bool empty() const noexcept { return levels_.empty() || levels_.back().front() == 0; }

  void insert(std::size_t rank) noexcept {
    assert(rank < capacity_);
    for (auto& level : levels_) {
      const auto index = rank / 64U;
      auto& word = level[index];
      const bool occupied = word != 0;
      word |= std::uint64_t{1} << (rank % 64U);
      if (occupied) return;
      rank = index;
    }
  }

  std::size_t pop_min() noexcept {
    assert(!empty());
    std::size_t rank = 0;
    for (auto level = levels_.rbegin(); level != levels_.rend(); ++level)
      rank = rank * 64U + static_cast<std::size_t>(std::countr_zero((*level)[rank]));
    const auto minimum = rank;
    for (auto& level : levels_) {
      const auto index = rank / 64U;
      auto& word = level[index];
      word &= ~(std::uint64_t{1} << (rank % 64U));
      if (word != 0) break;
      rank = index;
    }
    return minimum;
  }

 private:
  [[maybe_unused]] std::size_t capacity_;
  std::vector<std::vector<std::uint64_t>> levels_;
};

} // namespace mkpro::core::detail
