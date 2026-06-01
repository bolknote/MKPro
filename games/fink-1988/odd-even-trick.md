# Odd-Even (Who Can Outsmart Who)

- Source: [Финк Л.М., Папа, мама, я и микрокалькулятор (1988)](file:///Users/bolk/Downloads/%D0%A4%D0%B8%D0%BD%D0%BA%20%D0%9B.%D0%9C.%2C%20%D0%9F%D0%B0%D0%BF%D0%B0%2C%20%D0%BC%D0%B0%D0%BC%D0%B0%2C%20%D1%8F%20%D0%B8%20%D0%BC%D0%B8%D0%BA%D1%80%D0%BE%D0%BA%D0%B0%D0%BB%D1%8C%D1%82%D0%BE%D1%80%20(1988).pdf)
- Program: [odd-even-trick.txt](./odd-even-trick.txt)

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
