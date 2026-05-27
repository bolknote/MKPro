# M61 V2 Human DSL

V2 sources describe game rules in one place. The source says what the game is;
the compiler report says which MK-61 tactics were selected to fit it into 105
cells. Registers, preloads, overlay permissions, hex mantissa tricks, and random
backend details are not user-facing syntax.

```m61
target mk61
budget 105 cells
optimize size
reference lordbss_pmk198_giants_country

program GiantsCountry {
  input action: digit
  input answer: number

  state {
    [displayed] strength: resource 0..99 = 40 {
      terminal at 0 show error
    }

    [persistent] plans: bitset {
      generated random
      cleared when creature defeated
    }
  }

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
}
```

## Top Level

Only compilation metadata belongs outside `program`:

- `target mk61`
- `budget 105 cells`
- `optimize size`
- `reference name`

`benchmark`, `preload`, top-level `resource`, `bitset`, `maze`, `event`,
`random`, and `table packed code_overlay` are obsolete for v2 sources. Move
their meaning into `program/state/world/encounters`; the compiler report will
show the selected implementation tactics.

## Current Surface

Inside `program`, v2 now has one construct per game concept:

- `input` declares player input.
- `state` declares observable, persistent, temporary, generated, and terminal
  game values.
- `screen` declares reusable display layouts.
- `ending` declares named terminal outcomes.
- `world` declares movement spaces.
- `board` and `fleet` declare fixed-board games.
- `encounters` declares event tables.
- `turn` and `rule` declare flow.

The intended style is semantic: use `move`, `win`, `lose`, `end`, collection
operations, rewards, and formulas. Avoid spelling implementation facts such as
register placement, preloads, bit masks, code overlays, and hand-written
coordinate deltas in user programs.

## State Blocks

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

## Board And Fleet Blocks

Games that are about shots or probes on a board should not fake a movement
world. Use `board` for the coordinate system and `fleet` for a generated set of
occupied cells with a remaining-piece counter:

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

The compiler lowers this to the same packed board/mask/resource intent used by
the compact backend. The source stays about the game.

`examples/sea-battle.m61` is the reference board/fleet example. It models two
generated fleets, two ship counters, calculator shots, player shots, and victory
endings without exposing bit masks, magic stop numbers, or register allocation in
the source.

## Endings

Declare named outcomes once and use them from rules:

```m61
ending player_win {
  show -8
}

ending enemy_win {
  show 33333331
}

rule player_fire {
  if enemy_ships <= 0 {
    win player_win
  }
}
```

`win name`, `lose name`, and `end name` lower to the ending's display/stop
sequence. The verb is for the human reader; the compiler only needs the named
ending. A numeric `show` value can become the terminal value directly; a named
display is emitted as `show display` followed by a stop.
Ending names are checked strictly: duplicates and references to missing endings
are parse errors.

## World Blocks

`world` replaces the old split between coordinate, maze, and movement domains:

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

Movement should read as movement:

```m61
rule move_south {
  move player south remember blocked
  show cave_screen
}
```

Use `move pos north/south/east/west/up/down` for named directions, or
`move pos by expr` when the movement amount is itself part of the game formula.
`remember blocked` keeps the proposed destination in a named temporary for wall,
door, and collision handling.

The movement delta is chosen from the declared world position. Packed decimal
cave coordinates keep their old fractional deltas behind `move`; ordinary grids
use integer steps. That is why an example can say `move player south` instead of
`blocked = player + 0.0000002`.

## Formulas

Human formulas may use decimal constants inside ordinary arithmetic:

```m61
accel = burn * 10 / fuel - 9.8
height = height - speed - accel / 2
```

The compiler validates formulas with the normal expression parser before game
intent lowering. Decimal constants are therefore documented source syntax, not a
special case limited to standalone literals.

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

The parser lowers this to a generated `encounter(kind)` procedure with a compact
dispatch. The author writes encounter meaning; the compiler chooses dispatch
layout.

## Before And After

Old v2 mixed game facts with MK-61 tactics:

```m61
preload R9 = random_seed()
resource strength { register Ra initial 40 terminal_at 0 }
bitset plans { registers R1 R2 R3 generated_by calculator_random }
table packed code_overlay giant_country_tables { floor_plans may_overlay address_cells }
```

New v2 keeps that in the game language:

```m61
[displayed] strength: resource 0..99 = 40 {
  terminal at 0 show error
}

[persistent] plans: bitset {
  generated random
  cleared when creature defeated
}
```
