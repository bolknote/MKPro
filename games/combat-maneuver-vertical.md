# Combat Maneuvering: Vertical Flight

- Authors: V. Dovbnya and V. Teryaev
- Source: *Aviation and Cosmonautics*, 1987, article «Боевое маневрирование с ПМК», mirrored at [arbinada.ddns.net](http://arbinada.ddns.net/pmk-files/magazines/%D0%90%D0%B2%D0%B8%D0%B0%D1%86%D0%B8%D1%8F%20%D0%B8%20%D0%BA%D0%BE%D1%81%D0%BC%D0%BE%D0%BD%D0%B0%D0%B2%D1%82%D0%B8%D0%BA%D0%B0%201987%20%D0%9F%D0%9C%D0%9A.zip)
- Program: [combat-maneuver-vertical.txt](combat-maneuver-vertical.txt)
- Listing restored from the scanned magazine pages

## Description

This routine computes aircraft motion in the vertical plane. The article uses it
to model a loop, integrating the equations of motion by angle increments and
then plotting the resulting speed, turn radius, elapsed time, and altitude.

The program is written for the MK-54, MK-56, and MK-61 programmable calculators.
The scanned listing is parameterized for the MiG-21 example from the article:
addresses `10` and `11` store wing area `S = 23 m^2`, and addresses `22` to
`25` store induced-drag factor `A = 0.25`. The article notes that using another
aircraft may require these constants to occupy a different number of program
cells, shifting later addresses while keeping the command sequence unchanged.

## Setup

Load the program, switch back to automatic mode, and set the `Р-ГРД-Г` switch to
`Г`. Enter the fixed initial data:

| Register | Value |
| --- | --- |
| `R0` | initial speed `V0`, km/h |
| `R1` | aircraft weight `G`, kg |
| `R6` | zero-lift drag coefficient `Cx0` |
| `Rb` | initial altitude `H0`, meters |
| `Rc` | angle step `dtheta`, degrees |

For each integration step enter:

| Register | Value |
| --- | --- |
| `R2` | flight-path angle `theta_i`, degrees |
| `R3` | normal overload `ny_i` |
| `R7` | engine thrust `P_i` |

Start with `В/О С/П`. The article gives an approximate calculation time of 30
seconds per step.

## Output

After each stop:

| Place | Result |
| --- | --- |
| `X`, `R0` | new speed `Vi`, km/h |
| `R5` | elapsed time `ti`, seconds |
| `R9` | vertical-plane turn radius `ryi`, meters |
| `Rb` | altitude `Hi`, meters |

To continue the maneuver, enter the next values of `theta_i`, `ny_i`, and `P_i`
in `R2`, `R3`, and `R7`, then press `С/П`.
