import type { IrOp, RegisterName } from "../types.ts";
import { isStableIndirectSelector } from "../indirect-addressing.ts";
import { computeLiveness } from "./liveness-analysis.ts";
import {
  analyzeRecallRemoval,
  computeX2DotRestoreGapStates,
  computeX2ImmediateSyncStates,
  computeX2RegisterStates,
  computeX2ValueStates,
  directReturnAnalysisContext,
  hasRewriteBarrier,
  isDisplayFocusSensitive,
  isKnownReturnCallOp,
  knownReturnCallReturnsThroughNestedTransparentRange,
  knownIndirectFlowTarget,
  knownIndirectMemoryTarget,
  removableRecallValueRegister,
  removingRecallCanExposeX2Restore,
  removingPreShiftLiftCanExposeStack,
  x2CanUseSourceDotRestoreAt,
  x2HasSignRestoreGapBeforeVp,
  x2HasOnlyRestoreGapBeforeVp,
  x2ReplacementDotHasOnlyRestoreGapBeforeVp,
  x2StateHasUnsafeDotRestoreShapeX2,
  x2StateCanDiscardRestoreRunBeforeProvedVp,
  x2StatesHaveSameVpEntrySignSource,
  x2StateHasSameVisibleXAndY,
  x2ValueFactIsNormalizedDecimal,
  x2PreviousStackLiftAndX2SyncProducerIndex,
  x2ValueSetHasFact,
  x2ValueShapeSetsHaveSameDotSafeDecimal,
  x2ValueShapeSetsHaveSameDotSafeStructuralMantissa,
  x2ValueShapeSetsHaveSameRestoredDisplayShape,
  x2ValueShapeSetsHaveSameRestoredVisibleDecimal,
  type DirectReturnAnalysisContext,
  type IrPass,
  type IrPassFn,
  type KnownReturnCallOp,
  type X2ValueDataflowState,
  type X2ValueFact,
} from "./helpers.ts";

const DOT = 0x0a;

