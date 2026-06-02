import type {
  ConditionAst,
  DisplayItemAst,
  DomainAst,
  DispatchCaseAst,
  DispatchStatementAst,
  DisplayAst,
  EntryAst,
  ExpressionAst,
  IfStatementAst,
  ProgramAst,
  ProcAst,
  RawBlockLine,
  RawStackSlot,
  StatementAst,
  StateAst,
  StateBankAst,
  StateFieldAst,
  StateFieldType,
  V2BoardAst,
  V2ConstAst,
  V2IfStatementAst,
  V2InvokeStatementAst,
  V2LoopStatementAst,
  V2MatchCaseAst,
  V2MatchStatementAst,
  V2PredicateAst,
  V2ProgramAst,
  V2RuleAst,
  V2ScreenAst,
  V2ShowStatementAst,
  V2StateFieldAst,
  V2StatementAst,
  V2WhileStatementAst,
  V2WorldAst,
} from "./types.ts";
import { buildV2ConstContext, inlineConstIdentifiers, type V2ConstBinding } from "./v2-const.ts";

interface SourceLine {
  text: string;
  line: number;
}

const V2_RESERVED_RULE_NAMES = new Set([
  "challenge",
  "else",
  "fn",
  "halt",
  "if",
  "loop",
  "match",
  "otherwise",
  "program",
  "read",
  "return",
  "rule",
  "screen",
  "show",
  "state",
  "stop",
  "turn",
  "world",
]);

export class ParseError extends Error {
  readonly line: number;

  constructor(message: string, line: number) {
    super(`${message} at line ${line}`);
    this.line = line;
  }
}

export interface ParseOptions {
  signedAbsMatchPairs?: boolean;
  synthesizeParametricSiblings?: boolean;
}

export function parseProgram(source: string, options: ParseOptions = {}): ProgramAst {
  return new MKProParser(source, options).parseProgram();
}

class MKProParser {
  private readonly lines: SourceLine[];
  private readonly options: ParseOptions;
  private index = 0;

  constructor(source: string, options: ParseOptions) {
    this.options = options;
    this.lines = source
      .split(/\r?\n/u)
      .flatMap((text, offset) => normalizeSourceLine(text, offset + 1))
      .filter((line) => line.text.length > 0);
  }

  parseProgram(): ProgramAst {
    let reference: string | undefined;
    let v2: V2ProgramAst | undefined;
    const domains: DomainAst[] = [];
    const states: StateAst[] = [];
    const displays: DisplayAst[] = [];
    const entries: EntryAst[] = [];
    const procs: ProcAst[] = [];
    let programBanks: ProgramAst["banks"];

    while (!this.done()) {
      const line = this.peek();
      if (line.text === "}") {
        throw new ParseError("Unexpected closing brace", line.line);
      }
      if (line.text.startsWith("reference ")) {
        reference = line.text.slice("reference ".length).trim();
        this.index += 1;
      } else if (line.text.startsWith("program ")) {
        if (v2 !== undefined) {
          throw new ParseError("Only one program block is supported", line.line);
        }
        v2 = this.parseV2Program();
        if (this.options.synthesizeParametricSiblings === true) {
          synthesizeV2ParametricSiblingRules(v2);
        }
        const lowered = lowerV2Program(v2, this.options);
        domains.push(...lowered.domains);
        states.push(...lowered.states);
        displays.push(...lowered.displays);
        entries.push(...lowered.entries);
        procs.push(...lowered.procs);
        if (lowered.banks !== undefined) {
          if (programBanks === undefined) programBanks = [];
          programBanks.push(...lowered.banks);
        }
      } else {
        throw new ParseError(`Unexpected top-level line '${line.text}'`, line.line);
      }
    }

    if (v2 === undefined) throw new ParseError("Program must contain one V2 program block", 1);

    const program: ProgramAst = {
      domains,
      states,
      displays,
      entries,
      procs,
    };
    if (programBanks !== undefined && programBanks.length > 0) program.banks = programBanks;
    if (reference !== undefined) program.reference = reference;
    if (v2 !== undefined) program.v2 = v2;
    return program;
  }

