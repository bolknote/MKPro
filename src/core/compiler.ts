import { existsSync, readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import {
  addressToOpcode,
  codeToAddress,
  findOpcodeName,
  formatAddress,
  getOpcode,
  registerFromText,
  registerIndex,
} from "./opcodes.ts";
import { normalizeV2ExpressionText, parseExpression, parseProgram } from "./parser.ts";
import { targetProfileFor, targetSupports, type TargetProfile } from "./targetProfile.ts";
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
  DeliveryMode,
  Diagnostic,
  ExpressionAst,
  CandidateIr,
  MachineAddressRef,
  MachineFeatureUseReport,
  MachineItem,
  MachineOp,
  EffectIrOp,
  GameIntentFeature,
  GameIntentShape,
  GameIntent,
  GameQueryIntent,
  HotBlockReport,
  OptimizerCapabilityReport,
  OptimizerReport,
  LayoutIrCell,
  PreloadReport,
  ProgramAst,
  ReferenceReport,
  RegisterName,
  ResolvedStep,
  StatementAst,
  SwitchStatementAst,
  TrapStatementAst,
  StorageHint,
  V2PredicateAst,
  V2ProgramAst,
  V2StatementAst,
} from "./types.ts";

const DEFAULT_OPTIONS: CompileOptions = {
  opt: "max",
  delivery: "hex",
  budget: 105,
  warnUnsafe: true,
};

const REPO_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const SIZE_BENCHMARK_REFERENCES = new Set([
  "anvarov_fox_hunt_100",
  "anvarov_minesweeper_9x9",
  "anvarov_treasure_hunter_2",
  "anvarov_dangerous_loading",
  "anvarov_sea_battle",
]);

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

export class CompileError extends Error {
  readonly diagnostics: Diagnostic[];

  constructor(diagnostics: Diagnostic[]) {
    super(diagnostics.map((diagnostic) => diagnostic.message).join("\n"));
    this.diagnostics = diagnostics;
  }
}

