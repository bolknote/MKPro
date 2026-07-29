# Sailing Regatta

- Source: Финк Л.М., Папа, мама, я и микрокалькулятор (1988)
- Program: [sailing-regatta.txt](sailing-regatta.txt)

## Description

This is a numeric sailing simulation.
The calculator updates boat position each interval using wind speed, current speed vector,
and sail angle controls. Register `R4` stores wind direction and `R5` current sail angle.
Coordinates are accumulated in `R2` and `R3`; `R6`, `R7`, `R8` hold intermediate speed values.

## Setup

1. Load the program.
2. Set switch `P` to angle mode (author note in book mentions fixed `"Г"` mode was preferred there).
3. Enter wind direction to `R4` and start sail angle in `R5`.
4. Start with:

```text
В/О С/П
```

For each step, the calculator displays next `X`, `Y` coordinates pair so you can track movement.

## Notes

The original model assumes a fixed wind direction during one draw and applies the returned `5×` optimization factor
to compute coordinate increments directly.
