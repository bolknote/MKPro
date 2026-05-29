import { describe, expect, it } from "vitest";
import { createRequire } from "node:module";
import { compileMKPro } from "../../src/core/index.ts";

const require = createRequire(import.meta.url);

function compileOk(source: string) {
  return compileMKPro(source, { analysis: true, budget: 999 });
}

function hasOptimization(result: ReturnType<typeof compileOk>, name: string): boolean {
  return result.report.optimizations.some((item) => item.name === name);
}

function stepComments(result: ReturnType<typeof compileOk>): string[] {
  return result.steps.map((step) => step.comment ?? "");
}

function runCompiledDisplay(source: string): string {
  const { MK61 } = require("../emulator/mk61.cjs") as {
    MK61: new (options?: { extended?: boolean }) => {
      loadProgram: (codes: number[]) => { diagnostics: string[] };
      setRegister: (register: string, value: string) => void;
      pressSequence: (keys: string[]) => void;
      runUntilStable: (options: { maxFrames: number; stableFrames: number }) => { stopped: boolean };
      displayText: () => string;
    };
  };
  const result = compileMKPro(source, { analysis: true, budget: 999 });
  const calc = new MK61({ extended: true });
  expect(calc.loadProgram(result.steps.map((step) => step.opcode)).diagnostics).toEqual([]);
  for (const preload of result.report.preloads) {
    calc.setRegister(preload.register, preload.value);
  }
  calc.pressSequence(["В/О", "С/П"]);
  expect(calc.runUntilStable({ maxFrames: 1200, stableFrames: 8 }).stopped).toBe(true);
  return calc.displayText();
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

  it("renders literal-separated scoreboards correctly on the emulator", () => {
    const { MK61 } = require("../emulator/mk61.cjs") as {
      MK61: new (options?: { extended?: boolean }) => {
        loadProgram: (codes: number[]) => { diagnostics: string[] };
        setRegister: (register: string, value: string) => void;
        pressSequence: (keys: string[]) => void;
        runUntilStable: (options: { maxFrames: number; stableFrames: number }) => { stopped: boolean };
        displayText: () => string;
      };
    };
    const result = compileMKPro(`
program LiteralSeparatedScoreboardRun {
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
    const calc = new MK61({ extended: true });
    const loaded = calc.loadProgram(result.steps.map((step) => step.opcode));
    expect(loaded.diagnostics).toEqual([]);
    for (const preload of result.report.preloads) {
      calc.setRegister(preload.register, preload.value);
    }
    calc.setRegister(result.report.registers.die!, "5");
    calc.setRegister(result.report.registers.score!, "15");
    calc.setRegister(result.report.registers.total!, "42");
    calc.setRegister(result.report.registers.roll!, "3");
    calc.pressSequence(["В/О", "С/П"]);
    expect(calc.runUntilStable({ maxFrames: 1000, stableFrames: 8 }).stopped).toBe(true);
    expect(calc.displayText()).toBe("5,-15-042-03");
  });

  it("preserves zero-padded score fields when the display counter cannot be zero", () => {
    const { MK61 } = require("../emulator/mk61.cjs") as {
      MK61: new (options?: { extended?: boolean }) => {
        loadProgram: (codes: number[]) => { diagnostics: string[] };
        setRegister: (register: string, value: string) => void;
        pressSequence: (keys: string[]) => void;
        runUntilStable: (options: { maxFrames: number; stableFrames: number }) => { stopped: boolean };
        displayText: () => string;
      };
    };
    const result = compileMKPro(`
program NonZeroCounterScoreboardRun {
  state {
    die: counter 1..6 = 3
    score: counter 0..99 = 6
    total: counter 0..999 = 6
    roll: counter 1..99 = 2
  }
  screen status {
    show die ".-" score:02 "-" total:03 "-" roll:02
  }
  turn {
    show status
  }
}
`);
    const calc = new MK61({ extended: true });
    const loaded = calc.loadProgram(result.steps.map((step) => step.opcode));
    expect(loaded.diagnostics).toEqual([]);
    for (const preload of result.report.preloads) {
      calc.setRegister(preload.register, preload.value);
    }
    calc.pressSequence(["В/О", "С/П"]);
    expect(calc.runUntilStable({ maxFrames: 1000, stableFrames: 8 }).stopped).toBe(true);
    expect(calc.displayText()).toBe("3,-06-006-02");
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

  it("lowers literal calculator video strings without source raw blocks", () => {
    const result = compileOk(`
program LiteralVideoScreen {
  screen cave {
    show "8.E6-EГ C"
  }
  turn {
    show cave
  }
}
`);

    expect(hasOptimization(result, "screen-video-literal-lowering")).toBe(true);
    expect(result.steps.map((step) => step.mnemonic)).toEqual(expect.arrayContaining(["К ИНВ", "С/П"]));
    expect(result.report.machineFeaturesUsed.some((feature) => feature.id === "display-bytes")).toBe(true);
  });

  it("lowers literal calculator error screens", () => {
    const result = compileOk(`
program LiteralErrorScreen {
  screen mine {
    show "ЕГГОГ"
  }
  turn {
    show mine
  }
}
`);

    expect(hasOptimization(result, "screen-video-literal-lowering")).toBe(true);
    expect(hasOptimization(result, "error-stop")).toBe(true);
    expect(result.steps.map((step) => step.opcode)).toEqual(expect.arrayContaining([0x2b]));
    expect(result.steps.map((step) => step.mnemonic)).not.toEqual(expect.arrayContaining(["F 1/x"]));
  });

  it("lowers zero-digit literal tails through the 0C-tail display trick", () => {
    const source = `
program ZeroDigitTailScreen {
  screen tail {
    show "2Е"
  }
  turn {
    show tail
  }
}
`;

    const result = compileOk(source);
    expect(hasOptimization(result, "screen-zero-digit-tail-lowering")).toBe(true);
    expect(runCompiledDisplay(source)).toBe("2Е,");
  });

  it("lowers wider sign-digit literal screens through indirect display construction", () => {
    const source = `
program SignDigitLiteralScreen {
  screen cave {
    show "3Е0000021"
  }
  turn {
    show cave
  }
}
`;

    const result = compileOk(source);
    expect(hasOptimization(result, "screen-sign-digit-literal-lowering")).toBe(true);
    expect(runCompiledDisplay(source)).toBe("3Е0000021,");
  });

  it("lowers literal calculator error stops to one-cell trap opcodes", () => {
    const { MK61 } = require("../emulator/mk61.cjs") as {
      MK61: new () => {
        loadProgram: (codes: number[]) => { diagnostics: string[] };
        pressSequence: (keys: string[]) => void;
        runUntilStable: (options: { maxFrames: number; stableFrames: number }) => { stopped: boolean };
        displayText: () => string;
      };
    };
    const result = compileMKPro(`
program LiteralErrorStop {
  turn {
    stop "ЕГГОГ"
  }
}
`);

    expect(hasOptimization(result, "error-stop")).toBe(true);
    expect(result.steps.map((step) => step.opcode)).toEqual([0x2b]);

    const calc = new MK61();
    expect(calc.loadProgram(result.steps.map((step) => step.opcode)).diagnostics).toEqual([]);
    calc.pressSequence(["В/О", "С/П"]);
    expect(calc.runUntilStable({ maxFrames: 200, stableFrames: 6 }).stopped).toBe(true);
    expect(calc.displayText().toUpperCase()).toContain("ЕГГ");
  });

  it("lowers signed literal calculator video strings", () => {
    const result = compileOk(`
program SignedLiteralVideoScreen {
  screen win {
    show "-8CEC6-L-"
  }
  turn {
    show win
  }
}
`);

    expect(hasOptimization(result, "screen-video-literal-lowering")).toBe(true);
    expect(result.steps.map((step) => step.mnemonic)).toEqual(expect.arrayContaining(["К ИНВ", "/-/", "С/П"]));
  });
});
