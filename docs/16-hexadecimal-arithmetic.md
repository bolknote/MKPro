# Hexadecimal Arithmetic

MK-61 numeric storage uses 4-bit nibbles. Undocumented paths can leave
mantissa digits `A`..`E` in a number. Ordinary arithmetic then treats those
nibbles partly as hexadecimal digits and partly as normalized decimal output.

This is not a general-purpose number system. It is a compact way to encode bit
masks, display states, and branch-test artifacts.

## How to Think About `H`

Let `H` be one hex-like mantissa digit: `A`, `B`, `C`, `D`, or `E`.

Important rough rules:

- addition and subtraction often operate modulo a nibble and then normalize;
- multiplication by `A`..`D` often behaves like multiplying by `10`-like
  coefficients in the affected digit position;
- `E` is special in several tables and often collapses to zero-like results;
- division can produce non-normalized fractions, odd zero forms, or persistent
  error states.

For exact values, build a small emulator table for the concrete operands. The
tables are too irregular to safely derive by hand in dense programs.

## Practical Uses

### Bit Masks with Blue Logical Operations

The reliable high-level use of hex digits is as packed bit masks:

- `К AND` / `К ∧`
- `К OR` / `К ∨`
- `К XOR` / `К ⊕`
- `К ИНВ`

The result is shaped like `8.HHHHHHH`, and the blue binary operations do not
drop the stack. This is why games can store a whole floor plan or resource map
in one register.

### Detecting Out-of-Range Bit Shifts

Some multiplication cases reveal whether a bit-like fractional value stayed in
range. Multiplying by a chosen hex digit can produce a one-digit result for a
valid state and a two-digit result for an overflow-like state. This is useful
when moving a player marker in a packed maze.

### Creating Non-Normalized Zeroes

Subtracting a hex value from its decimal-like analogue can create zero with a
chosen exponent or with leading zero nibbles preserved. These zeroes are useful
mainly as inputs to X2 and sign-digit tricks.

### Generating Hex Digits

Common construction paths:

- logical operations on masks;
- indirect addressing side effects;
- error/display sequences that leave `ЕГГ0Г`-like values as data;
- `К ИНВ`, `К {x}`, and related normalization paths.

## Dangerous Cases

Division by or with hex-like digits can create a "bad error" state: after it,
many ordinary arithmetic and yellow functions keep producing `ЕГГ0Г` until the
machine is reset or carefully cleaned. Blue functions may still work. Avoid
using division tables as a code-golf primitive unless you have tested the exact
sequence.

The optimizer only models arithmetic pairs pinned by emulator tests. That
includes selected `A`..`E` divided by decimal or hex digits, selected reverse
decimal/hex cases such as `9 / B -> 0,4444443-01`, strict `A`..`E E-2`
exponent `+`/`-` pairs for decimal operands `0`..`18`, and
operand-order-specific `A`..`E E-2` exponent `*`/`/` pairs such as
`AE-2 * 1 -> 0E-2`, `AE-2 * 15 -> 9,9`,
`BE-2 / 18 -> 6,1111111-03`, `12 / ГE-2 -> 000`, and
`18 / BE-2 -> 943,43434`. Pairs that produce `ЕГГ0Г` remain opaque.

Hex-like values in exponent digits are a separate indirect-addressing topic.
Do not assume mantissa rules apply to exponent nibbles.

## Programming Advice

- Prefer blue logical operations for real applications.
- Use hex arithmetic tables only for fixed, tested constants.
- Avoid first digit `F`; it is generally more dangerous than `A`..`E`.
- If a trick depends on hex arithmetic, include a control example in the
  program notes.

## Sources

- [Hexadecimal arithmetic](https://sergeanvarov.github.io/russian/mk61/uf/hex.html)
- [Command appendix](https://sergeanvarov.github.io/russian/mk61/uf/commands.html)
