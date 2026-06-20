#include "mkpro/core/passes/flow_x_reuse.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/passes/cfg.hpp"
#include "mkpro/core/passes/recall_removal.hpp"

#include <algorithm>
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
  XRegisterSet output = input;
  if (!mkpro::core::is_stable_indirect_selector(register_name))
    output.erase(register_name);
  return output;
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
  std::vector<std::optional<XRegisterSet>> out_states(ops.size());
  if (!ops.empty())
    in_states.at(0) = XRegisterSet{};

  bool changed = true;
  int iterations = 0;
  while (changed && iterations < 200) {
    changed = false;
    ++iterations;

    for (std::size_t index = 0; index < ops.size(); ++index) {
      const std::optional<XRegisterSet>& input = in_states.at(index);
      if (!input.has_value())
        continue;

      const XRegisterSet output = transfer_x_set(*input, ops.at(index), CfgEdgeKind::Normal);
      if (!same_set_value(output, out_states.at(index))) {
        out_states.at(index) = output;
        changed = true;
      }

      for (const CfgEdge& edge : graph.at(index)) {
        const XRegisterSet edge_output = edge.kind == CfgEdgeKind::Normal
                                             ? output
                                             : transfer_x_set(*input, ops.at(index), edge.kind);
        const XRegisterSet joined =
            join_x_sets(in_states.at(static_cast<std::size_t>(edge.target)), edge_output);
        if (!same_set_value(joined, in_states.at(static_cast<std::size_t>(edge.target)))) {
          in_states.at(static_cast<std::size_t>(edge.target)) = joined;
          changed = true;
        }
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
        if (has_unknown_indirect_flow(ops))
          return;

        const std::optional<NumericFlowTargetLayoutGuard> numeric_targets =
            numeric_flow_target_layout_guard(ops);
        if (!numeric_targets.has_value())
          return;

        const std::vector<std::vector<CfgEdge>> graph = build_cfg_edges(ops);
        const std::vector<std::optional<XRegisterSet>> in_states =
            compute_x_register_states(ops, graph);

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

} // namespace mkpro::core::passes
