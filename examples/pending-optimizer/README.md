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

`jack-pot.mkpro` moved back to the top-level examples at 101 cells and passes the
main+setup load check.

`treasure-hunter-2.mkpro` moved back to the top-level examples at 105 cells and
passes the main+setup load check.

`rambo-iii.mkpro` moved back to the top-level examples at 105 cells and passes
the main+setup load check.

`teleport.mkpro` moved back to the top-level examples at 105 cells and passes
the main+setup load check.

`labyrinth777.mkpro` moved back to the top-level examples at 105 cells and
passes the main+setup load check.

Current `--analysis` sizes, measured against the local reference listings:

Strict `mk-pro compile` mode is not guaranteed for every pending file yet.
`--analysis` allows non-official address mapping to keep going, so these `Current`
numbers can be lower than what `bin/mk-pro.mjs compile` accepts.

| File | Current | Reference | Main blocker |
| --- | ---: | ---: | --- |
| `cave-highlevel-baseline.mkpro` | 150 | 105 | source-faithful fixed wall/cache setup, resource pressure, movement decoder, and remaining cave flow lowerers |
| `cave-treasure.mkpro` | 144 | 105 | source-shaped command decoder is in place; remaining blockers are resource pressure, wall breaking, cache reward flow, and dispatch overhead |
| `giants-country.mkpro` | 105 | 105 | fits after restoring the source-style direct R5 position counter; pending only for exact cave-picture/warning display audit |
| `tic-tac-toe-4x4.mkpro` | 269 | 105 | source-shaped line update/score pass is in place; remaining packed 4x4 scan lowering |

Prototype notes:

- `tic-tac-toe-4x4.txt` keeps 4x4 line state in packed line registers
  (`R4..R7`) and updates/scans those lines directly. The MK-Pro source now uses
  the same occupied mask, packed line weights, `X ПП Y С/П` input shape, and
  squared-deviation scan; line updates and scoring follow the source's explicit
  `x`, `y`, `x+y`, `x-y` pass. The remaining gap is compiler lowering: it still
  emits ordinary calls/loops for the full-board scan and win scan instead of the
  source listing's stack-resident indirect-address subroutine shape.

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
