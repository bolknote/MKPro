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

Current `--analysis` sizes, measured against the local reference listings:

| File | Current | Reference | Main blocker |
| --- | ---: | ---: | --- |
| `cave-highlevel-baseline.mkpro` | 157 | 105 | resource pressure, original command dispatch, and remaining cave flow lowerers |
| `cave-treasure.mkpro` | 165 | 105 | resource pressure, wall breaking, cache miss flow, and remaining dispatch overhead |
| `giants-country.mkpro` | 170 | 105 | packed room-map display/flow and remaining event flow lowerers |
| `labyrinth777.mkpro` | 206 | 105 | room inspection and local-jumper dispatch |
| `rambo-iii.mkpro` | 203 | 105 | grouped front/robots storage now works; event dispatch and battle-flow branches are the remaining large blocks |
| `teleport.mkpro` | 239 | 105 | packed row display, station masks, and vault/guard flow |
| `tic-tac-toe-4x4.mkpro` | 219 | 105 | 4x4 line-count state representation |
| `treasure-hunter-2.mkpro` | 105 | 105 | closed by packed cursor position, angle-switch movement, and indexed floor-row flow |

Prototype notes:

- `tic-tac-toe-4x4.txt` keeps 4x4 line state in packed line registers
  (`R4..R7`) and updates/scans those lines directly. The current `line_count`
  loop is smaller than the old expansion, but still recomputes line state
  instead of preserving it incrementally.
