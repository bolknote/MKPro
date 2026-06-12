import { describe, expect, it } from "vitest";
import { compileMKPro } from "../../src/core/index.ts";

function showHaltWarnings(source: string): string[] {
  const result = compileMKPro(source, { budget: 999 });
  return result.report.warnings.filter((warning) =>
    warning.includes("show(...) immediately followed by halt()")
  );
}

describe("show + halt style lint", () => {
  it("flags show(...) immediately followed by a bare halt()", () => {
    const source = `
program Test {
  state {
    score: counter 0..99 = 0
  }

  loop {
    score += 1
    if score > 10 {
      show(score)
      halt()
    }
  }
}
`;
    const warnings = showHaltWarnings(source);
    expect(warnings.length).toBeGreaterThan(0);
    expect(warnings[0]).toContain("halt(...)");
  });

  it("stays silent for halt(value) as the terminal screen", () => {
    const source = `
program Test {
  state {
    score: counter 0..99 = 0
  }

  loop {
    score += 1
    if score > 10 {
      halt(score)
    }
  }
}
`;
    expect(showHaltWarnings(source)).toHaveLength(0);
  });

  it("stays silent for a show used as a resumable prompt", () => {
    const source = `
program Test {
  state {
    key: packed = 0
    score: counter 0..99 = 0
  }

  loop {
    show(score)
    key = read()
    score += key
  }
}
`;
    expect(showHaltWarnings(source)).toHaveLength(0);
  });

  it("stays silent when halt displays its own value after a show", () => {
    const source = `
program Test {
  state {
    score: counter 0..99 = 0
    lives: counter 0..9 = 3
  }

  loop {
    score += 1
    if score > 10 {
      show(lives)
      halt(score)
    }
  }
}
`;
    expect(showHaltWarnings(source)).toHaveLength(0);
  });
});
