# MK-Pro Language

MK-Pro describes a compact calculator program in human terms: state, reads,
display output, loops, and functions. It does not ask the author to enable
dark entries, X2 tricks, overlays, or undocumented opcodes. Those are MK-61
machine capabilities, and the optimizer may use them whenever
its effect checks and emulator facts allow the rewrite.

The runnable examples in top-level `examples/*.mkpro` are intentionally written
in this V2 human DSL. MK-Pro source is one `program` block, plus optional report
metadata; standalone raw calculator listing syntax is not part of the language.

## Shape

```mkpro
program CounterGame {
  state {
    score: counter 0..9 = 0
    food: counter 0..9 = 5
  }

  fn main_screen() {
    show(score, food)
  }

  fn game_over() {
    halt(0)
  }

  loop {
    main_screen()
    key = read()

    match key {
      2 => gain()
      8 => spend()
      otherwise => game_over()
    }
  }

  fn gain() {
    score++
    main_screen()
  }

  fn spend() {
    food--
    main_screen()
  }
}
```

## No Hints

MK-Pro has no bracket hint syntax. If a word does not change the lowered program,
validation, or optimizer choices, it belongs in a comment, not in the language.
The compiler owns implementation choices such as X2, dark entries, overlays,
register placement, display bytes, and undocumented opcodes.

## Comments

MK-Pro accepts line and trailing comments with either `//` or `#`:

```mkpro
// A whole-line comment.
key = read()  # A trailing comment.
```

## Top Level

Only optional report metadata belongs outside `program`:

- `reference name`

Game meaning belongs inside `program`. Counters, cell sets, maps, events,
random, or packed-table facts should become ordinary declarations, `state`,
loops, matches, and functions. The compiler report shows which
registers, overlays, setup
constants, hex mantissas, random initialization, or other implementation tactics
were selected.

## Program Blocks

The current human DSL surface inside `program` is:

- `state { ... }` for game state, counters, masks, coordinates, and
  scratch fields.
- `name: board(0..9, 0..9)` for fixed-board coordinate domains.
- `name: board(encoding)` for compact generated coordinate domains.
- `loop { ... }` for the main game loop and `fn name(...) { ... }` for named
  actions.
- `name = read()` inside a loop or function for a player-entered value.

Functions should stay at the level of game actions: `player = move(player, south)`,
`safe_landing()`, normal assignments, comparisons, dispatch, and halts. The
lowerer turns those into assignments, display commands, dispatch, and stops.

Display output is a list of visible fragments. A fragment can be state or text:

```mkpro
state {
  bottles: counter 0..99 = stack.X
}

fn beer() {
  show("BEEr ", bottles:02)
}
```

Commas separate fragments; they do not add visible characters. `show(a, b)`
means `a` directly followed by `b`; write `show(a, " ", b)` for an explicit
space or `show(a, "-", b)` for `a-b`. Adjacent fragments without commas are a
syntax error. A source can request a fixed width with zero
padding, such as `bottles:02` or `score:03`; without an explicit width, the
counter range gives the natural visible width (`0..99` is two digits,
`0..9999` is four). The compiler decides whether that display becomes ordinary
numeric output, packed display bytes, sign-digit forms, `К ИНВ`, or another
proved MK-61 lowering.

Text fragments in `show(...)` use the MK-61 display alphabet: digits, spaces,
`-`, `L`, `С`, `Г`, and `Е` (Latin `C`, `D`/`G`, `E`, `O`, and Cyrillic
lookalikes are normalized where they map to calculator cells). Strings are
display-only; they are not runtime string values. A temporary assignment such
as `clue = "-"` is only propagated into a following `show(...)` when the
compiler can prove the value is used as a display fragment. This lets code write
patterns such as:

```mkpro
clue = "-"
show(room, " ", clue)
```

and still lower it as the display template `show(room, " -")`, without storing
`"-"` in a numeric register.

The lowering keeps short special cases first (`ЕГГОГ`, prebuilt literal video
strings, sign/exponent tricks, and other known compact forms). If no special
case applies, a generic display-cell builder handles fixed-width mixed screens
up to eight display cells, including combinations of numeric fields and literal
cells such as `show(room, " ", arrows, " ", clue)`, `show("L-L ", clue)`, and
`show("0 L", clue)`. Leading and trailing literal spaces are trimmed because the
calculator display has no stable visible edge space.

`show(...)` is a resumable visible stop. Use it for screens where the player
continues the program, often followed by `name = read()`. For terminal screens,
put the visible value directly in `halt(...)`.

