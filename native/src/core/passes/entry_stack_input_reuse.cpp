#include "mkpro/core/passes/entry_stack_input_reuse.hpp"

#include "mkpro/core/helper_invariant_recall_hoist.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/passes/cfg.hpp"
#include "mkpro/core/passes/recall_removal.hpp"
#include "mkpro/core/result.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <array>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

namespace {

// Four-slot generalization of flow-x-reuse: slots[0..3] hold the register
// names whose stored values provably equal the current X/Y/Z/T stack slots on
// every CFG path reaching the point.
struct EntryStackState {
  std::array<RegisterValueSet, 4> slots;

  bool operator==(const EntryStackState&) const = default;
};

// Fixed callee-entry facts keyed by the index of a proved-transparent direct
// call. A seed replaces the call edge's transfer output verbatim, so facts can
// never flow through a second call (single-level seeding only).
using EntryStackSeeds = std::map<std::size_t, EntryStackState>;

bool same_state_value(const EntryStackState& left, const std::optional<EntryStackState>& right) {
  return right.has_value() && left == *right;
}

EntryStackState join_states(const std::optional<EntryStackState>& current,
                            const EntryStackState& incoming) {
  if (!current.has_value())
    return incoming;
  EntryStackState joined;
  for (std::size_t slot = 0; slot < joined.slots.size(); ++slot) {
    for (const std::string& register_name : current->slots.at(slot)) {
      if (incoming.slots.at(slot).contains(register_name))
        joined.slots.at(slot).insert(register_name);
    }
  }
  return joined;
}

EntryStackState lift_state(const EntryStackState& input, RegisterValueSet fresh_x) {
  EntryStackState output;
  output.slots = {std::move(fresh_x), input.slots.at(0), input.slots.at(1), input.slots.at(2)};
  return output;
}

EntryStackState erase_register(EntryStackState state, const std::string& register_name) {
  for (RegisterValueSet& slot : state.slots)
    slot.erase(register_name);
  return state;
}

EntryStackState erase_mutated_indirect_selector(EntryStackState state,
                                                const std::string& register_name) {
  if (!mkpro::core::is_stable_indirect_selector(register_name))
    return erase_register(std::move(state), register_name);
  return state;
}

EntryStackState transfer_plain_state(const EntryStackState& input, const IrOp& op) {
  // Stack-only commands whose metadata is intentionally conservative (see
  // helper_invariant_recall_hoist.cpp): the swap permutation is modelled
  // exactly, while F Bx is cleared because its X1 interplay is not tracked.
  if (op.opcode == 0x14) {
    EntryStackState output;
    output.slots = {input.slots.at(1), input.slots.at(0), input.slots.at(2), input.slots.at(3)};
    return output;
  }
  if (op.opcode == 0x0f)
    return {};

  switch (analyze_x2_stack_effect(op).stack_effect) {
  case StackEffect::Preserves: {
    if (plain_preserves_x_value(op))
      return input;
    EntryStackState output = input;
    output.slots.at(0).clear();
    return output;
  }
  case StackEffect::Shifts:
    return lift_state(input, plain_preserves_x_value(op) ? input.slots.at(0)
                                                         : RegisterValueSet{});
  case StackEffect::ConsumeYDrop: {
    EntryStackState output;
    output.slots = {RegisterValueSet{}, input.slots.at(2), input.slots.at(3), input.slots.at(3)};
    return output;
  }
  case StackEffect::ConsumeYKeep: {
    EntryStackState output;
    output.slots = {RegisterValueSet{}, input.slots.at(1), input.slots.at(2), input.slots.at(3)};
    return output;
  }
  case StackEffect::Exposes: {
    EntryStackState output;
    output.slots = {input.slots.at(1), input.slots.at(2), input.slots.at(3), RegisterValueSet{}};
    return output;
  }
  case StackEffect::Barrier:
  case StackEffect::Unknown:
    return {};
  }
  return {};
}

EntryStackState transfer_state(const EntryStackState& input, const IrOp& op, CfgEdgeKind edge) {
  if (has_rewrite_barrier(op))
    return {};

  switch (op.kind) {
  case IrKind::Label:
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::OrphanAddress:
    return input;
  case IrKind::Store:
  case IrKind::IndirectStore: {
    EntryStackState output = op.kind == IrKind::IndirectStore
                                 ? erase_mutated_indirect_selector(input, op.register_name)
                                 : input;
    const std::optional<std::string> target = stored_current_x_value_register(op);
    if (!target.has_value())
      return {};
    output = erase_register(std::move(output), *target);
    output.slots.at(0).insert(*target);
    return output;
  }
  case IrKind::Recall:
    return lift_state(input, RegisterValueSet{op.register_name});
  case IrKind::IndirectRecall: {
    const EntryStackState output = erase_mutated_indirect_selector(input, op.register_name);
    if (const std::optional<std::string> target = known_indirect_memory_target(op))
      return lift_state(output, RegisterValueSet{*target});
    return lift_state(output, RegisterValueSet{});
  }
  case IrKind::Plain:
    return transfer_plain_state(input, op);
  case IrKind::Stop:
  case IrKind::Return:
  case IrKind::Call:
  case IrKind::IndirectCall:
    return {};
  case IrKind::Loop:
    return erase_register(input, loop_counter_register(op.counter));
  case IrKind::IndirectJump:
    return erase_mutated_indirect_selector(input, op.register_name);
  case IrKind::IndirectCondJump:
    return edge == CfgEdgeKind::Jump ? erase_mutated_indirect_selector(input, op.register_name)
                                     : input;
  }
  return {};
}

std::vector<std::optional<EntryStackState>>
compute_entry_stack_states(const std::vector<IrOp>& ops,
                           const std::vector<std::vector<CfgEdge>>& graph,
                           const EntryStackSeeds& seeds) {
  std::vector<std::optional<EntryStackState>> in_states(ops.size());
  std::deque<std::size_t> pending;
  std::vector<bool> queued(ops.size(), false);
  const auto enqueue = [&](std::size_t index) {
    if (!queued.at(index)) {
      queued.at(index) = true;
      pending.push_back(index);
    }
  };
  if (!ops.empty()) {
    in_states.at(0) = EntryStackState{};
    enqueue(0);
  }

  while (!pending.empty()) {
    const std::size_t index = pending.front();
    pending.pop_front();
    queued.at(index) = false;

    const std::optional<EntryStackState>& input = in_states.at(index);
    if (!input.has_value())
      continue;

    const auto seed = seeds.find(index);
    const bool seeded = seed != seeds.end();
    const EntryStackState output =
        seeded ? seed->second : transfer_state(*input, ops.at(index), CfgEdgeKind::Normal);

    for (const CfgEdge& edge : graph.at(index)) {
      const EntryStackState edge_output =
          seeded || edge.kind == CfgEdgeKind::Normal
              ? output
              : transfer_state(*input, ops.at(index), edge.kind);
      const std::size_t target = static_cast<std::size_t>(edge.target);
      const EntryStackState joined = join_states(in_states.at(target), edge_output);
      if (!same_state_value(joined, in_states.at(target))) {
        in_states.at(target) = joined;
        enqueue(target);
      }
    }
  }

  return in_states;
}

bool has_unknown_indirect_flow(const std::vector<IrOp>& ops) {
  return std::any_of(ops.begin(), ops.end(), [](const IrOp& op) {
    if (op.kind != IrKind::IndirectJump && op.kind != IrKind::IndirectCall &&
        op.kind != IrKind::IndirectCondJump) {
      return false;
    }
    return !op.meta.indirect_flow_targets.has_value() ||
           op.meta.indirect_flow_targets->empty();
  });
}

bool call_entry_flow_is_modeled(const std::vector<IrOp>& ops, const IrOp& op,
                                const DirectReturnAnalysisContext& return_context) {
  if (op.kind == IrKind::Call)
    return direct_callee_entry_stack_flow_is_modeled(ops, op, return_context);
  if (op.kind != IrKind::IndirectCall || !op.meta.indirect_flow_targets.has_value() ||
      op.meta.indirect_flow_targets->empty()) {
    return false;
  }
  // The CFG builder resolves every typed target independently. The call
  // instruction itself preserves X/Y/Z/T, so the same pre-call fact is valid
  // at every admitted callee entry; joins inside each callee still intersect
  // facts from all of its callers.
  return std::all_of(op.meta.indirect_flow_targets->begin(),
                     op.meta.indirect_flow_targets->end(), [](const IrTarget& target) {
                       return std::holds_alternative<std::string>(target) ||
                              std::holds_alternative<int>(target);
                     });
}

struct IrLabelAddressIndex {
  std::map<std::string, int> label_addresses;
};

int ir_op_cells(const IrOp& op) {
  if (op.kind == IrKind::Label)
    return 0;
  if (op.kind == IrKind::Jump || op.kind == IrKind::CondJump ||
      op.kind == IrKind::Call || op.kind == IrKind::Loop) {
    return 2;
  }
  return 1;
}

IrLabelAddressIndex index_ir_labels(const std::vector<IrOp>& ops) {
  IrLabelAddressIndex result;
  int address = 0;
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Label)
      result.label_addresses.emplace(op.name, address);
    address += ir_op_cells(op);
  }
  return result;
}

