import type { AssignStatementAst, ExpressionAst, ProgramAst, StatementAst } from "./types.ts";
import {
  findStateBankMember,
  numericIndexValue,
  stateBankElementForIndex,
  stateBankElementNames,
} from "./state-banks.ts";

// A statement-level, interprocedural control-flow graph over the lowered rule
// bodies (entries + procs + blocks). Rule invocations become real call edges;
// a callee's exit returns to every call site that targets it (context
// insensitive). That is conservative for both analyses built on top of it:
// liveness can only gain successors (keep more values live) and the value
// equalities can only lose facts at a wider merge, never invent a wrong one.

export interface CfgNode {
  readonly id: number;
  readonly succ: number[];
  readonly defs: readonly string[];
  readonly uses: readonly string[];
  // Plain `field = expr` assignment node (absent for every other statement).
  readonly assign?: AssignStatementAst;
  // Opaque statements (raw core / egg / call to an unknown routine). Treated as
  // reading every variable and clobbering every fact.
  readonly barrier?: boolean;
}

export interface RuleCfg {
  readonly nodes: readonly CfgNode[];
  readonly entryNode: number;
  readonly routineEntry: ReadonlyMap<string, number>;
  readonly routineExit: ReadonlyMap<string, number>;
}

export function collectExprVars(expr: ExpressionAst, out: Set<string>): void {
  switch (expr.kind) {
    case "number":
    case "string":
      return;
    case "identifier":
      out.add(expr.name);
      return;
    case "indexed":
      collectExprVars(expr.index, out);
      return;
    case "unary":
      collectExprVars(expr.expr, out);
      return;
    case "binary":
      collectExprVars(expr.left, out);
      collectExprVars(expr.right, out);
      return;
    case "call":
      for (const arg of expr.args) collectExprVars(arg, out);
      return;
  }
}

export function exprVars(expr: ExpressionAst): string[] {
  const out = new Set<string>();
  collectExprVars(expr, out);
  return [...out];
}

function collectExpressionCallNames(expr: ExpressionAst, out: string[]): void {
  switch (expr.kind) {
    case "number":
    case "string":
    case "identifier":
    case "indexed":
      if (expr.kind === "indexed") collectExpressionCallNames(expr.index, out);
      return;
    case "unary":
      collectExpressionCallNames(expr.expr, out);
      return;
    case "binary":
      collectExpressionCallNames(expr.left, out);
      collectExpressionCallNames(expr.right, out);
      return;
    case "call":
      for (const arg of expr.args) collectExpressionCallNames(arg, out);
      out.push(expr.callee);
      return;
  }
}

function expressionCallNames(expr: ExpressionAst): string[] {
  const out: string[] = [];
  collectExpressionCallNames(expr, out);
  return out;
}

// Strict purity: an expression is safe to drop or recompute only when it has no
// calls at all. This is intentionally stricter than the compiler's
// substitution purity, because calls like random() advance hidden state and
// must never be removed by dead-store elimination.
export function exprIsCallFree(expr: ExpressionAst): boolean {
  switch (expr.kind) {
    case "number":
    case "string":
    case "identifier":
      return true;
    case "indexed":
      return exprIsCallFree(expr.index);
    case "unary":
      return exprIsCallFree(expr.expr);
    case "binary":
      return exprIsCallFree(expr.left) && exprIsCallFree(expr.right);
    case "call":
      return false;
  }
}

interface Fragment {
  entry: number;
  exits: number[];
}

class Builder {
  readonly nodes: CfgNode[] = [];
  readonly routineEntry = new Map<string, number>();
  readonly routineExit = new Map<string, number>();
  private readonly ast: ProgramAst;

  constructor(ast: ProgramAst) {
    this.ast = ast;
  }

  private add(node: Omit<CfgNode, "id" | "succ"> & { succ?: number[] }): number {
    const id = this.nodes.length;
    this.nodes.push({ id, succ: node.succ ?? [], defs: node.defs, uses: node.uses, ...(node.assign ? { assign: node.assign } : {}), ...(node.barrier ? { barrier: true } : {}) });
    return id;
  }

  private link(exits: readonly number[], target: number): void {
    for (const exit of exits) {
      if (!this.nodes[exit]!.succ.includes(target)) this.nodes[exit]!.succ.push(target);
    }
  }

