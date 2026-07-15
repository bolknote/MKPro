# Pending Optimizer

This directory is only for MK-Pro sources that compile through the normal
pipeline but still exceed the MK-61 105-cell window.

Only `tic-tac-toe-4x4.mkpro` remains pending. Previously pending examples that
now fit were moved back to the top-level `examples/` directory; keep their old
optimization notes out of this file.

Treat the remaining file as an optimizer/lowering-size bug. Do not replace it
with a raw listing: the goal is to make the high-level source fit.

## Current Target

| File | Current | Target | Gap | Status |
| --- | ---: | ---: | ---: | --- |
| `tic-tac-toe-4x4.mkpro` | 151 | 105 | +46 | pending optimizer |

The `Current` number is the local `--analysis` size. Strict `mk-pro compile`
mode may reject over-window programs earlier than the analysis path.

## Live Optimization Notes

- Preserve the source UI and behavior: the player retry stop for an occupied
  cell must still expose `X=-99999999`, ordinary answers must expose the stack
  pair `X:Y`, documented `1..4` inputs and ordinary signed/fractional aliases
  must normalize like the original, extreme inputs must retain the MK-61's
  eight-digit rounding, wins must report the original fractional packed mask,
  and move selection must keep the source-style max/tie behavior.
- Only the manual input/STOP protocol and its visible `X:Y` results are the
  external contract. Occupied cells and logical line evolution are an internal
  model used to prove future UI behavior; their physical registers, initial and
  intermediate representations, selector values, and preload layout are not
  externally fixed. Any replacement must still prove the same later UI.
- Both the input path and internal diagonal indexes now use the generic signed
  `grid_norm` primitive. This deliberately follows the calculator's decimal
  execution instead of assuming mathematical modulo at the eight-digit
  rounding boundary. The main remaining size pressure is helper and ABI
  traffic around `grid_norm`, `cell_mask`, `packed_score`, `candidate_score`,
  and the line-update/check path.
- The generic natural-target component layout now moves only fallthrough-closed
  machine regions after an exact CFG/return-stack proof. It places a helper at
  the address already encoded by a stable internal preload and shortens three
  direct calls, reducing the proved full-search result from 166 to 163 cells.
- A generic semantic-alias pass now proves that a unary expression helper and
  an existing shared helper agree over the complete flow-sensitive finite input
  domain. Opaque call origins survive ordinary IR suffix sharing, while exact
  eight-digit input derivation, canonical `+0`, decimal helper execution,
  stack/return ABI, physical X1 continuation, numeric indirect targets, and the
  immutable helper body, final preload/layout artifact, and all relocated
  address operands are checked independently. Unknown input and post-proof body
  changes fail closed; unrelated synthetic programs cover both the positive and
  rejection paths.
- A one-parameter function whose complete body is a typed `grid_norm` or
  `grid_wrap` assignment can now consume its parameter directly from X and
  tail-forward to the shared primitive. Generic call threading and dead-proc
  elimination then remove the wrapper. Finally, candidate discovery compares
  proof-gated final layouts for the incumbent and every curated candidate
  instead of discarding an option because its temporary pre-layout addresses
  overflow. For this source that selects the general stack-resident option,
  places the shared normalizer at the existing stable R8 target, shortens five
  calls, and reduces the fully verified result from 163 to 154 cells.
- Later generic optimizer passes reduced the fully verified result from 154 to
  151 cells without source-specific recognition. The remaining gap to the
  MK-61 program window is 46 cells.
- Optimizer tests must use unrelated synthetic programs and local proof
  obligations. The tic-tac-toe fixture may lock only its size and observable
  UI; it must not select or justify an optimization by recognizing this game
  or its whole-program architecture.
