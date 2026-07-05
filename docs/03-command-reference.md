# MK-61 Command Reference

This reference uses the traditional MK-61 notation:

- `F` is the yellow shift key.
- `К` is the blue shift key.
- `R` means one of `R0`..`R9`, `Ra`..`Re`; the `mk61s-mini-expand`
  profile also enables direct `Rf` through opcodes `4F`/`6F`.
- Hex code ranges are opcode values, not necessarily things that can be typed normally.
- Commands that take an address consume the following program step as that address.

## Core Opcodes

| Code | Input | Operation | Notes |
| --- | --- | --- | --- |
| `00`..`09` | `0`..`9` | Digit entry | Digit entry can continue across a `С/П` boundary. A second decimal separator is ignored. If the previous command was not digit/separator input or `В↑`, a new number normally lifts the stack. |
| `0A` | `.` | Decimal separator | In program mode it can restore `X2 -> X`. |
| `0B` | `/-/` | Change sign | Does not copy old `X` to `X1`. After digit entry, later digit entry can replace the signed value without stack lift. |
| `0C` | `ВП` | Exponent entry | Repeated `ВП` can add exponents. In program mode it has major X2-restoration behavior. If the mantissa is zero, the first mantissa digit may be changed to `1` or to a sign-derived digit. |
| `0D` | `Cx` | Clear `X` | Does not move the stack and does not copy old `X` to `X1`. The next digit entry sometimes does not lift the stack. |
| `0E` | `В↑` | Stack lift | If the next command is digit entry, entry starts in `X`; if it is memory recall or `F pi`, another lift can happen. |
| `0F` | `F Вx` | Full stack lift | Includes `X1`. This is the only documented command whose code contains `F` as a hex digit. |
| `10` | `+` | Add | Overflow is often noticed only when `X` is copied to `X2`, allowing intermediate "supernumbers". |
| `11` | `-` | Subtract | Same delayed-boundary behavior as addition. |
| `12` | `*` | Multiply | Same delayed-boundary behavior as addition. |
| `13` | `/` | Divide | Division by zero errors immediately; other boundary issues can be delayed until `X -> X2`. |
| `14` | `<->` | Swap `X` and `Y` |  |
| `15` | `F 10^x` | Power of 10 | Argument overflow is checked immediately. |
| `16` | `F e^x` | Exponential | Argument overflow is checked immediately. |
| `17` | `F lg` | Decimal logarithm | Requires `X > 0`. |
| `18` | `F ln` | Natural logarithm | Requires `X > 0`. |
| `19` | `F sin^-1` | Arcsine | Requires `abs(X) <= 1` in every angle mode; may produce non-normalized zero in degree/grad modes for `X = 0`. |
| `1A` | `F cos^-1` | Arccosine | Requires `abs(X) <= 1` in every angle mode; arccos of `1` can produce non-normalized zero in degree/grad modes. |
| `1B` | `F tg^-1` | Arctangent | Very large arguments approach the angular maximum for the selected angle mode. |
| `1C` | `F sin` | Sine | Argument range is checked immediately. |
| `1D` | `F cos` | Cosine | Argument range is checked immediately. |
| `1E` | `F tg` | Tangent | Argument range is checked immediately; singularities error. |
| `1F` | not normally entered | Empty operator | Undocumented/unenterable by normal keyboard input. |

## Scientific, Conversion, and Blue-Shift Commands

