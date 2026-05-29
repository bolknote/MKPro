# Example Size Report

Generated with `npm run examples:size`.

## Snapshot

- Top-level examples: 17/17 fit in the 105-cell MK-61 window; 17/17 pass the headless load check.
- Referenced top-level examples: 13/13 are no larger than the original MK-61 listing.
- Tightest runnable examples: `wumpus.mkpro` (104), `game-100-pig.mkpro` (103), `fox-hunt-100.mkpro` (97).
- Pending optimizer: 7 programs still exceed the MK-61 window; nearest is `pending-optimizer/cave-highlevel-baseline.mkpro` (134).

## Measurements

`MK-Pro` is the current compiled program size in MK-61 cells. `MK-61 ref` is the span of the referenced original listing under `games/` when the source declares `reference ...`. `Delta` is `MK-Pro - MK-61 ref`. The emulator column is a mechanical headless MK-61 load check; it is not a full game-script equivalence proof.

| Example | MK-Pro | MK-61 ref | Delta | Size status | Emulator |
| --- | ---: | ---: | ---: | --- | --- |
| `99-bottles.mkpro` | 53 | 53 | 0 | ok: <= reference | load ok |
| `alaram.mkpro` | 82 | 105 | -23 | ok: <= reference | load ok |
| `basic.mkpro` | 8 | - | - | ok: no reference | load ok |
| `cave-sketch.mkpro` | 54 | 105 | -51 | ok: <= reference | load ok |
| `dangerous-loading.mkpro` | 89 | 103 | -14 | ok: <= reference | load ok |
| `dungeon.mkpro` | 88 | 105 | -17 | ok: <= reference | load ok |
| `fox-hunt-100.mkpro` | 97 | 105 | -8 | ok: <= reference | main+setup load ok |
| `fox-hunt-mk61.mkpro` | 88 | 92 | -4 | ok: <= reference | main+setup load ok |
| `game-100-pig.mkpro` | 103 | 103 | 0 | ok: <= reference | main+setup load ok |
| `human.mkpro` | 30 | - | - | ok: no reference | load ok |
| `lunar.mkpro` | 47 | 58 | -11 | ok: <= reference | load ok |
| `minesweeper-9x7.mkpro` | 77 | 104 | -27 | ok: <= reference | main+setup load ok |
| `minesweeper-9x9.mkpro` | 77 | 97 | -20 | ok: <= reference | main+setup load ok |
| `raja-yoga.mkpro` | 88 | 98 | -10 | ok: <= reference | main+setup load ok |
| `sea-battle.mkpro` | 67 | 102 | -35 | ok: <= reference | main+setup load ok |
| `tiny-game.mkpro` | 26 | - | - | ok: no reference | load ok |
| `wumpus.mkpro` | 104 | - | - | ok: no reference | main+setup load ok |
| `pending-optimizer/cave-highlevel-baseline.mkpro` | 134 | 105 | +29 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/cave-treasure.mkpro` | 150 | 105 | +45 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/giants-country.mkpro` | 180 | 105 | +75 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/labyrinth777.mkpro` | 228 | 105 | +123 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/teleport.mkpro` | 221 | 105 | +116 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/tic-tac-toe-4x4.mkpro` | 219 | 105 | +114 | pending optimizer | not loaded: main >105 |
| `pending-optimizer/treasure-hunter-2.mkpro` | 141 | 105 | +36 | pending optimizer | setup load ok; main >105 |
