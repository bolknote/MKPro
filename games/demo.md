# Treasure Cave Demo

- Author: Serge Anvarov
- Original game: "Treasure Cave", *Tekhnika Molodezhi* No. 7, 1987
- Source: [Serge Anvarov](https://sergeanvarov.github.io/russian/mk61/uf/demo.html)
- Program: [demo.txt](demo.txt)

## Description

This demonstration program is a modified version of the classic MK-61 game
"Treasure Cave". It uses the game as a compact showcase for bit masks,
hexadecimal-looking calculator values, indirect addressing, X2 side effects,
and other undocumented MK-61 behavior.

The game is a three-dimensional maze with 3 floors, each 7 columns by 4 rows.
Each cell is either open or blocked by a wall. The goal is to explore the maze,
reach the exit, and collect as much treasure as possible. The player chooses
the starting position. The exit is to the left of point `3.8` on the third
floor; the program reports a successful exit as `11`.

Coordinates are displayed as a packed decimal-like value. For example,
`1.0000008` means:

- `1`: the floor number.
- `7`: the horizontal position, encoded as the exponent of the fractional part
  by counting zeroes plus the final digit.
- `8`: the vertical position, encoded as the digit in the fractional part.

## Resources

The player has three limited resources:

- Food, stored in `Re`. One unit is spent on every command.
- Dynamite or grenades, stored in `Rd`. Dynamite is spent when breaking walls.
- Treasure, stored in `Rc`. The objective is to collect as much as possible.

Searching may reveal a cache. The reward depends on the floor:

- Floor 1: food, `+9` units.
- Floor 2: dynamite, `+4` units.
- Floor 3: treasure, `+1`.

When a cache is found, it disappears from that same cell on every floor. A
robber then appears. You may fight by pressing `В↑`, or decline by pressing
`0`; then press `С/П`. Fighting can either increase or reduce the resource
total.

## Controls

Enter a command, then press `С/П`.

| Command | Meaning |
| --- | --- |
| `2` | Move down |
| `4` | Move left |
| `6` | Move right |
| `8` | Move up |
| `5` | Move one floor up |
| `/-/ 5` | Move one floor down |
| `0` | Break the wall at the last failed movement target |
| `10` | Search for a cache |

Every command costs food. Breaking a wall also costs two dynamite units. If a
move succeeds, the calculator displays the new coordinate. If it fails, it
displays `0`. A successful wall break immediately moves the player into the new
cell; an invalid break attempt also displays `0`.

## Differences From The Original

- Wall breaking uses two dynamite units, balancing the larger `+4` dynamite
  reward on the second floor. The initial dynamite supply is four rather than
  two.
- If there is not enough dynamite, the program displays a negative number
  showing the shortage instead of `ЕГГ0Г`, so the game can continue.
- If food runs out, the program loops instead of displaying `ЕГГ0Г`, making the
  failed game state obvious.
- Wall breaking is command `0` rather than `F pi`, and the break also performs
  the move into the opened cell.
- Searching is command `10` rather than `0`. On success the display includes
  both the cache type and its floor position, for example `E.00002`.
- Walls are removed rather than toggled with XOR.
- The program blocks the rounding-based "teleport" from `N.0000008` to
  `N.0000001` when moving right.
- Outer walls, including the basement and roof, cannot be broken.
- The return stack is not damaged by error exits.
- Resource counters are initialized by the program instead of being manually
  re-entered before every game.
- The MK-61 generates both the maze and the cache layout itself. The starting
  position is guaranteed not to be inside a wall.

## Registers

- `R0`: cache map.
- `R1`..`R3`: maps of floors 1..3.
- `R4`..`R7`: movement constants: `2`, `10`, approximately `0.1`, and `0.5`.
- `R8`, `R9`: auxiliary constants for indirect jumps and rounding correction.
- `Ra`: current player position.
- `Rb`: last blocked target cell; also a shared work register.
- `Rc`, `Rd`, `Re`: treasure, dynamite, and food.

## Constants

Initial constants that do not change between games:

- `R4 = 2`
- `R5 = 10`
- `R6 = D.|-02`, displayed as `Г. -02`
- `R7 = 0.5`
- `R8 = -52`
- `R9 = 4.F3|-08`, displayed as `4. 3 -08`

Input sequence for the constants:

```text
5 2 /-/ X->П 8
4 4 7 3 В↑ 8 0 8 К ∨ К {x} ВП 7 /-/ X->П 9
1 0 X->П 5
2 X->П 4
F 1/x X->П 7
2 2 К ИНВ К {x} ВП 1 К [x] ВП 2 /-/ X->П 6
```

Set the `Р-ГРД-Г` switch to `Р`.

To start a game, put the initial food amount in `Y` and the initial position in
`X`. Execution must begin at address `44`. The source suggests the compact
startup sequence:

```text
БП 44 4 4 П->X 9 F 10^x С/П
```

## Program Notes

The program fits into the official 105-step space by leaning hard on MK-61
quirks:

- Indirect jump targets are stored in registers to save two-step jumps.
- Several constants do double duty as arithmetic values and indirect-address
  controls.
- Execution deliberately uses cyclic and side branches of the address space.
- The tail of one routine is reused as the entry or continuation of another.
- The X2 register is used as hidden temporary storage several times.
- `ВП` is used both to restore X2 and to merge X2 with hexadecimal-looking
  digits.
- `К ЗН` acts as a compact multiplication by two in one branch.
- `К max` is used in an undocumented way to let zero from `Y` pass through into
  `X`.
- Hexadecimal-style arithmetic is used to transform `C`, `D`, and `E` into the
  convenient resource increments `1`, `2`, and `3`.
