import { readFileSync, readdirSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import { compileM61 } from "../../src/core/compiler.ts";
import {
  lowerIrToLayout,
  lowerIrToMachine,
  machineItemsEqual,
  raiseLayoutToIr,
  raiseMachineToIr,
} from "../../src/core/ir.ts";
import type { LayoutIrCell, MachineItem } from "../../src/core/types.ts";

function exampleFiles(): string[] {
  const dir = resolve("examples");
  return readdirSync(dir)
    .filter((name) => name.endsWith(".m61"))
    .map((name) => resolve(dir, name));
}

function describeDifference(a: MachineItem, b: MachineItem): string {
  return `expected ${JSON.stringify(a)}\n  got    ${JSON.stringify(b)}`;
}

describe("ir round-trip", () => {
  const files = exampleFiles();
  expect(files.length).toBeGreaterThanOrEqual(16);

  for (const file of files) {
    it(`round-trips ${file.split("/").pop()} losslessly`, () => {
      const source = readFileSync(file, "utf8");
      const result = compileM61(source);
      const ir = raiseMachineToIr(result.items);
      const back = lowerIrToMachine(ir);

      expect(back.length).toBe(result.items.length);
      for (let i = 0; i < result.items.length; i += 1) {
        const original = result.items[i]!;
        const restored = back[i]!;
        if (!machineItemsEqual(original, restored)) {
          throw new Error(`Item ${i} mismatch:\n  ${describeDifference(original, restored)}`);
        }
      }
    });
  }

  it("preserves stop semantic heuristic for common comment patterns", () => {
    const items: MachineItem[] = [
      { kind: "op", opcode: 0x50, mnemonic: "С/П", comment: "halt" },
      { kind: "op", opcode: 0x50, mnemonic: "С/П", comment: "pause" },
      { kind: "op", opcode: 0x50, mnemonic: "С/П", comment: "show main" },
      { kind: "op", opcode: 0x50, mnemonic: "С/П", comment: "ask key" },
      { kind: "op", opcode: 0x50, mnemonic: "С/П", comment: "input digit x" },
      { kind: "op", opcode: 0x50, mnemonic: "С/П", comment: "implicit final stop" },
      { kind: "op", opcode: 0x50, mnemonic: "С/П" },
    ];
    const ir = raiseMachineToIr(items);
    const semantics = ir.map((op) => (op.kind === "stop" ? op.semantic : "n/a"));
    expect(semantics).toEqual(["halt", "pause", "show", "ask", "input", "halt", "unknown"]);
  });

  it("identifies all jump and call kinds", () => {
    const items: MachineItem[] = [
      { kind: "op", opcode: 0x51, mnemonic: "БП" },
      { kind: "address", target: "main" },
      { kind: "op", opcode: 0x53, mnemonic: "ПП" },
      { kind: "address", target: 12 },
      { kind: "op", opcode: 0x57, mnemonic: "F x!=0" },
      { kind: "address", target: "skip" },
      { kind: "op", opcode: 0x59, mnemonic: "F x>=0" },
      { kind: "address", target: "skip" },
      { kind: "op", opcode: 0x5c, mnemonic: "F x<0" },
      { kind: "address", target: "skip" },
      { kind: "op", opcode: 0x5e, mnemonic: "F x=0" },
      { kind: "address", target: "skip" },
      { kind: "op", opcode: 0x58, mnemonic: "F L2" },
      { kind: "address", target: "loop" },
      { kind: "op", opcode: 0x5b, mnemonic: "F L1" },
      { kind: "address", target: "loop" },
      { kind: "op", opcode: 0x52, mnemonic: "В/О" },
    ];
    const ir = raiseMachineToIr(items);
    expect(ir.map((op) => op.kind)).toEqual([
      "jump",
      "call",
      "cjump",
      "cjump",
      "cjump",
      "cjump",
      "loop",
      "loop",
      "return",
    ]);
    expect(ir[2]?.kind === "cjump" && ir[2].condition === "!=0").toBe(true);
    expect(ir[3]?.kind === "cjump" && ir[3].condition === ">=0").toBe(true);
    expect(ir[4]?.kind === "cjump" && ir[4].condition === "<0").toBe(true);
    expect(ir[5]?.kind === "cjump" && ir[5].condition === "==0").toBe(true);
    expect(ir[6]?.kind === "loop" && ir[6].counter === "L2").toBe(true);
    expect(ir[7]?.kind === "loop" && ir[7].counter === "L1").toBe(true);
  });

  it("identifies store and recall direct and indirect variants", () => {
    const items: MachineItem[] = [
      { kind: "op", opcode: 0x41, mnemonic: "X->П 1" },
      { kind: "op", opcode: 0x65, mnemonic: "П->X 5" },
      { kind: "op", opcode: 0xb3, mnemonic: "К X->П 3" },
      { kind: "op", opcode: 0xd7, mnemonic: "К П->X 7" },
    ];
    const ir = raiseMachineToIr(items);
    expect(ir.map((op) => op.kind)).toEqual([
      "store",
      "recall",
      "indirect-store",
      "indirect-recall",
    ]);
    expect(ir[0]?.kind === "store" && ir[0].register === "1").toBe(true);
    expect(ir[1]?.kind === "recall" && ir[1].register === "5").toBe(true);
    expect(ir[2]?.kind === "indirect-store" && ir[2].register === "3").toBe(true);
    expect(ir[3]?.kind === "indirect-recall" && ir[3].register === "7").toBe(true);
  });

  it("identifies indirect jump, call, and cond-jump variants", () => {
    const items: MachineItem[] = [
      { kind: "op", opcode: 0x82, mnemonic: "К БП 2" },
      { kind: "op", opcode: 0xa4, mnemonic: "К ПП 4" },
      { kind: "op", opcode: 0x71, mnemonic: "К x!=0 1" },
      { kind: "op", opcode: 0x93, mnemonic: "К x>=0 3" },
      { kind: "op", opcode: 0xc5, mnemonic: "К x<0 5" },
      { kind: "op", opcode: 0xe7, mnemonic: "К x=0 7" },
    ];
    const ir = raiseMachineToIr(items);
    expect(ir.map((op) => op.kind)).toEqual([
      "indirect-jump",
      "indirect-call",
      "indirect-cjump",
      "indirect-cjump",
      "indirect-cjump",
      "indirect-cjump",
    ]);
    expect(ir[2]?.kind === "indirect-cjump" && ir[2].condition === "!=0").toBe(true);
    expect(ir[5]?.kind === "indirect-cjump" && ir[5].condition === "==0").toBe(true);
  });

  it("round-trips LayoutIrCell cell-by-cell for GameIntent-shaped output", () => {
    const cells: LayoutIrCell[] = [
      { address: 0, opcode: 0x41, roles: ["exec"], tactic: "store" },
      { address: 1, opcode: 0x51, roles: ["exec"], tactic: "jump" },
      { address: 2, opcode: 0x10, roles: ["address"], tactic: "jump target" },
      { address: 3, opcode: 0x65, roles: ["exec"], tactic: "recall" },
      { address: 4, opcode: 0x10, roles: ["exec"], tactic: "add" },
      { address: 5, opcode: 0x50, roles: ["exec"], tactic: "halt" },
    ];
    const ir = raiseLayoutToIr(cells);
    const lowered = lowerIrToLayout(ir);
    expect(lowered.cells.map((c) => c.opcode)).toEqual(cells.map((c) => c.opcode));
    expect(lowered.cells.map((c) => c.address)).toEqual(cells.map((c) => c.address));
    expect(ir[1]?.kind === "jump" && ir[1].target === 0x10).toBe(true);
  });

  it("preserves comments, source lines, and raw flags through round trip", () => {
    const items: MachineItem[] = [
      { kind: "label", name: "loop" },
      {
        kind: "op",
        opcode: 0x41,
        mnemonic: "X->П 1",
        comment: "store value",
        sourceLine: 12,
        raw: true,
      },
      {
        kind: "op",
        opcode: 0x51,
        mnemonic: "БП",
        comment: "jump back",
        sourceLine: 13,
      },
      {
        kind: "address",
        target: "loop",
        comment: "loop target",
        sourceLine: 13,
      },
    ];
    const back = lowerIrToMachine(raiseMachineToIr(items));
    expect(back).toEqual(items);
  });
});
