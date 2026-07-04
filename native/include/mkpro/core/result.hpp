#pragma once

#include "mkpro/core/ir.hpp"
#include "mkpro/core/machine_profile.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mkpro {

enum class DiagnosticSeverity {
  Note,
  Warning,
  Error,
};

struct Diagnostic {
  DiagnosticSeverity severity;
  std::string code;
  std::string message;
};

struct ResolvedStep {
  int address = 0;
  int opcode = 0;
  std::string hex;
  std::string mnemonic;
  std::optional<std::string> comment;
};

struct PreloadReport {
  std::string register_name;
  std::string value;
  bool counts_against_program = false;
  std::optional<std::string> setup_target_name;
  bool setup_expression = false;
  std::optional<std::string> setup_expression_text;
  std::optional<int> setup_source_line;
};

struct OptimizationReport {
  std::string name;
  std::string detail;
};

struct ReferenceReport {
  std::string name;
  int reference_steps = 0;
  int reference_span = 0;
  int reference_entries = 0;
  std::vector<std::string> reference_gaps;
  int compiled_steps = 0;
  int delta = 0;
  std::string parity;
};

struct OptimizerCapabilityReport {
  std::string id;
  std::string category;
  std::string source;
  std::string status;
  std::string detail;
  std::vector<std::string> required_features;
};

struct OptimizerReport {
  bool automatic = false;
  int active = 0;
  int considered = 0;
  int candidate = 0;
  int planned = 0;
  std::vector<OptimizerCapabilityReport> capabilities;
};

struct IrReport {
  bool lowered = false;
  bool v2 = false;
  int intent_nodes = 0;
  int effect_ops = 0;
  int layout_cells = 0;
};

struct CandidateReport {
  std::string site;
  std::string variant;
  int steps = 0;
  bool selected = false;
  std::string reason;
};

struct SizeAttributionEntry {
  std::string kind;
  std::string label;
  int cells = 0;
  int occurrences = 0;
  int first_address = 0;
  int last_address = 0;
  std::string detail;
};

struct SizeOpportunityReport {
  std::string site;
  std::string variant;
  int current_steps = 0;
  int candidate_steps = 0;
  int savings = 0;
  std::string reason;
  std::string blocker_kind;
  std::map<std::string, std::string> details;
};

struct SizeSelectedOptimizationReport {
  std::string site;
  std::string variant;
  int current_steps = 0;
  int baseline_steps = 0;
  int savings = 0;
  std::string reason;
  std::map<std::string, std::string> details;
};

struct SizeBlockerSummaryReport {
  std::string blocker_kind;
  int opportunities = 0;
  int potential_savings = 0;
  int best_savings = 0;
  std::string best_site;
  std::string best_variant;
  std::string best_reason;
  std::map<std::string, std::string> best_details;
};

struct SizeNextActionSummaryReport {
  std::string source;
  std::string action;
  std::string status;
  int opportunities = 0;
  int potential_savings = 0;
  int best_savings = 0;
  std::string best_site;
  std::string best_variant;
  std::string best_blocker_kind;
  std::string best_reason;
  std::map<std::string, std::string> best_details;
};

struct SizeSpillSummaryReport {
  std::string name;
  int total_cells = 0;
  int recall_cells = 0;
  int store_cells = 0;
  int recall_occurrences = 0;
  int store_occurrences = 0;
  int first_address = 0;
  int last_address = 0;
};

struct SizeHelperSummaryReport {
  std::string label;
  int total_cells = 0;
  int body_cells = 0;
  int call_site_cells = 0;
  int body_occurrences = 0;
  int call_occurrences = 0;
  int register_traffic_cells = 0;
  int register_recall_cells = 0;
  int register_store_cells = 0;
  int register_traffic_occurrences = 0;
  int first_address = 0;
  int last_address = 0;
  std::map<std::string, std::string> details;
};

struct SizeHelperSpillSummaryReport {
  std::string helper_label;
  std::string name;
  int total_cells = 0;
  int recall_cells = 0;
  int store_cells = 0;
  int recall_occurrences = 0;
  int store_occurrences = 0;
  int first_address = 0;
  int last_address = 0;
};

struct SizeAbiBlockerReport {
  std::string kind;
  std::string label;
  int line = 0;
  int materialize_cells = 0;
  std::string reason;
  std::map<std::string, std::string> details;
};

struct SizeAttributionReport {
  int total_cells = 0;
  std::vector<SizeAttributionEntry> entries;
  std::vector<SizeHelperSummaryReport> helpers;
  std::vector<SizeHelperSpillSummaryReport> helper_spills;
  std::vector<SizeAbiBlockerReport> abi_blockers;
  std::vector<SizeSelectedOptimizationReport> selected_optimizations;
  std::vector<SizeOpportunityReport> opportunities;
  std::vector<SizeBlockerSummaryReport> blockers;
  std::vector<SizeNextActionSummaryReport> next_actions;
  std::vector<SizeSpillSummaryReport> spills;
};

