# Pending Optimizer

These MK-Pro sources lower through ordinary compiler IR, but the generated
program is still too large for MK-61 or larger than the original reference.

Treat each file here as an optimizer or lowering-size bug, not as an unsupported
syntax bucket. Do not replace these with raw listings; the point is to make the
high-level source fit.

Current `--analysis` sizes, measured against the local reference listings:

| File | Current | Reference | Main blocker |
| --- | ---: | ---: | --- |
| `cave-highlevel-baseline.mkpro` | 206 | 105 | command dispatch and remaining cave flow lowerers |
| `cave-treasure.mkpro` | 210 | 105 | command dispatch, guarded direction dispatch, and robber/cache flow |
| `giants-country.mkpro` | 213 | 105 | remaining encounter dispatch and plan bitset lowerers |
| `labyrinth777.mkpro` | 242 | 105 | room inspection and local-jumper dispatch |
| `teleport.mkpro` | 242 | 105 | station masks and vault/guard flow |
| `tic-tac-toe-4x4.mkpro` | 246 | 105 | 4x4 line-count state representation |
| `treasure-hunter-2.mkpro` | 185 | 105 | floor-plan display/state lowerer |
| `wumpus-full.mkpro` | 112 | 105 | repeated room loads in inspection and remaining hazard flow |

Prototype notes:

- `tic-tac-toe-4x4.txt` keeps 4x4 line state in packed line registers
  (`R4..R7`) and updates/scans those lines directly. The current `line_count`
  loop is smaller than the old expansion, but still recomputes line state
  instead of preserving it incrementally.
