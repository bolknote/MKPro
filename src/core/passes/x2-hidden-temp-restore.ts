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
  knownReturnCallReturnsThroughTransparentRange,
  knownIndirectFlowTarget,
  knownIndirectMemoryTarget,
  removableRecallValueRegister,
  removingRecallCanExposeX2Restore,
  x2CanUseDotRestoreAt,
  x2HasOnlyRestoreGapBeforeVp,
  x2NormalizedDecimalRestoreGapIsFreeStanding,
  x2StateHasUnsafeDotRestoreShapeX2,
  x2StateCanDiscardRestoreRunBeforeProvedVp,
  x2ValueFactIsNormalizedDecimal,
  x2ValueSetsHaveSameRestoredVisibleDecimal,
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
    const removal = analyzeRecallRemoval(ops, index, x2States[index], x2ValueStates[index]);
    if (removal === undefined) return op;
    const sourceAlreadySynced = hiddenTempStoreSourceAlreadySyncedInX2(x2ValueStates[storeIndex], x2ValueStates[index]);
    const sourceAlreadyDotSafe = hiddenTempStoreSourceAlreadyDotSafeInX2(
      x2ValueStates[storeIndex],
      x2ValueStates[index],
    );
    const sourceRestoresSameVisibleDecimal = hiddenTempStoreSourceRestoresSameVisibleDecimalFromX2(
      x2ValueStates[storeIndex],
      x2ValueStates[index],
    );
    if (removal.x2SyncRedundant !== true && !sourceAlreadySynced && !sourceRestoresSameVisibleDecimal) return op;
    if (x2StateHasUnsafeDotRestoreShapeX2(x2ValueStates[index])) return op;
    const canUseDotRestore = x2CanUseDotRestoreAt(
      ops,
      index,
      x2ValueStates[index],
      dotSafeStates[index] === true,
      immediateSyncStates[index] === true,
      directReturnContext,
    );
    const canUseNormalizedDecimalEscape = sourceAlreadyDotSafe &&
      x2NormalizedDecimalRestoreGapIsFreeStanding(ops, index, directReturnContext);
    const canUseVisibleDecimalEscape = sourceRestoresSameVisibleDecimal &&
      x2NormalizedDecimalRestoreGapIsFreeStanding(ops, index, directReturnContext);
    const canUseVpSourceEscape = sourceAlreadyDotSafe &&
      x2StateCanDiscardRestoreRunBeforeProvedVp(x2ValueStates[index], x2ValueStates[index + 1]) &&
      x2HasOnlyRestoreGapBeforeVp(ops, index + 1, directReturnContext);
    if (!canUseDotRestore && !canUseNormalizedDecimalEscape && !canUseVisibleDecimalEscape && !canUseVpSourceEscape) {
      return op;
    }
    const exposesX2Restore = sourceAlreadySynced && removal.x2SyncRedundant !== true
      ? removingRecallCanExposeX2Restore(ops, index, { redundantSyncValue: true })
      : removal.exposesX2Restore;
    if (removal.exposesStackLift || exposesX2Restore) return op;

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
  storeState: X2ValueDataflowState | undefined,
  recallState: X2ValueDataflowState | undefined,
): boolean {
  if (storeState === undefined || recallState === undefined) return false;
  for (const fact of storeState.x) {
    if (!isStableStoredSourceFact(fact)) continue;
    if (recallState.x2.has(fact)) return true;
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
    if (recallState.x2.has(fact)) return true;
  }
  return false;
}

function hiddenTempStoreSourceRestoresSameVisibleDecimalFromX2(
  storeState: X2ValueDataflowState | undefined,
  recallState: X2ValueDataflowState | undefined,
): boolean {
  return x2ValueSetsHaveSameRestoredVisibleDecimal(storeState?.x, recallState?.x2);
}

function isStableStoredSourceFact(fact: X2ValueFact): boolean {
  return fact.startsWith("expr:") || fact.startsWith("expr-key:") ||
    (fact.startsWith("decimal:") && fact.endsWith(":normalized"));
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
  return knownReturnCallReturnsThroughTransparentRange(
    ops,
    call,
    directReturnContext,
    (op) => memoryAccessDoesNotMentionRegister(op, register) && !stopsStraightLineSearch(op),
  );
}

function memoryAccessDoesNotMentionRegister(op: IrOp, register: RegisterName): boolean {
  if (mentionsRegister(op, register)) return false;
  if (op.kind !== "indirect-store" && op.kind !== "indirect-recall") return true;
  return isStableIndirectSelector(op.register) && knownIndirectMemoryTarget(op) !== undefined;
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
