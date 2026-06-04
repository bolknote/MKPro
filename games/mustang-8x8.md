# Mustang 8x8

- Author: N. Romashov
- Source: *Tekhnika - Molodezhi*, No. 6, 1988, «Клуб электронных игр», letter «Укрощение Мустанга», mirrored at [arbinada.ddns.net](http://arbinada.ddns.net/pmk-files/magazines/%D0%A2%D0%B5%D1%85%D0%BD%D0%B8%D0%BA%D0%B0%20%D0%BC%D0%BE%D0%BB%D0%BE%D0%B4%D0%B5%D0%B6%D0%B8%201985-1988%20%D0%9F%D0%9C%D0%9A.zip)
- Transcription source: [Habr article](https://habr.com/ru/articles/125484/)
- Program: [mustang-8x8.txt](mustang-8x8.txt)

## Description

A board game on an 8×8 grid. The “mustang” piece moves like a chess knight and tries to avoid hunters.

This version was adapted by N. Romashov, a ninth-grade student from Leningrad,
from the earlier MK-52/MK-61 «Mustang» program to the MK-54 and B3-34. The
published version expands the playing field to `8×8`.

## How It Works

- The board is indexed by coordinates `1.1` to `8.8`.
- Hunters are controlled by the player and move any number of cells diagonally.
- The goal is to corner the mustang so it cannot move.
- In the sample setup there are nine hunters, but the board and count can be customized.
