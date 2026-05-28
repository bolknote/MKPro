export type RegisterName =
  | "0"
  | "1"
  | "2"
  | "3"
  | "4"
  | "5"
  | "6"
  | "7"
  | "8"
  | "9"
  | "a"
  | "b"
  | "c"
  | "d"
  | "e";

export type DeliveryMode = "manual" | "loader" | "hex";
export type OutputMode = "listing" | "hex" | "json" | "all";
export type MachineId = "mk61";

export interface CompileOptions {
  delivery: DeliveryMode;
  budget: number;
  analysis: boolean;
  // Cell-count threshold above which the post-layout indirect-flow rescue may
  // trade a direct branch (2 cells) for a preloaded indirect one (1 cell + a
  // setup-time register preload). Defaults to the official 105-cell window so
  // in-budget programs keep their direct branches; lower it to rescue space
  // more aggressively.
  indirectFlowRescueAbove?: number;
  // Test-only: disable the interprocedural value-propagation and dead-store
  // passes so a differential test can compare optimized vs unoptimized output.
  disableInterproceduralOpts?: boolean;
}

export interface Diagnostic {
  level: "warning" | "error";
  message: string;
  code?: string;
  line?: number;
}

export interface AppliedOptimization {
  name: string;
  detail: string;
}

export interface OpcodeInfo {
  code: number;
  hex: string;
  name: string;
  keys: string;
  enterable: DeliveryMode[];
  takesAddress: boolean;
  x2Effect: "affects" | "restores" | "preserves" | "unknown";
  risk: "documented" | "undocumented" | "dangerous";
}

export interface ProgramAst {
  reference?: string;
  v2?: V2ProgramAst;
  preloads: PreloadAst[];
  domains: DomainAst[];
  states: StateAst[];
  displays: DisplayAst[];
  declarations: DeclarationAst[];
  entries: EntryAst[];
  procs: ProcAst[];
  blocks: BlockAst[];
}

export interface LayoutIrCell {
  address: number;
  opcode: number;
  roles: CellRole[];
  tactic: string;
}

export type DeclarationAst =
  | StoreDeclarationAst
  | TempDeclarationAst
  | ConstDeclarationAst;

export interface StoreDeclarationAst {
  kind: "store";
  name: string;
  value?: ExpressionAst;
  storage?: StorageHint;
  line: number;
}

export interface TempDeclarationAst {
  kind: "temp";
  name: string;
  storage?: StorageHint;
  line: number;
}

export interface ConstDeclarationAst {
  kind: "const";
  name: string;
  value: ExpressionAst;
  line: number;
}

export interface PreloadAst {
  kind: "preload";
  register: string;
  value: string;
  line: number;
}

export interface DomainAst {
  kind: "domain";
  domainKind: string;
  name?: string;
  header: string;
  lines: RawBlockLine[];
  line: number;
}

export interface StorageHint {
  mode: "prefer" | "fixed";
  register: RegisterName;
}

export interface EntryAst {
  kind: "entry";
  name: string;
  at?: number;
  body: StatementAst[];
  line: number;
}

export interface ProcAst {
  kind: "proc";
  name: string;
  body: StatementAst[];
  line: number;
}

export type StateFieldType = "flag" | "range" | "packed" | "addr";

export interface StateAst {
  kind: "state";
  name: string;
  fields: StateFieldAst[];
  line: number;
}

export interface StateFieldAst {
  name: string;
  type: StateFieldType;
  min?: number;
  max?: number;
  initial?: ExpressionAst;
  initialStack?: "X" | "Y";
  line: number;
}

export interface DisplayAst {
  kind: "display";
  name: string;
  format: "packed";
  sources: string[];
  items: DisplayItemAst[];
  line: number;
}

export type DisplayItemAst =
  | DisplayLiteralItemAst
  | DisplaySourceItemAst;

export interface DisplayLiteralItemAst {
  kind: "literal";
  text: string;
  line: number;
}