bool target_matches_label(const IrTarget& target, const std::string& label,
                          const IrLabelAddressIndex& labels) {
  if (const auto* named = std::get_if<std::string>(&target))
    return *named == label;
  const auto expected = labels.label_addresses.find(label);
  return expected != labels.label_addresses.end() &&
         std::get<int>(target) == expected->second;
}

bool call_targets_helper(const IrOp& op, const std::string& helper_label,
                         const IrLabelAddressIndex& labels) {
  if (op.kind == IrKind::Call)
    return target_matches_label(op.target, helper_label, labels);
  if (op.kind != IrKind::IndirectCall || !op.meta.indirect_flow_targets.has_value() ||
      op.meta.indirect_flow_targets->size() != 1U)
    return false;
  return target_matches_label(op.meta.indirect_flow_targets->front(), helper_label, labels);
}

std::map<std::string, std::pair<std::size_t, std::string>> helper_entry_recalls(
    const std::vector<IrOp>& ops) {
  std::map<std::string, std::pair<std::size_t, std::string>> result;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (ops.at(index).kind != IrKind::Label)
      continue;
    const std::optional<int> entry = next_executable_index(ops, static_cast<int>(index + 1U));
    if (!entry.has_value())
      continue;
    const std::optional<std::string> recalled =
        removable_recall_value_register(ops.at(static_cast<std::size_t>(*entry)));
    if (recalled.has_value())
      result.emplace(ops.at(index).name,
                     std::pair{static_cast<std::size_t>(*entry), *recalled});
  }
  return result;
}

