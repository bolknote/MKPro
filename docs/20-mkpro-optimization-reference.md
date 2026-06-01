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
10. Select the best lowering variant by cell count under the 105-cell window, using estimated startup+program cost as a tie-break for over-budget variants.

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

- `branch-removal` — removes an unnecessary branch when the needed value can be computed without a separate branch path.
- `arithmetic-if-select` — emits selected values through arithmetic formulas instead of `if/else`.
- `arithmetic-if-update` — performs conditional assignment in one path instead of two branches.
- `arithmetic-if-extrema` — replaces branching for `max/min` with a short arithmetic form.
- `zero-condition-test` — shortens checks such as `== 0` to a cheaper machine pattern.
- `dispatch-compare-chain` — compresses long compare-and-branch chains.
- `indirect-flow` — enables indirect jumps/dispatch when preconditions are proven.
- `indirect-memory-table` — reads the next-cell address through an indirect table instead of long absolute labels.
- `tail-call-lowering` — lowers tail calls to a shorter jump-based form instead of a full call frame.
- `return-zero-jump` — rewrites `return` as a short jump via cell `0`.
- `fl-decrement-branch` — compresses “decrement-and-jump” pattern into one block.
- `super-dark-dispatch` — uses FA..FF routing mode where a valid layout exists.
- `r0-alias-indirect` — allows R0 to be used as an indirect pointer when semantics stay safe.
- `r0-fractional-sentinel` — applies a fractional sentinel in R0 for a shorter jump path.
- `negative-zero-threshold-selector` — replaces range checks with a short threshold test.
- `x2-display-register` — saves cells/instructions in display by enabling X2 mode.
- `vp-fraction-restore` — restores VP quickly after arithmetic using a short path.
- `hex-mantissa-arithmetic` — simplifies arithmetic on hexadecimal mantissas.
- `fractional-indirect-addressing` — enables indirect jumps through fractional addressing.
- `error-stop-idiom` — compacts the common `error + stop` path.
- `kmax-zero-through` — optimizes `kmax` pattern by passing through zero and finishing immediately.
- `kor-digit-test` — compresses digit-kind testing into a single check.
- `constants-dual-use` — reuses one computed constant result in two places.
- `packed-position-type` — packs position type state, reducing support code.
- `address-constant-overlay` — overlays one constant address on another without extra cells.
- `cyclic-address-layout` — reorders address layout so jumps become shorter.
- `dark-entry-layout` — re-layouts entry points to enable dark-entry transitions.
- `liveness-analysis` — analyzes live values and removes unnecessary storage.
- `interprocedural-value-propagation` — propagates known values across procedures.
- `interprocedural-dead-store` — removes writes that are never read even after call expansion.
- `dead-store-elimination` — removes unnecessary `store`/`recall` when not used.
- `last-x-reuse` — avoids rewriting X when the needed value is already there.
- `constant-folding` — precomputes constant parts before code generation.
- `cse-display-block` — merges identical display logic blocks.
- `jump-thread` — rewires jump chains into one direct jump path.
- `jump-to-next-threading` — removes intermediate jumps to the next label.
- `dead-code-after-halt` — removes code unreachable after `HALT`.
- `register-coalesce` — merges separate temporary cells when lifetime ranges do not overlap.
- `duplicate-failure-tail-merge` — merges identical error/failure tail sequences.
- `arithmetic-if-pass` — a dedicated pass collecting all `arithmetic-if` opportunities.
- `redundant-prologue-elimination` — removes repeated identical prologues.
- `step-vs-run-verification` — chooses the more compact step/run verification form.
- `coord-list-scaled-decimal` — uses scaled coordinate lists for cheaper decimal handling.
- `raw-display-5f` — selects a low-level rendering path using opcode `0x5F`.

Capabilities can be `considered` and not active if no matching shape is found.

## 4) AST and source-level rewrites

These transformations run on source constructs before machine lowering:

