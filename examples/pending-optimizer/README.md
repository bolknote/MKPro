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

Current `--analysis` sizes, measured against the local reference listings:

Strict `mk-pro compile` mode is not guaranteed for every pending file yet.
`--analysis` allows non-official address mapping to keep going, so these `Current`
numbers can be lower than what `bin/mk-pro.mjs compile` accepts.

| File | Current | Reference | Main blocker |
| --- | ---: | ---: | --- |
| `cave-highlevel-baseline.mkpro` | 194 | 105 | source-faithful fixed wall/cache setup, prompt/result screen state, resource pressure, movement decoder, and remaining cave flow lowerers |
| `cave-treasure.mkpro` | 165 | 105 | resource pressure, wall breaking, cache miss flow, and remaining dispatch overhead |
| `giants-country.mkpro` | 170 | 105 | packed room-map display/flow and remaining event flow lowerers |
| `labyrinth777.mkpro` | 163 | 105 | room inspection and local-jumper dispatch; break-even indirect-call guard now collapses the repeated room/jumper helper calls to single-cell `К ПП r` |
| `rambo-iii.mkpro` | 105 | 105 | now matches the reference budget without `raw`: setup-only countdown init, source-shaped reinforcement/advance flow, previous-random stack reuse, zero-fallback store deferral, indexed store/domain-guard reuse, and fractional-debit stack reuse cover the remaining MK-61 idioms |
| `teleport.mkpro` | 194 | 105 | packed row display, station masks, and vault/guard flow |
| `tic-tac-toe-4x4.mkpro` | 200 | 105 | 4x4 line-count state representation |

Prototype notes:

- `tic-tac-toe-4x4.txt` keeps 4x4 line state in packed line registers
  (`R4..R7`) and updates/scans those lines directly. The current `line_count`
  loop is smaller than the old expansion, but still recomputes line state
  instead of preserving it incrementally.

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
