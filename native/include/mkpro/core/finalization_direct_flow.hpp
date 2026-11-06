#pragma once

#include "mkpro/core/ir.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core {

// The caller owns the erased command's data/stack proof. This helper only
// relocates typed targets. A real side counter is not an address cache: its
// fetch/return window must remain physical, or a separate joint layout proof
// must restore it. The compiler's empty-return contract is physical 01.
inline std::optional<std::vector<MachineItem>>
retarget_direct_flows_after_cell_erasure(
    const std::vector<MachineItem>& original,
    const std::vector<MachineItem>& reduced, int erased_address,
    AddressSpaceModel model, std::string* failure = nullptr) {
  const auto reject = [&](const std::string& reason)
      -> std::optional<std::vector<MachineItem>> {
    if (failure) *failure = reason;
    return std::nullopt;
  };
  const auto cells = [](const auto& items) {
    return std::count_if(items.begin(), items.end(), [](const auto& item) {
      return item.kind != MachineItemKind::Label;
    });
  };
  if (cells(original) != cells(reduced) + 1 || erased_address < 0 ||
      erased_address >= cells(original))
    return reject("physical geometry does not describe one cell deletion");
  bool side_entry = false;
  try {
    for (const auto& item : original) {
      if (item.formal_opcode &&
          formal_address_info(*item.formal_opcode, model).kind != FormalAddressKind::Official)
        side_entry = true;
      if (item.indirect_flow_formal_targets)
        for (const auto counter : *item.indirect_flow_formal_targets)
          if (formal_address_info(counter, model).kind != FormalAddressKind::Official)
            side_entry = true;
    }
  } catch (...) {
    return reject("a formal entry has no certified address interpretation");
  }
  if (side_entry) {
    PostLayoutControlFlowOptions options;
    options.address_space_model = model;
    options.empty_return_target = 1;
    const auto flow = build_post_layout_control_flow(original, options);
    if (!flow.proved)
      return reject("formal-entry erasure requires authoritative control flow");
    for (const auto& state : flow.execution_states) {
      if (!state.formal_opcode ||
          formal_address_info(*state.formal_opcode, model).kind == FormalAddressKind::Official)
        continue;
      if (state.address >= erased_address || state.operand_item_index)
        return reject("cell erasure moves an opaque fetch/return window; joint relayout is required");
      const auto& item = original[state.item_index];
      const int family = item.opcode & 0xf0;
      const bool indirect_flow = family == 0x70 || family == 0x80 || family == 0x90 ||
          family == 0xa0 || family == 0xc0 || family == 0xe0;
      if (item.kind != MachineItemKind::Op || item.raw ||
          opcode_by_code(item.opcode).takes_address || indirect_flow || item.opcode == 0x50)
        return reject("opaque control requires an independent continuation relocation proof");
    }
  }
  auto retargeted = reduced;
  for (auto& item : retargeted) {
    if (item.indirect_flow_targets)
      for (auto& target : *item.indirect_flow_targets)
        if (auto* numeric = std::get_if<int>(&target); numeric && *numeric > erased_address)
          --*numeric;
    if (item.kind != MachineItemKind::Address || !std::holds_alternative<int>(item.target))
      continue;
    const int old_target = std::get<int>(item.target);
    if (item.formal_opcode &&
        formal_address_info(*item.formal_opcode, model).kind != FormalAddressKind::Official) {
      if (formal_address_info(*item.formal_opcode, model).actual != old_target ||
          old_target >= erased_address)
        return reject("a real side entry cannot be retargeted as an ordinary operand");
      continue;
    }
    if (old_target < erased_address) continue;
    if (std::find(item.roles.begin(), item.roles.end(), "exec") != item.roles.end() ||
        (item.comment && item.comment->find("address/code overlay for ") != std::string::npos))
      return reject("shifted direct operand is executable or changed kind");
    if (old_target > erased_address) item.target = old_target - 1;
    item.formal_opcode.reset();
  }
  return retargeted;
}

} // namespace mkpro::core

