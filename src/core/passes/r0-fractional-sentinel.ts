import type { IrCondition, IrMeta, IrOp } from "../types.ts";
import { hasRewriteBarrier, type IrPass, type IrPassFn } from "./helpers.ts";
import { computeLiveness } from "./liveness-analysis.ts";

type R0Fact = "unknown" | "positive-fractional" | "sentinel";
type XFact = "unknown" | "sentinel";

const INDIRECT_COND_BASES: Record<IrCondition, number> = {
  "!=0": 0x70,
  ">=0": 0x90,
  "<0": 0xc0,
  "==0": 0xe0,
};

function isFractionalR0LiteralBeforeStore(ops: readonly IrOp[], storeIndex: number): boolean {
  let index = storeIndex - 1;
  let hasNonZeroFractionDigit = false;
  while (index >= 0) {
    const digit = ops[index];
    if (digit?.kind !== "plain" || digit.opcode < 0x00 || digit.opcode > 0x09) break;
    if (digit.opcode > 0) hasNonZeroFractionDigit = true;
    index -= 1;
  }
  const dot = ops[index];
  const zero = ops[index - 1];
  if (!hasNonZeroFractionDigit || dot?.kind !== "plain" || dot.opcode !== 0x0a) return false;
  if (zero === undefined) return true;
  return zero.kind === "plain" && zero.opcode === 0x00;
}

function isSentinelPreloadRecall(op: IrOp | undefined): boolean {
  return op?.kind === "recall" && /\bpreload const -99999999\b/u.test(op.meta.comment ?? "");
}

function isSentinelDirectLiteralAt(ops: readonly IrOp[], signIndex: number): boolean {
  const sign = ops[signIndex];
  if (sign?.kind !== "plain" || sign.opcode !== 0x0b) return false;

  const firstDigitIndex = signIndex - 8;
  if (firstDigitIndex < 0) return false;
  for (let index = firstDigitIndex; index < signIndex; index += 1) {
    const digit = ops[index];
    if (digit?.kind !== "plain" || digit.opcode !== 0x09) return false;
  }

  const before = ops[firstDigitIndex - 1];
  if (
    before?.kind === "plain" &&
    ((before.opcode >= 0x00 && before.opcode <= 0x09) || before.opcode === 0x0a || before.opcode === 0x0c)
  ) {
    return false;
  }
  return true;
}

function isSentinelDirectLiteralBeforeStore(ops: readonly IrOp[], storeIndex: number): boolean {
  return isSentinelDirectLiteralAt(ops, storeIndex - 1);
}

function isSentinelMaterializedBeforeStore(ops: readonly IrOp[], storeIndex: number): boolean {
  return isSentinelPreloadRecall(ops[storeIndex - 1]) || isSentinelDirectLiteralBeforeStore(ops, storeIndex);
}

function preservesR0Fact(op: IrOp): boolean {
  return op.kind === "plain" || op.kind === "recall" || (op.kind === "store" && op.register !== "0");
}

function cloneMeta(meta: IrMeta, comment: string): IrMeta {
  return {
    ...meta,
    comment: [meta.comment, comment].filter(Boolean).join("; "),
  };
}

function fractionalR0Jump(op: Extract<IrOp, { kind: "jump" }>): IrOp {
  return {
    kind: "indirect-jump",
    register: "0",
    opcode: 0x80,
    meta: cloneMeta({ ...op.meta, mnemonic: "К БП 0" }, "fractional R0 jump to 99"),
  };
}

function fractionalR0Call(op: Extract<IrOp, { kind: "call" }>): IrOp {
  return {
    kind: "indirect-call",
    register: "0",
    opcode: 0xa0,
    meta: cloneMeta({ ...op.meta, mnemonic: "К ПП 0" }, "fractional R0 call to 99"),
  };
}

function fractionalR0CondJump(op: Extract<IrOp, { kind: "cjump" }>): IrOp {
  const name = op.condition === "==0"
    ? "x=0"
    : op.condition === "!=0"
      ? "x!=0"
      : `x${op.condition}`;
  return {
    kind: "indirect-cjump",
    condition: op.condition,
    register: "0",
    opcode: INDIRECT_COND_BASES[op.condition],
    meta: cloneMeta({ ...op.meta, mnemonic: `К ${name} 0` }, "fractional R0 conditional jump to 99"),
  };
}

