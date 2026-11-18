#pragma once

#include "mkpro/core/layout_search_artifact.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <unordered_map>
#include <vector>

namespace mkpro::core::detail {

// A frontier retains exact typed images for duplicate detection. Keep one
// copy of each shared chunk instead of a full vector of MachineItems per
// pending root. The hashes only index buckets: even metadata-only changes
// get a new identity unless every item compares equal.
class ExactLayoutImageInterner {
 public:
  std::vector<std::size_t> intern(const std::vector<MachineItem>& items) {
    constexpr std::size_t width = 16;
    std::vector<std::size_t> ids;
    ids.reserve(items.size() / width + (items.size() % width != 0U));
    for (std::size_t at = 0; at < items.size(); at += width) {
      const auto count = std::min(width, items.size() - at);
      const auto first = items.begin() + static_cast<std::ptrdiff_t>(at);
      const auto last = first + static_cast<std::ptrdiff_t>(count);
      auto hash = count;
      const auto mix = [&](std::size_t value) { hash = hash * 16777619U ^ value; };
      for (auto item = first; item != last; ++item) {
        mix(static_cast<std::size_t>(item->kind));
        mix(static_cast<std::size_t>(item->opcode));
        mix(static_cast<std::size_t>(item->formal_opcode.value_or(-1)));
        mix(item->roles.size());
        if (!item->roles.empty()) {
          mix(std::hash<std::string>{}(item->roles.front()));
          mix(std::hash<std::string>{}(item->roles.back()));
        }
        if (item->kind == MachineItemKind::Label)
          mix(std::hash<std::string>{}(item->name));
        else if (item->kind == MachineItemKind::Address)
          mix(std::hash<IrTarget>{}(item->target));
      }
      const auto [begin, end] = chunks_.equal_range(hash);
      const auto found = std::find_if(begin, end, [&](const auto& entry) {
        const auto& old = entry.second.items;
        return std::equal(first, last, old.begin(), old.end(), same_layout_search_item);
      });
      if (found != end) {
        ids.push_back(found->second.id);
      } else {
        const auto id = next_++;
        chunks_.emplace(hash, Chunk{id, std::vector<MachineItem>(first, last)});
        ids.push_back(id);
      }
    }
    return ids;
  }

 private:
  struct Chunk { std::size_t id; std::vector<MachineItem> items; };
  std::size_t next_ = 0;
  std::unordered_multimap<std::size_t, Chunk> chunks_;
};

} // namespace mkpro::core::detail
