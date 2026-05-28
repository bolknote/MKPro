import { evaluateIndirectAddress } from "./indirect-addressing.ts";
import { lowerIrToMachine, raiseMachineToIr } from "./ir.ts";
import { cellsPerOp } from "./passes/helpers.ts";
import { runPreloadedIndirectFlow } from "./passes/preloaded-indirect-flow.ts";
import type {
  AppliedOptimization,
  CompileOptions,
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

type DirectBranch = Extract<IrOp, { kind: "jump" | "call" | "cjump" }>;
type IndirectBranch = Extract<IrOp, { kind: "indirect-jump" | "indirect-call" | "indirect-cjump" }>;

function isDirectBranch(op: IrOp): op is DirectBranch {
  return op.kind === "jump" || op.kind === "call" || op.kind === "cjump";
}

function isIndirectBranch(op: IrOp): op is IndirectBranch {
  return op.kind === "indirect-jump" || op.kind === "indirect-call" || op.kind === "indirect-cjump";
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
): string | undefined {
  for (const preload of preloads) {
    if (preload.register === register) return preload.value;
  }
  return undefined;
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
  for (const op of ir) {
    if (isDirectBranch(op) && typeof op.target === "string") {
      const address = labels.get(op.target);
      if (address !== undefined) {
        numeric.push({ ...op, target: address });
        targetLabel.push(op.target);
        continue;
      }
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
): RewriteStep | undefined {
  const rewritten = result.ops[index]!;
  const original = numeric[index]!;
  if (!isIndirectBranch(rewritten) || !isDirectBranch(original)) return undefined;

  const label = targetLabel[index];
  if (label === undefined) return undefined; // only rewrite re-resolvable label targets

  const selectorValue = selectorValueForRegister(result.preloads ?? [], rewritten.register);
  if (selectorValue === undefined) return undefined;

  const candidate = ir.map((op, i) => (i === index ? rewritten : op));
  const finalLabels = labelAddresses(candidate);
  const targetFinalAddress = finalLabels.get(label);
  if (targetFinalAddress === undefined) return undefined;

  const decoded = evaluateIndirectAddress(rewritten.register, selectorValue, "flow");
  if (decoded?.actualFlowTarget !== targetFinalAddress) return undefined; // unproven: reject

  const candidateItems = lowerIrToMachine(candidate);
  if (cellCount(candidateItems) >= cellCount(items)) return undefined; // no shrink: reject

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
): RewriteStep | undefined {
  if (indices.length === 0) return undefined;
  const first = result.ops[indices[0]!]!;
  if (!isIndirectBranch(first)) return undefined;
  const register = first.register;
  const selectorValue = selectorValueForRegister(result.preloads ?? [], register);
  if (selectorValue === undefined) return undefined;

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

  return {
    items: candidateItems,
    preload: { register, value: selectorValue, countsAgainstProgram: false },
    optimizations: result.optimizations,
    superDark,
    darkEntry: decoded.formalAddress !== undefined
      && decoded.formalAddress.kind !== "official"
      && decoded.formalAddress.kind !== "super-dark",
    converted: indices.length,
  };
}

function applyOneRewrite(
  items: readonly MachineItem[],
  options: CompileOptions,
): RewriteStep | undefined {
  const ir = raiseMachineToIr(items);
  const labels = labelAddresses(ir);
  const { numeric, targetLabel } = numericTargetView(ir, labels);

  // The post-layout driver keeps one verified step per re-layout and re-proves
  // every selector against the fresh address map, so it can safely drop the
  // tail-only guard and reach backward in-window calls anywhere in the program
  // (e.g. calls to a front-hoisted shared helper).
  const result = runPreloadedIndirectFlow(numeric, { options }, { relaxMaxTargetGuard: true });
  if (result.applied === 0) return undefined;

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

  // Prefer a provable FA..FF super-dark group (densest MK-61 idiom); otherwise
  // take the group that converts the most sites, falling back to a single
  // rewrite. Every choice goes through the same independent proof.
  let best: RewriteStep | undefined;
  for (const indices of groups.values()) {
    const group = validateRewriteGroup(indices, ir, numeric, targetLabel, result, items);
    const candidate = group ?? validateRewriteAt(indices[0]!, ir, numeric, targetLabel, result, items);
    if (candidate === undefined) continue;
    if (candidate.superDark) return candidate;
    if (best === undefined || candidate.converted > best.converted) best = candidate;
  }
  return best;
}

const MAX_REWRITES = 64;

// Official MK-61 program window: cells 00..A4 (105 cells). Trading a direct
// branch for a preloaded indirect one moves a cell out of the program window
// and into setup state, so it is only worthwhile to rescue programs that
// overflow the official window. In-budget programs keep their clean direct
// branches (and stay byte-identical) instead of acquiring a setup-time preload
// requirement just to shave a cell they do not need to shave.
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

  for (let round = 0; round < MAX_REWRITES; round += 1) {
    if (cellCount(current) <= rescueAbove) break;
    const step = applyOneRewrite(current, options);
    if (step === undefined) break;
    current = step.items;
    preloads.push(step.preload);
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

  return { items: current, preloads: finalPreloads, optimizations, applied };
}
