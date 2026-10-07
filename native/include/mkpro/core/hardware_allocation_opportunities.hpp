#pragma once

#include "mkpro/core/ir.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mkpro::core {

struct HardwareAllocationOpportunity {
  std::string logical_name;
  int register_index = -1;
  std::string rule;
};

// Discover placement requests, not rewrite certificates. The target-specific
// rematerializer must subsequently prove its own value, entry, stack and alias
// preconditions on regenerated code. No source identifier participates in
// discovery: logical identities only carry the request back to the allocator.
inline std::vector<HardwareAllocationOpportunity> hardware_allocation_opportunities(
    const std::vector<MachineItem>& items,
    const std::map<std::string, std::string>& immutable_preloads) {
  std::vector<HardwareAllocationOpportunity> result;
  std::set<std::string> seen;
  for (std::size_t index = 0; index + 1U < items.size(); ++index) {
    const MachineItem& producer = items[index];
    const MachineItem& destination = items[index + 1U];
    if (producer.kind != MachineItemKind::Op || destination.kind != MachineItemKind::Op ||
        producer.raw || destination.raw || producer.manual_interaction.has_value() ||
        destination.manual_interaction.has_value() || producer.opcode < 0x60 ||
        producer.opcode > 0x6e || destination.opcode < 0x40 || destination.opcode > 0x4e ||
        !destination.logical_register_name.has_value())
      continue;
    const std::string reg(1, "0123456789abcde"[producer.opcode - 0x60]);
    const auto value = immutable_preloads.find(reg);
    if (value == immutable_preloads.end() || value->second != "-99999999")
      continue;
    const std::string& name = *destination.logical_register_name;
    if (name.empty() || !seen.insert(name).second)
      continue;
    // A machine fact: canonical-zero X->P3; K P->X3 produces this constant
    // with no extra destination store only when that destination lives in R3.
    result.push_back({name, 3, "zero-underflow-constant-rematerialization"});
  }
  return result;
}

// Existing hardware anchors and user-requested assignments remain hard
// constraints. A provisional lowering may alias physical registers, but its
// logical interference graph, not those provisional opcodes, decides validity.
inline std::optional<std::map<std::string, int>> merge_logical_register_pins(
    const std::map<std::string, int>& fixed,
    const std::map<std::string, int>& requested,
    const std::set<std::string>& logical_names, int color_count) {
  auto result = fixed;
  for (const auto& [name, color] : requested) {
    if (!logical_names.contains(name) || color < 0 || color >= color_count)
      return std::nullopt;
    const auto [entry, inserted] = result.emplace(name, color);
    if (!inserted && entry->second != color)
      return std::nullopt;
  }
  return result;
}

} // namespace mkpro::core
