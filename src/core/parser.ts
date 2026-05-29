import type {
  AssignStatementAst,
  BlockAst,
  ConditionAst,
  DeclarationAst,
  DisplayItemAst,
  DomainAst,
  DispatchCaseAst,
  DispatchStatementAst,
  DisplayAst,
  EntryAst,
  ExpressionAst,
  IfStatementAst,
  PreloadAst,
  ProgramAst,
  ProcAst,
  RawBlockLine,
  RawStackSlot,
  StatementAst,
  StateAst,
  StateFieldAst,
  StateFieldType,
  V2BoardAst,
  V2ChallengeStatementAst,
  V2EncounterCaseAst,
  V2EncounterTableAst,
  V2IfStatementAst,
  V2InvokeStatementAst,
  V2MatchCaseAst,
  V2MatchStatementAst,
  V2MoveStatementAst,
  V2PredicateAst,
  V2ProgramAst,
  V2RuleAst,
  V2ScreenAst,
  V2StateFieldAst,
  V2StatementAst,
  V2TurnAst,
  V2WorldAst,
  V2WorldPositionAst,
} from "./types.ts";

interface SourceLine {
  text: string;
  line: number;
}

const V2_RESERVED_RULE_NAMES = new Set([
  "challenge",
  "else",
  "if",
  "match",
  "move",
  "otherwise",
  "read",
  "show",
  "stop",
]);

export class ParseError extends Error {
  readonly line: number;

  constructor(message: string, line: number) {
    super(`${message} at line ${line}`);
    this.line = line;
  }
}

export function parseProgram(source: string): ProgramAst {
  return new MKProParser(source).parseProgram();
}

class MKProParser {
  private readonly lines: SourceLine[];
  private index = 0;

  constructor(source: string) {
    this.lines = source
      .split(/\r?\n/u)
      .map((text, offset) => ({ text: stripComment(text).trim(), line: offset + 1 }))
      .filter((line) => line.text.length > 0);
  }