export interface DisplaySourceItemAst {
  kind: "source";
  name: string;
  line: number;
}

export interface V2ProgramAst {
  kind: "v2_program";
  name: string;
  state: V2StateFieldAst[];
  screens: V2ScreenAst[];
  boards: V2BoardAst[];
  worlds: V2WorldAst[];
  encounters: V2EncounterTableAst[];
  turn?: V2TurnAst;
  rules: V2RuleAst[];
  line: number;
}

export interface V2StateFieldAst {
  kind: "v2_state_field";
  name: string;
  type: "flag" | "counter" | "coord" | "cells" | "packed";
  domain?: string;
  min?: number;
  max?: number;
  initial?: string;
  line: number;
}

export interface V2BoardAst {
  kind: "v2_board";
  name: string;
  xMin: number;
  xMax: number;
  yMin: number;
  yMax: number;
  width: number;
  height: number;
  line: number;
}

export interface V2WorldAst {
  kind: "v2_world";
  name: string;
  position?: V2WorldPositionAst;
  line: number;
}

export interface V2WorldPositionAst {
  name: string;
  encoding?: string;
  line: number;
}

export interface V2EncounterTableAst {
  kind: "v2_encounters";
  expr: string;
  cases: V2EncounterCaseAst[];
  line: number;
}

export interface V2EncounterCaseAst {
  value: string;
  body: V2StatementAst[];
  line: number;
}

export interface V2ScreenAst {
  kind: "v2_screen";
  name: string;
  sources: string[];
  items: DisplayItemAst[];
  line: number;
}

export interface V2TurnAst {
  kind: "v2_turn";
  body: V2StatementAst[];
  line: number;
}

export interface V2RuleAst {
  kind: "v2_rule";
  name: string;
  params: string[];
  body: V2StatementAst[];
  line: number;
}

export type V2StatementAst =
  | V2ShowStatementAst
  | V2ReadStatementAst
  | V2StopStatementAst
  | V2IfStatementAst
  | V2ChallengeStatementAst
  | V2MoveStatementAst
  | V2MatchStatementAst
  | V2InvokeStatementAst
  | V2AssignStatementAst
  | V2UpdateStatementAst
  | V2RawStatementAst;

export interface V2ShowStatementAst {
  kind: "v2_show";
  target: string;
  line: number;
}

export interface V2ReadStatementAst {
  kind: "v2_read";
  target: string;
  line: number;
}

export interface V2StopStatementAst {
  kind: "v2_stop";
  value: string;
  line: number;
}

export interface V2IfStatementAst {
  kind: "v2_if";
  predicate: V2PredicateAst;
  thenBody: V2StatementAst[];
  elseBody?: V2StatementAst[];
  line: number;
}

export interface V2ChallengeStatementAst {
  kind: "v2_challenge";
  expr: string;
  successBody: V2StatementAst[];
  failureBody?: V2StatementAst[];
  challengeTarget: string;
  warningScreen: string;
  memoryScreen: string;
  answerInput: string;
  line: number;
}

export interface V2MoveStatementAst {
  kind: "v2_move";
  target: string;
  direction?: "north" | "south" | "east" | "west" | "up" | "down";
  line: number;
}

export interface V2InvokeStatementAst {
  kind: "v2_invoke";
  name: string;
  args: string[];
  line: number;
}

export interface V2AssignStatementAst {
  kind: "v2_assign";
  target: string;
  expr: string;
  line: number;
}

export interface V2UpdateStatementAst {
  kind: "v2_update";
  target: string;
  op: "+=" | "-=";
  expr: string;
  line: number;
}

export type RawStackSlot = "X" | "Y" | "Z" | "T";

export interface V2RawInputAst {
  slot: RawStackSlot;
  expr: string;
  line: number;
}

export interface V2RawOutputAst {
  slot: "X";
  target: string;
  line: number;
}

