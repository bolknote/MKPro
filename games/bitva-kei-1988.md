# Bitva: KEI Variant

- Author: Vyacheslav Alekseev
- Source: *Tekhnika - Molodezhi*, No. 2, 1988, «Клуб электронных игр», article «И бысть сеча ту велика...», mirrored at [arbinada.ddns.net](http://arbinada.ddns.net/pmk-files/magazines/%D0%A2%D0%B5%D1%85%D0%BD%D0%B8%D0%BA%D0%B0%20%D0%BC%D0%BE%D0%BB%D0%BE%D0%B4%D0%B5%D0%B6%D0%B8%201985-1988%20%D0%9F%D0%9C%D0%9A.zip)
- Program: [bitva-kei-1988.txt](bitva-kei-1988.txt)
- Listing restored from `TM.1988-02.KEI.djvu`

## Description

This is the KEI column's own variant of «Битва», published next to Yuri
Pshennik's version. It uses a similar medieval-battle model but rearranges the
algorithm, adds a stronger reserve raid mechanism, and needs the article's
additional explanation for command decisions.

The calculator army attacks as a wedge. The player manages the defending army,
deciding when each regiment receives reinforcements, fights on, or triggers a
reserve strike. The game ends when one army is reduced to its commander alone.

## Setup

The article gives these register roles:

| Register | Role |
| --- | --- |
| `R0`, `R2`, `R8` | operational registers |
| `R1` | calculator army strength, for example `10000 П1` |
| `R3` | losses for both sides, computed by the calculator |
| `R4`, `R5` | unused |
| `R6` | battle time; clear before the game |
| `R7` | subroutine entry address, usually `92 П7` |
| `R9` | `3ГГОГ`, the victory signal over the calculator army |
| `Ra` | player reserve |
| `Rb` | player's central regiment |
| `Rc` | player's left regiment |
| `Rd` | player's right regiment |

One setup mentioned in the source is:

`10000 П1 Сх П6 92 П7 1000 ПА 3700 ПВ 1800 ПС ПД`

Enter the program, set the `Р-Г` switch to `Г`, then start with `В/О С/П`.

## Play

At each stop the display shows the active regiment number, and `Y` contains its
strength. Enter a positive number and press `С/П` to reinforce the regiment,
or use `Сх С/П` to leave it fighting with current strength. To send reserve
troops on an independent rear raid, enter `3ГГОГ` with `ИП9 С/П`; the later the
raid, the stronger the panic effect on the calculator army, but the opponent can
counter it on the next hour.

If one of the main regiments is destroyed, the player loses and the calculator
shows `ЕГГОГ`; captured troops can be inspected in registers `Ra`, `Rb`, `Rc`,
and `Rd`. If the calculator wedge is broken, the display shows `3ГГОГ`.