struct MaterializationPairSwap {
  std::size_t first = 0;
  std::size_t second = 0;
};

std::optional<MaterializationPairSwap>
materialization_pair_swap_before_call(const std::vector<IrOp>& ops, std::size_t call_index,
                                      const std::string& required_x_register) {
  if (call_index < 4U)
    return std::nullopt;
  const std::size_t first = call_index - 4U;
  const std::size_t second = call_index - 2U;
  const IrOp& recall_first = ops.at(first);
  const IrOp& store_first = ops.at(first + 1U);
  const IrOp& recall_second = ops.at(second);
  const IrOp& store_second = ops.at(second + 1U);
  if (has_rewrite_barrier(recall_first) || has_rewrite_barrier(store_first) ||
      has_rewrite_barrier(recall_second) || has_rewrite_barrier(store_second)) {
    return std::nullopt;
  }
  const std::optional<std::string> source_first =
      removable_recall_value_register(recall_first);
  const std::optional<std::string> target_first =
      stored_current_x_value_register(store_first);
  const std::optional<std::string> source_second =
      removable_recall_value_register(recall_second);
  const std::optional<std::string> target_second =
      stored_current_x_value_register(store_second);
  if (!source_first.has_value() || !target_first.has_value() ||
      !source_second.has_value() || !target_second.has_value() ||
      recall_first.kind != IrKind::Recall || store_first.kind != IrKind::Store ||
      recall_second.kind != IrKind::Recall || store_second.kind != IrKind::Store ||
      *target_first != required_x_register || *target_second == required_x_register) {
    return std::nullopt;
  }

  // Both pairs must be independent memory copies. Besides preserving the
  // final register file, these exclusions make the composed stack theorem
  // exact: original `(A->D; B->E; call; recall D)` and reordered
  // `(B->E; A->D; call)` enter the helper with equal X/Y/X2 and can differ
  // only below Y. entry-stack-input-reuse proves convergence from the weaker
  // state produced by deleting `recall D` without this reordering.
  if (*target_first == *target_second || *target_first == *source_second ||
      *target_second == *source_first) {
    return std::nullopt;
  }
  return MaterializationPairSwap{.first = first, .second = second};
}

bool helper_entry_recall_was_removed(const std::vector<IrOp>& ops,
                                     const std::string& helper_label,
                                     const std::string& register_name) {
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (ops.at(index).kind != IrKind::Label || ops.at(index).name != helper_label)
      continue;
    const std::optional<int> entry = next_executable_index(ops, static_cast<int>(index + 1U));
    if (!entry.has_value())
      return false;
    return removable_recall_value_register(ops.at(static_cast<std::size_t>(*entry))) !=
           std::optional<std::string>{register_name};
  }
  return false;
}

struct MaterializationOrderCandidate {
  std::vector<IrOp> ops;
  int reordered = 0;
};

MaterializationOrderCandidate reorder_materialization_pairs(
    const std::vector<IrOp>& ops, const std::string& helper_label,
    const std::string& required_x_register) {
  MaterializationOrderCandidate result{.ops = ops};
  const IrLabelAddressIndex labels = index_ir_labels(ops);
  for (std::size_t call_index = 0; call_index < ops.size(); ++call_index) {
    if (!call_targets_helper(ops.at(call_index), helper_label, labels) ||
        has_rewrite_barrier(ops.at(call_index))) {
      continue;
    }
    const std::optional<MaterializationPairSwap> swap =
        materialization_pair_swap_before_call(ops, call_index, required_x_register);
    if (!swap.has_value())
      continue;
    const IrOp first_recall = result.ops.at(swap->first);
    const IrOp first_store = result.ops.at(swap->first + 1U);
    result.ops.at(swap->first) = result.ops.at(swap->second);
    result.ops.at(swap->first + 1U) = result.ops.at(swap->second + 1U);
    result.ops.at(swap->second) = first_recall;
    result.ops.at(swap->second + 1U) = first_store;
    ++result.reordered;
  }
  return result;
}