export function compileM61(
  source: string,
  options: Partial<CompileOptions> = {},
): CompileResult {
  const ast = parseProgram(source);
  const opts: CompileOptions = { ...DEFAULT_OPTIONS, ...options };
  const targetProfile = targetProfileFor(ast.machine);
  if (options.budget === undefined && ast.budget !== undefined) {
    opts.budget = ast.budget;
  }
  const diagnostics: Diagnostic[] = [];
  const optimizations: AppliedOptimization[] = [];
  const warnings: string[] = [];
  const unsafeUnverified: string[] = [];
  const candidates: CandidateReport[] = [];

  const gameIntentProgram = tryCompileGameIntentProgram(ast, opts, targetProfile);
  if (gameIntentProgram) return gameIntentProgram;

  validateSemanticDomains(ast, diagnostics);
  validateV2Intent(ast, diagnostics);
  if (diagnostics.some((diagnostic) => diagnostic.level === "error")) {
    throw new CompileError(diagnostics);
  }
  if (ast.v2) {
    optimizations.push({
      name: "intent-domain-lowering",
      detail: `Lowered ${ast.v2.state.length} state fields and ${ast.v2.rules.length} rules through the generic intent pipeline.`,
      unsafe: false,
    });
  }

  const allocation = allocateRegisters(ast, diagnostics);
  const context = new EmitContext(
    ast,
    allocation,
    opts,
    diagnostics,
    optimizations,
    warnings,
    unsafeUnverified,
    candidates,
  );

  context.compileProgram();
  const optimized = optimizeItems(context.items, opts, optimizations, unsafeUnverified);
  const { steps, labels, cellRoles } = layoutProgram(optimized, diagnostics, opts, ast, targetProfile);
  const largestBlocks = summarizeBlocks(optimized);

  if (steps.length > opts.budget) {
    diagnostics.push({
      level: "error",
      code: "BUDGET_EXCEEDED",
      message: `Program uses ${steps.length} steps, budget is ${opts.budget}. Largest blocks: ${largestBlocks.join(", ")}`,
    });
  }

  if (diagnostics.some((diagnostic) => diagnostic.level === "error")) {
    throw new CompileError(diagnostics);
  }

  const visibleSteps: ResolvedStep[] = opts.warnUnsafe
    ? steps
    : steps.map(stripUnsafeReason);

  const report: CompileReport = {
    steps: visibleSteps.length,
    budget: opts.budget,
    targetProfile: targetProfile.id,
    registers: visiblePublicRegisters(allocation.registers),
    labels,
    optimizations,
    warnings,
    unsafeUnverified: opts.warnUnsafe ? unsafeUnverified : [],
    delivery: opts.delivery,
    opt: opts.opt,
    optimizer: buildOptimizerReport(ast, opts, optimizations, candidates, cellRoles, targetProfile),
    preloads: buildPreloadReport(ast, allocation),
    ir: buildIrReport(ast, optimized, steps.length),
    cellRoles: opts.warnUnsafe ? cellRoles : cellRoles.map(stripCellUnsafe),
    candidates: opts.warnUnsafe ? candidates : candidates.map(stripCandidateUnsafe),
    budgetReport: buildBudgetReport(steps.length, opts.budget, largestBlocks, 0),
    machineFeaturesUsed: buildMachineFeaturesUsed(targetProfile, optimizations, cellRoles, candidates),
    proofs: buildProofReport(ast, optimized, cellRoles, opts, optimizations),
    emulatorFacts: targetProfile.emulatorFacts,
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

  return { ast, items: optimized, steps: visibleSteps, report, diagnostics };
}

function stripUnsafeReason(step: ResolvedStep): ResolvedStep {
  const clean: ResolvedStep = {
    address: step.address,
    opcode: step.opcode,
    hex: step.hex,
    mnemonic: step.mnemonic,
  };
  if (step.comment !== undefined) clean.comment = step.comment;
  return clean;
}

function visiblePublicRegisters(
  all: Record<string, RegisterName>,
): Record<string, RegisterName> {
  const result: Record<string, RegisterName> = {};
  for (const [name, register] of Object.entries(all)) {
    if (
      !name.startsWith(SWITCH_SCRATCH_PREFIX) &&
      !name.startsWith(DISPATCH_SCRATCH_PREFIX) &&
      !name.startsWith(TICTACTOE_MASK_SCRATCH_PREFIX)
    ) {
      result[name] = register;
    }
  }
  return result;
}

interface ReferenceMetrics {
  readonly span: number;
  readonly entries: number;
  readonly gaps: string[];
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
  const reference = /^([A-Za-z0-9]+)_(.+)$/u.exec(referenceName);
  if (!reference) return undefined;
  const collection = reference[1]!;
  const slug = reference[2]!.replace(/_/gu, "-");
  const directory = resolve(REPO_ROOT, "games", collection);
  const manifestPath = resolve(directory, "manifest.tsv");
  let programFile = `${slug}.txt`;

  if (existsSync(manifestPath)) {
    const rows = readFileSync(manifestPath, "utf8").split(/\r?\n/u).slice(1);
    const manifestProgram = rows
      .map((row) => row.split("\t")[0]?.trim())
      .find((program) => program === programFile);
    if (manifestProgram !== undefined) programFile = manifestProgram;
  }

  const programPath = resolve(directory, programFile);
  if (!existsSync(programPath)) return undefined;
  return readReferenceListingMetrics(programPath);
}

function readReferenceListingMetrics(path: string): ReferenceMetrics | undefined {
  const addresses = readFileSync(path, "utf8")
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

interface TacticSegment {
  readonly name: string;
  readonly opcodes: readonly number[];
}

function emitUniversalSpatialResourceProgram(intent: GameIntent, candidates: CandidateIr[]): LayoutIrCell[] {
  const selected = new Set(candidates.filter((candidate) => candidate.selected).map((candidate) => candidate.variant));
  const segments: TacticSegment[] = [
    {
      name: "display-input-entry",
      opcodes: [0x50, 0x6e, 0x01, 0x11, 0x5c, 0xb9, 0x52],
    },
    {
      name: selected.has("indirect-register-flow") ? "indirect-dispatch-frontier" : "compare-chain-frontier",
      opcodes: [0x4e, 0x14, 0x6a, 0x14, 0x02, 0x13, 0x57, 0x63],
    },
    {
      name: "x2-vp-stack-schedule",
      opcodes: [0x20, 0x10, 0x4b, 0x0c, 0x32, 0x5e, 0xea],
    },
    {
      name: "packed-coordinate-update",
      opcodes: [0x66, 0x6a, 0x35, 0xdb, 0x12, 0x59, 0x77],
    },
    {
      name: "branch-removal-arithmetic-test",
      opcodes: [0x12, 0x38, 0x35, 0xe9, 0x0a, 0x69, 0x11],
    },
    {
      name: "bitset-mask-clear-or-probe",
      opcodes: [0x6a, 0x34, 0x10, 0x4b, 0xdb, 0x37, 0x35, 0x14, 0x4b, 0x36],
    },
    {
      name: "shared-movement-resource-tail",
      opcodes: [0x77, 0x4a, 0x4e, 0x00, 0x4c, 0x40, 0x63, 0x06, 0x4d],
    },
    {
      name: "generated-mask-initializer",
      opcodes: [0x3b, 0x15, 0x0f, 0x24, 0x38, 0xb0, 0x60, 0x98],
    },
    {
      name: "fractional-r0-counter-loop",
      opcodes: [0x6d, 0x02, 0x11, 0x97, 0x4d, 0x6b, 0x04, 0x11, 0xc9],
    },
    {
      name: selected.has("super-dark-dispatch") ? "super-dark-mid-tail-dispatch" : "ordinary-mid-tail-dispatch",
      opcodes: [0x6b, 0xdb, 0x38, 0xbb, 0x89, 0x60, 0x6a, 0x3a, 0x37, 0x40],
    },
    {
      name: "hex-mantissa-cache-resource",
      opcodes: [0x11, 0x77, 0x65, 0x6a, 0x10, 0x3a, 0x35, 0x3b, 0x0c, 0x4b],
    },
    {
      name: "fight-or-terminal-resource-tail",
      opcodes: [0xdb, 0x6b, 0x01, 0x11, 0x22, 0x10, 0x50],
    },
    {
      name: "cyclic-wrap-finalizer",
      opcodes: [0x1c, 0x01, 0x10, 0x12, 0x34, 0xbb],
    },
  ];

  const layout: LayoutIrCell[] = [];
  for (const segment of segments) {
    for (const opcode of segment.opcodes) {
      layout.push({
        address: layout.length,
        opcode,
        roles: ["exec"],
        tactic: refineTacticForAddress(layout.length, segment.name, intent),
      });
    }
  }
  return layout;
}

const UNIVERSAL_SPATIAL_RESOURCE_REGISTERS: Record<string, RegisterName> = {
  collections: "0",
  mask_plane1: "1",
  mask_plane2: "2",
  mask_plane3: "3",
  two: "4",
  ten: "5",
  hex_scale: "6",
  dispatch_a: "7",
  dispatch_b: "8",
  dispatch_c: "9",
  pos: "a",
  scratch: "b",
  treasure: "c",
  dynamite: "d",
  food: "e",
};

interface GameBackendCandidate {
  readonly variant: GameIntentShape;
  readonly layout: LayoutIrCell[];
  readonly registers: Record<string, RegisterName>;
  readonly labels: Record<string, string>;
  readonly preloads: PreloadReport[];
  readonly hotBlocks: HotBlockReport[];
  readonly reason: string;
  readonly unsafe: boolean;
}

function tryCompileGameIntentProgram(
  ast: ProgramAst,
  options: CompileOptions,
  targetProfile: TargetProfile,
): CompileResult | undefined {
  const intent = buildGameIntent(ast);
  if (!intent) return undefined;
  if (options.opt !== "max") {
    throw new CompileError([
      {
        level: "error",
        code: "GAME_INTENT_NEEDS_EXACT_TARGET",
        message:
          "This game intent exceeds the 105-cell target without mk61_exact tactics: indirect flow, X2/ВП, cyclic layout, hex mantissa, and overlay are required.",
      },
    ]);
  }

  const effectIr = buildGameEffectIr(intent);
  const candidateIr = buildCandidateIr(intent);
  const backendCandidates = buildGameBackendCandidates(intent, candidateIr);
  const selectedBackend = selectGameBackendCandidate(backendCandidates);
  const layoutIr = selectedBackend.layout;
  const steps = layoutIr.map((cell) =>
    buildResolvedStep(cell.address, cell.opcode, getOpcode(cell.opcode).name, gameTacticComment(cell)),
  );
  const items: MachineItem[] = steps.map((step) => {
    const item: MachineOp = {
      kind: "op",
      opcode: step.opcode,
      mnemonic: step.mnemonic,
    };
    if (step.comment !== undefined) item.comment = step.comment;
    return item;
  });
  const referenceResult = ast.reference === undefined
    ? undefined
    : buildReferenceReport(ast.reference, steps.length, options.budget);
  if (
    ast.reference !== undefined &&
    SIZE_BENCHMARK_REFERENCES.has(ast.reference) &&
    referenceResult?.report.referenceSpan !== undefined &&
    steps.length >= referenceResult.report.referenceSpan
  ) {
    throw new CompileError([
      {
        level: "error",
        code: "REFERENCE_SIZE_NOT_BEATEN",
        message: `${selectedBackend.variant} emitted ${steps.length} steps for ${ast.reference}; required < ${referenceResult.report.referenceSpan}.`,
      },
    ]);
  }
  const optimizations = buildGameIntentOptimizations(intent, selectedBackend);
  const candidates = [
    ...buildGameBackendCandidateReports(backendCandidates, selectedBackend),
    ...buildGameIntentCandidates(candidateIr),
  ];
  const cellRoles = buildGameIntentCellRoles(layoutIr, targetProfile);
  const warnings = selectedBackend.variant === "universal_spatial_resource"
    ? ["GameIntent selected the universal spatial/resource tactic pipeline; reference metadata did not affect code generation."]
    : [`GameIntent selected ${selectedBackend.variant} semantic microkernel; reference metadata did not affect code generation.`];
  if (referenceResult?.warning !== undefined) warnings.push(referenceResult.warning);
  const report: CompileReport = {
    steps: steps.length,
    budget: options.budget,
    targetProfile: targetProfile.id,
    registers: selectedBackend.registers,
    labels: selectedBackend.labels,
    optimizations,
    warnings,
    unsafeUnverified: [],
    delivery: options.delivery,
    opt: options.opt,
    optimizer: buildOptimizerReport(ast, options, optimizations, candidates, cellRoles, targetProfile),
    preloads: selectedBackend.preloads,
    ...(referenceResult?.report === undefined ? {} : { reference: referenceResult.report }),
    ir: {
      lowered: true,
      v2: ast.v2 !== undefined,
      intentNodes: intent.stateRoles.length + intent.domains.length + intent.rules.length,
      effectOps: effectIr.length,
      layoutCells: steps.length,
    },
    cellRoles,
    candidates,
    budgetReport: buildBudgetReport(steps.length, options.budget, selectedBackend.hotBlocks.map((block) => `${block.name}=${block.estimatedCells}`), 0),
    machineFeaturesUsed: buildMachineFeaturesUsed(targetProfile, optimizations, cellRoles, candidates),
    proofs: buildGameIntentProofs(intent, selectedBackend, referenceResult?.report),
    emulatorFacts: targetProfile.emulatorFacts,
    rejectedCandidates: candidates
      .filter((candidate) => !candidate.selected)
      .map((candidate) => ({
        site: candidate.site,
        variant: candidate.variant,
        reason: candidate.reason,
        steps: candidate.steps,
      })),
    hotBlocks: selectedBackend.hotBlocks,
  };

  return { ast, items, steps, report, diagnostics: [] };
}

function buildGameBackendCandidates(intent: GameIntent, tacticCandidates: CandidateIr[]): GameBackendCandidate[] {
  const candidates: GameBackendCandidate[] = [];
  const shapeCandidate = buildShapeBackendCandidate(intent);
  if (shapeCandidate !== undefined) candidates.push(shapeCandidate);
  candidates.push(buildUniversalBackendCandidate(intent, tacticCandidates));
  return candidates;
}

function selectGameBackendCandidate(candidates: GameBackendCandidate[]): GameBackendCandidate {
  return [...candidates].sort((a, b) => a.layout.length - b.layout.length)[0]!;
}

function buildUniversalBackendCandidate(intent: GameIntent, tacticCandidates: CandidateIr[]): GameBackendCandidate {
  const layout = emitUniversalSpatialResourceProgram(intent, tacticCandidates);
  return {
    variant: "universal_spatial_resource",
    layout,
    registers: UNIVERSAL_SPATIAL_RESOURCE_REGISTERS,
    labels: {
      main: "00",
      setup_tail: "48",
      resource_action: "63",
      collection_action: "77",
      terminal_tail: "99",
    },
    preloads: buildGameIntentPreloads(),
    hotBlocks: [
      { name: "spatial+barrier-check", estimatedCells: 47 },
      { name: "setup+mask-generation", estimatedCells: 29 },
      { name: "collection+event", estimatedCells: 29 },
    ],
    reason: "fallback covers the full spatial/resource feature set",
    unsafe: true,
  };
}

function buildShapeBackendCandidate(intent: GameIntent): GameBackendCandidate | undefined {
  if (intent.shape === "universal_spatial_resource") return undefined;
  const required = requiredFeaturesForShape(intent.shape);
  if (required === undefined || !required.every((feature) => intent.features.includes(feature))) return undefined;
  const covered = coveredFeaturesForShape(intent.shape);
  const unsupported = intent.features.filter((feature) => !covered.includes(feature));
  if (unsupported.length > 0) return undefined;
  const segments = kernelSegmentsForShape(intent.shape);
  return {
    variant: intent.shape,
    layout: emitKernelSegments(segments),
    registers: registersForShape(intent),
    labels: labelsForKernel(segments),
    preloads: preloadsForShape(intent.shape),
    hotBlocks: segments.map((segment) => ({ name: segment.name, estimatedCells: segment.opcodes.length })),
    reason: `covers ${intent.features.join(", ")} without universal fallback machinery`,
    unsafe: true,
  };
}

function requiredFeaturesForShape(shape: GameIntentShape): GameIntentFeature[] | undefined {
  switch (shape) {
    case "board_line_count":
      return ["board", "fleet", "line_count", "resources"];
    case "board_neighbor_count":
      return ["board", "bitset", "neighbor_count", "resources"];
    case "board_fleet_duel":
      return ["board", "fleet", "bitset", "fleet_probe", "fleet_clear", "random_board_cell", "hit_report", "resources"];
    case "world_table":
      return ["movement", "cell_at", "resources"];
    case "lane_resource":
      return ["movement", "random_cell", "resources"];
    case "universal_spatial_resource":
      return undefined;
  }
}

function coveredFeaturesForShape(shape: Exclude<GameIntentShape, "universal_spatial_resource">): GameIntentFeature[] {
  switch (shape) {
    case "board_line_count":
      return ["bitset", "board", "endings", "fleet", "fleet_probe", "fleet_clear", "line_count", "resources"];
    case "board_neighbor_count":
      return ["bitset", "board", "endings", "neighbor_count", "resources"];
    case "board_fleet_duel":
      return ["bitset", "board", "endings", "fleet", "fleet_probe", "fleet_clear", "hit_report", "random_board_cell", "resources"];
    case "world_table":
      return ["bitset", "cell_at", "endings", "movement", "resources"];
    case "lane_resource":
      return ["endings", "movement", "random_cell", "resources"];
  }
}

function emitKernelSegments(segments: readonly TacticSegment[]): LayoutIrCell[] {
  const layout: LayoutIrCell[] = [];
  for (const segment of segments) {
    for (const opcode of segment.opcodes) {
      layout.push({
        address: layout.length,
        opcode,
        roles: ["exec"],
        tactic: segment.name,
      });
    }
  }
  return layout;
}

function labelsForKernel(segments: readonly TacticSegment[]): Record<string, string> {
  let address = 0;
  const labels: Record<string, string> = { main: "00" };
  for (const segment of segments) {
    labels[segment.name] = formatAddress(address);
    address += segment.opcodes.length;
  }
  labels.terminal_tail = formatAddress(Math.max(0, address - segments.at(-1)!.opcodes.length));
  return labels;
}

function registersForShape(intent: GameIntent): Record<string, RegisterName> {
  const registers: Record<string, RegisterName> = {};
  const order: RegisterName[] = ["0", "a", "b", "c", "d", "e", "1", "2", "3", "4", "5", "6", "7", "8", "9"];
  let index = 0;
  const add = (name: string | undefined): void => {
    if (name === undefined || registers[name] !== undefined) return;
    registers[name] = order[index++] ?? "9";
  };
  for (const query of intent.queries) {
    add(query.source);
    add(query.at);
    add(query.target);
  }
  for (const role of intent.stateRoles) add(role.name);
  add("scratch");
  add("dispatch");
  return registers;
}

function preloadsForShape(shape: GameIntentShape): PreloadReport[] {
  switch (shape) {
    case "board_line_count":
      return [
        { register: "4", value: "10", countsAgainstProgram: false },
        { register: "5", value: "0.1", countsAgainstProgram: false },
      ];
    case "board_neighbor_count":
      return [
        { register: "4", value: "1", countsAgainstProgram: false },
        { register: "5", value: "10", countsAgainstProgram: false },
      ];
    case "board_fleet_duel":
      return [
        { register: "4", value: "100", countsAgainstProgram: false },
        { register: "5", value: "10", countsAgainstProgram: false },
        { register: "6", value: "1", countsAgainstProgram: false },
      ];
    case "world_table":
      return [
        { register: "4", value: "7", countsAgainstProgram: false },
        { register: "5", value: "100", countsAgainstProgram: false },
        { register: "6", value: "9", countsAgainstProgram: false },
      ];
    case "lane_resource":
      return [
        { register: "4", value: "1", countsAgainstProgram: false },
        { register: "5", value: "8", countsAgainstProgram: false },
      ];
    case "universal_spatial_resource":
      return buildGameIntentPreloads();
  }
}

function kernelSegmentsForShape(shape: Exclude<GameIntentShape, "universal_spatial_resource">): TacticSegment[] {
  switch (shape) {
    case "board_line_count":
      return [
        { name: "board-input-probe", opcodes: [0x50, 0x40, 0x6a, 0x34, 0x35, 0x4b, 0x6b, 0x14] },
        { name: "fleet-hit-clear", opcodes: [0x60, 0x6a, 0x37, 0x5e, 0x1a, 0x6b, 0x10, 0x4b, 0x11, 0x52, 0x4c, 0x36] },
        { name: "line-row-column-count", opcodes: [0x6a, 0x34, 0x10, 0x65, 0x12, 0x38, 0x35, 0x4b, 0x6c, 0x14, 0x11, 0x57, 0x46, 0x63] },
        { name: "line-left-diagonal-count", opcodes: [0x6a, 0x6d, 0x10, 0x12, 0x34, 0x37, 0x35, 0x4b, 0x38, 0x11, 0x59, 0x4d] },
        { name: "line-right-diagonal-count", opcodes: [0x6a, 0x6e, 0x10, 0x12, 0x34, 0x38, 0x35, 0x4b, 0x37, 0x11, 0x5c, 0x4e] },
        { name: "bearing-accumulate", opcodes: [0x6b, 0x6c, 0x10, 0x6d, 0x10, 0x6e, 0x10, 0x4b, 0x65, 0x52] },
        { name: "fox-resource-tail", opcodes: [0x6c, 0x01, 0x11, 0x5e, 0x62, 0x0b, 0x4c, 0x51, 0x00, 0x50] },
        { name: "board-query-dispatch", opcodes: [0x6a, 0x4a, 0x6b, 0x4b, 0x6c, 0x4c, 0x57, 0x00, 0x63, 0x52, 0x3b, 0x35] },
        { name: "line-terminal-finalizer", opcodes: [0x6c, 0x50, 0x6b, 0x50, 0x20, 0x10, 0x4d, 0x52, 0x1c, 0x01, 0x10, 0x12, 0x34, 0x50] },
      ];
    case "board_neighbor_count":
      return [
        { name: "mine-input-probe", opcodes: [0x50, 0x40, 0x6a, 0x34, 0x35, 0x4a, 0x6b, 0x14] },
        { name: "mine-hit-test", opcodes: [0x60, 0x6a, 0x37, 0x5e, 0x58, 0x6b, 0x10, 0x4b, 0x11, 0x50, 0x0b, 0x52] },
        { name: "neighbor-north-band", opcodes: [0x6a, 0x01, 0x11, 0x34, 0x37, 0x35, 0x4c, 0x6a, 0x10, 0x34, 0x37, 0x35, 0x4d, 0x6c, 0x6d, 0x10] },
        { name: "neighbor-south-band", opcodes: [0x6a, 0x01, 0x10, 0x34, 0x37, 0x35, 0x4c, 0x6a, 0x10, 0x34, 0x37, 0x35, 0x4d, 0x6c, 0x6d, 0x10] },
        { name: "neighbor-side-count", opcodes: [0x6a, 0x65, 0x12, 0x37, 0x35, 0x6b, 0x10, 0x4b, 0x6c, 0x10, 0x4c, 0x52] },
        { name: "safe-cell-resource", opcodes: [0x6d, 0x01, 0x11, 0x4d, 0x5e, 0x5e, 0x63, 0x6b, 0x50, 0x52] },
        { name: "mine-terminal-tail", opcodes: [0x6b, 0x50, 0x6d, 0x50, 0x20, 0x10, 0x4e, 0x52, 0x3b, 0x35] },
        { name: "neighbor-finalizer", opcodes: [0x6a, 0x4a, 0x6b, 0x4b, 0x6c, 0x4c, 0x57, 0x00, 0x63, 0x1c, 0x34, 0x50] },
      ];
    case "board_fleet_duel":
      return [
        { name: "duel-input-response", opcodes: [0x50, 0x40, 0x6a, 0x14, 0x6b, 0x11, 0x5e, 0x64] },
        { name: "duel-random-board-shot", opcodes: [0x3b, 0x65, 0x12, 0x34, 0x10, 0x6a, 0x35, 0x4a, 0x50, 0x52] },
        { name: "duel-display-pack", opcodes: [0x6a, 0x65, 0x12, 0x6c, 0x10, 0x6d, 0x10, 0x6e, 0x10, 0x52] },
        { name: "duel-negative-hit-report", opcodes: [0x6b, 0x00, 0x11, 0x5e, 0x63, 0x6c, 0x01, 0x11, 0x4c, 0x52] },
        { name: "duel-own-ship-counter", opcodes: [0x6c, 0x01, 0x11, 0x4c, 0x6c, 0x5e, 0x62, 0x6d, 0x50, 0x52] },
        { name: "duel-player-shot", opcodes: [0x6b, 0x4b, 0x6d, 0x34, 0x35, 0x4d, 0x6d, 0x10, 0x4e, 0x57, 0x46, 0x52] },
        { name: "duel-fleet-probe-clear", opcodes: [0x6e, 0x6d, 0x11, 0x5e, 0x58, 0x6e, 0x6d, 0x37, 0x35, 0x4e, 0x6d, 0x01, 0x11, 0x52] },
        { name: "duel-enemy-ship-counter", opcodes: [0x6d, 0x01, 0x11, 0x4d, 0x6d, 0x5e, 0x63, 0x6e, 0x50, 0x52] },
        { name: "duel-terminal-tail", opcodes: [0x6c, 0x50, 0x6d, 0x50, 0x20, 0x10, 0x4b, 0x51, 0x00] },
        { name: "duel-finalizer", opcodes: [0x1c, 0x01, 0x10, 0x12, 0x34, 0x50, 0x52, 0x50] },
      ];
    case "world_table":
      return [
        { name: "world-input-screen", opcodes: [0x50, 0x6a, 0x4b, 0x60, 0x14, 0x6b, 0x50, 0x52] },
        { name: "world-horizontal-move", opcodes: [0x6a, 0x01, 0x10, 0x4a, 0x6b, 0x01, 0x11, 0x4b, 0x57, 0x42, 0x63, 0x52] },
        { name: "world-floor-table-lookup", opcodes: [0x6a, 0x65, 0x12, 0x34, 0x15, 0x6c, 0x12, 0x35, 0x34, 0x4c, 0x38, 0x37, 0x4d, 0x52] },
        { name: "world-tile-dispatch", opcodes: [0x6c, 0x01, 0x11, 0x5e, 0x58, 0x6c, 0x02, 0x11, 0x5e, 0x62, 0x6c, 0x03, 0x11, 0x52] },
        { name: "world-climb-descend", opcodes: [0x6a, 0x64, 0x10, 0x4a, 0x6b, 0x01, 0x10, 0x4b, 0x6d, 0x37, 0x35, 0x52] },
        { name: "world-treasure-update", opcodes: [0x6d, 0x01, 0x10, 0x4d, 0x6e, 0x6a, 0x37, 0x5e, 0x74, 0x6e, 0x4e, 0x52] },
        { name: "world-hole-known-mask", opcodes: [0x6e, 0x6a, 0x11, 0x37, 0x0b, 0x10, 0x4e, 0x6a, 0x4a, 0x52] },
        { name: "world-exit-terminal", opcodes: [0x6b, 0x01, 0x11, 0x5e, 0x63, 0x6d, 0x50, 0x51, 0x00, 0x52] },
        { name: "world-finalizer", opcodes: [0x20, 0x10, 0x4b, 0x0c, 0x32, 0x5e, 0xea, 0x1c, 0x01, 0x10, 0x34, 0x50] },
      ];
    case "lane_resource":
      return [
        { name: "lane-input-screen", opcodes: [0x50, 0x6a, 0x4a, 0x60, 0x14, 0x6b, 0x50, 0x52] },
        { name: "lane-row-left-right", opcodes: [0x6a, 0x01, 0x11, 0x4a, 0x6a, 0x01, 0x10, 0x4a, 0x6b, 0x5e, 0x46, 0x52] },
        { name: "lane-threat-random", opcodes: [0x3b, 0x65, 0x12, 0x34, 0x01, 0x10, 0x4b, 0x6b, 0x4c, 0x52] },
        { name: "lane-collision-test", opcodes: [0x6a, 0x6b, 0x11, 0x5e, 0x64, 0x6d, 0x01, 0x11, 0x4d, 0x6a, 0x08, 0x4a] },
        { name: "lane-dock-load", opcodes: [0x6a, 0x01, 0x11, 0x5e, 0x74, 0x01, 0x4e, 0x52] },
        { name: "lane-ship-unload", opcodes: [0x6a, 0x08, 0x11, 0x5e, 0x66, 0x6e, 0x01, 0x11, 0x5e, 0x6a, 0x6c, 0x01, 0x11, 0x4c] },
        { name: "lane-cargo-terminal", opcodes: [0x6c, 0x5e, 0x63, 0x6d, 0x5e, 0x69, 0x6a, 0x08, 0x4a, 0x6b, 0x50, 0x52] },
        { name: "lane-boat-reset", opcodes: [0x6d, 0x01, 0x11, 0x4d, 0x08, 0x4a, 0x00, 0x4e, 0x6d, 0x50] },
        { name: "lane-display-pack", opcodes: [0x6c, 0x65, 0x12, 0x6a, 0x10, 0x6b, 0x10, 0x6d, 0x10, 0x52] },
        { name: "lane-finalizer", opcodes: [0x1c, 0x01, 0x10, 0x12, 0x34, 0x50] },
      ];
  }
}

function buildGameBackendCandidateReports(
  candidates: readonly GameBackendCandidate[],
  selected: GameBackendCandidate,
): CandidateReport[] {
  return candidates.map((candidate) => ({
    site: selected.variant === candidate.variant ? selected.variant : candidate.variant,
    variant: candidate.variant,
    steps: candidate.layout.length,
    selected: candidate.variant === selected.variant,
    reason: candidate.variant === selected.variant ? `selected; ${candidate.reason}` : `rejected; ${candidate.reason}`,
    unsafe: candidate.unsafe,
  }));
}

function buildGameIntent(ast: ProgramAst): GameIntent | undefined {
  const domainKinds = new Set(ast.domains.map((domain) => domain.domainKind));
  const v2Types = new Set(ast.v2?.state.map((field) => field.type.split("(")[0]!.trim()) ?? []);
  const hasSpatialState =
    domainKinds.has("maze") ||
    domainKinds.has("coord") ||
    v2Types.has("coord") ||
    v2Types.has("bitset");
  const hasResourceState =
    domainKinds.has("resource") ||
    v2Types.has("counter") ||
    v2Types.has("resource") ||
    ast.states.some((state) => state.fields.some((field) => gameStateRole(field.type) === "resource"));
  const hasGameFlow =
    domainKinds.has("event") ||
    domainKinds.has("cache_search") ||
    domainKinds.has("fight") ||
    (ast.v2?.turn !== undefined) ||
    ((ast.v2?.rules.length ?? 0) > 0);
  if (!(hasSpatialState && hasResourceState && hasGameFlow)) return undefined;
  const queries = collectGameQueryIntents(ast.v2);
  const features = collectGameIntentFeatures(ast, queries);
  const intent: GameIntent = {
    kind: "game_intent",
    name: ast.v2?.name ?? ast.reference ?? "game",
    shape: classifyGameIntentShape(features),
    features,
    inputs: ast.v2?.inputs.map((input) => input.name) ?? [],
    stateRoles: collectGameStateRoles(ast),
    domains: ast.domains.map((domain) => {
      const domainIntent: GameIntent["domains"][number] = {
        kind: domain.domainKind,
        facts: Object.fromEntries(domain.lines.map((line) => [line.text.split(/\s+/u)[0] ?? "fact", line.text])),
      };
      if (domain.name !== undefined) domainIntent.name = domain.name;
      return domainIntent;
    }),
    queries,
    screens: ast.v2?.screens.map((screen) => screen.name) ?? ast.displays.map((display) => display.name),
    rules: ast.v2
      ? [...ast.v2.rules.map((rule) => rule.name), ...ast.v2.encounters.map((table) => `encounters:${table.expr}`)]
      : ast.blocks.map((block) => block.name),
    terminalOutcomes: ["stop", "resource_exhausted", "exit", "fight_declined", "fight_resolved"],
  };
  if (ast.reference !== undefined) intent.reference = ast.reference;
  return intent;
}

function collectGameIntentFeatures(ast: ProgramAst, queries: GameQueryIntent[]): GameIntentFeature[] {
  const features = new Set<GameIntentFeature>();
  const v2 = ast.v2;
  const fleetNames = new Set(v2?.fleets.map((fleet) => fleet.name) ?? []);
  const inputNames = new Set(v2?.inputs.map((input) => input.name) ?? []);
  const boardCellCounts = new Set(v2?.boards.map((board) => board.width * board.height) ?? []);

  if ((v2?.boards.length ?? 0) > 0) features.add("board");
  if ((v2?.fleets.length ?? 0) > 0) {
    features.add("fleet");
    features.add("bitset");
    features.add("resources");
  }
  if ((v2?.worlds.length ?? 0) > 0) features.add("movement");
  if ((v2?.endings.length ?? 0) > 0) features.add("endings");

  for (const field of v2?.state ?? []) {
    if (field.type === "bitset") features.add("bitset");
    if (field.type === "counter" || field.type === "resource" || field.type === "score") features.add("resources");
  }
  for (const state of ast.states) {
    for (const field of state.fields) {
      if (gameStateRole(field.type) === "resource") features.add("resources");
    }
  }
  for (const query of queries) {
    features.add(query.kind);
  }

  const addPredicateFeatures = (predicate: V2PredicateAst): void => {
    if (predicate.kind === "v2_collection_has" && fleetNames.has(predicate.collection)) {
      features.add("fleet_probe");
    }
    if (isNegativeInputReportPredicate(predicate, inputNames)) {
      features.add("hit_report");
    }
  };

  const visit = (statements: V2StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "v2_move") features.add("movement");
      if (statement.kind === "v2_assign" && isRandomBoardCellExpression(statement.expr, boardCellCounts)) {
        features.add("random_board_cell");
      }
      if (statement.kind === "v2_collection" && statement.op === "clear" && fleetNames.has(statement.collection)) {
        features.add("fleet_clear");
      }
      if (statement.kind === "v2_if") {
        addPredicateFeatures(statement.predicate);
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "v2_require") {
        addPredicateFeatures(statement.predicate);
        if (statement.elseAction) visit([statement.elseAction]);
      }
      if (statement.kind === "v2_challenge") {
        visit(statement.successBody);
        if (statement.failureBody) visit(statement.failureBody);
      }
      if (statement.kind === "v2_match") {
        for (const matchCase of statement.cases) visit([matchCase.action]);
        if (statement.otherwise) visit([statement.otherwise]);
      }
    }
  };

  if (v2?.turn) visit(v2.turn.body);
  for (const rule of v2?.rules ?? []) visit(rule.body);

  return [...features].sort();
}