export interface V2RawStatementAst {
  kind: "v2_raw";
  inputs: V2RawInputAst[];
  outputs: V2RawOutputAst[];
  clobbers: string[];
  preserves: string[];
  lines: RawBlockLine[];
  line: number;
}

export interface V2MatchStatementAst {
  kind: "v2_match";
  expr: string;
  cases: V2MatchCaseAst[];
  otherwise?: V2StatementAst;
  line: number;
}

export interface V2MatchCaseAst {
  values: string[];
  action: V2StatementAst;
  line: number;
}

export type V2PredicateAst = V2ComparePredicateAst | V2ContainsPredicateAst;

export interface V2ComparePredicateAst {
  kind: "v2_compare";
  left: string;
  op: "==" | "!=" | "<" | "<=" | ">" | ">=";
  right: string;
}

export interface V2ContainsPredicateAst {
  kind: "v2_contains";
  collection: string;
  item: string;
}

export interface BlockAst {
  kind: "block";
  name: string;
  mode: "inline" | "tail" | "shared_tail";
  body: StatementAst[];
  line: number;
}

export type StatementAst =
  | PauseStatementAst
  | AskStatementAst
  | InputStatementAst
  | HaltStatementAst
  | AssignStatementAst
  | LoopStatementAst
  | IfStatementAst
  | SwitchStatementAst
  | DispatchStatementAst
  | ShowStatementAst
  | CallBlockStatementAst
  | CoreStatementAst
  | EggStatementAst
  | TrapStatementAst;

export interface PauseStatementAst {
  kind: "pause";
  expr: ExpressionAst;
  line: number;
}

export interface AskStatementAst {
  kind: "ask";
  target: string;
  prompt?: ExpressionAst;
  line: number;
}

export interface InputStatementAst {
  kind: "input";
  target: string;
  line: number;
}

export interface HaltStatementAst {
  kind: "halt";
  expr: ExpressionAst;
  line: number;
}

export interface AssignStatementAst {
  kind: "assign";
  target: string;
  expr: ExpressionAst;
  line: number;
}

export interface LoopStatementAst {
  kind: "loop";
  body: StatementAst[];
  line: number;
}

export interface IfStatementAst {
  kind: "if";
  condition: ConditionAst;
  thenBody: StatementAst[];
  elseBody?: StatementAst[];
  line: number;
}

export interface SwitchStatementAst {
  kind: "switch";
  expr: ExpressionAst;
  cases: SwitchCaseAst[];
  defaultBody?: StatementAst[];
  line: number;
  scratchId: number;
}

export interface SwitchCaseAst {
  value: ExpressionAst;
  body: StatementAst[];
  line: number;
}

export interface DispatchStatementAst {
  kind: "dispatch";
  expr: ExpressionAst;
  name?: string;
  cases: DispatchCaseAst[];
  defaultBody?: StatementAst[];
  line: number;
  scratchId: number;
}

export interface DispatchCaseAst {
  value: ExpressionAst;
  body: StatementAst[];
  line: number;
}

export interface ShowStatementAst {
  kind: "show";
  display: string;
  line: number;
}

export interface CallBlockStatementAst {
  kind: "call";
  block: string;
  line: number;
}

export interface CoreStatementAst {
  kind: "core";
  lines: RawBlockLine[];
  inputs?: RawInputAst[];
  outputs?: RawOutputAst[];
  clobbers?: string[];
  preserves?: string[];
  strict?: boolean;
  line: number;
}

export interface EggStatementAst {
  kind: "egg";
  lines: RawBlockLine[];
  line: number;
}

export interface RawBlockLine {
  text: string;
  line: number;
}

export interface RawInputAst {
  slot: RawStackSlot;
  expr: ExpressionAst;
  line: number;
}

export interface RawOutputAst {
  slot: "X";
  target: string;
  line: number;
}

export interface TrapStatementAst {
  kind: "trap";
  trap: "zero" | "nonpositive" | "negative" | "gt_one" | "ge_100" | "frac_ge_06";
  expr: ExpressionAst;
  line: number;
}

