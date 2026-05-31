import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";
import { compileMKPro } from "../../src/core/index.ts";

const require = createRequire(import.meta.url);

interface RunResult {
  stopped: boolean;
  frames: number;
}

interface Mk61Instance {
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  pressSequence(keys: string[]): void;
  inputNumber(value: string, options?: { clear?: boolean }): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): RunResult;
  displayText(): string;
}

type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;

const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

function displayInteger(calc: Mk61Instance): number {
  return Number.parseInt(calc.displayText().replace(",", ".").replace(/\.$/u, ""), 10);
}

function runWithInput(source: string, value: number): number {
  const result = compileMKPro(source, { budget: 999 });
  const codes = result.steps.map((step) => step.opcode);
  const calc = new MK61();
  const loaded = calc.loadProgram(codes);
  expect(loaded.diagnostics).toEqual([]);
  calc.pressSequence(["В/О", "С/П"]);
  calc.inputNumber(String(value));
  calc.pressSequence(["С/П"]);
  const run = calc.runUntilStable({ maxFrames: 400, stableFrames: 5 });
  expect(run.stopped).toBe(true);
  return displayInteger(calc);
}

describe("rule functions on the real emulator", () => {
  it("returns n + n from a single function call", () => {
    const source = `
program Double {
  state {
    result: counter 0..99 = 0
  }
  fn double(n) {
    return n + n
  }
  loop {
    x = read()
    result = double(x)
    halt(result)
  }
}
`;
    expect(runWithInput(source, 3)).toBe(6);
    expect(runWithInput(source, 9)).toBe(18);
  });

  it("composes nested function calls dbl(inc(x))", () => {
    const source = `
program Nested {
  state {
    result: counter 0..99 = 0
  }
  fn inc(n) {
    return n + 1
  }
  fn dbl(n) {
    return n + n
  }
  loop {
    x = read()
    result = dbl(inc(x))
    halt(result)
  }
}
`;
    expect(runWithInput(source, 3)).toBe(8); // dbl(4) = 8
    expect(runWithInput(source, 10)).toBe(22); // dbl(11) = 22
  });

  it("evaluates a function call mixed into a larger expression", () => {
    const source = `
program Mixed {
  state {
    result: counter 0..99 = 0
  }
  fn triple(n) {
    return n + n + n
  }
  loop {
    x = read()
    result = 1 + triple(x)
    halt(result)
  }
}
`;
    expect(runWithInput(source, 2)).toBe(7); // 1 + 6
    expect(runWithInput(source, 5)).toBe(16); // 1 + 15
  });

  it("honors early returns from each branch of a function", () => {
    const source = `
program Sign {
  state {
    result: counter 0..99 = 0
  }
  fn sign(n) {
    if n < 1 {
      return 0
    }
    else {
      return 1
    }
  }
  loop {
    x = read()
    result = sign(x)
    halt(result)
  }
}
`;
    expect(runWithInput(source, 0)).toBe(0);
    expect(runWithInput(source, 5)).toBe(1);
  });

  it("runs direct tail recursion without accumulating return frames", () => {
    const source = `
program TailCount {
  state {
    result: counter 0..99 = 0
  }
  fn count_down(n, acc) {
    if n <= 0 {
      return acc
    }
    else {
      return count_down(n - 1, acc + 1)
    }
  }
  loop {
    x = read()
    result = count_down(x, 0)
    halt(result)
  }
}
`;
    expect(runWithInput(source, 3)).toBe(3);
    expect(runWithInput(source, 7)).toBe(7);
  });

  it("runs mutual tail recursion without accumulating return frames", () => {
    const source = `
program TailMutual {
  state {
    result: counter 0..99 = 0
  }
  fn even(n) {
    if n <= 0 {
      return 1
    }
    else {
      return odd(n - 1)
    }
  }
  fn odd(n) {
    if n <= 0 {
      return 0
    }
    else {
      return even(n - 1)
    }
  }
  loop {
    x = read()
    result = even(x)
    halt(result)
  }
}
`;
    expect(runWithInput(source, 6)).toBe(1);
    expect(runWithInput(source, 7)).toBe(0);
  });

  it("evaluates tail-recursive arguments before rebinding parameters", () => {
    const source = `
program TailFib {
  state {
    result: counter 0..99 = 0
  }
  fn fib_step(n, a, b) {
    if n <= 0 {
      return a
    }
    else {
      return fib_step(n - 1, b, a + b)
    }
  }
  loop {
    x = read()
    result = fib_step(x, 0, 1)
    halt(result)
  }
}
`;
    expect(runWithInput(source, 6)).toBe(8);
    expect(runWithInput(source, 7)).toBe(13);
  });
});
