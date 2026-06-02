import type { IrMeta, IrOp } from "../types.ts";
import { getOpcode } from "../opcodes.ts";
import {
  cellsPerOp,
  emptyResult,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  type IrPass,
  type IrPassFn,
} from "./helpers.ts";

interface Occurrence {
  key: string;
  start: number;
  end: number;
  cells: number;
}

interface Candidate {
  key: string;
  occurrences: Occurrence[];
  cells: number;
}

interface SelectedHelper {
  label: string;
  body: IrOp[];
  occurrences: Occurrence[];
  cells: number;
}

const MIN_BODY_CELLS = 4;
const GENERATED_BODY_LABEL_PREFIXES = [
  "__return_suffix_gadget_",
  "__shared_terminal_tail_",
  "__shared_straight_line_helper_",
];

const run: IrPassFn = (ops, context) => {
  if (hasNumericFlowTarget(ops)) return emptyResult(ops);

  const selected = selectHelpers(collectCandidates(ops, context.options.sharedStraightLineCallBodies === true), ops);
  if (selected.length === 0) return emptyResult(ops);

  const replacementByStart = new Map<number, { end: number; label: string }>();
  let applied = 0;
  let savedCells = 0;
  for (const helper of selected) {
    const helperCost = helper.cells + 1;
    const replacementSavings = helper.occurrences.reduce((sum, occurrence) => sum + occurrence.cells - 2, 0);
    savedCells += replacementSavings - helperCost;
    for (const occurrence of helper.occurrences) {
      replacementByStart.set(occurrence.start, { end: occurrence.end, label: helper.label });
      applied += 1;
    }
  }

  const result: IrOp[] = [];
  for (let index = 0; index < ops.length; index += 1) {
    const replacement = replacementByStart.get(index);
    if (replacement !== undefined) {
      result.push(helperCall(replacement.label, ops[index]!));
      index = replacement.end;
      continue;
    }
    result.push(ops[index]!);
  }

  for (const helper of selected) {
    result.push({ kind: "label", name: helper.label });
    result.push(...helper.body.map(markHelperBodyOp));
    result.push({ kind: "return", opcode: 0x52, meta: { mnemonic: "В/О", comment: "shared straight-line helper return" } });
  }

  return {
    ops: result,
    applied,
    optimizations: [{
      name: "shared-straight-line-helper",
      detail: `Extracted ${selected.length} straight-line helper${selected.length === 1 ? "" : "s"} from ${applied} repeated body occurrence${applied === 1 ? "" : "s"} (${savedCells} cell${savedCells === 1 ? "" : "s"} saved).`,
    }],
  };
};

export const sharedStraightLineHelper: IrPass = {
  name: "shared-straight-line-helper",
  run,
  layoutSafe: false,
};

function collectCandidates(ops: readonly IrOp[], allowDirectCalls: boolean): Candidate[] {
  const byKey = new Map<string, Occurrence[]>();
  const protectedIndexes = new Set([
    ...generatedBodyIndexes(ops, allowDirectCalls),
    ...displaySensitiveBlockIndexes(ops),
  ]);
  for (let start = 0; start < ops.length; start += 1) {
    if (protectedIndexes.has(start)) continue;
    const parts: string[] = [];
    let cells = 0;
    for (let end = start; end < ops.length; end += 1) {
      if (protectedIndexes.has(end)) break;
      const op = ops[end]!;
      if (!isShareableBodyOp(op, allowDirectCalls)) break;
      parts.push(opKey(op));
      cells += cellsPerOp(op);
      if (cells < MIN_BODY_CELLS) continue;
      const key = parts.join("\n");
      const occurrences = byKey.get(key) ?? [];
      occurrences.push({ key, start, end, cells });
      byKey.set(key, occurrences);
    }
  }

  return [...byKey.entries()]
    .map(([key, occurrences]) => ({ key, occurrences, cells: occurrences[0]!.cells }))
    .filter((candidate) => candidate.occurrences.length >= 2);
}

function generatedBodyIndexes(ops: readonly IrOp[], allowDirectCalls: boolean): Set<number> {
  const indexes = new Set<number>();
  let protect = false;
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") {
      protect = GENERATED_BODY_LABEL_PREFIXES.some((prefix) => op.name.startsWith(prefix));
      continue;
    }
    if (protect) indexes.add(index);
    if (!isShareableBodyOp(op, allowDirectCalls)) protect = false;
  }
  return indexes;
}

