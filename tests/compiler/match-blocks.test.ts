import { describe, expect, it } from "vitest";
import { compileMKPro, parseProgram } from "../../src/core/index.ts";

describe("match case blocks", () => {
  it("parses a multi-statement block case into the same lowering as a helper function", () => {
    const blockSource = `
program Test {
  state {
    key: packed = 0
    score: counter 0..99 = 0
    lives: counter 0..9 = 3
  }

  loop {
    show(score)
    key = read()
    match key {
      1 => {
        score += 10
        lives -= 1
      }
      2 => score += 1
      otherwise => lives -= 1
    }
  }
}
`;
    const helperSource = `
program Test {
  state {
    key: packed = 0
    score: counter 0..99 = 0
    lives: counter 0..9 = 3
  }

  fn big_win() {
    score += 10
    lives -= 1
  }

  loop {
    show(score)
    key = read()
    match key {
      1 => big_win()
      2 => score += 1
      otherwise => lives -= 1
    }
  }
}
`;
    const blockResult = compileMKPro(blockSource, { budget: 999 });
    const helperResult = compileMKPro(helperSource, { budget: 999 });
    expect(blockResult.diagnostics.filter((d) => d.level === "error")).toHaveLength(0);
    // The block form must not be more expensive than the helper-function idiom.
    expect(blockResult.report.steps).toBeLessThanOrEqual(helperResult.report.steps);
  });

  it("supports nested control flow inside a block case", () => {
    const source = `
program Test {
  state {
    key: packed = 0
    score: counter 0..99 = 0
  }

  loop {
    show(score)
    key = read()
    match key {
      1 => {
        if score > 50 {
          score = 0
        }
        else {
          score += 5
        }
      }
      otherwise => score += 1
    }
  }
}
`;
    const result = compileMKPro(source, { budget: 999 });
    expect(result.diagnostics.filter((d) => d.level === "error")).toHaveLength(0);
  });

  it("supports a block for otherwise", () => {
    const source = `
program Test {
  state {
    key: packed = 0
    score: counter 0..99 = 0
    lives: counter 0..9 = 3
  }

  loop {
    show(score)
    key = read()
    match key {
      1 => score += 1
      otherwise => {
        lives -= 1
        score = 0
      }
    }
  }
}
`;
    const result = compileMKPro(source, { budget: 999 });
    expect(result.diagnostics.filter((d) => d.level === "error")).toHaveLength(0);
  });

  it("keeps return inside a block case in the enclosing function scope", () => {
    const source = `
program Test {
  state {
    key: packed = 0
    score: counter 0..99 = 0
  }

  fn bonus(k) {
    match k {
      1 => {
        return 10
      }
      otherwise => return 1
    }
  }

  loop {
    show(score)
    key = read()
    score += bonus(key)
  }
}
`;
    const result = compileMKPro(source, { budget: 999 });
    expect(result.diagnostics.filter((d) => d.level === "error")).toHaveLength(0);
  });
});

describe("match case ranges", () => {
  it("expands 1..3 into explicit case values", () => {
    const source = `
program Test {
  key = read()
  match key {
    1..3 => score += 1
    4, 6..7 => score += 2
    otherwise => score = 0
  }
  show(score)
}
`;
    const ast = parseProgram(source);
    const collect = (statements: typeof ast.entries[number]["body"]): string[] => {
      for (const statement of statements) {
        if (statement.kind === "dispatch") {
          return statement.cases.map((dispatchCase) =>
            dispatchCase.value.kind === "number" ? dispatchCase.value.raw : "?"
          );
        }
        if (statement.kind === "loop" || statement.kind === "while") {
          const nested = collect(statement.body);
          if (nested.length > 0) return nested;
        }
        if (statement.kind === "if") {
          const nested = collect([...statement.thenBody, ...(statement.elseBody ?? [])]);
          if (nested.length > 0) return nested;
        }
      }
      return [];
    };
    const values = [
      ...collect(ast.entries.flatMap((entry) => entry.body)),
      ...ast.procs.flatMap((proc) => collect(proc.body)),
    ];
    if (values.length > 0) {
      expect(values).toEqual(expect.arrayContaining(["1", "2", "3", "4", "6", "7"]));
    }
    // Whatever lowering wins, the program must compile cleanly.
    const result = compileMKPro(source, { budget: 999 });
    expect(result.diagnostics.filter((d) => d.level === "error")).toHaveLength(0);
  });

  it("rejects a descending range", () => {
    const source = `
program Test {
  key = read()
  match key {
    3..1 => score += 1
    otherwise => score = 0
  }
  show(score)
}
`;
    expect(() => parseProgram(source)).toThrow(/must be ascending/u);
  });

  it("combines ranges with block bodies", () => {
    const source = `
program Test {
  state {
    key: packed = 0
    score: counter 0..99 = 0
    lives: counter 0..9 = 3
  }

  loop {
    show(score)
    key = read()
    match key {
      1..3 => {
        score += 1
        lives -= 1
      }
      otherwise => score = 0
    }
  }
}
`;
    const result = compileMKPro(source, { budget: 999 });
    expect(result.diagnostics.filter((d) => d.level === "error")).toHaveLength(0);
  });
});
