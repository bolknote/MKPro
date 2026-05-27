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
export type OptLevel = "safe" | "max";
export type OutputMode = "listing" | "hex" | "json" | "all";
export type TargetProfileId = "mk61_exact";

export interface CompileOptions {
  opt: OptLevel;
  delivery: DeliveryMode;
  budget: number;
  warnUnsafe: boolean;
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
  unsafe: boolean;
}

export type AllowFeature =
  | "undocumented"
  | "dark_entries"
  | "code_data_overlay"
  | "address_constants"
  | "display_bytes"
  | "x2_clobber"
  | "extra_cells"
  | "error_stops";

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
  machine: "mk61";
  targetProfile: TargetProfileId;
  budget?: number;
  benchmark?: string;
  optimize?: "size";
  v2?: V2ProgramAst;
  preloads: PreloadAst[];
  domains: DomainAst[];
  allows: AllowFeature[];
  states: StateAst[];
  displays: DisplayAst[];
  declarations: DeclarationAst[];
  entries: EntryAst[];
  procs: ProcAst[];
  blocks: BlockAst[];
}

export interface GameIntent {
  kind: "game_intent";
  name: string;
  benchmark?: string;
  inputs: string[];
  stateRoles: GameStateRole[];
  domains: GameDomainIntent[];
  screens: string[];
  rules: string[];
  terminalOutcomes: string[];
}

export interface GameStateRole {
  name: string;
  role: "input" | "coord" | "bitset" | "resource" | "flag" | "scratch" | "unknown";
  displayed: boolean;
  persistent: boolean;
}

export interface GameDomainIntent {
  kind: string;
  name?: string;
  facts: Record<string, string>;
}

export interface EffectIrOp {
  id: string;
  op: string;
  reads: string[];
  writes: string[];
  stack: Array<"X" | "Y" | "Z" | "T" | "X2">;
  displayObservable: boolean;
  mayTrap: boolean;
}

export interface CandidateIr {
  site: string;
  variant: string;
  cost: number;
  preconditions: string[];
  proofs: string[];
  features: string[];
  selected: boolean;
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

export type StateFieldType = "digit" | "flag" | "range" | "packed" | "resource" | "addr";

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
  initialInput?: "X" | "Y";
  line: number;
}

export interface DisplayAst {
  kind: "display";
  name: string;
  format: "packed";
  mode?: string;
  sources: string[];
  line: number;
}

export type SemanticHint =
  | "hot"
  | "rare"
  | "cold"
  | "displayed"
  | "hidden"
  | "temporary"
  | "persistent"
  | "wrap"
  | "saturate"
  | "trap"
  | "unordered"
  | "approx"
  | "exact"
  | "manual_entry"
  | "preload";

export interface V2ProgramAst {
  kind: "v2_program";
  name: string;
  inputs: V2InputAst[];
  state: V2StateFieldAst[];
  screens: V2ScreenAst[];
  turn?: V2TurnAst;
  rules: V2RuleAst[];
  line: number;
}

export interface V2InputAst {
  kind: "v2_input";
  name: string;
  inputType: "digit" | "number";
  hints: SemanticHint[];
  line: number;
}

export interface V2StateFieldAst {
  kind: "v2_state_field";
  name: string;
  type: "digit" | "flag" | "counter" | "coord" | "bitset" | "enum" | "packed" | "addr" | "resource";
  spec?: string;
  min?: number;
  max?: number;
  optional: boolean;
  generated?: "random";
  initial?: string;
  hints: SemanticHint[];
  line: number;
}

export interface V2ScreenAst {
  kind: "v2_screen";
  name: string;
  sources: string[];
  style: string[];
  hints: SemanticHint[];
  line: number;
}

export interface V2TurnAst {
  kind: "v2_turn";
  body: V2StatementAst[];
  hints: SemanticHint[];
  line: number;
}

export interface V2RuleAst {
  kind: "v2_rule";
  name: string;
  params: string[];
  body: V2StatementAst[];
  hints: SemanticHint[];
  line: number;
}

export type V2StatementAst =
  | V2ShowStatementAst
  | V2ReadStatementAst
  | V2StopStatementAst
  | V2LetStatementAst
  | V2IfStatementAst
  | V2RequireStatementAst
  | V2ChallengeStatementAst
  | V2MatchStatementAst
  | V2InvokeStatementAst
  | V2AssignStatementAst
  | V2UpdateStatementAst
  | V2CollectionStatementAst
  | V2RewardStatementAst
  | V2RawStatementAst;

export interface V2ShowStatementAst {
  kind: "v2_show";
  target: string;
  hints: SemanticHint[];
  line: number;
}

export interface V2ReadStatementAst {
  kind: "v2_read";
  target: string;
  hints: SemanticHint[];
  line: number;
}

export interface V2StopStatementAst {
  kind: "v2_stop";
  value: string;
  hints: SemanticHint[];
  line: number;
}

