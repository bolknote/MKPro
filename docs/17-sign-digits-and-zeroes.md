# Sign-Digits and Non-Normal Zeroes

The MK-61 normally uses a sign position and decimal mantissa digits. Because the
internal representation is nibble-based, undocumented operations can leave a
digit-like value where the sign should be, or preserve zero in a non-normal
form. These states are useful for display effects, compact flags, and X2
restoration tricks.

## Sign-Digits

A sign-digit is a visible digit in the sign position. It can be produced when a
carry enters the sign nibble or when an X2/indirect-addressing sequence combines
parts of different internal values.

Different commands interpret sign-digits differently:

- `К |x|` removes the sign/sign-digit and normalizes toward a positive-looking
  value.
- `К ЗН` treats zero specially and may ignore the sign-digit when the mantissa
  is zero.
- Some arithmetic sees the sign-digit as part of a larger abnormal number.
- Some display paths preserve the sign-digit even when later numeric operations
  would destroy it.

For programming, sign-digits are most useful as compact visual markers or as
intermediate state in `ВП` restoration, not as stable numeric values.

## Negative Zero

Several operations can produce a zero mantissa with a negative sign or a
negative-looking exponent. `К {x}` on negative integers is one common path:
small negative integers can yield negative zero rather than ordinary zero.

Negative zero can compare as both "zero" and "negative" depending on the
operation. This makes it useful for branch compression, but dangerous for
readable programs.

## Non-Normal Zero with Chosen Exponent

Hex arithmetic and trigonometric edge cases can create zeroes that keep leading
zeroes or unusual exponent/sign fields. X2-affecting commands often destroy
these by copying to `X2` and normalizing `X`; non-X2-affecting commands can pass
them through longer.

Practical uses:

- display effects;
- hidden flags carried through the stack or registers;
- carefully chosen inputs to `ВП`, `.`, and `/-/` restoration;
- constructing values used by indirect addressing.

## Cleanup

To return to ordinary numeric behavior:

- run an X2-affecting command that normalizes `X`;
- use `К |x|` if the sign/sign-digit is the problem;
- store and recall through a path known to normalize on the target sequence;
- avoid using abnormal zeroes across user-visible stops unless that is the
  intended display.

## Rules of Thumb

- Treat sign-digits and strange zeroes as volatile flags.
- Do not mix them with ordinary numerical algorithms.
- If a conditional depends on negative zero, test both step mode and continuous
  run.
- Document the exact producer sequence, not only the observed display.

## Sources

- [Undocumented MK-61 behavior](https://sergeanvarov.github.io/russian/mk61/%D0%9D%D0%B5%D0%B4%D0%BE%D0%BA%D1%83%D0%BC%D0%B5%D0%BD%D1%82%D0%B8%D1%80%D0%BE%D0%B2%D0%B0%D0%BD%D0%BD%D1%8B%D0%B5%20%D0%9F%D0%9C%D0%9A%20%D0%9C%D0%9A-61.html)
- [X2 display register](https://sergeanvarov.github.io/russian/mk61/uf/x2.html)
- [Hexadecimal arithmetic](https://sergeanvarov.github.io/russian/mk61/uf/hex.html)
