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
| `tic-tac-toe-4x4.mkpro` | 181 | 105 | +76 | pending optimizer |

The `Current` number is the local `--analysis` size. Strict `mk-pro compile`
mode may reject over-window programs earlier than the analysis path.

## Live Optimization Notes

- Preserve the source UI and behavior: the player retry stop for an occupied
  cell must still expose `X=-99999999`, ordinary answers must expose the stack
  pair `X:Y`, arbitrary signed/fractional coordinates must normalize like the
  original, wins must report the original fractional packed mask, and move
  selection must keep the source-style max/tie behavior.
- Packed occupied masks and four logical line banks are semantic state. Their
  physical registers, selector values, initial preload layout, and temporary
  encodings are compiler decisions and are not part of the UI contract.
- The main remaining size pressure is helper and ABI traffic around
  signed modulo, `cell_mask`, `packed_score`, `candidate_score`, and the
  line-update/check path.
- Optimizer tests must use unrelated synthetic programs and local proof
  obligations. The tic-tac-toe fixture may lock only its size and observable
  UI; it must not select or justify an optimization by recognizing this game
  or its whole-program architecture.