const run: IrPassFn = (ops) => {
  const x2States = computeX2RegisterStates(ops);
  const x2ValueStates = computeX2ValueStates(ops, { trackRegisterMemory: true });
  const dotSafeStates = computeX2DotRestoreGapStates(ops);
  const immediateSyncStates = computeX2ImmediateSyncStates(ops);
  const liveness = computeLiveness(ops);
  const directReturnContext = directReturnAnalysisContext(ops);
  let applied = 0;

  const result = ops.map((op, index): IrOp => {
    const register = removableRecallValueRegister(op);
    if (register === undefined) return op;
    if (!isSupportedScratchRecall(op)) return op;
    if (isDisplayFocusSensitive(op)) return op;
    const storeIndex = findDeadScratchStore(ops, index, register, directReturnContext);
    if (storeIndex === undefined) return op;
    if (liveness.liveOut[index]?.has(register) === true) return op;
    const removal = analyzeRecallRemoval(ops, index, x2States[index], x2ValueStates[index], directReturnContext);
    if (removal === undefined) return op;
    const sourceAlreadySynced = hiddenTempStoreSourceAlreadySyncedInX2(
      ops,
      storeIndex,
      index,
      x2ValueStates[storeIndex],
      x2ValueStates[index],
      directReturnContext,
    );
    const sourceAlreadyDotSafe = hiddenTempStoreSourceAlreadyDotSafeInX2(
      x2ValueStates[storeIndex],
      x2ValueStates[index],
    );
    const sourceRestoresSameVisibleDecimal = hiddenTempStoreSourceRestoresSameVisibleDecimalFromX2(
      x2ValueStates[storeIndex],
      x2ValueStates[index],
    );
    const sourceRestoresSameVisibleShape = hiddenTempStoreSourceRestoresSameVisibleShapeFromX2(
      ops,
      storeIndex,
      index,
      x2ValueStates[storeIndex],
      x2ValueStates[index],
      directReturnContext,
    );
    const sourceRestoresSameDotSafeDecimalShape = hiddenTempStoreSourceRestoresSameDotSafeDecimalShapeFromX2(
      x2ValueStates[storeIndex],
      x2ValueStates[index],
    );
    const sourceRestoresSameDotSafeStructuralShape = hiddenTempStoreSourceRestoresSameDotSafeStructuralShapeFromX2(
      x2ValueStates[storeIndex],
      x2ValueStates[index],
    );
    if (
      removal.x2SyncRedundant !== true &&
      !sourceAlreadySynced &&
      !sourceRestoresSameVisibleDecimal &&
      !sourceRestoresSameVisibleShape &&
      !sourceRestoresSameDotSafeDecimalShape &&
      !sourceRestoresSameDotSafeStructuralShape
    ) return op;
    const sourceProvesFreeStandingRestore = sourceAlreadyDotSafe ||
      sourceRestoresSameVisibleDecimal ||
      sourceRestoresSameDotSafeDecimalShape ||
      sourceRestoresSameDotSafeStructuralShape;
    const canUseSourceDotRestore = x2CanUseSourceDotRestoreAt(
      ops,
      index,
      x2ValueStates[index],
      dotSafeStates[index] === true,
      immediateSyncStates[index] === true,
      sourceProvesFreeStandingRestore,
      directReturnContext,
    );
    const hasOnlyRestoreGapBeforeVp = x2HasOnlyRestoreGapBeforeVp(ops, index + 1, directReturnContext);
    const insertedDotHasOnlyRestoreGapBeforeVp = x2ReplacementDotHasOnlyRestoreGapBeforeVp(
      ops,
      index + 1,
      directReturnContext,
    );
    const hasSignRestoreGapBeforeVp = x2HasSignRestoreGapBeforeVp(ops, index + 1, directReturnContext);
    const recallSyncProvesVpSource =
      removal.valueProof?.x2SyncVpShape === true ||
      removal.valueProof?.x2SyncShape === true;
    const canUseVpSourceEscape =
      (
        (
          sourceAlreadyDotSafe &&
          hasOnlyRestoreGapBeforeVp &&
          (
            x2StateCanDiscardRestoreRunBeforeProvedVp(x2ValueStates[index], x2ValueStates[index + 1]) ||
            (
              hasSignRestoreGapBeforeVp &&
              x2StatesHaveSameVpEntrySignSource(x2ValueStates[index], x2ValueStates[index + 1])
            )
          )
        ) ||
        (
          recallSyncProvesVpSource &&
          insertedDotHasOnlyRestoreGapBeforeVp &&
          !hasSignRestoreGapBeforeVp
        )
      );
    if (
      !canUseVpSourceEscape &&
      !sourceRestoresSameVisibleShape &&
      !sourceRestoresSameDotSafeDecimalShape &&
      !sourceRestoresSameDotSafeStructuralShape &&
      x2StateHasUnsafeDotRestoreShapeX2(x2ValueStates[index])
    ) return op;
    if (
      !canUseSourceDotRestore &&
      !canUseVpSourceEscape
    ) {
      return op;
    }
    const exposesX2Restore = canUseVpSourceEscape
      ? false
      : (
          sourceAlreadySynced ||
          sourceRestoresSameVisibleShape ||
          sourceRestoresSameDotSafeDecimalShape ||
          sourceRestoresSameDotSafeStructuralShape
        ) &&
          removal.x2SyncRedundant !== true
        ? removingRecallCanExposeX2Restore(ops, index, {
          redundantSyncValue: sourceAlreadySynced,
          redundantSyncShape: sourceRestoresSameVisibleShape ||
            sourceRestoresSameDotSafeDecimalShape ||
            sourceRestoresSameDotSafeStructuralShape,
        })
        : removal.exposesX2Restore;
    const exposesStackLift = removal.exposesStackLift &&
      !hiddenTempRecallStackLiftAlreadySuppliedByDuplicateY(ops, index, x2ValueStates[index], directReturnContext);
    if (exposesStackLift || exposesX2Restore) return op;

    applied += 1;
    return dotRestoreOp(register, op);
  });

  if (applied === 0) return { ops: [...ops], applied: 0, optimizations: [] };

  return {
    ops: result,
    applied,
    optimizations: [
      {
        name: "x2-hidden-temp-restore",
        detail: `Replaced ${applied} recall${applied === 1 ? "" : "s"} with . after proving the value already lives in X2 and the recall stack lift is unused.`,
      },
    ],
  };
};

