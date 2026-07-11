#include "mkpro/core/natural_component_layout.hpp"

#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core {

namespace {

constexpr int kSideOrdinalBase = 112; // B2 maps to physical 00.
constexpr int kDarkBoundaryPhysical = 47;
constexpr int kOfficialContinuationPhysical = 48;

constexpr std::string_view kOrdinaryRole = "shared-continuation:ordinary:";
constexpr std::string_view kDivergentRole = "shared-continuation:divergent:";
constexpr std::string_view kBodyEndRole = "shared-continuation:body-end:";
constexpr std::string_view kMovedJoinRole = "shared-continuation:moved-join:";
constexpr std::string_view kMovedStoreRole = "shared-continuation:moved-store:";
constexpr std::string_view kMovedReturnRole = "shared-continuation:moved-return:";

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

IrKind basic_kind_for_opcode(int opcode) {
  const std::vector<IrOp> raised =
      raise_machine_to_ir({MachineItem::op(opcode, opcode_by_code(opcode).name)});
  return raised.empty() ? IrKind::Plain : raised.front().kind;
}

bool is_indirect_flow_kind(IrKind kind) {
  return kind == IrKind::IndirectJump || kind == IrKind::IndirectCall ||
         kind == IrKind::IndirectCondJump;
}

bool straight_fallthrough_opcode(int opcode) {
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

std::optional<int> resolved_address(const MachineItem& item, const ArtifactIndex& index,
                                    AddressSpaceModel model) {
  if (item.kind != MachineItemKind::Address)
    return std::nullopt;
  if (item.formal_opcode.has_value()) {
    try {
      return formal_address_info(*item.formal_opcode, model).actual;
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }
  if (const auto* fixed = std::get_if<int>(&item.target))
    return *fixed;
  const auto* label = std::get_if<std::string>(&item.target);
  if (label == nullptr)
    return std::nullopt;
  const auto found = index.label_addresses.find(*label);
  return found == index.label_addresses.end() ? std::nullopt : std::optional<int>(found->second);
}

bool is_long_side_alias(const FormalAddressInfo& info) {
  return (info.kind == FormalAddressKind::LongSide || info.kind == FormalAddressKind::Dark) &&
         !info.one_command;
}

int side_alias_for_physical(int physical, AddressSpaceModel model) {
  const int ordinal = kSideOrdinalBase + physical;
  for (int high = 0x0b; high <= 0x0f; ++high) {
    const int low = ordinal - high * 10;
    if (low < 0 || low > 0x0f)
      continue;
    const int opcode = high * 16 + low;
    try {
      const FormalAddressInfo info = formal_address_info(opcode, model);
      if (is_long_side_alias(info) && info.actual == physical)
        return opcode;
    } catch (const std::exception&) {
      continue;
    }
  }
  return -1;
}

void append_comment(MachineItem& item, const std::string& suffix) {
  item.comment = item.comment.has_value() && !item.comment->empty()
                     ? std::optional<std::string>(*item.comment + "; " + suffix)
                     : std::optional<std::string>(suffix);
}

const SharedHelperContinuationCall* find_call(const SharedHelperContinuationProof& proof,
                                              std::size_t call_item) {
  const auto found = std::find_if(
      proof.calls.begin(), proof.calls.end(),
      [&](const SharedHelperContinuationCall& call) { return call.call_item_index == call_item; });
  return found == proof.calls.end() ? nullptr : &*found;
}

bool labels_between(const std::vector<MachineItem>& items, std::size_t after, std::size_t through) {
  for (std::size_t index = after + 1U; index <= through && index < items.size(); ++index) {
    if (items.at(index).kind == MachineItemKind::Label)
      return true;
  }
  return false;
}

std::set<int> removed_cell_addresses(const ArtifactIndex& index,
                                     const SharedHelperContinuationProof& continuation,
                                     const SharedHelperContinuationCall& natural,
                                     const std::vector<std::size_t>& tail_items) {
  std::set<int> removed;
  removed.insert(index.item_addresses.at(natural.call_item_index));
  removed.insert(index.item_addresses.at(natural.operand_item_index));
  for (const SharedHelperContinuationCall& call : continuation.calls) {
    if (!call.ordinary)
      continue;
    removed.insert(index.item_addresses.at(call.join_item_index));
    removed.insert(index.item_addresses.at(call.store_item_index));
  }
  for (const std::size_t item : tail_items)
    removed.insert(index.item_addresses.at(item));
  return removed;
}

bool external_entries_are_safe(const std::vector<MachineItem>& items, const ArtifactIndex& index,
                               const SharedHelperContinuationProof& continuation,
                               const SharedHelperContinuationCall& natural,
                               const std::vector<std::size_t>& tail_items,
                               const NaturalComponentLayoutOptions& options,
                               std::vector<std::string>& reasons) {
  const std::set<int> removed = removed_cell_addresses(index, continuation, natural, tail_items);
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind == MachineItemKind::Address) {
      const auto* label = std::get_if<std::string>(&item.target);
      if (label == nullptr || *label == continuation.helper_label)
        continue;
      const auto target = index.label_addresses.find(*label);
      if (target != index.label_addresses.end() && removed.contains(target->second)) {
        reasons.push_back("direct flow can enter a removed call, continuation, or tail cell");
        return false;
      }
    }
    if (item.kind != MachineItemKind::Op ||
        !is_indirect_flow_kind(basic_kind_for_opcode(item.opcode))) {
      continue;
    }
    const auto targets = options.continuation_options.proved_indirect_flow_targets.find(item_index);
    if (targets == options.continuation_options.proved_indirect_flow_targets.end()) {
      reasons.push_back("indirect flow lacks a complete target map");
      return false;
    }
    for (const int target : targets->second) {
      if (removed.contains(target)) {
        reasons.push_back("proved indirect flow can enter a removed layout cell");
        return false;
      }
    }
  }
  return true;
}

int cells_before(const ArtifactIndex& index, const std::set<std::size_t>& removed_items,
                 std::size_t before_item) {
  int count = 0;
  for (const std::size_t item : removed_items) {
    if (item < before_item && item < index.item_addresses.size())
      ++count;
  }
  return count;
}

struct RewrittenArtifact {
  std::vector<MachineItem> items;
  std::map<std::size_t, std::size_t> old_to_new_item;
};

RewrittenArtifact build_rewritten_artifact(const std::vector<MachineItem>& items,
                                           const SharedHelperContinuationProof& continuation,
                                           const SharedHelperContinuationCall& natural,
                                           const std::vector<std::size_t>& tail_removed_items,
                                           int divergent_formal_opcode) {
  std::set<std::size_t> skipped;
  skipped.insert(natural.call_item_index);
  skipped.insert(natural.operand_item_index);
  for (const SharedHelperContinuationCall& call : continuation.calls) {
    if (call.ordinary) {
      skipped.insert(call.join_item_index);
      skipped.insert(call.store_item_index);
    }
  }
  skipped.insert(tail_removed_items.begin(), tail_removed_items.end());
  for (std::size_t item = continuation.helper_label_item_index;
       item < continuation.helper_block_end_item_index; ++item) {
    skipped.insert(item);
  }

  const SharedHelperContinuationCall* canonical = nullptr;
  for (const SharedHelperContinuationCall& call : continuation.calls) {
    if (call.ordinary && call.call_item_index != natural.call_item_index) {
      canonical = &call;
      break;
    }
  }

  RewrittenArtifact rewritten;
  rewritten.items.reserve(items.size());
  auto append_old = [&](std::size_t old_index, MachineItem item) {
    rewritten.old_to_new_item[old_index] = rewritten.items.size();
    rewritten.items.push_back(std::move(item));
  };
  auto append_role = [](MachineItem& item, std::string role) {
    item.roles.push_back(std::move(role));
  };

  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    if (item_index == natural.call_item_index && canonical != nullptr) {
      for (std::size_t helper_item = continuation.helper_label_item_index;
           helper_item < continuation.helper_return_item_index; ++helper_item) {
        MachineItem moved = items.at(helper_item);
        if (const std::optional<std::size_t> previous =
                previous_cell_item(items, continuation.helper_return_item_index);
            previous.has_value() && helper_item == *previous) {
          append_role(moved, std::string(kBodyEndRole) + continuation.helper_label);
        }
        append_old(helper_item, std::move(moved));
      }

      MachineItem moved_join = items.at(canonical->join_item_index);
      append_role(moved_join, std::string(kMovedJoinRole) + continuation.helper_label);
      rewritten.items.push_back(std::move(moved_join));
      MachineItem moved_store = items.at(canonical->store_item_index);
      append_role(moved_store, std::string(kMovedStoreRole) + continuation.helper_label);
      rewritten.items.push_back(std::move(moved_store));

      MachineItem moved_return = items.at(continuation.helper_return_item_index);
      append_role(moved_return, std::string(kMovedReturnRole) + continuation.helper_label);
      append_old(continuation.helper_return_item_index, std::move(moved_return));
      for (std::size_t helper_item = continuation.helper_return_item_index + 1U;
           helper_item < continuation.helper_block_end_item_index; ++helper_item) {
        append_old(helper_item, items.at(helper_item));
      }
    }
    if (skipped.contains(item_index))
      continue;

    MachineItem item = items.at(item_index);
    const SharedHelperContinuationCall* call = find_call(continuation, item_index);
    if (call != nullptr) {
      append_role(item, std::string(call->ordinary ? kOrdinaryRole : kDivergentRole) +
                            continuation.helper_label);
    }
    const auto operand_call = std::find_if(continuation.calls.begin(), continuation.calls.end(),
                                           [&](const SharedHelperContinuationCall& candidate) {
                                             return candidate.operand_item_index == item_index;
                                           });
    if (operand_call != continuation.calls.end()) {
      if (!operand_call->ordinary) {
        item.formal_opcode = divergent_formal_opcode;
        append_comment(item, "divergent side-space helper return");
        append_role(item, std::string(kDivergentRole) + continuation.helper_label);
      } else {
        item.formal_opcode.reset();
        append_role(item, std::string(kOrdinaryRole) + continuation.helper_label);
      }
    }
    append_old(item_index, std::move(item));
  }
  return rewritten;
}