MaterializationOrderCandidate reorder_materialization_pairs_for_tail_plan(
    const std::vector<IrOp>& ops, const std::string& helper_label,
    const std::string& required_x_register,
    const HelperInvariantRecallHoistProof& tail_plan) {
  MaterializationOrderCandidate result{.ops = ops};
  if (tail_plan.insertion != HelperInvariantRecallInsertion::BeforeReturn ||
      tail_plan.calls.empty())
    return result;

  const IrLabelAddressIndex labels = index_ir_labels(ops);
  std::vector<std::size_t> ir_calls;
  for (std::size_t index = 0; index < ops.size(); ++index)
    if (call_targets_helper(ops.at(index), helper_label, labels))
      ir_calls.push_back(index);
  if (ir_calls.size() != tail_plan.calls.size())
    return result;

  const auto previous_executable = [&](std::size_t before) -> std::optional<std::size_t> {
    while (before > 0) {
      --before;
      if (ops.at(before).kind != IrKind::Label)
        return before;
    }
    return std::nullopt;
  };

  for (std::size_t call_number = 0; call_number < ir_calls.size(); ++call_number) {
    std::size_t endpoint = ir_calls.at(call_number);
    const HelperInvariantRecallCall& call = tail_plan.calls.at(call_number);
    if (call.placement ==
        HelperInvariantRecallPlacement::BeforeCallBeforeCommutative) {
      const std::optional<std::size_t> common_recall = previous_executable(endpoint);
      if (!common_recall.has_value() ||
          ops.at(*common_recall).opcode != tail_plan.recall_opcode)
        return MaterializationOrderCandidate{.ops = ops};
      endpoint = *common_recall;
    } else if (call.placement !=
               HelperInvariantRecallPlacement::AfterReturnBeforeCommutative) {
      return MaterializationOrderCandidate{.ops = ops};
    }

    const std::optional<MaterializationPairSwap> swap =
        materialization_pair_swap_before_call(result.ops, endpoint,
                                              required_x_register);
    if (!swap.has_value())
      continue;
    const IrOp first_recall = result.ops.at(swap->first);
    const IrOp first_store = result.ops.at(swap->first + 1U);
    result.ops.at(swap->first) = result.ops.at(swap->second);
    result.ops.at(swap->first + 1U) = result.ops.at(swap->second + 1U);
    result.ops.at(swap->second) = first_recall;
    result.ops.at(swap->second + 1U) = first_store;
    ++result.reordered;
  }
  return result;
}

PassResult reorder_materializations_for_entry(
    const std::vector<IrOp>& ops, const PassContext& context,
    const std::optional<std::string>& only_helper = std::nullopt) {
  PassResult unchanged{.ops = ops};
  const auto helpers = helper_entry_recalls(ops);
  for (const auto& [helper_label, entry] : helpers) {
    if (only_helper.has_value() && helper_label != *only_helper)
      continue;
    const std::string& required_x_register = entry.second;
    MaterializationOrderCandidate ordered =
        reorder_materialization_pairs(ops, helper_label, required_x_register);
    if (ordered.reordered == 0)
      continue;

    PassResult reused = entry_stack_input_reuse(ordered.ops, context);
    if (reused.applied <= 0 || reused.ops.size() >= ops.size() ||
        !helper_entry_recall_was_removed(reused.ops, helper_label,
                                         required_x_register)) {
      continue;
    }
    reused.applied += ordered.reordered;
    reused.optimizations.insert(
        reused.optimizations.begin(),
        AppliedOptimization{
            .name = "call-entry-materialization-order",
            .detail = "Reordered " + std::to_string(ordered.reordered) +
                      " independent recall/store materialization pair" +
                      (ordered.reordered == 1 ? "" : "s") + " before helper " +
                      helper_label +
                      " so its required value arrived in X; the existing bounded "
                      "entry-stack recall proof then removed the redundant helper recall.",
        });
    return reused;
  }
  return unchanged;
}

