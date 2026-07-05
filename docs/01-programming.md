# How to Program the Elektronika MK-61

The MK-61 is programmed as a sequence of key operations. A program usually repeats the same keystrokes you would perform manually, with extra commands for jumps, subroutines, loops, and register access.

The user program memory is volatile. When power is removed, the entered program and register contents are lost.

For a guided reading order and the full document map, see [README.md](./README.md).

## Manual Mode First

Before programming, use the calculator in automatic mode with an empty program.
The left power switch turns the machine on. The right `Р-Г-Г` switch selects
trigonometric units:

| Position | Unit |
| --- | --- |
| `Р` | Radians |
| middle | Grads (gons), 400 per full circle |
| `Г` | Degrees |

Digit keys enter up to **eight mantissa digits**. Further digit keys are ignored
until you clear or complete the number. Use `.` for the decimal separator. Use
`ВП` (enter exponent) for scientific notation: type the mantissa, press `ВП`,
type the two-digit exponent, then `+/-` if the exponent is negative.

Representative range:

```text
1e-99 <= abs(x) <= 9.9999999e99
```

`Cx` clears register `X` to zero. It does not move the stack the way a new
number entry often does; see [12-danilov-secrets-mk61-delta.md](./12-danilov-secrets-mk61-delta.md).

## RPN Basics

The calculator uses Reverse Polish Notation. There is no equals key. Operands are entered first, then the operation is pressed.

Example: to calculate `(2 + 5) * 3`, enter:

```text
3 В↑ 5 В↑ 2 + *
```

After a number is typed, `В↑` lifts the stack: `X` is copied to `Y`, `Y` to `Z`, `Z` to `T`, and the old `T` is discarded. Binary operations use `Y` and `X`, place the result in `X`, and usually drop the stack.

Operand order matters for `-` and `/`:

```text
Y - X   gives   (minuend) - (subtrahend)
Y / X   gives   (dividend) / (divisor)
```

After `2` `В↑` `2` `*`, the display shows `4`. The second `В↑` is optional in
that pattern because `В↑` copies `X` into `Y` without clearing `X`.

## Registers

The MK-61 has 15 addressable registers:

```text
R0 R1 R2 R3 R4 R5 R6 R7 R8 R9 Ra Rb Rc Rd Re
```

Use:

- `X->П R` to store the displayed `X` value into register `R`.
- `П->X R` to recall register `R` into `X`.

Registers are the normal way to pass inputs to a program and to keep intermediate values without wasting stack depth.

