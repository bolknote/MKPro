cmake_minimum_required(VERSION 3.25)

project(MKProNative VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

option(MKPRO_WARNINGS_AS_ERRORS "Treat native compiler warnings as errors" ON)
option(MKPRO_ENABLE_ASAN_UBSAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(MKPRO_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

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
  src/core/rule_cfg.cpp
  src/core/rules.cpp
  src/core/state_banks.cpp
  src/core/super_dark_layout.cpp
  src/core/v2_const.cpp
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
  tests/example_parity_test.cpp
  tests/expression_helper_test.cpp
  tests/expression_lowering_test.cpp
  tests/formal_address_test.cpp
  tests/format_test.cpp
  tests/functions_test.cpp
  tests/indirect_addressing_test.cpp
  tests/indirect_selector_integer_part_test.cpp
  tests/inline_floor_packed_row_test.cpp
  tests/ir_round_trip_test.cpp
  tests/golden_listing_contract_test.cpp
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
  tests/register_allocator_test.cpp
  tests/register_coalesce_pass_test.cpp
  tests/rules_test.cpp
  tests/safe_minmax_test.cpp
  tests/segmented_bitplane_test.cpp
  tests/setup_program_test.cpp
  tests/setup_only_counted_loop_test.cpp
  tests/show_read_guarded_transfer_test.cpp
  tests/show_sequence_helper_test.cpp
  tests/show_optimization_contract_test.cpp
  tests/small_set_condition_test.cpp
  tests/spatial_helpers_test.cpp
  tests/state_banks_test.cpp
  tests/state_init_counted_loop_test.cpp
  tests/stack_residency_test.cpp
  tests/strict_allocation_test.cpp
  tests/style_lints_test.cpp
  tests/super_dark_layout_test.cpp
  tests/test_main.cpp
  tests/v2_const_test.cpp
  tests/x2_register_dataflow_test.cpp
)
target_link_libraries(mkpro_tests PRIVATE mkpro_core)
add_test(NAME mkpro_tests COMMAND mkpro_tests)
set_tests_properties(mkpro_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

function(mkpro_configure_target target_name)
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
  endif()
endfunction()

mkpro_configure_target(mkpro_core)
mkpro_configure_target(mkpro_cli)
mkpro_configure_target(mkpro_tests)
