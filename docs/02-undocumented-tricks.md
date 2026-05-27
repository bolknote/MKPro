# MK-61 Tricks, Undocumented Commands, and Memory-Saving Hacks

The MK-61 has only 105 official program steps, so serious programs often depend on layout tricks. Some optimizations are purely disciplined RPN programming; others use undocumented behavior left by the calculator's microcode.

Use these techniques carefully. They are historically important and useful for emulator-compatible programs, but they can make code hard to read and hard to repair.

## Cheap Wins

### Use Indirect Flow Control

Direct flow-control commands take two steps: the command and the address. Indirect commands often take one step because the target is stored in a register:

| Direct | Indirect |
| --- | --- |
| `БП aa` | `К БП R` |
| `ПП aa` | `К ПП R` |
| `F x=0 aa` | `К x=0 R` |

This saves memory when the same target is used many times, when the target is computed, or when a register already contains a useful address-like value.

### Schedule the Stack

The cheapest storage is the stack. If a value will be needed several times, compute it early and arrange later operations so it drifts into `T`, then back down as binary operations drop the stack. This can remove repeated `П->X R` recalls or even remove the need for a memory register.

### Use `В/О` as a One-Step Jump

With an empty return stack, `В/О` behaves like a jump to address `01`. That makes it a one-step replacement for `БП 01`. More elaborate layouts put setup code, a subroutine body, and the main loop around this fact so one subroutine call can be removed.

### Overlay an Address and an Instruction

The address byte after a jump is also an opcode if execution reaches it from another path. For example, an address such as `4D` can simultaneously mean a jump target and the opcode `x->П d`. Program fragments can be rearranged so a branch operand is useful code for another path.

### Replace Branches with Arithmetic

A condition plus address costs two steps. Sometimes a mathematical operation can encode the same decision. For example, if `X` is known to be either zero or positive and you need to increment a counter only for non-zero `X`, `К ЗН` produces a sign-like value that can be added to the counter without a conditional branch.

For signed input, add `К |x|` after `К ЗН` to get a non-negative increment.

### Use `F Lx` Outside Normal Loops

The loop commands `F L0`..`F L3` decrement registers and branch. They can be used as compact decrement-and-jump instructions even when the surrounding code is not a classical loop. A common pattern is to use an intentionally non-expiring counter or to check the end condition elsewhere.

### Stop by Error

If an invalid argument is acceptable as a stop condition, an error can replace a conditional branch:

| Stop condition | Trigger |
| --- | --- |
| `X = 0` | `F 1/x` |
| `X <= 0` | `F lg` |
| `X < 0` | `F sqrt` |
| `X > 1` | `F sin^-1` or `F cos^-1` |
| `X >= 100` | `F 10^x` |
| fractional angle part `>= 0.6` | `К °->′"` |

This is a code-golf technique. It is compact, but it turns a program condition into a runtime error state.

### Call the Tail of a Subroutine

If two paths need almost the same processing, enter a subroutine in the middle instead of calling it twice. Anvarov gives an example where a number is split into integer and fractional parts, one path drops into a shared tail, and one explicit subroutine call is saved.

### Merge Constants and Addresses

If a numeric constant also contains useful trailing digits, it can double as an indirect address. This is especially useful because indirect addressing uses the last two transformed mantissa digits as the register number or program address.

## Undocumented Address Space

Official program memory has 105 cells: `00`..`99`, `A0`..`A4`.

Undocumented execution can reach aliases:

| Formal addresses | Effective behavior |
| --- | --- |
| `A5`..`B1` | Side branch mapping to `00`..`06` |
| `B2`..`F9` | Longer side branch mapping to `00`..`47` |
| `C0`..`F9` | "Dark" addresses: existing code is executed but not shown normally |
| `FA`..`FF` | "Super-dark" one-command branches, then return to an auxiliary address |

This allows unusual control flow and lets one physical command cell be reached through several formal addresses. For double commands such as `БП aa`, crossing an alias boundary can cause the address operand to be read from a different physical cell than expected. See [14-indirect-addressing.md](./14-indirect-addressing.md) for the detailed indirect-addressing rules.

### Use Super-Dark Addresses as Tiny Dispatch Cases

Emulator verification confirms Anvarov's map for the super-dark branch family:

| Formal address | Executes one command at | Then continues at |
| --- | --- | --- |
| `FA` | `48` | `01` |
| `FB` | `49` | `02` |
| `FC` | `50` | `03` |
| `FD` | `51` | `04` |
| `FE` | `52` | `05` |
| `FF` | `53` | `06` |

This is most useful through an **indirect** branch such as `К БП 7`, because
there is no following address byte to occupy `01`..`06`. If `R7` contains a
hex-looking formal address (`FA`..`FF`), the program executes exactly one
command at `48`..`53`, then drops into a compact shared tail at `01`..`06`.

For games, this gives six one-command cases plus six nearby continuation
points with a one-step dispatcher. The command after the super-dark physical
cell is not executed linearly; `FB`, for example, executes address `49`, skips
`50`, and resumes at `02`.

## Indirect Addressing Transformations

Officially, an indirect address register should contain a non-negative integer. In practice, almost any number is accepted and transformed first. The last two digits of the transformed mantissa select the program address or memory register.

Important rules:

- Non-negative integers are padded on the left with zeroes; their last two digits are the address.
- A fractional part is often discarded by the padding transformation, so an explicit integer-part command can be skipped.
- Negative values are padded with nines instead of zeroes.
- For `R0`..`R3`, the transformed mantissa is decremented before use.
- For `R4`..`R6`, the transformed mantissa is incremented before use.
- `F L0`..`F L3` use the decrement rules; if the transformed mantissa becomes zero, the loop finishes and the original register value may be preserved.
- Hexadecimal-looking digits in the mantissa survive or normalize depending on whether the operation decrements or increments.

