# MK-Pro Optimization and Lowering Strategy Reference

This is an implementation-level reference for the current translator in this repository. It describes what the compiler optimizes and lowers, where each optimization is applied, and how to read reported capabilities.

The translator runs on MK-Pro source and emits MK-61 program bytes. Optimizations are selected only when semantics are preserved under the selected MK-61 exact-machine profile. If an optimization is skipped, it is usually because the prerequisite constraints, control-flow shape, or machine feature preconditions were not met.

## 1) Compilation flow and optimization scope

The optimizer works in multiple passes, not in a single “on/off” mode.

1. Parse and normalize source.
2. Run source-level rewrites over MK-Pro AST.
3. Estimate intent, ranges, and live state usage.
4. Allocate registers and infer storage strategies.
5. Build display lowering strategy.
6. Perform lowering of high-level statements, reads, displays, dispatches, functions, and spatial helpers.
7. Run a set of IR-level optimization passes iteratively.
8. Run layout and apply layout-sensitive indirect/dark-entry candidates.
9. Optionally compile and optimize setup program (for initializers requiring work).
10. Select the best lowering variant by cell count under the 105-cell window.

For over-budget programs, compile retries include candidate demotions (constant demotion, helper-shape changes) that can free a selector register and enable stronger indirect flow lowering.

## 2) Sources of reported information

Use `mk-pro --out json` or `mk-pro explain` to inspect:

- `report.optimizations` — exact optimization names that fired.
- `report.optimizer` — capability matrix (`active`, `considered`, `planned`, `candidate`).
- `report.proofs` — explicit proofs that some rewrites depended on.
- `report.machineFeaturesUsed` — machine-feature tactics enabled by successful transformations.
- `report.preloads` and `report.setupProgram` — auto-initialization strategy.

## 3) Capability families (what the optimizer is able to report)

Below are the public capability IDs from `report.optimizer.capabilities`.

- `branch-removal`
- `arithmetic-if-select`
- `arithmetic-if-update`
- `arithmetic-if-extrema`
- `zero-condition-test`
- `dispatch-compare-chain`
- `indirect-flow`
- `indirect-memory-table`
- `tail-call-lowering`
- `return-zero-jump`
- `fl-decrement-branch`
- `super-dark-dispatch`
- `r0-alias-indirect`
- `r0-fractional-sentinel`
- `negative-zero-threshold-selector`
- `x2-display-register`
- `vp-fraction-restore`
- `hex-mantissa-arithmetic`
- `fractional-indirect-addressing`
- `error-stop-idiom`
- `kmax-zero-through`
- `kor-digit-test`
- `constants-dual-use`
- `packed-position-type`
- `address-constant-overlay`
- `cyclic-address-layout`
- `dark-entry-layout`
- `liveness-analysis`
- `interprocedural-value-propagation`
- `interprocedural-dead-store`
- `dead-store-elimination`
- `last-x-reuse`
- `constant-folding`
- `cse-display-block`
- `jump-thread`
- `jump-to-next-threading`
- `dead-code-after-halt`
- `register-coalesce`
- `duplicate-failure-tail-merge`
- `arithmetic-if-pass`
- `redundant-prologue-elimination`
- `step-vs-run-verification`
- `coord-list-scaled-decimal`
- `arithmetic-if-extrema`
- `raw-display-5f`
- `vp-fraction-restore`

Capabilites can be in `considered` and not active if no matching shape is found.

## 4) AST and source-level rewrites

These transformations run on source constructs before machine lowering:

- `constant-indexed-state-resolution` — constant indexes for indexed banks are pre-resolved.
- `display-string-inline` — inline display-string definitions into show sites.
- `display-string-guarded-show` and `display-string-assignment-elimination` — fold guarded temporary string fragments.
- `display-edge-whitespace-trim` — remove useless leading/trailing whitespace around display intent.
- `expression-constant-folder` — folds constant expression trees.
- `intent-domain-lowering` — lowers intentional typed domains into canonical source shapes.
- `packed-counter-stripes` — compresses packed counter declarations for dense ranges.
- `x-param-state-elision` — elides explicit transfer artifacts around one-cell X-parameter returns.
- `tail-copy-assignment-fusion` — fuses copy-tail forms to reduce redundant writes.
- `if-chain-dispatch-canonicalization` — normalizes long condition chains into canonical dispatch shape.
- `constant-guarded-call-inline` — hoists inlinable guarded calls used once.
- `common-branch-tail-hoisting` — hoists shared tails across similar branches.
- `single-use-tail-inline` — inlines one-use tails.
- `compact-dispatch-simplification` — simplifies small dispatch graphs before lowering.
- `one-shot-loop-init-hoist` — moves one-shot loop setup constants out of repeated iterations.
- `if-branch-order-inversion` — reorders branch structure for cleaner short-circuit lowering.
- `guarded-prologue-gadget` — builds shared prologue/guard gadgets when safe.
- `dead-state-elimination` — removes unobserved state fields.
- `identity-assignment-elimination` — removes `x = x` and equivalent no-op assignments.
- `terminal-display-fusion` (lowering-level too) — collapses trailing screen-and-termination patterns.

