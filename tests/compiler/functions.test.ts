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

  it("lowers direct tail recursion to jumps instead of growing the return stack", () => {
    const result = compileOk(`
program Recursive {
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
`);
    expect(result.report.optimizations.some((opt) => opt.name === "function-tail-recursion")).toBe(true);
    expect(result.steps.filter((step) => step.hex === "53").length).toBe(1);
  });

  it("lowers mutual tail recursion to jumps between function bodies", () => {
    const result = compileOk(`
program Mutual {
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
`);
    expect(result.report.optimizations.some((opt) => opt.name === "function-tail-call")).toBe(true);
    expect(result.steps.filter((step) => step.hex === "53").length).toBe(1);
  });

  it("lowers non-recursive value-function tail calls to direct jumps", () => {
    const result = compileOk(`
program TailForward {
  state {
    result: counter 0..99 = 0
  }
  fn id(n) {
    return n
  }
  fn forward(n) {
    return id(n + 1)
  }
  loop {
    x = read()
    result = forward(x)
    halt(result)
  }
}
`);
    expect(result.report.optimizations.some((opt) => opt.name === "function-tail-call")).toBe(true);
    expect(result.steps.filter((step) => step.hex === "53").length).toBe(1);
  });

  it("allows five nested hardware return-stack frames", () => {
    const result = compileOk(`
program FiveDeep {
  state {
    result: counter 0..999 = 0
  }
  fn f1(n) {
    return f2(n) + 1
  }
  fn f2(n) {
    return f3(n) + 1
  }
  fn f3(n) {
    return f4(n) + 1
  }
  fn f4(n) {
    return f5(n) + 1
  }
  fn f5(n) {
    return n
  }
  loop {
    x = read()
    result = f1(x)
    halt(result)
  }
}
`);
    expect(result.diagnostics.some((diagnostic) => diagnostic.code === "RETURN_STACK_DEPTH_EXCEEDED")).toBe(false);
  });

  it("rejects six nested hardware return-stack frames", () => {
    expect(() =>
      compileMKPro(`
program SixDeep {
  state {
    result: counter 0..999 = 0
  }
  fn f1(n) {
    return f2(n) + 1
  }
  fn f2(n) {
    return f3(n) + 2
  }
  fn f3(n) {
    return f4(n) + 3
  }
  fn f4(n) {
    return f5(n) + 4
  }
  fn f5(n) {
    return f6(n) + 5
  }
  fn f6(n) {
    return random(9)
  }
  loop {
    x = read()
    result = f1(x)
    halt(result)
  }
}
`, { budget: 999 })
    ).toThrow(/return stack holds at most 5 nested ПП frame\(s\).*depth 6/u);
  });

  it("does not count function tail-call chains as nested return frames", () => {
    const result = compileOk(`
program TailChain {
  state {
    result: counter 0..999 = 0
  }
  fn f1(n) {
    return f2(n)
  }
  fn f2(n) {
    return f3(n)
  }
  fn f3(n) {
    return f4(n)
  }
  fn f4(n) {
    return f5(n)
  }
  fn f5(n) {
    return f6(n)
  }
  fn f6(n) {
    return f7(n)
  }
  fn f7(n) {
    return n
  }
  loop {
    x = read()
    result = f1(x)
    halt(result)
  }
}
`);
    expect(result.report.optimizations.some((opt) => opt.name === "function-tail-call")).toBe(true);
    expect(result.diagnostics.some((diagnostic) => diagnostic.code === "RETURN_STACK_DEPTH_EXCEEDED")).toBe(false);
  });

  it("rejects direct recursion outside tail position", () => {
    expect(() =>
      compileMKPro(`
program Recursive {
  state {
    result: counter 0..99 = 0
  }
  fn loopy(n) {
    return loopy(n) + 1
  }
  loop {
    x = read()
    result = loopy(x)
    halt(result)
  }
}
`)
    ).toThrow(/non-tail call to 'loopy'/u);
  });

  it("rejects mutual recursion when a cycle edge is not a tail call", () => {
    expect(() =>
      compileMKPro(`
program Mutual {
  state {
    result: counter 0..99 = 0
  }
  fn ping(n) {
    return pong(n) + 1
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
    ).toThrow(/non-tail call to 'pong'/u);
  });

  it("rejects recursion hidden inside the arguments of a tail call", () => {
    expect(() =>
      compileMKPro(`
program Hidden {
  state {
    result: counter 0..99 = 0
  }
  fn wrap(n) {
    return finish(wrap(n - 1))
  }
  fn finish(n) {
    return n
  }
  loop {
    x = read()
    result = wrap(x)
    halt(result)
  }
}
`)
    ).toThrow(/non-tail call to 'wrap'/u);
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
