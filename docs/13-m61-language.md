# M61 Language

M61 describes a compact calculator program in human terms: state, input,
screen output, rules, and semantic hints. It does not ask the author to enable
dark entries, X2 tricks, overlays, or undocumented opcodes. Those are target
profile capabilities of `mk61_exact`, and the optimizer may use them whenever
its effect checks and emulator facts allow the rewrite.

## Shape

```m61
target mk61
budget 105 cells
optimize size

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

  turn {
    show main
    read key

    match key {
      2 => gain
      8 => spend
      otherwise => stop 0
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
- `[manual_entry]`, `[preload]` describe values supplied outside program cells.

Low-level hints such as `use_X2`, `use_dark_entries`, `put_in_R0`, or
`overlay_here` are rejected. The compiler owns those choices.

## Comments

M61 accepts line and trailing comments with either `//` or `#`:

```m61
// A whole-line comment.
input key: digit  # A trailing comment.
```

## Preloads

`preload` describes setup values supplied before the program starts. Values are
either literals or explicit builtin function calls:

```m61
preload R4 = 2
preload R9 = random_seed()
```

Bare symbolic values such as `preload R9 = random_seed` are rejected. If a value
is computed, it must look like a call so the source reads as an expression, not
as a magic identifier.

## Optimizer Contract

The default target profile is `mk61_exact`. In `--opt max`, the compiler
automatically considers stack scheduling, indirect flow, `FL0`..`FL3`,
arithmetic branch removal, tail merging, `В/О` as one-cell `БП 01`, code/data
overlay, address constants, dark entries, R0/T aliases, X2/display-byte packing,
hex/sign mantissa forms, safe `F0`..`FF` no-ops, and error-stop idioms.
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
The current production path recognizes a general spatial/resource game intent:
packed coordinates, generated bitsets, resources, events, dispatch, screens,
and terminal outcomes. `examples/cave-treasure.m61` is now just one
benchmark source; the same universal pipeline also compiles
`examples/grid-rescue.m61` and `examples/resource-raid.m61` to the same
105-cell target. `benchmark` is report metadata only and must not change code
generation.

For these programs the report marks selected tactics, not source switches:
indirect register flow, super-dark dispatch, cyclic/dark layout, code/data
overlay, X2/`ВП`, hex-mantissa data packing, R0 indirect behavior, `К ЗН`,
`К∨`, and `К max` are emitted as chosen lowerings through `GameIntent`,
`EffectIR`, `CandidateIR`, and `LayoutIR`.

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
- `match input { values => action }`

Numeric `let`, assignment, update, `exists`, comparison predicates,
`collection has`, `collection clear/set`, rule parameters, rule calls, and
`reward by expr` lower through the generic backend. A `turn` is a real loop,
and `rule` blocks compile as `ПП`/`В/О` procedures unless later cost-model
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
  `int(pos / 100)`, with `100` automatically placed in a preload register when
  that is cheaper than entering three digits.
- `bitset generated random` is moved to reported preload/setup state in
  `--opt max`.
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
- auto-preload initial state: moves setup values outside official program
  cells and records the required registers in the report;
- automatic preloaded constants: reserves spare registers for expensive
  constants such as `10`, `20`, and `100`;
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
  source semantics names a trap outcome.

The spatial/resource backend selects super-dark FA..FF dispatch, dark tables,
X2 display-byte scheduling, fractional-R0 sentinels, and branch-removal
arithmetic from the tactic registry automatically when the IR proves the
required layout, liveness, observability, and emulator-profile facts. Raw `5F`
display transforms remain modeled as target-profile capabilities and are only
legal when display semantics explicitly permit that raw display state.
