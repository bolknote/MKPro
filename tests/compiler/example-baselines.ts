// Exact compiled-size baselines, shared by the size-guard test and the
// `examples:check` no-regression script (`scripts/assert-no-size-regression.mjs`).
//
// Cell count is taken from `report.steps`, which equals the number of program
// cells emitted by `mk-pro compile ... --out hex`.
//
// Before adding a size optimization, lock the current compiled cell count here;
// when a program intentionally gets smaller, update the matching number in the
// same change so the diff shows the saving clearly. The candidate composition
// engine's selection is minimum-size, so these numbers may only ever go down.

export const EXAMPLE_BASELINE: Record<string, number> = {
  "99-bottles": 53,
  alaram: 75,
  basic: 8,
  "cave-sketch": 47,
  clock: 43,
  "dangerous-loading": 84,
  dungeon: 82,
  "e-94-digits": 64,
  "functions-demo": 25,
  "fox-hunt-100": 103,
  "fox-hunt-mk61": 65,
  "game-100-pig": 103,
  human: 23,
  "jack-pot": 99,
  labyrinth777: 105,
  lunar: 47,
  "minesweeper-9x7": 83,
  "minesweeper-9x9": 81,
  "raja-yoga": 87,
  "rambo-iii": 104,
  "sea-battle": 74,
  teleport: 97,
  "tic-tac-toe": 100,
  "tiny-game": 23,
  "treasure-hunter-2": 100,
  wumpus: 100,
};

// pending-optimizer programs may overflow the physical MK-61 address space, so
// they are measured with a large budget and analysis mode, mirroring
// `mk-pro compile ... --out hex --budget 999999 --analysis`. The goal is to
// shrink these toward 105; intentional shrinkage should update these exact
// baselines just like top-level examples.
export const PENDING_BASELINE: Record<string, number> = {
  "cave-highlevel-baseline": 134,
  "cave-treasure": 118,
  "giants-country": 120,
  "tic-tac-toe-4x4": 313,
};
