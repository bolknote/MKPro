#include "mkpro/core/shared_helper_continuation.hpp"

#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/stack_value_equivalence.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core {

namespace {

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

bool is_straight_helper_opcode(int opcode) {
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

bool supported_commutative_join(int opcode) {
  switch (opcode) {
  case 0x10: // +
  case 0x12: // multiplication
  case 0x36: // K max
  case 0x37: // K and
  case 0x38: // K or
  case 0x39: // K xor
    return true;
  default:
    return false;
  }
}

bool direct_store_opcode(int opcode) {
  return opcode >= 0x40 && opcode <= 0x4e;
}

std::optional<int> address_target(const MachineItem& item, const ArtifactIndex& index,
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

bool direct_address_artifact_is_relocatable(const std::vector<MachineItem>& items,
                                            const ArtifactIndex& index,
                                            std::vector<std::string>& reasons) {
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind == MachineItemKind::Op && opcode_by_code(item.opcode).takes_address) {
      const std::optional<std::size_t> operand = next_cell_item(items, item_index);
      if (!operand.has_value() || items.at(*operand).kind != MachineItemKind::Address) {
        reasons.push_back("address-taking opcode has no adjacent address operand");
        return false;
      }
      continue;
    }
    if (item.kind != MachineItemKind::Address)
      continue;
    const std::optional<std::size_t> flow = previous_cell_item(items, item_index);
    if (!flow.has_value() || items.at(*flow).kind != MachineItemKind::Op ||
        !opcode_by_code(items.at(*flow).opcode).takes_address) {
      reasons.push_back("artifact contains an orphan address operand");
      return false;
    }
    if (item.formal_opcode.has_value() || std::holds_alternative<int>(item.target)) {
      reasons.push_back("fixed numeric or formal address is unsafe across continuation layout");
      return false;
    }
    const auto* label = std::get_if<std::string>(&item.target);
    if (label == nullptr || !index.label_addresses.contains(*label)) {
      reasons.push_back("address operand references an unresolved label");
      return false;
    }
  }
  return true;
}

struct ContinuationShape {
  std::size_t join_item = 0;
  std::size_t store_item = 0;
  int join_opcode = -1;
  int store_opcode = -1;
  bool supported = false;
};

ContinuationShape continuation_shape(const std::vector<MachineItem>& items,
                                     std::size_t operand_item) {
  ContinuationShape shape;
  const std::optional<std::size_t> join = next_cell_item(items, operand_item);
  if (!join.has_value())
    return shape;
  const std::optional<std::size_t> store = next_cell_item(items, *join);
  if (!store.has_value())
    return shape;
  shape.join_item = *join;
  shape.store_item = *store;
  if (items.at(*join).kind != MachineItemKind::Op || items.at(*store).kind != MachineItemKind::Op) {
    return shape;
  }
  shape.join_opcode = items.at(*join).opcode;
  shape.store_opcode = items.at(*store).opcode;
  shape.supported =
      supported_commutative_join(shape.join_opcode) && direct_store_opcode(shape.store_opcode);
  return shape;
}

bool range_has_label(const std::vector<MachineItem>& items, std::size_t after,
                     std::size_t through) {
  for (std::size_t index = after + 1U; index <= through && index < items.size(); ++index) {
    if (items.at(index).kind == MachineItemKind::Label)
      return true;
  }
  return false;
}

int equality_state_key(const StackValueEqualityState& state) {
  int key = state.x2_equal ? 16 : 0;
  for (std::size_t index = 0; index < state.stack_equal.size(); ++index) {
    if (state.stack_equal.at(index))
      key |= 1 << static_cast<int>(index);
  }
  return key;
}

bool apply_x2_edge(StackValueEqualityState& state, X2Effect effect) {
  switch (effect) {
  case X2Effect::Affects:
    state.x2_equal = state.stack_equal.at(0);
    return true;
  case X2Effect::Preserves:
    return true;
  case X2Effect::Restores:
  case X2Effect::Unknown:
    return false;
  }
  return false;
}

std::optional<std::size_t> cell_item_for_address(const ArtifactIndex& index, int address) {
  const auto found = index.cell_items.find(address);
  return found == index.cell_items.end() ? std::nullopt : std::optional<std::size_t>(found->second);
}

bool enqueue_successor(std::deque<std::pair<std::size_t, StackValueEqualityState>>& work,
                       const ArtifactIndex& index, int target, const StackValueEqualityState& state,
                       std::vector<std::string>& reasons) {
  const std::optional<std::size_t> item = cell_item_for_address(index, target);
  if (!item.has_value()) {
    reasons.push_back("X2 convergence path leaves the materialized artifact");
    return false;
  }
  work.emplace_back(*item, state);
  return true;
}

bool x2_difference_converges(const std::vector<MachineItem>& items, const ArtifactIndex& index,
                             std::size_t start_item, const SharedHelperContinuationOptions& options,
                             std::vector<std::string>& reasons) {
  StackValueEqualityState initial;
  initial.stack_equal = {true, true, true, true};
  initial.x2_equal = false;
  std::deque<std::pair<std::size_t, StackValueEqualityState>> work;
  work.emplace_back(start_item, initial);
  std::set<std::pair<std::size_t, int>> visited;
  int explored = 0;

  while (!work.empty()) {
    auto [item_index, state] = work.front();
    work.pop_front();
    if (stack_values_fully_equal(state))
      continue;
    if (++explored > options.maximum_convergence_states) {
      reasons.push_back("X2 convergence proof exceeded its bounded state budget");
      return false;
    }
    if (!visited.insert({item_index, equality_state_key(state)}).second) {
      reasons.push_back("X2 difference can circulate through a non-converged cycle");
      return false;
    }
    if (item_index >= items.size()) {
      reasons.push_back("X2 convergence path reaches outside the artifact");
      return false;
    }
    const MachineItem& item = items.at(item_index);
    if (item.kind == MachineItemKind::Label) {
      const std::optional<std::size_t> next = next_cell_item(items, item_index);
      if (!next.has_value()) {
        reasons.push_back("X2 difference reaches the end of the artifact");
        return false;
      }
      work.emplace_back(*next, state);
      continue;
    }
    if (item.kind == MachineItemKind::Address) {
      const std::optional<std::size_t> next = next_cell_item(items, item_index);
      if (!next.has_value()) {
        reasons.push_back("X2 difference reaches a terminal address operand");
        return false;
      }
      work.emplace_back(*next, state);
      continue;
    }

    const IrKind kind = basic_kind_for_opcode(item.opcode);
    if (kind == IrKind::Return || kind == IrKind::Stop) {
      if (!state.stack_equal.at(0)) {
        reasons.push_back("return/stop observes unequal visible X before X2 converges");
        return false;
      }
      state.x2_equal = true;
      continue;
    }
    if (kind == IrKind::Call || kind == IrKind::IndirectCall) {
      reasons.push_back("a nested call can observe the moved return's unequal X2 value");
      return false;
    }

    if (kind == IrKind::Jump || kind == IrKind::CondJump || kind == IrKind::Loop) {
      const std::optional<std::size_t> operand = next_cell_item(items, item_index);
      if (!operand.has_value() || items.at(*operand).kind != MachineItemKind::Address) {
        reasons.push_back("direct flow in X2 proof has no address operand");
        return false;
      }
      const std::optional<int> target =
          address_target(items.at(*operand), index, options.address_space_model);
      if (!target.has_value()) {
        reasons.push_back("direct flow target in X2 proof cannot be resolved");
        return false;
      }
      if (kind == IrKind::Jump) {
        if (!enqueue_successor(work, index, *target, state, reasons))
          return false;
        continue;
      }

      const OpcodeInfo& info = opcode_by_code(item.opcode);
      if (!info.conditional_x2_effect.has_value()) {
        reasons.push_back("conditional flow lacks a path-specific X2 model");
        return false;
      }
      StackValueEqualityState jump_state = state;
      if (!apply_x2_edge(jump_state, info.conditional_x2_effect->jump)) {
        reasons.push_back("conditional jump can restore or obscure unequal X2");
        return false;
      }
      if (!stack_values_fully_equal(jump_state) &&
          !enqueue_successor(work, index, *target, jump_state, reasons)) {
        return false;
      }
      StackValueEqualityState fallthrough_state = state;
      if (!apply_x2_edge(fallthrough_state, info.conditional_x2_effect->fallthrough)) {
        reasons.push_back("conditional fallthrough can restore or obscure unequal X2");
        return false;
      }
      if (!stack_values_fully_equal(fallthrough_state)) {
        const std::optional<std::size_t> fallthrough = next_cell_item(items, *operand);
        if (!fallthrough.has_value()) {
          reasons.push_back("non-converged conditional fallthrough leaves the artifact");
          return false;
        }
        work.emplace_back(*fallthrough, fallthrough_state);
      }
      continue;
    }

    if (is_indirect_flow_kind(kind)) {
      const auto targets = options.proved_indirect_flow_targets.find(item_index);
      if (targets == options.proved_indirect_flow_targets.end()) {
        reasons.push_back("indirect flow in X2 proof lacks a complete target map");
        return false;
      }
      if (kind == IrKind::IndirectJump) {
        for (const int target : targets->second) {
          if (!enqueue_successor(work, index, target, state, reasons))
            return false;
        }
        continue;
      }
      const OpcodeInfo& info = opcode_by_code(item.opcode);
      if (!info.conditional_x2_effect.has_value()) {
        reasons.push_back("indirect conditional lacks a path-specific X2 model");
        return false;
      }
      StackValueEqualityState jump_state = state;
      if (!apply_x2_edge(jump_state, info.conditional_x2_effect->jump)) {
        reasons.push_back("indirect conditional jump can observe unequal X2");
        return false;
      }
      if (!stack_values_fully_equal(jump_state)) {
        for (const int target : targets->second) {
          if (!enqueue_successor(work, index, target, jump_state, reasons))
            return false;
        }
      }
      StackValueEqualityState fallthrough_state = state;
      if (!apply_x2_edge(fallthrough_state, info.conditional_x2_effect->fallthrough)) {
        reasons.push_back("indirect conditional fallthrough can observe unequal X2");
        return false;
      }
      if (!stack_values_fully_equal(fallthrough_state)) {
        const std::optional<std::size_t> fallthrough = next_cell_item(items, item_index);
        if (!fallthrough.has_value()) {
          reasons.push_back("non-converged indirect fallthrough leaves the artifact");
          return false;
        }
        work.emplace_back(*fallthrough, fallthrough_state);
      }
      continue;
    }

    StackValueEqualityStepKind step_kind = StackValueEqualityStepKind::Plain;
    if (kind == IrKind::Recall || kind == IrKind::IndirectRecall)
      step_kind = StackValueEqualityStepKind::Recall;
    else if (kind == IrKind::Store || kind == IrKind::IndirectStore)
      step_kind = StackValueEqualityStepKind::Store;
    const StackValueEqualityTransfer transfer =
        transfer_stack_value_equality(state, item.opcode, step_kind);
    if (transfer == StackValueEqualityTransfer::Rejected) {
      reasons.push_back("continuation can observe unequal X2 before convergence");
      return false;
    }
    if (transfer == StackValueEqualityTransfer::Converged)
      continue;
    const std::optional<std::size_t> next = next_cell_item(items, item_index);
    if (!next.has_value()) {
      reasons.push_back("X2 difference reaches the end of the artifact");
      return false;
    }
    work.emplace_back(*next, state);
  }
  return true;
}

bool no_external_entry_to_continuations(const std::vector<MachineItem>& items,
                                        const ArtifactIndex& index,
                                        const std::vector<SharedHelperContinuationCall>& calls,
                                        const SharedHelperContinuationOptions& options,
                                        std::vector<std::string>& reasons) {
  std::set<int> continuation_addresses;
  for (const SharedHelperContinuationCall& call : calls) {
    if (!call.ordinary)
      continue;
    continuation_addresses.insert(index.item_addresses.at(call.join_item_index));
    continuation_addresses.insert(index.item_addresses.at(call.store_item_index));
  }
  for (const auto& [flow_item, targets] : options.proved_indirect_flow_targets) {
    if (flow_item >= items.size()) {
      reasons.push_back("indirect target map contains an out-of-range item index");
      return false;
    }
    for (const int target : targets) {
      if (continuation_addresses.contains(target)) {
        reasons.push_back("proved indirect flow can enter a removable continuation cell");
        return false;
      }
    }
  }
  return true;
}

} // namespace

