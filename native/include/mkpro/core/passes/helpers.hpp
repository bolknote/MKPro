#pragma once

#include "mkpro/core/ir.hpp"
#include "mkpro/core/result.hpp"
#include "mkpro/core/types.hpp"

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace mkpro::core::passes {

struct PassContext {
  const CompileOptions& options;
};

struct AppliedOptimization {
  std::string name;
  std::string detail;
};

struct PassResult {
  std::vector<IrOp> ops;
  int applied = 0;
  std::vector<AppliedOptimization> optimizations;
  std::vector<PreloadReport> preloads;
};

using IrPassFn = PassResult (*)(const std::vector<IrOp>& ops, const PassContext& context);

struct IrPass {
  std::string_view name;
  IrPassFn run = nullptr;
  bool layout_safe = false;
};

struct X2StackEffectAnalysis {
  X2Effect x2_effect = X2Effect::Preserves;
  StackEffect stack_effect = StackEffect::Preserves;
  bool stack_shifts = false;
  bool stack_preserves = true;
  bool stack_consumes = false;
  bool stack_exposes = false;
  bool stack_barrier = false;
  bool x2_affects = false;
  bool x2_preserves = true;
  bool x2_restores = false;
  bool hard_x2_overwrite_without_stack_use = false;
  bool stack_lift_and_x2_sync = false;
};

struct DirectReturnAnalysisContext {
  std::set<int> label_entries;
  std::map<std::string, int> labels;
  std::map<int, int> addresses;
};

using RegisterValueSet = std::set<std::string>;

using X2ValueFact = std::string;
using X2ValueSet = std::set<X2ValueFact>;
using X2ShapeFact = std::string;
using X2ShapeSet = std::set<X2ShapeFact>;
using X2ValueMemory = std::map<std::string, X2ValueSet>;
using X2ShapeMemory = std::map<std::string, X2ShapeSet>;

struct RegisterDataflowState {
  RegisterValueSet x;
  RegisterValueSet y;
  RegisterValueSet x2;
};

struct X2RegisterEdgeState {
  std::optional<RegisterValueSet> x;
  std::optional<RegisterValueSet> y;
  std::optional<RegisterValueSet> x2;
};

struct X2EntryState {
  enum class Kind { Closed, Open, Exponent, Unknown };
  Kind kind = Kind::Closed;
  RegisterValueSet raw;
  RegisterValueSet mantissa;
  RegisterValueSet exponent;
};

struct X2VpContextState {
  enum class Kind { None, Exponent, Unknown };
  Kind kind = Kind::None;
  RegisterValueSet mantissa;
  RegisterValueSet exponent;
};

struct X2StructuralEntryState {
  enum class Kind { None, Exponent, Unknown };
  Kind kind = Kind::None;
  X2ShapeSet mantissa;
  RegisterValueSet exponent;
};

struct X2ValueDataflowState {
  X2ValueSet x;
  std::optional<X2ValueSet> y;
  X2ValueSet x2;
  X2ShapeSet xShape;
  std::optional<X2ShapeSet> yShape;
  X2ShapeSet x2Shape;
  std::optional<X2ShapeSet> xDirectShape;
  std::optional<X2ShapeSet> yDirectShape;
  X2EntryState entry;
  X2VpContextState vpContext;
  X2StructuralEntryState structuralEntry;
  X2StructuralEntryState structuralVpContext;
  std::optional<RegisterValueSet> vpEntryMantissa;
  bool vpEntryMantissaTransient = false;
  std::optional<RegisterValueSet> vpEntrySignMantissa;
  std::optional<X2ShapeSet> vpEntryShape;
  std::optional<X2ShapeSet> vpEntrySignShape;
  bool vpEntryShapeTransient = false;
  std::optional<X2ValueMemory> memory;
  std::optional<X2ShapeMemory> shapeMemory;
};

struct X2ValueStatesOptions {
  bool track_register_memory = false;
};

struct X2TransferStateOptions {
  bool track_register_memory = false;
  bool target_starts_with_vp = false;
};

struct X2RestoreExposureOptions {
  std::optional<std::string> redundant_sync_register;
  bool redundant_sync_value = false;
  bool redundant_sync_display_value = false;
  bool redundant_sync_shape = false;
  bool redundant_sync_vp_shape = false;
  std::optional<int> numeric_target_must_be_before_index;
};

struct RecallValueProof {
  std::string register_name;
  bool in_x = false;
  std::optional<std::string> x2_sync_register;
  bool x2_sync_value = false;
  bool x2_sync_display_value = false;
  bool x2_sync_shape = false;
  bool x2_sync_vp_shape = false;
};

struct RecallRemovalAnalysis {
  std::string register_name;
  std::optional<RecallValueProof> value_proof;
  std::optional<std::string> redundant_sync_register;
  bool redundant_sync_value = false;
  bool redundant_sync_display_value = false;
  bool redundant_sync_shape = false;
  bool x2_sync_redundant = false;
  bool exposes_stack_lift = false;
  bool exposes_x2_restore = false;
  bool removable = false;
};

struct RecallRemovalStackSchedulerOptions {
  const std::set<int>* removed_indexes = nullptr;
  std::optional<int> stack_scheduler_start;
  std::optional<int> stack_exposure_end;
  bool has_stack_scheduler_state_override = false;
  std::optional<X2ValueDataflowState> stack_scheduler_state;
};

struct RecallRemovalStackSchedulerPlan {
  RecallRemovalAnalysis analysis;
  std::optional<int> stack_lift_producer_index;
  bool stack_lift_already_supplied = false;
  bool removable = false;
};

enum class X2DataflowEdgeKind {
  Normal,
  Fallthrough,
  Jump,
};

inline bool has_rewrite_barrier(const IrOp& op) {
  return op.meta.raw;
}

int cells_per_op(const IrOp& op);
std::map<std::string, int> calculate_label_addresses(const std::vector<IrOp>& ops);
std::map<std::string, int> label_indexes(const std::vector<IrOp>& ops);
std::map<int, int> address_indexes(const std::vector<IrOp>& ops);
std::set<int> compute_label_entry_indexes(const std::vector<IrOp>& ops);
DirectReturnAnalysisContext direct_return_analysis_context(const std::vector<IrOp>& ops);
std::optional<int> target_address(const IrTarget& target, const std::map<std::string, int>& labels);
std::optional<std::string> known_indirect_memory_target(const IrOp& op);
std::optional<std::set<std::string>> known_indirect_memory_targets(const IrOp& op);
std::optional<int> known_indirect_flow_target(const IrOp& op);
std::optional<std::string> removable_recall_value_register(const IrOp& op);
std::optional<std::string> stored_current_x_value_register(const IrOp& op);
std::optional<int> next_executable_index(const std::vector<IrOp>& ops, int start);
bool is_display_focus_sensitive(const IrOp& op);
bool is_x2_affecting_op(const IrOp& op);
bool is_x2_restore_op(const IrOp& op);
bool plain_preserves_x_value(const IrOp& op);
X2StackEffectAnalysis analyze_x2_stack_effect(const IrOp* op);
X2StackEffectAnalysis analyze_x2_stack_effect(const IrOp& op);
bool removing_recall_can_expose_stack_lift(const std::vector<IrOp>& ops, int recall_index);
bool removing_stack_lift_can_expose_stack(const std::vector<IrOp>& ops, int lift_index);
bool removing_pre_shift_lift_can_expose_stack(const std::vector<IrOp>& ops, int producer_index);
bool replacing_number_entry_can_expose_stack_lift(
    const std::vector<IrOp>& ops, int number_entry_end_index,
    std::optional<int> numeric_target_must_be_before_index = std::nullopt);
bool x2_sync_can_expose_context_sensitive_restore(const std::vector<IrOp>& ops, int sync_index,
                                                  const X2RestoreExposureOptions& options = {});
bool removing_recall_can_expose_x2_restore(const std::vector<IrOp>& ops, int recall_index,
                                           const X2RestoreExposureOptions& options = {});
std::optional<int> x2_next_stack_shifting_producer_index(
    const std::vector<IrOp>& ops, int start, const DirectReturnAnalysisContext& context);
std::optional<int> x2_next_hard_x2_overwrite_index(
    const std::vector<IrOp>& ops, int start, const DirectReturnAnalysisContext& context);
std::optional<int> x2_next_x_preserving_x2_sync_index(
    const std::vector<IrOp>& ops, int start, const DirectReturnAnalysisContext& context);
std::optional<int> x2_previous_x_preserving_x2_sync_index(
    const std::vector<IrOp>& ops, int end, const DirectReturnAnalysisContext& context);
std::optional<int> x2_previous_hard_x2_overwrite_index(
    const std::vector<IrOp>& ops, int end, const DirectReturnAnalysisContext& context);
std::optional<int> x2_next_stack_preserving_return_x2_sync_index(
    const std::vector<IrOp>& ops, int start, const DirectReturnAnalysisContext& context);
std::optional<int> x2_previous_stack_preserving_return_x2_sync_index(
    const std::vector<IrOp>& ops, int end, const DirectReturnAnalysisContext& context);
std::optional<int> x2_previous_stack_lift_and_x2_sync_producer_index(
    const std::vector<IrOp>& ops, int end, const DirectReturnAnalysisContext& context);
std::optional<RecallRemovalAnalysis>
analyze_recall_removal(const std::vector<IrOp>& ops, int recall_index,
                       const std::optional<RegisterValueSet>& x2_register_state,
                       const std::optional<X2ValueDataflowState>& x2_value_state,
                       const DirectReturnAnalysisContext* context = nullptr);
std::optional<RecallRemovalStackSchedulerPlan>
plan_recall_removal_with_stack_scheduler(const std::vector<IrOp>& ops, int recall_index,
                                         const std::optional<RegisterValueSet>& x2_register_state,
                                         const std::optional<X2ValueDataflowState>& x2_value_state,
                                         const DirectReturnAnalysisContext& context,
                                         const RecallRemovalStackSchedulerOptions& options = {});
bool is_known_return_call_op(const IrOp& op);
std::optional<int> direct_call_target_index(const IrOp& call,
                                            const DirectReturnAnalysisContext& context);
std::optional<int> known_return_call_target_index(const IrOp& call,
                                                  const DirectReturnAnalysisContext& context);
bool linear_return_range_is_transparent(const std::vector<IrOp>& ops, int target_index,
                                        const std::set<int>& label_entries,
                                        const std::function<bool(const IrOp&)>& is_transparent);
bool direct_call_returns_through_transparent_range(
    const std::vector<IrOp>& ops, const IrOp& call, const DirectReturnAnalysisContext& context,
    const std::function<bool(const IrOp&)>& is_transparent);
bool known_return_call_returns_through_transparent_range(
    const std::vector<IrOp>& ops, const IrOp& call, const DirectReturnAnalysisContext& context,
    const std::function<bool(const IrOp&)>& is_transparent);
bool known_return_call_returns_through_nested_transparent_range(
    const std::vector<IrOp>& ops, const IrOp& call, const DirectReturnAnalysisContext& context,
    const std::function<bool(const IrOp&)>& is_transparent);
bool x2_known_return_call_preserves_stack_x_and_x2(
    const std::vector<IrOp>& ops, const IrOp& call,
    const DirectReturnAnalysisContext& context);
std::vector<std::optional<RegisterValueSet>>
compute_x2_register_states(const std::vector<IrOp>& ops);
std::optional<RegisterValueSet>
transfer_x2_register_state_for_edge(const X2RegisterEdgeState& input, const IrOp& op,
                                    X2DataflowEdgeKind edge);
X2ValueDataflowState empty_x2_value_dataflow_state(bool track_register_memory = false);
X2ValueDataflowState clone_x2_value_dataflow_state(const X2ValueDataflowState& input);
std::vector<std::optional<X2ValueDataflowState>>
compute_x2_value_states(const std::vector<IrOp>& ops,
                        const X2ValueStatesOptions& options = {});
std::optional<X2ValueDataflowState>
transfer_x2_value_state_for_edge(const std::optional<X2ValueDataflowState>& input, const IrOp& op,
                                X2DataflowEdgeKind edge,
                                const X2TransferStateOptions& options = {},
                                std::optional<int> producer_index = std::nullopt);
std::optional<X2ValueDataflowState>
transfer_x2_value_state_through_known_transparent_return_call(
    const std::optional<X2ValueDataflowState>& input, const IrOp& call,
    const X2TransferStateOptions& options = {}, std::optional<int> producer_index = std::nullopt);
std::optional<X2ValueDataflowState>
transfer_x2_value_state_through_known_transparent_return_call(
    const std::vector<IrOp>& ops, const IrOp& call,
    const std::optional<X2ValueDataflowState>& input,
    const DirectReturnAnalysisContext& context,
    const X2TransferStateOptions& options = {},
    std::optional<int> producer_index = std::nullopt);
X2ValueDataflowState join_x2_value_dataflow_states(
    const std::optional<X2ValueDataflowState>& current, const X2ValueDataflowState& incoming,
    bool track_register_memory = false);
bool same_x2_value_dataflow_state(const std::optional<X2ValueDataflowState>& left,
                                 const std::optional<X2ValueDataflowState>& right);

} // namespace mkpro::core::passes
