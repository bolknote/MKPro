# M61 Language

M61 describes a compact calculator program in human terms: state, input,
screen output, rules, and semantic hints. It does not ask the author to enable
dark entries, X2 tricks, overlays, or undocumented opcodes. Those are target
profile capabilities of `mk61_exact`, and the optimizer may use them whenever
its effect checks and emulator facts allow the rewrite.

The examples in `examples/*.m61` are intentionally written in this V2 human
DSL. M61 source starts with `target mk61` and one `program` block; raw
calculator listing syntax is not part of the language.

## Shape

```m61
target mk61
budget 105 cells

program CounterGame {
  input key: digit
  input answer: number

  state {
    [displayed] score: counter 0..9 = 0
    [persistent] food: counter 0..9 = 5
  }

  screen main {
    show score, food
    style compact digits letters hex
  }

  ending game_over {
    show 0
  }

  turn {
    show main
    read key

    match key {
      2 => gain
      8 => spend
      otherwise => end game_over
    }
  }

  [hot] rule gain {
    score += 1
    show main
  }
}
```

## Hints

Hints describe semantics, not implementation:

- `[hot]`, `[rare]`, `[cold]` guide fallthrough and layout pressure.
- `[displayed]`, `[hidden]` describe observability.
- `[temporary]`, `[persistent]` describe liveness across turns.
- `[wrap]`, `[saturate]`, `[trap]` describe overflow/error policy.
- `[unordered]`, `[approx]`, `[exact]` describe legal rewrites.
- `[manual_entry]` describes values supplied by the player before the game loop.

Low-level hints such as `use_X2`, `use_dark_entries`, `put_in_R0`, or
`overlay_here` are rejected. The compiler owns those choices.

## Comments

M61 accepts line and trailing comments with either `//` or `#`:

```m61
// A whole-line comment.
input key: digit  # A trailing comment.
```

## Top Level

Only compilation metadata belongs outside `program`:

- `target mk61`
- `budget 105 cells`
- `reference name`

Game meaning belongs inside `program`. Resource, bitset, maze, event, random,
or packed-table facts should become `state`, `world`, `encounters`, rules,
screens, and semantic hints. The compiler report shows which registers, overlays, setup
constants, hex mantissas, random initialization, or other implementation tactics
were selected.

## Program Blocks

The current human DSL surface inside `program` is:

- `input name: digit|number` for values read from the player.
- `state { ... }` for game state, resources, scores, masks, coordinates, and
  scratch fields.
- `screen name { show ...; style ... }` for reusable display layouts.
- `ending name { show ... }` for named terminal outcomes.
- `board` and `fleet` for fixed-board games such as Sea Battle.
- `world` for movement games with coordinates, generated rooms, doors, walls,
  and vertical wrapping.
- `encounters expr { ... }` for event tables.
- `turn { ... }` for the main game loop and `rule name { ... }` for named
  actions.

Rules should stay at the level of game actions: `move player south`, `win
safe_landing`, `plans clear pos`, `reward by value`, and normal formulas. The
lowerer turns those into assignments, display commands, dispatch, and stops.

Do not write setup or storage tactics as top-level implementation blocks:

```m61
preload R9 = random_seed()
resource strength { register Ra initial 40 terminal_at 0 }
bitset plans { registers R1 R2 R3 generated_by calculator_random }
table packed code_overlay giant_country_tables { floor_plans may_overlay address_cells }
```

M61 keeps those facts in the game language:

```m61
[displayed] strength: resource 0..99 = 40 {
  terminal at 0 show error
}

[persistent] plans: bitset {
  generated random
  cleared when creature defeated
}
```

## State Configuration

State fields can carry their own game configuration:

```m61
state {
  [displayed] strength: resource 0..99 = 40 {
    terminal at 0 show error
  }

  [persistent] score: score 0..99 = 0 {
    reward skeleton 1
    reward dragon 6
  }

  [persistent] plans: bitset {
    generated random
    cleared when creature defeated
  }
}
```

Use `resource` for consumable values, `score` for accumulated rewards, and
`bitset` for generated map or encounter masks. Register placement and compact
packing are compiler decisions.

## Endings

Use `ending` for named outcomes instead of scattering stop-display numbers
through rules:

```m61
ending safe_landing {
  show 777
}

ending crash {
  show 666
}

rule touchdown {
  if abs(speed) <= 5 {
    win safe_landing
  }
  else {
    lose crash
  }
}
```

