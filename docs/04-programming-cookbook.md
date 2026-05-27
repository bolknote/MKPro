# MK-61 Programming Cookbook

This cookbook is meant for actual programming sessions. It assumes you already know the stack, registers, program mode, and command names from [01-programming.md](./01-programming.md).

## Working Method

Use this workflow for almost every program:

1. Write the formula or state machine in ordinary notation.
2. Decide which values are inputs, outputs, constants, and scratch values.
3. Assign registers before writing code.
4. Convert expressions to RPN.
5. Write an address table.
6. Run one tiny test case.
7. Add jumps, loops, and subroutines only after the straight-line version works.

## Register Plan Template

Put this table above a non-trivial program:

| Register | Meaning | Lifetime |
| --- | --- | --- |
| `R0` | Loop counter or first input | Long |
| `R1` | Main accumulator | Long |
| `R2` | Input / constant | Long |
| `R3` | Input / constant | Long |
| `R4`..`R6` | Indirect-address counters if needed | Medium |
| `R7`..`Re` | Scratch, constants, jump targets | Short/medium |

Prefer `R0`..`R3` for counters because `F L0`..`F L3` can decrement and branch in one command. Prefer `R4`..`R6` only when their indirect-address increment behavior is useful.

## Address Table Template

Use a fixed format while designing:

| Address | Keys | Code | Stack after / note |
| --- | --- | --- | --- |
| `00` |  |  |  |
| `01` |  |  |  |
| `02` |  |  |  |

For commands that consume an address, put the address on its own line:

| Address | Keys | Code | Note |
| --- | --- | --- | --- |
| `10` | `F x=0` | `5E` | If `X = 0`, continue |
| `11` | `30` | `30` | If `X != 0`, jump to `30` |

## Stack Recipes

### Enter a Binary Operation

To compute `a op b`, the stack must contain:

```text
Y = a
X = b
```

Then press `op`. Subtraction and division are ordered:

```text
a - b  =>  a b -
a / b  =>  a b /
```

### Recall Several Registers in the Right Order

Each `П->X R` recall behaves like entering a new number: the previous `X` moves to `Y`.

To compute `(R2 + R3) * R4`, recall in reverse use order:

```text
П->X 4
П->X 3
П->X 2
+
*
```

### Keep a Value on the Stack

If a value will be reused soon, try to leave it in `Y`, `Z`, or `T` instead of storing and recalling it. This is the central MK-61 skill.

Example: to compute `x^2 + x`, with `x` already in `X`:

```text
В↑
F x^2
+
```

After `В↑`, both `X` and `Y` contain `x`. Squaring changes `X` to `x^2`, then `+` computes `Y + X`.

### Duplicate a Register Value

If `R2` contains `x` and you need two copies on the stack:

```text
П->X 2
В↑
```

Now `X = x`, `Y = x`.

### Swap Arguments

Use `<->` when the stack order is wrong. It often saves a store/recall pair.

```text
Y = divisor
X = dividend
<->
/
```

This computes `dividend / divisor`.

## Branching Patterns

MK-61 conditionals use the next step as the failure address. If the condition is true, the address is skipped and execution continues.

### If `X = 0` Then Block

```text
00  F x=0
01  ELSE
02  then-code
...
nn  БП
nn+1 END
ELSE:
...
END:
```

For `F x=0`, `then-code` runs only when `X` is zero. If `X` is not zero, execution jumps to `ELSE`.

### If `X != 0` Then Block

```text
00  F x!=0
01  ELSE
02  then-code
...
```

Same structure, different condition.

### If / Else

```text
00  condition
01  ELSE
02  then-code
03  ...
04  БП
05  END
ELSE:
06  else-code
END:
```

The exact addresses depend on code length. The important part is: condition points to `ELSE`, and the `then` branch jumps over `else`.

### Compare Two Values

To test `a - b`, put `a` in `Y` and `b` in `X`, then subtract:

```text
П->X a
П->X b
-
```

Now:

- `X = 0` means `a = b`.
- `X >= 0` means `a >= b`.
- `X < 0` means `a < b`.

If your recalls are in the wrong order, use `<->` before `-`.

## Loop Patterns

### Counted Loop with `F L0`

Use `R0` as a counter. If `R0 = n`, this pattern repeats while the decremented counter is non-zero.

```text
00  setup
LOOP:
...
10  F L0
11  LOOP
12  С/П
```

Remember: `F L0` changes `R0`. Do not use `R0` for another long-lived value inside the loop.

### Manual While Loop

Use a conditional at the top:

```text
LOOP:
00  compute-test-value-in-X
01  F x!=0
02  END
03  body
04  БП
05  LOOP
END:
06  С/П
```

This loops while `X != 0`.

### Repeat Until Zero

Use the test at the bottom when the body must run at least once:

```text
LOOP:
00  body
01  compute-test-value-in-X
02  F x=0
03  LOOP
04  С/П
```

This repeats until the test value becomes zero.

## Subroutine Patterns

### Simple Subroutine

```text
00  main-code
05  ПП
06  40
07  main-continues
...
40  subroutine-code
...
48  В/О
```

Keep the stack contract explicit:

```text
Input:  X = value
Output: X = transformed value
Clobbers: Y, X1
```

### Subroutine with Constants

Store constants in high registers and document them:

```text
R9 = 32
Ra = 1.8
```

The program can then use `П->X 9` or `П->X a` instead of entering numeric literals in code.

## Indirect Addressing Recipes

Use indirect addressing first in its documented form: a register contains an integer address or register number.

### One-Step Jump Table

If `R7` contains an address:

```text
К БП 7
```

This is a computed `goto` in one step.

### One-Step Subroutine Call

If `R8` contains an address:

```text
К ПП 8
```

This is useful for dispatching to one of several small routines.

### One-Step Register Selection

If `R9` contains a register number:

```text
К П->X 9
```

This recalls from the selected register. With `R9 = 4`, it recalls `R4`.

Avoid fractional, negative, or hexadecimal indirect values until the ordinary program works. Those transformations are powerful, but they make debugging much harder.

## Debugging Checklist

On a real calculator:

1. Start with `Cx` and known register values.
2. Enter only the first few program steps.
3. Use `F АВТ`, `В/О`, and `С/П` to run.
4. Use `ШГ->` and `ШГ<-` to inspect or patch steps.
5. Add `С/П` temporarily inside a program to inspect intermediate `X`.

In an emulator:

1. Watch `X`, `Y`, `Z`, `T`, and the program counter.
2. Step through the first loop iteration slowly.
3. Check whether a conditional skipped its address or jumped to it.
4. Check whether a recall lifted the stack one time too many.
5. Save a known-good state before trying undocumented tricks.

## Example: Linear Function

Compute:

```text
y = a*x + b
```

Register plan:

| Register | Meaning |
| --- | --- |
| `R0` | `x` |
| `R1` | `a` |
| `R2` | `b` |

Program:

| Address | Keys | Code | Meaning |
| --- | --- | --- | --- |
| `00` | `П->X 2` | `62` | Recall `b` |
| `01` | `П->X 0` | `60` | Recall `x` |
| `02` | `П->X 1` | `61` | Recall `a` |
| `03` | `*` | `12` | `a*x` |
| `04` | `+` | `10` | `a*x + b` |
| `05` | `С/П` | `50` | Stop |

## Example: Horner Polynomial

Compute:

```text
p(x) = ((a*x + b)*x + c)
```

Register plan:

| Register | Meaning |
| --- | --- |
| `R0` | `x` |
| `R1` | `a` |
| `R2` | `b` |
| `R3` | `c` |

Program:

