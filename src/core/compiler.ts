import {
  addressToOpcode,
  findOpcodeName,
  formatAddress,
  getOpcode,
  registerFromText,
  registerIndex,
} from "./opcodes.ts";
import {
  formalAddressInfo,
  formatFormalAddressOpcode,
  parseFormalAddressOpcode,
} from "./formal-address.ts";
import { foldProgramConstants } from "./constant-folder.ts";
import { normalizeV2ExpressionText, parseExpression, parseProgram } from "./parser.ts";
import { runIrPasses } from "./passes/index.ts";
import { optimizePostLayoutIndirectFlow } from "./post-layout-indirect-flow.ts";
import { verifySuperDarkSuffixLayout } from "./super-dark-layout.ts";
import { MK61_PROFILE, machineSupports, type MachineProfile } from "./machineProfile.ts";
import type {
  AppliedOptimization,
  BudgetReport,
  CandidateReport,
  CellRole,
  CellRoleReport,
  CompileOptions,
  CompileReport,
  CompileResult,
  ConditionAst,
  Diagnostic,
  DispatchCaseAst,
  ExpressionAst,
  MachineAddressRef,
  MachineFeatureUseReport,
  MachineItem,
  MachineOp,
  OptimizerCapabilityReport,
  OptimizerReport,
  LayoutIrCell,
  PreloadReport,
  ProgramAst,
  ReferenceReport,
  RegisterName,
  ResolvedStep,
  StateFieldAst,
  StatementAst,
  SwitchStatementAst,
  TrapStatementAst,
  StorageHint,
  V2BoardAst,
  V2PredicateAst,
  V2StatementAst,
} from "./types.ts";

const DEFAULT_OPTIONS: CompileOptions = {
  delivery: "hex",
  budget: 105,
  analysis: false,
};

interface NodeFsModule {
  existsSync(path: string): boolean;
  readFileSync(path: string, encoding: "utf8"): string;
}

interface NodePathModule {
  resolve(...paths: string[]): string;
  dirname(path: string): string;
}

interface NodeProcessLike {
  cwd?: () => string;
  getBuiltinModule?: (specifier: string) => unknown;
}

const REGISTER_ORDER: RegisterName[] = [
  "0",
  "1",
  "2",
  "3",
  "4",
  "5",
  "6",
  "7",
  "8",
  "9",
  "a",
  "b",
  "c",
  "d",
  "e",
];

const SWITCH_SCRATCH_PREFIX = "__switch_";
const DISPATCH_SCRATCH_PREFIX = "__dispatch_";
const TICTACTOE_MASK_SCRATCH_PREFIX = "__ttt_mask_";
const BIT_MASK_SCRATCH_PREFIX = "__bit_mask_";
const IF_SELECTOR_SCRATCH_PREFIX = "__if_selector_";
const CELL_MAP_PREFIX = "__cell_map_";
const SPATIAL_HIT_SCRATCH_PREFIX = "__spatial_hit_";
const SPATIAL_COUNT_SCRATCH_PREFIX = "__spatial_count_";
const NEGATIVE_ZERO_DEGREE_SELECTOR_GE = "__mkpro_negative_zero_ge";
const NEGATIVE_ZERO_DEGREE_PRELOAD_VALUE = "1|-00";
const INTERNAL_NAME_PREFIX = "__mkpro_";
const DISPLAY_HELPER_MIN_SAVINGS = 4;
const EXPRESSION_HELPER_MIN_COST = 8;
const EXPRESSION_HELPER_MIN_SAVINGS = 4;

export class CompileError extends Error {
  readonly diagnostics: Diagnostic[];

  constructor(diagnostics: Diagnostic[]) {
    super(diagnostics.map((diagnostic) => diagnostic.message).join("\n"));
    this.diagnostics = diagnostics;
  }
}

interface LoweringOptions {
  aggressiveTerminalDirect?: boolean;
}

export function compileMKPro(
  source: string,
  options: Partial<CompileOptions> = {},
): CompileResult {
  const primary = compileMKProOnce(source, options, {});
  let aggressive: CompileResult | undefined;
  try {
    aggressive = compileMKProOnce(source, options, { aggressiveTerminalDirect: true });
  } catch {
    aggressive = undefined;
  }
  if (aggressive !== undefined && aggressive.steps.length < primary.steps.length) {
    aggressive.report.optimizations.push({
      name: "late-layout-if-variant",
      detail: `Selected aggressive terminal-if lowering after full layout (${aggressive.steps.length} vs ${primary.steps.length} cells).`,
    });
    return aggressive;
  }
  return primary;
}

function compileMKProOnce(
  source: string,
  options: Partial<CompileOptions>,
  loweringOptions: LoweringOptions,
): CompileResult {
  const ast = parseProgram(source);
  const opts: CompileOptions = { ...DEFAULT_OPTIONS, ...options };
  const machineProfile = MK61_PROFILE;
  const diagnostics: Diagnostic[] = [];
  const optimizations: AppliedOptimization[] = [];
  const warnings: string[] = [];
  const candidates: CandidateReport[] = [];

  const foldedConstants = foldProgramConstants(ast);
  if (foldedConstants > 0) {
    optimizations.push({
      name: "expression-constant-folder",
      detail: `Folded ${foldedConstants} constant expression node(s) before code generation.`,
    });
  }
  eliminateUnobservedState(ast, optimizations);
  hoistCommonBranchTails(ast, optimizations);
  validateSemanticDomains(ast, diagnostics);
  validateV2Intent(ast, diagnostics);
  validateReservedInternalNames(ast, diagnostics);
  if (diagnostics.some((diagnostic) => diagnostic.level === "error")) {
    throw new CompileError(diagnostics);
  }
  if (ast.v2) {
    optimizations.push({
      name: "intent-domain-lowering",
      detail: `Lowered ${ast.v2.state.length} state fields and ${ast.v2.rules.length} rules through the generic intent pipeline.`,
    });
    const sharedChallengeHelpers = ast.procs.filter((proc) => proc.name.startsWith("encounter_effects_")).length;
    if (sharedChallengeHelpers > 0) {
      optimizations.push({
        name: "shared-challenge-effect-lowering",
        detail: `Collapsed ${sharedChallengeHelpers} repeated encounter challenge/effect table(s) into shared formula-driven helper logic.`,
      });
    }
  }

  const allocation = allocateRegisters(ast, diagnostics);
  const context = new EmitContext(
    ast,
    allocation,
    opts,
    machineProfile,
    diagnostics,
    optimizations,
    warnings,
    candidates,
    loweringOptions,
  );

  context.compileProgram();
  const optimizedResult = optimizeItems(context.items, opts, optimizations);
  const postLayoutFlow = optimizePostLayoutIndirectFlow(
    optimizedResult.items,
    opts,
    opts.indirectFlowRescueAbove,
  );
  const optimized = postLayoutFlow.items;
  optimizations.push(...postLayoutFlow.optimizations);
  const preloads = [
    ...buildPreloadReport(ast, allocation),
    ...buildNegativeZeroDegreePreloadReport(allocation, optimizations),
    ...optimizedResult.preloads,
    ...postLayoutFlow.preloads,
  ];
  appendOptimizationCandidateReports(optimizations, candidates);
  const { steps, labels, cellRoles } = layoutProgram(optimized, diagnostics, opts, ast, machineProfile);
  const largestBlocks = summarizeBlocks(optimized);
  const referenceResult = ast.reference === undefined
    ? undefined
    : buildReferenceReport(ast.reference, steps.length, opts.budget);
  if (referenceResult?.warning !== undefined) warnings.push(referenceResult.warning);

  if (steps.length > opts.budget) {
    diagnostics.push({
      level: opts.analysis ? "warning" : "error",
      code: "BUDGET_EXCEEDED",
      message: `Program uses ${steps.length} steps, budget is ${opts.budget}. Largest blocks: ${largestBlocks.join(", ")}`,
    });
  }

  if (diagnostics.some((diagnostic) => diagnostic.level === "error")) {
    throw new CompileError(diagnostics);
  }

  const report: CompileReport = {
    steps: steps.length,
    budget: opts.budget,
    machine: machineProfile.id,
    registers: visiblePublicRegisters(allocation.registers),
    labels,
    optimizations,
    warnings,
    delivery: opts.delivery,
    optimizer: buildOptimizerReport(ast, opts, optimizations, candidates, cellRoles, machineProfile),
    preloads,
    ...(referenceResult?.report === undefined ? {} : { reference: referenceResult.report }),
    ir: buildIrReport(ast, optimized, steps.length),
    cellRoles,
    candidates,
    budgetReport: buildBudgetReport(steps.length, opts.budget, largestBlocks, 0),
    machineFeaturesUsed: buildMachineFeaturesUsed(machineProfile, optimizations, cellRoles, candidates),
    proofs: buildProofReport(ast, optimized, cellRoles, opts, optimizations, preloads),
    emulatorFacts: machineProfile.emulatorFacts,
    rejectedCandidates: candidates
      .filter((candidate) => !candidate.selected)
      .map((candidate) => ({
        site: candidate.site,
        variant: candidate.variant,
        reason: candidate.reason,
        steps: candidate.steps,
      })),
    hotBlocks: largestBlocks.map(parseHotBlock),
  };

  return { ast, items: optimized, steps, report, diagnostics };
}

function visiblePublicRegisters(
  all: Record<string, RegisterName>,
): Record<string, RegisterName> {
  const result: Record<string, RegisterName> = {};
  for (const [name, register] of Object.entries(all)) {
    if (
      !name.startsWith(SWITCH_SCRATCH_PREFIX) &&
      !name.startsWith(DISPATCH_SCRATCH_PREFIX) &&
      !name.startsWith(TICTACTOE_MASK_SCRATCH_PREFIX) &&
      !name.startsWith(BIT_MASK_SCRATCH_PREFIX) &&
      !name.startsWith(IF_SELECTOR_SCRATCH_PREFIX) &&
      !name.startsWith(CELL_MAP_PREFIX) &&
      !name.startsWith(SPATIAL_HIT_SCRATCH_PREFIX) &&
      !name.startsWith(SPATIAL_COUNT_SCRATCH_PREFIX)
    ) {
      result[name] = register;
    }
  }
  return result;
}

function eliminateUnobservedState(ast: ProgramAst, optimizations: AppliedOptimization[]): void {
  const stateFields = new Set(ast.states.flatMap((state) => state.fields.map((field) => field.name)));
  if (stateFields.size === 0) return;
  const externallyRead = new Set<string>();
  const inputTargets = new Set<string>();
  const assigned = new Map<string, ExpressionAst[]>();

  const addRead = (name: string): void => {
    if (stateFields.has(name)) externallyRead.add(name);
  };
  const visitExpr = (expr: ExpressionAst, ignored?: string): void => {
    if (expr.kind === "identifier") {
      if (expr.name !== ignored) addRead(expr.name);
      return;
    }
    if (expr.kind === "unary") visitExpr(expr.expr, ignored);
    if (expr.kind === "binary") {
      visitExpr(expr.left, ignored);
      visitExpr(expr.right, ignored);
    }
    if (expr.kind === "call") {
      for (const arg of expr.args) visitExpr(arg, ignored);
    }
  };
  const visitStatements = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "pause" || statement.kind === "halt") visitExpr(statement.expr);
      if (statement.kind === "ask") {
        inputTargets.add(statement.target);
        if (statement.prompt) visitExpr(statement.prompt);
      }
      if (statement.kind === "input") inputTargets.add(statement.target);
      if (statement.kind === "assign") {
        if (!assigned.has(statement.target)) assigned.set(statement.target, []);
        assigned.get(statement.target)!.push(statement.expr);
        visitExpr(statement.expr, statement.target);
      }
      if (statement.kind === "show") {
        const display = ast.displays.find((candidate) => candidate.name === statement.display);
        for (const source of display?.sources ?? []) addRead(source);
      }
      if (statement.kind === "if") {
        visitExpr(statement.condition.left);
        visitExpr(statement.condition.right);
        visitStatements(statement.thenBody);
        if (statement.elseBody) visitStatements(statement.elseBody);
      }
      if (statement.kind === "loop") visitStatements(statement.body);
      if (statement.kind === "switch") {
        visitExpr(statement.expr);
        for (const switchCase of statement.cases) {
          visitExpr(switchCase.value);
          visitStatements(switchCase.body);
        }
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        visitExpr(statement.expr);
        for (const dispatchCase of statement.cases) {
          visitExpr(dispatchCase.value);
          visitStatements(dispatchCase.body);
        }
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
      if (statement.kind === "core") {
        for (const input of statement.inputs ?? []) visitExpr(input.expr);
        for (const output of statement.outputs ?? []) addRead(output.target);
      }
      if (statement.kind === "trap") visitExpr(statement.expr);
    }
  };

  for (const entry of ast.entries) visitStatements(entry.body);
  for (const proc of ast.procs) visitStatements(proc.body);
  for (const block of ast.blocks) visitStatements(block.body);

  const removable = new Set<string>();
  for (const state of ast.states) {
    for (const field of state.fields) {
      if (externallyRead.has(field.name) || inputTargets.has(field.name)) continue;
      if (field.initial !== undefined && !expressionPureForSubstitution(field.initial)) continue;
      const writes = assigned.get(field.name) ?? [];
      if (writes.every(expressionPureForSubstitution)) removable.add(field.name);
    }
  }
  if (removable.size === 0) return;

  for (const state of ast.states) {
    state.fields = state.fields.filter((field) => !removable.has(field.name));
  }
  const pruneStatements = (statements: StatementAst[]): StatementAst[] =>
    statements.flatMap((statement): StatementAst[] => {
      if (statement.kind === "assign" && removable.has(statement.target)) return [];
      if (statement.kind === "loop") return [{ ...statement, body: pruneStatements(statement.body) }];
      if (statement.kind === "if") {
        const pruned: Extract<StatementAst, { kind: "if" }> = {
          ...statement,
          thenBody: pruneStatements(statement.thenBody),
        };
        if (statement.elseBody !== undefined) pruned.elseBody = pruneStatements(statement.elseBody);
        return [pruned];
      }
      if (statement.kind === "switch") {
        return [{
          ...statement,
          cases: statement.cases.map((switchCase) => ({ ...switchCase, body: pruneStatements(switchCase.body) })),
          ...(statement.defaultBody === undefined ? {} : { defaultBody: pruneStatements(statement.defaultBody) }),
        }];
      }
      if (statement.kind === "dispatch") {
        return [{
          ...statement,
          cases: statement.cases.map((dispatchCase) => ({ ...dispatchCase, body: pruneStatements(dispatchCase.body) })),
          ...(statement.defaultBody === undefined ? {} : { defaultBody: pruneStatements(statement.defaultBody) }),
        }];
      }
      return [statement];
    });
  for (const entry of ast.entries) entry.body = pruneStatements(entry.body);
  for (const proc of ast.procs) proc.body = pruneStatements(proc.body);
  for (const block of ast.blocks) block.body = pruneStatements(block.body);
  optimizations.push({
    name: "dead-state-elimination",
    detail: `Removed ${removable.size} unobserved state field${removable.size === 1 ? "" : "s"} before register allocation.`,
  });
}

function hoistCommonBranchTails(ast: ProgramAst, optimizations: AppliedOptimization[]): void {
  let hoisted = 0;
  let simplified = 0;

  const visitList = (statements: StatementAst[]): StatementAst[] => {
    const result: StatementAst[] = [];
    for (const statement of statements) {
      result.push(...visitStatement(statement));
    }
    return result;
  };

  const visitStatement = (statement: StatementAst): StatementAst[] => {
    if (statement.kind === "loop") {
      return [{ ...statement, body: visitList(statement.body) }];
    }
    if (statement.kind === "if") {
      const thenBody = visitList(statement.thenBody);
      const elseBody = statement.elseBody === undefined ? undefined : visitList(statement.elseBody);
      const tails: StatementAst[] = [];
      if (elseBody !== undefined) {
        while (
          thenBody.length > 0 &&
          elseBody.length > 0 &&
          statementEquals(thenBody[thenBody.length - 1]!, elseBody[elseBody.length - 1]!)
        ) {
          tails.unshift(thenBody.pop()!);
          elseBody.pop();
        }
      }
      hoisted += tails.length;

      const simplifiedBranch = simplifyIfStatement({
        ...statement,
        thenBody,
        ...(elseBody === undefined ? {} : { elseBody }),
      });
      if (simplifiedBranch.length !== 1 || !statementEquals(simplifiedBranch[0]!, {
        ...statement,
        thenBody,
        ...(elseBody === undefined ? {} : { elseBody }),
      })) {
        simplified += 1;
      }
      return [...simplifiedBranch, ...tails];
    }
    if (statement.kind === "switch") {
      return [{
        ...statement,
        cases: statement.cases.map((switchCase) => ({ ...switchCase, body: visitList(switchCase.body) })),
        ...(statement.defaultBody === undefined ? {} : { defaultBody: visitList(statement.defaultBody) }),
      }];
    }
    if (statement.kind === "dispatch") {
      return [{
        ...statement,
        cases: statement.cases.map((dispatchCase) => ({ ...dispatchCase, body: visitList(dispatchCase.body) })),
        ...(statement.defaultBody === undefined ? {} : { defaultBody: visitList(statement.defaultBody) }),
      }];
    }
    return [statement];
  };

  for (const entry of ast.entries) entry.body = visitList(entry.body);
  for (const proc of ast.procs) proc.body = visitList(proc.body);
  for (const block of ast.blocks) block.body = visitList(block.body);

  if (hoisted > 0 || simplified > 0) {
    optimizations.push({
      name: "common-branch-tail-hoisting",
      detail: `Hoisted ${hoisted} shared branch tail${hoisted === 1 ? "" : "s"} and simplified ${simplified} conditional shape${simplified === 1 ? "" : "s"}.`,
    });
  }
}

function simplifyIfStatement(statement: Extract<StatementAst, { kind: "if" }>): StatementAst[] {
  if (statement.elseBody !== undefined && statement.thenBody.length === 0 && statement.elseBody.length === 0) {
    return [];
  }
  if (statement.elseBody !== undefined && statement.thenBody.length === 0) {
    const { elseBody, ...rest } = statement;
    return [{
      ...rest,
      condition: invertCondition(statement.condition),
      thenBody: elseBody,
    }];
  }
  if (statement.elseBody !== undefined && statement.elseBody.length === 0) {
    const rest = { ...statement };
    delete rest.elseBody;
    return [{
      ...rest,
    }];
  }
  return [statement];
}

function invertCondition(condition: ConditionAst): ConditionAst {
  return {
    ...condition,
    op: invertComparisonOp(condition.op),
  };
}

function invertComparisonOp(op: ConditionAst["op"]): ConditionAst["op"] {
  switch (op) {
    case "==":
      return "!=";
    case "!=":
      return "==";
    case "<":
      return ">=";
    case "<=":
      return ">";
    case ">":
      return "<=";
    case ">=":
      return "<";
  }
}

function appendOptimizationCandidateReports(
  optimizations: readonly AppliedOptimization[],
  candidates: CandidateReport[],
): void {
  const selectedPassCandidates: Array<[string, string, number]> = [
    ["stable-indirect-flow", "stable-register indirect branch/call selected by IR data-flow proof", 1],
    ["preloaded-indirect-flow", "compiler-owned address preload selected for one-cell indirect branch/call", 1],
    ["preloaded-super-dark-flow", "compiler-owned FA..FF preloaded one-command dispatch selected after layout proof", 1],
    ["indirect-memory-table", "stable selector reused for indirect memory access", 0],
    ["r0-fractional-sentinel", "fractional R0 selector side effect reused after liveness proof", 0],
  ];
  for (const [name, reason, steps] of selectedPassCandidates) {
    if (!optimizations.some((optimization) => optimization.name === name)) continue;
    if (candidates.some((candidate) => candidate.variant === name && candidate.selected)) continue;
    candidates.push({
      site: "ir-pass",
      variant: name,
      steps,
      selected: true,
      reason,
    });
  }
}

interface ReferenceMetrics {
  readonly span: number;
  readonly entries: number;
  readonly gaps: string[];
}

type ReferenceMetricsResolver = (referenceName: string) => ReferenceMetrics | undefined;

let customReferenceMetricsResolver: ReferenceMetricsResolver | undefined;

export function setReferenceMetricsResolver(
  resolver: ReferenceMetricsResolver | undefined,
): void {
  customReferenceMetricsResolver = resolver;
}

function buildReferenceReport(
  referenceName: string,
  compiledSteps: number,
  fallbackBudget: number,
): { report: ReferenceReport; warning?: string } {
  const metrics = resolveReferenceMetrics(referenceName);
  const referenceSteps = metrics?.span ?? fallbackBudget;
  const report = {
    name: referenceName,
    referenceSteps,
    referenceSpan: referenceSteps,
    referenceEntries: metrics?.entries ?? referenceSteps,
    referenceGaps: metrics?.gaps ?? [],
    compiledSteps,
    delta: compiledSteps - referenceSteps,
    parity: compiledSteps < referenceSteps ? "smaller" : compiledSteps === referenceSteps ? "equal" : "larger",
  } satisfies CompileReport["reference"];
  const warning = metrics === undefined
    ? `Reference '${referenceName}' was not found under games/*; using budget ${fallbackBudget} as reference size.`
    : undefined;
  return warning === undefined ? { report } : { report, warning };
}

function resolveReferenceMetrics(referenceName: string): ReferenceMetrics | undefined {
  const customMetrics = customReferenceMetricsResolver?.(referenceName);
  if (customMetrics !== undefined) return customMetrics;

  const fs = nodeBuiltin<NodeFsModule>("node:fs");
  const path = nodeBuiltin<NodePathModule>("node:path");
  const repoRoot = findRepoRoot(fs, path);
  if (fs === undefined || path === undefined || repoRoot === undefined) return undefined;

  const reference = /^([A-Za-z0-9]+)_(.+)$/u.exec(referenceName);
  if (!reference) return undefined;
  const collection = reference[1]!;
  const slug = reference[2]!.replace(/_/gu, "-");
  const directory = path.resolve(repoRoot, "games", collection);
  const manifestPath = path.resolve(directory, "manifest.tsv");
  let programFile = `${slug}.txt`;

  if (fs.existsSync(manifestPath)) {
    const rows = fs.readFileSync(manifestPath, "utf8").split(/\r?\n/u).slice(1);
    const manifestProgram = rows
      .map((row) => row.split("\t")[0]?.trim())
      .find((program) => program === programFile);
    if (manifestProgram !== undefined) programFile = manifestProgram;
  }

  const programPath = path.resolve(directory, programFile);
  if (!fs.existsSync(programPath)) return undefined;
  return readReferenceListingMetrics(programPath, fs);
}

function nodeBuiltin<T>(specifier: string): T | undefined {
  const processLike = (globalThis as typeof globalThis & { process?: NodeProcessLike }).process;
  const value = processLike?.getBuiltinModule?.(specifier);
  if (value !== undefined) return value as T;
  if (specifier.startsWith("node:")) {
    return processLike?.getBuiltinModule?.(specifier.slice("node:".length)) as T | undefined;
  }
  return undefined;
}

function findRepoRoot(
  fs: NodeFsModule | undefined,
  path: NodePathModule | undefined,
): string | undefined {
  const cwd = (globalThis as typeof globalThis & { process?: NodeProcessLike }).process?.cwd?.();
  if (fs === undefined || path === undefined || cwd === undefined) return undefined;

  let current = path.resolve(cwd);
  for (let depth = 0; depth < 8; depth += 1) {
    if (
      fs.existsSync(path.resolve(current, "package.json")) &&
      fs.existsSync(path.resolve(current, "games"))
    ) {
      return current;
    }
    const parent = path.dirname(current);
    if (parent === current) break;
    current = parent;
  }
  return undefined;
}

function readReferenceListingMetrics(path: string, fs: NodeFsModule): ReferenceMetrics | undefined {
  const addresses = fs.readFileSync(path, "utf8")
    .split(/\r?\n/u)
    .map((line) => line.trim())
    .filter(Boolean)
    .map((line) => line.split(/\s+/u)[0] ?? "")
    .filter((address) => /^[0-9]{2}$|^A[0-4]$/u.test(address))
    .map(parseReferenceAddress);
  if (addresses.length === 0) return undefined;
  const maxAddress = Math.max(...addresses);
  const occupied = new Set(addresses);
  const gaps: string[] = [];
  for (let address = 0; address <= maxAddress; address += 1) {
    if (!occupied.has(address)) gaps.push(formatAddress(address));
  }
  return {
    span: maxAddress + 1,
    entries: addresses.length,
    gaps,
  };
}

function parseReferenceAddress(text: string): number {
  if (/^[0-9]{2}$/u.test(text)) return Number(text);
  const extended = /^A([0-4])$/u.exec(text);
  if (extended) return 100 + Number(extended[1]);
  throw new Error(`Invalid MK-61 reference address '${text}'.`);
}

function validateSemanticDomains(ast: ProgramAst, diagnostics: Diagnostic[]): void {
  const unresolved = ast.domains.filter((domain) =>
    ["cache_search", "fight", "table"].includes(domain.domainKind),
  );
  if (unresolved.length === 0) return;
  diagnostics.push({
    level: "error",
    code: "SEMANTIC_DOMAIN_LOWERER_MISSING",
    message:
      `High-level semantic domains need real rule lowerers before code generation: ${unresolved.map(formatDomainName).join(", ")}. ` +
      "The compiler refuses to treat game rules as comments.",
  });
}

function validateV2Intent(ast: ProgramAst, diagnostics: Diagnostic[]): void {
  const v2 = ast.v2;
  if (!v2) return;
  const unsupported = collectUnsupportedV2Statements(v2);
  if (unsupported.length === 0) return;
  diagnostics.push({
    level: "error",
    code: "V2_SEMANTIC_LOWERER_MISSING",
    message:
      `MK-Pro source contains effects that need real rule lowerers before code generation: ` +
      `${unsupported.slice(0, 8).map((item) => `${item.text} (line ${item.line})`).join(", ")}. ` +
      "The compiler refuses to treat human-level semantics as comments.",
  });
}

function validateReservedInternalNames(ast: ProgramAst, diagnostics: Diagnostic[]): void {
  const seen = new Set<string>();
  const report = (name: string, line?: number): void => {
    if (!name.toLowerCase().startsWith(INTERNAL_NAME_PREFIX)) return;
    const key = `${line ?? 0}:${name}`;
    if (seen.has(key)) return;
    seen.add(key);
    diagnostics.push(buildDiagnostic(
      "error",
      `Name '${name}' uses reserved compiler-internal prefix '${INTERNAL_NAME_PREFIX}'.`,
      line,
    ));
  };
  const visitExpr = (expr: ExpressionAst): void => {
    if (expr.kind === "identifier") report(expr.name);
    if (expr.kind === "unary") visitExpr(expr.expr);
    if (expr.kind === "binary") {
      visitExpr(expr.left);
      visitExpr(expr.right);
    }
    if (expr.kind === "call") {
      report(expr.callee);
      for (const arg of expr.args) visitExpr(arg);
    }
  };
  const visitCondition = (condition: ConditionAst): void => {
    visitExpr(condition.left);
    visitExpr(condition.right);
  };
  const visitStatements = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "assign") {
        report(statement.target, statement.line);
        visitExpr(statement.expr);
      }
      if (statement.kind === "pause" || statement.kind === "halt") visitExpr(statement.expr);
      if (statement.kind === "ask") {
        report(statement.target, statement.line);
        if (statement.prompt) visitExpr(statement.prompt);
      }
      if (statement.kind === "show") report(statement.display, statement.line);
      if (statement.kind === "call") report(statement.block, statement.line);
      if (statement.kind === "trap") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitCondition(statement.condition);
        visitStatements(statement.thenBody);
        if (statement.elseBody) visitStatements(statement.elseBody);
      }
      if (statement.kind === "loop") visitStatements(statement.body);
      if (statement.kind === "switch") {
        visitExpr(statement.expr);
        for (const switchCase of statement.cases) {
          visitExpr(switchCase.value);
          visitStatements(switchCase.body);
        }
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        visitExpr(statement.expr);
        for (const dispatchCase of statement.cases) {
          visitExpr(dispatchCase.value);
          visitStatements(dispatchCase.body);
        }
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
    }
  };

  for (const declaration of ast.declarations) {
    report(declaration.name, declaration.line);
    if (declaration.kind === "const" || declaration.kind === "store") {
      if (declaration.value) visitExpr(declaration.value);
    }
  }
  for (const state of ast.states) {
    report(state.name, state.line);
    for (const field of state.fields) {
      report(field.name, field.line);
      if (field.initial) visitExpr(field.initial);
    }
  }
  for (const display of ast.displays) {
    report(display.name, display.line);
    for (const source of display.sources) report(source, display.line);
  }
  for (const entry of ast.entries) {
    report(entry.name, entry.line);
    visitStatements(entry.body);
  }
  for (const proc of ast.procs) {
    report(proc.name, proc.line);
    visitStatements(proc.body);
  }
  for (const block of ast.blocks) {
    report(block.name, block.line);
    visitStatements(block.body);
  }
}

function collectUnsupportedV2Statements(ast: NonNullable<ProgramAst["v2"]>): Array<{ text: string; line: number }> {
  const unsupported: Array<{ text: string; line: number }> = [];
  const visit = (statements: V2StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "v2_assign" && !isSimpleCompilerExpression(statement.expr)) {
        unsupported.push({ text: `${statement.target} = ${statement.expr}`, line: statement.line });
      }
      if (statement.kind === "v2_update" && !isSimpleCompilerExpression(statement.expr)) {
        unsupported.push({ text: `${statement.target} ${statement.op} ${statement.expr}`, line: statement.line });
      }
      if (statement.kind === "v2_if") {
        if (!isLowerableV2Predicate(statement.predicate)) {
          unsupported.push({ text: `if ${formatV2Predicate(statement.predicate)}`, line: statement.line });
        }
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "v2_challenge") {
        if (!isSimpleCompilerExpression(statement.expr)) {
          unsupported.push({ text: `challenge ${statement.expr}`, line: statement.line });
        }
        visit(statement.successBody);
        if (statement.failureBody) visit(statement.failureBody);
      }
      if (statement.kind === "v2_match") {
        for (const matchCase of statement.cases) visit([matchCase.action]);
        if (statement.otherwise) visit([statement.otherwise]);
      }
    }
  };
  if (ast.turn) visit(ast.turn.body);
  for (const rule of ast.rules) visit(rule.body);
  return unsupported;
}

function isLowerableV2Predicate(predicate: V2PredicateAst): boolean {
  if (predicate.kind === "v2_contains") {
    return isSimpleCompilerExpression(predicate.collection) &&
      isSimpleCompilerExpression(predicate.item);
  }
  if (predicate.kind === "v2_compare") {
    return isSimpleCompilerExpression(predicate.left) &&
      isSimpleCompilerExpression(predicate.right);
  }
  return false;
}

function formatV2Predicate(predicate: V2PredicateAst): string {
  if (predicate.kind === "v2_contains") return `${predicate.item} in ${predicate.collection}`;
  return `${predicate.left} ${predicate.op} ${predicate.right}`;
}

function isSimpleCompilerExpression(text: string): boolean {
  let normalized = normalizeV2ExpressionText(text);
  const direction = /^direction\((.+)\)$/u.exec(normalized.trim());
  if (direction) normalized = direction[1]!;
  try {
    parseExpression(normalized);
    return true;
  } catch {
    return false;
  }
}

function formatDomainName(domain: ProgramAst["domains"][number]): string {
  return domain.name ? `${domain.domainKind} ${domain.name}` : domain.domainKind;
}

class EmitContext {
  readonly items: MachineItem[] = [];
  private labelCounter = 0;
  private readonly constants = new Map<string, ExpressionAst>();
  private readonly constantStack = new Set<string>();
  private readonly ast: ProgramAst;
  private readonly allocation: RegisterAllocation;
  private readonly options: CompileOptions;
  private readonly machineProfile: MachineProfile;
  private readonly diagnostics: Diagnostic[];
  private readonly optimizations: AppliedOptimization[];
  private readonly warnings: string[];
  private readonly candidates: CandidateReport[];
  private readonly loweringOptions: LoweringOptions;
  private readonly inlineProcNames: Set<string>;
  private readonly readCounts: Map<string, number>;
  private readonly displayUseCounts: Map<string, number>;
  private readonly showSequenceUseCounts: Map<string, number>;
  private readonly expressionUseCounts: Map<string, { count: number; expr: ExpressionAst }>;
  private readonly lineCountCallCount: number;
  private readonly lineCountGroupCounts: Map<string, number>;
  private readonly spatialHitHelpers = new Map<string, { mask: string; scratch: string; label: string; line?: number }>();
  private readonly displayHelpers = new Map<string, { display: ProgramAst["displays"][number]; label: string; line: number }>();
  private readonly showSequenceHelpers = new Map<string, {
    first: ProgramAst["displays"][number];
    second: ProgramAst["displays"][number];
    label: string;
    line: number;
  }>();
  private readonly expressionHelpers = new Map<string, { expr: ExpressionAst; label: string; line?: number }>();
  private readonly lineCountHelpers = new Map<string, { cell: ExpressionAst; board: V2BoardAst; label: string; line?: number }>();
  private readonly spatialBitMaskHelpers = new Map<string, { scratch: string; label: string; line?: number }>();
  private readonly spatialSumLoopHelpers = new Map<string, {
    hitMask: string;
    cell: ExpressionAst;
    label: string;
    operation: "line_count" | "neighbor_count";
    line?: number;
  }>();
  private readonly terminalTailHelpers: Array<{ body: StatementAst[]; label: string; line: number }> = [];
  private currentXVariable: string | undefined;
  private currentXKnownZero = false;
  // True when the machine is mid number-entry, so the next number literal would
  // concatenate digits (e.g. 1 then 3 -> 13) instead of starting a new value.
  // Set by digit-building ops and by С/П reads (the user-typed value stays in
  // entry mode until a finalizing op lifts/uses it).
  private machineEntryOpen = false;
  private emittingExpressionHelper = false;

  constructor(
    ast: ProgramAst,
    allocation: RegisterAllocation,
    options: CompileOptions,
    machineProfile: MachineProfile,
    diagnostics: Diagnostic[],
    optimizations: AppliedOptimization[],
    warnings: string[],
    candidates: CandidateReport[],
    loweringOptions: LoweringOptions,
  ) {
    this.ast = ast;
    this.allocation = allocation;
    this.options = options;
    this.machineProfile = machineProfile;
    this.diagnostics = diagnostics;
    this.optimizations = optimizations;
    this.warnings = warnings;
    this.candidates = candidates;
    this.loweringOptions = loweringOptions;
    this.inlineProcNames = findSingleUseProcNames(ast);
    this.readCounts = collectVariableReadCounts(ast);
    this.displayUseCounts = collectDisplayUseCounts(ast);
    this.showSequenceUseCounts = collectShowSequenceUseCounts(ast);
    this.expressionUseCounts = collectExpressionUseCounts(ast);
    this.lineCountCallCount = countCalls(ast, "line_count");
    this.lineCountGroupCounts = collectLineCountGroupCounts(ast);
    for (const declaration of ast.declarations) {
      if (declaration.kind === "const") {
        this.constants.set(declaration.name, declaration.value);
      }
    }
  }

  compileProgram(): void {
    const main = this.ast.entries[0]!;
    this.emitLabel(main.name);
    this.compileInitialState();
    this.compileInitialStores();
    this.compileStatements(main.body);
    if (!(this.ast.v2 && this.statementsTerminate(main.body))) {
      this.emitOp(0x50, "С/П", "implicit final stop");
    }

    for (const proc of this.ast.procs) {
      if (this.inlineProcNames.has(proc.name)) continue;
      this.emitLabel(proc.name);
      this.compileStatements(proc.body);
      if (!this.statementsTerminate(proc.body)) {
        this.emitOp(0x52, "В/О", "implicit return from proc");
      }
    }

    for (const block of this.ast.blocks) {
      if (block.mode === "inline") continue;
      this.emitLabel(block.name);
      this.compileStatements(block.body);
      if (!this.statementsTerminate(block.body)) {
        this.emitOp(0x50, "С/П", `implicit stop for ${block.mode} block ${block.name}`, block.line);
      }
    }

    this.compileRuntimeHelpers();
  }

