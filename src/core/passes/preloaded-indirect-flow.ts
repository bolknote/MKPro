import {
  evaluateIndirectAddress,
  isStableIndirectSelector,
} from "../indirect-addressing.ts";
import { registerIndex } from "../opcodes.ts";
import type { IrCondition, IrMeta, IrOp, PreloadReport, RegisterName } from "../types.ts";
import {
  calculateLabelAddresses,
  cellsPerOp,
  hasRewriteBarrier,
  targetAddress,
  type IrPass,
  type IrPassFn,
} from "./helpers.ts";

const INDIRECT_COND_BASES: Record<IrCondition, number> = {
  "!=0": 0x70,
  ">=0": 0x90,
  "<0": 0xc0,
  "==0": 0xe0,
};

const STABLE_REGISTERS: RegisterName[] = ["7", "8", "9", "a", "b", "c", "d", "e"];

interface SelectorPlan {
  register: RegisterName;
  selectorValue: string;
  superDark: boolean;
}

function cloneMeta(meta: IrMeta, comment: string): IrMeta {
  return {
    ...meta,
    comment: [meta.comment, comment].filter(Boolean).join("; "),
  };
}

function usedRegisters(ops: readonly IrOp[]): Set<RegisterName> {
  const used = new Set<RegisterName>();
  for (const op of ops) {
    if (
      op.kind === "store" ||
      op.kind === "recall" ||
      op.kind === "indirect-store" ||
      op.kind === "indirect-recall" ||
      op.kind === "indirect-jump" ||
      op.kind === "indirect-call" ||
      op.kind === "indirect-cjump"
    ) {
      used.add(op.register);
    }
  }
  return used;
}

function spareStableRegisters(ops: readonly IrOp[]): RegisterName[] {
  const used = usedRegisters(ops);
  return STABLE_REGISTERS.filter((register) => !used.has(register));
}

function numericFlowTarget(op: IrOp): number | undefined {
  if (op.kind !== "jump" && op.kind !== "call" && op.kind !== "cjump" && op.kind !== "loop") return undefined;
  if (typeof op.target !== "number") return undefined;
  if (!Number.isInteger(op.target) || op.target < 0 || op.target > 104) return undefined;
  return op.target;
}

function branchTarget(op: IrOp): number | undefined {
  if (op.kind !== "jump" && op.kind !== "call" && op.kind !== "cjump") return undefined;
  if (op.targetMeta.formalOpcode !== undefined || op.targetMeta.roles?.includes("formal-address")) return undefined;
  return numericFlowTarget(op);
}

function maxNumericFlowTarget(ops: readonly IrOp[]): number {
  let max = -1;
  for (const op of ops) {
    const target = numericFlowTarget(op);
    if (target !== undefined && target > max) max = target;
  }
  return max;
}

function addressByIndex(ops: readonly IrOp[]): number[] {
  const addresses: number[] = [];
  let address = 0;
  for (const op of ops) {
    addresses.push(address);
    address += cellsPerOp(op);
  }
  return addresses;
}

function opAtAddress(
  ops: readonly IrOp[],
  addresses: readonly number[],
  address: number,
): IrOp | undefined {
  for (let i = 0; i < ops.length; i += 1) {
    const op = ops[i]!;
    if (cellsPerOp(op) === 0) continue;
    if (addresses[i] === address) return op;
  }
  return undefined;
}

function hasSingleCellFallthrough(op: IrOp | undefined): boolean {
  if (op === undefined || cellsPerOp(op) !== 1) return false;
  switch (op.kind) {
    case "plain":
    case "store":
    case "recall":
    case "indirect-store":
    case "indirect-recall":
    case "stop":
      return true;
    default:
      return false;
  }
}

function isSuperDarkCompatibleTarget(
  ops: readonly IrOp[],
  addresses: readonly number[],
  labels: ReadonlyMap<string, number>,
  target: number,
): boolean {
  if (target < 48 || target > 53) return false;
  const entry = opAtAddress(ops, addresses, target);
  if (!hasSingleCellFallthrough(entry)) return false;

  const continuationAddress = target - 47;
  const afterEntry = opAtAddress(ops, addresses, target + 1);
  if (afterEntry?.kind !== "jump") return false;
  return targetAddress(afterEntry.target, labels) === continuationAddress;
}

function selectorForTarget(
  ops: readonly IrOp[],
  addresses: readonly number[],
  labels: ReadonlyMap<string, number>,
  target: number,
): { selectorValue: string; superDark: boolean } {
  if (isSuperDarkCompatibleTarget(ops, addresses, labels, target)) {
    return {
      selectorValue: formalLabelFromOpcode(0xfa + (target - 48)),
      superDark: true,
    };
  }
  if (target <= 47) return { selectorValue: formalLabelFromOrdinal(target + 112), superDark: false };
  return { selectorValue: officialLabel(target), superDark: false };
}

function formalLabelFromOrdinal(ordinal: number): string {
  const high = Math.floor(ordinal / 10);
  const low = ordinal % 10;
  return `${high.toString(16).toUpperCase()}${low.toString(16).toUpperCase()}`;
}

function formalLabelFromOpcode(opcode: number): string {
  const high = Math.floor(opcode / 16);
  const low = opcode % 16;
  return `${high.toString(16).toUpperCase()}${low.toString(16).toUpperCase()}`;
}

