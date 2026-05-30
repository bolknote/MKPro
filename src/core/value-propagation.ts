import type { AppliedOptimization, ExpressionAst, ProgramAst } from "./types.ts";
import { buildRuleCfg, exprIsCallFree, programStateFields, type CfgNode, type RuleCfg } from "./rule-cfg.ts";

// Interprocedural value propagation. When a recomputed expression `f = e`
// provably equals the current value of another live variable g on every path
// reaching it, rewrite `f = e` to `f = g`. The analysis tracks each variable's
// value as an affine form over the other variables, so it can prove equalities
// that need linear-arithmetic reasoning across a merge (e.g. on one path
// preview_total == player_total + turn_score, on another preview_total ==
// player_total while turn_score == 0 -- both imply preview_total ==
// player_total + turn_score).

const MAX_ROUNDS = 1000;
const MAX_TERMS = 8;
const MAX_MAGNITUDE = 1_000_000;

interface Rat {
  n: number;
  d: number;
}

function gcd(a: number, b: number): number {
  a = Math.abs(a);
  b = Math.abs(b);
  while (b !== 0) {
    [a, b] = [b, a % b];
  }
  return a === 0 ? 1 : a;
}

function rat(n: number, d = 1): Rat {
  if (d === 0 || !Number.isFinite(n) || !Number.isFinite(d)) return { n: NaN, d: 1 };
  if (d < 0) {
    n = -n;
    d = -d;
  }
  const g = gcd(n, d);
  return { n: n / g, d: d / g };
}

function ratValid(r: Rat): boolean {
  return Number.isFinite(r.n) && Number.isFinite(r.d) && r.d !== 0 &&
    Math.abs(r.n) <= MAX_MAGNITUDE && Math.abs(r.d) <= MAX_MAGNITUDE;
}

function ratAdd(a: Rat, b: Rat): Rat {
  return rat(a.n * b.d + b.n * a.d, a.d * b.d);
}

function ratMul(a: Rat, b: Rat): Rat {
  return rat(a.n * b.n, a.d * b.d);
}

function ratEq(a: Rat, b: Rat): boolean {
  return a.n === b.n && a.d === b.d;
}

const RAT_ZERO: Rat = { n: 0, d: 1 };

// constant + sum(coeff * var); zero coefficients are pruned.
interface LinForm {
  constant: Rat;
  terms: Map<string, Rat>;
}

function constForm(value: Rat): LinForm {
  return { constant: value, terms: new Map() };
}

function singleVar(name: string): LinForm {
  return { constant: RAT_ZERO, terms: new Map([[name, rat(1)]]) };
}

function formValid(form: LinForm): boolean {
  if (!ratValid(form.constant)) return false;
  if (form.terms.size > MAX_TERMS) return false;
  for (const coeff of form.terms.values()) if (!ratValid(coeff)) return false;
  return true;
}

function addForms(a: LinForm, b: LinForm): LinForm {
  const terms = new Map(a.terms);
  for (const [name, coeff] of b.terms) {
    const next = ratAdd(terms.get(name) ?? RAT_ZERO, coeff);
    if (next.n === 0) terms.delete(name);
    else terms.set(name, next);
  }
  return { constant: ratAdd(a.constant, b.constant), terms };
}

function scaleForm(form: LinForm, factor: Rat): LinForm {
  const terms = new Map<string, Rat>();
  for (const [name, coeff] of form.terms) {
    const next = ratMul(coeff, factor);
    if (next.n !== 0) terms.set(name, next);
  }
  return { constant: ratMul(form.constant, factor), terms };
}

function formsEqual(a: LinForm, b: LinForm): boolean {
  if (!ratEq(a.constant, b.constant)) return false;
  if (a.terms.size !== b.terms.size) return false;
  for (const [name, coeff] of a.terms) {
    const other = b.terms.get(name);
    if (other === undefined || !ratEq(coeff, other)) return false;
  }
  return true;
}

function decimalToRat(raw: string): Rat | undefined {
  const trimmed = raw.trim();
  if (!/^[+-]?\d+(?:\.\d+)?$/u.test(trimmed)) return undefined;
  const negative = trimmed.startsWith("-");
  const body = trimmed.replace(/^[+-]/u, "");
  const [intPart, fracPart] = body.split(".");
  if (fracPart === undefined) {
    const value = Number(body);
    return Number.isFinite(value) ? rat(negative ? -value : value) : undefined;
  }
  const denom = 10 ** fracPart.length;
  const numer = Number(intPart) * denom + Number(fracPart);
  if (!Number.isFinite(numer) || !Number.isFinite(denom)) return undefined;
  return rat(negative ? -numer : numer, denom);
}

type State = Map<string, LinForm>;

function valueOf(state: State, name: string): LinForm {
  return state.get(name) ?? singleVar(name);
}

// Affine form of `expr` evaluated under `state`. Returns undefined when the
// expression is not affine (calls, variable*variable, division by a variable).
function affineForm(expr: ExpressionAst, state: State): LinForm | undefined {
  switch (expr.kind) {
    case "string":
      return undefined;
    case "number": {
      const value = decimalToRat(expr.raw);
      return value === undefined ? undefined : constForm(value);
    }
    case "identifier":
      return valueOf(state, expr.name);
    case "unary": {
      const inner = affineForm(expr.expr, state);
      if (inner === undefined) return undefined;
      return scaleForm(inner, rat(-1));
    }
    case "binary": {
      const left = affineForm(expr.left, state);
      const right = affineForm(expr.right, state);
      if (left === undefined || right === undefined) return undefined;
      let result: LinForm | undefined;
      switch (expr.op) {
        case "+":
          result = addForms(left, right);
          break;
        case "-":
          result = addForms(left, scaleForm(right, rat(-1)));
          break;
        case "*":
          if (left.terms.size === 0) result = scaleForm(right, left.constant);
          else if (right.terms.size === 0) result = scaleForm(left, right.constant);
          else return undefined;
          break;
        case "/":
          if (right.terms.size === 0 && right.constant.n !== 0) {
            result = scaleForm(left, rat(right.constant.d, right.constant.n));
          } else {
            return undefined;
          }
          break;
      }
      return result !== undefined && formValid(result) ? result : undefined;
    }
    case "call":
      return undefined;
  }
}