  private compileRuntimeHelpers(): void {
    for (let index = 0; index < this.terminalTailHelpers.length; index += 1) {
      const helper = this.terminalTailHelpers[index]!;
      this.emitLabel(helper.label);
      this.compileStatements(helper.body);
      this.optimizations.push({
        name: "local-terminal-tail",
        detail: `Emitted local terminal tail for branch at line ${helper.line}.`,
      });
    }
    for (const helper of this.displayHelpers.values()) {
      this.emitLabel(helper.label);
      this.compilePackedDisplayBody(helper.display, helper.line, false);
      this.emitOp(0x52, "В/О", `display ${helper.display.name} return`, helper.line);
      this.optimizations.push({
        name: "packed-display-helper",
        detail: `Emitted shared packed display helper for screen ${helper.display.name}.`,
      });
    }
    for (const helper of this.showSequenceHelpers.values()) {
      this.emitLabel(helper.label);
      this.compilePackedDisplayBody(helper.first, helper.line, false);
      this.compilePackedDisplayBody(helper.second, helper.line, false);
      this.emitOp(0x52, "В/О", `show sequence ${helper.first.name}/${helper.second.name} return`, helper.line);
      this.optimizations.push({
        name: "show-sequence-helper",
        detail: `Emitted shared helper for show ${helper.first.name}; show ${helper.second.name}; read.`,
      });
    }
    for (const helper of this.expressionHelpers.values()) {
      this.emitLabel(helper.label);
      this.emittingExpressionHelper = true;
      try {
        this.compileExpression(helper.expr);
      } finally {
        this.emittingExpressionHelper = false;
      }
      this.emitOp(0x52, "В/О", "expression helper return", helper.line);
      this.optimizations.push({
        name: "expression-helper",
        detail: `Emitted shared helper for ${expressionToIntentText(helper.expr)}.`,
      });
    }
    for (const helper of this.lineCountHelpers.values()) {
      this.emitLabel(helper.label);
      this.emitStore(spatialCountMaskScratchName(), "line count mask", helper.line);
      this.emitSpatialLineCountLoopBody(spatialCountMaskScratchName(), helper.cell, helper.board, helper.line);
      this.emitOp(0x52, "В/О", "line_count return", helper.line);
      this.optimizations.push({
        name: "spatial-line-count-helper",
        detail: `Emitted shared line_count helper for ${helper.board.name}/${expressionToIntentText(helper.cell)}.`,
      });
    }
    for (const helper of this.spatialSumLoopHelpers.values()) {
      this.emitLabel(helper.label);
      this.emitSpatialSumLoopHelperBody(helper.hitMask, helper.cell, helper.operation, helper.line);
      this.emitOp(0x52, "В/О", `${helper.operation} progression return`, helper.line);
      this.optimizations.push({
        name: "spatial-sum-loop-helper",
        detail: `Emitted shared ${helper.operation} progression helper for ${helper.hitMask}.`,
      });
    }
    for (const helper of this.spatialBitMaskHelpers.values()) {
      this.emitLabel(helper.label);
      this.emitStore(helper.scratch, "bit mask index", helper.line);
      this.compileExpression({
        kind: "call",
        callee: "bit_mask",
        args: [{ kind: "identifier", name: helper.scratch }],
      });
      this.emitOp(0x52, "В/О", "bit_mask return", helper.line);
      this.optimizations.push({
        name: "bit-mask-helper",
        detail: `Emitted shared bit_mask helper using ${helper.scratch}.`,
      });
    }
    for (const helper of this.spatialHitHelpers.values()) {
      this.emitLabel(helper.label);
      this.emitStore(helper.scratch, "spatial hit index", helper.line);
      this.emitRecall(helper.mask, "spatial hit mask", helper.line);
      this.compileExpression({
        kind: "call",
        callee: "bit_mask",
        args: [{ kind: "identifier", name: helper.scratch }],
      });
      this.emitOp(0x37, "К ∧", "spatial hit test", helper.line);
      this.emitOp(0x32, "К ЗН", "spatial hit to count", helper.line);
      this.emitOp(0x52, "В/О", "spatial hit return", helper.line);
    }
  }

  private compileInitialState(): void {
    if (this.ast.v2) {
      const fields = this.ast.states.flatMap((state) => state.fields);
      if (fields.some((field) => field.initial !== undefined || field.initialStack !== undefined)) {
        this.optimizations.push({
          name: "auto-preload-initial-state",
          detail: "Moved initial state into setup/preload values so official program cells stay focused on turn logic.",
        });
      }
      return;
    }
    for (const state of this.ast.states) {
      for (const field of state.fields.filter((candidate) => candidate.initialStack === "Y")) {
        this.emitOp(0x14, "X↔Y", `init ${state.name}.${field.name} from stack.Y`, field.line);
        this.emitStore(field.name, `init ${state.name}.${field.name}`, field.line);
        this.emitOp(0x14, "X↔Y", `restore stack.X after ${field.name}`, field.line);
      }
      for (const field of state.fields.filter((candidate) => candidate.initialStack === "X")) {
        this.emitStore(field.name, `init ${state.name}.${field.name}`, field.line);
      }
      for (const field of state.fields) {
        if (field.initialStack !== undefined) continue;
        if (field.initial === undefined) continue;
        this.compileExpression(field.initial);
        this.emitStore(field.name, `init ${state.name}.${field.name}`, field.line);
      }
      if (state.fields.length > 0) {
        this.optimizations.push({
          name: "intent-state-lowering",
          detail: `Lowered state ${state.name} with ${state.fields.length} fields to register-backed values.`,
        });
      }
    }
  }

  private compileInitialStores(): void {
    for (const declaration of this.ast.declarations) {
      if (declaration.kind !== "store" || !declaration.value) continue;
      this.compileExpression(declaration.value);
      this.emitStore(declaration.name, `init ${declaration.name}`, declaration.line);
    }
  }

  private compileStatements(statements: StatementAst[]): void {
    for (let index = 0; index < statements.length; index += 1) {
      const statement = statements[index]!;
      const next = statements[index + 1];
      if (statement.kind === "assign") {
        const reused = this.compileRepeatedAssignmentValue(statements, index);
        if (reused > 1) {
          index += reused - 1;
          continue;
        }
      }
      if (statement.kind === "assign" && next?.kind === "if" && this.compileDecrementZeroBranch(statement, next)) {
        index += 1;
        continue;
      }
      if (statement.kind === "assign" && next?.kind === "assign" && this.compileTicTacToeCellMaskReuse(statement, next)) {
        index += 1;
        continue;
      }
      if (statement.kind === "assign" && next?.kind === "assign" && this.compileBitSetMaskReuse(statement, next)) {
        index += 1;
        continue;
      }
      if (statement.kind === "assign" && next?.kind === "assign" && this.compileIntFracSharedTail(statement, next)) {
        index += 1;
        continue;
      }
      if (statement.kind === "assign" && next?.kind === "if" && this.compileGuardAssignmentSubstitution(statement, next)) {
        index += 1;
        continue;
      }
      if (statement.kind === "if" && next?.kind === "if" && this.compileDoubleBranchRemoval(statement, next)) {
        index += 1;
        continue;
      }
      if (statement.kind === "pause" && next?.kind === "halt" && expressionEquals(statement.expr, next.expr)) {
        this.compileStatement(next);
        this.optimizations.push({
          name: "terminal-display-fusion",
          detail: `Dropped duplicate terminal display before stop at line ${next.line}.`,
        });
        index += 1;
        continue;
      }
      if (statement.kind === "show" && next?.kind === "halt" && this.haltDisplaysSameValue(statement, next)) {
        this.compileStatement(next);
        this.optimizations.push({
          name: "terminal-display-fusion",
          detail: `Dropped duplicate screen ${statement.display} before stop at line ${next.line}.`,
        });
        index += 1;
        continue;
      }
      if (statement.kind === "show" && next?.kind === "show" && statements[index + 2]?.kind === "input") {
        const input = statements[index + 2] as Extract<StatementAst, { kind: "input" }>;
        if (this.compileShowSequenceRead(statement, next, input)) {
          index += 2;
          continue;
        }
      }
      if (statement.kind === "show" && next?.kind === "input") {
        this.compileShow(statement.display, statement.line);
        this.emitStore(next.target, `read ${next.target}`, next.line);
        this.optimizations.push({
          name: "show-read-fusion",
          detail: `Fused show ${statement.display} and read ${next.target} into one calculator stop.`,
        });
        index += 1;
        continue;
      }
      this.compileStatement(statement);
    }
  }

  private compileRepeatedAssignmentValue(statements: StatementAst[], start: number): number {
    const first = statements[start];
    if (first?.kind !== "assign" || !expressionPureForSubstitution(first.expr)) return 0;
    let end = start + 1;
    while (end < statements.length) {
      const candidate = statements[end]!;
      if (candidate.kind !== "assign" || !expressionEquals(candidate.expr, first.expr)) break;
      end += 1;
    }
    const count = end - start;
    if (count <= 1) return 0;
    this.compileExpression(first.expr);
    for (let index = start; index < end; index += 1) {
      const assignment = statements[index] as Extract<StatementAst, { kind: "assign" }>;
      this.emitStore(assignment.target, `set ${assignment.target}`, assignment.line);
    }
    this.optimizations.push({
      name: "repeated-assignment-value-reuse",
      detail: `Stored one computed value into ${count} consecutive assignment targets at line ${first.line}.`,
    });
    return count;
  }

  private haltDisplaysSameValue(
    show: Extract<StatementAst, { kind: "show" }>,
    halt: Extract<StatementAst, { kind: "halt" }>,
  ): boolean {
    const display = this.ast.displays.find((candidate) => candidate.name === show.display);
    if (display === undefined || display.items.length !== 1) return false;
    const [item] = display.items;
    if (item?.kind !== "source") return false;
    const field = this.findStateField(item.name);
    return field?.initial !== undefined && expressionEquals(field.initial, halt.expr);
  }

  private compileShowSequenceRead(
    firstShow: Extract<StatementAst, { kind: "show" }>,
    secondShow: Extract<StatementAst, { kind: "show" }>,
    input: Extract<StatementAst, { kind: "input" }>,
  ): boolean {
    const helper = this.sharedShowSequenceHelper(firstShow.display, secondShow.display, firstShow.line);
    if (helper === undefined) return false;
    this.emitJump(0x53, "ПП", helper.label, `show ${firstShow.display}; show ${secondShow.display}`, firstShow.line);
    this.emitStore(input.target, `read ${input.target}`, input.line);
    this.optimizations.push({
      name: "show-sequence-helper-call",
      detail: `Reused shared helper for show ${firstShow.display}; show ${secondShow.display}; read ${input.target}.`,
    });
    return true;
  }

  private compileGuardAssignmentSubstitution(
    assign: Extract<StatementAst, { kind: "assign" }>,
    guarded: Extract<StatementAst, { kind: "if" }>,
  ): boolean {
    const readsInCondition = countIdentifierReadsInCondition(guarded.condition, assign.target);
    if (readsInCondition === 0) return false;
    if ((this.readCounts.get(assign.target) ?? 0) !== readsInCondition) return false;
    if (!expressionPureForSubstitution(assign.expr)) return false;
    const substitutedCondition = substituteConditionIdentifier(guarded.condition, assign.target, assign.expr);
    const ordinaryCost = estimateExpressionCost(assign.expr) + 1 + conditionCompileCost(guarded.condition);
    const substitutedCost = conditionCompileCost(substitutedCondition);
    if (substitutedCost + 4 >= ordinaryCost) return false;
    this.compileIf({
      ...guarded,
      condition: substitutedCondition,
    }, guarded.line);
    this.optimizations.push({
      name: "single-use-guard-substitution",
      detail: `Substituted ${assign.target} directly into the following condition at lines ${assign.line}/${guarded.line}.`,
    });
    return true;
  }

  private compileStatement(statement: StatementAst): void {
    switch (statement.kind) {
      case "pause":
        this.compileExpression(statement.expr);
        this.emitOp(0x50, "С/П", "pause", statement.line);
        return;
      case "ask":
        if (statement.prompt) this.compileExpression(statement.prompt);
        this.emitOp(0x50, "С/П", `ask ${statement.target}`, statement.line);
        this.emitStore(statement.target, `input ${statement.target}`, statement.line);
        return;
      case "input":
        this.emitOp(0x50, "С/П", `read ${statement.target}`, statement.line);
        this.emitStore(statement.target, `read ${statement.target}`, statement.line);
        this.optimizations.push({
          name: "intent-read-lowering",
          detail: `Lowered read at line ${statement.line} to calculator stop plus register store.`,
        });
        return;
      case "halt":
        this.compileExpression(statement.expr);
        this.emitOp(0x50, "С/П", "halt", statement.line);
        return;
      case "assign":
        if (this.compileUnitDecrement(statement)) return;
        this.compileExpression(statement.expr);
        this.emitStore(statement.target, `set ${statement.target}`, statement.line);
        return;
      case "loop": {
        const start = this.freshLabel("loop");
        this.emitLabel(start);
        this.compileStatements(statement.body);
        this.emitJump(0x51, "БП", start, "loop back", statement.line);
        return;
      }
      case "if":
        this.compileIf(statement, statement.line);
        return;
      case "switch":
        this.compileSwitch(statement);
        return;
      case "dispatch":
        this.compileDispatch(statement);
        return;
      case "show":
        this.compileShow(statement.display, statement.line);
        return;
      case "call":
        this.compileBlockCall(statement.block, statement.line);
        return;
      case "core":
        this.compileRawStatement(statement);
        return;
      case "egg":
        this.compileRawLines(statement.lines);
        return;
      case "trap":
        this.compileTrap(statement);
        return;
    }
  }

  private compileTicTacToeCellMaskReuse(
    first: Extract<StatementAst, { kind: "assign" }>,
    second: Extract<StatementAst, { kind: "assign" }>,
  ): boolean {
    const used = matchCellHelperCall(first.expr, ["cell_used", "cell_has"]);
    const mark = matchCellHelperCall(second.expr, ["cell_mark", "cell_set"]);
    if (!used || !mark) return false;
    if (!expressionEquals(used.mask, mark.mask) || !expressionEquals(used.x, mark.x) || !expressionEquals(used.y, mark.y)) {
      return false;
    }
    if (used.mask.kind !== "identifier" || second.target !== used.mask.name) return false;

    const scratch = ticTacToeMaskScratchName(first);
    if (!this.allocation.registers[scratch]) return false;

    this.compileExpression(cellMaskExpression(used.x, used.y));
    this.emitStore(scratch, "4x4 cell mask scratch", first.line);
    this.compileExpression(used.mask);
    this.emitRecall(scratch, "reuse 4x4 cell mask", first.line);
    this.emitOp(0x37, "К ∧", "cell_has with reused mask", first.line);
    this.emitStore(first.target, `set ${first.target}`, first.line);
    this.compileExpression(mark.mask);
    this.emitRecall(scratch, "reuse 4x4 cell mask", second.line);
    this.emitOp(0x38, "К ∨", "cell_set with reused mask", second.line);
    this.emitStore(second.target, `set ${second.target}`, second.line);
    this.optimizations.push({
      name: "tic-tac-toe-cell-mask-cse",
      detail: `Computed cell_mask once for adjacent cell_has/cell_set at lines ${first.line}/${second.line}.`,
    });
    return true;
  }

  private compileBitSetMaskReuse(
    first: Extract<StatementAst, { kind: "assign" }>,
    second: Extract<StatementAst, { kind: "assign" }>,
  ): boolean {
    const firstSet = matchBitSetAssignment(first);
    const secondSet = matchBitSetAssignment(second);
    if (firstSet === undefined || secondSet === undefined) return false;
    if (!expressionEquals(firstSet.item, secondSet.item)) return false;

    const scratch = bitMaskScratchName(first);
    if (!this.allocation.registers[scratch]) return false;

    this.compileExpression(bitMaskExpression(firstSet.item));
    this.emitStore(scratch, "cell bit mask scratch", first.line);
    this.compileExpression(firstSet.collection);
    this.emitRecall(scratch, "reuse cell bit mask", first.line);
    this.emitOp(0x38, "К ∨", "bit_set with reused mask", first.line);
    this.emitStore(first.target, `set ${first.target}`, first.line);
    this.compileExpression(secondSet.collection);
    this.emitRecall(scratch, "reuse cell bit mask", second.line);
    this.emitOp(0x38, "К ∨", "bit_set with reused mask", second.line);
    this.emitStore(second.target, `set ${second.target}`, second.line);
    this.optimizations.push({
      name: "bit-set-mask-cse",
      detail: `Computed bit_mask() once for adjacent set updates at lines ${first.line}/${second.line}.`,
    });
    return true;
  }

  private compileDecrementZeroBranch(
    decrement: Extract<StatementAst, { kind: "assign" }>,
    branch: Extract<StatementAst, { kind: "if" }>,
  ): boolean {
    if (!isUnitDecrementExpression(decrement.target, decrement.expr)) return false;
    if (!decrementBranchTestsZero(branch.condition, decrement.target)) return false;
    const field = this.findStateField(decrement.target);
    if ((field?.min ?? Number.NEGATIVE_INFINITY) < 1) return false;
    const register = this.allocation.registers[decrement.target];
    if (register === undefined) return false;
    const opcode = flOpcode(register);
    if (opcode === undefined) return false;

    const nonZeroLabel = this.freshLabel("decrement_nonzero");
    const thenTerminates = this.statementsTerminate(branch.thenBody);
    const endLabel = branch.elseBody !== undefined && !thenTerminates ? this.freshLabel("if_end") : undefined;

    this.emitJump(opcode, getOpcode(opcode).name, nonZeroLabel, `decrement/test ${decrement.target}`, decrement.line);
    this.compileStatements(branch.thenBody);
    if (branch.elseBody !== undefined) {
      if (endLabel !== undefined) this.emitJump(0x51, "БП", endLabel, "if end", branch.line);
      this.emitLabel(nonZeroLabel);
      this.compileStatements(branch.elseBody);
      if (endLabel !== undefined) this.emitLabel(endLabel);
    } else {
      this.emitLabel(nonZeroLabel);
    }
    this.optimizations.push({
      name: "fl-decrement-zero-branch",
      detail: `Fused ${decrement.target} decrement and zero branch at lines ${decrement.line}/${branch.line}.`,
    });
    return true;
  }

  private compileIf(
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): void {
    if (this.compileArithmeticIfSelect(statement)) return;
    if (this.compileGuardedUpdateSelector(statement)) return;
    if (this.compileDirectTerminalIfBranch(statement, line)) return;
    if (this.compileMembershipClearReuse(statement, line)) return;
    if (this.compileMembershipSetReuse(statement, line)) return;
    if (this.compileLocalTerminalElseTail(statement, line)) return;

    const falseLabel = this.freshLabel("if_false");
    const thenTerminates = this.statementsTerminate(statement.thenBody);
    const endLabel = thenTerminates ? undefined : this.freshLabel("if_end");
    this.compileCondition(statement.condition, falseLabel, line);
    this.compileStatements(statement.thenBody);
    if (statement.elseBody) {
      if (endLabel !== undefined) this.emitJump(0x51, "БП", endLabel, "if end", line);
      this.emitLabel(falseLabel);
      this.compileStatements(statement.elseBody);
      if (endLabel !== undefined) this.emitLabel(endLabel);
      if (thenTerminates) {
        this.optimizations.push({
          name: "terminal-branch-end-elision",
          detail: `Omitted unreachable if-end jump after terminal then branch at line ${line}.`,
        });
      }
    } else {
      this.emitLabel(falseLabel);
    }
  }

  private compileDirectTerminalIfBranch(
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): boolean {
    const thenTarget = this.directTerminalCallTarget(statement.thenBody);
    const elseTarget = statement.elseBody === undefined ? undefined : this.directTerminalCallTarget(statement.elseBody);
    if (thenTarget === undefined && elseTarget === undefined) return false;

    const preloadedConstants = new Set(Object.keys(this.allocation.constants));
    const originalCost = estimateConditionCost(statement.condition, this.ast, preloadedConstants);
    const candidates: Array<{
      branchWhen: "true" | "false";
      target: string;
      condition: ConditionAst;
      estimatedCost: number;
      ordinaryCost: number;
    }> = [];

    if (
      thenTarget !== undefined &&
      (statement.elseBody !== undefined || this.loweringOptions.aggressiveTerminalDirect === true)
    ) {
      const condition = invertCondition(statement.condition);
      candidates.push({
        branchWhen: "true",
        target: thenTarget,
        condition,
        estimatedCost: estimateConditionCost(condition, this.ast, preloadedConstants),
        ordinaryCost: originalCost + 2,
      });
    }
    if (elseTarget !== undefined) {
      const thenTerminates = this.statementsTerminate(statement.thenBody);
      candidates.push({
        branchWhen: "false",
        target: elseTarget,
        condition: statement.condition,
        estimatedCost: originalCost,
        ordinaryCost: originalCost + (thenTerminates ? 1 : 2),
      });
    }

    const selected = candidates
      .filter((candidate) => candidate.estimatedCost < candidate.ordinaryCost)
      .sort((left, right) => left.estimatedCost - right.estimatedCost)[0];
    if (selected === undefined) return false;

    this.compileCondition(selected.condition, selected.target, line);
    if (selected.branchWhen === "true") {
      if (statement.elseBody) this.compileStatements(statement.elseBody);
    } else {
      this.compileStatements(statement.thenBody);
    }
    this.optimizations.push({
      name: "terminal-if-direct-branch",
      detail: `Branched directly to terminal ${selected.target} for ${selected.branchWhen} path at line ${line} (${selected.estimatedCost} vs ${selected.ordinaryCost} estimated branch cells).`,
    });
    return true;
  }

  private directTerminalCallTarget(statements: StatementAst[]): string | undefined {
    if (statements.length !== 1) return undefined;
    const statement = statements[0];
    if (statement?.kind !== "call") return undefined;

    const block = this.ast.blocks.find((candidate) => candidate.name === statement.block);
    if (block !== undefined) return block.mode === "inline" ? undefined : block.name;

    const proc = this.ast.procs.find((candidate) => candidate.name === statement.block);
    if (proc === undefined || this.inlineProcNames.has(proc.name)) return undefined;
    return this.statementsTerminate(proc.body) ? proc.name : undefined;
  }

  private compileLocalTerminalElseTail(
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): boolean {
    if (statement.elseBody === undefined) return false;
    if (this.statementsTerminate(statement.thenBody)) return false;
    if (!this.statementsTerminate(statement.elseBody)) return false;

    const helper = this.ensureTerminalTailHelper(statement.elseBody, line);
    this.compileCondition(statement.condition, helper.label, line);
    this.compileStatements(statement.thenBody);
    this.optimizations.push({
      name: "local-terminal-tail-branch",
      detail: `Branched to a local terminal tail for else path at line ${line}.`,
    });
    return true;
  }

  private ensureTerminalTailHelper(body: StatementAst[], line: number): { body: StatementAst[]; label: string; line: number } {
    const existing = this.terminalTailHelpers.find((helper) => statementListsEqual(helper.body, body));
    if (existing !== undefined) return existing;
    const helper = {
      body,
      label: this.freshLabel("terminal_tail"),
      line,
    };
    this.terminalTailHelpers.push(helper);
    return helper;
  }

  private compileMembershipClearReuse(
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): boolean {
    const clearPrefix = this.membershipClearPrefix(statement.thenBody);
    if (clearPrefix === undefined) return false;
    const { clear, tail } = clearPrefix;
    const membership = matchBitMembershipCondition(statement.condition);
    if (membership === undefined) return false;
    if (!isBitClearAssignment(clear, membership)) return false;

    const falseLabel = this.freshLabel("if_false");
    const endLabel = this.freshLabel("if_end");

    if (!this.compileBitMembershipMaskValue(membership, line)) this.compileExpression(membership.test);
    this.emitJump(0x57, "F x!=0", falseLabel, "false branch for !=", line);
    this.emitOp(0x3a, "К ИНВ", "reuse membership mask for clear", clear.line);
    this.compileExpression(membership.collection);
    this.emitOp(0x37, "К ∧", "clear matched cell with reused mask", clear.line);
    this.emitStore(clear.target, `set ${clear.target}`, clear.line);
    this.compileStatements(tail);
    if (statement.elseBody) {
      this.emitJump(0x51, "БП", endLabel, "if end", line);
      this.emitLabel(falseLabel);
      this.currentXKnownZero = true;
      this.compileStatements(statement.elseBody);
      this.emitLabel(endLabel);
    } else {
      this.emitLabel(falseLabel);
    }
    this.optimizations.push({
      name: "cell-membership-clear-reuse",
      detail: `Reused the successful membership mask when clearing ${clear.target} at line ${clear.line}.`,
    });
    return true;
  }

  private compileBitMembershipMaskValue(membership: BitMembershipCondition, line: number): boolean {
    if (membership.collection.kind !== "identifier") return false;
    const scratch = spatialHitScratchName(membership.collection.name);
    if (this.allocation.registers[scratch] === undefined) return false;
    const helper = this.ensureSpatialBitMaskHelper(scratch, line);
    this.compileExpression(membership.item);
    this.emitJump(0x53, "ПП", helper.label, "bit_mask helper", line);
    this.compileExpression(membership.collection);
    this.emitOp(0x37, "К ∧", "bit membership test", line);
    this.optimizations.push({
      name: "bit-mask-helper-call",
      detail: `Reused shared bit_mask helper for ${membership.collection.name} at line ${line}.`,
    });
    return true;
  }

  private compileMembershipSetReuse(
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): boolean {
    const present = matchBitMembershipCondition(statement.condition);
    if (present !== undefined && present.collection.kind === "identifier" && statement.elseBody !== undefined) {
      const setPrefix = this.membershipSetPrefix(statement.elseBody, present);
      if (setPrefix !== undefined) {
        return this.compileMembershipSetReuseForPresentCondition(statement, present, setPrefix, line);
      }
    }

    const absent = matchBitAbsenceCondition(statement.condition);
    if (absent === undefined || absent.collection.kind !== "identifier") return false;
    const setPrefix = this.membershipSetPrefix(statement.thenBody, absent);
    if (setPrefix === undefined) return false;
    return this.compileMembershipSetReuseForAbsentCondition(statement, absent, setPrefix, line);
  }

  private compileMembershipSetReuseForPresentCondition(
    statement: Extract<StatementAst, { kind: "if" }>,
    membership: BitMembershipCondition,
    setPrefix: {
      set: Extract<StatementAst, { kind: "assign" }>;
      tail: StatementAst[];
    },
    line: number,
  ): boolean {
    const scratch = bitMaskScratchName(statement);
    if (this.allocation.registers[scratch] === undefined) return false;

    const { set, tail } = setPrefix;
    const falseLabel = this.freshLabel("if_false");
    const thenTerminates = this.statementsTerminate(statement.thenBody);
    const endLabel = thenTerminates ? undefined : this.freshLabel("if_end");

    this.emitMembershipMaskTest(membership, scratch, line);
    this.emitJump(0x5e, "F x=0", falseLabel, "false branch for !=", line);
    this.compileStatements(statement.thenBody);
    if (endLabel !== undefined) this.emitJump(0x51, "БП", endLabel, "if end", line);
    this.emitLabel(falseLabel);
    this.emitBitSetWithScratch(membership, set, scratch);
    this.compileStatements(tail);
    if (endLabel !== undefined) this.emitLabel(endLabel);

    this.optimizations.push({
      name: "cell-membership-set-reuse",
      detail: `Reused the failed membership mask when setting ${set.target} at line ${set.line}.`,
    });
    return true;
  }

  private compileMembershipSetReuseForAbsentCondition(
    statement: Extract<StatementAst, { kind: "if" }>,
    membership: BitMembershipCondition,
    setPrefix: {
      set: Extract<StatementAst, { kind: "assign" }>;
      tail: StatementAst[];
    },
    line: number,
  ): boolean {
    const scratch = bitMaskScratchName(statement);
    if (this.allocation.registers[scratch] === undefined) return false;

    const { set, tail } = setPrefix;
    const falseLabel = this.freshLabel("if_false");
    const thenTerminates = this.statementsTerminate(statement.thenBody);
    const endLabel = statement.elseBody !== undefined && !thenTerminates ? this.freshLabel("if_end") : undefined;

    this.emitMembershipMaskTest(membership, scratch, line);
    this.emitJump(0x57, "F x!=0", falseLabel, "false branch for ==", line);
    this.emitBitSetWithScratch(membership, set, scratch);
    this.compileStatements(tail);
    if (statement.elseBody !== undefined) {
      if (endLabel !== undefined) this.emitJump(0x51, "БП", endLabel, "if end", line);
      this.emitLabel(falseLabel);
      this.compileStatements(statement.elseBody);
      if (endLabel !== undefined) this.emitLabel(endLabel);
    } else {
      this.emitLabel(falseLabel);
    }

    this.optimizations.push({
      name: "cell-membership-set-reuse",
      detail: `Reused the failed membership mask when setting ${set.target} at line ${set.line}.`,
    });
    return true;
  }

  private emitMembershipMaskTest(
    membership: BitMembershipCondition,
    scratch: string,
    line: number,
  ): void {
    this.compileExpression(bitMaskExpression(membership.item));
    this.emitStore(scratch, "cell bit mask scratch", line);
    this.compileExpression(membership.collection);
    this.emitRecall(scratch, "reuse cell bit mask", line);
    this.emitOp(0x37, "К ∧", "membership test with reused mask", line);
  }

  private emitBitSetWithScratch(
    membership: BitMembershipCondition,
    set: Extract<StatementAst, { kind: "assign" }>,
    scratch: string,
  ): void {
    this.compileExpression(membership.collection);
    this.emitRecall(scratch, "reuse cell bit mask", set.line);
    this.emitOp(0x38, "К ∨", "bit_set with reused mask", set.line);
    this.emitStore(set.target, `set ${set.target}`, set.line);
  }

  private membershipClearPrefix(statements: StatementAst[]): {
    clear: Extract<StatementAst, { kind: "assign" }>;
    tail: StatementAst[];
  } | undefined {
    const first = statements[0];
    if (first?.kind === "assign") {
      return { clear: first, tail: statements.slice(1) };
    }
    if (first?.kind !== "call" || !this.inlineProcNames.has(first.block)) return undefined;
    const proc = this.ast.procs.find((candidate) => candidate.name === first.block);
    if (proc === undefined) return undefined;
    const clear = proc.body[0];
    if (clear?.kind !== "assign") return undefined;
    return {
      clear,
      tail: [...proc.body.slice(1), ...statements.slice(1)],
    };
  }

  private membershipSetPrefix(
    statements: StatementAst[],
    membership: BitMembershipCondition,
  ): {
    set: Extract<StatementAst, { kind: "assign" }>;
    tail: StatementAst[];
  } | undefined {
    const first = statements[0];
    if (first?.kind === "assign" && isBitSetAssignment(first, membership)) {
      return { set: first, tail: statements.slice(1) };
    }
    if (first?.kind !== "call" || !this.inlineProcNames.has(first.block)) return undefined;
    const proc = this.ast.procs.find((candidate) => candidate.name === first.block);
    if (proc === undefined) return undefined;
    const set = proc.body[0];
    if (set?.kind !== "assign" || !isBitSetAssignment(set, membership)) return undefined;
    return {
      set,
      tail: [...proc.body.slice(1), ...statements.slice(1)],
    };
  }

  private compileArithmeticIfSelect(statement: Extract<StatementAst, { kind: "if" }>): boolean {
    const canUseNegativeZero = this.allocation.negativeZeroDegree !== undefined;
    const selected = buildBranchRemovalCandidate(
      statement,
      this.ast,
      { negativeZeroDegree: canUseNegativeZero },
    );
    if (!selected) {
      if (!canUseNegativeZero) this.recordRejectedNegativeZeroBranchCandidate(statement);
      return false;
    }

    const ordinaryCost = estimateOrdinaryIfCost(statement, this.ast);
    const selectedCost = estimateExpressionCost(selected.expr) + 1;
    if (selectedCost >= ordinaryCost) {
      this.candidates.push({
        site: `if@${statement.line}`,
        variant: selected.name,
        steps: selectedCost,
        selected: false,
        reason: `Branchless ${selected.name} estimated at ${selectedCost} cells; ordinary branched form was shorter (${ordinaryCost}).`,
      });
      if (!selected.name.startsWith("negative-zero-threshold-")) {
        this.recordRejectedNegativeZeroBranchCandidate(statement);
      }
      return false;
    }

    this.compileExpression(selected.expr);
    if (selected.kind === "assign") {
      this.emitStore(selected.target, `${selected.name} ${selected.target}`, statement.line);
    } else {
      this.emitOp(0x50, "С/П", `${selected.kind} ${selected.name}`, statement.line);
    }
    this.optimizations.push({
      name: "branch-removal",
      detail: `${selected.detail} at line ${statement.line}; emitted branchless ${selected.name}.`,
    });
    this.optimizations.push({
      name: selected.name,
      detail: `${selected.detail} at line ${statement.line} (${selectedCost} vs ${ordinaryCost} estimated steps).`,
    });
    return true;
  }

  private compileGuardedUpdateSelector(statement: Extract<StatementAst, { kind: "if" }>): boolean {
    const scratch = ifSelectorScratchName(statement);
    if (this.allocation.registers[scratch] === undefined) return false;
    const candidate = buildGuardedUpdateSelectorCandidate(statement, this.ast, {
      negativeZeroDegree: this.allocation.negativeZeroDegree !== undefined,
    });
    if (candidate === undefined) return false;

    const ordinaryCost = estimateOrdinaryGuardedUpdateCost(statement, this.ast);
    const selectedCost = estimateGuardedUpdateSelectorCost(candidate, scratch);
    if (selectedCost >= ordinaryCost) {
      this.candidates.push({
        site: `if@${statement.line}`,
        variant: candidate.name,
        steps: selectedCost,
        selected: false,
        reason: `Guarded update selector estimated at ${selectedCost} cells; ordinary branched form was shorter (${ordinaryCost}).`,
      });
      return false;
    }

    this.compileExpression(candidate.selector);
    this.emitStore(scratch, `${candidate.name} selector`, statement.line);
    const selector: ExpressionAst = { kind: "identifier", name: scratch };
    for (const update of candidate.updates) {
      this.compileExpression(maskedGuardedUpdateExpression(update, selector));
      this.emitStore(update.target, `${candidate.name} ${update.target}`, statement.line);
    }
    this.optimizations.push({
      name: "branch-removal",
      detail: `${candidate.detail} at line ${statement.line}; emitted masked guarded updates.`,
    });
    this.optimizations.push({
      name: candidate.name,
      detail: `${candidate.detail} at line ${statement.line} (${selectedCost} vs ${ordinaryCost} estimated steps).`,
    });
    return true;
  }

  private recordRejectedNegativeZeroBranchCandidate(statement: Extract<StatementAst, { kind: "if" }>): void {
    const selected = buildBranchRemovalCandidate(statement, this.ast, { negativeZeroDegree: true });
    if (selected === undefined || !selected.name.startsWith("negative-zero-threshold-")) return;
    const ordinaryCost = estimateOrdinaryIfCost(statement, this.ast);
    const selectedCost = estimateExpressionCost(selected.expr) + 1;
    this.candidates.push({
      site: `if@${statement.line}`,
      variant: selected.name,
      steps: selectedCost,
      selected: false,
      reason: selectedCost >= ordinaryCost
        ? `Branchless ${selected.name} estimated at ${selectedCost} cells; ordinary branched form was shorter (${ordinaryCost}).`
        : `Branchless ${selected.name} estimated at ${selectedCost} cells, but no compiler-owned negative-zero register was available.`,
    });
  }

  private compileDoubleBranchRemoval(
    first: Extract<StatementAst, { kind: "if" }>,
    second: Extract<StatementAst, { kind: "if" }>,
  ): boolean {
    const selected = buildDoubleClampCandidate(first, second);
    if (!selected) return false;

    const ordinaryCost = estimateOrdinaryIfCost(first, this.ast) + estimateOrdinaryIfCost(second, this.ast);
    const selectedCost = estimateExpressionCost(selected.expr) + 1;
    if (selectedCost >= ordinaryCost) {
      this.candidates.push({
        site: `if@${first.line}+${second.line}`,
        variant: selected.name,
        steps: selectedCost,
        selected: false,
        reason: `Branchless ${selected.name} estimated at ${selectedCost} cells; paired branched form was shorter (${ordinaryCost}).`,
      });
      return false;
    }

    this.compileExpression(selected.expr);
    this.emitStore(selected.target, `${selected.name} ${selected.target}`, first.line);
    this.optimizations.push({
      name: "branch-removal",
      detail: `${selected.detail} at lines ${first.line}/${second.line}; emitted branchless ${selected.name}.`,
    });
    this.optimizations.push({
      name: selected.name,
      detail: `${selected.detail} at lines ${first.line}/${second.line} (${selectedCost} vs ${ordinaryCost} estimated steps).`,
    });
    return true;
  }

  private compileSwitch(statement: SwitchStatementAst): void {
    const scratch = `${SWITCH_SCRATCH_PREFIX}${statement.scratchId}`;
    const register = this.allocation.registers[scratch];
    if (!register) {
      this.diagnostics.push({
        level: "error",
        message: `Internal: no scratch register reserved for switch at line ${statement.line}.`,
        line: statement.line,
      });
      return;
    }

    this.compileExpression(statement.expr);
    this.emitOp(
      0x40 + registerIndex(register),
      `X->П ${register}`,
      `switch scratch`,
      statement.line,
    );

    const endLabel = this.freshLabel("switch_end");
    for (const switchCase of statement.cases) {
      const nextLabel = this.freshLabel("switch_next");
      this.emitOp(
        0x60 + registerIndex(register),
        `П->X ${register}`,
        "switch scratch recall",
        switchCase.line,
      );
      this.compileExpression(switchCase.value);
      this.emitOp(0x11, "-", "switch compare", switchCase.line);
      this.emitJump(0x5e, "F x=0", nextLabel, "case mismatch", switchCase.line);
      this.compileStatements(switchCase.body);
      this.emitJump(0x51, "БП", endLabel, "switch end", switchCase.line);
      this.emitLabel(nextLabel);
    }
    if (statement.defaultBody) this.compileStatements(statement.defaultBody);
    this.emitLabel(endLabel);

    this.optimizations.push({
      name: "switch-lowering",
      detail: `Lowered switch at line ${statement.line} via scratch R${register}; expression evaluated once.`,
    });
  }

  private compileDispatch(statement: Extract<StatementAst, { kind: "dispatch" }>): void {
    const optimized = optimizeDispatchDefaultCases(statement);
    if (optimized.removed > 0) {
      this.optimizations.push({
        name: "dispatch-default-merge",
        detail: `Removed ${optimized.removed} dispatch case${optimized.removed === 1 ? "" : "s"} whose body matched the default branch.`,
      });
    }
    if (optimized.reordered > 0) {
      this.optimizations.push({
        name: "dispatch-case-ordering",
        detail: `Moved ${optimized.reordered} zero dispatch case${optimized.reordered === 1 ? "" : "s"} earlier to reuse the selector already in X.`,
      });
    }

    const site = statement.name ?? `dispatch@${statement.line}`;
    const selected = selectDispatchCandidate(optimized.statement, this.machineProfile);
    for (const candidate of selected.candidates) this.candidates.push(candidate);

    this.optimizations.push({
      name: "dispatch-lowering",
      detail: `Selected ${selected.selected.variant} for ${site}.`,
    });

    this.compileDispatchCompareChain(optimized.statement, selected.selected.variant === "fallthrough-compare-chain");
  }