  build(): RuleCfg {
    const routines: Array<{ name: string; body: StatementAst[] }> = [
      ...this.ast.entries.map((entry) => ({ name: entry.name, body: entry.body })),
      ...this.ast.procs.map((proc) => ({ name: proc.name, body: proc.body })),
    ];

    for (const routine of routines) {
      this.routineEntry.set(routine.name, this.add({ defs: [], uses: [] }));
      this.routineExit.set(routine.name, this.add({ defs: [], uses: [] }));
    }

    for (const routine of routines) {
      const fragment = this.buildSequence(routine.body);
      this.link([this.routineEntry.get(routine.name)!], fragment.entry);
      this.link(fragment.exits, this.routineExit.get(routine.name)!);
    }

    const entryNode = this.ast.entries.length > 0
      ? this.routineEntry.get(this.ast.entries[0]!.name)!
      : 0;

    return {
      nodes: this.nodes,
      entryNode,
      routineEntry: this.routineEntry,
      routineExit: this.routineExit,
    };
  }

  private buildSequence(statements: readonly StatementAst[]): Fragment {
    let entry: number | undefined;
    let pending: number[] = [];
    for (const statement of statements) {
      const fragment = this.buildStatement(statement);
      if (entry === undefined) entry = fragment.entry;
      else this.link(pending, fragment.entry);
      pending = fragment.exits;
    }
    if (entry === undefined) {
      const nop = this.add({ defs: [], uses: [] });
      return { entry: nop, exits: [nop] };
    }
    return { entry, exits: pending };
  }

  private buildStatement(statement: StatementAst): Fragment {
    switch (statement.kind) {
      case "assign": {
        const callFragment = this.buildExpressionCallFragment(statement.expr, {
          defs: [statement.target],
          uses: [],
        });
        if (callFragment !== undefined) return callFragment;
        const node = this.add({ defs: [statement.target], uses: this.exprUses(statement.expr), assign: statement });
        return { entry: node, exits: [node] };
      }
      case "indexed_assign": {
        const node = this.add({
          defs: this.indexedTargetDefs(statement.target),
          uses: [...new Set([...this.exprUses(statement.expr), ...exprVars(statement.target.index)])],
        });
        return { entry: node, exits: [node] };
      }
      case "coord_list_remove": {
        const node = this.add({
          defs: statement.items,
          uses: [...new Set([...this.exprUses(statement.item), ...statement.items])],
        });
        return { entry: node, exits: [node] };
      }
      case "show": {
        const display = this.ast.displays.find((candidate) => candidate.name === statement.display);
        const node = this.add({ defs: [], uses: display?.sources ?? [] });
        return { entry: node, exits: [node] };
      }
      case "input": {
        const node = this.add({ defs: [statement.target], uses: [] });
        return { entry: node, exits: [node] };
      }
      case "pause":
      case "halt":
      case "return_value": {
        // Treat stops as falling through: on the MK-61 С/П resumes at the next
        // cell, so a value can still be read after the stop. A return reads its
        // expression operands; modeling it as fall-through keeps liveness
        // conservative (it never drops a store that the return value needs).
        const callFragment = this.buildExpressionCallFragment(statement.expr, {
          defs: [],
          uses: [],
        });
        if (callFragment !== undefined) return callFragment;
        const node = this.add({ defs: [], uses: this.exprUses(statement.expr) });
        return { entry: node, exits: [node] };
      }
      case "call": {
        const calleeEntry = this.routineEntry.get(statement.block);
        const calleeExit = this.routineExit.get(statement.block);
        if (calleeEntry === undefined || calleeExit === undefined) {
          const node = this.add({ defs: [], uses: [], barrier: true });
          return { entry: node, exits: [node] };
        }
        const node = this.add({ defs: [], uses: [], succ: [calleeEntry] });
        // The continuation after this call is reached when the callee returns,
        // so the callee's exit (not the call node) is the fragment exit.
        return { entry: node, exits: [calleeExit] };
      }
      case "core": {
        const node = this.add({ defs: [], uses: [], barrier: true });
        return { entry: node, exits: [node] };
      }
      case "if": {
        const test = this.add({ defs: [], uses: [...this.exprUses(statement.condition.left), ...this.exprUses(statement.condition.right)] });
        const thenFragment = this.buildSequence(statement.thenBody);
        this.link([test], thenFragment.entry);
        const exits = [...thenFragment.exits];
        if (statement.elseBody !== undefined) {
          const elseFragment = this.buildSequence(statement.elseBody);
          this.link([test], elseFragment.entry);
          exits.push(...elseFragment.exits);
        } else {
          exits.push(test);
        }
        return { entry: test, exits };
      }
      case "dispatch": {
        const uses = new Set<string>(this.exprUses(statement.expr));
        for (const branch of statement.cases) for (const v of this.exprUses(branch.value)) uses.add(v);
        const test = this.add({ defs: [], uses: [...uses] });
        const exits: number[] = [];
        for (const branch of statement.cases) {
          const fragment = this.buildSequence(branch.body);
          this.link([test], fragment.entry);
          exits.push(...fragment.exits);
        }
        if (statement.defaultBody !== undefined) {
          const fragment = this.buildSequence(statement.defaultBody);
          this.link([test], fragment.entry);
          exits.push(...fragment.exits);
        } else {
          exits.push(test);
        }
        return { entry: test, exits };
      }
      case "loop": {
        const fragment = this.buildSequence(statement.body);
        this.link(fragment.exits, fragment.entry);
        // Also allow falling out (conservative: the loop may terminate), so any
        // code after it stays reachable for analysis.
        return { entry: fragment.entry, exits: [...fragment.exits] };
      }
      case "while": {
        const test = this.add({ defs: [], uses: [...this.exprUses(statement.condition.left), ...this.exprUses(statement.condition.right)] });
        const fragment = this.buildSequence(statement.body);
        this.link([test], fragment.entry);
        this.link(fragment.exits, test);
        return { entry: test, exits: [test] };
      }
      case "decimal_series": {
        const node = this.add({ defs: [], uses: [], barrier: true });
        return { entry: node, exits: [node] };
      }
    }
  }

