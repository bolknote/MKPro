# Applied Examples and Problem-Solving Method

This document captures the "when, why, and how" material from Danilov's early
chapters and the production example in chapter 11, oriented toward the MK-61.
Use it together with [07-flowcharts.md](./07-flowcharts.md) and
[09-debugging-and-service.md](./09-debugging-and-service.md).

## Which Tasks Belong on a PMK?

Danilov frames two extremes:

- "No serious task fits on a microcalculator."
- "Any task fits if you have no better machine."

The practical middle:

| Good PMK fit | Poor PMK fit |
| --- | --- |
| A few formulas repeated many times | Huge linear systems |
| Field calculations at the bench | Weather models |
| Personal tools you run all day | Jobs that need a day of queue time on a mainframe |
| Teaching algorithms | Massive statistics |
| Games and simulations under 105 steps | Large matrix eigenproblems |

The MK-61 wins when **total time to result** matters more than raw speed:
no login queue, no batch slot, always on the desk or in the pocket.

## Eight Stages of a Machine Solution

Use this checklist for every non-trivial program:

1. **Problem statement** — inputs, outputs, units, edge cases.
2. **Mathematical model** — equations, domains, constraints.
3. **Method** — exact vs iterative, stable vs naive formula.
4. **Algorithm** — ordered steps before memory layout.
5. **Flowchart** — branch and loop topology; see [07-flowcharts.md](./07-flowcharts.md).
6. **Program** — address table and listing.
7. **Debugging** — control examples, `ЕГГОГ` analysis; see [09-debugging-and-service.md](./09-debugging-and-service.md).
8. **Operation** — quick-start card for the user.

Skipping stage 5 is acceptable only when the program is straight-line math.

## Example A: Block on a Horizontal Plane

**Problem.** A block of mass `m = 350 g` slides on a horizontal plane under force
`F` at angle `α`. Acceleration `a = 0.3 m/s²`, friction coefficient `k = 0.11`,
`g = 9.8 m/s²`. Find tension `T` and normal force `N` as functions of angle.

**Why program it?** A single angle is a manual calculation. A family of angles is
a loop.

**Model.**

```text
T cos α - k(mg - N) = ma   (along motion)
N = mg - T sin α           (perpendicular)
```

Solve for `T` and `N` given `α`.

**Register plan.**

| Register | Meaning |
| --- | --- |
| `R0` | angle `α` (degrees if switch is in degree mode) |
| `R1` | `m` |
| `R2` | `a` |
| `R3` | `k` |
| `R4` | `g` |
| `R5` | `T` |
| `R6` | `N` |

**Structure.**

```text
initialize constants once
loop:
  compute trig functions of α
  evaluate T
  evaluate N
  stop or show (С/П) for recording
  advance α or decrement counter (F L0)
```

This is the archetype Danilov uses when moving from manual mode (chapter 1) to
the first real program (chapter 3).

## Example B: Fahrenheit to Celsius

Already in [01-programming.md](./01-programming.md). It illustrates stages 2–6
without branches: formula → registers → straight listing.

## Example C: Quadratic Equation

Use the flowchart method in [07-flowcharts.md](./07-flowcharts.md):

- branch on `a = 0`,
- linear path for `ax + c = 0`,
- quadratic path with `d = (b/2)² - ac`,
- branch on sign of `d`,
- output one or two roots with `С/П` between them if the user copies results by
  hand.

Count steps early: a fully general solver with complex roots and full error
messages may not fit without indirect jumps or shared tails.

## Example D: Steel Alloy Adjustment (Production)

**Setting.** During a melt, the lab reports current percentages `A1, A2, …` of
alloying elements. The target composition is `B1, B2, …`. Ferroalloy additives
contain percentages `C1, C2, …` of each element. Find masses `M1, M2, …` of
each additive to add to bath mass `M0`.

**Why a PMK?** The foreman needs an answer at the furnace, not tomorrow from a
computing center.

**Structure of the solution.**

1. Read current analysis and targets into registers.
2. For each element, compute required delta.
3. Convert delta into additive mass using `C` percentages.
4. Sequence `С/П` stops so the operator can write each result.
5. Outer loop restarts for the next sample after `В/О`.

**Service dominates size.** Danilov's published alloy program is large because it:

- prompts with flags on the display (`0`, `1`, …),
- validates input ranges,
- stores results in consecutive registers for sequential recall,
- documents a control example on the page.

That is intentional: **user time** matters more than **step count** unless memory
overflow forces compromise.

**Control example pattern.**

```text
Input: 5 С/П 20 С/П 25 С/П ...
Check: R0 = 10, R1 = 40, R2 = 50 after the documented sequence
```

Always ship the control example with the listing.

## Example E: Percent Mixture Loop

Many lab tasks reduce to:

```text
result_i = f(raw_j, constants_k)
```

If `f` fits in stack operations, keep constants in high registers (`R9`..`Re`)
and stream samples through `R0`..`R3`. Use `F L0` when the same count repeats.

## Mapping B3-34 Examples to MK-61

Danilov's book uses the B3-34 as reference (98 steps, no `К [x]`, `К {x}`,
`К |x|`, `К max`, logical ops, or built-in `К СЧ`). When porting:

| B3-34 habit | MK-61 replacement |
| --- | --- |
| Integer part via `К ИП R` trick | `К [x]` |
| Fractional part hacks | `К {x}` |
| Home-grown random numbers | `К СЧ` when quality suffices |
| 98-step budget | 105 steps — small extra room, same discipline |

Always re-count steps and re-run control examples after porting.

## Documentation You Should Publish With a Program

Minimum package:

```text
Title and purpose
Register initialization table
Quick start (keys to press)
Address listing or hex dump
Control example with expected registers/display
Known domain limits (angles, positive mass, etc.)
```

For games, add display code meanings; see [11-game-design.md](./11-game-design.md)
and [05-reading-lordbss-games.md](./05-reading-lordbss-games.md).

## Sources

- Igor Danilov, *Secrets of the Programmable Microcalculator*, chapters 2, 3, and 11
- [01-programming.md](./01-programming.md)
- [07-flowcharts.md](./07-flowcharts.md)
