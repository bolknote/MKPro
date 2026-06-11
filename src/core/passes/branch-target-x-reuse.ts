import type { IrOp, RegisterName } from "../types.ts";
import { isStableIndirectSelector } from "../indirect-addressing.ts";
import {
  analyzeRecallRemoval,
  addressIndexes,
  analyzeX2StackEffect,
  computeX2RegisterStates,
  computeX2ValueStates,
  directReturnAnalysisContext,
  emptyResult,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  isKnownReturnCallOp,
  knownIndirectFlowTarget,
  knownIndirectMemoryTarget,
  plainPreservesXValue,
  removableRecallValueRegister,
  transferX2ValueStateThroughKnownTransparentReturnCall,
  transferX2RegisterStateForEdge,
  transferX2ValueStateForEdge,
  x2KnownReturnCallPreservesStackXAndX2,
  type DirectReturnAnalysisContext,
  type IrPass,
  type IrPassFn,
  type X2ValueFact,
  type X2ValueDataflowState,
} from "./helpers.ts";

const run: IrPassFn = (ops) => {
  const labels = labelIndexes(ops);
  const addresses = addressIndexes(ops);
  const references = targetReferenceCountsByEntryIndex(ops, labels, addresses);
  const x2States = computeX2RegisterStates(ops);
  const x2ValueStates = computeX2ValueStates(ops, { trackRegisterMemory: true });
  const directReturnContext = directReturnAnalysisContext(ops);
  const remove = new Set<number>();

  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (!isConditionalTargetOp(op) || hasRewriteBarrier(op)) continue;

    const targetIndex = branchTargetEntryIndex(ops, op, labels, addresses);
    if (targetIndex === undefined) continue;
    if ((references.get(targetIndex) ?? 0) !== 1) continue;
    if (hasFallthroughIntoIndex(ops, targetIndex)) continue;
    const heldRegister = immediatelyHeldRegister(ops, index);
    const targetRegisterState = transferX2RegisterStateForEdge(
      {
        x: heldRegister === undefined ? undefined : new Set<RegisterName>([heldRegister]),
        x2: x2States[index],
      },
      op,
      "jump",
    );
    const targetRecall = branchTargetRecallAfterTransparentPrefix(
      ops,
      targetIndex,
      references,
      targetRegisterState,
      transferX2ValueStateForEdge(
        x2ValueStates[index],
        op,
        "jump",
        { trackRegisterMemory: true },
        index,
      ),
      directReturnContext,
    );
    if (targetRecall === undefined || remove.has(targetRecall.index)) continue;
    const target = ops[targetRecall.index]!;
    const targetRegister = removableRecallValueRegister(target);
    if (targetRegister === undefined) continue;
    if (op.kind === "loop" && loopCounterRegister(op.counter) === targetRegister) continue;
    const preservedRegister = branchPreservedRegister(heldRegister, op, targetRegister);
    const targetX2RegisterState = targetRecall.x2RegisterState ?? x2States[targetRecall.index];
    const targetValueState = targetRecall.valueState ?? x2ValueStates[targetRecall.index];
    const removal = analyzeRecallRemoval(
      ops,
      targetRecall.index,
      targetX2RegisterState,
      targetValueState,
      directReturnContext,
    );
    if (preservedRegister !== targetRegister && removal?.valueProof?.inX !== true) continue;
    if (removal?.removable !== true) {
      continue;
    }

    remove.add(targetRecall.index);
  }

  if (remove.size === 0) return emptyResult(ops);

  return {
    ops: ops.filter((_, index) => !remove.has(index)),
    applied: remove.size,
    optimizations: [{
      name: "branch-target-x-reuse",
      detail: `Dropped ${remove.size} branch-target recall${remove.size === 1 ? "" : "s"} already preserved in X by the branch path.`,
    }],
  };
};

export const branchTargetXReuse: IrPass = {
  name: "branch-target-x-reuse",
  run,
  layoutSafe: false,
};

