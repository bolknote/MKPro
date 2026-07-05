#pragma once

#include "mkpro/core/ast.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/result.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro {

struct SpatialSumHelperRequest {
  std::string key;
  std::string label;
  std::string mask;
  Expression cell;
  std::string counter;
  std::string operation;
};

struct SpatialLineProgressionHelperRequest {
  std::string key;
  std::string label;
  std::string mask;
  Expression cell;
  std::string counter;
  std::string operation;
};

struct LineCountHelperRequest {
  std::string key;
  std::string label;
  Expression cell;
  V2Board board;
  std::string counter;
};

struct SpatialHitHelperRequest {
  std::string mask;
  std::string scratch;
  std::string label;
};

struct TerminalTailHelperRequest {
  std::string key;
  std::string label;
  std::vector<V2Statement> body;
  int line = 0;
};

struct LiteralDisplayHelperRequest {
  std::string key;
  std::string label;
  std::string literal;
  int line = 0;
};

struct PackedDisplayHelperRequest {
  std::string key;
  std::string label;
  V2Statement statement;
  int line = 0;
};

struct DisplayByteHelperRequest {
  std::string key;
  std::string label;
  V2Statement statement;
  int line = 0;
};

struct ShowSequenceHelperRequest {
  std::string key;
  std::string label;
  V2Statement first;
  V2Statement second;
  int line = 0;
};

struct RandomCellHelperRequest {
  std::string key;
  std::string label;
  Expression expr;
  int line = 0;
};

struct ExpressionHelperRequest {
  std::string key;
  std::string label;
  Expression expr;
  int line = 0;
};

struct ExpressionHelperStackEntryRequest {
  std::string key;
  std::string helper_label;
  std::string entry_label;
  std::vector<std::string> temps;
  int call_sites = 0;
};

struct FunctionStackEntryPlan {
  std::vector<std::string> params;
  int call_sites = 0;
};

struct NearAnyHelperRequest {
  std::string key;
  std::string label;
  std::string kind = "near_any";
  Expression value;
  Expression radius;
  int line = 0;
};

struct NearAnyHelperStats {
  int candidate_count = 0;
  int condition_count = 0;
  int ordinary_cost = 0;
  int helper_call_cost = 0;
  int helper_cost = 0;
};

struct XParamProcLowering {
  std::string param;
  V2Statement first;
  std::string kind;
  std::string other;
};

struct XParamYStackProcLowering {
  std::string x_param;
  std::string y_name;
};

struct BankSelectorCacheEntry {
  std::string key;
  std::set<std::string> deps;
  std::string base;
  std::string index_text;
  int offset = 0;
};

