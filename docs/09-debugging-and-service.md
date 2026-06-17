# Debugging, Optimization, and Program Service

MK-61 programs are tiny products: you design them, type them in, tune them, and
often hand them to another person who never sees your notes. This document
covers inspection tools, optimization habits, and user-facing "service" that
Danilov describes in chapters 8 and 10 of *Secrets of the Programmable
Microcalculator*, updated for the MK-61.

## The Production Pipeline

Danilov maps program creation to ordinary engineering stages:

| Stage | Programmer activity |
| --- | --- |
| Specification | State the problem and acceptable output |
| Method | Choose formulas or a state machine |
| Algorithm | Define exact steps and branches |
| Flowchart | Draw the branch layout; see [07-flowcharts.md](./07-flowcharts.md) |
| First coding | Type version 1 into program memory |
| Debugging | Fix input mistakes, logic, and numeric edge cases |
| Use | Run control examples; document register conventions |

On a real calculator one person wears every hat. In this repository, keep the
same artifacts even when the source lives in `.mkpro` files: register plan,
address table, control example.

## Inspecting Program Memory

Program memory is edited in programming mode (`F ПРГ`). The display shows the
current address on the right and opcode history on the left.

### Browse from the Start

```text
F АВТ
В/О
F ПРГ
```

The display shows address `00`. Each press of `ШГ->` advances the program
counter and shows recent opcodes. Each press of `ШГ<-` goes backward.

The left side shows a sliding window of opcode values. Example display line:

```text
12 61 11 17
```

means address `14` holds `11` (`-`), address `15` holds `61` (`П->X 1`), address
`16` holds `12` (`*`).

### Browse from Address `nn`

To inspect a middle fragment without stepping from `00`:

```text
F АВТ
БП nn
F ПРГ
```

Then use `ШГ->` / `ШГ<-` as usual.

### Patch a Step

1. Step to the address with `ШГ->` / `ШГ<-`.
2. Type the replacement command. It overwrites the current cell.
3. Re-check downstream addresses if lengths changed.

There is **no insert** opcode. Inserting in the middle requires rewriting the
tail of the program unless you reserved space in advance.

## Running and Stopping

| Key | Role |
| --- | --- |
| `F АВТ` | Automatic calculation mode |
| `В/О` | Reset program counter to `00`; return from subroutine |
| `С/П` | Start or stop execution |
| `ПП` | Subroutine call in a program; also used for **instruction step** in some debugging flows |

Typical run from the beginning:

```text
F АВТ
В/О
С/П
```

If a program uses `С/П` as an input boundary, stopping is intentional: the
machine waits for data or shows an intermediate result.

### Step-by-Step Execution

Pressing `ПП` during execution advances one program command at a time in many
setups described in the classic literature. Step mode is excellent for finding
*where* a path diverges, but Danilov warns it is **not** a perfect oracle:
some programs yield different numeric results in step mode vs continuous run.
See [12-danilov-secrets-mk61-delta.md](./12-danilov-secrets-mk61-delta.md).

**Final acceptance criterion:** a control example in normal automatic mode.

## When `ЕГГОГ` Appears

`ЕГГОГ` (often shown as `EGG0G` in Latin transcription) means the calculator
refused an operation: division by zero, invalid log, out-of-range trig, explicit
error opcodes, and similar cases.

Explicit error opcodes such as `К /` are resumable traps, not ordinary `С/П`
stops. On the tested MK-61 ROM they leave the failed value in `X`, copy it to
`X1`, and pause with the program counter two cells after the trap. If execution is
resumed, the cell immediately after the trap is skipped; reserve it as padding or
as an overlay reached only from another path.

Debugging workflow:

1. Note what you were doing when the error appeared.
2. Switch to programming mode (`F ПРГ`). The display shows the **opcode** of the
   faulting or next command depending on firmware state.
3. Return to automatic mode (`F АВТ`). The display may show the **offending
   number** in `X`.
4. Fix the program logic or the input domain.

