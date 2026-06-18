import { formalAddressInfo } from "./formal-address.ts";
import { evaluateIndirectAddress } from "./indirect-addressing.ts";
import { lowerIrToMachine, raiseMachineToIr } from "./ir.ts";
import { addressToOpcode } from "./opcodes.ts";
import { cellsPerOp, hasRewriteBarrier } from "./passes/helpers.ts";
import { computeLiveness } from "./passes/liveness-analysis.ts";
import { runPreloadedIndirectFlow } from "./passes/preloaded-indirect-flow.ts";
import type {
  AppliedOptimization,
  CompileOptions,
  IrMeta,
  IrOp,
  MachineItem,
  PreloadReport,
  RegisterName,
} from "./types.ts";

export interface PostLayoutIndirectFlowResult {
  items: MachineItem[];
  preloads: PreloadReport[];
  optimizations: AppliedOptimization[];
  applied: number;
}

interface MachineCell {
  itemIndex: number;
  address: number;
  item: Exclude<MachineItem, { kind: "label" }>;
}

type DirectBranch = Extract<IrOp, { kind: "jump" | "call" | "cjump" }>;
type IndirectBranch = Extract<IrOp, { kind: "indirect-jump" | "indirect-call" | "indirect-cjump" }>;
type R0FlowBranch = Extract<IrOp, { kind: "jump" | "call" | "cjump" }>;

const STABLE_REGISTERS: RegisterName[] = ["7", "8", "9", "a", "b", "c", "d", "e"];

const INDIRECT_COND_BASES = {
  "!=0": 0x70,
  ">=0": 0x90,
  "<0": 0xc0,
  "==0": 0xe0,
} as const;

