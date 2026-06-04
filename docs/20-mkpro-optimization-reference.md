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
Do not read every ID as a separate optimization mechanism: the public names are
leaf-level report markers, and several markers are different entry points into
the same generalized lowering strategy.

- `branch-removal` — removes an unnecessary branch when the needed value can be computed without a separate branch path.
- `arithmetic-if-select` — emits selected values through arithmetic formulas instead of `if/else`.
- `arithmetic-if-update` — performs conditional assignment in one path instead of two branches.
- `arithmetic-if-extrema` — replaces branching for `max/min` with a short arithmetic form.
- `zero-condition-test` — shortens checks such as `== 0` to a cheaper machine pattern.
- `dispatch-compare-chain` — compresses long compare-and-branch chains.
- `indirect-flow` — enables indirect jumps/dispatch when preconditions are proven.
- `indirect-memory-table` — reads the next-cell address through an indirect table instead of long absolute labels.
- `tail-call-lowering` — lowers tail calls to a shorter jump-based form instead of a full call frame.
- `vo-return-body-reorder` — candidate to move a subroutine return body so a `ПП/В/О` pair can collapse when layout allows.
- `return-zero-jump` — rewrites `return` as a short jump via cell `0`.
- `fl-decrement-branch` — compresses “decrement-and-jump” pattern into one block.
- `super-dark-dispatch` — uses FA..FF routing mode where a valid layout exists.
- `r0-alias-indirect` — allows R0 to be used as an indirect pointer when semantics stay safe.
- `r0-fractional-sentinel` — applies a fractional sentinel in R0 for a shorter jump path.
- `negative-zero-threshold-selector` — replaces range checks with a short threshold test.
- `x2-display-register` — saves cells/instructions in display by enabling X2 mode.
- `vp-fraction-restore` — restores VP quickly after arithmetic using a short path.
- `hex-mantissa-arithmetic` — simplifies arithmetic on hexadecimal mantissas.
- `fractional-indirect-addressing` — enables indirect memory/flow selectors that deliberately rely on MK-61 fractional-address behavior.
- `indirect-selector-integer-part-reuse` — reuses the integer-part side effect of a proved fractional indirect-memory selector and removes a redundant `К [x]`.
- `destructive-selector-operand-order` — schedules fractional uses of a packed selector before a commutative operand that will indirectly address through `int(selector)`.
- `error-stop-idiom` — compacts the common `error + stop` path.
- `kmax-zero-through` — optimizes `kmax` pattern by passing through zero and finishing immediately.
- `kzn-double` — applies `К ЗН` as a one-cell equivalent in specific doubling/sign-digit cases when the shape is proven safe on the exact MK-61 arithmetic profile.
- `kor-digit-test` — compresses digit-kind testing into a single check.
- `constants-dual-use` — reuses one computed constant result in two places.
- `packed-position-type` — packs position type state, reducing support code.
- `address-constant-overlay` — overlays one constant address on another without extra cells; `address-code-overlay` is the executable-byte form.
- `cyclic-address-layout` — reorders address layout so jumps become shorter.
- `dark-entry-layout` — re-layouts entry points to enable dark-entry transitions.
- `liveness-analysis` — analyzes live values and removes unnecessary storage.
- `interprocedural-value-propagation` — propagates known values across procedures.
- `interprocedural-dead-store` — removes writes that are never read even after call expansion.
- `dead-store-elimination` — removes unnecessary `store`/`recall` when not used.
- `last-x-reuse` — avoids rewriting X when the needed value is already there,
  while respecting `.`/`ВП` X2-sync boundaries and downstream stack consumers.
- `flow-x-reuse` — avoids recalls when all CFG predecessors already carry the same register value in X, with the same X2-sync and downstream stack-lift guards.
- `branch-target-x-reuse` — avoids the first recall in a branch target when the tested value is still in X and the target has no other entry, unless that recall supplies the target-side X2 sync or a stack lift that reaches a downstream consumer through direct call returns.
- `constant-folding` — precomputes constant parts before code generation.
- `cse-display-block` — merges identical display logic blocks.
- `jump-thread` — rewires jump chains into one direct jump path.
- `jump-to-next-threading` — removes intermediate jumps to the next label.
- `dead-code-after-halt` — removes code unreachable after `HALT`.
- `register-coalesce` — merges separate temporary cells when lifetime ranges do not overlap.
- `duplicate-failure-tail-merge` — merges identical error/failure tail sequences, including pause-only tails that display the incoming X value.
- `shared-terminal-tail` — jumps into an existing identical straight-line suffix that already ends in unconditional terminal flow.
- `shared-straight-line-helper` — extracts repeated non-terminal straight-line opcode bodies into one helper subroutine when the `ПП`/`В/О` cost is lower than duplicated inline code; a size-gated candidate extends this to bodies with direct `ПП` calls, and `multi-entry-straight-line-helper` can add internal entry labels for repeated suffixes of the same helper body.
- `arithmetic-if-pass` — a dedicated pass collecting all `arithmetic-if` opportunities.
- `redundant-prologue-elimination` — removes repeated identical prologues.
- `step-vs-run-verification` — chooses the more compact step/run verification form.
- `coord-list-scaled-decimal` — uses scaled coordinate lists for cheaper decimal handling.
- `dual-constant-sign-digit` — exposes dual-constant sign-digit intent coverage behind negative-zero threshold assumptions.
- `raw-display-5f` — selects a low-level rendering path using opcode `0x5F`.
- `super-number-deferred-normalization` — keeps extended super-number form when canonicalized normalization is not yet considered provably safe.
- `stack-resident-temps` — keeps short-lived temporaries on the X/Y/Z/T stack instead of spilling them to numbered registers when the stack-residency path is active.

Note:
- Internal compiler artifacts such as `#display-anchor`, `#display-literal-gap`, `neighbor_count`, and `line_count` are intentionally not listed as optimization IDs in this document.
- The method IDs listed in this reference are the ones emitted into `report.optimizations` / `report.optimizer`, while the values above are structural field/call labels used during lowering.

Capabilities can be `considered` and not active if no matching shape is found.

## 3a) Optimization-family generalization map

The optimizer intentionally keeps fine-grained names in `report.optimizations`
so explain/json output can be correlated with local source and IR shapes. For
implementation and tuning, many of those names fall into broader families:

- **Value residency and forwarding** — keeps already-available values in
  `X`/stack/known-zero locations instead of storing or recalling them again.
  Includes `last-x-reuse`, `flow-x-reuse`, `branch-target-x-reuse`,
  `condition-current-x-reuse`, `display-current-x-reuse`, `known-zero-reuse`,
  `zero-reuse`, `stack-resident-*`, and `z-stack-*`.
- **Stop/read/prompt fusion** — exploits the calculator stop contract where the
  shown value and read input are already on the stack. Includes
  `show-read-fusion`, `show-read-stack-stop-risk-lowering`,
  `loop-carried-prompt-*`, `show-read-guarded-transfer`,
  `x-param-stack-stop-risk-*`, and `entered-current-x`.
- **Domain/error trap lowering** — turns terminal error guards into
  self-trapping opcodes on the value already in `X`. Includes
  `domain-error-guard`, `assign-zero-domain-guard`,
  `indexed-assign-zero-domain-guard`, `decrement-underflow-domain-guard`,
  `decrement-zero-domain-guard`, `error-stop`, and related literal stop paths.
- **Counted-loop and decrement-counter lowering** — recognizes safe countdown
  forms and supplies the counter initial value from inline source, setup, or
  state normalization. Includes `state-init-counted-loop`,
  `setup-only-counted-loop-init`, `initialized-counted-while-loop`,
  `fl-decrement-zero-branch`, `indirect-incdec-counter`, and
  `r0-indirect-counter`. `counted-loop-unroll` is a separate strategy for
  small constant-trip loops.
- **Selector, indirect addressing, and hardware side-effect reuse** — plans
  selectors once, proves exact-machine behavior, and reuses destructive
  indirect side effects when legal. Includes `affine-indexed-selector-reuse`,
  `indexed-selector-cache`, `indirect-memory-alias-selector`,
  `fractional-indirect-addressing`, `indirect-selector-integer-part-reuse`,
  `destructive-selector-operand-order`, `stable-indirect-flow`,
  `preloaded-indirect-flow`, `runtime-indirect-call-flow`,
  `r0-fractional-sentinel`, and `super-dark-*`.
- **Helperization, shared bodies, and tail merging** — extracts or jumps into
  repeated byte sequences when the `ПП`/`В/О` or jump cost is lower than
  duplication. Includes `shared-terminal-tail`, `shared-straight-line-helper`,
  `shared-call-tail`, `return-suffix-gadget`, `duplicate-failure-tail-merge`,
  `expression-helper`, `near-any-helper`, `random-cell-helper`,
  `bit-mask-helper`, spatial helpers, display helpers, and show-sequence
  helpers.
- **Branch, dispatch, and residual normalization** — selects between
  branchless arithmetic, direct tests, residual reuse, compact dispatch, and
  branch-order variants. Includes `branch-removal`, `arithmetic-if-*`,
  `zero-condition-test`, `comparison-boundary-normalization`, `residual-*`,
  `dispatch-*`, `if-chain-dispatch-canonicalization`,
  `terminal-if-direct-branch`, and `if-branch-order-inversion`.
