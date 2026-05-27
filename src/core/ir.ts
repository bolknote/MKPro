import { getOpcode, registerIndex } from "./opcodes.ts";
import type {
  CellRole,
  IrCondition,
  IrLoopCounter,
  IrMeta,
  IrOp,
  IrStopSemantic,
  IrTargetMeta,
  LayoutIrCell,
  MachineItem,
  MachineOp,
  RegisterName,
} from "./types.ts";

const REGISTERS_BY_INDEX: RegisterName[] = [
  "0",
  "1",
  "2",
  "3",
  "4",
  "5",
  "6",
  "7",
  "8",
  "9",
  "a",
  "b",
  "c",
  "d",
  "e",
];

const DIRECT_STORE_BASE = 0x40;
const DIRECT_RECALL_BASE = 0x60;
const INDIRECT_JUMP_BASE = 0x80;
const INDIRECT_CALL_BASE = 0xa0;
const INDIRECT_STORE_BASE = 0xb0;
const INDIRECT_RECALL_BASE = 0xd0;

const INDIRECT_COND_BASES: Record<number, IrCondition> = {
  0x70: "!=0",
  0x90: ">=0",
  0xc0: "<0",
  0xe0: "==0",
};

const COND_OPCODES: Record<number, IrCondition> = {
  0x57: "!=0",
  0x59: ">=0",
  0x5c: "<0",
  0x5e: "==0",
};

const LOOP_OPCODES: Record<number, IrLoopCounter> = {
  0x5d: "L0",
  0x5b: "L1",
  0x58: "L2",
  0x5a: "L3",
};

const TAKES_ADDRESS = new Set<number>([
  0x51,
  0x53,
  0x57,
  0x58,
  0x59,
  0x5a,
  0x5b,
  0x5c,
  0x5d,
  0x5e,
]);

function metaFromOp(op: MachineOp): IrMeta {
  const meta: IrMeta = { mnemonic: op.mnemonic };
  if (op.comment !== undefined) meta.comment = op.comment;
  if (op.sourceLine !== undefined) meta.sourceLine = op.sourceLine;
  if (op.raw === true) meta.raw = true;
  return meta;
}

function targetMetaFromAddress(item: Extract<MachineItem, { kind: "address" }>): IrTargetMeta {
  const meta: IrTargetMeta = {};
  if (item.comment !== undefined) meta.comment = item.comment;
  if (item.sourceLine !== undefined) meta.sourceLine = item.sourceLine;
  return meta;
}

function isInRange(opcode: number, base: number): boolean {
  return opcode >= base && opcode <= base + 0xe;
}

function registerForOffset(opcode: number, base: number): RegisterName {
  return REGISTERS_BY_INDEX[opcode - base]!;
}

function stopSemanticFromComment(comment: string | undefined): IrStopSemantic {
  if (comment === undefined) return "unknown";
  const lower = comment.toLowerCase();
  if (lower.startsWith("halt")) return "halt";
  if (lower.startsWith("pause")) return "pause";
  if (lower.startsWith("show")) return "show";
  if (lower.startsWith("ask")) return "ask";
  if (lower.startsWith("input") || lower.startsWith("read")) return "input";
  if (lower.startsWith("implicit final stop")) return "halt";
  if (lower.includes("implicit stop")) return "halt";
  return "unknown";
}

