# MK-61 headless emulator

This test helper is based on the JavaScript emulator published at:

<https://mk-61.moy.su/emulator.html>

Downloaded/extracted on 2026-05-27. The source page contains UI for several
devices; this folder keeps only the MK-61/MK-54-class calculator core needed for
tests:

- `rom.cjs` - ROM tables copied mechanically from `const ПЗУ`.
- `mk61.cjs` - headless `MK61` wrapper around the ИР2/ИК13 microcode model.
- `lordbss.cjs` - parser for local manifest-relative `games/<section>/*.txt`
  listings and the original Lord_BSS HTML tables.
- `smoke-games.cjs` - smoke check against a few downloaded game pages,
  including the local Treasure Cave demo fixture.

Basic use:

```js
const { MK61 } = require('./tests/emulator/mk61.cjs');

const calc = new MK61();
calc.loadProgram('00.Сx 01.1 02.2 03.+ 04.С/П');
calc.press('В/О');
calc.press('С/П');
calc.runUntilStable();
console.log(calc.displayText());
```

The wrapper exposes calculator-style operations:

- `press(key)` / `press(x, y)` - press a key by name or matrix coordinates.
- `inputNumber(value)` - enter a number through keys.
- `loadProgram(textOrCodes)` - load mnemonic text or numeric opcodes.
  Addressed listings with one `00<TAB>command` line per step are supported.
- `setRegister(register, value)` / `readRegister(register)` - seed/read memory
  and stack registers without going through the keyboard.
- `runFrames(n)` / `runUntilStable()` - advance the microcode clock.
- `displayText()` / `displayCells()` - inspect the display without any DOM.

Smoke run:

```sh
node tests/emulator/smoke-games.cjs
```

Pustyshka / hidden-memory experiments:

```sh
node tests/emulator/probe-pustyshka.cjs
node tests/emulator/trace-pustyshka.cjs --corruption
```

The first script reproduces keyboard protocols and their failure cases. The
second traces the ROM marker search and verifies the cause by intercepting one
ALU input in an explicitly labelled counterfactual copy. Neither changes the
emulator core. See [the mechanism analysis](../../docs/23-pustyshka-hidden-memory-experiment.md).

Continuous program-mode write/read protocols:

```sh
node tests/emulator/probe-pustyshka-program.cjs
```

This verifies one full decimal value or three integers in 0..999, including
repeated reads, erased ordinary registers, stack cleanup, nested calls, raw
numeric words, and all 105 program bytes. The exact programs are stored in
`fixtures/pustyshka-{triple,scalar,demo}.hex`; the demo generates its own inputs.
`probe-pustyshka-program.cpp` runs broader cases on the unchanged native emulator
using the same fixtures and its public API. Commands, placement constraints,
and build instructions are in [the program protocol](../../docs/24-pustyshka-program-protocol.md).