function dotRestoreOp(register: RegisterName, source: IrOp): IrOp {
  const sourceComment = "meta" in source ? source.meta.comment : undefined;
  return {
    kind: "plain",
    opcode: DOT,
    meta: {
      mnemonic: ".",
      comment: [sourceComment, `restore ${register} from hidden X2 temp`].filter(Boolean).join("; "),
    },
  };
}

function hiddenTempStoreSourceAlreadySyncedInX2(
  ops: readonly IrOp[],
  storeIndex: number,
  recallIndex: number,
  storeState: X2ValueDataflowState | undefined,
  recallState: X2ValueDataflowState | undefined,
  directReturnContext: DirectReturnAnalysisContext,
): boolean {
  if (storeState === undefined || recallState === undefined) return false;
  for (const fact of storeState.x) {
    if (
      !isStableStoredSourceFact(ops, storeIndex, recallIndex, fact, directReturnContext) &&
      !isStableRegisterStoredSourceFact(ops, storeIndex, recallIndex, fact, directReturnContext)
    ) continue;
    if (x2ValueSetHasFact(recallState.x2, fact)) return true;
  }
  return false;
}

function hiddenTempStoreSourceAlreadyDotSafeInX2(
  storeState: X2ValueDataflowState | undefined,
  recallState: X2ValueDataflowState | undefined,
): boolean {
  if (storeState === undefined || recallState === undefined) return false;
  for (const fact of storeState.x) {
    if (!isNormalizedDecimalFact(fact)) continue;
    if (x2ValueSetHasFact(recallState.x2, fact)) return true;
  }
  return false;
}

function hiddenTempStoreSourceRestoresSameVisibleDecimalFromX2(
  storeState: X2ValueDataflowState | undefined,
  recallState: X2ValueDataflowState | undefined,
): boolean {
  return x2ValueShapeSetsHaveSameRestoredVisibleDecimal(
    storeState?.x,
    storeState?.xShape,
    recallState?.x2,
    recallState?.x2Shape,
  );
}

function hiddenTempStoreSourceRestoresSameVisibleShapeFromX2(
  ops: readonly IrOp[],
  storeIndex: number,
  recallIndex: number,
  storeState: X2ValueDataflowState | undefined,
  recallState: X2ValueDataflowState | undefined,
  directReturnContext: DirectReturnAnalysisContext,
): boolean {
  return hiddenTempStoreComputedSourceAlreadySyncedInX2(
    ops,
    storeIndex,
    recallIndex,
    storeState,
    recallState,
    directReturnContext,
  ) &&
    x2ValueShapeSetsHaveSameRestoredDisplayShape(
      storeState?.x,
      storeState?.xShape,
      recallState?.x2,
      recallState?.x2Shape,
    );
}

function hiddenTempStoreSourceRestoresSameDotSafeDecimalShapeFromX2(
  storeState: X2ValueDataflowState | undefined,
  recallState: X2ValueDataflowState | undefined,
): boolean {
  return x2ValueShapeSetsHaveSameDotSafeDecimal(
    storeState?.x,
    storeState?.xShape,
    recallState?.x2,
    recallState?.x2Shape,
  );
}

function hiddenTempStoreSourceRestoresSameDotSafeStructuralShapeFromX2(
  storeState: X2ValueDataflowState | undefined,
  recallState: X2ValueDataflowState | undefined,
): boolean {
  return x2ValueShapeSetsHaveSameDotSafeStructuralMantissa(
    storeState?.x,
    storeState?.xShape,
    recallState?.x2,
    recallState?.x2Shape,
  );
}

