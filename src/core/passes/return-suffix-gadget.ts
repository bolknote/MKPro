import type { IrMeta, IrOp } from "../types.ts";
import { cellsPerOp, emptyResult, hasRewriteBarrier, type IrPass, type IrPassFn } from "./helpers.ts";
import {
  collectSuffixCandidates,
  createLabelAllocator,
  hasNumericOutlineFlowTarget,
  markRange,
  rangeIntersects,
  selectSharedSuffixes,
  targetKey,
  type LabelAllocator,
  type OutlineOccurrence,
} from "./outline.ts";

interface SelectedSuffix {
  kind: "jump";
  label: string;
  target: OutlineOccurrence;
  replacements: OutlineOccurrence[];
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
  if (hasNumericOutlineFlowTarget(ops)) return emptyResult(ops);
  if (ops.some((op) => op.kind === "label" && op.name.startsWith("__shared_straight_line_helper_"))) {
    return emptyResult(ops);
  }

  const candidates = collectSuffixCandidates(ops, {
    isTerminal: isShareableReturn,
    isBodyOp: isShareableBodyOp,
    opKey,
  });
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

function selectGadgets(
  candidates: ReturnType<typeof collectSuffixCandidates>,
  ops: readonly IrOp[],
): SelectedGadget[] {
  const labels = createLabelAllocator(ops, "__return_suffix_gadget_");
  const protectedIndexes = new Set<number>();
  const selected: SelectedGadget[] = selectSharedSuffixes(candidates, labels, protectedIndexes)
    .map((suffix) => ({ kind: "jump", ...suffix }));

  const bodyTargets = collectBodyTargets(ops, candidates);
  const bodyOccurrences = collectBodyOccurrences(ops, new Set(bodyTargets.keys()));
  const bodyCalls = selectBodyCalls(bodyTargets, bodyOccurrences, labels, protectedIndexes);
  selected.push(...bodyCalls);

  const tailCallTargets = collectTailCallTargets(ops);
  const tailCallOccurrences = collectTailCallOccurrences(ops, new Set(tailCallTargets.keys()));
  const tailCalls = selectBodyCalls(tailCallTargets, tailCallOccurrences, labels, protectedIndexes);
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

function collectBodyTargets(
  ops: readonly IrOp[],
  candidates: ReturnType<typeof collectSuffixCandidates>,
): Map<string, BodyTarget[]> {
  const result = new Map<string, BodyTarget[]>();
  for (const candidate of candidates) {
    for (const occurrence of candidate.occurrences) {
      const bodyKey = suffixBodyKey(occurrence.key);
      const bodyCells = occurrence.cells - cellsPerOp(ops[occurrence.end]!);
      if (bodyKey.length === 0 || bodyCells <= 2) continue;
      const targets = result.get(bodyKey) ?? [];
      targets.push({
        key: bodyKey,
        start: occurrence.start,
        bodyEnd: occurrence.end - 1,
        end: occurrence.end,
        cells: bodyCells,
      });
      result.set(bodyKey, targets);
    }
  }
  return result;
}

/** The suffix key minus its final (return) op: everything before the last newline. */
function suffixBodyKey(key: string): string {
  const cut = key.lastIndexOf("\n");
  return cut === -1 ? "" : key.slice(0, cut);
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
  labels: LabelAllocator,
  protectedIndexes: Set<number>,
): SelectedBodyCall[] {
  const selected: SelectedBodyCall[] = [];
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

    selected.push({ kind: "call", label: labels.next(), target, replacements: [...replacements] });

    markRange(protectedIndexes, target.start, target.end);
    for (const occurrence of replacements) {
      markRange(protectedIndexes, occurrence.start, occurrence.end);
    }
  }

  return selected;
}

function sameTargetBody(occurrence: BodyOccurrence, target: BodyTarget): boolean {
  return occurrence.start === target.start && occurrence.end === target.bodyEnd;
}