| Code | Input | Operation | Notes |
| --- | --- | --- | --- |
| `20` | `F pi` / `F π` | Push pi | Lifts the stack and copies old `X` to `X1`. It is unusual because it expands the stack while not being X2-affecting. |
| `21` | `F sqrt` | Square root | Requires `X >= 0`. |
| `22` | `F x^2` | Square | Boundary checking may be delayed until `X -> X2`. |
| `23` | `F 1/x` | Reciprocal | Division by zero errors immediately. |
| `24` | `F x^y` | Power | Requires `X > 0`, even when mathematics would allow a negative base. Unlike normal binary arithmetic, it does not drop the stack; `Y` remains available for repeated powers. |
| `25` | `F reverse` / `F ↻` | Reverse stack rotation |  |
| `26` | `К °->′` | Convert degrees/hours to whole-plus-minutes form | Fractional part `>= 0.6` errors. Hex digits in the integer part can survive. |
| `27` | `К -` | Error | Produces `ЕГГ0Г`. |
| `28` | `К *` | Error | Produces `ЕГГ0Г`. |
| `29` | `К /` | Error | Produces `ЕГГ0Г`. |
| `2A` | `К °->′"` | Convert to whole-plus-minutes-seconds form | Fractional part `>= 0.6` errors. |
| `2B`..`2E` | not normally entered | Error | Produces `ЕГГ0Г`. |
| `2F` | not normally entered | Empty operator | Undocumented/unenterable. |
| `30` | `К °<-′"` | Convert minutes-seconds form back | Very small fractional parts stop converting; integer hex digits may normalize in edge cases. |
| `31` | `К |x|` | Absolute value | Removes sign/sign-digit. |
| `32` | `К ЗН` | Sign | Negative zero reports as positive; zero ignores sign-digit. |
| `33` | `К °<-′` | Convert minutes form back | Same edge behavior as `30`. |
| `34` | `К [x]` | Integer part | Truncates toward zero, not mathematical floor. Integer hex values may normalize. |
| `35` | `К {x}` | Fractional part | Keeps sign on the fraction. For some negative integers it yields negative zero. |
| `36` | `К max` | Maximum | Does not drop the stack. Zero is treated as the greatest value in the documented note. |
| `37` | `К AND` / `К ∧` | Bitwise AND | Operates on normalized mantissa nibbles; result form is `8.HHHHHHH`. Blue binary operations do not drop the stack. |
| `38` | `К OR` / `К ∨` | Bitwise OR | Same logical-operation family as `37`. |
| `39` | `К XOR` / `К ⊕` | Bitwise XOR | Same logical-operation family as `37`. |
| `3A` | `К ИНВ` | Bitwise NOT | Same logical-operation family as `37`. |
| `3B` | `К СЧ` | Pseudo-random number | Does not generate `1`; can rarely generate `0`. The sequence can cycle after `К` operations. `К max` with zero in `Y` resets it to the initial sequence. |
| `3C` | not normally entered | Error | Produces `ЕГГ0Г`. |
| `3D` | not normally entered | Alias of `2A` | Same as `К °->′"`. |
| `3E` | not normally entered | Copy `Y` to `X`, `X -> X1` | Equivalent to `F ↻` then `В↑`, but not X2-affecting. |
| `3F` | not normally entered | Empty operator | Undocumented/unenterable. |

## Memory, Flow Control, and Tests

