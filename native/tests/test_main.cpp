#include <array>
#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

namespace mkpro::tests {

void address_formula_solver_synthesizes_dispatch();
void address_formula_solver_verifies_static_proof_obligations();
void computed_dispatch_targets_survive_dead_code_elimination();
void computed_dispatch_discovery_keeps_program_correct();
void guarded_computed_dispatch_default_discovery_keeps_program_correct();
void optimizer_analysis_reports_sign_pack_and_address_formula_opportunities();
void optimizer_analysis_reports_decimal_and_multi_sign_pack_opportunities();
void state_packing_rewrite_options_affect_lowering();
void optimizer_static_proof_gate_rejects_unproved_dangerous_candidates();
void optimizer_translation_unit_stays_emulator_free();
void arithmetic_if_matches_typescript_contract();
void bit_mask_quotient_reuse_matches_typescript_contract();
void board_width_macros_matches_typescript_contract();
void emulator_grid_norm_signed_modulo_matches_language_contract();
void grid_norm_unary_wrapper_uses_generic_x_entry_and_forwarding();
void branch_target_x_reuse_matches_typescript_contract();
void cfg_matches_typescript_contract();
void compiler_coord_list_lowering_matches_typescript_contract();
void compiler_display_lowering_matches_typescript_contract();
void compiler_feature_profile_expanded_program_space_contract();
void compiler_feature_profile_raw_rf_contract();
void compiler_feature_profile_rf_extends_optimizer_preloads_contract();
void compiler_feature_profile_rf_extends_state_allocator_contract();
void compiler_feature_profile_rf_optimizer_is_size_monotonic_contract();
void compiler_lowers_initial_v2_subset();
void counted_loop_unroll_matches_typescript_contract();
void constant_folding_matches_typescript_contract();
void cse_display_block_matches_typescript_contract();
void cyclic_end_return_relocates_only_proved_direct_call_helpers();
void dark_side_suffix_helper_rewrites_only_proved_layouts();
void helper_invariant_recall_hoist_rewrites_only_proved_calls();
void helper_semantic_alias_compiler_composes_with_natural_target_layout();
void helper_semantic_alias_compiler_rejects_unproved_source_domains();
void helper_semantic_alias_call_origins_survive_return_suffix_merging();
void helper_semantic_alias_emulator_pins_x1_transfer_classes();
void helper_semantic_alias_rebinds_numeric_indirect_targets_by_identity();
void helper_semantic_alias_rejects_extra_entries_and_side_effects();
void helper_semantic_alias_rejects_mutating_indirect_call_selectors();
void helper_semantic_alias_rejects_stale_certified_bodies();
void helper_semantic_alias_rejects_unproved_decimal_or_domain_claims();
void helper_semantic_alias_requires_x1_forgetting_continuations();
void helper_semantic_alias_rewrites_opaque_equivalent_helpers();
void dangerous_loading_size_report_tracks_flow_entry_stack();
void wumpus_size_report_tracks_split_entry_repeated_argument();
void dead_code_after_halt_matches_typescript_contract();
void dead_proc_elimination_matches_typescript_contract();
void dead_store_before_commutative_matches_typescript_contract();
void dead_store_elimination_matches_typescript_contract();
void display_byte_helpers_match_typescript_contract();
void display_lowering_helpers_match_typescript_contract();
void duplicate_failure_tail_matches_typescript_contract();
void emitter_matches_initial_typescript_contract();
void exact_decimal_arithmetic_matches_typescript_contract();
void emulator_bitmask_facts_match_typescript_contract();
void emulator_constants_dual_use_matches_typescript_contract();
void emulator_domain_error_guard_matches_typescript_contract();
void emulator_display_byte_facts_match_typescript_contract();
void emulator_fractional_r0_matches_typescript_contract();
void emulator_hex_arithmetic_facts_match_typescript_contract();
void emulator_function_equivalence_matches_typescript_contract();
void emulator_fl_counter_facts_match_typescript_contract();
void emulator_if_chain_dispatch_matches_typescript_contract();
void emulator_indirect_flow_equivalence_matches_typescript_contract();
void emulator_indirect_incdec_facts_match_typescript_contract();
void emulator_int_frac_shared_tail_matches_typescript_contract();
void emulator_interprocedural_equivalence_matches_typescript_contract();
void emulator_zagaday_tsifru_optimized_source_preserves_ui();
void emulator_log_selector_premise_matches_typescript_contract();
void emulator_mk61_execution_matches_typescript_contract();
void emulator_near_any_helper_matches_typescript_contract();
void emulator_number_entry_concat_matches_typescript_contract();
void emulator_indexed_packed_pow10_y_stack_semantics();
void emulator_packed_position_facts_match_typescript_contract();
void emulator_recall_side_effects_match_typescript_contract();
void emulator_regression_opcode_and_stop_flow_matches_typescript_contract();
void emulator_regression_example_loads(const std::string& example_file);
void emulator_regression_pending_optimizer_source_stays_before_loading(
    const std::string& source_file);
void emulator_regression_basic_scenario_matches_typescript_contract();
void emulator_regression_boot_scenarios_match_typescript_contract();
void emulator_regression_human_paths_match_typescript_contract();
void emulator_regression_tiny_game_drain_path_matches_typescript_contract();
void emulator_regression_stack_stop_risk_matches_typescript_contract();
void emulator_regression_cos_stack_stop_matches_typescript_contract();
void emulator_regression_resource_underflow_matches_typescript_contract();
void emulator_regression_wumpus_setup_matches_typescript_contract();
void emulator_regression_wumpus_arrow_exhaustion_matches_typescript_contract();
void emulator_rom_tables_match_typescript_contract();
void emulator_stack_dup_equivalence_matches_typescript_contract();
void emulator_stack_resident_equivalence_matches_typescript_contract();
void emulator_super_dark_matches_typescript_contract();
void emulator_vo_return_matches_typescript_contract();
void emulator_vp_splice_equivalence_matches_typescript_contract();
void emulator_x2_dead_restore_matches_typescript_contract();
void emulator_x2_restore_context_matches_typescript_contract();
void emulator_z_stack_derived_tail_matches_typescript_contract();
void example_sizes_match_typescript_baselines();
void compiler_examples_match_typescript_contract();
void supported_examples_match_native_oracles();
void expression_helpers_match_typescript_contract();
void expression_helper_size_report_counts_entry_y_materialization_coverage();
void expression_helper_size_report_tracks_selected_stack_carried_pow10_index();
void expression_lowering_helpers_match_typescript_contract();
void flow_structure_passes_match_typescript_contract();
void flow_x_reuse_matches_typescript_contract();
void formal_address_matches_typescript_contract();
void format_primitives_match_typescript_contract();
void golden_listing_contract_matches_typescript_contract();
void setup_formatting_matches_typescript_contract();
void functions_match_typescript_contract();
void indirect_flow_target_marker_requires_strict_boundary();
void indirect_addressing_matches_typescript_contract();
void indirect_selector_integer_part_matches_typescript_contract();
void int128_fallback_matches_builtin();
void inline_floor_packed_row_matches_typescript_contract();
void ir_round_trip_matches_typescript_contract();
void last_x_reuse_matches_typescript_contract();
void late_bound_decimal_selector_binds_only_proved_pairs();
void natural_target_component_layout_is_generic_and_proof_gated();
void shared_helper_wrapper_rewrites_only_proved_continuations();
void liveness_analysis_matches_typescript_contract();
void lowering_helpers_match_typescript_contract();
void machine_profile_matches_typescript_contract();
void match_blocks_match_typescript_contract();
void maxmin_zero_lint_matches_typescript_contract();
void mk61_trig_matches_emulator_contract();
void mk61_trig_calculate_matches_rom_values();
void opcode_catalog_matches_typescript_contract();
void oracle_index_loads_committed_artifacts();
void packed_counter_stripes_match_typescript_contract();
void sentinel_decimal_pack_matches_strategy_contract();
void packed_display_helpers_match_typescript_contract();
void packed_score_helpers_match_typescript_contract();
void parser_matches_initial_v2_source_contract();
void phase_selector_materializer_requires_proved_raw_phase_and_factor();
void expression_parser_matches_initial_contract();
void parser_accepts_all_example_sources();
void pass_pipeline_matches_initial_typescript_contract();
void post_layout_control_flow_matches_typed_contract();
void post_layout_indirect_flow_matches_typescript_contract();
void raw_bcd_unary_selector_matches_emulator_oracle();
void r0_fractional_sentinel_matches_typescript_contract();
void random_cell_helpers_match_typescript_contract();
void recall_removal_engine_matches_initial_typescript_contract();
void residual_elseif_matches_typescript_contract();
void residual_temp_matches_typescript_contract();
void return_stack_script_matches_mk61_strategy_contract();
void register_allocator_matches_typescript_contract();
void register_coalesce_matches_typescript_contract();
void rules_match_typescript_contract();
void safe_minmax_matches_typescript_contract();
void alternating_sign_toggle_arg_matches_literal_semantics();
void callee_hole_helper_matches_direct_call_semantics();
void segmented_bitplanes_match_typescript_contract();
void setup_program_matches_typescript_contract();
void terminal_report_tail_rewrites_only_with_explicit_proofs();
void terminal_cyclic_layout_derives_complete_proofs_transactionally();
void typed_indirect_facts_are_materialized_and_relocated();
void setup_only_counted_loop_matches_typescript_contract();
void store_recall_peephole_matches_typescript_contract();
void show_read_guarded_transfer_matches_typescript_contract();
void show_optimization_strategies_match_typescript_contract();
void show_sequence_helpers_match_typescript_contract();
void small_set_condition_lowering_matches_typescript_contract();
void spatial_helpers_match_typescript_contract();
void state_banks_match_typescript_contract();
void state_init_counted_loop_matches_typescript_contract();
void stack_residency_matches_typescript_contract();
void strict_allocation_matches_typescript_contract();
void style_lints_matches_typescript_contract();
void super_dark_layout_matches_typescript_contract();
void trig_fractional_pack_matches_strategy_contract();
void tic_tac_toe_4x4_manual_ui_contract_probe_matches_emulator();
void tic_tac_toe_4x4_reference_transcript_matches_original_listing();
void tic_tac_toe_4x4_reference_ui_normalizes_coordinates();
void tic_tac_toe_4x4_source_manual_ui_contract_is_explicit();
void tic_tac_toe_4x4_source_uses_reference_angle_mode();
void v2_const_matches_typescript_contract();
void x2_register_dataflow_matches_typescript_contract();
void x2_shape_data_model_matches_typescript_contract();
void x2_display_cluster_matches_typescript_contract();
void x2_structural_unary_matches_typescript_contract();
void x2_structural_bitwise_matches_typescript_contract();
void x2_structural_hex_binary_matches_typescript_contract();
void x2_stable_expression_matches_typescript_contract();
void x2_state_builders_matches_typescript_contract();
void x2_vp_splice_matches_typescript_contract();
void x2_vp_entry_matches_typescript_contract();
void x2_transfer_plain_matches_typescript_contract();
void x2_value_dataflow_matches_typescript_contract();
void x2_join_matches_typescript_contract();
void x2_noop_restore_matches_typescript_contract();
void x2_hidden_temp_restore_matches_typescript_contract();
void x2_literal_restore_matches_typescript_contract();
void vp_x2_peephole_matches_typescript_contract();
void vp_splice_matches_typescript_contract();
void pre_shift_stack_lift_matches_typescript_contract();

} // namespace mkpro::tests

