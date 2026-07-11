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
| `tic-tac-toe-4x4.mkpro` | 120 | 105 | +15 | pending optimizer |

The `Current` number is the local `--analysis` size. Strict `mk-pro compile`
mode may reject over-window programs earlier than the analysis path.

## Live Optimization Notes

- Preserve the source UI and behavior: the player retry stop for an occupied
  cell must still show `-99999999`, wins still report the original fractional
  packed mask, and the calculator move selection must keep the source-style
  max/tie behavior.
- The source uses packed occupied masks and four packed line banks in source
  register order: `R7=x`, `R6=y`, `R5=x+y`, `R4=x-y`.
- The main remaining size pressure is helper and ABI traffic around
  `cell_mask`, `packed_score`, `candidate_score`, and the shared
  line-update/check path.
- Keep optimization work generic where possible. Existing tests already track
  the tic-tac-toe-specific size attribution and should keep exposing the
  remaining blockers.
