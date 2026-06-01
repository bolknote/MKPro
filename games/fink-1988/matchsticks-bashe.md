# Matchsticks (Bash) Game

- Source: Финк Л.М., Папа, мама, я и микрокалькулятор (1988)
- Program: [matchsticks-bashe.txt](./matchsticks-bashe.txt)

## Description

This program plays a Nim-like matchstick game (called "Bash") using optimal strategy.
Register `A` holds the initial pile size `S`, register `B` holds the maximum take limit `N`.
The calculator chooses its moves based on `S mod (N + 1)` and tries to move toward winning by forcing
positions where the remainder is zero at your turns.

## Setup

After loading the program, enter the starting pile size into `A` and the limit into `B`.
Press:

```text
В/О С/П
```

## Play

- Enter your move (how many matchsticks you take) into `R1`.
- Press `С/П`; the calculator answers with its move.
- If it wins, it displays `77`.
- If it loses, it displays `0`.
- To let the calculator start first, input it in `R0`, then press:

```text
БП 54 С/П
```

You can check remaining matchsticks in `RD` at any time.
Cheating attempts (taking 0 or more than `N`) are rejected with an error display.
