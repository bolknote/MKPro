# Pending Optimizer

These MK-Pro sources lower through ordinary compiler IR, but the generated
program is still too large for MK-61 or larger than the original reference.

Treat each file here as an optimizer or lowering-size bug, not as an unsupported
syntax bucket. Do not replace these with raw listings; the point is to make the
high-level source fit.

Current `--analysis` sizes, measured against the local reference listings:

| File | Current | Reference | Main blocker |
| --- | ---: | ---: | --- |
| `cave-highlevel-baseline.mkpro` | 140 | 105 | command dispatch and remaining cave flow lowerers |
| `cave-treasure.mkpro` | 150 | 105 | guarded direction dispatch, wall breaking, and cache flow |
| `giants-country.mkpro` | 180 | 105 | remaining encounter dispatch and plan bitset lowerers |
| `labyrinth777.mkpro` | 241 | 105 | room inspection and local-jumper dispatch |
| `teleport.mkpro` | 232 | 105 | station masks and vault/guard flow |
| `tic-tac-toe-4x4.mkpro` | 241 | 105 | 4x4 line-count state representation |
| `treasure-hunter-2.mkpro` | 149 | 105 | floor-plan display/state lowerer |
| `wumpus.mkpro` | 107 | 105 | terminal loss now uses calculator error stop; still over the MK-61 window |

Prototype notes:

- `tic-tac-toe-4x4.txt` keeps 4x4 line state in packed line registers
  (`R4..R7`) and updates/scans those lines directly. The current `line_count`
  loop is smaller than the old expansion, but still recomputes line state
  instead of preserving it incrementally.
