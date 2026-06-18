import { describe, expect, it } from "vitest";
import { compileLoweringVariantForTest } from "../../src/core/compiler.ts";
import { parseProgram } from "../../src/core/index.ts";

const segmentedProgram = `
program SegmentedSmoke {
  field: board(0..9, 0..9)

  state {
    cell: coord(field)
    marks: cells(field) = 0
    answer: counter 0..9 = 0
  }

  loop {
    cell = read()
    marks += cell
    if cell in marks {
      marks -= cell
      answer = 1
    }
    halt(answer)
  }
}
`;

const segmentedFoxHuntProgram = `
program SegmentedRandomSetup {
  field: board(0..9, 0..9)

  state {
    cell: coord(field)
    foxes: cells(field) = random()
    remaining_foxes: counter 1..100 = stack.Y
    answer: counter 0..9 = 0
  }

  loop {
    cell = read()
    if cell in foxes {
      foxes -= cell
      remaining_foxes--
      answer = -remaining_foxes
    }
    else {
      answer = line_count(foxes, cell)
    }
    halt(answer)
  }
}
`;

describe("segmented 10x10 bitplane cells", () => {
  it("lowers zero-origin 10x10 cells to four physical bitplane fields", () => {
    const ast = parseProgram(segmentedProgram, { segmentedBitplanes: true });
    const fields = ast.states.flatMap((state) => state.fields.map((field) => field.name));

    expect(fields).not.toContain("marks");
    expect(fields).toEqual(expect.arrayContaining([
      "__seg_bitplane_marks_0",
      "__seg_bitplane_marks_1",
      "__seg_bitplane_marks_2",
      "__seg_bitplane_marks_3",
    ]));
  });

  it("keeps segmented membership and updates as fused internal operations", () => {
    const ast = parseProgram(segmentedProgram, { segmentedBitplanes: true });
    const text = JSON.stringify(ast.entries[0]?.body);

    expect(text).toContain("\"callee\":\"__seg_bit_has\"");
    expect(text).toContain("\"kind\":\"segmented_bitplane_update\"");
    expect(text).toContain("\"op\":\"+=\"");
    expect(text).toContain("\"op\":\"-=\"");
    expect(text).not.toContain("\"callee\":\"bit_clear\"");
  });

  it("allocates the four bitplanes using the Anvarov-inspired register family", () => {
    const result = compileLoweringVariantForTest(`
program SegmentedHas {
  field: board(0..9, 0..9)

  state {
    cell: coord(field)
    marks: cells(field) = random()
    answer: counter 0..9 = 0
  }

  loop {
    cell = read()
    if cell in marks {
      answer = 1
    }
    halt(answer)
  }
}
`, { analysis: true, budget: 999 }, { segmentedBitplanes: true });

    expect(result.report.registers).toMatchObject({
      __seg_bitplane_selector: "7",
      __seg_bitplane_marks_0: "0",
      __seg_bitplane_marks_1: "1",
      __seg_bitplane_marks_2: "b",
      __seg_bitplane_marks_3: "e",
    });
  }, 10_000);

  it("lowers random segmented cells with a stack.Y count to unique-placement setup", () => {
    const ast = parseProgram(`
program SegmentedRandomSetup {
  field: board(0..9, 0..9)

  state {
    cell: coord(field)
    foxes: cells(field) = random()
    remaining_foxes: counter 1..100 = stack.Y
    answer: counter 0..9 = 0
  }

  loop {
    cell = read()
    answer = line_count(foxes, cell)
    halt(answer)
  }
}
`, { segmentedBitplanes: true });

    const planeInitializers = ast.states
      .flatMap((state) => state.fields)
      .filter((field) => field.name.startsWith("__seg_bitplane_foxes_"))
      .map((field) => JSON.stringify(field.initial));

    expect(planeInitializers).toHaveLength(4);
    expect(planeInitializers.every((text) => text.includes("__seg_bitplane_random_unique"))).toBe(true);
    expect(planeInitializers.every((text) => text.includes("remaining_foxes"))).toBe(true);
  });

  it("generates one unique random setup loop for counted segmented cells", () => {
    const result = compileLoweringVariantForTest(segmentedFoxHuntProgram, { analysis: true, budget: 999 }, {
      segmentedBitplanes: true,
    });
    const setupComments = result.report.setupProgram?.steps.map((step) => step.comment).filter(Boolean) ?? [];

    const optimizationNames = result.report.optimizations.map((item) => item.name);

    expect(optimizationNames).toContain("setup-segmented-bitplane-random-unique");
    expect(optimizationNames).toContain("spatial-count-fl-loop");
    expect(setupComments).toContain("segmented bitplane random collision probe");
    expect(setupComments).toContain("segmented bitplane setup complete display");
    expect(result.report.registers).toMatchObject({
      __seg_bitplane_selector: "7",
      __seg_bitplane_foxes_0: "0",
      __seg_bitplane_foxes_1: "1",
      __seg_bitplane_foxes_2: "b",
      __seg_bitplane_foxes_3: "e",
    });
  }, 10_000);

  it("can scan segmented line_count through one 10x10 board loop", () => {
    const baseline = compileLoweringVariantForTest(segmentedFoxHuntProgram, { analysis: true, budget: 999 }, {
      segmentedBitplanes: true,
    });
    const scan = compileLoweringVariantForTest(segmentedFoxHuntProgram, { analysis: true, budget: 999 }, {
      segmentedBitplanes: true,
      segmentedLineCountScan: true,
    });

    const optimizationNames = scan.report.optimizations.map((item) => item.name);

    expect(optimizationNames).toContain("segmented-bitplane-line-count-scan");
    expect(optimizationNames).not.toContain("spatial-sum-loop-helper-call");
    expect(scan.steps.length).toBeLessThan(baseline.steps.length);
  }, 10_000);

  it("fuses hit-and-clear through one indirect selected bitplane", () => {
    const result = compileLoweringVariantForTest(`
program SegmentedClear {
  field: board(0..9, 0..9)

  state {
    cell: coord(field)
    marks: cells(field) = random()
    answer: counter 0..9 = 0
  }

  loop {
    cell = read()
    if cell in marks {
      marks -= cell
      answer = 1
    }
    halt(answer)
  }
}
`, { analysis: true, budget: 999 }, { segmentedBitplanes: true });

    expect(result.report.optimizations.map((item) => item.name)).toContain("segmented-bitplane-hit-update-indirect");
    expect(result.report.registers.__seg_bitplane_selector).toBe("7");
  }, 10_000);
});
