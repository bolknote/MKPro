#include "mkpro/core/dark_side_suffix_helper.hpp"

#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace mkpro::core {

namespace {

constexpr int kSideSpaceOrdinalBase = 112; // B2 maps to physical 00.
constexpr int kSideSpaceLastPhysical = 47; // F9.
constexpr int kExplicitReturnPhysical = 48;
constexpr std::string_view kBoundaryMarker = "dark-side suffix boundary return";

struct ArtifactIndex {
  std::vector<int> item_addresses;
  std::map<std::string, int> label_addresses;
  std::map<std::string, std::size_t> label_items;
  std::set<std::string> duplicate_labels;
  std::map<int, std::size_t> cell_items;
  int cells = 0;
};

ArtifactIndex index_artifact(const std::vector<MachineItem>& items) {
  ArtifactIndex index;
  index.item_addresses.resize(items.size(), 0);
  int address = 0;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    index.item_addresses.at(item_index) = address;
    if (item.kind == MachineItemKind::Label) {
      if (index.label_addresses.contains(item.name))
        index.duplicate_labels.insert(item.name);
      index.label_addresses[item.name] = address;
      index.label_items[item.name] = item_index;
      continue;
    }
    index.cell_items[address] = item_index;
    ++address;
  }
  index.cells = address;
  return index;
}

std::optional<int> resolved_ir_target(const IrTarget& target,
                                      const ArtifactIndex& index) {
  if (const auto* numeric = std::get_if<int>(&target))
    return *numeric;
  const auto* label = std::get_if<std::string>(&target);
  if (label == nullptr || index.duplicate_labels.contains(*label))
    return std::nullopt;
  const auto address = index.label_addresses.find(*label);
  return address == index.label_addresses.end() ? std::nullopt
                                                : std::optional<int>(address->second);
}

bool is_side_space_alias(const FormalAddressInfo& info);

int formal_opcode_for_side_entry(int physical_address, AddressSpaceModel model) {
  const int ordinal = kSideSpaceOrdinalBase + physical_address;
  // Formal bytes are not unique once their low nibble may be A..F: EB and F1
  // both have ordinal 151 and both map to physical 39.  Prefer the lowest byte
  // (EB), which avoids an F-prefixed alias whenever a non-F spelling exists.
  for (int high = 0x0b; high <= 0x0f; ++high) {
    const int low = ordinal - high * 10;
    if (low < 0 || low > 0x0f)
      continue;
    const int opcode = high * 16 + low;
    try {
      const FormalAddressInfo info = formal_address_info(opcode, model);
      if (is_side_space_alias(info) && info.actual == physical_address)
        return opcode;
    } catch (const std::exception&) {
      continue;
    }
  }
  return -1;
}

bool is_side_space_alias(const FormalAddressInfo& info) {
  return (info.kind == FormalAddressKind::LongSide || info.kind == FormalAddressKind::Dark) &&
         !info.one_command;
}

IrKind basic_kind_for_opcode(int opcode) {
  const std::vector<IrOp> raised =
      raise_machine_to_ir({MachineItem::op(opcode, opcode_by_code(opcode).name)});
  return raised.empty() ? IrKind::Plain : raised.front().kind;
}

bool is_indirect_flow_kind(IrKind kind) {
  return kind == IrKind::IndirectJump || kind == IrKind::IndirectCall ||
         kind == IrKind::IndirectCondJump;
}

bool is_straight_line_body_opcode(int opcode) {
  if (opcode_by_code(opcode).takes_address)
    return false;
  switch (basic_kind_for_opcode(opcode)) {
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
  case IrKind::IndirectCondJump:
  case IrKind::Return:
  case IrKind::Stop:
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Call:
  case IrKind::Loop:
  case IrKind::OrphanAddress:
  case IrKind::Label:
    return false;
  case IrKind::Store:
  case IrKind::Recall:
  case IrKind::IndirectStore:
  case IrKind::IndirectRecall:
  case IrKind::Plain:
    return true;
  }
  return false;
}

std::optional<std::size_t> previous_cell_item(const std::vector<MachineItem>& items,
                                              std::size_t before) {
  while (before > 0) {
    --before;
    if (items.at(before).kind != MachineItemKind::Label)
      return before;
  }
  return std::nullopt;
}

