#include "mkpro/core/passes/flow_x_reuse.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/passes/cfg.hpp"
#include "mkpro/core/passes/recall_removal.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

namespace {

using XRegisterSet = std::set<std::string>;

bool same_set_value(const XRegisterSet& left, const std::optional<XRegisterSet>& right) {
  return right.has_value() && left == *right;
}

XRegisterSet add_register(const XRegisterSet& input, const std::string& register_name) {
  XRegisterSet output = input;
  output.insert(register_name);
  return output;
}

XRegisterSet remove_register(const XRegisterSet& input, const std::string& register_name) {
  XRegisterSet output = input;
  output.erase(register_name);
  return output;
}

XRegisterSet join_x_sets(const std::optional<XRegisterSet>& current, const XRegisterSet& incoming) {
  if (!current.has_value())
    return incoming;
  XRegisterSet joined;
  for (const std::string& register_name : *current) {
    if (incoming.contains(register_name))
      joined.insert(register_name);
  }
  return joined;
}

XRegisterSet transfer_indirect_flow_x_set(const XRegisterSet& input,
                                          const std::string& register_name) {
  // "Stable" means no counter increment/decrement, not preservation of the
  // complete data word (e.g. 14.375 becomes 14). Without a word-level proof
  // only facts about other registers survive the selector operation.
  return remove_register(input, register_name);
}

XRegisterSet transfer_x_set(const XRegisterSet& input, const IrOp& op, CfgEdgeKind edge) {
  if (has_rewrite_barrier(op))
    return {};

  switch (op.kind) {
  case IrKind::Label:
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::OrphanAddress:
    return input;
  case IrKind::Store:
  case IrKind::IndirectStore:
    if (const std::optional<std::string> target = stored_current_x_value_register(op))
      return add_register(input, *target);
    return {};
  case IrKind::Recall:
    return XRegisterSet{op.register_name};
  case IrKind::IndirectRecall:
    if (const std::optional<std::string> target = known_indirect_memory_target(op))
      return XRegisterSet{*target};
    return {};
  case IrKind::Plain:
    return plain_preserves_x_value(op) ? input : XRegisterSet{};
  case IrKind::Stop:
    return {};
  case IrKind::Loop:
    return remove_register(input, loop_counter_register(op.counter));
  case IrKind::Call:
  case IrKind::Return:
    return input;
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
    return transfer_indirect_flow_x_set(input, op.register_name);
  case IrKind::IndirectCondJump:
    return edge == CfgEdgeKind::Jump ? transfer_indirect_flow_x_set(input, op.register_name)
                                     : input;
  }
  return {};
}

std::vector<std::optional<XRegisterSet>>
compute_x_register_states(const std::vector<IrOp>& ops,
                          const std::vector<std::vector<CfgEdge>>& graph) {
  std::vector<std::optional<XRegisterSet>> in_states(ops.size());
  std::deque<std::size_t> worklist;
  std::vector<bool> queued(ops.size(), false);
  if (!ops.empty())
    in_states.at(0) = XRegisterSet{};
  if (!ops.empty()) {
    worklist.push_back(0);
    queued.at(0) = true;
  }
  // Finite descending sets converge regardless of virtual program length or
  // block order. Never use partially propagated facts after an iteration cap.
  while (!worklist.empty()) {
    const std::size_t index = worklist.front();
    worklist.pop_front();
    queued.at(index) = false;
    const XRegisterSet input = *in_states.at(index);
    for (const CfgEdge& edge : graph.at(index)) {
      const std::size_t target = static_cast<std::size_t>(edge.target);
      const XRegisterSet joined =
          join_x_sets(in_states.at(target), transfer_x_set(input, ops.at(index), edge.kind));
      if (!same_set_value(joined, in_states.at(target))) {
        in_states.at(target) = joined;
        if (!queued.at(target)) {
          queued.at(target) = true;
          worklist.push_back(target);
        }
      }
    }
  }

  return in_states;
}

} // namespace

