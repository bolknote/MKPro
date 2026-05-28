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
  turn {
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
  turn {
    stop 1 // trailing
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
  screen main {
    show score
  }
  turn {
    show main
    read key
    match key {
      1 => inc
      otherwise => stop 0
    }
  }
  rule inc {
    score++
  }
}
`);
    expect(ast.v2?.name).toBe("Demo");
    expect(ast.states[0]?.fields.some((field) => field.name === "key")).toBe(true);
    const turn = ast.entries[0]?.body[0];
    expect(turn?.kind).toBe("loop");
    if (turn?.kind !== "loop") throw new Error("expected turn loop");
    expect(turn.body.some((statement) => statement.kind === "dispatch")).toBe(true);
  });

  it("parses screen text fragments as ordinary show items", () => {
    const ast = parseProgram(`
program BeerScreen {
  state {
    bottles: counter 0..99 = stack.X
  }
  screen beer {
    show "BEEr ", bottles
  }
  turn {
    show beer
  }
}
`);

    expect(ast.v2?.screens[0]?.items).toEqual([
      { kind: "literal", text: "BEEr ", line: 7 },
      { kind: "source", name: "bottles", line: 7 },
    ]);
    expect(ast.displays[0]?.sources).toEqual(["bottles"]);
  });

  it("parses command-style rule calls with expression arguments", () => {
    const ast = parseProgram(`
program Demo {
  arena: board(0..9, 0..9)

  state {
    pos: coord(arena) = 1
    delta: counter -100..100 = 0
  }
  turn {
    step direction(key)
  }
  rule step delta {
    pos += delta
  }
}
`);
    expect(ast.v2?.rules[0]?.params).toEqual(["delta"]);
    const loop = ast.entries[0]?.body[0];
    expect(loop?.kind).toBe("loop");
    if (loop?.kind !== "loop") throw new Error("expected turn loop");
    expect(loop.body).toEqual(expect.arrayContaining([
      expect.objectContaining({ kind: "assign", target: "delta" }),
      expect.objectContaining({ kind: "call", block: "step" }),
    ]));
  });

  it("rejects rule calls with the wrong arity", () => {
    expect(() =>
      parseProgram(`
program BadMissingArg {
  turn {
    jump_to
  }
  rule jump_to floor {
    stop floor
  }
}
`),
    ).toThrow(/Rule 'jump_to' expects 1 argument, got 0/u);

    expect(() =>
      parseProgram(`
program BadExtraArg {
  turn {
    done 1
  }
  rule done {
    stop 0
  }
}
`),
    ).toThrow(/Rule 'done' expects 0 arguments, got 1/u);
  });

  it("specializes constant rule arguments when that is cheaper than a shared parameter proc", () => {
    const ast = parseProgram(`
program Jump {
  state {
    floor: counter 1..3 = 1
    strength: counter 0..9 = 9
  }
  turn {
    match floor {
      1 => jump_to 2
      2 => jump_to 3
      3 => jump_to 1
    }
  }
  rule jump_to f {
    floor = f
    strength -= f
  }
}
`);
    expect(ast.procs.some((proc) => proc.name === "jump_to")).toBe(false);
    expect(ast.states[0]?.fields.some((field) => field.name === "f")).toBe(false);
    const loop = ast.entries[0]?.body[0];
    expect(loop?.kind).toBe("loop");
    if (loop?.kind !== "loop") throw new Error("expected turn loop");
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

  it("parses unary minus in normal expressions", () => {
    const ast = parseProgram(`
program Demo {
  state {
    value: counter -9..9 = 3
    result: counter -9..9 = 0
  }
  turn {
    result = -value
  }
}
`);
    const loop = ast.entries[0]?.body[0];
    expect(loop?.kind).toBe("loop");
    if (loop?.kind !== "loop") throw new Error("expected turn loop");
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
  turn {
    score++
    food--
  }
}
`);
    expect(ast.v2?.turn?.body).toEqual([
      expect.objectContaining({ kind: "v2_update", target: "score", op: "+=", expr: "1" }),
      expect.objectContaining({ kind: "v2_update", target: "food", op: "-=", expr: "1" }),
    ]);
  });

  it("rejects non-canonical rule syntax", () => {
    expect(() =>
      parseProgram(`
program Bad {
  turn {
    step direction(key)
  }
  rule step(delta) {
    stop 0
  }
}
`),
    ).toThrow(/Rule must look/u);

    expect(() =>
      parseProgram(`
program Bad {
  turn {
    step(direction(key))
  }
  rule step delta {
    stop 0
  }
}
`),
    ).toThrow(/Unexpected statement 'step\(direction\(key\)\)'/u);

    expect(() =>
      parseProgram(`
program Bad {
  rule move delta {
    stop 0
  }
  turn {
    stop 0
  }
}
`),
    ).toThrow(/Rule name 'move' is reserved/u);
  });

  it("parses v2 world, boards, cell sets, state config, encounters, and reference metadata", () => {
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
  world demo_world {
    position pos {
      encoding decimal_player
    }
  }
  screen main {
    show pos, strength
  }
  turn {
    encounter key
  }
  encounters key {
    0 {
      show main
    }
    3 {
      points++
    }
  }
}
`);

    expect(ast.reference).toBe("demo_reference");
    expect(ast.v2?.boards[0]).toMatchObject({ name: "ocean", xMin: 0, xMax: 9, yMin: 0, yMax: 9, width: 10, height: 10 });
    expect(ast.v2?.worlds[0]?.name).toBe("demo_world");
    expect(ast.v2?.encounters[0]?.cases.map((encounterCase) => encounterCase.value)).toEqual(["0", "3"]);
    expect(ast.domains.some((domain) => domain.domainKind === "maze" && domain.name === "ocean")).toBe(true);
    expect(ast.domains.some((domain) => domain.domainKind === "bitset" && domain.name === "enemy_fleet")).toBe(true);
    expect(ast.states[0]?.fields.some((field) => field.name === "enemy_ships" && field.type === "range")).toBe(true);
    expect(ast.domains.some((domain) => domain.domainKind === "maze" && domain.name === "demo_world")).toBe(true);
    expect(ast.procs.some((proc) => proc.name === "encounter")).toBe(true);
  });

  it("parses and lowers v2 move statements and named terminal rules", () => {
    const ast = parseProgram(`
program Demo {
  cave: board(0..9, 0..9)

  state {
    pos: coord(cave)
  }
  rule escaped {
    stop 777
  }
  turn {
    move pos east
    escaped
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

  it("parses human board and world query expressions", () => {
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
  world cave {
    position pos {
    }
  }
  turn {
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
    const calls = loop.body
      .filter((statement) => statement.kind === "assign")
      .map((statement) => statement.kind === "assign" && statement.expr.kind === "call" ? statement.expr.callee : undefined);

    expect(calls).toEqual(["line_count", "neighbor_count", "cell_at", "random_cell"]);
  });

  it("rejects unknown program syntax and unknown rules", () => {
    expect(() =>
      parseProgram(`
program Bad {
  turn {
    end missing
  }
}
`),
    ).toThrow(/Unknown rule 'end'/u);

    expect(() =>
      parseProgram(`
program Bad {
  ending done {
    show 1
  }
  turn {
    done
  }
}
`),
    ).toThrow(/Unexpected program line 'ending done \{'/u);

    expect(() =>
      parseProgram(`
program Bad {
  rule done {
    stop 1
  }
  rule done {
    stop 2
  }
  turn {
    done
  }
}
`),
    ).toThrow(/Duplicate rule 'done'/u);

    expect(() =>
      parseProgram(`
program Bad {
  turn {
    missing
  }
}
`),
    ).toThrow(/Unknown rule 'missing'/u);
  });

  it("rejects unknown setup and domain implementation blocks", () => {
    expect(() =>
      parseProgram(`
preload R9 = random_seed()
program Bad {
  turn {
    stop 0
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
  turn {
    stop 0
  }
}
`),
    ).toThrow(/Unexpected top-level line 'resource strength \{'/u);
  });

  it("parses challenge blocks as rule syntax", () => {
    const ast = parseProgram(`
program Demo {
  state {
    tile: counter 0..9 = 0
    score: counter 0..9 = 0
    strength: counter 0..9 = 9
  }
  screen warning {
    show tile
  }
  screen memory {
    show tile
  }
  turn {
    challenge tile as challenge using warning, memory, answer {
      success {
        score++
      }
      failure {
        strength -= 3
      }
    }
  }
}
`);
    const loop = ast.entries[0]?.body[0];
    expect(loop?.kind).toBe("loop");
    if (loop?.kind !== "loop") throw new Error("expected turn loop");
    expect(loop.body.some((statement) => statement.kind === "assign")).toBe(true);
    expect(loop.body.some((statement) => statement.kind === "if")).toBe(true);
  });

  it("rejects old input declarations and bad target references", () => {
    expect(() =>
      parseProgram(`
program OldInput {
  input key: digit
  turn {
    stop 0
  }
}
`),
    ).toThrow(/Unexpected program line 'input key: digit'/u);

    expect(() =>
      parseProgram(`
program BadEndingShow {
  rule done {
    show missing_screen
    stop 0
  }
  turn {
    done
  }
}
`),
    ).toThrow(/Unknown screen 'missing_screen'/u);

    expect(() =>
      parseProgram(`
program BadChallenge {
  state {
    tile: counter 0..9 = 0
  }
  screen warning {
    show tile
  }
  turn {
    challenge tile as challenge using warning, memory, answer {
      success {
        stop 1
      }
    }
  }
}
`),
    ).toThrow(/Unknown challenge memory screen 'memory'/u);
  });

  it("rejects unknown state field syntax", () => {
    expect(() =>
      parseProgram(`
program Bad {
  state {
    [use_X2] score: counter 0..9 = 0
  }
  turn {
    stop 0
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
  world cave {
    position pos {
    }
  }
  turn {
    stop 0
  }
  rule advance {
    next = pos + 1
    if next in walls {
      show 0
    }
    else {
      pos = next
    }
    if pos != 0 {
      walls -= pos
      pos++
    }
    else {
      show 0
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
      kind: "v2_compare",
      left: "walls",
      op: ">=",
      right: "next",
    });
  });

  it("parses raw blocks with an explicit stack and state contract", () => {
    const ast = parseProgram(`
program RawDemo {
  state {
    value: packed = 2
    result: packed = 0
  }
  turn {
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
    stop result
  }
}
`);
    const raw = ast.v2?.turn?.body[0];
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
  turn {
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
  turn {
    require pos != 0 else show 0
  }
}
`),
    ).toThrow(/Unknown rule 'require'/u);
  });

  it("rejects unknown screen lines", () => {
    expect(() =>
      parseProgram(`
program Bad {
  state {
    score: counter 0..9 = 0
  }
  screen main {
    show score
    style compact digits
  }
  turn {
    show main
  }
}
`),
    ).toThrow(/Unexpected screen line 'style compact digits'/u);
  });

  it("rejects unknown state and removed board/fleet config syntax", () => {
    expect(() =>
      parseProgram(`
program Bad {
  state {
    walls: cells(cave) generated random
  }
  turn {
    stop 0
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
  turn {
    stop 0
  }
}
`),
    ).toThrow(/State field must look/u);

    expect(() =>
      parseProgram(`
program Bad {
  board ocean: 10x10 {
  }
  turn {
    stop 0
  }
}
`),
    ).toThrow(/Board must look like 'name: board\(0\.\.9, 0\.\.9\)'/u);

    expect(() =>
      parseProgram(`
program Bad {
  fleet enemy_fleet on ocean {
  }
  turn {
    stop 0
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
  turn {
    stop 0
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
  turn {
    stop 0
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
  turn {
    stop 0
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
  turn {
    stop 0
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
  turn {
    stop 0
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
  turn {
    stop 0
  }
}
`),
      ).toThrow(message);
    }
  });
});
