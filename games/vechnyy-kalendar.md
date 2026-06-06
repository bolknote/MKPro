# Perpetual Calendar

- Authors: I. D. Danilov and G. V. Slavin
- Source: local archive `vechnyj_kalendar.zip`; originally published in `Five Evenings with a Microcalculator`
- Program: [vechnyy-kalendar.txt](vechnyy-kalendar.txt)

## Description

Computes the Gregorian weekday code for a date and the number of days between two dates.

Enter the starting date as `day`, `В↑`, `month`, `В↑`, `year`, then `В/О`, `С/П`. The calculator displays the weekday code. Enter the ending date in the same stack format and press `С/П`; the calculator displays the ending weekday code. Press `↔` to inspect the number of days between the dates.

Weekday codes are `0` for Sunday, `1` for Monday, `2` for Tuesday, `3` for Wednesday, `4` for Thursday, `5` for Friday, and `6` for Saturday.

## Setup

Before use, store constants:

- `30.5` in `Ra`
- `365.25` in `Rb`
- `694066` in `Rc`

## Import Note

The `.pmk` save stores one seven-command page out of display order and keeps saved-memory tail data after the program. The listing here follows the order printed in the source PDF and uses the first 57 program commands.
