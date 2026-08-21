# Pending Optimizer

This directory is only for MK-Pro sources that compile through the normal
pipeline but still exceed the MK-61 105-cell window.

The pending sources below compile normally but still need generic optimizer
work before they fit. Previously pending examples that now fit were moved back
to the top-level `examples/` directory; keep their old optimization notes out
of this file.

Treat each file as an optimizer/lowering-size bug. Do not replace it
with a raw listing: the goal is to make the high-level source fit.

## Current Target

| File | Current | Target | Gap | Status |
| --- | ---: | ---: | ---: | --- |
| `tic-tac-toe-4x4.mkpro` | 136 | 105 | +31 | pending optimizer |
| `nekromant.mkpro` | 136 | 105 | +31 | pending optimizer |

The `Current` number is the local `--analysis` size. Strict `mk-pro compile`
mode may reject over-window programs earlier than the analysis path.

## Live Optimization Notes

- Preserve the source UI and behavior: the player retry stop for an occupied
  cell must still expose `X=-99999999`, ordinary answers must expose the stack
  pair `X:Y`, documented `1..4` inputs and ordinary signed/fractional aliases
  must normalize like the original, extreme inputs must retain the MK-61's
  eight-digit rounding, wins must report the original fractional packed mask,
  and move selection must keep the source-style max/tie behavior.
- Only the manual input/STOP protocol and its visible `X:Y` results are the
  external contract. Occupied cells and logical line evolution are an internal
  model used to prove future UI behavior; their physical registers, initial and
  intermediate representations, selector values, and preload layout are not
  externally fixed. Any replacement must still prove the same later UI.
- Both the input path and internal diagonal indexes now use the generic signed
  `grid_norm` primitive. This deliberately follows the calculator's decimal
  execution instead of assuming mathematical modulo at the eight-digit
  rounding boundary. The main remaining size pressure is helper and ABI
  traffic around `grid_norm`, `cell_mask`, `packed_score`, `candidate_score`,
  and the line-update/check path.
- The generic natural-target component layout now moves only fallthrough-closed
  machine regions after an exact CFG/return-stack proof. It places a helper at
  the address already encoded by a stable internal preload and shortens three
  direct calls, reducing the proved full-search result from 166 to 163 cells.
- A generic semantic-alias pass now proves that a unary expression helper and
  an existing shared helper agree over the complete flow-sensitive finite input
  domain. Opaque call origins survive ordinary IR suffix sharing, while exact
  eight-digit input derivation, canonical `+0`, decimal helper execution,
  stack/return ABI, physical X1 continuation, numeric indirect targets, and the
  immutable helper body, final preload/layout artifact, and all relocated
  address operands are checked independently. Unknown input and post-proof body
  changes fail closed; unrelated synthetic programs cover both the positive and
  rejection paths.
- A one-parameter function whose complete body is a typed `grid_norm` or
  `grid_wrap` assignment can now consume its parameter directly from X and
  tail-forward to the shared primitive. Generic call threading and dead-proc
  elimination then remove the wrapper. Finally, candidate discovery compares
  proof-gated final layouts for the incumbent and every curated candidate
  instead of discarding an option because its temporary pre-layout addresses
  overflow. For this source that selects the general stack-resident option,
  places the shared normalizer at the existing stable R8 target, shortens five
  calls, and reduces the fully verified result from 163 to 154 cells.
- Later generic optimizer passes reduced the fully verified result from 154 to
  151 cells without source-specific recognition.
- A proved predecrement indexed-update ABI now carries each old bank element
  below the helper input on the stack. For linear selector sequences it folds
  the explicit decrement, indexed recall, and swap into `КхП0..3`; this reduces
  the intermediate result from 151 to 149 cells.
- Joint natural-target component layout treats several unrelated stable
  preload constants as simultaneous helper addresses, solves all component
  positions together, and proves the final CFG, return stack, X2, and runtime
  selectors in one artifact. Final candidate search retains both its joint and
  one-anchor Pareto forms so a locally shorter layout cannot regress the global
  result. At this stage the selected program remained 149 cells.
- Generic address-only selector reassignment compares the total benefit of all
  candidate call targets against the one-cell flows already using a stable
  preload. It may restore lower-value indirect flows to their equivalent direct
  commands, rebind the selector, and solve its new position jointly with fixed
  natural targets. The pass rejects written selectors, ordinary data recalls,
  indirect-memory projections, ambiguous targets, and any final artifact that
  fails exact CFG, return-stack, stack, X2, runtime-selector, or size proofs.
  Here it jointly shortens twelve calls while restoring one old flow, reducing
  the verified result from 149 to 144 cells.
- Exact signed decimal preloads whose complete integer mantissa survives the
  MK-61 eight-digit delivery step can now serve as natural-target selectors.
  The final machine decoder and runtime-selector proof remain authoritative;
  exotic, truncated, mutable, or generated values still fail closed. The
  additional candidates did not improve the selected 144-cell layout because
  their five-anchor assignments did not pass the bounded component geometry
  search.
