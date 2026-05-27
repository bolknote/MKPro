# Low-Level M61 Compatibility

This page describes the older compact-source surface that remains accepted by
the compiler. New high-level work should prefer
[13-m61-language.md](./13-m61-language.md), where source code describes
human-level intent and the `mk61_exact` target profile enables machine tricks
automatically.

The project now treats this as low-level compatibility, not as a second public
language. It exists so old sketches, handwritten calculator listings, and
compact experiments continue to compile while the main M61 language moves
toward a single human-centered surface.

## Core Ideas

- `allow` is accepted for old sources, but is deprecated. The optimizer now
  reads MK-61 capabilities from the target profile and gates them with checks.
- `state` describes small values and resources before they become registers,
  constants, addresses, or overlay cells.
- `display packed` describes output intent instead of forcing the user to
  hand-assemble mantissas or undocumented display bytes.
- `input digit` and `dispatch` describe command handling as a small automaton,
  not as a literal compare ladder.
- `block tail` and `shared tail` make control-flow sharing explicit, so the
  layout pass can avoid ordinary subroutine calls.

## Compatibility Surface

```m61
// Deprecated in new code: target mk61 / mk61_exact enables capabilities automatically.
allow undocumented, dark_entries, code_data_overlay, address_constants, display_bytes

state Cave {
  player: packed = 1
  command: digit = 0
  food: resource = 9
}

display packed cave_screen {
  mode digits_and_letters
  source player
}

entry main {
  show cave_screen
  command = input digit

  dispatch command as movement {
    case 2 { call move_south }
    default { halt 0 }
  }
}

block tail move_south {
  player = player + 2
}
```

`unsafe asm { ... }` is accepted as a clearer alias for `egg { ... }`.

## Current Lowering

The current implementation is intentionally rule-based:

- `state` fields with initial values lower to register-backed values.
- `display packed` lowers to ordinary packed numeric output for compatibility
  programs. Under `mk61_exact`, the optimizer reports display-byte and X2
  scheduling as automatic target-profile candidates.
- `dispatch` records candidate variants and emits either `safe-compare-chain`
  or `fallthrough-compare-chain`.
- `dark-indirect-table`, indirect-flow, address overlay, X2 display scheduling,
  and R0-alias rules are reported as generic optimizer capabilities until
  verifier-grade effect checks exist.
- Address operands are reported with `address`, `constant`, and `overlay`
  roles under `--opt max` when the target profile supports those features.
- `shared tail` cells are marked as `dark-entry` candidates in the layout
  report.

`examples/cave-sketch.m61` is now explicitly a sketch baseline, not the hard
target. The hard target is `examples/cave-treasure-full.m61`: it declares
preloads, maze/cache/resource domains, formal entry at `44`, and all unsafe
permissions needed by a truly compact lowering. The compiler currently refuses
to generate code for semantic domains that do not yet have real rule lowerers;
this prevents high-level game intent from being silently ignored.
