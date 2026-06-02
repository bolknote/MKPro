# Wolves and Goat 8x8

- Author: Dmitry Kaykov
- Source: [Tekhnika-Molodezhi, 1986 №8 (p.44–47)](http://epizodsspace.airbase.ru/bibl/tehnika_-_molodyoji/1986/8/44-47.html)
- Program: [volki-i-kozlik-8x8.txt](volki-i-kozlik-8x8.txt)
- Source note: Original B3-34 listing adapted with MK-61-compatible patch for lines 45–66.

## Description

This is the 8×8 board version of *Wolves and Goat*: three wolves (human-controlled) try to trap the goat (calculator-controlled).
The goat can move both forward and backward along dark squares, while wolves can move only forward.
The computer checks candidate moves and chooses a move that blocks the goat as long as any legal trap strategy remains.

## Setup

- In the original article, the starting position for 3 wolves is 28, 48, 68 and goat 31.
- Register-based setup follows the program’s internal convention:
  - R1–R3: wolf coordinates,
  - R5: goat coordinate,
  - additional helper registers are initialized by the program flow.
- The same base code is also used for a 4-wolf variant by setting the jump tables as described in the article.

## Notes

This listing uses the MK-61-compatible code section (addresses 45–66) so it works on MK-61 without the original B3-34 compatibility issue.

## Import note

Only the decoded command listing is stored here; this file is in the repository’s standard `ADDR<TAB>COMMAND` format.
