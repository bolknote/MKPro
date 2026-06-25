# Native Test Traceability

This file tracks the current TypeScript-to-C++ test migration status.
It is intentionally conservative: a TypeScript test file is not considered fully
closed merely because nearby native coverage exists.

## Summary

- TypeScript `.test.ts` files: 59.
- Compiler/root TypeScript test files: 30.
- Emulator TypeScript test files: 29.
- Native CTest cases currently registered: 97.
- Current compiler/example parity status: all `examples/*.mkpro` and
  `examples/pending-optimizer/tic-tac-toe-4x4.mkpro` are covered by native
  size, byte-oracle, and golden-listing checks.

## Native Test Gates

The native runner supports:

```sh
# Preferred parity gate (optimized)
cmake --preset release && cmake --build --preset release && ctest --preset release
# or:
npm run native:test

# Fast local cycle for sanitizer/debug triage
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
# or:
npm run native:test:debug

# Targeted single test (works on any preset)
build/debug/native/mkpro_tests --list
build/debug/native/mkpro_tests --exact opcode_catalog_matches_typescript_contract
ctest --preset debug -R mkpro.opcode_catalog_matches_typescript_contract --output-on-failure
```

CTest registers every native test group independently as `mkpro.<test-name>`.
This is required for sanitizer triage because the example and golden tests are
too slow to debug as one monolithic process.

## Compiler Test Mapping

| TypeScript test file | Native counterpart | Status |
| --- | --- | --- |
| `tests/compiler.test.ts` | `native/tests/examples_contract_test.cpp`, `native/tests/example_sizes_test.cpp`, `native/tests/example_parity_test.cpp`, `native/tests/golden_listing_contract_test.cpp` | File-level covered; per-assertion audit pending |
| `tests/compiler/bit-mask-quotient-reuse.test.ts` | `native/tests/bit_mask_quotient_reuse_test.cpp` | File-level covered |
| `tests/compiler/board-width-macros.test.ts` | `native/tests/board_width_macros_test.cpp` | File-level covered |
| `tests/compiler/cfg.test.ts` | `native/tests/cfg_test.cpp`, `native/tests/liveness_analysis_test.cpp` | File-level covered |
| `tests/compiler/compiler.test.ts` | `native/tests/compiler_test.cpp`, `native/tests/compiler_coord_list_test.cpp`, `native/tests/compiler_display_test.cpp`, `native/tests/display_lowering_test.cpp`, `native/tests/display_byte_helper_test.cpp`, `native/tests/expression_lowering_test.cpp`, `native/tests/lowering_helpers_test.cpp` | File-level covered; per-assertion audit pending |
| `tests/compiler/const.test.ts` | `native/tests/constant_folding_test.cpp`, `native/tests/v2_const_test.cpp` | File-level covered |
| `tests/compiler/example-sizes.test.ts` | `native/tests/example_sizes_test.cpp` | File-level covered |
| `tests/compiler/examples.test.ts` | `native/tests/examples_contract_test.cpp`, `native/tests/example_parity_test.cpp` | File-level covered |
| `tests/compiler/formal-address.test.ts` | `native/tests/formal_address_test.cpp` | File-level covered |
| `tests/compiler/functions.test.ts` | `native/tests/functions_test.cpp` | File-level covered |
| `tests/compiler/golden-listing.test.ts` | `native/tests/golden_listing_contract_test.cpp` | File-level covered |
| `tests/compiler/indirect-addressing.test.ts` | `native/tests/indirect_addressing_test.cpp`, `native/tests/indirect_selector_integer_part_test.cpp` | File-level covered |
| `tests/compiler/ir-round-trip.test.ts` | `native/tests/ir_round_trip_test.cpp` | File-level covered |
| `tests/compiler/match-blocks.test.ts` | `native/tests/match_blocks_test.cpp` | File-level covered |
| `tests/compiler/maxmin-zero-lint.test.ts` | `native/tests/maxmin_zero_lint_test.cpp` | File-level covered |
| `tests/compiler/opcodes.test.ts` | `native/tests/opcodes_test.cpp` | File-level covered |
| `tests/compiler/parser.test.ts` | `native/tests/parser_test.cpp` | File-level covered |
| `tests/compiler/passes.test.ts` | `native/tests/passes_test.cpp` plus pass-specific native tests | File-level covered; per-pass audit pending |
| `tests/compiler/post-layout-indirect-flow.test.ts` | `native/tests/post_layout_indirect_flow_test.cpp` | File-level covered |
| `tests/compiler/residual-elseif.test.ts` | `native/tests/residual_elseif_test.cpp` | File-level covered |
| `tests/compiler/residual-temp.test.ts` | `native/tests/residual_temp_test.cpp` | File-level covered |
| `tests/compiler/safe-minmax.test.ts` | `native/tests/safe_minmax_test.cpp` | File-level covered |
| `tests/compiler/segmented-bitplane.test.ts` | `native/tests/segmented_bitplane_test.cpp` | File-level covered |
| `tests/compiler/setup-format.test.ts` | `native/tests/setup_format_contract_test.cpp`, `native/tests/setup_program_test.cpp`, `native/tests/setup_only_counted_loop_test.cpp` | File-level covered |
| `tests/compiler/show-optimization.test.ts` | `native/tests/show_optimization_contract_test.cpp`, `native/tests/show_sequence_helper_test.cpp`, `native/tests/show_read_guarded_transfer_test.cpp` | File-level covered |
| `tests/compiler/stack-residency.test.ts` | `native/tests/stack_residency_test.cpp` | File-level covered |
| `tests/compiler/state-init-counted-loop.test.ts` | `native/tests/state_init_counted_loop_test.cpp`, `native/tests/state_banks_test.cpp` | File-level covered |
| `tests/compiler/strict-allocation.test.ts` | `native/tests/strict_allocation_test.cpp` | File-level covered |
| `tests/compiler/style-lints.test.ts` | `native/tests/style_lints_test.cpp` | File-level covered |
| `tests/compiler/super-dark-layout.test.ts` | `native/tests/super_dark_layout_test.cpp` | File-level covered |