`win name`, `lose name`, and `end name` lower to the ending's display/stop
sequence. The names carry the game meaning; the number stays in one declaration.
If the ending's `show` value is numeric, the compiler can lower it directly to a
terminal value; otherwise it shows the named display and stops.
Ending names must be unique, and `win`/`lose`/`end` must reference a declared
ending. A typo is a parse error, not an implicit `stop 0`.

## Board And Fleet Blocks

Use `board` when a game is about probes, shots, or pieces on a fixed board
rather than movement through a world:

```m61
board ocean: 10x10 {
  coordinate two_digit 00..99
}

fleet enemy_fleet on ocean {
  ships enemy_ships 0..99 = input.X
  generated random
  cleared when player hit
  terminal at 0 show player_win
}
```

`board` describes the coordinate system. `fleet` describes a generated set of
occupied cells plus the resource that counts remaining pieces. This keeps games
such as Sea Battle, Minesweeper, fox hunting, or board puzzles from pretending
to be hallway movement games.

Board queries should name the geometric operation directly:

```m61
rule scan_foxes {
  bearing = count lines from foxes at cell
}

rule reveal_safe_cell {
  clue = count neighbors from mines around probe
}
```

`count lines from fleet at cell` is the fox-hunt style row/column/diagonal
count. `count neighbors from bitset around cell` is the Minesweeper-style
8-neighbor count. These lower to spatial query intent; the optimizer chooses
whether to use masks, decimal digits, or a shared query tail.

## Example Programs

The repository examples are grouped by the game shape they exercise:

- `basic.m61`, `human.m61`, and `tiny-game.m61` are small syntax and control-flow
  examples.
- `lunar.m61` is a numeric resource game with touchdown rules.
- `cave-sketch.m61`, `cave-highlevel-baseline.m61`, `cave-treasure.m61`,
  `cave-treasure-full.m61`, and `labyrinth777.m61` show increasingly complete
  cave/grid games.
- `grid-rescue.m61`, `resource-raid.m61`, and `giants-country.m61` exercise
  spatial resources, generated masks, encounters, and challenge blocks.
- `sea-battle.m61` demonstrates `board` and `fleet` for non-movement board
  games.
- `fox-hunt-100.m61`, `minesweeper-9x9.m61`, `treasure-hunter-2.m61`, and
  `dangerous-loading.m61` are larger Anvarov ports used to check how well the
  human DSL covers dense 105-cell MK-61 games.

## World Blocks

`world` describes spatial rules in one place:

```m61
world giant_country: hall {
  position pos {
    floors 1..3
    rooms 0..7
    display decimal_player
    start 1
  }

  generated random
  player decimal_point
  door symbol 8 costs strength 1
  wall symbol 8 blocks forward costs strength 7
  vertical wrap 1 -> 2 -> 3 -> 1
}
```

This is intentionally about world rules, not storage. The compiler lowers it to
the GameIntent pipeline and reports whether it used shared tails, code/data
overlay, constants as branch targets, X2, display-byte packing, or other MK-61
features.

Movement rules should use `move` when they mean movement rather than coordinate
arithmetic:

```m61
rule move_south {
  move player south remember blocked
  show cave_screen
}

rule move_up {
  move player up remember blocked
}
```

`move pos north/south/east/west/up/down` lowers to the current compact coordinate
update for the position's world display format. For example,
`packed_decimal_zero_run` uses the same small fractional deltas that older cave
sources wrote by hand, while generic grid movement uses integer deltas. `move pos
by expr` is available when the movement amount is deliberately computed.
`remember name` stores the proposed destination before assigning it, which keeps
collision and wall rules readable.

World queries use the same expression position as ordinary formulas:

```m61
rule inspect_cell {
  tile = cell from cave at pos
}

rule move_monster {
  threat = random position in harbor
}
```

`cell from world at pos` reads the generated tile/event code for a world
position. `random position in world` asks the compiler for a compact random
coordinate inside that world's declared range.

## Formulas

V2 formulas use the same expression parser as the compiler backend. Decimal
constants are valid inside larger formulas, so numeric games can write:

```m61
accel = burn * 10 / fuel - 9.8
```

The compiler validates that an expression can be lowered before it enters the
game-intent path. It no longer rejects formulas simply because a decimal literal
appears next to variables or operators.

## Encounter Tables

Use `encounters expr` when a tile or event code selects one of several rules:

