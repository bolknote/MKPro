
> mk-pro@0.1.0 examples:size
> node ./scripts/example-size-report.mjs

# Example Size Report

Generated with `npm run examples:size`.

## Snapshot

- Top-level examples: 30/30 fit in the 105-cell MK-61 window; 30/30 pass the headless load check.
- Referenced top-level examples: 23/23 are no larger than the original MK-61 listing.
- Tightest runnable examples: `giants-country.mkpro` (105), `labyrinth777.mkpro` (105), `cave-highlevel-baseline.mkpro` (104).
- Pending optimizer: 1 programs still exceed the MK-61 window; nearest is `pending-optimizer/tic-tac-toe-4x4.mkpro` (155).

## Measurements

`MK-Pro` is the current compiled program size in MK-61 cells. `MK-61 ref` is the span of the referenced original listing under `games/` when the source declares `reference ...`. `Delta` is `MK-Pro - MK-61 ref`. The emulator column is a mechanical headless MK-61 load check; it is not a full game-script equivalence proof.

| Example | MK-Pro | MK-61 ref | Delta | Size status | Emulator |
| --- | ---: | ---: | ---: | --- | --- |
| `99-bottles.mkpro` | 52 | 53 | -1 | ok: <= reference | main+setup load ok |
| `alaram.mkpro` | 75 | 999999 | -999924 | ok: <= reference | main+setup load ok |
| `basic.mkpro` | 8 | - | - | ok: no reference | load ok |
| `cave-highlevel-baseline.mkpro` | 104 | 105 | -1 | ok: <= reference | main+setup load ok |
| `cave-sketch.mkpro` | 47 | 105 | -58 | ok: <= reference | main+setup load ok |
| `cave-treasure.mkpro` | 104 | 105 | -1 | ok: <= reference | main+setup load ok |
| `clock.mkpro` | 40 | 63 | -23 | ok: <= reference | main+setup load ok |
| `dangerous-loading.mkpro` | 86 | 103 | -17 | ok: <= reference | load ok |
| `dungeon.mkpro` | 84 | 999999 | -999915 | ok: <= reference | load ok |
| `e-94-digits.mkpro` | 64 | 64 | 0 | ok: <= reference | load ok |
| `fox-hunt-100.mkpro` | 104 | 105 | -1 | ok: <= reference | main+setup load ok |
| `fox-hunt-mk61.mkpro` | 65 | 92 | -27 | ok: <= reference | main+setup load ok |
| `functions-demo.mkpro` | 25 | - | - | ok: no reference | load ok |
| `game-100-pig.mkpro` | 99 | 103 | -4 | ok: <= reference | main+setup load ok |
| `giants-country.mkpro` | 105 | 999999 | -999894 | ok: <= reference | main+setup load ok |
| `human.mkpro` | 26 | - | - | ok: no reference | load ok |
| `jack-pot.mkpro` | 99 | 999999 | -999900 | ok: <= reference | main+setup load ok |
| `labyrinth777.mkpro` | 105 | 105 | 0 | ok: <= reference | main+setup load ok |
| `lunar.mkpro` | 47 | 999999 | -999952 | ok: <= reference | load ok |
| `minesweeper-9x7.mkpro` | 85 | 999999 | -999914 | ok: <= reference | main+setup load ok |
| `minesweeper-9x9.mkpro` | 85 | 97 | -12 | ok: <= reference | main+setup load ok |
| `raja-yoga.mkpro` | 85 | 98 | -13 | ok: <= reference | main+setup load ok |
| `rambo-iii.mkpro` | 104 | 999999 | -999895 | ok: <= reference | load ok |
| `river-battle.mkpro` | 95 | - | - | ok: no reference | main+setup load ok |
| `sea-battle.mkpro` | 74 | 102 | -28 | ok: <= reference | main+setup load ok |
| `teleport.mkpro` | 97 | 105 | -8 | ok: <= reference | main+setup load ok |
| `tic-tac-toe.mkpro` | 100 | - | - | ok: no reference | load ok |
| `tiny-game.mkpro` | 26 | - | - | ok: no reference | load ok |
| `treasure-hunter-2.mkpro` | 99 | 105 | -6 | ok: <= reference | main+setup load ok |
| `wumpus.mkpro` | 103 | - | - | ok: no reference | main+setup load ok |
| `pending-optimizer/tic-tac-toe-4x4.mkpro` | 155 | 105 | +50 | pending optimizer | not loaded: main >105 |