- `constant-indexed-state-resolution` — if array/field index is known at compile time, substitutes the exact cell address directly.
- `display-string-inline` — moves text templates directly into `show`, removing separate temporary definitions.
- `display-string-guarded-show` and `display-string-assignment-elimination` — simplify guarded string branches and remove extra intermediate variables.
- `display-edge-whitespace-trim` — removes leading/trailing whitespace around templates that does not affect display output.
- `expression-constant-folder` — precomputes constant expression subtrees.
- `show-read-fusion` — merges `show(...)` with a following `read`-based assignment/input path into one calculator `С/П`: `show(...); x = read()` or `show(...); x = int(read())` / `frac(int(read()))` forms share the same input stop and avoid emitting a second `С/П`.
- `counted-loop-unroll` — replaces small constant-trip counted `while` loops with explicit per-iteration copies when the induction variable updates are simple linear steps and entry values are known constants; this removes the loop machinery and registers update logic.
- `counted-loop-unroll-free-scratch` — runs counted-loop unrolling together with residual-dispatch scratch freeing (`freeResidualDispatchScratch`) as one candidate.
- `state-init-counted-loop` — recovers the compact one-cell `F Lx` counted-loop lowering for countdown loops whose counter carries its initial value on the state field (`time: counter 0..N = N` + `while time >= 1 { …; time-- }`). When that counter is used solely by the loop in the top-level entry body, the state initializer is rewritten into an explicit `time = N` immediately before the loop, matching the already-supported explicit-init form byte-for-byte while staying re-runnable (the inline store re-arms the counter on every `С/П`, unlike a setup-only preload).
- `domain-error-guard` — replaces a terminal-error guard (`if <expr> <op> 0 { halt("ЕГГОГ") }`, including a call to a proc whose body is just that trap) with a single self-trapping domain opcode that raises ЕГГОГ exactly on its mathematical domain (all proved on hardware in `tests/emulator/trap-opcodes.test.ts`): `F √` for `<` (traps iff X < 0), `F lg` for `<=` (iff X <= 0), and `F 1/x` for `==` (iff X == 0, division by zero — the exact equality trap regardless of sign). `>`/`>=` reduce to the swapped difference. The guard computes the comparison difference into X so the trap fires iff the condition holds; otherwise it falls through into the false branch. This collapses the compare + conditional branch + shared trap into one cell, and when every caller of a shared trap proc is converted the proc becomes dead and is dropped. Speculative (`domainErrorGuards`): adopted only when the whole program ends up smaller. Examples: rambo-iii 139→135, alaram 80→76, dungeon 85→83, wumpus 105→103.
- `startup-aware-constant-preloads` — tries a variant that leaves setup-expensive synthesized constants inline, such as decimal powers built with `F 10^x`, when that lowers estimated startup+program cost without increasing the main program size.
- `intent-domain-lowering` — normalizes special intent types into a base form for later compilation.
- `packed-counter-stripes` — packs dense counters into a shorter representation.
- `x-param-state-elision` — removes redundant transition states when returning through X parameters.
- `tail-copy-assignment-fusion` — merges copy assignments in tail blocks into one write pass.
- `if-chain-dispatch-canonicalization` — turns long `if` chains into a single dispatch template.
- `constant-guarded-call-inline` — inlines a guarded call when used once and safe.
- `common-branch-tail-hoisting` — merges identical tails from similar branches.
- `single-use-tail-inline` — inlines a one-time tail instead of emitting a separate call.
- `compact-dispatch-simplification` — compresses small dispatches to a minimal jump tree.
- `one-shot-loop-init-hoist` — hoists loop initialization that runs once out of repeated body.
- `if-branch-order-inversion` — reorders condition branches so profitable paths are checked earlier.
- `guarded-prologue-gadget` — creates one guarded prologue for multiple branches where logic is equivalent.
- `dead-state-elimination` — removes state fields that do not affect outcomes.
- `identity-assignment-elimination` — removes useless assignments like `x = x`.
- `terminal-display-fusion` — merges final `show+HALT` into a shorter block.

## 5) Control-flow and jump strategy rewriting

The control-flow family is where the largest byte savings are found.