| Address | Keys | Meaning |
| --- | --- | --- |
| `00` | `П->X 1` | `a` |
| `01` | `П->X 0` | `x` |
| `02` | `*` | `a*x` |
| `03` | `П->X 2` | `b` |
| `04` | `+` | `a*x + b` |
| `05` | `П->X 0` | `x` |
| `06` | `*` | `(a*x + b)*x` |
| `07` | `П->X 3` | `c` |
| `08` | `+` | result |
| `09` | `С/П` | stop |

Horner form is usually shorter and stack-friendlier than computing `x^2` separately.

## Example: Sum `1 + 2 + ... + n`

Input: `n` in `X`.

Register plan:

| Register | Meaning |
| --- | --- |
| `R0` | counter |
| `R1` | sum |

Program:

| Address | Keys | Meaning |
| --- | --- | --- |
| `00` | `X->П 0` | Store counter `n` |
| `01` | `0` | Initial sum |
| `02` | `X->П 1` | Store sum |
| `03` | `П->X 1` | Recall sum |
| `04` | `П->X 0` | Recall counter |
| `05` | `+` | Add counter to sum |
| `06` | `X->П 1` | Store sum |
| `07` | `F L0` | Decrement counter |
| `08` | `03` | Repeat if non-zero |
| `09` | `П->X 1` | Recall final sum |
| `10` | `С/П` | Stop |

For `n = 5`, the result is `15`.

## Example: Absolute Difference

Compute:

```text
abs(a - b)
```

Register plan:

| Register | Meaning |
| --- | --- |
| `R0` | `a` |
| `R1` | `b` |

Program:

| Address | Keys | Meaning |
| --- | --- | --- |
| `00` | `П->X 0` | `a` |
| `01` | `П->X 1` | `b` |
| `02` | `-` | `a - b` |
| `03` | `К |x|` | absolute value |
| `04` | `С/П` | stop |

This is shorter than branching around a sign correction.

## Example: Clamp Negative to Zero

Compute:

```text
max(X, 0)
```

Simple branch version:

| Address | Keys | Meaning |
| --- | --- | --- |
| `00` | `F x>=0` | If `X >= 0`, keep it |
| `01` | `04` | If negative, jump to zero branch |
| `02` | `С/П` | Stop with original `X` |
| `03` | `К НОП` | Padding / optional use |
| `04` | `Cx` | Negative branch: set `X = 0` |
| `05` | `С/П` | Stop |

This shows the conditional convention clearly: true skips the address and continues; false jumps.

## Memory-Saving Checklist

Before declaring that a program does not fit:

- Reorder recalls so the stack does more work.
- Use Horner form for polynomials.
- Replace `БП aa` with `В/О` if the target is `01` and the return stack is clean.
- Replace direct jumps/calls with indirect commands when the target already sits in a register.
- Use `F L0`..`F L3` for decrement-and-branch.
- Use `К ЗН`, `К |x|`, `К [x]`, or `К {x}` instead of a branch when they encode the decision.
- Overlay a jump address with an opcode only after the clear version works.
- Move constants into registers when several programs or subroutines share them.
- Remove temporary `С/П` stops used for debugging.

## When to Use Undocumented Tricks

Use dark addresses, X2 restoration, sign-digits, and `F*` insertion only when:

- the documented version is correct,
- you have a saved copy,
- the emulator confirms the exact behavior,
- and the saved steps are worth the loss of readability.

For most practical programs, stack scheduling, `F Lx`, indirect jumps, and arithmetic branch removal are enough.

## Sources

- [Antowka: programming the Elektronika MK61](https://antowka.ru/programming-eletronika-mk61/)
- [Habr: programming on a calculator](https://habr.com/ru/articles/111099/)
- [Serge Anvarov: optimization tricks](https://sergeanvarov.github.io/russian/mk61/uf/tricks.html)
- [Anvarov command appendix](https://sergeanvarov.github.io/russian/mk61/uf/commands.html)
