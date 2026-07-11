#include "mkpro/core/cyclic_end_return.hpp"

#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core {

namespace {

constexpr std::string_view kBoundaryMarker = "cyclic-end boundary return via 00";

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
  case IrKind::Store:
  case IrKind::Recall:
  case IrKind::IndirectStore:
  case IrKind::IndirectRecall:
  case IrKind::Plain:
    return true;
  case IrKind::Label:
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Call:
  case IrKind::Loop:
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
  case IrKind::IndirectCondJump:
  case IrKind::Return:
  case IrKind::Stop:
  case IrKind::OrphanAddress:
    return false;
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

std::optional<std::size_t> next_cell_item(const std::vector<MachineItem>& items,
                                          std::size_t after) {
  for (++after; after < items.size(); ++after) {
    if (items.at(after).kind != MachineItemKind::Label)
      return after;
  }
  return std::nullopt;
}

bool cell_is_fallthrough_barrier(const std::vector<MachineItem>& items,
                                 std::size_t cell_item_index) {
  const MachineItem& item = items.at(cell_item_index);
  if (item.kind == MachineItemKind::Op) {
    const IrKind kind = basic_kind_for_opcode(item.opcode);
    return kind == IrKind::Return || kind == IrKind::Stop;
  }
  if (item.kind != MachineItemKind::Address)
    return false;
  const std::optional<std::size_t> flow = previous_cell_item(items, cell_item_index);
  return flow.has_value() && items.at(*flow).kind == MachineItemKind::Op &&
         items.at(*flow).opcode == 0x51;
}

bool has_fallthrough_barrier_before(const std::vector<MachineItem>& items, std::size_t item_index) {
  const std::optional<std::size_t> previous = previous_cell_item(items, item_index);
  return previous.has_value() && cell_is_fallthrough_barrier(items, *previous);
}

bool has_fallthrough_barrier_at_end(const std::vector<MachineItem>& items) {
  const std::optional<std::size_t> last = previous_cell_item(items, items.size());
  return last.has_value() && cell_is_fallthrough_barrier(items, *last);
}

void append_comment(MachineItem& item, const std::string& suffix) {
  if (item.comment.has_value() && !item.comment->empty())
    item.comment = *item.comment + "; " + suffix;
  else
    item.comment = suffix;
}

bool direct_address_artifact_proved(const std::vector<MachineItem>& items,
                                    const ArtifactIndex& index, std::vector<std::string>& reasons) {
  std::set<std::string> referenced_labels;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind == MachineItemKind::Op && opcode_by_code(item.opcode).takes_address) {
      const std::optional<std::size_t> operand = next_cell_item(items, item_index);
      if (!operand.has_value() || items.at(*operand).kind != MachineItemKind::Address) {
        reasons.push_back("address-taking opcode is not followed by its address operand");
        return false;
      }
      continue;
    }
    if (item.kind != MachineItemKind::Address)
      continue;
    const std::optional<std::size_t> flow = previous_cell_item(items, item_index);
    if (!flow.has_value() || items.at(*flow).kind != MachineItemKind::Op ||
        !opcode_by_code(items.at(*flow).opcode).takes_address) {
      reasons.push_back("orphan address operand is unsafe across helper relocation");
      return false;
    }
    if (const auto* label = std::get_if<std::string>(&item.target))
      referenced_labels.insert(*label);
  }

  for (const std::string& label : referenced_labels) {
    const auto address = index.label_addresses.find(label);
    if (address == index.label_addresses.end()) {
      reasons.push_back("address operand references unresolved label " + label);
      continue;
    }
    const auto cell = index.cell_items.find(address->second);
    if (cell == index.cell_items.end()) {
      reasons.push_back("address label " + label + " does not denote an executable command cell");
      continue;
    }
    if (items.at(cell->second).kind == MachineItemKind::Address) {
      reasons.push_back("address/code overlay entry " + label +
                        " is unsafe when relocation changes encoded operands");
      return false;
    }
    if (items.at(cell->second).kind != MachineItemKind::Op) {
      reasons.push_back("address label " + label + " does not denote an executable command cell");
      continue;
    }
  }
  return reasons.empty();
}