- **Mask, membership, spatial, and packed-grid reuse** — builds masks or
  coordinate-derived values once, then reuses them through scratch, stack, X2,
  or shared helpers. Includes `cell-membership-*`, `membership-mask-*`,
  `mask-stack-op-reuse`, `bit-mask-condition-helper`,
  `spatial-hit-condition-helper`, `bit-set-mask-cse`, `grid-cell-mask-cse`,
  and `packed-grid-primitive-lowering`.
- **Display strategy lowering** — chooses a display representation, prepares
  display inputs, reuses current stack state where possible, and helperizes
  repeated output bodies. Includes `screen-*`, `packed-display-*`,
  `display-byte-*`, `formatted-coord-report-*`, `display-current-x-reuse`,
  `display-stack-reuse`, and `show-sequence-helper`.
- **Setup, preloads, constants, and startup trade-offs** — balances shorter
  main code against setup cost and available registers. Includes
  `constant-synthesis`, `preloaded-constant`, `auto-preload-initial-state`,
  `duplicate-preload-store-reuse`, `startup-aware-constant-preloads`, and
  constant-demotion rescue candidates.

When adding a new reported name, prefer documenting it as a leaf under one of
these families unless it introduces a new proof model, machine feature, or
pipeline phase.

## 4) AST and source-level rewrites

These transformations run on source constructs before machine lowering:

