#include "mkpro/core/passes/recall_removal.hpp"
#include "mkpro/core/passes/cfg.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>

#include <utility>
#include <vector>

namespace mkpro::core::passes {

namespace {

// Removing an indirect recall also removes selector writeback. Equality of
// its loaded X value is not enough. Follow all exact CFG continuations until
// the selector is overwritten, rejecting any intervening read or alias.
bool indirect_recall_writeback_is_dead(const std::vector<IrOp>& ops, int recall_index) {
  if (recall_index < 0 || recall_index >= static_cast<int>(ops.size()))
    return false;
  const IrOp& recall = ops.at(static_cast<std::size_t>(recall_index));
  if (recall.kind != IrKind::IndirectRecall)
    return true;
  if (!is_stable_indirect_selector(recall.register_name))
    return false;
  const std::string& selector = recall.register_name;
  const int selector_index = register_index(selector);
  const auto control = build_control_flow_graph(
      ops, {.terminal_stop_fallthrough = false});
  if (!control.targets_are_exact())
    return false;
  std::vector<int> pending;
  for (const auto& edge : control.edges.at(static_cast<std::size_t>(recall_index)))
    pending.push_back(edge.target);
  std::vector<bool> seen(ops.size(), false);
  while (!pending.empty()) {
    const int index = pending.back();
    pending.pop_back();
    if (index < 0 || index >= static_cast<int>(ops.size()))
      return false;
    if (seen.at(static_cast<std::size_t>(index)))
      continue;
    seen.at(static_cast<std::size_t>(index)) = true;
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (has_rewrite_barrier(op) || op.meta.manual_interaction.has_value())
      return false;
    if (op.kind == IrKind::Store && op.register_name == selector)
      continue;
    if ((op.kind == IrKind::Recall && op.register_name == selector) ||
        (op.kind == IrKind::Loop && loop_counter_register(op.counter) == selector))
      return false;
    switch (op.kind) {
    case IrKind::IndirectStore:
    case IrKind::IndirectRecall: {
      if (op.register_name == selector)
        return false;
      const auto target = known_indirect_memory_target(op);
      if (target.has_value() && *target == selector) {
        if (op.kind == IrKind::IndirectStore)
          continue; // Independent selector; this complete store kills the old word.
        return false;
      }
      if (!target.has_value()) {
        if (!op.meta.indirect_memory_targets.has_value() ||
            op.meta.indirect_memory_targets->empty() ||
            std::find(op.meta.indirect_memory_targets->begin(),
                      op.meta.indirect_memory_targets->end(), selector_index) !=
                op.meta.indirect_memory_targets->end())
          return false;
      }
      break;
    }
    case IrKind::IndirectJump:
    case IrKind::IndirectCall:
    case IrKind::IndirectCondJump:
      if (op.register_name == selector)
        return false;
      break;
    default:
      break;
    }
    for (const auto& edge : control.edges.at(static_cast<std::size_t>(index)))
      pending.push_back(edge.target);
  }
  return true;
}

bool recall_starts_label_entry(const std::vector<IrOp>& ops, int recall_index,
                               const DirectReturnAnalysisContext& context) {
  for (int index = recall_index - 1; index >= 0; --index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label)
      return context.label_entries.contains(index);
    if (op.kind != IrKind::OrphanAddress)
      return false;
  }
  return false;
}

} // namespace

RecallRemovalEngine::RecallRemovalEngine(const std::vector<IrOp>& ops) : ops_(ops) {}

const std::vector<IrOp>& RecallRemovalEngine::ops() const {
  return ops_;
}

std::set<int>& RecallRemovalEngine::removed() {
  return removed_;
}

const std::set<int>& RecallRemovalEngine::removed() const {
  return removed_;
}

std::optional<RegisterValueSet> RecallRemovalEngine::x2_register_state(int index) const {
  if (!x2_register_states_.has_value())
    x2_register_states_ = compute_x2_register_states(ops_);
  if (index < 0 || index >= static_cast<int>(x2_register_states_->size()))
    return std::nullopt;
  return x2_register_states_->at(static_cast<std::size_t>(index));
}

std::optional<X2ValueDataflowState> RecallRemovalEngine::x2_value_state(int index) const {
  if (!x2_value_states_.has_value())
    x2_value_states_ =
        compute_x2_value_states(ops_, X2ValueStatesOptions{.track_register_memory = true});
  if (index < 0 || index >= static_cast<int>(x2_value_states_->size()))
    return std::nullopt;
  return x2_value_states_->at(static_cast<std::size_t>(index));
}

const DirectReturnAnalysisContext& RecallRemovalEngine::direct_return_context() const {
  if (!direct_return_context_.has_value())
    direct_return_context_ = direct_return_analysis_context(ops_);
  return *direct_return_context_;
}

std::optional<RecallRemovalStackSchedulerPlan>
RecallRemovalEngine::plan(int recall_index, const RecallRemovalPlanOverrides& overrides) const {
  if (!indirect_recall_writeback_is_dead(ops_, recall_index))
    return std::nullopt;
  const DirectReturnAnalysisContext& context = direct_return_context();
  RecallRemovalStackSchedulerOptions options;
  options.removed_indexes = &removed_;
  options.stack_scheduler_start = overrides.stack_scheduler_start;
  options.stack_exposure_end = overrides.stack_exposure_end;
  options.has_stack_scheduler_state_override = overrides.has_stack_scheduler_state_override;
  options.stack_scheduler_state = overrides.stack_scheduler_state;

  const bool has_state_override = overrides.has_x2_register_state_override ||
                                  overrides.has_x2_value_state_override ||
                                  overrides.has_stack_scheduler_state_override;
  if (!has_state_override && !overrides.require_value_proof) {
    const std::optional<RecallRemovalStackSchedulerPlan> cheap =
        plan_recall_removal_with_stack_scheduler(ops_, recall_index, std::nullopt, std::nullopt,
                                                 context, options);
    if (cheap.has_value() && cheap->removable)
      return cheap;
  }

  const std::optional<RegisterValueSet> x2_state =
      overrides.has_x2_register_state_override ? overrides.x2_register_state
                                               : x2_register_state(recall_index);
  std::optional<X2ValueDataflowState> x2_value =
      overrides.has_x2_value_state_override ? overrides.x2_value_state
                                            : x2_value_state(recall_index);
  if (!overrides.has_x2_value_state_override && recall_starts_label_entry(ops_, recall_index, context))
    x2_value = std::nullopt;
  return plan_recall_removal_with_stack_scheduler(ops_, recall_index, x2_state, x2_value,
                                                  context, options);
}

PassResult run_recall_removal_pass(const std::vector<IrOp>& ops, const RecallRemovalReport& report,
                                   const RecallRemovalCollector& collect) {
  RecallRemovalEngine engine(ops);
  collect(engine);
  if (engine.removed().empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<IrOp> result;
  result.reserve(ops.size() - engine.removed().size());
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (!engine.removed().contains(static_cast<int>(index)))
      result.push_back(ops.at(index));
  }

  const int removed_count = static_cast<int>(engine.removed().size());
  return PassResult{
      .ops = std::move(result),
      .applied = removed_count,
      .optimizations =
          {
              AppliedOptimization{
                  .name = report.name,
                  .detail = report.detail(removed_count),
              },
          },
  };
}

} // namespace mkpro::core::passes
