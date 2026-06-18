import type { IrOp } from "../types.ts";
import { hasRewriteBarrier, type IrPass, type IrPassFn } from "./helpers.ts";
import { createLabelAllocator } from "./outline.ts";

const LABEL_PREFIX = "__conditional_branch_trampoline_";

const run: IrPassFn = (ops, context) => {
  if (context.options.conditionalBranchTrampoline !== true) {
    return { ops: [...ops], applied: 0, optimizations: [] };
  }

  const plans = collectPlans(ops);
  if (plans.length === 0) return { ops: [...ops], applied: 0, optimizations: [] };

  const labels = createLabelAllocator(ops, LABEL_PREFIX);
  const labelByTargetIndex = new Map<number, string>();
  for (const plan of plans) {
    if (!labelByTargetIndex.has(plan.targetIndex)) labelByTargetIndex.set(plan.targetIndex, labels.next());
  }

  const retargetByIndex = new Map(plans.map((plan) => [plan.branchIndex, labelByTargetIndex.get(plan.targetIndex)!]));
  const result: IrOp[] = [];
  for (let index = 0; index < ops.length; index += 1) {
    const entryLabel = labelByTargetIndex.get(index);
    if (entryLabel !== undefined) result.push({ kind: "label", name: entryLabel, hidden: true });

    const op = ops[index]!;
    const retarget = retargetByIndex.get(index);
    if (retarget !== undefined && op.kind === "cjump") {
      result.push({
        ...op,
        target: retarget,
        targetMeta: {
          ...op.targetMeta,
          comment: "conditional branch trampoline",
        },
        meta: {
          ...op.meta,
          comment: op.meta.comment?.replace(/^false branch/u, "conditional branch trampoline") ??
            "conditional branch trampoline",
        },
      });
      continue;
    }
    result.push(op);
  }

  return {
    ops: result,
    applied: plans.length,
    optimizations: [{
      name: "conditional-branch-trampoline",
      detail: `Retargeted ${plans.length} conditional branch${plans.length === 1 ? "" : "es"} through a later identical conditional with the same destination.`,
    }],
  };
};

interface BranchTrampolinePlan {
  readonly branchIndex: number;
  readonly targetIndex: number;
}

function collectPlans(ops: readonly IrOp[]): BranchTrampolinePlan[] {
  const plans: BranchTrampolinePlan[] = [];
  for (let index = 0; index < ops.length; index += 1) {
    const branch = ops[index]!;
    if (branch.kind !== "cjump" || typeof branch.target !== "string" || hasRewriteBarrier(branch)) continue;

    const target = findLaterEquivalentConditional(ops, index, branch);
    if (target === undefined) continue;
    plans.push({ branchIndex: index, targetIndex: target });
  }
  return plans;
}

function findLaterEquivalentConditional(
  ops: readonly IrOp[],
  branchIndex: number,
  branch: Extract<IrOp, { kind: "cjump" }>,
): number | undefined {
  for (let index = branchIndex + 1; index < ops.length; index += 1) {
    const candidate = ops[index]!;
    if (candidate.kind === "label") continue;
    if (
      candidate.kind === "cjump" &&
      candidate.condition === branch.condition &&
      candidate.opcode === branch.opcode &&
      sameTarget(candidate.target, branch.target) &&
      !hasRewriteBarrier(candidate)
    ) {
      return index;
    }
  }
  return undefined;
}

function sameTarget(left: string | number, right: string | number): boolean {
  return left === right;
}

export const conditionalBranchTrampoline: IrPass = {
  name: "conditional-branch-trampoline",
  run,
  layoutSafe: false,
};