std::map<std::size_t, int> item_addresses(const std::vector<MachineItem>& items) {
  std::map<std::size_t, int> result;
  int address = 0;
  for (std::size_t item = 0; item < items.size(); ++item) {
    result[item] = address;
    if (items.at(item).kind != MachineItemKind::Label)
      ++address;
  }
  return result;
}

bool reindex_indirect_targets(const std::vector<MachineItem>& input,
                              const ArtifactIndex& input_index, const RewrittenArtifact& rewritten,
                              const NaturalComponentLayoutOptions& options,
                              std::map<std::size_t, std::vector<int>>& output_targets,
                              std::vector<std::string>& reasons) {
  const std::map<std::size_t, int> output_addresses = item_addresses(rewritten.items);
  for (std::size_t item_index = 0; item_index < input.size(); ++item_index) {
    const MachineItem& item = input.at(item_index);
    if (item.kind != MachineItemKind::Op ||
        !is_indirect_flow_kind(basic_kind_for_opcode(item.opcode))) {
      continue;
    }
    const auto proof = options.continuation_options.proved_indirect_flow_targets.find(item_index);
    if (proof == options.continuation_options.proved_indirect_flow_targets.end()) {
      reasons.push_back("indirect flow lacks a complete input target proof");
      return false;
    }
    const auto new_flow = rewritten.old_to_new_item.find(item_index);
    if (new_flow == rewritten.old_to_new_item.end())
      continue; // A proved terminal transfer may be removed.
    std::vector<int> targets;
    for (const int target : proof->second) {
      const auto old_target_item = input_index.cell_items.find(target);
      if (old_target_item == input_index.cell_items.end()) {
        reasons.push_back("indirect target names no input cell");
        return false;
      }
      const auto new_target_item = rewritten.old_to_new_item.find(old_target_item->second);
      if (new_target_item == rewritten.old_to_new_item.end()) {
        reasons.push_back("indirect target names a removed layout cell");
        return false;
      }
      const auto new_address = output_addresses.find(new_target_item->second);
      if (new_address == output_addresses.end()) {
        reasons.push_back("reindexed indirect target has no output address");
        return false;
      }
      targets.push_back(new_address->second);
    }
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    output_targets[new_flow->second] = std::move(targets);
  }
  return true;
}

