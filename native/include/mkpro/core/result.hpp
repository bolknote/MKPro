#pragma once

#include "mkpro/core/ir.hpp"

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
  std::optional<int> setup_source_line;
};

struct OptimizationReport {
  std::string name;
  std::string detail;
};

struct SetupProgramReport {
  std::vector<ResolvedStep> steps;
  std::string reason;
  std::vector<OptimizationReport> optimizations;
};

struct RegisterShare {
  std::string free_register;
  std::string keep_register;
};

struct FractionalConstantSelectorPlan {
  std::string value;
  int target = 0;
};

enum class DeliveryMode {
  Manual,
  Loader,
  Hex,
};

enum class OutputFormat {
  Listing,
  Hex,
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
  bool disable_interprocedural_opts = false;
  bool coalesce_copies = false;
  bool aggressive_indirect_call_threshold = false;
  bool dual_use_constant_indirect_flow = false;
  bool aggressive_post_layout_indirect_flow = false;
  bool return_stack_script = false;
  bool preloaded_indirect_flow = false;
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
  bool canonicalize_repeated_unary_update_args = false;
  bool x_param_value_functions = false;
  bool x_param_y_stack_stored_entry = false;
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
  bool collect_coalesce_shares = false;
  std::map<std::string, std::string> preloaded_constant_registers;
  std::set<std::string> suppress_constant_preloads;
  std::vector<FractionalConstantSelectorPlan> fractional_constant_selectors;
  std::vector<std::string> force_fractional_constant_selector_preloads;
  std::vector<std::string> pack_counter_stripe_names;
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
  std::vector<OptimizationReport> optimizations;
  std::optional<SetupProgramReport> setup_program;
  std::vector<RegisterShare> coalesce_shares;
  std::string hex;
  std::string listing;
  std::string setup_hex;
  std::string setup_listing;
};

CompileResult compile_source_stub(std::string source, const CompileOptions& options);
std::string diagnostic_severity_name(DiagnosticSeverity severity);

} // namespace mkpro
