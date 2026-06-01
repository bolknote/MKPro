import type { IrMeta, IrOp } from "../types.ts";
import { cellsPerOp, emptyResult, hasRewriteBarrier, type IrPass, type IrPassFn } from "./helpers.ts";

interface ReturnSuffixOccurrence {
  key: string;
  bodyKey: string;
  start: number;
  end: number;
  cells: number;
  bodyCells: number;
}

interface ReturnSuffixCandidate {
  key: string;
  occurrences: ReturnSuffixOccurrence[];
  cells: number;
}

interface SelectedSuffix {
  kind: "jump";
  label: string;
  target: ReturnSuffixOccurrence;
  replacements: ReturnSuffixOccurrence[];
}

interface BodyOccurrence {
  key: string;
  start: number;
  end: number;
  cells: number;
}

interface BodyTarget {
  key: string;
  start: number;
  bodyEnd: number;
  end: number;
  cells: number;
}

interface SelectedBodyCall {
  kind: "call";
  label: string;
  target: BodyTarget;
  replacements: BodyOccurrence[];
}

type SelectedGadget = SelectedSuffix | SelectedBodyCall;

const run: IrPassFn = (ops) => {
  if (hasNumericFlowTarget(ops)) return emptyResult(ops);

  const candidates = collectReturnSuffixCandidates(ops);
  const selected = selectGadgets(candidates, ops);
  if (selected.length === 0) return emptyResult(ops);

  const targetLabels = new Map<number, string[]>();
  const replacementByStart = new Map<number, { kind: "jump" | "call"; end: number; label: string; skipNextRecall?: boolean }>();
  let applied = 0;
  let savedCells = 0;
  let jumps = 0;
  let calls = 0;
  let reusedReturnX = 0;

  for (const gadget of selected) {
    const labels = targetLabels.get(gadget.target.start) ?? [];
    labels.push(gadget.label);
    targetLabels.set(gadget.target.start, labels);
    for (const replacement of gadget.replacements) {
      const returnX = gadget.kind === "call" ? bodyReturnX(ops, gadget.target) : undefined;
      const skipNextRecall = returnX !== undefined && recallMatchesStore(ops[replacement.end + 1], returnX);
      replacementByStart.set(replacement.start, {
        kind: gadget.kind,
        end: replacement.end,
        label: gadget.label,
        ...(skipNextRecall ? { skipNextRecall } : {}),
      });
      applied += 1;
      savedCells += replacement.cells - 2;
      if (skipNextRecall) {
        savedCells += 1;
        reusedReturnX += 1;
      }
      if (gadget.kind === "jump") jumps += 1;
      else calls += 1;
    }
  }

  const result: IrOp[] = [];
  for (let index = 0; index < ops.length; index += 1) {
    for (const label of targetLabels.get(index) ?? []) {
      result.push({ kind: "label", name: label });
    }

    const replacement = replacementByStart.get(index);
    if (replacement !== undefined) {
      result.push(
        replacement.kind === "jump"
          ? gadgetJump(replacement.label, ops[index]!)
          : gadgetCall(replacement.label, ops[index]!),
      );
      index = replacement.skipNextRecall === true ? replacement.end + 1 : replacement.end;
      continue;
    }

    result.push(ops[index]!);
  }

  return {
    ops: result,
    applied,
    optimizations: [{
      name: "return-suffix-gadget",
      detail: `Shared ${applied} return/tail-call gadget${applied === 1 ? "" : "s"} (${jumps} jump, ${calls} call; ${savedCells} cell${savedCells === 1 ? "" : "s"} saved${reusedReturnX === 0 ? "" : `, ${reusedReturnX} returned-X recall${reusedReturnX === 1 ? "" : "s"} reused`}).`,
    }],
  };
};

type StoreResult =
  | { kind: "store"; register: string }
  | { kind: "indirect-store"; register: string };