  private parseV2Program(): V2ProgramAst {
    const header = this.next();
    const match = /^program\s+([A-Za-z_][\w]*)\s*\{$/u.exec(header.text);
    if (!match) throw new ParseError("Program must look like 'program Name {'", header.line);
    const consts: V2ConstAst[] = [];
    const state: V2StateFieldAst[] = [];
    const boards: V2BoardAst[] = [];
    const worlds: V2WorldAst[] = [];
    const body: V2StatementAst[] = [];
    const rules: V2RuleAst[] = [];

    while (!this.done()) {
      const line = this.peek();
      if (line.text === "}") {
        this.index += 1;
        const program: V2ProgramAst = {
          kind: "v2_program",
          name: match[1]!,
          consts,
          state,
          boards,
          worlds,
          body,
          rules,
          line: header.line,
        };
        return program;
      }
      if (line.text.startsWith("const ")) {
        this.index += 1;
        consts.push(parseV2ConstLine(line));
        continue;
      }
      if (line.text === "state {") {
        this.index += 1;
        state.push(...this.parseV2StateBlock());
        continue;
      }
      const board = parseV2BoardDeclaration(line);
      if (board !== undefined) {
        this.index += 1;
        boards.push(board);
        continue;
      }
      const compactBoard = parseV2CompactBoardDeclaration(line);
      if (compactBoard !== undefined) {
        this.index += 1;
        worlds.push(compactBoard);
        continue;
      }
      if (line.text.startsWith("board ")) throw new ParseError("Board must look like 'name: board(0..9, 0..9)'", line.line);
      if (line.text.startsWith("fleet ")) throw new ParseError("Fleet blocks were removed; declare cells and counters in state", line.line);
      if (line.text.startsWith("world ")) {
        throw new ParseError("Use 'name: board(encoding)' instead of world blocks", line.line);
      }
      if (line.text.startsWith("encounters ")) {
        throw new ParseError("Use match blocks instead of encounters blocks", line.line);
      }
      if (line.text.startsWith("screen ")) {
        throw new ParseError("Use 'fn name() { show(...) }' instead of screen blocks", line.line);
      }
      if (line.text === "turn {") {
        throw new ParseError("Use 'loop {' instead of turn blocks", line.line);
      }
      if (line.text.startsWith("fn ")) {
        rules.push(this.parseV2Rule(line.text));
        continue;
      }
      if (line.text.startsWith("rule ")) {
        throw new ParseError("Use 'fn name(arg, ...) {' instead of rule blocks", line.line);
      }
      const knownStatementBlock = /^(match|if|while|loop)\b/u.test(line.text) || line.text === "raw {";
      if (line.text.startsWith("input ") || line.text.endsWith("{") && !knownStatementBlock) {
        throw new ParseError(`Unexpected program line '${line.text}'`, line.line);
      }
      body.push(this.parseV2Statement());
    }
    throw new ParseError("Unclosed program block", header.line);
  }

  private parseV2StateBlock(): V2StateFieldAst[] {
    const fields: V2StateFieldAst[] = [];
    while (!this.done()) {
      const line = this.peek();
      if (line.text === "}") {
        this.index += 1;
        return fields;
      }
      if (/^[A-Za-z_][\w]*\s*:\s*group\s*\(\s*-?\d+\.\.-?\d+\s*\)\s*\{$/u.test(line.text)) {
        fields.push(...this.parseV2StateGroup());
        continue;
      }
      this.index += 1;
      if (line.text === "}") return fields;
      fields.push(parseV2StateField(line));
    }
    throw new ParseError("Unclosed state block", this.lines.at(-1)?.line ?? 1);
  }

  private parseV2StateGroup(): V2StateFieldAst[] {
    const header = this.next();
    const match = /^([A-Za-z_][\w]*)\s*:\s*group\s*\(\s*(-?\d+)\.\.(-?\d+)\s*\)\s*\{$/u.exec(header.text);
    if (!match) throw new ParseError("State group must look like 'name: group(1..3) {'", header.line);
    const name = match[1]!;
    const min = Number(match[2]);
    const max = Number(match[3]);
    if (!Number.isInteger(min) || !Number.isInteger(max) || max < min) {
      throw new ParseError(`Invalid state group range '${match[2]}..${match[3]}'`, header.line);
    }

    const fields: V2StateFieldAst[] = [];
    while (!this.done()) {
      const line = this.peek();
      if (line.text === "}") {
        this.index += 1;
        return fields;
      }
      if (line.text.includes("group")) {
        throw new ParseError("Nested state groups are not supported", line.line);
      }
      this.index += 1;
      const field = parseV2StateField(line);
      if (field.bank !== undefined) {
        throw new ParseError("State group members cannot also be indexed arrays", line.line);
      }
      fields.push({
        ...field,
        name: bankFieldStateName(name, field.name),
        bank: { name, member: field.name, min, max },
      });
    }
    throw new ParseError("Unclosed state group", header.line);
  }

  private parseV2Rule(text: string): V2RuleAst {
    const header = this.next();
    const fn = /^fn\s+([A-Za-z_][\w]*)\s*\(([^)]*)\)\s*\{$/u.exec(text);
    if (!fn) {
      throw new ParseError("Function must look like 'fn name(arg, ...) {'", header.line);
    }
    const name = fn[1]!;
    if (V2_RESERVED_RULE_NAMES.has(name)) {
      throw new ParseError(`Function name '${name}' is reserved`, header.line);
    }
    const params = parseCommaIdentifierList(fn[2] ?? "", header.line);
    for (const param of params) {
      if (!/^[A-Za-z_][\w]*$/u.test(param)) {
        throw new ParseError(`Invalid function parameter '${param}'`, header.line);
      }
    }
    return {
      kind: "v2_rule",
      name,
      params,
      body: this.parseV2StatementBlock(),
      line: header.line,
    };
  }

  private parseV2StatementBlock(): V2StatementAst[] {
    const statements: V2StatementAst[] = [];
    while (!this.done()) {
      const line = this.peek();
      if (line.text === "}") {
        this.index += 1;
        return statements;
      }
      statements.push(this.parseV2Statement());
    }
    throw new ParseError("Unclosed statement block", this.lines.at(-1)?.line ?? 1);
  }

  private parseV2Statement(): V2StatementAst {
    const line = this.peek();
    if (line.text.startsWith("match ") && line.text.endsWith("{")) {
      this.index += 1;
      return this.parseV2Match(line.text, line.line);
    }
    if (line.text.startsWith("unless ") && line.text.endsWith("{")) {
      this.index += 1;
      return this.parseV2If({ text: `if ${line.text.slice("unless ".length)}`, line: line.line }, true);
    }
    if (line.text.startsWith("if ") && line.text.endsWith("{")) {
      this.index += 1;
      return this.parseV2If(line);
    }
    if (line.text === "else {" || line.text.startsWith("else if ")) {
      throw new ParseError("'else' without matching 'if'", line.line);
    }
    if (line.text.startsWith("while ") && line.text.endsWith("{")) {
      this.index += 1;
      return {
        kind: "v2_while",
        predicate: parseV2Predicate(line.text.slice("while ".length, -1).trim(), line.line),
        body: this.parseV2StatementBlock(),
        line: line.line,
      };
    }
    if (line.text === "loop {") {
      this.index += 1;
      return {
        kind: "v2_loop",
        body: this.parseV2StatementBlock(),
        line: line.line,
      };
    }
    if (line.text.startsWith("challenge ") && line.text.endsWith("{")) {
      throw new ParseError("Use ordinary show/read/if statements instead of challenge blocks", line.line);
    }
    if (line.text === "raw {") {
      this.index += 1;
      return this.parseV2Raw(line.line);
    }
    this.index += 1;
    return parseV2InlineStatement(line.text, line.line);
  }

  private parseV2Raw(line: number): V2StatementAst {
    const inputs: Extract<V2StatementAst, { kind: "v2_raw" }>["inputs"] = [];
    const outputs: Extract<V2StatementAst, { kind: "v2_raw" }>["outputs"] = [];
    let clobbers: string[] | undefined;
    let preserves: string[] | undefined;
    let code: RawBlockLine[] | undefined;

    while (!this.done()) {
      const bodyLine = this.peek();
      if (bodyLine.text === "}") {
        this.index += 1;
        if (code === undefined) throw new ParseError("Raw block must contain code { ... }", line);
        if (clobbers === undefined) throw new ParseError("Raw block must declare clobbers", line);
        if (preserves === undefined || !preserves.includes("state")) {
          throw new ParseError("Raw block must declare preserves state", line);
        }
        if (clobbers.includes("state")) {
          throw new ParseError("Raw block cannot clobber high-level state; return values through returns X -> name", line);
        }
        validateV2RawInputs(inputs, line);
        return {
          kind: "v2_raw",
          inputs,
          outputs,
          clobbers,
          preserves,
          lines: code,
          line,
        };
      }
      if (bodyLine.text.startsWith("takes ")) {
        this.index += 1;
        for (const input of parseV2RawInputs(bodyLine.text.slice("takes ".length), bodyLine.line)) {
          if (inputs.some((existing) => existing.slot === input.slot)) {
            throw new ParseError(`Raw input for ${input.slot} is declared more than once`, input.line);
          }
          inputs.push(input);
        }
        continue;
      }
      if (bodyLine.text.startsWith("returns ")) {
        this.index += 1;
        if (outputs.length > 0) throw new ParseError("Raw block currently supports one return value", bodyLine.line);
        outputs.push(parseV2RawOutput(bodyLine.text.slice("returns ".length), bodyLine.line));
        continue;
      }
      if (bodyLine.text.startsWith("clobbers ")) {
        this.index += 1;
        if (clobbers !== undefined) throw new ParseError("Raw block must declare clobbers only once", bodyLine.line);
        clobbers = parseV2RawContractList(bodyLine.text.slice("clobbers ".length), bodyLine.line);
        continue;
      }
      if (bodyLine.text.startsWith("preserves ")) {
        this.index += 1;
        if (preserves !== undefined) throw new ParseError("Raw block must declare preserves only once", bodyLine.line);
        preserves = parseV2RawContractList(bodyLine.text.slice("preserves ".length), bodyLine.line);
        continue;
      }
      if (bodyLine.text === "code {") {
        this.index += 1;
        if (code !== undefined) throw new ParseError("Raw block must contain only one code block", bodyLine.line);
        code = this.parseRawCodeBlock(bodyLine.line);
        continue;
      }
      throw new ParseError(`Unexpected raw line '${bodyLine.text}'`, bodyLine.line);
    }
    throw new ParseError("Unclosed raw block", line);
  }

  private parseRawCodeBlock(line: number): RawBlockLine[] {
    const lines: RawBlockLine[] = [];
    while (!this.done()) {
      const rawLine = this.next();
      if (rawLine.text === "}") return lines;
      lines.push({ text: rawLine.text, line: rawLine.line });
    }
    throw new ParseError("Unclosed raw code block", line);
  }

  private parseV2If(line: SourceLine, negated = false): V2IfStatementAst {
    const match = /^if\s+(.+)\s*\{$/u.exec(line.text);
    if (!match) throw new ParseError("If must look like 'if predicate {'", line.line);
    const statement: V2IfStatementAst = {
      kind: "v2_if",
      predicate: parseV2Predicate(match[1]!.trim(), line.line),
      thenBody: this.parseV2StatementBlock(),
      line: line.line,
    };
    if (negated === true) statement.negated = true;
    const elseBody = this.parseV2ElseBody();
    if (elseBody !== undefined) statement.elseBody = elseBody;
    return statement;
  }

  private parseV2ElseBody(): V2StatementAst[] | undefined {
    const next = this.peekOptional();
    if (next?.text === "else {") {
      this.index += 1;
      return this.parseV2StatementBlock();
    }
    if (next?.text.startsWith("else if ") && next.text.endsWith("{")) {
      this.index += 1;
      return [this.parseV2If({ text: next.text.slice("else ".length), line: next.line })];
    }
    return undefined;
  }

  private parseV2Match(text: string, line: number): V2MatchStatementAst {
    const match = /^match\s+(.+?)\s*\{$/u.exec(text);
    if (!match) throw new ParseError("Match must look like 'match expr {'", line);
    const cases: V2MatchCaseAst[] = [];
    let otherwise: V2StatementAst | undefined;
    while (!this.done()) {
      const bodyLine = this.next();
      if (bodyLine.text === "}") {
        const statement: V2MatchStatementAst = {
          kind: "v2_match",
          expr: match[1]!.trim(),
          cases,
          line,
        };
        if (otherwise !== undefined) statement.otherwise = otherwise;
        return statement;
      }
      const arrow = /^(.+?)\s*=>\s*(.+)$/u.exec(bodyLine.text);
      if (!arrow) throw new ParseError("Match cases must look like 'value => action'", bodyLine.line);
      const left = arrow[1]!.trim();
      const action = parseV2InlineStatement(arrow[2]!.trim(), bodyLine.line);
      if (left === "otherwise") {
        otherwise = action;
      } else {
        cases.push({
          values: left.split(",").map((part) => part.trim()).filter(Boolean),
          action,
          line: bodyLine.line,
        });
      }
    }
    throw new ParseError("Unclosed match block", line);
  }

  private done(): boolean {
    return this.index >= this.lines.length;
  }

  private peek(): SourceLine {
    const line = this.lines[this.index];
    if (!line) throw new ParseError("Unexpected end of file", this.lines.at(-1)?.line ?? 1);
    return line;
  }

  private peekOptional(): SourceLine | undefined {
    return this.lines[this.index];
  }

  private next(): SourceLine {
    const line = this.peek();
    this.index += 1;
    return line;
  }
}

function parseV2StateField(line: SourceLine): V2StateFieldAst {
  const match = /^([A-Za-z_][\w]*)\s*:\s*([A-Za-z_][\w]*)(?:\[\s*(-?\d+)\.\.(-?\d+)\s*\])?(?:\(([^)]*)\))?(.*)$/u.exec(line.text);
  if (!match) {
    throw new ParseError("State field must look like 'name: counter 0..9 = 0', 'name: packed[1..3] = 0', or 'name: cells(domain) = random()'", line.line);
  }
  const typeText = match[2]!.toLowerCase();
  if (!["flag", "counter", "coord", "cells", "coord_list", "packed"].includes(typeText)) {
    throw new ParseError(`Unknown state type '${match[2]}'`, line.line);
  }
  const bankMin = match[3] === undefined ? undefined : Number(match[3]);
  const bankMax = match[4] === undefined ? undefined : Number(match[4]);
  if ((bankMin === undefined) !== (bankMax === undefined)) {
    throw new ParseError("Indexed state range must look like '[1..3]'", line.line);
  }
  if (bankMin !== undefined && (!Number.isInteger(bankMin) || !Number.isInteger(bankMax) || bankMax! < bankMin)) {
    throw new ParseError(`Invalid indexed state range '${match[3]}..${match[4]}'`, line.line);
  }
  if (bankMin !== undefined && typeText === "coord_list") {
    throw new ParseError("coord_list cannot be declared as an indexed state bank", line.line);
  }
  const args = match[5]?.trim();
  const argList = args === undefined ? [] : splitArgs(args);
  if (typeText === "cells" && (!args || argList.length !== 1)) {
    throw new ParseError("cells state must look like 'name: cells(domain) = random()'", line.line);
  }
  if (typeText === "coord" && (!args || argList.length !== 1)) {
    throw new ParseError("coord state must look like 'name: coord(domain)'", line.line);
  }
  if (typeText === "coord_list" && (!args || argList.length !== 2)) {
    throw new ParseError("coord_list state must look like 'name: coord_list(domain, count)'", line.line);
  }
  if (!["cells", "coord", "coord_list"].includes(typeText) && args !== undefined) {
    throw new ParseError(`State type '${match[2]}' does not take parameters`, line.line);
  }
  const tail = match[6]!.trim();
  const tailMatch = /^(?:(-?\d+)\.\.(-?\d+))?(?:\s*=\s*(.+))?$/u.exec(tail);
  if (!tailMatch) {
    throw new ParseError("State field must look like 'name: counter 0..9 = 0' or 'name: cells(domain) = random()'", line.line);
  }
  if (typeText === "counter" && tailMatch[1] === undefined) {
    throw new ParseError("counter state must look like 'name: counter 0..9 = 0'", line.line);
  }
  if (typeText !== "counter" && tailMatch[1] !== undefined) {
    throw new ParseError(`State type '${match[2]}' does not take a numeric range`, line.line);
  }
  const field: V2StateFieldAst = {
    kind: "v2_state_field",
    name: match[1]!,
    type: typeText as V2StateFieldAst["type"],
    line: line.line,
  };
  if (bankMin !== undefined && bankMax !== undefined) {
    field.bank = { name: field.name, min: bankMin, max: bankMax };
  }
  if (typeText === "cells" || typeText === "coord") {
    field.domain = argList[0]!;
  }
  if (typeText === "coord_list") {
    const count = Number(argList[1]);
    if (!Number.isInteger(count) || count < 1) {
      throw new ParseError(`coord_list count must be a positive integer, got '${argList[1]}'`, line.line);
    }
    field.domain = argList[0]!;
    field.count = count;
  }
  if (tailMatch[1] !== undefined) {
    field.min = Number(tailMatch[1]);
    field.max = Number(tailMatch[2]);
  }
  if (tailMatch[3] !== undefined) field.initial = tailMatch[3].trim();
  return field;
}

function bankFieldStateName(bank: string, member?: string): string {
  return member === undefined ? bank : `${bank}_${member}`;
}

function parseV2BoardDeclaration(line: SourceLine): V2BoardAst | undefined {
  const match = /^([A-Za-z_][\w]*)\s*:\s*board\(\s*(-?\d+)\.\.(-?\d+)\s*,\s*(-?\d+)\.\.(-?\d+)\s*\)$/u.exec(line.text);
  if (!match) return undefined;
  const xMin = Number(match[2]);
  const xMax = Number(match[3]);
  const yMin = Number(match[4]);
  const yMax = Number(match[5]);
  if (xMin > xMax || yMin > yMax) {
    throw new ParseError("Board ranges must be ascending", line.line);
  }
  const width = xMax - xMin + 1;
  const height = yMax - yMin + 1;
  return {
    kind: "v2_board",
    name: match[1]!,
    xMin,
    xMax,
    yMin,
    yMax,
    width,
    height,
    line: line.line,
  };
}

function parseV2CompactBoardDeclaration(line: SourceLine): V2WorldAst | undefined {
  const match = /^([A-Za-z_][\w]*)\s*:\s*board\(\s*([A-Za-z_][\w]*)\s*\)$/u.exec(line.text);
  if (!match) return undefined;
  const name = match[1]!;
  const encoding = match[2]!;
  if (!isKnownCompactBoardEncoding(encoding)) {
    throw new ParseError(`Unknown board encoding '${encoding}'.`, line.line);
  }
  return {
    kind: "v2_world",
    name,
    position: {
      name,
      encoding,
      line: line.line,
    },
    line: line.line,
  };
}

function isKnownCompactBoardEncoding(encoding: string): boolean {
  return [
    "corridor_plan",
    "decimal_player",
    "floor_plan",
    "packed_decimal_zero_run",
    "pier_to_ship",
    "row_scan",
  ].includes(encoding);
}

function parseV2InlineStatement(text: string, line: number): V2StatementAst {
  const showCall = parseNamedCall(text, "show");
  if (showCall !== undefined) return parseV2ShowCall(showCall.argsText, line);
  const haltCall = parseNamedCall(text, "halt");
  if (haltCall !== undefined) {
    return { kind: "v2_stop", value: haltCall.argsText.trim() === "" ? "0" : haltCall.argsText.trim(), line };
  }
  const readAssignment = /^([A-Za-z_][\w]*)\s*=\s*read\s*\(\s*\)$/u.exec(text);
  if (readAssignment) {
    return { kind: "v2_read", target: readAssignment[1]!, line };
  }
  if (text.startsWith("show ")) {
    throw new ParseError("Show must look like 'show(...)'", line);
  }
  const read = /^read\s+([A-Za-z_][\w]*)$/u.exec(text);
  if (read) {
    throw new ParseError("Read input with 'name = read()'", line);
  }
  if (text.startsWith("read ")) {
    throw new ParseError("Read input with 'name = read()'", line);
  }
  if (text.startsWith("stop ")) {
    throw new ParseError("Use 'halt(...)' instead of stop", line);
  }
  if (text === "return" || text.startsWith("return ")) {
    const expr = text.slice("return".length).trim();
    if (expr === "") {
      throw new ParseError("'return' must return a value, e.g. 'return x + 1'", line);
    }
    return { kind: "v2_return", expr, line };
  }
  const step = /^([A-Za-z_][\w]*)\s*(\+\+|--)$/u.exec(text);
  if (step) {
    return {
      kind: "v2_update",
      target: step[1]!,
      op: step[2] === "++" ? "+=" : "-=",
      expr: "1",
      line,
    };
  }
  const update = /^(.+?)\s*(\+=|-=)\s*(.+)$/u.exec(text);
  if (update) {
    const target = update[1]!.trim();
    validateAssignmentTargetText(target, line);
    return {
      kind: "v2_update",
      target,
      op: update[2] as "+=" | "-=",
      expr: update[3]!.trim(),
      line,
    };
  }
  const assignment = /^(.+?)\s*(?<![!<>=])=(?!=)\s*(.+)$/u.exec(text);
  if (assignment) {
    const target = assignment[1]!.trim();
    validateAssignmentTargetText(target, line);
    return {
      kind: "v2_assign",
      target,
      expr: assignment[2]!.trim(),
      line,
    };
  }
  const call = parseCall(text);
  if (call !== undefined) {
    return {
      kind: "v2_invoke",
      name: call.name,
      args: splitArgs(call.argsText),
      line,
    };
  }
  const command = /^([A-Za-z_][\w]*)(?:\s+(.+))?$/u.exec(text);
  if (command) {
    throw new ParseError("Function calls must look like 'name(...)'", line);
  }
  throw new ParseError(`Unexpected statement '${text}'`, line);
}

function validateAssignmentTargetText(target: string, line: number): void {
  if (/^[A-Za-z_][\w]*$/u.test(target)) return;
  const expr = parseExpression(target, line);
  if (expr.kind === "indexed") return;
  throw new ParseError(`Invalid assignment target '${target}'`, line);
}

function parseV2ShowCall(argsText: string, line: number): V2StatementAst {
  const trimmed = argsText.trim();
  if (trimmed.length === 0) {
    return { kind: "v2_show", items: parseDisplayItemList(trimmed, line), line };
  }
  if (isNumericLiteralText(trimmed)) {
    return { kind: "v2_show", target: trimmed, line };
  }
  const items = parseDisplayItemList(trimmed, line);
  const literal = displayLiteralText(items);
  const numericLiteral = literal === undefined ? undefined : canonicalNumericDisplayLiteralText(literal);
  if (numericLiteral !== undefined) {
    return { kind: "v2_show", target: numericLiteral, line };
  }
  return { kind: "v2_show", items, line };
}

function parseV2Predicate(text: string, line: number): V2PredicateAst {
  const trimmed = text.trim();
  const contains = /^(.+?)\s+in\s+([A-Za-z_][\w]*)$/u.exec(trimmed);
  if (contains) {
    return {
      kind: "v2_contains",
      collection: contains[2]!.trim(),
      item: contains[1]!.trim(),
    };
  }
  const compare = /^(.+?)\s*(==|!=|<=|>=|<|>)\s*(.+)$/u.exec(trimmed);
  if (compare) {
    return {
      kind: "v2_compare",
      left: compare[1]!.trim(),
      op: compare[2] as "==" | "!=" | "<" | "<=" | ">" | ">=",
      right: compare[3]!.trim(),
    };
  }
  return {
    kind: "v2_compare",
    left: trimmed,
    op: "!=",
    right: "0",
  };
}

function parseV2RawInputs(text: string, line: number): Extract<V2StatementAst, { kind: "v2_raw" }>["inputs"] {
  return splitArgs(text).map((part) => {
    const match = /^(X|Y|Z|T)\s*=\s*(.+)$/iu.exec(part);
    if (!match) throw new ParseError("Raw input must look like 'takes X = expr'", line);
    return {
      slot: match[1]!.toUpperCase() as RawStackSlot,
      expr: match[2]!.trim(),
      line,
    };
  });
}

function parseV2RawOutput(text: string, line: number): Extract<V2StatementAst, { kind: "v2_raw" }>["outputs"][number] {
  const explicit = /^X\s*->\s*([A-Za-z_][\w]*)$/iu.exec(text.trim());
  const shorthand = /^([A-Za-z_][\w]*)$/u.exec(text.trim());
  const target = explicit?.[1] ?? shorthand?.[1];
  if (target === undefined) throw new ParseError("Raw output must look like 'returns X -> name'", line);
  return { slot: "X", target, line };
}

function parseV2RawContractList(text: string, line: number): string[] {
  const values = parseIdentifierList(text).map((item) => normalizeV2RawContractItem(item, line));
  if (values.length === 0) throw new ParseError("Raw contract list must not be empty", line);
  if (values.includes("none") && values.length > 1) {
    throw new ParseError("Raw contract item 'none' cannot be combined with other items", line);
  }
  return [...new Set(values)];
}

function normalizeV2RawContractItem(text: string, line: number): string {
  const trimmed = text.trim();
  if (/^(none|state|stack|display|flags|memory)$/iu.test(trimmed)) return trimmed.toLowerCase();
  if (/^(X|Y|Z|T|X1)$/iu.test(trimmed)) return trimmed.toUpperCase();
  const register = /^R?([0-9a-eавсде])$/iu.exec(trimmed);
  if (register) return `R${register[1]!.toLowerCase()}`;
  throw new ParseError(`Unknown raw contract item '${text}'`, line);
}

function validateV2RawInputs(inputs: Extract<V2StatementAst, { kind: "v2_raw" }>["inputs"], line: number): void {
  const slots = new Set(inputs.map((input) => input.slot));
  if (slots.has("T") && (!slots.has("Z") || !slots.has("Y") || !slots.has("X"))) {
    throw new ParseError("Raw input T requires Z, Y, and X inputs", line);
  }
  if (slots.has("Z") && (!slots.has("Y") || !slots.has("X"))) {
    throw new ParseError("Raw input Z requires Y and X inputs", line);
  }
  if (slots.has("Y") && !slots.has("X")) {
    throw new ParseError("Raw input Y requires an X input", line);
  }
}

function splitArgs(text: string): string[] {
  const args: string[] = [];
  let start = 0;
  let depth = 0;
  let quote = false;
  let escaped = false;
  for (let index = 0; index < text.length; index += 1) {
    const char = text[index];
    if (quote) {
      if (escaped) {
        escaped = false;
      } else if (char === "\\") {
        escaped = true;
      } else if (char === "\"") {
        quote = false;
      }
      continue;
    }
    if (char === "\"") {
      quote = true;
      continue;
    }
    if (char === "(") depth += 1;
    if (char === ")") depth = Math.max(0, depth - 1);
    if (char === "," && depth === 0) {
      args.push(text.slice(start, index).trim());
      start = index + 1;
    }
  }
  args.push(text.slice(start).trim());
  return args.filter(Boolean);
}

interface ParsedCall {
  name: string;
  argsText: string;
}

function parseNamedCall(text: string, name: string): ParsedCall | undefined {
  const call = parseCall(text);
  return call?.name === name ? call : undefined;
}

function parseCall(text: string): ParsedCall | undefined {
  const header = /^([A-Za-z_][\w]*)\s*\(/u.exec(text);
  if (!header) return undefined;
  const open = header[0].lastIndexOf("(");
  let depth = 0;
  let quote = false;
  let escaped = false;
  for (let index = open; index < text.length; index += 1) {
    const char = text[index]!;
    if (quote) {
      if (escaped) {
        escaped = false;
      } else if (char === "\\") {
        escaped = true;
      } else if (char === "\"") {
        quote = false;
      }
      continue;
    }
    if (char === "\"") {
      quote = true;
      continue;
    }
    if (char === "(") {
      depth += 1;
      continue;
    }
    if (char !== ")") continue;
    depth -= 1;
    if (depth === 0) {
      if (text.slice(index + 1).trim().length > 0) return undefined;
      return { name: header[1]!, argsText: text.slice(open + 1, index) };
    }
    if (depth < 0) return undefined;
  }
  return undefined;
}

interface V2LoweringContext {
  signedAbsMatchPairs: boolean;
  consts: ReadonlyMap<string, V2ConstBinding>;
  ruleParams: Map<string, string[]>;
  rules: Map<string, V2RuleAst>;
  // Names of rules that act as value-returning functions (their body contains a
  // `return`). Used by validation and by the expression-position call path.
  functionRules: Set<string>;
  specializedRules: Set<string>;
  stateTypes: Map<string, V2StateFieldAst["type"]>;
  stateDomains: Map<string, string>;
  stateRanges: Map<string, { min?: number; max?: number }>;
  coordLists: Map<string, { domain: string; count: number; items: string[] }>;
  cellMapNames: Map<string, string>;
  boards: Map<string, V2BoardAst>;
  worlds: Map<string, V2WorldAst>;
}

interface LoweredV2Program {
  domains: DomainAst[];
  banks?: StateBankAst[];
  states: StateAst[];
  displays: DisplayAst[];
  entries: EntryAst[];
  procs: ProcAst[];
}

function lowerV2Program(v2: V2ProgramAst, options: ParseOptions = {}): LoweredV2Program {
  const decimalSeries = tryLowerV2DecimalSeries(v2);
  if (decimalSeries !== undefined) return decimalSeries;

  const ruleParams = collectV2RuleParams(v2);
  const rules = collectV2Rules(v2);
  const functionRules = collectV2FunctionRules(v2);
  const specializedRules = selectV2RuleSpecializations(v2, rules);
  const stateDomains = new Map(
    v2.state
      .filter((field) => field.domain !== undefined)
      .map((field) => [field.name, field.domain!]),
  );
  const coordLists = collectV2CoordLists(v2);
  const cellMapNames = collectV2CellMapNames(v2, stateDomains);
  validateV2Domains(v2);
  validateV2References(v2, { ruleParams });
  validateV2Functions(v2, functionRules);
  const constContext = buildV2ConstContext(v2);
  const context: V2LoweringContext = {
    signedAbsMatchPairs: options.signedAbsMatchPairs === true,
    consts: constContext.bindings,
    ruleParams,
    rules,
    functionRules,
    specializedRules,
    stateTypes: new Map(v2.state.map((field) => [field.name, field.type])),
    stateDomains,
    stateRanges: collectV2StateRanges(v2),
    coordLists,
    cellMapNames,
    boards: new Map(v2.boards.map((board) => [board.name, board])),
    worlds: new Map(v2.worlds.map((world) => [world.name, world])),
  };
  rewriteV2DisplayExpressions(v2, context);
  const inlineScreens = collectV2InlineScreens(v2);
  const banks = lowerV2StateBanks(v2);
  return {
    domains: lowerV2Domains(v2),
    ...(banks === undefined ? {} : { banks }),
    states: lowerV2State(v2, specializedRules, cellMapNames, context),
    displays: inlineScreens.map(lowerV2Screen),
    entries: [lowerV2Entry(v2, context)],
    procs: [
      ...v2.rules.filter((rule) => !specializedRules.has(rule.name)).map((rule) => lowerV2Rule(rule, context)),
    ],
  };
}

function rewriteV2DisplayExpressions(_v2: V2ProgramAst, _context: V2LoweringContext): void {
  // Hook for expression-display rewrites before inline screens are collected.
}

function tryLowerV2DecimalSeries(v2: V2ProgramAst): LoweredV2Program | undefined {
  if (
    v2.body.length !== 5 ||
    v2.consts.length > 0 ||
    v2.state.length > 0 ||
    v2.boards.length > 0 ||
    v2.worlds.length > 0 ||
    v2.rules.length > 0
  ) {
    return undefined;
  }

  const [precision, counterInit, valueInit, loop, stop] = v2.body;
  if (precision?.kind !== "v2_assign" || precision.target !== "digits") return undefined;
  const digits = Number(normalizedV2Text(precision.expr));
  if (!Number.isInteger(digits)) return undefined;
  if (counterInit?.kind !== "v2_assign") return undefined;
  const counterStart = Number(normalizedV2Text(counterInit.expr));
  if (!Number.isInteger(counterStart)) return undefined;
  if (valueInit?.kind !== "v2_assign" || normalizedV2Text(valueInit.expr) !== "1") {
    return undefined;
  }

  const counterName = counterInit.target;
  const valueName = valueInit.target;
  if (
    loop?.kind !== "v2_while" ||
    loop.predicate.kind !== "v2_compare" ||
    normalizedV2Text(loop.predicate.left) !== counterName ||
    loop.predicate.op !== ">=" ||
    normalizedV2Text(loop.predicate.right) !== "1" ||
    loop.body.length !== 2 ||
    stop?.kind !== "v2_stop" ||
    normalizedV2Text(stop.value) !== valueName
  ) {
    return undefined;
  }

  const [step, decrement] = loop.body;
  if (
    step?.kind !== "v2_assign" ||
    step.target !== valueName ||
    normalizedV2Text(step.expr) !== `1+${valueName}/${counterName}` ||
    decrement?.kind !== "v2_update" ||
    decrement.target !== counterName ||
    decrement.op !== "-=" ||
    normalizedV2Text(decrement.expr) !== "1"
  ) {
    return undefined;
  }

  return {
    domains: [],
    states: [],
    displays: [],
    entries: [{
      kind: "entry",
      name: "main",
      body: [{
        kind: "decimal_series",
        digits,
        counterStart,
        line: v2.line,
      }],
      line: v2.line,
    }],
    procs: [],
  };
}

function normalizedV2Text(text: string): string {
  return text.replace(/\s+/gu, "");
}

function collectV2StateRanges(v2: V2ProgramAst): Map<string, { min?: number; max?: number }> {
  return new Map(v2.state.map((field) => {
    const range: { min?: number; max?: number } = {};
    if (field.min !== undefined) range.min = field.min;
    if (field.max !== undefined) range.max = field.max;
    return [field.name, range];
  }));
}

function collectV2CoordLists(v2: V2ProgramAst): V2LoweringContext["coordLists"] {
  const lists: V2LoweringContext["coordLists"] = new Map();
  for (const field of v2.state) {
    if (field.type !== "coord_list") continue;
    if (field.domain === undefined || field.count === undefined) {
      throw new ParseError("coord_list state must declare a domain and count", field.line);
    }
    lists.set(field.name, {
      domain: field.domain,
      count: field.count,
      items: range(0, field.count - 1).map((index) => coordListItemName(field.name, index)),
    });
  }
  return lists;
}

function collectV2RuleParams(v2: V2ProgramAst): V2LoweringContext["ruleParams"] {
  const ruleParams = new Map<string, string[]>();
  for (const rule of v2.rules) {
    if (ruleParams.has(rule.name)) throw new ParseError(`Duplicate function '${rule.name}'`, rule.line);
    ruleParams.set(rule.name, rule.params);
  }
  return ruleParams;
}

function collectV2Rules(v2: V2ProgramAst): V2LoweringContext["rules"] {
  return new Map(v2.rules.map((rule) => [rule.name, rule]));
}

// A rule is a value-returning function when any statement in its body (at any
// nesting depth) is a `return`.
function collectV2FunctionRules(v2: V2ProgramAst): Set<string> {
  const functions = new Set<string>();
  for (const rule of v2.rules) {
    if (v2StatementsContainReturn(rule.body)) functions.add(rule.name);
  }
  return functions;
}

function v2StatementsContainReturn(statements: V2StatementAst[]): boolean {
  return statements.some(v2StatementContainsReturn);
}

function v2StatementContainsReturn(statement: V2StatementAst): boolean {
  switch (statement.kind) {
    case "v2_return":
      return true;
    case "v2_if":
      return v2StatementsContainReturn(statement.thenBody) ||
        (statement.elseBody !== undefined && v2StatementsContainReturn(statement.elseBody));
    case "v2_while":
    case "v2_loop":
      return v2StatementsContainReturn(statement.body);
    case "v2_match":
      return statement.cases.some((matchCase) => v2StatementContainsReturn(matchCase.action)) ||
        (statement.otherwise !== undefined && v2StatementContainsReturn(statement.otherwise));
    default:
      return false;
  }
}

// Every control path of a function body must end in a `return` (or a `stop`),
// so the value left in X at В/О is always defined.
function v2StatementsAlwaysReturn(statements: V2StatementAst[]): boolean {
  const last = statements.at(-1);
  if (last === undefined) return false;
  return v2StatementAlwaysReturns(last);
}

function v2StatementAlwaysReturns(statement: V2StatementAst): boolean {
  switch (statement.kind) {
    case "v2_return":
    case "v2_stop":
      return true;
    case "v2_if":
      return statement.elseBody !== undefined &&
        v2StatementsAlwaysReturn(statement.thenBody) &&
        v2StatementsAlwaysReturn(statement.elseBody);
    case "v2_match":
      return statement.otherwise !== undefined &&
        statement.cases.every((matchCase) => v2StatementAlwaysReturns(matchCase.action)) &&
        v2StatementAlwaysReturns(statement.otherwise);
    default:
      return false;
  }
}

function validateV2Functions(v2: V2ProgramAst, functionRules: Set<string>): void {
  // `return` only makes sense inside a function (it returns the function's value).
  if (v2StatementsContainReturn(v2.body)) {
    throw new ParseError("'return' is only allowed inside a function", v2.line);
  }
  for (const rule of v2.rules) {
    if (!functionRules.has(rule.name)) continue;
    if (!v2StatementsAlwaysReturn(rule.body)) {
      throw new ParseError(
        `Function '${rule.name}' must return a value on every path (end each branch with 'return' or 'halt')`,
        rule.line,
      );
    }
  }
  // Recursive value functions are validated after lowering, where the compiler
  // can distinguish true tail calls from calls nested inside larger expressions.
}

function v2StatementExprTexts(statement: V2StatementAst): string[] {
  switch (statement.kind) {
    case "v2_assign":
    case "v2_update":
      return [statement.expr];
    case "v2_return":
      return [statement.expr];
    case "v2_stop":
      return [statement.value];
    case "v2_invoke":
      return [...statement.args];
    case "v2_show":
    case "v2_read":
      return [];
    case "v2_if":
      return [
        ...v2PredicateExprTexts(statement.predicate),
        ...statement.thenBody.flatMap(v2StatementExprTexts),
        ...(statement.elseBody ? statement.elseBody.flatMap(v2StatementExprTexts) : []),
      ];
    case "v2_while":
      return [...v2PredicateExprTexts(statement.predicate), ...statement.body.flatMap(v2StatementExprTexts)];
    case "v2_loop":
      return statement.body.flatMap(v2StatementExprTexts);
    case "v2_match":
      return [
        statement.expr,
        ...statement.cases.flatMap((matchCase) => [...matchCase.values, ...v2StatementExprTexts(matchCase.action)]),
        ...(statement.otherwise ? v2StatementExprTexts(statement.otherwise) : []),
      ];
    case "v2_raw":
      return statement.inputs.map((input) => input.expr);
    default:
      return [];
  }
}

function v2PredicateExprTexts(predicate: V2PredicateAst): string[] {
  if (predicate.kind === "v2_contains") return [predicate.collection, predicate.item];
  return [predicate.left, predicate.right];
}

interface V2InvocationSite {
  statement: V2InvokeStatementAst;
  currentRule?: string;
}

function selectV2RuleSpecializations(v2: V2ProgramAst, rules: Map<string, V2RuleAst>): Set<string> {
  const invocations = collectV2Invocations(v2);
  const selected = new Set<string>();
  for (const rule of v2.rules) {
    if (rule.params.length === 0) continue;
    const indexedConstantSpecialization = ruleUsesIndexedParam(rule);
    if (!indexedConstantSpecialization && !isSpecializableRuleBody(rule)) continue;
    const sites = invocations.filter((site) => site.statement.name === rule.name);
    if (sites.length < 2) continue;
    if (sites.some((site) => site.currentRule === rule.name)) continue;
    if (!sites.every((site) => site.statement.args.length === rule.params.length && site.statement.args.every(isSpecializationArg))) {
      continue;
    }
    if (indexedConstantSpecialization && rules.has(rule.name)) {
      selected.add(rule.name);
      continue;
    }

    const genericCost = estimateV2Statements(rule.body) + 1 +
      sites.reduce((sum, site) => sum + estimateV2InvokeSetupCost(site.statement) + 2, 0);
    const inlineCost = sites.reduce((sum, site) => {
      const replacements = invokeReplacements(rule, site.statement.args);
      return sum + estimateV2Statements(rule.body, replacements);
    }, 0);

    if (inlineCost < genericCost && rules.has(rule.name)) selected.add(rule.name);
  }
  return selected;
}

function ruleUsesIndexedParam(rule: V2RuleAst): boolean {
  return rule.params.some((param) => {
    const pattern = new RegExp(`\\[\\s*${escapeRegExp(param)}\\s*\\]`, "u");
    return v2StatementTexts(rule.body).some((text) => pattern.test(text));
  });
}

function v2StatementTexts(statements: V2StatementAst[]): string[] {
  const texts: string[] = [];
  const visit = (items: V2StatementAst[]): void => {
    for (const statement of items) {
      switch (statement.kind) {
        case "v2_assign":
        case "v2_update":
          texts.push(statement.target, statement.expr);
          break;
        case "v2_if":
          texts.push(...v2PredicateExprTexts(statement.predicate));
          visit(statement.thenBody);
          if (statement.elseBody) visit(statement.elseBody);
          break;
        case "v2_while":
          texts.push(...v2PredicateExprTexts(statement.predicate));
          visit(statement.body);
          break;
        case "v2_loop":
          visit(statement.body);
          break;
        case "v2_match":
          texts.push(statement.expr, ...statement.cases.flatMap((matchCase) => matchCase.values));
          for (const matchCase of statement.cases) visit([matchCase.action]);
          if (statement.otherwise) visit([statement.otherwise]);
          break;
        case "v2_invoke":
          texts.push(...statement.args);
          break;
        case "v2_show":
          if (statement.target !== undefined) texts.push(statement.target);
          for (const item of statement.items ?? []) {
            if (item.kind === "source") texts.push(item.name);
          }
          break;
        case "v2_stop":
          texts.push(statement.value);
          break;
        case "v2_return":
          texts.push(statement.expr);
          break;
        case "v2_raw":
          for (const input of statement.inputs ?? []) texts.push(input.expr);
          break;
        default:
          break;
      }
    }
  };
  visit(statements);
  return texts;
}

function collectV2Invocations(v2: V2ProgramAst): V2InvocationSite[] {
  const sites: V2InvocationSite[] = [];
  const visit = (statements: V2StatementAst[], currentRule?: string): void => {
    for (const statement of statements) {
      if (statement.kind === "v2_invoke") {
        const site: V2InvocationSite = { statement };
        if (currentRule !== undefined) site.currentRule = currentRule;
        sites.push(site);
      }
      if (statement.kind === "v2_if") {
        visit(statement.thenBody, currentRule);
        if (statement.elseBody) visit(statement.elseBody, currentRule);
      }
      if (statement.kind === "v2_while") {
        visit(statement.body, currentRule);
      }
      if (statement.kind === "v2_loop") {
        visit(statement.body, currentRule);
      }
      if (statement.kind === "v2_match") {
        for (const matchCase of statement.cases) visit([matchCase.action], currentRule);
        if (statement.otherwise) visit([statement.otherwise], currentRule);
      }
    }
  };
  if (v2.body.length > 0) visit(v2.body);
  for (const rule of v2.rules) visit(rule.body, rule.name);
  return sites;
}

const PARAMETRIC_SIBLING_RULE_PREFIX = "__param_sibling_";

interface V2ParametricSiblingPlan {
  readonly body: V2StatementAst[];
  readonly leftDelta: number;
  readonly rightDelta: number;
}

function synthesizeV2ParametricSiblingRules(v2: V2ProgramAst): void {
  const rules = collectV2Rules(v2);
  const callCounts = new Map<string, number>();
  for (const site of collectV2Invocations(v2)) {
    callCounts.set(site.statement.name, (callCounts.get(site.statement.name) ?? 0) + 1);
  }
  const consumed = new Set<string>();

  const rewriteStatements = (statements: V2StatementAst[]): V2StatementAst[] =>
    statements.map((statement) => rewriteStatement(statement));

  const rewriteStatement = (statement: V2StatementAst): V2StatementAst => {
    switch (statement.kind) {
      case "v2_if": {
        const rewritten: V2IfStatementAst = {
          ...statement,
          thenBody: rewriteStatements(statement.thenBody),
        };
        if (statement.elseBody !== undefined) rewritten.elseBody = rewriteStatements(statement.elseBody);
        return rewritten;
      }
      case "v2_while":
        return { ...statement, body: rewriteStatements(statement.body) };
      case "v2_loop":
        return { ...statement, body: rewriteStatements(statement.body) };
      case "v2_match": {
        const rewritten: V2MatchStatementAst = {
          ...statement,
          cases: statement.cases.map((matchCase) => ({
            ...matchCase,
            action: rewriteStatement(matchCase.action),
          })),
        };
        if (statement.otherwise !== undefined) rewritten.otherwise = rewriteStatement(statement.otherwise);
        rewritten.cases = synthesizeV2ParametricSiblingMatchCases(v2, rewritten.cases, rules, callCounts, consumed);
        return rewritten;
      }
      default:
        return statement;
    }
  };

  v2.body = rewriteStatements(v2.body);
  for (const rule of [...v2.rules]) {
    if (consumed.has(rule.name) || rule.name.startsWith(PARAMETRIC_SIBLING_RULE_PREFIX)) continue;
    rule.body = rewriteStatements(rule.body);
  }
  if (consumed.size > 0) v2.rules = v2.rules.filter((rule) => !consumed.has(rule.name));
}

function synthesizeV2ParametricSiblingMatchCases(
  v2: V2ProgramAst,
  cases: V2MatchCaseAst[],
  rules: ReadonlyMap<string, V2RuleAst>,
  callCounts: ReadonlyMap<string, number>,
  consumed: Set<string>,
): V2MatchCaseAst[] {
  const usedCases = new Set<number>();
  const rewritten = [...cases];

  for (let leftIndex = 0; leftIndex < rewritten.length; leftIndex += 1) {
    if (usedCases.has(leftIndex)) continue;
    const leftInvoke = v2SingleInvokeAction(rewritten[leftIndex]!.action);
    if (leftInvoke === undefined || consumed.has(leftInvoke.name)) continue;
    const leftRule = singleUseV2SiblingRule(rules, callCounts, leftInvoke.name);
    if (leftRule === undefined) continue;

    for (let rightIndex = leftIndex + 1; rightIndex < rewritten.length; rightIndex += 1) {
      if (usedCases.has(rightIndex)) continue;
      const rightInvoke = v2SingleInvokeAction(rewritten[rightIndex]!.action);
      if (rightInvoke === undefined || consumed.has(rightInvoke.name)) continue;
      const rightRule = singleUseV2SiblingRule(rules, callCounts, rightInvoke.name);
      if (rightRule === undefined) continue;

      const param = freshV2ParametricSiblingName(v2, "delta");
      const plan = v2ParametricSiblingPlan(leftRule, rightRule, param);
      if (plan === undefined) continue;

      const helperName = freshV2ParametricSiblingName(v2, "proc");
      insertV2ParametricSiblingRule(v2, leftRule, rightRule, {
        kind: "v2_rule",
        name: helperName,
        params: [param],
        body: plan.body,
        line: leftRule.line,
      });
      rewritten[leftIndex] = {
        ...rewritten[leftIndex]!,
        action: {
          kind: "v2_invoke",
          name: helperName,
          args: [formatV2Number(plan.leftDelta)],
          line: leftInvoke.line,
        },
      };
      rewritten[rightIndex] = {
        ...rewritten[rightIndex]!,
        action: {
          kind: "v2_invoke",
          name: helperName,
          args: [formatV2Number(plan.rightDelta)],
          line: rightInvoke.line,
        },
      };
      consumed.add(leftRule.name);
      consumed.add(rightRule.name);
      usedCases.add(leftIndex);
      usedCases.add(rightIndex);
      break;
    }
  }

  return rewritten;
}

function insertV2ParametricSiblingRule(
  v2: V2ProgramAst,
  left: V2RuleAst,
  right: V2RuleAst,
  helper: V2RuleAst,
): void {
  const leftIndex = v2.rules.indexOf(left);
  const rightIndex = v2.rules.indexOf(right);
  const indexes = [leftIndex, rightIndex].filter((index) => index >= 0);
  const insertAt = indexes.length === 0 ? v2.rules.length : Math.min(...indexes);
  v2.rules.splice(insertAt, 0, helper);
}

function v2SingleInvokeAction(statement: V2StatementAst): V2InvokeStatementAst | undefined {
  return statement.kind === "v2_invoke" && statement.args.length === 0 ? statement : undefined;
}

function singleUseV2SiblingRule(
  rules: ReadonlyMap<string, V2RuleAst>,
  callCounts: ReadonlyMap<string, number>,
  name: string,
): V2RuleAst | undefined {
  const rule = rules.get(name);
  if (rule === undefined || rule.params.length > 0) return undefined;
  if ((callCounts.get(name) ?? 0) !== 1) return undefined;
  if (v2StatementsContainReturn(rule.body) || v2StatementsContainRaw(rule.body)) return undefined;
  return rule;
}

function v2ParametricSiblingPlan(
  left: V2RuleAst,
  right: V2RuleAst,
  param: string,
): V2ParametricSiblingPlan | undefined {
  if (left.body.length !== right.body.length || left.body.length === 0) return undefined;
  let leftDelta: number | undefined;
  let rightDelta: number | undefined;
  let parameterized = 0;
  const body: V2StatementAst[] = [];

  for (let index = 0; index < left.body.length; index += 1) {
    const leftStatement = left.body[index]!;
    const rightStatement = right.body[index]!;
    const leftUpdate = v2AdditiveSelfUpdate(leftStatement);
    const rightUpdate = v2AdditiveSelfUpdate(rightStatement);
    if (
      leftUpdate !== undefined &&
      rightUpdate !== undefined &&
      leftUpdate.target === rightUpdate.target &&
      leftUpdate.delta !== rightUpdate.delta
    ) {
      if (leftDelta === undefined) {
        leftDelta = leftUpdate.delta;
        rightDelta = rightUpdate.delta;
      } else if (leftDelta !== leftUpdate.delta || rightDelta !== rightUpdate.delta) {
        return undefined;
      }
      body.push({
        kind: "v2_update",
        target: leftUpdate.target,
        op: "+=",
        expr: param,
        line: leftStatement.line,
      });
      parameterized += 1;
      continue;
    }
    if (!v2StatementsComparable(leftStatement, rightStatement)) return undefined;
    body.push(structuredClone(leftStatement));
  }

  const first = body[0];
  if (
    leftDelta === undefined ||
    rightDelta === undefined ||
    parameterized === 0 ||
    first?.kind !== "v2_update" ||
    first.expr !== param
  ) {
    return undefined;
  }
  return { body, leftDelta, rightDelta };
}

function v2AdditiveSelfUpdate(
  statement: V2StatementAst,
): { target: string; delta: number } | undefined {
  if (statement.kind === "v2_update") {
    if (!isSimpleV2Identifier(statement.target)) return undefined;
    const value = numericV2LiteralValue(statement.expr);
    if (value === undefined) return undefined;
    return { target: statement.target, delta: statement.op === "+=" ? value : -value };
  }
  if (statement.kind !== "v2_assign" || !isSimpleV2Identifier(statement.target)) return undefined;
  const selfDelta = v2SelfAssignmentDelta(statement.target, statement.expr);
  return selfDelta === undefined ? undefined : { target: statement.target, delta: selfDelta };
}

function v2SelfAssignmentDelta(target: string, expr: string): number | undefined {
  const escaped = escapeRegExp(target);
  const numeric = "(-?\\d+(?:\\.\\d+)?(?:e[+-]?\\d+)?)";
  const targetPlus = new RegExp(`^${escaped}\\s*\\+\\s*${numeric}$`, "iu").exec(expr.trim());
  if (targetPlus) return numericV2LiteralValue(targetPlus[1]!);
  const plusTarget = new RegExp(`^${numeric}\\s*\\+\\s*${escaped}$`, "iu").exec(expr.trim());
  if (plusTarget) return numericV2LiteralValue(plusTarget[1]!);
  const targetMinus = new RegExp(`^${escaped}\\s*-\\s*${numeric}$`, "iu").exec(expr.trim());
  if (targetMinus) {
    const value = numericV2LiteralValue(targetMinus[1]!);
    return value === undefined ? undefined : -value;
  }
  return undefined;
}

function numericV2LiteralValue(text: string): number | undefined {
  if (!isNumericLiteralText(text)) return undefined;
  const value = Number(text.trim());
  return Number.isFinite(value) ? value : undefined;
}

function isSimpleV2Identifier(text: string): boolean {
  return /^[A-Za-z_][\w]*$/u.test(text);
}

function formatV2Number(value: number): string {
  return Object.is(value, -0) ? "0" : String(value);
}

function v2StatementsContainRaw(statements: readonly V2StatementAst[]): boolean {
  return statements.some((statement) => {
    switch (statement.kind) {
      case "v2_raw":
        return true;
      case "v2_if":
        return v2StatementsContainRaw(statement.thenBody) ||
          (statement.elseBody !== undefined && v2StatementsContainRaw(statement.elseBody));
      case "v2_while":
      case "v2_loop":
        return v2StatementsContainRaw(statement.body);
      case "v2_match":
        return statement.cases.some((matchCase) => v2StatementsContainRaw([matchCase.action])) ||
          (statement.otherwise !== undefined && v2StatementsContainRaw([statement.otherwise]));
      default:
        return false;
    }
  });
}

function v2StatementsComparable(left: V2StatementAst, right: V2StatementAst): boolean {
  return JSON.stringify(v2ComparableValue(left)) === JSON.stringify(v2ComparableValue(right));
}

function v2ComparableValue(value: unknown): unknown {
  if (Array.isArray(value)) return value.map(v2ComparableValue);
  if (value === null || typeof value !== "object") return value;
  const result: Record<string, unknown> = {};
  const record = value as Record<string, unknown>;
  for (const key of Object.keys(record).sort()) {
    if (key === "line") continue;
    const field = record[key];
    if (field !== undefined) result[key] = v2ComparableValue(field);
  }
  return result;
}

function freshV2ParametricSiblingName(v2: V2ProgramAst, role: "delta" | "proc"): string {
  const used = collectV2UsedNames(v2);
  for (let index = 0; ; index += 1) {
    const candidate = `${PARAMETRIC_SIBLING_RULE_PREFIX}${role}_${index}`;
    if (!used.has(candidate)) return candidate;
  }
}

function collectV2UsedNames(v2: V2ProgramAst): Set<string> {
  const used = new Set<string>([v2.name]);
  const addText = (text: string): void => {
    const pattern = /\b[A-Za-z_][\w]*\b/gu;
    let match: RegExpExecArray | null;
    while ((match = pattern.exec(text)) !== null) used.add(match[0]);
  };
  const visit = (statements: readonly V2StatementAst[]): void => {
    for (const statement of statements) {
      for (const text of v2StatementExprTexts(statement)) addText(text);
      switch (statement.kind) {
        case "v2_assign":
        case "v2_update":
          addText(statement.target);
          break;
        case "v2_read":
          used.add(statement.target);
          break;
        case "v2_show":
          if (statement.target !== undefined) addText(statement.target);
          if (statement.inlineName !== undefined) used.add(statement.inlineName);
          for (const item of statement.items ?? []) {
            if (item.kind === "source") used.add(item.name);
          }
          break;
        case "v2_if":
          visit(statement.thenBody);
          if (statement.elseBody !== undefined) visit(statement.elseBody);
          break;
        case "v2_while":
        case "v2_loop":
          visit(statement.body);
          break;
        case "v2_match":
          for (const matchCase of statement.cases) visit([matchCase.action]);
          if (statement.otherwise !== undefined) visit([statement.otherwise]);
          break;
        case "v2_invoke":
          used.add(statement.name);
          break;
        case "v2_raw":
          for (const output of statement.outputs) used.add(output.target);
          for (const clobber of statement.clobbers) used.add(clobber);
          for (const preserve of statement.preserves) used.add(preserve);
          break;
        default:
          break;
      }
    }
  };

  for (const field of v2.state) used.add(field.name);
  for (const board of v2.boards) used.add(board.name);
  for (const world of v2.worlds) {
    used.add(world.name);
    if (world.position !== undefined) used.add(world.position.name);
  }
  visit(v2.body);
  for (const rule of v2.rules) {
    used.add(rule.name);
    for (const param of rule.params) used.add(param);
    visit(rule.body);
  }
  return used;
}

function isSpecializableRuleBody(rule: V2RuleAst): boolean {
  const params = new Set(rule.params);
  return rule.body.length > 0 && rule.body.every((statement) => {
    if (statement.kind !== "v2_assign" && statement.kind !== "v2_update") return false;
    return !params.has(statement.target);
  });
}

function isSpecializationArg(arg: string): boolean {
  return isNumericLiteralText(arg.trim());
}

function invokeReplacements(rule: V2RuleAst, args: string[]): Map<string, string> {
  const replacements = new Map<string, string>();
  for (let index = 0; index < rule.params.length; index += 1) {
    replacements.set(rule.params[index]!, args[index]!.trim());
  }
  return replacements;
}

function estimateV2InvokeSetupCost(statement: V2InvokeStatementAst): number {
  return statement.args.reduce((sum, arg) => sum + estimateV2ExpressionText(arg) + 1, 0);
}

function estimateV2Statements(statements: V2StatementAst[], replacements = new Map<string, string>()): number {
  return statements.reduce((sum, statement) => sum + estimateV2Statement(statement, replacements), 0);
}

function estimateV2Statement(statement: V2StatementAst, replacements: Map<string, string>): number {
  switch (statement.kind) {
    case "v2_assign":
      return estimateV2ExpressionText(substituteV2Text(statement.expr, replacements)) + 1;
    case "v2_update":
      return estimateV2ExpressionText(statement.target) + estimateV2ExpressionText(substituteV2Text(statement.expr, replacements)) + 2;
    default:
      return 99;
  }
}

function estimateV2ExpressionText(text: string): number {
  const trimmed = text.trim();
  if (isNumericLiteralText(trimmed) || /^[A-Za-z_][\w]*$/u.test(trimmed)) return 1;
  const operators = trimmed.match(/[+\-*/]/gu)?.length ?? 0;
  const calls = trimmed.match(/[A-Za-z_][\w]*\s*\(/gu)?.length ?? 0;
  return 1 + operators + calls + Math.ceil(trimmed.length / 12);
}

function collectV2InlineScreens(v2: V2ProgramAst): V2ScreenAst[] {
  const screens: V2ScreenAst[] = [];
  const screensByItems = new Map<string, V2ScreenAst>();
  let next = 0;
  const visit = (statements: V2StatementAst[]): void => {
    for (const statement of statements) {
      if (statement.kind === "v2_show" && statement.items !== undefined) {
        const key = displayItemKey(statement.items);
        const existing = screensByItems.get(key);
        if (existing !== undefined) {
          statement.inlineName = existing.name;
        } else {
          const name = `__inline_show_${statement.line}_${next}`;
          next += 1;
          statement.inlineName = name;
          const screen = {
            kind: "v2_screen" as const,
            name,
            sources: displayItemSources(statement.items),
            items: statement.items,
            line: statement.line,
          };
          screensByItems.set(key, screen);
          screens.push(screen);
        }
      }
      if (statement.kind === "v2_if") {
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "v2_while") visit(statement.body);
      if (statement.kind === "v2_loop") visit(statement.body);
      if (statement.kind === "v2_match") {
        for (const matchCase of statement.cases) visit([matchCase.action]);
        if (statement.otherwise) visit([statement.otherwise]);
      }
    }
  };
  if (v2.body.length > 0) visit(v2.body);
  for (const rule of v2.rules) visit(rule.body);
  return screens;
}

function displayItemKey(items: DisplayItemAst[]): string {
  return JSON.stringify(items.map((item) => {
    if (item.kind === "literal") return ["literal", item.text];
    return ["source", item.name, item.width ?? null, item.pad ?? null];
  }));
}

function validateV2Domains(v2: V2ProgramAst): void {
  const domains = new Map<string, number>();
  for (const board of v2.boards) {
    if (domains.has(board.name)) throw new ParseError(`Duplicate domain '${board.name}'`, board.line);
    domains.set(board.name, board.line);
  }
  for (const world of v2.worlds) {
    if (domains.has(world.name)) throw new ParseError(`Duplicate domain '${world.name}'`, world.line);
    domains.set(world.name, world.line);
  }

  for (const field of v2.state) {
    if (
      (field.type === "coord" || field.type === "cells" || field.type === "coord_list") &&
      field.domain !== undefined &&
      !domains.has(field.domain)
    ) {
      throw new ParseError(`Unknown domain '${field.domain}'`, field.line);
    }
  }
}

function validateV2References(
  v2: V2ProgramAst,
  context: {
    ruleParams: Map<string, string[]>;
  },
): void {
  const visit = (statements: V2StatementAst[]): void => {
    for (const statement of statements) {
      validateV2Statement(statement, context, visit);
    }
  };
  if (v2.body.length > 0) visit(v2.body);
  for (const rule of v2.rules) visit(rule.body);
}

function validateV2Statement(
  statement: V2StatementAst,
  context: {
    ruleParams: Map<string, string[]>;
  },
  visit: (statements: V2StatementAst[]) => void,
): void {
  switch (statement.kind) {
    case "v2_show":
      if (statement.items !== undefined) return;
      if (statement.target === undefined) {
        throw new ParseError("Show must use a number or display fragments", statement.line);
      }
      if (!isNumericLiteralText(statement.target)) {
        throw new ParseError("Show must use a number or display fragments", statement.line);
      }
      return;
    case "v2_invoke":
      if (!context.ruleParams.has(statement.name)) {
        throw new ParseError(`Unknown function '${statement.name}'`, statement.line);
      }
      {
        const expected = context.ruleParams.get(statement.name)!.length;
        if (statement.args.length !== expected) {
          throw new ParseError(
            `Function '${statement.name}' expects ${expected} argument${expected === 1 ? "" : "s"}, got ${statement.args.length}`,
            statement.line,
          );
        }
      }
      return;
    case "v2_if":
      visit(statement.thenBody);
      if (statement.elseBody) visit(statement.elseBody);
      return;
    case "v2_while":
      visit(statement.body);
      return;
    case "v2_loop":
      visit(statement.body);
      return;
    case "v2_match":
      for (const matchCase of statement.cases) visit([matchCase.action]);
      if (statement.otherwise) visit([statement.otherwise]);
      return;
    default:
      return;
  }
}

function collectV2CellMapNames(v2: V2ProgramAst, stateDomains: Map<string, string>): Map<string, string> {
  const explicit = v2.state.find((field) => field.type === "packed" && /^(?:plan|map)$/iu.test(field.name));
  const byDomain = new Map<string, string>();
  const addDomain = (domain: string): void => {
    if (byDomain.has(domain)) return;
    const domainSpecific = v2.state.find((field) =>
      field.type === "packed" && new RegExp(`^${escapeRegExp(domain)}_(?:plan|map)$`, "iu").test(field.name),
    );
    const decimalPlayerCells = singleDecimalPlayerCellsField(v2, domain);
    byDomain.set(domain, domainSpecific?.name ?? explicit?.name ?? decimalPlayerCells?.name ?? `__cell_map_${domain}`);
  };

  for (const text of collectV2ExpressionTexts(v2)) {
    for (const args of findSpatialCalls(text, "cell_at")) {
      if (args.length === 2) {
        addDomain(args[0]!);
        continue;
      }
      if (args.length === 1) {
        const domain = stateDomains.get(args[0]!);
        if (domain !== undefined) addDomain(domain);
      }
    }
  }
  return byDomain;
}

function singleDecimalPlayerCellsField(v2: V2ProgramAst, domain: string): V2StateFieldAst | undefined {
  const world = v2.worlds.find((candidate) => candidate.name === domain);
  if (world?.position?.encoding !== "decimal_player") return undefined;
  const fields = v2.state.filter((field) => field.type === "cells" && field.domain === domain);
  return fields.length === 1 ? fields[0] : undefined;
}

function collectV2ExpressionTexts(v2: V2ProgramAst): string[] {
  const texts: string[] = [];
  const visit = (statements: V2StatementAst[]): void => {
    for (const statement of statements) {
      switch (statement.kind) {
        case "v2_assign":
          texts.push(statement.expr);
          break;
        case "v2_update":
          texts.push(statement.expr);
          break;
        case "v2_stop":
          texts.push(statement.value);
          break;
        case "v2_if":
          if (statement.predicate.kind === "v2_contains") {
            texts.push(statement.predicate.collection, statement.predicate.item);
          } else {
            texts.push(statement.predicate.left, statement.predicate.right);
          }
          visit(statement.thenBody);
          if (statement.elseBody !== undefined) visit(statement.elseBody);
          break;
        case "v2_while":
          if (statement.predicate.kind === "v2_contains") {
            texts.push(statement.predicate.collection, statement.predicate.item);
          } else {
            texts.push(statement.predicate.left, statement.predicate.right);
          }
          visit(statement.body);
          break;
        case "v2_loop":
          visit(statement.body);
          break;
        case "v2_match":
          texts.push(statement.expr);
          for (const matchCase of statement.cases) {
            texts.push(...matchCase.values);
            visit([matchCase.action]);
          }
          if (statement.otherwise !== undefined) visit([statement.otherwise]);
          break;
        case "v2_invoke":
          texts.push(...statement.args);
          break;
        case "v2_show":
        case "v2_read":
        case "v2_raw":
          break;
      }
    }
  };
  if (v2.body.length > 0) visit(v2.body);
  for (const rule of v2.rules) visit(rule.body);
  return texts;
}

function lowerV2Domains(v2: V2ProgramAst): DomainAst[] {
  const domains: DomainAst[] = [];
  for (const board of v2.boards) {
    const lines: RawBlockLine[] = [
      { text: `x ${board.xMin}..${board.xMax}`, line: board.line },
      { text: `y ${board.yMin}..${board.yMax}`, line: board.line },
      { text: `columns ${board.width}`, line: board.line },
      { text: `rows ${board.height}`, line: board.line },
    ];
    domains.push({
      kind: "domain",
      domainKind: "maze",
      name: board.name,
      header: `board ${board.name}`,
      lines,
      line: board.line,
    });
  }
  for (const world of v2.worlds) {
    const coordFields = v2.state.filter((field) => field.type === "coord" && field.domain === world.name);
    for (const field of coordFields) {
      const lines: RawBlockLine[] = [];
      if (world.position?.encoding !== undefined) lines.push({ text: `encoding ${world.position.encoding}`, line: world.position.line });
      domains.push({
        kind: "domain",
        domainKind: "coord",
        name: "packed",
        header: `coord packed ${field.name}`,
        lines,
        line: field.line,
      });
    }
    const worldLines: RawBlockLine[] = [];
    domains.push({
      kind: "domain",
      domainKind: "maze",
      name: world.name,
      header: `maze ${world.name}`,
      lines: worldLines,
      line: world.line,
    });
  }
  for (const field of v2.state) {
    if (field.type === "cells") {
      const lines: RawBlockLine[] = [];
      if (field.domain !== undefined) lines.push({ text: `domain ${field.domain}`, line: field.line });
      domains.push({
        kind: "domain",
        domainKind: "bitset",
        name: field.name,
        header: `cells ${field.name}`,
        lines,
        line: field.line,
      });
    }
  }
  return domains;
}

function lowerV2State(
  v2: V2ProgramAst,
  specializedRules: Set<string>,
  cellMapNames: Map<string, string>,
  context: V2LoweringContext,
): StateAst[] {
  const fields: StateFieldAst[] = [];
  for (const field of v2.state) {
    if (field.bank !== undefined) {
      fields.push(...lowerV2BankStateField(field, context));
      continue;
    }
    if (field.type === "coord_list") {
      fields.push(...lowerV2CoordListState(field, context));
      continue;
    }
    const lowered: StateFieldAst = {
      name: field.name,
      type: lowerV2StateFieldType(field.type),
      line: field.line,
    };
    if (field.min !== undefined) lowered.min = field.min;
    if (field.max !== undefined) lowered.max = field.max;
    if (field.initial !== undefined) {
      if (isIndexedInitializerList(field.initial)) {
        throw new ParseError("Indexed initializer lists require an indexed state bank", field.line);
      }
      const stackSource = parseStackSource(field.initial, field.line);
      if (stackSource !== undefined) {
        lowered.initialStack = stackSource;
      } else {
        lowered.initial = lowerV2InitialExpression(field, context);
      }
    }
    fields.push(lowered);
  }
  const declared = new Set(fields.map((field) => field.name));
  for (const scratch of collectV2ScratchFields(v2, specializedRules)) {
    if (declared.has(scratch.name)) continue;
    declared.add(scratch.name);
    fields.push(scratch);
  }
  for (const mapName of cellMapNames.values()) {
    if (!mapName.startsWith("__cell_map_") || declared.has(mapName)) continue;
    declared.add(mapName);
    fields.push({
      name: mapName,
      type: "packed",
      initial: lowerV2Expression("int(random() * 999999999) + 1", v2.line),
      line: v2.line,
    });
  }
  return fields.length > 0 ? [{ kind: "state", name: v2.name, fields, line: v2.line }] : [];
}

function lowerV2StateBanks(v2: V2ProgramAst): StateBankAst[] | undefined {
  const byBank = new Map<string, V2StateFieldAst[]>();
  for (const field of v2.state) {
    if (field.bank === undefined) continue;
    const fields = byBank.get(field.bank.name) ?? [];
    fields.push(field);
    byBank.set(field.bank.name, fields);
  }
  const banks: StateBankAst[] = [];
  for (const [name, fields] of byBank) {
    const first = fields[0]!;
    const min = first.bank!.min;
    const max = first.bank!.max;
    if (fields.some((field) => field.bank!.min !== min || field.bank!.max !== max)) {
      throw new ParseError(`State bank '${name}' has inconsistent ranges`, first.line);
    }
    banks.push({
      kind: "state_bank",
      name,
      min,
      max,
      members: fields.map((field) => {
        const member: StateBankAst["members"][number] = {
          ...(field.bank!.member === undefined ? {} : { name: field.bank!.member }),
          type: lowerV2StateFieldType(field.type),
          elements: bankIndexes(field).map((index) => ({ index, name: bankElementStateName(field, index) })),
          line: field.line,
        };
        if (field.min !== undefined) member.min = field.min;
        if (field.max !== undefined) member.max = field.max;
        return member;
      }),
      line: first.line,
    });
  }
  return banks.length === 0 ? undefined : banks;
}

function lowerV2BankStateField(field: V2StateFieldAst, context: V2LoweringContext): StateFieldAst[] {
  const indexes = bankIndexes(field);
  const initializers = indexedInitializerList(field, indexes.length);
  return indexes.map((index, offset) => {
    const lowered: StateFieldAst = {
      name: bankElementStateName(field, index),
      type: lowerV2StateFieldType(field.type),
      line: field.line,
    };
    if (field.min !== undefined) lowered.min = field.min;
    if (field.max !== undefined) lowered.max = field.max;
    const initial = initializers?.[offset] ?? field.initial;
    if (initial !== undefined) {
      const stackSource = parseStackSource(initial, field.line);
      if (stackSource !== undefined) {
        throw new ParseError("Indexed state banks cannot be initialized from stack.X or stack.Y", field.line);
      }
      lowered.initial = lowerV2InitialExpression({ ...field, initial }, context);
    }
    return lowered;
  });
}

function isIndexedInitializerList(initial: string): boolean {
  const trimmed = initial.trim();
  return trimmed.startsWith("[") || trimmed.endsWith("]");
}

function indexedInitializerList(field: V2StateFieldAst, expected: number): string[] | undefined {
  if (field.initial === undefined) return undefined;
  const trimmed = field.initial.trim();
  if (!isIndexedInitializerList(trimmed)) return undefined;
  if (!trimmed.startsWith("[") || !trimmed.endsWith("]")) {
    throw new ParseError("Indexed initializer list must look like '[a, b, c]'", field.line);
  }
  const values = splitArgs(trimmed.slice(1, -1));
  if (values.length !== expected) {
    throw new ParseError(`Indexed initializer list has ${values.length} values for ${expected} elements`, field.line);
  }
  return values;
}

function randomCoordinateExpression(domain: string, context: V2LoweringContext): string {
  const board = context.boards.get(domain);
  if (board !== undefined) return randomBoardCoordinateExpression(board);
  const world = context.worlds.get(domain);
  if (world !== undefined) return randomWorldCoordinateExpression(world);
  return "int(random(9)) + 1";
}

function randomBoardCoordinateExpression(board: V2BoardAst): string {
  if (board.height === 1) return addOffsetExpression(`int(random(${board.width}))`, board.xMin);
  if (board.width === 1) return addOffsetExpression(`int(random(${board.height}))`, board.yMin);
  const x = addOffsetExpression(`int(random(${board.width}))`, board.xMin);
  const y = addOffsetExpression(`int(random(${board.height}))`, board.yMin);
  return `${x} + 10 * (${y})`;
}

function randomWorldCoordinateExpression(world: V2WorldAst): string {
  return `int(random(${worldDomainLength(world)})) + ${worldCoordinateOrigin(world)}`;
}

function worldCoordinateOrigin(world: V2WorldAst): number {
  switch (world.position?.encoding) {
    case "pier_to_ship":
    case "corridor_plan":
    case "decimal_player":
    case "floor_plan":
    case "packed_decimal_zero_run":
    case "row_scan":
    case undefined:
      return 1;
  }
  return 1;
}

function bankIndexes(field: V2StateFieldAst): number[] {
  if (field.bank === undefined) return [];
  const indexes: number[] = [];
  for (let index = field.bank.min; index <= field.bank.max; index += 1) indexes.push(index);
  return indexes;
}

function bankElementStateName(field: V2StateFieldAst, index: number): string {
  const member = field.bank?.member;
  const base = field.bank?.name ?? field.name;
  return member === undefined ? `${base}_${index}` : `${base}_${member}_${index}`;
}

function lowerV2CoordListState(field: V2StateFieldAst, context: V2LoweringContext): StateFieldAst[] {
  const list = context.coordLists.get(field.name);
  if (list === undefined) throw new ParseError(`Unknown coord_list '${field.name}'`, field.line);
  const initial = field.initial?.trim() ?? "0";
  const randomRange = coordListRandomRangeInitialExpression(initial, field.line, context);
  if (
    initial !== "random()" &&
    initial !== "random_unique()" &&
    initial !== "0" &&
    randomRange === undefined
  ) {
    throw new ParseError("coord_list initial value must be random(), random(max), random(min, max), random_unique(), or 0", field.line);
  }
  const board = context.boards.get(list.domain);
  if ((initial === "random()" || initial === "random_unique()") && board === undefined) {
    throw new ParseError(`coord_list ${initial} currently needs a board domain`, field.line);
  }
  return list.items.map((name, index) => {
    const lowered: StateFieldAst = {
      name,
      type: "packed",
      line: field.line,
    };
    if (initial === "random_unique()") {
      lowered.initial = coordListRandomItemExpression(board!, list.count, index);
    } else if (initial === "random()") {
      lowered.initial = lowerV2Expression(randomCoordinateExpression(list.domain, context), field.line, context);
    } else if (randomRange !== undefined) {
      lowered.initial = randomRange;
    }
    return lowered;
  });
}

function collectV2ScratchFields(v2: V2ProgramAst, specializedRules: Set<string>): StateFieldAst[] {
  const fields: StateFieldAst[] = [];
  const add = (name: string, line: number): void => {
    fields.push({ name, type: "packed", line });
  };
  for (const rule of v2.rules) {
    if (!specializedRules.has(rule.name)) {
      for (const param of rule.params) add(param, rule.line);
    }
    visit(rule.body);
  }
  if (v2.body.length > 0) visit(v2.body);
  return fields;

  function visit(statements: V2StatementAst[]): void {
    for (const statement of statements) {
      if (statement.kind === "v2_if") {
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "v2_while") {
        visit(statement.body);
      }
      if (statement.kind === "v2_loop") {
        visit(statement.body);
      }
      if (statement.kind === "v2_match") {
        for (const matchCase of statement.cases) visit([matchCase.action]);
        if (statement.otherwise) visit([statement.otherwise]);
      }
      if (statement.kind === "v2_read") add(statement.target, statement.line);
    }
  }
}

function lowerV2InitialExpression(field: V2StateFieldAst, context: V2LoweringContext): ExpressionAst {
  const initial = field.initial ?? "0";
  if (field.type === "cells" && initial.trim() === "random()") {
    if (decimalPlayerPackedCellsIndex(field.name, "1", context) !== undefined) {
      return lowerV2Expression("int(random() * 999999999) + 1", field.line);
    }
    return lowerV2Expression("int(random() * 999)", field.line);
  }
  return lowerV2Expression(initial, field.line, context);
}

function numberLiteral(value: number): ExpressionAst {
  return value < 0
    ? { kind: "unary", op: "-", expr: { kind: "number", raw: String(Math.abs(value)) } }
    : { kind: "number", raw: String(value) };
}

function coordListItemName(listName: string, index: number): string {
  return `__coord_list_${listName}_${index}`;
}

function coordListRandomItemExpression(board: V2BoardAst, count: number, index: number): ExpressionAst {
  return {
    kind: "call",
    callee: "__random_coord_list_item",
    args: [
      numberLiteral(board.xMin),
      numberLiteral(board.width),
      numberLiteral(board.yMin),
      numberLiteral(board.height),
      numberLiteral(count),
      numberLiteral(index),
    ],
  };
}

function coordListRandomRangeInitialExpression(
  initial: string,
  line: number,
  context: V2LoweringContext,
): ExpressionAst | undefined {
  const trimmed = initial.trim();
  if (trimmed === "random()" || trimmed === "random_unique()") return undefined;
  if (!/\brandom\s*\(/u.test(trimmed)) return undefined;
  const expr = lowerV2Expression(trimmed, line, context);
  if (isRandomRangeExpression(expr)) return expr;
  if (
    expr.kind === "call" &&
    expr.callee.toLowerCase() === "int" &&
    expr.args.length === 1 &&
    isRandomRangeExpression(expr.args[0]!)
  ) {
    return expr;
  }
  if (expr.kind === "call" && expr.callee.toLowerCase() === "random") {
    throw new ParseError(`random() expects zero, one, or two arguments, got ${expr.args.length}.`, line);
  }
  throw new ParseError("coord_list random range initial must look like random(max) or random(min, max)", line);
}

function isRandomRangeExpression(expr: ExpressionAst): boolean {
  return expr.kind === "call" && expr.callee.toLowerCase() === "random" && (expr.args.length === 1 || expr.args.length === 2);
}

function lowerV2StateFieldType(type: V2StateFieldAst["type"]): StateFieldType {
  if (type === "counter") return "range";
  if (type === "coord_list") return "packed";
  if (type === "coord" || type === "cells") return "packed";
  return type;
}

function lowerV2Screen(screen: V2ScreenAst): DisplayAst {
  return {
    kind: "display",
    name: screen.name,
    format: "packed",
    sources: screen.sources,
    items: screen.items,
    line: screen.line,
  };
}

const V2_INTERNAL_ENTRY_NAME = "\u0000main";

function lowerV2Entry(v2: V2ProgramAst, context: V2LoweringContext): EntryAst {
  if (v2.body.length > 0) {
    return {
      kind: "entry",
      name: V2_INTERNAL_ENTRY_NAME,
      body: lowerV2Statements(v2.body, context),
      line: v2.line,
    };
  }
  return {
    kind: "entry",
    name: V2_INTERNAL_ENTRY_NAME,
    body: [{ kind: "halt", expr: parseExpression("0"), line: v2.line }],
    line: v2.line,
  };
}

function lowerV2Rule(rule: V2RuleAst, context: V2LoweringContext): ProcAst {
  const proc: ProcAst = {
    kind: "proc",
    name: rule.name,
    body: lowerV2Statements(rule.body, context),
    line: rule.line,
  };
  if (rule.params.length > 0) proc.params = [...rule.params];
  return proc;
}

interface LinearDeltaPlan {
  target: string;
  slope: number;
  intercept: number;
  corrections: Map<number, number>;
}

interface EffectPlan {
  deltas: LinearDeltaPlan[];
}

function lowerEffectPlan(
  plan: EffectPlan,
  keyName: string,
  line: number,
  context: V2LoweringContext,
): StatementAst[] {
  const statements: StatementAst[] = [];
  for (const delta of plan.deltas) {
    const expr = linearDeltaExpressionText(delta, keyName);
    if (expr !== "0") {
      statements.push(deltaAssignStatement(delta.target, expr, line, context));
    }
  }
  for (const [key, corrections] of groupedCorrections(plan.deltas)) {
    statements.push({
      kind: "if",
      condition: {
        left: { kind: "identifier", name: keyName },
        op: "==",
        right: { kind: "number", raw: String(key) },
      },
      thenBody: corrections.map((correction) =>
        deltaAssignStatement(correction.target, String(correction.delta), line, context)
      ),
      line,
    });
  }
  return statements;
}

function groupedCorrections(plans: LinearDeltaPlan[]): Array<[number, Array<{ target: string; delta: number }>]> {
  const grouped = new Map<number, Array<{ target: string; delta: number }>>();
  for (const plan of plans) {
    for (const [key, delta] of plan.corrections) {
      if (delta === 0) continue;
      const corrections = grouped.get(key) ?? [];
      corrections.push({ target: plan.target, delta });
      grouped.set(key, corrections);
    }
  }
  return [...grouped.entries()].sort(([left], [right]) => left - right);
}

function fitLinearDeltaPlan(target: string, values: Map<number, number>): LinearDeltaPlan | undefined {
  const entries = [...values.entries()].sort(([left], [right]) => left - right);
  if (entries.every(([, value]) => value === 0)) {
    return { target, slope: 0, intercept: 0, corrections: new Map() };
  }

  const candidates: LinearDeltaPlan[] = [];
  for (const [, value] of entries) {
    candidates.push(linearPlanFor(target, values, 0, value));
  }
  for (let i = 0; i < entries.length; i += 1) {
    for (let j = i + 1; j < entries.length; j += 1) {
      const [leftKey, leftValue] = entries[i]!;
      const [rightKey, rightValue] = entries[j]!;
      const keyDelta = rightKey - leftKey;
      const valueDelta = rightValue - leftValue;
      if (keyDelta === 0 || valueDelta % keyDelta !== 0) continue;
      const slope = valueDelta / keyDelta;
      const intercept = leftValue - slope * leftKey;
      candidates.push(linearPlanFor(target, values, slope, intercept));
    }
  }

  return candidates
    .filter((candidate) => candidate.corrections.size <= Math.max(1, Math.floor(entries.length / 3)))
    .sort((left, right) => linearPlanCost(left) - linearPlanCost(right))[0];
}

function linearPlanFor(
  target: string,
  values: Map<number, number>,
  slope: number,
  intercept: number,
): LinearDeltaPlan {
  const corrections = new Map<number, number>();
  for (const [key, value] of values) {
    const base = slope * key + intercept;
    if (base !== value) corrections.set(key, value - base);
  }
  return { target, slope, intercept, corrections };
}

function linearPlanCost(plan: LinearDeltaPlan): number {
  const baseCost = plan.slope === 0 && plan.intercept === 0
    ? 0
    : Math.abs(plan.slope) <= 1
      ? 3
      : 5;
  return baseCost + plan.corrections.size * 6;
}

function linearDeltaExpressionText(plan: LinearDeltaPlan, keyName: string): string {
  const variable = plan.slope === 0
    ? "0"
    : plan.slope === 1
      ? keyName
      : plan.slope === -1
        ? `(0 - ${keyName})`
        : `${plan.slope} * ${keyName}`;
  if (plan.intercept === 0) return variable;
  if (plan.slope === 0) return String(plan.intercept);
  return plan.intercept > 0
    ? `${variable} + ${plan.intercept}`
    : `${variable} - ${Math.abs(plan.intercept)}`;
}

function deltaAssignStatement(
  target: string,
  deltaText: string,
  line: number,
  context: V2LoweringContext,
): Extract<StatementAst, { kind: "assign" }> {
  const delta = numericLiteralTextValue(deltaText);
  const exprText = delta !== undefined && delta < 0
    ? `${target} - ${Math.abs(delta)}`
    : `${target} + (${deltaText})`;
  return {
    kind: "assign",
    target,
    expr: lowerV2Expression(exprText, line, context),
    line,
  };
}

function numericLiteralTextValue(text: string): number | undefined {
  if (!isNumericLiteralText(text.trim())) return undefined;
  const value = Number(text.trim());
  return Number.isInteger(value) ? value : undefined;
}

function lowerV2Statements(statements: V2StatementAst[], context: V2LoweringContext): StatementAst[] {
  const lowered: StatementAst[] = [];
  for (let index = 0; index < statements.length; index += 1) {
    const statement = statements[index]!;
    const literalHalt = lowerV2InlineLiteralShowHalt(statement, statements[index + 1]);
    if (literalHalt !== undefined) {
      lowered.push(literalHalt);
      index += 1;
      continue;
    }
    lowered.push(...lowerV2Statement(statement, context));
  }
  return lowered;
}

function lowerV2InlineLiteralShowHalt(
  statement: V2StatementAst,
  next: V2StatementAst | undefined,
): Extract<StatementAst, { kind: "halt" }> | undefined {
  if (statement.kind !== "v2_show" || next?.kind !== "v2_stop") return undefined;
  if (normalizedV2Text(next.value) !== "0") return undefined;
  const literal = statement.items === undefined ? undefined : collapseV2LiteralItems(statement.items);
  if (literal !== "ЕГГОГ") return undefined;
  return { kind: "halt", expr: parseExpression("0"), literal, line: statement.line };
}

function collapseV2LiteralItems(items: DisplayItemAst[]): string | undefined {
  if (items.some((item) => item.kind !== "literal")) return undefined;
  return items.map((item) => item.kind === "literal" ? item.text : "").join("");
}

function lowerV2Statement(statement: V2StatementAst, context: V2LoweringContext): StatementAst[] {
  switch (statement.kind) {
    case "v2_show":
      if (statement.items !== undefined) {
        if (statement.inlineName === undefined) {
          throw new ParseError("Inline show display was not collected before lowering", statement.line);
        }
        return [{ kind: "show", display: statement.inlineName, line: statement.line }];
      }
      if (statement.target === undefined) {
        throw new ParseError("Show must use a number or display fragments", statement.line);
      }
      if (isNumericLiteralText(statement.target)) {
        return [{ kind: "pause", expr: parseExpression(statement.target, statement.line), line: statement.line }];
      }
      return [{ kind: "show", display: statement.target, line: statement.line }];
    case "v2_read":
      return [{
        kind: "input",
        target: statement.target,
        line: statement.line,
      }];
    case "v2_stop": {
      const literal = parseV2StopLiteral(statement.value, statement.line);
      if (literal !== undefined) {
        return [{ kind: "halt", expr: parseExpression("0"), literal, line: statement.line }];
      }
      return [{ kind: "halt", expr: lowerV2Expression(statement.value, statement.line, context), line: statement.line }];
    }
    case "v2_invoke":
      return lowerV2Invoke(statement, context);
    case "v2_if": {
      const condition = lowerV2Predicate(statement.predicate, statement.line, context);
      const lowered: IfStatementAst = {
        kind: "if",
        condition: statement.negated === true ? invertLoweredCondition(condition) : condition,
        thenBody: lowerV2Statements(statement.thenBody, context),
        line: statement.line,
      };
      if (statement.elseBody !== undefined) lowered.elseBody = lowerV2Statements(statement.elseBody, context);
      return [lowered];
    }
    case "v2_while":
      return [{
        kind: "while",
        condition: lowerV2Predicate(statement.predicate, statement.line, context),
        body: lowerV2Statements(statement.body, context),
        line: statement.line,
      }];
    case "v2_loop":
      return [{
        kind: "loop",
        body: lowerV2Statements(statement.body, context),
        line: statement.line,
      }];
    case "v2_assign":
      if (isIndexedTargetText(statement.target)) {
        return [{
          kind: "indexed_assign",
          target: indexedTargetExpression(statement.target, statement.line),
          expr: lowerV2Expression(statement.expr, statement.line, context),
          line: statement.line,
        }];
      }
      return [{ kind: "assign", target: statement.target, expr: lowerV2Expression(statement.expr, statement.line, context), line: statement.line }];
    case "v2_update": {
      if (isIndexedTargetText(statement.target)) {
        const target = indexedTargetExpression(statement.target, statement.line);
        return [{
          kind: "indexed_assign",
          target,
          expr: {
            kind: "binary",
            op: statement.op === "+=" ? "+" : "-",
            left: target,
            right: lowerV2Expression(statement.expr, statement.line, context),
          },
          line: statement.line,
        }];
      }
      if (context.stateTypes.get(statement.target) === "cells") {
        return [{
          kind: "assign",
          target: statement.target,
          expr: lowerV2Expression(cellSetUpdateExpression(statement.target, statement.expr, statement.op, context), statement.line, context),
          line: statement.line,
        }];
      }
      if (statement.op === "-=") {
        const list = context.coordLists.get(statement.target);
        if (list !== undefined) {
          return [{
            kind: "coord_list_remove",
            list: statement.target,
            item: lowerV2Expression(statement.expr, statement.line, context),
            items: list.items,
            line: statement.line,
          }];
        }
      }
      return [{
        kind: "assign",
        target: statement.target,
        expr: {
          kind: "binary",
          op: statement.op === "+=" ? "+" : "-",
          left: { kind: "identifier", name: statement.target },
          right: lowerV2Expression(statement.expr, statement.line, context),
        },
        line: statement.line,
      }];
    }
    case "v2_raw":
      return [{
        kind: "core",
        inputs: statement.inputs.map((input) => ({
          slot: input.slot,
          expr: lowerV2Expression(input.expr, input.line, context),
          line: input.line,
        })),
        outputs: statement.outputs.map((output) => ({
          slot: output.slot,
          target: output.target,
          line: output.line,
        })),
        clobbers: statement.clobbers,
        preserves: statement.preserves,
        lines: statement.lines,
        strict: true,
        line: statement.line,
      }];
    case "v2_match":
      return lowerV2MatchStatements(statement, context);
    case "v2_return":
      return [{
        kind: "return_value",
        expr: lowerV2Expression(statement.expr, statement.line, context),
        line: statement.line,
      }];
  }
}

function isIndexedTargetText(target: string): boolean {
  return target.includes("[");
}

function indexedTargetExpression(target: string, line: number): Extract<ExpressionAst, { kind: "indexed" }> {
  const expr = parseExpression(target, line);
  if (expr.kind !== "indexed") throw new ParseError(`Invalid indexed assignment target '${target}'`, line);
  return expr;
}

function lowerV2Predicate(predicate: V2PredicateAst, line: number, context: V2LoweringContext): ConditionAst {
  if (predicate.kind === "v2_contains") {
    return {
      left: lowerV2Expression(cellMembershipExpression(predicate.collection, predicate.item, context), line, context),
      op: "!=",
      right: lowerV2Expression("0", line, context),
    };
  }
  return {
    left: lowerV2Expression(predicate.left, line, context),
    op: predicate.op,
    right: lowerV2Expression(predicate.right, line, context),
  };
}

function cellSetUpdateExpression(
  collection: string,
  item: string,
  op: "+=" | "-=",
  context: V2LoweringContext,
): string {
  const digitIndex = decimalPlayerPackedCellsIndex(collection, item, context);
  if (digitIndex !== undefined) return `digit_set(${collection}, ${digitIndex}, ${op === "+=" ? "1" : "0"})`;
  const mask = cellMaskExpressionForCollection(collection, item, context);
  if (mask === undefined) return `${op === "+=" ? "bit_set" : "bit_clear"}(${collection}, ${item})`;
  return op === "+="
    ? `bit_or(${collection}, ${mask})`
    : `bit_and(${collection}, bit_not(${mask}))`;
}

function cellMembershipExpression(
  collection: string,
  item: string,
  context: V2LoweringContext,
): string {
  const list = context.coordLists.get(collection.trim());
  if (list !== undefined) return `coord_list_has(${item}, ${list.items.join(", ")})`;
  const digitIndex = decimalPlayerPackedCellsIndex(collection, item, context);
  if (digitIndex !== undefined) return `digit_at(${collection}, ${digitIndex})`;
  const mask = cellMaskExpressionForCollection(collection, item, context);
  return mask === undefined ? `bit_has(${collection}, ${item})` : `bit_and(${collection}, ${mask})`;
}

function lowerV2MatchStatements(statement: V2MatchStatementAst, context: V2LoweringContext): StatementAst[] {
  const cyclic = lowerCyclicCounterMatch(statement, context);
  if (cyclic !== undefined) return cyclic;
  const effects = lowerSimpleEffectMatch(statement, context);
  if (effects !== undefined) return effects;
  if (context.signedAbsMatchPairs) {
    const signedPair = lowerSignedAbsPairMatch(statement, context);
    if (signedPair !== undefined) return signedPair;
  }
  return lowerV2MatchStatementsAfterSignedAbs(statement, context);
}

function lowerV2MatchStatementsAfterSignedAbs(statement: V2MatchStatementAst, context: V2LoweringContext): StatementAst[] {
  if (statement.cases.length === 0) {
    return statement.otherwise === undefined ? [] : lowerV2Statement(statement.otherwise, context);
  }
  const smallCase = lowerSmallCaseMatch(statement, context);
  if (smallCase !== undefined) return [smallCase];
  const singleCase = lowerSingleCaseMatch(statement, context);
  if (singleCase !== undefined) return [singleCase];
  return [lowerV2Match(statement, context)];
}

interface SignedAbsMatchRow {
  caseIndex: number;
  valueIndex: number;
  value: number;
  action: V2StatementAst;
  line: number;
}

interface SignedAbsMatchPair {
  positive: SignedAbsMatchRow;
  negative: SignedAbsMatchRow;
  action: V2InvokeStatementAst;
}

function lowerSignedAbsPairMatch(statement: V2MatchStatementAst, context: V2LoweringContext): StatementAst[] | undefined {
  const pair = signedAbsMatchPair(statement);
  if (pair === undefined) return undefined;
  const positiveKey = signedAbsRowKey(pair.positive);
  const negativeKey = signedAbsRowKey(pair.negative);
  const cases = statement.cases
    .map((matchCase, caseIndex) => ({
      ...matchCase,
      values: matchCase.values.filter((_, valueIndex) => {
        const key = signedAbsRowKey({ caseIndex, valueIndex });
        return key !== positiveKey && key !== negativeKey;
      }),
    }))
    .filter((matchCase) => matchCase.values.length > 0);
  const guard: V2IfStatementAst = {
    kind: "v2_if",
    predicate: {
      kind: "v2_compare",
      left: `abs(${statement.expr})`,
      op: "==",
      right: String(pair.positive.value),
    },
    thenBody: [pair.action],
    line: Math.min(pair.positive.line, pair.negative.line),
  };
  if (statement.otherwise !== undefined) guard.elseBody = [statement.otherwise];
  const rewritten: V2MatchStatementAst = {
    ...statement,
    cases,
    otherwise: guard,
  };
  const originalLowered = lowerV2MatchStatementsAfterSignedAbs(statement, context);
  const rewrittenLowered = lowerV2MatchStatements(rewritten, context);
  return estimateLoweredStatementsCost(rewrittenLowered) < estimateLoweredStatementsCost(originalLowered)
    ? rewrittenLowered
    : undefined;
}

function signedAbsRowKey(row: Pick<SignedAbsMatchRow, "caseIndex" | "valueIndex">): string {
  return `${row.caseIndex}:${row.valueIndex}`;
}

function signedAbsMatchPair(statement: V2MatchStatementAst): SignedAbsMatchPair | undefined {
  if (!/^[A-Za-z_][\w]*$/u.test(statement.expr.trim())) return undefined;
  const rows: SignedAbsMatchRow[] = [];
  for (let caseIndex = 0; caseIndex < statement.cases.length; caseIndex += 1) {
    const matchCase = statement.cases[caseIndex]!;
    for (let valueIndex = 0; valueIndex < matchCase.values.length; valueIndex += 1) {
      const value = numericLiteralTextValue(matchCase.values[valueIndex]!);
      if (value === undefined || value === 0) continue;
      rows.push({ caseIndex, valueIndex, value, action: matchCase.action, line: matchCase.line });
    }
  }
  for (const positive of rows.filter((row) => row.value > 0)) {
    const negative = rows.find((row) => row.value === -positive.value);
    if (negative === undefined) continue;
    const action = signedAbsAction(positive.action, negative.action, statement.expr);
    if (action !== undefined) return { positive, negative, action };
  }
  return undefined;
}

function signedAbsAction(
  positiveAction: V2StatementAst,
  negativeAction: V2StatementAst,
  matchExpr: string,
): V2InvokeStatementAst | undefined {
  if (positiveAction.kind !== "v2_invoke" || negativeAction.kind !== "v2_invoke") return undefined;
  if (positiveAction.name !== negativeAction.name || positiveAction.args.length !== negativeAction.args.length) return undefined;
  const args: string[] = [];
  let signedArg = false;
  for (let index = 0; index < positiveAction.args.length; index += 1) {
    const positiveArg = positiveAction.args[index]!.trim();
    const negativeArg = negativeAction.args[index]!.trim();
    if (positiveArg === negativeArg) {
      args.push(positiveAction.args[index]!);
      continue;
    }
    const positiveValue = numericLiteralTextValue(positiveArg);
    const negativeValue = numericLiteralTextValue(negativeArg);
    if (signedArg || positiveValue === undefined || negativeValue === undefined) return undefined;
    if (positiveValue === 1 && negativeValue === -1) {
      args.push(`sign(${matchExpr})`);
      signedArg = true;
      continue;
    }
    if (positiveValue === -1 && negativeValue === 1) {
      args.push(`-sign(${matchExpr})`);
      signedArg = true;
      continue;
    }
    return undefined;
  }
  return signedArg ? { ...positiveAction, args } : undefined;
}

function lowerSmallCaseMatch(statement: V2MatchStatementAst, context: V2LoweringContext): IfStatementAst | undefined {
  if (statement.otherwise === undefined) return undefined;
  const rows = statement.cases.flatMap((matchCase) =>
    matchCase.values.map((value) => ({ value, action: matchCase.action, line: matchCase.line }))
  );
  if (rows.length !== 1) return undefined;
  if (statement.cases.some((matchCase) => matchCase.values.length !== 1)) return undefined;
  if (rows.length > 1 && !/^[A-Za-z_][\w]*$/u.test(statement.expr.trim())) return undefined;

  const build = (index: number): IfStatementAst => {
    const row = rows[index]!;
    const elseBody = index + 1 < rows.length
      ? [build(index + 1)]
      : lowerV2Statement(statement.otherwise!, context);
    return {
      kind: "if",
      condition: {
        left: lowerV2Expression(statement.expr, statement.line, context),
        op: "==",
        right: lowerV2Expression(row.value, row.line, context),
      },
      thenBody: lowerV2MatchAction(row.action, context, row.line),
      elseBody,
      line: index === 0 ? statement.line : row.line,
    };
  };

  return build(0);
}

function lowerSingleCaseMatch(statement: V2MatchStatementAst, context: V2LoweringContext): IfStatementAst | undefined {
  const lowered = lowerSmallCaseMatch(statement, context);
  if (lowered !== undefined) return lowered;
  if (statement.otherwise !== undefined || statement.cases.length !== 1) return undefined;
  const matchCase = statement.cases[0]!;
  if (matchCase.values.length !== 1) return undefined;
  const value = matchCase.values[0]!;
  return {
    kind: "if",
    condition: {
      left: lowerV2Expression(statement.expr, statement.line, context),
      op: "==",
      right: lowerV2Expression(value, matchCase.line, context),
    },
    thenBody: lowerV2MatchAction(matchCase.action, context, matchCase.line),
    ...(statement.otherwise === undefined ? {} : { elseBody: lowerV2Statement(statement.otherwise, context) }),
    line: statement.line,
  };
}

function lowerV2Match(statement: V2MatchStatementAst, context: V2LoweringContext): DispatchStatementAst {
  const cases: DispatchCaseAst[] = [];
  for (const matchCase of statement.cases) {
    for (const value of matchCase.values) {
      cases.push({
        value: lowerV2Expression(value, matchCase.line, context),
        body: lowerV2MatchAction(matchCase.action, context, matchCase.line),
        line: matchCase.line,
      });
    }
  }
  const lowered: DispatchStatementAst = {
    kind: "dispatch",
    expr: lowerV2Expression(statement.expr, statement.line, context),
    cases,
    line: statement.line,
    scratchId: statement.line,
  };
  if (statement.otherwise !== undefined) lowered.defaultBody = lowerV2Statement(statement.otherwise, context);
  return lowered;
}

interface CyclicMatchCase {
  value: number;
  next: number;
  deltas: Map<string, number>;
}

interface SimpleNumericEffects {
  effects: Map<string, NumericEffect>;
}

type NumericEffect =
  | { kind: "delta"; value: number }
  | { kind: "assign"; value: number };

interface SimpleEffectMatchRow {
  value: number;
  effects: SimpleNumericEffects;
  line: number;
}

function lowerSimpleEffectMatch(statement: V2MatchStatementAst, context: V2LoweringContext): StatementAst[] | undefined {
  const keyName = statement.expr.trim();
  if (!/^[A-Za-z_][\w]*$/u.test(keyName)) return undefined;

  const defaultEffects = statement.otherwise === undefined
    ? { effects: new Map<string, NumericEffect>() }
    : analyzeSimpleNumericEffects(resolveV2ActionBody(statement.otherwise, context), context);
  if (defaultEffects === undefined) return undefined;

  const rows: SimpleEffectMatchRow[] = [];
  const seen = new Set<number>();
  for (const matchCase of statement.cases) {
    for (const valueText of matchCase.values) {
      const value = numericLiteralTextValue(valueText);
      if (value === undefined || seen.has(value)) return undefined;
      seen.add(value);
      const body = resolveV2ActionBody(matchCase.action, context);
      if (body === undefined) return undefined;
      const effects = analyzeSimpleNumericEffects(body, context);
      if (effects === undefined) return undefined;
      rows.push({ value, effects, line: matchCase.line });
    }
  }
  if (rows.length < 3) return undefined;

  const lowered: StatementAst[] = [
    ...lowerNumericEffects(defaultEffects, statement.line, context),
  ];
  for (const row of rows.sort((left, right) => left.value - right.value)) {
    const correction = correctionEffects(defaultEffects, row.effects);
    if (correction === undefined) return undefined;
    const body = lowerNumericEffects(correction, row.line, context);
    if (body.length === 0) continue;
    lowered.push({
      kind: "if",
      condition: {
        left: { kind: "identifier", name: keyName },
        op: "==",
        right: { kind: "number", raw: String(row.value) },
      },
      thenBody: body,
      line: row.line,
    });
  }

  if (lowered.length <= 1) return undefined;
  const dispatch = lowerV2Match(statement, context);
  return estimateStatementCount(lowered) < estimateDispatchStatementCount(dispatch) ? lowered : undefined;
}

function analyzeSimpleNumericEffects(
  statements: V2StatementAst[] | undefined,
  context: V2LoweringContext,
): SimpleNumericEffects | undefined {
  if (statements === undefined) return undefined;
  const effects = new Map<string, NumericEffect>();
  for (const statement of statements) {
    if (statement.kind === "v2_update" && context.stateTypes.get(statement.target) !== "cells") {
      if (!context.stateTypes.has(statement.target)) return undefined;
      const value = numericLiteralTextValue(statement.expr);
      if (value === undefined) return undefined;
      applyNumericDelta(effects, statement.target, statement.op === "+=" ? value : -value);
      continue;
    }
    if (statement.kind === "v2_assign" && context.stateTypes.get(statement.target) !== "cells") {
      if (!context.stateTypes.has(statement.target)) return undefined;
      const value = numericLiteralTextValue(statement.expr);
      if (value === undefined) return undefined;
      effects.set(statement.target, { kind: "assign", value });
      continue;
    }
    return undefined;
  }
  return { effects };
}

function applyNumericDelta(effects: Map<string, NumericEffect>, target: string, delta: number): void {
  const current = effects.get(target);
  if (current?.kind === "assign") {
    effects.set(target, { kind: "assign", value: current.value + delta });
    return;
  }
  effects.set(target, { kind: "delta", value: (current?.value ?? 0) + delta });
}

function correctionEffects(
  base: SimpleNumericEffects,
  desired: SimpleNumericEffects,
): SimpleNumericEffects | undefined {
  const effects = new Map<string, NumericEffect>();
  const targets = new Set([...base.effects.keys(), ...desired.effects.keys()]);
  for (const target of [...targets].sort()) {
    const correction = correctionEffect(
      base.effects.get(target) ?? { kind: "delta", value: 0 },
      desired.effects.get(target) ?? { kind: "delta", value: 0 },
    );
    if (correction === undefined) return undefined;
    if (correction.kind === "delta" && correction.value === 0) continue;
    effects.set(target, correction);
  }
  return { effects };
}

function correctionEffect(base: NumericEffect, desired: NumericEffect): NumericEffect | undefined {
  if (base.kind === "delta") {
    if (desired.kind === "delta") return { kind: "delta", value: desired.value - base.value };
    return desired;
  }
  if (desired.kind === "assign") {
    return desired.value === base.value ? { kind: "delta", value: 0 } : desired;
  }
  return undefined;
}

function lowerNumericEffects(
  effects: SimpleNumericEffects,
  line: number,
  context: V2LoweringContext,
): StatementAst[] {
  return [...effects.effects.entries()].sort(([left], [right]) => left.localeCompare(right)).flatMap(
    ([target, effect]) => {
      if (effect.kind === "delta") {
        return effect.value === 0 ? [] : [deltaAssignStatement(target, String(effect.value), line, context)];
      }
      return [{
        kind: "assign" as const,
        target,
        expr: lowerV2Expression(String(effect.value), line, context),
        line,
      }];
    },
  );
}

function estimateStatementCount(statements: StatementAst[]): number {
  let count = 0;
  for (const statement of statements) {
    count += 1;
    if (statement.kind === "if") {
      count += estimateStatementCount(statement.thenBody);
      if (statement.elseBody !== undefined) count += estimateStatementCount(statement.elseBody);
    }
    if (statement.kind === "dispatch") {
      count += statement.cases.reduce((sum, item) => sum + estimateStatementCount(item.body), 0);
      if (statement.defaultBody !== undefined) count += estimateStatementCount(statement.defaultBody);
    }
    if (statement.kind === "loop") count += estimateStatementCount(statement.body);
  }
  return count;
}

function estimateDispatchStatementCount(statement: DispatchStatementAst): number {
  const bodyCost = statement.cases.reduce((sum, item) => sum + estimateStatementCount(item.body), 0);
  const defaultCost = statement.defaultBody === undefined ? 0 : estimateStatementCount(statement.defaultBody);
  const jumpsAfterCases = Math.max(0, statement.cases.length - (statement.defaultBody === undefined ? 1 : 0));
  return 2 + statement.cases.length * 5 + jumpsAfterCases * 2 + bodyCost + defaultCost;
}

function estimateLoweredStatementsCost(statements: StatementAst[]): number {
  return statements.reduce((sum, statement) => sum + estimateLoweredStatementCost(statement), 0);
}

function estimateLoweredStatementCost(statement: StatementAst): number {
  switch (statement.kind) {
    case "assign":
      return 1 + estimateLoweredExpressionCost(statement.expr);
    case "indexed_assign":
      return 2 + estimateLoweredExpressionCost(statement.target.index) + estimateLoweredExpressionCost(statement.expr);
    case "coord_list_remove":
      return 4 + estimateLoweredExpressionCost(statement.item);
    case "pause":
    case "halt":
    case "return_value":
      return 1 + estimateLoweredExpressionCost(statement.expr);
    case "if":
      return 2 + estimateLoweredConditionCost(statement.condition) +
        estimateLoweredStatementsCost(statement.thenBody) +
        (statement.elseBody === undefined ? 0 : estimateLoweredStatementsCost(statement.elseBody));
    case "while":
      return 3 + estimateLoweredConditionCost(statement.condition) + estimateLoweredStatementsCost(statement.body);
    case "loop":
      return 1 + estimateLoweredStatementsCost(statement.body);
    case "dispatch":
      return estimateDispatchStatementCost(statement);
    case "core":
      return statement.lines.length +
        (statement.inputs ?? []).reduce((sum, input) => sum + estimateLoweredExpressionCost(input.expr), 0);
    case "decimal_series":
      return statement.digits;
    case "input":
    case "show":
    case "call":
      return 1;
  }
}

function estimateDispatchStatementCost(statement: DispatchStatementAst): number {
  const bodyCost = statement.cases.reduce((sum, item) => sum + estimateLoweredStatementsCost(item.body), 0);
  const defaultCost = statement.defaultBody === undefined ? 0 : estimateLoweredStatementsCost(statement.defaultBody);
  const jumpsAfterCases = Math.max(0, statement.cases.length - (statement.defaultBody === undefined ? 1 : 0));
  return 2 + estimateLoweredExpressionCost(statement.expr) + statement.cases.length * 5 + jumpsAfterCases * 2 + bodyCost + defaultCost;
}

function estimateLoweredConditionCost(condition: ConditionAst): number {
  return 1 + estimateLoweredExpressionCost(condition.left) + estimateLoweredExpressionCost(condition.right);
}

function estimateLoweredExpressionCost(expr: ExpressionAst): number {
  switch (expr.kind) {
    case "number":
    case "string":
    case "identifier":
      return 1;
    case "indexed":
      return 2 + estimateLoweredExpressionCost(expr.index);
    case "unary":
      return 1 + estimateLoweredExpressionCost(expr.expr);
    case "binary":
      return 1 + estimateLoweredExpressionCost(expr.left) + estimateLoweredExpressionCost(expr.right);
    case "call":
      return 1 + expr.args.reduce((sum, arg) => sum + estimateLoweredExpressionCost(arg), 0);
  }
}

function lowerCyclicCounterMatch(statement: V2MatchStatementAst, context: V2LoweringContext): StatementAst[] | undefined {
  const variable = statement.expr.trim();
  if (!/^[A-Za-z_][\w]*$/u.test(variable)) return undefined;
  const range = context.stateRanges.get(variable);
  if (range?.min === undefined || range.max === undefined) return undefined;
  if (!Number.isInteger(range.min) || !Number.isInteger(range.max)) return undefined;
  if (range.max <= range.min || range.max - range.min > 12) return undefined;

  const rows: CyclicMatchCase[] = [];
  for (const matchCase of statement.cases) {
    for (const valueText of matchCase.values) {
      const value = numericLiteralTextValue(valueText);
      if (value === undefined) return undefined;
      const body = resolveV2ActionBody(matchCase.action, context);
      if (body === undefined) return undefined;
      const analyzed = analyzeCyclicMatchBody(body, variable, context);
      if (analyzed === undefined) return undefined;
      rows.push({ value, ...analyzed });
    }
  }

  if (rows.length < 3) return undefined;
  const sorted = rows.sort((left, right) => left.value - right.value);
  if (sorted.length !== range.max - range.min + 1) return undefined;
  const seen = new Set<number>();
  for (let index = 0; index < sorted.length; index += 1) {
    const row = sorted[index]!;
    const value = range.min + index;
    if (row.value !== value || seen.has(value)) return undefined;
    seen.add(value);
    const expectedNext = value === range.max ? range.min : value + 1;
    if (row.next !== expectedNext) return undefined;
  }

  const targets = new Set<string>();
  for (const row of sorted) {
    for (const target of row.deltas.keys()) targets.add(target);
  }
  if (targets.size === 0) return undefined;

  const plans: LinearDeltaPlan[] = [];
  for (const target of [...targets].sort()) {
    const values = new Map(sorted.map((row) => [row.next, row.deltas.get(target) ?? 0]));
    const plan = fitLinearDeltaPlan(target, values);
    if (plan === undefined) return undefined;
    if (plan.slope !== 0 || plan.intercept !== 0 || plan.corrections.size > 0) plans.push(plan);
  }
  if (plans.length === 0) return undefined;

  return [
    {
      kind: "assign",
      target: variable,
      expr: lowerV2Expression(`${variable} + 1`, statement.line, context),
      line: statement.line,
    },
    {
      kind: "if",
      condition: {
        left: { kind: "identifier", name: variable },
        op: ">",
        right: { kind: "number", raw: String(range.max) },
      },
      thenBody: [{
        kind: "assign",
        target: variable,
        expr: { kind: "number", raw: String(range.min) },
        line: statement.line,
      }],
      line: statement.line,
    },
    ...lowerEffectPlan({ deltas: plans }, variable, statement.line, context),
  ];
}

function resolveV2ActionBody(
  action: V2StatementAst,
  context: V2LoweringContext,
): V2StatementAst[] | undefined {
  if (action.kind !== "v2_invoke") return [action];
  const rule = context.rules.get(action.name);
  if (rule === undefined) return undefined;
  if (rule.params.length === 0) return rule.body;
  if (rule.params.length !== action.args.length) return undefined;
  return substituteV2Statements(rule.body, invokeReplacements(rule, action.args));
}

function analyzeCyclicMatchBody(
  statements: V2StatementAst[],
  variable: string,
  context: V2LoweringContext,
): { next: number; deltas: Map<string, number> } | undefined {
  let next: number | undefined;
  const deltas = new Map<string, number>();
  for (const statement of statements) {
    if (statement.kind === "v2_assign" && statement.target === variable) {
      const value = numericLiteralTextValue(statement.expr);
      if (value === undefined || next !== undefined) return undefined;
      next = value;
      continue;
    }
    if (statement.kind === "v2_update" && context.stateTypes.get(statement.target) !== "cells") {
      const value = numericLiteralTextValue(statement.expr);
      if (value === undefined || statement.target === variable) return undefined;
      const delta = statement.op === "+=" ? value : -value;
      deltas.set(statement.target, (deltas.get(statement.target) ?? 0) + delta);
      continue;
    }
    return undefined;
  }
  return next === undefined ? undefined : { next, deltas };
}

function invertLoweredCondition(condition: ConditionAst): ConditionAst {
  return {
    ...condition,
    op: invertLoweredComparisonOp(condition.op),
  };
}

function invertLoweredComparisonOp(op: ConditionAst["op"]): ConditionAst["op"] {
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

function lowerV2MatchAction(
  action: V2StatementAst,
  context: V2LoweringContext,
  line: number,
): StatementAst[] {
  if (action.kind !== "v2_invoke") return lowerV2Statement(action, context);
  const rule = context.rules.get(action.name);
  if (rule !== undefined && context.specializedRules.has(rule.name)) {
    return lowerV2Statements(substituteV2Statements(rule.body, invokeReplacements(rule, action.args)), context);
  }
  const params = context.ruleParams.get(action.name) ?? [];
  const statements: StatementAst[] = [];
  for (let index = 0; index < Math.min(params.length, action.args.length); index += 1) {
    const arg = action.args[index]!;
    statements.push({
      kind: "assign",
      target: params[index]!,
      expr: lowerV2Expression(arg, line, context),
      line,
    });
  }
  statements.push({ kind: "call", block: action.name, line: action.line });
  return statements;
}

function lowerV2Invoke(statement: V2InvokeStatementAst, context: V2LoweringContext): StatementAst[] {
  const rule = context.rules.get(statement.name);
  if (rule !== undefined && context.specializedRules.has(rule.name)) {
    return lowerV2Statements(substituteV2Statements(rule.body, invokeReplacements(rule, statement.args)), context);
  }
  const params = context.ruleParams.get(statement.name) ?? [];
  const statements: StatementAst[] = [];
  for (let index = 0; index < Math.min(params.length, statement.args.length); index += 1) {
    statements.push({
      kind: "assign",
      target: params[index]!,
      expr: lowerV2Expression(statement.args[index]!, statement.line, context),
      line: statement.line,
    });
  }
  statements.push({ kind: "call", block: statement.name, line: statement.line });
  return statements;
}

function substituteV2Statements(statements: V2StatementAst[], replacements: Map<string, string>): V2StatementAst[] {
  return statements.map((statement) => substituteV2Statement(statement, replacements));
}

function substituteV2Statement(statement: V2StatementAst, replacements: Map<string, string>): V2StatementAst {
  switch (statement.kind) {
    case "v2_assign":
      return {
        ...statement,
        target: substituteV2Text(statement.target, replacements),
        expr: substituteV2Text(statement.expr, replacements),
      };
    case "v2_update":
      return {
        ...statement,
        target: substituteV2Text(statement.target, replacements),
        expr: substituteV2Text(statement.expr, replacements),
      };
    case "v2_show":
      return substituteV2ShowStatement(statement, replacements);
    case "v2_if": {
      const substituted: V2IfStatementAst = {
        ...statement,
        predicate: substituteV2Predicate(statement.predicate, replacements),
        thenBody: substituteV2Statements(statement.thenBody, replacements),
      };
      if (statement.elseBody !== undefined) {
        substituted.elseBody = substituteV2Statements(statement.elseBody, replacements);
      }
      return substituted;
    }
    case "v2_while": {
      const substituted: V2WhileStatementAst = {
        ...statement,
        predicate: substituteV2Predicate(statement.predicate, replacements),
        body: substituteV2Statements(statement.body, replacements),
      };
      return substituted;
    }
    case "v2_loop": {
      const substituted: V2LoopStatementAst = {
        ...statement,
        body: substituteV2Statements(statement.body, replacements),
      };
      return substituted;
    }
    case "v2_match": {
      const substituted: V2MatchStatementAst = {
        ...statement,
        expr: substituteV2Text(statement.expr, replacements),
        cases: statement.cases.map((matchCase) => ({
          ...matchCase,
          values: matchCase.values.map((value) => substituteV2Text(value, replacements)),
          action: substituteV2Statement(matchCase.action, replacements),
        })),
      };
      if (statement.otherwise !== undefined) {
        substituted.otherwise = substituteV2Statement(statement.otherwise, replacements);
      }
      return substituted;
    }
    case "v2_invoke":
      return { ...statement, args: statement.args.map((arg) => substituteV2Text(arg, replacements)) };
    case "v2_stop":
      return { ...statement, value: substituteV2Text(statement.value, replacements) };
    case "v2_return":
      return { ...statement, expr: substituteV2Text(statement.expr, replacements) };
    default:
      return statement;
  }
}

function substituteV2ShowStatement(
  statement: V2ShowStatementAst,
  replacements: Map<string, string>,
): V2ShowStatementAst {
  if (statement.target !== undefined) {
    return { ...statement, target: substituteV2Text(statement.target, replacements) };
  }
  if (statement.items === undefined) return statement;
  let changed = false;
  const items = statement.items.map((item): DisplayItemAst => {
    if (item.kind !== "source") return item;
    const replacement = replacements.get(item.name);
    if (replacement === undefined) return item;
    changed = true;
    if (isNumericLiteralText(replacement) && item.width === undefined && item.pad === undefined) {
      return { kind: "literal", text: replacement, line: item.line };
    }
    if (/^[A-Za-z_][\w]*$/u.test(replacement)) return { ...item, name: replacement };
    return item;
  });
  if (!changed) return statement;
  const literal = displayLiteralText(items);
  const numericLiteral = literal === undefined ? undefined : canonicalNumericDisplayLiteralText(literal);
  if (numericLiteral !== undefined) {
    return { kind: "v2_show", target: numericLiteral, line: statement.line };
  }
  const substituted: V2ShowStatementAst = { ...statement, items };
  delete substituted.inlineName;
  return substituted;
}

function displayLiteralText(items: readonly DisplayItemAst[]): string | undefined {
  if (items.some((item) => item.kind !== "literal")) return undefined;
  return items.map((item) => item.kind === "literal" ? item.text : "").join("");
}

function canonicalNumericDisplayLiteralText(text: string): string | undefined {
  if (text !== text.trim()) return undefined;
  if (!isNumericLiteralText(text)) return undefined;
  const value = Number(text);
  if (!Number.isFinite(value)) return undefined;
  const canonical = String(value);
  return canonical === text ? canonical : undefined;
}

function substituteV2Predicate(predicate: V2PredicateAst, replacements: Map<string, string>): V2PredicateAst {
  if (predicate.kind === "v2_contains") {
    return {
      ...predicate,
      collection: substituteV2Text(predicate.collection, replacements),
      item: substituteV2Text(predicate.item, replacements),
    };
  }
  return {
    ...predicate,
    left: substituteV2Text(predicate.left, replacements),
    right: substituteV2Text(predicate.right, replacements),
  };
}

function substituteV2Text(text: string, replacements: Map<string, string>): string {
  let result = text;
  for (const [name, value] of replacements) {
    const escaped = escapeRegExp(name);
    const replacement = isSimpleSubstitutionAtom(value) ? value : `(${value})`;
    result = result.replace(new RegExp(`\\b${escaped}\\b`, "gu"), replacement);
  }
  return result;
}

function isSimpleSubstitutionAtom(value: string): boolean {
  const trimmed = value.trim();
  if (isNumericLiteralText(trimmed) || /^[A-Za-z_][\w]*$/u.test(trimmed)) return true;
  try {
    return parseExpression(trimmed).kind === "indexed";
  } catch {
    return false;
  }
}

function escapeRegExp(text: string): string {
  return text.replace(/[.*+?^${}()|[\]\\]/gu, "\\$&");
}

function lowerV2Expression(text: string, line: number, context?: V2LoweringContext): ExpressionAst {
  const rewritten = rewriteSpatialExpressionText(text, context);
  const contextual = context === undefined ? rewritten : normalizeContextualFloorAccessText(rewritten, context);
  const normalized = normalizeV2ExpressionText(contextual);
  let expr = parseExpression(normalized, line);
  if (context !== undefined && context.consts.size > 0) {
    expr = inlineConstIdentifiers(expr, context.consts);
  }
  return context === undefined ? expr : lowerDomainRandomCalls(expr, context, line);
}

function parseV2ConstLine(line: SourceLine): V2ConstAst {
  const match = /^const\s+([A-Za-z_][\w]*)\s*=\s*(.+)$/u.exec(line.text);
  if (!match) {
    throw new ParseError("Const must look like 'const name = <expr>'", line.line);
  }
  return {
    kind: "v2_const",
    name: match[1]!,
    expr: match[2]!.trim(),
    line: line.line,
  };
}

function rewriteSpatialExpressionText(text: string, context: V2LoweringContext | undefined): string {
  if (context === undefined) return text;
  let rewritten = replaceSpatialCall(text, "cell_at", (args) => cellAtExpression(args, context));
  rewritten = replaceSpatialCall(rewritten, "line_count", (args) => coordListLineCountExpression(args, context));
  return rewritten;
}

function lowerDomainRandomCalls(expr: ExpressionAst, context: V2LoweringContext, line: number): ExpressionAst {
  switch (expr.kind) {
    case "number":
    case "string":
    case "identifier":
      return expr;
    case "indexed":
      return { ...expr, index: lowerDomainRandomCalls(expr.index, context, line) };
    case "unary":
      return { ...expr, expr: lowerDomainRandomCalls(expr.expr, context, line) };
    case "binary":
      return {
        ...expr,
        left: lowerDomainRandomCalls(expr.left, context, line),
        right: lowerDomainRandomCalls(expr.right, context, line),
      };
    case "call": {
      const callee = expr.callee.toLowerCase();
      const firstArg = expr.args[0];
      if (callee === "random" && expr.args.length === 1 && firstArg?.kind === "identifier") {
        if (domainLength(firstArg.name, context) !== undefined) {
          return lowerV2Expression(randomCoordinateExpression(firstArg.name, context), line, context);
        }
      }
      return {
        ...expr,
        args: expr.args.map((arg) => lowerDomainRandomCalls(arg, context, line)),
      };
    }
  }
}

function replaceSpatialCall(
  text: string,
  name: string,
  replacer: (args: string[]) => string | undefined,
): string {
  const pattern = new RegExp(`\\b${name}\\s*\\(([^()]*)\\)`, "gu");
  return text.replace(pattern, (match, rawArgs: string) => replacer(splitArgs(rawArgs)) ?? match);
}

function findSpatialCalls(text: string, name: string): string[][] {
  const pattern = new RegExp(`\\b${name}\\s*\\(([^()]*)\\)`, "gu");
  return [...text.matchAll(pattern)].map((match) => splitArgs(match[1]!));
}

function cellAtExpression(args: string[], context: V2LoweringContext): string | undefined {
  const [domain, pos] = cellAtDomainAndPosition(args, context);
  if (domain === undefined || pos === undefined) return undefined;
  const map = context.cellMapNames.get(domain) ?? `__cell_map_${domain}`;
  return `digit_at(${map}, ${cellAtIndexExpression(pos, domain, context)})`;
}

function coordListLineCountExpression(args: string[], context: V2LoweringContext): string | undefined {
  if (args.length !== 2) return undefined;
  const [collection, item] = args;
  if (collection === undefined || item === undefined) return undefined;
  const list = context.coordLists.get(collection.trim());
  if (list === undefined) return undefined;
  return `coord_list_line_count(${item}, ${list.items.join(", ")})`;
}

function cellAtDomainAndPosition(
  args: string[],
  context: V2LoweringContext,
): [string | undefined, string | undefined] {
  if (args.length === 2) return [args[0], args[1]];
  if (args.length !== 1) return [undefined, undefined];
  return [context.stateDomains.get(args[0]!), args[0]];
}

function cellAtIndexExpression(pos: string, domain: string, context: V2LoweringContext): string {
  const world = context.worlds.get(domain);
  switch (world?.position?.encoding) {
    case "row_scan":
    case "floor_plan":
    case "decimal_player":
    case "pier_to_ship":
      if (isSingleDecimalDigitExpression(pos, context)) return pos;
      return decimalOnesExpression(pos);
    case "corridor_plan":
    case "packed_decimal_zero_run":
    case undefined:
      return pos;
  }
  return pos;
}

function isSingleDecimalDigitExpression(expr: string, context: V2LoweringContext): boolean {
  const range = context.stateRanges.get(expr.trim());
  return range?.min !== undefined && range.max !== undefined && range.min >= 0 && range.max <= 9;
}

function boardForCells(mask: string, context: V2LoweringContext): V2BoardAst | undefined {
  const domain = context.stateDomains.get(mask.trim());
  return domain === undefined ? undefined : context.boards.get(domain);
}

function oneDimensionalCellMaskExpression(
  mask: string,
  cell: string,
  context: V2LoweringContext,
): string | undefined {
  const board = boardForCells(mask, context);
  if (board === undefined) return undefined;
  if (board.height === 1 && board.xMin >= 0 && board.xMax <= 7) return `pow10(${cell})`;
  if (board.width === 1 && board.yMin >= 0 && board.yMax <= 7) return `pow10(${cell})`;
  return undefined;
}

function cellMaskExpressionForCollection(
  mask: string,
  cell: string,
  context: V2LoweringContext,
): string | undefined {
  const oneDimensional = oneDimensionalCellMaskExpression(mask, cell, context);
  if (oneDimensional !== undefined) return oneDimensional;
  const domain = context.stateDomains.get(mask.trim());
  const world = domain === undefined ? undefined : context.worlds.get(domain);
  if (world?.position?.encoding === "packed_decimal_zero_run") return `frac(${cell})`;
  return undefined;
}

function decimalPlayerPackedCellsIndex(
  collection: string,
  cell: string,
  context: V2LoweringContext,
): string | undefined {
  const name = collection.trim();
  if (context.stateTypes.get(name) !== "cells") return undefined;
  const domain = context.stateDomains.get(name);
  if (domain === undefined) return undefined;
  const world = context.worlds.get(domain);
  if (world?.position?.encoding !== "decimal_player") return undefined;
  const mapName = context.cellMapNames.get(domain) ?? singleCellsFieldNameForDomain(context, domain);
  if (mapName !== name) return undefined;
  return cellAtIndexExpression(cell, domain, context);
}

function singleCellsFieldNameForDomain(context: V2LoweringContext, domain: string): string | undefined {
  const names = [...context.stateDomains.entries()]
    .filter(([name, fieldDomain]) => fieldDomain === domain && context.stateTypes.get(name) === "cells")
    .map(([name]) => name);
  return names.length === 1 ? names[0] : undefined;
}

function decimalOnesExpression(expr: string): string {
  return `(${expr}) - 10 * int((${expr}) / 10)`;
}

function range(start: number, end: number): number[] {
  const values: number[] = [];
  for (let value = start; value <= end; value += 1) values.push(value);
  return values;
}

function domainLength(domain: string, context: V2LoweringContext): number | undefined {
  const name = domain.trim();
  const board = context.boards.get(name);
  if (board !== undefined) return board.width * board.height;
  const world = context.worlds.get(name);
  if (world !== undefined) return worldDomainLength(world);
  return undefined;
}

function worldDomainLength(world: V2WorldAst): number {
  switch (world.position?.encoding) {
    case "pier_to_ship":
      return 8;
    case "corridor_plan":
    case "decimal_player":
    case "floor_plan":
    case "packed_decimal_zero_run":
    case "row_scan":
    case undefined:
      return 9;
  }
  return 9;
}

function addOffsetExpression(expr: string, offset: number): string {
  if (offset === 0) return expr;
  if (offset > 0) return `${expr} + ${offset}`;
  return `${expr} - ${Math.abs(offset)}`;
}

export function normalizeV2ExpressionText(text: string): string {
  return text
    .trim()
    .replace(/\b([A-Za-z_][\w]*)\.floor\b/gu, "int($1 / 100)");
}

function normalizeContextualFloorAccessText(text: string, context: V2LoweringContext): string {
  return text.replace(/\b([A-Za-z_][\w]*)\.floor\b/gu, (match, name: string) => {
    const domain = context.stateDomains.get(name);
    const world = domain === undefined ? undefined : context.worlds.get(domain);
    return world?.position?.encoding === "packed_decimal_zero_run" ? `int(${name})` : match;
  });
}

function parseStackSource(text: string, line: number): "X" | "Y" | undefined {
  const trimmed = text.trim();
  if (/^input\.(X|Y)$/u.test(trimmed)) {
    throw new ParseError(`Use '${trimmed.replace("input.", "stack.")}' for startup stack values`, line);
  }
  const match = /^stack\.(X|Y)$/u.exec(trimmed);
  return match?.[1] as "X" | "Y" | undefined;
}

function isNumericLiteralText(text: string): boolean {
  return /^-?\d+(?:\.\d+)?(?:e[+-]?\d+)?$/iu.test(text.trim());
}

function parseIdentifierList(text: string): string[] {
  return text
    .split(",")
    .map((part) => part.trim())
    .filter((part) => part.length > 0);
}

function parseCommaIdentifierList(text: string, line: number): string[] {
  const trimmed = text.trim();
  if (trimmed.length === 0) return [];
  const parts = text.split(",").map((part) => part.trim());
  if (parts.some((part) => part.length === 0)) {
    throw new ParseError("Function parameters must be comma-separated identifiers", line);
  }
  return parts;
}

function displayItemSources(items: DisplayItemAst[]): string[] {
  return items.filter((item): item is Extract<DisplayItemAst, { kind: "source" }> => item.kind === "source")
    .map((item) => item.name);
}

function parseDisplayItemList(text: string, line: number): DisplayItemAst[] {
  const tokens = tokenizeDisplayItems(text, line);
  const items: DisplayItemAst[] = [];
  let pendingComma = false;
  let justReadItem = false;

  for (const token of tokens) {
    if (token.kind === "comma") {
      if (!justReadItem || pendingComma) throw new ParseError("Display comma must separate two fragments", line);
      pendingComma = true;
      justReadItem = false;
      continue;
    }
    const item = parseDisplayItem(token.text, line);
    if (pendingComma) {
      pendingComma = false;
    } else if (justReadItem) {
      throw new ParseError("Display fragments must be separated by commas", line);
    }
    pushDisplayItem(items, item);
    justReadItem = true;
  }

  if (pendingComma) throw new ParseError("Display comma must separate two fragments", line);
  return items;
}

function pushDisplayItem(items: DisplayItemAst[], item: DisplayItemAst): void {
  const previous = items.at(-1);
  if (previous?.kind === "literal" && item.kind === "literal") {
    previous.text += item.text;
    return;
  }
  items.push(item);
}

function parseDisplayItem(text: string, line: number): DisplayItemAst {
  const trimmed = text.trim();
  if (trimmed.startsWith("\"")) {
    return {
      kind: "literal",
      text: parseQuotedDisplayText(trimmed, line),
      line,
    };
  }
  if (/^\d+$/u.test(trimmed)) {
    return {
      kind: "literal",
      text: trimmed,
      line,
    };
  }
  const source = /^([A-Za-z_][\w]*)(?::(0?)(\d+))?$/u.exec(trimmed);
  if (source) {
    const [, name, zero, widthText] = source;
    const item: DisplayItemAst = { kind: "source", name: name!, line };
    if (widthText !== undefined) {
      const width = Number(widthText);
      if (!Number.isInteger(width) || width <= 0 || width > 8) {
        throw new ParseError(`Display width must be 1..8, got '${widthText}'`, line);
      }
      item.width = width;
      item.pad = zero === "0" ? "zero" : "space";
    }
    return item;
  }
  if (trimmed.includes("\"")) {
    throw new ParseError("Display fragments must be separated by commas", line);
  }
  const exprSource = /^(.*?)(?::(0?)(\d+))?$/u.exec(trimmed);
  if (exprSource) {
    const [, exprText, zero, widthText] = exprSource;
    const expr = parseExpression(exprText!.trim(), line);
    const item: DisplayItemAst = { kind: "source", name: exprText!.trim(), expr, line };
    if (widthText !== undefined) {
      const width = Number(widthText);
      if (!Number.isInteger(width) || width <= 0 || width > 8) {
        throw new ParseError(`Display width must be 1..8, got '${widthText}'`, line);
      }
      item.width = width;
      item.pad = zero === "0" ? "zero" : "space";
    }
    return item;
  }
  throw new ParseError(`Display item must be a string literal, decimal literal, state name, or expression, got '${trimmed}'`, line);
}

function parseV2StopLiteral(text: string, line: number): string | undefined {
  const trimmed = text.trim();
  return trimmed.startsWith("\"") ? parseQuotedDisplayText(trimmed, line) : undefined;
}

function parseQuotedDisplayText(text: string, line: number): string {
  try {
    const value: unknown = JSON.parse(text);
    if (typeof value === "string") return value;
  } catch {
    // Fall through to the user-facing parse error below.
  }
  throw new ParseError(`Invalid display string literal '${text}'`, line);
}

type DisplayToken =
  | { kind: "comma" }
  | { kind: "item"; text: string };

function tokenizeDisplayItems(text: string, line: number): DisplayToken[] {
  const tokens: DisplayToken[] = [];
  let index = 0;

  while (index < text.length) {
    const char = text[index]!;
    if (/\s/u.test(char)) {
      index += 1;
      continue;
    }
    if (char === ",") {
      tokens.push({ kind: "comma" });
      index += 1;
      continue;
    }
    if (char === "\"") {
      const start = index;
      index += 1;
      let escaped = false;
      let closed = false;
      while (index < text.length) {
        const current = text[index]!;
        if (escaped) {
          escaped = false;
          index += 1;
          continue;
        }
        if (current === "\\") {
          escaped = true;
          index += 1;
          continue;
        }
        if (current === "\"") {
          index += 1;
          closed = true;
          break;
        }
        index += 1;
      }
      if (!closed) throw new ParseError("Unclosed display string literal", line);
      tokens.push({ kind: "item", text: text.slice(start, index) });
      continue;
    }

    const start = index;
    let depth = 0;
    while (index < text.length) {
      const current = text[index]!;
      if (current === "(") depth += 1;
      else if (current === ")") depth = Math.max(0, depth - 1);
      else if (current === "," && depth === 0) break;
      index += 1;
    }
    tokens.push({ kind: "item", text: text.slice(start, index).trim() });
  }

  return tokens;
}

export function parseExpression(text: string, line = 0): ExpressionAst {
  return new ExpressionParser(text, line).parse();
}

class ExpressionParser {
  private readonly tokens: string[];
  private index = 0;
  private readonly source: string;
  private readonly line: number;

  constructor(source: string, line: number) {
    this.source = source;
    this.line = line;
    this.tokens = tokenizeExpression(source, line);
  }

  parse(): ExpressionAst {
    const expr = this.parseAdditive();
    if (!this.done()) {
      throw new ParseError(
        `Unexpected token '${this.peek()}' in expression '${this.source}'`,
        this.line,
      );
    }
    return expr;
  }

  private parseAdditive(): ExpressionAst {
    let left = this.parseMultiplicative();
    while (this.peekOptional() === "+" || this.peekOptional() === "-") {
      const op = this.next() as "+" | "-";
      left = { kind: "binary", op, left, right: this.parseMultiplicative() };
    }
    return left;
  }

  private parseMultiplicative(): ExpressionAst {
    let left = this.parseUnary();
    while (this.peekOptional() === "*" || this.peekOptional() === "/") {
      const op = this.next() as "*" | "/";
      left = { kind: "binary", op, left, right: this.parseUnary() };
    }
    return left;
  }

  private parseUnary(): ExpressionAst {
    if (this.peekOptional() === "-") {
      this.next();
      return { kind: "unary", op: "-", expr: this.parseUnary() };
    }
    return this.parsePrimary();
  }

  private parsePrimary(): ExpressionAst {
    const token = this.next();
    if (token === "(") {
      const expr = this.parseAdditive();
      this.expect(")");
      return expr;
    }
    if (token.startsWith("\"")) {
      return { kind: "string", text: parseQuotedDisplayText(token, this.line) };
    }
    if (/^\d+(?:\.\d+)?(?:e[+-]?\d+)?$/iu.test(token)) {
      return { kind: "number", raw: token };
    }
    if (/^[A-Za-z_А-Яа-я][\wА-Яа-я]*$/u.test(token)) {
      if (this.peekOptional() === "(") {
        this.next();
        const args: ExpressionAst[] = [];
        if (this.peekOptional() !== ")") {
          do {
            args.push(this.parseAdditive());
            if (this.peekOptional() !== ",") break;
            this.next();
          } while (!this.done());
        }
        this.expect(")");
        return { kind: "call", callee: token, args };
      }
      if (this.peekOptional() === "[") {
        this.next();
        const index = this.parseAdditive();
        this.expect("]");
        let field: string | undefined;
        if (this.peekOptional() === ".") {
          this.next();
          const member = this.next();
          if (!/^[A-Za-z_А-Яа-я][\wА-Яа-я]*$/u.test(member)) {
            throw new ParseError(
              `Expected indexed field name, got '${member}' in expression '${this.source}'`,
              this.line,
            );
          }
          field = member;
        }
        return field === undefined
          ? { kind: "indexed", base: token, index }
          : { kind: "indexed", base: token, field, index };
      }
      return { kind: "identifier", name: token };
    }
    throw new ParseError(
      `Unexpected token '${token}' in expression '${this.source}'`,
      this.line,
    );
  }

  private expect(token: string): void {
    const actual = this.next();
    if (actual !== token) {
      throw new ParseError(
        `Expected '${token}', got '${actual}' in expression '${this.source}'`,
        this.line,
      );
    }
  }

  private done(): boolean {
    return this.index >= this.tokens.length;
  }

  private peek(): string {
    const token = this.tokens[this.index];
    if (!token) {
      throw new ParseError(`Unexpected end of expression '${this.source}'`, this.line);
    }
    return token;
  }

  private peekOptional(): string | undefined {
    return this.tokens[this.index];
  }

  private next(): string {
    const token = this.peek();
    this.index += 1;
    return token;
  }
}

function tokenizeExpression(source: string, line: number): string[] {
  const tokens: string[] = [];
  const regex = /\s*([A-Za-z_А-Яа-я][\wА-Яа-я]*|\d+(?:\.\d+)?(?:e[+-]?\d+)?|==|!=|<=|>=|[()[\].+\-*/,])\s*/giy;
  let index = 0;
  while (index < source.length) {
    while (index < source.length && /\s/u.test(source[index]!)) index += 1;
    if (index >= source.length) break;
    if (source[index] === "\"") {
      const start = index;
      index += 1;
      let escaped = false;
      let closed = false;
      while (index < source.length) {
        const current = source[index]!;
        if (escaped) {
          escaped = false;
          index += 1;
          continue;
        }
        if (current === "\\") {
          escaped = true;
          index += 1;
          continue;
        }
        if (current === "\"") {
          index += 1;
          closed = true;
          break;
        }
        index += 1;
      }
      if (!closed) throw new ParseError("Unclosed expression string literal", line);
      tokens.push(source.slice(start, index));
      continue;
    }
    regex.lastIndex = index;
    const match = regex.exec(source);
    if (!match) {
      throw new ParseError(
        `Cannot tokenize expression near '${source.slice(index)}'`,
        line,
      );
    }
    tokens.push(match[1]!);
    index = regex.lastIndex;
  }
  return tokens;
}

function stripComment(text: string): string {
  let quoted = false;
  let escaped = false;
  for (let index = 0; index < text.length; index += 1) {
    const char = text[index]!;
    if (escaped) {
      escaped = false;
      continue;
    }
    if (quoted && char === "\\") {
      escaped = true;
      continue;
    }
    if (char === "\"") {
      quoted = !quoted;
      continue;
    }
    if (!quoted && char === "#") return text.slice(0, index);
    if (!quoted && char === "/" && text[index + 1] === "/") return text.slice(0, index);
  }
  return text;
}

function normalizeSourceLine(text: string, line: number): SourceLine[] {
  const stripped = stripComment(text).trim();
  if (stripped.length === 0) return [];
  return tokenizeInlineBlocks(stripped).map((lineText) => ({ text: lineText, line }));
}

function tokenizeInlineBlocks(text: string): string[] {
  const rawTokens = tokenizeInlineGrammar(text);
  const lines: string[] = [];
  let pending = "";
  for (const token of rawTokens) {
    if (token === "{") {
      if (pending.trim().length > 0) {
        lines.push(`${pending.trim()} {`);
      } else {
        lines.push("{");
      }
      pending = "";
      continue;
    }
    if (token === "}") {
      const body = pending.trim();
      if (body.length > 0) lines.push(body);
      lines.push("}");
      pending = "";
      continue;
    }
    if (pending.length > 0) pending += " ";
    pending += token;
  }
  if (pending.trim().length > 0) lines.push(pending.trim());
  return lines;
}

function tokenizeInlineGrammar(text: string): string[] {
  const tokens: string[] = [];
  let token = "";
  let parenDepth = 0;
  let bracketDepth = 0;
  let quoted = false;
  let escaped = false;

  for (let index = 0; index < text.length; index += 1) {
    const char = text[index]!;
    if (escaped) {
      token += char;
      escaped = false;
      continue;
    }
    if (quoted) {
      token += char;
      if (char === "\\") {
        escaped = true;
      } else if (char === "\"") {
        quoted = false;
      }
      continue;
    }
    if (char === "\"") {
      quoted = true;
      token += char;
      continue;
    }
    if (char === "(") {
      parenDepth += 1;
      token += char;
      continue;
    }
    if (char === ")" && parenDepth > 0) {
      parenDepth -= 1;
      token += char;
      continue;
    }
    if (char === "[") {
      bracketDepth += 1;
      token += char;
      continue;
    }
    if (char === "]" && bracketDepth > 0) {
      bracketDepth -= 1;
      token += char;
      continue;
    }
    if (char === ";" && parenDepth === 0 && bracketDepth === 0) {
      if (token.trim().length > 0) {
        tokens.push(token.trim());
      }
      token = "";
      continue;
    }
    if ((char === "{" || char === "}") && parenDepth === 0 && bracketDepth === 0) {
      if (token.trim().length > 0) {
        tokens.push(token.trim());
      }
      token = "";
      tokens.push(char);
      continue;
    }
    token += char;
  }
  if (token.trim().length > 0) tokens.push(token.trim());
  return tokens;
}
