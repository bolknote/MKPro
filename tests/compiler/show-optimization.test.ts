import { describe, expect, it } from "vitest";
import { createRequire } from "node:module";
import { compileMKPro } from "../../src/core/index.ts";
import { compileLoweringVariantForTest } from "../../src/core/compiler.ts";

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

function inlineDisplaySourceComments(result: ReturnType<typeof compileOk>): string[] {
  return stepComments(result).filter((comment) =>
    /^display __inline_show_\d+_\d+ source$/u.test(comment)
  );
}

function runCompiledDisplay(source: string): string {
  return runCompiledResultDisplay(compileMKPro(source, { analysis: true, budget: 999 }));
}

function runCompiledResultDisplay(result: ReturnType<typeof compileOk>): string {
  const { MK61 } = require("../emulator/mk61.cjs") as {
    MK61: new (options?: { extended?: boolean }) => {
      loadProgram: (codes: number[]) => { diagnostics: string[] };
      setRegister: (register: string, value: string) => void;
      pressSequence: (keys: string[]) => void;
      runUntilStable: (options: { maxFrames: number; stableFrames: number }) => { stopped: boolean };
      displayText: () => string;
    };
  };
  const calc = new MK61({ extended: true });
  const setup = result.report.setupProgram?.steps.map((step) => step.opcode);
  if (setup !== undefined) {
    expect(calc.loadProgram(setup).diagnostics).toEqual([]);
    calc.pressSequence(["В/О", "С/П"]);
    expect(calc.runUntilStable({ maxFrames: 1200, stableFrames: 8 }).stopped).toBe(true);
  } else {
    for (const preload of result.report.preloads) {
      calc.setRegister(preload.register, preload.value);
    }
  }
  expect(calc.loadProgram(result.steps.map((step) => step.opcode)).diagnostics).toEqual([]);
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

  loop {
    prefix = int(raw / 100) * 100
    suffix = raw - prefix
    show(prefix, suffix:02)
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

  loop {
    a = 7
    show(a, b)
  }
}
`);

    expect(inlineDisplaySourceComments(result)).toHaveLength(1);
    expect(hasOptimization(result, "display-stack-reuse")).toBe(false);
  });

  it("reuses X for the last display field without changing field order", () => {
    const source = `
program CurrentXLastDisplayField {
  state {
    a: counter 0..9 = 1
    b: counter 0..9 = 2
    c: counter 0..9 = 3
  }

  loop {
    c = 7
    show(a, b, c)
  }
}
`;
    const result = compileOk(source);

    expect(hasOptimization(result, "display-current-x-suffix-reuse")).toBe(true);
    expect(inlineDisplaySourceComments(result)).toHaveLength(2);
    expect(stepComments(result).filter((comment) => comment === "packed display current field append")).toHaveLength(1);
    expect(runCompiledDisplay(source)).toBe("127,");
  });

  it("reuses X for a middle display field without changing field order", () => {
    const source = `
program CurrentXMiddleDisplayField {
  state {
    a: counter 0..9 = 1
    b: counter 0..9 = 2
    c: counter 0..9 = 3
  }

  loop {
    b = 7
    show(a, b, c)
  }
}
`;
    const result = compileOk(source);

    expect(hasOptimization(result, "display-current-x-middle-reuse")).toBe(true);
    expect(inlineDisplaySourceComments(result)).toHaveLength(2);
    expect(stepComments(result).filter((comment) => comment === "packed display current field append")).toHaveLength(1);
    expect(runCompiledDisplay(source)).toBe("173,");
  });

  it("packs decimal digit literals inside numeric show screens", () => {
    const source = `
program DecimalLiteralFields {
  state {
    a: counter 0..9 = 1
    b: counter 0..9 = 2
  }


  loop {
    show(a, 0, b)
    show(a, 012, b)
  }
}
`;
    const result = compileOk(source);

    expect(hasOptimization(result, "display-decimal-literal-field")).toBe(true);
    expect(runCompiledDisplay(`
program ShortDecimalLiteralField {
  state {
    a: counter 0..9 = 1
    b: counter 0..9 = 2
  }

  loop {
    show(a, 0, b)
  }
}
`)).toBe("102,");
    expect(runCompiledDisplay(`
program LongDecimalLiteralField {
  state {
    a: counter 0..9 = 1
    b: counter 0..9 = 2
  }

  loop {
    show(a, 012, b)
  }
}
`)).toBe("10122,");
    expect(runCompiledDisplay(`
program BareDecimalLiteralFields {
  state {
    a: counter 0..9 = 2
    b: counter 0..9 = 3
  }

  loop {
    show(123, a, b, 1)
  }
}
`)).toBe("123231,");
  });

  it("lowers quoted canonical numeric screens through the same path as numeric shows", () => {
    const quoted = compileOk(`
program QuotedIntegerScreen {
  loop {
    show("-20")
  }
}
`);
    const numeric = compileOk(`
program NumericIntegerScreen {
  loop {
    show(-20)
  }
}
`);
    const leadingZero = compileOk(`
program LeadingZeroIntegerScreen {
  loop {
    show("020")
  }
}
`);
    const plain = compileOk(`
program PlainIntegerScreen {
  loop {
    show(20)
  }
}
`);
    const canonicalDecimal = compileOk(`
program CanonicalDecimalScreen {
  loop {
    show("1.2")
  }
}
`);
    const decimalNumeric = compileOk(`
program DecimalNumericScreen {
  loop {
    show(1.2)
  }
}
`);
    const spaced = compileOk(`
program SpacedScreen {
  loop {
    show(" 20")
  }
}
`);

    expect(quoted.steps.map((step) => step.opcode)).toEqual(numeric.steps.map((step) => step.opcode));
    expect(leadingZero.steps.map((step) => step.opcode)).not.toEqual(plain.steps.map((step) => step.opcode));
    expect(canonicalDecimal.steps.map((step) => step.opcode)).toEqual(decimalNumeric.steps.map((step) => step.opcode));
    expect(hasOptimization(quoted, "screen-video-literal-lowering")).toBe(false);
    expect(hasOptimization(leadingZero, "screen-video-literal-lowering")).toBe(true);
    expect(hasOptimization(leadingZero, "screen-leading-zero-hex-lowering")).toBe(true);
    expect(hasOptimization(canonicalDecimal, "screen-video-literal-lowering")).toBe(false);
    expect(hasOptimization(spaced, "screen-video-literal-lowering")).toBe(true);
    expect(runCompiledDisplay(`
program QuotedIntegerDisplay {
  loop {
    show("-20")
  }
}
`)).toBe("-20,");
    expect(runCompiledDisplay(`
program LeadingZeroIntegerDisplay {
  loop {
    show("020")
  }
}
`)).toBe("020,");
    for (const literal of ["00", "000", "010", "030", "040", "050", "021", "032", "043", "054"]) {
      expect(runCompiledDisplay(`
program LeadingZeroLiteralDisplay {
  loop {
    show("${literal}")
  }
}
`)).toBe(`${literal},`);
    }
  }, 10000);

  it("uses preloaded decimal scale constants for packed display shifts", () => {
    const result = compileOk(`
program PreloadedDisplayScales {
  state {
    a: counter 0..9 = 1
    b: counter 0..99 = 23
    c: counter 0..999 = 456
  }

  loop {
    show(a, b, c)
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

  loop {
    match selector {
      1 => show(a, b, c)
      2 => show(a, b, c)
      3 => show(a, b, c)
      otherwise => halt(0)
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

  loop {
    show(die, ".-", score:02, "-", total:03, "-", roll:02)
  }
}
`);

    expect(hasOptimization(result, "display-byte-x2-lowering")).toBe(true);
    expect(result.report.machineFeaturesUsed.some((feature) => feature.id === "display-bytes")).toBe(true);
  });

  it("splices a one-digit floor into a packed room row", () => {
    const source = `
program FloorPackedRow {
  state {
    floor: counter 1..4 = 2
    base: packed = bit_not(5 / 9)
    row: packed = 0
  }

  loop {
    row = bit_xor(base, 8 + 4 / pow10(2))
    row = bit_xor(row, 8 + 2 / pow10(4))
    row = bit_xor(row, 8 + 1 / pow10(7))
    show(floor, ".", row)
  }
}
`;
    const result = compileOk(source);

    expect(hasOptimization(result, "floor-packed-row-display")).toBe(true);
    expect(runCompiledDisplay(source)).toBe("2,-Е-8--L");
  });

  it("splices a one-digit floor into a materialized packed row expression", () => {
    const source = `
program InlineFloorPackedRow {
  state {
    floor: counter 1..4 = 2
  }

  loop {
    show(floor, ".", bit_not(5 / 9))
  }
}
`;
    const result = compileOk(source);

    expect(hasOptimization(result, "display-expression-materialization")).toBe(true);
    expect(hasOptimization(result, "floor-packed-row-display")).toBe(true);
    expect(runCompiledDisplay(source)).toBe("2,-------");
  });

  it("can force an inline packed row expression without a hidden display register", () => {
    const source = `
program InlineFloorPackedRow {
  state {
    floor: counter 1..4 = 2
  }

  loop {
    show(floor, ".", bit_not(5 / 9))
  }
}
`;
    const result = compileLoweringVariantForTest(
      source,
      { analysis: true, budget: 999 },
      { inlineFloorPackedRowExpressions: true },
    );

    expect(hasOptimization(result, "display-expression-materialization")).toBe(false);
    expect(hasOptimization(result, "floor-packed-row-expression-display")).toBe(true);
    expect(result.steps.map((step) => step.hex)).toEqual([
      "05", "0E", "09", "13", "3A", "60", "14", "0E",
      "25", "14", "25", "0C", "50", "51", "00",
    ]);
  });

  it("can force an inline indexed packed row display without losing the state bank", () => {
    const source = `
program IndexedFloorPackedRow {
  state {
    floor: counter 1..9 = 2
    rows: packed[1..9] = bit_not(5 / 9)
  }

  loop {
    show(floor, ".", rows[floor])
  }
}
`;
    const result = compileLoweringVariantForTest(
      source,
      { analysis: true, budget: 999 },
      { inlineFloorPackedRowExpressions: true },
    );

    expect(hasOptimization(result, "display-expression-materialization")).toBe(false);
    expect(hasOptimization(result, "indexed-packed-row-table")).toBe(true);
    expect(hasOptimization(result, "floor-packed-row-expression-display")).toBe(true);
    expect(result.report.registers.rows_1).toBe("1");
    expect(result.report.registers.rows_9).toBe("9");
    expect(Number.parseInt(result.report.registers.floor!, 16)).toBeGreaterThanOrEqual(7);
    expect(stepComments(result)).toContain("indexed recall rows");
    expect(runCompiledResultDisplay(result)).toBe("2,-------");
  });

  it("selects inline indexed packed row display when materialization would need one register too many", () => {
    const source = `
program IndexedFloorPackedRowRegisterRescue {
  state {
    floor: counter 1..9 = 2
    rows: packed[1..9] = bit_not(5 / 9)
    a: packed = 1
    b: packed = 2
    c: packed = 3
    d: packed = 4
    e: packed = 5
  }

  loop {
    a = a + 1
    b = b + a
    c = c + b
    d = d + c
    e = e + d
    show(floor, ".", rows[floor])
    halt(a + b + c + d + e)
  }
}
`;
    const result = compileOk(source);

    expect(hasOptimization(result, "inline-floor-packed-row-expression")).toBe(true);
    expect(hasOptimization(result, "display-expression-materialization")).toBe(false);
    expect(hasOptimization(result, "indexed-packed-row-table")).toBe(true);
    expect(result.report.registers.rows_1).toBe("1");
    expect(result.report.registers.rows_9).toBe("9");
    expect(result.report.registers.e).toBeDefined();
    expect(runCompiledResultDisplay(result)).toBe("2,-------");
  });

  it("packs literal-separated small counters into one decimal display register", () => {
    const source = `
program DotCounterDisplay {
  state {
    floor: counter 1..9 = 9
    room: counter 0..6 = 0
  }

  loop {
    if room == 0 {
      room++
    }
    if floor == 9 {
      floor--
    }
    show(floor, ".", room)
  }
}
`;
    const result = compileOk(source);

    expect(hasOptimization(result, "packed-counter-stripes")).toBe(true);
    expect(hasOptimization(result, "fl-unit-decrement")).toBe(false);
    expect(result.report.registers).toHaveProperty("__packed_counter_0");
    expect(result.report.registers).not.toHaveProperty("floor");
    expect(result.report.registers).not.toHaveProperty("room");
    expect(runCompiledDisplay(source)).toBe("8,1");
  });

  it("can force packed counter stripes under a floor-packed row display", () => {
    const source = `
program PackedFloorRowCounters {
  state {
    floor: counter 1..9 = 9
    room: counter 0..6 = 0
    row: packed = bit_not(5 / 9)
  }

  loop {
    if room < 6 {
      room++
    }
    show(floor, ".", bit_not(row + 2 / pow10(room + 1)))
  }
}
`;
    const result = compileLoweringVariantForTest(
      source,
      { analysis: true, budget: 999 },
      { packCounterStripes: true },
    );

    expect(hasOptimization(result, "packed-counter-stripes")).toBe(true);
    expect(hasOptimization(result, "floor-packed-row-display")).toBe(true);
    expect(result.report.registers).toHaveProperty("__packed_counter_0");
    expect(result.report.registers).not.toHaveProperty("floor");
    expect(result.report.registers).not.toHaveProperty("room");
  });

  it("can force three small counters into one decimal-striped register", () => {
    const source = `
program MultiPackedCounters {
  state {
    a: counter 0..9 = 1
    b: counter 0..9 = 2
    c: counter 0..9 = 3
  }

  loop {
    a++
    b++
    c--
    if b >= 3 {
      halt(c)
    }
    else {
      halt(0)
    }
  }
}
`;
    const result = compileLoweringVariantForTest(
      source,
      { analysis: true, budget: 999 },
      { packCounterStripes: true, packCounterStripeNames: ["a", "b", "c"] },
    );

    expect(hasOptimization(result, "packed-counter-stripes")).toBe(true);
    expect(result.report.registers).toHaveProperty("__packed_counter_0");
    expect(result.report.registers).not.toHaveProperty("a");
    expect(result.report.registers).not.toHaveProperty("b");
    expect(result.report.registers).not.toHaveProperty("c");
    expect(runCompiledResultDisplay(result).trim()).toBe("2,");
  });

  it("can force fixed-width counters into one decimal-striped register", () => {
    const source = `
program WidePackedCounters {
  state {
    score: counter 0..99 = 12
    lives: counter 0..9 = 3
    room: counter 0..9 = 4
  }

  loop {
    score += 10
    lives--
    room++
    if score >= 22 {
      halt(room)
    }
    else {
      halt(0)
    }
  }
}
`;
    const result = compileLoweringVariantForTest(
      source,
      { analysis: true, budget: 999 },
      { packCounterStripes: true, packCounterStripeNames: ["score", "lives", "room"] },
    );

    expect(hasOptimization(result, "packed-counter-stripes")).toBe(true);
    expect(result.report.registers).toHaveProperty("__packed_counter_0");
    expect(result.report.registers).not.toHaveProperty("score");
    expect(result.report.registers).not.toHaveProperty("lives");
    expect(result.report.registers).not.toHaveProperty("room");
    expect(runCompiledResultDisplay(result).trim()).toBe("5,");
  });

  it("renders explicit literal spaces between display fields", () => {
    const source = `
program ExplicitSpaceDisplay {
  state {
    a: counter 1..9 = 1
    b: counter 0..9 = 2
  }

  loop {
    show(a, " ", b)
  }
}
`;
    const result = compileOk(source);

    expect(hasOptimization(result, "display-byte-mask-lowering")).toBe(true);
    expect(runCompiledDisplay(source)).toBe("1 2,");
  });

  it("renders mixed display-byte screens that start with a literal", () => {
    const source = `
program LiteralLeadingMixedDisplay {
  state {
    clue: counter 0..9 = 3
  }

  loop {
    show("L-L ", clue)
  }
}
`;
    const result = compileOk(source);

    expect(hasOptimization(result, "display-byte-mask-lowering")).toBe(true);
    expect(runCompiledDisplay(source)).toBe("L-L 3,");
  });

  it("trims edge display spaces before arbitrary mixed literal lowering", () => {
    const source = `
program EdgeSpaceMixedDisplay {
  state {
    clue: counter 0..9 = 3
  }

  loop {
    show(" L ", clue, " ")
  }
}
`;
    const result = compileOk(source);

    expect(hasOptimization(result, "display-edge-whitespace-trim")).toBe(true);
    expect(hasOptimization(result, "display-byte-mask-lowering")).toBe(true);
    expect(runCompiledDisplay(source)).toBe("L 3,");
  });

  it("lowers leading-zero display literal fragments in mixed templates", () => {
    const source = `
program LeadingZeroLiteralMixedDisplay {
  state {
    clue: counter 0..9 = 3
  }

  loop {
    show("0 L", clue)
  }
}
`;
    const result = compileOk(source);

    expect(hasOptimization(result, "display-byte-mask-lowering")).toBe(true);
    expect(runCompiledDisplay(source)).toBe("L3,");
  });

  it("renders literal separators after a variable-width leading field", () => {
    const low = `
program VariableLeadingSpaceLow {
  state {
    room: counter 1..20 = 1
    arrows: counter 0..5 = 5
    clue: counter 0..9 = 3
  }

  loop {
    show(room, " ", arrows, " ", clue)
  }
}
`;
    const high = low.replace("= 1", "= 20").replace("VariableLeadingSpaceLow", "VariableLeadingSpaceHigh");
    const result = compileOk(low);

    expect(hasOptimization(result, "display-byte-variable-mask-lowering")).toBe(true);
    expect(runCompiledDisplay(low)).toBe("1 5 3,");
    expect(runCompiledDisplay(high)).toBe("20 5 3,");
  });

  it("inlines display string assignments into mixed show templates", () => {
    const source = `
program DisplayStringClue {
  state {
    room: counter 1..20 = 1
    clue: counter 0..9 = 0
  }

  loop {
    clue = "-"
    show(room, " ", clue)
  }
}
`;
    const result = compileOk(source);

    expect(hasOptimization(result, "display-string-inline")).toBe(true);
    expect(hasOptimization(result, "display-string-assignment-elimination")).toBe(true);
    expect(runCompiledDisplay(source)).toBe("1 -,");
  });

  it("inlines display string prefixes into literal-leading mixed show templates", () => {
    const source = `
program DisplayStringPrefix {
  state {
    prefix: counter 0..9 = 0
    clue: counter 0..9 = 3
  }

  loop {
    prefix = "L-L "
    show(prefix, clue)
  }
}
`;
    const result = compileOk(source);

    expect(hasOptimization(result, "display-string-inline")).toBe(true);
    expect(hasOptimization(result, "display-byte-mask-lowering")).toBe(true);
    expect(runCompiledDisplay(source)).toBe("L-L 3,");
  });

  it("propagates arbitrary display string aliases into show", () => {
    const source = `
program DisplayStringAlias {
  state {
    clue: counter 0..9 = 0
    val: counter 0..9 = 0
  }

  loop {
    clue = "L-L"
    val = clue
    show(val, " ", clue)
  }
}
`;

    expect(runCompiledDisplay(source)).toBe("L-L L-L,");
  });

  it("splits a conditional display string assignment into show branches", () => {
    const trueBranch = `
program ConditionalDisplayStringTrue {
  state {
    room: counter 1..9 = 1
    clue: counter 0..9 = 3
  }

  loop {
    if room >= 0 {
      clue = "-"
    }
    show(room, " ", clue)
  }
}
`;
    const falseBranch = trueBranch
      .replace("ConditionalDisplayStringTrue", "ConditionalDisplayStringFalse")
      .replace("room >= 0", "room < 0");

    expect(runCompiledDisplay(trueBranch)).toBe("1 -,");
    expect(runCompiledDisplay(falseBranch)).toBe("1 3,");
  });

  it("moves guarded display string defaults to the final show", () => {
    const falseBranch = `
program GuardedDisplayStringDefault {
  state {
    room: counter 1..9 = 1
    clue: counter 0..9 = 0
  }

  loop {
    clue = "-"
    if room < 0 {
      clue = 3
    }
    show(room, clue)
  }
}
`;
    const trueBranch = falseBranch
      .replace("GuardedDisplayStringDefault", "GuardedDisplayStringNumber")
      .replace("room < 0", "room >= 0");
    const result = compileOk(falseBranch);

    expect(hasOptimization(result, "display-string-guarded-show")).toBe(true);
    expect(runCompiledDisplay(falseBranch)).toBe("1-,");
    expect(runCompiledDisplay(trueBranch)).toBe("13,");
  });

  it("moves guarded display string defaults through spaced shows", () => {
    const falseBranch = `
program GuardedSpacedDisplayStringDefault {
  state {
    room: counter 1..9 = 1
    clue: counter 0..9 = 0
  }

  loop {
    clue = "-"
    if room < 0 {
      clue = 3
    }
    show(room, " ", clue)
  }
}
`;
    const trueBranch = falseBranch
      .replace("GuardedSpacedDisplayStringDefault", "GuardedSpacedDisplayStringNumber")
      .replace("room < 0", "room >= 0");
    const result = compileOk(falseBranch);

    expect(hasOptimization(result, "display-string-guarded-show")).toBe(true);
    expect(runCompiledDisplay(falseBranch)).toBe("1 -,");
    expect(runCompiledDisplay(trueBranch)).toBe("1 3,");
  });

  it("moves guarded display string defaults from inlined helpers to show", () => {
    const falseBranch = `
program GuardedDisplayStringHelper {
  state {
    room: counter 1..9 = 1
    clue: counter 0..9 = 0
  }

  loop {
    sense()
    show(room, " ", clue)
  }

  fn sense() {
    clue = "-"
    if room < 0 {
      clue = 3
    }
  }
}
`;
    const trueBranch = falseBranch
      .replace("GuardedDisplayStringHelper", "GuardedDisplayStringHelperNumber")
      .replace("room < 0", "room >= 0");
    const result = compileOk(falseBranch);

    expect(hasOptimization(result, "display-string-guarded-show")).toBe(true);
    expect(runCompiledDisplay(falseBranch)).toBe("1 -,");
    expect(runCompiledDisplay(trueBranch)).toBe("1 3,");
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

  loop {
    show(die, ".-", score:02, "-", total:03, "-", roll:02)
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

  it("renders dashed coordinate reports through the calculator video mask", () => {
    const { MK61 } = require("../emulator/mk61.cjs") as {
      MK61: new (options?: { extended?: boolean }) => {
        loadProgram: (codes: number[]) => { diagnostics: string[] };
        pressSequence: (keys: string[]) => void;
        runUntilStable: (options: { maxFrames: number; stableFrames: number }) => { stopped: boolean };
        displayText: () => string;
      };
    };
    const result = compileMKPro(`
program DashedCoordReportRun {
  field: board(0..9, 0..9)

  state {
    cell: coord(field) = 58
    foxes: coord_list(field, 1) = 0
    bearing: counter 0..9 = 0
  }



  loop {
    bearing = line_count(foxes, cell)
    show("--", cell:02, "--", bearing)
  }
}
`);
    expect(hasOptimization(result, "dashed-coord-report-lowering")).toBe(true);
    expect(hasOptimization(result, "coord-list-line-count-dashed-report-body")).toBe(true);
    expect(hasOptimization(result, "dashed-coord-report-packed-body")).toBe(true);
    expect(stepComments(result)).not.toContain("display dashed cell scale");
    expect(result.report.setupProgram).toBeDefined();
    const calc = new MK61({ extended: true });
    const setup = result.report.setupProgram?.steps.map((step) => step.opcode) ?? [];
    expect(calc.loadProgram(setup).diagnostics).toEqual([]);
    calc.pressSequence(["В/О", "С/П"]);
    expect(calc.runUntilStable({ maxFrames: 300, stableFrames: 5 }).stopped).toBe(true);
    expect(calc.loadProgram(result.steps.map((step) => step.opcode)).diagnostics).toEqual([]);
    calc.pressSequence(["В/О", "С/П"]);
    expect(calc.runUntilStable({ maxFrames: 1000, stableFrames: 8 }).stopped).toBe(true);
    expect(calc.displayText()).toBe("--58-- 0,");
  });

  it("fuses coord_list hit checks with the following line_count scan", () => {
    const result = compileOk(`
program FusedFoxScan {
  field: board(0..9, 0..9)

  state {
    cell: coord(field)
    foxes: coord_list(field, 2) = 0
    bearing: counter 0..9 = 0
  }





  loop {
    cell = read()
    if cell in foxes {
      show("-20")
    }
    bearing = line_count(foxes, cell)
    show("--", cell:02, "--", bearing)
  }
}
`);

    expect(
      hasOptimization(result, "coord-list-fused-hit-line-count") ||
        hasOptimization(result, "coord-list-scaled-fused-hit-line-count"),
    ).toBe(true);
    expect(hasOptimization(result, "coord-list-fused-dashed-report-body")).toBe(true);
    expect(hasOptimization(result, "dashed-coord-report-packed-body")).toBe(true);
    expect(hasOptimization(result, "coord-list-indirect-membership")).toBe(false);
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

  loop {
    show(die, ".-", score:02, "-", total:03, "-", roll:02)
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

  loop {
    match selector {
      1 => show(die, ".-", score:02, "-", total:03, "-", roll:02)
      2 => show(die, ".-", score:02, "-", total:03, "-", roll:02)
      otherwise => halt(0)
    }
  }
}
`);

    expect(hasOptimization(result, "display-byte-helper")).toBe(true);
    expect(hasOptimization(result, "display-byte-helper-call")).toBe(true);
    expect(result.report.candidates.some((candidate) =>
      candidate.site.startsWith("display __inline_show_") &&
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

  loop {
    show(a, b)
  }
}
`);

    const displayCandidates = result.report.candidates.filter((candidate) =>
      candidate.site.startsWith("display __inline_show_")
    );
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

  loop {
    show("8.E6-EГ C")
  }
}
`);

    expect(hasOptimization(result, "screen-video-literal-lowering")).toBe(true);
    expect(result.steps.map((step) => step.mnemonic)).toEqual(expect.arrayContaining(["К ИНВ", "С/П"]));
    expect(result.report.machineFeaturesUsed.some((feature) => feature.id === "display-bytes")).toBe(true);
  });

  it("lowers edge-space and leading-zero literal-only strings without preloaded display corruption", () => {
    const spaced = `
program EdgeSpaceLiteralScreen {

  loop {
    show(" L ")
  }
}
`;
    const leadingZero = `
program LeadingZeroLiteralScreen {

  loop {
    show("0 L")
  }
}
`;

    expect(runCompiledDisplay(spaced)).toBe("L,");
    expect(runCompiledDisplay(leadingZero)).toBe("L,");
  });

  it("lowers empty literal screens as plain pauses", () => {
    const result = compileOk(`
program EmptyLiteralScreen {



  loop {
    show("")
    show()
    show()
  }
}
`);

    expect(result.report.optimizations.filter((item) => item.name === "screen-empty-literal-lowering")).toHaveLength(3);
    expect(result.steps.some((step) => step.mnemonic === "С/П")).toBe(true);
    expect(result.report.machineFeaturesUsed.some((feature) => feature.id === "display-bytes")).toBe(false);
    expect(runCompiledDisplay(`
program EmptyLiteralScreenRun {

  loop {
    show("")
  }
}
`)).toBe("0,");
    expect(runCompiledDisplay(`
program BareEmptyScreenRun {

  loop {
    show("")
  }
}
`)).toBe("0,");
  });

  it("lowers literal calculator error screens as resumable pauses", () => {
    const source = `
program LiteralErrorScreen {


  loop {
    show("ЕГГОГ")
    show("1")
  }
}
`;

    const result = compileOk(source);
    expect(hasOptimization(result, "screen-video-literal-lowering")).toBe(true);
    expect(hasOptimization(result, "screen-error-literal-lowering")).toBe(true);
    expect(result.report.machineFeaturesUsed.some((feature) => feature.id === "error-stops")).toBe(true);
    expect(result.steps.slice(0, 2).map((step) => step.opcode)).toEqual([0x2b, 0x54]);
    expect(result.steps.map((step) => step.mnemonic)).not.toEqual(expect.arrayContaining(["F 1/x"]));

    const { MK61 } = require("../emulator/mk61.cjs") as {
      MK61: new () => {
        loadProgram: (codes: number[]) => { diagnostics: string[] };
        pressSequence: (keys: string[]) => void;
        press: (key: string) => void;
        runUntilStable: (options: { maxFrames: number; stableFrames: number }) => { stopped: boolean };
        displayText: () => string;
      };
    };
    const calc = new MK61();
    expect(calc.loadProgram(result.steps.map((step) => step.opcode)).diagnostics).toEqual([]);
    calc.pressSequence(["В/О", "С/П"]);
    expect(calc.runUntilStable({ maxFrames: 200, stableFrames: 6 }).stopped).toBe(true);
    expect(calc.displayText().toUpperCase()).toContain("ЕГГ");
    calc.press("С/П");
    expect(calc.runUntilStable({ maxFrames: 200, stableFrames: 6 }).stopped).toBe(true);
    expect(calc.displayText()).toBe("1,");
  });

  it("lowers zero-digit literal tails through the 0C-tail display trick", () => {
    const source = `
program ZeroDigitTailScreen {

  loop {
    show("2Е")
  }
}
`;

    const result = compileOk(source);
    expect(hasOptimization(result, "screen-zero-digit-tail-lowering")).toBe(true);
    expect(runCompiledDisplay(source)).toBe("2Е,");
  });

  it("lowers arbitrary display-alphabet literals by splicing the first digit", () => {
    const source = `
program LiteralAlphabetScreen {



  loop {
    show("---")
    show("Е-LСГ90")
    show("8-LСГ90")
  }
}
`;

    const result = compileOk(source);
    expect(
      hasOptimization(result, "screen-text-literal-first-splice") ||
        hasOptimization(result, "screen-text-literal-preload"),
    ).toBe(true);
    expect(hasOptimization(result, "setup-display-literal-minus-source-reuse")).toBe(true);
    expect(hasOptimization(result, "setup-display-literal-first-digit-reuse")).toBe(true);
    expect(result.report.machineFeaturesUsed.some((feature) => feature.id === "display-bytes")).toBe(true);
    expect(runCompiledDisplay(`
program LiteralDashScreen {

  loop {
    show("---")
  }
}
`)).toBe("---,");
    expect(runCompiledDisplay(`
program LiteralMixedScreen {

  loop {
    show("Е-LСГ90")
  }
}
`)).toBe("Е-LСГ90,");
    expect(runCompiledDisplay(`
program LiteralLeadingMinusScreen {

  loop {
    show("-LСГ90")
  }
}
`)).toBe("-LСГ90,");
    expect(runCompiledDisplay(`
program LiteralEightScreen {

  loop {
    show("8-LСГ90")
  }
}
`)).toBe("8-LСГ90,");
  });

  it("lowers game-style text literals that use sign and exponent display slots", () => {
    const source = `
program GameStyleTextScreens {



  loop {
    show("700-----8")
    show("-9.С L -03")
    show("-CЛOBO-")
  }
}
`;

    const result = compileOk(source);
    expect(hasOptimization(result, "screen-text-literal-preload")).toBe(true);
    expect(result.report.machineFeaturesUsed.some((feature) => feature.id === "display-bytes")).toBe(true);
    expect(runCompiledDisplay(`
program WarningTextScreen {

  loop {
    show("700-----8")
  }
}
`)).toBe("7,00----- 08");
    expect(runCompiledDisplay(`
program SignedCaveTextScreen {

  loop {
    show("-9.С L -03")
  }
}
`)).toBe("-9,С L -03");
    expect(runCompiledDisplay(`
program CyrillicWordTextScreen {

  loop {
    show("-CЛOBO-")
  }
}
`)).toBe("-СL0L0-,");
  });

  it("lowers wider sign-digit literal screens through indirect display construction", () => {
    const source = `
program SignDigitLiteralScreen {

  loop {
    show("3Е0000021")
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
  loop {
    show("ЕГГОГ")
    halt()
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

  it("lowers literal terminal video strings as one visible stop", () => {
    const result = compileOk(`
program LiteralTerminalVideoStop {
  loop {
    halt("8СГ-Е-78")
  }
}
`);

    expect(hasOptimization(result, "terminal-literal-stop")).toBe(true);
    expect(result.steps.filter((step) => step.mnemonic === "С/П")).toHaveLength(1);
    expect(runCompiledDisplay(`
program LiteralTerminalVideoStop {
  loop {
    halt("8СГ-Е-78")
  }
}
`)).toContain("СГ-Е-78");
  });

  it("lowers first-splice literal terminal stops", () => {
    const result = compileOk(`
program FirstSpliceLiteralTerminalStop {
  loop {
    halt("Г16ЕL 91")
  }
}
`);

    expect(hasOptimization(result, "terminal-literal-stop")).toBe(true);
    expect(result.steps.filter((step) => step.mnemonic === "С/П")).toHaveLength(1);
  });

  it("lowers signed literal calculator video strings", () => {
    const result = compileOk(`
program SignedLiteralVideoScreen {

  loop {
    show("-8CEC6-L-")
  }
}
`);

    expect(hasOptimization(result, "screen-video-literal-lowering")).toBe(true);
    expect(result.steps.map((step) => step.mnemonic)).toEqual(expect.arrayContaining(["К ИНВ", "/-/", "С/П"]));
  });
});