export interface ConditionAst {
  left: ExpressionAst;
  op: "==" | "!=" | "<" | "<=" | ">" | ">=";
  right: ExpressionAst;
}

export type ExpressionAst =
  | NumberExpressionAst
  | IdentifierExpressionAst
  | UnaryExpressionAst
  | BinaryExpressionAst
  | CallExpressionAst;

export interface NumberExpressionAst {
  kind: "number";
  raw: string;
}

export interface IdentifierExpressionAst {
  kind: "identifier";
  name: string;
}

export interface UnaryExpressionAst {
  kind: "unary";
  op: "-";
  expr: ExpressionAst;
}

export interface BinaryExpressionAst {
  kind: "binary";
  op: "+" | "-" | "*" | "/";
  left: ExpressionAst;
  right: ExpressionAst;
}

export interface CallExpressionAst {
  kind: "call";
  callee: string;
  args: ExpressionAst[];
}

export type MachineItem = MachineLabel | MachineOp | MachineAddressRef;

export interface MachineLabel {
  kind: "label";
  name: string;
}

export interface MachineOp {
  kind: "op";
  opcode: number;
  mnemonic: string;
  comment?: string;
  sourceLine?: number;
  raw?: boolean;
}

export interface MachineAddressRef {
  kind: "address";
  target: string | number;
  comment?: string;
  sourceLine?: number;
  formalOpcode?: number;
}

export interface IrMeta {
  mnemonic: string;
  comment?: string;
  sourceLine?: number;
  raw?: boolean;
  roles?: readonly CellRole[];
  tactic?: string;
}

export interface IrTargetMeta {
  comment?: string;
  sourceLine?: number;
  roles?: readonly CellRole[];
  formalOpcode?: number;
}

export type IrCondition = "==0" | "!=0" | "<0" | ">=0";

export type IrLoopCounter = "L0" | "L1" | "L2" | "L3";

export type IrStopSemantic = "halt" | "pause" | "show" | "ask" | "input" | "unknown";

export interface IrLabel {
  kind: "label";
  name: string;
}

export interface IrStore {
  kind: "store";
  register: RegisterName;
  opcode: number;
  meta: IrMeta;
}

export interface IrRecall {
  kind: "recall";
  register: RegisterName;
  opcode: number;
  meta: IrMeta;
}

export interface IrIndirectStore {
  kind: "indirect-store";
  register: RegisterName;
  opcode: number;
  meta: IrMeta;
}

export interface IrIndirectRecall {
  kind: "indirect-recall";
  register: RegisterName;
  opcode: number;
  meta: IrMeta;
}

export interface IrJump {
  kind: "jump";
  target: string | number;
  opcode: number;
  meta: IrMeta;
  targetMeta: IrTargetMeta;
}

export interface IrCondJump {
  kind: "cjump";
  condition: IrCondition;
  target: string | number;
  opcode: number;
  meta: IrMeta;
  targetMeta: IrTargetMeta;
}

export interface IrCall {
  kind: "call";
  target: string | number;
  opcode: number;
  meta: IrMeta;
  targetMeta: IrTargetMeta;
}

export interface IrLoop {
  kind: "loop";
  counter: IrLoopCounter;
  target: string | number;
  opcode: number;
  meta: IrMeta;
  targetMeta: IrTargetMeta;
}

export interface IrIndirectJump {
  kind: "indirect-jump";
  register: RegisterName;
  opcode: number;
  meta: IrMeta;
}

export interface IrIndirectCall {
  kind: "indirect-call";
  register: RegisterName;
  opcode: number;
  meta: IrMeta;
}

export interface IrIndirectCondJump {
  kind: "indirect-cjump";
  condition: IrCondition;
  register: RegisterName;
  opcode: number;
  meta: IrMeta;
}

export interface IrReturn {
  kind: "return";
  opcode: number;
  meta: IrMeta;
}

export interface IrStop {
  kind: "stop";
  opcode: number;
  semantic: IrStopSemantic;
  meta: IrMeta;
}

