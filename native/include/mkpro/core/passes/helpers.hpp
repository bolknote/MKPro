#pragma once

#include "mkpro/core/formal_address.hpp"
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

struct X2ReplacementStackLiftOptions {
  const std::set<int>* invalidated_producer_indexes = nullptr;
  bool allow_duplicate_y_stack_proof = true;
};

struct X2ReplacementStackLiftPlan {
  bool initially_exposes_stack_lift = false;
  std::optional<int> stack_lift_producer_index;
  bool stack_lift_already_supplied = false;
  bool exposes_stack_lift = false;
};

struct X2HiddenTempReplacement {
  int index = 0;
  std::string register_name;
  bool branch_merged = false;
};

struct X2LiteralReplacement {
  int start = 0;
  int end = 0;
  std::string display_value;
};

enum class X2DataflowEdgeKind {
  Normal,
  Fallthrough,
  Jump,
};

inline bool has_rewrite_barrier(const IrOp& op) {
  return op.meta.raw || op.meta.manual_interaction.has_value();
}

// Exact-decimal concrete-evaluation engine (faithful port of the TS BigInt
// arithmetic). Exposed for focused unit testing in isolation.
std::optional<std::string> concrete_decimal_binary_value(int opcode, const std::string& y,
                                                         const std::string& x);
std::optional<std::string> concrete_decimal_unary_value(int opcode, const std::string& value);

// Deterministic shape-data-model serialization, exposed for the differential
// unit test against the TypeScript oracle.
std::string x2_shape_data_model_debug(const X2ShapeFact& fact);

// Session A (concrete-eval engine) isolation test hooks. These exercise the
// dead-code x2eval engine against the TypeScript oracle.
std::string x2_display_cluster_debug(const X2ShapeFact& fact);
std::string x2_decimal_fraction_part_shape_debug(const std::string& value);
// Session A.2 (structural-hex unary engine) isolation test hook. Exercises the
// dead-code plainProducesConcreteStructuralUnaryDecimal* entry points.
std::string x2_structural_unary_debug(int opcode, const X2ShapeFact& fact);
// Session A.2b (binary structural bitwise) isolation test hook.
std::string x2_structural_bitwise_binary_debug(int opcode, const X2ValueSet& y, const X2ValueSet& x,
                                               const X2ShapeSet& y_shape, const X2ShapeSet& x_shape);
// Session A.2b (binary structural-hex arithmetic) isolation test hook.
std::string x2_structural_hex_binary_debug(int opcode, const X2ValueSet& y, const X2ValueSet& x,
                                           const X2ShapeSet& y_shape, const X2ShapeSet& x_shape,
                                           const X2ShapeSet& direct_y_shape, const X2ShapeSet& direct_x_shape);

// Session A.3 + A.4 (decimal concrete-eval + stable/opaque expression-key) isolation test hook.
std::string x2_stable_expression_debug(int opcode, bool has_producer, int producer_index,
                                       const X2ValueSet& x, const X2ValueSet& y, const X2ShapeSet& x_shape,
                                       const X2ShapeSet& y_shape, const X2ShapeSet& direct_x_shape,
                                       const X2ShapeSet& direct_y_shape);

// Session B (entry/VP/structural state-machine builders) isolation test hook.
std::string x2_state_builders_debug(const std::string& scenario, const std::string& a,
                                    const std::string& b, const std::string& c);

// Session C (VP first-digit splice machinery) isolation test hook.
std::string x2_vp_splice_debug(const std::string& x_shape, const std::string& x2_shape,
                               bool include_exponent_targets);

// Session C.2 (transferPlainX2VpEntry* + shared*/vpSplice cluster) test hook.
std::string x2_vp_entry_debug(int opcode, const std::string& x, const std::string& x2,
                              const std::string& x_shape, const std::string& x2_shape,
                              const std::string& in_vp_mantissa, const std::string& in_vp_sign_mantissa,
                              const std::string& in_vp_shape, const std::string& in_vp_sign_shape,
                              bool mantissa_transient, bool shape_transient);

