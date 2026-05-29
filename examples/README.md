# Example Size Report

Generated with `npm run examples:size`.

`MK-Pro` is the current compiled program size in MK-61 cells. `MK-61 ref` is the span of the referenced original listing under `games/` when the source declares `reference ...`. `Delta` is `MK-Pro - MK-61 ref`. The emulator column is a mechanical headless MK-61 load check; it is not a full game-script equivalence proof.

| Example | MK-Pro | MK-61 ref | Delta | Size status | Emulator |
| --- | ---: | ---: | ---: | --- | --- |
| `99-bottles.mkpro` | 53 | 53 | 0 | ok: <= reference | load ok |
| `alaram.mkpro` | 99 | 105 | -6 | ok: <= reference | load ok |
| `basic.mkpro` | 8 | - | - | ok: no reference | load ok |
| `cave-sketch.mkpro` | 61 | 105 | -44 | ok: <= reference | load ok |
| `dangerous-loading.mkpro` | 89 | 103 | -14 | ok: <= reference | load ok |
| `dungeon.mkpro` | 89 | 105 | -16 | ok: <= reference | load ok |
| `fox-hunt-100.mkpro` | 97 | 105 | -8 | ok: <= reference | main+setup load ok |
| `fox-hunt-mk61.mkpro` | 88 | 92 | -4 | ok: <= reference | main+setup load ok |
| `game-100-pig.mkpro` | 100 | 103 | -3 | ok: <= reference | load ok |
| `human.mkpro` | 30 | - | - | ok: no reference | load ok |
| `lunar.mkpro` | 47 | 58 | -11 | ok: <= reference | load ok |
| `minesweeper-9x7.mkpro` | 79 | 104 | -25 | ok: <= reference | main+setup load ok |
| `minesweeper-9x9.mkpro` | 79 | 97 | -18 | ok: <= reference | main+setup load ok |
| `raja-yoga.mkpro` | 90 | 98 | -8 | ok: <= reference | main+setup load ok |
| `sea-battle.mkpro` | 63 | 102 | -39 | ok: <= reference | main+setup load ok |
| `tiny-game.mkpro` | 26 | - | - | ok: no reference | load ok |
| `pending-optimizer/cave-highlevel-baseline.mkpro` | 143 | 105 | +38 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/cave-treasure.mkpro` | 150 | 105 | +45 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/giants-country.mkpro` | 195 | 105 | +90 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/labyrinth777.mkpro` | 244 | 105 | +139 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/teleport.mkpro` | 238 | 105 | +133 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/tic-tac-toe-4x4.mkpro` | 242 | 105 | +137 | pending optimizer | not loaded: main >105 |
| `pending-optimizer/treasure-hunter-2.mkpro` | 153 | 105 | +48 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/wumpus.mkpro` | 111 | - | - | pending optimizer | setup load ok; main >105 |
