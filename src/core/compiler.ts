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
import { MachineEmitter } from "./emit/machine-emitter.ts";
import type { ProgramAnalysis } from "./emit/program-analysis.ts";
import { RuntimeHelperRegistry } from "./emit/runtime-helpers.ts";
import { compileExpression } from "./emit/lowering/expr.ts";
import { compileCondition, compileDecrementZeroBranch, compileDispatch, compileDoubleBranchRemoval, compileIf, compileLiteralHalt, compileLiteralShowHalt, emitKnownOneIndirectLoopBack } from "./emit/lowering/control-flow.ts";
import { compileShow, compileShowSequenceRead } from "./emit/lowering/display.ts";
import { compileBitSetMaskReuse, compileSingleBitMaskOpAssignment, compileTicTacToeCellMaskReuse } from "./emit/lowering/spatial.ts";
import { compileCoordListLineCountAssignment, compileCoordListLineCountDashedReport, compileFusedCoordListScan } from "./emit/lowering/coord-list.ts";
import { compileBlockCall, compileDecimalFactorialSeries, compileGuardAssignmentSubstitution, compileInitialState, compileIntFracSharedTail, compileProcedures, compileRawStatement, compileRepeatedAssignmentValue, compileRuntimeHelpers, compileSetupProgramWithPreloads, compileStackUnaryDerivedAssignments, compileUnitDecrement, compileXParamProcCall } from "./emit/lowering/proc-raw-setup.ts";
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

export const DISPATCH_SCRATCH_PREFIX = "__dispatch_";
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
export const COORD_LIST_POINTER = "__coord_list_pointer";
export const COORD_LIST_COUNTER = "__coord_list_counter";
export const COORD_LIST_CURRENT = "__coord_list_current";
export const COORD_LIST_DX = "__coord_list_dx";
export const DASHED_COORD_REPORT_MASK = "8,-00--_";
export const NEGATIVE_ZERO_DEGREE_SELECTOR_GE = "__mkpro_negative_zero_ge";
export const NEGATIVE_ZERO_DEGREE_PRELOAD_VALUE = "1|-00";
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

export interface StackUnaryDerivationCall {
  fn: StackUnaryDerivationFn;
  arg: ExpressionAst;
  opcode: number;
  mnemonic: string;
}

export interface XParamProcLowering {
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

export interface DisplayField {
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

export interface DashedCoordReportTemplate {
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

/**
 * Test-only hook: compile a single explicit lowering variant. Used by the
 * golden-output guardrail to fingerprint every lowering path (not just the
 * selected one). Not part of the public API surface.
 */
export function compileLoweringVariantForTest(
  source: string,
  options: Partial<CompileOptions>,
  loweringOptions: Record<string, unknown>,
): CompileResult {
  return compileLoweringAttempt(source, options, loweringOptions);
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
  liftFunctionCallsInExpressions(ast, optimizations);

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
      if (statement.kind === "return_value") visitExpr(statement.expr);
    }
  };