std::optional<HelperInvariantRecallHoistOptions>
early_helper_hoist_options(const std::vector<MachineItem>& items) {
  std::map<std::string, int> label_addresses;
  int address = 0;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label) {
      label_addresses.emplace(item.name, address);
    } else {
      ++address;
    }
  }

  HelperInvariantRecallHoistOptions options;
  options.prefer_before_return_plan = true;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.indirect_flow_targets.has_value()) {
      if (item.indirect_flow_targets->empty())
        return std::nullopt;
      std::vector<int> resolved;
      bool has_fixed_numeric_target = false;
      for (const IrTarget& target : *item.indirect_flow_targets) {
        if (const auto* numeric = std::get_if<int>(&target)) {
          resolved.push_back(*numeric);
          has_fixed_numeric_target = true;
          continue;
        }
        const auto* label = std::get_if<std::string>(&target);
        if (label == nullptr)
          return std::nullopt;
        const auto found = label_addresses.find(*label);
        if (found == label_addresses.end())
          return std::nullopt;
        resolved.push_back(found->second);
      }
      options.proved_indirect_flow_targets.emplace(item_index, resolved);
      if (has_fixed_numeric_target)
        options.fixed_indirect_flow_targets.emplace(item_index, std::move(resolved));
    }
    if (item.kind == MachineItemKind::Address) {
      if (const auto* numeric = std::get_if<int>(&item.target))
        options.fixed_direct_address_targets.emplace(item_index, *numeric);
    }
  }
  return options;
}

std::optional<std::set<std::size_t>> prove_tail_entry_x_at_every_call(
    const std::vector<IrOp>& ops, const std::string& helper_label,
    const std::string& required_x_register,
    const HelperInvariantRecallHoistProof& preliminary) {
  if (preliminary.insertion != HelperInvariantRecallInsertion::BeforeReturn ||
      preliminary.calls.empty()) {
    return std::nullopt;
  }
  std::vector<std::size_t> ir_calls;
  const IrLabelAddressIndex labels = index_ir_labels(ops);
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (call_targets_helper(ops.at(index), helper_label, labels))
      ir_calls.push_back(index);
  }
  if (ir_calls.size() != preliminary.calls.size())
    return std::nullopt;

  const std::vector<std::vector<CfgEdge>> graph = build_cfg_edges(ops);
  const std::vector<std::optional<EntryStackState>> states =
      compute_entry_stack_states(ops, graph, {});
  const auto previous_executable = [&](std::size_t before) -> std::optional<std::size_t> {
    while (before > 0) {
      --before;
      if (ops.at(before).kind != IrKind::Label)
        return before;
    }
    return std::nullopt;
  };
  std::set<std::size_t> proved_machine_calls;
  for (std::size_t call_number = 0; call_number < ir_calls.size(); ++call_number) {
    const std::size_t ir_call = ir_calls.at(call_number);
    const HelperInvariantRecallCall& machine_call = preliminary.calls.at(call_number);
    const std::optional<EntryStackState>& state = states.at(ir_call);
    if (!state.has_value())
      return std::nullopt;
    std::size_t required_slot = 0;
    std::optional<std::size_t> local_store;
    if (machine_call.placement ==
        HelperInvariantRecallPlacement::BeforeCallBeforeCommutative) {
      const std::optional<std::size_t> common_recall = previous_executable(ir_call);
      if (!common_recall.has_value() ||
          ops.at(*common_recall).opcode != preliminary.recall_opcode)
        return std::nullopt;
      required_slot = 1;
      local_store = previous_executable(*common_recall);
    } else if (machine_call.placement ==
               HelperInvariantRecallPlacement::AfterReturnBeforeCommutative) {
      const std::optional<int> common_recall =
          next_executable_index(ops, static_cast<int>(ir_call + 1U));
      if (!common_recall.has_value() ||
          ops.at(static_cast<std::size_t>(*common_recall)).opcode !=
              preliminary.recall_opcode)
        return std::nullopt;
      required_slot = 0;
      local_store = previous_executable(ir_call);
    } else {
      return std::nullopt;
    }
    bool x_proved = state->slots.at(required_slot).contains(required_x_register);
    if (!x_proved && local_store.has_value()) {
      const IrOp& store = ops.at(*local_store);
      x_proved = store.kind == IrKind::Store && !has_rewrite_barrier(store) &&
                 stored_current_x_value_register(store) ==
                     std::optional<std::string>{required_x_register};
    }
    if (!x_proved)
      return std::nullopt;
    proved_machine_calls.insert(machine_call.call_item_index);
  }
  return proved_machine_calls;
}

} // namespace