function optimizationDetail(
  directR3Accesses: number,
  sentinelStores: number,
  sentinelRecalls: number,
  fractionalJumps: number,
): string {
  const parts: string[] = [];
  if (directR3Accesses > 0) parts.push(`${directR3Accesses} redundant direct R3 access(es)`);
  if (sentinelStores > 0) parts.push(`${sentinelStores} redundant sentinel R0 store(s)`);
  if (sentinelRecalls > 0) parts.push(`${sentinelRecalls} redundant sentinel R0 recall(s)`);
  if (fractionalJumps > 0) parts.push(`${fractionalJumps} direct flow op(s) to 99 via fractional R0`);
  return `Reused fractional-R0 side effects for ${parts.join(" and ")}.`;
}

const run: IrPassFn = (ops) => {
  if (ops.length === 0) return { ops: [], applied: 0, optimizations: [] };
  const liveness = computeLiveness(ops);
  const remove = new Set<number>();
  const replace = new Map<number, IrOp>();
  let r0Fact: R0Fact = "unknown";
  let xFact: XFact = "unknown";
  let directR3Accesses = 0;
  let sentinelStores = 0;
  let sentinelRecalls = 0;
  let fractionalJumps = 0;

  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (hasRewriteBarrier(op)) {
      r0Fact = "unknown";
      xFact = "unknown";
      continue;
    }
    if (op.kind === "store" && op.register === "0") {
      const storesSentinel: boolean = xFact === "sentinel" || isSentinelMaterializedBeforeStore(ops, i);
      if (r0Fact === "sentinel" && storesSentinel) {
        remove.add(i);
        sentinelStores += 1;
        xFact = "sentinel";
        continue;
      }
      r0Fact = isFractionalR0LiteralBeforeStore(ops, i)
        ? "positive-fractional"
        : storesSentinel
          ? "sentinel"
          : "unknown";
      xFact = storesSentinel ? "sentinel" : "unknown";
      continue;
    }
    if (op.kind === "label") {
      if (r0Fact === "sentinel") r0Fact = "unknown";
      xFact = "unknown";
      continue;
    }
    if (op.kind === "recall") {
      if (op.register === "0" && r0Fact === "sentinel") {
        if (xFact === "sentinel") {
          remove.add(i);
          sentinelRecalls += 1;
          continue;
        }
        xFact = "sentinel";
        continue;
      }
      xFact = isSentinelPreloadRecall(op) ? "sentinel" : "unknown";
      continue;
    }
    if (op.kind === "plain") {
      xFact = isSentinelDirectLiteralAt(ops, i) ? "sentinel" : "unknown";
      continue;
    }
    if (preservesR0Fact(op)) {
      continue;
    }
    if (r0Fact !== "positive-fractional") {
      r0Fact = "unknown";
      xFact = "unknown";
      continue;
    }

    if (op.kind === "indirect-recall" && op.register === "0") {
      const next = ops[i + 1];
      if (next?.kind === "recall" && next.register === "3" && !liveness.liveOut[i]!.has("0")) {
        remove.add(i + 1);
        directR3Accesses += 1;
      }
      r0Fact = "sentinel";
      xFact = "unknown";
      continue;
    }

    if (op.kind === "indirect-store" && op.register === "0") {
      const next = ops[i + 1];
      if (next?.kind === "store" && next.register === "3" && !liveness.liveOut[i]!.has("0")) {
        remove.add(i + 1);
        directR3Accesses += 1;
      }
      r0Fact = "sentinel";
      continue;
    }

    if (
      (op.kind === "jump" || op.kind === "call" || op.kind === "cjump") &&
      op.target === 99 &&
      !liveness.liveOut[i]!.has("0")
    ) {
      replace.set(
        i,
        op.kind === "jump" ? fractionalR0Jump(op) : op.kind === "call" ? fractionalR0Call(op) : fractionalR0CondJump(op),
      );
      fractionalJumps += 1;
      r0Fact = "unknown";
      xFact = "unknown";
      continue;
    }

    r0Fact = "unknown";
    xFact = "unknown";
  }

  if (remove.size === 0 && replace.size === 0) return { ops: [...ops], applied: 0, optimizations: [] };
  return {
    ops: ops.flatMap((op, index) => remove.has(index) ? [] : [replace.get(index) ?? op]),
    applied: remove.size + replace.size,
    optimizations: [
      {
        name: "r0-fractional-sentinel",
        detail: optimizationDetail(directR3Accesses, sentinelStores, sentinelRecalls, fractionalJumps),
      },
    ],
  };
};

export const r0FractionalSentinel: IrPass = {
  name: "r0-fractional-sentinel",
  run,
  layoutSafe: false,
};