bool has_official_fallthrough_barrier(const std::vector<MachineItem>& items,
                                      std::size_t helper_label_item) {
  const std::optional<std::size_t> previous = previous_cell_item(items, helper_label_item);
  if (!previous.has_value())
    return false;
  const MachineItem& cell = items.at(*previous);
  if (cell.kind == MachineItemKind::Op) {
    const IrKind kind = basic_kind_for_opcode(cell.opcode);
    return kind == IrKind::Stop || kind == IrKind::Return || kind == IrKind::IndirectJump;
  }
  if (cell.kind != MachineItemKind::Address)
    return false;
  const std::optional<std::size_t> flow = previous_cell_item(items, *previous);
  return flow.has_value() && items.at(*flow).kind == MachineItemKind::Op &&
         items.at(*flow).opcode == 0x51;
}

bool verify_official_fallthrough_mode(
    const std::vector<MachineItem>& items, const ArtifactIndex& index,
    std::size_t helper_label_item, const std::string& helper_label,
    const DarkSideSuffixHelperOptions& options, bool final_artifact,
    DarkSideSuffixHelperProof* proof, std::vector<std::string>& reasons) {
  if (!options.proved_official_fallthrough.has_value()) {
    if (!has_official_fallthrough_barrier(items, helper_label_item)) {
      reasons.push_back(
          "official control can fall through into the helper without an explicit predecessor "
          "proof");
      return false;
    }
    return true;
  }

  const DarkSideOfficialFallthroughEntry& claimed =
      *options.proved_official_fallthrough;
  if (claimed.entry_label.empty() || claimed.entry_label != helper_label) {
    reasons.push_back(
        "proved official fallthrough entry does not name the helper root label");
    return false;
  }
  if (claimed.predecessor_item_index >= items.size()) {
    reasons.push_back("proved official fallthrough predecessor item is outside the artifact");
    return false;
  }

  const std::optional<std::size_t> actual_predecessor =
      previous_cell_item(items, helper_label_item);
  if (!actual_predecessor.has_value() ||
      *actual_predecessor != claimed.predecessor_item_index) {
    reasons.push_back(
        "proved official fallthrough predecessor is not the helper's unique immediate "
        "physical predecessor");
    return false;
  }
  const MachineItem& predecessor = items.at(*actual_predecessor);
  const auto root_address = index.label_addresses.find(helper_label);
  if (root_address == index.label_addresses.end() ||
      index.item_addresses.at(*actual_predecessor) != root_address->second - 1) {
    reasons.push_back(
        "proved official fallthrough predecessor is not immediately before the helper entry");
    return false;
  }
  if (predecessor.kind != MachineItemKind::Op ||
      !is_straight_line_body_opcode(predecessor.opcode)) {
    reasons.push_back(
        "proved official fallthrough predecessor is not a straight-line executable command");
    return false;
  }
  if (has_official_fallthrough_barrier(items, helper_label_item)) {
    reasons.push_back(
        "proved official fallthrough predecessor is fenced by a control-flow barrier");
    return false;
  }

  const int expected_continuation_address =
      final_artifact ? kExplicitReturnPhysical : kExplicitReturnPhysical + 1;
  const auto continuation = index.cell_items.find(expected_continuation_address);
  if (continuation == index.cell_items.end() ||
      items.at(continuation->second).kind != MachineItemKind::Op) {
    reasons.push_back(
        final_artifact
            ? "final dual-mode artifact has no executable official continuation at physical 48"
            : "dual-mode suffix has no executable continuation after the removable В/О");
    return false;
  }

  if (proof != nullptr) {
    proof->official_fallthrough_proved = true;
    proof->official_fallthrough_predecessor_item_index = *actual_predecessor;
    proof->official_fallthrough_predecessor_address =
        index.item_addresses.at(*actual_predecessor);
    proof->official_fallthrough_entry_label = helper_label;
    proof->official_continuation_address = kExplicitReturnPhysical;
    proof->official_continuation_item_index = continuation->second;
    proof->official_continuation_opcode = items.at(continuation->second).opcode;
  }
  return true;
}