// Apply `name = form` (form may be undefined to mean "unknown") to a copy of
// `state`, preserving the invariant that every stored form references only free
// variables (variables that are not themselves keys).
function applyDef(state: State, name: string, form: LinForm | undefined): State {
  const next: State = new Map();
  for (const [key, value] of state) {
    if (key === name) continue;
    if (value.terms.has(name)) continue; // stale: it referenced the redefined var
    next.set(key, value);
  }
  if (form !== undefined && !form.terms.has(name) && formValid(form)) {
    next.set(name, form);
  }
  return next;
}

function transfer(state: State, node: CfgNode): State {
  if (node.barrier) return new Map();
  const assign = node.assign;
  if (assign !== undefined) {
    const form = exprIsCallFree(assign.expr) ? affineForm(assign.expr, state) : undefined;
    return applyDef(state, assign.target, form);
  }
  if (node.defs.length > 0) {
    let next = state;
    for (const def of node.defs) next = applyDef(next, def, undefined);
    return next;
  }
  return state;
}

function holdsIn(state: State, name: string, form: LinForm): boolean {
  const lhs = valueOf(state, name);
  let rhs: LinForm = constForm(form.constant);
  for (const [term, coeff] of form.terms) {
    rhs = addForms(rhs, scaleForm(valueOf(state, term), coeff));
  }
  return formsEqual(lhs, rhs);
}

function meet(states: Array<State | undefined>): State | undefined {
  const reached = states.filter((state): state is State => state !== undefined);
  if (reached.length === 0) return undefined;
  const result: State = new Map();
  for (const state of reached) {
    for (const [name, form] of state) {
      if (result.has(name)) continue;
      if (reached.every((other) => holdsIn(other, name, form))) {
        result.set(name, form);
      }
    }
  }
  return result;
}

function statesEqual(a: State | undefined, b: State | undefined): boolean {
  if (a === undefined || b === undefined) return a === b;
  if (a.size !== b.size) return false;
  for (const [name, form] of a) {
    const other = b.get(name);
    if (other === undefined || !formsEqual(form, other)) return false;
  }
  return true;
}

function buildPredecessors(cfg: RuleCfg): number[][] {
  const preds: number[][] = Array.from({ length: cfg.nodes.length }, () => []);
  for (const node of cfg.nodes) {
    for (const successor of node.succ) preds[successor]!.push(node.id);
  }
  return preds;
}

function expressionCost(expr: ExpressionAst): number {
  switch (expr.kind) {
    case "number":
    case "string":
    case "identifier":
      return 1;
    case "indexed":
      return 2 + expressionCost(expr.index);
    case "unary":
      return 1 + expressionCost(expr.expr);
    case "binary":
      return 1 + expressionCost(expr.left) + expressionCost(expr.right);
    case "call":
      return 100;
  }
}

export function propagateValuesInterprocedurally(
  ast: ProgramAst,
  optimizations: AppliedOptimization[],
): number {
  const fields = programStateFields(ast);
  if (fields.size === 0) return 0;
  const cfg = buildRuleCfg(ast);
  const preds = buildPredecessors(cfg);
  const n = cfg.nodes.length;
  const out: Array<State | undefined> = Array.from({ length: n }, () => undefined);

  let changed = true;
  let rounds = 0;
  while (changed && rounds < MAX_ROUNDS) {
    changed = false;
    rounds += 1;
    for (let i = 0; i < n; i += 1) {
      const incoming = preds[i]!.map((p) => out[p]);
      if (i === cfg.entryNode) incoming.push(new Map());
      const inState = meet(incoming);
      const newOut = inState === undefined ? undefined : transfer(inState, cfg.nodes[i]!);
      if (!statesEqual(newOut, out[i])) {
        out[i] = newOut;
        changed = true;
      }
    }
  }
  if (rounds >= MAX_ROUNDS) return 0; // did not converge: stay safe and rewrite nothing

  let rewrites = 0;
  for (const node of cfg.nodes) {
    const assign = node.assign;
    if (assign === undefined) continue;
    if (!exprIsCallFree(assign.expr)) continue;
    if (assign.expr.kind !== "binary" && assign.expr.kind !== "unary") continue;
    const incoming = preds[node.id]!.map((p) => out[p]);
    if (node.id === cfg.entryNode) incoming.push(new Map());
    const inState = meet(incoming);
    if (inState === undefined) continue;
    const target = affineForm(assign.expr, inState);
    if (target === undefined) continue;
    let replacement: string | undefined;
    for (const candidate of fields) {
      if (candidate === assign.target) continue;
      if (formsEqual(valueOf(inState, candidate), target) && formValid(target)) {
        replacement = candidate;
        break;
      }
    }
    if (replacement === undefined) continue;
    if (expressionCost(assign.expr) <= 1) continue;
    assign.expr = { kind: "identifier", name: replacement };
    rewrites += 1;
  }
  if (rewrites === 0) return 0;
  optimizations.push({
    name: "interprocedural-value-propagation",
    detail: `Replaced ${rewrites} recomputed expression(s) with an equal live variable proved equivalent on every path.`,
  });
  return rewrites;
}