  private compileDispatchCompareChain(
    statement: Extract<StatementAst, { kind: "dispatch" }>,
    useFallthrough: boolean,
  ): void {
    if (this.compileNumericResidualDispatchCompareChain(statement, useFallthrough)) return;

    const scratch = `${DISPATCH_SCRATCH_PREFIX}${statement.scratchId}`;
    const sourceRegister = dispatchExpressionRegister(statement, this.allocation);
    const register = sourceRegister ?? this.allocation.registers[scratch];
    if (!register) {
      this.diagnostics.push({
        level: "error",
        message: `Internal: no scratch register reserved for dispatch at line ${statement.line}.`,
        line: statement.line,
      });
      return;
    }

    this.compileExpression(statement.expr);
    if (sourceRegister === undefined) {
      this.emitOp(
        0x40 + registerIndex(register),
        `X->П ${register}`,
        `dispatch scratch`,
        statement.line,
      );
    } else {
      this.optimizations.push({
        name: "dispatch-source-register",
        detail: `Reused R${register} as dispatch scratch for identifier expression.`,
      });
    }

    const endLabel = this.freshLabel("dispatch_end");
    let xContainsDispatchExpr = sourceRegister !== undefined;
    for (let index = 0; index < statement.cases.length; index += 1) {
      const dispatchCase = statement.cases[index]!;
      const nextLabel = this.freshLabel("dispatch_next");
      const lastCase = index === statement.cases.length - 1;
      if (index > 0 && !xContainsDispatchExpr) {
        this.emitOp(
          0x60 + registerIndex(register),
          `П->X ${register}`,
          "dispatch scratch recall",
          dispatchCase.line,
        );
        xContainsDispatchExpr = true;
      }
      if (xContainsDispatchExpr && isZeroExpression(dispatchCase.value)) {
        this.emitJump(0x5e, "F x=0", nextLabel, "zero-case mismatch", dispatchCase.line);
      } else {
        this.compileExpression(dispatchCase.value);
        this.emitOp(0x11, "-", "dispatch compare", dispatchCase.line);
        this.emitJump(0x5e, "F x=0", nextLabel, "case mismatch", dispatchCase.line);
        xContainsDispatchExpr = false;
      }
      this.compileStatements(dispatchCase.body);
      if (
        !this.statementsTerminate(dispatchCase.body) &&
        (!useFallthrough || !lastCase || statement.defaultBody !== undefined)
      ) {
        this.emitJump(0x51, "БП", endLabel, "dispatch end", dispatchCase.line);
      }
      this.emitLabel(nextLabel);
      xContainsDispatchExpr = xContainsDispatchExpr && isZeroExpression(dispatchCase.value);
    }
    if (statement.defaultBody) this.compileStatements(statement.defaultBody);
    this.emitLabel(endLabel);
  }

  private compileNumericResidualDispatchCompareChain(
    statement: Extract<StatementAst, { kind: "dispatch" }>,
    useFallthrough: boolean,
  ): boolean {
    if (statement.cases.length < 2) return false;
    const values = statement.cases.map((dispatchCase) => numericLiteralValue(dispatchCase.value));
    if (values.some((value) => value === undefined)) return false;
    const numericValues = values as number[];
    if (!numericResidualDispatchIsCheaper(statement, numericValues)) return false;

    this.compileExpression(statement.expr);
    const endLabel = this.freshLabel("dispatch_end");
    let comparedValue = 0;
    let hasComparedValue = false;
    for (let index = 0; index < statement.cases.length; index += 1) {
      const dispatchCase = statement.cases[index]!;
      const value = numericValues[index]!;
      const nextLabel = this.freshLabel("dispatch_next");
      const lastCase = index === statement.cases.length - 1;
      if (!hasComparedValue) {
        if (value !== 0) {
          this.emitNumberOrPreload(String(value));
          this.emitOp(0x11, "-", "dispatch compare", dispatchCase.line);
        }
        hasComparedValue = true;
      } else {
        const delta = comparedValue - value;
        if (delta !== 0) {
          this.emitNumberOrPreload(String(delta));
          this.emitOp(0x10, "+", "dispatch residual compare", dispatchCase.line);
        }
      }
      comparedValue = value;
      this.emitJump(0x5e, "F x=0", nextLabel, "case mismatch", dispatchCase.line);
      this.compileStatements(dispatchCase.body);
      if (
        !this.statementsTerminate(dispatchCase.body) &&
        (!useFallthrough || !lastCase || statement.defaultBody !== undefined)
      ) {
        this.emitJump(0x51, "БП", endLabel, "dispatch end", dispatchCase.line);
      }
      this.emitLabel(nextLabel);
    }
    if (statement.defaultBody) this.compileStatements(statement.defaultBody);
    this.emitLabel(endLabel);
    this.optimizations.push({
      name: "numeric-dispatch-residual-chain",
      detail: `Reused residual comparisons for numeric dispatch at line ${statement.line}.`,
    });
    return true;
  }

  private statementsTerminate(statements: StatementAst[]): boolean {
    return this.statementListTerminates(statements, new Set());
  }

  private statementListTerminates(statements: StatementAst[], seenProcs: Set<string>): boolean {
    const last = statements.at(-1);
    if (!last) return false;
    return this.statementTerminates(last, seenProcs);
  }

  private statementTerminates(statement: StatementAst, seenProcs: Set<string>): boolean {
    if (statement.kind === "halt" || statement.kind === "loop" || statement.kind === "trap") return true;
    if (statement.kind === "if") {
      return statement.elseBody !== undefined &&
        this.statementListTerminates(statement.thenBody, new Set(seenProcs)) &&
        this.statementListTerminates(statement.elseBody, new Set(seenProcs));
    }
    if (statement.kind === "dispatch") {
      return statement.defaultBody !== undefined &&
        this.statementListTerminates(statement.defaultBody, new Set(seenProcs)) &&
        statement.cases.every((dispatchCase) => this.statementListTerminates(dispatchCase.body, new Set(seenProcs)));
    }
    if (statement.kind !== "call") return false;
    const block = this.ast.blocks.find((candidate) => candidate.name === statement.block);
    if (block !== undefined) return block.mode !== "inline";
    const proc = this.ast.procs.find((candidate) => candidate.name === statement.block);
    if (proc === undefined || seenProcs.has(proc.name)) return false;
    seenProcs.add(proc.name);
    return this.statementListTerminates(proc.body, seenProcs);
  }

  private compileShow(displayName: string, line: number): void {
    const display = this.ast.displays.find((candidate) => candidate.name === displayName);
    if (!display) {
      this.diagnostics.push(buildDiagnostic("error", `Unknown display '${displayName}'.`, line));
      return;
    }

    if (this.compileTextDisplay(display, line)) return;
    if (display.items.some((item) => item.kind === "literal")) {
      this.diagnostics.push(buildDiagnostic(
        "error",
        `Screen '${display.name}' contains text fragments that are not lowerable for this program shape yet.`,
        line,
      ));
      return;
    }

    const helper = this.sharedDisplayHelper(display, line);
    if (helper !== undefined) {
      this.emitJump(0x53, "ПП", helper.label, `show ${display.name}`, line);
      this.optimizations.push({
        name: "packed-display-helper-call",
        detail: `Reused shared packed display helper for screen ${display.name}.`,
      });
      return;
    }

    this.compilePackedDisplayBody(display, line, true);
    this.reportPackedDisplayLowering(display);
  }

  private compilePackedDisplayBody(
    display: ProgramAst["displays"][number],
    line: number,
    reuseCurrentX: boolean,
  ): void {
    const sources = reuseCurrentX ? this.orderDisplaySources(display.sources) : display.sources;
    if (sources.length === 0) {
      this.emitNumber("0");
    } else {
      for (let index = 0; index < sources.length; index += 1) {
        const source = sources[index]!;
        if (!(index === 0 && source === this.currentXVariable)) {
          this.emitRecall(source, `display ${display.name} source`, line);
        }
        if (index > 0) this.emitOp(0x10, "+", "packed display combine", line);
      }
    }
    this.emitOp(0x50, "С/П", `show ${display.name}`, line);
  }

  private reportPackedDisplayLowering(display: ProgramAst["displays"][number]): void {
    const canUseDisplayBytes = machineSupports(this.machineProfile, "display-bytes");
    this.optimizations.push({
      name: "packed-display-lowering",
      detail: canUseDisplayBytes
        ? `Display ${display.name} may use display-byte encodings in later layout passes.`
        : `Display ${display.name} lowered as ordinary packed numeric output.`,
    });
  }

  private sharedDisplayHelper(
    display: ProgramAst["displays"][number],
    line: number,
  ): { display: ProgramAst["displays"][number]; label: string; line: number } | undefined {
    if (!this.shouldShareDisplay(display)) return undefined;
    const existing = this.displayHelpers.get(display.name);
    if (existing !== undefined) return existing;
    const helper = {
      display,
      label: `__display_${display.name}`,
      line,
    };
    this.displayHelpers.set(display.name, helper);
    return helper;
  }

  private shouldShareDisplay(display: ProgramAst["displays"][number]): boolean {
    if (display.items.some((item) => item.kind === "literal")) return false;
    const sources = display.sources.length;
    if (sources < 2) return false;
    const uses = this.displayUseCounts.get(display.name) ?? 0;
    if (uses < 2) return false;

    const inlineCost = estimatePackedDisplayBodyCost(sources);
    const helperCost = uses * 2 + inlineCost + 1;
    const inlineTotal = uses * inlineCost;
    return inlineTotal - helperCost >= DISPLAY_HELPER_MIN_SAVINGS;
  }

  private sharedShowSequenceHelper(
    firstName: string,
    secondName: string,
    line: number,
  ): {
    first: ProgramAst["displays"][number];
    second: ProgramAst["displays"][number];
    label: string;
    line: number;
  } | undefined {
    const first = this.ast.displays.find((candidate) => candidate.name === firstName);
    const second = this.ast.displays.find((candidate) => candidate.name === secondName);
    if (first === undefined || second === undefined) return undefined;
    if (!this.shouldShareShowSequence(first, second)) return undefined;
    const key = showSequenceKey(first.name, second.name);
    const existing = this.showSequenceHelpers.get(key);
    if (existing !== undefined) return existing;
    const helper = {
      first,
      second,
      label: `__showseq_${this.showSequenceHelpers.size}`,
      line,
    };
    this.showSequenceHelpers.set(key, helper);
    return helper;
  }

  private shouldShareShowSequence(
    first: ProgramAst["displays"][number],
    second: ProgramAst["displays"][number],
  ): boolean {
    if (first.items.some((item) => item.kind === "literal")) return false;
    if (second.items.some((item) => item.kind === "literal")) return false;
    const uses = this.showSequenceUseCounts.get(showSequenceKey(first.name, second.name)) ?? 0;
    if (uses < 2) return false;
    const bodyCost = estimatePackedDisplayBodyCost(first.sources.length) + estimatePackedDisplayBodyCost(second.sources.length);
    const inlineTotal = uses * (bodyCost + 1);
    const helperTotal = uses * 3 + bodyCost + 1;
    return inlineTotal - helperTotal >= DISPLAY_HELPER_MIN_SAVINGS;
  }

  private compileTextDisplay(display: ProgramAst["displays"][number], line: number): boolean {
    const [literal, source, ...rest] = display.items;
    if (
      literal?.kind !== "literal" ||
      literal.text !== "BEEr " ||
      source?.kind !== "source" ||
      rest.length !== 0
    ) {
      return false;
    }

    const field = this.findStateField(source.name);
    if (field === undefined || (field.min ?? 0) < 0 || (field.max ?? 0) > 99) return false;
    if (this.allocation.registers[source.name] !== "0") return false;
    if (this.currentAddress() !== 0) return false;

    const scratchRegisters = new Set<RegisterName>(["1", "2", "7", "8", "a"]);
    const conflicting = Object.entries(this.allocation.registers)
      .filter(([name, register]) => name !== source.name && scratchRegisters.has(register));
    if (conflicting.length > 0) return false;

    this.emitTwoDigitTextDisplay(source.name, line);
    this.optimizations.push({
      name: "screen-text-lowering",
      detail: `Lowered screen ${display.name} as visible text ${JSON.stringify(literal.text)} plus ${source.name}.`,
    });
    return true;
  }

  private emitTwoDigitTextDisplay(source: string, line: number): void {
    this.emitRecall(source, "text display verse", line);
    this.emitOp(0x01, "1", "text tens divisor", line);
    this.emitOp(0x00, "0", "text tens divisor", line);
    this.emitOp(0x13, "/", "text tens", line);
    this.emitOp(0x34, "К [x]", "text tens integer", line);
    this.emitOp(0x41, "X->П 1", "text tens scratch", line);
    this.emitOp(0x0f, "F Вx", "text ones from last X", line);
    this.emitOp(0x35, "К {x}", "text ones fraction", line);
    this.emitOp(0x01, "1", "text ones scale", line);
    this.emitOp(0x00, "0", "text ones scale", line);
    this.emitOp(0x12, "*", "text ones", line);
    this.emitOp(0x42, "X->П 2", "text ones scratch", line);
    this.emitOp(0x01, "1", "text display tens prefix", line);
    this.emitOp(0x01, "1", "text display tens prefix", line);
    this.emitOp(0x48, "X->П 8", "text display prefix scratch", line);
    this.emitOp(0x01, "1", "text display tens offset", line);
    this.emitOp(0x02, "2", "text display tens offset", line);
    this.emitOp(0x47, "X->П 7", "text display digit offset", line);
    this.emitOp(0x62, "П->X 2", "text display ones digit", line);
    this.emitJump(0x53, "ПП", 34, "text digit renderer", line);
    this.emitOp(0x4a, "X->П a", "text display rendered ones", line);
    this.emitOp(0x01, "1", "text display ones prefix", line);
    this.emitOp(0x04, "4", "text display ones prefix", line);
    this.emitOp(0x48, "X->П 8", "text display prefix scratch", line);
    this.emitOp(0x01, "1", "text display ones offset", line);
    this.emitOp(0x03, "3", "text display ones offset", line);
    this.emitOp(0x47, "X->П 7", "text display digit offset", line);
    this.emitOp(0x61, "П->X 1", "text display tens digit", line);
    this.emitJump(0x53, "ПП", 34, "text digit renderer", line);
    this.emitOp(0x6a, "П->X a", "text display rendered ones", line);
    this.emitOp(0x0e, "В↑", "show text", line);
    this.emitOp(0x50, "С/П", "show text", line);
    this.emitOp(0x06, "6", "text digit renderer", line);
    this.emitOp(0x11, "-", "text digit renderer", line);
    this.emitOp(0x0b, "/-/", "text digit renderer", line);
    this.emitJump(0x5c, "F x<0", 45, "text digit renderer", line);
    this.emitOp(0x09, "9", "text digit complement", line);
    this.emitOp(0x10, "+", "text digit complement", line);
    this.emitOp(0xd7, "К П->X 7", "text display byte", line);
    this.emitOp(0x10, "+", "text display byte", line);
    this.emitOp(0x3a, "К ИНВ", "text visible digit", line);
    this.emitOp(0x52, "В/О", "text digit return", line);
    this.emitOp(0x01, "1", "text digit complement", line);
    this.emitOp(0x10, "+", "text digit complement", line);
    this.emitOp(0xd7, "К П->X 7", "text display byte", line);
    this.emitOp(0x10, "+", "text display byte", line);
    this.emitOp(0x3a, "К ИНВ", "text visible digit", line);
    this.emitOp(0xd8, "К П->X 8", "text display prefix", line);
    this.emitOp(0x0e, "В↑", "text digit return value", line);
    this.emitOp(0x52, "В/О", "text digit return", line);
  }

  private findStateField(name: string): StateFieldAst | undefined {
    for (const state of this.ast.states) {
      const field = state.fields.find((candidate) => candidate.name === name);
      if (field !== undefined) return field;
    }
    return undefined;
  }

  private currentAddress(): number {
    return this.items.filter((item) => item.kind !== "label").length;
  }

  private compileBlockCall(blockName: string, line: number): void {
    const block = this.ast.blocks.find((candidate) => candidate.name === blockName);
    const proc = this.ast.procs.find((candidate) => candidate.name === blockName);
    if (!block && proc) {
      if (this.inlineProcNames.has(proc.name)) {
        this.compileStatements(proc.body);
        this.optimizations.push({
          name: "single-use-rule-inline",
          detail: `Inlined single-use rule ${proc.name} at line ${line}.`,
        });
        return;
      }
      if (this.statementsTerminate(proc.body)) {
        this.emitJump(0x51, "БП", proc.name, `terminal rule ${proc.name}`, line);
        this.optimizations.push({
          name: "terminal-rule-tail-call",
          detail: `Compiled terminal rule ${proc.name} as a direct jump instead of a subroutine call.`,
        });
        return;
      }
      this.emitJump(0x53, "ПП", proc.name, `proc call ${proc.name}`, line);
      this.optimizations.push({
        name: "proc-call-lowering",
        detail: `Compiled call to rule ${proc.name} as ПП/В/О subroutine.`,
      });
      return;
    }
    if (!block) {
      this.diagnostics.push(buildDiagnostic("error", `Unknown block '${blockName}'.`, line));
      return;
    }
    if (block.mode === "inline") {
      this.compileStatements(block.body);
      this.optimizations.push({
        name: "inline-block",
        detail: `Inlined block ${block.name} at line ${line}.`,
      });
      return;
    }
    this.emitJump(0x51, "БП", block.name, `${block.mode} call ${block.name}`, line);
    this.optimizations.push({
      name: block.mode === "shared_tail" ? "shared-tail-layout" : "tail-call-layout",
      detail: `Compiled call to ${block.name} as direct tail jump.`,
    });
  }

  private compileTrap(statement: TrapStatementAst): void {
    this.compileExpression(statement.expr);
    const mapping: Record<TrapStatementAst["trap"], [number, string]> = {
      zero: [0x23, "F 1/x"],
      nonpositive: [0x17, "F lg"],
      negative: [0x21, "F sqrt"],
      gt_one: [0x19, "F sin^-1"],
      ge_100: [0x15, "F 10^x"],
      // К °->′" rejects a fractional part >= 0.6 (no valid minutes/seconds),
      // making it a one-cell trap for that domain.
      frac_ge_06: [0x2a, "К °->′\""],
    };
    const [opcode, mnemonic] = mapping[statement.trap];
    this.emitOp(
      opcode,
      mnemonic,
      `trap ${statement.trap}`,
      statement.line,
    );
    this.optimizations.push({
      name: "error-stop",
      detail: `Used ${mnemonic} as trap ${statement.trap} at line ${statement.line}.`,
    });
  }

  private compileUnitDecrement(statement: Extract<StatementAst, { kind: "assign" }>): boolean {
    if (!isUnitDecrementExpression(statement.target, statement.expr)) return false;
    const register = this.allocation.registers[statement.target];
    if (register === undefined) return false;
    const opcode = flOpcode(register);
    if (opcode === undefined) return false;
    const after = this.freshLabel("fl_decrement_done");
    this.emitJump(opcode, getOpcode(opcode).name, after, `decrement ${statement.target}`, statement.line);
    this.emitLabel(after);
    this.optimizations.push({
      name: "fl-unit-decrement",
      detail: `Lowered ${statement.target} -= 1 through ${getOpcode(opcode).name}.`,
    });
    return true;
  }

  private compileCondition(
    condition: ConditionAst,
    falseLabel: string,
    line: number,
  ): void {
    if (this.compileNegativeZeroThresholdFlow(condition, falseLabel, line)) return;

    const selected = selectCheaperEquivalentCondition(
      condition,
      this.ast,
      new Set(Object.keys(this.allocation.constants)),
    );
    if (selected.changed) {
      this.optimizations.push({
        name: "comparison-boundary-normalization",
        detail: `Normalized ${conditionToText(condition)} to ${conditionToText(selected.condition)} at line ${line}.`,
      });
    }
    const compiledCondition = selected.condition;
    if (isZeroExpression(compiledCondition.right) && canTestAgainstZeroDirectly(compiledCondition.op)) {
      const usedSpatialHit = this.compileBitHasConditionWithSpatialHelper(compiledCondition.left, line);
      if (!usedSpatialHit) {
        this.compileExpression(compiledCondition.left);
      }
      const opcode = directTestOpcode(compiledCondition.op);
      this.emitJump(opcode, getOpcode(opcode).name, falseLabel, `false branch for ${condition.op}`, line);
      this.optimizations.push({
        name: usedSpatialHit ? "spatial-hit-condition-helper" : "zero-condition-test",
        detail: usedSpatialHit
          ? `Tested bit_has() through the shared spatial hit helper at line ${line}.`
          : `Tested ${compiledCondition.op} 0 without materializing a zero literal at line ${line}.`,
      });
      return;
    }
    if (this.compileEqualityWithCurrentX(compiledCondition, falseLabel, line)) return;
    if (compiledCondition.op === ">" || compiledCondition.op === "<=") {
      this.compileExpression(compiledCondition.right);
      this.compileExpression(compiledCondition.left);
    } else {
      this.compileExpression(compiledCondition.left);
      this.compileExpression(compiledCondition.right);
    }
    this.emitOp(0x11, "-", "condition compare", line);

    const opcode =
      compiledCondition.op === "<" || compiledCondition.op === ">"
        ? 0x5c
        : compiledCondition.op === ">=" || compiledCondition.op === "<="
          ? 0x59
          : compiledCondition.op === "=="
            ? 0x5e
            : 0x57;
    const mnemonic = getOpcode(opcode).name;
    this.emitJump(opcode, mnemonic, falseLabel, `false branch for ${compiledCondition.op}`, line);
  }

  private compileNegativeZeroThresholdFlow(
    condition: ConditionAst,
    falseLabel: string,
    line: number,
  ): boolean {
    const register = this.allocation.negativeZeroDegree;
    const threshold = matchNegativeZeroThresholdCondition(condition, this.ast);
    if (threshold === undefined) return false;

    const preloadedConstants = new Set(Object.keys(this.allocation.constants));
    const selectedCost = estimateNegativeZeroThresholdFlowCost(threshold, preloadedConstants);
    const ordinaryCost = conditionCompileCost(
      selectCheaperEquivalentCondition(condition, this.ast, preloadedConstants).condition,
      preloadedConstants,
    );
    if (register === undefined || selectedCost >= ordinaryCost) {
      this.candidates.push({
        site: `if@${line}`,
        variant: "negative-zero-threshold-flow",
        steps: selectedCost,
        selected: false,
        reason: selectedCost >= ordinaryCost
          ? `Negative-zero threshold flow estimated at ${selectedCost} cells; ordinary condition was shorter (${ordinaryCost}).`
          : "Negative-zero threshold flow matched, but no compiler-owned negative-zero register was available.",
      });
      return false;
    }

    this.emitNegativeZeroThresholdRaw(threshold.value, numberExpression(threshold.bound), register, line);
    const opcode = threshold.truth === "ge" ? 0x57 : 0x5e;
    this.emitJump(opcode, getOpcode(opcode).name, falseLabel, `negative-zero false branch for ${condition.op}`, line);
    this.optimizations.push({
      name: "negative-zero-threshold-flow",
      detail: `Tested ${conditionToText(condition)} through preloaded ${NEGATIVE_ZERO_DEGREE_PRELOAD_VALUE} in R${register}.`,
    });
    return true;
  }

  private emitNegativeZeroThresholdRaw(
    value: ExpressionAst,
    bound: ExpressionAst,
    register: RegisterName,
    line?: number,
  ): void {
    this.compileExpression(divideExpressions(value, bound));
    this.emitOp(0x60 + registerIndex(register), `П->X ${register}`, "negative-zero threshold sentinel", line);
    this.emitOp(0x14, "X↔Y", "place threshold value above negative-zero sentinel", line);
    this.emitOp(0x12, "*", "negative-zero threshold zero-through", line);
    this.emitOp(0x0e, "В↑", "normalize negative-zero threshold result", line);
  }

  private compileBitHasConditionWithSpatialHelper(expr: ExpressionAst, line: number): boolean {
    if (expr.kind !== "call" || expr.callee.toLowerCase() !== "bit_has" || expr.args.length !== 2) return false;
    const [mask, index] = expr.args;
    if (mask?.kind !== "identifier" || index === undefined) return false;
    const scratch = spatialHitScratchName(mask.name);
    if (!this.allocation.registers[scratch]) return false;
    const helper = this.ensureSpatialHitHelper(mask.name, scratch);
    this.compileExpression(index);
    this.emitJump(0x53, "ПП", helper.label, `spatial hit ${mask.name}`, line);
    return true;
  }

  private compileEqualityWithCurrentX(
    condition: ConditionAst,
    falseLabel: string,
    line: number,
  ): boolean {
    if (condition.op !== "==" && condition.op !== "!=") return false;
    const reused = this.currentXVariable;
    if (
      condition.right.kind === "identifier" &&
      condition.right.name === reused &&
      isSimpleStackLoad(condition.left)
    ) {
      this.compileExpression(condition.left);
    } else if (
      condition.left.kind === "identifier" &&
      condition.left.name === reused &&
      isSimpleStackLoad(condition.right)
    ) {
      this.compileExpression(condition.right);
    } else {
      return false;
    }
    this.emitOp(0x11, "-", "condition compare", line);
    const opcode = condition.op === "==" ? 0x5e : 0x57;
    this.emitJump(opcode, getOpcode(opcode).name, falseLabel, `false branch for ${condition.op}`, line);
    this.optimizations.push({
      name: "condition-current-x-reuse",
      detail: `Reused ${reused} already in X for equality comparison at line ${line}.`,
    });
    return true;
  }

  private compileExpression(expr: ExpressionAst): void {
    const helper = this.sharedExpressionHelper(expr);
    if (helper !== undefined) {
      this.emitJump(0x53, "ПП", helper.label, `expr ${expressionToIntentText(expr)}`);
      this.optimizations.push({
        name: "expression-helper-call",
        detail: `Reused shared helper for ${expressionToIntentText(expr)}.`,
      });
      return;
    }

    switch (expr.kind) {
      case "number":
        this.emitNumberOrPreload(expr.raw);
        return;
      case "identifier": {
        const constant = this.constants.get(expr.name);
        if (constant) {
          if (this.constantStack.has(expr.name)) {
            this.diagnostics.push({
              level: "error",
              message: `Cyclic constant reference '${expr.name}'.`,
            });
            return;
          }
          this.constantStack.add(expr.name);
          try {
            this.compileExpression(constant);
          } finally {
            this.constantStack.delete(expr.name);
          }
          return;
        }
        this.emitRecall(expr.name, `recall ${expr.name}`);
        return;
      }
      case "unary":
        if (expr.op === "-" && expr.expr.kind === "number") {
          this.emitNumberOrPreload(negatedNumberLiteral(expr.expr.raw));
          return;
        }
        this.compileExpression(expr.expr);
        this.emitOp(0x0b, "/-/", "unary minus");
        return;
      case "binary":
        if (expr.op === "-" && isNumericValue(expr.left, 0)) {
          this.compileExpression(expr.right);
          this.emitOp(0x0b, "/-/", "unary minus");
          return;
        }
        if (this.compileRemainderByConstant(expr)) {
          return;
        }
        if ((expr.op === "+" || expr.op === "*") && this.compileCommutativeWithCurrentX(expr)) {
          return;
        }
        if (this.compileStackDuplicatedBinary(expr)) {
          return;
        }
        this.compileExpression(expr.left);
        this.compileExpression(expr.right);
        this.emitOp(binaryOpcode(expr.op), expr.op, `expr ${expr.op}`);
        return;
      case "call":
        this.compileCall(expr);
        return;
    }
  }

  private orderDisplaySources(sources: string[]): string[] {
    if (this.currentXVariable === undefined) return sources;
    const index = sources.indexOf(this.currentXVariable);
    if (index <= 0) return sources;
    this.optimizations.push({
      name: "display-stack-reuse",
      detail: `Reordered packed display inputs to reuse ${this.currentXVariable} already in X.`,
    });
    return [
      this.currentXVariable,
      ...sources.slice(0, index),
      ...sources.slice(index + 1),
    ];
  }

  private compileCommutativeWithCurrentX(expr: Extract<ExpressionAst, { kind: "binary" }>): boolean {
    if (this.currentXVariable === undefined) return false;
    if (expr.left.kind === "identifier" && expr.left.name === this.currentXVariable && isSimpleStackLoad(expr.right)) {
      this.compileExpression(expr.right);
      this.emitOp(binaryOpcode(expr.op), expr.op, `expr ${expr.op}`);
      this.optimizations.push({
        name: "stack-current-x-scheduling",
        detail: `Reused ${expr.left.name} already in X for commutative ${expr.op}.`,
      });
      return true;
    }
    if (expr.right.kind === "identifier" && expr.right.name === this.currentXVariable && isSimpleStackLoad(expr.left)) {
      this.compileExpression(expr.left);
      this.emitOp(binaryOpcode(expr.op), expr.op, `expr ${expr.op}`);
      this.optimizations.push({
        name: "stack-current-x-scheduling",
        detail: `Reused ${expr.right.name} already in X for commutative ${expr.op}.`,
      });
      return true;
    }
    return false;
  }

  // Stack-duplicate a repeated pure operand: `e op e` becomes compute(e), В↑,
  // op, so the shared value is reused from the stack (Y) instead of recomputed.
  // Only applies to pure operands (so one evaluation equals two) and only when it
  // actually saves cells versus recomputing the operand.
  private compileStackDuplicatedBinary(expr: Extract<ExpressionAst, { kind: "binary" }>): boolean {
    if (!expressionEquals(expr.left, expr.right)) return false;
    if (!isPureExpression(expr.left)) return false;
    // В↑ costs one cell, so duplication only pays off when recomputing the
    // operand would cost more than one cell.
    if (estimateExpressionCost(expr.left) <= 1) return false;
    this.compileExpression(expr.left);
    this.emitOp(0x0e, "В↑", "duplicate repeated operand through stack");
    this.emitOp(binaryOpcode(expr.op), expr.op, `expr ${expr.op}`);
    this.optimizations.push({
      name: "stack-current-x-scheduling",
      detail: `Duplicated ${expressionToIntentText(expr.left)} through the stack (В↑) for ${expr.op} instead of recomputing it.`,
    });
    return true;
  }

  // Shared tail for the integer/fractional parts of one pure operand. When two
  // adjacent assignments take int(e) and frac(e) of the same pure expression e,
  // compute e once, duplicate it through the stack (В↑), take К [x] for the
  // integer target, swap the saved copy back with X↔Y, and take К {x} for the
  // fractional target. Both parts are produced by their own opcodes (identical
  // behavior, including the -0 fractional result of negative integers), and the
  // operand is evaluated once instead of twice.
  private compileIntFracSharedTail(
    first: Extract<StatementAst, { kind: "assign" }>,
    second: Extract<StatementAst, { kind: "assign" }>,
  ): boolean {
    const a = matchIntOrFracCall(first.expr);
    const b = matchIntOrFracCall(second.expr);
    if (a === undefined || b === undefined) return false;
    if (a.fn === b.fn) return false;
    if (!expressionEquals(a.arg, b.arg)) return false;
    if (!expressionPureForSubstitution(a.arg)) return false;
    if (first.target === second.target) return false;
    // В↑ + X↔Y add two cells over a single part, so the shared tail only wins
    // when recomputing the operand would cost more than that.
    if (estimateExpressionCost(a.arg) <= 2) return false;

    const intStatement = a.fn === "int" ? first : second;
    const fracStatement = a.fn === "frac" ? first : second;

    this.compileExpression(a.arg);
    this.emitOp(0x0e, "В↑", "duplicate operand for shared int/frac tail");
    this.emitOp(0x34, "К [x]", "int()");
    this.emitStore(intStatement.target, `set ${intStatement.target}`, intStatement.line);
    this.emitOp(0x14, "XY", "restore saved operand for frac()");
    this.emitOp(0x35, "К {x}", "frac()");
    this.emitStore(fracStatement.target, `set ${fracStatement.target}`, fracStatement.line);
    this.optimizations.push({
      name: "int-frac-shared-tail",
      detail: `Computed ${expressionToIntentText(a.arg)} once and derived int()/frac() through a shared В↑/X↔Y tail.`,
    });
    return true;
  }

  private compileRemainderByConstant(expr: Extract<ExpressionAst, { kind: "binary" }>): boolean {
    const matched = matchRemainderByConstant(expr);
    if (matched === undefined) return false;
    this.compileExpression(matched.value);
    this.compileExpression(matched.divisor);
    this.emitOp(0x13, "/", "remainder quotient");
    this.emitOp(0x35, "К {x}", "remainder fractional part");
    this.compileExpression(matched.divisor);
    this.emitOp(0x12, "*", "remainder scale");
    this.optimizations.push({
      name: "remainder-fraction-lowering",
      detail: `Lowered ${expressionToIntentText(expr)} without recomputing the dividend.`,
    });
    return true;
  }

  private sharedExpressionHelper(expr: ExpressionAst): { expr: ExpressionAst; label: string; line?: number } | undefined {
    if (this.emittingExpressionHelper) return undefined;
    if (!this.shouldShareExpression(expr)) return undefined;
    const key = expressionToIntentText(expr);
    const existing = this.expressionHelpers.get(key);
    if (existing !== undefined) return existing;
    const helper = {
      expr,
      label: `__expr_${this.expressionHelpers.size}`,
    };
    this.expressionHelpers.set(key, helper);
    return helper;
  }

  private shouldShareExpression(expr: ExpressionAst): boolean {
    if (!expressionPureForSubstitution(expr)) return false;
    if (expr.kind === "number" || expr.kind === "identifier") return false;
    const cost = estimateExpressionCost(expr);
    if (cost < EXPRESSION_HELPER_MIN_COST) return false;
    const key = expressionToIntentText(expr);
    const uses = this.expressionUseCounts.get(key)?.count ?? 0;
    if (uses < 2) return false;
    const inlineTotal = uses * cost;
    const helperTotal = uses * 2 + cost + 1;
    return inlineTotal - helperTotal >= EXPRESSION_HELPER_MIN_SAVINGS;
  }

  private compileCall(expr: Extract<ExpressionAst, { kind: "call" }>): void {
    const name = expr.callee.toLowerCase();
    if (name === "direction") {
      this.compileDirectionCall(expr);
      return;
    }
    if (name === "neighbor_count" || name === "line_count") {
      if (this.compileSpatialCountCall(name, expr)) return;
    }
    if (name === "__spatial_hit") {
      if (this.compileSpatialHitCall(expr)) return;
    }
    if (name === NEGATIVE_ZERO_DEGREE_SELECTOR_GE) {
      if (this.compileNegativeZeroDegreeSelectorCall(expr)) return;
    }
    if (isTicTacToeMacroName(name) && ticTacToeMacroArity(name) !== expr.args.length) {
      this.diagnostics.push({
        level: "error",
        message: `${expr.callee}() expects ${ticTacToeMacroArity(name)} arguments, got ${expr.args.length}.`,
      });
      return;
    }
    const macro = ticTacToeExpressionMacro(name, expr.args);
    if (macro !== undefined) {
      this.compileExpression(macro);
      this.optimizations.push({
        name: "tic-tac-toe-primitive-lowering",
        detail: `Lowered ${expr.callee}() to reusable 4x4 grid/packed-line arithmetic.`,
      });
      return;
    }

    const zeroArgOpcodes: Record<string, [number, string]> = {
      random: [0x3b, "К СЧ"],
      pi: [0x20, "F pi"],
    };
    const zeroArgOpcode = zeroArgOpcodes[name];
    if (zeroArgOpcode !== undefined) {
      if (expr.args.length !== 0) {
        this.diagnostics.push({
          level: "error",
          message: `${expr.callee}() takes no arguments, got ${expr.args.length}.`,
        });
        return;
      }
      this.emitOp(zeroArgOpcode[0], zeroArgOpcode[1], `${expr.callee}()`);
      return;
    }

    if (name === "pow") {
      if (expr.args.length !== 2) {
        this.diagnostics.push({
          level: "error",
          message: "Function pow expects two arguments.",
        });
        return;
      }
      this.compileExpression(expr.args[1]!);
      this.compileExpression(expr.args[0]!);
      this.emitOp(0x24, "F x^y", `${expr.callee}()`);
      return;
    }

    const binaryOpcodes: Record<string, [number, string]> = {
      max: [0x36, "К max"],
      bit_and: [0x37, "К ∧"],
      bit_or: [0x38, "К ∨"],
      bit_xor: [0x39, "К ⊕"],
    };
    const binaryCall = binaryOpcodes[name];
    if (binaryCall !== undefined) {
      if (expr.args.length !== 2) {
        this.diagnostics.push({
          level: "error",
          message: `Function ${expr.callee} expects two arguments.`,
        });
        return;
      }
      this.compileExpression(expr.args[0]!);
      this.compileExpression(expr.args[1]!);
      this.emitOp(binaryCall[0], binaryCall[1], `${expr.callee}()`);
      return;
    }

    if (expr.args.length !== 1) {
      this.diagnostics.push({
        level: "error",
        message: `Function ${expr.callee} expects one argument.`,
      });
      return;
    }
    this.compileExpression(expr.args[0]!);
    const opcodes: Record<string, [number, string]> = {
      abs: [0x31, "К |x|"],
      sign: [0x32, "К ЗН"],
      int: [0x34, "К [x]"],
      frac: [0x35, "К {x}"],
      sqr: [0x22, "F x^2"],
      inv: [0x23, "F 1/x"],
      sqrt: [0x21, "F sqrt"],
      lg: [0x17, "F lg"],
      ln: [0x18, "F ln"],
      sin: [0x1c, "F sin"],
      cos: [0x1d, "F cos"],
      tg: [0x1e, "F tg"],
      asin: [0x19, "F sin^-1"],
      acos: [0x1a, "F cos^-1"],
      atg: [0x1b, "F tg^-1"],
      exp: [0x16, "F e^x"],
      pow10: [0x15, "F 10^x"],
      bit_not: [0x3a, "К ИНВ"],
      to_min: [0x26, "К °->′"],
      to_sec: [0x2a, "К °->′\""],
      from_sec: [0x30, "К °<-′\""],
      from_min: [0x33, "К °<-′"],
    };
    const opcode = opcodes[name];
    if (!opcode) {
      this.diagnostics.push({
        level: "error",
        message: `Unknown function ${expr.callee}.`,
      });
      return;
    }
    this.emitOp(opcode[0], opcode[1], `${expr.callee}()`);
  }

