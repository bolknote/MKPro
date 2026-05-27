import type {
  AllowFeature,
  AskStatementAst,
  AssignStatementAst,
  BlockAst,
  CallBlockStatementAst,
  ConditionAst,
  ConstDeclarationAst,
  DeclarationAst,
  DomainAst,
  DispatchCaseAst,
  DispatchStatementAst,
  DisplayAst,
  EggStatementAst,
  EntryAst,
  ExpressionAst,
  HaltStatementAst,
  InputStatementAst,
  IfStatementAst,
  LoopStatementAst,
  PauseStatementAst,
  PreloadAst,
  ProgramAst,
  ProcAst,
  RawBlockLine,
  StatementAst,
  StateAst,
  StateFieldAst,
  StateFieldType,
  StorageHint,
  StoreDeclarationAst,
  SwitchCaseAst,
  SwitchStatementAst,
  TempDeclarationAst,
  TrapStatementAst,
  SemanticHint,
  V2CollectionStatementAst,
  V2InputAst,
  V2InvokeStatementAst,
  V2MatchCaseAst,
  V2MatchStatementAst,
  V2PredicateAst,
  V2ProgramAst,
  V2RewardStatementAst,
  V2RuleAst,
  V2ScreenAst,
  V2StateFieldAst,
  V2StatementAst,
  V2TurnAst,
} from "./types.ts";
import { registerFromText } from "./opcodes.ts";

interface SourceLine {
  text: string;
  line: number;
}

export class ParseError extends Error {
  readonly line: number;

  constructor(message: string, line: number) {
    super(`${message} at line ${line}`);
    this.line = line;
  }
}

export function parseProgram(source: string): ProgramAst {
  return new M61Parser(source).parseProgram();
}

class M61Parser {
  private readonly lines: SourceLine[];
  private index = 0;
  private switchCounter = 0;
  private dispatchCounter = 0;

  constructor(source: string) {
    this.lines = source
      .split(/\r?\n/u)
      .map((text, offset) => ({ text: stripComment(text).trim(), line: offset + 1 }))
      .filter((line) => line.text.length > 0);
  }

  parseProgram(): ProgramAst {
    let machine: "mk61" | undefined;
    let targetProfile: "mk61_exact" | undefined;
    let budget: number | undefined;
    let benchmark: string | undefined;
    let optimize: "size" | undefined;
    let v2: V2ProgramAst | undefined;
    const preloads: PreloadAst[] = [];
    const domains: DomainAst[] = [];
    const allows: AllowFeature[] = [];
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
      if (line.text.startsWith("target ")) {
        const value = line.text.slice("target ".length).trim().toLowerCase();
        if (value !== "mk61") {
          throw new ParseError(`Unsupported target '${value}'`, line.line);
        }
        machine = "mk61";
        targetProfile = "mk61_exact";
        this.index += 1;
      } else if (line.text.startsWith("machine ")) {
        const value = line.text.slice("machine ".length).trim().toLowerCase();
        if (value !== "mk61") {
          throw new ParseError(`Unsupported machine '${value}'`, line.line);
        }
        machine = "mk61";
        targetProfile ??= "mk61_exact";
        this.index += 1;
      } else if (line.text.startsWith("budget ")) {
        budget = parseBudget(line);
        this.index += 1;
      } else if (line.text === "optimize size") {
        optimize = "size";
        this.index += 1;
      } else if (line.text.startsWith("benchmark ")) {
        benchmark = line.text.slice("benchmark ".length).trim();
        this.index += 1;
      } else if (line.text.startsWith("preload ")) {
        preloads.push(parsePreload(line));
        this.index += 1;
      } else if (line.text.startsWith("allow ")) {
        allows.push(...parseAllow(line));
        this.index += 1;
      } else if (line.text.startsWith("program ")) {
        if (v2 !== undefined) {
          throw new ParseError("Only one program block is supported", line.line);
        }
        v2 = this.parseV2Program();
        const lowered = lowerV2Program(v2);
        states.push(...lowered.states);
        displays.push(...lowered.displays);
        entries.push(...lowered.entries);
        procs.push(...lowered.procs);
        blocks.push(...lowered.blocks);
      } else if (isDomainHeader(line.text)) {
        domains.push(this.parseDomain());
      } else if (line.text.startsWith("state")) {
        states.push(this.parseState());
      } else if (line.text.startsWith("display packed")) {
        displays.push(this.parseDisplay());
      } else if (
        line.text.startsWith("store ") ||
        line.text.startsWith("temp ") ||
        line.text.startsWith("const ")
      ) {
        declarations.push(this.parseDeclaration());
      } else if (line.text.startsWith("entry")) {
        entries.push(this.parseEntry());
      } else if (line.text.startsWith("proc")) {
        procs.push(this.parseProc());
      } else if (line.text.startsWith("block ") || line.text.startsWith("shared tail")) {
        blocks.push(this.parseBlock());
      } else {
        throw new ParseError(`Unexpected top-level line '${line.text}'`, line.line);
      }
    }

    if (!machine) throw new ParseError("Missing 'machine mk61' or 'target mk61'", 1);
    if (entries.length === 0) {
      throw new ParseError("Program must contain at least one entry block", 1);
    }