std::set<std::string> entry_labels(const CyclicEndReturnProof& proof) {
  std::set<std::string> labels;
  for (const CyclicEndReturnEntry& entry : proof.entries)
    labels.insert(entry.label);
  return labels;
}

std::vector<MachineItem> without_helper_block(const std::vector<MachineItem>& items,
                                              const CyclicEndReturnProof& proof) {
  std::vector<MachineItem> remaining;
  remaining.reserve(items.size() -
                    (proof.helper_block_end_item_index - proof.helper_block_begin_item_index));
  remaining.insert(remaining.end(), items.begin(),
                   items.begin() +
                       static_cast<std::ptrdiff_t>(proof.helper_block_begin_item_index));
  remaining.insert(remaining.end(),
                   items.begin() + static_cast<std::ptrdiff_t>(proof.helper_block_end_item_index),
                   items.end());
  return remaining;
}

std::optional<int> relocated_identity_address(int original_address,
                                              const CyclicEndReturnProof& proof) {
  if (original_address < proof.original_body_start_address)
    return original_address;
  if (original_address < proof.original_explicit_return_address) {
    return proof.relocated_body_start_address + original_address -
           proof.original_body_start_address;
  }
  if (original_address == proof.original_explicit_return_address)
    return std::nullopt;
  return original_address - (proof.body_cells + 1);
}

std::optional<int> modeled_formal_target(const MachineItem& item, AddressSpaceModel model,
                                         std::vector<std::string>& reasons) {
  if (!item.formal_opcode.has_value())
    return std::nullopt;
  try {
    const FormalAddressInfo formal = formal_address_info(*item.formal_opcode, model);
    if (formal.kind == FormalAddressKind::SuperDark || formal.one_command ||
        formal.extra.has_value()) {
      reasons.push_back("super-dark one-command operand has no exact cyclic CFG model");
      return std::nullopt;
    }
    return formal.actual;
  } catch (const std::exception&) {
    reasons.push_back("formal address operand is invalid for the cyclic address space");
    return std::nullopt;
  }
}

bool formal_operand_survives_relocation(const std::vector<MachineItem>& items,
                                        const MachineItem& item, const ArtifactIndex& index,
                                        const CyclicEndReturnProof& proof,
                                        const CyclicEndReturnOptions& options,
                                        std::vector<std::string>& reasons) {
  const std::optional<int> target =
      modeled_formal_target(item, options.address_space_model, reasons);
  if (!target.has_value())
    return false;
  const auto target_cell = index.cell_items.find(*target);
  if (target_cell == index.cell_items.end() || item.kind != MachineItemKind::Address ||
      items.at(target_cell->second).kind != MachineItemKind::Op) {
    reasons.push_back("formal address target is not an executable command cell");
    return false;
  }
  const std::optional<int> relocated = relocated_identity_address(*target, proof);
  if (!relocated.has_value() || *relocated != *target) {
    reasons.push_back("formal address target changes command identity across helper relocation");
    return false;
  }
  if (const auto* label = std::get_if<std::string>(&item.target)) {
    const auto labeled = index.label_addresses.find(*label);
    if (labeled == index.label_addresses.end() || labeled->second != *target) {
      reasons.push_back("formal address operand disagrees with its symbolic target identity");
      return false;
    }
  }
  return true;
}

