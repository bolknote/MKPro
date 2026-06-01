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
mk-pro compile file.mkpro --out listing|hex|json|keys|all
mk-pro explain file.mkpro
```

Flags:

- `--delivery manual|loader|hex` (default `hex`) controls opcode delivery
  metadata for listings.
- `--out keys` prints a bare press/input stream, including generated setup when
  the program needs one.
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
state, reads, display output, loops, and functions. The compiler decides
whether that becomes registers, stack scheduling, address constants, dark
entries, overlays, X2/display bytes, or other MK-61 tricks.

```rust
program TinyGame {
  field: board(0..9, 0..9)

  state {
    player: coord(field) = 1
    food: counter 0..9 = 5
  }

  fn score(a, b) {
    return a * b + food
  }

  fn main() {
    show(player, ".", score(player, food))
  }

  loop {
    main()
    key = read()

    match key {
      2 => apply_step(10)
      8 => apply_step(-10)
      4 => apply_step(-1)
      6 => apply_step(1)
      otherwise => game_over()
    }
  }

  fn adjust_position(pos, step) {
    return pos + step
  }

  fn apply_step(delta) {
    player = adjust_position(player, delta)
    main()
  }

  fn game_over() {
    halt(0)
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

- `examples/99-bottles.mkpro`: compact text/count display demo.
- `examples/basic.mkpro`: minimal input/output and arithmetic.
- `examples/alaram.mkpro`: cockpit interceptor port that fits the original.
- `examples/cave-sketch.mkpro`: compact cave sketch with generated board movement.
- `examples/dangerous-loading.mkpro`: ferry/loading game whose natural default
  command branch compiles smaller than the original.
- `examples/dungeon.mkpro`: Lord_BSS corridor dungeon port that fits the original.
- `examples/e-94-digits.mkpro`: Anvarov e-digit calculation port.
- `examples/clock.mkpro`: compact HH:MM clock/chime loop restored from a known
  reference program.
- `examples/fox-hunt-100.mkpro`: Anvarov 10x10 Fox Hunt port.
- `examples/fox-hunt-mk61.mkpro`: MonatkoDenis MK-61 Fox Hunt port with a
  generated setup program and row/column/diagonal bearing clues.
- `examples/functions-demo.mkpro`: value-returning function composition demo.
- `examples/game-100-pig.mkpro`: Anvarov dice game port that fits the original.
- `examples/human.mkpro`: small counter game used as syntax smoke test.
- `examples/jack-pot.mkpro`: converted Lord_BSS pmk151 "Jack Pot" game with
  angle-mode dependent jackpot flow.
- `examples/lunar.mkpro`: numeric landing game with counters and touchdown rules.
- `examples/minesweeper-9x7.mkpro`: Lord_BSS Sapper port whose 9x7 paper-board
  probes match the original 104-cell listing.
- `examples/minesweeper-9x9.mkpro`: Anvarov Minesweeper port using shared
  board-hit helpers and fitting the original.
- `examples/raja-yoga.mkpro`: Anvarov one-dimensional mask game port that fits
  the original.
- `examples/rambo-iii.mkpro`: Lord_BSS Rambo-III three-front defense port that
  now fits the original 105-cell MK-61 budget without raw code.
- `examples/sea-battle.mkpro`: board/cell-set game port that fits the original.
- `examples/treasure-hunter-2.mkpro`: Anvarov Treasure Hunter 2 port with nine
  generated floors, ladders, holes, and exit behavior.
- `examples/tiny-game.mkpro`: tiny menu-style loop.
- `examples/wumpus.mkpro`: Hunt the Wumpus port that fits the MK-61 window.

`examples/pending-optimizer/*.mkpro` contains ports that lower through ordinary
IR but are still too large for MK-61 or their reference. There is no separate
pending-lowerers bucket: source-level queries must either lower or fail as
ordinary compiler bugs.

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

- `match` lowers to a `dispatch`: the discriminant is evaluated once and the
  compiler selects the cheapest residual compare-chain or scratch-register
  lowering for the cases.
- Conditional branches follow the MK-61 convention "true falls through, false
  jumps to the address".
- Peephole optimization for redundant `X->П r ; П->X r` pairs at synthetic
  boundaries in compiler-generated lowering.
- MK-Pro lowers high-level `program`, `state`, `loop`, `read`, `match`,
  `show`, counter updates, and function calls through ordinary compiler IR.
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