function officialLabel(target: number): string {
  if (target <= 99) {
    return `${Math.floor(target / 10)}${target % 10}`;
  }
  return `A${target - 100}`;
}

function indirectFlowOp(
  op: Extract<IrOp, { kind: "jump" | "call" | "cjump" }>,
  register: RegisterName,
  selectorValue: string,
  target: number,
  superDark: boolean,
): IrOp {
  const offset = registerIndex(register);
  const suffix = `preloaded R${register}=${selectorValue} indirect-target=${target}${superDark ? " super-dark" : ""} indirect flow`;
  if (op.kind === "jump") {
    return {
      kind: "indirect-jump",
      register,
      opcode: 0x80 + offset,
      meta: cloneMeta({ ...op.meta, mnemonic: `К БП ${register}` }, suffix),
    };
  }
  if (op.kind === "call") {
    return {
      kind: "indirect-call",
      register,
      opcode: 0xa0 + offset,
      meta: cloneMeta({ ...op.meta, mnemonic: `К ПП ${register}` }, suffix),
    };
  }
  const opcode = INDIRECT_COND_BASES[op.condition] + offset;
  const name = op.condition === "==0"
    ? "x=0"
    : op.condition === "!=0"
      ? "x!=0"
      : `x${op.condition}`;
  return {
    kind: "indirect-cjump",
    condition: op.condition,
    register,
    opcode,
    meta: cloneMeta({ ...op.meta, mnemonic: `К ${name} ${register}` }, suffix),
  };
}

export interface IndirectFlowOptions {
  // When set, the `maxTarget > siteAddress` tail-only guard is dropped. This is
  // ONLY safe for the post-layout driver, which keeps a single rewrite per
  // re-layout and independently re-proves the selector against the fresh
  // address map (any target whose address shifted is rejected). The in-pipeline
  // pass rewrites many sites against one upfront address map, so it must keep
  // the conservative guard.
  relaxMaxTargetGuard?: boolean;
}

export function runPreloadedIndirectFlow(
  ops: readonly IrOp[],
  _context: { options: unknown },
  flowOptions: IndirectFlowOptions = {},
): { ops: IrOp[]; applied: number; optimizations: { name: string; detail: string }[]; preloads?: PreloadReport[] } {
  const registers = spareStableRegisters(ops);
  if (registers.length === 0) return { ops: [...ops], applied: 0, optimizations: [] };

  const addresses = addressByIndex(ops);
  const labels = calculateLabelAddresses(ops);
  const maxTarget = maxNumericFlowTarget(ops);
  const targets = new Map<number, SelectorPlan>();
  const preloads: PreloadReport[] = [];
  const result: IrOp[] = [];
  let applied = 0;
  let superDarkApplied = 0;

  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (hasRewriteBarrier(op) || (op.kind !== "jump" && op.kind !== "call" && op.kind !== "cjump")) {
      result.push(op);
      continue;
    }
    const target = branchTarget(op);
    const siteAddress = addresses[index]!;
    if (
      target === undefined ||
      target > siteAddress ||
      (!flowOptions.relaxMaxTargetGuard && maxTarget > siteAddress)
    ) {
      result.push(op);
      continue;
    }

    let selected = targets.get(target);
    if (selected === undefined) {
      const register = registers.shift();
      if (register === undefined) {
        result.push(op);
        continue;
      }
      const { selectorValue, superDark } = selectorForTarget(ops, addresses, labels, target);
      const evaluated = evaluateIndirectAddress(register, selectorValue, "flow");
      const selectedSuperDark = evaluated?.superDark?.entryAddress === target;
      if (
        evaluated?.actualFlowTarget !== target ||
        selectedSuperDark !== superDark ||
        !isStableIndirectSelector(register)
      ) {
        result.push(op);
        continue;
      }
      selected = { register, selectorValue, superDark };
      targets.set(target, selected);
      preloads.push({ register, value: selectorValue, countsAgainstProgram: false });
    }

    result.push(indirectFlowOp(op, selected.register, selected.selectorValue, target, selected.superDark));
    applied += 1;
    if (selected.superDark) superDarkApplied += 1;
  }

  if (applied === 0) return { ops: [...ops], applied: 0, optimizations: [] };
  const formal = preloads.filter((preload) => /[B-F]/iu.test(preload.value)).length;
  const optimizations = [
    {
      name: "preloaded-indirect-flow",
      detail: `Replaced ${applied} numeric direct branch/call(s) with compiler-owned preloaded indirect flow (${formal} formal alias selector${formal === 1 ? "" : "s"}).`,
    },
  ];
  if (superDarkApplied > 0) {
    optimizations.push({
      name: "preloaded-super-dark-flow",
      detail: `Selected ${superDarkApplied} FA..FF one-command indirect dispatch(es) after proving the entry cell falls through to the matching 01..06 continuation jump.`,
    });
  }
  return {
    ops: result,
    applied,
    preloads,
    optimizations,
  };
}

const run: IrPassFn = (ops, context) => runPreloadedIndirectFlow(ops, context, {});

export const preloadedIndirectFlow: IrPass = {
  name: "preloaded-indirect-flow",
  run,
  layoutSafe: false,
};
