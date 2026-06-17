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
import {
  createLabelAllocator,
  hasNumericOutlineFlowTarget,
  markRange,
  rangeIntersects,
  type LabelAllocator,
} from "./outline.ts";

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
  entries: SelectedHelperEntry[];
}

interface SelectedHelperEntry {
  label: string;
  offset: number;
  replacements: Occurrence[];
  cells: number;
}

const MIN_BODY_CELLS = 4;
const MIN_ENTRY_CELLS = 3;
const GENERATED_BODY_LABEL_PREFIXES = [
  "__return_suffix_gadget_",
  "__shared_terminal_tail_",
  "__shared_straight_line_helper_",
];

const run: IrPassFn = (ops, context) => {
  if (hasNumericOutlineFlowTarget(ops)) return emptyResult(ops);

  const selected = selectHelpers(collectCandidates(ops, context.options.sharedStraightLineCallBodies === true), ops);
  if (selected.length === 0) return emptyResult(ops);

  const replacementByStart = new Map<number, { end: number; label: string; entry: boolean }>();
  let applied = 0;
  let savedCells = 0;
  let entryCalls = 0;
  for (const helper of selected) {
    const helperCost = helper.cells + 1;
    const replacementSavings = helper.occurrences.reduce((sum, occurrence) => sum + occurrence.cells - 2, 0);
    const entrySavings = helper.entries.reduce(
      (sum, entry) => sum + entry.replacements.length * (entry.cells - 2),
      0,
    );
    savedCells += replacementSavings + entrySavings - helperCost;
    for (const occurrence of helper.occurrences) {
      replacementByStart.set(occurrence.start, { end: occurrence.end, label: helper.label, entry: false });
      applied += 1;
    }
    for (const entry of helper.entries) {
      for (const occurrence of entry.replacements) {
        replacementByStart.set(occurrence.start, { end: occurrence.end, label: entry.label, entry: true });
        applied += 1;
        entryCalls += 1;
      }
    }
  }

  const result: IrOp[] = [];
  for (let index = 0; index < ops.length; index += 1) {
    const replacement = replacementByStart.get(index);
    if (replacement !== undefined) {
      result.push(helperCall(replacement.label, ops[index]!, replacement.entry));
      index = replacement.end;
      continue;
    }
    result.push(ops[index]!);
  }

  for (const helper of selected) {
    result.push({ kind: "label", name: helper.label });
    const entriesByOffset = new Map<number, string[]>();
    for (const entry of helper.entries) {
      const labels = entriesByOffset.get(entry.offset) ?? [];
      labels.push(entry.label);
      entriesByOffset.set(entry.offset, labels);
    }
    for (let index = 0; index < helper.body.length; index += 1) {
      for (const label of entriesByOffset.get(index) ?? []) {
        result.push({ kind: "label", name: label });
      }
      result.push(markHelperBodyOp(helper.body[index]!));
    }
    result.push({ kind: "return", opcode: 0x52, meta: { mnemonic: "В/О", comment: "shared straight-line helper return" } });
  }

  return {
    ops: result,
    applied,
    optimizations: [
      {
        name: "shared-straight-line-helper",
        detail: `Extracted ${selected.length} straight-line helper${selected.length === 1 ? "" : "s"} from ${applied} repeated body occurrence${applied === 1 ? "" : "s"} (${savedCells} cell${savedCells === 1 ? "" : "s"} saved).`,
      },
      ...(entryCalls === 0
        ? []
        : [{
          name: "multi-entry-straight-line-helper",
          detail: `Reused ${entryCalls} repeated helper suffix${entryCalls === 1 ? "" : "es"} by adding internal helper entry label${entryCalls === 1 ? "" : "s"}.`,
        }]),
    ],
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
    if (startsAtX2Restore(ops, start)) continue;
    if (startsAfterX2Restore(ops, start)) continue;
    const parts: string[] = [];
    let cells = 0;
    for (let end = start; end < ops.length; end += 1) {
      if (protectedIndexes.has(end)) break;
      const op = ops[end]!;
      if (!isShareableBodyOp(op, allowDirectCalls)) break;
      parts.push(opKey(op));
      cells += cellsPerOp(op);
      if (cells < MIN_ENTRY_CELLS) continue;
      if (endsBeforeX2Restore(ops, end)) continue;
      if (!x2RestoreBoundariesAreInternal(ops, start, end)) continue;
      const key = parts.join("\n");
      const occurrences = byKey.get(key) ?? [];
      occurrences.push({ key, start, end, cells });
      byKey.set(key, occurrences);
    }
  }

  return [...byKey.entries()]
    .map(([key, occurrences]) => ({ key, occurrences, cells: occurrences[0]!.cells }))
    .filter((candidate) => candidate.occurrences.length >= 1);
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
  const labels = createLabelAllocator(ops, "__shared_straight_line_helper_");
  const protectedIndexes = new Set<number>();
  const selected: SelectedHelper[] = [];

  const ordered = candidates.filter((candidate) =>
    candidate.cells >= MIN_BODY_CELLS && candidate.occurrences.length >= 2
  ).sort((left, right) => {
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

    selected.push({
      label: labels.next(),
      body: ops.slice(occurrences[0]!.start, occurrences[0]!.end + 1),
      occurrences,
      cells: candidate.cells,
      entries: [],
    });
    for (const occurrence of occurrences) {
      markRange(protectedIndexes, occurrence.start, occurrence.end);
    }
  }

  selectInternalEntries(selected, candidates, labels, protectedIndexes);
  selectAnchoredInternalEntryHelpers(selected, candidates, ops, labels, protectedIndexes);
  return selected;
}

function netSavings(occurrences: number, cells: number): number {
  return occurrences * cells - (occurrences * 2 + cells + 1);
}

interface InternalEntryCandidate {
  helper: SelectedHelper;
  offset: number;
  key: string;
  cells: number;
  occurrences: Occurrence[];
}

function selectInternalEntries(
  helpers: readonly SelectedHelper[],
  candidates: readonly Candidate[],
  labels: LabelAllocator,
  protectedIndexes: Set<number>,
): void {
  const candidateByKey = new Map(candidates.map((candidate) => [candidate.key, candidate]));
  const entryCandidates: InternalEntryCandidate[] = [];
  for (const helper of helpers) {
    const parts = helper.body.map(opKey);
    const suffixCells = helper.body.map(cellsPerOp);
    for (let offset = 1; offset < helper.body.length; offset += 1) {
      const cells = suffixCells.slice(offset).reduce((sum, count) => sum + count, 0);
      if (cells < MIN_ENTRY_CELLS) continue;
      const key = parts.slice(offset).join("\n");
      const candidate = candidateByKey.get(key);
      if (candidate === undefined) continue;
      entryCandidates.push({ helper, offset, key, cells, occurrences: candidate.occurrences });
    }
  }

  const ordered = entryCandidates.sort((left, right) => {
    const leftSavings = left.occurrences.length * (left.cells - 2);
    const rightSavings = right.occurrences.length * (right.cells - 2);
    return rightSavings - leftSavings || right.cells - left.cells || left.key.localeCompare(right.key);
  });
  for (const candidate of ordered) {
    if (candidate.cells <= 2) continue;
    const replacements = candidate.occurrences.filter((occurrence) =>
      !rangeIntersects(protectedIndexes, occurrence.start, occurrence.end)
    );
    if (replacements.length === 0) continue;

    candidate.helper.entries.push({
      label: labels.next(),
      offset: candidate.offset,
      replacements,
      cells: candidate.cells,
    });
    for (const replacement of replacements) {
      markRange(protectedIndexes, replacement.start, replacement.end);
    }
  }
}

interface AnchoredInternalEntryCandidate {
  readonly bodyCandidate: Candidate;
  readonly anchor: Occurrence;
  readonly offset: number;
  readonly key: string;
  readonly cells: number;
  readonly replacements: Occurrence[];
  readonly savings: number;
}

function selectAnchoredInternalEntryHelpers(
  selected: SelectedHelper[],
  candidates: readonly Candidate[],
  ops: readonly IrOp[],
  labels: LabelAllocator,
  protectedIndexes: Set<number>,
): void {
  const candidateByKey = new Map(candidates.map((candidate) => [candidate.key, candidate]));
  const entryCandidates: AnchoredInternalEntryCandidate[] = [];

  for (const bodyCandidate of candidates) {
    if (bodyCandidate.cells < MIN_BODY_CELLS) continue;
    for (const anchor of bodyCandidate.occurrences) {
      if (rangeIntersects(protectedIndexes, anchor.start, anchor.end)) continue;
      const body = ops.slice(anchor.start, anchor.end + 1);
      const parts = body.map(opKey);
      const suffixCells = body.map(cellsPerOp);
      for (let offset = 1; offset < body.length; offset += 1) {
        const cells = suffixCells.slice(offset).reduce((sum, count) => sum + count, 0);
        if (cells < MIN_ENTRY_CELLS) continue;
        const key = parts.slice(offset).join("\n");
        const suffixCandidate = candidateByKey.get(key);
        if (suffixCandidate === undefined) continue;
        const replacements = suffixCandidate.occurrences.filter((occurrence) =>
          !rangeIntersects(protectedIndexes, occurrence.start, occurrence.end) &&
          !rangesOverlap(anchor.start, anchor.end, occurrence.start, occurrence.end)
        );
        if (replacements.length === 0) continue;
        const savings = replacements.length * (cells - 2) - 3;
        if (savings <= 0) continue;
        entryCandidates.push({
          bodyCandidate,
          anchor,
          offset,
          key,
          cells,
          replacements,
          savings,
        });
      }
    }
  }

  const ordered = entryCandidates.sort((left, right) =>
    right.savings - left.savings ||
    right.cells - left.cells ||
    right.bodyCandidate.cells - left.bodyCandidate.cells ||
    left.key.localeCompare(right.key)
  );

  for (const candidate of ordered) {
    if (rangeIntersects(protectedIndexes, candidate.anchor.start, candidate.anchor.end)) continue;
    const replacements = candidate.replacements.filter((occurrence) =>
      !rangeIntersects(protectedIndexes, occurrence.start, occurrence.end)
    );
    if (replacements.length === 0) continue;
    const savings = replacements.length * (candidate.cells - 2) - 3;
    if (savings <= 0) continue;

    const helper: SelectedHelper = {
      label: labels.next(),
      body: ops.slice(candidate.anchor.start, candidate.anchor.end + 1),
      occurrences: [candidate.anchor],
      cells: candidate.bodyCandidate.cells,
      entries: [{
        label: labels.next(),
        offset: candidate.offset,
        replacements,
        cells: candidate.cells,
      }],
    };
    selected.push(helper);
    markRange(protectedIndexes, candidate.anchor.start, candidate.anchor.end);
    for (const replacement of replacements) {
      markRange(protectedIndexes, replacement.start, replacement.end);
    }
  }
}

function rangesOverlap(leftStart: number, leftEnd: number, rightStart: number, rightEnd: number): boolean {
  return leftStart <= rightEnd && rightStart <= leftEnd;
}

function helperCall(label: string, source: IrOp, entry = false): IrOp {
  const meta: IrMeta = {
    mnemonic: "ПП",
    comment: entry ? "shared straight-line helper entry" : "shared straight-line helper",
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

function startsAtX2Restore(ops: readonly IrOp[], start: number): boolean {
  const op = ops[start];
  return op !== undefined && isX2RestoreOp(op);
}

function startsAfterX2Restore(ops: readonly IrOp[], start: number): boolean {
  const previous = ops[start - 1];
  const current = ops[start];
  return previous !== undefined &&
    current !== undefined &&
    isX2RestoreOp(previous) &&
    !isX2AffectingOp(current);
}

function endsBeforeX2Restore(ops: readonly IrOp[], end: number): boolean {
  const next = nextExecutableOp(ops, end + 1);
  return next !== undefined && isX2RestoreOp(next);
}

function nextExecutableOp(ops: readonly IrOp[], start: number): IrOp | undefined {
  for (let index = start; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind !== "label") return op;
  }
  return undefined;
}

function x2RestoreBoundariesAreInternal(ops: readonly IrOp[], start: number, end: number): boolean {
  for (let index = start; index <= end; index += 1) {
    const op = ops[index]!;
    if (!isX2RestoreOp(op)) continue;
    if (index === start) return false;
    const previous = ops[index - 1];
    if (previous === undefined) return false;
    const previousEffect = "opcode" in previous ? getOpcode(previous.opcode).x2Effect : "preserves";
    if (previousEffect !== "affects" && previousEffect !== "restores") return false;
  }
  return true;
}

function isX2AffectingOp(op: IrOp): boolean {
  return "opcode" in op && getOpcode(op.opcode).x2Effect === "affects";
}

function isX2RestoreOp(op: IrOp): boolean {
  return "opcode" in op && getOpcode(op.opcode).x2Effect === "restores";
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