  parseProgram(): ProgramAst {
    let reference: string | undefined;
    let v2: V2ProgramAst | undefined;
    const preloads: PreloadAst[] = [];
    const domains: DomainAst[] = [];
    const states: StateAst[] = [];
    const displays: DisplayAst[] = [];
    const declarations: DeclarationAst[] = [];
    const entries: EntryAst[] = [];
    const procs: ProcAst[] = [];
    const blocks: BlockAst[] = [];

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
        const lowered = lowerV2Program(v2);
        domains.push(...lowered.domains);
        states.push(...lowered.states);
        displays.push(...lowered.displays);
        entries.push(...lowered.entries);
        procs.push(...lowered.procs);
        blocks.push(...lowered.blocks);
      } else {
        throw new ParseError(`Unexpected top-level line '${line.text}'`, line.line);
      }
    }

    if (v2 === undefined) throw new ParseError("Program must contain one V2 program block", 1);

    const program: ProgramAst = {
      preloads,
      domains,
      states,
      displays,
      declarations,
      entries,
      procs,
      blocks,
    };
    if (reference !== undefined) program.reference = reference;
    if (v2 !== undefined) program.v2 = v2;
    return program;
  }

  private parseV2Program(): V2ProgramAst {
    const header = this.next();
    const match = /^program\s+([A-Za-z_][\w]*)\s*\{$/u.exec(header.text);
    if (!match) throw new ParseError("Program must look like 'program Name {'", header.line);
    const state: V2StateFieldAst[] = [];
    const screens: V2ScreenAst[] = [];
    const boards: V2BoardAst[] = [];
    const worlds: V2WorldAst[] = [];
    const encounters: V2EncounterTableAst[] = [];
    const rules: V2RuleAst[] = [];
    let turn: V2TurnAst | undefined;

    while (!this.done()) {
      const line = this.peek();
      if (line.text === "}") {
        this.index += 1;
        const program: V2ProgramAst = {
          kind: "v2_program",
          name: match[1]!,
          state,
          screens,
          boards,
          worlds,
          encounters,
          rules,
          line: header.line,
        };
        if (turn !== undefined) program.turn = turn;
        return program;
      }
      if (line.text === "state {") {
        this.index += 1;
        state.push(...this.parseV2StateBlock());
        continue;
      }
      if (line.text.startsWith("screen ")) {
        screens.push(this.parseV2Screen(line.text));
        continue;
      }
      const board = parseV2BoardDeclaration(line);
      if (board !== undefined) {
        this.index += 1;
        boards.push(board);
        continue;
      }
      if (line.text.startsWith("board ")) throw new ParseError("Board must look like 'name: board(0..9, 0..9)'", line.line);
      if (line.text.startsWith("fleet ")) throw new ParseError("Fleet blocks were removed; declare cells and counters in state", line.line);
      if (line.text.startsWith("world ")) {
        worlds.push(this.parseV2World(line.text));
        continue;
      }
      if (line.text.startsWith("encounters ")) {
        encounters.push(this.parseV2Encounters(line.text));
        continue;
      }
      if (line.text === "turn {") {
        if (turn !== undefined) throw new ParseError("Only one turn block is supported", line.line);
        this.index += 1;
        turn = {
          kind: "v2_turn",
          body: this.parseV2StatementBlock(),
          line: line.line,
        };
        continue;
      }
      if (line.text.startsWith("rule ")) {
        rules.push(this.parseV2Rule(line.text));
        continue;
      }
      throw new ParseError(`Unexpected program line '${line.text}'`, line.line);
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
      this.index += 1;
      if (line.text === "}") return fields;
      fields.push(parseV2StateField(line));
    }
    throw new ParseError("Unclosed state block", this.lines.at(-1)?.line ?? 1);
  }

  private parseV2Screen(text: string): V2ScreenAst {
    const header = this.next();
    const match = /^screen\s+([A-Za-z_][\w]*)\s*\{$/u.exec(text);
    if (!match) throw new ParseError("Screen must look like 'screen name {'", header.line);
    let sources: string[] = [];
    let items: DisplayItemAst[] = [];
    while (!this.done()) {
      const line = this.next();
      if (line.text === "}") {
        return {
          kind: "v2_screen",
          name: match[1]!,
          sources,
          items,
          line: header.line,
        };
      }
      if (line.text.startsWith("show ")) {
        items = parseDisplayItemList(line.text.slice("show ".length), line.line);
        sources = items.filter((item): item is Extract<DisplayItemAst, { kind: "source" }> => item.kind === "source")
          .map((item) => item.name);
        continue;
      }
      throw new ParseError(`Unexpected screen line '${line.text}'`, line.line);
    }
    throw new ParseError("Unclosed screen block", header.line);
  }

  private parseV2World(text: string): V2WorldAst {
    const header = this.next();
    const match = /^world\s+([A-Za-z_][\w]*)\s*\{$/u.exec(text);
    if (!match) throw new ParseError("World must look like 'world name {'", header.line);
    const world: V2WorldAst = {
      kind: "v2_world",
      name: match[1]!,
      line: header.line,
    };
    while (!this.done()) {
      const line = this.next();
      if (line.text === "}") return world;
      if (line.text.startsWith("position ") && line.text.endsWith("{")) {
        world.position = this.parseV2WorldPosition(line);
        continue;
      }
      throw new ParseError("World block only supports position blocks", line.line);
    }
    throw new ParseError("Unclosed world block", header.line);
  }

  private parseV2WorldPosition(header: SourceLine): V2WorldPositionAst {
    const match = /^position\s+([A-Za-z_][\w]*)\s*\{$/u.exec(header.text);
    if (!match) throw new ParseError("World position must look like 'position name {'", header.line);
    const position: V2WorldPositionAst = { name: match[1]!, line: header.line };
    while (!this.done()) {
      const line = this.next();
      if (line.text === "}") return position;
      if (line.text.startsWith("encoding ")) {
        position.encoding = line.text.slice("encoding ".length).trim();
        continue;
      }
      throw new ParseError(`Unexpected world position line '${line.text}'`, line.line);
    }
    throw new ParseError("Unclosed world position block", header.line);
  }

  private parseV2Encounters(text: string): V2EncounterTableAst {
    const header = this.next();
    const match = /^encounters\s+(.+?)\s*\{$/u.exec(text);
    if (!match) throw new ParseError("Encounter table must look like 'encounters expr {'", header.line);
    const cases: V2EncounterTableAst["cases"] = [];
    while (!this.done()) {
      const line = this.peek();
      if (line.text === "}") {
        this.index += 1;
        return {
          kind: "v2_encounters",
          expr: match[1]!.trim(),
          cases,
          line: header.line,
        };
      }
      const inline = /^(\S+)\s*\{\s*(.*?)\s*\}$/u.exec(line.text);
      if (inline) {
        this.index += 1;
        cases.push({
          value: inline[1]!,
          body: inline[2]!.split(";").map((part) => part.trim()).filter(Boolean).map((text) => parseV2InlineStatement(text, line.line)),
          line: line.line,
        });
        continue;
      }
      const caseMatch = /^(\S+)\s*\{$/u.exec(line.text);
      if (!caseMatch) throw new ParseError("Encounter case must look like 'value {'", line.line);
      this.index += 1;
      cases.push({
        value: caseMatch[1]!,
        body: this.parseV2StatementBlock(),
        line: line.line,
      });
    }
    throw new ParseError("Unclosed encounter table", header.line);
  }

  private parseV2Rule(text: string): V2RuleAst {
    const header = this.next();
    const match = /^rule\s+([A-Za-z_][\w]*)(?:\s+(.+?))?\s*\{$/u.exec(text);
    if (!match) throw new ParseError("Rule must look like 'rule name {' or 'rule name arg {'", header.line);
    if (V2_RESERVED_RULE_NAMES.has(match[1]!)) {
      throw new ParseError(`Rule name '${match[1]}' is reserved`, header.line);
    }
    const params = match[2] ? parseIdentifierList(match[2]) : [];
    for (const param of params) {
      if (!/^[A-Za-z_][\w]*$/u.test(param)) {
        throw new ParseError(`Invalid rule parameter '${param}'`, header.line);
      }
    }
    return {
      kind: "v2_rule",
      name: match[1]!,
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
    if (line.text.startsWith("if ") && line.text.endsWith("{")) {
      this.index += 1;
      const predicate = parseV2Predicate(line.text.slice("if ".length, -1).trim(), line.line);
      const thenBody = this.parseV2StatementBlock();
      let elseBody: V2StatementAst[] | undefined;
      const next = this.peekOptional();
      if (next?.text === "else {") {
        this.index += 1;
        elseBody = this.parseV2StatementBlock();
      }
      const statement: V2StatementAst = {
        kind: "v2_if",
        predicate,
        thenBody,
        line: line.line,
      };
      if (elseBody !== undefined) statement.elseBody = elseBody;
      return statement;
    }
    if (line.text.startsWith("challenge ") && line.text.endsWith("{")) {
      this.index += 1;
      return this.parseV2Challenge(line.text, line.line);
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

  private parseV2Challenge(text: string, line: number): V2StatementAst {
    const match =
      /^challenge\s+(.+?)\s+as\s+([A-Za-z_][\w]*)\s+using\s+([A-Za-z_][\w]*),\s*([A-Za-z_][\w]*),\s*([A-Za-z_][\w]*)\s*\{$/u.exec(
        text,
      );
    if (!match) {
      throw new ParseError("Challenge must look like 'challenge expr as memory_var using warning_screen, memory_screen, answer_input {'", line);
    }
    let successBody: V2StatementAst[] | undefined;
    let failureBody: V2StatementAst[] | undefined;
    while (!this.done()) {
      const bodyLine = this.peek();
      if (bodyLine.text === "}") {
        this.index += 1;
        if (!successBody) throw new ParseError("Challenge block must contain success { ... }", line);
        const statement: V2StatementAst = {
          kind: "v2_challenge",
          expr: match[1]!.trim(),
          successBody,
          challengeTarget: match[2]!,
          warningScreen: match[3]!,
          memoryScreen: match[4]!,
          answerInput: match[5]!,
          line,
        };
        if (failureBody !== undefined) statement.failureBody = failureBody;
        return statement;
      }
      if (bodyLine.text === "success {") {
        this.index += 1;
        successBody = this.parseV2StatementBlock();
        continue;
      }
      if (bodyLine.text === "failure {") {
        this.index += 1;
        failureBody = this.parseV2StatementBlock();
        continue;
      }
      throw new ParseError("Challenge block must contain success/failure blocks", bodyLine.line);
    }
    throw new ParseError("Unclosed challenge block", line);
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
  const match = /^([A-Za-z_][\w]*)\s*:\s*([A-Za-z_][\w]*)(?:\(([^)]*)\))?(.*)$/u.exec(line.text);
  if (!match) {
    throw new ParseError("State field must look like 'name: counter 0..9 = 0' or 'name: cells(domain) = random()'", line.line);
  }
  const typeText = match[2]!.toLowerCase();
  if (!["flag", "counter", "coord", "cells", "packed"].includes(typeText)) {
    throw new ParseError(`Unknown state type '${match[2]}'`, line.line);
  }
  const args = match[3]?.trim();
  const argList = args === undefined ? [] : splitArgs(args);
  if (typeText === "cells" && (!args || argList.length !== 1)) {
    throw new ParseError("cells state must look like 'name: cells(domain) = random()'", line.line);
  }
  if (typeText === "coord" && (!args || argList.length !== 1)) {
    throw new ParseError("coord state must look like 'name: coord(domain)'", line.line);
  }
  if (typeText !== "cells" && typeText !== "coord" && args !== undefined) {
    throw new ParseError(`State type '${match[2]}' does not take parameters`, line.line);
  }
  const tail = match[4]!.trim();
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
  if (typeText === "cells" || typeText === "coord") {
    field.domain = argList[0]!;
  }
  if (tailMatch[1] !== undefined) {
    field.min = Number(tailMatch[1]);
    field.max = Number(tailMatch[2]);
  }
  if (tailMatch[3] !== undefined) field.initial = tailMatch[3].trim();
  return field;
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
  return {
    kind: "v2_board",
    name: match[1]!,
    xMin,
    xMax,
    yMin,
    yMax,
    width: xMax - xMin + 1,
    height: yMax - yMin + 1,
    line: line.line,
  };
}

function parseV2InlineStatement(text: string, line: number): V2StatementAst {
  if (text.startsWith("show ")) {
    return { kind: "v2_show", target: text.slice("show ".length).trim(), line };
  }
  const read = /^read\s+([A-Za-z_][\w]*)$/u.exec(text);
  if (read) {
    return { kind: "v2_read", target: read[1]!, line };
  }
  if (text.startsWith("read ")) {
    throw new ParseError("Read must look like 'read name'", line);
  }
  if (text.startsWith("stop ")) {
    return { kind: "v2_stop", value: text.slice("stop ".length).trim(), line };
  }
  const move = /^move\s+([A-Za-z_][\w]*)\s+(north|south|east|west|up|down)$/u.exec(text);
  if (move) {
    const statement: V2MoveStatementAst = {
      kind: "v2_move",
      target: move[1]!,
      line,
    };
    if (move[2] !== undefined) statement.direction = parseV2MoveDirection(move[2], line);
    return statement;
  }
  if (text.startsWith("move ")) {
    throw new ParseError("Move must look like 'move pos north'. Use 'pos += expr' for computed movement", line);
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
  const update = /^([A-Za-z_][\w]*)\s*(\+=|-=)\s*(.+)$/u.exec(text);
  if (update) {
    return {
      kind: "v2_update",
      target: update[1]!,
      op: update[2] as "+=" | "-=",
      expr: update[3]!.trim(),
      line,
    };
  }
  const assignment = /^([A-Za-z_][\w]*)\s*=\s*(.+)$/u.exec(text);
  if (assignment) {
    return {
      kind: "v2_assign",
      target: assignment[1]!,
      expr: assignment[2]!.trim(),
      line,
    };
  }
  const invoke = /^([A-Za-z_][\w]*)(?:\s+(.+))?$/u.exec(text);
  if (invoke) {
    return {
      kind: "v2_invoke",
      name: invoke[1]!,
      args: invoke[2] ? splitArgs(invoke[2]) : [],
      line,
    };
  }
  throw new ParseError(`Unexpected statement '${text}'`, line);
}

function parseV2Predicate(text: string, line: number): V2PredicateAst {
  const contains = /^(.+?)\s+in\s+([A-Za-z_][\w]*)$/u.exec(text);
  if (contains) {
    return {
      kind: "v2_contains",
      collection: contains[2]!.trim(),
      item: contains[1]!.trim(),
    };
  }
  const compare = /^(.+?)\s*(==|!=|<=|>=|<|>)\s*(.+)$/u.exec(text);
  if (compare) {
    return {
      kind: "v2_compare",
      left: compare[1]!.trim(),
      op: compare[2] as "==" | "!=" | "<" | "<=" | ">" | ">=",
      right: compare[3]!.trim(),
    };
  }
  throw new ParseError(`Predicate must look like 'left op right'`, line);
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
  for (let index = 0; index < text.length; index += 1) {
    const char = text[index];
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

interface V2LoweringContext {
  ruleParams: Map<string, string[]>;
  rules: Map<string, V2RuleAst>;
  specializedRules: Set<string>;
  moveDeltas: Map<string, Partial<Record<NonNullable<V2MoveStatementAst["direction"]>, string>>>;
  stateTypes: Map<string, V2StateFieldAst["type"]>;
  stateDomains: Map<string, string>;
  stateRanges: Map<string, { min?: number; max?: number }>;
  cellMapNames: Map<string, string>;
  boards: Map<string, V2BoardAst>;
  worlds: Map<string, V2WorldAst>;
}

function lowerV2Program(v2: V2ProgramAst): {
  domains: DomainAst[];
  states: StateAst[];
  displays: DisplayAst[];
  entries: EntryAst[];
  procs: ProcAst[];
  blocks: BlockAst[];
} {
  const screens = collectV2Screens(v2);
  const ruleParams = collectV2RuleParams(v2);
  const rules = collectV2Rules(v2);
  const specializedRules = selectV2RuleSpecializations(v2, rules);
  const stateDomains = new Map(
    v2.state
      .filter((field) => field.domain !== undefined)
      .map((field) => [field.name, field.domain!]),
  );
  const cellMapNames = collectV2CellMapNames(v2, stateDomains);
  validateV2Domains(v2);
  validateV2References(v2, { screens, ruleParams });
  const context: V2LoweringContext = {
    ruleParams,
    rules,
    specializedRules,
    moveDeltas: collectV2MoveDeltas(v2),
    stateTypes: new Map(v2.state.map((field) => [field.name, field.type])),
    stateDomains,
    stateRanges: collectV2StateRanges(v2),
    cellMapNames,
    boards: new Map(v2.boards.map((board) => [board.name, board])),
    worlds: new Map(v2.worlds.map((world) => [world.name, world])),
  };
  return {
    domains: lowerV2Domains(v2),
    states: lowerV2State(v2, specializedRules, cellMapNames, context),
    displays: v2.screens.map(lowerV2Screen),
    entries: [lowerV2Entry(v2, context)],
    procs: [
      ...v2.rules.filter((rule) => !specializedRules.has(rule.name)).map((rule) => lowerV2Rule(rule, context)),
      ...lowerV2EncounterRules(v2, context),
    ],
    blocks: [],
  };
}

function collectV2StateRanges(v2: V2ProgramAst): Map<string, { min?: number; max?: number }> {
  return new Map(v2.state.map((field) => {
    const range: { min?: number; max?: number } = {};
    if (field.min !== undefined) range.min = field.min;
    if (field.max !== undefined) range.max = field.max;
    return [field.name, range];
  }));
}

function collectV2RuleParams(v2: V2ProgramAst): V2LoweringContext["ruleParams"] {
  const ruleParams = new Map<string, string[]>();
  for (const rule of v2.rules) {
    if (ruleParams.has(rule.name)) throw new ParseError(`Duplicate rule '${rule.name}'`, rule.line);
    ruleParams.set(rule.name, rule.params);
  }
  for (const table of v2.encounters) {
    if (ruleParams.has("encounter")) throw new ParseError("Duplicate rule 'encounter'", table.line);
    ruleParams.set("encounter", [encounterParamName(table.expr)]);
  }
  return ruleParams;
}

function collectV2Rules(v2: V2ProgramAst): V2LoweringContext["rules"] {
  return new Map(v2.rules.map((rule) => [rule.name, rule]));
}

interface V2InvocationSite {
  statement: V2InvokeStatementAst;
  currentRule?: string;
}

function selectV2RuleSpecializations(v2: V2ProgramAst, rules: Map<string, V2RuleAst>): Set<string> {
  const invocations = collectV2Invocations(v2);
  const selected = new Set<string>();
  for (const rule of v2.rules) {
    if (rule.params.length === 0 || !isSpecializableRuleBody(rule)) continue;
    const sites = invocations.filter((site) => site.statement.name === rule.name);
    if (sites.length < 2) continue;
    if (sites.some((site) => site.currentRule === rule.name)) continue;
    if (!sites.every((site) => site.statement.args.length === rule.params.length && site.statement.args.every(isSpecializationArg))) {
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
      if (statement.kind === "v2_match") {
        for (const matchCase of statement.cases) visit([matchCase.action], currentRule);
        if (statement.otherwise) visit([statement.otherwise], currentRule);
      }
      if (statement.kind === "v2_challenge") {
        visit(statement.successBody, currentRule);
        if (statement.failureBody) visit(statement.failureBody, currentRule);
      }
    }
  };
  if (v2.turn !== undefined) visit(v2.turn.body);
  for (const rule of v2.rules) visit(rule.body, rule.name);
  for (const table of v2.encounters) {
    for (const encounterCase of table.cases) visit(encounterCase.body);
  }
  return sites;
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

function collectV2Screens(v2: V2ProgramAst): Map<string, V2ScreenAst> {
  const screens = new Map<string, V2ScreenAst>();
  for (const screen of v2.screens) {
    if (screens.has(screen.name)) throw new ParseError(`Duplicate screen '${screen.name}'`, screen.line);
    screens.set(screen.name, screen);
  }
  return screens;
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
    if ((field.type === "coord" || field.type === "cells") && field.domain !== undefined && !domains.has(field.domain)) {
      throw new ParseError(`Unknown domain '${field.domain}'`, field.line);
    }
  }
}

function validateV2References(
  v2: V2ProgramAst,
  context: {
    ruleParams: Map<string, string[]>;
    screens: Map<string, V2ScreenAst>;
  },
): void {
  const visit = (statements: V2StatementAst[]): void => {
    for (const statement of statements) {
      validateV2Statement(statement, context, visit);
    }
  };
  if (v2.turn) visit(v2.turn.body);
  for (const rule of v2.rules) visit(rule.body);
  for (const table of v2.encounters) {
    for (const encounterCase of table.cases) visit(encounterCase.body);
  }
}

function validateV2Statement(
  statement: V2StatementAst,
  context: {
    ruleParams: Map<string, string[]>;
    screens: Map<string, V2ScreenAst>;
  },
  visit: (statements: V2StatementAst[]) => void,
): void {
  switch (statement.kind) {
    case "v2_show":
      if (!isNumericLiteralText(statement.target) && !context.screens.has(statement.target)) {
        throw new ParseError(`Unknown screen '${statement.target}'`, statement.line);
      }
      return;
    case "v2_invoke":
      if (!context.ruleParams.has(statement.name)) {
        throw new ParseError(`Unknown rule '${statement.name}'`, statement.line);
      }
      {
        const expected = context.ruleParams.get(statement.name)!.length;
        if (statement.args.length !== expected) {
          throw new ParseError(
            `Rule '${statement.name}' expects ${expected} argument${expected === 1 ? "" : "s"}, got ${statement.args.length}`,
            statement.line,
          );
        }
      }
      return;
    case "v2_challenge":
      if (!context.screens.has(statement.warningScreen)) {
        throw new ParseError(`Unknown challenge warning screen '${statement.warningScreen}'`, statement.line);
      }
      if (!context.screens.has(statement.memoryScreen)) {
        throw new ParseError(`Unknown challenge memory screen '${statement.memoryScreen}'`, statement.line);
      }
      visit(statement.successBody);
      if (statement.failureBody) visit(statement.failureBody);
      return;
    case "v2_if":
      visit(statement.thenBody);
      if (statement.elseBody) visit(statement.elseBody);
      return;
    case "v2_match":
      for (const matchCase of statement.cases) visit([matchCase.action]);
      if (statement.otherwise) visit([statement.otherwise]);
      return;
    default:
      return;
  }
}

function collectV2MoveDeltas(v2: V2ProgramAst): V2LoweringContext["moveDeltas"] {
  const deltas = new Map<string, Partial<Record<NonNullable<V2MoveStatementAst["direction"]>, string>>>();
  for (const world of v2.worlds) {
    if (world.position === undefined) continue;
    deltas.set(world.position.name, moveDeltasForEncoding(world.position.encoding));
  }
  return deltas;
}

function collectV2CellMapNames(v2: V2ProgramAst, stateDomains: Map<string, string>): Map<string, string> {
  const explicit = v2.state.find((field) => field.type === "packed" && /^(?:plan|map)$/iu.test(field.name));
  const byDomain = new Map<string, string>();
  const addDomain = (domain: string): void => {
    if (byDomain.has(domain)) return;
    const domainSpecific = v2.state.find((field) =>
      field.type === "packed" && new RegExp(`^${escapeRegExp(domain)}_(?:plan|map)$`, "iu").test(field.name),
    );
    byDomain.set(domain, domainSpecific?.name ?? explicit?.name ?? `__cell_map_${domain}`);
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
        case "v2_challenge":
          texts.push(statement.expr);
          visit(statement.successBody);
          if (statement.failureBody !== undefined) visit(statement.failureBody);
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
        case "v2_move":
        case "v2_show":
        case "v2_read":
        case "v2_raw":
          break;
      }
    }
  };
  if (v2.turn !== undefined) visit(v2.turn.body);
  for (const rule of v2.rules) visit(rule.body);
  for (const table of v2.encounters) {
    texts.push(table.expr);
    for (const encounterCase of table.cases) {
      texts.push(encounterCase.value);
      visit(encounterCase.body);
    }
  }
  return texts;
}

function moveDeltasForEncoding(encoding: string | undefined): Partial<Record<NonNullable<V2MoveStatementAst["direction"]>, string>> {
  if (encoding === "packed_decimal_zero_run") {
    return {
      south: "0.0000002",
      north: "-0.0000002",
      west: "0.000001",
      east: "-0.000001",
      up: "1",
      down: "-1",
    };
  }
  return {
    south: "1",
    north: "-1",
    east: "10",
    west: "-10",
    up: "100",
    down: "-100",
  };
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
    if (world.position !== undefined) {
      const lines: RawBlockLine[] = [];
      if (world.position.encoding !== undefined) lines.push({ text: `encoding ${world.position.encoding}`, line: world.position.line });
      domains.push({
        kind: "domain",
        domainKind: "coord",
        name: "packed",
        header: `coord packed ${world.position.name}`,
        lines,
        line: world.position.line,
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
  if (v2.encounters.length > 0) {
    domains.push({
      kind: "domain",
      domainKind: "event",
      name: "encounters",
      header: "event encounters",
    lines: v2.encounters.flatMap((table) => table.cases.map((encounterCase) => ({
      text: encounterCase.value,
      line: encounterCase.line,
    }))),
      line: v2.encounters[0]!.line,
    });
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
    const lowered: StateFieldAst = {
      name: field.name,
      type: lowerV2StateFieldType(field.type),
      line: field.line,
    };
    if (field.min !== undefined) lowered.min = field.min;
    if (field.max !== undefined) lowered.max = field.max;
    if (field.initial !== undefined) {
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
  if (v2.turn !== undefined) visit(v2.turn.body);
  for (const table of v2.encounters) {
    add(encounterParamName(table.expr), table.line);
    for (const encounterCase of table.cases) visit(encounterCase.body);
  }
  return fields;

  function visit(statements: V2StatementAst[]): void {
    for (const statement of statements) {
      if (statement.kind === "v2_if") {
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
      }
      if (statement.kind === "v2_match") {
        for (const matchCase of statement.cases) visit([matchCase.action]);
        if (statement.otherwise) visit([statement.otherwise]);
      }
      if (statement.kind === "v2_challenge") {
        add(statement.challengeTarget, statement.line);
        add(statement.answerInput, statement.line);
        visit(statement.successBody);
        if (statement.failureBody) visit(statement.failureBody);
      }
      if (statement.kind === "v2_read") add(statement.target, statement.line);
    }
  }
}

function lowerV2InitialExpression(field: V2StateFieldAst, context: V2LoweringContext): ExpressionAst {
  const initial = field.initial ?? "0";
  if (field.type === "cells" && initial.trim() === "random()") {
    return lowerV2Expression("int(random() * 999)", field.line);
  }
  return lowerV2Expression(initial, field.line, context);
}

function lowerV2StateFieldType(type: V2StateFieldAst["type"]): StateFieldType {
  if (type === "counter") return "range";
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

function lowerV2Entry(v2: V2ProgramAst, context: V2LoweringContext): EntryAst {
  return {
    kind: "entry",
    name: "main",
    body: v2.turn
      ? [{ kind: "loop", body: lowerV2Statements(v2.turn.body, context), line: v2.turn.line }]
      : [{ kind: "halt", expr: parseExpression("0"), line: v2.line }],
    line: v2.turn?.line ?? v2.line,
  };
}

function lowerV2Rule(rule: V2RuleAst, context: V2LoweringContext): ProcAst {
  return {
    kind: "proc",
    name: rule.name,
    body: lowerV2Statements(rule.body, context),
    line: rule.line,
  };
}

function lowerV2EncounterRules(v2: V2ProgramAst, context: V2LoweringContext): ProcAst[] {
  return v2.encounters.flatMap((table) => lowerV2EncounterRule(table, context));
}

function lowerV2EncounterRule(table: V2EncounterTableAst, context: V2LoweringContext): ProcAst[] {
  const optimized = lowerSharedChallengeEncounter(table, context);
  if (optimized !== undefined) return optimized;
  return [{
    kind: "proc",
    name: "encounter",
    body: [lowerV2EncounterDispatch(table, context)],
    line: table.line,
  }];
}

function lowerV2EncounterDispatch(table: V2EncounterTableAst, context: V2LoweringContext): DispatchStatementAst {
  return {
    kind: "dispatch",
    expr: lowerV2Expression(encounterParamName(table.expr), table.line, context),
    cases: table.cases.map((encounterCase) => ({
      value: lowerV2Expression(encounterCase.value, encounterCase.line, context),
      body: lowerV2Statements(encounterCase.body, context),
      line: encounterCase.line,
    })),
    line: table.line,
    scratchId: table.line,
  };
}

function encounterParamName(expr: string): string {
  return /^[A-Za-z_][\w]*$/u.test(expr.trim()) ? expr.trim() : "encounter_kind";
}

interface SimpleChallengeCase {
  key: number;
  value: string;
  line: number;
  challenge: V2ChallengeStatementAst;
  success: SimpleEffects;
  failure: SimpleEffects;
}

interface SharedChallengeException {
  key: number;
  line: number;
  successBody: V2StatementAst[];
  failureBody: V2StatementAst[];
}

interface SimpleEffects {
  deltas: Map<string, number>;
  cellUpdates: CellUpdateEffect[];
}

interface CellUpdateEffect {
  target: string;
  op: "+=" | "-=";
  expr: string;
}

interface LinearDeltaPlan {
  target: string;
  slope: number;
  intercept: number;
  corrections: Map<number, number>;
}

function lowerSharedChallengeEncounter(table: V2EncounterTableAst, context: V2LoweringContext): ProcAst[] | undefined {
  const analyzed = table.cases.map((encounterCase) => analyzeSimpleChallengeCase(encounterCase, context));
  const simple = analyzed.filter((item): item is SimpleChallengeCase => item !== undefined);
  if (simple.length < 3) return undefined;

  const groups = groupSimpleChallengeCases(simple);
  const group = groups
    .filter((candidate) => candidate.length >= 3)
    .map((candidate) => ({
      cases: candidate,
      success: buildEffectPlan(candidate, "success"),
      failure: buildEffectPlan(candidate, "failure"),
    }))
    .filter((candidate): candidate is {
      cases: SimpleChallengeCase[];
      success: EffectPlan;
      failure: EffectPlan;
    } => candidate.success !== undefined && candidate.failure !== undefined)
    .sort((left, right) => right.cases.length - left.cases.length)[0];
  if (group === undefined) return undefined;

  const protocol = group.cases[0]!.challenge;
  const groupKeys = new Set(group.cases.map((item) => item.key));
  const exceptions = table.cases
    .filter((encounterCase) => {
      const key = numericLiteralTextValue(encounterCase.value);
      return key !== undefined && !groupKeys.has(key);
    })
    .map((encounterCase) => analyzeSharedChallengeException(encounterCase, protocol))
    .filter((item): item is SharedChallengeException => item !== undefined)
    .sort((left, right) => left.key - right.key);
  const optimizedKeys = new Set([
    ...group.cases.map((item) => item.key),
    ...exceptions.map((item) => item.key),
  ]);
  const helperName = `encounter_effects_${table.line}`;
  const defaultDispatch = defaultableOptimizedEncounterDispatch(table, optimizedKeys, helperName, context);
  const dispatch: DispatchStatementAst = {
    kind: "dispatch",
    expr: lowerV2Expression(encounterParamName(table.expr), table.line, context),
    cases: defaultDispatch?.cases ?? table.cases.map((encounterCase) => {
      const key = numericLiteralTextValue(encounterCase.value);
      if (key !== undefined && optimizedKeys.has(key)) {
        return {
          value: lowerV2Expression(encounterCase.value, encounterCase.line, context),
          body: [{ kind: "call", block: helperName, line: encounterCase.line }],
          line: encounterCase.line,
        };
      }
      return {
        value: lowerV2Expression(encounterCase.value, encounterCase.line, context),
        body: lowerV2Statements(encounterCase.body, context),
        line: encounterCase.line,
      };
    }),
    ...(defaultDispatch === undefined ? {} : { defaultBody: defaultDispatch.defaultBody }),
    line: table.line,
    scratchId: table.line,
  };
  const encounterBody = defaultDispatch === undefined
    ? [dispatch]
    : lowerDefaultDispatchAsIfChain(dispatch) ?? [dispatch];

  const helper: ProcAst = {
    kind: "proc",
    name: helperName,
    body: lowerSharedChallengeHelper(
      protocol,
      group.success,
      group.failure,
      encounterParamName(table.expr),
      context,
      exceptions,
    ),
    line: table.line,
  };

  return [
    {
      kind: "proc",
      name: "encounter",
      body: encounterBody,
      line: table.line,
    },
    helper,
  ];
}

function lowerDefaultDispatchAsIfChain(dispatch: DispatchStatementAst): StatementAst[] | undefined {
  if (dispatch.defaultBody === undefined || dispatch.cases.length > 2) return undefined;
  const emptyCases = dispatch.cases.filter((item) => item.body.length === 0);
  const nonEmptyCases = dispatch.cases.filter((item) => item.body.length > 0);
  if (emptyCases.length === 0 || nonEmptyCases.length > 1) return undefined;

  let body = dispatch.defaultBody;
  for (const dispatchCase of [...emptyCases].reverse()) {
    body = [{
      kind: "if",
      condition: {
        left: dispatch.expr,
        op: "!=",
        right: dispatchCase.value,
      },
      thenBody: body,
      line: dispatchCase.line,
    }];
  }

  const special = nonEmptyCases[0];
  if (special === undefined) return body;
  return [{
    kind: "if",
    condition: {
      left: dispatch.expr,
      op: "==",
      right: special.value,
    },
    thenBody: special.body,
    elseBody: body,
    line: dispatch.line,
  }];
}

function defaultableOptimizedEncounterDispatch(
  table: V2EncounterTableAst,
  optimizedKeys: Set<number>,
  helperName: string,
  context: V2LoweringContext,
): { cases: DispatchCaseAst[]; defaultBody: StatementAst[] } | undefined {
  const keyName = encounterParamName(table.expr);
  const range = context.stateRanges.get(keyName);
  if (range?.min === undefined || range.max === undefined) return undefined;
  if (!Number.isInteger(range.min) || !Number.isInteger(range.max)) return undefined;
  if (range.max < range.min || range.max - range.min > 20) return undefined;

  const sourceKeys = new Set<number>();
  const cases: DispatchCaseAst[] = [];
  for (const encounterCase of table.cases) {
    const key = numericLiteralTextValue(encounterCase.value);
    if (key === undefined) return undefined;
    sourceKeys.add(key);
    if (optimizedKeys.has(key)) continue;
    cases.push({
      value: lowerV2Expression(encounterCase.value, encounterCase.line, context),
      body: lowerV2Statements(encounterCase.body, context),
      line: encounterCase.line,
    });
  }

  for (let value = range.min; value <= range.max; value += 1) {
    if (sourceKeys.has(value) || optimizedKeys.has(value)) continue;
    cases.push({
      value: lowerV2Expression(String(value), table.line, context),
      body: [],
      line: table.line,
    });
  }

  if (cases.length + 1 >= optimizedKeys.size) return undefined;
  return {
    cases: cases.sort((left, right) =>
      (numericLiteralValueForExpression(left.value) ?? 0) - (numericLiteralValueForExpression(right.value) ?? 0)
    ),
    defaultBody: [{ kind: "call", block: helperName, line: table.line }],
  };
}

interface EffectPlan {
  keys: number[];
  deltas: LinearDeltaPlan[];
  cellUpdates: CellUpdateEffect[];
}

function analyzeSharedChallengeException(
  encounterCase: V2EncounterCaseAst,
  protocol: V2ChallengeStatementAst,
): SharedChallengeException | undefined {
  const key = numericLiteralTextValue(encounterCase.value);
  if (key === undefined) return undefined;
  if (encounterCase.body.length !== 1) return undefined;
  const challenge = encounterCase.body[0];
  if (challenge?.kind !== "v2_challenge" || challenge.failureBody === undefined) return undefined;
  if (challengeProtocolKey(challenge) !== challengeProtocolKey(protocol)) return undefined;
  return {
    key,
    line: encounterCase.line,
    successBody: challenge.successBody,
    failureBody: challenge.failureBody,
  };
}

function analyzeSimpleChallengeCase(
  encounterCase: V2EncounterCaseAst,
  context: V2LoweringContext,
): SimpleChallengeCase | undefined {
  const key = numericLiteralTextValue(encounterCase.value);
  if (key === undefined) return undefined;
  if (encounterCase.body.length !== 1) return undefined;
  const challenge = encounterCase.body[0];
  if (challenge?.kind !== "v2_challenge" || challenge.failureBody === undefined) return undefined;
  const success = analyzeSimpleEffects(challenge.successBody, context);
  const failure = analyzeSimpleEffects(challenge.failureBody, context);
  if (success === undefined || failure === undefined) return undefined;
  return {
    key,
    value: encounterCase.value,
    line: encounterCase.line,
    challenge,
    success,
    failure,
  };
}

function analyzeSimpleEffects(statements: V2StatementAst[], context: V2LoweringContext): SimpleEffects | undefined {
  const deltas = new Map<string, number>();
  const cellUpdates: CellUpdateEffect[] = [];
  for (const statement of statements) {
    if (statement.kind !== "v2_update") return undefined;
    if (context.stateTypes.get(statement.target) === "cells") {
      cellUpdates.push({
        target: statement.target,
        op: statement.op,
        expr: statement.expr.trim(),
      });
      continue;
    }
    const value = numericLiteralTextValue(statement.expr);
    if (value === undefined) return undefined;
    const delta = statement.op === "+=" ? value : -value;
    deltas.set(statement.target, (deltas.get(statement.target) ?? 0) + delta);
  }
  return {
    deltas,
    cellUpdates: cellUpdates.sort(compareCellUpdates),
  };
}

function groupSimpleChallengeCases(cases: SimpleChallengeCase[]): SimpleChallengeCase[][] {
  const groups = new Map<string, SimpleChallengeCase[]>();
  for (const challengeCase of cases) {
    const key = challengeProtocolKey(challengeCase.challenge);
    const group = groups.get(key);
    if (group === undefined) {
      groups.set(key, [challengeCase]);
    } else {
      group.push(challengeCase);
    }
  }
  return [...groups.values()];
}

function challengeProtocolKey(statement: V2ChallengeStatementAst): string {
  return [
    statement.expr.trim(),
    statement.challengeTarget,
    statement.warningScreen,
    statement.memoryScreen,
    statement.answerInput,
  ].join("\0");
}

function buildEffectPlan(cases: SimpleChallengeCase[], branch: "success" | "failure"): EffectPlan | undefined {
  const cellUpdates = cases[0]![branch].cellUpdates;
  if (!cases.every((item) => cellUpdatesEqual(item[branch].cellUpdates, cellUpdates))) return undefined;

  const targets = new Set<string>();
  for (const challengeCase of cases) {
    for (const target of challengeCase[branch].deltas.keys()) targets.add(target);
  }

  const plans: LinearDeltaPlan[] = [];
  for (const target of [...targets].sort()) {
    const values = new Map(cases.map((challengeCase) => [
      challengeCase.key,
      challengeCase[branch].deltas.get(target) ?? 0,
    ]));
    const plan = fitLinearDeltaPlan(target, values);
    if (plan === undefined) return undefined;
    if (plan.slope !== 0 || plan.intercept !== 0 || plan.corrections.size > 0) {
      plans.push(plan);
    }
  }

  if (plans.length === 0 && cellUpdates.length === 0) return undefined;
  return {
    keys: cases.map((item) => item.key),
    deltas: plans,
    cellUpdates,
  };
}

function lowerSharedChallengeHelper(
  protocol: V2ChallengeStatementAst,
  success: EffectPlan,
  failure: EffectPlan,
  keyName: string,
  context: V2LoweringContext,
  exceptions: SharedChallengeException[] = [],
): StatementAst[] {
  return [
    {
      kind: "assign",
      target: protocol.challengeTarget,
      expr: lowerV2Expression(protocol.expr, protocol.line, context),
      line: protocol.line,
    },
    { kind: "show", display: protocol.warningScreen, line: protocol.line },
    { kind: "show", display: protocol.memoryScreen, line: protocol.line },
    { kind: "input", target: protocol.answerInput, line: protocol.line },
    {
      kind: "if",
      condition: {
        left: { kind: "identifier", name: protocol.answerInput },
        op: "==",
        right: { kind: "identifier", name: protocol.challengeTarget },
      },
      thenBody: lowerSharedChallengeBranch(
        lowerEffectPlan(success, keyName, protocol.line, context),
        exceptions,
        "success",
        keyName,
        protocol.line,
        context,
      ),
      elseBody: lowerSharedChallengeBranch(
        lowerEffectPlan(failure, keyName, protocol.line, context),
        exceptions,
        "failure",
        keyName,
        protocol.line,
        context,
      ),
      line: protocol.line,
    },
  ];
}

function lowerSharedChallengeBranch(
  fallback: StatementAst[],
  exceptions: SharedChallengeException[],
  branch: "success" | "failure",
  keyName: string,
  line: number,
  context: V2LoweringContext,
): StatementAst[] {
  let body = fallback;
  for (const exception of [...exceptions].reverse()) {
    body = [{
      kind: "if",
      condition: {
        left: { kind: "identifier", name: keyName },
        op: "==",
        right: { kind: "number", raw: String(exception.key) },
      },
      thenBody: lowerV2Statements(branch === "success" ? exception.successBody : exception.failureBody, context),
      elseBody: body,
      line: exception.line || line,
    }];
  }
  return body;
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
  for (const update of plan.cellUpdates) {
    statements.push(...lowerV2Statement({
      kind: "v2_update",
      target: update.target,
      op: update.op,
      expr: update.expr,
      line,
    }, context));
  }
  return statements;
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

function numericLiteralTextValue(text: string): number | undefined {
  if (!isNumericLiteralText(text.trim())) return undefined;
  const value = Number(text.trim());
  return Number.isInteger(value) ? value : undefined;
}

function numericLiteralValueForExpression(expr: ExpressionAst): number | undefined {
  if (expr.kind !== "number") return undefined;
  const value = Number(expr.raw);
  return Number.isInteger(value) ? value : undefined;
}

function cellUpdatesEqual(left: CellUpdateEffect[], right: CellUpdateEffect[]): boolean {
  return left.length === right.length &&
    left.every((item, index) => compareCellUpdates(item, right[index]!) === 0);
}

function compareCellUpdates(left: CellUpdateEffect, right: CellUpdateEffect): number {
  return left.target.localeCompare(right.target) ||
    left.op.localeCompare(right.op) ||
    left.expr.localeCompare(right.expr);
}

function lowerV2Statements(statements: V2StatementAst[], context: V2LoweringContext): StatementAst[] {
  const lowered: StatementAst[] = [];
  for (let index = 0; index < statements.length; index += 1) {
    const statement = statements[index]!;
    lowered.push(...lowerV2Statement(statement, context));
  }
  return lowered;
}

function lowerV2Statement(statement: V2StatementAst, context: V2LoweringContext): StatementAst[] {
  switch (statement.kind) {
    case "v2_show":
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
    case "v2_stop":
      return [{ kind: "halt", expr: lowerV2Expression(statement.value, statement.line, context), line: statement.line }];
    case "v2_invoke":
      return lowerV2Invoke(statement, context);
    case "v2_if": {
      const condition = lowerV2Predicate(statement.predicate, statement.line, context);
      const lowered: IfStatementAst = {
        kind: "if",
        condition,
        thenBody: lowerV2Statements(statement.thenBody, context),
        line: statement.line,
      };
      if (statement.elseBody !== undefined) lowered.elseBody = lowerV2Statements(statement.elseBody, context);
      return [lowered];
    }
    case "v2_challenge":
      return lowerV2Challenge(statement, context);
    case "v2_move":
      return lowerV2Move(statement, context);
    case "v2_assign":
      return [{ kind: "assign", target: statement.target, expr: lowerV2Expression(statement.expr, statement.line, context), line: statement.line }];
    case "v2_update": {
      if (context.stateTypes.get(statement.target) === "cells") {
        return [{
          kind: "assign",
          target: statement.target,
          expr: lowerV2Expression(cellSetUpdateExpression(statement.target, statement.expr, statement.op, context), statement.line, context),
          line: statement.line,
        }];
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
  }
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
  const mask = cellMaskExpressionForCollection(collection, item, context);
  return mask === undefined ? `bit_has(${collection}, ${item})` : `bit_and(${collection}, ${mask})`;
}

function lowerV2Challenge(
  statement: Extract<V2StatementAst, { kind: "v2_challenge" }>,
  context: V2LoweringContext,
): StatementAst[] {
  const failureBody = statement.failureBody ?? [{ kind: "v2_show", target: "0", line: statement.line } satisfies V2StatementAst];
  return [
    {
      kind: "assign",
      target: statement.challengeTarget,
      expr: lowerV2Expression(statement.expr, statement.line, context),
      line: statement.line,
    },
    { kind: "show", display: statement.warningScreen, line: statement.line },
    { kind: "show", display: statement.memoryScreen, line: statement.line },
    {
      kind: "input",
      target: statement.answerInput,
      line: statement.line,
    },
    {
      kind: "if",
      condition: {
        left: { kind: "identifier", name: statement.answerInput },
        op: "==",
        right: { kind: "identifier", name: statement.challengeTarget },
      },
      thenBody: lowerV2Statements(statement.successBody, context),
      elseBody: lowerV2Statements(failureBody, context),
      line: statement.line,
    },
  ];
}

function lowerV2Move(statement: V2MoveStatementAst, context: V2LoweringContext): StatementAst[] {
  const delta = namedMoveDelta(statement.target, statement.direction, statement.line, context);
  const move: AssignStatementAst = {
    kind: "assign",
    target: statement.target,
    expr: {
      kind: "binary",
      op: "+",
      left: { kind: "identifier", name: statement.target },
      right: lowerV2Expression(delta, statement.line, context),
    },
    line: statement.line,
  };
  return [move];
}

function namedMoveDelta(
  target: string,
  direction: V2MoveStatementAst["direction"],
  line: number,
  context: V2LoweringContext,
): string {
  if (direction === undefined) throw new ParseError("Move must specify a direction", line);
  return context.moveDeltas.get(target)?.[direction] ?? moveDeltasForEncoding(undefined)[direction] ?? "0";
}

function parseV2MoveDirection(text: string, line: number): NonNullable<V2MoveStatementAst["direction"]> {
  switch (text) {
    case "north":
    case "south":
    case "east":
    case "west":
    case "up":
    case "down":
      return text;
    default:
      throw new ParseError("Move direction must be north, south, east, west, up, or down", line);
  }
}

function lowerV2MatchStatements(statement: V2MatchStatementAst, context: V2LoweringContext): StatementAst[] {
  const cyclic = lowerCyclicCounterMatch(statement, context);
  if (cyclic !== undefined) return cyclic;
  const effects = lowerSimpleEffectMatch(statement, context);
  if (effects !== undefined) return effects;
  const guardedDirection = lowerGuardedDirectionMatch(statement, context);
  if (guardedDirection !== undefined) return guardedDirection;
  const smallCase = lowerSmallCaseMatch(statement, context);
  if (smallCase !== undefined) return [smallCase];
  const singleCase = lowerSingleCaseMatch(statement, context);
  if (singleCase !== undefined) return [singleCase];
  return [lowerV2Match(statement, context)];
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
      thenBody: lowerV2MatchAction(row.action, context, statement.expr, row.value, row.line),
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
    thenBody: lowerV2MatchAction(matchCase.action, context, statement.expr, value, matchCase.line),
    ...(statement.otherwise === undefined ? {} : { elseBody: lowerV2Statement(statement.otherwise, context) }),
    line: statement.line,
  };
}

function lowerV2Match(statement: V2MatchStatementAst, context: V2LoweringContext): DispatchStatementAst {
  const compact = lowerCompactDirectionDispatch(statement, context);
  if (compact !== undefined) return compact;

  const cases: DispatchCaseAst[] = [];
  for (const matchCase of statement.cases) {
    for (const value of matchCase.values) {
      cases.push({
        value: lowerV2Expression(value, matchCase.line, context),
        body: lowerV2MatchAction(matchCase.action, context, statement.expr, value, matchCase.line),
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
    : analyzeSimpleNumericEffects(resolveV2ActionBody(statement.otherwise, context, statement.expr), context);
  if (defaultEffects === undefined) return undefined;

  const rows: SimpleEffectMatchRow[] = [];
  const seen = new Set<number>();
  for (const matchCase of statement.cases) {
    for (const valueText of matchCase.values) {
      const value = numericLiteralTextValue(valueText);
      if (value === undefined || seen.has(value)) return undefined;
      seen.add(value);
      const body = resolveV2ActionBody(matchCase.action, context, statement.expr, valueText);
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
    if (statement.kind === "switch") {
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
      const body = resolveV2ActionBody(matchCase.action, context, statement.expr, valueText);
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
    ...lowerEffectPlan({
      keys: sorted.map((row) => row.next),
      deltas: plans,
      cellUpdates: [],
    }, variable, statement.line, context),
  ];
}

function resolveV2ActionBody(
  action: V2StatementAst,
  context: V2LoweringContext,
  matchExpr: string,
  matchValue?: string,
): V2StatementAst[] | undefined {
  if (action.kind !== "v2_invoke") return [action];
  const rule = context.rules.get(action.name);
  if (rule === undefined) return undefined;
  if (rule.params.length === 0) return rule.body;
  if (rule.params.length !== action.args.length) return undefined;
  const args: string[] = [];
  for (const arg of action.args) {
    const resolved = resolveV2InvokeArg(arg, matchExpr, matchValue);
    if (resolved === undefined) return undefined;
    args.push(resolved);
  }
  return substituteV2Statements(rule.body, invokeReplacements(rule, args));
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

function lowerCompactDirectionDispatch(
  statement: V2MatchStatementAst,
  context: V2LoweringContext,
): DispatchStatementAst | undefined {
  const directionalCases = statement.cases.filter((matchCase) => isDirectionInvoke(matchCase.action, statement.expr));
  const directionalValueCount = directionalCases.reduce((sum, matchCase) => sum + matchCase.values.length, 0);
  if (directionalValueCount < 3) return undefined;
  const action = directionalCases[0]!.action;
  if (action.kind !== "v2_invoke") return undefined;
  if (!directionalCases.every((matchCase) => sameInvoke(matchCase.action, action))) return undefined;
  const params = context.ruleParams.get(action.name) ?? [];
  if (params.length === 0) return undefined;
  const needsGuard = statement.otherwise === undefined || !isDirectionInvoke(statement.otherwise, statement.expr);
  if (needsGuard && directionalValueCount < 5) return undefined;

  const cases: DispatchCaseAst[] = [];
  for (const matchCase of statement.cases) {
    if (directionalCases.includes(matchCase)) continue;
    for (const value of matchCase.values) {
      cases.push({
        value: lowerV2Expression(value, matchCase.line, context),
        body: lowerV2MatchAction(matchCase.action, context, statement.expr, value, matchCase.line),
        line: matchCase.line,
      });
    }
  }

  const directionBody: StatementAst[] = [
    {
      kind: "assign",
      target: params[0]!,
      expr: lowerV2Expression(`direction(${statement.expr})`, statement.line, context),
      line: statement.line,
    },
    { kind: "call", block: action.name, line: action.line },
  ];
  const defaultBody: StatementAst[] = needsGuard
    ? [{
        kind: "if",
        condition: {
          left: lowerV2Expression(directionKeyGuardExpression(statement.expr), statement.line, context),
          op: "==",
          right: lowerV2Expression("0", statement.line, context),
        },
        thenBody: directionBody,
        ...(statement.otherwise === undefined ? {} : { elseBody: lowerV2Statement(statement.otherwise, context) }),
        line: statement.line,
      }]
    : directionBody;

  return {
    kind: "dispatch",
    expr: lowerV2Expression(statement.expr, statement.line, context),
    name: "direction_dispatch",
    cases,
    defaultBody,
    line: statement.line,
    scratchId: statement.line,
  };
}

interface GuardedDirectionCase {
  matchCase: V2MatchCaseAst;
  predicate: V2PredicateAst;
  terminalBody: V2StatementAst[];
  invertPredicate: boolean;
}

function lowerGuardedDirectionMatch(
  statement: V2MatchStatementAst,
  context: V2LoweringContext,
): StatementAst[] | undefined {
  const directionalCases = statement.cases.filter((matchCase) => isDirectionInvoke(matchCase.action, statement.expr));
  const directionalValueCount = directionalCases.reduce((sum, matchCase) => sum + matchCase.values.length, 0);
  if (directionalValueCount < 3) return undefined;
  const action = directionalCases[0]?.action;
  if (action?.kind !== "v2_invoke") return undefined;
  if (!directionalCases.every((matchCase) => sameInvoke(matchCase.action, action))) return undefined;

  const guardedCases: GuardedDirectionCase[] = [];
  for (const matchCase of statement.cases) {
    if (directionalCases.includes(matchCase)) continue;
    const guarded = analyzeGuardedDirectionCase(matchCase, action, statement.expr, context);
    if (guarded !== undefined) guardedCases.push(guarded);
  }
  if (guardedCases.length === 0) return undefined;

  const rewritten: V2MatchStatementAst = {
    ...statement,
    cases: statement.cases.map((matchCase) =>
      guardedCases.some((guarded) => guarded.matchCase === matchCase)
        ? { ...matchCase, action }
        : matchCase
    ),
  };
  const dispatch = lowerCompactDirectionDispatch(rewritten, context);
  if (dispatch === undefined) return undefined;

  return [
    ...guardedCases.flatMap((guarded) => lowerGuardedDirectionCasePrelude(statement, guarded, context)),
    dispatch,
  ];
}

function analyzeGuardedDirectionCase(
  matchCase: V2MatchCaseAst,
  sharedAction: V2InvokeStatementAst,
  matchExpr: string,
  context: V2LoweringContext,
): GuardedDirectionCase | undefined {
  const body = resolveV2ActionBody(matchCase.action, context, matchExpr);
  if (body?.length !== 1) return undefined;
  const guarded = body[0];
  if (guarded?.kind !== "v2_if" || guarded.elseBody === undefined) return undefined;

  const thenShared = isSharedDirectionBranch(guarded.thenBody, sharedAction);
  const elseShared = isSharedDirectionBranch(guarded.elseBody, sharedAction);
  if (thenShared === elseShared) return undefined;

  if (elseShared && v2StatementsTerminate(guarded.thenBody, context)) {
    return {
      matchCase,
      predicate: guarded.predicate,
      terminalBody: guarded.thenBody,
      invertPredicate: false,
    };
  }
  if (thenShared && v2StatementsTerminate(guarded.elseBody, context)) {
    return {
      matchCase,
      predicate: guarded.predicate,
      terminalBody: guarded.elseBody,
      invertPredicate: true,
    };
  }
  return undefined;
}

function isSharedDirectionBranch(statements: V2StatementAst[], sharedAction: V2InvokeStatementAst): boolean {
  return statements.length === 1 && sameInvoke(statements[0]!, sharedAction);
}

function lowerGuardedDirectionCasePrelude(
  statement: V2MatchStatementAst,
  guarded: GuardedDirectionCase,
  context: V2LoweringContext,
): StatementAst[] {
  return guarded.matchCase.values.map((value) => {
    const condition = lowerV2Predicate(guarded.predicate, guarded.matchCase.line, context);
    const innerCondition = guarded.invertPredicate ? invertLoweredCondition(condition) : condition;
    return {
      kind: "if" as const,
      condition: {
        left: lowerV2Expression(statement.expr, statement.line, context),
        op: "==",
        right: lowerV2Expression(value, guarded.matchCase.line, context),
      },
      thenBody: [{
        kind: "if" as const,
        condition: innerCondition,
        thenBody: lowerV2Statements(guarded.terminalBody, context),
        line: guarded.matchCase.line,
      }],
      line: guarded.matchCase.line,
    };
  });
}

function v2StatementsTerminate(
  statements: V2StatementAst[],
  context: V2LoweringContext,
  seenRules = new Set<string>(),
): boolean {
  for (const statement of statements) {
    if (v2StatementTerminates(statement, context, seenRules)) return true;
  }
  return false;
}

function v2StatementTerminates(
  statement: V2StatementAst,
  context: V2LoweringContext,
  seenRules: Set<string>,
): boolean {
  switch (statement.kind) {
    case "v2_stop":
      return true;
    case "v2_invoke": {
      if (seenRules.has(statement.name)) return false;
      const rule = context.rules.get(statement.name);
      if (rule === undefined) return false;
      seenRules.add(statement.name);
      const terminates = v2StatementsTerminate(rule.body, context, seenRules);
      seenRules.delete(statement.name);
      return terminates;
    }
    case "v2_if":
      return statement.elseBody !== undefined &&
        v2StatementsTerminate(statement.thenBody, context, new Set(seenRules)) &&
        v2StatementsTerminate(statement.elseBody, context, new Set(seenRules));
    case "v2_match":
      return statement.otherwise !== undefined &&
        statement.cases.every((matchCase) => v2StatementTerminates(matchCase.action, context, new Set(seenRules))) &&
        v2StatementTerminates(statement.otherwise, context, new Set(seenRules));
    case "v2_challenge":
      return statement.failureBody !== undefined &&
        v2StatementsTerminate(statement.successBody, context, new Set(seenRules)) &&
        v2StatementsTerminate(statement.failureBody, context, new Set(seenRules));
    default:
      return false;
  }
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

function directionKeyGuardExpression(expr: string): string {
  const key = `(${expr})`;
  const cardinal = `abs(abs(${key} - 5) - 2) - 1`;
  const vertical = `abs(${key}) - 5`;
  return `(${cardinal}) * (${vertical})`;
}

function isDirectionInvoke(action: V2StatementAst, matchExpr: string): boolean {
  if (action.kind !== "v2_invoke") return false;
  return action.args.some((arg) => {
    const direction = /^direction\((.+)\)$/u.exec(arg.trim());
    return direction?.[1]?.trim() === matchExpr.trim();
  });
}

function sameInvoke(action: V2StatementAst, expected: V2InvokeStatementAst): boolean {
  return action.kind === "v2_invoke" && action.name === expected.name && action.args.join(",") === expected.args.join(",");
}

function lowerV2MatchAction(
  action: V2StatementAst,
  context: V2LoweringContext,
  matchExpr: string,
  matchValue: string,
  line: number,
): StatementAst[] {
  if (action.kind !== "v2_invoke") return lowerV2Statement(action, context);
  const rule = context.rules.get(action.name);
  if (rule !== undefined && context.specializedRules.has(rule.name)) {
    const args = action.args.map((arg) => resolveV2InvokeArg(arg, matchExpr, matchValue)!);
    return lowerV2Statements(substituteV2Statements(rule.body, invokeReplacements(rule, args)), context);
  }
  const params = context.ruleParams.get(action.name) ?? [];
  const statements: StatementAst[] = [];
  for (let index = 0; index < Math.min(params.length, action.args.length); index += 1) {
    const arg = resolveV2InvokeArg(action.args[index]!, matchExpr, matchValue)!;
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
      return { ...statement, expr: substituteV2Text(statement.expr, replacements) };
    case "v2_update":
      return { ...statement, expr: substituteV2Text(statement.expr, replacements) };
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
    case "v2_challenge": {
      const substituted: V2ChallengeStatementAst = {
        ...statement,
        expr: substituteV2Text(statement.expr, replacements),
        successBody: substituteV2Statements(statement.successBody, replacements),
      };
      if (statement.failureBody !== undefined) {
        substituted.failureBody = substituteV2Statements(statement.failureBody, replacements);
      }
      return substituted;
    }
    case "v2_move":
      return statement;
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
    default:
      return statement;
  }
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
  return isNumericLiteralText(trimmed) || /^[A-Za-z_][\w]*$/u.test(trimmed);
}

function escapeRegExp(text: string): string {
  return text.replace(/[.*+?^${}()|[\]\\]/gu, "\\$&");
}

function resolveV2InvokeArg(arg: string, matchExpr: string, matchValue?: string): string | undefined {
  const direction = /^direction\((.+)\)$/u.exec(arg.trim());
  if (direction && direction[1]!.trim() === matchExpr.trim()) {
    if (matchValue === undefined) return undefined;
    const delta = directionDelta(matchValue);
    if (delta !== undefined) return String(delta);
  }
  return arg;
}

function lowerV2Expression(text: string, line: number, context?: V2LoweringContext): ExpressionAst {
  const rewritten = rewriteSpatialExpressionText(text, context);
  const contextual = context === undefined ? rewritten : normalizeContextualFloorAccessText(rewritten, context);
  const normalized = normalizeV2ExpressionText(contextual);
  return parseExpression(normalized, line);
}

function rewriteSpatialExpressionText(text: string, context: V2LoweringContext | undefined): string {
  if (context === undefined) return text;
  let rewritten = replaceSpatialCall(text, "random_cell", (args) => {
    if (args.length !== 1) return undefined;
    return randomCellExpression(args[0]!, context);
  });
  rewritten = replaceSpatialCall(rewritten, "cell_at", (args) => cellAtExpression(args, context));
  return rewritten;
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
    case "cockpit_perspective":
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

function lineCells(
  board: V2BoardAst,
  kind: "row" | "column" | "diag-left" | "diag-right",
  cell: string,
): string[] {
  const x = decimalOnesExpression(cell);
  const y = decimalTensExpression(cell);
  switch (kind) {
    case "row":
      return range(board.xMin, board.xMax).map((candidateX) => boardCellExpression(String(candidateX), y));
    case "column":
      return range(board.yMin, board.yMax).map((candidateY) => boardCellExpression(x, String(candidateY)));
    case "diag-left":
      return range(-Math.max(board.width, board.height) + 1, Math.max(board.width, board.height) - 1)
        .map((delta) => offsetExpression(cell, delta * 11));
    case "diag-right":
      return range(-Math.max(board.width, board.height) + 1, Math.max(board.width, board.height) - 1)
        .map((delta) => offsetExpression(cell, delta * 9));
  }
}

function bitHitExpression(mask: string, index: string): string {
  return `sign(bit_has(${mask}, ${index}))`;
}

function boardCellExpression(x: string, y: string): string {
  return `${x} + 10 * (${y})`;
}

function decimalTensExpression(expr: string): string {
  return `int((${expr}) / 10)`;
}

function decimalOnesExpression(expr: string): string {
  return `(${expr}) - 10 * int((${expr}) / 10)`;
}

function offsetExpression(expr: string, offset: number): string {
  if (offset === 0) return expr;
  if (offset > 0) return `(${expr}) + ${offset}`;
  return `(${expr}) - ${Math.abs(offset)}`;
}

function sumExpressionText(parts: string[]): string {
  if (parts.length === 0) return "0";
  return parts.map((part) => `(${part})`).join(" + ");
}

function maxExpressionText(parts: string[]): string {
  const nonEmpty = parts.filter((part) => part !== "0");
  if (nonEmpty.length === 0) return "0";
  return nonEmpty.reduce((left, right) => `max(${left}, ${right})`);
}

function range(start: number, end: number): number[] {
  const values: number[] = [];
  for (let value = start; value <= end; value += 1) values.push(value);
  return values;
}

function randomCellExpression(domain: string, context: V2LoweringContext): string {
  const board = context.boards.get(domain);
  if (board !== undefined) return randomBoardCellExpression(board);
  const world = context.worlds.get(domain);
  if (world !== undefined) return randomWorldCellExpression(world);
  return "int(random() * 9) + 1";
}

function randomBoardCellExpression(board: V2BoardAst): string {
  if (board.height === 1) return addOffsetExpression(`int(random() * ${board.width})`, board.xMin);
  if (board.width === 1) return addOffsetExpression(`int(random() * ${board.height})`, board.yMin);
  const x = addOffsetExpression(`int(random() * ${board.width})`, board.xMin);
  const y = addOffsetExpression(`int(random() * ${board.height})`, board.yMin);
  return `${x} + 10 * (${y})`;
}

function randomWorldCellExpression(world: V2WorldAst): string {
  switch (world.position?.encoding) {
    case "pier_to_ship":
      return "int(random() * 8) + 1";
    case "cockpit_perspective":
    case "corridor_plan":
    case "decimal_player":
    case "floor_plan":
    case "packed_decimal_zero_run":
    case "row_scan":
    case undefined:
      return "int(random() * 9) + 1";
  }
  return "int(random() * 9) + 1";
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

function directionDelta(text: string): number | undefined {
  const value = Number(text.trim());
  if (!Number.isFinite(value)) return undefined;
  const mapping = new Map<number, number>([
    [2, 1],
    [8, -1],
    [4, -10],
    [6, 10],
    [5, 100],
    [-5, -100],
  ]);
  return mapping.get(value);
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
    if (pendingComma) {
      items.push({ kind: "literal", text: " ", synthetic: "comma-space", line });
      pendingComma = false;
    }
    items.push(parseDisplayItem(token.text, line));
    justReadItem = true;
  }

  if (pendingComma) throw new ParseError("Display comma must separate two fragments", line);
  return items;
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
  throw new ParseError(`Display item must be a string literal or state name, got '${trimmed}'`, line);
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
    while (index < text.length && !/[\s,]/u.test(text[index]!)) index += 1;
    tokens.push({ kind: "item", text: text.slice(start, index) });
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
  const regex = /\s*([A-Za-z_А-Яа-я][\wА-Яа-я]*|\d+(?:\.\d+)?(?:e[+-]?\d+)?|==|!=|<=|>=|[()+\-*/,])\s*/giy;
  let index = 0;
  while (index < source.length) {
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