// Human-readable proof report derived from local verifier results. This is an
// output artifact only: optimizer candidate selection must re-run the relevant
// verifier at the optimization boundary and must not trust caller-supplied
// ProofReport entries.
struct ProofReport {
  std::string id;
  std::string status;
  std::string detail;
};

struct SetupProgramReport {
  std::vector<ResolvedStep> steps;
  std::string reason;
  std::vector<OptimizationReport> optimizations;
};

struct ManualSetupInput {
  std::string name;
  std::string stack;
  std::optional<int> min;
  std::optional<int> max;
};

struct RegisterShare {
  std::string free_register;
  std::string keep_register;
};

struct FractionalConstantSelectorPlan {
  std::string value;
  int target = 0;
};

struct SynthesizedDispatchProofConstraint {
  double input = 0.0;
  int target = 0;
};

// A solved computed-dispatch for one `match` statement: instead of a k-way
// compare chain, the selector value is transformed by op(scale*x + offset) and
// used as an indirect jump (К БП r), landing directly on the matching case body.
// The formula is found post-layout by the address-formula solver so that each
// case value resolves to that case body entry address. The proof constraints are
// captured at the fixpoint that solved the final post-layout entry addresses, so
// the optimizer gate can re-check this candidate statically without falling back
// to emulator-backed behavioral equivalence.
struct SynthesizedDispatchPlan {
  int match_line = 0;            // identifies the match statement to rewrite
  std::string selector_register; // stable register holding the raw selector value
  std::string indirect_register; // stable register used for the К БП indirect jump
  int op_opcode = -1;            // unary op applied last (-1 = identity)
  std::string op_name;           // human-readable op name (for listing comments)
  double scale = 1.0;            // affine pre-multiply
  double offset = 0.0;           // affine pre-add
  std::vector<SynthesizedDispatchProofConstraint> proof_constraints;
  bool default_guard = false;     // true => explicit unmatched values branch to `otherwise`
  bool proof_angle_fixed = false; // true => proof_angle_mode pins trig op semantics
  std::string proof_angle_mode;   // "deg", "rad", or "grd" for proof re-checks
};

struct SignPackedStatePlan {
  std::string state;
  std::string carrier;
  int bit = 0;
};

enum class DeliveryMode {
  Manual,
  Loader,
  Hex,
};

enum class OutputFormat {
  Listing,
  Hex,
  Mk61s,
  Dot,
  Json,
  Keys,
  All,
};

