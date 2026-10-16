#include "mkpro/core/post_layout_packed_sign.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/post_layout_progress_bounds.hpp"

#include <algorithm>
#include <deque>
#include <exception>
#include <optional>
#include <set>
#include <string>

namespace mkpro::core {

PackedSignInputProof prove_post_layout_packed_sign_inputs(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<bool>& automatic_entry_lift,
    const std::vector<std::size_t>& requested_states,
    const std::vector<PreloadReport>* preloads, std::size_t maximum_work) {
  PackedSignInputProof result;
  constexpr std::string_view registers = "0123456789abcde";
  const auto count = flow.execution_states.size();
  if (requested_states.empty()) return result;
  if (!flow.proved || automatic_entry_lift.size() != count ||
      flow.execution_successors.size() != count) return result;

  std::map<std::string, const PreloadReport*> words;
  if (preloads) {
    for (const auto& preload : *preloads) {
      try {
        const auto name = register_from_text(preload.register_name);
        if (name.size() != 1U ||
            registers.find(name[0]) == std::string_view::npos ||
            !words.emplace(name, &preload).second)
          return {};
      } catch (const std::exception&) {
        return {};
      }
    }
  }
  const auto charge = [&] {
    if (result.work >= maximum_work) {
      result.exhausted = true;
      result.signs.clear();
      return false;
    }
    ++result.work;
    return true;
  };
  std::vector<std::vector<std::size_t>> predecessors(count);
  for (std::size_t state = 0; state < count; ++state) {
    if (!charge()) return result;
    if (flow.execution_states[state].item_index >= items.size()) return {};
    for (const auto next : flow.execution_successors[state]) {
      if (!charge()) return result;
      if (next >= count) return {};
      predecessors[next].push_back(state);
    }
  }
  std::set<std::size_t> external;
  for (const auto& entry : flow.external_entries)
    external.insert(entry.entry.item_index);
  const auto transparent = [&](std::size_t state) {
    const auto& item = items[flow.execution_states[state].item_index];
    if (item.kind != MachineItemKind::Op || item.raw || item.manual_interaction)
      return false;
    const int family = item.opcode & 0xf0;
    return (item.opcode >= 0x40 && item.opcode <= 0x4e) || family == 0xb0 ||
        item.opcode == 0x51 || item.opcode == 0x52 || item.opcode == 0x53 ||
        item.opcode == 0x54 || family == 0x80 || family == 0xa0;
  };
  // Backward must proof with grounded producer leaves. A cycle cannot prove
  // itself; every predecessor dependency must independently become ready.
  const auto originate = [&](const std::vector<std::size_t>& roots,
                             const auto& producer) {
    if (roots.empty()) return false;
    std::deque<std::size_t> pending(roots.begin(), roots.end()), ready;
    std::set<std::size_t> seen, proved;
    std::map<std::size_t, std::size_t> unresolved;
    std::map<std::size_t, std::vector<std::size_t>> users;
    while (!pending.empty()) {
      const auto state = pending.front();
      pending.pop_front();
      if (!seen.insert(state).second) continue;
      if (!charge() || external.contains(flow.execution_states[state].item_index))
        return false;
      if (producer(state)) {
        proved.insert(state);
        ready.push_back(state);
        continue;
      }
      if (!transparent(state) || predecessors[state].empty()) return false;
      unresolved[state] = predecessors[state].size();
      for (const auto previous : predecessors[state]) {
        if (!charge()) return false;
        users[previous].push_back(state);
        pending.push_back(previous);
      }
    }
    while (!ready.empty()) {
      const auto state = ready.front();
      ready.pop_front();
      for (const auto next : users[state]) {
        if (!charge()) return false;
        if (--unresolved[next] == 0U) {
          proved.insert(next);
          ready.push_back(next);
        }
      }
    }
    return std::all_of(roots.begin(), roots.end(),
                       [&](auto root) { return proved.contains(root); });
  };

  std::set<std::size_t> remaining(requested_states.begin(), requested_states.end());
  for (auto it = remaining.begin(); it != remaining.end();) {
    const auto state = *it;
    if (state >= count) return {};
    if (external.contains(flow.execution_states[state].item_index)) {
      it = remaining.erase(it);
      continue;
    }
    const bool positive = originate(predecessors[state], [&](auto previous) {
      const auto& item = items[flow.execution_states[previous].item_index];
      return !item.raw && item.opcode >= 0x37 && item.opcode <= 0x3a;
    });
    if (result.exhausted) return result;
    if (positive) {
      result.signs.emplace(state, 1);
      it = remaining.erase(it);
    } else {
      ++it;
    }
  }
  if (remaining.empty() || !preloads) return result;
  const auto progress = prove_post_layout_monotone_progress(
      items, flow, automatic_entry_lift, *preloads, maximum_work - result.work);
  if (progress.work > maximum_work - result.work) {
    result.work = maximum_work;
    result.exhausted = true;
    result.signs.clear();
    return result;
  }
  result.work += progress.work;
  if (progress.exhausted) {
    result.exhausted = true;
    result.signs.clear();
    return result;
  }
  if (!progress.witness) return result;
  const auto& witness = *progress.witness;
  if (witness.source_state >= count || witness.target_state >= count) return result;
  if (static_cast<std::size_t>(witness.register_index) >= registers.size()) return result;
  const auto word = words.find(std::string(1, registers[static_cast<std::size_t>(witness.register_index)]));
  if (word == words.end() || word->second->value != "0" ||
      predecessors[witness.source_state].size() != 1U ||
      predecessors[witness.target_state].size() != 1U ||
      predecessors[witness.target_state].front() != witness.source_state)
    return result;
  const auto subtraction = predecessors[witness.source_state].front();
  if (items[flow.execution_states[subtraction].item_index].opcode != 0x11 ||
      !originate(predecessors[subtraction], [&](auto previous) {
        const auto& item = items[flow.execution_states[previous].item_index];
        return !item.raw && item.opcode == 0x40 + witness.register_index;
      })) {
    if (result.exhausted) result.signs.clear();
    return result;
  }
  for (const auto state : remaining) {
    // This is an edge certificate, not a fact on every incoming path to an
    // arbitrary join. The singleton predecessor check above is mandatory.
    const bool negative = state == witness.target_state ||
        originate(predecessors[state], [&](auto previous) {
          return previous == witness.target_state && transparent(previous);
        });
    if (result.exhausted) {
      result.signs.clear();
      return result;
    }
    if (negative) result.signs.emplace(state, -1);
  }
  return result;
}

} // namespace mkpro::core
