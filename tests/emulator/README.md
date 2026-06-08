# MK-61 headless emulator

This test helper is based on the JavaScript emulator published at:

<https://mk-61.moy.su/emulator.html>

Downloaded/extracted on 2026-05-27. The source page contains UI for several
devices; this folder keeps only the MK-61/MK-54-class calculator core needed for
tests:

- `rom.cjs` - ROM tables copied mechanically from `const ПЗУ`.
- `mk61.cjs` - headless `MK61` wrapper around the ИР2/ИК13 microcode model.
- `lordbss.cjs` - parser for local `games/*.txt` listings
  and the original Lord_BSS HTML tables.
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
