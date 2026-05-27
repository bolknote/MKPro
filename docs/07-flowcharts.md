# Block Diagrams: A Portrait of the Program

A flowchart is the usual bridge between a mathematical algorithm and a linear
MK-61 program. The calculator has no branches in memory layout: every command
occupies the next address. Flowcharts make the hidden structure visible before
you pay for it in program steps.

This note follows the method in Danilov's *Secrets of the Programmable
Microcalculator*, adapted for the MK-61's 105-step memory and extended command
set.

## When to Draw One

Draw a flowchart when any of these is true:

- The algorithm has more than one formula path.
- A loop body is longer than a few commands.
- You need to share code between branches.
- You are about to spend address cells on jumps and want to count them first.

For a straight formula with no tests, an address table in
[04-programming-cookbook.md](./04-programming-cookbook.md) is usually enough.

## The Four Shapes

| Shape | Meaning | MK-61 role |
| --- | --- | --- |
| Oval | Start / End | Usually implicit: run from `00`, stop with `С/П` |
| Parallelogram | Input / Output | `С/П` stops for user input; register recalls for output |
| Rectangle | Calculation | Arithmetic, recalls, stores, function keys |
| Diamond | Comparison | `F x=0`, `F x>=0`, `F x<0`, `F x!=0`, or loop tests |

Rules:

- Exactly one Start and one End block per diagram.
- Every path must eventually reach End.
- Diamonds always split into two outgoing paths.

## Simplify the Math First

Before drawing, rewrite formulas into a machine-friendly form:

- Factor repeated subexpressions into named temporaries.
- Prefer Horner form for polynomials.
- For quadratic equations `ax² + bx + c = 0`, use stable root formulas when
  `|b|` is large:

```text
d = b² - ac
x1 = (-b - sqrt(d)) / a
x2 = (-b + sqrt(d)) / a
```

After algebraic rearrangement:

```text
B = b / 2
d = B² - ac
x1 = (-B - sqrt(d)) / a
x2 = (-B + sqrt(d)) / a
```

The flowchart still branches on `a = 0`, on the sign of `d`, and on special
cases such as `b = 0` in the linear case.

## From Diamond to MK-61 Conditional

MK-61 conditionals use an inverted convention:

- If the condition is **true**, execution **falls through** to the next command.
- If the condition is **false**, execution **jumps** to the address in the
  following cell.

Example skeleton for `if X = 0 then A else B`:

```text
00  F x=0
01  addr_of_B        ; failure branch
02  ... code for A ...
nn  БП addr_after_B
...
addr_of_B:
    ... code for B ...
addr_after_B:
    С/П
```

See [04-programming-cookbook.md](./04-programming-cookbook.md) for more branch
templates.

## The "Rubber Band" Layout Trick

On paper, branches spread sideways. In program memory they must be stacked
vertically. Danilov's useful image:

- Think of branch lines as **stretchy bands**.
- The **unstretched** path out of a diamond is the fall-through (true) branch.
- The **stretched** path is the jump (false) branch.

Workflow:

1. Translate the true branch first, starting at the command after the address
   cell.
2. Leave the address cell empty until you know where the false branch begins.
3. At the end of the true branch, insert `БП` with another empty address cell
   to skip over the false branch unless this is the last branch.
4. Write the false branch next and fill in the reserved address.
5. Fill in `БП` skip targets after all branch lengths are known.

This is how a wide diagram becomes one numbered tape of commands.

## Worked Layout: Quadratic Equation

High-level flow:

```text
Start
  -> input a, b, c
  -> if a = 0 then linear case else quadratic case
Quadratic:
  -> compute d = B² - ac
  -> if d >= 0 then real roots else complex roots (or error stop)
  -> output roots
End
```

Register plan example:

| Register | Role |
| --- | --- |
| `R0` | `a` |
| `R1` | `b` |
| `R2` | `c` |
| `R3` | `B = b/2` |
| `R4` | `d` |
| `R5`, `R6` | roots or temporaries |

When translating:

| Flowchart block | MK-61 idea |
| --- | --- |
| `a = 0?` | `П->X 0`, test, jump to linear block |
| `d >= 0?` | compute `d`, `F x>=0`, jump to complex/error block |
| repeated `sqrt` and `/` | share a subroutine tail if both roots need it |
| output | `С/П` between roots if the user should copy each one |

Count address-consuming commands early: every `БП`, `ПП`, `F x=0`, and `F Ln`
costs **two** steps.

## Merging Blocks

Adjacent rectangles with no decision between them can be merged in the diagram
and implemented as one straight-line code sequence. This reduces visual clutter
and makes the eventual program shorter.

## Shared "Trunk" Code

If two branches need the same tail (for example `sqrt(d)` then `/ a`), place
that sequence:

- before the first split, if both branches need the same prepared value, or
- after the branches rejoin, if only the final formatting differs.

Subroutine calls (`ПП` / `В/О`) are the structured version of this idea; see
[01-programming.md](./01-programming.md).

## From Flowchart to Address Table

Final deliverable before typing into the calculator:

| Address | Keys | Code | Note |
| --- | --- | --- | --- |
| `00` | ... | ... | |
| `10` | `F x=0` | `5E` | true: fall through |
| `11` | `40` | `40` | false: jump to `40` |

Mark every reserved address cell. Patch them only after all branch bodies are
sized.

## Common Mistakes

- Treating the address after a conditional as "dead data" rather than a possible
  entry point for another path.
- Forgetting that each `П->X R` lifts the stack.
- Placing the failure address on the wrong side of the diamond.
- Drawing a loop in the diagram but implementing it with scattered `БП`
  instead of `F L0`..`F L3` when a counter lives in `R0`..`R3`.

## Relation to Other Docs

- Branch templates: [04-programming-cookbook.md](./04-programming-cookbook.md)
- Conditional opcode details: [03-command-reference.md](./03-command-reference.md)
- Full applied quadratic walkthrough context: [10-applied-examples.md](./10-applied-examples.md)

## Sources

- Igor Danilov, *Secrets of the Programmable Microcalculator*, chapter 5
- [Antowka: programming the Elektronika MK61](https://antowka.ru/programming-eletronika-mk61/)