PassResult flow_x_reuse(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;
  return run_recall_removal_pass(
      ops,
      RecallRemovalReport{
          .name = "flow-x-reuse",
          .detail =
              [](int count) {
                return "Dropped " + std::to_string(count) + " recall" + (count == 1 ? "" : "s") +
                       " whose register value already reaches the point in X on every CFG "
                       "predecessor.";
              },
      },
      [&](RecallRemovalEngine& engine) {
        if (ops.empty())
          return;
        const ControlFlowGraph control = build_control_flow_graph(ops);
        // Typed labels and finite target sets are exact even when no physical
        // byte can encode their current logical positions. Numeric-only
        // convenience queries must not veto an otherwise proved CFG.
        if (!control.targets_are_exact())
          return;

        const std::optional<NumericFlowTargetLayoutGuard> numeric_targets =
            numeric_flow_target_layout_guard(ops);
        if (!numeric_targets.has_value())
          return;

        const std::vector<std::optional<XRegisterSet>> in_states =
            compute_x_register_states(ops, control.edges);

        for (std::size_t index = 0; index < ops.size(); ++index) {
          const std::optional<std::string> recall_register =
              removable_recall_value_register(ops.at(index));
          if (!recall_register.has_value())
            continue;
          if (!numeric_targets->can_delete_at(static_cast<int>(index)))
            continue;

          const bool cfg_already_in_x =
              in_states.at(index).has_value() && in_states.at(index)->contains(*recall_register);
          RecallRemovalPlanOverrides overrides;
          overrides.require_value_proof = !cfg_already_in_x;
          const std::optional<RecallRemovalStackSchedulerPlan> removal_plan =
              engine.plan(static_cast<int>(index), overrides);
          if (!removal_plan.has_value() || !removal_plan->removable)
            continue;

          const bool value_proves_in_x = removal_plan->analysis.value_proof.has_value() &&
                                         removal_plan->analysis.value_proof->in_x;
          if (cfg_already_in_x || value_proves_in_x)
            engine.removed().insert(static_cast<int>(index));
        }
      });
}

IrPass flow_x_reuse_pass() {
  return IrPass{
      .name = "flow-x-reuse",
      .run = flow_x_reuse,
      .layout_safe = false,
  };
}

PassResult stack_lift_recall_forwarding(const std::vector<IrOp>& ops,
                                        const PassContext& context) {
  (void)context;
  const ControlFlowGraph control = build_control_flow_graph(ops);
  if (ops.empty() || !control.targets_are_exact())
    return PassResult{.ops = ops};
  const auto in_states = compute_x_register_states(ops, control.edges);
  RecallRemovalEngine engine(ops);
  std::vector<IrOp> result = ops;
  int forwarded = 0;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& recall = ops[index];
    // Only a plain direct recall is replaced. Raw/manual cells and role-bound
    // opcode/data overlays require a separate byte-identity proof.
    if (recall.kind != IrKind::Recall || has_rewrite_barrier(recall) ||
        !recall.meta.roles.empty() || !in_states[index].has_value() ||
        !in_states[index]->contains(recall.register_name))
      continue;
    const auto plan = engine.plan(static_cast<int>(index));
    if (!plan.has_value() || !plan->analysis.exposes_stack_lift ||
        plan->analysis.exposes_x2_restore)
      continue;

    // X already equals the register on every predecessor. Enter duplicates
    // that same word instead of fetching it, preserving the required stack
    // lift. The shared recall proof establishes that X2 resynchronization is
    // unobserved. This one-cell rewrite cannot move even a fixed numeric
    // target, but can make the register's defining store dead in the next DSE.
    IrOp& replacement = result[index];
    replacement.kind = IrKind::Plain;
    replacement.opcode = 0x0e;
    replacement.register_name.clear();
    replacement.semantic.clear();
    replacement.meta.logical_register_name.reset();
    replacement.meta.mnemonic = "В↑";
    replacement.meta.comment = "current-X forwarding with preserved stack lift";
    replacement.meta.tactic = "stack-lift-recall-forwarding";
    ++forwarded;
  }
  if (forwarded == 0)
    return PassResult{.ops = ops};
  return PassResult{
      .ops = std::move(result),
      .applied = forwarded,
      .optimizations = {{
          .name = "stack-lift-recall-forwarding",
          .detail = "Replaced " + std::to_string(forwarded) +
                    " current-X recall(s) with Enter, preserving stack lifts and "
                    "physical positions while exposing dead register stores.",
      }},
  };
}

IrPass stack_lift_recall_forwarding_pass() {
  return IrPass{
      .name = "stack-lift-recall-forwarding",
      .run = stack_lift_recall_forwarding,
      .layout_safe = true,
  };
}

} // namespace mkpro::core::passes
