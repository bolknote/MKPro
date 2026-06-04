# White Starts... and Loses

- Author: Stanislav Chegodaev
- Source: *Tekhnika - Molodezhi*, No. 4, 1988, «Клуб электронных игр», article «Белые начинают... и проигрывают», mirrored at [arbinada.ddns.net](http://arbinada.ddns.net/pmk-files/magazines/%D0%A2%D0%B5%D1%85%D0%BD%D0%B8%D0%BA%D0%B0%20%D0%BC%D0%BE%D0%BB%D0%BE%D0%B4%D0%B5%D0%B6%D0%B8%201985-1988%20%D0%9F%D0%9C%D0%9A.zip)
- Program: [white-starts-and-loses.txt](white-starts-and-loses.txt)
- Listing restored from `TM.1988-04.KEI.djvu`

## Description

This is an early full-board checkers program. White is controlled by the
calculator and black by the player. The source notes that it was originally
written by Stanislav Chegodaev for MK-61 and then published in a B3-34 version.

The calculator's strategy is deliberately simple and slow: the article mentions
about ten minutes of calculation per move. It also cannot play kings, so the
game ends when one side is destroyed or when a king appears.

## Setup

The eight leading white and black checkers are numbered. Coordinates of same-
numbered white and black checkers share one register: the white checker is in
the fractional part and the black checker in the integer part.

For the diagrammed starting position enter:

`17.13 П1 26.22 П2 37.33 П3 46.42 П4 57.53 П5 66.62 П6 77.73 П7 86.82 П8`

Then enter `100 П9`, set the `Р-Г` switch to `Г`, and start with
`Сх БП 47 С/П`.

## Play

After the calculator thinks, the display shows the number of the white checker
that moved. Press `↔` to inspect its destination coordinate in `Y`. Then make
the black move as:

`checker-number ПП new-coordinate С/П`

The calculator does not remove captured checkers. The player must manually clear
captured pieces from the corresponding registers, replacing them with reserve
checkers if available, or with zero if the reserve is empty.