bool validate_complete_indirect_relocation(const std::vector<MachineItem>& items,
                                           const ArtifactIndex& index,
                                           const CyclicEndReturnProof& proof,
                                           const CyclicEndReturnOptions& options,
                                           std::vector<std::string>& reasons) {
  std::set<std::size_t> indirect_items;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind == MachineItemKind::Op &&
        is_indirect_flow_kind(basic_kind_for_opcode(item.opcode))) {
      indirect_items.insert(item_index);
    }
  }

  for (const std::size_t item_index : indirect_items) {
    const auto fact = options.proved_indirect_flow_targets.find(item_index);
    if (fact == options.proved_indirect_flow_targets.end()) {
      reasons.push_back("indirect flow at item " + std::to_string(item_index) +
                        " has no complete relocation proof");
      continue;
    }
    if (fact->second.empty()) {
      reasons.push_back("indirect flow at item " + std::to_string(item_index) +
                        " has an empty target set");
      continue;
    }
    const std::set<int> targets(fact->second.begin(), fact->second.end());
    if (targets.size() != fact->second.size()) {
      reasons.push_back("indirect flow at item " + std::to_string(item_index) +
                        " has duplicate targets");
    }
    for (const int target : targets) {
      const auto target_cell = index.cell_items.find(target);
      if (target_cell == index.cell_items.end() ||
          items.at(target_cell->second).kind != MachineItemKind::Op) {
        reasons.push_back("indirect flow target " + std::to_string(target) +
                          " is not an executable command cell");
        continue;
      }
      if (target >= proof.original_body_start_address &&
          target <= proof.original_explicit_return_address) {
        reasons.push_back("indirect flow enters the helper block at physical " +
                          std::to_string(target));
        continue;
      }
      const std::optional<int> relocated = relocated_identity_address(target, proof);
      if (!relocated.has_value() || *relocated != target) {
        reasons.push_back("indirect flow target " + std::to_string(target) +
                          " changes command identity across helper relocation");
      }
    }
  }
  for (const auto& [item_index, targets] : options.proved_indirect_flow_targets) {
    (void)targets;
    if (!indirect_items.contains(item_index)) {
      reasons.push_back("complete indirect-flow map contains a non-flow item " +
                        std::to_string(item_index));
    }
  }

  if (!options.external_entry_addresses.has_value()) {
    reasons.push_back("complete external-entry set was not supplied");
  } else {
    std::set<int> external_entries;
    for (const int entry : *options.external_entry_addresses) {
      if (!external_entries.insert(entry).second) {
        reasons.push_back("external entry set contains duplicate physical " +
                          std::to_string(entry));
        continue;
      }
      const auto entry_cell = index.cell_items.find(entry);
      if (entry_cell == index.cell_items.end() ||
          items.at(entry_cell->second).kind != MachineItemKind::Op) {
        reasons.push_back("external entry " + std::to_string(entry) +
                          " is not an executable command cell");
        continue;
      }
      const std::optional<int> relocated = relocated_identity_address(entry, proof);
      if (!relocated.has_value() || *relocated != entry) {
        reasons.push_back("external entry " + std::to_string(entry) +
                          " changes command identity across helper relocation");
      }
    }
  }
  return reasons.empty();
}

bool validate_final_indirect_and_entries(const std::vector<MachineItem>& items,
                                         const ArtifactIndex& index,
                                         const CyclicEndReturnProof& proof,
                                         std::vector<std::string>& reasons) {
  std::set<std::size_t> indirect_items;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind == MachineItemKind::Op &&
        is_indirect_flow_kind(basic_kind_for_opcode(item.opcode))) {
      indirect_items.insert(item_index);
      const auto fact = proof.final_indirect_flow_targets.find(item_index);
      if (fact == proof.final_indirect_flow_targets.end() || fact->second.empty()) {
        reasons.push_back("final indirect flow at item " + std::to_string(item_index) +
                          " lacks a complete target set");
        continue;
      }
      const std::set<int> targets(fact->second.begin(), fact->second.end());
      if (targets.size() != fact->second.size()) {
        reasons.push_back("final indirect flow target set contains duplicates");
      }
      for (const int target : targets) {
        const auto target_cell = index.cell_items.find(target);
        if (target_cell == index.cell_items.end() ||
            items.at(target_cell->second).kind != MachineItemKind::Op) {
          reasons.push_back("final indirect target " + std::to_string(target) +
                            " is not executable");
        }
      }
    }
  }
  for (const auto& [item_index, targets] : proof.final_indirect_flow_targets) {
    (void)targets;
    if (!indirect_items.contains(item_index))
      reasons.push_back("final indirect-flow map contains a non-flow item");
  }
  std::set<int> entries;
  for (const int entry : proof.final_external_entry_addresses) {
    if (!entries.insert(entry).second) {
      reasons.push_back("final external entry set contains a duplicate");
      continue;
    }
    const auto cell = index.cell_items.find(entry);
    if (cell == index.cell_items.end() || items.at(cell->second).kind != MachineItemKind::Op)
      reasons.push_back("final external entry is not executable");
  }
  return reasons.empty();
}

