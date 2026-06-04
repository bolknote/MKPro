# Combat Maneuvering: Unsteady Turn

- Authors: V. Dovbnya and V. Teryaev
- Source: *Aviation and Cosmonautics*, 1987, article «Боевое маневрирование с ПМК», mirrored at [arbinada.ddns.net](http://arbinada.ddns.net/pmk-files/magazines/%D0%90%D0%B2%D0%B8%D0%B0%D1%86%D0%B8%D1%8F%20%D0%B8%20%D0%BA%D0%BE%D1%81%D0%BC%D0%BE%D0%BD%D0%B0%D0%B2%D1%82%D0%B8%D0%BA%D0%B0%201987%20%D0%9F%D0%9C%D0%9A.zip)
- Program: [combat-maneuver-turn.txt](combat-maneuver-turn.txt)
- Listing restored from the scanned magazine pages

## Description

This flight-modeling routine computes the parameters of an unsteady horizontal
turn for an aircraft. It is intended for the MK-54, MK-56, and MK-61
programmable calculators.

The article presents the routine as part of pre-flight modeling: by integrating
short maneuver steps, a pilot can build the turn trajectory from the calculated
speed, heading increment, and turn radius.

## Setup

Load the program, switch back to automatic mode, then enter the initial data:

| Register | Value |
| --- | --- |
| `R0` | initial speed `V0`, km/h |
| `Rc` | integration step `dt`, seconds |

For each integration step enter:

| Register | Value |
| --- | --- |
| `Ra` | longitudinal overload `nxi` |
| `Rb` | normal overload `nyi` |

Start with `В/О С/П`.

## Output

After each stop:

| Place | Result |
| --- | --- |
| `X`, `R0` | new speed `Vi`, km/h |
| `R4` | heading increment `dphi`, degrees |
| `R5` | turn radius `rzi`, meters |

To continue the turn, enter the next `nxi` and `nyi` values in `Ra` and `Rb`,
then press `С/П`.