bool final_artifact_is_proved(const std::vector<MachineItem>& items,
                              NaturalComponentLayoutProof& proof,
                              const NaturalComponentLayoutOptions& options,
                              std::vector<std::string>& reasons) {
  const ArtifactIndex index = index_artifact(items);
  if (!index.duplicate_labels.empty()) {
    reasons.push_back("rewritten artifact contains duplicate labels");
    return false;
  }
  if (index.cells != proof.output_cells) {
    reasons.push_back("rewritten artifact has an unexpected cell count");
    return false;
  }
  const auto zero = index.cell_items.find(0);
  if (zero == index.cell_items.end() || items.at(zero->second).kind != MachineItemKind::Op ||
      basic_kind_for_opcode(items.at(zero->second).opcode) != IrKind::Return) {
    reasons.push_back("rewritten artifact has no bare return at physical 00");
    return false;
  }
  const auto helper = index.label_addresses.find(proof.continuation.helper_label);
  if (helper == index.label_addresses.end() ||
      helper->second != proof.relocated_helper_start_address) {
    reasons.push_back("rewritten helper did not occupy the proved natural call hole");
    return false;
  }
  for (int address = proof.relocated_helper_start_address; address <= kDarkBoundaryPhysical;
       ++address) {
    const auto cell = index.cell_items.find(address);
    if (cell == index.cell_items.end() || items.at(cell->second).kind != MachineItemKind::Op ||
        !straight_fallthrough_opcode(items.at(cell->second).opcode)) {
      reasons.push_back("relocated helper body is not straight-line through physical 47");
      return false;
    }
  }
  const auto join = index.cell_items.find(kOfficialContinuationPhysical);
  const auto store = index.cell_items.find(kOfficialContinuationPhysical + 1);
  const auto ret = index.cell_items.find(kOfficialContinuationPhysical + 2);
  if (join == index.cell_items.end() || store == index.cell_items.end() ||
      ret == index.cell_items.end() ||
      items.at(join->second).opcode != proof.continuation.join_opcode ||
      items.at(store->second).opcode != proof.continuation.store_opcode ||
      basic_kind_for_opcode(items.at(ret->second).opcode) != IrKind::Return) {
    reasons.push_back("shared continuation/return is not the proved physical 48..50 suffix");
    return false;
  }

  int ordinary_calls = 0;
  int divergent_calls = 0;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind == MachineItemKind::Op &&
        is_indirect_flow_kind(basic_kind_for_opcode(item.opcode)) &&
        !proof.final_indirect_flow_targets.contains(item_index)) {
      reasons.push_back("rewritten indirect flow lacks a reindexed complete target map");
      return false;
    }
    if (item.kind != MachineItemKind::Address)
      continue;
    const auto* label = std::get_if<std::string>(&item.target);
    if (label == nullptr || *label != proof.continuation.helper_label)
      continue;
    const std::optional<std::size_t> call = previous_cell_item(items, item_index);
    if (!call.has_value() || items.at(*call).kind != MachineItemKind::Op ||
        items.at(*call).opcode != 0x53) {
      reasons.push_back("rewritten helper has a non-call entry");
      return false;
    }
    if (item.formal_opcode.has_value()) {
      if (*item.formal_opcode != proof.divergent_formal_opcode) {
        reasons.push_back("divergent call has the wrong side-space alias");
        return false;
      }
      const FormalAddressInfo formal = formal_address_info(
          *item.formal_opcode, options.continuation_options.address_space_model);
      if (!is_long_side_alias(formal) || formal.actual != helper->second) {
        reasons.push_back("divergent side-space call no longer reaches the helper");
        return false;
      }
      ++divergent_calls;
    } else {
      ++ordinary_calls;
    }
  }
  if (ordinary_calls != 1 || divergent_calls != 1) {
    reasons.push_back("rewritten artifact does not have one ordinary and one divergent call");
    return false;
  }

  const std::optional<std::size_t> helper_item =
      index.label_items.contains(proof.continuation.helper_label)
          ? std::optional<std::size_t>(index.label_items.at(proof.continuation.helper_label))
          : std::nullopt;
  if (!helper_item.has_value()) {
    reasons.push_back("rewritten helper label item is absent");
    return false;
  }
  const std::optional<std::size_t> predecessor = previous_cell_item(items, *helper_item);
  if (!predecessor.has_value() || items.at(*predecessor).kind != MachineItemKind::Op ||
      !straight_fallthrough_opcode(items.at(*predecessor).opcode) ||
      index.item_addresses.at(*predecessor) != helper->second - 1) {
    reasons.push_back("natural helper entry has no unique straight-line predecessor");
    return false;
  }
  return true;
}

} // namespace