  for (const entry of ast.entries) visitStatements(entry.body);
  for (const proc of ast.procs) visitStatements(proc.body);
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
    if (statement.kind === "dispatch") {
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

export function matchEqualityConstantCondition(
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

export function expressionIsDeterministic(expr: ExpressionAst): boolean {
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
      if (
        statement.kind === "pause" || statement.kind === "halt" ||
        statement.kind === "return_value"
      ) {
        visitExpr(statement.expr);
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

// The expressions owned directly by a statement (not those inside nested
// statement bodies, which callers recurse into separately).
function statementOwnExpressions(statement: StatementAst): ExpressionAst[] {
  switch (statement.kind) {
    case "assign":
    case "pause":
    case "halt":
    case "return_value":
      return [statement.expr];
    case "if":
    case "while":
      return [statement.condition.left, statement.condition.right];
    case "dispatch":
      return [statement.expr, ...statement.cases.map((branch) => branch.value)];
    case "core":
      return (statement.inputs ?? []).map((input) => input.expr);
    default:
      return [];
  }
}

// All call-expression callee names within an expression tree.
function expressionCallCallees(expr: ExpressionAst): string[] {
  switch (expr.kind) {
    case "call":
      return [expr.callee, ...expr.args.flatMap(expressionCallCallees)];
    case "unary":
      return expressionCallCallees(expr.expr);
    case "binary":
      return [...expressionCallCallees(expr.left), ...expressionCallCallees(expr.right)];
    default:
      return [];
  }
}

function statementExpressionCallees(statement: StatementAst): string[] {
  return statementOwnExpressions(statement).flatMap(expressionCallCallees);
}

function countStatementCalls(ast: ProgramAst): Map<string, number> {
  const counts = new Map<string, number>();
  const add = (name: string): void => {
    counts.set(name, (counts.get(name) ?? 0) + 1);
  };
  const visitList = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "call") add(statement.block);
      for (const callee of statementExpressionCallees(statement)) add(callee);
      if (statement.kind === "loop") visitList(statement.body);
      if (statement.kind === "if") {
        visitList(statement.thenBody);
        if (statement.elseBody !== undefined) visitList(statement.elseBody);
      }
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visitList(dispatchCase.body);
        if (statement.defaultBody !== undefined) visitList(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visitList(entry.body);
  for (const proc of ast.procs) visitList(proc.body);
  return counts;
}

function statementsContainExactMachineCode(statements: StatementAst[]): boolean {
  for (const statement of statements) {
    if (statement.kind === "core" || statement.kind === "decimal_series") return true;
    if (statement.kind === "loop" && statementsContainExactMachineCode(statement.body)) return true;
    if (statement.kind === "while" && statementsContainExactMachineCode(statement.body)) return true;
    if (statement.kind === "if") {
      if (statementsContainExactMachineCode(statement.thenBody)) return true;
      if (statement.elseBody !== undefined && statementsContainExactMachineCode(statement.elseBody)) return true;
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
      if (statement.kind === "dispatch") {
        if (statement.cases.some((dispatchCase) => visit(dispatchCase.body))) return true;
        if (statement.defaultBody !== undefined && visit(statement.defaultBody)) return true;
      }
    }
    return false;
  };
  return ast.entries.some((entry) => visit(entry.body)) ||
    ast.procs.some((proc) => visit(proc.body));
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
    if (statement.kind === "dispatch") {
      for (const dispatchCase of statement.cases) visitList(dispatchCase.body, depth);
      if (statement.defaultBody !== undefined) visitList(statement.defaultBody, depth);
    }
  };

  for (const entry of ast.entries) visitList(entry.body, 0);
  for (const proc of ast.procs) visitList(proc.body, 0);
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

export function invertCondition(condition: ConditionAst): ConditionAst {
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
      if (statement.kind === "show") report(statement.display, statement.line);
      if (statement.kind === "call") report(statement.block, statement.line);
      if (statement.kind === "if") {
        visitCondition(statement.condition);
        visitStatements(statement.thenBody);
        if (statement.elseBody) visitStatements(statement.elseBody);
      }
      if (statement.kind === "loop") visitStatements(statement.body);
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

export class EmitContext {
  // Machine-code emission + X-tracking live in a dedicated collaborator; this
  // class delegates the low-level primitives to it and routes the X-state
  // fields through getters/setters so the lowering code reads unchanged.
  readonly emitter = new MachineEmitter<DashedCoordReportTemplate>();
  get items(): MachineItem[] {
    return this.emitter.items;
  }
  readonly constants = new Map<string, ExpressionAst>();
  readonly constantStack = new Set<string>();
  readonly ast: ProgramAst;
  readonly allocation: RegisterAllocation;
  readonly options: CompileOptions;
  readonly machineProfile: MachineProfile;
  readonly diagnostics: Diagnostic[];
  readonly optimizations: AppliedOptimization[];
  readonly warnings: string[];
  readonly candidates: CandidateReport[];
  readonly loweringOptions: LoweringOptions;
  // Read-only program analysis is computed once and injected; the lowering code
  // reads these maps through getters so call sites stay unchanged.
  readonly analysis: ProgramAnalysis;
  get inlineProcNames(): Set<string> {
    return this.analysis.inlineProcNames;
  }
  get procCallCounts(): Map<string, number> {
    return this.analysis.procCallCounts;
  }
  get functionProcs(): Map<string, ProcAst> {
    return this.analysis.functionProcs;
  }
  get xParamProcs(): Map<string, XParamProcLowering> {
    return this.analysis.xParamProcs;
  }
  get readCounts(): Map<string, number> {
    return this.analysis.readCounts;
  }
  get displayUseCounts(): Map<string, number> {
    return this.analysis.displayUseCounts;
  }
  get showSequenceUseCounts(): Map<string, number> {
    return this.analysis.showSequenceUseCounts;
  }
  get expressionUseCounts(): Map<string, { count: number; expr: ExpressionAst }> {
    return this.analysis.expressionUseCounts;
  }
  get nearAnyHelperStats(): Map<string, NearAnyHelperStats> {
    return this.analysis.nearAnyHelperStats;
  }
  get lineCountCallCount(): number {
    return this.analysis.lineCountCallCount;
  }
  get lineCountGroupCounts(): Map<string, number> {
    return this.analysis.lineCountGroupCounts;
  }
  get scaledCoordLists(): Set<string> {
    return this.analysis.scaledCoordLists;
  }
  get scaledCoordCellNames(): Set<string> {
    return this.analysis.scaledCoordCellNames;
  }
  readonly scaledCoordVariables = new Set<string>();
  // Shared runtime-helper tables live in a dedicated collaborator; lowering
  // reads/registers them through these getters so call sites stay unchanged.
  readonly helpers = new RuntimeHelperRegistry();
  get spatialHitHelpers() {
    return this.helpers.spatialHitHelpers;
  }
  get displayHelpers() {
    return this.helpers.displayHelpers;
  }
  get displayByteHelpers() {
    return this.helpers.displayByteHelpers;
  }
  get literalDisplayHelpers() {
    return this.helpers.literalDisplayHelpers;
  }
  get showSequenceHelpers() {
    return this.helpers.showSequenceHelpers;
  }
  get expressionHelpers() {
    return this.helpers.expressionHelpers;
  }
  get randomCellHelpers() {
    return this.helpers.randomCellHelpers;
  }
  get nearAnyHelpers() {
    return this.helpers.nearAnyHelpers;
  }
  get lineCountHelpers() {
    return this.helpers.lineCountHelpers;
  }
  get spatialBitMaskHelpers() {
    return this.helpers.spatialBitMaskHelpers;
  }
  get spatialLineProgressionHelpers() {
    return this.helpers.spatialLineProgressionHelpers;
  }
  get spatialSumLoopHelpers() {
    return this.helpers.spatialSumLoopHelpers;
  }
  get terminalTailHelpers() {
    return this.helpers.terminalTailHelpers;
  }
  // X-tracking state physically lives in `this.emitter`; these accessors keep
  // the lowering code reading/writing it as plain fields.
  get currentXVariable(): string | undefined {
    return this.emitter.currentXVariable;
  }
  set currentXVariable(value: string | undefined) {
    this.emitter.currentXVariable = value;
  }
  get currentXAliases(): Set<string> {
    return this.emitter.currentXAliases;
  }
  set currentXAliases(value: Set<string>) {
    this.emitter.currentXAliases = value;
  }
  get currentXKnownZero(): boolean {
    return this.emitter.currentXKnownZero;
  }
  set currentXKnownZero(value: boolean) {
    this.emitter.currentXKnownZero = value;
  }
  get coordListCounterKnownOne(): boolean {
    return this.emitter.coordListCounterKnownOne;
  }
  set coordListCounterKnownOne(value: boolean) {
    this.emitter.coordListCounterKnownOne = value;
  }
  get zeroAddressLabels(): Set<string> {
    return this.emitter.zeroAddressLabels;
  }
  get currentXDashedCoordReportBody(): DashedCoordReportTemplate | undefined {
    return this.emitter.currentXDashedCoordReportBody;
  }
  set currentXDashedCoordReportBody(value: DashedCoordReportTemplate | undefined) {
    this.emitter.currentXDashedCoordReportBody = value;
  }
  get machineEntryOpen(): boolean {
    return this.emitter.machineEntryOpen;
  }
  set machineEntryOpen(value: boolean) {
    this.emitter.machineEntryOpen = value;
  }
  get emittingExpressionHelper(): boolean {
    return this.helpers.emittingExpressionHelper;
  }
  set emittingExpressionHelper(value: boolean) {
    this.helpers.emittingExpressionHelper = value;
  }
  get emittingRandomCellHelper(): boolean {
    return this.helpers.emittingRandomCellHelper;
  }
  set emittingRandomCellHelper(value: boolean) {
    this.helpers.emittingRandomCellHelper = value;
  }

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
    this.analysis = buildProgramAnalysis(ast, allocation);
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

    if (hoistProcs) compileProcedures(this);

    this.emitLabel(main.name);
    compileInitialState(this);
    this.compileStatements(main.body);
    if (!(this.ast.v2 && this.statementsTerminate(main.body))) {
      this.emitOp(0x50, "С/П", "implicit final stop");
    }

    if (!hoistProcs) compileProcedures(this);

    const helperStart = this.items.length;
    compileRuntimeHelpers(this);
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




  randomCoordListSetupContext(fields: readonly StateFieldAst[]): {
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




  compileStatements(statements: StatementAst[]): void {
    for (let index = 0; index < statements.length; index += 1) {
      const statement = statements[index]!;
      const next = statements[index + 1];
      if (statement.kind === "assign" && next?.kind === "call" && compileXParamProcCall(this, statement, next)) {
        index += 1;
        continue;
      }
      if (statement.kind === "assign") {
        const reused = compileRepeatedAssignmentValue(this, statements, index);
        if (reused > 1) {
          index += reused - 1;
          continue;
        }
        const derived = compileStackUnaryDerivedAssignments(this, statements, index);
        if (derived > 1) {
          index += derived - 1;
          continue;
        }
      }
      if (statement.kind === "assign" && next?.kind === "if" && compileDecrementZeroBranch(this, statement, next)) {
        index += 1;
        continue;
      }
      if (statement.kind === "assign" && next?.kind === "assign" && compileTicTacToeCellMaskReuse(this, statement, next)) {
        index += 1;
        continue;
      }
      if (statement.kind === "assign" && next?.kind === "assign" && compileBitSetMaskReuse(this, statement, next)) {
        index += 1;
        continue;
      }
      if (statement.kind === "assign" && next?.kind === "assign" && compileIntFracSharedTail(this, statement, next)) {
        index += 1;
        continue;
      }
      if (
        statement.kind === "assign" &&
        next?.kind === "show" &&
        index + 2 === statements.length &&
        compileCoordListLineCountDashedReport(this, statement, next)
      ) {
        index += 1;
        continue;
      }
      if (statement.kind === "if") {
        const fused = compileFusedCoordListScan(this, statements, index);
        if (fused > 1) {
          index += fused - 1;
          continue;
        }
      }
      if (statement.kind === "assign" && next?.kind === "if" && compileGuardAssignmentSubstitution(this, statement, next)) {
        index += 1;
        continue;
      }
      if (statement.kind === "if" && next?.kind === "if" && compileDoubleBranchRemoval(this, statement, next)) {
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
      if (statement.kind === "show" && next?.kind === "halt" && compileLiteralShowHalt(this, statement, next)) {
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
        if (compileShowSequenceRead(this, statement, next, input)) {
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
        compileShow(this, statement.display, statement.line);
        this.markCurrentX(next.target);
        compileIf(this, branch, branch.line);
        this.optimizations.push({
          name: "ephemeral-input-branch",
          detail: `Branched directly on input ${next.target} at line ${next.line} without storing it.`,
        });
        index += 2;
        continue;
      }
      if (statement.kind === "show" && next?.kind === "input") {
        compileShow(this, statement.display, statement.line);
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
        compileIf(this, next, next.line);
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

  inputFeedsOnlyFollowingCondition(
    input: Extract<StatementAst, { kind: "input" }>,
    branch: Extract<StatementAst, { kind: "if" }>,
  ): boolean {
    const reads = countIdentifierReadsInCondition(branch.condition, input.target);
    return reads > 0 && (this.readCounts.get(input.target) ?? 0) === reads;
  }


  markCurrentX(name: string): void {
    this.currentXVariable = name;
    this.currentXAliases = new Set([name]);
    this.currentXKnownZero = false;
    this.currentXDashedCoordReportBody = undefined;
  }





  haltDisplaysSameValue(
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



  compileStatement(statement: StatementAst): void {
    switch (statement.kind) {
      case "pause":
        compileExpression(this, statement.expr);
        this.emitOp(0x50, "С/П", "pause", statement.line);
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
          compileLiteralHalt(this, statement.literal, statement.line);
          return;
        }
        compileExpression(this, statement.expr);
        this.emitOp(0x50, "С/П", "halt", statement.line);
        return;
      case "assign":
        if (compileCoordListLineCountAssignment(this, statement)) return;
        if (compileUnitDecrement(this, statement)) return;
        if (compileSingleBitMaskOpAssignment(this, statement)) return;
        compileExpression(this, statement.expr);
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
          if (!emitKnownOneIndirectLoopBack(this, start, statement.line)) {
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
        compileCondition(this, statement.condition, end, statement.line);
        this.compileStatements(statement.body);
        if (!this.statementsEndMachineFlow(statement.body)) {
          this.emitJump(0x51, "БП", start, "while loop back", statement.line);
        }
        this.emitLabel(end);
        return;
      }
      case "if":
        compileIf(this, statement, statement.line);
        return;
      case "dispatch":
        compileDispatch(this, statement);
        return;
      case "show":
        compileShow(this, statement.display, statement.line);
        return;
      case "call":
        compileBlockCall(this, statement.block, statement.line);
        return;
      case "core":
        compileRawStatement(this, statement);
        return;
      case "return_value":
        compileExpression(this, statement.expr);
        this.emitOp(0x52, "В/О", "return value", statement.line);
        return;
      case "decimal_series":
        compileDecimalFactorialSeries(this, statement);
        return;
    }
  }




  // Stack-safe lowering for a standalone `cells += item` / `cells -= item`
  // (see matchSingleBitMaskOpAssignment). Builds the cell mask into a scratch
  // register first so the held accumulator never rides the four-deep stack
  // through the frac/x^y/10^x construction.


  sharedBitMaskHelperScratch(): string | undefined {
    if (this.loweringOptions.sharedBitMaskHelperCalls !== true) return undefined;
    return this.allocation.registers[SHARED_BIT_MASK_SCRATCH] === undefined ? undefined : SHARED_BIT_MASK_SCRATCH;
  }

  // Build the `8.HHHHHHH` cell-mask value for the bit index currently in X (see
  // bitMaskExpression for the representation). The bit lands in fractional nibble
  // floor(index/4)+1; `2^(index mod 4)` is rounded because `F x^y` is imprecise.





  nearAnyFallthroughCandidate(
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

  falseBranchCurrentXCandidate(
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

  firstInlineStatement(statements: StatementAst[], seen = new Set<string>()): StatementAst | undefined {
    const first = statements[0];
    if (first?.kind !== "call") return first;
    if (!this.inlineProcNames.has(first.block) || seen.has(first.block)) return first;
    const proc = this.ast.procs.find((candidate) => candidate.name === first.block);
    if (proc === undefined) return first;
    seen.add(first.block);
    return this.firstInlineStatement(proc.body, seen);
  }

  inlineStatementPrefix(statements: StatementAst[], seen = new Set<string>()): StatementAst[] {
    const first = statements[0];
    if (first?.kind !== "call") return statements;
    if (!this.inlineProcNames.has(first.block) || seen.has(first.block)) return statements;
    const proc = this.ast.procs.find((candidate) => candidate.name === first.block);
    if (proc === undefined) return statements;
    seen.add(first.block);
    return this.inlineStatementPrefix(proc.body, seen);
  }

  statementStartsWithCurrentXUse(statement: StatementAst | undefined, variable: string): boolean {
    if (statement === undefined) return false;
    if (statement.kind === "if") return this.conditionCanUseCurrentX(statement.condition, variable);
    if (statement.kind === "assign") return this.expressionCanUseCurrentX(statement.expr, variable);
    return false;
  }

  conditionCanUseCurrentX(condition: ConditionAst, variable: string): boolean {
    const selected = selectCheaperEquivalentCondition(
      condition,
      this.ast,
      new Set(Object.keys(this.allocation.constants)),
    ).condition;
    const normalized = normalizeZeroComparison(selected);
    return normalized !== undefined && this.expressionCanUseCurrentX(normalized.expr, variable);
  }

  expressionCanUseCurrentX(expr: ExpressionAst, variable: string): boolean {
    return expr.kind === "binary" &&
      (expr.op === "+" || expr.op === "*") &&
      (
        (expr.left.kind === "identifier" && expr.left.name === variable && isSimpleStackLoad(expr.right)) ||
        (expr.right.kind === "identifier" && expr.right.name === variable && isSimpleStackLoad(expr.left))
      );
  }


  branchOrderStatement(
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


  directTerminalCallTarget(statements: StatementAst[], seen = new Set<string>()): string | undefined {
    if (statements.length !== 1) return undefined;
    const statement = statements[0];
    if (statement?.kind !== "call") return undefined;

    const proc = this.ast.procs.find((candidate) => candidate.name === statement.block);
    if (proc === undefined) return undefined;
    if (this.inlineProcNames.has(proc.name)) {
      if (seen.has(proc.name)) return undefined;
      seen.add(proc.name);
      return this.directTerminalCallTarget(proc.body, seen);
    }
    return this.statementsTerminate(proc.body) ? proc.name : undefined;
  }



  ensureTerminalTailHelper(body: StatementAst[], line: number): { body: StatementAst[]; label: string; line: number } {
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











  membershipClearPrefix(statements: StatementAst[]): {
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

  membershipSetPrefix(
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

  membershipSetRunPrefix(
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



  recordRejectedNegativeZeroBranchCandidate(statement: Extract<StatementAst, { kind: "if" }>): void {
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







  statementsTerminate(statements: StatementAst[]): boolean {
    return this.statementListTerminates(statements, new Set());
  }

  statementsEndMachineFlow(statements: StatementAst[]): boolean {
    return this.statementListEndsMachineFlow(statements, new Set());
  }

  statementListTerminates(statements: StatementAst[], seenProcs: Set<string>): boolean {
    const last = statements.at(-1);
    if (!last) return false;
    return this.statementTerminates(last, seenProcs);
  }

  statementListEndsMachineFlow(statements: StatementAst[], seenProcs: Set<string>): boolean {
    const last = statements.at(-1);
    if (!last) return false;
    return this.statementEndsMachineFlow(last, seenProcs);
  }

  statementTerminates(statement: StatementAst, seenProcs: Set<string>): boolean {
    if (
      statement.kind === "halt" || statement.kind === "loop" ||
      statement.kind === "decimal_series" || statement.kind === "return_value"
    ) {
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
    const proc = this.ast.procs.find((candidate) => candidate.name === statement.block);
    if (proc === undefined || seenProcs.has(proc.name)) return false;
    seenProcs.add(proc.name);
    return this.statementListTerminates(proc.body, seenProcs);
  }

  statementEndsMachineFlow(statement: StatementAst, seenProcs: Set<string>): boolean {
    if (
      statement.kind === "loop" || statement.kind === "decimal_series" ||
      statement.kind === "return_value"
    ) {
      return true;
    }
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
    const proc = this.ast.procs.find((candidate) => candidate.name === statement.block);
    if (proc === undefined || seenProcs.has(proc.name)) return false;
    seenProcs.add(proc.name);
    return this.statementListEndsMachineFlow(proc.body, seenProcs);
  }



  currentXDashedCoordReportBodyMatches(template: DashedCoordReportTemplate): boolean {
    const body = this.currentXDashedCoordReportBody;
    return body !== undefined &&
      body.cell.name === template.cell.name &&
      body.cell.width === template.cell.width &&
      body.bearing.name === template.bearing.name &&
      body.bearing.width === template.bearing.width;
  }


  selectDisplayStrategy(display: ProgramAst["displays"][number]): DisplayStrategyVariant | undefined {
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

  displayStrategyCandidates(display: ProgramAst["displays"][number]): DisplayStrategyCandidate[] {
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





  numericDisplayFields(
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

  displaySourceFields(display: ProgramAst["displays"][number]): DisplayField[] {
    return display.items
      .filter((item): item is DisplaySourceItem => item.kind === "source")
      .map((item) => ({ kind: "source", item, name: item.name, width: item.width ?? this.naturalDisplayWidth(item.name) }));
  }

  estimateDecimalDisplayCost(fields: DisplayField[], reuseCurrentX: boolean): number {
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

  estimateDisplayFieldValueCost(field: DisplayField): number {
    return field.kind === "literal" ? this.estimateNumberOrPreloadCost(field.value ?? "0") : 1;
  }

  estimateNumberOrPreloadCost(raw: string): number {
    return this.allocation.constants[normalizeConstantLiteral(raw)] === undefined ? estimateNumberCost(raw) : 1;
  }

  packedStorageReuseFields(display: ProgramAst["displays"][number]): DisplayField[] | undefined {
    const fields = this.numericDisplayFields(display);
    if (fields === undefined || fields.length < 2) return undefined;
    if (fields.some((field) => field.kind !== "source")) return undefined;
    const firstState = this.findStateField(fields[0]!.name);
    if (firstState?.type !== "packed") return undefined;
    if (!fields.slice(1).every((field) => field.item?.width !== undefined)) return undefined;
    return fields;
  }

  estimatePackedStorageReuseCost(fields: DisplayField[], reuseCurrentX: boolean): number {
    if (fields.length === 0) return 2;
    const currentIndex = reuseCurrentX && this.currentXVariable !== undefined
      ? fields.findIndex((field) => field.name === this.currentXVariable)
      : -1;
    const recalled = currentIndex >= 0 ? fields.length - 1 : fields.length;
    return recalled + Math.max(0, fields.length - 1) + 1;
  }


  orderStorageReuseFields(fields: DisplayField[], reuseCurrentX: boolean): DisplayField[] {
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

  canCompileDisplayByteBuilder(display: ProgramAst["displays"][number]): boolean {
    if (!machineSupports(this.machineProfile, "display-bytes")) return false;
    return this.mantissaExponentDisplayTemplate(display) !== undefined &&
      this.displayTemplateScratchRegisters(display) !== undefined ||
      this.mantissaMaskDisplayTemplate(display) !== undefined &&
      this.displayMaskScratchRegister(display) !== undefined &&
      this.displayMaskRegister(display) !== undefined;
  }

  estimateDisplayByteBuilderCost(
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



  mantissaExponentDisplayTemplate(
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

  mantissaMaskDisplayTemplate(
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

  displayFieldFitsUnsignedWidth(field: DisplayField): boolean {
    const state = this.findStateField(field.name);
    if (state === undefined) return false;
    const min = state.min ?? 0;
    const max = state.max ?? min;
    return min >= 0 && max < 10 ** field.width;
  }

  displayFieldCanBeZero(field: DisplayField): boolean {
    const state = this.findStateField(field.name);
    return state === undefined || (state.min ?? 0) <= 0;
  }

  displayFieldMin(field: DisplayField): number | undefined {
    return this.findStateField(field.name)?.min;
  }

  displayTemplateScratchRegisters(display: ProgramAst["displays"][number]): {
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

  displayMaskScratchRegister(display: ProgramAst["displays"][number]): RegisterName | undefined {
    return this.allocation.registers[displayTemplateValueScratchName(display)];
  }

  displayMaskRegister(display: ProgramAst["displays"][number]): RegisterName | undefined {
    const template = this.mantissaMaskDisplayTemplate(display);
    return template === undefined ? undefined : this.allocation.constants[normalizeConstantLiteral(template.mask)];
  }





  canReorderNumericDisplay(display: ProgramAst["displays"][number]): boolean {
    return display.sources.length <= 1 && display.items.every((item) => item.kind === "source" && item.width === undefined);
  }

  naturalDisplayWidth(source: string): number {
    const field = this.findStateField(source);
    if (field === undefined) return 1;
    const min = field.min ?? 0;
    const max = field.max ?? min;
    const magnitude = Math.max(Math.abs(min), Math.abs(max));
    return Math.max(1, String(Math.trunc(magnitude)).length);
  }

  reportPackedDisplayLowering(display: ProgramAst["displays"][number]): void {
    const canUseDisplayBytes = machineSupports(this.machineProfile, "display-bytes");
    this.optimizations.push({
      name: "packed-display-lowering",
      detail: canUseDisplayBytes
        ? `Display ${display.name} may use display-byte encodings in later layout passes.`
        : `Display ${display.name} lowered as ordinary packed numeric output.`,
    });
  }

  sharedDisplayByteHelper(
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

  shouldShareDisplayByte(display: ProgramAst["displays"][number]): boolean {
    if (!this.canCompileDisplayByteBuilder(display)) return false;
    const uses = this.displayUseCounts.get(display.name) ?? 0;
    if (uses < 2) return false;
    const bodyCost = this.estimateDisplayByteBuilderCost(display, this.displaySourceFields(display), false);
    const helperCost = uses * 2 + bodyCost + 1;
    const inlineTotal = uses * bodyCost;
    return inlineTotal - helperCost >= DISPLAY_HELPER_MIN_SAVINGS;
  }

  sharedDisplayHelper(
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

  shouldShareDisplay(display: ProgramAst["displays"][number]): boolean {
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

  sharedShowSequenceHelper(
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

  shouldShareShowSequence(
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





  firstSpliceDisplayScratch(display: ProgramAst["displays"][number]): RegisterName | undefined {
    return this.allocation.registers[firstSpliceDisplayScratchName(display)];
  }






  signDigitLiteralScratch(): { indirect: RegisterName; source: RegisterName } | undefined {
    const used = this.usedAllocatedRegisters();
    const indirect = (["4", "5", "6"] as const).find((register) => !used.has(register));
    if (indirect === undefined) return undefined;
    const source = [...REGISTER_ORDER].reverse().find((register) => register !== indirect && !used.has(register));
    if (source === undefined) return undefined;
    return { indirect, source };
  }

  scratchRegistersAvailable(registers: ReadonlySet<RegisterName>): boolean {
    const used = this.usedAllocatedRegisters();
    return [...registers].every((register) => !used.has(register));
  }

  usedAllocatedRegisters(): Set<RegisterName> {
    const used = new Set<RegisterName>([
      ...Object.values(this.allocation.registers),
      ...Object.values(this.allocation.constants),
    ]);
    if (this.allocation.negativeZeroDegree !== undefined) used.add(this.allocation.negativeZeroDegree);
    return used;
  }



  sharedLiteralDisplayHelper(
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

  shouldShareLiteralDisplay(display: ProgramAst["displays"][number]): boolean {
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

  collapseLiteralOnlyDisplay(display: ProgramAst["displays"][number]): string | undefined {
    if (display.items.length === 0) return "";
    if (display.items.some((item) => item.kind !== "literal")) return undefined;
    const text = display.items.map((item) => item.kind === "literal" ? item.text : "").join("");
    if (text.length === 0) return "";
    return text.trim().length === 0 ? undefined : text;
  }

  collapseTextPrefixDisplay(
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


  findStateField(name: string): StateFieldAst | undefined {
    for (const state of this.ast.states) {
      const field = state.fields.find((candidate) => candidate.name === name);
      if (field !== undefined) return field;
    }
    return undefined;
  }

  currentAddress(): number {
    return this.items.filter((item) => item.kind !== "label").length;
  }


  procReturnXVariable(proc: ProgramAst["procs"][number]): string | undefined {
    if (this.statementsTerminate(proc.body)) return undefined;
    const last = proc.body.at(-1);
    return last?.kind === "assign" ? last.target : undefined;
  }



  dashedCoordReportTemplateAfterLineCount(
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




  coordListUsesScaledDecimalStorage(callOrList: CoordListCall | string): boolean {
    const listName = typeof callOrList === "string" ? callOrList : coordListNameFromItems(callOrList.items);
    return listName !== undefined && this.scaledCoordLists.has(listName);
  }

  scaleCoordListCellInPlace(cell: ExpressionAst, line: number): boolean {
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





  coordListLineCountAssignmentFromStatement(
    statement: StatementAst,
  ): Extract<StatementAst, { kind: "assign" }> | undefined {
    if (statement.kind === "assign" && coordListLineCountCall(statement.expr) !== undefined) return statement;
    if (statement.kind !== "call") return undefined;
    const proc = this.ast.procs.find((candidate) => candidate.name === statement.block);
    if (proc?.body.length !== 1) return undefined;
    const [only] = proc.body;
    return only?.kind === "assign" && coordListLineCountCall(only.expr) !== undefined ? only : undefined;
  }

  coordListFusedHitBodyAllowed(statements: StatementAst[], seen = new Set<string>()): boolean {
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






  coordListIndirectContext(call: CoordListCall): CoordListIndirectContext | undefined {
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












  nearAnyHelper(
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







  // Stack-duplicate a repeated pure operand: `e op e` becomes compute(e), В↑,
  // op, so the shared value is reused from the stack (Y) instead of recomputed.
  // Only applies to pure operands (so one evaluation equals two) and only when it
  // actually saves cells versus recomputing the operand.

  // Shared tail for the integer/fractional parts of one pure operand. When two
  // adjacent assignments take int(e) and frac(e) of the same pure expression e,
  // compute e once, duplicate it through the stack (В↑), take К [x] for the
  // integer target, swap the saved copy back with X↔Y, and take К {x} for the
  // fractional target. Both parts are produced by their own opcodes (identical
  // behavior, including the -0 fractional result of negative integers), and the
  // operand is evaluated once instead of twice.


  sharedRandomCellHelper(expr: ExpressionAst): { expr: ExpressionAst; label: string; line?: number } | undefined {
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

  shouldShareRandomCellExpression(expr: ExpressionAst): boolean {
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

  sharedExpressionHelper(expr: ExpressionAst): { expr: ExpressionAst; label: string; line?: number } | undefined {
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

  shouldShareExpression(expr: ExpressionAst): boolean {
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

  // Compiles a call to a user-defined value-returning function. Arguments are
  // evaluated and stored into the function's parameter registers (the argument
  // list is call-free after function-call lifting, so the working stack is not
  // clobbered between argument evaluations), then `ПП` jumps to the function,
  // which leaves its result in X. Returns false when the callee is not a
  // function (so built-in expression calls fall through).

















  sharedLineCountHelper(
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

  ensureSpatialHitHelper(mask: string, scratch: string): { mask: string; scratch: string; label: string; line?: number } {
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

  ensureSpatialBitMaskHelper(
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

  ensureSpatialSumLoopHelper(
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

  ensureSpatialLineProgressionHelper(
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



  emitNumber(raw: string): void {
    this.emitter.emitNumber(raw);
  }

  emitZero(comment?: string, sourceLine?: number): void {
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

  emitNumberOrPreload(raw: string): void {
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

  emitStore(name: string, comment?: string, sourceLine?: number, raw = false): void {
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

  emitRecall(name: string, comment?: string, sourceLine?: number): void {
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
  xHolds(name: string): boolean {
    if (name === this.currentXVariable) return true;
    return this.loweringOptions.aliasXReuse === true && this.currentXAliases.has(name);
  }

  emitJump(
    opcode: number,
    mnemonic: string,
    target: string | number,
    comment?: string,
    sourceLine?: number,
  ): void {
    this.emitter.emitJump(opcode, mnemonic, target, comment, sourceLine);
  }

  emitAddress(
    target: string | number,
    comment?: string,
    sourceLine?: number,
  ): void {
    this.emitter.emitAddress(target, comment, sourceLine);
  }

  emitFormalAddress(
    opcode: number,
    comment?: string,
    sourceLine?: number,
  ): void {
    this.emitter.emitFormalAddress(opcode, comment, sourceLine);
  }

  emitOp(
    opcode: number,
    mnemonic?: string,
    comment?: string,
    sourceLine?: number,
    raw = false,
  ): void {
    this.emitter.emitOp(opcode, mnemonic, comment, sourceLine, raw);
  }

  emitLabel(name: string): void {
    this.emitter.emitLabel(name);
  }

  freshLabel(prefix: string): string {
    return this.emitter.freshLabel(prefix);
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

export interface CoordListIndirectContext {
  cell: ExpressionAst;
  count: number;
  pointerStart: number;
  pointerRegister: RegisterName;
  counterRegister: RegisterName;
}

export function coordListHasConditionCall(condition: ConditionAst): CoordListCall | undefined {
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

export function coordListLineCountCall(expr: ExpressionAst): CoordListCall | undefined {
  if (expr.kind !== "call" || expr.callee.toLowerCase() !== "coord_list_line_count") return undefined;
  if (expr.args.length < 2) return undefined;
  const [cell, ...items] = expr.args;
  if (cell === undefined) return undefined;
  const identifiers = items.every((item): item is Extract<ExpressionAst, { kind: "identifier" }> => item.kind === "identifier");
  if (!identifiers) return undefined;
  return { cell, items: items.map((item) => ({ name: item.name })) };
}

export function sameCoordListCall(left: CoordListCall, right: CoordListCall): boolean {
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
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };

  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
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
      if (statement.kind === "if") {
        visitCondition(statement.condition);
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "loop") visit(statement.body);
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
      if (statement.kind === "if") {
        visitCondition(statement.condition);
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "loop") visit(statement.body);
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
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };

  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
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
      if (statement.kind === "if") {
        if (!conditionSafe(statement.condition)) return false;
        if (!statementsSafe(statement.thenBody, seenProcs)) return false;
        if (statement.elseBody !== undefined && !statementsSafe(statement.elseBody, seenProcs)) return false;
      }
      if (statement.kind === "loop" && !statementsSafe(statement.body, seenProcs)) return false;
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
    ast.procs.every((proc) => statementsSafe(proc.body));
}

function coordListItemInfo(name: string): { listName: string; index: number } | undefined {
  if (!name.startsWith(COORD_LIST_ITEM_PREFIX)) return undefined;
  const match = /^__coord_list_(.+)_(\d+)$/u.exec(name);
  if (!match) return undefined;
  return { listName: match[1]!, index: Number(match[2]) };
}

export function isPreincrementIndirectRegister(register: RegisterName): boolean {
  return register === "4" || register === "5" || register === "6";
}

function buildProgramAnalysis(ast: ProgramAst, allocation: RegisterAllocation): ProgramAnalysis {
  const procCallCounts = collectProcCallCounts(ast);
  const inlineProcNames = findInlineProcNamesBySize(ast, procCallCounts);
  const readCounts = collectVariableReadCounts(ast);
  const scaledCoordLists = collectScaledCoordListNames(ast);
  return {
    procCallCounts,
    inlineProcNames,
    functionProcs: new Map(
      ast.procs.filter((proc) => procContainsReturnValue(proc.body)).map((proc) => [proc.name, proc]),
    ),
    xParamProcs: collectXParamProcLowerings(ast, readCounts, inlineProcNames),
    readCounts,
    displayUseCounts: collectDisplayUseCounts(ast),
    showSequenceUseCounts: collectShowSequenceUseCounts(ast),
    expressionUseCounts: collectExpressionUseCounts(ast),
    nearAnyHelperStats: collectNearAnyHelperStats(ast, new Set(Object.keys(allocation.constants))),
    lineCountCallCount: countCalls(ast, "line_count"),
    lineCountGroupCounts: collectLineCountGroupCounts(ast),
    scaledCoordLists,
    scaledCoordCellNames: collectScaledCoordCellNames(ast, scaledCoordLists),
  };
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
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body, currentProc);
        if (statement.defaultBody) visit(statement.defaultBody, currentProc);
      }
    }
  };

  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body, proc.name);
  return recursive;
}

function findSingleUseProcNames(ast: ProgramAst): Set<string> {
  const counts = collectProcCallCounts(ast);
  const recursive = collectRecursiveProcNames(ast);
  const functions = collectFunctionProcNames(ast);

  return new Set(
    [...counts.entries()]
      .filter(([name, count]) => count === 1 && !recursive.has(name) && !functions.has(name))
      .map(([name]) => name),
  );
}

// A proc that returns a value (its body contains a `return`). Value-returning
// functions are always emitted as ПП/В/О subroutines and never inlined or
// dropped, because expression-position calls jump to their label.
function procContainsReturnValue(body: readonly StatementAst[]): boolean {
  return body.some((statement) => {
    switch (statement.kind) {
      case "return_value":
        return true;
      case "if":
        return procContainsReturnValue(statement.thenBody) ||
          (statement.elseBody !== undefined && procContainsReturnValue(statement.elseBody));
      case "loop":
      case "while":
        return procContainsReturnValue(statement.body);
      case "dispatch":
        return statement.cases.some((branch) => procContainsReturnValue(branch.body)) ||
          (statement.defaultBody !== undefined && procContainsReturnValue(statement.defaultBody));
      default:
        return false;
    }
  });
}

function collectFunctionProcNames(ast: ProgramAst): Set<string> {
  return new Set(ast.procs.filter((proc) => procContainsReturnValue(proc.body)).map((proc) => proc.name));
}

function findInlineProcNamesBySize(ast: ProgramAst, counts = collectProcCallCounts(ast)): Set<string> {
  const recursive = collectRecursiveProcNames(ast);
  const functions = collectFunctionProcNames(ast);
  const inlineNames = new Set<string>();
  for (const proc of ast.procs) {
    const uses = counts.get(proc.name) ?? 0;
    if (uses === 0 || recursive.has(proc.name) || functions.has(proc.name)) continue;
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
  if (
    statement.kind === "halt" || statement.kind === "loop" ||
    statement.kind === "return_value"
  ) {
    return true;
  }
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
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visitEveryStatementList(dispatchCase.body, atTail ? terminalDisplay : undefined);
        if (statement.defaultBody !== undefined) visitEveryStatementList(statement.defaultBody, atTail ? terminalDisplay : undefined);
      }
    }
  };

  for (const entry of ast.entries) visitEveryStatementList(entry.body);
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
  if (last.kind === "dispatch") {
    for (const dispatchCase of last.cases) removed += elideTailShowInStatementList(dispatchCase.body, display);
    if (last.defaultBody !== undefined) removed += elideTailShowInStatementList(last.defaultBody, display);
  }
  return removed;
}

// Flattens value-returning function calls out of compound expressions so each
// remaining function call sits in an X-result position (the sole right-hand
// side of an assignment, or the sole operand of return/pause/halt). A function
// call lowers to `ПП`, which clobbers the whole X/Y/Z/T working stack, so a
// nested call would destroy a partially built expression. Hoisting nested calls
// into preceding temporary assignments keeps the working stack discipline
// intact; the temporaries are short-lived and the optimizer reclaims them.
function liftFunctionCallsInExpressions(ast: ProgramAst, optimizations: AppliedOptimization[]): void {
  const functions = collectFunctionProcNames(ast);
  if (functions.size === 0) return;
  let lifted = 0;
  let counter = 0;
  const freshTemp = (): string => {
    counter += 1;
    return `${INTERNAL_NAME_PREFIX}call_${counter}`;
  };

  // Hoist nested function calls in `expr` into `prelude`. When `allowRootCall`
  // is true a function call at the very root may stay in place (it produces the
  // value directly in X); otherwise even a root call is hoisted to a temp.
  const liftExpr = (expr: ExpressionAst, prelude: StatementAst[], allowRootCall: boolean, line: number): ExpressionAst => {
    switch (expr.kind) {
      case "number":
      case "identifier":
        return expr;
      case "unary":
        return { ...expr, expr: liftExpr(expr.expr, prelude, false, line) };
      case "binary":
        return {
          ...expr,
          left: liftExpr(expr.left, prelude, false, line),
          right: liftExpr(expr.right, prelude, false, line),
        };
      case "call": {
        const loweredArgs = expr.args.map((arg) => liftExpr(arg, prelude, false, line));
        const loweredCall: ExpressionAst = { ...expr, args: loweredArgs };
        if (!functions.has(expr.callee)) return loweredCall;
        if (allowRootCall) return loweredCall;
        const temp = freshTemp();
        prelude.push({ kind: "assign", target: temp, expr: loweredCall, line });
        lifted += 1;
        return { kind: "identifier", name: temp };
      }
    }
  };

  const liftStatement = (statement: StatementAst): StatementAst[] => {
    const prelude: StatementAst[] = [];
    switch (statement.kind) {
      case "assign":
      case "pause":
      case "halt":
      case "return_value": {
        const expr = liftExpr(statement.expr, prelude, true, statement.line);
        return [...prelude, { ...statement, expr }];
      }
      case "if": {
        const left = liftExpr(statement.condition.left, prelude, false, statement.line);
        const right = liftExpr(statement.condition.right, prelude, false, statement.line);
        const rebuilt: StatementAst = {
          ...statement,
          condition: { ...statement.condition, left, right },
          thenBody: liftStatements(statement.thenBody),
          ...(statement.elseBody === undefined ? {} : { elseBody: liftStatements(statement.elseBody) }),
        };
        return [...prelude, rebuilt];
      }
      case "while": {
        // The condition is re-evaluated every iteration, so its hoisted prelude
        // must run before the loop and again at the end of the body.
        const left = liftExpr(statement.condition.left, prelude, false, statement.line);
        const right = liftExpr(statement.condition.right, prelude, false, statement.line);
        const body = liftStatements(statement.body);
        const rebuilt: StatementAst = {
          ...statement,
          condition: { ...statement.condition, left, right },
          body: prelude.length === 0 ? body : [...body, ...cloneStatements(prelude)],
        };
        return [...prelude, rebuilt];
      }
      case "loop":
        return [{ ...statement, body: liftStatements(statement.body) }];
      case "dispatch": {
        const expr = liftExpr(statement.expr, prelude, false, statement.line);
        const rebuilt: StatementAst = {
          ...statement,
          expr,
          cases: statement.cases.map((dispatchCase) => ({ ...dispatchCase, body: liftStatements(dispatchCase.body) })),
          ...(statement.defaultBody === undefined ? {} : { defaultBody: liftStatements(statement.defaultBody) }),
        };
        return [...prelude, rebuilt];
      }
      default:
        return [statement];
    }
  };

  const liftStatements = (statements: StatementAst[]): StatementAst[] => statements.flatMap(liftStatement);

  for (const entry of ast.entries) entry.body = liftStatements(entry.body);
  for (const proc of ast.procs) proc.body = liftStatements(proc.body);
  if (lifted > 0) {
    optimizations.push({
      name: "function-call-lifting",
      detail: `Hoisted ${lifted} nested function call(s) into temporaries to preserve the working stack.`,
    });
  }
}

function collectReachableProcNames(ast: ProgramAst): Set<string> {
  const procMap = new Map(ast.procs.map((proc) => [proc.name, proc]));
  const reachableProcs = new Set<string>();
  const procQueue: string[] = [];

  const enqueueCall = (name: string): void => {
    if (procMap.has(name) && !reachableProcs.has(name)) {
      reachableProcs.add(name);
      procQueue.push(name);
    }
  };

  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "call") enqueueCall(statement.block);
      for (const callee of statementExpressionCallees(statement)) enqueueCall(callee);
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "while") visit(statement.body);
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
  while (procQueue.length > 0) {
    const procName = procQueue.shift();
    if (procName === undefined) continue;
    const proc = procMap.get(procName);
    if (proc !== undefined) visit(proc.body);
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
    if (literal !== undefined) collectDisplayLiteralPreloadValues(literal, values);

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
      if (statement.kind === "halt" && statement.literal !== undefined) {
        collectDisplayLiteralPreloadValues(statement.literal, values);
      }
      if (statement.kind === "pause" || statement.kind === "halt") visitExpr(statement.expr);
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitExpr(statement.condition.left);
        visitExpr(statement.condition.right);
        visitStatements(statement.thenBody);
        if (statement.elseBody) visitStatements(statement.elseBody);
      }
      if (statement.kind === "loop") visitStatements(statement.body);
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
  return [...values].filter((value) => value !== "0" && value !== "1").sort((a, b) => estimateNumberCost(b) - estimateNumberCost(a));
}

function collectDisplayLiteralPreloadValues(literal: string, values: Set<string>): void {
  const literalProgram = displayLiteralProgram(literal);
  if (shouldUsePreloadedDisplayLiteral(literal)) {
    values.add(normalizeConstantLiteral(literal));
  }
  if (literalProgram?.kind === "kinv" && estimateNumberCost(literalProgram.digits) > 1) {
    values.add(normalizeConstantLiteral(literalProgram.digits));
  }
  if (literalProgram?.kind === "xor") {
    if (estimateNumberCost(literalProgram.left) > 1) values.add(normalizeConstantLiteral(literalProgram.left));
    if (estimateNumberCost(literalProgram.right) > 1) values.add(normalizeConstantLiteral(literalProgram.right));
  }
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
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitExpr(statement.condition.left);
        visitExpr(statement.condition.right);
        visitStatements(statement.thenBody);
        if (statement.elseBody) visitStatements(statement.elseBody);
      }
      if (statement.kind === "loop") visitStatements(statement.body);
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
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visitStatements(dispatchCase.body);
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visitStatements(entry.body);
  for (const proc of ast.procs) visitStatements(proc.body);
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
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitExpr(statement.condition.left);
        visitExpr(statement.condition.right);
        visitStatements(statement.thenBody);
        if (statement.elseBody) visitStatements(statement.elseBody);
      }
      if (statement.kind === "loop") visitStatements(statement.body);
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
  return count;
}

export function programHasLineCountForMask(ast: ProgramAst, maskName: string): boolean {
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
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitExpr(statement.condition.left);
        visitExpr(statement.condition.right);
        visitStatements(statement.thenBody);
        if (statement.elseBody) visitStatements(statement.elseBody);
      }
      if (statement.kind === "loop") visitStatements(statement.body);
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
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitExpr(statement.condition.left);
        visitExpr(statement.condition.right);
        visitStatements(statement.thenBody);
        if (statement.elseBody) visitStatements(statement.elseBody);
      }
      if (statement.kind === "loop") visitStatements(statement.body);
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
      if (statement.kind === "return_value") visitExpr(statement.expr);
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
    case "dispatch":
      return expressionReferencesIdentifier(statement.expr, name) ||
        statement.cases.some((dispatchCase) =>
          expressionReferencesIdentifier(dispatchCase.value, name) || statementsReadIdentifier(dispatchCase.body, name)
        ) ||
        (statement.defaultBody !== undefined && statementsReadIdentifier(statement.defaultBody, name));
    case "show":
    case "input":
    case "call":
    case "decimal_series":
      return false;
    case "core":
      return statement.inputs?.some((input) => expressionReferencesIdentifier(input.expr, name)) ?? false;
    case "return_value":
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
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
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
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
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
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitCondition(statement.condition);
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "loop") visit(statement.body);
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
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
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

export function programUsesDashedCoordReport(ast: ProgramAst): boolean {
  return ast.displays.some((display) => dashedCoordReportDisplayTemplate(display) !== undefined);
}

export function dashedCoordReportDisplayTemplate(
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

export function displayLoopOpcode(register: 0 | 1 | 2 | 3): number {
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

export type DisplayLiteralProgram =
  | { kind: "error" }
  | { kind: "kinv"; digits: string; negative: boolean }
  | { kind: "xor"; left: string; right: string; negative: boolean };

export interface FirstSpliceDisplayLiteralProgram {
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

export function displayLiteralProgram(text: string): DisplayLiteralProgram | undefined {
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

export function firstSpliceDisplayLiteralProgram(text: string): FirstSpliceDisplayLiteralProgram | undefined {
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

export function signedFirstSpliceDisplayLiteralProgram(text: string): FirstSpliceDisplayLiteralProgram | undefined {
  const normalized = normalizeDisplayLiteralText(text);
  if (!/^-[0-9]/u.test(normalized)) return undefined;
  const body = normalized.slice(1);
  const cells = displayLiteralCells(body);
  if (cells === undefined || cells.length === 0 || cells.length > 8) return undefined;
  return firstSpliceDisplayLiteralProgramFromCells(cells, displayLiteralPointExponent(body) ?? cells.length - 1, true);
}

export function exponentTailDisplayLiteralProgram(text: string): FirstSpliceDisplayLiteralProgram | undefined {
  const cells = displayLiteralCells(text);
  if (cells === undefined || cells.length !== 9) return undefined;
  const exponent = cells.at(-1);
  if (exponent === undefined || exponent < 0 || exponent > 9) return undefined;
  return firstSpliceDisplayLiteralProgramFromCells(cells.slice(0, 8), exponent, false);
}

export function shouldUsePreloadedDisplayLiteral(text: string): boolean {
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

export function decimalDisplayLiteralNumber(text: string): string | undefined {
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

export function zeroDigitTailDisplayProgram(text: string): { input: number } | undefined {
  const cells = displayLiteralCells(text);
  if (cells === undefined || cells.length !== 2) return undefined;
  const [signDigit, tail] = cells;
  if (signDigit === undefined || signDigit < 2 || signDigit > 9 || tail !== 14) return undefined;
  return { input: signDigit - 1 };
}

export function signDigitLiteralDisplayProgram(text: string): SignDigitLiteralDisplayProgram | undefined {
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
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitExpr(statement.condition.left);
        visitExpr(statement.condition.right);
        visitStatements(statement.thenBody);
        if (statement.elseBody) visitStatements(statement.elseBody);
      }
      if (statement.kind === "loop") visitStatements(statement.body);
      if (statement.kind === "dispatch") {
        visitExpr(statement.expr);
        for (const dispatchCase of statement.cases) visitStatements(dispatchCase.body);
        if (statement.defaultBody) visitStatements(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visitStatements(entry.body);
  for (const proc of ast.procs) visitStatements(proc.body);
  return found;
}

export function normalizeConstantLiteral(raw: string): string {
  const value = Number(raw);
  return Number.isFinite(value) ? String(value) : raw.trim();
}

export function negatedNumberLiteral(raw: string): string {
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
      if (statement.kind === "assign" || statement.kind === "input") {
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
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
}

function collectAssignedVariables(ast: ProgramAst, variables: Set<string>): void {
  const ephemeralInputs = collectEphemeralInputTargets(ast);
  const visit = (statements: StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "assign") {
        variables.add(statement.target);
      }
      if (statement.kind === "input" && !ephemeralInputs.has(statement.target)) variables.add(statement.target);
      if (statement.kind === "loop") visit(statement.body);
      if (statement.kind === "while") visit(statement.body);
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
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
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
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
  return targets;
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
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
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
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
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
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
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
      if (statement.kind === "if") {
        visitCondition(statement.condition);
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "loop") visit(statement.body);
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
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        if (!membershipConditionHandledBeforeGenericBitHas(statement)) {
          visitCondition(statement.condition);
        }
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "loop") visit(statement.body);
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
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitExpr(statement.condition.left);
        visitExpr(statement.condition.right);
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "loop") visit(statement.body);
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
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitExpr(statement.condition.left);
        visitExpr(statement.condition.right);
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "loop") visit(statement.body);
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
      if (statement.kind === "assign") visitExpr(statement.expr);
      if (statement.kind === "if") {
        visitExpr(statement.condition.left);
        visitExpr(statement.condition.right);
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "loop") visit(statement.body);
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
      if (statement.kind === "dispatch") {
        for (const dispatchCase of statement.cases) visit(dispatchCase.body);
        if (statement.defaultBody) visit(statement.defaultBody);
      }
    }
  };
  for (const entry of ast.entries) visit(entry.body);
  for (const proc of ast.procs) visit(proc.body);
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

export function ticTacToeMaskScratchName(statement: StatementAst): string {
  return `${TICTACTOE_MASK_SCRATCH_PREFIX}${statement.line}`;
}

export function dispatchExpressionRegister(
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
export function dispatchUsesNumericResidualChain(statement: Extract<StatementAst, { kind: "dispatch" }>): boolean {
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
  return residual <= ordinary;
}

export function isZeroExpression(expr: ExpressionAst): boolean {
  return expr.kind === "number" && Number(expr.raw) === 0;
}

export function isUnitDecrementExpression(target: string, expr: ExpressionAst): boolean {
  return expr.kind === "binary" &&
    expr.op === "-" &&
    expr.left.kind === "identifier" &&
    expr.left.name === target &&
    expr.right.kind === "number" &&
    Number(expr.right.raw) === 1;
}

export function matchResidualGuardedUpdate(
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

export function decrementBranchTestsZero(condition: ConditionAst, target: string): boolean {
  return condition.left.kind === "identifier" &&
    condition.left.name === target &&
    (condition.op === "<=" || condition.op === "==") &&
    isZeroExpression(condition.right);
}

export function flOpcode(register: RegisterName): number | undefined {
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

export function isSimpleStackLoad(expr: ExpressionAst): boolean {
  return expr.kind === "identifier" || expr.kind === "number";
}

export function canTestAgainstZeroDirectly(op: ConditionAst["op"]): boolean {
  return op === "==" || op === "!=" || op === ">=" || op === "<";
}

export function directTestOpcode(op: ConditionAst["op"]): number {
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

export function selectCheaperEquivalentCondition(
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

export function conditionCompileCost(condition: ConditionAst, preloadedConstants?: ReadonlySet<string>): number {
  if (isZeroExpression(condition.right) && canTestAgainstZeroDirectly(condition.op)) {
    return estimateExpressionCostForCondition(condition.left, preloadedConstants) + 2;
  }
  return estimateExpressionCostForCondition(condition.left, preloadedConstants) +
    estimateExpressionCostForCondition(condition.right, preloadedConstants) +
    3;
}

export function estimateSmallSetConditionCost(
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

export function conditionToText(condition: ConditionAst): string {
  return `${expressionToIntentText(condition.left)} ${condition.op} ${expressionToIntentText(condition.right)}`;
}

export function expressionToIntentText(expr: ExpressionAst): string {
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

export function buildDiagnostic(
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
  const sharedTailNames = new Set<string>();
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

export function buildBranchRemovalCandidate(
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

export function buildGuardedUpdateSelectorCandidate(
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

export function maskedGuardedUpdateExpression(update: GuardedUpdate, selector: ExpressionAst): ExpressionAst {
  const current: ExpressionAst = { kind: "identifier", name: update.target };
  const delta = multiplyExpressions(update.delta, selector);
  return update.op === "+"
    ? addExpressions(current, delta)
    : subtractExpressions(current, delta);
}

export function buildDoubleClampCandidate(
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

export function matchNegativeZeroThresholdCondition(
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

export function isTicTacToeMacroName(name: string): boolean {
  return ticTacToeMacroArity(name) !== undefined;
}

export function ticTacToeMacroArity(name: string): number | undefined {
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

export function ticTacToeExpressionMacro(name: string, args: ExpressionAst[]): ExpressionAst | undefined {
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

export interface NearAnyHelperConditionMatch {
  value: ExpressionAst;
  radius: ExpressionAst;
  candidates: ExpressionAst[];
  op: ">=" | "<";
}

export interface NearAnyHelperStats {
  candidateCount: number;
  conditionCount: number;
  ordinaryCost: number;
  helperCallCost: number;
  helperCost: number;
}

export function isSmallSetMacroName(name: string): name is SmallSetMacroName {
  return name === "near_any" || name === "eq_any";
}

export function smallSetMacroArityOk(name: SmallSetMacroName, argCount: number): boolean {
  return name === "near_any" ? argCount >= 3 : argCount >= 2;
}

export function smallSetMacroArityText(name: SmallSetMacroName): string {
  return name === "near_any" ? "at least three arguments" : "at least two arguments";
}

export function smallSetExpressionMacro(name: string, args: ExpressionAst[]): ExpressionAst | undefined {
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

export function matchSmallSetCondition(condition: ConditionAst): SmallSetConditionMatch | undefined {
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

export function matchNearAnyHelperCondition(condition: ConditionAst): NearAnyHelperConditionMatch | undefined {
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

export function nearAnyHelperKey(value: ExpressionAst, radius: ExpressionAst): string {
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

export interface BitMembershipCondition {
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

export function matchCellHelperCall(expr: ExpressionAst, names: readonly CellHelperName[]): CellHelperCall | undefined {
  if (expr.kind !== "call" || !names.includes(expr.callee.toLowerCase() as CellHelperName) || expr.args.length !== 3) return undefined;
  return {
    mask: expr.args[0]!,
    x: expr.args[1]!,
    y: expr.args[2]!,
  };
}

export function matchBitSetAssignment(statement: Extract<StatementAst, { kind: "assign" }>): BitSetAssignment | undefined {
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

export function bitMaskScratchName(statement: StatementAst): string {
  return `${BIT_MASK_SCRATCH_PREFIX}${statement.line}`;
}

export function ifSelectorScratchName(statement: StatementAst): string {
  return `${IF_SELECTOR_SCRATCH_PREFIX}${statement.line}`;
}

export function matchBitMembershipCondition(condition: ConditionAst): BitMembershipCondition | undefined {
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

export function matchBitAbsenceCondition(condition: ConditionAst): BitMembershipCondition | undefined {
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

export function isBitClearAssignment(
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
export function matchSingleBitMaskOpAssignment(
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

export function cellMaskExpression(x: ExpressionAst, y: ExpressionAst): ExpressionAst {
  return addExpressions(
    pow10Expression(x),
    { kind: "call", callee: "int", args: [multiplyExpressions(pow10Expression(y), numberExpression(0.22600029))] },
  );
}

export interface RandomCoordListPlacement {
  listName: string;
  xMin: number;
  width: number;
  yMin: number;
  height: number;
  count: number;
}

export function isZeroOriginTenByTenPlacement(placement: RandomCoordListPlacement): boolean {
  return placement.xMin === 0 && placement.yMin === 0 && placement.width === 10 && placement.height === 10;
}

export function randomCoordListItemPlacement(fieldName: string, expr: ExpressionAst): RandomCoordListPlacement | undefined {
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

export function randomCoordListSetupFields(
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

export function multiplyExpressions(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  if (isNumericValue(left, 0) || isNumericValue(right, 0)) return numberExpression(0);
  if (isNumericValue(left, 1)) return right;
  if (isNumericValue(right, 1)) return left;
  return { kind: "binary", op: "*", left, right };
}

function productExpressions(expressions: ExpressionAst[]): ExpressionAst {
  return expressions.reduce((product, expr) => multiplyExpressions(product, expr));
}

export function addExpressions(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  if (isNumericValue(left, 0)) return right;
  if (isNumericValue(right, 0)) return left;
  return { kind: "binary", op: "+", left, right };
}

export function subtractExpressions(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  if (isNumericValue(right, 0)) return left;
  if (isNumericValue(left, 0)) return { kind: "unary", op: "-", expr: right };
  return { kind: "binary", op: "-", left, right };
}

export function divideExpressions(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
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

export function intExpression(expr: ExpressionAst): ExpressionAst {
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

export function spatialCountExpression(
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

export function boardForCellMask(mask: ExpressionAst, ast: ProgramAst): V2BoardAst | undefined {
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

export interface SpatialLineProgression {
  startOffset: ExpressionAst;
  step: ExpressionAst;
  count: number;
}

export function spatialLineProgressions(board: V2BoardAst, cell: ExpressionAst): SpatialLineProgression[] {
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

export function spatialNeighborProgressions(board: V2BoardAst | undefined): SpatialLineProgression[] {
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

export function offsetExpressionAst(expr: ExpressionAst, offset: number): ExpressionAst {
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

export function spatialHitScratchName(mask: string): string {
  return `${SPATIAL_HIT_SCRATCH_PREFIX}${mask}`;
}

export function spatialCountScratchNames(): string[] {
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

export function spatialCountMaskScratchName(): string {
  return `${SPATIAL_COUNT_SCRATCH_PREFIX}mask`;
}

export function spatialCountStepScratchName(): string {
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

export function matchRemainderByConstant(
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

export function numberExpression(value: number): ExpressionAst {
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

export function expressionEquals(left: ExpressionAst, right: ExpressionAst): boolean {
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

export function expressionReferencesIdentifier(expr: ExpressionAst, name: string): boolean {
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
export function isPureExpression(expr: ExpressionAst): boolean {
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

export function matchIntOrFracCall(expr: ExpressionAst): { fn: "int" | "frac"; arg: ExpressionAst } | undefined {
  if (expr.kind !== "call" || expr.args.length !== 1) return undefined;
  const name = expr.callee.toLowerCase();
  if (name !== "int" && name !== "frac") return undefined;
  return { fn: name, arg: expr.args[0]! };
}

export function matchStackUnaryDerivationCall(expr: ExpressionAst): StackUnaryDerivationCall | undefined {
  if (expr.kind !== "call" || expr.args.length !== 1) return undefined;
  const name = expr.callee.toLowerCase();
  if (!isStackUnaryDerivationFn(name)) return undefined;
  const [opcode, mnemonic] = STACK_UNARY_DERIVATION_OPCODES[name];
  return { fn: name, arg: expr.args[0]!, opcode, mnemonic };
}

function isStackUnaryDerivationFn(name: string): name is StackUnaryDerivationFn {
  return Object.prototype.hasOwnProperty.call(STACK_UNARY_DERIVATION_OPCODES, name);
}

export function optimizeDispatchDefaultCases(
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
  const delta = previous === undefined ? value : value - previous;
  return delta === 0 ? 0 : estimateNumberCost(String(Math.abs(delta))) + 1;
}

export function statementListsEqual(left: readonly StatementAst[], right: readonly StatementAst[]): boolean {
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
    case "return_value":
      return expressionEquals(left.expr, (right as typeof left).expr);
    case "decimal_series":
      return left.digits === (right as typeof left).digits &&
        left.counterStart === (right as typeof left).counterStart;
  }
}

function statementListOptionEquals(left: StatementAst[] | undefined, right: StatementAst[] | undefined): boolean {
  if (left === undefined || right === undefined) return left === right;
  return statementListsEqual(left, right);
}

function rawLinesEqual(left: readonly { text: string }[], right: readonly { text: string }[]): boolean {
  return left.length === right.length && left.every((line, index) => line.text === right[index]!.text);
}

export function countIdentifierReadsInCondition(condition: ConditionAst, name: string): number {
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

export function substituteConditionIdentifier(condition: ConditionAst, name: string, replacement: ExpressionAst): ConditionAst {
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

export function expressionPureForSubstitution(expr: ExpressionAst): boolean {
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

export function isNumericValue(expr: ExpressionAst, value: number): boolean {
  const parsed = numericLiteralValue(expr);
  return parsed !== undefined && parsed === value;
}

export function numericLiteralValue(expr: ExpressionAst): number | undefined {
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
    case "return_value":
      return estimateExpressionCost(statement.expr) + 1;
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
      return statement.lines.length;
    case "decimal_series":
      return 64;
    case "while":
    case "loop":
    case "dispatch":
      return Number.POSITIVE_INFINITY;
  }
}

export function estimateOrdinaryIfCost(statement: Extract<StatementAst, { kind: "if" }>, ast: ProgramAst): number {
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

export function estimateOrdinaryGuardedUpdateCost(statement: Extract<StatementAst, { kind: "if" }>, ast: ProgramAst): number {
  if (statement.elseBody !== undefined || statement.thenBody.length === 0) return Number.POSITIVE_INFINITY;
  let bodyCost = 0;
  for (const inner of statement.thenBody) {
    const cost = estimateSimpleStatementCost(inner, ast);
    if (!Number.isFinite(cost)) return Number.POSITIVE_INFINITY;
    bodyCost += cost;
  }
  return estimateConditionCost(statement.condition, ast) + bodyCost;
}

export function estimateGuardedUpdateSelectorCost(candidate: GuardedUpdateSelectorCandidate, scratch: string): number {
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

export function estimateConditionCost(
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

export function estimateNegativeZeroThresholdFlowCost(
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

export function estimateExpressionCost(expr: ExpressionAst): number {
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

export function estimateNumberCost(raw: string): number {
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

export function residualAdjustmentCost(previousValue: number, nextValue: number): number {
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
  return [...synthetic, ...constants, ...displayTemplateMasks];
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
  compileSetupProgramWithPreloads(setupContext, executablePreloads, fields);
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

export interface ExecutableSetupPreload {
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
    ast.domains.length > 0 ||
    ast.states.length > 0 ||
    ast.displays.length > 0 ||
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
  }
  return false;
}

function countIntentNodes(ast: ProgramAst): number {
  return (
    countV2IntentNodes(ast) +
    ast.domains.reduce((sum, domain) => sum + 1 + domain.lines.length, 0) +
    ast.states.reduce((sum, state) => sum + 1 + state.fields.length, 0) +
    ast.displays.length +
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
    if (statement.kind === "dispatch") {
      count += statement.cases.reduce((sum, dispatchCase) => sum + countStatements(dispatchCase.body), 0);
      if (statement.defaultBody) count += countStatements(statement.defaultBody);
    }
  }
  return count;
}

export function selectDispatchCandidate(
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

export function orderRawInputs(
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

export function formatRawContractDetail(statement: Extract<StatementAst, { kind: "core" }>): string {
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

export function parseRawInstruction(
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

export function binaryOpcode(op: "+" | "-" | "*" | "/"): number {
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