Do not write setup or storage tactics as top-level implementation blocks:

```mkpro
preload R9 = 0.5
table packed code_overlay giant_country_tables { floor_plans may_overlay address_cells }
```

MK-Pro keeps real game facts in the game language:

```mkpro
strength: counter 0..99 = 40

plans: cells(giant_country) = random()
```

## State Configuration

State fields carry only data that affects the program:

```mkpro
state {
  strength: counter 0..99 = 40
  score: counter 0..99 = 0

  plans: cells(giant_country) = random()
}
```

Use `counter` for bounded numeric values, including consumables, scores, fuel,
and remaining-piece counts. Use `coord(domain)` for a cell coordinate,
`cells(domain)` for generated map, fleet, mine, wall, or encounter masks, and
`coord_list(domain, count)` for a fixed-size list of coordinates on a board.
The complete canonical state set is `flag`, `counter`, `coord(domain)`,
`cells(domain)`, `coord_list(domain, count)`, and `packed`. Register placement,
address storage, and compact packing are compiler decisions.

Use `packed` for ordinary numeric storage that is not a bounded counter: reserves,
random seeds, scratch fields, terminal video values, and other calculator-shaped
numbers that may include decimals or large magnitudes. A `counter` carries a visible
digit range; `packed` does not.

```mkpro
state {
  reserve: packed = 25000
  random_state: packed = 0.5
  scratch: packed = 0
}
```

Some original MK-61 games store several related facts in one register as a single
decimal value, such as `5000.999` for strength and distance together. Express that
as one `packed` field with the same literal; the compiler stores the value, not a
separate game model.

### Indexed State Banks

When several parallel arrays share the same index range, declare them as a state
group:

```mkpro
state {
  front_line: group(1..3) {
    front: packed = 5000.999
    robots: packed = 0
  }
}
```

Read and write group members with indexed access:

```mkpro
front = front_line[slot].front
robots = front_line[slot].robots
front_line[slot].front += scratch
front_line[2].robots = 0
```

The group range is inclusive. All members in one group must use the same range.
Nested groups are not supported, and a group member cannot also declare its own
`[min..max]` index range.

For a single indexed `packed` array, the bracket form is equivalent:

```mkpro
state {
  slots: packed[1..3] = 0
}

loop {
  slots[i] = x
  halt(slots[2])
}
```

Both forms lower to contiguous calculator registers. A constant index such as
`front_line[2].front` or `slots[2]` is resolved at compile time. A dynamic index
such as `front_line[slot].front` lowers through MK-61 indirect memory commands
(`К X->П` / `К П->X`) with a compiler-owned selector register. Indexed bank
elements cannot be initialized from `stack.X` or `stack.Y`.

When a loop stores through a running pointer and then increments it, for example
`slots[pointer + 1] = value` followed by `pointer++`, the compiler may fuse the
pair into one preincrement indirect store when the bank layout allows it.

Initial values can come from the startup stack registers `stack.X` and
`stack.Y`, for games where the player enters setup values before running the
program:

```mkpro
state {
  pos: coord(cave) = stack.X
  food: counter 0..99 = stack.Y
}
```

This is a startup convention, not general register binding. Memory registers
`R0`..`Re` are allocated by the compiler; source code should not name them for
state placement.

For emulator runs and regression tests, use the compiler's setup block instead
of hand-writing register assignments in source. The source keeps the natural
state facts above; `mk-pro explain` reports the allocated registers and an
emulator-ready block such as:

```text
`R3=0; R0=0; Rc=20`
```

That block is generated output. It is useful for loading or comparing compiled
programs, but it should not replace high-level state declarations.

Names beginning with `__mkpro_` are reserved for compiler-internal helper
expressions. User programs should express game rules directly; generated helper
names may appear in reports, but they are not part of the source language.

## Terminal Functions

Terminal outcomes are ordinary functions. Put the final display/halt sequence in
a named function and call that function like any other action:

```mkpro
fn safe_landing() {
  halt(777)
}

fn crash() {
  halt(666)
}

fn touchdown() {
  if abs(speed) <= 5 {
    safe_landing()
  }
  else {
    crash()
  }
}
```

Use `halt(value)` when the terminal display is a value, and `halt("text")` for
a terminal calculator video literal such as `halt("ЕГГОГ")` or
`halt("8СГ-Е-78")`. Do not write `show(...); halt()` for one final screen:
that describes two source-level effects even if a specific lowering can fuse
some cases. Single-use terminal functions are inlined, and shared terminal
functions can be lowered as direct jumps by the optimizer, so authors do not
need a separate terminal form for layout reasons. Function names must be unique,
and a call must reference a declared function.

