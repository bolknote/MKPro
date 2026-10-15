#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
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

// Every composing lane uses the immutable search root, never its current
// parent or incumbent, as the growth origin. Subtract only ordered,
// nonnegative counts so even the largest representable budget cannot overflow.
constexpr bool transaction_growth_fits(
    int original_cells, int candidate_cells, int maximum_growth_cells) {
  return original_cells >= 0 && candidate_cells >= 0 && maximum_growth_cells >= 0 &&
      (candidate_cells <= original_cells ||
       candidate_cells - original_cells <= maximum_growth_cells);
}


// The parent may have earned space, or borrowed some, in a proved earlier
// step. Preserve the immutable root's allowance rather than restarting a
// local growth limit. Saturation loses no representable int-sized candidate.
inline std::optional<int> transaction_remaining_growth_budget(
    int original_cells, int parent_cells, int maximum_growth_cells) {
  if (!transaction_growth_fits(original_cells, parent_cells, maximum_growth_cells))
    return std::nullopt;
  if (parent_cells > original_cells)
    return maximum_growth_cells - (parent_cells - original_cells);
  const int earned = original_cells - parent_cells;
  return maximum_growth_cells > std::numeric_limits<int>::max() - earned
      ? std::numeric_limits<int>::max() : maximum_growth_cells + earned;
}

template <class Node> struct ProvedSearchTransition {
  Node node;
  std::string rule;
  // Equivalence to the parent, not merely a valid CFG. Unknown is false.
  bool proved = false;
};

template <class Node> struct ProvedProductFrontier {
  std::vector<ProvedSearchTransition<Node>> candidates;
  std::size_t expanded = 0;
  std::size_t duplicate = 0;
  std::size_t rejected = 0;
  bool budget_exhausted = false;
};

// Compose one further axis with independently proved parent representations.
// Keep the established frontier outside this lane and fill only its caller's
// remaining slots. Round-robin admission, not parent-major truncation or local
// size ranking, lets later parents reach the downstream atomic closure.
// Parents/edges are conditional artifacts, not publishable incumbents: the
// caller must still close every equation and perform its final-artifact proof.
template <class Node, class Cells, class Equal, class Expand>
ProvedProductFrontier<Node> collect_proved_product_frontier(
    const std::vector<Node>& parents, const std::vector<Node>& already_admitted,
    int original_cells, const TransactionSearchLimits& limits,
    Cells cells, Equal equal, Expand expand) {
  ProvedProductFrontier<Node> result;
  if (limits.maximum_expansions == 0 || limits.maximum_candidates == 0 ||
      limits.maximum_depth == 0)
    return result;
  std::vector<std::vector<ProvedSearchTransition<Node>>> lanes;
  const auto parent_count = std::min(parents.size(), limits.maximum_expansions);
  for (std::size_t i = 0; i < parent_count; ++i) {
    if (!transaction_growth_fits(
            original_cells, cells(parents[i]), limits.maximum_growth_cells)) {
      ++result.rejected;
      continue;
    }
    lanes.push_back(expand(parents[i]));
    ++result.expanded;
  }
  std::vector<std::size_t> next(lanes.size(), 0);
  bool admitted = true;
  while (admitted && result.candidates.size() < limits.maximum_candidates) {
    admitted = false;
    for (std::size_t lane = 0; lane < lanes.size(); ++lane) {
      while (next[lane] < lanes[lane].size()) {
        auto edge = std::move(lanes[lane][next[lane]++]);
        if (!edge.proved || !transaction_growth_fits(
                original_cells, cells(edge.node), limits.maximum_growth_cells)) {
          ++result.rejected;
          continue;
        }
        if (std::any_of(parents.begin(), parents.end(),
                        [&](const Node& old) { return equal(old, edge.node); }) ||
            std::any_of(already_admitted.begin(), already_admitted.end(),
                        [&](const Node& old) { return equal(old, edge.node); }) ||
            std::any_of(result.candidates.begin(), result.candidates.end(),
                        [&](const auto& old) { return equal(old.node, edge.node); })) {
          ++result.duplicate;
          continue;
        }
        result.candidates.push_back(std::move(edge));
        admitted = true;
        break;
      }
      if (result.candidates.size() == limits.maximum_candidates)
        break;
    }
  }
  result.budget_exhausted = parent_count < parents.size();
  for (std::size_t lane = 0; lane < lanes.size(); ++lane)
    result.budget_exhausted = result.budget_exhausted || next[lane] < lanes[lane].size();
  return result;
}

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
TransactionSearchResult<Node> search_proved_transaction_closures(
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
    if (!transition.proved ||
        !transaction_growth_fits(original_cells, count, limits.maximum_growth_cells)) {
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
    // Closure is a relation, not a greedy canonicalizer. A shorter executable
    // artifact can have fewer legal continuations than a neutral alternative
    // (for example after an irreversible address/code overlay). Keep every
    // independently proved closure within the same ordinary search bounds.
    for (auto& closed : finalize(parent.node, parent.path)) {
      if (transaction_growth_fits(original_cells, cells(closed), limits.maximum_growth_cells) &&
          cells(closed) < original_cells &&
          (!result.incumbent || cells(closed) < cells(*result.incumbent)))
        result.incumbent = closed;
      admit({std::move(closed), "final-layout", true}, parent.path, parent.lane);
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

// Preserve the historical optional-finalizer interface and ordering. Clients
// with several proof-valid closures opt into the relation above explicitly;
// an unproved or absent closure never becomes a speculative search edge.
template <class Node, class Cells, class Equal, class Expand, class Finalize>
TransactionSearchResult<Node> search_proved_transactions(
    std::vector<ProvedSearchTransition<Node>> seeds,
    const std::vector<Node>& already_expanded, int original_cells,
    const TransactionSearchLimits& limits, Cells cells, Equal equal,
    Expand expand, Finalize finalize) {
  return search_proved_transaction_closures(
      std::move(seeds), already_expanded, original_cells, limits,
      std::move(cells), std::move(equal), std::move(expand),
      [&](const Node& node, const std::vector<std::string>& path) {
        std::vector<Node> closures;
        if (auto closed = finalize(node, path))
          closures.push_back(std::move(*closed));
        return closures;
      });
}

} // namespace mkpro::core