- `branch-removal` — removes `if/else` when the target value can be computed arithmetically.
- `comparison-boundary-normalization` — rewrites comparisons into an equivalent, cheaper form.
- `residual-guarded-update` — compacts guarded updates to remove extra steps.
- `arithmetic-if-select` — performs conditional selection through arithmetic instead of jumps.
- `arithmetic-if-update` — performs conditional assignment in one path instead of multiple instructions.
- `arithmetic-if-extrema` — computes `max/min` using shorter conditional forms.
- `zero-condition-test` — emits zero tests in the shortest variant instead of expanded `if`.
- `dispatch-lowering` — converts dispatch nodes to short jump sequences.
- `dispatch-default-merge` — shares one tail when different `default` branches are identical.
- `dispatch-case-ordering` — reorders cases so fast paths are checked earlier.
- `dispatch-source-register` — keeps selected source in a dedicated register in advance.
- `numeric-dispatch-residual-chain` — packs numeric check chains in tail lowering form.
- `terminal-if-direct-branch` — turns final checks into direct branches.
- `terminal-branch-end-elision` — removes the final redundant jump at block end.
- `nested-guard-shared-failure` — uses one shared failure handler for nested guarded branches.
- `ephemeral-input-branch` — shortens one-off input paths into compact branches.
- `ephemeral-input-dispatch` — chooses input-based dispatch through denser tables.
- `decrement-underflow-branch` — decrements and immediately handles underflow in one step.
- `fl-decrement-zero-branch` — a dedicated “decrement and test zero” sequence in one short block.
- `if-branch-order-inversion` — reorders branches so downstream lowering is shorter.
- `x-preserving-false-branch` — preserves current X value in the false branch.
- `small-set-condition-lowering` — lowers small `set` conditions to compact code.
- `cell-membership-*` patterns — tests “is in set” using short branch patterns.
- helper-conditioned transforms — moves conditional checks so a helper pass can simplify them.

Machine-level variants around branches:

- `tail-call-lowering` — rewrites final calls into a tail-safe short form.
- `tail-branch-inversion` — flips the branch condition when shorter.
- `tail-call-layout` — reorders tail calls to fit better in layout.
- `function-tail-call` — does the same for function tail calls by converting call to direct jump.
- `function-tail-recursion` — when a function tail-calls itself, emits a loop.
- `terminal-rule-tail-call` — turns final rule calls into direct jumps.
- `return-suffix-gadget` — shares a common suffix after `return` across similar regions.
- `shared-call-tail` — keeps one shared tail after calls instead of duplicates.
- `jump-thread` — rewires jump chains into a straight flow.
- `jump-to-next-threading` — removes jumps that only go to the next label.
- `redundant-prologue-elimination` — merges repeated prologues while preserving side effects.

## 6) Function and call lowering

- `function-call` — lowers a normal call into a short machine form with shared helper and return handling, removing unnecessary call/return steps.
- `function-call-lifting` — lifts direct call sites when safe, simplifying straightforward calls.
- `x-param-proc-entry` — alternative procedure entry through X when cheaper.
- `x-param-proc-call` — passes parameters through X with fewer instructions.
- `x-param-return-decay` — prepares a return path through X for safe reuse afterward.
- `x-param-return-decay-call` — applies the same X-return pattern at call sites.
- `proc-call-lowering` — builds procedure calls with return strategy and state handling.
- `proc-return-x-reuse` — avoids rewriting X if it already holds the needed value on return.
- `local-terminal-tail` — shares a tail block for local calls.
- `local-terminal-tail-branch` — shares a branching tail similarly.
- `int-frac-shared-tail` — one common tail for int/frac returns reduces duplication.
- `function-tail-recursion` — recognizes tail recursion and turns it into a loop.
- `function-tail-call` — converts function tail recursion into a direct jump to entry, skipping the final call.

## 7) Indirect flow, dispatch, and addressing strategies

The translator aggressively evaluates when undocumented/edge MK-61 behavior can be relied on.

