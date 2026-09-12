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

} // namespace mkpro::core