function hiddenTempRecallStackLiftAlreadySuppliedByDuplicateY(
  ops: readonly IrOp[],
  recallIndex: number,
  recallState: X2ValueDataflowState | undefined,
  directReturnContext: DirectReturnAnalysisContext,
): boolean {
  return x2StateHasSameVisibleXAndY(recallState) &&
    x2PreviousStackLiftAndX2SyncProducerIndex(ops, recallIndex, directReturnContext) !== undefined &&
    !removingPreShiftLiftCanExposeStack(ops, recallIndex);
}

function hiddenTempStoreComputedSourceAlreadySyncedInX2(
  ops: readonly IrOp[],
  storeIndex: number,
  recallIndex: number,
  storeState: X2ValueDataflowState | undefined,
  recallState: X2ValueDataflowState | undefined,
  directReturnContext: DirectReturnAnalysisContext,
): boolean {
  if (storeState === undefined || recallState === undefined) return false;
  for (const fact of storeState.x) {
    if (!fact.startsWith("expr:") && !fact.startsWith("expr-key:")) continue;
    if (!isStableStoredSourceFact(ops, storeIndex, recallIndex, fact, directReturnContext)) continue;
    if (x2ValueSetHasFact(recallState.x2, fact)) return true;
  }
  return false;
}

function isStableStoredSourceFact(
  ops: readonly IrOp[],
  storeIndex: number,
  recallIndex: number,
  fact: X2ValueFact,
  directReturnContext: DirectReturnAnalysisContext,
): boolean {
  if (fact.startsWith("expr:")) return true;
  if (fact.startsWith("expr-key:")) {
    return [...registerDependenciesInValueFact(fact)].every((register) =>
      !registerMayBeOverwrittenBetween(ops, storeIndex + 1, recallIndex, register, directReturnContext)
    );
  }
  return fact.startsWith("decimal:") && fact.endsWith(":normalized");
}

function isStableRegisterStoredSourceFact(
  ops: readonly IrOp[],
  storeIndex: number,
  recallIndex: number,
  fact: X2ValueFact,
  directReturnContext: DirectReturnAnalysisContext,
): boolean {
  const register = registerSourceValueFact(fact);
  return register !== undefined &&
    !registerMayBeOverwrittenBetween(ops, storeIndex + 1, recallIndex, register, directReturnContext);
}

function registerSourceValueFact(fact: X2ValueFact): RegisterName | undefined {
  const match = /^reg:([0-9a-e])$/u.exec(fact);
  return match?.[1] as RegisterName | undefined;
}

function registerDependenciesInValueFact(fact: X2ValueFact): Set<RegisterName> {
  const registers = new Set<RegisterName>();
  const re = /reg:([0-9a-e])/gu;
  for (const match of fact.matchAll(re)) registers.add(match[1] as RegisterName);
  return registers;
}

function registerMayBeOverwrittenBetween(
  ops: readonly IrOp[],
  start: number,
  end: number,
  register: RegisterName,
  directReturnContext: DirectReturnAnalysisContext,
): boolean {
  const visited = new Set<string>();
  const visit = (index: number, maybeOverwritten: boolean): boolean => {
    for (let cursor = index; cursor < ops.length; cursor += 1) {
      if (cursor === end) return maybeOverwritten;
      const key = `${cursor}:${maybeOverwritten ? 1 : 0}`;
      if (visited.has(key)) return false;
      visited.add(key);

      const op = ops[cursor]!;
      if (hasRewriteBarrier(op)) return true;
      if (op.kind === "label" || op.kind === "orphan-address") continue;

      switch (op.kind) {
        case "store":
          maybeOverwritten = maybeOverwritten || op.register === register;
          continue;
        case "indirect-store": {
          maybeOverwritten = maybeOverwritten || memoryAccessMayOverwriteRegister(op, register);
          continue;
        }
        case "indirect-recall":
          maybeOverwritten = maybeOverwritten || memoryAccessMayOverwriteRegister(op, register);
          continue;
        case "loop": {
          const nextOverwritten = maybeOverwritten || loopCounterRegister(op.counter) === register;
          const target = flowTargetStartIndex(ops, op.target, directReturnContext);
          return target === undefined ||
            visit(cursor + 1, nextOverwritten) ||
            visit(target, nextOverwritten);
        }
        case "cjump": {
          const target = flowTargetStartIndex(ops, op.target, directReturnContext);
          return target === undefined ||
            visit(cursor + 1, maybeOverwritten) ||
            visit(target, maybeOverwritten);
        }
        case "indirect-cjump": {
          const target = knownIndirectFlowStartIndex(ops, op, directReturnContext);
          const nextOverwritten = maybeOverwritten || memoryAccessMayOverwriteRegister(op, register);
          return target === undefined ||
            visit(cursor + 1, nextOverwritten) ||
            visit(target, nextOverwritten);
        }
        case "jump": {
          const target = flowTargetStartIndex(ops, op.target, directReturnContext);
          return target === undefined || visit(target, maybeOverwritten);
        }
        case "indirect-jump": {
          const target = knownIndirectFlowStartIndex(ops, op, directReturnContext);
          const nextOverwritten = maybeOverwritten || memoryAccessMayOverwriteRegister(op, register);
          return target === undefined || visit(target, nextOverwritten);
        }
        case "call":
        case "indirect-call":
          if (isKnownReturnCallOp(op) && directReturningCallDoesNotOverwriteRegister(
            ops,
            op,
            register,
            directReturnContext,
          )) continue;
          return true;
        case "return":
          return true;
        case "stop":
          return false;
        case "recall":
        case "plain":
          continue;
      }
    }
    return false;
  };
  return visit(start, false);
}