## Value-Returning Functions (`return`)

An `fn` whose body contains `return expr` is a **value-returning function**: it
computes a value and hands it back to the caller. Such functions can be used in
expression position, including nested inside larger expressions and inside other
functions.

```mkpro
fn square(n) {
  return n * n
}

fn sum_of_squares(a, b) {
  return square(a) + square(b)
}

loop {
  x = read()
  y = read()
  result = sum_of_squares(x, y)
  halt(result)
}
```

Semantics and rules:

- The returned value is delivered in the calculator's X register. A call lowers
  to `ПП` (subroutine call); the function body ends with `В/О`, which returns to
  the caller with the result in X. The caller then consumes X directly (for
  example `result = f(x)` stores X into `result`, and `a + f(x)` adds X).
- A value-returning function must `return` on **every** control path. End each
  branch of an `if`/`match` with `return` (or `halt`); a function that can fall
  off the end is a compile error.
- `return` is only valid inside an `fn`. Using it in the `loop` block or the main
  body is a compile error.
- Recursion is not supported (the MK-61 keeps only a five-level subroutine return
  stack), so a function that calls itself directly or mutually is rejected.
- Calling a value-returning function as a plain statement is allowed; its result
  is simply discarded.

Nested calls are handled automatically. Because `ПП` clobbers the whole
X/Y/Z/T working stack, a call nested inside a larger expression (such as the
`square(a)` operands above) is hoisted into a short-lived temporary before the
surrounding expression is built, so a partially computed value is never
destroyed. The optimizer reclaims those temporaries when it can.

## Boards And Cell Sets

Use `board` when a game is about probes, shots, pieces, or generated movement
domains. Rectangular board coordinates are ranges, not a
separate `10x10` mini-syntax:

```mkpro
ocean: board(0..9, 0..9)

state {
  player_shot: coord(ocean)
  enemy_fleet: cells(ocean) = random()
  enemy_ships: counter 0..99 = stack.X
}
```

`board` describes the coordinate system. `cells(domain)` describes a generated
set of occupied cells. Counters that count remaining pieces are ordinary
`counter` fields, not hidden inside a special fleet block. This keeps games
such as Sea Battle, Minesweeper, fox hunting, or board puzzles from pretending
to be hallway movement games while still using the same declaration form as
other state.

Use `coord_list(domain, count)` when a game needs an exact number of distinct
board coordinates, not a bitmask. Fox Hunt on the MK-61 is the reference shape:

```mkpro
field: board(0..9, 0..9)

state {
  cell: coord(field)
  foxes: coord_list(field, 9) = random_unique()
  bearing: counter 0..9 = 0
}

loop {
  cell = read()

  if cell in foxes {
    found_fox()
  }

  bearing = line_count(foxes, cell)
  show("--", cell:02, "--", bearing)
}
```

The list name is game-facing state. The compiler lowers it to contiguous
coordinate registers and uses `coord_list_has(...)` and
`coord_list_line_count(...)` internally for membership and fox-hunt-style
row/column/diagonal scans. Initial values are `random_unique()` or `0`.
`random_unique()` is a startup initializer, not a call to `random()`: the
compiler fills the list with `count` unique coordinates on the board domain,
retrying draws until there are no collisions. Prefer it over
`cells(domain) = random()` when the game needs a known piece count without
overlap. See `examples/fox-hunt-mk61.mkpro` and contrast with
`examples/fox-hunt-100.mkpro`, which uses a random cell-set mask instead.

Board queries should name the geometric operation directly:

```mkpro
fn scan_foxes() {
  bearing = line_count(foxes, cell)
}

fn reveal_safe_cell() {
  clue = neighbor_count(mines, probe)
}
```

`line_count(cells, cell)` is the fox-hunt style row/column/diagonal count.
`neighbor_count(cells, cell)` is the Minesweeper-style 8-neighbor count. They
lower into ordinary mask/digit expressions, after which the optimizer can choose
shared arithmetic, masks, decimal digits, or a query tail.
For one-dimensional boards that fit in the mantissa, `cells(domain)` membership
and updates lower to decimal position masks such as `pow10(cell)` instead of the
general four-bit mask formula.

## Example Programs

The top-level repository examples are runnable MK-Pro programs:

- `basic.mkpro`, `human.mkpro`, and `tiny-game.mkpro` are small syntax and control-flow
  examples.