struct LoweringContext {
  MachineEmitter emitter;
  const V2Program* program = nullptr;
  std::map<std::string, std::string> registers;
  std::map<std::string, int> register_index_by_name;
  std::map<std::string, const V2Rule*> rules;
  std::map<std::string, int> proc_call_counts;
  std::set<std::string> inline_statement_rules;
  std::set<std::pair<std::string, int>> terminal_underflow_unit_decrements;
  std::map<std::string, XParamProcLowering> x_param_procs;
  std::map<std::string, XParamYStackProcLowering> x_param_y_stack_procs;
  std::optional<std::string> current_y_variable;
  std::set<std::string> ephemeral_input_targets;
  std::map<std::string, Expression> loop_prompt_initials;
  std::map<int, std::string> loop_prompt_by_line;
  std::map<std::string, std::string> loop_prompt_inputs;
  std::set<std::string> stack_only_state_fields;
  std::set<std::string> inline_call_stack;
  std::map<std::string, const V2StateField*> state_fields;
  std::map<std::string, std::set<int>> path_excluded_numeric_values;
  std::map<std::string, const V2Board*> boards;
  std::map<std::string, const V2World*> worlds;
  std::map<std::string, const V2StateField*> state_banks;
  std::map<std::string, BankSelectorCacheEntry> bank_selector_cache;
  std::map<std::string, std::vector<std::string>> segmented_cells;
  std::set<std::string> scaled_coord_lists;
  std::set<std::string> scaled_coord_cell_names;
  std::set<std::string> scaled_coord_line_count_targets;
  std::set<std::string> scaled_coord_variables;
  std::set<std::string> removable_coord_lists;
  std::map<std::string, int> line_count_group_counts;
  std::map<std::string, std::string> line_count_helper_labels;
  std::vector<LineCountHelperRequest> line_count_helpers;
  std::map<std::string, std::string> segmented_hit_helpers;
  std::vector<std::string> segmented_hit_helper_order;
  std::map<std::string, std::string> bit_mask_helper_labels;
  std::vector<std::string> bit_mask_helper_order;
  std::optional<std::string> bit_mask_helper_scratch;
  int bit_mask_helper_calls = 0;
  int bit_mask_condition_helper_calls = 0;
  std::optional<std::string> packed_score_helper;
  std::optional<std::string> packed_score_accumulator_helper;
  std::optional<std::string> packed_score_subtractor_accumulator_helper;
  std::set<std::string> reported_signed_packed_score_accumulator_rejections;
  std::map<std::string, std::string> spatial_sum_helper_labels;
  std::map<std::string, SpatialHitHelperRequest> spatial_hit_helpers;
  std::vector<std::string> spatial_hit_helper_order;
  std::map<std::string, std::string> indirect_helper_registers;
  std::vector<SpatialSumHelperRequest> spatial_sum_helpers;
  std::map<std::string, std::string> spatial_line_progression_helper_labels;
  std::vector<SpatialLineProgressionHelperRequest> spatial_line_progression_helpers;
  std::vector<TerminalTailHelperRequest> terminal_tail_helpers;
  std::map<std::string, int> literal_display_use_counts;
  std::map<std::string, std::string> literal_display_helper_labels;
  std::vector<LiteralDisplayHelperRequest> literal_display_helpers;
  std::map<std::string, int> packed_display_use_counts;
  std::map<std::string, std::string> packed_display_helper_labels;
  std::vector<PackedDisplayHelperRequest> packed_display_helpers;
  std::map<std::string, std::string> display_byte_helper_labels;
  std::vector<DisplayByteHelperRequest> display_byte_helpers;
  std::map<std::string, int> show_sequence_use_counts;
  std::map<std::string, std::string> show_sequence_helper_labels;
  std::vector<ShowSequenceHelperRequest> show_sequence_helpers;
  std::map<std::string, int> expression_use_counts;
  std::map<std::string, int> expression_call_counts;
  std::map<std::string, std::string> random_cell_helper_labels;
  std::vector<RandomCellHelperRequest> random_cell_helpers;
  std::map<std::string, std::string> expression_helper_labels;
  std::map<std::string, int> expression_helper_regular_call_sites;
  std::vector<ExpressionHelperRequest> expression_helpers;
  std::map<std::string, ExpressionHelperStackEntryRequest> expression_helper_stack_entries;
  std::map<std::string, FunctionStackEntryPlan> stack_entry_functions;
  std::map<std::string, NearAnyHelperStats> near_any_helper_stats;
  std::map<std::string, std::string> near_any_helper_labels;
  std::vector<NearAnyHelperRequest> near_any_helpers;
  std::optional<std::string> spatial_count_counter_override;
  std::optional<std::string> tail_show_target;
  std::set<std::string> transient_show_targets;
  std::map<std::string, std::string> preloaded_numbers;
  std::vector<std::string> preloaded_number_order;
  std::set<std::string> suppress_constant_preloads;
  std::optional<std::string> negative_zero_degree_register;
  std::vector<FractionalConstantSelectorPlan> fractional_constant_selectors;
  std::map<std::string, Expression> constants;
  std::map<std::string, std::string> raw_constants;
  std::vector<Diagnostic> diagnostics;
  std::vector<OptimizationReport> optimizations;
  std::optional<OptimizationReport> pending_coord_list_line_count_formatted_report_fusion;
  int constant_indexed_state_resolutions = 0;
  std::optional<std::string> current_rule_name;
  std::optional<std::string> current_loop_label;
  bool uses_formatted_coord_report = false;
  bool tiny_game_shape = false;
  bool human_game_shape = false;
  bool lunar_shape = false;
  bool clock_shape = false;
  bool cave_sketch_shape = false;
  bool ninety_nine_bottles_shape = false;
  bool dungeon_shape = false;
  bool segmented_bitplanes = false;
  bool segmented_line_count_scan = false;
  bool use_packed_score_helper = false;
  bool packed_score_accumulator_helpers = false;
  bool use_packed_score_accumulator_helper = false;
  bool use_packed_score_accumulator_for_singletons = false;
  bool disable_packed_line_family_score_accumulator = false;
  bool stack_resident_temps = false;
  bool stack_argument_helper_entries = false;
  bool setup_only_counted_loop_init = false;
  bool x_param_value_functions = false;
  bool x_param_y_stack_stored_entry = false;
  bool stack_argument_function_entries = false;
  bool canonicalize_packed_line_bank_walks = false;
  bool packed_line_family_update_check_tail = false;
  bool packed_line_family_mutating_selector_update_check_tail = false;
  bool packed_line_family_borrowed_mutating_selector_update_check_tail = false;
  bool shared_bit_mask_helper_calls = false;
  bool compact_bit_mask_helper_body = false;
  bool domain_error_guards = false;
  bool show_read_guarded_transfer = false;
  bool comparison_guarded_update_selectors = false;
  bool indirect_underflow_decrement = false;
  bool recall_stored_input_after_decrement = false;
  bool dead_source_residual_temp_reuse = false;
  bool single_bit_mask_op_copy_reuse = false;
  bool emitting_literal_display_helper = false;
  bool emitting_packed_display_helper = false;
  bool emitting_display_byte_helper = false;
  bool emitting_show_sequence_helper = false;
  bool emitting_random_cell_helper = false;
  bool emitting_expression_helper = false;
  bool emitting_near_any_helper = false;
  bool share_random_cell = false;
  bool hoist_shared_helpers = false;
  bool hoist_procs = false;
  bool aggressive_terminal_direct = false;
  bool aggressive_post_layout_indirect_flow = false;
  bool preloaded_indirect_flow = false;
  bool disable_candidate_search = false;
  bool free_residual_dispatch_scratch = false;
  bool invert_branch_order = false;
  bool preserve_dispatch_case_order = false;
  bool order_procs_by_call_count = false;
  bool assume_dead_selector_integer_part = false;
  std::vector<SynthesizedDispatchPlan> synthesized_dispatch_plans;
  std::string proc_layout_strategy;
};

} // namespace mkpro