- `stable-indirect-flow` — after register-liveness analysis, routes branches/calls through one indirect pointer.
- `indirect-register-flow` — the same for regions where address is in a register and already safe for indirect jump.
- `preloaded-indirect-flow` — preloads selector/address once so multiple indirect jumps become shorter.
- `preloaded-super-dark-flow` — super-dark path with a preloaded indirect target.
- `indirect-incdec-counter` — lowers a unit `x++`/`x--` through the indirect pre-increment (R4..R6) or pre-decrement (R0..R3) side effect of `К П->X r`, a one-cell true `±1` that correctly reaches 0 (used as the standalone unit-decrement form).
- `r0-indirect-counter` — uses R0 as a readable counter/switch for jump dispatch where provably safe.
- `indirect-memory-table` — builds a compact address table in memory and jumps by index.
- `indexed-packed-row-table` — stores packed rows/cells in an addressable table for dense display access.
- `coord-list-scaled-read` — reads coordinates via scaled index, removing runtime decode work.
- `coord-list-scaled-decimal-storage` — same as above but decimal form, using fewer cells.
- `fractional-indirect-addressing` — allows indirect access through fractional address arithmetic when proofs are available.
- `r0-fractional-sentinel` — uses a fractional-state sentinel in R0 to steer jumps and tables.
- `super-dark-dispatch` — enables FA..FF range routing for shorter jumps with strictly valid address neighborhoods.

## 8) Spatial and coordinate-list optimization family

- `setup-coord-list-indirect-random-unique` — builds random-unique coordinate lists via indirect access to save layout.
- `coord-list-line-count-dashed-report-fusion` — merges line-count report construction with subsequent output.
- `coord-list-line-count-dashed-report-body` — extracts a shared report body for reuse.
- `coord-list-fused-dashed-report-body` — joins multiple report-building stages into one sequence.
- `coord-list-scaled-read` — reads coordinates in scaled form to reduce index-recompute instructions.
- `coord-list-scaled-decimal-storage` — stores scaled declared lists in compact decimal form.
- `spatial-count-hit-helper` — extracts a helper for bulk hit counting.
- `spatial-hit-inline` — inlines the hot “hit” count case directly, removing extra calls.
- `spatial-count-fl-loop` — unrolls a short fractional-loop hit counter over lines/tiles locally.
- `spatial-line-count-helper` — one shared helper counts a long line by index.
- `spatial-line-count-helper-call` — inlines or dispatches to `spatial-line-count-helper` based on profile.
- `spatial-line-progression-helper` — generalizes line/progression movement into its own compute block.
- `spatial-line-progression-helper-call` — replaces repeated line-progression loops with a ready helper call.
- `spatial-sum-loop-helper` — extracts a shared summation loop if it appears in multiple sites.
- `spatial-sum-loop-helper-call` — turns repeated complex sum loops into one shared call.
- `spatial-hit-bit-mask-helper-reuse` — reuses a prepared bit-mask for hit helper paths.
- `spatial-neighbor-count-unroll` — unrolls small neighbor counting directly when it is shorter than a call.
- `bit-set-mask-cse` — removes repeated bit-mask calculations for identical coordinates.
- `bit-mask-quotient-reuse` — reuses previously computed quotients/parts for mask generation.
- `tic-tac-toe-cell-mask-cse` — a dedicated CSE optimization for tic-tac-toe cell-mask patterns.

## 9) Display lowering strategy (largest semantic-sensitive area)

Display rewrites are separated into strategy selection + body lowering.

