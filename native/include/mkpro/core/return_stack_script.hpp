#pragma once

#include "mkpro/core/post_layout_indirect_flow.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mkpro::core {

struct ReturnStackReturnStep {
  int stored_return_address = 0;
  int target_address = 0;
  std::vector<int> stack_after_return;
};

struct ReturnStackTailBlock {
  std::string label;
  std::vector<IrOp> body;
};

struct ReturnStackExistingCallSite {
  std::string label;
  std::string target_label;
  std::string continuation_label;
  int source_address = -1;
};

struct ReturnStackLayoutOpportunity {
  std::vector<ReturnStackTailBlock> tails;
  std::vector<IrOp> entry_body;
  std::string entry_label = "__return_stack_entry";
  std::vector<ReturnStackExistingCallSite> existing_call_sites;
  bool entry_at_address_zero = true;
  bool single_start_jump = false;
};

struct ReturnStackStartupLayoutPlan {
  std::vector<MachineItem> items;
  int transitions = 0;
  int injected_call_sites = 0;
  int existing_call_sites = 0;
  int paid_call_sites = 0;
  int transition_savings = 0;
  int charge_cost = 0;
  int address_overlay_savings = 0;
  int address_shift_risk_count = 0;
  int address_shift_cell_count = 0;
  int net_savings = 0;
  bool profitable = false;
  bool one_shot_proved = false;
  bool no_backward_charge_jumps = false;
  bool no_external_charge_entries = false;
  bool unique_entry_after_charge = false;
  bool tail_order_proved = false;
  bool allowed_by_size_rescue = false;
  std::string strategy;
  std::string rejection_reason;
  std::vector<std::string> ordered_tail_labels;
  std::vector<std::string> proofs;
  std::vector<std::string> risk_reasons;
};

struct ReturnStackStartupLayoutOptions {
  int existing_call_sites = 0;
  int min_net_savings = 1;
  bool size_rescue = false;
};

struct ReturnStackLayoutOpportunityAnalysis {
  ReturnStackLayoutOpportunity opportunity;
  ReturnStackStartupLayoutOptions options;
  ReturnStackStartupLayoutPlan plan;
};

struct ReturnStackIrTailLayoutSearch {
  bool has_opportunity = false;
  bool materialized = false;
  int extracted_tail_fragments = 0;
  int rewritten_tail_fragments = 0;
  int reused_generated_tail_fragments = 0;
  int reused_existing_tail_fragments = 0;
  int extracted_existing_callsite_fragments = 0;
  int reused_generated_callsite_fragments = 0;
  int cfg_tail_entry_candidates = 0;
  int cfg_tail_valid_chain_candidates = 0;
  int cfg_tail_short_chain_candidates = 0;
  int cfg_tail_too_long_chain_candidates = 0;
  int cfg_tail_broken_chain_candidates = 0;
  int cfg_tail_unresolved_chain_candidates = 0;
  int cfg_tail_repeated_chain_candidates = 0;
  int cfg_tail_nonterminal_chain_candidates = 0;
  std::vector<std::string> cfg_tail_nonterminal_break_labels;
  int cfg_tail_external_entry_rejections = 0;
  std::vector<std::string> cfg_tail_external_entry_labels;
  std::vector<std::string> cfg_tail_external_predecessor_labels;
  int symbolic_existing_callsite_hints = 0;
  int symbolic_existing_callsite_target_groups = 0;
  int symbolic_existing_callsite_largest_target_group = 0;
  std::vector<std::string> symbolic_existing_callsite_target_labels;
  std::vector<std::string> symbolic_existing_callsite_target_group_details;
  std::vector<std::string> symbolic_existing_callsite_source_labels;
  std::vector<std::string> symbolic_existing_callsite_source_target_details;
  ReturnStackLayoutOpportunityAnalysis analysis;
  std::vector<MachineItem> materialized_items;
  bool pipeline_compared = false;
  bool pipeline_candidate_better = false;
  int pipeline_candidates_measured = 0;
  int pipeline_current_final_cells = 0;
  int pipeline_candidate_final_cells = 0;
  std::string rejection_reason;
};

struct ReturnStackScriptOpportunityScan {
  bool possible = false;
  int direct_call_sites = 0;
  int direct_jumps = 0;
  int chained_call_sites = 0;
  std::string rejection_reason;
};

struct ReturnStackPostLayoutPipelineReport {
  int input_cells = 0;
  int return_stack_script_applied = 0;
  int address_overlay_applied = 0;
  int indirect_flow_applied = 0;
  int final_cells = 0;
};

struct ReturnStackPostLayoutPipelineComparison {
  ReturnStackPostLayoutPipelineReport current;
  ReturnStackPostLayoutPipelineReport candidate;
  bool candidate_better = false;
};