bool final_artifact_proved(const std::vector<MachineItem>& items,
                           const CyclicEndReturnProof& original,
                           const CyclicEndReturnOptions& options,
                           std::vector<std::string>& reasons) {
  if (options.address_space_model != AddressSpaceModel::Standard) {
    reasons.push_back("cyclic A4-to-00 return is valid only for the standard address space");
    return false;
  }

  const ArtifactIndex index = index_artifact(items);
  const int official_cells = official_program_step_limit(options.address_space_model);
  const int last_address = official_program_last_address(options.address_space_model);
  if (index.cells != official_cells) {
    reasons.push_back("final artifact does not occupy exactly the official 105 cells");
    return false;
  }
  if (!direct_address_artifact_proved(items, index, reasons))
    return false;
  const auto zero = index.cell_items.find(0);
  if (zero == index.cell_items.end() || items.at(zero->second).kind != MachineItemKind::Op ||
      basic_kind_for_opcode(items.at(zero->second).opcode) != IrKind::Return) {
    reasons.push_back("final artifact has no В/О at physical 00");
    return false;
  }

  const auto root = index.label_addresses.find(original.helper_label);
  const auto root_item = index.label_items.find(original.helper_label);
  if (root == index.label_addresses.end() || root_item == index.label_items.end() ||
      index.duplicate_labels.contains(original.helper_label) ||
      root->second != original.relocated_body_start_address) {
    reasons.push_back("final artifact did not relocate the helper to its proved suffix");
    return false;
  }
  if (!has_fallthrough_barrier_before(items, root_item->second)) {
    reasons.push_back("official control can fall through into the relocated helper suffix");
    return false;
  }

  for (int address = original.relocated_body_start_address; address <= last_address; ++address) {
    const auto cell = index.cell_items.find(address);
    if (cell == index.cell_items.end() || items.at(cell->second).kind != MachineItemKind::Op ||
        !is_straight_line_body_opcode(items.at(cell->second).opcode)) {
      reasons.push_back("relocated helper suffix is not straight-line at physical " +
                        std::to_string(address));
      return false;
    }
  }
  const auto last = index.cell_items.find(last_address);
  if (last == index.cell_items.end() || !items.at(last->second).comment.has_value() ||
      items.at(last->second).comment->find(kBoundaryMarker) == std::string::npos) {
    reasons.push_back("final A4 body cell has no cyclic-return proof marker");
    return false;
  }

  const std::set<std::string> entries = entry_labels(original);
  if (!validate_final_indirect_and_entries(items, index, original, reasons))
    return false;
  int calls = 0;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind != MachineItemKind::Address)
      continue;
    if (std::holds_alternative<int>(item.target)) {
      reasons.push_back("final artifact contains a fixed numeric address operand");
      return false;
    }
    if (item.formal_opcode.has_value()) {
      const std::optional<int> formal_target =
          modeled_formal_target(item, options.address_space_model, reasons);
      if (!formal_target.has_value())
        return false;
      const auto target_cell = index.cell_items.find(*formal_target);
      if (target_cell == index.cell_items.end() ||
          items.at(target_cell->second).kind != MachineItemKind::Op) {
        reasons.push_back("final formal address target is not executable");
        return false;
      }
      if (const auto* formal_label = std::get_if<std::string>(&item.target)) {
        const auto labeled = index.label_addresses.find(*formal_label);
        if (labeled == index.label_addresses.end() || labeled->second != *formal_target) {
          reasons.push_back("final formal address disagrees with its symbolic target identity");
          return false;
        }
      }
    }
    if (!std::holds_alternative<std::string>(item.target)) {
      reasons.push_back("final address operand has no symbolic identity");
      return false;
    }
    const std::string& label = std::get<std::string>(item.target);
    const auto target = index.label_addresses.find(label);
    if (target == index.label_addresses.end()) {
      reasons.push_back("final artifact contains an unresolved address label " + label);
      return false;
    }
    if (!entries.contains(label))
      continue;
    if (item_index == 0 || items.at(item_index - 1U).kind != MachineItemKind::Op ||
        items.at(item_index - 1U).opcode != 0x53) {
      reasons.push_back("relocated helper entry " + label + " has a non-ПП reference");
      return false;
    }
    const auto expected =
        std::find_if(original.entries.begin(), original.entries.end(),
                     [&](const CyclicEndReturnEntry& entry) { return entry.label == label; });
    if (expected == original.entries.end() || target->second != expected->relocated_address) {
      reasons.push_back("relocated helper entry " + label + " moved from its proved address");
      return false;
    }
    ++calls;
  }
  if (calls != static_cast<int>(original.calls.size())) {
    reasons.push_back("final artifact changed the proved direct helper call set");
    return false;
  }
  return true;
}

} // namespace