namespace {

using TestFn = void (*)();

struct TestCase {
  std::string_view name;
  TestFn run;
};

template <typename Run> int run_named_test(std::string_view name, Run run) {
  const auto started = std::chrono::steady_clock::now();
  try {
    run();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    std::cout << "[PASS] " << name << " (" << elapsed.count() << " ms)" << std::endl;
    return 0;
  } catch (const std::exception& error) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    std::cerr << "[FAIL] " << name << " (" << elapsed.count() << " ms): " << error.what()
              << std::endl;
    return 1;
  }
}

bool matches_selector(std::string_view test_name, std::string_view exact, std::string_view filter) {
  if (!exact.empty() && test_name != exact) {
    return false;
  }
  if (!filter.empty() && test_name.find(filter) == std::string_view::npos) {
    return false;
  }
  return true;
}

} // namespace

// clang-format off
#define MKPRO_TEST(name) TestCase{#name, mkpro::tests::name}
// clang-format on

int main(int argc, char** argv) {
  constexpr std::array tests = {
      MKPRO_TEST(address_formula_solver_synthesizes_dispatch),
      MKPRO_TEST(address_formula_solver_verifies_static_proof_obligations),
      MKPRO_TEST(computed_dispatch_targets_survive_dead_code_elimination),
      MKPRO_TEST(computed_dispatch_discovery_keeps_program_correct),
      MKPRO_TEST(guarded_computed_dispatch_default_discovery_keeps_program_correct),
      MKPRO_TEST(optimizer_analysis_reports_sign_pack_and_address_formula_opportunities),
      MKPRO_TEST(optimizer_analysis_reports_decimal_and_multi_sign_pack_opportunities),
      MKPRO_TEST(state_packing_rewrite_options_affect_lowering),
      MKPRO_TEST(optimizer_static_proof_gate_rejects_unproved_dangerous_candidates),
      MKPRO_TEST(optimizer_translation_unit_stays_emulator_free),
      MKPRO_TEST(arithmetic_if_matches_typescript_contract),
      MKPRO_TEST(bit_mask_quotient_reuse_matches_typescript_contract),
      MKPRO_TEST(board_width_macros_matches_typescript_contract),
      MKPRO_TEST(emulator_grid_norm_signed_modulo_matches_language_contract),
      MKPRO_TEST(grid_norm_unary_wrapper_uses_generic_x_entry_and_forwarding),
      MKPRO_TEST(branch_target_x_reuse_matches_typescript_contract),
      MKPRO_TEST(cfg_matches_typescript_contract),
      MKPRO_TEST(compiler_coord_list_lowering_matches_typescript_contract),
      MKPRO_TEST(compiler_display_lowering_matches_typescript_contract),
      MKPRO_TEST(compiler_feature_profile_expanded_program_space_contract),
      MKPRO_TEST(compiler_feature_profile_raw_rf_contract),
      MKPRO_TEST(compiler_feature_profile_rf_extends_optimizer_preloads_contract),
      MKPRO_TEST(compiler_feature_profile_rf_extends_state_allocator_contract),
      MKPRO_TEST(compiler_feature_profile_rf_optimizer_is_size_monotonic_contract),
      MKPRO_TEST(compiler_lowers_initial_v2_subset),
      MKPRO_TEST(counted_loop_unroll_matches_typescript_contract),
      MKPRO_TEST(constant_folding_matches_typescript_contract),
      MKPRO_TEST(cse_display_block_matches_typescript_contract),
      MKPRO_TEST(cyclic_end_return_relocates_only_proved_direct_call_helpers),
      MKPRO_TEST(dark_side_suffix_helper_rewrites_only_proved_layouts),
      MKPRO_TEST(helper_invariant_recall_hoist_rewrites_only_proved_calls),
      MKPRO_TEST(helper_semantic_alias_compiler_composes_with_natural_target_layout),
      MKPRO_TEST(helper_semantic_alias_compiler_rejects_unproved_source_domains),
      MKPRO_TEST(helper_semantic_alias_call_origins_survive_return_suffix_merging),
      MKPRO_TEST(helper_semantic_alias_emulator_pins_x1_transfer_classes),
      MKPRO_TEST(helper_semantic_alias_rebinds_numeric_indirect_targets_by_identity),
      MKPRO_TEST(helper_semantic_alias_rejects_extra_entries_and_side_effects),
      MKPRO_TEST(helper_semantic_alias_rejects_mutating_indirect_call_selectors),
      MKPRO_TEST(helper_semantic_alias_rejects_stale_certified_bodies),
      MKPRO_TEST(helper_semantic_alias_rejects_unproved_decimal_or_domain_claims),
      MKPRO_TEST(helper_semantic_alias_requires_x1_forgetting_continuations),
      MKPRO_TEST(helper_semantic_alias_rewrites_opaque_equivalent_helpers),
      MKPRO_TEST(dangerous_loading_size_report_tracks_flow_entry_stack),
      MKPRO_TEST(wumpus_size_report_tracks_split_entry_repeated_argument),
      MKPRO_TEST(dead_code_after_halt_matches_typescript_contract),
      MKPRO_TEST(dead_proc_elimination_matches_typescript_contract),
      MKPRO_TEST(dead_store_before_commutative_matches_typescript_contract),
      MKPRO_TEST(dead_store_elimination_matches_typescript_contract),
      MKPRO_TEST(display_byte_helpers_match_typescript_contract),
      MKPRO_TEST(display_lowering_helpers_match_typescript_contract),
      MKPRO_TEST(duplicate_failure_tail_matches_typescript_contract),
      MKPRO_TEST(emitter_matches_initial_typescript_contract),
      MKPRO_TEST(exact_decimal_arithmetic_matches_typescript_contract),
      MKPRO_TEST(emulator_bitmask_facts_match_typescript_contract),
      MKPRO_TEST(emulator_constants_dual_use_matches_typescript_contract),
      MKPRO_TEST(emulator_domain_error_guard_matches_typescript_contract),
      MKPRO_TEST(emulator_display_byte_facts_match_typescript_contract),
      MKPRO_TEST(emulator_fractional_r0_matches_typescript_contract),
      MKPRO_TEST(emulator_hex_arithmetic_facts_match_typescript_contract),
      MKPRO_TEST(emulator_function_equivalence_matches_typescript_contract),
      MKPRO_TEST(emulator_fl_counter_facts_match_typescript_contract),
      MKPRO_TEST(emulator_if_chain_dispatch_matches_typescript_contract),
      MKPRO_TEST(emulator_indirect_flow_equivalence_matches_typescript_contract),
      MKPRO_TEST(emulator_indirect_incdec_facts_match_typescript_contract),
      MKPRO_TEST(emulator_int_frac_shared_tail_matches_typescript_contract),
      MKPRO_TEST(emulator_interprocedural_equivalence_matches_typescript_contract),
      MKPRO_TEST(emulator_zagaday_tsifru_optimized_source_preserves_ui),
      MKPRO_TEST(emulator_log_selector_premise_matches_typescript_contract),
      MKPRO_TEST(emulator_mk61_execution_matches_typescript_contract),
      MKPRO_TEST(emulator_near_any_helper_matches_typescript_contract),
      MKPRO_TEST(emulator_number_entry_concat_matches_typescript_contract),
      MKPRO_TEST(emulator_indexed_packed_pow10_y_stack_semantics),
      MKPRO_TEST(emulator_packed_position_facts_match_typescript_contract),
      MKPRO_TEST(emulator_recall_side_effects_match_typescript_contract),
      MKPRO_TEST(emulator_regression_opcode_and_stop_flow_matches_typescript_contract),
      MKPRO_TEST(emulator_regression_basic_scenario_matches_typescript_contract),
      MKPRO_TEST(emulator_regression_boot_scenarios_match_typescript_contract),
      MKPRO_TEST(emulator_regression_human_paths_match_typescript_contract),
      MKPRO_TEST(emulator_regression_tiny_game_drain_path_matches_typescript_contract),
      MKPRO_TEST(emulator_regression_stack_stop_risk_matches_typescript_contract),
      MKPRO_TEST(emulator_regression_cos_stack_stop_matches_typescript_contract),
      MKPRO_TEST(emulator_regression_resource_underflow_matches_typescript_contract),
      MKPRO_TEST(emulator_regression_wumpus_setup_matches_typescript_contract),
      MKPRO_TEST(emulator_regression_wumpus_arrow_exhaustion_matches_typescript_contract),
      MKPRO_TEST(emulator_rom_tables_match_typescript_contract),
      MKPRO_TEST(emulator_stack_dup_equivalence_matches_typescript_contract),
      MKPRO_TEST(emulator_stack_resident_equivalence_matches_typescript_contract),
      MKPRO_TEST(emulator_super_dark_matches_typescript_contract),
      MKPRO_TEST(emulator_vo_return_matches_typescript_contract),
      MKPRO_TEST(emulator_vp_splice_equivalence_matches_typescript_contract),
      MKPRO_TEST(emulator_x2_dead_restore_matches_typescript_contract),
      MKPRO_TEST(emulator_x2_restore_context_matches_typescript_contract),
      MKPRO_TEST(emulator_z_stack_derived_tail_matches_typescript_contract),
      MKPRO_TEST(example_sizes_match_typescript_baselines),
      MKPRO_TEST(compiler_examples_match_typescript_contract),
      MKPRO_TEST(supported_examples_match_native_oracles),
      MKPRO_TEST(expression_helpers_match_typescript_contract),
      MKPRO_TEST(expression_helper_size_report_counts_entry_y_materialization_coverage),
      MKPRO_TEST(expression_helper_size_report_tracks_selected_stack_carried_pow10_index),
      MKPRO_TEST(expression_lowering_helpers_match_typescript_contract),
      MKPRO_TEST(flow_structure_passes_match_typescript_contract),
      MKPRO_TEST(flow_x_reuse_matches_typescript_contract),
      MKPRO_TEST(formal_address_matches_typescript_contract),
      MKPRO_TEST(format_primitives_match_typescript_contract),
      MKPRO_TEST(golden_listing_contract_matches_typescript_contract),
      MKPRO_TEST(functions_match_typescript_contract),
      MKPRO_TEST(indirect_flow_target_marker_requires_strict_boundary),
      MKPRO_TEST(indirect_addressing_matches_typescript_contract),
      MKPRO_TEST(indirect_selector_integer_part_matches_typescript_contract),
      MKPRO_TEST(int128_fallback_matches_builtin),
      MKPRO_TEST(inline_floor_packed_row_matches_typescript_contract),
      MKPRO_TEST(ir_round_trip_matches_typescript_contract),
      MKPRO_TEST(last_x_reuse_matches_typescript_contract),
      MKPRO_TEST(late_bound_decimal_selector_binds_only_proved_pairs),
      MKPRO_TEST(natural_target_component_layout_is_generic_and_proof_gated),
      MKPRO_TEST(shared_helper_wrapper_rewrites_only_proved_continuations),
      MKPRO_TEST(liveness_analysis_matches_typescript_contract),
      MKPRO_TEST(lowering_helpers_match_typescript_contract),
      MKPRO_TEST(machine_profile_matches_typescript_contract),
      MKPRO_TEST(match_blocks_match_typescript_contract),
      MKPRO_TEST(maxmin_zero_lint_matches_typescript_contract),
      MKPRO_TEST(mk61_trig_matches_emulator_contract),
      MKPRO_TEST(mk61_trig_calculate_matches_rom_values),
      MKPRO_TEST(opcode_catalog_matches_typescript_contract),
      MKPRO_TEST(oracle_index_loads_committed_artifacts),
      MKPRO_TEST(packed_counter_stripes_match_typescript_contract),
      MKPRO_TEST(sentinel_decimal_pack_matches_strategy_contract),
      MKPRO_TEST(packed_display_helpers_match_typescript_contract),
      MKPRO_TEST(packed_score_helpers_match_typescript_contract),
      MKPRO_TEST(parser_matches_initial_v2_source_contract),
      MKPRO_TEST(phase_selector_materializer_requires_proved_raw_phase_and_factor),
      MKPRO_TEST(expression_parser_matches_initial_contract),
      MKPRO_TEST(parser_accepts_all_example_sources),
      MKPRO_TEST(pass_pipeline_matches_initial_typescript_contract),
      MKPRO_TEST(post_layout_control_flow_matches_typed_contract),
      MKPRO_TEST(post_layout_indirect_flow_matches_typescript_contract),
      MKPRO_TEST(raw_bcd_unary_selector_matches_emulator_oracle),
      MKPRO_TEST(r0_fractional_sentinel_matches_typescript_contract),
      MKPRO_TEST(random_cell_helpers_match_typescript_contract),
      MKPRO_TEST(recall_removal_engine_matches_initial_typescript_contract),
      MKPRO_TEST(residual_elseif_matches_typescript_contract),
      MKPRO_TEST(residual_temp_matches_typescript_contract),
      MKPRO_TEST(return_stack_script_matches_mk61_strategy_contract),
      MKPRO_TEST(register_allocator_matches_typescript_contract),
      MKPRO_TEST(register_coalesce_matches_typescript_contract),
      MKPRO_TEST(rules_match_typescript_contract),
      MKPRO_TEST(safe_minmax_matches_typescript_contract),
      MKPRO_TEST(alternating_sign_toggle_arg_matches_literal_semantics),
      MKPRO_TEST(callee_hole_helper_matches_direct_call_semantics),
      MKPRO_TEST(segmented_bitplanes_match_typescript_contract),
      MKPRO_TEST(setup_program_matches_typescript_contract),
      MKPRO_TEST(terminal_report_tail_rewrites_only_with_explicit_proofs),
      MKPRO_TEST(terminal_cyclic_layout_derives_complete_proofs_transactionally),
      MKPRO_TEST(typed_indirect_facts_are_materialized_and_relocated),
      MKPRO_TEST(setup_only_counted_loop_matches_typescript_contract),
      MKPRO_TEST(store_recall_peephole_matches_typescript_contract),
      MKPRO_TEST(show_optimization_strategies_match_typescript_contract),
      MKPRO_TEST(setup_formatting_matches_typescript_contract),
      MKPRO_TEST(show_read_guarded_transfer_matches_typescript_contract),
      MKPRO_TEST(show_sequence_helpers_match_typescript_contract),
      MKPRO_TEST(small_set_condition_lowering_matches_typescript_contract),
      MKPRO_TEST(spatial_helpers_match_typescript_contract),
      MKPRO_TEST(state_banks_match_typescript_contract),
      MKPRO_TEST(state_init_counted_loop_matches_typescript_contract),
      MKPRO_TEST(stack_residency_matches_typescript_contract),
      MKPRO_TEST(strict_allocation_matches_typescript_contract),
      MKPRO_TEST(style_lints_matches_typescript_contract),
      MKPRO_TEST(super_dark_layout_matches_typescript_contract),
      MKPRO_TEST(trig_fractional_pack_matches_strategy_contract),
      MKPRO_TEST(tic_tac_toe_4x4_manual_ui_contract_probe_matches_emulator),
      MKPRO_TEST(tic_tac_toe_4x4_reference_transcript_matches_original_listing),
      MKPRO_TEST(tic_tac_toe_4x4_reference_ui_normalizes_coordinates),
      MKPRO_TEST(tic_tac_toe_4x4_source_manual_ui_contract_is_explicit),
      MKPRO_TEST(tic_tac_toe_4x4_source_uses_reference_angle_mode),
      MKPRO_TEST(v2_const_matches_typescript_contract),
      MKPRO_TEST(x2_register_dataflow_matches_typescript_contract),
      MKPRO_TEST(x2_shape_data_model_matches_typescript_contract),
      MKPRO_TEST(x2_display_cluster_matches_typescript_contract),
      MKPRO_TEST(x2_structural_unary_matches_typescript_contract),
      MKPRO_TEST(x2_structural_bitwise_matches_typescript_contract),
      MKPRO_TEST(x2_structural_hex_binary_matches_typescript_contract),
      MKPRO_TEST(x2_stable_expression_matches_typescript_contract),
      MKPRO_TEST(x2_state_builders_matches_typescript_contract),
      MKPRO_TEST(x2_vp_splice_matches_typescript_contract),
      MKPRO_TEST(x2_vp_entry_matches_typescript_contract),
      MKPRO_TEST(x2_transfer_plain_matches_typescript_contract),
      MKPRO_TEST(x2_value_dataflow_matches_typescript_contract),
      MKPRO_TEST(x2_join_matches_typescript_contract),
      MKPRO_TEST(x2_noop_restore_matches_typescript_contract),
      MKPRO_TEST(x2_hidden_temp_restore_matches_typescript_contract),
      MKPRO_TEST(x2_literal_restore_matches_typescript_contract),
      MKPRO_TEST(vp_x2_peephole_matches_typescript_contract),
      MKPRO_TEST(vp_splice_matches_typescript_contract),
      MKPRO_TEST(pre_shift_stack_lift_matches_typescript_contract),
  };
#undef MKPRO_TEST

  std::string filter;
  std::string exact;
  std::string load_example;
  std::string pending_optimizer_source;
  bool list = false;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--list") {
      list = true;
      continue;
    }
    if (argument == "--exact") {
      if (i + 1 >= argc) {
        std::cerr << "--exact requires a test name" << std::endl;
        return 2;
      }
      exact = argv[++i];
      continue;
    }
    if (argument == "--load-example") {
      if (i + 1 >= argc) {
        std::cerr << "--load-example requires an example path" << std::endl;
        return 2;
      }
      load_example = argv[++i];
      continue;
    }
    if (argument == "--pending-optimizer-source") {
      if (i + 1 >= argc) {
        std::cerr << "--pending-optimizer-source requires a source path" << std::endl;
        return 2;
      }
      pending_optimizer_source = argv[++i];
      continue;
    }
    if (filter.empty()) {
      filter = argument;
      continue;
    }
    std::cerr << "Unexpected native test argument: " << argument << std::endl;
    return 2;
  }

  const int custom_modes =
      (load_example.empty() ? 0 : 1) + (pending_optimizer_source.empty() ? 0 : 1);
  if (custom_modes > 0) {
    if (custom_modes > 1 || list || !filter.empty() || !exact.empty()) {
      std::cerr << "Custom native test modes cannot be combined with other selectors" << std::endl;
      return 2;
    }

    const std::string name =
        !load_example.empty()
            ? "emulator_regression_example_loads:" + load_example
            : "emulator_regression_pending_optimizer_source_stays_before_loading:" +
                  pending_optimizer_source;
    return run_named_test(name, [&]() {
      if (!load_example.empty()) {
        mkpro::tests::emulator_regression_example_loads(load_example);
      } else {
        mkpro::tests::emulator_regression_pending_optimizer_source_stays_before_loading(
            pending_optimizer_source);
      }
    });
  }

  if (list) {
    for (const auto& test : tests)
      std::cout << test.name << std::endl;
    return 0;
  }

  int failed = 0;
  int selected = 0;
  const std::string_view exact_name{exact.data(), exact.size()};
  const std::string_view name_filter{filter.data(), filter.size()};
  for (const auto& test : tests) {
    if (!matches_selector(test.name, exact_name, name_filter)) {
      continue;
    }
    ++selected;
    failed += run_named_test(test.name, test.run);
  }

  if (selected == 0) {
    if (!exact.empty())
      std::cerr << "No native tests matched exact name: " << exact << std::endl;
    else
      std::cerr << "No native tests matched filter: " << filter << std::endl;
    return 2;
  }
  if (failed > 0) {
    std::cerr << failed << " native test(s) failed" << std::endl;
    return 1;
  }
  std::cout << selected << " native test(s) passed" << std::endl;
  return 0;
}
