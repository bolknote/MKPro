# Example Size Report

Generated with `npm run examples:size`.

## Snapshot

- Top-level examples: 19/19 fit in the 105-cell MK-61 window; 19/19 pass the headless load check.
- Referenced top-level examples: 14/14 are no larger than the original MK-61 listing.
- Tightest runnable examples: `fox-hunt-100.mkpro` (105), `game-100-pig.mkpro` (103), `wumpus.mkpro` (103).
- Pending optimizer: 9 programs still exceed the MK-61 window; nearest is `pending-optimizer/cave-highlevel-baseline.mkpro` (120).

## Measurements

`MK-Pro` is the current compiled program size in MK-61 cells. `MK-61 ref` is the span of the referenced original listing under `games/` when the source declares `reference ...`. `Delta` is `MK-Pro - MK-61 ref`. The emulator column is a mechanical headless MK-61 load check; it is not a full game-script equivalence proof.

| Example | MK-Pro | MK-61 ref | Delta | Size status | Emulator |
| --- | ---: | ---: | ---: | --- | --- |
| `99-bottles.mkpro` | 53 | 53 | 0 | ok: <= reference | main+setup load ok |
| `alaram.mkpro` | 79 | 105 | -26 | ok: <= reference | main+setup load ok |
| `basic.mkpro` | 8 | - | - | ok: no reference | load ok |
| `cave-sketch.mkpro` | 52 | 105 | -53 | ok: <= reference | main+setup load ok |
| `dangerous-loading.mkpro` | 87 | 103 | -16 | ok: <= reference | load ok |
| `dungeon.mkpro` | 84 | 105 | -21 | ok: <= reference | load ok |
| `e-94-digits.mkpro` | 64 | 64 | 0 | ok: <= reference | load ok |
| `fox-hunt-100.mkpro` | 105 | 105 | 0 | ok: <= reference | main+setup load ok |
| `fox-hunt-mk61.mkpro` | 65 | 92 | -27 | ok: <= reference | main+setup load ok |
| `functions-demo.mkpro` | 29 | - | - | ok: no reference | load ok |
| `game-100-pig.mkpro` | 103 | 103 | 0 | ok: <= reference | main+setup load ok |
| `human.mkpro` | 25 | - | - | ok: no reference | load ok |
| `lunar.mkpro` | 47 | 58 | -11 | ok: <= reference | load ok |
| `minesweeper-9x7.mkpro` | 100 | 104 | -4 | ok: <= reference | main+setup load ok |
| `minesweeper-9x9.mkpro` | 90 | 97 | -7 | ok: <= reference | main+setup load ok |
| `raja-yoga.mkpro` | 85 | 98 | -13 | ok: <= reference | main+setup load ok |
| `sea-battle.mkpro` | 76 | 102 | -26 | ok: <= reference | main+setup load ok |
| `tiny-game.mkpro` | 24 | - | - | ok: no reference | load ok |
| `wumpus.mkpro` | 103 | - | - | ok: no reference | main+setup load ok |
| `pending-optimizer/cave-highlevel-baseline.mkpro` | 120 | 105 | +15 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/cave-treasure.mkpro` | 139 | 105 | +34 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/giants-country.mkpro` | 162 | 105 | +57 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/jack-pot.mkpro` | 153 | 104 | +49 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/labyrinth777.mkpro` | 221 | 105 | +116 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/rambo-iii.mkpro` | 324 | 105 | +219 | pending optimizer | not loaded: main >105 |
| `pending-optimizer/teleport.mkpro` | 245 | 105 | +140 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/tic-tac-toe-4x4.mkpro` | 260 | 105 | +155 | pending optimizer | not loaded: main >105 |
| `pending-optimizer/treasure-hunter-2.mkpro` | 135 | 105 | +30 | pending optimizer | setup load ok; main >105 |