CyclicEndReturnProof verify_cyclic_end_return(const std::vector<MachineItem>& items,
                                              const std::string& helper_label,
                                              const CyclicEndReturnOptions& options) {
  CyclicEndReturnProof proof;
  proof.helper_label = helper_label;
  const ArtifactIndex index = index_artifact(items);
  proof.input_cells = index.cells;

  if (options.address_space_model != AddressSpaceModel::Standard) {
    proof.reasons.push_back(
        "cyclic A4-to-00 return is disabled outside the standard 105-cell profile");
    return proof;
  }
  const int official_cells = official_program_step_limit(options.address_space_model);
  if (index.cells != official_cells + 1) {
    proof.reasons.push_back(
        "cyclic-end candidate must contain exactly one removable cell beyond 00..A4");
    return proof;
  }
  if (!direct_address_artifact_proved(items, index, proof.reasons))
    return proof;

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
  proof.helper_block_begin_item_index = root_item->second;
  proof.original_body_start_address = root_address->second;

  for (std::size_t item_index = 0; item_index < root_item->second; ++item_index) {
    if (items.at(item_index).kind == MachineItemKind::Label &&
        index.item_addresses.at(item_index) == proof.original_body_start_address) {
      proof.reasons.push_back(
          "helper root is preceded by a co-located label that would be stranded by relocation");
      break;
    }
  }

  const auto zero = index.cell_items.find(0);
  if (zero == index.cell_items.end() || items.at(zero->second).kind != MachineItemKind::Op ||
      basic_kind_for_opcode(items.at(zero->second).opcode) != IrKind::Return) {
    proof.reasons.push_back("physical 00 is not В/О, so A4 wrap cannot return");
  }
  if (!has_fallthrough_barrier_before(items, root_item->second)) {
    proof.reasons.push_back("official control can fall through into the helper at its old site");
  }

  std::map<std::string, int> entries;
  bool found_return = false;
  for (std::size_t item_index = root_item->second; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    const int address = index.item_addresses.at(item_index);
    if (item.kind == MachineItemKind::Label) {
      if (!found_return)
        entries[item.name] = address;
      continue;
    }
    if (found_return)
      break;
    if (item.kind != MachineItemKind::Op) {
      proof.reasons.push_back("helper body contains address data");
      break;
    }
    const IrKind kind = basic_kind_for_opcode(item.opcode);
    if (kind == IrKind::Return) {
      found_return = true;
      proof.original_explicit_return_address = address;
      proof.original_explicit_return_item_index = item_index;
      proof.helper_block_end_item_index = item_index + 1U;
      while (proof.helper_block_end_item_index < items.size() &&
             items.at(proof.helper_block_end_item_index).kind == MachineItemKind::Label &&
             items.at(proof.helper_block_end_item_index).procedure_boundary == "end") {
        ++proof.helper_block_end_item_index;
      }
      continue;
    }
    if (!is_straight_line_body_opcode(item.opcode)) {
      proof.reasons.push_back("helper body contains control flow at physical " +
                              std::to_string(address));
      break;
    }
    proof.original_body_end_address = address;
    ++proof.body_cells;
  }
  if (!found_return)
    proof.reasons.push_back("helper has no removable explicit В/О");
  if (proof.body_cells <= 0)
    proof.reasons.push_back("helper has no straight-line body commands");

  for (const std::string& duplicate : index.duplicate_labels) {
    if (entries.contains(duplicate))
      proof.reasons.push_back("helper entry label '" + duplicate + "' is duplicated");
  }

  const int relocated_start = official_cells - proof.body_cells;
  proof.relocated_body_start_address = relocated_start;
  proof.relocated_body_end_address = official_cells - 1;
  proof.relocated_explicit_return_address = official_cells;
  for (const auto& [label, address] : entries) {
    proof.entries.push_back(CyclicEndReturnEntry{
        .label = label,
        .original_address = address,
        .relocated_address = relocated_start + address - proof.original_body_start_address,
    });
  }
  std::sort(proof.entries.begin(), proof.entries.end(),
            [](const CyclicEndReturnEntry& left, const CyclicEndReturnEntry& right) {
              if (left.relocated_address != right.relocated_address)
                return left.relocated_address < right.relocated_address;
              return left.label < right.label;
            });

  const std::set<std::string> helper_entries = entry_labels(proof);
  std::set<std::string> moved_labels = helper_entries;
  for (std::size_t item_index = proof.original_explicit_return_item_index + 1U;
       item_index < proof.helper_block_end_item_index; ++item_index) {
    if (items.at(item_index).kind == MachineItemKind::Label)
      moved_labels.insert(items.at(item_index).name);
  }

  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind == MachineItemKind::Op) {
      continue;
    }
    if (item.kind != MachineItemKind::Address)
      continue;
    if (std::holds_alternative<int>(item.target)) {
      proof.reasons.push_back("fixed numeric address operands are unsafe across helper relocation");
      continue;
    }
    if (item.formal_opcode.has_value() &&
        !formal_operand_survives_relocation(items, item, index, proof, options, proof.reasons)) {
      continue;
    }
    if (!std::holds_alternative<std::string>(item.target)) {
      proof.reasons.push_back("address operand has no symbolic identity");
      continue;
    }
    const std::string& label = std::get<std::string>(item.target);
    if (!index.label_addresses.contains(label)) {
      proof.reasons.push_back("address operand references missing label " + label);
      continue;
    }
    if (!moved_labels.contains(label))
      continue;
    if (!helper_entries.contains(label)) {
      proof.reasons.push_back("metadata/end label " + label +
                              " is referenced as an executable helper entry");
      continue;
    }
    if (item_index == 0 || items.at(item_index - 1U).kind != MachineItemKind::Op ||
        items.at(item_index - 1U).opcode != 0x53) {
      proof.reasons.push_back("helper entry " + label + " has a non-ПП reference");
      continue;
    }
    proof.calls.push_back(CyclicEndReturnCall{
        .call_item_index = item_index - 1U,
        .operand_item_index = item_index,
        .entry_label = label,
    });
  }
  if (proof.calls.empty())
    proof.reasons.push_back("helper has no proved direct ПП call sites");

  validate_complete_indirect_relocation(items, index, proof, options, proof.reasons);

  if (found_return) {
    const std::vector<MachineItem> remaining = without_helper_block(items, proof);
    if (!has_fallthrough_barrier_at_end(remaining)) {
      proof.reasons.push_back(
          "control can fall through from the remaining program into the relocated helper");
    }
  }

  if (proof.relocated_body_start_address <= 0 ||
      proof.relocated_body_end_address !=
          official_program_last_address(options.address_space_model) ||
      proof.relocated_explicit_return_address != official_cells) {
    proof.reasons.push_back("helper body cannot be placed at an A4-ending suffix");
  }

  proof.proved = proof.reasons.empty();
  return proof;
}

