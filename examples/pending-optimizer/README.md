# Pending Optimizer

These MK-Pro sources lower through ordinary compiler IR, but the generated
program is usually still too large for MK-61 or larger than the original
reference. Files that already fit may stay here briefly as regression fixtures
until they are moved back to the top-level examples.

Treat each file here as an optimizer or lowering-size bug, not as an unsupported
syntax bucket. Do not replace these with raw listings; the point is to make the
high-level source fit.

`wumpus.mkpro` is no longer pending: it moved back to the top-level examples at
105 cells and passes the main+setup load check.

`jack-pot.mkpro` moved back to the top-level examples at 99 cells and passes the
main+setup load check.

`treasure-hunter-2.mkpro` moved back to the top-level examples at 100 cells and
passes the main+setup load check.

`rambo-iii.mkpro` moved back to the top-level examples at 105 cells and passes
the main+setup load check.

`teleport.mkpro` moved back to the top-level examples at 97 cells and passes
the main+setup load check.

`labyrinth777.mkpro` moved back to the top-level examples at 105 cells and
passes the main+setup load check.

`cave-treasure.mkpro` moved back to the top-level examples at 105 cells and
passes the main+setup load check after strict-mode rescue candidate probing
learned to rank over-window intermediates.

`cave-highlevel-baseline.mkpro` moved back to the top-level examples at 105
cells and passes the main+setup load check.

`giants-country.mkpro` moved back to the top-level examples at 105 cells and
passes the main+setup load check.

Current `--analysis` sizes, measured against the local reference listings:

Strict `mk-pro compile` mode is not guaranteed for every pending file yet.
`--analysis` allows non-official address mapping to keep going, so these `Current`
numbers can be lower than what `bin/mk-pro.mjs compile` accepts.

| File | Current | Reference | Main blocker |
| --- | ---: | ---: | --- |
| `tic-tac-toe-4x4.mkpro` | 134 | 105 | source port uses original-style R1/R2 counted search loops, R4..R7 line banks in the source register order (`R7=x`, `R6=y`, `R5=x+y`, `R4=x-y`), source-shaped packed occupied masks, source-style `bit_or` test-and-set occupied retry, derives the player mark sign from the test-and-set success value, passes the player/calculator mark sign into the shared line checker where it becomes `best_score`, uses the source-style `max` tie update for calculator move selection, keeps selected Y in real state while proving the line index and candidate score as stack-only, shared indirect line update/check flow, best-score-derived line deltas, the original fractional win-mask report, a source-shaped physical selector for line banks, a demoted `-1` preload freeing an indirect selector for the shared line-update helper, fractional constant target packing after demotion, a post-layout empty-stack tail-call rewrite for the terminal line-check call, a local accumulated `packed_score` helper that keeps candidate `score` on the stack and shares the middle-entry diagonal tail, a zero-accumulator entry when the candidate branch already proved X=0, a branchless one-based diagonal normalizer that wraps negative differences like the source, and terminal fractional report restore through X2; remaining blocker is optimizer/lowering size for packed line helpers |

Prototype notes:

- Membership/set reuse now consumes a freshly returned mask directly from X and,
  when the failed branch sets the same packed collection, avoids the scratch
  register entirely: the mask stays in Y, the collection is restored from X2
  with `.`, and `К∨` performs the set.
- The scratch fallback still keeps the mask stack-resident for a simple
  collection load when the following set updates a different collection or a
  multi-set run, so the test itself does not have to recall the scratch again
  before `К∧`.
- Repeated literal stores now have a counted-loop-safe bridge: `best_score = 4`
  can share the same entered `4` with the following `x = 4` initializer while
  the `while x >= 1 { ...; x-- }` loop still lowers through `F L3`.
- `cave-highlevel-baseline.mkpro` now benefits from fractional indirect
  addressing: `walls[int(blocked)]` can use the `blocked` coordinate register
  directly as the indirect selector because MK-61 indirect memory addressing
  ignores the fractional coordinate tail. This is the source-listing trick in a
  general indexed-bank form, not a cave-only special case. The lowering also
  schedules fractional uses of the same selector before the destructive indirect
  access, matching the original listing's reliance on selector side effects
  without losing the coordinate tail too early.
