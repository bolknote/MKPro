#include "mkpro/core/passes/branch_target_x_reuse.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/passes/cfg.hpp"
#include "mkpro/core/passes/recall_removal.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

bool is_conditional_target_op(const IrOp& op) {
  return op.kind == IrKind::CondJump || op.kind == IrKind::Loop ||
         op.kind == IrKind::IndirectCondJump;
}

std::optional<int> next_executable(const std::vector<IrOp>& ops, int start) {
  return next_executable_index(ops, start);
}

std::optional<int> previous_executable(const std::vector<IrOp>& ops, int start) {
  for (int index = start; index >= 0; --index) {
    if (ops.at(static_cast<std::size_t>(index)).kind != IrKind::Label)
      return index;
  }
  return std::nullopt;
}

bool is_no_fallthrough(const IrOp& op) {
  return op.kind == IrKind::Jump || op.kind == IrKind::IndirectJump ||
         op.kind == IrKind::Return || op.kind == IrKind::Stop;
}

bool has_fallthrough_into_index(const std::vector<IrOp>& ops, int target_index) {
  const std::optional<int> previous = previous_executable(ops, target_index - 1);
  return previous.has_value() && !is_no_fallthrough(ops.at(static_cast<std::size_t>(*previous)));
}

std::optional<int> flow_target_entry_index(const std::vector<IrOp>& ops, const IrOp& op,
                                           const CfgTargetIndexes& indexes) {
  switch (op.kind) {
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Call:
  case IrKind::Loop:
    if (const auto* address = std::get_if<int>(&op.target)) {
      const auto found = indexes.address_index.find(*address);
      return found == indexes.address_index.end() ? std::optional<int>{} : found->second;
    }
    if (const auto* label = std::get_if<std::string>(&op.target)) {
      const auto found = indexes.label_index.find(*label);
      if (found == indexes.label_index.end())
        return std::nullopt;
      return next_executable(ops, found->second + 1);
    }
    return std::nullopt;
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
  case IrKind::IndirectCondJump:
    if (const std::optional<int> target = known_indirect_flow_target(op)) {
      const auto found = indexes.address_index.find(*target);
      return found == indexes.address_index.end() ? std::optional<int>{} : found->second;
    }
    return std::nullopt;
  default:
    return std::nullopt;
  }
}

std::map<int, int> target_reference_counts_by_entry_index(const std::vector<IrOp>& ops,
                                                          const CfgTargetIndexes& indexes) {
  std::map<int, int> result;
  for (const IrOp& op : ops) {
    const std::optional<int> target = flow_target_entry_index(ops, op, indexes);
    if (target.has_value())
      result[*target] += 1;
  }
  return result;
}

std::optional<int> branch_target_entry_index(const std::vector<IrOp>& ops, const IrOp& op,
                                             const CfgTargetIndexes& indexes) {
  if ((op.kind == IrKind::CondJump || op.kind == IrKind::Loop) &&
      std::holds_alternative<std::string>(op.target)) {
    const auto label = indexes.label_index.find(std::get<std::string>(op.target));
    if (label == indexes.label_index.end())
      return std::nullopt;
    return next_executable(ops, label->second + 1);
  }
  if ((op.kind == IrKind::CondJump || op.kind == IrKind::Loop) &&
      std::holds_alternative<int>(op.target)) {
    const auto address = indexes.address_index.find(std::get<int>(op.target));
    return address == indexes.address_index.end() ? std::optional<int>{} : address->second;
  }
  if (op.kind == IrKind::IndirectCondJump) {
    if (const std::optional<int> target = known_indirect_flow_target(op)) {
      const auto address = indexes.address_index.find(*target);
      return address == indexes.address_index.end() ? std::optional<int>{} : address->second;
    }
  }
  return std::nullopt;
}

std::optional<std::string> immediately_held_register(const std::vector<IrOp>& ops,
                                                     std::size_t branch_index) {
  for (int index = static_cast<int>(branch_index) - 1; index >= 0; --index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label)
      continue;
    return removable_recall_value_register(op);
  }
  return std::nullopt;
}

