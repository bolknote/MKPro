# Danilov-Era Secrets and the MK-61 Delta

Igor Danilov's final chapter collects "secrets" of the B3-34 family: behavior
documented incompletely or not at all in the factory manual. Serge Anvarov's MK-61
notes go much deeper (X2, dark addresses, F* opcodes). This document records
**Danilov-specific** items and how they translate to the MK-61.

Read [02-undocumented-tricks.md](./02-undocumented-tricks.md) for advanced MK-61
eggology. Read this file when porting 1980s B3-34 listings or understanding
classic magazine programs.

## Family Warning

Danilov explicitly warns that later calculators reuse opcode bytes for **new
official functions**. A trick typed on a B3-34 may be a normal instruction on
MK-61, or may hang.

Before using any secret in production code:

1. Build the documented version first.
2. Test on the target model or a trusted emulator.
3. Treat B3-34 listings as hints, not guarantees.

## R0 Alias via `T` (Arrow Register)

On B3-34, register commands accept a `T` (arrow) pseudo-name meaning **R0**:

| Command | Code (B3-34 notation) | Effect |
| --- | --- | --- |
| `X->П T` | `4E` | Store to `R0` |
| `П->X T` | `6E` | Recall from `R0` |
| `К X->П T` | `BE` | Indirect store via `R0` |
| `К П->X T` | `DE` | Indirect recall via `R0` |
| `К БП T` | `8E` | Indirect jump via `R0` |
| `К ПП T` | `AE` | Indirect call via `R0` |
| `К x?0 T` | various | Indirect conditional via `R0` |

**Critical B3-34 behavior:** for indirect **flow** commands through `T`, `R0`
is **not** decremented/modified the way it is when you write `К БП 0` explicitly.

On the `mk61` emulator model used by this project, the `*F`
undocumented aliases (`4F`, `6F`, `7F`, …) correspond to using `R0` in those
slots, but they behave like the corresponding `*0` commands, including normal
`R0` transformation. They are **not** a shortcut for using `R0` without changing
it.

**Compiler consequence:** use `*F` aliases only as byte/formal-address tools,
not as R0-preserving computed jump table operations.

## "Error Opcodes" on B3-34 vs MK-61

Danilov lists blue-shift combinations that produced `ЕГГОГ` on B3-34, including
codes in the `26`..`39` and `3C`..`3F` ranges.

On **MK-61**, many of those cells are **real operations**:

| B3-34 "error" idea | MK-61 |
| --- | --- |
| `К +`, `К -`, `К *`, `К /` as errors | still error-like or unenterable; see `03` |
| `К 2`..`К 9` as errors | `К °->′"`, `К |x|`, `К [x]`, logic ops, etc. |
| Trigger `ЕГГОГ` cheaply | use domain errors (`F 1/x` at 0) or `К -` where still invalid |

Do not port B3-34 "stop by error" golf without re-checking each opcode.

## Grads / Gons Mode

Besides radians (`Р`) and degrees (`Г`), many PMKs expose a **middle switch
position** for **grads** (gons): full circle = 400 gon, right angle = 100 gon.

MK-61 trig functions honor the switch. Documented in the official manual but easy
to overlook. Programs that mix unit assumptions fail silently when the switch is
wrong.

## Fraction Truncation via Recall and Loops

Danilov notes side effects beyond the manual:

### `П->X` / `К П->X` and `F L0`..`F L3`

When the source register value is **greater than 1**, fractional parts are
discarded in some paths (similar in spirit to integer addressing).

When **`R0` holds a positive value less than 1**, B3-34 behavior of **`К П->X 0`**
can subtract from the **last mantissa digit** each time instead of a normal
recall. Tsvetkov and Epanichnikov used this in random-number programs.

On the `mk61` emulator model, fractional positive `R0` has two useful
special cases: `К БП 0` jumps to `99` and leaves `R0 = -99999999`; `К П->X 0`
and `К X->П 0` access `R3` and leave the same sentinel. Use these as computed
dispatch/sentinel facts, not as a shorter replacement for direct `R3` access.

### Integer Part Without `К [x]` (Historical)

Before MK-61 integer opcodes, programmers used indirect recall truncation tricks
with registers `4`..`6` increment rules. See [02-undocumented-tricks.md](./02-undocumented-tricks.md)
for the full indirect transformation rules.

## `В/О` as a One-Step Jump

With an **empty return stack**, `В/О` behaves like **`БП 01`**: go to address `01`
in program memory, but costs one cell instead of two.

Caveats:

- In **manual** mode, `В/О` resets to address `00`, not `01`.
- Any pending subroutine frame changes behavior.

