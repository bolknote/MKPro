# Designing MK-61 Games

The MK-61 display shows numbers, not graphics. Every "board", "sprite", or map
is a convention encoded in mantissa digits, exponents, and stop points. This
note summarizes Danilov's game chapter and connects it to the Lord_BSS material
already in this repository.

## Why Games Belong in PMK Documentation

Games are not a distraction from serious programming. They train:

- stack scheduling under pressure,
- state machines with almost no RAM,
- pseudo-random behavior,
- user I/O through `С/П` stops,
- reading numeric output as symbols.

They are also the stress test for emulator fidelity: classic listings assume
specific stack, random, and display side effects.

## Two Game Architectures

| Type | Calculator role | Player experience |
| --- | --- | --- |
| **Opponent** | Program replies to each move | Chess-like: you vs machine |
| **Situation generator** | Program builds a scenario; you solve it | Puzzle or race: machine sets constraints |

**Opponent games** with a perfect strategy become boring quickly. If the program
always wins at "tic-tac-toe" or "nim", human players stop.

**Generator games** stay fresh because randomness and player choices interact.
Examples: lunar landing (fuel burn each turn), minesweeper boards, compass
quadrant hints in battleship.

Lord_BSS listings span both types; see [05-reading-lordbss-games.md](./05-reading-lordbss-games.md).

## Design Constraints

Before choosing a game, list hard limits:

| Resource | MK-61 typical use |
| --- | --- |
| 105 program steps | core loop + one subroutine |
| 15 registers | board state, scores, RNG seed |
| Display | 8 mantissa digits + exponent |
| Input | digits and `С/П` pauses |
| Switches | `Р-Г-Г` trig mode; middle position is **grads/gons** on many models |

If the rules do not fit, simplify the world (1-D race instead of 2-D maze).

## Random Numbers

### On MK-61 Today

Use `К СЧ` for many games. Notes from [03-command-reference.md](./03-command-reference.md):

- does not generate `1`,
- can rarely generate `0`,
- sequence can cycle,
- `К max` with zero in `Y` resets the sequence.

Test whether that statistical bias matters for your game.

### Historical B3-34 Approach (Pre-`К СЧ`)

Danilov describes building pseudo-random numbers because early PMKs had no RNG
opcode. Typical ingredients:

- iterate a short formula on a seed register,
- exploit undocumented fractional behavior of indirect recalls when `R0 < 1`; see
  [12-danilov-secrets-mk61-delta.md](./12-danilov-secrets-mk61-delta.md),
- keep the generator inside the step budget.

Pseudo-random sequences repeat, which is an advantage for debugging: the same
seed reproduces the same minefield.

## Worked Pattern: Ice Race (Danilov)

Danilov's chapter-12 sketch is a **situation generator**:

**World.**

- Several parallel straight tracks on frozen lake.
- Crosswind pushes vehicles sideways.
- Player chooses speed or aggression each turn.
- Strong forward motion increases risk of a "crash" event that zeroes progress.
- First to finish wins.

**Implementation mapping.**

| Game concept | PMK representation |
| --- | --- |
| Lateral position | register or packed digit in display |
| Progress along track | register incremented each turn |
| Wind / hazard | `К СЧ` or seed formula compared to threshold |
| Player input | digit at `С/П` stop |
| Crash | branch to zero progress register |
| Finish | compare progress to track length, stop with code |

Even without copying Danilov's exact listing, the pattern is reusable:

```text
init positions and RNG
loop:
  display state (С/П)
  accept player choice
  update risk from choice + random draw
  maybe crash
  advance progress
  test win/lose
  jump to loop
```

## Display Encoding

When the "picture" is numeric:

- assign digit positions to entities (`777` = success, `666` = crash in lunar
  landing),
- pack multiple small counters into one mantissa when safe,
- document the alphabet in a companion `.md` file beside the listing.

Lord_BSS uses mixed Cyrillic/Latin glyphs on paper; emulators show raw numbers.
Always decode from the game's README, not from opcode tables alone.

## Subroutines in Games

Games love tiny subroutines:

- `ПП` to draw one row,
- `ПП` to apply wind,
- shared tail for "clamp value to 0..9".

Remember the 5-level return stack. Deep nesting between opponent AI and UI
quickly consumes it.

Indirect calls (`К ПП R`) help when a table of handlers lives in registers; see
[04-programming-cookbook.md](./04-programming-cookbook.md).

## Intentional `ЕГГОГ` as Game Feedback

On B3-34, programs sometimes triggered `ЕГГОГ` with undocumented error opcodes
as a compact "illegal move" signal. On MK-61 many codes became real functions.
Prefer explicit branches or a displayed error code unless you have verified the
old trick on hardware or emulator. Details in
[12-danilov-secrets-mk61-delta.md](./12-danilov-secrets-mk61-delta.md).

## Testing a Game Port

1. Initialize registers exactly as the original page specifies.
2. Expand compressed notation using [05-reading-lordbss-games.md](./05-reading-lordbss-games.md).
3. Run one full turn with emulator registers visible.
4. Verify each `С/П` boundary: input expected vs given.
5. Compare random-dependent runs only after fixing the seed or accepting
   distribution differences.

Smoke tests for packaged games live under `tests/emulator/`.

## Where to Look in This Repo

| Path | Content |
| --- | --- |
| `games/lordbss/` | Cleaned Lord_BSS listings + per-game `.md` |
| `games/tehnika-molodyzhi/` | OCR-derived Tekhnika Molodyzhi material |
| `examples/tiny-game.mkpro` | Minimal MK-Pro game sketch |
| `examples/alaram.mkpro` | Lord_BSS cockpit interceptor port that fits the original |
| `examples/cave-sketch.mkpro` | compact cave sketch/baseline |
| `examples/dangerous-loading.mkpro` | ferry/loading port that fits after dispatch default merging |
| `examples/dungeon.mkpro` | Lord_BSS Dungeon port that fits the original |
| `examples/game-100-pig.mkpro` | Anvarov dice game port that fits the original |
| `examples/minesweeper-9x9.mkpro` | Anvarov Minesweeper port that fits the original |
| `examples/raja-yoga.mkpro` | Anvarov one-dimensional mask game port that fits the original |
| `examples/rambo-iii.mkpro` | Lord_BSS three-front defense port that fits the original |
| `examples/jack-pot.mkpro` | Lord_BSS Jack Pot slot-machine port that fits the original |
| `examples/pending-optimizer/cave-treasure.mkpro` | hard high-level compactness reference needing size work |

## Sources

- Igor Danilov, *Secrets of the Programmable Microcalculator*, chapter 12
- [05-reading-lordbss-games.md](./05-reading-lordbss-games.md)
- [02-undocumented-tricks.md](./02-undocumented-tricks.md)
