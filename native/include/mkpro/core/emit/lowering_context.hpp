#pragma once

#include "mkpro/core/ast.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/result.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
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
  std::map<std::string, std::string> registers;
  std::map<std::string, int> register_index_by_name;
  std::map<std::string, const V2Rule*> rules;
  std::map<std::string, int> proc_call_counts;
  std::set<std::string> inline_statement_rules;
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
  std::map<std::string, const V2Board*> boards;
  std::map<std::string, const V2StateField*> state_banks;
  std::map<std::string, BankSelectorCacheEntry> bank_selector_cache;
  std::map<std::string, std::vector<std::string>> segmented_cells;
  std::set<std::string> scaled_coord_lists;
  std::set<std::string> scaled_coord_cell_names;
  std::set<std::string> scaled_coord_line_count_targets;
  std::set<std::string> scaled_coord_variables;
  std::set<std::string> removable_coord_lists;
  std::map<std::string, std::string> segmented_hit_helpers;
  std::vector<std::string> segmented_hit_helper_order;
  std::optional<std::string> bit_mask_helper;
  std::optional<std::string> bit_mask_helper_scratch;
  int bit_mask_helper_calls = 0;
  int bit_mask_condition_helper_calls = 0;
  std::optional<std::string> packed_score_helper;
  std::map<std::string, std::string> spatial_sum_helper_labels;
  std::map<std::string, SpatialHitHelperRequest> spatial_hit_helpers;
  std::vector<std::string> spatial_hit_helper_order;
  std::map<std::string, std::string> indirect_helper_registers;
  std::vector<SpatialSumHelperRequest> spatial_sum_helpers;
  std::vector<TerminalTailHelperRequest> terminal_tail_helpers;
  std::optional<std::string> spatial_count_counter_override;
  std::optional<std::string> tail_show_target;
  std::set<std::string> transient_show_targets;
  std::map<std::string, std::string> preloaded_numbers;
  std::set<std::string> suppress_constant_preloads;
  std::vector<FractionalConstantSelectorPlan> fractional_constant_selectors;
  std::map<std::string, Expression> constants;
  std::vector<Diagnostic> diagnostics;
  std::vector<OptimizationReport> optimizations;
  int constant_indexed_state_resolutions = 0;
  std::optional<std::string> current_rule_name;
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
  bool stack_resident_temps = false;
  bool x_param_value_functions = false;
  bool x_param_y_stack_stored_entry = false;
  bool compact_bit_mask_helper_body = false;
  bool domain_error_guards = false;
  bool comparison_guarded_update_selectors = false;
  bool indirect_underflow_decrement = false;
  bool recall_stored_input_after_decrement = false;
  bool dead_source_residual_temp_reuse = false;
  bool aggressive_terminal_direct = false;
  bool invert_branch_order = false;
  bool order_procs_by_call_count = false;
  std::string proc_layout_strategy;
};

} // namespace mkpro