function bodyReturnX(ops: readonly IrOp[], target: BodyTarget): StoreResult | undefined {
  const final = ops[target.bodyEnd];
  if (final === undefined || hasRewriteBarrier(final)) return undefined;
  if (final.kind === "store") return { kind: "store", register: final.register };
  if (final.kind === "indirect-store") return { kind: "indirect-store", register: final.register };
  return undefined;
}

function recallMatchesStore(op: IrOp | undefined, store: StoreResult): boolean {
  if (op === undefined || hasRewriteBarrier(op)) return false;
  return (store.kind === "store" && op.kind === "recall" && op.register === store.register) ||
    (store.kind === "indirect-store" && op.kind === "indirect-recall" && op.register === store.register);
}

export const returnSuffixGadget: IrPass = {
  name: "return-suffix-gadget",
  run,
  layoutSafe: false,
};

function collectReturnSuffixCandidates(ops: readonly IrOp[]): ReturnSuffixCandidate[] {
  const byKey = new Map<string, ReturnSuffixOccurrence[]>();

  for (let end = 0; end < ops.length; end += 1) {
    const final = ops[end]!;
    if (!isShareableReturn(final)) continue;

    const parts = [opKey(final)];
    let cells = cellsPerOp(final);
    for (let start = end - 1; start >= 0; start -= 1) {
      const op = ops[start]!;
      if (!isShareableBodyOp(op)) break;

      parts.unshift(opKey(op));
      cells += cellsPerOp(op);
      if (cells <= 2) continue;

      const key = parts.join("\n");
      const bodyKey = parts.slice(0, -1).join("\n");
      const occurrences = byKey.get(key) ?? [];
      occurrences.push({ key, bodyKey, start, end, cells, bodyCells: cells - cellsPerOp(final) });
      byKey.set(key, occurrences);
    }
  }

  return [...byKey.entries()]
    .map(([key, occurrences]) => ({ key, occurrences, cells: occurrences[0]!.cells }));
}

function selectGadgets(
  candidates: readonly ReturnSuffixCandidate[],
  ops: readonly IrOp[],
): SelectedGadget[] {
  const existingLabels = new Set(ops.flatMap((op) => op.kind === "label" ? [op.name] : []));
  const protectedIndexes = new Set<number>();
  const selected: SelectedGadget[] = [];
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
    selected.push({ kind: "jump", label, target, replacements });

    for (const occurrence of [target, ...replacements]) {
      markRange(protectedIndexes, occurrence.start, occurrence.end);
    }
  }

  const bodyTargets = collectBodyTargets(candidates);
  const bodyOccurrences = collectBodyOccurrences(ops, new Set(bodyTargets.keys()));
  const bodyCalls = selectBodyCalls(bodyTargets, bodyOccurrences, existingLabels, protectedIndexes, labelIndex);
  selected.push(...bodyCalls);

  const tailCallTargets = collectTailCallTargets(ops);
  const tailCallOccurrences = collectTailCallOccurrences(ops, new Set(tailCallTargets.keys()));
  const tailCalls = selectBodyCalls(tailCallTargets, tailCallOccurrences, existingLabels, protectedIndexes, labelIndex);
  selected.push(...tailCalls);

  return selected;
}

function gadgetJump(label: string, source: IrOp): IrOp {
  const meta: IrMeta = {
    mnemonic: "БП",
    comment: "return suffix gadget",
  };
  if ("meta" in source && source.meta.sourceLine !== undefined) {
    meta.sourceLine = source.meta.sourceLine;
  }
  return {
    kind: "jump",
    target: label,
    opcode: 0x51,
    meta,
    targetMeta: { comment: "return suffix gadget" },
  };
}

function gadgetCall(label: string, source: IrOp): IrOp {
  const meta: IrMeta = {
    mnemonic: "ПП",
    comment: "proc call return suffix gadget",
  };
  if ("meta" in source && source.meta.sourceLine !== undefined) {
    meta.sourceLine = source.meta.sourceLine;
  }
  return {
    kind: "call",
    target: label,
    opcode: 0x53,
    meta,
    targetMeta: { comment: "return suffix gadget target" },
  };
}

