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
function address(target: string): MachineItem {
  return { kind: "address", target };
}
function label(name: string): MachineItem {
  return { kind: "label", name };
}

// Builds a program whose only rewritable branch is a backward jump into an FF
// super-dark entry: a one-cell command at physical 53 that falls through to a
// БП to continuation 06. Running it shows the marker "7" and halts.
function superDarkProgram(): MachineItem[] {
  const items: MachineItem[] = [];
  items.push(op(0x51, "БП"), address("site")); // 0,1: jump straight to the dispatch site
  for (let i = 0; i < 4; i += 1) items.push(op(0x0d, "Cx")); // 2..5 filler
  items.push(label("cont"), op(0x50, "С/П")); // 6: continuation halts
  for (let i = 0; i < 46; i += 1) items.push(op(0x0d, "Cx")); // 7..52 filler
  items.push(label("entry"), op(0x07, "7")); // 53: one-cell super-dark entry (marker)
  items.push(op(0x51, "БП"), address("cont")); // 54,55: fall-through jump to continuation 06
  items.push(label("site"), op(0x51, "БП"), address("entry")); // 56,57: backward jump -> FF
  return items;
}

const options = { delivery: "manual" as const, budget: 999999, analysis: true };

function run(codes: number[], preloads: readonly { register: string; value: string }[] = []): { display: string; stopped: boolean } {
  const calc = new MK61();
  calc.loadProgram(codes);
  for (const preload of preloads) calc.setRegister(preload.register, mk61HexLiteral(preload.value));
  calc.pressSequence(["В/О", "С/П"]);
  const stable = calc.runUntilStable({ maxFrames: 400, stableFrames: 5 });
  return { display: calc.displayText().trim(), stopped: stable.stopped };
}

function lower(items: readonly MachineItem[]): number[] {
  // Mirror the compiler's sequential layout: resolve label targets to addresses.
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

describe("super-dark dispatch behavioral equivalence (real emulator)", () => {
  const program = superDarkProgram();
  const result = optimizePostLayoutIndirectFlow(program, options, 0);

  it("selects a proven FF super-dark rewrite", () => {
    expect(result.applied).toBeGreaterThanOrEqual(1);
    expect(result.optimizations.some((o) => o.name === "preloaded-super-dark-flow")).toBe(true);
    const preload = result.preloads.find((candidate) => candidate.value.toUpperCase() === "FF")!;
    expect(preload.value.toUpperCase()).toBe("FF");
    expect(result.items.some((item) => item.kind === "op" && item.opcode >= 0x80 && item.opcode <= 0x8e)).toBe(true);
  });

  it("behaves identically before and after on the emulator", () => {
    const before = run(lower(program));
    const after = run(lower(result.items), result.preloads);

    expect(before.stopped).toBe(true);
    expect(before.display).toContain("7");
    expect(after.stopped).toBe(before.stopped);
    expect(after.display).toBe(before.display);
  });
});
