import type { AppliedOptimization, ProgramAst, StatementAst } from "./types.ts";
import { buildRuleCfg, exprIsCallFree, programStateFields, type RuleCfg } from "./rule-cfg.ts";

// Interprocedural, flow-sensitive dead-store elimination on the rule call
// graph. A pure assignment `field = expr` is removed when, on every path that
// leaves it, the field is reassigned before it is ever observed (shown, read in
// a condition, read into another expression, output, or read back as input).
// This is exactly register liveness over the whole-program CFG; the value is
// dead when its register is not live-out.

function computeLiveOut(cfg: RuleCfg, allVars: ReadonlySet<string>): Set<string>[] {
  const n = cfg.nodes.length;
  const liveIn: Set<string>[] = Array.from({ length: n }, () => new Set<string>());
  const liveOut: Set<string>[] = Array.from({ length: n }, () => new Set<string>());
  let changed = true;
  let rounds = 0;
  while (changed && rounds < 1000) {
    changed = false;
    rounds += 1;
    for (let i = n - 1; i >= 0; i -= 1) {
      const node = cfg.nodes[i]!;
      const newOut = new Set<string>();
      for (const successor of node.succ) {
        for (const reg of liveIn[successor]!) newOut.add(reg);
      }
      // A barrier (raw core / egg / unknown call) may read anything, so keep
      // every variable live across it.
      const uses = node.barrier ? allVars : node.uses;
      const defs = node.barrier ? [] : node.defs;
      const newIn = new Set<string>(uses);
      for (const reg of newOut) {
        if (!defs.includes(reg)) newIn.add(reg);
      }
      if (!setsEqual(newIn, liveIn[i]!) || !setsEqual(newOut, liveOut[i]!)) {
        liveIn[i] = newIn;
        liveOut[i] = newOut;
        changed = true;
      }
    }
  }
  return liveOut;
}

function setsEqual(a: ReadonlySet<string>, b: ReadonlySet<string>): boolean {
  if (a.size !== b.size) return false;
  for (const value of a) if (!b.has(value)) return false;
  return true;
}

function collectAllVars(cfg: RuleCfg): Set<string> {
  const vars = new Set<string>();
  for (const node of cfg.nodes) {
    for (const def of node.defs) vars.add(def);
    for (const use of node.uses) vars.add(use);
  }
  return vars;
}

function pruneStatements(statements: StatementAst[], doomed: ReadonlySet<StatementAst>): StatementAst[] {
  return statements.flatMap((statement): StatementAst[] => {
    if (doomed.has(statement)) return [];
    if (statement.kind === "loop") return [{ ...statement, body: pruneStatements(statement.body, doomed) }];
    if (statement.kind === "if") {
      const pruned: Extract<StatementAst, { kind: "if" }> = {
        ...statement,
        thenBody: pruneStatements(statement.thenBody, doomed),
      };
      if (statement.elseBody !== undefined) pruned.elseBody = pruneStatements(statement.elseBody, doomed);
      return [pruned];
    }
    if (statement.kind === "dispatch") {
      return [{
        ...statement,
        cases: statement.cases.map((branch) => ({ ...branch, body: pruneStatements(branch.body, doomed) })),
        ...(statement.defaultBody === undefined ? {} : { defaultBody: pruneStatements(statement.defaultBody, doomed) }),
      }];
    }
    return [statement];
  });
}

export function eliminateInterproceduralDeadStores(
  ast: ProgramAst,
  optimizations: AppliedOptimization[],
): number {
  const fields = programStateFields(ast);
  if (fields.size === 0) return 0;
  const cfg = buildRuleCfg(ast);
  const allVars = collectAllVars(cfg);
  const liveOut = computeLiveOut(cfg, allVars);

  const doomed = new Set<StatementAst>();
  for (const node of cfg.nodes) {
    const assign = node.assign;
    if (assign === undefined) continue;
    if (!fields.has(assign.target)) continue;
    if (!exprIsCallFree(assign.expr)) continue;
    if (liveOut[node.id]!.has(assign.target)) continue;
    doomed.add(assign);
  }
  if (doomed.size === 0) return 0;

  for (const entry of ast.entries) entry.body = pruneStatements(entry.body, doomed);
  for (const proc of ast.procs) proc.body = pruneStatements(proc.body, doomed);

  optimizations.push({
    name: "interprocedural-dead-store",
    detail: `Removed ${doomed.size} store(s) whose value is always overwritten before it can be observed across the rule call graph.`,
  });
  return doomed.size;
}