export function raiseMachineToIr(items: readonly MachineItem[]): IrOp[] {
  const result: IrOp[] = [];
  for (let i = 0; i < items.length; i += 1) {
    const item = items[i]!;
    if (item.kind === "label") {
      result.push({ kind: "label", name: item.name });
      continue;
    }
    if (item.kind === "address") {
      result.push({
        kind: "orphan-address",
        target: item.target,
        meta: targetMetaFromAddress(item),
      });
      continue;
    }
    const meta = metaFromOp(item);
    const opcode = item.opcode;
    if (TAKES_ADDRESS.has(opcode)) {
      const next = items[i + 1];
      if (next?.kind !== "address") {
        result.push({ kind: "plain", opcode, meta });
        continue;
      }
      const target = next.target;
      const tmeta = targetMetaFromAddress(next);
      i += 1;
      if (opcode === 0x51) {
        result.push({ kind: "jump", target, opcode, meta, targetMeta: tmeta });
        continue;
      }
      if (opcode === 0x53) {
        result.push({ kind: "call", target, opcode, meta, targetMeta: tmeta });
        continue;
      }
      const condition = COND_OPCODES[opcode];
      if (condition !== undefined) {
        result.push({ kind: "cjump", condition, target, opcode, meta, targetMeta: tmeta });
        continue;
      }
      const loop = LOOP_OPCODES[opcode];
      if (loop !== undefined) {
        result.push({ kind: "loop", counter: loop, target, opcode, meta, targetMeta: tmeta });
        continue;
      }
      result.push({ kind: "plain", opcode, meta });
      continue;
    }
    if (isInRange(opcode, DIRECT_STORE_BASE)) {
      result.push({
        kind: "store",
        register: registerForOffset(opcode, DIRECT_STORE_BASE),
        opcode,
        meta,
      });
      continue;
    }
    if (isInRange(opcode, DIRECT_RECALL_BASE)) {
      result.push({
        kind: "recall",
        register: registerForOffset(opcode, DIRECT_RECALL_BASE),
        opcode,
        meta,
      });
      continue;
    }
    if (isInRange(opcode, INDIRECT_STORE_BASE)) {
      result.push({
        kind: "indirect-store",
        register: registerForOffset(opcode, INDIRECT_STORE_BASE),
        opcode,
        meta,
      });
      continue;
    }
    if (isInRange(opcode, INDIRECT_RECALL_BASE)) {
      result.push({
        kind: "indirect-recall",
        register: registerForOffset(opcode, INDIRECT_RECALL_BASE),
        opcode,
        meta,
      });
      continue;
    }
    if (isInRange(opcode, INDIRECT_JUMP_BASE)) {
      result.push({
        kind: "indirect-jump",
        register: registerForOffset(opcode, INDIRECT_JUMP_BASE),
        opcode,
        meta,
      });
      continue;
    }
    if (isInRange(opcode, INDIRECT_CALL_BASE)) {
      result.push({
        kind: "indirect-call",
        register: registerForOffset(opcode, INDIRECT_CALL_BASE),
        opcode,
        meta,
      });
      continue;
    }
    const indirectCondBase = Object.keys(INDIRECT_COND_BASES)
      .map((value) => Number(value))
      .find((base) => isInRange(opcode, base));
    if (indirectCondBase !== undefined) {
      result.push({
        kind: "indirect-cjump",
        condition: INDIRECT_COND_BASES[indirectCondBase]!,
        register: registerForOffset(opcode, indirectCondBase),
        opcode,
        meta,
      });
      continue;
    }
    if (opcode === 0x52) {
      result.push({ kind: "return", opcode, meta });
      continue;
    }
    if (opcode === 0x50) {
      result.push({
        kind: "stop",
        opcode,
        semantic: stopSemanticFromComment(item.comment),
        meta,
      });
      continue;
    }
    result.push({ kind: "plain", opcode, meta });
  }
  return result;
}

function machineOpFromMeta(opcode: number, meta: IrMeta): MachineOp {
  const op: MachineOp = {
    kind: "op",
    opcode,
    mnemonic: meta.mnemonic,
  };
  if (meta.comment !== undefined) op.comment = meta.comment;
  if (meta.sourceLine !== undefined) op.sourceLine = meta.sourceLine;
  if (meta.raw === true) op.raw = true;
  return op;
}

function machineAddressFromMeta(target: string | number, meta: IrTargetMeta): MachineItem {
  const ref: Extract<MachineItem, { kind: "address" }> = { kind: "address", target };
  if (meta.comment !== undefined) ref.comment = meta.comment;
  if (meta.sourceLine !== undefined) ref.sourceLine = meta.sourceLine;
  return ref;
}