  private compileDirectionCall(expr: Extract<ExpressionAst, { kind: "call" }>): void {
    if (expr.args.length !== 1) {
      this.diagnostics.push({
        level: "error",
        message: `direction() expects one keypad argument, got ${expr.args.length}.`,
      });
      return;
    }
    const arg = expr.args[0]!;
    if (arg.kind !== "identifier") {
      this.diagnostics.push({
        level: "error",
        message: "direction() currently requires an identifier argument so the optimizer can reuse its register.",
      });
      return;
    }
    const keyRegister = this.allocation.registers[arg.name];
    if (!keyRegister) {
      this.diagnostics.push({
        level: "error",
        message: `Unknown direction key '${arg.name}'.`,
      });
      return;
    }

    const notFloor = this.freshLabel("direction_not_floor");
    const yAxis = this.freshLabel("direction_y_axis");
    const done = this.freshLabel("direction_done");

    this.emitOp(0x60 + registerIndex(keyRegister), `П->X ${keyRegister}`, "direction key");
    this.emitOp(0x31, "К |x|", "direction floor test");
    this.emitNumberOrPreload("5");
    this.emitOp(0x11, "-", "direction abs(key)-5");
    this.emitJump(0x5e, "F x=0", notFloor, "direction not floor");

    this.emitOp(0x60 + registerIndex(keyRegister), `П->X ${keyRegister}`, "direction floor key");
    this.emitNumberOrPreload("20");
    this.emitOp(0x12, "*", "direction floor delta");
    this.emitJump(0x51, "БП", done, "direction done");

    this.emitLabel(notFloor);
    this.emitOp(0x31, "К |x|", "direction x-axis test");
    this.emitNumberOrPreload("1");
    this.emitOp(0x11, "-", "direction axis discriminator");
    this.emitJump(0x5e, "F x=0", yAxis, "direction y-axis");

    this.emitOp(0x60 + registerIndex(keyRegister), `П->X ${keyRegister}`, "direction x key");
    this.emitNumberOrPreload("5");
    this.emitOp(0x11, "-", "direction key-5");
    this.emitNumberOrPreload("10");
    this.emitOp(0x12, "*", "direction x delta");
    this.emitJump(0x51, "БП", done, "direction done");

    this.emitLabel(yAxis);
    this.emitNumberOrPreload("5");
    this.emitOp(0x60 + registerIndex(keyRegister), `П->X ${keyRegister}`, "direction y key");
    this.emitOp(0x11, "-", "direction 5-key");
    this.emitNumberOrPreload("3");
    this.emitOp(0x13, "/", "direction y delta");

    this.emitLabel(done);
    this.optimizations.push({
      name: "direction-keypad-lowering",
      detail: `Lowered direction(${arg.name}) through a shared keypad geometry formula.`,
    });
  }

  private compileSpatialCountCall(name: "neighbor_count" | "line_count", expr: Extract<ExpressionAst, { kind: "call" }>): boolean {
    if (expr.args.length !== 2) {
      this.diagnostics.push({
        level: "error",
        message: `${name}() expects two arguments, got ${expr.args.length}.`,
      });
      return true;
    }
    if (name === "line_count" && this.compileSpatialLineCountLoop(expr)) return true;
    const expanded = spatialCountExpression(name, expr.args, this.ast);
    if (expanded === undefined) return false;
    this.compileExpression(expanded);
    this.optimizations.push({
      name: "spatial-count-hit-helper",
      detail: `Lowered ${name}() through shared spatial hit helper calls.`,
    });
    return true;
  }

  private compileSpatialNeighborCountLoop(expr: Extract<ExpressionAst, { kind: "call" }>): boolean {
    const [mask, cell] = expr.args;
    if (mask?.kind !== "identifier" || cell === undefined) return false;
    const board = boardForCellMask(mask, this.ast);
    const scratch = spatialCountScratchNames();
    if (scratch.some((name) => this.allocation.registers[name] === undefined)) return false;
    const progressions = spatialNeighborProgressions(board);
    this.emitSpatialNeighborCountLoopBody(mask.name, cell, progressions, undefined);
    this.optimizations.push({
      name: "spatial-neighbor-count-loop",
      detail: `Lowered neighbor_count(${mask.name}, ...) as spatial hit loops.`,
    });
    return true;
  }

  private emitSpatialNeighborCountLoopBody(
    hitMask: string,
    cell: ExpressionAst,
    progressions: SpatialLineProgression[],
    sourceLine: number | undefined,
  ): void {
    const scratch = spatialCountScratchNames();
    const total = scratch[0]!;
    const offset = scratch[2]!;
    const counter = scratch[3]!;

    this.emitNumber("0");
    this.emitStore(total, "neighbor_count total", sourceLine);
    for (const progression of progressions) {
      this.compileExpression(progression.startOffset);
      this.emitStore(offset, "neighbor_count offset", sourceLine);
      this.emitNumber(String(progression.count));
      this.emitStore(counter, "neighbor_count counter", sourceLine);

      const start = this.freshLabel("neighbor_count_loop");
      this.emitLabel(start);
      this.compileExpression(addExpressions(cell, { kind: "identifier", name: offset }));
      const helper = this.ensureSpatialHitHelper(hitMask, spatialHitScratchName(hitMask));
      this.emitJump(0x53, "ПП", helper.label, `spatial hit ${hitMask}`, sourceLine);
      this.emitRecall(total, "neighbor_count total");
      this.emitOp(0x10, "+", "neighbor_count add hit");
      this.emitStore(total, "neighbor_count total");

      this.emitRecall(offset, "neighbor_count offset");
      this.compileExpression(progression.step);
      this.emitOp(0x10, "+", "neighbor_count next offset");
      this.emitStore(offset, "neighbor_count offset");

      const counterRegister = this.allocation.registers[counter];
      const flCounterOpcode = counterRegister === undefined ? undefined : flOpcode(counterRegister);
      if (flCounterOpcode !== undefined) {
        this.emitJump(flCounterOpcode, getOpcode(flCounterOpcode).name, start, "neighbor_count loop", sourceLine);
        this.optimizations.push({
          name: "spatial-count-fl-loop",
          detail: `Used ${getOpcode(flCounterOpcode).name} for neighbor_count loop counter.`,
        });
      } else {
        this.emitRecall(counter, "neighbor_count counter");
        this.emitNumber("1");
        this.emitOp(0x11, "-", "neighbor_count decrement");
        this.emitStore(counter, "neighbor_count counter");
        this.emitRecall(counter, "neighbor_count counter");
        this.emitJump(0x5e, "F x=0", start, "neighbor_count loop", sourceLine);
      }
    }
    this.emitRecall(total, "neighbor_count result");
  }

  private compileSpatialLineCountLoop(expr: Extract<ExpressionAst, { kind: "call" }>): boolean {
    const [mask, cell] = expr.args;
    if (mask?.kind !== "identifier" || cell === undefined) return false;
    const board = boardForCellMask(mask, this.ast);
    if (board === undefined) return false;
    const scratch = spatialCountScratchNames();
    if (scratch.some((name) => this.allocation.registers[name] === undefined)) return false;

    const maskScratch = spatialCountMaskScratchName();
    const useSharedMask = this.lineCountCallCount > 1 && this.allocation.registers[maskScratch] !== undefined;
    const hitMask = useSharedMask ? maskScratch : mask.name;
    const helper = this.sharedLineCountHelper(mask, cell, board, undefined);
    if (helper !== undefined) {
      this.compileExpression(mask);
      this.emitJump(0x53, "ПП", helper.label, `line_count ${mask.name}`, undefined);
      this.optimizations.push({
        name: "spatial-line-count-helper-call",
        detail: `Reused shared line_count helper for ${mask.name}.`,
      });
      return true;
    }

    if (useSharedMask) {
      this.compileExpression(mask);
      this.emitStore(maskScratch, "line count mask", undefined);
    }
    this.emitSpatialLineCountLoopBody(hitMask, cell, board, undefined);
    return true;
  }

  private emitSpatialLineCountLoopBody(
    hitMask: string,
    cell: ExpressionAst,
    board: V2BoardAst,
    sourceLine: number | undefined,
  ): void {
    this.emitSpatialProgressionCountLoopBody(
      hitMask,
      cell,
      spatialLineProgressions(board, cell),
      board.width <= 4 && board.height <= 4,
      sourceLine,
      "line_count",
    );
  }

  private emitSpatialProgressionCountLoopBody(
    hitMask: string,
    cell: ExpressionAst,
    progressions: SpatialLineProgression[],
    useMax: boolean,
    sourceLine: number | undefined,
    operation: "line_count" | "neighbor_count",
  ): void {
    const scratch = spatialCountScratchNames();
    const total = scratch[0]!;
    const line = scratch[1]!;
    const offset = scratch[2]!;
    const counter = scratch[3]!;
    const counterRegister = this.allocation.registers[counter];
    const helperTakesCounterInX = counterRegister !== undefined && flOpcode(counterRegister) !== undefined;

    this.emitZero(`${operation} total`, sourceLine);
    this.emitStore(total, `${operation} total`, sourceLine);
    if (!useMax && progressions.length >= 3) {
      const helper = this.ensureSpatialSumLoopHelper(hitMask, cell, operation, sourceLine);
      for (const progression of progressions) {
        this.compileExpression(progression.startOffset);
        this.emitStore(offset, `${operation} offset`, sourceLine);
        this.compileExpression(progression.step);
        this.emitStore(line, `${operation} step`, sourceLine);
        if (!isNumericValue(progression.step, progression.count)) {
          this.emitNumberOrPreload(String(progression.count));
        }
        if (!helperTakesCounterInX) this.emitStore(counter, `${operation} counter`, sourceLine);
        this.emitJump(0x53, "ПП", helper.label, `${operation} progression`, sourceLine);
      }
      this.optimizations.push({
        name: "spatial-sum-loop-helper-call",
        detail: `Reused shared ${operation} progression helper for ${hitMask}.`,
      });
      return;
    }
    for (const progression of progressions) {
      if (useMax) {
        this.emitNumber("0");
        this.emitStore(line, `${operation} current line`, sourceLine);
      }
      this.compileExpression(progression.startOffset);
      this.emitStore(offset, `${operation} offset`, sourceLine);
      this.emitNumberOrPreload(String(progression.count));
      this.emitStore(counter, `${operation} counter`, sourceLine);

      const start = this.freshLabel(`${operation}_loop`);
      this.emitLabel(start);
      this.compileExpression(addExpressions(cell, { kind: "identifier", name: offset }));
      const helper = this.ensureSpatialHitHelper(hitMask, spatialHitScratchName(hitMask));
      this.emitJump(0x53, "ПП", helper.label, `spatial hit ${hitMask}`, sourceLine);
      this.emitRecall(useMax ? line : total, `${operation} accumulator`);
      this.emitOp(0x10, "+", `${operation} add hit`);
      this.emitStore(useMax ? line : total, `${operation} accumulator`);

      this.emitRecall(offset, `${operation} offset`);
      this.compileExpression(progression.step);
      this.emitOp(0x10, "+", `${operation} next offset`);
      this.emitStore(offset, `${operation} offset`);

      const counterRegister = this.allocation.registers[counter];
      const flCounterOpcode = counterRegister === undefined ? undefined : flOpcode(counterRegister);
      if (flCounterOpcode !== undefined) {
        this.emitJump(flCounterOpcode, getOpcode(flCounterOpcode).name, start, `${operation} loop`, sourceLine);
        this.optimizations.push({
          name: "spatial-count-fl-loop",
          detail: `Used ${getOpcode(flCounterOpcode).name} for ${operation} loop counter.`,
        });
      } else {
        this.emitRecall(counter, `${operation} counter`);
        this.emitNumber("1");
        this.emitOp(0x11, "-", `${operation} decrement`);
        this.emitStore(counter, `${operation} counter`);
        this.emitRecall(counter, `${operation} counter`);
        this.emitJump(0x5e, "F x=0", start, `${operation} loop`, sourceLine);
      }

      if (useMax) {
        this.emitRecall(total, `${operation} total`);
        this.emitRecall(line, `${operation} current line`);
        this.emitOp(0x36, "К max", `${operation} best line`);
        this.emitStore(total, `${operation} total`);
      }
    }
    this.emitRecall(total, `${operation} result`);
    this.optimizations.push({
      name: `spatial-${operation.replace("_", "-")}-loop`,
      detail: `Lowered ${operation}(...) as shared spatial hit loops.`,
    });
  }

  private emitSpatialSumLoopHelperBody(
    hitMask: string,
    cell: ExpressionAst,
    operation: "line_count" | "neighbor_count",
    sourceLine: number | undefined,
  ): void {
    const scratch = spatialCountScratchNames();
    const total = scratch[0]!;
    const step = scratch[1]!;
    const offset = scratch[2]!;
    const counter = scratch[3]!;
    const counterRegister = this.allocation.registers[counter];
    const flCounterOpcode = counterRegister === undefined ? undefined : flOpcode(counterRegister);

    if (flCounterOpcode !== undefined) {
      this.emitStore(counter, `${operation} counter`, sourceLine);
    }

    const start = this.freshLabel(`${operation}_loop`);
    this.emitLabel(start);
    this.compileExpression(addExpressions(cell, { kind: "identifier", name: offset }));
    const hitScratch = spatialHitScratchName(hitMask);
    this.emitStore(hitScratch, "spatial hit index", sourceLine);

    this.emitRecall(offset, `${operation} offset`);
    this.emitRecall(step, `${operation} step`);
    this.emitOp(0x10, "+", `${operation} next offset`);
    this.emitStore(offset, `${operation} offset`);

    this.emitInlineSpatialHitFromScratch(hitMask, hitScratch, sourceLine);
    this.emitRecall(total, `${operation} accumulator`);
    this.emitOp(0x10, "+", `${operation} add hit`);
    this.emitStore(total, `${operation} accumulator`);

    if (flCounterOpcode !== undefined) {
      this.emitJump(flCounterOpcode, getOpcode(flCounterOpcode).name, start, `${operation} loop`, sourceLine);
      this.optimizations.push({
        name: "spatial-count-fl-loop",
        detail: `Used ${getOpcode(flCounterOpcode).name} for ${operation} loop counter.`,
      });
    } else {
      this.emitRecall(counter, `${operation} counter`);
      this.emitNumber("1");
      this.emitOp(0x11, "-", `${operation} decrement`);
      this.emitStore(counter, `${operation} counter`);
      this.emitRecall(counter, `${operation} counter`);
      this.emitJump(0x5e, "F x=0", start, `${operation} loop`, sourceLine);
    }
  }

  private emitInlineSpatialHit(hitMask: string, sourceLine: number | undefined): void {
    const scratch = spatialHitScratchName(hitMask);
    this.emitStore(scratch, "spatial hit index", sourceLine);
    this.emitInlineSpatialHitFromScratch(hitMask, scratch, sourceLine);
  }

  private emitInlineSpatialHitFromScratch(
    hitMask: string,
    scratch: string,
    sourceLine: number | undefined,
  ): void {
    const helper = this.ensureSpatialBitMaskHelper(scratch, sourceLine);
    this.emitRecall(scratch, "spatial hit index", sourceLine);
    this.emitJump(0x53, "ПП", helper.label, "bit_mask helper", sourceLine);
    this.emitRecall(hitMask, "spatial hit mask", sourceLine);
    this.emitOp(0x37, "К ∧", "spatial hit test", sourceLine);
    this.emitOp(0x32, "К ЗН", "spatial hit to count", sourceLine);
    this.optimizations.push({
      name: "spatial-hit-inline",
      detail: `Inlined spatial hit test for ${hitMask} into ${sourceLine === undefined ? "generated loop" : `line ${sourceLine}`}.`,
    });
  }

  private compileSpatialHitCall(expr: Extract<ExpressionAst, { kind: "call" }>): boolean {
    if (expr.args.length !== 2) {
      this.diagnostics.push({
        level: "error",
        message: "__spatial_hit() expects two arguments.",
      });
      return true;
    }
    const [mask, index] = expr.args;
    if (mask?.kind !== "identifier" || index === undefined) return false;
    const scratch = spatialHitScratchName(mask.name);
    if (!this.allocation.registers[scratch]) return false;
    const helper = this.ensureSpatialHitHelper(mask.name, scratch);
    this.compileExpression(index);
    this.emitJump(0x53, "ПП", helper.label, `spatial hit ${mask.name}`, helper.line);
    return true;
  }

  private compileNegativeZeroDegreeSelectorCall(expr: Extract<ExpressionAst, { kind: "call" }>): boolean {
    if (expr.args.length !== 2) {
      this.diagnostics.push({
        level: "error",
        message: `${NEGATIVE_ZERO_DEGREE_SELECTOR_GE}() expects two arguments.`,
      });
      return true;
    }
    const register = this.allocation.negativeZeroDegree;
    if (register === undefined) {
      this.diagnostics.push({
        level: "error",
        message: "Internal: negative-zero threshold selector was emitted without a reserved register.",
      });
      return true;
    }
    const [value, bound] = expr.args;
    if (value === undefined || bound === undefined) return true;

    this.emitNegativeZeroThresholdRaw(value, bound, register);
    this.emitOp(0x32, "К ЗН", "negative-zero threshold selector");
    this.optimizations.push({
      name: "negative-zero-threshold-selector",
      detail: `Used preloaded ${NEGATIVE_ZERO_DEGREE_PRELOAD_VALUE} in R${register} for ${expressionToIntentText(value)} >= ${expressionToIntentText(bound)}.`,
    });
    return true;
  }

  private sharedLineCountHelper(
    mask: ExpressionAst,
    cell: ExpressionAst,
    board: V2BoardAst,
    line: number | undefined,
  ): { cell: ExpressionAst; board: V2BoardAst; label: string; line?: number } | undefined {
    if (this.lineCountCallCount < 2) return undefined;
    if (this.allocation.registers[spatialCountMaskScratchName()] === undefined) return undefined;
    const key = lineCountGroupKeyFor(board, cell);
    if ((this.lineCountGroupCounts.get(key) ?? 0) < 2) return undefined;
    if (mask.kind !== "identifier") return undefined;
    const existing = this.lineCountHelpers.get(key);
    if (existing !== undefined) return existing;
    const helper = {
      cell,
      board,
      label: `__line_count_${this.lineCountHelpers.size}`,
      ...(line === undefined ? {} : { line }),
    };
    this.lineCountHelpers.set(key, helper);
    return helper;
  }

  private ensureSpatialHitHelper(mask: string, scratch: string): { mask: string; scratch: string; label: string; line?: number } {
    const existing = this.spatialHitHelpers.get(mask);
    if (existing !== undefined) return existing;
    const helper = {
      mask,
      scratch,
      label: `__spatial_hit_${mask}`,
    };
    this.spatialHitHelpers.set(mask, helper);
    return helper;
  }

  private ensureSpatialBitMaskHelper(
    scratch: string,
    line: number | undefined,
  ): { scratch: string; label: string; line?: number } {
    const existing = this.spatialBitMaskHelpers.get(scratch);
    if (existing !== undefined) return existing;
    const helper = {
      scratch,
      label: `__bit_mask_${this.spatialBitMaskHelpers.size}`,
      ...(line === undefined ? {} : { line }),
    };
    this.spatialBitMaskHelpers.set(scratch, helper);
    return helper;
  }

  private ensureSpatialSumLoopHelper(
    hitMask: string,
    cell: ExpressionAst,
    operation: "line_count" | "neighbor_count",
    line: number | undefined,
  ): { hitMask: string; cell: ExpressionAst; label: string; operation: "line_count" | "neighbor_count"; line?: number } {
    const key = `${operation}:${hitMask}:${expressionToIntentText(cell)}`;
    const existing = this.spatialSumLoopHelpers.get(key);
    if (existing !== undefined) return existing;
    const helper = {
      hitMask,
      cell,
      label: `__${operation}_progression_${this.spatialSumLoopHelpers.size}`,
      operation,
      ...(line === undefined ? {} : { line }),
    };
    this.spatialSumLoopHelpers.set(key, helper);
    return helper;
  }

  private compileRawStatement(statement: Extract<StatementAst, { kind: "core" }>): void {
    const inputs = statement.inputs ?? [];
    const outputs = statement.outputs ?? [];

    for (const input of orderRawInputs(inputs)) {
      this.compileExpression(input.expr);
    }

    this.compileRawLines(statement.lines, statement.strict ?? false);

    for (const output of outputs) {
      this.emitStore(output.target, `raw returns ${output.slot}`, output.line);
    }

    if (
      inputs.length > 0 ||
      outputs.length > 0 ||
      statement.clobbers !== undefined ||
      statement.preserves !== undefined
    ) {
      this.optimizations.push({
        name: "raw-block-contract",
        detail: formatRawContractDetail(statement),
      });
    }
  }

  private compileRawLines(
    lines: Array<{ text: string; line: number }>,
    strict = false,
  ): void {
    for (const line of lines) {
      if (line.text.endsWith(":")) {
        this.emitLabel(line.text.slice(0, -1));
        continue;
      }
      const parsed = parseRawInstruction(line.text);
      if (!parsed) {
        this.diagnostics.push({
          level: strict ? "error" : "warning",
          message: `Unknown raw instruction '${line.text}'`,
          line: line.line,
        });
        continue;
      }
      this.emitOp(parsed.opcode, parsed.mnemonic, parsed.comment, line.line, true);
      if (parsed.formalTargetOpcode !== undefined) {
        this.emitFormalAddress(parsed.formalTargetOpcode, parsed.comment ?? parsed.mnemonic, line.line);
      } else if (parsed.target !== undefined) {
        this.emitAddress(parsed.target, parsed.comment ?? parsed.mnemonic, line.line);
      }
    }
  }

  private emitNumber(raw: string): void {
    this.currentXVariable = undefined;
    this.currentXKnownZero = false;
    // If the machine is still in number-entry mode, a fresh literal would
    // concatenate onto the previous value (1 then 3 -> 13) or onto a just-read
    // input. Push the previous value with В↑ so the new number starts clean.
    if (this.machineEntryOpen) {
      this.emitOp(0x0e, "В↑", "separate adjacent number entry");
    }
    const normalized = raw.trim().toLowerCase();
    const negative = normalized.startsWith("-");
    const unsigned = negative ? normalized.slice(1) : normalized;
    const [mantissa, exponent] = unsigned.split("e");
    for (const char of mantissa ?? "0") {
      if (char === ".") this.emitOp(0x0a, ".");
      else if (/\d/u.test(char)) this.emitOp(Number(char), char);
    }
    if (exponent !== undefined) {
      this.emitOp(0x0c, "ВП", "exponent");
      const expNegative = exponent.startsWith("-");
      const expDigits =
        expNegative || exponent.startsWith("+") ? exponent.slice(1) : exponent;
      for (const char of expDigits) {
        if (/\d/u.test(char)) this.emitOp(Number(char), char);
      }
      if (expNegative) this.emitOp(0x0b, "/-/", "negative exponent");
    }
    if (negative) this.emitOp(0x0b, "/-/", "negative number");
    this.currentXKnownZero = Number(raw) === 0;
  }

  private emitZero(comment?: string, sourceLine?: number): void {
    if (this.currentXKnownZero) {
      this.optimizations.push({
        name: "known-zero-reuse",
        detail: `Reused known zero in X${comment === undefined ? "" : ` for ${comment}`}.`,
      });
      return;
    }
    this.emitNumber("0");
    if (comment !== undefined) {
      const last = this.items.at(-1);
      if (last?.kind === "op" && last.comment === undefined) last.comment = comment;
      if (last?.kind === "op" && sourceLine !== undefined && last.sourceLine === undefined) last.sourceLine = sourceLine;
    }
  }

  private emitNumberOrPreload(raw: string): void {
    const normalized = normalizeConstantLiteral(raw);
    const register = this.allocation.constants[normalized];
    if (register !== undefined) {
      this.emitOp(0x60 + registerIndex(register), `П->X ${register}`, `preload const ${normalized}`);
      this.optimizations.push({
        name: "preloaded-constant",
        detail: `Used preloaded R${register} for constant ${normalized}.`,
      });
      return;
    }
    this.emitNumber(raw);
  }

  private emitStore(name: string, comment?: string, sourceLine?: number): void {
    const register = this.allocation.registers[name];
    if (!register) {
      this.diagnostics.push(buildDiagnostic("error", `No register allocated for ${name}`, sourceLine));
      return;
    }
    this.emitOp(0x40 + registerIndex(register), `X->П ${register}`, comment, sourceLine);
    this.currentXVariable = name;
  }

  private emitRecall(name: string, comment?: string, sourceLine?: number): void {
    const register = this.allocation.registers[name];
    if (!register) {
      this.diagnostics.push(buildDiagnostic("error", `Unknown variable '${name}'`, sourceLine));
      return;
    }
    this.emitOp(0x60 + registerIndex(register), `П->X ${register}`, comment, sourceLine);
    this.currentXVariable = name;
  }

  private emitJump(
    opcode: number,
    mnemonic: string,
    target: string | number,
    comment?: string,
    sourceLine?: number,
  ): void {
    this.emitOp(opcode, mnemonic, comment, sourceLine);
    this.emitAddress(target, comment ?? mnemonic, sourceLine);
  }

  private emitAddress(
    target: string | number,
    comment?: string,
    sourceLine?: number,
  ): void {
    const item: MachineAddressRef = { kind: "address", target };
    if (comment !== undefined) item.comment = comment;
    if (sourceLine !== undefined) item.sourceLine = sourceLine;
    this.items.push(item);
  }

  private emitFormalAddress(
    opcode: number,
    comment?: string,
    sourceLine?: number,
  ): void {
    const info = formalAddressInfo(opcode);
    const item: MachineAddressRef = { kind: "address", target: info.ordinal, formalOpcode: opcode };
    if (comment !== undefined) item.comment = `${comment}; formal ${info.label}->${formatAddress(info.actual)}`;
    if (sourceLine !== undefined) item.sourceLine = sourceLine;
    this.items.push(item);
  }

  private emitOp(
    opcode: number,
    mnemonic?: string,
    comment?: string,
    sourceLine?: number,
    raw = false,
  ): void {
    const info = getOpcode(opcode);
    const op: MachineOp = {
      kind: "op",
      opcode,
      mnemonic: mnemonic ?? info.name,
    };
    if (comment !== undefined) op.comment = comment;
    if (sourceLine !== undefined) op.sourceLine = sourceLine;
    if (raw) op.raw = true;
    this.items.push(op);
    this.currentXVariable = undefined;
    this.currentXKnownZero = false;
    // Digit / '.' / sign / ВП opcodes (0x00..0x0c) keep the machine in
    // number-entry mode; every other op finalizes it.
    this.machineEntryOpen = opcode <= 0x0c;
  }

  private emitLabel(name: string): void {
    this.items.push({ kind: "label", name });
  }

  private freshLabel(prefix: string): string {
    const label = `__${prefix}_${this.labelCounter}`;
    this.labelCounter += 1;
    return label;
  }
}

interface RegisterAllocation {
  registers: Record<string, RegisterName>;
  constants: Record<string, RegisterName>;
  negativeZeroDegree?: RegisterName;
}

function findSingleUseProcNames(ast: ProgramAst): Set<string> {
  const procNames = new Set(ast.procs.map((proc) => proc.name));
  const counts = new Map<string, number>();
  const recursive = new Set<string>();

  const visit = (statements: StatementAst[], currentProc?: string): void => {
    for (const statement of statements) {
      if (statement.kind === "call" && procNames.has(statement.block)) {
        counts.set(statement.block, (counts.get(statement.block) ?? 0) + 1);
        if (statement.block === currentProc) recursive.add(statement.block);
      }
      if (statement.kind === "loop") visit(statement.body, currentProc);
      if (statement.kind === "if") {
        visit(statement.thenBody, currentProc);
        if (statement.elseBody) visit(statement.elseBody, currentProc);
      }
      if (statement.kind === "switch") {
        for (const switchCase of statement.cases) visit(switchCase.body, currentProc);
        if (statement.defaultBody) visit(statement.defaultBody, currentProc);
      }
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body, currentProc);
        if (statement.defaultBody) visit(statement.defaultBody, currentProc);
      }
    }
  };

  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body, proc.name);
  for (const block of ast.blocks) visit(block.body);

  return new Set(
    [...counts.entries()]
      .filter(([name, count]) => count === 1 && !recursive.has(name))
      .map(([name]) => name),
  );
}

interface DomainBinding {
  name: string;
  storage?: StorageHint;
}

function allocateRegisters(
  ast: ProgramAst,
  diagnostics: Diagnostic[],
): RegisterAllocation {
  const declared = new Set<string>();
  const hints = new Map<string, { mode: "prefer" | "fixed"; register: RegisterName }>();
  const variables = new Set<string>();

  for (const declaration of ast.declarations) {
    if (declaration.kind === "const") continue;
    declared.add(declaration.name);
    variables.add(declaration.name);
    if (declaration.storage) hints.set(declaration.name, declaration.storage);
  }
  for (const state of ast.states) {
    for (const field of state.fields) {
      declared.add(field.name);
      variables.add(field.name);
    }
  }
  for (const binding of collectDomainBindings(ast)) {
    declared.add(binding.name);
    variables.add(binding.name);
    if (binding.storage) hints.set(binding.name, binding.storage);
  }
  applyTicTacToeRegisterHints(ast, variables, hints);
  const flPreferenceOrder: RegisterName[] = ["2", "3", "1", "0"];
  let flPreferenceIndex = 0;
  for (const variable of collectUnitDecrementTargets(ast)) {
    if (!variables.has(variable) || hints.has(variable)) continue;
    const register = flPreferenceOrder[flPreferenceIndex];
    if (register === undefined) break;
    hints.set(variable, { mode: "prefer", register });
    flPreferenceIndex += 1;
  }

  warnUndeclaredAssignments(ast, declared, diagnostics);
  collectAssignedVariables(ast, variables);
  collectSwitchScratchVariables(ast, variables);
  collectDispatchScratchVariables(ast, variables);
  collectTicTacToeScratchVariables(ast, variables);
  collectBitMaskScratchVariables(ast, variables);
  collectSpatialHitScratchVariables(ast, variables);
  collectSpatialCountScratchVariables(ast, variables);
  collectGuardedUpdateScratchVariables(ast, variables);

  const registers: Record<string, RegisterName> = {};
  const used = new Set<RegisterName>();

  for (const variable of variables) {
    const hint = hints.get(variable);
    if (hint?.mode === "fixed") {
      if (used.has(hint.register)) {
        diagnostics.push({
          level: "error",
          message: `Register R${hint.register} is fixed for more than one variable.`,
        });
      }
      registers[variable] = hint.register;
      used.add(hint.register);
    }
  }

  const ordered = [...variables].sort(
    (a, b) => priority(a, hints) - priority(b, hints) || a.localeCompare(b),
  );
  for (const variable of ordered) {
    if (registers[variable]) continue;
    const hint = hints.get(variable);
    if (hint?.mode === "prefer" && !used.has(hint.register)) {
      registers[variable] = hint.register;
      used.add(hint.register);
      continue;
    }
    const candidate = pickRegister(variable, used);
    if (!candidate) {
      diagnostics.push({
        level: "error",
        message: `Out of MK-61 registers while allocating '${variable}'.`,
      });
      continue;
    }
    registers[variable] = candidate;
    used.add(candidate);
  }

  const constants: Record<string, RegisterName> = {};
  for (const value of collectPreloadConstantValues(ast)) {
    const register = pickConstantRegister(used);
    if (!register) break;
    constants[value] = register;
    used.add(register);
  }

  const negativeZeroDegree = programNeedsNegativeZeroDegree(ast)
    ? pickConstantRegister(used)
    : undefined;
  if (negativeZeroDegree !== undefined) used.add(negativeZeroDegree);

  return negativeZeroDegree === undefined
    ? { registers, constants }
    : { registers, constants, negativeZeroDegree };
}

function collectDomainBindings(ast: ProgramAst): DomainBinding[] {
  const bindings: DomainBinding[] = [];
  for (const domain of ast.domains) {
    const name = domainBindingName(domain);
    if (!name) continue;
    const binding: DomainBinding = { name };
    const register = domainRegisterHint(domain);
    if (register !== undefined) {
      binding.storage = { mode: "fixed", register };
    }
    bindings.push(binding);
  }
  return bindings;
}

function domainBindingName(domain: ProgramAst["domains"][number]): string | undefined {
  const header = domain.header.trim();
  if (domain.domainKind === "coord") {
    const match = /^coord\s+(?:packed\s+)?([A-Za-z_][\w]*)/u.exec(header);
    return match?.[1];
  }
  if (domain.domainKind === "resource" || domain.domainKind === "bitset") {
    return domain.name;
  }
  return undefined;
}

function domainRegisterHint(domain: ProgramAst["domains"][number]): RegisterName | undefined {
  for (const line of domain.lines) {
    const match = /^register\s+R?([0-9a-eавсде])$/iu.exec(line.text);
    if (match) return registerFromText(match[1]!);
  }
  return undefined;
}

function applyTicTacToeRegisterHints(
  ast: ProgramAst,
  variables: Set<string>,
  hints: Map<string, { mode: "prefer" | "fixed"; register: RegisterName }>,
): void {
  if (!programUsesTicTacToeHelpers(ast)) return;
  const preferences: Array<[string, RegisterName]> = [
    ["x", "1"],
    ["y", "2"],
    ["mask", "9"],
    ["lines", "4"],
  ];
  for (const [name, register] of preferences) {
    if (variables.has(name) && !hints.has(name)) {
      hints.set(name, { mode: "prefer", register });
    }
  }
}

function pickRegister(
  variable: string,
  used: Set<RegisterName>,
): RegisterName | undefined {
  if (variable.startsWith(SWITCH_SCRATCH_PREFIX)) {
    for (let i = REGISTER_ORDER.length - 1; i >= 0; i -= 1) {
      const candidate = REGISTER_ORDER[i]!;
      if (!used.has(candidate)) return candidate;
    }
    return undefined;
  }
  if (variable.startsWith(DISPATCH_SCRATCH_PREFIX)) {
    for (let i = REGISTER_ORDER.length - 1; i >= 0; i -= 1) {
      const candidate = REGISTER_ORDER[i]!;
      if (!used.has(candidate)) return candidate;
    }
    return undefined;
  }
  if (variable.startsWith(TICTACTOE_MASK_SCRATCH_PREFIX)) {
    for (let i = REGISTER_ORDER.length - 1; i >= 0; i -= 1) {
      const candidate = REGISTER_ORDER[i]!;
      if (!used.has(candidate)) return candidate;
    }
    return undefined;
  }
  if (variable.startsWith(BIT_MASK_SCRATCH_PREFIX)) {
    for (let i = REGISTER_ORDER.length - 1; i >= 0; i -= 1) {
      const candidate = REGISTER_ORDER[i]!;
      if (!used.has(candidate)) return candidate;
    }
    return undefined;
  }
  if (variable.startsWith(IF_SELECTOR_SCRATCH_PREFIX)) {
    for (let i = REGISTER_ORDER.length - 1; i >= 0; i -= 1) {
      const candidate = REGISTER_ORDER[i]!;
      if (!used.has(candidate)) return candidate;
    }
    return undefined;
  }
  if (variable.startsWith(SPATIAL_COUNT_SCRATCH_PREFIX)) {
    if (variable === spatialCountCounterScratchName()) {
      for (const candidate of ["0", "1", "2", "3"] satisfies RegisterName[]) {
        if (!used.has(candidate)) return candidate;
      }
    }
    for (let i = REGISTER_ORDER.length - 1; i >= 0; i -= 1) {
      const candidate = REGISTER_ORDER[i]!;
      if (!used.has(candidate)) return candidate;
    }
    return undefined;
  }
  return REGISTER_ORDER.find((candidate) => !used.has(candidate));
}

function pickConstantRegister(used: Set<RegisterName>): RegisterName | undefined {
  for (let i = REGISTER_ORDER.length - 1; i >= 0; i -= 1) {
    const candidate = REGISTER_ORDER[i]!;
    if (!used.has(candidate)) return candidate;
  }
  return undefined;
}

function collectPreloadConstantValues(ast: ProgramAst): string[] {
  const values = new Set<string>();
  if (programContainsCall(ast, "direction")) {
    values.add("20");
    values.add("10");
  }
  if (programContainsCall(ast, "line_count")) {
    values.add("10");
    values.add("11");
    values.add("19");
    values.add("-99");
    values.add("-81");
  }
  const visitExpr = (expr: ExpressionAst): void => {
    if (expr.kind === "number" && estimateNumberCost(expr.raw) > 1) values.add(normalizeConstantLiteral(expr.raw));
    if (expr.kind === "unary") {
      if (expr.op === "-" && expr.expr.kind === "number") {
        values.add(normalizeConstantLiteral(negatedNumberLiteral(expr.expr.raw)));
        return;
      }
      visitExpr(expr.expr);
    }
    if (expr.kind === "binary") {
      visitExpr(expr.left);
      visitExpr(expr.right);
    }
    if (expr.kind === "call") {
      const macro = ticTacToeExpressionMacro(expr.callee.toLowerCase(), expr.args);
      if (macro !== undefined) {
        visitExpr(macro);
        return;
      }
      for (const arg of expr.args) visitExpr(arg);
    }
  };
  const visitStatements = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "pause" || statement.kind === "halt") visitExpr(statement.expr);
      if (statement.kind === "ask" && statement.prompt) visitExpr(statement.prompt);
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitExpr(statement.condition.left);
        visitExpr(statement.condition.right);
        visitStatements(statement.thenBody);
        if (statement.elseBody) visitStatements(statement.elseBody);
      }
      if (statement.kind === "loop") visitStatements(statement.body);
      if (statement.kind === "switch") {
        visitExpr(statement.expr);
        for (const switchCase of statement.cases) {
          visitExpr(switchCase.value);
          visitStatements(switchCase.body);
        }
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        visitExpr(statement.expr);
        for (const dispatchCase of statement.cases) {
          visitExpr(dispatchCase.value);
          visitStatements(dispatchCase.body);
        }
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visitStatements(entry.body);
  for (const proc of ast.procs) visitStatements(proc.body);
  for (const block of ast.blocks) visitStatements(block.body);
  return [...values].filter((value) => value !== "0" && value !== "1").sort((a, b) => estimateNumberCost(b) - estimateNumberCost(a));
}