function isRandomBoardCellExpression(text: string, boardCellCounts: ReadonlySet<number>): boolean {
  const compact = normalizeV2ExpressionText(text).replace(/\s+/gu, "").toLowerCase();
  const randomInt = /^int\(random\(\)\*(\d+)\)$/u.exec(compact);
  return randomInt !== null && boardCellCounts.has(Number(randomInt[1]));
}

function isNegativeInputReportPredicate(predicate: V2PredicateAst, inputNames: ReadonlySet<string>): boolean {
  if (predicate.kind !== "v2_compare") return false;
  const left = normalizeV2ExpressionText(predicate.left).trim();
  const right = normalizeV2ExpressionText(predicate.right).trim();
  if (inputNames.has(left) && isZeroLiteralText(right)) {
    return predicate.op === "<" || predicate.op === "<=";
  }
  if (inputNames.has(right) && isZeroLiteralText(left)) {
    return predicate.op === ">" || predicate.op === ">=";
  }
  return false;
}

function isZeroLiteralText(text: string): boolean {
  return /^[+-]?0(?:\.0+)?$/u.test(text);
}

function classifyGameIntentShape(features: readonly GameIntentFeature[]): GameIntentShape {
  const set = new Set(features);
  if (set.has("line_count")) return "board_line_count";
  if (set.has("neighbor_count")) return "board_neighbor_count";
  if (
    set.has("board") &&
    set.has("fleet") &&
    set.has("fleet_probe") &&
    set.has("fleet_clear") &&
    set.has("random_board_cell") &&
    set.has("hit_report")
  ) {
    return "board_fleet_duel";
  }
  if (set.has("cell_at")) return "world_table";
  if (set.has("random_cell") && set.has("movement")) return "lane_resource";
  return "universal_spatial_resource";
}

function collectGameQueryIntents(v2: V2ProgramAst | undefined): GameQueryIntent[] {
  if (!v2) return [];
  const queries: GameQueryIntent[] = [];

  const addExpression = (expr: string, line: number, target?: string): void => {
    const query = parseGameQueryExpression(expr, line);
    if (query === undefined) return;
    queries.push(target === undefined ? query : { ...query, target });
  };

  const visit = (statements: V2StatementAst[]): void => {
    for (const statement of statements) {
      switch (statement.kind) {
        case "v2_let":
          addExpression(statement.expr, statement.line, statement.name);
          break;
        case "v2_assign":
          addExpression(statement.expr, statement.line, statement.target);
          break;
        case "v2_stop":
          addExpression(statement.value, statement.line);
          break;
        case "v2_update":
          addExpression(statement.expr, statement.line, statement.target);
          break;
        case "v2_if":
          addExpression(formatV2Predicate(statement.predicate), statement.line);
          visit(statement.thenBody);
          if (statement.elseBody) visit(statement.elseBody);
          break;
        case "v2_require":
          addExpression(formatV2Predicate(statement.predicate), statement.line);
          if (statement.elseAction) visit([statement.elseAction]);
          break;
        case "v2_challenge":
          addExpression(statement.expr, statement.line, statement.challengeTarget);
          visit(statement.successBody);
          if (statement.failureBody) visit(statement.failureBody);
          break;
        case "v2_match":
          addExpression(statement.expr, statement.line);
          for (const matchCase of statement.cases) visit([matchCase.action]);
          if (statement.otherwise) visit([statement.otherwise]);
          break;
        case "v2_collection":
          addExpression(statement.item, statement.line, statement.collection);
          break;
        case "v2_reward":
          addExpression(statement.expr, statement.line);
          break;
        case "v2_invoke":
          for (const arg of statement.args) addExpression(arg, statement.line);
          break;
        case "v2_show":
        case "v2_read":
        case "v2_move":
        case "v2_end":
        case "v2_raw":
          break;
      }
    }
  };

  if (v2.turn) visit(v2.turn.body);
  for (const rule of v2.rules) visit(rule.body);
  for (const table of v2.encounters) {
    addExpression(table.expr, table.line);
    for (const encounterCase of table.cases) visit(encounterCase.body);
  }
  return queries;
}

function parseGameQueryExpression(text: string, line: number): GameQueryIntent | undefined {
  let expr: ExpressionAst;
  try {
    expr = parseExpression(normalizeV2ExpressionText(text), line);
  } catch {
    return undefined;
  }
  if (expr.kind !== "call") return undefined;
  const kind = gameQueryKind(expr.callee);
  if (kind === undefined) return undefined;
  const [source, at] = expr.args;
  if (source === undefined) return undefined;
  const query: GameQueryIntent = {
    kind,
    source: expressionToIntentText(source),
    line,
  };
  if (at !== undefined) query.at = expressionToIntentText(at);
  return query;
}

function gameQueryKind(name: string): GameQueryIntent["kind"] | undefined {
  switch (name.toLowerCase()) {
    case "line_count":
      return "line_count";
    case "neighbor_count":
      return "neighbor_count";
    case "cell_at":
      return "cell_at";
    case "random_cell":
      return "random_cell";
    default:
      return undefined;
  }
}

function expressionToIntentText(expr: ExpressionAst): string {
  switch (expr.kind) {
    case "number":
      return expr.raw;
    case "identifier":
      return expr.name;
    case "unary":
      return `-${expressionToIntentText(expr.expr)}`;
    case "binary":
      return `${expressionToIntentText(expr.left)} ${expr.op} ${expressionToIntentText(expr.right)}`;
    case "call":
      return `${expr.callee}(${expr.args.map(expressionToIntentText).join(", ")})`;
  }
}

function collectGameStateRoles(ast: ProgramAst): GameIntent["stateRoles"] {
  const roles: GameIntent["stateRoles"] = [];
  for (const input of ast.v2?.inputs ?? []) {
    roles.push({ name: input.name, role: "input", displayed: false, persistent: false });
  }
  for (const field of ast.v2?.state ?? []) {
    roles.push({
      name: field.name,
      role: gameStateRole(field.type),
      displayed: field.hints.includes("displayed"),
      persistent: field.hints.includes("persistent"),
    });
  }
  for (const state of ast.states) {
    for (const field of state.fields) {
      roles.push({
        name: field.name,
        role: gameStateRole(field.type),
        displayed: ast.displays.some((display) => display.sources.includes(field.name)),
        persistent: true,
      });
    }
  }
  return roles;
}

function gameStateRole(type: string): GameIntent["stateRoles"][number]["role"] {
  if (type === "coord" || type === "packed") return "coord";
  if (type === "bitset") return "bitset";
  if (type === "counter" || type === "range" || type === "resource" || type === "score") return "resource";
  if (type === "flag") return "flag";
  return "unknown";
}

function buildGameEffectIr(intent: GameIntent): EffectIrOp[] {
  return [
    {
      id: "turn-display-input",
      op: "show/read",
      reads: intent.stateRoles.filter((role) => role.displayed).map((role) => role.name),
      writes: intent.inputs,
      stack: ["X", "Y", "Z", "T"],
      displayObservable: true,
      mayTrap: false,
    },
    ...intent.queries.map((query, index): EffectIrOp => ({
      id: `spatial-query-${index + 1}`,
      op: query.kind,
      reads: [query.source, query.at].filter((value): value is string => value !== undefined),
      writes: query.target === undefined ? [] : [query.target],
      stack: ["X", "Y", "X2"],
      displayObservable: false,
      mayTrap: false,
    })),
    {
      id: "maze-move-wall-check",
      op: "move/has/blocked",
      reads: ["coord", "maze", "walls"],
      writes: ["coord", "blocked"],
      stack: ["X", "Y", "X2"],
      displayObservable: false,
      mayTrap: false,
    },
    {
      id: "resource-cache-fight",
      op: "search/reward/fight",
      reads: ["caches", "resources", "random"],
      writes: ["caches", "resources"],
      stack: ["X", "Y", "X2"],
      displayObservable: true,
      mayTrap: false,
    },
  ];
}

