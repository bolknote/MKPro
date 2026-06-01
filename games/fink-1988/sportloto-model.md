# Sportloto Model (5 out of 36)

- Source: [Финк Л.М., Папа, мама, я и микрокалькулятор (1988)](file:///Users/bolk/Downloads/%D0%A4%D0%B8%D0%BD%D0%BA%20%D0%9B.%D0%9C.%2C%20%D0%9F%D0%B0%D0%BF%D0%B0%2C%20%D0%BC%D0%B0%D0%BC%D0%B0%2C%20%D1%8F%20%D0%B8%20%D0%BC%D0%B8%D0%BA%D1%80%D0%BE%D0%BA%D0%B0%D0%BB%D1%8C%D1%82%D0%BE%D1%80%20(1988).pdf)
- Program: [sportloto-model.txt](./sportloto-model.txt)

## Description

This program is a basic pseudorandom lottery simulator for a `5 of 36` draw.
It multiplies a uniform pseudorandom value in `R0` by 36, shifts it into `1..36`,
takes integer part, and outputs each generated number with `С/П`.

The printed variant in the book does not remove duplicate numbers inside a draw.
In that case, repeating numbers must be discarded and the draw repeated manually.

## Setup

After loading:

1. Set the pseudorandom sequence seed in `R0` (the book example uses a fixed value).
2. Press:

```text
С/П
```

and repeatedly read five winning numbers on each draw.

## Notes

If you want better randomness for real play, replace the seed in `R0` (e.g. from ticket number and bus fare style tricks described in the book).
