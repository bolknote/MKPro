# MK-Pro

`mk-pro` is an experimental TypeScript CLI translator from the MK-Pro language to
Elektronika MK-61 program listings.

This milestone focuses on the translator. The repo also contains emulator
smoke/regression tests. The `mk61` machine model is the authoritative execution
model for optimizer proofs, including documented, undocumented, and dark-entry
machine behavior.

## Run

Node 22.6 or later strips TypeScript at runtime, so no build step is required:

```sh
npm install
npm run mk-pro -- compile examples/basic.mkpro --out listing
npm run mk-pro -- compile examples/basic.mkpro --out hex
npm run mk-pro -- compile examples/basic.mkpro --out json
npm run mk-pro -- explain examples/tiny-game.mkpro
```

The CLI shape is:

```sh
mk-pro compile file.mkpro --out listing|hex|json|all
mk-pro explain file.mkpro
```

Flags:

- `--delivery manual|loader|hex` (default `hex`) controls opcode delivery
  metadata for listings.
- `--budget N` (default `105`). Hard error if exceeded.

## Browser Bridge

To load MK-Pro source directly into Serge Anvarov's browser emulator:

```sh
npm run build:browser
npm run serve:browser
```

Then paste the console loader from
[docs/19-anvarov-browser-bridge.md](./docs/19-anvarov-browser-bridge.md) into
the emulator page. The bridge compiles `#program` before the emulator's own
write-to-memory handler runs, so the page only sees MK-61 hex opcodes.

## MK-Pro Language

MK-Pro is a single V2 language for game/application intent. Programs describe
state, reads, screens, rules, and tables. The compiler decides
whether that becomes registers, stack scheduling, address constants, dark
entries, overlays, X2/display bytes, or other MK-61 tricks.

```mkpro
program TinyGame {
  field: board(0..9, 0..9)

  state {
    player: coord(field) = 1
    food: counter 0..9 = 5
  }

  screen main {
    show player, food
  }

  turn {
    show main
    read key

    match key {
      2, 4, 6, 8 => go direction(key)
      otherwise => game_over
    }
  }

  rule go delta {
    player += delta
    show main
  }

  rule game_over {
    stop 0
  }
}
```

The compiler always uses the `mk61` machine model and automatic proofs to select
the strongest proved tactics: indirect flow, branch removal, shared tails,
code/data overlay, X2/`ВП`, display-byte packing, hexadecimal mantissa data,
R0 edge cases, and Danilov-style error-stop idioms when the source semantics
allows them. Super-dark FA..FF dispatch is modeled, but it is selected only
when the layout proves the entry/continuation cells and the dispatch register's
FA..FF selector value.

All top-level `examples/*.mkpro` programs are runnable MK-Pro programs. They cover:

- `examples/basic.mkpro`: minimal input/output and arithmetic.
- `examples/tiny-game.mkpro`: tiny menu-style loop.
- `examples/lunar.mkpro`: numeric landing game with counters and touchdown rules.
- `examples/human.mkpro`: small counter game used as syntax smoke test.

`examples/spatial-drafts/*.mkpro` contains source-level board/world game ports.
They are intentionally not runnable examples yet: the compiler rejects them
until their spatial rules lower from the AST into ordinary IR.

## Documentation

MK-61 reference and programming notes live in [`docs/`](./docs/README.md).
Start with [docs/01-programming.md](./docs/01-programming.md) for RPN basics,
then follow the reading order in [docs/README.md](./docs/README.md).

## Development

```sh
npm run typecheck
npm test
```

The test suite covers the parser, the opcode catalog, V2 lowering, example
program reports, and the headless emulator loader smoke test.

## Status

- `compileSwitch` evaluates the discriminant once and stores it in a
  reserved scratch register; nested switches each get their own.
- Conditional branches follow the MK-61 convention "true falls through, false
  jumps to the address".
- Peephole optimization for redundant `X->П r ; П->X r` pairs at synthetic
  boundaries in compiler-generated lowering.
- MK-Pro lowers high-level `program`, `state`, `screen`, `read`, `match`,
  `challenge`, counter updates, and rule calls through ordinary compiler IR.
- JSON reports include IR stats, layout cell roles, candidate lowerings, and
  a budget summary.
- JSON/explain reports include an automatic optimizer capability matrix:
  active rules, considered candidates, and planned verifier hooks for
  Danilov-era MK-61 quirks.
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