struct DirtyReturnStackDispatchCellProof {
  int dirty_return_address = 0;
  int actual_pc = 0;
  int required_opcode = 0;
  bool safe = false;
  std::string continuation_proof;
  std::string rejection_reason;
};

struct DirtyReturnStackDispatchPlan {
  std::vector<int> clean_targets;
  std::vector<int> dirty_return_addresses;
  std::vector<int> dirty_targets;
  std::vector<DirtyReturnStackDispatchCellProof> cell_proofs;
  bool high_risk = true;
  bool enabled = false;
  bool layout_proved = false;
  std::string risk_reason;
  std::string rejection_reason;
};

struct DirtyReturnStackDispatchAllocationPlan {
  DirtyReturnStackDispatchPlan dispatch;
  std::vector<MachineItem> items;
  int padding_cells = 0;
  int append_padding_cells = 0;
  int insert_padding_cells = 0;
  int fixed_point_rounds = 0;
  bool allocated = false;
  bool size_rescue_only = true;
  bool control_flow_rewrite_enabled = false;
  std::string rejection_reason;
};

struct DirtyReturnStackDispatchOptions {
  bool size_rescue = false;
  int min_dirty_targets = 1;
  bool expect_fallthrough = true;
  int max_padding_cells = 12;
  int max_append_padding_cells = -1;
  int max_insert_padding_cells = -1;
  int max_fixed_point_rounds = 4;
  bool allow_append_padding = true;
  bool allow_insert_padding = true;
};

std::vector<int> mk61_return_stack_after_call(std::vector<int> stack,
                                              int stored_return_address);
std::optional<ReturnStackReturnStep> mk61_return_stack_after_return(
    const std::vector<int>& stack);
std::vector<ReturnStackReturnStep> simulate_mk61_return_stack(
    std::vector<int> stack, int return_count);
ReturnStackLayoutOpportunityAnalysis analyze_return_stack_layout_opportunity(
    ReturnStackLayoutOpportunity opportunity,
    const ReturnStackStartupLayoutOptions& options = {});
ReturnStackStartupLayoutPlan materialize_return_stack_layout(
    const ReturnStackLayoutOpportunityAnalysis& analysis);
ReturnStackStartupLayoutPlan build_return_stack_startup_layout(
    const std::vector<ReturnStackTailBlock>& tails,
    const std::vector<IrOp>& entry_body,
    const std::string& entry_label = "__return_stack_entry",
    const ReturnStackStartupLayoutOptions& options = {});
ReturnStackIrTailLayoutSearch analyze_return_stack_ir_tail_layout(
    const std::vector<IrOp>& ops,
    const ReturnStackStartupLayoutOptions& options = {});
ReturnStackIrTailLayoutSearch analyze_return_stack_ir_tail_layout_with_pipeline(
    const std::vector<IrOp>& ops, const std::vector<MachineItem>& current_items,
    const CompileOptions& compile_options,
    const ReturnStackStartupLayoutOptions& options = {},
    int indirect_flow_rescue_above = 105);
ReturnStackScriptOpportunityScan scan_return_stack_script_opportunity(
    const std::vector<MachineItem>& items);
std::string explain_return_stack_script_rejection(const std::vector<MachineItem>& items);
DirtyReturnStackDispatchPlan plan_dirty_return_stack_dispatch(std::vector<int> stack,
                                                              int return_count,
                                                              const DirtyReturnStackDispatchOptions&
                                                                  options = {});
DirtyReturnStackDispatchPlan plan_dirty_return_stack_dispatch(
    std::vector<int> stack, int return_count, const std::vector<MachineItem>& layout,
    const DirtyReturnStackDispatchOptions& options = {});
DirtyReturnStackDispatchAllocationPlan allocate_dirty_return_stack_dispatch_layout(
    std::vector<int> stack, int return_count, const std::vector<MachineItem>& layout,
    const DirtyReturnStackDispatchOptions& options = {});
std::vector<DirtyReturnStackDispatchAllocationPlan>
allocate_dirty_return_stack_dispatch_layouts(const std::vector<MachineItem>& layout,
                                             const DirtyReturnStackDispatchOptions& options = {});

PostLayoutIndirectFlowResult
optimize_post_layout_return_stack_script(const std::vector<MachineItem>& items);
ReturnStackPostLayoutPipelineReport measure_return_stack_post_layout_pipeline(
    const std::vector<MachineItem>& items, const CompileOptions& options,
    int indirect_flow_rescue_above = 105);
ReturnStackPostLayoutPipelineComparison compare_return_stack_post_layout_pipeline(
    const std::vector<MachineItem>& current, const std::vector<MachineItem>& candidate,
    const CompileOptions& options, int indirect_flow_rescue_above = 105);

} // namespace mkpro::core
