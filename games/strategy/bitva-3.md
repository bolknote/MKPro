# Bitva-3

- Author: Evgeny Temezhnikov
- Source: *Tekhnika - Molodezhi*, No. 10, 1988, «Клуб электронных игр», article «Урок истории», mirrored at [arbinada.ddns.net](http://arbinada.ddns.net/pmk-files/magazines/%D0%A2%D0%B5%D1%85%D0%BD%D0%B8%D0%BA%D0%B0%20%D0%BC%D0%BE%D0%BB%D0%BE%D0%B4%D0%B5%D0%B6%D0%B8%201985-1988%20%D0%9F%D0%9C%D0%9A.zip)
- Program: [bitva-3.txt](bitva-3.txt)
- Listing restored from `TM.1988-10.KEI.djvu`

## Description

«Битва-3» continues the medieval battle simulation series from KEI. It extends
the earlier «Битва» variants with several enemy formations, player reserves,
an ambush regiment, and configurable coefficients for historical scenarios.

The main listing is for MK-52 and MK-61. The article also gives a replacement
loss-calculation block for B3-34 and MK-54; that block is not included here as a
separate game because it is an adaptation fragment for the same scenario.

## Setup

Before play, set the coefficient cells in the program:

| Address | Value |
| --- | --- |
| `02` | `A`, effectiveness of the forward regiment |
| `46` | `N`, regiment behind which the ambush is placed |
| `60` | `B`, ambush surprise coefficient, `0..7` |
| `99` | `I`, battle intensity coefficient, `2..9` |

The source gives these register roles:

| Register | Role |
| --- | --- |
| `Ra` | player reserve and command post |
| `Rb` | player regiment 1, central |
| `Rc` | player regiment 2, left |
| `Rd` | player regiment 3, right |
| `Re` | player regiment 4 |
| `R0` | ambush regiment |
| `R1` | calculator army |
| `R2` | number of calculator regiments |
| `R3` | player's forward regiment, later current regiment number |
| `R4` | losses and reinforcements |
| `R5` | calculator regiment strength |
| `R6` | time |
| `R7` | `92` for random play, `99` for deterministic analysis |
| `R8` | indirect regiment address and defeat symbol |
| `R9` | number of active regiments on one side |

After loading the program and registers, start with `В/О С/П`.

## Play

At each stop the display shows the regiment number. Enter a positive number to
reinforce it from reserve, or a negative number to withdraw troops to reserve,
then press `С/П`. If the command exceeds the available reserve or the regiment's
strength, the display repeats the regiment number and waits for a valid order.

A negative display beginning with seven nines means that the corresponding
regiment has been broken; `Y` also contains a negative value. The reserve can
still save the battle if it is large enough to cover the breach. `ЕГГОГ` means
the player has won; captured calculator troops are in `R1`, and the number of
surrendered regiments is in `R2`.
