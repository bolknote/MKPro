import type { IrMeta, IrOp } from "../types.ts";
import { cellsPerOp, emptyResult, hasRewriteBarrier, type IrPass, type IrPassFn } from "./helpers.ts";

interface TailOccurrence {
  key: string;
  start: number;
  end: number;
  cells: number;
}

interface TailCandidate {
  key: string;
  occurrences: TailOccurrence[];
  cells: number;
}

interface SelectedTail {
  label: string;
  target: TailOccurrence;
  replacements: TailOccurrence[];
}

const run: IrPassFn = (ops) => {
  if (hasNumericFlowTarget(ops)) return emptyResult(ops);

  const candidates = collectTailCandidates(ops);
  const selected = selectTails(candidates, ops);
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

function collectTailCandidates(ops: readonly IrOp[]): TailCandidate[] {
  const byKey = new Map<string, TailOccurrence[]>();

  for (let end = 0; end < ops.length; end += 1) {
    const final = ops[end]!;
    if (!isShareableTerminal(final)) continue;

    const parts = [opKey(final)];
    let cells = cellsPerOp(final);
    for (let start = end - 1; start >= 0; start -= 1) {
      const op = ops[start]!;
      if (!isShareableBodyOp(op)) break;

      parts.unshift(opKey(op));
      cells += cellsPerOp(op);
      if (cells <= 2) continue;

      const key = parts.join("\n");
      const occurrences = byKey.get(key) ?? [];
      occurrences.push({ key, start, end, cells });
      byKey.set(key, occurrences);
    }
  }

  return [...byKey.entries()]
    .map(([key, occurrences]) => ({ key, occurrences, cells: occurrences[0]!.cells }));
}

function selectTails(candidates: readonly TailCandidate[], ops: readonly IrOp[]): SelectedTail[] {
  const existingLabels = new Set(ops.flatMap((op) => op.kind === "label" ? [op.name] : []));
  const protectedIndexes = new Set<number>();
  const selected: SelectedTail[] = [];
  let labelIndex = 0;

  const ordered = [...candidates].sort((left, right) => {
    const leftSavings = (left.occurrences.length - 1) * (left.cells - 2);
    const rightSavings = (right.occurrences.length - 1) * (right.cells - 2);
    return rightSavings - leftSavings || right.cells - left.cells || left.key.localeCompare(right.key);
  });

  for (const candidate of ordered) {
    const available = candidate.occurrences.filter((occurrence) =>
      !rangeIntersects(protectedIndexes, occurrence.start, occurrence.end)
    );
    if (available.length < 2) continue;

    const [target, ...replacements] = available;
    if (target === undefined || replacements.length === 0) continue;

    const savedCells = replacements.reduce((sum, occurrence) => sum + occurrence.cells - 2, 0);
    if (savedCells <= 0) continue;

    const label = freshLabel(existingLabels, labelIndex);
    labelIndex += 1;
    existingLabels.add(label);
    selected.push({ label, target, replacements });

    for (const occurrence of [target, ...replacements]) {
      markRange(protectedIndexes, occurrence.start, occurrence.end);
    }
  }

  return selected;
}

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
    case "plain":
      return true;
    case "label":
    case "jump":
    case "cjump":
    case "loop":
    case "indirect-jump":
    case "indirect-cjump":
    case "return":
    case "stop":
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
    case "stop":
      return `stop:${op.semantic}`;
    case "orphan-address":
      return `address:${targetKey(op.target)}`;
  }
}

function targetKey(target: string | number): string {
  return typeof target === "number" ? `#${target}` : target;
}

function hasNumericFlowTarget(ops: readonly IrOp[]): boolean {
  return ops.some((op) => {
    switch (op.kind) {
      case "jump":
      case "cjump":
      case "call":
        return typeof op.target === "number";
      case "loop":
        return typeof op.target === "number";
      default:
        return false;
    }
  });
}

function rangeIntersects(indexes: ReadonlySet<number>, start: number, end: number): boolean {
  for (let index = start; index <= end; index += 1) {
    if (indexes.has(index)) return true;
  }
  return false;
}

function markRange(indexes: Set<number>, start: number, end: number): void {
  for (let index = start; index <= end; index += 1) indexes.add(index);
}

function freshLabel(existing: Set<string>, startIndex: number): string {
  let index = startIndex;
  while (true) {
    const label = `__shared_terminal_tail_${index}`;
    if (!existing.has(label)) return label;
    index += 1;
  }
}