std::optional<std::string> branch_preserved_register(const std::optional<std::string>& held,
                                                     const IrOp& branch,
                                                     const std::string& target_register) {
  if (!held.has_value())
    return std::nullopt;
  if (branch.kind == IrKind::Loop && loop_counter_register(branch.counter) == *held)
    return std::nullopt;
  if (branch.kind == IrKind::IndirectCondJump && branch.register_name == *held &&
      !mkpro::core::is_stable_indirect_selector(branch.register_name)) {
    return std::nullopt;
  }
  if (branch.kind == IrKind::IndirectCondJump && branch.register_name == target_register &&
      !mkpro::core::is_stable_indirect_selector(branch.register_name)) {
    return std::nullopt;
  }
  return held;
}

bool sets_intersect_register_facts(const X2ValueSet& values, const RegisterValueSet& registers) {
  if (registers.empty())
    return false;
  constexpr std::string_view prefix = "reg:";
  for (const X2ValueFact& fact : values) {
    if (fact.starts_with(prefix) && registers.contains(fact.substr(prefix.size())))
      return true;
  }
  return false;
}

std::optional<std::string> transparent_prefix_stored_register(const IrOp& op) {
  switch (op.kind) {
  case IrKind::Store:
    return op.register_name;
  case IrKind::IndirectStore:
    return mkpro::core::is_stable_indirect_selector(op.register_name)
               ? known_indirect_memory_target(op)
               : std::nullopt;
  default:
    return std::nullopt;
  }
}

std::optional<RegisterValueSet> transfer_transparent_store_x2_register_state(
    const std::optional<RegisterValueSet>& x2, const std::optional<X2ValueDataflowState>& value,
    const std::string& register_name) {
  if (!x2.has_value() || !value.has_value())
    return std::nullopt;
  RegisterValueSet output;
  for (const std::string& fact : *x2) {
    if (fact != register_name)
      output.insert(fact);
  }
  if (sets_intersect_register_facts(value->x, *x2))
    output.insert(register_name);
  return output;
}

bool is_transparent_branch_target_prefix_op(const std::vector<IrOp>& ops, const IrOp& op,
                                           const DirectReturnAnalysisContext& context) {
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op))
    return false;
  if (is_known_return_call_op(op))
    return x2_known_return_call_preserves_stack_x_and_x2(ops, op, context);
  if (op.kind == IrKind::Label || op.kind == IrKind::Store || op.kind == IrKind::OrphanAddress)
    return true;
  if (op.kind == IrKind::IndirectStore) {
    return mkpro::core::is_stable_indirect_selector(op.register_name) &&
           known_indirect_memory_target(op).has_value();
  }
  if (op.kind == IrKind::Plain) {
    if (!op.meta.roles.empty() || !plain_preserves_x_value(op))
      return false;
    const X2StackEffectAnalysis effect = analyze_x2_stack_effect(op);
    return effect.x2_preserves && effect.stack_preserves;
  }
  return false;
}

struct BranchTargetRecall {
  int index = 0;
  std::optional<RegisterValueSet> x2_register_state;
  std::optional<X2ValueDataflowState> value_state;
};

std::optional<BranchTargetRecall> branch_target_recall_after_transparent_prefix(
    const std::vector<IrOp>& ops, int target_index, const std::map<int, int>& references,
    std::optional<RegisterValueSet> x2_register_state,
    std::optional<X2ValueDataflowState> target_value_state,
    const DirectReturnAnalysisContext& direct_return_context) {
  std::optional<X2ValueDataflowState> value_state = std::move(target_value_state);
  std::optional<RegisterValueSet> register_state = std::move(x2_register_state);
  X2TransferStateOptions transfer_options;
  transfer_options.track_register_memory = true;

  for (int index = target_index; index < static_cast<int>(ops.size()); ++index) {
    if (index != target_index) {
      const auto reference = references.find(index);
      if (reference != references.end() && reference->second > 0)
        return std::nullopt;
    }
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (removable_recall_value_register(op).has_value())
      return BranchTargetRecall{.index = index,
                                .x2_register_state = register_state,
                                .value_state = value_state};
    if (!is_transparent_branch_target_prefix_op(ops, op, direct_return_context))
      return std::nullopt;

    const std::optional<std::string> stored_register = transparent_prefix_stored_register(op);
    if (stored_register.has_value()) {
      register_state =
          transfer_transparent_store_x2_register_state(register_state, value_state, *stored_register);
    }
    if (is_known_return_call_op(op)) {
      value_state = transfer_x2_value_state_through_known_transparent_return_call(
          ops, op, value_state, direct_return_context, transfer_options, index);
    } else {
      value_state = transfer_x2_value_state_for_edge(value_state, op, X2DataflowEdgeKind::Normal,
                                                     transfer_options, index);
    }
  }
  return std::nullopt;
}

} // namespace