- `constant-indexed-state-resolution` — if array/field index is known at compile time, substitutes the exact cell address directly.
- `affine-indexed-selector-reuse` — if an affine dynamic index such as `physical - 3` already evaluates to the physical register number for a contiguous bank member, uses that variable as the MK-61 indirect selector instead of allocating and filling a separate selector.
- `indirect-memory-alias-selector` — chooses the cheapest proved indexed-bank selector offset using the MK-61 two-digit indirect-memory register table, so values such as `17..19` or proved negative aliases can directly select bank registers and avoid a scratch selector or a larger arithmetic offset.
- `fractional-indirect-addressing` — if `bank[int(selector)]` targets a physically aligned contiguous bank and `selector` is already in `R7..Re`, uses that register directly as the indirect-memory selector. This relies on MK-61 indirect memory addressing ignoring the fractional tail, so packed coordinates can select by their integer part without an explicit `К [x]`. When the lowering proves that exact `int(selector)` form, it marks the indirect op so later IR passes may also reuse the selector register's post-indirect integer-part side effect.
- `destructive-selector-operand-order` — for deterministic commutative primitives such as `bit_and(table[int(coord)], bit_not(frac(coord)))`, evaluates the fractional operand first and leaves the destructive indirect-memory access last. This preserves the packed coordinate tail while still allowing `fractional-indirect-addressing` to use the coordinate register itself as the selector.
- `indexed-selector-cache` — when repeated dynamic bank accesses share the same index expression, reuses the cached selector directly or derives a sibling field selector by applying only the contiguous offset delta.
- `display-string-inline` — moves text templates directly into `show`, removing separate temporary definitions.
- `display-string-guarded-show` — hoists guarded string value selection into the display path when safe.
- `display-string-assignment-elimination` — deletes compile-time removable display-string assignments that only flow into later `show` inputs and are never consumed elsewhere.
- `display-edge-whitespace-trim` — removes leading/trailing whitespace around templates that does not affect display output.
- `expression-constant-folder` — precomputes constant expression subtrees.
- `entered-current-x` — consumes the currently keyboard-entered X value for the `entered()` builtin without emitting a second stop, clearing tracked X aliases because the value is already live in X.
- `show-read-fusion` — merges `show(...)` with a following `read`-based assignment/input path into one calculator `С/П`: `show(...); x = read()` or `show(...); x = int(read())` / `frac(int(read()))` forms share the same input stop and avoid emitting a second `С/П`.
- `running-display-preview` — lowers `preview(expr)` as expression preparation only, leaving the value visible without inserting a calculator `С/П`.
- `show-read-decrement-underflow-fusion` — merges `show -> input -> decrement -> if (counter < 0) ...` into one compact sequence, keeping input in `Y` across the decrement-underflow check.
- `show-read-stack-stop-risk-lowering` — a generalized "stack-stop fusion": when a single plain `show` source value (`stake`) is combined with a freshly read input across the stop, it keeps `stake` in `Y`, transforms the input in `X`, and computes the result directly on the stack with no input register. It recognizes any pure shape `wrap*( stake (op) g(input) )` where `op` is `+`/`-`/`*`/`/` (non-commutative ops keep `stake` on the left so they map to the machine's `Y op X` order), `g(input)` is a chain of single-argument X-transform intrinsics over the input (e.g. `sin`, `cos`, `tg`, `sqrt`) optionally fused with one single-digit additive/scaling constant, and `wrap*` is an outer chain of X-transform intrinsics (e.g. `int`, `frac`). The input leaf may be a direct `sin(read())` or a stored input field, avoiding a source-visible throwaway field. The classic `int(stake * (1 + sin(read())))` robber-fight idiom is the canonical case and lowers byte-for-byte identically; the generalization never grows a program because every accepted form reuses the same kept-in-`Y` stack sequence.
- `loop-carried-prompt-x` — for loops shaped as `show(screen); key = read()` where every non-terminal branch assigns the next `screen`, removes the register-backed prompt state and leaves the next visible value in `X` for the loop-back stop. If the prompt starts from `stack.X` / `stack.Y`, an allocated sibling field initialized from the same stack slot can seed the first prompt.
- `loop-carried-prompt-input-branch` — after a loop-carried prompt stop, branches directly on the read key with no extra store when the branch condition consumes only that input.
- `loop-carried-prompt-input-dispatch` — after a loop-carried prompt stop, dispatches directly on the read key with no intermediate variable, while preserving the prompt flow across loop back-edge.
- `loop-carried-prompt-decrement-underflow` — after a loop-carried prompt stop, handles `resource--; if resource < 0 ...` patterns by checking underflow in-line. It keeps the input value in `Y`, emits `F x<0` branch flow, and restores `X/Y` state only where required for the next prompt consumer.
- `show-read-guarded-transfer` — rewrites `show; x=input; decrementTarget -= x; if decrementTarget < 0 { halt } ; incrementTarget += x; if incrementTarget < 0 { halt }` into one stack-based sequence that keeps the read value on the stack across both guarded updates.
- `counted-loop-unroll` — replaces small constant-trip counted `while` loops with explicit per-iteration copies when the induction variable updates are simple linear steps and entry values are known constants; this removes the loop machinery and registers update logic.
- `counted-loop-unroll-free-scratch` — runs counted-loop unrolling together with residual-dispatch scratch freeing (`freeResidualDispatchScratch`) as one candidate.
- `state-init-counted-loop` — recovers the compact one-cell `F Lx` counted-loop lowering for countdown loops whose counter carries its initial value on the state field (`time: counter 0..N = N` + `while time >= 1 { …; time-- }`). When that counter is used solely by the loop in the top-level entry body, the state initializer is rewritten into an explicit `time = N` immediately before the loop, matching the already-supported explicit-init form byte-for-byte while staying re-runnable (the inline store re-arms the counter on every `С/П`, unlike a setup-only preload).
- `setup-only-counted-loop-init` — speculative companion to `state-init-counted-loop`: keeps the countdown initializer in the generated setup program and still emits the compact `F Lx` loop. This mirrors hand-entered MK listings whose loop counter is loaded before the main program starts; considered only in size-rescue compiles and selected only when the whole program shrinks.
- `initialized-counted-while-loop` — compiles `x = N; while x >= 1` / `> 0` loops with `x--` in the last body statement into compact `F Lx` loops when the pattern is safe (intervening statements do not touch `x`, loop body has non-terminating tail, and the loop register has an `F Lx` opcode).
- All three counted-loop init strategies above (`state-init-counted-loop`, `setup-only-counted-loop-init`, `initialized-counted-while-loop`) share one loop recognizer (`recognizeCountedWhileLoop` over `unitDecrementCountedWhileTarget`) and one emit tail (`emitCountedWhileBody`): they accept the same equivalent condition spellings (`x >= 1`, `x > 0`, `1 <= x`, `0 < x`) and differ only in how the counter's starting value is supplied (inline store, setup preload, or state-field initializer normalized to an inline store). `counted-loop-unroll` is a separate family that targets ascending `while v < bound` / `<= bound` loops, not the unit-decrement countdown.
- `domain-error-guard` — replaces a terminal-error guard (`if <expr> <op> 0 { halt("ЕГГОГ") }`, including a call to a proc whose body is just that trap) with a single self-trapping domain opcode that raises ЕГГОГ exactly on its mathematical domain (all proved on hardware in `tests/emulator/trap-opcodes.test.ts`): `F √` for `<` (traps iff X < 0), `F lg` for `<=` (iff X <= 0), and `F 1/x` for `==` (iff X == 0, division by zero — the exact equality trap regardless of sign). `>`/`>=` reduce to the swapped difference. The guard computes the comparison difference into X so the trap fires iff the condition holds; otherwise it falls through into the false branch. This collapses the compare + conditional branch + shared trap into one cell, and when every caller of a shared trap proc is converted the proc becomes dead and is dropped. Speculative (`domainErrorGuards`): adopted only when the whole program ends up smaller. Examples: rambo-iii 139→135, alaram 80→76, dungeon 85→83, wumpus 105→103.
- `indexed-assign-zero-domain-guard` — extends the adjacent store+trap fusion to dynamic indexed stores. After `cells[i] = expr`, `К X→П i` leaves the stored value in X, so an immediate `if cells[i] <op> 0 { halt("ЕГГОГ") }` can emit the self-trapping opcode directly without a redundant `К П→X i`. It now shares the same comparison→opcode table as the scalar guards (`planDomainErrorGuard`): `<`→`F √`, `<=`→`F lg`, and `==`→`F 1/x` (the equality trap the earlier bespoke indexed table could not express). All store-then-trap fusions (`domain-error-guard`, `assign-zero-domain-guard`, `indexed-assign-zero-domain-guard`, and the unit-decrement guards) emit their trap through one shared `emitDomainTrapOnX` tail.
- `assign-zero-fallback-store` — defers the register store for `x = expr; unless x { x = fallback }` until after the zero fallback. The branch tests the just-computed X value, emits the fallback only on the zero path, and performs one shared `X→П`.
- `prior-random-branch-stack-reuse` — for `old = random_state; random_state = random(); if old - random_state < 0 ...`, keeps the old random in Y while storing the new random, then branches on the subtraction without spilling `old`.
- `prior-random-fractional-decrement` — recognizes guarded fractional decrements of the form `old=random_state; random_state=random(); step=int((old+random_state+1)*factor*amount/divisor)/scale; if frac(amount)-step <= 0 trap else amount -= step`. It keeps `frac(amount)` and the old random on the stack, and reuses a just-stored amount in X when the source flow leaves one there.
- All three prior-random idioms (`prior-random-stack-reuse`, `prior-random-branch-stack-reuse`, `prior-random-fractional-decrement`) share one recognizer preamble (`matchPriorRandomSeedUpdate`) and one kept-in-Y head (`emitPriorRandomSeedUpdate`): they match `old = seed; seed = random()` — written inline or as a call to a one-statement random proc, which is inlined — and emit `recall seed; В↑; К СЧ; store seed` so the previous value stays parked in Y for the following stack-direct consumer. They differ only in how that consumer (a pure expression, a `<` branch, or a guarded fractional decrement) reads Y and the new X. `show-read-guarded-transfer` emits its negative-balance trap through the shared `emitDomainTrapOnX` tail (the same `F √` self-trap used by the store-then-domain-trap fusions).
- `decrement-zero-domain-guard` — when a unit decrement is followed by a terminal `x == 0` error guard and no compact `F Lx` counter opcode is available, stores the decremented value and uses `F 1/x` as the zero trap.
- `startup-aware-constant-preloads` — tries a variant that leaves setup-expensive synthesized constants inline, such as decimal powers built with `F 10^x`, when that lowers estimated startup+program cost without increasing the main program size.
- `intent-read-lowering` — inlines direct `read()`-driven arguments when they are used to initialize x-param stake/sin procs and related intent states.
- `intent-domain-lowering` — normalizes special intent types into a base form for later compilation.
- `packed-counter-stripes` — packs dense counters into a shorter representation.
- `x-param-state-elision` — removes redundant transition states when rule/function parameters are consumed directly from `X`.
- `tail-copy-assignment-fusion` — merges copy assignments in tail blocks into one write pass.
- `if-chain-dispatch-canonicalization` — turns long `if` / inverted `if !=` chains that test the same deterministic expression against constants into a single dispatch template.
- `constant-guarded-call-inline` — inlines a guarded call when used once and safe.
- `common-branch-tail-hoisting` — merges identical tails from similar branches.
- `single-use-tail-inline` — inlines a one-time tail instead of emitting a separate call.
- `expression-helper` — builds a shared helper for a pure, expensive expression when repeated use count makes it profitable after cost gating.
- `expression-helper-call` — replaces repeated inline compilation of the same pure expression with a helper call (`ПП`) when that helper already exists.
- `near-any-helper` — emits a shared helper for `near_any`-style checks that computes absolute deltas and compares against a precomputed radius.
- `repeated-x-param-self-assignment` — for consecutive `x = f(x)` / `x = f(x)` (or indexed equivalents) on the same target, emits two x-param calls in one X-based chain and stores once.
- `single-use-guard-substitution` — removes a one-shot assignment if it can be substituted directly into a following condition and the lowered cost is strictly lower.
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
- `residual-guarded-update` — compacts guarded self-updates such as `if x >= N { x -= N }` by reusing the comparison residual already left in X. The same residual is now exposed to the first statement of the opposite branch when that statement consumes the exact `x - N` value.
- `branch-residual-x-reuse` — after an ordinary comparison branch, reuses the residual left in X for the first branch statement when it is the same expression (`show(expr)`, `pause(expr)`, `halt(expr)`, or `target = expr`). Special condition lowerings with different X contracts, such as small-set helpers and remainder-zero tests, are excluded.
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
- `dead-proc-elimination` — removes unreachable lowered procedures after `match/effect` pass by collecting reachability from entries and call sites.
- `ephemeral-input-branch` — shortens one-off input paths into compact branches.
- `ephemeral-input-dispatch` — chooses input-based dispatch through denser tables.
- `decrement-underflow-branch` — decrements and immediately handles underflow in one step.
- `decrement-underflow-domain-guard` — fuses unit decrement and terminal `halt("ЕГГОГ")` underflow paths through `F sqrt` when the branch target is exactly a one-cell domain-error stop.
- `fl-decrement-zero-branch` — a dedicated “decrement and test zero” sequence in one short block.
- `one-based-modulo-normalization` — for a proved non-negative scalar, folds
  `x = frac(int(x) / N) * N; if x <= 0 { x += N }` into
  `frac((int(x) + N - 1) / N) * N + 1`. The non-negative range proof is required:
  MK-61 `К {x}` keeps a negative fractional sign, so the formula is not sound for
  signed/unknown packed temporaries.
- `if-branch-order-inversion` — reorders branches so downstream lowering is shorter.
- `x-preserving-false-branch` — preserves current X value in the false branch.
- `x-preserving-fallthrough-branch` — preserves current X value in the true
  fallthrough branch after a direct zero-test when the first branch statement
  immediately consumes the same scalar (for example `halt(x)`/`pause(x)`).
- `equality-zero-fallthrough` — marks the true branch of a simple `a == b`
  comparison as having zero already in X, so `halt(0)`, `pause(0)`, and similar
  immediate zero consumers do not materialize a fresh zero.
- `inequality-zero-false-branch` — marks the false branch of a proved `expr != 0`
  test as having zero already in X, covering the same immediate zero consumers on
  the branch target.
- `current-x-negated-zero-test` — for `x <= 0` / `x > 0` shapes where `x` is
  already in X, emits a single sign flip and the normal direct zero-test instead
  of materializing `0 - x` through `0; П→X x; -`.
- `small-set-condition-lowering` — lowers small `set` conditions to compact code.
- `cell-membership-clear-reuse` — reuses a computed membership mask when clearing the same assignable bit collection, including indexed bank cells with prepared indirect selectors, and eliminates duplicate mask construction.
- `cell-membership-set-reuse` — reuses a computed membership mask when setting one cell in an `if` suffix.
- `cell-membership-mask-run-reuse` — extends membership mask reuse across a short run of set updates.
- `membership-mask-current-x-scratch` — when a membership mask expression is
  already in current X, stores the reusable scratch copy directly from X instead
  of recalling the mask register first.
- `membership-mask-stack-test-reuse` — after saving such a reusable mask
  scratch, keeps the same mask in the stack for a simple collection load and
  emits the membership `К ∧` without recalling the scratch for the test itself.
- `mask-stack-op-reuse` — applies the same stack-resident scratch-mask idea to
  adjacent `bit_set`/single-bit/grid-mask helper paths, skipping the first
  scratch recall when the collection side is a simple stack load.
- `bit-mask-condition-helper` — lowers `bit_has(mask, index)` comparisons through a shared bit-mask helper (`ПП` + test opcode).
- `spatial-hit-condition-helper` — routes `bit_has(...)` conditions through the shared spatial-hit helper path.
- `near-any-helper-lowering` — replaces near-threshold comparisons with a shared near-any helper when helper statistics show lower total cost.
- `remainder-zero-test-lowering` — lowers `%` comparisons to zero into quotient/fraction checks with one direct zero test.
- `residual-elseif-compare` — fuses deterministic `if/else if` compare chains into one base compare plus residual adjustment.
- `condition-current-x-reuse` — if one compare operand is already in X and the other is a simple stack load, emits compare directly without reloading.
- `negative-zero-threshold-flow` — emits preloaded threshold-flow test through negative-zero selector machinery for tighter `>= / <` checks.
- `assign-zero-domain-guard` — when a scalar assignment is directly followed by a terminal error check (for example `x <op> 0`), fuses the assignment and trap branch into one domain-guard opcode using the same register value in X.
- `error-stop` — uses the dedicated one-cell `ЕГГ0Г` error-stop path for literal terminal halts when supported, bypassing generic literal-stop lowering.
- `terminal-literal-stop` — lowers supported literal terminal halts through the dedicated literal terminal path and records this compact terminal stop strategy.

Machine-level variants around branches:

- `tail-call-lowering` — rewrites final calls into a tail-safe short form.
- `tail-branch-inversion` — flips the branch condition when shorter.
- `tail-call-layout` — reorders tail calls to fit better in layout.
- `function-tail-call` — does the same for function tail calls by converting call to direct jump.
- `function-tail-recursion` — when a function tail-calls itself, emits a loop.
- `terminal-rule-tail-call` — turns final rule calls into direct jumps.
- `terminal-loop-screen-elision` — removes terminal `show` duplicates already provided by the following loop header and may inline one-screen loop-header helpers before input.
- `return-suffix-gadget` — shares a common suffix after `return` across similar regions.
- `shared-call-tail` — keeps one shared tail after calls instead of duplicates.
- `shared-straight-line-helper` — turns repeated straight-line opcode runs into
  one helper body with `В/О`, covering the general non-terminal form of "enter a
  shared body, then continue at the original call site." The
  `shared-call-body-helper` whole-program candidate also lets such bodies contain
  direct `ПП` calls, and is adopted only when the final program shrinks.
  `multi-entry-straight-line-helper` reuses suffixes of an already-selected
  helper by adding internal entry labels instead of allocating another helper.
- `jump-thread` — rewires jump chains into a straight flow.
- `jump-to-next-threading` — removes jumps that only go to the next label.
- `redundant-prologue-elimination` — merges repeated prologues while preserving side effects.
- `repeated-unary-update-arg-temp` — routes the argument of repeated single-argument X-transform intrinsic calls (the `pow10`/`sqr`/`int`/`frac`/`sin`/… family, not just `pow10`) through one hidden scratch when that exposes shorter shared straight-line tails than spelling each argument inline. Statements are grouped by their structure modulo the routed argument and constant array indices, so a repeated shape is canonicalized even when its occurrences are not adjacent.

## 5a) Candidate variants and layout re-trials

The `report.candidates` array in `report` shows lowerings that were recompiled and scored during best-fit selection, then one entry is marked `selected`. These are distinct from always-on `report.optimizations` entries.

Many candidate IDs are generated from a small set of orthogonal axes rather
than being independent rewrites. Read names such as
`domain-error-guards-unroll-setup-counted-loop-<layout>` as "enable the
domain-error rewrite, enable counted-loop unroll, allow setup-counted-loop
initialization, then try a specific procedure layout." The common axes are:

- **Feature toggles**: `domain-error-guards`, `show-read-guarded-transfer`,
  `counted-loop-unroll`, `setup-counted-loop`, `stack-resident-temps`,
  `x-param-value-function`, `packed-counter-stripes`, and
  `inline-floor-packed-row-expression`.
- **Canonicalization/support toggles**: `free-residual-dispatch-scratch`,
  `if-chain-dispatch-canonicalization`, `repeated-unary-update-arg-temp`,
  `coalesce-copies`, and constant preload demotion/reclamation.
- **Layout axes**: late terminal-if variants, branch-order trials, procedure
  order (`call-count`, `size-asc`, `size-desc`, `reverse`), helper/procedure
  hoisting, and tail-branch inversion.
- **Helper-sharing axes**: random-cell, bit-mask, straight-line body, sibling
  proc, and call-body helper extraction.

The long candidate names are kept because they make the selected compile path
unambiguous in reports; the generalized model is the axis combination above.

This axis model is realized by a declarative candidate composition engine in
`compileMKPro` (`src/core/compiler.ts`):

- Each speculative variant is a `CandidateSpec` — a `LoweringOptions` flag bundle
  plus a stable name/detail and a gate (`always`, `sizeRescue`, or `unaryArg`).
- `enumerateStaticCandidateSpecs` yields the source-determined specs (including
  the proc-layout combinations generated from `PROC_LAYOUT_MODIFIERS`) in a fixed
  order; the gates reproduce the former `trySizeRescueCandidate` /
  `tryUnaryArgCandidate` conditions, so the candidate set, order, and names are
  unchanged from the earlier hand-written sequence.
- Two probe-driven providers (`reclaimCoalescedPreloadCandidates`,
  `demoteConstantIndirectFlowCandidates`) build extra specs from trial compiles
  at fixed positions.
- `enumerateExpansionCandidateSpecs` fills the remaining matrix holes by crossing
  the size-reducing bundles that were never combined with a placement strategy
  (currently `stack-resident-temps` and `shared-bit-mask-helper`) with the
  proc-layout modifiers. It runs only in the rescue regime — when the standard
  search still leaves the program over the 105-cell window — so in-budget
  programs never pay for the broader search and stay byte-identical.

Because selection (`selectBest`) is minimum cell count, broadening the candidate
set can only keep a program the same size or shrink it, never grow it. A
no-regression guard (`npm run examples:check`,
`scripts/assert-no-size-regression.mjs`) enforces `steps <= baseline` against the
shared baselines in `tests/compiler/example-baselines.ts`.

- `late-layout-if-variant` — re-runs lowering with an aggressive terminal-if lowering variant after full layout.
- `late-layout-branch-order` — re-runs with swapped terminal-if branch order after full layout.
- `late-layout-if-branch-order` — combines aggressive terminal-if and branch-order re-runs after full layout.
- `break-even-indirect-call` — hoists procs/shared helpers and evaluates a guarded indirect-call candidate to collapse repeated direct calls into one-cell indirect flow.
- `hoisted-helper-indirect-layout` — hoists shared helpers before re-layout and recompiles for better preloaded indirect flow.
- `hoisted-proc-indirect-layout` — additionally hoists ordinary procedures before re-layout for tighter call/jump sequences.
- `if-chain-dispatch-canonicalization` — rechecks constant if/else-if dispatch shape under a full-layout candidate pass.
- `free-residual-dispatch-scratch` — frees residual dispatch scratch in a candidate pass.
- `alias-x-reuse` — tests value reuse of X at scalar sites for cleaner candidate control-flow.
- `coalesce-copies` — enables copy coalescing candidate before final layout scoring.
- `parametric-sibling-proc` — synthesizes one-parameter sibling helpers and reruns full layout around them.
- `free-residual-dispatch-scratch-with-if-chain` — combines scratch-freeing and if-chain canonicalization as one candidate.
- `share-random-cell-helper` — candidates around shared random-cell helper extraction.
- `share-random-cell-helper-hoisted` — same random-cell-sharing candidate with front-hoisted helpers enabled.
- `late-layout-tail-branch-inversion` — tests tail-branch inversion as a late-layout candidate.
- `hoisted-helper-if-chain-tail-branch-layout` — tests helper hoisting + if-chain canonicalization + tail-branch inversion as one candidate.
- `guarded-prologue-gadget-layout` — candidate for guarded prologue gadget extraction after full layout.
- `guarded-prologue-hoisted-proc-layout` — same with hoisted helper/procedure pre-layout.
- `shared-bit-mask-helper-layout` — candidate that enables shared bit-mask helper calls after full layout.
- `shared-bit-mask-helper-hoisted-layout` — same with hoisted helpers enabled.
- `signed-abs-match-pair` — candidate for signed abs/sign normalization on match-pair patterns.
- `signed-abs-shared-bit-helper-hoisted-layout` — combines signed abs/sign candidate with hoisted bit-mask helper calls.
- `signed-abs-shared-bit-helper-hoisted-proc-layout` — combines signed abs/sign candidate with hoisted helper/procedure layout.
- `packed-counter-stripes` — candidate that packs compatible fixed-width counters into one striped register.
- `repeated-unary-update-arg-temp` — candidate that routes repeated X-transform unary-call arguments through one hidden scratch so repeated helper tails can be shared; only attempted when a cheap structural scan finds a routable-unary shape that repeats within some statement list.
- `x-param-value-function` — candidate that passes simple positive-modulo value-function arguments through `X` instead of allocating a parameter register.
- `x-param-value-function-with-unary-arg-temp` — combines X-parameter value functions with repeated unary-call scratch canonicalization.
- `x-param-value-function-unary-arg-temp-coalesce` — additionally enables copy coalescing for the same value-function / unary-call scratch shape.
- `x-param-unary-arg-shared-call-hoisted-proc` — combines X-parameter value functions, repeated unary-call scratch canonicalization, shared call-body helper extraction, and front-hoisted helper/procedure layout. This lets repeated straight-line bodies that contain direct calls become one hoisted helper after their arguments have been made structurally identical.
- `packed-counter-stripes:<id+id+…>` — parameterized variant for each packed stripe set combination.
- `counted-loop-unroll` — candidate that fully unrolls small constant-trip counted loops.
- `startup-aware-constant-preloads` — candidate balancing main/ setup constant trade-offs.
- `counted-loop-unroll-free-scratch` — combines counted-loop unrolling with residual-dispatch scratch freeing.
- `stack-resident-temps` — recompiles with stack-temporary fusion enabled (`<=4` temps lifted with `В↑`) to avoid `X->П`/`П->X` spills.
- `stack-resident-temps-hoisted` — same stack-temp fusion candidate with shared helper hoisting enabled.
- `stack-resident-temps-hoisted-proc` — same stack-temp fusion candidate with helper and procedure hoisting enabled.
- `stack-resident-temps-setup-counted-loop` — combined stack-temp fusion with setup-only counted-loop initializers.
- `domain-error-guards` — candidate that rewrites terminal `halt("ЕГГОГ")` style checks to self-trapping domain opcodes.
- `domain-error-guards-unroll` — combines domain-error candidate with counted-loop unrolling.
- `domain-error-guards-setup-counted-loop` — combines domain-error rewriting with setup-only counted-loop initializers.
- `domain-error-guards-unroll-setup-counted-loop` — combines domain-error rewriting with counted-loop unrolling and setup-only counted-loop initializers.
- `domain-error-guards-setup-counted-loop-stack-temps` — combines domain-error rewriting with setup-only counted-loop initializers and stack-temporary residency.
- `show-read-guarded-transfer` — candidate that tries stack-resident read/decrement/increment guarded update fusion.
- `show-read-guarded-transfer-unroll` — combines stack read/decrement/increment guarding with counted-loop unrolling.
- `show-read-guarded-transfer-setup-counted-loop` — combines read/decrement/increment guarded transfer with setup-only counted-loop initializers.
- `show-read-guarded-transfer-unroll-setup-counted-loop` — combines guarded read/decrement/increment transfer with counted-loop unrolling and setup-only counted-loop initializers.
- `call-count-proc-layout` — procedure reordering by descending call count.
- `size-asc-proc-layout` — procedure reordering from smallest to largest.
- `size-desc-proc-layout` — procedure reordering from largest to smallest.
- `reverse-proc-layout` — procedure reordering in reverse source order.
- `call-count-proc-layout-hoisted` — same as above plus front-hoisted procs/shared helpers.
- `size-asc-proc-layout-hoisted` — size-ascending procedure order with front-hoisted procs/shared helpers.
- `size-desc-proc-layout-hoisted` — size-descending procedure order with front-hoisted procs/shared helpers.
- `reverse-proc-layout-hoisted` — reverse procedure order with front-hoisted procs/shared helpers.
- `domain-error-guards-unroll-<layout>` — generated for each proc-layout candidate (`call-count`, `size-*`, `reverse`) combining domain-error rewrite and counted-loop unroll under full layout.
- `domain-error-guards-unroll-setup-counted-loop-<layout>` — generated for each proc-layout candidate (`call-count`, `size-*`, `reverse`) combining domain-error rewrite, counted-loop unroll, and setup-only counted-loop initializers.
- `show-read-guarded-transfer-<layout>` — generated for each proc-layout candidate (`call-count`, `size-*`, `reverse`) combining guarded read/decrement/increment fusion with that proc ordering.
- `inline-floor-packed-row-expression` — computes floor-packed display rows inline to reduce hidden display pressure.
- `inline-floor-hoisted-proc-tail-layout` — combines inline floor-row lowering with helper/proc hoisting and tail-branch inversion.
- `reclaim-coalesced-preloads` — compiles with forced coalesce-induced register sharing to free constants for preload allocation.
- `demote-constant-indirect-flow` — recompiles with selective integer constant inlining to free registers for post-layout indirect-flow rescue.
- `demote-constant-chain-indirect-flow` — repeatedly suppresses integer preloads (depth-limited) and recompiles to keep a non-dynamic register free for post-layout indirect-flow rescue.

## 6) Function and call lowering

- `function-call` — lowers a normal call into a short machine form with shared helper and return handling, removing unnecessary call/return steps.
- `function-call-lifting` — lifts direct call sites when safe, simplifying straightforward calls.
- `x-param-proc-entry` — alternative procedure entry through X when cheaper. The
  first assignment may be a direct copy, `param + other`, or a pure expression
  that consumes the single-use parameter from the X register through a
  left-deep/commutative stack-safe expression shape, so the parameter itself
  does not need a storage register.
- `x-param-proc-call` — passes parameters through X with fewer instructions.
- `x-param-return-decay` — prepares a return path through X for safe reuse afterward.
- `x-param-return-decay-call` — applies the same X-return pattern at call sites.
- `x-param-stack-stop-risk-read` — compiles a single-argument x-param helper proc shaped as `show(param); return wrap*( param (op) g(read()) )` so it consumes its argument from X and returns through direct `В/О`, reusing the same generalized stack-stop fusion prepared by `show-read-stack-stop-risk-lowering` (any X-transform intrinsic, binary op, single-digit constant, and outer wrap chain — not only `int(param * (1 + sin(read())))`).
- `x-param-stack-stop-risk-call` — compiles a one-argument call into the matched stake/sin helper procedure by passing its argument in X, based on a strict two-statement body shape (`show` of the argument source, then `return int(arg * (1 + sin(read())))` in equivalent forms).
- `x-param-value-function` / `x-param-value-function-call` — compiles a one-argument value function shaped like positive modulo normalization (`value = frac(int(value) / N) * N; if value <= 0 return value + N; return value`) so the argument is consumed from `X` and the result is kept in a hidden scratch plus `X`.
- `x-param-value-call-temp-reuse` — when a nested value-function call must be lifted to protect the MK-61 stack, reuses the same hidden scratch instead of allocating fresh `__mkpro_call_*` temporaries.
- `x-param-value-scratch-store-elision` — skips the caller-side `X->П scratch` after `scratch = normalize(...)` because the X-param value function already updated that scratch internally.
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
- `preloaded-indirect-flow` — preloads selector/address once so multiple indirect jumps become shorter. The post-layout pass is not started for already in-budget programs, but if a program needed indirect-flow rescue it keeps applying all further proved shrinking rewrites instead of stopping at the first <=105 result.
- `runtime-indirect-call-flow` — for repeated backward helper calls with legal numeric targets, initializes a dead stable register once at runtime and replaces direct `ПП addr` pairs with one-cell `К ПП r` calls.
- `preloaded-super-dark-flow` — super-dark path with a preloaded indirect target.
- `indirect-incdec-counter` — lowers a unit `x++`/`x--` through the indirect pre-increment (R4..R6) or pre-decrement (R0..R3) side effect of `К П->X r`, a one-cell true `±1` that correctly reaches 0 (used as the standalone unit-decrement form).
- `r0-indirect-counter` — uses R0 as a readable counter/switch for jump dispatch where provably safe.
- `indirect-memory-table` — builds a compact address table in memory and jumps by index.
- `indirect-memory-alias-selector` — lets indexed-bank lowering use non-linear register aliases from the two-digit indirect-memory table, including negative selector values after a full per-bank-element proof, when that removes selector arithmetic or an allocated selector scratch.
- `indexed-packed-row-table` — stores packed rows/cells in an addressable table for dense display access.
- `coord-list-scaled-read` — reads coordinates via scaled index, removing runtime decode work.
- `coord-list-scaled-decimal-storage` — same as above but decimal form, using fewer cells.
- `fractional-indirect-addressing` — allows indirect access through fractional address arithmetic when proofs are available, including direct `bank[int(selector)]` memory selectors.
- `indirect-selector-integer-part-reuse` — after a proved `bank[int(selector)]` indirect-memory access through a stable `R7..Re` selector, tracks that the selector register now holds the truncated integer part and deletes a later redundant `П->X selector; К [x]` pair's `К [x]` cell. The pass requires the proof marker emitted by fractional indexed lowering; unmarked hex, negative, or otherwise opaque indirect selectors are left alone.
- `destructive-selector-operand-order` — when a commutative expression has one operand that can use direct `bank[int(selector)]` indirect addressing and the other operand still needs the same selector's fractional tail, schedules the fractional operand first so the MK-61 selector mutation happens only after that tail is safely on the stack.
- `r0-fractional-sentinel` — uses a fractional-state sentinel in R0 to steer tables and to replace proved direct flow to address 99 (`БП`, `ПП`, or `F x?0`, numeric or post-layout label-resolved) with one-cell `К БП/К ПП/К x?0 0` when the R0 mutation is dead. The R0-fractional proof is preserved through unrelated indirect memory operations, but not through indirect flow.
- `super-dark-dispatch` — enables FA..FF range routing for shorter jumps with strictly valid address neighborhoods.

## 8) Spatial and coordinate-list optimization family

- `setup-coord-list-indirect-random-unique` — builds random-unique coordinate lists via indirect access to save layout.
- `coord-list-line-count-formatted-report-fusion` — merges line-count report construction with subsequent output.
- `coord-list-line-count-formatted-report-body` — extracts a shared report body for reuse.
- `coord-list-fused-formatted-report-body` — joins multiple report-building stages into one sequence.
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
- `single-bit-mask-op` — materializes a single-bit mask in scratch once, then applies `К ИНВ`, `К ∧`, or `К ∨` style operations.
- `bit-mask-helper` — emits shared helper routines that build bit masks from indices once per scratch register.
- `bit-mask-helper-call` — routes repeated `bit_mask` construction through existing helper labels instead of recompiling.
- `bit-mask-quotient-reuse` — reuses previously computed quotients/parts for mask generation.
- `grid-cell-mask-cse` — removes repeated 4x4 packed grid cell-mask calculations for adjacent membership/set operations.
- `indexed-packed-pow10-delta` — updates a dynamically indexed packed digit bank by a `pow10(...)` term without recompiling the whole self-update expression.

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
- `formatted-coord-report-lowering` — compact output for formatted-coordinate reports. The recognizer now captures any `<literal> cell:width <literal> bearing:width` report shape and gates it on a verified layout descriptor (`VERIFIED_COORD_REPORT_FORMATS`): the video mask, the cell-scale exponent, and the video-anchor exponent are all read from that descriptor instead of scattered constants. The mask and exponents are hardware-fitted to the exact separators and field widths, so only on-hardware-verified layouts (currently the `--CC--N` screen: prefix `--`, cell width 2, separator `--`, bearing width 1, mask `8,-00--_`) lower through the video path; any other shape falls back to the generic per-item display lowering. Adding a new report layout is a data-only entry once its mask is verified.
- `formatted-coord-report-packed-body` — compresses report body into packed format.
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
- `prior-random-stack-reuse` — when source keeps a previous seed, updates the same seed with `random()`, and consumes both previous and current values in a pure expression, keeps the previous value on the stack instead of storing and recalling it.
- `random-cell-helper` — extracts shared random-cell handling into one helper.
- `random-cell-helper-call` — calls the extracted helper instead of repeating logic.
- `coord-list-scaled-read` — in random coordinate paths, reduces table-unfold cost.
- `remainder-fraction-lowering` — chooses quick modulo paths through fraction operations.

## 11) Arithmetic and operator normalization

- `small-set-primitive-lowering` — replaces small multi-way boolean/state sets with dense arithmetic chains.
- `packed-grid-primitive-lowering` — maps packed grid and digit helper operations into bit masks and add/sub-style forms. The square-board helpers are width-parametric: the coordinate wrap (`grid_norm` / `positiveGridNorm`, i.e. `% width`) and the right-diagonal fold (`+ width`) derive exactly from the board width and default to the shipped 4-wide grid. The fractional cell-mask packing constant (`10^x + floor(10^(y * K_width))`) is hardware-fitted per width — its digits encode each row's collision-free nibble offsets — so it lives in a verified width-keyed table (`cellMaskRowConstant`) with only the on-hardware-verified `width: 4` entry. Other widths derive their structural macros automatically but require a verified fractional constant before they can lower (`cell_mask` refuses to fabricate one), the same honest limit as the decimal-series emitter.
- `reciprocal-division-lowering` — lowers `1 / x`-form divisions into `F 1/x` after evaluating the right side once.
- `arithmetic-if-update` — turns conditional updates into arithmetic form instead of branching.
- `arithmetic-if-conditional-move` — replaces conditional `move`/copy with arithmetic form.
- `arithmetic-if-sign-toggle` — routes sign handling through arithmetic when it shortens branches.
- `arithmetic-if-abs` — converts absolute value to branchless arithmetic.
- `arithmetic-if-max` — computes max using a branchless path.
- `arithmetic-if-min` — computes min using a branchless path.
- `min-via-max-lowering` — rewrites source-level `min(a, b)` into a max-based normalized expression that uses the existing `К max` primitive path.
- `quirk-free-minmax-lowering` — rewrites source-level `safe_max(a, b)` and `safe_min(a, b)` into explicit arithmetic forms to avoid the MK-61 `К max` zero-is-greatest quirk:
  `safe_max = (a + b + abs(a - b)) / 2`, `safe_min = (a + b - abs(a - b)) / 2`.
  Requires both operands to be pure and duplicable.
- `pow-square-lowering` — rewrites `pow(x, 2)` into `F x^2`.
- `pow10-opcode-lowering` — rewrites `pow(10, n)` into `F 10^x`.
- `square-expression-lowering` — rewrites pure repeated multiplication `x * x` into `F x^2`.
- `arithmetic-if-double-clamp` — special double-check clamp in one arithmetic template.
- `arithmetic-if-comparison-mask` — builds comparison masks without explicit `if`.
- `arithmetic-if-boolean-algebra` — lowers complex boolean comparisons into masks and arithmetic.
- `hex-mantissa-arithmetic` — simplifies hex mantissa operations, lowering instruction count.
- `negative-zero-threshold-selector` — threshold check for `-0`/`0` when it reduces branches.
- `decimal-series-lowering` — emits a hand-tuned, hardware-verified decimal recurrence listing for a factorial-like `decimal_series` declaration. The parser now reads `digits` and `counterStart` from the source (no longer hardcoding `counterStart = 65`), and the emitter looks the pair up in a `VERIFIED_DECIMAL_SERIES_LISTINGS` table keyed by `(digits, counterStart)`. Each entry carries its full validated byte sequence — these recurrences are hand-tuned for the MK-61 and cannot be derived parametrically, so an unverified pair fails with a clear diagnostic that lists the verified pairs rather than fabricating bytes. The `(94, 65)` listing is currently the sole verified entry; adding another precision is a data-only table addition once its sequence is validated on hardware. **Note:** this is recognition/structure cleanup, not a true byte-level generalization — the recurrence bytes still come from a verified table, by design.

## 12) Register allocation and liveness-driven memory trims

- `interprocedural-value-propagation` — propagates known constants/values across function calls.
- `interprocedural-dead-store` — removes writes to cells not read beyond procedure boundaries.
- `elideXParamReturnStateFields` — removes unused X return-state fields and reduces memory.
- `x-param-value-state-elision` — removes parser-created parameter state fields for matched X-param value functions when that parameter is not read outside the function body.
- `elide`-style elimination patterns — remove intermediate bookkeeping artifacts when no longer needed.
- `constant-synthesis` — synthesizes reusable constants in minimally short ways. Exact positive powers of ten can be built as `exponent; F 10^x` when that beats digit entry, both in main code and setup preloads.
- `preloaded-constant` — preloads constants when cheaper than recomputing each time.
- `const-inline` — expands program-level `const` names at use sites before register allocation; literals then follow the usual `preloaded-constant` / inline digit cost model.
- `auto-preload-initial-state` — moves required startup cells into setup so main code is shorter.
- `preloaded-indirect-flow` — enables indexed writes via preloaded selector.
- `preincrement-indexed-store` — uses preincrement semantics for indexed stores where profitable.
- `register-coalesce` — coalesces cells when lifetimes do not overlap.
- `copy-coalesce` — removes redundant copy writes between registers.
- `last-x-reuse` — avoids `P->X` when X already holds the needed value and the
  recall is not an X2-sync boundary for `.`/`ВП` before the next X2-affecting
  op, including direct `В/О` returns, and its stack lift cannot reach a
  downstream stack consumer through direct call returns. The sync guard is
  X2-register-aware: if dataflow proves X2 already contains the same register
  value and the removed recall is not the immediate previous-command context
  consumed by `.`/`ВП`, the recall can be removed as a redundant re-sync. The
  same removable-recall proof covers stable indirect `К П->X R7..Re` with a
  proved `indirect-memory-target`, but not mutating `R0..R6` selectors.
- `pre-shift-stack-lift` — removes `В↑` immediately before any proved
  stack-shifting producer (`П->X`, `К П->X`, `F pi`, or another `В↑`) when the
  following command already supplies the old X in Y and the shared
  stack-difference proof shows the extra Z/T difference cannot reach a later
  consumer.
- `known-zero-reuse` — reuses a known zero source instead of reloading.
- `inequality-zero-false-branch` — feeds `known-zero-reuse` after a false
  `!= 0` branch, avoiding a fresh zero literal or `Cx`.
- `zero-reuse` — similarly reuses zero in multiple places when liveness is confirmed.
- `stack-current-x-scheduling` — reorders current-X operations to avoid extra push/pop-like steps.
  Single-use stack temps are kept only when the value is not read before a later
  write on the remaining local path; visible state values (`show`, `halt`,
  `pause`, `return`) keep their store if they are read elsewhere. When a shared
  expression helper is already profitable, it wins over the local stack-temp
  rewrite so repeated tails remain shareable.
- `membership-collection-x2-restore` — for a packed membership test followed
  by setting the same assignable collection, including an indexed bank cell with
  a prepared indirect selector, uses X2 as the hidden temporary: the mask
  remains in Y, the collection recall synchronizes X2, `К∧; К{x}` performs the
  test, and `.` restores the collection on the jumped set branch before `К∨`.
  Deterministic known-fractional masks skip the redundant `К{x}` and get a
  preserving `К НОП` before `.` to keep the X2 restore gap safe.
- `x2-hidden-temp` — uses X2 as a temporary across X2-preserving logic so the
  active mask can stay in Y and the collection is restored on the target branch
  without adding a dedicated scratch register. Direct conditional opcodes expose
  a path-sensitive X2 profile to these proofs: fallthrough syncs X2, while the
  jump edge preserves the previous hidden value. When X itself is proved to
  equal a register on the sync edge, the X2 proof records the same register.
- `x2-hidden-temp-restore` — turns a direct scratch `П->X r`, or a
  stable-indirect proved scratch `К П->X R7..Re`, into `.` when X2 already
  contains `r`, a `.` restore-gap dataflow has seen two safe X2-preserving
  executable steps after the last X2 sync, and the normal stack-lift/context
  guards prove that the recall's stack shift and previous-command class are
  dead. This exposes the scratch register store to ordinary DSE instead of
  requiring a membership-specific lowering; repeated reads of loop/state
  registers are left unchanged.
- `x2-register-dataflow` — tracks definite states of the form “X2 currently
  equals register `r`” through X2-preserving code, stores, known indirect memory
  recalls, direct calls into the proved graph, and branch-specific direct
  conditional effects. Direct `В/О` continuations sync X2 from the returned X
  value when that value is proved; if returned X is unknown, the proof is
  cleared. Stops, opaque X2-affecting ops, and unknown indirect flow also clear
  the proof. Recall-removal passes use it to remove redundant `П->X r` re-syncs
  while still preserving immediate `ВП`/`.` context. When X and X2 are proved to
  share a register value, `X->П s` extends the proof with `s` as another alias
  for that hidden value; overwriting `s` from a value no longer equal to X2
  removes the alias.
- `stack-resident-temps` — keeps up to four consecutive single-use temps on the stack, using `В↑` lifts and restore sequences (`X↔Y` / `F reverse`) before direct stack-based consumers.
- `stack-resident-indexed-temp` — keeps a single-use temp in X across one indexed compound store `cells[i] op= temp` when the temp is consumed exactly once and selector/index setup is not temp-dependent.
- `stack-resident-control-flow` — marks stack-temp fusion that crosses stack-preserving `if` / `while` / `dispatch` regions; these regions cannot clobber live temps and the lowering rebuilds stack state if the region requires it.
- `dead-temp-store` — removes temporary stores after their last read when no longer needed.
- `store-recall-peephole` — collapses direct or stable-indirect proved
  same-cell `store` then immediate `recall` pairs unless the recall supplies
  the last X2 sync before `.`/`ВП` before the next X2-affecting op, including
  direct `В/О` returns, or lifts the stack for a downstream consumer through
  direct call returns. If X2-register dataflow proves that X2 already contains
  the same register and a preserving command remains before the restore, the
  recall is no longer considered the required sync. Mutating `R0..R6` indirect
  selectors are kept because their selector side effects are observable.
- `dead-store-elimination` — full pass removing pointless direct stores and
  stable-indirect `К X->П R7..Re` stores with proved memory targets, while
  keeping stores that are observable through number-entry, mutating `R0..R6`
  indirect selector side effects, or the `ВП`/X2 restore context.
- `repeated-assignment-value-reuse` — reuses the same computed value across multiple assignments, but yields to `initialized-counted-while-loop` when one of the repeated stores is the initializer for a following countdown loop. A one-cell literal reuse must not hide the much shorter `F Lx` loop shape.
- `repeated-assignment-counted-loop-reuse` — bridges that conflict: prefix
  assignments sharing the counted-loop initializer literal are stored from the
  same current X, and the loop still lowers through the normal `F Lx` counted
  tail.
- `int-frac-shared-tail` — a shared int/frac return tail reduces duplication.
- `subroutine-part-shared-tail` — computes one shared pure operand once and
  derives both `К [x]` and `К {x}` through one stack-tail, matching the same
  reduced-unary-return pattern used by `int-frac-shared-tail`.
- `z-stack-derived-tail` — shares a single operand once and uses one stack-tail (`X↔Y`, `X↔Z`, then restore) to derive adjacent `К [x]`/`К {x}`-style results, avoiding duplicated unary math work.
- `z-stack-derived-value-reuse` — lowers Z-stack pressure by moving values through warm locations.

## 13) IR pass pipeline (fixed-point)

The IR pipeline defined in `src/core/passes/index.ts` runs repeatedly:

1. `redundant-prologue-elimination` — removes duplicate `display+HALT` prologues immediately before a jump target when an identical prologue is already at that jump target.
2. `tail-call-lowering` — rewrites certain tail `call`s and trailing `return`s into direct `БП`/tail flow when the continuation is the same for all exits of that region.
3. `tail-branch-inversion` — flips `cjump` condition when the then-path is only a single tail jump and the target label is uniquely referenced.
4. `shared-call-tail` — groups repeated `call` + `jump` tails (three or more occurrences), emits one shared helper tail, and replaces duplicates with `БП` to that helper.
5. `return-suffix-gadget` — finds repeated return-ending blocks ending in `return`, extracts one shared suffix, and redirects additional copies to it.
6. `shared-terminal-tail` — finds repeated straight-line suffixes that already end in unconditional flow (`БП`, `К БП r`, or `В/О`) and replaces extra copies with a jump into the canonical suffix; it refuses programs with absolute numeric flow targets.
7. `return-zero-jump` — when no procedure calls are used, replaces a backward jump to `01` with `В/О` and tags it as an empty-stack optimization.
8. `store-recall-peephole` — removes `X->П r` immediately followed by `П->X r`, or stable-indirect proved same-cell `К X->П R7..Re` followed by `К П->X R7..Re`, only when the recall is not the last X2 sync before a context-sensitive `.`/`ВП` restoration before the next X2-affecting op, including direct `В/О` returns, and its stack lift cannot reach a downstream binary/stack-consuming op through direct call returns; mutating `R0..R6` indirect selectors are not folded.
9. `pre-shift-stack-lift` — removes `В↑` immediately before direct/indirect `П->X`, `F pi`, or another stack-shifting producer when that following command already supplies the old X in Y, unless the deeper stack difference can reach a later consumer.
10. `jump-to-next-threading` — removes unconditional jumps where target is the next label in sequence.
11. `jump-thread` — threads labels by replacing jumps to label chains with the final target label.
12. `flow-x-reuse` — runs forward CFG data-flow for values already held in X and removes a direct `П->X r` or stable-indirect `К П->X R7..Re` with a proved memory target when every predecessor reaches that point with the same value still in X; it refuses absolute numeric and indirect flow targets and keeps recalls that provide the last X2 sync before `.`/`ВП` before the next X2-affecting op, including direct `В/О` returns, or a stack lift that can reach a downstream consumer through direct call returns.
13. `branch-target-x-reuse` — removes the first direct or stable-indirect proved recall in a unique branch target when the source `cjump` tested the same recalled value and no fallthrough path can enter the target, unless the target recall is needed as a `.`/`ВП` X2-sync boundary before the next X2-affecting op, including direct `В/О` returns, or a stack lift that can reach a downstream consumer through direct call returns.
    These recall-removal guards read the shared `OpcodeInfo.stackEffect`
    profile, so stack-preserving, shifting, Y-consuming, exposing, and barrier
    opcodes are modeled consistently across passes.
14. `stable-indirect-flow` — rewrites direct `jump/call/cjump` to indirect forms (`К БП`, `К ПП`, `К <cond>`) when a stable selector is already live in a register.
15. `preloaded-indirect-flow` — preloads a selector value into a spare stable register and rewrites repeated backward-direct numeric jumps/calls through that preloaded value; after rescue starts, subsequent proved shrinking rewrites are still accepted below the official window.
16. `indirect-memory-table` — rewrites direct `store/recall` into `К X->П`/`К П->X` when a stable selector maps to the indexed target cell.
17. `x2-hidden-temp-restore` — replaces a direct or stable-indirect proved scratch recall with `.` when X2 already carries the same value and both the `.` restore gap and missing stack-lift observation are proven, allowing later DSE to remove now-unused scratch stores.
18. `dead-store-before-commutative` — removes temporary stores that are followed by immediate `recall` + commutative ALU (`+` or `*`) and never read again before the next write of that register.
19. `dead-store-elimination` — removes direct stores, plus stable-indirect stores with proved targets, whose target register is not live after the write and does not affect number-entry/input finalization or the previous-command context consumed by `ВП` while it restores X2; mutating indirect selectors are kept.
20. `last-x-reuse` — removes `П->X r` when `X` already contains `r` from the immediately preceding `X->П`, preserving recalls that serve as the last X2 sync before `.`/`ВП` before the next X2-affecting op, including direct `В/О` returns, or as a stack lift that can reach a downstream consumer through direct call returns.
21. `r0-fractional-sentinel` — drops redundant immediate `П->X 3`/`X->П 3`
    after fractional-R0 indirect access when `R0` liveness proves that the
    direct access only repeats the hardware-selected `R3`; it also removes
    later `X->П 0`/`П->X 0` repetitions when the same straight-line path has
    already left the hardware `-99999999` sentinel in `R0` and `X` is proved to
    hold the same value, and rewrites direct `БП 99` / `ПП 99` / `F x?0 99`
    flow to `К БП 0` / `К ПП 0` / `К x?0 0` when `R0` is already proved fractional and the
    resulting sentinel write is dead. A final post-layout verifier can perform
    the same rewrite for label targets only after replacing the two-cell branch
    proves that the label will land exactly at hardware address `99`.
22. `indirect-selector-integer-part` — tracks the proof marker from
    `fractional-indirect-addressing` and removes a redundant `К [x]` after the
    same stable selector register is recalled as an already-truncated integer.
23. `address-code-overlay` — a final post-layout verifier moves labels from a
    single-cell op immediately after `БП target` or a proved-terminal
    `ПП target` onto the branch address byte when removing that op proves the
    address byte will be the same opcode. The overlaid executable cell may be
    an ordinary op or an existing numeric/formal address byte; if the overlaid
    opcode itself takes an address, the following operand byte is kept as that
    command's operand. Fixed numeric/formal branch operands are rejected when
    shrinking would move their real target. The same verifier can move the
    branch target label onto the branch's own address byte, allowing that
    operand byte to be the first executed opcode.
24. `vp-splice` — deletes redundant exponent-entry chains (`ВП ВП`) and inert `КНОП ВП` forms, reporting `vp-exponent-splice` when one or more cells are removed.
25. `vp-exponent-splice` — optimization marker emitted to `report.optimizations` when at least one `ВП`/`КНОП` redundancy optimization pass removes cells.
26. `vp-x2-peephole` — removes redundant `К {x}` that immediately follows a proved `ВП`/X2 marker, display or ordinary, and reports `vp-fraction-restore` when one or more restores are removed. A marker is not required when CFG dataflow proves an ordinary X2 restoration boundary: an X2 sync, at least one X2-preserving executable command, then `ВП`; direct conditional jump/fallthrough edges use their path-sensitive X2 effects, and joins require every incoming path to carry the proof.
27. `constant-folding` — deletes identity arithmetic operations (`0+` and `1*`) when both operations are explicit user-facing constants.
28. `duplicate-failure-tail-merge` — removes duplicated failure tails by redirecting the first tail to the second; this covers both `(label -> 0 -> pause)` and `(label -> pause -> same terminal flow)` forms.
29. `cse-display-block` — detects identical `recall/plain/.../return(stop)` blocks and replaces duplicates with one canonical block plus jump.
30. `dead-code-after-halt` — removes unreachable IR ops by CFG reachability from entry.
31. `register-coalesce` — merges non-overlapping register live ranges and, when enabled, performs copy coalescing for safe `recall/store` aliases.
32. `arithmetic-if-pass` — merges two branch paths that lower to byte-identical pure linear blocks (same side effects and same single-pass behavior).

A fixed-point loop repeats while transformations continue, up to internal iteration limits.

## 14) Setup-program and preload strategy

Setup generation is separate from main program layout when needed:

- `generated-setup-program` indicates that a setup routine was emitted.
- `preloaded-constant` and `constant-synthesis` entries describe synthetic constants.
- `duplicate-preload-store-reuse` — setup preload planning computed one numeric literal once and emitted `X->П` into multiple registers when values were identical in the same preload action.
- `intent-state-lowering` — moves declared state initialization into generated setup by emitting setup `store` operations and records that state-related initialization was lowered out of the main path.
- `auto-preload-initial-state` and `intent-state-lowering` can push selected state to setup only.
- `raw-block-contract` — records and applies the input/output/clobber/preserve contract for raw `core` blocks in helper emission.
- `intent-read-lowering`, `show-read-*` may force setup when runtime behavior or literals require state initialization.
- Setup helpers are themselves subject to the same optimization pipeline (`setup-...` names appear as prefixed entries).
- `indexed-bank-loop` — initializes runs of consecutively allocated indexed bank fields with one compact setup loop when their initializers and register layout allow it.

## 15) Machine features this optimizer may activate in report

Feature flags are added only after successful candidate/optimization evidence:

- `return-empty-stack-jump` — added when `return-zero-jump` is used; means the compiler selected `В/О` as the one-cell `БП 01` shape.
- `branch-removal` — added when `branch-removal` optimization rewrites a branch to a branchless equivalent.
- `indirect-flow` — added when register-held or preloaded indirect flow rewrites (`stable-indirect-flow`, `preloaded-indirect-flow`, `preloaded-super-dark-flow`) are emitted.
- `indirect-memory` — added when indirect-memory selectors are used (`indirect-memory-table`, `indirect-memory-alias-selector`, `indexed-packed-row-table`).
- `dark-entries` — added from cyclic formal dark-entry selection and related layout features.
- `address-constants` — added when constants are reused as arithmetic/address-like data.
- `x2-register` — added when X2/Xп/дисплей-byte scheduling relies on X2 boundaries across display-byte or ordinary hidden-temp paths; opcode metadata follows the reference distinction between X2-preserving, X2-syncing/normalizing, and X2-restoring commands, plus branch-specific effects for direct conditionals.
- `fl-decrement-branch` — added when one-cell `F Lx` decrement/control forms are selected through optimizer-safe flow patterns (`fl-decrement-zero-branch`, `indirect-incdec-counter`, `r0-indirect-counter`).
- `stack-resident-temps` — added when any stack-temporary residency optimization is used (`stack-resident-temps`, `stack-resident-indexed-temp`, or `stack-resident-control-flow`); recall-removal proofs use the shared opcode stack-effect profile to avoid deleting `П->X` lifts that can still be observed downstream.
- `negative-zero-degree` — added when `negative-zero-threshold-selector` proof uses the `1|-00` preload trick.
- `x2-restore-boundaries` — added when `vp-fraction-restore` is active.
- `z-stack-register` — added when `z-stack-derived-value-reuse` uses deeper stack-derived storage.
- `display-bytes` — added when display-byte or packed hex-mantissa lowering is active.
- `raw-display-5f` — added when the optimizer emits raw-display opcode `5F` as a display-state mutation.
- `r0-fractional-sentinel` — added when fractional indirect addressing or R0 fractional sentinel flow/path is active.
- `r0-t-alias` — added when `r0-indirect-counter` path is selected and R0-transforming aliases are proven safe.
- `error-stops` — added for domain-error stop/trap lowering (`error-stop`, `screen-error-literal-lowering`, `domain-error-guard`).
- `code-data-overlay` — added when layout marks address cells as overlayable with code/data reuse.
- `super-dark-dispatch` — added when `super-dark-dispatch` or `preloaded-super-dark-flow` candidate is selected and FA..FF routing is proven.

These are not independent optimizations; they gate whether the lowering strategy can legally use the corresponding opcode/behavior.

## 15a) Exact-machine profile and emulator facts

- `report.machine` — fixed to `mk61` for this toolchain.
- `report.machineFeaturesUsed` — feature names set from successful candidate/evidence, as listed above.
- `report.emulatorFacts` — probe-backed machine truths used by lowering and verified rewrites.

- `undocumented-opcodes` (feature precondition) — source-level pass uses `F0..FF` and undocumented aliases only where exact behavior is proved safe.
- `extra-cells` (feature precondition) — non-official/extra physical cells are tracked via
  `report.budgetReport.extraCells` and included in `report.budgetReport.totalPhysicalCells`.

Profile facts in `report.emulatorFacts`:
- `return-empty-stack-jumps-to-01` (`status: proved`) — `В/О` with an empty return stack behaves as one-cell `БП 01` during continuous execution.
- `r0-star-f-aliases` (`status: proved`) — `*F` aliases track as matching `*0` entries with explicit `R0` transformation; neither form preserves `R0`.
- `super-dark-fa-ff-indirect` (`status: proved`) — `К БП R` with `R = FA..FF` executes one command at `48..53`, then continues at `01..06`.
- `fa-direct-vs-indirect` (`status: proved`) — direct `БП FA` consumes/overwrites the next operand byte, while indirect `К БП R` leaves `01..06` bytes available as continuation space.
- `r0-fractional-jump-99` (`status: proved`) — `К БП 0` with `0 < R0 < 1` jumps to `99` and leaves `R0 = -99999999`.
- `r0-fractional-selects-r3` (`status: proved`) — `К П→X 0` and `К X→П 0` with `0 < R0 < 1` can select `R3` and leave `R0 = -99999999`.
- `negative-zero-degree-threshold` (`status: proved`) — with `1|-00` in `Y`, multiplying by `X` then normalizing through `В↑` produces a binary threshold at `|X| = 1`.
- `step-vs-run-delta` (`status: proved`) — continuous-run behavior is the default and step-only deviations are explicitly modeled through `mk61` facts and verification.

## 16) Proof-guided safety model (important)

The optimizer does not blindly apply undocumented behavior. Several proofs are explicitly logged and checked:

- `value-ranges`, `observability`, and `formal-address-operands` when source bounds are known.
- `branch-equivalence` — records that conditional rewriting (`branch-removal` and arithmetic-if-family rewrites) was proven equivalent for the rewritten branch shapes.
- `negative-zero-threshold-selector` proof for threshold selectors.
- `indirect-addressing-ranges` proof when selector stability is required.
- `indirect-memory-alias-selector` proof when indexed-bank lowering depends on non-linear two-digit memory aliases, including negative selector values.
- `display-byte-observable-boundary` proof for display-byte candidates when only display-observable boundaries allow the optimization.
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