| Code | Input | Operation | Notes |
| --- | --- | --- | --- |
| `40`..`4E` | `X->П R` | Store `X` in `R0`..`Re` | Low hex digit selects the register. |
| `4F` | `X->П Rf` in `mk61s-mini-expand`; otherwise not normally entered | Store `X` in `Rf` under the expanded profile; default `mk61` profile treats it as `R0` alias behavior | Raw hex remains accepted, but symbolic `X->П f` requires `mk61s-mini-expand`. |
| `50` | `С/П` | Stop/start | If pressed during execution, the command counter points to the next command, even if normal flow would not. |
| `51` | `БП` | Unconditional jump | Consumes next step as address. |
| `52` | `В/О` | Return | Uses a 5-cell return stack. With an all-zero return stack, it is equivalent to `БП 01`. |
| `53` | `ПП` | Subroutine call | Consumes next step as address and pushes it to the return stack. |
| `54` | `К НОП` | No-op | Documented empty operator. |
| `55` | `К 1` | No-op | Empty operator. |
| `56` | `К 2` | No-op | Empty operator. |
| `57` | `F x!=0` | Conditional test | If condition is true, skip address and continue; if false, jump to the address. |
| `58` | `F L2` | Loop on `R2` | Decrement and jump if non-zero. |
| `59` | `F x>=0` | Conditional test | True means fall through; false means jump. |
| `5A` | `F L3` | Loop on `R3` | Decrement and jump if non-zero. |
| `5B` | `F L1` | Loop on `R1` | Decrement and jump if non-zero. |
| `5C` | `F x<0` | Conditional test | True means fall through; false means jump. |
| `5D` | `F L0` | Loop on `R0` | Decrement and jump if non-zero. |
| `5E` | `F x=0` | Conditional test | True means fall through; false means jump. |
| `5F` | not normally entered | Raw display-side transform | In this MK-61 emulator ROM it does not hang. It leaves internal `X` intact but changes the display/raw state, for example `X=5` shows `0,5000000000,0,`. |
| `60`..`6E` | `П->X R` | Recall `R0`..`Re` into `X` | Low hex digit selects the register. |
| `6F` | `П->X Rf` in `mk61s-mini-expand`; otherwise not normally entered | Recall `Rf` under the expanded profile; default `mk61` profile treats it as `R0` alias behavior | Raw hex remains accepted, but symbolic `П->X f` requires `mk61s-mini-expand`. |

## Indirect Command Blocks

For the ranges below, the low hex digit selects `R0`..`Re`. The corresponding
`*F` opcode is an undocumented alias using `R0`. Even under
`mk61s-mini-expand`, symbolic indirect raw commands such as `К БП f` or
`К П->X f` are invalid; `Rf` is modeled only for direct `X->П f` / `П->X f`.

| Code | Input | Operation | Notes |
| --- | --- | --- | --- |
| `70`..`7E` | `К x!=0 R` | Indirect conditional jump if `X != 0` | True skips the address behavior and continues; false jumps via transformed register value. Register modification occurs only when the indirect target is actually used. |
| `7F` | not normally entered | Same as `70`, using `R0` | Undocumented alias. |
| `80`..`8E` | `К БП R` | Indirect unconditional jump | Target comes from transformed register value. |
| `8F` | not normally entered | Same as `80`, using `R0` | Undocumented alias. |
| `90`..`9E` | `К x>=0 R` | Indirect conditional jump if `X >= 0` | Same conditional convention as direct tests. |
| `9F` | not normally entered | Same as `90`, using `R0` | Undocumented alias. |
| `A0`..`AE` | `К ПП R` | Indirect subroutine call | Target comes from transformed register value. |
| `AF` | not normally entered | Same as `A0`, using `R0` | Undocumented alias. |
| `B0`..`BE` | `К X->П R` | Indirect store | Store `X` into the register whose number is held in transformed `R`. |
| `BF` | not normally entered | Same as `B0`, using `R0` | Undocumented alias. |
| `C0`..`CE` | `К x<0 R` | Indirect conditional jump if `X < 0` | Same conditional convention as direct tests. |
| `CF` | not normally entered | Same as `C0`, using `R0` | Undocumented alias. |
| `D0`..`DE` | `К П->X R` | Indirect recall | Recall from the register whose number is held in transformed `R`. |
| `DF` | not normally entered | Same as `D0`, using `R0` | Undocumented alias. |
| `E0`..`EE` | `К x=0 R` | Indirect conditional jump if `X = 0` | Same conditional convention as direct tests. |
| `EF` | not normally entered | Same as `E0`, using `R0` | Undocumented alias. |
| `F0`..`FF` | not normally entered | Empty operators | Undocumented. Emulator probes found no difference from `К НОП` for simple `X` states and for `F*` followed by `.`, but individual bytes have distinct ROM words and should not be assumed identical in X2-heavy code. |

