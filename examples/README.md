
> mk-pro@0.1.0 examples:size
> node ./scripts/example-size-report.mjs

# Example Size Report

Generated with `npm run examples:size`.

## Snapshot

- Top-level examples: 30/30 fit in the 105-cell MK-61 window; 30/30 pass the headless load check.
- Referenced top-level examples: 23/23 are no larger than the original MK-61 listing.
- Tightest runnable examples: `labyrinth777.mkpro` (105), `cave-highlevel-baseline.mkpro` (104), `cave-treasure.mkpro` (104).
- Pending optimizer: 1 program still exceeds the MK-61 window; nearest is `pending-optimizer/tic-tac-toe-4x4.mkpro` (144).

## Measurements

`MK-Pro` is the current compiled program size in MK-61 cells. `MK-61 ref` is the span of the referenced original listing under `games/` when the source declares `reference ...`. `Delta` is `MK-Pro - MK-61 ref`. The emulator column is a mechanical headless MK-61 load check; it is not a full game-script equivalence proof.

| Example | MK-Pro | MK-61 ref | Delta | Size status | Emulator |
| --- | ---: | ---: | ---: | --- | --- |
| `99-bottles.mkpro` | 52 | 53 | -1 | ok: <= reference | main+setup load ok |
| `alaram.mkpro` | 66 | 999999 | -999933 | ok: <= reference | main+setup load ok |
| `basic.mkpro` | 8 | - | - | ok: no reference | load ok |
| `cave-highlevel-baseline.mkpro` | 104 | 105 | -1 | ok: <= reference | main+setup load ok |
| `cave-sketch.mkpro` | 38 | 105 | -67 | ok: <= reference | main+setup load ok |
| `cave-treasure.mkpro` | 104 | 105 | -1 | ok: <= reference | main+setup load ok |
| `clock.mkpro` | 33 | 63 | -30 | ok: <= reference | main+setup load ok |
| `dangerous-loading.mkpro` | 75 | 103 | -28 | ok: <= reference | load ok |
| `dungeon.mkpro` | 75 | 999999 | -999924 | ok: <= reference | load ok |
| `e-94-digits.mkpro` | 64 | 64 | 0 | ok: <= reference | load ok |
| `fox-hunt-100.mkpro` | 103 | 105 | -2 | ok: <= reference | main+setup load ok |
| `fox-hunt-mk61.mkpro` | 60 | 92 | -32 | ok: <= reference | main+setup load ok |
| `functions-demo.mkpro` | 13 | - | - | ok: no reference | load ok |
| `game-100-pig.mkpro` | 97 | 103 | -6 | ok: <= reference | main+setup load ok |
| `giants-country.mkpro` | 102 | 999999 | -999897 | ok: <= reference | main+setup load ok |
| `human.mkpro` | 27 | - | - | ok: no reference | load ok |
| `jack-pot.mkpro` | 96 | 999999 | -999903 | ok: <= reference | main+setup load ok |
| `labyrinth777.mkpro` | 105 | 105 | 0 | ok: <= reference | main+setup load ok |
| `lunar.mkpro` | 44 | 999999 | -999955 | ok: <= reference | load ok |
| `minesweeper-9x7.mkpro` | 79 | 999999 | -999920 | ok: <= reference | main+setup load ok |
| `minesweeper-9x9.mkpro` | 79 | 97 | -18 | ok: <= reference | main+setup load ok |
| `raja-yoga.mkpro` | 77 | 98 | -21 | ok: <= reference | main+setup load ok |
| `rambo-iii.mkpro` | 104 | 999999 | -999895 | ok: <= reference | load ok |
| `river-battle.mkpro` | 95 | - | - | ok: no reference | main+setup load ok |
| `sea-battle.mkpro` | 65 | 102 | -37 | ok: <= reference | main+setup load ok |
| `teleport.mkpro` | 96 | 105 | -9 | ok: <= reference | main+setup load ok |
| `tic-tac-toe.mkpro` | 99 | - | - | ok: no reference | load ok |
| `tiny-game.mkpro` | 27 | - | - | ok: no reference | load ok |
| `treasure-hunter-2.mkpro` | 98 | 105 | -7 | ok: <= reference | main+setup load ok |
| `wumpus.mkpro` | 103 | - | - | ok: no reference | main+setup load ok |
| `pending-optimizer/tic-tac-toe-4x4.mkpro` | 144 | 105 | +39 | pending optimizer | not loaded: main >105 |
