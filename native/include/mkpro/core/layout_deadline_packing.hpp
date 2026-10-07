#pragma once

#include "mkpro/core/layout_gap_capacity_bound.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace mkpro::core {

struct DeadlinePackingComponent {
  int cells = 0;
  std::optional<int> latest_start;
};

struct DeadlinePackingGap {
  int start = 0;
  int cells = 0;
};

struct DeadlinePackingResult {
  // -1 is the unbounded trailing region; other entries index gaps.
  std::vector<int> assignment;
  int padding = 0;
  std::size_t work = 0;
  bool proved = false;
  bool exhausted = false;
};

// Pack indivisible components while retaining every nondominated deadline
// profile. This is a geometry witness, never a machine-equivalence proof.
// A start constraint s <= d is a completion deadline d + length. EDF is
// therefore ordered by d + length, not by d.
//
// For fixed gap fills, cumulative bounded work at every completion deadline
// is a sufficient future-feasibility signature. A componentwise smaller
// profile dominates a larger one; lexicographic order alone does not.
inline DeadlinePackingResult pack_components_with_deadlines(
    const std::vector<DeadlinePackingComponent>& components,
    const std::vector<DeadlinePackingGap>& gaps, int trailing_start,
    int maximum_padding, std::size_t maximum_states = 20000,
    std::size_t maximum_work = 2000000) {
  DeadlinePackingResult result;
  if (maximum_padding < 0 || trailing_start < 0)
    return result;
  std::vector<int> lengths, capacities;
  std::vector<std::int64_t> deadlines;
  std::int64_t capacity = 0;
  std::int64_t total_cells = 0;
  for (const auto& component : components) {
    if (component.cells <= 0)
      return result;
    total_cells += component.cells;
    if (total_cells > std::numeric_limits<int>::max())
      return result;
    lengths.push_back(component.cells);
    if (component.latest_start)
      deadlines.push_back(static_cast<std::int64_t>(*component.latest_start) +
                          component.cells);
  }
  std::int64_t previous_end = 0;
  for (const auto& gap : gaps) {
    if (gap.start < previous_end || gap.cells < 0 ||
        static_cast<std::int64_t>(gap.start) + gap.cells > trailing_start)
      return result;
    previous_end = static_cast<std::int64_t>(gap.start) + gap.cells;
    capacities.push_back(gap.cells);
    capacity += gap.cells;
  }
  if (capacity > std::numeric_limits<int>::max())
    return result;
  std::sort(deadlines.begin(), deadlines.end());
  deadlines.erase(std::unique(deadlines.begin(), deadlines.end()), deadlines.end());
  const int largest_gap = capacities.empty() ? 0 :
      *std::max_element(capacities.begin(), capacities.end());
  const LayoutGapCapacityBound capacity_bound(lengths, largest_gap);
  const auto charge = [&](std::size_t amount = 1U) {
    if (amount > maximum_work - result.work) {
      result.exhausted = true;
      return false;
    }
    result.work += amount;
    return true;
  };
  struct State {
    std::vector<std::int64_t> demand;
    std::vector<int> assignment;
  };
  using Totals = std::vector<int>;
  using Frontier = std::map<Totals, std::vector<State>>;
  Frontier states;
  states[Totals(gaps.size(), 0)].push_back(
      State{std::vector<std::int64_t>((gaps.size() + 1U) * deadlines.size(), 0), {}});
  if (maximum_states == 0) {
    result.exhausted = true;
    return result;
  }

  for (std::size_t index = 0; index < components.size(); ++index) {
    Frontier next;
    std::size_t retained = 0;
    const auto& component = components[index];
    std::optional<std::size_t> deadline_rank;
    if (component.latest_start) {
      const auto end = static_cast<std::int64_t>(*component.latest_start) + component.cells;
      deadline_rank = static_cast<std::size_t>(
          std::lower_bound(deadlines.begin(), deadlines.end(), end) - deadlines.begin());
    }
    for (const auto& [totals, alternatives] : states) {
      for (const auto& state : alternatives) {
        // A trailing choice is explicit in the profile, not "no constraint".
        for (std::size_t choice = 0; choice <= gaps.size(); ++choice) {
          if (!charge())
            return result;
          const bool trailing = choice == gaps.size();
          Totals extended = totals;
          if (!trailing) {
            if (component.cells > gaps[choice].cells - extended[choice])
              continue;
            extended[choice] += component.cells;
          }
          if (capacity_bound.minimum_padding(index + 1U, capacities, extended) >
              maximum_padding)
            continue;
          State candidate = state;
          candidate.assignment.push_back(trailing ? -1 : static_cast<int>(choice));
          if (deadline_rank) {
            const int start = trailing ? trailing_start : gaps[choice].start;
            bool feasible = true;
            for (std::size_t rank = *deadline_rank; rank < deadlines.size(); ++rank) {
              if (!charge())
                return result;
              auto& demand = candidate.demand[choice * deadlines.size() + rank];
              demand += component.cells;
              if (static_cast<std::int64_t>(start) + demand > deadlines[rank])
                feasible = false;
            }
            if (!feasible)
              continue;
          }
          auto& bucket = next[extended];
          const auto dominates = [&](const State& left, const State& right) {
            for (std::size_t slot = 0; slot < left.demand.size(); ++slot) {
              if (!charge())
                return false;
              if (left.demand[slot] > right.demand[slot])
                return false;
            }
            return true;
          };
          bool dominated = false;
          for (const auto& existing : bucket) {
            if (dominates(existing, candidate)) {
              dominated = true;
              break;
            }
            if (result.exhausted)
              return result;
          }
          if (dominated)
            continue;
          for (auto old = bucket.begin(); old != bucket.end();) {
            if (dominates(candidate, *old)) {
              old = bucket.erase(old);
              --retained;
            } else {
              ++old;
            }
            if (result.exhausted)
              return result;
          }
          bucket.push_back(std::move(candidate));
          if (++retained > maximum_states) {
            result.exhausted = true;
            return result;
          }
        }
      }
    }
    states = std::move(next);
    if (states.empty())
      return result;
  }
  for (const auto& [totals, alternatives] : states) {
    std::int64_t padding = capacity;
    for (const int fill : totals)
      padding -= fill;
    if (padding > maximum_padding)
      continue;
    for (const auto& state : alternatives) {
      if (!result.proved || padding < result.padding ||
          (padding == result.padding && state.assignment < result.assignment)) {
        result.assignment = state.assignment;
        result.padding = static_cast<int>(padding);
        result.proved = true;
      }
    }
  }
  return result;
}

} // namespace mkpro::core