// Session C.3 (transfer dispatchers + transfer_plain_x2_value_state) test hook.
std::string x2_transfer_plain_debug(const std::string& opcodes_csv, bool use_producer);
std::string x2_join_debug(const std::string& csv_a, const std::string& csv_b, bool use_producer);

// x2-noop-restore pass: faithful decision logic consumed by the pass.
std::vector<int> x2_noop_restore_removed_indexes(const std::vector<IrOp>& ops);

int cells_per_op(const IrOp& op);
std::map<std::string, int> calculate_label_addresses(const std::vector<IrOp>& ops);
std::map<std::string, int> label_indexes(const std::vector<IrOp>& ops);
std::map<int, int> address_indexes(const std::vector<IrOp>& ops);
std::set<int> compute_label_entry_indexes(const std::vector<IrOp>& ops);
DirectReturnAnalysisContext direct_return_analysis_context(const std::vector<IrOp>& ops);
std::optional<int> target_address(const IrTarget& target, const std::map<std::string, int>& labels);
std::optional<std::string> known_indirect_memory_target(const IrOp& op);
std::optional<std::set<std::string>> known_indirect_memory_targets(const IrOp& op);
// Parses a proof-carrying `indirect-target=N` annotation only when the number is
// followed by a clear marker boundary. Malformed comments are not treated as
// known control-flow facts.
std::optional<int> known_indirect_flow_target(const IrOp& op);
std::optional<int> known_indirect_flow_target(const IrOp& op, AddressSpaceModel model);
// Target entry labels advertised by a computed-dispatch indirect jump (empty for
// every other op). These edges are otherwise invisible to the CFG because the
// jump address is computed at runtime; reachability/overlay passes consult this
// so the dispatched case bodies are not treated as dead/unreferenced.
std::vector<std::string> computed_dispatch_target_labels(const IrOp& op);
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
bool x2_state_has_visible_unary_noop(const std::optional<X2ValueDataflowState>& state, int opcode);
bool x2_states_have_same_vp_entry_source(const std::optional<X2ValueDataflowState>& left,
                                         const std::optional<X2ValueDataflowState>& right);
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
X2ReplacementStackLiftPlan plan_x2_replacement_stack_lift(
    const std::vector<IrOp>& ops, int replacement_start, int stack_exposure_end,
    const std::optional<X2ValueDataflowState>& state, const DirectReturnAnalysisContext& context,
    bool initially_exposes_stack_lift, const X2ReplacementStackLiftOptions& options = {});
std::optional<RecallRemovalStackSchedulerPlan>
plan_recall_removal_with_stack_scheduler(const std::vector<IrOp>& ops, int recall_index,
                                         const std::optional<RegisterValueSet>& x2_register_state,
                                         const std::optional<X2ValueDataflowState>& x2_value_state,
                                         const DirectReturnAnalysisContext& context,
                                         const RecallRemovalStackSchedulerOptions& options = {});
std::vector<X2HiddenTempReplacement>
x2_hidden_temp_restore_replacements(const std::vector<IrOp>& ops);
std::vector<X2LiteralReplacement> x2_literal_restore_replacements(const std::vector<IrOp>& ops);
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

// vp-splice candidate planner (faithful port of helpers.ts
// x2PlanVpSpliceCandidatesAt). The terminal sub-planners need the pass-supplied
// predicates for fresh decimal digits / hard X2 overwrites without stack use.
struct X2VpSplicePlannerOptions {
  std::function<bool(const IrOp&, int)> is_decimal_digit;
  std::function<bool(const IrOp&, int)> is_hard_x2_overwrite_without_stack_use;
};

struct X2VpSpliceCandidate {
  std::string stage;
  std::vector<int> removable_indexes;
  std::optional<std::string> source_match_reason;
  std::optional<std::string> sign_restore_source_proof_reason;
};

std::vector<X2VpSpliceCandidate> x2_plan_vp_splice_candidates_at(
    const std::vector<IrOp>& ops, int index,
    const std::vector<std::optional<X2ValueDataflowState>>& states,
    const DirectReturnAnalysisContext& context, const X2VpSplicePlannerOptions& options);

} // namespace mkpro::core::passes