These rules are the foundation for many compact counters, computed jumps, and screen effects.

Verified compact edge cases for `R0`:

- `К БП 0` with `R0 = 0,1`, `0,5`, `0,9`, or even `1` branches to address
  `99` and leaves `R0 = -99999999`. If that state is already available, it is
  a one-command jump to `99` with a sentinel value left behind.
- `К П->X 0` with fractional positive `R0 < 1` recalls `R3` and changes
  `R0` to `-99999999`.
- `К X->П 0` with fractional positive `R0 < 1` stores to `R3` and changes
  `R0` to `-99999999`.

The last two tricks do not beat direct `П->X 3` or `X->П 3` by themselves;
their value is in computed dispatch tables where the same `R0` state can mean
"use `R3`" and simultaneously mark that the fractional/sentinel case happened.

## The Hidden `X2` Display Register

Anvarov distinguishes `X` from a display-related register called `X2`. In normal calculation mode they are kept in sync. In program execution, some commands copy `X` to `X2` and normalize, while others operate only on `X`.

Useful categories:

- X2-affecting commands copy `X -> X2` and normalize `X`.
- Non-X2-affecting commands can leave `X` changed while the display-side value is still old or non-normalized.
- X2-restoring commands, especially `.`, `ВП`, `/-/`, and digit entry in certain contexts, can copy `X2 -> X` instead.

Consequences:

- `.` can restore the old `X2` value in program mode without moving the stack.
- `ВП` can restore `X2` while replacing or incrementing the first mantissa digit under specific conditions.
- `ВП` immediately after an indirect jump has special behavior: the first digit can become `7`.
- `ВП .` and `ВП /-/` are not just exponent-entry combinations; in programs they can be used as X2-restoration tools, sometimes producing errors after useful side effects.

This is one of the deepest MK-61 hack areas. It lets programs combine parts of different numbers, create non-normalized values, and sometimes avoid extra storage. See [15-x2-display-register.md](./15-x2-display-register.md) for programming patterns.

## Hex Digits, Sign-Digits, and Strange Zeroes

Because numeric storage uses 4-bit nibbles, values can contain digit-like symbols beyond `9`. The calculator's normal functions do not all treat them the same way.

Known effects include:

- Arithmetic with hexadecimal mantissa digits follows partial hex-like rules, then may normalize back to decimal-looking values.
- Logical operations `К AND`, `К OR`, `К XOR`, and `К ИНВ` operate on mantissa nibbles and return values shaped like `8.HHHHHHH`.
- Carry into the sign nibble can create a "sign-digit", a visible digit where a sign would normally be.
- Some operations treat sign-digit numbers as positive, negative, or zero depending on the operation, so they are useful but fragile.
- Special numbers with visually negative zero exponents can exist in more than one internal form. Many X2-affecting operations immediately destroy them by normalization.

These tricks are usually for extreme memory savings, display effects, or classic
"eggolology" programs rather than everyday numerical work. See
[16-hexadecimal-arithmetic.md](./16-hexadecimal-arithmetic.md) and
[17-sign-digits-and-zeroes.md](./17-sign-digits-and-zeroes.md) for the detailed
notes.

## Undocumented `F*` Opcodes

Most `F0`..`FF` opcodes cannot be entered normally. Anvarov documents several ways to insert them indirectly:

- Certain `В/О К ПП R` sequences can put `F0`..`FE` around addresses `30`..`44`.
- A constructed `ЕГГ0Г` value followed by `ВП D1 D2 . 0` can insert `F{D2}` at `5{D1}`.
- With a prepared return stack, several `FF` commands can be placed at alternating addresses.

On the MK-61 emulator ROM used by this project, `F0`..`FF` behave like `К НОП`
in simple one-command probes (`X = 5`, `0`, `-1`, `1,5`) and also when followed
by `.`. Treat them mainly as fill/overlay bytes unless a specific surrounding
X2 setup proves an effect.

The `5F` opcode is not a non-terminating hang in this ROM. It leaves internal
`X` intact but changes the displayed/raw value; examples:

| Initial `X` | Display after `5F` |
| --- | --- |
| `0` | `0,0000000000,0,` |
| `1` | `0,1000000000,0,` |
| `5` | `0,5000000000,0,` |
| `1,5` | `0,1500000000,0,` |
| `-1` | `9,1000000000,0,` |

This can be useful only as a display/raw-state trick. Do not use it as a
portable harmless no-op.

## Rule of Thumb

Use documented optimizations first: stack scheduling, indirect jumps, compact loops, and address overlays. Reach for X2, dark addresses, sign-digits, or `F*` insertion only when the program genuinely cannot fit otherwise.

## Sources

- [Serge Anvarov: undocumented MK-61 features](https://sergeanvarov.github.io/russian/mk61/%D0%9D%D0%B5%D0%B4%D0%BE%D0%BA%D1%83%D0%BC%D0%B5%D0%BD%D1%82%D0%B8%D1%80%D0%BE%D0%B2%D0%B0%D0%BD%D0%BD%D1%8B%D0%B5%20%D0%B2%D0%BE%D0%B7%D0%BC%D0%BE%D0%B6%D0%BD%D0%BE%D1%81%D1%82%D0%B8%20%D0%9F%D0%9C%D0%9A%20%D0%9C%D0%9A-61.html)
- [Anvarov: program address space](https://sergeanvarov.github.io/russian/mk61/uf/addr_space.html)
- [Anvarov command appendix](https://sergeanvarov.github.io/russian/mk61/uf/commands.html)
- [Habr: MK-61 history, emulation, and device internals](https://habr.com/ru/articles/505612/)
