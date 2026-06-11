import { describe, expect, it } from "vitest";

import {
  buildCfgEdges,
  buildCfgSuccessors,
  buildTargetIndexes,
  loopCounterRegister,
} from "../../src/core/passes/cfg.ts";
import type { IrLoopCounter, IrOp, RegisterName } from "../../src/core/types.ts";

const REGISTER_INDEX: Record<RegisterName, number> = {
  "0": 0,
  "1": 1,
  "2": 2,
  "3": 3,
  "4": 4,
  "5": 5,
  "6": 6,
  "7": 7,
  "8": 8,
  "9": 9,
  a: 10,
  b: 11,
  c: 12,
  d: 13,
  e: 14,
};

function plain(opcode: number, mnemonic: string): IrOp {
  return { kind: "plain", opcode, meta: { mnemonic } };
}

function store(register: RegisterName): IrOp {
  return {
    kind: "store",
    register,
    opcode: 0x40 + REGISTER_INDEX[register],
    meta: { mnemonic: `X->П ${register}` },
  };
}

function label(name: string): IrOp {
  return { kind: "label", name };
}

function jump(target: string | number): IrOp {
  return { kind: "jump", target, opcode: 0x51, meta: { mnemonic: "БП" }, targetMeta: {} };
}

function cjump(target: string | number): IrOp {
  return {
    kind: "cjump",
    condition: "==0",
    target,
    opcode: 0x5e,
    meta: { mnemonic: "F x=0" },
    targetMeta: {},
  };
}

function call(target: string | number): IrOp {
  return { kind: "call", target, opcode: 0x53, meta: { mnemonic: "ПП" }, targetMeta: {} };
}

function loop(target: string): IrOp {
  return {
    kind: "loop",
    counter: "L0",
    target,
    opcode: 0x5d,
    meta: { mnemonic: "F L0" },
    targetMeta: {},
  };
}

function halt(): IrOp {
  return { kind: "stop", opcode: 0x50, semantic: "halt", meta: { mnemonic: "С/П" } };
}

function ret(): IrOp {
  return { kind: "return", opcode: 0x52, meta: { mnemonic: "В/О" } };
}

function indirectJump(register: RegisterName, target?: number): IrOp {
  return {
    kind: "indirect-jump",
    register,
    opcode: 0x80 + REGISTER_INDEX[register],
    meta: target === undefined
      ? { mnemonic: `К БП ${register}` }
      : { mnemonic: `К БП ${register}`, comment: `indirect-target=${target}` },
  };
}

function indirectCall(register: RegisterName, target?: number): IrOp {
  return {
    kind: "indirect-call",
    register,
    opcode: 0xa0 + REGISTER_INDEX[register],
    meta: target === undefined
      ? { mnemonic: `К ПП ${register}` }
      : { mnemonic: `К ПП ${register}`, comment: `indirect-target=${target}` },
  };
}

describe("cfg target indexes", () => {
  it("maps labels to op indexes and addresses to executable ops", () => {
    const ops: IrOp[] = [
      plain(0x01, "1"), // address 0
      label("head"), // not addressable
      jump("head"), // address 1, two cells
      plain(0x02, "2"), // address 3
    ];
    const { labelIndex, addressIndex } = buildTargetIndexes(ops);
    expect(labelIndex.get("head")).toBe(1);
    expect(addressIndex.get(0)).toBe(0);
    expect(addressIndex.get(1)).toBe(2);
    expect(addressIndex.get(3)).toBe(3);
    expect(addressIndex.has(2)).toBe(false);
  });
});