function branchPreservedRegister(
  register: RegisterName | undefined,
  branch: Extract<IrOp, { kind: "cjump" | "loop" | "indirect-cjump" }>,
  targetRegister: RegisterName,
): RegisterName | undefined {
  if (register === undefined) return undefined;
  if (branch.kind === "loop" && loopCounterRegister(branch.counter) === register) return undefined;
  if (branch.kind === "indirect-cjump" && branch.register === register && !isStableIndirectSelector(branch.register)) {
    return undefined;
  }
  if (branch.kind === "indirect-cjump" && branch.register === targetRegister && !isStableIndirectSelector(branch.register)) {
    return undefined;
  }
  return register;
}

function immediatelyHeldRegister(ops: readonly IrOp[], branchIndex: number): RegisterName | undefined {
  for (let index = branchIndex - 1; index >= 0; index -= 1) {
    const op = ops[index]!;
    if (op.kind === "label") continue;
    return removableRecallValueRegister(op);
  }
  return undefined;
}

function loopCounterRegister(counter: Extract<IrOp, { kind: "loop" }>["counter"]): RegisterName {
  switch (counter) {
    case "L0":
      return "0";
    case "L1":
      return "1";
    case "L2":
      return "2";
    case "L3":
      return "3";
  }
}

function labelIndexes(ops: readonly IrOp[]): Map<string, number> {
  const result = new Map<string, number>();
  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (op.kind === "label") result.set(op.name, index);
  }
  return result;
}

function targetReferenceCountsByEntryIndex(
  ops: readonly IrOp[],
  labels: ReadonlyMap<string, number>,
  addresses: ReadonlyMap<number, number>,
): Map<number, number> {
  const result = new Map<number, number>();
  for (const op of ops) {
    const target = flowTargetEntryIndex(ops, op, labels, addresses);
    if (target !== undefined) result.set(target, (result.get(target) ?? 0) + 1);
  }
  return result;
}

function flowTargetEntryIndex(
  ops: readonly IrOp[],
  op: IrOp,
  labels: ReadonlyMap<string, number>,
  addresses: ReadonlyMap<number, number>,
): number | undefined {
  switch (op.kind) {
    case "jump":
    case "cjump":
    case "call":
    case "loop": {
      if (typeof op.target === "number") return addresses.get(op.target);
      const labelIndex = labels.get(op.target);
      return labelIndex === undefined ? undefined : nextExecutableIndex(ops, labelIndex + 1);
    }
    case "indirect-jump":
    case "indirect-call":
    case "indirect-cjump": {
      const target = knownIndirectFlowTarget(op);
      return target === undefined ? undefined : addresses.get(target);
    }
    default:
      return undefined;
  }
}

function isConditionalTargetOp(
  op: IrOp,
): op is Extract<IrOp, { kind: "cjump" | "loop" | "indirect-cjump" }> {
  return op.kind === "cjump" || op.kind === "loop" || op.kind === "indirect-cjump";
}

function branchTargetEntryIndex(
  ops: readonly IrOp[],
  op: IrOp,
  labels: ReadonlyMap<string, number>,
  addresses: ReadonlyMap<number, number>,
): number | undefined {
  if ((op.kind === "cjump" || op.kind === "loop") && typeof op.target === "string") {
    const labelIndex = labels.get(op.target);
    return labelIndex === undefined ? undefined : nextExecutableIndex(ops, labelIndex + 1);
  }
  if ((op.kind === "cjump" || op.kind === "loop") && typeof op.target === "number") {
    return addresses.get(op.target);
  }
  if (op.kind === "indirect-cjump") {
    const target = knownIndirectFlowTarget(op);
    return target === undefined ? undefined : addresses.get(target);
  }
  return undefined;
}