```m61
encounters tile {
  0 empty {
    show cave
  }

  3 skeleton {
    challenge tile {
      success {
        strength += 1
        score += 1
        plans clear pos
      }
      failure {
        strength -= 3
      }
    }
  }
}
```

The parser lowers this to a generated `encounter(kind)` procedure with compact
dispatch. The author writes encounter meaning; the compiler chooses dispatch
layout.

## Optimizer Contract

The default target profile is `mk61_exact`. The compiler always runs the
maximum optimizer and automatically considers stack scheduling, indirect flow,
`FL0`..`FL3`, arithmetic branch removal, tail merging, `В/О` as one-cell
`БП 01`, code/data overlay, address constants, dark entries, R0/T aliases,
X2/display-byte packing, hex/sign mantissa forms, `F0`..`FF` no-ops, and
error-stop idioms.
Opcode `5F` is modeled separately from filler no-ops: in `mk61_exact` it is a
raw display-state transform, not a hang and not a generic filler.

The optimizer is automatic. Source code never says "use dark entry" or "use
X2"; the compiler either proves the precondition and applies the shorter
lowering, or records the shorter candidate as rejected with the missing proof or
layout fact.

The source only states what is allowed semantically. The report explains:

- machine features actually used,
- static proofs and assumptions,
- emulator facts from the target profile,
- rejected shorter candidates,
- budget and hot blocks.

Unsupported high-level effects fail compilation instead of becoming comments.
The current production path first classifies `GameIntent` by shape and features,
then selects the smallest valid backend candidate. Shape-specific microkernels
cover:

- `board_line_count` for fox-hunt row/column/diagonal probes;
- `board_neighbor_count` for Minesweeper-style 8-neighbor probes;
- `board_fleet_duel` for Sea Battle-style hidden fleet probes, hit reports,
  random board shots, and ship counters;
- `world_table` for compact generated-world tile lookup;
- `lane_resource` for one-dimensional movement with random hazards/resources.

The universal spatial/resource backend remains the fallback for mixed or
unsupported shapes: packed coordinates, generated bitsets, resources, events,
dispatch, screens, and terminal outcomes. `examples/cave-treasure.m61` is now
just one reference source; `examples/grid-rescue.m61`,
and `examples/resource-raid.m61` continue compiling through this fallback when
no shape-specific backend covers the features.

`reference` is report metadata only and must not change code generation. For
known `games/*` references, the report resolves the original listing, counts the
real addressed span (`max(address)+1`), occupied entries, and gaps, and keeps
`referenceSteps` equal to that span for existing report consumers. Missing
listings fall back to the budget with a warning.

For these programs the report marks selected tactics, not source switches:
indirect register flow, super-dark dispatch, cyclic/dark layout, code/data
overlay, X2/`ВП`, hex-mantissa data packing, R0 indirect behavior, `К ЗН`,
`К∨`, and `К max` are emitted as chosen lowerings through `GameIntent`,
`EffectIR`, `CandidateIR`, and `LayoutIR`.

Documented capabilities such as `branch-removal`, `arithmetic-if-*`,
`zero-condition-test`, `dispatch-compare-chain`, and `fl-decrement-branch` are
reported `active` not only when their literal rewrite fired, but also when the
GameIntent backend picked a semantic equivalent. `К max` zero-through counts as
the extrema selection, `К∨` and fractional indirect addressing count as the
zero/digit test, `super-dark dispatch` counts as the dispatch lowering,
`r0-indirect-counter` counts as the decrement-branch loop, and hex-mantissa
arithmetic counts as the masked conditional update. This keeps the capability
report honest about the optimizer's effective behavior across both backends.

## Current Generic Intent Nodes

The parser keeps these high-level statements as typed intent:

- `input name: digit`
- `input name: number`
- `let name = expr`
- `if predicate { ... } else { ... }`
- `require predicate else action`
- `collection clear item`
- `collection set item`
- `reward by expr`
- `challenge expr { success { ... } failure { ... } }`
- `move pos direction`
- `win|lose|end outcome`
- `match input { values => action }`
- query expressions: `count lines from set at cell`, `count neighbors from set
  around cell`, `cell from world at pos`, and `random position in world`

Numeric `let`, assignment, update, `exists`, comparison predicates,
`collection has`, `collection clear/set`, rule parameters, rule calls, and
`reward by expr` lower through the generic backend. Query expressions are
captured as spatial query intent before code generation. A `turn` is a real
loop, and `rule` blocks compile as `ПП`/`В/О` procedures unless later cost-model
passes decide to inline them.

