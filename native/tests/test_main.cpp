#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

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
void branch_target_x_reuse_matches_typescript_contract();
void cfg_matches_typescript_contract();
void compiler_coord_list_lowering_matches_typescript_contract();
void compiler_display_lowering_matches_typescript_contract();
void compiler_lowers_initial_v2_subset();
void counted_loop_unroll_matches_typescript_contract();
void constant_folding_matches_typescript_contract();
void cse_display_block_matches_typescript_contract();
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
void emulator_log_selector_premise_matches_typescript_contract();
void emulator_mk61_execution_matches_typescript_contract();
void emulator_near_any_helper_matches_typescript_contract();
void emulator_number_entry_concat_matches_typescript_contract();
void emulator_packed_position_facts_match_typescript_contract();
void emulator_recall_side_effects_match_typescript_contract();
void emulator_regression_matches_typescript_contract();
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
void expression_parser_matches_initial_contract();
void parser_accepts_all_example_sources();
void pass_pipeline_matches_initial_typescript_contract();
void post_layout_indirect_flow_matches_typescript_contract();
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
void segmented_bitplanes_match_typescript_contract();
void setup_program_matches_typescript_contract();
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

using TestFn = std::function<void()>;

struct TestCase {
  std::string name;
  TestFn run;
};

} // namespace