export interface V2LetStatementAst {
  kind: "v2_let";
  name: string;
  expr: string;
  hints: SemanticHint[];
  line: number;
}

export interface V2IfStatementAst {
  kind: "v2_if";
  predicate: V2PredicateAst;
  thenBody: V2StatementAst[];
  elseBody?: V2StatementAst[];
  hints: SemanticHint[];
  line: number;
}

export interface V2RequireStatementAst {
  kind: "v2_require";
  predicate: V2PredicateAst;
  elseAction?: V2StatementAst;
  hints: SemanticHint[];
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
  hints: SemanticHint[];
  line: number;
}

export interface V2InvokeStatementAst {
  kind: "v2_invoke";
  name: string;
  args: string[];
  hints: SemanticHint[];
  line: number;
}

export interface V2AssignStatementAst {
  kind: "v2_assign";
  target: string;
  expr: string;
  hints: SemanticHint[];
  line: number;
}

export interface V2UpdateStatementAst {
  kind: "v2_update";
  target: string;
  op: "+=" | "-=";
  expr: string;
  hints: SemanticHint[];
  line: number;
}

export interface V2CollectionStatementAst {
  kind: "v2_collection";
  collection: string;
  op: "clear" | "set";
  item: string;
  hints: SemanticHint[];
  line: number;
}

export interface V2RewardStatementAst {
  kind: "v2_reward";
  expr: string;
  hints: SemanticHint[];
  line: number;
}

export interface V2MatchStatementAst {
  kind: "v2_match";
  expr: string;
  cases: V2MatchCaseAst[];
  otherwise?: V2StatementAst;
  hints: SemanticHint[];
  line: number;
}

export interface V2MatchCaseAst {
  values: string[];
  action: V2StatementAst;
  hints: SemanticHint[];
  line: number;
}

export interface V2RawStatementAst {
  kind: "v2_raw";
  text: string;
  hints: SemanticHint[];
  line: number;
}

export type V2PredicateAst =
  | V2ComparePredicateAst
  | V2ExistsPredicateAst
  | V2CollectionHasPredicateAst
  | V2RawPredicateAst;

export interface V2ComparePredicateAst {
  kind: "v2_compare";
  left: string;
  op: "==" | "!=" | "<" | "<=" | ">" | ">=";
  right: string;
}

export interface V2ExistsPredicateAst {
  kind: "v2_exists";
  target: string;
}

export interface V2CollectionHasPredicateAst {
  kind: "v2_collection_has";
  collection: string;
  item: string;
}

export interface V2RawPredicateAst {
  kind: "v2_raw_predicate";
  text: string;
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
  inputType: "digit";
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

export interface TrapStatementAst {
  kind: "trap";
  trap: "zero" | "nonpositive" | "negative" | "gt_one" | "ge_100";
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
  unsafeReason?: string;
  raw?: boolean;
}

export interface MachineAddressRef {
  kind: "address";
  target: string | number;
  comment?: string;
  sourceLine?: number;
  unsafeReason?: string;
}

export interface ResolvedStep {
  address: number;
  opcode: number;
  hex: string;
  mnemonic: string;
  comment?: string;
  unsafeReason?: string;
}

export interface CompileReport {
  steps: number;
  budget: number;
  targetProfile: TargetProfileId;
  registers: Record<string, RegisterName>;
  labels: Record<string, string>;
  optimizations: AppliedOptimization[];
  warnings: string[];
  unsafeUnverified: string[];
  delivery: DeliveryMode;
  opt: OptLevel;
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
}

export interface ReferenceReport {
  name: string;
  referenceSteps: number;
  compiledSteps: number;
  delta: number;
  parity: "smaller" | "equal" | "larger";
}

export interface OptimizerReport {
  automatic: boolean;
  active: number;
  candidate: number;
  blocked: number;
  planned: number;
  capabilities: OptimizerCapabilityReport[];
}

export interface OptimizerCapabilityReport {
  id: string;
  category: "stack" | "flow" | "layout" | "data" | "display" | "trap" | "verification";
  source: "documented" | "mk61-delta" | "undocumented";
  status: "active" | "candidate" | "blocked" | "planned";
  unsafe: boolean;
  detail: string;
  requires: string[];
}

export interface IrReport {
  v1: boolean;
  v2: boolean;
  allowed: AllowFeature[];
  intentNodes: number;
  effectOps: number;
  layoutCells: number;
}

export type CellRole = "exec" | "address" | "constant" | "display-byte" | "overlay" | "dark-entry";

export interface CellRoleReport {
  address: string;
  hex: string;
  roles: CellRole[];
  note?: string;
  unsafe?: boolean;
}

export interface CandidateReport {
  site: string;
  variant: string;
  steps: number;
  selected: boolean;
  reason: string;
  unsafe: boolean;
}

export interface MachineFeatureUseReport {
  id: string;
  source: "target-profile" | "optimizer" | "layout" | "emulator";
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
