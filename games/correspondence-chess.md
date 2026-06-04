# Correspondence Chess

- Author: Nikolai Avdeev
- Source: *Tekhnika - Molodezhi*, No. 12, 1986, «Клуб электронных игр», article «Партия по переписке», mirrored at [arbinada.ddns.net](http://arbinada.ddns.net/pmk-files/magazines/%D0%A2%D0%B5%D1%85%D0%BD%D0%B8%D0%BA%D0%B0%20%D0%BC%D0%BE%D0%BB%D0%BE%D0%B4%D0%B5%D0%B6%D0%B8%201985-1988%20%D0%9F%D0%9C%D0%9A.zip)
- Program: [correspondence-chess.txt](correspondence-chess.txt)
- Listing restored from `TM.1986-12.KEI.djvu`

## Description

This is a small chess endgame. The player has the white king, bishop, and
knight; the programmable calculator controls the lone black king. The aim is to
mate the black king, preferably on `h8` (`88`). The article notes that mate on
`a8`, `h1`, or other light edge squares is also possible, but harder if black
plays correctly.

The original listing is for the Elektronika B3-34. It uses only the compact
programming techniques available to that calculator family, including indirect
transitions through the subroutine stack.

## Setup

First load the program and initialize the helper constants:

- `1 П9`
- `73 ПВ`
- `7 ПС`
- `0.1 ПД`

Enter the piece coordinates:

| Registers | Piece |
| --- | --- |
| `R7`, `R8` | white king |
| `R4`, `R5` | black king |
| `R1`, `R2` | white bishop |
| `R0`, `Ra` | white knight |

The diagrammed starting position from the article is entered as:

`3 П7 5 П0 7 П1 1 П8 П2 ПА 5 П4 8 П5`

Put the end-of-game signal `E50` in `R3` with `150 ВП 99 ВП П3`, then jump to
the move generator with `БП 52`.

## Play

Make a legal white move by writing the new coordinate or coordinates of the
moved piece to the corresponding registers, then press `С/П`. The calculator
usually thinks for about 50 seconds and replies with the black king's new
two-digit coordinate: the first digit is the file and the second is the rank.

If the black king is mated or stalemated, the display shows `E` with the number
of moves remaining before the 50-move limit. If the display shows `Г` with a
number, the 50-move rule has been exceeded and the game is drawn.

The calculator does not validate the player's move. The source warns that the
white pieces must not be left en prise, because the black king may capture them.