    const program: ProgramAst = {
      machine,
      targetProfile: targetProfile ?? "mk61_exact",
      preloads,
      domains,
      allows: uniqueAllows(allows),
      states,
      displays,
      declarations,
      entries,
      procs,
      blocks,
    };
    if (budget !== undefined) program.budget = budget;
    if (benchmark !== undefined) program.benchmark = benchmark;
    if (optimize !== undefined) program.optimize = optimize;
    if (v2 !== undefined) program.v2 = v2;
    return program;
  }

  private parseV2Program(): V2ProgramAst {
    const header = this.next();
    const match = /^program\s+([A-Za-z_][\w]*)\s*\{$/u.exec(header.text);
    if (!match) throw new ParseError("Program must look like 'program Name {'", header.line);
    const inputs: V2InputAst[] = [];
    const state: V2StateFieldAst[] = [];
    const screens: V2ScreenAst[] = [];
    const rules: V2RuleAst[] = [];
    let turn: V2TurnAst | undefined;

    while (!this.done()) {
      const line = this.peek();
      if (line.text === "}") {
        this.index += 1;
        const program: V2ProgramAst = {
          kind: "v2_program",
          name: match[1]!,
          inputs,
          state,
          screens,
          rules,
          line: header.line,
        };
        if (turn !== undefined) program.turn = turn;
        return program;
      }
      const hinted = parseLeadingHints(line);
      if (hinted.text.startsWith("input ")) {
        inputs.push(parseV2Input(hinted, line.line));
        this.index += 1;
        continue;
      }
      if (hinted.text === "state {") {
        this.index += 1;
        state.push(...this.parseV2StateBlock());
        continue;
      }
      if (hinted.text.startsWith("screen ")) {
        screens.push(this.parseV2Screen(hinted));
        continue;
      }
      if (hinted.text === "turn {") {
        if (turn !== undefined) throw new ParseError("Only one turn block is supported", line.line);
        this.index += 1;
        turn = {
          kind: "v2_turn",
          body: this.parseV2StatementBlock(),
          hints: hinted.hints,
          line: line.line,
        };
        continue;
      }
      if (hinted.text.startsWith("rule ")) {
        rules.push(this.parseV2Rule(hinted));
        continue;
      }
      throw new ParseError(`Unexpected program line '${line.text}'`, line.line);
    }
    throw new ParseError("Unclosed program block", header.line);
  }

  private parseV2StateBlock(): V2StateFieldAst[] {
    const fields: V2StateFieldAst[] = [];
    while (!this.done()) {
      const line = this.next();
      if (line.text === "}") return fields;
      fields.push(parseV2StateField(line));
    }
    throw new ParseError("Unclosed state block", this.lines.at(-1)?.line ?? 1);
  }

  private parseV2Screen(hinted: { text: string; hints: SemanticHint[] }): V2ScreenAst {
    const header = this.next();
    const match = /^screen\s+([A-Za-z_][\w]*)\s*\{$/u.exec(hinted.text);
    if (!match) throw new ParseError("Screen must look like 'screen name {'", header.line);
    let sources: string[] = [];
    let style: string[] = [];
    while (!this.done()) {
      const line = this.next();
      if (line.text === "}") {
        return {
          kind: "v2_screen",
          name: match[1]!,
          sources,
          style,
          hints: hinted.hints,
          line: header.line,
        };
      }
      const body = parseLeadingHints(line);
      if (body.text.startsWith("show ")) {
        sources = parseIdentifierList(body.text.slice("show ".length));
        continue;
      }
      if (body.text.startsWith("style ")) {
        style = body.text.slice("style ".length).split(/\s+/u).filter(Boolean);
        continue;
      }
      throw new ParseError("Screen block must contain show/style lines", line.line);
    }
    throw new ParseError("Unclosed screen block", header.line);
  }

  private parseV2Rule(hinted: { text: string; hints: SemanticHint[] }): V2RuleAst {
    const header = this.next();
    const match = /^rule\s+([A-Za-z_][\w]*)(?:\(([^)]*)\))?\s*\{$/u.exec(hinted.text);
    if (!match) throw new ParseError("Rule must look like 'rule name {' or 'rule name(arg) {'", header.line);
    return {
      kind: "v2_rule",
      name: match[1]!,
      params: match[2] ? parseIdentifierList(match[2]) : [],
      body: this.parseV2StatementBlock(),
      hints: hinted.hints,
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
    const hinted = parseLeadingHints(line);
    if (hinted.text.startsWith("match ") && hinted.text.endsWith("{")) {
      this.index += 1;
      return this.parseV2Match(hinted.text, hinted.hints, line.line);
    }
    if (hinted.text.startsWith("if ") && hinted.text.endsWith("{")) {
      this.index += 1;
      const predicate = parseV2Predicate(hinted.text.slice("if ".length, -1).trim());
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
        hints: hinted.hints,
        line: line.line,
      };
      if (elseBody !== undefined) statement.elseBody = elseBody;
      return statement;
    }
    if (hinted.text.startsWith("challenge ") && hinted.text.endsWith("{")) {
      this.index += 1;
      return this.parseV2Challenge(hinted.text, hinted.hints, line.line);
    }
    this.index += 1;
    return parseV2InlineStatement(hinted.text, hinted.hints, line.line);
  }

  private parseV2Challenge(text: string, hints: SemanticHint[], line: number): V2StatementAst {
    const match = /^challenge\s+(.+?)(?:\s+as\s+([A-Za-z_][\w]*))?\s*\{$/u.exec(text);
    if (!match) throw new ParseError("Challenge must look like 'challenge expr {'", line);
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
          challengeTarget: match[2] ?? "challenge",
          warningScreen: "warning",
          memoryScreen: "memory",
          answerInput: "answer",
          hints,
          line,
        };
        if (failureBody !== undefined) statement.failureBody = failureBody;
        return statement;
      }
      const hinted = parseLeadingHints(bodyLine);
      if (hinted.text === "success {") {
        this.index += 1;
        successBody = this.parseV2StatementBlock();
        continue;
      }
      if (hinted.text === "failure {") {
        this.index += 1;
        failureBody = this.parseV2StatementBlock();
        continue;
      }
      throw new ParseError("Challenge block must contain success/failure blocks", bodyLine.line);
    }
    throw new ParseError("Unclosed challenge block", line);
  }

  private parseV2Match(text: string, hints: SemanticHint[], line: number): V2MatchStatementAst {
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
          hints,
          line,
        };
        if (otherwise !== undefined) statement.otherwise = otherwise;
        return statement;
      }
      const hinted = parseLeadingHints(bodyLine);
      const arrow = /^(.+?)\s*=>\s*(.+)$/u.exec(hinted.text);
      if (!arrow) throw new ParseError("Match cases must look like 'value => action'", bodyLine.line);
      const left = arrow[1]!.trim();
      const action = parseV2InlineStatement(arrow[2]!.trim(), hinted.hints, bodyLine.line);
      if (left === "otherwise") {
        otherwise = action;
      } else {
        cases.push({
          values: left.split(",").map((part) => part.trim()).filter(Boolean),
          action,
          hints: hinted.hints,
          line: bodyLine.line,
        });
      }
    }
    throw new ParseError("Unclosed match block", line);
  }

  private parseDomain(): DomainAst {
    const header = this.next();
    const parsed = parseDomainHeader(header);
    if (!header.text.endsWith("{")) {
      return { kind: "domain", ...parsed, lines: [], line: header.line };
    }
    return {
      kind: "domain",
      ...parsed,
      lines: this.parseRawBlock(),
      line: header.line,
    };
  }

  private parseState(): StateAst {
    const header = this.next();
    const match = /^state(?:\s+([A-Za-z_][\w]*))?\s*\{$/u.exec(header.text);
    if (!match) throw new ParseError("State must look like 'state Name {'", header.line);
    const fields: StateFieldAst[] = [];
    while (!this.done()) {
      const line = this.next();
      if (line.text === "}") {
        return {
          kind: "state",
          name: match[1] ?? "State",
          fields,
          line: header.line,
        };
      }
      fields.push(parseStateField(line));
    }
    throw new ParseError("Unclosed state block", header.line);
  }

  private parseDisplay(): DisplayAst {
    const header = this.next();
    const match = /^display\s+packed\s+([A-Za-z_][\w]*)(?:\s+from\s+(.+?))?\s*\{$/u.exec(header.text);
    if (!match) {
      throw new ParseError("Display must look like 'display packed name {'", header.line);
    }
    let mode: string | undefined;
    let sources = match[2] ? parseIdentifierList(match[2]) : [];

    while (!this.done()) {
      const line = this.next();
      if (line.text === "}") {
        const display: DisplayAst = {
          kind: "display",
          name: match[1]!,
          format: "packed",
          sources,
          line: header.line,
        };
        if (mode !== undefined) display.mode = mode;
        return display;
      }
      if (line.text.startsWith("mode ")) {
        mode = line.text.slice("mode ".length).trim();
        continue;
      }
      if (line.text.startsWith("source ")) {
        sources = parseIdentifierList(line.text.slice("source ".length));
        continue;
      }
      if (line.text.startsWith("sources ")) {
        sources = parseIdentifierList(line.text.slice("sources ".length));
        continue;
      }
      throw new ParseError("Display block must contain mode/source lines", line.line);
    }
    throw new ParseError("Unclosed display block", header.line);
  }

  private parseDeclaration(): DeclarationAst {
    const line = this.next();
    if (line.text.startsWith("const ")) {
      const match = /^const\s+([A-Za-z_][\w]*)\s*=\s*(.+)$/u.exec(line.text);
      if (!match) throw new ParseError("Invalid const declaration", line.line);
      const declaration: ConstDeclarationAst = {
        kind: "const",
        name: match[1]!,
        value: parseExpression(match[2]!, line.line),
        line: line.line,
      };
      return declaration;
    }

    const match =
      /^(store|temp)\s+([A-Za-z_][\w]*)(?:\s*:\s*[A-Za-z_][\w]*)?(?:\s*=\s*(.*?))?(?:\s+(prefer|fixed)\s+R?([0-9a-eавсде]))?$/iu.exec(
        line.text,
      );
    if (!match) {
      throw new ParseError("Invalid store/temp declaration", line.line);
    }

    const hint = parseStorageHint(match[4], match[5]);
    if (match[1] === "store") {
      const declaration: StoreDeclarationAst = {
        kind: "store",
        name: match[2]!,
        line: line.line,
      };
      if (match[3]?.trim()) declaration.value = parseExpression(match[3].trim(), line.line);
      if (hint) declaration.storage = hint;
      return declaration;
    }

    const declaration: TempDeclarationAst = {
      kind: "temp",
      name: match[2]!,
      line: line.line,
    };
    if (hint) declaration.storage = hint;
    return declaration;
  }

  private parseEntry(): EntryAst {
    const line = this.next();
    const match = /^entry(?:\s+([A-Za-z_][\w]*))?(?:\s+at\s+([0-9A-Fa-f]{1,2}|A[0-4]))?\s*\{$/u.exec(line.text);
    if (!match) throw new ParseError("Entry must look like 'entry name {'", line.line);
    const entry: EntryAst = {
      kind: "entry",
      name: match[1] ?? "main",
      body: this.parseStatementBlock(),
      line: line.line,
    };
    if (match[2] !== undefined) entry.at = parseFormalAddress(match[2], line.line);
    return entry;
  }

  private parseProc(): ProcAst {
    const line = this.next();
    const match = /^proc\s+([A-Za-z_][\w]*)[^{]*\{$/u.exec(line.text);
    if (!match) throw new ParseError("Proc must look like 'proc name {'", line.line);
    return {
      kind: "proc",
      name: match[1]!,
      body: this.parseStatementBlock(),
      line: line.line,
    };
  }

  private parseBlock(): BlockAst {
    const line = this.next();
    const match = /^(?:block\s+(inline|tail)\s+([A-Za-z_][\w]*)|shared\s+tail\s+([A-Za-z_][\w]*))\s*\{$/u.exec(
      line.text,
    );
    if (!match) {
      throw new ParseError("Block must look like 'block inline name {' or 'shared tail name {'", line.line);
    }
    const mode = match[3] ? "shared_tail" : (match[1] as "inline" | "tail");
    return {
      kind: "block",
      name: match[2] ?? match[3]!,
      mode,
      body: this.parseStatementBlock(),
      line: line.line,
    };
  }

  private parseStatementBlock(): StatementAst[] {
    const statements: StatementAst[] = [];
    while (!this.done()) {
      const line = this.peek();
      if (line.text === "}") {
        this.index += 1;
        return statements;
      }
      statements.push(this.parseStatement());
    }
    throw new ParseError("Unclosed block", this.lines.at(-1)?.line ?? 1);
  }

  private parseStatement(): StatementAst {
    const line = this.peek();

    if (line.text === "loop {") {
      this.index += 1;
      const loop: LoopStatementAst = {
        kind: "loop",
        body: this.parseStatementBlock(),
        line: line.line,
      };
      return loop;
    }

    if (line.text.startsWith("if ") && line.text.endsWith("{")) {
      this.index += 1;
      const conditionText = line.text.slice(3, -1).trim();
      const thenBody = this.parseStatementBlock();
      const statement: IfStatementAst = {
        kind: "if",
        condition: parseCondition(conditionText, line.line),
        thenBody,
        line: line.line,
      };
      const next = this.peekOptional();
      if (next?.text === "else {") {
        this.index += 1;
        statement.elseBody = this.parseStatementBlock();
      }
      return statement;
    }

    if (line.text.startsWith("switch ") && line.text.endsWith("{")) {
      this.index += 1;
      return this.parseSwitch(line);
    }

    if (line.text.startsWith("dispatch ") && line.text.endsWith("{")) {
      this.index += 1;
      return this.parseDispatch(line);
    }

    if (line.text === "core {") {
      this.index += 1;
      return { kind: "core", lines: this.parseRawBlock(), line: line.line };
    }

    if (line.text === "egg {" || line.text === "unsafe asm {") {
      this.index += 1;
      const statement: EggStatementAst = {
        kind: "egg",
        lines: this.parseRawBlock(),
        line: line.line,
      };
      return statement;
    }

    this.index += 1;
    return parseSimpleStatement(line);
  }

  private parseSwitch(header: SourceLine): StatementAst {
    const exprText = header.text.slice("switch ".length, -1).trim();
    const cases: SwitchCaseAst[] = [];
    let defaultBody: StatementAst[] | undefined;
    const scratchId = this.switchCounter++;

    while (!this.done()) {
      const line = this.peek();
      if (line.text === "}") {
        this.index += 1;
        const statement: SwitchStatementAst = {
          kind: "switch",
          expr: parseExpression(exprText, header.line),
          cases,
          line: header.line,
          scratchId,
        };
        if (defaultBody !== undefined) statement.defaultBody = defaultBody;
        return statement;
      }
      if (line.text.startsWith("case ") && line.text.endsWith("{")) {
        this.index += 1;
        cases.push({
          value: parseExpression(line.text.slice("case ".length, -1).trim(), line.line),
          body: this.parseStatementBlock(),
          line: line.line,
        });
        continue;
      }
      if (line.text === "default {") {
        this.index += 1;
        defaultBody = this.parseStatementBlock();
        continue;
      }
      throw new ParseError("Switch body must contain case/default blocks", line.line);
    }

    throw new ParseError("Unclosed switch block", header.line);
  }

  private parseDispatch(header: SourceLine): StatementAst {
    const match = /^dispatch\s+(.+?)(?:\s+as\s+([A-Za-z_][\w]*))?\s*\{$/u.exec(header.text);
    if (!match) {
      throw new ParseError("Dispatch must look like 'dispatch expr {'", header.line);
    }
    const cases: DispatchCaseAst[] = [];
    let defaultBody: StatementAst[] | undefined;
    const scratchId = this.dispatchCounter++;

    while (!this.done()) {
      const line = this.peek();
      if (line.text === "}") {
        this.index += 1;
        const statement: DispatchStatementAst = {
          kind: "dispatch",
          expr: parseExpression(match[1]!.trim(), header.line),
          cases,
          line: header.line,
          scratchId,
        };
        if (match[2] !== undefined) statement.name = match[2];
        if (defaultBody !== undefined) statement.defaultBody = defaultBody;
        return statement;
      }
      if (line.text.startsWith("case ") && line.text.endsWith("{")) {
        this.index += 1;
        cases.push({
          value: parseExpression(line.text.slice("case ".length, -1).trim(), line.line),
          body: this.parseStatementBlock(),
          line: line.line,
        });
        continue;
      }
      if (line.text === "default {") {
        this.index += 1;
        defaultBody = this.parseStatementBlock();
        continue;
      }
      throw new ParseError("Dispatch body must contain case/default blocks", line.line);
    }

    throw new ParseError("Unclosed dispatch block", header.line);
  }

  private parseRawBlock(): RawBlockLine[] {
    const lines: RawBlockLine[] = [];
    while (!this.done()) {
      const line = this.next();
      if (line.text === "}") return lines;
      lines.push({ text: line.text, line: line.line });
    }
    throw new ParseError("Unclosed raw block", this.lines.at(-1)?.line ?? 1);
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

function parseBudget(line: SourceLine): number {
  const match =
    /^budget\s+steps\s*<=\s*(\d+)(?:\s+hard)?$/u.exec(line.text) ??
    /^budget\s+(\d+)\s+cells(?:\s+hard)?$/u.exec(line.text);
  if (!match) {
    throw new ParseError("Budget must look like 'budget steps <= 105' or 'budget 105 cells'", line.line);
  }
  return Number(match[1]);
}

const semanticHints: SemanticHint[] = [
  "hot",
  "rare",
  "cold",
  "displayed",
  "hidden",
  "temporary",
  "persistent",
  "wrap",
  "saturate",
  "trap",
  "unordered",
  "approx",
  "exact",
  "manual_entry",
  "preload",
];

const forbiddenLowLevelHints = new Set([
  "use_dark_entries",
  "use_x2",
  "put_in_r0",
  "overlay_here",
  "use_overlay",
  "use_display_bytes",
  "use_error_stop",
  "use_5f",
]);

function parseLeadingHints(line: SourceLine): { text: string; hints: SemanticHint[] } {
  let text = line.text;
  const hints: SemanticHint[] = [];
  while (text.startsWith("[")) {
    const end = text.indexOf("]");
    if (end < 0) throw new ParseError("Unclosed hint list", line.line);
    const rawHints = text.slice(1, end).split(",").map((part) => part.trim()).filter(Boolean);
    for (const rawHint of rawHints) {
      const normalized = rawHint.replace(/-/gu, "_").toLowerCase();
      if (forbiddenLowLevelHints.has(normalized)) {
        throw new ParseError(`Low-level implementation hint '${rawHint}' is not allowed in M61`, line.line);
      }
      if (!semanticHints.includes(normalized as SemanticHint)) {
        throw new ParseError(`Unknown semantic hint '${rawHint}'`, line.line);
      }
      hints.push(normalized as SemanticHint);
    }
    text = text.slice(end + 1).trim();
  }
  return { text, hints };
}

function parseV2Input(
  hinted: { text: string; hints: SemanticHint[] },
  line: number,
): V2InputAst {
  const match = /^input\s+([A-Za-z_][\w]*)\s*:\s*(digit|number)$/u.exec(hinted.text);
  if (!match) throw new ParseError("Input must look like 'input key: digit' or 'input answer: number'", line);
  return {
    kind: "v2_input",
    name: match[1]!,
    inputType: match[2] as "digit" | "number",
    hints: hinted.hints,
    line,
  };
}

function parseV2StateField(line: SourceLine): V2StateFieldAst {
  const hinted = parseLeadingHints(line);
  const match = /^([A-Za-z_][\w]*)\s*:\s*([A-Za-z_][\w]*)(?:\(([^)]*)\))?(.*)$/u.exec(hinted.text);
  if (!match) {
    throw new ParseError("State field must look like 'name: counter 0..9 = 0'", line.line);
  }
  const typeText = match[2]!.toLowerCase();
  if (!["digit", "flag", "counter", "coord", "bitset", "enum", "packed", "addr", "resource"].includes(typeText)) {
    throw new ParseError(`Unknown state type '${match[2]}'`, line.line);
  }
  const tail = match[4]!.trim();
  const initialMatch = /(?:^|\s)=\s*(.+)$/u.exec(tail);
  const rangeMatch = /(-?\d+)\.\.(-?\d+)/u.exec(tail);
  const field: V2StateFieldAst = {
    kind: "v2_state_field",
    name: match[1]!,
    type: typeText as V2StateFieldAst["type"],
    optional: /\boptional\b/u.test(tail),
    hints: hinted.hints,
    line: line.line,
  };
  if (match[3] !== undefined) field.spec = match[3]!.trim();
  if (rangeMatch) {
    field.min = Number(rangeMatch[1]);
    field.max = Number(rangeMatch[2]);
  }
  if (/\bgenerated\s+random\b/u.test(tail)) field.generated = "random";
  if (initialMatch) field.initial = initialMatch[1]!.trim();
  return field;
}

function parseV2InlineStatement(
  text: string,
  hints: SemanticHint[],
  line: number,
): V2StatementAst {
  if (text.startsWith("let ")) {
    const match = /^let\s+([A-Za-z_][\w]*)\s*=\s*(.+)$/u.exec(text);
    if (!match) throw new ParseError("Let must look like 'let name = expr'", line);
    return {
      kind: "v2_let",
      name: match[1]!,
      expr: match[2]!.trim(),
      hints,
      line,
    };
  }
  if (text.startsWith("show ")) {
    return { kind: "v2_show", target: text.slice("show ".length).trim(), hints, line };
  }
  if (text.startsWith("read ")) {
    return { kind: "v2_read", target: text.slice("read ".length).trim(), hints, line };
  }
  if (text.startsWith("stop ")) {
    return { kind: "v2_stop", value: text.slice("stop ".length).trim(), hints, line };
  }
  if (text.startsWith("require ")) {
    const match = /^require\s+(.+?)(?:\s+else\s+(.+))?$/u.exec(text);
    if (!match) throw new ParseError("Require must look like 'require condition [else action]'", line);
    const statement: V2StatementAst = {
      kind: "v2_require",
      predicate: parseV2Predicate(match[1]!.trim()),
      hints,
      line,
    };
    if (match[2] !== undefined) {
      statement.elseAction = parseV2InlineStatement(match[2]!.trim(), hints, line);
    }
    return statement;
  }
  const collection = /^([A-Za-z_][\w]*)\s+(clear|set)\s+(.+)$/u.exec(text);
  if (collection) {
    return {
      kind: "v2_collection",
      collection: collection[1]!,
      op: collection[2] as "clear" | "set",
      item: collection[3]!.trim(),
      hints,
      line,
    };
  }
  if (text.startsWith("reward by ")) {
    return {
      kind: "v2_reward",
      expr: text.slice("reward by ".length).trim(),
      hints,
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
      hints,
      line,
    };
  }
  const assignment = /^([A-Za-z_][\w]*)\s*=\s*(.+)$/u.exec(text);
  if (assignment) {
    return {
      kind: "v2_assign",
      target: assignment[1]!,
      expr: assignment[2]!.trim(),
      hints,
      line,
    };
  }
  const invoke = /^([A-Za-z_][\w]*)(?:\((.*)\))?$/u.exec(text);
  if (invoke) {
    return {
      kind: "v2_invoke",
      name: invoke[1]!,
      args: invoke[2] ? splitArgs(invoke[2]) : [],
      hints,
      line,
    };
  }
  return { kind: "v2_raw", text, hints, line };
}

function parseV2Predicate(text: string): V2PredicateAst {
  const exists = /^([A-Za-z_][\w]*)\s+exists$/u.exec(text);
  if (exists) {
    return { kind: "v2_exists", target: exists[1]! };
  }
  const has = /^([A-Za-z_][\w]*)\s+has\s+(.+)$/u.exec(text);
  if (has) {
    return {
      kind: "v2_collection_has",
      collection: has[1]!,
      item: has[2]!.trim(),
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
  return { kind: "v2_raw_predicate", text };
}

function splitArgs(text: string): string[] {
  return text
    .split(",")
    .map((part) => part.trim())
    .filter(Boolean);
}

interface V2LoweringContext {
  ruleParams: Map<string, string[]>;
}

function lowerV2Program(v2: V2ProgramAst): {
  states: StateAst[];
  displays: DisplayAst[];
  entries: EntryAst[];
  procs: ProcAst[];
  blocks: BlockAst[];
} {
  const context: V2LoweringContext = {
    ruleParams: new Map(v2.rules.map((rule) => [rule.name, rule.params])),
  };
  return {
    states: lowerV2State(v2),
    displays: v2.screens.map(lowerV2Screen),
    entries: [lowerV2Entry(v2, context)],
    procs: v2.rules.map((rule) => lowerV2Rule(rule, context)),
    blocks: [],
  };
}

function lowerV2State(v2: V2ProgramAst): StateAst[] {
  const fields: StateFieldAst[] = [];
  for (const input of v2.inputs) {
    fields.push({
      name: input.name,
      type: input.inputType === "digit" ? "digit" : "packed",
      line: input.line,
    });
  }
  for (const field of v2.state) {
    const lowered: StateFieldAst = {
      name: field.name,
      type: lowerV2StateFieldType(field.type),
      line: field.line,
    };
    if (field.min !== undefined) lowered.min = field.min;
    if (field.max !== undefined) lowered.max = field.max;
    if (field.initial !== undefined) {
      const inputSource = parseInputSource(field.initial);
      if (inputSource !== undefined) {
        lowered.initialInput = inputSource;
      } else {
        lowered.initial = lowerV2Expression(field.initial, field.line);
      }
    } else if (field.generated === "random") {
      lowered.initial = lowerV2Expression("random() * 999", field.line);
    } else if (field.optional) {
      lowered.initial = parseExpression("0", field.line);
    }
    fields.push(lowered);
  }
  const declared = new Set(fields.map((field) => field.name));
  for (const scratch of collectV2ScratchFields(v2)) {
    if (declared.has(scratch.name)) continue;
    declared.add(scratch.name);
    fields.push(scratch);
  }
  return fields.length > 0 ? [{ kind: "state", name: v2.name, fields, line: v2.line }] : [];
}

function collectV2ScratchFields(v2: V2ProgramAst): StateFieldAst[] {
  const fields: StateFieldAst[] = [];
  const add = (name: string, line: number): void => {
    fields.push({ name, type: "packed", line });
  };
  for (const rule of v2.rules) {
    for (const param of rule.params) add(param, rule.line);
    const visit = (statements: V2StatementAst[]): void => {
      for (const statement of statements) {
        if (statement.kind === "v2_let") add(statement.name, statement.line);
        if (statement.kind === "v2_if") {
          visit(statement.thenBody);
          if (statement.elseBody) visit(statement.elseBody);
        }
        if (statement.kind === "v2_match") {
          for (const matchCase of statement.cases) visit([matchCase.action]);
          if (statement.otherwise) visit([statement.otherwise]);
        }
        if (statement.kind === "v2_require" && statement.elseAction) visit([statement.elseAction]);
        if (statement.kind === "v2_challenge") {
          add(statement.challengeTarget, statement.line);
          visit(statement.successBody);
          if (statement.failureBody) visit(statement.failureBody);
        }
      }
    };
    visit(rule.body);
  }
  return fields;
}

function lowerV2StateFieldType(type: V2StateFieldAst["type"]): StateFieldType {
  if (type === "counter") return "range";
  if (type === "coord" || type === "bitset" || type === "enum") return "packed";
  if (type === "resource") return "resource";
  return type;
}

function lowerV2Screen(screen: V2ScreenAst): DisplayAst {
  const display: DisplayAst = {
    kind: "display",
    name: screen.name,
    format: "packed",
    sources: screen.sources,
    line: screen.line,
  };
  if (screen.style.length > 0) display.mode = screen.style.join("_");
  return display;
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

function lowerV2Statements(statements: V2StatementAst[], context: V2LoweringContext): StatementAst[] {
  const lowered: StatementAst[] = [];
  for (let index = 0; index < statements.length; index += 1) {
    const statement = statements[index]!;
    if (statement.kind === "v2_require") {
      const condition = lowerV2Predicate(statement.predicate, statement.line);
      const thenBody = lowerV2Statements(statements.slice(index + 1), context);
      const elseBody: StatementAst[] = statement.elseAction
        ? lowerV2Statements([statement.elseAction], context)
        : [{ kind: "pause", expr: parseExpression("0", statement.line), line: statement.line }];
      lowered.push({
        kind: "if",
        condition,
        thenBody,
        elseBody,
        line: statement.line,
      });
      return lowered;
    }
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
      return [{ kind: "input", inputType: "digit", target: statement.target, line: statement.line }];
    case "v2_stop":
      return [{ kind: "halt", expr: lowerV2Expression(statement.value, statement.line), line: statement.line }];
    case "v2_let":
      return [{ kind: "assign", target: statement.name, expr: lowerV2Expression(statement.expr, statement.line), line: statement.line }];
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
    case "v2_require":
      return [];
    case "v2_challenge":
      return lowerV2Challenge(statement, context);
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
    case "v2_match":
      return [lowerV2Match(statement, context)];
    case "v2_collection":
      return [lowerV2Collection(statement)];
    case "v2_reward":
      return lowerV2Reward(statement);
    case "v2_raw":
      return [];
  }
}

function lowerV2Predicate(predicate: V2PredicateAst, line: number): ConditionAst {
  switch (predicate.kind) {
    case "v2_compare":
      return {
        left: lowerV2Expression(predicate.left, line),
        op: predicate.op,
        right: lowerV2Expression(predicate.right, line),
      };
    case "v2_exists":
      return {
        left: lowerV2Expression(predicate.target, line),
        op: "!=",
        right: parseExpression("0", line),
      };
    case "v2_collection_has": {
      return {
        left: { kind: "identifier", name: predicate.collection },
        op: ">=",
        right: lowerV2Expression(predicate.item, line),
      };
    }
    case "v2_raw_predicate":
      return {
        left: parseExpression("0", line),
        op: "!=",
        right: parseExpression("0", line),
      };
  }
}

function lowerV2Challenge(
  statement: Extract<V2StatementAst, { kind: "v2_challenge" }>,
  context: V2LoweringContext,
): StatementAst[] {
  const failureBody = statement.failureBody ?? [{ kind: "v2_show", target: "0", hints: [], line: statement.line } satisfies V2StatementAst];
  return [
    {
      kind: "assign",
      target: statement.challengeTarget,
      expr: lowerV2Expression(`memory_code(${statement.expr})`, statement.line),
      line: statement.line,
    },
    { kind: "show", display: statement.warningScreen, line: statement.line },
    { kind: "show", display: statement.memoryScreen, line: statement.line },
    { kind: "input", inputType: "digit", target: statement.answerInput, line: statement.line },
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

function resolveV2InvokeArg(arg: string, matchExpr: string, matchValue: string): string {
  const direction = /^direction\((.+)\)$/u.exec(arg.trim());
  if (direction && direction[1]!.trim() === matchExpr.trim()) {
    const delta = directionDelta(matchValue);
    if (delta !== undefined) return String(delta);
  }
  return arg;
}

function lowerV2Collection(statement: V2CollectionStatementAst): StatementAst {
  return {
    kind: "assign",
    target: statement.collection,
    expr: {
      kind: "binary",
      op: statement.op === "clear" ? "-" : "+",
      left: { kind: "identifier", name: statement.collection },
      right: lowerV2Expression(statement.item, statement.line),
    },
    line: statement.line,
  };
}

function lowerV2Reward(statement: V2RewardStatementAst): StatementAst[] {
  return [{
    kind: "assign",
    target: "treasure",
    expr: {
      kind: "binary",
      op: "+",
      left: { kind: "identifier", name: "treasure" },
      right: lowerV2Expression(statement.expr, statement.line),
    },
    line: statement.line,
  }];
}

function lowerV2Expression(text: string, line: number): ExpressionAst {
  const normalized = text
    .trim()
    .replace(/\b([A-Za-z_][\w]*)\.floor\b/gu, "int($1 / 100)")
    .replace(/\binput\.X\b/gu, "0")
    .replace(/\binput\.Y\b/gu, "0");
  return parseExpression(normalized, line);
}

function parseInputSource(text: string): "X" | "Y" | undefined {
  const match = /^input\.(X|Y)$/u.exec(text.trim());
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

function isPreloadLiteralText(text: string): boolean {
  const trimmed = text.trim();
  return (
    isNumericLiteralText(trimmed) ||
    /^[+-]?(?=.*[0-9A-FА-ЕГа-еГг])[\dA-FА-ЕГа-еГгEe.,_+-]+$/u.test(trimmed)
  );
}

function isPreloadCallText(text: string): boolean {
  return /^[A-Za-z_][\w]*(?:\.[A-Za-z_][\w]*)?\([^()]*\)$/u.test(text.trim());
}

function parsePreload(line: SourceLine): PreloadAst {
  const match = /^preload\s+R?([0-9A-Ea-eАВСДЕавсдеXYZTxyz t]+)\s*=\s*(.+)$/u.exec(line.text);
  if (!match) throw new ParseError("Preload must look like 'preload R4 = 2'", line.line);
  const value = match[2]!.trim();
  if (/^[A-Za-z_][\w]*$/u.test(value)) {
    throw new ParseError(
      `Preload value '${value}' looks like a bare name; use a literal or a function call such as '${value}()'`,
      line.line,
    );
  }
  if (!isPreloadLiteralText(value) && !isPreloadCallText(value)) {
    throw new ParseError(
      `Preload value '${value}' must be a calculator literal or an explicit function call`,
      line.line,
    );
  }
  return {
    kind: "preload",
    register: match[1]!.replace(/\s+/gu, ""),
    value,
    line: line.line,
  };
}

function isDomainHeader(text: string): boolean {
  return /^(coord|maze|bitset|resource|event|random|fight|cache\s+search|table|clobber|uses)\b/u.test(text);
}

function parseDomainHeader(line: SourceLine): Omit<DomainAst, "kind" | "lines" | "line"> {
  const text = line.text.endsWith("{") ? line.text.slice(0, -1).trim() : line.text;
  const match = /^(cache\s+search|coord|maze|bitset|resource|event|random|fight|table|clobber|uses)(?:\s+([A-Za-z_][\w]*))?/u.exec(
    text,
  );
  if (!match) throw new ParseError(`Invalid domain declaration '${line.text}'`, line.line);
  const domain: Omit<DomainAst, "kind" | "lines" | "line"> = {
    domainKind: match[1]!.replace(/\s+/gu, "_"),
    header: text,
  };
  if (match[2] !== undefined) domain.name = match[2];
  return domain;
}

function parseFormalAddress(text: string, line: number): number {
  const normalized = text.toUpperCase();
  if (/^A[0-4]$/u.test(normalized)) return 100 + Number(normalized.slice(1));
  if (/^[0-9A-F]{1,2}$/u.test(normalized)) {
    const code = Number.parseInt(normalized, 16);
    const high = code >> 4;
    const low = code & 0x0f;
    if (high <= 9 && low <= 9) return high * 10 + low;
  }
  throw new ParseError(`Invalid formal address '${text}'`, line);
}

function parseAllow(line: SourceLine): AllowFeature[] {
  return line.text
    .slice("allow ".length)
    .split(",")
    .flatMap((part) => part.trim().split(/\s+/u))
    .filter((part) => part.length > 0)
    .map((part) => parseAllowFeature(part, line.line));
}

function parseAllowFeature(text: string, line: number): AllowFeature {
  const normalized = text.replace(/-/gu, "_").toLowerCase();
  const allowed: AllowFeature[] = [
    "undocumented",
    "dark_entries",
    "code_data_overlay",
    "address_constants",
    "display_bytes",
    "x2_clobber",
    "extra_cells",
    "error_stops",
  ];
  if (allowed.includes(normalized as AllowFeature)) return normalized as AllowFeature;
  throw new ParseError(`Unknown allow feature '${text}'`, line);
}

function uniqueAllows(allows: AllowFeature[]): AllowFeature[] {
  return [...new Set(allows)];
}

function parseStateField(line: SourceLine): StateFieldAst {
  const match =
    /^([A-Za-z_][\w]*)\s*:\s*(digit|flag|range|packed|resource|addr)(?:\s+(-?\d+)\.\.(-?\d+))?(?:\s*=\s*(.+))?$/u.exec(
      line.text,
    );
  if (!match) {
    throw new ParseError("State field must look like 'name: digit = 1' or 'name: range 1..3'", line.line);
  }
  const type = match[2] as StateFieldType;
  const field: StateFieldAst = {
    name: match[1]!,
    type,
    line: line.line,
  };
  if (match[3] !== undefined) field.min = Number(match[3]);
  if (match[4] !== undefined) field.max = Number(match[4]);
  if (match[5] !== undefined) field.initial = parseExpression(match[5], line.line);
  return field;
}

function parseIdentifierList(text: string): string[] {
  return text
    .split(",")
    .map((part) => part.trim())
    .filter((part) => part.length > 0);
}

function parseStorageHint(mode?: string, register?: string): StorageHint | undefined {
  if (!mode || !register) return undefined;
  return {
    mode: mode.toLowerCase() as "prefer" | "fixed",
    register: registerFromText(register),
  };
}

function parseSimpleStatement(line: SourceLine): StatementAst {
  const inputAssignment = /^([A-Za-z_][\w]*)\s*=\s*input\s+digit$/u.exec(line.text);
  if (inputAssignment) {
    const statement: InputStatementAst = {
      kind: "input",
      inputType: "digit",
      target: inputAssignment[1]!,
      line: line.line,
    };
    return statement;
  }

  const inputStatement = /^input\s+digit\s+([A-Za-z_][\w]*)$/u.exec(line.text);
  if (inputStatement) {
    const statement: InputStatementAst = {
      kind: "input",
      inputType: "digit",
      target: inputStatement[1]!,
      line: line.line,
    };
    return statement;
  }

  const showStatement = /^(?:show|display)\s+([A-Za-z_][\w]*)$/u.exec(line.text);
  if (showStatement) {
    return { kind: "show", display: showStatement[1]!, line: line.line };
  }

  const callStatement = /^call\s+([A-Za-z_][\w]*)$/u.exec(line.text);
  if (callStatement) {
    const statement: CallBlockStatementAst = {
      kind: "call",
      block: callStatement[1]!,
      line: line.line,
    };
    return statement;
  }

  if (line.text.startsWith("pause ")) {
    const statement: PauseStatementAst = {
      kind: "pause",
      expr: parseExpression(line.text.slice("pause ".length).trim(), line.line),
      line: line.line,
    };
    return statement;
  }

  if (line.text.startsWith("halt ")) {
    const statement: HaltStatementAst = {
      kind: "halt",
      expr: parseExpression(line.text.slice("halt ".length).trim(), line.line),
      line: line.line,
    };
    return statement;
  }

  if (line.text.startsWith("trap ")) {
    return parseTrap(line);
  }

  if (line.text.startsWith("ask ")) {
    const match = /^ask\s+([A-Za-z_][\w]*)(?:\s+from\s+(.+))?$/u.exec(line.text);
    if (!match) throw new ParseError("Invalid ask statement", line.line);
    const statement: AskStatementAst = {
      kind: "ask",
      target: match[1]!,
      line: line.line,
    };
    if (match[2]) statement.prompt = parseExpression(match[2], line.line);
    return statement;
  }

  const askAssignment = /^([A-Za-z_][\w]*)\s*=\s*ask(?:\s+(.+))?$/u.exec(line.text);
  if (askAssignment) {
    const statement: AskStatementAst = {
      kind: "ask",
      target: askAssignment[1]!,
      line: line.line,
    };
    if (askAssignment[2]) statement.prompt = parseExpression(askAssignment[2], line.line);
    return statement;
  }

  const assignment = /^(?:set\s+)?([A-Za-z_][\w]*)\s*=\s*(.+)$/u.exec(line.text);
  if (assignment) {
    const statement: AssignStatementAst = {
      kind: "assign",
      target: assignment[1]!,
      expr: parseExpression(assignment[2]!, line.line),
      line: line.line,
    };
    return statement;
  }

  throw new ParseError(`Unknown statement '${line.text}'`, line.line);
}

function parseTrap(line: SourceLine): TrapStatementAst {
  const match = /^trap\s+(zero|nonpositive|negative|gt_one|ge_100)\s+(.+)$/u.exec(line.text);
  if (!match) {
    throw new ParseError("Trap must look like 'trap zero expr'", line.line);
  }
  return {
    kind: "trap",
    trap: match[1] as TrapStatementAst["trap"],
    expr: parseExpression(match[2]!, line.line),
    line: line.line,
  };
}

function parseCondition(text: string, line: number): ConditionAst {
  const match = /^(.+?)\s*(==|!=|<=|>=|<|>)\s*(.+)$/u.exec(text);
  if (!match) {
    throw new ParseError("Invalid condition", line);
  }
  return {
    left: parseExpression(match[1]!.trim(), line),
    op: match[2] as ConditionAst["op"],
    right: parseExpression(match[3]!.trim(), line),
  };
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
  const slash = text.indexOf("//");
  const hash = text.indexOf("#");
  const cut =
    slash === -1 ? hash : hash === -1 ? slash : Math.min(slash, hash);
  return cut === -1 ? text : text.slice(0, cut);
}
