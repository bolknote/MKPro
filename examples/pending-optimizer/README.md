# Pending Optimizer

These MK-Pro sources lower through ordinary compiler IR, but the generated
program is still too large for MK-61 or larger than the original reference.

Treat each file here as an optimizer or lowering-size bug, not as an unsupported
syntax bucket. Do not replace these with raw listings; the point is to make the
high-level source fit.

`wumpus.mkpro` is no longer pending: it moved back to the top-level examples at
104 cells and passes the main+setup load check.

Current `--analysis` sizes, measured against the local reference listings:

| File | Current | Reference | Main blocker |
| --- | ---: | ---: | --- |
| `cave-highlevel-baseline.mkpro` | 129 | 105 | command dispatch and remaining cave flow lowerers |
| `cave-treasure.mkpro` | 147 | 105 | guarded direction dispatch, wall breaking, and cache flow |
| `giants-country.mkpro` | 194 | 105 | remaining encounter dispatch and plan bitset lowerers |
| `labyrinth777.mkpro` | 230 | 105 | room inspection and local-jumper dispatch |
| `teleport.mkpro` | 233 | 105 | station masks and vault/guard flow |
| `tic-tac-toe-4x4.mkpro` | 232 | 105 | 4x4 line-count state representation |
| `treasure-hunter-2.mkpro` | 138 | 105 | floor-plan display/state lowerer |

Prototype notes:

- `tic-tac-toe-4x4.txt` keeps 4x4 line state in packed line registers
  (`R4..R7`) and updates/scans those lines directly. The current `line_count`
  loop is smaller than the old expansion, but still recomputes line state
  instead of preserving it incrementally.