function isShareableReturn(op: IrOp): boolean {
  return op.kind === "return" && !hasRewriteBarrier(op);
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

function isCallableBodyOp(op: IrOp): boolean {
  return isShareableBodyOp(op) && op.kind !== "stop";
}

function isTailCallGadgetBodyOp(op: IrOp): boolean {
  if (hasRewriteBarrier(op)) return false;
  switch (op.kind) {
    case "store":
    case "recall":
    case "indirect-store":
    case "indirect-recall":
    case "plain":
      return true;
    case "label":
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
      return false;
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

function opKey(op: IrOp): string {
  switch (op.kind) {
    case "store":
      return `store:${op.opcode}:${op.register}`;
    case "recall":
      return `recall:${op.opcode}:${op.register}`;
    case "indirect-store":
      return `indirect-store:${op.opcode}:${op.register}`;
    case "indirect-recall":
      return `indirect-recall:${op.opcode}:${op.register}`;
    case "call":
      return `call:${op.opcode}:${targetKey(op.target)}:${op.targetMeta.formalOpcode ?? ""}`;
    case "indirect-call":
      return `indirect-call:${op.opcode}:${op.register}`;
    case "stop":
      return `stop:${op.opcode}`;
    case "plain":
      return `plain:${op.opcode}`;
    case "return":
      return `return:${op.opcode}`;
    case "label":
    case "jump":
    case "cjump":
    case "loop":
    case "indirect-jump":
    case "indirect-cjump":
    case "orphan-address":
      return op.kind;
  }
}

function collectBodyTargets(candidates: readonly ReturnSuffixCandidate[]): Map<string, BodyTarget[]> {
  const result = new Map<string, BodyTarget[]>();
  for (const candidate of candidates) {
    for (const occurrence of candidate.occurrences) {
      if (occurrence.bodyKey.length === 0 || occurrence.bodyCells <= 2) continue;
      const targets = result.get(occurrence.bodyKey) ?? [];
      targets.push({
        key: occurrence.bodyKey,
        start: occurrence.start,
        bodyEnd: occurrence.end - 1,
        end: occurrence.end,
        cells: occurrence.bodyCells,
      });
      result.set(occurrence.bodyKey, targets);
    }
  }
  return result;
}

function collectBodyOccurrences(
  ops: readonly IrOp[],
  wantedKeys: ReadonlySet<string>,
): Map<string, BodyOccurrence[]> {
  const result = new Map<string, BodyOccurrence[]>();
  if (wantedKeys.size === 0) return result;

  for (let start = 0; start < ops.length; start += 1) {
    const parts: string[] = [];
    let cells = 0;
    for (let end = start; end < ops.length; end += 1) {
      const op = ops[end]!;
      if (!isCallableBodyOp(op)) break;
      parts.push(opKey(op));
      cells += cellsPerOp(op);
      if (cells <= 2) continue;

      const key = parts.join("\n");
      if (!wantedKeys.has(key)) continue;
      const occurrences = result.get(key) ?? [];
      occurrences.push({ key, start, end, cells });
      result.set(key, occurrences);
    }
  }

  return result;
}

function collectTailCallTargets(ops: readonly IrOp[]): Map<string, BodyTarget[]> {
  const result = new Map<string, BodyTarget[]>();

  for (let jumpIndex = 0; jumpIndex < ops.length; jumpIndex += 1) {
    const jump = ops[jumpIndex]!;
    if (jump.kind !== "jump" || typeof jump.target !== "string" || hasRewriteBarrier(jump)) continue;

    const parts = [`target:${targetKey(jump.target)}`];
    let cells = cellsPerOp(jump);
    for (let start = jumpIndex - 1; start >= 0; start -= 1) {
      const op = ops[start]!;
      if (!isTailCallGadgetBodyOp(op)) break;

      parts.unshift(opKey(op));
      cells += cellsPerOp(op);
      const bodyCells = cells - cellsPerOp(jump);
      if (bodyCells <= 0) continue;

      const key = parts.join("\n");
      const targets = result.get(key) ?? [];
      targets.push({
        key,
        start,
        bodyEnd: jumpIndex - 1,
        end: jumpIndex,
        cells: bodyCells,
      });
      result.set(key, targets);
    }
  }

  return result;
}

function collectTailCallOccurrences(
  ops: readonly IrOp[],
  wantedKeys: ReadonlySet<string>,
): Map<string, BodyOccurrence[]> {
  const result = new Map<string, BodyOccurrence[]>();
  if (wantedKeys.size === 0) return result;

  for (let start = 0; start < ops.length; start += 1) {
    const parts: string[] = [];
    let cells = 0;
    for (let callIndex = start; callIndex < ops.length; callIndex += 1) {
      const op = ops[callIndex]!;
      if (op.kind === "call" && typeof op.target === "string" && !hasRewriteBarrier(op)) {
        const key = [...parts, `target:${targetKey(op.target)}`].join("\n");
        if (cells > 0 && wantedKeys.has(key)) {
          const occurrences = result.get(key) ?? [];
          occurrences.push({ key, start, end: callIndex, cells: cells + cellsPerOp(op) });
          result.set(key, occurrences);
        }
        break;
      }
      if (!isTailCallGadgetBodyOp(op)) break;
      parts.push(opKey(op));
      cells += cellsPerOp(op);
    }
  }

  return result;
}

function selectBodyCalls(
  targets: ReadonlyMap<string, readonly BodyTarget[]>,
  occurrences: ReadonlyMap<string, readonly BodyOccurrence[]>,
  existingLabels: Set<string>,
  protectedIndexes: Set<number>,
  initialLabelIndex: number,
): SelectedBodyCall[] {
  const selected: SelectedBodyCall[] = [];
  let labelIndex = initialLabelIndex;
  const keys = [...targets.keys()].sort((left, right) => {
    const leftCells = targets.get(left)?.[0]?.cells ?? 0;
    const rightCells = targets.get(right)?.[0]?.cells ?? 0;
    return rightCells - leftCells || left.localeCompare(right);
  });

  for (const key of keys) {
    const availableTargets = (targets.get(key) ?? [])
      .filter((target) => !rangeIntersects(protectedIndexes, target.start, target.end));
    if (availableTargets.length === 0) continue;
    const target = availableTargets[0]!;
    const replacements = (occurrences.get(key) ?? [])
      .filter((occurrence) => !sameTargetBody(occurrence, target))
      .filter((occurrence) => !rangeIntersects(protectedIndexes, occurrence.start, occurrence.end));
    if (replacements.length === 0) continue;

    const savedCells = replacements.reduce((sum, occurrence) => sum + occurrence.cells - 2, 0);
    if (savedCells <= 0) continue;

    const label = freshLabel(existingLabels, labelIndex);
    labelIndex += 1;
    existingLabels.add(label);
    selected.push({ kind: "call", label, target, replacements: [...replacements] });

    markRange(protectedIndexes, target.start, target.end);
    for (const occurrence of replacements) {
      markRange(protectedIndexes, occurrence.start, occurrence.end);
    }
  }

  return selected;
}

function targetKey(target: string | number): string {
  return typeof target === "number" ? `#${target}` : target;
}

function sameTargetBody(occurrence: BodyOccurrence, target: BodyTarget): boolean {
  return occurrence.start === target.start && occurrence.end === target.bodyEnd;
}

function rangeIntersects(indexes: ReadonlySet<number>, start: number, end: number): boolean {
  for (let index = start; index <= end; index += 1) {
    if (indexes.has(index)) return true;
  }
  return false;
}

function markRange(indexes: Set<number>, start: number, end: number): void {
  for (let index = start; index <= end; index += 1) {
    indexes.add(index);
  }
}

function freshLabel(existingLabels: ReadonlySet<string>, index: number): string {
  let suffix = index;
  while (true) {
    const label = `__return_suffix_gadget_${suffix}`;
    if (!existingLabels.has(label)) return label;
    suffix += 1;
  }
}
