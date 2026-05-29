import { describe, expect, it } from "vitest";
import { compileMKPro } from "../../src/core/index.ts";

function compileOk(source: string) {
  return compileMKPro(source, { analysis: true, budget: 999 });
}

function hasOptimization(result: ReturnType<typeof compileOk>, name: string): boolean {
  return result.report.optimizations.some((item) => item.name === name);
}

function stepComments(result: ReturnType<typeof compileOk>): string[] {
  return result.steps.map((step) => step.comment ?? "");
}

describe("show optimization strategy ideas", () => {
  it("reuses a packed display prefix that is already stored in the target decimal positions", () => {
    const result = compileOk(`
program ReadyPackedDisplayPrefix {
  state {
    raw: packed = 1234
    prefix: packed = 0
    suffix: counter 0..99 = 0
  }
  screen view {
    show prefix, suffix:02
  }
  turn {
    prefix = int(raw / 100) * 100
    suffix = raw - prefix
    show view
  }
}
`);

    expect(hasOptimization(result, "packed-display-storage-reuse")).toBe(true);
    expect(stepComments(result)).not.toContain("packed display field shift");
  });

  it("reuses X for the first display field without changing field order", () => {
    const result = compileOk(`
program CurrentXFirstDisplayField {
  state {
    a: counter 0..9 = 1
    b: counter 0..99 = 23
  }
  screen view {
    show a, b
  }
  turn {
    a = 7
    show view
  }
}
`);

    expect(stepComments(result).filter((comment) => comment === "display view source")).toHaveLength(1);
    expect(hasOptimization(result, "display-stack-reuse")).toBe(false);
  });

  it("uses preloaded decimal scale constants for packed display shifts", () => {
    const result = compileOk(`
program PreloadedDisplayScales {
  state {
    a: counter 0..9 = 1
    b: counter 0..99 = 23
    c: counter 0..999 = 456
  }
  screen view {
    show a, b, c
  }
  turn {
    show view
  }
}
`);

    expect(result.report.preloads.map((item) => item.value)).toEqual(expect.arrayContaining(["100", "1000"]));
    expect(hasOptimization(result, "preloaded-constant")).toBe(true);
    expect(stepComments(result)).toEqual(expect.arrayContaining(["preload const 100", "preload const 1000"]));
  });

  it("shares frequent identical packed display bodies through a helper", () => {
    const result = compileOk(`
program RepeatedShowBody {
  state {
    selector: counter 0..9 = 0
    a: counter 0..9 = 1
    b: counter 0..99 = 23
    c: counter 0..999 = 456
  }
  screen view {
    show a, b, c
  }
  turn {
    match selector {
      1 => show view
      2 => show view
      3 => show view
      otherwise => stop 0
    }
  }
}
`);

    expect(hasOptimization(result, "packed-display-helper")).toBe(true);
    expect(hasOptimization(result, "packed-display-helper-call")).toBe(true);
  });

  it("lowers literal-separated scoreboards through a display-byte/X2 builder", () => {
    const result = compileOk(`
program LiteralSeparatedScoreboard {
  state {
    die: counter 1..6 = 1
    score: counter 0..99 = 0
    total: counter 0..999 = 0
    roll: counter 0..99 = 0
  }
  screen status {
    show die ".-" score:02 "-" total:03 "-" roll:02
  }
  turn {
    show status
  }
}
`);

    expect(hasOptimization(result, "display-byte-x2-lowering")).toBe(true);
    expect(result.report.machineFeaturesUsed.some((feature) => feature.id === "display-bytes")).toBe(true);
  });

  it("shares frequent literal-separated display bodies through a display-byte helper", () => {
    const result = compileOk(`
program RepeatedLiteralSeparatedScoreboard {
  state {
    selector: counter 0..9 = 0
    die: counter 1..6 = 1
    score: counter 0..99 = 0
    total: counter 0..999 = 0
    roll: counter 0..99 = 0
  }
  screen status {
    show die ".-" score:02 "-" total:03 "-" roll:02
  }
  turn {
    match selector {
      1 => show status
      2 => show status
      otherwise => stop 0
    }
  }
}
`);

    expect(hasOptimization(result, "display-byte-helper")).toBe(true);
    expect(hasOptimization(result, "display-byte-helper-call")).toBe(true);
    expect(result.report.candidates.some((candidate) =>
      candidate.site === "display status" &&
      candidate.variant === "display-byte-helper" &&
      candidate.selected
    )).toBe(true);
  });

  it("records and selects the cheapest display strategy for a multi-field show", () => {
    const result = compileOk(`
program DisplayStrategySelection {
  state {
    a: counter 0..9 = 1
    b: counter 0..99 = 23
  }
  screen view {
    show a, b
  }
  turn {
    show view
  }
}
`);

    const displayCandidates = result.report.candidates.filter((candidate) => candidate.site === "display view");
    expect(displayCandidates.map((candidate) => candidate.variant)).toEqual(expect.arrayContaining([
      "decimal-pack",
      "packed-storage-reuse",
      "packed-display-helper",
      "display-byte-builder",
    ]));
    expect(displayCandidates.some((candidate) => candidate.selected)).toBe(true);
    expect(hasOptimization(result, "display-strategy-selection")).toBe(true);
  });
});