## 5) Control-flow and jump strategy rewriting

The control-flow family is where the largest byte savings are found.

- `branch-removal` — removes explicit branch instructions where arithmetic-select or zero-test is equivalent.
- `comparison-boundary-normalization` — normalizes comparisons to cheaper equivalent forms.
- `residual-guarded-update` and `arithmetic-if-*` group — transforms guarded updates, select updates, and extrema-like branches.
- `zero-condition-test` — direct zero-test specialization in branch-heavy places.
- `dispatch-lowering`, `dispatch-default-merge`, `dispatch-case-ordering`, `dispatch-source-register`, `numeric-dispatch-residual-chain` — different dispatch lowering paths.
- `terminal-if-direct-branch`, `terminal-branch-end-elision` — end-of-path branch trimming.
- `nested-guard-shared-failure` — shares identical failure tails under nested guards.
- `ephemeral-input-branch` and `ephemeral-input-dispatch` — optimized input-driven command menus.
- `decrement-underflow-branch` and `fl-decrement-zero-branch` — prefer compact decrement-and-continue branches.
- `if-branch-order-inversion` influences whether these lowerings trigger.
- `branch`-adjacent helpers: `x-preserving-false-branch`, `small-set-condition-lowering`, `cell-membership-*` patterns, and helper-conditioned transforms.

Machine-level variants around branches:

- `tail-call-lowering`, `tail-branch-inversion`, `tail-call-layout`, `function-tail-call`, `function-tail-recursion`, `terminal-rule-tail-call`, `terminal-if-direct-branch`, `terminal-branch-end-elision` etc. together form the shared-tail economy.
- `return-suffix-gadget` and `shared-call-tail` share trailing straight-line tails.
- `jump-thread` and `jump-to-next-threading` erase jump trampolines.
- `redundant-prologue-elimination` removes duplicate loop prologues where user-visible side-effects are preserved.

## 6) Function and call lowering

- `function-call` — call lowering to shared helper calls and return handling.
- `function-call-lifting` — lifts direct calls to remove overhead when safe.
- `x-param-proc-entry` and `x-param-proc-call` — X-parameter procedure calls.
- `x-param-return-decay` / `x-param-return-decay-call` — uses decay-compatible entry conventions.
- `proc-call-lowering`, `proc-return-x-reuse`, `local-terminal-tail`, `local-terminal-tail-branch`, `int-frac-shared-tail` — tail-shape-aware and shared-tail procedures.
- `function-tail-recursion` and `function-tail-call` entries appear in lower-level reports when tail recursion is structurally recognized.

## 7) Indirect flow, dispatch, and addressing strategies

The translator aggressively evaluates when MK-61 undocumented/edge behavior can be relied upon.

- `stable-indirect-flow`, `indirect-register-flow` family — jump/call through registers rather than full two-cell addresses.
- `preloaded-indirect-flow` and `preloaded-super-dark-flow` — layout-sensitive indirect variants with preloaded selectors.
- `indirect-incdec-counter` / `r0-indirect-counter` / `fl-unit-decrement` — compact decrement patterns that combine counter update and branch.
- `indirect-memory-table` and `indexed-packed-row-table` — compact table access with `К П->X / К X->П` when selector life is provable.
- `coord-list-scaled-read` and `coord-list-scaled-decimal-storage` — scaled coordinate representation to reduce repeated extraction cost.
- `fractional-indirect-addressing` and `r0-fractional-sentinel` — using fractional-R0 behavior for selection/jumps when proof obligations are met.
- `super-dark-dispatch` / `super-dark-dispatch` candidates — uses FA..FF indirect routing with strict 48..53 / 01..06 layout constraints.

## 8) Spatial and coordinate-list optimization family