- `lunar.mkpro` is a numeric counter game with touchdown functions.
- `99-bottles.mkpro` is a direct port of the Bolknote beer demo.
- `alaram.mkpro`, `cave-sketch.mkpro`, `dangerous-loading.mkpro`,
  `dungeon.mkpro`, `game-100-pig.mkpro`, `minesweeper-9x9.mkpro`,
  `raja-yoga.mkpro`, and `sea-battle.mkpro` are game
  ports that lower through ordinary IR and fit within their references.

`examples/pending-optimizer/*.mkpro` contains ports that lower through ordinary
IR but are still too large for MK-61 or their reference. Source-level board and
cell queries are part of the language surface, not a separate draft bucket.

## Board Domains

`board` declares a coordinate domain. Rectangular boards use explicit ranges:

```mkpro
field: board(0..9, 0..9)
```

Compact generated boards use an encoding name:

```mkpro
giant_country: board(decimal_player)
```

This is a semantic commitment, not a storage hint: if the compiler has no lowerer
for that board operation, it rejects the program instead of selecting a canned
game template.

Movement functions should use `move(...)` when they mean movement rather than coordinate
arithmetic:

```mkpro
fn move_south() {
  player = move(player, south)
  cave_screen()
}

fn move_dir(dir) {
  next = player + dir
  if next in walls {
    blocked = next
    show(0)
  }
  else {
    player = next
  }
}
```

`move(pos, north/south/east/west/up/down)` lowers to the current compact coordinate
expression for the position's encoding. Assign the result back to the position:
`pos = move(pos, east)`. For example,
`packed_decimal_zero_run` uses the same small fractional deltas that older cave
sources wrote by hand, while generic grid movement uses integer deltas. When the
movement amount is deliberately computed, use the ordinary update form:
`pos += expr`.
If a function needs to remember a blocked destination, write that as normal state:
`next = player + dir`, then assign `blocked = next` only in the branch where a
wall was actually hit.

Board queries use the same expression position as ordinary formulas:

```mkpro
fn inspect_cell() {
  tile = cell_at(cave, pos)
}

fn move_monster() {
  threat = random_cell(harbor)
}
```

`cell_at(board, pos)` reads the generated tile/event code for a board position.
`random_cell(board)` asks the compiler for a compact random coordinate for that
board lowerer.

## Formulas

V2 formulas use the same expression parser as the compiler pipeline. Decimal
constants are valid inside larger formulas, so numeric games can write:

```mkpro
accel = burn * 10 / fuel - 9.8
```

The compiler validates that an expression can be lowered before it enters code
generation. It no longer rejects formulas simply because a decimal literal
appears next to variables or operators.

Formula helpers expose useful calculator-shaped operations without requiring
raw opcodes:

```mkpro
height = pow(base, exponent)
probe = bit_has(walls, 5)
walls = bit_set(walls, 5)
walls = bit_clear(walls, 5)
walls = bit_toggle(walls, 5)

probe = cell_has(walls, x, y)
walls = cell_set(walls, x, y)
walls = cell_clear(walls, x, y)
walls = cell_toggle(walls, x, y)

code = digit_at(display_value, 2)
display_value = digit_add(display_value, 1, 7)
display_value = digit_set(display_value, 2, 9)

near = near_any(room, 1, pit1, pit2)
hit = eq_any(room, bat1, bat2)

roll = int(random(0, 10))
shot = int(random(0, 100))
```

`random()` lowers to the MK-61 pseudorandom command (`К СЧ`) and yields a unit
fraction in `[0, 1)`. The command never produces `1`. `0` can appear only in
the same rare hexadecimal-`Y` stack cases as the underlying calculator
command. Use it in state initializers such as `cells(domain) = random()` and
in formulas that scale the draw, for example `int(random() * 9) + 1`.

`random(min, max)` is range sugar for `min + random() * (max - min)`. Only
zero- and two-argument forms are valid. The upper bound is exclusive; wrap
the call in `int(...)` when the game needs a whole number. `int(random(0, 10))`
yields `0..9`, and `int(random(0, 100))` yields `0..99`. Fractional bounds
are allowed when a continuous draw is intended, for example
`int(random(0.5, 2.5))`.

`pow(base, exponent)` lowers to the MK-61 `F x^y` command. `bit_mask(index)`
builds a zero-based packed bit mask: index `0` is `1`, `1` is `2`, `2` is `4`,
`3` is `8`, and `4` starts the next mantissa digit as `10`. `bit_has`,
`cell_has`, and the corresponding `clear`, `set`, and `toggle` helpers lower to
the blue logical operations where profitable. The `has` helpers return zero or
the matching mask, so tests should compare them with zero.