### Emulator-Verified Alias Notes

The undocumented alias opcodes are behavior aliases, not necessarily identical
ROM microprograms.

- `3D` behaves as `К °->′"` for checked inputs, but its ROM words differ from
  `2A`.
- In the default `mk61` profile, `4F` stores to `R0`, and `6F` recalls from `R0`.
  In `mk61s-mini-expand`, those bytes are modeled as direct `Rf` store/recall.
- The indirect `*F` aliases (`7F`, `8F`, `9F`, `AF`, `BF`, `CF`, `DF`, `EF`)
  behaved the same as the corresponding `*0` commands in probes, including
  `R0` transformation. They do **not** provide a free "use R0 without changing
  R0" shortcut.

## Error Side Effects

Commands that fail due to an invalid argument, range check, or explicit error code can still copy `X -> X1` before the error is displayed. Overflow can be subtler: the copy to `X1` may happen at the operation that created the overflow, while a later X2-affecting command only reveals it.

The explicit `ЕГГ0Г` opcodes (`27`, `28`, `29`, `2B`..`2E`, `3C`) are not useful for arithmetic, but they are useful as one-cell traps. Emulator probes show they stop with `ЕГГ0Г`, leave `X` intact, copy `X` to `X1`, and pause with the program counter at `addr + 2`. If the user resumes execution with `С/П`, the cell immediately after the trap is skipped. Their ROM words are not identical, so treat them as behavior-equivalent traps rather than byte-identical aliases.

Ordinary domain-checking commands can also be used as conditional traps when their success result is acceptable or unreachable. Examples include `F 1/x` for zero, `F sqrt` for negative values, `F lg` for nonpositive values, `F sin^-1` or `F cos^-1` for `abs(X) > 1`, `F 10^x` for large exponents, and `К °->′"` for fractional parts `>= 0.6`. The inverse sine/cosine error boundary is independent of the `Р`/`Г`/`ГРД` angle switch; only the successful angle result changes. `F tg^-1` is not a useful finite-input error trap because large values saturate toward the mode's angular maximum instead of raising `ЕГГ0Г`.

## Approximate Execution Times

Real devices vary by controller batch, but Anvarov gives these approximate timings:

| Time | Commands |
| --- | --- |
| `0.20 s` | Basic arithmetic, `F x^2`, `F pi`, `F 1/x`, integer `F 10^x`, integer/fraction/abs/max/logical operations, stack commands |
| `0.22 s` | `F sqrt` |
| `0.26 s` | Explicit number entry, memory recall, `F Вx`, `В↑` |
| `0.31 s` | `F x^y` for positive integer `X` and `Y` |
| `0.40 s` | `К СЧ`, `F L0`..`F L3` |
| `1.20 s` | `F 10^x`, `F e^x`, `F lg`, `F ln` |
| `1.50 s` | Trigonometric functions |
| `2.00 s` | General `F x^y` |

Reading commands from higher program addresses can be about 1-4% slower than reading from low addresses.

## Sources

- [Anvarov command appendix](https://sergeanvarov.github.io/russian/mk61/uf/commands.html)
- [Serge Anvarov: undocumented MK-61 features](https://sergeanvarov.github.io/russian/mk61/%D0%9D%D0%B5%D0%B4%D0%BE%D0%BA%D1%83%D0%BC%D0%B5%D0%BD%D1%82%D0%B8%D1%80%D0%BE%D0%B2%D0%B0%D0%BD%D0%BD%D1%8B%D0%B5%20%D0%B2%D0%BE%D0%B7%D0%BC%D0%BE%D0%B6%D0%BD%D0%BE%D1%81%D1%82%D0%B8%20%D0%9F%D0%9C%D0%9A%20%D0%9C%D0%9A-61.html)
- [Elektronika MK-61 operating manual scan](https://www.wass.net/manuals/Elektronika%20MK-61.pdf)
