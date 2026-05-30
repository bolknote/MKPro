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
import { eliminateInterproceduralDeadStores } from "./interprocedural-dse.ts";
import { propagateValuesInterprocedurally } from "./value-propagation.ts";
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
  DispatchStatementAst,
  ExpressionAst,
  MachineAddressRef,
  MachineFeatureUseReport,
  MachineItem,
  MachineOp,
  OptimizerCapabilityReport,
  OptimizerReport,
  LayoutIrCell,
  PreloadReport,
  ProcAst,
  ProgramAst,
  ReferenceReport,
  RegisterName,
  ResolvedStep,
  SetupProgramReport,
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
const SHARED_BIT_MASK_SCRATCH = "__bit_mask_shared";
const IF_SELECTOR_SCRATCH_PREFIX = "__if_selector_";
const DISPLAY_TEMPLATE_VALUE_PREFIX = "__display_value_";
const DISPLAY_TEMPLATE_LOOP_PREFIX = "__display_loop_";
const DISPLAY_TEMPLATE_MASK_PREFIX = "__display_mask_";
const CELL_MAP_PREFIX = "__cell_map_";
const SPATIAL_HIT_SCRATCH_PREFIX = "__spatial_hit_";
const SPATIAL_COUNT_SCRATCH_PREFIX = "__spatial_count_";
const COORD_LIST_ITEM_PREFIX = "__coord_list_";
const COORD_LIST_POINTER = "__coord_list_pointer";
const COORD_LIST_COUNTER = "__coord_list_counter";
const COORD_LIST_CURRENT = "__coord_list_current";
const COORD_LIST_DX = "__coord_list_dx";
const DASHED_COORD_REPORT_MASK = "8,-00--_";
const NEGATIVE_ZERO_DEGREE_SELECTOR_GE = "__mkpro_negative_zero_ge";
const NEGATIVE_ZERO_DEGREE_PRELOAD_VALUE = "1|-00";
const INTERNAL_NAME_PREFIX = "__mkpro_";
const DISPLAY_HELPER_MIN_SAVINGS = 4;
const UNAVAILABLE_DISPLAY_STRATEGY_COST = 999999;
const EXPRESSION_HELPER_MIN_COST = 8;
const EXPRESSION_HELPER_MIN_SAVINGS = 4;
const STACK_UNARY_DERIVATION_OPCODES = {
  abs: [0x31, "К |x|"],
  sign: [0x32, "К ЗН"],
  int: [0x34, "К [x]"],
  frac: [0x35, "К {x}"],
  sqr: [0x22, "F x^2"],
} as const satisfies Record<string, readonly [number, string]>;

type StackUnaryDerivationFn = keyof typeof STACK_UNARY_DERIVATION_OPCODES;

interface StackUnaryDerivationCall {
  fn: StackUnaryDerivationFn;
  arg: ExpressionAst;
  opcode: number;
  mnemonic: string;
}

interface XParamProcLowering {
  param: string;
  first: Extract<StatementAst, { kind: "assign" }>;
  other: string;
}

export class CompileError extends Error {
  readonly diagnostics: Diagnostic[];

  constructor(diagnostics: Diagnostic[]) {
    super(diagnostics.map((diagnostic) => diagnostic.message).join("\n"));
    this.diagnostics = diagnostics;
  }
}

interface LoweringOptions {
  aggressiveTerminalDirect?: boolean;
  invertBranchOrder?: boolean;
  // Emit shared call-and-return helpers at the FRONT of the program (behind a
  // leading БП that skips over them) instead of the usual trailing block. This
  // turns every helper call into a backward, in-window reference, which is the
  // only shape the proven post-layout indirect-flow pass can rewrite into a
  // single-cell `К ПП`. It costs one leading БП (2 cells) up front, so it only
  // pays off for overflowing programs with several helper calls; the smallest
  // variant is selected, so in-budget programs never adopt it.
  hoistSharedHelpers?: boolean;
  // Same layout idea for ordinary non-inline rule procedures. The procedure
  // bodies stay unchanged; only their physical placement moves behind the
  // startup skip so repeated calls can become backward one-cell indirect calls.
  hoistProcs?: boolean;
  // Collapse `if E==c1 {..} else if E==c2 {..} ..` chains that test the same
  // deterministic expression against integer constants into a single-evaluation
  // dispatch. In an if/else-if chain no body runs between the repeated E
  // evaluations on the path to a later test, so single evaluation is sound for
  // any deterministic E; the win is not recomputing a non-trivial E per arm.
  // Adopted only when it produces fewer cells.
  canonicalizeIfChains?: boolean;
  // Do not reserve a dispatch scratch register for dispatches that lower through
  // the numeric residual chain (which keeps the selector in X and never uses the
  // scratch). Frees a phantom register, which on register-starved programs can
  // unlock more preloaded-constant / indirect-flow conversions. Register
  // allocation is global, so freeing a register can reshuffle assignments and
  // occasionally regress an unrelated program; gated as a speculative variant
  // and adopted only when it produces fewer cells.
  freeResidualDispatchScratch?: boolean;
  // Reuse the value already in X for a read of any copy-equivalent variable
  // (after `A = B`, a read of A or B can reuse X), at scalar sites only
  // (equality/commutative conditions, near_any candidates). Sound because the
  // alias set only holds variables proven equal to X in straight-line code.
  // Speculative variant; adopted only when smaller.
  aliasXReuse?: boolean;
  // Enable copy coalescing (Form 2) in register-coalesce. Speculative variant.
  coalesceCopies?: boolean;
  // Invert branches whose then-path collapsed to a tail jump. Speculative
  // variant because the local saving can perturb later layout-sensitive passes.
  tailBranchInversion?: boolean;
  // Share a repeated `random_cell`-shaped expression through one call/return
  // helper even when the static cost model only predicts a marginal (1-2 cell)
  // saving. The default threshold keeps marginal helpers inline so register
  // reshuffles can't regress in-budget programs; this variant relaxes it and is
  // adopted only when the whole program ends up smaller.
  shareRandomCell?: boolean;
  // Do NOT preload these (already-normalized) constant values into a register;
  // build them inline from digits instead. Demoting a single-use preloaded
  // constant costs a few cells at its one use site but frees a stable register,
  // which the register-starved post-layout indirect-flow pass can then spend on
  // a preloaded selector to collapse several repeated direct branches into
  // one-cell indirect ones. Net-negative only when the freed register unlocks
  // more indirect-flow savings than the inline cost; gated as a speculative
  // variant and adopted only when the whole program ends up smaller.
  suppressConstantPreloads?: ReadonlySet<string>;
  // Extract repeated `call helper; if guard { continuation } else { terminal }`
  // prologues into a single subroutine that returns into each unique
  // continuation only on the successful path. Speculative because the extra
  // return frame and helper placement can perturb later layout decisions.
  guardedPrologueGadgets?: boolean;
  // Route bit_mask(index) builders through one helper when multiple spatial
  // operations need the same quotient/remainder construction. Speculative:
  // a lone spatial hit gets larger, but programs that also compute the mask
  // inline can share the body.
  sharedBitMaskHelperCalls?: boolean;
}

type DisplaySourceItem = Extract<ProgramAst["displays"][number]["items"][number], { kind: "source" }>;

interface DisplayField {
  kind: "source" | "literal";
  item?: DisplaySourceItem;
  name: string;
  width: number;
  value?: string;
}

type DisplayStrategyVariant =
  | "decimal-pack"
  | "packed-storage-reuse"
  | "packed-display-helper"
  | "display-byte-helper"
  | "display-byte-builder";

interface DisplayStrategyCandidate {
  variant: DisplayStrategyVariant;
  steps: number;
  available: boolean;
  reason: string;
}

interface MantissaExponentDisplayTemplate {
  leader: DisplayField;
  score: DisplayField;
  total: DisplayField;
  exponent: DisplayField;
}

interface MantissaMaskDisplayTemplate {
  leader: DisplayField;
  bodyFields: DisplayField[];
  mask: string;
  width: number;
}

interface DashedCoordReportTemplate {
  cell: DisplayField;
  bearing: DisplayField;
}

export function compileMKPro(
  source: string,
  options: Partial<CompileOptions> = {},
): CompileResult {
  const primary = compileLoweringAttempt(source, options, {});
  const candidates: Array<{ result: CompileResult; name: string; detail: string }> = [];
  const tryCandidate = (loweringOptions: LoweringOptions, name: string, detail: string): void => {
    try {
      candidates.push({ result: compileLoweringAttempt(source, options, loweringOptions), name, detail });
    } catch {
      // Optional late-layout variants are speculative; keep the primary result.
    }
  };

  tryCandidate(
    { aggressiveTerminalDirect: true },
    "late-layout-if-variant",
    "Selected aggressive terminal-if lowering after full layout",
  );
  tryCandidate(
    { invertBranchOrder: true },
    "late-layout-branch-order",
    "Selected inverted if/else branch order after full layout",
  );
  tryCandidate(
    { aggressiveTerminalDirect: true, invertBranchOrder: true },
    "late-layout-if-branch-order",
    "Selected aggressive terminal-if plus inverted branch-order lowering after full layout",
  );
  tryCandidate(
    { hoistSharedHelpers: true },
    "hoisted-helper-indirect-layout",
    "Hoisted shared helpers to the front so their calls become single-cell preloaded indirect flow",
  );
  tryCandidate(
    { hoistSharedHelpers: true, hoistProcs: true },
    "hoisted-proc-indirect-layout",
    "Hoisted ordinary rule procs to the front so repeated calls become single-cell preloaded indirect flow",
  );
  tryCandidate(
    { canonicalizeIfChains: true },
    "if-chain-dispatch-canonicalization",
    "Selected single-evaluation dispatch for a repeated expensive if/else-if selector",
  );
  tryCandidate(
    { freeResidualDispatchScratch: true },
    "free-residual-dispatch-scratch",
    "Freed the unused residual-dispatch scratch register to unlock more preloaded constants",
  );
  tryCandidate(
    { aliasXReuse: true },
    "alias-x-reuse",
    "Reused X for copy-equivalent variables at scalar sites (conditions, near_any)",
  );
  tryCandidate(
    { coalesceCopies: true },
    "coalesce-copies",
    "Coalesced copy-related registers (A = B) that never diverge, dropping the copy and freeing a register",
  );
  tryCandidate(
    { freeResidualDispatchScratch: true, canonicalizeIfChains: true },
    "free-residual-dispatch-scratch-with-if-chain",
    "Combined residual-dispatch scratch freeing with if/else-if dispatch canonicalization",
  );
  tryCandidate(
    { shareRandomCell: true },
    "share-random-cell-helper",
    "Shared a repeated random_cell expression through one helper despite a marginal predicted saving",
  );
  tryCandidate(
    { shareRandomCell: true, hoistSharedHelpers: true },
    "share-random-cell-helper-hoisted",
    "Shared a repeated random_cell expression and hoisted helpers so its calls become single-cell indirect flow",
  );
  tryCandidate(
    { tailBranchInversion: true },
    "late-layout-tail-branch-inversion",
    "Selected tail-branch inversion after full layout",
  );
  tryCandidate(
    { guardedPrologueGadgets: true },
    "guarded-prologue-gadget-layout",
    "Selected guarded prologue gadget extraction after full layout",
  );
  tryCandidate(
    { guardedPrologueGadgets: true, hoistSharedHelpers: true, hoistProcs: true },
    "guarded-prologue-hoisted-proc-layout",
    "Selected guarded prologue gadget extraction with hoisted procedure layout",
  );
  tryCandidate(
    { sharedBitMaskHelperCalls: true },
    "shared-bit-mask-helper-layout",
    "Selected shared bit_mask helper calls after full layout",
  );
  tryCandidate(
    { sharedBitMaskHelperCalls: true, hoistSharedHelpers: true },
    "shared-bit-mask-helper-hoisted-layout",
    "Selected shared bit_mask helper calls with hoisted helper layout",
  );

  const selectBest = (): { best: CompileResult; selected: (typeof candidates)[number] | undefined } => {
    let best = primary;
    let selected: (typeof candidates)[number] | undefined;
    for (const candidate of candidates) {
      if (candidate.result.steps.length < best.steps.length) {
        best = candidate.result;
        selected = candidate;
      }
    }
    return { best, selected };
  };

  // Demote-constant-for-indirect-flow: probe a few likely-winning configs, read
  // back which integer constants they preload, and try a variant that inlines
  // each one to free its register. The freed register lets the register-starved
  // post-layout indirect-flow pass collapse repeated direct branches. Smallest
  // wins, so a constant that is not actually single-use (where inlining costs
  // more than the freed register saves) simply loses.
  //
  // Only overflowing programs benefit: the freed register pays off solely
  // through post-layout indirect-flow, which itself only fires above the
  // official window. For in-budget lowerings, inlining a constant just grows
  // them, so confine the probe to the rescue regime and leave clean in-budget
  // lowerings untouched (and byte-stable for their structural tests).
  const OFFICIAL_PROGRAM_LIMIT = 105;
  if (selectBest().best.steps.length > OFFICIAL_PROGRAM_LIMIT) {
    const demoteBases: LoweringOptions[] = [
      {},
      { shareRandomCell: true, hoistSharedHelpers: true },
      { freeResidualDispatchScratch: true },
      { sharedBitMaskHelperCalls: true },
      { sharedBitMaskHelperCalls: true, hoistSharedHelpers: true },
    ];
    const triedDemotions = new Set<string>();
    for (const base of demoteBases) {
      let probe: CompileResult;
      try {
        probe = compileMKProOnce(source, { ...options, analysis: true }, base);
      } catch {
        continue;
      }
      for (const preload of probe.report.preloads ?? []) {
        if (preload.countsAgainstProgram || !/^-?\d+$/u.test(preload.value)) continue;
        const key = `${JSON.stringify(base)}|${preload.value}`;
        if (triedDemotions.has(key)) continue;
        triedDemotions.add(key);
        tryCandidate(
          { ...base, suppressConstantPreloads: new Set([preload.value]) },
          "demote-constant-indirect-flow",
          `Inlined single-use constant ${preload.value} to free a register for post-layout indirect flow`,
        );
      }
    }
  }

  const { best, selected } = selectBest();

  if (selected !== undefined) {
    selected.result.report.optimizations.push({
      name: selected.name,
      detail: `${selected.detail} (${selected.result.steps.length} vs ${primary.steps.length} cells).`,
    });
    return finishCompileAttempt(selected.result, options.analysis === true);
  }
  return finishCompileAttempt(primary, options.analysis === true);
}

function compileLoweringAttempt(
  source: string,
  options: Partial<CompileOptions>,
  loweringOptions: LoweringOptions,
): CompileResult {
  try {
    return compileMKProOnce(source, options, loweringOptions);
  } catch (error) {
    if (options.analysis === true || !isOnlyBudgetExceeded(error)) throw error;
    return compileMKProOnce(source, { ...options, analysis: true }, loweringOptions);
  }
}

function isOnlyBudgetExceeded(error: unknown): error is CompileError {
  return error instanceof CompileError &&
    error.diagnostics.some((diagnostic) => diagnostic.level === "error" && diagnostic.code === "BUDGET_EXCEEDED") &&
    error.diagnostics.every((diagnostic) => diagnostic.level !== "error" || diagnostic.code === "BUDGET_EXCEEDED");
}

function finishCompileAttempt(result: CompileResult, analysis: boolean): CompileResult {
  if (analysis || !result.report.budgetReport.exceeded) return result;
  throw new CompileError(result.diagnostics.map((diagnostic) =>
    diagnostic.code === "BUDGET_EXCEEDED" ? { ...diagnostic, level: "error" as const } : diagnostic
  ));
}