function programContainsCall(ast: ProgramAst, name: string): boolean {
  const target = name.toLowerCase();
  let found = false;
  const visitExpr = (expr: ExpressionAst): void => {
    if (found) return;
    if (expr.kind === "call" && expr.callee.toLowerCase() === target) {
      found = true;
      return;
    }
    if (expr.kind === "unary") visitExpr(expr.expr);
    if (expr.kind === "binary") {
      visitExpr(expr.left);
      visitExpr(expr.right);
    }
    if (expr.kind === "call") {
      for (const arg of expr.args) visitExpr(arg);
    }
  };
  const visitStatements = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (found) return;
      if (statement.kind === "pause" || statement.kind === "halt") visitExpr(statement.expr);
      if (statement.kind === "ask" && statement.prompt) visitExpr(statement.prompt);
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitExpr(statement.condition.left);
        visitExpr(statement.condition.right);
        visitStatements(statement.thenBody);
        if (statement.elseBody) visitStatements(statement.elseBody);
      }
      if (statement.kind === "loop") visitStatements(statement.body);
      if (statement.kind === "switch") {
        visitExpr(statement.expr);
        for (const switchCase of statement.cases) {
          visitExpr(switchCase.value);
          visitStatements(switchCase.body);
        }
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        visitExpr(statement.expr);
        for (const dispatchCase of statement.cases) {
          visitExpr(dispatchCase.value);
          visitStatements(dispatchCase.body);
        }
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visitStatements(entry.body);
  for (const proc of ast.procs) visitStatements(proc.body);
  for (const block of ast.blocks) visitStatements(block.body);
  return found;
}

function programNeedsNegativeZeroDegree(ast: ProgramAst): boolean {
  return programVisitsIf(ast, (statement) => statementNeedsNegativeZeroDegree(statement, ast));
}

function statementNeedsNegativeZeroDegree(
  statement: Extract<StatementAst, { kind: "if" }>,
  ast: ProgramAst,
): boolean {
  const selected = buildBranchRemovalCandidate(statement, ast, { negativeZeroDegree: true });
  if (selected !== undefined && selected.name.startsWith("negative-zero-threshold-")) {
    return estimateExpressionCost(selected.expr) + 1 < estimateOrdinaryIfCost(statement, ast);
  }
  const guarded = buildGuardedUpdateSelectorCandidate(statement, ast, { negativeZeroDegree: true });
  if (guarded?.usesNegativeZero === true) {
    return estimateGuardedUpdateSelectorCost(guarded, ifSelectorScratchName(statement)) <
      estimateOrdinaryGuardedUpdateCost(statement, ast);
  }
  const threshold = matchNegativeZeroThresholdCondition(statement.condition, ast);
  if (threshold === undefined) return false;
  return estimateNegativeZeroThresholdFlowCost(threshold, undefined) < estimateConditionCost(statement.condition, ast);
}

function programVisitsIf(
  ast: ProgramAst,
  predicate: (statement: Extract<StatementAst, { kind: "if" }>) => boolean,
): boolean {
  let found = false;
  const visitStatements = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (found) return;
      if (statement.kind === "if") {
        if (predicate(statement)) {
          found = true;
          return;
        }
        visitStatements(statement.thenBody);
        if (statement.elseBody) visitStatements(statement.elseBody);
      }
      if (statement.kind === "loop") visitStatements(statement.body);
      if (statement.kind === "switch") {
        for (const switchCase of statement.cases) visitStatements(switchCase.body);
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visitStatements(dispatchCase.body);
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visitStatements(entry.body);
  for (const proc of ast.procs) visitStatements(proc.body);
  for (const block of ast.blocks) visitStatements(block.body);
  return found;
}

function countCalls(ast: ProgramAst, name: string): number {
  const target = name.toLowerCase();
  let count = 0;
  const visitExpr = (expr: ExpressionAst): void => {
    if (expr.kind === "call" && expr.callee.toLowerCase() === target) count += 1;
    if (expr.kind === "unary") visitExpr(expr.expr);
    if (expr.kind === "binary") {
      visitExpr(expr.left);
      visitExpr(expr.right);
    }
    if (expr.kind === "call") {
      for (const arg of expr.args) visitExpr(arg);
    }
  };
  const visitStatements = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "pause" || statement.kind === "halt") visitExpr(statement.expr);
      if (statement.kind === "ask" && statement.prompt) visitExpr(statement.prompt);
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitExpr(statement.condition.left);
        visitExpr(statement.condition.right);
        visitStatements(statement.thenBody);
        if (statement.elseBody) visitStatements(statement.elseBody);
      }
      if (statement.kind === "loop") visitStatements(statement.body);
      if (statement.kind === "switch") {
        visitExpr(statement.expr);
        for (const switchCase of statement.cases) {
          visitExpr(switchCase.value);
          visitStatements(switchCase.body);
        }
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        visitExpr(statement.expr);
        for (const dispatchCase of statement.cases) {
          visitExpr(dispatchCase.value);
          visitStatements(dispatchCase.body);
        }
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visitStatements(entry.body);
  for (const proc of ast.procs) visitStatements(proc.body);
  for (const block of ast.blocks) visitStatements(block.body);
  return count;
}

function collectLineCountGroupCounts(ast: ProgramAst): Map<string, number> {
  const counts = new Map<string, number>();
  const visitExpr = (expr: ExpressionAst): void => {
    if (expr.kind === "call" && expr.callee.toLowerCase() === "line_count" && expr.args.length === 2) {
      const [mask, cell] = expr.args;
      if (mask !== undefined && cell !== undefined) {
        const board = boardForCellMask(mask, ast);
        if (board !== undefined) {
          const key = lineCountGroupKeyFor(board, cell);
          counts.set(key, (counts.get(key) ?? 0) + 1);
        }
      }
    }
    if (expr.kind === "unary") visitExpr(expr.expr);
    if (expr.kind === "binary") {
      visitExpr(expr.left);
      visitExpr(expr.right);
    }
    if (expr.kind === "call") {
      for (const arg of expr.args) visitExpr(arg);
    }
  };
  const visitStatements = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "pause" || statement.kind === "halt") visitExpr(statement.expr);
      if (statement.kind === "ask" && statement.prompt) visitExpr(statement.prompt);
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitExpr(statement.condition.left);
        visitExpr(statement.condition.right);
        visitStatements(statement.thenBody);
        if (statement.elseBody) visitStatements(statement.elseBody);
      }
      if (statement.kind === "loop") visitStatements(statement.body);
      if (statement.kind === "switch") {
        visitExpr(statement.expr);
        for (const switchCase of statement.cases) {
          visitExpr(switchCase.value);
          visitStatements(switchCase.body);
        }
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        visitExpr(statement.expr);
        for (const dispatchCase of statement.cases) {
          visitExpr(dispatchCase.value);
          visitStatements(dispatchCase.body);
        }
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visitStatements(entry.body);
  for (const proc of ast.procs) visitStatements(proc.body);
  for (const block of ast.blocks) visitStatements(block.body);
  return counts;
}

function collectVariableReadCounts(ast: ProgramAst): Map<string, number> {
  const counts = new Map<string, number>();
  const add = (name: string): void => {
    counts.set(name, (counts.get(name) ?? 0) + 1);
  };
  const visitExpr = (expr: ExpressionAst): void => {
    if (expr.kind === "identifier") add(expr.name);
    if (expr.kind === "unary") visitExpr(expr.expr);
    if (expr.kind === "binary") {
      visitExpr(expr.left);
      visitExpr(expr.right);
    }
    if (expr.kind === "call") {
      for (const arg of expr.args) visitExpr(arg);
    }
  };
  const visitCondition = (condition: ConditionAst): void => {
    visitExpr(condition.left);
    visitExpr(condition.right);
  };
  const visitStatements = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "pause" || statement.kind === "halt") visitExpr(statement.expr);
      if (statement.kind === "ask" && statement.prompt) visitExpr(statement.prompt);
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "show") {
        const display = ast.displays.find((candidate) => candidate.name === statement.display);
        for (const source of display?.sources ?? []) add(source);
      }
      if (statement.kind === "if") {
        visitCondition(statement.condition);
        visitStatements(statement.thenBody);
        if (statement.elseBody) visitStatements(statement.elseBody);
      }
      if (statement.kind === "loop") visitStatements(statement.body);
      if (statement.kind === "switch") {
        visitExpr(statement.expr);
        for (const switchCase of statement.cases) {
          visitExpr(switchCase.value);
          visitStatements(switchCase.body);
        }
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        visitExpr(statement.expr);
        for (const dispatchCase of statement.cases) {
          visitExpr(dispatchCase.value);
          visitStatements(dispatchCase.body);
        }
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visitStatements(entry.body);
  for (const proc of ast.procs) visitStatements(proc.body);
  for (const block of ast.blocks) visitStatements(block.body);
  return counts;
}