  private exprUses(expr: ExpressionAst): string[] {
    const uses = new Set<string>(exprVars(expr));
    this.collectIndexedReads(expr, uses);
    return [...uses];
  }

  private collectIndexedReads(expr: ExpressionAst, out: Set<string>): void {
    switch (expr.kind) {
      case "number":
      case "string":
      case "identifier":
        return;
      case "indexed": {
        this.collectIndexedReads(expr.index, out);
        const resolved = findStateBankMember(this.ast, expr);
        if (resolved === undefined) return;
        const constant = numericIndexValue(expr.index);
        if (constant !== undefined) {
          const element = stateBankElementForIndex(resolved.member, constant);
          if (element !== undefined) out.add(element.name);
          return;
        }
        for (const name of stateBankElementNames(resolved.member)) out.add(name);
        return;
      }
      case "unary":
        this.collectIndexedReads(expr.expr, out);
        return;
      case "binary":
        this.collectIndexedReads(expr.left, out);
        this.collectIndexedReads(expr.right, out);
        return;
      case "call":
        for (const arg of expr.args) this.collectIndexedReads(arg, out);
        return;
    }
  }

  private indexedTargetDefs(expr: Extract<ExpressionAst, { kind: "indexed" }>): string[] {
    const resolved = findStateBankMember(this.ast, expr);
    if (resolved === undefined) return [];
    const constant = numericIndexValue(expr.index);
    if (constant !== undefined) {
      const element = stateBankElementForIndex(resolved.member, constant);
      return element === undefined ? [] : [element.name];
    }
    return stateBankElementNames(resolved.member);
  }

  private buildExpressionCallFragment(
    expr: ExpressionAst,
    finalNode: Omit<CfgNode, "id" | "succ">,
  ): Fragment | undefined {
    const calls = expressionCallNames(expr);
    if (calls.length === 0) return undefined;

    const entry = this.add({ defs: [], uses: this.exprUses(expr) });
    let exits = [entry];
    for (const call of calls) {
      const calleeEntry = this.routineEntry.get(call);
      const calleeExit = this.routineExit.get(call);
      if (calleeEntry === undefined || calleeExit === undefined) {
        const barrier = this.add({ defs: [], uses: [], barrier: true });
        this.link(exits, barrier);
        exits = [barrier];
        continue;
      }
      const callNode = this.add({ defs: [], uses: [], succ: [calleeEntry] });
      this.link(exits, callNode);
      exits = [calleeExit];
    }

    const final = this.add(finalNode);
    this.link(exits, final);
    return { entry, exits: [final] };
  }
}

export function buildRuleCfg(ast: ProgramAst): RuleCfg {
  return new Builder(ast).build();
}

export function programStateFields(ast: ProgramAst): Set<string> {
  return new Set(ast.states.flatMap((state) => state.fields.map((field) => field.name)));
}
