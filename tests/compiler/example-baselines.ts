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
  alaram: 75,
  basic: 8,
  "cave-highlevel-baseline": 104,
  "cave-sketch": 47,
  "cave-treasure": 104,
  clock: 40,
  "dangerous-loading": 86,
  dungeon: 84,
  "e-94-digits": 64,
  "functions-demo": 25,
  "fox-hunt-100": 105,
  "fox-hunt-mk61": 65,
  "game-100-pig": 101,
  "giants-country": 104,
  human: 26,
  "jack-pot": 99,
  labyrinth777: 105,
  lunar: 47,
  "minesweeper-9x7": 87,
  "minesweeper-9x9": 87,
  "raja-yoga": 84,
  "rambo-iii": 103,
  "river-battle": 95,
  "sea-battle": 72,
  teleport: 97,
  "tic-tac-toe": 100,
  "tiny-game": 25,
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
