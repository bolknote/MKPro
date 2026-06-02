# Naval Battle

- Author: illi4
- Source: [Habr article](https://habr.com/ru/articles/125484/)
- Program: [naval-battle.txt](naval-battle.txt)

## Description

The game is played on a 10×10 board (rows and columns 0…9). Each side places ten single-cell ships, and players take alternating shots until one side loses all ships.

## Controls

1. Enter a positive seed into `X` and start with `В/О С/П` to initialize the random generator and placement.
2. For a human shot, enter coordinate from `00` to `99` and press `С/П`.
3. `-10` indicates the program is ready to begin and turn handling is active.
4. On hit, the target value is reduced and the same player gets an extra shot.

## Winning

The first side to lose all ships loses; the computer may also be set to move first with `БП 46` (MK-61/MK-52) or `БП 40` (BK-34/MK-54/MK-56).

## Note

This is a full adaptation of the classic battleship rules for PMK execution.