Packed digit helpers use one-based indexes from the right: `digit_at(value, 1)`
is the units digit, `digit_at(value, 2)` is the tens digit. `digit_add` adds a
digit at that position, while `digit_set` first removes the old digit at that
position and then inserts the new one.

Small coordinate-set helpers are ordinary expression macros. `near_any(value,
radius, a, b, ...) >= 0` means at least one candidate is within `radius`;
`near_any(...) < 0` means none are. `eq_any(value, a, b, ...) == 0` means the
value equals at least one candidate; `eq_any(...) != 0` means it equals none.

## Raw Blocks

Use `raw` only when a program really needs a calculator command sequence that
does not yet have a semantic helper. A raw block is allowed inside `loop`, `fn`,
`if`, and `match` bodies, but it must declare its contract:

```mkpro
fn invert_sum() {
  raw {
    takes Y = left, X = right
    returns X -> result
    clobbers X, Y, X1
    preserves state
    code {
      +
      К ИНВ
    }
  }
}
```

`takes` loads stack inputs before the raw code. Stack inputs must be contiguous
from `X`: `Y` requires `X`, `Z` requires `Y` and `X`, and `T` requires `Z`, `Y`,
and `X`. The compiler loads them bottom-to-top, so `takes Y = left, X = right`
enters the raw code with `X = right` and `Y = left`.

`returns X -> name` stores the final `X` value into a declared state field after
the raw code. `clobbers` records the stack/register/display side effects that the
author expects. `preserves state` is required: raw code must not mutate
compiler-owned state registers directly. Return high-level values through
`returns`, not by guessing register allocation.

Inside `code`, lines are MK-61 commands or two-digit hex opcodes understood by
the raw instruction parser, plus local labels written as `label:`. Unknown raw
commands are compile errors in contracted raw blocks. Raw instructions are an
optimizer barrier for the values they touch; the report records the contract and
marks raw cells as byte-readable constants.

The raw parser accepts the full `00`..`FF` opcode range as two-digit hex. It
also accepts documented command names and the common Anvarov command-reference
notation:

```mkpro
code {
  F π
  F√
  F x^{2}
  F x^{y}
  F↻
  ←→
  К°→′
  К°→′"
  К°←′"
  К∣x∣
  К∧
  К∨
  К⊕
}
```

ASCII spellings are equivalent where they are clearer in source control:
`F pi`, `F sqrt`, `F x^2`, `F x^y`, `F reverse`, `<->`, `К °->′`,
`К °<-′"`, `К |x|`, `К *`, and `К /`. Comparison aliases are normalized too:
`F x!=0` may be written as `F x≠0`, and `F x>=0` as `F x≥0`.

Branch raw commands take a following label or two-digit address:

```mkpro
code {
  F x≠0 retry
  БП done
retry:
  К БП 7
done:
  С/П
}
```

Memory commands accept canonical and compact forms. `X->П 3`, `X→П R3`, and
`хП3` are the same direct store; `П->X 3`, `П→X R3`, and `Пх3` are the same
recall. Indirect forms use the blue prefix, for example `К БП 7`, `К x≥0 e`,
`К X→П R4`, or compact `КБП7`, `КхП4`, `КПх4`.

For undocumented aliases and not-normally-entered commands, use the hex opcode
directly: `1F`, `2F`, `3D`, `3E`, `4F`, `5F`, `6F`, `7F`..`EF`, and
`F0`..`FF`. See `docs/03-command-reference.md` for the command catalog and
machine notes.

## Event Dispatch

Use `match` when a tile or event code selects one of several actions. If a case
needs several statements, put them in a function and call it from the case:

```mkpro
match tile {
  0 => cave()
  3 => event_3(tile)
}

fn event_3(question) {
  memory_code = question
  warning()
  memory()
  answer = read()
  if answer == memory_code {
    strength++
    score++
    plans -= pos
  }
  else {
    strength -= 3
  }
}
```

The author writes ordinary control flow. The compiler still chooses dispatch
layout, inlining, tail sharing, and branch removal.

## Optimizer Contract

The machine model is `mk61`. The compiler always runs the maximum optimizer and
automatically considers stack scheduling, indirect flow,
`FL0`..`FL3`, arithmetic branch removal, tail merging, `В/О` as one-cell
`БП 01`, code/data overlay, address constants, dark entries, R0/T aliases,
X2/display-byte packing, hex/sign mantissa forms, `F0`..`FF` no-ops, and
error-stop idioms.
Opcode `5F` is modeled separately from filler no-ops: in `mk61` it is a
raw display-state transform, not a hang and not a generic filler.

