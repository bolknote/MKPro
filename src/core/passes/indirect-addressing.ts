import {
  evaluateIndirectAddress,
  isStableIndirectSelector,
} from "../indirect-addressing.ts";
import { registerIndex } from "../opcodes.ts";
import type { IrCondition, IrMeta, IrOp, RegisterName } from "../types.ts";
import { hasRewriteBarrier, isDisplayFocusSensitive, type IrPass, type IrPassFn } from "./helpers.ts";

const INDIRECT_COND_BASES: Record<IrCondition, number> = {
  "!=0": 0x70,
  ">=0": 0x90,
  "<0": 0xc0,
  "==0": 0xe0,
};

interface KnownState {
  currentLiteral: string | undefined;
  stableRegisters: Map<RegisterName, string>;
}

function clearKnownState(state: KnownState): void {
  state.currentLiteral = undefined;
  state.stableRegisters.clear();
}

function cloneMeta(meta: IrMeta, comment: string): IrMeta {
  return {
    ...meta,
    comment: [meta.comment, comment].filter(Boolean).join("; "),
  };
}

function digitForPlain(op: IrOp): string | undefined {
  if (op.kind !== "plain" || op.opcode < 0x00 || op.opcode > 0x09) return undefined;
  return String(op.opcode);
}

function literalNumber(text: string | undefined): number | undefined {
  if (text === undefined || !/^\d+$/u.test(text)) return undefined;
  const value = Number(text);
  if (!Number.isSafeInteger(value)) return undefined;
  return value;
}

function rememberStore(state: KnownState, register: RegisterName): void {
  if (!isStableIndirectSelector(register)) return;
  const value = literalNumber(state.currentLiteral);
  if (value === undefined) {
    state.stableRegisters.delete(register);
  } else {
    state.stableRegisters.set(register, String(value));
  }
}

function findFlowSelector(
  state: KnownState,
  target: string | number,
): RegisterName | undefined {
  if (typeof target !== "number") return undefined;
  for (const [register, value] of state.stableRegisters) {
    const evaluated = evaluateIndirectAddress(register, value, "flow");
    if (evaluated?.flowTarget === target) return register;
  }
  return undefined;
}

function findMemorySelector(
  state: KnownState,
  target: RegisterName,
): RegisterName | undefined {
  for (const [register, value] of state.stableRegisters) {
    const evaluated = evaluateIndirectAddress(register, value, "memory");
    if (evaluated?.memoryTarget === target) return register;
  }
  return undefined;
}

function updateKnownAfterOp(state: KnownState, op: IrOp): void {
  if (hasRewriteBarrier(op)) {
    clearKnownState(state);
    return;
  }

  const digit = digitForPlain(op);
  if (digit !== undefined) {
    state.currentLiteral = `${state.currentLiteral ?? ""}${digit}`;
    return;
  }

  switch (op.kind) {
    case "store":
      rememberStore(state, op.register);
      return;
    case "recall": {
      state.currentLiteral = state.stableRegisters.get(op.register);
      return;
    }
    case "label":
      clearKnownState(state);
      return;
    case "indirect-store":
      clearKnownState(state);
      return;
    case "call":
    case "indirect-call":
    case "cjump":
    case "indirect-cjump":
    case "stop":
    case "jump":
    case "indirect-jump":
    case "return":
      clearKnownState(state);
      return;
    default:
      state.currentLiteral = undefined;
      return;
  }
}

function indirectFlowOp(
  op: Extract<IrOp, { kind: "jump" | "call" | "cjump" }>,
  register: RegisterName,
  target: number,
): IrOp {
  const offset = registerIndex(register);
  const suffix = `stable indirect flow indirect-target=${target}`;
  if (op.kind === "jump") {
    return {
      kind: "indirect-jump",
      register,
      opcode: 0x80 + offset,
      meta: cloneMeta({ ...op.meta, mnemonic: `К БП ${register}` }, suffix),
    };
  }
  if (op.kind === "call") {
    return {
      kind: "indirect-call",
      register,
      opcode: 0xa0 + offset,
      meta: cloneMeta({ ...op.meta, mnemonic: `К ПП ${register}` }, suffix),
    };
  }
  const opcode = INDIRECT_COND_BASES[op.condition] + offset;
  const name = op.condition === "==0"
    ? "x=0"
    : op.condition === "!=0"
      ? "x!=0"
      : `x${op.condition}`;
  return {
    kind: "indirect-cjump",
    condition: op.condition,
    register,
    opcode,
    meta: cloneMeta({ ...op.meta, mnemonic: `К ${name} ${register}` }, suffix),
  };
}

const stableFlowRun: IrPassFn = (ops) => {
  const result: IrOp[] = [];
  const state: KnownState = { currentLiteral: undefined, stableRegisters: new Map() };
  let applied = 0;

  for (const op of ops) {
    if (!hasRewriteBarrier(op) && (op.kind === "jump" || op.kind === "call" || op.kind === "cjump")) {
      const register = findFlowSelector(state, op.target);
      if (register !== undefined && typeof op.target === "number") {
        const rewritten = indirectFlowOp(op, register, op.target);
        result.push(rewritten);
        updateKnownAfterOp(state, rewritten);
        applied += 1;
        continue;
      }
    }
    result.push(op);
    updateKnownAfterOp(state, op);
  }

  if (applied === 0) return { ops: [...ops], applied: 0, optimizations: [] };
  return {
    ops: result,
    applied,
    optimizations: [
      {
        name: "stable-indirect-flow",
        detail: `Replaced ${applied} direct branch/call(s) with stable-register indirect flow.`,
      },
    ],
  };
};

const memoryTableRun: IrPassFn = (ops) => {
  const result: IrOp[] = [];
  const state: KnownState = { currentLiteral: undefined, stableRegisters: new Map() };
  let applied = 0;

  for (const op of ops) {
    if ((op.kind === "recall" || op.kind === "store") && !hasRewriteBarrier(op) && !isDisplayFocusSensitive(op)) {
      const selector = findMemorySelector(state, op.register);
      if (selector !== undefined) {
        const opcodeBase = op.kind === "recall" ? 0xd0 : 0xb0;
        const mnemonic = op.kind === "recall" ? `К П->X ${selector}` : `К X->П ${selector}`;
        const rewritten: IrOp = {
          kind: op.kind === "recall" ? "indirect-recall" : "indirect-store",
          register: selector,
          opcode: opcodeBase + registerIndex(selector),
          meta: cloneMeta({ ...op.meta, mnemonic }, `indirect memory table indirect-memory-target=${op.register}`),
        };
        result.push(rewritten);
        updateKnownAfterOp(state, rewritten);
        applied += 1;
        continue;
      }
    }
    result.push(op);
    updateKnownAfterOp(state, op);
  }

  if (applied === 0) return { ops: [...ops], applied: 0, optimizations: [] };
  return {
    ops: result,
    applied,
    optimizations: [
      {
        name: "indirect-memory-table",
        detail: `Rewrote ${applied} direct memory access(es) through an existing stable selector.`,
      },
    ],
  };
};

export const stableIndirectFlow: IrPass = {
  name: "stable-indirect-flow",
  run: stableFlowRun,
  layoutSafe: false,
};

export const indirectMemoryTable: IrPass = {
  name: "indirect-memory-table",
  run: memoryTableRun,
  layoutSafe: false,
};