The stack registers (`X`, `Y`, `Z`, `T`), saved previous-`X` state
(`X1`/`BX`), and the display-side `X2` state are separate from these 15
addressable memory registers. `X2` is still important in program mode because
some commands synchronize `X -> X2` and number-entry commands can restore
`X2 -> X`; see [00-hardware.md](./00-hardware.md#user-visible-architecture) and
[15-x2-display-register.md](./15-x2-display-register.md).

## Entering and Running Programs

Switch to program-entry mode with `F ПРГ`. On many keyboard diagrams, `ПРГ` is the shifted label on the `ВП` key. The display shows the current step address, starting from `00`.

Each entered command occupies one program step. Some flow-control commands
consume the next step as an address. The stock maximum official program size is
105 steps (`00`..`A4`); the `mk61s-mini-expand` compiler feature profile treats
112 steps (`00`..`B1`) as official.

Useful mode and editing keys:

| Key | Meaning |
| --- | --- |
| `F ПРГ` | Enter program mode |
| `F АВТ` | Return to automatic calculation mode |
| `ШГ->` | Step forward through program memory |
| `ШГ<-` | Step backward through program memory |
| `В/О` | Reset the program counter to the beginning / return from subroutine |
| `С/П` | Start or stop execution |

To run a program from the beginning, return to automatic mode, press `В/О`, then `С/П`.

## Example 1: `(x + y) * z`

Store the three inputs in registers:

```text
z -> R4
y -> R3
x -> R2
```

Enter this program:

| Address | Keys | Code | Meaning |
| --- | --- | --- | --- |
| `00` | `П->X 4` | `64` | Recall `z` |
| `01` | `П->X 3` | `63` | Recall `y`, stack lifts |
| `02` | `П->X 2` | `62` | Recall `x`, stack lifts |
| `03` | `+` | `10` | Compute `x + y` |
| `04` | `*` | `12` | Multiply by `z` |
| `05` | `С/П` | `50` | Stop |

For `x=2`, `y=5`, `z=3`:

```text
3 X->П 4
5 X->П 3
2 X->П 2
В/О С/П
```

The result is `21`.

## Example 2: Fahrenheit to Celsius

Formula:

```text
C = (F - 32) / 1.8
```

Use `R2` for `F`, `R3` for `32`, and `R4` for `1.8`.

Program:

| Address | Keys | Code |
| --- | --- | --- |
| `00` | `П->X 2` | `62` |
| `01` | `П->X 3` | `63` |
| `02` | `-` | `11` |
| `03` | `П->X 4` | `64` |
| `04` | `/` | `13` |
| `05` | `С/П` | `50` |

Program code:

```text
62 63 11 64 13 50
```

For `50 F`, store:

```text
50 X->П 2
32 X->П 3
1.8 X->П 4
В/О С/П
```

The result is `10`.

## Example 3: Circle Area

Formula:

```text
area = pi * r^2
```

Store the radius in `R1`.

| Address | Keys | Code | Meaning |
| --- | --- | --- | --- |
| `00` | `П->X 1` | `61` | Recall `r` |
| `01` | `F x^2` | `22` | Square it |
| `02` | `F pi` | `20` | Push pi |
| `03` | `*` | `12` | Multiply |
| `04` | `С/П` | `50` | Stop |

For `r=5`, store `5 X->П 1`, run the program, and the display shows about `78.539815`.

## Branches, Subroutines, and Conditions

Direct jumps use a command plus a two-step address:

| Command | Meaning |
| --- | --- |
| `БП aa` | Unconditional jump to address `aa` |
| `ПП aa` | Call subroutine at address `aa` |
| `В/О` | Return from subroutine; with an empty return stack it behaves like a jump to `01` |

Conditional commands also take an address. If the condition is true, the address step is skipped and execution continues. If the condition is false, execution jumps to the address.

| Command | Condition |
| --- | --- |
| `F x!=0 aa` | True if `X` is not zero |
| `F x>=0 aa` | True if `X` is non-negative |
| `F x<0 aa` | True if `X` is negative |
| `F x=0 aa` | True if `X` is zero |

This inverted-looking convention matters: the address is the failure branch.

## Counter Loops

Loop commands operate on registers `R0`..`R3`:

| Command | Register |
| --- | --- |
| `F L0 aa` | `R0` |
| `F L1 aa` | `R1` |
| `F L2 aa` | `R2` |
| `F L3 aa` | `R3` |

The loop command decrements the register. If the result is not zero, it jumps to `aa`; if the result is zero, it falls through.

## Factorial Sketch

Assume `n` starts in `X`. Use `R0` as counter and `R1` as product.

| Address | Keys | Meaning |
| --- | --- | --- |
| `00` | `X->П 0` | Store counter |
| `01` | `1` | Start product |
| `02` | `X->П 1` | Store product |
| `03` | `П->X 1` | Recall product |
| `04` | `П->X 0` | Recall counter |
| `05` | `*` | Multiply |
| `06` | `X->П 1` | Save product |
| `07` | `F L0` | Decrement counter |
| `08` | `03` | Repeat if non-zero |
| `09` | `С/П` | Stop |

## Development Workflow

For anything beyond a few lines, follow the usual PMK pipeline:

1. State the problem and expected outputs.
2. Write formulas and domain limits.
3. Choose a method (direct formula vs iteration).
4. Sketch an algorithm.
5. Draw a flowchart when branches or loops appear; see [07-flowcharts.md](./07-flowcharts.md).
6. Type the program with a register plan and address table.
7. Debug with control examples; see [09-debugging-and-service.md](./09-debugging-and-service.md).
8. Document initialization and stops for the user.

Applied walkthroughs live in [10-applied-examples.md](./10-applied-examples.md).

## Practical Style

Memory is tight, so MK-61 programs are usually shaped around the stack. Before spending a register recall, ask whether a value can be kept alive in `Y`, `Z`, or `T`. Before spending a two-step direct jump, ask whether an indirect command or loop command can do the same job in one step. These habits are not just micro-optimizations; they are the normal programming style of the machine.

## Related Docs

- [04-programming-cookbook.md](./04-programming-cookbook.md) — stack recipes and branch templates
- [07-flowcharts.md](./07-flowcharts.md) — from diagram to linear memory
- [09-debugging-and-service.md](./09-debugging-and-service.md) — inspection and patching

## Sources

- Igor Danilov, *Secrets of the Programmable Microcalculator*, chapters 1–4 and 6
- [Antowka: programming the Elektronika MK61](https://antowka.ru/programming-eletronika-mk61/)
- [Habr: programming on a calculator](https://habr.com/ru/articles/111099/)
- [Antowka: Elektronika MK61 overview](https://antowka.ru/calc-electronika-mk61/)
- [Elektronika MK-61 operating manual scan](https://www.wass.net/manuals/Elektronika%20MK-61.pdf)
