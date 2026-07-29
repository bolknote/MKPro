# Чемпион

- Author: КЭИ
- Source: *Техника - молодежи*, No. 11, 1986, «Требуется выигрышная стратегия»
- Program: [chempion.txt](chempion.txt)
- Listing restored from DjVu scan with macOS Vision OCR and manual table check.
- This file includes the article's MK-61/MK-52 patch.

## Description

Strategy game on a 9x9 checkers board, based on «Волки и козлик». The PMK
plays the goat in the center of the board, and the player controls four wolves.
The goat tries to break through to the last rank; the wolves try to trap it
against the board edge.

The printed program is for B3-34/MK-54. For MK-61/MK-52, the article says to
insert the standard bridge `Пх0 хПЕ XY` between original addresses `80` and `81`,
change the call target at address `58` from `88` to `91`, and use `97 хПД`.
Those changes are already applied here.

## Setup

Set the angle switch to grads.

Enter transition addresses:

- `20 хП8`
- `42 хПА`
- `65 хПВ`
- `69 хПС`
- `97 хПД`

Enter the starting pieces:

- `19 хП1`
- `39 хП2`
- `79 хП3`
- `99 хП4`
- `55 хП5`

Start with `Сx В/О С/П`. At each stop the display shows the square chosen by
the PMK. To move, enter the piece number, `ПП`, the target square, and `С/П`.