## Human-Facing Game Sugar

Repeated game mechanics should be written once as intent, not expanded into the
same input/display/branch boilerplate everywhere.

`challenge` describes the common “show warning, show memory value, read answer,
then apply success/failure effects” pattern:

```m61
rule skeleton {
  challenge tile {
    success {
      strength += 1
      score += 1
      plans clear pos
    }
    failure {
      strength -= 3
    }
  }
}
```

This lowers as if the source had said:

```m61
challenge = memory_code(tile)
show warning
show memory
read answer
if answer == challenge {
  // success body
}
else {
  // failure body
}
```

The default names are deliberately conventional:

- generated value: `challenge`
- warning screen: `warning`
- memory screen: `memory`
- player answer input: `answer`

If the generated value needs a different temporary, use `as`:

```m61
challenge tile as monster_code {
  success { score += 1 }
  failure { strength -= 3 }
}
```

The construct is semantic sugar only. It does not request a particular MK-61
layout. The optimizer may still inline it, share tails, branch-remove the
success/failure effects, or route it through a GameIntent/EffectIR lowering.

Current scalar lowerings are still deliberately small and auditable:

- `coord` is represented as a packed numeric value; `pos.floor` lowers to
  `int(pos / 100)`, with `100` automatically placed in setup state when
  that is cheaper than entering three digits.
- `bitset generated random` is moved to reported setup state by the optimizer.
- `collection has item` currently lowers to a compact range-style mask test
  (`collection >= item`) until the full bitset solver lands.
- `reward by expr` updates `treasure` when that state field exists.
- `direction(key)` lowers through a shared keypad geometry decoder. It no
  longer treats the key value itself as the movement delta.

## Implemented Size Rewrites

The optimizer currently performs these real rewrites, not just report-only
candidates:

- store/recall peephole: removes immediate `X->П r ; П->X r`;
- branch-removal umbrella: replaces matched conditionals with branchless
  arithmetic, `К max`, `К |x|`, sign transforms, or boolean-masked updates;
  `arithmetic-if-terminal-select` covers both boolean-flag `==`/`!=` forms
  and full six-operator comparisons (`==`/`!=`/`<`/`<=`/`>`/`>=`) by lowering
  the predicate to a `0..1` selector via `sign`/`max`/`abs`. A cost gate keeps
  the branched form whenever it is shorter, so the rewrite never increases
  size. When the branchless form is rejected by the cost gate, the report
  records the rejection in `rejectedCandidates` (with branchless vs branched
  cell counts) and marks the matching capability `considered` rather than
  `candidate`, making it clear the rewrite was tried and not profitable;
- setup-state extraction: moves setup values outside official program cells and
  records the required calculator state in the report;
- automatic constants: reserves spare registers for expensive constants such as
  `10`, `20`, and `100`;
- show/read fusion: `show screen` followed by `read key` uses one calculator
  stop, not two;
- direction dispatch: groups many direction-key cases into one default
  movement path plus explicit rare commands;
- single-use rule inlining: inlines rules called once and drops their
  `ПП`/`В/О` overhead;
- dispatch source reuse: `match key` reuses `key`'s register instead of
  allocating a second scratch register;
- zero-condition tests: comparisons against zero use the direct `F x?0`
  command instead of materializing `0` and subtracting;
- stack-current-X scheduling and dead-temp-store elimination: consumes the
  current `X` value directly when a commutative expression permits it;
- `F L0`..`F L3` unit decrement: counters assigned to `R0`..`R3` can lower
  `counter -= 1` to a two-cell decrement-and-continue form;
- duplicate failure-tail merge: identical `show 0` failure tails are shared;
- display stack reuse: packed display sources are reordered when the current
  `X` value is already one of the displayed values;
- jump threading: `БП label` is removed when `label` is the immediately next
  executable position;
- `В/О` replaces literal or label-resolved `БП 01` when the return stack is
  proven empty;
- explicit `trap` lowers to Danilov-style domain-error stops only when the
  source semantics names a trap outcome;
- redundant-prologue elimination: when a rule ends with the same `display +
  С/П` block that the loop head opens with and then jumps to the head, the
  trailing copy is removed — the user only ever observes the show performed
  by the loop head. With the X-state tracker the pass also matches the
  "partial" backward prologue produced by `stack-current-X scheduling` against
  the loop head's full prologue (a virtual `П->X r` is prepended when the
  preceding op is `X->П r`). This is what shrinks `human.m61` 35→28 and
  `tiny-game.m61` 30→26 without any source edits.

