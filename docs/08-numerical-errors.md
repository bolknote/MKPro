# Computational Errors on the MK-61

The MK-61 displays eight mantissa digits and a two-digit exponent. That is
enough for school and field calculations, but not enough to treat every displayed
result as exact. This note explains how to reason about error on a programmable
microcalculator (PMK) and how to design control examples that expose bad
assumptions.

## Error vs Mistake

In everyday speech, "error" often means a blunder. In numerical work, **error**
usually means the gap between a true value and the rounded value the machine
stores or shows.

A result can be:

- **Wrong** — the method or program is incorrect.
- **Correct but imprecise** — the method is right, but rounding limits accuracy.
- **Correct within a stated tolerance** — the usual goal for PMK programs.

The MK-61 manual gives a nominal range of `1e-99 <= abs(x) <= 9.9999999e99` with
eight decimal mantissa digits. Internal storage uses packed decimal-like nibbles;
see [00-hardware.md](./00-hardware.md).

## Absolute and Relative Error

**Absolute error** is the magnitude of the difference between the true value and
the approximation:

```text
Δ = |x_true - x_approx|
```

**Relative error** compares that gap to the size of the quantity itself:

```text
δ = Δ / |x_true|
```

Example:

- Distance Moscow–Leningrad quoted as 650 km is uncertain by about ±0.5 km:
  relative error ≈ 0.5/650 ≈ 1/1300.
- A page height measured as 198 mm with ±0.5 mm uncertainty has a smaller
  absolute error but a larger relative error: 0.5/198 ≈ 1/396.

Use absolute error when the tolerance is fixed (±1 unit, ±0.01 in the last
digit). Use relative error when the value spans many orders of magnitude.

## Sources of Error on a PMK

| Source | Effect |
| --- | --- |
| Input rounding | User enters fewer digits than the true constant |
| Finite mantissa | Each result keeps only eight significant decimal digits |
| Exponent limits | Underflow toward 0, overflow toward `ЕГГОГ` |
| Non-associativity | `(a + b) + c` may differ from `a + (b + c)` in extreme cases |
| Transcendental functions | `sin`, `ln`, `x^y` use internal approximations |
| Catastrophic cancellation | Subtracting nearly equal numbers destroys leading digits |

Measured inputs already carry uncertainty. Even "exact" constants such as `32` or
`1.8` are exact only when they are representable without surprise in the current
path through the machine.

## Typical MK-61 Behavior

Documented expectations:

- Basic arithmetic and many stack operations round to the displayed precision.
- `F x^y` is slow and often less accurate than multiplying by repeated factors
  when the exponent is a small integer.
- `F 10^x`, `F e^x`, logs, and trig functions take longer and introduce their
  own truncation models.
- Overflow may not appear immediately on some operations; boundary effects can
  surface only when a later command copies `X` to the display path. See
  [03-command-reference.md](./03-command-reference.md).

Danilov recommends avoiding `F x^y` when a cheaper equivalent exists, both for
speed and for accuracy.

## Accumulated Error in Programs

A program is a pipeline of approximations. Error compounds when:

- A loop adds many small terms to a large running sum.
- A formula subtracts two large, nearly equal results.
- A root finder or iteration stops on a flat part of the curve.
- Intermediate values cross exponent boundaries repeatedly.

Mitigations that fit 105 steps:

- Reformulate to avoid subtraction of close values (classic example: quadratic
  formula via `B = b/2`).
- Scale inputs so intermediate values stay near 1 when possible.
- Use Horner form instead of explicit powers.
- For sums, add smaller terms first if the dynamic range allows.
- Stop loops on a tolerance in `X`, not on a magic iteration count alone.

## Designing Control Examples

Danilov's debugging advice applies directly to numerical testing:

1. Run a **normal** case with hand-checkable numbers.
2. Run **edge** cases: zero, sign change, very small, very large, exponent form.
3. Run the same case in **different input shapes** — fixed decimal vs
   exponential entry (`1.10^-3` vs `0.001`).
4. Compare against a known reference (table, manual value, or desktop
   calculator with higher precision).
5. Do not trust **step mode alone**; automatic and step-by-step execution can
   diverge on some paths. See [12-danilov-secrets-mk61-delta.md](./12-danilov-secrets-mk61-delta.md).

### Example: Sine by Series vs Built-in `F sin`

Suppose a program approximates `sin x` by a Taylor partial sum. For `x = 0.1`:

- Built-in `F sin` on the PMK gives about `9.9833417e-2`.
- A truncated series might give `9.9833413e-2`.

That difference is expected. The question is whether it exceeds your application's
tolerance. For a game or a steel-alloy percentage, it may not matter. For a
convergence demonstration, it matters a great deal.

Always state the acceptable absolute or relative error when publishing a
program.

## Representational Surprises

These are not rounding in the classical sense, but they affect numerical
reasoning:

- Negative zero and non-normalized zeros appear in some modes and undocumented
  paths; many operations normalize them away.
- Hexadecimal-looking mantissa digits and sign-digit artifacts behave unlike
  ordinary decimal numbers. See [02-undocumented-tricks.md](./02-undocumented-tricks.md).
- Indirect addressing transforms mantissa digits before use; fractional parts
  may disappear in ways that look like intentional truncation.

Do not use those features in programs that must behave like plain decimal math
unless you have verified every path.

## Practical Checklist

Before calling a numeric program finished:

- [ ] State which registers hold inputs and which hold outputs.
- [ ] Pick one control case you can compute by hand.
- [ ] Pick one case with exponent-form input.
- [ ] Pick one case near overflow/underflow if the domain allows it.
- [ ] If the program loops, check both the first iteration and the last.
- [ ] If you use `F x^y`, compare against an equivalent sequence without it.
- [ ] Record the acceptable error band in comments or companion notes.

## Sources

- Igor Danilov, *Secrets of the Programmable Microcalculator*, chapter 9
- [Elektronika MK-61 operating manual scan](https://www.wass.net/manuals/Elektronika%20MK-61.pdf)
- [03-command-reference.md](./03-command-reference.md)
