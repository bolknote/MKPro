#include "mkpro/core/passes/recall_removal.hpp"

#include <utility>
#include <vector>

namespace mkpro::core::passes {

namespace {

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