function collectDisplayUseCounts(ast: ProgramAst): Map<string, number> {
  const counts = new Map<string, number>();
  const add = (name: string): void => {
    counts.set(name, (counts.get(name) ?? 0) + 1);
  };
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "show") add(statement.display);
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "if") {
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "switch") {
        for (const switchCase of statement.cases) visit(switchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
  for (const block of ast.blocks) visit(block.body);
  return counts;
}

function collectShowSequenceUseCounts(ast: ProgramAst): Map<string, number> {
  const counts = new Map<string, number>();
  const add = (first: string, second: string): void => {
    const key = showSequenceKey(first, second);
    counts.set(key, (counts.get(key) ?? 0) + 1);
  };
  const visit = (statements: StatementAst[]): void => {
    for (let index = 0; index < statements.length; index += 1) {
      const statement = statements[index]!;
      const next = statements[index + 1];
      const afterNext = statements[index + 2];
      if (statement.kind === "show" && next?.kind === "show" && afterNext?.kind === "input") {
        add(statement.display, next.display);
      }
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "if") {
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "switch") {
        for (const switchCase of statement.cases) visit(switchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
  for (const block of ast.blocks) visit(block.body);
  return counts;
}

function showSequenceKey(first: string, second: string): string {
  return `${first}\0${second}`;
}

function collectExpressionUseCounts(ast: ProgramAst): Map<string, { count: number; expr: ExpressionAst }> {
  const counts = new Map<string, { count: number; expr: ExpressionAst }>();
  const add = (expr: ExpressionAst): void => {
    const key = expressionToIntentText(expr);
    const existing = counts.get(key);
    if (existing === undefined) {
      counts.set(key, { count: 1, expr });
    } else {
      existing.count += 1;
    }
  };
  const visitExpr = (expr: ExpressionAst): void => {
    add(expr);
    if (expr.kind === "unary") visitExpr(expr.expr);
    if (expr.kind === "binary") {
      visitExpr(expr.left);
      visitExpr(expr.right);
    }
    if (expr.kind === "call") {
      for (const arg of expr.args) visitExpr(arg);
    }
  };
  const visitCondition = (condition: ConditionAst): void => {
    visitExpr(condition.left);
    visitExpr(condition.right);
  };
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "pause" || statement.kind === "halt") visitExpr(statement.expr);
      if (statement.kind === "ask" && statement.prompt) visitExpr(statement.prompt);
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitCondition(statement.condition);
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "switch") {
        visitExpr(statement.expr);
        for (const switchCase of statement.cases) {
          visitExpr(switchCase.value);
          visit(switchCase.body);
        }
        if (statement.defaultBody) visit(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        visitExpr(statement.expr);
        for (const dispatchCase of statement.cases) {
          visitExpr(dispatchCase.value);
          visit(dispatchCase.body);
        }
        if (statement.defaultBody) visit(statement.defaultBody);
      }
      if (statement.kind === "core") {
        for (const input of statement.inputs ?? []) visitExpr(input.expr);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
  for (const block of ast.blocks) visit(block.body);
  return counts;
}

function estimatePackedDisplayBodyCost(sourceCount: number): number {
  return sourceCount === 0 ? 2 : sourceCount * 2;
}

function programUsesTicTacToeHelpers(ast: ProgramAst): boolean {
  let found = false;
  const visitExpr = (expr: ExpressionAst): void => {
    if (found) return;
    if (expr.kind === "call" && isTicTacToeMacroName(expr.callee.toLowerCase())) {
      found = true;
      return;
    }
    if (expr.kind === "unary") visitExpr(expr.expr);
    if (expr.kind === "binary") {
      visitExpr(expr.left);
      visitExpr(expr.right);
    }
    if (expr.kind === "call") {
      for (const arg of expr.args) visitExpr(arg);
    }
  };
  const visitStatements = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (found) return;
      if (statement.kind === "pause" || statement.kind === "halt") visitExpr(statement.expr);
      if (statement.kind === "ask" && statement.prompt) visitExpr(statement.prompt);
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitExpr(statement.condition.left);
        visitExpr(statement.condition.right);
        visitStatements(statement.thenBody);
        if (statement.elseBody) visitStatements(statement.elseBody);
      }
      if (statement.kind === "loop") visitStatements(statement.body);
      if (statement.kind === "switch") {
        visitExpr(statement.expr);
        for (const switchCase of statement.cases) visitStatements(switchCase.body);
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        visitExpr(statement.expr);
        for (const dispatchCase of statement.cases) visitStatements(dispatchCase.body);
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visitStatements(entry.body);
  for (const proc of ast.procs) visitStatements(proc.body);
  for (const block of ast.blocks) visitStatements(block.body);
  return found;
}

function normalizeConstantLiteral(raw: string): string {
  const value = Number(raw);
  return Number.isFinite(value) ? String(value) : raw.trim().toLowerCase();
}

function negatedNumberLiteral(raw: string): string {
  const normalized = raw.trim();
  return normalized.startsWith("-") ? normalized.slice(1) : `-${normalized}`;
}

function warnUndeclaredAssignments(
  ast: ProgramAst,
  declared: Set<string>,
  diagnostics: Diagnostic[],
): void {
  const seen = new Set<string>();
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "assign" || statement.kind === "ask" || statement.kind === "input") {
        if (!declared.has(statement.target) && !seen.has(statement.target)) {
          diagnostics.push({
            level: "warning",
            message: `Implicit allocation for undeclared variable '${statement.target}'. Add 'store ${statement.target}' to silence.`,
            line: statement.line,
          });
          seen.add(statement.target);
        }
      }
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "if") {
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "switch") {
        for (const switchCase of statement.cases) visit(switchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
  for (const block of ast.blocks) visit(block.body);
}

function collectAssignedVariables(ast: ProgramAst, variables: Set<string>): void {
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "assign" || statement.kind === "ask" || statement.kind === "input") {
        variables.add(statement.target);
      }
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "if") {
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "switch") {
        for (const switchCase of statement.cases) visit(switchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
  for (const block of ast.blocks) visit(block.body);
}

function collectUnitDecrementTargets(ast: ProgramAst): Set<string> {
  const targets = new Set<string>();
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "assign" && isUnitDecrementExpression(statement.target, statement.expr)) {
        targets.add(statement.target);
      }
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "if") {
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "switch") {
        for (const switchCase of statement.cases) visit(switchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
  for (const block of ast.blocks) visit(block.body);
  return targets;
}

function collectSwitchScratchVariables(ast: ProgramAst, variables: Set<string>): void {
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "switch") {
        variables.add(`${SWITCH_SCRATCH_PREFIX}${statement.scratchId}`);
        for (const switchCase of statement.cases) visit(switchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "if") {
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
  for (const block of ast.blocks) visit(block.body);
}

function collectDispatchScratchVariables(ast: ProgramAst, variables: Set<string>): void {
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "dispatch") {
        if (statement.expr.kind !== "identifier") {
          variables.add(`${DISPATCH_SCRATCH_PREFIX}${statement.scratchId}`);
        }
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "if") {
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "switch") {
        for (const switchCase of statement.cases) visit(switchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
  for (const block of ast.blocks) visit(block.body);
}

function collectTicTacToeScratchVariables(ast: ProgramAst, variables: Set<string>): void {
  const visit = (statements: StatementAst[]): void => {
    for (let index = 0; index < statements.length; index += 1) {
      const statement = statements[index]!;
      const next = statements[index + 1];
      if (statement.kind === "assign" && next?.kind === "assign" && isReusableCellMaskPair(statement, next)) {
        variables.add(ticTacToeMaskScratchName(statement));
      }
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "if") {
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "switch") {
        for (const switchCase of statement.cases) visit(switchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
  for (const block of ast.blocks) visit(block.body);
}

function collectBitMaskScratchVariables(ast: ProgramAst, variables: Set<string>): void {
  const visit = (statements: StatementAst[]): void => {
    for (let index = 0; index < statements.length; index += 1) {
      const statement = statements[index]!;
      const next = statements[index + 1];
      if (statement.kind === "assign" && next?.kind === "assign" && isReusableBitSetPair(statement, next)) {
        variables.add(bitMaskScratchName(statement));
      }
      if (statement.kind === "if" && isReusableMembershipSet(statement)) {
        variables.add(bitMaskScratchName(statement));
      }
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "if") {
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "switch") {
        for (const switchCase of statement.cases) visit(switchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
  for (const block of ast.blocks) visit(block.body);
}

function collectSpatialHitScratchVariables(ast: ProgramAst, variables: Set<string>): void {
  const shareLineCountMask = countCalls(ast, "line_count") > 1;
  const visitExpr = (expr: ExpressionAst): void => {
    if (shareLineCountMask && expr.kind === "call" && expr.callee.toLowerCase() === "line_count") {
      variables.add(spatialHitScratchName(spatialCountMaskScratchName()));
      for (const arg of expr.args) visitExpr(arg);
      return;
    }
    if (
      expr.kind === "call" &&
      (expr.callee.toLowerCase() === "neighbor_count" || expr.callee.toLowerCase() === "line_count") &&
      expr.args[0]?.kind === "identifier"
    ) {
      variables.add(spatialHitScratchName(expr.args[0].name));
    }
    if (expr.kind === "unary") visitExpr(expr.expr);
    if (expr.kind === "binary") {
      visitExpr(expr.left);
      visitExpr(expr.right);
    }
    if (expr.kind === "call") {
      for (const arg of expr.args) visitExpr(arg);
    }
  };
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "pause" || statement.kind === "halt") visitExpr(statement.expr);
      if (statement.kind === "ask" && statement.prompt) visitExpr(statement.prompt);
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitExpr(statement.condition.left);
        visitExpr(statement.condition.right);
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "switch") {
        visitExpr(statement.expr);
        for (const switchCase of statement.cases) {
          visitExpr(switchCase.value);
          visit(switchCase.body);
        }
        if (statement.defaultBody) visit(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        visitExpr(statement.expr);
        for (const dispatchCase of statement.cases) {
          visitExpr(dispatchCase.value);
          visit(dispatchCase.body);
        }
        if (statement.defaultBody) visit(statement.defaultBody);
      }
      if (statement.kind === "core") {
        for (const input of statement.inputs ?? []) visitExpr(input.expr);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
  for (const block of ast.blocks) visit(block.body);
}

function collectSpatialCountScratchVariables(ast: ProgramAst, variables: Set<string>): void {
  let needsScratch = false;
  const visitExpr = (expr: ExpressionAst): void => {
    if (expr.kind === "call" && expr.callee.toLowerCase() === "line_count") {
      needsScratch = true;
    }
    if (expr.kind === "unary") visitExpr(expr.expr);
    if (expr.kind === "binary") {
      visitExpr(expr.left);
      visitExpr(expr.right);
    }
    if (expr.kind === "call") {
      for (const arg of expr.args) visitExpr(arg);
    }
  };
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "pause" || statement.kind === "halt") visitExpr(statement.expr);
      if (statement.kind === "ask" && statement.prompt) visitExpr(statement.prompt);
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitExpr(statement.condition.left);
        visitExpr(statement.condition.right);
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "switch") {
        visitExpr(statement.expr);
        for (const switchCase of statement.cases) {
          visitExpr(switchCase.value);
          visit(switchCase.body);
        }
        if (statement.defaultBody) visit(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        visitExpr(statement.expr);
        for (const dispatchCase of statement.cases) {
          visitExpr(dispatchCase.value);
          visit(dispatchCase.body);
        }
        if (statement.defaultBody) visit(statement.defaultBody);
      }
      if (statement.kind === "core") {
        for (const input of statement.inputs ?? []) visitExpr(input.expr);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
  for (const block of ast.blocks) visit(block.body);
  if (!needsScratch) return;
  for (const scratch of spatialCountScratchNames()) variables.add(scratch);
  if (countCalls(ast, "line_count") > 1) variables.add(spatialCountMaskScratchName());
}

function collectGuardedUpdateScratchVariables(ast: ProgramAst, variables: Set<string>): void {
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "if") {
        if (guardedUpdateSelectorProfitable(statement, ast, { negativeZeroDegree: true })) {
          variables.add(ifSelectorScratchName(statement));
        }
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "switch") {
        for (const switchCase of statement.cases) visit(switchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
  for (const block of ast.blocks) visit(block.body);
}

function guardedUpdateSelectorProfitable(
  statement: Extract<StatementAst, { kind: "if" }>,
  ast: ProgramAst,
  options: { negativeZeroDegree?: boolean } = {},
): boolean {
  const candidate = buildGuardedUpdateSelectorCandidate(statement, ast, options);
  if (candidate === undefined) return false;
  return estimateGuardedUpdateSelectorCost(candidate, ifSelectorScratchName(statement)) <
    estimateOrdinaryGuardedUpdateCost(statement, ast);
}

function isReusableCellMaskPair(
  first: Extract<StatementAst, { kind: "assign" }>,
  second: Extract<StatementAst, { kind: "assign" }>,
): boolean {
  const used = matchCellHelperCall(first.expr, ["cell_used", "cell_has"]);
  const mark = matchCellHelperCall(second.expr, ["cell_mark", "cell_set"]);
  return Boolean(
    used &&
    mark &&
    used.mask.kind === "identifier" &&
    second.target === used.mask.name &&
    expressionEquals(used.mask, mark.mask) &&
    expressionEquals(used.x, mark.x) &&
    expressionEquals(used.y, mark.y),
  );
}

function ticTacToeMaskScratchName(statement: StatementAst): string {
  return `${TICTACTOE_MASK_SCRATCH_PREFIX}${statement.line}`;
}

function dispatchExpressionRegister(
  statement: Extract<StatementAst, { kind: "dispatch" }>,
  allocation: RegisterAllocation,
): RegisterName | undefined {
  if (statement.expr.kind !== "identifier") return undefined;
  return allocation.registers[statement.expr.name];
}

function numericResidualDispatchIsCheaper(
  statement: Extract<StatementAst, { kind: "dispatch" }>,
  values: readonly number[],
): boolean {
  const sourceRegister = statement.expr.kind === "identifier";
  let ordinary = estimateExpressionCost(statement.expr) + (sourceRegister ? 0 : 1);
  for (let index = 0; index < values.length; index += 1) {
    const value = values[index]!;
    ordinary += (index > 0 ? 1 : 0) + (value === 0 ? 0 : estimateNumberCost(String(value)) + 1) + 2;
  }

  let residual = estimateExpressionCost(statement.expr);
  let comparedValue = 0;
  let hasComparedValue = false;
  for (const value of values) {
    if (!hasComparedValue) {
      residual += value === 0 ? 0 : estimateNumberCost(String(value)) + 1;
      hasComparedValue = true;
    } else {
      const delta = comparedValue - value;
      residual += delta === 0 ? 0 : estimateNumberCost(String(delta)) + 1;
    }
    residual += 2;
    comparedValue = value;
  }
  return residual < ordinary;
}

function isZeroExpression(expr: ExpressionAst): boolean {
  return expr.kind === "number" && Number(expr.raw) === 0;
}

function isUnitDecrementExpression(target: string, expr: ExpressionAst): boolean {
  return expr.kind === "binary" &&
    expr.op === "-" &&
    expr.left.kind === "identifier" &&
    expr.left.name === target &&
    expr.right.kind === "number" &&
    Number(expr.right.raw) === 1;
}

function decrementBranchTestsZero(condition: ConditionAst, target: string): boolean {
  return condition.left.kind === "identifier" &&
    condition.left.name === target &&
    (condition.op === "<=" || condition.op === "==") &&
    isZeroExpression(condition.right);
}

function flOpcode(register: RegisterName): number | undefined {
  switch (register) {
    case "0":
      return 0x5d;
    case "1":
      return 0x5b;
    case "2":
      return 0x58;
    case "3":
      return 0x5a;
    default:
      return undefined;
  }
}

function isSimpleStackLoad(expr: ExpressionAst): boolean {
  return expr.kind === "identifier" || expr.kind === "number";
}

function canTestAgainstZeroDirectly(op: ConditionAst["op"]): boolean {
  return op === "==" || op === "!=" || op === ">=" || op === "<";
}

function directTestOpcode(op: ConditionAst["op"]): number {
  switch (op) {
    case "==":
      return 0x5e;
    case "!=":
      return 0x57;
    case ">=":
      return 0x59;
    case "<":
      return 0x5c;
    default:
      throw new Error(`No direct zero-test opcode for ${op}`);
  }
}

function selectCheaperEquivalentCondition(
  condition: ConditionAst,
  ast: ProgramAst,
  preloadedConstants?: ReadonlySet<string>,
): { condition: ConditionAst; changed: boolean } {
  let best = condition;
  let bestCost = conditionCompileCost(condition, preloadedConstants);
  for (const candidate of equivalentConditionCandidates(condition, ast)) {
    const cost = conditionCompileCost(candidate, preloadedConstants);
    if (cost < bestCost) {
      best = candidate;
      bestCost = cost;
    }
  }
  return { condition: best, changed: !conditionEquals(best, condition) };
}

function equivalentConditionCandidates(condition: ConditionAst, ast: ProgramAst): ConditionAst[] {
  const candidates: ConditionAst[] = [];
  const add = (candidate: ConditionAst): void => {
    if (!candidates.some((existing) => conditionEquals(existing, candidate))) candidates.push(candidate);
  };
  add(condition);
  const flipped = flipNumericLeftCondition(condition);
  if (flipped !== undefined) add(flipped);
  for (const candidate of [...candidates]) {
    for (const boundary of integerBoundaryCandidates(candidate, ast)) add(boundary);
  }
  return candidates;
}

function flipNumericLeftCondition(condition: ConditionAst): ConditionAst | undefined {
  if (condition.left.kind !== "number") return undefined;
  return {
    left: condition.right,
    op: flipComparisonOp(condition.op),
    right: condition.left,
  };
}

function flipComparisonOp(op: ConditionAst["op"]): ConditionAst["op"] {
  switch (op) {
    case "<":
      return ">";
    case "<=":
      return ">=";
    case ">":
      return "<";
    case ">=":
      return "<=";
    case "==":
    case "!=":
      return op;
  }
}

function integerBoundaryCandidates(condition: ConditionAst, ast: ProgramAst): ConditionAst[] {
  if (!isKnownIntegerExpression(condition.left, ast)) return [];
  const value = numericLiteralValue(condition.right);
  if (value === undefined || !Number.isSafeInteger(value)) return [];
  const shifted = shiftedIntegerBoundary(condition.op, value);
  if (shifted === undefined) return [];
  return [{
    left: condition.left,
    op: shifted.op,
    right: numberExpression(shifted.value),
  }];
}

function shiftedIntegerBoundary(
  op: ConditionAst["op"],
  value: number,
): { op: ConditionAst["op"]; value: number } | undefined {
  switch (op) {
    case "<":
      return Number.isSafeInteger(value - 1) ? { op: "<=", value: value - 1 } : undefined;
    case "<=":
      return Number.isSafeInteger(value + 1) ? { op: "<", value: value + 1 } : undefined;
    case ">":
      return Number.isSafeInteger(value + 1) ? { op: ">=", value: value + 1 } : undefined;
    case ">=":
      return Number.isSafeInteger(value - 1) ? { op: ">", value: value - 1 } : undefined;
    case "==":
    case "!=":
      return undefined;
  }
}

function isKnownIntegerExpression(expr: ExpressionAst, ast: ProgramAst): boolean {
  return expr.kind === "identifier" && integerRangeFor(expr.name, ast) !== undefined;
}

function isKnownIntegerValuedExpression(expr: ExpressionAst, ast: ProgramAst): boolean {
  if (expr.kind === "number") return Number.isSafeInteger(Number(expr.raw));
  if (expr.kind === "identifier") return integerRangeFor(expr.name, ast) !== undefined;
  if (expr.kind === "unary" && expr.op === "-") return isKnownIntegerValuedExpression(expr.expr, ast);
  if (expr.kind === "call" && expr.args.length === 1) {
    const name = expr.callee.toLowerCase();
    if (name === "int") return true;
    if (name === "abs") return isKnownIntegerValuedExpression(expr.args[0]!, ast);
  }
  if (expr.kind === "binary" && (expr.op === "+" || expr.op === "-" || expr.op === "*")) {
    return isKnownIntegerValuedExpression(expr.left, ast) && isKnownIntegerValuedExpression(expr.right, ast);
  }
  return false;
}

function numericRangeForExpression(expr: ExpressionAst, ast: ProgramAst): { min?: number; max?: number } | undefined {
  const value = numericLiteralValue(expr);
  if (value !== undefined) return { min: value, max: value };
  if (expr.kind === "identifier") return numericRangeFor(expr.name, ast);
  if (expr.kind === "unary" && expr.op === "-") {
    const range = numericRangeForExpression(expr.expr, ast);
    if (range === undefined) return undefined;
    return {
      ...(range.max === undefined ? {} : { min: -range.max }),
      ...(range.min === undefined ? {} : { max: -range.min }),
    };
  }
  if (expr.kind === "call" && expr.callee.toLowerCase() === "abs" && expr.args.length === 1) {
    const range = numericRangeForExpression(expr.args[0]!, ast);
    if (range === undefined || range.min === undefined || range.max === undefined) return undefined;
    return {
      min: range.min <= 0 && range.max >= 0 ? 0 : Math.min(Math.abs(range.min), Math.abs(range.max)),
      max: Math.max(Math.abs(range.min), Math.abs(range.max)),
    };
  }
  return undefined;
}

function conditionCompileCost(condition: ConditionAst, preloadedConstants?: ReadonlySet<string>): number {
  if (isZeroExpression(condition.right) && canTestAgainstZeroDirectly(condition.op)) {
    return estimateExpressionCostForCondition(condition.left, preloadedConstants) + 2;
  }
  return estimateExpressionCostForCondition(condition.left, preloadedConstants) +
    estimateExpressionCostForCondition(condition.right, preloadedConstants) +
    3;
}

function conditionEquals(left: ConditionAst, right: ConditionAst): boolean {
  return left.op === right.op && expressionEquals(left.left, right.left) && expressionEquals(left.right, right.right);
}

function conditionToText(condition: ConditionAst): string {
  return `${expressionToIntentText(condition.left)} ${condition.op} ${expressionToIntentText(condition.right)}`;
}

function expressionToIntentText(expr: ExpressionAst): string {
  switch (expr.kind) {
    case "number":
      return expr.raw;
    case "identifier":
      return expr.name;
    case "unary":
      return `-${wrapExpressionText(expr.expr, 3)}`;
    case "binary":
      return `${wrapExpressionText(expr.left, binaryPrecedence(expr.op))} ${expr.op} ${wrapExpressionText(expr.right, binaryPrecedence(expr.op) + (expr.op === "-" || expr.op === "/" ? 1 : 0))}`;
    case "call":
      return `${expr.callee}(${expr.args.map(expressionToIntentText).join(", ")})`;
  }
}

function wrapExpressionText(expr: ExpressionAst, parentPrecedence: number): string {
  const text = expressionToIntentText(expr);
  const precedence = expressionPrecedence(expr);
  return precedence < parentPrecedence ? `(${text})` : text;
}

function expressionPrecedence(expr: ExpressionAst): number {
  switch (expr.kind) {
    case "number":
    case "identifier":
    case "call":
      return 4;
    case "unary":
      return 3;
    case "binary":
      return binaryPrecedence(expr.op);
  }
}

function binaryPrecedence(op: Extract<ExpressionAst, { kind: "binary" }>["op"]): number {
  return op === "*" || op === "/" ? 2 : 1;
}

function priority(
  variable: string,
  hints: Map<string, { mode: "prefer" | "fixed"; register: RegisterName }>,
): number {
  const hint = hints.get(variable);
  if (hint?.mode === "prefer") return REGISTER_ORDER.indexOf(hint.register) - 100;
  return 0;
}

function optimizeItems(
  items: MachineItem[],
  options: CompileOptions,
  optimizations: AppliedOptimization[],
): { items: MachineItem[]; preloads: PreloadReport[] } {
  const result = runIrPasses(items, options);
  optimizations.push(...result.optimizations);
  return { items: result.items, preloads: result.preloads };
}

function layoutProgram(
  items: MachineItem[],
  diagnostics: Diagnostic[],
  options: CompileOptions,
  ast: ProgramAst,
  machineProfile: MachineProfile,
): { steps: ResolvedStep[]; labels: Record<string, string>; cellRoles: CellRoleReport[] } {
  const labelAddresses = new Map<string, number>();
  let address = 0;
  for (const item of items) {
    if (item.kind === "label") {
      labelAddresses.set(item.name, address);
    } else {
      address += 1;
    }
  }

  const steps: ResolvedStep[] = [];
  const cellRoles: CellRoleReport[] = [];
  address = 0;
  for (const item of items) {
    if (item.kind === "label") continue;
    if (!options.analysis && address > 0xff) {
      diagnostics.push(
        buildDiagnostic("error", `Program address ${address} exceeds formal MK-61 address range.`),
      );
      break;
    }
    if (item.kind === "op") {
      const step = buildResolvedStep(address, item.opcode, item.mnemonic, item.comment);
      steps.push(step);
      cellRoles.push(buildCellRole(address, step.hex, item, options, machineProfile));
      address += 1;
      continue;
    }
    const targetAddress =
      typeof item.target === "number" ? item.target : labelAddresses.get(item.target);
    if (targetAddress === undefined) {
      diagnostics.push(
        buildDiagnostic("error", `Unknown label '${item.target}'`, item.sourceLine),
      );
      continue;
    }
    const opcode = item.formalOpcode ?? safeAddressToOpcode(targetAddress, item.sourceLine, diagnostics, options);
    if (opcode === undefined) {
      address += 1;
      continue;
    }
    steps.push(
      buildResolvedStep(
        address,
        opcode,
        item.formalOpcode === undefined ? safeFormatAddress(targetAddress) : formatFormalAddressOpcode(item.formalOpcode),
        item.comment,
      ),
    );
    cellRoles.push(buildAddressCellRole(address, opcode, item, options, machineProfile));
    address += 1;
  }

  const labels: Record<string, string> = {};
  const sortedLabels = [...labelAddresses.entries()].sort(
    ([, a], [, b]) => a - b,
  );
  for (const [label, labelAddress] of sortedLabels) {
    labels[label] = safeFormatAddress(labelAddress);
  }
  markDarkEntryCells(cellRoles, labelAddresses, options, ast, machineProfile);
  return { steps, labels, cellRoles };
}

function buildResolvedStep(
  address: number,
  opcode: number,
  mnemonic: string,
  comment?: string,
): ResolvedStep {
  const step: ResolvedStep = {
    address,
    opcode,
    hex: getOpcode(opcode).hex,
    mnemonic,
  };
  if (comment !== undefined) step.comment = comment;
  return step;
}

function safeAddressToOpcode(
  address: number,
  line: number | undefined,
  diagnostics: Diagnostic[],
  options: CompileOptions,
): number | undefined {
  if (options.analysis && Number.isInteger(address) && address >= 0) {
    return address & 0xff;
  }
  try {
    return addressToOpcode(address);
  } catch (error) {
    diagnostics.push(
      buildDiagnostic(
        "error",
        error instanceof Error ? error.message : String(error),
        line,
      ),
    );
    return undefined;
  }
}

function safeFormatAddress(address: number): string {
  try {
    return formatAddress(address);
  } catch {
    return `>${address.toString(16).toUpperCase()}`;
  }
}

function buildDiagnostic(
  level: "warning" | "error",
  message: string,
  line?: number,
  code?: string,
): Diagnostic {
  const diagnostic: Diagnostic = { level, message };
  if (line !== undefined) diagnostic.line = line;
  if (code !== undefined) diagnostic.code = code;
  return diagnostic;
}

function buildCellRole(
  address: number,
  hex: string,
  item: MachineOp,
  options: CompileOptions,
  machineProfile: MachineProfile,
): CellRoleReport {
  const roles: CellRole[] = ["exec"];
  const notes: string[] = [];
  if (item.raw) {
    roles.push("constant");
    notes.push("raw opcode can also be read as a byte");
  }
  if (machineSupports(machineProfile, "display-bytes") && item.comment?.includes("display")) {
    roles.push("display-byte");
    notes.push("display byte role allowed");
  }
  const role: CellRoleReport = {
    address: safeFormatAddress(address),
    hex,
    roles: uniqueRoles(roles),
  };
  if (notes.length > 0) role.note = notes.join("; ");
  return role;
}

function buildAddressCellRole(
  address: number,
  opcode: number,
  item: MachineAddressRef,
  options: CompileOptions,
  machineProfile: MachineProfile,
): CellRoleReport {
  const roles: CellRole[] = ["address"];
  const notes: string[] = [];
  if (item.formalOpcode !== undefined) {
    const info = formalAddressInfo(item.formalOpcode);
    roles.push("formal-address");
    notes.push(`formal address ${info.label} maps to ${safeFormatAddress(info.actual)} (${info.kind})`);
    if (info.kind !== "official" && machineSupports(machineProfile, "dark-entries")) {
      roles.push("dark-entry");
      notes.push("uses formal/dark program-address mapping");
    }
  }
  if (machineSupports(machineProfile, "address-constants")) {
    roles.push("constant");
    notes.push("address can be reused as constant");
  }
  if (machineSupports(machineProfile, "code-data-overlay")) {
    roles.push("overlay");
    notes.push("code/data overlay allowed");
  }
  const role: CellRoleReport = {
    address: safeFormatAddress(address),
    hex: getOpcode(opcode).hex,
    roles: uniqueRoles(roles),
  };
  if (notes.length > 0) role.note = notes.join("; ");
  return role;
}

function markDarkEntryCells(
  cellRoles: CellRoleReport[],
  labelAddresses: Map<string, number>,
  options: CompileOptions,
  ast: ProgramAst,
  machineProfile: MachineProfile,
): void {
  if (!machineSupports(machineProfile, "dark-entries")) return;
  const sharedTailNames = new Set(
    ast.blocks.filter((block) => block.mode === "shared_tail").map((block) => block.name),
  );
  for (const [label, address] of labelAddresses) {
    if (!sharedTailNames.has(label)) continue;
    const cell = cellRoles.find((candidate) => candidate.address === safeFormatAddress(address));
    if (!cell) continue;
    cell.roles = uniqueRoles([...cell.roles, "dark-entry"]);
    cell.note = [cell.note, "shared tail can be used as dark-entry target"].filter(Boolean).join("; ");
  }
}

function uniqueRoles(roles: CellRole[]): CellRole[] {
  return [...new Set(roles)];
}

type BranchRemovalCandidate = BranchAssignCandidate | BranchTerminalCandidate;

interface GuardedUpdate {
  target: string;
  op: "+" | "-";
  delta: ExpressionAst;
}

interface GuardedUpdateSelectorCandidate {
  selector: ExpressionAst;
  updates: GuardedUpdate[];
  name: string;
  detail: string;
  usesNegativeZero: boolean;
}

interface BranchAssignCandidate {
  kind: "assign";
  target: string;
  expr: ExpressionAst;
  name: string;
  detail: string;
}

interface BranchTerminalCandidate {
  kind: "pause" | "halt";
  expr: ExpressionAst;
  name: string;
  detail: string;
}

function buildBranchRemovalCandidate(
  statement: Extract<StatementAst, { kind: "if" }>,
  ast: ProgramAst,
  options: { negativeZeroDegree?: boolean } = {},
): BranchRemovalCandidate | undefined {
  return buildTerminalSelectCandidate(statement, ast, options) ??
    buildComparisonBooleanCandidate(statement) ??
    buildBooleanAlgebraCandidate(statement, ast) ??
    buildAbsCandidate(statement) ??
    buildMaxMinCandidate(statement) ??
    buildClampCandidate(statement) ??
    buildSaturatingUpdateCandidate(statement, ast) ??
    buildBooleanSignToggleCandidate(statement, ast) ??
    buildBooleanUpdateCandidate(statement, ast) ??
    buildArithmeticIfSelect(statement, ast, options);
}

function buildGuardedUpdateSelectorCandidate(
  statement: Extract<StatementAst, { kind: "if" }>,
  ast: ProgramAst,
  options: { negativeZeroDegree?: boolean } = {},
): GuardedUpdateSelectorCandidate | undefined {
  const updates = guardedUpdates(statement);
  if (updates === undefined) return undefined;

  const booleanSelector = booleanSelectorExpression(statement.condition, ast);
  const negativeZeroSelector = options.negativeZeroDegree
    ? negativeZeroThresholdSelectorExpression(statement.condition, ast)
    : undefined;
  const selector = booleanSelector ?? negativeZeroSelector ?? comparisonSelectorExpression(statement.condition);
  if (selector === undefined) return undefined;

  const usesNegativeZero = booleanSelector === undefined && negativeZeroSelector !== undefined;
  if (!usesNegativeZero && updates.length < 2) return undefined;
  return {
    selector,
    updates,
    name: usesNegativeZero ? "negative-zero-threshold-update" : "multi-guarded-update",
    detail: usesNegativeZero
      ? "Replaced threshold guarded update with a negative-zero selector"
      : "Replaced guarded updates with one stored arithmetic selector",
    usesNegativeZero,
  };
}

function guardedUpdates(statement: Extract<StatementAst, { kind: "if" }>): GuardedUpdate[] | undefined {
  if (statement.elseBody !== undefined || statement.thenBody.length === 0) return undefined;
  const updates: GuardedUpdate[] = [];
  for (const inner of statement.thenBody) {
    if (inner.kind !== "assign") return undefined;
    const plus = matchTargetPlusDelta(inner.expr, inner.target);
    if (plus !== undefined) {
      if (!expressionPureForSubstitution(plus)) return undefined;
      updates.push({ target: inner.target, op: "+", delta: plus });
      continue;
    }
    const minus = matchTargetMinusDelta(inner.expr, inner.target);
    if (minus !== undefined) {
      if (!expressionPureForSubstitution(minus)) return undefined;
      updates.push({ target: inner.target, op: "-", delta: minus });
      continue;
    }
    return undefined;
  }
  return updates;
}

function maskedGuardedUpdateExpression(update: GuardedUpdate, selector: ExpressionAst): ExpressionAst {
  const current: ExpressionAst = { kind: "identifier", name: update.target };
  const delta = multiplyExpressions(update.delta, selector);
  return update.op === "+"
    ? addExpressions(current, delta)
    : subtractExpressions(current, delta);
}

function buildDoubleClampCandidate(
  first: Extract<StatementAst, { kind: "if" }>,
  second: Extract<StatementAst, { kind: "if" }>,
): BranchAssignCandidate | undefined {
  const lower = clampBound(first, "lower");
  const upper = clampBound(second, "upper");
  if (!lower || !upper || lower.target !== upper.target) return undefined;
  const targetExpr: ExpressionAst = { kind: "identifier", name: lower.target };
  return {
    kind: "assign",
    target: lower.target,
    expr: minExpression(maxExpression(targetExpr, lower.bound), upper.bound),
    name: "arithmetic-if-double-clamp",
    detail: "Replaced adjacent lower/upper clamp branches with min(max())",
  };
}

function clampBound(
  statement: Extract<StatementAst, { kind: "if" }>,
  direction: "lower" | "upper",
): { target: string; bound: ExpressionAst } | undefined {
  const assign = singleEffectiveAssign(statement);
  if (!assign) return undefined;
  const targetExpr: ExpressionAst = { kind: "identifier", name: assign.target };
  const { left, right, op } = statement.condition;
  if (!expressionEquals(left, targetExpr) || !expressionEquals(assign.expr, right)) return undefined;
  if (direction === "lower" && (op === "<" || op === "<=")) return { target: assign.target, bound: right };
  if (direction === "upper" && (op === ">" || op === ">=")) return { target: assign.target, bound: right };
  return undefined;
}

function buildTerminalSelectCandidate(
  statement: Extract<StatementAst, { kind: "if" }>,
  ast: ProgramAst,
  options: { negativeZeroDegree?: boolean } = {},
): BranchRemovalCandidate | undefined {
  if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) return undefined;
  const thenStatement = effectiveTerminalStatement(statement.thenBody[0], ast);
  const elseStatement = effectiveTerminalStatement(statement.elseBody[0], ast);
  if (!thenStatement || !elseStatement) return undefined;
  if (elseStatement.kind !== thenStatement.kind) return undefined;

  const booleanSelector = booleanSelectorExpression(statement.condition, ast);
  const negativeZeroSelector = options.negativeZeroDegree
    ? negativeZeroThresholdSelectorExpression(statement.condition, ast)
    : undefined;
  const selector = booleanSelector ?? negativeZeroSelector ?? comparisonSelectorExpression(statement.condition);
  if (!selector) return undefined;
  const usesNegativeZero = booleanSelector === undefined && negativeZeroSelector !== undefined;
  return {
    kind: thenStatement.kind,
    expr: terminalSelectExpression(thenStatement.expr, elseStatement.expr, selector),
    name: usesNegativeZero ? "negative-zero-threshold-terminal-select" : "arithmetic-if-terminal-select",
    detail: usesNegativeZero
      ? `Replaced threshold ${thenStatement.kind} if/else with negative-zero selection`
      : `Replaced boolean ${thenStatement.kind} if/else with arithmetic selection`,
  };
}

function effectiveTerminalStatement(
  statement: StatementAst | undefined,
  ast: ProgramAst,
): Extract<StatementAst, { kind: "pause" | "halt" }> | undefined {
  if (statement === undefined) return undefined;
  if (statement.kind === "pause" || statement.kind === "halt") return statement;
  if (statement.kind !== "call") return undefined;
  const proc = ast.procs.find((candidate) => candidate.name === statement.block);
  if (proc === undefined || proc.body.length !== 1) return undefined;
  const terminal = proc.body[0];
  return terminal?.kind === "pause" || terminal?.kind === "halt" ? terminal : undefined;
}

function terminalSelectExpression(
  thenExpr: ExpressionAst,
  elseExpr: ExpressionAst,
  selector: ExpressionAst,
): ExpressionAst {
  const thenValue = numericLiteralValue(thenExpr);
  const elseValue = numericLiteralValue(elseExpr);
  if (thenValue !== undefined && elseValue !== undefined) {
    const delta = thenValue - elseValue;
    if (delta === 0) return numberExpression(thenValue);
    return addExpressions(
      numberExpression(elseValue),
      multiplyExpressions(numberExpression(delta), selector),
    );
  }
  return addExpressions(
    multiplyExpressions(thenExpr, selector),
    multiplyExpressions(elseExpr, oneMinus(selector)),
  );
}

function comparisonSelectorExpression(condition: ConditionAst): ExpressionAst | undefined {
  const { left, right, op } = condition;
  switch (op) {
    case "==":
      return oneMinus(signExpression(absExpression(subtractExpressions(left, right))));
    case "!=":
      return signExpression(absExpression(subtractExpressions(left, right)));
    case ">":
      return maxExpression(numberExpression(0), signExpression(subtractExpressions(left, right)));
    case "<":
      return maxExpression(numberExpression(0), signExpression(subtractExpressions(right, left)));
    case ">=":
      return oneMinus(
        maxExpression(numberExpression(0), signExpression(subtractExpressions(right, left))),
      );
    case "<=":
      return oneMinus(
        maxExpression(numberExpression(0), signExpression(subtractExpressions(left, right))),
      );
    default:
      return undefined;
  }
}

function negativeZeroThresholdSelectorExpression(condition: ConditionAst, ast: ProgramAst): ExpressionAst | undefined {
  const threshold = matchNegativeZeroThresholdCondition(condition, ast);
  if (threshold === undefined) return undefined;
  const selector: ExpressionAst = {
    kind: "call",
    callee: NEGATIVE_ZERO_DEGREE_SELECTOR_GE,
    args: [threshold.value, numberExpression(threshold.bound)],
  };
  return threshold.truth === "ge" ? selector : oneMinus(selector);
}

function matchNegativeZeroThresholdCondition(
  condition: ConditionAst,
  ast: ProgramAst,
): { value: ExpressionAst; bound: number; truth: "ge" | "lt" } | undefined {
  if (condition.left.kind === "number") {
    const flipped = flipNumericLeftCondition(condition);
    return flipped === undefined ? undefined : matchNegativeZeroThresholdCondition(flipped, ast);
  }

  const value = condition.left;
  const bound = numericLiteralValue(condition.right);
  if (bound === undefined || !Number.isFinite(bound) || bound <= 0 || bound > 1e12) return undefined;
  if (!isKnownIntegerValuedExpression(value, ast)) return undefined;
  const range = numericRangeForExpression(value, ast);
  if (range === undefined || range.min === undefined || range.min < 0) return undefined;
  if (range.max !== undefined && range.max / bound >= 1e60) return undefined;

  switch (condition.op) {
    case ">=":
      return { value, bound, truth: "ge" };
    case "<":
      return { value, bound, truth: "lt" };
    case ">":
      return Number.isSafeInteger(bound + 1) ? { value, bound: bound + 1, truth: "ge" } : undefined;
    case "<=":
      return Number.isSafeInteger(bound + 1) ? { value, bound: bound + 1, truth: "lt" } : undefined;
    case "==":
    case "!=":
      return undefined;
  }
}

function buildArithmeticIfSelect(
  statement: Extract<StatementAst, { kind: "if" }>,
  ast: ProgramAst,
  options: { negativeZeroDegree?: boolean } = {},
): BranchRemovalCandidate | undefined {
  if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) {
    return undefined;
  }
  const thenAssign = statement.thenBody[0];
  const elseAssign = statement.elseBody[0];
  if (thenAssign?.kind !== "assign" || elseAssign?.kind !== "assign") return undefined;
  if (thenAssign.target !== elseAssign.target) return undefined;

  const booleanSelector = booleanSelectorExpression(statement.condition, ast);
  const negativeZeroSelector = options.negativeZeroDegree
    ? negativeZeroThresholdSelectorExpression(statement.condition, ast)
    : undefined;
  const selector = booleanSelector ?? negativeZeroSelector;
  if (!selector) return undefined;
  const usesNegativeZero = booleanSelector === undefined && negativeZeroSelector !== undefined;

  const expr = addExpressions(
    multiplyExpressions(thenAssign.expr, selector),
    multiplyExpressions(elseAssign.expr, oneMinus(selector)),
  );
  return {
    kind: "assign",
    target: thenAssign.target,
    expr,
    name: usesNegativeZero ? "negative-zero-threshold-select" : "arithmetic-if-select",
    detail: usesNegativeZero
      ? "Replaced threshold if/else assignment with negative-zero selection"
      : "Replaced boolean if/else with arithmetic selection",
  };
}

function buildComparisonBooleanCandidate(statement: Extract<StatementAst, { kind: "if" }>): BranchRemovalCandidate | undefined {
  if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) return undefined;
  const thenAssign = statement.thenBody[0];
  const elseAssign = statement.elseBody[0];
  if (thenAssign?.kind !== "assign" || elseAssign?.kind !== "assign") return undefined;
  if (thenAssign.target !== elseAssign.target) return undefined;

  const thenValue = numericLiteralValue(thenAssign.expr);
  const elseValue = numericLiteralValue(elseAssign.expr);
  if (!((thenValue === 1 && elseValue === 0) || (thenValue === 0 && elseValue === 1))) return undefined;

  const truth = comparisonMask(statement.condition);
  if (!truth) return undefined;
  return {
    kind: "assign",
    target: thenAssign.target,
    expr: thenValue === 1 ? truth : oneMinus(truth),
    name: "arithmetic-if-comparison-mask",
    detail: "Replaced comparison-to-boolean branch with arithmetic mask",
  };
}

function buildBooleanAlgebraCandidate(
  statement: Extract<StatementAst, { kind: "if" }>,
  ast: ProgramAst,
): BranchRemovalCandidate | undefined {
  if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) return undefined;
  const thenAssign = statement.thenBody[0];
  const elseAssign = statement.elseBody[0];
  if (thenAssign?.kind !== "assign" || elseAssign?.kind !== "assign") return undefined;
  if (thenAssign.target !== elseAssign.target) return undefined;

  const selector = booleanSelectorExpression(statement.condition, ast);
  const selectorName = booleanSelectorVariableName(statement.condition, ast);
  if (!selector || !selectorName) return undefined;
  const otherThen = booleanIdentifier(thenAssign.expr, ast);
  const otherElse = booleanIdentifier(elseAssign.expr, ast);
  const thenValue = numericLiteralValue(thenAssign.expr);
  const elseValue = numericLiteralValue(elseAssign.expr);
  if (otherThen && elseValue === 0) {
    return booleanAlgebraCandidate(thenAssign.target, multiplyExpressions(selector, otherThen), "and");
  }
  if (thenValue === 1 && otherElse) {
    return booleanAlgebraCandidate(thenAssign.target, maxExpression(selector, otherElse), "or");
  }
  if (otherThen && otherElse && expressionEquals(otherThen, otherElse)) return undefined;
  if (otherThen && otherElse && expressionEquals(thenAssign.expr, oneMinus(otherElse))) {
    return booleanAlgebraCandidate(thenAssign.target, absExpression(subtractExpressions(selector, otherElse)), "xor");
  }
  return undefined;
}

function booleanAlgebraCandidate(target: string, expr: ExpressionAst, operation: string): BranchRemovalCandidate {
  return {
    kind: "assign",
    target,
    expr,
    name: "arithmetic-if-boolean-algebra",
    detail: `Replaced boolean ${operation.toUpperCase()} branch with arithmetic expression`,
  };
}

function buildBooleanUpdateCandidate(
  statement: Extract<StatementAst, { kind: "if" }>,
  ast: ProgramAst,
): BranchRemovalCandidate | undefined {
  if (statement.elseBody || statement.thenBody.length !== 1) return undefined;
  const assign = statement.thenBody[0];
  if (assign?.kind !== "assign") return undefined;

  const selector = booleanSelectorExpression(statement.condition, ast);
  if (!selector) return undefined;

  const current: ExpressionAst = { kind: "identifier", name: assign.target };
  const plus = matchTargetPlusDelta(assign.expr, assign.target);
  if (plus) {
    return {
      kind: "assign",
      target: assign.target,
      expr: addExpressions(current, multiplyExpressions(plus, selector)),
      name: "arithmetic-if-update",
      detail: "Replaced conditional addition with boolean-masked arithmetic",
    };
  }

  const minus = matchTargetMinusDelta(assign.expr, assign.target);
  if (minus) {
    return {
      kind: "assign",
      target: assign.target,
      expr: subtractExpressions(current, multiplyExpressions(minus, selector)),
      name: "arithmetic-if-update",
      detail: "Replaced conditional subtraction with boolean-masked arithmetic",
    };
  }

  if (expressionEquals(assign.expr, negateExpression(current))) {
    return {
      kind: "assign",
      target: assign.target,
      expr: signToggleExpression(current, selector),
      name: "arithmetic-if-sign-toggle",
      detail: "Replaced conditional sign toggle with boolean-masked multiplier",
    };
  }

  return buildConditionalMoveCandidate(assign, selector);
}

function buildBooleanSignToggleCandidate(
  statement: Extract<StatementAst, { kind: "if" }>,
  ast: ProgramAst,
): BranchRemovalCandidate | undefined {
  if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) return undefined;
  const thenAssign = statement.thenBody[0];
  const elseAssign = statement.elseBody[0];
  if (thenAssign?.kind !== "assign" || elseAssign?.kind !== "assign") return undefined;
  if (thenAssign.target !== elseAssign.target || !isIdentityAssignment(elseAssign)) return undefined;
  const selector = booleanSelectorExpression(statement.condition, ast);
  if (!selector) return undefined;
  const current: ExpressionAst = { kind: "identifier", name: thenAssign.target };
  if (!expressionEquals(thenAssign.expr, negateExpression(current))) return undefined;
  return {
    kind: "assign",
    target: thenAssign.target,
    expr: signToggleExpression(current, selector),
    name: "arithmetic-if-sign-toggle",
    detail: "Replaced conditional sign toggle with boolean-masked multiplier",
  };
}

function buildConditionalMoveCandidate(
  assign: Extract<StatementAst, { kind: "assign" }>,
  selector: ExpressionAst,
): BranchRemovalCandidate | undefined {
  const current: ExpressionAst = { kind: "identifier", name: assign.target };
  if (expressionEquals(assign.expr, current)) return undefined;
  return {
    kind: "assign",
    target: assign.target,
    expr: addExpressions(
      multiplyExpressions(current, oneMinus(selector)),
      multiplyExpressions(assign.expr, selector),
    ),
    name: "arithmetic-if-conditional-move",
    detail: "Replaced conditional assignment with boolean-masked conditional move",
  };
}

function buildSaturatingUpdateCandidate(
  statement: Extract<StatementAst, { kind: "if" }>,
  ast: ProgramAst,
): BranchRemovalCandidate | undefined {
  if (statement.elseBody || statement.thenBody.length !== 1) return undefined;
  const assign = statement.thenBody[0];
  if (assign?.kind !== "assign") return undefined;
  const range = integerRangeFor(assign.target, ast);
  if (!range) return undefined;

  const targetExpr: ExpressionAst = { kind: "identifier", name: assign.target };
  const { left, right, op } = statement.condition;
  if (!expressionEquals(left, targetExpr)) return undefined;

  const decrement = matchTargetMinusDelta(assign.expr, assign.target);
  if (decrement && op === ">" && isNumericValue(decrement, 1) && range.min !== undefined && isNumericValue(right, range.min)) {
    return maxCandidate(assign.target, assign.expr, right, "Replaced saturating decrement branch with max()");
  }

  const increment = matchTargetPlusDelta(assign.expr, assign.target);
  if (increment && op === "<" && isNumericValue(increment, 1) && range.max !== undefined && isNumericValue(right, range.max)) {
    return minCandidate(assign.target, assign.expr, right, "Replaced saturating increment branch with min-via-max()");
  }

  return undefined;
}

function buildMaxMinCandidate(statement: Extract<StatementAst, { kind: "if" }>): BranchRemovalCandidate | undefined {
  if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) return undefined;
  const thenAssign = statement.thenBody[0];
  const elseAssign = statement.elseBody[0];
  if (thenAssign?.kind !== "assign" || elseAssign?.kind !== "assign") return undefined;
  if (thenAssign.target !== elseAssign.target) return undefined;

  const { left, right, op } = statement.condition;
  if (["<", "<=", ">", ">="].includes(op)) {
    if ((op === ">" || op === ">=") && expressionEquals(thenAssign.expr, left) && expressionEquals(elseAssign.expr, right)) {
      return maxCandidate(thenAssign.target, left, right);
    }
    if ((op === "<" || op === "<=") && expressionEquals(thenAssign.expr, right) && expressionEquals(elseAssign.expr, left)) {
      return maxCandidate(thenAssign.target, left, right);
    }
    if ((op === "<" || op === "<=") && expressionEquals(thenAssign.expr, left) && expressionEquals(elseAssign.expr, right)) {
      return minCandidate(thenAssign.target, left, right);
    }
    if ((op === ">" || op === ">=") && expressionEquals(thenAssign.expr, right) && expressionEquals(elseAssign.expr, left)) {
      return minCandidate(thenAssign.target, left, right);
    }
  }

  return undefined;
}

function buildClampCandidate(statement: Extract<StatementAst, { kind: "if" }>): BranchRemovalCandidate | undefined {
  const assign = singleEffectiveAssign(statement);
  if (!assign) return undefined;
  const targetExpr: ExpressionAst = { kind: "identifier", name: assign.target };
  const { left, right, op } = statement.condition;
  if (!expressionEquals(left, targetExpr)) return undefined;

  if ((op === "<" || op === "<=") && expressionEquals(assign.expr, right)) {
    return maxCandidate(assign.target, targetExpr, right, "Replaced lower clamp branch with max()");
  }
  if ((op === ">" || op === ">=") && expressionEquals(assign.expr, right)) {
    return minCandidate(assign.target, targetExpr, right, "Replaced upper clamp branch with min-via-max()");
  }
  return undefined;
}

function buildAbsCandidate(statement: Extract<StatementAst, { kind: "if" }>): BranchRemovalCandidate | undefined {
  if (statement.thenBody.length !== 1) return undefined;
  const thenAssign = statement.thenBody[0];
  if (thenAssign?.kind !== "assign") return undefined;
  const { left, right, op } = statement.condition;
  if (!isNumericValue(right, 0)) return undefined;
  const negativeLeft = negateExpression(left);

  if (!statement.elseBody) {
    const targetExpr: ExpressionAst = { kind: "identifier", name: thenAssign.target };
    if (!expressionEquals(targetExpr, left)) return undefined;
    if ((op === "<" || op === "<=") && expressionEquals(thenAssign.expr, negativeLeft)) {
      return absCandidate(thenAssign.target, left);
    }
    return undefined;
  }

  if (statement.elseBody.length !== 1) return undefined;
  const elseAssign = statement.elseBody[0];
  if (elseAssign?.kind !== "assign" || thenAssign.target !== elseAssign.target) return undefined;

  if ((op === "<" || op === "<=") &&
    expressionEquals(thenAssign.expr, negativeLeft) &&
    expressionEquals(elseAssign.expr, left)) {
    return absCandidate(thenAssign.target, left);
  }
  if ((op === ">" || op === ">=") &&
    expressionEquals(thenAssign.expr, left) &&
    expressionEquals(elseAssign.expr, negativeLeft)) {
    return absCandidate(thenAssign.target, left);
  }
  return undefined;
}

function absCandidate(target: string, expr: ExpressionAst): BranchRemovalCandidate {
  return {
    kind: "assign",
    target,
    expr: { kind: "call", callee: "abs", args: [expr] },
    name: "arithmetic-if-abs",
    detail: "Replaced sign branch with abs()",
  };
}

function maxCandidate(
  target: string,
  left: ExpressionAst,
  right: ExpressionAst,
  detail = "Replaced max branch with К max",
): BranchRemovalCandidate {
  return {
    kind: "assign",
    target,
    expr: { kind: "call", callee: "max", args: [left, right] },
    name: "arithmetic-if-max",
    detail,
  };
}

function minCandidate(
  target: string,
  left: ExpressionAst,
  right: ExpressionAst,
  detail = "Replaced min branch with min-via-max()",
): BranchRemovalCandidate {
  return {
    kind: "assign",
    target,
    expr: negateExpression({
      kind: "call",
      callee: "max",
      args: [negateExpression(left), negateExpression(right)],
    }),
    name: "arithmetic-if-min",
    detail,
  };
}

function booleanSelectorExpression(condition: ConditionAst, ast: ProgramAst): ExpressionAst | undefined {
  const leftIdentifier = condition.left.kind === "identifier" ? condition.left.name : undefined;
  const rightIdentifier = condition.right.kind === "identifier" ? condition.right.name : undefined;
  const leftNumber = numericLiteralValue(condition.left);
  const rightNumber = numericLiteralValue(condition.right);

  let variable: string | undefined;
  let value: number | undefined;
  if (leftIdentifier !== undefined && rightNumber !== undefined) {
    variable = leftIdentifier;
    value = rightNumber;
  } else if (rightIdentifier !== undefined && leftNumber !== undefined) {
    variable = rightIdentifier;
    value = leftNumber;
  }
  if (variable === undefined || value === undefined) return undefined;
  if (!isBooleanVariable(variable, ast)) return undefined;

  const variableExpr: ExpressionAst = { kind: "identifier", name: variable };
  if ((condition.op === "==" && value === 1) || (condition.op === "!=" && value === 0)) {
    return variableExpr;
  }
  if ((condition.op === "==" && value === 0) || (condition.op === "!=" && value === 1)) {
    return oneMinus(variableExpr);
  }
  return undefined;
}

function isBooleanVariable(name: string, ast: ProgramAst): boolean {
  for (const state of ast.states) {
    const field = state.fields.find((candidate) => candidate.name === name);
    if (!field) continue;
    if (field.type === "flag") return true;
    if (field.min === 0 && field.max === 1) return true;
  }
  return false;
}

function integerRangeFor(name: string, ast: ProgramAst): { min?: number; max?: number } | undefined {
  const range = numericRangeFor(name, ast);
  if (range === undefined) return undefined;
  if (!Number.isInteger(range.min) || !Number.isInteger(range.max)) return undefined;
  return range;
}

function numericRangeFor(name: string, ast: ProgramAst): { min?: number; max?: number } | undefined {
  for (const state of ast.states) {
    const field = state.fields.find((candidate) => candidate.name === name);
    if (!field) continue;
    if (field.type === "flag") return { min: 0, max: 1 };
    if (field.type === "range") {
      const range: { min?: number; max?: number } = {};
      if (field.min !== undefined) range.min = field.min;
      if (field.max !== undefined) range.max = field.max;
      return range;
    }
  }
  return undefined;
}

function booleanSelectorVariableName(condition: ConditionAst, ast: ProgramAst): string | undefined {
  const leftIdentifier = condition.left.kind === "identifier" ? condition.left.name : undefined;
  const rightIdentifier = condition.right.kind === "identifier" ? condition.right.name : undefined;
  const leftNumber = numericLiteralValue(condition.left);
  const rightNumber = numericLiteralValue(condition.right);
  const name = leftIdentifier !== undefined && rightNumber !== undefined
    ? leftIdentifier
    : rightIdentifier !== undefined && leftNumber !== undefined
      ? rightIdentifier
      : undefined;
  return name !== undefined && isBooleanVariable(name, ast) ? name : undefined;
}

function booleanIdentifier(expr: ExpressionAst, ast: ProgramAst): ExpressionAst | undefined {
  return expr.kind === "identifier" && isBooleanVariable(expr.name, ast) ? expr : undefined;
}

function comparisonMask(condition: ConditionAst): ExpressionAst | undefined {
  if (condition.op !== "==" && condition.op !== "!=") return undefined;
  const notEqual = signExpression(absExpression(subtractExpressions(condition.left, condition.right)));
  return condition.op === "==" ? oneMinus(notEqual) : notEqual;
}

function isTicTacToeMacroName(name: string): boolean {
  return ticTacToeMacroArity(name) !== undefined;
}

function ticTacToeMacroArity(name: string): number | undefined {
  const arities: Record<string, number> = {
    norm4: 1,
    grid4_norm: 1,
    bit_mask: 1,
    bit_has: 2,
    bit_set: 2,
    bit_clear: 2,
    bit_toggle: 2,
    diag_left_index: 2,
    diag_right_index: 2,
    cell_mask: 2,
    cell_has: 3,
    cell_set: 3,
    cell_clear: 3,
    cell_toggle: 3,
    cell_used: 3,
    cell_mark: 3,
    digit_at: 2,
    digit_add: 3,
    digit_set: 3,
    packed4_add: 3,
    packed4_digit: 2,
    packed4_score: 2,
  };
  return arities[name];
}

function ticTacToeExpressionMacro(name: string, args: ExpressionAst[]): ExpressionAst | undefined {
  switch (name) {
    case "norm4":
    case "grid4_norm":
      return norm4Expression(args[0]!);
    case "bit_mask":
      return bitMaskExpression(args[0]!);
    case "bit_has":
      return bitAndExpression(args[0]!, bitMaskExpression(args[1]!));
    case "bit_set":
      return bitOrExpression(args[0]!, bitMaskExpression(args[1]!));
    case "bit_clear":
      return bitAndExpression(args[0]!, bitNotExpression(bitMaskExpression(args[1]!)));
    case "bit_toggle":
      return bitXorExpression(args[0]!, bitMaskExpression(args[1]!));
    case "diag_left_index":
      return norm4Expression(addExpressions(args[0]!, args[1]!));
    case "diag_right_index":
      return norm4Expression(subtractExpressions(args[0]!, args[1]!));
    case "cell_mask":
      return cellMaskExpression(args[0]!, args[1]!);
    case "cell_has":
    case "cell_used":
      return bitAndExpression(args[0]!, cellMaskExpression(args[1]!, args[2]!));
    case "cell_set":
    case "cell_mark":
      return bitOrExpression(args[0]!, cellMaskExpression(args[1]!, args[2]!));
    case "cell_clear":
      return bitAndExpression(args[0]!, bitNotExpression(cellMaskExpression(args[1]!, args[2]!)));
    case "cell_toggle":
      return bitXorExpression(args[0]!, cellMaskExpression(args[1]!, args[2]!));
    case "digit_at":
      return packed4DigitExpression(args[0]!, args[1]!);
    case "digit_add":
    case "packed4_add":
      return addExpressions(
        args[0]!,
        multiplyExpressions(args[2]!, digitPlaceExpression(args[1]!)),
      );
    case "digit_set":
      return digitSetExpression(args[0]!, args[1]!, args[2]!);
    case "packed4_digit":
      return packed4DigitExpression(args[0]!, args[1]!);
    case "packed4_score":
      return {
        kind: "call",
        callee: "sqr",
        args: [subtractExpressions(packed4DigitExpression(args[0]!, args[1]!), numberExpression(0.41200076))],
      };
    default:
      return undefined;
  }
}

interface CellHelperCall {
  mask: ExpressionAst;
  x: ExpressionAst;
  y: ExpressionAst;
}

type CellHelperName = "cell_used" | "cell_has" | "cell_mark" | "cell_set";

interface BitMembershipCondition {
  collection: ExpressionAst;
  item: ExpressionAst;
  test: ExpressionAst;
}

interface BitSetAssignment {
  collection: ExpressionAst;
  item: ExpressionAst;
}

function matchCellHelperCall(expr: ExpressionAst, names: readonly CellHelperName[]): CellHelperCall | undefined {
  if (expr.kind !== "call" || !names.includes(expr.callee.toLowerCase() as CellHelperName) || expr.args.length !== 3) return undefined;
  return {
    mask: expr.args[0]!,
    x: expr.args[1]!,
    y: expr.args[2]!,
  };
}

function matchBitSetAssignment(statement: Extract<StatementAst, { kind: "assign" }>): BitSetAssignment | undefined {
  const expr = statement.expr;
  if (expr.kind !== "call" || expr.callee.toLowerCase() !== "bit_set" || expr.args.length !== 2) return undefined;
  const collection = expr.args[0]!;
  if (collection.kind !== "identifier" || statement.target !== collection.name) return undefined;
  return {
    collection,
    item: expr.args[1]!,
  };
}

function isReusableBitSetPair(
  first: Extract<StatementAst, { kind: "assign" }>,
  second: Extract<StatementAst, { kind: "assign" }>,
): boolean {
  const firstSet = matchBitSetAssignment(first);
  const secondSet = matchBitSetAssignment(second);
  return firstSet !== undefined &&
    secondSet !== undefined &&
    expressionEquals(firstSet.item, secondSet.item);
}

function isReusableMembershipSet(statement: Extract<StatementAst, { kind: "if" }>): boolean {
  const present = matchBitMembershipCondition(statement.condition);
  if (present !== undefined && statement.elseBody !== undefined) {
    const first = statement.elseBody[0];
    if (first?.kind === "assign" && isBitSetAssignment(first, present)) return true;
  }
  const absent = matchBitAbsenceCondition(statement.condition);
  if (absent === undefined) return false;
  const first = statement.thenBody[0];
  return first?.kind === "assign" && isBitSetAssignment(first, absent);
}

function bitMaskScratchName(statement: StatementAst): string {
  return `${BIT_MASK_SCRATCH_PREFIX}${statement.line}`;
}

function ifSelectorScratchName(statement: StatementAst): string {
  return `${IF_SELECTOR_SCRATCH_PREFIX}${statement.line}`;
}

function matchBitMembershipCondition(condition: ConditionAst): BitMembershipCondition | undefined {
  if (condition.op !== "!=" || !isZeroExpression(condition.right)) return undefined;
  const test = condition.left;
  if (test.kind !== "call" || test.callee.toLowerCase() !== "bit_has" || test.args.length !== 2) return undefined;
  return {
    collection: test.args[0]!,
    item: test.args[1]!,
    test,
  };
}

function matchBitAbsenceCondition(condition: ConditionAst): BitMembershipCondition | undefined {
  if (condition.op !== "==" || !isZeroExpression(condition.right)) return undefined;
  const test = condition.left;
  if (test.kind !== "call" || test.callee.toLowerCase() !== "bit_has" || test.args.length !== 2) return undefined;
  return {
    collection: test.args[0]!,
    item: test.args[1]!,
    test,
  };
}

function isBitClearAssignment(
  statement: Extract<StatementAst, { kind: "assign" }>,
  membership: BitMembershipCondition,
): boolean {
  if (membership.collection.kind !== "identifier" || statement.target !== membership.collection.name) return false;
  const expr = statement.expr;
  return expr.kind === "call" &&
    expr.callee.toLowerCase() === "bit_clear" &&
    expr.args.length === 2 &&
    expressionEquals(expr.args[0]!, membership.collection) &&
    expressionEquals(expr.args[1]!, membership.item);
}

function isBitSetAssignment(
  statement: Extract<StatementAst, { kind: "assign" }>,
  membership: BitMembershipCondition,
): boolean {
  if (membership.collection.kind !== "identifier" || statement.target !== membership.collection.name) return false;
  const expr = statement.expr;
  return expr.kind === "call" &&
    expr.callee.toLowerCase() === "bit_set" &&
    expr.args.length === 2 &&
    expressionEquals(expr.args[0]!, membership.collection) &&
    expressionEquals(expr.args[1]!, membership.item);
}

function norm4Expression(expr: ExpressionAst): ExpressionAst {
  const rem = multiplyExpressions(
    { kind: "call", callee: "frac", args: [divideExpressions({ kind: "call", callee: "int", args: [expr] }, numberExpression(4))] },
    numberExpression(4),
  );
  return addExpressions(
    rem,
    multiplyExpressions(numberExpression(4), oneMinus(signExpression(maxExpression(rem, numberExpression(0))))),
  );
}

function cellMaskExpression(x: ExpressionAst, y: ExpressionAst): ExpressionAst {
  return addExpressions(
    pow10Expression(x),
    { kind: "call", callee: "int", args: [multiplyExpressions(pow10Expression(y), numberExpression(0.22600029))] },
  );
}

function bitMaskExpression(index: ExpressionAst): ExpressionAst {
  const nibble = intExpression(divideExpressions(index, numberExpression(4)));
  const offset = subtractExpressions(index, multiplyExpressions(nibble, numberExpression(4)));
  return multiplyExpressions(
    powExpression(numberExpression(2), offset),
    pow10Expression(nibble),
  );
}

function packed4DigitExpression(lines: ExpressionAst, index: ExpressionAst): ExpressionAst {
  return {
    kind: "call",
    callee: "int",
    args: [
      multiplyExpressions(
        { kind: "call", callee: "frac", args: [divideExpressions(lines, pow10Expression(index))] },
        numberExpression(10),
      ),
    ],
  };
}

function digitSetExpression(value: ExpressionAst, index: ExpressionAst, digit: ExpressionAst): ExpressionAst {
  const place = digitPlaceExpression(index);
  return addExpressions(
    subtractExpressions(value, multiplyExpressions(packed4DigitExpression(value, index), place)),
    multiplyExpressions(digit, place),
  );
}

function digitPlaceExpression(index: ExpressionAst): ExpressionAst {
  return pow10Expression(subtractExpressions(index, numberExpression(1)));
}

function oneMinus(expr: ExpressionAst): ExpressionAst {
  if (isNumericValue(expr, 0)) return numberExpression(1);
  if (isNumericValue(expr, 1)) return numberExpression(0);
  return {
    kind: "binary",
    op: "-",
    left: numberExpression(1),
    right: expr,
  };
}

function multiplyExpressions(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  if (isNumericValue(left, 0) || isNumericValue(right, 0)) return numberExpression(0);
  if (isNumericValue(left, 1)) return right;
  if (isNumericValue(right, 1)) return left;
  return { kind: "binary", op: "*", left, right };
}

function addExpressions(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  if (isNumericValue(left, 0)) return right;
  if (isNumericValue(right, 0)) return left;
  return { kind: "binary", op: "+", left, right };
}

function subtractExpressions(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  if (isNumericValue(right, 0)) return left;
  if (isNumericValue(left, 0)) return { kind: "unary", op: "-", expr: right };
  return { kind: "binary", op: "-", left, right };
}

function divideExpressions(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  if (isNumericValue(right, 1)) return left;
  return { kind: "binary", op: "/", left, right };
}

function pow10Expression(expr: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "pow10", args: [expr] };
}

function powExpression(base: ExpressionAst, exponent: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "pow", args: [base, exponent] };
}

function maxExpression(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "max", args: [left, right] };
}

function minExpression(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  return negateExpression(maxExpression(negateExpression(left), negateExpression(right)));
}

function absExpression(expr: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "abs", args: [expr] };
}

function intExpression(expr: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "int", args: [expr] };
}

function signExpression(expr: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "sign", args: [expr] };
}

function bitAndExpression(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "bit_and", args: [left, right] };
}

