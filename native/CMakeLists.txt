cmake_minimum_required(VERSION 3.25)

project(MKProNative VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

option(MKPRO_WARNINGS_AS_ERRORS "Treat native compiler warnings as errors" ON)
option(MKPRO_ENABLE_ASAN_UBSAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(MKPRO_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(MKPRO_STATIC "Statically link the CLI (GNU/Clang on non-Apple platforms)" OFF)
option(MKPRO_FORCE_INT128_FALLBACK "Use the software 128-bit integer even where a builtin __int128 exists" OFF)

if(MKPRO_ENABLE_ASAN_UBSAN AND MKPRO_ENABLE_TSAN)
  message(FATAL_ERROR "ASan/UBSan and TSan cannot be enabled in the same build")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(mkpro_core STATIC
  src/core/ast_json.cpp
  src/core/compiler.cpp
  src/core/constant_folder.cpp
  src/core/emit/lowering/coord_list.cpp
  src/core/emit/lowering/display.cpp
  src/core/emit/lowering/expr.cpp
  src/core/emit/lowering_helpers.cpp
  src/core/emit/machine_emitter.cpp
  src/core/emit/lowering/proc_raw_setup.cpp
  src/core/emit/stack_residency_analysis.cpp
  src/core/formal_address.cpp
  src/core/format.cpp
  src/core/indirect_addressing.cpp
  src/core/interprocedural_dse.cpp
  src/core/interprocedural_value_propagation.cpp
  src/core/ir.cpp
  src/core/machine_profile.cpp
  src/core/opcodes.cpp
  src/core/parser.cpp
  src/core/passes/arithmetic_if.cpp
  src/core/passes/branch_target_x_reuse.cpp
  src/core/passes/cfg.cpp
  src/core/passes/conditional_branch_trampoline.cpp
  src/core/passes/constant_folding.cpp
  src/core/passes/cse_display_block.cpp
  src/core/passes/dead_code_after_halt.cpp
  src/core/passes/dead_proc_elimination.cpp
  src/core/passes/dead_store_before_commutative.cpp
  src/core/passes/dead_store_elimination.cpp
  src/core/passes/duplicate_failure_tail.cpp
  src/core/passes/flow_x_reuse.cpp
  src/core/passes/helpers.cpp
  src/core/passes/indirect_addressing.cpp
  src/core/passes/indirect_selector_integer_part.cpp
  src/core/passes/index.cpp
  src/core/passes/jump_thread.cpp
  src/core/passes/jump_to_next.cpp
  src/core/passes/last_x_reuse.cpp
  src/core/passes/liveness_analysis.cpp
  src/core/passes/outline.cpp
  src/core/passes/pre_shift_stack_lift.cpp
  src/core/passes/preloaded_indirect_flow.cpp
  src/core/passes/r0_fractional_sentinel.cpp
  src/core/passes/recall_removal.cpp
  src/core/passes/register_coalesce.cpp
  src/core/passes/redundant_prologue.cpp
  src/core/passes/return_suffix_gadget.cpp
  src/core/passes/return_zero_jump.cpp
  src/core/passes/shared_call_tail.cpp
  src/core/passes/shared_straight_line_helper.cpp
  src/core/passes/shared_terminal_tail.cpp
  src/core/passes/store_recall_peephole.cpp
  src/core/passes/tail_branch_inversion.cpp
  src/core/passes/tail_call.cpp
  src/core/passes/vp_splice.cpp
  src/core/passes/vp_x2_peephole.cpp
  src/core/passes/x2_dead_restore_before_overwrite.cpp
  src/core/passes/x2_hidden_temp_restore.cpp
  src/core/passes/x2_literal_restore.cpp
  src/core/passes/x2_noop_restore.cpp
  src/core/post_layout_indirect_flow.cpp
  src/core/register_allocator.cpp
  src/core/return_stack_script.cpp
  src/core/rule_cfg.cpp
  src/core/rules.cpp
  src/core/state_banks.cpp
  src/core/super_dark_layout.cpp
  src/core/v2_const.cpp
  src/emulator/mk61.cpp
  src/emulator/rom.cpp
  src/oracle.cpp
  src/stub_compiler.cpp
)
target_include_directories(mkpro_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(mkpro_core PUBLIC cxx_std_23)

add_executable(mkpro_cli src/main.cpp)
set_target_properties(mkpro_cli PROPERTIES OUTPUT_NAME mkpro-native)
target_link_libraries(mkpro_cli PRIVATE mkpro_core)

add_executable(mkpro_tests
  tests/arithmetic_if_pass_test.cpp
  tests/bit_mask_quotient_reuse_test.cpp
  tests/board_width_macros_test.cpp
  tests/branch_target_x_reuse_pass_test.cpp
  tests/cfg_test.cpp
  tests/compiler_coord_list_test.cpp
  tests/examples_contract_test.cpp
  tests/compiler_display_test.cpp
  tests/compiler_test.cpp
  tests/counted_loop_unroll_test.cpp
  tests/example_sizes_test.cpp
  tests/constant_folding_test.cpp
  tests/cse_display_block_test.cpp
  tests/dead_code_after_halt_test.cpp
  tests/dead_proc_elimination_test.cpp
  tests/dead_store_before_commutative_test.cpp
  tests/dead_store_elimination_test.cpp
  tests/display_byte_helper_test.cpp
  tests/display_lowering_test.cpp
  tests/duplicate_failure_tail_test.cpp
  tests/emitter_test.cpp
  tests/exact_decimal_arithmetic_test.cpp
  tests/emulator_bitmask_facts_test.cpp
  tests/emulator_constants_dual_use_test.cpp
  tests/emulator_domain_error_guard_test.cpp
  tests/emulator_display_byte_test.cpp
  tests/emulator_fractional_r0_test.cpp
  tests/emulator_hex_arithmetic_facts_test.cpp
  tests/emulator_function_equivalence_test.cpp
  tests/emulator_fl_counter_test.cpp
  tests/emulator_if_chain_dispatch_test.cpp
  tests/emulator_indirect_flow_equivalence_test.cpp
  tests/emulator_indirect_incdec_test.cpp
  tests/emulator_int_frac_shared_tail_test.cpp
  tests/emulator_interprocedural_equivalence_test.cpp
  tests/emulator_log_selector_premise_test.cpp
  tests/emulator_mk61_test.cpp
  tests/emulator_near_any_helper_test.cpp
  tests/emulator_number_entry_concat_test.cpp
  tests/emulator_packed_position_test.cpp
  tests/emulator_recall_side_effects_test.cpp
  tests/emulator_regression_test.cpp
  tests/emulator_rom_test.cpp
  tests/emulator_stack_dup_test.cpp
  tests/emulator_stack_resident_equivalence_test.cpp
  tests/emulator_super_dark_test.cpp
  tests/emulator_vo_return_test.cpp
  tests/emulator_vp_splice_equivalence_test.cpp
  tests/emulator_x2_dead_restore_test.cpp
  tests/emulator_x2_restore_context_test.cpp
  tests/emulator_z_stack_derived_tail_test.cpp
  tests/example_parity_test.cpp
  tests/expression_helper_test.cpp
  tests/expression_lowering_test.cpp
  tests/flow_structure_passes_test.cpp
  tests/flow_x_reuse_pass_test.cpp
  tests/formal_address_test.cpp
  tests/format_test.cpp
  tests/functions_test.cpp
  tests/indirect_addressing_test.cpp
  tests/indirect_selector_integer_part_test.cpp
  tests/inline_floor_packed_row_test.cpp
  tests/ir_round_trip_test.cpp
  tests/golden_listing_contract_test.cpp
  tests/last_x_reuse_test.cpp
  tests/liveness_analysis_test.cpp
  tests/lowering_helpers_test.cpp
  tests/machine_profile_test.cpp
  tests/match_blocks_test.cpp
  tests/maxmin_zero_lint_test.cpp
  tests/opcodes_test.cpp
  tests/oracle_test.cpp
  tests/packed_counter_stripes_test.cpp
  tests/packed_display_helper_test.cpp
  tests/packed_score_helper_test.cpp
  tests/setup_format_contract_test.cpp
  tests/parser_test.cpp
  tests/passes_test.cpp
  tests/post_layout_indirect_flow_test.cpp
  tests/r0_fractional_sentinel_test.cpp
  tests/random_cell_helper_test.cpp
  tests/recall_removal_test.cpp
  tests/residual_elseif_test.cpp
  tests/residual_temp_test.cpp
  tests/return_stack_script_test.cpp
  tests/register_allocator_test.cpp
  tests/register_coalesce_pass_test.cpp
  tests/rules_test.cpp
  tests/safe_minmax_test.cpp
  tests/segmented_bitplane_test.cpp
  tests/setup_program_test.cpp
  tests/setup_only_counted_loop_test.cpp
  tests/store_recall_peephole_pass_test.cpp
  tests/show_read_guarded_transfer_test.cpp
  tests/show_sequence_helper_test.cpp
  tests/show_optimization_contract_test.cpp
  tests/small_set_condition_test.cpp
  tests/spatial_helpers_test.cpp
  tests/state_banks_test.cpp
  tests/state_init_counted_loop_test.cpp
  tests/stack_residency_test.cpp
  tests/int128_test.cpp
  tests/strict_allocation_test.cpp
  tests/style_lints_test.cpp
  tests/super_dark_layout_test.cpp
  tests/test_main.cpp
  tests/v2_const_test.cpp
  tests/x2_register_dataflow_test.cpp
  tests/x2_shape_data_model_test.cpp
  tests/x2_display_cluster_test.cpp
  tests/x2_structural_unary_test.cpp
  tests/x2_structural_bitwise_test.cpp
  tests/x2_structural_hex_binary_test.cpp
  tests/x2_stable_expression_test.cpp
  tests/x2_state_builders_test.cpp
  tests/x2_vp_splice_test.cpp
  tests/x2_vp_entry_test.cpp
  tests/x2_transfer_plain_test.cpp
  tests/x2_value_dataflow_test.cpp
  tests/x2_join_test.cpp
  tests/x2_noop_restore_test.cpp
  tests/x2_hidden_temp_restore_test.cpp
  tests/x2_literal_restore_test.cpp
  tests/vp_x2_peephole_test.cpp
  tests/vp_splice_pass_test.cpp
  tests/pre_shift_stack_lift_pass_test.cpp
)
target_link_libraries(mkpro_tests PRIVATE mkpro_core)
set(MKPRO_NATIVE_TEST_NAMES
  arithmetic_if_matches_typescript_contract
  bit_mask_quotient_reuse_matches_typescript_contract
  board_width_macros_matches_typescript_contract
  branch_target_x_reuse_matches_typescript_contract
  cfg_matches_typescript_contract
  compiler_coord_list_lowering_matches_typescript_contract
  compiler_display_lowering_matches_typescript_contract
  compiler_lowers_initial_v2_subset
  counted_loop_unroll_matches_typescript_contract
  constant_folding_matches_typescript_contract
  cse_display_block_matches_typescript_contract
  dead_code_after_halt_matches_typescript_contract
  dead_proc_elimination_matches_typescript_contract
  dead_store_before_commutative_matches_typescript_contract
  dead_store_elimination_matches_typescript_contract
  display_byte_helpers_match_typescript_contract
  display_lowering_helpers_match_typescript_contract
  duplicate_failure_tail_matches_typescript_contract
  emitter_matches_initial_typescript_contract
  exact_decimal_arithmetic_matches_typescript_contract
  emulator_bitmask_facts_match_typescript_contract
  emulator_constants_dual_use_matches_typescript_contract
  emulator_domain_error_guard_matches_typescript_contract
  emulator_display_byte_facts_match_typescript_contract
  emulator_fractional_r0_matches_typescript_contract
  emulator_hex_arithmetic_facts_match_typescript_contract
  emulator_function_equivalence_matches_typescript_contract
  emulator_fl_counter_facts_match_typescript_contract
  emulator_if_chain_dispatch_matches_typescript_contract
  emulator_indirect_flow_equivalence_matches_typescript_contract
  emulator_indirect_incdec_facts_match_typescript_contract
  emulator_int_frac_shared_tail_matches_typescript_contract
  emulator_interprocedural_equivalence_matches_typescript_contract
  emulator_log_selector_premise_matches_typescript_contract
  emulator_mk61_execution_matches_typescript_contract
  emulator_near_any_helper_matches_typescript_contract
  emulator_number_entry_concat_matches_typescript_contract
  emulator_packed_position_facts_match_typescript_contract
  emulator_recall_side_effects_match_typescript_contract
  emulator_regression_matches_typescript_contract
  emulator_rom_tables_match_typescript_contract
  emulator_stack_dup_equivalence_matches_typescript_contract
  emulator_stack_resident_equivalence_matches_typescript_contract
  emulator_super_dark_matches_typescript_contract
  emulator_vo_return_matches_typescript_contract
  emulator_vp_splice_equivalence_matches_typescript_contract
  emulator_x2_dead_restore_matches_typescript_contract
  emulator_x2_restore_context_matches_typescript_contract
  emulator_z_stack_derived_tail_matches_typescript_contract
  example_sizes_match_typescript_baselines
  compiler_examples_match_typescript_contract
  supported_examples_match_native_oracles
  expression_helpers_match_typescript_contract
  expression_lowering_helpers_match_typescript_contract
  flow_structure_passes_match_typescript_contract
  flow_x_reuse_matches_typescript_contract
  formal_address_matches_typescript_contract
  format_primitives_match_typescript_contract
  golden_listing_contract_matches_typescript_contract
  functions_match_typescript_contract
  indirect_addressing_matches_typescript_contract
  indirect_selector_integer_part_matches_typescript_contract
  inline_floor_packed_row_matches_typescript_contract
  ir_round_trip_matches_typescript_contract
  last_x_reuse_matches_typescript_contract
  liveness_analysis_matches_typescript_contract
  lowering_helpers_match_typescript_contract
  machine_profile_matches_typescript_contract
  match_blocks_match_typescript_contract
  maxmin_zero_lint_matches_typescript_contract
  opcode_catalog_matches_typescript_contract
  oracle_index_loads_committed_artifacts
  packed_counter_stripes_match_typescript_contract
  packed_display_helpers_match_typescript_contract
  packed_score_helpers_match_typescript_contract
  parser_matches_initial_v2_source_contract
  expression_parser_matches_initial_contract
  parser_accepts_all_example_sources
  pass_pipeline_matches_initial_typescript_contract
  post_layout_indirect_flow_matches_typescript_contract
  r0_fractional_sentinel_matches_typescript_contract
  random_cell_helpers_match_typescript_contract
  recall_removal_engine_matches_initial_typescript_contract
  residual_elseif_matches_typescript_contract
  residual_temp_matches_typescript_contract
  register_allocator_matches_typescript_contract
  register_coalesce_matches_typescript_contract
  rules_match_typescript_contract
  safe_minmax_matches_typescript_contract
  segmented_bitplanes_match_typescript_contract
  setup_program_matches_typescript_contract
  setup_only_counted_loop_matches_typescript_contract
  store_recall_peephole_matches_typescript_contract
  show_optimization_strategies_match_typescript_contract
  setup_formatting_matches_typescript_contract
  show_read_guarded_transfer_matches_typescript_contract
  show_sequence_helpers_match_typescript_contract
  small_set_condition_lowering_matches_typescript_contract
  spatial_helpers_match_typescript_contract
  state_banks_match_typescript_contract
  state_init_counted_loop_matches_typescript_contract
  stack_residency_matches_typescript_contract
  int128_fallback_matches_builtin
  strict_allocation_matches_typescript_contract
  style_lints_matches_typescript_contract
  super_dark_layout_matches_typescript_contract
  v2_const_matches_typescript_contract
  x2_register_dataflow_matches_typescript_contract
  x2_shape_data_model_matches_typescript_contract
  x2_display_cluster_matches_typescript_contract
  x2_structural_unary_matches_typescript_contract
  x2_structural_bitwise_matches_typescript_contract
  x2_structural_hex_binary_matches_typescript_contract
  x2_stable_expression_matches_typescript_contract
  x2_state_builders_matches_typescript_contract
  x2_vp_splice_matches_typescript_contract
  x2_vp_entry_matches_typescript_contract
  x2_transfer_plain_matches_typescript_contract
  x2_value_dataflow_matches_typescript_contract
  x2_join_matches_typescript_contract
  x2_noop_restore_matches_typescript_contract
  x2_hidden_temp_restore_matches_typescript_contract
  x2_literal_restore_matches_typescript_contract
  vp_x2_peephole_matches_typescript_contract
  vp_splice_matches_typescript_contract
  pre_shift_stack_lift_matches_typescript_contract
)
foreach(test_name IN LISTS MKPRO_NATIVE_TEST_NAMES)
  add_test(NAME "mkpro.${test_name}" COMMAND mkpro_tests --exact "${test_name}")
  set_tests_properties("mkpro.${test_name}" PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()

function(mkpro_configure_target target_name)
  if(MKPRO_FORCE_INT128_FALLBACK)
    target_compile_definitions(${target_name} PRIVATE MKPRO_FORCE_INT128_FALLBACK)
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    target_compile_options(${target_name} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wsign-conversion
      -Wshadow
    )
    if(MKPRO_WARNINGS_AS_ERRORS)
      target_compile_options(${target_name} PRIVATE -Werror)
    endif()
    if(MKPRO_ENABLE_ASAN_UBSAN)
      target_compile_options(${target_name} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${target_name} PRIVATE -fsanitize=address,undefined)
    endif()
    if(MKPRO_ENABLE_TSAN)
      target_compile_options(${target_name} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
      target_link_options(${target_name} PRIVATE -fsanitize=thread)
    endif()
    # Apple does not support statically linking the system C library, so -static
    # is only applied on other Unix-like platforms. macOS portability is handled
    # via CMAKE_OSX_DEPLOYMENT_TARGET / CMAKE_OSX_ARCHITECTURES instead.
    if(MKPRO_STATIC AND NOT APPLE)
      target_link_options(${target_name} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
  endif()
endfunction()

add_executable(mkpro_int128_selftest tests/int128_selftest_main.cpp)
target_include_directories(mkpro_int128_selftest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
add_test(NAME int128_selftest COMMAND mkpro_int128_selftest)

mkpro_configure_target(mkpro_core)
mkpro_configure_target(mkpro_cli)
mkpro_configure_target(mkpro_tests)
mkpro_configure_target(mkpro_int128_selftest)