The optimizer is automatic. Source code never says "use dark entry" or "use
X2"; the compiler either proves the precondition and applies the shorter
lowering, or records the shorter candidate as rejected with the missing proof or
layout fact.

The source only states what is allowed semantically. The report explains:

- machine features actually used,
- static proofs and assumptions,
- emulator facts from the MK-61 model,
- rejected shorter candidates,
- external budget and hot blocks.

Unsupported high-level effects fail compilation instead of becoming comments.
There is no production spatial template path: spatial programs must lower from
their actual AST nodes through ordinary IR. Board/cell queries lower into
the same expression and control-flow IR as the rest of the language.

`reference` is report metadata only and must not change code generation. For
known `games/*` references, the report resolves the original listing, counts the
real addressed span (`max(address)+1`), occupied entries, and gaps, and keeps
`referenceSteps` equal to that span for existing report consumers. Missing
listings fall back to the budget with a warning.

For compiled programs the report marks selected tactics, not source switches:
indirect register flow, code/data overlay, X2/`ВП`, display-byte packing, and
raw machine facts appear only when a real IR lowering or optimizer pass selected
them. Super-dark dispatch and cyclic/dark layout stay as rejected candidates
unless the layout verifier proves both halves: FA..FF entry/continuation cells
and a dispatch register containing a proved FA..FF selector.

Documented capabilities such as `branch-removal`, `arithmetic-if-*`,
`zero-condition-test`, `dispatch-compare-chain`, and `fl-decrement-branch` are
reported `active` when their literal rewrite fires. This keeps the capability
report tied to proof-backed optimizer behavior rather than to template paths.

## Current Generic Source Nodes

The parser keeps these high-level statements as typed source nodes:

- `show(...)`
- `name = read()`
- `halt()`, `halt(expr)`, or `halt("text")`
- `name = expr`
- `name += expr`
- `name -= expr`
- `name++`
- `name--`
- `if predicate { ... } else { ... }`
- `while predicate { ... }`
- `loop { ... }`
- `name = move(pos, direction)`
- `fn_name(arg1, arg2)`
- `match expr { values => action }`
- query expressions: `line_count(set, cell)`, `neighbor_count(set, cell)`,
  `cell_at(board, pos)`, and `random_cell(board)`
- formula helpers: `pow`, `bit_mask`, `bit_has`, `bit_set`, `bit_clear`,
  `bit_toggle`, `cell_mask`, `cell_has`, `cell_set`, `cell_clear`,
  `cell_toggle`, `digit_at`, `digit_add`, `digit_set`, `near_any`, and `eq_any`
- contracted `raw { ... }` blocks for explicit MK-61 command sequences

Assignments, updates, comparison predicates, function parameters, loops, and
function calls lower through the generic compiler path. Query expressions are captured as
spatial query nodes before code generation. A `loop` is a real loop, and `fn` blocks
compile as `ПП`/`В/О` procedures, direct terminal jumps, or inline code depending
on the compiler's cost model and termination analysis. Calls must pass exactly
the parameters declared by the function.

```mkpro
fn jump_to(floor) {
  current_floor = floor
  strength -= floor
}

fn jump_floor() {
  match current_floor {
    1 => jump_to(2)
    2 => jump_to(3)
    3 => jump_to(1)
  }
}
```

When all calls to a small parameterized function use literal values and duplicating
the body is cheaper than storing the parameter and calling a shared subroutine,
the lowerer specializes those calls automatically.

Statement-level actions use the same call shape as the rest of the language:
write `go(direction(key))`, because `go` is a function call and
`direction(key)` is an expression. Built-in names such as `move`, `show`, `read`,
and `halt` cannot be used as function names.

Current scalar lowerings are still deliberately small and auditable:

- `coord` is represented as a packed numeric value; `pos.floor` lowers to
  `int(pos / 100)`, with `100` automatically placed in setup state when
  that is cheaper than entering three digits.
- `random()` is `[0, 1)` via `К СЧ`; `1` never appears, and `0` only in rare
  hexadecimal-`Y` stack cases.
- `random(min, max)` is syntax sugar for `min + random() * (max - min)`; the
  upper bound is exclusive. When `min` is known to be integer-valued,
  `int(random(min, max))` floors `random() * (max - min)` with `x - frac(x)`
  before adding `min`, avoiding `К [x]` immediately after `К СЧ`.
- `cells(domain) = random()` is moved to reported setup state by the optimizer.
- cell-set membership is written as `item in collection`; the lowering keeps the
  compact comparison form internally when that is smaller.
