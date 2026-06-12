import { describe, expect, it } from "vitest";
import { compileMKPro } from "../../src/core/index.ts";

function maxMinWarnings(source: string): string[] {
  const result = compileMKPro(source, { budget: 999 });
  return result.report.warnings.filter((warning) => warning.includes("К max"));
}

describe("hardware max/min zero-operand lint", () => {
  it("warns when a max() operand can provably be exactly 0", () => {
    const source = `
program Test {
  state {
    score: counter 0..9 = 0
    best: counter 0..99 = 1
  }

  loop {
    best = max(best, score)
    show(best)
  }
}
`;
    const warnings = maxMinWarnings(source);
    expect(warnings.length).toBeGreaterThan(0);
    expect(warnings[0]).toContain("safe_max()");
  });

  it("warns for min() the same way", () => {
    const source = `
program Test {
  state {
    score: counter 0..9 = 5
    worst: counter 0..99 = 99
  }

  loop {
    worst = min(worst, score)
    show(worst)
  }
}
`;
    const warnings = maxMinWarnings(source);
    expect(warnings.length).toBeGreaterThan(0);
    expect(warnings[0]).toContain("safe_min()");
  });

  it("stays silent for a literal 0 operand (intentional idiom)", () => {
    const source = `
program Test {
  state {
    score: counter 1..9 = 1
  }

  loop {
    score = max(score, 0)
    show(score)
  }
}
`;
    expect(maxMinWarnings(source)).toHaveLength(0);
  });

  it("stays silent when the declared range excludes 0", () => {
    const source = `
program Test {
  state {
    a: counter 1..9 = 1
    b: counter 2..8 = 2
  }

  loop {
    a = max(a, b)
    show(a)
  }
}
`;
    expect(maxMinWarnings(source)).toHaveLength(0);
  });

  it("stays silent for safe_max/safe_min", () => {
    const source = `
program Test {
  state {
    score: counter 0..9 = 0
    best: counter 0..99 = 1
  }

  loop {
    best = safe_max(best, score)
    show(best)
  }
}
`;
    expect(maxMinWarnings(source)).toHaveLength(0);
  });
});