- Compiler-generated constants may expose a proved two-digit fractional address
  suffix without fixing that suffix in the source-level arithmetic. The
  width-four `cell_mask` lowering now preloads the mathematical value `0.226`;
  complete-use provenance certifies that its final two mantissa digits are
  unobservable to the row-mask calculation. Natural-target layout rebuilds
  those digits from the selected helper address, then independently rechecks
  the final numeric decoder, CFG, return stack, data stack, X2 state, selector
  stability, and every ordinary data recall. An untagged user literal with the
  same numeric value blocks the proof. This selected `0.22600088`, shortened
  fourteen flows jointly, and reduced the verified result from 144 to 142
  cells.
- Exact final-layout fingerprints group option sets only when their complete
  machine items, preloads, register contracts, address-space model, and layout
  limits are byte-for-byte equivalent. This reduced 889 finalist option sets
  to 183 distinct proof inputs. The resulting generic final search selected a
  reverse-suffix-free packed-score accumulator layout and reduced the verified
  result from 142 to 141 cells.
- Callee-hole extraction now keeps invariant nested calls inside a shared
  skeleton and canonicalizes divergent source-only call annotations. This
  shares the complete four-line traversal without recognizing the game or its
  data representation and reduces the verified result from 141 to 138 cells;
  the generic natural-target layout then carries bounded helper entries through
  a proved fallthrough split and reduces the result to 137 cells. The remaining
  gap is 32 cells.
- The natural-target layout now admits sign-toggled stable selectors: a
  register written only by proved uninterruptible `П->X r; /-/; X->П r`
  triples stays a valid flow anchor when both signed spellings decode to the
  same target and are fixed points of the machine's selector write-back
  (eight-digit magnitudes such as `±99999999` -> `99`). The pinned write-back
  emulator fact also proves two-target sign selectors are unsound on stable
  registers, closing that line of investigation. The current artifact contains
  no toggle triple on a stable register, so this is enabling infrastructure
  (for example, for carrying a runtime-toggled mark sign inside an existing
  selector constant) rather than an immediate saving; the verified result
  stays at 137 cells.
- The charged-selector direct-flow reuse pass converts a direct `ПП addr` /
  `БП addr` into a one-cell `К`-form when a flow-sensitive value analysis over
  the execution-state graph proves a stable register holds exactly that
  address on every reaching path (from a preload or a runtime charge the
  program already performs for another indirect consumer), the value is a
  write-back fixed point, and the one-cell deletion cannot break any
  non-retargetable encoding. On this artifact it reuses the runtime-charged
  callee-hole selector `Re=51` for the direct `ПП 51` next to the charge-entry
  call, reducing the verified result from 137 to 136 cells. The remaining gap
  is 31 cells.
- The empty-stack loop-return machinery (a one-cell `К НОП` entry pad at
  physical 00 plus a post-layout pass converting proved empty-stack `БП 01`
  into one-cell `В/О`, exploiting the documented `В/О`-as-`БП 01` continuation
  pinned by `emulator_vo_empty_continuation_facts`) is size-neutral on this
  artifact: the pad costs one cell, the two converted loop returns save two,
  and the shifted geometry displaces one other proved rewrite, so padded and
  unpadded full-search winners tie at 136 cells. The pass and the
  `--empty-stack-loop-return` option remain available for shapes with three or
  more main-level loop returns, where the pad amortizes.
- A register-traffic audit of the 136-cell winner found 47 cells of register
  stores/recalls, of which exactly one is provably dead: the raw `y = entered()`
  store, overwritten by the normalized value right after a `grid_norm` call that
  touches no registers. The finalization dead-store pass already proves it (the
  exact-call-stack proof walks the resolved indirect call; regression-tested on
  a synthetic head shape), but the one-cell erasure fails closed in the selector
  rebind: it would shift the callee-hole charge entry off physical 99, which the
  layout deliberately pinned so the `-99999999` occupied-cell sentinel doubles
  as its charge constant, and the sentinel value is part of the visible UI
  contract. Every other register cell is live or protocol-pinned: the 22
  coordinate recalls cross helper calls that destroy X, the eight save/restore
  copies track the best move while both values stay live, and the remaining
  stores anchor the manual-entry protocol or feed indexed line updates. The
  remaining 31-cell gap will not come from register traffic; it requires
  structural changes.