PassResult early_helper_invariant_recall_hoist(const std::vector<IrOp>& ops,
                                                const PassContext& context) {
  PassResult unchanged{.ops = ops};
  if (ops.empty() || has_unknown_indirect_flow(ops))
    return unchanged;
  const std::vector<MachineItem> machine = lower_ir_to_machine(ops);
  const std::optional<HelperInvariantRecallHoistOptions> options =
      early_helper_hoist_options(machine);
  if (!options.has_value())
    return unchanged;
  HelperInvariantRecallHoistOptions root_options = *options;
  root_options.allow_before_call_commutative_tail = false;
  root_options.prefer_before_return_plan = false;
  const HelperInvariantRecallHoistResult hoisted =
      optimize_helper_invariant_recall_hoist(machine, root_options);
  if (hoisted.applied <= 0)
    return unchanged;
  std::vector<IrOp> candidate = raise_machine_to_ir(
      hoisted.items, effective_optimizer_feature_profile(context.options));
  if (candidate.size() >= ops.size())
    return unchanged;
  return PassResult{.ops = std::move(candidate),
                    .applied = hoisted.applied,
                    .optimizations = hoisted.optimizations};
}

IrPass early_helper_invariant_recall_hoist_pass() {
  return IrPass{
      .name = "early-helper-invariant-recall-hoist",
      .run = early_helper_invariant_recall_hoist,
      .layout_safe = false,
  };
}

PassResult call_entry_materialization_order(const std::vector<IrOp>& ops,
                                            const PassContext& context) {
  PassResult unchanged{.ops = ops};
  if (ops.empty() || has_unknown_indirect_flow(ops))
    return unchanged;

  PassResult direct = reorder_materializations_for_entry(ops, context);
  if (direct.applied > 0)
    return direct;

  // A common call-site recall may currently obscure the value already in X.
  // Explore the existing proved helper-tail hoist as an internal candidate,
  // then compose it with the same independent-copy reorder and entry-recall
  // proof above. The whole transaction is retained only when it beats the
  // original IR; a locally smaller root-hoist remains available to the normal
  // post-layout optimizer when this composition does not win.
  const auto helpers = helper_entry_recalls(ops);
  for (const auto& [helper_label, entry] : helpers) {
    const std::string& required_x_register = entry.second;
    MaterializationOrderCandidate ordered =
        reorder_materialization_pairs(ops, helper_label, required_x_register);
    if (ordered.reordered == 0)
      continue;
    const std::vector<MachineItem> machine = lower_ir_to_machine(ordered.ops);
    const std::optional<HelperInvariantRecallHoistOptions> hoist_options =
        early_helper_hoist_options(machine);
    if (!hoist_options.has_value())
      continue;
    HelperInvariantRecallHoistOptions composed_options = *hoist_options;
    const HelperInvariantRecallHoistProof preliminary =
        verify_helper_invariant_recall_hoist(machine, helper_label, composed_options);
    const std::optional<std::set<std::size_t>> entry_x_calls =
        prove_tail_entry_x_at_every_call(ordered.ops, helper_label,
                                         required_x_register, preliminary);
    if (!entry_x_calls.has_value())
      continue;
    composed_options.simultaneous_entry_recall_opcode =
        ordered.ops.at(entry.first).opcode;
    composed_options.entry_x_proved_call_items = *entry_x_calls;
    const HelperInvariantRecallHoistResult hoisted =
        rewrite_helper_invariant_recall_hoist(machine, helper_label, composed_options);
    if (hoisted.applied <= 0 ||
        hoisted.proof.insertion != HelperInvariantRecallInsertion::BeforeReturn)
      continue;

    const std::vector<IrOp> tail_ops = raise_machine_to_ir(
        hoisted.items, effective_optimizer_feature_profile(context.options));
    PassResult composed{.ops = tail_ops};
    if (composed.ops.size() >= ops.size() ||
        !helper_entry_recall_was_removed(composed.ops, helper_label,
                                         required_x_register)) {
      continue;
    }

    composed.applied = ordered.reordered + hoisted.applied;
    composed.optimizations.insert(composed.optimizations.begin(),
                                  hoisted.optimizations.begin(),
                                  hoisted.optimizations.end());
    composed.optimizations.insert(
        composed.optimizations.begin(),
        AppliedOptimization{
            .name = "call-entry-materialization-order",
            .detail = "Reordered " + std::to_string(ordered.reordered) +
                      " independent recall/store materialization pair" +
                      (ordered.reordered == 1 ? "" : "s") + " before helper " +
                      helper_label +
                      ", enabling a proved invariant-recall tail hoist and the "
                      "bounded removal of its redundant entry recall.",
        });
    return composed;
  }
  return unchanged;
}

IrPass call_entry_materialization_order_pass() {
  return IrPass{
      .name = "call-entry-materialization-order",
      .run = call_entry_materialization_order,
      .layout_safe = false,
  };
}

