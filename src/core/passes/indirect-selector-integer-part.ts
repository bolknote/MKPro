import { isStableIndirectSelector } from "../indirect-addressing.ts";
import type { IrOp, RegisterName } from "../types.ts";
import { emptyResult, hasRewriteBarrier, type IrPass, type IrPassFn } from "./helpers.ts";

const INT_OPCODE = 0x34;
const MARKER = /\bindirect-selector-integer-part=([A-Za-z_]\w*)\b/u;

function markedIntegerPart(op: IrOp): string | undefined {
  if (op.kind !== "indirect-recall" && op.kind !== "indirect-store") return undefined;
  if (!isStableIndirectSelector(op.register)) return undefined;
  const match = MARKER.exec(op.meta.comment ?? "");
  return match?.[1];
}

function mutatesSelectorRegister(op: IrOp, register: RegisterName): boolean {
  if (op.kind === "store" && op.register === register) return true;
  if (op.kind === "indirect-recall" && op.register === register) return true;
  if (op.kind === "indirect-store" && op.register === register) return true;
  if (op.kind === "indirect-jump" && op.register === register) return true;
  if (op.kind === "indirect-call" && op.register === register) return true;
  if (op.kind === "indirect-cjump" && op.register === register) return true;
  return false;
}

const run: IrPassFn = (ops) => {
  const integerPartRegisters = new Map<RegisterName, string>();
  const remove = new Set<number>();

  for (let index = 0; index < ops.length; index += 1) {
    const op = ops[index]!;
    if (hasRewriteBarrier(op)) {
      integerPartRegisters.clear();
      continue;
    }

    if (op.kind === "recall") {
      const source = integerPartRegisters.get(op.register);
      const next = ops[index + 1];
      if (
        source !== undefined &&
        next?.kind === "plain" &&
        next.opcode === INT_OPCODE &&
        !hasRewriteBarrier(next)
      ) {
        remove.add(index + 1);
      }
      continue;
    }

    const marked = markedIntegerPart(op);
    if (marked !== undefined && (op.kind === "indirect-recall" || op.kind === "indirect-store")) {
      integerPartRegisters.set(op.register, marked);
      continue;
    }

    for (const register of [...integerPartRegisters.keys()]) {
      if (mutatesSelectorRegister(op, register)) integerPartRegisters.delete(register);
    }
  }

  if (remove.size === 0) return emptyResult(ops);
  return {
    ops: ops.filter((_, index) => !remove.has(index)),
    applied: remove.size,
    optimizations: [
      {
        name: "indirect-selector-integer-part-reuse",
        detail: `Removed ${remove.size} redundant К [x] cell${remove.size === 1 ? "" : "s"} after a proved fractional indirect selector had already truncated its register to the integer part.`,
      },
    ],
  };
};

export const indirectSelectorIntegerPart: IrPass = {
  name: "indirect-selector-integer-part",
  run,
  layoutSafe: false,
};
