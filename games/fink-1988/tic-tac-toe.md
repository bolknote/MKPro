# Tic-Tac-Toe (Unbeatable)

- Source: Финк Л.М., Папа, мама, я и микрокалькулятор (1988)
- Program: [tic-tac-toe.txt](./tic-tac-toe.txt)

## Description

This is a dialog-mode tic-tac-toe opponent that uses a full minimax-like strategy for 3x3.
For all move combinations that deviate from the forced optimal reply path, the program steers to
a forced win quickly.

When loaded, the program makes its first move automatically as cross.

## Setup

1. Load the program.
2. Set `RX = 5` (as required by the printed control logic).
3. Start by pressing:

```text
В/О С/П
```

## Gameplay

- Enter your move as a cell number `1..9` into `RX`.
- Press `С/П` to get the calculator's next move.
- On a win, it shows its winning move scaled by one million.
- On a draw, it shows `О`.
- Any deviation from expected optimal replies is handled as immediate win for the calculator.

## Notes

The strategy is tuned for play-by-play response and is intended to be robust against
suboptimal replies.