function branchTargetRecallAfterTransparentPrefix(
  ops: readonly IrOp[],
  targetIndex: number,
  references: ReadonlyMap<number, number>,
  x2RegisterState: ReadonlySet<RegisterName> | undefined,
  targetValueState: X2ValueDataflowState | undefined,
  directReturnContext: DirectReturnAnalysisContext,
): {
  readonly index: number;
  readonly x2RegisterState?: ReadonlySet<RegisterName> | undefined;
  readonly valueState?: X2ValueDataflowState | undefined;
} | undefined {
  let valueState = targetValueState;
  let registerState = x2RegisterState;
  for (let index = targetIndex; index < ops.length; index += 1) {
    if (index !== targetIndex && (references.get(index) ?? 0) > 0) return undefined;
    const op = ops[index]!;
    if (removableRecallValueRegister(op) !== undefined) return { index, x2RegisterState: registerState, valueState };
    if (!isTransparentBranchTargetPrefixOp(ops, op, directReturnContext)) return undefined;
    const storedRegister = transparentPrefixStoredRegister(op);
    if (storedRegister !== undefined) {
      registerState = transferTransparentStoreX2RegisterState(registerState, valueState?.x, storedRegister);
    }
    valueState = isKnownReturnCallOp(op)
      ? transferX2ValueStateThroughKnownTransparentReturnCall(
        ops,
        op,
        valueState,
        directReturnContext,
        { trackRegisterMemory: true },
        index,
      )
      : transferX2ValueStateForEdge(valueState, op, "normal", { trackRegisterMemory: true }, index);
  }
  return undefined;
}

function isTransparentBranchTargetPrefixOp(
  ops: readonly IrOp[],
  op: IrOp,
  directReturnContext: DirectReturnAnalysisContext,
): boolean {
  if (hasRewriteBarrier(op) || isDisplayFocusSensitive(op)) return false;
  if (isKnownReturnCallOp(op)) return x2KnownReturnCallPreservesStackXAndX2(ops, op, directReturnContext);
  switch (op.kind) {
    case "label":
    case "store":
    case "orphan-address":
      return true;
    case "indirect-store":
      return isStableIndirectSelector(op.register) && knownIndirectMemoryTarget(op) !== undefined;
    case "plain": {
      if (hasIrRoles(op) || !plainPreservesXValue(op)) return false;
      const effect = analyzeX2StackEffect(op);
      return effect.x2Preserves && effect.stackPreserves;
    }
    default:
      return false;
  }
}

function hasIrRoles(op: Extract<IrOp, { kind: "plain" }>): boolean {
  return "meta" in op && op.meta.roles !== undefined && op.meta.roles.length > 0;
}

function transparentPrefixStoredRegister(op: IrOp): RegisterName | undefined {
  switch (op.kind) {
    case "store":
      return op.register;
    case "indirect-store":
      return isStableIndirectSelector(op.register) ? knownIndirectMemoryTarget(op) : undefined;
    default:
      return undefined;
  }
}

function transferTransparentStoreX2RegisterState(
  x2: ReadonlySet<RegisterName> | undefined,
  xValues: ReadonlySet<X2ValueFact> | undefined,
  register: RegisterName,
): ReadonlySet<RegisterName> | undefined {
  if (x2 === undefined || xValues === undefined) return undefined;
  const output = new Set<RegisterName>();
  for (const fact of x2) {
    if (fact !== register) output.add(fact);
  }
  if (setsIntersectRegisterFacts(xValues, x2)) output.add(register);
  return output;
}

function setsIntersectRegisterFacts(values: ReadonlySet<X2ValueFact>, registers: ReadonlySet<RegisterName>): boolean {
  if (registers.size === 0) return false;
  for (const fact of values) {
    const match = /^reg:([0-9a-e])$/u.exec(fact);
    if (match !== null && registers.has(match[1] as RegisterName)) return true;
  }
  return false;
}

function hasFallthroughIntoIndex(ops: readonly IrOp[], targetIndex: number): boolean {
  const previous = previousExecutableIndex(ops, targetIndex - 1);
  if (previous === undefined) return false;
  return !isNoFallthrough(ops[previous]!);
}

function previousExecutableIndex(ops: readonly IrOp[], start: number): number | undefined {
  for (let index = start; index >= 0; index -= 1) {
    if (ops[index]?.kind !== "label") return index;
  }
  return undefined;
}

function nextExecutableIndex(ops: readonly IrOp[], start: number): number | undefined {
  for (let index = start; index < ops.length; index += 1) {
    if (ops[index]?.kind !== "label") return index;
  }
  return undefined;
}

function isNoFallthrough(op: IrOp): boolean {
  return op.kind === "jump" || op.kind === "indirect-jump" || op.kind === "return" || op.kind === "stop";
}
