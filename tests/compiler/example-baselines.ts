// Exact compiled-size baselines, shared by the size-guard test and the
// `examples:check` no-regression script (`scripts/assert-no-size-regression.mjs`).
//
// Cell count is taken from `report.steps`, which equals the number of program
// cells emitted by `mk-pro compile ... --out hex`.
//
// Before adding a size optimization, lock the current compiled cell count here.
// When a program intentionally gets smaller, update the matching number in the
// same change so the diff shows the saving clearly. A growth update should only
// accompany a semantic/safety fix that behavioral tests cover.

export const EXAMPLE_BASELINE: Record<string, number> = {
  "99-bottles": 52,
  alaram: 66,
  basic: 8,
  "cave-highlevel-baseline": 104,
  "cave-sketch": 38,
  "cave-treasure": 104,
  clock: 34,
  "dangerous-loading": 84,
  dungeon: 75,
  "e-94-digits": 64,
  "functions-demo": 25,
  "fox-hunt-100": 103,
  "fox-hunt-mk61": 60,
  "game-100-pig": 97,
  "giants-country": 105,
  human: 27,
  "jack-pot": 96,
  labyrinth777: 105,
  lunar: 44,
  "minesweeper-9x7": 79,
  "minesweeper-9x9": 79,
  "raja-yoga": 77,
  "rambo-iii": 104,
  "river-battle": 95,
  "sea-battle": 66,
  teleport: 96,
  "tic-tac-toe": 99,
  "tiny-game": 27,
  "treasure-hunter-2": 99,
  wumpus: 103,
};

// pending-optimizer programs may overflow the physical MK-61 address space, so
// they are measured with a large budget and analysis mode, mirroring
// `mk-pro compile ... --out hex --budget 999999 --analysis`. The goal is to
// shrink these toward 105; intentional shrinkage should update these exact
// baselines just like top-level examples. Growth is reserved for semantic or
// machine-safety fixes.
export const PENDING_BASELINE: Record<string, number> = {
  "tic-tac-toe-4x4": 134,
};