- `cave-highlevel-baseline.mkpro` now keeps food, dynamite, and treasure in one
  floor-indexed resource bank. The cache reward selects `resources[floor]` once,
  computes the original `10, 1, 1` floor bonus as `pow10(int(1 / floor))`, passes
  the updated stake through the stack-stop robber helper, and reuses the dynamic
  indexed store value as the next loop prompt instead of recalling the same bank
  element again.
- Its movement fallback also spells the documented post-`4`/`6` vertical keys as
  `sign(6 - command)`, letting the residual dispatch avoid the extra unit adjust
  while preserving the visible keypad UI.
- Its movement test now follows the source listing's passability-mask shape:
  it checks the fractional part of `bit_and(passages[int(blocked)], blocked)`
  and only commits the blocked coordinate when that fractional membership is
  nonzero. The generated setup also coalesces duplicate same-register preloads,
  so the high-level baseline now fits the 105-cell window with setup loading.
- `cave-treasure.mkpro` now keeps food, dynamite, and treasure in one
  floor-indexed resource bank. Cache rewards use `resources[pos.floor]` and the
  source-level `(4 - pos.floor)^2` bonus sequence (`9`, `4`, `1`), relying on
  the same contextual `.floor` lowering for indexed assignment targets as for
  ordinary expressions.
- `cave-treasure.mkpro` lets the successful movement branch fall through in
  `go(dir)` (`unless blocked in walls`) while keeping the failed-wall UI exactly
  as the original `show(0)` stop. This saves two cells before any X2 movement
  decoder work.
- Shared terminal tails now cover source-listing style "tail of one procedure as
  the tail of another" when two identical straight-line suffixes already end in
  unconditional flow. This is intentionally generic: it can jump into a matching
  `...; БП` / `...; К БП r` / `...; В/О` suffix, but it refuses programs with
  absolute numeric flow targets.
- Duplicate failure-tail merge also covers adjacent `С/П; terminal-flow` tails
  where the displayed value is already in X. This keeps source-level `show(0)`
  and "show the shortage" branches distinct while sharing the identical pause
  and continuation cells.
- Shared straight-line helper extraction covers the non-terminal sibling of that
  trick: repeated straight-line opcode bodies become one `ПП`/`В/О` helper when
  the call cost is lower than keeping every copy inline. It deliberately does
  not mix with `return-suffix-gadget` bodies until the layout model can prove
  both contracts at once.
- Branch-target X reuse is the flow-sensitive sibling of last-X reuse: if a
  `П->X r; F x?0 label` condition is the only way to enter `label`, and `label`
  immediately recalls the same `r`, the recall is removed because the condition
  path preserves X.
- Flow X reuse generalizes that idea to direct jumps and both sides of a
  conditional: the IR pass intersects all CFG predecessors and drops `П->X r`
  when every known incoming path already carries `r` in X. It refuses absolute
  numeric and indirect flow targets, so address-shifting remains conservative.

Stack-resident temp scheduler (2026-06):

- Measurement harness: `node scripts/spill-report.mjs --pending` reports spill
  counts plus static candidate counts (`fuse`/`xflow`/`s1`/`idx` columns).
- Speculative variant `stackResidentTemps` fuses single-use assign temps and
  keeps them on X/Y/Z/T instead of `X->П`/`П->X` spills when a combining
  consumer reads each temp exactly once. The scheduler now spans
  stack-preserving control-flow gaps (empty `if`/`while`/`loop`/`dispatch`
  bodies that do not read the protected temps or clobber X). `selectBest()`
  adopts it only when the whole program shrinks.
- Current pending sources still expose **zero** multi-temp fusion sites at the
  AST level (straight-line sections hold at most one live assign temp), so
  these programs do not shrink yet from this pass alone. The machinery is in
  place for future source shapes and for smaller examples that do contain
  `t0=e0; … stack-preserving stmt …; t1=e1; out=t0 op t1` chains.