describe("cfg successor edges", () => {
  it("links straight-line ops, labels, and stops by fallthrough", () => {
    const ops: IrOp[] = [plain(0x01, "1"), label("x"), store("4"), halt(), plain(0x02, "2")];
    const edges = buildCfgEdges(ops);
    expect(edges[0]).toEqual([{ target: 1, kind: "fallthrough" }]);
    expect(edges[1]).toEqual([{ target: 2, kind: "fallthrough" }]);
    expect(edges[2]).toEqual([{ target: 3, kind: "fallthrough" }]);
    // С/П pauses; pressing С/П resumes from the next cell.
    expect(edges[3]).toEqual([{ target: 4, kind: "fallthrough" }]);
    expect(edges[4]).toEqual([]);
  });

  it("gives jumps a single jump edge and conditionals jump plus fallthrough", () => {
    const ops: IrOp[] = [cjump("done"), plain(0x01, "1"), jump("done"), label("done"), halt()];
    const edges = buildCfgEdges(ops);
    expect(edges[0]).toEqual([
      { target: 3, kind: "jump" },
      { target: 1, kind: "fallthrough" },
    ]);
    expect(edges[2]).toEqual([{ target: 3, kind: "jump" }]);
  });

  it("treats counted loops like conditionals", () => {
    const ops: IrOp[] = [label("head"), plain(0x01, "1"), loop("head"), halt()];
    const edges = buildCfgEdges(ops);
    expect(edges[2]).toEqual([
      { target: 0, kind: "jump" },
      { target: 3, kind: "fallthrough" },
    ]);
  });

  it("resolves numeric targets through machine addresses", () => {
    const ops: IrOp[] = [
      jump(3), // address 0, two cells
      plain(0x01, "1"), // address 2
      plain(0x02, "2"), // address 3
    ];
    const edges = buildCfgEdges(ops);
    expect(edges[0]).toEqual([{ target: 2, kind: "jump" }]);
  });

  it("wires returns to every call continuation instead of call fallthrough", () => {
    const ops: IrOp[] = [
      call("helper"), // continuation index 1
      halt(),
      label("helper"),
      plain(0x01, "1"),
      ret(),
    ];
    const edges = buildCfgEdges(ops);
    expect(edges[0]).toEqual([{ target: 2, kind: "jump" }]);
    expect(edges[4]).toEqual([{ target: 1, kind: "normal" }]);
  });

  it("uses proved indirect targets and leaves unknown indirect flow without edges", () => {
    const ops: IrOp[] = [
      indirectJump("7", 3), // address 0
      indirectJump("8"), // address 1, unknown target
      plain(0x01, "1"), // address 2
      plain(0x02, "2"), // address 3
    ];
    const edges = buildCfgEdges(ops);
    expect(edges[0]).toEqual([{ target: 3, kind: "jump" }]);
    expect(edges[1]).toEqual([]);
  });

  it("adds indirect-call fallthrough only when requested", () => {
    const ops: IrOp[] = [
      indirectCall("7", 2), // address 0
      halt(), // address 1
      plain(0x01, "1"), // address 2
      ret(),
    ];
    const withoutFallthrough = buildCfgEdges(ops);
    expect(withoutFallthrough[0]).toEqual([{ target: 2, kind: "jump" }]);
    // Return still reaches the call continuation.
    expect(withoutFallthrough[3]).toEqual([{ target: 1, kind: "normal" }]);

    const withFallthrough = buildCfgEdges(ops, { indirectCallFallthrough: true });
    expect(withFallthrough[0]).toEqual([
      { target: 2, kind: "jump" },
      { target: 1, kind: "fallthrough" },
    ]);
  });

  it("projects plain successor indexes for consumers that ignore edge kinds", () => {
    const ops: IrOp[] = [cjump("done"), plain(0x01, "1"), label("done"), halt()];
    expect(buildCfgSuccessors(ops)).toEqual([[2, 1], [2], [3], []]);
  });
});

describe("loopCounterRegister", () => {
  it("maps F L0..F L3 to memory registers 0..3", () => {
    const expected: Record<IrLoopCounter, RegisterName> = {
      L0: "0",
      L1: "1",
      L2: "2",
      L3: "3",
    };
    for (const [counter, register] of Object.entries(expected)) {
      expect(loopCounterRegister(counter as IrLoopCounter)).toBe(register);
    }
  });
});
