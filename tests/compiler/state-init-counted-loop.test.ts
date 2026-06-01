import { describe, expect, it } from "vitest";
import { compileMKPro } from "../../src/core/index.ts";

function opcodes(source: string): number[] {
  return compileMKPro(source, { budget: 999 }).steps.map((step) => step.opcode);
}

function firedStateInitCountedLoop(source: string): boolean {
  return compileMKPro(source, { budget: 999 }).report.optimizations.some(
    (opt) => opt.name === "state-init-counted-loop",
  );
}

// The compiler lowers an explicitly initialized countdown loop through a compact
// one-cell F Lx counter. A loop written with the initial value on the state field
// should reach exactly the same lowering: the state-init form is normalized into
// the explicit form before code generation, so the two compile byte-for-byte
// identically and the F Lx counter is recovered.
describe("state-initialized counted loops", () => {
  const stateInit = `
program C {
  state {
    t: counter 0..25 = 25
    acc: counter 0..99 = 0
  }
  while t >= 1 {
    acc = acc + 1
    t--
  }
  halt(acc)
}
`;
  const explicit = `
program C {
  state {
    t: counter 0..25
    acc: counter 0..99 = 0
  }
  t = 25
  while t >= 1 {
    acc = acc + 1
    t--
  }
  halt(acc)
}
`;

  it("compiles the state-init countdown identically to the explicit-init form", () => {
    expect(opcodes(stateInit)).toEqual(opcodes(explicit));
  });

  it("recovers the compact F Lx counted loop (one-cell counter opcode present)", () => {
    expect(firedStateInitCountedLoop(stateInit)).toBe(true);
    // 0x58 = F L2 — the counted-loop counter for register 2 (the only counter here).
    expect(opcodes(stateInit)).toContain(0x58);
  });

  it("leaves the explicit-init form untouched (no normalization needed)", () => {
    expect(firedStateInitCountedLoop(explicit)).toBe(false);
  });

  it("does not normalize when the counter is also used outside the loop", () => {
    // `t` is read after the loop, so its state-init value is observable elsewhere;
    // moving the initializer would change semantics, so the pass must not fire.
    const reusedCounter = `
program C {
  state {
    t: counter 0..25 = 25
    acc: counter 0..99 = 0
  }
  while t >= 1 {
    acc = acc + 1
    t--
  }
  halt(t)
}
`;
    expect(firedStateInitCountedLoop(reusedCounter)).toBe(false);
  });
});