- `display-strategy-selection` — chooses the best output mode: packed, display-byte, literal splice, or shared helper.
- `display-expression-materialization` — prepares expressions for the display node so they can be compacted faster.
- `display-expression-materialization helper family` — adds temporary helper nodes only when there is a gain.
- `screen-text-lowering` — turns ordinary text blocks into minimal MK-61 instruction sequences.
- `screen-text-literal-first-splice` — optimizes the first segment of a text literal separately.
- `screen-text-literal-preload` — preloads a literal early so it is not treated as a runtime-computed path.
- `screen-decimal-literal-lowering` — prints decimal literals using a dedicated short scheme.
- `screen-leading-zero-hex-lowering` — removes extra leading zeros in hexadecimal output.
- `screen-sign-digit-literal-lowering` — prints sign + digit through a compact form.
- `screen-zero-digit-tail-lowering` — efficiently processes trailing zero digits in numeric strings.
- `screen-error-literal-lowering` — emits common errors/codes through a short output path.
- `screen-video-literal-helper` — lifts video/text literals into a reusable helper for repeated use.
- `screen-video-literal-helper-call` — calls `screen-video-literal-helper` instead of re-expanding the template.
- `packed-display-storage-reuse` — reuses already-packed storage for display output.
- `packed-display-helper` — extracts repeated packed display format into one helper.
- `packed-display-helper-call` — replaces repeated code with a call to that helper.
- `packed-display-lowering` — base path for packed numeric rendering.
- `display-byte-x2-lowering` — uses X2 extension for simplified byte-packet output.
- `display-byte-mask-lowering` — applies masking for byte-template output.
- `display-byte-variable-mask-lowering` — supports variable masks to avoid unnecessary branches.
- `display-byte-helper` — prepares a shared helper for frequent `display-byte` patterns.
- `display-byte-helper-call` — calls `display-byte-helper` when available.
- `floor-packed-row-display` — merges `floor` + packed-row into one short path.
- `floor-packed-row-expression-display` — same for expressions where floor comes from an expression.
- `dashed-coord-report-lowering` — compact output for dashed-coordinate reports.
- `dashed-coord-report-packed-body` — compresses report body into packed format.
- `display-decimal-literal-field` — prints a single integer field in decimal mode without extra parsing.
- `display-literal-first-digit-reuse` — reuses the first digit already printed in the template.
- `display-literal-minus-source-reuse` — reuses the source for minus/sign output.
- `display-current-x-reuse` — uses current X as display source and avoids extra transfers.
- `display-stack-reuse` — reuses terminal X stack usage in display and removes redundant jumps.
- `show-sequence-helper` — shared helper for typical `show(...)` sequences.
- `show-sequence-helper-call` — calls the shared helper instead of duplicating show blocks.
- `decimal-point-display` — renders fixed-point decimal layouts like `show(x, ".", frac)` by building fractional digits and dividing by 10^width.
- `display-byte` strategies (`display-byte-*`) are applied only with `display-bytes` flag; otherwise a safe fallback is used.

## 10) Random and numeric helpers

- `random-range-lowering` — shortens random value generation within a range into shorter microcode.
- `int-random-range-lowering` — returns only integer result without extra fractional post-processing.
- `random-cell-helper` — extracts shared random-cell handling into one helper.
- `random-cell-helper-call` — calls the extracted helper instead of repeating logic.
- `coord-list-scaled-read` — in random coordinate paths, reduces table-unfold cost.
- `remainder-fraction-lowering` — chooses quick modulo paths through fraction operations.

## 11) Arithmetic and operator normalization

- `small-set-primitive-lowering` — replaces small multi-way boolean/state sets with dense arithmetic chains.
- `tic-tac-toe-primitive-lowering` — maps tic-tac-toe operations into bit masks and add/sub-style forms.
- `direction-keypad-lowering` — lowers keypad movement to a validated short machine code.
- `direction-cardinal-lowering` — movement optimization for cardinal directions.
- `arithmetic-if-update` — turns conditional updates into arithmetic form instead of branching.
- `arithmetic-if-conditional-move` — replaces conditional `move`/copy with arithmetic form.
- `arithmetic-if-sign-toggle` — routes sign handling through arithmetic when it shortens branches.
- `arithmetic-if-abs` — converts absolute value to branchless arithmetic.
- `arithmetic-if-max` — computes max using a branchless path.
- `arithmetic-if-min` — computes min using a branchless path.
- `arithmetic-if-double-clamp` — special double-check clamp in one arithmetic template.
- `arithmetic-if-comparison-mask` — builds comparison masks without explicit `if`.
- `arithmetic-if-boolean-algebra` — lowers complex boolean comparisons into masks and arithmetic.
- `hex-mantissa-arithmetic` — simplifies hex mantissa operations, lowering instruction count.
- `negative-zero-threshold-selector` — threshold check for `-0`/`0` when it reduces branches.

## 12) Register allocation and liveness-driven memory trims