export function lowerIrToMachine(ops: readonly IrOp[]): MachineItem[] {
  const result: MachineItem[] = [];
  for (const op of ops) {
    switch (op.kind) {
      case "label":
        result.push({ kind: "label", name: op.name });
        break;
      case "orphan-address":
        result.push(machineAddressFromMeta(op.target, op.meta));
        break;
      case "store":
      case "recall":
      case "indirect-store":
      case "indirect-recall":
      case "indirect-jump":
      case "indirect-call":
      case "indirect-cjump":
      case "return":
      case "stop":
      case "plain":
        result.push(machineOpFromMeta(op.opcode, op.meta));
        break;
      case "jump":
      case "cjump":
      case "call":
      case "loop":
        result.push(machineOpFromMeta(op.opcode, op.meta));
        result.push(machineAddressFromMeta(op.target, op.targetMeta));
        break;
    }
  }
  return result;
}

export function makeStore(
  register: RegisterName,
  meta: IrMeta = { mnemonic: `X->П ${register}` },
): IrOp {
  const opcode = DIRECT_STORE_BASE + registerIndex(register);
  const resolved: IrMeta = { ...meta, mnemonic: meta.mnemonic ?? getOpcode(opcode).name };
  return { kind: "store", register, opcode, meta: resolved };
}

export function makeRecall(
  register: RegisterName,
  meta: IrMeta = { mnemonic: `П->X ${register}` },
): IrOp {
  const opcode = DIRECT_RECALL_BASE + registerIndex(register);
  const resolved: IrMeta = { ...meta, mnemonic: meta.mnemonic ?? getOpcode(opcode).name };
  return { kind: "recall", register, opcode, meta: resolved };
}

export function raiseLayoutToIr(cells: readonly LayoutIrCell[]): IrOp[] {
  const result: IrOp[] = [];
  for (let i = 0; i < cells.length; i += 1) {
    const cell = cells[i]!;
    const info = getOpcode(cell.opcode);
    const meta: IrMeta = { mnemonic: info.name };
    if (cell.roles.length > 0) meta.roles = [...cell.roles];
    if (cell.tactic !== undefined) meta.tactic = cell.tactic;
    if (TAKES_ADDRESS.has(cell.opcode)) {
      const next = cells[i + 1];
      if (next === undefined || !next.roles.includes("address")) {
        result.push({ kind: "plain", opcode: cell.opcode, meta });
        continue;
      }
      const target = next.opcode;
      const targetMeta: IrTargetMeta = {};
      if (next.tactic !== undefined && next.tactic !== "") {
        targetMeta.comment = next.tactic;
      }
      if (next.roles.length > 0) targetMeta.roles = [...next.roles];
      i += 1;
      if (cell.opcode === 0x51) {
        result.push({ kind: "jump", target, opcode: cell.opcode, meta, targetMeta });
        continue;
      }
      if (cell.opcode === 0x53) {
        result.push({ kind: "call", target, opcode: cell.opcode, meta, targetMeta });
        continue;
      }
      const condition = COND_OPCODES[cell.opcode];
      if (condition !== undefined) {
        result.push({ kind: "cjump", condition, target, opcode: cell.opcode, meta, targetMeta });
        continue;
      }
      const loop = LOOP_OPCODES[cell.opcode];
      if (loop !== undefined) {
        result.push({ kind: "loop", counter: loop, target, opcode: cell.opcode, meta, targetMeta });
        continue;
      }
      result.push({ kind: "plain", opcode: cell.opcode, meta });
      continue;
    }
    if (isInRange(cell.opcode, DIRECT_STORE_BASE)) {
      result.push({
        kind: "store",
        register: registerForOffset(cell.opcode, DIRECT_STORE_BASE),
        opcode: cell.opcode,
        meta,
      });
      continue;
    }
    if (isInRange(cell.opcode, DIRECT_RECALL_BASE)) {
      result.push({
        kind: "recall",
        register: registerForOffset(cell.opcode, DIRECT_RECALL_BASE),
        opcode: cell.opcode,
        meta,
      });
      continue;
    }
    if (isInRange(cell.opcode, INDIRECT_STORE_BASE)) {
      result.push({
        kind: "indirect-store",
        register: registerForOffset(cell.opcode, INDIRECT_STORE_BASE),
        opcode: cell.opcode,
        meta,
      });
      continue;
    }
    if (isInRange(cell.opcode, INDIRECT_RECALL_BASE)) {
      result.push({
        kind: "indirect-recall",
        register: registerForOffset(cell.opcode, INDIRECT_RECALL_BASE),
        opcode: cell.opcode,
        meta,
      });
      continue;
    }
    if (isInRange(cell.opcode, INDIRECT_JUMP_BASE)) {
      result.push({
        kind: "indirect-jump",
        register: registerForOffset(cell.opcode, INDIRECT_JUMP_BASE),
        opcode: cell.opcode,
        meta,
      });
      continue;
    }
    if (isInRange(cell.opcode, INDIRECT_CALL_BASE)) {
      result.push({
        kind: "indirect-call",
        register: registerForOffset(cell.opcode, INDIRECT_CALL_BASE),
        opcode: cell.opcode,
        meta,
      });
      continue;
    }
    const indirectCondBase = Object.keys(INDIRECT_COND_BASES)
      .map((value) => Number(value))
      .find((base) => isInRange(cell.opcode, base));
    if (indirectCondBase !== undefined) {
      result.push({
        kind: "indirect-cjump",
        condition: INDIRECT_COND_BASES[indirectCondBase]!,
        register: registerForOffset(cell.opcode, indirectCondBase),
        opcode: cell.opcode,
        meta,
      });
      continue;
    }
    if (cell.opcode === 0x52) {
      result.push({ kind: "return", opcode: cell.opcode, meta });
      continue;
    }
    if (cell.opcode === 0x50) {
      result.push({
        kind: "stop",
        opcode: cell.opcode,
        semantic: stopSemanticFromComment(cell.tactic),
        meta,
      });
      continue;
    }
    result.push({ kind: "plain", opcode: cell.opcode, meta });
  }
  return result;
}

