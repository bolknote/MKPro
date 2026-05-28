import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";
import { optimizePostLayoutIndirectFlow } from "../../src/core/post-layout-indirect-flow.ts";
import type { MachineItem } from "../../src/core/types.ts";

const require = createRequire(import.meta.url);

interface Mk61Instance {
  setRegister(register: string, value: string): void;
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  pressSequence(keys: string[]): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): { stopped: boolean; frames: number };
  displayText(): string;
}
type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;
const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

const MK61_HEX_DIGITS: Record<string, string> = { A: "-", B: "L", C: "С", D: "Г", E: "Е", F: "_" };
function mk61HexLiteral(text: string): string {
  return [...text.toUpperCase()].map((digit) => MK61_HEX_DIGITS[digit] ?? digit).join("");
}

function op(opcode: number, mnemonic: string): MachineItem {
  return { kind: "op", opcode, mnemonic };
}
const address = (target: string): MachineItem => ({ kind: "address", target });
const label = (name: string): MachineItem => ({ kind: "label", name });

// Two backward jumps to the same head: both want the same dark selector for
// target 5, so the pass allocates two selector registers with the identical
// value, which the merge then collapses to one.
function twoSiteProgram(): MachineItem[] {
  const items: MachineItem[] = [];
  items.push(op(0x51, "БП"), address("site1")); // 0,1: enter via the first site
  items.push(op(0x0d, "Cx"), op(0x0d, "Cx"), op(0x0d, "Cx")); // 2..4 filler
  items.push(label("head"), op(0x01, "1")); // 5: head pushes marker "1"
  items.push(op(0x50, "С/П")); // 6: halt
  items.push(label("site1"), op(0x51, "БП"), address("head")); // 7,8: jump back to head
  items.push(label("site2"), op(0x51, "БП"), address("head")); // 9,10: second (dead) site, same target
  return items;
}

const options = { delivery: "manual" as const, budget: 999999, analysis: true };

function lower(items: readonly MachineItem[]): number[] {
  const addressOf = new Map<string, number>();
  let addr = 0;
  for (const item of items) {
    if (item.kind === "label") addressOf.set(item.name, addr);
    else addr += 1;
  }
  const codes: number[] = [];
  for (const item of items) {
    if (item.kind === "label") continue;
    if (item.kind === "op") codes.push(item.opcode);
    else codes.push(typeof item.target === "number" ? item.target : addressOf.get(item.target)!);
  }
  return codes;
}

function run(codes: number[], preloads: Array<{ register: string; value: string }>): { display: string; stopped: boolean } {
  const calc = new MK61();
  calc.loadProgram(codes);
  for (const preload of preloads) calc.setRegister(preload.register, mk61HexLiteral(preload.value));
  calc.pressSequence(["В/О", "С/П"]);
  const stable = calc.runUntilStable({ maxFrames: 400, stableFrames: 5 });
  return { display: calc.displayText().trim(), stopped: stable.stopped };
}

describe("constants-dual-use selector merge behavioral equivalence (real emulator)", () => {
  const program = twoSiteProgram();
  const result = optimizePostLayoutIndirectFlow(program, options, 0);

  it("rewrites both sites and shares one selector register", () => {
    expect(result.applied).toBe(2);
    expect(result.optimizations.some((o) => o.name === "constants-dual-use")).toBe(true);
    const flowPreloads = result.preloads.filter((p) => p.countsAgainstProgram === false);
    // One shared selector value -> exactly one preload, two indirect branches.
    const distinctValues = new Set(flowPreloads.map((p) => p.value));
    expect(distinctValues.size).toBe(1);
    expect(flowPreloads.length).toBe(1);
    const indirect = result.items.filter((i) => i.kind === "op" && i.opcode >= 0x80 && i.opcode <= 0x8e);
    expect(indirect.length).toBe(2);
    const registers = new Set(indirect.map((i) => (i.kind === "op" ? i.opcode : 0)));
    expect(registers.size).toBe(1); // both branches go through the same register
  });

  it("behaves identically before and after the merge on the emulator", () => {
    const before = run(lower(program), []);
    const flowPreloads = result.preloads
      .filter((p) => p.countsAgainstProgram === false)
      .map((p) => ({ register: p.register, value: p.value }));
    const after = run(lower(result.items), flowPreloads);

    expect(before.stopped).toBe(true);
    expect(before.display).toContain("1");
    expect(after.stopped).toBe(before.stopped);
    expect(after.display).toBe(before.display);
  });
});
