# Mustang 8x8

- Author: illi4
- Source: [Habr article](https://habr.com/ru/articles/125484/)
- Program: [mustang-8x8.txt](mustang-8x8.txt)

## Description

A board game on an 8×8 grid. The “mustang” piece moves like a chess knight and tries to avoid hunters.

## How It Works

- The board is indexed by coordinates `1.1` to `8.8`.
- Hunters are controlled by the player and move any number of cells diagonally.
- The goal is to corner the mustang so it cannot move.
- In the sample setup there are nine hunters, but the board and count can be customized.