- `interprocedural-value-propagation` — propagates known constants/values across function calls.
- `interprocedural-dead-store` — removes writes to cells not read beyond procedure boundaries.
- `elideXParamReturnStateFields` — removes unused X return-state fields and reduces memory.
- `elide`-style elimination patterns — remove intermediate bookkeeping artifacts when no longer needed.
- `constant-synthesis` — synthesizes reusable constants in minimally short ways. Exact positive powers of ten can be built as `exponent; F 10^x` when that beats digit entry, both in main code and setup preloads.
- `preloaded-constant` — preloads constants when cheaper than recomputing each time.
- `auto-preload-initial-state` — moves required startup cells into setup so main code is shorter.
- `preloaded-indirect-flow` — enables indexed writes via preloaded selector.
- `preincrement-indexed-store` — uses preincrement semantics for indexed stores where profitable.
- `register-coalesce` — coalesces cells when lifetimes do not overlap.
- `copy-coalesce` — removes redundant copy writes between registers.
- `last-x-reuse` — avoids `P->X` when X already holds the needed value.
- `known-zero-reuse` — reuses a known zero source instead of reloading.
- `zero-reuse` — similarly reuses zero in multiple places when liveness is confirmed.
- `stack-current-x-scheduling` — reorders current-X operations to avoid extra push/pop-like steps.
- `dead-temp-store` — removes temporary stores after their last read when no longer needed.
- `store-recall-peephole` — collapses `store` then immediate `recall` of same cell.
- `dead-store-elimination` — full pass removing pointless stores and empty branches.
- `repeated-assignment-value-reuse` — reuses the same computed value across multiple assignments.
- `int-frac-shared-tail` — a shared int/frac return tail reduces duplication.
- `z-stack-derived-value-reuse` — lowers Z-stack pressure by moving values through warm locations.

## 13) IR pass pipeline (fixed-point)

The IR pipeline defined in `src/core/passes/index.ts` runs repeatedly:

1. `redundant-prologue-elimination` — removes duplicate `display+HALT` prologues immediately before a jump target when an identical prologue is already at that jump target.
2. `tail-call-lowering` — rewrites certain tail `call`s and trailing `return`s into direct `БП`/tail flow when the continuation is the same for all exits of that region.
3. `tail-branch-inversion` — flips `cjump` condition when the then-path is only a single tail jump and the target label is uniquely referenced.
4. `shared-call-tail` — groups repeated `call` + `jump` tails (three or more occurrences), emits one shared helper tail, and replaces duplicates with `БП` to that helper.
5. `return-suffix-gadget` — finds repeated return-ending blocks ending in `return`, extracts one shared suffix, and redirects additional copies to it.
6. `return-zero-jump` — when no procedure calls are used, replaces a backward jump to `01` with `В/О` and tags it as an empty-stack optimization.
7. `store-recall-peephole` — removes `X->П r` immediately followed by `П->X r` to the same register.
8. `jump-to-next-threading` — removes unconditional jumps where target is the next label in sequence.
9. `jump-thread` — threads labels by replacing jumps to label chains with the final target label.
10. `stable-indirect-flow` — rewrites direct `jump/call/cjump` to indirect forms (`К БП`, `К ПП`, `К <cond>`) when a stable selector is already live in a register.
11. `preloaded-indirect-flow` — preloads a selector value into a spare stable register and rewrites repeated backward-direct numeric jumps/calls through that preloaded value.
12. `indirect-memory-table` — rewrites direct `store/recall` into `К X->П`/`К П->X` when a stable selector maps to the indexed target cell.
13. `dead-store-before-commutative` — removes temporary stores that are followed by immediate `recall` + commutative ALU (`+` or `*`) and never read again before the next write of that register.
14. `dead-store-elimination` — removes stores whose target register is not live after the write and does not affect number-entry/input finalization.
15. `last-x-reuse` — removes `П->X r` when `X` already contains `r` from the immediately preceding `X->П`.
16. `r0-fractional-sentinel` — drops redundant immediate `П->X 3` after fractional-R0 indirect access when `R0` is already known to be non-live.
17. `vp-splice` — deletes redundant exponent-entry chains (`ВП ВП`) and inert `КНОП ВП` forms.
18. `vp-x2-peephole` — removes redundant `К {x}` that immediately follows a display-aware `ВП`/X2 marker.
19. `constant-folding` — deletes identity arithmetic operations (`0+` and `1*`) when both operations are explicit user-facing constants.
20. `duplicate-failure-tail-merge` — removes duplicated `(label -> 0 -> pause)` failure tails by redirecting the first tail to the second.
21. `cse-display-block` — detects identical `recall/plain/.../return(stop)` blocks and replaces duplicates with one canonical block plus jump.
22. `dead-code-after-halt` — removes unreachable IR ops by CFG reachability from entry.
23. `register-coalesce` — merges non-overlapping register live ranges and, when enabled, performs copy coalescing for safe `recall/store` aliases.
24. `arithmetic-if-pass` — merges two branch paths that lower to byte-identical pure linear blocks (same side effects and same single-pass behavior).

