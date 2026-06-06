# The Neverending Story

- Authors: Vladimir Talalaev, Vladimir Shilo
- Source: *Tekhnika - Molodezhi*, No. 1, 1988, "Electronic Games Club", article "The Neverending Story", pp. 59-61; the issue is described on [FantLab](https://fantlab.ru/edition387762)
- Program: [beskonechnaya-istoriya.txt](beskonechnaya-istoriya.txt)
- Listing restored from `mk-game-1.jpg`, `mk-game-2.jpg`, and `mk-game-3.jpg` with macOS Vision OCR and a manual table check.

## Description

A dynamic adventure game based on the film *The NeverEnding Story*. The player
starts at the Princess's palace in the center of Fantasia and must reach the
Southern Oracle before the advancing Nothing consumes the land.

The game map is coordinate-based. Along the route there are areas taken by the
Nothing, Gmork's territory, Falkor's zone, where Falkor can carry the player to
the Southern Oracle's latitude, and the Sphinx line, which fires across the
approaches to the Oracle.

## Setup

After entering the program, initialize the registers:

The setup below uses Latin aliases: `STO` stores `X` into a register, `EE` is
exponent entry, `RTN` is return, and `STOP` is start/stop.

```text
31 STO7
99 STO8
10 STO9 STO1
1 STO3
100 STO0 STOA STOC
7 /-/ STO6
40 /-/ STOB
39 /-/ STOE
Cx STO4 STO5 STO2 / EE STOD
```

Then clear the stack: press `0` and the stack-lift key seven times. Start with
`RTN STOP`.

## Controls

Movement is controlled with the angle switch:

- `G`: turn 90 degrees clockwise and take a step in the new direction.
- `R`: turn 90 degrees counterclockwise and take a step in the new direction.
- `GRD`: take a step in the previous direction.

The current coordinates are shown by flashing values: longitude `X` first, then
latitude `Y`. A positive `X` means east and a negative `X` means west; a positive
`Y` means north and a negative `Y` means south.

## Events

If the Nothing closes in and consumes the Princess's palace, the display shows
Darkness and the game is lost.

If the player enters another area taken by the Nothing, the display shows
`EGGOG`. In this case, choose a new angle-switch position and press `RTN STOP`.
If the player cannot escape within five moves, the player has fallen into the
Nothing's trap.

When the player reaches the Southern Oracle, the program stops and registers
`X` and `Y` contain zeroes. This is a win.

## Registers

- `R7`, `R8`: jump addresses.
- `R9`: constant.
- `RA`: movement-control value.
- `R4`, `R5`: current `X` and `Y` coordinates.
- `R2`, `R3`: movement direction.
- `R0`, `R1`: movement of Darkness.
- `RD`: Darkness image.
- `R6`: longitude of the Falkor-patrolled zone boundary.
- `RB`: Southern Oracle latitude.
- `RE`: Sphinx latitude.

## Note

The magazine listing is marked as an MK-52 variant and occupies addresses
`00`-`A5`. The article's third page also gives a separate B3-34 variant with the
Sphinx; it is not extracted here as a separate MK-61 program.
