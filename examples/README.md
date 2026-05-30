# Example Size Report

Generated with `npm run examples:size`.

## Snapshot

- Top-level examples: 19/19 fit in the 105-cell MK-61 window; 19/19 pass the headless load check.
- Referenced top-level examples: 14/14 are no larger than the original MK-61 listing.
- Tightest runnable examples: `fox-hunt-100.mkpro` (105), `wumpus.mkpro` (104), `game-100-pig.mkpro` (103).
- Pending optimizer: 7 programs still exceed the MK-61 window; nearest is `pending-optimizer/cave-highlevel-baseline.mkpro` (128).

## Measurements

`MK-Pro` is the current compiled program size in MK-61 cells. `MK-61 ref` is the span of the referenced original listing under `games/` when the source declares `reference ...`. `Delta` is `MK-Pro - MK-61 ref`. The emulator column is a mechanical headless MK-61 load check; it is not a full game-script equivalence proof.

| Example | MK-Pro | MK-61 ref | Delta | Size status | Emulator |
| --- | ---: | ---: | ---: | --- | --- |
| `99-bottles.mkpro` | 53 | 53 | 0 | ok: <= reference | main+setup load ok |
| `alaram.mkpro` | 81 | 105 | -24 | ok: <= reference | load ok |
| `basic.mkpro` | 8 | - | - | ok: no reference | load ok |
| `cave-sketch.mkpro` | 52 | 105 | -53 | ok: <= reference | main+setup load ok |
| `dangerous-loading.mkpro` | 87 | 103 | -16 | ok: <= reference | load ok |
| `dungeon.mkpro` | 86 | 105 | -19 | ok: <= reference | load ok |
| `e-94-digits.mkpro` | 64 | 64 | 0 | ok: <= reference | load ok |
| `fox-hunt-100.mkpro` | 105 | 105 | 0 | ok: <= reference | main+setup load ok |
| `fox-hunt-mk61.mkpro` | 65 | 92 | -27 | ok: <= reference | main+setup load ok |
| `functions-demo.mkpro` | 29 | - | - | ok: no reference | load ok |
| `game-100-pig.mkpro` | 103 | 103 | 0 | ok: <= reference | main+setup load ok |
| `human.mkpro` | 28 | - | - | ok: no reference | load ok |
| `lunar.mkpro` | 47 | 58 | -11 | ok: <= reference | load ok |
| `minesweeper-9x7.mkpro` | 100 | 104 | -4 | ok: <= reference | main+setup load ok |
| `minesweeper-9x9.mkpro` | 90 | 97 | -7 | ok: <= reference | main+setup load ok |
| `raja-yoga.mkpro` | 88 | 98 | -10 | ok: <= reference | main+setup load ok |
| `sea-battle.mkpro` | 76 | 102 | -26 | ok: <= reference | main+setup load ok |
| `tiny-game.mkpro` | 24 | - | - | ok: no reference | load ok |
| `wumpus.mkpro` | 104 | - | - | ok: no reference | main+setup load ok |
| `pending-optimizer/cave-highlevel-baseline.mkpro` | 128 | 105 | +23 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/cave-treasure.mkpro` | 149 | 105 | +44 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/giants-country.mkpro` | 375 | 105 | +270 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/labyrinth777.mkpro` | 224 | 105 | +119 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/teleport.mkpro` | 249 | 105 | +144 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/tic-tac-toe-4x4.mkpro` | 260 | 105 | +155 | pending optimizer | not loaded: main >105 |
| `pending-optimizer/treasure-hunter-2.mkpro` | 137 | 105 | +32 | pending optimizer | setup load ok; main >105 |
