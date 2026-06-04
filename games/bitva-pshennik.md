# Bitva: Pshennik Variant

- Author: Yuri Pshennik
- Source: *Tekhnika - Molodezhi*, No. 2, 1988, «Клуб электронных игр», article «И бысть сеча ту велика...», mirrored at [arbinada.ddns.net](http://arbinada.ddns.net/pmk-files/magazines/%D0%A2%D0%B5%D1%85%D0%BD%D0%B8%D0%BA%D0%B0%20%D0%BC%D0%BE%D0%BB%D0%BE%D0%B4%D0%B5%D0%B6%D0%B8%201985-1988%20%D0%9F%D0%9C%D0%9A.zip)
- Program: [bitva-pshennik.txt](bitva-pshennik.txt)
- Listing restored from `TM.1988-02.KEI.djvu`

## Description

«Битва» is a medieval battle simulation. In this first published variant the
calculator plays a heavy-cavalry wedge, while the player commands the defending
army: a forward blocking regiment, two flank regiments, and a reserve. The game
models hourly losses, reinforcement from the reserve, flank attacks, and the
changing morale effect of unequal army sizes.

The article says that the version was submitted by Yuri Pshennik from
Molodechno and lightly cleaned up by the KEI editors.

## Setup

The source article uses these starting values:

| Register | Role |
| --- | --- |
| `R0` | loss coefficient |
| `R1` | total calculator army strength, for example `9000 П1` |
| `R2`, `R3` | calculator reserve and equivalent strength; clear `R3` before the game |
| `R4` | battle time; clear before the game |
| `R5` | player's forward regiment, then total engaged player strength |
| `R6` | total player flank-regiment strength |
| `R7` | player reserve |
| `R8` | equivalent player strength; clear before the game |
| `R9` | player losses per hour |
| `Ra` | calculator losses per hour |
| `Rb`, `Rc` | operational registers |
| `Rd` | random four- or five-digit seed |

One published setup is:

`9000 П1 Сх П3 Сх П4 5000 П5 3000 П6 1000 П7 Сх П8 600 П9 500 ПА`

Enter the program, set the `Р-Г` switch to `Г`, then start with `В/О С/П`.

## Play

At each stop the display identifies the active player regiment and `Y` contains
its current strength. Enter reinforcements and press `С/П`, or use `Сх С/П` if
the regiment should continue fighting with its current strength. A negative
entry starts a flank or reserve strike.

If a regiment loses all of its strength, the game is lost and the calculator
shows `ЕГГОГ`; captured troops can be inspected in registers `Ra`, `Rb`, `Rc`,
and `Rd`. If the calculator army is broken, the display shows `3ГГОГ`.