SharedHelperContinuationProof
verify_shared_helper_continuation(const std::vector<MachineItem>& items,
                                  const std::string& helper_label,
                                  const SharedHelperContinuationOptions& options) {
  SharedHelperContinuationProof proof;
  proof.helper_label = helper_label;
  const ArtifactIndex index = index_artifact(items);
  proof.input_cells = index.cells;

  if (index.duplicate_labels.contains(helper_label)) {
    proof.reasons.push_back("helper label is duplicated");
    return proof;
  }
  const auto root_item = index.label_items.find(helper_label);
  const auto root_address = index.label_addresses.find(helper_label);
  if (root_item == index.label_items.end() || root_address == index.label_addresses.end()) {
    proof.reasons.push_back("helper label is absent from the artifact");
    return proof;
  }
  proof.helper_label_item_index = root_item->second;
  proof.helper_body_start_address = root_address->second;
  if (!direct_address_artifact_is_relocatable(items, index, proof.reasons))
    return proof;

  bool found_return = false;
  for (std::size_t item_index = root_item->second + 1U; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    const int address = index.item_addresses.at(item_index);
    if (item.kind == MachineItemKind::Label) {
      if (!found_return) {
        proof.reasons.push_back("helper has a secondary executable entry label");
        break;
      }
      if (item.procedure_boundary == "end") {
        proof.helper_block_end_item_index = item_index + 1U;
        continue;
      }
      break;
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
      proof.helper_return_item_index = item_index;
      proof.helper_return_address = address;
      proof.helper_block_end_item_index = item_index + 1U;
      continue;
    }
    if (!is_straight_helper_opcode(item.opcode)) {
      proof.reasons.push_back("helper body contains control flow");
      break;
    }
    proof.helper_body_end_address = address;
    ++proof.helper_body_cells;
  }
  if (!found_return)
    proof.reasons.push_back("helper has no explicit return");
  if (proof.helper_body_cells <= 0)
    proof.reasons.push_back("helper has no straight-line body");

  std::vector<SharedHelperContinuationCall> calls;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind == MachineItemKind::Op &&
        is_indirect_flow_kind(basic_kind_for_opcode(item.opcode)) &&
        !options.proved_indirect_flow_targets.contains(item_index)) {
      proof.reasons.push_back("indirect flow lacks a complete target map");
    }
    if (item.kind != MachineItemKind::Address)
      continue;
    const auto* label = std::get_if<std::string>(&item.target);
    if (label == nullptr || *label != helper_label)
      continue;
    const std::optional<std::size_t> call = previous_cell_item(items, item_index);
    if (!call.has_value() || items.at(*call).kind != MachineItemKind::Op ||
        items.at(*call).opcode != 0x53) {
      proof.reasons.push_back("helper has a non-direct-call reference");
      continue;
    }
    const ContinuationShape shape = continuation_shape(items, item_index);
    calls.push_back(SharedHelperContinuationCall{
        .call_item_index = *call,
        .operand_item_index = item_index,
        .join_item_index = shape.join_item,
        .store_item_index = shape.store_item,
        .call_address = index.item_addresses.at(*call),
    });
  }
  if (calls.size() != 3U)
    proof.reasons.push_back("helper does not have a complete set of exactly three direct calls");

  std::map<std::pair<int, int>, std::vector<std::size_t>> shape_calls;
  for (std::size_t call_index = 0; call_index < calls.size(); ++call_index) {
    const ContinuationShape shape =
        continuation_shape(items, calls.at(call_index).operand_item_index);
    if (shape.supported)
      shape_calls[{shape.join_opcode, shape.store_opcode}].push_back(call_index);
  }
  std::optional<std::pair<int, int>> selected_shape;
  for (const auto& [shape, members] : shape_calls) {
    if (members.size() != 2U)
      continue;
    if (selected_shape.has_value()) {
      proof.reasons.push_back("more than one continuation pair is structurally ambiguous");
      break;
    }
    selected_shape = shape;
  }
  if (!selected_shape.has_value()) {
    proof.reasons.push_back(
        "no unique pair uses the same commutative join and direct store continuation");
  } else {
    proof.join_opcode = selected_shape->first;
    proof.store_opcode = selected_shape->second;
    proof.continuation_cells = 2;
    for (SharedHelperContinuationCall& call : calls) {
      const ContinuationShape shape = continuation_shape(items, call.operand_item_index);
      call.ordinary = shape.supported && shape.join_opcode == proof.join_opcode &&
                      shape.store_opcode == proof.store_opcode;
      if (call.ordinary) {
        if (range_has_label(items, call.operand_item_index, call.store_item_index))
          proof.reasons.push_back("removable continuation contains an entry label");
        proof.ordinary_call_item_indices.push_back(call.call_item_index);
        const std::optional<std::size_t> after_store = next_cell_item(items, call.store_item_index);
        if (!after_store.has_value()) {
          proof.reasons.push_back("ordinary continuation has no following convergence path");
        } else {
          std::vector<std::string> convergence_reasons;
          if (!x2_difference_converges(items, index, *after_store, options, convergence_reasons)) {
            proof.reasons.push_back("ordinary call at physical " +
                                    std::to_string(call.call_address) +
                                    " lacks an X2 convergence proof: " +
                                    (convergence_reasons.empty() ? std::string("unknown reason")
                                                                 : convergence_reasons.front()));
          }
        }
      } else {
        proof.divergent_call_item_index = call.call_item_index;
      }
    }
  }
  proof.calls = calls;

  const int helper_begin = proof.helper_body_start_address;
  const int helper_end = proof.helper_return_address;
  for (const auto& [flow_item, targets] : options.proved_indirect_flow_targets) {
    if (flow_item >= items.size()) {
      proof.reasons.push_back("indirect target map contains an out-of-range item index");
      continue;
    }
    for (const int target : targets) {
      if (target >= helper_begin && target <= helper_end)
        proof.reasons.push_back("proved indirect flow can enter the helper body");
    }
  }
  (void)no_external_entry_to_continuations(items, index, calls, options, proof.reasons);

  proof.proved = proof.reasons.empty();
  return proof;
}

SharedHelperContinuationProof
find_shared_helper_continuation(const std::vector<MachineItem>& items,
                                const SharedHelperContinuationOptions& options) {
  std::set<std::string> labels;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label)
      labels.insert(item.name);
  }
  SharedHelperContinuationProof first_rejection;
  for (const std::string& label : labels) {
    SharedHelperContinuationProof candidate =
        verify_shared_helper_continuation(items, label, options);
    if (candidate.proved)
      return candidate;
    if (first_rejection.reasons.empty() && !candidate.reasons.empty())
      first_rejection = std::move(candidate);
  }
  return first_rejection;
}

} // namespace mkpro::core
