// Shared skeleton for the outlining pass family.
//
// shared-terminal-tail, return-suffix-gadget, shared-straight-line-helper, and
// shared-call-tail all hunt for repeated op sequences and replace duplicates
// with a 2-cell transfer to one shared copy. They used to carry private copies
// of the same machinery: backward suffix collection keyed by op identity,
// greedy savings-ordered selection with range protection, collision-safe label
// allocation, and numeric-flow guards. This module owns those pieces; each
// pass keeps its own matcher (what counts as a shareable op / terminal) and
// its established label prefix and report identity.

import type { IrOp } from "../types.ts";
import { cellsPerOp } from "./helpers.ts";

export interface OutlineOccurrence {
  readonly key: string;
  readonly start: number;
  readonly end: number;
  readonly cells: number;
}

export interface OutlineCandidate {
  readonly key: string;
  readonly occurrences: OutlineOccurrence[];
  readonly cells: number;
}

export interface SelectedOutline {
  readonly label: string;
  readonly target: OutlineOccurrence;
  readonly replacements: OutlineOccurrence[];
}

export function targetKey(target: string | number): string {
  return typeof target === "number" ? `#${target}` : target;
}

/**
 * Outlining passes refuse to run on IR with raw numeric flow targets: moving
 * ops around would silently break hand-written cell addresses.
 */
export function hasNumericOutlineFlowTarget(ops: readonly IrOp[]): boolean {
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

export function rangeIntersects(indexes: ReadonlySet<number>, start: number, end: number): boolean {
  for (let index = start; index <= end; index += 1) {
    if (indexes.has(index)) return true;
  }
  return false;
}

export function markRange(indexes: Set<number>, start: number, end: number): void {
  for (let index = start; index <= end; index += 1) indexes.add(index);
}

export interface LabelAllocator {
  /** Allocate the next collision-free `${prefix}${n}` label and reserve it. */
  next(): string;
}

/**
 * Label allocator seeded with every label already present in the program, so
 * repeated fixpoint iterations of an outlining pass can never emit a duplicate.
 */
export function createLabelAllocator(ops: readonly IrOp[], prefix: string): LabelAllocator {
  const existing = new Set(ops.flatMap((op) => op.kind === "label" ? [op.name] : []));
  let counter = 0;
  return {
    next: (): string => {
      let suffix = counter;
      while (existing.has(`${prefix}${suffix}`)) suffix += 1;
      counter += 1;
      const label = `${prefix}${suffix}`;
      existing.add(label);
      return label;
    },
  };
}

export interface SuffixCollectionConfig {
  /** Final op of a candidate suffix (e.g. return, or any terminal flow op). */
  readonly isTerminal: (op: IrOp) => boolean;
  /** Ops allowed inside the straight-line body before the terminal. */
  readonly isBodyOp: (op: IrOp) => boolean;
  /** Identity key for one op; sequences match iff their key lists match. */
  readonly opKey: (op: IrOp) => string;
}

/**
 * Collect every repeated-or-not straight-line suffix ending in a terminal op,
 * keyed by op identity. An occurrence is recorded once the sequence is longer
 * than the 2 cells its replacement jump would cost.
 */
export function collectSuffixCandidates(
  ops: readonly IrOp[],
  config: SuffixCollectionConfig,
): OutlineCandidate[] {
  const byKey = new Map<string, OutlineOccurrence[]>();

  for (let end = 0; end < ops.length; end += 1) {
    const final = ops[end]!;
    if (!config.isTerminal(final)) continue;

    const parts = [config.opKey(final)];
    let cells = cellsPerOp(final);
    for (let start = end - 1; start >= 0; start -= 1) {
      const op = ops[start]!;
      if (!config.isBodyOp(op)) break;

      parts.unshift(config.opKey(op));
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

/**
 * Greedy shared-suffix selection: order candidates by total saved cells, keep
 * the first occurrence as the shared target, and replace the rest, protecting
 * every chosen range from later overlapping selections.
 */
export function selectSharedSuffixes(
  candidates: readonly OutlineCandidate[],
  labels: LabelAllocator,
  protectedIndexes: Set<number>,
): SelectedOutline[] {
  const selected: SelectedOutline[] = [];

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

    selected.push({ label: labels.next(), target, replacements });

    for (const occurrence of [target, ...replacements]) {
      markRange(protectedIndexes, occurrence.start, occurrence.end);
    }
  }

  return selected;
}