- `coord-list-line-count-dashed-report-fusion`
- `coord-list-line-count-dashed-report-body`
- `coord-list-fused-dashed-report-body`
- `coord-list-scaled-read`
- `setup-coord-list-indirect-random-unique`
- `coord-list-scaled-decimal-storage`
- `spatial-count-hit-helper`, `spatial-hit-inline`, `spatial-count-fl-loop`
- `spatial-line-count-helper`, `spatial-line-count-helper-call`
- `spatial-line-progression-helper`, `spatial-line-progression-helper-call`
- `spatial-sum-loop-helper`, `spatial-sum-loop-helper-call`
- `spatial-hit-bit-mask-helper-reuse`
- `spatial-count-hit-helper`
- `spatial-neighbor-count-unroll`
- `spatial-count-hit-helper`
- `bit-set-mask-cse`
- `bit-mask-quotient-reuse`
- `bit-set-mask-cse`
- `tic-tac-toe-cell-mask-cse`

These mostly reduce repeated bit-mask, neighbor counting, and line/row scan helper bodies by caching and reusing helper calls.

## 9) Display lowering strategy (largest semantic-sensitive area)

Display rewrites are separated into strategy selection + body lowering.

- `display-strategy-selection` — chooses one of packed numeric strategy, display-byte strategy, literal splicing, or helper-call strategy.
- `display-expression-materialization` and `display-expression-materialization`-related helper selection.
- `screen-text-lowering`, `screen-text-literal-*` family for text-heavy literal sequences.
- `screen-video-literal-helper`, `screen-video-literal-helper-call`, `screen-decimal-literal-lowering`, `screen-sign-digit-literal-lowering`, `screen-leading-zero-hex-lowering`, `screen-error-literal-lowering` and `screen-zero-digit-tail-lowering` cover special literal forms.
- `packed-display-lowering`, `packed-display-helper`, `packed-display-storage-reuse`, `packed-display-helper-call` — standard packed numeric and packed-storage paths.
- `display-byte-x2-lowering`, `display-byte-mask-lowering`, `display-byte-variable-mask-lowering`, `display-byte-helper` and `display-byte-helper-call` — hidden X2 / mantissa template paths.
- `floor-packed-row-display`, `floor-packed-row-expression-display` — floor + packed-row merge when a one-digit floor cell is present.
- `dashed-coord-report-lowering` and `dashed-coord-report-packed-body` — compact bearing reports.
- `display-decimal-literal-field`, `display-literal-first-digit-reuse`, `display-literal-minus-source-reuse`, `display-current-x-reuse` — micro-reuse in fragmented template emission.
- `display-stack-reuse` — reuse current X stack for immediate display tails.
- `display-byte-lowering` strategies are guarded by machine feature `display-bytes`.

## 10) Random and numeric helpers

- `random-range-lowering` — low-cost random range shaping.
- `int-random-range-lowering` — integerized random sequence without re-reading RNG.
- `random-cell-helper` and `random-cell-helper-call` — shared helper when random-cell access can be factored.
- `coord-list-scaled-read` and random unique coord-list setup can reduce table setup cost.
- `remainder-fraction-lowering` — specialized division path for remainder by constant.

## 11) Arithmetic and operator normalization

- `small-set-primitive-lowering`, `tic-tac-toe-primitive-lowering` — macro lowering to packed integer arithmetic and mask operations.
- `direction-keypad-lowering`, `direction-cardinal-lowering` — keypad movement macros lowered to preverified instruction sequences.
- `arithmetic-if-update`, `arithmetic-if-conditional-move`, `arithmetic-if-sign-toggle`, `arithmetic-if-abs`, `arithmetic-if-max`, `arithmetic-if-min` — branch-to-expression transforms.
- `hex-mantissa-arithmetic`, `negative-zero-threshold-*` chain — compact threshold and sign-digit style transforms.

## 12) Register allocation and liveness-driven memory trims

- `interprocedural-value-propagation` and `elideXParamReturnStateFields` (via related names) reduce cross-procedure recomputation.
- `elideXParamReturnStateFields`, `elide`-style elimination patterns keep register pressure low before layout.
- `known-zero-reuse` and `zero-reuse`-related helper substitutions avoid reloading constants.
- `constant-synthesis` and `preloaded-constant` create canonical constants with fewer literal cells.
- `preincrement-indexed-store` and `preloaded-indirect-flow` allow indexed writes to use preincrement semantics.
- `register-coalesce` merges non-overlapping live ranges.
- `last-x-reuse` deletes `П->X` when X already contains the same live value.
- `store-recall-peephole` and `dead-store-*` pass families remove redundant store-recall pairs.
- `dead-store-elimination` and `interprocedural-dead-store` remove dead writes.