NaturalComponentLayoutProof verify_natural_component_layout(
    const std::vector<MachineItem>& items, const std::string& helper_label,
    std::size_t natural_call_item_index, const NaturalComponentLayoutOptions& options) {
  NaturalComponentLayoutProof proof;
  proof.continuation =
      verify_shared_helper_continuation(items, helper_label, options.continuation_options);
  proof.input_cells = proof.continuation.input_cells;
  proof.natural_call_item_index = natural_call_item_index;
  if (!proof.continuation.proved) {
    proof.reasons = proof.continuation.reasons;
    return proof;
  }
  const ArtifactIndex index = index_artifact(items);
  const SharedHelperContinuationCall* natural =
      find_call(proof.continuation, natural_call_item_index);
  if (natural == nullptr || !natural->ordinary) {
    proof.reasons.push_back("natural edge is not one of the two ordinary helper calls");
    return proof;
  }
  proof.natural_operand_item_index = natural->operand_item_index;
  proof.natural_call_input_address = natural->call_address;

  const auto zero = index.cell_items.find(0);
  if (zero == index.cell_items.end() || items.at(zero->second).kind != MachineItemKind::Op ||
      basic_kind_for_opcode(items.at(zero->second).opcode) != IrKind::Return) {
    proof.reasons.push_back("physical 00 is not a bare return");
  }

  const std::optional<std::size_t> predecessor =
      previous_cell_item(items, natural->call_item_index);
  if (!predecessor.has_value() || items.at(*predecessor).kind != MachineItemKind::Op ||
      !straight_fallthrough_opcode(items.at(*predecessor).opcode) ||
      labels_between(items, *predecessor, natural->call_item_index)) {
    proof.reasons.push_back("natural call has no unique straight-line fallthrough predecessor");
  }

  const std::optional<std::size_t> tail = next_cell_item(items, natural->store_item_index);
  if (!tail.has_value() || items.at(*tail).kind != MachineItemKind::Op) {
    proof.reasons.push_back("natural continuation has no terminal tail transfer");
  } else {
    proof.natural_tail_item_index = *tail;
    const IrKind tail_kind = basic_kind_for_opcode(items.at(*tail).opcode);
    if (tail_kind == IrKind::Return) {
      proof.tail_kind = NaturalComponentTailKind::BareReturn;
      proof.natural_tail_removed_item_indices = {*tail};
    } else if (tail_kind == IrKind::Jump) {
      const std::optional<std::size_t> operand = next_cell_item(items, *tail);
      const std::optional<int> target =
          operand.has_value() ? resolved_address(items.at(*operand), index,
                                                 options.continuation_options.address_space_model)
                              : std::nullopt;
      if (!operand.has_value() || items.at(*operand).kind != MachineItemKind::Address ||
          target != 1 || !options.proved_empty_return_stack) {
        proof.reasons.push_back(
            "natural direct tail is not a proved empty-stack jump to physical 01");
      } else {
        proof.tail_kind = NaturalComponentTailKind::EmptyStackJumpToOne;
        proof.natural_tail_removed_item_indices = {*tail, *operand};
      }
    } else if (tail_kind == IrKind::IndirectJump) {
      const auto targets = options.continuation_options.proved_indirect_flow_targets.find(*tail);
      if (targets == options.continuation_options.proved_indirect_flow_targets.end() ||
          targets->second != std::vector<int>{1} || !options.proved_empty_return_stack) {
        proof.reasons.push_back(
            "natural indirect tail is not an exhaustive empty-stack jump to physical 01");
      } else {
        proof.tail_kind = NaturalComponentTailKind::EmptyStackJumpToOne;
        proof.natural_tail_removed_item_indices = {*tail};
      }
    } else {
      proof.reasons.push_back("natural helper call is not in a proved terminal position");
    }
    if (tail.has_value() && labels_between(items, natural->store_item_index, *tail))
      proof.reasons.push_back("natural terminal tail contains an externally enterable label");
  }

  std::set<std::size_t> removed_before_insertion;
  for (const SharedHelperContinuationCall& call : proof.continuation.calls) {
    if (call.ordinary) {
      removed_before_insertion.insert(call.join_item_index);
      removed_before_insertion.insert(call.store_item_index);
    }
  }
  for (std::size_t helper_item = proof.continuation.helper_label_item_index;
       helper_item < proof.continuation.helper_block_end_item_index; ++helper_item) {
    if (items.at(helper_item).kind != MachineItemKind::Label)
      removed_before_insertion.insert(helper_item);
  }
  proof.relocated_helper_start_address =
      natural->call_address -
      cells_before(index, removed_before_insertion, natural->call_item_index);
  proof.relocated_helper_body_end_address =
      proof.relocated_helper_start_address + proof.continuation.helper_body_cells - 1;
  proof.relocated_continuation_start_address = proof.relocated_helper_body_end_address + 1;
  proof.relocated_return_address =
      proof.relocated_continuation_start_address + proof.continuation.continuation_cells;
  if (proof.relocated_helper_body_end_address != kDarkBoundaryPhysical) {
    proof.reasons.push_back("natural call hole does not place helper body end at physical 47");
  }
  proof.divergent_formal_opcode = side_alias_for_physical(
      proof.relocated_helper_start_address, options.continuation_options.address_space_model);
  if (proof.divergent_formal_opcode < 0)
    proof.reasons.push_back("no long-side alias exists for relocated helper entry");

  if (!external_entries_are_safe(items, index, proof.continuation, *natural,
                                 proof.natural_tail_removed_item_indices, options, proof.reasons)) {
    return proof;
  }
  const int tail_cells = static_cast<int>(proof.natural_tail_removed_item_indices.size());
  proof.output_cells = proof.input_cells - proof.continuation.continuation_cells - 2 - tail_cells;
  proof.proved = proof.reasons.empty();
  return proof;
}