interface LowerLayoutResult {
  cells: LayoutIrCell[];
  addressOfLabel: Map<string, number>;
}

export function lowerIrToLayout(
  ops: readonly IrOp[],
  options: { defaultTactic?: string } = {},
): LowerLayoutResult {
  const cells: LayoutIrCell[] = [];
  const addressOfLabel = new Map<string, number>();
  let address = 0;
  const fallbackTactic = options.defaultTactic ?? "";
  for (const op of ops) {
    if (op.kind === "label") {
      addressOfLabel.set(op.name, address);
      continue;
    }
    if (op.kind === "orphan-address") {
      cells.push({
        address,
        opcode: typeof op.target === "number" ? op.target : 0,
        roles: op.meta.roles ? [...op.meta.roles] : ["address"],
        tactic: op.meta.comment ?? fallbackTactic,
      });
      address += 1;
      continue;
    }
    const opcode = op.opcode;
    const tactic = op.meta.tactic ?? op.meta.comment ?? fallbackTactic;
    const roles: CellRole[] = op.meta.roles ? [...op.meta.roles] : ["exec"];
    cells.push({ address, opcode, roles, tactic });
    address += 1;
    if (op.kind === "jump" || op.kind === "cjump" || op.kind === "call" || op.kind === "loop") {
      const targetValue = typeof op.target === "number" ? op.target : addressOfLabel.get(op.target) ?? 0;
      const targetRoles: CellRole[] = op.targetMeta.roles
        ? [...op.targetMeta.roles]
        : ["address"];
      cells.push({
        address,
        opcode: targetValue,
        roles: targetRoles,
        tactic: op.targetMeta.comment ?? tactic,
      });
      address += 1;
    }
  }
  return { cells, addressOfLabel };
}

export function machineItemsEqual(a: MachineItem, b: MachineItem): boolean {
  if (a.kind !== b.kind) return false;
  if (a.kind === "label" && b.kind === "label") return a.name === b.name;
  if (a.kind === "op" && b.kind === "op") {
    return (
      a.opcode === b.opcode &&
      a.mnemonic === b.mnemonic &&
      (a.comment ?? undefined) === (b.comment ?? undefined) &&
      (a.sourceLine ?? undefined) === (b.sourceLine ?? undefined) &&
      (a.raw ?? false) === (b.raw ?? false)
    );
  }
  if (a.kind === "address" && b.kind === "address") {
    return (
      a.target === b.target &&
      (a.comment ?? undefined) === (b.comment ?? undefined) &&
      (a.sourceLine ?? undefined) === (b.sourceLine ?? undefined)
    );
  }
  return false;
}
