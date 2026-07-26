#include "mkpro/core/passes/entry_stack_input_reuse.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/passes/cfg.hpp"
#include "mkpro/core/passes/recall_removal.hpp"

#include <algorithm>
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
    return (op.kind == IrKind::IndirectJump || op.kind == IrKind::IndirectCall ||
            op.kind == IrKind::IndirectCondJump) &&
           !known_indirect_flow_target(op).has_value();
  });
}

} // namespace

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

        // Single-level call seeding: a proved-transparent direct call carries
        // the caller's pre-call Y/Z/T facts (computed with every call edge
        // cleared) into its callee entry as a fixed seed. Multi-caller safety
        // is automatic because every other predecessor edge joins at the
        // callee label by intersection.
        const DirectReturnAnalysisContext return_context = direct_return_analysis_context(ops);
        EntryStackSeeds seeds;
        for (std::size_t index = 0; index < ops.size(); ++index) {
          const IrOp& op = ops.at(index);
          if (op.kind != IrKind::Call || has_rewrite_barrier(op))
            continue;
          const std::optional<EntryStackState>& pre_call = intra_states.at(index);
          if (!pre_call.has_value())
            continue;
          if (pre_call->slots.at(1).empty() && pre_call->slots.at(2).empty() &&
              pre_call->slots.at(3).empty())
            continue;
          if (!direct_callee_entry_stack_flow_is_modeled(ops, op, return_context))
            continue;
          EntryStackState seed;
          seed.slots = {RegisterValueSet{}, pre_call->slots.at(1), pre_call->slots.at(2),
                        pre_call->slots.at(3)};
          seeds.emplace(index, std::move(seed));
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
