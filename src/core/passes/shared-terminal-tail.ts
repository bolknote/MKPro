import type { IrMeta, IrOp } from "../types.ts";
import { emptyResult, hasRewriteBarrier, type IrPass, type IrPassFn } from "./helpers.ts";
import {
  collectSuffixCandidates,
  createLabelAllocator,
  hasNumericOutlineFlowTarget,
  selectSharedSuffixes,
  targetKey,
} from "./outline.ts";

const run: IrPassFn = (ops) => {
  if (hasNumericOutlineFlowTarget(ops)) return emptyResult(ops);

  const candidates = collectSuffixCandidates(ops, {
    isTerminal: isShareableTerminal,
    isBodyOp: isShareableBodyOp,
    opKey,
  });
  const selected = selectSharedSuffixes(
    candidates,
    createLabelAllocator(ops, "__shared_terminal_tail_"),
    new Set<number>(),
  );
  if (selected.length === 0) return emptyResult(ops);

  const targetLabels = new Map<number, string[]>();
  const replacementByStart = new Map<number, { end: number; label: string }>();
  let applied = 0;
  let savedCells = 0;

  for (const tail of selected) {
    const labels = targetLabels.get(tail.target.start) ?? [];
    labels.push(tail.label);
    targetLabels.set(tail.target.start, labels);
    for (const replacement of tail.replacements) {
      replacementByStart.set(replacement.start, { end: replacement.end, label: tail.label });
      applied += 1;
      savedCells += replacement.cells - 2;
    }
  }

  const result: IrOp[] = [];
  for (let index = 0; index < ops.length; index += 1) {
    for (const label of targetLabels.get(index) ?? []) {
      result.push({ kind: "label", name: label });
    }

    const replacement = replacementByStart.get(index);
    if (replacement !== undefined) {
      result.push(sharedTailJump(replacement.label, ops[index]!));
      index = replacement.end;
      continue;
    }

    result.push(ops[index]!);
  }

  return {
    ops: result,
    applied,
    optimizations: [{
      name: "shared-terminal-tail",
      detail: `Shared ${applied} terminal straight-line tail${applied === 1 ? "" : "s"} (${savedCells} cell${savedCells === 1 ? "" : "s"} saved).`,
    }],
  };
};

export const sharedTerminalTail: IrPass = {
  name: "shared-terminal-tail",
  run,
  layoutSafe: false,
};

function sharedTailJump(label: string, source: IrOp): IrOp {
  const meta: IrMeta = {
    mnemonic: "БП",
    comment: "shared terminal tail",
  };
  if ("meta" in source && source.meta.sourceLine !== undefined) {
    meta.sourceLine = source.meta.sourceLine;
  }
  return {
    kind: "jump",
    target: label,
    opcode: 0x51,
    meta,
    targetMeta: { comment: "shared terminal tail" },
  };
}

function isShareableTerminal(op: IrOp): boolean {
  if (hasRewriteBarrier(op)) return false;
  return op.kind === "jump" || op.kind === "indirect-jump" || op.kind === "return";
}

function isShareableBodyOp(op: IrOp): boolean {
  if (hasRewriteBarrier(op)) return false;
  switch (op.kind) {
    case "store":
    case "recall":
    case "indirect-store":
    case "indirect-recall":
    case "call":
    case "indirect-call":
    case "stop":
    case "plain":
      return true;
    case "label":
    case "jump":
    case "cjump":
    case "loop":
    case "indirect-jump":
    case "indirect-cjump":
    case "return":
    case "orphan-address":
      return false;
  }
}

function opKey(op: IrOp): string {
  switch (op.kind) {
    case "store":
    case "recall":
    case "indirect-store":
    case "indirect-recall":
    case "indirect-jump":
    case "indirect-call":
      return `${op.kind}:${op.register}`;
    case "stop":
      return `stop:${op.semantic}`;
    case "jump":
    case "call":
      return `${op.kind}:${targetKey(op.target)}`;
    case "return":
      return "return";
    case "plain":
      return `plain:${op.opcode}`;
    case "label":
      return `label:${op.name}`;
    case "cjump":
      return `cjump:${op.condition}:${targetKey(op.target)}`;
    case "loop":
      return `loop:${op.counter}:${targetKey(op.target)}`;
    case "indirect-cjump":
      return `indirect-cjump:${op.condition}:${op.register}`;
    case "orphan-address":
      return `address:${targetKey(op.target)}`;
  }
}