CyclicEndReturnResult rewrite_cyclic_end_return(const std::vector<MachineItem>& items,
                                                const std::string& helper_label,
                                                const CyclicEndReturnOptions& options) {
  CyclicEndReturnResult result;
  result.items = items;
  result.proof = verify_cyclic_end_return(items, helper_label, options);
  if (!result.proof.proved)
    return result;

  std::vector<MachineItem> helper(
      items.begin() + static_cast<std::ptrdiff_t>(result.proof.helper_block_begin_item_index),
      items.begin() + static_cast<std::ptrdiff_t>(result.proof.helper_block_end_item_index));
  const std::size_t return_offset =
      result.proof.original_explicit_return_item_index - result.proof.helper_block_begin_item_index;
  result.items = without_helper_block(items, result.proof);
  const std::size_t relocated_block_begin = result.items.size();
  result.items.insert(result.items.end(), helper.begin(), helper.end());
  const std::size_t relocated_return_item = relocated_block_begin + return_offset;

  const std::optional<std::size_t> final_body_item =
      previous_cell_item(result.items, relocated_return_item);
  if (!final_body_item.has_value()) {
    result.proof.proved = false;
    result.proof.reasons.push_back("cannot locate the final A4 helper body command");
    result.items = items;
    return result;
  }
  append_comment(result.items.at(*final_body_item), std::string(kBoundaryMarker));
  result.items.at(*final_body_item).roles.push_back("cyclic-end-return:A4-to-00");
  result.items.erase(result.items.begin() + static_cast<std::ptrdiff_t>(relocated_return_item));

  const std::size_t block_size =
      result.proof.helper_block_end_item_index - result.proof.helper_block_begin_item_index;
  const std::size_t remaining_item_count = items.size() - block_size;
  for (const auto& [old_item_index, targets] : options.proved_indirect_flow_targets) {
    std::optional<std::size_t> new_item_index;
    if (old_item_index < result.proof.helper_block_begin_item_index) {
      new_item_index = old_item_index;
    } else if (old_item_index >= result.proof.helper_block_end_item_index) {
      new_item_index = old_item_index - block_size;
    } else if (old_item_index != result.proof.original_explicit_return_item_index) {
      new_item_index =
          remaining_item_count + old_item_index - result.proof.helper_block_begin_item_index;
      if (old_item_index > result.proof.original_explicit_return_item_index)
        --*new_item_index;
    }
    if (!new_item_index.has_value() || *new_item_index >= result.items.size() ||
        result.items.at(*new_item_index).kind != MachineItemKind::Op ||
        !is_indirect_flow_kind(basic_kind_for_opcode(result.items.at(*new_item_index).opcode))) {
      result.proof.proved = false;
      result.proof.reasons.push_back(
          "cannot reindex complete indirect-flow proof after helper relocation");
      result.items = items;
      return result;
    }
    result.proof.final_indirect_flow_targets.emplace(*new_item_index, targets);
  }
  result.proof.final_external_entry_addresses = *options.external_entry_addresses;

  std::vector<std::string> final_reasons;
  result.proof.output_cells = index_artifact(result.items).cells;
  result.proof.final_artifact_proved =
      final_artifact_proved(result.items, result.proof, options, final_reasons);
  if (!result.proof.final_artifact_proved) {
    result.proof.proved = false;
    result.proof.reasons.insert(result.proof.reasons.end(), final_reasons.begin(),
                                final_reasons.end());
    result.items = items;
    return result;
  }

  result.applied = 1;
  result.optimizations.push_back(passes::AppliedOptimization{
      .name = "cyclic-end-return",
      .detail = "Relocated straight-line helper " + helper_label +
                " to end at A4 and removed "
                "its explicit В/О after proving wrap to В/О at physical 00 for " +
                std::to_string(result.proof.calls.size()) + " direct call" +
                (result.proof.calls.size() == 1U ? "." : "s."),
  });
  return result;
}

CyclicEndReturnResult optimize_cyclic_end_return(const std::vector<MachineItem>& items,
                                                 const CyclicEndReturnOptions& options) {
  std::vector<std::string> labels;
  std::set<std::string> seen;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label && seen.insert(item.name).second)
      labels.push_back(item.name);
  }

  CyclicEndReturnResult rejection;
  rejection.items = items;
  for (const std::string& label : labels) {
    CyclicEndReturnResult candidate = rewrite_cyclic_end_return(items, label, options);
    if (candidate.applied > 0)
      return candidate;
    if (rejection.proof.reasons.empty() && !candidate.proof.reasons.empty())
      rejection.proof = std::move(candidate.proof);
  }
  return rejection;
}

} // namespace mkpro::core
