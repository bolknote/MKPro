# M61

`m61c` is an experimental TypeScript CLI translator from the M61 language to
Elektronika MK-61 program listings.

This milestone focuses on the translator. The repo also contains emulator
smoke/regression tests, but not a full semantic verifier for every optimizer
rewrite yet. Optimizations and `egg` constructs that rely on undocumented
behavior are reported as `unsafe-unverified` so you can see what still depends
on target-profile facts.

## Run

Node 22.6 or later strips TypeScript at runtime, so no build step is required:

```sh
npm install
npm run m61c -- compile examples/basic.m61 --out listing
npm run m61c -- compile examples/basic.m61 --out hex
npm run m61c -- compile examples/basic.m61 --out json
npm run m61c -- explain examples/tiny-game.m61
```

The CLI shape is:

```sh
m61c compile file.m61 --out listing|hex|json|all
m61c explain file.m61
```

Flags:

- `--opt safe|max` (default `max`). `safe` skips `egg` blocks. `max` keeps
  low-level compatibility blocks, enables automatic intent/lowering tactics,
  and reports unverified eggology choices explicitly.
- `--delivery manual|loader|hex` (default `hex`) controls which opcodes are
  considered enterable. Anything outside that delivery is reported as
  `unsafe-unverified`.
- `--budget N` (default `105`). Hard error if exceeded.
- `--no-warn-unsafe` strips `unsafe-unverified` annotations from listings
  and JSON output.

## M61 Language

M61 is a single language with a high-level game/application surface and a
low-level compatibility surface for handwritten calculator code. New programs
should describe intent: state, input, screens, rules, tables, and semantic
hints. The compiler decides whether that becomes registers, stack scheduling,
address constants, dark entries, overlays, X2/display bytes, or other MK-61
tricks.

```m61
target mk61
budget 105 cells
optimize size

preload R9 = random_seed()

program TinyGame {
  input key: digit

  state {
    [displayed] player: coord(floor 1..3, x 1..7, y 1..4) = input.X
    food: counter 0..9 = 5
  }

  screen main {
    show player, food
    style compact digits letters hex
  }

  turn {
    show main
    read key

    match key {
      2, 4, 6, 8 => move(direction(key))
      otherwise => stop 0
    }
  }
}
```

Handwritten `machine`/`entry`/`core`/`egg` programs still compile as the
low-level compatibility surface, but they are not a second public language.
They exist for listings, experiments, and cases where we intentionally want to
drop to raw calculator commands.

`--opt safe` lowers conservatively. `--opt max` uses the `mk61_exact` target
profile and automatic proofs to select aggressive tactics: indirect flow,
super-dark dispatch, branch removal, shared tails, cyclic layout, code/data
overlay, X2/`ВП`, display-byte packing, hexadecimal mantissa data, R0 edge
cases, and Danilov-style error-stop idioms when the source semantics allows
them.

All `examples/*.m61` programs are written in the human DSL. They cover:

- `examples/basic.m61`: minimal input/output and arithmetic.
- `examples/tiny-game.m61`: tiny menu-style loop.
- `examples/lunar.m61`: numeric landing game with resources and touchdown rules.
- `examples/human.m61`: small counter game used as syntax smoke test.
- `examples/cave-sketch.m61`: compact cave sketch in game intent form.
- `examples/cave-highlevel-baseline.m61`: readable Cave Treasure baseline.
- `examples/cave-treasure.m61`: full high-level compactness reference.
- `examples/cave-treasure-full.m61`: full reference metadata variant.
- `examples/grid-rescue.m61`: grid/bitset reference.
- `examples/resource-raid.m61`: resource/dispatch reference.
- `examples/giants-country.m61`: commented high-level game port.
- `examples/sea-battle.m61`: turn-based fleet duel port from the games archive.

## Documentation

MK-61 reference and programming notes live in [`docs/`](./docs/README.md).
Start with [docs/01-programming.md](./docs/01-programming.md) for RPN basics,
then follow the reading order in [docs/README.md](./docs/README.md).

## Development

```sh
npm run typecheck
npm test
```

The test suite covers the parser, the opcode catalog, high-level lowering,
low-level compatibility programs, the headless emulator loader smoke test, and
snapshots of the example programs.
Snapshots live in
`tests/compiler/__snapshots__/`.

## Status

- `compileSwitch` evaluates the discriminant once and stores it in a
  reserved scratch register; nested switches each get their own.
- Conditional branches follow the MK-61 convention "true falls through, false
  jumps to the address".
- Peephole optimization for redundant `X->П r ; П->X r` pairs at synthetic
  boundaries; raw items from `core { }` and `egg { }` blocks are skipped.
- M61 lowers high-level `program`, `state`, `screen`, `input`, `match`,
  `challenge`, resource updates, and rule calls through an
  intent/effect/layout report.
- JSON reports include IR stats, layout cell roles, candidate lowerings, and
  a budget summary.
- JSON/explain reports include an automatic optimizer capability matrix:
  implemented rules, candidates, safe-mode blocks, and planned verifier hooks
  for Danilov-era MK-61 quirks.
- Const expressions detect simple cycles.
- Implicit allocation for an undeclared assign/ask target produces a warning,
  not a silent allocation.

## Layout

- `src/cli.ts` — argv parsing and output dispatch
- `src/core/parser.ts` — text → AST
- `src/core/compiler.ts` — AST → `MachineItem[]` → resolved steps
- `src/core/opcodes.ts` — full 256-entry catalog with risk/delivery metadata
- `src/core/format.ts` — listing, hex (8-byte columns), JSON, explain
- `tests/emulator/` — headless MK-61 microcode model used for smoke tests
  and future end-to-end verification.