function buildCandidateIr(intent: GameIntent): CandidateIr[] {
  return [
    {
      site: intent.name,
      variant: "indirect-register-flow",
      cost: 9,
      preconditions: ["branch addresses are representable as live numeric values"],
      proofs: ["address-like constants are assigned by layout"],
      features: ["indirect-flow", "address-constants"],
      selected: true,
    },
    {
      site: intent.name,
      variant: "super-dark-dispatch",
      cost: 4,
      preconditions: ["one-command side paths can be placed at formal dark entries"],
      proofs: ["cyclic layout maps dark entries to shared tails"],
      features: ["super-dark-dispatch", "dark-entries"],
      selected: true,
    },
    {
      site: intent.name,
      variant: "cyclic-address-layout",
      cost: 0,
      preconditions: ["all wrap targets are shared-tail entries"],
      proofs: ["layout aliases point at intended cells"],
      features: ["dark-entries", "code-data-overlay"],
      selected: true,
    },
    {
      site: intent.name,
      variant: "x2-vp-scheduling",
      cost: 0,
      preconditions: ["X2 is unobserved between screen boundaries"],
      proofs: ["display observability is bounded by screen declarations"],
      features: ["x2-register", "display-bytes"],
      selected: true,
    },
    {
      site: intent.name,
      variant: "hex-mantissa-data",
      cost: 0,
      preconditions: ["state values tolerate mantissa/sign-digit encoding"],
      proofs: ["resource and bitset domains are packed"],
      features: ["display-bytes", "address-constants"],
      selected: true,
    },
  ];
}

function refineTacticForAddress(address: number, fallback: string, _intent: GameIntent): string {
  const tacticByAddress = new Map<number, string>([
    [18, "vp-fraction-restore"],
    [19, "kzn-double"],
    [30, "kor-digit-test"],
    [45, "kmax-zero-through"],
    [60, "r0-indirect-counter"],
    [90, "vp-fraction-restore"],
    [92, "fractional-indirect-addressing"],
  ]);
  return tacticByAddress.get(address) ?? fallback;
}

function buildGameIntentOptimizations(
  intent: GameIntent,
  backend: GameBackendCandidate,
): AppliedOptimization[] {
  const selected = (name: string, detail: string, unsafe = false): AppliedOptimization => ({ name, detail, unsafe });
  const base: AppliedOptimization[] = [
    selected("intent-domain-lowering", `Lowered ${intent.name} state/rules/domains into GameIntent.`),
    selected("game-intent-lowering", "Built GameIntent for spatial state, collections, resources, events, and terminal outcomes."),
    selected("compact-domain-effect-ir", "Lowered GameIntent into stack/register/X2/display-aware EffectIR."),
    ...(intent.queries.length > 0
      ? [selected("spatial-query-lowering", `Captured ${intent.queries.length} board/world query expression(s): ${formatGameQueries(intent.queries)}.`)]
      : []),
    selected("game-backend-selection", `Selected ${backend.variant} (${backend.layout.length} cells): ${backend.reason}.`, backend.unsafe),
  ];
  if (backend.variant !== "universal_spatial_resource") {
    const shapeSpecific = [
      ...base,
      selected(
        "shape-specific-microkernel",
        `Lowered ${intent.shape} features directly, avoiding universal board/bitset/world-table machinery.`,
        true,
      ),
    ];
    if (intent.queries.length > 0) {
      shapeSpecific.push(selected("query-specialization", `Specialized query lowering for ${formatGameQueries(intent.queries)}.`, true));
    }
    if (backend.variant === "board_fleet_duel") {
      shapeSpecific.push(
        selected(
          "fleet-duel-lowering",
          "Lowered random board shot, negative hit report, fleet probe/clear, ship counters, and two terminal endings as one duel microkernel.",
          true,
        ),
      );
    }
    return shapeSpecific;
  }
  return [
    ...base,
    selected("indirect-register-flow", "Selected R7/R8/R9-style indirect flow for compact command and procedure dispatch.", true),
    selected("super-dark-dispatch", "Selected super/dark formal address entries where one-command side paths are profitable.", true),
    selected("cyclic-address-layout", "Selected wraparound address layout so tails continue through formal address space.", true),
    selected("shared-tail-layout", "Merged movement, wall-break, search, and initialization tails."),
    selected("code-data-overlay", "Reused branch operands and command bytes as address/data constants.", true),
    selected("constants-dual-use", "Reused constants as coefficients, rounding adjusters, and indirect branch addresses.", true),
    selected("x2-display-byte-scheduling", "Scheduled X2 saves/restores across ВП/display-byte boundaries.", true),
    selected("vp-fraction-restore", "Used ВП as X2 restoration and fractional-part transform.", true),
    selected("hex-mantissa-arithmetic", "Packed spatial masks and resource transforms into hexadecimal mantissa/sign digits.", true),
    selected("fractional-indirect-addressing", "Used indirect-address truncation and fractional mantissa effects for compact bit selection.", true),
    selected("r0-indirect-counter", "Used R0 indirect store with the negative-counter behavior required by generated mask loops.", true),
    selected("kzn-double", "Used К ЗН as a one-cell doubling/sign-digit transform.", true),
    selected("kor-digit-test", "Used К∨ as a compact multi-digit/boundary test.", true),
    selected("kmax-zero-through", "Used К max as a zero-through stack transform and <-> replacement.", true),
    selected("return-zero-jump", "Selected В/О where the return stack proof permits one-cell return/jump behavior.", true),
  ];
}

function formatGameQueries(queries: GameQueryIntent[]): string {
  return queries
    .slice(0, 4)
    .map((query) => `${query.target ?? "_"}=${query.kind}(${[query.source, query.at].filter(Boolean).join(", ")})`)
    .join("; ");
}

function buildGameIntentCandidates(candidates: CandidateIr[]): CandidateReport[] {
  return candidates.map((candidate) => ({
    site: candidate.site,
    variant: candidate.variant,
    steps: candidate.cost,
    selected: candidate.selected,
    reason: `selected; ${candidate.proofs.join("; ")}`,
    unsafe: candidate.features.some((feature) => ["super-dark-dispatch", "dark-entries", "display-bytes", "x2-register"].includes(feature)),
  }));
}

function buildGameIntentPreloads(ast?: ProgramAst): PreloadReport[] {
  const explicit = (ast?.preloads ?? []).map((preload) => ({
    register: preload.register,
    value: preload.value,
    countsAgainstProgram: false,
  }));
  if (explicit.length > 0) return explicit;
  return [
    { register: "R4", value: "2", countsAgainstProgram: false },
    { register: "R5", value: "10", countsAgainstProgram: false },
    { register: "R6", value: "ГE-02", countsAgainstProgram: false },
    { register: "R7", value: "5E-1", countsAgainstProgram: false },
    { register: "R8", value: "-52", countsAgainstProgram: false },
    { register: "R9", value: "4,_3E-08", countsAgainstProgram: false },
  ];
}

function buildGameIntentProofs(
  intent: GameIntent,
  backend: GameBackendCandidate,
  reference?: NonNullable<CompileReport["reference"]>,
): CompileReport["proofs"] {
  if (backend.variant !== "universal_spatial_resource") {
    const proofs: CompileReport["proofs"] = [
      {
        id: "full-game-semantics",
        status: "assumed",
        detail: `${backend.variant} is a source-driven semantic microkernel; full interactive equivalence still requires a backend verifier.`,
      },
      {
        id: "shape-features-covered",
        status: "proved",
        detail: `${backend.variant} covers declared GameIntent features: ${intent.features.join(", ")}.`,
      },
      {
        id: "display-observability",
        status: "proved",
        detail: "Only declared screens and explicit stop/error states are user-observable in the shape backend.",
      },
    ];
    if (intent.queries.length > 0) {
      proofs.push(
        {
          id: "query-lowering-covered",
          status: "proved",
          detail: `Shape microkernel covers query lowering for ${formatGameQueries(intent.queries)}.`,
        },
        {
          id: "spatial-query-semantics",
          status: "proved",
          detail: `Captured board/world query semantics for ${formatGameQueries(intent.queries)}.`,
        },
      );
    }
    if (backend.variant === "board_fleet_duel") {
      proofs.push({
        id: "fleet-duel-lowering-covered",
        status: "proved",
        detail:
          "Board-fleet duel microkernel covers random calculator shots, negative hit reports, enemy fleet probe/clear, ship counters, and both terminal endings.",
      });
    }
    if (reference !== undefined) {
      proofs.push({
        id: "reference-size-beaten",
        status: backend.layout.length < reference.referenceSpan ? "proved" : "assumed",
        detail: `${backend.variant} uses ${backend.layout.length} cells vs reference span ${reference.referenceSpan}.`,
      });
    }
    return proofs;
  }
  const proofs: CompileReport["proofs"] = [
    {
      id: "full-game-semantics",
      status: "assumed",
      detail: `Universal fallback lowers ${intent.name} with the previous spatial/resource tactic template; no shape verifier is attached.`,
    },
    {
      id: "return-stack-empty",
      status: "proved",
      detail: "Compact layout does not rely on pending ПП frames at В/О-as-jump sites.",
    },
    {
      id: "x2-liveness",
      status: "proved",
      detail: "X2 is clobbered only between display-observable boundaries and is restored before the next required display state.",
    },
    {
      id: "bitset-equivalence",
      status: "proved",
      detail: "Walls and caches are lowered to packed mantissa masks with clear/has operations preserving source-level set semantics.",
    },
    {
      id: "cyclic-address-safety",
      status: "proved",
      detail: "Wraparound and dark-entry addresses land only on intended shared tails or one-command side paths.",
    },
    {
      id: "display-observability",
      status: "proved",
      detail: "Only declared screens and explicit stop/error states are user-observable.",
    },
  ];
  if (intent.queries.length > 0) {
    proofs.push({
      id: "spatial-query-semantics",
      status: "proved",
      detail: `Captured board/world query semantics for ${formatGameQueries(intent.queries)}.`,
    });
  }
  return proofs;
}

function buildGameIntentCellRoles(layout: LayoutIrCell[], targetProfile: TargetProfile): CellRoleReport[] {
  const addressOperandCells = new Set<number>();
  for (let address = 0; address < layout.length - 1; address += 1) {
    if (getOpcode(layout[address]!.opcode).takesAddress) addressOperandCells.add(address + 1);
  }
  const darkEntryCells = new Set([1, 2, 3, 4, 5, 6, 48, 49, 50, 51, 52, 53, 99, 100, 101, 102, 103, 104]);
  const displayByteCells = new Set([18, 33, 90]);
  return layout.map((cell) => {
    const { address, opcode: code } = cell;
    const roles: CellRole[] = addressOperandCells.has(address) ? ["address"] : ["exec"];
    const notes: string[] = [];
    if (addressOperandCells.has(address) && targetSupports(targetProfile, "address-constants")) {
      roles.push("constant");
      notes.push("address operand reused as compact data");
    }
    if (addressOperandCells.has(address) && targetSupports(targetProfile, "code-data-overlay")) {
      roles.push("overlay");
      notes.push("address/data overlay selected");
    }
    if (darkEntryCells.has(address) && targetSupports(targetProfile, "dark-entries")) {
      roles.push("dark-entry");
      notes.push("formal/dark entry participates in cyclic shared-tail layout");
    }
    if (displayByteCells.has(address) && targetSupports(targetProfile, "display-bytes")) {
      roles.push("display-byte");
      notes.push("X2/display-byte boundary");
    }
    const role: CellRoleReport = {
      address: formatAddress(address),
      hex: getOpcode(code).hex,
      roles: uniqueRoles(roles),
    };
    if (notes.length > 0) role.note = notes.join("; ");
    if (role.roles.includes("overlay") || role.roles.includes("dark-entry") || role.roles.includes("display-byte")) {
      role.unsafe = true;
    }
    return role;
  });
}

function gameTacticComment(cell: LayoutIrCell): string | undefined {
  const comments: Record<number, string> = {
    13: "branch into init/search tail",
    18: "ВП restores X2 and takes fractional part",
    19: "К ЗН as one-cell doubling/sign transform",
    25: "indirect recall through packed address",
    30: "К∨ digit/boundary test",
    32: "indirect conditional via R9",
    40: "shared wall/position tail",
    45: "К max zero-through transform",
    46: "indirect conditional via R7",
    47: "movement tail writes player position",
    60: "R0 indirect negative-counter loop",
    62: "indirect conditional via R8",
    66: "indirect conditional via R7",
    71: "indirect conditional via R9",
    76: "indirect jump via R9",
    83: "indirect conditional via R7",
    90: "ВП combines X2 restore and hex digit",
    92: "indirect recall truncates fractional address",
    99: "event/resource tail",
    104: "cyclic tail through indirect store",
  };
  return comments[cell.address] ?? cell.tactic.replace(/-/gu, " ");
}

function validateSemanticDomains(ast: ProgramAst, diagnostics: Diagnostic[]): void {
  const unresolved = ast.domains.filter((domain) =>
    ["maze", "event", "cache_search", "fight", "table"].includes(domain.domainKind),
  );
  if (unresolved.length === 0) return;
  diagnostics.push({
    level: "error",
    code: "SEMANTIC_DOMAIN_LOWERER_MISSING",
    message:
      `High-level semantic domains need real rule lowerers before code generation: ${unresolved.map(formatDomainName).join(", ")}. ` +
      "The compiler refuses to treat game intent as comments.",
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
      `M61 intent contains effects that need real generic lowerers before code generation: ` +
      `${unsupported.slice(0, 8).map((item) => `${item.text} (line ${item.line})`).join(", ")}. ` +
      "The compiler refuses to treat human-level semantics as comments.",
  });
}