Intentional `ЕГГОГ` can replace a branch when an invalid operation is acceptable
as a stop signal. On B3-34, several blue-shift arithmetic keys acted as compact
error generators; on MK-61 many of those codes became real functions. Compare
[12-danilov-secrets-mk61-delta.md](./12-danilov-secrets-mk61-delta.md).

## Reserving Space for Later Patches

Because memory is a tape, Danilov suggests leaving **`К НОП` padding** every
~10 commands in early drafts when step budget allows. During debugging you
replace a `К НОП` with a real command and retype only the local tail instead of
the entire program.

Trade-off: padding consumes scarce steps. Remove it in the final version unless
you expect field patches.

## Optimization Without Obscurity

Optimize in this order:

1. **Algorithm** — fewer operations beats clever opcodes.
2. **Formula** — Horner form, shared subexpressions, stable root formulas.
3. **Stack** — keep values in `Y`/`Z`/`T` instead of register traffic.
4. **Branches** — prefer `F L0`..`F L3` for decrement-and-jump.
5. **Indirect jumps** — `К БП R` when the target already lives in a register.
6. **Undocumented tricks** — only after the clear version works; see
   [02-undocumented-tricks.md](./02-undocumented-tricks.md).

Rules of thumb from the literature:

- Avoid `F x^y` when repeated multiply or square suffices.
- Move invariant computation **outside** loops even if the program grows slightly.
- Prefer counted loops (`F Lx`) over manual counter arithmetic when `R0`..`R3`
  are free.
- Keep subroutine tails shared instead of duplicating blocks.

Reading commands from higher addresses can be slightly slower on real hardware.
Lay out hot loops in lower addresses when every tenth of a second counts.

## Service for the Programmer

Tools and habits that speed development:

| Habit | Benefit |
| --- | --- |
| Register plan table | Stops `R3` from becoming three different things |
| Address table with stack notes | Catches extra stack lifts |
| Flowchart before coding | Counts branch cells before you run out of memory |
| Control example written first | Gives a finish line |
| Emulator watch on `X,Y,Z,T,PC` | Faster than hardware alone |

In this repo, `mk-pro explain` and emulator smoke tests complement manual stepping.

## Service for the User

A program someone else runs weekly should optimize for **clarity over golf**:

- Document initialization: which registers to fill before `В/О С/П`.
- Use `С/П` at natural pause points with a predictable display.
- Prefer explicit prompts (`0` = next dataset, `1` = quit) over cryptic codes.
- Do not strip stops that prevent typing into the wrong part of the cycle.
- Publish a one-screen "quick start" even if the listing is compact.

Danilov's alloy-calculation example is long precisely because most steps are
user service, not math. See [10-applied-examples.md](./10-applied-examples.md).

Only sacrifice user service when the program cannot fit otherwise.

## Compact Listings vs Standard Notation

Books and magazines compress listings:

- ten commands per row to save address columns,
- drop hex codes,
- merge `П->X` into `Пx3` style notation.

That saves paper but creates dialects. [05-reading-lordbss-games.md](./05-reading-lordbss-games.md)
documents one popular dialect. For exchange within this project, prefer the
notation in [03-command-reference.md](./03-command-reference.md) unless you also
ship a translation table.

## Suggested Debug Session

1. Clear registers and enter known inputs.
2. Type or load only the first branch of the program.
3. Run to the first `С/П`; verify `X`.
4. Extend to the first loop; single-step one iteration.
5. Complete the program; run the control example in automatic mode.
6. Retry the control example with exponential-form inputs.
7. Only then try size optimizations or undocumented tricks.

## Cross-References

- Cookbook checklist: [04-programming-cookbook.md](./04-programming-cookbook.md)
- Numeric test design: [08-numerical-errors.md](./08-numerical-errors.md)
- B3-34-era quirks: [12-danilov-secrets-mk61-delta.md](./12-danilov-secrets-mk61-delta.md)

## Sources

- Igor Danilov, *Secrets of the Programmable Microcalculator*, chapters 8 and 10
- [04-programming-cookbook.md](./04-programming-cookbook.md)
- [Anvarov command appendix](https://sergeanvarov.github.io/russian/mk61/uf/commands.html)