HelperInvariantRecallHoistResult post_layout_call_entry_materialization_order(
    const std::vector<MachineItem>& items,
    const HelperInvariantRecallHoistOptions& hoist_options,
    const PassContext& context) {
  HelperInvariantRecallHoistResult unchanged;
  unchanged.items = items;
  if (items.empty())
    return unchanged;
  const auto cell_count = [](const std::vector<MachineItem>& machine) {
    return static_cast<int>(std::count_if(
        machine.begin(), machine.end(),
        [](const MachineItem& item) { return item.kind != MachineItemKind::Label; }));
  };
  const bool trace = std::getenv("MKPRO_TRACE_POST_LAYOUT_ENTRY_X") != nullptr;

  // Feed the already-proved physical targets into the ordinary IR CFG.  This
  // does not retarget code; it only lets the existing entry-stack analysis see
  // the complete late call graph that was unavailable before layout.
  std::vector<MachineItem> annotated = items;
  for (const auto& [item_index, targets] : hoist_options.proved_indirect_flow_targets) {
    if (item_index >= annotated.size() ||
        annotated.at(item_index).kind != MachineItemKind::Op) {
      return unchanged;
    }
    std::vector<IrTarget> typed_targets;
    typed_targets.reserve(targets.size());
    for (const int target : targets)
      typed_targets.emplace_back(target);
    annotated.at(item_index).indirect_flow_targets = std::move(typed_targets);
  }

  const std::vector<IrOp> ops = raise_machine_to_ir(
      annotated, effective_optimizer_feature_profile(context.options));
  if (has_unknown_indirect_flow(ops))
    return unchanged;

  const auto helpers = helper_entry_recalls(ops);
  const IrLabelAddressIndex trace_labels = index_ir_labels(ops);
  for (const auto& [helper_label, entry] : helpers) {
    const std::string& required_x_register = entry.second;
    if (trace) {
      int matching_calls = 0;
      for (std::size_t index = 0; index < ops.size(); ++index)
        if (call_targets_helper(ops.at(index), helper_label, trace_labels))
          ++matching_calls;
      if (matching_calls >= 2) {
        std::cerr << "late-entry-x shape helper=" << helper_label << " reg="
                  << required_x_register << " calls=" << matching_calls << '\n';
        for (std::size_t index = 0; index < ops.size(); ++index) {
          if (!call_targets_helper(ops.at(index), helper_label, trace_labels))
            continue;
          std::cerr << "  call-ir=" << index << " prefix=";
          const std::size_t begin = index > 6U ? index - 6U : 0U;
          for (std::size_t item = begin; item < index; ++item) {
            if (ops.at(item).kind == IrKind::Label)
              continue;
            std::cerr << std::hex << ops.at(item).opcode << std::dec << ',';
          }
          std::cerr << '\n';
        }
      }
    }
    const std::vector<MachineItem> original_machine = lower_ir_to_machine(ops);
    if (original_machine.size() != annotated.size())
      continue;
    HelperInvariantRecallHoistOptions tail_options = hoist_options;
    tail_options.prefer_before_return_plan = true;
    const HelperInvariantRecallHoistProof original_tail_plan =
        verify_helper_invariant_recall_hoist(original_machine, helper_label,
                                             tail_options);
    if (trace && original_tail_plan.calls.size() >= 2U)
      std::cerr << "  tail-plan helper=" << helper_label
                << " proved=" << original_tail_plan.proved
                << " insertion=" << static_cast<int>(original_tail_plan.insertion)
                << " reasons=" << (original_tail_plan.reasons.empty()
                                      ? "<none>"
                                      : original_tail_plan.reasons.front())
                << '\n';
    MaterializationOrderCandidate ordered =
        reorder_materialization_pairs_for_tail_plan(
            ops, helper_label, required_x_register, original_tail_plan);
    if (trace && ordered.reordered > 0)
      std::cerr << "late-entry-x helper=" << helper_label << " reg="
                << required_x_register << " reordered=" << ordered.reordered << '\n';
    if (ordered.reordered == 0)
      continue;

    const std::vector<MachineItem> machine = lower_ir_to_machine(ordered.ops);
    if (machine.size() != annotated.size())
      continue;

    HelperInvariantRecallHoistOptions composed_options = hoist_options;
    composed_options.prefer_before_return_plan = true;
    const HelperInvariantRecallHoistProof preliminary =
        verify_helper_invariant_recall_hoist(machine, helper_label, composed_options);
    if (trace)
      std::cerr << "  preliminary proved=" << preliminary.proved
                << " insertion=" << static_cast<int>(preliminary.insertion)
                << " calls=" << preliminary.calls.size()
                << " reasons=" << (preliminary.reasons.empty() ? "<none>"
                                                               : preliminary.reasons.front())
                << '\n';
    const std::optional<std::set<std::size_t>> entry_x_calls =
        prove_tail_entry_x_at_every_call(ordered.ops, helper_label,
                                         required_x_register, preliminary);
    if (trace)
      std::cerr << "  entry-x=" << entry_x_calls.has_value() << '\n';
    if (!entry_x_calls.has_value())
      continue;

    composed_options.simultaneous_entry_recall_opcode = ordered.ops.at(entry.first).opcode;
    composed_options.entry_x_proved_call_items = *entry_x_calls;
    HelperInvariantRecallHoistResult hoisted =
        rewrite_helper_invariant_recall_hoist(machine, helper_label, composed_options);
    if (trace)
      std::cerr << "  final applied=" << hoisted.applied
                << " proved=" << hoisted.proof.final_artifact_proved
                << " cells=" << cell_count(hoisted.items)
                << " reasons=" << (hoisted.proof.reasons.empty()
                                        ? "<none>"
                                        : hoisted.proof.reasons.front())
                << '\n';
    if (hoisted.applied <= 0 || !hoisted.proof.final_artifact_proved ||
        hoisted.proof.insertion != HelperInvariantRecallInsertion::BeforeReturn ||
        cell_count(hoisted.items) >= cell_count(items)) {
      continue;
    }

    hoisted.applied += ordered.reordered;
    hoisted.optimizations.insert(
        hoisted.optimizations.begin(),
        AppliedOptimization{
            .name = "call-entry-materialization-order",
            .detail = "Reordered " + std::to_string(ordered.reordered) +
                      " independent recall/store materialization pair" +
                      (ordered.reordered == 1 ? "" : "s") + " before helper " +
                      helper_label +
                      " after layout; the complete indirect-flow and bounded "
                      "entry-stack proofs then removed its redundant entry recall.",
        });
    return hoisted;
  }
  return unchanged;
}