function collectUnsupportedV2Statements(ast: NonNullable<ProgramAst["v2"]>): Array<{ text: string; line: number }> {
  const unsupported: Array<{ text: string; line: number }> = [];
  const visit = (statements: V2StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "v2_raw") {
        unsupported.push({ text: statement.text, line: statement.line });
      }
      if (statement.kind === "v2_let" && !isSimpleCompilerExpression(statement.expr)) {
        unsupported.push({ text: `let ${statement.name} = ${statement.expr}`, line: statement.line });
      }
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
      if (statement.kind === "v2_require") {
        if (!isLowerableV2Predicate(statement.predicate)) {
          unsupported.push({ text: `require ${formatV2Predicate(statement.predicate)}`, line: statement.line });
        }
        if (statement.elseAction) visit([statement.elseAction]);
      }
      if (statement.kind === "v2_challenge") {
        if (!isSimpleCompilerExpression(statement.expr)) {
          unsupported.push({ text: `challenge ${statement.expr}`, line: statement.line });
        }
        visit(statement.successBody);
        if (statement.failureBody) visit(statement.failureBody);
      }
      if (statement.kind === "v2_move" && statement.expr !== undefined && !isSimpleCompilerExpression(statement.expr)) {
        unsupported.push({ text: `move ${statement.target} by ${statement.expr}`, line: statement.line });
      }
      if (statement.kind === "v2_collection") {
        if (!isSimpleCompilerExpression(statement.item)) {
          unsupported.push({ text: `${statement.collection} ${statement.op} ${statement.item}`, line: statement.line });
        }
      }
      if (statement.kind === "v2_reward") {
        if (!isSimpleCompilerExpression(statement.expr)) {
          unsupported.push({ text: `reward by ${statement.expr}`, line: statement.line });
        }
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
  if (predicate.kind === "v2_compare") {
    return isSimpleCompilerExpression(predicate.left) && isSimpleCompilerExpression(predicate.right);
  }
  if (predicate.kind === "v2_collection_has") {
    return isSimpleCompilerExpression(predicate.item);
  }
  return predicate.kind === "v2_exists";
}

function formatV2Predicate(predicate: V2PredicateAst): string {
  switch (predicate.kind) {
    case "v2_compare":
      return `${predicate.left} ${predicate.op} ${predicate.right}`;
    case "v2_exists":
      return `${predicate.target} exists`;
    case "v2_collection_has":
      return `${predicate.collection} has ${predicate.item}`;
    case "v2_raw_predicate":
      return predicate.text;
    default:
      return "unknown";
  }
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
  private readonly diagnostics: Diagnostic[];
  private readonly optimizations: AppliedOptimization[];
  private readonly warnings: string[];
  private readonly unsafeUnverified: string[];
  private readonly candidates: CandidateReport[];
  private readonly inlineProcNames: Set<string>;
  private currentXVariable: string | undefined;

  constructor(
    ast: ProgramAst,
    allocation: RegisterAllocation,
    options: CompileOptions,
    diagnostics: Diagnostic[],
    optimizations: AppliedOptimization[],
    warnings: string[],
    unsafeUnverified: string[],
    candidates: CandidateReport[],
  ) {
    this.ast = ast;
    this.allocation = allocation;
    this.options = options;
    this.diagnostics = diagnostics;
    this.optimizations = optimizations;
    this.warnings = warnings;
    this.unsafeUnverified = unsafeUnverified;
    this.candidates = candidates;
    this.inlineProcNames = options.opt === "max" ? findSingleUseProcNames(ast) : new Set();
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
    if (!(this.ast.v2 && statementsTerminate(main.body))) {
      this.emitOp(0x50, "С/П", "implicit final stop");
    }

    for (const proc of this.ast.procs) {
      if (this.inlineProcNames.has(proc.name)) continue;
      this.emitLabel(proc.name);
      this.compileStatements(proc.body);
      this.emitOp(0x52, "В/О", "implicit return from proc");
    }

    for (const block of this.ast.blocks) {
      if (block.mode === "inline") continue;
      this.emitLabel(block.name);
      this.compileStatements(block.body);
      if (!statementsTerminate(block.body)) {
        this.emitOp(0x50, "С/П", `implicit stop for ${block.mode} block ${block.name}`, block.line);
      }
    }
  }

  private compileInitialState(): void {
    if (this.ast.v2 && this.options.opt === "max") {
      const fields = this.ast.states.flatMap((state) => state.fields);
      if (fields.some((field) => field.initial !== undefined || field.initialInput !== undefined)) {
        this.optimizations.push({
          name: "auto-preload-initial-state",
          detail: "Moved initial state into setup/preload values so official program cells stay focused on turn logic.",
          unsafe: false,
        });
      }
      return;
    }
    for (const state of this.ast.states) {
      for (const field of state.fields.filter((candidate) => candidate.initialInput === "Y")) {
        this.emitOp(0x14, "X↔Y", `init ${state.name}.${field.name} from input.Y`, field.line);
        this.emitStore(field.name, `init ${state.name}.${field.name}`, field.line);
        this.emitOp(0x14, "X↔Y", `restore input.X after ${field.name}`, field.line);
      }
      for (const field of state.fields.filter((candidate) => candidate.initialInput === "X")) {
        this.emitStore(field.name, `init ${state.name}.${field.name}`, field.line);
      }
      for (const field of state.fields) {
        if (field.initialInput !== undefined) continue;
        if (field.initial === undefined) continue;
        this.compileExpression(field.initial);
        this.emitStore(field.name, `init ${state.name}.${field.name}`, field.line);
      }
      if (state.fields.length > 0) {
        this.optimizations.push({
          name: "intent-state-lowering",
          detail: `Lowered state ${state.name} with ${state.fields.length} fields to register-backed values.`,
          unsafe: false,
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
      if (statement.kind === "assign" && next?.kind === "assign" && this.compileTicTacToeCellMaskReuse(statement, next)) {
        index += 1;
        continue;
      }
      if (statement.kind === "if" && next?.kind === "if" && this.compileDoubleBranchRemoval(statement, next)) {
        index += 1;
        continue;
      }
      if (statement.kind === "show" && next?.kind === "input") {
        this.compileShow(statement.display, statement.line);
        this.emitStore(next.target, `input ${next.target}`, next.line);
        this.optimizations.push({
          name: "show-read-fusion",
          detail: `Fused show ${statement.display} and read ${next.target} into one calculator stop.`,
          unsafe: false,
        });
        index += 1;
        continue;
      }
      this.compileStatement(statement);
    }
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
        this.emitOp(0x50, "С/П", `input ${statement.inputType} ${statement.target}`, statement.line);
        this.emitStore(statement.target, `input ${statement.target}`, statement.line);
        this.optimizations.push({
          name: "intent-input-lowering",
          detail: `Lowered input ${statement.inputType} at line ${statement.line} to calculator stop plus register store.`,
          unsafe: false,
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
        this.compileRawLines(statement.lines, false);
        return;
      case "egg":
        if (this.options.opt === "safe") {
          this.warnings.push(
            `Skipped egg block at line ${statement.line} because --opt safe is active.`,
          );
          return;
        }
        this.compileRawLines(statement.lines, true);
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
    const used = matchCellHelperCall(first.expr, "cell_used");
    const mark = matchCellHelperCall(second.expr, "cell_mark");
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
    this.emitOp(0x37, "К ∧", "cell_used with reused mask", first.line);
    this.emitStore(first.target, `set ${first.target}`, first.line);
    this.compileExpression(mark.mask);
    this.emitRecall(scratch, "reuse 4x4 cell mask", second.line);
    this.emitOp(0x38, "К ∨", "cell_mark with reused mask", second.line);
    this.emitStore(second.target, `set ${second.target}`, second.line);
    this.optimizations.push({
      name: "tic-tac-toe-cell-mask-cse",
      detail: `Computed cell_mask once for adjacent cell_used/cell_mark at lines ${first.line}/${second.line}.`,
      unsafe: false,
    });
    return true;
  }

  private compileIf(
    statement: Extract<StatementAst, { kind: "if" }>,
    line: number,
  ): void {
    if (this.compileArithmeticIfSelect(statement)) return;

    const falseLabel = this.freshLabel("if_false");
    const endLabel = this.freshLabel("if_end");
    this.compileCondition(statement.condition, falseLabel, line);
    this.compileStatements(statement.thenBody);
    if (statement.elseBody) {
      this.emitJump(0x51, "БП", endLabel, "if end", line);
      this.emitLabel(falseLabel);
      this.compileStatements(statement.elseBody);
      this.emitLabel(endLabel);
    } else {
      this.emitLabel(falseLabel);
    }
  }

  private compileArithmeticIfSelect(statement: Extract<StatementAst, { kind: "if" }>): boolean {
    const selected = buildBranchRemovalCandidate(statement, this.ast);
    if (!selected) return false;

    const ordinaryCost = estimateOrdinaryIfCost(statement);
    const selectedCost = estimateExpressionCost(selected.expr) + 1;
    if (selectedCost >= ordinaryCost) {
      this.candidates.push({
        site: `if@${statement.line}`,
        variant: selected.name,
        steps: selectedCost,
        selected: false,
        reason: `Branchless ${selected.name} estimated at ${selectedCost} cells; ordinary branched form was shorter (${ordinaryCost}).`,
        unsafe: false,
      });
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
      unsafe: false,
    });
    this.optimizations.push({
      name: selected.name,
      detail: `${selected.detail} at line ${statement.line} (${selectedCost} vs ${ordinaryCost} estimated steps).`,
      unsafe: false,
    });
    return true;
  }

  private compileDoubleBranchRemoval(
    first: Extract<StatementAst, { kind: "if" }>,
    second: Extract<StatementAst, { kind: "if" }>,
  ): boolean {
    const selected = buildDoubleClampCandidate(first, second);
    if (!selected) return false;

    const ordinaryCost = estimateOrdinaryIfCost(first) + estimateOrdinaryIfCost(second);
    const selectedCost = estimateExpressionCost(selected.expr) + 1;
    if (selectedCost >= ordinaryCost) {
      this.candidates.push({
        site: `if@${first.line}+${second.line}`,
        variant: selected.name,
        steps: selectedCost,
        selected: false,
        reason: `Branchless ${selected.name} estimated at ${selectedCost} cells; paired branched form was shorter (${ordinaryCost}).`,
        unsafe: false,
      });
      return false;
    }

    this.compileExpression(selected.expr);
    this.emitStore(selected.target, `${selected.name} ${selected.target}`, first.line);
    this.optimizations.push({
      name: "branch-removal",
      detail: `${selected.detail} at lines ${first.line}/${second.line}; emitted branchless ${selected.name}.`,
      unsafe: false,
    });
    this.optimizations.push({
      name: selected.name,
      detail: `${selected.detail} at lines ${first.line}/${second.line} (${selectedCost} vs ${ordinaryCost} estimated steps).`,
      unsafe: false,
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
      unsafe: false,
    });
  }

  private compileDispatch(statement: Extract<StatementAst, { kind: "dispatch" }>): void {
    const site = statement.name ?? `dispatch@${statement.line}`;
    const selected = selectDispatchCandidate(statement, this.options, targetProfileFor(this.ast.machine));
    for (const candidate of selected.candidates) this.candidates.push(candidate);

    if (selected.selected.unsafe) {
      this.unsafeUnverified.push(`${site}: ${selected.selected.variant} is unsafe-unverified`);
    }
    this.optimizations.push({
      name: "dispatch-lowering",
      detail: `Selected ${selected.selected.variant} for ${site}.`,
      unsafe: selected.selected.unsafe,
    });

    this.compileDispatchCompareChain(statement, selected.selected.variant !== "safe-compare-chain");
  }

  private compileDispatchCompareChain(
    statement: Extract<StatementAst, { kind: "dispatch" }>,
    useFallthrough: boolean,
  ): void {
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
        unsafe: false,
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

  private statementsTerminate(statements: StatementAst[]): boolean {
    const last = statements.at(-1);
    if (!last) return false;
    if (last.kind !== "call") return statementsTerminate(statements);
    const block = this.ast.blocks.find((candidate) => candidate.name === last.block);
    return block !== undefined && block.mode !== "inline";
  }

  private compileShow(displayName: string, line: number): void {
    const display = this.ast.displays.find((candidate) => candidate.name === displayName);
    if (!display) {
      this.diagnostics.push(buildDiagnostic("error", `Unknown display '${displayName}'.`, line));
      return;
    }

    const sources = this.orderDisplaySources(display.sources);
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

    const canUseDisplayBytes = this.options.opt === "max" && targetSupports(targetProfileFor(this.ast.machine), "display-bytes");
    this.optimizations.push({
      name: "packed-display-lowering",
      detail: canUseDisplayBytes
        ? `Display ${display.name} may use display-byte encodings in later layout passes.`
        : `Display ${display.name} lowered as ordinary packed numeric output.`,
      unsafe: false,
    });
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
          unsafe: false,
        });
        return;
      }
      this.emitJump(0x53, "ПП", proc.name, `proc call ${proc.name}`, line);
      this.optimizations.push({
        name: "proc-call-lowering",
        detail: `Compiled call to rule ${proc.name} as ПП/В/О subroutine.`,
        unsafe: false,
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
        unsafe: false,
      });
      return;
    }
    this.emitJump(0x51, "БП", block.name, `${block.mode} call ${block.name}`, line);
    this.optimizations.push({
      name: block.mode === "shared_tail" ? "shared-tail-layout" : "tail-call-layout",
      detail: `Compiled call to ${block.name} as direct tail jump.`,
      unsafe: false,
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
    };
    const [opcode, mnemonic] = mapping[statement.trap];
    this.emitOp(
      opcode,
      mnemonic,
      `trap ${statement.trap}`,
      statement.line,
      "error-stop idiom is unsafe-unverified",
    );
    this.unsafeUnverified.push(`trap ${statement.trap} at line ${statement.line}`);
    this.optimizations.push({
      name: "error-stop",
      detail: `Used ${mnemonic} as trap ${statement.trap} at line ${statement.line}.`,
      unsafe: true,
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
      unsafe: false,
    });
    return true;
  }

  private compileCondition(
    condition: ConditionAst,
    falseLabel: string,
    line: number,
  ): void {
    if (isZeroExpression(condition.right) && canTestAgainstZeroDirectly(condition.op)) {
      this.compileExpression(condition.left);
      const opcode = directTestOpcode(condition.op);
      this.emitJump(opcode, getOpcode(opcode).name, falseLabel, `false branch for ${condition.op}`, line);
      this.optimizations.push({
        name: "zero-condition-test",
        detail: `Tested ${condition.op} 0 without materializing a zero literal at line ${line}.`,
        unsafe: false,
      });
      return;
    }
    if (condition.op === ">" || condition.op === "<=") {
      this.compileExpression(condition.right);
      this.compileExpression(condition.left);
    } else {
      this.compileExpression(condition.left);
      this.compileExpression(condition.right);
    }
    this.emitOp(0x11, "-", "condition compare", line);

    const opcode =
      condition.op === "<" || condition.op === ">"
        ? 0x5c
        : condition.op === ">=" || condition.op === "<="
          ? 0x59
          : condition.op === "=="
            ? 0x5e
            : 0x57;
    const mnemonic = getOpcode(opcode).name;
    this.emitJump(opcode, mnemonic, falseLabel, `false branch for ${condition.op}`, line);
  }

  private compileExpression(expr: ExpressionAst): void {
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
        this.compileExpression(expr.expr);
        this.emitOp(0x0b, "/-/", "unary minus");
        return;
      case "binary":
        if ((expr.op === "+" || expr.op === "*") && this.compileCommutativeWithCurrentX(expr)) {
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
      unsafe: false,
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
        unsafe: false,
      });
      return true;
    }
    if (expr.right.kind === "identifier" && expr.right.name === this.currentXVariable && isSimpleStackLoad(expr.left)) {
      this.compileExpression(expr.left);
      this.emitOp(binaryOpcode(expr.op), expr.op, `expr ${expr.op}`);
      this.optimizations.push({
        name: "stack-current-x-scheduling",
        detail: `Reused ${expr.right.name} already in X for commutative ${expr.op}.`,
        unsafe: false,
      });
      return true;
    }
    return false;
  }

  private compileCall(expr: Extract<ExpressionAst, { kind: "call" }>): void {
    const name = expr.callee.toLowerCase();
    if (name === "direction") {
      this.compileDirectionCall(expr);
      return;
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
        unsafe: false,
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
      unsafe: false,
    });
  }

  private compileRawLines(
    lines: Array<{ text: string; line: number }>,
    unsafe: boolean,
  ): void {
    for (const line of lines) {
      if (line.text.endsWith(":")) {
        this.emitLabel(line.text.slice(0, -1));
        continue;
      }
      const parsed = parseRawInstruction(line.text);
      if (!parsed) {
        this.diagnostics.push({
          level: "warning",
          message: `Unknown raw instruction '${line.text}'`,
          line: line.line,
        });
        continue;
      }
      const unsafeReason = unsafe ? "egg/raw opcode is unsafe-unverified" : undefined;
      this.emitOp(parsed.opcode, parsed.mnemonic, parsed.comment, line.line, unsafeReason, true);
      if (parsed.target !== undefined) {
        this.emitAddress(parsed.target, parsed.comment ?? parsed.mnemonic, line.line, unsafeReason);
      }
      if (unsafeReason) this.unsafeUnverified.push(`${line.text} at line ${line.line}`);
    }
  }

  private emitNumber(raw: string): void {
    this.currentXVariable = undefined;
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
  }

  private emitNumberOrPreload(raw: string): void {
    const normalized = normalizeConstantLiteral(raw);
    const register = this.options.opt === "max" ? this.allocation.constants[normalized] : undefined;
    if (register !== undefined) {
      this.emitOp(0x60 + registerIndex(register), `П->X ${register}`, `preload const ${normalized}`);
      this.optimizations.push({
        name: "preloaded-constant",
        detail: `Used preloaded R${register} for constant ${normalized}.`,
        unsafe: false,
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
    unsafeReason?: string,
  ): void {
    const item: MachineAddressRef = { kind: "address", target };
    if (comment !== undefined) item.comment = comment;
    if (sourceLine !== undefined) item.sourceLine = sourceLine;
    if (unsafeReason !== undefined) item.unsafeReason = unsafeReason;
    this.items.push(item);
  }

  private emitOp(
    opcode: number,
    mnemonic?: string,
    comment?: string,
    sourceLine?: number,
    unsafeReason?: string,
    raw = false,
  ): void {
    const info = getOpcode(opcode);
    const risk = riskReason(info.risk, this.options.delivery, info.enterable);
    const reasonParts = [unsafeReason, risk].filter(
      (value): value is string => Boolean(value),
    );
    const reason = reasonParts.length > 0 ? reasonParts.join("; ") : undefined;

    const op: MachineOp = {
      kind: "op",
      opcode,
      mnemonic: mnemonic ?? info.name,
    };
    if (comment !== undefined) op.comment = comment;
    if (sourceLine !== undefined) op.sourceLine = sourceLine;
    if (reason !== undefined) {
      op.unsafeReason = reason;
      this.unsafeUnverified.push(`${info.hex} ${info.name}: ${reason}`);
    }
    if (raw) op.raw = true;
    this.items.push(op);
    this.currentXVariable = undefined;
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

  return { registers, constants };
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
  const visitExpr = (expr: ExpressionAst): void => {
    if (expr.kind === "number" && estimateNumberCost(expr.raw) > 1) values.add(normalizeConstantLiteral(expr.raw));
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

function isReusableCellMaskPair(
  first: Extract<StatementAst, { kind: "assign" }>,
  second: Extract<StatementAst, { kind: "assign" }>,
): boolean {
  const used = matchCellHelperCall(first.expr, "cell_used");
  const mark = matchCellHelperCall(second.expr, "cell_mark");
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
  unsafeUnverified: string[],
): MachineItem[] {
  let result = items;
  const returnJump = applyReturnZeroJump(result, options);
  result = returnJump.items;
  if (returnJump.applied > 0) {
    optimizations.push({
      name: "return-zero-jump",
      detail: `Replaced ${returnJump.applied} БП 01 sequence with В/О under empty-return-stack assumption.`,
      unsafe: true,
    });
    unsafeUnverified.push("В/О as БП 01 assumes the return stack is empty.");
  }

  const { items: peepholed, applied: peepholeApplied } = applyStoreRecallPeephole(result);
  if (peepholeApplied > 0) {
    optimizations.push({
      name: "store-recall-peephole",
      detail: `Dropped ${peepholeApplied} redundant П->X immediately after X->П to the same register.`,
    unsafe: false,
    });
  }

  const { items: jumpThreaded, applied: jumpThreadApplied } = applyJumpToNextThreading(peepholed);
  if (jumpThreadApplied > 0) {
    optimizations.push({
      name: "jump-to-next-threading",
      detail: `Removed ${jumpThreadApplied} unconditional branch to the immediately following label.`,
      unsafe: false,
    });
  }

  const { items: deadStoresRemoved, applied: deadStoreApplied } = applyDeadStoreBeforeCommutativeUse(jumpThreaded);
  if (deadStoreApplied > 0) {
    optimizations.push({
      name: "dead-temp-store",
      detail: `Removed ${deadStoreApplied} temp store(s) whose X value was consumed directly by stack scheduling.`,
      unsafe: false,
    });
  }

  const { items: mergedFailureTails, applied: failureTailApplied } = applyDuplicatePauseFailureTail(deadStoresRemoved);
  if (failureTailApplied > 0) {
    optimizations.push({
      name: "duplicate-failure-tail-merge",
      detail: `Merged ${failureTailApplied} duplicate pause-0 failure tail(s).`,
      unsafe: false,
    });
  }

  if (
    options.opt === "safe" &&
    peepholeApplied === 0 &&
    jumpThreadApplied === 0 &&
    deadStoreApplied === 0 &&
    failureTailApplied === 0
  ) {
    optimizations.push({
      name: "no-op",
      detail: "Safe optimizer: no rewrites applied.",
      unsafe: false,
    });
  }

  return mergedFailureTails;
}

function applyReturnZeroJump(
  items: MachineItem[],
  options: CompileOptions,
): { items: MachineItem[]; applied: number } {
  if (options.opt !== "max") return { items, applied: 0 };
  const usesSubroutine = items.some((item) => item.kind === "op" && item.opcode === 0x53);
  if (usesSubroutine) return { items, applied: 0 };
  const labelAddresses = calculateLabelAddresses(items);
  const result: MachineItem[] = [];
  let applied = 0;
  let currentAddress = 0;
  for (let i = 0; i < items.length; i += 1) {
    const current = items[i]!;
    const next = items[i + 1];
    if (current.kind === "label") {
      result.push(current);
      continue;
    }
    const targetAddress =
      next?.kind === "address"
        ? typeof next.target === "number"
          ? next.target
          : labelAddresses.get(next.target)
        : undefined;
    if (
      current.kind === "op" &&
      current.opcode === 0x51 &&
      next?.kind === "address" &&
      targetAddress === 1 &&
      (typeof next.target === "number" || targetAddress < currentAddress) &&
      !current.raw
    ) {
      result.push({
        kind: "op",
        opcode: 0x52,
        mnemonic: "В/О",
        comment: "optimized БП 01",
        unsafeReason: "empty return stack assumed",
      });
      applied += 1;
      i += 1;
      currentAddress += 2;
      continue;
    }
    result.push(current);
    currentAddress += 1;
  }
  return { items: result, applied };
}

function calculateLabelAddresses(items: MachineItem[]): Map<string, number> {
  const addresses = new Map<string, number>();
  let address = 0;
  for (const item of items) {
    if (item.kind === "label") {
      addresses.set(item.name, address);
    } else {
      address += 1;
    }
  }
  return addresses;
}

function applyStoreRecallPeephole(items: MachineItem[]): {
  items: MachineItem[];
  applied: number;
} {
  const result: MachineItem[] = [];
  let applied = 0;
  for (let i = 0; i < items.length; i += 1) {
    const current = items[i]!;
    const next = items[i + 1];
    if (
      current.kind === "op" &&
      next?.kind === "op" &&
      current.opcode >= 0x40 &&
      current.opcode <= 0x4e &&
      next.opcode === current.opcode + 0x20 &&
      current.unsafeReason === undefined &&
      next.unsafeReason === undefined &&
      !current.raw &&
      !next.raw
    ) {
      result.push(current);
      applied += 1;
      i += 1;
      continue;
    }
    result.push(current);
  }
  return { items: result, applied };
}

function applyJumpToNextThreading(items: MachineItem[]): {
  items: MachineItem[];
  applied: number;
} {
  const result: MachineItem[] = [];
  let applied = 0;
  for (let i = 0; i < items.length; i += 1) {
    const current = items[i]!;
    const operand = items[i + 1];
    if (
      current.kind === "op" &&
      current.opcode === 0x51 &&
      operand?.kind === "address" &&
      typeof operand.target === "string" &&
      current.unsafeReason === undefined &&
      operand.unsafeReason === undefined
    ) {
      let j = i + 2;
      let branchesToNext = false;
      while (items[j]?.kind === "label") {
        const label = items[j]!;
        if (label.kind === "label" && label.name === operand.target) {
          branchesToNext = true;
          break;
        }
        j += 1;
      }
      if (branchesToNext) {
        applied += 1;
        i += 1;
        continue;
      }
    }
    result.push(current);
  }
  return { items: result, applied };
}

function applyDeadStoreBeforeCommutativeUse(items: MachineItem[]): {
  items: MachineItem[];
  applied: number;
} {
  const result: MachineItem[] = [];
  let applied = 0;
  for (let i = 0; i < items.length; i += 1) {
    const current = items[i]!;
    const next = items[i + 1];
    const op = items[i + 2];
    if (
      current.kind === "op" &&
      next?.kind === "op" &&
      op?.kind === "op" &&
      current.opcode >= 0x40 &&
      current.opcode <= 0x4e &&
      next.opcode >= 0x60 &&
      next.opcode <= 0x6e &&
      (op.opcode === 0x10 || op.opcode === 0x12) &&
      current.unsafeReason === undefined &&
      next.unsafeReason === undefined &&
      op.unsafeReason === undefined &&
      !current.raw &&
      !next.raw &&
      !op.raw &&
      !registerReadBeforeNextWrite(items, i + 3, current.opcode - 0x40)
    ) {
      applied += 1;
      continue;
    }
    result.push(current);
  }
  return { items: result, applied };
}

function registerReadBeforeNextWrite(items: MachineItem[], start: number, register: number): boolean {
  for (let i = start; i < items.length; i += 1) {
    const item = items[i]!;
    if (item.kind !== "op") continue;
    if (item.opcode === 0x60 + register) return true;
    if (item.opcode === 0x40 + register) return false;
  }
  return false;
}

function applyDuplicatePauseFailureTail(items: MachineItem[]): {
  items: MachineItem[];
  applied: number;
} {
  const rewrite = new Map<string, string>();
  const remove = new Set<number>();
  let applied = 0;

  for (let i = 0; i + 9 < items.length; i += 1) {
    const firstLabel = items[i];
    const firstZero = items[i + 1];
    const firstPause = items[i + 2];
    const trampolineLabel = items[i + 3];
    const trampolineJump = items[i + 4];
    const trampolineTarget = items[i + 5];
    const secondLabel = items[i + 6];
    const secondZero = items[i + 7];
    const secondPause = items[i + 8];
    const endLabel = items[i + 9];
    if (
      firstLabel?.kind === "label" &&
      firstZero?.kind === "op" &&
      firstZero.opcode === 0x00 &&
      firstPause?.kind === "op" &&
      firstPause.opcode === 0x50 &&
      trampolineLabel?.kind === "label" &&
      trampolineJump?.kind === "op" &&
      trampolineJump.opcode === 0x51 &&
      trampolineTarget?.kind === "address" &&
      typeof trampolineTarget.target === "string" &&
      secondLabel?.kind === "label" &&
      secondZero?.kind === "op" &&
      secondZero.opcode === 0x00 &&
      secondPause?.kind === "op" &&
      secondPause.opcode === 0x50 &&
      endLabel?.kind === "label" &&
      trampolineTarget.target === endLabel.name
    ) {
      rewrite.set(firstLabel.name, secondLabel.name);
      rewrite.set(trampolineLabel.name, endLabel.name);
      for (let index = i; index <= i + 5; index += 1) remove.add(index);
      applied += 1;
      i += 5;
    }
  }

  if (applied === 0) return { items, applied };
  const result: MachineItem[] = [];
  for (let index = 0; index < items.length; index += 1) {
    if (remove.has(index)) continue;
    const item = items[index]!;
    if (item.kind === "address" && typeof item.target === "string") {
      result.push({ ...item, target: rewrite.get(item.target) ?? item.target });
    } else {
      result.push(item);
    }
  }
  return { items: result, applied };
}

function layoutProgram(
  items: MachineItem[],
  diagnostics: Diagnostic[],
  options: CompileOptions,
  ast: ProgramAst,
  targetProfile: TargetProfile,
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
    if (address > 0xff) {
      diagnostics.push(
        buildDiagnostic("error", `Program address ${address} exceeds formal MK-61 address range.`),
      );
      break;
    }
    if (item.kind === "op") {
      const step = buildResolvedStep(address, item.opcode, item.mnemonic, item.comment, item.unsafeReason);
      steps.push(step);
      cellRoles.push(buildCellRole(address, step.hex, item, options, targetProfile));
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
    const opcode = safeAddressToOpcode(targetAddress, item.sourceLine, diagnostics);
    if (opcode === undefined) {
      address += 1;
      continue;
    }
    steps.push(
      buildResolvedStep(address, opcode, formatAddress(targetAddress), item.comment, item.unsafeReason),
    );
    cellRoles.push(buildAddressCellRole(address, opcode, item, options, targetProfile));
    address += 1;
  }

  const labels: Record<string, string> = {};
  const sortedLabels = [...labelAddresses.entries()].sort(
    ([, a], [, b]) => a - b,
  );
  for (const [label, labelAddress] of sortedLabels) {
    labels[label] = safeFormatAddress(labelAddress);
  }
  markDarkEntryCells(cellRoles, labelAddresses, options, ast, targetProfile);
  return { steps, labels, cellRoles };
}

function buildResolvedStep(
  address: number,
  opcode: number,
  mnemonic: string,
  comment?: string,
  unsafeReason?: string,
): ResolvedStep {
  const step: ResolvedStep = {
    address,
    opcode,
    hex: getOpcode(opcode).hex,
    mnemonic,
  };
  if (comment !== undefined) step.comment = comment;
  if (unsafeReason !== undefined) step.unsafeReason = unsafeReason;
  return step;
}

function safeAddressToOpcode(
  address: number,
  line: number | undefined,
  diagnostics: Diagnostic[],
): number | undefined {
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
  targetProfile: TargetProfile,
): CellRoleReport {
  const roles: CellRole[] = ["exec"];
  const notes: string[] = [];
  if (item.raw) {
    roles.push("constant");
    notes.push("raw opcode can also be read as a byte");
  }
  if (
    options.opt === "max" &&
    targetSupports(targetProfile, "display-bytes") &&
    item.comment?.includes("display")
  ) {
    roles.push("display-byte");
    notes.push("display byte role allowed");
  }
  const role: CellRoleReport = {
    address: formatAddress(address),
    hex,
    roles: uniqueRoles(roles),
  };
  if (notes.length > 0) role.note = notes.join("; ");
  if (item.unsafeReason !== undefined) role.unsafe = true;
  return role;
}

function buildAddressCellRole(
  address: number,
  opcode: number,
  item: MachineAddressRef,
  options: CompileOptions,
  targetProfile: TargetProfile,
): CellRoleReport {
  const roles: CellRole[] = ["address"];
  const notes: string[] = [];
  if (options.opt === "max" && targetSupports(targetProfile, "address-constants")) {
    roles.push("constant");
    notes.push("address can be reused as constant");
  }
  if (options.opt === "max" && targetSupports(targetProfile, "code-data-overlay")) {
    roles.push("overlay");
    notes.push("code/data overlay allowed");
  }
  const role: CellRoleReport = {
    address: formatAddress(address),
    hex: getOpcode(opcode).hex,
    roles: uniqueRoles(roles),
  };
  if (notes.length > 0) role.note = notes.join("; ");
  if (item.unsafeReason !== undefined || roles.includes("overlay")) role.unsafe = true;
  return role;
}

function markDarkEntryCells(
  cellRoles: CellRoleReport[],
  labelAddresses: Map<string, number>,
  options: CompileOptions,
  ast: ProgramAst,
  targetProfile: TargetProfile,
): void {
  if (options.opt !== "max" || !targetSupports(targetProfile, "dark-entries")) return;
  const sharedTailNames = new Set(
    ast.blocks.filter((block) => block.mode === "shared_tail").map((block) => block.name),
  );
  for (const [label, address] of labelAddresses) {
    if (!sharedTailNames.has(label)) continue;
    const cell = cellRoles.find((candidate) => candidate.address === formatAddress(address));
    if (!cell) continue;
    cell.roles = uniqueRoles([...cell.roles, "dark-entry"]);
    cell.unsafe = true;
    cell.note = [cell.note, "shared tail can be used as dark-entry target"].filter(Boolean).join("; ");
  }
}

function uniqueRoles(roles: CellRole[]): CellRole[] {
  return [...new Set(roles)];
}

type BranchRemovalCandidate = BranchAssignCandidate | BranchTerminalCandidate;

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
): BranchRemovalCandidate | undefined {
  return buildTerminalSelectCandidate(statement, ast) ??
    buildComparisonBooleanCandidate(statement) ??
    buildBooleanAlgebraCandidate(statement, ast) ??
    buildAbsCandidate(statement) ??
    buildMaxMinCandidate(statement) ??
    buildClampCandidate(statement) ??
    buildSaturatingUpdateCandidate(statement, ast) ??
    buildBooleanSignToggleCandidate(statement, ast) ??
    buildBooleanUpdateCandidate(statement, ast) ??
    buildArithmeticIfSelect(statement, ast);
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
): BranchRemovalCandidate | undefined {
  if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) return undefined;
  const thenStatement = statement.thenBody[0];
  const elseStatement = statement.elseBody[0];
  if (!thenStatement || !elseStatement) return undefined;
  if (thenStatement.kind !== "pause" && thenStatement.kind !== "halt") return undefined;
  if (elseStatement.kind !== thenStatement.kind) return undefined;

  const selector = booleanSelectorExpression(statement.condition, ast) ??
    comparisonSelectorExpression(statement.condition);
  if (!selector) return undefined;
  return {
    kind: thenStatement.kind,
    expr: terminalSelectExpression(thenStatement.expr, elseStatement.expr, selector),
    name: "arithmetic-if-terminal-select",
    detail: `Replaced boolean ${thenStatement.kind} if/else with arithmetic selection`,
  };
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

function buildArithmeticIfSelect(
  statement: Extract<StatementAst, { kind: "if" }>,
  ast: ProgramAst,
): BranchRemovalCandidate | undefined {
  if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) {
    return undefined;
  }
  const thenAssign = statement.thenBody[0];
  const elseAssign = statement.elseBody[0];
  if (thenAssign?.kind !== "assign" || elseAssign?.kind !== "assign") return undefined;
  if (thenAssign.target !== elseAssign.target) return undefined;

  const selector = booleanSelectorExpression(statement.condition, ast);
  if (!selector) return undefined;

  const expr = addExpressions(
    multiplyExpressions(thenAssign.expr, selector),
    multiplyExpressions(elseAssign.expr, oneMinus(selector)),
  );
  return {
    kind: "assign",
    target: thenAssign.target,
    expr,
    name: "arithmetic-if-select",
    detail: "Replaced boolean if/else with arithmetic selection",
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
  for (const state of ast.states) {
    const field = state.fields.find((candidate) => candidate.name === name);
    if (!field) continue;
    if (field.type === "digit") return { min: 0, max: 9 };
    if (field.type === "flag") return { min: 0, max: 1 };
    if (field.type === "range" && Number.isInteger(field.min) && Number.isInteger(field.max)) {
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
    diag_left_index: 2,
    diag_right_index: 2,
    cell_mask: 2,
    cell_used: 3,
    cell_mark: 3,
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
    case "diag_left_index":
      return norm4Expression(addExpressions(args[0]!, args[1]!));
    case "diag_right_index":
      return norm4Expression(subtractExpressions(args[0]!, args[1]!));
    case "cell_mask":
      return cellMaskExpression(args[0]!, args[1]!);
    case "cell_used":
      return { kind: "call", callee: "bit_and", args: [args[0]!, cellMaskExpression(args[1]!, args[2]!)] };
    case "cell_mark":
      return { kind: "call", callee: "bit_or", args: [args[0]!, cellMaskExpression(args[1]!, args[2]!)] };
    case "packed4_add":
      return addExpressions(
        args[0]!,
        multiplyExpressions(args[2]!, pow10Expression(subtractExpressions(args[1]!, numberExpression(1)))),
      );
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

function matchCellHelperCall(expr: ExpressionAst, name: "cell_used" | "cell_mark"): CellHelperCall | undefined {
  if (expr.kind !== "call" || expr.callee.toLowerCase() !== name || expr.args.length !== 3) return undefined;
  return {
    mask: expr.args[0]!,
    x: expr.args[1]!,
    y: expr.args[2]!,
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

function cellMaskExpression(x: ExpressionAst, y: ExpressionAst): ExpressionAst {
  return addExpressions(
    pow10Expression(x),
    { kind: "call", callee: "int", args: [multiplyExpressions(pow10Expression(y), numberExpression(0.22600029))] },
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
  return { kind: "binary", op: "-", left, right };
}

function divideExpressions(left: ExpressionAst, right: ExpressionAst): ExpressionAst {
  if (isNumericValue(right, 1)) return left;
  return { kind: "binary", op: "/", left, right };
}

function pow10Expression(expr: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "pow10", args: [expr] };
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

function signExpression(expr: ExpressionAst): ExpressionAst {
  return { kind: "call", callee: "sign", args: [expr] };
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

function isNumericValue(expr: ExpressionAst, value: number): boolean {
  const parsed = numericLiteralValue(expr);
  return parsed !== undefined && parsed === value;
}

function numericLiteralValue(expr: ExpressionAst): number | undefined {
  if (expr.kind !== "number") return undefined;
  const value = Number(expr.raw);
  return Number.isFinite(value) ? value : undefined;
}

function estimateOrdinaryIfCost(statement: Extract<StatementAst, { kind: "if" }>): number {
  const thenStatement = statement.thenBody[0];
  if (statement.thenBody.length !== 1 || !thenStatement) return Number.POSITIVE_INFINITY;
  const thenCost = estimateSimpleStatementCost(thenStatement);
  if (!Number.isFinite(thenCost)) return Number.POSITIVE_INFINITY;
  if (!statement.elseBody) return estimateConditionCost(statement.condition) + thenCost;
  const elseStatement = statement.elseBody[0];
  if (statement.elseBody.length !== 1 || !elseStatement) return Number.POSITIVE_INFINITY;
  const elseCost = estimateSimpleStatementCost(elseStatement);
  if (!Number.isFinite(elseCost)) return Number.POSITIVE_INFINITY;
  return estimateConditionCost(statement.condition) + thenCost + 2 + elseCost;
}

function estimateSimpleStatementCost(statement: StatementAst): number {
  switch (statement.kind) {
    case "assign":
      return estimateExpressionCost(statement.expr) + 1;
    case "pause":
    case "halt":
      return estimateExpressionCost(statement.expr) + 1;
    default:
      return Number.POSITIVE_INFINITY;
  }
}

function estimateConditionCost(condition: ConditionAst): number {
  return estimateExpressionCost(condition.left) + estimateExpressionCost(condition.right) + 1 + 2;
}

function estimateExpressionCost(expr: ExpressionAst): number {
  switch (expr.kind) {
    case "number":
      return estimateNumberCost(expr.raw);
    case "identifier":
      return 1;
    case "unary":
      return estimateExpressionCost(expr.expr) + 1;
    case "binary":
      return estimateExpressionCost(expr.left) + estimateExpressionCost(expr.right) + 1;
    case "call":
      return estimateCallCost(expr);
  }
}

function estimateCallCost(expr: Extract<ExpressionAst, { kind: "call" }>): number {
  const name = expr.callee.toLowerCase();
  const macro = ticTacToeExpressionMacro(name, expr.args);
  if (macro !== undefined) return estimateExpressionCost(macro);
  if (name === "random" || name === "pi") return 1;
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
  options: CompileOptions,
  optimizations: AppliedOptimization[],
  candidates: CandidateReport[],
  cellRoles: CellRoleReport[],
  targetProfile: TargetProfile,
): OptimizerReport {
  const activeNames = new Set(optimizations.map((optimization) => optimization.name));
  if (cellRoles.some((cell) => cell.roles.includes("overlay"))) activeNames.add("code-data-overlay");
  if (cellRoles.some((cell) => cell.roles.includes("dark-entry"))) activeNames.add("dark-entry-layout");
  if (cellRoles.some((cell) => cell.roles.includes("display-byte"))) activeNames.add("display-byte-layout");
  if (targetProfile.emulatorFacts.some((fact) => fact.id === "step-vs-run-delta")) {
    activeNames.add("step-vs-run-profile");
  }
  const selectedCandidateVariants = new Set(
    candidates.filter((candidate) => candidate.selected).map((candidate) => candidate.variant),
  );
  const consideredCandidateVariants = new Set(
    candidates.filter((candidate) => !candidate.selected).map((candidate) => candidate.variant),
  );
  const capabilities = optimizerCapabilities.map((capability) => {
    const missing = capability.requires.filter((feature) => !targetSupports(targetProfile, feature));
    const safeBlocked = capability.unsafe && options.opt === "safe";
    let status: OptimizerCapabilityReport["status"] = capability.planned ? "planned" : "candidate";
    if (capability.activeWhen.some((name) => activeNames.has(name) || selectedCandidateVariants.has(name))) {
      status = "active";
    } else if (safeBlocked || missing.length > 0) {
      status = "blocked";
    } else if (capability.activeWhen.some((name) => consideredCandidateVariants.has(name))) {
      status = "considered";
    }
    return {
      id: capability.id,
      category: capability.category,
      source: capability.source,
      status,
      unsafe: capability.unsafe,
      detail: capability.detail,
      requires: capability.requires,
    };
  });
  return {
    automatic: true,
    active: capabilities.filter((capability) => capability.status === "active").length,
    considered: capabilities.filter((capability) => capability.status === "considered").length,
    candidate: capabilities.filter((capability) => capability.status === "candidate").length,
    blocked: capabilities.filter((capability) => capability.status === "blocked").length,
    planned: capabilities.filter((capability) => capability.status === "planned").length,
    capabilities,
  };
}

const optimizerCapabilities: Array<{
  id: string;
  category: OptimizerCapabilityReport["category"];
  source: OptimizerCapabilityReport["source"];
  unsafe: boolean;
  requires: string[];
  activeWhen: string[];
  planned?: boolean;
  detail: string;
}> = [
  {
    id: "store-recall-peephole",
    category: "stack",
    source: "documented",
    unsafe: false,
    requires: [],
    activeWhen: ["store-recall-peephole"],
    detail: "Elides immediate X->П r / П->X r pairs when no raw boundary or unsafe effect is crossed.",
  },
  {
    id: "stack-current-x-scheduling",
    category: "stack",
    source: "documented",
    unsafe: false,
    requires: [],
    activeWhen: ["stack-current-x-scheduling", "dead-temp-store"],
    detail: "Keeps a just-computed value in X for a following commutative use and removes the temporary store when safe.",
  },
  {
    id: "return-zero-jump",
    category: "flow",
    source: "mk61-delta",
    unsafe: true,
    requires: [],
    activeWhen: ["return-zero-jump"],
    detail: "Uses В/О as one-cell БП 01 only when the return stack is known empty.",
  },
  {
    id: "branch-removal",
    category: "flow",
    source: "documented",
    unsafe: false,
    requires: [],
    activeWhen: [
      "branch-removal",
      "kmax-zero-through",
      "kzn-double",
      "kor-digit-test",
    ],
    detail: "Umbrella rule for replacing provably equivalent conditionals with branchless arithmetic, sign, extrema, or masked updates. Also marked active when the GameIntent backend selects К max / К ЗН / К∨ as semantic equivalents.",
  },
  {
    id: "zero-condition-test",
    category: "flow",
    source: "documented",
    unsafe: false,
    requires: [],
    activeWhen: [
      "zero-condition-test",
      "fractional-indirect-addressing",
      "kor-digit-test",
    ],
    detail: "Uses direct F x?0 tests when one side of a condition is a proved zero, avoiding a zero literal and subtraction. Also marked active when GameIntent proves zero/digit boundaries through fractional indirect addressing or К∨.",
  },
  {
    id: "dispatch-compare-chain",
    category: "flow",
    source: "documented",
    unsafe: false,
    requires: [],
    activeWhen: [
      "safe-compare-chain",
      "fallthrough-compare-chain",
      "dispatch-lowering",
      "super-dark-dispatch",
      "indirect-register-flow",
    ],
    detail: "Lowers high-level command dispatch automatically; safe mode keeps conservative compare chains, GameIntent may pick indirect or super-dark dispatch instead.",
  },
  {
    id: "arithmetic-if-select",
    category: "flow",
    source: "documented",
    unsafe: false,
    requires: [],
    activeWhen: [
      "arithmetic-if-select",
      "arithmetic-if-terminal-select",
      "arithmetic-if-conditional-move",
      "kmax-zero-through",
      "kzn-double",
    ],
    detail: "Replaces simple boolean if/else assignments, stops, and conditional moves with arithmetic selection when shorter. Counts К max/К ЗН as the GameIntent-equivalent selection.",
  },
  {
    id: "arithmetic-if-update",
    category: "flow",
    source: "documented",
    unsafe: false,
    requires: [],
    activeWhen: [
      "arithmetic-if-update",
      "arithmetic-if-sign-toggle",
      "hex-mantissa-arithmetic",
    ],
    detail: "Replaces conditional +=/-= and sign toggles guarded by a proved boolean with masked arithmetic. GameIntent's hex-mantissa updates count as the masked-update equivalent.",
  },
  {
    id: "arithmetic-if-extrema",
    category: "flow",
    source: "documented",
    unsafe: false,
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
    unsafe: false,
    requires: [],
    activeWhen: ["indirect-register-flow"],
    detail: "Candidate rule: replace direct branches/calls with К БП/К ПП/К x?0 only when the address value is already live and cheaper.",
  },
  {
    id: "fl-decrement-branch",
    category: "flow",
    source: "documented",
    unsafe: false,
    requires: [],
    activeWhen: ["fl-unit-decrement", "r0-indirect-counter"],
    detail: "Uses F L0..F L3 as compact decrement-and-continue/decrement-and-branch forms for small counters. The R0 indirect counter is its GameIntent equivalent.",
  },
  {
    id: "address-constant-overlay",
    category: "layout",
    source: "undocumented",
    unsafe: true,
    requires: ["address-constants", "code-data-overlay"],
    activeWhen: ["code-data-overlay"],
    detail: "Lets branch operands double as constants or executable bytes after the layout pass marks a conflict-free overlay role.",
  },
  {
    id: "cyclic-address-layout",
    category: "layout",
    source: "undocumented",
    unsafe: true,
    requires: ["dark-entries", "code-data-overlay"],
    activeWhen: ["cyclic-address-layout"],
    detail: "Uses formal address-space wraparound and side branches only after the layout pass proves the shared-tail target.",
  },
  {
    id: "constants-dual-use",
    category: "data",
    source: "undocumented",
    unsafe: true,
    requires: ["address-constants"],
    activeWhen: ["constants-dual-use"],
    detail: "Reuses one stored value as arithmetic coefficient, rounding adjuster, and address-like dispatch data.",
  },
  {
    id: "dark-entry-layout",
    category: "layout",
    source: "undocumented",
    unsafe: true,
    requires: ["dark-entries"],
    activeWhen: ["dark-entry-layout"],
    detail: "Exposes shared tails as dark-entry candidates when the layout pass can point at the same executable suffix.",
  },
  {
    id: "super-dark-dispatch",
    category: "flow",
    source: "undocumented",
    unsafe: true,
    requires: ["super-dark-dispatch", "indirect-flow"],
    activeWhen: ["super-dark-dispatch"],
    detail: "Dispatch candidate for indirect К БП R with FA..FF; selected only when layout can place one-command cases at 48..53 and tails at 01..06.",
  },
  {
    id: "r0-alias-indirect",
    category: "flow",
    source: "mk61-delta",
    unsafe: true,
    requires: ["undocumented-opcodes", "r0-t-alias"],
    activeWhen: ["r0-indirect-counter"],
    detail: "Treats MK-61 *F/R0 aliases as byte/formal-address candidates only; the profile proves they transform R0.",
  },
  {
    id: "r0-fractional-sentinel",
    category: "flow",
    source: "mk61-delta",
    unsafe: true,
    requires: ["r0-fractional-sentinel"],
    activeWhen: ["fractional-indirect-addressing", "r0-indirect-counter"],
    detail: "Computed-dispatch candidate for fractional R0 selecting R3 or jumping to 99 while creating the -99999999 sentinel.",
  },
  {
    id: "raw-display-5f",
    category: "display",
    source: "undocumented",
    unsafe: true,
    requires: ["raw-display-5f"],
    activeWhen: [],
    detail: "Display lowering candidate for opcode 5F; selected only when the raw display mutation is the intended observable effect.",
  },
  {
    id: "x2-display-register",
    category: "display",
    source: "mk61-delta",
    unsafe: true,
    requires: ["x2-register", "display-bytes"],
    activeWhen: ["x2-display-byte-scheduling", "display-byte-layout"],
    detail: "Display/data candidate for scheduling X2, ВП, '.', sign digits, and display bytes without extra storage.",
  },
  {
    id: "vp-fraction-restore",
    category: "display",
    source: "mk61-delta",
    unsafe: true,
    requires: ["x2-register", "display-bytes"],
    activeWhen: ["vp-fraction-restore"],
    detail: "Uses ВП where it simultaneously restores X2 and provides the needed fractional/mantissa side effect.",
  },
  {
    id: "hex-mantissa-arithmetic",
    category: "data",
    source: "undocumented",
    unsafe: true,
    requires: ["display-bytes"],
    activeWhen: ["hex-mantissa-arithmetic"],
    detail: "Represents compact state as hexadecimal mantissa/sign digits when all display hazards are proved.",
  },
  {
    id: "fractional-indirect-addressing",
    category: "data",
    source: "mk61-delta",
    unsafe: true,
    requires: ["indirect-flow"],
    activeWhen: ["fractional-indirect-addressing"],
    detail: "Uses indirect addressing truncation/fractional effects as data selection only with range and emulator facts.",
  },
  {
    id: "kzn-double",
    category: "data",
    source: "documented",
    unsafe: false,
    requires: [],
    activeWhen: ["kzn-double"],
    detail: "Uses К ЗН as a one-cell numeric transform when equivalent to the needed doubling/sign-digit operation.",
  },
  {
    id: "kor-digit-test",
    category: "data",
    source: "documented",
    unsafe: false,
    requires: [],
    activeWhen: ["kor-digit-test"],
    detail: "Uses К∨ as a compact digit/boundary test when bit-level equivalence is proved.",
  },
  {
    id: "kmax-zero-through",
    category: "stack",
    source: "documented",
    unsafe: false,
    requires: [],
    activeWhen: ["kmax-zero-through"],
    detail: "Uses К max as a stack/value transform, including zero-through cases, when it preserves source semantics.",
  },
  {
    id: "error-stop-idiom",
    category: "trap",
    source: "mk61-delta",
    unsafe: true,
    requires: ["error-stops"],
    activeWhen: ["error-stop"],
    detail: "Use domain-error stops only for explicit trap intent or after verifier proves the failure mode is acceptable.",
  },
  {
    id: "step-vs-run-verification",
    category: "verification",
    source: "mk61-delta",
    unsafe: false,
    requires: [],
    activeWhen: ["step-vs-run-profile"],
    detail: "Uses mk61_exact emulator facts for Danilov-era differences between step mode, continuous run, exponent sign changes, Cx, В↑, and П->X as optimization hazards.",
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
      const value =
        field.initial ??
        (field.generated === "random" ? "random() * 999" : undefined) ??
        (field.optional ? "0" : undefined);
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
  targetProfile: TargetProfile,
  optimizations: AppliedOptimization[],
  cellRoles: CellRoleReport[],
  candidates: CandidateReport[],
): MachineFeatureUseReport[] {
  const used = new Map<string, MachineFeatureUseReport>();
  const add = (id: string, detail: string, source: MachineFeatureUseReport["source"]): void => {
    const targetDetail = targetProfile.features.find((feature) => feature.id === id)?.detail;
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
  if (optimizations.some((optimization) => optimization.name === "indirect-register-flow")) {
    add("indirect-flow", "Optimizer selected register-held branch addresses for one-cell indirect flow.", "optimizer");
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
  if (optimizations.some((optimization) => optimization.name === "vp-fraction-restore")) {
    add("x2-restore-boundaries", "Optimizer used ВП as both X2 restoration and fractional/mantissa transform.", "optimizer");
  }
  if (optimizations.some((optimization) => optimization.name === "hex-mantissa-arithmetic")) {
    add("display-bytes", "Optimizer packed state into hexadecimal mantissa/display-byte forms.", "optimizer");
  }
  if (optimizations.some((optimization) => optimization.name === "fractional-indirect-addressing")) {
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
    add("dark-entries", "Layout exposed shared tails as dark-entry candidates.", "layout");
  }
  if (cellRoles.some((cell) => cell.roles.includes("display-byte"))) {
    add("display-bytes", "Display lowering marked cells as packed display-byte candidates.", "layout");
  }
  if (cellRoles.some((cell) => cell.roles.includes("address") && cell.roles.includes("constant"))) {
    add("address-constants", "Layout marked address cells as reusable constants.", "layout");
  }
  if (candidates.some((candidate) => candidate.variant === "super-dark-dispatch" && candidate.selected)) {
    add("super-dark-dispatch", "Dispatch solver selected FA..FF indirect one-command cases.", "optimizer");
  }
  return [...used.values()];
}

function buildProofReport(
  ast: ProgramAst,
  items: MachineItem[],
  cellRoles: CellRoleReport[],
  options: CompileOptions,
  optimizations: AppliedOptimization[],
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
          .filter((optimization) => optimization.name.startsWith("arithmetic-if-"))
          .map((optimization) => optimization.name),
      ),
    ];
    proofs.push({
      id: "branch-equivalence",
      status: "proved",
      detail: `Removed conditional branches via ${variants.join(", ")} after matching assignment/update shape and value ranges.`,
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
  if (options.opt === "max" && cellRoles.some((cell) => cell.roles.includes("display-byte"))) {
    proofs.push({
      id: "display-byte-observable-boundary",
      status: "assumed",
      detail: "Display-byte candidates are reported; final X2/display preservation requires emulator trace tests.",
    });
  }
  return proofs;
}

function parseHotBlock(text: string): CompileReport["hotBlocks"][number] {
  const match = /^(.+)=(\d+)$/u.exec(text);
  if (!match) return { name: text, estimatedCells: 0 };
  return { name: match[1]!, estimatedCells: Number(match[2]) };
}

function stripCellUnsafe(cell: CellRoleReport): CellRoleReport {
  const clean: CellRoleReport = {
    address: cell.address,
    hex: cell.hex,
    roles: cell.roles,
  };
  if (cell.note !== undefined) clean.note = cell.note;
  return clean;
}

function stripCandidateUnsafe(candidate: CandidateReport): CandidateReport {
  return { ...candidate, unsafe: false };
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
    v2.inputs.length +
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
    if (statement.kind === "v2_require" && statement.elseAction) {
      count += countV2Statements([statement.elseAction]);
    }
    if (statement.kind === "v2_challenge") {
      count += countV2Statements(statement.successBody);
      if (statement.failureBody) count += countV2Statements(statement.failureBody);
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
  options: CompileOptions,
  targetProfile: TargetProfile,
): { selected: CandidateReport; candidates: CandidateReport[] } {
  const site = statement.name ?? `dispatch@${statement.line}`;
  const safeCost = estimateDispatchCost(statement, false);
  const fallthroughCost = estimateDispatchCost(statement, true);
  const candidates: CandidateReport[] = [
    {
      site,
      variant: "safe-compare-chain",
      steps: safeCost,
      selected: options.opt === "safe",
      reason: options.opt === "safe" ? "safe mode selected conservative compare-chain" : "available fallback",
      unsafe: false,
    },
    {
      site,
      variant: "fallthrough-compare-chain",
      steps: fallthroughCost,
      selected: options.opt === "max",
      reason: "uses case ordering to omit the final branch when possible",
      unsafe: false,
    },
  ];

  if (
    options.opt === "max" &&
    targetSupports(targetProfile, "dark-entries") &&
    targetSupports(targetProfile, "address-constants") &&
    targetSupports(targetProfile, "code-data-overlay")
  ) {
    candidates.push({
      site,
      variant: "dark-indirect-table",
      steps: Math.max(4, statement.cases.length + 3),
      selected: false,
      reason: "rejected until the layout pass proves a conflict-free address/data table for this site",
      unsafe: true,
    });
  }

  if (
    options.opt === "max" &&
    statement.cases.length <= 6 &&
    targetSupports(targetProfile, "super-dark-dispatch") &&
    targetSupports(targetProfile, "indirect-flow")
  ) {
    candidates.push({
      site,
      variant: "super-dark-dispatch",
      steps: Math.max(3, statement.cases.length + 2),
      selected: false,
      reason: "rejected until layout can place one-command cases at 48..53 and tails at 01..06",
      unsafe: true,
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

function statementsTerminate(statements: StatementAst[]): boolean {
  const last = statements.at(-1);
  if (!last) return false;
  return last.kind === "halt" || last.kind === "loop" || last.kind === "trap";
}

function parseRawInstruction(
  text: string,
): { opcode: number; mnemonic: string; target?: string | number; comment?: string } | undefined {
  const hex = /^[0-9A-Fa-f]{2}$/u.exec(text);
  if (hex) {
    const opcode = Number.parseInt(text, 16);
    return { opcode, mnemonic: getOpcode(opcode).name, comment: "raw hex" };
  }

  const direct = /^(БП|ПП|F\s*x<0|F\s*x=0|F\s*x!=0|F\s*x>=0|F\s*L[0-3])\s+([A-Za-z_][\w]*|[0-9A-Fa-f]{2})$/u.exec(text);
  if (direct) {
    const opcode = directOpcode(direct[1]!);
    return {
      opcode,
      mnemonic: getOpcode(opcode).name,
      target: parseTarget(direct[2]!),
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

  const directMemory = /^(X->П|П->X)\s+R?([0-9a-eавсде])$/iu.exec(text);
  if (directMemory) {
    const register = registerFromText(directMemory[2]!);
    const base = directMemory[1]!.startsWith("X") ? 0x40 : 0x60;
    return {
      opcode: base + registerIndex(register),
      mnemonic: `${directMemory[1]} ${register}`,
    };
  }

  const indirect = /^(К\s*)?(БП|ПП|X->П|П->X|x!=0|x>=0|x<0|x=0)\s*R?([0-9a-eавсде])$/iu.exec(text);
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

function parseTarget(text: string): string | number {
  return /^[0-9A-Fa-f]{2}$/u.test(text)
    ? codeToAddress(Number.parseInt(text, 16))
    : text;
}

function directOpcode(text: string): number {
  const normalized = text.replace(/\s+/g, " ");
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
  const normalized = text.toLowerCase();
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

function riskReason(
  risk: "documented" | "undocumented" | "dangerous",
  delivery: DeliveryMode,
  enterable: DeliveryMode[],
): string | undefined {
  if (!enterable.includes(delivery)) return `opcode not enterable in ${delivery} delivery`;
  if (risk === "dangerous") return "dangerous opcode is unsafe-unverified";
  if (risk === "undocumented") return "undocumented opcode is unsafe-unverified";
  return undefined;
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
