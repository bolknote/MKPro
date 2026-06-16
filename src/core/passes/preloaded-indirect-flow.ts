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
  knownIndirectMemoryTargets,
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
  existingConstant?: boolean;
}

interface EligibleTarget {
  target: number;
  selectorValue: string;
  superDark: boolean;
  indices: number[];
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

function reservedPreloadedRegisters(
  preloaded: Partial<Record<RegisterName, string>> | undefined,
): Set<RegisterName> {
  return new Set(Object.keys(preloaded ?? {}) as RegisterName[]);
}

function spareStableRegisters(ops: readonly IrOp[], reserved: ReadonlySet<RegisterName> = new Set()): RegisterName[] {
  const used = usedRegisters(ops);
  return STABLE_REGISTERS.filter((register) => !used.has(register) && !reserved.has(register));
}

function registerIsOverwritten(ops: readonly IrOp[], register: RegisterName): boolean {
  for (const op of ops) {
    if (op.kind === "store" && op.register === register) return true;
    if (op.kind !== "indirect-store") continue;
    const targets = knownIndirectMemoryTargets(op);
    if (targets === undefined || targets.has(register)) return true;
  }
  return false;
}

function existingConstantSelectorPlans(
  ops: readonly IrOp[],
  preloaded: Partial<Record<RegisterName, string>> | undefined,
): Map<number, SelectorPlan> {
  const plans = new Map<number, SelectorPlan>();
  if (preloaded === undefined) return plans;
  for (const register of STABLE_REGISTERS) {
    const value = preloaded[register];
    if (value === undefined || registerIsOverwritten(ops, register)) continue;
    const evaluated = evaluateIndirectAddress(register, value, "flow");
    if (evaluated === undefined) continue;
    const target = evaluated?.actualFlowTarget;
    if (target === undefined || target < 0 || target > 104) continue;
    if (plans.has(target)) continue;
    plans.set(target, {
      register,
      selectorValue: value,
      superDark: evaluated.superDark?.entryAddress === target,
      existingConstant: true,
    });
  }
  return plans;
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

function runtimeSelectorLiteralOps(target: number, register: RegisterName): IrOp[] {
  const digits = String(target).split("").map((digit): IrOp => ({
    kind: "plain",
    opcode: Number(digit),
    meta: { mnemonic: digit, comment: `runtime indirect call selector ${target}` },
  }));
  return [
    ...digits,
    {
      kind: "store",
      register,
      opcode: 0x40 + registerIndex(register),
      meta: { mnemonic: `X->П ${register}`, comment: `runtime indirect call selector ${target}` },
    },
  ];
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

interface RuntimeCallTarget {
  target: number;
  indices: number[];
  insertIndex: number;
  insertAddress: number;
}

interface RuntimeCallPlan {
  target: number;
  register: RegisterName;
  indices: Set<number>;
  insertIndex: number;
}

function opReferencesRegister(op: IrOp, register: RegisterName): boolean {
  switch (op.kind) {
    case "store":
    case "recall":
    case "indirect-store":
    case "indirect-recall":
    case "indirect-jump":
    case "indirect-call":
    case "indirect-cjump":
      return op.register === register;
    default:
      return false;
  }
}

function firstOpIndexAtAddress(ops: readonly IrOp[], addresses: readonly number[], address: number): number | undefined {
  for (let index = 0; index < ops.length; index += 1) {
    if (cellsPerOp(ops[index]!) === 0) continue;
    if (addresses[index] === address) return index;
  }
  return undefined;
}

function canBorrowRegisterForRuntimeSelector(
  ops: readonly IrOp[],
  register: RegisterName,
  targetIndex: number,
  insertIndex: number,
  selected: ReadonlySet<number>,
): boolean {
  for (let index = targetIndex; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (hasRewriteBarrier(op)) return false;
    if (selected.has(index)) continue;
    if (index >= insertIndex && opReferencesRegister(op, register)) return false;
    if (index < insertIndex && opReferencesRegister(op, register)) return false;
  }
  return true;
}

function runtimeIndirectCallPlans(
  ops: readonly IrOp[],
  breakEven = false,
  reserved: ReadonlySet<RegisterName> = new Set(),
): RuntimeCallPlan[] {
  const addresses = addressByIndex(ops);
  const labels = calculateLabelAddresses(ops);
  const targets = new Map<number, RuntimeCallTarget>();
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (hasRewriteBarrier(op) || op.kind !== "call") continue;
    if (op.targetMeta.formalOpcode !== undefined || op.targetMeta.roles?.includes("formal-address")) continue;
    const target = targetAddress(op.target, labels);
    const siteAddress = addresses[index]!;
    if (
      target === undefined ||
      !Number.isInteger(target) ||
      target < 0 ||
      target > 99 ||
      target >= siteAddress
    ) {
      continue;
    }
    const existing = targets.get(target);
    if (existing === undefined) {
      targets.set(target, {
        target,
        indices: [index],
        insertIndex: index,
        insertAddress: siteAddress,
      });
      continue;
    }
    existing.indices.push(index);
  }

  const plans: RuntimeCallPlan[] = [];
  const usedRegisters = new Set<RegisterName>();
  const sorted = [...targets.values()].sort((left, right) =>
    right.indices.length - left.indices.length ||
    left.insertAddress - right.insertAddress
  );
  for (const candidate of sorted) {
    const preloadCost = String(candidate.target).length + 1;
    // Each rewritten site saves one cell (2-cell direct `ПП addr` -> 1-cell
    // `К ПП r`); the one-time `preloadCost` literal+store pays for the selector
    // register. Break-even is therefore `indices.length > preloadCost`; the
    // default keeps a `+2` margin so marginal rewrites can't perturb the other
    // layout-sensitive indirect-flow passes for a net loss.
    const margin = breakEven ? 0 : 2;
    if (candidate.indices.length <= preloadCost + margin) continue;
    const selected = new Set(candidate.indices);
    const targetIndex = firstOpIndexAtAddress(ops, addresses, candidate.target);
    if (targetIndex === undefined) continue;
    const register = STABLE_REGISTERS.find((item) =>
      !usedRegisters.has(item) &&
      !reserved.has(item) &&
      canBorrowRegisterForRuntimeSelector(ops, item, targetIndex, candidate.insertIndex, selected)
    );
    if (register === undefined) continue;
    usedRegisters.add(register);
    plans.push({
      target: candidate.target,
      register,
      indices: selected,
      insertIndex: candidate.insertIndex,
    });
  }
  return plans;
}

const runtimeIndirectCallRun: IrPassFn = (ops, context) => {
  const plans = runtimeIndirectCallPlans(
    ops,
    context.options.aggressiveIndirectCallThreshold === true,
    reservedPreloadedRegisters(context.options.preloadedConstantRegisters),
  );
  if (plans.length === 0) return { ops: [...ops], applied: 0, optimizations: [] };
  const byInsert = new Map<number, RuntimeCallPlan[]>();
  const byIndex = new Map<number, RuntimeCallPlan>();
  for (const plan of plans) {
    const existing = byInsert.get(plan.insertIndex);
    if (existing === undefined) byInsert.set(plan.insertIndex, [plan]);
    else existing.push(plan);
    for (const index of plan.indices) byIndex.set(index, plan);
  }

  const result: IrOp[] = [];
  let rewritten = 0;
  for (let index = 0; index < ops.length; index += 1) {
    for (const plan of byInsert.get(index) ?? []) {
      result.push(...runtimeSelectorLiteralOps(plan.target, plan.register));
    }
    const plan = byIndex.get(index);
    const op = ops[index]!;
    if (plan !== undefined && op.kind === "call") {
      result.push(indirectFlowOp(op, plan.register, String(plan.target), plan.target, false));
      rewritten += 1;
      continue;
    }
    result.push(op);
  }

  return {
    ops: result,
    applied: rewritten,
    optimizations: [
      {
        name: "runtime-indirect-call-flow",
        detail: `Borrowed dead stable register(s) for ${rewritten} repeated direct helper call(s).`,
      },
    ],
  };
};

export function runPreloadedIndirectFlow(
  ops: readonly IrOp[],
  context: { options: { preloadedConstantRegisters?: Partial<Record<RegisterName, string>>; dualUseConstantIndirectFlow?: boolean } },
  flowOptions: IndirectFlowOptions = {},
): { ops: IrOp[]; applied: number; optimizations: { name: string; detail: string }[]; preloads?: PreloadReport[] } {
  const reserved = reservedPreloadedRegisters(context.options.preloadedConstantRegisters);
  const registers = spareStableRegisters(ops, reserved);
  const existingSelectors = context.options.dualUseConstantIndirectFlow === true
    ? existingConstantSelectorPlans(ops, context.options.preloadedConstantRegisters)
    : new Map<number, SelectorPlan>();
  if (registers.length === 0 && existingSelectors.size === 0) return { ops: [...ops], applied: 0, optimizations: [] };

  const addresses = addressByIndex(ops);
  const labels = calculateLabelAddresses(ops);
  const maxTarget = maxNumericFlowTarget(ops);
  const eligibleTargets = new Map<number, EligibleTarget>();
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (hasRewriteBarrier(op) || (op.kind !== "jump" && op.kind !== "call" && op.kind !== "cjump")) continue;
    const target = branchTarget(op);
    const siteAddress = addresses[index]!;
    if (
      target === undefined ||
      target > siteAddress ||
      (!flowOptions.relaxMaxTargetGuard && maxTarget > siteAddress)
    ) {
      continue;
    }

    const { selectorValue, superDark } = selectorForTarget(ops, addresses, labels, target);
    const existingSelector = existingSelectors.get(target);
    const evaluated = registers[0] === undefined
      ? undefined
      : evaluateIndirectAddress(registers[0], selectorValue, "flow");
    const selectedSuperDark = evaluated?.superDark?.entryAddress === target;
    if (
      existingSelector === undefined &&
      (
        evaluated?.actualFlowTarget !== target ||
        selectedSuperDark !== superDark
      )
    ) {
      continue;
    }

    const existing = eligibleTargets.get(target);
    if (existing !== undefined) {
      existing.indices.push(index);
    } else {
      eligibleTargets.set(target, { target, selectorValue, superDark, indices: [index] });
    }
  }

  const targets = new Map<number, SelectorPlan>();
  const preloads: PreloadReport[] = [];
  let reusedExistingConstants = 0;
  const usedExistingRegisters = new Set<RegisterName>();
  const sortedTargets = [...eligibleTargets.values()].sort((left, right) =>
    right.indices.length - left.indices.length ||
    left.indices[0]! - right.indices[0]!
  );
  for (const target of sortedTargets) {
    const existingSelector = existingSelectors.get(target.target);
    if (existingSelector !== undefined && !usedExistingRegisters.has(existingSelector.register)) {
      targets.set(target.target, existingSelector);
      usedExistingRegisters.add(existingSelector.register);
      reusedExistingConstants += 1;
      continue;
    }
    const register = registers.shift();
    if (register === undefined) break;
    if (!isStableIndirectSelector(register)) continue;
    targets.set(target.target, {
      register,
      selectorValue: target.selectorValue,
      superDark: target.superDark,
    });
    preloads.push({ register, value: target.selectorValue, countsAgainstProgram: false });
  }

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

    const selected = targets.get(target);
    if (selected === undefined) {
      result.push(op);
      continue;
    }

    result.push(indirectFlowOp(op, selected.register, selected.selectorValue, target, selected.superDark));
    applied += 1;
    if (selected.superDark) superDarkApplied += 1;
  }

  if (applied === 0) return { ops: [...ops], applied: 0, optimizations: [] };
  const formal = preloads.filter((preload) => /[B-F]/iu.test(preload.value)).length;
  const reused = reusedExistingConstants === 0
    ? ""
    : ` and reused ${reusedExistingConstants} existing constant selector${reusedExistingConstants === 1 ? "" : "s"}`;
  const optimizations = [
    {
      name: "preloaded-indirect-flow",
      detail: `Replaced ${applied} numeric direct branch/call(s) with compiler-owned preloaded indirect flow (${formal} formal alias selector${formal === 1 ? "" : "s"})${reused}.`,
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

export const runtimeIndirectCallFlow: IrPass = {
  name: "runtime-indirect-call-flow",
  run: runtimeIndirectCallRun,
  layoutSafe: false,
};