std::optional<int> referenced_physical_address(const MachineItem& address,
                                               const ArtifactIndex& index,
                                               AddressSpaceModel model) {
  if (address.kind != MachineItemKind::Address)
    return std::nullopt;
  if (address.formal_opcode.has_value()) {
    try {
      return formal_address_info(*address.formal_opcode, model).actual;
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }
  if (const auto* label = std::get_if<std::string>(&address.target)) {
    const auto found = index.label_addresses.find(*label);
    if (found == index.label_addresses.end())
      return std::nullopt;
    return found->second;
  }
  return std::get<int>(address.target);
}

std::optional<std::string> unique_label_at_address(const ArtifactIndex& index,
                                                   int address) {
  std::optional<std::string> result;
  for (const auto& [label, label_address] : index.label_addresses) {
    if (label_address != address || index.duplicate_labels.contains(label))
      continue;
    if (result.has_value())
      return std::nullopt;
    result = label;
  }
  return result;
}

void append_comment(MachineItem& item, const std::string& suffix) {
  if (item.comment.has_value() && !item.comment->empty())
    item.comment = *item.comment + "; " + suffix;
  else
    item.comment = suffix;
}

std::map<std::string, DarkSideSuffixEntry> entry_map(const DarkSideSuffixHelperProof& proof) {
  std::map<std::string, DarkSideSuffixEntry> entries;
  for (const DarkSideSuffixEntry& entry : proof.entries) {
    if (!entry.label.empty())
      entries.emplace(entry.label, entry);
  }
  return entries;
}

DarkSideSuffixHelperOptions
shifted_options_after_erasure(const DarkSideSuffixHelperOptions& options, std::size_t erased_item) {
  DarkSideSuffixHelperOptions shifted = options;
  if (shifted.proved_official_fallthrough.has_value() &&
      shifted.proved_official_fallthrough->predecessor_item_index > erased_item) {
    --shifted.proved_official_fallthrough->predecessor_item_index;
  }
  shifted.proved_indirect_flow_targets.clear();
  const auto& final_targets = options.proved_rebound_indirect_flow_targets.empty()
                                  ? options.proved_indirect_flow_targets
                                  : options.proved_rebound_indirect_flow_targets;
  for (const auto& [item, targets] : final_targets) {
    if (item == erased_item)
      continue;
    shifted.proved_indirect_flow_targets.emplace(item > erased_item ? item - 1U : item, targets);
  }
  shifted.proved_rebound_indirect_flow_targets.clear();
  for (const auto& [item, targets] : options.proved_rebound_indirect_flow_targets) {
    if (item == erased_item)
      continue;
    shifted.proved_rebound_indirect_flow_targets.emplace(
        item > erased_item ? item - 1U : item, targets);
  }
  return shifted;
}

bool verify_rewritten_artifact(const std::vector<MachineItem>& items,
                               const DarkSideSuffixHelperProof& original,
                               const DarkSideSuffixHelperOptions& options,
                               std::vector<std::string>& reasons) {
  const ArtifactIndex index = index_artifact(items);
  if (index.cells != original.input_cells - 1) {
    reasons.push_back("final artifact did not remove exactly one explicit return cell");
    return false;
  }
  const auto root = index.label_addresses.find(original.helper_label);
  const auto root_item = index.label_items.find(original.helper_label);
  if (root == index.label_addresses.end() || root_item == index.label_items.end() ||
      index.duplicate_labels.contains(original.helper_label) ||
      root->second != original.body_start_address) {
    reasons.push_back("final artifact moved or removed the dark-side helper entry");
    return false;
  }
  if (!verify_official_fallthrough_mode(items, index, root_item->second,
                                        original.helper_label, options,
                                        /*final_artifact=*/true, nullptr, reasons)) {
    return false;
  }
  if (original.official_fallthrough_proved) {
    const auto continuation = index.cell_items.find(kExplicitReturnPhysical);
    if (continuation == index.cell_items.end() ||
        items.at(continuation->second).kind != MachineItemKind::Op ||
        items.at(continuation->second).opcode != original.official_continuation_opcode) {
      reasons.push_back(
          "final dual-mode continuation differs from the proved post-return command");
      return false;
    }
  }
  const auto return_zero = index.cell_items.find(0);
  if (return_zero == index.cell_items.end() ||
      items.at(return_zero->second).kind != MachineItemKind::Op ||
      basic_kind_for_opcode(items.at(return_zero->second).opcode) != IrKind::Return) {
    reasons.push_back("final artifact has no В/О at physical 00 for the side-space boundary");
    return false;
  }

  for (int address = original.body_start_address; address <= kSideSpaceLastPhysical; ++address) {
    const auto cell = index.cell_items.find(address);
    if (cell == index.cell_items.end() || items.at(cell->second).kind != MachineItemKind::Op ||
        !is_straight_line_body_opcode(items.at(cell->second).opcode)) {
      reasons.push_back("final artifact no longer has a straight-line executable suffix at " +
                        std::to_string(address));
      return false;
    }
  }

  const auto last = index.cell_items.find(kSideSpaceLastPhysical);
  if (last == index.cell_items.end() || !items.at(last->second).comment.has_value() ||
      items.at(last->second).comment->find(kBoundaryMarker) == std::string::npos) {
    reasons.push_back("final artifact is missing its proved F9 boundary marker");
    return false;
  }

  const std::map<std::string, DarkSideSuffixEntry> entries = entry_map(original);
  std::map<int, DarkSideSuffixEntry> entries_by_address;
  for (const DarkSideSuffixEntry& entry : original.entries)
    entries_by_address.emplace(entry.entry_address, entry);
  int calls = 0;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind != MachineItemKind::Address)
      continue;
    const auto* label = std::get_if<std::string>(&item.target);
    std::optional<DarkSideSuffixEntry> expected;
    if (label != nullptr && entries.contains(*label))
      expected = entries.at(*label);
    if (item.formal_opcode.has_value()) {
      try {
        const FormalAddressInfo formal =
            formal_address_info(*item.formal_opcode, options.address_space_model);
        if (is_side_space_alias(formal) && entries_by_address.contains(formal.actual)) {
          expected = entries_by_address.at(formal.actual);
        } else if (is_side_space_alias(formal) &&
                   formal.actual >= original.body_start_address &&
                   formal.actual <= kSideSpaceLastPhysical) {
          reasons.push_back("final artifact introduced an unproved side-space helper entry");
          return false;
        }
      } catch (const std::exception&) {
        reasons.push_back("final artifact contains an undecodable formal helper call");
        return false;
      }
    }
    if (!expected.has_value())
      continue;
    if (item_index == 0 || items.at(item_index - 1U).kind != MachineItemKind::Op ||
        items.at(item_index - 1U).opcode != 0x53) {
      reasons.push_back("final artifact has a non-call reference to a dark-side entry");
      return false;
    }
    if (!item.formal_opcode.has_value() ||
        *item.formal_opcode != expected->formal_opcode) {
      reasons.push_back("final artifact helper call is not bound to its proved B2..F9 alias");
      return false;
    }
    const FormalAddressInfo formal =
        formal_address_info(*item.formal_opcode, options.address_space_model);
    if (!is_side_space_alias(formal) || formal.actual != expected->entry_address) {
      reasons.push_back("final artifact formal call no longer resolves to its helper entry");
      return false;
    }
    ++calls;
  }
  if (calls != static_cast<int>(original.calls.size())) {
    reasons.push_back("final artifact changed the proved dark-side call set");
    return false;
  }

  for (const DarkSideShiftedDirectTarget& shifted :
       original.shifted_direct_targets) {
    const std::size_t operand_item =
        shifted.operand_item_index > original.explicit_return_item_index
            ? shifted.operand_item_index - 1U
            : shifted.operand_item_index;
    const std::size_t target_item =
        shifted.target_item_index > original.explicit_return_item_index
            ? shifted.target_item_index - 1U
            : shifted.target_item_index;
    if (operand_item >= items.size() || target_item >= items.size() ||
        items.at(operand_item).kind != MachineItemKind::Address ||
        items.at(target_item).kind != MachineItemKind::Op) {
      reasons.push_back("final artifact lost a shifted direct-flow identity");
      return false;
    }
    const std::optional<int> rebound = referenced_physical_address(
        items.at(operand_item), index, options.address_space_model);
    if (!rebound.has_value() ||
        *rebound != index.item_addresses.at(target_item)) {
      reasons.push_back("final artifact changed a shifted direct-flow target identity");
      return false;
    }
  }

  // Re-run the indirect-flow obligations on the final item indexes and resolve
  // the rewritten metadata independently. Official indirect entry into the
  // helper remains invalid because it would bypass the implicit side return.
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind != MachineItemKind::Op ||
        !is_indirect_flow_kind(basic_kind_for_opcode(item.opcode)))
      continue;
    const auto targets = options.proved_indirect_flow_targets.find(item_index);
    if (targets == options.proved_indirect_flow_targets.end()) {
      reasons.push_back("final artifact indirect flow lacks a complete target proof");
      return false;
    }
    if (options.proved_rebound_indirect_flow_targets.contains(item_index)) {
      if (!item.indirect_flow_targets.has_value() || item.indirect_flow_targets->empty()) {
        reasons.push_back("final artifact indirect flow lost its typed target metadata");
        return false;
      }
      std::vector<int> resolved;
      for (const IrTarget& target : *item.indirect_flow_targets) {
        const std::optional<int> address = resolved_ir_target(target, index);
        if (!address.has_value()) {
          reasons.push_back("final artifact indirect target metadata cannot be resolved");
          return false;
        }
        resolved.push_back(*address);
      }
      std::vector<int> expected = targets->second;
      std::sort(resolved.begin(), resolved.end());
      std::sort(expected.begin(), expected.end());
      if (resolved != expected) {
        reasons.push_back("final artifact indirect target metadata changed command identity");
        return false;
      }
    }
    for (int target : targets->second) {
      if (target >= original.body_start_address && target <= kSideSpaceLastPhysical) {
        reasons.push_back("final artifact indirect target enters the implicit-return suffix");
        return false;
      }
    }
  }

  return true;
}

} // namespace

