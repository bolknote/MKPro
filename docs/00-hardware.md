# Elektronika MK-61: Hardware Internals and Characteristics

The Elektronika MK-61 is a Soviet programmable scientific calculator released in 1984. It belongs to the B3-34-compatible family, but extends the earlier machines with a third processor, more program memory, one more data register, and several additional scientific and service functions. It is closest in programming model to the MK-52, except that the MK-61 has volatile program memory rather than the MK-52's non-volatile storage.

## User-Visible Architecture

The calculator is an RPN machine, and its register-like state is easiest to read
in groups. The official "memory register" count refers only to the 15
addressable user registers `R0`..`Re`; stack registers and display-side state
are separate parts of the programming model.

### Operational Stack

| Register | Purpose |
| --- | --- |
| `X` | Top of stack, main input register, visible result in ordinary calculation |
| `Y` | Second stack level and left operand for binary operations such as `-` and `/` |
| `Z` | Third stack level for temporary operands |
| `T` | Fourth stack level for deeper temporary operands |

Stack-lift commands move values upward through `X -> Y -> Z -> T`; most binary
operations combine `Y` and `X`, put the result in `X`, and drop the lower stack.

### Saved and Display-Side State

| Register-like state | Purpose |
| --- | --- |
| `X1` / `BX` | Previous `X` value saved by many operations; used by stack/last-X style behavior |
| `X2` | Hidden display-side value. In program mode some commands copy `X -> X2`, while digit entry, `.`, `ВП`, and `/-/` can restore `X2 -> X`. This makes `X2` useful as a one-value programming cache, but it is not addressable by `X->П R` / `П->X R` and is not counted among the 15 user memory registers. See [15-x2-display-register.md](./15-x2-display-register.md). |

`X2` is therefore a real part of the programming model, but not a normal
addressable memory register. It belongs with the display/input machinery rather
than with `R0`..`Re`.

### Addressable Memory Registers

All addressable memory registers are general-purpose storage: a program decides
what each one means. Their built-in differences matter mainly for indirect
addressing and loop-style compact code:

| Registers | Normal purpose | Special programming behavior |
| --- | --- | --- |
| `R0`, `R1`, `R2`, `R3` | General data registers; input, state, counters, temporary values | Indirect commands using these registers decrement the transformed selector before use; `F L0`..`F L3` also use them as loop counters |
| `R4`, `R5`, `R6` | General data registers; input, state, counters, temporary values | Indirect commands using these registers increment the transformed selector before use |
| `R7`, `R8`, `R9` | General data registers; often convenient for stable computed addresses | Indirect commands use the selector without the `R0`..`R6` counter side effect |
| `Ra`, `Rb`, `Rc`, `Rd`, `Re` | General data registers; five extra hexadecimal-named storage cells beyond `R9` | Also stable indirect selector registers; there is no stock `Rf` register |

Program memory contains 105 steps. Official addresses are `00`..`99` and `A0`..`A4`; the calculator also has undocumented alias and dark-address behavior described in [02-undocumented-tricks.md](./02-undocumented-tricks.md).

The `mk61s-mini-expand` compiler feature profile models an expanded
MK61S_MINI variant rather than the stock machine: it enables direct `Rf`
access through `4F`/`6F` and treats 112 cells, `00`..`B1`, as official program
memory.

The display is a 12-position luminescent indicator. A normal number uses 8 mantissa digits, 2 exponent digits, and sign positions for the mantissa and exponent. Internally, digits are stored in 4-bit BCD-like nibbles, which is why hexadecimal-looking values can appear in undocumented modes.

## Official Operating Characteristics

The scan of the operating manual gives the following user-level characteristics:

| Characteristic | Value |
| --- | --- |
| Numeral system | Decimal |
| Mantissa precision | 8 digits |
| Exponent digits | 2 digits |
| Calculation range | `1e-99 <= abs(x) <= 9.9999999e99` |
| Displayed numeric format | Fixed for `1 <= abs(x) <= 99999999`; floating/scientific outside that range |
| Modes | Automatic calculation and programming |
| Program capacity | 105 steps |
| Memory registers | 15 user registers |
| Subroutine return stack | 5 levels |
| Operating temperature | `+10..+35 C` |
| Humidity range | `50..80%` |
| Atmospheric pressure | `66..106 kPa` |
| Power | Three A-316 "Kvant" cells or D2-10M external power supply |
| External supply input | 220 V AC, 50 +/- 1 Hz, via the D2-10M unit |
| Consumption from cells | Not more than 0.6 W |
| Dimensions | About `170 x 80 x 38 mm` |
| Mass without cells | Not more than `0.25 kg` |

The manual also notes that after switching the calculator off, repeated switching on should be delayed by at least 30 seconds.

## Chip Set

The MK-61 is built around the K745/K145 calculator chipset:

