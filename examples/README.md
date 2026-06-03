# Example Size Report

Generated with `npm run examples:size`.

## Snapshot

- Top-level examples: 26/26 fit in the 105-cell MK-61 window; 26/26 pass the headless load check.
- Referenced top-level examples: 21/21 are no larger than the original MK-61 listing.
- Tightest runnable examples: `labyrinth777.mkpro` (105), `rambo-iii.mkpro` (105), `giants-country.mkpro` (104).
- Pending optimizer: 3 programs still exceed the MK-61 window; nearest is `pending-optimizer/cave-treasure.mkpro` (118).

## Measurements

`MK-Pro` is the current compiled program size in MK-61 cells. `MK-61 ref` is the span of the referenced original listing under `games/` when the source declares `reference ...`. `Delta` is `MK-Pro - MK-61 ref`. The emulator column is a mechanical headless MK-61 load check; it is not a full game-script equivalence proof.

| Example | MK-Pro | MK-61 ref | Delta | Size status | Emulator |
| --- | ---: | ---: | ---: | --- | --- |
| `99-bottles.mkpro` | 53 | 53 | 0 | ok: <= reference | main+setup load ok |
| `alaram.mkpro` | 75 | 105 | -30 | ok: <= reference | main+setup load ok |
| `basic.mkpro` | 8 | - | - | ok: no reference | load ok |
| `cave-sketch.mkpro` | 47 | 105 | -58 | ok: <= reference | main+setup load ok |
| `clock.mkpro` | 43 | 63 | -20 | ok: <= reference | main+setup load ok |
| `dangerous-loading.mkpro` | 84 | 103 | -19 | ok: <= reference | load ok |
| `dungeon.mkpro` | 82 | 105 | -23 | ok: <= reference | load ok |
| `e-94-digits.mkpro` | 64 | 64 | 0 | ok: <= reference | load ok |
| `fox-hunt-100.mkpro` | 103 | 105 | -2 | ok: <= reference | main+setup load ok |
| `fox-hunt-mk61.mkpro` | 65 | 92 | -27 | ok: <= reference | main+setup load ok |
| `functions-demo.mkpro` | 28 | - | - | ok: no reference | load ok |
| `game-100-pig.mkpro` | 103 | 103 | 0 | ok: <= reference | main+setup load ok |
| `giants-country.mkpro` | 104 | 105 | -1 | ok: <= reference | main+setup load ok |
| `human.mkpro` | 23 | - | - | ok: no reference | load ok |
| `jack-pot.mkpro` | 99 | 104 | -5 | ok: <= reference | main+setup load ok |
| `labyrinth777.mkpro` | 105 | 105 | 0 | ok: <= reference | main+setup load ok |
| `lunar.mkpro` | 47 | 58 | -11 | ok: <= reference | load ok |
| `minesweeper-9x7.mkpro` | 83 | 104 | -21 | ok: <= reference | main+setup load ok |
| `minesweeper-9x9.mkpro` | 81 | 97 | -16 | ok: <= reference | main+setup load ok |
| `raja-yoga.mkpro` | 87 | 98 | -11 | ok: <= reference | main+setup load ok |
| `rambo-iii.mkpro` | 105 | 105 | 0 | ok: <= reference | load ok |
| `sea-battle.mkpro` | 74 | 102 | -28 | ok: <= reference | main+setup load ok |
| `teleport.mkpro` | 97 | 105 | -8 | ok: <= reference | main+setup load ok |
| `tiny-game.mkpro` | 23 | - | - | ok: no reference | load ok |
| `treasure-hunter-2.mkpro` | 100 | 105 | -5 | ok: <= reference | main+setup load ok |
| `wumpus.mkpro` | 100 | - | - | ok: no reference | main+setup load ok |
| `pending-optimizer/cave-highlevel-baseline.mkpro` | 134 | 105 | +29 | pending optimizer | not loaded: main >105 |
| `pending-optimizer/cave-treasure.mkpro` | 118 | 105 | +13 | pending optimizer | setup load ok; main >105 |
| `pending-optimizer/tic-tac-toe-4x4.mkpro` | 247 | 105 | +142 | pending optimizer | not loaded: main >105 |