function bitOrExpression(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "bit_or", args: [left, right] };
}

function bitXorExpression(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "bit_xor", args: [left, right] };
}

function bitNotExpression(expr: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "bit_not", args: [expr] };
}

function fracExpression(expr: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "frac", args: [expr] };
}

function spatialCountExpression(
  name: "neighbor_count" | "line_count",
  args: ExpressionAst[],
  ast: ProgramAst,
): ExpressionAst | undefined {
  const [mask, cell] = args;
  if (mask === undefined || cell === undefined) return undefined;
  if (name === "neighbor_count") {
    const board = boardForCellMask(mask, ast);
    const offsets = board?.height === 1
      ? [-1, 1]
      : board?.width === 1
        ? [-10, 10]
        : [-11, -10, -9, -1, 1, 9, 10, 11];
    return sumExpressions(offsets.map((offset) => spatialHitExpression(mask, offsetExpressionAst(cell, offset))));
  }

  const board = boardForCellMask(mask, ast);
  if (board !== undefined && board.width <= 4 && board.height <= 4) {
    return maxExpressions([
      sumExpressions(spatialLineCells(board, "row", cell).map((index) => spatialHitExpression(mask, index))),
      sumExpressions(spatialLineCells(board, "column", cell).map((index) => spatialHitExpression(mask, index))),
      sumExpressions(spatialLineCells(board, "diag-left", cell).map((index) => spatialHitExpression(mask, index))),
      sumExpressions(spatialLineCells(board, "diag-right", cell).map((index) => spatialHitExpression(mask, index))),
    ]);
  }

  const offsets = [-99, -90, -81, -72, -63, -54, -45, -36, -27, -18, -9, 0, 9, 18, 27, 36, 45, 54, 63, 72, 81, 90, 99];
  return sumExpressions([
    spatialHitExpression(mask, cell),
    ...offsets.filter((offset) => offset !== 0).map((offset) => spatialHitExpression(mask, offsetExpressionAst(cell, offset))),
  ]);
}

function spatialHitExpression(mask: ExpressionAst, index: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "__spatial_hit", args: [mask, index] };
}

function boardForCellMask(mask: ExpressionAst, ast: ProgramAst): V2BoardAst | undefined {
  if (mask.kind !== "identifier" || ast.v2 === undefined) return undefined;
  const domain = ast.v2.state.find((field) => field.name === mask.name)?.domain;
  if (domain === undefined) return undefined;
  return ast.v2.boards.find((board) => board.name === domain);
}

function spatialLineCells(
  board: V2BoardAst,
  kind: "row" | "column" | "diag-left" | "diag-right",
  cell: ExpressionAst,
): ExpressionAst[] {
  const x = decimalOnesExpressionAst(cell);
  const y = decimalTensExpressionAst(cell);
  switch (kind) {
    case "row":
      return range(board.xMin, board.xMax).map((candidateX) => boardCellExpressionAst(numberExpression(candidateX), y));
    case "column":
      return range(board.yMin, board.yMax).map((candidateY) => boardCellExpressionAst(x, numberExpression(candidateY)));
    case "diag-left":
      return range(-Math.max(board.width, board.height) + 1, Math.max(board.width, board.height) - 1)
        .map((delta) => offsetExpressionAst(cell, delta * 11));
    case "diag-right":
      return range(-Math.max(board.width, board.height) + 1, Math.max(board.width, board.height) - 1)
        .map((delta) => offsetExpressionAst(cell, delta * 9));
  }
}

interface SpatialLineProgression {
  startOffset: ExpressionAst;
  step: ExpressionAst;
  count: number;
}

function spatialLineProgressions(board: V2BoardAst, cell: ExpressionAst): SpatialLineProgression[] {
  const x = decimalOnesExpressionAst(cell);
  const y = decimalTensExpressionAst(cell);
  const span = Math.max(board.width, board.height);
  return [
    {
      startOffset: subtractExpressions(numberExpression(board.xMin), x),
      step: numberExpression(1),
      count: board.width,
    },
    {
      startOffset: multiplyExpressions(numberExpression(10), subtractExpressions(numberExpression(board.yMin), y)),
      step: numberExpression(10),
      count: board.height,
    },
    {
      startOffset: numberExpression(-(span - 1) * 11),
      step: numberExpression(11),
      count: span * 2 - 1,
    },
    {
      startOffset: numberExpression(-(span - 1) * 9),
      step: numberExpression(9),
      count: span * 2 - 1,
    },
  ];
}

function spatialNeighborProgressions(board: V2BoardAst | undefined): SpatialLineProgression[] {
  if (board?.height === 1) {
    return [{ startOffset: numberExpression(-1), step: numberExpression(2), count: 2 }];
  }
  if (board?.width === 1) {
    return [{ startOffset: numberExpression(-10), step: numberExpression(20), count: 2 }];
  }
  return [
    { startOffset: numberExpression(-11), step: numberExpression(1), count: 3 },
    { startOffset: numberExpression(-1), step: numberExpression(2), count: 2 },
    { startOffset: numberExpression(9), step: numberExpression(1), count: 3 },
  ];
}

function lineCountGroupKeyFor(board: V2BoardAst, cell: ExpressionAst): string {
  return `${board.name}:${board.xMin}:${board.xMax}:${board.yMin}:${board.yMax}:${expressionToIntentText(cell)}`;
}

function boardCellExpressionAst(x: ExpressionAst, y: ExpressionAst): ExpressionAst {
  return addExpressions(x, multiplyExpressions(numberExpression(10), y));
}

function decimalTensExpressionAst(expr: ExpressionAst): ExpressionAst {
  return intExpression(divideExpressions(expr, numberExpression(10)));
}

function decimalOnesExpressionAst(expr: ExpressionAst): ExpressionAst {
  return multiplyExpressions(fracExpression(divideExpressions(expr, numberExpression(10))), numberExpression(10));
}

function offsetExpressionAst(expr: ExpressionAst, offset: number): ExpressionAst {
  if (offset === 0) return expr;
  return offset > 0
    ? addExpressions(expr, numberExpression(offset))
    : subtractExpressions(expr, numberExpression(Math.abs(offset)));
}

function sumExpressions(expressions: ExpressionAst[]): ExpressionAst {
  return expressions.reduce((sum, expr) => addExpressions(sum, expr), numberExpression(0));
}

function maxExpressions(expressions: ExpressionAst[]): ExpressionAst {
  return expressions.reduce((best, expr) => maxExpression(best, expr));
}

function spatialHitScratchName(mask: string): string {
  return `${SPATIAL_HIT_SCRATCH_PREFIX}${mask}`;
}

function spatialCountScratchNames(): string[] {
  return [
    `${SPATIAL_COUNT_SCRATCH_PREFIX}total`,
    `${SPATIAL_COUNT_SCRATCH_PREFIX}line`,
    `${SPATIAL_COUNT_SCRATCH_PREFIX}offset`,
    spatialCountCounterScratchName(),
  ];
}

function spatialCountCounterScratchName(): string {
  return `${SPATIAL_COUNT_SCRATCH_PREFIX}counter`;
}

function spatialCountMaskScratchName(): string {
  return `${SPATIAL_COUNT_SCRATCH_PREFIX}mask`;
}

function range(start: number, end: number): number[] {
  const values: number[] = [];
  for (let value = start; value <= end; value += 1) values.push(value);
  return values;
}

function signToggleExpression(current: ExpressionAst, selector: ExpressionAst): ExpressionAst {
  return multiplyExpressions(
    current,
    subtractExpressions(numberExpression(1), multiplyExpressions(numberExpression(2), selector)),
  );
}

function negateExpression(expr: ExpressionAst): ExpressionAst {
  if (expr.kind === "unary") return expr.expr;
  const value = numericLiteralValue(expr);
  if (value !== undefined) return numberExpression(-value);
  return { kind: "unary", op: "-", expr };
}

function matchRemainderByConstant(
  expr: Extract<ExpressionAst, { kind: "binary" }>,
): { value: ExpressionAst; divisor: ExpressionAst } | undefined {
  if (expr.op !== "-") return undefined;
  if (!expressionPureForSubstitution(expr.left)) return undefined;
  const product = expr.right;
  if (product.kind !== "binary" || product.op !== "*") return undefined;
  const leftIntDivide = matchIntDivideByConstant(product.left);
  if (
    leftIntDivide !== undefined &&
    expressionEquals(leftIntDivide.value, expr.left) &&
    expressionEquals(leftIntDivide.divisor, product.right)
  ) {
    return leftIntDivide;
  }
  const rightIntDivide = matchIntDivideByConstant(product.right);
  if (
    rightIntDivide !== undefined &&
    expressionEquals(rightIntDivide.value, expr.left) &&
    expressionEquals(rightIntDivide.divisor, product.left)
  ) {
    return rightIntDivide;
  }
  return undefined;
}

function matchIntDivideByConstant(expr: ExpressionAst): { value: ExpressionAst; divisor: ExpressionAst } | undefined {
  if (expr.kind !== "call" || expr.callee.toLowerCase() !== "int" || expr.args.length !== 1) return undefined;
  const divided = expr.args[0]!;
  if (divided.kind !== "binary" || divided.op !== "/") return undefined;
  if (numericLiteralValue(divided.right) === undefined) return undefined;
  return {
    value: divided.left,
    divisor: divided.right,
  };
}

function numberExpression(value: number): ExpressionAst {
  return { kind: "number", raw: String(value) };
}

function matchTargetPlusDelta(expr: ExpressionAst, target: string): ExpressionAst | undefined {
  if (expr.kind !== "binary" || expr.op !== "+") return undefined;
  const targetExpr: ExpressionAst = { kind: "identifier", name: target };
  if (expressionEquals(expr.left, targetExpr)) return expr.right;
  if (expressionEquals(expr.right, targetExpr)) return expr.left;
  return undefined;
}

function matchTargetMinusDelta(expr: ExpressionAst, target: string): ExpressionAst | undefined {
  if (expr.kind !== "binary" || expr.op !== "-") return undefined;
  const targetExpr: ExpressionAst = { kind: "identifier", name: target };
  if (expressionEquals(expr.left, targetExpr)) return expr.right;
  return undefined;
}

function singleEffectiveAssign(statement: Extract<StatementAst, { kind: "if" }>): Extract<StatementAst, { kind: "assign" }> | undefined {
  if (statement.thenBody.length !== 1) return undefined;
  const thenAssign = statement.thenBody[0];
  if (thenAssign?.kind !== "assign") return undefined;
  if (!statement.elseBody) return thenAssign;
  if (statement.elseBody.length !== 1) return undefined;
  const elseAssign = statement.elseBody[0];
  if (elseAssign?.kind !== "assign") return undefined;
  if (thenAssign.target !== elseAssign.target) return undefined;
  if (isIdentityAssignment(elseAssign)) return thenAssign;
  return undefined;
}

function isIdentityAssignment(statement: Extract<StatementAst, { kind: "assign" }>): boolean {
  return expressionEquals(statement.expr, { kind: "identifier", name: statement.target });
}

function expressionEquals(left: ExpressionAst, right: ExpressionAst): boolean {
  if (left.kind !== right.kind) return false;
  switch (left.kind) {
    case "number":
      return right.kind === "number" && left.raw === right.raw;
    case "identifier":
      return right.kind === "identifier" && left.name === right.name;
    case "unary":
      return right.kind === "unary" && left.op === right.op && expressionEquals(left.expr, right.expr);
    case "binary":
      return right.kind === "binary" &&
        left.op === right.op &&
        expressionEquals(left.left, right.left) &&
        expressionEquals(left.right, right.right);
    case "call":
      return right.kind === "call" &&
        left.callee.toLowerCase() === right.callee.toLowerCase() &&
        left.args.length === right.args.length &&
        left.args.every((arg, index) => expressionEquals(arg, right.args[index]!));
  }
}

// An expression is pure when evaluating it twice yields the same value with no
// observable effect. Calls (random, read, macros) may be non-idempotent or have
// side effects, so they are conservatively impure. Purity makes it sound to
// compute a repeated operand once and duplicate it through the stack.
function isPureExpression(expr: ExpressionAst): boolean {
  switch (expr.kind) {
    case "number":
    case "identifier":
      return true;
    case "unary":
      return isPureExpression(expr.expr);
    case "binary":
      return isPureExpression(expr.left) && isPureExpression(expr.right);
    case "call":
      return false;
  }
}

function matchIntOrFracCall(expr: ExpressionAst): { fn: "int" | "frac"; arg: ExpressionAst } | undefined {
  if (expr.kind !== "call" || expr.args.length !== 1) return undefined;
  const name = expr.callee.toLowerCase();
  if (name !== "int" && name !== "frac") return undefined;
  return { fn: name, arg: expr.args[0]! };
}

function optimizeDispatchDefaultCases(
  statement: Extract<StatementAst, { kind: "dispatch" }>,
): { statement: Extract<StatementAst, { kind: "dispatch" }>; removed: number; reordered: number } {
  if (statement.cases.length === 0) return { statement, removed: 0, reordered: 0 };
  if (!statement.defaultBody) {
    const ordered = orderZeroDispatchCasesFirst(statement.cases);
    return {
      statement: ordered.reordered === 0 ? statement : { ...statement, cases: ordered.cases },
      removed: 0,
      reordered: ordered.reordered,
    };
  }

  const defaultBody = statement.defaultBody;
  const kept: typeof statement.cases = [];
  let removed = 0;
  for (let index = 0; index < statement.cases.length; index += 1) {
    const dispatchCase = statement.cases[index]!;
    const value = numericLiteralValue(dispatchCase.value);
    const laterSameValue = value !== undefined &&
      statement.cases.slice(index + 1).some((laterCase) => numericLiteralValue(laterCase.value) === value);
    if (
      value !== undefined &&
      !laterSameValue &&
      statementListsEqual(dispatchCase.body, defaultBody)
    ) {
      removed += 1;
      continue;
    }
    kept.push(dispatchCase);
  }

  const ordered = orderZeroDispatchCasesFirst(kept);
  const nextStatement = removed === 0 && ordered.reordered === 0
    ? statement
    : { ...statement, cases: ordered.cases };
  return { statement: nextStatement, removed, reordered: ordered.reordered };
}

function orderZeroDispatchCasesFirst(cases: DispatchCaseAst[]): { cases: DispatchCaseAst[]; reordered: number } {
  if (cases.length < 2) return { cases, reordered: 0 };
  const values = cases.map((dispatchCase) => numericLiteralValue(dispatchCase.value));
  if (values.some((value) => value === undefined)) return { cases, reordered: 0 };
  const seen = new Set<number>();
  for (const value of values as number[]) {
    if (seen.has(value)) return { cases, reordered: 0 };
    seen.add(value);
  }
  const zeroIndex = (values as number[]).indexOf(0);
  if (zeroIndex <= 0) return { cases, reordered: 0 };
  const zeroCase = cases[zeroIndex]!;
  return {
    cases: [zeroCase, ...cases.slice(0, zeroIndex), ...cases.slice(zeroIndex + 1)],
    reordered: 1,
  };
}

function statementListsEqual(left: readonly StatementAst[], right: readonly StatementAst[]): boolean {
  return left.length === right.length && left.every((statement, index) => statementEquals(statement, right[index]!));
}

function statementEquals(left: StatementAst, right: StatementAst): boolean {
  if (left.kind !== right.kind) return false;
  switch (left.kind) {
    case "pause":
    case "halt":
      return expressionEquals(left.expr, (right as typeof left).expr);
    case "ask":
      return left.target === (right as typeof left).target &&
        expressionOptionEquals(left.prompt, (right as typeof left).prompt);
    case "input":
      return left.target === (right as typeof left).target;
    case "assign":
      return left.target === (right as typeof left).target && expressionEquals(left.expr, (right as typeof left).expr);
    case "loop":
      return statementListsEqual(left.body, (right as typeof left).body);
    case "if":
      return conditionEquals(left.condition, (right as typeof left).condition) &&
        statementListsEqual(left.thenBody, (right as typeof left).thenBody) &&
        statementListOptionEquals(left.elseBody, (right as typeof left).elseBody);
    case "switch":
      return expressionEquals(left.expr, (right as typeof left).expr) &&
        left.cases.length === (right as typeof left).cases.length &&
        left.cases.every((switchCase, index) =>
          expressionEquals(switchCase.value, (right as typeof left).cases[index]!.value) &&
          statementListsEqual(switchCase.body, (right as typeof left).cases[index]!.body)
        ) &&
        statementListOptionEquals(left.defaultBody, (right as typeof left).defaultBody);
    case "dispatch":
      return expressionEquals(left.expr, (right as typeof left).expr) &&
        left.cases.length === (right as typeof left).cases.length &&
        left.cases.every((dispatchCase, index) =>
          expressionEquals(dispatchCase.value, (right as typeof left).cases[index]!.value) &&
          statementListsEqual(dispatchCase.body, (right as typeof left).cases[index]!.body)
        ) &&
        statementListOptionEquals(left.defaultBody, (right as typeof left).defaultBody);
    case "show":
      return left.display === (right as typeof left).display;
    case "call":
      return left.block === (right as typeof left).block;
    case "core":
      return rawLinesEqual(left.lines, (right as typeof left).lines) &&
        JSON.stringify(left.inputs ?? []) === JSON.stringify((right as typeof left).inputs ?? []) &&
        JSON.stringify(left.outputs ?? []) === JSON.stringify((right as typeof left).outputs ?? []) &&
        JSON.stringify(left.clobbers ?? []) === JSON.stringify((right as typeof left).clobbers ?? []) &&
        JSON.stringify(left.preserves ?? []) === JSON.stringify((right as typeof left).preserves ?? []) &&
        left.strict === (right as typeof left).strict;
    case "egg":
      return rawLinesEqual(left.lines, (right as typeof left).lines);
    case "trap":
      return left.trap === (right as typeof left).trap && expressionEquals(left.expr, (right as typeof left).expr);
  }
}

function expressionOptionEquals(left: ExpressionAst | undefined, right: ExpressionAst | undefined): boolean {
  if (left === undefined || right === undefined) return left === right;
  return expressionEquals(left, right);
}

function statementListOptionEquals(left: StatementAst[] | undefined, right: StatementAst[] | undefined): boolean {
  if (left === undefined || right === undefined) return left === right;
  return statementListsEqual(left, right);
}

function rawLinesEqual(left: readonly { text: string }[], right: readonly { text: string }[]): boolean {
  return left.length === right.length && left.every((line, index) => line.text === right[index]!.text);
}

function countIdentifierReadsInCondition(condition: ConditionAst, name: string): number {
  return countIdentifierReads(condition.left, name) + countIdentifierReads(condition.right, name);
}

function countIdentifierReads(expr: ExpressionAst, name: string): number {
  switch (expr.kind) {
    case "number":
      return 0;
    case "identifier":
      return expr.name === name ? 1 : 0;
    case "unary":
      return countIdentifierReads(expr.expr, name);
    case "binary":
      return countIdentifierReads(expr.left, name) + countIdentifierReads(expr.right, name);
    case "call":
      return expr.args.reduce((sum, arg) => sum + countIdentifierReads(arg, name), 0);
  }
}

function substituteConditionIdentifier(condition: ConditionAst, name: string, replacement: ExpressionAst): ConditionAst {
  return {
    left: substituteExpressionIdentifier(condition.left, name, replacement),
    op: condition.op,
    right: substituteExpressionIdentifier(condition.right, name, replacement),
  };
}

function substituteExpressionIdentifier(expr: ExpressionAst, name: string, replacement: ExpressionAst): ExpressionAst {
  switch (expr.kind) {
    case "number":
      return expr;
    case "identifier":
      return expr.name === name ? replacement : expr;
    case "unary":
      return { ...expr, expr: substituteExpressionIdentifier(expr.expr, name, replacement) };
    case "binary":
      return {
        ...expr,
        left: substituteExpressionIdentifier(expr.left, name, replacement),
        right: substituteExpressionIdentifier(expr.right, name, replacement),
      };
    case "call":
      return { ...expr, args: expr.args.map((arg) => substituteExpressionIdentifier(arg, name, replacement)) };
  }
}

function expressionPureForSubstitution(expr: ExpressionAst): boolean {
  switch (expr.kind) {
    case "number":
    case "identifier":
      return true;
    case "unary":
      return expressionPureForSubstitution(expr.expr);
    case "binary":
      return expressionPureForSubstitution(expr.left) && expressionPureForSubstitution(expr.right);
    case "call": {
      const name = expr.callee.toLowerCase();
      if (name === "random") return false;
      return expr.args.every(expressionPureForSubstitution);
    }
  }
}

function isNumericValue(expr: ExpressionAst, value: number): boolean {
  const parsed = numericLiteralValue(expr);
  return parsed !== undefined && parsed === value;
}

function numericLiteralValue(expr: ExpressionAst): number | undefined {
  if (expr.kind === "unary" && expr.op === "-") {
    const value = numericLiteralValue(expr.expr);
    return value === undefined ? undefined : -value;
  }
  if (expr.kind !== "number") return undefined;
  const value = Number(expr.raw);
  return Number.isFinite(value) ? value : undefined;
}

function estimateOrdinaryIfCost(statement: Extract<StatementAst, { kind: "if" }>, ast: ProgramAst): number {
  const thenStatement = statement.thenBody[0];
  if (statement.thenBody.length !== 1 || !thenStatement) return Number.POSITIVE_INFINITY;
  const thenCost = estimateSimpleStatementCost(thenStatement, ast);
  if (!Number.isFinite(thenCost)) return Number.POSITIVE_INFINITY;
  if (!statement.elseBody) return estimateConditionCost(statement.condition, ast) + thenCost;
  const elseStatement = statement.elseBody[0];
  if (statement.elseBody.length !== 1 || !elseStatement) return Number.POSITIVE_INFINITY;
  const elseCost = estimateSimpleStatementCost(elseStatement, ast);
  if (!Number.isFinite(elseCost)) return Number.POSITIVE_INFINITY;
  return estimateConditionCost(statement.condition, ast) + thenCost + 2 + elseCost;
}

function estimateOrdinaryGuardedUpdateCost(statement: Extract<StatementAst, { kind: "if" }>, ast: ProgramAst): number {
  if (statement.elseBody !== undefined || statement.thenBody.length === 0) return Number.POSITIVE_INFINITY;
  let bodyCost = 0;
  for (const inner of statement.thenBody) {
    const cost = estimateSimpleStatementCost(inner, ast);
    if (!Number.isFinite(cost)) return Number.POSITIVE_INFINITY;
    bodyCost += cost;
  }
  return estimateConditionCost(statement.condition, ast) + bodyCost;
}

function estimateGuardedUpdateSelectorCost(candidate: GuardedUpdateSelectorCandidate, scratch: string): number {
  const selector: ExpressionAst = { kind: "identifier", name: scratch };
  return estimateExpressionCost(candidate.selector) + 1 +
    candidate.updates.reduce(
      (sum, update) => sum + estimateExpressionCost(maskedGuardedUpdateExpression(update, selector)) + 1,
      0,
    );
}

function estimateSimpleStatementCost(statement: StatementAst, ast: ProgramAst): number {
  switch (statement.kind) {
    case "assign":
      return estimateExpressionCost(statement.expr) + 1;
    case "pause":
    case "halt":
      return estimateExpressionCost(statement.expr) + 1;
    case "call": {
      const terminal = effectiveTerminalStatement(statement, ast);
      return terminal === undefined ? Number.POSITIVE_INFINITY : estimateSimpleStatementCost(terminal, ast);
    }
    default:
      return Number.POSITIVE_INFINITY;
  }
}

function estimateConditionCost(
  condition: ConditionAst,
  ast: ProgramAst,
  preloadedConstants?: ReadonlySet<string>,
): number {
  return conditionCompileCost(
    selectCheaperEquivalentCondition(condition, ast, preloadedConstants).condition,
    preloadedConstants,
  );
}

function estimateExpressionCostForCondition(
  expr: ExpressionAst,
  preloadedConstants: ReadonlySet<string> | undefined,
): number {
  if (preloadedConstants === undefined) return estimateExpressionCost(expr);
  if (expr.kind === "number" && preloadedConstants.has(normalizeConstantLiteral(expr.raw))) return 1;
  if (
    expr.kind === "unary" &&
    expr.op === "-" &&
    expr.expr.kind === "number" &&
    preloadedConstants.has(normalizeConstantLiteral(negatedNumberLiteral(expr.expr.raw)))
  ) {
    return 1;
  }
  switch (expr.kind) {
    case "number":
      return estimateNumberCost(expr.raw);
    case "identifier":
      return 1;
    case "unary":
      return estimateExpressionCostForCondition(expr.expr, preloadedConstants) + 1;
    case "binary": {
      const remainder = matchRemainderByConstant(expr);
      if (remainder !== undefined) {
        return estimateExpressionCostForCondition(remainder.value, preloadedConstants) +
          estimateExpressionCostForCondition(remainder.divisor, preloadedConstants) * 2 +
          3;
      }
      return estimateExpressionCostForCondition(expr.left, preloadedConstants) +
        estimateExpressionCostForCondition(expr.right, preloadedConstants) +
        1;
    }
    case "call":
      return estimateCallCostForCondition(expr, preloadedConstants);
  }
}

function estimateCallCostForCondition(
  expr: Extract<ExpressionAst, { kind: "call" }>,
  preloadedConstants: ReadonlySet<string>,
): number {
  const name = expr.callee.toLowerCase();
  if (name === NEGATIVE_ZERO_DEGREE_SELECTOR_GE) {
    return estimateNegativeZeroDegreeSelectorCost(expr, preloadedConstants);
  }
  const macro = ticTacToeExpressionMacro(name, expr.args);
  if (macro !== undefined) return estimateExpressionCostForCondition(macro, preloadedConstants);
  if (name === "random" || name === "pi") return 1;
  if (name === "pow" || ["max", "bit_and", "bit_or", "bit_xor"].includes(name)) {
    return (expr.args[0] ? estimateExpressionCostForCondition(expr.args[0], preloadedConstants) : 0) +
      (expr.args[1] ? estimateExpressionCostForCondition(expr.args[1], preloadedConstants) : 0) +
      1;
  }
  return (expr.args[0] ? estimateExpressionCostForCondition(expr.args[0], preloadedConstants) : 0) + 1;
}

function estimateNegativeZeroDegreeSelectorCost(
  expr: Extract<ExpressionAst, { kind: "call" }>,
  preloadedConstants?: ReadonlySet<string>,
): number {
  if (expr.args.length !== 2 || expr.args[0] === undefined || expr.args[1] === undefined) {
    return Number.POSITIVE_INFINITY;
  }
  return estimateNegativeZeroThresholdRawCost(expr.args[0], expr.args[1], preloadedConstants) + 1;
}

function estimateNegativeZeroThresholdFlowCost(
  threshold: { value: ExpressionAst; bound: number },
  preloadedConstants?: ReadonlySet<string>,
): number {
  return estimateNegativeZeroThresholdRawCost(threshold.value, numberExpression(threshold.bound), preloadedConstants) + 2;
}

function estimateNegativeZeroThresholdRawCost(
  value: ExpressionAst,
  bound: ExpressionAst,
  preloadedConstants?: ReadonlySet<string>,
): number {
  const ratio = divideExpressions(value, bound);
  return estimateExpressionCostForCondition(ratio, preloadedConstants) + 4;
}

function estimateExpressionCost(expr: ExpressionAst): number {
  switch (expr.kind) {
    case "number":
      return estimateNumberCost(expr.raw);
    case "identifier":
      return 1;
    case "unary":
      return estimateExpressionCost(expr.expr) + 1;
    case "binary": {
      const remainder = matchRemainderByConstant(expr);
      if (remainder !== undefined) {
        return estimateExpressionCost(remainder.value) + estimateExpressionCost(remainder.divisor) * 2 + 3;
      }
      return estimateExpressionCost(expr.left) + estimateExpressionCost(expr.right) + 1;
    }
    case "call":
      return estimateCallCost(expr);
  }
}

function estimateCallCost(expr: Extract<ExpressionAst, { kind: "call" }>): number {
  const name = expr.callee.toLowerCase();
  if (name === NEGATIVE_ZERO_DEGREE_SELECTOR_GE) {
    return estimateNegativeZeroDegreeSelectorCost(expr);
  }
  const macro = ticTacToeExpressionMacro(name, expr.args);
  if (macro !== undefined) return estimateExpressionCost(macro);
  if (name === "random" || name === "pi") return 1;
  if (name === "pow") {
    return (expr.args[0] ? estimateExpressionCost(expr.args[0]) : 0) +
      (expr.args[1] ? estimateExpressionCost(expr.args[1]) : 0) +
      1;
  }
  if (["max", "bit_and", "bit_or", "bit_xor"].includes(name)) {
    return (expr.args[0] ? estimateExpressionCost(expr.args[0]) : 0) +
      (expr.args[1] ? estimateExpressionCost(expr.args[1]) : 0) +
      1;
  }
  return (expr.args[0] ? estimateExpressionCost(expr.args[0]) : 0) + 1;
}

function estimateNumberCost(raw: string): number {
  const normalized = raw.trim().toLowerCase();
  const negative = normalized.startsWith("-");
  const unsigned = negative ? normalized.slice(1) : normalized;
  const [mantissa = "0", exponent] = unsigned.split("e");
  let cost = negative ? 1 : 0;
  for (const char of mantissa) {
    if (char === "." || /\d/u.test(char)) cost += 1;
  }
  if (exponent !== undefined) {
    cost += 1;
    if (exponent.startsWith("-")) cost += 1;
    cost += exponent.replace(/^[+-]/u, "").length;
  }
  return cost;
}

function buildIrReport(ast: ProgramAst, items: MachineItem[], steps: number): CompileReport["ir"] {
  return {
    lowered: hasLoweredIr(ast),
    v2: ast.v2 !== undefined,
    intentNodes: countIntentNodes(ast),
    effectOps: items.filter((item) => item.kind !== "label").length,
    layoutCells: steps,
  };
}

function buildOptimizerReport(
  ast: ProgramAst,
  _options: CompileOptions,
  optimizations: AppliedOptimization[],
  candidates: CandidateReport[],
  cellRoles: CellRoleReport[],
  machineProfile: MachineProfile,
): OptimizerReport {
  const activeNames = new Set(optimizations.map((optimization) => optimization.name));
  if (cellRoles.some((cell) => cell.roles.includes("overlay"))) activeNames.add("code-data-overlay");
  if (cellRoles.some((cell) => cell.roles.includes("dark-entry"))) activeNames.add("dark-entry-layout");
  if (cellRoles.some((cell) => cell.roles.includes("display-byte"))) activeNames.add("display-byte-layout");
  if (machineProfile.emulatorFacts.some((fact) => fact.id === "step-vs-run-delta")) {
    activeNames.add("step-vs-run-profile");
  }
  const selectedCandidateVariants = new Set(
    candidates.filter((candidate) => candidate.selected).map((candidate) => candidate.variant),
  );
  const consideredCandidateVariants = new Set(
    candidates.filter((candidate) => !candidate.selected).map((candidate) => candidate.variant),
  );
  const capabilities = optimizerCapabilities.map((capability) => {
    let status: OptimizerCapabilityReport["status"] = capability.planned ? "planned" : "candidate";
    if (capability.activeWhen.some((name) => activeNames.has(name) || selectedCandidateVariants.has(name))) {
      status = "active";
    } else if (capability.activeWhen.some((name) => consideredCandidateVariants.has(name))) {
      status = "considered";
    }
    return {
      id: capability.id,
      category: capability.category,
      source: capability.source,
      status,
      detail: capability.detail,
      requires: capability.requires,
    };
  });
  return {
    automatic: true,
    active: capabilities.filter((capability) => capability.status === "active").length,
    considered: capabilities.filter((capability) => capability.status === "considered").length,
    candidate: capabilities.filter((capability) => capability.status === "candidate").length,
    planned: capabilities.filter((capability) => capability.status === "planned").length,
    capabilities,
  };
}

