# MK-61 generated trig runtime

This document describes the native generated implementation of MK-61
`sin` / `cos` / `tg`.

## Contract

The public API is declared in:

```text
native/include/mkpro/core/mk61_trig_generated.hpp
```

The implementation is generated into:

```text
native/src/core/mk61_trig_generated.cpp
```

The value contract is MK-61 display-string equality, not host
floating-point equality. The generated runtime must match the ROM/microcode
probe for all validated `rad`, `deg`, and `grad` mode cases.

## Source of truth

The source of truth is:

```text
tools/mk61_trig_microcode_probe.cpp
```

That tool contains the ROM/microcode data and can dump specialized C++ command
bodies with:

```bash
tools/mk61_trig_microcode_probe --dump-chip-specialization IK1303 422e9a
```

The production runtime must not include ROM tables, emulator source files, or
generic ROM dispatch at runtime. It contains only generated command bodies and
the minimal ring-state machinery required to execute the trig path.

## Regeneration

Do not edit command bodies or dispatch tables in
`native/src/core/mk61_trig_generated.cpp` by hand.

Regenerate them with:

```bash
tools/generate_mk61_trig_generated.py
```

Check that the committed generated source is reproducible:

```bash
tools/generate_mk61_trig_generated.py --check
```

## Validation

Run the full ROM-derived parity gate:

```bash
tools/validate_mk61_trig_generated.py
```

Run the extended deterministic stress parity gate:

```bash
tools/validate_mk61_trig_generated.py --stress
```

The same gate is available through CMake and CTest:

```bash
cmake --build native/build --target mkpro_validate_trig_generated
ctest --test-dir native/build -R '^mkpro\.validate_trig_generated$' --output-on-failure
```

The quick native API guard is:

```bash
native/build/mkpro_tests --exact mk61_trig_generated_matches_rom_derived_contract
```

## Current command-set invariant

The full validation gate asserts the generated command set:

```text
ik1302:addresses=83,commands=80
ik1303:addresses=136,commands=132
ik1306:addresses=9,commands=7
```

If this changes, do not blindly update the expected values. First inspect
whether the ROM-derived coverage corpus legitimately found a new trig path or
whether unnecessary specializations were added.

## Corpus

The deterministic generation/validation corpus is defined in:

```text
tools/mk61_trig_corpus.py
```

It covers representative BCD mantissas, signs, exponents, small-angle paths,
range reduction, and large magnitudes. The heavy validator currently compares
`3 modes * 3 functions * 157 values = 1413` display strings against the
ROM/microcode probe. The optional stress mode adds deterministic pseudo-random
BCD-shaped values and currently compares `2565` display strings.
