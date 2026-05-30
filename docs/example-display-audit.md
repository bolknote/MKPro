# Example Display Audit

This table compares display intent in referenced `examples/*.mkpro` programs with
the original MK-61 listing stops. The original column cites the listing-level
shape, not the prose descriptions.

| MK-Pro example | Original listing display | MK-Pro display now | Status |
| --- | --- | --- | --- |
| `examples/99-bottles.mkpro` | `games/bolknote/99-bottles.txt`: `С/П@33` after the text renderer at `34..52`, producing `BEEr NN`. | `show("BEEr ", bottles:02)` | Matches the original text/count screen. |
| `examples/alaram.mkpro` | `games/lordbss/pmk210.txt`: `С/П@04`, `С/П@09`, `С/П@15` and terminal `В/О` paths show packed cockpit/message patterns. | `show("8СГ -78")`; terminal `halt("ЕГГОГ")`, `halt("Г16ЕL 91")`, `halt("8СГ-Е-78")`. | Full cockpit video-byte composition is still represented by semantic packed fields. |
| `examples/cave-sketch.mkpro` | `games/kei/treasure-cave.txt`: `С/П@88` shows the packed cave coordinate; failure paths show `0`. | `show(player)` | Resource counters are no longer shown on the main screen. |
| `examples/dangerous-loading.mkpro` | `games/anvarov/dangerous-loading.txt`: `С/П@56` follows `Пх1; FВx; F↻; ВП; ...; /-/`, one signed fractional/video water-lane value. | `show(cargo_left, boat, threat, boats_left)` | C/L/- lane symbols still need a real video-byte lowering. |
| `examples/dungeon.mkpro` | `games/lordbss/pmk164.txt`: `В/О@92` returns the `PV` position/height value; `В/О@A4` returns the dungeon plan. | `show(pos, height)`; `show(plan)` | Score is no longer shown; it remains register state like in the original. |
| `examples/fox-hunt-100.mkpro` | `games/anvarov/fox-hunt-100.txt`: hit path stops with a negative remaining count; scan path returns the bearing/count. | `show(bearing)` with hits assigning a negative remaining count. | Matches the numeric output shape. |
| `examples/fox-hunt-mk61.mkpro` | `games/monatkodenis/fox-hunt-mk61.txt`: setup stops on `7`; normal play shows `--CC-- N`; the hit path shows the `-20...` warning before resuming to the bearing screen. | `show("-20")`; `show("--", cell:02, "--", bearing)` | Normal report now matches the original video shape; hit remains the numeric warning, not the full `-20ГГ0,-` video literal. |
| `examples/game-100-pig.mkpro` | `games/anvarov/game-100-pig.txt`: `С/П@36` after decimal/X2 construction shows `К.-СС-ООО-ББ`. | `show(die, ".-", turn_score:02, "-", preview_total:03, "-", roll_count:02)` | Matches the original field/punctuation format. |
| `examples/jack-pot.mkpro` | `games/lordbss/pmk151.txt`: menu paths display the player's money, the three drawn digits, and terminal game over text. | `show(money)`; `show(reels)`; `halt("С-Е O-Г")` | Rewrites the published payout rules directly in MK-Pro; command `1` spins and command `2` redraws the last reel value. |
| `examples/lunar.mkpro` | `games/lordbss/pmk38.txt`: `Пх1; С/П`, `Пх3; С/П`, `Пх2; С/П`; terminal `666`/`777`. | `show(height)`; `show(speed)`; `show(fuel)`; terminal stops only. | Restored sequential stops instead of one combined screen. |
| `examples/minesweeper-9x7.mkpro` | `games/lordbss/pmk39.txt`: `С/П@46` shows the adjacent-mine count in `RВ`. | `show(clue)` | Matches the numeric clue screen. |
| `examples/minesweeper-9x9.mkpro` | `games/anvarov/minesweeper-9x9.txt`: `С/П@34` shows clue or the mine error path. | `show(clue)` | Main clue screen matches; terminal error representation is still abstract. |
| `examples/sea-battle.mkpro` | `games/anvarov/sea-battle.txt`: `В/О@30/49/75/88/A1` show either calculator shot or negative enemy-ship remainder. | `show(calculator_shot)`; `show(enemy_report)` | Removed the non-original three-field diagnostic screen. |
| `examples/pending-optimizer/cave-highlevel-baseline.mkpro` | `games/kei/treasure-cave.txt`: same visible contract as the cave coordinate screen, with `0` on failed moves. | `show(player)` | Resource counters are no longer part of the visible screen. |
| `examples/pending-optimizer/cave-treasure.mkpro` | `games/anvarov/demo.txt`: `С/П@98` shows the packed coordinate; failure paths show `0`. | `show(pos)` | Resource counters are no longer part of the visible screen. |
| `examples/pending-optimizer/giants-country.mkpro` | `games/lordbss/pmk198.txt`: `С/П@27` shows the cave picture; `С/П@66` shows the memory code. | `show(tile, strength, score)`; `show(warning_pattern)`; `show(memory_code)` | Cave picture video-byte layout is not reconstructed yet. |
| `examples/pending-optimizer/labyrinth777.mkpro` | `games/anvarov/labyrinth777.txt`: `С/П@22` shows row/floor picture; `С/П@63` is a secondary stop path. | `show(pos, row_view, energy)` | Row picture/floor packing is not reconstructed yet. |
| `examples/pending-optimizer/teleport.mkpro` | `games/anvarov/teleport.txt`: `С/П@70` and `С/П@A4` stop on packed station-state values. | `show(pos, danger, charges, loot)` | Exact packed station screen still needs source-level reconstruction. |
| `examples/pending-optimizer/tic-tac-toe-4x4.mkpro` | `games/anvarov/tic-tac-toe-4x4.txt`: `С/П@03` after `Пх2; Пх3` displays the active board/move value. | `show(cell, turn_count)` | Current port still shows a simplified state pair. |
| `examples/pending-optimizer/treasure-hunter-2.mkpro` | `games/anvarov/treasure-hunter-2.txt`: `С/П@16`/`С/П@97` stop on floor/plan display values. | `show(pos, floor_view, treasure)`; exit `halt(treasure)` | Full floor-plan screen is not reconstructed yet. |
| `examples/raja-yoga.mkpro` | `games/anvarov/raja-yoga.txt`: `С/П@37` after `Пх4; FВx; ...; ВП; 7` shows stage plus seven-cell life path. | `show(stage, adept, goal, old_age)` | E/C/- life-path video-byte layout is not reconstructed yet. |

Not covered here: examples without a `reference ...` line, such as `basic`,
`human`, `tiny-game`, and the pending `wumpus` port.