std::vector<DarkSideSuffixLayoutCandidate>
find_dark_side_suffix_layout_candidates(const std::vector<MachineItem>& items) {
  const ArtifactIndex index = index_artifact(items);
  std::vector<DarkSideSuffixLayoutCandidate> candidates;
  std::set<std::size_t> emitted_targets;

  for (const auto& [root_label, root_item] : index.label_items) {
    if (index.duplicate_labels.contains(root_label))
      continue;

    std::set<std::string> entry_labels;
    std::size_t target_item = items.size();
    int body_cells = 0;
    bool found_return = false;
    bool straight_line = true;
    for (std::size_t item_index = root_item; item_index < items.size(); ++item_index) {
      const MachineItem& item = items.at(item_index);
      if (item.kind == MachineItemKind::Label) {
        if (index.duplicate_labels.contains(item.name)) {
          straight_line = false;
          break;
        }
        entry_labels.insert(item.name);
        continue;
      }
      if (item.kind != MachineItemKind::Op) {
        straight_line = false;
        break;
      }
      if (basic_kind_for_opcode(item.opcode) == IrKind::Return) {
        found_return = true;
        break;
      }
      if (!is_straight_line_body_opcode(item.opcode)) {
        straight_line = false;
        break;
      }
      if (target_item == items.size())
        target_item = item_index;
      ++body_cells;
    }
    if (!straight_line || !found_return || target_item == items.size() ||
        body_cells <= 0 || body_cells > kSideSpaceLastPhysical) {
      continue;
    }

    int direct_calls = 0;
    bool non_call_entry = false;
    for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
      const MachineItem& item = items.at(item_index);
      if (item.kind != MachineItemKind::Address)
        continue;
      const auto* label = std::get_if<std::string>(&item.target);
      if (label == nullptr || !entry_labels.contains(*label))
        continue;
      if (item_index == 0U || items.at(item_index - 1U).kind != MachineItemKind::Op ||
          items.at(item_index - 1U).opcode != 0x53) {
        non_call_entry = true;
        break;
      }
      ++direct_calls;
    }
    if (non_call_entry || direct_calls == 0 || !emitted_targets.insert(target_item).second)
      continue;

    candidates.push_back(DarkSideSuffixLayoutCandidate{
        .helper_label = root_label,
        .target_item_index = target_item,
        .body_cells = body_cells,
        .required_start_address = kExplicitReturnPhysical - body_cells,
    });
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const DarkSideSuffixLayoutCandidate& left,
               const DarkSideSuffixLayoutCandidate& right) {
              return std::tie(left.required_start_address, left.target_item_index,
                              left.helper_label) <
                     std::tie(right.required_start_address, right.target_item_index,
                              right.helper_label);
            });
  return candidates;
}