function flowTargetStartIndex(
  ops: readonly IrOp[],
  target: string | number,
  directReturnContext: DirectReturnAnalysisContext,
): number | undefined {
  const targetIndex = typeof target === "string"
    ? directReturnContext.labels.get(target)
    : directReturnContext.addresses.get(target);
  return targetIndex === undefined ? undefined : executableStartIndex(ops, targetIndex);
}

function knownIndirectFlowStartIndex(
  ops: readonly IrOp[],
  op: Extract<IrOp, { kind: "indirect-jump" | "indirect-call" | "indirect-cjump" }>,
  directReturnContext: DirectReturnAnalysisContext,
): number | undefined {
  const target = knownIndirectFlowTarget(op);
  if (target === undefined) return undefined;
  const targetIndex = directReturnContext.addresses.get(target);
  return targetIndex === undefined ? undefined : executableStartIndex(ops, targetIndex);
}

function executableStartIndex(ops: readonly IrOp[], index: number): number {
  return ops[index]?.kind === "label" ? index + 1 : index;
}

function isNormalizedDecimalFact(fact: X2ValueFact): boolean {
  return x2ValueFactIsNormalizedDecimal(fact);
}

function isSupportedScratchRecall(op: IrOp): op is Extract<IrOp, { kind: "recall" | "indirect-recall" }> {
  if (op.kind === "recall") return true;
  return op.kind === "indirect-recall" &&
    isStableIndirectSelector(op.register) &&
    knownIndirectMemoryTarget(op) !== undefined;
}

function findDeadScratchStore(
  ops: readonly IrOp[],
  recallIndex: number,
  register: RegisterName,
  directReturnContext: DirectReturnAnalysisContext,
): number | undefined {
  for (let index = recallIndex - 1; index >= 0; index -= 1) {
    const op = ops[index]!;
    if (op.kind === "label") {
      if (directReturnContext.labelEntries.has(index)) return undefined;
      continue;
    }
    if (hasRewriteBarrier(op)) return undefined;
    if (op.kind === "store" && op.register === register) {
      return isDisplayFocusSensitive(op) ? undefined : index;
    }
    if (
      op.kind === "indirect-store" &&
      isStableIndirectSelector(op.register) &&
      knownIndirectMemoryTarget(op) === register
    ) {
      return isDisplayFocusSensitive(op) ? undefined : index;
    }
    if (mentionsRegister(op, register)) return undefined;
    if (op.kind === "cjump" || op.kind === "loop") continue;
    if (isKnownIndirectConditionalFallthroughThatDoesNotMentionRegister(op, register)) continue;
    if (isKnownReturnCallOp(op) && directReturningCallDoesNotMentionRegister(
      ops,
      op,
      register,
      directReturnContext,
    )) continue;
    if (stopsStraightLineSearch(op)) return undefined;
  }
  return undefined;
}