- rewards are ordinary updates such as `treasure += expr`.
- `direction(key)` lowers through a shared keypad geometry decoder. It no
  longer treats the key value itself as the movement delta.

## Implemented Size Rewrites

The optimizer currently performs these real rewrites, not just report-only
candidates:

- store/recall peephole: removes immediate `X->П r ; П->X r`;
- branch-removal umbrella: replaces matched conditionals with branchless
  arithmetic, `К max`, `К |x|`, sign transforms, or boolean-masked updates;
- negative-zero threshold selector: for bounded integer nonnegative threshold
  `if/else` forms, the compiler may use a compiler-owned `1|-00` preload and
  `В↑` normalization to produce a `0..1` selector when that is shorter than the
  ordinary branch. The report includes a setup program for the unusual preload;
  ordinary flow branches are also costed through this path, but kept unchanged
  whenever the normal condition is shorter;
- terminal branch simplification: `if/else` lowering omits an unreachable
  `БП if_end` after a terminal then-branch, and may branch directly to a
  reusable terminal function instead of landing on a one-command branch stub. For
  multi-command terminal `else` endings, the compiler may branch to a local
  terminal tail so the continuing `then` path does not pay a skip-over jump.
  A late layout check can try a more aggressive terminal-if lowering and keep
  it only when the full optimized layout is smaller;
- multi-update guard selection: one-legged `if` bodies made only of pure
  `+=`/`-=` updates may store one selector in a compiler scratch register and
  apply it to several updates when that beats the branch. Negative-zero
  threshold selectors are costed for the same update path but remain gated
  because ordinary branches are usually shorter there;
- cell membership clear reuse: when `if cell in cells` immediately clears that
  same cell, the compiler reuses the successful mask instead of recomputing it;
- adjacent bit-set reuse: consecutive `cells += cell` updates compute the cell
  bit mask once and apply it to each collection;
- repeated packed display helpers: repeated packed screens and repeated
  `show; show; read` prompt sequences are emitted once and called normally;
- pure expression helpers: repeated expensive pure expressions such as
  generated `digit_at(...)` bodies are shared as ordinary subroutines;
- remainder fraction lowering: `x - n * int(x / n)` lowers as `frac(x / n) * n`
  so generated digit/bit indexing does not recompute `x`;
- spatial count hit helpers: `neighbor_count` and `line_count` keep their
  query shape until code generation and share compiled hit-test helpers;
- spatial line-count loops: large `line_count` queries use compact hit loops
  and shared geometry helpers when several masks use the same board/cell;
  `arithmetic-if-terminal-select` covers both boolean-flag `==`/`!=` forms
  and full six-operator comparisons (`==`/`!=`/`<`/`<=`/`>`/`>=`) by lowering
  the predicate to a `0..1` selector via `sign`/`max`/`abs`. A cost gate keeps
  the branched form whenever it is shorter, so the rewrite never increases
  size. When the branchless form is rejected by the cost gate, the report
  records the rejection in `rejectedCandidates` (with branchless vs branched
  cell counts) and marks the matching capability `considered` rather than
  `candidate`, making it clear the rewrite was tried and not profitable;
- setup-state extraction: moves setup values outside official program cells and
  records the required calculator state in the report. Special compiler-owned
  setup values, such as `1|-00`, include `setupProgram`/`setupNote` fields in
  JSON and an extra setup line in `explain`;
- automatic constants: reserves spare registers for expensive constants such as
  `10`, `20`, and `100`;
- show/read fusion: `show(...)` followed by `key = read()` uses one calculator
  stop, not two;
- direction dispatch: groups many direction-key cases into one default
  movement path plus explicit rare commands;
- single-use function inlining: inlines functions called once and drops their
  `ПП`/`В/О` overhead;
- dispatch source reuse: `match key` reuses `key`'s register instead of
  allocating a second scratch register;
- zero-condition tests: comparisons against zero use the direct `F x?0`
  command instead of materializing `0` and subtracting;
- stack-current-X scheduling and dead-temp-store elimination: consumes the
  current `X` value directly when a commutative expression permits it;
- `F L0`..`F L3` unit decrement: counters assigned to `R0`..`R3` can lower
  `counter--` to a two-cell decrement-and-continue form;
- duplicate failure-tail merge: identical `show(0)` failure tails are shared;
- display stack reuse: packed display sources are reordered when the current
  `X` value is already one of the displayed values;
- jump threading: `БП label` is removed when `label` is the immediately next
  executable position;
- `В/О` replaces literal or label-resolved `БП 01` when the return stack is
  proven empty;