NaturalComponentLayoutResult rewrite_natural_component_layout(
    const std::vector<MachineItem>& items, const std::string& helper_label,
    std::size_t natural_call_item_index, const NaturalComponentLayoutOptions& options) {
  NaturalComponentLayoutResult result;
  result.items = items;
  result.proof =
      verify_natural_component_layout(items, helper_label, natural_call_item_index, options);
  if (!result.proof.proved)
    return result;
  const SharedHelperContinuationCall* natural =
      find_call(result.proof.continuation, natural_call_item_index);
  if (natural == nullptr) {
    result.proof.proved = false;
    result.proof.reasons.push_back("natural call disappeared after verification");
    return result;
  }
  const ArtifactIndex input_index = index_artifact(items);
  RewrittenArtifact rewritten = build_rewritten_artifact(
      items, result.proof.continuation, *natural, result.proof.natural_tail_removed_item_indices,
      result.proof.divergent_formal_opcode);
  if (!reindex_indirect_targets(items, input_index, rewritten, options,
                                result.proof.final_indirect_flow_targets, result.proof.reasons)) {
    result.proof.proved = false;
    return result;
  }
  std::vector<std::string> final_reasons;
  if (!final_artifact_is_proved(rewritten.items, result.proof, options, final_reasons)) {
    result.proof.proved = false;
    result.proof.reasons.insert(result.proof.reasons.end(), final_reasons.begin(),
                                final_reasons.end());
    return result;
  }

  result.items = std::move(rewritten.items);
  result.proof.final_artifact_proved = true;
  result.applied = 1;
  result.removed_cells = result.proof.input_cells - result.proof.output_cells;
  result.optimizations.push_back(passes::AppliedOptimization{
      .name = "natural-component-layout",
      .detail = "Shared a two-cell continuation across two ordinary calls to " + helper_label +
                ", rebound the divergent call through a side-space return, and naturalized one "
                "proved terminal call edge; removed " +
                std::to_string(result.removed_cells) + " cells.",
  });
  return result;
}

NaturalComponentLayoutResult
optimize_natural_component_layout(const std::vector<MachineItem>& items,
                                  const NaturalComponentLayoutOptions& options) {
  std::set<std::string> labels;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label)
      labels.insert(item.name);
  }
  NaturalComponentLayoutResult first_rejection;
  first_rejection.items = items;
  for (const std::string& label : labels) {
    const SharedHelperContinuationProof continuation =
        verify_shared_helper_continuation(items, label, options.continuation_options);
    if (!continuation.proved)
      continue;
    for (const std::size_t call : continuation.ordinary_call_item_indices) {
      NaturalComponentLayoutResult candidate =
          rewrite_natural_component_layout(items, label, call, options);
      if (candidate.applied > 0)
        return candidate;
      if (first_rejection.proof.reasons.empty() && !candidate.proof.reasons.empty())
        first_rejection.proof = std::move(candidate.proof);
    }
  }
  return first_rejection;
}

} // namespace mkpro::core
