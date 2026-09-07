#pragma once

#include <algorithm>
#include <cstddef>
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

} // namespace mkpro::core