function isKnownIndirectConditionalFallthroughThatDoesNotMentionRegister(
  op: IrOp,
  register: RegisterName,
): boolean {
  return op.kind === "indirect-cjump" &&
    isStableIndirectSelector(op.register) &&
    knownIndirectFlowTarget(op) !== undefined &&
    !mentionsRegister(op, register);
}

function directReturningCallDoesNotMentionRegister(
  ops: readonly IrOp[],
  call: KnownReturnCallOp,
  register: RegisterName,
  directReturnContext: DirectReturnAnalysisContext,
): boolean {
  return directReturningCallHasRegisterSafeBody(ops, call, register, directReturnContext, "does-not-mention");
}

function directReturningCallDoesNotOverwriteRegister(
  ops: readonly IrOp[],
  call: KnownReturnCallOp,
  register: RegisterName,
  directReturnContext: DirectReturnAnalysisContext,
): boolean {
  return directReturningCallHasRegisterSafeBody(ops, call, register, directReturnContext, "does-not-overwrite");
}

type RegisterSafeBodyMode = "does-not-mention" | "does-not-overwrite";

function directReturningCallHasRegisterSafeBody(
  ops: readonly IrOp[],
  call: KnownReturnCallOp,
  register: RegisterName,
  directReturnContext: DirectReturnAnalysisContext,
  mode: RegisterSafeBodyMode,
): boolean {
  return knownReturnCallReturnsThroughNestedTransparentRange(
    ops,
    call,
    directReturnContext,
    (op) => linearRegisterSafetyPredicate(op, register, mode),
  );
}

function linearRegisterSafetyPredicate(
  op: IrOp,
  register: RegisterName,
  mode: RegisterSafeBodyMode,
): boolean {
  if (mode === "does-not-mention") {
    return memoryAccessDoesNotMentionRegister(op, register) && !stopsStraightLineSearch(op);
  }
  return !memoryAccessMayOverwriteRegister(op, register) && !stopsStraightLineSearch(op);
}

function memoryAccessDoesNotMentionRegister(op: IrOp, register: RegisterName): boolean {
  if (mentionsRegister(op, register)) return false;
  if (op.kind !== "indirect-store" && op.kind !== "indirect-recall") return true;
  return isStableIndirectSelector(op.register) && knownIndirectMemoryTarget(op) !== undefined;
}

function memoryAccessMayOverwriteRegister(op: IrOp, register: RegisterName): boolean {
  switch (op.kind) {
    case "store":
      return op.register === register;
    case "indirect-store": {
      const target = knownIndirectMemoryTarget(op);
      return target === undefined ||
        target === register ||
        (!isStableIndirectSelector(op.register) && op.register === register);
    }
    case "indirect-recall":
    case "indirect-jump":
    case "indirect-call":
    case "indirect-cjump":
      return !isStableIndirectSelector(op.register) && op.register === register;
    case "loop":
      return loopCounterRegister(op.counter) === register;
    default:
      return false;
  }
}

function mentionsRegister(op: IrOp, register: RegisterName): boolean {
  switch (op.kind) {
    case "store":
    case "recall":
    case "indirect-store":
    case "indirect-recall":
    case "indirect-jump":
    case "indirect-call":
    case "indirect-cjump":
      return op.register === register || knownIndirectMemoryTarget(op) === register;
    case "loop":
      return loopCounterRegister(op.counter) === register;
    default:
      return false;
  }
}

function stopsStraightLineSearch(op: IrOp): boolean {
  switch (op.kind) {
    case "jump":
    case "cjump":
    case "call":
    case "loop":
    case "return":
    case "stop":
    case "indirect-jump":
    case "indirect-call":
    case "indirect-cjump":
      return true;
    default:
      return false;
  }
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

export const x2HiddenTempRestore: IrPass = {
  name: "x2-hidden-temp-restore",
  run,
  layoutSafe: false,
};
