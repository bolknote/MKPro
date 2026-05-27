# F Opcodes and Demonstration Program Notes

Most opcodes with high nibble `F` cannot be entered from the keyboard in the
ordinary way. They matter because eggology sequences can insert them into
program memory, and because they can be useful filler bytes in overlays.

## What `F0`..`FF` Do

The command appendix describes `F0`..`FF` as empty operators that are
X2-affecting. Emulator probes in this repository found no difference from
`К НОП` for simple `X` states (`5`, `0`, `-1`, `1,5`) or for `F*` followed by
`.`. However, the ROM words differ by byte and by chip, so do not assume all
`F*` opcodes are identical in X2-heavy or error-heavy contexts.

`5F` is a special case in the ROM used here. It is not a non-terminating hang in
the wrapper: it leaves internal `X` intact and changes the display/raw state.
For example, `X=5` shows `0,5000000000,0,`.

## Ways to Insert `F*` Commands

Known insertion families:

- `В/О К ПП R` can place `F0`..`FE` around addresses `30`..`44`, depending on
  the indirect-addressing state.
- A constructed `ЕГГ0Г` value followed by `ВП D1 D2 . 0` can insert `F{D2}` at
  address `5{D1}`.
- With a prepared return stack, repeated `FF` commands can be inserted at
  alternating addresses.

These are service/program-entry tricks, not normal runtime instructions. Always
verify the resulting listing before relying on it.

## Useful Roles for `F*`

- filler in an address/code overlay;
- X2-affecting no-op where `К НОП` would be too visible or impossible to place;
- raw byte needed to form a formal address such as `FA`..`FF`;
- display/raw-state experiment, especially with `5F`.

Avoid `F*` when a documented `К НОП`, `К 1`, or `К 2` can do the same job.

## Demonstration Program Lessons

The demonstration cave program shows how multiple undocumented mechanisms work
together in a real 105-step application:

- floor and treasure maps are packed as bit masks in registers;
- blue logical operations test and update map bits;
- player coordinates encode floor, column, and row in one number;
- dark-address entry lets a shared routine return to the main loop without a
  normal `В/О`;
- X2 restoration carries user input through intermediate computation;
- indirect addressing chooses movement coefficients and control-flow targets;
- `К max` and logical operations are used as compact condition/result shapers.

The most important design lesson is that no single trick saves the program. The
space saving comes from arranging data representation, control flow, and display
conventions so the same number or address byte serves more than one role.

## Sources

- [Demonstration program](https://sergeanvarov.github.io/russian/mk61/uf/demo.html)
- [Command appendix](https://sergeanvarov.github.io/russian/mk61/uf/commands.html)
- [Optimization tricks](https://sergeanvarov.github.io/russian/mk61/uf/tricks.html)