int main(int argc, char** argv) {
  const std::vector<TestCase> tests = {
      {"address_formula_solver_synthesizes_dispatch",
       mkpro::tests::address_formula_solver_synthesizes_dispatch},
      {"address_formula_solver_verifies_static_proof_obligations",
       mkpro::tests::address_formula_solver_verifies_static_proof_obligations},
      {"computed_dispatch_targets_survive_dead_code_elimination",
       mkpro::tests::computed_dispatch_targets_survive_dead_code_elimination},
      {"computed_dispatch_discovery_keeps_program_correct",
       mkpro::tests::computed_dispatch_discovery_keeps_program_correct},
      {"guarded_computed_dispatch_default_discovery_keeps_program_correct",
       mkpro::tests::guarded_computed_dispatch_default_discovery_keeps_program_correct},
      {"optimizer_analysis_reports_sign_pack_and_address_formula_opportunities",
       mkpro::tests::optimizer_analysis_reports_sign_pack_and_address_formula_opportunities},
      {"optimizer_analysis_reports_decimal_and_multi_sign_pack_opportunities",
       mkpro::tests::optimizer_analysis_reports_decimal_and_multi_sign_pack_opportunities},
      {"state_packing_rewrite_options_affect_lowering",
       mkpro::tests::state_packing_rewrite_options_affect_lowering},
      {"optimizer_static_proof_gate_rejects_unproved_dangerous_candidates",
       mkpro::tests::optimizer_static_proof_gate_rejects_unproved_dangerous_candidates},
      {"optimizer_translation_unit_stays_emulator_free",
       mkpro::tests::optimizer_translation_unit_stays_emulator_free},
      {"arithmetic_if_matches_typescript_contract",
       mkpro::tests::arithmetic_if_matches_typescript_contract},
      {"bit_mask_quotient_reuse_matches_typescript_contract",
       mkpro::tests::bit_mask_quotient_reuse_matches_typescript_contract},
      {"board_width_macros_matches_typescript_contract",
       mkpro::tests::board_width_macros_matches_typescript_contract},
      {"branch_target_x_reuse_matches_typescript_contract",
       mkpro::tests::branch_target_x_reuse_matches_typescript_contract},
      {"cfg_matches_typescript_contract", mkpro::tests::cfg_matches_typescript_contract},
      {"compiler_coord_list_lowering_matches_typescript_contract",
       mkpro::tests::compiler_coord_list_lowering_matches_typescript_contract},
      {"compiler_display_lowering_matches_typescript_contract",
       mkpro::tests::compiler_display_lowering_matches_typescript_contract},
      {"compiler_lowers_initial_v2_subset", mkpro::tests::compiler_lowers_initial_v2_subset},
      {"counted_loop_unroll_matches_typescript_contract",
       mkpro::tests::counted_loop_unroll_matches_typescript_contract},
      {"constant_folding_matches_typescript_contract",
       mkpro::tests::constant_folding_matches_typescript_contract},
      {"cse_display_block_matches_typescript_contract",
       mkpro::tests::cse_display_block_matches_typescript_contract},
      {"dead_code_after_halt_matches_typescript_contract",
       mkpro::tests::dead_code_after_halt_matches_typescript_contract},
      {"dead_proc_elimination_matches_typescript_contract",
       mkpro::tests::dead_proc_elimination_matches_typescript_contract},
      {"dead_store_before_commutative_matches_typescript_contract",
       mkpro::tests::dead_store_before_commutative_matches_typescript_contract},
      {"dead_store_elimination_matches_typescript_contract",
       mkpro::tests::dead_store_elimination_matches_typescript_contract},
      {"display_byte_helpers_match_typescript_contract",
       mkpro::tests::display_byte_helpers_match_typescript_contract},
      {"display_lowering_helpers_match_typescript_contract",
       mkpro::tests::display_lowering_helpers_match_typescript_contract},
      {"duplicate_failure_tail_matches_typescript_contract",
       mkpro::tests::duplicate_failure_tail_matches_typescript_contract},
      {"emitter_matches_initial_typescript_contract",
       mkpro::tests::emitter_matches_initial_typescript_contract},
      {"exact_decimal_arithmetic_matches_typescript_contract",
       mkpro::tests::exact_decimal_arithmetic_matches_typescript_contract},
      {"emulator_bitmask_facts_match_typescript_contract",
       mkpro::tests::emulator_bitmask_facts_match_typescript_contract},
      {"emulator_constants_dual_use_matches_typescript_contract",
       mkpro::tests::emulator_constants_dual_use_matches_typescript_contract},
      {"emulator_domain_error_guard_matches_typescript_contract",
       mkpro::tests::emulator_domain_error_guard_matches_typescript_contract},
      {"emulator_display_byte_facts_match_typescript_contract",
       mkpro::tests::emulator_display_byte_facts_match_typescript_contract},
      {"emulator_fractional_r0_matches_typescript_contract",
       mkpro::tests::emulator_fractional_r0_matches_typescript_contract},
      {"emulator_hex_arithmetic_facts_match_typescript_contract",
       mkpro::tests::emulator_hex_arithmetic_facts_match_typescript_contract},
      {"emulator_function_equivalence_matches_typescript_contract",
       mkpro::tests::emulator_function_equivalence_matches_typescript_contract},
      {"emulator_fl_counter_facts_match_typescript_contract",
       mkpro::tests::emulator_fl_counter_facts_match_typescript_contract},
      {"emulator_if_chain_dispatch_matches_typescript_contract",
       mkpro::tests::emulator_if_chain_dispatch_matches_typescript_contract},
      {"emulator_indirect_flow_equivalence_matches_typescript_contract",
       mkpro::tests::emulator_indirect_flow_equivalence_matches_typescript_contract},
      {"emulator_indirect_incdec_facts_match_typescript_contract",
       mkpro::tests::emulator_indirect_incdec_facts_match_typescript_contract},
      {"emulator_int_frac_shared_tail_matches_typescript_contract",
       mkpro::tests::emulator_int_frac_shared_tail_matches_typescript_contract},
      {"emulator_interprocedural_equivalence_matches_typescript_contract",
       mkpro::tests::emulator_interprocedural_equivalence_matches_typescript_contract},
      {"emulator_log_selector_premise_matches_typescript_contract",
       mkpro::tests::emulator_log_selector_premise_matches_typescript_contract},
      {"emulator_mk61_execution_matches_typescript_contract",
       mkpro::tests::emulator_mk61_execution_matches_typescript_contract},
      {"emulator_near_any_helper_matches_typescript_contract",
       mkpro::tests::emulator_near_any_helper_matches_typescript_contract},
      {"emulator_number_entry_concat_matches_typescript_contract",
       mkpro::tests::emulator_number_entry_concat_matches_typescript_contract},
      {"emulator_packed_position_facts_match_typescript_contract",
       mkpro::tests::emulator_packed_position_facts_match_typescript_contract},
      {"emulator_recall_side_effects_match_typescript_contract",
       mkpro::tests::emulator_recall_side_effects_match_typescript_contract},
      {"emulator_regression_matches_typescript_contract",
       mkpro::tests::emulator_regression_matches_typescript_contract},
      {"emulator_rom_tables_match_typescript_contract",
       mkpro::tests::emulator_rom_tables_match_typescript_contract},
      {"emulator_stack_dup_equivalence_matches_typescript_contract",
       mkpro::tests::emulator_stack_dup_equivalence_matches_typescript_contract},
      {"emulator_stack_resident_equivalence_matches_typescript_contract",
       mkpro::tests::emulator_stack_resident_equivalence_matches_typescript_contract},
      {"emulator_super_dark_matches_typescript_contract",
       mkpro::tests::emulator_super_dark_matches_typescript_contract},
      {"emulator_vo_return_matches_typescript_contract",
       mkpro::tests::emulator_vo_return_matches_typescript_contract},
      {"emulator_vp_splice_equivalence_matches_typescript_contract",
       mkpro::tests::emulator_vp_splice_equivalence_matches_typescript_contract},
      {"emulator_x2_dead_restore_matches_typescript_contract",
       mkpro::tests::emulator_x2_dead_restore_matches_typescript_contract},
      {"emulator_x2_restore_context_matches_typescript_contract",
       mkpro::tests::emulator_x2_restore_context_matches_typescript_contract},
      {"emulator_z_stack_derived_tail_matches_typescript_contract",
       mkpro::tests::emulator_z_stack_derived_tail_matches_typescript_contract},
      {"example_sizes_match_typescript_baselines",
       mkpro::tests::example_sizes_match_typescript_baselines},
      {"compiler_examples_match_typescript_contract",
       mkpro::tests::compiler_examples_match_typescript_contract},
      {"supported_examples_match_native_oracles",
       mkpro::tests::supported_examples_match_native_oracles},
      {"expression_helpers_match_typescript_contract",
       mkpro::tests::expression_helpers_match_typescript_contract},
      {"expression_lowering_helpers_match_typescript_contract",
       mkpro::tests::expression_lowering_helpers_match_typescript_contract},
      {"flow_structure_passes_match_typescript_contract",
       mkpro::tests::flow_structure_passes_match_typescript_contract},
      {"flow_x_reuse_matches_typescript_contract",
       mkpro::tests::flow_x_reuse_matches_typescript_contract},
      {"formal_address_matches_typescript_contract",
       mkpro::tests::formal_address_matches_typescript_contract},
      {"format_primitives_match_typescript_contract",
       mkpro::tests::format_primitives_match_typescript_contract},
      {"golden_listing_contract_matches_typescript_contract",
       mkpro::tests::golden_listing_contract_matches_typescript_contract},
      {"functions_match_typescript_contract", mkpro::tests::functions_match_typescript_contract},
      {"indirect_flow_target_marker_requires_strict_boundary",
       mkpro::tests::indirect_flow_target_marker_requires_strict_boundary},
      {"indirect_addressing_matches_typescript_contract",
       mkpro::tests::indirect_addressing_matches_typescript_contract},
      {"indirect_selector_integer_part_matches_typescript_contract",
       mkpro::tests::indirect_selector_integer_part_matches_typescript_contract},
      {"int128_fallback_matches_builtin", mkpro::tests::int128_fallback_matches_builtin},
      {"inline_floor_packed_row_matches_typescript_contract",
       mkpro::tests::inline_floor_packed_row_matches_typescript_contract},
      {"ir_round_trip_matches_typescript_contract",
       mkpro::tests::ir_round_trip_matches_typescript_contract},
      {"last_x_reuse_matches_typescript_contract",
       mkpro::tests::last_x_reuse_matches_typescript_contract},
      {"liveness_analysis_matches_typescript_contract",
       mkpro::tests::liveness_analysis_matches_typescript_contract},
      {"lowering_helpers_match_typescript_contract",
       mkpro::tests::lowering_helpers_match_typescript_contract},
      {"machine_profile_matches_typescript_contract",
       mkpro::tests::machine_profile_matches_typescript_contract},
      {"match_blocks_match_typescript_contract",
       mkpro::tests::match_blocks_match_typescript_contract},
      {"maxmin_zero_lint_matches_typescript_contract",
       mkpro::tests::maxmin_zero_lint_matches_typescript_contract},
      {"mk61_trig_matches_emulator_contract",
       mkpro::tests::mk61_trig_matches_emulator_contract},
      {"mk61_trig_calculate_matches_rom_values",
       mkpro::tests::mk61_trig_calculate_matches_rom_values},
      {"opcode_catalog_matches_typescript_contract",
       mkpro::tests::opcode_catalog_matches_typescript_contract},
      {"oracle_index_loads_committed_artifacts",
       mkpro::tests::oracle_index_loads_committed_artifacts},
      {"packed_counter_stripes_match_typescript_contract",
       mkpro::tests::packed_counter_stripes_match_typescript_contract},
      {"sentinel_decimal_pack_matches_strategy_contract",
       mkpro::tests::sentinel_decimal_pack_matches_strategy_contract},
      {"packed_display_helpers_match_typescript_contract",
       mkpro::tests::packed_display_helpers_match_typescript_contract},
      {"packed_score_helpers_match_typescript_contract",
       mkpro::tests::packed_score_helpers_match_typescript_contract},
      {"parser_matches_initial_v2_source_contract",
       mkpro::tests::parser_matches_initial_v2_source_contract},
      {"expression_parser_matches_initial_contract",
       mkpro::tests::expression_parser_matches_initial_contract},
      {"parser_accepts_all_example_sources", mkpro::tests::parser_accepts_all_example_sources},
      {"pass_pipeline_matches_initial_typescript_contract",
       mkpro::tests::pass_pipeline_matches_initial_typescript_contract},
      {"post_layout_indirect_flow_matches_typescript_contract",
       mkpro::tests::post_layout_indirect_flow_matches_typescript_contract},
      {"r0_fractional_sentinel_matches_typescript_contract",
       mkpro::tests::r0_fractional_sentinel_matches_typescript_contract},
      {"random_cell_helpers_match_typescript_contract",
       mkpro::tests::random_cell_helpers_match_typescript_contract},
      {"recall_removal_engine_matches_initial_typescript_contract",
       mkpro::tests::recall_removal_engine_matches_initial_typescript_contract},
      {"residual_elseif_matches_typescript_contract",
       mkpro::tests::residual_elseif_matches_typescript_contract},
      {"residual_temp_matches_typescript_contract",
       mkpro::tests::residual_temp_matches_typescript_contract},
      {"return_stack_script_matches_mk61_strategy_contract",
       mkpro::tests::return_stack_script_matches_mk61_strategy_contract},
      {"register_allocator_matches_typescript_contract",
       mkpro::tests::register_allocator_matches_typescript_contract},
      {"register_coalesce_matches_typescript_contract",
       mkpro::tests::register_coalesce_matches_typescript_contract},
      {"rules_match_typescript_contract", mkpro::tests::rules_match_typescript_contract},
      {"safe_minmax_matches_typescript_contract",
       mkpro::tests::safe_minmax_matches_typescript_contract},
      {"segmented_bitplanes_match_typescript_contract",
       mkpro::tests::segmented_bitplanes_match_typescript_contract},
      {"setup_program_matches_typescript_contract",
       mkpro::tests::setup_program_matches_typescript_contract},
      {"setup_only_counted_loop_matches_typescript_contract",
       mkpro::tests::setup_only_counted_loop_matches_typescript_contract},
      {"store_recall_peephole_matches_typescript_contract",
       mkpro::tests::store_recall_peephole_matches_typescript_contract},
      {"show_optimization_strategies_match_typescript_contract",
       mkpro::tests::show_optimization_strategies_match_typescript_contract},
      {"setup_formatting_matches_typescript_contract",
       mkpro::tests::setup_formatting_matches_typescript_contract},
      {"show_read_guarded_transfer_matches_typescript_contract",
       mkpro::tests::show_read_guarded_transfer_matches_typescript_contract},
      {"show_sequence_helpers_match_typescript_contract",
       mkpro::tests::show_sequence_helpers_match_typescript_contract},
      {"small_set_condition_lowering_matches_typescript_contract",
       mkpro::tests::small_set_condition_lowering_matches_typescript_contract},
      {"spatial_helpers_match_typescript_contract",
       mkpro::tests::spatial_helpers_match_typescript_contract},
      {"state_banks_match_typescript_contract",
       mkpro::tests::state_banks_match_typescript_contract},
      {"state_init_counted_loop_matches_typescript_contract",
       mkpro::tests::state_init_counted_loop_matches_typescript_contract},
      {"stack_residency_matches_typescript_contract",
       mkpro::tests::stack_residency_matches_typescript_contract},
      {"strict_allocation_matches_typescript_contract",
       mkpro::tests::strict_allocation_matches_typescript_contract},
      {"style_lints_matches_typescript_contract",
       mkpro::tests::style_lints_matches_typescript_contract},
      {"super_dark_layout_matches_typescript_contract",
       mkpro::tests::super_dark_layout_matches_typescript_contract},
      {"trig_fractional_pack_matches_strategy_contract",
       mkpro::tests::trig_fractional_pack_matches_strategy_contract},
      {"v2_const_matches_typescript_contract", mkpro::tests::v2_const_matches_typescript_contract},
      {"x2_register_dataflow_matches_typescript_contract",
       mkpro::tests::x2_register_dataflow_matches_typescript_contract},
      {"x2_shape_data_model_matches_typescript_contract",
       mkpro::tests::x2_shape_data_model_matches_typescript_contract},
      {"x2_display_cluster_matches_typescript_contract",
       mkpro::tests::x2_display_cluster_matches_typescript_contract},
      {"x2_structural_unary_matches_typescript_contract",
       mkpro::tests::x2_structural_unary_matches_typescript_contract},
      {"x2_structural_bitwise_matches_typescript_contract",
       mkpro::tests::x2_structural_bitwise_matches_typescript_contract},
      {"x2_structural_hex_binary_matches_typescript_contract",
       mkpro::tests::x2_structural_hex_binary_matches_typescript_contract},
      {"x2_stable_expression_matches_typescript_contract",
       mkpro::tests::x2_stable_expression_matches_typescript_contract},
      {"x2_state_builders_matches_typescript_contract",
       mkpro::tests::x2_state_builders_matches_typescript_contract},
      {"x2_vp_splice_matches_typescript_contract",
       mkpro::tests::x2_vp_splice_matches_typescript_contract},
      {"x2_vp_entry_matches_typescript_contract",
       mkpro::tests::x2_vp_entry_matches_typescript_contract},
      {"x2_transfer_plain_matches_typescript_contract",
       mkpro::tests::x2_transfer_plain_matches_typescript_contract},
      {"x2_value_dataflow_matches_typescript_contract",
       mkpro::tests::x2_value_dataflow_matches_typescript_contract},
      {"x2_join_matches_typescript_contract",
       mkpro::tests::x2_join_matches_typescript_contract},
      {"x2_noop_restore_matches_typescript_contract",
       mkpro::tests::x2_noop_restore_matches_typescript_contract},
      {"x2_hidden_temp_restore_matches_typescript_contract",
       mkpro::tests::x2_hidden_temp_restore_matches_typescript_contract},
      {"x2_literal_restore_matches_typescript_contract",
       mkpro::tests::x2_literal_restore_matches_typescript_contract},
      {"vp_x2_peephole_matches_typescript_contract",
       mkpro::tests::vp_x2_peephole_matches_typescript_contract},
      {"vp_splice_matches_typescript_contract",
       mkpro::tests::vp_splice_matches_typescript_contract},
      {"pre_shift_stack_lift_matches_typescript_contract",
       mkpro::tests::pre_shift_stack_lift_matches_typescript_contract},
  };

  std::string filter;
  std::string exact;
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
    if (filter.empty()) {
      filter = argument;
      continue;
    }
    std::cerr << "Unexpected native test argument: " << argument << std::endl;
    return 2;
  }

  if (list) {
    for (const auto& test : tests)
      std::cout << test.name << std::endl;
    return 0;
  }

  int failed = 0;
  int selected = 0;
  for (const auto& test : tests) {
    if (!exact.empty() && test.name != exact) {
      continue;
    }
    if (!filter.empty() && test.name.find(filter) == std::string::npos) {
      continue;
    }
    ++selected;
    const auto started = std::chrono::steady_clock::now();
    try {
      test.run();
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started);
      std::cout << "[PASS] " << test.name << " (" << elapsed.count() << " ms)"
                << std::endl;
    } catch (const std::exception& error) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started);
      ++failed;
      std::cerr << "[FAIL] " << test.name << " (" << elapsed.count() << " ms): "
                << error.what() << std::endl;
    }
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