PassResult branch_target_x_reuse(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;

  return run_recall_removal_pass(
      ops,
      RecallRemovalReport{
          .name = "branch-target-x-reuse",
          .detail =
              [](int count) {
                return "Dropped " + std::to_string(count) + " branch-target recall" +
                       (count == 1 ? "" : "s") +
                       " already preserved in X by the branch path.";
              },
      },
      [&](RecallRemovalEngine& engine) {
        const CfgTargetIndexes indexes = build_target_indexes(ops);
        const std::map<int, int> references = target_reference_counts_by_entry_index(ops, indexes);
        const std::optional<NumericFlowTargetLayoutGuard> numeric_targets =
            numeric_flow_target_layout_guard(ops);
        if (!numeric_targets.has_value())
          return;

        X2TransferStateOptions value_options;
        value_options.track_register_memory = true;

        for (std::size_t index = 0; index < ops.size(); ++index) {
          const IrOp& op = ops.at(index);
          if (!is_conditional_target_op(op) || has_rewrite_barrier(op))
            continue;

          const std::optional<int> target_index = branch_target_entry_index(ops, op, indexes);
          if (!target_index.has_value())
            continue;
          const auto reference = references.find(*target_index);
          if (reference == references.end() || reference->second != 1)
            continue;
          if (has_fallthrough_into_index(ops, *target_index))
            continue;

          const std::optional<std::string> held = immediately_held_register(ops, index);
          X2RegisterEdgeState edge_state;
          if (held.has_value())
            edge_state.x = RegisterValueSet{*held};
          edge_state.x2 = engine.x2_register_state(static_cast<int>(index));
          const std::optional<RegisterValueSet> target_register_state =
              transfer_x2_register_state_for_edge(edge_state, op, X2DataflowEdgeKind::Jump);
          const std::optional<X2ValueDataflowState> target_value_state =
              transfer_x2_value_state_for_edge(engine.x2_value_state(static_cast<int>(index)), op,
                                               X2DataflowEdgeKind::Jump, value_options,
                                               static_cast<int>(index));

          const std::optional<BranchTargetRecall> target_recall =
              branch_target_recall_after_transparent_prefix(
                  ops, *target_index, references, target_register_state, target_value_state,
                  engine.direct_return_context());
          if (!target_recall.has_value() ||
              engine.removed().contains(target_recall->index)) {
            continue;
          }
          if (!numeric_targets->can_delete_at(target_recall->index))
            continue;

          const IrOp& target = ops.at(static_cast<std::size_t>(target_recall->index));
          const std::optional<std::string> target_register = removable_recall_value_register(target);
          if (!target_register.has_value())
            continue;
          if (op.kind == IrKind::Loop && loop_counter_register(op.counter) == *target_register)
            continue;

          const std::optional<std::string> preserved =
              branch_preserved_register(held, op, *target_register);

          RecallRemovalPlanOverrides overrides;
          overrides.has_x2_register_state_override = true;
          overrides.x2_register_state = target_recall->x2_register_state.has_value()
                                            ? target_recall->x2_register_state
                                            : engine.x2_register_state(target_recall->index);
          overrides.has_x2_value_state_override = true;
          overrides.x2_value_state = target_recall->value_state.has_value()
                                         ? target_recall->value_state
                                         : engine.x2_value_state(target_recall->index);
          overrides.stack_scheduler_start = static_cast<int>(index);
          overrides.stack_exposure_end = target_recall->index;
          overrides.has_stack_scheduler_state_override = true;
          overrides.stack_scheduler_state = engine.x2_value_state(static_cast<int>(index));

          const std::optional<RecallRemovalStackSchedulerPlan> removal_plan =
              engine.plan(target_recall->index, overrides);
          if (preserved != target_register &&
              !(removal_plan.has_value() && removal_plan->analysis.value_proof.has_value() &&
                removal_plan->analysis.value_proof->in_x)) {
            continue;
          }
          if (!removal_plan.has_value() || !removal_plan->removable)
            continue;

          engine.removed().insert(target_recall->index);
        }
      });
}

IrPass branch_target_x_reuse_pass() {
  return IrPass{
      .name = "branch-target-x-reuse",
      .run = branch_target_x_reuse,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