## 13) IR pass pipeline (fixed-point)

The IR pipeline defined in `src/core/passes/index.ts` runs repeatedly:

1. `redundant-prologue-elimination`
2. `tail-call`
3. `tail-branch-inversion`
4. `shared-call-tail`
5. `return-suffix-gadget`
6. `return-zero-jump`
7. `store-recall-peephole`
8. `jump-to-next-threading`
9. `jump-thread`
10. `stable-indirect-flow`
11. `preloaded-indirect-flow`
12. `indirect-memory-table`
13. `dead-store-before-commutative`
14. `dead-store-elimination`
15. `last-x-reuse`
16. `r0-fractional-sentinel`
17. `vp-splice`
18. `vp-x2-peephole`
19. `constant-folding`
20. `duplicate-failure-tail`
21. `cse-display-block`
22. `dead-code-after-halt`
23. `register-coalesce`
24. `arithmetic-if-pass`

A fixed-point loop repeats while transformations continue, up to internal iteration limits.

## 14) Setup-program and preload strategy

Setup generation is separate from main program layout when needed:

- `generated-setup-program` indicates that a setup routine was emitted.
- `preloaded-constant`, `preloaded-constant` and `constant-synthesis` entries describe synthetic constants.
- `auto-preload-initial-state` and `intent-state-lowering` can push selected state to setup only.
- `intent-read-lowering`, `show-read-*` may force setup when runtime behavior or literals require state initialization.
- Setup helpers are themselves subject to same optimization pipeline (`setup-...` names appear as prefixed entries).

## 15) Machine features this optimizer may activate in report

Feature flags are added only after successful candidate/optimization evidence:

- `return-empty-stack-jump`
- `branch-removal`
- `indirect-flow`
- `indirect-memory`
- `dark-entries`
- `address-constants`
- `x2-register`
- `negative-zero-degree`
- `x2-restore-boundaries`
- `z-stack-register`
- `display-bytes`
- `r0-fractional-sentinel`
- `r0-t-alias`
- `error-stops`
- `code-data-overlay`
- `display-byte`

These are not independent optimizations; they gate whether the lowering strategy can legally use the corresponding opcode/behavior.

## 16) Proof-guided safety model (important)

The optimizer does not blindly apply undocumented behavior. Several proofs are explicitly logged and checked:

- `value-ranges`, `observability`, and `formal-address-operands` when source bounds are known.
- branch equivalence proof for `branch-removal` and arithmetic-if variants.
- `negative-zero-threshold-selector` proof for threshold selectors.
- `indirect-addressing-ranges` proof when selector stability is required.
- `super-dark-suffix-layout` proof when FA..FF dispatch is selected.
- `return-stack-empty` proof for `В/О`-as-`БП 01` behavior.

If proofs are insufficient, those transformations are not activated.

## 17) Practical tuning rules for game authors

1. Prefer stable finite ranges for counters and coordinates.
2. Keep branch conditions simple and deterministic.
3. Let displays be composed from a few reused fields.
4. Reuse identifiers in temporary values; this helps current-X reuse.
5. Avoid unnecessary string temporaries; prefer direct `show(...)` fragments.
6. Keep helper-like repeated computations in one place to trigger CSE and helper lowering.
7. Use single-purpose states and avoid side-effecting function wrappers unless semantically necessary.

## 18) What to inspect first after a compile

1. `report.budgetReport` for fit and largest blocks.
2. `report.optimizer` to see which capabilities were `active` / `considered` / `planned`.
3. `report.optimizations` for concrete names.
4. `machineFeaturesUsed` to ensure edge tactics were only enabled where intended.
5. `proofs` to validate any risky-looking branchless and indirect behavior.

## 19) Non-goals and current limits

- The compiler does not expose every microcode trick as a source hint.
- Some capability entries remain `candidate`/`planned` while not yet stable to turn on globally.
- Optimization priority is always bounded by semantic safety and layout validity, then by cell budget.

This reference should be used as a working map while reading generated listings in explain/json mode: every named optimization corresponds to a concrete rewrite class that can be correlated with local sequences in emitted IR or final machine text.
