#pragma once

#include "mkpro/core/passes/helpers.hpp"

#include <functional>

namespace mkpro::core::passes {

struct RecallRemovalPlanOverrides {
  bool has_x2_register_state_override = false;
  std::optional<RegisterValueSet> x2_register_state;
  bool has_x2_value_state_override = false;
  std::optional<X2ValueDataflowState> x2_value_state;
  bool require_value_proof = false;
  std::optional<int> stack_scheduler_start;
  std::optional<int> stack_exposure_end;
  bool has_stack_scheduler_state_override = false;
  std::optional<X2ValueDataflowState> stack_scheduler_state;
};

struct RecallRemovalReport {
  std::string name;
  std::function<std::string(int)> detail;
};

class RecallRemovalEngine {
public:
  explicit RecallRemovalEngine(const std::vector<IrOp>& ops);

  const std::vector<IrOp>& ops() const;
  std::set<int>& removed();
  const std::set<int>& removed() const;

  std::optional<RegisterValueSet> x2_register_state(int index) const;
  std::optional<X2ValueDataflowState> x2_value_state(int index) const;
  const DirectReturnAnalysisContext& direct_return_context() const;
  std::optional<RecallRemovalStackSchedulerPlan>
  plan(int recall_index, const RecallRemovalPlanOverrides& overrides = {}) const;

private:
  const std::vector<IrOp>& ops_;
  std::set<int> removed_;
  mutable std::optional<std::vector<std::optional<RegisterValueSet>>> x2_register_states_;
  mutable std::optional<std::vector<std::optional<X2ValueDataflowState>>> x2_value_states_;
  mutable std::optional<DirectReturnAnalysisContext> direct_return_context_;
};

using RecallRemovalCollector = std::function<void(RecallRemovalEngine&)>;

PassResult run_recall_removal_pass(const std::vector<IrOp>& ops, const RecallRemovalReport& report,
                                   const RecallRemovalCollector& collect);

} // namespace mkpro::core::passes
