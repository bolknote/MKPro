# Fox Hunt 3 (Hramov)

- Author: Sergey Hramov
- Source: variant by Сергей Храмов.
- Program: [okhota-na-lis-3-hramov.txt](okhota-na-lis-3-hramov.txt)

## Description

An MK-52/MK-61 improvement over the `Fox Hunt 2` listing. It keeps the 10 by 10 field and eight foxes, but uses a recurrence with `p = 101` and `q = 74` so foxes do not share a cell. A fox at `0,0` can also be detected and captured.

Captured foxes are removed from later direction-finder counts, which makes the puzzle harder.

## Setup

Set the `Р-Г` switch to `Р`. Store `101` in `Re`, enter any fractional random seed, then press `В/О`, `С/П`. Enter moves as fractional coordinates `A,B`, where both coordinates are from `0` to `9`.

## Import Note

The `.pmk` save stores the seven-command page for addresses `07` to `13` at the end. The listing here follows the order printed in the source PDF.
