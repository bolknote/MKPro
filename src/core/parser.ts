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
  V2EncounterTableAst,
  V2FleetAst,
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
    const fleets: V2FleetAst[] = [];
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
          fleets,
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
      if (line.text.startsWith("board ")) {
        boards.push(this.parseV2Board(line.text));
        continue;
      }
      if (line.text.startsWith("fleet ")) {
        fleets.push(this.parseV2Fleet(line.text));
        continue;
      }
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

  private parseV2Board(text: string): V2BoardAst {
    const header = this.next();
    const match = /^board\s+([A-Za-z_][\w]*)\s*:\s*(\d+)x(\d+)\s*\{$/u.exec(text);
    if (!match) throw new ParseError("Board must look like 'board name: 10x10 {'", header.line);
    const board: V2BoardAst = {
      kind: "v2_board",
      name: match[1]!,
      width: Number(match[2]),
      height: Number(match[3]),
      line: header.line,
    };
    while (!this.done()) {
      const line = this.next();
      if (line.text === "}") return board;
      throw new ParseError("Board block does not support body lines", line.line);
    }
    throw new ParseError("Unclosed board block", header.line);
  }

  private parseV2Fleet(text: string): V2FleetAst {
    const header = this.next();
    const match = /^fleet\s+([A-Za-z_][\w]*)\s+on\s+([A-Za-z_][\w]*)\s*\{$/u.exec(text);
    if (!match) throw new ParseError("Fleet must look like 'fleet name on board {'", header.line);
    let ships: V2FleetAst["ships"] | undefined;
    const fleet: Omit<V2FleetAst, "ships"> = {
      kind: "v2_fleet",
      name: match[1]!,
      board: match[2]!,
      line: header.line,
    };
    while (!this.done()) {
      const line = this.next();
      if (line.text === "}") {
        if (ships === undefined) throw new ParseError("Fleet block must contain ships line", header.line);
        return { ...fleet, ships };
      }
      const shipsMatch = /^ships\s+([A-Za-z_][\w]*)(?:\s+(-?\d+)\.\.(-?\d+))?\s*=\s*(.+)$/u.exec(line.text);
      if (shipsMatch) {
        ships = { name: shipsMatch[1]!, initial: shipsMatch[4]!.trim() };
        if (shipsMatch[2] !== undefined) ships.min = Number(shipsMatch[2]);
        if (shipsMatch[3] !== undefined) ships.max = Number(shipsMatch[3]);
        continue;
      }
      throw new ParseError(`Unexpected fleet line '${line.text}'`, line.line);
    }
    throw new ParseError("Unclosed fleet block", header.line);
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
  const match = /^([A-Za-z_][\w]*)\s*:\s*([A-Za-z_][\w]*)(.*)$/u.exec(line.text);
  if (!match) {
    throw new ParseError("State field must look like 'name: counter 0..9 = 0'", line.line);
  }
  const typeText = match[2]!.toLowerCase();
  if (!["flag", "counter", "coord", "bitset", "packed"].includes(typeText)) {
    throw new ParseError(`Unknown state type '${match[2]}'`, line.line);
  }
  const tail = match[3]!.trim();
  const tailMatch = /^(?:(-?\d+)\.\.(-?\d+))?(?:\s*=\s*(.+))?$/u.exec(tail);
  if (!tailMatch) {
    throw new ParseError("State field must look like 'name: counter 0..9 = 0'", line.line);
  }
  const field: V2StateFieldAst = {
    kind: "v2_state_field",
    name: match[1]!,
    type: typeText as V2StateFieldAst["type"],
    line: line.line,
  };
  if (tailMatch[1] !== undefined) {
    field.min = Number(tailMatch[1]);
    field.max = Number(tailMatch[2]);
  }
  if (tailMatch[3] !== undefined) field.initial = tailMatch[3].trim();
  return field;
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
  validateV2References(v2, { screens, ruleParams });
  const context: V2LoweringContext = {
    ruleParams,
    rules,
    specializedRules,
    moveDeltas: collectV2MoveDeltas(v2),
  };
  return {
    domains: lowerV2Domains(v2),
    states: lowerV2State(v2, specializedRules),
    displays: v2.screens.map(lowerV2Screen),
    entries: [lowerV2Entry(v2, context)],
    procs: [
      ...v2.rules.filter((rule) => !specializedRules.has(rule.name)).map((rule) => lowerV2Rule(rule, context)),
      ...lowerV2EncounterRules(v2, context),
    ],
    blocks: [],
  };
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
    if (field.type === "bitset") {
      domains.push({
        kind: "domain",
        domainKind: "bitset",
        name: field.name,
        header: `bitset ${field.name}`,
        lines: [],
        line: field.line,
      });
    }
  }
  for (const fleet of v2.fleets) {
    domains.push({
      kind: "domain",
      domainKind: "bitset",
      name: fleet.name,
      header: `fleet ${fleet.name}`,
      lines: [
        { text: `board ${fleet.board}`, line: fleet.line },
      ],
      line: fleet.line,
    });
    const resourceLines: RawBlockLine[] = [
      { text: `initial ${fleet.ships.initial}`, line: fleet.line },
      { text: `fleet ${fleet.name}`, line: fleet.line },
    ];
    domains.push({
      kind: "domain",
      domainKind: "resource",
      name: fleet.ships.name,
      header: `fleet ships ${fleet.ships.name}`,
      lines: resourceLines,
      line: fleet.line,
    });
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

function lowerV2State(v2: V2ProgramAst, specializedRules: Set<string>): StateAst[] {
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
        lowered.initial = lowerV2InitialExpression(field);
      }
    }
    fields.push(lowered);
  }
  for (const fleet of v2.fleets) {
    const ships: StateFieldAst = {
      name: fleet.ships.name,
      type: "range",
      line: fleet.line,
    };
    if (fleet.ships.min !== undefined) ships.min = fleet.ships.min;
    if (fleet.ships.max !== undefined) ships.max = fleet.ships.max;
    const stackSource = parseStackSource(fleet.ships.initial, fleet.line);
    if (stackSource !== undefined) {
      ships.initialStack = stackSource;
    } else {
      ships.initial = lowerV2Expression(fleet.ships.initial, fleet.line);
    }
    fields.push(ships);
    fields.push({
      name: fleet.name,
      type: "packed",
      initial: lowerV2Expression("random() * 999", fleet.line),
      line: fleet.line,
    });
  }
  const declared = new Set(fields.map((field) => field.name));
  for (const scratch of collectV2ScratchFields(v2, specializedRules)) {
    if (declared.has(scratch.name)) continue;
    declared.add(scratch.name);
    fields.push(scratch);
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

function lowerV2InitialExpression(field: V2StateFieldAst): ExpressionAst {
  const initial = field.initial ?? "0";
  if (field.type === "bitset" && initial.trim() === "random()") {
    return lowerV2Expression("random() * 999", field.line);
  }
  return lowerV2Expression(initial, field.line);
}

function lowerV2StateFieldType(type: V2StateFieldAst["type"]): StateFieldType {
  if (type === "counter") return "range";
  if (type === "coord" || type === "bitset") return "packed";
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
  return v2.encounters.map((table) => ({
    kind: "proc",
    name: "encounter",
    body: [lowerV2EncounterDispatch(table, context)],
    line: table.line,
  }));
}

function lowerV2EncounterDispatch(table: V2EncounterTableAst, context: V2LoweringContext): DispatchStatementAst {
  return {
    kind: "dispatch",
    expr: lowerV2Expression(encounterParamName(table.expr), table.line),
    cases: table.cases.map((encounterCase) => ({
      value: lowerV2Expression(encounterCase.value, encounterCase.line),
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
      return [{ kind: "halt", expr: lowerV2Expression(statement.value, statement.line), line: statement.line }];
    case "v2_invoke":
      return lowerV2Invoke(statement, context);
    case "v2_if": {
      const condition = lowerV2Predicate(statement.predicate, statement.line);
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
      return [{ kind: "assign", target: statement.target, expr: lowerV2Expression(statement.expr, statement.line), line: statement.line }];
    case "v2_update": {
      return [{
        kind: "assign",
        target: statement.target,
        expr: {
          kind: "binary",
          op: statement.op === "+=" ? "+" : "-",
          left: { kind: "identifier", name: statement.target },
          right: lowerV2Expression(statement.expr, statement.line),
        },
        line: statement.line,
      }];
    }
    case "v2_raw":
      return [{
        kind: "core",
        inputs: statement.inputs.map((input) => ({
          slot: input.slot,
          expr: lowerV2Expression(input.expr, input.line),
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
      return [lowerV2Match(statement, context)];
  }
}

function lowerV2Predicate(predicate: V2PredicateAst, line: number): ConditionAst {
  return {
    left: lowerV2Expression(predicate.left, line),
    op: predicate.op,
    right: lowerV2Expression(predicate.right, line),
  };
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
      expr: lowerV2Expression(`memory_code(${statement.expr})`, statement.line),
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
      right: lowerV2Expression(delta, statement.line),
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

function lowerV2Match(statement: V2MatchStatementAst, context: V2LoweringContext): DispatchStatementAst {
  const compact = lowerCompactDirectionDispatch(statement, context);
  if (compact !== undefined) return compact;

  const cases: DispatchCaseAst[] = [];
  for (const matchCase of statement.cases) {
    for (const value of matchCase.values) {
      cases.push({
        value: lowerV2Expression(value, matchCase.line),
        body: lowerV2MatchAction(matchCase.action, context, statement.expr, value, matchCase.line),
        line: matchCase.line,
      });
    }
  }
  const lowered: DispatchStatementAst = {
    kind: "dispatch",
    expr: lowerV2Expression(statement.expr, statement.line),
    cases,
    line: statement.line,
    scratchId: statement.line,
  };
  if (statement.otherwise !== undefined) lowered.defaultBody = lowerV2Statement(statement.otherwise, context);
  return lowered;
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

  const cases: DispatchCaseAst[] = [];
  for (const matchCase of statement.cases) {
    if (directionalCases.includes(matchCase)) continue;
    for (const value of matchCase.values) {
      cases.push({
        value: lowerV2Expression(value, matchCase.line),
        body: lowerV2MatchAction(matchCase.action, context, statement.expr, value, matchCase.line),
        line: matchCase.line,
      });
    }
  }

  const defaultBody: StatementAst[] = [
    {
      kind: "assign",
      target: params[0]!,
      expr: lowerV2Expression(`direction(${statement.expr})`, statement.line),
      line: statement.line,
    },
    { kind: "call", block: action.name, line: action.line },
  ];

  return {
    kind: "dispatch",
    expr: lowerV2Expression(statement.expr, statement.line),
    name: "direction_dispatch",
    cases,
    defaultBody,
    line: statement.line,
    scratchId: statement.line,
  };
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
    const args = action.args.map((arg) => resolveV2InvokeArg(arg, matchExpr, matchValue));
    return lowerV2Statements(substituteV2Statements(rule.body, invokeReplacements(rule, args)), context);
  }
  const params = context.ruleParams.get(action.name) ?? [];
  const statements: StatementAst[] = [];
  for (let index = 0; index < Math.min(params.length, action.args.length); index += 1) {
    const arg = resolveV2InvokeArg(action.args[index]!, matchExpr, matchValue);
    statements.push({
      kind: "assign",
      target: params[index]!,
      expr: lowerV2Expression(arg, line),
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
      expr: lowerV2Expression(statement.args[index]!, statement.line),
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

function resolveV2InvokeArg(arg: string, matchExpr: string, matchValue: string): string {
  const direction = /^direction\((.+)\)$/u.exec(arg.trim());
  if (direction && direction[1]!.trim() === matchExpr.trim()) {
    const delta = directionDelta(matchValue);
    if (delta !== undefined) return String(delta);
  }
  return arg;
}

function lowerV2Expression(text: string, line: number): ExpressionAst {
  const normalized = normalizeV2ExpressionText(text);
  return parseExpression(normalized, line);
}

export function normalizeV2ExpressionText(text: string): string {
  return text
    .trim()
    .replace(/\b([A-Za-z_][\w]*)\.floor\b/gu, "int($1 / 100)");
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
  return splitDisplayArgs(text, line).map((part) => parseDisplayItem(part, line));
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
  if (/^[A-Za-z_][\w]*$/u.test(trimmed)) {
    return { kind: "source", name: trimmed, line };
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

function splitDisplayArgs(text: string, line: number): string[] {
  const parts: string[] = [];
  let start = 0;
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
    if (char === "," && !quoted) {
      parts.push(text.slice(start, index).trim());
      start = index + 1;
    }
  }
  if (quoted) throw new ParseError("Unclosed display string literal", line);
  parts.push(text.slice(start).trim());
  return parts.filter((part) => part.length > 0);
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
