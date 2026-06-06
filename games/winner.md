# Winner

- Author: Mikhail Pukhov
- Source: *Tekhnika Molodezhi*, No. 7, 1987, «Клуб электронных игр» (KEI column)
- Program: [winner.txt](winner.txt)
- Listing restored from a *Tekhnika Molodezhi*, No. 7, 1987 scan with MK-61 patches from the article

## Description

«Победитель» (*Winner*) is an improved version of B. Grekov’s «Волки и козлик»
(*Wolves and a Goat*) on a 9×9 board. The player moves the goat; four wolves
respond automatically. The goat starts near the center and tries to reach the
top row (horizontal 9); the wolves begin on fields 91, 93, 97, and 99.

The coordinate system matches the one used in
[`treasure-cave.md`](./treasure-cave.md): a packed value `floor.000000N` where
the integer part is the row, the last fractional digit is the column, and its
position encodes the horizontal coordinate.

## Setup

Before the first move, preload registers and mark jump targets (as in the
article):

| Register | Initial value | Role |
| --- | --- | --- |
| `R1` | `91` | Wolf 1 position |
| `R2` | `93` | Wolf 2 position |
| `R3` | `97` | Wolf 3 position |
| `R4` | `99` | Wolf 4 position |
| `R5` | `55` | Goat start (any central cell is allowed) |
| `R6` | `11` | Previous goat move code (`11`, `-11`, `9`, or `-9`) |
| `R7` | (optional) | Goat deviation from the center vertical |
| `Rc` | `20` | Jump address; also a loop counter coefficient |
| `R9` | `9` | Jump address |
| `Ra` | `40` | Jump address; also a counter coefficient |
| `Rb` | `41` | Jump address |
| `Rd` | `58` | Jump address (**required on MK-61**) |

Each wolf keeps its own number (1..4) and coordinates in the matching register.
Register `R6` stores the direction of the goat’s previous move as the difference
between old and new coordinates.

## How to play

Enter a move, then press `С/П`. The article uses the same directional codes as
*Treasure Cave*:

| Code | Move |
| --- | --- |
| `2` | Down |
| `4` | Left |
| `6` | Right |
| `8` | Up |

The program distinguishes **attack** (goat moved forward: `R6` positive, codes
`9` or `11`) from **defense** (goat moved backward). On the first turn,
command `00.КБПc` (equivalent to `БП 20`) enters the main loop.

If the goat leaves the board, control returns to address `01` with zero in `X`,
then the conditional at step `01` sends execution to `09` — reset and a new
game. A successful wolf win shows the updated goat coordinates with zero in `X`.

## MK-61 adaptation

The magazine listing targets MK-54/56 (98 steps, addresses `00`–`97`). The
[`winner.txt`](winner.txt) file is the **MK-61 version** (101 steps) with these
patches applied:

1. **47–49** — replace `КПх7` with `Пх0`, `хПe`, `КПхe` (artificial `0`–`E`
   register link).
2. **22–23** — `Fx>=0` / `59` instead of `Fx<0` / `57`.
3. **62–63** — `Fx=0` / `66` instead of `Fx=0` / `64`.
4. **68–69** — `F L0` / `75` instead of `F L0` / `72`.
5. **73–75** — `Fx!=0`, `78`, `Пхc` instead of `Fx!=0`, `Пх8` (code-address
   link for the `72.Пхc` / `6С` trick described in the article).

Set `Rd` to `58` on MK-61 (`58 хПd`).

## Program blocks (from the article)

| Steps | Purpose |
| --- | --- |
| 00 | Indirect jump into the main loop (`КБПc` → `20`) |
| 01–09 | Out-of-board check and restart |
| 11–12 | Subroutine call into the move handler |
| 20–23 | Inspect the goat’s previous move |
| 24–56 | Attack branch (forward move) |
| 57+ | Defense branch (backward move) |
| 47–49 | MK-61 indirect recall through `Re` |
| 73–75 | MK-61 code-address dispatch block |

## Related material

Grekov’s original «Волки и козлик» on a 9×9 field was promised in an earlier KEI
column; this program fulfills that promise with a stronger wolf defense. The
same issue also contains [`treasure-cave.txt`](treasure-cave.txt) by Vasiliy
Zakharenko.
