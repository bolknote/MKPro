import { describe, expect, it } from "vitest";
import { ParseError, parseProgram } from "../../src/core/index.ts";

describe("parser", () => {
  it("requires one V2 program block", () => {
    expect(() => parseProgram("")).toThrow(/one V2 program block/u);
  });

  it("rejects unknown top-level syntax", () => {
    for (const source of [
      "target mk61",
      "target bk0010",
      "budget 105 cells",
      "budget steps <= 20",
      "machine mk61",
      "entry main {",
      "store x = 1",
      "preload R9 = random_seed()",
      "allow undocumented",
      "resource strength {",
    ]) {
      expect(() => parseProgram(`${source}\n`)).toThrow(/Unexpected top-level line/u);
    }
  });

  it("rejects extra tokens in expressions", () => {
    expect(() =>
      parseProgram(`
program BadExpression {
  state {
    score: counter 0..9 = 0
  }
  loop {
    score = 1 2
  }
}
`),
    ).toThrow(
      ParseError,
    );
  });

  it("strips comments", () => {
    const ast = parseProgram(`
# also a comment
program Comments {
  loop {
    halt(1) // trailing
  }
}
`);
    expect(ast.v2?.name).toBe("Comments");
  });

  it("parses human-centered MK-Pro programs", () => {
    const ast = parseProgram(`
program Demo {
  state {
    score: counter 0..9 = 0
  }

  loop {
    show(score)
    key = read()
    match key {
      1 => inc()
      otherwise => halt(0)
    }
  }
  fn inc() {
    score++
  }
}
`);
    expect(ast.v2?.name).toBe("Demo");
    expect(ast.states[0]?.fields.some((field) => field.name === "key")).toBe(true);
    const loop = ast.entries[0]?.body[0];
    expect(loop?.kind).toBe("loop");
    if (loop?.kind !== "loop") throw new Error("expected loop");
    expect(loop.body.some((statement) => statement.kind === "if")).toBe(true);
  });

  it("parses display text fragments as ordinary show items", () => {
    const ast = parseProgram(`
program BeerScreen {
  state {
    bottles: counter 0..99 = stack.X
  }

  loop {
    show("BEEr", bottles:02)
  }
}
`);

    expect(ast.displays[0]?.items).toEqual([
      { kind: "literal", text: "BEEr", line: 8 },
      { kind: "source", name: "bottles", width: 2, pad: "zero", line: 8 },
    ]);
    expect(ast.displays[0]?.sources).toEqual(["bottles"]);
  });

  it("parses empty display string fragments", () => {
    const ast = parseProgram(`
program EmptyScreen {

  loop {
    show("")
  }
}
`);

    expect(ast.displays[0]?.items).toMatchObject([
      { kind: "literal", text: "" },
    ]);
    expect(ast.displays[0]?.sources).toEqual([]);
  });

  it("parses bare empty show statements", () => {
    const ast = parseProgram(`
program BareEmptyScreen {

  loop {
    show()
  }
}
`);

    expect(ast.displays[0]?.items).toEqual([]);
    expect(ast.displays[0]?.sources).toEqual([]);
  });

  it("uses commas as display separators without adding spaces", () => {
    const ast = parseProgram(`
program CounterScreen {
  state {
    a: counter 0..9 = 2
    b: counter 0..9 = 5
  }

  loop {
    show(a, b)
  }
}
`);

    expect(ast.displays[0]?.items).toEqual([
      { kind: "source", name: "a", line: 9 },
      { kind: "source", name: "b", line: 9 },
    ]);
  });

  it("requires commas between adjacent display fragments", () => {
    expect(() => parseProgram(`
program StatusScreen {
  state {
    die: counter 1..6 = 1
    turn_score: counter 0..99 = 0
  }

  loop {
    show(die ".-" turn_score:02)
  }
}
`)).toThrow(/Display fragments must be separated by commas/u);
  });

  it("parses explicitly separated display fragments", () => {
    const ast = parseProgram(`
program StatusScreen {
  state {
    die: counter 1..6 = 1
    turn_score: counter 0..99 = 0
  }

  loop {
    show(die, ".-", turn_score:02)
  }
}
`);

    expect(ast.displays[0]?.items).toEqual([
      { kind: "source", name: "die", line: 9 },
      { kind: "literal", text: ".-", line: 9 },
      { kind: "source", name: "turn_score", width: 2, pad: "zero", line: 9 },
    ]);
  });

  it("parses bare decimal display fragments", () => {
    const ast = parseProgram(`
program NumericFragments {
  state {
    a: counter 0..9 = 2
    b: counter 0..9 = 3
  }

  loop {
    show(123, a, b, 1)
  }
}
`);

    expect(ast.displays[0]?.items).toEqual([
      { kind: "literal", text: "123", line: 9 },
      { kind: "source", name: "a", line: 9 },
      { kind: "source", name: "b", line: 9 },
      { kind: "literal", text: "1", line: 9 },
    ]);
  });

  it("parses inline state displays when show() names a state field", () => {
    const ast = parseProgram(`
program InlineStateShow {
  state {
    score: counter 0..99 = 7
  }
  loop {
    show(score)
  }
}
`);

    expect(ast.displays.some((display) => display.sources.includes("score"))).toBe(true);
    const loop = ast.entries[0]?.body[0];
    expect(loop?.kind).toBe("loop");
    if (loop?.kind !== "loop") throw new Error("expected loop");
    expect(loop.body[0]).toMatchObject({ kind: "show" });
  });

  it("parses function-style calls with expression arguments", () => {
    const ast = parseProgram(`
program Demo {
  arena: board(0..9, 0..9)

  state {
    pos: coord(arena) = 1
    delta: counter -100..100 = 0
  }
  loop {
    step(direction(key))
  }
  fn step(delta) {
    pos += delta
  }
}
`);
    expect(ast.v2?.rules[0]?.params).toEqual(["delta"]);
    const loop = ast.entries[0]?.body[0];
    expect(loop?.kind).toBe("loop");
    if (loop?.kind !== "loop") throw new Error("expected loop");
    expect(loop.body).toEqual(expect.arrayContaining([
      expect.objectContaining({ kind: "assign", target: "delta" }),
      expect.objectContaining({ kind: "call", block: "step" }),
    ]));
  });

  it("shares guarded direction match actions with the common direction dispatch", () => {
    const ast = parseProgram(`
program GuardedDirection {
  state {
    key: packed = 0
    pos: coord(cave) = 1
  }
  cave: board(row_scan)
  loop {
    key = read()
    match key {
      4 => move_left()
      2, 6, 8, 5, -5 => go(direction(key))
      otherwise => ignored()
    }
  }
  fn move_left() {
    if pos == 7 {
      halt(77)
    }
    else {
      go(direction(key))
    }
  }
  fn go(dir) {
    pos += dir
  }
  fn ignored() {
    halt(0)
  }
}
`);
    const loop = ast.entries[0]?.body[0];
    expect(loop?.kind).toBe("loop");
    if (loop?.kind !== "loop") throw new Error("expected loop");
    const guarded = loop.body.find((statement) => statement.kind === "if");
    expect(guarded).toMatchObject({
      kind: "if",
      condition: {
        op: "==",
        right: { kind: "number", raw: "4" },
      },
    });
    const dispatch = loop.body.find((statement) => statement.kind === "dispatch");
    expect(dispatch).toMatchObject({ kind: "dispatch", name: "direction_dispatch" });
    if (dispatch?.kind !== "dispatch") throw new Error("expected direction dispatch");
    expect(dispatch.cases.map((dispatchCase) => JSON.stringify(dispatchCase.value))).not.toContain(JSON.stringify({ kind: "number", raw: "4" }));
  });

  it("marks guarded cardinal direction dispatches for the compact direction lowerer", () => {
    const ast = parseProgram(`
program CardinalDirection {
  state {
    key: packed = 0
    pos: packed = 0
  }
  loop {
    match key {
      2, 4, 6, 8 => step(direction(key))
      otherwise => halt(0)
    }
  }
  fn step(delta) {
    pos += delta
  }
}
`);
    const loop = ast.entries[0]?.body[0];
    expect(loop?.kind).toBe("loop");
    if (loop?.kind !== "loop") throw new Error("expected loop");
    const dispatch = loop.body[0];
    expect(dispatch?.kind).toBe("dispatch");
    if (dispatch?.kind !== "dispatch") throw new Error("expected dispatch");
    expect(JSON.stringify(dispatch.defaultBody)).toContain('"callee":"__direction_cardinal"');
  });

  it("rejects function calls with the wrong arity", () => {
    expect(() =>
      parseProgram(`
program BadMissingArg {
  loop {
    jump_to()
  }
  fn jump_to(floor) {
    halt(floor)
  }
}
`),
    ).toThrow(/Function 'jump_to' expects 1 argument, got 0/u);

    expect(() =>
      parseProgram(`
program BadExtraArg {
  loop {
    done(1)
  }
  fn done() {
    halt(0)
  }
}
`),
    ).toThrow(/Function 'done' expects 0 arguments, got 1/u);
  });

  it("specializes constant function arguments when that is cheaper than a shared parameter proc", () => {
    const ast = parseProgram(`
program Jump {
  state {
    floor: counter 1..4 = 1
    strength: counter 0..9 = 9
  }
  loop {
    match floor {
      1 => jump_to(2)
      2 => jump_to(3)
    }
  }
  fn jump_to(f) {
    floor = f
    strength -= f
  }
}
`);
    expect(ast.procs.some((proc) => proc.name === "jump_to")).toBe(false);
    expect(ast.states[0]?.fields.some((field) => field.name === "f")).toBe(false);
    const loop = ast.entries[0]?.body[0];
    expect(loop?.kind).toBe("loop");
    if (loop?.kind !== "loop") throw new Error("expected loop");
    const dispatch = loop.body[0];
    expect(dispatch?.kind).toBe("dispatch");
    if (dispatch?.kind !== "dispatch") throw new Error("expected dispatch");
    expect(dispatch.cases[0]?.body).toEqual([
      expect.objectContaining({ kind: "assign", target: "floor" }),
      expect.objectContaining({ kind: "assign", target: "strength" }),
    ]);
    const floorAssign = dispatch.cases[0]?.body[0];
    expect(floorAssign?.kind).toBe("assign");
    if (floorAssign?.kind !== "assign") throw new Error("expected floor assignment");
    expect(floorAssign.expr).toMatchObject({ kind: "number", raw: "2" });
  });

  it("lowers pure numeric match effect tables to default effects plus corrections", () => {
    const ast = parseProgram(`
program EffectTable {
  state {
    tile: counter 0..9 = 0
    energy: counter 0..9 = 9
  }
  loop {
    match tile {
      1 => pool_exit()
      2 => ladder_exit()
      3 => shaft_exit()
      otherwise => clear_exit()
    }
  }
  fn clear_exit() {
    energy++
  }
  fn pool_exit() {
    energy = 0
  }
  fn ladder_exit() {
    energy -= 5
  }
  fn shaft_exit() {
    energy -= 6
  }
}
`);
    const loop = ast.entries[0]?.body[0];
    expect(loop?.kind).toBe("loop");
    if (loop?.kind !== "loop") throw new Error("expected loop");
    expect(loop.body.map((statement) => statement.kind)).toEqual(["assign", "if", "if", "if"]);
    expect(loop.body.some((statement) => statement.kind === "dispatch")).toBe(false);
  });

  it("lowers single-key matches as ordinary conditionals", () => {
    const ast = parseProgram(`
program BinaryChoice {
  state {
    choice: counter 0..9 = 0
    score: counter 0..9 = 0
  }
  loop {
    match choice {
      0 => score++
      otherwise => score--
    }
  }
}
`);
    const loop = ast.entries[0]?.body[0];
    expect(loop?.kind).toBe("loop");
    if (loop?.kind !== "loop") throw new Error("expected loop");
    expect(loop.body[0]?.kind).toBe("if");
  });

  it("lowers exhaustive cyclic counter matches without a dispatch chain", () => {
    const ast = parseProgram(`
program Cycle {
  state {
    floor: counter 1..3 = 1
    strength: counter 0..9 = 9
  }
  loop {
    match floor {
      1 => jump_to(2)
      2 => jump_to(3)
      3 => jump_to(1)
    }
  }
  fn jump_to(f) {
    floor = f
    strength -= f
  }
}
`);
    const loop = ast.entries[0]?.body[0];
    expect(loop?.kind).toBe("loop");
    if (loop?.kind !== "loop") throw new Error("expected loop");
    expect(loop.body.map((statement) => statement.kind)).toEqual(["assign", "if", "assign"]);
    expect(ast.procs.some((proc) => proc.name === "jump_to")).toBe(false);
  });

  it("parses unary minus in normal expressions", () => {
    const ast = parseProgram(`
program Demo {
  state {
    value: counter -9..9 = 3
    result: counter -9..9 = 0
  }
  loop {
    result = -value
  }
}
`);
    const loop = ast.entries[0]?.body[0];
    expect(loop?.kind).toBe("loop");
    if (loop?.kind !== "loop") throw new Error("expected loop");
    const assignment = loop.body.find((statement) => statement.kind === "assign" && statement.target === "result");
    expect(assignment?.kind).toBe("assign");
    if (assignment?.kind !== "assign") throw new Error("expected result assignment");
    expect(assignment.expr.kind).toBe("unary");
    if (assignment.expr.kind !== "unary") throw new Error("expected unary expression");
    expect(assignment.expr.op).toBe("-");
    expect(assignment.expr.expr).toMatchObject({ kind: "identifier", name: "value" });
  });

  it("parses increment and decrement sugar as unit updates", () => {
    const ast = parseProgram(`
program Demo {
  state {
    score: counter 0..9 = 0
    food: counter 0..9 = 5
  }
  loop {
    score++
    food--
  }
}
`);
    const sourceLoop = ast.v2?.body[0];
    expect(sourceLoop?.kind).toBe("v2_loop");
    if (sourceLoop?.kind !== "v2_loop") throw new Error("expected source loop");
    expect(sourceLoop.body).toEqual([
      expect.objectContaining({ kind: "v2_update", target: "score", op: "+=", expr: "1" }),
      expect.objectContaining({ kind: "v2_update", target: "food", op: "-=", expr: "1" }),
    ]);
  });

  it("rejects non-canonical function syntax", () => {
    expect(() =>
      parseProgram(`
program Bad {
  loop {
    step(direction(key))
  }
  rule step(delta) {
    halt(0)
  }
}
`),
    ).toThrow(/Use 'fn name\(arg, \.\.\.\) \{'/u);

    expect(() =>
      parseProgram(`
program Bad {
  loop {
    step direction(key)
  }
  fn step(delta) {
    halt(0)
  }
}
`),
    ).toThrow(/Function calls must look like 'name\(\.\.\.\)'/u);

    expect(() =>
      parseProgram(`
program Bad {
  fn move(delta) {
    halt(0)
  }
  loop {
    halt(0)
  }
}
`),
    ).toThrow(/Function name 'move' is reserved/u);
  });

  it("keeps explicit otherwise branches when compacting direction calls", () => {
    const ast = parseProgram(`
program DirectionOtherwise {
  state {
    pos: counter -99..99 = 0
  }
  loop {
    match key {
      2, 4, 5, 6, 8 => step(direction(key))
      otherwise => wait()
    }
  }
  fn step(delta) {
    pos += delta
  }
  fn wait() {
    halt(0)
  }
}
`);
    const loop = ast.entries[0]?.body[0];
    expect(loop?.kind).toBe("loop");
    if (loop?.kind !== "loop") throw new Error("expected loop");
    const dispatch = loop.body[0];
    expect(dispatch?.kind).toBe("dispatch");
    if (dispatch?.kind !== "dispatch") throw new Error("expected dispatch");
    expect(dispatch.cases).toHaveLength(0);
    expect(dispatch.defaultBody).toEqual([
      expect.objectContaining({
        kind: "if",
        thenBody: expect.arrayContaining([expect.objectContaining({ kind: "call", block: "step" })]) as unknown,
        elseBody: [expect.objectContaining({ kind: "call", block: "wait" })],
      }),
    ]);
    expect(JSON.stringify(dispatch.defaultBody)).not.toContain(
      `"callee":"abs","args":[{"kind":"identifier","name":"key"}]`,
    );
  });

  it("parses compact board domains, cell sets, state config, and reference metadata", () => {
    const ast = parseProgram(`
reference demo_reference

program Demo {
  ocean: board(0..9, 0..9)
  state {
    pos: coord(demo_world) = 1
    strength: counter 0..99 = 40
    points: counter 0..9 = 0
    plans: cells(demo_world) = random()
    enemy_fleet: cells(ocean) = random()
    enemy_ships: counter 0..99 = stack.X
  }
  demo_world: board(decimal_player)

  loop {
    match key {
      0 => show(score)
      3 => score_point()
    }
  }
  fn score_point() {
    points++
  }
}
`);

    expect(ast.reference).toBe("demo_reference");
    expect(ast.v2?.boards[0]).toMatchObject({ name: "ocean", xMin: 0, xMax: 9, yMin: 0, yMax: 9, width: 10, height: 10 });
    expect(ast.v2?.worlds[0]?.name).toBe("demo_world");
    expect(ast.domains.some((domain) => domain.domainKind === "maze" && domain.name === "ocean")).toBe(true);
    expect(ast.domains.some((domain) => domain.domainKind === "bitset" && domain.name === "enemy_fleet")).toBe(true);
    expect(ast.states[0]?.fields.some((field) => field.name === "enemy_ships" && field.type === "range")).toBe(true);
    expect(ast.domains.some((domain) => domain.domainKind === "maze" && domain.name === "demo_world")).toBe(true);
    expect(ast.procs.some((proc) => proc.name === "score_point")).toBe(true);
  });

  it("lowers coord_list state to random-unique coordinate registers", () => {
    const ast = parseProgram(`
program Demo {
  field: board(0..9, 0..9)
  state {
    cell: coord(field)
    foxes: coord_list(field, 3) = random_unique()
    bearing: counter 0..3 = 0
  }

  loop {
    cell = read()
    if cell in foxes {
      show(score)
    }
    bearing = line_count(foxes, cell)
    show(score)
  }
}
`);

    expect(ast.v2?.state.find((field) => field.name === "foxes")).toMatchObject({ type: "coord_list", count: 3 });
    expect(ast.states[0]?.fields.map((field) => field.name)).toEqual(expect.arrayContaining([
      "__coord_list_foxes_0",
      "__coord_list_foxes_1",
      "__coord_list_foxes_2",
    ]));
    expect(JSON.stringify(ast.entries[0]?.body)).toContain("coord_list_has");
    expect(JSON.stringify(ast.entries[0]?.body)).toContain("coord_list_line_count");
  });

  it("parses and lowers move() expressions and named terminal functions", () => {
    const ast = parseProgram(`
program Demo {
  cave: board(0..9, 0..9)

  state {
    pos: coord(cave)
  }
  fn escaped() {
    halt(777)
  }
  loop {
    pos = move(pos, east)
    escaped()
  }
}
`);

    expect(ast.v2?.rules[0]).toMatchObject({ name: "escaped" });
    expect(ast.procs.find((proc) => proc.name === "escaped")?.body).toEqual([
      expect.objectContaining({ kind: "halt" }),
    ]);
    const loop = ast.entries[0]?.body[0];
    expect(loop?.kind).toBe("loop");
    if (loop?.kind !== "loop") throw new Error("expected loop");
    expect(loop.body).toEqual(expect.arrayContaining([
      expect.objectContaining({ kind: "assign", target: "pos" }),
      expect.objectContaining({ kind: "call", block: "escaped" }),
    ]));
  });

  it("parses human board and board query expressions", () => {
    const ast = parseProgram(`
program Queries {
  state {
    pos: coord(cave)
    foxes: cells(cave) = random()
    mines: cells(cave) = random()
    bearing: counter 0..8 = 0
    clue: counter 0..8 = 0
    tile: counter 0..9 = 0
    threat: coord(cave)
  }
  cave: board(row_scan)
  loop {
    bearing = line_count(foxes, pos)
    clue = neighbor_count(mines, pos)
    tile = cell_at(cave, pos)
    threat = random_cell(cave)
  }
}
`);
    const loop = ast.entries[0]?.body[0];
    expect(loop?.kind).toBe("loop");
    if (loop?.kind !== "loop") throw new Error("expected loop");
    const lowered = JSON.stringify(loop.body);

    expect(lowered).toMatch(/line_count/u);
    expect(lowered).toMatch(/neighbor_count/u);
    expect(lowered).not.toMatch(/cell_at/u);
    expect(lowered).toMatch(/digit_at/u);
    const sourceLoop = ast.v2?.body[0];
    const sourceAssignments = sourceLoop?.kind === "v2_loop"
      ? sourceLoop.body.filter((statement) => statement.kind === "v2_assign")
      : [];
    expect(sourceAssignments.map((statement) => statement.kind === "v2_assign" ? statement.expr : undefined)).toContain("random_cell(cave)");
  });

  it("lowers one-dimensional cell sets to decimal position masks", () => {
    const ast = parseProgram(`
program LineMask {
  life: board(1..7, 1..1)
  state {
    pos: coord(life) = 4
    hazards: cells(life) = random()
  }
  loop {
    hazards += pos
    if pos in hazards {
      halt(1)
    }
  }
}
`);
    const lowered = JSON.stringify(ast.entries[0]?.body);

    expect(lowered).toMatch(/pow10/u);
    expect(lowered).toMatch(/bit_or/u);
    expect(lowered).toMatch(/bit_and/u);
    expect(lowered).not.toMatch(/bit_set|bit_has/u);
  });

  it("lowers packed decimal zero-run cell sets to fractional cell masks", () => {
    const ast = parseProgram(`
program PackedCaveMask {
  state {
    pos: coord(cave) = 1.0000008
    walls: cells(cave) = random()
    floor: counter 0..9 = 0
  }
  cave: board(packed_decimal_zero_run)
  loop {
    walls -= pos
    if pos in walls {
      floor = pos.floor
    }
  }
}
`);
    const lowered = JSON.stringify(ast.entries[0]?.body);

    expect(lowered).toMatch(/frac/u);
    expect(lowered).toMatch(/bit_and/u);
    expect(lowered).not.toMatch(/bit_has|bit_clear/u);
    expect(lowered).toMatch(/"callee":"int","args":\[\{"kind":"identifier","name":"pos"\}\]/u);
  });

  it("rejects unknown program syntax and unknown functions", () => {
    expect(() =>
      parseProgram(`
program Bad {
  loop {
    end(missing)
  }
}
`),
    ).toThrow(/Unknown function 'end'/u);

    expect(() =>
      parseProgram(`
program Bad {
  ending done {
    show(1)
  }
  loop {
    done()
  }
}
`),
    ).toThrow(/Unexpected program line 'ending done \{'/u);

    expect(() =>
      parseProgram(`
program Bad {
  fn done() {
    halt(1)
  }
  fn done() {
    halt(2)
  }
  loop {
    done()
  }
}
`),
    ).toThrow(/Duplicate function 'done'/u);

    expect(() =>
      parseProgram(`
program Bad {
  loop {
    missing()
  }
}
`),
    ).toThrow(/Unknown function 'missing'/u);
  });

  it("rejects unknown setup and domain implementation blocks", () => {
    expect(() =>
      parseProgram(`
preload R9 = random_seed()
program Bad {
  loop {
    halt(0)
  }
}
`),
    ).toThrow(/Unexpected top-level line 'preload R9 = random_seed\(\)'/u);
    expect(() =>
      parseProgram(`
resource strength {
  register Ra
}
program Bad {
  loop {
    halt(0)
  }
}
`),
    ).toThrow(/Unexpected top-level line 'resource strength \{'/u);
  });

  it("rejects removed challenge blocks", () => {
    expect(() =>
      parseProgram(`
program Demo {
  state {
    tile: counter 0..9 = 0
  }
  loop {
    challenge tile as challenge using warning, memory, answer {
      success {
        halt(1)
      }
    }
  }
}
`),
    ).toThrow(/Use ordinary show\/read\/if statements instead of challenge blocks/u);
  });

  it("rejects old input declarations and bad target references", () => {
    expect(() =>
      parseProgram(`
program OldInput {
  input key: digit
  loop {
    halt(0)
  }
}
`),
    ).toThrow(/Unexpected program line 'input key: digit'/u);

    expect(() =>
      parseProgram(`
program BadEndingShow {
  fn done() {
    missing_screen()
    halt(0)
  }
  loop {
    done()
  }
}
`),
    ).toThrow(/Unknown function 'missing_screen'/u);

    expect(() =>
      parseProgram(`
program BadChallenge {
  state {
    tile: counter 0..9 = 0
  }

  loop {
    challenge tile as challenge using warning, memory, answer {
      success {
        halt(1)
      }
    }
  }
}
`),
    ).toThrow(/Use ordinary show\/read\/if statements instead of challenge blocks/u);
  });

  it("rejects unknown state field syntax", () => {
    expect(() =>
      parseProgram(`
program Bad {
  state {
    [use_X2] score: counter 0..9 = 0
  }
  loop {
    halt(0)
  }
}
`),
    ).toThrow(/State field must look/u);
  });

  it("keeps canonical rule semantics typed instead of raw text", () => {
    const ast = parseProgram(`
program Demo {
  state {
    pos: coord(cave) = 1
    next: coord(cave)
    walls: cells(cave) = random()
  }
  cave: board(row_scan)
  loop {
    halt(0)
  }
  fn advance() {
    next = pos + 1
    if next in walls {
      show(0)
    }
    else {
      pos = next
    }
    if pos != 0 {
      walls -= pos
      pos++
    }
    else {
      show(0)
    }
  }
}
`);
    const rule = ast.v2?.rules[0];
    expect(rule?.body.map((statement) => statement.kind)).toEqual([
      "v2_assign",
      "v2_if",
      "v2_if",
    ]);
    const conditional = rule?.body[1];
    expect(conditional?.kind).toBe("v2_if");
    if (conditional?.kind !== "v2_if") throw new Error("expected v2_if");
    expect(conditional.predicate).toMatchObject({
      kind: "v2_contains",
      collection: "walls",
      item: "next",
    });
  });

  it("parses raw blocks with an explicit stack and state contract", () => {
    const ast = parseProgram(`
program RawDemo {
  state {
    value: packed = 2
    result: packed = 0
  }
  loop {
    raw {
      takes Y = value, X = 3
      returns X -> result
      clobbers X, Y, X1
      preserves state
      code {
        +
        К ИНВ
      }
    }
    halt(result)
  }
}
`);
    const sourceLoop = ast.v2?.body[0];
    const raw = sourceLoop?.kind === "v2_loop" ? sourceLoop.body[0] : undefined;
    expect(raw?.kind).toBe("v2_raw");
    if (raw?.kind !== "v2_raw") throw new Error("expected v2_raw");
    expect(raw.inputs.map((input) => [input.slot, input.expr])).toEqual([
      ["Y", "value"],
      ["X", "3"],
    ]);
    expect(raw.outputs.map((output) => [output.slot, output.target])).toEqual([["X", "result"]]);

    const core = ast.entries[0]?.body[0]?.kind === "loop" ? ast.entries[0].body[0].body[0] : undefined;
    expect(core?.kind).toBe("core");
    if (core?.kind !== "core") throw new Error("expected core");
    expect(core.strict).toBe(true);
    expect(core.clobbers).toEqual(["X", "Y", "X1"]);
    expect(core.preserves).toEqual(["state"]);
  });

  it("rejects raw blocks without a state preservation contract", () => {
    expect(() =>
      parseProgram(`
program BadRaw {
  loop {
    raw {
      clobbers X
      code {
        +
      }
    }
  }
}
`),
    ).toThrow(/Raw block must declare preserves state/u);
  });

  it("rejects unknown statement syntax", () => {
    expect(() =>
      parseProgram(`
program Bad {
  loop {
    require pos != 0 else show 0
  }
}
`),
    ).toThrow(/Function calls must look like 'name\(\.\.\.\)'/u);
  });

  it("rejects removed screen blocks", () => {
    expect(() =>
      parseProgram(`
program Bad {
  state {
    score: counter 0..9 = 0
  }
  screen main {
    show(score)
    style compact digits
  }
  loop {
    show(score)
  }
}
`),
    ).toThrow(/Use 'fn name\(\) \{ show\(\.\.\.\) \}' instead of screen blocks/u);
  });

  it("rejects unknown state and removed board/fleet config syntax", () => {
    expect(() =>
      parseProgram(`
program Bad {
  state {
    walls: cells(cave) generated random
  }
  loop {
    halt(0)
  }
}
`),
    ).toThrow(/State field must look/u);

    expect(() =>
      parseProgram(`
program Bad {
  state {
    walls: cells(cave) {
      generated random
    }
  }
  loop {
    halt(0)
  }
}
`),
    ).toThrow(/State field must look/u);

    expect(() =>
      parseProgram(`
program Bad {
  board ocean: 10x10 {
  }
  loop {
    halt(0)
  }
}
`),
    ).toThrow(/Board must look like 'name: board\(0\.\.9, 0\.\.9\)'/u);

    expect(() =>
      parseProgram(`
program Bad {
  fleet enemy_fleet on ocean {
  }
  loop {
    halt(0)
  }
}
`),
    ).toThrow(/Fleet blocks were removed/u);
  });

  it("rejects unknown state field tails", () => {
    expect(() =>
      parseProgram(`
program Bad {
  state {
    pos: coord = 0
  }
  loop {
    halt(0)
  }
}
`),
    ).toThrow(/coord state must look like 'name: coord\(domain\)'/u);

    expect(() =>
      parseProgram(`
program Bad {
  state {
    score: counter() 0..9 = 0
  }
  loop {
    halt(0)
  }
}
`),
    ).toThrow(/State type 'counter' does not take parameters/u);

    expect(() =>
      parseProgram(`
program Bad {
  state {
    pos: coord(missing) = 0
  }
  loop {
    halt(0)
  }
}
`),
    ).toThrow(/Unknown domain 'missing'/u);

    expect(() =>
      parseProgram(`
program Bad {
  state {
    blocked: coord(cave) optional
  }
  loop {
    halt(0)
  }
}
`),
    ).toThrow(/State field must look/u);
  });

  it("rejects old input.X/input.Y startup stack syntax", () => {
    expect(() =>
      parseProgram(`
program Bad {
  cave: board(0..9, 0..9)

  state {
    pos: coord(cave) = input.X
  }
  loop {
    halt(0)
  }
}
`),
    ).toThrow(/Use 'stack.X' for startup stack values/u);
  });

  it("rejects unknown state types", () => {
    for (const [field, message] of [
      ["code: digit = 0", /Unknown state type 'digit'/u],
      ["fuel: resource 0..9 = 4", /Unknown state type 'resource'/u],
      ["tile: enum = 0", /Unknown state type 'enum'/u],
      ["jump: addr = 0", /Unknown state type 'addr'/u],
    ] as const) {
      expect(() =>
        parseProgram(`
program Bad {
  state {
    ${field}
  }
  loop {
    halt(0)
  }
}
`),
      ).toThrow(message);
    }
  });
});
