# Pending Optimizer

These MK-Pro sources lower through ordinary compiler IR, but the generated
program is still too large for MK-61 or larger than the original reference.

Treat each file here as an optimizer or lowering-size bug, not as an unsupported
syntax bucket. Do not replace these with raw listings; the point is to make the
high-level source fit.

Current `--analysis` sizes, measured against the local reference listings:

| File | Current | Reference | Main blocker |
| --- | ---: | ---: | --- |
| `cave-highlevel-baseline.mkpro` | 265 | 105 | command dispatch and cave mask lowerers |
| `cave-treasure.mkpro` | 300 | 105 | command dispatch, guarded direction dispatch, and cave mask lowerers |
| `fox-hunt-100.mkpro` | 185 | 105 | `line_count` still larger than the reference fox table helper |
| `giants-country.mkpro` | 429 | 105 | repeated encounter/challenge dispatch |
| `labyrinth777.mkpro` | 270 | 105 | room inspection and local-jumper dispatch |
| `teleport.mkpro` | 258 | 105 | station masks and vault/guard flow |
| `tic-tac-toe-4x4.mkpro` | 302 | 105 | 4x4 line-count state representation |
| `treasure-hunter-2.mkpro` | 206 | 105 | floor-plan display/state lowerer |

Prototype notes:

- `fox-hunt-100.txt` similarly treats hidden fox positions as generated packed
  data addressed through compact helpers, not as an expanded sum of independent
  `bit_has` formulas.
- `tic-tac-toe-4x4.txt` keeps 4x4 line state in packed line registers
  (`R4..R7`) and updates/scans those lines directly. The current `line_count`
  loop is smaller than the old expansion, but still recomputes line state
  instead of preserving it incrementally.