A fixed-point loop repeats while transformations continue, up to internal iteration limits.

## 14) Setup-program and preload strategy

Setup generation is separate from main program layout when needed:

- `generated-setup-program` indicates that a setup routine was emitted.
- `preloaded-constant` and `constant-synthesis` entries describe synthetic constants.
- `auto-preload-initial-state` and `intent-state-lowering` can push selected state to setup only.
- `intent-read-lowering`, `show-read-*` may force setup when runtime behavior or literals require state initialization.
- Setup helpers are themselves subject to the same optimization pipeline (`setup-...` names appear as prefixed entries).
- `indexed-bank-loop` — initializes runs of consecutively allocated indexed bank fields with one compact setup loop when their initializers and register layout allow it.

## 15) Machine features this optimizer may activate in report

Feature flags are added only after successful candidate/optimization evidence:

- `return-empty-stack-jump` — added when `return-zero-jump` is used; means the compiler selected `В/О` as the one-cell `БП 01` shape.
- `branch-removal` — added when `branch-removal` optimization rewrites a branch to a branchless equivalent.
- `indirect-flow` — added when register-held or preloaded indirect flow rewrites (`stable-indirect-flow`, `preloaded-indirect-flow`, `preloaded-super-dark-flow`) are emitted.
- `indirect-memory` — added when indirect-memory selectors are used (`indirect-memory-table`, `indexed-packed-row-table`).
- `dark-entries` — added from cyclic formal dark-entry selection and related layout features.
- `address-constants` — added when constants are reused as arithmetic/address-like data.
- `x2-register` — added when X2/Xп/дисплей-byte scheduling relies on X2 boundaries across display-byte paths.
- `negative-zero-degree` — added when `negative-zero-threshold-selector` proof uses the `1|-00` preload trick.
- `x2-restore-boundaries` — added when `vp-fraction-restore` is active.
- `z-stack-register` — added when `z-stack-derived-value-reuse` uses deeper stack-derived storage.
- `display-bytes` — added when display-byte or packed hex-mantissa lowering is active.
- `r0-fractional-sentinel` — added when fractional indirect addressing or R0 fractional sentinel flow/path is active.
- `r0-t-alias` — added when `r0-indirect-counter` path is selected and R0-transforming aliases are proven safe.
- `error-stops` — added for domain-error stop/trap lowering (`error-stop`, `screen-error-literal-lowering`, `domain-error-guard`).
- `code-data-overlay` — added when layout marks address cells as overlayable with code/data reuse.
- `super-dark-dispatch` — added when `super-dark-dispatch` or `preloaded-super-dark-flow` candidate is selected and FA..FF routing is proven.

These are not independent optimizations; they gate whether the lowering strategy can legally use the corresponding opcode/behavior.

## 16) Proof-guided safety model (important)

The optimizer does not blindly apply undocumented behavior. Several proofs are explicitly logged and checked:

- `value-ranges`, `observability`, and `formal-address-operands` when source bounds are known.
- branch equivalence proof for `branch-removal` and arithmetic-if variants.
- `negative-zero-threshold-selector` proof for threshold selectors.
- `indirect-addressing-ranges` proof when selector stability is required.
- `super-dark-suffix-layout` proof when FA..FF dispatch is selected.
- `return-stack-empty` proof for `I/O` as `JP 01` behavior.

If proofs are insufficient, those transformations are not activated.

## 17) Practical tuning rules for game authors

1. Prefer stable finite ranges for counters and coordinates.
2. Keep branch conditions simple and deterministic.
3. Compose displays from a few reusable fields.
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
- Some capability entries remain `candidate`/`planned` while not yet stable enough for global enabling.
- Optimization priority is always bounded by semantic safety and layout validity, then by cell budget.

This reference should be used as a working map while reading generated listings in explain/json mode: every named optimization corresponds to a concrete rewrite class that can be correlated with local sequences in emitted IR or final machine text.
