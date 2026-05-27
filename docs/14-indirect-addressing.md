# Indirect Addressing Edge Cases

Official MK-61 programming treats an indirect register as holding an integer
address or register number. The real machine first transforms the stored number,
then uses the last transformed digits. This is why indirect commands are useful
not only for computed jumps, but also for compact state machines.

## Address vs Register Target

The same transformation feeds two families:

- indirect flow: `К БП R`, `К ПП R`, `К x?0 R`;
- indirect memory: `К X->П R`, `К П->X R`.

For flow commands the last two transformed decimal/hex-like digits select a
program address. For memory commands the last transformed digit selects
`R0`..`Re`; values beyond `e` wrap through the same nibble logic rather than
being rejected cleanly.

## Integer Values

For ordinary non-negative integers, pad on the left and use the trailing digits:

| Register value | Flow target | Memory target |
| --- | --- | --- |
| `4` | `04` | `R4` |
| `10` | `10` | `R0` |
| `99` | `99` | `R9` |
| `123` | `23` | `R3` |

This is the safe subset. It is enough for normal jump tables and register
tables.

## `R0`..`R3`: Decrement Before Use

When the selector register is `R0`, `R1`, `R2`, or `R3`, indirect addressing
decrements the transformed mantissa before use. This gives compact counters and
"pre-decrement then branch" patterns.

Useful verified examples for `R0`:

| Initial `R0` | Command | Effective result |
| --- | --- | --- |
| `2` | `К БП 0` | target `01`, then `R0 = 00000001` |
| `10` | `К БП 0` | target `09`, then `R0 = 00000009` |
| `99` | `К БП 0` | target `98`, then `R0 = 00000098` |
| `0,1`..`0,9` | `К БП 0` | target `99`, then `R0 = -99999999` |
| `0,1`..`0,9` | `К П->X 0` | recalls `R3`, then `R0 = -99999999` |
| `0,1`..`0,9` | `К X->П 0` | stores to `R3`, then `R0 = -99999999` |

The fractional case is not a shorter spelling of `R3`; direct `П->X 3` is also
one step. Its value is that one `R0` state can mean "select `R3`" and leave a
sentinel (`-99999999`) for the next part of the program.

## `R4`..`R6`: Increment Before Use

When the selector register is `R4`, `R5`, or `R6`, the mantissa is transformed
in the opposite direction: it is incremented before use. This is useful for
compact "next item" scans and for producing normalized/unnormalized edge
values, especially when the mantissa contains hex-like nibbles.

Use these registers when you want post-increment table traversal. Use
`R0`..`R3` when you want pre-decrement loop behavior.

## `R7`..`Re`: No Counter Side Effect

For `R7`..`Re`, the selector is used without the `R0`..`R6` counter update.
These registers are best for stable computed addresses.

This is especially useful with formal addresses containing hex digits. For
example, if `R7` contains `FA`..`FF`, then `К БП 7` enters the super-dark branch
family without consuming a following address byte. That leaves addresses
`01`..`06` free for continuation code.

## Fractional, Negative, and Hex-Like Values

The machine does not require a clean integer:

- positive fractions can underflow into `99`-like cases through decrementing;
- negative values are padded with nines, so `-1` behaves like a high trailing
  value rather than like a rejected argument;
- hex-looking mantissa digits survive in many addressing paths and can select
  dark or super-dark formal addresses.

These cases are powerful but hard to debug. Prefer them only when the state was
already needed for something else.

## Practical Patterns

- **One-step computed jump:** replace repeated `БП aa` with `К БП R` when `R`
  already contains the address.
- **Loop counter as address:** use `R0`..`R3` when the decrement is useful.
- **Stable dispatch register:** use `R7`..`Re` when the address must survive.
- **Sentinel case:** fractional `R0 < 1` can mean "case 99" and leave
  `-99999999` for later tests.
- **Super-dark dispatch:** store `FA`..`FF` in `R7`..`Re` and branch indirectly
  to get six one-command cases with continuations at `01`..`06`.

## Sources

- [Program address space](https://sergeanvarov.github.io/russian/mk61/uf/addr_space.html)
- [Undocumented MK-61 behavior](https://sergeanvarov.github.io/russian/mk61/%D0%9D%D0%B5%D0%B4%D0%BE%D0%BA%D1%83%D0%BC%D0%B5%D0%BD%D1%82%D0%B8%D1%80%D0%BE%D0%B2%D0%B0%D0%BD%D0%BD%D1%8B%D0%B5%20%D0%B2%D0%BE%D0%B7%D0%BC%D0%BE%D0%B6%D0%BD%D0%BE%D1%81%D1%82%D0%B8%20%D0%9F%D0%9C%D0%9A%20%D0%9C%D0%9A-61.html)
