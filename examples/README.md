# Example Size Report

Generated with `npm run examples:size`.

## Snapshot

- Top-level examples: 17/17 fit in the 105-cell MK-61 window; 17/17 pass the headless load check.
- Referenced top-level examples: 12/13 are no larger than the original MK-61 listing.
- Tightest runnable examples: `fox-hunt-100.mkpro` (105), `wumpus.mkpro` (104), `game-100-pig.mkpro` (103).
- Pending optimizer: 7 programs still exceed the MK-61 window; nearest is `pending-optimizer/cave-highlevel-baseline.mkpro` (129).

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
| `fox-hunt-100.mkpro` | 105 | 105 | 0 | ok: <= reference | main+setup load ok |
| `fox-hunt-mk61.mkpro` | 72 | 92 | -20 | ok: <= reference | main+setup load ok |
| `game-100-pig.mkpro` | 103 | 103 | 0 | ok: <= reference | main+setup load ok |
| `human.mkpro` | 30 | - | - | ok: no reference | load ok |
| `lunar.mkpro` | 47 | 58 | -11 | ok: <= reference | load ok |
| `minesweeper-9x7.mkpro` | 87 | 104 | -17 | ok: <= reference | main+setup load ok |
| `minesweeper-9x9.mkpro` | 87 | 97 | -10 | ok: <= reference | main+setup load ok |
| `raja-yoga.mkpro` | 88 | 98 | -10 | ok: <= reference | main+setup load ok |
| `sea-battle.mkpro` | 76 | 102 | -26 | ok: <= reference | main+setup load ok |
| `tiny-game.mkpro` | 26 | - | - | ok: no reference | load ok |
| `wumpus.mkpro` | 104 | - | - | ok: no reference | main+setup load ok |
| `pending-optimizer/cave-highlevel-baseline.mkpro` | 129 | 105 | +24 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/cave-treasure.mkpro` | 147 | 105 | +42 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/giants-country.mkpro` | 194 | 105 | +89 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/labyrinth777.mkpro` | 230 | 105 | +125 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/teleport.mkpro` | 233 | 105 | +128 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/tic-tac-toe-4x4.mkpro` | 232 | 105 | +127 | pending optimizer | not loaded: main >105 |
| `pending-optimizer/treasure-hunter-2.mkpro` | 138 | 105 | +33 | pending optimizer | setup load ok; main >105 |