| Chip | Role |
| --- | --- |
| `K745IK1302-2` | Dispatcher/control processor, keyboard and display control |
| `K745IK1303-2` | Mathematical processor |
| `K745IK1306-2` | Processor for MK-61 additional functions |
| `K745IR2-2` | Dynamic register memory |
| `K745GF3-2` | Clock generator |

The K745 parts are unpackaged variants of the K145 series. Habr's emulator-oriented article describes the processors and memory chips as a serial ring: processor output goes into the next device, then through the dynamic RAM chips, and finally back to the first processor.

## Microarchitecture

Each IK13-family processor operates on 4-bit words. The main dynamic areas inside a processor are:

- `M`, `R`, and `ST`: 42 4-bit words each.
- `S` and `S1`: one 4-bit word each.
- `L`, `T`, and `P`: one-bit state cells.

Each processor ROM is organized as:

| ROM area | Size |
| --- | --- |
| Command ROM | 256 commands, 23 bits each |
| Synchronization programs | 128 programs, each 9 six-bit cells |
| Microcommands | 68 microcommands, 28 bits each |

A processor command is not the same thing as a user program step. A processor command selects synchronization programs; those select microcommands; microcommand bits drive elementary micro-operations. The Habr article describes one processor command as 42 ticks. A real MK-61 executes roughly 3-4 user program steps per second; one user step involves many lower-level ROM command executions.

## Ring Memory Layout

All MK-61 user-visible memory — program steps, the 15 data registers, and the
stack — physically lives in one circular bus ("magistral M") that chains the
processor and register chips. For the MK-61 the chain is
`IR2.1 -> IR2.2 -> IK1306 -> IK1303 -> IK1302`, closing back onto `IR2.1`. The
total ring holds `42*3 + 252*2 = 630` tetrads (one byte per tetrad in the
emulator), which is why the emulator model in
[`tests/emulator/mk61.cjs`](../tests/emulator/mk61.cjs) addresses memory through
fixed page/permutation tables rather than a flat register file.

The Sugonyaev MSP430 reverse-engineering write-up pins down the physical packing
that MK-Pro's code/data overlay and display-byte tricks rely on:

- Program steps are grouped into packets of 42 tetrads, **7 steps per packet**
  (period `42 / 7 = 6`). Inside a packet the step order is non-linear: `STEP-0`
  sits at the end, `STEP-1` at the start, then `3, 4, 5, 6` ascending. A step
  code is one byte; its high nibble is at offset `+6`, its low nibble at `+3`.
- Only 2 of the 6 tetrads per step hold the step code. The remaining tetrads in
  each packet hold the data registers `R0..Re` and the stack. Register `R0`
  lives in the same packet as `STEP-0..STEP-6`, the next register in the next
  packet, and so on up to `Re`; the stack registers `X1, X, Y, Z, T` sit beside
  the register digits starting from the `STEP-64..STEP-70` packet.
- There is **no register F** on a stock MK-61: a 16th register simply does not
  fit this 42-tetrad packet system. The "register F" reachable only through
  indirect `КП9`/`КИП9` in the memory-extension experiment exists only on a
  *modified* ring with 42 extra tetrads added, not on real hardware. See
  [14-indirect-addressing.md](./14-indirect-addressing.md).

## Numeric Representation

Numbers are stored as packed decimal-like nibbles. A regular number has:

- exponent sign,
- two exponent digits,
- mantissa sign,
- eight mantissa digits.

Only two sign codes are normally used: positive/zero and negative. Undocumented behavior appears when calculations or address transformations leave other nibble values in sign or digit positions. Those states are the basis for "hexadecimal" digits, sign-digits, non-normalized zeros, and other MK-61 hacks.

## Sources

- [MK-61 history, emulation, and device internals](https://habr.com/ru/articles/505612/)
- [Antowka: Elektronika MK61 hardware overview](https://antowka.ru/calc-electronika-mk61/)
- [Elektronika MK-61 operating manual scan](https://www.wass.net/manuals/Elektronika%20MK-61.pdf)
- [Serge Anvarov: undocumented MK-61 features](https://sergeanvarov.github.io/russian/mk61/%D0%9D%D0%B5%D0%B4%D0%BE%D0%BA%D1%83%D0%BC%D0%B5%D0%BD%D1%82%D0%B8%D1%80%D0%BE%D0%B2%D0%B0%D0%BD%D0%BD%D1%8B%D0%B5%20%D0%B2%D0%BE%D0%B7%D0%BC%D0%BE%D0%B6%D0%BD%D0%BE%D1%81%D1%82%D0%B8%20%D0%9F%D0%9C%D0%9A%20%D0%9C%D0%9A-61.html)
- [Alexey Sugonyaev: porting the MK-61 emulator to MSP430 (K145IK130x ring, microcode, and packet layout)](https://arbinada.com/pmk/category/01305.html)
- [Alexey Sugonyaev: MK-61 emulator memory-extension experiment ("register F" on a modified ring)](https://arbinada.com/pmk/category/01306.html)
