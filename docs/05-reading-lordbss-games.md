# Reading Lord_BSS MK-61 Games

This note answers a practical question: are the MK-61 references in this
directory enough to understand the cleaned Lord_BSS game listings in `games/`?

Short answer: they are enough to read the calculator commands, but not enough
to comfortably understand the games as published. Lord_BSS pages use compact
listing notation, mixed Cyrillic/Latin glyphs, display art, switch-position
controls, and per-game register conventions. Those need their own reading
layer.

## What the Existing References Cover

The existing documents are good for the calculator itself:

- [01-programming.md](./01-programming.md) explains the stack, registers,
  program entry, direct jumps, conditionals, loops, and subroutines.
- [02-undocumented-tricks.md](./02-undocumented-tricks.md) explains indirect
  addressing, alias/dark addresses, X2/display side effects, logical nibble
  operations, and memory-saving tricks.
- [03-command-reference.md](./03-command-reference.md) gives the command table,
  including `К СЧ`, `К {x}`, `К [x]`, `К ∧`, `К ∨`, `К ⊕`, `К ИНВ`, indirect
  jumps/calls/stores/recalls, and `F L0`..`F L3`.
- [04-programming-cookbook.md](./04-programming-cookbook.md) gives enough
  workflow to write ordinary MK-61 programs.

That is sufficient for programs like `games/lunar-landing.txt` ("Lunar
Landing"). The code uses direct register stores/recalls, basic arithmetic, a
few direct conditionals, and stop points. The companion
`games/lunar-landing.md` describes the game variables, so the listing can be
followed almost directly.

## Lord_BSS Listing Notation

The Lord_BSS pages compress key names aggressively. Before reading a game,
translate the local notation into the command names used by the references.

| Lord_BSS form | Reference form | Meaning |
| --- | --- | --- |
| `ПхR` | `П->X R` | Recall memory register `R` into `X`. |
| `хПR` | `X->П R` | Store `X` into memory register `R`. |
| `КППR` | `К ПП R` | Indirect subroutine call through register `R`. |
| `КБПR` | `К БП R` | Indirect unconditional jump through register `R`. |
| `КПхR` | `К П->X R` | Indirect recall; register `R` selects the source register. |
| `КхПR` | `К X->П R` | Indirect store; register `R` selects the destination register. |
| `Kx<oR` | `К x<0 R` | Indirect conditional jump. |
| `Kx=oR` | `К x=0 R` | Indirect conditional jump. |
| `Kx≠oR` | `К x!=0 R` | Indirect conditional jump. |
| `Kx≥oR` | `К x>=0 R` | Indirect conditional jump. |
| `Fx<o aa` | `F x<0 aa` | Direct conditional; the next cell is the failure address. |
| `Fx=o aa` | `F x=0 aa` | Direct conditional; the next cell is the failure address. |
| `Fx≠o aa` | `F x!=0 aa` | Direct conditional; the next cell is the failure address. |
| `Fx≥o aa` | `F x>=0 aa` | Direct conditional; the next cell is the failure address. |
| `FLn aa` | `F Ln aa` | Loop on `Rn`; decrement and jump if non-zero. |
| `Кноп` | `К НОП` | No-op. |
| `FBx` | `F Вx` | Full stack lift. |
| `K[x]`, `K{x}` | `К [x]`, `К {x}` | Integer part and fractional part. |
| `KΛ`, `KV`, `K⊕` | `К ∧`, `К ∨`, `К ⊕` | Logical AND, OR, XOR on mantissa nibbles. |
| `Кинв`, `Кзн`, `K|x|` | `К ИНВ`, `К ЗН`, `К |x|` | Bitwise NOT, sign, absolute value. |
| `В↑` | `В↑` | Stack lift / enter. |
| `↔` | `<->` | Swap `X` and `Y`. |
| `Сх`, `Cx` | `Cx` | Clear `X`; the Latin and Cyrillic forms are mixed in pages. |

Register letters also mix alphabets. Treat `А`, `В`, `С`, `Д`, `Е` as
`Ra`, `Rb`, `Rc`, `Rd`, `Re`. On old pages, Latin-looking `A`, `B`, `C` may be
used for the same calculator registers, not for English variables.

## Program Tables

Each program table is an address grid. A row label plus a column label gives
the step address:

- row `00`, column `00` is address `00`;
- row `00`, column `09` is address `09`;
- row `10`, column `00` is address `10`;
- row `A0`, column `04` is address `A4`.

Direct jumps, subroutine calls, conditionals, and loops consume the next cell
as an address operand. For example, `БП 60` occupies two program steps. The
address cell is still a physical program cell; if another path jumps into it,
the calculator will execute whatever opcode that cell represents. Some compact
games rely on this.

## What Still Requires Game-Specific Reading

The references cannot tell you what a game display means. They explain how the
calculator produces values, but the page text defines the user interface.

Examples:

- `lunar-landing.txt` / `lunar-landing.md` ("Lunar Landing") is mostly transparent. `R1` is height, `R2`
  fuel, `R3` speed, and the program stops to show height, speed, fuel, then
  accepts fuel burn. `777` means success, `666` means crash.
- `morskoy-boy-lordbss.txt` / `morskoy-boy-lordbss.md` ("Sea Battle") is still readable from the references, but the
  output `1`/`-1` only makes sense with the rules on the page: it encodes which
  half of the compass directions the hidden submarine lies in.
- `minesweeper.txt` / `minesweeper.md` ("Minesweeper") needs the command reference plus the page's
  board model. It uses `К СЧ` for mine placement, indirect register operations
  for board cells, and logical operations for packed state.
- `tetris.txt` / `tetris.md` ("Tetris") is command-readable but not obvious as a game until
  you know its display packing. It uses logical XOR/AND/OR, indirect calls,
  random generation, and the `Р-ГРД-Г` switch as a control input.
- `aeroport.txt` / `aeroport.md` ("Airport") and `alaram.txt` / `alaram.md`
  ("Alaram") are good stress tests.
  Their commands are covered by the references, but their meaning depends on
  constants, packed display glyphs, and the text explaining outputs such as
  `Г-Н-ХХХ`, `8CГ -78`, `ГI6EL 91`, and `ЕГГОГ`.

## Practical Reverse-Engineering Workflow

1. Copy the page's register initialization block into a separate note.
2. Expand Lord_BSS notation to the reference notation above.
3. Mark every two-step command with its operand: `БП`, `ПП`, direct
   conditionals, and `F L0`..`F L3`.
4. Mark every indirect command and inspect the referenced register value.
5. Treat constants such as `13200087`, `88168891`, or `8.XXXXXXX` as packed
   display/state data until proven otherwise.
6. Read every `С/П` as an I/O boundary: the program either shows state, asks
   for input, or both.
7. Decode display strings from the game text, not from the command reference.
8. Only then try to infer the high-level algorithm.

## Sufficiency Verdict

For writing new MK-61 programs, the current references are sufficient.

For understanding downloaded Lord_BSS games, the references are necessary but
not sufficient by themselves. With this note, the command notation gap is mostly
closed; the remaining work is per-game annotation: registers, packed display
formats, controls, and the intended state machine.
