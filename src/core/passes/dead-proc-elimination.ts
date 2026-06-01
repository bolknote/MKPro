import type { IrOp } from "../types.ts";
import { cellsPerOp, type IrPass, type IrPassFn } from "./helpers.ts";

interface ProcBlock {
  readonly name: string;
  readonly start: number;
  readonly end: number;
}

function collectProcedureBlocks(ops: readonly IrOp[]): ProcBlock[] {
  const blocks: ProcBlock[] = [];
  let active: { name: string; start: number } | undefined;
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind !== "label") continue;
    if (op.procedureBoundary === "start") {
      active = { name: op.procedureName ?? op.name, start: index };
      continue;
    }
    if (op.procedureBoundary === "end" && active !== undefined) {
      const name = op.procedureName ?? active.name;
      if (name === active.name) blocks.push({ name, start: active.start, end: index + 1 });
      active = undefined;
    }
  }
  return blocks;
}

function knownIndirectTarget(op: IrOp): number | undefined {
  if (op.kind !== "indirect-jump" && op.kind !== "indirect-call" && op.kind !== "indirect-cjump") {
    return undefined;
  }
  const match = /\bindirect-target=(\d+)\b/u.exec(op.meta.comment ?? "");
  if (!match) return undefined;
  const target = Number(match[1]);
  return Number.isInteger(target) && target >= 0 ? target : undefined;
}

function canFallThroughPastBlock(ops: readonly IrOp[], block: ProcBlock): boolean {
  for (let index = block.end - 1; index >= block.start; index -= 1) {
    const op = ops[index]!;
    if (op.kind === "label") continue;
    if (op.kind === "jump" || op.kind === "return" || op.kind === "indirect-jump") return false;
    if (op.kind === "stop" && op.semantic === "halt") return false;
    if (op.kind === "plain" && op.meta.comment?.startsWith("halt")) return false;
    return true;
  }
  return true;
}

const run: IrPassFn = (ops, context) => {
  if (context.options.disableInterproceduralOpts === true) {
    return { ops: [...ops], applied: 0, optimizations: [] };
  }
  if (ops.some((op) => "meta" in op && op.meta.raw === true)) {
    return { ops: [...ops], applied: 0, optimizations: [] };
  }

  const blocks = collectProcedureBlocks(ops);
  if (blocks.length === 0) return { ops: [...ops], applied: 0, optimizations: [] };

  const ownerByIndex = new Map<number, string>();
  const labelOwner = new Map<string, string>();
  for (const block of blocks) {
    for (let index = block.start; index < block.end; index += 1) {
      ownerByIndex.set(index, block.name);
      const op = ops[index]!;
      if (op.kind === "label") labelOwner.set(op.name, block.name);
    }
  }
  const addressOwner = new Map<number, string>();
  let address = 0;
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") continue;
    const owner = ownerByIndex.get(index);
    if (owner !== undefined) {
      for (let offset = 0; offset < cellsPerOp(op); offset += 1) {
        addressOwner.set(address + offset, owner);
      }
    }
    address += cellsPerOp(op);
  }

  const rootTargets = new Set<string>();
  const edges = new Map<string, Set<string>>();
  const addEdge = (sourceOwner: string, targetOwner: string): void => {
    const targets = edges.get(sourceOwner) ?? new Set<string>();
    targets.add(targetOwner);
    edges.set(sourceOwner, targets);
  };
  const markReference = (index: number, targetOwner: string | undefined): void => {
    if (targetOwner === undefined) return;
    const sourceOwner = ownerByIndex.get(index);
    if (sourceOwner === undefined) {
      rootTargets.add(targetOwner);
    } else {
      addEdge(sourceOwner, targetOwner);
    }
  };
  for (const block of blocks) {
    if (!canFallThroughPastBlock(ops, block)) continue;
    const targetOwner = ownerByIndex.get(block.end);
    if (targetOwner !== undefined && targetOwner !== block.name) addEdge(block.name, targetOwner);
  }
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (
      op.kind !== "jump" &&
      op.kind !== "cjump" &&
      op.kind !== "call" &&
      op.kind !== "loop" &&
      op.kind !== "orphan-address" &&
      op.kind !== "indirect-jump" &&
      op.kind !== "indirect-call" &&
      op.kind !== "indirect-cjump"
    ) {
      continue;
    }
    if (op.kind === "indirect-jump" || op.kind === "indirect-call" || op.kind === "indirect-cjump") {
      const target = knownIndirectTarget(op);
      if (target === undefined) return { ops: [...ops], applied: 0, optimizations: [] };
      markReference(index, addressOwner.get(target));
      continue;
    }
    markReference(
      index,
      typeof op.target === "string" ? labelOwner.get(op.target) : addressOwner.get(op.target),
    );
  }

  const reachable = new Set<string>();
  const stack = [...rootTargets];
  while (stack.length > 0) {
    const name = stack.pop()!;
    if (reachable.has(name)) continue;
    reachable.add(name);
    for (const target of edges.get(name) ?? []) stack.push(target);
  }

  const dead = blocks.filter((block) => !reachable.has(block.name));
  if (dead.length === 0) return { ops: [...ops], applied: 0, optimizations: [] };

  const remove = new Set<number>();
  for (const block of dead) {
    for (let index = block.start; index < block.end; index += 1) remove.add(index);
  }
  const result = ops.filter((_, index) => !remove.has(index));
  return {
    ops: result,
    applied: remove.size,
    optimizations: [{
      name: "dead-proc-elimination",
      detail: `Removed ${dead.length} unreferenced emitted rule proc(s) after IR optimization.`,
    }],
  };
};

export const deadProcElimination: IrPass = {
  name: "dead-proc-elimination",
  run,
  layoutSafe: false,
};