const optimizerCapabilities: Array<{
  id: string;
  category: OptimizerCapabilityReport["category"];
  source: OptimizerCapabilityReport["source"];
  requires: string[];
  activeWhen: string[];
  planned?: boolean;
  detail: string;
}> = [
  {
    id: "store-recall-peephole",
    category: "stack",
    source: "documented",
    requires: [],
    activeWhen: ["store-recall-peephole"],
    detail: "Elides immediate X->П r / П->X r pairs when no exact-machine effect is crossed.",
  },
  {
    id: "stack-current-x-scheduling",
    category: "stack",
    source: "documented",
    requires: [],
    activeWhen: ["stack-current-x-scheduling", "dead-temp-store"],
    detail: "Keeps a just-computed value in X for a following commutative use and removes the temporary store after data-flow proof.",
  },
  {
    id: "tail-call-lowering",
    category: "flow",
    source: "documented",
    requires: [],
    activeWhen: [
      "tail-call-lowering",
      "terminal-rule-tail-call",
      "tail-call-layout",
      "terminal-if-direct-branch",
      "terminal-branch-end-elision",
      "local-terminal-tail",
      "local-terminal-tail-branch",
      "late-layout-if-variant",
    ],
    detail: "Replaces subroutine calls in tail position with direct jumps so rule factoring and terminal branches do not force extra returns or branch hops.",
  },
  {
    id: "return-zero-jump",
    category: "flow",
    source: "mk61-delta",
    requires: [],
    activeWhen: ["return-zero-jump"],
    detail: "Uses В/О as one-cell БП 01 only when the return stack is known empty.",
  },
  {
    id: "vo-return-body-reorder",
    category: "flow",
    source: "mk61-delta",
    requires: [],
    activeWhen: ["vo-return-body-reorder"],
    detail: "Candidate: reorder HEAD/MAIN/SUB so a subroutine return lands at the program head and a ПП/В/О pair collapses. Single-use procedures are already inlined and tail-position calls already become direct jumps, so the remaining gain needs a global layout search with no proven safe trigger on the current programs.",
  },
  {
    id: "super-number-deferred-normalization",
    category: "data",
    source: "undocumented",
    requires: [],
    activeWhen: ["super-number-deferred-normalization"],
    detail: "Candidate: keep an un-normalized super-number (extra hidden mantissa digits) to defer a range/error check. The reference emulator models only the 8 visible mantissa digits, so the hidden-digit behavior the idiom relies on cannot be reproduced or safely proven, and no language construct produces it.",
  },
  {
    id: "packed-position-type",
    category: "data",
    source: "undocumented",
    requires: ["constants-dual-use"],
    activeWhen: ["packed-position-type"],
    detail: "Candidate: a language-level packed position type N.0000H carrying an integer position plus packed (hex) fractional sub-coordinates. The dual-use round-trip (К [x] recovers the position, К {x} recovers the packed fraction) is already exploited by constants-dual-use; a dedicated type would only add language surface that no current program consumes.",
  },
  {
    id: "dual-constant-sign-digit",
    category: "data",
    source: "undocumented",
    requires: ["negative-zero-threshold-selector"],
    activeWhen: ["dual-constant-sign-digit"],
    detail: "Candidate: a language intent for a dual constant 1.|-00 plus compact sign-digits (2N..9N) and their comparison semantics. The negative-zero comparison behavior is already active through the negative-zero-threshold family; no current program needs the additional sign-digit literal surface.",
  },
  {
    id: "branch-removal",
    category: "flow",
    source: "documented",
    requires: [],
    activeWhen: [
      "branch-removal",
      "kmax-zero-through",
      "kzn-double",
      "kor-digit-test",
    ],
    detail: "Umbrella rule for replacing provably equivalent conditionals with branchless arithmetic, sign, extrema, or masked updates.",
  },
  {
    id: "zero-condition-test",
    category: "flow",
    source: "documented",
    requires: [],
    activeWhen: [
      "zero-condition-test",
      "fractional-indirect-addressing",
      "kor-digit-test",
    ],
    detail: "Uses direct F x?0 tests when one side of a condition is a proved zero, avoiding a zero literal and subtraction.",
  },
  {
    id: "dispatch-compare-chain",
    category: "flow",
    source: "documented",
    requires: [],
    activeWhen: [
      "fallthrough-compare-chain",
      "dispatch-lowering",
      "dispatch-default-merge",
      "terminal-display-fusion",
      "super-dark-dispatch",
      "indirect-register-flow",
    ],
    detail: "Lowers high-level command dispatch automatically; small proven dispatches may use indirect or super-dark dispatch.",
  },
  {
    id: "shared-challenge-effect-lowering",
    category: "data",
    source: "documented",
    requires: [],
    activeWhen: ["shared-challenge-effect-lowering"],
    detail: "Recognizes repeated challenge protocols whose branches differ by numeric state deltas and common cell updates, then lowers them as one shared formula-driven effect table.",
  },
  {
    id: "arithmetic-if-select",
    category: "flow",
    source: "documented",
    requires: [],
    activeWhen: [
      "arithmetic-if-select",
      "arithmetic-if-terminal-select",
      "arithmetic-if-conditional-move",
      "negative-zero-threshold-select",
      "negative-zero-threshold-terminal-select",
      "negative-zero-threshold-selector",
      "kmax-zero-through",
      "kzn-double",
    ],
    detail: "Replaces simple boolean if/else assignments, stops, and conditional moves with arithmetic selection when shorter.",
  },
  {
    id: "negative-zero-threshold-selector",
    category: "flow",
    source: "undocumented",
    requires: ["negative-zero-degree", "x2-register"],
    activeWhen: [
      "negative-zero-threshold-selector",
      "negative-zero-threshold-select",
      "negative-zero-threshold-terminal-select",
      "negative-zero-threshold-flow",
      "negative-zero-threshold-update",
    ],
    detail: "Uses a compiler-owned 1|-00 preload plus В↑ normalization to build a 0/1 selector or flow test for bounded nonnegative threshold branches when that beats ordinary branching.",
  },
  {
    id: "arithmetic-if-update",
    category: "flow",
    source: "documented",
    requires: [],
    activeWhen: [
      "arithmetic-if-update",
      "arithmetic-if-sign-toggle",
      "multi-guarded-update",
      "negative-zero-threshold-update",
      "hex-mantissa-arithmetic",
    ],
    detail: "Replaces conditional +=/-= and sign toggles guarded by a proved boolean with masked arithmetic.",
  },
  {
    id: "arithmetic-if-extrema",
    category: "flow",
    source: "documented",
    requires: [],
    activeWhen: [
      "arithmetic-if-max",
      "arithmetic-if-min",
      "arithmetic-if-abs",
      "arithmetic-if-double-clamp",
      "arithmetic-if-comparison-mask",
      "arithmetic-if-boolean-algebra",
      "kmax-zero-through",
    ],
    detail: "Replaces simple extrema, sign, clamp, comparison-mask, and boolean-algebra branches when shorter. К max zero-through counts as the extrema selection.",
  },
  {
    id: "indirect-flow",
    category: "flow",
    source: "documented",
    requires: [],
    activeWhen: ["indirect-register-flow", "stable-indirect-flow", "preloaded-indirect-flow", "preloaded-super-dark-flow"],
    detail: "Candidate rule: replace direct branches/calls with К БП/К ПП/К x?0 when the address value is already live, or when a compiler-owned preload is cheaper.",
  },
  {
    id: "indirect-memory-table",
    category: "data",
    source: "documented",
    requires: ["indirect-memory"],
    activeWhen: ["indirect-memory-table"],
    detail: "Rewrites direct register memory access through К П->X/К X->П when a stable selector already proves the target register.",
  },
  {
    id: "fl-decrement-branch",
    category: "flow",
    source: "documented",
    requires: [],
    activeWhen: ["fl-unit-decrement", "r0-indirect-counter"],
    detail: "Uses F L0..F L3 as compact decrement-and-continue/decrement-and-branch forms for small counters.",
  },
  {
    id: "address-constant-overlay",
    category: "layout",
    source: "undocumented",
    requires: ["address-constants", "code-data-overlay"],
    activeWhen: ["code-data-overlay"],
    detail: "Lets branch operands double as constants or executable bytes after the layout pass marks a conflict-free overlay role.",
  },
  {
    id: "cyclic-address-layout",
    category: "layout",
    source: "undocumented",
    requires: ["dark-entries", "code-data-overlay"],
    activeWhen: ["cyclic-address-layout"],
    detail: "Uses formal address-space wraparound and side branches only after the layout pass proves the shared-tail target.",
  },
  {
    id: "constants-dual-use",
    category: "data",
    source: "undocumented",
    requires: ["address-constants"],
    activeWhen: ["constants-dual-use"],
    detail: "Reuses one stored value as arithmetic coefficient, rounding adjuster, and address-like dispatch data.",
  },
  {
    id: "dark-entry-layout",
    category: "layout",
    source: "undocumented",
    requires: ["dark-entries"],
    activeWhen: ["dark-entry-layout"],
    detail: "Exposes shared tails as dark-entry candidates when the layout pass can point at the same executable suffix.",
  },
  {
    id: "super-dark-dispatch",
    category: "flow",
    source: "undocumented",
    requires: ["super-dark-dispatch", "indirect-flow"],
    activeWhen: ["super-dark-dispatch", "preloaded-super-dark-flow"],
    detail: "Dispatch candidate for indirect К БП R with FA..FF; selected only when layout can place one-command cases at 48..53, tails at 01..06, and prove the selector register contains FA..FF.",
  },
  {
    id: "r0-alias-indirect",
    category: "flow",
    source: "mk61-delta",
    requires: ["undocumented-opcodes", "r0-t-alias"],
    activeWhen: ["r0-indirect-counter"],
    detail: "Treats MK-61 *F/R0 aliases as byte/formal-address candidates only; the profile proves they transform R0.",
  },
  {
    id: "r0-fractional-sentinel",
    category: "flow",
    source: "mk61-delta",
    requires: ["r0-fractional-sentinel"],
    activeWhen: ["fractional-indirect-addressing", "r0-indirect-counter", "r0-fractional-sentinel"],
    detail: "Computed-dispatch candidate for fractional R0 selecting R3 or jumping to 99 while creating the -99999999 sentinel.",
  },
  {
    id: "raw-display-5f",
    category: "display",
    source: "undocumented",
    requires: ["raw-display-5f"],
    activeWhen: [],
    detail: "Display lowering candidate for opcode 5F; selected only when the raw display mutation is the intended observable effect.",
  },
  {
    id: "x2-display-register",
    category: "display",
    source: "mk61-delta",
    requires: ["x2-register", "display-bytes"],
    activeWhen: ["x2-display-byte-scheduling", "display-byte-layout"],
    detail: "Display/data candidate for scheduling X2, ВП, '.', sign digits, and display bytes without extra storage.",
  },
  {
    id: "vp-fraction-restore",
    category: "display",
    source: "mk61-delta",
    requires: ["x2-register", "display-bytes"],
    activeWhen: ["vp-fraction-restore", "vp-exponent-splice"],
    detail: "Uses ВП where it simultaneously restores X2 and provides the needed fractional/mantissa side effect, and collapses redundant ВП ВП / КНОП ВП exponent-entry splices.",
  },
  {
    id: "hex-mantissa-arithmetic",
    category: "data",
    source: "undocumented",
    requires: ["display-bytes"],
    activeWhen: ["hex-mantissa-arithmetic"],
    detail: "Represents compact state as hexadecimal mantissa/sign digits when display-boundary proofs hold.",
  },
  {
    id: "fractional-indirect-addressing",
    category: "data",
    source: "mk61-delta",
    requires: ["indirect-flow"],
    activeWhen: ["fractional-indirect-addressing"],
    detail: "Uses indirect addressing truncation/fractional effects as data selection only with range and emulator facts.",
  },
  {
    id: "kzn-double",
    category: "data",
    source: "documented",
    requires: [],
    activeWhen: ["kzn-double"],
    detail: "Uses К ЗН as a one-cell numeric transform when equivalent to the needed doubling/sign-digit operation.",
  },
  {
    id: "kor-digit-test",
    category: "data",
    source: "documented",
    requires: [],
    activeWhen: ["kor-digit-test"],
    detail: "Uses К∨ as a compact digit/boundary test when bit-level equivalence is proved.",
  },
  {
    id: "kmax-zero-through",
    category: "stack",
    source: "documented",
    requires: [],
    activeWhen: ["kmax-zero-through"],
    detail: "Uses К max as a stack/value transform, including zero-through cases, when it preserves source semantics.",
  },
  {
    id: "error-stop-idiom",
    category: "trap",
    source: "mk61-delta",
    requires: ["error-stops"],
    activeWhen: ["error-stop"],
    detail: "Use domain-error stops only for explicit trap intent or after verifier proves the failure mode is acceptable.",
  },
  {
    id: "step-vs-run-verification",
    category: "verification",
    source: "mk61-delta",
    requires: [],
    activeWhen: ["step-vs-run-profile"],
    detail: "Uses mk61 emulator facts for Danilov-era differences between step mode, continuous run, exponent sign changes, Cx, В↑, and П->X as exact-machine preconditions.",
  },
  {
    id: "jump-to-next-threading",
    category: "flow",
    source: "documented",
    requires: [],
    activeWhen: ["jump-to-next-threading"],
    detail: "Drops unconditional БП whose only target is the immediately following label after a layout pass collapses the trampoline.",
  },
  {
    id: "duplicate-failure-tail-merge",
    category: "flow",
    source: "documented",
    requires: [],
    activeWhen: ["duplicate-failure-tail-merge"],
    detail: "Coalesces structurally identical pause-0 failure tails into a single shared exit, removing the trampoline cells between them.",
  },
  {
    id: "subroutine-part-shared-tail",
    category: "flow",
    source: "undocumented",
    requires: [],
    activeWhen: ["int-frac-shared-tail"],
    detail: "Computes a shared pure operand once and derives both its integer (К [x]) and fractional (К {x}) parts through a single В↑ / X↔Y stack tail instead of recomputing the operand for each part.",
  },
  {
    id: "liveness-analysis",
    category: "verification",
    source: "documented",
    requires: [],
    activeWhen: ["liveness-analysis"],
    detail: "Foundational data-flow pass: computes liveIn/liveOut per IR position so dead-store-elimination, register-coalesce and other proof-backed passes can fire.",
  },
  {
    id: "dead-store-elimination",
    category: "stack",
    source: "documented",
    requires: [],
    activeWhen: ["dead-store-elimination"],
    detail: "Removes X->П r writes whose register is never read again before the next write to the same register, using whole-program liveness.",
  },
  {
    id: "last-x-reuse",
    category: "stack",
    source: "documented",
    requires: [],
    activeWhen: ["last-x-reuse"],
    detail: "Drops П->X r when the IR pass can prove X already holds the value just stored to r and no intervening op clobbers X (including С/П, jumps, ALU).",
  },
  {
    id: "constant-folding",
    category: "data",
    source: "documented",
    requires: [],
    activeWhen: ["expression-constant-folder", "constant-folding"],
    detail: "Folds numeric source-expression subtrees before code generation and eliminates identity arithmetic such as '0 +' and '1 *' from the IR after upstream passes simplify the expression tree.",
  },
  {
    id: "cse-display-block",
    category: "data",
    source: "documented",
    requires: [],
    activeWhen: ["cse-display-block"],
    detail: "Common subexpression elimination for pure recall/ALU blocks ending in В/О; redirects duplicates to a shared exit when profitable.",
  },
  {
    id: "jump-thread",
    category: "flow",
    source: "documented",
    requires: [],
    activeWhen: ["jump-thread"],
    detail: "Threads jump-to-jump chains through trampoline labels to their final target, freeing intermediate cells.",
  },
  {
    id: "dead-code-after-halt",
    category: "flow",
    source: "documented",
    requires: [],
    activeWhen: ["dead-code-after-halt"],
    detail: "CFG analysis from the entry point removes ops that are unreachable through any combination of fall-through and jump edges.",
  },
  {
    id: "register-coalesce",
    category: "stack",
    source: "documented",
    requires: [],
    activeWhen: ["register-coalesce"],
    detail: "Coalesces non-overlapping direct-register live ranges after data-flow proves neither register is externally live, indirect, or a loop counter.",
  },
  {
    id: "arithmetic-if-pass",
    category: "flow",
    source: "documented",
    requires: [],
    activeWhen: ["arithmetic-if-pass"],
    detail: "IR-level seat for branchless arithmetic-if rewriting; current size-gated rewriting still happens at the AST select stage and feeds this pass via the candidate ledger.",
  },
  {
    id: "redundant-prologue-elimination",
    category: "flow",
    source: "documented",
    requires: [],
    activeWhen: ["redundant-prologue-elimination"],
    detail: "Removes a display/halt prologue that immediately precedes БП to a label whose forward prologue is byte-identical, since the user only ever observes the one display performed by the loop head.",
  },
];

function buildPreloadReport(ast: ProgramAst, allocation: RegisterAllocation): PreloadReport[] {
  const explicit = ast.preloads.map((preload) => ({
    register: preload.register,
    value: preload.value,
    countsAgainstProgram: false,
  }));
  const synthetic: PreloadReport[] = [];
  if (ast.v2) {
    for (const field of ast.v2.state) {
      const register = allocation.registers[field.name];
      if (!register) continue;
      const value = field.initial;
      if (value === undefined) continue;
      synthetic.push({
        register,
        value,
        countsAgainstProgram: false,
      });
    }
  }
  const constants = Object.entries(allocation.constants).map(([value, register]) => ({
    register,
    value,
    countsAgainstProgram: false,
  }));
  return [...explicit, ...synthetic, ...constants];
}

function buildNegativeZeroDegreePreloadReport(
  allocation: RegisterAllocation,
  optimizations: readonly AppliedOptimization[],
): PreloadReport[] {
  if (allocation.negativeZeroDegree === undefined) return [];
  if (!optimizations.some((optimization) => optimization.name === "negative-zero-threshold-selector")) return [];
  return [{
    register: allocation.negativeZeroDegree,
    value: NEGATIVE_ZERO_DEGREE_PRELOAD_VALUE,
    countsAgainstProgram: false,
    setupProgram: negativeZeroDegreeSetupProgramText(allocation.negativeZeroDegree),
    setupNote: `Run this setup program once before loading the main program; it leaves ${NEGATIVE_ZERO_DEGREE_PRELOAD_VALUE} in R${allocation.negativeZeroDegree}.`,
  }];
}

function negativeZeroDegreeSetupProgramText(register: RegisterName): string {
  const registerOpcode = (0x40 + registerIndex(register)).toString(16).toUpperCase().padStart(2, "0");
  return [
    "54",
    "01",
    "03",
    registerOpcode,
    "01",
    "08",
    "38",
    "35",
    "0B",
    "0C",
    "02",
    "15",
    "0E",
    "0C",
    "0B",
    "05",
    "00",
    registerOpcode,
    "50",
  ].join(" ");
}

function buildBudgetReport(used: number, limit: number, largestBlocks: string[], extraCells: number): BudgetReport {
  return {
    used,
    limit,
    remaining: limit - used,
    exceeded: used > limit,
    largestBlocks,
    officialSteps: used,
    extraCells,
    totalPhysicalCells: used + extraCells,
  };
}

function buildMachineFeaturesUsed(
  machineProfile: MachineProfile,
  optimizations: AppliedOptimization[],
  cellRoles: CellRoleReport[],
  candidates: CandidateReport[],
): MachineFeatureUseReport[] {
  const used = new Map<string, MachineFeatureUseReport>();
  const add = (id: string, detail: string, source: MachineFeatureUseReport["source"]): void => {
    const targetDetail = machineProfile.features.find((feature) => feature.id === id)?.detail;
    used.set(id, {
      id,
      source,
      detail: targetDetail ? `${detail}; ${targetDetail}` : detail,
    });
  };
  if (optimizations.some((optimization) => optimization.name === "return-zero-jump")) {
    add("return-empty-stack-jump", "Optimizer selected В/О as one-cell БП 01.", "optimizer");
  }
  if (optimizations.some((optimization) => optimization.name === "branch-removal")) {
    add("branch-removal", "Optimizer removed a conditional branch through a proved branchless equivalent.", "optimizer");
  }
  if (optimizations.some((optimization) => optimization.name === "fl-unit-decrement")) {
    add("fl-decrement-branch", "Optimizer selected F L0..F L3 for a unit decrement.", "optimizer");
  }
  if (
    optimizations.some((optimization) =>
      optimization.name === "indirect-register-flow" ||
      optimization.name === "stable-indirect-flow" ||
      optimization.name === "preloaded-indirect-flow" ||
      optimization.name === "preloaded-super-dark-flow"
    )
  ) {
    add("indirect-flow", "Optimizer selected register-held branch addresses for one-cell indirect flow.", "optimizer");
  }
  if (optimizations.some((optimization) => optimization.name === "indirect-memory-table")) {
    add("indirect-memory", "Optimizer reused a stable selector for one-cell indirect memory access.", "optimizer");
  }
  if (optimizations.some((optimization) => optimization.name === "cyclic-address-layout")) {
    add("dark-entries", "Optimizer selected formal/dark entry points inside a cyclic shared-tail layout.", "layout");
  }
  if (optimizations.some((optimization) => optimization.name === "constants-dual-use")) {
    add("address-constants", "Optimizer reused constants as arithmetic values and address-like data.", "optimizer");
  }
  if (optimizations.some((optimization) => optimization.name === "x2-display-byte-scheduling")) {
    add("x2-register", "Optimizer scheduled hidden X2 values across display-byte boundaries.", "optimizer");
  }
  if (optimizations.some((optimization) => optimization.name === "negative-zero-threshold-selector")) {
    add("negative-zero-degree", "Optimizer selected a preloaded negative-zero exponent threshold selector.", "optimizer");
  }
  if (optimizations.some((optimization) => optimization.name === "vp-fraction-restore")) {
    add("x2-restore-boundaries", "Optimizer used ВП as both X2 restoration and fractional/mantissa transform.", "optimizer");
  }
  if (optimizations.some((optimization) => optimization.name === "hex-mantissa-arithmetic")) {
    add("display-bytes", "Optimizer packed state into hexadecimal mantissa/display-byte forms.", "optimizer");
  }
  if (optimizations.some((optimization) => optimization.name === "fractional-indirect-addressing" || optimization.name === "r0-fractional-sentinel")) {
    add("r0-fractional-sentinel", "Optimizer used fractional/indirect addressing side effects under emulator-proved semantics.", "optimizer");
  }
  if (optimizations.some((optimization) => optimization.name === "r0-indirect-counter")) {
    add("r0-t-alias", "Optimizer used R0 indirect behavior with explicit R0 transformation accounted for.", "optimizer");
  }
  if (optimizations.some((optimization) => optimization.name === "error-stop")) {
    add("error-stops", "Compiler emitted a domain-error stop for explicit trap semantics.", "optimizer");
  }
  if (cellRoles.some((cell) => cell.roles.includes("overlay"))) {
    add("code-data-overlay", "Layout marked address cells as reusable code/data overlay candidates.", "layout");
  }
  if (cellRoles.some((cell) => cell.roles.includes("dark-entry"))) {
    const formal = cellRoles.some((cell) => cell.roles.includes("formal-address"));
    add(
      "dark-entries",
      formal
        ? "Layout emitted formal/dark MK-61 address operand(s)."
        : "Layout exposed shared tails as dark-entry candidates.",
      "layout",
    );
  }
  if (cellRoles.some((cell) => cell.roles.includes("display-byte"))) {
    add("display-bytes", "Display lowering marked cells as packed display-byte candidates.", "layout");
  }
  if (cellRoles.some((cell) => cell.roles.includes("address") && cell.roles.includes("constant"))) {
    add("address-constants", "Layout marked address cells as reusable constants.", "layout");
  }
  if (
    candidates.some((candidate) =>
      (candidate.variant === "super-dark-dispatch" || candidate.variant === "preloaded-super-dark-flow") &&
      candidate.selected
    )
  ) {
    add("super-dark-dispatch", "Optimizer selected FA..FF indirect one-command cases.", "optimizer");
  }
  return [...used.values()];
}

function machineItemsToLayoutCells(items: readonly MachineItem[]): LayoutIrCell[] {
  const cells: LayoutIrCell[] = [];
  let address = 0;
  for (const item of items) {
    if (item.kind === "label") continue;
    if (item.kind === "op") {
      cells.push({
        address,
        opcode: item.opcode,
        roles: ["exec"],
        tactic: item.comment ?? "",
      });
      address += 1;
      continue;
    }
    cells.push({
      address,
      opcode: item.formalOpcode ?? (typeof item.target === "number" ? item.target : 0),
      roles: ["address"],
      tactic: item.comment ?? "",
    });
    address += 1;
  }
  return cells;
}

function buildProofReport(
  ast: ProgramAst,
  items: MachineItem[],
  cellRoles: CellRoleReport[],
  _options: CompileOptions,
  optimizations: AppliedOptimization[],
  preloads: readonly PreloadReport[],
): CompileReport["proofs"] {
  const proofs: CompileReport["proofs"] = [];
  const usesSubroutine = items.some((item) => item.kind === "op" && item.opcode === 0x53);
  proofs.push({
    id: "return-stack-empty",
    status: usesSubroutine ? "not-needed" : "proved",
    detail: usesSubroutine
      ? "Program uses subroutine calls, so В/О-as-jump is not assumed globally."
      : "No ПП opcodes were emitted; empty return stack precondition holds for В/О-as-БП01 rewrites.",
  });
  if (optimizations.some((optimization) => optimization.name === "branch-removal")) {
    const variants = [
      ...new Set(
        optimizations
          .filter((optimization) =>
            optimization.name.startsWith("arithmetic-if-") ||
            optimization.name.startsWith("negative-zero-threshold-")
          )
          .map((optimization) => optimization.name),
      ),
    ];
    proofs.push({
      id: "branch-equivalence",
      status: "proved",
      detail: `Removed conditional branches via ${variants.join(", ")} after matching assignment/update shape and value ranges.`,
    });
  }
  if (optimizations.some((optimization) => optimization.name === "negative-zero-threshold-selector")) {
    proofs.push({
      id: "negative-zero-threshold-selector",
      status: "proved",
      detail: "Selected only for bounded integer nonnegative thresholds; В↑ normalizes the underflowed 1|-00 product before К ЗН turns it into a 0/1 selector.",
    });
  }
  if (
    optimizations.some((optimization) =>
      optimization.name === "stable-indirect-flow" ||
      optimization.name === "preloaded-indirect-flow" ||
      optimization.name === "preloaded-super-dark-flow" ||
      optimization.name === "indirect-memory-table" ||
      optimization.name === "r0-fractional-sentinel"
    )
  ) {
    proofs.push({
      id: "indirect-addressing-ranges",
      status: "proved",
      detail: "Indirect selectors are rewritten only after local data-flow proves a stable target, a compiler-owned address preload, or the fractional R0 sentinel shape.",
    });
  }
  if (optimizations.some((optimization) => optimization.name === "preloaded-super-dark-flow")) {
    const proof = verifySuperDarkSuffixLayout(machineItemsToLayoutCells(items), {
      selectorValues: superDarkSelectorValues(preloads),
    });
    proofs.push({
      id: "super-dark-suffix-layout",
      status: proof.proved ? "proved" : "assumed",
      detail: proof.proved
        ? `Compiler-owned FA..FF dispatch entries land on physical 48..53 and resume through proved 01..06 continuations (${proof.pairs.length} pair${proof.pairs.length === 1 ? "" : "s"}).`
        : `Compiler selected preloaded super-dark flow, but the final layout proof is incomplete: ${proof.reasons.join("; ")}.`,
    });
  }
  if (ast.v2) {
    const ranged = ast.v2.state.filter((field) => field.min !== undefined || field.max !== undefined);
    if (ranged.length > 0) {
      proofs.push({
        id: "value-ranges",
        status: "proved",
        detail: `Collected source ranges for ${ranged.map((field) => field.name).join(", ")}.`,
      });
    }
    const observed = ast.v2.screens.flatMap((screen) => screen.sources);
    if (observed.length > 0) {
      proofs.push({
        id: "observability",
        status: "proved",
        detail: `Visible state is limited by screen declarations: ${[...new Set(observed)].join(", ")}.`,
      });
    }
  }
  if (cellRoles.some((cell) => cell.roles.includes("display-byte"))) {
    proofs.push({
      id: "display-byte-observable-boundary",
      status: "assumed",
      detail: "Display-byte candidates are bounded by screen declarations and the exact mk61 profile.",
    });
  }
  const formalOperands = items
    .filter((item): item is Extract<MachineItem, { kind: "address" }> => item.kind === "address" && item.formalOpcode !== undefined)
    .map((item) => formalAddressInfo(item.formalOpcode!))
    .filter((info) => info.kind !== "official");
  if (formalOperands.length > 0) {
    proofs.push({
      id: "formal-address-operands",
      status: "proved",
      detail: `Resolved formal MK-61 address byte(s): ${
        formalOperands.map((info) => `${info.label}->${safeFormatAddress(info.actual)}`).join(", ")
      }.`,
    });
  }
  return proofs;
}

function superDarkSelectorValues(preloads: readonly PreloadReport[]): Record<string, string> {
  const selectors: Record<string, string> = {};
  for (const preload of preloads) selectors[preload.register] = preload.value;
  return selectors;
}

function parseHotBlock(text: string): CompileReport["hotBlocks"][number] {
  const match = /^(.+)=(\d+)$/u.exec(text);
  if (!match) return { name: text, estimatedCells: 0 };
  return { name: match[1]!, estimatedCells: Number(match[2]) };
}

function hasLoweredIr(ast: ProgramAst): boolean {
  return (
    ast.preloads.length > 0 ||
    ast.domains.length > 0 ||
    ast.states.length > 0 ||
    ast.displays.length > 0 ||
    ast.blocks.length > 0 ||
    ast.entries.some((entry) => containsLoweredStatement(entry.body)) ||
    ast.procs.some((proc) => containsLoweredStatement(proc.body))
  );
}

function containsLoweredStatement(statements: StatementAst[]): boolean {
  for (const statement of statements) {
    if (
      statement.kind === "input" ||
      statement.kind === "dispatch" ||
      statement.kind === "show" ||
      statement.kind === "call"
    ) {
      return true;
    }
    if (statement.kind === "loop" && containsLoweredStatement(statement.body)) return true;
    if (statement.kind === "if") {
      if (containsLoweredStatement(statement.thenBody)) return true;
      if (statement.elseBody && containsLoweredStatement(statement.elseBody)) return true;
    }
    if (statement.kind === "switch") {
      if (statement.cases.some((switchCase) => containsLoweredStatement(switchCase.body))) return true;
      if (statement.defaultBody && containsLoweredStatement(statement.defaultBody)) return true;
    }
  }
  return false;
}

function countIntentNodes(ast: ProgramAst): number {
  return (
    countV2IntentNodes(ast) +
    ast.preloads.length +
    ast.domains.reduce((sum, domain) => sum + 1 + domain.lines.length, 0) +
    ast.states.reduce((sum, state) => sum + 1 + state.fields.length, 0) +
    ast.displays.length +
    ast.blocks.reduce((sum, block) => sum + 1 + countStatements(block.body), 0) +
    ast.entries.reduce((sum, entry) => sum + countStatements(entry.body), 0) +
    ast.procs.reduce((sum, proc) => sum + countStatements(proc.body), 0)
  );
}

function countV2IntentNodes(ast: ProgramAst): number {
  const v2 = ast.v2;
  if (!v2) return 0;
  return (
    1 +
    v2.state.length +
    v2.screens.length +
    (v2.turn ? 1 + countV2Statements(v2.turn.body) : 0) +
    v2.rules.reduce((sum, rule) => sum + 1 + countV2Statements(rule.body), 0)
  );
}

function countV2Statements(statements: V2StatementAst[]): number {
  let count = 0;
  for (const statement of statements) {
    count += 1;
    if (statement.kind === "v2_match") {
      count += statement.cases.length;
      for (const matchCase of statement.cases) count += countV2Statements([matchCase.action]);
      if (statement.otherwise) count += countV2Statements([statement.otherwise]);
    }
    if (statement.kind === "v2_if") {
      count += countV2Statements(statement.thenBody);
      if (statement.elseBody) count += countV2Statements(statement.elseBody);
    }
    if (statement.kind === "v2_challenge") {
      count += countV2Statements(statement.successBody);
      if (statement.failureBody) count += countV2Statements(statement.failureBody);
    }
    if (statement.kind === "v2_raw") {
      count += statement.inputs.length + statement.outputs.length + statement.lines.length;
    }
  }
  return count;
}

function countStatements(statements: StatementAst[]): number {
  let count = 0;
  for (const statement of statements) {
    count += 1;
    if (statement.kind === "loop") count += countStatements(statement.body);
    if (statement.kind === "if") {
      count += countStatements(statement.thenBody);
      if (statement.elseBody) count += countStatements(statement.elseBody);
    }
    if (statement.kind === "switch") {
      count += statement.cases.reduce((sum, switchCase) => sum + countStatements(switchCase.body), 0);
      if (statement.defaultBody) count += countStatements(statement.defaultBody);
    }
    if (statement.kind === "dispatch") {
      count += statement.cases.reduce((sum, dispatchCase) => sum + countStatements(dispatchCase.body), 0);
      if (statement.defaultBody) count += countStatements(statement.defaultBody);
    }
  }
  return count;
}

function selectDispatchCandidate(
  statement: Extract<StatementAst, { kind: "dispatch" }>,
  machineProfile: MachineProfile,
): { selected: CandidateReport; candidates: CandidateReport[] } {
  const site = statement.name ?? `dispatch@${statement.line}`;
  const fallthroughCost = estimateDispatchCost(statement, true);
  const candidates: CandidateReport[] = [
    {
      site,
      variant: "fallthrough-compare-chain",
      steps: fallthroughCost,
      selected: true,
      reason: "uses case ordering; key-based dispatch does not already provide an address-valued selector",
    },
  ];

  if (machineSupports(machineProfile, "indirect-flow")) {
    candidates.push({
      site,
      variant: "indirect-register-flow",
      steps: Math.max(fallthroughCost, statement.cases.length + 3),
      selected: false,
      reason: "rejected; selector is key-valued, not address-valued, and building an address register would not beat the compare-chain",
    });
  }

  if (
    machineSupports(machineProfile, "dark-entries") &&
    machineSupports(machineProfile, "address-constants") &&
    machineSupports(machineProfile, "code-data-overlay")
  ) {
    candidates.push({
      site,
      variant: "dark-indirect-table",
      steps: Math.max(4, statement.cases.length + 3),
      selected: false,
      reason: "considered; layout proof did not establish a conflict-free address/data table for this site",
    });
  }

  if (
    statement.cases.length <= 6 &&
    machineSupports(machineProfile, "super-dark-dispatch") &&
    machineSupports(machineProfile, "indirect-flow")
  ) {
    candidates.push({
      site,
      variant: "super-dark-dispatch",
      steps: Math.max(3, statement.cases.length + 2),
      selected: false,
      reason: "considered; selector is key-valued, and layout proof did not place one-command cases at 48..53 with tails at 01..06",
    });
  }

  const selected = candidates.find((candidate) => candidate.selected) ?? candidates[0]!;
  return { selected, candidates };
}

function estimateDispatchCost(
  statement: Extract<StatementAst, { kind: "dispatch" }>,
  fallthrough: boolean,
): number {
  const bodyCost = statement.cases.reduce((sum, dispatchCase) => sum + countStatements(dispatchCase.body), 0);
  const defaultCost = statement.defaultBody ? countStatements(statement.defaultBody) : 0;
  const jumpsAfterCases = Math.max(0, statement.cases.length - (fallthrough && !statement.defaultBody ? 1 : 0));
  return 2 + statement.cases.length * 5 + jumpsAfterCases * 2 + bodyCost + defaultCost;
}

function orderRawInputs(
  inputs: NonNullable<Extract<StatementAst, { kind: "core" }>["inputs"]>,
): NonNullable<Extract<StatementAst, { kind: "core" }>["inputs"]> {
  const order = new Map([
    ["T", 0],
    ["Z", 1],
    ["Y", 2],
    ["X", 3],
  ]);
  return [...inputs].sort((left, right) => order.get(left.slot)! - order.get(right.slot)!);
}

function formatRawContractDetail(statement: Extract<StatementAst, { kind: "core" }>): string {
  const inputs = statement.inputs?.length
    ? `takes ${orderRawInputs(statement.inputs).map((input) => `${input.slot}=${expressionToIntentText(input.expr)}`).join(", ")}`
    : "takes none";
  const outputs = statement.outputs?.length
    ? `returns ${statement.outputs.map((output) => `${output.slot}->${output.target}`).join(", ")}`
    : "returns none";
  const clobbers = `clobbers ${(statement.clobbers ?? ["unknown"]).join(", ")}`;
  const preserves = `preserves ${(statement.preserves ?? ["unknown"]).join(", ")}`;
  return `Inserted raw MK-61 block at line ${statement.line}: ${inputs}; ${outputs}; ${clobbers}; ${preserves}.`;
}

function parseRawInstruction(
  text: string,
): { opcode: number; mnemonic: string; target?: string | number; formalTargetOpcode?: number; comment?: string } | undefined {
  const hex = /^[0-9A-Fa-f]{2}$/u.exec(text);
  if (hex) {
    const opcode = Number.parseInt(text, 16);
    return { opcode, mnemonic: getOpcode(opcode).name, comment: "raw hex" };
  }

  const direct = /^(БП|ПП|F\s*x<0|F\s*x=0|F\s*x(?:!=|≠)0|F\s*x(?:>=|≥)0|F\s*L[0-3])\s+([A-Za-z_][\w]*|[0-9A-Fa-f]{2})$/u.exec(text);
  if (direct) {
    const opcode = directOpcode(direct[1]!);
    const target = parseTarget(direct[2]!);
    return {
      opcode,
      mnemonic: getOpcode(opcode).name,
      ...(typeof target === "object" ? target : { target }),
      comment: "raw branch",
    };
  }

  const compactStore = /^хП([0-9a-eавсде])$/iu.exec(text);
  if (compactStore) {
    const register = registerFromText(compactStore[1]!);
    return { opcode: 0x40 + registerIndex(register), mnemonic: `X->П ${register}` };
  }
  const compactRecall = /^Пх([0-9a-eавсде])$/iu.exec(text);
  if (compactRecall) {
    const register = registerFromText(compactRecall[1]!);
    return { opcode: 0x60 + registerIndex(register), mnemonic: `П->X ${register}` };
  }

  const directMemory = /^(X(?:->|→)П|П(?:->|→)X)\s+R?([0-9a-eавсде])$/iu.exec(text);
  if (directMemory) {
    const register = registerFromText(directMemory[2]!);
    const op = directMemory[1]!.replaceAll("→", "->");
    const base = op.startsWith("X") ? 0x40 : 0x60;
    return {
      opcode: base + registerIndex(register),
      mnemonic: `${op} ${register}`,
    };
  }

  const indirect = /^(К\s*)?(БП|ПП|X(?:->|→)П|П(?:->|→)X|x(?:!=|≠)0|x(?:>=|≥)0|x<0|x=0)\s*R?([0-9a-eавсде])$/iu.exec(text);
  if (indirect?.[1]) {
    const register = registerFromText(indirect[3]!);
    return {
      opcode: indirectBase(indirect[2]!) + registerIndex(register),
      mnemonic: `К ${indirect[2]} ${register}`,
    };
  }

  const compactIndirect = /^К(БП|ПП|Пх|хП)([0-9a-eавсде])$/iu.exec(text);
  if (compactIndirect) {
    const register = registerFromText(compactIndirect[2]!);
    const op =
      compactIndirect[1] === "Пх"
        ? "П->X"
        : compactIndirect[1] === "хП"
          ? "X->П"
          : compactIndirect[1]!;
    return {
      opcode: indirectBase(op) + registerIndex(register),
      mnemonic: `К ${op} ${register}`,
    };
  }

  const found = findOpcodeName(text);
  if (found) return { opcode: found.code, mnemonic: found.name };
  return undefined;
}

function parseTarget(text: string): string | number | { target: number; formalTargetOpcode: number } {
  const formalOpcode = parseFormalAddressOpcode(text);
  if (formalOpcode === undefined) return text;
  const info = formalAddressInfo(formalOpcode);
  return { target: info.ordinal, formalTargetOpcode: formalOpcode };
}

function directOpcode(text: string): number {
  const normalized = text.replace(/\s+/g, " ").replaceAll("≠", "!=").replaceAll("≥", ">=");
  if (normalized === "БП") return 0x51;
  if (normalized === "ПП") return 0x53;
  if (normalized === "F x<0") return 0x5c;
  if (normalized === "F x=0") return 0x5e;
  if (normalized === "F x!=0") return 0x57;
  if (normalized === "F x>=0") return 0x59;
  if (normalized === "F L0") return 0x5d;
  if (normalized === "F L1") return 0x5b;
  if (normalized === "F L2") return 0x58;
  if (normalized === "F L3") return 0x5a;
  throw new Error(`Unknown direct opcode ${text}`);
}

function indirectBase(text: string): number {
  const normalized = text.toLowerCase().replaceAll("→", "->").replaceAll("≠", "!=").replaceAll("≥", ">=");
  if (normalized === "x!=0") return 0x70;
  if (normalized === "бп") return 0x80;
  if (normalized === "x>=0") return 0x90;
  if (normalized === "пп") return 0xa0;
  if (normalized === "x->п") return 0xb0;
  if (normalized === "x<0") return 0xc0;
  if (normalized === "п->x") return 0xd0;
  if (normalized === "x=0") return 0xe0;
  throw new Error(`Unknown indirect opcode ${text}`);
}

function binaryOpcode(op: "+" | "-" | "*" | "/"): number {
  return op === "+" ? 0x10 : op === "-" ? 0x11 : op === "*" ? 0x12 : 0x13;
}

function summarizeBlocks(items: MachineItem[]): string[] {
  const blocks: Array<{ label: string; size: number }> = [];
  let current = "<entry>";
  let size = 0;
  for (const item of items) {
    if (item.kind === "label") {
      if (size > 0) blocks.push({ label: current, size });
      current = item.name;
      size = 0;
    } else {
      size += 1;
    }
  }
  if (size > 0) blocks.push({ label: current, size });
  return blocks
    .sort((a, b) => b.size - a.size)
    .slice(0, 3)
    .map((block) => `${block.label}=${block.size}`);
}
