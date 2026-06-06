# Training Flight (Modernized)

- Author: Sergey Hramov
- Based on: Sergey Chuprin's `Training Flight`
- Source: local archive `uchebnyj_polet.zip`
- Program: [uchebnyy-polyot-modernized.txt](uchebnyy-polyot-modernized.txt)

## Description

A modernized real-time version of `Training Flight`. The goal is still to take off, follow the route over and under the terrain markers, approach the landing zone, and land without crashing or overshooting the runway.

The modernization replaces the original integer-part subroutine with `К[x]`, uses `КЗН` for sign handling, improves speed, and adds a selectable stopwatch or fuel-tracking mode. Step `02` is `+` in the archived `.pmk`; replacing it with `-` selects the fuel variant described by the source PDF.

## Setup

Set the `Р-ГРД-Г` switch to `Г`.

Before the first game, store `11` in `R0`; store `0` in `Rb`, `Rc`, and `Rd`; store route constants `4000` in `Ra`, `2000` in `R9`, `6000` in `R8`, `-1500` in `R7`, `8000` in `R6`, `-1500` in `R5`, `10000` in `R4`, `1800` in `R3`, `13000` in `R2`, and `100` in `R1`.

Use `0` in `Re` for stopwatch mode. For fuel mode, the source suggests fuel values around `30` to `40` in `Re`.

## Import Note

The `.pmk` save stores one seven-command page out of display order. The listing here follows the order printed in the source PDF.