DarkSideSuffixHelperProof
verify_dark_side_suffix_helper(const std::vector<MachineItem>& items,
                               const std::string& helper_label,
                               const DarkSideSuffixHelperOptions& options) {
  DarkSideSuffixHelperProof proof;
  proof.helper_label = helper_label;
  const ArtifactIndex index = index_artifact(items);
  proof.input_cells = index.cells;

  const auto root_item = index.label_items.find(helper_label);
  const auto root_address = index.label_addresses.find(helper_label);
  if (root_item == index.label_items.end() || root_address == index.label_addresses.end()) {
    proof.reasons.push_back("helper label '" + helper_label + "' is absent from the artifact");
    return proof;
  }
  if (index.duplicate_labels.contains(helper_label)) {
    proof.reasons.push_back("helper label '" + helper_label + "' is duplicated");
    return proof;
  }
  proof.body_start_address = root_address->second;
  if (proof.body_start_address < 0 || proof.body_start_address > kSideSpaceLastPhysical) {
    proof.reasons.push_back("helper entry is outside physical cells 00..47");
    return proof;
  }
  const auto return_zero = index.cell_items.find(0);
  if (return_zero == index.cell_items.end() ||
      items.at(return_zero->second).kind != MachineItemKind::Op ||
      basic_kind_for_opcode(items.at(return_zero->second).opcode) != IrKind::Return) {
    proof.reasons.push_back(
        "physical 00 is not В/О, so crossing the side-space boundary cannot return");
  }
  std::map<std::string, int> entry_addresses;
  for (std::size_t item_index = root_item->second; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    const int address = index.item_addresses.at(item_index);
    if (item.kind == MachineItemKind::Label) {
      if (address <= kSideSpaceLastPhysical)
        entry_addresses[item.name] = address;
      continue;
    }
    if (address < proof.body_start_address)
      continue;
    if (address <= kSideSpaceLastPhysical) {
      if (item.kind != MachineItemKind::Op || !is_straight_line_body_opcode(item.opcode)) {
        proof.reasons.push_back("helper body contains control flow or address data at physical " +
                                std::to_string(address));
      }
      continue;
    }
    if (address == kExplicitReturnPhysical && item.kind == MachineItemKind::Op &&
        basic_kind_for_opcode(item.opcode) == IrKind::Return) {
      proof.explicit_return_address = address;
      proof.explicit_return_item_index = item_index;
    } else {
      proof.reasons.push_back("helper suffix is not followed by an explicit В/О at physical 48");
    }
    break;
  }

  proof.body_end_address = kSideSpaceLastPhysical;
  proof.body_cells = proof.body_end_address - proof.body_start_address + 1;
  if (proof.body_cells <= 0 || proof.body_cells > 48)
    proof.reasons.push_back("helper body length is outside 1..48 cells");
  if (proof.explicit_return_address != kExplicitReturnPhysical)
    proof.reasons.push_back("no removable explicit В/О was proved at physical 48");
  (void)verify_official_fallthrough_mode(
      items, index, root_item->second, helper_label, options,
      /*final_artifact=*/false, &proof, proof.reasons);
  for (const std::string& duplicate : index.duplicate_labels) {
    if (entry_addresses.contains(duplicate))
      proof.reasons.push_back("helper entry label '" + duplicate + "' is duplicated");
  }

  for (const auto& [label, address] : entry_addresses) {
    const int formal_opcode = formal_opcode_for_side_entry(address, options.address_space_model);
    if (formal_opcode < 0) {
      proof.reasons.push_back("no B2..F9 formal alias exists for entry " + label);
      continue;
    }
    try {
      const FormalAddressInfo formal =
          formal_address_info(formal_opcode, options.address_space_model);
      if (!is_side_space_alias(formal) || formal.actual != address) {
        proof.reasons.push_back("formal alias for entry " + label +
                                " does not resolve to its physical cell");
        continue;
      }
    } catch (const std::exception&) {
      proof.reasons.push_back("formal alias for entry " + label + " cannot be decoded");
      continue;
    }
    proof.entries.push_back(DarkSideSuffixEntry{
        .label = label,
        .entry_address = address,
        .formal_opcode = formal_opcode,
    });
  }
  std::sort(proof.entries.begin(), proof.entries.end(),
            [](const DarkSideSuffixEntry& left, const DarkSideSuffixEntry& right) {
              if (left.entry_address != right.entry_address)
                return left.entry_address < right.entry_address;
              return left.label < right.label;
            });

  auto ensure_entry = [&](int address, std::string label)
      -> std::optional<DarkSideSuffixEntry> {
    const auto existing = std::find_if(
        proof.entries.begin(), proof.entries.end(),
        [&](const DarkSideSuffixEntry& entry) {
          return entry.entry_address == address;
        });
    if (existing != proof.entries.end())
      return *existing;
    const int formal_opcode =
        formal_opcode_for_side_entry(address, options.address_space_model);
    if (formal_opcode < 0)
      return std::nullopt;
    DarkSideSuffixEntry entry{
        .label = std::move(label),
        .entry_address = address,
        .formal_opcode = formal_opcode,
    };
    proof.entries.push_back(entry);
    return entry;
  };
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind != MachineItemKind::Address)
      continue;
    const std::optional<int> target =
        referenced_physical_address(item, index, options.address_space_model);
    if (!target.has_value()) {
      proof.reasons.push_back("an address operand could not be resolved in the final artifact");
      continue;
    }
    if (*target < 0 || *target >= index.cells) {
      proof.reasons.push_back("an address operand targets outside the final artifact");
      continue;
    }
    if (*target >= proof.body_start_address && *target < kExplicitReturnPhysical &&
        item_index > 0U &&
        items.at(item_index - 1U).kind == MachineItemKind::Op &&
        items.at(item_index - 1U).opcode == 0x53) {
      const auto* label = std::get_if<std::string>(&item.target);
      const std::optional<DarkSideSuffixEntry> entry =
          ensure_entry(*target, label == nullptr ? std::string{} : *label);
      if (!entry.has_value()) {
        proof.reasons.push_back("no B2..F9 formal alias exists for a direct helper call");
        continue;
      }
      if (item.formal_opcode.has_value() &&
          *item.formal_opcode != entry->formal_opcode) {
        proof.reasons.push_back("existing formal helper call does not use its B2..F9 alias");
        continue;
      }
      proof.calls.push_back(DarkSideSuffixCall{
          .call_item_index = item_index - 1U,
          .operand_item_index = item_index,
          .entry_label = entry->label,
          .entry_address = entry->entry_address,
          .formal_opcode = entry->formal_opcode,
      });
      continue;
    }
    if (*target >= proof.body_start_address && *target <= kExplicitReturnPhysical) {
      const std::string source =
          item_index > 0U && items.at(item_index - 1U).kind == MachineItemKind::Op
              ? opcode_by_code(items.at(item_index - 1U).opcode).name
              : std::string("non-op");
      proof.reasons.push_back(
          "non-ПП reference at item " + std::to_string(item_index) +
          " through " + source + " targets physical " + std::to_string(*target) +
          " inside the helper or its removed В/О");
      continue;
    }
    const bool fixed_target =
        item.formal_opcode.has_value() || std::holds_alternative<int>(item.target);
    if (fixed_target && *target > kExplicitReturnPhysical) {
      const auto target_item = index.cell_items.find(*target);
      if (target_item == index.cell_items.end() ||
          items.at(target_item->second).kind != MachineItemKind::Op) {
        proof.reasons.push_back(
            "fixed address after physical 48 does not identify an executable command");
      } else {
        proof.shifted_direct_targets.push_back(DarkSideShiftedDirectTarget{
            .operand_item_index = item_index,
            .target_item_index = target_item->second,
            .original_target_address = *target,
        });
      }
    }
  }
  std::sort(proof.entries.begin(), proof.entries.end(),
            [](const DarkSideSuffixEntry& left, const DarkSideSuffixEntry& right) {
              if (left.entry_address != right.entry_address)
                return left.entry_address < right.entry_address;
              return left.label < right.label;
            });
  if (proof.calls.empty())
    proof.reasons.push_back("helper has no direct ПП call sites to rewrite");

  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind != MachineItemKind::Op ||
        !is_indirect_flow_kind(basic_kind_for_opcode(item.opcode)))
      continue;
    const auto targets = options.proved_indirect_flow_targets.find(item_index);
    if (targets == options.proved_indirect_flow_targets.end()) {
      proof.reasons.push_back("indirect flow at item " + std::to_string(item_index) +
                              " has no complete final-artifact target proof");
      continue;
    }
    std::vector<int> expected_targets;
    bool needs_rebind = false;
    for (int target : targets->second) {
      if (target >= proof.body_start_address && target <= kExplicitReturnPhysical) {
        proof.reasons.push_back("proved indirect target " + std::to_string(target) +
                                " enters a helper whose explicit return is removed");
        continue;
      }
      needs_rebind = needs_rebind || target > kExplicitReturnPhysical;
      expected_targets.push_back(target > kExplicitReturnPhysical ? target - 1 : target);
    }
    const auto rebound = options.proved_rebound_indirect_flow_targets.find(item_index);
    if (needs_rebind && rebound == options.proved_rebound_indirect_flow_targets.end()) {
      proof.reasons.push_back("proved indirect target after physical 48 has no selector-rebind "
                              "proof");
      continue;
    }
    std::vector<int> supplied =
        rebound == options.proved_rebound_indirect_flow_targets.end()
            ? targets->second
            : rebound->second;
    std::sort(expected_targets.begin(), expected_targets.end());
    std::sort(supplied.begin(), supplied.end());
    if (expected_targets != supplied) {
      proof.reasons.push_back("proved indirect target rebinding does not preserve command "
                              "identity across cell erasure");
    }
  }

  proof.proved = proof.reasons.empty();
  return proof;
}