- A dedicated first IR phase can use the bounded exact-return-stack proof
  (`exact_stack_dead_store_elimination`, candidate `exact-stack-dead-store`)
  before any address-sensitive lowering. It accepts only a wholly symbolic CFG:
  numeric operands, orphan address cells, and materialized indirect targets fail
  closed and remain the responsibility of the retargeted finalization
  transaction. The phase removes the raw entered() store and reproduces the
  original listing's input head: С/П resumes directly on the normalize call and
  raw `y` lives only in X. On this artifact the removal is a proved
  pessimization, which is why the option is candidate-searched rather than
  unconditional: the freed cell shifts the helper zone and the natural-target
  solver loses the `cell_mask` one-cell-call coincidence (three direct two-cell
  `ПП` replace three `К ПП d`), landing at 137; combining it with the
  empty-stack loop-return pad recovers the coincidences but loses the
  charged-selector reuse, tying at 136 again. The committed size baseline pins
  that 136-cell equilibrium.
- Optimizer tests must use unrelated synthetic programs and local proof
  obligations. The tic-tac-toe fixture may lock only its size and observable
  UI; it must not select or justify an optimization by recognizing this game
  or its whole-program architecture.
- Generic packed-digit read-modify-write fusion now recognizes a single pure
  `digit_at(value, index)` transformed and written back through
  `digit_set(value, index, ...)`. When both indexes are proved to select the
  leading significant digit of a physical register-backed value, two
  register-synchronized X2 splices replace the repeated decimal-place
  arithmetic. This reduces `nekromant.mkpro` from 222 to 186 cells without
  changing its source; computed values, stack-only values, mismatched indexes,
  extra digit reads, and impure transforms retain the generic fallback.
- Generic `divmod-pair-fusion` shares each division between an integer-quotient
  sum and the immediately following matching remainder stores. Together with
  ordinary straight-line helper sharing, this reduces `nekromant.mkpro` from
  186 to 178 cells; aliases, reordered stores, mismatched divisors, and
  nonnumeric divisors retain the independent lowering.
- Generic interprocedural stack-through lowering keeps a one-parameter
  function argument below an independent pure producer and forwards both to
  the return expression. An exact finite CFG proof requires every caller
  continuation to erase the changed lower stack before it is observed. In
  combination with existing finalization passes this reduces `nekromant.mkpro`
  from 163 to 159 cells without changing the source.
- Generic guarded stack-SSA lowering keeps a two-parameter zero-guarded
  function call in `X:Y`. Its packed-digit composition consumes one binary
  operand directly from `Y` while the leading digit is produced in `X`, with a
  conservative fallback for every unproved expression or continuation. This
  reduces `nekromant.mkpro` from 159 to 155 cells without changing the source.
- Generic natural-target layout now retargets every independent compiler-owned
  address-only selector whose target moves with a selected component, including
  raw-BCD selector spellings. Each companion selector must already have one
  authoritative flow target, no ordinary data use, a stable unwritten register,
  and a literal runtime preload; final CFG, return-stack, data-stack, X2, command
  identity, and selector decoding are then proved again as one transaction. This
  lets the stack-SSA function layout compose with existing indirect branches and
  reduces `nekromant.mkpro` from 155 to 150 cells without changing its source.
- Generic guarded-countdown range analysis now proves a `counter 0..N` positive
  whenever it reaches its sole unit decrement: the field must have a positive
  literal initializer, its non-positive edge must terminate, and no other write
  or opaque raw effect may exist. This enables the existing `F L0`..`F L3`
  decrement-and-zero lowering without changing source semantics and reduces
  `nekromant.mkpro` from 150 to 145 cells. The same proof reduces the unrelated
  top-level `river-battle.mkpro` example from 95 to 90 cells.
- Destructive decrement/test lowering now runs before generic temporary-X
  forwarding, including inside nested control-flow blocks. Branch-consumer
  forwarding also refuses a purely X-resident register-backed value with any
  source-level read outside the proved producer/consumer run. Hardware
  `К П->X r` increments/decrements remain eligible because their side effect has
  already persisted the new register value. This prevents a nested update from
  being lost at its enclosing continuation and lets `lives--; if lives <= 0`
  use `F L2`, reducing `nekromant.mkpro` from 145 to 141 cells. Independent
  compiler and emulator probes exercise the same nested countdown shape without
  recognizing the game.
- Generic `branch-y-payload-forwarding` recognizes a two-way CFG diamond in
  which both arms consume the same single-use value through a commutative scalar
  update. Recalling the zero-tested guard after producing the value places the
  guard in `X` and the payload in `Y`; each arm then consumes `Y` directly, so
  the payload store and both recalls disappear. Def-use checks reject later
  reads, noncommutative consumers, and raw stack interaction. Composed with the
  existing stack function entries and final layout, this reduces
  `nekromant.mkpro` from 141 to 140 cells without changing its source.
- Generic interprocedural caller-X-to-Y forwarding carries a proved-live caller
  value through one-argument stack-entry calls. Ordinary dead-store elimination
  then removes the redundant register materializations, reducing
  `nekromant.mkpro` from 140 to 136 cells without changing its source.
