import { describe, expect, it } from "vitest";
import { ParseError, parseProgram } from "../../src/core/index.ts";

describe("parser", () => {
  it("requires 'machine mk61'", () => {
    expect(() => parseProgram("entry main {\n}")).toThrow(ParseError);
  });

  it("requires at least one entry", () => {
    expect(() => parseProgram("machine mk61\n")).toThrow(ParseError);
  });

  it("rejects unsupported machine names", () => {
    expect(() => parseProgram("machine bk0010\nentry main {\n}")).toThrow(
      /Unsupported machine/u,
    );
  });

  it("parses storage hints with prefer/fixed and Latin/Cyrillic registers", () => {
    const ast = parseProgram(`
machine mk61
store x = 1 prefer R3
store y fixed Re
entry main {
  pause x
}
`);
    expect(ast.declarations[0]).toMatchObject({
      name: "x",
      storage: { mode: "prefer", register: "3" },
    });
    expect(ast.declarations[1]).toMatchObject({
      name: "y",
      storage: { mode: "fixed", register: "e" },
    });
  });

  it("rejects extra tokens in expressions", () => {
    expect(() => parseProgram("machine mk61\nstore x = 1 2\nentry main {\n pause x\n}")).toThrow(
      ParseError,
    );
  });

  it("assigns unique scratchIds to nested switches", () => {
    const ast = parseProgram(`
machine mk61
entry main {
  switch 1 {
    case 1 {
      switch 2 {
        case 2 {
          halt 1
        }
      }
    }
  }
}
`);
    const outer = ast.entries[0]?.body[0];
    expect(outer?.kind).toBe("switch");
    if (outer?.kind !== "switch") throw new Error("expected switch");
    const inner = outer.cases[0]?.body[0];
    expect(inner?.kind).toBe("switch");
    if (inner?.kind !== "switch") throw new Error("expected nested switch");
    expect(outer.scratchId).not.toBe(inner.scratchId);
  });

  it("strips comments", () => {
    const ast = parseProgram(`
machine mk61 // top
# also a comment
entry main {
  pause 1 // trailing
}
`);
    expect(ast.machine).toBe("mk61");
  });

  it("parses preload function calls and rejects bare preload names", () => {
    const ast = parseProgram(`
target mk61
preload R9 = random_seed()
entry main {
  halt 0
}
`);

    expect(ast.preloads[0]).toMatchObject({ register: "9", value: "random_seed()" });
    expect(() =>
      parseProgram(`
target mk61
preload R9 = random_seed
entry main {
  halt 0
}
`),
    ).toThrow(/bare name/u);
    expect(() =>
      parseProgram(`
target mk61
preload R9 = random_seed + 1
entry main {
  halt 0
}
`),
    ).toThrow(/literal or an explicit function call/u);
  });

  it("parses human-centered M61 programs", () => {
    const ast = parseProgram(`
target mk61
budget 105 cells
optimize size

program Demo {
  input key: digit
  input answer: number
  state {
    [displayed] score: counter 0..9 = 0
  }
  screen main {
    show score
    style compact digits
  }
  turn {
    show main
    read key
    match key {
      1 => inc
      otherwise => stop 0
    }
  }
  [hot] rule inc {
    score += 1
  }
}
`);
    expect(ast.targetProfile).toBe("mk61_exact");
    expect(ast.optimize).toBe("size");
    expect(ast.v2?.name).toBe("Demo");
    expect(ast.v2?.inputs[1]?.inputType).toBe("number");
    expect(ast.v2?.state[0]?.hints).toContain("displayed");
    expect(ast.v2?.rules[0]?.hints).toContain("hot");
    const turn = ast.entries[0]?.body[0];
    expect(turn?.kind).toBe("loop");
    if (turn?.kind !== "loop") throw new Error("expected turn loop");
    expect(turn.body.some((statement) => statement.kind === "dispatch")).toBe(true);
  });

  it("parses v2 world, boards, fleets, state config, encounters, and reference metadata", () => {
    const ast = parseProgram(`
target mk61
budget 105 cells
optimize size
reference demo_reference

program Demo {
  input key: digit
  board ocean: 10x10 {
    coordinate two_digit 00..99
  }
  state {
    [displayed] pos: coord(floor 1..3, x 0..7) = 1
    [displayed] strength: resource 0..99 = 40 {
      terminal at 0 show error
    }
    score: score 0..9 = 0 {
      reward skeleton 1
    }
    plans: bitset {
      generated random
      cleared when creature defeated
    }
  }
  world demo_world: hall {
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
  fleet enemy_fleet on ocean {
    ships enemy_ships 0..99 = input.X
    generated random
    cleared when player hit
    terminal at 0 show main
  }
  screen main {
    show pos, strength
    style compact digits
  }
  turn {
    encounter(key)
  }
  encounters key {
    0 empty {
      show main
    }
    3 skeleton {
      score += 1
    }
  }
}
`);

    expect(ast.reference).toBe("demo_reference");
    expect(ast.v2?.boards[0]).toMatchObject({ name: "ocean", width: 10, height: 10, coordinateStyle: "two_digit" });
    expect(ast.v2?.fleets[0]?.ships).toMatchObject({ name: "enemy_ships", initial: "input.X", min: 0, max: 99 });
    expect(ast.v2?.worlds[0]?.name).toBe("demo_world");
    expect(ast.v2?.state.find((field) => field.name === "strength")?.terminal).toMatchObject({ at: "0", show: "error" });
    expect(ast.v2?.state.find((field) => field.name === "plans")?.clearedWhen).toBe("creature defeated");
    expect(ast.v2?.encounters[0]?.cases.map((encounterCase) => encounterCase.name)).toEqual(["empty", "skeleton"]);
    expect(ast.domains.some((domain) => domain.domainKind === "maze" && domain.name === "ocean")).toBe(true);
    expect(ast.domains.some((domain) => domain.domainKind === "bitset" && domain.name === "enemy_fleet")).toBe(true);
    expect(ast.states[0]?.fields.some((field) => field.name === "enemy_ships" && field.type === "resource")).toBe(true);
    expect(ast.domains.some((domain) => domain.domainKind === "maze" && domain.name === "demo_world")).toBe(true);
    expect(ast.procs.some((proc) => proc.name === "encounter")).toBe(true);
  });

  it("rejects old v2 top-level implementation blocks", () => {
    expect(() =>
      parseProgram(`
target mk61
preload R9 = random_seed()
program Bad {
  turn {
    stop 0
  }
}
`),
    ).toThrow(/v2 source must not use preload/u);
    expect(() =>
      parseProgram(`
target mk61
resource strength {
  register Ra
}
program Bad {
  turn {
    stop 0
  }
}
`),
    ).toThrow(/Move v2 domain intent/u);
  });

  it("parses challenge blocks as game intent", () => {
    const ast = parseProgram(`
target mk61
program Demo {
  input answer: number
  state {
    tile: enum = 0
    score: counter 0..9 = 0
    strength: counter 0..9 = 9
  }
  screen warning {
    show tile
    style compact digits
  }
  screen memory {
    show tile
    style compact digits
  }
  turn {
    challenge tile {
      success {
        score += 1
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

  it("rejects low-level implementation hints", () => {
    expect(() =>
      parseProgram(`
target mk61
program Bad {
  input key: digit
  state {
    [use_X2] score: counter 0..9 = 0
  }
  turn {
    stop 0
  }
}
`),
    ).toThrow(/Low-level implementation hint/u);
  });

  it("keeps rule semantics typed instead of raw text", () => {
    const ast = parseProgram(`
target mk61
program Demo {
  input key: digit
  state {
    pos: coord(x 1..7) = 1
    walls: bitset generated random
  }
  turn {
    stop 0
  }
  rule move {
    let next = pos + 1
    if walls has next {
      show 0
    }
    else {
      pos = next
    }
    require pos exists else show 0
    walls clear pos
    reward by pos
  }
}
`);
    const rule = ast.v2?.rules[0];
    expect(rule?.body.map((statement) => statement.kind)).toEqual([
      "v2_let",
      "v2_if",
      "v2_require",
      "v2_collection",
      "v2_reward",
    ]);
    const conditional = rule?.body[1];
    expect(conditional?.kind).toBe("v2_if");
    if (conditional?.kind !== "v2_if") throw new Error("expected v2_if");
    expect(conditional.predicate).toMatchObject({
      kind: "v2_collection_has",
      collection: "walls",
      item: "next",
    });
  });
});