struct CompileOptions {
  DeliveryMode delivery = DeliveryMode::Manual;
  OutputFormat output = OutputFormat::Listing;
  std::optional<int> budget;
  bool analysis = false;
  bool strict = false;
  bool aggressive_terminal_direct = false;
  bool invert_branch_order = false;
  bool canonicalize_if_chains = false;
  bool free_residual_dispatch_scratch = false;
  bool preserve_dispatch_case_order = false;
  bool single_bit_mask_op_copy_reuse = false;
  bool indirect_underflow_decrement = false;
  bool alias_x_reuse = false;
  bool segmented_bitplanes = false;
  bool segmented_line_count_scan = false;
  bool tail_branch_inversion = false;
  bool conditional_branch_trampoline = false;
  bool shared_straight_line_call_bodies = false;
  // Extract one skeleton from straight-line regions that are identical except
  // for the target of their (single, repeated) leaf call. Callers charge a
  // spare stable register with their leaf address and the skeleton dispatches
  // through К ПП r. Verified by callee_hole_indirect_call_targets_proved.
  bool callee_hole_straight_line_helper = false;
  bool disable_interprocedural_opts = false;
  bool coalesce_copies = false;
  bool aggressive_indirect_call_threshold = false;
  bool dual_use_constant_indirect_flow = false;
  bool aggressive_post_layout_indirect_flow = false;
  bool return_stack_script = false;
  bool disable_return_stack_script = false;
  // Opt-out used by focused unit tests that pin a specific mid-level lowering
  // (e.g. shared-helper / direct-ПП structure). The aggressive post-layout
  // indirect-flow rescue is enabled by default for every program; setting this
  // suppresses only that late repacking while leaving the rest of candidate
  // search (helper sharing, etc.) intact.
  bool disable_aggressive_post_layout = false;
  bool preloaded_indirect_flow = false;
  // Allow the post-layout indirect-flow rescue to convert FORWARD direct
  // transfers (target after the call site) into 1-cell indirect calls/jumps
  // through a register whose value resolves to that forward address. Runtime
  // К ПП/К БП is direction-agnostic; correctness is still guaranteed because
  // every rewrite is validated to decode to the proven target address.
  bool forward_indirect_flow = false;
  bool runtime_indirect_call_flow = false;
  bool general_constant_preloads = false;
  bool stack_resident_temps = false;
  bool share_random_cell = false;
  bool startup_aware_constant_preloads = false;
  bool guarded_prologue_gadgets = false;
  bool shared_bit_mask_helper_calls = false;
  bool compact_bit_mask_helper_body = false;
  bool signed_abs_match_pairs = false;
  bool synthesize_parametric_siblings = false;
  bool pack_counter_stripes = false;
  bool sentinel_decimal_pack = false;
  bool trig_fractional_pack = false;
  bool sign_pack_state = false;
  bool packed_score_accumulator_helpers = false;
  bool disable_packed_line_family_score_accumulator = false;
  bool canonicalize_repeated_unary_update_args = false;
  bool alternating_sign_toggle_args = false;
  // Rewrite self-decrementing packed-line mark walks and monolithic score walks
  // into the same explicit (line prep; leaf(bank_slot)) sequence so callee-hole
  // IR merging can unify them.
  bool canonicalize_packed_line_bank_walks = false;
  bool x_param_value_functions = false;
  bool x_param_y_stack_stored_entry = false;
  bool stack_argument_helper_entries = false;
  bool packed_line_family_update_check_tail = false;
  bool packed_line_family_mutating_selector_update_check_tail = false;
  bool packed_line_family_borrowed_mutating_selector_update_check_tail = false;
  bool inline_floor_packed_row_expressions = false;
  bool unroll_counted_loops = false;
  bool setup_only_counted_loop_init = false;
  bool domain_error_guards = false;
  bool show_read_guarded_transfer = false;
  bool comparison_guarded_update_selectors = false;
  bool recall_stored_input_after_decrement = false;
  bool aggressive_indirect_call = false;
  bool dead_source_residual_temp_reuse = false;
  bool hoist_shared_helpers = false;
  bool hoist_procs = false;
  bool order_procs_by_call_count = false;
  std::string proc_layout_strategy;
  bool disable_candidate_search = false;
  bool fast_candidate_search = false;
  int fast_candidate_threshold_ms = 500;
  bool collect_coalesce_shares = false;
  // Requested setup-time constant preloads. This is optimizer intent, not a
  // proof artifact: indirect-flow gates must still prove branch selectors from
  // final PreloadReport entries rather than trusting this map.
  std::map<std::string, std::string> preloaded_constant_registers;
  std::set<std::string> suppress_constant_preloads;
  std::vector<FractionalConstantSelectorPlan> fractional_constant_selectors;
  // When true, the lowering omits the `К {x}` fractional-recovery op that
  // normally restores a dual-use register's data value after its integer part
  // has been retuned to an address. This is only sound when the integer part is
  // erased before every data read (the reference program relies on exactly
  // this). Static candidate selection treats this as dangerous unless final
  // artifacts prove the erase happens before any arithmetic consumer.
  bool assume_dead_selector_integer_part = false;
  // Solved computed-dispatch plans (one per rewritten match). Default empty:
  // normal compiles keep the compare-chain lowering untouched.
  std::vector<SynthesizedDispatchPlan> synthesized_dispatch_plans;
  std::vector<std::string> force_fractional_constant_selector_preloads;
  std::vector<std::string> pack_counter_stripe_names;
  std::vector<std::string> sentinel_decimal_pack_names;
  std::vector<std::string> trig_fractional_pack_names;
  std::vector<SignPackedStatePlan> sign_packed_state_plans;
  std::vector<RegisterShare> forced_register_shares;
};

struct CompileResult {
  bool implemented = false;
  std::vector<Diagnostic> diagnostics;
  std::vector<std::string> warnings;
  std::vector<MachineItem> items;
  std::vector<ResolvedStep> steps;
  std::map<std::string, std::string> registers;
  std::map<std::string, std::string> labels;
  std::vector<PreloadReport> preloads;
  std::vector<ManualSetupInput> manual_setup_inputs;
  std::vector<OptimizationReport> optimizations;
  std::optional<ReferenceReport> reference;
  OptimizerReport optimizer;
  IrReport ir;
  SizeAttributionReport size_attribution;
  std::vector<CandidateReport> candidates;
  std::vector<MachineFeatureUseReport> machine_features_used;
  std::vector<ProofReport> proofs;
  std::vector<EmulatorFactReport> emulator_facts;
  std::vector<CandidateReport> rejected_candidates;
  std::optional<SetupProgramReport> setup_program;
  std::optional<std::string> expected_mode;
  std::vector<RegisterShare> coalesce_shares;
  std::string hex;
  std::string listing;
  std::string setup_hex;
  std::string setup_listing;
};

CompileResult compile_source_stub(std::string source, const CompileOptions& options);
std::string diagnostic_severity_name(DiagnosticSeverity severity);

} // namespace mkpro