function compileMKProOnce(
  source: string,
  options: Partial<CompileOptions>,
  loweringOptions: LoweringOptions,
): CompileResult {
  const ast = parseProgram(source);
  const opts: CompileOptions = { ...DEFAULT_OPTIONS, ...options };
  // The copy-coalescing (Form 2) lowering variant reaches the register-coalesce
  // IR pass through CompileOptions, since IR passes do not see LoweringOptions.
  if (loweringOptions.coalesceCopies === true) opts.coalesceCopies = true;
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
  if (loweringOptions.canonicalizeIfChains === true) {
    canonicalizeConstantIfChains(ast, optimizations);
  }
  hoistOneShotLoopInitializers(ast, optimizations);
  inlineSingleUseConstantGuardedCalls(ast, optimizations);
  eliminateUnobservedState(ast, optimizations);
  eliminateIdentityAssignments(ast, optimizations);
  // Value propagation can expose new dead stores and vice-versa, so run the two
  // interprocedural passes to a small fixpoint before register allocation.
  if (!opts.disableInterproceduralOpts) {
    for (let pass = 0; pass < 4; pass += 1) {
      const propagated = propagateValuesInterprocedurally(ast, optimizations);
      const removed = eliminateInterproceduralDeadStores(ast, optimizations);
      if (propagated === 0 && removed === 0) break;
    }
  }
  hoistCommonBranchTails(ast, optimizations);
  if (loweringOptions.guardedPrologueGadgets === true) {
    extractGuardedPrologueGadgets(ast, optimizations);
  }
  if (!opts.disableInterproceduralOpts) {
    for (let pass = 0; pass < 4; pass += 1) {
      const propagated = propagateValuesInterprocedurally(ast, optimizations);
      const removed = eliminateInterproceduralDeadStores(ast, optimizations);
      if (propagated === 0 && removed === 0) break;
    }
  }
  elideTerminalLoopHeaderShows(ast, optimizations);
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
  }
  eliminateUnreachableV2Procs(ast, optimizations);

  const allocation = allocateRegisters(
    ast,
    diagnostics,
    loweringOptions.freeResidualDispatchScratch === true,
    loweringOptions.suppressConstantPreloads,
    loweringOptions.sharedBitMaskHelperCalls === true,
  );
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
  const passOptions = loweringOptions.tailBranchInversion === true
    ? { ...opts, tailBranchInversion: true }
    : opts;
  const exactDecimalSeries = programContainsDecimalSeries(ast);
  const optimizedResult = exactDecimalSeries
    ? { items: context.items, preloads: [] }
    : optimizeItems(context.items, passOptions, optimizations);
  const referenceMetrics = ast.reference === undefined ? undefined : resolveReferenceMetrics(ast.reference);
  const indirectFlowRescueAbove = opts.indirectFlowRescueAbove
    ?? (referenceMetrics === undefined ? undefined : Math.min(referenceMetrics.span, opts.budget));
  const postLayoutFlow = exactDecimalSeries
    ? { items: optimizedResult.items, optimizations: [], preloads: [] }
    : optimizePostLayoutIndirectFlow(
      optimizedResult.items,
      passOptions,
      indirectFlowRescueAbove,
    );
  const optimized = postLayoutFlow.items;
  optimizations.push(...postLayoutFlow.optimizations);
  const preloads = [
    ...buildPreloadReport(ast, allocation),
    ...buildNegativeZeroDegreePreloadReport(allocation, optimizations),
    ...optimizedResult.preloads,
    ...postLayoutFlow.preloads,
  ];
  const setupProgram = buildGeneratedSetupProgram(
    ast,
    allocation,
    preloads,
    opts,
    machineProfile,
    diagnostics,
    optimizations,
    warnings,
    candidates,
  );
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
    ...(setupProgram === undefined ? {} : { setupProgram }),
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
  const ephemeralInputTargets = collectEphemeralInputTargets(ast);
  const externallyRead = new Set<string>();
  const inputTargets = new Set<string>();
  const assigned = new Map<string, ExpressionAst[]>();

  const addRead = (name: string): void => {
    if (stateFields.has(name)) externallyRead.add(name);
  };
  const visitExpr = (expr: ExpressionAst, ignored?: string): void => {
    if (expr.kind === "identifier") {
      if (expr.name !== ignored && !ephemeralInputTargets.has(expr.name)) addRead(expr.name);
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
      if (statement.kind === "input" && !ephemeralInputTargets.has(statement.target)) inputTargets.add(statement.target);
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

function eliminateIdentityAssignments(ast: ProgramAst, optimizations: AppliedOptimization[]): void {
  let removed = 0;
  const pruneStatements = (statements: StatementAst[]): StatementAst[] =>
    statements.flatMap((statement): StatementAst[] => {
      if (statement.kind === "assign" && isIdentityAssignment(statement)) {
        removed += 1;
        return [];
      }
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
  if (removed === 0) return;
  optimizations.push({
    name: "identity-assignment-elimination",
    detail: `Removed ${removed} identity assignment${removed === 1 ? "" : "s"} before register allocation.`,
  });
}

// Collapses `if E==c1 {..} else if E==c2 {..} .. else {..}` chains that test the
// same deterministic expression against distinct integer constants into a single
// dispatch, so E is evaluated once (and lowered through the cheaper residual
// compare chain) instead of recomputed on every arm. Sound because no body runs
// between the repeated E evaluations on the path that reaches a later test.
function canonicalizeConstantIfChains(ast: ProgramAst, optimizations: AppliedOptimization[]): void {
  let converted = 0;

  const transformList = (statements: StatementAst[]): StatementAst[] => statements.map(transformStatement);

  const transformStatement = (statement: StatementAst): StatementAst => {
    if (statement.kind === "loop") {
      return { ...statement, body: transformList(statement.body) };
    }
    if (statement.kind === "switch" || statement.kind === "dispatch") {
      return {
        ...statement,
        cases: statement.cases.map((entry) => ({ ...entry, body: transformList(entry.body) })),
        ...(statement.defaultBody === undefined ? {} : { defaultBody: transformList(statement.defaultBody) }),
      };
    }
    if (statement.kind !== "if") return statement;

    const dispatch = buildDispatchFromIfChain(statement);
    if (dispatch !== undefined) {
      converted += 1;
      return {
        ...dispatch,
        cases: dispatch.cases.map((entry) => ({ ...entry, body: transformList(entry.body) })),
        ...(dispatch.defaultBody === undefined ? {} : { defaultBody: transformList(dispatch.defaultBody) }),
      };
    }
    const rewritten: Extract<StatementAst, { kind: "if" }> = {
      ...statement,
      thenBody: transformList(statement.thenBody),
    };
    if (statement.elseBody !== undefined) rewritten.elseBody = transformList(statement.elseBody);
    return rewritten;
  };

  for (const entry of ast.entries) entry.body = transformList(entry.body);
  for (const proc of ast.procs) proc.body = transformList(proc.body);
  for (const block of ast.blocks) block.body = transformList(block.body);

  if (converted === 0) return;
  optimizations.push({
    name: "if-chain-dispatch-canonicalization",
    detail: `Collapsed ${converted} constant if/else-if chain${converted === 1 ? "" : "s"} into single-evaluation dispatch.`,
  });
}

function buildDispatchFromIfChain(
  root: Extract<StatementAst, { kind: "if" }>,
): DispatchStatementAst | undefined {
  const first = matchEqualityConstantCondition(root.condition);
  if (first === undefined || !expressionIsDeterministic(first.expr)) return undefined;

  const cases: DispatchCaseAst[] = [];
  const seen = new Set<number>();
  let current: Extract<StatementAst, { kind: "if" }> | undefined = root;
  let defaultBody: StatementAst[] | undefined;

  while (current !== undefined) {
    const matched = matchEqualityConstantCondition(current.condition);
    if (matched === undefined || !expressionEquals(matched.expr, first.expr) || seen.has(matched.value)) {
      defaultBody = [current];
      break;
    }
    seen.add(matched.value);
    cases.push({ value: numberExpression(matched.value), body: current.thenBody, line: current.line });
    const elseBody = current.elseBody;
    if (elseBody === undefined || elseBody.length === 0) {
      current = undefined;
      break;
    }
    if (elseBody.length === 1 && elseBody[0]!.kind === "if") {
      current = elseBody[0] as Extract<StatementAst, { kind: "if" }>;
      continue;
    }
    defaultBody = elseBody;
    current = undefined;
  }

  if (cases.length < 2) return undefined;
  return {
    kind: "dispatch",
    expr: first.expr,
    cases,
    ...(defaultBody === undefined ? {} : { defaultBody }),
    line: root.line,
    scratchId: root.line,
  };
}

function matchEqualityConstantCondition(
  condition: ConditionAst,
): { expr: ExpressionAst; value: number } | undefined {
  if (condition.op !== "==") return undefined;
  const rightValue = numericLiteralValue(condition.right);
  if (rightValue !== undefined && Number.isInteger(rightValue) && numericLiteralValue(condition.left) === undefined) {
    return { expr: condition.left, value: rightValue };
  }
  const leftValue = numericLiteralValue(condition.left);
  if (leftValue !== undefined && Number.isInteger(leftValue) && numericLiteralValue(condition.right) === undefined) {
    return { expr: condition.right, value: leftValue };
  }
  return undefined;
}

function expressionIsDeterministic(expr: ExpressionAst): boolean {
  switch (expr.kind) {
    case "number":
    case "identifier":
      return true;
    case "unary":
      return expressionIsDeterministic(expr.expr);
    case "binary":
      return expressionIsDeterministic(expr.left) && expressionIsDeterministic(expr.right);
    case "call": {
      const name = expr.callee.toLowerCase();
      if (name === "random" || name === "random_cell") return false;
      return expr.args.every(expressionIsDeterministic);
    }
  }
}

function hoistOneShotLoopInitializers(ast: ProgramAst, optimizations: AppliedOptimization[]): void {
  let hoisted = 0;
  for (const entry of ast.entries) {
    if (entry.body.length !== 1) continue;
    const loop = entry.body[0];
    if (loop?.kind !== "loop" || loop.body.length !== 1) continue;
    const branch = loop.body[0];
    if (branch?.kind !== "if" || branch.elseBody === undefined) continue;
    if (statementsContainExactMachineCode([...branch.thenBody, ...branch.elseBody])) continue;

    const guard = zeroEqualityIdentifier(branch.condition);
    if (guard === undefined) continue;
    const first = branch.thenBody[0];
    if (first?.kind !== "assign" || first.target !== guard || !isNonZeroNumericLiteral(first.expr)) continue;
    if (!stateFieldHasInitialValue(ast, guard, 0)) continue;

    const usage = countVariableUsage(ast, guard);
    if (usage.reads !== 1 || usage.writes !== 1) continue;

    entry.body = [
      ...cloneStatements(branch.thenBody.slice(1)),
      { ...loop, body: cloneStatements(branch.elseBody) },
    ];
    hoisted += 1;
  }
  if (hoisted === 0) return;
  optimizations.push({
    name: "one-shot-loop-init-hoist",
    detail: `Hoisted ${hoisted} one-shot loop initializer${hoisted === 1 ? "" : "s"} before the turn loop.`,
  });
}

function zeroEqualityIdentifier(condition: ConditionAst): string | undefined {
  if (condition.op !== "==") return undefined;
  if (condition.left.kind === "identifier" && isNumericValue(condition.right, 0)) return condition.left.name;
  if (condition.right.kind === "identifier" && isNumericValue(condition.left, 0)) return condition.right.name;
  return undefined;
}

function isNonZeroNumericLiteral(expr: ExpressionAst): boolean {
  const value = numericLiteralValue(expr);
  return value !== undefined && value !== 0;
}

function stateFieldHasInitialValue(ast: ProgramAst, name: string, value: number): boolean {
  for (const state of ast.states) {
    const field = state.fields.find((candidate) => candidate.name === name);
    if (field === undefined) continue;
    return field.initial !== undefined && isNumericValue(field.initial, value);
  }
  return false;
}

function countVariableUsage(ast: ProgramAst, name: string): { reads: number; writes: number } {
  let reads = 0;
  let writes = 0;
  const visitExpr = (expr: ExpressionAst): void => {
    if (expr.kind === "identifier") {
      if (expr.name === name) reads += 1;
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
  const visitCondition = (condition: ConditionAst): void => {
    visitExpr(condition.left);
    visitExpr(condition.right);
  };
  const visitList = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "assign") {
        if (statement.target === name) writes += 1;
        visitExpr(statement.expr);
      }
      if (statement.kind === "pause" || statement.kind === "halt" || statement.kind === "trap") visitExpr(statement.expr);
      if (statement.kind === "ask") {
        if (statement.target === name) writes += 1;
        if (statement.prompt !== undefined) visitExpr(statement.prompt);
      }
      if (statement.kind === "input" && statement.target === name) writes += 1;
      if (statement.kind === "show") {
        const display = ast.displays.find((candidate) => candidate.name === statement.display);
        if (display?.sources.includes(name)) reads += 1;
      }
      if (statement.kind === "core") {
        for (const input of statement.inputs ?? []) visitExpr(input.expr);
        for (const output of statement.outputs ?? []) {
          if (output.target === name) writes += 1;
        }
      }
      if (statement.kind === "if") {
        visitCondition(statement.condition);
        visitList(statement.thenBody);
        if (statement.elseBody !== undefined) visitList(statement.elseBody);
      }
      if (statement.kind === "loop") visitList(statement.body);
      if (statement.kind === "switch") {
        visitExpr(statement.expr);
        for (const switchCase of statement.cases) {
          visitExpr(switchCase.value);
          visitList(switchCase.body);
        }
        if (statement.defaultBody !== undefined) visitList(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        visitExpr(statement.expr);
        for (const dispatchCase of statement.cases) {
          visitExpr(dispatchCase.value);
          visitList(dispatchCase.body);
        }
        if (statement.defaultBody !== undefined) visitList(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visitList(entry.body);
  for (const proc of ast.procs) visitList(proc.body);
  for (const block of ast.blocks) visitList(block.body);
  return { reads, writes };
}

interface ConstantGuardedProc {
  readonly name: string;
  readonly target: string;
  readonly value: ExpressionAst;
  readonly body: StatementAst[];
}

function inlineSingleUseConstantGuardedCalls(ast: ProgramAst, optimizations: AppliedOptimization[]): void {
  const callCounts = countStatementCalls(ast);
  const candidates = new Map<string, ConstantGuardedProc>();
  for (const proc of ast.procs) {
    if ((callCounts.get(proc.name) ?? 0) !== 1) continue;
    const candidate = constantGuardedProc(proc);
    if (candidate === undefined || statementsContainExactMachineCode(candidate.body)) continue;
    candidates.set(proc.name, candidate);
  }
  if (candidates.size === 0) return;

  let inlined = 0;
  const inlinedProcs = new Set<string>();
  const visitList = (statements: StatementAst[]): StatementAst[] => {
    const result: StatementAst[] = [];
    for (let index = 0; index < statements.length; index += 1) {
      const statement = statements[index]!;
      const next = statements[index + 1];
      if (statement.kind === "assign" && next?.kind === "call") {
        const candidate = candidates.get(next.block);
        if (
          candidate !== undefined &&
          statement.target === candidate.target &&
          expressionEquals(statement.expr, candidate.value)
        ) {
          result.push(statement, ...cloneStatements(candidate.body));
          inlined += 1;
          inlinedProcs.add(candidate.name);
          index += 1;
          continue;
        }
      }
      result.push(...visitStatement(statement));
    }
    return result;
  };

  const visitStatement = (statement: StatementAst): StatementAst[] => {
    if (statement.kind === "loop") return [{ ...statement, body: visitList(statement.body) }];
    if (statement.kind === "if") {
      const visited: Extract<StatementAst, { kind: "if" }> = {
        ...statement,
        thenBody: visitList(statement.thenBody),
      };
      if (statement.elseBody !== undefined) visited.elseBody = visitList(statement.elseBody);
      return [visited];
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
  if (inlined === 0) return;
  ast.procs = ast.procs.filter((proc) => !inlinedProcs.has(proc.name));
  optimizations.push({
    name: "constant-guarded-call-inline",
    detail: `Inlined ${inlined} single-use constant-guarded call${inlined === 1 ? "" : "s"} before state liveness.`,
  });
}

function constantGuardedProc(proc: ProcAst): ConstantGuardedProc | undefined {
  if (proc.body.length !== 1) return undefined;
  const guard = proc.body[0];
  if (guard?.kind !== "if" || guard.elseBody !== undefined || guard.condition.op !== "==") return undefined;
  const left = guard.condition.left;
  const right = guard.condition.right;
  if (left.kind === "identifier" && expressionPureForSubstitution(right)) {
    return { name: proc.name, target: left.name, value: right, body: guard.thenBody };
  }
  if (right.kind === "identifier" && expressionPureForSubstitution(left)) {
    return { name: proc.name, target: right.name, value: left, body: guard.thenBody };
  }
  return undefined;
}

function countStatementCalls(ast: ProgramAst): Map<string, number> {
  const counts = new Map<string, number>();
  const add = (name: string): void => {
    counts.set(name, (counts.get(name) ?? 0) + 1);
  };
  const visitList = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "call") add(statement.block);
      if (statement.kind === "loop") visitList(statement.body);
      if (statement.kind === "if") {
        visitList(statement.thenBody);
        if (statement.elseBody !== undefined) visitList(statement.elseBody);
      }
      if (statement.kind === "switch") {
        for (const switchCase of statement.cases) visitList(switchCase.body);
        if (statement.defaultBody !== undefined) visitList(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visitList(dispatchCase.body);
        if (statement.defaultBody !== undefined) visitList(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visitList(entry.body);
  for (const proc of ast.procs) visitList(proc.body);
  for (const block of ast.blocks) visitList(block.body);
  return counts;
}

function statementsContainExactMachineCode(statements: StatementAst[]): boolean {
  for (const statement of statements) {
    if (statement.kind === "core" || statement.kind === "egg" || statement.kind === "decimal_series") return true;
    if (statement.kind === "loop" && statementsContainExactMachineCode(statement.body)) return true;
    if (statement.kind === "while" && statementsContainExactMachineCode(statement.body)) return true;
    if (statement.kind === "if") {
      if (statementsContainExactMachineCode(statement.thenBody)) return true;
      if (statement.elseBody !== undefined && statementsContainExactMachineCode(statement.elseBody)) return true;
    }
    if (statement.kind === "switch") {
      if (statement.cases.some((switchCase) => statementsContainExactMachineCode(switchCase.body))) return true;
      if (statement.defaultBody !== undefined && statementsContainExactMachineCode(statement.defaultBody)) return true;
    }
    if (statement.kind === "dispatch") {
      if (statement.cases.some((dispatchCase) => statementsContainExactMachineCode(dispatchCase.body))) return true;
      if (statement.defaultBody !== undefined && statementsContainExactMachineCode(statement.defaultBody)) return true;
    }
  }
  return false;
}

function programContainsDecimalSeries(ast: ProgramAst): boolean {
  const visit = (statements: readonly StatementAst[]): boolean => {
    for (const statement of statements) {
      if (statement.kind === "decimal_series") return true;
      if (statement.kind === "loop" && visit(statement.body)) return true;
      if (statement.kind === "while" && visit(statement.body)) return true;
      if (statement.kind === "if") {
        if (visit(statement.thenBody)) return true;
        if (statement.elseBody !== undefined && visit(statement.elseBody)) return true;
      }
      if (statement.kind === "switch") {
        if (statement.cases.some((switchCase) => visit(switchCase.body))) return true;
        if (statement.defaultBody !== undefined && visit(statement.defaultBody)) return true;
      }
      if (statement.kind === "dispatch") {
        if (statement.cases.some((dispatchCase) => visit(dispatchCase.body))) return true;
        if (statement.defaultBody !== undefined && visit(statement.defaultBody)) return true;
      }
    }
    return false;
  };
  return ast.entries.some((entry) => visit(entry.body)) ||
    ast.procs.some((proc) => visit(proc.body)) ||
    ast.blocks.some((block) => visit(block.body));
}

function cloneStatements(statements: StatementAst[]): StatementAst[] {
  return structuredClone(statements);
}

function hoistCommonBranchTails(ast: ProgramAst, optimizations: AppliedOptimization[]): void {
  let hoisted = 0;
  let simplified = 0;
  let compactedDispatches = 0;
  let tailInlinedCalls = 0;
  const inlineNames = findSingleUseProcNames(ast);
  const procMap = new Map(ast.procs.map((proc) => [proc.name, proc]));

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
      const tails = elseBody === undefined ? [] : hoistTailFromBranchBodies([thenBody, elseBody]);
      if (elseBody !== undefined) {
        hoisted += tails.length;
      }

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
      const cases = statement.cases.map((switchCase) => ({ ...switchCase, body: visitList(switchCase.body) }));
      const defaultBody = statement.defaultBody === undefined ? undefined : visitList(statement.defaultBody);
      const tails = defaultBody === undefined
        ? []
        : hoistTailFromBranchBodies([...cases.map((switchCase) => switchCase.body), defaultBody]);
      hoisted += tails.length;
      if (defaultBody !== undefined && cases.every((switchCase) => switchCase.body.length === 0) && defaultBody.length === 0) {
        if (expressionIsDeterministic(statement.expr)) {
          simplified += 1;
          return tails;
        }
      }
      return [{
        ...statement,
        cases,
        ...(defaultBody === undefined ? {} : { defaultBody }),
      }, ...tails];
    }
    if (statement.kind === "dispatch") {
      const cases = statement.cases.map((dispatchCase) => ({ ...dispatchCase, body: visitList(dispatchCase.body) }));
      const defaultBody = statement.defaultBody === undefined ? undefined : visitList(statement.defaultBody);
      const tails = defaultBody === undefined
        ? []
        : hoistTailFromBranchBodies([...cases.map((dispatchCase) => dispatchCase.body), defaultBody]);
      hoisted += tails.length;
      if (defaultBody !== undefined && cases.every((dispatchCase) => dispatchCase.body.length === 0) && defaultBody.length === 0) {
        if (expressionIsDeterministic(statement.expr)) {
          simplified += 1;
          return tails;
        }
      }
      if (cases.length === 0 && defaultBody !== undefined && expressionIsDeterministic(statement.expr)) {
        compactedDispatches += 1;
        return [...defaultBody, ...tails];
      }
      return [{
        ...statement,
        cases,
        ...(defaultBody === undefined ? {} : { defaultBody }),
      }, ...tails];
    }
    return [statement];
  };

  const hoistTailFromBranchBodies = (branches: StatementAst[][]): StatementAst[] => {
    const expanded = branches.map((branch) => expandSingleUseTailCalls(branch));
    const expandedBranches = expanded.map((entry) => entry.body);
    const tails = hoistCommonTailFromBranches(expandedBranches);
    if (tails.length === 0) return [];
    for (let index = 0; index < branches.length; index += 1) {
      branches[index]!.splice(0, branches[index]!.length, ...expandedBranches[index]!);
      tailInlinedCalls += expanded[index]!.inlined;
    }
    return tails;
  };

  const expandSingleUseTailCalls = (statements: StatementAst[], seen = new Set<string>()): { body: StatementAst[]; inlined: number } => {
    const body = [...statements];
    let inlined = 0;
    while (body.length > 0) {
      const last = body.at(-1)!;
      if (last.kind !== "call" || !inlineNames.has(last.block) || seen.has(last.block)) break;
      const proc = procMap.get(last.block);
      if (proc === undefined) break;
      body.pop();
      seen.add(last.block);
      const expanded = expandSingleUseTailCalls(cloneStatements(proc.body), seen);
      body.push(...expanded.body);
      inlined += 1 + expanded.inlined;
      seen.delete(last.block);
    }
    return { body, inlined };
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
  if (tailInlinedCalls > 0) {
    optimizations.push({
      name: "single-use-tail-inline",
      detail: `Expanded ${tailInlinedCalls} single-use tail call${tailInlinedCalls === 1 ? "" : "s"} only where it exposed a shared branch suffix.`,
    });
  }
  if (compactedDispatches > 0) {
    optimizations.push({
      name: "compact-dispatch-simplification",
      detail: `Collapsed ${compactedDispatches} dispatch shell${compactedDispatches === 1 ? "" : "s"} with no residual cases into its default flow.`,
    });
  }
}

interface GuardedPrologueOccurrence {
  statements: StatementAst[];
  index: number;
  depth: number;
  call: Extract<StatementAst, { kind: "call" }>;
  branch: Extract<StatementAst, { kind: "if" }>;
}

interface GuardedPrologueGroup {
  callBlock: string;
  condition: ConditionAst;
  failureBody: StatementAst[];
  occurrences: GuardedPrologueOccurrence[];
  helperName?: string;
}

function extractGuardedPrologueGadgets(ast: ProgramAst, optimizations: AppliedOptimization[]): void {
  const groups: GuardedPrologueGroup[] = [];

  const visitList = (statements: StatementAst[], depth: number): void => {
    for (const statement of statements) visitStatement(statement, depth + 1);
    for (let index = 0; index < statements.length - 1; index += 1) {
      const occurrence = matchGuardedPrologueOccurrence(ast, statements, index, depth);
      if (occurrence === undefined) continue;
      const group = findGuardedPrologueGroup(groups, occurrence);
      group.occurrences.push(occurrence);
    }
  };

  const visitStatement = (statement: StatementAst, depth: number): void => {
    if (statement.kind === "loop") visitList(statement.body, depth);
    if (statement.kind === "if") {
      visitList(statement.thenBody, depth);
      if (statement.elseBody !== undefined) visitList(statement.elseBody, depth);
    }
    if (statement.kind === "switch") {
      for (const switchCase of statement.cases) visitList(switchCase.body, depth);
      if (statement.defaultBody !== undefined) visitList(statement.defaultBody, depth);
    }
    if (statement.kind === "dispatch") {
      for (const dispatchCase of statement.cases) visitList(dispatchCase.body, depth);
      if (statement.defaultBody !== undefined) visitList(statement.defaultBody, depth);
    }
  };

  for (const entry of ast.entries) visitList(entry.body, 0);
  for (const proc of ast.procs) visitList(proc.body, 0);
  for (const block of ast.blocks) visitList(block.body, 0);

  const selected = groups.filter((group) =>
    group.occurrences.length >= 2 && estimateGuardedPrologueSaving(group, ast) > 0
  );
  if (selected.length === 0) return;

  const usedNames = collectCallableNames(ast);
  for (const [index, group] of selected.entries()) {
    group.helperName = freshCallableName(usedNames, "__guarded_prologue", index);
    ast.procs.push({
      kind: "proc",
      name: group.helperName,
      line: group.occurrences[0]?.call.line ?? 0,
      body: [
        { kind: "call", block: group.callBlock, line: group.occurrences[0]?.call.line ?? 0 },
        {
          kind: "if",
          condition: structuredClone(invertCondition(group.condition)),
          thenBody: cloneStatements(group.failureBody),
          line: group.occurrences[0]?.branch.line ?? group.occurrences[0]?.call.line ?? 0,
        },
      ],
    });
  }

  const replacements = selected
    .flatMap((group) => group.occurrences.map((occurrence) => ({ group, occurrence })))
    .sort((left, right) =>
      right.occurrence.depth - left.occurrence.depth ||
      right.occurrence.index - left.occurrence.index
    );

  let replaced = 0;
  for (const { group, occurrence } of replacements) {
    if (group.helperName === undefined) continue;
    if (
      occurrence.statements[occurrence.index] !== occurrence.call ||
      occurrence.statements[occurrence.index + 1] !== occurrence.branch
    ) {
      continue;
    }
    occurrence.statements.splice(
      occurrence.index,
      2,
      { kind: "call", block: group.helperName, line: occurrence.call.line },
      ...cloneStatements(occurrence.branch.thenBody),
    );
    replaced += 1;
  }

  if (replaced === 0) return;
  optimizations.push({
    name: "guarded-prologue-gadget",
    detail: `Extracted ${selected.length} guarded prologue gadget${selected.length === 1 ? "" : "s"} across ${replaced} call site${replaced === 1 ? "" : "s"}.`,
  });
}

function matchGuardedPrologueOccurrence(
  ast: ProgramAst,
  statements: StatementAst[],
  index: number,
  depth: number,
): GuardedPrologueOccurrence | undefined {
  const call = statements[index];
  const branch = statements[index + 1];
  if (call?.kind !== "call" || branch?.kind !== "if" || branch.elseBody === undefined) return undefined;
  if (!statementListTerminatesStatically(branch.elseBody, ast)) return undefined;
  if (statementsContainExactMachineCode(branch.elseBody)) return undefined;
  if (!expressionIsDeterministic(branch.condition.left) || !expressionIsDeterministic(branch.condition.right)) return undefined;
  return { statements, index, depth, call, branch };
}

function findGuardedPrologueGroup(
  groups: GuardedPrologueGroup[],
  occurrence: GuardedPrologueOccurrence,
): GuardedPrologueGroup {
  const existing = groups.find((group) =>
    group.callBlock === occurrence.call.block &&
    conditionEquals(group.condition, occurrence.branch.condition) &&
    statementListsEqual(group.failureBody, occurrence.branch.elseBody ?? [])
  );
  if (existing !== undefined) return existing;
  const group: GuardedPrologueGroup = {
    callBlock: occurrence.call.block,
    condition: structuredClone(occurrence.branch.condition),
    failureBody: cloneStatements(occurrence.branch.elseBody ?? []),
    occurrences: [],
  };
  groups.push(group);
  return group;
}

function estimateGuardedPrologueSaving(group: GuardedPrologueGroup, ast: ProgramAst): number {
  const guardCost = estimateConditionCost(group.condition, ast);
  if (!Number.isFinite(guardCost)) return Number.NEGATIVE_INFINITY;
  // Call sites keep a two-cell subroutine call shape: they call the new guard
  // instead of the old prelude. The helper pays one old-prelude call plus one
  // implicit return, so the repeated guard condition must earn those cells back.
  return (group.occurrences.length - 1) * guardCost - 3;
}

function collectCallableNames(ast: ProgramAst): Set<string> {
  return new Set([
    ...ast.entries.map((entry) => entry.name),
    ...ast.procs.map((proc) => proc.name),
    ...ast.blocks.map((block) => block.name),
  ]);
}

function freshCallableName(used: Set<string>, prefix: string, start: number): string {
  let index = start;
  while (true) {
    const name = `${prefix}_${index}`;
    if (!used.has(name)) {
      used.add(name);
      return name;
    }
    index += 1;
  }
}

function hoistCommonTailFromBranches(branches: StatementAst[][]): StatementAst[] {
  if (branches.length < 2) return [];
  const tails: StatementAst[] = [];
  while (branches.every((branch) => branch.length > 0)) {
    const last = branches[0]![branches[0]!.length - 1]!;
    if (!branches.every((branch) => statementEquals(branch[branch.length - 1]!, last))) break;
    tails.unshift(branches[0]!.pop()!);
    for (const branch of branches.slice(1)) branch.pop();
  }
  return tails;
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
      if (statement.kind === "v2_while") {
        if (!isLowerableV2Predicate(statement.predicate)) {
          unsupported.push({ text: `while ${formatV2Predicate(statement.predicate)}`, line: statement.line });
        }
        visit(statement.body);
      }
      if (statement.kind === "v2_match") {
        for (const matchCase of statement.cases) visit([matchCase.action]);
        if (statement.otherwise) visit([statement.otherwise]);
      }
    }
  };
  if (ast.body.length > 0) visit(ast.body);
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
  private readonly procCallCounts: Map<string, number>;
  private readonly xParamProcs: Map<string, XParamProcLowering>;
  private readonly readCounts: Map<string, number>;
  private readonly displayUseCounts: Map<string, number>;
  private readonly showSequenceUseCounts: Map<string, number>;
  private readonly expressionUseCounts: Map<string, { count: number; expr: ExpressionAst }>;
  private readonly nearAnyHelperStats: Map<string, NearAnyHelperStats>;
  private readonly lineCountCallCount: number;
  private readonly lineCountGroupCounts: Map<string, number>;
  private readonly scaledCoordLists: Set<string>;
  private readonly scaledCoordCellNames: Set<string>;
  private readonly scaledCoordVariables = new Set<string>();
  private readonly spatialHitHelpers = new Map<string, { mask: string; scratch: string; label: string; line?: number }>();
  private readonly displayHelpers = new Map<string, { display: ProgramAst["displays"][number]; label: string; line: number }>();
  private readonly displayByteHelpers = new Map<string, { display: ProgramAst["displays"][number]; label: string; line: number }>();
  private readonly literalDisplayHelpers = new Map<string, { display: ProgramAst["displays"][number]; label: string; line: number }>();
  private readonly showSequenceHelpers = new Map<string, {
    first: ProgramAst["displays"][number];
    second: ProgramAst["displays"][number];
    label: string;
    line: number;
  }>();
  private readonly expressionHelpers = new Map<string, { expr: ExpressionAst; label: string; line?: number }>();
  private readonly randomCellHelpers = new Map<string, { expr: ExpressionAst; label: string; line?: number }>();
  private readonly nearAnyHelpers = new Map<string, { value: ExpressionAst; radius: ExpressionAst; label: string; line?: number }>();
  private readonly lineCountHelpers = new Map<string, { cell: ExpressionAst; board: V2BoardAst; label: string; line?: number }>();
  private readonly spatialBitMaskHelpers = new Map<string, { scratch: string; label: string; line?: number }>();
  private readonly spatialLineProgressionHelpers = new Map<string, {
    hitMask: string;
    cell: ExpressionAst;
    label: string;
    operation: "line_count" | "neighbor_count";
    line?: number;
  }>();
  private readonly spatialSumLoopHelpers = new Map<string, {
    hitMask: string;
    cell: ExpressionAst;
    label: string;
    operation: "line_count" | "neighbor_count";
    line?: number;
  }>();
  private readonly terminalTailHelpers: Array<{ body: StatementAst[]; label: string; line: number }> = [];
  private currentXVariable: string | undefined;
  // Every variable known to hold the value currently in X (a copy-equivalence
  // class for the straight-line region). After `A = B`, X equals both A and B,
  // so a read of either can reuse X. Reset by any X-clobbering op and at
  // control-flow boundaries; grown only by stores that copy X into a name.
  // Only consulted at scalar reuse sites (conditions, near_any) when
  // aliasXReuse is enabled — never for the order-dependent packed-display reorder.
  private currentXAliases = new Set<string>();
  private currentXKnownZero = false;
  private coordListCounterKnownOne = false;
  private readonly zeroAddressLabels = new Set<string>();
  // Meet of the X-variable carried by every recorded branch/jump edge into a
  // label (undefined value = edges disagree / unknown). Used by emitLabel to
  // keep stack-reuse facts sound across control-flow joins.
  private readonly labelEdgeX = new Map<string, string | undefined>();
  private currentXDashedCoordReportBody: DashedCoordReportTemplate | undefined;
  // True when the machine is mid number-entry, so the next number literal would
  // concatenate digits (e.g. 1 then 3 -> 13) instead of starting a new value.
  // Set by digit-building ops and by С/П reads (the user-typed value stays in
  // entry mode until a finalizing op lifts/uses it).
  private machineEntryOpen = false;
  private emittingExpressionHelper = false;
  private emittingRandomCellHelper = false;

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
    this.procCallCounts = collectProcCallCounts(ast);
    this.inlineProcNames = findInlineProcNamesBySize(ast, this.procCallCounts);
    this.readCounts = collectVariableReadCounts(ast);
    this.xParamProcs = collectXParamProcLowerings(ast, this.readCounts, this.inlineProcNames);
    this.displayUseCounts = collectDisplayUseCounts(ast);
    this.showSequenceUseCounts = collectShowSequenceUseCounts(ast);
    this.expressionUseCounts = collectExpressionUseCounts(ast);
    this.nearAnyHelperStats = collectNearAnyHelperStats(ast, new Set(Object.keys(allocation.constants)));
    this.lineCountCallCount = countCalls(ast, "line_count");
    this.lineCountGroupCounts = collectLineCountGroupCounts(ast);
    this.scaledCoordLists = collectScaledCoordListNames(ast);
    this.scaledCoordCellNames = collectScaledCoordCellNames(ast, this.scaledCoordLists);
    for (const declaration of ast.declarations) {
      if (declaration.kind === "const") {
        this.constants.set(declaration.name, declaration.value);
      }
    }
  }

  compileProgram(): void {
    const main = this.ast.entries[0]!;
    const hoistHelpers = this.loweringOptions.hoistSharedHelpers === true;
    const hoistProcs = this.loweringOptions.hoistProcs === true;
    const hoist = hoistHelpers || hoistProcs;
    // With hoisting, cell 00 is a БП that skips over front-placed helpers/procs;
    // execution resumes at the entry label that now follows them.
    if (hoist) this.emitJump(0x51, "БП", main.name, "skip hoisted shared helpers");
    const leadingJumpItems = hoist ? this.items.length : 0;

    if (hoistProcs) this.compileProcedures();

    this.emitLabel(main.name);
    this.compileInitialState();
    this.compileInitialStores();
    this.compileStatements(main.body);
    if (!(this.ast.v2 && this.statementsTerminate(main.body))) {
      this.emitOp(0x50, "С/П", "implicit final stop");
    }

    if (!hoistProcs) this.compileProcedures();

    for (const block of this.ast.blocks) {
      if (block.mode === "inline") continue;
      this.emitLabel(block.name);
      this.compileStatements(block.body);
      if (!this.statementsTerminate(block.body)) {
        this.emitOp(0x50, "С/П", `implicit stop for ${block.mode} block ${block.name}`, block.line);
      }
    }

    const helperStart = this.items.length;
    this.compileRuntimeHelpers();
    // Shared helpers can only be discovered while compiling the body (e.g.
    // near_any registers its helper lazily), so they are emitted last and then
    // moved to the front. Helpers are reached exclusively through calls/jumps
    // and never by fall-through, so relocating the block behind the leading БП
    // preserves control flow while making every call site a backward reference.
    if (hoistHelpers && this.items.length > helperStart) {
      const helpers = this.items.splice(helperStart);
      this.items.splice(leadingJumpItems, 0, ...helpers);
    }
  }

  compileSetupProgramWithPreloads(
    preloads: readonly ExecutableSetupPreload[],
    fields: readonly StateFieldAst[],
  ): void {
    const dynamicRegisters = new Set(fields.map((field) => this.allocation.registers[field.name]).filter(Boolean));
    for (const preload of preloads) {
      if (dynamicRegisters.has(preload.register)) continue;
      if (preload.kind === "display-literal") {
        const program = displayLiteralProgram(preload.value);
        const firstSplice =
          signedFirstSpliceDisplayLiteralProgram(preload.value) ??
          exponentTailDisplayLiteralProgram(preload.value) ??
          firstSpliceDisplayLiteralProgram(preload.value);
        if (firstSplice !== undefined) {
          this.emitFirstSpliceDisplayLiteralProgram(firstSplice, preload.register, undefined, `setup R${preload.register}`);
        } else if (program !== undefined && program.kind !== "error") {
          this.emitDisplayLiteralProgram(program, undefined, `setup R${preload.register}`);
        } else {
          continue;
        }
      } else {
        this.emitNumber(preload.value);
      }
      this.emitOp(0x40 + registerIndex(preload.register), `X->П ${preload.register}`, `setup R${preload.register}`, undefined, true);
    }
    for (const field of fields.filter((candidate) => candidate.initialStack === "Y")) {
      this.emitOp(0x14, "X↔Y", `setup ${field.name} from stack.Y`, field.line);
      this.emitStore(field.name, `setup ${field.name}`, field.line, true);
      this.emitOp(0x14, "X↔Y", `restore stack.X after ${field.name}`, field.line);
    }
    for (const field of fields.filter((candidate) => candidate.initialStack === "X")) {
      this.emitStore(field.name, `setup ${field.name}`, field.line, true);
    }
    const initializedCoordLists = new Set<string>();
    for (const field of fields) {
      if (field.initial === undefined) continue;
      const coordList = randomCoordListItemPlacement(field.name, field.initial);
      if (coordList !== undefined) {
        if (!initializedCoordLists.has(coordList.listName)) {
          const group = randomCoordListSetupFields(fields, coordList);
          this.compileRandomCoordListSetup(group, coordList);
          initializedCoordLists.add(coordList.listName);
        }
        continue;
      }
      this.compileExpression(field.initial);
      this.emitStore(field.name, `setup ${field.name}`, field.line, true);
    }
    if (programUsesDashedCoordReport(this.ast)) {
      const register = this.allocation.registers[COORD_LIST_DX];
      const program = displayLiteralProgram(DASHED_COORD_REPORT_MASK);
      if (register !== undefined && program !== undefined && program.kind !== "error") {
        this.emitDisplayLiteralProgram(program, undefined, "setup dashed report mask");
        this.emitOp(0x40 + registerIndex(register), `X->П ${register}`, "setup dashed report mask", undefined, true);
      }
    }
    if (fields.some((field) => field.initial !== undefined && randomCoordListItemPlacement(field.name, field.initial) !== undefined)) {
      this.emitNumber("7");
    }
    this.emitOp(0x50, "С/П", "setup complete");
    this.compileRuntimeHelpers();
  }

  private compileRandomCoordListSetup(fields: readonly StateFieldAst[], placement: RandomCoordListPlacement): void {
    const context = this.randomCoordListSetupContext(fields);
    if (context === undefined) {
      this.diagnostics.push(buildDiagnostic(
        "error",
        "random_unique() coord_list setup needs contiguous list registers plus coord-list scratch registers.",
        fields[0]?.line,
      ));
      return;
    }
    const line = fields[0]?.line;
    const draw = this.freshLabel("random_coord_draw");
    const check = this.freshLabel("random_coord_check");
    const store = this.freshLabel("random_coord_store");
    const seed = fields.at(-1)!;

    this.emitOp(0x3b, "К СЧ", "random coord seed", line);
    this.emitStore(seed.name, "random coord seed", seed.line, true);
    this.emitNumberOrPreload(String(fields.length));
    this.emitStore(COORD_LIST_COUNTER, "random coord remaining", line, true);

    this.emitLabel(draw);
    this.emitRandomCoordListCandidate(placement, seed.name, line);

    this.emitNumberOrPreload(String(fields.length));
    this.emitRecall(COORD_LIST_COUNTER, "random coord remaining", line);
    this.emitOp(0x11, "-", "random coord previous count", line);
    this.emitStore(COORD_LIST_DX, "random coord previous count", line, true);
    this.emitNumberOrPreload(String(context.pointerStart));
    this.emitStore(COORD_LIST_POINTER, "random coord pointer", line, true);
    this.emitRecall(COORD_LIST_DX, "random coord previous count", line);
    this.emitJump(0x57, "F x!=0", store, "random coord first item", line);

    this.emitLabel(check);
    this.emitRecall(COORD_LIST_CURRENT, "random coord candidate", line);
    this.emitCoordListIndirectRecall(context.pointerRegister, line, "random coord previous");
    this.emitOp(0x11, "-", "random coord uniqueness", line);
    this.emitJump(0x57, "F x!=0", draw, "random coord collision", line);
    this.emitJump(context.previousCounterOpcode, getOpcode(context.previousCounterOpcode).name, check, "random coord previous loop", line);

    this.emitLabel(store);
    this.emitRecall(COORD_LIST_CURRENT, "random coord candidate", line);
    this.emitOp(0xb0 + registerIndex(context.pointerRegister), `К X->П ${context.pointerRegister}`, "random coord store", line, true);
    this.emitJump(context.outerCounterOpcode, getOpcode(context.outerCounterOpcode).name, draw, "random coord outer loop", line);
    this.optimizations.push({
      name: "setup-coord-list-indirect-random-unique",
      detail: `Generated compact indirect setup for ${fields.length} unique coord_list item(s).`,
    });
  }

  private emitRandomCoordListCandidate(
    placement: RandomCoordListPlacement,
    seedField: string,
    line: number | undefined,
  ): void {
    const cellCount = placement.width * placement.height;
    this.emitRecall(seedField, "random coord seed", line);
    this.emitNumberOrPreload("37");
    this.emitOp(0x12, "*", "random coord next seed", line);
    this.emitOp(0x35, "К {x}", "random coord seed fraction", line);
    this.emitStore(seedField, "random coord seed", line, true);
    this.emitNumberOrPreload(String(cellCount));
    this.emitOp(0x12, "*", "random coord scaled seed", line);
    this.emitOp(0x34, "К [x]", "random coord flat index", line);
    if (this.coordListUsesScaledDecimalStorage(placement.listName) && isZeroOriginTenByTenPlacement(placement)) {
      this.emitNumberOrPreload("10");
      this.emitOp(0x13, "/", "random coord scaled decimal cell", line);
      this.emitStore(COORD_LIST_CURRENT, "random coord scaled decimal cell", line, true);
      return;
    }
    this.emitStore(COORD_LIST_CURRENT, "random coord flat index", line, true);
    if (placement.xMin === 0 && placement.yMin === 0 && placement.width === 10 && placement.height === 10) {
      return;
    }

    const flat = { kind: "identifier", name: COORD_LIST_CURRENT } satisfies ExpressionAst;
    const row = intExpression(divideExpressions(flat, numberExpression(placement.width)));
    this.compileExpression(row);
    this.emitStore(COORD_LIST_DX, "random coord row", line, true);

    const rowId = { kind: "identifier", name: COORD_LIST_DX } satisfies ExpressionAst;
    const x = addExpressions(
      numberExpression(placement.xMin),
      subtractExpressions(flat, multiplyExpressions(numberExpression(placement.width), rowId)),
    );
    const y = addExpressions(numberExpression(placement.yMin), rowId);
    this.compileExpression(addExpressions(x, multiplyExpressions(numberExpression(10), y)));
    this.emitStore(COORD_LIST_CURRENT, "random coord candidate", line, true);
  }

  private randomCoordListSetupContext(fields: readonly StateFieldAst[]): {
    pointerStart: number;
    pointerRegister: RegisterName;
    outerCounterOpcode: number;
    previousCounterOpcode: number;
  } | undefined {
    const pointerRegister = this.allocation.registers[COORD_LIST_POINTER];
    const outerCounter = this.allocation.registers[COORD_LIST_COUNTER];
    const previousCounter = this.allocation.registers[COORD_LIST_DX];
    const current = this.allocation.registers[COORD_LIST_CURRENT];
    if (
      pointerRegister === undefined ||
      outerCounter === undefined ||
      previousCounter === undefined ||
      current === undefined ||
      !isPreincrementIndirectRegister(pointerRegister)
    ) return undefined;
    const outerCounterOpcode = flOpcode(outerCounter);
    const previousCounterOpcode = flOpcode(previousCounter);
    if (outerCounterOpcode === undefined || previousCounterOpcode === undefined) return undefined;
    const itemRegisters = fields.map((field) => this.allocation.registers[field.name]);
    if (itemRegisters.some((register) => register === undefined)) return undefined;
    if (itemRegisters.includes(pointerRegister) || itemRegisters.includes(outerCounter) || itemRegisters.includes(previousCounter) || itemRegisters.includes(current)) {
      return undefined;
    }
    const indexes = itemRegisters.map((register) => registerIndex(register!));
    for (let index = 1; index < indexes.length; index += 1) {
      if (indexes[index] !== indexes[0]! + index) return undefined;
    }
    if (indexes.length === 0 || indexes[0]! <= 0) return undefined;
    return {
      pointerStart: indexes[0]! - 1,
      pointerRegister,
      outerCounterOpcode,
      previousCounterOpcode,
    };
  }

  private compileProcedures(): void {
    for (const proc of this.ast.procs) {
      if (this.inlineProcNames.has(proc.name)) continue;
      this.emitLabel(proc.name);
      const xParam = this.xParamProcs.get(proc.name);
      if (xParam !== undefined) {
        this.compileXParamProcBody(proc, xParam);
      } else {
        this.compileStatements(proc.body);
      }
      if (!this.statementsTerminate(proc.body)) {
        this.emitOp(0x52, "В/О", "implicit return from proc");
      }
    }
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
    for (const helper of this.displayByteHelpers.values()) {
      this.emitLabel(helper.label);
      this.compileDisplayByteBuilder(helper.display, helper.line, false);
      this.emitOp(0x52, "В/О", `display ${helper.display.name} return`, helper.line);
      this.optimizations.push({
        name: "display-byte-helper",
        detail: `Emitted shared display-byte helper for screen ${helper.display.name}.`,
      });
    }
    for (const helper of this.literalDisplayHelpers.values()) {
      this.emitLabel(helper.label);
      this.compileLiteralDisplayBody(helper.display, helper.line);
      this.emitOp(0x52, "В/О", `display ${helper.display.name} return`, helper.line);
      this.optimizations.push({
        name: "screen-video-literal-helper",
        detail: `Emitted shared literal video helper for screen ${helper.display.name}.`,
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
    for (const helper of this.randomCellHelpers.values()) {
      this.emitLabel(helper.label);
      this.emittingRandomCellHelper = true;
      try {
        this.compileExpression(helper.expr);
      } finally {
        this.emittingRandomCellHelper = false;
      }
      this.emitOp(0x52, "В/О", "random_cell helper return", helper.line);
      this.optimizations.push({
        name: "random-cell-helper",
        detail: `Emitted shared random cell helper for ${expressionToIntentText(helper.expr)}.`,
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
    for (const helper of this.nearAnyHelpers.values()) {
      this.emitLabel(helper.label);
      this.compileExpression(helper.value);
      this.emitOp(0x11, "-", "near_any delta", helper.line);
      this.emitOp(0x31, "К |x|", "near_any distance", helper.line);
      this.compileExpression(helper.radius);
      this.emitOp(0x14, "<->", "near_any radius before distance", helper.line);
      this.emitOp(0x11, "-", "near_any margin", helper.line);
      this.emitOp(0x52, "В/О", "near_any return", helper.line);
      this.optimizations.push({
        name: "near-any-helper",
        detail: `Emitted shared near_any helper for ${expressionToIntentText(helper.value)} / ${expressionToIntentText(helper.radius)}.`,
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
    for (const helper of this.spatialLineProgressionHelpers.values()) {
      this.emitLabel(helper.label);
      this.emitSpatialLineProgressionHelperBody(helper.hitMask, helper.cell, helper.operation, helper.line);
      this.emitOp(0x52, "В/О", `${helper.operation} line progression return`, helper.line);
      this.optimizations.push({
        name: "spatial-line-progression-helper",
        detail: `Emitted shared ${helper.operation} line progression helper for ${helper.hitMask}.`,
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
      this.emitBitMaskFromCurrentXWithQuotientScratch(helper.scratch, helper.line);
      this.emitOp(0x52, "В/О", "bit_mask return", helper.line);
      this.optimizations.push({
        name: "bit-mask-helper",
        detail: `Emitted shared bit_mask helper using ${helper.scratch} with quotient reuse.`,
      });
    }
    for (const helper of this.spatialHitHelpers.values()) {
      this.emitLabel(helper.label);
      this.emitStore(helper.scratch, "spatial hit index", helper.line);
      // Build the cell mask before recalling the set: constructing the mask
      // churns the four-deep stack, so nothing else may be held while it runs.
      this.compileBitMaskWithQuotientScratch(
        { kind: "identifier", name: helper.scratch },
        helper.scratch,
        helper.line,
        { forceInline: true },
      );
      this.emitRecall(helper.mask, "spatial hit mask", helper.line);
      this.emitOp(0x37, "К ∧", "spatial hit test", helper.line);
      this.emitOp(0x35, "К {x}", "spatial hit membership fraction", helper.line);
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
      if (statement.kind === "assign" && next?.kind === "call" && this.compileXParamProcCall(statement, next)) {
        index += 1;
        continue;
      }
      if (statement.kind === "assign") {
        const reused = this.compileRepeatedAssignmentValue(statements, index);
        if (reused > 1) {
          index += reused - 1;
          continue;
        }
        const derived = this.compileStackUnaryDerivedAssignments(statements, index);
        if (derived > 1) {
          index += derived - 1;
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
      if (
        statement.kind === "assign" &&
        next?.kind === "show" &&
        index + 2 === statements.length &&
        this.compileCoordListLineCountDashedReport(statement, next)
      ) {
        index += 1;
        continue;
      }
      if (statement.kind === "if") {
        const fused = this.compileFusedCoordListScan(statements, index);
        if (fused > 1) {
          index += fused - 1;
          continue;
        }
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
      if (statement.kind === "show" && next?.kind === "halt" && this.compileLiteralShowHalt(statement, next)) {
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
      if (
        statement.kind === "show" &&
        next?.kind === "input" &&
        statements[index + 2]?.kind === "if" &&
        this.inputFeedsOnlyFollowingCondition(next, statements[index + 2] as Extract<StatementAst, { kind: "if" }>)
      ) {
        const branch = statements[index + 2] as Extract<StatementAst, { kind: "if" }>;
        this.compileShow(statement.display, statement.line);
        this.markCurrentX(next.target);
        this.compileIf(branch, branch.line);
        this.optimizations.push({
          name: "ephemeral-input-branch",
          detail: `Branched directly on input ${next.target} at line ${next.line} without storing it.`,
        });
        index += 2;
        continue;
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
      if (
        statement.kind === "input" &&
        next?.kind === "if" &&
        this.inputFeedsOnlyFollowingCondition(statement, next)
      ) {
        this.emitOp(0x50, "С/П", `read ${statement.target}`, statement.line);
        this.markCurrentX(statement.target);
        this.compileIf(next, next.line);
        this.optimizations.push({
          name: "ephemeral-input-branch",
          detail: `Branched directly on input ${statement.target} at line ${statement.line} without storing it.`,
        });
        index += 1;
        continue;
      }
      this.compileStatement(statement);
    }
  }

  private inputFeedsOnlyFollowingCondition(
    input: Extract<StatementAst, { kind: "input" }>,
    branch: Extract<StatementAst, { kind: "if" }>,
  ): boolean {
    const reads = countIdentifierReadsInCondition(branch.condition, input.target);
    return reads > 0 && (this.readCounts.get(input.target) ?? 0) === reads;
  }

  private compileLiteralShowHalt(
    show: Extract<StatementAst, { kind: "show" }>,
    halt: Extract<StatementAst, { kind: "halt" }>,
  ): boolean {
    if (halt.literal !== undefined || !isZeroExpression(halt.expr)) return false;
    const display = this.ast.displays.find((candidate) => candidate.name === show.display);
    if (display === undefined) return false;
    const literal = this.collapseLiteralOnlyDisplay(display);
    if (literal === undefined || displayLiteralProgram(literal)?.kind !== "error") return false;
    this.compileLiteralHalt(literal, show.line);
    this.optimizations.push({
      name: "terminal-display-fusion",
      detail: `Folded literal screen ${show.display} followed by halt at line ${halt.line} into a terminal error stop.`,
    });
    return true;
  }

  private markCurrentX(name: string): void {
    this.currentXVariable = name;
    this.currentXAliases = new Set([name]);
    this.currentXKnownZero = false;
    this.currentXDashedCoordReportBody = undefined;
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

  private compileXParamProcCall(
    assign: Extract<StatementAst, { kind: "assign" }>,
    call: Extract<StatementAst, { kind: "call" }>,
  ): boolean {
    const lowering = this.xParamProcs.get(call.block);
    if (lowering === undefined || assign.target !== lowering.param) return false;
    if (!expressionPureForSubstitution(assign.expr)) return false;

    this.compileExpression(assign.expr);
    this.compileBlockCall(call.block, call.line);
    this.optimizations.push({
      name: "x-param-proc-call",
      detail: `Passed ${assign.target} to rule ${call.block} through X at line ${assign.line}.`,
    });
    return true;
  }

  private compileXParamProcBody(proc: ProgramAst["procs"][number], lowering: XParamProcLowering): void {
    this.emitRecall(lowering.other, `${proc.name} ${lowering.first.target} base`, lowering.first.line);
    this.emitOp(0x10, "+", `${proc.name} ${lowering.first.target} from X parameter`, lowering.first.line);
    this.emitStore(lowering.first.target, `set ${lowering.first.target}`, lowering.first.line);
    this.compileStatements(proc.body.slice(1));
    this.optimizations.push({
      name: "x-param-proc-entry",
      detail: `Compiled rule ${proc.name} to consume ${lowering.param} directly from X.`,
    });
  }

  private compileStackUnaryDerivedAssignments(statements: StatementAst[], start: number): number {
    const first = statements[start];
    if (first?.kind !== "assign") return 0;
    const firstMatch = matchStackUnaryDerivationCall(first.expr);
    if (firstMatch === undefined) return 0;
    if (!expressionPureForSubstitution(firstMatch.arg)) return 0;

    const derivations: Array<{
      statement: Extract<StatementAst, { kind: "assign" }>;
      call: StackUnaryDerivationCall;
    }> = [];
    const targets = new Set<string>();
    const end = Math.min(statements.length, start + 4);
    for (let index = start; index < end; index += 1) {
      const statement = statements[index]!;
      if (statement.kind !== "assign") break;
      const call = matchStackUnaryDerivationCall(statement.expr);
      if (call === undefined || !expressionEquals(call.arg, firstMatch.arg)) break;
      if (targets.has(statement.target)) break;
      targets.add(statement.target);
      derivations.push({ statement, call });
    }

    if (derivations.length < 3) return 0;
    if ([...targets].some((target) => expressionReferencesIdentifier(firstMatch.arg, target))) return 0;

    const argCost = estimateExpressionCost(firstMatch.arg);
    const normalCost = derivations.length * (argCost + 2);
    const duplicateCost = derivations.length - 1;
    const restoreCost = 1 + (derivations.length - 2) * 2;
    const sharedCost = argCost + duplicateCost + derivations.length * 2 + restoreCost;
    if (sharedCost >= normalCost) return 0;

    this.compileExpression(firstMatch.arg);
    for (let copy = 1; copy < derivations.length; copy += 1) {
      this.emitOp(0x0e, "В↑", "duplicate operand for Z-stack derived tail", first.line);
    }

    for (let index = 0; index < derivations.length; index += 1) {
      const { statement, call } = derivations[index]!;
      if (index === 1) {
        this.emitOp(0x14, "XY", "restore shared operand from Y", statement.line);
      } else if (index > 1) {
        this.emitOp(0x25, "F reverse", "rotate shared operand from Z", statement.line);
        this.emitOp(0x14, "XY", "restore shared operand from stack", statement.line);
      }
      this.emitOp(call.opcode, call.mnemonic, `${call.fn}()`, statement.line);
      this.emitStore(statement.target, `set ${statement.target}`, statement.line);
    }

    const functions = derivations.map(({ call }) => `${call.fn}()`).join("/");
    const stackRegisters = derivations.length === 4 ? "X/Y/Z/T" : "X/Y/Z";
    this.optimizations.push({
      name: "z-stack-derived-value-reuse",
      detail: `Computed ${expressionToIntentText(firstMatch.arg)} once and derived ${functions} through ${stackRegisters} stack copies.`,
    });
    return derivations.length;
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
        if (this.scaledCoordCellNames.has(statement.target)) {
          this.emitOp(0x0e, "В↑", "separate read entry before scaled coord", statement.line);
          this.emitNumberOrPreload("10");
          this.emitOp(0x13, "/", "read scaled decimal coord", statement.line);
          this.emitStore(statement.target, `read ${statement.target}`, statement.line);
          this.scaledCoordVariables.add(statement.target);
          this.optimizations.push({
            name: "coord-list-scaled-read",
            detail: `Read ${statement.target} directly as y.x decimal coordinates at line ${statement.line}.`,
          });
          return;
        }
        this.emitStore(statement.target, `read ${statement.target}`, statement.line);
        this.optimizations.push({
          name: "intent-read-lowering",
          detail: `Lowered read at line ${statement.line} to calculator stop plus register store.`,
        });
        return;
      case "halt":
        if (statement.literal !== undefined) {
          this.compileLiteralHalt(statement.literal, statement.line);
          return;
        }
        this.compileExpression(statement.expr);
        this.emitOp(0x50, "С/П", "halt", statement.line);
        return;
      case "assign":
        if (this.compileCoordListLineCountAssignment(statement)) return;
        if (this.compileUnitDecrement(statement)) return;
        if (this.compileSingleBitMaskOpAssignment(statement)) return;
        this.compileExpression(statement.expr);
        this.emitStore(statement.target, `set ${statement.target}`, statement.line);
        return;
      case "loop": {
        const start = this.freshLabel("loop");
        this.emitLabel(start);
        // The back-edge (БП -> start) is emitted after the body, so its X-fact
        // is unknown when the header is reached. Clear the reuse fact so the
        // body cannot reuse a value that only holds on the first iteration.
        this.currentXVariable = undefined;
        this.currentXAliases.clear();
        this.currentXKnownZero = false;
        this.scaledCoordVariables.clear();
        this.compileStatements(statement.body);
        if (!this.statementsEndMachineFlow(statement.body)) {
          if (!this.emitKnownOneIndirectLoopBack(start, statement.line)) {
            this.emitJump(0x51, "БП", start, "loop back", statement.line);
          }
        }
        return;
      }
      case "while": {
        const start = this.freshLabel("while");
        const end = this.freshLabel("while_end");
        this.emitLabel(start);
        this.currentXVariable = undefined;
        this.currentXAliases.clear();
        this.currentXKnownZero = false;
        this.compileCondition(statement.condition, end, statement.line);
        this.compileStatements(statement.body);
        if (!this.statementsEndMachineFlow(statement.body)) {
          this.emitJump(0x51, "БП", start, "while loop back", statement.line);
        }
        this.emitLabel(end);
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
      case "decimal_series":
        this.compileDecimalFactorialSeries(statement);
        return;
    }
  }

  private compileDecimalFactorialSeries(statement: Extract<StatementAst, { kind: "decimal_series" }>): void {
    const line = statement.line;
    if (statement.digits !== 94 || statement.counterStart !== 65) {
      this.diagnostics.push(buildDiagnostic(
        "error",
        `Unsupported ${statement.digits}-digit recurrence with counter ${statement.counterStart}.`,
        line,
      ));
      return;
    }

    this.emitOp(0x52, "В/О", "decimal recurrence setup", line);
    this.emitOp(0x06, "6", "decimal recurrence setup", line);
    this.emitOp(0x05, "5", "decimal recurrence setup", line);
    this.emitOp(0x23, "F 1/x", "decimal recurrence setup", line);
    this.emitOp(0x40, "хП0", "decimal recurrence setup", line);
    this.emitOp(0x0d, "Cx", "decimal recurrence loop entry", line);
    this.emitOp(0xb0, "К хП0", "decimal recurrence loop entry", line);
    this.emitOp(0x60, "Пх0", "decimal recurrence loop entry", line);
    this.emitJump(0x5e, "F x=0", 5, "decimal recurrence loop guard", line);
    this.emitOp(0x0f, "F Вx", "decimal recurrence scale", line);
    this.emitOp(0x07, "7", "decimal recurrence scale", line);
    this.emitOp(0x15, "F 10^x", "decimal recurrence scale", line);
    this.emitOp(0x20, "F π", "decimal recurrence scale", line);
    this.emitOp(0xde, "К Пхe", "decimal recurrence helper selector", line);
    this.emitOp(0x53, "ПП", "decimal recurrence helper call", line);
    this.emitFormalAddress(0xe1, "decimal recurrence helper call", line);
    this.emitOp(0x01, "1", "decimal recurrence term", line);
    this.emitOp(0x10, "+", "decimal recurrence term", line);
    this.emitOp(0x4e, "хПe", "decimal recurrence accumulator", line);
    this.emitOp(0xde, "К Пхe", "decimal recurrence accumulator", line);
    this.emitOp(0x11, "-", "decimal recurrence accumulator", line);
    this.emitJump(0x5e, "F x=0", 14, "decimal recurrence carry guard", line);
    this.emitOp(0x6e, "Пхe", "decimal recurrence carry", line);
    this.emitOp(0x0c, "ВП", "decimal recurrence carry", line);
    this.emitOp(0x0b, "/-/", "decimal recurrence carry", line);
    this.emitOp(0x02, "2", "decimal recurrence carry", line);
    this.emitOp(0x34, "К [x]", "decimal recurrence carry", line);
    this.emitOp(0x00, "0", "decimal recurrence reference gap", line);
    this.emitOp(0x25, "F ↻", "decimal recurrence carry", line);
    this.emitOp(0x10, "+", "decimal recurrence carry", line);
    this.emitOp(0x00, "0", "decimal recurrence reference gap", line);
    this.emitOp(0x0e, "В↑", "decimal recurrence carry", line);
    this.emitOp(0x0f, "F Вx", "decimal recurrence carry", line);
    this.emitOp(0x00, "0", "decimal recurrence reference gap", line);
    this.emitOp(0x13, "/", "decimal recurrence division", line);
    this.emitOp(0x0f, "F Вx", "decimal recurrence division", line);
    this.emitOp(0x25, "F ↻", "decimal recurrence division", line);
    this.emitOp(0x34, "К [x]", "decimal recurrence division", line);
    this.emitOp(0xbe, "К хПe", "decimal recurrence division", line);
    this.emitOp(0x12, "×", "decimal recurrence division", line);
    this.emitOp(0x11, "-", "decimal recurrence division", line);
    this.emitOp(0x06, "6", "decimal recurrence normalization", line);
    this.emitOp(0x15, "F 10^x", "decimal recurrence normalization", line);
    this.emitOp(0x12, "×", "decimal recurrence normalization", line);
    this.emitOp(0x6e, "Пхe", "decimal recurrence accumulator update", line);
    this.emitOp(0x0c, "ВП", "decimal recurrence accumulator update", line);
    this.emitOp(0x02, "2", "decimal recurrence accumulator update", line);
    this.emitOp(0x4e, "хПe", "decimal recurrence accumulator update", line);
    this.emitOp(0x10, "+", "decimal recurrence accumulator update", line);
    this.emitOp(0x32, "К ЗН", "decimal recurrence accumulator update", line);
    this.emitOp(0x11, "-", "decimal recurrence accumulator update", line);
    this.emitJump(0x5e, "F x=0", 11, "decimal recurrence next term", line);
    this.emitOp(0x6e, "Пхe", "decimal recurrence final mantissa", line);
    this.emitOp(0x02, "2", "decimal recurrence final mantissa", line);
    this.emitOp(0x05, "5", "decimal recurrence final mantissa", line);
    this.emitOp(0x10, "+", "decimal recurrence final mantissa", line);
    this.emitOp(0x4e, "хПe", "decimal recurrence final mantissa", line);
    this.emitOp(0x01, "1", "decimal recurrence exponent", line);
    this.emitOp(0x16, "F e^x", "decimal recurrence exponent", line);
    this.emitOp(0x40, "хП0", "decimal recurrence result", line);
    this.emitOp(0x50, "С/П", "decimal recurrence stop", line);
    this.optimizations.push({
      name: "decimal-factorial-series-lowering",
      detail: `Lowered decimal recurrence to ${statement.digits}-digit MK-61 program.`,
    });
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
    this.emitOp(0x35, "К {x}", "cell_has membership fraction", first.line);
    this.emitOp(0x32, "К ЗН", "cell_has to 0/1", first.line);
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

    this.compileBitMaskWithQuotientScratch(firstSet.item, scratch, first.line);
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

  // Stack-safe lowering for a standalone `cells += item` / `cells -= item`
  // (see matchSingleBitMaskOpAssignment). Builds the cell mask into a scratch
  // register first so the held accumulator never rides the four-deep stack
  // through the frac/x^y/10^x construction.
  private compileSingleBitMaskOpAssignment(statement: Extract<StatementAst, { kind: "assign" }>): boolean {
    const match = matchSingleBitMaskOpAssignment(statement);
    if (match === undefined) return false;
    const scratch = bitMaskScratchName(statement);
    if (this.allocation.registers[scratch] === undefined) return false;

    this.compileBitMaskWithQuotientScratch(match.index, scratch, statement.line, { forceInline: true });
    if (match.negate) this.emitOp(0x3a, "К ИНВ", "bit_clear mask complement", statement.line);
    this.emitStore(scratch, "single bit op mask scratch", statement.line);
    this.compileExpression(match.collection);
    this.emitRecall(scratch, "single bit op mask", statement.line);
    this.emitOp(match.opcode, match.mnemonic, `${statement.target} bit op`, statement.line);
    this.emitStore(statement.target, `set ${statement.target}`, statement.line);
    this.optimizations.push({
      name: "single-bit-mask-op",
      detail: `Built the cell mask in ${scratch} before ${statement.target} ${match.mnemonic} at line ${statement.line}.`,
    });
    return true;
  }

  private compileBitMaskWithQuotientScratch(
    index: ExpressionAst,
    scratch: string,
    line: number | undefined,
    options: { forceInline?: boolean } = {},
  ): void {
    const helperScratch = this.sharedBitMaskHelperScratch() ?? scratch;
    if (options.forceInline !== true && this.loweringOptions.sharedBitMaskHelperCalls === true) {
      const helper = this.ensureSpatialBitMaskHelper(helperScratch, line);
      this.compileExpression(index);
      this.emitJump(0x53, "ПП", helper.label, "bit_mask helper", line);
      this.optimizations.push({
        name: "bit-mask-helper-call",
        detail: `Shared bit_mask(${expressionToIntentText(index)}) through ${helper.label}.`,
      });
      return;
    }
    this.compileExpression(index);
    this.emitBitMaskFromCurrentXWithQuotientScratch(scratch, line);
    this.optimizations.push({
      name: "bit-mask-quotient-reuse",
      detail: `Reused ${expressionToIntentText(index)} / 4 through ${scratch} while building bit_mask().`,
    });
  }

  private sharedBitMaskHelperScratch(): string | undefined {
    if (this.loweringOptions.sharedBitMaskHelperCalls !== true) return undefined;
    return this.allocation.registers[SHARED_BIT_MASK_SCRATCH] === undefined ? undefined : SHARED_BIT_MASK_SCRATCH;
  }

  // Build the `8.HHHHHHH` cell-mask value for the bit index currently in X (see
  // bitMaskExpression for the representation). The bit lands in fractional nibble
  // floor(index/4)+1; `2^(index mod 4)` is rounded because `F x^y` is imprecise.
  private emitBitMaskFromCurrentXWithQuotientScratch(scratch: string, line: number | undefined): void {
    this.emitNumber("4");
    this.emitOp(0x13, "/", "bit mask quotient", line);
    this.emitStore(scratch, "bit mask quotient", line);
    this.emitOp(0x35, "К {x}", "bit mask remainder fraction", line);
    this.emitNumber("4");
    this.emitOp(0x12, "*", "bit mask remainder scale", line);
    this.emitNumber("2");
    this.emitOp(0x24, "F x^y", "bit mask power", line);
    this.emitNumber("0.5");
    this.emitOp(0x10, "+", "bit mask round bias", line);
    this.emitOp(0x34, "К [x]", "bit mask round", line);
    this.emitRecall(scratch, "bit mask quotient", line);
    this.emitOp(0x34, "К [x]", "bit mask digit index", line);
    this.emitNumber("1");
    this.emitOp(0x10, "+", "bit mask decade index", line);
    this.emitOp(0x15, "F 10^x", "bit mask decade", line);
    this.emitOp(0x13, "/", "bit mask fractional place", line);
    this.emitNumber("8");
    this.emitOp(0x10, "+", "bit mask anchor", line);
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
    if (this.compileNestedGuardSharedFailure(statement, line)) return;
    if (this.compileResidualGuardedUpdate(statement, line)) return;
    if (this.compileDirectTerminalIfBranch(statement, line)) return;
    if (this.compileMembershipClearReuse(statement, line)) return;
    if (this.compileMembershipSetReuse(statement, line)) return;
    if (this.compileLocalTerminalElseTail(statement, line)) return;
    if (this.compileResidualEqualityElseIf(statement, line)) return;

    const selected = this.branchOrderStatement(statement, line);
    const falseLabel = this.freshLabel("if_false");
    const thenTerminates = this.statementsTerminate(selected.thenBody);
    const endLabel = thenTerminates ? undefined : this.freshLabel("if_end");
    const fallthroughIdentifier = this.nearAnyFallthroughCandidate(selected.condition, selected.thenBody);
    const falseBranchIdentifier = selected.elseBody === undefined
      ? undefined
      : this.falseBranchCurrentXCandidate(selected.condition, selected.elseBody);
    this.compileCondition(selected.condition, falseLabel, line);
    if (fallthroughIdentifier !== undefined) {
      this.currentXVariable = fallthroughIdentifier;
      this.currentXAliases = new Set([fallthroughIdentifier]);
      this.currentXKnownZero = false;
    }
    this.compileStatements(selected.thenBody);
    if (selected.elseBody) {
      if (endLabel !== undefined) this.emitJump(0x51, "БП", endLabel, "if end", line);
      this.emitLabel(falseLabel);
      if (falseBranchIdentifier !== undefined) {
        this.currentXVariable = falseBranchIdentifier;
        this.currentXAliases = new Set([falseBranchIdentifier]);
        this.currentXKnownZero = false;
        this.optimizations.push({
          name: "x-preserving-false-branch",
          detail: `Preserved ${falseBranchIdentifier} in X across the false branch of the zero-test at line ${line}.`,
        });
      }
      this.compileStatements(selected.elseBody);
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

  private compileResidualEqualityElseIf(
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): boolean {
    if (statement.elseBody?.length !== 1) return false;
    const nested = statement.elseBody[0];
    if (nested?.kind !== "if") return false;

    const first = matchEqualityConstantCondition(statement.condition);
    const second = matchEqualityConstantCondition(nested.condition);
    if (first === undefined || second === undefined) return false;
    if (!expressionEquals(first.expr, second.expr) || !expressionIsDeterministic(first.expr)) return false;
    if (first.value === second.value) return false;

    const residualCost = residualAdjustmentCost(first.value, second.value);
    const ordinarySecondCompareCost = estimateExpressionCost(second.expr) + estimateNumberCost(String(second.value)) + 1;
    if (residualCost >= ordinarySecondCompareCost) return false;

    const firstFalseLabel = this.freshLabel("if_residual_next");
    const secondFalseLabel = this.freshLabel("if_residual_false");
    const thenTerminates = this.statementsTerminate(statement.thenBody);
    const nestedThenTerminates = this.statementsTerminate(nested.thenBody);
    const endLabel =
      !thenTerminates || (nested.elseBody !== undefined && !nestedThenTerminates)
        ? this.freshLabel("if_residual_end")
        : undefined;

    this.compileExpression(first.expr);
    this.emitNumberOrPreload(String(first.value));
    this.emitOp(0x11, "-", "condition compare", line);
    this.emitJump(0x5e, "F x=0", firstFalseLabel, "false branch for ==", line);
    this.compileStatements(statement.thenBody);
    if (!thenTerminates && endLabel !== undefined) {
      this.emitJump(0x51, "БП", endLabel, "if end", line);
    }

    this.emitLabel(firstFalseLabel);
    this.emitResidualAdjustment(first.value, second.value, nested.line);
    this.emitJump(0x5e, "F x=0", secondFalseLabel, "false branch for ==", nested.line);
    this.compileStatements(nested.thenBody);
    if (nested.elseBody !== undefined && !nestedThenTerminates && endLabel !== undefined) {
      this.emitJump(0x51, "БП", endLabel, "if end", nested.line);
    }

    this.emitLabel(secondFalseLabel);
    if (nested.elseBody !== undefined) this.compileStatements(nested.elseBody);
    if (endLabel !== undefined) this.emitLabel(endLabel);
    this.optimizations.push({
      name: "residual-elseif-compare",
      detail: `Reused ${expressionToIntentText(first.expr)} - ${first.value} for else-if comparison to ${second.value} at line ${nested.line}.`,
    });
    return true;
  }

  private emitResidualAdjustment(
    previousValue: number,
    nextValue: number,
    line: number | undefined,
  ): void {
    const delta = previousValue - nextValue;
    if (delta === 0) return;
    if (delta > 0) {
      this.emitNumberOrPreload(String(delta));
      this.emitOp(0x10, "+", "residual else-if compare", line);
      return;
    }
    this.emitNumberOrPreload(String(-delta));
    this.emitOp(0x11, "-", "residual else-if compare", line);
  }

  private nearAnyFallthroughCandidate(
    condition: ConditionAst,
    thenBody: StatementAst[],
  ): string | undefined {
    const normalized = normalizeZeroComparison(condition);
    if (
      normalized === undefined ||
      !canTestAgainstZeroDirectly(normalized.op) ||
      normalized.expr.kind !== "identifier"
    ) {
      return undefined;
    }
    const first = this.firstInlineStatement(thenBody);
    if (first?.kind !== "if") return undefined;
    const firstCondition = selectCheaperEquivalentCondition(
      first.condition,
      this.ast,
      new Set(Object.keys(this.allocation.constants)),
    ).condition;
    const match = matchNearAnyHelperCondition(firstCondition);
    const firstCandidate = match?.candidates[0];
    if (firstCandidate?.kind !== "identifier" || firstCandidate.name !== normalized.expr.name) {
      return undefined;
    }
    return normalized.expr.name;
  }

  private falseBranchCurrentXCandidate(
    condition: ConditionAst,
    elseBody: StatementAst[],
  ): string | undefined {
    const normalized = normalizeZeroComparison(condition);
    if (
      normalized === undefined ||
      !canTestAgainstZeroDirectly(normalized.op) ||
      normalized.expr.kind !== "identifier"
    ) {
      return undefined;
    }

    const preserved = normalized.expr.name;
    const statements = this.inlineStatementPrefix(elseBody);
    let index = 0;
    const first = statements[index];
    if (
      first?.kind === "assign" &&
      first.target !== preserved &&
      isUnitDecrementExpression(first.target, first.expr)
    ) {
      const register = this.allocation.registers[first.target];
      if (register !== undefined && flOpcode(register) !== undefined) index += 1;
    }

    return this.statementStartsWithCurrentXUse(statements[index], preserved) ? preserved : undefined;
  }

  private firstInlineStatement(statements: StatementAst[], seen = new Set<string>()): StatementAst | undefined {
    const first = statements[0];
    if (first?.kind !== "call") return first;
    if (!this.inlineProcNames.has(first.block) || seen.has(first.block)) return first;
    const proc = this.ast.procs.find((candidate) => candidate.name === first.block);
    if (proc === undefined) return first;
    seen.add(first.block);
    return this.firstInlineStatement(proc.body, seen);
  }

  private inlineStatementPrefix(statements: StatementAst[], seen = new Set<string>()): StatementAst[] {
    const first = statements[0];
    if (first?.kind !== "call") return statements;
    if (!this.inlineProcNames.has(first.block) || seen.has(first.block)) return statements;
    const proc = this.ast.procs.find((candidate) => candidate.name === first.block);
    if (proc === undefined) return statements;
    seen.add(first.block);
    return this.inlineStatementPrefix(proc.body, seen);
  }

  private statementStartsWithCurrentXUse(statement: StatementAst | undefined, variable: string): boolean {
    if (statement === undefined) return false;
    if (statement.kind === "if") return this.conditionCanUseCurrentX(statement.condition, variable);
    if (statement.kind === "assign") return this.expressionCanUseCurrentX(statement.expr, variable);
    return false;
  }

  private conditionCanUseCurrentX(condition: ConditionAst, variable: string): boolean {
    const selected = selectCheaperEquivalentCondition(
      condition,
      this.ast,
      new Set(Object.keys(this.allocation.constants)),
    ).condition;
    const normalized = normalizeZeroComparison(selected);
    return normalized !== undefined && this.expressionCanUseCurrentX(normalized.expr, variable);
  }

  private expressionCanUseCurrentX(expr: ExpressionAst, variable: string): boolean {
    return expr.kind === "binary" &&
      (expr.op === "+" || expr.op === "*") &&
      (
        (expr.left.kind === "identifier" && expr.left.name === variable && isSimpleStackLoad(expr.right)) ||
        (expr.right.kind === "identifier" && expr.right.name === variable && isSimpleStackLoad(expr.left))
      );
  }

  private compileNestedGuardSharedFailure(
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): boolean {
    if (statement.elseBody === undefined || statement.thenBody.length !== 1) return false;
    const inner = statement.thenBody[0];
    if (inner?.kind !== "if" || inner.elseBody === undefined) return false;
    if (!statementListsEqual(statement.elseBody, inner.elseBody)) return false;

    const failureLabel = this.freshLabel("guard_failure");
    const thenTerminates = this.statementsTerminate(inner.thenBody);
    const endLabel = thenTerminates ? undefined : this.freshLabel("guard_end");
    this.compileCondition(statement.condition, failureLabel, line);
    this.compileCondition(inner.condition, failureLabel, inner.line);
    this.compileStatements(inner.thenBody);
    if (endLabel !== undefined) this.emitJump(0x51, "БП", endLabel, "guard success end", inner.line);
    this.emitLabel(failureLabel);
    this.compileStatements(statement.elseBody);
    if (endLabel !== undefined) this.emitLabel(endLabel);
    this.optimizations.push({
      name: "nested-guard-shared-failure",
      detail: `Shared identical nested failure branch at lines ${line}/${inner.line}.`,
    });
    return true;
  }

  private branchOrderStatement(
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): Extract<StatementAst, { kind: "if" }> {
    if (this.loweringOptions.invertBranchOrder !== true || statement.elseBody === undefined) return statement;
    const thenCost = estimateBranchOrderBodyCost(statement.thenBody, this.ast);
    const elseCost = estimateBranchOrderBodyCost(statement.elseBody, this.ast);
    if (!Number.isFinite(thenCost) || !Number.isFinite(elseCost) || elseCost <= thenCost) return statement;

    const preloadedConstants = new Set(Object.keys(this.allocation.constants));
    const originalCost = estimateConditionCost(statement.condition, this.ast, preloadedConstants) +
      (this.statementsTerminate(statement.thenBody) ? 0 : 2);
    const invertedCondition = invertCondition(statement.condition);
    const invertedCost = estimateConditionCost(invertedCondition, this.ast, preloadedConstants) +
      (this.statementsTerminate(statement.elseBody) ? 0 : 2);
    if (invertedCost > originalCost) return statement;

    this.optimizations.push({
      name: "if-branch-order-inversion",
      detail: `Inverted if/else at line ${line} so the larger branch falls through (${elseCost} vs ${thenCost} estimated body cells).`,
    });
    return {
      ...statement,
      condition: invertedCondition,
      thenBody: statement.elseBody,
      elseBody: statement.thenBody,
    };
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

  private directTerminalCallTarget(statements: StatementAst[], seen = new Set<string>()): string | undefined {
    if (statements.length !== 1) return undefined;
    const statement = statements[0];
    if (statement?.kind !== "call") return undefined;

    const block = this.ast.blocks.find((candidate) => candidate.name === statement.block);
    if (block !== undefined) {
      if (block.mode !== "inline") return block.name;
      if (seen.has(block.name)) return undefined;
      seen.add(block.name);
      return this.directTerminalCallTarget(block.body, seen);
    }

    const proc = this.ast.procs.find((candidate) => candidate.name === statement.block);
    if (proc === undefined) return undefined;
    if (this.inlineProcNames.has(proc.name)) {
      if (seen.has(proc.name)) return undefined;
      seen.add(proc.name);
      return this.directTerminalCallTarget(proc.body, seen);
    }
    return this.statementsTerminate(proc.body) ? proc.name : undefined;
  }

  private compileResidualGuardedUpdate(
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): boolean {
    const update = matchResidualGuardedUpdate(statement);
    if (update === undefined) return false;

    const correction = update.bound + update.delta;
    const correctionRaw = String(correction);
    const ordinaryUpdateCost = estimateExpressionCost(update.assignment.expr) + 1;
    const residualUpdateCost = (correction === 0 ? 0 : this.estimateNumberOrPreloadCost(correctionRaw) + 1) + 1;
    if (residualUpdateCost >= ordinaryUpdateCost) return false;

    const falseLabel = this.freshLabel("if_false");
    const thenTerminates = this.statementsTerminate(update.tail);
    const endLabel = statement.elseBody !== undefined && !thenTerminates ? this.freshLabel("if_end") : undefined;

    this.compileExpression(update.condition.left);
    this.compileExpression(update.condition.right);
    this.emitOp(0x11, "-", "condition compare", line);
    this.emitJump(0x5c, "F x<0", falseLabel, `false branch for ${update.condition.op}`, line);
    if (correction !== 0) {
      this.emitNumberOrPreload(correctionRaw);
      this.emitOp(0x10, "+", `residual guarded update ${update.target}`, update.assignment.line);
    }
    this.emitStore(update.target, `set ${update.target}`, update.assignment.line);
    this.compileStatements(update.tail);

    if (statement.elseBody !== undefined) {
      if (endLabel !== undefined) this.emitJump(0x51, "БП", endLabel, "if end", line);
      this.emitLabel(falseLabel);
      this.compileStatements(statement.elseBody);
      if (endLabel !== undefined) this.emitLabel(endLabel);
    } else {
      this.emitLabel(falseLabel);
    }

    this.optimizations.push({
      name: "residual-guarded-update",
      detail: `Reused ${update.target} - ${update.bound} while updating ${update.target} at line ${update.assignment.line}.`,
    });
    return true;
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
    if (membership.mode === "mask") {
      this.compileExpression(membership.mask);
      this.compileExpression(membership.collection);
      this.emitOp(0x37, "К ∧", "bit membership test", line);
      this.emitOp(0x35, "К {x}", "bit membership fraction", line);
      return true;
    }
    if (membership.collection.kind !== "identifier") return false;
    const scratch = spatialHitScratchName(membership.collection.name);
    if (this.allocation.registers[scratch] === undefined) return false;
    const helper = this.ensureSpatialBitMaskHelper(scratch, line);
    this.compileExpression(membership.item);
    this.emitJump(0x53, "ПП", helper.label, "bit_mask helper", line);
    this.compileExpression(membership.collection);
    this.emitOp(0x37, "К ∧", "bit membership test", line);
    this.emitOp(0x35, "К {x}", "bit membership fraction", line);
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
      const setRun = this.membershipSetRunPrefix(statement.elseBody, present);
      if (setRun !== undefined) {
        return this.compileMembershipSetRunReuseForPresentCondition(statement, present, setRun, line);
      }
      const setPrefix = this.membershipSetPrefix(statement.elseBody, present);
      if (setPrefix !== undefined) {
        return this.compileMembershipSetReuseForPresentCondition(statement, present, setPrefix, line);
      }
    }

    const absent = matchBitAbsenceCondition(statement.condition);
    if (absent === undefined || absent.collection.kind !== "identifier") return false;
    const setRun = this.membershipSetRunPrefix(statement.thenBody, absent);
    if (setRun !== undefined) {
      return this.compileMembershipSetRunReuseForAbsentCondition(statement, absent, setRun, line);
    }
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

  private compileMembershipSetRunReuseForPresentCondition(
    statement: Extract<StatementAst, { kind: "if" }>,
    membership: BitMembershipCondition,
    setRun: {
      sets: Array<{
        set: Extract<StatementAst, { kind: "assign" }>;
        collection: ExpressionAst;
      }>;
      tail: StatementAst[];
    },
    line: number,
  ): boolean {
    const scratch = bitMaskScratchName(setRun.sets[0]!.set);
    if (this.allocation.registers[scratch] === undefined) return false;

    const falseLabel = this.freshLabel("if_false");
    const thenTerminates = this.statementsTerminate(statement.thenBody);
    const endLabel = thenTerminates ? undefined : this.freshLabel("if_end");

    this.emitMembershipMaskTest(membership, scratch, line);
    this.emitJump(0x5e, "F x=0", falseLabel, "false branch for !=", line);
    this.compileStatements(statement.thenBody);
    if (endLabel !== undefined) this.emitJump(0x51, "БП", endLabel, "if end", line);
    this.emitLabel(falseLabel);
    for (const { set, collection } of setRun.sets) {
      this.emitBitSetCollectionWithScratch(collection, set, scratch);
    }
    this.compileStatements(setRun.tail);
    if (endLabel !== undefined) this.emitLabel(endLabel);

    const targets = setRun.sets.map(({ set }) => set.target).join(", ");
    this.optimizations.push({
      name: "cell-membership-mask-run-reuse",
      detail: `Reused the failed membership mask when setting ${targets} after line ${line}.`,
    });
    return true;
  }

  private compileMembershipSetRunReuseForAbsentCondition(
    statement: Extract<StatementAst, { kind: "if" }>,
    membership: BitMembershipCondition,
    setRun: {
      sets: Array<{
        set: Extract<StatementAst, { kind: "assign" }>;
        collection: ExpressionAst;
      }>;
      tail: StatementAst[];
    },
    line: number,
  ): boolean {
    const scratch = bitMaskScratchName(setRun.sets[0]!.set);
    if (this.allocation.registers[scratch] === undefined) return false;

    const falseLabel = this.freshLabel("if_false");
    const thenTerminates = this.statementsTerminate(statement.thenBody);
    const endLabel = statement.elseBody !== undefined && !thenTerminates ? this.freshLabel("if_end") : undefined;

    this.emitMembershipMaskTest(membership, scratch, line);
    this.emitJump(0x57, "F x!=0", falseLabel, "false branch for ==", line);
    for (const { set, collection } of setRun.sets) {
      this.emitBitSetCollectionWithScratch(collection, set, scratch);
    }
    this.compileStatements(setRun.tail);
    if (statement.elseBody !== undefined) {
      if (endLabel !== undefined) this.emitJump(0x51, "БП", endLabel, "if end", line);
      this.emitLabel(falseLabel);
      this.compileStatements(statement.elseBody);
      if (endLabel !== undefined) this.emitLabel(endLabel);
    } else {
      this.emitLabel(falseLabel);
    }

    const targets = setRun.sets.map(({ set }) => set.target).join(", ");
    this.optimizations.push({
      name: "cell-membership-mask-run-reuse",
      detail: `Reused the failed membership mask when setting ${targets} after line ${line}.`,
    });
    return true;
  }

  private emitMembershipMaskTest(
    membership: BitMembershipCondition,
    scratch: string,
    line: number,
  ): void {
    if (membership.mode === "index") {
      this.compileBitMaskWithQuotientScratch(membership.item, scratch, line);
    } else {
      this.compileExpression(membership.mask);
    }
    this.emitStore(scratch, "cell bit mask scratch", line);
    this.compileExpression(membership.collection);
    this.emitRecall(scratch, "reuse cell bit mask", line);
    this.emitOp(0x37, "К ∧", "membership test with reused mask", line);
    this.emitOp(0x35, "К {x}", "membership fraction", line);
  }

  private emitBitSetWithScratch(
    membership: BitMembershipCondition,
    set: Extract<StatementAst, { kind: "assign" }>,
    scratch: string,
  ): void {
    this.emitBitSetCollectionWithScratch(membership.collection, set, scratch);
  }

  private emitBitSetCollectionWithScratch(
    collection: ExpressionAst,
    set: Extract<StatementAst, { kind: "assign" }>,
    scratch: string,
  ): void {
    this.compileExpression(collection);
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

  private membershipSetRunPrefix(
    statements: StatementAst[],
    membership: BitMembershipCondition,
  ): {
    sets: Array<{
      set: Extract<StatementAst, { kind: "assign" }>;
      collection: ExpressionAst;
    }>;
    tail: StatementAst[];
  } | undefined {
    const sets: Array<{
      set: Extract<StatementAst, { kind: "assign" }>;
      collection: ExpressionAst;
    }> = [];
    let index = 0;
    while (index < statements.length) {
      const statement = statements[index];
      if (statement?.kind !== "assign") break;
      const matched = matchAnyBitSetAssignment(statement, membership);
      if (matched === undefined) break;
      sets.push({ set: statement, collection: matched.collection });
      index += 1;
    }
    if (sets.length < 2) return undefined;
    return {
      sets,
      tail: statements.slice(index),
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
        detail: `Reordered ${optimized.reordered} numeric dispatch case${optimized.reordered === 1 ? "" : "s"} to shorten residual comparisons.`,
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
    if (!dispatchUsesNumericResidualChain(statement)) return false;
    const numericValues = statement.cases.map((dispatchCase) => numericLiteralValue(dispatchCase.value)) as number[];

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

  private statementsEndMachineFlow(statements: StatementAst[]): boolean {
    return this.statementListEndsMachineFlow(statements, new Set());
  }

  private statementListTerminates(statements: StatementAst[], seenProcs: Set<string>): boolean {
    const last = statements.at(-1);
    if (!last) return false;
    return this.statementTerminates(last, seenProcs);
  }

  private statementListEndsMachineFlow(statements: StatementAst[], seenProcs: Set<string>): boolean {
    const last = statements.at(-1);
    if (!last) return false;
    return this.statementEndsMachineFlow(last, seenProcs);
  }

  private statementTerminates(statement: StatementAst, seenProcs: Set<string>): boolean {
    if (statement.kind === "halt" || statement.kind === "loop" || statement.kind === "trap" || statement.kind === "decimal_series") {
      return true;
    }
    if (statement.kind === "while") return false;
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

  private statementEndsMachineFlow(statement: StatementAst, seenProcs: Set<string>): boolean {
    if (statement.kind === "loop" || statement.kind === "trap" || statement.kind === "decimal_series") return true;
    if (statement.kind === "while") return false;
    if (statement.kind === "halt") return statement.literal !== undefined;
    if (statement.kind === "if") {
      return statement.elseBody !== undefined &&
        this.statementListEndsMachineFlow(statement.thenBody, new Set(seenProcs)) &&
        this.statementListEndsMachineFlow(statement.elseBody, new Set(seenProcs));
    }
    if (statement.kind === "dispatch") {
      return statement.defaultBody !== undefined &&
        this.statementListEndsMachineFlow(statement.defaultBody, new Set(seenProcs)) &&
        statement.cases.every((dispatchCase) =>
          this.statementListEndsMachineFlow(dispatchCase.body, new Set(seenProcs))
        );
    }
    if (statement.kind !== "call") return false;
    const block = this.ast.blocks.find((candidate) => candidate.name === statement.block);
    if (block !== undefined) return block.mode !== "inline";
    const proc = this.ast.procs.find((candidate) => candidate.name === statement.block);
    if (proc === undefined || seenProcs.has(proc.name)) return false;
    seenProcs.add(proc.name);
    return this.statementListEndsMachineFlow(proc.body, seenProcs);
  }

  private compileShow(displayName: string, line: number): void {
    const display = this.ast.displays.find((candidate) => candidate.name === displayName);
    if (!display) {
      this.diagnostics.push(buildDiagnostic("error", `Unknown display '${displayName}'.`, line));
      return;
    }

    const literalHelper = this.sharedLiteralDisplayHelper(display, line);
    if (literalHelper !== undefined) {
      this.emitJump(0x53, "ПП", literalHelper.label, `show ${display.name}`, line);
      this.optimizations.push({
        name: "screen-video-literal-helper-call",
        detail: `Reused shared literal video helper for screen ${display.name}.`,
      });
      return;
    }
    if (this.compileLiteralDisplay(display, line)) return;
    if (this.compileTextDisplay(display, line)) return;
    if (this.compileDashedCoordReportDisplay(display, line)) return;

    const strategy = this.selectDisplayStrategy(display);
    if (strategy === "packed-display-helper") {
      const helper = this.sharedDisplayHelper(display, line);
      if (helper !== undefined) {
        this.emitJump(0x53, "ПП", helper.label, `show ${display.name}`, line);
        this.optimizations.push({
          name: "packed-display-helper-call",
          detail: `Reused shared packed display helper for screen ${display.name}.`,
        });
        return;
      }
    }
    if (strategy === "display-byte-helper") {
      const helper = this.sharedDisplayByteHelper(display, line);
      if (helper !== undefined) {
        this.emitJump(0x53, "ПП", helper.label, `show ${display.name}`, line);
        this.optimizations.push({
          name: "display-byte-helper-call",
          detail: `Reused shared display-byte helper for screen ${display.name}.`,
        });
        return;
      }
    }

    if (strategy === "packed-storage-reuse" && this.compilePackedStorageReuseDisplay(display, line, true)) return;
    if (strategy === "display-byte-builder" && this.compileDisplayByteBuilder(display, line, true)) return;

    this.compilePackedDisplayBody(display, line, true);
    this.reportPackedDisplayLowering(display);
  }

  private compileDashedCoordReportDisplay(display: ProgramAst["displays"][number], line: number): boolean {
    const template = dashedCoordReportDisplayTemplate(display);
    if (template === undefined) return false;
    const maskRegister = this.allocation.registers[COORD_LIST_DX];
    if (maskRegister === undefined) return false;
    if (!this.displayFieldFitsUnsignedWidth(template.cell) || !this.displayFieldFitsUnsignedWidth(template.bearing)) {
      return false;
    }

    if (this.currentXDashedCoordReportBodyMatches(template)) {
      this.emitDashedCoordReportPackedBodyDisplay(display.name, maskRegister, line);
      this.optimizations.push({
        name: "dashed-coord-report-packed-body",
        detail: `Reused packed --CC-- N body already in X for screen ${display.name}.`,
      });
      this.optimizations.push({
        name: "dashed-coord-report-lowering",
        detail: `Lowered screen ${display.name} as --CC-- N calculator video output.`,
      });
      return true;
    }

    if (this.currentXVariable !== template.bearing.name) {
      this.emitRecall(template.bearing.name, `display ${display.name} bearing`, line);
    }
    this.emitRecall(template.cell.name, `display ${display.name} cell`, line);
    if (this.scaledCoordVariables.has(template.cell.name)) {
      this.emitNumberOrPreload("10");
      this.emitOp(0x12, "*", "display dashed scaled cell restore", line);
    }
    this.emitNumber("4");
    this.emitOp(0x15, "F 10^x", "display dashed cell scale", line);
    this.emitOp(0x12, "*", "display dashed cell shift", line);
    this.emitOp(0x10, "+", "display dashed bearing append", line);
    this.emitNumber("7");
    this.emitOp(0x15, "F 10^x", "display dashed video anchor", line);
    this.emitOp(0x10, "+", "display dashed video body", line);
    this.emitOp(0x60 + registerIndex(maskRegister), `П->X ${maskRegister}`, `display ${display.name} dashed mask`, line);
    this.emitOp(0x39, "К ⊕", "display dashed mask merge", line);
    this.emitOp(0x35, "К {x}", "display dashed video fraction", line);
    this.emitOp(0x0b, "/-/", "display dashed sign", line);
    this.emitOp(0x0c, "ВП", "display dashed exponent entry", line);
    this.emitOp(0x07, "7", "display dashed exponent", line);
    this.emitOp(0x50, "С/П", `show ${display.name}`, line);
    this.optimizations.push({
      name: "dashed-coord-report-lowering",
      detail: `Lowered screen ${display.name} as --CC-- N calculator video output.`,
    });
    return true;
  }

  private currentXDashedCoordReportBodyMatches(template: DashedCoordReportTemplate): boolean {
    const body = this.currentXDashedCoordReportBody;
    return body !== undefined &&
      body.cell.name === template.cell.name &&
      body.cell.width === template.cell.width &&
      body.bearing.name === template.bearing.name &&
      body.bearing.width === template.bearing.width;
  }

  private emitDashedCoordReportPackedBodyDisplay(displayName: string, maskRegister: RegisterName, line: number): void {
    this.emitNumber("7");
    this.emitOp(0x15, "F 10^x", "display dashed video anchor", line);
    this.emitOp(0x10, "+", "display dashed video body", line);
    this.emitOp(0x60 + registerIndex(maskRegister), `П->X ${maskRegister}`, `display ${displayName} dashed mask`, line);
    this.emitOp(0x39, "К ⊕", "display dashed mask merge", line);
    this.emitOp(0x35, "К {x}", "display dashed video fraction", line);
    this.emitOp(0x0b, "/-/", "display dashed sign", line);
    this.emitOp(0x0c, "ВП", "display dashed exponent entry", line);
    this.emitOp(0x07, "7", "display dashed exponent", line);
    this.emitOp(0x50, "С/П", `show ${displayName}`, line);
  }

  private selectDisplayStrategy(display: ProgramAst["displays"][number]): DisplayStrategyVariant | undefined {
    const candidates = this.displayStrategyCandidates(display);
    const available = candidates.filter((candidate) => candidate.available);
    if (available.length === 0) {
      for (const candidate of candidates) {
        this.candidates.push({
          site: `display ${display.name}`,
          variant: candidate.variant,
          steps: candidate.steps,
          selected: false,
          reason: candidate.reason,
        });
      }
      return undefined;
    }
    const selected = available.reduce((best, candidate) => {
      if (candidate.steps < best.steps) return candidate;
      return best;
    });
    for (const candidate of candidates) {
      this.candidates.push({
        site: `display ${display.name}`,
        variant: candidate.variant,
        steps: candidate.steps,
        selected: candidate.variant === selected.variant,
        reason: candidate.variant === selected.variant
          ? `selected ${candidate.variant} for screen ${display.name}`
          : candidate.reason,
      });
    }
    this.optimizations.push({
      name: "display-strategy-selection",
      detail: `Selected ${selected.variant} for screen ${display.name}.`,
    });
    return selected.variant;
  }

  private displayStrategyCandidates(display: ProgramAst["displays"][number]): DisplayStrategyCandidate[] {
    const fields = this.numericDisplayFields(display);
    const sourceFields = this.displaySourceFields(display);
    const decimalCost = fields === undefined
      ? UNAVAILABLE_DISPLAY_STRATEGY_COST
      : this.estimateDecimalDisplayCost(fields, true);
    const helperCost = fields === undefined
      ? UNAVAILABLE_DISPLAY_STRATEGY_COST
      : 2;
    const helperAvailable = fields !== undefined && this.shouldShareDisplay(display);
    const storageFields = this.packedStorageReuseFields(display);
    const storageCost = storageFields === undefined
      ? UNAVAILABLE_DISPLAY_STRATEGY_COST
      : this.estimatePackedStorageReuseCost(storageFields, true);
    const displayByteAvailable = this.canCompileDisplayByteBuilder(display);
    const displayByteCost = sourceFields.length === 0
      ? UNAVAILABLE_DISPLAY_STRATEGY_COST
      : this.estimateDisplayByteBuilderCost(display, sourceFields, true);
    const displayByteHelperAvailable = displayByteAvailable && this.shouldShareDisplayByte(display);
    const displayByteHelperCost = displayByteHelperAvailable ? 2 : UNAVAILABLE_DISPLAY_STRATEGY_COST;

    return [
      {
        variant: "decimal-pack",
        steps: decimalCost,
        available: fields !== undefined,
        reason: fields === undefined
          ? "display contains non-space literal fragments"
          : "ordinary decimal field packing was not the cheapest display strategy",
      },
      {
        variant: "packed-storage-reuse",
        steps: storageCost,
        available: storageFields !== undefined,
        reason: storageFields === undefined
          ? "display fields are not proved to already occupy their decimal positions"
          : "ready-packed storage reuse was not the cheapest display strategy",
      },
      {
        variant: "packed-display-helper",
        steps: helperCost,
        available: helperAvailable,
        reason: helperAvailable
          ? "shared packed display helper was not the cheapest display strategy"
          : "display is not repeated often enough for a shared packed display helper",
      },
      {
        variant: "display-byte-helper",
        steps: displayByteHelperCost,
        available: displayByteHelperAvailable,
        reason: displayByteHelperAvailable
          ? "shared display-byte helper was not the cheapest display strategy"
          : "display is not repeated often enough for a shared display-byte helper",
      },
      {
        variant: "display-byte-builder",
        steps: displayByteCost,
        available: displayByteAvailable,
        reason: displayByteAvailable
          ? "display-byte builder was not the cheapest display strategy"
          : "display has no literal/X2-sensitive fragments to build",
      },
    ];
  }

  private compilePackedDisplayBody(
    display: ProgramAst["displays"][number],
    line: number,
    reuseCurrentX: boolean,
  ): void {
    const fields = this.numericDisplayFields(display, line);
    if (fields === undefined) return;
    this.compilePackedDisplayFields(display, fields, line, reuseCurrentX);
    if (fields.some((field) => field.kind === "literal")) {
      this.optimizations.push({
        name: "display-decimal-literal-field",
        detail: `Packed decimal digit literals directly into screen ${display.name}.`,
      });
    }
    this.emitOp(0x50, "С/П", `show ${display.name}`, line);
  }

  private compilePackedDisplayFields(
    display: ProgramAst["displays"][number],
    fields: DisplayField[],
    line: number,
    reuseCurrentX: boolean,
  ): void {
    const currentIndex = reuseCurrentX && this.currentXVariable !== undefined
      ? fields.findIndex((field) => field.kind === "source" && field.name === this.currentXVariable)
      : -1;
    if (currentIndex > 0) {
      const current = fields[currentIndex]!;
      this.compilePackedDisplayFieldsInOrder(display, fields.slice(0, currentIndex), line, false);
      this.emitNumberOrPreload(String(10 ** current.width));
      this.emitOp(0x12, "*", "packed display field shift", line);
      this.emitOp(0x10, "+", "packed display current field append", line);
      for (const field of fields.slice(currentIndex + 1)) {
        this.emitNumberOrPreload(String(10 ** field.width));
        this.emitOp(0x12, "*", "packed display field shift", line);
        if (field.kind === "source" || field.value !== "0") {
          this.emitDisplayFieldValue(display, field, line);
          this.emitOp(0x10, "+", "packed display field append", line);
        }
      }
      this.optimizations.push({
        name: currentIndex === fields.length - 1 ? "display-current-x-suffix-reuse" : "display-current-x-middle-reuse",
        detail: `Reused ${current.name} already in X as field ${currentIndex + 1} of screen ${display.name}.`,
      });
      return;
    }

    const orderedFields = reuseCurrentX && this.canReorderNumericDisplay(display)
      ? this.orderDisplaySources(fields.map((field) => field.name))
        .map((source) => fields.find((field) => field.name === source)!)
      : fields;

    this.compilePackedDisplayFieldsInOrder(
      display,
      orderedFields,
      line,
      reuseCurrentX,
    );
  }

  private compilePackedDisplayFieldsInOrder(
    display: ProgramAst["displays"][number],
    fields: DisplayField[],
    line: number,
    reuseCurrentX: boolean,
  ): void {
    if (fields.length === 0) {
      this.emitNumber("0");
    } else {
      for (let index = 0; index < fields.length; index += 1) {
        const field = fields[index]!;
        if (index === 0 && reuseCurrentX && field.kind === "source" && field.name === this.currentXVariable) {
          this.optimizations.push({
            name: "display-current-x-reuse",
            detail: `Reused ${field.name} already in X as the first field of screen ${display.name}.`,
          });
          continue;
        }
        if (index === 0) {
          this.emitDisplayFieldValue(display, field, line);
        } else {
          this.emitNumberOrPreload(String(10 ** field.width));
          this.emitOp(0x12, "*", "packed display field shift", line);
          if (field.kind === "source" || field.value !== "0") {
            this.emitDisplayFieldValue(display, field, line);
            this.emitOp(0x10, "+", "packed display field append", line);
          }
        }
      }
    }
  }

  private emitDisplayFieldValue(
    display: ProgramAst["displays"][number],
    field: DisplayField,
    line: number,
  ): void {
    if (field.kind === "literal") {
      this.emitNumberOrPreload(field.value ?? "0");
      const last = this.items.at(-1);
      if (last?.kind === "op") last.comment = `display ${display.name} digit literal`;
      return;
    }
    this.emitRecall(field.name, `display ${display.name} source`, line);
  }

  private numericDisplayFields(
    display: ProgramAst["displays"][number],
    line?: number,
  ): DisplayField[] | undefined {
    const fields: DisplayField[] = [];
    for (const item of display.items) {
      if (item.kind === "literal") {
        const literal = decimalDisplayFieldLiteral(item.text, fields.length === 0);
        if (literal !== undefined) {
          fields.push({ kind: "literal", name: `#${literal.digits}`, width: literal.width, value: literal.value });
          continue;
        }
        if (line !== undefined) {
          this.diagnostics.push(buildDiagnostic(
            "error",
            `Screen '${display.name}' contains display literal ${JSON.stringify(item.text)} that is not lowerable yet.`,
            line,
          ));
        }
        return undefined;
      }
      fields.push({ kind: "source", item, name: item.name, width: item.width ?? this.naturalDisplayWidth(item.name) });
    }
    return fields;
  }

  private displaySourceFields(display: ProgramAst["displays"][number]): DisplayField[] {
    return display.items
      .filter((item): item is DisplaySourceItem => item.kind === "source")
      .map((item) => ({ kind: "source", item, name: item.name, width: item.width ?? this.naturalDisplayWidth(item.name) }));
  }

  private estimateDecimalDisplayCost(fields: DisplayField[], reuseCurrentX: boolean): number {
    if (fields.length === 0) return 2;
    const currentIndex = reuseCurrentX && this.currentXVariable !== undefined
      ? fields.findIndex((field) => field.kind === "source" && field.name === this.currentXVariable)
      : -1;
    if (currentIndex > 0) {
      let cost = this.estimateDisplayFieldValueCost(fields[0]!);
      for (const field of fields.slice(1, currentIndex)) {
        cost += this.estimateNumberOrPreloadCost(String(10 ** field.width)) +
          (field.kind === "literal" && field.value === "0" ? 1 : this.estimateDisplayFieldValueCost(field) + 2);
      }
      cost += this.estimateNumberOrPreloadCost(String(10 ** fields[currentIndex]!.width)) + 2;
      for (const field of fields.slice(currentIndex + 1)) {
        cost += this.estimateNumberOrPreloadCost(String(10 ** field.width)) +
          (field.kind === "literal" && field.value === "0" ? 1 : this.estimateDisplayFieldValueCost(field) + 2);
      }
      return cost + 1;
    }
    let cost = reuseCurrentX && fields[0]?.kind === "source" && fields[0].name === this.currentXVariable
      ? 0
      : this.estimateDisplayFieldValueCost(fields[0]!);
    for (const field of fields.slice(1)) {
      cost += this.estimateNumberOrPreloadCost(String(10 ** field.width)) +
        (field.kind === "literal" && field.value === "0" ? 1 : this.estimateDisplayFieldValueCost(field) + 2);
    }
    return cost + 1;
  }

  private estimateDisplayFieldValueCost(field: DisplayField): number {
    return field.kind === "literal" ? this.estimateNumberOrPreloadCost(field.value ?? "0") : 1;
  }

  private estimateNumberOrPreloadCost(raw: string): number {
    return this.allocation.constants[normalizeConstantLiteral(raw)] === undefined ? estimateNumberCost(raw) : 1;
  }

  private packedStorageReuseFields(display: ProgramAst["displays"][number]): DisplayField[] | undefined {
    const fields = this.numericDisplayFields(display);
    if (fields === undefined || fields.length < 2) return undefined;
    if (fields.some((field) => field.kind !== "source")) return undefined;
    const firstState = this.findStateField(fields[0]!.name);
    if (firstState?.type !== "packed") return undefined;
    if (!fields.slice(1).every((field) => field.item?.width !== undefined)) return undefined;
    return fields;
  }

  private estimatePackedStorageReuseCost(fields: DisplayField[], reuseCurrentX: boolean): number {
    if (fields.length === 0) return 2;
    const currentIndex = reuseCurrentX && this.currentXVariable !== undefined
      ? fields.findIndex((field) => field.name === this.currentXVariable)
      : -1;
    const recalled = currentIndex >= 0 ? fields.length - 1 : fields.length;
    return recalled + Math.max(0, fields.length - 1) + 1;
  }

  private compilePackedStorageReuseDisplay(
    display: ProgramAst["displays"][number],
    line: number,
    reuseCurrentX: boolean,
  ): boolean {
    const fields = this.packedStorageReuseFields(display);
    if (fields === undefined) return false;
    const ordered = this.orderStorageReuseFields(fields, reuseCurrentX);
    for (let index = 0; index < ordered.length; index += 1) {
      const field = ordered[index]!;
      if (!(index === 0 && reuseCurrentX && field.name === this.currentXVariable)) {
        this.emitRecall(field.name, `display ${display.name} packed field`, line);
      }
      if (index > 0) this.emitOp(0x10, "+", "packed display storage append", line);
    }
    this.emitOp(0x50, "С/П", `show ${display.name}`, line);
    this.optimizations.push({
      name: "packed-display-storage-reuse",
      detail: `Displayed screen ${display.name} by adding fields already stored in their decimal positions.`,
    });
    return true;
  }

  private orderStorageReuseFields(fields: DisplayField[], reuseCurrentX: boolean): DisplayField[] {
    if (!reuseCurrentX || this.currentXVariable === undefined) return fields;
    const index = fields.findIndex((field) => field.name === this.currentXVariable);
    if (index <= 0) return fields;
    this.optimizations.push({
      name: "display-stack-reuse",
      detail: `Reordered ready-packed display inputs to reuse ${this.currentXVariable} already in X.`,
    });
    return [
      fields[index]!,
      ...fields.slice(0, index),
      ...fields.slice(index + 1),
    ];
  }

  private canCompileDisplayByteBuilder(display: ProgramAst["displays"][number]): boolean {
    if (!machineSupports(this.machineProfile, "display-bytes")) return false;
    return this.mantissaExponentDisplayTemplate(display) !== undefined &&
      this.displayTemplateScratchRegisters(display) !== undefined ||
      this.mantissaMaskDisplayTemplate(display) !== undefined &&
      this.displayMaskScratchRegister(display) !== undefined &&
      this.displayMaskRegister(display) !== undefined;
  }

  private estimateDisplayByteBuilderCost(
    display: ProgramAst["displays"][number],
    _fields: DisplayField[],
    reuseCurrentX: boolean,
  ): number {
    const template = this.mantissaExponentDisplayTemplate(display);
    if (template !== undefined) {
      const leaderCost = reuseCurrentX && template.leader.name === this.currentXVariable ? 0 : 1;
      return 39 + leaderCost;
    }
    const maskTemplate = this.mantissaMaskDisplayTemplate(display);
    if (maskTemplate === undefined) return UNAVAILABLE_DISPLAY_STRATEGY_COST;
    return this.estimateDecimalDisplayCost(maskTemplate.bodyFields, false) + 9 +
      String(maskTemplate.width - 1).length;
  }

  private compileDisplayByteBuilder(
    display: ProgramAst["displays"][number],
    line: number,
    _reuseCurrentX: boolean,
  ): boolean {
    const template = this.mantissaExponentDisplayTemplate(display);
    if (template === undefined) return this.compileMantissaMaskDisplay(display, line, _reuseCurrentX);
    const scratch = this.displayTemplateScratchRegisters(display);
    if (scratch === undefined) return false;

    this.emitRecall(template.score.name, `display ${display.name} score`, line);
    this.emitNumberOrPreload("1000");
    this.emitOp(0x13, "/", "display template score shift", line);
    this.emitRecall(template.total.name, `display ${display.name} total`, line);
    this.emitNumberOrPreload("10000000");
    this.emitOp(0x13, "/", "display template total shift", line);
    this.emitOp(0x10, "+", "display template total append", line);
    this.emitOp(0x09, "9", "display template numeric anchor", line);
    this.emitOp(0x10, "+", "display template numeric body", line);
    this.emitRecall(scratch.mask, `display ${display.name} separator mask`, line);
    this.emitOp(0x38, "К ∨", "display template body merge", line);
    const exponentCanBeZero = this.displayFieldCanBeZero(template.exponent);
    this.emitStore(scratch.value, `display ${display.name} body`, line);

    const exponentZero = exponentCanBeZero ? this.freshLabel("display_exponent_zero") : undefined;
    this.emitRecall(template.exponent.name, `display ${display.name} exponent`, line);
    if (exponentZero !== undefined) {
      this.emitJump(0x57, "F x!=0", exponentZero, "display template zero exponent", line);
    }
    this.emitStore(scratch.loop, `display ${display.name} exponent counter`, line);
    this.emitRecall(scratch.value, `display ${display.name} body`, line);
    const loopStart = this.freshLabel("display_exponent_loop");
    this.emitLabel(loopStart);
    this.emitOp(0x0c, "ВП", "display template exponent entry", line);
    this.emitOp(0x01, "1", "display template exponent digit", line);
    this.emitOp(0x0b, "/-/", "display template exponent sign", line);
    this.emitJump(displayLoopOpcode(scratch.loopRegister), `F L${scratch.loopRegister}`, loopStart, "display template exponent loop", line);
    this.emitStore(scratch.value, `display ${display.name} exponent body`, line);
    if (exponentZero !== undefined) this.emitLabel(exponentZero);

    this.emitRecall(template.leader.name, `display ${display.name} leader`, line);
    this.emitRecall(scratch.value, `display ${display.name} body`, line);
    this.emitOp(0x14, "<->", "display template leader merge", line);
    this.emitOp(0x54, "К НОП", "display template leader preserve", line, true);
    this.emitOp(0x0c, "ВП", "display template leader restore", line);
    this.emitOp(0x50, "С/П", `show ${display.name}`, line);
    this.optimizations.push({
      name: "display-byte-x2-lowering",
      detail: `Built literal-separated screen ${display.name} through a mantissa/exponent video template.`,
    });
    return true;
  }

  private compileMantissaMaskDisplay(
    display: ProgramAst["displays"][number],
    line: number,
    _reuseCurrentX: boolean,
  ): boolean {
    const template = this.mantissaMaskDisplayTemplate(display);
    const scratch = this.displayMaskScratchRegister(display);
    const maskRegister = this.displayMaskRegister(display);
    if (template === undefined || scratch === undefined || maskRegister === undefined) return false;

    this.compilePackedDisplayFields(display, template.bodyFields, line, false);
    this.emitOp(0x60 + registerIndex(maskRegister), `П->X ${maskRegister}`, `display ${display.name} literal mask`, line);
    this.emitOp(0x38, "К ∨", "display mask body merge", line);
    this.emitOp(0x40 + registerIndex(scratch), `X->П ${scratch}`, `display ${display.name} body`, line, true);
    this.emitRecall(template.leader.name, `display ${display.name} leader`, line);
    this.emitOp(0x60 + registerIndex(scratch), `П->X ${scratch}`, `display ${display.name} body`, line);
    this.emitOp(0x14, "<->", "display mask leader merge", line);
    this.emitOp(0x54, "К НОП", "display mask leader preserve", line, true);
    this.emitOp(0x0c, "ВП", "display mask leader restore", line);
    this.emitDisplayExponent(template.width - 1, line, "display mask exponent");
    this.emitOp(0x50, "С/П", `show ${display.name}`, line);
    this.optimizations.push({
      name: "display-byte-mask-lowering",
      detail: `Built literal-separated screen ${display.name} through a calculator video mask.`,
    });
    return true;
  }

  private mantissaExponentDisplayTemplate(
    display: ProgramAst["displays"][number],
  ): MantissaExponentDisplayTemplate | undefined {
    const [leader, firstLiteral, score, secondLiteral, total, thirdLiteral, exponent] = display.items;
    if (
      leader?.kind !== "source" ||
      firstLiteral?.kind !== "literal" ||
      score?.kind !== "source" ||
      secondLiteral?.kind !== "literal" ||
      total?.kind !== "source" ||
      thirdLiteral?.kind !== "literal" ||
      exponent?.kind !== "source" ||
      display.items.length !== 7
    ) {
      return undefined;
    }
    if (normalizeDisplayTemplateLiteral(firstLiteral.text) !== ".-") return undefined;
    if (normalizeDisplayTemplateLiteral(secondLiteral.text) !== "-") return undefined;
    if (normalizeDisplayTemplateLiteral(thirdLiteral.text) !== "-") return undefined;

    const result: MantissaExponentDisplayTemplate = {
      leader: { kind: "source", item: leader, name: leader.name, width: leader.width ?? this.naturalDisplayWidth(leader.name) },
      score: { kind: "source", item: score, name: score.name, width: score.width ?? this.naturalDisplayWidth(score.name) },
      total: { kind: "source", item: total, name: total.name, width: total.width ?? this.naturalDisplayWidth(total.name) },
      exponent: { kind: "source", item: exponent, name: exponent.name, width: exponent.width ?? this.naturalDisplayWidth(exponent.name) },
    };
    if (result.leader.width !== 1 || result.score.width !== 2 || result.total.width !== 3 || result.exponent.width !== 2) {
      return undefined;
    }
    if (!this.displayFieldFitsUnsignedWidth(result.leader)) return undefined;
    if (!this.displayFieldFitsUnsignedWidth(result.score)) return undefined;
    if (!this.displayFieldFitsUnsignedWidth(result.total)) return undefined;
    if (!this.displayFieldFitsUnsignedWidth(result.exponent)) return undefined;
    return result;
  }

  private mantissaMaskDisplayTemplate(
    display: ProgramAst["displays"][number],
  ): MantissaMaskDisplayTemplate | undefined {
    const [first, ...rest] = display.items;
    if (first?.kind !== "source" || rest.length === 0) return undefined;

    const leader: DisplayField = {
      kind: "source",
      item: first,
      name: first.name,
      width: first.width ?? this.naturalDisplayWidth(first.name),
    };
    if (leader.width !== 1 || !this.displayFieldFitsUnsignedWidth(leader)) return undefined;
    const leaderMin = this.displayFieldMin(leader);
    if (leaderMin === undefined || leaderMin <= 0) return undefined;

    const bodyFields: DisplayField[] = [
      { kind: "literal", name: "#display-anchor", width: 1, value: "9" },
    ];
    const maskCells = [8];
    let width = 1;
    let hasVideoLiteral = false;

    for (const item of rest) {
      if (item.kind === "source") {
        const field: DisplayField = {
          kind: "source",
          item,
          name: item.name,
          width: item.width ?? this.naturalDisplayWidth(item.name),
        };
        if (!this.displayFieldFitsUnsignedWidth(field)) return undefined;
        bodyFields.push(field);
        for (let index = 0; index < field.width; index += 1) maskCells.push(0);
        width += field.width;
        continue;
      }

      const cells = displayLiteralMantissaCells(item.text);
      if (cells === undefined) return undefined;
      if (cells.some((cell) => cell > 9)) hasVideoLiteral = true;
      if (cells.length === 0) continue;
      bodyFields.push({ kind: "literal", name: "#display-literal-gap", width: cells.length, value: "0" });
      maskCells.push(...cells);
      width += cells.length;
    }

    if (!hasVideoLiteral || width < 2 || width > 8) return undefined;
    return {
      leader,
      bodyFields,
      mask: displayCellsLiteral(maskCells),
      width,
    };
  }

  private displayFieldFitsUnsignedWidth(field: DisplayField): boolean {
    const state = this.findStateField(field.name);
    if (state === undefined) return false;
    const min = state.min ?? 0;
    const max = state.max ?? min;
    return min >= 0 && max < 10 ** field.width;
  }

  private displayFieldCanBeZero(field: DisplayField): boolean {
    const state = this.findStateField(field.name);
    return state === undefined || (state.min ?? 0) <= 0;
  }

  private displayFieldMin(field: DisplayField): number | undefined {
    return this.findStateField(field.name)?.min;
  }

  private displayTemplateScratchRegisters(display: ProgramAst["displays"][number]): {
    value: string;
    loop: string;
    loopRegister: 0 | 1 | 2 | 3;
    mask: string;
  } | undefined {
    const value = displayTemplateValueScratchName(display);
    const loop = displayTemplateLoopScratchName(display);
    const mask = displayTemplateMaskScratchName(display);
    const loopRegister = this.allocation.registers[loop];
    if (
      this.allocation.registers[value] === undefined ||
      this.allocation.registers[mask] === undefined ||
      loopRegister === undefined
    ) {
      return undefined;
    }
    if (loopRegister !== "0" && loopRegister !== "1" && loopRegister !== "2" && loopRegister !== "3") return undefined;
    return { value, loop, loopRegister: Number(loopRegister) as 0 | 1 | 2 | 3, mask };
  }

  private displayMaskScratchRegister(display: ProgramAst["displays"][number]): RegisterName | undefined {
    return this.allocation.registers[displayTemplateValueScratchName(display)];
  }

  private displayMaskRegister(display: ProgramAst["displays"][number]): RegisterName | undefined {
    const template = this.mantissaMaskDisplayTemplate(display);
    return template === undefined ? undefined : this.allocation.constants[normalizeConstantLiteral(template.mask)];
  }

  private emitDisplayLiteralProgram(
    program: Exclude<DisplayLiteralProgram, { kind: "error" }>,
    line: number | undefined,
    comment: string,
  ): void {
    if (program.kind === "kinv") {
      this.emitNumberOrPreload(program.digits);
      this.emitOp(0x3a, "К ИНВ", comment, line);
      if (program.negative) this.emitOp(0x0b, "/-/", `${comment} sign`, line);
      return;
    }
    this.emitNumberOrPreload(program.left);
    this.emitOp(0x0e, "В↑", `${comment} split`, line);
    this.emitNumberOrPreload(program.right);
    this.emitOp(0x39, "К ⊕", comment, line);
    if (program.negative) this.emitOp(0x0b, "/-/", `${comment} sign`, line);
  }

  private emitFirstSpliceDisplayLiteralProgram(
    program: FirstSpliceDisplayLiteralProgram,
    tempRegister: RegisterName,
    line: number | undefined,
    comment: string,
  ): void {
    this.emitDisplayLiteralProgram(program.body, line, `${comment} body`);
    this.emitOp(0x40 + registerIndex(tempRegister), `X->П ${tempRegister}`, `${comment} body scratch`, line, true);
    if (program.first === 8) {
      this.emitOp(0x60 + registerIndex(tempRegister), `П->X ${tempRegister}`, `${comment} body scratch`, line);
      if (program.negative) this.emitOp(0x0b, "/-/", `${comment} sign`, line);
      this.emitOp(0x54, "К НОП", `${comment} first digit reuse`, line, true);
      this.emitOp(0x0c, "ВП", `${comment} first digit reuse`, line);
      this.emitDisplayExponent(program.exponent, line, `${comment} exponent`);
      this.optimizations.push({
        name: "display-literal-first-digit-reuse",
        detail: "Reused the literal body's leading 8 while restoring X2.",
      });
      return;
    }
    if (program.first === 10 && program.second === 10) {
      this.emitOp(0x35, "К {x}", `${comment} first digit from body`, line);
      this.emitOp(0x60 + registerIndex(tempRegister), `П->X ${tempRegister}`, `${comment} body scratch`, line);
      if (program.negative) this.emitOp(0x0b, "/-/", `${comment} sign`, line);
      this.emitFirstDigitSplice(line);
      this.emitDisplayExponent(program.exponent, line, `${comment} exponent`);
      this.optimizations.push({
        name: "display-literal-minus-source-reuse",
        detail: "Derived a leading '-' from the literal body's fractional tail.",
      });
      return;
    }
    this.emitDisplayFirstDigit(program.first, line, `${comment} first digit`);
    this.emitOp(0x60 + registerIndex(tempRegister), `П->X ${tempRegister}`, `${comment} body scratch`, line);
    if (program.negative) this.emitOp(0x0b, "/-/", `${comment} sign`, line);
    this.emitFirstDigitSplice(line);
    this.emitDisplayExponent(program.exponent, line, `${comment} exponent`);
  }

  private emitDisplayFirstDigit(cell: number, line: number | undefined, comment: string): void {
    if (cell >= 0 && cell <= 9) {
      this.emitNumber(String(cell));
      const last = this.items.at(-1);
      if (last?.kind === "op") last.comment = comment;
      return;
    }
    if (cell >= 10 && cell <= 14) {
      this.emitNumber(`1${15 - cell}`);
      this.emitOp(0x3a, "К ИНВ", comment, line);
      this.emitOp(0x35, "К {x}", comment, line);
      return;
    }
    this.diagnostics.push(buildDiagnostic("error", `Unsupported display first digit ${cell}.`, line));
  }

  private emitDisplayExponent(exponent: number, line: number | undefined, comment: string): void {
    if (!Number.isInteger(exponent) || exponent < 0 || exponent > 99) {
      this.diagnostics.push(buildDiagnostic("error", `Unsupported display exponent ${exponent}.`, line));
      return;
    }
    this.emitOp(0x0c, "ВП", comment, line);
    for (const char of String(exponent)) {
      this.emitOp(Number(char), char, comment, line);
    }
  }

  private canReorderNumericDisplay(display: ProgramAst["displays"][number]): boolean {
    return display.sources.length <= 1 && display.items.every((item) => item.kind === "source" && item.width === undefined);
  }

  private naturalDisplayWidth(source: string): number {
    const field = this.findStateField(source);
    if (field === undefined) return 1;
    const min = field.min ?? 0;
    const max = field.max ?? min;
    const magnitude = Math.max(Math.abs(min), Math.abs(max));
    return Math.max(1, String(Math.trunc(magnitude)).length);
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

  private sharedDisplayByteHelper(
    display: ProgramAst["displays"][number],
    line: number,
  ): { display: ProgramAst["displays"][number]; label: string; line: number } | undefined {
    if (!this.shouldShareDisplayByte(display)) return undefined;
    const existing = this.displayByteHelpers.get(display.name);
    if (existing !== undefined) return existing;
    const helper = {
      display,
      label: `__display_byte_${display.name}`,
      line,
    };
    this.displayByteHelpers.set(display.name, helper);
    return helper;
  }

  private shouldShareDisplayByte(display: ProgramAst["displays"][number]): boolean {
    if (!this.canCompileDisplayByteBuilder(display)) return false;
    const uses = this.displayUseCounts.get(display.name) ?? 0;
    if (uses < 2) return false;
    const bodyCost = this.estimateDisplayByteBuilderCost(display, this.displaySourceFields(display), false);
    const helperCost = uses * 2 + bodyCost + 1;
    const inlineTotal = uses * bodyCost;
    return inlineTotal - helperCost >= DISPLAY_HELPER_MIN_SAVINGS;
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
    const fields = this.numericDisplayFields(display);
    if (fields === undefined) return false;
    const sources = fields.length;
    if (sources < 2) return false;
    const uses = this.displayUseCounts.get(display.name) ?? 0;
    if (uses < 2) return false;

    const inlineCost = estimatePackedDisplayBodyCost(fields.map((field) => field.width));
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
    const firstFields = this.numericDisplayFields(first);
    const secondFields = this.numericDisplayFields(second);
    if (firstFields === undefined || secondFields === undefined) return false;
    const uses = this.showSequenceUseCounts.get(showSequenceKey(first.name, second.name)) ?? 0;
    if (uses < 2) return false;
    const bodyCost = estimatePackedDisplayBodyCost(firstFields.map((field) => field.width)) + estimatePackedDisplayBodyCost(secondFields.map((field) => field.width));
    const inlineTotal = uses * (bodyCost + 1);
    const helperTotal = uses * 3 + bodyCost + 1;
    return inlineTotal - helperTotal >= DISPLAY_HELPER_MIN_SAVINGS;
  }

  private compileTextDisplay(display: ProgramAst["displays"][number], line: number): boolean {
    const normalized = this.collapseTextPrefixDisplay(display);
    if (normalized === undefined) return false;
    const { text, source } = normalized;
    if (
      text !== "BEEr " ||
      source.width !== undefined && source.width !== 2
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
      detail: `Lowered screen ${display.name} as visible text ${JSON.stringify(text)} plus ${source.name}.`,
    });
    return true;
  }

  private compileLiteralDisplay(display: ProgramAst["displays"][number], line: number): boolean {
    const literal = this.collapseLiteralOnlyDisplay(display);
    if (literal === undefined) return false;
    const compiled = this.compileLiteralDisplayBody(display, line, literal);
    if (!compiled) return false;
    this.optimizations.push({
      name: literal.length === 0 ? "screen-empty-literal-lowering" : "screen-video-literal-lowering",
      detail: literal.length === 0
        ? `Lowered empty screen ${display.name} as a plain pause.`
        : `Lowered screen ${display.name} as a literal calculator video string.`,
    });
    return true;
  }

  private compileLiteralDisplayBody(
    display: ProgramAst["displays"][number],
    line: number,
    literal = this.collapseLiteralOnlyDisplay(display),
  ): boolean {
    if (literal === undefined) return false;
    if (literal.length === 0) {
      this.emitOp(0x50, "С/П", `show ${display.name}`, line);
      return true;
    }
    if (this.compilePreloadedDisplayLiteral(display, literal, line)) return true;
    const program = displayLiteralProgram(literal);
    if (program !== undefined) {
      if (program.kind === "error") {
        this.emitErrorStopOpcode(`show ${display.name}`, line, true);
        this.emitOp(0x54, "К НОП", `show ${display.name} skipped after error pause`, line, true);
        this.optimizations.push({
          name: "screen-error-literal-lowering",
          detail: `Lowered screen ${display.name} as a resumable ЕГГ0Г pause with a skipped padding cell.`,
        });
      } else if (program.kind === "kinv") {
        this.emitNumberOrPreload(program.digits);
        this.emitOp(0x3a, "К ИНВ", "display literal video bytes", line);
      } else {
        this.emitNumberOrPreload(program.left);
        this.emitOp(0x0e, "В↑", "display literal x/y split", line);
        this.emitNumberOrPreload(program.right);
        this.emitOp(0x39, "К ⊕", "display literal video bytes", line);
      }
      if (program.kind !== "error" && program.negative) {
        this.emitOp(0x0b, "/-/", "display literal sign", line);
      }
      if (program.kind === "error") return true;
      this.emitOp(0x50, "С/П", `show ${display.name}`, line);
      return true;
    }
    if (this.compileDecimalLiteralDisplay(display, literal, line)) return true;
    if (this.compileZeroDigitTailDisplay(display, literal, line)) return true;
    if (this.compileSignDigitLiteralDisplay(display, literal, line)) return true;
    const firstSplice =
      signedFirstSpliceDisplayLiteralProgram(literal) ??
      exponentTailDisplayLiteralProgram(literal) ??
      firstSpliceDisplayLiteralProgram(literal);
    if (firstSplice !== undefined) {
      const scratch = this.firstSpliceDisplayScratch(display);
      if (scratch !== undefined) {
        this.emitFirstSpliceDisplayLiteralProgram(firstSplice, scratch, line, "display literal video bytes");
        this.emitOp(0x50, "С/П", `show ${display.name}`, line);
        this.optimizations.push({
          name: "screen-text-literal-first-splice",
          detail: `Lowered screen ${display.name} by building a literal mantissa and splicing its first digit.`,
        });
        return true;
      }
    }
    return false;
  }

  private compilePreloadedDisplayLiteral(
    display: ProgramAst["displays"][number],
    literal: string,
    line: number,
  ): boolean {
    if (!shouldUsePreloadedDisplayLiteral(literal)) return false;
    const register = this.allocation.constants[normalizeConstantLiteral(literal)];
    if (register === undefined) return false;
    this.emitOp(0x60 + registerIndex(register), `П->X ${register}`, `display ${display.name} literal`, line);
    this.emitOp(0x50, "С/П", `show ${display.name}`, line);
    this.optimizations.push({
      name: "screen-text-literal-preload",
      detail: `Displayed screen ${display.name} from prebuilt literal R${register}.`,
    });
    return true;
  }

  private firstSpliceDisplayScratch(display: ProgramAst["displays"][number]): RegisterName | undefined {
    return this.allocation.registers[firstSpliceDisplayScratchName(display)];
  }

  private compileDecimalLiteralDisplay(
    display: ProgramAst["displays"][number],
    literal: string,
    line: number,
  ): boolean {
    const value = decimalDisplayLiteralNumber(literal);
    if (value === undefined) return false;
    this.emitNumber(value);
    this.emitOp(0x50, "С/П", `show ${display.name}`, line);
    this.optimizations.push({
      name: "screen-decimal-literal-lowering",
      detail: `Lowered screen ${display.name} as an ordinary decimal display literal.`,
    });
    return true;
  }

  private compileZeroDigitTailDisplay(
    display: ProgramAst["displays"][number],
    literal: string,
    line: number,
  ): boolean {
    const program = zeroDigitTailDisplayProgram(literal);
    if (program === undefined) return false;
    if (!this.scratchRegistersAvailable(new Set<RegisterName>(["9", "c"]))) return false;

    this.emitNumber(String(program.input));
    this.emitOp(0x54, "К НОП", "display zero-digit tail seed", line, true);
    this.emitNumber("50");
    this.emitOp(0x15, "F 10^x", "display zero-digit tail monster", line);
    this.emitOp(0x22, "F x^2", "display zero-digit tail monster", line);
    this.emitOp(0x22, "F x^2", "display zero-digit tail monster", line);
    this.emitOp(0x22, "F x^2", "display zero-digit tail monster", line);
    this.emitOp(0x12, "*", "display zero-digit tail monster", line);
    this.emitOp(0x49, "X->П 9", "display zero-digit tail scratch", line, true);
    this.emitOp(0x69, "П->X 9", "display zero-digit tail scratch", line);
    this.emitOp(0x6c, "П->X c", "display zero-digit tail hidden tail", line);
    this.emitOp(0x0c, "ВП", "display zero-digit tail restore", line);
    this.emitOp(0x07, "7", "display zero-digit tail exponent", line);
    this.emitOp(0x50, "С/П", `show ${display.name}`, line);
    this.optimizations.push({
      name: "screen-zero-digit-tail-lowering",
      detail: `Lowered screen ${display.name} through the 0C tail sign-digit display trick.`,
    });
    return true;
  }

  private compileSignDigitLiteralDisplay(
    display: ProgramAst["displays"][number],
    literal: string,
    line: number,
  ): boolean {
    const program = signDigitLiteralDisplayProgram(literal);
    if (program === undefined) return false;
    const scratch = this.signDigitLiteralScratch();
    if (scratch === undefined) return false;

    this.emitNumber("11");
    this.emitOp(0x3a, "К ИНВ", "display sign-digit E source", line);
    this.emitOp(0x35, "К {x}", "display sign-digit E source", line);
    this.emitOp(0x40 + registerIndex(scratch.source), `X->П ${scratch.source}`, "display sign-digit E source", line, true);

    this.emitOp(0x60 + registerIndex(scratch.source), `П->X ${scratch.source}`, "display sign-digit source", line);
    this.emitNumber(program.start);
    this.emitFirstDigitSplice(line);

    for (let index = 0; index < program.indirectSteps; index += 1) {
      this.emitSignDigitIndirectStep(scratch.indirect, line);
      if (index < program.indirectSteps - 1) {
        this.emitOp(0x60 + registerIndex(scratch.source), `П->X ${scratch.source}`, "display sign-digit source", line);
        this.emitOp(0x60 + registerIndex(scratch.indirect), `П->X ${scratch.indirect}`, "display sign-digit body", line);
        this.emitFirstDigitSplice(line);
      }
    }

    if (program.first === "Е") {
      this.emitOp(0x60 + registerIndex(scratch.source), `П->X ${scratch.source}`, "display sign-digit final source", line);
    } else {
      this.emitNumber(program.first);
    }
    this.emitOp(0x60 + registerIndex(scratch.indirect), `П->X ${scratch.indirect}`, "display sign-digit final body", line);
    this.emitFirstDigitSplice(line);
    this.emitOp(0x50, "С/П", `show ${display.name}`, line);
    this.optimizations.push({
      name: "screen-sign-digit-literal-lowering",
      detail: `Lowered screen ${display.name} through indirect sign-digit display construction.`,
    });
    return true;
  }

  private emitFirstDigitSplice(line: number | undefined): void {
    this.emitOp(0x14, "<->", "display sign-digit first-cell splice", line);
    this.emitOp(0x54, "К НОП", "display sign-digit first-cell splice", line, true);
    this.emitOp(0x0c, "ВП", "display sign-digit first-cell splice", line);
  }

  private emitSignDigitIndirectStep(register: RegisterName, line: number): void {
    this.emitOp(0x40 + registerIndex(register), `X->П ${register}`, "display sign-digit indirect scratch", line);
    this.emitOp(0xd0 + registerIndex(register), `К П->X ${register}`, "display sign-digit indirect normalize", line);
    this.emitOp(0x60 + registerIndex(register), `П->X ${register}`, "display sign-digit indirect body", line);
  }

  private signDigitLiteralScratch(): { indirect: RegisterName; source: RegisterName } | undefined {
    const used = this.usedAllocatedRegisters();
    const indirect = (["4", "5", "6"] as const).find((register) => !used.has(register));
    if (indirect === undefined) return undefined;
    const source = [...REGISTER_ORDER].reverse().find((register) => register !== indirect && !used.has(register));
    if (source === undefined) return undefined;
    return { indirect, source };
  }

  private scratchRegistersAvailable(registers: ReadonlySet<RegisterName>): boolean {
    const used = this.usedAllocatedRegisters();
    return [...registers].every((register) => !used.has(register));
  }

  private usedAllocatedRegisters(): Set<RegisterName> {
    const used = new Set<RegisterName>([
      ...Object.values(this.allocation.registers),
      ...Object.values(this.allocation.constants),
    ]);
    if (this.allocation.negativeZeroDegree !== undefined) used.add(this.allocation.negativeZeroDegree);
    return used;
  }

  private compileLiteralHalt(literal: string, line: number): void {
    const program = displayLiteralProgram(literal);
    if (program?.kind !== "error") {
      this.diagnostics.push(buildDiagnostic("error", `Only halt("ЕГГОГ") literal stops are supported.`, line));
      return;
    }
    this.emitErrorStopOpcode("halt literal ЕГГ0Г", line);
    this.optimizations.push({
      name: "error-stop",
      detail: `Used one-cell error opcode for literal ЕГГ0Г stop at line ${line}.`,
    });
  }

  private emitErrorStopOpcode(comment: string, line: number, raw = false): void {
    this.emitOp(0x2b, "error 2B", comment, line, raw);
  }

  private sharedLiteralDisplayHelper(
    display: ProgramAst["displays"][number],
    line: number,
  ): { display: ProgramAst["displays"][number]; label: string; line: number } | undefined {
    if (!this.shouldShareLiteralDisplay(display)) return undefined;
    const existing = this.literalDisplayHelpers.get(display.name);
    if (existing !== undefined) return existing;
    const helper = {
      display,
      label: `__display_literal_${display.name}`,
      line,
    };
    this.literalDisplayHelpers.set(display.name, helper);
    return helper;
  }

  private shouldShareLiteralDisplay(display: ProgramAst["displays"][number]): boolean {
    const literal = this.collapseLiteralOnlyDisplay(display);
    const program = literal === undefined ? undefined : displayLiteralProgram(literal);
    if (program === undefined) return false;
    const uses = this.displayUseCounts.get(display.name) ?? 0;
    if (uses < 2) return false;
    if (program.kind === "error") return false;
    const signCost = program.negative ? 1 : 0;
    const bodyCost = program.kind === "kinv" ? program.digits.length + 2 + signCost : program.left.length + program.right.length + 3 + signCost;
    const helperCost = uses * 2 + bodyCost + 1;
    const inlineTotal = uses * bodyCost;
    return inlineTotal - helperCost >= DISPLAY_HELPER_MIN_SAVINGS;
  }

  private collapseLiteralOnlyDisplay(display: ProgramAst["displays"][number]): string | undefined {
    if (display.items.length === 0) return "";
    if (display.items.some((item) => item.kind !== "literal")) return undefined;
    const text = display.items.map((item) => item.kind === "literal" ? item.text : "").join("");
    if (text.length === 0) return "";
    return text.trim().length === 0 ? undefined : text;
  }

  private collapseTextPrefixDisplay(
    display: ProgramAst["displays"][number],
  ): { text: string; source: Extract<ProgramAst["displays"][number]["items"][number], { kind: "source" }> } | undefined {
    let text = "";
    let source: Extract<ProgramAst["displays"][number]["items"][number], { kind: "source" }> | undefined;
    for (const item of display.items) {
      if (item.kind === "literal") {
        if (source !== undefined) return undefined;
        text += item.text;
        continue;
      }
      if (source !== undefined) return undefined;
      source = item;
    }
    if (source === undefined || text.length === 0) return undefined;
    return { text, source };
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
        const uses = this.procCallCounts.get(proc.name) ?? 0;
        this.optimizations.push({
          name: uses === 1 ? "single-use-rule-inline" : "size-model-rule-inline",
          detail: uses === 1
            ? `Inlined single-use rule ${proc.name} at line ${line}.`
            : `Inlined ${uses}-use rule ${proc.name} because it is smaller than a ПП/В/О subroutine.`,
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
      const returnX = this.procReturnXVariable(proc);
      if (returnX !== undefined) {
        this.currentXVariable = returnX;
        this.currentXAliases = new Set([returnX]);
        this.currentXKnownZero = false;
      }
      this.optimizations.push({
        name: "proc-call-lowering",
        detail: `Compiled call to rule ${proc.name} as ПП/В/О subroutine.`,
      });
      if (returnX !== undefined) {
        this.optimizations.push({
          name: "proc-return-x-reuse",
          detail: `Tracked ${returnX} in X after returning from rule ${proc.name}.`,
        });
      }
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

  private procReturnXVariable(proc: ProgramAst["procs"][number]): string | undefined {
    if (this.statementsTerminate(proc.body)) return undefined;
    const last = proc.body.at(-1);
    return last?.kind === "assign" ? last.target : undefined;
  }

  private compileCoordListLineCountDashedReport(
    assignment: Extract<StatementAst, { kind: "assign" }>,
    show: Extract<StatementAst, { kind: "show" }>,
  ): boolean {
    const template = this.dashedCoordReportTemplateAfterLineCount(assignment, show);
    if (template === undefined) return false;
    if (!this.compileCoordListLineCountAssignment(assignment, template)) return false;
    this.compileShow(show.display, show.line);
    this.optimizations.push({
      name: "coord-list-line-count-dashed-report-fusion",
      detail: `Packed coord_list line_count() directly for dashed report ${show.display} at line ${assignment.line}.`,
    });
    return true;
  }

  private compileCoordListLineCountAssignment(
    statement: Extract<StatementAst, { kind: "assign" }>,
    dashedReport?: DashedCoordReportTemplate,
  ): boolean {
    const call = coordListLineCountCall(statement.expr);
    if (call === undefined) return false;
    const context = this.coordListIndirectContext(call);
    if (context === undefined) return false;
    const current = this.allocation.registers[COORD_LIST_CURRENT];
    if (current === undefined) return false;
    const scaled = this.coordListUsesScaledDecimalStorage(call);
    if (scaled && !this.scaleCoordListCellInPlace(context.cell, statement.line)) return false;

    this.emitCoordListLineCountInitialTotal(statement.target, statement.line, dashedReport);
    this.emitCoordListLoopSetup(context, statement.line);

    const start = this.freshLabel("coord_list_line_loop");
    const visible = this.freshLabel("coord_list_visible");
    const countNext = this.freshLabel("coord_list_count_next");
    this.emitLabel(start);
    this.emitCoordListIndirectRecall(context.pointerRegister, statement.line, "coord_list candidate");
    this.emitStore(COORD_LIST_CURRENT, "coord_list current", statement.line);

    if (scaled) {
      this.compileScaledCoordListVisibilityTest(context.cell, visible, countNext, statement.line, "coord_list");
    } else {
      this.compileCoordOnesDigit({ kind: "identifier", name: COORD_LIST_CURRENT }, statement.line);
      this.compileCoordOnesDigit(context.cell, statement.line);
      this.emitOp(0x11, "-", "coord_list dx", statement.line);
      this.emitJump(0x57, "F x!=0", visible, "coord_list same column", statement.line);

      this.compileCoordTensDigit({ kind: "identifier", name: COORD_LIST_CURRENT }, statement.line);
      this.compileCoordTensDigit(context.cell, statement.line);
      this.emitOp(0x11, "-", "coord_list dy", statement.line);
      this.emitJump(0x57, "F x!=0", visible, "coord_list same row", statement.line);
      this.emitOp(0x31, "К |x|", "coord_list |dy|", statement.line);
      this.emitOp(0x14, "<->", "coord_list dx", statement.line);
      this.emitOp(0x31, "К |x|", "coord_list |dx|", statement.line);
      this.emitOp(0x11, "-", "coord_list diagonal compare", statement.line);
      this.emitJump(0x57, "F x!=0", visible, "coord_list same diagonal", statement.line);
      this.emitJump(0x51, "БП", countNext, "coord_list not visible", statement.line);
    }

    this.emitLabel(visible);
    if (!this.emitIndirectUnitIncrement(statement.target, "coord_list line_count total", statement.line)) {
      this.emitRecall(statement.target, "coord_list line_count total", statement.line);
      this.emitNumberOrPreload("1");
      this.emitOp(0x10, "+", "coord_list add visible", statement.line);
      this.emitStore(statement.target, "coord_list line_count total", statement.line);
    }

    this.emitLabel(countNext);
    this.emitCoordListCounterLoop(context.counterRegister, start, statement.line, "coord_list line_count loop");
    this.emitCoordListLineCountResult(statement.target, statement.line, dashedReport);
    this.optimizations.push({
      name: scaled ? "coord-list-scaled-line-count" : "coord-list-line-count-indirect-loop",
      detail: scaled
        ? `Lowered coord_list_line_count() through scaled decimal coordinates at line ${statement.line}.`
        : `Lowered coord_list_line_count() through a compact indirect register loop at line ${statement.line}.`,
    });
    if (dashedReport !== undefined) {
      this.optimizations.push({
        name: "coord-list-line-count-dashed-report-body",
        detail: `Accumulated ${statement.target} as a packed dashed report body at line ${statement.line}.`,
      });
    }
    return true;
  }

  private dashedCoordReportTemplateAfterLineCount(
    assignment: Extract<StatementAst, { kind: "assign" }>,
    statement: StatementAst | undefined,
  ): DashedCoordReportTemplate | undefined {
    if (statement?.kind !== "show") return undefined;
    const call = coordListLineCountCall(assignment.expr);
    if (call === undefined) return undefined;
    const display = this.ast.displays.find((candidate) => candidate.name === statement.display);
    if (display === undefined) return undefined;
    const template = dashedCoordReportDisplayTemplate(display);
    if (template === undefined) return undefined;
    if (assignment.target !== template.bearing.name) return undefined;
    if (!expressionEquals(call.cell, { kind: "identifier", name: template.cell.name })) return undefined;
    if (!this.displayFieldFitsUnsignedWidth(template.cell) || !this.displayFieldFitsUnsignedWidth(template.bearing)) {
      return undefined;
    }
    return template;
  }

  private emitCoordListLineCountInitialTotal(
    target: string,
    line: number,
    dashedReport?: DashedCoordReportTemplate,
    commentPrefix = "coord_list line_count",
  ): void {
    if (dashedReport === undefined) {
      this.emitZero(`${commentPrefix} total`, line);
      this.emitStore(target, `${commentPrefix} total`, line);
      return;
    }
    this.emitDashedCoordReportCellBody(dashedReport, line, `${commentPrefix} dashed report`);
    this.emitStore(target, `${commentPrefix} dashed report body`, line);
  }

  private emitCoordListLineCountResult(
    target: string,
    line: number,
    dashedReport?: DashedCoordReportTemplate,
    commentPrefix = "coord_list line_count",
  ): void {
    this.emitRecall(
      target,
      dashedReport === undefined ? `${commentPrefix} result` : `${commentPrefix} dashed report body`,
      line,
    );
    if (dashedReport !== undefined) this.currentXDashedCoordReportBody = dashedReport;
  }

  private emitDashedCoordReportCellBody(
    template: DashedCoordReportTemplate,
    line: number,
    commentPrefix: string,
  ): void {
    if (!this.xHolds(template.cell.name)) this.emitRecall(template.cell.name, `${commentPrefix} cell`, line);
    if (this.scaledCoordVariables.has(template.cell.name)) {
      this.emitNumber("5");
    } else {
      this.emitNumber("4");
    }
    this.emitOp(0x15, "F 10^x", `${commentPrefix} cell scale`, line);
    this.emitOp(0x12, "*", `${commentPrefix} cell shift`, line);
  }

  private coordListUsesScaledDecimalStorage(callOrList: CoordListCall | string): boolean {
    const listName = typeof callOrList === "string" ? callOrList : coordListNameFromItems(callOrList.items);
    return listName !== undefined && this.scaledCoordLists.has(listName);
  }

  private scaleCoordListCellInPlace(cell: ExpressionAst, line: number): boolean {
    if (cell.kind !== "identifier") return false;
    if (this.scaledCoordVariables.has(cell.name)) return true;
    if (!this.xHolds(cell.name)) this.emitRecall(cell.name, "coord_list raw cell", line);
    this.emitNumberOrPreload("10");
    this.emitOp(0x13, "/", "coord_list scaled decimal cell", line);
    this.emitStore(cell.name, "coord_list scaled decimal cell", line);
    this.scaledCoordVariables.add(cell.name);
    this.optimizations.push({
      name: "coord-list-scaled-decimal-storage",
      detail: `Stored ${cell.name} as y.x decimal coordinates for coord_list scans at line ${line}.`,
    });
    return true;
  }

  private compileScaledCoordListVisibilityTest(
    cell: ExpressionAst,
    visible: string,
    countNext: string,
    line: number,
    commentPrefix: string,
  ): void {
    this.compileScaledCoordFraction({ kind: "identifier", name: COORD_LIST_CURRENT }, line, `${commentPrefix} current x`);
    this.compileScaledCoordFraction(cell, line, `${commentPrefix} cell x`);
    this.emitOp(0x11, "-", `${commentPrefix} dx`, line);
    this.emitJump(0x57, "F x!=0", visible, `${commentPrefix} same column`, line);
    this.emitNumberOrPreload("10");
    this.emitOp(0x12, "*", `${commentPrefix} dx digit`, line);

    this.compileScaledCoordInteger({ kind: "identifier", name: COORD_LIST_CURRENT }, line, `${commentPrefix} current y`);
    this.compileScaledCoordInteger(cell, line, `${commentPrefix} cell y`);
    this.emitOp(0x11, "-", `${commentPrefix} dy`, line);
    this.emitJump(0x57, "F x!=0", visible, `${commentPrefix} same row`, line);
    this.emitOp(0x31, "К |x|", `${commentPrefix} |dy|`, line);
    this.emitOp(0x14, "<->", `${commentPrefix} dx`, line);
    this.emitOp(0x31, "К |x|", `${commentPrefix} |dx|`, line);
    this.emitOp(0x11, "-", `${commentPrefix} diagonal compare`, line);
    this.emitJump(0x57, "F x!=0", visible, `${commentPrefix} same diagonal`, line);
    this.emitJump(0x51, "БП", countNext, `${commentPrefix} not visible`, line);
  }

  private compileScaledCoordFraction(expr: ExpressionAst, line: number, comment: string): void {
    this.compileExpression(expr);
    this.emitOp(0x35, "К {x}", comment, line);
  }

  private compileScaledCoordInteger(expr: ExpressionAst, line: number, comment: string): void {
    this.compileExpression(expr);
    this.emitOp(0x34, "К [x]", comment, line);
  }

  private compileFusedCoordListScan(statements: StatementAst[], index: number): number {
    const branch = statements[index];
    const next = statements[index + 1];
    if (branch?.kind !== "if" || next === undefined || branch.elseBody !== undefined) return 0;
    if (!this.coordListFusedHitBodyAllowed(branch.thenBody)) return 0;

    const hasCall = coordListHasConditionCall(branch.condition);
    const lineCount = this.coordListLineCountAssignmentFromStatement(next);
    if (hasCall === undefined || lineCount === undefined) return 0;
    const lineCountCall = coordListLineCountCall(lineCount.expr);
    if (lineCountCall === undefined || !sameCoordListCall(hasCall, lineCountCall)) return 0;

    const context = this.coordListIndirectContext(lineCountCall);
    const current = this.allocation.registers[COORD_LIST_CURRENT];
    if (context === undefined || current === undefined) return 0;
    const scaled = this.coordListUsesScaledDecimalStorage(lineCountCall);

    const target = lineCount.target;
    const line = branch.line;
    const dashedReport = index + 3 === statements.length
      ? this.dashedCoordReportTemplateAfterLineCount(lineCount, statements[index + 2])
      : undefined;
    if (scaled && !this.scaleCoordListCellInPlace(context.cell, line)) return 0;
    this.emitCoordListLineCountInitialTotal(target, line, dashedReport, "coord_list fused");
    this.emitCoordListLoopSetup(context, line);

    const start = this.freshLabel("coord_list_fused_loop");
    const hit = this.freshLabel("coord_list_fused_hit");
    const visible = this.freshLabel("coord_list_fused_visible");
    const countNext = this.freshLabel("coord_list_fused_next");
    this.emitLabel(start);
    this.emitCoordListIndirectRecall(context.pointerRegister, line, "coord_list fused candidate");
    this.emitStore(COORD_LIST_CURRENT, "coord_list fused current", line);

    this.compileExpression(context.cell);
    this.emitRecall(COORD_LIST_CURRENT, "coord_list fused current", line);
    this.emitOp(0x11, "-", "coord_list fused hit compare", line);
    this.emitJump(0x57, "F x!=0", hit, "coord_list fused hit", line);

    if (scaled) {
      this.compileScaledCoordListVisibilityTest(context.cell, visible, countNext, line, "coord_list fused");
    } else {
      this.compileCoordOnesDigit({ kind: "identifier", name: COORD_LIST_CURRENT }, line);
      this.compileCoordOnesDigit(context.cell, line);
      this.emitOp(0x11, "-", "coord_list fused dx", line);
      this.emitJump(0x57, "F x!=0", visible, "coord_list fused same column", line);

      this.compileCoordTensDigit({ kind: "identifier", name: COORD_LIST_CURRENT }, line);
      this.compileCoordTensDigit(context.cell, line);
      this.emitOp(0x11, "-", "coord_list fused dy", line);
      this.emitJump(0x57, "F x!=0", visible, "coord_list fused same row", line);
      this.emitOp(0x31, "К |x|", "coord_list fused |dy|", line);
      this.emitOp(0x14, "<->", "coord_list fused dx", line);
      this.emitOp(0x31, "К |x|", "coord_list fused |dx|", line);
      this.emitOp(0x11, "-", "coord_list fused diagonal compare", line);
      this.emitJump(0x57, "F x!=0", visible, "coord_list fused same diagonal", line);
      this.emitJump(0x51, "БП", countNext, "coord_list fused not visible", line);
    }

    this.emitLabel(hit);
    this.compileStatements(branch.thenBody);
    this.emitLabel(visible);
    if (!this.emitIndirectUnitIncrement(target, "coord_list fused total", line)) {
      this.emitRecall(target, "coord_list fused total", line);
      this.emitNumberOrPreload("1");
      this.emitOp(0x10, "+", "coord_list fused add visible", line);
      this.emitStore(target, "coord_list fused total", line);
    }

    this.emitLabel(countNext);
    this.emitCoordListCounterLoop(context.counterRegister, start, line, "coord_list fused loop");
    this.emitCoordListLineCountResult(target, line, dashedReport, "coord_list fused");
    this.optimizations.push({
      name: scaled ? "coord-list-scaled-fused-hit-line-count" : "coord-list-fused-hit-line-count",
      detail: scaled
        ? `Fused coord_list membership and line_count through scaled decimal coordinates at line ${branch.line}.`
        : `Fused coord_list membership and line_count into one indirect scan at line ${branch.line}.`,
    });
    if (dashedReport !== undefined) {
      this.optimizations.push({
        name: "coord-list-fused-dashed-report-body",
        detail: `Accumulated ${target} as a packed dashed report body during the fused scan at line ${branch.line}.`,
      });
    }
    return 2;
  }

  private coordListLineCountAssignmentFromStatement(
    statement: StatementAst,
  ): Extract<StatementAst, { kind: "assign" }> | undefined {
    if (statement.kind === "assign" && coordListLineCountCall(statement.expr) !== undefined) return statement;
    if (statement.kind !== "call") return undefined;
    const proc = this.ast.procs.find((candidate) => candidate.name === statement.block);
    if (proc?.body.length !== 1) return undefined;
    const [only] = proc.body;
    return only?.kind === "assign" && coordListLineCountCall(only.expr) !== undefined ? only : undefined;
  }

  private coordListFusedHitBodyAllowed(statements: StatementAst[], seen = new Set<string>()): boolean {
    if (statements.length === 0) return true;
    for (const statement of statements) {
      if (statement.kind === "show" || statement.kind === "pause") continue;
      if (statement.kind !== "call") return false;
      if (seen.has(statement.block)) return false;
      const proc = this.ast.procs.find((candidate) => candidate.name === statement.block);
      if (proc === undefined) return false;
      seen.add(statement.block);
      const allowed = this.coordListFusedHitBodyAllowed(proc.body, seen);
      seen.delete(statement.block);
      if (!allowed) return false;
    }
    return true;
  }

  private emitIndirectUnitIncrement(target: string, comment: string, line: number): boolean {
    const register = this.allocation.registers[target];
    if (register === undefined || !isPreincrementIndirectRegister(register)) return false;
    this.emitOp(0xd0 + registerIndex(register), `К П->X ${register}`, comment, line);
    this.optimizations.push({
      name: "indirect-incdec-counter",
      detail: `Incremented ${target} by using ${getOpcode(0xd0 + registerIndex(register)).name}'s pre-increment side effect at line ${line}.`,
    });
    return true;
  }

  private emitKnownOneIndirectLoopBack(target: string, line: number): boolean {
    if (!this.coordListCounterKnownOne || !this.zeroAddressLabels.has(target)) return false;
    const register = this.allocation.registers[COORD_LIST_COUNTER];
    if (register === undefined || flOpcode(register) === undefined) return false;
    this.emitOp(0x80 + registerIndex(register), `К БП ${register}`, "loop back via known-one counter", line);
    this.optimizations.push({
      name: "indirect-incdec-counter",
      detail: `Reused ${COORD_LIST_COUNTER} = 1 as a one-cell indirect loop-back to 00 at line ${line}.`,
    });
    return true;
  }

  private compileUnitDecrement(statement: Extract<StatementAst, { kind: "assign" }>): boolean {
    if (!isUnitDecrementExpression(statement.target, statement.expr)) return false;
    const register = this.allocation.registers[statement.target];
    if (register === undefined) return false;
    const opcode = flOpcode(register);
    if (opcode === undefined) return false;
    const after = this.freshLabel("fl_decrement_done");
    const preservedXVariable = this.currentXVariable === statement.target ? undefined : this.currentXVariable;
    const preservedXKnownZero = this.currentXKnownZero;
    this.emitJump(opcode, getOpcode(opcode).name, after, `decrement ${statement.target}`, statement.line);
    this.currentXVariable = preservedXVariable;
    this.currentXAliases = preservedXVariable !== undefined ? new Set([preservedXVariable]) : new Set();
    this.currentXKnownZero = preservedXKnownZero;
    this.machineEntryOpen = false;
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
    if (this.compileCoordListHasCondition(condition, falseLabel, line)) return;
    if (this.compileNegativeZeroThresholdFlow(condition, falseLabel, line)) return;

    const preloadedConstants = new Set(Object.keys(this.allocation.constants));
    const selected = selectCheaperEquivalentCondition(
      condition,
      this.ast,
      preloadedConstants,
    );
    if (selected.changed) {
      this.optimizations.push({
        name: "comparison-boundary-normalization",
        detail: `Normalized ${conditionToText(condition)} to ${conditionToText(selected.condition)} at line ${line}.`,
      });
    }
    const compiledCondition = selected.condition;
    if (this.compileNearAnyHelperCondition(compiledCondition, falseLabel, line, preloadedConstants)) return;
    if (this.compileSmallSetCondition(compiledCondition, falseLabel, line, preloadedConstants)) return;
    if (isZeroExpression(compiledCondition.right) && canTestAgainstZeroDirectly(compiledCondition.op)) {
      const bitHasLowering = this.compileBitHasConditionWithBitMaskHelper(compiledCondition.left, line)
        ?? this.compileBitHasConditionWithSpatialHelper(compiledCondition.left, line);
      if (bitHasLowering === undefined) {
        if (!(compiledCondition.left.kind === "identifier" && this.xHolds(compiledCondition.left.name))) {
          this.compileExpression(compiledCondition.left);
        }
      }
      const opcode = directTestOpcode(compiledCondition.op);
      this.emitJump(opcode, getOpcode(opcode).name, falseLabel, `false branch for ${condition.op}`, line);
      this.optimizations.push({
        name: bitHasLowering?.name ?? "zero-condition-test",
        detail: bitHasLowering?.detail
          ?? `Tested ${compiledCondition.op} 0 without materializing a zero literal at line ${line}.`,
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

  private compileCoordListHasCondition(
    condition: ConditionAst,
    falseLabel: string,
    line: number,
  ): boolean {
    const call = coordListHasConditionCall(condition);
    if (call === undefined) return false;
    const context = this.coordListIndirectContext(call);
    if (context === undefined) return false;
    const scaled = this.coordListUsesScaledDecimalStorage(call);
    if (scaled && !this.scaleCoordListCellInPlace(context.cell, line)) return false;

    const trueLabel = this.freshLabel("coord_list_hit");
    this.emitCoordListLoopSetup(context, line);
    const start = this.freshLabel("coord_list_has_loop");
    this.emitLabel(start);
    this.compileExpression(context.cell);
    this.emitCoordListIndirectRecall(context.pointerRegister, line, "coord_list candidate");
    this.emitOp(0x11, "-", "coord_list hit compare", line);
    this.emitJump(0x57, "F x!=0", trueLabel, "coord_list hit", line);
    this.emitCoordListCounterLoop(context.counterRegister, start, line, "coord_list has loop");
    this.emitJump(0x51, "БП", falseLabel, "coord_list miss", line);
    this.emitLabel(trueLabel);
    this.optimizations.push({
      name: scaled ? "coord-list-scaled-membership" : "coord-list-indirect-membership",
      detail: scaled
        ? `Lowered coord_list membership through scaled decimal coordinates at line ${line}.`
        : `Lowered coord_list membership through an indirect register walk at line ${line}.`,
    });
    return true;
  }

  private coordListIndirectContext(call: CoordListCall): CoordListIndirectContext | undefined {
    const pointerRegister = this.allocation.registers[COORD_LIST_POINTER];
    const counterRegister = this.allocation.registers[COORD_LIST_COUNTER];
    if (pointerRegister === undefined || counterRegister === undefined) return undefined;
    if (!isPreincrementIndirectRegister(pointerRegister)) return undefined;
    const itemRegisters = call.items.map((item) => this.allocation.registers[item.name]);
    if (itemRegisters.some((register) => register === undefined)) return undefined;
    const indexes = itemRegisters.map((register) => registerIndex(register!));
    for (let index = 1; index < indexes.length; index += 1) {
      if (indexes[index] !== indexes[0]! + index) return undefined;
    }
    if (indexes.length === 0 || indexes[0]! <= 0) return undefined;
    if (itemRegisters.includes(pointerRegister)) return undefined;
    return {
      cell: call.cell,
      count: call.items.length,
      pointerStart: indexes[0]! - 1,
      pointerRegister,
      counterRegister,
    };
  }

  private emitCoordListLoopSetup(context: CoordListIndirectContext, line: number): void {
    this.emitNumberOrPreload(String(context.pointerStart));
    this.emitStore(COORD_LIST_POINTER, "coord_list pointer", line);
    this.emitNumberOrPreload(String(context.count));
    this.emitStore(COORD_LIST_COUNTER, "coord_list counter", line);
  }

  private emitCoordListIndirectRecall(
    pointerRegister: RegisterName,
    line: number | undefined,
    comment: string,
  ): void {
    this.emitOp(0xd0 + registerIndex(pointerRegister), `К П->X ${pointerRegister}`, comment, line);
  }

  private emitCoordListCounterLoop(
    counterRegister: RegisterName,
    target: string,
    line: number,
    comment: string,
  ): void {
    const opcode = flOpcode(counterRegister);
    if (opcode !== undefined) {
      this.emitJump(opcode, getOpcode(opcode).name, target, comment, line);
      this.coordListCounterKnownOne = true;
      return;
    }
    this.emitRecall(COORD_LIST_COUNTER, "coord_list counter", line);
    this.emitNumberOrPreload("1");
    this.emitOp(0x11, "-", "coord_list decrement", line);
    this.emitStore(COORD_LIST_COUNTER, "coord_list counter", line);
    this.emitRecall(COORD_LIST_COUNTER, "coord_list counter", line);
    this.emitJump(0x5e, "F x=0", target, comment, line);
    this.coordListCounterKnownOne = false;
  }

  private compileCoordOnesDigit(expr: ExpressionAst, line: number): void {
    this.compileExpression(expr);
    this.emitNumberOrPreload("10");
    this.emitOp(0x13, "/", "coord quotient", line);
    this.emitOp(0x35, "К {x}", "coord fractional part", line);
    this.emitNumberOrPreload("10");
    this.emitOp(0x12, "*", "coord ones digit", line);
  }

  private compileCoordTensDigit(expr: ExpressionAst, line: number): void {
    this.compileExpression(expr);
    this.emitNumberOrPreload("10");
    this.emitOp(0x13, "/", "coord quotient", line);
    this.emitOp(0x34, "К [x]", "coord tens digit", line);
  }

  private compileBitHasConditionWithBitMaskHelper(
    expr: ExpressionAst,
    line: number,
  ): { name: string; detail: string } | undefined {
    if (expr.kind !== "call" || expr.callee.toLowerCase() !== "bit_has" || expr.args.length !== 2) return undefined;
    const [mask, index] = expr.args;
    if (mask?.kind !== "identifier" || index === undefined) return undefined;
    if (
      !programHasLineCountForMask(this.ast, mask.name) &&
      this.loweringOptions.sharedBitMaskHelperCalls !== true
    ) return undefined;
    const scratch = this.sharedBitMaskHelperScratch() ?? spatialHitScratchName(mask.name);
    if (!this.allocation.registers[scratch]) return undefined;
    const helper = this.ensureSpatialBitMaskHelper(scratch, line);
    this.compileExpression(index);
    this.emitJump(0x53, "ПП", helper.label, "bit_mask helper", line);
    this.compileExpression(mask);
    this.emitOp(0x37, "К ∧", "bit membership test", line);
    this.emitOp(0x35, "К {x}", "bit membership fraction", line);
    return {
      name: "bit-mask-condition-helper",
      detail: `Tested bit_has() through the shared bit_mask helper at line ${line}.`,
    };
  }

  private compileBitHasConditionWithSpatialHelper(
    expr: ExpressionAst,
    line: number,
  ): { name: string; detail: string } | undefined {
    if (expr.kind !== "call" || expr.callee.toLowerCase() !== "bit_has" || expr.args.length !== 2) return undefined;
    const [mask, index] = expr.args;
    if (mask?.kind !== "identifier" || index === undefined) return undefined;
    const scratch = spatialHitScratchName(mask.name);
    if (!this.allocation.registers[scratch]) return undefined;
    const helper = this.ensureSpatialHitHelper(mask.name, scratch);
    this.compileExpression(index);
    this.emitJump(0x53, "ПП", helper.label, `spatial hit ${mask.name}`, line);
    return {
      name: "spatial-hit-condition-helper",
      detail: `Tested bit_has() through the shared spatial hit helper at line ${line}.`,
    };
  }

  private compileEqualityWithCurrentX(
    condition: ConditionAst,
    falseLabel: string,
    line: number,
  ): boolean {
    if (condition.op !== "==" && condition.op !== "!=") return false;
    if (
      condition.right.kind === "identifier" &&
      this.xHolds(condition.right.name) &&
      isSimpleStackLoad(condition.left)
    ) {
      this.compileExpression(condition.left);
    } else if (
      condition.left.kind === "identifier" &&
      this.xHolds(condition.left.name) &&
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
      detail: `Reused the value already in X for equality comparison at line ${line}.`,
    });
    return true;
  }

  private compileNearAnyHelperCondition(
    condition: ConditionAst,
    falseLabel: string,
    line: number,
    preloadedConstants: ReadonlySet<string>,
  ): boolean {
    const match = matchNearAnyHelperCondition(condition);
    if (match === undefined) return false;
    const key = nearAnyHelperKey(match.value, match.radius);
    const stats = this.nearAnyHelperStats.get(key);
    if (stats === undefined || stats.helperCost >= stats.ordinaryCost) return false;

    const helper = this.nearAnyHelper(match.value, match.radius, line);
    this.compileNearAnyMarginWithHelper(match, helper.label, line);
    const opcode = directTestOpcode(match.op);
    this.emitJump(opcode, getOpcode(opcode).name, falseLabel, `false branch for ${match.op}`, line);
    this.optimizations.push({
      name: "near-any-helper-lowering",
      detail: `Lowered ${conditionToText(condition)} through shared near_any helper at line ${line} (${stats.helperCost} vs ${stats.ordinaryCost} estimated group steps).`,
    });
    return true;
  }

  private compileNearAnyMarginWithHelper(
    match: NearAnyHelperConditionMatch,
    label: string,
    line: number,
  ): void {
    for (let index = 0; index < match.candidates.length; index += 1) {
      const candidate = match.candidates[index]!;
      this.compileNearAnyCandidate(candidate, line);
      this.emitJump(0x53, "ПП", label, "near_any candidate", line);
      if (index > 0) this.emitOp(0x36, "К max", "near_any max margin", line);
    }
  }

  private compileNearAnyCandidate(candidate: ExpressionAst, line: number): void {
    if (candidate.kind === "identifier" && this.xHolds(candidate.name)) {
      this.optimizations.push({
        name: "stack-current-x-scheduling",
        detail: `Reused ${candidate.name} already in X for near_any candidate at line ${line}.`,
      });
      return;
    }
    this.compileExpression(candidate);
  }

  private nearAnyHelper(
    value: ExpressionAst,
    radius: ExpressionAst,
    line?: number,
  ): { value: ExpressionAst; radius: ExpressionAst; label: string; line?: number } {
    const key = nearAnyHelperKey(value, radius);
    const existing = this.nearAnyHelpers.get(key);
    if (existing !== undefined) return existing;
    const helper = {
      value,
      radius,
      label: `__near_any_${this.nearAnyHelpers.size}`,
      ...(line === undefined ? {} : { line }),
    };
    this.nearAnyHelpers.set(key, helper);
    return helper;
  }

  private compileSmallSetCondition(
    condition: ConditionAst,
    falseLabel: string,
    line: number,
    preloadedConstants: ReadonlySet<string>,
  ): boolean {
    const match = matchSmallSetCondition(condition);
    if (match === undefined) return false;
    const selectedCost = estimateSmallSetConditionCost(match, preloadedConstants);
    const ordinaryCost = conditionCompileCost(condition, preloadedConstants);
    if (selectedCost >= ordinaryCost) return false;
    if (match.mode === "any") {
      const trueLabel = this.freshLabel(`${match.kind}_any_true`);
      for (const item of match.tests.slice(0, -1)) {
        this.compileExpression(item.expr);
        this.emitJump(item.trueOpcode, getOpcode(item.trueOpcode).name, trueLabel, `${match.kind} any hit`, line);
      }
      const last = match.tests.at(-1)!;
      this.compileExpression(last.expr);
      this.emitJump(last.falseOpcode, getOpcode(last.falseOpcode).name, falseLabel, `${match.kind} any miss`, line);
      this.emitLabel(trueLabel);
    } else {
      for (const item of match.tests) {
        this.compileExpression(item.expr);
        this.emitJump(item.falseOpcode, getOpcode(item.falseOpcode).name, falseLabel, `${match.kind} all miss`, line);
      }
    }
    this.optimizations.push({
      name: "small-set-condition-lowering",
      detail: `Lowered ${conditionToText(condition)} as ${match.tests.length} direct small-set test${match.tests.length === 1 ? "" : "s"} at line ${line} (${selectedCost} vs ${ordinaryCost} estimated steps).`,
    });
    return true;
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

  private compileExpression(expr: ExpressionAst): void {
    const randomCellHelper = this.sharedRandomCellHelper(expr);
    if (randomCellHelper !== undefined) {
      this.emitJump(0x53, "ПП", randomCellHelper.label, `random cell ${expressionToIntentText(expr)}`);
      this.optimizations.push({
        name: "random-cell-helper-call",
        detail: `Reused shared random cell helper for ${expressionToIntentText(expr)}.`,
      });
      return;
    }

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
    if (expr.left.kind === "identifier" && this.xHolds(expr.left.name) && isSimpleStackLoad(expr.right)) {
      this.compileExpression(expr.right);
      this.emitOp(binaryOpcode(expr.op), expr.op, `expr ${expr.op}`);
      this.optimizations.push({
        name: "stack-current-x-scheduling",
        detail: `Reused ${expr.left.name} already in X for commutative ${expr.op}.`,
      });
      return true;
    }
    if (expr.right.kind === "identifier" && this.xHolds(expr.right.name) && isSimpleStackLoad(expr.left)) {
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

  private sharedRandomCellHelper(expr: ExpressionAst): { expr: ExpressionAst; label: string; line?: number } | undefined {
    if (this.emittingRandomCellHelper) return undefined;
    if (!this.shouldShareRandomCellExpression(expr)) return undefined;
    const key = expressionToIntentText(expr);
    const existing = this.randomCellHelpers.get(key);
    if (existing !== undefined) return existing;
    const helper = {
      expr,
      label: `__random_cell_${this.randomCellHelpers.size}`,
    };
    this.randomCellHelpers.set(key, helper);
    return helper;
  }

  private shouldShareRandomCellExpression(expr: ExpressionAst): boolean {
    if (!isRandomCellExpressionShape(expr)) return false;
    const key = expressionToIntentText(expr);
    const uses = this.expressionUseCounts.get(key)?.count ?? 0;
    if (uses < 2) return false;
    const preloadedConstants = new Set(Object.keys(this.allocation.constants));
    const cost = estimateExpressionCostForCondition(expr, preloadedConstants);
    const inlineTotal = uses * cost;
    const helperTotal = uses * 2 + cost + 1;
    const threshold = this.loweringOptions.shareRandomCell === true ? 1 : EXPRESSION_HELPER_MIN_SAVINGS;
    return inlineTotal - helperTotal >= threshold;
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
    if (name === "__direction_cardinal") {
      this.compileCardinalDirectionCall(expr);
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
    if (isSmallSetMacroName(name) && !smallSetMacroArityOk(name, expr.args.length)) {
      this.diagnostics.push({
        level: "error",
        message: `${expr.callee}() expects ${smallSetMacroArityText(name)}, got ${expr.args.length}.`,
      });
      return;
    }
    const smallSetMacro = smallSetExpressionMacro(name, expr.args);
    if (smallSetMacro !== undefined) {
      this.compileExpression(smallSetMacro);
      this.optimizations.push({
        name: "small-set-primitive-lowering",
        detail: `Lowered ${expr.callee}() to coordinate-set arithmetic.`,
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

  private compileCardinalDirectionCall(expr: Extract<ExpressionAst, { kind: "call" }>): void {
    const keyRegister = this.directionKeyRegister(expr);
    if (keyRegister === undefined) return;

    const yAxis = this.freshLabel("direction_cardinal_y_axis");
    const done = this.freshLabel("direction_cardinal_done");

    this.emitOp(0x60 + registerIndex(keyRegister), `П->X ${keyRegister}`, "cardinal direction key");
    this.emitNumberOrPreload("5");
    this.emitOp(0x11, "-", "cardinal direction key-5");
    this.emitOp(0x31, "К |x|", "cardinal direction axis test");
    this.emitNumberOrPreload("1");
    this.emitOp(0x11, "-", "cardinal direction axis discriminator");
    this.emitJump(0x5e, "F x=0", yAxis, "cardinal direction y-axis");

    this.emitOp(0x60 + registerIndex(keyRegister), `П->X ${keyRegister}`, "cardinal direction x key");
    this.emitNumberOrPreload("5");
    this.emitOp(0x11, "-", "cardinal direction key-5");
    this.emitNumberOrPreload("10");
    this.emitOp(0x12, "*", "cardinal direction x delta");
    this.emitJump(0x51, "БП", done, "cardinal direction done");

    this.emitLabel(yAxis);
    this.emitNumberOrPreload("5");
    this.emitOp(0x60 + registerIndex(keyRegister), `П->X ${keyRegister}`, "cardinal direction y key");
    this.emitOp(0x11, "-", "cardinal direction 5-key");
    this.emitNumberOrPreload("3");
    this.emitOp(0x13, "/", "cardinal direction y delta");

    this.emitLabel(done);
    this.optimizations.push({
      name: "direction-cardinal-lowering",
      detail: `Lowered guarded cardinal direction(${this.directionKeyName(expr)}) without floor-key cases.`,
    });
  }

  private directionKeyRegister(expr: Extract<ExpressionAst, { kind: "call" }>): RegisterName | undefined {
    if (expr.args.length !== 1) {
      this.diagnostics.push({
        level: "error",
        message: `direction() expects one keypad argument, got ${expr.args.length}.`,
      });
      return undefined;
    }
    const arg = expr.args[0]!;
    if (arg.kind !== "identifier") {
      this.diagnostics.push({
        level: "error",
        message: "direction() currently requires an identifier argument so the optimizer can reuse its register.",
      });
      return undefined;
    }
    const keyRegister = this.allocation.registers[arg.name];
    if (!keyRegister) {
      this.diagnostics.push({
        level: "error",
        message: `Unknown direction key '${arg.name}'.`,
      });
      return undefined;
    }
    return keyRegister;
  }

  private directionKeyName(expr: Extract<ExpressionAst, { kind: "call" }>): string {
    const arg = expr.args[0];
    return arg?.kind === "identifier" ? arg.name : "?";
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
    // neighbor_count sums several spatial-hit probes. Each hit-helper call churns
    // the four-deep MK-61 stack, so a stack-held running sum is corrupted; the
    // loop body keeps the partial total in a register instead.
    if (name === "neighbor_count" && this.compileSpatialNeighborCountLoop(expr)) return true;
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
    const total = spatialCountScratchNames()[0]!;
    if (this.allocation.registers[total] === undefined) return false;

    // Enumerate the concrete neighbour offsets and accumulate each spatial hit
    // in a register. An FL-counter loop is avoided on purpose: the spatial-hit
    // helper churns the four-deep stack, and the post-layout indirect-memory
    // pass relocates the loop counter, so a register-accumulated unroll is the
    // only shape that survives both. The neighbour set is tiny (2 on a line,
    // 8 on a grid), so the unroll stays cheap.
    const offsets: number[] = [];
    for (const progression of spatialNeighborProgressions(board)) {
      const start = numericLiteralValue(progression.startOffset);
      const step = numericLiteralValue(progression.step);
      if (start === undefined || step === undefined) return false;
      for (let i = 0; i < progression.count; i += 1) offsets.push(start + i * step);
    }
    if (offsets.length === 0) return false;

    const helper = this.ensureSpatialHitHelper(mask.name, spatialHitScratchName(mask.name));
    offsets.forEach((offset, position) => {
      this.compileExpression(offsetExpressionAst(cell, offset));
      this.emitJump(0x53, "ПП", helper.label, `spatial hit ${mask.name}`, undefined);
      if (position === 0) {
        this.emitStore(total, "neighbor_count total", undefined);
      } else {
        this.emitRecall(total, "neighbor_count total", undefined);
        this.emitOp(0x10, "+", "neighbor_count add hit", undefined);
        this.emitStore(total, "neighbor_count total", undefined);
      }
    });
    this.emitRecall(total, "neighbor_count result", undefined);
    this.optimizations.push({
      name: "spatial-neighbor-count-unroll",
      detail: `Lowered neighbor_count(${mask.name}, ...) as ${offsets.length} register-accumulated spatial hits.`,
    });
    return true;
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
    if (useMax && progressions.length >= 3 && this.allocation.registers[spatialCountStepScratchName()] !== undefined) {
      const helper = this.ensureSpatialLineProgressionHelper(hitMask, cell, operation, sourceLine);
      for (const progression of progressions) {
        this.emitZero(`${operation} current line`, sourceLine);
        this.emitStore(line, `${operation} current line`, sourceLine);
        this.compileExpression(progression.startOffset);
        this.emitStore(offset, `${operation} offset`, sourceLine);
        this.compileExpression(progression.step);
        this.emitStore(spatialCountStepScratchName(), `${operation} step`, sourceLine);
        this.emitNumberOrPreload(String(progression.count));
        this.emitStore(counter, `${operation} counter`, sourceLine);
        this.emitJump(0x53, "ПП", helper.label, `${operation} line progression`, sourceLine);

        this.emitRecall(total, `${operation} total`);
        this.emitRecall(line, `${operation} current line`);
        this.emitOp(0x36, "К max", `${operation} best line`);
        this.emitStore(total, `${operation} total`);
      }
      this.emitRecall(total, `${operation} result`);
      this.optimizations.push({
        name: "spatial-line-progression-helper-call",
        detail: `Reused shared ${operation} line progression helper for ${hitMask}.`,
      });
      return;
    }
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

  private emitSpatialLineProgressionHelperBody(
    hitMask: string,
    cell: ExpressionAst,
    operation: "line_count" | "neighbor_count",
    sourceLine: number | undefined,
  ): void {
    const scratch = spatialCountScratchNames();
    const line = scratch[1]!;
    const offset = scratch[2]!;
    const counter = scratch[3]!;
    const counterRegister = this.allocation.registers[counter];
    const flCounterOpcode = counterRegister === undefined ? undefined : flOpcode(counterRegister);

    const start = this.freshLabel(`${operation}_line_loop`);
    this.emitLabel(start);
    this.compileExpression(addExpressions(cell, { kind: "identifier", name: offset }));
    const helper = this.ensureSpatialHitHelper(hitMask, spatialHitScratchName(hitMask));
    this.emitJump(0x53, "ПП", helper.label, `spatial hit ${hitMask}`, sourceLine);
    this.emitRecall(line, `${operation} line accumulator`);
    this.emitOp(0x10, "+", `${operation} add hit`);
    this.emitStore(line, `${operation} line accumulator`);

    this.emitRecall(offset, `${operation} offset`);
    this.emitRecall(spatialCountStepScratchName(), `${operation} step`);
    this.emitOp(0x10, "+", `${operation} next offset`);
    this.emitStore(offset, `${operation} offset`);

    if (flCounterOpcode !== undefined) {
      this.emitJump(flCounterOpcode, getOpcode(flCounterOpcode).name, start, `${operation} line loop`, sourceLine);
      this.optimizations.push({
        name: "spatial-count-fl-loop",
        detail: `Used ${getOpcode(flCounterOpcode).name} for ${operation} line progression loop counter.`,
      });
    } else {
      this.emitRecall(counter, `${operation} counter`);
      this.emitNumber("1");
      this.emitOp(0x11, "-", `${operation} decrement`);
      this.emitStore(counter, `${operation} counter`);
      this.emitRecall(counter, `${operation} counter`);
      this.emitJump(0x5e, "F x=0", start, `${operation} line loop`, sourceLine);
    }
    this.emitRecall(line, `${operation} current line`);
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
    this.emitOp(0x35, "К {x}", "spatial hit membership fraction", sourceLine);
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

  private ensureSpatialLineProgressionHelper(
    hitMask: string,
    cell: ExpressionAst,
    operation: "line_count" | "neighbor_count",
    line: number | undefined,
  ): { hitMask: string; cell: ExpressionAst; label: string; operation: "line_count" | "neighbor_count"; line?: number } {
    const key = `${operation}:${hitMask}:${expressionToIntentText(cell)}`;
    const existing = this.spatialLineProgressionHelpers.get(key);
    if (existing !== undefined) return existing;
    const helper = {
      hitMask,
      cell,
      label: `__${operation}_line_progression_${this.spatialLineProgressionHelpers.size}`,
      operation,
      ...(line === undefined ? {} : { line }),
    };
    this.spatialLineProgressionHelpers.set(key, helper);
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
    this.currentXAliases.clear();
    this.currentXKnownZero = false;
    this.currentXDashedCoordReportBody = undefined;
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

  private emitStore(name: string, comment?: string, sourceLine?: number, raw = false): void {
    const register = this.allocation.registers[name];
    if (!register) {
      this.diagnostics.push(buildDiagnostic("error", `No register allocated for ${name}`, sourceLine));
      return;
    }
    const knownZero = this.currentXKnownZero;
    // A store does not change X: every variable already equal to X stays equal,
    // and `name` now joins them (X was just copied into it).
    const aliases = new Set(this.currentXAliases);
    this.emitOp(0x40 + registerIndex(register), `X->П ${register}`, comment, sourceLine, raw);
    this.currentXVariable = name;
    aliases.add(name);
    this.currentXAliases = aliases;
    this.currentXKnownZero = knownZero;
    this.scaledCoordVariables.delete(name);
    if (name === COORD_LIST_COUNTER) this.coordListCounterKnownOne = false;
  }

  private emitRecall(name: string, comment?: string, sourceLine?: number): void {
    const register = this.allocation.registers[name];
    if (!register) {
      this.diagnostics.push(buildDiagnostic("error", `Unknown variable '${name}'`, sourceLine));
      return;
    }
    this.emitOp(0x60 + registerIndex(register), `П->X ${register}`, comment, sourceLine);
    this.currentXVariable = name;
    this.currentXAliases = new Set([name]);
  }

  // True when the value in X is known to equal `name` (directly, or via a
  // copy-equivalence alias when alias reuse is enabled), so a recall can be
  // elided. Restricted to scalar reuse sites by callers.
  private xHolds(name: string): boolean {
    if (name === this.currentXVariable) return true;
    return this.loweringOptions.aliasXReuse === true && this.currentXAliases.has(name);
  }

  private emitJump(
    opcode: number,
    mnemonic: string,
    target: string | number,
    comment?: string,
    sourceLine?: number,
  ): void {
    // Capture the X-fact on this edge *before* the branch/jump opcode clears
    // it, so merge points can verify all predecessors agree (see emitLabel).
    if (typeof target === "string") this.recordLabelEdge(target, this.currentXVariable);
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
    if (opcode >= 0x80 && opcode <= 0xfe) this.coordListCounterKnownOne = false;
    this.currentXVariable = undefined;
    this.currentXAliases.clear();
    this.currentXKnownZero = false;
    this.currentXDashedCoordReportBody = undefined;
    // Digit / '.' / sign / ВП opcodes (0x00..0x0c) keep the machine in
    // number-entry mode; every other op finalizes it.
    this.machineEntryOpen = opcode <= 0x0c;
  }

  private recordLabelEdge(label: string, fact: string | undefined): void {
    if (this.labelEdgeX.has(label)) {
      if (this.labelEdgeX.get(label) !== fact) this.labelEdgeX.set(label, undefined);
    } else {
      this.labelEdgeX.set(label, fact);
    }
  }

  private emitLabel(name: string): void {
    if (this.items.every((item) => item.kind === "label")) this.zeroAddressLabels.add(name);
    this.coordListCounterKnownOne = false;
    this.items.push({ kind: "label", name });
    // A label is a control-flow merge point. The "current X" fact tracked
    // textually only reflects the fall-through edge; reusing it across a join
    // is sound only if every incoming branch/jump edge agrees on the same
    // variable. Otherwise the value in X is path-dependent and must not be
    // reused (this is what made display-stack-reuse pick up garbage left by a
    // sibling branch). Labels with no recorded edge keep the fall-through fact.
    if (this.labelEdgeX.has(name)) {
      const edgeFact = this.labelEdgeX.get(name);
      this.currentXVariable = this.currentXVariable === edgeFact ? this.currentXVariable : undefined;
      this.currentXKnownZero = false;
    }
    // Copy-equivalence aliases never survive a merge: other predecessors reach
    // this label with X clobbered, so only the proven single fact may remain.
    this.currentXAliases = this.currentXVariable !== undefined ? new Set([this.currentXVariable]) : new Set();
    this.currentXDashedCoordReportBody = undefined;
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

interface CoordListCall {
  cell: ExpressionAst;
  items: Array<{ name: string }>;
}

interface CoordListIndirectContext {
  cell: ExpressionAst;
  count: number;
  pointerStart: number;
  pointerRegister: RegisterName;
  counterRegister: RegisterName;
}

function coordListHasConditionCall(condition: ConditionAst): CoordListCall | undefined {
  if (!isZeroExpression(condition.right) || condition.op !== "!=") return undefined;
  return coordListHasCall(condition.left);
}

function coordListHasCall(expr: ExpressionAst): CoordListCall | undefined {
  if (expr.kind !== "call" || expr.callee.toLowerCase() !== "coord_list_has") return undefined;
  if (expr.args.length < 2) return undefined;
  const [cell, ...items] = expr.args;
  if (cell === undefined) return undefined;
  const identifiers = items.every((item): item is Extract<ExpressionAst, { kind: "identifier" }> => item.kind === "identifier");
  if (!identifiers) return undefined;
  return { cell, items: items.map((item) => ({ name: item.name })) };
}

function coordListLineCountCall(expr: ExpressionAst): CoordListCall | undefined {
  if (expr.kind !== "call" || expr.callee.toLowerCase() !== "coord_list_line_count") return undefined;
  if (expr.args.length < 2) return undefined;
  const [cell, ...items] = expr.args;
  if (cell === undefined) return undefined;
  const identifiers = items.every((item): item is Extract<ExpressionAst, { kind: "identifier" }> => item.kind === "identifier");
  if (!identifiers) return undefined;
  return { cell, items: items.map((item) => ({ name: item.name })) };
}

function sameCoordListCall(left: CoordListCall, right: CoordListCall): boolean {
  return expressionEquals(left.cell, right.cell) &&
    left.items.length === right.items.length &&
    left.items.every((item, index) => item.name === right.items[index]?.name);
}

function coordListNameFromItems(items: readonly { name: string }[]): string | undefined {
  let listName: string | undefined;
  for (const item of items) {
    const info = coordListItemInfo(item.name);
    if (info === undefined) return undefined;
    if (listName === undefined) {
      listName = info.listName;
    } else if (listName !== info.listName) {
      return undefined;
    }
  }
  return listName;
}

function collectScaledCoordListNames(ast: ProgramAst): Set<string> {
  const candidates = new Map<string, Set<string>>();
  const procMap = new Map(ast.procs.map((proc) => [proc.name, proc]));

  const addCandidate = (call: CoordListCall): void => {
    if (call.cell.kind !== "identifier") return;
    const listName = coordListNameFromItems(call.items);
    if (listName === undefined || !coordListEligibleForScaledDecimalStorage(ast, listName)) return;
    const cells = candidates.get(listName) ?? new Set<string>();
    cells.add(call.cell.name);
    candidates.set(listName, cells);
  };

  const visit = (statements: StatementAst[]): void => {
    for (let index = 0; index < statements.length; index += 1) {
      const statement = statements[index]!;
      const next = statements[index + 1];
      if (statement.kind === "if" && next !== undefined && statement.elseBody === undefined) {
        const hasCall = coordListHasConditionCall(statement.condition);
        const lineCount = coordListLineCountAssignmentFromStatement(next, procMap);
        const lineCountCall = lineCount === undefined ? undefined : coordListLineCountCall(lineCount.expr);
        if (hasCall !== undefined && lineCountCall !== undefined && sameCoordListCall(hasCall, lineCountCall)) {
          addCandidate(lineCountCall);
        }
      }
      if (statement.kind === "if") {
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

  const selected = new Set<string>();
  for (const [listName, cells] of candidates) {
    if (cells.size !== 1) continue;
    const [cell] = cells;
    if (cell !== undefined && coordVariableHasOnlyScaledSafeReads(ast, cell, listName, procMap)) {
      selected.add(listName);
    }
  }
  return selected;
}

function collectCoordListCellNames(ast: ProgramAst): Set<string> {
  const names = new Set<string>();
  const visitExpr = (expr: ExpressionAst): void => {
    const hasCall = coordListHasCall(expr);
    if (hasCall?.cell.kind === "identifier") names.add(hasCall.cell.name);
    const lineCount = coordListLineCountCall(expr);
    if (lineCount?.cell.kind === "identifier") names.add(lineCount.cell.name);
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
    const hasCall = coordListHasConditionCall(condition);
    if (hasCall?.cell.kind === "identifier") names.add(hasCall.cell.name);
    visitExpr(condition.left);
    visitExpr(condition.right);
  };
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "pause" || statement.kind === "halt") visitExpr(statement.expr);
      if (statement.kind === "ask" && statement.prompt !== undefined) visitExpr(statement.prompt);
      if (statement.kind === "if") {
        visitCondition(statement.condition);
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "switch") {
        visitExpr(statement.expr);
        for (const switchCase of statement.cases) visit(switchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
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
  return names;
}

function collectScaledCoordCellNames(ast: ProgramAst, scaledLists: ReadonlySet<string>): Set<string> {
  const names = new Set<string>();
  if (scaledLists.size === 0) return names;
  const add = (call: CoordListCall | undefined): void => {
    if (call?.cell.kind !== "identifier") return;
    const listName = coordListNameFromItems(call.items);
    if (listName !== undefined && scaledLists.has(listName)) names.add(call.cell.name);
  };
  const visitExpr = (expr: ExpressionAst): void => {
    add(coordListHasCall(expr));
    add(coordListLineCountCall(expr));
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
    add(coordListHasConditionCall(condition));
    visitExpr(condition.left);
    visitExpr(condition.right);
  };
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "pause" || statement.kind === "halt") visitExpr(statement.expr);
      if (statement.kind === "ask" && statement.prompt !== undefined) visitExpr(statement.prompt);
      if (statement.kind === "if") {
        visitCondition(statement.condition);
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "switch") {
        visitExpr(statement.expr);
        for (const switchCase of statement.cases) visit(switchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
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
  return names;
}

function collectCoordListPackedReportTargets(ast: ProgramAst): Set<string> {
  const names = new Set<string>();
  const procMap = new Map(ast.procs.map((proc) => [proc.name, proc]));

  const tryAdd = (
    assignment: Extract<StatementAst, { kind: "assign" }> | undefined,
    statement: StatementAst | undefined,
  ): void => {
    if (assignment === undefined || statement?.kind !== "show") return;
    const call = coordListLineCountCall(assignment.expr);
    if (call === undefined) return;
    const display = ast.displays.find((candidate) => candidate.name === statement.display);
    if (display === undefined) return;
    const template = dashedCoordReportDisplayTemplate(display);
    if (template === undefined) return;
    if (assignment.target !== template.bearing.name) return;
    if (!expressionEquals(call.cell, { kind: "identifier", name: template.cell.name })) return;
    names.add(assignment.target);
  };

  const visit = (statements: StatementAst[]): void => {
    for (let index = 0; index < statements.length; index += 1) {
      const statement = statements[index]!;
      tryAdd(coordListLineCountAssignmentFromStatement(statement, procMap), statements[index + 1]);
      if (statement.kind === "if" && statement.elseBody === undefined) {
        const afterIf = statements[index + 1];
        if (afterIf !== undefined) {
          tryAdd(coordListLineCountAssignmentFromStatement(afterIf, procMap), statements[index + 2]);
        }
      }
      if (statement.kind === "if") {
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
  return names;
}

function coordListLineCountAssignmentFromStatement(
  statement: StatementAst,
  procMap: ReadonlyMap<string, ProgramAst["procs"][number]>,
): Extract<StatementAst, { kind: "assign" }> | undefined {
  if (statement.kind === "assign" && coordListLineCountCall(statement.expr) !== undefined) return statement;
  if (statement.kind !== "call") return undefined;
  const proc = procMap.get(statement.block);
  if (proc?.body.length !== 1) return undefined;
  const [only] = proc.body;
  return only?.kind === "assign" && coordListLineCountCall(only.expr) !== undefined ? only : undefined;
}

function coordListEligibleForScaledDecimalStorage(ast: ProgramAst, listName: string): boolean {
  const field = ast.v2?.state.find((candidate) => candidate.name === listName && candidate.type === "coord_list");
  if (field?.domain === undefined) return false;
  const board = ast.v2?.boards.find((candidate) => candidate.name === field.domain);
  return board?.xMin === 0 &&
    board.xMax === 9 &&
    board.yMin === 0 &&
    board.yMax === 9 &&
    board.width === 10 &&
    board.height === 10;
}

function coordVariableHasOnlyScaledSafeReads(
  ast: ProgramAst,
  cellName: string,
  listName: string,
  procMap: ReadonlyMap<string, ProgramAst["procs"][number]>,
): boolean {
  const callIsSafe = (call: CoordListCall | undefined): boolean =>
    call?.cell.kind === "identifier" &&
    call.cell.name === cellName &&
    coordListNameFromItems(call.items) === listName;

  const exprSafe = (expr: ExpressionAst): boolean => {
    const hasCall = coordListHasCall(expr);
    if (callIsSafe(hasCall)) return true;
    const lineCount = coordListLineCountCall(expr);
    if (callIsSafe(lineCount)) return true;
    if (expr.kind === "identifier") return expr.name !== cellName;
    if (expr.kind === "unary") return exprSafe(expr.expr);
    if (expr.kind === "binary") return exprSafe(expr.left) && exprSafe(expr.right);
    if (expr.kind === "call") return expr.args.every(exprSafe);
    return true;
  };

  const conditionSafe = (condition: ConditionAst): boolean => {
    if (callIsSafe(coordListHasConditionCall(condition))) return true;
    return exprSafe(condition.left) && exprSafe(condition.right);
  };

  const statementsSafe = (statements: StatementAst[], seenProcs = new Set<string>()): boolean => {
    for (const statement of statements) {
      if (statement.kind === "assign" && !exprSafe(statement.expr)) return false;
      if ((statement.kind === "pause" || statement.kind === "halt") && !exprSafe(statement.expr)) return false;
      if (statement.kind === "ask" && statement.prompt !== undefined && !exprSafe(statement.prompt)) return false;
      if (statement.kind === "if") {
        if (!conditionSafe(statement.condition)) return false;
        if (!statementsSafe(statement.thenBody, seenProcs)) return false;
        if (statement.elseBody !== undefined && !statementsSafe(statement.elseBody, seenProcs)) return false;
      }
      if (statement.kind === "loop" && !statementsSafe(statement.body, seenProcs)) return false;
      if (statement.kind === "switch") {
        if (!exprSafe(statement.expr)) return false;
        for (const switchCase of statement.cases) {
          if (!statementsSafe(switchCase.body, seenProcs)) return false;
        }
        if (statement.defaultBody !== undefined && !statementsSafe(statement.defaultBody, seenProcs)) return false;
      }
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) {
          if (!statementsSafe(dispatchCase.body, seenProcs)) return false;
        }
        if (statement.defaultBody !== undefined && !statementsSafe(statement.defaultBody, seenProcs)) return false;
      }
      if (statement.kind === "core") {
        for (const input of statement.inputs ?? []) {
          if (!exprSafe(input.expr)) return false;
        }
      }
      if (statement.kind === "call") {
        const proc = procMap.get(statement.block);
        if (proc !== undefined && !seenProcs.has(proc.name)) {
          seenProcs.add(proc.name);
          const ok = statementsSafe(proc.body, seenProcs);
          seenProcs.delete(proc.name);
          if (!ok) return false;
        }
      }
    }
    return true;
  };

  for (const display of ast.displays) {
    if (!display.items.some((item) => item.kind === "source" && item.name === cellName)) continue;
    const template = dashedCoordReportDisplayTemplate(display);
    if (template?.cell.name !== cellName) return false;
  }
  return ast.entries.every((entry) => statementsSafe(entry.body)) &&
    ast.procs.every((proc) => statementsSafe(proc.body)) &&
    ast.blocks.every((block) => statementsSafe(block.body));
}

function coordListItemInfo(name: string): { listName: string; index: number } | undefined {
  if (!name.startsWith(COORD_LIST_ITEM_PREFIX)) return undefined;
  const match = /^__coord_list_(.+)_(\d+)$/u.exec(name);
  if (!match) return undefined;
  return { listName: match[1]!, index: Number(match[2]) };
}

function isPreincrementIndirectRegister(register: RegisterName): boolean {
  return register === "4" || register === "5" || register === "6";
}

function collectProcCallCounts(ast: ProgramAst): Map<string, number> {
  const procNames = new Set(ast.procs.map((proc) => proc.name));
  const counts = countStatementCalls(ast);
  const procCounts = new Map<string, number>();
  for (const [name, count] of counts) {
    if (procNames.has(name)) procCounts.set(name, count);
  }
  return procCounts;
}

function collectRecursiveProcNames(ast: ProgramAst): Set<string> {
  const procNames = new Set(ast.procs.map((proc) => proc.name));
  const recursive = new Set<string>();

  const visit = (statements: StatementAst[], currentProc?: string): void => {
    for (const statement of statements) {
      if (statement.kind === "call" && procNames.has(statement.block) && statement.block === currentProc) {
        recursive.add(statement.block);
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
  return recursive;
}

function findSingleUseProcNames(ast: ProgramAst): Set<string> {
  const counts = collectProcCallCounts(ast);
  const recursive = collectRecursiveProcNames(ast);

  return new Set(
    [...counts.entries()]
      .filter(([name, count]) => count === 1 && !recursive.has(name))
      .map(([name]) => name),
  );
}

function findInlineProcNamesBySize(ast: ProgramAst, counts = collectProcCallCounts(ast)): Set<string> {
  const recursive = collectRecursiveProcNames(ast);
  const inlineNames = new Set<string>();
  for (const proc of ast.procs) {
    const uses = counts.get(proc.name) ?? 0;
    if (uses === 0 || recursive.has(proc.name)) continue;
    const bodyCost = estimateBranchOrderBodyCost(proc.body, ast);
    if (!Number.isFinite(bodyCost)) {
      if (uses === 1) inlineNames.add(proc.name);
      continue;
    }
    const terminal = statementListTerminatesStatically(proc.body, ast);
    if (uses > 1 && !isStraightLineAssignmentBody(proc.body)) continue;
    if (uses > 1 && terminal) continue;
    const subroutineCost = bodyCost + (terminal ? 0 : 1) + uses * 2;
    const inlineCost = bodyCost * uses;
    if (inlineCost < subroutineCost) inlineNames.add(proc.name);
  }
  return inlineNames;
}

function isStraightLineAssignmentBody(statements: readonly StatementAst[]): boolean {
  return statements.length > 0 && statements.every((statement) => statement.kind === "assign");
}

function statementListTerminatesStatically(statements: readonly StatementAst[], ast: ProgramAst): boolean {
  return statementListTerminatesStaticallySeen(statements, ast, new Set());
}

function statementListTerminatesStaticallySeen(
  statements: readonly StatementAst[],
  ast: ProgramAst,
  seenProcs: Set<string>,
): boolean {
  const last = statements.at(-1);
  return last !== undefined && statementTerminatesStatically(last, ast, seenProcs);
}

function statementTerminatesStatically(
  statement: StatementAst,
  ast: ProgramAst,
  seenProcs: Set<string>,
): boolean {
  if (statement.kind === "halt" || statement.kind === "loop" || statement.kind === "trap") return true;
  if (statement.kind === "if") {
    return statement.elseBody !== undefined &&
      statementListTerminatesStaticallySeen(statement.thenBody, ast, new Set(seenProcs)) &&
      statementListTerminatesStaticallySeen(statement.elseBody, ast, new Set(seenProcs));
  }
  if (statement.kind === "dispatch") {
    return statement.defaultBody !== undefined &&
      statementListTerminatesStaticallySeen(statement.defaultBody, ast, new Set(seenProcs)) &&
      statement.cases.every((dispatchCase) =>
        statementListTerminatesStaticallySeen(dispatchCase.body, ast, new Set(seenProcs)),
      );
  }
  if (statement.kind !== "call") return false;
  const block = ast.blocks.find((candidate) => candidate.name === statement.block);
  if (block !== undefined) return block.mode !== "inline";
  const proc = ast.procs.find((candidate) => candidate.name === statement.block);
  if (proc === undefined || seenProcs.has(proc.name)) return false;
  seenProcs.add(proc.name);
  return statementListTerminatesStaticallySeen(proc.body, ast, seenProcs);
}

function eliminateUnreachableV2Procs(ast: ProgramAst, optimizations: AppliedOptimization[]): void {
  if (!ast.v2 || ast.procs.length === 0) return;
  const reachable = collectReachableProcNames(ast);
  const before = ast.procs.length;
  ast.procs = ast.procs.filter((proc) => reachable.has(proc.name));
  const removed = before - ast.procs.length;
  if (removed === 0) return;
  optimizations.push({
    name: "dead-proc-elimination",
    detail: `Removed ${removed} unreachable lowered rule proc(s) after high-level match/effect lowering.`,
  });
}

function elideTerminalLoopHeaderShows(ast: ProgramAst, optimizations: AppliedOptimization[]): void {
  const procMap = new Map(ast.procs.map((proc) => [proc.name, proc]));
  if (procMap.size === 0) return;

  const allTerminalCallsByDisplay = new Map<string, Set<string>>();
  const nonTerminalCalls = new Set<string>();
  const visitEveryStatementList = (statements: StatementAst[], terminalDisplay?: string): void => {
    for (let index = 0; index < statements.length; index += 1) {
      const statement = statements[index]!;
      const atTail = index === statements.length - 1;
      collectCallsByPosition(statement, atTail ? terminalDisplay : undefined, procMap, allTerminalCallsByDisplay, nonTerminalCalls);
      if (statement.kind === "loop") visitEveryStatementList(statement.body, loopHeaderDisplay(statement));
      if (statement.kind === "if") {
        visitEveryStatementList(statement.thenBody, atTail ? terminalDisplay : undefined);
        if (statement.elseBody !== undefined) visitEveryStatementList(statement.elseBody, atTail ? terminalDisplay : undefined);
      }
      if (statement.kind === "switch") {
        for (const switchCase of statement.cases) visitEveryStatementList(switchCase.body, atTail ? terminalDisplay : undefined);
        if (statement.defaultBody !== undefined) visitEveryStatementList(statement.defaultBody, atTail ? terminalDisplay : undefined);
      }
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visitEveryStatementList(dispatchCase.body, atTail ? terminalDisplay : undefined);
        if (statement.defaultBody !== undefined) visitEveryStatementList(statement.defaultBody, atTail ? terminalDisplay : undefined);
      }
    }
  };

  for (const entry of ast.entries) visitEveryStatementList(entry.body);
  for (const block of ast.blocks) visitEveryStatementList(block.body);
  for (const proc of ast.procs) {
    collectStatementListCallsByPosition(proc.body, "__mkpro_terminal_proc_tail", procMap, new Map(), nonTerminalCalls);
  }

  let removed = 0;
  for (const [display, calls] of allTerminalCallsByDisplay) {
    const terminalProcs = expandTerminalProcClosure(calls, procMap, nonTerminalCalls);
    for (const name of terminalProcs) {
      const proc = procMap.get(name);
      if (proc === undefined) continue;
      removed += elideTailShowInStatementList(proc.body, display);
    }
  }

  if (removed === 0) return;
  optimizations.push({
    name: "terminal-loop-screen-elision",
    detail: `Elided ${removed} terminal show${removed === 1 ? "" : "s"} already provided by the next loop header.`,
  });
}

function loopHeaderDisplay(statement: Extract<StatementAst, { kind: "loop" }>): string | undefined {
  const first = statement.body[0];
  const second = statement.body[1];
  return first?.kind === "show" && second?.kind === "input" ? first.display : undefined;
}

function collectCallsByPosition(
  statement: StatementAst,
  terminalDisplay: string | undefined,
  procMap: ReadonlyMap<string, ProgramAst["procs"][number]>,
  terminalCallsByDisplay: Map<string, Set<string>>,
  nonTerminalCalls: Set<string>,
): void {
  if (statement.kind === "call" && procMap.has(statement.block)) {
    if (terminalDisplay === undefined) {
      nonTerminalCalls.add(statement.block);
    } else {
      const calls = terminalCallsByDisplay.get(terminalDisplay) ?? new Set<string>();
      calls.add(statement.block);
      terminalCallsByDisplay.set(terminalDisplay, calls);
    }
    return;
  }

  if (statement.kind === "if") {
    collectStatementListCallsByPosition(statement.thenBody, terminalDisplay, procMap, terminalCallsByDisplay, nonTerminalCalls);
    if (statement.elseBody !== undefined) {
      collectStatementListCallsByPosition(statement.elseBody, terminalDisplay, procMap, terminalCallsByDisplay, nonTerminalCalls);
    }
  }
  if (statement.kind === "switch") {
    for (const switchCase of statement.cases) {
      collectStatementListCallsByPosition(switchCase.body, terminalDisplay, procMap, terminalCallsByDisplay, nonTerminalCalls);
    }
    if (statement.defaultBody !== undefined) {
      collectStatementListCallsByPosition(statement.defaultBody, terminalDisplay, procMap, terminalCallsByDisplay, nonTerminalCalls);
    }
  }
  if (statement.kind === "dispatch") {
    for (const dispatchCase of statement.cases) {
      collectStatementListCallsByPosition(dispatchCase.body, terminalDisplay, procMap, terminalCallsByDisplay, nonTerminalCalls);
    }
    if (statement.defaultBody !== undefined) {
      collectStatementListCallsByPosition(statement.defaultBody, terminalDisplay, procMap, terminalCallsByDisplay, nonTerminalCalls);
    }
  }
}

function collectStatementListCallsByPosition(
  statements: StatementAst[],
  terminalDisplay: string | undefined,
  procMap: ReadonlyMap<string, ProgramAst["procs"][number]>,
  terminalCallsByDisplay: Map<string, Set<string>>,
  nonTerminalCalls: Set<string>,
): void {
  for (let index = 0; index < statements.length; index += 1) {
    collectCallsByPosition(
      statements[index]!,
      index === statements.length - 1 ? terminalDisplay : undefined,
      procMap,
      terminalCallsByDisplay,
      nonTerminalCalls,
    );
  }
}

function expandTerminalProcClosure(
  initial: ReadonlySet<string>,
  procMap: ReadonlyMap<string, ProgramAst["procs"][number]>,
  nonTerminalCalls: ReadonlySet<string>,
): Set<string> {
  const result = new Set<string>();
  const queue = [...initial].filter((name) => !nonTerminalCalls.has(name));
  while (queue.length > 0) {
    const name = queue.shift()!;
    if (result.has(name) || nonTerminalCalls.has(name)) continue;
    const proc = procMap.get(name);
    if (proc === undefined) continue;
    result.add(name);
    const terminalCalls = new Set<string>();
    collectStatementListTerminalCalls(proc.body, procMap, terminalCalls);
    for (const nested of terminalCalls) {
      if (!result.has(nested) && !nonTerminalCalls.has(nested)) queue.push(nested);
    }
  }
  return result;
}

function collectStatementListTerminalCalls(
  statements: StatementAst[],
  procMap: ReadonlyMap<string, ProgramAst["procs"][number]>,
  terminalCalls: Set<string>,
): void {
  const last = statements.at(-1);
  if (last === undefined) return;
  if (last.kind === "call" && procMap.has(last.block)) {
    terminalCalls.add(last.block);
    return;
  }
  if (last.kind === "if") {
    collectStatementListTerminalCalls(last.thenBody, procMap, terminalCalls);
    if (last.elseBody !== undefined) collectStatementListTerminalCalls(last.elseBody, procMap, terminalCalls);
  }
  if (last.kind === "switch") {
    for (const switchCase of last.cases) collectStatementListTerminalCalls(switchCase.body, procMap, terminalCalls);
    if (last.defaultBody !== undefined) collectStatementListTerminalCalls(last.defaultBody, procMap, terminalCalls);
  }
  if (last.kind === "dispatch") {
    for (const dispatchCase of last.cases) collectStatementListTerminalCalls(dispatchCase.body, procMap, terminalCalls);
    if (last.defaultBody !== undefined) collectStatementListTerminalCalls(last.defaultBody, procMap, terminalCalls);
  }
}

function elideTailShowInStatementList(statements: StatementAst[], display: string): number {
  const last = statements.at(-1);
  if (last === undefined) return 0;
  if (last.kind === "show" && last.display === display) {
    statements.pop();
    return 1;
  }
  let removed = 0;
  if (last.kind === "if") {
    removed += elideTailShowInStatementList(last.thenBody, display);
    if (last.elseBody !== undefined) removed += elideTailShowInStatementList(last.elseBody, display);
  }
  if (last.kind === "switch") {
    for (const switchCase of last.cases) removed += elideTailShowInStatementList(switchCase.body, display);
    if (last.defaultBody !== undefined) removed += elideTailShowInStatementList(last.defaultBody, display);
  }
  if (last.kind === "dispatch") {
    for (const dispatchCase of last.cases) removed += elideTailShowInStatementList(dispatchCase.body, display);
    if (last.defaultBody !== undefined) removed += elideTailShowInStatementList(last.defaultBody, display);
  }
  return removed;
}

function collectReachableProcNames(ast: ProgramAst): Set<string> {
  const procMap = new Map(ast.procs.map((proc) => [proc.name, proc]));
  const blockMap = new Map(ast.blocks.map((block) => [block.name, block]));
  const reachableProcs = new Set<string>();
  const reachableBlocks = new Set<string>();
  const procQueue: string[] = [];
  const blockQueue: string[] = [];

  const enqueueCall = (name: string): void => {
    if (blockMap.has(name)) {
      if (!reachableBlocks.has(name)) {
        reachableBlocks.add(name);
        blockQueue.push(name);
      }
      return;
    }
    if (procMap.has(name) && !reachableProcs.has(name)) {
      reachableProcs.add(name);
      procQueue.push(name);
    }
  };

  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "call") enqueueCall(statement.block);
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "while") visit(statement.body);
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
  while (procQueue.length > 0 || blockQueue.length > 0) {
    const procName = procQueue.shift();
    if (procName !== undefined) {
      const proc = procMap.get(procName);
      if (proc !== undefined) visit(proc.body);
      continue;
    }
    const blockName = blockQueue.shift();
    if (blockName !== undefined) {
      const block = blockMap.get(blockName);
      if (block !== undefined) visit(block.body);
    }
  }
  return reachableProcs;
}

interface DomainBinding {
  name: string;
  storage?: StorageHint;
}

function allocateRegisters(
  ast: ProgramAst,
  diagnostics: Diagnostic[],
  freeResidualDispatchScratch = false,
  suppressConstantPreloads: ReadonlySet<string> = new Set(),
  sharedBitMaskHelperCalls = false,
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
  applyCoordListRegisterHints(variables, hints);
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
  collectDispatchScratchVariables(ast, variables, freeResidualDispatchScratch);
  collectTicTacToeScratchVariables(ast, variables);
  collectBitMaskScratchVariables(ast, variables);
  collectCoordListScratchVariables(ast, variables);
  collectSpatialHitScratchVariables(ast, variables, sharedBitMaskHelperCalls);
  collectSpatialCountScratchVariables(ast, variables);
  collectGuardedUpdateScratchVariables(ast, variables);
  collectDisplayTemplateScratchVariables(ast, variables);
  applyCoordListRegisterHints(variables, hints);
  applyCoordListPackedReportRegisterHints(ast, variables, hints);
  applyCoordListCellRegisterHints(ast, variables, hints);

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
  const reusableScratchRegisters = new Map<string, RegisterName>();
  for (const variable of ordered) {
    if (registers[variable]) continue;
    const reusableScratch = reusableScratchFamily(variable);
    const existingScratchRegister = reusableScratch === undefined ? undefined : reusableScratchRegisters.get(reusableScratch);
    if (existingScratchRegister !== undefined) {
      registers[variable] = existingScratchRegister;
      continue;
    }
    const hint = hints.get(variable);
    if (hint?.mode === "prefer" && !used.has(hint.register)) {
      registers[variable] = hint.register;
      used.add(hint.register);
      if (reusableScratch !== undefined) reusableScratchRegisters.set(reusableScratch, hint.register);
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
    if (reusableScratch !== undefined) reusableScratchRegisters.set(reusableScratch, candidate);
  }

  const constants: Record<string, RegisterName> = {};
  for (const value of collectPreloadConstantValues(ast)) {
    if (suppressConstantPreloads.has(value)) continue;
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

function reusableScratchFamily(variable: string): string | undefined {
  if (variable.startsWith(BIT_MASK_SCRATCH_PREFIX)) return BIT_MASK_SCRATCH_PREFIX;
  return undefined;
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

function applyCoordListRegisterHints(
  variables: Set<string>,
  hints: Map<string, { mode: "prefer" | "fixed"; register: RegisterName }>,
): void {
  const itemRegisters: RegisterName[] = ["6", "7", "8", "9", "a", "b", "c", "d", "e"];
  const groups = new Map<string, Array<{ name: string; index: number }>>();
  for (const variable of variables) {
    const item = coordListItemInfo(variable);
    if (item === undefined) continue;
    const group = groups.get(item.listName) ?? [];
    group.push({ name: variable, index: item.index });
    groups.set(item.listName, group);
  }
  for (const group of groups.values()) {
    for (const item of group) {
      const register = itemRegisters[item.index];
      if (register !== undefined && !hints.has(item.name)) {
        hints.set(item.name, { mode: "prefer", register });
      }
    }
  }

  const scratchHints: Array<[string, RegisterName]> = [
    [COORD_LIST_POINTER, "5"],
    [COORD_LIST_COUNTER, "2"],
    [COORD_LIST_CURRENT, "0"],
    [COORD_LIST_DX, "1"],
  ];
  for (const [name, register] of scratchHints) {
    if (variables.has(name) && !hints.has(name)) hints.set(name, { mode: "prefer", register });
  }
}

function applyCoordListCellRegisterHints(
  ast: ProgramAst,
  variables: Set<string>,
  hints: Map<string, { mode: "prefer" | "fixed"; register: RegisterName }>,
): void {
  const packedTargets = collectCoordListPackedReportTargets(ast);
  for (const name of collectCoordListCellNames(ast)) {
    if (!variables.has(name) || hints.has(name)) continue;
    hints.set(name, { mode: "prefer", register: packedTargets.size > 0 ? "3" : "4" });
  }
}

function applyCoordListPackedReportRegisterHints(
  ast: ProgramAst,
  variables: Set<string>,
  hints: Map<string, { mode: "prefer" | "fixed"; register: RegisterName }>,
): void {
  for (const name of collectCoordListPackedReportTargets(ast)) {
    if (variables.has(name) && !hints.has(name)) hints.set(name, { mode: "prefer", register: "4" });
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
  if (variable.startsWith(DISPLAY_TEMPLATE_LOOP_PREFIX)) {
    for (const candidate of ["1", "0", "2", "3"] satisfies RegisterName[]) {
      if (!used.has(candidate)) return candidate;
    }
    return undefined;
  }
  if (variable.startsWith(DISPLAY_TEMPLATE_VALUE_PREFIX)) {
    for (const candidate of ["2", "0", "3", "4", "5", "6", "8", "9", "a", "b", "c", "d", "e"] satisfies RegisterName[]) {
      if (!used.has(candidate)) return candidate;
    }
    return undefined;
  }
  if (variable.startsWith(DISPLAY_TEMPLATE_MASK_PREFIX)) {
    for (let i = REGISTER_ORDER.length - 1; i >= 0; i -= 1) {
      const candidate = REGISTER_ORDER[i]!;
      if (!used.has(candidate)) return candidate;
    }
    return undefined;
  }
  if (variable === COORD_LIST_POINTER) {
    for (const candidate of ["5", "4", "6"] satisfies RegisterName[]) {
      if (!used.has(candidate)) return candidate;
    }
    return undefined;
  }
  if (variable === COORD_LIST_COUNTER) {
    for (const candidate of ["2", "3", "1", "0"] satisfies RegisterName[]) {
      if (!used.has(candidate)) return candidate;
    }
    return undefined;
  }
  if (variable === COORD_LIST_CURRENT || variable === COORD_LIST_DX) {
    for (const candidate of ["0", "1", "3", "4"] satisfies RegisterName[]) {
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
  if (programContainsCall(ast, "__direction_cardinal")) {
    values.add("10");
  }
  if (programContainsCall(ast, "line_count")) {
    values.add("10");
    values.add("11");
    values.add("19");
    values.add("-99");
    values.add("-81");
  }
  for (const display of ast.displays) {
    if (displayHasMantissaExponentTemplateShape(display)) {
      values.add("1000");
      values.add("10000000");
    }
    const mantissaMask = displayMantissaMaskTextForAst(ast, display);
    if (mantissaMask !== undefined) values.add(normalizeConstantLiteral(mantissaMask));
    const literal = literalOnlyDisplayText(display);
    const literalProgram = literal === undefined ? undefined : displayLiteralProgram(literal);
    if (literal !== undefined && shouldUsePreloadedDisplayLiteral(literal)) {
      values.add(normalizeConstantLiteral(literal));
    }
    if (literalProgram?.kind === "kinv" && estimateNumberCost(literalProgram.digits) > 1) {
      values.add(normalizeConstantLiteral(literalProgram.digits));
    }
    if (literalProgram?.kind === "xor") {
      if (estimateNumberCost(literalProgram.left) > 1) values.add(normalizeConstantLiteral(literalProgram.left));
      if (estimateNumberCost(literalProgram.right) > 1) values.add(normalizeConstantLiteral(literalProgram.right));
    }

    let seenSource = false;
    for (const item of display.items) {
      if (item.kind !== "source") continue;
      if (seenSource) {
        const width = item.width ?? naturalDisplayWidthForAst(ast, item.name);
        const scale = String(10 ** width);
        if (estimateNumberCost(scale) > 1) values.add(scale);
      }
      seenSource = true;
    }
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

function naturalDisplayWidthForAst(ast: ProgramAst, source: string): number {
  const field = findStateFieldInAst(ast, source);
  if (field !== undefined) {
    const min = field.min ?? 0;
    const max = field.max ?? min;
    const magnitude = Math.max(Math.abs(min), Math.abs(max));
    return Math.max(1, String(Math.trunc(magnitude)).length);
  }
  return 1;
}

function findStateFieldInAst(ast: ProgramAst, source: string): StateFieldAst | undefined {
  for (const state of ast.states) {
    const field = state.fields.find((candidate) => candidate.name === source);
    if (field !== undefined) return field;
  }
  return undefined;
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

function programHasLineCountForMask(ast: ProgramAst, maskName: string): boolean {
  let found = false;
  const visitExpr = (expr: ExpressionAst): void => {
    if (found) return;
    if (
      expr.kind === "call" &&
      expr.callee.toLowerCase() === "line_count" &&
      expr.args[0]?.kind === "identifier" &&
      expr.args[0].name === maskName
    ) {
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

function collectXParamProcLowerings(
  ast: ProgramAst,
  readCounts: ReadonlyMap<string, number>,
  inlineProcNames: ReadonlySet<string>,
): Map<string, XParamProcLowering> {
  const result = new Map<string, XParamProcLowering>();
  const v2Rules = new Map(ast.v2?.rules.map((rule) => [rule.name, rule]) ?? []);
  for (const proc of ast.procs) {
    if (inlineProcNames.has(proc.name)) continue;
    const params = v2Rules.get(proc.name)?.params ?? [];
    if (params.length !== 1) continue;
    const param = params[0]!;
    if ((readCounts.get(param) ?? 0) !== 1) continue;
    const first = proc.body[0];
    if (first?.kind !== "assign") continue;
    const matched = matchXParamFirstAssignment(first, param);
    if (matched === undefined) continue;
    if (statementsReadIdentifier(proc.body.slice(1), param)) continue;
    if (!allProcCallsHaveImmediateParamAssignment(ast, proc.name, param)) continue;
    result.set(proc.name, { param, first, other: matched.other });
  }
  return result;
}

function matchXParamFirstAssignment(
  statement: Extract<StatementAst, { kind: "assign" }>,
  param: string,
): { other: string } | undefined {
  const expr = statement.expr;
  if (expr.kind !== "binary" || expr.op !== "+") return undefined;
  if (expr.left.kind === "identifier" && expr.left.name === param && expr.right.kind === "identifier") {
    return { other: expr.right.name };
  }
  if (expr.right.kind === "identifier" && expr.right.name === param && expr.left.kind === "identifier") {
    return { other: expr.left.name };
  }
  return undefined;
}

function allProcCallsHaveImmediateParamAssignment(ast: ProgramAst, procName: string, param: string): boolean {
  let calls = 0;
  let ok = true;
  const visit = (statements: readonly StatementAst[]): void => {
    for (let index = 0; index < statements.length; index += 1) {
      const statement = statements[index]!;
      if (statement.kind === "call" && statement.block === procName) {
        calls += 1;
        const previous = statements[index - 1];
        if (previous?.kind !== "assign" || previous.target !== param) ok = false;
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
  for (const proc of ast.procs) {
    if (proc.name !== procName) visit(proc.body);
  }
  for (const block of ast.blocks) visit(block.body);
  return ok && calls > 0;
}

function statementsReadIdentifier(statements: readonly StatementAst[], name: string): boolean {
  return statements.some((statement) => statementReadsIdentifier(statement, name));
}

function statementReadsIdentifier(statement: StatementAst, name: string): boolean {
  switch (statement.kind) {
    case "pause":
    case "halt":
      return expressionReferencesIdentifier(statement.expr, name);
    case "ask":
      return statement.prompt !== undefined && expressionReferencesIdentifier(statement.prompt, name);
    case "assign":
      return expressionReferencesIdentifier(statement.expr, name);
    case "if":
      return expressionReferencesIdentifier(statement.condition.left, name) ||
        expressionReferencesIdentifier(statement.condition.right, name) ||
        statementsReadIdentifier(statement.thenBody, name) ||
        (statement.elseBody !== undefined && statementsReadIdentifier(statement.elseBody, name));
    case "loop":
      return statementsReadIdentifier(statement.body, name);
    case "while":
      return expressionReferencesIdentifier(statement.condition.left, name) ||
        expressionReferencesIdentifier(statement.condition.right, name) ||
        statementsReadIdentifier(statement.body, name);
    case "switch":
      return expressionReferencesIdentifier(statement.expr, name) ||
        statement.cases.some((switchCase) =>
          expressionReferencesIdentifier(switchCase.value, name) || statementsReadIdentifier(switchCase.body, name)
        ) ||
        (statement.defaultBody !== undefined && statementsReadIdentifier(statement.defaultBody, name));
    case "dispatch":
      return expressionReferencesIdentifier(statement.expr, name) ||
        statement.cases.some((dispatchCase) =>
          expressionReferencesIdentifier(dispatchCase.value, name) || statementsReadIdentifier(dispatchCase.body, name)
        ) ||
        (statement.defaultBody !== undefined && statementsReadIdentifier(statement.defaultBody, name));
    case "show":
    case "input":
    case "call":
    case "egg":
    case "decimal_series":
      return false;
    case "core":
      return statement.inputs?.some((input) => expressionReferencesIdentifier(input.expr, name)) ?? false;
    case "trap":
      return expressionReferencesIdentifier(statement.expr, name);
  }
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

function collectNearAnyHelperStats(
  ast: ProgramAst,
  preloadedConstants: ReadonlySet<string>,
): Map<string, NearAnyHelperStats> {
  const stats = new Map<string, NearAnyHelperStats>();
  const add = (condition: ConditionAst): void => {
    const selected = selectCheaperEquivalentCondition(condition, ast, preloadedConstants).condition;
    const match = matchNearAnyHelperCondition(selected);
    if (match === undefined) return;
    const key = nearAnyHelperKey(match.value, match.radius);
    const candidateCallCost = match.candidates.reduce(
      (sum, candidate) => sum + estimateExpressionCostForCondition(candidate, preloadedConstants) + 2,
      0,
    );
    const helperConditionCost = candidateCallCost + Math.max(0, match.candidates.length - 1) + 2;
    const ordinaryCost = conditionCompileCost(selected, preloadedConstants);
    const existing = stats.get(key);
    if (existing === undefined) {
      const helperBodyCost =
        estimateExpressionCostForCondition(match.value, preloadedConstants) +
        estimateExpressionCostForCondition(match.radius, preloadedConstants) +
        5;
      stats.set(key, {
        candidateCount: match.candidates.length,
        conditionCount: 1,
        ordinaryCost,
        helperCallCost: helperConditionCost,
        helperCost: helperBodyCost + helperConditionCost,
      });
      return;
    }
    existing.candidateCount += match.candidates.length;
    existing.conditionCount += 1;
    existing.ordinaryCost += ordinaryCost;
    existing.helperCallCost += helperConditionCost;
    existing.helperCost += helperConditionCost;
  };
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "if") {
        add(statement.condition);
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
  return stats;
}

function estimatePackedDisplayBodyCost(widthsOrCount: number | readonly number[]): number {
  if (typeof widthsOrCount === "number") {
    return widthsOrCount === 0 ? 2 : widthsOrCount * 2;
  }
  if (widthsOrCount.length === 0) return 2;
  return 2 + widthsOrCount.slice(1).reduce((cost, width) => cost + String(10 ** width).length + 3, 0);
}

function displayByteBuilderSupportsLiteral(text: string): boolean {
  return /^[\s.-]+$/u.test(text);
}

function displayHasMantissaExponentTemplateShape(display: ProgramAst["displays"][number]): boolean {
  const [leader, firstLiteral, score, secondLiteral, total, thirdLiteral, exponent] = display.items;
  return display.items.length === 7 &&
    leader?.kind === "source" &&
    firstLiteral?.kind === "literal" &&
    normalizeDisplayTemplateLiteral(firstLiteral.text) === ".-" &&
    score?.kind === "source" &&
    secondLiteral?.kind === "literal" &&
    normalizeDisplayTemplateLiteral(secondLiteral.text) === "-" &&
    total?.kind === "source" &&
    thirdLiteral?.kind === "literal" &&
    normalizeDisplayTemplateLiteral(thirdLiteral.text) === "-" &&
    exponent?.kind === "source";
}

function displayMantissaMaskTextForAst(
  ast: ProgramAst,
  display: ProgramAst["displays"][number],
): string | undefined {
  const [first, ...rest] = display.items;
  if (first?.kind !== "source" || rest.length === 0) return undefined;
  const leaderWidth = first.width ?? naturalDisplayWidthForAst(ast, first.name);
  const leader = findStateFieldInAst(ast, first.name);
  if (leaderWidth !== 1 || leader === undefined || (leader.min ?? 0) <= 0 || (leader.max ?? 0) > 9) {
    return undefined;
  }

  const maskCells = [8];
  let width = 1;
  let hasVideoLiteral = false;
  for (const item of rest) {
    if (item.kind === "source") {
      const sourceWidth = item.width ?? naturalDisplayWidthForAst(ast, item.name);
      const state = findStateFieldInAst(ast, item.name);
      if (state === undefined || (state.min ?? 0) < 0 || (state.max ?? 0) >= 10 ** sourceWidth) return undefined;
      for (let index = 0; index < sourceWidth; index += 1) maskCells.push(0);
      width += sourceWidth;
      continue;
    }
    const cells = displayLiteralMantissaCells(item.text);
    if (cells === undefined) return undefined;
    if (cells.some((cell) => cell > 9)) hasVideoLiteral = true;
    maskCells.push(...cells);
    width += cells.length;
  }
  if (!hasVideoLiteral || width < 2 || width > 8) return undefined;
  return displayCellsLiteral(maskCells);
}

function programUsesDashedCoordReport(ast: ProgramAst): boolean {
  return ast.displays.some((display) => dashedCoordReportDisplayTemplate(display) !== undefined);
}

function dashedCoordReportDisplayTemplate(
  display: ProgramAst["displays"][number],
): DashedCoordReportTemplate | undefined {
  const [prefix, cell, separator, bearing] = display.items;
  if (
    display.items.length !== 4 ||
    prefix?.kind !== "literal" ||
    cell?.kind !== "source" ||
    separator?.kind !== "literal" ||
    bearing?.kind !== "source"
  ) {
    return undefined;
  }
  if (normalizeDisplayTemplateLiteral(prefix.text) !== "--") return undefined;
  if (normalizeDisplayTemplateLiteral(separator.text) !== "--") return undefined;
  const result: DashedCoordReportTemplate = {
    cell: { kind: "source", item: cell, name: cell.name, width: cell.width ?? 2 },
    bearing: { kind: "source", item: bearing, name: bearing.name, width: bearing.width ?? 1 },
  };
  if (result.cell.width !== 2 || result.bearing.width !== 1) return undefined;
  return result;
}

function displayTemplateValueScratchName(display: ProgramAst["displays"][number]): string {
  return `${DISPLAY_TEMPLATE_VALUE_PREFIX}${display.name}`;
}

function displayTemplateLoopScratchName(display: ProgramAst["displays"][number]): string {
  return `${DISPLAY_TEMPLATE_LOOP_PREFIX}${display.name}`;
}

function displayTemplateMaskScratchName(display: ProgramAst["displays"][number]): string {
  return `${DISPLAY_TEMPLATE_MASK_PREFIX}${display.name}`;
}

function firstSpliceDisplayScratchName(display: ProgramAst["displays"][number]): string {
  return `__display_first_${display.name}`;
}

function normalizeDisplayTemplateLiteral(text: string): string {
  return text.replace(/\s/gu, "");
}

function displayLoopOpcode(register: 0 | 1 | 2 | 3): number {
  switch (register) {
    case 0:
      return 0x5d;
    case 1:
      return 0x5b;
    case 2:
      return 0x58;
    case 3:
      return 0x5a;
  }
}

type DisplayLiteralProgram =
  | { kind: "error" }
  | { kind: "kinv"; digits: string; negative: boolean }
  | { kind: "xor"; left: string; right: string; negative: boolean };

interface FirstSpliceDisplayLiteralProgram {
  first: number;
  second?: number;
  body: Exclude<DisplayLiteralProgram, { kind: "error" }>;
  exponent: number;
  negative?: boolean;
}

interface SignDigitLiteralDisplayProgram {
  signDigit: number;
  first: string;
  start: string;
  indirectSteps: number;
}

function displayLiteralProgram(text: string): DisplayLiteralProgram | undefined {
  const normalized = normalizeDisplayLiteralText(text);
  const errorCells = displayLiteralCells(normalized);
  if (errorCells !== undefined && isErrorLiteralCells(errorCells)) return { kind: "error" };

  const negative = normalized.startsWith("-") && normalized.length > 1;
  const body = negative ? normalized.slice(1) : normalized;
  const cells = displayLiteralCells(body);
  return displayLiteralProgramFromCells(cells, negative);
}

function displayLiteralProgramFromCells(
  cells: readonly number[] | undefined,
  negative: boolean,
): DisplayLiteralProgram | undefined {
  if (cells === undefined || cells.length === 0 || cells.length > 8) return undefined;
  if (cells[0] !== 8) return undefined;

  const inverted = displayLiteralInversionDigits(cells);
  if (inverted !== undefined) return { kind: "kinv", digits: inverted, negative };

  const left: string[] = [];
  const right: string[] = [];
  for (let index = 0; index < cells.length; index += 1) {
    const pair = index === 0 ? [1, 9] as const : decimalXorPair(cells[index]!);
    if (pair === undefined) return undefined;
    left.push(String(pair[0]));
    right.push(String(pair[1]));
  }
  return { kind: "xor", left: left.join(""), right: right.join(""), negative };
}

function firstSpliceDisplayLiteralProgram(text: string): FirstSpliceDisplayLiteralProgram | undefined {
  const cells = displayLiteralCells(text);
  if (cells === undefined || cells.length === 0 || cells.length > 8) return undefined;
  return firstSpliceDisplayLiteralProgramFromCells(
    cells,
    displayLiteralPointExponent(text) ?? cells.length - 1,
    false,
  );
}

function firstSpliceDisplayLiteralProgramFromCells(
  cells: readonly number[],
  exponent: number,
  negative: boolean,
): FirstSpliceDisplayLiteralProgram | undefined {
  const first = cells[0]!;
  if (first === 15) return undefined;
  const body = displayLiteralProgramFromCells([8, ...cells.slice(1)], false);
  if (body === undefined || body.kind === "error") return undefined;
  const program: FirstSpliceDisplayLiteralProgram = { first, body, exponent };
  if (cells[1] !== undefined) program.second = cells[1];
  if (negative) program.negative = true;
  return program;
}

function shouldUseFirstSpliceDisplayLiteral(text: string): boolean {
  if (firstSpliceDisplayLiteralProgram(text) === undefined) return false;
  if (decimalDisplayLiteralNumber(text) !== undefined) return false;
  if (zeroDigitTailDisplayProgram(text) !== undefined) return false;
  if (signDigitLiteralDisplayProgram(text) !== undefined) return false;
  const direct = displayLiteralProgram(text);
  return direct === undefined || direct.kind !== "error" && displayLiteralTrailingZeroExponent(text) !== undefined;
}

function signedFirstSpliceDisplayLiteralProgram(text: string): FirstSpliceDisplayLiteralProgram | undefined {
  const normalized = normalizeDisplayLiteralText(text);
  if (!/^-[0-9]/u.test(normalized)) return undefined;
  const body = normalized.slice(1);
  const cells = displayLiteralCells(body);
  if (cells === undefined || cells.length === 0 || cells.length > 8) return undefined;
  return firstSpliceDisplayLiteralProgramFromCells(cells, displayLiteralPointExponent(body) ?? cells.length - 1, true);
}

function exponentTailDisplayLiteralProgram(text: string): FirstSpliceDisplayLiteralProgram | undefined {
  const cells = displayLiteralCells(text);
  if (cells === undefined || cells.length !== 9) return undefined;
  const exponent = cells.at(-1);
  if (exponent === undefined || exponent < 0 || exponent > 9) return undefined;
  return firstSpliceDisplayLiteralProgramFromCells(cells.slice(0, 8), exponent, false);
}

function shouldUsePreloadedDisplayLiteral(text: string): boolean {
  if (decimalDisplayLiteralNumber(text) !== undefined) return false;
  if (zeroDigitTailDisplayProgram(text) !== undefined) return false;
  if (signDigitLiteralDisplayProgram(text) !== undefined) return false;
  const direct = displayLiteralProgram(text);
  if (direct !== undefined && direct.kind !== "error") return shouldUseFirstSpliceDisplayLiteral(text);
  return shouldUseFirstSpliceDisplayLiteral(text) ||
    signedFirstSpliceDisplayLiteralProgram(text) !== undefined ||
    exponentTailDisplayLiteralProgram(text) !== undefined;
}

function displayLiteralPointExponent(text: string): number | undefined {
  const normalized = normalizeDisplayLiteralText(text);
  const point = /[.,]/u.exec(normalized);
  if (point === null) return undefined;
  const prefix = normalized.slice(0, point.index);
  const cells = displayLiteralCells(prefix);
  if (cells === undefined || cells.length === 0) return undefined;
  return cells.length - 1;
}

function displayLiteralTrailingZeroExponent(text: string): number | undefined {
  const cells = displayLiteralCells(text);
  if (cells === undefined || cells.length === 0 || cells.length > 8) return undefined;
  return cells.at(-1) === 0 ? cells.length - 1 : undefined;
}

function decimalDisplayLiteralNumber(text: string): string | undefined {
  const normalized = normalizeDisplayLiteralText(text);
  if (!/^-?(?:0|[1-9][0-9]{0,7})$/u.test(normalized)) return undefined;
  return normalized;
}

function decimalDisplayFieldLiteral(
  text: string,
  leadingField: boolean,
): { digits: string; value: string; width: number } | undefined {
  const normalized = normalizeDisplayLiteralText(text.trim());
  if (!/^[0-9]+$/u.test(normalized)) return undefined;
  if (leadingField && normalized.startsWith("0")) return undefined;
  const value = normalized.replace(/^0+/u, "") || "0";
  return { digits: normalized, value, width: normalized.length };
}

function displayLiteralMantissaCells(text: string): number[] | undefined {
  const normalized = normalizeDisplayLiteralText(text);
  if (/[.,]/u.test(normalized)) return undefined;
  return displayLiteralCells(normalized);
}

function displayCellsLiteral(cells: readonly number[]): string {
  return cells.map((cell) => {
    if (cell >= 0 && cell <= 9) return String(cell);
    switch (cell) {
      case 10:
        return "-";
      case 11:
        return "L";
      case 12:
        return "С";
      case 13:
        return "Г";
      case 14:
        return "Е";
      case 15:
        return "_";
      default:
        return "";
    }
  }).join("");
}

function zeroDigitTailDisplayProgram(text: string): { input: number } | undefined {
  const cells = displayLiteralCells(text);
  if (cells === undefined || cells.length !== 2) return undefined;
  const [signDigit, tail] = cells;
  if (signDigit === undefined || signDigit < 2 || signDigit > 9 || tail !== 14) return undefined;
  return { input: signDigit - 1 };
}

function signDigitLiteralDisplayProgram(text: string): SignDigitLiteralDisplayProgram | undefined {
  const cells = displayLiteralCells(text);
  if (cells === undefined || cells.length !== 9) return undefined;
  const signDigit = cells[0];
  if (signDigit === undefined || signDigit < 2 || signDigit > 9) return undefined;

  const body = cells.slice(1);
  const first = body[0];
  if (first === undefined || !((first >= 0 && first <= 9) || first === 14)) return undefined;
  const lower = body.slice(1);
  if (lower.length !== 7 || !lower.every((cell) => cell >= 0 && cell <= 9)) return undefined;

  const indirectSteps = signDigit - 1;
  const targetLower = Number.parseInt(lower.join(""), 10);
  const startLower = targetLower - indirectSteps;
  if (!Number.isSafeInteger(startLower) || startLower < 0 || startLower > 9999999) return undefined;
  return {
    signDigit,
    first: first === 14 ? "Е" : String(first),
    start: `1${String(startLower).padStart(7, "0")}`,
    indirectSteps,
  };
}

function isErrorLiteralCells(cells: readonly number[]): boolean {
  return cells.length === 5 &&
    cells[0] === 14 &&
    cells[1] === 13 &&
    cells[2] === 13 &&
    cells[3] === 0 &&
    cells[4] === 13;
}

function displayLiteralInversionDigits(cells: readonly number[]): string | undefined {
  if (cells.length === 0 || cells[0] !== 8) return undefined;
  const digits = ["1"];
  for (const cell of cells.slice(1)) {
    if (cell < 6 || cell > 15) return undefined;
    digits.push(String(15 - cell));
  }
  return digits.join("");
}

function displayLiteralCells(text: string): number[] | undefined {
  const cells: number[] = [];
  const normalized = normalizeDisplayLiteralText(text);
  for (const char of normalized) {
    if (char === "." || char === ",") continue;
    if (/\s/u.test(char) || char === "_") {
      cells.push(15);
      continue;
    }
    if (/[0-9]/u.test(char)) {
      cells.push(Number(char));
      continue;
    }
    const symbol = DISPLAY_LITERAL_SYMBOLS[char];
    if (symbol === undefined) return undefined;
    cells.push(symbol);
  }
  return cells;
}

function normalizeDisplayLiteralText(text: string): string {
  return text
    .replace(/[–—]/gu, "-")
    .replace(/[ОO]/gu, "0")
    .replace(/[Л]/gu, "L")
    .replace(/[ВB]/gu, "L")
    .replace(/[C]/gu, "С")
    .replace(/[D]/gu, "Г")
    .replace(/[G]/gu, "Г")
    .replace(/[E]/gu, "Е");
}

const DISPLAY_LITERAL_SYMBOLS: Record<string, number> = {
  "-": 10,
  L: 11,
  "С": 12,
  "Г": 13,
  "Е": 14,
};

function decimalXorPair(value: number): readonly [number, number] | undefined {
  for (let left = 0; left <= 9; left += 1) {
    for (let right = 0; right <= 9; right += 1) {
      if ((left ^ right) === value) return [left, right];
    }
  }
  return undefined;
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
  return Number.isFinite(value) ? String(value) : raw.trim();
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
  const ephemeralInputs = collectEphemeralInputTargets(ast);
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "assign" || statement.kind === "ask" || statement.kind === "input") {
        if (statement.kind === "input" && ephemeralInputs.has(statement.target)) continue;
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
  const ephemeralInputs = collectEphemeralInputTargets(ast);
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "assign" || statement.kind === "ask") {
        variables.add(statement.target);
      }
      if (statement.kind === "input" && !ephemeralInputs.has(statement.target)) variables.add(statement.target);
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "while") visit(statement.body);
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

function collectEphemeralInputTargets(ast: ProgramAst): Set<string> {
  const readCounts = collectVariableReadCounts(ast);
  const targets = new Set<string>();
  const visit = (statements: StatementAst[]): void => {
    for (let index = 0; index < statements.length; index += 1) {
      const statement = statements[index]!;
      const next = statements[index + 1];
      const afterNext = statements[index + 2];
      if (statement.kind === "input" && next?.kind === "if") {
        const reads = countIdentifierReadsInCondition(next.condition, statement.target);
        if (reads > 0 && (readCounts.get(statement.target) ?? 0) === reads) targets.add(statement.target);
      }
      if (statement.kind === "show" && next?.kind === "input" && afterNext?.kind === "if") {
        const reads = countIdentifierReadsInCondition(afterNext.condition, next.target);
        if (reads > 0 && (readCounts.get(next.target) ?? 0) === reads) targets.add(next.target);
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

function collectDispatchScratchVariables(
  ast: ProgramAst,
  variables: Set<string>,
  freeResidualDispatchScratch = false,
): void {
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "dispatch") {
        const residualNeedsNoScratch = freeResidualDispatchScratch && dispatchUsesNumericResidualChain(statement);
        if (statement.expr.kind !== "identifier" && !residualNeedsNoScratch) {
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
      if (statement.kind === "assign" && matchSingleBitMaskOpAssignment(statement) !== undefined) {
        variables.add(bitMaskScratchName(statement));
      }
      if (statement.kind === "if") {
        const runScratch = membershipSetRunScratchName(statement);
        if (runScratch !== undefined) variables.add(runScratch);
        else if (isReusableMembershipSet(statement)) variables.add(bitMaskScratchName(statement));
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

function collectCoordListScratchVariables(ast: ProgramAst, variables: Set<string>): void {
  const addHasScratch = (): void => {
    variables.add(COORD_LIST_POINTER);
    variables.add(COORD_LIST_COUNTER);
  };
  const addLineCountScratch = (): void => {
    addHasScratch();
    variables.add(COORD_LIST_CURRENT);
    variables.add(COORD_LIST_DX);
  };
  for (const state of ast.states) {
    for (const field of state.fields) {
      if (field.initial !== undefined && randomCoordListItemPlacement(field.name, field.initial) !== undefined) {
        addLineCountScratch();
      }
    }
  }
  const visitExpr = (expr: ExpressionAst): void => {
    if (expr.kind === "call" && expr.callee.toLowerCase() === "coord_list_line_count") {
      addLineCountScratch();
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
  const visitCondition = (condition: ConditionAst): void => {
    const call = coordListHasConditionCall(condition);
    if (call !== undefined) addHasScratch();
    visitExpr(condition.left);
    visitExpr(condition.right);
  };
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "pause" || statement.kind === "halt") visitExpr(statement.expr);
      if (statement.kind === "ask" && statement.prompt) visitExpr(statement.prompt);
      if (statement.kind === "if") {
        visitCondition(statement.condition);
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "switch") {
        visitExpr(statement.expr);
        for (const switchCase of statement.cases) visit(switchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
      if (statement.kind === "dispatch") {
        visitExpr(statement.expr);
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
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

interface BitHasConditionStats {
  count: number;
  inlineCost: number;
  helperCallCost: number;
}

function collectBitHasConditionStats(ast: ProgramAst): Map<string, BitHasConditionStats> {
  const stats = new Map<string, BitHasConditionStats>();
  const visitExpr = (expr: ExpressionAst): void => {
    if (expr.kind === "unary") visitExpr(expr.expr);
    if (expr.kind === "binary") {
      visitExpr(expr.left);
      visitExpr(expr.right);
    }
    if (expr.kind === "call") {
      for (const arg of expr.args) visitExpr(arg);
    }
  };
  const visitBitHasCondition = (expr: ExpressionAst): void => {
    if (expr.kind !== "call" || expr.callee.toLowerCase() !== "bit_has" || expr.args.length !== 2) {
      visitExpr(expr);
      return;
    }
    const [mask, index] = expr.args;
    if (mask?.kind !== "identifier" || index === undefined) return;
    const current = stats.get(mask.name) ?? { count: 0, inlineCost: 0, helperCallCost: 0 };
    current.count += 1;
    current.inlineCost += estimateExpressionCostForCondition(expr, undefined);
    current.helperCallCost += estimateExpressionCost(index) + 2;
    stats.set(mask.name, current);
  };
  const visitCondition = (condition: ConditionAst): void => {
    if ((condition.op === "==" || condition.op === "!=") && isZeroExpression(condition.right)) {
      visitBitHasCondition(condition.left);
      return;
    }
    if ((condition.op === "==" || condition.op === "!=") && isZeroExpression(condition.left)) {
      visitBitHasCondition(condition.right);
      return;
    }
    visitExpr(condition.left);
    visitExpr(condition.right);
  };
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "pause" || statement.kind === "halt") visitExpr(statement.expr);
      if (statement.kind === "ask" && statement.prompt) visitExpr(statement.prompt);
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        if (!membershipConditionHandledBeforeGenericBitHas(statement)) {
          visitCondition(statement.condition);
        }
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
  return stats;
}

function bitHasConditionHelperSaves(mask: string, stats: BitHasConditionStats): boolean {
  if (stats.count < 2) return false;
  const scratch: ExpressionAst = { kind: "identifier", name: spatialHitScratchName(mask) };
  const helperBodyCost = 1 + 1 + estimateExpressionCost(bitMaskExpression(scratch)) + 1 + 1 + 1;
  return stats.inlineCost > helperBodyCost + stats.helperCallCost;
}

function membershipConditionHandledBeforeGenericBitHas(statement: Extract<StatementAst, { kind: "if" }>): boolean {
  return isReusableMembershipClear(statement) ||
    isReusableMembershipSet(statement) ||
    isReusableMembershipSetRun(statement);
}

function collectSpatialHitScratchVariables(
  ast: ProgramAst,
  variables: Set<string>,
  sharedBitMaskHelperCalls = false,
): void {
  if (sharedBitMaskHelperCalls) variables.add(SHARED_BIT_MASK_SCRATCH);
  for (const [mask, stats] of collectBitHasConditionStats(ast)) {
    if (bitHasConditionHelperSaves(mask, stats)) variables.add(spatialHitScratchName(mask));
  }
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
  let needsLineCountScratch = false;
  let needsNeighborTotalScratch = false;
  const visitExpr = (expr: ExpressionAst): void => {
    if (expr.kind === "call") {
      const callee = expr.callee.toLowerCase();
      if (callee === "line_count") {
        needsLineCountScratch = true;
      } else if (callee === "neighbor_count" && expr.args[0]?.kind === "identifier") {
        needsNeighborTotalScratch = true;
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
  if (!needsLineCountScratch && !needsNeighborTotalScratch) return;
  if (needsLineCountScratch) {
    for (const scratch of spatialCountScratchNames()) variables.add(scratch);
  } else {
    variables.add(spatialCountScratchNames()[0]!);
  }
  if (countCalls(ast, "line_count") > 1) variables.add(spatialCountMaskScratchName());
  if (programNeedsSpatialLineProgressionHelper(ast) && variables.size < REGISTER_ORDER.length) {
    variables.add(spatialCountStepScratchName());
  }
}

function programNeedsSpatialLineProgressionHelper(ast: ProgramAst): boolean {
  let needed = false;
  const visitExpr = (expr: ExpressionAst): void => {
    if (needed) return;
    if (expr.kind === "call" && expr.callee.toLowerCase() === "line_count" && expr.args[0] !== undefined) {
      const board = boardForCellMask(expr.args[0], ast);
      if (board !== undefined && board.width <= 4 && board.height <= 4) {
        needed = true;
        return;
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
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (needed) return;
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
  return needed;
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

function collectDisplayTemplateScratchVariables(ast: ProgramAst, variables: Set<string>): void {
  for (const display of ast.displays) {
    if (displayHasMantissaExponentTemplateShape(display)) {
      variables.add(displayTemplateValueScratchName(display));
      variables.add(displayTemplateLoopScratchName(display));
      variables.add(displayTemplateMaskScratchName(display));
      continue;
    }
    if (displayMantissaMaskTextForAst(ast, display) !== undefined) {
      variables.add(displayTemplateValueScratchName(display));
    }
  }
  for (const display of ast.displays) {
    const literal = literalOnlyDisplayText(display);
    if (literal === undefined || !shouldUsePreloadedDisplayLiteral(literal)) continue;
    variables.add(firstSpliceDisplayScratchName(display));
  }
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

// True when the dispatch will be lowered through the numeric residual compare
// chain, which evaluates the selector once and keeps the running residual in X
// across cases (mismatched cases skip their body, so X survives). That lowering
// needs NO scratch register even for a non-identifier selector, so the allocator
// must not reserve one for it. Mirrors compileNumericResidualDispatchCompareChain.
function dispatchUsesNumericResidualChain(statement: Extract<StatementAst, { kind: "dispatch" }>): boolean {
  if (statement.cases.length < 2) return false;
  const values = statement.cases.map((dispatchCase) => numericLiteralValue(dispatchCase.value));
  if (values.some((value) => value === undefined)) return false;
  return numericResidualDispatchIsCheaper(statement, values as number[]);
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

function matchResidualGuardedUpdate(
  statement: Extract<StatementAst, { kind: "if" }>,
): {
  condition: ConditionAst;
  assignment: Extract<StatementAst, { kind: "assign" }>;
  tail: StatementAst[];
  target: string;
  bound: number;
  delta: number;
} | undefined {
  const first = statement.thenBody[0];
  if (first?.kind !== "assign") return undefined;
  const condition = statement.condition;
  if (condition.op !== "<") return undefined;
  if (condition.left.kind !== "identifier") return undefined;
  const bound = numericLiteralValue(condition.right);
  if (bound === undefined) return undefined;

  const delta = matchNumericSelfUpdate(condition.left.name, first.expr);
  if (delta === undefined || delta === 0) return undefined;
  if (first.target !== condition.left.name) return undefined;

  return {
    condition,
    assignment: first,
    tail: statement.thenBody.slice(1),
    target: condition.left.name,
    bound,
    delta,
  };
}

function matchNumericSelfUpdate(target: string, expr: ExpressionAst): number | undefined {
  if (expr.kind !== "binary") return undefined;
  if (expr.op === "+") {
    if (expr.left.kind === "identifier" && expr.left.name === target) return numericLiteralValue(expr.right);
    if (expr.right.kind === "identifier" && expr.right.name === target) return numericLiteralValue(expr.left);
    return undefined;
  }
  if (
    expr.op === "-" &&
    expr.left.kind === "identifier" &&
    expr.left.name === target
  ) {
    const value = numericLiteralValue(expr.right);
    return value === undefined ? undefined : -value;
  }
  return undefined;
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

function estimateSmallSetConditionCost(
  match: SmallSetConditionMatch,
  preloadedConstants: ReadonlySet<string>,
): number {
  return match.tests.reduce(
    (sum, test) => sum + estimateExpressionCostForCondition(test.expr, preloadedConstants) + 2,
    0,
  );
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
      return bitMembershipExpression(args[0]!, args[1]!);
    case "bit_set":
      return bitOrExpression(args[0]!, bitMaskExpression(args[1]!));
    case "bit_clear":
      return bitAndExpression(args[0]!, bitNotExpression(bitMaskExpression(args[1]!)));
    case "bit_toggle":
      return bitXorExpression(args[0]!, bitMaskExpression(args[1]!));
    case "diag_left_index":
      return positiveNorm4Expression(addExpressions(args[0]!, args[1]!));
    case "diag_right_index":
      return positiveNorm4Expression(addExpressions(subtractExpressions(args[0]!, args[1]!), numberExpression(4)));
    case "cell_mask":
      return cellMaskExpression(args[0]!, args[1]!);
    case "cell_has":
    case "cell_used":
      return {
        kind: "call",
        callee: "sign",
        args: [{
          kind: "call",
          callee: "frac",
          args: [bitAndExpression(args[0]!, cellMaskExpression(args[1]!, args[2]!))],
        }],
      };
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

type SmallSetMacroName = "near_any" | "eq_any";

interface SmallSetTest {
  expr: ExpressionAst;
  trueOpcode: number;
  falseOpcode: number;
}

interface SmallSetConditionMatch {
  kind: SmallSetMacroName;
  mode: "any" | "all";
  tests: SmallSetTest[];
}

interface NearAnyHelperConditionMatch {
  value: ExpressionAst;
  radius: ExpressionAst;
  candidates: ExpressionAst[];
  op: ">=" | "<";
}

interface NearAnyHelperStats {
  candidateCount: number;
  conditionCount: number;
  ordinaryCost: number;
  helperCallCost: number;
  helperCost: number;
}

function isSmallSetMacroName(name: string): name is SmallSetMacroName {
  return name === "near_any" || name === "eq_any";
}

function smallSetMacroArityOk(name: SmallSetMacroName, argCount: number): boolean {
  return name === "near_any" ? argCount >= 3 : argCount >= 2;
}

function smallSetMacroArityText(name: SmallSetMacroName): string {
  return name === "near_any" ? "at least three arguments" : "at least two arguments";
}

function smallSetExpressionMacro(name: string, args: ExpressionAst[]): ExpressionAst | undefined {
  if (!isSmallSetMacroName(name) || !smallSetMacroArityOk(name, args.length)) return undefined;
  if (name === "near_any") {
    const value = args[0]!;
    const radius = args[1]!;
    const distances = args.slice(2).map((candidate) => absExpression(subtractExpressions(value, candidate)));
    if (distances.length === 1) return subtractExpressions(radius, distances[0]!);
    return addExpressions(radius, maxExpressions(distances.map(negateExpression)));
  }
  const value = args[0]!;
  const differences = args.slice(1).map((candidate) => subtractExpressions(value, candidate));
  return productExpressions(differences);
}

function matchSmallSetCondition(condition: ConditionAst): SmallSetConditionMatch | undefined {
  const normalized = normalizeZeroComparison(condition);
  if (normalized === undefined) return undefined;

  const near = matchNearAnyExpression(normalized.expr);
  if (near !== undefined && (normalized.op === ">=" || normalized.op === "<")) {
    return {
      kind: "near_any",
      mode: normalized.op === ">=" ? "any" : "all",
      tests: near.map((expr) => ({
        expr,
        trueOpcode: directTestOpcode("<"),
        falseOpcode: directTestOpcode(">="),
      })),
    };
  }

  const equal = matchEqAnyExpression(normalized.expr);
  if (equal !== undefined && (normalized.op === "==" || normalized.op === "!=")) {
    return {
      kind: "eq_any",
      mode: normalized.op === "==" ? "any" : "all",
      tests: equal.map((expr) => ({
        expr,
        trueOpcode: directTestOpcode("!="),
        falseOpcode: directTestOpcode("=="),
      })),
    };
  }

  return undefined;
}

function matchNearAnyHelperCondition(condition: ConditionAst): NearAnyHelperConditionMatch | undefined {
  const normalized = normalizeZeroComparison(condition);
  if (normalized === undefined || (normalized.op !== ">=" && normalized.op !== "<")) return undefined;
  const match = matchNearAnySetExpression(normalized.expr);
  if (match === undefined) return undefined;
  if (!isSimpleStackLoad(match.value) || !isSimpleStackLoad(match.radius)) return undefined;
  if (match.candidates.length === 0 || match.candidates.some((candidate) => !isSimpleStackLoad(candidate))) {
    return undefined;
  }
  return { ...match, op: normalized.op };
}

function matchNearAnySetExpression(
  expr: ExpressionAst,
): { value: ExpressionAst; radius: ExpressionAst; candidates: ExpressionAst[] } | undefined {
  if (expr.kind === "call" && expr.callee.toLowerCase() === "near_any" && smallSetMacroArityOk("near_any", expr.args.length)) {
    return {
      value: expr.args[0]!,
      radius: expr.args[1]!,
      candidates: expr.args.slice(2),
    };
  }
  return matchNearAnyMarginSetExpression(expr);
}

function matchNearAnyMarginSetExpression(
  expr: ExpressionAst,
): { value: ExpressionAst; radius: ExpressionAst; candidates: ExpressionAst[] } | undefined {
  const terms = flattenMaxTerms(expr);
  const margins = terms.map(matchNearMarginTerm);
  if (margins.some((margin) => margin === undefined)) return undefined;
  const typedMargins = margins as NearMarginTerm[];
  if (typedMargins.length === 0) return undefined;
  const commonValue = commonDifferenceEndpoint(typedMargins.map((margin) => margin.difference));
  if (commonValue === undefined) return undefined;
  const radius = typedMargins[0]!.radius;
  if (!typedMargins.every((margin) => expressionEquals(margin.radius, radius))) return undefined;
  const candidates = typedMargins.map((margin) =>
    expressionEquals(margin.difference.left, commonValue) ? margin.difference.right : margin.difference.left
  );
  return { value: commonValue, radius, candidates };
}

function nearAnyHelperKey(value: ExpressionAst, radius: ExpressionAst): string {
  return `${expressionToIntentText(value)}|${expressionToIntentText(radius)}`;
}

function normalizeZeroComparison(condition: ConditionAst): { expr: ExpressionAst; op: ConditionAst["op"] } | undefined {
  if (isZeroExpression(condition.right)) return { expr: condition.left, op: condition.op };
  if (isZeroExpression(condition.left)) return { expr: condition.right, op: flipComparisonOp(condition.op) };
  return undefined;
}

function matchNearAnyExpression(expr: ExpressionAst): ExpressionAst[] | undefined {
  if (expr.kind === "call" && expr.callee.toLowerCase() === "near_any") {
    const macro = smallSetExpressionMacro("near_any", expr.args);
    return macro === undefined ? undefined : matchNearAnyExpression(macro);
  }

  const terms = flattenMaxTerms(expr);
  const margins = terms.map(matchNearMarginTerm);
  if (margins.some((margin) => margin === undefined)) return undefined;
  const typedMargins = margins as NearMarginTerm[];
  const commonValue = commonDifferenceEndpoint(typedMargins.map((margin) => margin.difference));
  if (commonValue === undefined) return undefined;
  const radius = typedMargins[0]!.radius;
  if (!typedMargins.every((margin) => expressionEquals(margin.radius, radius))) return undefined;
  return typedMargins.map((margin) => margin.expr);
}

interface NearMarginTerm {
  expr: ExpressionAst;
  radius: ExpressionAst;
  difference: Extract<ExpressionAst, { kind: "binary" }>;
}

function matchNearMarginTerm(expr: ExpressionAst): NearMarginTerm | undefined {
  if (expr.kind !== "binary" || expr.op !== "-") return undefined;
  const absCall = expr.right;
  if (absCall.kind !== "call" || absCall.callee.toLowerCase() !== "abs" || absCall.args.length !== 1) {
    return undefined;
  }
  const difference = absCall.args[0]!;
  if (difference.kind !== "binary" || difference.op !== "-") return undefined;
  return { expr, radius: expr.left, difference };
}

function matchEqAnyExpression(expr: ExpressionAst): ExpressionAst[] | undefined {
  if (expr.kind === "call" && expr.callee.toLowerCase() === "eq_any") {
    const macro = smallSetExpressionMacro("eq_any", expr.args);
    return macro === undefined ? undefined : matchEqAnyExpression(macro);
  }

  const factors = flattenProductTerms(expr);
  if (factors.length === 0) return undefined;
  const differences = factors.map((factor) =>
    factor.kind === "binary" && factor.op === "-" ? factor : undefined
  );
  if (differences.some((difference) => difference === undefined)) return undefined;
  const typedDifferences = differences as Array<Extract<ExpressionAst, { kind: "binary" }>>;
  if (commonDifferenceEndpoint(typedDifferences) === undefined) return undefined;
  return factors;
}

function commonDifferenceEndpoint(
  differences: Array<Extract<ExpressionAst, { kind: "binary" }>>,
): ExpressionAst | undefined {
  if (differences.length === 0) return undefined;
  const candidates = [differences[0]!.left, differences[0]!.right];
  return candidates.find((candidate) =>
    differences.every((difference) =>
      expressionEquals(difference.left, candidate) || expressionEquals(difference.right, candidate)
    )
  );
}

function flattenMaxTerms(expr: ExpressionAst): ExpressionAst[] {
  if (expr.kind === "call" && expr.callee.toLowerCase() === "max" && expr.args.length === 2) {
    return [...flattenMaxTerms(expr.args[0]!), ...flattenMaxTerms(expr.args[1]!)];
  }
  return [expr];
}

function flattenProductTerms(expr: ExpressionAst): ExpressionAst[] {
  if (expr.kind === "binary" && expr.op === "*") {
    return [...flattenProductTerms(expr.left), ...flattenProductTerms(expr.right)];
  }
  return [expr];
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
  mask: ExpressionAst;
  mode: "index" | "mask";
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

function matchAnyBitSetAssignment(
  statement: Extract<StatementAst, { kind: "assign" }>,
  membership: BitMembershipCondition,
): BitSetAssignment | undefined {
  const expr = statement.expr;
  if (expr.kind !== "call" || expr.args.length !== 2) return undefined;
  const collection = expr.args[0]!;
  if (collection.kind !== "identifier" || statement.target !== collection.name) return undefined;
  const name = expr.callee.toLowerCase();
  if (name === "bit_set") {
    if (membership.mode !== "index" || !expressionEquals(expr.args[1]!, membership.item)) return undefined;
  } else if (name === "bit_or") {
    if (!expressionEquals(expr.args[1]!, membership.mask)) return undefined;
  } else {
    return undefined;
  }
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

function isReusableMembershipSetRun(statement: Extract<StatementAst, { kind: "if" }>): boolean {
  return membershipSetRunScratchName(statement) !== undefined;
}

function isReusableMembershipClear(statement: Extract<StatementAst, { kind: "if" }>): boolean {
  const present = matchBitMembershipCondition(statement.condition);
  if (present === undefined) return false;
  const first = statement.thenBody[0];
  return first?.kind === "assign" && isBitClearAssignment(first, present);
}

function membershipSetRunScratchName(statement: Extract<StatementAst, { kind: "if" }>): string | undefined {
  const present = matchBitMembershipCondition(statement.condition);
  if (present !== undefined && statement.elseBody !== undefined) {
    const first = statement.elseBody[0];
    if (first?.kind !== "assign") return undefined;
    let count = matchAnyBitSetAssignment(first, present) === undefined ? 0 : 1;
    if (count === 0) return undefined;
    for (const inner of statement.elseBody) {
      if (inner === first) continue;
      if (inner.kind !== "assign" || matchAnyBitSetAssignment(inner, present) === undefined) break;
      count += 1;
    }
    if (count >= 2) return bitMaskScratchName(first);
  }

  const absent = matchBitAbsenceCondition(statement.condition);
  if (absent === undefined) return undefined;
  const first = statement.thenBody[0];
  if (first?.kind !== "assign") return undefined;
  let count = matchAnyBitSetAssignment(first, absent) === undefined ? 0 : 1;
  if (count === 0) return undefined;
  for (const inner of statement.thenBody) {
    if (inner === first) continue;
    if (inner.kind !== "assign" || matchAnyBitSetAssignment(inner, absent) === undefined) break;
    count += 1;
  }
  return count >= 2 ? bitMaskScratchName(first) : undefined;
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
  if (test.kind !== "call" || test.args.length !== 2) return undefined;
  if (test.callee.toLowerCase() === "bit_and") {
    return {
      collection: test.args[0]!,
      item: test.args[1]!,
      mask: test.args[1]!,
      mode: "mask",
      test,
    };
  }
  if (test.callee.toLowerCase() !== "bit_has") return undefined;
  const item = test.args[1]!;
  return {
    collection: test.args[0]!,
    item,
    mask: bitMaskExpression(item),
    mode: "index",
    test,
  };
}

function matchBitAbsenceCondition(condition: ConditionAst): BitMembershipCondition | undefined {
  if (condition.op !== "==" || !isZeroExpression(condition.right)) return undefined;
  const test = condition.left;
  if (test.kind !== "call" || test.args.length !== 2) return undefined;
  if (test.callee.toLowerCase() === "bit_and") {
    return {
      collection: test.args[0]!,
      item: test.args[1]!,
      mask: test.args[1]!,
      mode: "mask",
      test,
    };
  }
  if (test.callee.toLowerCase() !== "bit_has") return undefined;
  const item = test.args[1]!;
  return {
    collection: test.args[0]!,
    item,
    mask: bitMaskExpression(item),
    mode: "index",
    test,
  };
}

function isBitClearAssignment(
  statement: Extract<StatementAst, { kind: "assign" }>,
  membership: BitMembershipCondition,
): boolean {
  if (membership.collection.kind !== "identifier" || statement.target !== membership.collection.name) return false;
  const expr = statement.expr;
  if (expr.kind !== "call" || expr.args.length !== 2 || !expressionEquals(expr.args[0]!, membership.collection)) {
    return false;
  }
  const name = expr.callee.toLowerCase();
  if (name === "bit_clear") return membership.mode === "index" && expressionEquals(expr.args[1]!, membership.item);
  return name === "bit_and" && isBitNotOf(expr.args[1]!, membership.mask);
}

function isBitSetAssignment(
  statement: Extract<StatementAst, { kind: "assign" }>,
  membership: BitMembershipCondition,
): boolean {
  return matchAnyBitSetAssignment(statement, membership) !== undefined;
}

function isBitNotOf(expr: ExpressionAst, inner: ExpressionAst): boolean {
  return expr.kind === "call" &&
    expr.callee.toLowerCase() === "bit_not" &&
    expr.args.length === 1 &&
    expressionEquals(expr.args[0]!, inner);
}

interface SingleBitMaskOpAssignment {
  opcode: number;
  mnemonic: string;
  collection: ExpressionAst;
  index: ExpressionAst;
  negate: boolean;
}

// Recognize a standalone `target = bit_or/bit_and/bit_xor(collection,
// [bit_not] bit_mask(index))` assignment. These come from `cells += item` /
// `cells -= item` on a single-row board. The generic expression compiler builds
// `bit_mask(index)` inline while `collection` sits on the stack; the cell-mask
// construction (frac/x^y/10^x) overflows the four-deep MK-61 stack and corrupts
// the held accumulator. The dedicated lowering builds the mask into a scratch
// register first (anchor added last, max depth three), so nothing is held while
// the mask is constructed.
function matchSingleBitMaskOpAssignment(
  statement: Extract<StatementAst, { kind: "assign" }>,
): SingleBitMaskOpAssignment | undefined {
  const expr = statement.expr;
  if (expr.kind !== "call" || expr.args.length !== 2) return undefined;
  const name = expr.callee.toLowerCase();

  // `cells += item` / `cells -= item` on a single-row board lower directly to
  // bit_set/bit_clear/bit_toggle(collection, index).
  const indexOps: Record<string, [number, string, boolean]> = {
    bit_set: [0x38, "К ∨", false],
    bit_clear: [0x37, "К ∧", true],
    bit_toggle: [0x39, "К ⊕", false],
  };
  const indexOp = indexOps[name];
  if (indexOp !== undefined) {
    return {
      opcode: indexOp[0],
      mnemonic: indexOp[1],
      collection: expr.args[0]!,
      index: expr.args[1]!,
      negate: indexOp[2],
    };
  }

  // Two-dimensional boards (and explicit bit ops) arrive pre-expanded as
  // bit_or/bit_and/bit_xor(collection, [bit_not] bit_mask(index)).
  const maskOps: Record<string, [number, string]> = {
    bit_or: [0x38, "К ∨"],
    bit_and: [0x37, "К ∧"],
    bit_xor: [0x39, "К ⊕"],
  };
  const op = maskOps[name];
  if (op === undefined) return undefined;

  let maskArg = expr.args[1]!;
  let negate = false;
  if (maskArg.kind === "call" && maskArg.callee.toLowerCase() === "bit_not" && maskArg.args.length === 1) {
    negate = true;
    maskArg = maskArg.args[0]!;
  }
  if (maskArg.kind !== "call" || maskArg.callee.toLowerCase() !== "bit_mask" || maskArg.args.length !== 1) {
    return undefined;
  }
  return {
    opcode: op[0],
    mnemonic: op[1],
    collection: expr.args[0]!,
    index: maskArg.args[0]!,
    negate,
  };
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

function positiveNorm4Expression(expr: ExpressionAst): ExpressionAst {
  const rem = multiplyExpressions(
    fracExpression(divideExpressions(intExpression(expr), numberExpression(4))),
    numberExpression(4),
  );
  return addExpressions(
    rem,
    multiplyExpressions(numberExpression(4), oneMinus(signExpression(rem))),
  );
}

function cellMaskExpression(x: ExpressionAst, y: ExpressionAst): ExpressionAst {
  return addExpressions(
    pow10Expression(x),
    { kind: "call", callee: "int", args: [multiplyExpressions(pow10Expression(y), numberExpression(0.22600029))] },
  );
}

interface RandomCoordListPlacement {
  listName: string;
  xMin: number;
  width: number;
  yMin: number;
  height: number;
  count: number;
}

function isZeroOriginTenByTenPlacement(placement: RandomCoordListPlacement): boolean {
  return placement.xMin === 0 && placement.yMin === 0 && placement.width === 10 && placement.height === 10;
}

function randomCoordListItemPlacement(fieldName: string, expr: ExpressionAst): RandomCoordListPlacement | undefined {
  const item = coordListItemInfo(fieldName);
  if (item === undefined) return undefined;
  if (expr.kind !== "call" || expr.callee !== "__random_coord_list_item" || expr.args.length !== 6) return undefined;
  const xMin = numericLiteralValue(expr.args[0]!);
  const width = numericLiteralValue(expr.args[1]!);
  const yMin = numericLiteralValue(expr.args[2]!);
  const height = numericLiteralValue(expr.args[3]!);
  const count = numericLiteralValue(expr.args[4]!);
  const index = numericLiteralValue(expr.args[5]!);
  if (
    xMin === undefined ||
    width === undefined ||
    yMin === undefined ||
    height === undefined ||
    count === undefined ||
    index === undefined ||
    index !== item.index
  ) return undefined;
  return { listName: item.listName, xMin, width, yMin, height, count };
}

function randomCoordListSetupFields(
  fields: readonly StateFieldAst[],
  placement: RandomCoordListPlacement,
): StateFieldAst[] {
  return fields
    .filter((field) => {
      const item = coordListItemInfo(field.name);
      if (item === undefined || item.listName !== placement.listName) return false;
      const current = field.initial === undefined ? undefined : randomCoordListItemPlacement(field.name, field.initial);
      return current !== undefined &&
        current.xMin === placement.xMin &&
        current.width === placement.width &&
        current.yMin === placement.yMin &&
        current.height === placement.height &&
        current.count === placement.count;
    })
    .sort((left, right) => coordListItemInfo(left.name)!.index - coordListItemInfo(right.name)!.index);
}

function randomCoordListCellExpression(placement: RandomCoordListPlacement): ExpressionAst {
  const x = addExpressions(
    numberExpression(placement.xMin),
    intExpression(multiplyExpressions({ kind: "call", callee: "random", args: [] }, numberExpression(placement.width))),
  );
  const y = addExpressions(
    numberExpression(placement.yMin),
    intExpression(multiplyExpressions({ kind: "call", callee: "random", args: [] }, numberExpression(placement.height))),
  );
  return addExpressions(x, multiplyExpressions(numberExpression(10), y));
}

// A cell mask is stored as `8.HHHHHHH`: the MK-61 blue logical operations
// (К∨/К∧/К⊕) force the integer part to 8 and operate nibble-wise on the seven
// fractional hex digits, so each cell's bit lives in a fixed fractional nibble
// rather than in an integer position (which normalization would collapse).
// Bit `index` (0-based) occupies hex nibble `floor(index/4)+1` after the point,
// with value `2^(index mod 4)` inside that nibble. `2^offset` is computed with
// `F x^y`, which is slightly imprecise (e.g. 2^3 → 7.9999993), so it is rounded
// before being placed, keeping the nibble exact.
function bitMaskExpression(index: ExpressionAst): ExpressionAst {
  const nibble = intExpression(divideExpressions(index, numberExpression(4)));
  const offset = subtractExpressions(index, multiplyExpressions(nibble, numberExpression(4)));
  const bitValue = intExpression(addExpressions(powExpression(numberExpression(2), offset), numberExpression(0.5)));
  return addExpressions(
    numberExpression(8),
    divideExpressions(bitValue, pow10Expression(addExpressions(nibble, numberExpression(1)))),
  );
}

// Membership of a bit reduces to: the fractional part of `mask К∧ bitMask` is
// non-zero exactly when the bit is set (an absent bit yields `8.0`). `sign` of
// that fraction collapses to the 0/1 the language expects from `bit_has`.
function bitMembershipExpression(mask: ExpressionAst, index: ExpressionAst): ExpressionAst {
  return {
    kind: "call",
    callee: "sign",
    args: [{
      kind: "call",
      callee: "frac",
      args: [bitAndExpression(mask, bitMaskExpression(index))],
    }],
  };
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

function productExpressions(expressions: ExpressionAst[]): ExpressionAst {
  return expressions.reduce((product, expr) => multiplyExpressions(product, expr));
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

function spatialCountStepScratchName(): string {
  return `${SPATIAL_COUNT_SCRATCH_PREFIX}step`;
}

function range(start: number, end: number): number[] {
  const values: number[] = [];
  for (let value = start; value <= end; value += 1) values.push(value);
  return values;
}

function literalOnlyDisplayText(display: ProgramAst["displays"][number]): string | undefined {
  if (display.items.length === 0 || display.items.some((item) => item.kind !== "literal")) return undefined;
  const text = display.items.map((item) => item.kind === "literal" ? item.text : "").join("");
  return text.trim().length === 0 ? undefined : text;
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

function expressionReferencesIdentifier(expr: ExpressionAst, name: string): boolean {
  switch (expr.kind) {
    case "number":
      return false;
    case "identifier":
      return expr.name === name;
    case "unary":
      return expressionReferencesIdentifier(expr.expr, name);
    case "binary":
      return expressionReferencesIdentifier(expr.left, name) || expressionReferencesIdentifier(expr.right, name);
    case "call":
      return expr.args.some((arg) => expressionReferencesIdentifier(arg, name));
  }
}

function isRandomCellExpressionShape(expr: ExpressionAst): boolean {
  if (isRandomScaledInteger(expr)) return true;
  if (expr.kind !== "binary") return false;
  if (expr.op === "+") {
    return (isRandomScaledInteger(expr.left) && numericLiteralValue(expr.right) !== undefined) ||
      (numericLiteralValue(expr.left) !== undefined && isRandomScaledInteger(expr.right));
  }
  if (expr.op === "-") {
    return isRandomScaledInteger(expr.left) && numericLiteralValue(expr.right) !== undefined;
  }
  return false;
}

function isRandomScaledInteger(expr: ExpressionAst): boolean {
  if (expr.kind !== "call" || expr.callee.toLowerCase() !== "int" || expr.args.length !== 1) return false;
  const arg = expr.args[0]!;
  if (arg.kind !== "binary" || arg.op !== "*") return false;
  if (isRandomCall(arg.left)) return !expressionContainsRandom(arg.right);
  if (isRandomCall(arg.right)) return !expressionContainsRandom(arg.left);
  return false;
}

function isRandomCall(expr: ExpressionAst): boolean {
  return expr.kind === "call" && expr.callee.toLowerCase() === "random" && expr.args.length === 0;
}

function expressionContainsRandom(expr: ExpressionAst): boolean {
  switch (expr.kind) {
    case "number":
    case "identifier":
      return false;
    case "unary":
      return expressionContainsRandom(expr.expr);
    case "binary":
      return expressionContainsRandom(expr.left) || expressionContainsRandom(expr.right);
    case "call":
      return isRandomCall(expr) || expr.args.some(expressionContainsRandom);
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

function matchStackUnaryDerivationCall(expr: ExpressionAst): StackUnaryDerivationCall | undefined {
  if (expr.kind !== "call" || expr.args.length !== 1) return undefined;
  const name = expr.callee.toLowerCase();
  if (!isStackUnaryDerivationFn(name)) return undefined;
  const [opcode, mnemonic] = STACK_UNARY_DERIVATION_OPCODES[name];
  return { fn: name, arg: expr.args[0]!, opcode, mnemonic };
}

function isStackUnaryDerivationFn(name: string): name is StackUnaryDerivationFn {
  return Object.prototype.hasOwnProperty.call(STACK_UNARY_DERIVATION_OPCODES, name);
}

function optimizeDispatchDefaultCases(
  statement: Extract<StatementAst, { kind: "dispatch" }>,
): { statement: Extract<StatementAst, { kind: "dispatch" }>; removed: number; reordered: number } {
  if (statement.cases.length === 0) return { statement, removed: 0, reordered: 0 };
  if (!statement.defaultBody) {
    const ordered = orderNumericDispatchCasesForResidual(statement.cases);
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

  const ordered = orderNumericDispatchCasesForResidual(kept);
  const nextStatement = removed === 0 && ordered.reordered === 0
    ? statement
    : { ...statement, cases: ordered.cases };
  return { statement: nextStatement, removed, reordered: ordered.reordered };
}

function orderNumericDispatchCasesForResidual(cases: DispatchCaseAst[]): { cases: DispatchCaseAst[]; reordered: number } {
  if (cases.length < 2) return { cases, reordered: 0 };
  const values = cases.map((dispatchCase) => numericLiteralValue(dispatchCase.value));
  if (values.some((value) => value === undefined)) return { cases, reordered: 0 };
  const seen = new Set<number>();
  for (const value of values as number[]) {
    if (seen.has(value)) return { cases, reordered: 0 };
    seen.add(value);
  }
  const residualOrder = bestResidualDispatchOrder(values as number[]);
  const order = orderMatchesIdentity(residualOrder) ? zeroFirstDispatchOrder(values as number[]) : residualOrder;
  const reordered = order.filter((originalIndex, index) => originalIndex !== index).length;
  if (reordered === 0) return { cases, reordered: 0 };
  const ordered = order.map((index) => cases[index]!);
  return { cases: ordered, reordered };
}

function orderMatchesIdentity(order: readonly number[]): boolean {
  return order.every((originalIndex, index) => originalIndex === index);
}

function zeroFirstDispatchOrder(values: readonly number[]): number[] {
  const zeroIndex = values.indexOf(0);
  if (zeroIndex <= 0) return values.map((_, index) => index);
  return [zeroIndex, ...values.map((_, index) => index).filter((index) => index !== zeroIndex)];
}

function bestResidualDispatchOrder(values: readonly number[]): number[] {
  const current = values.map((_, index) => index);
  let best = current;
  const currentCost = residualDispatchValueCost(values, current);
  let bestCost = currentCost;
  if (values.length > 8) return current;

  const used = new Set<number>();
  const order: number[] = [];
  const visit = (previous: number | undefined, cost: number): void => {
    if (cost >= bestCost) return;
    if (order.length === values.length) {
      bestCost = cost;
      best = [...order];
      return;
    }
    for (let index = 0; index < values.length; index += 1) {
      if (used.has(index)) continue;
      used.add(index);
      order.push(index);
      const value = values[index]!;
      visit(value, cost + residualStepCost(previous, value));
      order.pop();
      used.delete(index);
    }
  };
  visit(undefined, 0);
  if (currentCost - bestCost < 3) return current;
  return best;
}

function residualDispatchValueCost(values: readonly number[], order: readonly number[]): number {
  let previous: number | undefined;
  let cost = 0;
  for (const index of order) {
    const value = values[index]!;
    cost += residualStepCost(previous, value);
    previous = value;
  }
  return cost;
}

function residualStepCost(previous: number | undefined, value: number): number {
  const delta = previous === undefined ? value : previous - value;
  return delta === 0 ? 0 : estimateNumberCost(String(delta)) + 1;
}

function statementListsEqual(left: readonly StatementAst[], right: readonly StatementAst[]): boolean {
  return left.length === right.length && left.every((statement, index) => statementEquals(statement, right[index]!));
}

function statementEquals(left: StatementAst, right: StatementAst): boolean {
  if (left.kind !== right.kind) return false;
  switch (left.kind) {
    case "pause":
      return expressionEquals(left.expr, (right as typeof left).expr) &&
        left.kind === (right as typeof left).kind;
    case "halt":
      return expressionEquals(left.expr, (right as typeof left).expr) &&
        left.literal === (right as typeof left).literal;
    case "ask":
      return left.target === (right as typeof left).target &&
        expressionOptionEquals(left.prompt, (right as typeof left).prompt);
    case "input":
      return left.target === (right as typeof left).target;
    case "assign":
      return left.target === (right as typeof left).target && expressionEquals(left.expr, (right as typeof left).expr);
    case "loop":
      return statementListsEqual(left.body, (right as typeof left).body);
    case "while":
      return conditionEquals(left.condition, (right as typeof left).condition) &&
        statementListsEqual(left.body, (right as typeof left).body);
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
    case "decimal_series":
      return left.digits === (right as typeof left).digits &&
        left.counterStart === (right as typeof left).counterStart;
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

function estimateBranchOrderBodyCost(statements: readonly StatementAst[], ast: ProgramAst): number {
  let total = 0;
  for (const statement of statements) {
    const cost = estimateBranchOrderStatementCost(statement, ast);
    if (!Number.isFinite(cost)) return Number.POSITIVE_INFINITY;
    total += cost;
  }
  return total;
}

function estimateBranchOrderStatementCost(statement: StatementAst, ast: ProgramAst): number {
  switch (statement.kind) {
    case "assign":
      return estimateExpressionCost(statement.expr) + 1;
    case "pause":
    case "halt":
    case "trap":
      return estimateExpressionCost(statement.expr) + 1;
    case "ask":
      return (statement.prompt === undefined ? 0 : estimateExpressionCost(statement.prompt)) + 2;
    case "input":
      return 2;
    case "show": {
      const display = ast.displays.find((candidate) => candidate.name === statement.display);
      return display === undefined ? Number.POSITIVE_INFINITY : estimatePackedDisplayBodyCost(display.sources.length);
    }
    case "call":
      return 2;
    case "if": {
      const thenCost = estimateBranchOrderBodyCost(statement.thenBody, ast);
      if (!Number.isFinite(thenCost)) return Number.POSITIVE_INFINITY;
      if (statement.elseBody === undefined) return estimateConditionCost(statement.condition, ast) + thenCost;
      const elseCost = estimateBranchOrderBodyCost(statement.elseBody, ast);
      if (!Number.isFinite(elseCost)) return Number.POSITIVE_INFINITY;
      return estimateConditionCost(statement.condition, ast) + thenCost + 2 + elseCost;
    }
    case "core":
    case "egg":
      return statement.lines.length;
    case "decimal_series":
      return 64;
    case "while":
    case "loop":
    case "switch":
    case "dispatch":
      return Number.POSITIVE_INFINITY;
  }
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
    case "decimal_series":
      return 64;
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
  const smallSetMacro = smallSetExpressionMacro(name, expr.args);
  if (smallSetMacro !== undefined) return estimateExpressionCostForCondition(smallSetMacro, preloadedConstants);
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
  const smallSetMacro = smallSetExpressionMacro(name, expr.args);
  if (smallSetMacro !== undefined) return estimateExpressionCost(smallSetMacro);
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

function residualAdjustmentCost(previousValue: number, nextValue: number): number {
  const delta = previousValue - nextValue;
  if (delta === 0) return 0;
  return estimateNumberCost(String(Math.abs(delta))) + 1;
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
      "tail-branch-inversion",
      "terminal-rule-tail-call",
      "tail-call-layout",
      "terminal-if-direct-branch",
      "terminal-branch-end-elision",
      "local-terminal-tail",
      "local-terminal-tail-branch",
      "late-layout-if-variant",
      "single-use-rule-inline",
      "size-model-rule-inline",
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
    id: "coord-list-scaled-decimal",
    category: "data",
    source: "documented",
    requires: [],
    activeWhen: [
      "coord-list-scaled-decimal-storage",
      "coord-list-scaled-line-count",
      "coord-list-scaled-fused-hit-line-count",
      "coord-list-scaled-membership",
    ],
    detail: "Stores 10x10 board coordinates as y.x decimal values when all visible uses allow it, so row/column/diagonal scans avoid repeated /10 digit extraction.",
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
      "bit-mask-condition-helper",
      "spatial-hit-condition-helper",
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
    activeWhen: ["fl-unit-decrement", "indirect-incdec-counter", "r0-indirect-counter"],
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
    activeWhen: ["error-stop", "screen-error-literal-lowering"],
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
    id: "return-suffix-gadget",
    category: "flow",
    source: "documented",
    requires: [],
    activeWhen: ["return-suffix-gadget"],
    detail: "Shares identical straight-line suffixes ending in В/О, and calls into existing straight-line body plus БП tail-call gadgets when the extra return frame is proven to land on the original continuation.",
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
    id: "z-stack-derived-tail",
    category: "stack",
    source: "documented",
    requires: [],
    activeWhen: ["z-stack-derived-value-reuse"],
    detail: "Computes a shared pure operand once and keeps extra copies in Y/Z (and T for four outputs), restoring them with X↔Y and F reverse for adjacent unary derivations.",
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
    id: "interprocedural-dead-store",
    category: "data",
    source: "documented",
    requires: [],
    activeWhen: ["interprocedural-dead-store"],
    detail: "Drops a field assignment when interprocedural liveness over the rule call graph proves the value is always overwritten before it can be observed (shown, branched on, output, or read back).",
  },
  {
    id: "interprocedural-value-propagation",
    category: "data",
    source: "documented",
    requires: [],
    activeWhen: ["interprocedural-value-propagation"],
    detail: "Replaces a recomputed expression with an equal live variable when an affine-form analysis proves they hold the same value on every path, including merges that need linear-arithmetic reasoning.",
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
  const displayTemplateMasks: PreloadReport[] = [];
  for (const display of ast.displays) {
    if (!displayHasMantissaExponentTemplateShape(display)) continue;
    const register = allocation.registers[displayTemplateMaskScratchName(display)];
    if (register === undefined) continue;
    displayTemplateMasks.push({
      register,
      value: "8,-00-000",
      countsAgainstProgram: false,
    });
  }
  return [...explicit, ...synthetic, ...constants, ...displayTemplateMasks];
}

function buildGeneratedSetupProgram(
  ast: ProgramAst,
  allocation: RegisterAllocation,
  preloads: readonly PreloadReport[],
  options: CompileOptions,
  machineProfile: MachineProfile,
  diagnostics: Diagnostic[],
  optimizations: AppliedOptimization[],
  warnings: string[],
  candidates: CandidateReport[],
): SetupProgramReport | undefined {
  const fields = setupProgramFields(ast);
  const executablePreloads = preloads.flatMap((preload) => executableSetupPreload(preload));
  const needsSetupProgram = fields.length > 0 ||
    executablePreloads.some((preload) => preload.kind === "display-literal") ||
    programUsesDashedCoordReport(ast);
  if (!needsSetupProgram) return undefined;

  const setupOptimizations: AppliedOptimization[] = [];
  const setupContext = new EmitContext(
    ast,
    allocation,
    options,
    machineProfile,
    diagnostics,
    setupOptimizations,
    warnings,
    candidates,
    {},
  );
  setupContext.compileSetupProgramWithPreloads(executablePreloads, fields);
  const optimizedSetup = optimizeItems(setupContext.items, options, setupOptimizations);
  const { steps } = layoutProgram(optimizedSetup.items, diagnostics, options, ast, machineProfile);

  optimizations.push({
    name: "generated-setup-program",
    detail: `Generated optimized setup program for ${fields.map((field) => field.name).join(", ")}.`,
  });
  optimizations.push(...setupOptimizations.map((optimization) => ({
    name: `setup-${optimization.name}`,
    detail: optimization.detail,
  })));

  return {
    steps,
    reason: `initializes ${fields.map((field) => field.name).join(", ")}`,
  };
}

function setupProgramFields(ast: ProgramAst): StateFieldAst[] {
  if (!ast.v2) return [];
  return ast.states
    .flatMap((state) => state.fields)
    .filter((field) =>
      field.initialStack !== undefined ||
      field.initial !== undefined && !isSetupLiteralExpression(field.initial)
    );
}

interface ExecutableSetupPreload {
  register: RegisterName;
  value: string;
  kind: "number" | "display-literal";
}

function executableSetupPreload(preload: PreloadReport): ExecutableSetupPreload[] {
  const register = registerFromText(preload.register);
  const value = executableSetupValue(preload.value);
  if (value !== undefined) return [{ register, value, kind: "number" }];
  const literal = displayLiteralProgram(preload.value);
  const firstSplice =
    signedFirstSpliceDisplayLiteralProgram(preload.value) ??
    exponentTailDisplayLiteralProgram(preload.value) ??
    firstSpliceDisplayLiteralProgram(preload.value);
  if ((literal === undefined || literal.kind === "error") && firstSplice === undefined) return [];
  return [{ register, value: preload.value, kind: "display-literal" }];
}

function executableSetupValue(value: string): string | undefined {
  const trimmed = value.trim();
  if (/^-?\d+(?:[,.]\d+)?(?:E-?\d{1,2})?$/iu.test(trimmed)) return trimmed.replace(",", ".");
  if (/^[A-F][0-9A-F]$/iu.test(trimmed)) {
    return String(formalAddressInfo(Number.parseInt(trimmed, 16)).actual);
  }
  return undefined;
}

function isSetupLiteralExpression(expr: ExpressionAst): boolean {
  if (expr.kind === "number") return true;
  return expr.kind === "unary" && expr.op === "-" && expr.expr.kind === "number";
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
  if (optimizations.some((optimization) =>
    optimization.name === "x2-display-byte-scheduling" ||
    optimization.name === "display-byte-x2-lowering"
  )) {
    add("x2-register", "Optimizer scheduled hidden X2 values across display-byte boundaries.", "optimizer");
  }
  if (optimizations.some((optimization) => optimization.name === "negative-zero-threshold-selector")) {
    add("negative-zero-degree", "Optimizer selected a preloaded negative-zero exponent threshold selector.", "optimizer");
  }
  if (optimizations.some((optimization) => optimization.name === "vp-fraction-restore")) {
    add("x2-restore-boundaries", "Optimizer used ВП as both X2 restoration and fractional/mantissa transform.", "optimizer");
  }
  if (optimizations.some((optimization) => optimization.name === "z-stack-derived-value-reuse")) {
    add("z-stack-register", "Optimizer reused saved operands from Y/Z stack levels with X↔Y and F reverse.", "optimizer");
  }
  if (optimizations.some((optimization) =>
    optimization.name === "hex-mantissa-arithmetic" ||
    optimization.name === "display-byte-x2-lowering"
  )) {
    add("display-bytes", "Optimizer packed state into hexadecimal mantissa/display-byte forms.", "optimizer");
  }
  if (optimizations.some((optimization) => optimization.name === "fractional-indirect-addressing" || optimization.name === "r0-fractional-sentinel")) {
    add("r0-fractional-sentinel", "Optimizer used fractional/indirect addressing side effects under emulator-proved semantics.", "optimizer");
  }
  if (optimizations.some((optimization) => optimization.name === "r0-indirect-counter")) {
    add("r0-t-alias", "Optimizer used R0 indirect behavior with explicit R0 transformation accounted for.", "optimizer");
  }
  if (optimizations.some((optimization) =>
    optimization.name === "error-stop" ||
    optimization.name === "screen-error-literal-lowering"
  )) {
    add("error-stops", "Compiler emitted a domain-error pause or explicit trap.", "optimizer");
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
    const observed = ast.displays.flatMap((display) => display.sources);
    if (observed.length > 0) {
      proofs.push({
        id: "observability",
        status: "proved",
        detail: `Visible state is limited by display output: ${[...new Set(observed)].join(", ")}.`,
      });
    }
  }
  if (cellRoles.some((cell) => cell.roles.includes("display-byte"))) {
    proofs.push({
      id: "display-byte-observable-boundary",
      status: "assumed",
      detail: "Display-byte candidates are bounded by display output and the exact mk61 profile.",
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
    (v2.body.length > 0 ? countV2Statements(v2.body) : 0) +
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
    if (statement.kind === "v2_while") {
      count += countV2Statements(statement.body);
    }
    if (statement.kind === "v2_loop") {
      count += countV2Statements(statement.body);
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
    if (statement.kind === "while") count += countStatements(statement.body);
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
