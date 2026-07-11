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
| `tic-tac-toe-4x4.mkpro` | 163 | 105 | +58 | pending optimizer |

The `Current` number is the local `--analysis` size. Strict `mk-pro compile`
mode may reject over-window programs earlier than the analysis path.

## Live Optimization Notes

- Preserve the source UI and behavior: the player retry stop for an occupied
  cell must still expose `X=-99999999`, ordinary answers must expose the stack
  pair `X:Y`, arbitrary signed/fractional coordinates must normalize like the
  original, wins must report the original fractional packed mask, and move
  selection must keep the source-style max/tie behavior.
- Only the manual input/STOP protocol and its visible `X:Y` results are the
  external contract. Occupied cells and logical line evolution are an internal
  model used to prove future UI behavior; their physical registers, initial and
  intermediate representations, selector values, and preload layout are not
  externally fixed. Any replacement must still prove the same later UI.
- The input path now uses the generic signed `grid_norm` primitive; internal
  diagonal indexes use a separate domain-proved one-based modulo form. The main
  remaining size pressure is helper and ABI traffic around `grid_norm`,
  `cell_mask`, `packed_score`, `candidate_score`, and the
  line-update/check path.
- The generic natural-target component layout now moves only fallthrough-closed
  machine regions after an exact CFG/return-stack proof. It places a helper at
  the address already encoded by a stable internal preload and shortens three
  direct calls, reducing the proved full-search result from 166 to 163 cells.
- Optimizer tests must use unrelated synthetic programs and local proof
  obligations. The tic-tac-toe fixture may lock only its size and observable
  UI; it must not select or justify an optimization by recognizing this game
  or its whole-program architecture.
