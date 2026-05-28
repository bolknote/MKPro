"use strict";
var MKProEmulatorBundle = (() => {
  var __defProp = Object.defineProperty;
  var __getOwnPropDesc = Object.getOwnPropertyDescriptor;
  var __getOwnPropNames = Object.getOwnPropertyNames;
  var __hasOwnProp = Object.prototype.hasOwnProperty;
  var __export = (target, all) => {
    for (var name in all)
      __defProp(target, name, { get: all[name], enumerable: true });
  };
  var __copyProps = (to, from, except, desc) => {
    if (from && typeof from === "object" || typeof from === "function") {
      for (let key of __getOwnPropNames(from))
        if (!__hasOwnProp.call(to, key) && key !== except)
          __defProp(to, key, { get: () => from[key], enumerable: !(desc = __getOwnPropDesc(from, key)) || desc.enumerable });
    }
    return to;
  };
  var __toCommonJS = (mod) => __copyProps(__defProp({}, "__esModule", { value: true }), mod);

  // src/browser/emulator-bridge.ts
  var emulator_bridge_exports = {};
  __export(emulator_bridge_exports, {
    compileForBrowser: () => compileForBrowser,
    compileToProgramText: () => compileToProgramText,
    installEmulatorBridge: () => installEmulatorBridge,
    looksLikeMKProSource: () => looksLikeMKProSource
  });

  // src/core/formal-address.ts
  function hexByte(value) {
    return value.toString(16).toUpperCase().padStart(2, "0");
  }
  function assertByte(value) {
    if (!Number.isInteger(value) || value < 0 || value > 255) {
      throw new Error(`Formal MK-61 address byte ${value} is out of range`);
    }
  }
  function formalAddressOrdinal(opcode) {
    assertByte(opcode);
    const high = opcode >> 4;
    const low = opcode & 15;
    return high * 10 + low;
  }
  function officialAddressToOpcode(address) {
    if (!Number.isInteger(address) || address < 0 || address > 104) {
      throw new Error(`Physical MK-61 program address ${address} is outside 00..A4`);
    }
    if (address <= 99) {
      const tens = Math.floor(address / 10);
      const ones = address % 10;
      return tens * 16 + ones;
    }
    return 160 + (address - 100);
  }
  function formatFormalAddressOpcode(opcode) {
    assertByte(opcode);
    return hexByte(opcode);
  }
  function formatOfficialAddress(address) {
    return formatFormalAddressOpcode(officialAddressToOpcode(address));
  }
  function parseFormalAddressOpcode(text) {
    const normalized = text.trim().replace(/[.-]/gu, "A").toUpperCase();
    if (!/^[0-9A-F]{2}$/u.test(normalized)) return void 0;
    return Number.parseInt(normalized, 16);
  }
  function formalAddressInfo(opcode) {
    assertByte(opcode);
    const ordinal = formalAddressOrdinal(opcode);
    const label = formatFormalAddressOpcode(opcode);
    if (ordinal <= 104) {
      return {
        opcode,
        label,
        ordinal,
        actual: ordinal,
        kind: "official",
        oneCommand: false
      };
    }
    if (ordinal <= 111) {
      return {
        opcode,
        label,
        ordinal,
        actual: ordinal - 105,
        kind: "short-side",
        oneCommand: false
      };
    }
    if (ordinal <= 159) {
      return {
        opcode,
        label,
        ordinal,
        actual: ordinal - 112,
        kind: ordinal >= 120 ? "dark" : "long-side",
        oneCommand: false
      };
    }
    if (ordinal <= 165) {
      return {
        opcode,
        label,
        ordinal,
        actual: ordinal - 112,
        kind: "super-dark",
        oneCommand: true,
        extra: ordinal - 159
      };
    }
    throw new Error(`Formal MK-61 address ${label} maps past the known address space`);
  }

  // src/core/opcodes.ts
  var REGISTERS = [
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
    "e"
  ];
  function hex(code) {
    return code.toString(16).toUpperCase().padStart(2, "0");
  }
  function info(code, name, keys = name, extra = {}) {
    return {
      code,
      hex: hex(code),
      name,
      keys,
      enterable: extra.enterable ?? ["manual", "loader", "hex"],
      takesAddress: extra.takesAddress ?? false,
      x2Effect: extra.x2Effect ?? "affects",
      risk: extra.risk ?? "documented"
    };
  }
  function onlyHex() {
    return ["hex"];
  }
  function loaderOrHex() {
    return ["loader", "hex"];
  }
  var opcodeCatalog = buildOpcodeCatalog();
  var opcodeByCode = new Map(
    opcodeCatalog.map((item) => [item.code, item])
  );
  var opcodeByName = /* @__PURE__ */ new Map();
  for (const item of opcodeCatalog) {
    for (const key of normalizeAliases(item)) {
      opcodeByName.set(key, item);
    }
  }
  function getOpcode(code) {
    const item = opcodeByCode.get(code);
    if (!item) {
      throw new Error(`Unknown opcode ${hex(code)}`);
    }
    return item;
  }
  function findOpcodeName(name) {
    return opcodeByName.get(normalizeName(name));
  }
  function registerIndex(register) {
    const index = REGISTERS.indexOf(register);
    if (index === -1) {
      throw new Error(`Unknown register ${register}`);
    }
    return index;
  }
  function registerFromText(text) {
    const normalized = text.trim().toLowerCase();
    const cyrMap = {
      \u0430: "a",
      \u0432: "b",
      \u0441: "c",
      \u0434: "d",
      \u0435: "e"
    };
    const mapped = cyrMap[normalized] ?? normalized;
    if (REGISTERS.includes(mapped)) {
      return mapped;
    }
    throw new Error(`Unknown register ${text}`);
  }
  function addressToOpcode(address) {
    return officialAddressToOpcode(address);
  }
  function formatAddress(address) {
    return formatOfficialAddress(address);
  }
  function buildOpcodeCatalog() {
    const result = [];
    for (let code = 0; code <= 255; code += 1) {
      result.push(
        info(code, `undoc ${hex(code)}`, `undoc ${hex(code)}`, {
          enterable: onlyHex(),
          risk: "undocumented",
          x2Effect: "unknown"
        })
      );
    }
    const set = (item) => {
      result[item.code] = item;
    };
    for (let i = 0; i <= 9; i += 1) set(info(i, String(i), String(i), { x2Effect: "restores" }));
    set(info(10, ".", ".", { x2Effect: "restores" }));
    set(info(11, "/-/", "/-/", { x2Effect: "restores" }));
    set(info(12, "\u0412\u041F", "\u0412\u041F", { x2Effect: "restores" }));
    set(info(13, "Cx", "Cx", { x2Effect: "preserves" }));
    set(info(14, "\u0412\u2191", "\u0412\u2191", { x2Effect: "affects" }));
    set(info(15, "F \u0412x", "F \u0412x", { x2Effect: "affects" }));
    set(info(16, "+"));
    set(info(17, "-"));
    set(info(18, "*"));
    set(info(19, "/"));
    set(info(20, "<->"));
    set(info(21, "F 10^x"));
    set(info(22, "F e^x"));
    set(info(23, "F lg"));
    set(info(24, "F ln"));
    set(info(25, "F sin^-1"));
    set(info(26, "F cos^-1"));
    set(info(27, "F tg^-1"));
    set(info(28, "F sin"));
    set(info(29, "F cos"));
    set(info(30, "F tg"));
    set(info(31, "empty 1F", "1F", { enterable: loaderOrHex(), risk: "undocumented" }));
    set(info(32, "F pi"));
    set(info(33, "F sqrt"));
    set(info(34, "F x^2"));
    set(info(35, "F 1/x"));
    set(info(36, "F x^y"));
    set(info(37, "F reverse"));
    set(info(38, "\u041A \xB0->\u2032"));
    set(info(39, "\u041A -", "\u041A -", { risk: "dangerous" }));
    set(info(40, "\u041A *", "\u041A *", { risk: "dangerous" }));
    set(info(41, "\u041A /", "\u041A /", { risk: "dangerous" }));
    set(info(42, '\u041A \xB0->\u2032"'));
    for (let code = 43; code <= 46; code += 1) {
      set(info(code, `error ${hex(code)}`, hex(code), { enterable: loaderOrHex(), risk: "dangerous" }));
    }
    set(info(47, "empty 2F", "2F", { enterable: loaderOrHex(), risk: "undocumented" }));
    set(info(48, '\u041A \xB0<-\u2032"'));
    set(info(49, "\u041A |x|"));
    set(info(50, "\u041A \u0417\u041D"));
    set(info(51, "\u041A \xB0<-\u2032"));
    set(info(52, "\u041A [x]"));
    set(info(53, "\u041A {x}"));
    set(info(54, "\u041A max"));
    set(info(55, "\u041A \u2227"));
    set(info(56, "\u041A \u2228"));
    set(info(57, "\u041A \u2295"));
    set(info(58, "\u041A \u0418\u041D\u0412"));
    set(info(59, "\u041A \u0421\u0427"));
    set(info(60, "error 3C", "3C", { enterable: loaderOrHex(), risk: "dangerous" }));
    set(info(61, "alias 3D", "3D", { enterable: loaderOrHex(), risk: "undocumented" }));
    set(info(62, "Y->X", "3E", { enterable: loaderOrHex(), risk: "undocumented", x2Effect: "preserves" }));
    set(info(63, "empty 3F", "3F", { enterable: loaderOrHex(), risk: "undocumented" }));
    for (let i = 0; i <= 14; i += 1) {
      const r = REGISTERS[i];
      set(info(64 + i, `X->\u041F ${r}`));
    }
    set(info(79, "X->\u041F 0 alias", "4F", { enterable: loaderOrHex(), risk: "undocumented" }));
    set(info(80, "\u0421/\u041F"));
    set(info(81, "\u0411\u041F", "\u0411\u041F", { takesAddress: true }));
    set(info(82, "\u0412/\u041E"));
    set(info(83, "\u041F\u041F", "\u041F\u041F", { takesAddress: true }));
    set(info(84, "\u041A \u041D\u041E\u041F"));
    set(info(85, "\u041A 1"));
    set(info(86, "\u041A 2"));
    set(info(87, "F x!=0", "F x!=0", { takesAddress: true }));
    set(info(88, "F L2", "F L2", { takesAddress: true }));
    set(info(89, "F x>=0", "F x>=0", { takesAddress: true }));
    set(info(90, "F L3", "F L3", { takesAddress: true }));
    set(info(91, "F L1", "F L1", { takesAddress: true }));
    set(info(92, "F x<0", "F x<0", { takesAddress: true }));
    set(info(93, "F L0", "F L0", { takesAddress: true }));
    set(info(94, "F x=0", "F x=0", { takesAddress: true }));
    set(info(95, "raw display 5F", "5F", { enterable: loaderOrHex(), risk: "undocumented" }));
    for (let i = 0; i <= 14; i += 1) {
      const r = REGISTERS[i];
      set(info(96 + i, `\u041F->X ${r}`));
    }
    set(info(111, "\u041F->X 0 alias", "6F", { enterable: loaderOrHex(), risk: "undocumented" }));
    const indirectBlocks = [
      [112, "\u041A x!=0"],
      [128, "\u041A \u0411\u041F"],
      [144, "\u041A x>=0"],
      [160, "\u041A \u041F\u041F"],
      [176, "\u041A X->\u041F"],
      [192, "\u041A x<0"],
      [208, "\u041A \u041F->X"],
      [224, "\u041A x=0"]
    ];
    for (const [base, name] of indirectBlocks) {
      for (let i = 0; i <= 14; i += 1) {
        const r = REGISTERS[i];
        set(info(base + i, `${name} ${r}`));
      }
      set(info(base + 15, `${name} 0 alias`, hex(base + 15), {
        enterable: loaderOrHex(),
        risk: "undocumented"
      }));
    }
    for (let code = 240; code <= 255; code += 1) {
      set(info(code, `F* empty ${hex(code)}`, hex(code), {
        enterable: loaderOrHex(),
        risk: "undocumented",
        x2Effect: "affects"
      }));
    }
    return result;
  }
  function normalizeAliases(item) {
    const aliases = /* @__PURE__ */ new Set([
      item.name,
      item.keys,
      item.hex,
      item.name.replaceAll("->", "\u2192"),
      item.name.replaceAll("\u041F->X", "\u041F\u0445"),
      item.name.replaceAll("X->\u041F", "\u0445\u041F")
    ]);
    if (item.name === "\u041A \u2227") aliases.add("K\u039B");
    if (item.name === "\u041A \u2227") aliases.add("\u041A AND");
    if (item.name === "\u041A \u2228") aliases.add("KV");
    if (item.name === "\u041A \u2228") aliases.add("\u041A OR");
    if (item.name === "\u041A \u2295") aliases.add("K\u2295");
    if (item.name === "\u041A \u2295") aliases.add("\u041A XOR");
    if (item.name === "\u041A \u0418\u041D\u0412") aliases.add("\u041A\u0438\u043D\u0432");
    if (item.name === "\u041A \u0417\u041D") aliases.add("\u041A\u0437\u043D");
    if (item.name === "\u041A |x|") aliases.add("K|x|");
    if (item.name === "\u041A [x]") aliases.add("K[x]");
    if (item.name === "\u041A {x}") aliases.add("K{x}");
    if (item.name === "F \u0412x") aliases.add("FBx");
    if (item.name === "\u0421/\u041F") aliases.add("STOP");
    if (item.name === "\u0412/\u041E") aliases.add("RTN");
    return [...aliases].map(normalizeName);
  }
  function normalizeName(name) {
    return name.trim().replaceAll("\u2190\u2192", "<->").replaceAll("\u2192", "->").replaceAll("\u2190", "<-").replace(/\^\{([^}]+)\}/gu, "^$1").replaceAll("\u03C0", "pi").replaceAll("\u221A", "sqrt").replaceAll("\u21BB", "reverse").replaceAll("\u2260", "!=").replaceAll("\u2265", ">=").replaceAll("\u2264", "<=").replaceAll("\xD7", "*").replaceAll("\xF7", "/").replaceAll("\u2212", "-").replaceAll("\u2223", "|").replaceAll("\u0425", "X").replaceAll("\u0445", "x").replaceAll("K", "\u041A").replaceAll("k", "\u043A").replace(/^([FfКк])(?=\S)/u, "$1 ").replace(/\s+/g, " ").toLowerCase();
  }

  // src/core/parser.ts
  var V2_RESERVED_RULE_NAMES = /* @__PURE__ */ new Set([
    "challenge",
    "else",
    "if",
    "match",
    "move",
    "otherwise",
    "read",
    "show",
    "stop"
  ]);
  var ParseError = class extends Error {
    line;
    constructor(message, line) {
      super(`${message} at line ${line}`);
      this.line = line;
    }
  };
  function parseProgram(source) {
    return new MKProParser(source).parseProgram();
  }
  var MKProParser = class {
    lines;
    index = 0;
    constructor(source) {
      this.lines = source.split(/\r?\n/u).map((text, offset) => ({ text: stripComment(text).trim(), line: offset + 1 })).filter((line) => line.text.length > 0);
    }
    parseProgram() {
      let reference;
      let v2;
      const preloads = [];
      const domains = [];
      const states = [];
      const displays = [];
      const declarations = [];
      const entries = [];
      const procs = [];
      const blocks = [];
      while (!this.done()) {
        const line = this.peek();
        if (line.text === "}") {
          throw new ParseError("Unexpected closing brace", line.line);
        }
        if (line.text.startsWith("reference ")) {
          reference = line.text.slice("reference ".length).trim();
          this.index += 1;
        } else if (line.text.startsWith("program ")) {
          if (v2 !== void 0) {
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
      if (v2 === void 0) throw new ParseError("Program must contain one V2 program block", 1);
      const program = {
        preloads,
        domains,
        states,
        displays,
        declarations,
        entries,
        procs,
        blocks
      };
      if (reference !== void 0) program.reference = reference;
      if (v2 !== void 0) program.v2 = v2;
      return program;
    }
    parseV2Program() {
      const header = this.next();
      const match = /^program\s+([A-Za-z_][\w]*)\s*\{$/u.exec(header.text);
      if (!match) throw new ParseError("Program must look like 'program Name {'", header.line);
      const state = [];
      const screens = [];
      const boards = [];
      const worlds = [];
      const encounters = [];
      const rules = [];
      let turn;
      while (!this.done()) {
        const line = this.peek();
        if (line.text === "}") {
          this.index += 1;
          const program = {
            kind: "v2_program",
            name: match[1],
            state,
            screens,
            boards,
            worlds,
            encounters,
            rules,
            line: header.line
          };
          if (turn !== void 0) program.turn = turn;
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
        if (board !== void 0) {
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
          if (turn !== void 0) throw new ParseError("Only one turn block is supported", line.line);
          this.index += 1;
          turn = {
            kind: "v2_turn",
            body: this.parseV2StatementBlock(),
            line: line.line
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
    parseV2StateBlock() {
      const fields = [];
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
    parseV2Screen(text) {
      const header = this.next();
      const match = /^screen\s+([A-Za-z_][\w]*)\s*\{$/u.exec(text);
      if (!match) throw new ParseError("Screen must look like 'screen name {'", header.line);
      let sources = [];
      let items = [];
      while (!this.done()) {
        const line = this.next();
        if (line.text === "}") {
          return {
            kind: "v2_screen",
            name: match[1],
            sources,
            items,
            line: header.line
          };
        }
        if (line.text.startsWith("show ")) {
          items = parseDisplayItemList(line.text.slice("show ".length), line.line);
          sources = items.filter((item) => item.kind === "source").map((item) => item.name);
          continue;
        }
        throw new ParseError(`Unexpected screen line '${line.text}'`, line.line);
      }
      throw new ParseError("Unclosed screen block", header.line);
    }
    parseV2World(text) {
      const header = this.next();
      const match = /^world\s+([A-Za-z_][\w]*)\s*\{$/u.exec(text);
      if (!match) throw new ParseError("World must look like 'world name {'", header.line);
      const world = {
        kind: "v2_world",
        name: match[1],
        line: header.line
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
    parseV2WorldPosition(header) {
      const match = /^position\s+([A-Za-z_][\w]*)\s*\{$/u.exec(header.text);
      if (!match) throw new ParseError("World position must look like 'position name {'", header.line);
      const position = { name: match[1], line: header.line };
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
    parseV2Encounters(text) {
      const header = this.next();
      const match = /^encounters\s+(.+?)\s*\{$/u.exec(text);
      if (!match) throw new ParseError("Encounter table must look like 'encounters expr {'", header.line);
      const cases = [];
      while (!this.done()) {
        const line = this.peek();
        if (line.text === "}") {
          this.index += 1;
          return {
            kind: "v2_encounters",
            expr: match[1].trim(),
            cases,
            line: header.line
          };
        }
        const inline = /^(\S+)\s*\{\s*(.*?)\s*\}$/u.exec(line.text);
        if (inline) {
          this.index += 1;
          cases.push({
            value: inline[1],
            body: inline[2].split(";").map((part) => part.trim()).filter(Boolean).map((text2) => parseV2InlineStatement(text2, line.line)),
            line: line.line
          });
          continue;
        }
        const caseMatch = /^(\S+)\s*\{$/u.exec(line.text);
        if (!caseMatch) throw new ParseError("Encounter case must look like 'value {'", line.line);
        this.index += 1;
        cases.push({
          value: caseMatch[1],
          body: this.parseV2StatementBlock(),
          line: line.line
        });
      }
      throw new ParseError("Unclosed encounter table", header.line);
    }
    parseV2Rule(text) {
      const header = this.next();
      const match = /^rule\s+([A-Za-z_][\w]*)(?:\s+(.+?))?\s*\{$/u.exec(text);
      if (!match) throw new ParseError("Rule must look like 'rule name {' or 'rule name arg {'", header.line);
      if (V2_RESERVED_RULE_NAMES.has(match[1])) {
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
        name: match[1],
        params,
        body: this.parseV2StatementBlock(),
        line: header.line
      };
    }
    parseV2StatementBlock() {
      const statements = [];
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
    parseV2Statement() {
      const line = this.peek();
      if (line.text.startsWith("match ") && line.text.endsWith("{")) {
        this.index += 1;
        return this.parseV2Match(line.text, line.line);
      }
      if (line.text.startsWith("if ") && line.text.endsWith("{")) {
        this.index += 1;
        const predicate = parseV2Predicate(line.text.slice("if ".length, -1).trim(), line.line);
        const thenBody = this.parseV2StatementBlock();
        let elseBody;
        const next = this.peekOptional();
        if (next?.text === "else {") {
          this.index += 1;
          elseBody = this.parseV2StatementBlock();
        }
        const statement = {
          kind: "v2_if",
          predicate,
          thenBody,
          line: line.line
        };
        if (elseBody !== void 0) statement.elseBody = elseBody;
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
    parseV2Raw(line) {
      const inputs = [];
      const outputs = [];
      let clobbers;
      let preserves;
      let code;
      while (!this.done()) {
        const bodyLine = this.peek();
        if (bodyLine.text === "}") {
          this.index += 1;
          if (code === void 0) throw new ParseError("Raw block must contain code { ... }", line);
          if (clobbers === void 0) throw new ParseError("Raw block must declare clobbers", line);
          if (preserves === void 0 || !preserves.includes("state")) {
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
            line
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
          if (clobbers !== void 0) throw new ParseError("Raw block must declare clobbers only once", bodyLine.line);
          clobbers = parseV2RawContractList(bodyLine.text.slice("clobbers ".length), bodyLine.line);
          continue;
        }
        if (bodyLine.text.startsWith("preserves ")) {
          this.index += 1;
          if (preserves !== void 0) throw new ParseError("Raw block must declare preserves only once", bodyLine.line);
          preserves = parseV2RawContractList(bodyLine.text.slice("preserves ".length), bodyLine.line);
          continue;
        }
        if (bodyLine.text === "code {") {
          this.index += 1;
          if (code !== void 0) throw new ParseError("Raw block must contain only one code block", bodyLine.line);
          code = this.parseRawCodeBlock(bodyLine.line);
          continue;
        }
        throw new ParseError(`Unexpected raw line '${bodyLine.text}'`, bodyLine.line);
      }
      throw new ParseError("Unclosed raw block", line);
    }
    parseRawCodeBlock(line) {
      const lines = [];
      while (!this.done()) {
        const rawLine = this.next();
        if (rawLine.text === "}") return lines;
        lines.push({ text: rawLine.text, line: rawLine.line });
      }
      throw new ParseError("Unclosed raw code block", line);
    }
    parseV2Challenge(text, line) {
      const match = /^challenge\s+(.+?)\s+as\s+([A-Za-z_][\w]*)\s+using\s+([A-Za-z_][\w]*),\s*([A-Za-z_][\w]*),\s*([A-Za-z_][\w]*)\s*\{$/u.exec(
        text
      );
      if (!match) {
        throw new ParseError("Challenge must look like 'challenge expr as memory_var using warning_screen, memory_screen, answer_input {'", line);
      }
      let successBody;
      let failureBody;
      while (!this.done()) {
        const bodyLine = this.peek();
        if (bodyLine.text === "}") {
          this.index += 1;
          if (!successBody) throw new ParseError("Challenge block must contain success { ... }", line);
          const statement = {
            kind: "v2_challenge",
            expr: match[1].trim(),
            successBody,
            challengeTarget: match[2],
            warningScreen: match[3],
            memoryScreen: match[4],
            answerInput: match[5],
            line
          };
          if (failureBody !== void 0) statement.failureBody = failureBody;
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
    parseV2Match(text, line) {
      const match = /^match\s+(.+?)\s*\{$/u.exec(text);
      if (!match) throw new ParseError("Match must look like 'match expr {'", line);
      const cases = [];
      let otherwise;
      while (!this.done()) {
        const bodyLine = this.next();
        if (bodyLine.text === "}") {
          const statement = {
            kind: "v2_match",
            expr: match[1].trim(),
            cases,
            line
          };
          if (otherwise !== void 0) statement.otherwise = otherwise;
          return statement;
        }
        const arrow = /^(.+?)\s*=>\s*(.+)$/u.exec(bodyLine.text);
        if (!arrow) throw new ParseError("Match cases must look like 'value => action'", bodyLine.line);
        const left = arrow[1].trim();
        const action = parseV2InlineStatement(arrow[2].trim(), bodyLine.line);
        if (left === "otherwise") {
          otherwise = action;
        } else {
          cases.push({
            values: left.split(",").map((part) => part.trim()).filter(Boolean),
            action,
            line: bodyLine.line
          });
        }
      }
      throw new ParseError("Unclosed match block", line);
    }
    done() {
      return this.index >= this.lines.length;
    }
    peek() {
      const line = this.lines[this.index];
      if (!line) throw new ParseError("Unexpected end of file", this.lines.at(-1)?.line ?? 1);
      return line;
    }
    peekOptional() {
      return this.lines[this.index];
    }
    next() {
      const line = this.peek();
      this.index += 1;
      return line;
    }
  };
  function parseV2StateField(line) {
    const match = /^([A-Za-z_][\w]*)\s*:\s*([A-Za-z_][\w]*)(?:\(([^)]*)\))?(.*)$/u.exec(line.text);
    if (!match) {
      throw new ParseError("State field must look like 'name: counter 0..9 = 0' or 'name: cells(domain) = random()'", line.line);
    }
    const typeText = match[2].toLowerCase();
    if (!["flag", "counter", "coord", "cells", "packed"].includes(typeText)) {
      throw new ParseError(`Unknown state type '${match[2]}'`, line.line);
    }
    const args = match[3]?.trim();
    const argList = args === void 0 ? [] : splitArgs(args);
    if (typeText === "cells" && (!args || argList.length !== 1)) {
      throw new ParseError("cells state must look like 'name: cells(domain) = random()'", line.line);
    }
    if (typeText === "coord" && (!args || argList.length !== 1)) {
      throw new ParseError("coord state must look like 'name: coord(domain)'", line.line);
    }
    if (typeText !== "cells" && typeText !== "coord" && args !== void 0) {
      throw new ParseError(`State type '${match[2]}' does not take parameters`, line.line);
    }
    const tail = match[4].trim();
    const tailMatch = /^(?:(-?\d+)\.\.(-?\d+))?(?:\s*=\s*(.+))?$/u.exec(tail);
    if (!tailMatch) {
      throw new ParseError("State field must look like 'name: counter 0..9 = 0' or 'name: cells(domain) = random()'", line.line);
    }
    if (typeText === "counter" && tailMatch[1] === void 0) {
      throw new ParseError("counter state must look like 'name: counter 0..9 = 0'", line.line);
    }
    if (typeText !== "counter" && tailMatch[1] !== void 0) {
      throw new ParseError(`State type '${match[2]}' does not take a numeric range`, line.line);
    }
    const field = {
      kind: "v2_state_field",
      name: match[1],
      type: typeText,
      line: line.line
    };
    if (typeText === "cells" || typeText === "coord") {
      field.domain = argList[0];
    }
    if (tailMatch[1] !== void 0) {
      field.min = Number(tailMatch[1]);
      field.max = Number(tailMatch[2]);
    }
    if (tailMatch[3] !== void 0) field.initial = tailMatch[3].trim();
    return field;
  }
  function parseV2BoardDeclaration(line) {
    const match = /^([A-Za-z_][\w]*)\s*:\s*board\(\s*(-?\d+)\.\.(-?\d+)\s*,\s*(-?\d+)\.\.(-?\d+)\s*\)$/u.exec(line.text);
    if (!match) return void 0;
    const xMin = Number(match[2]);
    const xMax = Number(match[3]);
    const yMin = Number(match[4]);
    const yMax = Number(match[5]);
    if (xMin > xMax || yMin > yMax) {
      throw new ParseError("Board ranges must be ascending", line.line);
    }
    return {
      kind: "v2_board",
      name: match[1],
      xMin,
      xMax,
      yMin,
      yMax,
      width: xMax - xMin + 1,
      height: yMax - yMin + 1,
      line: line.line
    };
  }
  function parseV2InlineStatement(text, line) {
    if (text.startsWith("show ")) {
      return { kind: "v2_show", target: text.slice("show ".length).trim(), line };
    }
    const read = /^read\s+([A-Za-z_][\w]*)$/u.exec(text);
    if (read) {
      return { kind: "v2_read", target: read[1], line };
    }
    if (text.startsWith("read ")) {
      throw new ParseError("Read must look like 'read name'", line);
    }
    if (text.startsWith("stop ")) {
      return { kind: "v2_stop", value: text.slice("stop ".length).trim(), line };
    }
    const move = /^move\s+([A-Za-z_][\w]*)\s+(north|south|east|west|up|down)$/u.exec(text);
    if (move) {
      const statement = {
        kind: "v2_move",
        target: move[1],
        line
      };
      if (move[2] !== void 0) statement.direction = parseV2MoveDirection(move[2], line);
      return statement;
    }
    if (text.startsWith("move ")) {
      throw new ParseError("Move must look like 'move pos north'. Use 'pos += expr' for computed movement", line);
    }
    const step = /^([A-Za-z_][\w]*)\s*(\+\+|--)$/u.exec(text);
    if (step) {
      return {
        kind: "v2_update",
        target: step[1],
        op: step[2] === "++" ? "+=" : "-=",
        expr: "1",
        line
      };
    }
    const update = /^([A-Za-z_][\w]*)\s*(\+=|-=)\s*(.+)$/u.exec(text);
    if (update) {
      return {
        kind: "v2_update",
        target: update[1],
        op: update[2],
        expr: update[3].trim(),
        line
      };
    }
    const assignment = /^([A-Za-z_][\w]*)\s*=\s*(.+)$/u.exec(text);
    if (assignment) {
      return {
        kind: "v2_assign",
        target: assignment[1],
        expr: assignment[2].trim(),
        line
      };
    }
    const invoke = /^([A-Za-z_][\w]*)(?:\s+(.+))?$/u.exec(text);
    if (invoke) {
      return {
        kind: "v2_invoke",
        name: invoke[1],
        args: invoke[2] ? splitArgs(invoke[2]) : [],
        line
      };
    }
    throw new ParseError(`Unexpected statement '${text}'`, line);
  }
  function parseV2Predicate(text, line) {
    const contains = /^(.+?)\s+in\s+([A-Za-z_][\w]*)$/u.exec(text);
    if (contains) {
      return {
        kind: "v2_contains",
        collection: contains[2].trim(),
        item: contains[1].trim()
      };
    }
    const compare = /^(.+?)\s*(==|!=|<=|>=|<|>)\s*(.+)$/u.exec(text);
    if (compare) {
      return {
        kind: "v2_compare",
        left: compare[1].trim(),
        op: compare[2],
        right: compare[3].trim()
      };
    }
    throw new ParseError(`Predicate must look like 'left op right'`, line);
  }
  function parseV2RawInputs(text, line) {
    return splitArgs(text).map((part) => {
      const match = /^(X|Y|Z|T)\s*=\s*(.+)$/iu.exec(part);
      if (!match) throw new ParseError("Raw input must look like 'takes X = expr'", line);
      return {
        slot: match[1].toUpperCase(),
        expr: match[2].trim(),
        line
      };
    });
  }
  function parseV2RawOutput(text, line) {
    const explicit = /^X\s*->\s*([A-Za-z_][\w]*)$/iu.exec(text.trim());
    const shorthand = /^([A-Za-z_][\w]*)$/u.exec(text.trim());
    const target = explicit?.[1] ?? shorthand?.[1];
    if (target === void 0) throw new ParseError("Raw output must look like 'returns X -> name'", line);
    return { slot: "X", target, line };
  }
  function parseV2RawContractList(text, line) {
    const values = parseIdentifierList(text).map((item) => normalizeV2RawContractItem(item, line));
    if (values.length === 0) throw new ParseError("Raw contract list must not be empty", line);
    if (values.includes("none") && values.length > 1) {
      throw new ParseError("Raw contract item 'none' cannot be combined with other items", line);
    }
    return [...new Set(values)];
  }
  function normalizeV2RawContractItem(text, line) {
    const trimmed = text.trim();
    if (/^(none|state|stack|display|flags|memory)$/iu.test(trimmed)) return trimmed.toLowerCase();
    if (/^(X|Y|Z|T|X1)$/iu.test(trimmed)) return trimmed.toUpperCase();
    const register = /^R?([0-9a-eавсде])$/iu.exec(trimmed);
    if (register) return `R${register[1].toLowerCase()}`;
    throw new ParseError(`Unknown raw contract item '${text}'`, line);
  }
  function validateV2RawInputs(inputs, line) {
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
  function splitArgs(text) {
    const args = [];
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
  function lowerV2Program(v2) {
    const screens = collectV2Screens(v2);
    const ruleParams = collectV2RuleParams(v2);
    const rules = collectV2Rules(v2);
    const specializedRules = selectV2RuleSpecializations(v2, rules);
    const stateDomains = new Map(
      v2.state.filter((field) => field.domain !== void 0).map((field) => [field.name, field.domain])
    );
    const cellMapNames = collectV2CellMapNames(v2, stateDomains);
    validateV2Domains(v2);
    validateV2References(v2, { screens, ruleParams });
    const context = {
      ruleParams,
      rules,
      specializedRules,
      moveDeltas: collectV2MoveDeltas(v2),
      stateTypes: new Map(v2.state.map((field) => [field.name, field.type])),
      stateDomains,
      cellMapNames,
      boards: new Map(v2.boards.map((board) => [board.name, board])),
      worlds: new Map(v2.worlds.map((world) => [world.name, world]))
    };
    return {
      domains: lowerV2Domains(v2),
      states: lowerV2State(v2, specializedRules, cellMapNames, context),
      displays: v2.screens.map(lowerV2Screen),
      entries: [lowerV2Entry(v2, context)],
      procs: [
        ...v2.rules.filter((rule) => !specializedRules.has(rule.name)).map((rule) => lowerV2Rule(rule, context)),
        ...lowerV2EncounterRules(v2, context)
      ],
      blocks: []
    };
  }
  function collectV2RuleParams(v2) {
    const ruleParams = /* @__PURE__ */ new Map();
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
  function collectV2Rules(v2) {
    return new Map(v2.rules.map((rule) => [rule.name, rule]));
  }
  function selectV2RuleSpecializations(v2, rules) {
    const invocations = collectV2Invocations(v2);
    const selected = /* @__PURE__ */ new Set();
    for (const rule of v2.rules) {
      if (rule.params.length === 0 || !isSpecializableRuleBody(rule)) continue;
      const sites = invocations.filter((site) => site.statement.name === rule.name);
      if (sites.length < 2) continue;
      if (sites.some((site) => site.currentRule === rule.name)) continue;
      if (!sites.every((site) => site.statement.args.length === rule.params.length && site.statement.args.every(isSpecializationArg))) {
        continue;
      }
      const genericCost = estimateV2Statements(rule.body) + 1 + sites.reduce((sum, site) => sum + estimateV2InvokeSetupCost(site.statement) + 2, 0);
      const inlineCost = sites.reduce((sum, site) => {
        const replacements = invokeReplacements(rule, site.statement.args);
        return sum + estimateV2Statements(rule.body, replacements);
      }, 0);
      if (inlineCost < genericCost && rules.has(rule.name)) selected.add(rule.name);
    }
    return selected;
  }
  function collectV2Invocations(v2) {
    const sites = [];
    const visit = (statements, currentRule) => {
      for (const statement of statements) {
        if (statement.kind === "v2_invoke") {
          const site = { statement };
          if (currentRule !== void 0) site.currentRule = currentRule;
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
    if (v2.turn !== void 0) visit(v2.turn.body);
    for (const rule of v2.rules) visit(rule.body, rule.name);
    for (const table of v2.encounters) {
      for (const encounterCase of table.cases) visit(encounterCase.body);
    }
    return sites;
  }
  function isSpecializableRuleBody(rule) {
    const params = new Set(rule.params);
    return rule.body.length > 0 && rule.body.every((statement) => {
      if (statement.kind !== "v2_assign" && statement.kind !== "v2_update") return false;
      return !params.has(statement.target);
    });
  }
  function isSpecializationArg(arg) {
    return isNumericLiteralText(arg.trim());
  }
  function invokeReplacements(rule, args) {
    const replacements = /* @__PURE__ */ new Map();
    for (let index = 0; index < rule.params.length; index += 1) {
      replacements.set(rule.params[index], args[index].trim());
    }
    return replacements;
  }
  function estimateV2InvokeSetupCost(statement) {
    return statement.args.reduce((sum, arg) => sum + estimateV2ExpressionText(arg) + 1, 0);
  }
  function estimateV2Statements(statements, replacements = /* @__PURE__ */ new Map()) {
    return statements.reduce((sum, statement) => sum + estimateV2Statement(statement, replacements), 0);
  }
  function estimateV2Statement(statement, replacements) {
    switch (statement.kind) {
      case "v2_assign":
        return estimateV2ExpressionText(substituteV2Text(statement.expr, replacements)) + 1;
      case "v2_update":
        return estimateV2ExpressionText(statement.target) + estimateV2ExpressionText(substituteV2Text(statement.expr, replacements)) + 2;
      default:
        return 99;
    }
  }
  function estimateV2ExpressionText(text) {
    const trimmed = text.trim();
    if (isNumericLiteralText(trimmed) || /^[A-Za-z_][\w]*$/u.test(trimmed)) return 1;
    const operators = trimmed.match(/[+\-*/]/gu)?.length ?? 0;
    const calls = trimmed.match(/[A-Za-z_][\w]*\s*\(/gu)?.length ?? 0;
    return 1 + operators + calls + Math.ceil(trimmed.length / 12);
  }
  function collectV2Screens(v2) {
    const screens = /* @__PURE__ */ new Map();
    for (const screen of v2.screens) {
      if (screens.has(screen.name)) throw new ParseError(`Duplicate screen '${screen.name}'`, screen.line);
      screens.set(screen.name, screen);
    }
    return screens;
  }
  function validateV2Domains(v2) {
    const domains = /* @__PURE__ */ new Map();
    for (const board of v2.boards) {
      if (domains.has(board.name)) throw new ParseError(`Duplicate domain '${board.name}'`, board.line);
      domains.set(board.name, board.line);
    }
    for (const world of v2.worlds) {
      if (domains.has(world.name)) throw new ParseError(`Duplicate domain '${world.name}'`, world.line);
      domains.set(world.name, world.line);
    }
    for (const field of v2.state) {
      if ((field.type === "coord" || field.type === "cells") && field.domain !== void 0 && !domains.has(field.domain)) {
        throw new ParseError(`Unknown domain '${field.domain}'`, field.line);
      }
    }
  }
  function validateV2References(v2, context) {
    const visit = (statements) => {
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
  function validateV2Statement(statement, context, visit) {
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
          const expected = context.ruleParams.get(statement.name).length;
          if (statement.args.length !== expected) {
            throw new ParseError(
              `Rule '${statement.name}' expects ${expected} argument${expected === 1 ? "" : "s"}, got ${statement.args.length}`,
              statement.line
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
  function collectV2MoveDeltas(v2) {
    const deltas = /* @__PURE__ */ new Map();
    for (const world of v2.worlds) {
      if (world.position === void 0) continue;
      deltas.set(world.position.name, moveDeltasForEncoding(world.position.encoding));
    }
    return deltas;
  }
  function collectV2CellMapNames(v2, stateDomains) {
    const explicit = v2.state.find((field) => field.type === "packed" && /^(?:plan|map)$/iu.test(field.name));
    const byDomain = /* @__PURE__ */ new Map();
    const addDomain = (domain) => {
      if (byDomain.has(domain)) return;
      const domainSpecific = v2.state.find(
        (field) => field.type === "packed" && new RegExp(`^${escapeRegExp(domain)}_(?:plan|map)$`, "iu").test(field.name)
      );
      byDomain.set(domain, domainSpecific?.name ?? explicit?.name ?? `__cell_map_${domain}`);
    };
    for (const text of collectV2ExpressionTexts(v2)) {
      for (const args of findSpatialCalls(text, "cell_at")) {
        if (args.length === 2) {
          addDomain(args[0]);
          continue;
        }
        if (args.length === 1) {
          const domain = stateDomains.get(args[0]);
          if (domain !== void 0) addDomain(domain);
        }
      }
    }
    return byDomain;
  }
  function collectV2ExpressionTexts(v2) {
    const texts = [];
    const visit = (statements) => {
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
            if (statement.elseBody !== void 0) visit(statement.elseBody);
            break;
          case "v2_challenge":
            texts.push(statement.expr);
            visit(statement.successBody);
            if (statement.failureBody !== void 0) visit(statement.failureBody);
            break;
          case "v2_match":
            texts.push(statement.expr);
            for (const matchCase of statement.cases) {
              texts.push(...matchCase.values);
              visit([matchCase.action]);
            }
            if (statement.otherwise !== void 0) visit([statement.otherwise]);
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
    if (v2.turn !== void 0) visit(v2.turn.body);
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
  function moveDeltasForEncoding(encoding) {
    if (encoding === "packed_decimal_zero_run") {
      return {
        south: "0.0000002",
        north: "-0.0000002",
        west: "0.000001",
        east: "-0.000001",
        up: "1",
        down: "-1"
      };
    }
    return {
      south: "1",
      north: "-1",
      east: "10",
      west: "-10",
      up: "100",
      down: "-100"
    };
  }
  function lowerV2Domains(v2) {
    const domains = [];
    for (const board of v2.boards) {
      const lines = [
        { text: `x ${board.xMin}..${board.xMax}`, line: board.line },
        { text: `y ${board.yMin}..${board.yMax}`, line: board.line },
        { text: `columns ${board.width}`, line: board.line },
        { text: `rows ${board.height}`, line: board.line }
      ];
      domains.push({
        kind: "domain",
        domainKind: "maze",
        name: board.name,
        header: `board ${board.name}`,
        lines,
        line: board.line
      });
    }
    for (const world of v2.worlds) {
      if (world.position !== void 0) {
        const lines = [];
        if (world.position.encoding !== void 0) lines.push({ text: `encoding ${world.position.encoding}`, line: world.position.line });
        domains.push({
          kind: "domain",
          domainKind: "coord",
          name: "packed",
          header: `coord packed ${world.position.name}`,
          lines,
          line: world.position.line
        });
      }
      const worldLines = [];
      domains.push({
        kind: "domain",
        domainKind: "maze",
        name: world.name,
        header: `maze ${world.name}`,
        lines: worldLines,
        line: world.line
      });
    }
    for (const field of v2.state) {
      if (field.type === "cells") {
        const lines = [];
        if (field.domain !== void 0) lines.push({ text: `domain ${field.domain}`, line: field.line });
        domains.push({
          kind: "domain",
          domainKind: "bitset",
          name: field.name,
          header: `cells ${field.name}`,
          lines,
          line: field.line
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
          line: encounterCase.line
        }))),
        line: v2.encounters[0].line
      });
    }
    return domains;
  }
  function lowerV2State(v2, specializedRules, cellMapNames, context) {
    const fields = [];
    for (const field of v2.state) {
      const lowered = {
        name: field.name,
        type: lowerV2StateFieldType(field.type),
        line: field.line
      };
      if (field.min !== void 0) lowered.min = field.min;
      if (field.max !== void 0) lowered.max = field.max;
      if (field.initial !== void 0) {
        const stackSource = parseStackSource(field.initial, field.line);
        if (stackSource !== void 0) {
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
        line: v2.line
      });
    }
    return fields.length > 0 ? [{ kind: "state", name: v2.name, fields, line: v2.line }] : [];
  }
  function collectV2ScratchFields(v2, specializedRules) {
    const fields = [];
    const add = (name, line) => {
      fields.push({ name, type: "packed", line });
    };
    for (const rule of v2.rules) {
      if (!specializedRules.has(rule.name)) {
        for (const param of rule.params) add(param, rule.line);
      }
      visit(rule.body);
    }
    if (v2.turn !== void 0) visit(v2.turn.body);
    for (const table of v2.encounters) {
      add(encounterParamName(table.expr), table.line);
      for (const encounterCase of table.cases) visit(encounterCase.body);
    }
    return fields;
    function visit(statements) {
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
  function lowerV2InitialExpression(field, context) {
    const initial = field.initial ?? "0";
    if (field.type === "cells" && initial.trim() === "random()") {
      return lowerV2Expression("int(random() * 999)", field.line);
    }
    return lowerV2Expression(initial, field.line, context);
  }
  function lowerV2StateFieldType(type) {
    if (type === "counter") return "range";
    if (type === "coord" || type === "cells") return "packed";
    return type;
  }
  function lowerV2Screen(screen) {
    return {
      kind: "display",
      name: screen.name,
      format: "packed",
      sources: screen.sources,
      items: screen.items,
      line: screen.line
    };
  }
  function lowerV2Entry(v2, context) {
    return {
      kind: "entry",
      name: "main",
      body: v2.turn ? [{ kind: "loop", body: lowerV2Statements(v2.turn.body, context), line: v2.turn.line }] : [{ kind: "halt", expr: parseExpression("0"), line: v2.line }],
      line: v2.turn?.line ?? v2.line
    };
  }
  function lowerV2Rule(rule, context) {
    return {
      kind: "proc",
      name: rule.name,
      body: lowerV2Statements(rule.body, context),
      line: rule.line
    };
  }
  function lowerV2EncounterRules(v2, context) {
    return v2.encounters.map((table) => ({
      kind: "proc",
      name: "encounter",
      body: [lowerV2EncounterDispatch(table, context)],
      line: table.line
    }));
  }
  function lowerV2EncounterDispatch(table, context) {
    return {
      kind: "dispatch",
      expr: lowerV2Expression(encounterParamName(table.expr), table.line, context),
      cases: table.cases.map((encounterCase) => ({
        value: lowerV2Expression(encounterCase.value, encounterCase.line, context),
        body: lowerV2Statements(encounterCase.body, context),
        line: encounterCase.line
      })),
      line: table.line,
      scratchId: table.line
    };
  }
  function encounterParamName(expr) {
    return /^[A-Za-z_][\w]*$/u.test(expr.trim()) ? expr.trim() : "encounter_kind";
  }
  function lowerV2Statements(statements, context) {
    const lowered = [];
    for (let index = 0; index < statements.length; index += 1) {
      const statement = statements[index];
      lowered.push(...lowerV2Statement(statement, context));
    }
    return lowered;
  }
  function lowerV2Statement(statement, context) {
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
          line: statement.line
        }];
      case "v2_stop":
        return [{ kind: "halt", expr: lowerV2Expression(statement.value, statement.line, context), line: statement.line }];
      case "v2_invoke":
        return lowerV2Invoke(statement, context);
      case "v2_if": {
        const condition = lowerV2Predicate(statement.predicate, statement.line, context);
        const lowered = {
          kind: "if",
          condition,
          thenBody: lowerV2Statements(statement.thenBody, context),
          line: statement.line
        };
        if (statement.elseBody !== void 0) lowered.elseBody = lowerV2Statements(statement.elseBody, context);
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
            line: statement.line
          }];
        }
        return [{
          kind: "assign",
          target: statement.target,
          expr: {
            kind: "binary",
            op: statement.op === "+=" ? "+" : "-",
            left: { kind: "identifier", name: statement.target },
            right: lowerV2Expression(statement.expr, statement.line, context)
          },
          line: statement.line
        }];
      }
      case "v2_raw":
        return [{
          kind: "core",
          inputs: statement.inputs.map((input) => ({
            slot: input.slot,
            expr: lowerV2Expression(input.expr, input.line, context),
            line: input.line
          })),
          outputs: statement.outputs.map((output) => ({
            slot: output.slot,
            target: output.target,
            line: output.line
          })),
          clobbers: statement.clobbers,
          preserves: statement.preserves,
          lines: statement.lines,
          strict: true,
          line: statement.line
        }];
      case "v2_match":
        return [lowerV2Match(statement, context)];
    }
  }
  function lowerV2Predicate(predicate, line, context) {
    if (predicate.kind === "v2_contains") {
      return {
        left: lowerV2Expression(cellMembershipExpression(predicate.collection, predicate.item, context), line, context),
        op: "!=",
        right: lowerV2Expression("0", line, context)
      };
    }
    return {
      left: lowerV2Expression(predicate.left, line, context),
      op: predicate.op,
      right: lowerV2Expression(predicate.right, line, context)
    };
  }
  function cellSetUpdateExpression(collection, item, op, context) {
    const mask = cellMaskExpressionForCollection(collection, item, context);
    if (mask === void 0) return `${op === "+=" ? "bit_set" : "bit_clear"}(${collection}, ${item})`;
    return op === "+=" ? `bit_or(${collection}, ${mask})` : `bit_and(${collection}, bit_not(${mask}))`;
  }
  function cellMembershipExpression(collection, item, context) {
    const mask = cellMaskExpressionForCollection(collection, item, context);
    return mask === void 0 ? `bit_has(${collection}, ${item})` : `bit_and(${collection}, ${mask})`;
  }
  function lowerV2Challenge(statement, context) {
    const failureBody = statement.failureBody ?? [{ kind: "v2_show", target: "0", line: statement.line }];
    return [
      {
        kind: "assign",
        target: statement.challengeTarget,
        expr: lowerV2Expression(statement.expr, statement.line, context),
        line: statement.line
      },
      { kind: "show", display: statement.warningScreen, line: statement.line },
      { kind: "show", display: statement.memoryScreen, line: statement.line },
      {
        kind: "input",
        target: statement.answerInput,
        line: statement.line
      },
      {
        kind: "if",
        condition: {
          left: { kind: "identifier", name: statement.answerInput },
          op: "==",
          right: { kind: "identifier", name: statement.challengeTarget }
        },
        thenBody: lowerV2Statements(statement.successBody, context),
        elseBody: lowerV2Statements(failureBody, context),
        line: statement.line
      }
    ];
  }
  function lowerV2Move(statement, context) {
    const delta = namedMoveDelta(statement.target, statement.direction, statement.line, context);
    const move = {
      kind: "assign",
      target: statement.target,
      expr: {
        kind: "binary",
        op: "+",
        left: { kind: "identifier", name: statement.target },
        right: lowerV2Expression(delta, statement.line, context)
      },
      line: statement.line
    };
    return [move];
  }
  function namedMoveDelta(target, direction, line, context) {
    if (direction === void 0) throw new ParseError("Move must specify a direction", line);
    return context.moveDeltas.get(target)?.[direction] ?? moveDeltasForEncoding(void 0)[direction] ?? "0";
  }
  function parseV2MoveDirection(text, line) {
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
  function lowerV2Match(statement, context) {
    const compact = lowerCompactDirectionDispatch(statement, context);
    if (compact !== void 0) return compact;
    const cases = [];
    for (const matchCase of statement.cases) {
      for (const value of matchCase.values) {
        cases.push({
          value: lowerV2Expression(value, matchCase.line, context),
          body: lowerV2MatchAction(matchCase.action, context, statement.expr, value, matchCase.line),
          line: matchCase.line
        });
      }
    }
    const lowered = {
      kind: "dispatch",
      expr: lowerV2Expression(statement.expr, statement.line, context),
      cases,
      line: statement.line,
      scratchId: statement.line
    };
    if (statement.otherwise !== void 0) lowered.defaultBody = lowerV2Statement(statement.otherwise, context);
    return lowered;
  }
  function lowerCompactDirectionDispatch(statement, context) {
    const directionalCases = statement.cases.filter((matchCase) => isDirectionInvoke(matchCase.action, statement.expr));
    const directionalValueCount = directionalCases.reduce((sum, matchCase) => sum + matchCase.values.length, 0);
    if (directionalValueCount < 3) return void 0;
    const action = directionalCases[0].action;
    if (action.kind !== "v2_invoke") return void 0;
    if (!directionalCases.every((matchCase) => sameInvoke(matchCase.action, action))) return void 0;
    const params = context.ruleParams.get(action.name) ?? [];
    if (params.length === 0) return void 0;
    const needsGuard = statement.otherwise === void 0 || !isDirectionInvoke(statement.otherwise, statement.expr);
    if (needsGuard && directionalValueCount < 5) return void 0;
    const cases = [];
    for (const matchCase of statement.cases) {
      if (directionalCases.includes(matchCase)) continue;
      for (const value of matchCase.values) {
        cases.push({
          value: lowerV2Expression(value, matchCase.line, context),
          body: lowerV2MatchAction(matchCase.action, context, statement.expr, value, matchCase.line),
          line: matchCase.line
        });
      }
    }
    const directionBody = [
      {
        kind: "assign",
        target: params[0],
        expr: lowerV2Expression(`direction(${statement.expr})`, statement.line, context),
        line: statement.line
      },
      { kind: "call", block: action.name, line: action.line }
    ];
    const defaultBody = needsGuard ? [{
      kind: "if",
      condition: {
        left: lowerV2Expression(directionKeyGuardExpression(statement.expr), statement.line, context),
        op: "==",
        right: lowerV2Expression("0", statement.line, context)
      },
      thenBody: directionBody,
      ...statement.otherwise === void 0 ? {} : { elseBody: lowerV2Statement(statement.otherwise, context) },
      line: statement.line
    }] : directionBody;
    return {
      kind: "dispatch",
      expr: lowerV2Expression(statement.expr, statement.line, context),
      name: "direction_dispatch",
      cases,
      defaultBody,
      line: statement.line,
      scratchId: statement.line
    };
  }
  function directionKeyGuardExpression(expr) {
    const key = `(${expr})`;
    const cardinal = `(abs(${key} - 5) - 1) * (abs(${key} - 5) - 3)`;
    const vertical = `abs(${key}) - 5`;
    return `(${cardinal}) * (${vertical})`;
  }
  function isDirectionInvoke(action, matchExpr) {
    if (action.kind !== "v2_invoke") return false;
    return action.args.some((arg) => {
      const direction = /^direction\((.+)\)$/u.exec(arg.trim());
      return direction?.[1]?.trim() === matchExpr.trim();
    });
  }
  function sameInvoke(action, expected) {
    return action.kind === "v2_invoke" && action.name === expected.name && action.args.join(",") === expected.args.join(",");
  }
  function lowerV2MatchAction(action, context, matchExpr, matchValue, line) {
    if (action.kind !== "v2_invoke") return lowerV2Statement(action, context);
    const rule = context.rules.get(action.name);
    if (rule !== void 0 && context.specializedRules.has(rule.name)) {
      const args = action.args.map((arg) => resolveV2InvokeArg(arg, matchExpr, matchValue));
      return lowerV2Statements(substituteV2Statements(rule.body, invokeReplacements(rule, args)), context);
    }
    const params = context.ruleParams.get(action.name) ?? [];
    const statements = [];
    for (let index = 0; index < Math.min(params.length, action.args.length); index += 1) {
      const arg = resolveV2InvokeArg(action.args[index], matchExpr, matchValue);
      statements.push({
        kind: "assign",
        target: params[index],
        expr: lowerV2Expression(arg, line, context),
        line
      });
    }
    statements.push({ kind: "call", block: action.name, line: action.line });
    return statements;
  }
  function lowerV2Invoke(statement, context) {
    const rule = context.rules.get(statement.name);
    if (rule !== void 0 && context.specializedRules.has(rule.name)) {
      return lowerV2Statements(substituteV2Statements(rule.body, invokeReplacements(rule, statement.args)), context);
    }
    const params = context.ruleParams.get(statement.name) ?? [];
    const statements = [];
    for (let index = 0; index < Math.min(params.length, statement.args.length); index += 1) {
      statements.push({
        kind: "assign",
        target: params[index],
        expr: lowerV2Expression(statement.args[index], statement.line, context),
        line: statement.line
      });
    }
    statements.push({ kind: "call", block: statement.name, line: statement.line });
    return statements;
  }
  function substituteV2Statements(statements, replacements) {
    return statements.map((statement) => substituteV2Statement(statement, replacements));
  }
  function substituteV2Statement(statement, replacements) {
    switch (statement.kind) {
      case "v2_assign":
        return { ...statement, expr: substituteV2Text(statement.expr, replacements) };
      case "v2_update":
        return { ...statement, expr: substituteV2Text(statement.expr, replacements) };
      case "v2_if": {
        const substituted = {
          ...statement,
          predicate: substituteV2Predicate(statement.predicate, replacements),
          thenBody: substituteV2Statements(statement.thenBody, replacements)
        };
        if (statement.elseBody !== void 0) {
          substituted.elseBody = substituteV2Statements(statement.elseBody, replacements);
        }
        return substituted;
      }
      case "v2_challenge": {
        const substituted = {
          ...statement,
          expr: substituteV2Text(statement.expr, replacements),
          successBody: substituteV2Statements(statement.successBody, replacements)
        };
        if (statement.failureBody !== void 0) {
          substituted.failureBody = substituteV2Statements(statement.failureBody, replacements);
        }
        return substituted;
      }
      case "v2_move":
        return statement;
      case "v2_match": {
        const substituted = {
          ...statement,
          expr: substituteV2Text(statement.expr, replacements),
          cases: statement.cases.map((matchCase) => ({
            ...matchCase,
            values: matchCase.values.map((value) => substituteV2Text(value, replacements)),
            action: substituteV2Statement(matchCase.action, replacements)
          }))
        };
        if (statement.otherwise !== void 0) {
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
  function substituteV2Predicate(predicate, replacements) {
    if (predicate.kind === "v2_contains") {
      return {
        ...predicate,
        collection: substituteV2Text(predicate.collection, replacements),
        item: substituteV2Text(predicate.item, replacements)
      };
    }
    return {
      ...predicate,
      left: substituteV2Text(predicate.left, replacements),
      right: substituteV2Text(predicate.right, replacements)
    };
  }
  function substituteV2Text(text, replacements) {
    let result = text;
    for (const [name, value] of replacements) {
      const escaped = escapeRegExp(name);
      const replacement = isSimpleSubstitutionAtom(value) ? value : `(${value})`;
      result = result.replace(new RegExp(`\\b${escaped}\\b`, "gu"), replacement);
    }
    return result;
  }
  function isSimpleSubstitutionAtom(value) {
    const trimmed = value.trim();
    return isNumericLiteralText(trimmed) || /^[A-Za-z_][\w]*$/u.test(trimmed);
  }
  function escapeRegExp(text) {
    return text.replace(/[.*+?^${}()|[\]\\]/gu, "\\$&");
  }
  function resolveV2InvokeArg(arg, matchExpr, matchValue) {
    const direction = /^direction\((.+)\)$/u.exec(arg.trim());
    if (direction && direction[1].trim() === matchExpr.trim()) {
      const delta = directionDelta(matchValue);
      if (delta !== void 0) return String(delta);
    }
    return arg;
  }
  function lowerV2Expression(text, line, context) {
    const normalized = normalizeV2ExpressionText(rewriteSpatialExpressionText(text, context));
    return parseExpression(normalized, line);
  }
  function rewriteSpatialExpressionText(text, context) {
    if (context === void 0) return text;
    let rewritten = replaceSpatialCall(text, "random_cell", (args) => {
      if (args.length !== 1) return void 0;
      return randomCellExpression(args[0], context);
    });
    rewritten = replaceSpatialCall(rewritten, "cell_at", (args) => cellAtExpression(args, context));
    return rewritten;
  }
  function replaceSpatialCall(text, name, replacer) {
    const pattern = new RegExp(`\\b${name}\\s*\\(([^()]*)\\)`, "gu");
    return text.replace(pattern, (match, rawArgs) => replacer(splitArgs(rawArgs)) ?? match);
  }
  function findSpatialCalls(text, name) {
    const pattern = new RegExp(`\\b${name}\\s*\\(([^()]*)\\)`, "gu");
    return [...text.matchAll(pattern)].map((match) => splitArgs(match[1]));
  }
  function cellAtExpression(args, context) {
    const [domain, pos] = cellAtDomainAndPosition(args, context);
    if (domain === void 0 || pos === void 0) return void 0;
    const map = context.cellMapNames.get(domain) ?? `__cell_map_${domain}`;
    return `digit_at(${map}, ${cellAtIndexExpression(pos, domain, context)})`;
  }
  function cellAtDomainAndPosition(args, context) {
    if (args.length === 2) return [args[0], args[1]];
    if (args.length !== 1) return [void 0, void 0];
    return [context.stateDomains.get(args[0]), args[0]];
  }
  function cellAtIndexExpression(pos, domain, context) {
    const world = context.worlds.get(domain);
    switch (world?.position?.encoding) {
      case "row_scan":
      case "floor_plan":
      case "decimal_player":
      case "pier_to_ship":
        return decimalOnesExpression(pos);
      case "corridor_plan":
      case "packed_decimal_zero_run":
      case "cockpit_perspective":
      case void 0:
        return pos;
    }
    return pos;
  }
  function boardForCells(mask, context) {
    const domain = context.stateDomains.get(mask.trim());
    return domain === void 0 ? void 0 : context.boards.get(domain);
  }
  function oneDimensionalCellMaskExpression(mask, cell, context) {
    const board = boardForCells(mask, context);
    if (board === void 0) return void 0;
    if (board.height === 1 && board.xMin >= 0 && board.xMax <= 7) return `pow10(${cell})`;
    if (board.width === 1 && board.yMin >= 0 && board.yMax <= 7) return `pow10(${cell})`;
    return void 0;
  }
  function cellMaskExpressionForCollection(mask, cell, context) {
    return oneDimensionalCellMaskExpression(mask, cell, context);
  }
  function decimalOnesExpression(expr) {
    return `(${expr}) - 10 * int((${expr}) / 10)`;
  }
  function randomCellExpression(domain, context) {
    const board = context.boards.get(domain);
    if (board !== void 0) return randomBoardCellExpression(board);
    const world = context.worlds.get(domain);
    if (world !== void 0) return randomWorldCellExpression(world);
    return "int(random() * 9) + 1";
  }
  function randomBoardCellExpression(board) {
    if (board.height === 1) return addOffsetExpression(`int(random() * ${board.width})`, board.xMin);
    if (board.width === 1) return addOffsetExpression(`int(random() * ${board.height})`, board.yMin);
    const x = addOffsetExpression(`int(random() * ${board.width})`, board.xMin);
    const y = addOffsetExpression(`int(random() * ${board.height})`, board.yMin);
    return `${x} + 10 * (${y})`;
  }
  function randomWorldCellExpression(world) {
    switch (world.position?.encoding) {
      case "pier_to_ship":
        return "int(random() * 8) + 1";
      case "cockpit_perspective":
      case "corridor_plan":
      case "decimal_player":
      case "floor_plan":
      case "packed_decimal_zero_run":
      case "row_scan":
      case void 0:
        return "int(random() * 9) + 1";
    }
    return "int(random() * 9) + 1";
  }
  function addOffsetExpression(expr, offset) {
    if (offset === 0) return expr;
    if (offset > 0) return `${expr} + ${offset}`;
    return `${expr} - ${Math.abs(offset)}`;
  }
  function normalizeV2ExpressionText(text) {
    return text.trim().replace(/\b([A-Za-z_][\w]*)\.floor\b/gu, "int($1 / 100)");
  }
  function parseStackSource(text, line) {
    const trimmed = text.trim();
    if (/^input\.(X|Y)$/u.test(trimmed)) {
      throw new ParseError(`Use '${trimmed.replace("input.", "stack.")}' for startup stack values`, line);
    }
    const match = /^stack\.(X|Y)$/u.exec(trimmed);
    return match?.[1];
  }
  function directionDelta(text) {
    const value = Number(text.trim());
    if (!Number.isFinite(value)) return void 0;
    const mapping = /* @__PURE__ */ new Map([
      [2, 1],
      [8, -1],
      [4, -10],
      [6, 10],
      [5, 100],
      [-5, -100]
    ]);
    return mapping.get(value);
  }
  function isNumericLiteralText(text) {
    return /^-?\d+(?:\.\d+)?(?:e[+-]?\d+)?$/iu.test(text.trim());
  }
  function parseIdentifierList(text) {
    return text.split(",").map((part) => part.trim()).filter((part) => part.length > 0);
  }
  function parseDisplayItemList(text, line) {
    return splitDisplayArgs(text, line).map((part) => parseDisplayItem(part, line));
  }
  function parseDisplayItem(text, line) {
    const trimmed = text.trim();
    if (trimmed.startsWith('"')) {
      return {
        kind: "literal",
        text: parseQuotedDisplayText(trimmed, line),
        line
      };
    }
    if (/^[A-Za-z_][\w]*$/u.test(trimmed)) {
      return { kind: "source", name: trimmed, line };
    }
    throw new ParseError(`Display item must be a string literal or state name, got '${trimmed}'`, line);
  }
  function parseQuotedDisplayText(text, line) {
    try {
      const value = JSON.parse(text);
      if (typeof value === "string") return value;
    } catch {
    }
    throw new ParseError(`Invalid display string literal '${text}'`, line);
  }
  function splitDisplayArgs(text, line) {
    const parts = [];
    let start = 0;
    let quoted = false;
    let escaped = false;
    for (let index = 0; index < text.length; index += 1) {
      const char = text[index];
      if (escaped) {
        escaped = false;
        continue;
      }
      if (quoted && char === "\\") {
        escaped = true;
        continue;
      }
      if (char === '"') {
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
  function parseExpression(text, line = 0) {
    return new ExpressionParser(text, line).parse();
  }
  var ExpressionParser = class {
    tokens;
    index = 0;
    source;
    line;
    constructor(source, line) {
      this.source = source;
      this.line = line;
      this.tokens = tokenizeExpression(source, line);
    }
    parse() {
      const expr = this.parseAdditive();
      if (!this.done()) {
        throw new ParseError(
          `Unexpected token '${this.peek()}' in expression '${this.source}'`,
          this.line
        );
      }
      return expr;
    }
    parseAdditive() {
      let left = this.parseMultiplicative();
      while (this.peekOptional() === "+" || this.peekOptional() === "-") {
        const op = this.next();
        left = { kind: "binary", op, left, right: this.parseMultiplicative() };
      }
      return left;
    }
    parseMultiplicative() {
      let left = this.parseUnary();
      while (this.peekOptional() === "*" || this.peekOptional() === "/") {
        const op = this.next();
        left = { kind: "binary", op, left, right: this.parseUnary() };
      }
      return left;
    }
    parseUnary() {
      if (this.peekOptional() === "-") {
        this.next();
        return { kind: "unary", op: "-", expr: this.parseUnary() };
      }
      return this.parsePrimary();
    }
    parsePrimary() {
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
          const args = [];
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
        this.line
      );
    }
    expect(token) {
      const actual = this.next();
      if (actual !== token) {
        throw new ParseError(
          `Expected '${token}', got '${actual}' in expression '${this.source}'`,
          this.line
        );
      }
    }
    done() {
      return this.index >= this.tokens.length;
    }
    peek() {
      const token = this.tokens[this.index];
      if (!token) {
        throw new ParseError(`Unexpected end of expression '${this.source}'`, this.line);
      }
      return token;
    }
    peekOptional() {
      return this.tokens[this.index];
    }
    next() {
      const token = this.peek();
      this.index += 1;
      return token;
    }
  };
  function tokenizeExpression(source, line) {
    const tokens = [];
    const regex = /\s*([A-Za-z_А-Яа-я][\wА-Яа-я]*|\d+(?:\.\d+)?(?:e[+-]?\d+)?|==|!=|<=|>=|[()+\-*/,])\s*/giy;
    let index = 0;
    while (index < source.length) {
      regex.lastIndex = index;
      const match = regex.exec(source);
      if (!match) {
        throw new ParseError(
          `Cannot tokenize expression near '${source.slice(index)}'`,
          line
        );
      }
      tokens.push(match[1]);
      index = regex.lastIndex;
    }
    return tokens;
  }
  function stripComment(text) {
    let quoted = false;
    let escaped = false;
    for (let index = 0; index < text.length; index += 1) {
      const char = text[index];
      if (escaped) {
        escaped = false;
        continue;
      }
      if (quoted && char === "\\") {
        escaped = true;
        continue;
      }
      if (char === '"') {
        quoted = !quoted;
        continue;
      }
      if (!quoted && char === "#") return text.slice(0, index);
      if (!quoted && char === "/" && text[index + 1] === "/") return text.slice(0, index);
    }
    return text;
  }

  // src/core/ir.ts
  var REGISTERS_BY_INDEX = [
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
    "e"
  ];
  var DIRECT_STORE_BASE = 64;
  var DIRECT_RECALL_BASE = 96;
  var INDIRECT_JUMP_BASE = 128;
  var INDIRECT_CALL_BASE = 160;
  var INDIRECT_STORE_BASE = 176;
  var INDIRECT_RECALL_BASE = 208;
  var INDIRECT_COND_BASES = {
    112: "!=0",
    144: ">=0",
    192: "<0",
    224: "==0"
  };
  var COND_OPCODES = {
    87: "!=0",
    89: ">=0",
    92: "<0",
    94: "==0"
  };
  var LOOP_OPCODES = {
    93: "L0",
    91: "L1",
    88: "L2",
    90: "L3"
  };
  var TAKES_ADDRESS = /* @__PURE__ */ new Set([
    81,
    83,
    87,
    88,
    89,
    90,
    91,
    92,
    93,
    94
  ]);
  function metaFromOp(op) {
    const meta = { mnemonic: op.mnemonic };
    if (op.comment !== void 0) meta.comment = op.comment;
    if (op.sourceLine !== void 0) meta.sourceLine = op.sourceLine;
    if (op.raw === true) meta.raw = true;
    return meta;
  }
  function targetMetaFromAddress(item) {
    const meta = {};
    if (item.comment !== void 0) meta.comment = item.comment;
    if (item.sourceLine !== void 0) meta.sourceLine = item.sourceLine;
    if (item.formalOpcode !== void 0) meta.formalOpcode = item.formalOpcode;
    return meta;
  }
  function isInRange(opcode, base) {
    return opcode >= base && opcode <= base + 14;
  }
  function registerForOffset(opcode, base) {
    return REGISTERS_BY_INDEX[opcode - base];
  }
  function stopSemanticFromComment(comment) {
    if (comment === void 0) return "unknown";
    const lower = comment.toLowerCase();
    if (lower.startsWith("halt")) return "halt";
    if (lower.startsWith("pause")) return "pause";
    if (lower.startsWith("show")) return "show";
    if (lower.startsWith("ask")) return "ask";
    if (lower.startsWith("input") || lower.startsWith("read")) return "input";
    if (lower.startsWith("implicit final stop")) return "halt";
    if (lower.includes("implicit stop")) return "halt";
    return "unknown";
  }
  function raiseMachineToIr(items) {
    const result = [];
    for (let i = 0; i < items.length; i += 1) {
      const item = items[i];
      if (item.kind === "label") {
        result.push({ kind: "label", name: item.name });
        continue;
      }
      if (item.kind === "address") {
        result.push({
          kind: "orphan-address",
          target: item.target,
          meta: targetMetaFromAddress(item)
        });
        continue;
      }
      const meta = metaFromOp(item);
      const opcode = item.opcode;
      if (TAKES_ADDRESS.has(opcode)) {
        const next = items[i + 1];
        if (next?.kind !== "address") {
          result.push({ kind: "plain", opcode, meta });
          continue;
        }
        const target = next.target;
        const tmeta = targetMetaFromAddress(next);
        i += 1;
        if (opcode === 81) {
          result.push({ kind: "jump", target, opcode, meta, targetMeta: tmeta });
          continue;
        }
        if (opcode === 83) {
          result.push({ kind: "call", target, opcode, meta, targetMeta: tmeta });
          continue;
        }
        const condition = COND_OPCODES[opcode];
        if (condition !== void 0) {
          result.push({ kind: "cjump", condition, target, opcode, meta, targetMeta: tmeta });
          continue;
        }
        const loop = LOOP_OPCODES[opcode];
        if (loop !== void 0) {
          result.push({ kind: "loop", counter: loop, target, opcode, meta, targetMeta: tmeta });
          continue;
        }
        result.push({ kind: "plain", opcode, meta });
        continue;
      }
      if (isInRange(opcode, DIRECT_STORE_BASE)) {
        result.push({
          kind: "store",
          register: registerForOffset(opcode, DIRECT_STORE_BASE),
          opcode,
          meta
        });
        continue;
      }
      if (isInRange(opcode, DIRECT_RECALL_BASE)) {
        result.push({
          kind: "recall",
          register: registerForOffset(opcode, DIRECT_RECALL_BASE),
          opcode,
          meta
        });
        continue;
      }
      if (isInRange(opcode, INDIRECT_STORE_BASE)) {
        result.push({
          kind: "indirect-store",
          register: registerForOffset(opcode, INDIRECT_STORE_BASE),
          opcode,
          meta
        });
        continue;
      }
      if (isInRange(opcode, INDIRECT_RECALL_BASE)) {
        result.push({
          kind: "indirect-recall",
          register: registerForOffset(opcode, INDIRECT_RECALL_BASE),
          opcode,
          meta
        });
        continue;
      }
      if (isInRange(opcode, INDIRECT_JUMP_BASE)) {
        result.push({
          kind: "indirect-jump",
          register: registerForOffset(opcode, INDIRECT_JUMP_BASE),
          opcode,
          meta
        });
        continue;
      }
      if (isInRange(opcode, INDIRECT_CALL_BASE)) {
        result.push({
          kind: "indirect-call",
          register: registerForOffset(opcode, INDIRECT_CALL_BASE),
          opcode,
          meta
        });
        continue;
      }
      const indirectCondBase = Object.keys(INDIRECT_COND_BASES).map((value) => Number(value)).find((base) => isInRange(opcode, base));
      if (indirectCondBase !== void 0) {
        result.push({
          kind: "indirect-cjump",
          condition: INDIRECT_COND_BASES[indirectCondBase],
          register: registerForOffset(opcode, indirectCondBase),
          opcode,
          meta
        });
        continue;
      }
      if (opcode === 82) {
        result.push({ kind: "return", opcode, meta });
        continue;
      }
      if (opcode === 80) {
        result.push({
          kind: "stop",
          opcode,
          semantic: stopSemanticFromComment(item.comment),
          meta
        });
        continue;
      }
      result.push({ kind: "plain", opcode, meta });
    }
    return result;
  }
  function machineOpFromMeta(opcode, meta) {
    const op = {
      kind: "op",
      opcode,
      mnemonic: meta.mnemonic
    };
    if (meta.comment !== void 0) op.comment = meta.comment;
    if (meta.sourceLine !== void 0) op.sourceLine = meta.sourceLine;
    if (meta.raw === true) op.raw = true;
    return op;
  }
  function machineAddressFromMeta(target, meta) {
    const ref = { kind: "address", target };
    if (meta.comment !== void 0) ref.comment = meta.comment;
    if (meta.sourceLine !== void 0) ref.sourceLine = meta.sourceLine;
    if (meta.formalOpcode !== void 0) ref.formalOpcode = meta.formalOpcode;
    return ref;
  }
  function lowerIrToMachine(ops) {
    const result = [];
    for (const op of ops) {
      switch (op.kind) {
        case "label":
          result.push({ kind: "label", name: op.name });
          break;
        case "orphan-address":
          result.push(machineAddressFromMeta(op.target, op.meta));
          break;
        case "store":
        case "recall":
        case "indirect-store":
        case "indirect-recall":
        case "indirect-jump":
        case "indirect-call":
        case "indirect-cjump":
        case "return":
        case "stop":
        case "plain":
          result.push(machineOpFromMeta(op.opcode, op.meta));
          break;
        case "jump":
        case "cjump":
        case "call":
        case "loop":
          result.push(machineOpFromMeta(op.opcode, op.meta));
          result.push(machineAddressFromMeta(op.target, op.targetMeta));
          break;
      }
    }
    return result;
  }

  // src/core/passes/helpers.ts
  function cellsPerOp(op) {
    switch (op.kind) {
      case "label":
        return 0;
      case "jump":
      case "cjump":
      case "call":
      case "loop":
        return 2;
      case "orphan-address":
        return 1;
      default:
        return 1;
    }
  }
  function calculateLabelAddresses(ops) {
    const map = /* @__PURE__ */ new Map();
    let address = 0;
    for (const op of ops) {
      if (op.kind === "label") {
        map.set(op.name, address);
        continue;
      }
      address += cellsPerOp(op);
    }
    return map;
  }
  function targetAddress(target, labels) {
    if (typeof target === "number") return target;
    return labels.get(target);
  }
  function hasRewriteBarrier(op) {
    return "meta" in op && "raw" in op.meta && op.meta.raw === true;
  }
  function isDisplayFocusSensitive(op) {
    return "meta" in op && (op.meta.roles?.includes("display-byte") === true || /\b(display|screen|show|x2|вп)\b/iu.test(op.meta.comment ?? ""));
  }
  function knownIndirectMemoryTarget(op) {
    if (op.kind !== "indirect-recall" && op.kind !== "indirect-store") return void 0;
    const match = /\bindirect-memory-target=([0-9a-e])\b/iu.exec(op.meta.comment ?? "");
    if (!match) return void 0;
    return match[1].toLowerCase();
  }

  // src/core/passes/arithmetic-if.ts
  var run = (ops) => {
    const labelRefs = countLabelRefs(ops);
    const result = [];
    let applied = 0;
    for (let i = 0; i < ops.length; i += 1) {
      const op = ops[i];
      if (op.kind !== "cjump" || typeof op.target !== "string" || hasRewriteBarrier(op)) {
        result.push(op);
        continue;
      }
      const thenJumpIndex = findNextFlowOp(ops, i + 1);
      if (thenJumpIndex === void 0) {
        result.push(op);
        continue;
      }
      const thenJump = ops[thenJumpIndex];
      if (thenJump.kind !== "jump" || typeof thenJump.target !== "string") {
        result.push(op);
        continue;
      }
      const falseLabelIndex = thenJumpIndex + 1;
      const falseLabel = ops[falseLabelIndex];
      if (falseLabel?.kind !== "label" || falseLabel.name !== op.target) {
        result.push(op);
        continue;
      }
      const endLabelIndex = findLabel(ops, thenJump.target, falseLabelIndex + 1);
      if (endLabelIndex === void 0 || (labelRefs.get(op.target) ?? 0) !== 1) {
        result.push(op);
        continue;
      }
      const thenOps = ops.slice(i + 1, thenJumpIndex);
      const elseOps = ops.slice(falseLabelIndex + 1, endLabelIndex);
      if (!isPureLinearBlock(thenOps) || !isPureLinearBlock(elseOps) || !opsEquivalent(thenOps, elseOps)) {
        result.push(op);
        continue;
      }
      result.push(...thenOps);
      i = endLabelIndex - 1;
      applied += 1;
    }
    if (applied === 0) return { ops: result, applied: 0, optimizations: [] };
    return {
      ops: result,
      applied,
      optimizations: [
        {
          name: "arithmetic-if-pass",
          detail: `Collapsed ${applied} conditional block(s) whose simplified branches were byte-identical.`
        }
      ]
    };
  };
  function countLabelRefs(ops) {
    const refs = /* @__PURE__ */ new Map();
    for (const op of ops) {
      if ((op.kind === "jump" || op.kind === "cjump" || op.kind === "call" || op.kind === "loop") && typeof op.target === "string") {
        refs.set(op.target, (refs.get(op.target) ?? 0) + 1);
      }
    }
    return refs;
  }
  function findNextFlowOp(ops, start) {
    for (let i = start; i < ops.length; i += 1) {
      const op = ops[i];
      if (op.kind === "label") return void 0;
      if (op.kind === "jump" || op.kind === "cjump" || op.kind === "call" || op.kind === "loop") return i;
    }
    return void 0;
  }
  function findLabel(ops, name, start) {
    for (let i = start; i < ops.length; i += 1) {
      const op = ops[i];
      if (op.kind === "label" && op.name === name) return i;
    }
    return void 0;
  }
  function isPureLinearBlock(ops) {
    return ops.length > 0 && ops.every(
      (op) => !hasRewriteBarrier(op) && (op.kind === "plain" || op.kind === "store" || op.kind === "recall" || op.kind === "stop")
    );
  }
  function opsEquivalent(a, b) {
    if (a.length !== b.length) return false;
    for (let i = 0; i < a.length; i += 1) {
      const left = a[i];
      const right = b[i];
      if (left.kind !== right.kind) return false;
      if ("opcode" in left && "opcode" in right && left.opcode !== right.opcode) return false;
      if (left.kind === "store" && right.kind === "store" && left.register !== right.register) return false;
      if (left.kind === "recall" && right.kind === "recall" && left.register !== right.register) return false;
      if (left.kind === "stop" && right.kind === "stop" && left.semantic !== right.semantic) return false;
    }
    return true;
  }
  var arithmeticIfPass = {
    name: "arithmetic-if-pass",
    run,
    layoutSafe: false
  };

  // src/core/passes/constant-folding.ts
  function digitOf(op) {
    if (op.kind === "plain" && op.opcode <= 9) return op.opcode;
    return void 0;
  }
  function aluOpcode(op) {
    if (op.kind !== "plain") return void 0;
    if (op.opcode === 16) return "+";
    if (op.opcode === 17) return "-";
    if (op.opcode === 18) return "*";
    if (op.opcode === 19) return "/";
    return void 0;
  }
  function isIdentityPlus(op, prev) {
    if (prev === void 0) return false;
    const digit = digitOf(prev);
    if (digit !== 0) return false;
    return aluOpcode(op) === "+";
  }
  function isIdentityMul(op, prev) {
    if (prev === void 0) return false;
    const digit = digitOf(prev);
    if (digit !== 1) return false;
    return aluOpcode(op) === "*";
  }
  var run2 = (ops) => {
    const result = [];
    let applied = 0;
    for (let i = 0; i < ops.length; i += 1) {
      const op = ops[i];
      const prev = result[result.length - 1];
      if ((isIdentityPlus(op, prev) || isIdentityMul(op, prev)) && prev !== void 0 && prev.kind === "plain" && prev.meta.raw !== true && op.kind === "plain" && op.meta.raw !== true) {
        result.pop();
        applied += 1;
        continue;
      }
      result.push(op);
    }
    if (applied === 0) {
      return { ops: result, applied: 0, optimizations: [] };
    }
    const passResult = {
      ops: result,
      applied,
      optimizations: [
        {
          name: "constant-folding",
          detail: `Dropped ${applied} identity arithmetic operation(s) (0+ or 1*).`
        }
      ]
    };
    return passResult;
  };
  var constantFolding = {
    name: "constant-folding",
    run: run2,
    layoutSafe: false
  };

  // src/core/passes/cse-display-block.ts
  function isPureDataOp(op) {
    if (hasRewriteBarrier(op)) return false;
    if (op.kind === "recall") return true;
    if (op.kind === "plain") {
      if (op.opcode === 16 || op.opcode === 18) return true;
      if (op.opcode <= 9 || op.opcode === 10) return true;
    }
    return false;
  }
  function isBlockTerminator(op) {
    if (hasRewriteBarrier(op)) return false;
    return op.kind === "stop" || op.kind === "store" || op.kind === "indirect-store";
  }
  function collectCseCandidates(ops) {
    const blocks = [];
    let start = -1;
    let buffer = [];
    for (let i = 0; i < ops.length; i += 1) {
      const op = ops[i];
      if (op.kind === "label") {
        start = -1;
        buffer = [];
        continue;
      }
      if (isPureDataOp(op)) {
        if (start === -1) start = i;
        buffer.push(op);
        continue;
      }
      if (buffer.length >= 3 && op.kind === "return") {
        blocks.push({ startIndex: start, ops: [...buffer, op] });
      } else if (buffer.length >= 3 && op.kind === "stop" && ops[i + 1]?.kind === "return") {
        blocks.push({ startIndex: start, ops: [...buffer, op, ops[i + 1]] });
      }
      start = -1;
      buffer = [];
      if (isBlockTerminator(op)) continue;
    }
    return blocks;
  }
  function blockSignature(block) {
    return block.ops.map((op) => {
      if (op.kind === "recall") return `r:${op.register}`;
      if (op.kind === "plain") return `p:${op.opcode.toString(16)}`;
      if (op.kind === "stop") return `stop:${op.semantic}`;
      if (op.kind === "return") return "return";
      return `o:${op.kind}`;
    }).join("|");
  }
  var cseLabelCounter = 0;
  function freshLabel() {
    cseLabelCounter += 1;
    return `__cse_block_${cseLabelCounter}`;
  }
  var run3 = (ops) => {
    if (ops.length === 0) return { ops: [], applied: 0, optimizations: [] };
    const blocks = collectCseCandidates(ops);
    if (blocks.length < 2) return { ops: [...ops], applied: 0, optimizations: [] };
    const bySignature = /* @__PURE__ */ new Map();
    for (const block of blocks) {
      const sig = blockSignature(block);
      if (!bySignature.has(sig)) bySignature.set(sig, []);
      bySignature.get(sig).push(block);
    }
    let applied = 0;
    const replaceWith = /* @__PURE__ */ new Map();
    const labelsToInsert = /* @__PURE__ */ new Map();
    for (const [, group] of bySignature) {
      if (group.length < 2) continue;
      const blockSize = group[0].ops.length;
      const savedPerSite = blockSize - 2;
      if (savedPerSite < 1) continue;
      const totalSavings = savedPerSite * (group.length - 1);
      if (totalSavings <= 0) continue;
      const canonical = group[0];
      const label = freshLabel();
      labelsToInsert.set(canonical.startIndex, label);
      for (let i2 = 1; i2 < group.length; i2 += 1) {
        const dup = group[i2];
        replaceWith.set(dup.startIndex, {
          count: dup.ops.length,
          label,
          targetMeta: { comment: "cse jump" },
          jumpMeta: { mnemonic: "\u0411\u041F", comment: "cse" }
        });
        applied += 1;
      }
    }
    if (applied === 0) return { ops: [...ops], applied: 0, optimizations: [] };
    const result = [];
    let i = 0;
    while (i < ops.length) {
      const labelHere = labelsToInsert.get(i);
      if (labelHere !== void 0) {
        result.push({ kind: "label", name: labelHere });
      }
      const replacement = replaceWith.get(i);
      if (replacement !== void 0) {
        result.push({
          kind: "jump",
          target: replacement.label,
          opcode: 81,
          meta: replacement.jumpMeta,
          targetMeta: replacement.targetMeta
        });
        i += replacement.count;
        continue;
      }
      result.push(ops[i]);
      i += 1;
    }
    const passResult = {
      ops: result,
      applied,
      optimizations: [
        {
          name: "cse-display-block",
          detail: `Deduplicated ${applied} display block(s) by redirecting to a shared exit.`
        }
      ]
    };
    return passResult;
  };
  var cseDisplayBlock = {
    name: "cse-display-block",
    run: run3,
    layoutSafe: false
  };

  // src/core/passes/liveness-analysis.ts
  function buildTargetIndexes(ops) {
    const labelIndex = /* @__PURE__ */ new Map();
    const addressIndex = /* @__PURE__ */ new Map();
    let address = 0;
    for (let i = 0; i < ops.length; i += 1) {
      const op = ops[i];
      if (op.kind === "label") {
        labelIndex.set(op.name, i);
        continue;
      }
      addressIndex.set(address, i);
      address += cellsPerOp(op);
    }
    return { labelIndex, addressIndex };
  }
  function buildSuccessors(ops) {
    const { labelIndex, addressIndex } = buildTargetIndexes(ops);
    const successors = Array.from({ length: ops.length }, () => []);
    const callReturns = [];
    for (let i = 0; i < ops.length; i += 1) {
      const op = ops[i];
      const next = i + 1;
      if ((op.kind === "call" || op.kind === "indirect-call") && next < ops.length) {
        callReturns.push(next);
      }
    }
    for (let i = 0; i < ops.length; i += 1) {
      const op = ops[i];
      const next = i + 1;
      const fallthrough = () => {
        if (next < ops.length) successors[i].push(next);
      };
      const jumpTo = (target) => {
        if (typeof target === "string") {
          const idx = labelIndex.get(target);
          if (idx !== void 0) successors[i].push(idx);
        } else {
          const idx = addressIndex.get(target);
          if (idx !== void 0) successors[i].push(idx);
        }
      };
      switch (op.kind) {
        case "label":
        case "store":
        case "recall":
        case "indirect-store":
        case "indirect-recall":
        case "plain":
        case "orphan-address":
          fallthrough();
          break;
        case "stop":
          fallthrough();
          break;
        case "return":
          successors[i].push(...callReturns);
          break;
        case "jump":
          jumpTo(op.target);
          break;
        case "cjump":
        case "loop":
          jumpTo(op.target);
          fallthrough();
          break;
        case "call":
          jumpTo(op.target);
          fallthrough();
          break;
        case "indirect-jump":
          break;
        case "indirect-call":
          fallthrough();
          break;
        case "indirect-cjump":
          fallthrough();
          break;
      }
    }
    return { successors };
  }
  function defsAndUses(op) {
    switch (op.kind) {
      case "store":
        return { defs: [op.register], uses: [] };
      case "recall":
        return { defs: [], uses: [op.register] };
      case "indirect-recall": {
        const target = knownIndirectMemoryTarget(op);
        return {
          defs: [],
          uses: target === void 0 ? [op.register] : [op.register, target]
        };
      }
      case "indirect-store": {
        const target = knownIndirectMemoryTarget(op);
        return {
          defs: target === void 0 ? [] : [target],
          uses: [op.register]
        };
      }
      case "indirect-jump":
      case "indirect-call":
      case "indirect-cjump":
        return { defs: [], uses: [op.register] };
      default:
        return { defs: [], uses: [] };
    }
  }
  function computeLiveness(ops) {
    const { successors } = buildSuccessors(ops);
    const n = ops.length;
    const liveIn = Array.from({ length: n }, () => /* @__PURE__ */ new Set());
    const liveOut = Array.from({ length: n }, () => /* @__PURE__ */ new Set());
    let changed = true;
    let iterations = 0;
    while (changed && iterations < 200) {
      changed = false;
      iterations += 1;
      for (let i = n - 1; i >= 0; i -= 1) {
        const op = ops[i];
        const succ = successors[i];
        const newOut = /* @__PURE__ */ new Set();
        for (const s of succ) {
          for (const reg of liveIn[s]) newOut.add(reg);
        }
        const { defs, uses } = defsAndUses(op);
        const newIn = new Set(uses);
        for (const reg of newOut) {
          if (!defs.includes(reg)) newIn.add(reg);
        }
        if (!setsEqual(newIn, liveIn[i]) || !setsEqual(newOut, liveOut[i])) {
          liveIn[i] = newIn;
          liveOut[i] = newOut;
          changed = true;
        }
      }
    }
    return { liveIn, liveOut };
  }
  function setsEqual(a, b) {
    if (a.size !== b.size) return false;
    for (const value of a) {
      if (!b.has(value)) return false;
    }
    return true;
  }

  // src/core/passes/dead-code-after-halt.ts
  function reachableFromEntry(ops) {
    const labelIndex = /* @__PURE__ */ new Map();
    const addressIndex = /* @__PURE__ */ new Map();
    let address = 0;
    for (let i = 0; i < ops.length; i += 1) {
      const op = ops[i];
      if (op.kind === "label") labelIndex.set(op.name, i);
      else {
        addressIndex.set(address, i);
        address += cellsPerOp(op);
      }
    }
    const visited = /* @__PURE__ */ new Set();
    const stack = [];
    if (ops.length > 0) stack.push(0);
    while (stack.length > 0) {
      const i = stack.pop();
      if (visited.has(i)) continue;
      visited.add(i);
      const op = ops[i];
      const fallthrough = () => {
        if (i + 1 < ops.length) stack.push(i + 1);
      };
      const target = (label) => {
        if (typeof label === "string") {
          const idx = labelIndex.get(label);
          if (idx !== void 0) stack.push(idx);
        } else {
          const idx = addressIndex.get(label);
          if (idx !== void 0) stack.push(idx);
        }
      };
      switch (op.kind) {
        case "label":
        case "store":
        case "recall":
        case "indirect-store":
        case "indirect-recall":
        case "plain":
        case "orphan-address":
          fallthrough();
          break;
        case "stop":
          fallthrough();
          break;
        case "return":
          break;
        case "jump":
          target(op.target);
          break;
        case "cjump":
        case "loop":
          target(op.target);
          fallthrough();
          break;
        case "call":
          target(op.target);
          fallthrough();
          break;
        case "indirect-jump": {
          const knownTarget = knownIndirectTarget(op);
          if (knownTarget !== void 0) target(knownTarget);
          break;
        }
        case "indirect-call": {
          const knownTarget = knownIndirectTarget(op);
          if (knownTarget !== void 0) target(knownTarget);
          fallthrough();
          break;
        }
        case "indirect-cjump": {
          const knownTarget = knownIndirectTarget(op);
          if (knownTarget !== void 0) target(knownTarget);
          fallthrough();
          break;
        }
      }
    }
    return visited;
  }
  function knownIndirectTarget(op) {
    if (op.kind !== "indirect-jump" && op.kind !== "indirect-call" && op.kind !== "indirect-cjump") {
      return void 0;
    }
    const match = /\bindirect-target=(\d+)\b/u.exec(op.meta.comment ?? "");
    if (!match) return void 0;
    const target = Number(match[1]);
    if (!Number.isInteger(target) || target < 0 || target > 104) return void 0;
    return target;
  }
  var run4 = (ops) => {
    if (ops.length === 0) return { ops: [], applied: 0, optimizations: [] };
    const reachable = reachableFromEntry(ops);
    if (reachable.size === ops.length) {
      return { ops: [...ops], applied: 0, optimizations: [] };
    }
    computeLiveness(ops);
    const result = [];
    let applied = 0;
    for (let i = 0; i < ops.length; i += 1) {
      if (reachable.has(i)) {
        result.push(ops[i]);
        continue;
      }
      if (ops[i].kind === "label") {
        result.push(ops[i]);
        continue;
      }
      applied += 1;
    }
    if (applied === 0) {
      return { ops: result, applied: 0, optimizations: [] };
    }
    const passResult = {
      ops: result,
      applied,
      optimizations: [
        {
          name: "dead-code-after-halt",
          detail: `Removed ${applied} unreachable op(s) from the entry CFG.`
        }
      ]
    };
    return passResult;
  };
  var deadCodeAfterHalt = {
    name: "dead-code-after-halt",
    run: run4,
    layoutSafe: false
  };

  // src/core/passes/dead-store-before-commutative.ts
  function registerReadBeforeNextWrite(ops, start, register) {
    for (let i = start; i < ops.length; i += 1) {
      const op = ops[i];
      if (op.kind === "recall" && op.register === register) return true;
      if (op.kind === "store" && op.register === register) return false;
    }
    return false;
  }
  function isCommutativeAlu(op) {
    if (op.kind !== "plain") return false;
    return op.opcode === 16 || op.opcode === 18;
  }
  var run5 = (ops) => {
    const result = [];
    let applied = 0;
    for (let i = 0; i < ops.length; i += 1) {
      const current = ops[i];
      const next = ops[i + 1];
      const after = ops[i + 2];
      if (current.kind === "store" && next?.kind === "recall" && after !== void 0 && isCommutativeAlu(after) && !hasRewriteBarrier(current) && !hasRewriteBarrier(next) && !hasRewriteBarrier(after) && !registerReadBeforeNextWrite(ops, i + 3, current.register)) {
        applied += 1;
        continue;
      }
      result.push(current);
    }
    const passResult = {
      ops: result,
      applied,
      optimizations: applied > 0 ? [
        {
          name: "dead-temp-store",
          detail: `Removed ${applied} temp store(s) whose X value was consumed directly by stack scheduling.`
        }
      ] : []
    };
    return passResult;
  };
  var deadStoreBeforeCommutative = {
    name: "dead-temp-store",
    run: run5,
    layoutSafe: false
  };

  // src/core/passes/dead-store-elimination.ts
  var run6 = (ops) => {
    if (ops.length === 0) return { ops: [], applied: 0, optimizations: [] };
    const liveness = computeLiveness(ops);
    const removed = /* @__PURE__ */ new Set();
    for (let i = 0; i < ops.length; i += 1) {
      const op = ops[i];
      if (op.kind !== "store") continue;
      if (hasRewriteBarrier(op)) continue;
      if (liveness.liveOut[i].has(op.register)) continue;
      removed.add(i);
    }
    if (removed.size === 0) {
      return { ops: [...ops], applied: 0, optimizations: [] };
    }
    const result = [];
    for (let i = 0; i < ops.length; i += 1) {
      if (!removed.has(i)) result.push(ops[i]);
    }
    const passResult = {
      ops: result,
      applied: removed.size,
      optimizations: [
        {
          name: "dead-store-elimination",
          detail: `Removed ${removed.size} store(s) to register(s) never read before the next assignment.`
        }
      ]
    };
    return passResult;
  };
  var deadStoreElimination = {
    name: "dead-store-elimination",
    run: run6,
    layoutSafe: false
  };

  // src/core/passes/duplicate-failure-tail.ts
  function isZeroDigit(op) {
    return op.kind === "plain" && op.opcode === 0;
  }
  function isPauseLike(op) {
    return op.kind === "stop";
  }
  function isUnconditionalJump(op) {
    return op.kind === "jump";
  }
  var run7 = (ops) => {
    const rewrite = /* @__PURE__ */ new Map();
    const remove = /* @__PURE__ */ new Set();
    let applied = 0;
    for (let i = 0; i + 8 < ops.length; i += 1) {
      const firstLabel = ops[i];
      const firstZero = ops[i + 1];
      const firstPause = ops[i + 2];
      const trampolineLabel = ops[i + 3];
      const trampolineJump = ops[i + 4];
      const secondLabel = ops[i + 5];
      const secondZero = ops[i + 6];
      const secondPause = ops[i + 7];
      const endLabel = ops[i + 8];
      if (firstLabel?.kind === "label" && firstZero !== void 0 && isZeroDigit(firstZero) && firstPause !== void 0 && isPauseLike(firstPause) && trampolineLabel?.kind === "label" && trampolineJump !== void 0 && isUnconditionalJump(trampolineJump) && typeof trampolineJump.target === "string" && secondLabel?.kind === "label" && secondZero !== void 0 && isZeroDigit(secondZero) && secondPause !== void 0 && isPauseLike(secondPause) && endLabel?.kind === "label" && trampolineJump.target === endLabel.name && !hasRewriteBarrier(firstZero) && !hasRewriteBarrier(firstPause) && !hasRewriteBarrier(trampolineJump) && !hasRewriteBarrier(secondZero) && !hasRewriteBarrier(secondPause)) {
        rewrite.set(firstLabel.name, secondLabel.name);
        rewrite.set(trampolineLabel.name, endLabel.name);
        for (let index = i; index <= i + 4; index += 1) remove.add(index);
        applied += 1;
        i += 4;
      }
    }
    if (applied === 0) {
      return { ops: [...ops], applied: 0, optimizations: [] };
    }
    const result = [];
    for (let index = 0; index < ops.length; index += 1) {
      if (remove.has(index)) continue;
      const op = ops[index];
      if ((op.kind === "jump" || op.kind === "cjump" || op.kind === "call" || op.kind === "loop") && typeof op.target === "string") {
        const replacement = rewrite.get(op.target);
        if (replacement !== void 0) {
          const targetMeta = {};
          if (op.targetMeta.comment !== void 0) targetMeta.comment = op.targetMeta.comment;
          if (op.targetMeta.sourceLine !== void 0) targetMeta.sourceLine = op.targetMeta.sourceLine;
          if (op.targetMeta.roles !== void 0) targetMeta.roles = [...op.targetMeta.roles];
          if (op.targetMeta.formalOpcode !== void 0) targetMeta.formalOpcode = op.targetMeta.formalOpcode;
          result.push({ ...op, target: replacement, targetMeta });
          continue;
        }
      }
      result.push(op);
    }
    const passResult = {
      ops: result,
      applied,
      optimizations: [
        {
          name: "duplicate-failure-tail-merge",
          detail: `Merged ${applied} duplicate pause-0 failure tail(s).`
        }
      ]
    };
    return passResult;
  };
  var duplicateFailureTail = {
    name: "duplicate-failure-tail-merge",
    run: run7,
    layoutSafe: false
  };

  // src/core/indirect-addressing.ts
  var REGISTERS_BY_INDEX2 = [
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
    "e"
  ];
  function indirectSelectorMutation(register) {
    const index = registerIndex(register);
    if (index <= 3) return "pre-decrement";
    if (index <= 6) return "pre-increment";
    return "stable";
  }
  function isStableIndirectSelector(register) {
    return indirectSelectorMutation(register) === "stable";
  }
  function evaluateIndirectAddress(selector, value, operation) {
    const mutation = indirectSelectorMutation(selector);
    const fractional = isPositiveFractional(value);
    if (selector === "0" && fractional) {
      const formalAddress = formalAddressInfo(153);
      return {
        selector,
        mutation,
        operation,
        transformed: "99",
        ...operation === "flow" ? { flowTarget: 99, actualFlowTarget: formalAddress.actual, formalAddress } : { memoryTarget: "3" },
        resultValue: "-99999999"
      };
    }
    const transformed = transformSelectorValue(value, mutation);
    if (transformed === void 0) return void 0;
    if (operation === "flow") {
      const flowTarget = flowTargetFromTransformed(transformed);
      const formalAddress = formalAddressInfo(formalOpcodeForFlowTarget(transformed, flowTarget));
      const result = {
        selector,
        mutation,
        operation,
        transformed,
        formalAddress,
        flowTarget,
        actualFlowTarget: formalAddress.actual,
        resultValue: transformed
      };
      const superDark = superDarkTarget(formalAddress.opcode);
      if (superDark !== void 0) result.superDark = superDark;
      return result;
    }
    const memoryTarget = memoryTargetFromTransformed(transformed);
    if (memoryTarget === void 0) return void 0;
    return {
      selector,
      mutation,
      operation,
      transformed,
      memoryTarget,
      resultValue: transformed
    };
  }
  function memoryTargetFromTransformed(transformed) {
    const normalized = transformed.trim().toLowerCase();
    if (/^-?\d+$/u.test(normalized)) {
      return REGISTERS_BY_INDEX2[positiveModulo(Number(normalized), 15)];
    }
    const last = normalized.at(-1);
    if (last === void 0) return void 0;
    const nibble = Number.parseInt(last, 16);
    if (!Number.isFinite(nibble) || nibble < 0 || nibble > 15) return void 0;
    if (nibble === 15) return "0";
    return REGISTERS_BY_INDEX2[nibble];
  }
  function superDarkTarget(formalTarget) {
    const info2 = formalAddressInfo(formalTarget);
    if (info2.kind !== "super-dark" || info2.extra === void 0) return void 0;
    return {
      formal: info2.opcode,
      entryAddress: info2.actual,
      continuationAddress: info2.extra
    };
  }
  function transformSelectorValue(value, mutation) {
    if (typeof value === "number") {
      if (!Number.isFinite(value)) return void 0;
      const integer = Math.trunc(value) + mutationDelta(mutation);
      return String(integer);
    }
    const normalized = value.trim().replace(/^0x/iu, "").toLowerCase();
    if (!/^[0-9a-f]+$/iu.test(normalized)) return void 0;
    if (mutation === "stable" && /[a-f]/iu.test(normalized)) return normalized;
    const decimal = Number(normalized);
    if (!Number.isFinite(decimal)) return void 0;
    return String(Math.trunc(decimal) + mutationDelta(mutation));
  }
  function isPositiveFractional(value) {
    const numeric = typeof value === "number" ? value : Number(value.trim().replace(",", "."));
    return numeric !== void 0 && numeric > 0 && numeric < 1;
  }
  function mutationDelta(mutation) {
    if (mutation === "pre-decrement") return -1;
    if (mutation === "pre-increment") return 1;
    return 0;
  }
  function flowTargetFromTransformed(transformed) {
    const normalized = transformed.trim().toLowerCase();
    if (/^[0-9a-f]+$/iu.test(normalized) && /[a-f]/iu.test(normalized)) {
      return Number.parseInt(normalized.slice(-2), 16);
    }
    const numeric = Number(normalized);
    if (!Number.isFinite(numeric)) return 0;
    return positiveModulo(Math.trunc(numeric), 100);
  }
  function formalOpcodeForFlowTarget(transformed, flowTarget) {
    const normalized = transformed.trim().toLowerCase();
    if (/^[0-9a-f]+$/iu.test(normalized) && /[a-f]/iu.test(normalized)) {
      return flowTarget;
    }
    return officialAddressToOpcode(flowTarget);
  }
  function positiveModulo(value, modulus) {
    return (value % modulus + modulus) % modulus;
  }

  // src/core/passes/indirect-addressing.ts
  var INDIRECT_COND_BASES2 = {
    "!=0": 112,
    ">=0": 144,
    "<0": 192,
    "==0": 224
  };
  function clearKnownState(state) {
    state.currentLiteral = void 0;
    state.stableRegisters.clear();
  }
  function cloneMeta(meta, comment) {
    return {
      ...meta,
      comment: [meta.comment, comment].filter(Boolean).join("; ")
    };
  }
  function digitForPlain(op) {
    if (op.kind !== "plain" || op.opcode < 0 || op.opcode > 9) return void 0;
    return String(op.opcode);
  }
  function literalNumber(text) {
    if (text === void 0 || !/^\d+$/u.test(text)) return void 0;
    const value = Number(text);
    if (!Number.isSafeInteger(value)) return void 0;
    return value;
  }
  function rememberStore(state, register) {
    if (!isStableIndirectSelector(register)) return;
    const value = literalNumber(state.currentLiteral);
    if (value === void 0) {
      state.stableRegisters.delete(register);
    } else {
      state.stableRegisters.set(register, String(value));
    }
  }
  function findFlowSelector(state, target) {
    if (typeof target !== "number") return void 0;
    for (const [register, value] of state.stableRegisters) {
      const evaluated = evaluateIndirectAddress(register, value, "flow");
      if (evaluated?.flowTarget === target) return register;
    }
    return void 0;
  }
  function findMemorySelector(state, target) {
    for (const [register, value] of state.stableRegisters) {
      const evaluated = evaluateIndirectAddress(register, value, "memory");
      if (evaluated?.memoryTarget === target) return register;
    }
    return void 0;
  }
  function updateKnownAfterOp(state, op) {
    if (hasRewriteBarrier(op)) {
      clearKnownState(state);
      return;
    }
    const digit = digitForPlain(op);
    if (digit !== void 0) {
      state.currentLiteral = `${state.currentLiteral ?? ""}${digit}`;
      return;
    }
    switch (op.kind) {
      case "store":
        rememberStore(state, op.register);
        return;
      case "recall": {
        state.currentLiteral = state.stableRegisters.get(op.register);
        return;
      }
      case "label":
        clearKnownState(state);
        return;
      case "indirect-store":
        clearKnownState(state);
        return;
      case "call":
      case "indirect-call":
      case "cjump":
      case "indirect-cjump":
      case "stop":
      case "jump":
      case "indirect-jump":
      case "return":
        clearKnownState(state);
        return;
      default:
        state.currentLiteral = void 0;
        return;
    }
  }
  function indirectFlowOp(op, register, target) {
    const offset = registerIndex(register);
    const suffix = `stable indirect flow indirect-target=${target}`;
    if (op.kind === "jump") {
      return {
        kind: "indirect-jump",
        register,
        opcode: 128 + offset,
        meta: cloneMeta({ ...op.meta, mnemonic: `\u041A \u0411\u041F ${register}` }, suffix)
      };
    }
    if (op.kind === "call") {
      return {
        kind: "indirect-call",
        register,
        opcode: 160 + offset,
        meta: cloneMeta({ ...op.meta, mnemonic: `\u041A \u041F\u041F ${register}` }, suffix)
      };
    }
    const opcode = INDIRECT_COND_BASES2[op.condition] + offset;
    const name = op.condition === "==0" ? "x=0" : op.condition === "!=0" ? "x!=0" : `x${op.condition}`;
    return {
      kind: "indirect-cjump",
      condition: op.condition,
      register,
      opcode,
      meta: cloneMeta({ ...op.meta, mnemonic: `\u041A ${name} ${register}` }, suffix)
    };
  }
  var stableFlowRun = (ops) => {
    const result = [];
    const state = { currentLiteral: void 0, stableRegisters: /* @__PURE__ */ new Map() };
    let applied = 0;
    for (const op of ops) {
      if (!hasRewriteBarrier(op) && (op.kind === "jump" || op.kind === "call" || op.kind === "cjump")) {
        const register = findFlowSelector(state, op.target);
        if (register !== void 0 && typeof op.target === "number") {
          const rewritten = indirectFlowOp(op, register, op.target);
          result.push(rewritten);
          updateKnownAfterOp(state, rewritten);
          applied += 1;
          continue;
        }
      }
      result.push(op);
      updateKnownAfterOp(state, op);
    }
    if (applied === 0) return { ops: [...ops], applied: 0, optimizations: [] };
    return {
      ops: result,
      applied,
      optimizations: [
        {
          name: "stable-indirect-flow",
          detail: `Replaced ${applied} direct branch/call(s) with stable-register indirect flow.`
        }
      ]
    };
  };
  var memoryTableRun = (ops) => {
    const result = [];
    const state = { currentLiteral: void 0, stableRegisters: /* @__PURE__ */ new Map() };
    let applied = 0;
    for (const op of ops) {
      if ((op.kind === "recall" || op.kind === "store") && !hasRewriteBarrier(op) && !isDisplayFocusSensitive(op)) {
        const selector = findMemorySelector(state, op.register);
        if (selector !== void 0) {
          const opcodeBase = op.kind === "recall" ? 208 : 176;
          const mnemonic = op.kind === "recall" ? `\u041A \u041F->X ${selector}` : `\u041A X->\u041F ${selector}`;
          const rewritten = {
            kind: op.kind === "recall" ? "indirect-recall" : "indirect-store",
            register: selector,
            opcode: opcodeBase + registerIndex(selector),
            meta: cloneMeta({ ...op.meta, mnemonic }, `indirect memory table indirect-memory-target=${op.register}`)
          };
          result.push(rewritten);
          updateKnownAfterOp(state, rewritten);
          applied += 1;
          continue;
        }
      }
      result.push(op);
      updateKnownAfterOp(state, op);
    }
    if (applied === 0) return { ops: [...ops], applied: 0, optimizations: [] };
    return {
      ops: result,
      applied,
      optimizations: [
        {
          name: "indirect-memory-table",
          detail: `Rewrote ${applied} direct memory access(es) through an existing stable selector.`
        }
      ]
    };
  };
  var stableIndirectFlow = {
    name: "stable-indirect-flow",
    run: stableFlowRun,
    layoutSafe: false
  };
  var indirectMemoryTable = {
    name: "indirect-memory-table",
    run: memoryTableRun,
    layoutSafe: false
  };

  // src/core/passes/jump-thread.ts
  function labelTargets(ops) {
    const map = /* @__PURE__ */ new Map();
    for (let i = 0; i < ops.length; i += 1) {
      const op = ops[i];
      if (op.kind === "label") map.set(op.name, i);
    }
    return map;
  }
  function followLabel(ops, labels, start, seen) {
    let current = start;
    while (!seen.has(current)) {
      seen.add(current);
      const index = labels.get(current);
      if (index === void 0) return current;
      let cursor = index + 1;
      while (cursor < ops.length && ops[cursor].kind === "label") cursor += 1;
      const next = ops[cursor];
      if (next?.kind !== "jump") return current;
      if (typeof next.target !== "string") return current;
      if (hasRewriteBarrier(next)) return current;
      current = next.target;
    }
    return current;
  }
  var run8 = (ops) => {
    const labels = labelTargets(ops);
    const result = [];
    let applied = 0;
    for (const op of ops) {
      if ((op.kind === "jump" || op.kind === "cjump") && typeof op.target === "string" && !hasRewriteBarrier(op)) {
        const final = followLabel(ops, labels, op.target, /* @__PURE__ */ new Set());
        if (final !== void 0 && final !== op.target) {
          applied += 1;
          result.push({ ...op, target: final });
          continue;
        }
      }
      result.push(op);
    }
    if (applied === 0) {
      return { ops: result, applied: 0, optimizations: [] };
    }
    const passResult = {
      ops: result,
      applied,
      optimizations: [
        {
          name: "jump-thread",
          detail: `Threaded ${applied} jump(s) through trampoline labels to the final target.`
        }
      ]
    };
    return passResult;
  };
  var jumpThread = {
    name: "jump-thread",
    run: run8,
    layoutSafe: false
  };

  // src/core/passes/jump-to-next.ts
  var run9 = (ops) => {
    const result = [];
    let applied = 0;
    for (let i = 0; i < ops.length; i += 1) {
      const current = ops[i];
      if (current.kind === "jump" && typeof current.target === "string" && !hasRewriteBarrier(current)) {
        let cursor = i + 1;
        let threaded = false;
        while (cursor < ops.length && ops[cursor].kind === "label") {
          const label = ops[cursor];
          if (label.kind === "label" && label.name === current.target) {
            threaded = true;
            break;
          }
          cursor += 1;
        }
        if (threaded) {
          applied += 1;
          continue;
        }
      }
      result.push(current);
    }
    const passResult = {
      ops: result,
      applied,
      optimizations: applied > 0 ? [
        {
          name: "jump-to-next-threading",
          detail: `Removed ${applied} unconditional branch to the immediately following label.`
        }
      ] : []
    };
    return passResult;
  };
  var jumpToNextThreading = {
    name: "jump-to-next-threading",
    run: run9,
    layoutSafe: false
  };

  // src/core/passes/last-x-reuse.ts
  function clobbersX(op) {
    switch (op.kind) {
      case "label":
      case "orphan-address":
        return false;
      case "store":
        return false;
      case "recall":
      case "indirect-recall":
        return true;
      case "indirect-store":
        return false;
      case "plain":
        return true;
      case "stop":
        return false;
      case "jump":
      case "cjump":
      case "call":
      case "loop":
      case "indirect-jump":
      case "indirect-call":
      case "indirect-cjump":
      case "return":
        return false;
    }
  }
  var run10 = (ops) => {
    const removed = /* @__PURE__ */ new Set();
    let xHolds;
    for (let i = 0; i < ops.length; i += 1) {
      const op = ops[i];
      if (op.kind === "label") {
        xHolds = void 0;
        continue;
      }
      if (op.kind === "store") {
        if (!hasRewriteBarrier(op)) xHolds = op.register;
        else xHolds = void 0;
        continue;
      }
      if (op.kind === "recall" && !hasRewriteBarrier(op) && xHolds === op.register) {
        removed.add(i);
        continue;
      }
      if (op.kind === "jump" || op.kind === "cjump" || op.kind === "call" || op.kind === "loop" || op.kind === "indirect-jump" || op.kind === "indirect-call" || op.kind === "indirect-cjump" || op.kind === "return" || op.kind === "stop") {
        xHolds = void 0;
        continue;
      }
      if (clobbersX(op)) xHolds = void 0;
    }
    if (removed.size === 0) {
      return { ops: [...ops], applied: 0, optimizations: [] };
    }
    const result = [];
    for (let i = 0; i < ops.length; i += 1) {
      if (!removed.has(i)) result.push(ops[i]);
    }
    const passResult = {
      ops: result,
      applied: removed.size,
      optimizations: [
        {
          name: "last-x-reuse",
          detail: `Dropped ${removed.size} recall(s) whose register value was already in X.`
        }
      ]
    };
    return passResult;
  };
  var lastXReuse = {
    name: "last-x-reuse",
    run: run10,
    layoutSafe: false
  };

  // src/core/passes/preloaded-indirect-flow.ts
  var INDIRECT_COND_BASES3 = {
    "!=0": 112,
    ">=0": 144,
    "<0": 192,
    "==0": 224
  };
  var STABLE_REGISTERS = ["7", "8", "9", "a", "b", "c", "d", "e"];
  function cloneMeta2(meta, comment) {
    return {
      ...meta,
      comment: [meta.comment, comment].filter(Boolean).join("; ")
    };
  }
  function usedRegisters(ops) {
    const used = /* @__PURE__ */ new Set();
    for (const op of ops) {
      if (op.kind === "store" || op.kind === "recall" || op.kind === "indirect-store" || op.kind === "indirect-recall" || op.kind === "indirect-jump" || op.kind === "indirect-call" || op.kind === "indirect-cjump") {
        used.add(op.register);
      }
    }
    return used;
  }
  function spareStableRegisters(ops) {
    const used = usedRegisters(ops);
    return STABLE_REGISTERS.filter((register) => !used.has(register));
  }
  function numericFlowTarget(op) {
    if (op.kind !== "jump" && op.kind !== "call" && op.kind !== "cjump" && op.kind !== "loop") return void 0;
    if (typeof op.target !== "number") return void 0;
    if (!Number.isInteger(op.target) || op.target < 0 || op.target > 104) return void 0;
    return op.target;
  }
  function branchTarget(op) {
    if (op.kind !== "jump" && op.kind !== "call" && op.kind !== "cjump") return void 0;
    if (op.targetMeta.formalOpcode !== void 0 || op.targetMeta.roles?.includes("formal-address")) return void 0;
    return numericFlowTarget(op);
  }
  function maxNumericFlowTarget(ops) {
    let max = -1;
    for (const op of ops) {
      const target = numericFlowTarget(op);
      if (target !== void 0 && target > max) max = target;
    }
    return max;
  }
  function addressByIndex(ops) {
    const addresses = [];
    let address = 0;
    for (const op of ops) {
      addresses.push(address);
      address += cellsPerOp(op);
    }
    return addresses;
  }
  function opAtAddress(ops, addresses, address) {
    for (let i = 0; i < ops.length; i += 1) {
      const op = ops[i];
      if (cellsPerOp(op) === 0) continue;
      if (addresses[i] === address) return op;
    }
    return void 0;
  }
  function hasSingleCellFallthrough(op) {
    if (op === void 0 || cellsPerOp(op) !== 1) return false;
    switch (op.kind) {
      case "plain":
      case "store":
      case "recall":
      case "indirect-store":
      case "indirect-recall":
      case "stop":
        return true;
      default:
        return false;
    }
  }
  function isSuperDarkCompatibleTarget(ops, addresses, labels, target) {
    if (target < 48 || target > 53) return false;
    const entry = opAtAddress(ops, addresses, target);
    if (!hasSingleCellFallthrough(entry)) return false;
    const continuationAddress = target - 47;
    const afterEntry = opAtAddress(ops, addresses, target + 1);
    if (afterEntry?.kind !== "jump") return false;
    return targetAddress(afterEntry.target, labels) === continuationAddress;
  }
  function selectorForTarget(ops, addresses, labels, target) {
    if (isSuperDarkCompatibleTarget(ops, addresses, labels, target)) {
      return {
        selectorValue: formalLabelFromOpcode(250 + (target - 48)),
        superDark: true
      };
    }
    if (target <= 47) return { selectorValue: formalLabelFromOrdinal(target + 112), superDark: false };
    return { selectorValue: officialLabel(target), superDark: false };
  }
  function formalLabelFromOrdinal(ordinal) {
    const high = Math.floor(ordinal / 10);
    const low = ordinal % 10;
    return `${high.toString(16).toUpperCase()}${low.toString(16).toUpperCase()}`;
  }
  function formalLabelFromOpcode(opcode) {
    const high = Math.floor(opcode / 16);
    const low = opcode % 16;
    return `${high.toString(16).toUpperCase()}${low.toString(16).toUpperCase()}`;
  }
  function officialLabel(target) {
    if (target <= 99) {
      return `${Math.floor(target / 10)}${target % 10}`;
    }
    return `A${target - 100}`;
  }
  function indirectFlowOp2(op, register, selectorValue, target, superDark) {
    const offset = registerIndex(register);
    const suffix = `preloaded R${register}=${selectorValue} indirect-target=${target}${superDark ? " super-dark" : ""} indirect flow`;
    if (op.kind === "jump") {
      return {
        kind: "indirect-jump",
        register,
        opcode: 128 + offset,
        meta: cloneMeta2({ ...op.meta, mnemonic: `\u041A \u0411\u041F ${register}` }, suffix)
      };
    }
    if (op.kind === "call") {
      return {
        kind: "indirect-call",
        register,
        opcode: 160 + offset,
        meta: cloneMeta2({ ...op.meta, mnemonic: `\u041A \u041F\u041F ${register}` }, suffix)
      };
    }
    const opcode = INDIRECT_COND_BASES3[op.condition] + offset;
    const name = op.condition === "==0" ? "x=0" : op.condition === "!=0" ? "x!=0" : `x${op.condition}`;
    return {
      kind: "indirect-cjump",
      condition: op.condition,
      register,
      opcode,
      meta: cloneMeta2({ ...op.meta, mnemonic: `\u041A ${name} ${register}` }, suffix)
    };
  }
  var run11 = (ops) => {
    const registers = spareStableRegisters(ops);
    if (registers.length === 0) return { ops: [...ops], applied: 0, optimizations: [] };
    const addresses = addressByIndex(ops);
    const labels = calculateLabelAddresses(ops);
    const maxTarget = maxNumericFlowTarget(ops);
    const targets = /* @__PURE__ */ new Map();
    const preloads = [];
    const result = [];
    let applied = 0;
    let superDarkApplied = 0;
    for (let index = 0; index < ops.length; index += 1) {
      const op = ops[index];
      if (hasRewriteBarrier(op) || op.kind !== "jump" && op.kind !== "call" && op.kind !== "cjump") {
        result.push(op);
        continue;
      }
      const target = branchTarget(op);
      const siteAddress = addresses[index];
      if (target === void 0 || target > siteAddress || maxTarget > siteAddress) {
        result.push(op);
        continue;
      }
      let selected = targets.get(target);
      if (selected === void 0) {
        const register = registers.shift();
        if (register === void 0) {
          result.push(op);
          continue;
        }
        const { selectorValue, superDark } = selectorForTarget(ops, addresses, labels, target);
        const evaluated = evaluateIndirectAddress(register, selectorValue, "flow");
        const selectedSuperDark = evaluated?.superDark?.entryAddress === target;
        if (evaluated?.actualFlowTarget !== target || selectedSuperDark !== superDark || !isStableIndirectSelector(register)) {
          result.push(op);
          continue;
        }
        selected = { register, selectorValue, superDark };
        targets.set(target, selected);
        preloads.push({ register, value: selectorValue, countsAgainstProgram: false });
      }
      result.push(indirectFlowOp2(op, selected.register, selected.selectorValue, target, selected.superDark));
      applied += 1;
      if (selected.superDark) superDarkApplied += 1;
    }
    if (applied === 0) return { ops: [...ops], applied: 0, optimizations: [] };
    const formal = preloads.filter((preload) => /[B-F]/iu.test(preload.value)).length;
    const optimizations = [
      {
        name: "preloaded-indirect-flow",
        detail: `Replaced ${applied} numeric direct branch/call(s) with compiler-owned preloaded indirect flow (${formal} formal alias selector${formal === 1 ? "" : "s"}).`
      }
    ];
    if (superDarkApplied > 0) {
      optimizations.push({
        name: "preloaded-super-dark-flow",
        detail: `Selected ${superDarkApplied} FA..FF one-command indirect dispatch(es) after proving the entry cell falls through to the matching 01..06 continuation jump.`
      });
    }
    return {
      ops: result,
      applied,
      preloads,
      optimizations
    };
  };
  var preloadedIndirectFlow = {
    name: "preloaded-indirect-flow",
    run: run11,
    layoutSafe: false
  };

  // src/core/passes/redundant-prologue.ts
  function isShowDisplayOp(op) {
    if (op.kind === "recall") return true;
    if (op.kind === "plain") {
      if (op.opcode === 16 || op.opcode === 18) return true;
      if (op.opcode <= 10) return true;
      if (op.opcode === 14) return true;
    }
    return false;
  }
  function isShowStop(op) {
    return op.kind === "stop" && (op.semantic === "show" || op.semantic === "halt");
  }
  function collectForwardPrologue(ops, from) {
    const collected = [];
    let i = from;
    while (i < ops.length && ops[i].kind === "label") i += 1;
    while (i < ops.length) {
      const op = ops[i];
      if (op.kind === "label") {
        i += 1;
        continue;
      }
      if (isShowDisplayOp(op) && !hasRewriteBarrier(op)) {
        collected.push(op);
        i += 1;
        continue;
      }
      if (isShowStop(op) && !hasRewriteBarrier(op)) {
        collected.push(op);
        return { ops: collected };
      }
      return { ops: [] };
    }
    return { ops: [] };
  }
  function collectBackwardPrologue(ops, beforeIndex) {
    const collected = [];
    let i = beforeIndex - 1;
    let sawStop = false;
    while (i >= 0) {
      const op = ops[i];
      if (op.kind === "label") {
        i -= 1;
        continue;
      }
      if (!sawStop) {
        if (isShowStop(op) && !hasRewriteBarrier(op)) {
          collected.push(op);
          sawStop = true;
          i -= 1;
          continue;
        }
        return { ops: [], startIndex: -1 };
      }
      if (isShowDisplayOp(op) && !hasRewriteBarrier(op)) {
        collected.push(op);
        i -= 1;
        continue;
      }
      break;
    }
    if (!sawStop) return { ops: [], startIndex: -1 };
    let startIndex = i + 1;
    while (startIndex < beforeIndex && ops[startIndex].kind === "label") startIndex += 1;
    let virtualHeadRegister;
    let scan = i;
    while (scan >= 0 && ops[scan].kind === "label") scan -= 1;
    if (scan >= 0) {
      const prior = ops[scan];
      if (prior.kind === "store" && !hasRewriteBarrier(prior)) {
        virtualHeadRegister = prior.register;
      }
    }
    const segment = {
      ops: collected.slice().reverse(),
      startIndex
    };
    if (virtualHeadRegister !== void 0) return { ...segment, virtualHeadRegister };
    return segment;
  }
  function opsEquivalent2(a, b) {
    if (a.kind !== b.kind) return false;
    if (a.kind === "recall" && b.kind === "recall") return a.register === b.register;
    if (a.kind === "plain" && b.kind === "plain") return a.opcode === b.opcode;
    if (a.kind === "stop" && b.kind === "stop") return a.semantic === b.semantic;
    return false;
  }
  function segmentsMatch(a, b) {
    if (a.length === 0 || a.length !== b.length) return false;
    for (let i = 0; i < a.length; i += 1) {
      if (!opsEquivalent2(a[i], b[i])) return false;
    }
    return true;
  }
  var run12 = (ops) => {
    const labelIndex = /* @__PURE__ */ new Map();
    for (let i = 0; i < ops.length; i += 1) {
      const op = ops[i];
      if (op.kind === "label") labelIndex.set(op.name, i);
    }
    const removeRanges = [];
    let applied = 0;
    for (let i = 0; i < ops.length; i += 1) {
      const op = ops[i];
      if (op.kind !== "jump") continue;
      if (typeof op.target !== "string") continue;
      if (hasRewriteBarrier(op)) continue;
      const labelAt = labelIndex.get(op.target);
      if (labelAt === void 0) continue;
      const headForward = collectForwardPrologue(ops, labelAt);
      if (headForward.ops.length === 0) continue;
      const headBackward = collectBackwardPrologue(ops, i);
      if (headBackward.ops.length === 0) continue;
      const backwardOps = headBackward.ops;
      let matched = segmentsMatch(backwardOps, headForward.ops);
      if (!matched && headBackward.virtualHeadRegister !== void 0) {
        const firstForward = headForward.ops[0];
        if (firstForward !== void 0 && firstForward.kind === "recall" && firstForward.register === headBackward.virtualHeadRegister) {
          const suffix = headForward.ops.slice(1);
          if (segmentsMatch(backwardOps, suffix)) {
            matched = true;
          }
        }
      }
      if (!matched) continue;
      const start = headBackward.startIndex;
      const end = i;
      const forwardEnd = labelAt + headForward.ops.length;
      if (start <= forwardEnd) continue;
      let hasIntermediateContent = false;
      for (let k = forwardEnd + 1; k < start; k += 1) {
        const intermediate = ops[k];
        if (intermediate.kind === "label") continue;
        hasIntermediateContent = true;
        break;
      }
      if (!hasIntermediateContent) continue;
      const overlaps = removeRanges.some(
        (range2) => !(end <= range2.start || start >= range2.end)
      );
      if (overlaps) continue;
      removeRanges.push({ start, end });
      applied += 1;
    }
    if (applied === 0) {
      return { ops: [...ops], applied: 0, optimizations: [] };
    }
    const shouldRemove = new Array(ops.length).fill(false);
    for (const range2 of removeRanges) {
      for (let i = range2.start; i < range2.end; i += 1) shouldRemove[i] = true;
    }
    const result = [];
    for (let i = 0; i < ops.length; i += 1) {
      const op = ops[i];
      if (!shouldRemove[i] || op.kind === "label") result.push(op);
    }
    const totalCells = removeRanges.reduce((acc, range2) => acc + (range2.end - range2.start), 0);
    return {
      ops: result,
      applied,
      optimizations: [
        {
          name: "redundant-prologue-elimination",
          detail: `Removed ${applied} display/halt prologue(s) immediately before a jump to their identical loop head (${totalCells} cells).`
        }
      ]
    };
  };
  var redundantPrologueElimination = {
    name: "redundant-prologue-elimination",
    run: run12,
    layoutSafe: false
  };

  // src/core/passes/register-coalesce.ts
  var DIRECT_STORE_BASE2 = 64;
  var DIRECT_RECALL_BASE2 = 96;
  var LOOP_COUNTER_REGISTER = {
    L0: "0",
    L1: "1",
    L2: "2",
    L3: "3"
  };
  function gatherUsedRegisters(ops) {
    const set = /* @__PURE__ */ new Set();
    for (const op of ops) {
      if (op.kind === "store" || op.kind === "recall") set.add(op.register);
      if (op.kind === "indirect-store" || op.kind === "indirect-recall" || op.kind === "indirect-jump" || op.kind === "indirect-call" || op.kind === "indirect-cjump") {
        set.add(op.register);
        const memoryTarget = knownIndirectMemoryTarget(op);
        if (memoryTarget !== void 0) set.add(memoryTarget);
      }
      if (op.kind === "loop") set.add(LOOP_COUNTER_REGISTER[op.counter]);
    }
    return set;
  }
  function liveRangePerRegister(ops, registers) {
    const liveness = computeLiveness(ops);
    const ranges = /* @__PURE__ */ new Map();
    for (const reg of registers) ranges.set(reg, /* @__PURE__ */ new Set());
    for (let i = 0; i < ops.length; i += 1) {
      for (const reg of liveness.liveIn[i]) {
        ranges.get(reg)?.add(i);
      }
      for (const reg of liveness.liveOut[i]) {
        ranges.get(reg)?.add(i);
      }
    }
    return ranges;
  }
  function intersects(a, b) {
    const smaller = a.size < b.size ? a : b;
    const larger = a.size < b.size ? b : a;
    for (const value of smaller) {
      if (larger.has(value)) return true;
    }
    return false;
  }
  function usesIndirectAccess(ops, register) {
    for (const op of ops) {
      if (op.kind === "indirect-store" || op.kind === "indirect-recall" || op.kind === "indirect-jump" || op.kind === "indirect-call" || op.kind === "indirect-cjump") {
        if (op.register === register) return true;
        if (knownIndirectMemoryTarget(op) === register) return true;
      }
    }
    return false;
  }
  function usesDisplayFocusSensitiveAccess(ops, register) {
    return ops.some(
      (op) => (op.kind === "store" || op.kind === "recall") && op.register === register && isDisplayFocusSensitive(op)
    );
  }
  function usesLoopCounter(ops, register) {
    return ops.some((op) => op.kind === "loop" && LOOP_COUNTER_REGISTER[op.counter] === register);
  }
  function unionInto(target, source) {
    for (const value of source) target.add(value);
  }
  function coalescedOpcode(kind, register) {
    const base = kind === "store" ? DIRECT_STORE_BASE2 : DIRECT_RECALL_BASE2;
    return base + registerIndex(register);
  }
  function rewriteRegisterOp(op, register) {
    if (op.kind !== "store" && op.kind !== "recall") return op;
    const opcode = coalescedOpcode(op.kind, register);
    return {
      ...op,
      register,
      opcode,
      meta: {
        ...op.meta,
        mnemonic: getOpcode(opcode).name
      }
    };
  }
  var run13 = (ops) => {
    if (ops.length === 0) return { ops: [], applied: 0, optimizations: [] };
    if (ops.some((op) => hasRewriteBarrier(op))) {
      return { ops: [...ops], applied: 0, optimizations: [] };
    }
    const registers = gatherUsedRegisters(ops);
    if (registers.size <= 1) {
      return { ops: [...ops], applied: 0, optimizations: [] };
    }
    const liveness = computeLiveness(ops);
    const liveAtEntry = liveness.liveIn[0] ?? /* @__PURE__ */ new Set();
    const ranges = liveRangePerRegister(ops, registers);
    const ordered = [...registers].sort();
    const mapping = /* @__PURE__ */ new Map();
    for (let i = 0; i < ordered.length; i += 1) {
      const a = ordered[i];
      if (mapping.has(a)) continue;
      if (liveAtEntry.has(a)) continue;
      if (usesIndirectAccess(ops, a)) continue;
      if (usesDisplayFocusSensitiveAccess(ops, a)) continue;
      if (usesLoopCounter(ops, a)) continue;
      for (let j = i + 1; j < ordered.length; j += 1) {
        const b = ordered[j];
        if (mapping.has(b)) continue;
        if (liveAtEntry.has(b)) continue;
        if (usesIndirectAccess(ops, b)) continue;
        if (usesDisplayFocusSensitiveAccess(ops, b)) continue;
        if (usesLoopCounter(ops, b)) continue;
        const rangeA = ranges.get(a);
        const rangeB = ranges.get(b);
        if (intersects(rangeA, rangeB)) continue;
        mapping.set(b, a);
        unionInto(rangeA, rangeB);
        break;
      }
    }
    if (mapping.size === 0) {
      return { ops: [...ops], applied: 0, optimizations: [] };
    }
    const result = ops.map((op) => {
      if (op.kind !== "store" && op.kind !== "recall") return op;
      const replacement = mapping.get(op.register);
      return replacement === void 0 ? op : rewriteRegisterOp(op, replacement);
    });
    return {
      ops: result,
      applied: mapping.size,
      optimizations: [
        {
          name: "register-coalesce",
          detail: `Coalesced ${mapping.size} non-overlapping register live range(s).`
        }
      ]
    };
  };
  var registerCoalesce = {
    name: "register-coalesce",
    run: run13,
    layoutSafe: true
  };

  // src/core/passes/return-zero-jump.ts
  var run14 = (ops) => {
    const usesCall = ops.some((op) => op.kind === "call" || op.kind === "indirect-call");
    if (usesCall) return { ops: [...ops], applied: 0, optimizations: [] };
    const labels = calculateLabelAddresses(ops);
    const result = [];
    let applied = 0;
    let currentAddress = 0;
    for (const op of ops) {
      if (op.kind === "label") {
        result.push(op);
        continue;
      }
      if (op.kind === "jump" && !hasRewriteBarrier(op)) {
        const resolved = targetAddress(op.target, labels);
        const targetsBackward = typeof op.target === "number" ? true : resolved !== void 0 && resolved < currentAddress;
        if (resolved === 1 && targetsBackward) {
          result.push({
            kind: "return",
            opcode: 82,
            meta: {
              mnemonic: "\u0412/\u041E",
              comment: "optimized \u0411\u041F 01"
            }
          });
          applied += 1;
          currentAddress += 1;
          continue;
        }
      }
      result.push(op);
      currentAddress += cellsPerOp(op);
    }
    const passResult = {
      ops: result,
      applied,
      optimizations: applied > 0 ? [
        {
          name: "return-zero-jump",
          detail: `Replaced ${applied} \u0411\u041F 01 sequence with \u0412/\u041E under empty-return-stack assumption.`
        }
      ] : []
    };
    return passResult;
  };
  var returnZeroJump = {
    name: "return-zero-jump",
    run: run14,
    layoutSafe: false
  };

  // src/core/passes/r0-fractional-sentinel.ts
  function isFractionalR0LiteralBeforeStore(ops, storeIndex) {
    let index = storeIndex - 1;
    let hasNonZeroFractionDigit = false;
    while (index >= 0) {
      const digit = ops[index];
      if (digit?.kind !== "plain" || digit.opcode < 0 || digit.opcode > 9) break;
      if (digit.opcode > 0) hasNonZeroFractionDigit = true;
      index -= 1;
    }
    const dot = ops[index];
    const zero = ops[index - 1];
    if (!hasNonZeroFractionDigit || dot?.kind !== "plain" || dot.opcode !== 10) return false;
    if (zero === void 0) return true;
    return zero.kind === "plain" && zero.opcode === 0;
  }
  var run15 = (ops) => {
    if (ops.length === 0) return { ops: [], applied: 0, optimizations: [] };
    const liveness = computeLiveness(ops);
    const remove = /* @__PURE__ */ new Set();
    let r0Fractional = false;
    for (let i = 0; i < ops.length; i += 1) {
      const op = ops[i];
      if (hasRewriteBarrier(op)) {
        r0Fractional = false;
        continue;
      }
      if (op.kind === "store" && op.register === "0") {
        r0Fractional = isFractionalR0LiteralBeforeStore(ops, i);
        continue;
      }
      if (op.kind === "plain" || op.kind === "recall" || op.kind === "label") {
        continue;
      }
      if (!r0Fractional) continue;
      if (op.kind === "indirect-recall" && op.register === "0") {
        const next = ops[i + 1];
        if (next?.kind === "recall" && next.register === "3" && !liveness.liveOut[i].has("0")) {
          remove.add(i + 1);
        }
        r0Fractional = false;
        continue;
      }
      if (op.kind === "indirect-store" && op.register === "0") {
        const next = ops[i + 1];
        if (next?.kind === "store" && next.register === "3" && !liveness.liveOut[i].has("0")) {
          remove.add(i + 1);
        }
        r0Fractional = false;
        continue;
      }
      if (op.kind !== "store" || op.register !== "0") r0Fractional = false;
    }
    if (remove.size === 0) return { ops: [...ops], applied: 0, optimizations: [] };
    return {
      ops: ops.filter((_, index) => !remove.has(index)),
      applied: remove.size,
      optimizations: [
        {
          name: "r0-fractional-sentinel",
          detail: `Removed ${remove.size} redundant direct R3 access(es) after fractional-R0 indirect access.`
        }
      ]
    };
  };
  var r0FractionalSentinel = {
    name: "r0-fractional-sentinel",
    run: run15,
    layoutSafe: false
  };

  // src/core/passes/store-recall-peephole.ts
  var run16 = (ops) => {
    const result = [];
    let applied = 0;
    for (let i = 0; i < ops.length; i += 1) {
      const current = ops[i];
      const next = ops[i + 1];
      if (current.kind === "store" && next?.kind === "recall" && current.register === next.register && !hasRewriteBarrier(current) && !hasRewriteBarrier(next)) {
        result.push(current);
        applied += 1;
        i += 1;
        continue;
      }
      result.push(current);
    }
    const passResult = {
      ops: result,
      applied,
      optimizations: applied > 0 ? [
        {
          name: "store-recall-peephole",
          detail: `Dropped ${applied} redundant \u041F->X immediately after X->\u041F to the same register.`
        }
      ] : []
    };
    return passResult;
  };
  var storeRecallPeephole = {
    name: "store-recall-peephole",
    run: run16,
    layoutSafe: false
  };

  // src/core/passes/tail-call.ts
  var run17 = (ops) => {
    const tailJumpTargets = findTailJumpTargets(ops);
    const returnContinuations = collectReturnContinuations(ops, tailJumpTargets);
    const returnLabels = collectReturnLabels(ops);
    const result = [];
    let applied = 0;
    for (let index = 0; index < ops.length; index += 1) {
      const op = ops[index];
      if (op.kind === "label") {
        result.push(op);
        continue;
      }
      const next = ops[index + 1];
      if (op.kind === "return") {
        const continuation = returnContinuations.get(index);
        if (continuation !== void 0) {
          const meta = {
            mnemonic: "\u0411\u041F",
            comment: op.meta.comment?.replace(/^implicit return from proc/u, "tail continuation") ?? "tail continuation"
          };
          if (op.meta.sourceLine !== void 0) meta.sourceLine = op.meta.sourceLine;
          result.push({
            kind: "jump",
            target: continuation,
            opcode: 81,
            meta,
            targetMeta: { comment: "tail continuation" }
          });
          applied += 1;
          continue;
        }
      }
      if (op.kind === "call") {
        const continuationIndex = nextExecutableIndex(ops, index + 1);
        const continuation = continuationIndex === void 0 ? void 0 : ops[continuationIndex];
        const continuationIsImmediate = continuationIndex === index + 1;
        if (continuation?.kind === "jump" && isReturnLabel(continuation.target, returnLabels)) {
          result.push({
            kind: "jump",
            target: op.target,
            opcode: 81,
            meta: {
              ...op.meta,
              mnemonic: "\u0411\u041F",
              comment: op.meta.comment?.replace(/^proc call/u, "tail call") ?? "tail call"
            },
            targetMeta: { ...op.targetMeta }
          });
          if (continuationIsImmediate) index += 1;
          applied += 1;
          continue;
        }
        if (continuation?.kind === "return") {
          result.push({
            kind: "jump",
            target: op.target,
            opcode: 81,
            meta: {
              ...op.meta,
              mnemonic: "\u0411\u041F",
              comment: op.meta.comment?.replace(/^proc call/u, "tail call") ?? "tail call"
            },
            targetMeta: { ...op.targetMeta }
          });
          if (continuationIsImmediate) index += 1;
          applied += 1;
          continue;
        }
        const target = typeof op.target === "string" ? tailJumpTargets.get(op.target) : void 0;
        if (target !== void 0 && next?.kind === "jump" && sameTarget(next.target, target.continuation)) {
          result.push({
            kind: "jump",
            target: op.target,
            opcode: 81,
            meta: {
              ...op.meta,
              mnemonic: "\u0411\u041F",
              comment: op.meta.comment?.replace(/^proc call/u, "tail jump") ?? "tail jump"
            },
            targetMeta: { ...op.targetMeta }
          });
          index += 1;
          applied += 1;
          continue;
        }
        if (target !== void 0 && next?.kind === "label" && sameTarget(next.name, target.continuation)) {
          result.push({
            kind: "jump",
            target: op.target,
            opcode: 81,
            meta: {
              ...op.meta,
              mnemonic: "\u0411\u041F",
              comment: op.meta.comment?.replace(/^proc call/u, "tail jump") ?? "tail jump"
            },
            targetMeta: { ...op.targetMeta }
          });
          applied += 1;
          continue;
        }
      }
      result.push(op);
    }
    if (applied === 0) return { ops: [...ops], applied: 0, optimizations: [] };
    const tailJumpCount = tailJumpTargets.size;
    return {
      ops: result,
      applied,
      optimizations: [{
        name: "tail-call-lowering",
        detail: tailJumpCount === 0 ? `Replaced ${applied} subroutine tail call${applied === 1 ? "" : "s"} with direct jump(s).` : `Replaced ${applied} subroutine tail operation${applied === 1 ? "" : "s"} with direct jump continuation${tailJumpCount === 1 ? "" : "s"}.`
      }]
    };
  };
  var tailCallLowering = {
    name: "tail-call-lowering",
    run: run17,
    layoutSafe: false
  };
  function findTailJumpTargets(ops) {
    const calls = /* @__PURE__ */ new Map();
    const nonCallFlowTargets = /* @__PURE__ */ new Set();
    for (let index = 0; index < ops.length; index += 1) {
      const op = ops[index];
      if (op.kind === "call" && typeof op.target === "string") {
        const continuation = callContinuation(ops, index);
        const existing = calls.get(op.target) ?? [];
        existing.push(continuation);
        calls.set(op.target, existing);
        continue;
      }
      if ((op.kind === "jump" || op.kind === "cjump" || op.kind === "loop") && typeof op.target === "string") {
        nonCallFlowTargets.add(op.target);
      }
    }
    const regions = collectCallableRegions(ops, new Set(calls.keys()));
    const result = /* @__PURE__ */ new Map();
    for (const [target, continuations] of calls) {
      if (nonCallFlowTargets.has(target)) continue;
      const region = regions.get(target);
      if (region === void 0 || !blockHasReturn(ops, region.start, region.end)) continue;
      const first = continuations[0];
      if (first === void 0) continue;
      if (continuations.every((continuation) => continuation !== void 0 && sameTarget(continuation, first))) {
        result.set(target, { continuation: first, start: region.start, end: region.end });
      }
    }
    return result;
  }
  function collectCallableRegions(ops, callTargets) {
    const result = /* @__PURE__ */ new Map();
    let current;
    for (let index = 0; index < ops.length; index += 1) {
      const op = ops[index];
      if (op.kind !== "label") continue;
      if (!callTargets.has(op.name)) continue;
      if (current !== void 0) result.set(current.name, { start: current.start, end: index });
      current = { name: op.name, start: index + 1 };
    }
    if (current !== void 0) result.set(current.name, { start: current.start, end: ops.length });
    return result;
  }
  function collectReturnContinuations(ops, targets) {
    const result = /* @__PURE__ */ new Map();
    for (const target of targets.values()) {
      for (let index = target.start; index < target.end; index += 1) {
        if (ops[index]?.kind === "return") result.set(index, target.continuation);
      }
    }
    return result;
  }
  function collectReturnLabels(ops) {
    const result = /* @__PURE__ */ new Set();
    for (let index = 0; index < ops.length; index += 1) {
      const op = ops[index];
      if (op.kind !== "label") continue;
      const next = nextExecutableIndex(ops, index + 1);
      if (next !== void 0 && ops[next]?.kind === "return") result.add(op.name);
    }
    return result;
  }
  function nextExecutableIndex(ops, start) {
    for (let index = start; index < ops.length; index += 1) {
      if (ops[index]?.kind !== "label") return index;
    }
    return void 0;
  }
  function isReturnLabel(target, returnLabels) {
    return typeof target === "string" && returnLabels.has(target);
  }
  function blockHasReturn(ops, start, end) {
    for (let index = start; index < end; index += 1) {
      if (ops[index]?.kind === "return") return true;
    }
    return false;
  }
  function callContinuation(ops, index) {
    const next = ops[index + 1];
    if (next?.kind === "jump") return next.target;
    if (next?.kind === "label") return next.name;
    return void 0;
  }
  function sameTarget(left, right) {
    return left === right;
  }

  // src/core/passes/vp-x2-peephole.ts
  function displayBoundaryText(op) {
    if (!("meta" in op)) return "";
    return [
      op.meta.comment,
      "tactic" in op.meta ? op.meta.tactic : void 0
    ].filter(Boolean).join(" ").toLowerCase();
  }
  function isDisplayVp(op) {
    if (hasRewriteBarrier(op)) return false;
    return op.kind === "plain" && op.opcode === 12 && /display|x2|вп/u.test(displayBoundaryText(op));
  }
  function isFractionAfterDisplayBoundary(op) {
    if (hasRewriteBarrier(op)) return false;
    return op.kind === "plain" && op.opcode === 53 && /display|x2|frac/u.test(displayBoundaryText(op));
  }
  var run18 = (ops) => {
    const remove = /* @__PURE__ */ new Set();
    for (let i = 1; i < ops.length; i += 1) {
      if (isDisplayVp(ops[i - 1]) && isFractionAfterDisplayBoundary(ops[i])) {
        remove.add(i);
      }
    }
    if (remove.size === 0) return { ops: [...ops], applied: 0, optimizations: [] };
    return {
      ops: ops.filter((_, index) => !remove.has(index)),
      applied: remove.size,
      optimizations: [
        {
          name: "vp-fraction-restore",
          detail: `Removed ${remove.size} \u041A {x} op(s) already supplied by a \u0412\u041F/X2 display boundary.`
        }
      ]
    };
  };
  var vpX2Peephole = {
    name: "vp-x2-peephole",
    run: run18,
    layoutSafe: false
  };

  // src/core/passes/index.ts
  var PASS_PIPELINE = [
    redundantPrologueElimination,
    tailCallLowering,
    returnZeroJump,
    storeRecallPeephole,
    jumpToNextThreading,
    jumpThread,
    stableIndirectFlow,
    preloadedIndirectFlow,
    indirectMemoryTable,
    deadStoreBeforeCommutative,
    deadStoreElimination,
    lastXReuse,
    r0FractionalSentinel,
    vpX2Peephole,
    constantFolding,
    duplicateFailureTail,
    cseDisplayBlock,
    deadCodeAfterHalt,
    registerCoalesce,
    arithmeticIfPass
  ];
  var MAX_FIXPOINT_ITERATIONS = 8;
  function runPassesOnIr(initial, options, { layoutOnly }) {
    let current = initial;
    let totalApplied = 0;
    const optimizations = [];
    const preloads = [];
    const passCounts = {};
    let changedInIteration = true;
    let iteration = 0;
    while (changedInIteration && iteration < MAX_FIXPOINT_ITERATIONS) {
      changedInIteration = false;
      iteration += 1;
      for (const pass of PASS_PIPELINE) {
        if (layoutOnly && !pass.layoutSafe) continue;
        const result = pass.run(current, { options });
        passCounts[pass.name] = (passCounts[pass.name] ?? 0) + result.applied;
        if (result.applied > 0) {
          changedInIteration = true;
          totalApplied += result.applied;
          for (const opt of result.optimizations) {
            const existing = optimizations.find((entry) => entry.name === opt.name);
            if (existing !== void 0) {
              existing.detail = `${existing.detail} (+${opt.detail})`;
            } else {
              optimizations.push({ ...opt });
            }
          }
          if (result.preloads !== void 0) preloads.push(...result.preloads);
        }
        current = result.ops;
      }
    }
    return { ops: current, applied: totalApplied, optimizations, passCounts, preloads };
  }
  function runIrPasses(items, options) {
    const ir = raiseMachineToIr(items);
    const result = runPassesOnIr(ir, options, { layoutOnly: false });
    return {
      items: lowerIrToMachine(result.ops),
      applied: result.applied,
      optimizations: result.optimizations,
      passCounts: result.passCounts,
      preloads: result.preloads
    };
  }

  // src/core/super-dark-layout.ts
  function hex2(value) {
    return value.toString(16).toUpperCase().padStart(2, "0");
  }
  function verifySuperDarkSuffixLayout(layout, options = {}) {
    const byAddress = new Map(layout.map((cell) => [cell.address, cell]));
    const pairs = [];
    const dispatchCells = collectSuperDarkDispatchCells(layout, options.selectorValues ?? {});
    const provedDispatchCells = dispatchCells.filter((cell) => isSuperDarkSelectorValue(cell.selectorValue));
    const requiredOffsets = requiredSuperDarkOffsets(provedDispatchCells);
    const reasons = [];
    if (dispatchCells.length === 0) {
      reasons.push("no super-dark \u041A \u0411\u041F R dispatch cell is marked in the layout");
    } else if (provedDispatchCells.length === 0) {
      reasons.push("no super-dark dispatch register has a proved FA..FF selector value");
    }
    for (const offset of requiredOffsets) {
      const formal = 250 + offset;
      const entryAddress = 48 + offset;
      const continuationAddress = 1 + offset;
      const entry = byAddress.get(entryAddress);
      const continuation = byAddress.get(continuationAddress);
      if (entry === void 0) {
        reasons.push(`${hex2(formal)} has no physical entry cell at ${entryAddress}`);
        continue;
      }
      if (continuation === void 0) {
        reasons.push(`${hex2(formal)} has no continuation cell at ${continuationAddress}`);
        continue;
      }
      if (!entry.roles.includes("exec")) {
        reasons.push(`${hex2(formal)} entry ${entryAddress} is not executable`);
        continue;
      }
      if (getOpcode(entry.opcode).takesAddress) {
        reasons.push(`${hex2(formal)} entry ${entryAddress} is a two-cell address-taking command`);
        continue;
      }
      if (!continuation.roles.includes("exec")) {
        reasons.push(`${hex2(formal)} continuation ${continuationAddress} is not executable`);
        continue;
      }
      pairs.push({
        formal,
        entryAddress,
        continuationAddress,
        entryOpcode: entry.opcode,
        continuationOpcode: continuation.opcode
      });
    }
    if (pairs.length !== requiredOffsets.length && reasons.length === 0) {
      reasons.push("FA..FF did not produce the required super-dark entry/continuation pairs");
    }
    return {
      proved: provedDispatchCells.length > 0 && pairs.length === requiredOffsets.length && reasons.length === 0,
      pairs,
      dispatchCells,
      reasons
    };
  }
  function requiredSuperDarkOffsets(dispatchCells) {
    const offsets = /* @__PURE__ */ new Set();
    for (const cell of dispatchCells) {
      const value = cell.selectorValue?.trim().toUpperCase().replace(/\s+/gu, "");
      const formal = value === void 0 ? void 0 : /^F([A-F])$/u.exec(value);
      if (formal) {
        offsets.add(Number.parseInt(formal[1], 16) - 10);
      } else if (isSuperDarkSelectorValue(value)) {
        for (let offset = 0; offset <= 5; offset += 1) offsets.add(offset);
      }
    }
    return [...offsets].sort((a, b) => a - b);
  }
  function collectSuperDarkDispatchCells(layout, selectorValues) {
    const cells = [];
    for (const cell of layout) {
      if (cell.opcode < 135 || cell.opcode > 142) continue;
      if (!/\bsuper[- ]dark\b/iu.test(cell.tactic)) continue;
      const register = registerForIndirectJumpOpcode(cell.opcode);
      const selectorValue = selectorValueForRegister(selectorValues, register);
      cells.push({
        address: cell.address,
        opcode: cell.opcode,
        register,
        tactic: cell.tactic,
        ...selectorValue === void 0 ? {} : { selectorValue }
      });
    }
    return cells;
  }
  function registerForIndirectJumpOpcode(opcode) {
    const registers = ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"];
    return registers[opcode - 128] ?? "?";
  }
  function selectorValueForRegister(selectorValues, register) {
    const aliases = [
      register,
      register.toUpperCase(),
      `R${register}`,
      `R${register.toUpperCase()}`,
      `r${register}`
    ];
    for (const alias of aliases) {
      const value = selectorValues[alias];
      if (value !== void 0) return value;
    }
    return void 0;
  }
  function isSuperDarkSelectorValue(value) {
    if (value === void 0) return false;
    const normalized = value.trim().toUpperCase().replace(/\s+/gu, "");
    if (/^F[A-F]$/u.test(normalized)) return true;
    return normalized === "FA..FF" || normalized === "FA-FF" || normalized === "FA\u2026FF" || normalized === "SUPER-DARK" || normalized === "SUPER_DARK";
  }

  // src/core/machineProfile.ts
  var MK61_PROFILE = {
    id: "mk61",
    features: [
      {
        id: "branch-removal",
        source: "machine",
        detail: "Documented arithmetic, sign, extrema, and zero-test opcodes can replace provably equivalent branches."
      },
      {
        id: "fl-decrement-branch",
        source: "machine",
        detail: "F L0..F L3 are available for compact decrement-and-continue/decrement-and-branch forms."
      },
      {
        id: "return-empty-stack-jump",
        source: "machine",
        detail: "\u0412/\u041E can be used as \u0411\u041F 01 when static control-flow proves that no return frame is pending."
      },
      {
        id: "undocumented-opcodes",
        source: "machine",
        detail: "F0..FF and undocumented aliases are available when exact-machine preconditions are proved."
      },
      {
        id: "dark-entries",
        source: "machine",
        detail: "Formal dark/super-dark entry addresses are available to the layout solver."
      },
      {
        id: "super-dark-dispatch",
        source: "machine",
        detail: "Indirect \u041A \u0411\u041F R to FA..FF can execute one command at 48..53 and continue at 01..06."
      },
      {
        id: "indirect-flow",
        source: "machine",
        detail: "\u041A \u0411\u041F/\u041A \u041F\u041F/\u041A x?0 indirect flow commands are available to the optimizer."
      },
      {
        id: "indirect-memory",
        source: "machine",
        detail: "\u041A X->\u041F/\u041A \u041F->X indirect memory commands are available when a selector register is proved live."
      },
      {
        id: "code-data-overlay",
        source: "machine",
        detail: "Address operands, constants, opcodes, and display bytes may share cells after conflict checks."
      },
      {
        id: "address-constants",
        source: "machine",
        detail: "Address cells may double as constants for indirect flow and data transforms."
      },
      {
        id: "display-bytes",
        source: "machine",
        detail: "Packed display bytes, hexadecimal mantissa digits, and sign-digit forms are available."
      },
      {
        id: "x2-register",
        source: "machine",
        detail: "The hidden X2 display register can be scheduled when observable display semantics are preserved."
      },
      {
        id: "negative-zero-degree",
        source: "machine",
        detail: "Negative-zero exponent values such as 1|-00 can act as constants or threshold sentinels when X2-normalization boundaries are controlled."
      },
      {
        id: "extra-cells",
        source: "machine",
        detail: "Extra physical cells are tracked separately from official program cells."
      },
      {
        id: "error-stops",
        source: "machine",
        detail: "Domain-error stops are available only when the source semantics permits trap-like termination."
      },
      {
        id: "r0-t-alias",
        source: "machine",
        detail: "R0/T and *F alias behavior is available, including normal R0 transformation; aliases do not preserve R0."
      },
      {
        id: "r0-fractional-sentinel",
        source: "machine",
        detail: "Fractional positive R0 states can select R3 or jump to 99 while leaving the -99999999 sentinel."
      },
      {
        id: "raw-display-5f",
        source: "machine",
        detail: "Opcode 5F is a display/raw-state transform in this ROM, not a hang."
      }
    ],
    emulatorFacts: [
      {
        id: "return-empty-stack-jumps-to-01",
        status: "probed",
        detail: "\u0412/\u041E with an empty return stack behaves as one-cell \u0411\u041F 01 in continuous execution."
      },
      {
        id: "r0-star-f-aliases",
        status: "probed",
        detail: "*F aliases behave like the corresponding *0 commands, including R0 transformation; they do not preserve R0."
      },
      {
        id: "super-dark-fa-ff-indirect",
        status: "probed",
        detail: "Indirect \u041A \u0411\u041F R with R=FA..FF executes one command at 48..53, then continues at 01..06."
      },
      {
        id: "fa-direct-vs-indirect",
        status: "probed",
        detail: "Direct \u0411\u041F FA consumes/overwrites the following operand byte, while indirect \u041A \u0411\u041F R leaves 01..06 usable as tail code."
      },
      {
        id: "r0-fractional-jump-99",
        status: "probed",
        detail: "\u041A \u0411\u041F 0 with R0 in fractional positive/small states jumps to 99 and leaves R0=-99999999."
      },
      {
        id: "r0-fractional-selects-r3",
        status: "probed",
        detail: "\u041A \u041F->X 0 and \u041A X->\u041F 0 with 0<R0<1 access R3 and leave R0=-99999999."
      },
      {
        id: "x2-restore-boundaries",
        status: "probed",
        detail: "\u0412\u041F, '.', '/-/', and digit-entry X2 restoration boundaries are modeled as display-state boundaries."
      },
      {
        id: "negative-zero-degree-threshold",
        status: "probed",
        detail: "With 1|-00 in Y, multiplying by X and then normalizing through \u0412\u2191 yields a zero/nonzero threshold at |X|=1."
      },
      {
        id: "step-vs-run-delta",
        status: "probed",
        detail: "Continuous-run behavior is the default profile; step-only divergences are explicit exact-machine facts."
      },
      {
        id: "raw-display-5f",
        status: "probed",
        detail: "Opcode 5F leaves internal X intact but mutates display/raw state; it is usable only as an explicit display-state trick."
      }
    ]
  };
  function machineSupports(profile, featureId) {
    return profile.features.some((feature) => feature.id === featureId);
  }

  // src/core/compiler.ts
  var DEFAULT_OPTIONS = {
    delivery: "hex",
    budget: 105,
    analysis: false
  };
  var REGISTER_ORDER = [
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
    "e"
  ];
  var SWITCH_SCRATCH_PREFIX = "__switch_";
  var DISPATCH_SCRATCH_PREFIX = "__dispatch_";
  var TICTACTOE_MASK_SCRATCH_PREFIX = "__ttt_mask_";
  var BIT_MASK_SCRATCH_PREFIX = "__bit_mask_";
  var CELL_MAP_PREFIX = "__cell_map_";
  var SPATIAL_HIT_SCRATCH_PREFIX = "__spatial_hit_";
  var SPATIAL_COUNT_SCRATCH_PREFIX = "__spatial_count_";
  var NEGATIVE_ZERO_DEGREE_SELECTOR_GE = "__mkpro_negative_zero_ge";
  var NEGATIVE_ZERO_DEGREE_PRELOAD_VALUE = "1|-00";
  var INTERNAL_NAME_PREFIX = "__mkpro_";
  var DISPLAY_HELPER_MIN_SAVINGS = 4;
  var EXPRESSION_HELPER_MIN_COST = 8;
  var EXPRESSION_HELPER_MIN_SAVINGS = 4;
  var CompileError = class extends Error {
    diagnostics;
    constructor(diagnostics) {
      super(diagnostics.map((diagnostic) => diagnostic.message).join("\n"));
      this.diagnostics = diagnostics;
    }
  };
  function compileMKPro(source, options = {}) {
    const ast = parseProgram(source);
    const opts = { ...DEFAULT_OPTIONS, ...options };
    const machineProfile = MK61_PROFILE;
    const diagnostics = [];
    const optimizations = [];
    const warnings = [];
    const candidates = [];
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
        detail: `Lowered ${ast.v2.state.length} state fields and ${ast.v2.rules.length} rules through the generic intent pipeline.`
      });
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
      candidates
    );
    context.compileProgram();
    const optimizedResult = optimizeItems(context.items, opts, optimizations);
    const optimized = optimizedResult.items;
    const preloads = [
      ...buildPreloadReport(ast, allocation),
      ...buildNegativeZeroDegreePreloadReport(allocation, optimizations),
      ...optimizedResult.preloads
    ];
    appendOptimizationCandidateReports(optimizations, candidates);
    const { steps, labels, cellRoles } = layoutProgram(optimized, diagnostics, opts, ast, machineProfile);
    const largestBlocks = summarizeBlocks(optimized);
    const referenceResult = ast.reference === void 0 ? void 0 : buildReferenceReport(ast.reference, steps.length, opts.budget);
    if (referenceResult?.warning !== void 0) warnings.push(referenceResult.warning);
    if (steps.length > opts.budget) {
      diagnostics.push({
        level: opts.analysis ? "warning" : "error",
        code: "BUDGET_EXCEEDED",
        message: `Program uses ${steps.length} steps, budget is ${opts.budget}. Largest blocks: ${largestBlocks.join(", ")}`
      });
    }
    if (diagnostics.some((diagnostic) => diagnostic.level === "error")) {
      throw new CompileError(diagnostics);
    }
    const report = {
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
      ...referenceResult?.report === void 0 ? {} : { reference: referenceResult.report },
      ir: buildIrReport(ast, optimized, steps.length),
      cellRoles,
      candidates,
      budgetReport: buildBudgetReport(steps.length, opts.budget, largestBlocks, 0),
      machineFeaturesUsed: buildMachineFeaturesUsed(machineProfile, optimizations, cellRoles, candidates),
      proofs: buildProofReport(ast, optimized, cellRoles, opts, optimizations, preloads),
      emulatorFacts: machineProfile.emulatorFacts,
      rejectedCandidates: candidates.filter((candidate) => !candidate.selected).map((candidate) => ({
        site: candidate.site,
        variant: candidate.variant,
        reason: candidate.reason,
        steps: candidate.steps
      })),
      hotBlocks: largestBlocks.map(parseHotBlock)
    };
    return { ast, items: optimized, steps, report, diagnostics };
  }
  function visiblePublicRegisters(all) {
    const result = {};
    for (const [name, register] of Object.entries(all)) {
      if (!name.startsWith(SWITCH_SCRATCH_PREFIX) && !name.startsWith(DISPATCH_SCRATCH_PREFIX) && !name.startsWith(TICTACTOE_MASK_SCRATCH_PREFIX) && !name.startsWith(BIT_MASK_SCRATCH_PREFIX) && !name.startsWith(CELL_MAP_PREFIX) && !name.startsWith(SPATIAL_HIT_SCRATCH_PREFIX) && !name.startsWith(SPATIAL_COUNT_SCRATCH_PREFIX)) {
        result[name] = register;
      }
    }
    return result;
  }
  function eliminateUnobservedState(ast, optimizations) {
    const stateFields = new Set(ast.states.flatMap((state) => state.fields.map((field) => field.name)));
    if (stateFields.size === 0) return;
    const externallyRead = /* @__PURE__ */ new Set();
    const inputTargets = /* @__PURE__ */ new Set();
    const assigned = /* @__PURE__ */ new Map();
    const addRead = (name) => {
      if (stateFields.has(name)) externallyRead.add(name);
    };
    const visitExpr = (expr, ignored) => {
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
    const visitStatements = (statements) => {
      for (const statement of statements) {
        if (statement.kind === "pause" || statement.kind === "halt") visitExpr(statement.expr);
        if (statement.kind === "ask") {
          inputTargets.add(statement.target);
          if (statement.prompt) visitExpr(statement.prompt);
        }
        if (statement.kind === "input") inputTargets.add(statement.target);
        if (statement.kind === "assign") {
          if (!assigned.has(statement.target)) assigned.set(statement.target, []);
          assigned.get(statement.target).push(statement.expr);
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
    const removable = /* @__PURE__ */ new Set();
    for (const state of ast.states) {
      for (const field of state.fields) {
        if (externallyRead.has(field.name) || inputTargets.has(field.name)) continue;
        if (field.initial !== void 0 && !expressionPureForSubstitution(field.initial)) continue;
        const writes = assigned.get(field.name) ?? [];
        if (writes.every(expressionPureForSubstitution)) removable.add(field.name);
      }
    }
    if (removable.size === 0) return;
    for (const state of ast.states) {
      state.fields = state.fields.filter((field) => !removable.has(field.name));
    }
    const pruneStatements = (statements) => statements.flatMap((statement) => {
      if (statement.kind === "assign" && removable.has(statement.target)) return [];
      if (statement.kind === "loop") return [{ ...statement, body: pruneStatements(statement.body) }];
      if (statement.kind === "if") {
        const pruned = {
          ...statement,
          thenBody: pruneStatements(statement.thenBody)
        };
        if (statement.elseBody !== void 0) pruned.elseBody = pruneStatements(statement.elseBody);
        return [pruned];
      }
      if (statement.kind === "switch") {
        return [{
          ...statement,
          cases: statement.cases.map((switchCase) => ({ ...switchCase, body: pruneStatements(switchCase.body) })),
          ...statement.defaultBody === void 0 ? {} : { defaultBody: pruneStatements(statement.defaultBody) }
        }];
      }
      if (statement.kind === "dispatch") {
        return [{
          ...statement,
          cases: statement.cases.map((dispatchCase) => ({ ...dispatchCase, body: pruneStatements(dispatchCase.body) })),
          ...statement.defaultBody === void 0 ? {} : { defaultBody: pruneStatements(statement.defaultBody) }
        }];
      }
      return [statement];
    });
    for (const entry of ast.entries) entry.body = pruneStatements(entry.body);
    for (const proc of ast.procs) proc.body = pruneStatements(proc.body);
    for (const block of ast.blocks) block.body = pruneStatements(block.body);
    optimizations.push({
      name: "dead-state-elimination",
      detail: `Removed ${removable.size} unobserved state field${removable.size === 1 ? "" : "s"} before register allocation.`
    });
  }
  function hoistCommonBranchTails(ast, optimizations) {
    let hoisted = 0;
    let simplified = 0;
    const visitList = (statements) => {
      const result = [];
      for (const statement of statements) {
        result.push(...visitStatement(statement));
      }
      return result;
    };
    const visitStatement = (statement) => {
      if (statement.kind === "loop") {
        return [{ ...statement, body: visitList(statement.body) }];
      }
      if (statement.kind === "if") {
        const thenBody = visitList(statement.thenBody);
        const elseBody = statement.elseBody === void 0 ? void 0 : visitList(statement.elseBody);
        const tails = [];
        if (elseBody !== void 0) {
          while (thenBody.length > 0 && elseBody.length > 0 && statementEquals(thenBody[thenBody.length - 1], elseBody[elseBody.length - 1])) {
            tails.unshift(thenBody.pop());
            elseBody.pop();
          }
        }
        hoisted += tails.length;
        const simplifiedBranch = simplifyIfStatement({
          ...statement,
          thenBody,
          ...elseBody === void 0 ? {} : { elseBody }
        });
        if (simplifiedBranch.length !== 1 || !statementEquals(simplifiedBranch[0], {
          ...statement,
          thenBody,
          ...elseBody === void 0 ? {} : { elseBody }
        })) {
          simplified += 1;
        }
        return [...simplifiedBranch, ...tails];
      }
      if (statement.kind === "switch") {
        return [{
          ...statement,
          cases: statement.cases.map((switchCase) => ({ ...switchCase, body: visitList(switchCase.body) })),
          ...statement.defaultBody === void 0 ? {} : { defaultBody: visitList(statement.defaultBody) }
        }];
      }
      if (statement.kind === "dispatch") {
        return [{
          ...statement,
          cases: statement.cases.map((dispatchCase) => ({ ...dispatchCase, body: visitList(dispatchCase.body) })),
          ...statement.defaultBody === void 0 ? {} : { defaultBody: visitList(statement.defaultBody) }
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
        detail: `Hoisted ${hoisted} shared branch tail${hoisted === 1 ? "" : "s"} and simplified ${simplified} conditional shape${simplified === 1 ? "" : "s"}.`
      });
    }
  }
  function simplifyIfStatement(statement) {
    if (statement.elseBody !== void 0 && statement.thenBody.length === 0 && statement.elseBody.length === 0) {
      return [];
    }
    if (statement.elseBody !== void 0 && statement.thenBody.length === 0) {
      const { elseBody, ...rest } = statement;
      return [{
        ...rest,
        condition: invertCondition(statement.condition),
        thenBody: elseBody
      }];
    }
    if (statement.elseBody !== void 0 && statement.elseBody.length === 0) {
      const { elseBody: _elseBody, ...rest } = statement;
      return [{
        ...rest
      }];
    }
    return [statement];
  }
  function invertCondition(condition) {
    return {
      ...condition,
      op: invertComparisonOp(condition.op)
    };
  }
  function invertComparisonOp(op) {
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
  function appendOptimizationCandidateReports(optimizations, candidates) {
    const selectedPassCandidates = [
      ["stable-indirect-flow", "stable-register indirect branch/call selected by IR data-flow proof", 1],
      ["preloaded-indirect-flow", "compiler-owned address preload selected for one-cell indirect branch/call", 1],
      ["preloaded-super-dark-flow", "compiler-owned FA..FF preloaded one-command dispatch selected after layout proof", 1],
      ["indirect-memory-table", "stable selector reused for indirect memory access", 0],
      ["r0-fractional-sentinel", "fractional R0 selector side effect reused after liveness proof", 0]
    ];
    for (const [name, reason, steps] of selectedPassCandidates) {
      if (!optimizations.some((optimization) => optimization.name === name)) continue;
      if (candidates.some((candidate) => candidate.variant === name && candidate.selected)) continue;
      candidates.push({
        site: "ir-pass",
        variant: name,
        steps,
        selected: true,
        reason
      });
    }
  }
  var customReferenceMetricsResolver;
  function buildReferenceReport(referenceName, compiledSteps, fallbackBudget) {
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
      parity: compiledSteps < referenceSteps ? "smaller" : compiledSteps === referenceSteps ? "equal" : "larger"
    };
    const warning = metrics === void 0 ? `Reference '${referenceName}' was not found under games/*; using budget ${fallbackBudget} as reference size.` : void 0;
    return warning === void 0 ? { report } : { report, warning };
  }
  function resolveReferenceMetrics(referenceName) {
    const customMetrics = customReferenceMetricsResolver?.(referenceName);
    if (customMetrics !== void 0) return customMetrics;
    const fs = nodeBuiltin("node:fs");
    const path = nodeBuiltin("node:path");
    const repoRoot = findRepoRoot(fs, path);
    if (fs === void 0 || path === void 0 || repoRoot === void 0) return void 0;
    const reference = /^([A-Za-z0-9]+)_(.+)$/u.exec(referenceName);
    if (!reference) return void 0;
    const collection = reference[1];
    const slug = reference[2].replace(/_/gu, "-");
    const directory = path.resolve(repoRoot, "games", collection);
    const manifestPath = path.resolve(directory, "manifest.tsv");
    let programFile = `${slug}.txt`;
    if (fs.existsSync(manifestPath)) {
      const rows = fs.readFileSync(manifestPath, "utf8").split(/\r?\n/u).slice(1);
      const manifestProgram = rows.map((row) => row.split("	")[0]?.trim()).find((program) => program === programFile);
      if (manifestProgram !== void 0) programFile = manifestProgram;
    }
    const programPath = path.resolve(directory, programFile);
    if (!fs.existsSync(programPath)) return void 0;
    return readReferenceListingMetrics(programPath, fs);
  }
  function nodeBuiltin(specifier) {
    const processLike = globalThis.process;
    const value = processLike?.getBuiltinModule?.(specifier);
    if (value !== void 0) return value;
    if (specifier.startsWith("node:")) {
      return processLike?.getBuiltinModule?.(specifier.slice("node:".length));
    }
    return void 0;
  }
  function findRepoRoot(fs, path) {
    const cwd = globalThis.process?.cwd?.();
    if (fs === void 0 || path === void 0 || cwd === void 0) return void 0;
    let current = path.resolve(cwd);
    for (let depth = 0; depth < 8; depth += 1) {
      if (fs.existsSync(path.resolve(current, "package.json")) && fs.existsSync(path.resolve(current, "games"))) {
        return current;
      }
      const parent = path.dirname(current);
      if (parent === current) break;
      current = parent;
    }
    return void 0;
  }
  function readReferenceListingMetrics(path, fs) {
    const addresses = fs.readFileSync(path, "utf8").split(/\r?\n/u).map((line) => line.trim()).filter(Boolean).map((line) => line.split(/\s+/u)[0] ?? "").filter((address) => /^[0-9]{2}$|^A[0-4]$/u.test(address)).map(parseReferenceAddress);
    if (addresses.length === 0) return void 0;
    const maxAddress = Math.max(...addresses);
    const occupied = new Set(addresses);
    const gaps = [];
    for (let address = 0; address <= maxAddress; address += 1) {
      if (!occupied.has(address)) gaps.push(formatAddress(address));
    }
    return {
      span: maxAddress + 1,
      entries: addresses.length,
      gaps
    };
  }
  function parseReferenceAddress(text) {
    if (/^[0-9]{2}$/u.test(text)) return Number(text);
    const extended = /^A([0-4])$/u.exec(text);
    if (extended) return 100 + Number(extended[1]);
    throw new Error(`Invalid MK-61 reference address '${text}'.`);
  }
  function validateSemanticDomains(ast, diagnostics) {
    const unresolved = ast.domains.filter(
      (domain) => ["cache_search", "fight", "table"].includes(domain.domainKind)
    );
    if (unresolved.length === 0) return;
    diagnostics.push({
      level: "error",
      code: "SEMANTIC_DOMAIN_LOWERER_MISSING",
      message: `High-level semantic domains need real rule lowerers before code generation: ${unresolved.map(formatDomainName).join(", ")}. The compiler refuses to treat game rules as comments.`
    });
  }
  function validateV2Intent(ast, diagnostics) {
    const v2 = ast.v2;
    if (!v2) return;
    const unsupported = collectUnsupportedV2Statements(v2);
    if (unsupported.length === 0) return;
    diagnostics.push({
      level: "error",
      code: "V2_SEMANTIC_LOWERER_MISSING",
      message: `MK-Pro source contains effects that need real rule lowerers before code generation: ${unsupported.slice(0, 8).map((item) => `${item.text} (line ${item.line})`).join(", ")}. The compiler refuses to treat human-level semantics as comments.`
    });
  }
  function validateReservedInternalNames(ast, diagnostics) {
    const seen = /* @__PURE__ */ new Set();
    const report = (name, line) => {
      if (!name.toLowerCase().startsWith(INTERNAL_NAME_PREFIX)) return;
      const key = `${line ?? 0}:${name}`;
      if (seen.has(key)) return;
      seen.add(key);
      diagnostics.push(buildDiagnostic(
        "error",
        `Name '${name}' uses reserved compiler-internal prefix '${INTERNAL_NAME_PREFIX}'.`,
        line
      ));
    };
    const visitExpr = (expr) => {
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
    const visitCondition = (condition) => {
      visitExpr(condition.left);
      visitExpr(condition.right);
    };
    const visitStatements = (statements) => {
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
  function collectUnsupportedV2Statements(ast) {
    const unsupported = [];
    const visit = (statements) => {
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
  function isLowerableV2Predicate(predicate) {
    if (predicate.kind === "v2_contains") {
      return isSimpleCompilerExpression(predicate.collection) && isSimpleCompilerExpression(predicate.item);
    }
    if (predicate.kind === "v2_compare") {
      return isSimpleCompilerExpression(predicate.left) && isSimpleCompilerExpression(predicate.right);
    }
    return false;
  }
  function formatV2Predicate(predicate) {
    if (predicate.kind === "v2_contains") return `${predicate.item} in ${predicate.collection}`;
    return `${predicate.left} ${predicate.op} ${predicate.right}`;
  }
  function isSimpleCompilerExpression(text) {
    let normalized = normalizeV2ExpressionText(text);
    const direction = /^direction\((.+)\)$/u.exec(normalized.trim());
    if (direction) normalized = direction[1];
    try {
      parseExpression(normalized);
      return true;
    } catch {
      return false;
    }
  }
  function formatDomainName(domain) {
    return domain.name ? `${domain.domainKind} ${domain.name}` : domain.domainKind;
  }
  var EmitContext = class {
    items = [];
    labelCounter = 0;
    constants = /* @__PURE__ */ new Map();
    constantStack = /* @__PURE__ */ new Set();
    ast;
    allocation;
    options;
    machineProfile;
    diagnostics;
    optimizations;
    warnings;
    candidates;
    inlineProcNames;
    readCounts;
    displayUseCounts;
    showSequenceUseCounts;
    expressionUseCounts;
    lineCountCallCount;
    lineCountGroupCounts;
    spatialHitHelpers = /* @__PURE__ */ new Map();
    displayHelpers = /* @__PURE__ */ new Map();
    showSequenceHelpers = /* @__PURE__ */ new Map();
    expressionHelpers = /* @__PURE__ */ new Map();
    lineCountHelpers = /* @__PURE__ */ new Map();
    currentXVariable;
    emittingExpressionHelper = false;
    constructor(ast, allocation, options, machineProfile, diagnostics, optimizations, warnings, candidates) {
      this.ast = ast;
      this.allocation = allocation;
      this.options = options;
      this.machineProfile = machineProfile;
      this.diagnostics = diagnostics;
      this.optimizations = optimizations;
      this.warnings = warnings;
      this.candidates = candidates;
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
    compileProgram() {
      const main = this.ast.entries[0];
      this.emitLabel(main.name);
      this.compileInitialState();
      this.compileInitialStores();
      this.compileStatements(main.body);
      if (!(this.ast.v2 && this.statementsTerminate(main.body))) {
        this.emitOp(80, "\u0421/\u041F", "implicit final stop");
      }
      for (const proc of this.ast.procs) {
        if (this.inlineProcNames.has(proc.name)) continue;
        this.emitLabel(proc.name);
        this.compileStatements(proc.body);
        if (!this.statementsTerminate(proc.body)) {
          this.emitOp(82, "\u0412/\u041E", "implicit return from proc");
        }
      }
      for (const block of this.ast.blocks) {
        if (block.mode === "inline") continue;
        this.emitLabel(block.name);
        this.compileStatements(block.body);
        if (!this.statementsTerminate(block.body)) {
          this.emitOp(80, "\u0421/\u041F", `implicit stop for ${block.mode} block ${block.name}`, block.line);
        }
      }
      this.compileRuntimeHelpers();
    }
    compileRuntimeHelpers() {
      for (const helper of this.displayHelpers.values()) {
        this.emitLabel(helper.label);
        this.compilePackedDisplayBody(helper.display, helper.line, false);
        this.emitOp(82, "\u0412/\u041E", `display ${helper.display.name} return`, helper.line);
        this.optimizations.push({
          name: "packed-display-helper",
          detail: `Emitted shared packed display helper for screen ${helper.display.name}.`
        });
      }
      for (const helper of this.showSequenceHelpers.values()) {
        this.emitLabel(helper.label);
        this.compilePackedDisplayBody(helper.first, helper.line, false);
        this.compilePackedDisplayBody(helper.second, helper.line, false);
        this.emitOp(82, "\u0412/\u041E", `show sequence ${helper.first.name}/${helper.second.name} return`, helper.line);
        this.optimizations.push({
          name: "show-sequence-helper",
          detail: `Emitted shared helper for show ${helper.first.name}; show ${helper.second.name}; read.`
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
        this.emitOp(82, "\u0412/\u041E", "expression helper return", helper.line);
        this.optimizations.push({
          name: "expression-helper",
          detail: `Emitted shared helper for ${expressionToIntentText(helper.expr)}.`
        });
      }
      for (const helper of this.lineCountHelpers.values()) {
        this.emitLabel(helper.label);
        this.emitStore(spatialCountMaskScratchName(), "line count mask", helper.line);
        this.emitSpatialLineCountLoopBody(spatialCountMaskScratchName(), helper.cell, helper.board, helper.line);
        this.emitOp(82, "\u0412/\u041E", "line_count return", helper.line);
        this.optimizations.push({
          name: "spatial-line-count-helper",
          detail: `Emitted shared line_count helper for ${helper.board.name}/${expressionToIntentText(helper.cell)}.`
        });
      }
      for (const helper of this.spatialHitHelpers.values()) {
        this.emitLabel(helper.label);
        this.emitStore(helper.scratch, "spatial hit index", helper.line);
        this.emitRecall(helper.mask, "spatial hit mask", helper.line);
        this.compileExpression({
          kind: "call",
          callee: "bit_mask",
          args: [{ kind: "identifier", name: helper.scratch }]
        });
        this.emitOp(55, "\u041A \u2227", "spatial hit test", helper.line);
        this.emitOp(50, "\u041A \u0417\u041D", "spatial hit to count", helper.line);
        this.emitOp(82, "\u0412/\u041E", "spatial hit return", helper.line);
      }
    }
    compileInitialState() {
      if (this.ast.v2) {
        const fields = this.ast.states.flatMap((state) => state.fields);
        if (fields.some((field) => field.initial !== void 0 || field.initialStack !== void 0)) {
          this.optimizations.push({
            name: "auto-preload-initial-state",
            detail: "Moved initial state into setup/preload values so official program cells stay focused on turn logic."
          });
        }
        return;
      }
      for (const state of this.ast.states) {
        for (const field of state.fields.filter((candidate) => candidate.initialStack === "Y")) {
          this.emitOp(20, "X\u2194Y", `init ${state.name}.${field.name} from stack.Y`, field.line);
          this.emitStore(field.name, `init ${state.name}.${field.name}`, field.line);
          this.emitOp(20, "X\u2194Y", `restore stack.X after ${field.name}`, field.line);
        }
        for (const field of state.fields.filter((candidate) => candidate.initialStack === "X")) {
          this.emitStore(field.name, `init ${state.name}.${field.name}`, field.line);
        }
        for (const field of state.fields) {
          if (field.initialStack !== void 0) continue;
          if (field.initial === void 0) continue;
          this.compileExpression(field.initial);
          this.emitStore(field.name, `init ${state.name}.${field.name}`, field.line);
        }
        if (state.fields.length > 0) {
          this.optimizations.push({
            name: "intent-state-lowering",
            detail: `Lowered state ${state.name} with ${state.fields.length} fields to register-backed values.`
          });
        }
      }
    }
    compileInitialStores() {
      for (const declaration of this.ast.declarations) {
        if (declaration.kind !== "store" || !declaration.value) continue;
        this.compileExpression(declaration.value);
        this.emitStore(declaration.name, `init ${declaration.name}`, declaration.line);
      }
    }
    compileStatements(statements) {
      for (let index = 0; index < statements.length; index += 1) {
        const statement = statements[index];
        const next = statements[index + 1];
        if (statement.kind === "assign") {
          const reused = this.compileRepeatedAssignmentValue(statements, index);
          if (reused > 1) {
            index += reused - 1;
            continue;
          }
        }
        if (statement.kind === "assign" && next?.kind === "assign" && this.compileTicTacToeCellMaskReuse(statement, next)) {
          index += 1;
          continue;
        }
        if (statement.kind === "assign" && next?.kind === "assign" && this.compileBitSetMaskReuse(statement, next)) {
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
            detail: `Dropped duplicate terminal display before stop at line ${next.line}.`
          });
          index += 1;
          continue;
        }
        if (statement.kind === "show" && next?.kind === "halt" && this.haltDisplaysSameValue(statement, next)) {
          this.compileStatement(next);
          this.optimizations.push({
            name: "terminal-display-fusion",
            detail: `Dropped duplicate screen ${statement.display} before stop at line ${next.line}.`
          });
          index += 1;
          continue;
        }
        if (statement.kind === "show" && next?.kind === "show" && statements[index + 2]?.kind === "input") {
          const input = statements[index + 2];
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
            detail: `Fused show ${statement.display} and read ${next.target} into one calculator stop.`
          });
          index += 1;
          continue;
        }
        this.compileStatement(statement);
      }
    }
    compileRepeatedAssignmentValue(statements, start) {
      const first = statements[start];
      if (first?.kind !== "assign" || !expressionPureForSubstitution(first.expr)) return 0;
      let end = start + 1;
      while (end < statements.length) {
        const candidate = statements[end];
        if (candidate.kind !== "assign" || !expressionEquals(candidate.expr, first.expr)) break;
        end += 1;
      }
      const count = end - start;
      if (count <= 1) return 0;
      this.compileExpression(first.expr);
      for (let index = start; index < end; index += 1) {
        const assignment = statements[index];
        this.emitStore(assignment.target, `set ${assignment.target}`, assignment.line);
      }
      this.optimizations.push({
        name: "repeated-assignment-value-reuse",
        detail: `Stored one computed value into ${count} consecutive assignment targets at line ${first.line}.`
      });
      return count;
    }
    haltDisplaysSameValue(show, halt) {
      const display = this.ast.displays.find((candidate) => candidate.name === show.display);
      if (display === void 0 || display.items.length !== 1) return false;
      const [item] = display.items;
      if (item?.kind !== "source") return false;
      const field = this.findStateField(item.name);
      return field?.initial !== void 0 && expressionEquals(field.initial, halt.expr);
    }
    compileShowSequenceRead(firstShow, secondShow, input) {
      const helper = this.sharedShowSequenceHelper(firstShow.display, secondShow.display, firstShow.line);
      if (helper === void 0) return false;
      this.emitJump(83, "\u041F\u041F", helper.label, `show ${firstShow.display}; show ${secondShow.display}`, firstShow.line);
      this.emitStore(input.target, `read ${input.target}`, input.line);
      this.optimizations.push({
        name: "show-sequence-helper-call",
        detail: `Reused shared helper for show ${firstShow.display}; show ${secondShow.display}; read ${input.target}.`
      });
      return true;
    }
    compileGuardAssignmentSubstitution(assign, guarded) {
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
        condition: substitutedCondition
      }, guarded.line);
      this.optimizations.push({
        name: "single-use-guard-substitution",
        detail: `Substituted ${assign.target} directly into the following condition at lines ${assign.line}/${guarded.line}.`
      });
      return true;
    }
    compileStatement(statement) {
      switch (statement.kind) {
        case "pause":
          this.compileExpression(statement.expr);
          this.emitOp(80, "\u0421/\u041F", "pause", statement.line);
          return;
        case "ask":
          if (statement.prompt) this.compileExpression(statement.prompt);
          this.emitOp(80, "\u0421/\u041F", `ask ${statement.target}`, statement.line);
          this.emitStore(statement.target, `input ${statement.target}`, statement.line);
          return;
        case "input":
          this.emitOp(80, "\u0421/\u041F", `read ${statement.target}`, statement.line);
          this.emitStore(statement.target, `read ${statement.target}`, statement.line);
          this.optimizations.push({
            name: "intent-read-lowering",
            detail: `Lowered read at line ${statement.line} to calculator stop plus register store.`
          });
          return;
        case "halt":
          this.compileExpression(statement.expr);
          this.emitOp(80, "\u0421/\u041F", "halt", statement.line);
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
          this.emitJump(81, "\u0411\u041F", start, "loop back", statement.line);
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
    compileTicTacToeCellMaskReuse(first, second) {
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
      this.emitOp(55, "\u041A \u2227", "cell_has with reused mask", first.line);
      this.emitStore(first.target, `set ${first.target}`, first.line);
      this.compileExpression(mark.mask);
      this.emitRecall(scratch, "reuse 4x4 cell mask", second.line);
      this.emitOp(56, "\u041A \u2228", "cell_set with reused mask", second.line);
      this.emitStore(second.target, `set ${second.target}`, second.line);
      this.optimizations.push({
        name: "tic-tac-toe-cell-mask-cse",
        detail: `Computed cell_mask once for adjacent cell_has/cell_set at lines ${first.line}/${second.line}.`
      });
      return true;
    }
    compileBitSetMaskReuse(first, second) {
      const firstSet = matchBitSetAssignment(first);
      const secondSet = matchBitSetAssignment(second);
      if (firstSet === void 0 || secondSet === void 0) return false;
      if (!expressionEquals(firstSet.item, secondSet.item)) return false;
      const scratch = bitMaskScratchName(first);
      if (!this.allocation.registers[scratch]) return false;
      this.compileExpression(bitMaskExpression(firstSet.item));
      this.emitStore(scratch, "cell bit mask scratch", first.line);
      this.compileExpression(firstSet.collection);
      this.emitRecall(scratch, "reuse cell bit mask", first.line);
      this.emitOp(56, "\u041A \u2228", "bit_set with reused mask", first.line);
      this.emitStore(first.target, `set ${first.target}`, first.line);
      this.compileExpression(secondSet.collection);
      this.emitRecall(scratch, "reuse cell bit mask", second.line);
      this.emitOp(56, "\u041A \u2228", "bit_set with reused mask", second.line);
      this.emitStore(second.target, `set ${second.target}`, second.line);
      this.optimizations.push({
        name: "bit-set-mask-cse",
        detail: `Computed bit_mask() once for adjacent set updates at lines ${first.line}/${second.line}.`
      });
      return true;
    }
    compileIf(statement, line) {
      if (this.compileArithmeticIfSelect(statement)) return;
      if (this.compileMembershipClearReuse(statement, line)) return;
      const falseLabel = this.freshLabel("if_false");
      const endLabel = this.freshLabel("if_end");
      this.compileCondition(statement.condition, falseLabel, line);
      this.compileStatements(statement.thenBody);
      if (statement.elseBody) {
        this.emitJump(81, "\u0411\u041F", endLabel, "if end", line);
        this.emitLabel(falseLabel);
        this.compileStatements(statement.elseBody);
        this.emitLabel(endLabel);
      } else {
        this.emitLabel(falseLabel);
      }
    }
    compileMembershipClearReuse(statement, line) {
      const clearPrefix = this.membershipClearPrefix(statement.thenBody);
      if (clearPrefix === void 0) return false;
      const { clear, tail } = clearPrefix;
      const membership = matchBitMembershipCondition(statement.condition);
      if (membership === void 0) return false;
      if (!isBitClearAssignment(clear, membership)) return false;
      const falseLabel = this.freshLabel("if_false");
      const endLabel = this.freshLabel("if_end");
      this.compileExpression(membership.test);
      this.emitJump(87, "F x!=0", falseLabel, "false branch for !=", line);
      this.emitOp(58, "\u041A \u0418\u041D\u0412", "reuse membership mask for clear", clear.line);
      this.compileExpression(membership.collection);
      this.emitOp(55, "\u041A \u2227", "clear matched cell with reused mask", clear.line);
      this.emitStore(clear.target, `set ${clear.target}`, clear.line);
      this.compileStatements(tail);
      if (statement.elseBody) {
        this.emitJump(81, "\u0411\u041F", endLabel, "if end", line);
        this.emitLabel(falseLabel);
        this.compileStatements(statement.elseBody);
        this.emitLabel(endLabel);
      } else {
        this.emitLabel(falseLabel);
      }
      this.optimizations.push({
        name: "cell-membership-clear-reuse",
        detail: `Reused the successful membership mask when clearing ${clear.target} at line ${clear.line}.`
      });
      return true;
    }
    membershipClearPrefix(statements) {
      const first = statements[0];
      if (first?.kind === "assign") {
        return { clear: first, tail: statements.slice(1) };
      }
      if (first?.kind !== "call" || !this.inlineProcNames.has(first.block)) return void 0;
      const proc = this.ast.procs.find((candidate) => candidate.name === first.block);
      if (proc === void 0) return void 0;
      const clear = proc.body[0];
      if (clear?.kind !== "assign") return void 0;
      return {
        clear,
        tail: [...proc.body.slice(1), ...statements.slice(1)]
      };
    }
    compileArithmeticIfSelect(statement) {
      const canUseNegativeZero = this.allocation.negativeZeroDegree !== void 0;
      const selected = buildBranchRemovalCandidate(
        statement,
        this.ast,
        { negativeZeroDegree: canUseNegativeZero }
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
          reason: `Branchless ${selected.name} estimated at ${selectedCost} cells; ordinary branched form was shorter (${ordinaryCost}).`
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
        this.emitOp(80, "\u0421/\u041F", `${selected.kind} ${selected.name}`, statement.line);
      }
      this.optimizations.push({
        name: "branch-removal",
        detail: `${selected.detail} at line ${statement.line}; emitted branchless ${selected.name}.`
      });
      this.optimizations.push({
        name: selected.name,
        detail: `${selected.detail} at line ${statement.line} (${selectedCost} vs ${ordinaryCost} estimated steps).`
      });
      return true;
    }
    recordRejectedNegativeZeroBranchCandidate(statement) {
      const selected = buildBranchRemovalCandidate(statement, this.ast, { negativeZeroDegree: true });
      if (selected === void 0 || !selected.name.startsWith("negative-zero-threshold-")) return;
      const ordinaryCost = estimateOrdinaryIfCost(statement, this.ast);
      const selectedCost = estimateExpressionCost(selected.expr) + 1;
      this.candidates.push({
        site: `if@${statement.line}`,
        variant: selected.name,
        steps: selectedCost,
        selected: false,
        reason: selectedCost >= ordinaryCost ? `Branchless ${selected.name} estimated at ${selectedCost} cells; ordinary branched form was shorter (${ordinaryCost}).` : `Branchless ${selected.name} estimated at ${selectedCost} cells, but no compiler-owned negative-zero register was available.`
      });
    }
    compileDoubleBranchRemoval(first, second) {
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
          reason: `Branchless ${selected.name} estimated at ${selectedCost} cells; paired branched form was shorter (${ordinaryCost}).`
        });
        return false;
      }
      this.compileExpression(selected.expr);
      this.emitStore(selected.target, `${selected.name} ${selected.target}`, first.line);
      this.optimizations.push({
        name: "branch-removal",
        detail: `${selected.detail} at lines ${first.line}/${second.line}; emitted branchless ${selected.name}.`
      });
      this.optimizations.push({
        name: selected.name,
        detail: `${selected.detail} at lines ${first.line}/${second.line} (${selectedCost} vs ${ordinaryCost} estimated steps).`
      });
      return true;
    }
    compileSwitch(statement) {
      const scratch = `${SWITCH_SCRATCH_PREFIX}${statement.scratchId}`;
      const register = this.allocation.registers[scratch];
      if (!register) {
        this.diagnostics.push({
          level: "error",
          message: `Internal: no scratch register reserved for switch at line ${statement.line}.`,
          line: statement.line
        });
        return;
      }
      this.compileExpression(statement.expr);
      this.emitOp(
        64 + registerIndex(register),
        `X->\u041F ${register}`,
        `switch scratch`,
        statement.line
      );
      const endLabel = this.freshLabel("switch_end");
      for (const switchCase of statement.cases) {
        const nextLabel = this.freshLabel("switch_next");
        this.emitOp(
          96 + registerIndex(register),
          `\u041F->X ${register}`,
          "switch scratch recall",
          switchCase.line
        );
        this.compileExpression(switchCase.value);
        this.emitOp(17, "-", "switch compare", switchCase.line);
        this.emitJump(94, "F x=0", nextLabel, "case mismatch", switchCase.line);
        this.compileStatements(switchCase.body);
        this.emitJump(81, "\u0411\u041F", endLabel, "switch end", switchCase.line);
        this.emitLabel(nextLabel);
      }
      if (statement.defaultBody) this.compileStatements(statement.defaultBody);
      this.emitLabel(endLabel);
      this.optimizations.push({
        name: "switch-lowering",
        detail: `Lowered switch at line ${statement.line} via scratch R${register}; expression evaluated once.`
      });
    }
    compileDispatch(statement) {
      const optimized = optimizeDispatchDefaultCases(statement);
      if (optimized.removed > 0) {
        this.optimizations.push({
          name: "dispatch-default-merge",
          detail: `Removed ${optimized.removed} dispatch case${optimized.removed === 1 ? "" : "s"} whose body matched the default branch.`
        });
      }
      const site = statement.name ?? `dispatch@${statement.line}`;
      const selected = selectDispatchCandidate(optimized.statement, this.machineProfile);
      for (const candidate of selected.candidates) this.candidates.push(candidate);
      this.optimizations.push({
        name: "dispatch-lowering",
        detail: `Selected ${selected.selected.variant} for ${site}.`
      });
      this.compileDispatchCompareChain(optimized.statement, selected.selected.variant === "fallthrough-compare-chain");
    }
    compileDispatchCompareChain(statement, useFallthrough) {
      const scratch = `${DISPATCH_SCRATCH_PREFIX}${statement.scratchId}`;
      const sourceRegister = dispatchExpressionRegister(statement, this.allocation);
      const register = sourceRegister ?? this.allocation.registers[scratch];
      if (!register) {
        this.diagnostics.push({
          level: "error",
          message: `Internal: no scratch register reserved for dispatch at line ${statement.line}.`,
          line: statement.line
        });
        return;
      }
      this.compileExpression(statement.expr);
      if (sourceRegister === void 0) {
        this.emitOp(
          64 + registerIndex(register),
          `X->\u041F ${register}`,
          `dispatch scratch`,
          statement.line
        );
      } else {
        this.optimizations.push({
          name: "dispatch-source-register",
          detail: `Reused R${register} as dispatch scratch for identifier expression.`
        });
      }
      const endLabel = this.freshLabel("dispatch_end");
      let xContainsDispatchExpr = sourceRegister !== void 0;
      for (let index = 0; index < statement.cases.length; index += 1) {
        const dispatchCase = statement.cases[index];
        const nextLabel = this.freshLabel("dispatch_next");
        const lastCase = index === statement.cases.length - 1;
        if (index > 0 && !xContainsDispatchExpr) {
          this.emitOp(
            96 + registerIndex(register),
            `\u041F->X ${register}`,
            "dispatch scratch recall",
            dispatchCase.line
          );
          xContainsDispatchExpr = true;
        }
        if (xContainsDispatchExpr && isZeroExpression(dispatchCase.value)) {
          this.emitJump(94, "F x=0", nextLabel, "zero-case mismatch", dispatchCase.line);
        } else {
          this.compileExpression(dispatchCase.value);
          this.emitOp(17, "-", "dispatch compare", dispatchCase.line);
          this.emitJump(94, "F x=0", nextLabel, "case mismatch", dispatchCase.line);
          xContainsDispatchExpr = false;
        }
        this.compileStatements(dispatchCase.body);
        if (!this.statementsTerminate(dispatchCase.body) && (!useFallthrough || !lastCase || statement.defaultBody !== void 0)) {
          this.emitJump(81, "\u0411\u041F", endLabel, "dispatch end", dispatchCase.line);
        }
        this.emitLabel(nextLabel);
        xContainsDispatchExpr = xContainsDispatchExpr && isZeroExpression(dispatchCase.value);
      }
      if (statement.defaultBody) this.compileStatements(statement.defaultBody);
      this.emitLabel(endLabel);
    }
    statementsTerminate(statements) {
      return this.statementListTerminates(statements, /* @__PURE__ */ new Set());
    }
    statementListTerminates(statements, seenProcs) {
      const last = statements.at(-1);
      if (!last) return false;
      return this.statementTerminates(last, seenProcs);
    }
    statementTerminates(statement, seenProcs) {
      if (statement.kind === "halt" || statement.kind === "loop" || statement.kind === "trap") return true;
      if (statement.kind === "if") {
        return statement.elseBody !== void 0 && this.statementListTerminates(statement.thenBody, new Set(seenProcs)) && this.statementListTerminates(statement.elseBody, new Set(seenProcs));
      }
      if (statement.kind === "dispatch") {
        return statement.defaultBody !== void 0 && this.statementListTerminates(statement.defaultBody, new Set(seenProcs)) && statement.cases.every((dispatchCase) => this.statementListTerminates(dispatchCase.body, new Set(seenProcs)));
      }
      if (statement.kind !== "call") return false;
      const block = this.ast.blocks.find((candidate) => candidate.name === statement.block);
      if (block !== void 0) return block.mode !== "inline";
      const proc = this.ast.procs.find((candidate) => candidate.name === statement.block);
      if (proc === void 0 || seenProcs.has(proc.name)) return false;
      seenProcs.add(proc.name);
      return this.statementListTerminates(proc.body, seenProcs);
    }
    compileShow(displayName, line) {
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
          line
        ));
        return;
      }
      const helper = this.sharedDisplayHelper(display, line);
      if (helper !== void 0) {
        this.emitJump(83, "\u041F\u041F", helper.label, `show ${display.name}`, line);
        this.optimizations.push({
          name: "packed-display-helper-call",
          detail: `Reused shared packed display helper for screen ${display.name}.`
        });
        return;
      }
      this.compilePackedDisplayBody(display, line, true);
      this.reportPackedDisplayLowering(display);
    }
    compilePackedDisplayBody(display, line, reuseCurrentX) {
      const sources = reuseCurrentX ? this.orderDisplaySources(display.sources) : display.sources;
      if (sources.length === 0) {
        this.emitNumber("0");
      } else {
        for (let index = 0; index < sources.length; index += 1) {
          const source = sources[index];
          if (!(index === 0 && source === this.currentXVariable)) {
            this.emitRecall(source, `display ${display.name} source`, line);
          }
          if (index > 0) this.emitOp(16, "+", "packed display combine", line);
        }
      }
      this.emitOp(80, "\u0421/\u041F", `show ${display.name}`, line);
    }
    reportPackedDisplayLowering(display) {
      const canUseDisplayBytes = machineSupports(this.machineProfile, "display-bytes");
      this.optimizations.push({
        name: "packed-display-lowering",
        detail: canUseDisplayBytes ? `Display ${display.name} may use display-byte encodings in later layout passes.` : `Display ${display.name} lowered as ordinary packed numeric output.`
      });
    }
    sharedDisplayHelper(display, line) {
      if (!this.shouldShareDisplay(display)) return void 0;
      const existing = this.displayHelpers.get(display.name);
      if (existing !== void 0) return existing;
      const helper = {
        display,
        label: `__display_${display.name}`,
        line
      };
      this.displayHelpers.set(display.name, helper);
      return helper;
    }
    shouldShareDisplay(display) {
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
    sharedShowSequenceHelper(firstName, secondName, line) {
      const first = this.ast.displays.find((candidate) => candidate.name === firstName);
      const second = this.ast.displays.find((candidate) => candidate.name === secondName);
      if (first === void 0 || second === void 0) return void 0;
      if (!this.shouldShareShowSequence(first, second)) return void 0;
      const key = showSequenceKey(first.name, second.name);
      const existing = this.showSequenceHelpers.get(key);
      if (existing !== void 0) return existing;
      const helper = {
        first,
        second,
        label: `__showseq_${this.showSequenceHelpers.size}`,
        line
      };
      this.showSequenceHelpers.set(key, helper);
      return helper;
    }
    shouldShareShowSequence(first, second) {
      if (first.items.some((item) => item.kind === "literal")) return false;
      if (second.items.some((item) => item.kind === "literal")) return false;
      const uses = this.showSequenceUseCounts.get(showSequenceKey(first.name, second.name)) ?? 0;
      if (uses < 2) return false;
      const bodyCost = estimatePackedDisplayBodyCost(first.sources.length) + estimatePackedDisplayBodyCost(second.sources.length);
      const inlineTotal = uses * (bodyCost + 1);
      const helperTotal = uses * 3 + bodyCost + 1;
      return inlineTotal - helperTotal >= DISPLAY_HELPER_MIN_SAVINGS;
    }
    compileTextDisplay(display, line) {
      const [literal, source, ...rest] = display.items;
      if (literal?.kind !== "literal" || literal.text !== "BEEr " || source?.kind !== "source" || rest.length !== 0) {
        return false;
      }
      const field = this.findStateField(source.name);
      if (field === void 0 || (field.min ?? 0) < 0 || (field.max ?? 0) > 99) return false;
      if (this.allocation.registers[source.name] !== "0") return false;
      if (this.currentAddress() !== 0) return false;
      const scratchRegisters = /* @__PURE__ */ new Set(["1", "2", "7", "8", "a"]);
      const conflicting = Object.entries(this.allocation.registers).filter(([name, register]) => name !== source.name && scratchRegisters.has(register));
      if (conflicting.length > 0) return false;
      this.emitTwoDigitTextDisplay(source.name, line);
      this.optimizations.push({
        name: "screen-text-lowering",
        detail: `Lowered screen ${display.name} as visible text ${JSON.stringify(literal.text)} plus ${source.name}.`
      });
      return true;
    }
    emitTwoDigitTextDisplay(source, line) {
      this.emitRecall(source, "text display verse", line);
      this.emitOp(1, "1", "text tens divisor", line);
      this.emitOp(0, "0", "text tens divisor", line);
      this.emitOp(19, "/", "text tens", line);
      this.emitOp(52, "\u041A [x]", "text tens integer", line);
      this.emitOp(65, "X->\u041F 1", "text tens scratch", line);
      this.emitOp(15, "F \u0412x", "text ones from last X", line);
      this.emitOp(53, "\u041A {x}", "text ones fraction", line);
      this.emitOp(1, "1", "text ones scale", line);
      this.emitOp(0, "0", "text ones scale", line);
      this.emitOp(18, "*", "text ones", line);
      this.emitOp(66, "X->\u041F 2", "text ones scratch", line);
      this.emitOp(1, "1", "text display tens prefix", line);
      this.emitOp(1, "1", "text display tens prefix", line);
      this.emitOp(72, "X->\u041F 8", "text display prefix scratch", line);
      this.emitOp(1, "1", "text display tens offset", line);
      this.emitOp(2, "2", "text display tens offset", line);
      this.emitOp(71, "X->\u041F 7", "text display digit offset", line);
      this.emitOp(98, "\u041F->X 2", "text display ones digit", line);
      this.emitJump(83, "\u041F\u041F", 34, "text digit renderer", line);
      this.emitOp(74, "X->\u041F a", "text display rendered ones", line);
      this.emitOp(1, "1", "text display ones prefix", line);
      this.emitOp(4, "4", "text display ones prefix", line);
      this.emitOp(72, "X->\u041F 8", "text display prefix scratch", line);
      this.emitOp(1, "1", "text display ones offset", line);
      this.emitOp(3, "3", "text display ones offset", line);
      this.emitOp(71, "X->\u041F 7", "text display digit offset", line);
      this.emitOp(97, "\u041F->X 1", "text display tens digit", line);
      this.emitJump(83, "\u041F\u041F", 34, "text digit renderer", line);
      this.emitOp(106, "\u041F->X a", "text display rendered ones", line);
      this.emitOp(14, "\u0412\u2191", "show text", line);
      this.emitOp(80, "\u0421/\u041F", "show text", line);
      this.emitOp(6, "6", "text digit renderer", line);
      this.emitOp(17, "-", "text digit renderer", line);
      this.emitOp(11, "/-/", "text digit renderer", line);
      this.emitJump(92, "F x<0", 45, "text digit renderer", line);
      this.emitOp(9, "9", "text digit complement", line);
      this.emitOp(16, "+", "text digit complement", line);
      this.emitOp(215, "\u041A \u041F->X 7", "text display byte", line);
      this.emitOp(16, "+", "text display byte", line);
      this.emitOp(58, "\u041A \u0418\u041D\u0412", "text visible digit", line);
      this.emitOp(82, "\u0412/\u041E", "text digit return", line);
      this.emitOp(1, "1", "text digit complement", line);
      this.emitOp(16, "+", "text digit complement", line);
      this.emitOp(215, "\u041A \u041F->X 7", "text display byte", line);
      this.emitOp(16, "+", "text display byte", line);
      this.emitOp(58, "\u041A \u0418\u041D\u0412", "text visible digit", line);
      this.emitOp(216, "\u041A \u041F->X 8", "text display prefix", line);
      this.emitOp(14, "\u0412\u2191", "text digit return value", line);
      this.emitOp(82, "\u0412/\u041E", "text digit return", line);
    }
    findStateField(name) {
      for (const state of this.ast.states) {
        const field = state.fields.find((candidate) => candidate.name === name);
        if (field !== void 0) return field;
      }
      return void 0;
    }
    currentAddress() {
      return this.items.filter((item) => item.kind !== "label").length;
    }
    compileBlockCall(blockName, line) {
      const block = this.ast.blocks.find((candidate) => candidate.name === blockName);
      const proc = this.ast.procs.find((candidate) => candidate.name === blockName);
      if (!block && proc) {
        if (this.inlineProcNames.has(proc.name)) {
          this.compileStatements(proc.body);
          this.optimizations.push({
            name: "single-use-rule-inline",
            detail: `Inlined single-use rule ${proc.name} at line ${line}.`
          });
          return;
        }
        if (this.statementsTerminate(proc.body)) {
          this.emitJump(81, "\u0411\u041F", proc.name, `terminal rule ${proc.name}`, line);
          this.optimizations.push({
            name: "terminal-rule-tail-call",
            detail: `Compiled terminal rule ${proc.name} as a direct jump instead of a subroutine call.`
          });
          return;
        }
        this.emitJump(83, "\u041F\u041F", proc.name, `proc call ${proc.name}`, line);
        this.optimizations.push({
          name: "proc-call-lowering",
          detail: `Compiled call to rule ${proc.name} as \u041F\u041F/\u0412/\u041E subroutine.`
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
          detail: `Inlined block ${block.name} at line ${line}.`
        });
        return;
      }
      this.emitJump(81, "\u0411\u041F", block.name, `${block.mode} call ${block.name}`, line);
      this.optimizations.push({
        name: block.mode === "shared_tail" ? "shared-tail-layout" : "tail-call-layout",
        detail: `Compiled call to ${block.name} as direct tail jump.`
      });
    }
    compileTrap(statement) {
      this.compileExpression(statement.expr);
      const mapping = {
        zero: [35, "F 1/x"],
        nonpositive: [23, "F lg"],
        negative: [33, "F sqrt"],
        gt_one: [25, "F sin^-1"],
        ge_100: [21, "F 10^x"]
      };
      const [opcode, mnemonic] = mapping[statement.trap];
      this.emitOp(
        opcode,
        mnemonic,
        `trap ${statement.trap}`,
        statement.line
      );
      this.optimizations.push({
        name: "error-stop",
        detail: `Used ${mnemonic} as trap ${statement.trap} at line ${statement.line}.`
      });
    }
    compileUnitDecrement(statement) {
      if (!isUnitDecrementExpression(statement.target, statement.expr)) return false;
      const register = this.allocation.registers[statement.target];
      if (register === void 0) return false;
      const opcode = flOpcode(register);
      if (opcode === void 0) return false;
      const after = this.freshLabel("fl_decrement_done");
      this.emitJump(opcode, getOpcode(opcode).name, after, `decrement ${statement.target}`, statement.line);
      this.emitLabel(after);
      this.optimizations.push({
        name: "fl-unit-decrement",
        detail: `Lowered ${statement.target} -= 1 through ${getOpcode(opcode).name}.`
      });
      return true;
    }
    compileCondition(condition, falseLabel, line) {
      if (this.compileNegativeZeroThresholdFlow(condition, falseLabel, line)) return;
      const selected = selectCheaperEquivalentCondition(
        condition,
        this.ast,
        new Set(Object.keys(this.allocation.constants))
      );
      if (selected.changed) {
        this.optimizations.push({
          name: "comparison-boundary-normalization",
          detail: `Normalized ${conditionToText(condition)} to ${conditionToText(selected.condition)} at line ${line}.`
        });
      }
      const compiledCondition = selected.condition;
      if (isZeroExpression(compiledCondition.right) && canTestAgainstZeroDirectly(compiledCondition.op)) {
        const usedSpatialHit = this.compileBitHasConditionWithSpatialHelper(compiledCondition.left, line);
        if (!usedSpatialHit) {
          this.compileExpression(compiledCondition.left);
        }
        const opcode2 = directTestOpcode(compiledCondition.op);
        this.emitJump(opcode2, getOpcode(opcode2).name, falseLabel, `false branch for ${condition.op}`, line);
        this.optimizations.push({
          name: usedSpatialHit ? "spatial-hit-condition-helper" : "zero-condition-test",
          detail: usedSpatialHit ? `Tested bit_has() through the shared spatial hit helper at line ${line}.` : `Tested ${compiledCondition.op} 0 without materializing a zero literal at line ${line}.`
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
      this.emitOp(17, "-", "condition compare", line);
      const opcode = compiledCondition.op === "<" || compiledCondition.op === ">" ? 92 : compiledCondition.op === ">=" || compiledCondition.op === "<=" ? 89 : compiledCondition.op === "==" ? 94 : 87;
      const mnemonic = getOpcode(opcode).name;
      this.emitJump(opcode, mnemonic, falseLabel, `false branch for ${compiledCondition.op}`, line);
    }
    compileNegativeZeroThresholdFlow(condition, falseLabel, line) {
      const register = this.allocation.negativeZeroDegree;
      const threshold = matchNegativeZeroThresholdCondition(condition, this.ast);
      if (threshold === void 0) return false;
      const preloadedConstants = new Set(Object.keys(this.allocation.constants));
      const selectedCost = estimateNegativeZeroThresholdFlowCost(threshold, preloadedConstants);
      const ordinaryCost = conditionCompileCost(
        selectCheaperEquivalentCondition(condition, this.ast, preloadedConstants).condition,
        preloadedConstants
      );
      if (register === void 0 || selectedCost >= ordinaryCost) {
        this.candidates.push({
          site: `if@${line}`,
          variant: "negative-zero-threshold-flow",
          steps: selectedCost,
          selected: false,
          reason: selectedCost >= ordinaryCost ? `Negative-zero threshold flow estimated at ${selectedCost} cells; ordinary condition was shorter (${ordinaryCost}).` : "Negative-zero threshold flow matched, but no compiler-owned negative-zero register was available."
        });
        return false;
      }
      this.emitNegativeZeroThresholdRaw(threshold.value, numberExpression(threshold.bound), register, line);
      const opcode = threshold.truth === "ge" ? 87 : 94;
      this.emitJump(opcode, getOpcode(opcode).name, falseLabel, `negative-zero false branch for ${condition.op}`, line);
      this.optimizations.push({
        name: "negative-zero-threshold-flow",
        detail: `Tested ${conditionToText(condition)} through preloaded ${NEGATIVE_ZERO_DEGREE_PRELOAD_VALUE} in R${register}.`
      });
      return true;
    }
    emitNegativeZeroThresholdRaw(value, bound, register, line) {
      this.compileExpression(divideExpressions(value, bound));
      this.emitOp(96 + registerIndex(register), `\u041F->X ${register}`, "negative-zero threshold sentinel", line);
      this.emitOp(20, "X\u2194Y", "place threshold value above negative-zero sentinel", line);
      this.emitOp(18, "*", "negative-zero threshold zero-through", line);
      this.emitOp(14, "\u0412\u2191", "normalize negative-zero threshold result", line);
    }
    compileBitHasConditionWithSpatialHelper(expr, line) {
      if (expr.kind !== "call" || expr.callee.toLowerCase() !== "bit_has" || expr.args.length !== 2) return false;
      const [mask, index] = expr.args;
      if (mask?.kind !== "identifier" || index === void 0) return false;
      const scratch = spatialHitScratchName(mask.name);
      if (!this.allocation.registers[scratch]) return false;
      const helper = this.ensureSpatialHitHelper(mask.name, scratch);
      this.compileExpression(index);
      this.emitJump(83, "\u041F\u041F", helper.label, `spatial hit ${mask.name}`, line);
      return true;
    }
    compileEqualityWithCurrentX(condition, falseLabel, line) {
      if (condition.op !== "==" && condition.op !== "!=") return false;
      const reused = this.currentXVariable;
      if (condition.right.kind === "identifier" && condition.right.name === reused && isSimpleStackLoad(condition.left)) {
        this.compileExpression(condition.left);
      } else if (condition.left.kind === "identifier" && condition.left.name === reused && isSimpleStackLoad(condition.right)) {
        this.compileExpression(condition.right);
      } else {
        return false;
      }
      this.emitOp(17, "-", "condition compare", line);
      const opcode = condition.op === "==" ? 94 : 87;
      this.emitJump(opcode, getOpcode(opcode).name, falseLabel, `false branch for ${condition.op}`, line);
      this.optimizations.push({
        name: "condition-current-x-reuse",
        detail: `Reused ${reused} already in X for equality comparison at line ${line}.`
      });
      return true;
    }
    compileExpression(expr) {
      const helper = this.sharedExpressionHelper(expr);
      if (helper !== void 0) {
        this.emitJump(83, "\u041F\u041F", helper.label, `expr ${expressionToIntentText(expr)}`);
        this.optimizations.push({
          name: "expression-helper-call",
          detail: `Reused shared helper for ${expressionToIntentText(expr)}.`
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
                message: `Cyclic constant reference '${expr.name}'.`
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
          this.emitOp(11, "/-/", "unary minus");
          return;
        case "binary":
          if (this.compileRemainderByConstant(expr)) {
            return;
          }
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
    orderDisplaySources(sources) {
      if (this.currentXVariable === void 0) return sources;
      const index = sources.indexOf(this.currentXVariable);
      if (index <= 0) return sources;
      this.optimizations.push({
        name: "display-stack-reuse",
        detail: `Reordered packed display inputs to reuse ${this.currentXVariable} already in X.`
      });
      return [
        this.currentXVariable,
        ...sources.slice(0, index),
        ...sources.slice(index + 1)
      ];
    }
    compileCommutativeWithCurrentX(expr) {
      if (this.currentXVariable === void 0) return false;
      if (expr.left.kind === "identifier" && expr.left.name === this.currentXVariable && isSimpleStackLoad(expr.right)) {
        this.compileExpression(expr.right);
        this.emitOp(binaryOpcode(expr.op), expr.op, `expr ${expr.op}`);
        this.optimizations.push({
          name: "stack-current-x-scheduling",
          detail: `Reused ${expr.left.name} already in X for commutative ${expr.op}.`
        });
        return true;
      }
      if (expr.right.kind === "identifier" && expr.right.name === this.currentXVariable && isSimpleStackLoad(expr.left)) {
        this.compileExpression(expr.left);
        this.emitOp(binaryOpcode(expr.op), expr.op, `expr ${expr.op}`);
        this.optimizations.push({
          name: "stack-current-x-scheduling",
          detail: `Reused ${expr.right.name} already in X for commutative ${expr.op}.`
        });
        return true;
      }
      return false;
    }
    compileRemainderByConstant(expr) {
      const matched = matchRemainderByConstant(expr);
      if (matched === void 0) return false;
      this.compileExpression(matched.value);
      this.compileExpression(matched.divisor);
      this.emitOp(19, "/", "remainder quotient");
      this.emitOp(53, "\u041A {x}", "remainder fractional part");
      this.compileExpression(matched.divisor);
      this.emitOp(18, "*", "remainder scale");
      this.optimizations.push({
        name: "remainder-fraction-lowering",
        detail: `Lowered ${expressionToIntentText(expr)} without recomputing the dividend.`
      });
      return true;
    }
    sharedExpressionHelper(expr) {
      if (this.emittingExpressionHelper) return void 0;
      if (!this.shouldShareExpression(expr)) return void 0;
      const key = expressionToIntentText(expr);
      const existing = this.expressionHelpers.get(key);
      if (existing !== void 0) return existing;
      const helper = {
        expr,
        label: `__expr_${this.expressionHelpers.size}`
      };
      this.expressionHelpers.set(key, helper);
      return helper;
    }
    shouldShareExpression(expr) {
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
    compileCall(expr) {
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
          message: `${expr.callee}() expects ${ticTacToeMacroArity(name)} arguments, got ${expr.args.length}.`
        });
        return;
      }
      const macro = ticTacToeExpressionMacro(name, expr.args);
      if (macro !== void 0) {
        this.compileExpression(macro);
        this.optimizations.push({
          name: "tic-tac-toe-primitive-lowering",
          detail: `Lowered ${expr.callee}() to reusable 4x4 grid/packed-line arithmetic.`
        });
        return;
      }
      const zeroArgOpcodes = {
        random: [59, "\u041A \u0421\u0427"],
        pi: [32, "F pi"]
      };
      const zeroArgOpcode = zeroArgOpcodes[name];
      if (zeroArgOpcode !== void 0) {
        if (expr.args.length !== 0) {
          this.diagnostics.push({
            level: "error",
            message: `${expr.callee}() takes no arguments, got ${expr.args.length}.`
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
            message: "Function pow expects two arguments."
          });
          return;
        }
        this.compileExpression(expr.args[1]);
        this.compileExpression(expr.args[0]);
        this.emitOp(36, "F x^y", `${expr.callee}()`);
        return;
      }
      const binaryOpcodes = {
        max: [54, "\u041A max"],
        bit_and: [55, "\u041A \u2227"],
        bit_or: [56, "\u041A \u2228"],
        bit_xor: [57, "\u041A \u2295"]
      };
      const binaryCall = binaryOpcodes[name];
      if (binaryCall !== void 0) {
        if (expr.args.length !== 2) {
          this.diagnostics.push({
            level: "error",
            message: `Function ${expr.callee} expects two arguments.`
          });
          return;
        }
        this.compileExpression(expr.args[0]);
        this.compileExpression(expr.args[1]);
        this.emitOp(binaryCall[0], binaryCall[1], `${expr.callee}()`);
        return;
      }
      if (expr.args.length !== 1) {
        this.diagnostics.push({
          level: "error",
          message: `Function ${expr.callee} expects one argument.`
        });
        return;
      }
      this.compileExpression(expr.args[0]);
      const opcodes = {
        abs: [49, "\u041A |x|"],
        sign: [50, "\u041A \u0417\u041D"],
        int: [52, "\u041A [x]"],
        frac: [53, "\u041A {x}"],
        sqr: [34, "F x^2"],
        inv: [35, "F 1/x"],
        sqrt: [33, "F sqrt"],
        lg: [23, "F lg"],
        ln: [24, "F ln"],
        sin: [28, "F sin"],
        cos: [29, "F cos"],
        tg: [30, "F tg"],
        asin: [25, "F sin^-1"],
        acos: [26, "F cos^-1"],
        atg: [27, "F tg^-1"],
        exp: [22, "F e^x"],
        pow10: [21, "F 10^x"],
        bit_not: [58, "\u041A \u0418\u041D\u0412"],
        to_min: [38, "\u041A \xB0->\u2032"],
        to_sec: [42, '\u041A \xB0->\u2032"'],
        from_sec: [48, '\u041A \xB0<-\u2032"'],
        from_min: [51, "\u041A \xB0<-\u2032"]
      };
      const opcode = opcodes[name];
      if (!opcode) {
        this.diagnostics.push({
          level: "error",
          message: `Unknown function ${expr.callee}.`
        });
        return;
      }
      this.emitOp(opcode[0], opcode[1], `${expr.callee}()`);
    }
    compileDirectionCall(expr) {
      if (expr.args.length !== 1) {
        this.diagnostics.push({
          level: "error",
          message: `direction() expects one keypad argument, got ${expr.args.length}.`
        });
        return;
      }
      const arg = expr.args[0];
      if (arg.kind !== "identifier") {
        this.diagnostics.push({
          level: "error",
          message: "direction() currently requires an identifier argument so the optimizer can reuse its register."
        });
        return;
      }
      const keyRegister = this.allocation.registers[arg.name];
      if (!keyRegister) {
        this.diagnostics.push({
          level: "error",
          message: `Unknown direction key '${arg.name}'.`
        });
        return;
      }
      const notFloor = this.freshLabel("direction_not_floor");
      const yAxis = this.freshLabel("direction_y_axis");
      const done = this.freshLabel("direction_done");
      this.emitOp(96 + registerIndex(keyRegister), `\u041F->X ${keyRegister}`, "direction key");
      this.emitOp(49, "\u041A |x|", "direction floor test");
      this.emitNumberOrPreload("5");
      this.emitOp(17, "-", "direction abs(key)-5");
      this.emitJump(94, "F x=0", notFloor, "direction not floor");
      this.emitOp(96 + registerIndex(keyRegister), `\u041F->X ${keyRegister}`, "direction floor key");
      this.emitNumberOrPreload("20");
      this.emitOp(18, "*", "direction floor delta");
      this.emitJump(81, "\u0411\u041F", done, "direction done");
      this.emitLabel(notFloor);
      this.emitOp(49, "\u041A |x|", "direction x-axis test");
      this.emitNumberOrPreload("1");
      this.emitOp(17, "-", "direction axis discriminator");
      this.emitJump(94, "F x=0", yAxis, "direction y-axis");
      this.emitOp(96 + registerIndex(keyRegister), `\u041F->X ${keyRegister}`, "direction x key");
      this.emitNumberOrPreload("5");
      this.emitOp(17, "-", "direction key-5");
      this.emitNumberOrPreload("10");
      this.emitOp(18, "*", "direction x delta");
      this.emitJump(81, "\u0411\u041F", done, "direction done");
      this.emitLabel(yAxis);
      this.emitNumberOrPreload("5");
      this.emitOp(96 + registerIndex(keyRegister), `\u041F->X ${keyRegister}`, "direction y key");
      this.emitOp(17, "-", "direction 5-key");
      this.emitNumberOrPreload("3");
      this.emitOp(19, "/", "direction y delta");
      this.emitLabel(done);
      this.optimizations.push({
        name: "direction-keypad-lowering",
        detail: `Lowered direction(${arg.name}) through a shared keypad geometry formula.`
      });
    }
    compileSpatialCountCall(name, expr) {
      if (expr.args.length !== 2) {
        this.diagnostics.push({
          level: "error",
          message: `${name}() expects two arguments, got ${expr.args.length}.`
        });
        return true;
      }
      if (name === "line_count" && this.compileSpatialLineCountLoop(expr)) return true;
      const expanded = spatialCountExpression(name, expr.args, this.ast);
      if (expanded === void 0) return false;
      this.compileExpression(expanded);
      this.optimizations.push({
        name: "spatial-count-hit-helper",
        detail: `Lowered ${name}() through shared spatial hit helper calls.`
      });
      return true;
    }
    compileSpatialNeighborCountLoop(expr) {
      const [mask, cell] = expr.args;
      if (mask?.kind !== "identifier" || cell === void 0) return false;
      const board = boardForCellMask(mask, this.ast);
      const scratch = spatialCountScratchNames();
      if (scratch.some((name) => this.allocation.registers[name] === void 0)) return false;
      const progressions = spatialNeighborProgressions(board);
      this.emitSpatialNeighborCountLoopBody(mask.name, cell, progressions, void 0);
      this.optimizations.push({
        name: "spatial-neighbor-count-loop",
        detail: `Lowered neighbor_count(${mask.name}, ...) as spatial hit loops.`
      });
      return true;
    }
    emitSpatialNeighborCountLoopBody(hitMask, cell, progressions, sourceLine) {
      const scratch = spatialCountScratchNames();
      const total = scratch[0];
      const offset = scratch[2];
      const counter = scratch[3];
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
        this.emitJump(83, "\u041F\u041F", helper.label, `spatial hit ${hitMask}`, sourceLine);
        this.emitRecall(total, "neighbor_count total");
        this.emitOp(16, "+", "neighbor_count add hit");
        this.emitStore(total, "neighbor_count total");
        this.emitRecall(offset, "neighbor_count offset");
        this.compileExpression(progression.step);
        this.emitOp(16, "+", "neighbor_count next offset");
        this.emitStore(offset, "neighbor_count offset");
        const counterRegister = this.allocation.registers[counter];
        const flCounterOpcode = counterRegister === void 0 ? void 0 : flOpcode(counterRegister);
        if (flCounterOpcode !== void 0) {
          this.emitJump(flCounterOpcode, getOpcode(flCounterOpcode).name, start, "neighbor_count loop", sourceLine);
          this.optimizations.push({
            name: "spatial-count-fl-loop",
            detail: `Used ${getOpcode(flCounterOpcode).name} for neighbor_count loop counter.`
          });
        } else {
          this.emitRecall(counter, "neighbor_count counter");
          this.emitNumber("1");
          this.emitOp(17, "-", "neighbor_count decrement");
          this.emitStore(counter, "neighbor_count counter");
          this.emitRecall(counter, "neighbor_count counter");
          this.emitJump(94, "F x=0", start, "neighbor_count loop", sourceLine);
        }
      }
      this.emitRecall(total, "neighbor_count result");
    }
    compileSpatialLineCountLoop(expr) {
      const [mask, cell] = expr.args;
      if (mask?.kind !== "identifier" || cell === void 0) return false;
      const board = boardForCellMask(mask, this.ast);
      if (board === void 0) return false;
      const scratch = spatialCountScratchNames();
      if (scratch.some((name) => this.allocation.registers[name] === void 0)) return false;
      const maskScratch = spatialCountMaskScratchName();
      const useSharedMask = this.lineCountCallCount > 1 && this.allocation.registers[maskScratch] !== void 0;
      const hitMask = useSharedMask ? maskScratch : mask.name;
      const helper = this.sharedLineCountHelper(mask, cell, board, void 0);
      if (helper !== void 0) {
        this.compileExpression(mask);
        this.emitJump(83, "\u041F\u041F", helper.label, `line_count ${mask.name}`, void 0);
        this.optimizations.push({
          name: "spatial-line-count-helper-call",
          detail: `Reused shared line_count helper for ${mask.name}.`
        });
        return true;
      }
      if (useSharedMask) {
        this.compileExpression(mask);
        this.emitStore(maskScratch, "line count mask", void 0);
      }
      this.emitSpatialLineCountLoopBody(hitMask, cell, board, void 0);
      return true;
    }
    emitSpatialLineCountLoopBody(hitMask, cell, board, sourceLine) {
      this.emitSpatialProgressionCountLoopBody(
        hitMask,
        cell,
        spatialLineProgressions(board, cell),
        board.width <= 4 && board.height <= 4,
        sourceLine,
        "line_count"
      );
    }
    emitSpatialProgressionCountLoopBody(hitMask, cell, progressions, useMax, sourceLine, operation) {
      const scratch = spatialCountScratchNames();
      const total = scratch[0];
      const line = scratch[1];
      const offset = scratch[2];
      const counter = scratch[3];
      this.emitNumber("0");
      this.emitStore(total, `${operation} total`, sourceLine);
      for (const progression of progressions) {
        this.emitNumber("0");
        this.emitStore(line, `${operation} current line`, sourceLine);
        this.compileExpression(progression.startOffset);
        this.emitStore(offset, `${operation} offset`, sourceLine);
        this.emitNumber(String(progression.count));
        this.emitStore(counter, `${operation} counter`, sourceLine);
        const start = this.freshLabel(`${operation}_loop`);
        this.emitLabel(start);
        this.compileExpression(addExpressions(cell, { kind: "identifier", name: offset }));
        const helper = this.ensureSpatialHitHelper(hitMask, spatialHitScratchName(hitMask));
        this.emitJump(83, "\u041F\u041F", helper.label, `spatial hit ${hitMask}`, sourceLine);
        this.emitRecall(line, `${operation} accumulator`);
        this.emitOp(16, "+", `${operation} add hit`);
        this.emitStore(line, `${operation} accumulator`);
        this.emitRecall(offset, `${operation} offset`);
        this.compileExpression(progression.step);
        this.emitOp(16, "+", `${operation} next offset`);
        this.emitStore(offset, `${operation} offset`);
        const counterRegister = this.allocation.registers[counter];
        const flCounterOpcode = counterRegister === void 0 ? void 0 : flOpcode(counterRegister);
        if (flCounterOpcode !== void 0) {
          this.emitJump(flCounterOpcode, getOpcode(flCounterOpcode).name, start, `${operation} loop`, sourceLine);
          this.optimizations.push({
            name: "spatial-count-fl-loop",
            detail: `Used ${getOpcode(flCounterOpcode).name} for ${operation} loop counter.`
          });
        } else {
          this.emitRecall(counter, `${operation} counter`);
          this.emitNumber("1");
          this.emitOp(17, "-", `${operation} decrement`);
          this.emitStore(counter, `${operation} counter`);
          this.emitRecall(counter, `${operation} counter`);
          this.emitJump(94, "F x=0", start, `${operation} loop`, sourceLine);
        }
        this.emitRecall(total, `${operation} total`);
        this.emitRecall(line, `${operation} current line`);
        if (useMax) {
          this.emitOp(54, "\u041A max", `${operation} best line`);
        } else {
          this.emitOp(16, "+", `${operation} total add line`);
        }
        this.emitStore(total, `${operation} total`);
      }
      this.emitRecall(total, `${operation} result`);
      this.optimizations.push({
        name: `spatial-${operation.replace("_", "-")}-loop`,
        detail: `Lowered ${operation}(...) as shared spatial hit loops.`
      });
    }
    compileSpatialHitCall(expr) {
      if (expr.args.length !== 2) {
        this.diagnostics.push({
          level: "error",
          message: "__spatial_hit() expects two arguments."
        });
        return true;
      }
      const [mask, index] = expr.args;
      if (mask?.kind !== "identifier" || index === void 0) return false;
      const scratch = spatialHitScratchName(mask.name);
      if (!this.allocation.registers[scratch]) return false;
      const helper = this.ensureSpatialHitHelper(mask.name, scratch);
      this.compileExpression(index);
      this.emitJump(83, "\u041F\u041F", helper.label, `spatial hit ${mask.name}`, helper.line);
      return true;
    }
    compileNegativeZeroDegreeSelectorCall(expr) {
      if (expr.args.length !== 2) {
        this.diagnostics.push({
          level: "error",
          message: `${NEGATIVE_ZERO_DEGREE_SELECTOR_GE}() expects two arguments.`
        });
        return true;
      }
      const register = this.allocation.negativeZeroDegree;
      if (register === void 0) {
        this.diagnostics.push({
          level: "error",
          message: "Internal: negative-zero threshold selector was emitted without a reserved register."
        });
        return true;
      }
      const [value, bound] = expr.args;
      if (value === void 0 || bound === void 0) return true;
      this.emitNegativeZeroThresholdRaw(value, bound, register);
      this.emitOp(50, "\u041A \u0417\u041D", "negative-zero threshold selector");
      this.optimizations.push({
        name: "negative-zero-threshold-selector",
        detail: `Used preloaded ${NEGATIVE_ZERO_DEGREE_PRELOAD_VALUE} in R${register} for ${expressionToIntentText(value)} >= ${expressionToIntentText(bound)}.`
      });
      return true;
    }
    sharedLineCountHelper(mask, cell, board, line) {
      if (this.lineCountCallCount < 2) return void 0;
      if (this.allocation.registers[spatialCountMaskScratchName()] === void 0) return void 0;
      const key = lineCountGroupKeyFor(board, cell);
      if ((this.lineCountGroupCounts.get(key) ?? 0) < 2) return void 0;
      if (mask.kind !== "identifier") return void 0;
      const existing = this.lineCountHelpers.get(key);
      if (existing !== void 0) return existing;
      const helper = {
        cell,
        board,
        label: `__line_count_${this.lineCountHelpers.size}`,
        ...line === void 0 ? {} : { line }
      };
      this.lineCountHelpers.set(key, helper);
      return helper;
    }
    ensureSpatialHitHelper(mask, scratch) {
      const existing = this.spatialHitHelpers.get(mask);
      if (existing !== void 0) return existing;
      const helper = {
        mask,
        scratch,
        label: `__spatial_hit_${mask}`
      };
      this.spatialHitHelpers.set(mask, helper);
      return helper;
    }
    compileRawStatement(statement) {
      const inputs = statement.inputs ?? [];
      const outputs = statement.outputs ?? [];
      for (const input of orderRawInputs(inputs)) {
        this.compileExpression(input.expr);
      }
      this.compileRawLines(statement.lines, statement.strict ?? false);
      for (const output of outputs) {
        this.emitStore(output.target, `raw returns ${output.slot}`, output.line);
      }
      if (inputs.length > 0 || outputs.length > 0 || statement.clobbers !== void 0 || statement.preserves !== void 0) {
        this.optimizations.push({
          name: "raw-block-contract",
          detail: formatRawContractDetail(statement)
        });
      }
    }
    compileRawLines(lines, strict = false) {
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
            line: line.line
          });
          continue;
        }
        this.emitOp(parsed.opcode, parsed.mnemonic, parsed.comment, line.line, true);
        if (parsed.formalTargetOpcode !== void 0) {
          this.emitFormalAddress(parsed.formalTargetOpcode, parsed.comment ?? parsed.mnemonic, line.line);
        } else if (parsed.target !== void 0) {
          this.emitAddress(parsed.target, parsed.comment ?? parsed.mnemonic, line.line);
        }
      }
    }
    emitNumber(raw) {
      this.currentXVariable = void 0;
      const normalized = raw.trim().toLowerCase();
      const negative = normalized.startsWith("-");
      const unsigned = negative ? normalized.slice(1) : normalized;
      const [mantissa, exponent] = unsigned.split("e");
      for (const char of mantissa ?? "0") {
        if (char === ".") this.emitOp(10, ".");
        else if (/\d/u.test(char)) this.emitOp(Number(char), char);
      }
      if (exponent !== void 0) {
        this.emitOp(12, "\u0412\u041F", "exponent");
        const expNegative = exponent.startsWith("-");
        const expDigits = expNegative || exponent.startsWith("+") ? exponent.slice(1) : exponent;
        for (const char of expDigits) {
          if (/\d/u.test(char)) this.emitOp(Number(char), char);
        }
        if (expNegative) this.emitOp(11, "/-/", "negative exponent");
      }
      if (negative) this.emitOp(11, "/-/", "negative number");
    }
    emitNumberOrPreload(raw) {
      const normalized = normalizeConstantLiteral(raw);
      const register = this.allocation.constants[normalized];
      if (register !== void 0) {
        this.emitOp(96 + registerIndex(register), `\u041F->X ${register}`, `preload const ${normalized}`);
        this.optimizations.push({
          name: "preloaded-constant",
          detail: `Used preloaded R${register} for constant ${normalized}.`
        });
        return;
      }
      this.emitNumber(raw);
    }
    emitStore(name, comment, sourceLine) {
      const register = this.allocation.registers[name];
      if (!register) {
        this.diagnostics.push(buildDiagnostic("error", `No register allocated for ${name}`, sourceLine));
        return;
      }
      this.emitOp(64 + registerIndex(register), `X->\u041F ${register}`, comment, sourceLine);
      this.currentXVariable = name;
    }
    emitRecall(name, comment, sourceLine) {
      const register = this.allocation.registers[name];
      if (!register) {
        this.diagnostics.push(buildDiagnostic("error", `Unknown variable '${name}'`, sourceLine));
        return;
      }
      this.emitOp(96 + registerIndex(register), `\u041F->X ${register}`, comment, sourceLine);
      this.currentXVariable = name;
    }
    emitJump(opcode, mnemonic, target, comment, sourceLine) {
      this.emitOp(opcode, mnemonic, comment, sourceLine);
      this.emitAddress(target, comment ?? mnemonic, sourceLine);
    }
    emitAddress(target, comment, sourceLine) {
      const item = { kind: "address", target };
      if (comment !== void 0) item.comment = comment;
      if (sourceLine !== void 0) item.sourceLine = sourceLine;
      this.items.push(item);
    }
    emitFormalAddress(opcode, comment, sourceLine) {
      const info2 = formalAddressInfo(opcode);
      const item = { kind: "address", target: info2.ordinal, formalOpcode: opcode };
      if (comment !== void 0) item.comment = `${comment}; formal ${info2.label}->${formatAddress(info2.actual)}`;
      if (sourceLine !== void 0) item.sourceLine = sourceLine;
      this.items.push(item);
    }
    emitOp(opcode, mnemonic, comment, sourceLine, raw = false) {
      const info2 = getOpcode(opcode);
      const op = {
        kind: "op",
        opcode,
        mnemonic: mnemonic ?? info2.name
      };
      if (comment !== void 0) op.comment = comment;
      if (sourceLine !== void 0) op.sourceLine = sourceLine;
      if (raw) op.raw = true;
      this.items.push(op);
      this.currentXVariable = void 0;
    }
    emitLabel(name) {
      this.items.push({ kind: "label", name });
    }
    freshLabel(prefix) {
      const label = `__${prefix}_${this.labelCounter}`;
      this.labelCounter += 1;
      return label;
    }
  };
  function findSingleUseProcNames(ast) {
    const procNames = new Set(ast.procs.map((proc) => proc.name));
    const counts = /* @__PURE__ */ new Map();
    const recursive = /* @__PURE__ */ new Set();
    const visit = (statements, currentProc) => {
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
      [...counts.entries()].filter(([name, count]) => count === 1 && !recursive.has(name)).map(([name]) => name)
    );
  }
  function allocateRegisters(ast, diagnostics) {
    const declared = /* @__PURE__ */ new Set();
    const hints = /* @__PURE__ */ new Map();
    const variables = /* @__PURE__ */ new Set();
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
    const flPreferenceOrder = ["2", "3", "1", "0"];
    let flPreferenceIndex = 0;
    for (const variable of collectUnitDecrementTargets(ast)) {
      if (!variables.has(variable) || hints.has(variable)) continue;
      const register = flPreferenceOrder[flPreferenceIndex];
      if (register === void 0) break;
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
    const registers = {};
    const used = /* @__PURE__ */ new Set();
    for (const variable of variables) {
      const hint = hints.get(variable);
      if (hint?.mode === "fixed") {
        if (used.has(hint.register)) {
          diagnostics.push({
            level: "error",
            message: `Register R${hint.register} is fixed for more than one variable.`
          });
        }
        registers[variable] = hint.register;
        used.add(hint.register);
      }
    }
    const ordered = [...variables].sort(
      (a, b) => priority(a, hints) - priority(b, hints) || a.localeCompare(b)
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
          message: `Out of MK-61 registers while allocating '${variable}'.`
        });
        continue;
      }
      registers[variable] = candidate;
      used.add(candidate);
    }
    const constants = {};
    for (const value of collectPreloadConstantValues(ast)) {
      const register = pickConstantRegister(used);
      if (!register) break;
      constants[value] = register;
      used.add(register);
    }
    const negativeZeroDegree = programNeedsNegativeZeroDegree(ast) ? pickConstantRegister(used) : void 0;
    if (negativeZeroDegree !== void 0) used.add(negativeZeroDegree);
    return negativeZeroDegree === void 0 ? { registers, constants } : { registers, constants, negativeZeroDegree };
  }
  function collectDomainBindings(ast) {
    const bindings = [];
    for (const domain of ast.domains) {
      const name = domainBindingName(domain);
      if (!name) continue;
      const binding = { name };
      const register = domainRegisterHint(domain);
      if (register !== void 0) {
        binding.storage = { mode: "fixed", register };
      }
      bindings.push(binding);
    }
    return bindings;
  }
  function domainBindingName(domain) {
    const header = domain.header.trim();
    if (domain.domainKind === "coord") {
      const match = /^coord\s+(?:packed\s+)?([A-Za-z_][\w]*)/u.exec(header);
      return match?.[1];
    }
    if (domain.domainKind === "resource" || domain.domainKind === "bitset") {
      return domain.name;
    }
    return void 0;
  }
  function domainRegisterHint(domain) {
    for (const line of domain.lines) {
      const match = /^register\s+R?([0-9a-eавсде])$/iu.exec(line.text);
      if (match) return registerFromText(match[1]);
    }
    return void 0;
  }
  function applyTicTacToeRegisterHints(ast, variables, hints) {
    if (!programUsesTicTacToeHelpers(ast)) return;
    const preferences = [
      ["x", "1"],
      ["y", "2"],
      ["mask", "9"],
      ["lines", "4"]
    ];
    for (const [name, register] of preferences) {
      if (variables.has(name) && !hints.has(name)) {
        hints.set(name, { mode: "prefer", register });
      }
    }
  }
  function pickRegister(variable, used) {
    if (variable.startsWith(SWITCH_SCRATCH_PREFIX)) {
      for (let i = REGISTER_ORDER.length - 1; i >= 0; i -= 1) {
        const candidate = REGISTER_ORDER[i];
        if (!used.has(candidate)) return candidate;
      }
      return void 0;
    }
    if (variable.startsWith(DISPATCH_SCRATCH_PREFIX)) {
      for (let i = REGISTER_ORDER.length - 1; i >= 0; i -= 1) {
        const candidate = REGISTER_ORDER[i];
        if (!used.has(candidate)) return candidate;
      }
      return void 0;
    }
    if (variable.startsWith(TICTACTOE_MASK_SCRATCH_PREFIX)) {
      for (let i = REGISTER_ORDER.length - 1; i >= 0; i -= 1) {
        const candidate = REGISTER_ORDER[i];
        if (!used.has(candidate)) return candidate;
      }
      return void 0;
    }
    if (variable.startsWith(BIT_MASK_SCRATCH_PREFIX)) {
      for (let i = REGISTER_ORDER.length - 1; i >= 0; i -= 1) {
        const candidate = REGISTER_ORDER[i];
        if (!used.has(candidate)) return candidate;
      }
      return void 0;
    }
    if (variable.startsWith(SPATIAL_COUNT_SCRATCH_PREFIX)) {
      if (variable === spatialCountCounterScratchName()) {
        for (const candidate of ["0", "1", "2", "3"]) {
          if (!used.has(candidate)) return candidate;
        }
      }
      for (let i = REGISTER_ORDER.length - 1; i >= 0; i -= 1) {
        const candidate = REGISTER_ORDER[i];
        if (!used.has(candidate)) return candidate;
      }
      return void 0;
    }
    return REGISTER_ORDER.find((candidate) => !used.has(candidate));
  }
  function pickConstantRegister(used) {
    for (let i = REGISTER_ORDER.length - 1; i >= 0; i -= 1) {
      const candidate = REGISTER_ORDER[i];
      if (!used.has(candidate)) return candidate;
    }
    return void 0;
  }
  function collectPreloadConstantValues(ast) {
    const values = /* @__PURE__ */ new Set();
    if (programContainsCall(ast, "direction")) {
      values.add("20");
      values.add("10");
    }
    const visitExpr = (expr) => {
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
        if (macro !== void 0) {
          visitExpr(macro);
          return;
        }
        for (const arg of expr.args) visitExpr(arg);
      }
    };
    const visitStatements = (statements) => {
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
  function programContainsCall(ast, name) {
    const target = name.toLowerCase();
    let found = false;
    const visitExpr = (expr) => {
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
    const visitStatements = (statements) => {
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
  function programNeedsNegativeZeroDegree(ast) {
    return programVisitsIf(ast, (statement) => statementNeedsNegativeZeroDegree(statement, ast));
  }
  function statementNeedsNegativeZeroDegree(statement, ast) {
    const selected = buildBranchRemovalCandidate(statement, ast, { negativeZeroDegree: true });
    if (selected !== void 0 && selected.name.startsWith("negative-zero-threshold-")) {
      return estimateExpressionCost(selected.expr) + 1 < estimateOrdinaryIfCost(statement, ast);
    }
    const threshold = matchNegativeZeroThresholdCondition(statement.condition, ast);
    if (threshold === void 0) return false;
    return estimateNegativeZeroThresholdFlowCost(threshold, void 0) < estimateConditionCost(statement.condition, ast);
  }
  function programVisitsIf(ast, predicate) {
    let found = false;
    const visitStatements = (statements) => {
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
  function countCalls(ast, name) {
    const target = name.toLowerCase();
    let count = 0;
    const visitExpr = (expr) => {
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
    const visitStatements = (statements) => {
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
  function collectLineCountGroupCounts(ast) {
    const counts = /* @__PURE__ */ new Map();
    const visitExpr = (expr) => {
      if (expr.kind === "call" && expr.callee.toLowerCase() === "line_count" && expr.args.length === 2) {
        const [mask, cell] = expr.args;
        if (mask !== void 0 && cell !== void 0) {
          const board = boardForCellMask(mask, ast);
          if (board !== void 0) {
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
    const visitStatements = (statements) => {
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
  function collectVariableReadCounts(ast) {
    const counts = /* @__PURE__ */ new Map();
    const add = (name) => {
      counts.set(name, (counts.get(name) ?? 0) + 1);
    };
    const visitExpr = (expr) => {
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
    const visitCondition = (condition) => {
      visitExpr(condition.left);
      visitExpr(condition.right);
    };
    const visitStatements = (statements) => {
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
  function collectDisplayUseCounts(ast) {
    const counts = /* @__PURE__ */ new Map();
    const add = (name) => {
      counts.set(name, (counts.get(name) ?? 0) + 1);
    };
    const visit = (statements) => {
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
  function collectShowSequenceUseCounts(ast) {
    const counts = /* @__PURE__ */ new Map();
    const add = (first, second) => {
      const key = showSequenceKey(first, second);
      counts.set(key, (counts.get(key) ?? 0) + 1);
    };
    const visit = (statements) => {
      for (let index = 0; index < statements.length; index += 1) {
        const statement = statements[index];
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
  function showSequenceKey(first, second) {
    return `${first}\0${second}`;
  }
  function collectExpressionUseCounts(ast) {
    const counts = /* @__PURE__ */ new Map();
    const add = (expr) => {
      const key = expressionToIntentText(expr);
      const existing = counts.get(key);
      if (existing === void 0) {
        counts.set(key, { count: 1, expr });
      } else {
        existing.count += 1;
      }
    };
    const visitExpr = (expr) => {
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
    const visitCondition = (condition) => {
      visitExpr(condition.left);
      visitExpr(condition.right);
    };
    const visit = (statements) => {
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
  function estimatePackedDisplayBodyCost(sourceCount) {
    return sourceCount === 0 ? 2 : sourceCount * 2;
  }
  function programUsesTicTacToeHelpers(ast) {
    let found = false;
    const visitExpr = (expr) => {
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
    const visitStatements = (statements) => {
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
  function normalizeConstantLiteral(raw) {
    const value = Number(raw);
    return Number.isFinite(value) ? String(value) : raw.trim().toLowerCase();
  }
  function negatedNumberLiteral(raw) {
    const normalized = raw.trim();
    return normalized.startsWith("-") ? normalized.slice(1) : `-${normalized}`;
  }
  function warnUndeclaredAssignments(ast, declared, diagnostics) {
    const seen = /* @__PURE__ */ new Set();
    const visit = (statements) => {
      for (const statement of statements) {
        if (statement.kind === "assign" || statement.kind === "ask" || statement.kind === "input") {
          if (!declared.has(statement.target) && !seen.has(statement.target)) {
            diagnostics.push({
              level: "warning",
              message: `Implicit allocation for undeclared variable '${statement.target}'. Add 'store ${statement.target}' to silence.`,
              line: statement.line
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
  function collectAssignedVariables(ast, variables) {
    const visit = (statements) => {
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
  function collectUnitDecrementTargets(ast) {
    const targets = /* @__PURE__ */ new Set();
    const visit = (statements) => {
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
  function collectSwitchScratchVariables(ast, variables) {
    const visit = (statements) => {
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
  function collectDispatchScratchVariables(ast, variables) {
    const visit = (statements) => {
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
  function collectTicTacToeScratchVariables(ast, variables) {
    const visit = (statements) => {
      for (let index = 0; index < statements.length; index += 1) {
        const statement = statements[index];
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
  function collectBitMaskScratchVariables(ast, variables) {
    const visit = (statements) => {
      for (let index = 0; index < statements.length; index += 1) {
        const statement = statements[index];
        const next = statements[index + 1];
        if (statement.kind === "assign" && next?.kind === "assign" && isReusableBitSetPair(statement, next)) {
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
  function collectSpatialHitScratchVariables(ast, variables) {
    const shareLineCountMask = countCalls(ast, "line_count") > 1;
    const visitExpr = (expr) => {
      if (shareLineCountMask && expr.kind === "call" && expr.callee.toLowerCase() === "line_count") {
        variables.add(spatialHitScratchName(spatialCountMaskScratchName()));
        for (const arg of expr.args) visitExpr(arg);
        return;
      }
      if (expr.kind === "call" && (expr.callee.toLowerCase() === "neighbor_count" || expr.callee.toLowerCase() === "line_count") && expr.args[0]?.kind === "identifier") {
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
    const visit = (statements) => {
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
  function collectSpatialCountScratchVariables(ast, variables) {
    let needsScratch = false;
    const visitExpr = (expr) => {
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
    const visit = (statements) => {
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
  function isReusableCellMaskPair(first, second) {
    const used = matchCellHelperCall(first.expr, ["cell_used", "cell_has"]);
    const mark = matchCellHelperCall(second.expr, ["cell_mark", "cell_set"]);
    return Boolean(
      used && mark && used.mask.kind === "identifier" && second.target === used.mask.name && expressionEquals(used.mask, mark.mask) && expressionEquals(used.x, mark.x) && expressionEquals(used.y, mark.y)
    );
  }
  function ticTacToeMaskScratchName(statement) {
    return `${TICTACTOE_MASK_SCRATCH_PREFIX}${statement.line}`;
  }
  function dispatchExpressionRegister(statement, allocation) {
    if (statement.expr.kind !== "identifier") return void 0;
    return allocation.registers[statement.expr.name];
  }
  function isZeroExpression(expr) {
    return expr.kind === "number" && Number(expr.raw) === 0;
  }
  function isUnitDecrementExpression(target, expr) {
    return expr.kind === "binary" && expr.op === "-" && expr.left.kind === "identifier" && expr.left.name === target && expr.right.kind === "number" && Number(expr.right.raw) === 1;
  }
  function flOpcode(register) {
    switch (register) {
      case "0":
        return 93;
      case "1":
        return 91;
      case "2":
        return 88;
      case "3":
        return 90;
      default:
        return void 0;
    }
  }
  function isSimpleStackLoad(expr) {
    return expr.kind === "identifier" || expr.kind === "number";
  }
  function canTestAgainstZeroDirectly(op) {
    return op === "==" || op === "!=" || op === ">=" || op === "<";
  }
  function directTestOpcode(op) {
    switch (op) {
      case "==":
        return 94;
      case "!=":
        return 87;
      case ">=":
        return 89;
      case "<":
        return 92;
      default:
        throw new Error(`No direct zero-test opcode for ${op}`);
    }
  }
  function selectCheaperEquivalentCondition(condition, ast, preloadedConstants) {
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
  function equivalentConditionCandidates(condition, ast) {
    const candidates = [];
    const add = (candidate) => {
      if (!candidates.some((existing) => conditionEquals(existing, candidate))) candidates.push(candidate);
    };
    add(condition);
    const flipped = flipNumericLeftCondition(condition);
    if (flipped !== void 0) add(flipped);
    for (const candidate of [...candidates]) {
      for (const boundary of integerBoundaryCandidates(candidate, ast)) add(boundary);
    }
    return candidates;
  }
  function flipNumericLeftCondition(condition) {
    if (condition.left.kind !== "number") return void 0;
    return {
      left: condition.right,
      op: flipComparisonOp(condition.op),
      right: condition.left
    };
  }
  function flipComparisonOp(op) {
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
  function integerBoundaryCandidates(condition, ast) {
    if (!isKnownIntegerExpression(condition.left, ast)) return [];
    const value = numericLiteralValue(condition.right);
    if (value === void 0 || !Number.isSafeInteger(value)) return [];
    const shifted = shiftedIntegerBoundary(condition.op, value);
    if (shifted === void 0) return [];
    return [{
      left: condition.left,
      op: shifted.op,
      right: numberExpression(shifted.value)
    }];
  }
  function shiftedIntegerBoundary(op, value) {
    switch (op) {
      case "<":
        return Number.isSafeInteger(value - 1) ? { op: "<=", value: value - 1 } : void 0;
      case "<=":
        return Number.isSafeInteger(value + 1) ? { op: "<", value: value + 1 } : void 0;
      case ">":
        return Number.isSafeInteger(value + 1) ? { op: ">=", value: value + 1 } : void 0;
      case ">=":
        return Number.isSafeInteger(value - 1) ? { op: ">", value: value - 1 } : void 0;
      case "==":
      case "!=":
        return void 0;
    }
  }
  function isKnownIntegerExpression(expr, ast) {
    return expr.kind === "identifier" && integerRangeFor(expr.name, ast) !== void 0;
  }
  function isKnownIntegerValuedExpression(expr, ast) {
    if (expr.kind === "number") return Number.isSafeInteger(Number(expr.raw));
    if (expr.kind === "identifier") return integerRangeFor(expr.name, ast) !== void 0;
    if (expr.kind === "unary" && expr.op === "-") return isKnownIntegerValuedExpression(expr.expr, ast);
    if (expr.kind === "call" && expr.args.length === 1) {
      const name = expr.callee.toLowerCase();
      if (name === "int") return true;
      if (name === "abs") return isKnownIntegerValuedExpression(expr.args[0], ast);
    }
    if (expr.kind === "binary" && (expr.op === "+" || expr.op === "-" || expr.op === "*")) {
      return isKnownIntegerValuedExpression(expr.left, ast) && isKnownIntegerValuedExpression(expr.right, ast);
    }
    return false;
  }
  function numericRangeForExpression(expr, ast) {
    const value = numericLiteralValue(expr);
    if (value !== void 0) return { min: value, max: value };
    if (expr.kind === "identifier") return numericRangeFor(expr.name, ast);
    if (expr.kind === "unary" && expr.op === "-") {
      const range2 = numericRangeForExpression(expr.expr, ast);
      if (range2 === void 0) return void 0;
      return {
        ...range2.max === void 0 ? {} : { min: -range2.max },
        ...range2.min === void 0 ? {} : { max: -range2.min }
      };
    }
    if (expr.kind === "call" && expr.callee.toLowerCase() === "abs" && expr.args.length === 1) {
      const range2 = numericRangeForExpression(expr.args[0], ast);
      if (range2 === void 0 || range2.min === void 0 || range2.max === void 0) return void 0;
      return {
        min: range2.min <= 0 && range2.max >= 0 ? 0 : Math.min(Math.abs(range2.min), Math.abs(range2.max)),
        max: Math.max(Math.abs(range2.min), Math.abs(range2.max))
      };
    }
    return void 0;
  }
  function conditionCompileCost(condition, preloadedConstants) {
    if (isZeroExpression(condition.right) && canTestAgainstZeroDirectly(condition.op)) {
      return estimateExpressionCostForCondition(condition.left, preloadedConstants) + 2;
    }
    return estimateExpressionCostForCondition(condition.left, preloadedConstants) + estimateExpressionCostForCondition(condition.right, preloadedConstants) + 3;
  }
  function conditionEquals(left, right) {
    return left.op === right.op && expressionEquals(left.left, right.left) && expressionEquals(left.right, right.right);
  }
  function conditionToText(condition) {
    return `${expressionToIntentText(condition.left)} ${condition.op} ${expressionToIntentText(condition.right)}`;
  }
  function expressionToIntentText(expr) {
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
  function wrapExpressionText(expr, parentPrecedence) {
    const text = expressionToIntentText(expr);
    const precedence = expressionPrecedence(expr);
    return precedence < parentPrecedence ? `(${text})` : text;
  }
  function expressionPrecedence(expr) {
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
  function binaryPrecedence(op) {
    return op === "*" || op === "/" ? 2 : 1;
  }
  function priority(variable, hints) {
    const hint = hints.get(variable);
    if (hint?.mode === "prefer") return REGISTER_ORDER.indexOf(hint.register) - 100;
    return 0;
  }
  function optimizeItems(items, options, optimizations) {
    const result = runIrPasses(items, options);
    optimizations.push(...result.optimizations);
    return { items: result.items, preloads: result.preloads };
  }
  function layoutProgram(items, diagnostics, options, ast, machineProfile) {
    const labelAddresses = /* @__PURE__ */ new Map();
    let address = 0;
    for (const item of items) {
      if (item.kind === "label") {
        labelAddresses.set(item.name, address);
      } else {
        address += 1;
      }
    }
    const steps = [];
    const cellRoles = [];
    address = 0;
    for (const item of items) {
      if (item.kind === "label") continue;
      if (!options.analysis && address > 255) {
        diagnostics.push(
          buildDiagnostic("error", `Program address ${address} exceeds formal MK-61 address range.`)
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
      const targetAddress2 = typeof item.target === "number" ? item.target : labelAddresses.get(item.target);
      if (targetAddress2 === void 0) {
        diagnostics.push(
          buildDiagnostic("error", `Unknown label '${item.target}'`, item.sourceLine)
        );
        continue;
      }
      const opcode = item.formalOpcode ?? safeAddressToOpcode(targetAddress2, item.sourceLine, diagnostics, options);
      if (opcode === void 0) {
        address += 1;
        continue;
      }
      steps.push(
        buildResolvedStep(
          address,
          opcode,
          item.formalOpcode === void 0 ? safeFormatAddress(targetAddress2) : formatFormalAddressOpcode(item.formalOpcode),
          item.comment
        )
      );
      cellRoles.push(buildAddressCellRole(address, opcode, item, options, machineProfile));
      address += 1;
    }
    const labels = {};
    const sortedLabels = [...labelAddresses.entries()].sort(
      ([, a], [, b]) => a - b
    );
    for (const [label, labelAddress] of sortedLabels) {
      labels[label] = safeFormatAddress(labelAddress);
    }
    markDarkEntryCells(cellRoles, labelAddresses, options, ast, machineProfile);
    return { steps, labels, cellRoles };
  }
  function buildResolvedStep(address, opcode, mnemonic, comment) {
    const step = {
      address,
      opcode,
      hex: getOpcode(opcode).hex,
      mnemonic
    };
    if (comment !== void 0) step.comment = comment;
    return step;
  }
  function safeAddressToOpcode(address, line, diagnostics, options) {
    if (options.analysis && Number.isInteger(address) && address >= 0) {
      return address & 255;
    }
    try {
      return addressToOpcode(address);
    } catch (error) {
      diagnostics.push(
        buildDiagnostic(
          "error",
          error instanceof Error ? error.message : String(error),
          line
        )
      );
      return void 0;
    }
  }
  function safeFormatAddress(address) {
    try {
      return formatAddress(address);
    } catch {
      return `>${address.toString(16).toUpperCase()}`;
    }
  }
  function buildDiagnostic(level, message, line, code) {
    const diagnostic = { level, message };
    if (line !== void 0) diagnostic.line = line;
    if (code !== void 0) diagnostic.code = code;
    return diagnostic;
  }
  function buildCellRole(address, hex3, item, options, machineProfile) {
    const roles = ["exec"];
    const notes = [];
    if (item.raw) {
      roles.push("constant");
      notes.push("raw opcode can also be read as a byte");
    }
    if (machineSupports(machineProfile, "display-bytes") && item.comment?.includes("display")) {
      roles.push("display-byte");
      notes.push("display byte role allowed");
    }
    const role = {
      address: safeFormatAddress(address),
      hex: hex3,
      roles: uniqueRoles(roles)
    };
    if (notes.length > 0) role.note = notes.join("; ");
    return role;
  }
  function buildAddressCellRole(address, opcode, item, options, machineProfile) {
    const roles = ["address"];
    const notes = [];
    if (item.formalOpcode !== void 0) {
      const info2 = formalAddressInfo(item.formalOpcode);
      roles.push("formal-address");
      notes.push(`formal address ${info2.label} maps to ${safeFormatAddress(info2.actual)} (${info2.kind})`);
      if (info2.kind !== "official" && machineSupports(machineProfile, "dark-entries")) {
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
    const role = {
      address: safeFormatAddress(address),
      hex: getOpcode(opcode).hex,
      roles: uniqueRoles(roles)
    };
    if (notes.length > 0) role.note = notes.join("; ");
    return role;
  }
  function markDarkEntryCells(cellRoles, labelAddresses, options, ast, machineProfile) {
    if (!machineSupports(machineProfile, "dark-entries")) return;
    const sharedTailNames = new Set(
      ast.blocks.filter((block) => block.mode === "shared_tail").map((block) => block.name)
    );
    for (const [label, address] of labelAddresses) {
      if (!sharedTailNames.has(label)) continue;
      const cell = cellRoles.find((candidate) => candidate.address === safeFormatAddress(address));
      if (!cell) continue;
      cell.roles = uniqueRoles([...cell.roles, "dark-entry"]);
      cell.note = [cell.note, "shared tail can be used as dark-entry target"].filter(Boolean).join("; ");
    }
  }
  function uniqueRoles(roles) {
    return [...new Set(roles)];
  }
  function buildBranchRemovalCandidate(statement, ast, options = {}) {
    return buildTerminalSelectCandidate(statement, ast, options) ?? buildComparisonBooleanCandidate(statement) ?? buildBooleanAlgebraCandidate(statement, ast) ?? buildAbsCandidate(statement) ?? buildMaxMinCandidate(statement) ?? buildClampCandidate(statement) ?? buildSaturatingUpdateCandidate(statement, ast) ?? buildBooleanSignToggleCandidate(statement, ast) ?? buildBooleanUpdateCandidate(statement, ast) ?? buildArithmeticIfSelect(statement, ast, options);
  }
  function buildDoubleClampCandidate(first, second) {
    const lower = clampBound(first, "lower");
    const upper = clampBound(second, "upper");
    if (!lower || !upper || lower.target !== upper.target) return void 0;
    const targetExpr = { kind: "identifier", name: lower.target };
    return {
      kind: "assign",
      target: lower.target,
      expr: minExpression(maxExpression(targetExpr, lower.bound), upper.bound),
      name: "arithmetic-if-double-clamp",
      detail: "Replaced adjacent lower/upper clamp branches with min(max())"
    };
  }
  function clampBound(statement, direction) {
    const assign = singleEffectiveAssign(statement);
    if (!assign) return void 0;
    const targetExpr = { kind: "identifier", name: assign.target };
    const { left, right, op } = statement.condition;
    if (!expressionEquals(left, targetExpr) || !expressionEquals(assign.expr, right)) return void 0;
    if (direction === "lower" && (op === "<" || op === "<=")) return { target: assign.target, bound: right };
    if (direction === "upper" && (op === ">" || op === ">=")) return { target: assign.target, bound: right };
    return void 0;
  }
  function buildTerminalSelectCandidate(statement, ast, options = {}) {
    if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) return void 0;
    const thenStatement = effectiveTerminalStatement(statement.thenBody[0], ast);
    const elseStatement = effectiveTerminalStatement(statement.elseBody[0], ast);
    if (!thenStatement || !elseStatement) return void 0;
    if (elseStatement.kind !== thenStatement.kind) return void 0;
    const booleanSelector = booleanSelectorExpression(statement.condition, ast);
    const negativeZeroSelector = options.negativeZeroDegree ? negativeZeroThresholdSelectorExpression(statement.condition, ast) : void 0;
    const selector = booleanSelector ?? negativeZeroSelector ?? comparisonSelectorExpression(statement.condition);
    if (!selector) return void 0;
    const usesNegativeZero = booleanSelector === void 0 && negativeZeroSelector !== void 0;
    return {
      kind: thenStatement.kind,
      expr: terminalSelectExpression(thenStatement.expr, elseStatement.expr, selector),
      name: usesNegativeZero ? "negative-zero-threshold-terminal-select" : "arithmetic-if-terminal-select",
      detail: usesNegativeZero ? `Replaced threshold ${thenStatement.kind} if/else with negative-zero selection` : `Replaced boolean ${thenStatement.kind} if/else with arithmetic selection`
    };
  }
  function effectiveTerminalStatement(statement, ast) {
    if (statement === void 0) return void 0;
    if (statement.kind === "pause" || statement.kind === "halt") return statement;
    if (statement.kind !== "call") return void 0;
    const proc = ast.procs.find((candidate) => candidate.name === statement.block);
    if (proc === void 0 || proc.body.length !== 1) return void 0;
    const terminal = proc.body[0];
    return terminal?.kind === "pause" || terminal?.kind === "halt" ? terminal : void 0;
  }
  function terminalSelectExpression(thenExpr, elseExpr, selector) {
    const thenValue = numericLiteralValue(thenExpr);
    const elseValue = numericLiteralValue(elseExpr);
    if (thenValue !== void 0 && elseValue !== void 0) {
      const delta = thenValue - elseValue;
      if (delta === 0) return numberExpression(thenValue);
      return addExpressions(
        numberExpression(elseValue),
        multiplyExpressions(numberExpression(delta), selector)
      );
    }
    return addExpressions(
      multiplyExpressions(thenExpr, selector),
      multiplyExpressions(elseExpr, oneMinus(selector))
    );
  }
  function comparisonSelectorExpression(condition) {
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
          maxExpression(numberExpression(0), signExpression(subtractExpressions(right, left)))
        );
      case "<=":
        return oneMinus(
          maxExpression(numberExpression(0), signExpression(subtractExpressions(left, right)))
        );
      default:
        return void 0;
    }
  }
  function negativeZeroThresholdSelectorExpression(condition, ast) {
    const threshold = matchNegativeZeroThresholdCondition(condition, ast);
    if (threshold === void 0) return void 0;
    const selector = {
      kind: "call",
      callee: NEGATIVE_ZERO_DEGREE_SELECTOR_GE,
      args: [threshold.value, numberExpression(threshold.bound)]
    };
    return threshold.truth === "ge" ? selector : oneMinus(selector);
  }
  function matchNegativeZeroThresholdCondition(condition, ast) {
    if (condition.left.kind === "number") {
      const flipped = flipNumericLeftCondition(condition);
      return flipped === void 0 ? void 0 : matchNegativeZeroThresholdCondition(flipped, ast);
    }
    const value = condition.left;
    const bound = numericLiteralValue(condition.right);
    if (bound === void 0 || !Number.isFinite(bound) || bound <= 0 || bound > 1e12) return void 0;
    if (!isKnownIntegerValuedExpression(value, ast)) return void 0;
    const range2 = numericRangeForExpression(value, ast);
    if (range2 === void 0 || range2.min === void 0 || range2.min < 0) return void 0;
    if (range2.max !== void 0 && range2.max / bound >= 1e60) return void 0;
    switch (condition.op) {
      case ">=":
        return { value, bound, truth: "ge" };
      case "<":
        return { value, bound, truth: "lt" };
      case ">":
        return Number.isSafeInteger(bound + 1) ? { value, bound: bound + 1, truth: "ge" } : void 0;
      case "<=":
        return Number.isSafeInteger(bound + 1) ? { value, bound: bound + 1, truth: "lt" } : void 0;
      case "==":
      case "!=":
        return void 0;
    }
  }
  function buildArithmeticIfSelect(statement, ast, options = {}) {
    if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) {
      return void 0;
    }
    const thenAssign = statement.thenBody[0];
    const elseAssign = statement.elseBody[0];
    if (thenAssign?.kind !== "assign" || elseAssign?.kind !== "assign") return void 0;
    if (thenAssign.target !== elseAssign.target) return void 0;
    const booleanSelector = booleanSelectorExpression(statement.condition, ast);
    const negativeZeroSelector = options.negativeZeroDegree ? negativeZeroThresholdSelectorExpression(statement.condition, ast) : void 0;
    const selector = booleanSelector ?? negativeZeroSelector;
    if (!selector) return void 0;
    const usesNegativeZero = booleanSelector === void 0 && negativeZeroSelector !== void 0;
    const expr = addExpressions(
      multiplyExpressions(thenAssign.expr, selector),
      multiplyExpressions(elseAssign.expr, oneMinus(selector))
    );
    return {
      kind: "assign",
      target: thenAssign.target,
      expr,
      name: usesNegativeZero ? "negative-zero-threshold-select" : "arithmetic-if-select",
      detail: usesNegativeZero ? "Replaced threshold if/else assignment with negative-zero selection" : "Replaced boolean if/else with arithmetic selection"
    };
  }
  function buildComparisonBooleanCandidate(statement) {
    if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) return void 0;
    const thenAssign = statement.thenBody[0];
    const elseAssign = statement.elseBody[0];
    if (thenAssign?.kind !== "assign" || elseAssign?.kind !== "assign") return void 0;
    if (thenAssign.target !== elseAssign.target) return void 0;
    const thenValue = numericLiteralValue(thenAssign.expr);
    const elseValue = numericLiteralValue(elseAssign.expr);
    if (!(thenValue === 1 && elseValue === 0 || thenValue === 0 && elseValue === 1)) return void 0;
    const truth = comparisonMask(statement.condition);
    if (!truth) return void 0;
    return {
      kind: "assign",
      target: thenAssign.target,
      expr: thenValue === 1 ? truth : oneMinus(truth),
      name: "arithmetic-if-comparison-mask",
      detail: "Replaced comparison-to-boolean branch with arithmetic mask"
    };
  }
  function buildBooleanAlgebraCandidate(statement, ast) {
    if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) return void 0;
    const thenAssign = statement.thenBody[0];
    const elseAssign = statement.elseBody[0];
    if (thenAssign?.kind !== "assign" || elseAssign?.kind !== "assign") return void 0;
    if (thenAssign.target !== elseAssign.target) return void 0;
    const selector = booleanSelectorExpression(statement.condition, ast);
    const selectorName = booleanSelectorVariableName(statement.condition, ast);
    if (!selector || !selectorName) return void 0;
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
    if (otherThen && otherElse && expressionEquals(otherThen, otherElse)) return void 0;
    if (otherThen && otherElse && expressionEquals(thenAssign.expr, oneMinus(otherElse))) {
      return booleanAlgebraCandidate(thenAssign.target, absExpression(subtractExpressions(selector, otherElse)), "xor");
    }
    return void 0;
  }
  function booleanAlgebraCandidate(target, expr, operation) {
    return {
      kind: "assign",
      target,
      expr,
      name: "arithmetic-if-boolean-algebra",
      detail: `Replaced boolean ${operation.toUpperCase()} branch with arithmetic expression`
    };
  }
  function buildBooleanUpdateCandidate(statement, ast) {
    if (statement.elseBody || statement.thenBody.length !== 1) return void 0;
    const assign = statement.thenBody[0];
    if (assign?.kind !== "assign") return void 0;
    const selector = booleanSelectorExpression(statement.condition, ast);
    if (!selector) return void 0;
    const current = { kind: "identifier", name: assign.target };
    const plus = matchTargetPlusDelta(assign.expr, assign.target);
    if (plus) {
      return {
        kind: "assign",
        target: assign.target,
        expr: addExpressions(current, multiplyExpressions(plus, selector)),
        name: "arithmetic-if-update",
        detail: "Replaced conditional addition with boolean-masked arithmetic"
      };
    }
    const minus = matchTargetMinusDelta(assign.expr, assign.target);
    if (minus) {
      return {
        kind: "assign",
        target: assign.target,
        expr: subtractExpressions(current, multiplyExpressions(minus, selector)),
        name: "arithmetic-if-update",
        detail: "Replaced conditional subtraction with boolean-masked arithmetic"
      };
    }
    if (expressionEquals(assign.expr, negateExpression(current))) {
      return {
        kind: "assign",
        target: assign.target,
        expr: signToggleExpression(current, selector),
        name: "arithmetic-if-sign-toggle",
        detail: "Replaced conditional sign toggle with boolean-masked multiplier"
      };
    }
    return buildConditionalMoveCandidate(assign, selector);
  }
  function buildBooleanSignToggleCandidate(statement, ast) {
    if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) return void 0;
    const thenAssign = statement.thenBody[0];
    const elseAssign = statement.elseBody[0];
    if (thenAssign?.kind !== "assign" || elseAssign?.kind !== "assign") return void 0;
    if (thenAssign.target !== elseAssign.target || !isIdentityAssignment(elseAssign)) return void 0;
    const selector = booleanSelectorExpression(statement.condition, ast);
    if (!selector) return void 0;
    const current = { kind: "identifier", name: thenAssign.target };
    if (!expressionEquals(thenAssign.expr, negateExpression(current))) return void 0;
    return {
      kind: "assign",
      target: thenAssign.target,
      expr: signToggleExpression(current, selector),
      name: "arithmetic-if-sign-toggle",
      detail: "Replaced conditional sign toggle with boolean-masked multiplier"
    };
  }
  function buildConditionalMoveCandidate(assign, selector) {
    const current = { kind: "identifier", name: assign.target };
    if (expressionEquals(assign.expr, current)) return void 0;
    return {
      kind: "assign",
      target: assign.target,
      expr: addExpressions(
        multiplyExpressions(current, oneMinus(selector)),
        multiplyExpressions(assign.expr, selector)
      ),
      name: "arithmetic-if-conditional-move",
      detail: "Replaced conditional assignment with boolean-masked conditional move"
    };
  }
  function buildSaturatingUpdateCandidate(statement, ast) {
    if (statement.elseBody || statement.thenBody.length !== 1) return void 0;
    const assign = statement.thenBody[0];
    if (assign?.kind !== "assign") return void 0;
    const range2 = integerRangeFor(assign.target, ast);
    if (!range2) return void 0;
    const targetExpr = { kind: "identifier", name: assign.target };
    const { left, right, op } = statement.condition;
    if (!expressionEquals(left, targetExpr)) return void 0;
    const decrement = matchTargetMinusDelta(assign.expr, assign.target);
    if (decrement && op === ">" && isNumericValue(decrement, 1) && range2.min !== void 0 && isNumericValue(right, range2.min)) {
      return maxCandidate(assign.target, assign.expr, right, "Replaced saturating decrement branch with max()");
    }
    const increment = matchTargetPlusDelta(assign.expr, assign.target);
    if (increment && op === "<" && isNumericValue(increment, 1) && range2.max !== void 0 && isNumericValue(right, range2.max)) {
      return minCandidate(assign.target, assign.expr, right, "Replaced saturating increment branch with min-via-max()");
    }
    return void 0;
  }
  function buildMaxMinCandidate(statement) {
    if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) return void 0;
    const thenAssign = statement.thenBody[0];
    const elseAssign = statement.elseBody[0];
    if (thenAssign?.kind !== "assign" || elseAssign?.kind !== "assign") return void 0;
    if (thenAssign.target !== elseAssign.target) return void 0;
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
    return void 0;
  }
  function buildClampCandidate(statement) {
    const assign = singleEffectiveAssign(statement);
    if (!assign) return void 0;
    const targetExpr = { kind: "identifier", name: assign.target };
    const { left, right, op } = statement.condition;
    if (!expressionEquals(left, targetExpr)) return void 0;
    if ((op === "<" || op === "<=") && expressionEquals(assign.expr, right)) {
      return maxCandidate(assign.target, targetExpr, right, "Replaced lower clamp branch with max()");
    }
    if ((op === ">" || op === ">=") && expressionEquals(assign.expr, right)) {
      return minCandidate(assign.target, targetExpr, right, "Replaced upper clamp branch with min-via-max()");
    }
    return void 0;
  }
  function buildAbsCandidate(statement) {
    if (statement.thenBody.length !== 1) return void 0;
    const thenAssign = statement.thenBody[0];
    if (thenAssign?.kind !== "assign") return void 0;
    const { left, right, op } = statement.condition;
    if (!isNumericValue(right, 0)) return void 0;
    const negativeLeft = negateExpression(left);
    if (!statement.elseBody) {
      const targetExpr = { kind: "identifier", name: thenAssign.target };
      if (!expressionEquals(targetExpr, left)) return void 0;
      if ((op === "<" || op === "<=") && expressionEquals(thenAssign.expr, negativeLeft)) {
        return absCandidate(thenAssign.target, left);
      }
      return void 0;
    }
    if (statement.elseBody.length !== 1) return void 0;
    const elseAssign = statement.elseBody[0];
    if (elseAssign?.kind !== "assign" || thenAssign.target !== elseAssign.target) return void 0;
    if ((op === "<" || op === "<=") && expressionEquals(thenAssign.expr, negativeLeft) && expressionEquals(elseAssign.expr, left)) {
      return absCandidate(thenAssign.target, left);
    }
    if ((op === ">" || op === ">=") && expressionEquals(thenAssign.expr, left) && expressionEquals(elseAssign.expr, negativeLeft)) {
      return absCandidate(thenAssign.target, left);
    }
    return void 0;
  }
  function absCandidate(target, expr) {
    return {
      kind: "assign",
      target,
      expr: { kind: "call", callee: "abs", args: [expr] },
      name: "arithmetic-if-abs",
      detail: "Replaced sign branch with abs()"
    };
  }
  function maxCandidate(target, left, right, detail = "Replaced max branch with \u041A max") {
    return {
      kind: "assign",
      target,
      expr: { kind: "call", callee: "max", args: [left, right] },
      name: "arithmetic-if-max",
      detail
    };
  }
  function minCandidate(target, left, right, detail = "Replaced min branch with min-via-max()") {
    return {
      kind: "assign",
      target,
      expr: negateExpression({
        kind: "call",
        callee: "max",
        args: [negateExpression(left), negateExpression(right)]
      }),
      name: "arithmetic-if-min",
      detail
    };
  }
  function booleanSelectorExpression(condition, ast) {
    const leftIdentifier = condition.left.kind === "identifier" ? condition.left.name : void 0;
    const rightIdentifier = condition.right.kind === "identifier" ? condition.right.name : void 0;
    const leftNumber = numericLiteralValue(condition.left);
    const rightNumber = numericLiteralValue(condition.right);
    let variable;
    let value;
    if (leftIdentifier !== void 0 && rightNumber !== void 0) {
      variable = leftIdentifier;
      value = rightNumber;
    } else if (rightIdentifier !== void 0 && leftNumber !== void 0) {
      variable = rightIdentifier;
      value = leftNumber;
    }
    if (variable === void 0 || value === void 0) return void 0;
    if (!isBooleanVariable(variable, ast)) return void 0;
    const variableExpr = { kind: "identifier", name: variable };
    if (condition.op === "==" && value === 1 || condition.op === "!=" && value === 0) {
      return variableExpr;
    }
    if (condition.op === "==" && value === 0 || condition.op === "!=" && value === 1) {
      return oneMinus(variableExpr);
    }
    return void 0;
  }
  function isBooleanVariable(name, ast) {
    for (const state of ast.states) {
      const field = state.fields.find((candidate) => candidate.name === name);
      if (!field) continue;
      if (field.type === "flag") return true;
      if (field.min === 0 && field.max === 1) return true;
    }
    return false;
  }
  function integerRangeFor(name, ast) {
    const range2 = numericRangeFor(name, ast);
    if (range2 === void 0) return void 0;
    if (!Number.isInteger(range2.min) || !Number.isInteger(range2.max)) return void 0;
    return range2;
  }
  function numericRangeFor(name, ast) {
    for (const state of ast.states) {
      const field = state.fields.find((candidate) => candidate.name === name);
      if (!field) continue;
      if (field.type === "flag") return { min: 0, max: 1 };
      if (field.type === "range") {
        const range2 = {};
        if (field.min !== void 0) range2.min = field.min;
        if (field.max !== void 0) range2.max = field.max;
        return range2;
      }
    }
    return void 0;
  }
  function booleanSelectorVariableName(condition, ast) {
    const leftIdentifier = condition.left.kind === "identifier" ? condition.left.name : void 0;
    const rightIdentifier = condition.right.kind === "identifier" ? condition.right.name : void 0;
    const leftNumber = numericLiteralValue(condition.left);
    const rightNumber = numericLiteralValue(condition.right);
    const name = leftIdentifier !== void 0 && rightNumber !== void 0 ? leftIdentifier : rightIdentifier !== void 0 && leftNumber !== void 0 ? rightIdentifier : void 0;
    return name !== void 0 && isBooleanVariable(name, ast) ? name : void 0;
  }
  function booleanIdentifier(expr, ast) {
    return expr.kind === "identifier" && isBooleanVariable(expr.name, ast) ? expr : void 0;
  }
  function comparisonMask(condition) {
    if (condition.op !== "==" && condition.op !== "!=") return void 0;
    const notEqual = signExpression(absExpression(subtractExpressions(condition.left, condition.right)));
    return condition.op === "==" ? oneMinus(notEqual) : notEqual;
  }
  function isTicTacToeMacroName(name) {
    return ticTacToeMacroArity(name) !== void 0;
  }
  function ticTacToeMacroArity(name) {
    const arities = {
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
      packed4_score: 2
    };
    return arities[name];
  }
  function ticTacToeExpressionMacro(name, args) {
    switch (name) {
      case "norm4":
      case "grid4_norm":
        return norm4Expression(args[0]);
      case "bit_mask":
        return bitMaskExpression(args[0]);
      case "bit_has":
        return bitAndExpression(args[0], bitMaskExpression(args[1]));
      case "bit_set":
        return bitOrExpression(args[0], bitMaskExpression(args[1]));
      case "bit_clear":
        return bitAndExpression(args[0], bitNotExpression(bitMaskExpression(args[1])));
      case "bit_toggle":
        return bitXorExpression(args[0], bitMaskExpression(args[1]));
      case "diag_left_index":
        return norm4Expression(addExpressions(args[0], args[1]));
      case "diag_right_index":
        return norm4Expression(subtractExpressions(args[0], args[1]));
      case "cell_mask":
        return cellMaskExpression(args[0], args[1]);
      case "cell_has":
      case "cell_used":
        return bitAndExpression(args[0], cellMaskExpression(args[1], args[2]));
      case "cell_set":
      case "cell_mark":
        return bitOrExpression(args[0], cellMaskExpression(args[1], args[2]));
      case "cell_clear":
        return bitAndExpression(args[0], bitNotExpression(cellMaskExpression(args[1], args[2])));
      case "cell_toggle":
        return bitXorExpression(args[0], cellMaskExpression(args[1], args[2]));
      case "digit_at":
        return packed4DigitExpression(args[0], args[1]);
      case "digit_add":
      case "packed4_add":
        return addExpressions(
          args[0],
          multiplyExpressions(args[2], digitPlaceExpression(args[1]))
        );
      case "digit_set":
        return digitSetExpression(args[0], args[1], args[2]);
      case "packed4_digit":
        return packed4DigitExpression(args[0], args[1]);
      case "packed4_score":
        return {
          kind: "call",
          callee: "sqr",
          args: [subtractExpressions(packed4DigitExpression(args[0], args[1]), numberExpression(0.41200076))]
        };
      default:
        return void 0;
    }
  }
  function matchCellHelperCall(expr, names) {
    if (expr.kind !== "call" || !names.includes(expr.callee.toLowerCase()) || expr.args.length !== 3) return void 0;
    return {
      mask: expr.args[0],
      x: expr.args[1],
      y: expr.args[2]
    };
  }
  function matchBitSetAssignment(statement) {
    const expr = statement.expr;
    if (expr.kind !== "call" || expr.callee.toLowerCase() !== "bit_set" || expr.args.length !== 2) return void 0;
    const collection = expr.args[0];
    if (collection.kind !== "identifier" || statement.target !== collection.name) return void 0;
    return {
      collection,
      item: expr.args[1]
    };
  }
  function isReusableBitSetPair(first, second) {
    const firstSet = matchBitSetAssignment(first);
    const secondSet = matchBitSetAssignment(second);
    return firstSet !== void 0 && secondSet !== void 0 && expressionEquals(firstSet.item, secondSet.item);
  }
  function bitMaskScratchName(statement) {
    return `${BIT_MASK_SCRATCH_PREFIX}${statement.line}`;
  }
  function matchBitMembershipCondition(condition) {
    if (condition.op !== "!=" || !isZeroExpression(condition.right)) return void 0;
    const test = condition.left;
    if (test.kind !== "call" || test.callee.toLowerCase() !== "bit_has" || test.args.length !== 2) return void 0;
    return {
      collection: test.args[0],
      item: test.args[1],
      test
    };
  }
  function isBitClearAssignment(statement, membership) {
    if (membership.collection.kind !== "identifier" || statement.target !== membership.collection.name) return false;
    const expr = statement.expr;
    return expr.kind === "call" && expr.callee.toLowerCase() === "bit_clear" && expr.args.length === 2 && expressionEquals(expr.args[0], membership.collection) && expressionEquals(expr.args[1], membership.item);
  }
  function norm4Expression(expr) {
    const rem = multiplyExpressions(
      { kind: "call", callee: "frac", args: [divideExpressions({ kind: "call", callee: "int", args: [expr] }, numberExpression(4))] },
      numberExpression(4)
    );
    return addExpressions(
      rem,
      multiplyExpressions(numberExpression(4), oneMinus(signExpression(maxExpression(rem, numberExpression(0)))))
    );
  }
  function cellMaskExpression(x, y) {
    return addExpressions(
      pow10Expression(x),
      { kind: "call", callee: "int", args: [multiplyExpressions(pow10Expression(y), numberExpression(0.22600029))] }
    );
  }
  function bitMaskExpression(index) {
    const nibble = intExpression(divideExpressions(index, numberExpression(4)));
    const offset = subtractExpressions(index, multiplyExpressions(nibble, numberExpression(4)));
    return multiplyExpressions(
      powExpression(numberExpression(2), offset),
      pow10Expression(nibble)
    );
  }
  function packed4DigitExpression(lines, index) {
    return {
      kind: "call",
      callee: "int",
      args: [
        multiplyExpressions(
          { kind: "call", callee: "frac", args: [divideExpressions(lines, pow10Expression(index))] },
          numberExpression(10)
        )
      ]
    };
  }
  function digitSetExpression(value, index, digit) {
    const place = digitPlaceExpression(index);
    return addExpressions(
      subtractExpressions(value, multiplyExpressions(packed4DigitExpression(value, index), place)),
      multiplyExpressions(digit, place)
    );
  }
  function digitPlaceExpression(index) {
    return pow10Expression(subtractExpressions(index, numberExpression(1)));
  }
  function oneMinus(expr) {
    if (isNumericValue(expr, 0)) return numberExpression(1);
    if (isNumericValue(expr, 1)) return numberExpression(0);
    return {
      kind: "binary",
      op: "-",
      left: numberExpression(1),
      right: expr
    };
  }
  function multiplyExpressions(left, right) {
    if (isNumericValue(left, 0) || isNumericValue(right, 0)) return numberExpression(0);
    if (isNumericValue(left, 1)) return right;
    if (isNumericValue(right, 1)) return left;
    return { kind: "binary", op: "*", left, right };
  }
  function addExpressions(left, right) {
    if (isNumericValue(left, 0)) return right;
    if (isNumericValue(right, 0)) return left;
    return { kind: "binary", op: "+", left, right };
  }
  function subtractExpressions(left, right) {
    if (isNumericValue(right, 0)) return left;
    return { kind: "binary", op: "-", left, right };
  }
  function divideExpressions(left, right) {
    if (isNumericValue(right, 1)) return left;
    return { kind: "binary", op: "/", left, right };
  }
  function pow10Expression(expr) {
    return { kind: "call", callee: "pow10", args: [expr] };
  }
  function powExpression(base, exponent) {
    return { kind: "call", callee: "pow", args: [base, exponent] };
  }
  function maxExpression(left, right) {
    return { kind: "call", callee: "max", args: [left, right] };
  }
  function minExpression(left, right) {
    return negateExpression(maxExpression(negateExpression(left), negateExpression(right)));
  }
  function absExpression(expr) {
    return { kind: "call", callee: "abs", args: [expr] };
  }
  function intExpression(expr) {
    return { kind: "call", callee: "int", args: [expr] };
  }
  function signExpression(expr) {
    return { kind: "call", callee: "sign", args: [expr] };
  }
  function bitAndExpression(left, right) {
    return { kind: "call", callee: "bit_and", args: [left, right] };
  }
  function bitOrExpression(left, right) {
    return { kind: "call", callee: "bit_or", args: [left, right] };
  }
  function bitXorExpression(left, right) {
    return { kind: "call", callee: "bit_xor", args: [left, right] };
  }
  function bitNotExpression(expr) {
    return { kind: "call", callee: "bit_not", args: [expr] };
  }
  function spatialCountExpression(name, args, ast) {
    const [mask, cell] = args;
    if (mask === void 0 || cell === void 0) return void 0;
    if (name === "neighbor_count") {
      const board2 = boardForCellMask(mask, ast);
      const offsets2 = board2?.height === 1 ? [-1, 1] : board2?.width === 1 ? [-10, 10] : [-11, -10, -9, -1, 1, 9, 10, 11];
      return sumExpressions(offsets2.map((offset) => spatialHitExpression(mask, offsetExpressionAst(cell, offset))));
    }
    const board = boardForCellMask(mask, ast);
    if (board !== void 0 && board.width <= 4 && board.height <= 4) {
      return maxExpressions([
        sumExpressions(spatialLineCells(board, "row", cell).map((index) => spatialHitExpression(mask, index))),
        sumExpressions(spatialLineCells(board, "column", cell).map((index) => spatialHitExpression(mask, index))),
        sumExpressions(spatialLineCells(board, "diag-left", cell).map((index) => spatialHitExpression(mask, index))),
        sumExpressions(spatialLineCells(board, "diag-right", cell).map((index) => spatialHitExpression(mask, index)))
      ]);
    }
    const offsets = [-99, -90, -81, -72, -63, -54, -45, -36, -27, -18, -9, 0, 9, 18, 27, 36, 45, 54, 63, 72, 81, 90, 99];
    return sumExpressions([
      spatialHitExpression(mask, cell),
      ...offsets.filter((offset) => offset !== 0).map((offset) => spatialHitExpression(mask, offsetExpressionAst(cell, offset)))
    ]);
  }
  function spatialHitExpression(mask, index) {
    return { kind: "call", callee: "__spatial_hit", args: [mask, index] };
  }
  function boardForCellMask(mask, ast) {
    if (mask.kind !== "identifier" || ast.v2 === void 0) return void 0;
    const domain = ast.v2.state.find((field) => field.name === mask.name)?.domain;
    if (domain === void 0) return void 0;
    return ast.v2.boards.find((board) => board.name === domain);
  }
  function spatialLineCells(board, kind, cell) {
    const x = decimalOnesExpressionAst(cell);
    const y = decimalTensExpressionAst(cell);
    switch (kind) {
      case "row":
        return range(board.xMin, board.xMax).map((candidateX) => boardCellExpressionAst(numberExpression(candidateX), y));
      case "column":
        return range(board.yMin, board.yMax).map((candidateY) => boardCellExpressionAst(x, numberExpression(candidateY)));
      case "diag-left":
        return range(-Math.max(board.width, board.height) + 1, Math.max(board.width, board.height) - 1).map((delta) => offsetExpressionAst(cell, delta * 11));
      case "diag-right":
        return range(-Math.max(board.width, board.height) + 1, Math.max(board.width, board.height) - 1).map((delta) => offsetExpressionAst(cell, delta * 9));
    }
  }
  function spatialLineProgressions(board, cell) {
    const x = decimalOnesExpressionAst(cell);
    const y = decimalTensExpressionAst(cell);
    const span = Math.max(board.width, board.height);
    return [
      {
        startOffset: subtractExpressions(boardCellExpressionAst(numberExpression(board.xMin), y), cell),
        step: numberExpression(1),
        count: board.width
      },
      {
        startOffset: subtractExpressions(boardCellExpressionAst(x, numberExpression(board.yMin)), cell),
        step: numberExpression(10),
        count: board.height
      },
      {
        startOffset: numberExpression(-(span - 1) * 11),
        step: numberExpression(11),
        count: span * 2 - 1
      },
      {
        startOffset: numberExpression(-(span - 1) * 9),
        step: numberExpression(9),
        count: span * 2 - 1
      }
    ];
  }
  function spatialNeighborProgressions(board) {
    if (board?.height === 1) {
      return [{ startOffset: numberExpression(-1), step: numberExpression(2), count: 2 }];
    }
    if (board?.width === 1) {
      return [{ startOffset: numberExpression(-10), step: numberExpression(20), count: 2 }];
    }
    return [
      { startOffset: numberExpression(-11), step: numberExpression(1), count: 3 },
      { startOffset: numberExpression(-1), step: numberExpression(2), count: 2 },
      { startOffset: numberExpression(9), step: numberExpression(1), count: 3 }
    ];
  }
  function lineCountGroupKeyFor(board, cell) {
    return `${board.name}:${board.xMin}:${board.xMax}:${board.yMin}:${board.yMax}:${expressionToIntentText(cell)}`;
  }
  function boardCellExpressionAst(x, y) {
    return addExpressions(x, multiplyExpressions(numberExpression(10), y));
  }
  function decimalTensExpressionAst(expr) {
    return intExpression(divideExpressions(expr, numberExpression(10)));
  }
  function decimalOnesExpressionAst(expr) {
    return subtractExpressions(expr, multiplyExpressions(numberExpression(10), intExpression(divideExpressions(expr, numberExpression(10)))));
  }
  function offsetExpressionAst(expr, offset) {
    if (offset === 0) return expr;
    return offset > 0 ? addExpressions(expr, numberExpression(offset)) : subtractExpressions(expr, numberExpression(Math.abs(offset)));
  }
  function sumExpressions(expressions) {
    return expressions.reduce((sum, expr) => addExpressions(sum, expr), numberExpression(0));
  }
  function maxExpressions(expressions) {
    return expressions.reduce((best, expr) => maxExpression(best, expr));
  }
  function spatialHitScratchName(mask) {
    return `${SPATIAL_HIT_SCRATCH_PREFIX}${mask}`;
  }
  function spatialCountScratchNames() {
    return [
      `${SPATIAL_COUNT_SCRATCH_PREFIX}total`,
      `${SPATIAL_COUNT_SCRATCH_PREFIX}line`,
      `${SPATIAL_COUNT_SCRATCH_PREFIX}offset`,
      spatialCountCounterScratchName()
    ];
  }
  function spatialCountCounterScratchName() {
    return `${SPATIAL_COUNT_SCRATCH_PREFIX}counter`;
  }
  function spatialCountMaskScratchName() {
    return `${SPATIAL_COUNT_SCRATCH_PREFIX}mask`;
  }
  function range(start, end) {
    const values = [];
    for (let value = start; value <= end; value += 1) values.push(value);
    return values;
  }
  function signToggleExpression(current, selector) {
    return multiplyExpressions(
      current,
      subtractExpressions(numberExpression(1), multiplyExpressions(numberExpression(2), selector))
    );
  }
  function negateExpression(expr) {
    if (expr.kind === "unary") return expr.expr;
    const value = numericLiteralValue(expr);
    if (value !== void 0) return numberExpression(-value);
    return { kind: "unary", op: "-", expr };
  }
  function matchRemainderByConstant(expr) {
    if (expr.op !== "-") return void 0;
    if (!expressionPureForSubstitution(expr.left)) return void 0;
    const product = expr.right;
    if (product.kind !== "binary" || product.op !== "*") return void 0;
    const leftIntDivide = matchIntDivideByConstant(product.left);
    if (leftIntDivide !== void 0 && expressionEquals(leftIntDivide.value, expr.left) && expressionEquals(leftIntDivide.divisor, product.right)) {
      return leftIntDivide;
    }
    const rightIntDivide = matchIntDivideByConstant(product.right);
    if (rightIntDivide !== void 0 && expressionEquals(rightIntDivide.value, expr.left) && expressionEquals(rightIntDivide.divisor, product.left)) {
      return rightIntDivide;
    }
    return void 0;
  }
  function matchIntDivideByConstant(expr) {
    if (expr.kind !== "call" || expr.callee.toLowerCase() !== "int" || expr.args.length !== 1) return void 0;
    const divided = expr.args[0];
    if (divided.kind !== "binary" || divided.op !== "/") return void 0;
    if (numericLiteralValue(divided.right) === void 0) return void 0;
    return {
      value: divided.left,
      divisor: divided.right
    };
  }
  function numberExpression(value) {
    return { kind: "number", raw: String(value) };
  }
  function matchTargetPlusDelta(expr, target) {
    if (expr.kind !== "binary" || expr.op !== "+") return void 0;
    const targetExpr = { kind: "identifier", name: target };
    if (expressionEquals(expr.left, targetExpr)) return expr.right;
    if (expressionEquals(expr.right, targetExpr)) return expr.left;
    return void 0;
  }
  function matchTargetMinusDelta(expr, target) {
    if (expr.kind !== "binary" || expr.op !== "-") return void 0;
    const targetExpr = { kind: "identifier", name: target };
    if (expressionEquals(expr.left, targetExpr)) return expr.right;
    return void 0;
  }
  function singleEffectiveAssign(statement) {
    if (statement.thenBody.length !== 1) return void 0;
    const thenAssign = statement.thenBody[0];
    if (thenAssign?.kind !== "assign") return void 0;
    if (!statement.elseBody) return thenAssign;
    if (statement.elseBody.length !== 1) return void 0;
    const elseAssign = statement.elseBody[0];
    if (elseAssign?.kind !== "assign") return void 0;
    if (thenAssign.target !== elseAssign.target) return void 0;
    if (isIdentityAssignment(elseAssign)) return thenAssign;
    return void 0;
  }
  function isIdentityAssignment(statement) {
    return expressionEquals(statement.expr, { kind: "identifier", name: statement.target });
  }
  function expressionEquals(left, right) {
    if (left.kind !== right.kind) return false;
    switch (left.kind) {
      case "number":
        return right.kind === "number" && left.raw === right.raw;
      case "identifier":
        return right.kind === "identifier" && left.name === right.name;
      case "unary":
        return right.kind === "unary" && left.op === right.op && expressionEquals(left.expr, right.expr);
      case "binary":
        return right.kind === "binary" && left.op === right.op && expressionEquals(left.left, right.left) && expressionEquals(left.right, right.right);
      case "call":
        return right.kind === "call" && left.callee.toLowerCase() === right.callee.toLowerCase() && left.args.length === right.args.length && left.args.every((arg, index) => expressionEquals(arg, right.args[index]));
    }
  }
  function optimizeDispatchDefaultCases(statement) {
    if (!statement.defaultBody || statement.cases.length === 0) return { statement, removed: 0 };
    const defaultBody = statement.defaultBody;
    const kept = [];
    let removed = 0;
    for (let index = 0; index < statement.cases.length; index += 1) {
      const dispatchCase = statement.cases[index];
      const value = numericLiteralValue(dispatchCase.value);
      const laterSameValue = value !== void 0 && statement.cases.slice(index + 1).some((laterCase) => numericLiteralValue(laterCase.value) === value);
      if (value !== void 0 && !laterSameValue && statementListsEqual(dispatchCase.body, defaultBody)) {
        removed += 1;
        continue;
      }
      kept.push(dispatchCase);
    }
    if (removed === 0) return { statement, removed };
    return { statement: { ...statement, cases: kept }, removed };
  }
  function statementListsEqual(left, right) {
    return left.length === right.length && left.every((statement, index) => statementEquals(statement, right[index]));
  }
  function statementEquals(left, right) {
    if (left.kind !== right.kind) return false;
    switch (left.kind) {
      case "pause":
      case "halt":
        return expressionEquals(left.expr, right.expr);
      case "ask":
        return left.target === right.target && expressionOptionEquals(left.prompt, right.prompt);
      case "input":
        return left.target === right.target;
      case "assign":
        return left.target === right.target && expressionEquals(left.expr, right.expr);
      case "loop":
        return statementListsEqual(left.body, right.body);
      case "if":
        return conditionEquals(left.condition, right.condition) && statementListsEqual(left.thenBody, right.thenBody) && statementListOptionEquals(left.elseBody, right.elseBody);
      case "switch":
        return expressionEquals(left.expr, right.expr) && left.cases.length === right.cases.length && left.cases.every(
          (switchCase, index) => expressionEquals(switchCase.value, right.cases[index].value) && statementListsEqual(switchCase.body, right.cases[index].body)
        ) && statementListOptionEquals(left.defaultBody, right.defaultBody);
      case "dispatch":
        return expressionEquals(left.expr, right.expr) && left.cases.length === right.cases.length && left.cases.every(
          (dispatchCase, index) => expressionEquals(dispatchCase.value, right.cases[index].value) && statementListsEqual(dispatchCase.body, right.cases[index].body)
        ) && statementListOptionEquals(left.defaultBody, right.defaultBody);
      case "show":
        return left.display === right.display;
      case "call":
        return left.block === right.block;
      case "core":
        return rawLinesEqual(left.lines, right.lines) && JSON.stringify(left.inputs ?? []) === JSON.stringify(right.inputs ?? []) && JSON.stringify(left.outputs ?? []) === JSON.stringify(right.outputs ?? []) && JSON.stringify(left.clobbers ?? []) === JSON.stringify(right.clobbers ?? []) && JSON.stringify(left.preserves ?? []) === JSON.stringify(right.preserves ?? []) && left.strict === right.strict;
      case "egg":
        return rawLinesEqual(left.lines, right.lines);
      case "trap":
        return left.trap === right.trap && expressionEquals(left.expr, right.expr);
    }
  }
  function expressionOptionEquals(left, right) {
    if (left === void 0 || right === void 0) return left === right;
    return expressionEquals(left, right);
  }
  function statementListOptionEquals(left, right) {
    if (left === void 0 || right === void 0) return left === right;
    return statementListsEqual(left, right);
  }
  function rawLinesEqual(left, right) {
    return left.length === right.length && left.every((line, index) => line.text === right[index].text);
  }
  function countIdentifierReadsInCondition(condition, name) {
    return countIdentifierReads(condition.left, name) + countIdentifierReads(condition.right, name);
  }
  function countIdentifierReads(expr, name) {
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
  function substituteConditionIdentifier(condition, name, replacement) {
    return {
      left: substituteExpressionIdentifier(condition.left, name, replacement),
      op: condition.op,
      right: substituteExpressionIdentifier(condition.right, name, replacement)
    };
  }
  function substituteExpressionIdentifier(expr, name, replacement) {
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
          right: substituteExpressionIdentifier(expr.right, name, replacement)
        };
      case "call":
        return { ...expr, args: expr.args.map((arg) => substituteExpressionIdentifier(arg, name, replacement)) };
    }
  }
  function expressionPureForSubstitution(expr) {
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
  function isNumericValue(expr, value) {
    const parsed = numericLiteralValue(expr);
    return parsed !== void 0 && parsed === value;
  }
  function numericLiteralValue(expr) {
    if (expr.kind === "unary" && expr.op === "-") {
      const value2 = numericLiteralValue(expr.expr);
      return value2 === void 0 ? void 0 : -value2;
    }
    if (expr.kind !== "number") return void 0;
    const value = Number(expr.raw);
    return Number.isFinite(value) ? value : void 0;
  }
  function estimateOrdinaryIfCost(statement, ast) {
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
  function estimateSimpleStatementCost(statement, ast) {
    switch (statement.kind) {
      case "assign":
        return estimateExpressionCost(statement.expr) + 1;
      case "pause":
      case "halt":
        return estimateExpressionCost(statement.expr) + 1;
      case "call": {
        const terminal = effectiveTerminalStatement(statement, ast);
        return terminal === void 0 ? Number.POSITIVE_INFINITY : estimateSimpleStatementCost(terminal, ast);
      }
      default:
        return Number.POSITIVE_INFINITY;
    }
  }
  function estimateConditionCost(condition, ast) {
    return conditionCompileCost(selectCheaperEquivalentCondition(condition, ast).condition);
  }
  function estimateExpressionCostForCondition(expr, preloadedConstants) {
    if (preloadedConstants === void 0) return estimateExpressionCost(expr);
    if (expr.kind === "number" && preloadedConstants.has(normalizeConstantLiteral(expr.raw))) return 1;
    if (expr.kind === "unary" && expr.op === "-" && expr.expr.kind === "number" && preloadedConstants.has(normalizeConstantLiteral(negatedNumberLiteral(expr.expr.raw)))) {
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
        if (remainder !== void 0) {
          return estimateExpressionCostForCondition(remainder.value, preloadedConstants) + estimateExpressionCostForCondition(remainder.divisor, preloadedConstants) * 2 + 3;
        }
        return estimateExpressionCostForCondition(expr.left, preloadedConstants) + estimateExpressionCostForCondition(expr.right, preloadedConstants) + 1;
      }
      case "call":
        return estimateCallCostForCondition(expr, preloadedConstants);
    }
  }
  function estimateCallCostForCondition(expr, preloadedConstants) {
    const name = expr.callee.toLowerCase();
    if (name === NEGATIVE_ZERO_DEGREE_SELECTOR_GE) {
      return estimateNegativeZeroDegreeSelectorCost(expr, preloadedConstants);
    }
    const macro = ticTacToeExpressionMacro(name, expr.args);
    if (macro !== void 0) return estimateExpressionCostForCondition(macro, preloadedConstants);
    if (name === "random" || name === "pi") return 1;
    if (name === "pow" || ["max", "bit_and", "bit_or", "bit_xor"].includes(name)) {
      return (expr.args[0] ? estimateExpressionCostForCondition(expr.args[0], preloadedConstants) : 0) + (expr.args[1] ? estimateExpressionCostForCondition(expr.args[1], preloadedConstants) : 0) + 1;
    }
    return (expr.args[0] ? estimateExpressionCostForCondition(expr.args[0], preloadedConstants) : 0) + 1;
  }
  function estimateNegativeZeroDegreeSelectorCost(expr, preloadedConstants) {
    if (expr.args.length !== 2 || expr.args[0] === void 0 || expr.args[1] === void 0) {
      return Number.POSITIVE_INFINITY;
    }
    return estimateNegativeZeroThresholdRawCost(expr.args[0], expr.args[1], preloadedConstants) + 1;
  }
  function estimateNegativeZeroThresholdFlowCost(threshold, preloadedConstants) {
    return estimateNegativeZeroThresholdRawCost(threshold.value, numberExpression(threshold.bound), preloadedConstants) + 2;
  }
  function estimateNegativeZeroThresholdRawCost(value, bound, preloadedConstants) {
    const ratio = divideExpressions(value, bound);
    return estimateExpressionCostForCondition(ratio, preloadedConstants) + 4;
  }
  function estimateExpressionCost(expr) {
    switch (expr.kind) {
      case "number":
        return estimateNumberCost(expr.raw);
      case "identifier":
        return 1;
      case "unary":
        return estimateExpressionCost(expr.expr) + 1;
      case "binary": {
        const remainder = matchRemainderByConstant(expr);
        if (remainder !== void 0) {
          return estimateExpressionCost(remainder.value) + estimateExpressionCost(remainder.divisor) * 2 + 3;
        }
        return estimateExpressionCost(expr.left) + estimateExpressionCost(expr.right) + 1;
      }
      case "call":
        return estimateCallCost(expr);
    }
  }
  function estimateCallCost(expr) {
    const name = expr.callee.toLowerCase();
    if (name === NEGATIVE_ZERO_DEGREE_SELECTOR_GE) {
      return estimateNegativeZeroDegreeSelectorCost(expr);
    }
    const macro = ticTacToeExpressionMacro(name, expr.args);
    if (macro !== void 0) return estimateExpressionCost(macro);
    if (name === "random" || name === "pi") return 1;
    if (name === "pow") {
      return (expr.args[0] ? estimateExpressionCost(expr.args[0]) : 0) + (expr.args[1] ? estimateExpressionCost(expr.args[1]) : 0) + 1;
    }
    if (["max", "bit_and", "bit_or", "bit_xor"].includes(name)) {
      return (expr.args[0] ? estimateExpressionCost(expr.args[0]) : 0) + (expr.args[1] ? estimateExpressionCost(expr.args[1]) : 0) + 1;
    }
    return (expr.args[0] ? estimateExpressionCost(expr.args[0]) : 0) + 1;
  }
  function estimateNumberCost(raw) {
    const normalized = raw.trim().toLowerCase();
    const negative = normalized.startsWith("-");
    const unsigned = negative ? normalized.slice(1) : normalized;
    const [mantissa = "0", exponent] = unsigned.split("e");
    let cost = negative ? 1 : 0;
    for (const char of mantissa) {
      if (char === "." || /\d/u.test(char)) cost += 1;
    }
    if (exponent !== void 0) {
      cost += 1;
      if (exponent.startsWith("-")) cost += 1;
      cost += exponent.replace(/^[+-]/u, "").length;
    }
    return cost;
  }
  function buildIrReport(ast, items, steps) {
    return {
      lowered: hasLoweredIr(ast),
      v2: ast.v2 !== void 0,
      intentNodes: countIntentNodes(ast),
      effectOps: items.filter((item) => item.kind !== "label").length,
      layoutCells: steps
    };
  }
  function buildOptimizerReport(ast, _options, optimizations, candidates, cellRoles, machineProfile) {
    const activeNames = new Set(optimizations.map((optimization) => optimization.name));
    if (cellRoles.some((cell) => cell.roles.includes("overlay"))) activeNames.add("code-data-overlay");
    if (cellRoles.some((cell) => cell.roles.includes("dark-entry"))) activeNames.add("dark-entry-layout");
    if (cellRoles.some((cell) => cell.roles.includes("display-byte"))) activeNames.add("display-byte-layout");
    if (machineProfile.emulatorFacts.some((fact) => fact.id === "step-vs-run-delta")) {
      activeNames.add("step-vs-run-profile");
    }
    const selectedCandidateVariants = new Set(
      candidates.filter((candidate) => candidate.selected).map((candidate) => candidate.variant)
    );
    const consideredCandidateVariants = new Set(
      candidates.filter((candidate) => !candidate.selected).map((candidate) => candidate.variant)
    );
    const capabilities = optimizerCapabilities.map((capability) => {
      let status = capability.planned ? "planned" : "candidate";
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
        requires: capability.requires
      };
    });
    return {
      automatic: true,
      active: capabilities.filter((capability) => capability.status === "active").length,
      considered: capabilities.filter((capability) => capability.status === "considered").length,
      candidate: capabilities.filter((capability) => capability.status === "candidate").length,
      planned: capabilities.filter((capability) => capability.status === "planned").length,
      capabilities
    };
  }
  var optimizerCapabilities = [
    {
      id: "store-recall-peephole",
      category: "stack",
      source: "documented",
      requires: [],
      activeWhen: ["store-recall-peephole"],
      detail: "Elides immediate X->\u041F r / \u041F->X r pairs when no exact-machine effect is crossed."
    },
    {
      id: "stack-current-x-scheduling",
      category: "stack",
      source: "documented",
      requires: [],
      activeWhen: ["stack-current-x-scheduling", "dead-temp-store"],
      detail: "Keeps a just-computed value in X for a following commutative use and removes the temporary store after data-flow proof."
    },
    {
      id: "tail-call-lowering",
      category: "flow",
      source: "documented",
      requires: [],
      activeWhen: ["tail-call-lowering", "terminal-rule-tail-call", "tail-call-layout"],
      detail: "Replaces subroutine calls in tail position with direct jumps so rule factoring does not force an extra return."
    },
    {
      id: "return-zero-jump",
      category: "flow",
      source: "mk61-delta",
      requires: [],
      activeWhen: ["return-zero-jump"],
      detail: "Uses \u0412/\u041E as one-cell \u0411\u041F 01 only when the return stack is known empty."
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
        "kor-digit-test"
      ],
      detail: "Umbrella rule for replacing provably equivalent conditionals with branchless arithmetic, sign, extrema, or masked updates."
    },
    {
      id: "zero-condition-test",
      category: "flow",
      source: "documented",
      requires: [],
      activeWhen: [
        "zero-condition-test",
        "fractional-indirect-addressing",
        "kor-digit-test"
      ],
      detail: "Uses direct F x?0 tests when one side of a condition is a proved zero, avoiding a zero literal and subtraction."
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
        "indirect-register-flow"
      ],
      detail: "Lowers high-level command dispatch automatically; small proven dispatches may use indirect or super-dark dispatch."
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
        "kzn-double"
      ],
      detail: "Replaces simple boolean if/else assignments, stops, and conditional moves with arithmetic selection when shorter."
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
        "negative-zero-threshold-flow"
      ],
      detail: "Uses a compiler-owned 1|-00 preload plus \u0412\u2191 normalization to build a 0/1 selector or flow test for bounded nonnegative threshold branches when that beats ordinary branching."
    },
    {
      id: "arithmetic-if-update",
      category: "flow",
      source: "documented",
      requires: [],
      activeWhen: [
        "arithmetic-if-update",
        "arithmetic-if-sign-toggle",
        "hex-mantissa-arithmetic"
      ],
      detail: "Replaces conditional +=/-= and sign toggles guarded by a proved boolean with masked arithmetic."
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
        "kmax-zero-through"
      ],
      detail: "Replaces simple extrema, sign, clamp, comparison-mask, and boolean-algebra branches when shorter. \u041A max zero-through counts as the extrema selection."
    },
    {
      id: "indirect-flow",
      category: "flow",
      source: "documented",
      requires: [],
      activeWhen: ["indirect-register-flow", "stable-indirect-flow", "preloaded-indirect-flow", "preloaded-super-dark-flow"],
      detail: "Candidate rule: replace direct branches/calls with \u041A \u0411\u041F/\u041A \u041F\u041F/\u041A x?0 when the address value is already live, or when a compiler-owned preload is cheaper."
    },
    {
      id: "indirect-memory-table",
      category: "data",
      source: "documented",
      requires: ["indirect-memory"],
      activeWhen: ["indirect-memory-table"],
      detail: "Rewrites direct register memory access through \u041A \u041F->X/\u041A X->\u041F when a stable selector already proves the target register."
    },
    {
      id: "fl-decrement-branch",
      category: "flow",
      source: "documented",
      requires: [],
      activeWhen: ["fl-unit-decrement", "r0-indirect-counter"],
      detail: "Uses F L0..F L3 as compact decrement-and-continue/decrement-and-branch forms for small counters."
    },
    {
      id: "address-constant-overlay",
      category: "layout",
      source: "undocumented",
      requires: ["address-constants", "code-data-overlay"],
      activeWhen: ["code-data-overlay"],
      detail: "Lets branch operands double as constants or executable bytes after the layout pass marks a conflict-free overlay role."
    },
    {
      id: "cyclic-address-layout",
      category: "layout",
      source: "undocumented",
      requires: ["dark-entries", "code-data-overlay"],
      activeWhen: ["cyclic-address-layout"],
      detail: "Uses formal address-space wraparound and side branches only after the layout pass proves the shared-tail target."
    },
    {
      id: "constants-dual-use",
      category: "data",
      source: "undocumented",
      requires: ["address-constants"],
      activeWhen: ["constants-dual-use"],
      detail: "Reuses one stored value as arithmetic coefficient, rounding adjuster, and address-like dispatch data."
    },
    {
      id: "dark-entry-layout",
      category: "layout",
      source: "undocumented",
      requires: ["dark-entries"],
      activeWhen: ["dark-entry-layout"],
      detail: "Exposes shared tails as dark-entry candidates when the layout pass can point at the same executable suffix."
    },
    {
      id: "super-dark-dispatch",
      category: "flow",
      source: "undocumented",
      requires: ["super-dark-dispatch", "indirect-flow"],
      activeWhen: ["super-dark-dispatch", "preloaded-super-dark-flow"],
      detail: "Dispatch candidate for indirect \u041A \u0411\u041F R with FA..FF; selected only when layout can place one-command cases at 48..53, tails at 01..06, and prove the selector register contains FA..FF."
    },
    {
      id: "r0-alias-indirect",
      category: "flow",
      source: "mk61-delta",
      requires: ["undocumented-opcodes", "r0-t-alias"],
      activeWhen: ["r0-indirect-counter"],
      detail: "Treats MK-61 *F/R0 aliases as byte/formal-address candidates only; the profile proves they transform R0."
    },
    {
      id: "r0-fractional-sentinel",
      category: "flow",
      source: "mk61-delta",
      requires: ["r0-fractional-sentinel"],
      activeWhen: ["fractional-indirect-addressing", "r0-indirect-counter", "r0-fractional-sentinel"],
      detail: "Computed-dispatch candidate for fractional R0 selecting R3 or jumping to 99 while creating the -99999999 sentinel."
    },
    {
      id: "raw-display-5f",
      category: "display",
      source: "undocumented",
      requires: ["raw-display-5f"],
      activeWhen: [],
      detail: "Display lowering candidate for opcode 5F; selected only when the raw display mutation is the intended observable effect."
    },
    {
      id: "x2-display-register",
      category: "display",
      source: "mk61-delta",
      requires: ["x2-register", "display-bytes"],
      activeWhen: ["x2-display-byte-scheduling", "display-byte-layout"],
      detail: "Display/data candidate for scheduling X2, \u0412\u041F, '.', sign digits, and display bytes without extra storage."
    },
    {
      id: "vp-fraction-restore",
      category: "display",
      source: "mk61-delta",
      requires: ["x2-register", "display-bytes"],
      activeWhen: ["vp-fraction-restore"],
      detail: "Uses \u0412\u041F where it simultaneously restores X2 and provides the needed fractional/mantissa side effect."
    },
    {
      id: "hex-mantissa-arithmetic",
      category: "data",
      source: "undocumented",
      requires: ["display-bytes"],
      activeWhen: ["hex-mantissa-arithmetic"],
      detail: "Represents compact state as hexadecimal mantissa/sign digits when display-boundary proofs hold."
    },
    {
      id: "fractional-indirect-addressing",
      category: "data",
      source: "mk61-delta",
      requires: ["indirect-flow"],
      activeWhen: ["fractional-indirect-addressing"],
      detail: "Uses indirect addressing truncation/fractional effects as data selection only with range and emulator facts."
    },
    {
      id: "kzn-double",
      category: "data",
      source: "documented",
      requires: [],
      activeWhen: ["kzn-double"],
      detail: "Uses \u041A \u0417\u041D as a one-cell numeric transform when equivalent to the needed doubling/sign-digit operation."
    },
    {
      id: "kor-digit-test",
      category: "data",
      source: "documented",
      requires: [],
      activeWhen: ["kor-digit-test"],
      detail: "Uses \u041A\u2228 as a compact digit/boundary test when bit-level equivalence is proved."
    },
    {
      id: "kmax-zero-through",
      category: "stack",
      source: "documented",
      requires: [],
      activeWhen: ["kmax-zero-through"],
      detail: "Uses \u041A max as a stack/value transform, including zero-through cases, when it preserves source semantics."
    },
    {
      id: "error-stop-idiom",
      category: "trap",
      source: "mk61-delta",
      requires: ["error-stops"],
      activeWhen: ["error-stop"],
      detail: "Use domain-error stops only for explicit trap intent or after verifier proves the failure mode is acceptable."
    },
    {
      id: "step-vs-run-verification",
      category: "verification",
      source: "mk61-delta",
      requires: [],
      activeWhen: ["step-vs-run-profile"],
      detail: "Uses mk61 emulator facts for Danilov-era differences between step mode, continuous run, exponent sign changes, Cx, \u0412\u2191, and \u041F->X as exact-machine preconditions."
    },
    {
      id: "jump-to-next-threading",
      category: "flow",
      source: "documented",
      requires: [],
      activeWhen: ["jump-to-next-threading"],
      detail: "Drops unconditional \u0411\u041F whose only target is the immediately following label after a layout pass collapses the trampoline."
    },
    {
      id: "duplicate-failure-tail-merge",
      category: "flow",
      source: "documented",
      requires: [],
      activeWhen: ["duplicate-failure-tail-merge"],
      detail: "Coalesces structurally identical pause-0 failure tails into a single shared exit, removing the trampoline cells between them."
    },
    {
      id: "liveness-analysis",
      category: "verification",
      source: "documented",
      requires: [],
      activeWhen: ["liveness-analysis"],
      detail: "Foundational data-flow pass: computes liveIn/liveOut per IR position so dead-store-elimination, register-coalesce and other proof-backed passes can fire."
    },
    {
      id: "dead-store-elimination",
      category: "stack",
      source: "documented",
      requires: [],
      activeWhen: ["dead-store-elimination"],
      detail: "Removes X->\u041F r writes whose register is never read again before the next write to the same register, using whole-program liveness."
    },
    {
      id: "last-x-reuse",
      category: "stack",
      source: "documented",
      requires: [],
      activeWhen: ["last-x-reuse"],
      detail: "Drops \u041F->X r when the IR pass can prove X already holds the value just stored to r and no intervening op clobbers X (including \u0421/\u041F, jumps, ALU)."
    },
    {
      id: "constant-folding",
      category: "data",
      source: "documented",
      requires: [],
      activeWhen: ["constant-folding"],
      detail: "Eliminates identity arithmetic such as '0 +' and '1 *' from the IR after upstream passes simplify the expression tree."
    },
    {
      id: "cse-display-block",
      category: "data",
      source: "documented",
      requires: [],
      activeWhen: ["cse-display-block"],
      detail: "Common subexpression elimination for pure recall/ALU blocks ending in \u0412/\u041E; redirects duplicates to a shared exit when profitable."
    },
    {
      id: "jump-thread",
      category: "flow",
      source: "documented",
      requires: [],
      activeWhen: ["jump-thread"],
      detail: "Threads jump-to-jump chains through trampoline labels to their final target, freeing intermediate cells."
    },
    {
      id: "dead-code-after-halt",
      category: "flow",
      source: "documented",
      requires: [],
      activeWhen: ["dead-code-after-halt"],
      detail: "CFG analysis from the entry point removes ops that are unreachable through any combination of fall-through and jump edges."
    },
    {
      id: "register-coalesce",
      category: "stack",
      source: "documented",
      requires: [],
      activeWhen: ["register-coalesce"],
      detail: "Coalesces non-overlapping direct-register live ranges after data-flow proves neither register is externally live, indirect, or a loop counter."
    },
    {
      id: "arithmetic-if-pass",
      category: "flow",
      source: "documented",
      requires: [],
      activeWhen: ["arithmetic-if-pass"],
      detail: "IR-level seat for branchless arithmetic-if rewriting; current size-gated rewriting still happens at the AST select stage and feeds this pass via the candidate ledger."
    },
    {
      id: "redundant-prologue-elimination",
      category: "flow",
      source: "documented",
      requires: [],
      activeWhen: ["redundant-prologue-elimination"],
      detail: "Removes a display/halt prologue that immediately precedes \u0411\u041F to a label whose forward prologue is byte-identical, since the user only ever observes the one display performed by the loop head."
    }
  ];
  function buildPreloadReport(ast, allocation) {
    const explicit = ast.preloads.map((preload) => ({
      register: preload.register,
      value: preload.value,
      countsAgainstProgram: false
    }));
    const synthetic = [];
    if (ast.v2) {
      for (const field of ast.v2.state) {
        const register = allocation.registers[field.name];
        if (!register) continue;
        const value = field.initial;
        if (value === void 0) continue;
        synthetic.push({
          register,
          value,
          countsAgainstProgram: false
        });
      }
    }
    const constants = Object.entries(allocation.constants).map(([value, register]) => ({
      register,
      value,
      countsAgainstProgram: false
    }));
    return [...explicit, ...synthetic, ...constants];
  }
  function buildNegativeZeroDegreePreloadReport(allocation, optimizations) {
    if (allocation.negativeZeroDegree === void 0) return [];
    if (!optimizations.some((optimization) => optimization.name === "negative-zero-threshold-selector")) return [];
    return [{
      register: allocation.negativeZeroDegree,
      value: NEGATIVE_ZERO_DEGREE_PRELOAD_VALUE,
      countsAgainstProgram: false,
      setupProgram: negativeZeroDegreeSetupProgramText(allocation.negativeZeroDegree),
      setupNote: `Run this setup program once before loading the main program; it leaves ${NEGATIVE_ZERO_DEGREE_PRELOAD_VALUE} in R${allocation.negativeZeroDegree}.`
    }];
  }
  function negativeZeroDegreeSetupProgramText(register) {
    const registerOpcode = (64 + registerIndex(register)).toString(16).toUpperCase().padStart(2, "0");
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
      "50"
    ].join(" ");
  }
  function buildBudgetReport(used, limit, largestBlocks, extraCells) {
    return {
      used,
      limit,
      remaining: limit - used,
      exceeded: used > limit,
      largestBlocks,
      officialSteps: used,
      extraCells,
      totalPhysicalCells: used + extraCells
    };
  }
  function buildMachineFeaturesUsed(machineProfile, optimizations, cellRoles, candidates) {
    const used = /* @__PURE__ */ new Map();
    const add = (id, detail, source) => {
      const targetDetail = machineProfile.features.find((feature) => feature.id === id)?.detail;
      used.set(id, {
        id,
        source,
        detail: targetDetail ? `${detail}; ${targetDetail}` : detail
      });
    };
    if (optimizations.some((optimization) => optimization.name === "return-zero-jump")) {
      add("return-empty-stack-jump", "Optimizer selected \u0412/\u041E as one-cell \u0411\u041F 01.", "optimizer");
    }
    if (optimizations.some((optimization) => optimization.name === "branch-removal")) {
      add("branch-removal", "Optimizer removed a conditional branch through a proved branchless equivalent.", "optimizer");
    }
    if (optimizations.some((optimization) => optimization.name === "fl-unit-decrement")) {
      add("fl-decrement-branch", "Optimizer selected F L0..F L3 for a unit decrement.", "optimizer");
    }
    if (optimizations.some(
      (optimization) => optimization.name === "indirect-register-flow" || optimization.name === "stable-indirect-flow" || optimization.name === "preloaded-indirect-flow" || optimization.name === "preloaded-super-dark-flow"
    )) {
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
      add("x2-restore-boundaries", "Optimizer used \u0412\u041F as both X2 restoration and fractional/mantissa transform.", "optimizer");
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
        formal ? "Layout emitted formal/dark MK-61 address operand(s)." : "Layout exposed shared tails as dark-entry candidates.",
        "layout"
      );
    }
    if (cellRoles.some((cell) => cell.roles.includes("display-byte"))) {
      add("display-bytes", "Display lowering marked cells as packed display-byte candidates.", "layout");
    }
    if (cellRoles.some((cell) => cell.roles.includes("address") && cell.roles.includes("constant"))) {
      add("address-constants", "Layout marked address cells as reusable constants.", "layout");
    }
    if (candidates.some(
      (candidate) => (candidate.variant === "super-dark-dispatch" || candidate.variant === "preloaded-super-dark-flow") && candidate.selected
    )) {
      add("super-dark-dispatch", "Optimizer selected FA..FF indirect one-command cases.", "optimizer");
    }
    return [...used.values()];
  }
  function machineItemsToLayoutCells(items) {
    const cells = [];
    let address = 0;
    for (const item of items) {
      if (item.kind === "label") continue;
      if (item.kind === "op") {
        cells.push({
          address,
          opcode: item.opcode,
          roles: ["exec"],
          tactic: item.comment ?? ""
        });
        address += 1;
        continue;
      }
      cells.push({
        address,
        opcode: item.formalOpcode ?? (typeof item.target === "number" ? item.target : 0),
        roles: ["address"],
        tactic: item.comment ?? ""
      });
      address += 1;
    }
    return cells;
  }
  function buildProofReport(ast, items, cellRoles, _options, optimizations, preloads) {
    const proofs = [];
    const usesSubroutine = items.some((item) => item.kind === "op" && item.opcode === 83);
    proofs.push({
      id: "return-stack-empty",
      status: usesSubroutine ? "not-needed" : "proved",
      detail: usesSubroutine ? "Program uses subroutine calls, so \u0412/\u041E-as-jump is not assumed globally." : "No \u041F\u041F opcodes were emitted; empty return stack precondition holds for \u0412/\u041E-as-\u0411\u041F01 rewrites."
    });
    if (optimizations.some((optimization) => optimization.name === "branch-removal")) {
      const variants = [
        ...new Set(
          optimizations.filter(
            (optimization) => optimization.name.startsWith("arithmetic-if-") || optimization.name.startsWith("negative-zero-threshold-")
          ).map((optimization) => optimization.name)
        )
      ];
      proofs.push({
        id: "branch-equivalence",
        status: "proved",
        detail: `Removed conditional branches via ${variants.join(", ")} after matching assignment/update shape and value ranges.`
      });
    }
    if (optimizations.some((optimization) => optimization.name === "negative-zero-threshold-selector")) {
      proofs.push({
        id: "negative-zero-threshold-selector",
        status: "proved",
        detail: "Selected only for bounded integer nonnegative thresholds; \u0412\u2191 normalizes the underflowed 1|-00 product before \u041A \u0417\u041D turns it into a 0/1 selector."
      });
    }
    if (optimizations.some(
      (optimization) => optimization.name === "stable-indirect-flow" || optimization.name === "preloaded-indirect-flow" || optimization.name === "preloaded-super-dark-flow" || optimization.name === "indirect-memory-table" || optimization.name === "r0-fractional-sentinel"
    )) {
      proofs.push({
        id: "indirect-addressing-ranges",
        status: "proved",
        detail: "Indirect selectors are rewritten only after local data-flow proves a stable target, a compiler-owned address preload, or the fractional R0 sentinel shape."
      });
    }
    if (optimizations.some((optimization) => optimization.name === "preloaded-super-dark-flow")) {
      const proof = verifySuperDarkSuffixLayout(machineItemsToLayoutCells(items), {
        selectorValues: superDarkSelectorValues(preloads)
      });
      proofs.push({
        id: "super-dark-suffix-layout",
        status: proof.proved ? "proved" : "assumed",
        detail: proof.proved ? `Compiler-owned FA..FF dispatch entries land on physical 48..53 and resume through proved 01..06 continuations (${proof.pairs.length} pair${proof.pairs.length === 1 ? "" : "s"}).` : `Compiler selected preloaded super-dark flow, but the final layout proof is incomplete: ${proof.reasons.join("; ")}.`
      });
    }
    if (ast.v2) {
      const ranged = ast.v2.state.filter((field) => field.min !== void 0 || field.max !== void 0);
      if (ranged.length > 0) {
        proofs.push({
          id: "value-ranges",
          status: "proved",
          detail: `Collected source ranges for ${ranged.map((field) => field.name).join(", ")}.`
        });
      }
      const observed = ast.v2.screens.flatMap((screen) => screen.sources);
      if (observed.length > 0) {
        proofs.push({
          id: "observability",
          status: "proved",
          detail: `Visible state is limited by screen declarations: ${[...new Set(observed)].join(", ")}.`
        });
      }
    }
    if (cellRoles.some((cell) => cell.roles.includes("display-byte"))) {
      proofs.push({
        id: "display-byte-observable-boundary",
        status: "assumed",
        detail: "Display-byte candidates are bounded by screen declarations and the exact mk61 profile."
      });
    }
    const formalOperands = items.filter((item) => item.kind === "address" && item.formalOpcode !== void 0).map((item) => formalAddressInfo(item.formalOpcode)).filter((info2) => info2.kind !== "official");
    if (formalOperands.length > 0) {
      proofs.push({
        id: "formal-address-operands",
        status: "proved",
        detail: `Resolved formal MK-61 address byte(s): ${formalOperands.map((info2) => `${info2.label}->${safeFormatAddress(info2.actual)}`).join(", ")}.`
      });
    }
    return proofs;
  }
  function superDarkSelectorValues(preloads) {
    const selectors = {};
    for (const preload of preloads) selectors[preload.register] = preload.value;
    return selectors;
  }
  function parseHotBlock(text) {
    const match = /^(.+)=(\d+)$/u.exec(text);
    if (!match) return { name: text, estimatedCells: 0 };
    return { name: match[1], estimatedCells: Number(match[2]) };
  }
  function hasLoweredIr(ast) {
    return ast.preloads.length > 0 || ast.domains.length > 0 || ast.states.length > 0 || ast.displays.length > 0 || ast.blocks.length > 0 || ast.entries.some((entry) => containsLoweredStatement(entry.body)) || ast.procs.some((proc) => containsLoweredStatement(proc.body));
  }
  function containsLoweredStatement(statements) {
    for (const statement of statements) {
      if (statement.kind === "input" || statement.kind === "dispatch" || statement.kind === "show" || statement.kind === "call") {
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
  function countIntentNodes(ast) {
    return countV2IntentNodes(ast) + ast.preloads.length + ast.domains.reduce((sum, domain) => sum + 1 + domain.lines.length, 0) + ast.states.reduce((sum, state) => sum + 1 + state.fields.length, 0) + ast.displays.length + ast.blocks.reduce((sum, block) => sum + 1 + countStatements(block.body), 0) + ast.entries.reduce((sum, entry) => sum + countStatements(entry.body), 0) + ast.procs.reduce((sum, proc) => sum + countStatements(proc.body), 0);
  }
  function countV2IntentNodes(ast) {
    const v2 = ast.v2;
    if (!v2) return 0;
    return 1 + v2.state.length + v2.screens.length + (v2.turn ? 1 + countV2Statements(v2.turn.body) : 0) + v2.rules.reduce((sum, rule) => sum + 1 + countV2Statements(rule.body), 0);
  }
  function countV2Statements(statements) {
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
  function countStatements(statements) {
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
  function selectDispatchCandidate(statement, machineProfile) {
    const site = statement.name ?? `dispatch@${statement.line}`;
    const fallthroughCost = estimateDispatchCost(statement, true);
    const candidates = [
      {
        site,
        variant: "fallthrough-compare-chain",
        steps: fallthroughCost,
        selected: true,
        reason: "uses case ordering; key-based dispatch does not already provide an address-valued selector"
      }
    ];
    if (machineSupports(machineProfile, "indirect-flow")) {
      candidates.push({
        site,
        variant: "indirect-register-flow",
        steps: Math.max(fallthroughCost, statement.cases.length + 3),
        selected: false,
        reason: "rejected; selector is key-valued, not address-valued, and building an address register would not beat the compare-chain"
      });
    }
    if (machineSupports(machineProfile, "dark-entries") && machineSupports(machineProfile, "address-constants") && machineSupports(machineProfile, "code-data-overlay")) {
      candidates.push({
        site,
        variant: "dark-indirect-table",
        steps: Math.max(4, statement.cases.length + 3),
        selected: false,
        reason: "considered; layout proof did not establish a conflict-free address/data table for this site"
      });
    }
    if (statement.cases.length <= 6 && machineSupports(machineProfile, "super-dark-dispatch") && machineSupports(machineProfile, "indirect-flow")) {
      candidates.push({
        site,
        variant: "super-dark-dispatch",
        steps: Math.max(3, statement.cases.length + 2),
        selected: false,
        reason: "considered; selector is key-valued, and layout proof did not place one-command cases at 48..53 with tails at 01..06"
      });
    }
    const selected = candidates.find((candidate) => candidate.selected) ?? candidates[0];
    return { selected, candidates };
  }
  function estimateDispatchCost(statement, fallthrough) {
    const bodyCost = statement.cases.reduce((sum, dispatchCase) => sum + countStatements(dispatchCase.body), 0);
    const defaultCost = statement.defaultBody ? countStatements(statement.defaultBody) : 0;
    const jumpsAfterCases = Math.max(0, statement.cases.length - (fallthrough && !statement.defaultBody ? 1 : 0));
    return 2 + statement.cases.length * 5 + jumpsAfterCases * 2 + bodyCost + defaultCost;
  }
  function orderRawInputs(inputs) {
    const order = /* @__PURE__ */ new Map([
      ["T", 0],
      ["Z", 1],
      ["Y", 2],
      ["X", 3]
    ]);
    return [...inputs].sort((left, right) => order.get(left.slot) - order.get(right.slot));
  }
  function formatRawContractDetail(statement) {
    const inputs = statement.inputs?.length ? `takes ${orderRawInputs(statement.inputs).map((input) => `${input.slot}=${expressionToIntentText(input.expr)}`).join(", ")}` : "takes none";
    const outputs = statement.outputs?.length ? `returns ${statement.outputs.map((output) => `${output.slot}->${output.target}`).join(", ")}` : "returns none";
    const clobbers = `clobbers ${(statement.clobbers ?? ["unknown"]).join(", ")}`;
    const preserves = `preserves ${(statement.preserves ?? ["unknown"]).join(", ")}`;
    return `Inserted raw MK-61 block at line ${statement.line}: ${inputs}; ${outputs}; ${clobbers}; ${preserves}.`;
  }
  function parseRawInstruction(text) {
    const hex3 = /^[0-9A-Fa-f]{2}$/u.exec(text);
    if (hex3) {
      const opcode = Number.parseInt(text, 16);
      return { opcode, mnemonic: getOpcode(opcode).name, comment: "raw hex" };
    }
    const direct = /^(БП|ПП|F\s*x<0|F\s*x=0|F\s*x(?:!=|≠)0|F\s*x(?:>=|≥)0|F\s*L[0-3])\s+([A-Za-z_][\w]*|[0-9A-Fa-f]{2})$/u.exec(text);
    if (direct) {
      const opcode = directOpcode(direct[1]);
      const target = parseTarget(direct[2]);
      return {
        opcode,
        mnemonic: getOpcode(opcode).name,
        ...typeof target === "object" ? target : { target },
        comment: "raw branch"
      };
    }
    const compactStore = /^хП([0-9a-eавсде])$/iu.exec(text);
    if (compactStore) {
      const register = registerFromText(compactStore[1]);
      return { opcode: 64 + registerIndex(register), mnemonic: `X->\u041F ${register}` };
    }
    const compactRecall = /^Пх([0-9a-eавсде])$/iu.exec(text);
    if (compactRecall) {
      const register = registerFromText(compactRecall[1]);
      return { opcode: 96 + registerIndex(register), mnemonic: `\u041F->X ${register}` };
    }
    const directMemory = /^(X(?:->|→)П|П(?:->|→)X)\s+R?([0-9a-eавсде])$/iu.exec(text);
    if (directMemory) {
      const register = registerFromText(directMemory[2]);
      const op = directMemory[1].replaceAll("\u2192", "->");
      const base = op.startsWith("X") ? 64 : 96;
      return {
        opcode: base + registerIndex(register),
        mnemonic: `${op} ${register}`
      };
    }
    const indirect = /^(К\s*)?(БП|ПП|X(?:->|→)П|П(?:->|→)X|x(?:!=|≠)0|x(?:>=|≥)0|x<0|x=0)\s*R?([0-9a-eавсде])$/iu.exec(text);
    if (indirect?.[1]) {
      const register = registerFromText(indirect[3]);
      return {
        opcode: indirectBase(indirect[2]) + registerIndex(register),
        mnemonic: `\u041A ${indirect[2]} ${register}`
      };
    }
    const compactIndirect = /^К(БП|ПП|Пх|хП)([0-9a-eавсде])$/iu.exec(text);
    if (compactIndirect) {
      const register = registerFromText(compactIndirect[2]);
      const op = compactIndirect[1] === "\u041F\u0445" ? "\u041F->X" : compactIndirect[1] === "\u0445\u041F" ? "X->\u041F" : compactIndirect[1];
      return {
        opcode: indirectBase(op) + registerIndex(register),
        mnemonic: `\u041A ${op} ${register}`
      };
    }
    const found = findOpcodeName(text);
    if (found) return { opcode: found.code, mnemonic: found.name };
    return void 0;
  }
  function parseTarget(text) {
    const formalOpcode = parseFormalAddressOpcode(text);
    if (formalOpcode === void 0) return text;
    const info2 = formalAddressInfo(formalOpcode);
    return { target: info2.ordinal, formalTargetOpcode: formalOpcode };
  }
  function directOpcode(text) {
    const normalized = text.replace(/\s+/g, " ").replaceAll("\u2260", "!=").replaceAll("\u2265", ">=");
    if (normalized === "\u0411\u041F") return 81;
    if (normalized === "\u041F\u041F") return 83;
    if (normalized === "F x<0") return 92;
    if (normalized === "F x=0") return 94;
    if (normalized === "F x!=0") return 87;
    if (normalized === "F x>=0") return 89;
    if (normalized === "F L0") return 93;
    if (normalized === "F L1") return 91;
    if (normalized === "F L2") return 88;
    if (normalized === "F L3") return 90;
    throw new Error(`Unknown direct opcode ${text}`);
  }
  function indirectBase(text) {
    const normalized = text.toLowerCase().replaceAll("\u2192", "->").replaceAll("\u2260", "!=").replaceAll("\u2265", ">=");
    if (normalized === "x!=0") return 112;
    if (normalized === "\u0431\u043F") return 128;
    if (normalized === "x>=0") return 144;
    if (normalized === "\u043F\u043F") return 160;
    if (normalized === "x->\u043F") return 176;
    if (normalized === "x<0") return 192;
    if (normalized === "\u043F->x") return 208;
    if (normalized === "x=0") return 224;
    throw new Error(`Unknown indirect opcode ${text}`);
  }
  function binaryOpcode(op) {
    return op === "+" ? 16 : op === "-" ? 17 : op === "*" ? 18 : 19;
  }
  function summarizeBlocks(items) {
    const blocks = [];
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
    return blocks.sort((a, b) => b.size - a.size).slice(0, 3).map((block) => `${block.label}=${block.size}`);
  }

  // src/core/format.ts
  function formatListing(result) {
    const lines = [
      " Step | Code | Command                 | Comment",
      "------+------+-------------------------+----------------"
    ];
    for (const step of result.steps) {
      const address = formatStepAddress(step.address).padStart(4, " ");
      const command = step.mnemonic.padEnd(23, " ");
      const comments = [step.comment].filter((value) => Boolean(value)).join("; ");
      lines.push(` ${address} |  ${step.hex}  | ${command} | ${comments}`);
    }
    return lines.join("\n");
  }
  function formatStepAddress(address) {
    try {
      return formatAddress(address);
    } catch {
      return `>${address.toString(16).toUpperCase()}`;
    }
  }

  // src/browser/emulator-bridge.ts
  var DEFAULT_COMPILE_OPTIONS = {
    delivery: "hex",
    budget: 105,
    analysis: false
  };
  var DEFAULT_DEBOUNCE_MS = 250;
  var STATUS_ID = "mk-pro-emulator-status";
  function compileForBrowser(source, options = {}) {
    const result = compileMKPro(source, { ...DEFAULT_COMPILE_OPTIONS, ...options });
    const programText = formatProgramTokens(result.steps.map((step) => step.hex));
    const setupPrograms = result.report.preloads.map((preload) => preload.setupProgram).filter((program) => program !== void 0);
    const setupProgramText = setupPrograms.length === 0 ? void 0 : setupPrograms.join("\n\n");
    return {
      source,
      programText,
      ...setupProgramText === void 0 ? {} : { setupProgramText },
      listing: formatListing(result),
      steps: result.steps,
      report: result.report,
      diagnostics: result.diagnostics
    };
  }
  function compileToProgramText(source, options = {}) {
    return compileForBrowser(source, options).programText;
  }
  function looksLikeMKProSource(text) {
    const normalized = text.trim();
    return /\bprogram\s+[A-Za-z_][\w-]*\s*\{/u.test(normalized);
  }
  function installEmulatorBridge(options = {}) {
    window.__mkProEmulatorBridge?.uninstall();
    const program = document.getElementById("program");
    const writeButton = document.getElementById("text_to_program");
    if (!(program instanceof HTMLElement)) {
      throw new Error("MK-Pro bridge cannot find #program on this page.");
    }
    if (!(writeButton instanceof HTMLElement)) {
      throw new Error("MK-Pro bridge cannot find #text_to_program on this page.");
    }
    const statusElement = ensureStatusElement(writeButton);
    const cleanups = [];
    const compilerOptions = options.compilerOptions ?? {};
    const debounceMs = options.debounceMs ?? DEFAULT_DEBOUNCE_MS;
    const autoWriteOnPaste = options.autoWriteOnPaste ?? false;
    let lastResult;
    let lastSource;
    let previewTimer;
    const setStatus = (message, isError = false) => {
      statusElement.textContent = message;
      statusElement.style.color = isError ? "#ff9b9b" : "#b8ffd8";
    };
    const compileField = () => {
      const source = readProgramText(program);
      if (!looksLikeMKProSource(source)) return void 0;
      const compiled = compileForBrowser(source, compilerOptions);
      lastSource = source;
      lastResult = compiled;
      writeProgramText(program, compiled.programText);
      program.dispatchEvent(new CustomEvent("mk-pro:compiled", {
        bubbles: true,
        detail: compiled
      }));
      setStatus(
        compiled.setupProgramText === void 0 ? `MK-Pro compiled: ${compiled.report.steps}/${compiled.report.budget} cells.` : `MK-Pro compiled: ${compiled.report.steps}/${compiled.report.budget} cells; setup program required.`
      );
      return compiled;
    };
    const writeFieldToMemory = () => {
      const compiled = compileField();
      writeButton.click();
      return compiled;
    };
    const restoreSource = () => {
      if (lastSource === void 0) return false;
      writeProgramText(program, lastSource);
      setStatus("MK-Pro source restored.");
      return true;
    };
    const uninstall = () => {
      if (previewTimer !== void 0) window.clearTimeout(previewTimer);
      for (const cleanup of cleanups.splice(0)) cleanup();
      statusElement.remove();
      if (window.__mkProEmulatorBridge === bridge) {
        delete window.__mkProEmulatorBridge;
      }
      if (window.MKProEmulator === bridge) {
        delete window.MKProEmulator;
      }
    };
    const bridge = {
      compileField,
      writeFieldToMemory,
      restoreSource,
      uninstall,
      getLastResult: () => lastResult,
      statusElement
    };
    const beforeEmulatorWrite = (event) => {
      try {
        compileField();
      } catch (error) {
        event.preventDefault();
        event.stopImmediatePropagation();
        setStatus(errorToStatus(error), true);
        console.error("[MK-Pro] compile failed", error);
      }
    };
    const previewCurrentText = () => {
      const source = readProgramText(program);
      if (!looksLikeMKProSource(source)) return;
      try {
        const compiled = compileForBrowser(source, compilerOptions);
        lastResult = compiled;
        setStatus(
          compiled.setupProgramText === void 0 ? `MK-Pro detected: ${compiled.report.steps}/${compiled.report.budget} cells. Click Write to load.` : `MK-Pro detected: ${compiled.report.steps}/${compiled.report.budget} cells. Run setupProgramText once, then click Write.`
        );
      } catch (error) {
        setStatus(errorToStatus(error), true);
      }
    };
    const schedulePreview = () => {
      if (previewTimer !== void 0) window.clearTimeout(previewTimer);
      previewTimer = window.setTimeout(previewCurrentText, debounceMs);
    };
    const afterPaste = () => {
      window.setTimeout(() => {
        if (autoWriteOnPaste && looksLikeMKProSource(readProgramText(program))) {
          writeFieldToMemory();
        } else {
          previewCurrentText();
        }
      }, 0);
    };
    writeButton.addEventListener("click", beforeEmulatorWrite, true);
    program.addEventListener("input", schedulePreview);
    program.addEventListener("paste", afterPaste);
    cleanups.push(() => writeButton.removeEventListener("click", beforeEmulatorWrite, true));
    cleanups.push(() => program.removeEventListener("input", schedulePreview));
    cleanups.push(() => program.removeEventListener("paste", afterPaste));
    setStatus("MK-Pro bridge ready. Paste MK-Pro source, then click Write.");
    window.__mkProEmulatorBridge = bridge;
    window.MKProEmulator = bridge;
    return bridge;
  }
  function formatProgramTokens(tokens) {
    const rows = [];
    for (let index = 0; index < tokens.length; index += 16) {
      rows.push(tokens.slice(index, index + 16).join(" "));
    }
    return rows.join("\n");
  }
  function readProgramText(element) {
    const text = element.innerText ?? element.textContent;
    return (text ?? "").replace(/\u00a0/gu, " ").trim();
  }
  function writeProgramText(element, text) {
    element.innerHTML = text.split(/\r?\n/u).map(escapeHtml).join("<br>");
  }
  function ensureStatusElement(anchor) {
    const oldStatus = document.getElementById(STATUS_ID);
    if (oldStatus instanceof HTMLElement) oldStatus.remove();
    const status = document.createElement("div");
    status.id = STATUS_ID;
    status.style.marginTop = "6px";
    status.style.font = "13px monospace";
    status.style.color = "#b8ffd8";
    anchor.parentElement?.append(status);
    return status;
  }
  function escapeHtml(text) {
    return text.replace(/&/gu, "&amp;").replace(/</gu, "&lt;").replace(/>/gu, "&gt;");
  }
  function errorToStatus(error) {
    if (error instanceof CompileError) {
      return error.diagnostics.map((diagnostic) => {
        const line = diagnostic.line === void 0 ? "" : `:${diagnostic.line}`;
        return `${diagnostic.level.toUpperCase()}${line}: ${diagnostic.message}`;
      }).join(" | ");
    }
    if (error instanceof Error) return error.message;
    return String(error);
  }
  var api = {
    compile: compileForBrowser,
    compileToProgramText,
    installEmulatorBridge,
    looksLikeMKProSource
  };
  if (typeof window !== "undefined") {
    window.MKPro = api;
    try {
      installEmulatorBridge();
      console.info("[MK-Pro] Emulator bridge installed.");
    } catch (error) {
      console.warn("[MK-Pro] Compiler loaded, but the emulator bridge was not installed.", error);
    }
  }
  return __toCommonJS(emulator_bridge_exports);
})();