- redundant-prologue elimination: when a function ends with the same `display +
  С/П` block that the loop head opens with and then jumps to the head, the
  trailing copy is removed — the user only ever observes the show performed
  by the loop head. With the X-state tracker the pass also matches the
  "partial" backward prologue produced by `stack-current-X scheduling` against
  the loop head's full prologue (a virtual `П->X r` is prepended when the
  preceding op is `X->П r`). This is what shrinks `human.mkpro` 35→28 and
  `tiny-game.mkpro` 30→26 without any source edits.

Layout-sensitive tactics such as X2 display-byte scheduling, fractional-R0
sentinels, branch-removal arithmetic, super-dark FA..FF dispatch, dark/cyclic
layout, and raw `5F` display transforms are available only as proved optimizer
candidates. A real lowerer must first emit IR from the source semantics; the
optimizer may then apply a tactic when the IR proves the required liveness,
observability, address, and emulator facts. Without that proof, the candidate
remains rejected in the report.

## Unified IR Pipeline

Since the unified IR refactor, every compiled program flows through a single
typed intermediate representation (`IrOp[]`) before final cell resolution. The IR captures
semantic kinds (`store`, `recall`, `jump`, `cjump`, `call`, `loop`, `stop`,
`return`, `plain` …) rather than raw opcodes, so passes no longer guess
behavior from opcode ranges.

`lowerIrToMachine` and `raiseMachineToIr` round-trip `MachineItem[]`
losslessly; `raiseLayoutToIr` and `lowerIrToLayout` do the same for
`LayoutIrCell[]`. The supported example set is property-tested through the
machine IR path, while layout IR is tested independently as a data structure.

The pass driver in `src/core/passes/` runs the registered passes to a fixed
point (with a bounded iteration cap) and aggregates their `applied` counts
into the optimizer report. Passes that depend on fixed addresses or observable
display state must prove those constraints from the IR before they can rewrite
the program.

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
  coalescing, and dead-code analysis; `F L0`..`F L3` loop counters are modeled
  as both read and written registers.
- **dispatch-case-ordering** — moves unique numeric zero cases earlier when a
  high-level `match` can reuse the selector already in X.
- **jump-to-next-threading** — drops `БП label` immediately before `label`.
- **jump-thread** — chases jump-to-jump trampolines to the final target.
- **preloaded-indirect-flow** — for address-stable numeric branch/call
  targets, reserves a spare stable register as a compiler-owned address
  preload and emits one-cell `К БП`/`К ПП`/`К x?0`. The pass rewrites only
  branches whose numeric targets cannot be shifted by the removed address
  cell. Targets `00`..`47` use formal aliases `B2`..`F9`; targets `48`..`53`
  may use `FA`..`FF` only when the target cell is a proved one-command entry
  and the following direct jump already resumes at the matching `01`..`06`
  continuation.
- **dead-code-after-halt** — CFG reachability from the entry removes ops
  that no fall-through or jump edge reaches.
- **constant-folding** — folds numeric source-expression subtrees, deterministic
  constant primitive calls, and affine constant/variable expressions before code
  generation, then strips `0 +` and `1 *` identities in later IR.
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
- **duplicate-failure-tail-merge** — merges duplicate `show(0)` failure
  tails into a shared exit.
- **shared-call-tail** — coalesces repeated `ПП helper; БП continuation`
  pairs into one shared call tail when that is smaller.
- **return-suffix-gadget** — shares identical straight-line tails ending in
  `В/О` by jumping into one reusable suffix instead of duplicating it.
- **bit-mask-helper** — shares generated `bit_mask(index)` code when spatial
  membership and line-count lowering need the same packed-cell mask shape.
- **small-set-primitive-lowering** — lowers `near_any` and `eq_any` to compact
  arithmetic over small coordinate sets.
- **near-any-helper** — shares repeated `near_any(value, radius, ...)`
  candidate-distance checks through a stack-parameterized subroutine when the
  group is smaller than inlining.
- **random-cell-helper** — shares repeated single-axis `random_cell` arithmetic
  as a subroutine only when the estimated saving clears the normal helper
  threshold; each helper call still executes `random()` independently.
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
  `human.mkpro` 35→28 and `tiny-game.mkpro` 30→26 without touching the source.

A round-trip emulator regression suite (`tests/emulator/regression.test.ts`)
loads each of the 16 examples into the headless MK-61 emulator and runs a
small set of scenarios (input cycles, terminal stops, expected PC). The suite
runs at every phase of the IR pipeline as a regression gate, ensuring that
optimizations never break observable program behavior.