The spatial/resource backend selects super-dark FA..FF dispatch, dark tables,
X2 display-byte scheduling, fractional-R0 sentinels, and branch-removal
arithmetic from the tactic registry automatically when the IR proves the
required layout, liveness, observability, and emulator-profile facts. Raw `5F`
display transforms remain modeled as target-profile capabilities and are only
legal when display semantics explicitly permit that raw display state.

## Unified IR Pipeline

Since the unified IR refactor, every program — both the regular intent
backend and the GameIntent layout backend — flows through a single typed
intermediate representation (`IrOp[]`) before final cell resolution. The IR captures
semantic kinds (`store`, `recall`, `jump`, `cjump`, `call`, `loop`, `stop`,
`return`, `plain` …) rather than raw opcodes, so passes no longer guess
behavior from opcode ranges.

`lowerIrToMachine` and `raiseMachineToIr` round-trip `MachineItem[]`
losslessly; `raiseLayoutToIr` and `lowerIrToLayout` do the same for
`LayoutIrCell[]`. Both round trips are property-tested across the 16 examples,
so the pipeline is byte-identical on every checkpoint.

The pass driver in `src/core/passes/` runs the registered passes to a fixed
point (with a bounded iteration cap) and aggregates their `applied` counts
into the optimizer report. Passes that would move already-laid-out cells
(anything that changes cell count or label addresses) are filtered out when
the GameIntent backend invokes the driver, so the layout's pinned addresses,
dark-entries, and overlays are preserved.

The pipeline currently contains:

- **store-recall-peephole** — drops adjacent `X->П r ; П->X r`.
- **stack-current-X / dead-temp-store** — eliminates the temp store when the
  current X value can be consumed directly by a following commutative op.
- **dead-store-elimination** — whole-program liveness-driven DSE: removes
  `X->П r` when liveOut at that point excludes `r`.
- **last-x-reuse** — drops `П->X r` when the IR proves X already holds the
  value last stored to `r` and no intervening op (С/П, jump, ALU, …) clobbers
  X. С/П acts as a barrier because the user may overwrite X during pause.
- **liveness-analysis** — foundational dataflow used by DSE, register
  coalescing, and dead-code analysis.
- **jump-to-next-threading** — drops `БП label` immediately before `label`.
- **jump-thread** — chases jump-to-jump trampolines to the final target.
- **dead-code-after-halt** — CFG reachability from the entry removes ops
  that no fall-through or jump edge reaches.
- **constant-folding** — strips `0 +` and `1 *` identities.
- **r0-fractional-sentinel** — removes redundant direct `R3` accesses after
  fractional `R0` indirect access when liveness proves `R0` is dead afterward.
- **vp-x2-peephole** — drops a `К {x}` immediately after a proved display
  `ВП`/X2 boundary when `ВП` already supplies the fractional transform.
- **cse-display-block** — coalesces structurally identical pure recall/ALU
  blocks that terminate with `В/О`, redirecting duplicates through a single
  shared exit.
- **register-coalesce** — rewrites direct store/recall opcodes when
  non-overlapping live ranges can share a physical register.
- **arithmetic-if-pass** — removes duplicated branch bodies after earlier
  simplifications prove both sides are byte-identical and the result is
  shorter.
- **duplicate-failure-tail-merge** — merges duplicate `show 0` failure
  tails into a shared exit.
- **return-zero-jump** — replaces `БП 01` with `В/О` only when the return
  stack is provably empty.
- **redundant-prologue-elimination** — when a `display + С/П` block
  immediately precedes `БП main_loop` and the loop head begins with the same
  byte-identical block, the trailing copy is removed. With the X-state tracker
  it also matches the "partial" backward prologue left behind by AST-level
  `stack-current-X scheduling`: when the op preceding the backward prologue
  is `X->П r`, the pass virtually prepends a `П->X r` and matches against the
  loop head's full prologue. The display the user actually sees is the one
  at the loop head; observable behavior is preserved. This is what shrinks
  `human.m61` 35→28 and `tiny-game.m61` 30→26 without touching the source.

A round-trip emulator regression suite (`tests/emulator/regression.test.ts`)
loads each of the 16 examples into the headless MK-61 emulator and runs a
small set of scenarios (input cycles, terminal stops, expected PC). The suite
runs at every phase of the IR pipeline as a regression gate, ensuring that
optimizations never break observable program behavior.