PassResult entry_stack_input_reuse(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;
  return run_recall_removal_pass(
      ops,
      RecallRemovalReport{
          .name = "entry-stack-input-reuse",
          .detail =
              [](int count) {
                return "Dropped " + std::to_string(count) + " recall" + (count == 1 ? "" : "s") +
                       " whose register value already rides the caller's entry stack into the "
                       "proved-transparent callee.";
              },
      },
      [&](RecallRemovalEngine& engine) {
        if (ops.empty())
          return;
        if (has_unknown_indirect_flow(ops))
          return;

        const std::optional<NumericFlowTargetLayoutGuard> numeric_targets =
            numeric_flow_target_layout_guard(ops);
        if (!numeric_targets.has_value())
          return;

        const std::vector<std::vector<CfgEdge>> graph = build_cfg_edges(ops);
        const std::vector<std::optional<EntryStackState>> intra_states =
            compute_entry_stack_states(ops, graph, {});

        // Single-level call seeding: a direct call or a fully typed indirect
        // call carries the caller's pre-call X/Y/Z/T facts (computed with
        // every call edge cleared) into every admitted callee entry. Calls do
        // not mutate the arithmetic stack. Multi-caller and multi-target
        // safety is automatic because every predecessor joins by intersection.
        const DirectReturnAnalysisContext return_context = direct_return_analysis_context(ops);
        EntryStackSeeds seeds;
        for (std::size_t index = 0; index < ops.size(); ++index) {
          const IrOp& op = ops.at(index);
          if ((op.kind != IrKind::Call && op.kind != IrKind::IndirectCall) ||
              has_rewrite_barrier(op))
            continue;
          const std::optional<EntryStackState>& pre_call = intra_states.at(index);
          if (!pre_call.has_value())
            continue;
          if (std::all_of(pre_call->slots.begin(), pre_call->slots.end(),
                          [](const RegisterValueSet& slot) { return slot.empty(); }))
            continue;
          if (!call_entry_flow_is_modeled(ops, op, return_context))
            continue;
          seeds.emplace(index, *pre_call);
        }

        const std::vector<std::optional<EntryStackState>> in_states =
            seeds.empty() ? intra_states : compute_entry_stack_states(ops, graph, seeds);

        for (std::size_t index = 0; index < ops.size(); ++index) {
          const std::optional<std::string> recall_register =
              removable_recall_value_register(ops.at(index));
          if (!recall_register.has_value())
            continue;
          if (!numeric_targets->can_delete_at(static_cast<int>(index)))
            continue;
          const std::optional<EntryStackState>& state = in_states.at(index);
          if (!state.has_value() || !state->slots.at(0).contains(*recall_register))
            continue;

          // Zero-materialization gate: the value already reaches the recall
          // point in X, so the only remaining question is whether the recall's
          // stack-lift or X2 side effect is load-bearing. Rely on the engine's
          // verdict and never insert replacement ops.
          RecallRemovalPlanOverrides overrides;
          overrides.require_value_proof = false;
          const std::optional<RecallRemovalStackSchedulerPlan> removal_plan =
              engine.plan(static_cast<int>(index), overrides);
          if (!removal_plan.has_value() || !removal_plan->removable)
            continue;
          engine.removed().insert(static_cast<int>(index));
        }
      });
}

IrPass entry_stack_input_reuse_pass() {
  return IrPass{
      .name = "entry-stack-input-reuse",
      .run = entry_stack_input_reuse,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