export interface IrPlainOp {
  kind: "plain";
  opcode: number;
  meta: IrMeta;
}

export interface IrOrphanAddress {
  kind: "orphan-address";
  target: string | number;
  meta: IrTargetMeta;
}

export type IrOp =
  | IrLabel
  | IrStore
  | IrRecall
  | IrIndirectStore
  | IrIndirectRecall
  | IrJump
  | IrCondJump
  | IrCall
  | IrLoop
  | IrIndirectJump
  | IrIndirectCall
  | IrIndirectCondJump
  | IrReturn
  | IrStop
  | IrPlainOp
  | IrOrphanAddress;

export interface ResolvedStep {
  address: number;
  opcode: number;
  hex: string;
  mnemonic: string;
  comment?: string;
}

export interface CompileReport {
  steps: number;
  budget: number;
  machine: MachineId;
  registers: Record<string, RegisterName>;
  labels: Record<string, string>;
  optimizations: AppliedOptimization[];
  warnings: string[];
  delivery: DeliveryMode;
  optimizer: OptimizerReport;
  preloads: PreloadReport[];
  reference?: ReferenceReport;
  ir: IrReport;
  cellRoles: CellRoleReport[];
  candidates: CandidateReport[];
  budgetReport: BudgetReport;
  machineFeaturesUsed: MachineFeatureUseReport[];
  proofs: ProofReport[];
  emulatorFacts: EmulatorFactReport[];
  rejectedCandidates: RejectedCandidateReport[];
  hotBlocks: HotBlockReport[];
}

export interface PreloadReport {
  register: string;
  value: string;
  countsAgainstProgram: boolean;
  setupProgram?: string;
  setupNote?: string;
}

export interface ReferenceReport {
  name: string;
  referenceSteps: number;
  referenceSpan: number;
  referenceEntries: number;
  referenceGaps: string[];
  compiledSteps: number;
  delta: number;
  parity: "smaller" | "equal" | "larger";
}

export interface OptimizerReport {
  automatic: boolean;
  active: number;
  considered: number;
  candidate: number;
  planned: number;
  capabilities: OptimizerCapabilityReport[];
}

export interface OptimizerCapabilityReport {
  id: string;
  category: "stack" | "flow" | "layout" | "data" | "display" | "trap" | "verification";
  source: "documented" | "mk61-delta" | "undocumented";
  status: "active" | "considered" | "candidate" | "planned";
  detail: string;
  requires: string[];
}

export interface IrReport {
  lowered: boolean;
  v2: boolean;
  intentNodes: number;
  effectOps: number;
  layoutCells: number;
}

export type CellRole = "exec" | "address" | "constant" | "display-byte" | "overlay" | "dark-entry" | "formal-address";

export interface CellRoleReport {
  address: string;
  hex: string;
  roles: CellRole[];
  note?: string;
}

export interface CandidateReport {
  site: string;
  variant: string;
  steps: number;
  selected: boolean;
  reason: string;
}

export interface MachineFeatureUseReport {
  id: string;
  source: "machine" | "optimizer" | "layout" | "emulator";
  detail: string;
}

export interface ProofReport {
  id: string;
  status: "proved" | "assumed" | "not-needed";
  detail: string;
}

export interface EmulatorFactReport {
  id: string;
  status: "probed" | "documented";
  detail: string;
}

export interface RejectedCandidateReport {
  site: string;
  variant: string;
  reason: string;
  steps: number;
}

export interface HotBlockReport {
  name: string;
  estimatedCells: number;
}

export interface BudgetReport {
  used: number;
  limit: number;
  remaining: number;
  exceeded: boolean;
  largestBlocks: string[];
  officialSteps: number;
  extraCells: number;
  totalPhysicalCells: number;
}

export interface CompileResult {
  ast: ProgramAst;
  items: MachineItem[];
  steps: ResolvedStep[];
  report: CompileReport;
  diagnostics: Diagnostic[];
}