## Emulator Test Mapping

The TypeScript emulator suite has not yet been fully migrated to a native
MK-61 emulator suite. Twenty-four files are fully covered by native emulator tests,
one file is partially covered, and four files are still pending native
equivalents:

| TypeScript emulator test file | Native counterpart | Status |
| --- | --- | --- |
| `tests/emulator/bitmask-facts.test.ts` | `native/tests/emulator_bitmask_facts_test.cpp` | File-level covered |
| `tests/emulator/constants-dual-use-equivalence.test.ts` | `native/tests/emulator_constants_dual_use_test.cpp` | File-level covered |
| `tests/emulator/display-byte-facts.test.ts` | `native/tests/emulator_display_byte_test.cpp` | File-level covered |
| `tests/emulator/domain-error-guard.test.ts` | `native/tests/emulator_domain_error_guard_test.cpp` | File-level covered |
| `tests/emulator/fl-counter-facts.test.ts` | `native/tests/emulator_fl_counter_test.cpp` | File-level covered |
| `tests/emulator/fractional-r0.test.ts` | `native/tests/emulator_fractional_r0_test.cpp` | File-level covered |
| `tests/emulator/function-equivalence.test.ts` | `native/tests/emulator_function_equivalence_test.cpp` | File-level covered |
| `tests/emulator/hex-arithmetic-facts.test.ts` | Pending native emulator test | Missing |
| `tests/emulator/if-chain-dispatch.test.ts` | `native/tests/emulator_if_chain_dispatch_test.cpp` | File-level covered |
| `tests/emulator/indirect-flow-equivalence.test.ts` | `native/tests/emulator_indirect_flow_equivalence_test.cpp` | File-level covered |
| `tests/emulator/indirect-incdec-facts.test.ts` | `native/tests/emulator_indirect_incdec_test.cpp` | File-level covered |
| `tests/emulator/int-frac-shared-tail.test.ts` | `native/tests/emulator_int_frac_shared_tail_test.cpp` | File-level covered |
| `tests/emulator/interprocedural-equivalence.test.ts` | `native/tests/emulator_interprocedural_equivalence_test.cpp` | File-level covered |
| `tests/emulator/log-selector-premise.test.ts` | `native/tests/emulator_log_selector_premise_test.cpp` | File-level covered |
| `tests/emulator/near-any-helper.test.ts` | `native/tests/emulator_near_any_helper_test.cpp` | File-level covered |
| `tests/emulator/number-entry-concat.test.ts` | `native/tests/emulator_number_entry_concat_test.cpp` | File-level covered |
| `tests/emulator/packed-position-facts.test.ts` | `native/tests/emulator_packed_position_test.cpp` | File-level covered |
| `tests/emulator/recall-side-effects.test.ts` | `native/tests/emulator_recall_side_effects_test.cpp` | File-level covered |
| `tests/emulator/regression.test.ts` | Pending native emulator test | Missing |
| `tests/emulator/rom-discoveries.test.ts` | `native/tests/emulator_rom_test.cpp`, `native/tests/emulator_mk61_test.cpp` | Partial: ROM table facts and selected execution facts covered; remaining execution discoveries pending |
| `tests/emulator/stack-dup-equivalence.test.ts` | `native/tests/emulator_stack_dup_test.cpp` | File-level covered |
| `tests/emulator/stack-resident-equivalence.test.ts` | Pending native emulator test | Missing |
| `tests/emulator/super-dark-equivalence.test.ts` | `native/tests/emulator_super_dark_test.cpp` | File-level covered |
| `tests/emulator/trap-opcodes.test.ts` | `native/tests/emulator_mk61_test.cpp` | File-level covered |
| `tests/emulator/vo-return-facts.test.ts` | `native/tests/emulator_vo_return_test.cpp` | File-level covered |
| `tests/emulator/vp-splice-equivalence.test.ts` | Pending native emulator test | Missing |
| `tests/emulator/x2-dead-restore-before-overwrite.test.ts` | `native/tests/emulator_x2_dead_restore_test.cpp` | File-level covered |
| `tests/emulator/x2-restore-context.test.ts` | `native/tests/emulator_x2_restore_context_test.cpp` | File-level covered |
| `tests/emulator/z-stack-derived-tail.test.ts` | `native/tests/emulator_z_stack_derived_tail_test.cpp` | File-level covered |

## Remaining Closure Work

1. Port the JS/TS MK-61 emulator helpers into native test support.
2. Complete native equivalents for the remaining missing and partial emulator
   `.test.ts` files above.
3. Audit compiler/root mappings at assertion level, especially broad files such
   as `tests/compiler.test.ts`, `tests/compiler/compiler.test.ts`, and
   `tests/compiler/passes.test.ts`.
4. Run the five heavy TSan cases individually:
   - `mkpro.compiler_lowers_initial_v2_subset`
   - `mkpro.example_sizes_match_typescript_baselines`
   - `mkpro.supported_examples_match_native_oracles`
   - `mkpro.golden_listing_contract_matches_typescript_contract`
   - `mkpro.setup_formatting_matches_typescript_contract`
