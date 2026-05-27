import type { IrOp } from "../types.ts";
import { hasUnsafe, type IrPass, type IrPassFn } from "./helpers.ts";

function isShowDisplayOp(op: IrOp): boolean {
  if (op.kind === "recall") return true;
  if (op.kind === "plain") {
    if (op.opcode === 0x10 || op.opcode === 0x12) return true;
    if (op.opcode <= 0x0a) return true;
    if (op.opcode === 0x0e) return true;
  }
  return false;
}

function isShowStop(op: IrOp): boolean {
  return op.kind === "stop" && (op.semantic === "show" || op.semantic === "halt");
}

interface PrologueSegment {
  readonly ops: readonly IrOp[];
}

function collectForwardPrologue(ops: readonly IrOp[], from: number): PrologueSegment {
  const collected: IrOp[] = [];
  let i = from;
  while (i < ops.length && ops[i]!.kind === "label") i += 1;
  while (i < ops.length) {
    const op = ops[i]!;
    if (op.kind === "label") {
      i += 1;
      continue;
    }
    if (isShowDisplayOp(op) && !hasUnsafe(op)) {
      collected.push(op);
      i += 1;
      continue;
    }
    if (isShowStop(op) && !hasUnsafe(op)) {
      collected.push(op);
      return { ops: collected };
    }
    return { ops: [] };
  }
  return { ops: [] };
}

interface BackwardSegment {
  readonly ops: readonly IrOp[];
  readonly startIndex: number; // index of the first op of the backward prologue
  readonly virtualHeadRegister?: import("../types.ts").RegisterName;
}

function collectBackwardPrologue(ops: readonly IrOp[], beforeIndex: number): BackwardSegment {
  const collected: IrOp[] = [];
  let i = beforeIndex - 1;
  let sawStop = false;
  while (i >= 0) {
    const op = ops[i]!;
    if (op.kind === "label") {
      i -= 1;
      continue;
    }
    if (!sawStop) {
      if (isShowStop(op) && !hasUnsafe(op)) {
        collected.push(op);
        sawStop = true;
        i -= 1;
        continue;
      }
      return { ops: [], startIndex: -1 };
    }
    if (isShowDisplayOp(op) && !hasUnsafe(op)) {
      collected.push(op);
      i -= 1;
      continue;
    }
    break;
  }
  if (!sawStop) return { ops: [], startIndex: -1 };
  // Walk past leading labels to find the true first op of the backward prologue.
  let startIndex = i + 1;
  while (startIndex < beforeIndex && ops[startIndex]!.kind === "label") startIndex += 1;
  // If the op immediately preceding the backward prologue is X->П r, then X
  // already holds r's value at the start of the prologue, equivalent to
  // virtually prepending a П->X r.
  let virtualHeadRegister: import("../types.ts").RegisterName | undefined;
  let scan = i;
  while (scan >= 0 && ops[scan]!.kind === "label") scan -= 1;
  if (scan >= 0) {
    const prior = ops[scan]!;
    if (prior.kind === "store" && !hasUnsafe(prior)) {
      virtualHeadRegister = prior.register;
    }
  }
  return {
    ops: collected.slice().reverse(),
    startIndex,
    virtualHeadRegister,
  };
}

function opsEquivalent(a: IrOp, b: IrOp): boolean {
  if (a.kind !== b.kind) return false;
  if (a.kind === "recall" && b.kind === "recall") return a.register === b.register;
  if (a.kind === "plain" && b.kind === "plain") return a.opcode === b.opcode;
  if (a.kind === "stop" && b.kind === "stop") return a.semantic === b.semantic;
  return false;
}

function segmentsMatch(a: readonly IrOp[], b: readonly IrOp[]): boolean {
  if (a.length === 0 || a.length !== b.length) return false;
  for (let i = 0; i < a.length; i += 1) {
    if (!opsEquivalent(a[i]!, b[i]!)) return false;
  }
  return true;
}

const run: IrPassFn = (ops) => {
  const labelIndex = new Map<string, number>();
  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind === "label") labelIndex.set(op.name, i);
  }

  const removeRanges: Array<{ start: number; end: number }> = [];
  let applied = 0;
  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (op.kind !== "jump") continue;
    if (typeof op.target !== "string") continue;
    if (hasUnsafe(op)) continue;
    const labelAt = labelIndex.get(op.target);
    if (labelAt === undefined) continue;
    const headForward = collectForwardPrologue(ops, labelAt);
    if (headForward.ops.length === 0) continue;
    const headBackward = collectBackwardPrologue(ops, i);
    if (headBackward.ops.length === 0) continue;
    let backwardOps = headBackward.ops;
    let matched = segmentsMatch(backwardOps, headForward.ops);
    if (!matched && headBackward.virtualHeadRegister !== undefined) {
      const firstForward = headForward.ops[0];
      if (
        firstForward !== undefined &&
        firstForward.kind === "recall" &&
        firstForward.register === headBackward.virtualHeadRegister
      ) {
        // The forward prologue starts with a П->X r whose value X already
        // holds (because the prior op stored r). Match against the suffix.
        const suffix = headForward.ops.slice(1);
        if (segmentsMatch(backwardOps, suffix)) {
          matched = true;
        }
      }
    }
    if (!matched) continue;
    const start = headBackward.startIndex;
    const end = i;
    // Refuse to fire when the backward prologue would overlap with — or even
    // touch — the forward prologue at the loop head. Otherwise we'd be
    // erasing the only halt and producing an unconditional infinite loop.
    const forwardEnd = labelAt + headForward.ops.length;
    if (start <= forwardEnd) continue;
    let hasIntermediateContent = false;
    for (let k = forwardEnd + 1; k < start; k += 1) {
      const intermediate = ops[k]!;
      if (intermediate.kind === "label") continue;
      hasIntermediateContent = true;
      break;
    }
    if (!hasIntermediateContent) continue;
    const overlaps = removeRanges.some(
      (range) => !(end <= range.start || start >= range.end),
    );
    if (overlaps) continue;
    removeRanges.push({ start, end });
    applied += 1;
  }

  if (applied === 0) {
    return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
  }
  const shouldRemove = new Array<boolean>(ops.length).fill(false);
  for (const range of removeRanges) {
    for (let i = range.start; i < range.end; i += 1) shouldRemove[i] = true;
  }
  const result: IrOp[] = [];
  for (let i = 0; i < ops.length; i += 1) {
    if (!shouldRemove[i]) result.push(ops[i]!);
  }
  const totalCells = removeRanges.reduce((acc, range) => acc + (range.end - range.start), 0);
  return {
    ops: result,
    applied,
    optimizations: [
      {
        name: "redundant-prologue-elimination",
        detail: `Removed ${applied} display/halt prologue(s) immediately before a jump to their identical loop head (${totalCells} cells).`,
        unsafe: false,
      },
    ],
    unsafeUnverified: [],
  };
};

export const redundantPrologueElimination: IrPass = {
  name: "redundant-prologue-elimination",
  run,
  layoutSafe: false,
};
