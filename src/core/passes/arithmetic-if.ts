import type { IrOp } from "../types.ts";
import { type IrPass, type IrPassFn } from "./helpers.ts";

const run: IrPassFn = (ops) => {
  // Arithmetic-if rewriting currently happens at the AST level via the V2/V1
  // compileArithmeticIfSelect path. This pass is an architectural placeholder so
  // that future cost-gated branchless rewrites can see the IR post-optimization
  // size and decide whether the branchless form remains profitable.
  return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
};

export const arithmeticIfPass: IrPass = {
  name: "arithmetic-if-pass",
  run,
  layoutSafe: false,
};

export type { IrOp };
