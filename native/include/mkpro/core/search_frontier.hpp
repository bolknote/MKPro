#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core {

// The incumbent is not a search frontier entry. Once a node has been
// expanded, keeping it in a size-ranked beam can starve a larger enabling
// step forever. Admission/proof checks belong to the caller; this helper
// selects only unexpanded identities, with deterministic equal-cost ties.
template <class Node, class Key, class Better>
std::vector<Node> select_unexpanded_search_frontier(
    std::vector<Node> candidates, const std::set<std::string>& expanded,
    std::size_t width, Key key, Better better) {
  std::erase_if(candidates, [&](const Node& node) {
    return expanded.contains(key(node));
  });
  std::stable_sort(candidates.begin(), candidates.end(),
                   [&](const Node& left, const Node& right) {
    if (better(left, right))
      return true;
    if (better(right, left))
      return false;
    return key(left) < key(right);
  });
  std::set<std::string> selected;
  std::erase_if(candidates, [&](const Node& node) {
    return !selected.insert(key(node)).second;
  });
  if (candidates.size() > width)
    candidates.resize(width);
  return candidates;
}

// Exploration may carry an incomplete candidate while a later transaction
// repairs its layout. It must not displace the publishable incumbent until
// the caller's complete-artifact proof succeeds. A failed proof sees only a
// private candidate copy; deterministic ties retain the existing incumbent.
template <class Node, class Better, class Prove>
bool retain_proved_search_incumbent(std::optional<Node>& incumbent,
                                    Node candidate, Better better, Prove prove) {
  if ((incumbent.has_value() && !better(candidate, *incumbent)) || !prove(candidate))
    return false;
  incumbent = std::move(candidate);
  return true;
}

struct TransactionSearchLimits {
  std::size_t maximum_expansions = 8;
  std::size_t maximum_candidates = 48;
  std::size_t maximum_depth = 4;
  int maximum_growth_cells = 4;
};

template <class Node> struct ProvedSearchTransition {
  Node node;
  std::string rule;
  // Equivalence to the parent, not merely a valid CFG. Unknown is false.
  bool proved = false;
};

template <class Node> struct TransactionSearchResult {
  std::optional<Node> incumbent;
  std::size_t expanded = 0;
  std::size_t admitted = 0;
  std::size_t duplicate = 0;
  std::size_t rejected = 0;
  bool budget_exhausted = false;
};

// A bounded search over complete, equivalence-proved transactions, rather
// than a fixed sequence of independently greedy passes. Finalize must return
// only a fully proved executable artifact; an explored intermediate is never
// a publishable result merely because it is shorter.
//
// Distinct seed families receive round-robin expansion slots. Within each
// family smaller unexpanded artifacts run first, with deterministic discovery
// order for ties. A locally larger enabling family therefore cannot be
// starved by a family producing many cheap alternatives. The incumbent does
// not occupy a frontier slot, and its size does not narrow the exploration
// allowance: that allowance is fixed relative to the original artifact.
template <class Node, class Cells, class Equal, class Expand, class Finalize>
TransactionSearchResult<Node> search_proved_transactions(
    std::vector<ProvedSearchTransition<Node>> seeds,
    const std::vector<Node>& already_expanded, int original_cells,
    const TransactionSearchLimits& limits, Cells cells, Equal equal,
    Expand expand, Finalize finalize) {
  struct Entry {
    Node node;
    std::vector<std::string> path;
    std::size_t lane = 0;
    bool expanded = false;
  };
  TransactionSearchResult<Node> result;
  std::vector<Entry> entries;
  std::vector<std::string> lanes;
  const auto admit = [&](ProvedSearchTransition<Node> transition,
                         std::vector<std::string> path, std::size_t lane) {
    const int count = cells(transition.node);
    if (!transition.proved || count < 0 || limits.maximum_growth_cells < 0 ||
        (count > original_cells &&
         count - original_cells > limits.maximum_growth_cells)) {
      ++result.rejected;
      return;
    }
    if (std::any_of(already_expanded.begin(), already_expanded.end(),
                    [&](const Node& old) { return equal(old, transition.node); }) ||
        std::any_of(entries.begin(), entries.end(),
                    [&](const Entry& old) { return equal(old.node, transition.node); })) {
      ++result.duplicate;
      return;
    }
    if (path.size() >= limits.maximum_depth ||
        entries.size() >= limits.maximum_candidates) {
      result.budget_exhausted = true;
      return;
    }
    path.push_back(std::move(transition.rule));
    entries.push_back({std::move(transition.node), std::move(path), lane, false});
    ++result.admitted;
  };
  for (auto& seed : seeds) {
    auto lane = std::find(lanes.begin(), lanes.end(), seed.rule);
    if (lane == lanes.end()) {
      lanes.push_back(seed.rule);
      lane = lanes.end() - 1;
    }
    admit(std::move(seed), {}, static_cast<std::size_t>(lane - lanes.begin()));
  }
  std::size_t next_lane = 0;
  while (result.expanded < limits.maximum_expansions) {
    std::optional<std::size_t> selected;
    for (std::size_t offset = 0; offset < lanes.size(); ++offset) {
      const auto lane = (next_lane + offset) % lanes.size();
      for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].expanded || entries[i].lane != lane)
          continue;
        if (!selected || cells(entries[i].node) < cells(entries[*selected].node))
          selected = i;
      }
      if (selected) {
        next_lane = (lane + 1) % lanes.size();
        break;
      }
    }
    if (!selected)
      break;
    entries[*selected].expanded = true;
    // Child admission can reallocate entries. Keep the parent independent.
    const Entry parent = entries[*selected];
    ++result.expanded;
    const auto closed = finalize(parent.node, parent.path);
    if (closed) {
      if (cells(*closed) < original_cells &&
          (!result.incumbent || cells(*closed) < cells(*result.incumbent)))
        result.incumbent = *closed;
      admit({*closed, "final-layout", true}, parent.path, parent.lane);
    }
    if (parent.path.size() < limits.maximum_depth) {
      for (auto& child : expand(parent.node))
        admit(std::move(child), parent.path, parent.lane);
    }
  }
  result.budget_exhausted = result.budget_exhausted ||
      std::any_of(entries.begin(), entries.end(),
                  [](const Entry& entry) { return !entry.expanded; });
  return result;
}

} // namespace mkpro::core
