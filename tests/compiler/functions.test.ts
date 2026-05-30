import { describe, expect, it } from "vitest";
import { compileMKPro } from "../../src/core/index.ts";
import type { CompileOptions } from "../../src/core/index.ts";

function compileOk(source: string, options: Partial<CompileOptions> = { budget: 999 }) {
  return compileMKPro(source, options);
}

describe("rule functions (return)", () => {
  it("lowers a value-returning function called in an assignment to ПП/В/О with the result in X", () => {
    const result = compileOk(`
program FunctionDemo {
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
`);
    const hex = result.steps.map((step) => step.hex);
    expect(hex).toContain("53"); // ПП (subroutine call)
    expect(hex).toContain("52"); // В/О (return)
    // The function call sets the argument register, calls, and the result is
    // consumed straight from X by the following halt.
    expect(result.report.optimizations.some((opt) => opt.name === "function-call")).toBe(true);
  });

  it("hoists a nested function call into a temporary so the working stack is preserved", () => {
    const result = compileOk(`
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
`);
    expect(result.report.optimizations.some((opt) => opt.name === "function-call-lifting")).toBe(true);
    // Two distinct subroutine calls are emitted, one per function.
    const calls = result.steps.filter((step) => step.hex === "53").length;
    expect(calls).toBe(2);
  });

  it("supports early returns on every branch of a function", () => {
    const result = compileOk(`
program Sign {
  state {
    result: counter 0..99 = 0
  }
  fn sign(n) {
    if n < 0 {
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
`);
    expect(result.steps.map((step) => step.hex)).toContain("52");
  });

  it("rejects a return outside of a function", () => {
    expect(() =>
      compileMKPro(`
program BadReturn {
  loop {
    return 1
  }
}
`)
    ).toThrow(/'return' is only allowed inside a function/u);
  });

  it("rejects a function that does not return on every path", () => {
    expect(() =>
      compileMKPro(`
program PartialReturn {
  state {
    result: counter 0..99 = 0
  }
  fn maybe(n) {
    if n < 0 {
      return 0
    }
  }
  loop {
    x = read()
    result = maybe(x)
    halt(result)
  }
}
`)
    ).toThrow(/must return a value on every path/u);
  });

  it("rejects direct recursion in a function", () => {
    expect(() =>
      compileMKPro(`
program Recursive {
  state {
    result: counter 0..99 = 0
  }
  fn loopy(n) {
    return loopy(n)
  }
  loop {
    x = read()
    result = loopy(x)
    halt(result)
  }
}
`)
    ).toThrow(/Recursive function 'loopy' is not supported/u);
  });

  it("rejects mutual recursion between functions", () => {
    expect(() =>
      compileMKPro(`
program Mutual {
  state {
    result: counter 0..99 = 0
  }
  fn ping(n) {
    return pong(n)
  }
  fn pong(n) {
    return ping(n)
  }
  loop {
    x = read()
    result = ping(x)
    halt(result)
  }
}
`)
    ).toThrow(/Recursive function '(ping|pong)' is not supported/u);
  });

  it("requires a value after return", () => {
    expect(() =>
      compileMKPro(`
program EmptyReturn {
  state {
    result: counter 0..99 = 0
  }
  fn nothing(n) {
    return
  }
  loop {
    x = read()
    result = nothing(x)
    halt(result)
  }
}
`)
    ).toThrow(/'return' must return a value/u);
  });
});
