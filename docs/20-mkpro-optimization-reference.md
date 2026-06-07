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
  while respecting real label entry points, `.`/`/-/`/`ВП` X2-sync boundaries, and
  downstream stack consumers.
- `flow-x-reuse` — avoids recalls when all CFG predecessors already carry the same register value in X, or the same concrete decimal value proved through X2 register-memory/preload metadata, with the same X2-sync and downstream stack-lift guards.
- `branch-target-x-reuse` — avoids the first recall in a branch target when the source branch still preserves that value in X, or the target-entry value proof shows the same decimal/structural shape is already visible, and the target has no other entry, including counted-loop targets for non-counter registers and labels separated by `С/П`. X2-sensitive target checks use the source branch's projected jump-edge value state, so the pass does not lose a unique-entry proof to unrelated global target joins, unless that recall supplies the target-side X2 sync or a stack lift that reaches a downstream consumer through direct call returns.
- `constant-folding` — precomputes constant parts before code generation.
- `cse-display-block` — merges identical display logic blocks.
- `jump-thread` — rewires jump chains into one direct jump path.
- `jump-to-next-threading` — removes intermediate jumps to the next label.
- `dead-code-after-halt` — removes code unreachable after `HALT`.
- `register-coalesce` — merges separate temporary cells when lifetime ranges do not overlap.
- `duplicate-failure-tail-merge` — merges identical error/failure tail sequences, including pause-only tails that display the incoming X value.
- `shared-terminal-tail` — jumps into an existing identical straight-line suffix that already ends in unconditional terminal flow.
- `shared-straight-line-helper` — extracts repeated non-terminal straight-line opcode bodies into one helper subroutine when the `ПП`/`В/О` cost is lower than duplicated inline code; a size-gated candidate extends this to bodies with direct `ПП` calls, and `multi-entry-straight-line-helper` can add internal entry labels for repeated suffixes of the same helper body. X2-restoring numeric-entry commands may be shared only when their restore context is wholly inside the helper body; helper calls/returns are not allowed to become the previous command for an adjacent digit/`.`/`/-/`/`ВП`.
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
- `current-x-unary-derivation` — when a single-argument X-transform intrinsic (`abs`, `sign`, `frac`, `sqr`, etc.) consumes a value already proved live in `X`, emits only the transform opcode instead of recalling the argument first.
- `show-read-fusion` — merges `show(...)` with a following `read`-based assignment/input path into one calculator `С/П`: `show(...); x = read()` or `show(...); x = int(read())` / `frac(int(read()))` forms share the same input stop and avoid emitting a second `С/П`.
- `running-display-preview` — lowers `preview(expr)` as expression preparation only, leaving the value visible without inserting a calculator `С/П`.
- `show-read-decrement-underflow-fusion` — merges `show -> input -> decrement -> if (counter < 0) ...` into one compact sequence, keeping input in `Y` across the decrement-underflow check.
- `show-read-stored-decrement-underflow-fusion` — the stored-input variant for nested consumers: it keeps the input's ordinary register store, but still restores the same input from `Y` after the decrement-underflow check so the first following expression can use current-`X` scheduling.
- `show-read-stack-stop-risk-lowering` — a generalized "stack-stop fusion": when a single plain `show` source value (`stake`) is combined with a freshly read input across the stop, it keeps `stake` in `Y`, transforms the input in `X`, and computes the result directly on the stack with no input register. It recognizes any pure shape `wrap*( stake (op) g(input) )` where `op` is `+`/`-`/`*`/`/` (non-commutative ops keep `stake` on the left so they map to the machine's `Y op X` order), `g(input)` is a chain of single-argument X-transform intrinsics over the input (e.g. `sin`, `cos`, `tg`, `sqrt`) optionally fused with one single-digit additive/scaling constant, and `wrap*` is an outer chain of X-transform intrinsics (e.g. `int`, `frac`). The input leaf may be a direct `sin(read())` or a stored input field, avoiding a source-visible throwaway field. The classic `int(stake * (1 + sin(read())))` robber-fight idiom is the canonical case and lowers byte-for-byte identically; the generalization never grows a program because every accepted form reuses the same kept-in-`Y` stack sequence.
- `loop-carried-prompt-x` — for loops shaped as `show(screen); key = read()` where every non-terminal branch assigns the next `screen`, removes the register-backed prompt state and leaves the next visible value in `X` for the loop-back stop. If the prompt starts from `stack.X` / `stack.Y`, an allocated sibling field initialized from the same stack slot can seed the first prompt.
- `loop-carried-prompt-input-branch` — after a loop-carried prompt stop, branches directly on the read key with no extra store when the branch condition consumes only that input.
- `loop-carried-prompt-input-dispatch` — after a loop-carried prompt stop, dispatches directly on the read key with no intermediate variable, while preserving the prompt flow across loop back-edge.
- `loop-carried-prompt-decrement-underflow` — after a loop-carried prompt stop, handles `resource--; if resource < 0 ...` patterns by checking underflow in-line. It keeps the input value in `Y`, emits `F x<0` branch flow, and restores `X/Y` state only where required for the next prompt consumer.
- `show-read-guarded-transfer` — rewrites `show; x=input; decrementTarget -= x; if decrementTarget < 0 { halt } ; incrementTarget += x; if incrementTarget < 0 { halt }` into one stack-based sequence that keeps the read value on the stack across both guarded updates.
- `comparison-guarded-update-selector` — speculative whole-program candidate
  that forces `abs`/`sign` comparison masks for guarded `+=`/`-=` updates, then
  keeps the candidate only if the final layout is smaller.
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
- `arithmetic-if-update` — performs conditional assignment in one path instead of multiple instructions; the speculative comparison-mask variant can try `if x == c { y += d }` as `y += d * (1 - sign(abs(x - c)))` and keep it only when the whole layout shrinks.
- `arithmetic-if-extrema` — computes `max/min` using shorter conditional forms.
- `zero-condition-test` — emits zero tests in the shortest variant instead of expanded `if`.
- `dispatch-lowering` — converts dispatch nodes to short jump sequences.
- `dispatch-default-merge` — shares one tail when different `default` branches are identical.
- `dispatch-case-ordering` — reorders cases so fast paths are checked earlier.
- `dispatch-source-register` — keeps selected source in a dedicated register in advance.
- `numeric-dispatch-residual-chain` — packs numeric check chains in tail lowering form.
- `dispatch-default-residual-sign` — derives `sign(selector - bound)` /
  `sign(bound - selector)` from the residual `selector - lastCase` already left
  in X at a numeric dispatch `default`, optionally multiplying by a numeric
  scale, so the selector does not have to be recalled just to compute the sign.
  If the selector is a declared integer `counter` and an enclosing branch has
  proven the adjacent boundary value impossible (for example the false branch of
  `abs(key) == 5`), `dispatch-default-residual-sign-domain` can also skip the
  one-cell residual shift between `bound` and `lastCase`.
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
  Candidate boundaries are X2-context aware: a body may contain digit-entry,
  `.`, `/-/`, or `ВП` only when each restore is preceded by its required
  context inside the shared body, and extraction refuses starts/ends that would
  insert a helper call or `В/О` return next to an X2 restore.
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
- `comparison-guarded-update-selector` — candidate that tries abs/sign comparison masks for guarded arithmetic updates after full layout, so locally longer branchless forms are adopted only when they pay back globally.
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
- `bit-mask-helper` — emits shared helper routines that build bit masks from indices once per scratch register. The helper treats the packed-mask literal steps as X2-sensitive stack literals: helper-only preloaded constants can replace selected `В↑; digit` pairs only where emulator tests prove the stack effect is equivalent, while ordinary one-digit expression literals ignore those preloads.
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
- `arithmetic-if-update` — turns conditional updates into arithmetic form instead of branching, including comparison-mask trial forms such as `1 - sign(abs(x - c))` in the speculative whole-program variant.
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
- `arithmetic-if-comparison-update` — speculative companion for guarded
  `+=`/`-=` updates: a comparison is converted to an arithmetic mask even when
  the local estimate prefers a branch, but the candidate is adopted only after
  whole-layout selection proves a shrink.
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
  recall is not an X2-sync boundary for `.`/`/-/`/`ВП` before the next X2-affecting
  op, including direct `В/О` returns, and its stack lift cannot reach a
  downstream stack consumer through direct or proved-indirect flow. The X-held-value proof
  is seeded by direct stores and by proved indirect stores, including mutating
  `R0..R6` indirect stores because the store and its selector side effect are
  kept, by kept direct/stable recalls, and is preserved by documented empty
  operators `К НОП`/`К 1`/`К 2` plus unreferenced compiler marker labels. X2
  decimal register-memory, decimal `preload const` metadata, and structural
  hex/super shape-memory can also seed the proof when the current X was rebuilt
  as the same concrete decimal value or display shape stored in the recalled
  register. Structural shapes are used only as visible-X equality evidence; they
  do not make `.`/`/-/` restores dot-safe.
  Labels targeted by string flow, numeric-address flow, proved indirect flow,
  procedure starts, or any unknown indirect flow are treated as entry barriers.
  The sync guard is X2-register-aware: if dataflow proves X2 already
  contains the same register value, or the same concrete decimal value from
  register-memory, and the removed recall is not the immediate
  previous-command context consumed by `.`/`/-/`/`ВП`, the recall can be removed as a
  redundant re-sync. The same removable-recall proof covers stable indirect
  `К П->X R7..Re` with a proved `indirect-memory-target`, but not mutating
  `R0..R6` selectors.
- `pre-shift-stack-lift` — removes `В↑` before any proved stack-shifting
  producer (`П->X`, `К П->X`, `F pi`, or another `В↑`), even through
  stack-preserving labels, stores, address bytes, and plain stack-neutral
  commands, when the producer already supplies the current X in Y and the
  shared stack-difference proof shows the extra Z/T difference cannot reach a
  later consumer. The gap may include a direct conditional, counted-loop,
  proved-indirect conditional fallthrough, a linear `В/О` return, or a direct/proved-indirect
  `ПП` helper chain whose `В/О` return itself syncs the same X
  into X2, when both the call-return-aware CFG stack proof and the X2-restore
  exposure proof show that skipped or downstream edges cannot observe the
  removed sync/lift. Such `ПП` helpers must
  reach `В/О` through nested return-helper calls and only stack-preserving
  commands; stack consumers, X2 restores, recursive helper cycles, and other
  entry labels keep the call as a barrier.
  The same
  proof also removes `В↑` before a hard X/X2 overwrite such as `Cx` when the
  lift's Y value cannot reach any later stack consumer, or before a plain
  X-preserving X2 sync such as `F0`..`FF` when that sync replaces the removed
  lift's X2 update and the stack lift is dead. A `В↑` after any proved
  stack-lift + X2-sync producer (`П->X`, proved stable `К П->X`, or another
  `В↑`), after a plain X-preserving X2 sync (`F0`..`FF`), after a hard
  X/X2 overwrite such as `Cx` whose stack lift is dead, after a
  path-safe direct conditional/counting-loop fallthrough X2 sync, after a
  stack-preserving direct/proved-indirect return helper whose `В/О` syncs the
  helper's returned X even when the helper computed a new X, or after a
  transparent direct/proved-indirect return-helper call is also removed when
  only stack/X2-preserving local gap cells stand between them and the lift's
  deeper stack cell is dead: the earlier operation has already supplied the
  hidden X2 sync, and stack-lift producers also supplied the visible X in Y, so
  the extra lift is only a redundant scheduler cell. Jump edges that can enter
  the scanned range without the fallthrough sync keep the lift. The post-producer scan can
  cross a path-safe direct conditional, counted-loop, or proved-indirect
  conditional fallthrough whose opcode has a known fallthrough X2 effect and preserves the
  stack; unknown indirect conditionals remain barriers. It can also cross a
  direct or proved-indirect `ПП` helper chain that reaches `В/О`
  through commands preserving X, stack, and X2; helpers that compute a new X,
  consume stack, restore/overwrite X2, branch, recurse, or expose another entry remain
  barriers. Transparent-helper proofs are memoized per IR body so the forward
  and backward scheduler scans do not re-walk the same helper tree. A separate
  return-sync scan can use stack-preserving helpers that change X, but it still
  refuses helper bodies with stack mutation or context-sensitive X2 restores
  before `В/О`. Targeted entry labels, display/X2 restore context, stack consumers,
  and other flow commands keep the post-producer scan conservative.
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
- `bit-mask-decade-scale` / `bit-mask-decade-index-preload` — chooses the
  cheaper fractional-nibble bit-mask placement form. `10^int(q) * 10` wins when
  `10` is already preloaded by spatial coordinate code; `10^(int(q) + 1)` wins
  when a stack-preloaded `1` is available; with direct digit entry the forms are
  the same length.
- `spatial-sum-hit-stack-restore` — in shared `line_count`/`neighbor_count`
  sum loops, calls the bit-mask hit test while the computed index is still in
  X, advances the offset afterward, and uses `X↔Y` to recover the 0/1 hit
  count from the stack before accumulating. This removes the scratch
  store/recall pair formerly needed to survive offset advancement.
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
  Register and value/shape edge projections now share helper semantics, so
  local passes such as branch-target rewrites consume the same direct
  conditional, counted-loop, and proved-indirect selector rules as global X2
  dataflow.
  Indirect conditionals are also path-sensitive for control flow, but both
  edges preserve the previous X2 value; they do not create an X-to-X2 sync. The
  stricter value proof also remembers concrete decimal facts stored by direct
  `X->П r`, reads decimal preload facts from `П->X r` metadata, and rehydrates
  remembered facts on a later direct or proved indirect `П->X r`, while unknown
  indirect stores clear that register value-memory. Hex-like and super preload
  facts are tracked only as structural shapes until a separate proof makes them
  safe to restore. Those structural facts can still feed `ВП`-entry splice
  proofs after direct/proved recalls or the fallthrough side of a direct
  conditional X2 sync. The same structural shape equality can prove that a
  redundant recall would not change hidden X2 before a later
  context-sensitive restore after an X2-preserving gap; it still does not make
  hex/super shapes dot-safe. The jump edge remains conservative and preserves
  the old hidden state instead of creating a new shape source.
- `x2-hidden-temp-restore` — turns a direct scratch `П->X r`, or a
  stable-indirect proved scratch `К П->X R7..Re`, into `.` when X2 already
  contains `r`, and either a `.` restore-gap dataflow has seen two safe
  X2-preserving executable steps after the last X2 sync or CFG proves the
  recall starts immediately after an X2 sync, or the shared X2 shape model
  proves a closed-context `/-/` dot source through only free-standing
  `КНОП`/`К1`/`К2` empty ops. The normal stack-lift/context
  guards still prove that the recall's stack shift and previous-command class
  are dead. The proof can come from either X2-register dataflow or the stricter
  X2 value dataflow, so a prior closed-context `.` restore keeps the hidden
  `reg:r` fact available for later scratch aliases. It may also come from
  register value-memory when the scratch register and hidden X2 share the same
  proved decimal fact, even without a live `reg:r` alias. Structural hex/super
  shape-memory remains available to recall-elimination proofs, including
  closed `hex-exponent:*:*` / `super-exponent:*:*` restore-shape facts, but is
  not used as a hidden-temp `.` restore source until a separate dot-safety
  proof exists, except for the VP-only escape where the inserted `.` is the
  first restore feeding an immediately proved same-source `ВП`. Closed
  structural exponent facts prove recall-visible equality; structural mantissa
  facts, including restored exponent shapes, can seed that `ВП` source. If register-memory has
  become too conservative at a join, the pass can still use the dead scratch
  store's own stable source fact (`decimal:*:normalized` or `expr:*`) as the
  hidden-temp proof.
  The scratch-store search may cross a direct conditional or counted-loop
  fallthrough when the path-sensitive X2 dataflow proves that fallthrough edge
  already synchronized the same value into X2; the non-fallthrough edge remains
  governed by ordinary liveness/DSE and does not create an X2 sync proof.
  Counted-loop crossings are refused when the scratch register is the loop
  counter being decremented. It may also cross direct/proved-indirect `ПП`
  helper chains when every nested callee reaches `В/О` without touching the
  scratch register; unknown indirect memory access, recursive helper cycles, or
  another entry label keeps the call as a barrier.
  This exposes the scratch
  register store to ordinary DSE instead of
  requiring a membership-specific lowering; repeated reads of loop/state
  registers are left unchanged because `.` and `П->X r` are both one cell and a
  no-net rewrite can perturb layout.
- `x2-dead-restore-before-overwrite` — removes a proved-safe X2 restore whose
  visible `X` result is immediately overwritten by a hard X/X2 replacement
  command. Consecutive same-segment restores and free-standing separators are
  removed as one run, while labels split the removable run. The overwrite
  search may cross direct/proved-indirect `ПП` helper chains that reach `В/О`
  through only nested transparent helper calls and restore-transparent
  empty/address cells; helpers that restore X2, store X, consume stack, branch,
  recurse, or expose another entry remain barriers.
  The proof uses
  decimal X2 value facts for `.`/`/-/` and mantissa/exponent-entry state for
  `ВП`; structural hex/super `ВП` sources are accepted only as shape facts,
  while unknown register-valued X2 facts are kept because hex/non-normal
  preloads can make the restore itself observable.
- `x2-register-dataflow` — tracks definite states of the form “X2 currently
  equals register `r`” through X2-preserving code, stores, known indirect memory
  recalls, direct or proved-indirect calls into the graph, proved indirect flow
  targets (`indirect-target=NN`), and branch-specific direct conditional
  effects. Stable indirect-flow selectors preserve register-valued X/X2 facts;
  mutating selectors drop only facts about the selector register that the
  hardware address computation changes. The same proof now tracks a narrow
  register-valued `Y` fact through stack shifts, `X↔Y`, `Y->X`, Y-keeping
  operations, and non-staling register writes, so `Y->X` followed by an X2 sync
  can recover the register alias without falling back to the heavier value
  lattice. Direct `В/О` continuations sync X2 from
  the returned X value when that value is proved; if returned X is unknown, the
  proof is cleared. Stops, opaque X2-affecting ops, and unknown indirect flow
  also clear the proof. Recall-removal passes use it to remove redundant
  `П->X r` re-syncs
  while still preserving immediate `.`/`/-/`/`ВП` context. When X and X2 are proved to
  share a register value, `X->П s` extends the proof with `s` as another alias
  for that hidden value; overwriting `s` from a value no longer equal to X2
  removes the alias. A separate CFG fact tracks points reached immediately
  after an X2 sync on every incoming path, including direct `В/О` call
  continuations, so restore passes do not have to rely on a purely linear
  previous-op scan. Opaque X/X2 equality produced by a known instruction is
  carried as an `expr:<step>` token; closed-context `/-/` creates a fresh
  expression token rather than collapsing the value to undifferentiated
  `same:unknown`, and also creates a stable `expr-key:0B(<source>)` when the
  closed-context source is already a proved register, normalized decimal, or
  stable expression key. For shared structural exponent sources, the sign
  source key is canonicalized through the proved restored mantissa when that
  closed form is also shared, so `hex-exponent:FACE:3` records
  `shape:hex:FACE000:mantissa` rather than a duplicate raw-exponent key.
  Whitelisted documented pure computations such as
  `F sqrt`, `F x^2`, `К [x]`, `К {x}`, arithmetic `+`/`-`/`*`/`/`, `F x^y`,
  `К max`, and `К ∧`/`К ∨`/`К ⊕` also seed an opaque `expr:<step>` value in
  visible X while leaving X2 untouched; a later explicit X2 sync (`В↑`, F*
  empty, direct conditional fallthrough, etc.) can then carry that exact
  computed value as a hidden temporary. Producer-local `expr:<step>` facts do
  not survive CFG backedges, context-mixed `ПП`/`В/О` call-return edges, or
  register-memory carried across them; only operand-shaped `expr-key:*` facts
  are allowed to prove equality through a repeated entry. Documented unary
  X-only computations
  additionally seed a stable `expr-key:<opcode>(<source>)` fact when the source
  is a proved register, normalized decimal, raw decimal value fact normalized by
  restored-visible value, or earlier stable expression key. The same
  restored-visible normalization feeds concrete pure unary/binary decimal
  results, and value+shape restored-visible decimal proofs use one combined set
  helper so raw decimal facts and exact decimal display-shapes are compared by
  the same normalized visible value. The stable-key concrete gate delegates to
  that shared evaluator, so
  decimal, exact display-shape, and structural-unary concrete proofs cannot
  drift into separate opcode-specific suppression rules; it still does not make
  the raw mantissa shape dot-safe and does not promote it to a `ВП`
  display-shape proof.
  Closed-context `/-/` states that produce a decimal mantissa also record that
  mantissa as an explicit `ВП` sign source. Exact decimal display-shapes and
  structural hex/super sources have a separate explicit sign-shape channel,
  distinct from ordinary `vpEntryShape`; path-sensitive preserving branch edges
  can carry these sign sources onward for a later `/-/ ВП` without promoting the
  same edge into an ordinary `ВП` entry source.
  The documented `F pi` stack producer seeds the stable `expr-key:20()`,
  the hardware decimal constant `3.1415926`, and its display shape
  `mantissa:3.1415926:decimal`. Emulator-verified exact
  special values for documented functions are also modeled: `F e^x` on `0`,
  `F lg`/`F ln` on `1`, inverse/direct sine and tangent on `0`, inverse cosine
  on `1`, direct cosine on `0`, and `F x^y` exact cases (`0^y`, `1^y`, `x^1`,
  and positive `x^0`). For concrete normalized decimal `X`, `F x^2`, finite `F 1/x`,
  perfect-square `F sqrt`, integer-exponent `F 10^x`, `К |x|`, `К ЗН`, and
  `К [x]` seed exact decimal results in visible X while preserving the old X2
  fact. If that exact result has a provable display form within the mantissa
  width, the shape layer records it as either dot-safe `mantissa:*:decimal`
  or structural scientific `exponent:*:*:decimal`; fractional and wide/small
  exact decimals therefore keep their calculator display shape without being
  flattened into ordinary mantissas. Exact decimal display-shape equality is a
  visible-`X` proof only: closed forms such as `exponent:1:8:decimal` can match
  the same stored display shape, but they are not made dot-safe for `.` restore
  rewrites. These canonical decimal display shapes can also seed opaque
  stable-expression source keys; shape-only decimal display shapes, including
  exact exponent display forms, do this only when an equivalent
  `decimal:*:normalized` value fact is not already present or when the
  operation cannot prove an exact decimal result. When a
  unary operation does prove an exact result from the restored-visible decimal
  display, the result is recorded as a normal decimal value while the original
  display-shape source still is not treated as a dot-safe X2 alias. Stable
  expression keys for structural exponent-entry sources prefer the proved
  restored mantissa when closure is available (`hex:Г; ВП 2` keys as
  `shape:hex:Г00:mantissa`); the raw `hex-exponent:*:*`/`super-exponent:*:*`
  source key remains only as a fallback when no closed structural mantissa can
  be proved. The same canonical structural source-key set is used by
  `ВП`/source equality and closed sign-source proofs, so the comparison does
  not carry both the raw exponent-entry spelling and its restored mantissa.
  Stable `expr-key:*` facts canonicalize structural `shape:*` operands
  recursively as well, so a hidden-temp proof that already contains
  `shape:hex-exponent:Г:2` is reused as `shape:hex:Г00:mantissa` instead of
  becoming a separate computed value. If an `expr-key:*` contains a `shape:*`
  operand that the shape algebra cannot prove, the key is discarded instead of
  being kept as an opaque stable proof. The reverse `shape:*` source-key decode
  uses the same restored-display algebra, so exact decimal exponent displays
  and closed hex/super exponent entries feed concrete-result checks through the
  same canonical shape facts that originally created the key. X2 value-set
  operations, register-memory storage/recall, and CFG joins keep the same canonical form, so equivalent
  stable-expression facts do not split after path-sensitive propagation. VP
  source joins also use source keys: a raw decimal mantissa path and an exact
  decimal display-shape path can keep their shared mantissa/sign source without
  normalizing leading-zero text, while structural exponent/mantissa spellings
  meet at the restored mantissa source. These joins accumulate every proved
  common source class instead of stopping at a direct raw/shape intersection, so
  a CFG merge can preserve both a shared ordinary mantissa and a separate shared
  exact display/structural source.
  Closed `/-/` shared-source proofs use the same canonical value sets before
  comparing visible `X` and hidden X2, so sign-change keys remain reusable
  after mixed raw/canonical paths meet.
  Hex and super displays are not promoted to decimal shapes without
  a separate display proof. For concrete decimal `X`, `К {x}` also
  seeds the exact normalized fractional decimal value in visible X while
  preserving the old X2 fact; its display shape is kept as a separate
  exponent-entry fact for non-zero fractions (`0.2` as
  `exponent:2:-1:decimal`, `0.0012` as `exponent:1.2:-3:decimal`) so later
  `ВП`/`.` context sees the same scientific display shape as the calculator;
  negative integers produce visible decimal zero and still seed an `errorProne`
  `mantissa:-0:decimal` shape so later X2 analysis can remember the signed-zero
  display context without treating hidden X2 as ordinary zero. Once a
  later X2 sync proves that same signed-zero shape in both visible `X` and
  hidden X2, it can seed a `-0` `ВП` mantissa source while still remaining
  error-prone for dot-safety.
  Concrete normalized decimal `Y,X` operands for `+`, `-`, and `*` also seed
  exact decimal results when the normalized result stays within the dataflow's
  eight-significant-digit bound; `/` does the same only when the reduced
  quotient has a finite decimal expansion. Short ordinary integer results from
  exact decimal binary operations also seed the same display-shape proof:
  ordinary results use dot-safe `mantissa:*:decimal`, while fractional and
  wide/small scientific results use `exponent:*:*:decimal`. `К max`
  is modeled for concrete normalized decimal operands with the MK-61 zero quirk
  preserved: if either operand is exactly zero, the proved result is zero.
  `К ∧`, `К ∨`, `К ⊕`, and `К ИНВ` share the MK-61 mantissa-nibble model used
  by constant folding and seed decimal facts only when every resulting nibble is
  still decimal, including results computed from structural hex/super display
  operands. The raw result display remains tracked as shape-only metadata, so
  non-normal hex-shaped displays are not treated as freshly entered decimal
  mantissas; when the structural bitwise result is decimal-only, the proof also
  records the exact decimal display shape alongside the structural spelling.
  Stable expression keys use the same structural-unary concrete proof for
  `x^2`, `К ЗН`, and `К ИНВ`, so a decimal-only structural result is
  represented by the concrete decimal fact instead of also keeping a redundant
  opaque `expr-key:...(shape:...)`.
  A-F/hex-cell results seed shape-only structural `hex:*` mantissas
  when both operands can be parsed as Latin hex nibbles or known
  MK-61 display glyphs `С`/`Г`/`Е`, while unknown glyph cells remain
  conservative. The shape parser and preload recognizer use that same closed
  digit set, so arbitrary Cyrillic display letters such as `Ж` no longer enter
  the structural X2 lattice as if they were proved hex digits. They also reject
  malformed mantissa spellings with multiple decimal points, while preserving
  MK-61 display separator cells such as the embedded `-` in `8.70Е2-6С`.
  `К °->′`,
  `К °->′"`, `К °<-′"`, and `К °<-′` seed exact decimal facts only when the
  domain is safe, the rational sexagesimal conversion has a finite decimal
  expansion, and the normalized result fits the eight-significant-digit
  machine-mantissa proof bound. Integer trailing zeroes may be carried as an
  exact scientific exponent shift without creating an ordinary mantissa display
  shape. The structural hex exponent arithmetic also recognizes the strict
  closed single-nibble forms produced by the same shape algebra (`Г`, `Г00`,
  `0.Г`, `0.0Г`) as exponent operands, but only the already pinned table cases
  are folded. Structural hex `+`, `-`, `*`, and `/` now enumerate one
  product fact for each proved operand pair and derive both the normalized
  decimal value and display-shape facts from that same product, so value and
  shape proofs cannot drift between opcode-specific local tables. Bitwise
  operators remain on the separate mantissa-nibble model because their raw
  hex/super display result is not necessarily decimal. Decimal-only structural
  mantissa shapes also carry a normalized decimal in the shape model, but they
  become visible value facts only when the structural spelling is already the
  exact decimal display (`hex:123:mantissa`, or a closed structural exponent
  such as `hex-exponent:123:1` -> `hex:1230:mantissa`). Leading-zero,
  fractional-scientific, and other raw structural spellings stay shape-only and
  still are not dot-safe X2 restore aliases. Stable expression keys canonicalize
  only those exact structural decimal display sources to
  `decimal:*:normalized`; ordinary decimal display shapes, especially exact
  exponent displays, stay shape-keyed so VP/sign-source context is not
  collapsed into plain value equality. Reverse decimal/hex division also uses
  its own emulator-pinned table for decimal `0..9`/`18` in `Y` and structural
  `A`..`E` in `X`, preserving non-normal display shapes such as
  `exponent:0.4444443:-1:decimal` separately from normalized decimal value
  facts. Signed, multi-nibble, trailing-tail, and
  unsupported-exponent forms remain structural. Wider products/quotients, division by zero,
  non-terminating division, irrational square roots, fractional powers of ten,
  remaining non-zero powers, hardware-rounded sexagesimal conversions, and
  non-decimal cases remain structural/opaque rather than pretending to be an
  ordinary decimal value.
  Later passes can therefore distinguish visible no-op
  integer/fraction/sign transforms and exact arithmetic transforms from
  observable hidden-X2 restores.
  Register-sourced keys are version-sensitive: a later direct/known-indirect
  store to that register, counted-loop mutation, or mutating indirect-flow
  selector edge drops `expr-key:*reg:r*` facts from the value lattice and
  register-memory, unless the store is proved to write the same `reg:r` value
  back.
  That lets repeated `F sqrt(2)`-style computations and their closed sign
  changes meet in X2 dataflow without relying on the producer address. The
  value dataflow also tracks a value-only `Y` fact through proved stack shifts,
  through `X↔Y` swaps, through undocumented `Y->X` copies that leave hidden X2
  untouched, and through documented operations whose stack profile keeps `Y`, so
  stack-consuming documented pure computations seed
  `expr-key:<opcode>(<Y>,<X>)` when both operands are stable sources and no
  concrete decimal result has already been proved; otherwise they remain
  producer-local. Stable keys for commutative binary opcodes (`+`, `*`, `К max`,
  `К∧`, `К∨`, `К⊕`) canonicalize operand order, while
  non-commutative opcodes keep the hardware `Y,X` order. Display-role, barrier,
  exposing, undocumented, dangerous, and random-like commands do not seed such
  facts. The same stack transfer model carries structural `Y` shape facts for
  hex/super values through `В↑`, `X↔Y`, and documented Y-keeping operations, so
  a later store/recall can still use shape-memory proofs without decimalizing
  those values.
- `stack-resident-temps` — keeps up to four consecutive single-use temps on the stack, using `В↑` lifts and restore sequences (`X↔Y` / `F reverse`) before direct stack-based consumers.
- `stack-resident-indexed-temp` — keeps a single-use temp in X across one indexed compound store `cells[i] op= temp` when the temp is consumed exactly once and selector/index setup is not temp-dependent.
- `stack-resident-control-flow` — marks stack-temp fusion that crosses stack-preserving `if` / `while` / `dispatch` regions; these regions cannot clobber live temps and the lowering rebuilds stack state if the region requires it.
- `dead-temp-store` — removes temporary stores after their last read when no longer needed.
- `store-recall-peephole` — collapses direct or stable-indirect proved
  same-cell `store` then immediate `recall` pairs, and adjacent recalls to
  another cell when value/shape dataflow proves the recalled decimal value or
  structural display shape is already in X. Structural display equality uses
  the shared exponent-shift shape algebra, so a recalled `hex:Г00` can match
  an X value built as `hex:Г; ВП 2` without making either side decimal. The
  rewrite is refused when the
  recall supplies the last X2 sync before `.`/`/-/`/`ВП` before the next X2-affecting op, including
  direct `В/О` returns, or lifts the stack for a downstream consumer through
  direct or proved-indirect flow. If X2-register/value/shape dataflow proves
  that X2 already contains the same register, decimal fact, or structural
  hex/super display shape and a preserving command remains before the restore,
  the recall is no longer considered the required sync. The same shared guard
  also permits a direct or proved stable-indirect decimal/structural recall
  immediately before `ВП` when the active decimal mantissa or structural `ВП`
  source already matches the recalled source; recalls before a free-standing
  `/-/ ... ВП` gap are handled the same way when the store-backed decimal sign
  source, a proved closed `X == X2` normalized-decimal sign source, or a proved
  shared structural hex/super source shape already matches the recalled source,
  including through transparent direct/proved-indirect return helpers. A store or other
  context-closing command keeps the recall as the visible source. Mutating
  `R0..R6` indirect
  selectors are kept because their selector side effects are observable.
- `dead-store-elimination` — full pass removing pointless direct stores and
  stable-indirect `К X->П R7..Re` stores with proved memory targets, while
  keeping stores that are observable through number-entry, proved indirect-flow
  liveness, mutating `R0..R6` indirect selector side effects, or the `ВП`/X2
  restore context.
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
8. `store-recall-peephole` — removes `X->П r` immediately followed by `П->X r`, stable-indirect proved same-cell `К X->П R7..Re` followed by `К П->X R7..Re`, or an adjacent recall to another cell when the shared value/shape proof shows the recalled decimal value or structural hex/super display shape is already visible in X, including exact decimal display-shape versus ordinary decimal-value equality after restored-visible normalization and non-negative structural exponent shifts such as `hex:Г; ВП 2` matching `hex:Г00`. The rewrite fires only when the recall is not the last X2 sync before a context-sensitive `.`/`/-/`/`ВП` restoration before the next X2-affecting op, including direct conditional/`F Lx` fallthrough syncs and direct `В/О` returns, or when the same shared proof shows X2 already carries the recalled decimal value, an exact decimal display-shape's synced normalized value, or structural hex/super shape across an X2-preserving gap. Exact-display normalized-value proofs can come from hidden X2 value facts or hidden dot-safe decimal display-shape facts, but still cover `.` restore exposure only; they are not accepted as general `/-/` or `ВП` previous-command context replacements. Shape/value equality for visible X does not promote raw decimal mantissas or exact display-shape facts into general redundant X2 sync proofs for future `.`/`/-/`; the hidden decimal value or dot-safe display shape must already have been produced by a real X2-sync boundary. A direct or proved stable-indirect decimal/structural recall immediately before `ВП`, or before an X2-preserving gap and then `ВП`, can also be removed when the active decimal mantissa, exact decimal display-shape source, or structural `ВП` source already matches the recalled source through the shared VP source-key algebra; that display-shape proof is VP-only and is not counted as a general redundant shape sync for `.`/`/-/`. Decimal recalls before a free-standing `/-/ ... ВП` gap are also removable when the store-backed sign source, proved closed `X == X2` normalized-decimal sign source, exact decimal display-shape sign source, or proved shared structural hex/super source already matches the recalled mantissa/source, including through transparent direct/proved-indirect return helpers. A store or other context-closing command keeps the recall as the visible source. Its stack lift still cannot reach a downstream binary/stack-consuming op through direct or proved-indirect flow; mutating `R0..R6` indirect selectors and loop-counter recalls are not folded when the hardware side effect is observable.
9. `pre-shift-stack-lift` — removes `В↑` before direct/indirect `П->X`, `F pi`, another stack-shifting producer, a linear `В/О` return, or a direct conditional/counted-loop fallthrough X2 sync, possibly through stack-preserving labels/stores/plain ops, path-safe direct conditional/counted-loop/proved-indirect conditional fallthroughs, and stack-preserving direct/proved-indirect return-helper chains, when that following operation already supplies the current X in Y or syncs the same X into X2, unless the call-return-aware CFG stack/X2 exposure proofs show that some skipped or downstream edge can observe the removed lift/sync. It can also use a following stack-preserving direct/proved-indirect return helper as an X2 sync even when the local gap or the helper computes a new X before `В/О`, provided the scanned gap preserves stack and does not perform a context-sensitive X2 restore, and the callee has no stack mutation or context-sensitive X2 restore before return. The forward and backward X-preserving-X2-sync proofs are shared helper logic, while the forward return-sync proof deliberately uses the looser stack-preserving/no-restore gap because the return overwrites X2 from the helper's returned X. Plain X-preserving syncs, direct/loop fallthrough syncs, proved stable-indirect conditional fallthrough gaps, linear `В/О`, transparent direct/proved-indirect return helpers, and hard X/X2 overwrites use one scheduler model with memoized transparent-helper proofs. It also removes a `В↑` after any proved stack-lift + X2-sync producer (`П->X`, proved stable `К П->X`, or another `В↑`), X-preserving X2 sync, hard X/X2 overwrite, or stack-preserving return helper that syncs the helper's returned X across local stack/X2-preserving gap cells, path-safe conditional fallthroughs with known X2 effects, and direct/proved-indirect return-helper chains that preserve X, stack, and X2, when the added stack lift cannot reach a consumer and no targeted entry label, display/X2 restore context, stack consumer, recursive helper cycle, unknown indirect conditional, jump edge entering the scanned range without the fallthrough sync, or other flow command interrupts the producer-to-lift proof.
    The scans for the next stack-shifting producer and previous/following dead hard X2 overwrites are
    shared helper code (`x2NextStackShiftingProducerIndex`,
    `x2NextHardX2OverwriteIndex`, `x2PreviousHardX2OverwriteIndex`,
    `x2NextStackPreservingReturnX2SyncIndex`,
    `x2PreviousStackPreservingReturnX2SyncIndex`), so later stack+X2 scheduler rewrites use
    the same fallthrough, direct-return, and stack-preserving-gap rules instead
    of reimplementing them locally. Plain context-sensitive X2 restores
    (`.`, `/-/`, `ВП`) and display-sensitive cells are barriers for those
    scans even when their stack profile is otherwise preserving.
10. `jump-to-next-threading` — removes unconditional jumps where target is the next label in sequence.
11. `jump-thread` — threads labels by replacing jumps to label chains with the final target label.
12. `flow-x-reuse` — runs forward CFG data-flow for values already held in X and removes a direct `П->X r` or stable-indirect `К П->X R7..Re` with a proved memory target when every predecessor reaches that point with the same value still in X, including concrete decimal equality proved through X2 register-memory or decimal preload metadata after X was rebuilt; proved indirect flow targets (`indirect-target=NN`) are included in the CFG, direct and proved-indirect `ПП`/`В/О` edges carry X facts into callees and back to continuations, documented empty operators `К НОП`/`К 1`/`К 2` preserve X facts, stable selectors preserve the X fact, counted-loop `F L0`..`F L3` backedges preserve visible X for non-counter registers while dropping the decremented counter alias, mutating selectors drop only the mutated selector register from the proof, and unknown indirect flow plus absolute numeric direct targets are still refused. Recalls that provide the last X2 sync before `.`/`/-/`/`ВП` before the next X2-affecting op, including direct `В/О` returns, or a stack lift that can reach a downstream consumer through direct or proved-indirect flow are kept.
13. `branch-target-x-reuse` — removes the first direct or stable-indirect proved recall in a unique branch target when the source direct conditional, counted loop, or proved stable-indirect conditional target preserves the same recalled value in X, or when the target-entry X2 register/value/shape proof shows the same register, decimal value, or structural hex/super shape is already visible. Loop-counter recalls and mutating indirect selector recalls are excluded because `F Lx`/`К x?0 R0..R6` mutate those registers. `С/П` counts as a no-fallthrough separator for the uniqueness check. Target uniqueness is resolved by the actual executable entry index, so alias labels or alternate numeric/indirect entries to the same cell keep the recall. The proof may cross free-standing stack/X2-preserving empty prefix cells, direct stores, proved stable-indirect stores, and address cells before the target recall, carrying the branch's projected jump-edge X2 register and value/shape state through those cells; a prefix store can itself prove that the later recalled register is already visible in X. X2-sensitive checks are run against that projected path state, not only the globally joined target state, so unique branch entries can reuse X2/sign-source facts even when ordinary dataflow also models continuation after `С/П` or cannot follow numeric direct targets. The rewrite is refused when the target recall is needed as a `.`/`/-/`/`ВП` X2-sync boundary before the next X2-affecting op, including direct `В/О` returns, or a stack lift that can reach a downstream consumer through direct or proved-indirect flow.
    These recall-removal guards read the shared `OpcodeInfo.stackEffect`
    profile, so stack-preserving, shifting, Y-consuming, exposing, and barrier
    opcodes are modeled consistently across passes.
    The shared X2 helpers also treat `К x?0 r` as a path-sensitive conditional,
    but unlike direct `F x?0` commands both edges preserve X2. On the proved
    jump edge they still drop only facts about a mutating `R0..R6` selector.
14. `stable-indirect-flow` — rewrites direct `jump/call/cjump` to indirect forms (`К БП`, `К ПП`, `К <cond>`) when a stable selector is already live in a register.
15. `preloaded-indirect-flow` — preloads a selector value into a spare stable register and rewrites repeated backward-direct numeric jumps/calls through that preloaded value; after rescue starts, subsequent proved shrinking rewrites are still accepted below the official window.
16. `indirect-memory-table` — rewrites direct `store/recall` into `К X->П`/`К П->X` when a stable selector maps to the indexed target cell.
17. `x2-noop-restore` — removes `.` when X2 value dataflow proves that `X`
    already contains the same hidden X2 value, including register aliases,
    normalized integer or fractional decimal digit-runs (`decimal:12:normalized`,
    `decimal:1.2:normalized`), signed digit-runs
    while number entry is still open (`decimal:-12:normalized`,
    `decimal:-1.2:normalized`), and the
    normalized zero from `Cx`; leading-zero runs are split
    (`X=decimal:2:normalized`, `X2=decimal:02:unnormalized`, likewise `-2`
    vs `-02`) so they do not satisfy the exact equality proof, but a separate
    visible-decimal proof can still remove `.` when the restored display value
    is the same and no later context-sensitive restore observes the raw X2
    mantissa shape. The same proof accepts emulator-pinned dot-safe structural
    single-hex mantissas `A`/`B`/`C` after closed-context sign-pair modeling:
    when visible `X` and hidden X2 carry the same structural restore key, the
    trailing `.` is removable under the existing exposure guards. Unsafe
    structural digits (`D`/`F`), structural exponent shapes, and observable
    next-`ВП` contexts remain blocked. The value proof also
    models closed-context `.` as a real X2-to-X restore, normalizing decimal
    facts only for visible `X`. Open number-entry dots are modeled separately
    as decimal separators: `1.` remains an open raw mantissa with
    `X2=decimal:1.:unnormalized`, and following digits such as `1.2` continue
    the fractional digit-run without making the separator itself a removable
    no-op restore. The value proof also treats `В↑` and `F0..FF` empty opcodes as
    X-preserving X2-affecting commands: when `X` is already proved, those
    opcodes sync the same fact into `X2`, including normalized visible values
    whose old X2 form had leading zeroes or came from a non-normal structural
    arithmetic display such as `A^2 -> 00` or `A * 18 -> 020`. A dot-restored
    leading-zero X2 form
    is deliberately not upgraded into an ordinary `ВП` mantissa source:
    `02; К{x}; .; ВП; 3` yields `22000` on the emulator, not `2 ВП 3`.
    When the same dataflow proves that `.` would keep the exact same
    `ВП`-entry source, the dot is removable before immediate `ВП` only if the
    explicit sign-source is also unchanged; signed-zero and unknown recall
    contexts therefore stay conservative. The dot is also removed when the next
    context-sensitive restore is reached only through a free-standing
    `КНОП`/`К1`/`К2` and `/-/` restore gap before `ВП`: emulator tests cover
    normalized and signed normalized mantissas in this exponent-entry shape,
    and store-backed sign sources where the dot-restored mantissa matches the
    original hidden mantissa used by `/-/ ... ВП`, while role-bearing display
    cells still block the shortcut.
    That restore-gap proof may cross a direct or proved-indirect `ПП` helper
    chain that reaches `В/О` through only nested transparent helper calls and
    restore-transparent empty/address cells; helpers that store, branch,
    restore X2, recurse, or expose another entry remain barriers. This
    transparent-helper proof is memoized per IR body and shared by the X2
    restore-gap scanners. The same direct-return context is used for closed-context `/-/` dot sources, so a transparent
    helper or orphan address-byte gap between the modeled sign-change and the
    candidate `.` does not reset the proof, while a helper that performs its own
    X2 restore still blocks it.
    A separate normalized/visible-decimal/dot-safe-structural escape hatch
    handles proved X2-preserving gaps such as stable indirect conditionals: if
    `X` and `X2` carry the same normalized decimal fact, restore to the same
    visible decimal after raw X2 normalization, or carry the same
    emulator-pinned single-hex `A`/`B`/`C` restore key, and the local gap back
    to the X2 sync contains no display-focused cell, `.` can still be removed.
    This escape hatch uses the
    same direct-return context as the `ВП` restore-gap and closed-context
    `/-/` source proof: transparent direct or proved-indirect helper chains may
    be crossed, while helpers that store, branch, restore X2, recurse, or expose
    entry state remain barriers. Display-byte gaps remain explicit.
    Closed-context `/-/` is modeled for
    proved normalized decimal `X == X2` facts, including zero, and for raw
    decimal X2 facts that restore to the same visible decimal after the sign
    toggle; this lets an immediately following `.`, or one reached only through
    free-standing `КНОП`/`К1`/`К2`, be removed unless it would shape a later X2 restore
    context. `ВП` after an open mantissa creates both a structural exponent-entry
    state and a separate VP/exponent context. `ВП` after a proved closed
    decimal X2 sync (`Cx`, `В↑`, direct conditional/`F Lx` fallthrough, or `F0..FF`,
    possibly through only `КНОП`/`К1`/`К2`) also becomes a structural
    exponent-entry state. Exact normalized scientific decimal X2 facts feed the
    same source proof as decimal mantissas after `.` restore without becoming
    ordinary mantissa shapes: `F 10^x(8); В↑; .; ВП; 2` proves `1E10`, and
    `1E-8; .; ВП; 2` proves `1E-6` while keeping `exponent:*:*:decimal`
    display metadata. Direct `X->П r` and `К X->П r` have a separate
    decimal store-splice proof: when the hidden X2 shape is a proved decimal
    mantissa, the following `ВП` gets the hardware-spliced source
    (`12; X->П; ВП` starts from `2`, `05; X->П; ВП` starts from `5`,
    `1.2; X->П; ВП` starts from `0.2`, non-zero integer tails that become all
    zero start from raw `0.` rather than normalized `0`, all-zero integer runs
    preserve their raw length, `-2; X->П; ВП` starts from `-9`, and signed zero
    starts from `-1`). Structural hex/super mantissas use the same immediate
    store-splice boundary as shape-only transient sources: the first structural
    display digit is removed (`FACE; X->П; ВП` starts from structural `ACE`),
    and closed structural exponent-entry shapes first pass through the shared
    restored-shape algebra (`hex-exponent:Г:2 -> hex:Г00 -> hex:00` for the
    immediate store splice). No decimal value or dot-safe restore fact is
    created. This proof is deliberately derived from hidden X2 shape, not
    visible `X`, and it is not generalized to arbitrary X-preserving commands
    because the MK-61
    previous-command context changes what `ВП` restores. A proved `/-/` carries the same fact with the mantissa sign
    toggled; after a closed, value-proved decimal exponent-entry sync it also
    keeps the signed exponent shape (`5 ВП 3 F0 /-/` carries
    `exponent:-5:3:decimal`, and leading-zero forms stay shape-only metadata).
    Closed `/-/` can also use exact decimal display-shape equality when visible
    `X` and hidden X2 spell the same display through different shapes, for
    example `exponent:100:0:decimal` versus `mantissa:100:decimal`; that is a
    sign-source proof, not a new dot-safe restore proof. The same sign-source
    proof accepts mixed ordinary decimal value versus exact exponent
    display-shape equality in either direction. When hidden X2 is only the
    display shape it still emits only signed display-shape metadata plus
    `expr-key:0B(shape:...)`, not a hidden dot-safe decimal value; when hidden
    X2 is already a normalized decimal value, the signed value remains dot-safe.
    Shape-only decimal display sign changes also seed stable
    `expr-key:0B(shape:...)` facts for hidden-temp equality after an explicit
    X2 sync. The raw-X2 leading-zero path remains separate, so visible `2` with
    hidden `02` still produces hidden `-02`; a visible normalized decimal value
    can prove that sign source, but the hidden X2 spelling remains
    unnormalized.
    Zero is represented as a distinct `-0` mantissa shape rather than
    normalized away, including during open digit entry (`0 /-/`) and after
    closed zero syncs, because the emulator distinguishes `Cx /-/ ВП` from
    `Cx ВП` even though visible `X` normalizes both decimal values to `0`.
    Ordinary digits after an
    X2-preserving gap start fresh number entry, but `/-/` can still see and
    update that VP context. A non-zero exponent-entry form can also become a
    normalized decimal value fact after an X2-affecting, X-preserving sync
    closes it (`5 ВП 3 F0` proves `decimal:5000:normalized`,
    `1.2 ВП 3 F0` proves `decimal:1200:normalized`,
    `5 ВП 3 /-/ F0` proves `decimal:0.005:normalized`, and
    `5 /-/ ВП 3 F0` / `5 /-/ ВП 3 /-/ F0` prove
    `decimal:-5000:normalized` / `decimal:-0.005:normalized`, clearing the VP
    context). Hidden exponent forms that remain under observable VP context
    still do not become dot-safe value aliases; emulator probes show that later
    `.` can signal `ЕГГ0Г`. A separate shape lattice now tracks decimal
    mantissa and exponent-entry forms independently from value facts: for
    example unsafe `05 ВП 3` is preserved as `exponent:05:3:decimal` without
    granting a `decimal:*` equality fact, while safe `5 ВП 3` carries both the
    exponent shape and `mantissa:5000:decimal`. A closed-context `.` restore
    from a raw decimal X2 value such as `decimal:02:unnormalized` seeds the next
    `ВП` mantissa source from its restored visible value (`2`), while the raw
    display shape remains separate metadata and is not promoted into a general
    display-shape proof. Exact decimal display shapes can become normalized
    value facts only at a real X2-sync boundary: plain X2-affecting commands,
    branch-specific conditional syncs, and direct `В/О` returns copy the proved
    visible decimal display value into hidden X2, while active exponent-entry
    forms still remain shape-only. Direct conditional jump edges that preserve X2 remain
    conservative for ordinary `ВП` sources, but they carry the explicit raw
    decimal sign-source plus exact decimal display-shape and structural
    sign-shape metadata used by a following closed-context `/-/ ВП` sequence.
    Structural hex/super mantissas
    consumed by `ВП` become shape-only `hex-exponent:*:*` /
    `super-exponent:*:*` forms; exponent digits and exponent `/-/` update that
    structural context without creating decimal value facts. The construction is
    routed through the shared shape algebra, which can rebuild canonical
    mantissa facts, derive structural exponent-context sign toggles, derive
    closed-context mantissa sign toggles for synced structural shapes through
    the same restore-equality model, and compare structural exponent shifts
    that are pure display mantissa
    shifts (`hex:Г` through `ВП 2` is the same structural display shape as
    `hex:Г00`; `hex:Г ВП -2` is the same structural display shape as
    `hex:0.0Г`; shifted two-byte `super:FA` forms compare as the resulting
    hex-like display mantissa). `ВП` source comparisons now use the same
    source-key algebra for decimal mantissas and structural restore shapes,
    rather than separate exact-set checks. Closed structural exponent-entry
    shapes also feed `ВП` source proofs through that restored mantissa form, so a
    later `.` restore of `hex-exponent:Г:2` exposes `hex:Г00` as the next
    structural mantissa source instead of dropping the context. The same
    algebra now has shape-only structural digit append/concat operations
    (`hex:8.7` + `hex:0Е` proves `hex:8.70Е`) with eight-display-digit
    bounds and no signed/fractional structural operand; either side may also
    be a pure decimal digit-run when the other side proves structural content
    (`hex:8` + decimal `02` proves shape-only `hex:802`, decimal `8` +
    `hex:1` proves shape-only `hex:81`) or a restored structural
    exponent-entry with a pure mantissa (`hex:A` + `hex-exponent:B:2` proves
    shape-only `hex:AB00`) without becoming a decimal value. Exact decimal
    display-shape facts feed unary display-shape
    results and, when the unary result itself is proved exact, concrete decimal
    result facts: `exponent:5:-1:decimal` through `К {x}` yields both the
    displayed fractional shape and `decimal:0.5:normalized`. The same shape
    algebra exposes a closed display form for decimal exponent-entry facts
    without making them dot-safe hidden-X2 values: `exponent:5:3:decimal`
    closes to `mantissa:5000:decimal`, while wide scientific forms such as
    `exponent:100000000:2:decimal` close to `exponent:1:10:decimal` and remain
    error-prone shape metadata. Decimal display closure and structural
    exponent closure now feed one restored-display shape set, while stable
    source keys use the canonical source subset (for example
    `hex-exponent:Г:2` keys as `hex:Г00:mantissa`). VP first-digit splice
    sources and closed sign/source comparisons consume the same restored-display
    layer instead of rebuilding decimal and structural cases separately. The same
    restored-visible decimal extractor is used for binary concrete arithmetic
    operands, so shape-only decimal displays can participate in exact
    `+`/`-`/`*`/finite-division proofs and in the pinned structural
    hex-versus-decimal tables without making the original display-shape source
    a dot-safe X2 alias.
    The same exact display-shape proof can feed a following ordinary `ВП`
    through the restored visible decimal mantissa: a synced
    `exponent:1:8:decimal` re-enters `ВП` as mantissa `100000000`, enabling
    exponent separator/sign rewrites while still withholding any dot-safe
    `decimal:*` hidden-X2 alias.
    Restored visible-decimal comparisons also mix ordinary decimal value facts
    with exact decimal display-shape facts (`decimal:0.5:normalized` equals
    `exponent:5:-1:decimal`), while raw entry spellings such as
    `mantissa:0.5:decimal` remain shape-only and do not become exact restored
    values. It
    also models the
    X2-preserving first-command `ВП` first-digit splice as a structural source:
    a proved visible first digit and a proved hidden decimal/structural mantissa
    tail can form a new shape-only source (`hex:A` with hidden `hex:8A0` gives
    `hex:AA0`; hidden decimal `800` gives `hex:A00`) for the following exponent
    entry. Decimal first-digit plus decimal tail uses the same context rule for
    ordinary decimal exponent-entry facts: immediate `←→; ВП` inherits the old
    decimal tail (`800`), while an empty preserving gap can prove the current
    visible first digit plus the hidden tail (`3` with `800` gives `300`).
    Non-empty X2-preserving commands create a transient proof for the immediate
    `ВП`; a later empty command drops that transient source and proves a fresh
    source from the current visible `X`. Structural forms remain shape-only and
    are not used as decimal value or dot-safety evidence. Value-set membership
    checks for stable expression keys go through the same canonical structural
    source-key layer as value-set joins, so a legacy/raw
    `expr-key:...(shape:hex-exponent:...)` fact and the restored
    `expr-key:...(shape:hex:...:mantissa)` spelling are treated as the same X2
    computed value without promoting either shape to a decimal alias. The same
    stable-key layer can evaluate nested `expr-key:*` operands back into
    proved decimal/display-shape facts when every opcode and operand is already
    covered by the concrete evaluator; for example an `F pi` key can feed
    integer-part proofs, and a structural `К |x|` key can feed a following
    `К ЗН` proof through its canonical shape. Unproved hex/super expressions
    remain opaque. A real X2 sync boundary (`В↑`, X-preserving `F0`..`FF`,
    direct/loop/proved-indirect fallthrough, `В/О`, direct `П->X`, or proved
    indirect `К П->X`) materializes the stable-key decimals and display shapes
    that this evaluator can prove into hidden-X2 facts, so computed stable
    values stay structural before the sync but become ordinary restore evidence
    after the calculator would have copied visible X into X2.
    The same
    canonicalization is applied after register-dependency and address-local
    opaque-expression cleanup, when sync/preserve transfer functions carry
    visible X/X2 value facts forward, and when stack copies move value facts
    through `Y`, so surviving stable `expr-key:*` facts do not drift back to
    raw structural spellings after selector mutation, calls, backedges, empty
    plain opcodes, or `X↔Y`/`Y->X` stack shuffles. X2-preserving transfers that
    carry register value/shape memory clone it through this canonical layer
    instead of sharing raw memory objects, including labels, empty/plain
    opcodes, dot/sign/VP entry transitions, stack moves, and conditional/return
    close paths. Direct and proved-indirect stores also materialize proved
    stable-key decimal values and display/structural shapes into register
    value/shape memory, so later recall and recall-elimination proofs see the
    same facts without waiting for an actual recall sync. If a join or older
    path still has only stable value-memory, recall-elimination shape proofs
    derive the same display/structural facts from those stable keys instead of
    requiring a separate shape-memory fact. State-level restore-safety and
    same-X/X2 shape predicates use the same effective-shape view, so a stable
    `expr-key:*` value can prove structural or dot-safe structural context even
    when the explicit `xShape`/`x2Shape` set is empty. CFG and register-memory
    joins use the same effective value/shape sets, preserving proved stable-key
    decimals and display/structural shapes when one path has already
    materialized them and another still carries only the stable key. Raw decimal
    spellings remain exact facts, so leading-zero X2 values such as `02` still
    do not merge with normalized `2`. The proved decimal
    first-digit source is visible to `vp-splice`, so an empty separator and a
    cancelling exponent sign pair before that `ВП` can be removed together. It
    also carries exact emulator-pinned single-digit hex
    arithmetic tables as decimal value proofs. `F x^2` has a unary
    single-significant-hex-digit model: leading zeros before the digit are
    accepted, trailing digits are refused (`B0` is not treated like `B`), and
    the display shape keeps the ROM spelling (`A^2` has normalized value `0`
    but display shape `00`). A later explicit X2 sync records the normalized
    hidden restore shape for non-normal decimal displays (`00 -> 0`,
    `020 -> 20`), while signed zero remains sticky as `-0`; this same
    normalization is applied to plain X2-affecting opcodes, direct conditional
    and counted-loop fallthrough syncs, and direct `В/О` return syncs. Operand order remains part of the binary
    proof. For `+` and `-`, a single `A`/`B`/`C`/`D`/`E` hex digit paired with a
    proved decimal operand `0`..`18`, or with another verified single
    `A`/`B`/`C`/`D`/`E` hex digit, uses the verified operand-order-specific table,
    including cases such as `Г + 4 -> 17`, `3 + С -> 5`, `A + B -> 5`,
    `A + 18 -> 28`, `18 - B -> 23`, `Г + Е -> 11`, `С - 2 -> 0`,
    `0 - С -> -2`, and `A - Е -> -4`;
    `+`/`-` now use the same product-backed value/display-shape proof path as
    `*`/`/`, so future hex/super arithmetic extensions cannot split numeric and
    display evidence by accident.
    The regular `F x^2` value model can then derive follow-on
    values such as `1`/`4`/`9`. The verified
    single-hex-digit multiplication table is also operand-order-specific:
    `A`/`B`/`C`/`D`/`E` in `Y` is modeled for pinned decimal right operands
    `0`..`18`, preserving display shape such as non-normal
    `A * 16 -> 000` and `B * 17 -> 043`; with a hex digit in `X`, verified `A`/`B`/`C`/`D`
    behave as decimal-times-ten display results for `0`..`18` while `E` gives zero. Verified
    hex-pair products `A`..`E` by `A`..`E` are also modeled with their observed
    display spelling, including non-normal `A * A -> 00` and `B * Г -> 10`.
    Division has separate emulator-pinned tables for `A`..`E` divided by
    decimal `1`..`18`, decimal `0`..`18` divided by `A`..`E` where the emulator
    does not signal `ЕГГ0Г`, and `A`..`E` divided by `A`..`E`, with
    display-shape preservation; for example `Г / 8 -> 1.625`,
    `A / 10 -> 0E-1`, `Е / 17 -> 8.2352941E-1`, `16 / B -> 9.2525252`,
    `15 / E -> 0.2292929`, `A / Г -> 4E-1`, `С / B -> 1.2525252`, while
    error cases such as `10 / A -> ЕГГ0Г` and `A / С -> ЕГГ0Г` stay outside the proof.
    Hidden X2
    remains the right operand until an explicit sync, so literal/scratch restore
    rewrites consume these table facts only after a later X2-syncing command. A
    second verified family covers structural `A`..`E E-2` exponent arithmetic
    without promoting unsupported hex/super shapes into ordinary decimal values.
    Addition/subtraction is formulaic only for emulator-pinned decimal operands
    (`0`..`18`) and proves cases such as `BE-2 + 0 -> 1.1E-1`,
    `ЕE-2 + 6 -> 6.14`, `BE-2 - 17 -> -16.89`, and
    `17 - BE-2 -> 17.05`. Multiplication/division now use the same pinned
    `A`..`E E-2` family, with cases such as `1 * ГE-2 -> 1E-1`,
    `AE-2 * 1 -> 0E-2`, `BE-2 * 18 -> 0.54`,
    `AE-2 * 15 -> 9.9`, `ГE-2 / 2 -> 6.5E-2`,
    `BE-2 / 18 -> 6.1111111E-3`, `12 / ГE-2 -> 000`,
    `18 / BE-2 -> 943.43434`, and `16 / ЕE-2 -> 052.92929`; error pairs such
    as `1 / AE-2` and `3 / CE-2` stay opaque. Each operand order uses only its own emulator-pinned
    results and records display shape independently from normalized value shape,
    so later store/recall/`ВП` context stays equivalent to the MK-61 display
    state.
    Reverse decimal/hex division has a separate emulator-pinned table for
    selected decimal `0..9`/`18` left operands and structural `A`..`E` right
    operands; `ЕГГ0Г` pairs and unsupported display forms remain absent from the
    proof lattice.
    This is not a
    general wide multiply/divide, borrow/carry, or decimalization rule.
    Structural `К |x|` removes the sign from canonical hex/super mantissa or
    closed exponent-entry restore shapes as another shape-only transform; the
    hidden X2 shape is still preserved by the opcode and is not decimalized.
    Structural `К ЗН` has a narrow emulator-pinned value model:
    canonical hex mantissas or closed structural exponent mantissas whose first
    significant nibble is `1..E` seed exact decimal `1`/`-1` facts plus the
    matching decimal display shape, while `F`-leading forms and `super:*` shapes
    remain opaque. Closed-context `/-/` on a proved shared structural X/X2
    source is still shape-only, but it also emits a stable
    `expr-key:0B(shape:...)` fact keyed by the canonical restored structural
    source. Decimal display-shape-only sign sources use the same key model.
    The proof first materializes stable `expr-key:*` display shapes into the
    same shape algebra, so a computed value whose only concrete form is inside
    a stable key can still seed the signed decimal/structural X2 state without
    requiring a separate explicit `xShape`/`x2Shape` fact.
    That lets repeated structural or display-shaped sign toggles meet in hidden-temp
    dataflow after an explicit X2 sync without promoting either side to a
    decimal value. These are structural display proofs only: they still refuse
    hex/super arithmetic, carry, or decimalization proofs.
    Structural equality uses canonical shape reconstruction, so equivalent
    hex/super spellings compare as the same shape without becoming decimal
    values. Shape-set joins and equality checks use the same canonical
    spelling, so branch-merged structural `ВП`/restore proofs do not split on
    formatting. The shared VP-source helpers use the same materialized
    stable-key shapes after X2-sync boundaries: computed decimal display shapes
    can seed exact `ВП` mantissas, and computed structural shapes can seed
    structural `ВП` sources, even when the concrete form exists only inside an
    `expr-key:*` value fact. First-digit `ВП` splices use the same materialized
    source/target shapes across X2-preserving commands, so a computed stable-key
    `X` shape can provide the leading digit and a computed stable-key hidden X2
    shape can provide the mantissa tail. Store-backed, direct-flow, and proved
    indirect-flow `ВП` splice helpers use the same effective-shape source, so
    they do not require a separate explicit shape fact when a stable `expr-key:*`
    already carries the decimal or structural display form. Signed-zero decimal mantissas (`-0`, `-0.0`, etc.) are kept as
    `errorProne` shape facts, not dot-safe decimal facts; shared signed-zero
    shapes can feed `ВП` source proofs after X2 sync or closed `.` restore,
    but never become ordinary decimal zero. Shared decimal exponent display
    shapes also survive closed-context `/-/` as signed display-shape facts
    without value promotion: `exponent:100:0:decimal` can become
    `exponent:-100:0:decimal` and `mantissa:-100:decimal`, but the presence of
    the exponent-entry shape keeps later `.` restore safety conservative. When a true merge sees
    different structural spellings with the
    same restored display mantissa (`hex-exponent:Г:2` vs `hex:Г00`), the join
    keeps that restored mantissa as a structural-only fact for `X`, `X2`, `Y`,
    `ВП` sources, and shape memory; identical straight-line facts are not
    expanded into aliases by themselves.
    Closed structural exponent shapes become fresh `ВП`-entry sources only
    after X2 value/shape dataflow proves the same structural restore-shape is
    visible in both `X` and hidden `X2`; the source is the restored mantissa
    shape (`hex-exponent:Г:2` seeds `hex:Г00`, and negative shifts such as
    `hex-exponent:Г:-2` seed `hex:0.0Г`), not a decimal value and not the old
    exponent-entry marker.
    Closed-context `.`
    restores carry structural hex/super hidden X2 shapes forward as structural
    `ВП`-entry sources and carry signed-zero decimal shapes forward as `-0`
    `ВП`-entry sources, while dot-restored leading-zero decimal forms are still
    not promoted to ordinary mantissas. Preloaded `П->X r` constants
    seed the same lattice: ordinary decimal/scientific constants with a one- or
    two-digit exponent become `decimal:*` facts and display-accurate decimal
    shapes. Ordinary decimal displays seed `mantissa:*:decimal`, while wide or
    small scientific decimal displays seed `exponent:*:*:decimal` rather than
    fake ordinary mantissas. Longer display-glyph runs such as `8Е000000`,
    hex-like display mantissas, and `FA`..`FF` super forms become
    structural-only `hex:*` / `super:*` shape facts. Structural
    preloads with a Latin `E` exponent marker (`ГE-2`, `FAE2`) seed
    shape-only `hex-exponent:*:*` / `super-exponent:*:*` facts; Cyrillic `Е`
    remains a display digit. Until those shapes are separately proved dot-safe,
    hex/super/display shapes remain
    structural only. The `ВП .` exception is modeled as a separate
    context-sensitive structural proof: if the active structural `ВП` context
    closes to a mantissa whose first significant nibble is `D`/`Е`, a following
    `.` reached through address-byte gaps and at most one role-free
    X2-preserving non-empty command can be removed when its result is
    immediately overwritten; this does not turn the shape into a general
    dot-safe value. `ВП /-/` is modeled as a
    signed exponent restore that materializes decimal value/display facts or
    structural exponent shapes in `X` and hidden X2 while preserving the active
    VP context; a later empty-op `ВП` can therefore splice a fresh first digit
    into the same signed decimal or structural order. `super:*` is narrower than `hex:*`: only optional-sign
    `F[A-F]` slot forms
    stay in the super lattice; other structural displays stay hex-only or
    unknown. Set-level shape proofs first canonicalize and drop invalid shape
    facts, including malformed decimal mantissas. Wide scientific decimal
    mantissas such as `100000000` remain valid only as exponent-entry sources,
    not as ordinary mantissa facts. Shape-memory stores only validated canonical
    structural spellings: unknown/invalid display shapes are dropped before they
    can be recalled as proof facts, while equivalent hex/super display shapes
    still join after store/recall proofs; active decimal and structural
    exponent-entry/VP contexts use validated canonical mantissa/exponent
    constructors, so invalid entry text, structural shapes, or malformed
    exponents cannot survive as splice proofs or single-fact shape safety claims.
    Dot restore and X2 sync
    shape normalization use the same canonical set layer, so legacy invalid
    shape facts cannot be reintroduced while visible `X` is rebuilt from hidden X2;
    during linear open decimal entry a repeated `.` is modeled as the MK-61
    no-op continuation of the same mantissa (`1 . 2 . 3 -> 1.23`), while a
    CFG entry to that `.` still remains a closed-context X2 restore and the
    following digit starts fresh input.
    structural VP context is not considered plain closed
    context by `.`/`/-/` rewrite guards. Closed-context
    `/-/` without a proved decimal, opaque, structural shape, or VP context stays
    unknown. The pass accepts either a
    safe dot-restore gap or a CFG-proven immediate no-op form after an
    X2-affecting sync such as `П->X r`/`Cx`/conditional fallthrough/direct
    `В/О` return, and refuses display/raw/context-sensitive follow-up
    `.`/`/-/`/`ВП` cases. The context-sensitive follow-up check uses the shared
    CFG-aware X2 exposure walker: direct and proved-indirect branches are
    followed path-sensitively, while opaque flow remains conservative. The
    VP-follow-up checks use the shared X2 restore-gap scanner: free-standing
    `КНОП`/`К1`/`К2` and `/-/` cells, marker labels, and transparent
    direct/proved-indirect return helpers are interpreted the same way by
    `x2-noop-restore`, `vp-splice`, and
    `x2-dead-restore-before-overwrite`; role-bearing/display-sensitive cells
    remain barriers. The
    VP-source escape keeps ordinary mantissa sources and sign mantissa sources
    as separate key sets, so a raw decimal dot restore can expose visible
    mantissa `2` for `ВП` while still proving that a following sign-gap `/-/ ВП`
    sees the raw sign source `02`.
18. `x2-dead-restore-before-overwrite` — removes a safe context-sensitive
    `.`/`/-/`/`ВП` restore, plus adjacent free-standing `КНОП`/`К1`/`К2`
    separators, when a following hard X/X2 overwrite such as `Cx` destroys the
    restored `X` before it can be observed. Consecutive same-segment dead
    restores and free-standing separators are removed as one run, while labels
    split the run and orphan address-byte cells are transparent but preserved. `.` requires a closed, non-`ВП`
    context with a proved decimal X2 value, an emulator-pinned single-hex
    A/B/C structural mantissa, or the same shared dot-restore safety proof used
    by `x2-noop-restore` (for example an immediate sync or closed sign-change
    dot source); active decimal/exponent-entry `.` cells are also dead when the
    following hard overwrite destroys that input context before observation. A bare `reg:r` fact after only an X2-preserving gap is
    intentionally rejected because a preloaded hex or non-normal register value
    can make `.` signal `ЕГГ0Г`. `/-/` may also be removed from open mantissa, active
    exponent-entry, or VP/X2 restore contexts because the following hard
    overwrite destroys both the restored X and the toggled X2. The following
    hard overwrite may sit after orphan address-byte cells or a direct/proved-indirect return-helper chain
    only when every nested helper is restore-transparent; display-sensitive separator cells are not
    transparent and are not removed from a same-segment dead restore run. A direct `П->X r` or proved stable-indirect
    `К П->X R7..Re` is treated as a terminal overwrite for an earlier dead
    restore, and as the same kind of dead X/X2 producer before a later hard
    overwrite, but only when the shared stack-exposure proof shows that the
    recall's stack lift cannot reach a later stack consumer. Free-standing
    stack-shifting plain opcodes whose metadata says they affect X2 and replace
    `X` are also terminal overwrite endpoints for earlier dead restores, and
    are themselves deleted before a later hard overwrite only when their
    produced `X` is not observed and their implicit stack lift is dead.
    `ВП` may also be removed from a structural
    hex/super `vpEntryShape` source, including one produced by an immediate
    direct/proved-indirect store-splice, a direct or proved-indirect `В/О` return continuation or the fallthrough side of a direct conditional/`F Lx`
    loop, or from an already active VP/X2 restore context, when the following
    overwrite destroys its visible result; the conditional jump edge does not
    invent such a source, and structural sources outside the pinned A/B/C
    mantissa set are not treated as dot-safe. Closed structural exponent sign restores are treated as structural
    shape-only `/-/` restores here: removable before a hard overwrite, but never
    promoted into decimal or dot-safe facts.
    This pass requests the register value-memory layer and also consumes decimal preload facts from `П->X r` metadata:
    direct stores of proved decimal `X` facts seed remembered `decimal:*` facts
    for later recalls, joins keep only facts common to every path, and unknown
    indirect stores clear the memory. Hex-like preload facts remain shape-only,
    so they do not make `.`/`/-/` dead-restore candidates.
19. `x2-hidden-temp-restore` — replaces a direct or stable-indirect proved scratch recall with `.` when X2 already carries the same value and either the `.` restore gap, a CFG-proven immediate X2 sync, a normalized decimal source fact already synced in X2 through a display-free local gap, a raw decimal X2 fact or exact decimal display-shape fact whose restored visible value equals the stored scratch value, a dot-safe decimal `mantissa:*:decimal` shape already restored by X2, an opaque `expr:<step>` or stable `expr-key:*` computed value, including one produced by a whitelisted pure X or X/Y computation and then explicitly synced into X2, an emulator-pinned dot-safe structural single hex mantissa (`A`/`B`/`C`) with the same restored shape already in X2, or a modeled closed-context `/-/` dot source through only free-standing `КНОП`/`К1`/`К2` empty ops is available, while also proving the recall stack lift is unobserved. A dead scratch store whose source is `reg:r` or an `expr-key:*reg:r*` value can also be matched after a later X2 sync of the same source, but only through a path-aware proof that every reaching path from the store to the scratch recall keeps every referenced register intact; straight-line code, direct/loop/proved-indirect conditional paths that either skip the recall or preserve the referenced register, transparent direct/proved-indirect return helpers, including nested helper calls, that may read but do not overwrite or mutate the referenced register, and preserving cells are allowed, while stores to a referenced register, indirect stores with unknown or matching targets, mutating `R0..R6` indirect selectors that are themselves dependencies, loop-counter mutation on a referenced register, recursive helper cycles, and unknown flow barriers keep the old scratch recall. Stable `expr-key:*` operands may come from canonical structural shape sources, canonical exact decimal display-shape sources, and decimal/register facts, so equivalent hex/super display forms such as `hex:Г ВП 2` and `hex:Г00`, exact scientific decimal display forms such as `exponent:1:8:decimal`, or shape-only ordinary decimal mantissas can prove the same opaque computed value without promoting either source to a decimal value or dot-safe restore source; stable constant stack producers such as `F pi` seed both the documented decimal/display-shape proof and the stable key, and nested stable keys are decoded back into concrete facts only when the shared evaluator can prove the result. For computed decimal/scientific temporaries, an explicit X2 sync plus exact decimal display-shape equality can discharge the unsafe-shape guard for the replacement dot; stable `expr-key:*` shape materialization now participates in the same restored-display and dot-safe shape comparisons, so an explicit shape set is not required when the key itself proves the shape. The dot itself is still admitted only by the normal dot-safety/immediate-sync proof. For computed structural temporaries, a synced `expr:*`/`expr-key:*` plus structural restore-shape equality can make the replacement `.` safe even though the shape remains non-decimal; path-sensitive conditional, loop, and direct-return syncs use the same normalized hidden restore shape as plain X2-affecting syncs, so non-normal structural decimal displays such as `A^2 -> 00` can still remove scratch recalls after the sync. Plain structural preload/register aliases that merely survived in X2 stay conservative outside the pinned `A`/`B`/`C` dot-safe set unless the VP-source escape proves that the inserted `.` immediately recreates the same structural `ВП` mantissa source and no sign restore gap intervenes. The VP-source escape uses the shared restore-gap scanner, so a transparent direct/proved-indirect return helper between that gap and `ВП` is allowed when the helper body cannot observe the restore; it also accepts the recall-removal VP-shape proof when replacing the scratch recall with `.` recreates the same mantissa source for the immediately following `ВП`. The raw decimal case covers visible-only leading-zero forms such as `01.2 -> 1.2`; it remains blocked when removing the recall would expose a following context-sensitive `.`/`/-/`/`ВП` restore that can observe the raw mantissa shape. The scratch-store proof can cross direct conditional/loop fallthrough syncs, proved stable-indirect conditional fallthroughs that do not mutate dependency registers, and direct/proved-indirect return helper chains that only read dependency registers; it can also use stable source facts from the dead scratch store when register-memory is too conservative. Mutating indirect-store selectors now invalidate dependent `expr-key:*reg:r*` memory facts, while stable selectors preserve them. This lets later DSE remove now-unused scratch stores.
20. `x2-literal-restore` — replaces a repeated explicit numeric literal with
    `.` when X2 value dataflow proves the same normalized decimal value is
    already in the hidden X2 register, the dot-restore gap is safe (or CFG
    proves the literal starts immediately after an X2 sync, including direct or
    proved-indirect `В/О` continuations, a normalized decimal fact survives through a
    display-free local gap such as a proved stable indirect conditional, or a
    modeled closed-context `/-/` reached through only free-standing
    `КНОП`/`К1`/`К2` empty ops), and removing number entry cannot
    expose a consumed stack lift. The pass uses the shared closed plain-context
    guard, so active decimal or structural `ВП` contexts keep their numeric
    entry explicit instead of being treated as ordinary decimal literal input.
    It recognizes ordinary integer or fractional
    digit-runs (`12`, `1.2`), their signed open-entry forms, and normalized
    exponent-entry literals such as
    `5 ВП 3`, `1.2 ВП 3`, `5 ВП 3 /-/`, `5 /-/ ВП 3`, or
    `5 /-/ ВП 3 /-/` once the prior value has been closed by a safe
    X2-affecting sync; role-bearing `/-/` cells are not parsed as replaceable
    literal sign suffixes. When a cell range can be read both as a full
    exponent-entry literal and as a mantissa prefix before `ВП`, the pass tries
    the full literal first and then the prefix; the prefix is replaceable only
    when the inserted `.` itself becomes the first X2 restore and recreates the
    same mantissa source for that `ВП`. Fractional digit-runs remain explicit
    before a following `ВП` source context; a repeated normalized non-zero
    integer digit-run, signed digit-run, or normalized exponent-entry literal
    with a non-leading-zero mantissa may also be replaced before an immediate
    `ВП`, or before a free-standing `КНОП`/`К1`/`К2` and `/-/` restore gap
    followed by `ВП`, including when a transparent direct/proved-indirect
    return helper sits between the gap and `ВП`: emulator tests prove that the
    inserted `.` preserves the same mantissa source for that exponent entry.
    Leading-zero and signed-zero forms are excluded from this
    shortcut because their restored mantissa shape is observable, and
    leading-zero exponent mantissas stay explicit for the same reason. The VP
    reachability guard is subroutine-aware: direct/proved-indirect calls carry a
    return stack, so a leading-zero literal before a transparent helper and a
    following `ВП` is still kept explicit. If the
    following code cannot observe that raw mantissa shape, normalized X2 facts
    may still replace leading-zero decimal literals by visible restored value
    (`02 -> .` after X2 holds `2`), but that restored-visible proof is accepted
    only for later `.` exposure, not as a general previous-command proof for
    `/-/` or `ВП`.
    Leading-zero exponent mantissas are normalized after
    that closing sync (`05 ВП 3` -> `5000`, `00 ВП 3` -> `10000`), but active
    exponent-entry X2 remains shape-only because an immediate `.` can still
    signal `ЕГГ0Г`. Exact scientific decimal values with at most eight
    significant digits may still be restored through `.` and then reused as
    numeric `ВП` mantissas (`100000000 ВП 2`, `0.00000001 ВП 2`), but their
    visible shape remains scientific rather than `mantissa:*:decimal`.
    Too-wide exponent forms, display/raw bytes, and later
    context-sensitive `.`/`/-/`/`ВП`
    observations are kept. The same CFG-aware exposure guard is used for the
    inserted dot, so a branch without a reachable preserving-edge restore is no
    longer a blanket blocker. When the only newly exposed context restore is a
    non-`ВП` `.` reached after an executable gap and the replacement dot
    restores the same exact or restored-visible X2 value, the pass treats the
    original literal sync as redundant; `/-/` and reachable `ВП` restores still
    require the stricter value/shape or mantissa-source proof above. Register value-memory can supply the same decimal
    fact after a direct or proved-indirect recall of a previously stored
    literal-shaped decimal.
21. `dead-store-before-commutative` — removes temporary stores that are followed by immediate `recall` + commutative ALU (`+` or `*`) and never read again before the next write of that register.
22. `dead-store-elimination` — removes direct stores, plus stable-indirect stores with proved targets, whose target register is not live after the write in a CFG that follows proved indirect flow targets (`indirect-target=NN`) and does not affect number-entry/input finalization or the previous-command context consumed by `ВП` while it restores X2; mutating indirect selectors are kept.
23. `last-x-reuse` — removes `П->X r` when `X` already contains `r` from the immediately preceding direct/proved-indirect `X->П`, a kept direct/stable recall, X2 decimal register-memory, decimal preload metadata, decimal display-shape memory such as `exponent:*:*:decimal`, or structural hex/super shape-memory proving that current X was rebuilt as the same concrete value/display shape, including exact decimal display-shape versus ordinary decimal-value equality after restored-visible normalization, possibly through documented empty operators `К НОП`/`К 1`/`К 2`, direct conditional fallthroughs, counted-loop `F L0`..`F L3` fallthroughs for non-counter registers, and unreferenced compiler marker labels, preserving recalls that serve as the last X2 sync before `.`/`/-/`/`ВП` before the next X2-affecting op, including direct `В/О` returns, or as a stack lift that can reach a downstream consumer through direct or proved-indirect flow; shape-memory proofs do not make decimal exponent, raw decimal mantissa, or structural `.`/`/-/` restores dot-safe; labels targeted by string, numeric, or proved-indirect flow plus procedure starts are entry barriers, and unknown indirect flow makes labels barriers too; mutating indirect stores can seed the X fact because the store remains, while mutating indirect recalls are not removed.
24. `r0-fractional-sentinel` — drops redundant immediate `П->X 3`/`X->П 3`
    after fractional-R0 indirect access when `R0` liveness proves that the
    direct access only repeats the hardware-selected `R3`; it also removes
    later `X->П 0`/`П->X 0` repetitions when the same straight-line path has
    already left the hardware `-99999999` sentinel in `R0` and `X` is proved to
    hold the same value, and rewrites direct `БП 99` / `ПП 99` / `F x?0 99`
    flow to `К БП 0` / `К ПП 0` / `К x?0 0` when `R0` is already proved fractional and the
    resulting sentinel write is dead. A final post-layout verifier can perform
    the same rewrite for label targets only after replacing the two-cell branch
    proves that the label will land exactly at hardware address `99`.
25. `indirect-selector-integer-part` — tracks the proof marker from
    `fractional-indirect-addressing` and removes a redundant `К [x]` after the
    same stable selector register is recalled as an already-truncated integer.
26. `address-code-overlay` — a final post-layout verifier moves labels from a
    single-cell op immediately after `БП target` or a proved-terminal
    `ПП target` onto the branch address byte when removing that op proves the
    address byte will be the same opcode. The overlaid executable cell may be
    an ordinary op or an existing numeric/formal address byte; if the overlaid
    opcode itself takes an address, the following operand byte is kept as that
    command's operand. Fixed numeric/formal branch operands are rejected when
    shrinking would move their real target. The same verifier can move the
    branch target label onto the branch's own address byte, allowing that
    operand byte to be the first executed opcode.
27. `vp-splice` — deletes redundant exponent-entry chains (`ВП ВП`) only
    when the first `ВП` is entered from a proved active number-entry context
    (`active-mantissa`, decimal exponent-entry, or structural exponent-entry);
    a closed-context X2 restore `ВП` can make a following `ВП` observable and
    is not treated as redundant,
    inert empty-op runs before `ВП`, including marker labels and orphan
    address-byte cells between the empty ops and the exponent-entry command,
    adjacent `/-/ /-/`
    exponent-sign toggles, and shape-proved empty separators after at least
    one exponent digit before a non-digit command. It also uses the separate
    VP/exponent context to remove empty separators before `/-/` after
    X2-preserving gaps such as `ВП 3 Fπ КНОП /-/`, and it can remove exponent
    sign toggles after a closed decimal X2-sync-fed `ВП` such as
    `2 F0 ВП /-/ /-/ 3`, or after a store-backed decimal splice such as
    `2 F0 X->П ВП /-/ /-/ 3` / `2 F0 К X->П ВП /-/ /-/ 3`; a non-zero restore run before the proved `ВП`
    (`2 F0 /-/ /-/ ВП 3`, `02 /-/ /-/ ВП 3`,
    `02 /-/ КНОП /-/ ВП 3`) can also collapse when the shared source proof
    shows the same mantissa reaches `ВП`. Structural hex/super preload shapes
    use the same shape-source proof without becoming decimal values. The
    signed-zero forms are kept because `0 /-/ /-/ ВП` still differs from
    `0 ВП`. Active decimal mantissa restore-runs are also compared through the
    shared VP source-key algebra: raw mantissas and their exact display shapes
    can match the source recorded immediately before `ВП`, while signed zero
    stays distinct.
    The pass consumes the shared VP shape-context classifier rather than
    decoding local `kind` strings: the classifier records active mantissa,
    active exponent-entry, and closed VP-context phases, decimal vs structural
    source, exponent-digit presence, and the exact splice actions that are
    safe for that state. Restore runs before a proved `ВП` are checked through
    the same helper for decimal active mantissas and shape-only structural
    sources; structural source equality now goes through the shared shape
    algebra, so shifted exponent-entry shapes such as `hex-exponent:Г:2` can
    match the equivalent `hex:Г00` source without becoming decimal values.
    First-digit splices use that same structural source path, so an immediate
    X2-preserving command before `ВП` can expose a proved pre-command `X` first
    digit plus a hidden decimal/structural X2 tail as the following
    exponent-entry source without adding a special-case rewrite. Decimal tails
    only become structural when the first digit proves a non-decimal display
    shape. The non-empty-command proof is transient: another executable command
    must establish its own source.
    The helper's restore-gap scanner and source-backed `.`-restore admission are
    shared with `x2-noop-restore`, `x2-hidden-temp-restore`, and
    `x2-literal-restore`, so all three passes use the same
    marker-label/display-sensitive/role safety rules before deciding that a
    `КНОП`/`К1`/`К2`/`/-/` run, including an empty-only run, can be ignored
    before `ВП` or before a proved source restore; orphan address-byte cells
    are treated as the same transparent gap elements here as they are inside
    transparent return helpers. The shared admission only
    proves that `.` is available as a restore mechanism; each pass still proves
    the concrete value, visible decimal, or structural source separately. With a
    direct-return context, the same scanner can cross
    direct or proved-indirect `ПП` helper chains whose bodies are only nested
    transparent helper calls and restore-transparent empty/address cells;
    helpers that store, branch, restore X2, recurse, or expose another entry
    remain barriers. The non-zero open/closed mantissa sign-pair
    proof before `ВП` uses the same transparent gap/helper crossing, so it is
    no longer limited to an immediately adjacent `ВП`; mixed shape-only
    structural restore runs (`/-/` plus empty cells before a structural `ВП`
    source) use the same crossing.
    After an X2-preserving gap, a VP-context sign or sign pair is kept when its
    X2 restore is observable (`5 ВП 3 Fπ /-/ С/П`,
    `5 ВП 3 Fπ /-/ /-/ С/П`), but a free-standing `/-/`/empty restore run can
    be dropped before a fresh digit (`5 ВП 3 Fπ /-/ 4`,
    `5 ВП 3 Fπ /-/ КНОП 4`, `5 ВП 3 Fπ /-/ /-/ КНОП 4`), because that digit
    starts new number entry and discards the restored `X`; labels and orphan
    address-byte cells inside the gap are preserved. The fresh-digit proof can cross the same direct or
    proved-indirect `ПП` helper-chain shape when every nested helper reaches
    `В/О` through only restore-transparent empty/address cells, because the helper cannot
    observe the restored `X` and the following digit starts fresh entry. The
    same fresh-digit proof also applies in proved closed plain context: a
    role-free `КНОП`/`К1`/`К2`/`/-/` run after a closing X2 sync is discarded
    before a following digit, but a following `ВП` remains observable and keeps
    the run.
    The
    same kind of restore run is likewise removed before a proved hard X/X2
    overwrite such as `Cx`, including across labels, orphan address-byte cells,
    and that transparent return-helper shape.
    The after-digit separator
    rewrite is deliberately shape-sensitive: the same empty op before the
    first exponent digit, or before another exponent digit, changes number
    entry and is kept. The check now uses the next executable opcode rather
    than the next IR cell, so marker labels or orphan address-byte cells
    between the separator and a following digit keep the separator, while the
    same transparent cells before a real non-digit close command are preserved
    and the separator can still drop.
    Closed-context `/-/ /-/` pairs are removed only when
    value dataflow proves the same sign source in visible `X` and hidden X2:
    an ordinary decimal/register/opaque fact, exact decimal display-shape
    equality such as `exponent:*:*:decimal`, mixed ordinary decimal value versus
    exact display-shape equality in either direction, or structural
    restored-display equality, including synced structural exponent shapes.
    Shape-only proofs cancel the
    pair without decimalizing the shape or making single restores dot-safe. The downstream
    scan still proves the pair is not acting as the previous-command
    shield for a later context-sensitive `.`/`/-/`/`ВП` restore. Register
    value-memory and decimal preload metadata can supply the same proved
    mantissa/exponent context after a direct or proved-indirect recall of a
    previously stored or setup-loaded literal-shaped decimal. The same source
    proof also compares exact decimal display shapes (`exponent:100:0:decimal`
    can match `mantissa:100:decimal`) while keeping leading-zero entry text such
    as `02` distinct from `2`. The same generic `ВП` source proof is also used
    for structural hex/super shapes after direct or proved-indirect return
    continuations and path-sensitive direct-conditional/`F Lx`
    fallthrough X2 syncs; structural arithmetic displays normalized by those
    syncs participate too, while zero-like cases keep the signed-zero guard.
    When such a source reaches `ВП`, the same context
    classifier carries `hex-exponent:*:*` / `super-exponent:*:*` through
    exponent signs/digits, so structural exponent sign pairs can collapse by the
    same rule as decimal exponent sign pairs, still without promoting those
    shapes into decimal value facts. Store-backed `ВП` sources keep a
    separate closed-sign source: `X->П`/`К X->П` followed by `ВП` uses the
    store-spliced hidden mantissa, including transient shape-only structural
    tails such as `FACE -> ACE`; but `X->П`/`К X->П` followed by `/-/ ВП`
    toggles the original hidden decimal or structural mantissa. Only empty
    X2-preserving cells preserve that sign source. Structural hex/super sources
    stay shape-only: the optimizer first prefers the ordinary shared structural
    equality proof, because it also carries stable `expr-key:*` facts. Explicit
    sign-shape metadata is used as a fallback source for both exact decimal
    display-shapes and structural shapes, and never promotes a transient
    store-splice tail into an ordinary `ВП` source.
28. `vp-exponent-splice` — optimization marker emitted to `report.optimizations` when at least one `ВП`/empty-op/sign redundancy optimization pass removes cells.
29. `vp-x2-peephole` — removes redundant `К {x}` that follows a compiler-owned `ВП`/X2 marker, display or ordinary, possibly through free-standing `КНОП`/`К1`/`К2` empty ops, other role-free X-preserving gaps such as `X->П`/`В↑`, unreferenced marker labels, and direct/proved-indirect return-helper chains whose bodies also preserve X, and reports `vp-fraction-restore` when one or more restores are removed. The removed `К {x}` is recognized by opcode rather than by a display/frac comment; the preceding `ВП` must carry a boundary marker because a plain opcode pattern such as `П->X r; Fπ; ВП` restores X2 but does not generally make `К {x}` redundant. The same pass also uses X2 value/shape dataflow to remove a role-free, non-display `К {x}` when closed-context `X` is proved to be an already-fractional decimal (`0`, `0.x`, or `-0.x`), including exact decimal display-shape facts that remain shape-only. It also removes role-free, non-display `К [x]` when closed-context `X` already has an exact integer display shape, including exact decimal structural mantissas such as `hex:123:mantissa`; value-only integer facts are not enough because `К [x]` could otherwise normalize a leading-zero or exponent display. Role-free `К |x|` is removed under the same display-shape proof only for exact non-negative integer displays, so negative values and raw display spellings remain observable. Since `К {x}`, `К [x]`, and `К |x|` preserve hidden X2, these no-op proofs do not require hidden X2 to match visible `X`; a later context-sensitive `.`, `/-/`, or `ВП` is allowed after a preserving executable gap because the restore's previous-command context is unchanged. Negative-integer `К {x}` is handled through the same visible-zero proof, but the first signed-zero-producing `К {x}` is kept if a later X2 sync can feed a context-sensitive restore; only repeated no-op fractional operations after visible zero is already proved may be removed. Immediate restore boundaries remain conservative unless a separate VP/source proof covers them.
30. `constant-folding` — deletes identity arithmetic operations (`0+` and `1*`) when both operations are explicit user-facing constants.
31. `duplicate-failure-tail-merge` — removes duplicated failure tails by redirecting the first tail to the second; this covers both `(label -> 0 -> pause)` and `(label -> pause -> same terminal flow)` forms.
32. `cse-display-block` — detects identical `recall/plain/.../return(stop)` blocks and replaces duplicates with one canonical block plus jump.
33. `dead-code-after-halt` — removes unreachable IR ops by CFG reachability from entry.
34. `register-coalesce` — merges non-overlapping register live ranges and, when enabled, performs copy coalescing for safe `recall/store` aliases.
35. `arithmetic-if-pass` — merges two branch paths that lower to byte-identical pure linear blocks (same side effects and same single-pass behavior).

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
