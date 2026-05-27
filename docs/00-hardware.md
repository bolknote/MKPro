# Elektronika MK-61: Hardware Internals and Characteristics

The Elektronika MK-61 is a Soviet programmable scientific calculator released in 1984. It belongs to the B3-34-compatible family, but extends the earlier machines with a third processor, more program memory, one more data register, and several additional scientific and service functions. It is closest in programming model to the MK-52, except that the MK-61 has volatile program memory rather than the MK-52's non-volatile storage.

## User-Visible Architecture

The calculator is an RPN machine. The visible value is the top of the operational stack:

| Register | Purpose |
| --- | --- |
| `X` | Displayed value and main input register |
| `Y` | Second operand for binary operations |
| `Z` | Third stack level |
| `T` | Fourth stack level |
| `X1` / `BX` | Previous `X` value saved by many operations |

In addition to the stack, the user has 15 addressable memory registers: `R0`..`R9`, `Ra`, `Rb`, `Rc`, `Rd`, and `Re`. Programs can store and recall them directly or indirectly.

Program memory contains 105 steps. Official addresses are `00`..`99` and `A0`..`A4`; the calculator also has undocumented alias and dark-address behavior described in [02-undocumented-tricks.md](./02-undocumented-tricks.md).

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
