# Пи-чо

- Author: А. В. Раков
- Source: *Техника - молодежи*, No. 7, 1988, «Вверх по лестнице, бегущей вбок»
- Program: [pi-cho.txt](pi-cho.txt)
- Listing restored from DjVu scan with macOS Vision OCR and manual table check.
- This file includes the article's MK-61/MK-52 patch.

## Description

The game is played on a cylindrical field with four levels and nine cells per
level. A character starts on the first level and must climb to the roof by
matching the moving square coordinates on successive levels. Each square has a
speed; changing one square redistributes speed units across the others.

The display shows an eight-digit value split by the decimal separator: speeds
are on the left, coordinates on the right. Register `Y` contains the current
level. Reaching the hatch on the fourth level sets `Y` to `5`.

## MK-61 patch

The printed program is for B3-34/MK-54. The article's MK-61/MK-52 note replaces
the original command at address `69` with `Пх0 хПЕ F↻ КПхЕ` and updates branch
targets:

- `55.80` -> `55.83`
- `60.94` -> `60.97`
- `62.80` -> `62.83`
- `93 хПС` -> `96 хПС`

The note prints the last two old `97` targets as `A1`, but the same +3 shift
used by the rest of the patch places the shifted final `В/О` at `A0`; this
listing uses `A0` at addresses `87` and `92`.

## Setup

Before the first run:

- `5 хП9`
- `63 хПД`
- `96 хПС`
- `Сx хП0`

Put the difficulty, from `1` to `9`, into registers `R1`..`R4`. Put starting
cell coordinates into `R5`..`R8`; the article's simple setup uses the same
number for all eight registers.

Start with `Сx С/П`. On each turn, enter the square number and press `С/П`.
