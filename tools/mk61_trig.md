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

The ROM-derived oracle is:

```text
tools/mk61_trig_microcode_probe.cpp
```

That tool contains the ROM/microcode data and is used only by validation. It is
not linked into production code.

## Validation

Run the full ROM-derived parity gate:

```bash
tools/validate_mk61_trig.py
```

Run the extended deterministic stress parity gate:

```bash
tools/validate_mk61_trig.py --stress
```

The same gate is available through CMake and CTest:

```bash
cmake --build native/build --target mkpro_validate_trig
ctest --test-dir native/build -R '^mkpro\.validate_trig$' --output-on-failure
```

The quick native API guard is:

```bash
native/build/mkpro_tests --exact mk61_trig_matches_rom_derived_contract
```

## Corpus

The deterministic validation corpus is defined in:

```text
tools/mk61_trig_corpus.py
```

It covers representative BCD mantissas, signs, exponents, small-angle paths,
range reduction, and large magnitudes. The base validator compares
`3 modes * 3 functions * 157 values = 1413` display strings against the
ROM/microcode probe. The optional stress mode adds deterministic pseudo-random
BCD-shaped values and compares `2565` display strings.
