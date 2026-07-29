# Fox Hunt 4 (Hramov)

- Author: Sergey Hramov
- Source: four-field variant by Сергей Храмов.
- Program: [okhota-na-lis-4-hramov.txt](okhota-na-lis-4-hramov.txt)

## Description

The fourth Hramov `Fox Hunt` variant. It keeps the 10 by 10, eight-fox game shape, but replaces the clustered recurrence placement from `Fox Hunt 3` with pseudo-random placement plus collision checking so all fox coordinates are distinct.

Captured foxes are removed from later direction-finder counts. The source also adds a score interpretation based on the number of attempts after all foxes are found.

## Setup

Set the `Р-Г` switch to `Р`. Store `49` in `R7`, enter any fractional random seed, then press `В/О`, `С/П`. Enter moves as fractional coordinates `A,B`, where both coordinates are from `0` to `9`.

## Import Note

The `.pmk` save stores the seven-command page for addresses `07` to `13` at the end. The listing here follows the order printed in the source PDF.