function displaySensitiveBlockIndexes(ops: readonly IrOp[]): Set<number> {
  const indexes = new Set<number>();
  let segment: number[] = [];
  let segmentIsSensitive = false;

  const flush = (): void => {
    if (segmentIsSensitive) {
      for (const index of segment) indexes.add(index);
    }
    segment = [];
    segmentIsSensitive = false;
  };

  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") {
      flush();
      continue;
    }
    segment.push(index);
    if (isDisplayFocusSensitive(op)) segmentIsSensitive = true;
    if (endsStraightLineSegment(op)) flush();
  }
  flush();

  return indexes;
}

function endsStraightLineSegment(op: IrOp): boolean {
  switch (op.kind) {
    case "jump":
    case "cjump":
    case "call":
    case "loop":
    case "indirect-jump":
    case "indirect-call":
    case "indirect-cjump":
    case "return":
    case "stop":
    case "orphan-address":
      return true;
    case "label":
    case "store":
    case "recall":
    case "indirect-store":
    case "indirect-recall":
    case "plain":
      return false;
  }
}

function selectHelpers(candidates: readonly Candidate[], ops: readonly IrOp[]): SelectedHelper[] {
  const existingLabels = new Set(ops.flatMap((op) => op.kind === "label" ? [op.name] : []));
  const protectedIndexes = new Set<number>();
  const selected: SelectedHelper[] = [];
  let labelIndex = 0;

  const ordered = [...candidates].sort((left, right) => {
    const leftSavings = netSavings(left.occurrences.length, left.cells);
    const rightSavings = netSavings(right.occurrences.length, right.cells);
    return rightSavings - leftSavings || right.cells - left.cells || left.key.localeCompare(right.key);
  });

  for (const candidate of ordered) {
    const occurrences = candidate.occurrences.filter((occurrence) =>
      !rangeIntersects(protectedIndexes, occurrence.start, occurrence.end)
    );
    if (occurrences.length < 2) continue;
    if (netSavings(occurrences.length, candidate.cells) <= 0) continue;

    const label = freshLabel(existingLabels, labelIndex);
    labelIndex += 1;
    existingLabels.add(label);
    selected.push({
      label,
      body: ops.slice(occurrences[0]!.start, occurrences[0]!.end + 1),
      occurrences,
      cells: candidate.cells,
    });
    for (const occurrence of occurrences) {
      markRange(protectedIndexes, occurrence.start, occurrence.end);
    }
  }

  return selected;
}

function netSavings(occurrences: number, cells: number): number {
  return occurrences * cells - (occurrences * 2 + cells + 1);
}

function helperCall(label: string, source: IrOp): IrOp {
  const meta: IrMeta = {
    mnemonic: "ПП",
    comment: "shared straight-line helper",
  };
  if ("meta" in source && source.meta.sourceLine !== undefined) {
    meta.sourceLine = source.meta.sourceLine;
  }
  return {
    kind: "call",
    target: label,
    opcode: 0x53,
    meta,
    targetMeta: { comment: "shared straight-line helper" },
  };
}

function markHelperBodyOp(op: IrOp): IrOp {
  if (!("meta" in op)) return op;
  const comment = op.meta.comment ?? "shared straight-line helper body";
  return {
    ...op,
    meta: {
      ...op.meta,
      comment,
    },
  } as IrOp;
}

function isShareableBodyOp(op: IrOp, allowDirectCalls = false): boolean {
  if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) return false;
  if ("opcode" in op && getOpcode(op.opcode).x2Effect === "restores") return false;
  switch (op.kind) {
    case "store":
    case "recall":
    case "indirect-store":
    case "indirect-recall":
    case "plain":
      return true;
    case "call":
      return allowDirectCalls;
    case "label":
    case "jump":
    case "cjump":
    case "loop":
    case "indirect-jump":
    case "indirect-call":
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
      return `store:${op.register}`;
    case "recall":
      return `recall:${op.register}`;
    case "indirect-store":
      return `indirect-store:${op.register}`;
    case "indirect-recall":
      return `indirect-recall:${op.register}`;
    case "plain":
      return `plain:${op.opcode}`;
    case "call":
      return `call:${op.target}`;
    case "label":
    case "jump":
    case "cjump":
    case "loop":
    case "indirect-jump":
    case "indirect-call":
    case "indirect-cjump":
    case "return":
    case "stop":
    case "orphan-address":
      return op.kind;
  }
}

function hasNumericFlowTarget(ops: readonly IrOp[]): boolean {
  return ops.some((op) => {
    switch (op.kind) {
      case "jump":
      case "cjump":
      case "call":
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

function freshLabel(existingLabels: ReadonlySet<string>, index: number): string {
  let suffix = index;
  while (true) {
    const label = `__shared_straight_line_helper_${suffix}`;
    if (!existingLabels.has(label)) return label;
    suffix += 1;
  }
}