Already noted in [01-programming.md](./01-programming.md) and
[02-undocumented-tricks.md](./02-undocumented-tricks.md).

## `ВП` Tricks

Danilov documents program-mode patterns:

| Pattern | Effect |
| --- | --- |
| `В↑ П->X n ВП` | Can drop the leading mantissa digit; for `0..10` useful as fractional-part hack |
| `ВП` after indirect jump | Special first-digit behavior (Anvarov: may become `7`) |
| Exponent entry in programs | X2 restoration side effects; see Anvarov |

On MK-61, **`К {x}`** is the readable fractional-part tool unless you are
matching a byte-identical classic listing.

## `ШГ->` / `ШГ<-` in Calculation Mode

Officially these keys edit the program counter in programming mode. They also
advance/retreat the program counter **during calculation mode** on classic
hardware.

Use: jump into a multi-part program without a two-cell `БП nn` — faster for
interactive tools with many segments.

Verify on your emulator; some interfaces map `ШГ` only in program mode.

## Stack Lift After `В↑` and `Cx`

Documented expectation: after `В↑` or `Cx`, the next **keyboard digit** or
**programmed digit/recall** may behave differently.

Danilov's examples:

```text
Stack cleared, then in program or manual:
  5  В↑  8        -> X=8, Y=5, Z=0, T=0

Stack filled with 1, then:
  Cx  8            -> X=8, Y=1, Z=1, T=1

Store 8 in R1, then:
  5  В↑  П->X 1    -> X=8, Y=5, Z=5, T=0   (differs from digit entry path)
  Cx  П->X 1       -> X=8, Y=0, Z=1, T=1
```

Because MK-61 programming is stack-first, ignoring this splits manual tests from
program behavior.

[03-command-reference.md](./03-command-reference.md) documents pieces (`Cx` next
digit may not lift; `П->X` lifts). This file highlights the **contradiction** that
catches porting bugs.

## Sign Change vs Exponent (`+/-` Bug Class)

Program:

```text
П->X a  +/-  С/П
```

For `X = 1` manual entry, sign flip is correct. For `X = 1e-3` entered as
`1 ВП 3 +/-`, **continuous run** may show `1000` (sign applied to exponent, not
mantissa) while **step mode** shows `-1e-3`.

Treat as a firmware quirk when writing control examples. Always test exponential
inputs; see [08-numerical-errors.md](./08-numerical-errors.md).

## Step Mode vs Continuous Run

Danilov's meta-lessons:

1. Step mode (`ПП`) is invaluable but **not sufficient** for sign-off.
2. Control examples must include **multiple input shapes**.
3. Prefer **`К П->X T` / `К X->П T`** style optimizations only after baseline
   correctness — on MK-61, map to the verified `*F` aliases if applicable.

## What Anvarov Adds Beyond Danilov

Not repeated here in full:

- hidden `X2` display register,
- dark and alias program addresses,
- hexadecimal mantissa digits and sign-digit arithmetic,
- inserting `F0`..`FF` empty operators,
- `5F` hang opcode.

See [02-undocumented-tricks.md](./02-undocumented-tricks.md).

## Quick Port Checklist: B3-34 Listing → MK-61

- [ ] Re-count steps (98 → 105 budget change is minor; algorithm change is not).
- [ ] Replace integer/fraction hacks with `К [x]` / `К {x}` when readable.
- [ ] Replace home-grown RNG with `К СЧ` if statistics allow.
- [ ] Re-map error-stop opcodes.
- [ ] Check angle switch (degrees / radians / grads).
- [ ] Re-run control example in **automatic** mode with exponential inputs.
- [ ] Compare stack layout after `Cx` / `В↑` / `П->X` sequences.

## Sources

- Igor Danilov, *Secrets of the Programmable Microcalculator*, chapter 13
- [Serge Anvarov: undocumented MK-61 features](https://sergeanvarov.github.io/russian/mk61/%D0%9D%D0%B5%D0%B4%D0%BE%D0%BA%D1%83%D0%BC%D0%B5%D0%BD%D1%82%D0%B8%D1%80%D0%BE%D0%B2%D0%B0%D0%BD%D0%BD%D1%8B%D0%B5%20%D0%B2%D0%BE%D0%B7%D0%BC%D0%BE%D0%B6%D0%BD%D0%BE%D1%81%D1%82%D0%B8%20%D0%9F%D0%9C%D0%9A%20%D0%9C%D0%9A-61.html)
- [03-command-reference.md](./03-command-reference.md)
- [02-undocumented-tricks.md](./02-undocumented-tricks.md)
