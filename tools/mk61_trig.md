# MK-61 trig runtime

This document describes the native compact implementation of MK-61
`sin` / `cos` / `tg`.

## Contract

The public API is declared in:

```text
native/include/mkpro/core/mk61_trig.hpp
```

The production implementation is:

```text
native/src/core/mk61_trig.cpp
```

The value contract is MK-61 display-string equality, not host floating-point
equality.

## Runtime shape

The production runtime is a compact C++ implementation of the MK-61 trig path:

- 8-significant-digit decimal/BCD-style rounding and truncation;
- `рад`, `град`, and `гр` angle reduction;
- MK-61-specific continued fractions for `sin`, `cos`, and `tg`;
- MK-61 display-string formatting.

It must not include ROM tables, emulator source files, generated command
dispatchers, or hardcoded result tables.

## Source of truth for validation

The validation oracle is the native emulator:

```text
native/src/emulator/mk61.cpp
native/src/emulator/rom.cpp
```

The validator builds a tiny CLI around the emulator, loads the single
`sin` / `cos` / `tg` opcode plus `С/П`, and compares calculator display
strings against the compact runtime. The emulator is used only by validation;
it is not linked into production trig code.

## Validation

Run the full emulator parity gate:

```bash
cmake --build native/build --target mkpro_validate_trig
native/build/mkpro_validate_trig --source-root .
```

Run the extended deterministic stress parity gate:

```bash
native/build/mkpro_validate_trig --source-root . --stress
```

The same gate is available through CMake and CTest:

```bash
cmake --build native/build --target mkpro_validate_trig
ctest --test-dir native/build -R '^mkpro\.validate_trig$' --output-on-failure
```

The quick native API guard is:

```bash
native/build/mkpro_tests --exact mk61_trig_matches_emulator_contract
```

## Corpus

The deterministic validation corpus is defined in:

```text
native/tools/validate_mk61_trig.cpp
```

It covers representative BCD mantissas, signs, exponents, small-angle paths,
range reduction, and large magnitudes. The base validator compares all
`rad` / `deg` / `grad` and `sin` / `cos` / `tg` display strings against the
emulator. The optional stress mode adds deterministic pseudo-random BCD-shaped
values.