const ADDRESS_TAKING_OPCODES = new Set([
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

function isDirectBranch(op: IrOp): op is DirectBranch {
  return op.kind === "jump" || op.kind === "call" || op.kind === "cjump";
}

function isIndirectBranch(op: IrOp): op is IndirectBranch {
  return op.kind === "indirect-jump" || op.kind === "indirect-call" || op.kind === "indirect-cjump";
}

function isR0FlowBranch(op: IrOp): op is R0FlowBranch {
  return op.kind === "jump" || op.kind === "call" || op.kind === "cjump";
}

// MK-61 register operand encoding: R0..R9 -> 0..9, RA..RE -> 10..14.
function registerIndex(register: RegisterName): number {
  return /^[0-9]$/u.test(register) ? Number(register) : register.toLowerCase().charCodeAt(0) - 87;
}

function labelAddresses(ops: readonly IrOp[]): Map<string, number> {
  const map = new Map<string, number>();
  let address = 0;
  for (const op of ops) {
    if (op.kind === "label") {
      map.set(op.name, address);
      continue;
    }
    address += cellsPerOp(op);
  }
  return map;
}

function cellCount(items: readonly MachineItem[]): number {
  return items.filter((item) => item.kind !== "label").length;
}

function machineLabelAddresses(items: readonly MachineItem[]): Map<string, number> {
  const labels = new Map<string, number>();
  let address = 0;
  for (const item of items) {
    if (item.kind === "label") {
      labels.set(item.name, address);
    } else {
      address += 1;
    }
  }
  return labels;
}

function machineCells(items: readonly MachineItem[]): MachineCell[] {
  const cells: MachineCell[] = [];
  let address = 0;
  for (let itemIndex = 0; itemIndex < items.length; itemIndex += 1) {
    const item = items[itemIndex]!;
    if (item.kind === "label") continue;
    cells.push({ itemIndex, address, item });
    address += 1;
  }
  return cells;
}

function machineLabelsByAddress(items: readonly MachineItem[]): Map<number, string[]> {
  const labels = new Map<number, string[]>();
  let address = 0;
  for (const item of items) {
    if (item.kind === "label") {
      const list = labels.get(address) ?? [];
      list.push(item.name);
      labels.set(address, list);
    } else {
      address += 1;
    }
  }
  return labels;
}

function machineCellAt(cells: readonly MachineCell[], address: number): MachineCell | undefined {
  return cells.find((cell) => cell.address === address);
}

function referencedMachineLabels(items: readonly MachineItem[]): Set<string> {
  const referenced = new Set<string>();
  for (const item of items) {
    if (item.kind === "address" && typeof item.target === "string") referenced.add(item.target);
  }
  return referenced;
}

function addressHasReferencedLabel(
  labels: ReadonlyMap<number, readonly string[]>,
  referenced: ReadonlySet<string>,
  address: number,
): boolean {
  return labels.get(address)?.some((label) => referenced.has(label)) === true;
}

function machineAddressByItemIndex(items: readonly MachineItem[]): Map<number, number> {
  const addresses = new Map<number, number>();
  let address = 0;
  for (let itemIndex = 0; itemIndex < items.length; itemIndex += 1) {
    if (items[itemIndex]?.kind === "label") continue;
    addresses.set(itemIndex, address);
    address += 1;
  }
  return addresses;
}

function opAddresses(ops: readonly IrOp[]): number[] {
  const addresses: number[] = [];
  let address = 0;
  for (const op of ops) {
    addresses.push(address);
    address += cellsPerOp(op);
  }
  return addresses;
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

function spareStableRegister(ops: readonly IrOp[], reserved: ReadonlySet<RegisterName> = new Set()): RegisterName | undefined {
  const used = usedRegisters(ops);
  return STABLE_REGISTERS.find((register) => !used.has(register) && !reserved.has(register));
}

function preloadRegisterMap(preloads: readonly PreloadReport[]): Partial<Record<RegisterName, string>> {
  const result: Partial<Record<RegisterName, string>> = {};
  for (const preload of preloads) result[preload.register as RegisterName] = preload.value;
  return result;
}

function optionsWithReservedPreloads(
  options: CompileOptions,
  preloads: readonly PreloadReport[],
): CompileOptions {
  if (preloads.length === 0) return options;
  return {
    ...options,
    preloadedConstantRegisters: {
      ...options.preloadedConstantRegisters,
      ...preloadRegisterMap(preloads),
    },
  };
}

function formalLabelFromOrdinal(ordinal: number): string {
  const high = Math.floor(ordinal / 10);
  const low = ordinal % 10;
  return `${high.toString(16).toUpperCase()}${low.toString(16).toUpperCase()}`;
}

function officialLabel(target: number): string {
  if (target <= 99) {
    return `${Math.floor(target / 10)}${target % 10}`;
  }
  return `A${target - 100}`;
}

function addressOpcodeForItem(items: readonly MachineItem[], item: Extract<MachineItem, { kind: "address" }>): number | undefined {
  if (item.formalOpcode !== undefined) return item.formalOpcode;
  const target = typeof item.target === "number" ? item.target : machineLabelAddresses(items).get(item.target);
  if (target === undefined) return undefined;
  try {
    return addressToOpcode(target);
  } catch {
    return undefined;
  }
}

interface MachineLayout {
  labels: Map<string, number>;
  itemIndexByAddress: Map<number, number>;
  addressByItemIndex: Map<number, number>;
}

function machineLayout(items: readonly MachineItem[]): MachineLayout {
  const labels = new Map<string, number>();
  const itemIndexByAddress = new Map<number, number>();
  const addressByItemIndex = new Map<number, number>();
  let address = 0;
  for (let index = 0; index < items.length; index += 1) {
    const item = items[index]!;
    if (item.kind === "label") {
      labels.set(item.name, address);
    } else {
      itemIndexByAddress.set(address, index);
      addressByItemIndex.set(index, address);
      address += 1;
    }
  }
  return { labels, itemIndexByAddress, addressByItemIndex };
}

function resolvedMachineTarget(
  target: string | number,
  labels: ReadonlyMap<string, number>,
): number | undefined {
  return typeof target === "number" ? target : labels.get(target);
}

function selectorForActualTarget(target: number): string | undefined {
  if (!Number.isInteger(target) || target < 0 || target > 104) return undefined;
  if (target <= 47) return formalLabelFromOrdinal(target + 112);
  return officialLabel(target);
}

function retargetedSelectorValue(
  register: RegisterName,
  previousValue: string,
  shiftedTarget: number,
): string | undefined {
  const fractional = fractionalSelectorSuffix(previousValue);
  if (fractional !== undefined) {
    const candidate = `${shiftedTarget}${fractional}`;
    if (evaluateIndirectAddress(register, candidate, "flow")?.actualFlowTarget === shiftedTarget) {
      return candidate;
    }
  }
  return selectorForActualTarget(shiftedTarget);
}

function fractionalSelectorSuffix(value: string): string | undefined {
  const match = /^(\d+)(\.\d+)$/u.exec(value.trim());
  if (match === null || match[1] === "0") return undefined;
  return match?.[2];
}

function indirectFlowOp(
  op: DirectBranch,
  register: RegisterName,
  selectorValue: string,
  target: number,
): IndirectBranch {
  const offset = registerIndex(register);
  const suffix = `preloaded R${register}=${selectorValue} indirect-target=${target} shifted-forward indirect flow`;
  if (op.kind === "jump") {
    return {
      kind: "indirect-jump",
      register,
      opcode: 0x80 + offset,
      meta: { ...op.meta, mnemonic: `К БП ${register}`, comment: [op.meta.comment, suffix].filter(Boolean).join("; ") },
    };
  }
  if (op.kind === "call") {
    return {
      kind: "indirect-call",
      register,
      opcode: 0xa0 + offset,
      meta: { ...op.meta, mnemonic: `К ПП ${register}`, comment: [op.meta.comment, suffix].filter(Boolean).join("; ") },
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
    meta: { ...op.meta, mnemonic: `К ${name} ${register}`, comment: [op.meta.comment, suffix].filter(Boolean).join("; ") },
  };
}

function cloneMeta(meta: IrMeta, comment: string): IrMeta {
  return {
    ...meta,
    comment: [meta.comment, comment].filter(Boolean).join("; "),
  };
}

function fractionalR0FlowOp(op: R0FlowBranch): IndirectBranch {
  if (op.kind === "jump") {
    return {
      kind: "indirect-jump",
      register: "0",
      opcode: 0x80,
      meta: cloneMeta({ ...op.meta, mnemonic: "К БП 0" }, "post-layout fractional R0 flow to 99"),
    };
  }
  if (op.kind === "call") {
    return {
      kind: "indirect-call",
      register: "0",
      opcode: 0xa0,
      meta: cloneMeta({ ...op.meta, mnemonic: "К ПП 0" }, "post-layout fractional R0 call to 99"),
    };
  }
  const opcode = INDIRECT_COND_BASES[op.condition];
  const name = op.condition === "==0"
    ? "x=0"
    : op.condition === "!=0"
      ? "x!=0"
      : `x${op.condition}`;
  return {
    kind: "indirect-cjump",
    condition: op.condition,
    register: "0",
    opcode,
    meta: cloneMeta({ ...op.meta, mnemonic: `К ${name} 0` }, "post-layout fractional R0 conditional flow to 99"),
  };
}

function isFractionalR0LiteralBeforeStore(ops: readonly IrOp[], storeIndex: number): boolean {
  let index = storeIndex - 1;
  let hasNonZeroFractionDigit = false;
  while (index >= 0) {
    const digit = ops[index];
    if (digit?.kind !== "plain" || digit.opcode < 0x00 || digit.opcode > 0x09) break;
    if (digit.opcode > 0) hasNonZeroFractionDigit = true;
    index -= 1;
  }
  const dot = ops[index];
  const zero = ops[index - 1];
  if (!hasNonZeroFractionDigit || dot?.kind !== "plain" || dot.opcode !== 0x0a) return false;
  if (zero === undefined) return true;
  return zero.kind === "plain" && zero.opcode === 0x00;
}

function preservesFractionalR0Fact(op: IrOp): boolean {
  return op.kind === "plain" ||
    op.kind === "recall" ||
    op.kind === "label" ||
    (op.kind === "store" && op.register !== "0") ||
    (op.kind === "indirect-recall" && op.register !== "0") ||
    (op.kind === "indirect-store" && op.register !== "0");
}

function targetAddressAfterReplacingOp(
  ops: readonly IrOp[],
  replaceIndex: number,
  targetLabel: string,
): number | undefined {
  let address = 0;
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") {
      if (op.name === targetLabel) return address;
      continue;
    }
    address += index === replaceIndex ? 1 : cellsPerOp(op);
  }
  return undefined;
}

function findFractionalR0FlowRewrite(ops: readonly IrOp[]): { index: number; op: IndirectBranch } | undefined {
  const liveness = computeLiveness(ops);
  let r0Fractional = false;

  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (hasRewriteBarrier(op)) {
      r0Fractional = false;
      continue;
    }
    if (op.kind === "store" && op.register === "0") {
      r0Fractional = isFractionalR0LiteralBeforeStore(ops, index);
      continue;
    }
    if (preservesFractionalR0Fact(op)) continue;

    if (r0Fractional && isR0FlowBranch(op) && !liveness.liveOut[index]!.has("0")) {
      const finalTarget = typeof op.target === "string"
        ? targetAddressAfterReplacingOp(ops, index, op.target)
        : op.target;
      if (finalTarget === 99) {
        return { index, op: fractionalR0FlowOp(op) };
      }
    }
    r0Fractional = false;
  }

  return undefined;
}

function applyFractionalR0FlowRewrite(items: readonly MachineItem[]): PostLayoutIndirectFlowResult {
  const ir = raiseMachineToIr(items);
  const rewrite = findFractionalR0FlowRewrite(ir);
  if (rewrite === undefined) return { items: [...items], preloads: [], optimizations: [], applied: 0 };

  const candidate = ir.map((op, index) => index === rewrite.index ? rewrite.op : op);
  const candidateItems = lowerIrToMachine(candidate);
  if (cellCount(candidateItems) >= cellCount(items)) {
    return { items: [...items], preloads: [], optimizations: [], applied: 0 };
  }

  return {
    items: candidateItems,
    preloads: [],
    optimizations: [{
      name: "r0-fractional-sentinel",
      detail: "Activated post-layout: replaced direct flow whose target resolves to address 99 with fractional R0 indirect flow.",
    }],
    applied: 1,
  };
}

export function optimizePostLayoutFractionalR0Flow(
  items: readonly MachineItem[],
  existingFlowPreloads: readonly PreloadReport[] = [],
): PostLayoutIndirectFlowResult {
  // Existing flow selector preloads encode concrete addresses. A fractional-R0
  // rewrite shrinks the program by one cell and cannot be retargeted like an
  // ordinary selector preload, so keep this final verifier conservative.
  if (existingFlowPreloads.length > 0) {
    return { items: [...items], preloads: [], optimizations: [], applied: 0 };
  }

  let current: MachineItem[] = [...items];
  let applied = 0;
  for (let round = 0; round < 8; round += 1) {
    const result = applyFractionalR0FlowRewrite(current);
    if (result.applied === 0) break;
    current = result.items;
    applied += result.applied;
  }

  if (applied === 0) return { items: [...items], preloads: [], optimizations: [], applied: 0 };
  return {
    items: current,
    preloads: [],
    optimizations: [{
      name: "r0-fractional-sentinel",
      detail: `Activated post-layout: replaced ${applied} direct flow op(s) whose target resolves to address 99 with fractional R0 indirect flow.`,
    }],
    applied,
  };
}

function overlayExecutableAt(items: readonly MachineItem[], index: number): { opcode: number; mnemonic: string } | undefined {
  const item = items[index];
  if (item?.kind === "op") return { opcode: item.opcode, mnemonic: item.mnemonic };
  if (item?.kind === "address") {
    const opcode = addressOpcodeForItem(items, item);
    return opcode === undefined ? undefined : { opcode, mnemonic: `address byte ${opcode.toString(16).toUpperCase().padStart(2, "0")}` };
  }
  return undefined;
}

function canOverlayExecutableCellAt(
  items: readonly MachineItem[],
  index: number,
): boolean {
  const executable = overlayExecutableAt(items, index);
  if (executable === undefined) return false;
  if (!ADDRESS_TAKING_OPCODES.has(executable.opcode)) return true;
  return items[index + 1]?.kind === "address";
}

function overlaidMnemonic(items: readonly MachineItem[], index: number): string {
  return overlayExecutableAt(items, index)?.mnemonic ?? "unknown";
}

function overlaidOpcode(items: readonly MachineItem[], index: number): number | undefined {
  return overlayExecutableAt(items, index)?.opcode;
}

function overlayAddressItem(
  address: Extract<MachineItem, { kind: "address" }>,
  overlaid: string,
): Extract<MachineItem, { kind: "address" }> {
  return {
    ...address,
    comment: [address.comment, `address/code overlay for ${overlaid}`].filter(Boolean).join("; "),
  };
}

function overlayAddressItemWithFormalOpcode(
  address: Extract<MachineItem, { kind: "address" }>,
  formalOpcode: number,
  overlaid: string,
): Extract<MachineItem, { kind: "address" }> {
  return {
    ...address,
    formalOpcode,
    comment: [
      address.comment,
      `formal address/code overlay for ${overlaid}`,
    ].filter(Boolean).join("; "),
  };
}

function directAddressTarget(
  items: readonly MachineItem[],
  itemIndex: number,
): string | number | undefined {
  const address = items[itemIndex + 1];
  return address?.kind === "address" ? address.target : undefined;
}

function createMachineReturnAnalyzer(items: readonly MachineItem[]): (target: string | number) => boolean {
  const layout = machineLayout(items);
  const visiting = new Set<number>();
  const memo = new Map<number, boolean>();

  const mayReturnFrom = (address: number): boolean => {
    if (memo.has(address)) return memo.get(address)!;
    if (visiting.has(address)) return false;
    const itemIndex = layout.itemIndexByAddress.get(address);
    if (itemIndex === undefined) return false;
    const item = items[itemIndex]!;
    const opcode = item.kind === "op"
      ? item.opcode
      : item.kind === "address"
        ? addressOpcodeForItem(items, item)
        : undefined;
    if (opcode === undefined) return true;

    visiting.add(address);
    let result: boolean;
    if (opcode === 0x52) {
      result = true;
    } else if (opcode === 0x50) {
      result = false;
    } else if (opcode === 0x51) {
      const branchTarget = directAddressTarget(items, itemIndex);
      const branchAddress = branchTarget === undefined ? undefined : resolvedMachineTarget(branchTarget, layout.labels);
      result = branchAddress === undefined ? true : mayReturnFrom(branchAddress);
    } else if (opcode === 0x53) {
      const branchTarget = directAddressTarget(items, itemIndex);
      const branchAddress = branchTarget === undefined ? undefined : resolvedMachineTarget(branchTarget, layout.labels);
      result = branchAddress === undefined ? true : mayReturnFrom(branchAddress) ? mayReturnFrom(address + 2) : false;
    } else if (ADDRESS_TAKING_OPCODES.has(opcode)) {
      const branchTarget = directAddressTarget(items, itemIndex);
      const branchAddress = branchTarget === undefined ? undefined : resolvedMachineTarget(branchTarget, layout.labels);
      result = branchAddress === undefined ? true : mayReturnFrom(branchAddress) || mayReturnFrom(address + 2);
    } else if (opcode >= 0x80 && opcode <= 0xee) {
      result = true;
    } else {
      result = mayReturnFrom(address + 1);
    }
    visiting.delete(address);
    memo.set(address, result);
    return result;
  };

  return (target: string | number): boolean => {
    const targetAddress = resolvedMachineTarget(target, layout.labels);
    return targetAddress === undefined ? true : mayReturnFrom(targetAddress);
  };
}

function canOverlayAddressContinuation(
  targetMayReturn: (target: string | number) => boolean,
  branch: Extract<MachineItem, { kind: "op" }>,
  address: Extract<MachineItem, { kind: "address" }>,
): boolean {
  if (branch.opcode === 0x51) return true;
  if (branch.opcode === 0x53) return !targetMayReturn(address.target);
  return false;
}

function previousNonLabelIndex(items: readonly MachineItem[], before: number): number | undefined {
  for (let index = before - 1; index >= 0; index -= 1) {
    if (items[index]?.kind !== "label") return index;
  }
  return undefined;
}

function labelsHaveNoLinearFallthrough(
  items: readonly MachineItem[],
  labelsStart: number,
  targetMayReturn: (target: string | number) => boolean,
): boolean {
  const previousIndex = previousNonLabelIndex(items, labelsStart);
  if (previousIndex === undefined) return false;
  const previous = items[previousIndex];
  if (previous?.kind === "op") return previous.opcode === 0x50 || previous.opcode === 0x52;
  if (previous?.kind !== "address") return false;

  const branchIndex = previousNonLabelIndex(items, previousIndex);
  const branch = branchIndex === undefined ? undefined : items[branchIndex];
  if (branch?.kind !== "op") return false;
  if (branch.opcode === 0x51) return true;
  if (branch.opcode === 0x53) return !targetMayReturn(previous.target);
  return false;
}

function fixedAddressTargetsSurviveRemoval(items: readonly MachineItem[], removedAddress: number): boolean {
  for (const item of items) {
    if (item.kind !== "address") continue;
    const fixedTarget = fixedAddressActualTarget(item);
    if (fixedTarget !== undefined && fixedTarget >= removedAddress) return false;
  }
  return true;
}

function fixedAddressActualTarget(address: Extract<MachineItem, { kind: "address" }>): number | undefined {
  if (address.formalOpcode !== undefined) return formalAddressInfo(address.formalOpcode).actual;
  return typeof address.target === "number" ? address.target : undefined;
}

function resolvedAddressTarget(
  items: readonly MachineItem[],
  address: Extract<MachineItem, { kind: "address" }>,
): number | undefined {
  if (typeof address.target === "number") return address.target;
  return machineLayout(items).labels.get(address.target);
}

function chooseOverlayAddressItem(
  candidate: readonly MachineItem[],
  address: Extract<MachineItem, { kind: "address" }>,
  executableOpcode: number,
  overlaid: string,
): Extract<MachineItem, { kind: "address" }> | undefined {
  const ordinary = overlayAddressItem(address, overlaid);
  if (addressOpcodeForItem(candidate, ordinary) === executableOpcode) return ordinary;

  const target = resolvedAddressTarget(candidate, ordinary);
  if (target === undefined) return undefined;
  const formal = formalAddressInfo(executableOpcode);
  if (formal.actual !== target) return undefined;

  const formalAddress = overlayAddressItemWithFormalOpcode(address, executableOpcode, overlaid);
  if (addressOpcodeForItem(candidate, formalAddress) !== executableOpcode) return undefined;
  return formalAddress;
}

function applyAddressCodeOverlay(items: readonly MachineItem[]): { items: MachineItem[]; applied: number } {
  const layout = machineLayout(items);
  const targetMayReturn = createMachineReturnAnalyzer(items);
  for (let index = 0; index < items.length - 2; index += 1) {
    const branch = items[index];
    const address = items[index + 1];
    if (branch?.kind !== "op" || address?.kind !== "address") continue;
    // БП has no live continuation after the address byte. ПП is accepted only
    // when its target is proved terminal; conditionals keep a fall-through path.
    if (!canOverlayAddressContinuation(targetMayReturn, branch, address)) continue;

    let labelsEnd = index + 2;
    const labels: Extract<MachineItem, { kind: "label" }>[] = [];
    while (items[labelsEnd]?.kind === "label") {
      labels.push(items[labelsEnd] as Extract<MachineItem, { kind: "label" }>);
      labelsEnd += 1;
    }
    if (labels.length === 0) continue;

    if (!canOverlayExecutableCellAt(items, labelsEnd)) continue;
    const executableOpcode = overlaidOpcode(items, labelsEnd);
    if (executableOpcode === undefined) continue;
    const addressCellAddress = layout.addressByItemIndex.get(index + 1);
    if (addressCellAddress === undefined) continue;
    const fixedTarget = fixedAddressActualTarget(address);
    if (fixedTarget !== undefined && fixedTarget > addressCellAddress) continue;

    const overlaid = overlaidMnemonic(items, labelsEnd);
    const provisional = [
      ...items.slice(0, index + 1),
      ...labels,
      address,
      ...items.slice(labelsEnd + 1),
    ];
    const candidateAddress = chooseOverlayAddressItem(provisional, address, executableOpcode, overlaid);
    if (candidateAddress === undefined) continue;
    const candidate = [
      ...items.slice(0, index + 1),
      ...labels,
      candidateAddress,
      ...items.slice(labelsEnd + 1),
    ];
    if (addressOpcodeForItem(candidate, candidateAddress) !== executableOpcode) continue;
    if (cellCount(candidate) >= cellCount(items)) continue;
    return { items: candidate, applied: 1 };
  }

  const referenced = referencedMachineLabels(items);
  for (let index = 0; index < items.length - 2; index += 1) {
    const branch = items[index];
    const address = items[index + 1];
    if (branch?.kind !== "op" || address?.kind !== "address") continue;
    if (!ADDRESS_TAKING_OPCODES.has(branch.opcode)) continue;

    for (let labelsStart = index + 3; labelsStart < items.length - 1; labelsStart += 1) {
      if (items[labelsStart]?.kind !== "label") continue;
      let labelsEnd = labelsStart;
      const labels: Extract<MachineItem, { kind: "label" }>[] = [];
      while (items[labelsEnd]?.kind === "label") {
        labels.push(items[labelsEnd] as Extract<MachineItem, { kind: "label" }>);
        labelsEnd += 1;
      }
      if (labels.length === 0 || !labels.some((label) => referenced.has(label.name))) continue;
      if (!labelsHaveNoLinearFallthrough(items, labelsStart, targetMayReturn)) continue;
      if (!canOverlayExecutableCellAt(items, labelsEnd)) continue;
      const executableOpcode = overlaidOpcode(items, labelsEnd);
      if (executableOpcode === undefined || ADDRESS_TAKING_OPCODES.has(executableOpcode)) continue;

      const removedAddress = layout.addressByItemIndex.get(labelsEnd);
      if (removedAddress === undefined || !fixedAddressTargetsSurviveRemoval(items, removedAddress)) continue;
      const branchTargetAddress = resolvedMachineTarget(address.target, layout.labels);
      if (
        branchTargetAddress === removedAddress &&
        !canOverlayAddressContinuation(targetMayReturn, branch, address)
      ) continue;
      const overlaid = overlaidMnemonic(items, labelsEnd);
      const provisional = [
        ...items.slice(0, index + 1),
        ...labels,
        address,
        ...items.slice(index + 2, labelsStart),
        ...items.slice(labelsEnd + 1),
      ];
      const candidateAddress = chooseOverlayAddressItem(provisional, address, executableOpcode, overlaid);
      if (candidateAddress === undefined) continue;
      const candidate = [
        ...items.slice(0, index + 1),
        ...labels,
        candidateAddress,
        ...items.slice(index + 2, labelsStart),
        ...items.slice(labelsEnd + 1),
      ];
      if (addressOpcodeForItem(candidate, candidateAddress) !== executableOpcode) continue;
      if (cellCount(candidate) >= cellCount(items)) continue;
      return { items: candidate, applied: 1 };
    }
  }

  return { items: [...items], applied: 0 };
}

export function optimizePostLayoutAddressCodeOverlay(items: readonly MachineItem[]): PostLayoutIndirectFlowResult {
  let current: MachineItem[] = [...items];
  let applied = 0;
  for (let round = 0; round < 16; round += 1) {
    const result = applyAddressCodeOverlay(current);
    if (result.applied === 0) break;
    current = result.items;
    applied += result.applied;
  }
  if (applied === 0) return { items: [...items], preloads: [], optimizations: [], applied: 0 };
  return {
    items: current,
    preloads: [],
    optimizations: [{
      name: "address-code-overlay",
      detail: `Overlaid ${applied} executable cell${applied === 1 ? "" : "s"} onto direct-jump address byte${applied === 1 ? "" : "s"} after post-layout proof.`,
    }],
    applied,
  };
}

// Merges selector registers that hold the identical preloaded value: every
// indirect branch through a duplicate register is re-pointed at the first
// register holding that value, and the redundant preloads are dropped. The one
// stored constant then serves several dispatch sites (constants-dual-use),
// freeing scarce registers. Read-only selectors holding the same value are
// behaviorally interchangeable, so this never changes execution.
function mergeDuplicateSelectors(
  items: readonly MachineItem[],
  preloads: readonly PreloadReport[],
): { items: MachineItem[]; preloads: PreloadReport[]; merged: number } {
  const canonicalByValue = new Map<string, RegisterName>();
  const remap = new Map<RegisterName, RegisterName>();
  const kept: PreloadReport[] = [];
  for (const preload of preloads) {
    const register = preload.register as RegisterName;
    const canonical = canonicalByValue.get(preload.value);
    if (canonical === undefined) {
      canonicalByValue.set(preload.value, register);
      kept.push(preload);
    } else if (register !== canonical) {
      remap.set(register, canonical);
    }
  }
  if (remap.size === 0) return { items: [...items], preloads: [...preloads], merged: 0 };

  const ir = raiseMachineToIr(items).map((op) => {
    if (!isIndirectBranch(op) || !remap.has(op.register)) return op;
    const target = remap.get(op.register)!;
    // The IR op caches its opcode; recompute it for the new register operand so
    // the rewrite does not silently keep the old register's byte.
    const opcode = op.opcode - registerIndex(op.register) + registerIndex(target);
    return { ...op, register: target, opcode };
  });
  return { items: lowerIrToMachine(ir), preloads: kept, merged: remap.size };
}

function selectorValueForRegister(
  preloads: readonly PreloadReport[],
  register: RegisterName,
  options?: CompileOptions,
): { value: string; existing: boolean } | undefined {
  for (const preload of preloads) {
    if (preload.register === register) return { value: preload.value, existing: false };
  }
  const existing = options?.preloadedConstantRegisters?.[register];
  return existing === undefined ? undefined : { value: existing, existing: true };
}

function firstExecutableOpIndexAtAddress(
  ops: readonly IrOp[],
  addresses: readonly number[],
  target: number,
): number | undefined {
  for (let index = 0; index < ops.length; index += 1) {
    if (addresses[index] !== target) continue;
    if (cellsPerOp(ops[index]!) > 0) return index;
  }
  return undefined;
}

function replaceIndirectTargetComment(
  comment: string | undefined,
  register: RegisterName,
  selectorValue: string,
  target: number,
): string | undefined {
  if (comment === undefined) return undefined;
  const escaped = register.replace(/[.*+?^${}()|[\]\\]/gu, "\\$&");
  const pattern = new RegExp(`preloaded R${escaped}=[^\\s;]+ indirect-target=\\d+`, "iu");
  if (!pattern.test(comment)) return comment;
  return comment.replace(pattern, `preloaded R${register}=${selectorValue} indirect-target=${target}`);
}

function retargetExistingSelectorsAfterShift(
  beforeItems: readonly MachineItem[],
  afterItems: readonly MachineItem[],
  preloads: readonly PreloadReport[],
): { items: MachineItem[]; preloads: PreloadReport[] } | undefined {
  if (preloads.length === 0) return { items: [...afterItems], preloads: [] };

  const before = raiseMachineToIr(beforeItems);
  const beforeAddresses = opAddresses(before);
  const after = raiseMachineToIr(afterItems);
  const afterAddresses = opAddresses(after);
  const nextPreloads: PreloadReport[] = [];
  const nextByRegister = new Map<RegisterName, string>();

  for (const preload of preloads) {
    const register = preload.register as RegisterName;
    const decoded = evaluateIndirectAddress(register, preload.value, "flow");
    if (decoded?.actualFlowTarget === undefined) return undefined;
    const targetIndex = firstExecutableOpIndexAtAddress(before, beforeAddresses, decoded.actualFlowTarget);
    if (targetIndex === undefined) return undefined;
    const shiftedTarget = afterAddresses[targetIndex];
    if (shiftedTarget === undefined) return undefined;
    const selectorValue = retargetedSelectorValue(register, preload.value, shiftedTarget);
    if (selectorValue === undefined) return undefined;
    const shiftedDecoded = evaluateIndirectAddress(register, selectorValue, "flow");
    if (shiftedDecoded?.actualFlowTarget !== shiftedTarget) return undefined;
    nextPreloads.push({ ...preload, value: selectorValue });
    nextByRegister.set(register, selectorValue);
  }

  const retargeted: IrOp[] = after.map((op) => {
    if (!isIndirectBranch(op)) return op;
    const selectorValue = nextByRegister.get(op.register);
    if (selectorValue === undefined) return op;
    const target = evaluateIndirectAddress(op.register, selectorValue, "flow")?.actualFlowTarget;
    if (target === undefined) return op;
    const comment = replaceIndirectTargetComment(op.meta.comment, op.register, selectorValue, target);
    const meta = comment === undefined
      ? { ...op.meta }
      : { ...op.meta, comment };
    if (comment === undefined) delete meta.comment;
    return {
      ...op,
      meta,
    };
  });

  return { items: lowerIrToMachine(retargeted), preloads: nextPreloads };
}

// Builds a numeric-target view of the program where every string-label branch
// target is replaced with that label's resolved cell address. Returns the
// numeric IR plus, for each op, the original label it targeted (when any) so a
// rewrite can later be verified and re-resolved against the final layout.
function numericTargetView(
  ir: readonly IrOp[],
  labels: ReadonlyMap<string, number>,
): { numeric: IrOp[]; targetLabel: Array<string | undefined> } {
  const numeric: IrOp[] = [];
  const targetLabel: Array<string | undefined> = [];
  const labelByAddress = new Map<number, string>();
  for (const [label, address] of labels) {
    if (!labelByAddress.has(address)) labelByAddress.set(address, label);
  }
  for (const op of ir) {
    if (isDirectBranch(op) && typeof op.target === "string") {
      const address = labels.get(op.target);
      if (address !== undefined) {
        numeric.push({ ...op, target: address });
        targetLabel.push(op.target);
        continue;
      }
    }
    if (isDirectBranch(op) && typeof op.target === "number") {
      targetLabel.push(labelByAddress.get(op.target));
      numeric.push(op);
      continue;
    }
    numeric.push(op);
    targetLabel.push(undefined);
  }
  return { numeric, targetLabel };
}

// Applies at most one verified indirect-flow rewrite. Runs the existing,
// unit-tested preloaded-indirect-flow pass on a numeric-target view, then keeps
// only the FIRST rewrite whose label target is known, re-lays out the program,
// and independently decodes the produced selector through the MK-61 indirect
// address model to prove it still lands on the target's final address. Any
// failure to prove correctness, or any failure to shrink, rejects the rewrite.
interface RewriteStep {
  items: MachineItem[];
  preload: PreloadReport;
  optimizations: AppliedOptimization[];
  superDark: boolean;
  darkEntry: boolean;
  convertedAddresses: number[];
  protectedTargets: number[];
  existingPreload: boolean;
  // Number of call/branch sites converted in this step. Calls that share a
  // single backward target (e.g. all calls to one front-hoisted helper) reuse
  // one selector register, so they can be converted together against a single
  // re-layout when the target sits before every site (its address is stable
  // under the batch of one-cell shrinks).
  converted: number;
}

function isSuperDarkRewrite(op: IndirectBranch): boolean {
  return /\bsuper-dark\b/iu.test(op.meta.comment ?? "");
}

// Validates a single candidate rewrite at `index`: keeps just that rewrite,
// restores every other op to its original label-targeted form, re-lays out, and
// independently decodes the produced selector through the MK-61 indirect address
// model to prove it still lands on the target's final address. Returns the proven,
// shrinking step or undefined.
function validateRewriteAt(
  index: number,
  ir: readonly IrOp[],
  numeric: readonly IrOp[],
  targetLabel: ReadonlyArray<string | undefined>,
  result: { ops: IrOp[]; preloads?: readonly PreloadReport[]; optimizations: AppliedOptimization[] },
  items: readonly MachineItem[],
  options: CompileOptions,
): RewriteStep | undefined {
  const rewritten = result.ops[index]!;
  const original = numeric[index]!;
  if (!isIndirectBranch(rewritten) || !isDirectBranch(original)) return undefined;

  const label = targetLabel[index];
  if (label === undefined) return undefined; // only rewrite re-resolvable label targets

  const selector = selectorValueForRegister(result.preloads ?? [], rewritten.register, options);
  if (selector === undefined) return undefined;
  const selectorValue = selector.value;

  const candidate = ir.map((op, i) => (i === index ? rewritten : op));
  const finalLabels = labelAddresses(candidate);
  const targetFinalAddress = finalLabels.get(label);
  if (targetFinalAddress === undefined) return undefined;

  const decoded = evaluateIndirectAddress(rewritten.register, selectorValue, "flow");
  if (decoded?.actualFlowTarget !== targetFinalAddress) return undefined; // unproven: reject

  const candidateItems = lowerIrToMachine(candidate);
  if (cellCount(candidateItems) >= cellCount(items)) return undefined; // no shrink: reject
  const addresses = opAddresses(ir);

  return {
    items: candidateItems,
    preload: { register: rewritten.register, value: selectorValue, countsAgainstProgram: false },
    optimizations: result.optimizations,
    superDark: isSuperDarkRewrite(rewritten),
    // A target reached through a formal address beyond the official 104-cell
    // window is a dark entry pointing at the executable suffix. The dark region
    // spans several formal kinds (dark / long-side / short-side, e.g. B2 -> 0,
    // C5 -> 13); FA..FF super-dark is reported separately.
    darkEntry: decoded.formalAddress !== undefined
      && decoded.formalAddress.kind !== "official"
      && decoded.formalAddress.kind !== "super-dark",
    convertedAddresses: [addresses[index]!],
    protectedTargets: [targetFinalAddress],
    existingPreload: selector.existing,
    converted: 1,
  };
}

// Validates a whole group of rewrites that share one selector register (and
// therefore one backward target): applies all of them at once, re-lays out, and
// proves every member's selector still decodes to the target's final address.
// This is sound precisely when the shared target sits before every converted
// site, so the batch of one-cell shrinks never moves it; any group whose target
// would shift fails the per-member decode check and is rejected.
function validateRewriteGroup(
  indices: readonly number[],
  ir: readonly IrOp[],
  numeric: readonly IrOp[],
  targetLabel: ReadonlyArray<string | undefined>,
  result: { ops: IrOp[]; preloads?: readonly PreloadReport[]; optimizations: AppliedOptimization[] },
  items: readonly MachineItem[],
  options: CompileOptions,
): RewriteStep | undefined {
  if (indices.length === 0) return undefined;
  const first = result.ops[indices[0]!]!;
  if (!isIndirectBranch(first)) return undefined;
  const register = first.register;
  const selector = selectorValueForRegister(result.preloads ?? [], register, options);
  if (selector === undefined) return undefined;
  const selectorValue = selector.value;

  const indexSet = new Set(indices);
  const candidate = ir.map((op, i) => (indexSet.has(i) ? result.ops[i]! : op));
  const finalLabels = labelAddresses(candidate);
  const decoded = evaluateIndirectAddress(register, selectorValue, "flow");
  if (decoded === undefined) return undefined;

  let superDark = false;
  for (const index of indices) {
    const rewritten = result.ops[index]!;
    const original = numeric[index]!;
    if (!isIndirectBranch(rewritten) || !isDirectBranch(original)) return undefined;
    if (rewritten.register !== register) return undefined;
    const label = targetLabel[index];
    if (label === undefined) return undefined;
    const targetFinalAddress = finalLabels.get(label);
    if (decoded.actualFlowTarget !== targetFinalAddress) return undefined; // target shifted: reject
    if (isSuperDarkRewrite(rewritten)) superDark = true;
  }

  const candidateItems = lowerIrToMachine(candidate);
  if (cellCount(candidateItems) >= cellCount(items)) return undefined;
  const addresses = opAddresses(ir);

  return {
    items: candidateItems,
    preload: { register, value: selectorValue, countsAgainstProgram: false },
    optimizations: result.optimizations,
    superDark,
    darkEntry: decoded.formalAddress !== undefined
      && decoded.formalAddress.kind !== "official"
      && decoded.formalAddress.kind !== "super-dark",
    convertedAddresses: indices.map((index) => addresses[index]!),
    protectedTargets: indices
      .map((index) => targetLabel[index])
      .map((label) => label === undefined ? undefined : finalLabels.get(label))
      .filter((target): target is number => target !== undefined),
    existingPreload: selector.existing,
    converted: indices.length,
  };
}

function validateForwardRewriteGroup(
  indices: readonly number[],
  ir: readonly IrOp[],
  targetLabel: ReadonlyArray<string | undefined>,
  register: RegisterName,
  items: readonly MachineItem[],
): RewriteStep | undefined {
  if (indices.length === 0) return undefined;

  const indexSet = new Set(indices);
  const provisional = ir.map((op, index) => {
    if (!indexSet.has(index)) return op;
    if (!isDirectBranch(op)) return op;
    return indirectFlowOp(op, register, "00", 0);
  });
  const finalLabels = labelAddresses(provisional);

  let finalTarget: number | undefined;
  for (const index of indices) {
    const original = ir[index]!;
    if (!isDirectBranch(original)) return undefined;
    const label = targetLabel[index];
    if (label === undefined) return undefined;
    const target = finalLabels.get(label);
    if (target === undefined) return undefined;
    if (finalTarget === undefined) {
      finalTarget = target;
    } else if (finalTarget !== target) {
      return undefined;
    }
  }
  if (finalTarget === undefined) return undefined;

  const selectorValue = selectorForActualTarget(finalTarget);
  if (selectorValue === undefined) return undefined;
  const decoded = evaluateIndirectAddress(register, selectorValue, "flow");
  if (decoded?.actualFlowTarget !== finalTarget) return undefined;

  const candidate = ir.map((op, index) => {
    if (!indexSet.has(index)) return op;
    if (!isDirectBranch(op)) return op;
    return indirectFlowOp(op, register, selectorValue, finalTarget);
  });
  const candidateItems = lowerIrToMachine(candidate);
  if (cellCount(candidateItems) >= cellCount(items)) return undefined;
  const addresses = opAddresses(ir);

  return {
    items: candidateItems,
    preload: { register, value: selectorValue, countsAgainstProgram: false },
    optimizations: [
      {
        name: "preloaded-indirect-flow",
        detail: `Replaced ${indices.length} forward branch/call(s) with compiler-owned preloaded indirect flow after re-layout proof.`,
      },
    ],
    superDark: false,
    darkEntry: decoded.formalAddress !== undefined
      && decoded.formalAddress.kind !== "official"
      && decoded.formalAddress.kind !== "super-dark",
    convertedAddresses: indices.map((index) => addresses[index]!),
    protectedTargets: [finalTarget],
    existingPreload: false,
    converted: indices.length,
  };
}

function applyForwardRewrite(
  ir: readonly IrOp[],
  targetLabel: ReadonlyArray<string | undefined>,
  items: readonly MachineItem[],
  reserved: ReadonlySet<RegisterName>,
): RewriteStep | undefined {
  const register = spareStableRegister(ir, reserved);
  if (register === undefined) return undefined;

  const labels = labelAddresses(ir);
  const addresses = opAddresses(ir);
  const groups = new Map<string, number[]>();
  for (let index = 0; index < ir.length; index += 1) {
    const op = ir[index]!;
    if (hasRewriteBarrier(op) || !isDirectBranch(op)) continue;
    const label = targetLabel[index];
    if (label === undefined) continue;
    const target = labels.get(label);
    if (target === undefined || target <= addresses[index]!) continue;
    const group = groups.get(label) ?? [];
    group.push(index);
    groups.set(label, group);
  }

  let best: RewriteStep | undefined;
  for (const indices of groups.values()) {
    const candidate = validateForwardRewriteGroup(indices, ir, targetLabel, register, items);
    if (candidate === undefined) continue;
    best = betterRewrite(best, candidate);
  }
  return best;
}

function applyOneRewrite(
  items: readonly MachineItem[],
  options: CompileOptions,
  existingPreloads: readonly PreloadReport[] = [],
): RewriteStep | undefined {
  const roundOptions = optionsWithReservedPreloads(options, existingPreloads);
  const reserved = new Set(Object.keys(roundOptions.preloadedConstantRegisters ?? {}) as RegisterName[]);
  const ir = raiseMachineToIr(items);
  const labels = labelAddresses(ir);
  const { numeric, targetLabel } = numericTargetView(ir, labels);
  let best: RewriteStep | undefined;

  // The post-layout driver keeps one verified step per re-layout and re-proves
  // every selector against the fresh address map, so it can safely drop the
  // tail-only guard and reach backward in-window calls anywhere in the program
  // (e.g. calls to a front-hoisted shared helper).
  const result = runPreloadedIndirectFlow(numeric, { options: roundOptions }, { relaxMaxTargetGuard: true });
  if (result.applied > 0) {
    // run() reuses one selector register per distinct backward target, so several
    // call sites can share it. Group the produced rewrites by register and try to
    // convert each group as a unit: when the shared target sits before every site
    // (e.g. a front-hoisted helper), the whole group is proven against one
    // re-layout, which avoids burning a fresh spare register per round.
    const groups = new Map<RegisterName, number[]>();
    for (let index = 0; index < result.ops.length; index += 1) {
      const op = result.ops[index]!;
      const original = numeric[index]!;
      if (!isIndirectBranch(op) || !isDirectBranch(original) || targetLabel[index] === undefined) continue;
      const list = groups.get(op.register) ?? [];
      list.push(index);
      groups.set(op.register, list);
    }

    // Keep the group that converts the most sites, falling back to a single
    // rewrite. Every choice goes through the same independent proof.
    for (const indices of groups.values()) {
      const group = validateRewriteGroup(indices, ir, numeric, targetLabel, result, items, roundOptions);
      const candidate = group ?? validateRewriteAt(indices[0]!, ir, numeric, targetLabel, result, items, roundOptions);
      if (candidate === undefined) continue;
      best = betterRewrite(best, candidate);
    }
  }

  return betterRewrite(best, applyForwardRewrite(ir, targetLabel, items, reserved));
}

function betterRewrite(current: RewriteStep | undefined, candidate: RewriteStep | undefined): RewriteStep | undefined {
  if (candidate === undefined) return current;
  if (current === undefined) return candidate;
  if (candidate.converted !== current.converted) return candidate.converted > current.converted ? candidate : current;
  if (candidate.superDark !== current.superDark) return candidate.superDark ? candidate : current;
  if (candidate.darkEntry !== current.darkEntry) return candidate.darkEntry ? candidate : current;
  return current;
}

const MAX_REWRITES = 64;

// Official MK-61 program window: cells 00..A4 (105 cells). Trading a direct
// branch for a preloaded indirect one moves a cell out of the program window
// and into setup state, so an already in-budget program keeps its clean direct
// branches. Once a program needs this rescue pass, though, every subsequent
// rewrite is proven and strictly shrinking, so the pass keeps going instead of
// stopping at the first <=105 layout.
const OFFICIAL_PROGRAM_LIMIT = 105;

// Activates the preloaded indirect-flow optimization on real programs. The
// underlying pass only fires on numeric branch targets, which exist only after
// layout assigns addresses; this driver runs it once addresses are known,
// rewriting one branch per re-layout so every step is verified against a fresh,
// correct address map. Every rewrite is proven safe (the selector decodes back
// to the target's final address) and strictly shrinks the program, so the
// transform is a no-op when it cannot help.
export function optimizePostLayoutIndirectFlow(
  items: readonly MachineItem[],
  options: CompileOptions,
  // Programs at or below this cell count keep their direct branches. Defaults
  // to the official MK-61 window; tests inject a smaller limit to exercise the
  // transform (and prove its behavior) on programs the emulator can actually run.
  rescueAbove: number = OFFICIAL_PROGRAM_LIMIT,
): PostLayoutIndirectFlowResult {
  let current: MachineItem[] = [...items];
  const preloads: PreloadReport[] = [];
  let applied = 0;
  let superDarkApplied = 0;
  let darkEntryApplied = 0;
  let existingSelectorApplied = 0;
  const immutableTargets: number[] = [];

  if (cellCount(current) <= rescueAbove) {
    return { items: [...items], preloads: [], optimizations: [], applied: 0 };
  }

  for (let round = 0; round < MAX_REWRITES; round += 1) {
    const step = applyOneRewrite(current, options, preloads);
    if (step === undefined) break;
    if (step.convertedAddresses.some((address) => immutableTargets.some((target) => address < target))) {
      break;
    }
    const retargeted = retargetExistingSelectorsAfterShift(current, step.items, preloads);
    if (retargeted === undefined) break;
    current = retargeted.items;
    preloads.splice(0, preloads.length, ...retargeted.preloads);
    if (step.existingPreload) {
      existingSelectorApplied += step.converted;
      immutableTargets.push(...step.protectedTargets);
    } else {
      preloads.push(step.preload);
    }
    applied += step.converted;
    if (step.superDark) superDarkApplied += 1;
    if (step.darkEntry) darkEntryApplied += 1;
  }

  if (applied === 0) {
    return { items: [...items], preloads: [], optimizations: [], applied: 0 };
  }

  const merge = mergeDuplicateSelectors(current, preloads);
  current = merge.items;
  const finalPreloads = merge.preloads;

  // Report exactly the idioms actually applied so capability status stays honest.
  const optimizations: AppliedOptimization[] = [
    {
      name: "preloaded-indirect-flow",
      detail: `Activated post-layout: replaced ${applied} direct branch/call(s) with proven preloaded indirect flow.`,
    },
  ];
  if (superDarkApplied > 0) {
    optimizations.push({
      name: "preloaded-super-dark-flow",
      detail: `Selected ${superDarkApplied} FA..FF one-command super-dark dispatch(es), each proven to fall through to its 01..06 continuation.`,
    });
  }
  if (darkEntryApplied > 0) {
    optimizations.push({
      name: "dark-entry-layout",
      detail: `Pointed ${darkEntryApplied} branch(es) at an executable suffix through a proven dark-entry formal address (beyond the 104-cell window).`,
    });
  }
  if (merge.merged > 0) {
    optimizations.push({
      name: "constants-dual-use",
      detail: `Shared ${merge.merged} duplicate selector register(s): one stored constant now drives several dispatch sites, freeing the rest.`,
    });
  }
  if (existingSelectorApplied > 0) {
    optimizations.push({
      name: "constants-dual-use",
      detail: `Reused existing setup constant preload(s) as immutable indirect-flow selector(s) for ${existingSelectorApplied} branch/call site(s).`,
    });
  }

  return { items: current, preloads: finalPreloads, optimizations, applied };
}

interface StopTailReuseBase {
  register: RegisterName;
  target: number;
  continuationOpcode: number;
}

interface StopTailReuseRewrite {
  base: StopTailReuseBase;
  replaceIndex: number;
  removeIndex: number;
  zeroPrefixed: boolean;
}

interface BranchToStopTailSelectorRewrite {
  branchIndex: number;
  addressIndex: number;
  opcode: number;
  mnemonic: string;
  comment: string;
  sourceLine?: number;
}

interface ExistingSelectorFlowRewrite {
  branchIndex: number;
  addressIndex: number;
  opcode: number;
  mnemonic: string;
  comment: string;
  sourceLine?: number;
}

interface EmptyStackTailCallRewrite {
  callIndex: number;
  loopBackIndex: number;
  mnemonic: string;
  comment: string;
  sourceLine?: number;
}

function indirectJumpMachineOp(register: RegisterName, comment: string, sourceLine?: number): MachineItem {
  return {
    kind: "op",
    opcode: 0x80 + registerIndex(register),
    mnemonic: `К БП ${register}`,
    comment,
    ...(sourceLine === undefined ? {} : { sourceLine }),
  };
}

function indirectBranchOpcodeForRegister(opcode: number, register: RegisterName): number | undefined {
  const offset = registerIndex(register);
  switch (opcode) {
    case 0x51:
      return 0x80 + offset;
    case 0x53:
      return 0xa0 + offset;
    case 0x57:
      return 0x70 + offset;
    case 0x59:
      return 0x90 + offset;
    case 0x5c:
      return 0xc0 + offset;
    case 0x5e:
      return 0xe0 + offset;
    default:
      return undefined;
  }
}

function indirectBranchMnemonic(opcode: number, register: RegisterName): string {
  const base = opcode - registerIndex(register);
  switch (base) {
    case 0x70:
      return `К x≠0 ${register}`;
    case 0x80:
      return `К БП ${register}`;
    case 0x90:
      return `К x≥0 ${register}`;
    case 0xa0:
      return `К ПП ${register}`;
    case 0xc0:
      return `К x<0 ${register}`;
    case 0xe0:
      return `К x=0 ${register}`;
    default:
      return `К БП ${register}`;
  }
}

function existingSelectorFlowTargets(
  preloads: readonly PreloadReport[],
): Array<{ register: RegisterName; value: string; target: number }> {
  const targets: Array<{ register: RegisterName; value: string; target: number }> = [];
  for (const preload of preloads) {
    const register = preload.register as RegisterName;
    const target = evaluateIndirectAddress(register, preload.value, "flow")?.actualFlowTarget;
    if (target === undefined) continue;
    targets.push({ register, value: preload.value, target });
  }
  return targets;
}

function findExistingSelectorFlowRewrite(
  items: readonly MachineItem[],
  preloads: readonly PreloadReport[],
): ExistingSelectorFlowRewrite | undefined {
  const selectors = existingSelectorFlowTargets(preloads);
  if (selectors.length === 0) return undefined;
  const cells = machineCells(items);
  const labels = machineLabelAddresses(items);
  const labelsByAddress = machineLabelsByAddress(items);
  const referencedLabels = referencedMachineLabels(items);

  for (let index = 0; index < cells.length - 1; index += 1) {
    const branch = cells[index]!;
    const address = cells[index + 1]!;
    if (branch.item.kind !== "op" || address.item.kind !== "address") continue;
    const target = resolvedMachineTarget(address.item.target, labels);
    if (target === undefined) continue;
    for (const selector of selectors) {
      if (selector.target !== target) continue;
      const opcode = indirectBranchOpcodeForRegister(branch.item.opcode, selector.register);
      if (opcode === undefined) continue;
      if (addressHasReferencedLabel(labelsByAddress, referencedLabels, address.address)) continue;
      return {
        branchIndex: branch.itemIndex,
        addressIndex: address.itemIndex,
        opcode,
        mnemonic: indirectBranchMnemonic(opcode, selector.register),
        comment: [
          branch.item.comment,
          `reused preloaded R${selector.register}=${selector.value} indirect-target=${target} direct flow`,
        ].filter(Boolean).join("; "),
        ...(branch.item.sourceLine === undefined ? {} : { sourceLine: branch.item.sourceLine }),
      };
    }
  }
  return undefined;
}

function applyExistingSelectorFlowRewrite(
  items: readonly MachineItem[],
  rewrite: ExistingSelectorFlowRewrite,
): MachineItem[] {
  const result: MachineItem[] = [];
  for (let index = 0; index < items.length; index += 1) {
    if (index === rewrite.addressIndex) continue;
    if (index === rewrite.branchIndex) {
      result.push({
        kind: "op",
        opcode: rewrite.opcode,
        mnemonic: rewrite.mnemonic,
        comment: rewrite.comment,
        ...(rewrite.sourceLine === undefined ? {} : { sourceLine: rewrite.sourceLine }),
      });
      continue;
    }
    result.push(items[index]!);
  }
  return result;
}

function firstProcedureStartAddress(items: readonly MachineItem[]): number | undefined {
  let address = 0;
  for (const item of items) {
    if (item.kind === "label") {
      if (item.procedureBoundary === "start") return address;
      continue;
    }
    address += 1;
  }
  return undefined;
}

function knownMachineIndirectJumpTarget(op: MachineItem, preloads: readonly PreloadReport[]): number | undefined {
  if (op.kind !== "op") return undefined;
  const register = registerFromIndirectOpcode(op.opcode);
  if (register === undefined || op.opcode - registerIndex(register) !== 0x80) return undefined;
  const commentTarget = /\bindirect-target=(\d+)\b/u.exec(op.comment ?? "")?.[1];
  if (commentTarget !== undefined) {
    const target = Number(commentTarget);
    if (Number.isInteger(target)) return target;
  }
  const selectorValue = preloadValueForRegister(preloads, register);
  return selectorValue === undefined
    ? undefined
    : evaluateIndirectAddress(register, selectorValue, "flow")?.actualFlowTarget;
}

function findEmptyStackTailCallRewrite(
  items: readonly MachineItem[],
  preloads: readonly PreloadReport[],
): EmptyStackTailCallRewrite | undefined {
  const cells = machineCells(items);
  const firstProc = firstProcedureStartAddress(items);
  if (firstProc === undefined) return undefined;

  for (let index = 0; index < cells.length - 2; index += 1) {
    const call = cells[index]!;
    const address = cells[index + 1]!;
    const loopBack = cells[index + 2]!;
    if (call.address >= firstProc) break;
    if (call.item.kind !== "op" || call.item.opcode !== 0x53) continue;
    if (address.item.kind !== "address") continue;
    if (knownMachineIndirectJumpTarget(loopBack.item, preloads) !== 0) continue;

    return {
      callIndex: call.itemIndex,
      loopBackIndex: loopBack.itemIndex,
      mnemonic: "БП",
      comment: [
        call.item.comment?.replace(/^proc call/u, "empty-stack tail call") ?? "empty-stack tail call",
        "empty-return-stack loop head",
      ].filter(Boolean).join("; "),
      ...(call.item.sourceLine === undefined ? {} : { sourceLine: call.item.sourceLine }),
    };
  }
  return undefined;
}

function applyEmptyStackTailCallRewrite(
  items: readonly MachineItem[],
  rewrite: EmptyStackTailCallRewrite,
): MachineItem[] {
  const result: MachineItem[] = [];
  for (let index = 0; index < items.length; index += 1) {
    if (index === rewrite.loopBackIndex) continue;
    if (index === rewrite.callIndex) {
      result.push({
        kind: "op",
        opcode: 0x51,
        mnemonic: rewrite.mnemonic,
        comment: rewrite.comment,
        ...(rewrite.sourceLine === undefined ? {} : { sourceLine: rewrite.sourceLine }),
      });
      continue;
    }
    result.push(items[index]!);
  }
  return result;
}

function stopTailReuseBases(
  items: readonly MachineItem[],
  preloads: readonly PreloadReport[],
): StopTailReuseBase[] {
  const cells = machineCells(items);
  const bases: StopTailReuseBase[] = [];
  for (const preload of preloads) {
    const register = preload.register as RegisterName;
    const decoded = evaluateIndirectAddress(register, preload.value, "flow");
    const target = decoded?.actualFlowTarget;
    if (target === undefined) continue;
    const stop = machineCellAt(cells, target)?.item;
    const continuation = machineCellAt(cells, target + 1)?.item;
    if (
      stop?.kind !== "op" ||
      stop.opcode !== 0x50 ||
      continuation?.kind !== "op" ||
      continuation.opcode < 0x80 ||
      continuation.opcode > 0x8e
    ) {
      continue;
    }
    bases.push({ register, target, continuationOpcode: continuation.opcode });
  }
  return bases;
}

function findStopTailReuseRewrite(
  items: readonly MachineItem[],
  preloads: readonly PreloadReport[],
): StopTailReuseRewrite | undefined {
  const bases = stopTailReuseBases(items, preloads);
  if (bases.length === 0) return undefined;
  const cells = machineCells(items);
  const labels = machineLabelsByAddress(items);
  const referencedLabels = referencedMachineLabels(items);

  for (const base of bases) {
    for (const cell of cells) {
      if (cell.address <= base.target) continue;
      const item = cell.item;
      const next = machineCellAt(cells, cell.address + 1);
      const afterNext = machineCellAt(cells, cell.address + 2);

      if (
        item.kind === "op" &&
        item.opcode === 0x50 &&
        next?.item.kind === "op" &&
        next.item.opcode === base.continuationOpcode &&
        !addressHasReferencedLabel(labels, referencedLabels, next.address)
      ) {
        return {
          base,
          replaceIndex: cell.itemIndex,
          removeIndex: next.itemIndex,
          zeroPrefixed: false,
        };
      }

      if (
        item.kind === "op" &&
        (item.opcode === 0x00 || item.opcode === 0x0d) &&
        next?.item.kind === "op" &&
        next.item.opcode === 0x50 &&
        afterNext?.item.kind === "op" &&
        afterNext.item.opcode === base.continuationOpcode &&
        !addressHasReferencedLabel(labels, referencedLabels, next.address) &&
        !addressHasReferencedLabel(labels, referencedLabels, afterNext.address)
      ) {
        return {
          base,
          replaceIndex: next.itemIndex,
          removeIndex: afterNext.itemIndex,
          zeroPrefixed: true,
        };
      }
    }
  }
  return undefined;
}

function applyStopTailReuseRewrite(
  items: readonly MachineItem[],
  rewrite: StopTailReuseRewrite,
): MachineItem[] {
  const result: MachineItem[] = [];
  const source = items[rewrite.replaceIndex];
  for (let index = 0; index < items.length; index += 1) {
    if (index === rewrite.removeIndex) continue;
    if (index === rewrite.replaceIndex) {
      result.push(indirectJumpMachineOp(
        rewrite.base.register,
        `${rewrite.zeroPrefixed ? "zero then " : ""}reuse stop tail at ${rewrite.base.target}`,
        source?.kind === "op" || source?.kind === "address" ? source.sourceLine : undefined,
      ));
      continue;
    }
    result.push(items[index]!);
  }
  return result;
}

function preloadValueForRegister(
  preloads: readonly PreloadReport[],
  register: RegisterName,
): string | undefined {
  return preloads.find((preload) => preload.register === register)?.value;
}

function findBranchToStopTailSelectorRewrite(
  items: readonly MachineItem[],
  preloads: readonly PreloadReport[],
): BranchToStopTailSelectorRewrite | undefined {
  const cells = machineCells(items);
  const labels = machineLabelAddresses(items);
  const labelsByAddress = machineLabelsByAddress(items);
  const referencedLabels = referencedMachineLabels(items);

  for (let index = 0; index < cells.length - 1; index += 1) {
    const branch = cells[index]!;
    const address = cells[index + 1]!;
    if (branch.item.kind !== "op" || address.item.kind !== "address") continue;
    const target = resolvedMachineTarget(address.item.target, labels);
    if (target === undefined) continue;
    const targetCell = machineCellAt(cells, target);
    if (targetCell?.item.kind !== "op") continue;
    const register = registerFromIndirectOpcode(targetCell.item.opcode);
    if (register === undefined || targetCell.item.opcode - registerIndex(register) !== 0x80) continue;
    const selectorValue = preloadValueForRegister(preloads, register);
    if (selectorValue === undefined) continue;
    const stopTarget = evaluateIndirectAddress(register, selectorValue, "flow")?.actualFlowTarget;
    if (stopTarget === undefined) continue;
    const stop = machineCellAt(cells, stopTarget)?.item;
    if (stop?.kind !== "op" || stop.opcode !== 0x50) continue;
    const opcode = indirectBranchOpcodeForRegister(branch.item.opcode, register);
    if (opcode === undefined) continue;
    if (addressHasReferencedLabel(labelsByAddress, referencedLabels, address.address)) continue;
    return {
      branchIndex: branch.itemIndex,
      addressIndex: address.itemIndex,
      opcode,
      mnemonic: indirectBranchMnemonic(opcode, register),
      comment: [branch.item.comment, `branch to reused stop tail at ${stopTarget}`].filter(Boolean).join("; "),
      ...(branch.item.sourceLine === undefined ? {} : { sourceLine: branch.item.sourceLine }),
    };
  }
  return undefined;
}

function applyBranchToStopTailSelectorRewrite(
  items: readonly MachineItem[],
  rewrite: BranchToStopTailSelectorRewrite,
): MachineItem[] {
  const result: MachineItem[] = [];
  for (let index = 0; index < items.length; index += 1) {
    if (index === rewrite.addressIndex) continue;
    if (index === rewrite.branchIndex) {
      result.push({
        kind: "op",
        opcode: rewrite.opcode,
        mnemonic: rewrite.mnemonic,
        comment: rewrite.comment,
        ...(rewrite.sourceLine === undefined ? {} : { sourceLine: rewrite.sourceLine }),
      });
      continue;
    }
    result.push(items[index]!);
  }
  return result;
}

function registerFromIndirectOpcode(opcode: number): RegisterName | undefined {
  const offset = opcode & 0x0f;
  if (offset > 14) return undefined;
  const base = opcode - offset;
  if (
    base !== 0x70 &&
    base !== 0x80 &&
    base !== 0x90 &&
    base !== 0xa0 &&
    base !== 0xc0 &&
    base !== 0xe0
  ) {
    return undefined;
  }
  return offset <= 9 ? String(offset) as RegisterName : String.fromCharCode(87 + offset) as RegisterName;
}

function retargetMachineSelectorComments(
  items: readonly MachineItem[],
  selectorByRegister: ReadonlyMap<RegisterName, string>,
): MachineItem[] {
  return items.map((item): MachineItem => {
    if (item.kind !== "op") return item;
    const register = registerFromIndirectOpcode(item.opcode);
    if (register === undefined) return item;
    const selectorValue = selectorByRegister.get(register);
    if (selectorValue === undefined) return item;
    const target = evaluateIndirectAddress(register, selectorValue, "flow")?.actualFlowTarget;
    if (target === undefined) return item;
    const comment = replaceIndirectTargetComment(item.comment, register, selectorValue, target);
    if (comment === item.comment) return item;
    if (comment === undefined) {
      const { comment: _comment, ...rest } = item;
      return rest;
    }
    return { ...item, comment };
  });
}

function retargetSelectorPreloadsAfterMachineDeletion(
  beforeItems: readonly MachineItem[],
  afterItems: readonly MachineItem[],
  preloads: readonly PreloadReport[],
): { items: MachineItem[]; preloads: PreloadReport[] } | undefined {
  if (preloads.length === 0) return { items: [...afterItems], preloads: [] };

  const beforeCells = machineCells(beforeItems);
  const afterAddressByItemIndex = machineAddressByItemIndex(afterItems);
  const nextPreloads: PreloadReport[] = [];
  const nextByRegister = new Map<RegisterName, string>();

  for (const preload of preloads) {
    const register = preload.register as RegisterName;
    const decoded = evaluateIndirectAddress(register, preload.value, "flow");
    const target = decoded?.actualFlowTarget;
    if (target === undefined) return undefined;
    const beforeCell = machineCellAt(beforeCells, target);
    if (beforeCell === undefined) return undefined;
    const targetItem = beforeItems[beforeCell.itemIndex];
    const afterItemIndex = afterItems.findIndex((item) => item === targetItem);
    if (afterItemIndex < 0) return undefined;
    const shiftedTarget = afterAddressByItemIndex.get(afterItemIndex);
    if (shiftedTarget === undefined) return undefined;
    const selectorValue = shiftedTarget === target
      ? preload.value
      : retargetedSelectorValue(register, preload.value, shiftedTarget);
    if (selectorValue === undefined) return undefined;
    const shiftedDecoded = evaluateIndirectAddress(register, selectorValue, "flow");
    if (shiftedDecoded?.actualFlowTarget !== shiftedTarget) return undefined;
    nextPreloads.push({ ...preload, value: selectorValue });
    nextByRegister.set(register, selectorValue);
  }

  return {
    items: retargetMachineSelectorComments(afterItems, nextByRegister),
    preloads: nextPreloads,
  };
}

export function optimizePostLayoutStopTailReuse(
  items: readonly MachineItem[],
  preloads: readonly PreloadReport[],
): PostLayoutIndirectFlowResult {
  let current: MachineItem[] = [...items];
  let currentPreloads: PreloadReport[] = [...preloads];
  let stopTailApplied = 0;
  let existingSelectorApplied = 0;
  let emptyStackTailCallApplied = 0;

  for (let round = 0; round < MAX_REWRITES; round += 1) {
    const emptyStackTailCall = findEmptyStackTailCallRewrite(current, currentPreloads);
    if (emptyStackTailCall !== undefined) {
      const candidate = applyEmptyStackTailCallRewrite(current, emptyStackTailCall);
      if (cellCount(candidate) >= cellCount(current)) break;
      const retargeted = retargetSelectorPreloadsAfterMachineDeletion(current, candidate, currentPreloads);
      if (retargeted === undefined) break;
      current = retargeted.items;
      currentPreloads = retargeted.preloads;
      emptyStackTailCallApplied += 1;
      continue;
    }

    const existingSelectorRewrite = findExistingSelectorFlowRewrite(current, currentPreloads);
    if (existingSelectorRewrite !== undefined) {
      const candidate = applyExistingSelectorFlowRewrite(current, existingSelectorRewrite);
      if (cellCount(candidate) >= cellCount(current)) break;
      const retargeted = retargetSelectorPreloadsAfterMachineDeletion(current, candidate, currentPreloads);
      if (retargeted === undefined) break;
      current = retargeted.items;
      currentPreloads = retargeted.preloads;
      existingSelectorApplied += 1;
      continue;
    }

    const branchRewrite = findBranchToStopTailSelectorRewrite(current, currentPreloads);
    if (branchRewrite !== undefined) {
      const candidate = applyBranchToStopTailSelectorRewrite(current, branchRewrite);
      if (cellCount(candidate) >= cellCount(current)) break;
      const retargeted = retargetSelectorPreloadsAfterMachineDeletion(current, candidate, currentPreloads);
      if (retargeted === undefined) break;
      current = retargeted.items;
      currentPreloads = retargeted.preloads;
      stopTailApplied += 1;
      continue;
    }

    const rewrite = findStopTailReuseRewrite(current, currentPreloads);
    if (rewrite === undefined) break;
    const candidate = applyStopTailReuseRewrite(current, rewrite);
    if (cellCount(candidate) >= cellCount(current)) break;
    const retargeted = retargetSelectorPreloadsAfterMachineDeletion(current, candidate, currentPreloads);
    if (retargeted === undefined) break;
    current = retargeted.items;
    currentPreloads = retargeted.preloads;
    stopTailApplied += 1;
  }

  const applied = stopTailApplied + existingSelectorApplied + emptyStackTailCallApplied;
  if (applied === 0) {
    return { items: [...items], preloads: [...preloads], optimizations: [], applied: 0 };
  }
  const optimizations: AppliedOptimization[] = [];
  if (emptyStackTailCallApplied > 0) {
    optimizations.push({
      name: "post-layout-empty-stack-tail-call",
      detail: `Replaced ${emptyStackTailCallApplied} terminal main-loop call${emptyStackTailCallApplied === 1 ? "" : "s"} with direct jump(s) whose final В/О returns through the empty stack to the loop head.`,
    });
  }
  if (stopTailApplied > 0) {
    optimizations.push({
      name: "post-layout-stop-tail-reuse",
      detail: `Replaced ${stopTailApplied} repeated stop tail${stopTailApplied === 1 ? "" : "s"} with proven preloaded indirect jumps to an existing stop tail.`,
    });
  }
  if (existingSelectorApplied > 0) {
    optimizations.push({
      name: "post-layout-existing-selector-flow",
      detail: `Replaced ${existingSelectorApplied} direct branch/call${existingSelectorApplied === 1 ? "" : "s"} with already-proven preloaded selector flow after retargeting shifted selectors.`,
    });
  }
  return {
    items: current,
    preloads: currentPreloads,
    optimizations,
    applied,
  };
}
