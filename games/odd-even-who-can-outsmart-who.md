# Odd-Even (Who Can Outsmart Who)

- Source: Финк Л.М., Папа, мама, я и микрокалькулятор (1988)
- Program: [odd-even-trick.txt](odd-even-trick.txt)

## Description

The calculator adapts to your pattern and tries to predict the next bit (0 or 1).
It stores the last three your choices in `RA`, `RB`, `RC`, estimates which value appears more often
for the same context, outputs its guess in `R9`, and counts your score in `R8`.

## Setup

After entering the program, initialize and start with:

```text
(initial) С/П
```

For each move:

- Enter your next bit (`0` or `1`) into `RX` as requested by the text of play.
- The calculator displays its prediction in the same step.

## Game loop

The win/lose accumulator in `R8` is updated after each round:

- wrong guess adds `+1` to your score,
- correct guess subtracts `1`.

To run again, continue feeding your new choices into the expected input register and pressing `С/П`.