DarkSideSuffixHelperResult
rewrite_dark_side_suffix_helper(const std::vector<MachineItem>& items,
                                const std::string& helper_label,
                                const DarkSideSuffixHelperOptions& options) {
  DarkSideSuffixHelperResult result;
  result.items = items;
  result.proof = verify_dark_side_suffix_helper(items, helper_label, options);
  if (!result.proof.proved)
    return result;

  const ArtifactIndex symbolic_index = index_artifact(result.items);
  for (const auto& [item_index, targets] :
       options.proved_rebound_indirect_flow_targets) {
    (void)targets;
    if (item_index >= result.items.size())
      continue;
    MachineItem& item = result.items.at(item_index);
    if (item.kind != MachineItemKind::Op || !item.indirect_flow_targets.has_value())
      continue;
    bool rebound = false;
    for (IrTarget& target : *item.indirect_flow_targets) {
      const auto* numeric = std::get_if<int>(&target);
      if (numeric == nullptr || *numeric <= kExplicitReturnPhysical)
        continue;
      const std::optional<std::string> label =
          unique_label_at_address(symbolic_index, *numeric);
      target = label.has_value() ? IrTarget(*label) : IrTarget(*numeric - 1);
      rebound = true;
    }
    if (rebound)
      append_comment(item, "dark-side shifted indirect target rebound by command identity");
  }
  for (MachineItem& item : result.items) {
    if (item.kind != MachineItemKind::Address)
      continue;
    const std::optional<int> target = referenced_physical_address(
        item, symbolic_index, options.address_space_model);
    const bool fixed_target =
        item.formal_opcode.has_value() || std::holds_alternative<int>(item.target);
    if (!fixed_target || !target.has_value() || *target <= kExplicitReturnPhysical)
      continue;
    const std::optional<std::string> label =
        unique_label_at_address(symbolic_index, *target);
    item.target = label.has_value()
                      ? IrTarget(*label)
                      : IrTarget(*target - 1);
    item.formal_opcode.reset();
    append_comment(item,
                   label.has_value()
                       ? "dark-side shifted direct target rebound by command-identity label"
                       : "dark-side shifted direct target rebound by proved numeric identity");
  }

  for (const DarkSideSuffixCall& call : result.proof.calls) {
    MachineItem& operand = result.items.at(call.operand_item_index);
    operand.formal_opcode = call.formal_opcode;
    append_comment(operand, "dark-side suffix call; formal=" +
                                format_formal_address_opcode(call.formal_opcode) + "->" +
                                std::to_string(call.entry_address));
  }

  const ArtifactIndex before_erasure = index_artifact(result.items);
  const auto last = before_erasure.cell_items.find(kSideSpaceLastPhysical);
  if (last == before_erasure.cell_items.end()) {
    result.proof.proved = false;
    result.proof.reasons.push_back("cannot mark the final F9 body cell");
    result.items = items;
    return result;
  }
  append_comment(result.items.at(last->second), std::string(kBoundaryMarker));
  result.items.erase(result.items.begin() +
                     static_cast<std::ptrdiff_t>(result.proof.explicit_return_item_index));

  const DarkSideSuffixHelperOptions final_options =
      shifted_options_after_erasure(options, result.proof.explicit_return_item_index);
  std::vector<std::string> final_reasons;
  result.proof.final_artifact_proved =
      verify_rewritten_artifact(result.items, result.proof, final_options, final_reasons);
  if (!result.proof.final_artifact_proved) {
    result.proof.proved = false;
    result.proof.reasons.insert(result.proof.reasons.end(), final_reasons.begin(),
                                final_reasons.end());
    result.items = items;
    return result;
  }

  result.applied = 1;
  const std::string official_mode = result.proof.official_fallthrough_proved
                                        ? ", preserved one proved official fallthrough entry "
                                              "whose post-F9 continuation is physical 48"
                                        : "";
  result.optimizations.push_back(passes::AppliedOptimization{
      .name = "dark-side-suffix-helper",
      .detail = "Rebound " + std::to_string(result.proof.calls.size()) + " direct helper call" +
                (result.proof.calls.size() == 1U ? "" : "s") + " to B2..F9 side-space entr" +
                (result.proof.entries.size() == 1U ? "y" : "ies") +
                official_mode +
                " and removed В/О at physical 48 after a final-artifact proof.",
  });
  return result;
}

DarkSideSuffixHelperResult
optimize_dark_side_suffix_helper(const std::vector<MachineItem>& items,
                                 const DarkSideSuffixHelperOptions& options) {
  std::set<std::string> labels;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label)
      labels.insert(item.name);
  }
  DarkSideSuffixHelperResult rejection;
  rejection.items = items;
  for (const std::string& label : labels) {
    DarkSideSuffixHelperResult candidate = rewrite_dark_side_suffix_helper(items, label, options);
    if (candidate.applied > 0)
      return candidate;
    if (rejection.proof.reasons.empty() && !candidate.proof.reasons.empty())
      rejection.proof = std::move(candidate.proof);
  }
  return rejection;
}

} // namespace mkpro::core
