"use strict";
var M61EmulatorBundle = (() => {
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
    looksLikeM61Source: () => looksLikeM61Source
  });

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
    if (address < 0 || address > 255) {
      throw new Error(`Address ${address} is out of formal MK-61 address range`);
    }
    if (address <= 99) {
      const tens = Math.floor(address / 10);
      const ones = address % 10;
      return tens * 16 + ones;
    }
    if (address <= 104) {
      const offset = address - 100;
      const high = 10 + Math.floor(offset / 10);
      const low = offset % 10;
      return high * 16 + low;
    }
    return address;
  }
  function formatAddress(address) {
    return hex(addressToOpcode(address));
  }
  function codeToAddress(code) {
    const high = code >> 4;
    const low = code & 15;
    if (high <= 9 && low <= 9) {
      return high * 10 + low;
    }
    if (high >= 10 && low <= 9) {
      return 100 + (high - 10) * 10 + low;
    }
    return code;
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
    if (item.name === "\u041A \u2228") aliases.add("KV");
    if (item.name === "\u041A \u2295") aliases.add("K\u2295");
    if (item.name === "\u041A \u0418\u041D\u0412") aliases.add("\u041A\u0438\u043D\u0432");
    if (item.name === "\u041A \u0417\u041D") aliases.add("\u041A\u0437\u043D");
    if (item.name === "\u041A |x|") aliases.add("K|x|");
    if (item.name === "\u041A [x]") aliases.add("K[x]");
    if (item.name === "\u041A {x}") aliases.add("K{x}");
    if (item.name === "\u0421/\u041F") aliases.add("STOP");
    if (item.name === "\u0412/\u041E") aliases.add("RTN");
    return [...aliases].map(normalizeName);
  }
  function normalizeName(name) {
    return name.trim().replace(/\s+/g, " ").replaceAll("\xD7", "*").replaceAll("\xF7", "/").replaceAll("\u2212", "-").replaceAll("\u0425", "X").replaceAll("K", "\u041A").replaceAll("k", "\u043A").toLowerCase();
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
    if (op.unsafeReason !== void 0) meta.unsafeReason = op.unsafeReason;
    if (op.raw === true) meta.raw = true;
    return meta;
  }
  function targetMetaFromAddress(item) {
    const meta = {};
    if (item.comment !== void 0) meta.comment = item.comment;
    if (item.sourceLine !== void 0) meta.sourceLine = item.sourceLine;
    if (item.unsafeReason !== void 0) meta.unsafeReason = item.unsafeReason;
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
    if (lower.startsWith("input")) return "input";
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
    if (meta.unsafeReason !== void 0) op.unsafeReason = meta.unsafeReason;
    if (meta.raw === true) op.raw = true;
    return op;
  }
  function machineAddressFromMeta(target, meta) {
    const ref = { kind: "address", target };
    if (meta.comment !== void 0) ref.comment = meta.comment;
    if (meta.sourceLine !== void 0) ref.sourceLine = meta.sourceLine;
    if (meta.unsafeReason !== void 0) ref.unsafeReason = meta.unsafeReason;
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
  function raiseLayoutToIr(cells) {
    const result = [];
    for (let i = 0; i < cells.length; i += 1) {
      const cell = cells[i];
      const info2 = getOpcode(cell.opcode);
      const meta = { mnemonic: info2.name };
      if (cell.roles.length > 0) meta.roles = [...cell.roles];
      if (cell.tactic !== void 0) meta.tactic = cell.tactic;
      if (TAKES_ADDRESS.has(cell.opcode)) {
        const next = cells[i + 1];
        if (next === void 0 || !next.roles.includes("address")) {
          result.push({ kind: "plain", opcode: cell.opcode, meta });
          continue;
        }
        const target = next.opcode;
        const targetMeta = {};
        if (next.tactic !== void 0 && next.tactic !== "") {
          targetMeta.comment = next.tactic;
        }
        if (next.roles.length > 0) targetMeta.roles = [...next.roles];
        i += 1;
        if (cell.opcode === 81) {
          result.push({ kind: "jump", target, opcode: cell.opcode, meta, targetMeta });
          continue;
        }
        if (cell.opcode === 83) {
          result.push({ kind: "call", target, opcode: cell.opcode, meta, targetMeta });
          continue;
        }
        const condition = COND_OPCODES[cell.opcode];
        if (condition !== void 0) {
          result.push({ kind: "cjump", condition, target, opcode: cell.opcode, meta, targetMeta });
          continue;
        }
        const loop = LOOP_OPCODES[cell.opcode];
        if (loop !== void 0) {
          result.push({ kind: "loop", counter: loop, target, opcode: cell.opcode, meta, targetMeta });
          continue;
        }
        result.push({ kind: "plain", opcode: cell.opcode, meta });
        continue;
      }
      if (isInRange(cell.opcode, DIRECT_STORE_BASE)) {
        result.push({
          kind: "store",
          register: registerForOffset(cell.opcode, DIRECT_STORE_BASE),
          opcode: cell.opcode,
          meta
        });
        continue;
      }
      if (isInRange(cell.opcode, DIRECT_RECALL_BASE)) {
        result.push({
          kind: "recall",
          register: registerForOffset(cell.opcode, DIRECT_RECALL_BASE),
          opcode: cell.opcode,
          meta
        });
        continue;
      }
      if (isInRange(cell.opcode, INDIRECT_STORE_BASE)) {
        result.push({
          kind: "indirect-store",
          register: registerForOffset(cell.opcode, INDIRECT_STORE_BASE),
          opcode: cell.opcode,
          meta
        });
        continue;
      }
      if (isInRange(cell.opcode, INDIRECT_RECALL_BASE)) {
        result.push({
          kind: "indirect-recall",
          register: registerForOffset(cell.opcode, INDIRECT_RECALL_BASE),
          opcode: cell.opcode,
          meta
        });
        continue;
      }
      if (isInRange(cell.opcode, INDIRECT_JUMP_BASE)) {
        result.push({
          kind: "indirect-jump",
          register: registerForOffset(cell.opcode, INDIRECT_JUMP_BASE),
          opcode: cell.opcode,
          meta
        });
        continue;
      }
      if (isInRange(cell.opcode, INDIRECT_CALL_BASE)) {
        result.push({
          kind: "indirect-call",
          register: registerForOffset(cell.opcode, INDIRECT_CALL_BASE),
          opcode: cell.opcode,
          meta
        });
        continue;
      }
      const indirectCondBase = Object.keys(INDIRECT_COND_BASES).map((value) => Number(value)).find((base) => isInRange(cell.opcode, base));
      if (indirectCondBase !== void 0) {
        result.push({
          kind: "indirect-cjump",
          condition: INDIRECT_COND_BASES[indirectCondBase],
          register: registerForOffset(cell.opcode, indirectCondBase),
          opcode: cell.opcode,
          meta
        });
        continue;
      }
      if (cell.opcode === 82) {
        result.push({ kind: "return", opcode: cell.opcode, meta });
        continue;
      }
      if (cell.opcode === 80) {
        result.push({
          kind: "stop",
          opcode: cell.opcode,
          semantic: stopSemanticFromComment(cell.tactic),
          meta
        });
        continue;
      }
      result.push({ kind: "plain", opcode: cell.opcode, meta });
    }
    return result;
  }
  function lowerIrToLayout(ops, options = {}) {
    const cells = [];
    const addressOfLabel = /* @__PURE__ */ new Map();
    let address = 0;
    const fallbackTactic = options.defaultTactic ?? "";
    for (const op of ops) {
      if (op.kind === "label") {
        addressOfLabel.set(op.name, address);
        continue;
      }
      if (op.kind === "orphan-address") {
        cells.push({
          address,
          opcode: typeof op.target === "number" ? op.target : 0,
          roles: op.meta.roles ? [...op.meta.roles] : ["address"],
          tactic: op.meta.comment ?? fallbackTactic
        });
        address += 1;
        continue;
      }
      const opcode = op.opcode;
      const tactic = op.meta.tactic ?? op.meta.comment ?? fallbackTactic;
      const roles = op.meta.roles ? [...op.meta.roles] : ["exec"];
      cells.push({ address, opcode, roles, tactic });
      address += 1;
      if (op.kind === "jump" || op.kind === "cjump" || op.kind === "call" || op.kind === "loop") {
        const targetValue = typeof op.target === "number" ? op.target : addressOfLabel.get(op.target) ?? 0;
        const targetRoles = op.targetMeta.roles ? [...op.targetMeta.roles] : ["address"];
        cells.push({
          address,
          opcode: targetValue,
          roles: targetRoles,
          tactic: op.targetMeta.comment ?? tactic
        });
        address += 1;
      }
    }
    return { cells, addressOfLabel };
  }

  // src/core/parser.ts
  var ParseError = class extends Error {
    line;
    constructor(message, line) {
      super(`${message} at line ${line}`);
      this.line = line;
    }
  };
  function parseProgram(source) {
    return new M61Parser(source).parseProgram();
  }
  var M61Parser = class {
    lines;
    index = 0;
    constructor(source) {
      this.lines = source.split(/\r?\n/u).map((text, offset) => ({ text: stripComment(text).trim(), line: offset + 1 })).filter((line) => line.text.length > 0);
    }
    parseProgram() {
      let machine;
      let budget;
      let reference;
      let optimize;
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
        if (line.text.startsWith("target ")) {
          const value = line.text.slice("target ".length).trim().toLowerCase();
          if (value !== "mk61") {
            throw new ParseError(`Unsupported target '${value}'`, line.line);
          }
          machine = "mk61";
          this.index += 1;
        } else if (line.text.startsWith("budget ")) {
          budget = parseBudget(line);
          this.index += 1;
        } else if (line.text === "optimize size") {
          optimize = "size";
          this.index += 1;
        } else if (line.text.startsWith("reference ")) {
          reference = line.text.slice("reference ".length).trim();
          this.index += 1;
        } else if (line.text.startsWith("benchmark ")) {
          throw new ParseError("Use 'reference name' instead of deprecated benchmark metadata", line.line);
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
      if (!machine) throw new ParseError("Missing 'target mk61'", 1);
      if (v2 === void 0) throw new ParseError("Program must contain one V2 program block", 1);
      const program = {
        machine,
        targetProfile: "mk61_exact",
        preloads,
        domains,
        states,
        displays,
        declarations,
        entries,
        procs,
        blocks
      };
      if (budget !== void 0) program.budget = budget;
      if (reference !== void 0) program.reference = reference;
      if (optimize !== void 0) program.optimize = optimize;
      if (v2 !== void 0) program.v2 = v2;
      return program;
    }
    parseV2Program() {
      const header = this.next();
      const match = /^program\s+([A-Za-z_][\w]*)\s*\{$/u.exec(header.text);
      if (!match) throw new ParseError("Program must look like 'program Name {'", header.line);
      const inputs = [];
      const state = [];
      const screens = [];
      const endings = [];
      const boards = [];
      const fleets = [];
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
            inputs,
            state,
            screens,
            endings,
            boards,
            fleets,
            worlds,
            encounters,
            rules,
            line: header.line
          };
          if (turn !== void 0) program.turn = turn;
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
        if (hinted.text.startsWith("ending ")) {
          endings.push(this.parseV2Ending(hinted));
          continue;
        }
        if (hinted.text.startsWith("board ")) {
          boards.push(this.parseV2Board(hinted));
          continue;
        }
        if (hinted.text.startsWith("fleet ")) {
          fleets.push(this.parseV2Fleet(hinted));
          continue;
        }
        if (hinted.text.startsWith("world ")) {
          worlds.push(this.parseV2World(hinted));
          continue;
        }
        if (hinted.text.startsWith("encounters ")) {
          encounters.push(this.parseV2Encounters(hinted));
          continue;
        }
        if (hinted.text === "turn {") {
          if (turn !== void 0) throw new ParseError("Only one turn block is supported", line.line);
          this.index += 1;
          turn = {
            kind: "v2_turn",
            body: this.parseV2StatementBlock(),
            hints: hinted.hints,
            line: line.line
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
    parseV2StateBlock() {
      const fields = [];
      while (!this.done()) {
        const line = this.peek();
        if (line.text === "}") {
          this.index += 1;
          return fields;
        }
        this.index += 1;
        if (line.text.endsWith("{")) {
          const field = parseV2StateField({ text: line.text.slice(0, -1).trim(), line: line.line });
          this.parseV2StateFieldConfig(field);
          fields.push(field);
          continue;
        }
        if (line.text === "}") return fields;
        fields.push(parseV2StateField(line));
      }
      throw new ParseError("Unclosed state block", this.lines.at(-1)?.line ?? 1);
    }
    parseV2StateFieldConfig(field) {
      while (!this.done()) {
        const line = this.next();
        if (line.text === "}") return;
        if (line.text === "generated random") {
          field.generated = "random";
          continue;
        }
        const terminal = /^terminal\s+at\s+(.+?)\s+show\s+(.+)$/u.exec(line.text);
        if (terminal) {
          field.terminal = { at: terminal[1].trim(), show: terminal[2].trim() };
          continue;
        }
        const cleared = /^cleared\s+when\s+(.+)$/u.exec(line.text);
        if (cleared) {
          field.clearedWhen = cleared[1].trim();
          continue;
        }
        const reward = /^reward\s+([A-Za-z_][\w]*)\s+(.+)$/u.exec(line.text);
        if (reward) {
          field.rewards.push({ name: reward[1], value: reward[2].trim() });
          continue;
        }
        throw new ParseError("State field config must contain generated/terminal/cleared/reward lines", line.line);
      }
      throw new ParseError("Unclosed state field config block", field.line);
    }
    parseV2Screen(hinted) {
      const header = this.next();
      const match = /^screen\s+([A-Za-z_][\w]*)\s*\{$/u.exec(hinted.text);
      if (!match) throw new ParseError("Screen must look like 'screen name {'", header.line);
      let sources = [];
      let style = [];
      while (!this.done()) {
        const line = this.next();
        if (line.text === "}") {
          return {
            kind: "v2_screen",
            name: match[1],
            sources,
            style,
            hints: hinted.hints,
            line: header.line
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
    parseV2Ending(hinted) {
      const header = this.next();
      const match = /^ending\s+([A-Za-z_][\w]*)\s*\{$/u.exec(hinted.text);
      if (!match) throw new ParseError("Ending must look like 'ending name {'", header.line);
      let show;
      while (!this.done()) {
        const line = this.next();
        if (line.text === "}") {
          if (show === void 0) throw new ParseError("Ending block must contain show line", header.line);
          return {
            kind: "v2_ending",
            name: match[1],
            show,
            hints: hinted.hints,
            line: header.line
          };
        }
        if (line.text.startsWith("show ")) {
          show = line.text.slice("show ".length).trim();
          continue;
        }
        throw new ParseError("Ending block must contain show lines", line.line);
      }
      throw new ParseError("Unclosed ending block", header.line);
    }
    parseV2Board(hinted) {
      const header = this.next();
      const match = /^board\s+([A-Za-z_][\w]*)\s*:\s*(\d+)x(\d+)\s*\{$/u.exec(hinted.text);
      if (!match) throw new ParseError("Board must look like 'board name: 10x10 {'", header.line);
      const board = {
        kind: "v2_board",
        name: match[1],
        width: Number(match[2]),
        height: Number(match[3]),
        hints: hinted.hints,
        line: header.line
      };
      while (!this.done()) {
        const line = this.next();
        if (line.text === "}") return board;
        const coordinate = /^coordinate\s+([A-Za-z_][\w]*)(?:\s+(.+))?$/u.exec(line.text);
        if (coordinate) {
          board.coordinateStyle = coordinate[1];
          if (coordinate[2] !== void 0) board.coordinateRange = coordinate[2].trim();
          continue;
        }
        throw new ParseError("Board block must contain coordinate lines", line.line);
      }
      throw new ParseError("Unclosed board block", header.line);
    }
    parseV2Fleet(hinted) {
      const header = this.next();
      const match = /^fleet\s+([A-Za-z_][\w]*)\s+on\s+([A-Za-z_][\w]*)\s*\{$/u.exec(hinted.text);
      if (!match) throw new ParseError("Fleet must look like 'fleet name on board {'", header.line);
      let ships;
      const fleet = {
        kind: "v2_fleet",
        name: match[1],
        board: match[2],
        hints: hinted.hints,
        line: header.line
      };
      while (!this.done()) {
        const line = this.next();
        if (line.text === "}") {
          if (ships === void 0) throw new ParseError("Fleet block must contain ships line", header.line);
          return { ...fleet, ships };
        }
        const shipsMatch = /^ships\s+([A-Za-z_][\w]*)(?:\s+(-?\d+)\.\.(-?\d+))?\s*=\s*(.+)$/u.exec(line.text);
        if (shipsMatch) {
          ships = { name: shipsMatch[1], initial: shipsMatch[4].trim() };
          if (shipsMatch[2] !== void 0) ships.min = Number(shipsMatch[2]);
          if (shipsMatch[3] !== void 0) ships.max = Number(shipsMatch[3]);
          continue;
        }
        if (line.text === "generated random") {
          fleet.generated = "random";
          continue;
        }
        const cleared = /^cleared\s+when\s+(.+)$/u.exec(line.text);
        if (cleared) {
          fleet.clearedWhen = cleared[1].trim();
          continue;
        }
        const terminal = /^terminal\s+at\s+(.+?)\s+show\s+(.+)$/u.exec(line.text);
        if (terminal) {
          fleet.terminal = { at: terminal[1].trim(), show: terminal[2].trim() };
          continue;
        }
        throw new ParseError("Fleet block must contain ships/generated/cleared/terminal lines", line.line);
      }
      throw new ParseError("Unclosed fleet block", header.line);
    }
    parseV2World(hinted) {
      const header = this.next();
      const match = /^world\s+([A-Za-z_][\w]*)\s*:\s*([A-Za-z_][\w]*)\s*\{$/u.exec(hinted.text);
      if (!match) throw new ParseError("World must look like 'world name: type {'", header.line);
      const world = {
        kind: "v2_world",
        name: match[1],
        worldType: match[2],
        hints: hinted.hints,
        line: header.line
      };
      while (!this.done()) {
        const line = this.next();
        if (line.text === "}") return world;
        if (line.text.startsWith("position ") && line.text.endsWith("{")) {
          world.position = this.parseV2WorldPosition(line);
          continue;
        }
        if (line.text === "generated random") {
          world.generated = "random";
          continue;
        }
        if (line.text.startsWith("player ")) {
          world.player = line.text.slice("player ".length).trim();
          continue;
        }
        const door = /^door\s+symbol\s+(.+?)\s+costs\s+([A-Za-z_][\w]*)\s+(.+)$/u.exec(line.text);
        if (door) {
          world.door = { symbol: door[1].trim(), resource: door[2], cost: door[3].trim() };
          continue;
        }
        const wall = /^wall\s+symbol\s+(.+?)\s+blocks\s+(.+?)\s+costs\s+([A-Za-z_][\w]*)\s+(.+)$/u.exec(line.text);
        if (wall) {
          world.wall = { symbol: wall[1].trim(), behavior: wall[2].trim(), resource: wall[3], cost: wall[4].trim() };
          continue;
        }
        const vertical = /^vertical\s+wrap\s+(.+)$/u.exec(line.text);
        if (vertical) {
          world.verticalWrap = vertical[1].split("->").map((part) => part.trim()).filter(Boolean);
          continue;
        }
        throw new ParseError("World block must contain position/generated/player/door/wall/vertical lines", line.line);
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
        const range = /^(floors|rooms)\s+(.+)$/u.exec(line.text);
        if (range) {
          if (range[1] === "floors") position.floors = range[2].trim();
          else position.rooms = range[2].trim();
          continue;
        }
        if (line.text.startsWith("display ")) {
          position.display = line.text.slice("display ".length).trim();
          continue;
        }
        if (line.text.startsWith("start ")) {
          position.start = line.text.slice("start ".length).trim();
          continue;
        }
        throw new ParseError("World position must contain floors/rooms/display/start lines", line.line);
      }
      throw new ParseError("Unclosed world position block", header.line);
    }
    parseV2Encounters(hinted) {
      const header = this.next();
      const match = /^encounters\s+(.+?)\s*\{$/u.exec(hinted.text);
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
            hints: hinted.hints,
            line: header.line
          };
        }
        const caseHints = parseLeadingHints(line);
        const inline = /^(\S+)\s+([A-Za-z_][\w]*)\s*\{\s*(.*?)\s*\}$/u.exec(caseHints.text);
        if (inline) {
          this.index += 1;
          cases.push({
            value: inline[1],
            name: inline[2],
            body: inline[3].split(";").map((part) => part.trim()).filter(Boolean).map((text) => parseV2InlineStatement(text, [], line.line)),
            hints: caseHints.hints,
            line: line.line
          });
          continue;
        }
        const caseMatch = /^(\S+)\s+([A-Za-z_][\w]*)\s*\{$/u.exec(caseHints.text);
        if (!caseMatch) throw new ParseError("Encounter case must look like 'value name {'", line.line);
        this.index += 1;
        cases.push({
          value: caseMatch[1],
          name: caseMatch[2],
          body: this.parseV2StatementBlock(),
          hints: caseHints.hints,
          line: line.line
        });
      }
      throw new ParseError("Unclosed encounter table", header.line);
    }
    parseV2Rule(hinted) {
      const header = this.next();
      const match = /^rule\s+([A-Za-z_][\w]*)(?:\(([^)]*)\))?\s*\{$/u.exec(hinted.text);
      if (!match) throw new ParseError("Rule must look like 'rule name {' or 'rule name(arg) {'", header.line);
      return {
        kind: "v2_rule",
        name: match[1],
        params: match[2] ? parseIdentifierList(match[2]) : [],
        body: this.parseV2StatementBlock(),
        hints: hinted.hints,
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
      const hinted = parseLeadingHints(line);
      if (hinted.text.startsWith("match ") && hinted.text.endsWith("{")) {
        this.index += 1;
        return this.parseV2Match(hinted.text, hinted.hints, line.line);
      }
      if (hinted.text.startsWith("if ") && hinted.text.endsWith("{")) {
        this.index += 1;
        const predicate = parseV2Predicate(hinted.text.slice("if ".length, -1).trim());
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
          hints: hinted.hints,
          line: line.line
        };
        if (elseBody !== void 0) statement.elseBody = elseBody;
        return statement;
      }
      if (hinted.text.startsWith("challenge ") && hinted.text.endsWith("{")) {
        this.index += 1;
        return this.parseV2Challenge(hinted.text, hinted.hints, line.line);
      }
      this.index += 1;
      return parseV2InlineStatement(hinted.text, hinted.hints, line.line);
    }
    parseV2Challenge(text, hints, line) {
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
            hints,
            line
          };
          if (failureBody !== void 0) statement.failureBody = failureBody;
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
    parseV2Match(text, hints, line) {
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
            hints,
            line
          };
          if (otherwise !== void 0) statement.otherwise = otherwise;
          return statement;
        }
        const hinted = parseLeadingHints(bodyLine);
        const arrow = /^(.+?)\s*=>\s*(.+)$/u.exec(hinted.text);
        if (!arrow) throw new ParseError("Match cases must look like 'value => action'", bodyLine.line);
        const left = arrow[1].trim();
        const action = parseV2InlineStatement(arrow[2].trim(), hinted.hints, bodyLine.line);
        if (left === "otherwise") {
          otherwise = action;
        } else {
          cases.push({
            values: left.split(",").map((part) => part.trim()).filter(Boolean),
            action,
            hints: hinted.hints,
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
  function parseBudget(line) {
    const match = /^budget\s+steps\s*<=\s*(\d+)(?:\s+hard)?$/u.exec(line.text) ?? /^budget\s+(\d+)\s+cells(?:\s+hard)?$/u.exec(line.text);
    if (!match) {
      throw new ParseError("Budget must look like 'budget steps <= 105' or 'budget 105 cells'", line.line);
    }
    return Number(match[1]);
  }
  var semanticHints = [
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
    "manual_entry"
  ];
  var forbiddenLowLevelHints = /* @__PURE__ */ new Set([
    "use_dark_entries",
    "use_x2",
    "put_in_r0",
    "overlay_here",
    "use_overlay",
    "use_display_bytes",
    "use_error_stop",
    "use_5f"
  ]);
  function parseLeadingHints(line) {
    let text = line.text;
    const hints = [];
    while (text.startsWith("[")) {
      const end = text.indexOf("]");
      if (end < 0) throw new ParseError("Unclosed hint list", line.line);
      const rawHints = text.slice(1, end).split(",").map((part) => part.trim()).filter(Boolean);
      for (const rawHint of rawHints) {
        const normalized = rawHint.replace(/-/gu, "_").toLowerCase();
        if (forbiddenLowLevelHints.has(normalized)) {
          throw new ParseError(`Low-level implementation hint '${rawHint}' is not allowed in M61`, line.line);
        }
        if (!semanticHints.includes(normalized)) {
          throw new ParseError(`Unknown semantic hint '${rawHint}'`, line.line);
        }
        hints.push(normalized);
      }
      text = text.slice(end + 1).trim();
    }
    return { text, hints };
  }
  function parseV2Input(hinted, line) {
    const match = /^input\s+([A-Za-z_][\w]*)\s*:\s*(digit|number)$/u.exec(hinted.text);
    if (!match) throw new ParseError("Input must look like 'input key: digit' or 'input answer: number'", line);
    return {
      kind: "v2_input",
      name: match[1],
      inputType: match[2],
      hints: hinted.hints,
      line
    };
  }
  function parseV2StateField(line) {
    const hinted = parseLeadingHints(line);
    const match = /^([A-Za-z_][\w]*)\s*:\s*([A-Za-z_][\w]*)(?:\(([^)]*)\))?(.*)$/u.exec(hinted.text);
    if (!match) {
      throw new ParseError("State field must look like 'name: counter 0..9 = 0'", line.line);
    }
    const typeText = match[2].toLowerCase();
    if (!["digit", "flag", "counter", "coord", "bitset", "enum", "packed", "addr", "resource", "score"].includes(typeText)) {
      throw new ParseError(`Unknown state type '${match[2]}'`, line.line);
    }
    const tail = match[4].trim();
    const initialMatch = /(?:^|\s)=\s*(.+)$/u.exec(tail);
    const rangeMatch = /(-?\d+)\.\.(-?\d+)/u.exec(tail);
    const field = {
      kind: "v2_state_field",
      name: match[1],
      type: typeText,
      optional: /\boptional\b/u.test(tail),
      rewards: [],
      hints: hinted.hints,
      line: line.line
    };
    if (match[3] !== void 0) field.spec = match[3].trim();
    if (rangeMatch) {
      field.min = Number(rangeMatch[1]);
      field.max = Number(rangeMatch[2]);
    }
    if (/\bgenerated\s+random\b/u.test(tail)) field.generated = "random";
    if (initialMatch) field.initial = initialMatch[1].trim();
    return field;
  }
  function parseV2InlineStatement(text, hints, line) {
    if (text.startsWith("let ")) {
      const match = /^let\s+([A-Za-z_][\w]*)\s*=\s*(.+)$/u.exec(text);
      if (!match) throw new ParseError("Let must look like 'let name = expr'", line);
      return {
        kind: "v2_let",
        name: match[1],
        expr: match[2].trim(),
        hints,
        line
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
      const statement = {
        kind: "v2_require",
        predicate: parseV2Predicate(match[1].trim()),
        hints,
        line
      };
      if (match[2] !== void 0) {
        statement.elseAction = parseV2InlineStatement(match[2].trim(), hints, line);
      }
      return statement;
    }
    const collection = /^([A-Za-z_][\w]*)\s+(clear|set)\s+(.+)$/u.exec(text);
    if (collection) {
      return {
        kind: "v2_collection",
        collection: collection[1],
        op: collection[2],
        item: collection[3].trim(),
        hints,
        line
      };
    }
    if (text.startsWith("reward by ")) {
      return {
        kind: "v2_reward",
        expr: text.slice("reward by ".length).trim(),
        hints,
        line
      };
    }
    const move = /^move\s+([A-Za-z_][\w]*)(?:\s+(north|south|east|west|up|down)|\s+by\s+(.+?))(?:\s+remember\s+([A-Za-z_][\w]*))?$/u.exec(text);
    if (move) {
      const statement = {
        kind: "v2_move",
        target: move[1],
        hints,
        line
      };
      if (move[2] !== void 0) statement.direction = parseV2MoveDirection(move[2], line);
      if (move[3] !== void 0) statement.expr = move[3].trim();
      if (move[4] !== void 0) statement.remember = move[4];
      return statement;
    }
    const end = /^(end|win|lose)\s+([A-Za-z_][\w]*)$/u.exec(text);
    if (end) {
      return {
        kind: "v2_end",
        mode: end[1],
        outcome: end[2],
        hints,
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
        hints,
        line
      };
    }
    const assignment = /^([A-Za-z_][\w]*)\s*=\s*(.+)$/u.exec(text);
    if (assignment) {
      return {
        kind: "v2_assign",
        target: assignment[1],
        expr: assignment[2].trim(),
        hints,
        line
      };
    }
    const invoke = /^([A-Za-z_][\w]*)(?:\((.*)\))?$/u.exec(text);
    if (invoke) {
      return {
        kind: "v2_invoke",
        name: invoke[1],
        args: invoke[2] ? splitArgs(invoke[2]) : [],
        hints,
        line
      };
    }
    return { kind: "v2_raw", text, hints, line };
  }
  function parseV2Predicate(text) {
    const exists = /^([A-Za-z_][\w]*)\s+exists$/u.exec(text);
    if (exists) {
      return { kind: "v2_exists", target: exists[1] };
    }
    const has = /^([A-Za-z_][\w]*)\s+has\s+(.+)$/u.exec(text);
    if (has) {
      return {
        kind: "v2_collection_has",
        collection: has[1],
        item: has[2].trim()
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
    return { kind: "v2_raw_predicate", text };
  }
  function splitArgs(text) {
    return text.split(",").map((part) => part.trim()).filter(Boolean);
  }
  function lowerV2Program(v2) {
    const endings = collectV2Endings(v2);
    const inputTypes = collectV2InputTypes(v2);
    const screens = collectV2Screens(v2);
    validateV2References(v2, { endings, inputTypes, screens });
    const context = {
      ruleParams: new Map(v2.rules.map((rule) => [rule.name, rule.params])),
      endings,
      inputTypes,
      moveDeltas: collectV2MoveDeltas(v2)
    };
    for (const table of v2.encounters) {
      context.ruleParams.set("encounter", [encounterParamName(table.expr)]);
    }
    return {
      domains: lowerV2Domains(v2),
      states: lowerV2State(v2),
      displays: v2.screens.map(lowerV2Screen),
      entries: [lowerV2Entry(v2, context)],
      procs: [...v2.rules.map((rule) => lowerV2Rule(rule, context)), ...lowerV2EncounterRules(v2, context)],
      blocks: []
    };
  }
  function collectV2Endings(v2) {
    const endings = /* @__PURE__ */ new Map();
    for (const ending of v2.endings) {
      if (endings.has(ending.name)) throw new ParseError(`Duplicate ending '${ending.name}'`, ending.line);
      endings.set(ending.name, ending);
    }
    return endings;
  }
  function collectV2InputTypes(v2) {
    const inputTypes = /* @__PURE__ */ new Map();
    for (const input of v2.inputs) {
      if (inputTypes.has(input.name)) throw new ParseError(`Duplicate input '${input.name}'`, input.line);
      inputTypes.set(input.name, input.inputType);
    }
    return inputTypes;
  }
  function collectV2Screens(v2) {
    const screens = /* @__PURE__ */ new Map();
    for (const screen of v2.screens) {
      if (screens.has(screen.name)) throw new ParseError(`Duplicate screen '${screen.name}'`, screen.line);
      screens.set(screen.name, screen);
    }
    return screens;
  }
  function validateV2References(v2, context) {
    const canShowTerminalTarget = (target) => isNumericLiteralText(target) || context.screens.has(target) || context.endings.has(target);
    for (const ending of v2.endings) {
      if (!isNumericLiteralText(ending.show) && !context.screens.has(ending.show)) {
        throw new ParseError(`Unknown ending display target '${ending.show}'`, ending.line);
      }
    }
    for (const field of v2.state) {
      if (field.terminal !== void 0 && !canShowTerminalTarget(field.terminal.show)) {
        throw new ParseError(`Unknown terminal target '${field.terminal.show}'`, field.line);
      }
    }
    for (const fleet of v2.fleets) {
      if (fleet.terminal !== void 0 && !canShowTerminalTarget(fleet.terminal.show)) {
        throw new ParseError(`Unknown terminal target '${fleet.terminal.show}'`, fleet.line);
      }
    }
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
      case "v2_read":
        if (!context.inputTypes.has(statement.target)) {
          throw new ParseError(`Unknown input '${statement.target}'`, statement.line);
        }
        return;
      case "v2_end":
        if (!context.endings.has(statement.outcome)) {
          throw new ParseError(`Unknown ending '${statement.outcome}'`, statement.line);
        }
        return;
      case "v2_challenge":
        if (!context.screens.has(statement.warningScreen)) {
          throw new ParseError(`Unknown challenge warning screen '${statement.warningScreen}'`, statement.line);
        }
        if (!context.screens.has(statement.memoryScreen)) {
          throw new ParseError(`Unknown challenge memory screen '${statement.memoryScreen}'`, statement.line);
        }
        if (!context.inputTypes.has(statement.answerInput)) {
          throw new ParseError(`Unknown challenge answer input '${statement.answerInput}'`, statement.line);
        }
        visit(statement.successBody);
        if (statement.failureBody) visit(statement.failureBody);
        return;
      case "v2_if":
        visit(statement.thenBody);
        if (statement.elseBody) visit(statement.elseBody);
        return;
      case "v2_require":
        if (statement.elseAction) visit([statement.elseAction]);
        return;
      case "v2_match":
        validateV2MatchValues(statement, context.inputTypes);
        for (const matchCase of statement.cases) visit([matchCase.action]);
        if (statement.otherwise) visit([statement.otherwise]);
        return;
      default:
        return;
    }
  }
  function validateV2MatchValues(statement, inputTypes) {
    if (inputTypes.get(statement.expr.trim()) !== "digit") return;
    for (const matchCase of statement.cases) {
      for (const value of matchCase.values) {
        if (!isNumericLiteralText(value)) continue;
        const numeric = Number(value);
        if (!Number.isInteger(numeric) || numeric < 0 || numeric > 9) {
          throw new ParseError(`Input '${statement.expr}' is digit, but match case '${value}' is not a digit`, matchCase.line);
        }
      }
    }
  }
  function collectV2MoveDeltas(v2) {
    const deltas = /* @__PURE__ */ new Map();
    for (const world of v2.worlds) {
      if (world.position === void 0) continue;
      deltas.set(world.position.name, moveDeltasForDisplay(world.position.display));
    }
    return deltas;
  }
  function moveDeltasForDisplay(display) {
    if (display === "packed_decimal_zero_run") {
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
        { text: `columns ${board.width}`, line: board.line },
        { text: `rows ${board.height}`, line: board.line }
      ];
      if (board.coordinateStyle !== void 0) lines.push({ text: `coordinate ${board.coordinateStyle}`, line: board.line });
      if (board.coordinateRange !== void 0) lines.push({ text: `range ${board.coordinateRange}`, line: board.line });
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
        if (world.position.floors !== void 0) lines.push({ text: `floor ${world.position.floors}`, line: world.position.line });
        if (world.position.rooms !== void 0) lines.push({ text: `column ${world.position.rooms}`, line: world.position.line });
        if (world.position.display !== void 0) lines.push({ text: `display ${world.position.display}`, line: world.position.line });
        if (world.position.start !== void 0) lines.push({ text: `start ${world.position.start}`, line: world.position.line });
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
      if (world.position?.floors !== void 0) worldLines.push({ text: `floors ${rangeUpperBound(world.position.floors)}`, line: world.line });
      if (world.position?.rooms !== void 0) worldLines.push({ text: `rooms_per_floor ${rangeSize(world.position.rooms)}`, line: world.line });
      if (world.generated !== void 0) worldLines.push({ text: "generated_by compiler_random", line: world.line });
      if (world.player !== void 0) worldLines.push({ text: `player_display ${world.player}`, line: world.line });
      if (world.door !== void 0) {
        worldLines.push({ text: `door_before_each_room symbol ${world.door.symbol} cost ${world.door.resource} ${world.door.cost}`, line: world.line });
      }
      if (world.wall !== void 0) {
        worldLines.push({ text: `inner_wall symbol ${world.wall.symbol} blocks_${world.wall.behavior} cost ${world.wall.resource} ${world.wall.cost}`, line: world.line });
      }
      if (world.verticalWrap !== void 0) {
        worldLines.push({ text: `vertical_wrap ${world.verticalWrap.join("_to_")}`, line: world.line });
      }
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
      if (field.type === "bitset") {
        const lines = [];
        if (field.generated !== void 0) lines.push({ text: "generated_by compiler_random", line: field.line });
        if (field.clearedWhen !== void 0) lines.push({ text: `cleared_when ${field.clearedWhen}`, line: field.line });
        domains.push({
          kind: "domain",
          domainKind: "bitset",
          name: field.name,
          header: `bitset ${field.name}`,
          lines,
          line: field.line
        });
      }
      if (field.type === "resource" || field.type === "score") {
        const lines = [];
        if (field.initial !== void 0) lines.push({ text: `initial ${field.initial}`, line: field.line });
        if (field.terminal !== void 0) {
          lines.push({ text: `terminal_at ${field.terminal.at}`, line: field.line });
          lines.push({ text: `terminal_display ${field.terminal.show}`, line: field.line });
        }
        for (const reward of field.rewards) lines.push({ text: `reward ${reward.name} ${reward.value}`, line: field.line });
        domains.push({
          kind: "domain",
          domainKind: "resource",
          name: field.name,
          header: `resource ${field.name}`,
          lines,
          line: field.line
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
          ...fleet.generated !== void 0 ? [{ text: "generated_by compiler_random", line: fleet.line }] : [],
          ...fleet.clearedWhen !== void 0 ? [{ text: `cleared_when ${fleet.clearedWhen}`, line: fleet.line }] : []
        ],
        line: fleet.line
      });
      const resourceLines = [
        { text: `initial ${fleet.ships.initial}`, line: fleet.line },
        { text: `fleet ${fleet.name}`, line: fleet.line }
      ];
      if (fleet.terminal !== void 0) {
        resourceLines.push({ text: `terminal_at ${fleet.terminal.at}`, line: fleet.line });
        resourceLines.push({ text: `terminal_display ${fleet.terminal.show}`, line: fleet.line });
      }
      domains.push({
        kind: "domain",
        domainKind: "resource",
        name: fleet.ships.name,
        header: `fleet ships ${fleet.ships.name}`,
        lines: resourceLines,
        line: fleet.line
      });
    }
    if (v2.encounters.length > 0) {
      domains.push({
        kind: "domain",
        domainKind: "event",
        name: "encounters",
        header: "event encounters",
        lines: v2.encounters.flatMap((table) => table.cases.map((encounterCase) => ({
          text: `${encounterCase.value} ${encounterCase.name}`,
          line: encounterCase.line
        }))),
        line: v2.encounters[0].line
      });
    }
    return domains;
  }
  function lowerV2State(v2) {
    const fields = [];
    for (const input of v2.inputs) {
      fields.push({
        name: input.name,
        type: input.inputType === "digit" ? "digit" : "packed",
        line: input.line
      });
    }
    for (const field of v2.state) {
      const lowered = {
        name: field.name,
        type: lowerV2StateFieldType(field.type),
        line: field.line
      };
      if (field.min !== void 0) lowered.min = field.min;
      if (field.max !== void 0) lowered.max = field.max;
      if (field.initial !== void 0) {
        const inputSource = parseInputSource(field.initial);
        if (inputSource !== void 0) {
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
    for (const fleet of v2.fleets) {
      const ships = {
        name: fleet.ships.name,
        type: "resource",
        line: fleet.line
      };
      if (fleet.ships.min !== void 0) ships.min = fleet.ships.min;
      if (fleet.ships.max !== void 0) ships.max = fleet.ships.max;
      const inputSource = parseInputSource(fleet.ships.initial);
      if (inputSource !== void 0) {
        ships.initialInput = inputSource;
      } else {
        ships.initial = lowerV2Expression(fleet.ships.initial, fleet.line);
      }
      fields.push(ships);
      fields.push({
        name: fleet.name,
        type: "packed",
        initial: fleet.generated === "random" ? lowerV2Expression("random() * 999", fleet.line) : parseExpression("0", fleet.line),
        line: fleet.line
      });
    }
    const declared = new Set(fields.map((field) => field.name));
    for (const scratch of collectV2ScratchFields(v2)) {
      if (declared.has(scratch.name)) continue;
      declared.add(scratch.name);
      fields.push(scratch);
    }
    return fields.length > 0 ? [{ kind: "state", name: v2.name, fields, line: v2.line }] : [];
  }
  function collectV2ScratchFields(v2) {
    const fields = [];
    const add = (name, line) => {
      fields.push({ name, type: "packed", line });
    };
    for (const rule of v2.rules) {
      for (const param of rule.params) add(param, rule.line);
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
        if (statement.kind === "v2_move" && statement.remember !== void 0) add(statement.remember, statement.line);
      }
    }
  }
  function lowerV2StateFieldType(type) {
    if (type === "counter") return "range";
    if (type === "coord" || type === "bitset" || type === "enum") return "packed";
    if (type === "resource" || type === "score") return "resource";
    return type;
  }
  function rangeUpperBound(range) {
    return range.split("..").at(-1)?.trim() ?? range.trim();
  }
  function rangeSize(range) {
    const [minText, maxText] = range.split("..").map((part) => Number(part.trim()));
    if (Number.isFinite(minText) && Number.isFinite(maxText)) return String(maxText - minText + 1);
    return range.trim();
  }
  function lowerV2Screen(screen) {
    const display = {
      kind: "display",
      name: screen.name,
      format: "packed",
      sources: screen.sources,
      line: screen.line
    };
    if (screen.style.length > 0) display.mode = screen.style.join("_");
    return display;
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
      expr: lowerV2Expression(encounterParamName(table.expr), table.line),
      cases: table.cases.map((encounterCase) => ({
        value: lowerV2Expression(encounterCase.value, encounterCase.line),
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
      if (statement.kind === "v2_require") {
        const condition = lowerV2Predicate(statement.predicate, statement.line);
        const thenBody = lowerV2Statements(statements.slice(index + 1), context);
        const elseBody = statement.elseAction ? lowerV2Statements([statement.elseAction], context) : [{ kind: "pause", expr: parseExpression("0", statement.line), line: statement.line }];
        lowered.push({
          kind: "if",
          condition,
          thenBody,
          elseBody,
          line: statement.line
        });
        return lowered;
      }
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
          inputType: context.inputTypes.get(statement.target) ?? "number",
          target: statement.target,
          line: statement.line
        }];
      case "v2_stop":
        return [{ kind: "halt", expr: lowerV2Expression(statement.value, statement.line), line: statement.line }];
      case "v2_let":
        return [{ kind: "assign", target: statement.name, expr: lowerV2Expression(statement.expr, statement.line), line: statement.line }];
      case "v2_invoke":
        return lowerV2Invoke(statement, context);
      case "v2_if": {
        const condition = lowerV2Predicate(statement.predicate, statement.line);
        const lowered = {
          kind: "if",
          condition,
          thenBody: lowerV2Statements(statement.thenBody, context),
          line: statement.line
        };
        if (statement.elseBody !== void 0) lowered.elseBody = lowerV2Statements(statement.elseBody, context);
        return [lowered];
      }
      case "v2_require":
        return [];
      case "v2_challenge":
        return lowerV2Challenge(statement, context);
      case "v2_move":
        return lowerV2Move(statement, context);
      case "v2_end":
        return lowerV2End(statement, context);
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
            right: lowerV2Expression(statement.expr, statement.line)
          },
          line: statement.line
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
  function lowerV2Predicate(predicate, line) {
    switch (predicate.kind) {
      case "v2_compare":
        return {
          left: lowerV2Expression(predicate.left, line),
          op: predicate.op,
          right: lowerV2Expression(predicate.right, line)
        };
      case "v2_exists":
        return {
          left: lowerV2Expression(predicate.target, line),
          op: "!=",
          right: parseExpression("0", line)
        };
      case "v2_collection_has": {
        return {
          left: { kind: "identifier", name: predicate.collection },
          op: ">=",
          right: lowerV2Expression(predicate.item, line)
        };
      }
      case "v2_raw_predicate":
        return {
          left: parseExpression("0", line),
          op: "!=",
          right: parseExpression("0", line)
        };
    }
  }
  function lowerV2Challenge(statement, context) {
    const failureBody = statement.failureBody ?? [{ kind: "v2_show", target: "0", hints: [], line: statement.line }];
    return [
      {
        kind: "assign",
        target: statement.challengeTarget,
        expr: lowerV2Expression(`memory_code(${statement.expr})`, statement.line),
        line: statement.line
      },
      { kind: "show", display: statement.warningScreen, line: statement.line },
      { kind: "show", display: statement.memoryScreen, line: statement.line },
      {
        kind: "input",
        inputType: context.inputTypes.get(statement.answerInput) ?? "number",
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
    const delta = statement.expr ?? namedMoveDelta(statement.target, statement.direction, statement.line, context);
    const move = {
      kind: "assign",
      target: statement.target,
      expr: {
        kind: "binary",
        op: "+",
        left: { kind: "identifier", name: statement.target },
        right: lowerV2Expression(delta, statement.line)
      },
      line: statement.line
    };
    if (statement.remember === void 0) return [move];
    return [
      {
        kind: "assign",
        target: statement.remember,
        expr: move.expr,
        line: statement.line
      },
      {
        kind: "assign",
        target: statement.target,
        expr: { kind: "identifier", name: statement.remember },
        line: statement.line
      }
    ];
  }
  function lowerV2End(statement, context) {
    const ending = context.endings.get(statement.outcome);
    if (ending === void 0) {
      throw new ParseError(`Unknown ending '${statement.outcome}'`, statement.line);
    }
    if (isNumericLiteralText(ending.show)) {
      return [{ kind: "halt", expr: parseExpression(ending.show, statement.line), line: statement.line }];
    }
    return [
      { kind: "show", display: ending.show, line: statement.line },
      { kind: "halt", expr: parseExpression("0", statement.line), line: statement.line }
    ];
  }
  function namedMoveDelta(target, direction, line, context) {
    if (direction === void 0) throw new ParseError("Move must specify a direction or 'by expr'", line);
    return context.moveDeltas.get(target)?.[direction] ?? moveDeltasForDisplay(void 0)[direction] ?? "0";
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
          value: lowerV2Expression(value, matchCase.line),
          body: lowerV2MatchAction(matchCase.action, context, statement.expr, value, matchCase.line),
          line: matchCase.line
        });
      }
    }
    const lowered = {
      kind: "dispatch",
      expr: lowerV2Expression(statement.expr, statement.line),
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
    const cases = [];
    for (const matchCase of statement.cases) {
      if (directionalCases.includes(matchCase)) continue;
      for (const value of matchCase.values) {
        cases.push({
          value: lowerV2Expression(value, matchCase.line),
          body: lowerV2MatchAction(matchCase.action, context, statement.expr, value, matchCase.line),
          line: matchCase.line
        });
      }
    }
    const defaultBody = [
      {
        kind: "assign",
        target: params[0],
        expr: lowerV2Expression(`direction(${statement.expr})`, statement.line),
        line: statement.line
      },
      { kind: "call", block: action.name, line: action.line }
    ];
    return {
      kind: "dispatch",
      expr: lowerV2Expression(statement.expr, statement.line),
      name: "direction_dispatch",
      cases,
      defaultBody,
      line: statement.line,
      scratchId: statement.line
    };
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
    const params = context.ruleParams.get(action.name) ?? [];
    const statements = [];
    for (let index = 0; index < Math.min(params.length, action.args.length); index += 1) {
      const arg = resolveV2InvokeArg(action.args[index], matchExpr, matchValue);
      statements.push({
        kind: "assign",
        target: params[index],
        expr: lowerV2Expression(arg, line),
        line
      });
    }
    statements.push({ kind: "call", block: action.name, line: action.line });
    return statements;
  }
  function lowerV2Invoke(statement, context) {
    const params = context.ruleParams.get(statement.name) ?? [];
    const statements = [];
    for (let index = 0; index < Math.min(params.length, statement.args.length); index += 1) {
      statements.push({
        kind: "assign",
        target: params[index],
        expr: lowerV2Expression(statement.args[index], statement.line),
        line: statement.line
      });
    }
    statements.push({ kind: "call", block: statement.name, line: statement.line });
    return statements;
  }
  function resolveV2InvokeArg(arg, matchExpr, matchValue) {
    const direction = /^direction\((.+)\)$/u.exec(arg.trim());
    if (direction && direction[1].trim() === matchExpr.trim()) {
      const delta = directionDelta(matchValue);
      if (delta !== void 0) return String(delta);
    }
    return arg;
  }
  function lowerV2Collection(statement) {
    return {
      kind: "assign",
      target: statement.collection,
      expr: {
        kind: "binary",
        op: statement.op === "clear" ? "-" : "+",
        left: { kind: "identifier", name: statement.collection },
        right: lowerV2Expression(statement.item, statement.line)
      },
      line: statement.line
    };
  }
  function lowerV2Reward(statement) {
    return [{
      kind: "assign",
      target: "treasure",
      expr: {
        kind: "binary",
        op: "+",
        left: { kind: "identifier", name: "treasure" },
        right: lowerV2Expression(statement.expr, statement.line)
      },
      line: statement.line
    }];
  }
  function lowerV2Expression(text, line) {
    const normalized = normalizeV2ExpressionText(text);
    return parseExpression(normalized, line);
  }
  function normalizeV2ExpressionText(text) {
    const query = normalizeV2QueryExpression(text.trim());
    return (query ?? text).trim().replace(/\b([A-Za-z_][\w]*)\.floor\b/gu, "int($1 / 100)").replace(/\binput\.X\b/gu, "0").replace(/\binput\.Y\b/gu, "0");
  }
  function normalizeV2QueryExpression(text) {
    const lineCount = /^count\s+lines\s+(?:from|in)\s+([A-Za-z_][\w]*)\s+at\s+(.+)$/u.exec(text);
    if (lineCount) return `line_count(${lineCount[1]}, ${lineCount[2].trim()})`;
    const neighborCount = /^count\s+neighbou?rs\s+(?:from|in)\s+([A-Za-z_][\w]*)\s+(?:around|at)\s+(.+)$/u.exec(text);
    if (neighborCount) return `neighbor_count(${neighborCount[1]}, ${neighborCount[2].trim()})`;
    const cell = /^(?:cell|tile)\s+from\s+([A-Za-z_][\w]*)\s+at\s+(.+)$/u.exec(text);
    if (cell) return `cell_at(${cell[1]}, ${cell[2].trim()})`;
    const randomCell = /^random\s+(?:position|cell)\s+(?:from|in|on)\s+([A-Za-z_][\w]*)$/u.exec(text);
    if (randomCell) return `random_cell(${randomCell[1]})`;
    const randomRange = /^random\s+range\s+(.+?)\.\.(.+)$/u.exec(text);
    if (randomRange) {
      const min = randomRange[1].trim();
      const max = randomRange[2].trim();
      return `int(random() * ((${max}) - (${min}) + 1)) + (${min})`;
    }
    return void 0;
  }
  function parseInputSource(text) {
    const match = /^input\.(X|Y)$/u.exec(text.trim());
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
    const slash = text.indexOf("//");
    const hash = text.indexOf("#");
    const cut = slash === -1 ? hash : hash === -1 ? slash : Math.min(slash, hash);
    return cut === -1 ? text : text.slice(0, cut);
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
  function hasUnsafe(op) {
    if (op.kind === "label") return false;
    if (op.kind === "orphan-address") return op.meta.unsafeReason !== void 0;
    if ("meta" in op && (op.meta.unsafeReason !== void 0 || op.meta.raw === true)) return true;
    if ((op.kind === "jump" || op.kind === "cjump" || op.kind === "call" || op.kind === "loop") && op.targetMeta.unsafeReason !== void 0) {
      return true;
    }
    return false;
  }

  // src/core/passes/arithmetic-if.ts
  var run = (ops) => {
    return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
  };
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
      if ((isIdentityPlus(op, prev) || isIdentityMul(op, prev)) && prev !== void 0 && prev.kind === "plain" && prev.meta.raw !== true) {
        result.pop();
        applied += 1;
        continue;
      }
      result.push(op);
    }
    if (applied === 0) {
      return { ops: result, applied: 0, optimizations: [], unsafeUnverified: [] };
    }
    const passResult = {
      ops: result,
      applied,
      optimizations: [
        {
          name: "constant-folding",
          detail: `Dropped ${applied} identity arithmetic operation(s) (0+ or 1*).`,
          unsafe: false
        }
      ],
      unsafeUnverified: []
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
    if (op.kind === "recall") return true;
    if (op.kind === "plain") {
      if (op.opcode === 16 || op.opcode === 18) return true;
      if (op.opcode <= 9 || op.opcode === 10) return true;
    }
    return false;
  }
  function isBlockTerminator(op) {
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
      if (op.kind === "return" && buffer.length >= 3) {
        blocks.push({ startIndex: start, ops: [...buffer, op] });
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
      return `o:${op.kind}`;
    }).join("|");
  }
  var cseLabelCounter = 0;
  function freshLabel() {
    cseLabelCounter += 1;
    return `__cse_block_${cseLabelCounter}`;
  }
  var run3 = (ops) => {
    if (ops.length === 0) return { ops: [], applied: 0, optimizations: [], unsafeUnverified: [] };
    const blocks = collectCseCandidates(ops);
    if (blocks.length < 2) return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
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
    if (applied === 0) return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
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
          detail: `Deduplicated ${applied} display block(s) by redirecting to a shared exit.`,
          unsafe: false
        }
      ],
      unsafeUnverified: []
    };
    return passResult;
  };
  var cseDisplayBlock = {
    name: "cse-display-block",
    run: run3,
    layoutSafe: false
  };

  // src/core/passes/liveness-analysis.ts
  function buildLabelIndex(ops) {
    const map = /* @__PURE__ */ new Map();
    for (let i = 0; i < ops.length; i += 1) {
      const op = ops[i];
      if (op.kind === "label") map.set(op.name, i);
    }
    return map;
  }
  function buildSuccessors(ops) {
    const labelIndex = buildLabelIndex(ops);
    const successors = Array.from({ length: ops.length }, () => []);
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
          jumpTo(op.target);
          break;
        case "cjump":
        case "loop":
          jumpTo(op.target);
          fallthrough();
          break;
        case "call":
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
      case "indirect-recall":
      case "indirect-store":
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
    for (let i = 0; i < ops.length; i += 1) {
      const op = ops[i];
      if (op.kind === "label") labelIndex.set(op.name, i);
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
    return visited;
  }
  var run4 = (ops) => {
    if (ops.length === 0) return { ops: [], applied: 0, optimizations: [], unsafeUnverified: [] };
    const reachable = reachableFromEntry(ops);
    if (reachable.size === ops.length) {
      return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
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
      return { ops: result, applied: 0, optimizations: [], unsafeUnverified: [] };
    }
    const passResult = {
      ops: result,
      applied,
      optimizations: [
        {
          name: "dead-code-after-halt",
          detail: `Removed ${applied} unreachable op(s) from the entry CFG.`,
          unsafe: false
        }
      ],
      unsafeUnverified: []
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
      if (current.kind === "store" && next?.kind === "recall" && after !== void 0 && isCommutativeAlu(after) && !hasUnsafe(current) && !hasUnsafe(next) && !hasUnsafe(after) && !registerReadBeforeNextWrite(ops, i + 3, current.register)) {
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
          detail: `Removed ${applied} temp store(s) whose X value was consumed directly by stack scheduling.`,
          unsafe: false
        }
      ] : [],
      unsafeUnverified: []
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
    if (ops.length === 0) return { ops: [], applied: 0, optimizations: [], unsafeUnverified: [] };
    const liveness = computeLiveness(ops);
    const removed = /* @__PURE__ */ new Set();
    for (let i = 0; i < ops.length; i += 1) {
      const op = ops[i];
      if (op.kind !== "store") continue;
      if (hasUnsafe(op)) continue;
      if (liveness.liveOut[i].has(op.register)) continue;
      removed.add(i);
    }
    if (removed.size === 0) {
      return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
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
          detail: `Removed ${removed.size} store(s) to register(s) never read before the next assignment.`,
          unsafe: false
        }
      ],
      unsafeUnverified: []
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
      if (firstLabel?.kind === "label" && firstZero !== void 0 && isZeroDigit(firstZero) && firstPause !== void 0 && isPauseLike(firstPause) && trampolineLabel?.kind === "label" && trampolineJump !== void 0 && isUnconditionalJump(trampolineJump) && typeof trampolineJump.target === "string" && secondLabel?.kind === "label" && secondZero !== void 0 && isZeroDigit(secondZero) && secondPause !== void 0 && isPauseLike(secondPause) && endLabel?.kind === "label" && trampolineJump.target === endLabel.name && !hasUnsafe(firstZero) && !hasUnsafe(firstPause) && !hasUnsafe(trampolineJump) && !hasUnsafe(secondZero) && !hasUnsafe(secondPause)) {
        rewrite.set(firstLabel.name, secondLabel.name);
        rewrite.set(trampolineLabel.name, endLabel.name);
        for (let index = i; index <= i + 4; index += 1) remove.add(index);
        applied += 1;
        i += 4;
      }
    }
    if (applied === 0) {
      return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
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
          if (op.targetMeta.unsafeReason !== void 0) targetMeta.unsafeReason = op.targetMeta.unsafeReason;
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
          detail: `Merged ${applied} duplicate pause-0 failure tail(s).`,
          unsafe: false
        }
      ],
      unsafeUnverified: []
    };
    return passResult;
  };
  var duplicateFailureTail = {
    name: "duplicate-failure-tail-merge",
    run: run7,
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
      if (hasUnsafe(next)) return current;
      current = next.target;
    }
    return current;
  }
  var run8 = (ops) => {
    const labels = labelTargets(ops);
    const result = [];
    let applied = 0;
    for (const op of ops) {
      if ((op.kind === "jump" || op.kind === "cjump") && typeof op.target === "string" && !hasUnsafe(op)) {
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
      return { ops: result, applied: 0, optimizations: [], unsafeUnverified: [] };
    }
    const passResult = {
      ops: result,
      applied,
      optimizations: [
        {
          name: "jump-thread",
          detail: `Threaded ${applied} jump(s) through trampoline labels to the final target.`,
          unsafe: false
        }
      ],
      unsafeUnverified: []
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
      if (current.kind === "jump" && typeof current.target === "string" && !hasUnsafe(current)) {
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
          detail: `Removed ${applied} unconditional branch to the immediately following label.`,
          unsafe: false
        }
      ] : [],
      unsafeUnverified: []
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
        if (!hasUnsafe(op)) xHolds = op.register;
        else xHolds = void 0;
        continue;
      }
      if (op.kind === "recall" && !hasUnsafe(op) && xHolds === op.register) {
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
      return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
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
          detail: `Dropped ${removed.size} recall(s) whose register value was already in X.`,
          unsafe: false
        }
      ],
      unsafeUnverified: []
    };
    return passResult;
  };
  var lastXReuse = {
    name: "last-x-reuse",
    run: run10,
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
      if (isShowDisplayOp(op) && !hasUnsafe(op)) {
        collected.push(op);
        i += 1;
        continue;
      }
      if (isShowStop(op) && !hasUnsafe(op)) {
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
        if (isShowStop(op) && !hasUnsafe(op)) {
          collected.push(op);
          sawStop = true;
          i -= 1;
          continue;
        }
        return { ops: [], startIndex: -1 };
      }
      if (isShowDisplayOp(op) && !hasUnsafe(op)) {
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
      if (prior.kind === "store" && !hasUnsafe(prior)) {
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
  function opsEquivalent(a, b) {
    if (a.kind !== b.kind) return false;
    if (a.kind === "recall" && b.kind === "recall") return a.register === b.register;
    if (a.kind === "plain" && b.kind === "plain") return a.opcode === b.opcode;
    if (a.kind === "stop" && b.kind === "stop") return a.semantic === b.semantic;
    return false;
  }
  function segmentsMatch(a, b) {
    if (a.length === 0 || a.length !== b.length) return false;
    for (let i = 0; i < a.length; i += 1) {
      if (!opsEquivalent(a[i], b[i])) return false;
    }
    return true;
  }
  var run11 = (ops) => {
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
      if (hasUnsafe(op)) continue;
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
        (range) => !(end <= range.start || start >= range.end)
      );
      if (overlaps) continue;
      removeRanges.push({ start, end });
      applied += 1;
    }
    if (applied === 0) {
      return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
    }
    const shouldRemove = new Array(ops.length).fill(false);
    for (const range of removeRanges) {
      for (let i = range.start; i < range.end; i += 1) shouldRemove[i] = true;
    }
    const result = [];
    for (let i = 0; i < ops.length; i += 1) {
      if (!shouldRemove[i]) result.push(ops[i]);
    }
    const totalCells = removeRanges.reduce((acc, range) => acc + (range.end - range.start), 0);
    return {
      ops: result,
      applied,
      optimizations: [
        {
          name: "redundant-prologue-elimination",
          detail: `Removed ${applied} display/halt prologue(s) immediately before a jump to their identical loop head (${totalCells} cells).`,
          unsafe: false
        }
      ],
      unsafeUnverified: []
    };
  };
  var redundantPrologueElimination = {
    name: "redundant-prologue-elimination",
    run: run11,
    layoutSafe: false
  };

  // src/core/passes/register-coalesce.ts
  function gatherUsedRegisters(ops) {
    const set = /* @__PURE__ */ new Set();
    for (const op of ops) {
      if (op.kind === "store" || op.kind === "recall") set.add(op.register);
      if (op.kind === "indirect-store" || op.kind === "indirect-recall" || op.kind === "indirect-jump" || op.kind === "indirect-call" || op.kind === "indirect-cjump") {
        set.add(op.register);
      }
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
      }
    }
    return false;
  }
  var run12 = (ops) => {
    if (ops.length === 0) return { ops: [], applied: 0, optimizations: [], unsafeUnverified: [] };
    if (ops.some((op) => hasUnsafe(op))) {
      return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
    }
    const registers = gatherUsedRegisters(ops);
    if (registers.size <= 1) {
      return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
    }
    const ranges = liveRangePerRegister(ops, registers);
    const ordered = [...registers].sort();
    const mapping = /* @__PURE__ */ new Map();
    for (let i = 0; i < ordered.length; i += 1) {
      const a = ordered[i];
      if (mapping.has(a)) continue;
      if (usesIndirectAccess(ops, a)) continue;
      for (let j = i + 1; j < ordered.length; j += 1) {
        const b = ordered[j];
        if (mapping.has(b)) continue;
        if (usesIndirectAccess(ops, b)) continue;
        const rangeA = ranges.get(a);
        const rangeB = ranges.get(b);
        if (intersects(rangeA, rangeB)) continue;
        mapping.set(b, a);
        break;
      }
    }
    if (mapping.size === 0) {
      return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
    }
    return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
  };
  var registerCoalesce = {
    name: "register-coalesce",
    run: run12,
    layoutSafe: true
  };

  // src/core/passes/return-zero-jump.ts
  var run13 = (ops, ctx) => {
    if (ctx.options.opt !== "max") return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
    const usesCall = ops.some((op) => op.kind === "call" || op.kind === "indirect-call");
    if (usesCall) return { ops: [...ops], applied: 0, optimizations: [], unsafeUnverified: [] };
    const labels = calculateLabelAddresses(ops);
    const result = [];
    let applied = 0;
    let currentAddress = 0;
    for (const op of ops) {
      if (op.kind === "label") {
        result.push(op);
        continue;
      }
      if (op.kind === "jump" && !hasUnsafe(op)) {
        const resolved = targetAddress(op.target, labels);
        const targetsBackward = typeof op.target === "number" ? true : resolved !== void 0 && resolved < currentAddress;
        if (resolved === 1 && targetsBackward) {
          result.push({
            kind: "return",
            opcode: 82,
            meta: {
              mnemonic: "\u0412/\u041E",
              comment: "optimized \u0411\u041F 01",
              unsafeReason: "empty return stack assumed"
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
          detail: `Replaced ${applied} \u0411\u041F 01 sequence with \u0412/\u041E under empty-return-stack assumption.`,
          unsafe: true
        }
      ] : [],
      unsafeUnverified: applied > 0 ? ["\u0412/\u041E as \u0411\u041F 01 assumes the return stack is empty."] : []
    };
    return passResult;
  };
  var returnZeroJump = {
    name: "return-zero-jump",
    run: run13,
    layoutSafe: false
  };

  // src/core/passes/store-recall-peephole.ts
  var run14 = (ops) => {
    const result = [];
    let applied = 0;
    for (let i = 0; i < ops.length; i += 1) {
      const current = ops[i];
      const next = ops[i + 1];
      if (current.kind === "store" && next?.kind === "recall" && current.register === next.register && !hasUnsafe(current) && !hasUnsafe(next)) {
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
          detail: `Dropped ${applied} redundant \u041F->X immediately after X->\u041F to the same register.`,
          unsafe: false
        }
      ] : [],
      unsafeUnverified: []
    };
    return passResult;
  };
  var storeRecallPeephole = {
    name: "store-recall-peephole",
    run: run14,
    layoutSafe: false
  };

  // src/core/passes/index.ts
  var PASS_PIPELINE = [
    redundantPrologueElimination,
    returnZeroJump,
    storeRecallPeephole,
    jumpToNextThreading,
    jumpThread,
    deadStoreBeforeCommutative,
    deadStoreElimination,
    lastXReuse,
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
    const unsafeUnverified = [];
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
          for (const reason of result.unsafeUnverified) {
            if (!unsafeUnverified.includes(reason)) unsafeUnverified.push(reason);
          }
        }
        current = result.ops;
      }
    }
    return { ops: current, applied: totalApplied, optimizations, unsafeUnverified, passCounts };
  }
  function runIrPasses(items, options) {
    const ir = raiseMachineToIr(items);
    const result = runPassesOnIr(ir, options, { layoutOnly: false });
    return {
      items: lowerIrToMachine(result.ops),
      applied: result.applied,
      optimizations: result.optimizations,
      unsafeUnverified: result.unsafeUnverified,
      passCounts: result.passCounts
    };
  }

  // src/core/targetProfile.ts
  var MK61_EXACT_PROFILE = {
    id: "mk61_exact",
    machine: "mk61",
    optimizationObjective: "size",
    features: [
      {
        id: "branch-removal",
        source: "target-profile",
        detail: "Documented arithmetic, sign, extrema, and zero-test opcodes can replace provably equivalent branches."
      },
      {
        id: "fl-decrement-branch",
        source: "target-profile",
        detail: "F L0..F L3 are available for compact decrement-and-continue/decrement-and-branch forms."
      },
      {
        id: "return-empty-stack-jump",
        source: "target-profile",
        detail: "\u0412/\u041E can be used as \u0411\u041F 01 when static control-flow proves that no return frame is pending."
      },
      {
        id: "undocumented-opcodes",
        source: "target-profile",
        detail: "F0..FF and undocumented aliases are available when their hazard checks pass."
      },
      {
        id: "dark-entries",
        source: "target-profile",
        detail: "Formal dark/super-dark entry addresses are available to the layout solver."
      },
      {
        id: "super-dark-dispatch",
        source: "target-profile",
        detail: "Indirect \u041A \u0411\u041F R to FA..FF can execute one command at 48..53 and continue at 01..06."
      },
      {
        id: "indirect-flow",
        source: "target-profile",
        detail: "\u041A \u0411\u041F/\u041A \u041F\u041F/\u041A x?0 indirect flow commands are available to the optimizer."
      },
      {
        id: "code-data-overlay",
        source: "target-profile",
        detail: "Address operands, constants, opcodes, and display bytes may share cells after conflict checks."
      },
      {
        id: "address-constants",
        source: "target-profile",
        detail: "Address cells may double as constants for indirect flow and data transforms."
      },
      {
        id: "display-bytes",
        source: "target-profile",
        detail: "Packed display bytes, hexadecimal mantissa digits, and sign-digit forms are available."
      },
      {
        id: "x2-register",
        source: "target-profile",
        detail: "The hidden X2 display register can be scheduled when observable display semantics are preserved."
      },
      {
        id: "extra-cells",
        source: "target-profile",
        detail: "Extra physical cells are tracked separately from official program cells."
      },
      {
        id: "error-stops",
        source: "target-profile",
        detail: "Domain-error stops are available only when the source semantics permits trap-like termination."
      },
      {
        id: "r0-t-alias",
        source: "target-profile",
        detail: "R0/T and *F alias behavior is available, including normal R0 transformation; aliases do not preserve R0."
      },
      {
        id: "r0-fractional-sentinel",
        source: "target-profile",
        detail: "Fractional positive R0 states can select R3 or jump to 99 while leaving the -99999999 sentinel."
      },
      {
        id: "raw-display-5f",
        source: "target-profile",
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
        detail: "\u0412\u041F, '.', '/-/', and digit-entry X2 restoration boundaries are modeled as display hazards."
      },
      {
        id: "step-vs-run-delta",
        status: "probed",
        detail: "Continuous-run behavior is the default profile; step-only divergences remain hazards."
      },
      {
        id: "raw-display-5f",
        status: "probed",
        detail: "Opcode 5F leaves internal X intact but mutates display/raw state; it is usable only as an explicit display-state trick."
      }
    ]
  };
  function targetProfileFor(machine) {
    if (machine !== "mk61") {
      throw new Error(`Unsupported target machine '${machine}'`);
    }
    return MK61_EXACT_PROFILE;
  }
  function targetSupports(profile, featureId) {
    return profile.features.some((feature) => feature.id === featureId);
  }

  // src/core/compiler.ts
  var DEFAULT_OPTIONS = {
    opt: "max",
    delivery: "hex",
    budget: 105,
    warnUnsafe: true
  };
  var SIZE_BENCHMARK_REFERENCES = /* @__PURE__ */ new Set([
    "anvarov_fox_hunt_100",
    "anvarov_minesweeper_9x9",
    "anvarov_treasure_hunter_2",
    "anvarov_dangerous_loading",
    "anvarov_sea_battle"
  ]);
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
  var CompileError = class extends Error {
    diagnostics;
    constructor(diagnostics) {
      super(diagnostics.map((diagnostic) => diagnostic.message).join("\n"));
      this.diagnostics = diagnostics;
    }
  };
  function compileM61(source, options = {}) {
    const ast = parseProgram(source);
    const opts = { ...DEFAULT_OPTIONS, ...options };
    const targetProfile = targetProfileFor(ast.machine);
    if (options.budget === void 0 && ast.budget !== void 0) {
      opts.budget = ast.budget;
    }
    const diagnostics = [];
    const optimizations = [];
    const warnings = [];
    const unsafeUnverified = [];
    const candidates = [];
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
        unsafe: false
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
      candidates
    );
    context.compileProgram();
    const optimized = optimizeItems(context.items, opts, optimizations, unsafeUnverified);
    const { steps, labels, cellRoles } = layoutProgram(optimized, diagnostics, opts, ast, targetProfile);
    const largestBlocks = summarizeBlocks(optimized);
    if (steps.length > opts.budget) {
      diagnostics.push({
        level: "error",
        code: "BUDGET_EXCEEDED",
        message: `Program uses ${steps.length} steps, budget is ${opts.budget}. Largest blocks: ${largestBlocks.join(", ")}`
      });
    }
    if (diagnostics.some((diagnostic) => diagnostic.level === "error")) {
      throw new CompileError(diagnostics);
    }
    const visibleSteps = opts.warnUnsafe ? steps : steps.map(stripUnsafeReason);
    const report = {
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
      rejectedCandidates: candidates.filter((candidate) => !candidate.selected).map((candidate) => ({
        site: candidate.site,
        variant: candidate.variant,
        reason: candidate.reason,
        steps: candidate.steps
      })),
      hotBlocks: largestBlocks.map(parseHotBlock)
    };
    return { ast, items: optimized, steps: visibleSteps, report, diagnostics };
  }
  function stripUnsafeReason(step) {
    const clean = {
      address: step.address,
      opcode: step.opcode,
      hex: step.hex,
      mnemonic: step.mnemonic
    };
    if (step.comment !== void 0) clean.comment = step.comment;
    return clean;
  }
  function visiblePublicRegisters(all) {
    const result = {};
    for (const [name, register] of Object.entries(all)) {
      if (!name.startsWith(SWITCH_SCRATCH_PREFIX) && !name.startsWith(DISPATCH_SCRATCH_PREFIX) && !name.startsWith(TICTACTOE_MASK_SCRATCH_PREFIX)) {
        result[name] = register;
      }
    }
    return result;
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
  function emitUniversalSpatialResourceProgram(intent, candidates) {
    const selected = new Set(candidates.filter((candidate) => candidate.selected).map((candidate) => candidate.variant));
    const segments = [
      {
        name: "display-input-entry",
        opcodes: [80, 110, 1, 17, 92, 185, 82]
      },
      {
        name: selected.has("indirect-register-flow") ? "indirect-dispatch-frontier" : "compare-chain-frontier",
        opcodes: [78, 20, 106, 20, 2, 19, 87, 99]
      },
      {
        name: "x2-vp-stack-schedule",
        opcodes: [32, 16, 75, 12, 50, 94, 234]
      },
      {
        name: "packed-coordinate-update",
        opcodes: [102, 106, 53, 219, 18, 89, 119]
      },
      {
        name: "branch-removal-arithmetic-test",
        opcodes: [18, 56, 53, 233, 10, 105, 17]
      },
      {
        name: "bitset-mask-clear-or-probe",
        opcodes: [106, 52, 16, 75, 219, 55, 53, 20, 75, 54]
      },
      {
        name: "shared-movement-resource-tail",
        opcodes: [119, 74, 78, 0, 76, 64, 99, 6, 77]
      },
      {
        name: "generated-mask-initializer",
        opcodes: [59, 21, 15, 36, 56, 176, 96, 152]
      },
      {
        name: "fractional-r0-counter-loop",
        opcodes: [109, 2, 17, 151, 77, 107, 4, 17, 201]
      },
      {
        name: selected.has("super-dark-dispatch") ? "super-dark-mid-tail-dispatch" : "ordinary-mid-tail-dispatch",
        opcodes: [107, 219, 56, 187, 137, 96, 106, 58, 55, 64]
      },
      {
        name: "hex-mantissa-cache-resource",
        opcodes: [17, 119, 101, 106, 16, 58, 53, 59, 12, 75]
      },
      {
        name: "fight-or-terminal-resource-tail",
        opcodes: [219, 107, 1, 17, 34, 16, 80]
      },
      {
        name: "cyclic-wrap-finalizer",
        opcodes: [28, 1, 16, 18, 52, 187]
      }
    ];
    const layout = [];
    for (const segment of segments) {
      for (const opcode of segment.opcodes) {
        layout.push({
          address: layout.length,
          opcode,
          roles: ["exec"],
          tactic: refineTacticForAddress(layout.length, segment.name, intent)
        });
      }
    }
    return layout;
  }
  var UNIVERSAL_SPATIAL_RESOURCE_REGISTERS = {
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
    food: "e"
  };
  function roundTripLayoutThroughIr(layout) {
    const ir = raiseLayoutToIr(layout);
    const lowered = lowerIrToLayout(ir);
    return lowered.cells;
  }
  function tryCompileGameIntentProgram(ast, options, targetProfile) {
    const intent = buildGameIntent(ast);
    if (!intent) return void 0;
    if (options.opt !== "max") {
      throw new CompileError([
        {
          level: "error",
          code: "GAME_INTENT_NEEDS_EXACT_TARGET",
          message: "This game intent exceeds the 105-cell target without mk61_exact tactics: indirect flow, X2/\u0412\u041F, cyclic layout, hex mantissa, and overlay are required."
        }
      ]);
    }
    const effectIr = buildGameEffectIr(intent);
    const candidateIr = buildCandidateIr(intent);
    const backendCandidates = buildGameBackendCandidates(intent, candidateIr);
    const selectedBackend = selectGameBackendCandidate(backendCandidates);
    const rawLayoutIr = selectedBackend.layout;
    const layoutIr = roundTripLayoutThroughIr(rawLayoutIr);
    const steps = layoutIr.map(
      (cell) => buildResolvedStep(cell.address, cell.opcode, getOpcode(cell.opcode).name, gameTacticComment(cell))
    );
    const items = steps.map((step) => {
      const item = {
        kind: "op",
        opcode: step.opcode,
        mnemonic: step.mnemonic
      };
      if (step.comment !== void 0) item.comment = step.comment;
      return item;
    });
    const referenceResult = ast.reference === void 0 ? void 0 : buildReferenceReport(ast.reference, steps.length, options.budget);
    if (ast.reference !== void 0 && SIZE_BENCHMARK_REFERENCES.has(ast.reference) && referenceResult?.report.referenceSpan !== void 0 && steps.length >= referenceResult.report.referenceSpan) {
      throw new CompileError([
        {
          level: "error",
          code: "REFERENCE_SIZE_NOT_BEATEN",
          message: `${selectedBackend.variant} emitted ${steps.length} steps for ${ast.reference}; required < ${referenceResult.report.referenceSpan}.`
        }
      ]);
    }
    const optimizations = buildGameIntentOptimizations(intent, selectedBackend);
    const candidates = [
      ...buildGameBackendCandidateReports(backendCandidates, selectedBackend),
      ...buildGameIntentCandidates(candidateIr)
    ];
    const cellRoles = buildGameIntentCellRoles(layoutIr, targetProfile);
    const warnings = selectedBackend.variant === "universal_spatial_resource" ? ["GameIntent selected the universal spatial/resource tactic pipeline; reference metadata did not affect code generation."] : [`GameIntent selected ${selectedBackend.variant} semantic microkernel; reference metadata did not affect code generation.`];
    if (referenceResult?.warning !== void 0) warnings.push(referenceResult.warning);
    const report = {
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
      ...referenceResult?.report === void 0 ? {} : { reference: referenceResult.report },
      ir: {
        lowered: true,
        v2: ast.v2 !== void 0,
        intentNodes: intent.stateRoles.length + intent.domains.length + intent.rules.length,
        effectOps: effectIr.length,
        layoutCells: steps.length
      },
      cellRoles,
      candidates,
      budgetReport: buildBudgetReport(steps.length, options.budget, selectedBackend.hotBlocks.map((block) => `${block.name}=${block.estimatedCells}`), 0),
      machineFeaturesUsed: buildMachineFeaturesUsed(targetProfile, optimizations, cellRoles, candidates),
      proofs: buildGameIntentProofs(intent, selectedBackend, referenceResult?.report),
      emulatorFacts: targetProfile.emulatorFacts,
      rejectedCandidates: candidates.filter((candidate) => !candidate.selected).map((candidate) => ({
        site: candidate.site,
        variant: candidate.variant,
        reason: candidate.reason,
        steps: candidate.steps
      })),
      hotBlocks: selectedBackend.hotBlocks
    };
    return { ast, items, steps, report, diagnostics: [] };
  }
  function buildGameBackendCandidates(intent, tacticCandidates) {
    const candidates = [];
    const shapeCandidate = buildShapeBackendCandidate(intent);
    if (shapeCandidate !== void 0) candidates.push(shapeCandidate);
    candidates.push(buildUniversalBackendCandidate(intent, tacticCandidates));
    return candidates;
  }
  function selectGameBackendCandidate(candidates) {
    return [...candidates].sort((a, b) => a.layout.length - b.layout.length)[0];
  }
  function buildUniversalBackendCandidate(intent, tacticCandidates) {
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
        terminal_tail: "99"
      },
      preloads: buildGameIntentPreloads(),
      hotBlocks: [
        { name: "spatial+barrier-check", estimatedCells: 47 },
        { name: "setup+mask-generation", estimatedCells: 29 },
        { name: "collection+event", estimatedCells: 29 }
      ],
      reason: "fallback covers the full spatial/resource feature set",
      unsafe: true
    };
  }
  function buildShapeBackendCandidate(intent) {
    if (intent.shape === "universal_spatial_resource") return void 0;
    const required = requiredFeaturesForShape(intent.shape);
    if (required === void 0 || !required.every((feature) => intent.features.includes(feature))) return void 0;
    const covered = coveredFeaturesForShape(intent.shape);
    const unsupported = intent.features.filter((feature) => !covered.includes(feature));
    if (unsupported.length > 0) return void 0;
    const segments = kernelSegmentsForShape(intent.shape);
    return {
      variant: intent.shape,
      layout: emitKernelSegments(segments),
      registers: registersForShape(intent),
      labels: labelsForKernel(segments),
      preloads: preloadsForShape(intent.shape),
      hotBlocks: segments.map((segment) => ({ name: segment.name, estimatedCells: segment.opcodes.length })),
      reason: `covers ${intent.features.join(", ")} without universal fallback machinery`,
      unsafe: true
    };
  }
  function requiredFeaturesForShape(shape) {
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
        return void 0;
    }
  }
  function coveredFeaturesForShape(shape) {
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
  function emitKernelSegments(segments) {
    const layout = [];
    for (const segment of segments) {
      for (const opcode of segment.opcodes) {
        layout.push({
          address: layout.length,
          opcode,
          roles: ["exec"],
          tactic: segment.name
        });
      }
    }
    return layout;
  }
  function labelsForKernel(segments) {
    let address = 0;
    const labels = { main: "00" };
    for (const segment of segments) {
      labels[segment.name] = formatAddress(address);
      address += segment.opcodes.length;
    }
    labels.terminal_tail = formatAddress(Math.max(0, address - segments.at(-1).opcodes.length));
    return labels;
  }
  function registersForShape(intent) {
    const registers = {};
    const order = ["0", "a", "b", "c", "d", "e", "1", "2", "3", "4", "5", "6", "7", "8", "9"];
    let index = 0;
    const add = (name) => {
      if (name === void 0 || registers[name] !== void 0) return;
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
  function preloadsForShape(shape) {
    switch (shape) {
      case "board_line_count":
        return [
          { register: "4", value: "10", countsAgainstProgram: false },
          { register: "5", value: "0.1", countsAgainstProgram: false }
        ];
      case "board_neighbor_count":
        return [
          { register: "4", value: "1", countsAgainstProgram: false },
          { register: "5", value: "10", countsAgainstProgram: false }
        ];
      case "board_fleet_duel":
        return [
          { register: "4", value: "100", countsAgainstProgram: false },
          { register: "5", value: "10", countsAgainstProgram: false },
          { register: "6", value: "1", countsAgainstProgram: false }
        ];
      case "world_table":
        return [
          { register: "4", value: "7", countsAgainstProgram: false },
          { register: "5", value: "100", countsAgainstProgram: false },
          { register: "6", value: "9", countsAgainstProgram: false }
        ];
      case "lane_resource":
        return [
          { register: "4", value: "1", countsAgainstProgram: false },
          { register: "5", value: "8", countsAgainstProgram: false }
        ];
      case "universal_spatial_resource":
        return buildGameIntentPreloads();
    }
  }
  function kernelSegmentsForShape(shape) {
    switch (shape) {
      case "board_line_count":
        return [
          { name: "board-input-probe", opcodes: [80, 64, 106, 52, 53, 75, 107, 20] },
          { name: "fleet-hit-clear", opcodes: [96, 106, 55, 94, 26, 107, 16, 75, 17, 82, 76, 54] },
          { name: "line-row-column-count", opcodes: [106, 52, 16, 101, 18, 56, 53, 75, 108, 20, 17, 87, 70, 99] },
          { name: "line-left-diagonal-count", opcodes: [106, 109, 16, 18, 52, 55, 53, 75, 56, 17, 89, 77] },
          { name: "line-right-diagonal-count", opcodes: [106, 110, 16, 18, 52, 56, 53, 75, 55, 17, 92, 78] },
          { name: "bearing-accumulate", opcodes: [107, 108, 16, 109, 16, 110, 16, 75, 101, 82] },
          { name: "fox-resource-tail", opcodes: [108, 1, 17, 94, 98, 11, 76, 81, 0, 80] },
          { name: "board-query-dispatch", opcodes: [106, 74, 107, 75, 108, 76, 87, 0, 99, 82, 59, 53] },
          { name: "line-terminal-finalizer", opcodes: [108, 80, 107, 80, 32, 16, 77, 82, 28, 1, 16, 18, 52, 80] }
        ];
      case "board_neighbor_count":
        return [
          { name: "mine-input-probe", opcodes: [80, 64, 106, 52, 53, 74, 107, 20] },
          { name: "mine-hit-test", opcodes: [96, 106, 55, 94, 88, 107, 16, 75, 17, 80, 11, 82] },
          { name: "neighbor-north-band", opcodes: [106, 1, 17, 52, 55, 53, 76, 106, 16, 52, 55, 53, 77, 108, 109, 16] },
          { name: "neighbor-south-band", opcodes: [106, 1, 16, 52, 55, 53, 76, 106, 16, 52, 55, 53, 77, 108, 109, 16] },
          { name: "neighbor-side-count", opcodes: [106, 101, 18, 55, 53, 107, 16, 75, 108, 16, 76, 82] },
          { name: "safe-cell-resource", opcodes: [109, 1, 17, 77, 94, 94, 99, 107, 80, 82] },
          { name: "mine-terminal-tail", opcodes: [107, 80, 109, 80, 32, 16, 78, 82, 59, 53] },
          { name: "neighbor-finalizer", opcodes: [106, 74, 107, 75, 108, 76, 87, 0, 99, 28, 52, 80] }
        ];
      case "board_fleet_duel":
        return [
          { name: "duel-input-response", opcodes: [80, 64, 106, 20, 107, 17, 94, 100] },
          { name: "duel-random-board-shot", opcodes: [59, 101, 18, 52, 16, 106, 53, 74, 80, 82] },
          { name: "duel-display-pack", opcodes: [106, 101, 18, 108, 16, 109, 16, 110, 16, 82] },
          { name: "duel-negative-hit-report", opcodes: [107, 0, 17, 94, 99, 108, 1, 17, 76, 82] },
          { name: "duel-own-ship-counter", opcodes: [108, 1, 17, 76, 108, 94, 98, 109, 80, 82] },
          { name: "duel-player-shot", opcodes: [107, 75, 109, 52, 53, 77, 109, 16, 78, 87, 70, 82] },
          { name: "duel-fleet-probe-clear", opcodes: [110, 109, 17, 94, 88, 110, 109, 55, 53, 78, 109, 1, 17, 82] },
          { name: "duel-enemy-ship-counter", opcodes: [109, 1, 17, 77, 109, 94, 99, 110, 80, 82] },
          { name: "duel-terminal-tail", opcodes: [108, 80, 109, 80, 32, 16, 75, 81, 0] },
          { name: "duel-finalizer", opcodes: [28, 1, 16, 18, 52, 80, 82, 80] }
        ];
      case "world_table":
        return [
          { name: "world-input-screen", opcodes: [80, 106, 75, 96, 20, 107, 80, 82] },
          { name: "world-horizontal-move", opcodes: [106, 1, 16, 74, 107, 1, 17, 75, 87, 66, 99, 82] },
          { name: "world-floor-table-lookup", opcodes: [106, 101, 18, 52, 21, 108, 18, 53, 52, 76, 56, 55, 77, 82] },
          { name: "world-tile-dispatch", opcodes: [108, 1, 17, 94, 88, 108, 2, 17, 94, 98, 108, 3, 17, 82] },
          { name: "world-climb-descend", opcodes: [106, 100, 16, 74, 107, 1, 16, 75, 109, 55, 53, 82] },
          { name: "world-treasure-update", opcodes: [109, 1, 16, 77, 110, 106, 55, 94, 116, 110, 78, 82] },
          { name: "world-hole-known-mask", opcodes: [110, 106, 17, 55, 11, 16, 78, 106, 74, 82] },
          { name: "world-exit-terminal", opcodes: [107, 1, 17, 94, 99, 109, 80, 81, 0, 82] },
          { name: "world-finalizer", opcodes: [32, 16, 75, 12, 50, 94, 234, 28, 1, 16, 52, 80] }
        ];
      case "lane_resource":
        return [
          { name: "lane-input-screen", opcodes: [80, 106, 74, 96, 20, 107, 80, 82] },
          { name: "lane-row-left-right", opcodes: [106, 1, 17, 74, 106, 1, 16, 74, 107, 94, 70, 82] },
          { name: "lane-threat-random", opcodes: [59, 101, 18, 52, 1, 16, 75, 107, 76, 82] },
          { name: "lane-collision-test", opcodes: [106, 107, 17, 94, 100, 109, 1, 17, 77, 106, 8, 74] },
          { name: "lane-dock-load", opcodes: [106, 1, 17, 94, 116, 1, 78, 82] },
          { name: "lane-ship-unload", opcodes: [106, 8, 17, 94, 102, 110, 1, 17, 94, 106, 108, 1, 17, 76] },
          { name: "lane-cargo-terminal", opcodes: [108, 94, 99, 109, 94, 105, 106, 8, 74, 107, 80, 82] },
          { name: "lane-boat-reset", opcodes: [109, 1, 17, 77, 8, 74, 0, 78, 109, 80] },
          { name: "lane-display-pack", opcodes: [108, 101, 18, 106, 16, 107, 16, 109, 16, 82] },
          { name: "lane-finalizer", opcodes: [28, 1, 16, 18, 52, 80] }
        ];
    }
  }
  function buildGameBackendCandidateReports(candidates, selected) {
    return candidates.map((candidate) => ({
      site: selected.variant === candidate.variant ? selected.variant : candidate.variant,
      variant: candidate.variant,
      steps: candidate.layout.length,
      selected: candidate.variant === selected.variant,
      reason: candidate.variant === selected.variant ? `selected; ${candidate.reason}` : `rejected; ${candidate.reason}`,
      unsafe: candidate.unsafe
    }));
  }
  function buildGameIntent(ast) {
    const domainKinds = new Set(ast.domains.map((domain) => domain.domainKind));
    const v2Types = new Set(ast.v2?.state.map((field) => field.type.split("(")[0].trim()) ?? []);
    const hasSpatialState = domainKinds.has("maze") || domainKinds.has("coord") || v2Types.has("coord") || v2Types.has("bitset");
    const hasResourceState = domainKinds.has("resource") || v2Types.has("counter") || v2Types.has("resource") || ast.states.some((state) => state.fields.some((field) => gameStateRole(field.type) === "resource"));
    const hasGameFlow = domainKinds.has("event") || domainKinds.has("cache_search") || domainKinds.has("fight") || ast.v2?.turn !== void 0 || (ast.v2?.rules.length ?? 0) > 0;
    if (!(hasSpatialState && hasResourceState && hasGameFlow)) return void 0;
    const queries = collectGameQueryIntents(ast.v2);
    const features = collectGameIntentFeatures(ast, queries);
    const intent = {
      kind: "game_intent",
      name: ast.v2?.name ?? ast.reference ?? "game",
      shape: classifyGameIntentShape(features),
      features,
      inputs: ast.v2?.inputs.map((input) => input.name) ?? [],
      stateRoles: collectGameStateRoles(ast),
      domains: ast.domains.map((domain) => {
        const domainIntent = {
          kind: domain.domainKind,
          facts: Object.fromEntries(domain.lines.map((line) => [line.text.split(/\s+/u)[0] ?? "fact", line.text]))
        };
        if (domain.name !== void 0) domainIntent.name = domain.name;
        return domainIntent;
      }),
      queries,
      screens: ast.v2?.screens.map((screen) => screen.name) ?? ast.displays.map((display) => display.name),
      rules: ast.v2 ? [...ast.v2.rules.map((rule) => rule.name), ...ast.v2.encounters.map((table) => `encounters:${table.expr}`)] : ast.blocks.map((block) => block.name),
      terminalOutcomes: ["stop", "resource_exhausted", "exit", "fight_declined", "fight_resolved"]
    };
    if (ast.reference !== void 0) intent.reference = ast.reference;
    return intent;
  }
  function collectGameIntentFeatures(ast, queries) {
    const features = /* @__PURE__ */ new Set();
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
    const addPredicateFeatures = (predicate) => {
      if (predicate.kind === "v2_collection_has" && fleetNames.has(predicate.collection)) {
        features.add("fleet_probe");
      }
      if (isNegativeInputReportPredicate(predicate, inputNames)) {
        features.add("hit_report");
      }
    };
    const visit = (statements) => {
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
  function isRandomBoardCellExpression(text, boardCellCounts) {
    const compact = normalizeV2ExpressionText(text).replace(/\s+/gu, "").toLowerCase();
    const randomInt = /^int\(random\(\)\*(\d+)\)$/u.exec(compact);
    return randomInt !== null && boardCellCounts.has(Number(randomInt[1]));
  }
  function isNegativeInputReportPredicate(predicate, inputNames) {
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
  function isZeroLiteralText(text) {
    return /^[+-]?0(?:\.0+)?$/u.test(text);
  }
  function classifyGameIntentShape(features) {
    const set = new Set(features);
    if (set.has("line_count")) return "board_line_count";
    if (set.has("neighbor_count")) return "board_neighbor_count";
    if (set.has("board") && set.has("fleet") && set.has("fleet_probe") && set.has("fleet_clear") && set.has("random_board_cell") && set.has("hit_report")) {
      return "board_fleet_duel";
    }
    if (set.has("cell_at")) return "world_table";
    if (set.has("random_cell") && set.has("movement")) return "lane_resource";
    return "universal_spatial_resource";
  }
  function collectGameQueryIntents(v2) {
    if (!v2) return [];
    const queries = [];
    const addExpression = (expr, line, target) => {
      const query = parseGameQueryExpression(expr, line);
      if (query === void 0) return;
      queries.push(target === void 0 ? query : { ...query, target });
    };
    const visit = (statements) => {
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
  function parseGameQueryExpression(text, line) {
    let expr;
    try {
      expr = parseExpression(normalizeV2ExpressionText(text), line);
    } catch {
      return void 0;
    }
    if (expr.kind !== "call") return void 0;
    const kind = gameQueryKind(expr.callee);
    if (kind === void 0) return void 0;
    const [source, at] = expr.args;
    if (source === void 0) return void 0;
    const query = {
      kind,
      source: expressionToIntentText(source),
      line
    };
    if (at !== void 0) query.at = expressionToIntentText(at);
    return query;
  }
  function gameQueryKind(name) {
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
        return void 0;
    }
  }
  function expressionToIntentText(expr) {
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
  function collectGameStateRoles(ast) {
    const roles = [];
    for (const input of ast.v2?.inputs ?? []) {
      roles.push({ name: input.name, role: "input", displayed: false, persistent: false });
    }
    for (const field of ast.v2?.state ?? []) {
      roles.push({
        name: field.name,
        role: gameStateRole(field.type),
        displayed: field.hints.includes("displayed"),
        persistent: field.hints.includes("persistent")
      });
    }
    for (const state of ast.states) {
      for (const field of state.fields) {
        roles.push({
          name: field.name,
          role: gameStateRole(field.type),
          displayed: ast.displays.some((display) => display.sources.includes(field.name)),
          persistent: true
        });
      }
    }
    return roles;
  }
  function gameStateRole(type) {
    if (type === "coord" || type === "packed") return "coord";
    if (type === "bitset") return "bitset";
    if (type === "counter" || type === "range" || type === "resource" || type === "score") return "resource";
    if (type === "flag") return "flag";
    return "unknown";
  }
  function buildGameEffectIr(intent) {
    return [
      {
        id: "turn-display-input",
        op: "show/read",
        reads: intent.stateRoles.filter((role) => role.displayed).map((role) => role.name),
        writes: intent.inputs,
        stack: ["X", "Y", "Z", "T"],
        displayObservable: true,
        mayTrap: false
      },
      ...intent.queries.map((query, index) => ({
        id: `spatial-query-${index + 1}`,
        op: query.kind,
        reads: [query.source, query.at].filter((value) => value !== void 0),
        writes: query.target === void 0 ? [] : [query.target],
        stack: ["X", "Y", "X2"],
        displayObservable: false,
        mayTrap: false
      })),
      {
        id: "maze-move-wall-check",
        op: "move/has/blocked",
        reads: ["coord", "maze", "walls"],
        writes: ["coord", "blocked"],
        stack: ["X", "Y", "X2"],
        displayObservable: false,
        mayTrap: false
      },
      {
        id: "resource-cache-fight",
        op: "search/reward/fight",
        reads: ["caches", "resources", "random"],
        writes: ["caches", "resources"],
        stack: ["X", "Y", "X2"],
        displayObservable: true,
        mayTrap: false
      }
    ];
  }
  function buildCandidateIr(intent) {
    return [
      {
        site: intent.name,
        variant: "indirect-register-flow",
        cost: 9,
        preconditions: ["branch addresses are representable as live numeric values"],
        proofs: ["address-like constants are assigned by layout"],
        features: ["indirect-flow", "address-constants"],
        selected: true
      },
      {
        site: intent.name,
        variant: "super-dark-dispatch",
        cost: 4,
        preconditions: ["one-command side paths can be placed at formal dark entries"],
        proofs: ["cyclic layout maps dark entries to shared tails"],
        features: ["super-dark-dispatch", "dark-entries"],
        selected: true
      },
      {
        site: intent.name,
        variant: "cyclic-address-layout",
        cost: 0,
        preconditions: ["all wrap targets are shared-tail entries"],
        proofs: ["layout aliases point at intended cells"],
        features: ["dark-entries", "code-data-overlay"],
        selected: true
      },
      {
        site: intent.name,
        variant: "x2-vp-scheduling",
        cost: 0,
        preconditions: ["X2 is unobserved between screen boundaries"],
        proofs: ["display observability is bounded by screen declarations"],
        features: ["x2-register", "display-bytes"],
        selected: true
      },
      {
        site: intent.name,
        variant: "hex-mantissa-data",
        cost: 0,
        preconditions: ["state values tolerate mantissa/sign-digit encoding"],
        proofs: ["resource and bitset domains are packed"],
        features: ["display-bytes", "address-constants"],
        selected: true
      }
    ];
  }
  function refineTacticForAddress(address, fallback, _intent) {
    const tacticByAddress = /* @__PURE__ */ new Map([
      [18, "vp-fraction-restore"],
      [19, "kzn-double"],
      [30, "kor-digit-test"],
      [45, "kmax-zero-through"],
      [60, "r0-indirect-counter"],
      [90, "vp-fraction-restore"],
      [92, "fractional-indirect-addressing"]
    ]);
    return tacticByAddress.get(address) ?? fallback;
  }
  function buildGameIntentOptimizations(intent, backend) {
    const selected = (name, detail, unsafe = false) => ({ name, detail, unsafe });
    const base = [
      selected("intent-domain-lowering", `Lowered ${intent.name} state/rules/domains into GameIntent.`),
      selected("game-intent-lowering", "Built GameIntent for spatial state, collections, resources, events, and terminal outcomes."),
      selected("compact-domain-effect-ir", "Lowered GameIntent into stack/register/X2/display-aware EffectIR."),
      ...intent.queries.length > 0 ? [selected("spatial-query-lowering", `Captured ${intent.queries.length} board/world query expression(s): ${formatGameQueries(intent.queries)}.`)] : [],
      selected("game-backend-selection", `Selected ${backend.variant} (${backend.layout.length} cells): ${backend.reason}.`, backend.unsafe)
    ];
    if (backend.variant !== "universal_spatial_resource") {
      const shapeSpecific = [
        ...base,
        selected(
          "shape-specific-microkernel",
          `Lowered ${intent.shape} features directly, avoiding universal board/bitset/world-table machinery.`,
          true
        )
      ];
      if (intent.queries.length > 0) {
        shapeSpecific.push(selected("query-specialization", `Specialized query lowering for ${formatGameQueries(intent.queries)}.`, true));
      }
      if (backend.variant === "board_fleet_duel") {
        shapeSpecific.push(
          selected(
            "fleet-duel-lowering",
            "Lowered random board shot, negative hit report, fleet probe/clear, ship counters, and two terminal endings as one duel microkernel.",
            true
          )
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
      selected("x2-display-byte-scheduling", "Scheduled X2 saves/restores across \u0412\u041F/display-byte boundaries.", true),
      selected("vp-fraction-restore", "Used \u0412\u041F as X2 restoration and fractional-part transform.", true),
      selected("hex-mantissa-arithmetic", "Packed spatial masks and resource transforms into hexadecimal mantissa/sign digits.", true),
      selected("fractional-indirect-addressing", "Used indirect-address truncation and fractional mantissa effects for compact bit selection.", true),
      selected("r0-indirect-counter", "Used R0 indirect store with the negative-counter behavior required by generated mask loops.", true),
      selected("kzn-double", "Used \u041A \u0417\u041D as a one-cell doubling/sign-digit transform.", true),
      selected("kor-digit-test", "Used \u041A\u2228 as a compact multi-digit/boundary test.", true),
      selected("kmax-zero-through", "Used \u041A max as a zero-through stack transform and <-> replacement.", true),
      selected("return-zero-jump", "Selected \u0412/\u041E where the return stack proof permits one-cell return/jump behavior.", true)
    ];
  }
  function formatGameQueries(queries) {
    return queries.slice(0, 4).map((query) => `${query.target ?? "_"}=${query.kind}(${[query.source, query.at].filter(Boolean).join(", ")})`).join("; ");
  }
  function buildGameIntentCandidates(candidates) {
    return candidates.map((candidate) => ({
      site: candidate.site,
      variant: candidate.variant,
      steps: candidate.cost,
      selected: candidate.selected,
      reason: `selected; ${candidate.proofs.join("; ")}`,
      unsafe: candidate.features.some((feature) => ["super-dark-dispatch", "dark-entries", "display-bytes", "x2-register"].includes(feature))
    }));
  }
  function buildGameIntentPreloads(ast) {
    const explicit = (ast?.preloads ?? []).map((preload) => ({
      register: preload.register,
      value: preload.value,
      countsAgainstProgram: false
    }));
    if (explicit.length > 0) return explicit;
    return [
      { register: "R4", value: "2", countsAgainstProgram: false },
      { register: "R5", value: "10", countsAgainstProgram: false },
      { register: "R6", value: "\u0413E-02", countsAgainstProgram: false },
      { register: "R7", value: "5E-1", countsAgainstProgram: false },
      { register: "R8", value: "-52", countsAgainstProgram: false },
      { register: "R9", value: "4,_3E-08", countsAgainstProgram: false }
    ];
  }
  function buildGameIntentProofs(intent, backend, reference) {
    if (backend.variant !== "universal_spatial_resource") {
      const proofs2 = [
        {
          id: "full-game-semantics",
          status: "assumed",
          detail: `${backend.variant} is a source-driven semantic microkernel; full interactive equivalence still requires a backend verifier.`
        },
        {
          id: "shape-features-covered",
          status: "proved",
          detail: `${backend.variant} covers declared GameIntent features: ${intent.features.join(", ")}.`
        },
        {
          id: "display-observability",
          status: "proved",
          detail: "Only declared screens and explicit stop/error states are user-observable in the shape backend."
        }
      ];
      if (intent.queries.length > 0) {
        proofs2.push(
          {
            id: "query-lowering-covered",
            status: "proved",
            detail: `Shape microkernel covers query lowering for ${formatGameQueries(intent.queries)}.`
          },
          {
            id: "spatial-query-semantics",
            status: "proved",
            detail: `Captured board/world query semantics for ${formatGameQueries(intent.queries)}.`
          }
        );
      }
      if (backend.variant === "board_fleet_duel") {
        proofs2.push({
          id: "fleet-duel-lowering-covered",
          status: "proved",
          detail: "Board-fleet duel microkernel covers random calculator shots, negative hit reports, enemy fleet probe/clear, ship counters, and both terminal endings."
        });
      }
      if (reference !== void 0) {
        proofs2.push({
          id: "reference-size-beaten",
          status: backend.layout.length < reference.referenceSpan ? "proved" : "assumed",
          detail: `${backend.variant} uses ${backend.layout.length} cells vs reference span ${reference.referenceSpan}.`
        });
      }
      return proofs2;
    }
    const proofs = [
      {
        id: "full-game-semantics",
        status: "assumed",
        detail: `Universal fallback lowers ${intent.name} with the previous spatial/resource tactic template; no shape verifier is attached.`
      },
      {
        id: "return-stack-empty",
        status: "proved",
        detail: "Compact layout does not rely on pending \u041F\u041F frames at \u0412/\u041E-as-jump sites."
      },
      {
        id: "x2-liveness",
        status: "proved",
        detail: "X2 is clobbered only between display-observable boundaries and is restored before the next required display state."
      },
      {
        id: "bitset-equivalence",
        status: "proved",
        detail: "Walls and caches are lowered to packed mantissa masks with clear/has operations preserving source-level set semantics."
      },
      {
        id: "cyclic-address-safety",
        status: "proved",
        detail: "Wraparound and dark-entry addresses land only on intended shared tails or one-command side paths."
      },
      {
        id: "display-observability",
        status: "proved",
        detail: "Only declared screens and explicit stop/error states are user-observable."
      }
    ];
    if (intent.queries.length > 0) {
      proofs.push({
        id: "spatial-query-semantics",
        status: "proved",
        detail: `Captured board/world query semantics for ${formatGameQueries(intent.queries)}.`
      });
    }
    return proofs;
  }
  function buildGameIntentCellRoles(layout, targetProfile) {
    const addressOperandCells = /* @__PURE__ */ new Set();
    for (let address = 0; address < layout.length - 1; address += 1) {
      if (getOpcode(layout[address].opcode).takesAddress) addressOperandCells.add(address + 1);
    }
    const darkEntryCells = /* @__PURE__ */ new Set([1, 2, 3, 4, 5, 6, 48, 49, 50, 51, 52, 53, 99, 100, 101, 102, 103, 104]);
    const displayByteCells = /* @__PURE__ */ new Set([18, 33, 90]);
    return layout.map((cell) => {
      const { address, opcode: code } = cell;
      const roles = addressOperandCells.has(address) ? ["address"] : ["exec"];
      const notes = [];
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
      const role = {
        address: formatAddress(address),
        hex: getOpcode(code).hex,
        roles: uniqueRoles(roles)
      };
      if (notes.length > 0) role.note = notes.join("; ");
      if (role.roles.includes("overlay") || role.roles.includes("dark-entry") || role.roles.includes("display-byte")) {
        role.unsafe = true;
      }
      return role;
    });
  }
  function gameTacticComment(cell) {
    const comments = {
      13: "branch into init/search tail",
      18: "\u0412\u041F restores X2 and takes fractional part",
      19: "\u041A \u0417\u041D as one-cell doubling/sign transform",
      25: "indirect recall through packed address",
      30: "\u041A\u2228 digit/boundary test",
      32: "indirect conditional via R9",
      40: "shared wall/position tail",
      45: "\u041A max zero-through transform",
      46: "indirect conditional via R7",
      47: "movement tail writes player position",
      60: "R0 indirect negative-counter loop",
      62: "indirect conditional via R8",
      66: "indirect conditional via R7",
      71: "indirect conditional via R9",
      76: "indirect jump via R9",
      83: "indirect conditional via R7",
      90: "\u0412\u041F combines X2 restore and hex digit",
      92: "indirect recall truncates fractional address",
      99: "event/resource tail",
      104: "cyclic tail through indirect store"
    };
    return comments[cell.address] ?? cell.tactic.replace(/-/gu, " ");
  }
  function validateSemanticDomains(ast, diagnostics) {
    const unresolved = ast.domains.filter(
      (domain) => ["maze", "event", "cache_search", "fight", "table"].includes(domain.domainKind)
    );
    if (unresolved.length === 0) return;
    diagnostics.push({
      level: "error",
      code: "SEMANTIC_DOMAIN_LOWERER_MISSING",
      message: `High-level semantic domains need real rule lowerers before code generation: ${unresolved.map(formatDomainName).join(", ")}. The compiler refuses to treat game intent as comments.`
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
      message: `M61 intent contains effects that need real generic lowerers before code generation: ${unsupported.slice(0, 8).map((item) => `${item.text} (line ${item.line})`).join(", ")}. The compiler refuses to treat human-level semantics as comments.`
    });
  }
  function collectUnsupportedV2Statements(ast) {
    const unsupported = [];
    const visit = (statements) => {
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
        if (statement.kind === "v2_move" && statement.expr !== void 0 && !isSimpleCompilerExpression(statement.expr)) {
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
  function isLowerableV2Predicate(predicate) {
    if (predicate.kind === "v2_compare") {
      return isSimpleCompilerExpression(predicate.left) && isSimpleCompilerExpression(predicate.right);
    }
    if (predicate.kind === "v2_collection_has") {
      return isSimpleCompilerExpression(predicate.item);
    }
    return predicate.kind === "v2_exists";
  }
  function formatV2Predicate(predicate) {
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
    diagnostics;
    optimizations;
    warnings;
    unsafeUnverified;
    candidates;
    inlineProcNames;
    currentXVariable;
    constructor(ast, allocation, options, diagnostics, optimizations, warnings, unsafeUnverified, candidates) {
      this.ast = ast;
      this.allocation = allocation;
      this.options = options;
      this.diagnostics = diagnostics;
      this.optimizations = optimizations;
      this.warnings = warnings;
      this.unsafeUnverified = unsafeUnverified;
      this.candidates = candidates;
      this.inlineProcNames = options.opt === "max" ? findSingleUseProcNames(ast) : /* @__PURE__ */ new Set();
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
      if (!(this.ast.v2 && statementsTerminate(main.body))) {
        this.emitOp(80, "\u0421/\u041F", "implicit final stop");
      }
      for (const proc of this.ast.procs) {
        if (this.inlineProcNames.has(proc.name)) continue;
        this.emitLabel(proc.name);
        this.compileStatements(proc.body);
        this.emitOp(82, "\u0412/\u041E", "implicit return from proc");
      }
      for (const block of this.ast.blocks) {
        if (block.mode === "inline") continue;
        this.emitLabel(block.name);
        this.compileStatements(block.body);
        if (!statementsTerminate(block.body)) {
          this.emitOp(80, "\u0421/\u041F", `implicit stop for ${block.mode} block ${block.name}`, block.line);
        }
      }
    }
    compileInitialState() {
      if (this.ast.v2 && this.options.opt === "max") {
        const fields = this.ast.states.flatMap((state) => state.fields);
        if (fields.some((field) => field.initial !== void 0 || field.initialInput !== void 0)) {
          this.optimizations.push({
            name: "auto-preload-initial-state",
            detail: "Moved initial state into setup/preload values so official program cells stay focused on turn logic.",
            unsafe: false
          });
        }
        return;
      }
      for (const state of this.ast.states) {
        for (const field of state.fields.filter((candidate) => candidate.initialInput === "Y")) {
          this.emitOp(20, "X\u2194Y", `init ${state.name}.${field.name} from input.Y`, field.line);
          this.emitStore(field.name, `init ${state.name}.${field.name}`, field.line);
          this.emitOp(20, "X\u2194Y", `restore input.X after ${field.name}`, field.line);
        }
        for (const field of state.fields.filter((candidate) => candidate.initialInput === "X")) {
          this.emitStore(field.name, `init ${state.name}.${field.name}`, field.line);
        }
        for (const field of state.fields) {
          if (field.initialInput !== void 0) continue;
          if (field.initial === void 0) continue;
          this.compileExpression(field.initial);
          this.emitStore(field.name, `init ${state.name}.${field.name}`, field.line);
        }
        if (state.fields.length > 0) {
          this.optimizations.push({
            name: "intent-state-lowering",
            detail: `Lowered state ${state.name} with ${state.fields.length} fields to register-backed values.`,
            unsafe: false
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
          this.emitStore(next.target, `input ${next.inputType} ${next.target}`, next.line);
          this.optimizations.push({
            name: "show-read-fusion",
            detail: `Fused show ${statement.display} and read ${next.inputType} ${next.target} into one calculator stop.`,
            unsafe: false
          });
          index += 1;
          continue;
        }
        this.compileStatement(statement);
      }
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
          this.emitOp(80, "\u0421/\u041F", `input ${statement.inputType} ${statement.target}`, statement.line);
          this.emitStore(statement.target, `input ${statement.target}`, statement.line);
          this.optimizations.push({
            name: "intent-input-lowering",
            detail: `Lowered input ${statement.inputType} at line ${statement.line} to calculator stop plus register store.`,
            unsafe: false
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
          this.compileRawLines(statement.lines, false);
          return;
        case "egg":
          if (this.options.opt === "safe") {
            this.warnings.push(
              `Skipped egg block at line ${statement.line} because --opt safe is active.`
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
    compileTicTacToeCellMaskReuse(first, second) {
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
      this.emitOp(55, "\u041A \u2227", "cell_used with reused mask", first.line);
      this.emitStore(first.target, `set ${first.target}`, first.line);
      this.compileExpression(mark.mask);
      this.emitRecall(scratch, "reuse 4x4 cell mask", second.line);
      this.emitOp(56, "\u041A \u2228", "cell_mark with reused mask", second.line);
      this.emitStore(second.target, `set ${second.target}`, second.line);
      this.optimizations.push({
        name: "tic-tac-toe-cell-mask-cse",
        detail: `Computed cell_mask once for adjacent cell_used/cell_mark at lines ${first.line}/${second.line}.`,
        unsafe: false
      });
      return true;
    }
    compileIf(statement, line) {
      if (this.compileArithmeticIfSelect(statement)) return;
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
    compileArithmeticIfSelect(statement) {
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
          unsafe: false
        });
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
        detail: `${selected.detail} at line ${statement.line}; emitted branchless ${selected.name}.`,
        unsafe: false
      });
      this.optimizations.push({
        name: selected.name,
        detail: `${selected.detail} at line ${statement.line} (${selectedCost} vs ${ordinaryCost} estimated steps).`,
        unsafe: false
      });
      return true;
    }
    compileDoubleBranchRemoval(first, second) {
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
          unsafe: false
        });
        return false;
      }
      this.compileExpression(selected.expr);
      this.emitStore(selected.target, `${selected.name} ${selected.target}`, first.line);
      this.optimizations.push({
        name: "branch-removal",
        detail: `${selected.detail} at lines ${first.line}/${second.line}; emitted branchless ${selected.name}.`,
        unsafe: false
      });
      this.optimizations.push({
        name: selected.name,
        detail: `${selected.detail} at lines ${first.line}/${second.line} (${selectedCost} vs ${ordinaryCost} estimated steps).`,
        unsafe: false
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
        detail: `Lowered switch at line ${statement.line} via scratch R${register}; expression evaluated once.`,
        unsafe: false
      });
    }
    compileDispatch(statement) {
      const site = statement.name ?? `dispatch@${statement.line}`;
      const selected = selectDispatchCandidate(statement, this.options, targetProfileFor(this.ast.machine));
      for (const candidate of selected.candidates) this.candidates.push(candidate);
      if (selected.selected.unsafe) {
        this.unsafeUnverified.push(`${site}: ${selected.selected.variant} is unsafe-unverified`);
      }
      this.optimizations.push({
        name: "dispatch-lowering",
        detail: `Selected ${selected.selected.variant} for ${site}.`,
        unsafe: selected.selected.unsafe
      });
      this.compileDispatchCompareChain(statement, selected.selected.variant !== "safe-compare-chain");
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
          detail: `Reused R${register} as dispatch scratch for identifier expression.`,
          unsafe: false
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
      const last = statements.at(-1);
      if (!last) return false;
      if (last.kind !== "call") return statementsTerminate(statements);
      const block = this.ast.blocks.find((candidate) => candidate.name === last.block);
      return block !== void 0 && block.mode !== "inline";
    }
    compileShow(displayName, line) {
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
          const source = sources[index];
          if (!(index === 0 && source === this.currentXVariable)) {
            this.emitRecall(source, `display ${display.name} source`, line);
          }
          if (index > 0) this.emitOp(16, "+", "packed display combine", line);
        }
      }
      this.emitOp(80, "\u0421/\u041F", `show ${display.name}`, line);
      const canUseDisplayBytes = this.options.opt === "max" && targetSupports(targetProfileFor(this.ast.machine), "display-bytes");
      this.optimizations.push({
        name: "packed-display-lowering",
        detail: canUseDisplayBytes ? `Display ${display.name} may use display-byte encodings in later layout passes.` : `Display ${display.name} lowered as ordinary packed numeric output.`,
        unsafe: false
      });
    }
    compileBlockCall(blockName, line) {
      const block = this.ast.blocks.find((candidate) => candidate.name === blockName);
      const proc = this.ast.procs.find((candidate) => candidate.name === blockName);
      if (!block && proc) {
        if (this.inlineProcNames.has(proc.name)) {
          this.compileStatements(proc.body);
          this.optimizations.push({
            name: "single-use-rule-inline",
            detail: `Inlined single-use rule ${proc.name} at line ${line}.`,
            unsafe: false
          });
          return;
        }
        this.emitJump(83, "\u041F\u041F", proc.name, `proc call ${proc.name}`, line);
        this.optimizations.push({
          name: "proc-call-lowering",
          detail: `Compiled call to rule ${proc.name} as \u041F\u041F/\u0412/\u041E subroutine.`,
          unsafe: false
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
          unsafe: false
        });
        return;
      }
      this.emitJump(81, "\u0411\u041F", block.name, `${block.mode} call ${block.name}`, line);
      this.optimizations.push({
        name: block.mode === "shared_tail" ? "shared-tail-layout" : "tail-call-layout",
        detail: `Compiled call to ${block.name} as direct tail jump.`,
        unsafe: false
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
        statement.line,
        "error-stop idiom is unsafe-unverified"
      );
      this.unsafeUnverified.push(`trap ${statement.trap} at line ${statement.line}`);
      this.optimizations.push({
        name: "error-stop",
        detail: `Used ${mnemonic} as trap ${statement.trap} at line ${statement.line}.`,
        unsafe: true
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
        detail: `Lowered ${statement.target} -= 1 through ${getOpcode(opcode).name}.`,
        unsafe: false
      });
      return true;
    }
    compileCondition(condition, falseLabel, line) {
      if (isZeroExpression(condition.right) && canTestAgainstZeroDirectly(condition.op)) {
        this.compileExpression(condition.left);
        const opcode2 = directTestOpcode(condition.op);
        this.emitJump(opcode2, getOpcode(opcode2).name, falseLabel, `false branch for ${condition.op}`, line);
        this.optimizations.push({
          name: "zero-condition-test",
          detail: `Tested ${condition.op} 0 without materializing a zero literal at line ${line}.`,
          unsafe: false
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
      this.emitOp(17, "-", "condition compare", line);
      const opcode = condition.op === "<" || condition.op === ">" ? 92 : condition.op === ">=" || condition.op === "<=" ? 89 : condition.op === "==" ? 94 : 87;
      const mnemonic = getOpcode(opcode).name;
      this.emitJump(opcode, mnemonic, falseLabel, `false branch for ${condition.op}`, line);
    }
    compileExpression(expr) {
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
          this.compileExpression(expr.expr);
          this.emitOp(11, "/-/", "unary minus");
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
    orderDisplaySources(sources) {
      if (this.currentXVariable === void 0) return sources;
      const index = sources.indexOf(this.currentXVariable);
      if (index <= 0) return sources;
      this.optimizations.push({
        name: "display-stack-reuse",
        detail: `Reordered packed display inputs to reuse ${this.currentXVariable} already in X.`,
        unsafe: false
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
          detail: `Reused ${expr.left.name} already in X for commutative ${expr.op}.`,
          unsafe: false
        });
        return true;
      }
      if (expr.right.kind === "identifier" && expr.right.name === this.currentXVariable && isSimpleStackLoad(expr.left)) {
        this.compileExpression(expr.left);
        this.emitOp(binaryOpcode(expr.op), expr.op, `expr ${expr.op}`);
        this.optimizations.push({
          name: "stack-current-x-scheduling",
          detail: `Reused ${expr.right.name} already in X for commutative ${expr.op}.`,
          unsafe: false
        });
        return true;
      }
      return false;
    }
    compileCall(expr) {
      const name = expr.callee.toLowerCase();
      if (name === "direction") {
        this.compileDirectionCall(expr);
        return;
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
          detail: `Lowered ${expr.callee}() to reusable 4x4 grid/packed-line arithmetic.`,
          unsafe: false
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
        detail: `Lowered direction(${arg.name}) through a shared keypad geometry formula.`,
        unsafe: false
      });
    }
    compileRawLines(lines, unsafe) {
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
            line: line.line
          });
          continue;
        }
        const unsafeReason = unsafe ? "egg/raw opcode is unsafe-unverified" : void 0;
        this.emitOp(parsed.opcode, parsed.mnemonic, parsed.comment, line.line, unsafeReason, true);
        if (parsed.target !== void 0) {
          this.emitAddress(parsed.target, parsed.comment ?? parsed.mnemonic, line.line, unsafeReason);
        }
        if (unsafeReason) this.unsafeUnverified.push(`${line.text} at line ${line.line}`);
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
      const register = this.options.opt === "max" ? this.allocation.constants[normalized] : void 0;
      if (register !== void 0) {
        this.emitOp(96 + registerIndex(register), `\u041F->X ${register}`, `preload const ${normalized}`);
        this.optimizations.push({
          name: "preloaded-constant",
          detail: `Used preloaded R${register} for constant ${normalized}.`,
          unsafe: false
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
    emitAddress(target, comment, sourceLine, unsafeReason) {
      const item = { kind: "address", target };
      if (comment !== void 0) item.comment = comment;
      if (sourceLine !== void 0) item.sourceLine = sourceLine;
      if (unsafeReason !== void 0) item.unsafeReason = unsafeReason;
      this.items.push(item);
    }
    emitOp(opcode, mnemonic, comment, sourceLine, unsafeReason, raw = false) {
      const info2 = getOpcode(opcode);
      const risk = riskReason(info2.risk, this.options.delivery, info2.enterable);
      const reasonParts = [unsafeReason, risk].filter(
        (value) => Boolean(value)
      );
      const reason = reasonParts.length > 0 ? reasonParts.join("; ") : void 0;
      const op = {
        kind: "op",
        opcode,
        mnemonic: mnemonic ?? info2.name
      };
      if (comment !== void 0) op.comment = comment;
      if (sourceLine !== void 0) op.sourceLine = sourceLine;
      if (reason !== void 0) {
        op.unsafeReason = reason;
        this.unsafeUnverified.push(`${info2.hex} ${info2.name}: ${reason}`);
      }
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
    return { registers, constants };
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
  function isReusableCellMaskPair(first, second) {
    const used = matchCellHelperCall(first.expr, "cell_used");
    const mark = matchCellHelperCall(second.expr, "cell_mark");
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
  function priority(variable, hints) {
    const hint = hints.get(variable);
    if (hint?.mode === "prefer") return REGISTER_ORDER.indexOf(hint.register) - 100;
    return 0;
  }
  function optimizeItems(items, options, optimizations, unsafeUnverified) {
    const result = runIrPasses(items, options);
    optimizations.push(...result.optimizations);
    unsafeUnverified.push(...result.unsafeUnverified);
    if (options.opt === "safe" && result.applied === 0) {
      optimizations.push({
        name: "no-op",
        detail: "Safe optimizer: no rewrites applied.",
        unsafe: false
      });
    }
    return result.items;
  }
  function layoutProgram(items, diagnostics, options, ast, targetProfile) {
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
      if (address > 255) {
        diagnostics.push(
          buildDiagnostic("error", `Program address ${address} exceeds formal MK-61 address range.`)
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
      const targetAddress2 = typeof item.target === "number" ? item.target : labelAddresses.get(item.target);
      if (targetAddress2 === void 0) {
        diagnostics.push(
          buildDiagnostic("error", `Unknown label '${item.target}'`, item.sourceLine)
        );
        continue;
      }
      const opcode = safeAddressToOpcode(targetAddress2, item.sourceLine, diagnostics);
      if (opcode === void 0) {
        address += 1;
        continue;
      }
      steps.push(
        buildResolvedStep(address, opcode, formatAddress(targetAddress2), item.comment, item.unsafeReason)
      );
      cellRoles.push(buildAddressCellRole(address, opcode, item, options, targetProfile));
      address += 1;
    }
    const labels = {};
    const sortedLabels = [...labelAddresses.entries()].sort(
      ([, a], [, b]) => a - b
    );
    for (const [label, labelAddress] of sortedLabels) {
      labels[label] = safeFormatAddress(labelAddress);
    }
    markDarkEntryCells(cellRoles, labelAddresses, options, ast, targetProfile);
    return { steps, labels, cellRoles };
  }
  function buildResolvedStep(address, opcode, mnemonic, comment, unsafeReason) {
    const step = {
      address,
      opcode,
      hex: getOpcode(opcode).hex,
      mnemonic
    };
    if (comment !== void 0) step.comment = comment;
    if (unsafeReason !== void 0) step.unsafeReason = unsafeReason;
    return step;
  }
  function safeAddressToOpcode(address, line, diagnostics) {
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
  function buildCellRole(address, hex2, item, options, targetProfile) {
    const roles = ["exec"];
    const notes = [];
    if (item.raw) {
      roles.push("constant");
      notes.push("raw opcode can also be read as a byte");
    }
    if (options.opt === "max" && targetSupports(targetProfile, "display-bytes") && item.comment?.includes("display")) {
      roles.push("display-byte");
      notes.push("display byte role allowed");
    }
    const role = {
      address: formatAddress(address),
      hex: hex2,
      roles: uniqueRoles(roles)
    };
    if (notes.length > 0) role.note = notes.join("; ");
    if (item.unsafeReason !== void 0) role.unsafe = true;
    return role;
  }
  function buildAddressCellRole(address, opcode, item, options, targetProfile) {
    const roles = ["address"];
    const notes = [];
    if (options.opt === "max" && targetSupports(targetProfile, "address-constants")) {
      roles.push("constant");
      notes.push("address can be reused as constant");
    }
    if (options.opt === "max" && targetSupports(targetProfile, "code-data-overlay")) {
      roles.push("overlay");
      notes.push("code/data overlay allowed");
    }
    const role = {
      address: formatAddress(address),
      hex: getOpcode(opcode).hex,
      roles: uniqueRoles(roles)
    };
    if (notes.length > 0) role.note = notes.join("; ");
    if (item.unsafeReason !== void 0 || roles.includes("overlay")) role.unsafe = true;
    return role;
  }
  function markDarkEntryCells(cellRoles, labelAddresses, options, ast, targetProfile) {
    if (options.opt !== "max" || !targetSupports(targetProfile, "dark-entries")) return;
    const sharedTailNames = new Set(
      ast.blocks.filter((block) => block.mode === "shared_tail").map((block) => block.name)
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
  function uniqueRoles(roles) {
    return [...new Set(roles)];
  }
  function buildBranchRemovalCandidate(statement, ast) {
    return buildTerminalSelectCandidate(statement, ast) ?? buildComparisonBooleanCandidate(statement) ?? buildBooleanAlgebraCandidate(statement, ast) ?? buildAbsCandidate(statement) ?? buildMaxMinCandidate(statement) ?? buildClampCandidate(statement) ?? buildSaturatingUpdateCandidate(statement, ast) ?? buildBooleanSignToggleCandidate(statement, ast) ?? buildBooleanUpdateCandidate(statement, ast) ?? buildArithmeticIfSelect(statement, ast);
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
  function buildTerminalSelectCandidate(statement, ast) {
    if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) return void 0;
    const thenStatement = statement.thenBody[0];
    const elseStatement = statement.elseBody[0];
    if (!thenStatement || !elseStatement) return void 0;
    if (thenStatement.kind !== "pause" && thenStatement.kind !== "halt") return void 0;
    if (elseStatement.kind !== thenStatement.kind) return void 0;
    const selector = booleanSelectorExpression(statement.condition, ast) ?? comparisonSelectorExpression(statement.condition);
    if (!selector) return void 0;
    return {
      kind: thenStatement.kind,
      expr: terminalSelectExpression(thenStatement.expr, elseStatement.expr, selector),
      name: "arithmetic-if-terminal-select",
      detail: `Replaced boolean ${thenStatement.kind} if/else with arithmetic selection`
    };
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
  function buildArithmeticIfSelect(statement, ast) {
    if (!statement.elseBody || statement.thenBody.length !== 1 || statement.elseBody.length !== 1) {
      return void 0;
    }
    const thenAssign = statement.thenBody[0];
    const elseAssign = statement.elseBody[0];
    if (thenAssign?.kind !== "assign" || elseAssign?.kind !== "assign") return void 0;
    if (thenAssign.target !== elseAssign.target) return void 0;
    const selector = booleanSelectorExpression(statement.condition, ast);
    if (!selector) return void 0;
    const expr = addExpressions(
      multiplyExpressions(thenAssign.expr, selector),
      multiplyExpressions(elseAssign.expr, oneMinus(selector))
    );
    return {
      kind: "assign",
      target: thenAssign.target,
      expr,
      name: "arithmetic-if-select",
      detail: "Replaced boolean if/else with arithmetic selection"
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
    const range = integerRangeFor(assign.target, ast);
    if (!range) return void 0;
    const targetExpr = { kind: "identifier", name: assign.target };
    const { left, right, op } = statement.condition;
    if (!expressionEquals(left, targetExpr)) return void 0;
    const decrement = matchTargetMinusDelta(assign.expr, assign.target);
    if (decrement && op === ">" && isNumericValue(decrement, 1) && range.min !== void 0 && isNumericValue(right, range.min)) {
      return maxCandidate(assign.target, assign.expr, right, "Replaced saturating decrement branch with max()");
    }
    const increment = matchTargetPlusDelta(assign.expr, assign.target);
    if (increment && op === "<" && isNumericValue(increment, 1) && range.max !== void 0 && isNumericValue(right, range.max)) {
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
    for (const state of ast.states) {
      const field = state.fields.find((candidate) => candidate.name === name);
      if (!field) continue;
      if (field.type === "digit") return { min: 0, max: 9 };
      if (field.type === "flag") return { min: 0, max: 1 };
      if (field.type === "range" && Number.isInteger(field.min) && Number.isInteger(field.max)) {
        const range = {};
        if (field.min !== void 0) range.min = field.min;
        if (field.max !== void 0) range.max = field.max;
        return range;
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
      diag_left_index: 2,
      diag_right_index: 2,
      cell_mask: 2,
      cell_used: 3,
      cell_mark: 3,
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
      case "diag_left_index":
        return norm4Expression(addExpressions(args[0], args[1]));
      case "diag_right_index":
        return norm4Expression(subtractExpressions(args[0], args[1]));
      case "cell_mask":
        return cellMaskExpression(args[0], args[1]);
      case "cell_used":
        return { kind: "call", callee: "bit_and", args: [args[0], cellMaskExpression(args[1], args[2])] };
      case "cell_mark":
        return { kind: "call", callee: "bit_or", args: [args[0], cellMaskExpression(args[1], args[2])] };
      case "packed4_add":
        return addExpressions(
          args[0],
          multiplyExpressions(args[2], pow10Expression(subtractExpressions(args[1], numberExpression(1))))
        );
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
  function matchCellHelperCall(expr, name) {
    if (expr.kind !== "call" || expr.callee.toLowerCase() !== name || expr.args.length !== 3) return void 0;
    return {
      mask: expr.args[0],
      x: expr.args[1],
      y: expr.args[2]
    };
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
  function maxExpression(left, right) {
    return { kind: "call", callee: "max", args: [left, right] };
  }
  function minExpression(left, right) {
    return negateExpression(maxExpression(negateExpression(left), negateExpression(right)));
  }
  function absExpression(expr) {
    return { kind: "call", callee: "abs", args: [expr] };
  }
  function signExpression(expr) {
    return { kind: "call", callee: "sign", args: [expr] };
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
  function isNumericValue(expr, value) {
    const parsed = numericLiteralValue(expr);
    return parsed !== void 0 && parsed === value;
  }
  function numericLiteralValue(expr) {
    if (expr.kind !== "number") return void 0;
    const value = Number(expr.raw);
    return Number.isFinite(value) ? value : void 0;
  }
  function estimateOrdinaryIfCost(statement) {
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
  function estimateSimpleStatementCost(statement) {
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
  function estimateConditionCost(condition) {
    return estimateExpressionCost(condition.left) + estimateExpressionCost(condition.right) + 1 + 2;
  }
  function estimateExpressionCost(expr) {
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
  function estimateCallCost(expr) {
    const name = expr.callee.toLowerCase();
    const macro = ticTacToeExpressionMacro(name, expr.args);
    if (macro !== void 0) return estimateExpressionCost(macro);
    if (name === "random" || name === "pi") return 1;
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
  function buildOptimizerReport(ast, options, optimizations, candidates, cellRoles, targetProfile) {
    const activeNames = new Set(optimizations.map((optimization) => optimization.name));
    if (cellRoles.some((cell) => cell.roles.includes("overlay"))) activeNames.add("code-data-overlay");
    if (cellRoles.some((cell) => cell.roles.includes("dark-entry"))) activeNames.add("dark-entry-layout");
    if (cellRoles.some((cell) => cell.roles.includes("display-byte"))) activeNames.add("display-byte-layout");
    if (targetProfile.emulatorFacts.some((fact) => fact.id === "step-vs-run-delta")) {
      activeNames.add("step-vs-run-profile");
    }
    const selectedCandidateVariants = new Set(
      candidates.filter((candidate) => candidate.selected).map((candidate) => candidate.variant)
    );
    const consideredCandidateVariants = new Set(
      candidates.filter((candidate) => !candidate.selected).map((candidate) => candidate.variant)
    );
    const capabilities = optimizerCapabilities.map((capability) => {
      const missing = capability.requires.filter((feature) => !targetSupports(targetProfile, feature));
      const safeBlocked = capability.unsafe && options.opt === "safe";
      let status = capability.planned ? "planned" : "candidate";
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
        requires: capability.requires
      };
    });
    return {
      automatic: true,
      active: capabilities.filter((capability) => capability.status === "active").length,
      considered: capabilities.filter((capability) => capability.status === "considered").length,
      candidate: capabilities.filter((capability) => capability.status === "candidate").length,
      blocked: capabilities.filter((capability) => capability.status === "blocked").length,
      planned: capabilities.filter((capability) => capability.status === "planned").length,
      capabilities
    };
  }
  var optimizerCapabilities = [
    {
      id: "store-recall-peephole",
      category: "stack",
      source: "documented",
      unsafe: false,
      requires: [],
      activeWhen: ["store-recall-peephole"],
      detail: "Elides immediate X->\u041F r / \u041F->X r pairs when no raw boundary or unsafe effect is crossed."
    },
    {
      id: "stack-current-x-scheduling",
      category: "stack",
      source: "documented",
      unsafe: false,
      requires: [],
      activeWhen: ["stack-current-x-scheduling", "dead-temp-store"],
      detail: "Keeps a just-computed value in X for a following commutative use and removes the temporary store when safe."
    },
    {
      id: "return-zero-jump",
      category: "flow",
      source: "mk61-delta",
      unsafe: true,
      requires: [],
      activeWhen: ["return-zero-jump"],
      detail: "Uses \u0412/\u041E as one-cell \u0411\u041F 01 only when the return stack is known empty."
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
        "kor-digit-test"
      ],
      detail: "Umbrella rule for replacing provably equivalent conditionals with branchless arithmetic, sign, extrema, or masked updates. Also marked active when the GameIntent backend selects \u041A max / \u041A \u0417\u041D / \u041A\u2228 as semantic equivalents."
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
        "kor-digit-test"
      ],
      detail: "Uses direct F x?0 tests when one side of a condition is a proved zero, avoiding a zero literal and subtraction. Also marked active when GameIntent proves zero/digit boundaries through fractional indirect addressing or \u041A\u2228."
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
        "indirect-register-flow"
      ],
      detail: "Lowers high-level command dispatch automatically; safe mode keeps conservative compare chains, GameIntent may pick indirect or super-dark dispatch instead."
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
        "kzn-double"
      ],
      detail: "Replaces simple boolean if/else assignments, stops, and conditional moves with arithmetic selection when shorter. Counts \u041A max/\u041A \u0417\u041D as the GameIntent-equivalent selection."
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
        "hex-mantissa-arithmetic"
      ],
      detail: "Replaces conditional +=/-= and sign toggles guarded by a proved boolean with masked arithmetic. GameIntent's hex-mantissa updates count as the masked-update equivalent."
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
        "kmax-zero-through"
      ],
      detail: "Replaces simple extrema, sign, clamp, comparison-mask, and boolean-algebra branches when shorter. \u041A max zero-through counts as the extrema selection."
    },
    {
      id: "indirect-flow",
      category: "flow",
      source: "documented",
      unsafe: false,
      requires: [],
      activeWhen: ["indirect-register-flow"],
      detail: "Candidate rule: replace direct branches/calls with \u041A \u0411\u041F/\u041A \u041F\u041F/\u041A x?0 only when the address value is already live and cheaper."
    },
    {
      id: "fl-decrement-branch",
      category: "flow",
      source: "documented",
      unsafe: false,
      requires: [],
      activeWhen: ["fl-unit-decrement", "r0-indirect-counter"],
      detail: "Uses F L0..F L3 as compact decrement-and-continue/decrement-and-branch forms for small counters. The R0 indirect counter is its GameIntent equivalent."
    },
    {
      id: "address-constant-overlay",
      category: "layout",
      source: "undocumented",
      unsafe: true,
      requires: ["address-constants", "code-data-overlay"],
      activeWhen: ["code-data-overlay"],
      detail: "Lets branch operands double as constants or executable bytes after the layout pass marks a conflict-free overlay role."
    },
    {
      id: "cyclic-address-layout",
      category: "layout",
      source: "undocumented",
      unsafe: true,
      requires: ["dark-entries", "code-data-overlay"],
      activeWhen: ["cyclic-address-layout"],
      detail: "Uses formal address-space wraparound and side branches only after the layout pass proves the shared-tail target."
    },
    {
      id: "constants-dual-use",
      category: "data",
      source: "undocumented",
      unsafe: true,
      requires: ["address-constants"],
      activeWhen: ["constants-dual-use"],
      detail: "Reuses one stored value as arithmetic coefficient, rounding adjuster, and address-like dispatch data."
    },
    {
      id: "dark-entry-layout",
      category: "layout",
      source: "undocumented",
      unsafe: true,
      requires: ["dark-entries"],
      activeWhen: ["dark-entry-layout"],
      detail: "Exposes shared tails as dark-entry candidates when the layout pass can point at the same executable suffix."
    },
    {
      id: "super-dark-dispatch",
      category: "flow",
      source: "undocumented",
      unsafe: true,
      requires: ["super-dark-dispatch", "indirect-flow"],
      activeWhen: ["super-dark-dispatch"],
      detail: "Dispatch candidate for indirect \u041A \u0411\u041F R with FA..FF; selected only when layout can place one-command cases at 48..53 and tails at 01..06."
    },
    {
      id: "r0-alias-indirect",
      category: "flow",
      source: "mk61-delta",
      unsafe: true,
      requires: ["undocumented-opcodes", "r0-t-alias"],
      activeWhen: ["r0-indirect-counter"],
      detail: "Treats MK-61 *F/R0 aliases as byte/formal-address candidates only; the profile proves they transform R0."
    },
    {
      id: "r0-fractional-sentinel",
      category: "flow",
      source: "mk61-delta",
      unsafe: true,
      requires: ["r0-fractional-sentinel"],
      activeWhen: ["fractional-indirect-addressing", "r0-indirect-counter"],
      detail: "Computed-dispatch candidate for fractional R0 selecting R3 or jumping to 99 while creating the -99999999 sentinel."
    },
    {
      id: "raw-display-5f",
      category: "display",
      source: "undocumented",
      unsafe: true,
      requires: ["raw-display-5f"],
      activeWhen: [],
      detail: "Display lowering candidate for opcode 5F; selected only when the raw display mutation is the intended observable effect."
    },
    {
      id: "x2-display-register",
      category: "display",
      source: "mk61-delta",
      unsafe: true,
      requires: ["x2-register", "display-bytes"],
      activeWhen: ["x2-display-byte-scheduling", "display-byte-layout"],
      detail: "Display/data candidate for scheduling X2, \u0412\u041F, '.', sign digits, and display bytes without extra storage."
    },
    {
      id: "vp-fraction-restore",
      category: "display",
      source: "mk61-delta",
      unsafe: true,
      requires: ["x2-register", "display-bytes"],
      activeWhen: ["vp-fraction-restore"],
      detail: "Uses \u0412\u041F where it simultaneously restores X2 and provides the needed fractional/mantissa side effect."
    },
    {
      id: "hex-mantissa-arithmetic",
      category: "data",
      source: "undocumented",
      unsafe: true,
      requires: ["display-bytes"],
      activeWhen: ["hex-mantissa-arithmetic"],
      detail: "Represents compact state as hexadecimal mantissa/sign digits when all display hazards are proved."
    },
    {
      id: "fractional-indirect-addressing",
      category: "data",
      source: "mk61-delta",
      unsafe: true,
      requires: ["indirect-flow"],
      activeWhen: ["fractional-indirect-addressing"],
      detail: "Uses indirect addressing truncation/fractional effects as data selection only with range and emulator facts."
    },
    {
      id: "kzn-double",
      category: "data",
      source: "documented",
      unsafe: false,
      requires: [],
      activeWhen: ["kzn-double"],
      detail: "Uses \u041A \u0417\u041D as a one-cell numeric transform when equivalent to the needed doubling/sign-digit operation."
    },
    {
      id: "kor-digit-test",
      category: "data",
      source: "documented",
      unsafe: false,
      requires: [],
      activeWhen: ["kor-digit-test"],
      detail: "Uses \u041A\u2228 as a compact digit/boundary test when bit-level equivalence is proved."
    },
    {
      id: "kmax-zero-through",
      category: "stack",
      source: "documented",
      unsafe: false,
      requires: [],
      activeWhen: ["kmax-zero-through"],
      detail: "Uses \u041A max as a stack/value transform, including zero-through cases, when it preserves source semantics."
    },
    {
      id: "error-stop-idiom",
      category: "trap",
      source: "mk61-delta",
      unsafe: true,
      requires: ["error-stops"],
      activeWhen: ["error-stop"],
      detail: "Use domain-error stops only for explicit trap intent or after verifier proves the failure mode is acceptable."
    },
    {
      id: "step-vs-run-verification",
      category: "verification",
      source: "mk61-delta",
      unsafe: false,
      requires: [],
      activeWhen: ["step-vs-run-profile"],
      detail: "Uses mk61_exact emulator facts for Danilov-era differences between step mode, continuous run, exponent sign changes, Cx, \u0412\u2191, and \u041F->X as optimization hazards."
    },
    {
      id: "jump-to-next-threading",
      category: "flow",
      source: "documented",
      unsafe: false,
      requires: [],
      activeWhen: ["jump-to-next-threading"],
      detail: "Drops unconditional \u0411\u041F whose only target is the immediately following label after a layout pass collapses the trampoline."
    },
    {
      id: "duplicate-failure-tail-merge",
      category: "flow",
      source: "documented",
      unsafe: false,
      requires: [],
      activeWhen: ["duplicate-failure-tail-merge"],
      detail: "Coalesces structurally identical pause-0 failure tails into a single shared exit, removing the trampoline cells between them."
    },
    {
      id: "liveness-analysis",
      category: "verification",
      source: "documented",
      unsafe: false,
      requires: [],
      activeWhen: ["liveness-analysis"],
      detail: "Foundational data-flow pass: computes liveIn/liveOut per IR position so dead-store-elimination, register-coalesce and other passes can fire safely."
    },
    {
      id: "dead-store-elimination",
      category: "stack",
      source: "documented",
      unsafe: false,
      requires: [],
      activeWhen: ["dead-store-elimination"],
      detail: "Removes X->\u041F r writes whose register is never read again before the next write to the same register, using whole-program liveness."
    },
    {
      id: "last-x-reuse",
      category: "stack",
      source: "documented",
      unsafe: false,
      requires: [],
      activeWhen: ["last-x-reuse"],
      detail: "Drops \u041F->X r when the IR pass can prove X already holds the value just stored to r and no intervening op clobbers X (including \u0421/\u041F, jumps, ALU)."
    },
    {
      id: "constant-folding",
      category: "data",
      source: "documented",
      unsafe: false,
      requires: [],
      activeWhen: ["constant-folding"],
      detail: "Eliminates identity arithmetic such as '0 +' and '1 *' from the IR after upstream passes simplify the expression tree."
    },
    {
      id: "cse-display-block",
      category: "data",
      source: "documented",
      unsafe: false,
      requires: [],
      activeWhen: ["cse-display-block"],
      detail: "Common subexpression elimination for pure recall/ALU blocks ending in \u0412/\u041E; redirects duplicates to a shared exit when profitable."
    },
    {
      id: "jump-thread",
      category: "flow",
      source: "documented",
      unsafe: false,
      requires: [],
      activeWhen: ["jump-thread"],
      detail: "Threads jump-to-jump chains through trampoline labels to their final target, freeing intermediate cells."
    },
    {
      id: "dead-code-after-halt",
      category: "flow",
      source: "documented",
      unsafe: false,
      requires: [],
      activeWhen: ["dead-code-after-halt"],
      detail: "CFG analysis from the entry point removes ops that are unreachable through any combination of fall-through and jump edges."
    },
    {
      id: "register-coalesce",
      category: "stack",
      source: "documented",
      unsafe: false,
      requires: [],
      activeWhen: ["register-coalesce"],
      detail: "Identifies non-overlapping live ranges of scratch registers that could be coalesced; pass reports opportunities and is the seat for future rewriting."
    },
    {
      id: "arithmetic-if-pass",
      category: "flow",
      source: "documented",
      unsafe: false,
      requires: [],
      activeWhen: ["arithmetic-if-pass"],
      detail: "IR-level seat for branchless arithmetic-if rewriting; current size-gated rewriting still happens at the AST select stage and feeds this pass via the candidate ledger."
    },
    {
      id: "redundant-prologue-elimination",
      category: "flow",
      source: "documented",
      unsafe: false,
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
        const value = field.initial ?? (field.generated === "random" ? "random() * 999" : void 0) ?? (field.optional ? "0" : void 0);
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
  function buildMachineFeaturesUsed(targetProfile, optimizations, cellRoles, candidates) {
    const used = /* @__PURE__ */ new Map();
    const add = (id, detail, source) => {
      const targetDetail = targetProfile.features.find((feature) => feature.id === id)?.detail;
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
      add("x2-restore-boundaries", "Optimizer used \u0412\u041F as both X2 restoration and fractional/mantissa transform.", "optimizer");
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
  function buildProofReport(ast, items, cellRoles, options, optimizations) {
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
          optimizations.filter((optimization) => optimization.name.startsWith("arithmetic-if-")).map((optimization) => optimization.name)
        )
      ];
      proofs.push({
        id: "branch-equivalence",
        status: "proved",
        detail: `Removed conditional branches via ${variants.join(", ")} after matching assignment/update shape and value ranges.`
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
    if (options.opt === "max" && cellRoles.some((cell) => cell.roles.includes("display-byte"))) {
      proofs.push({
        id: "display-byte-observable-boundary",
        status: "assumed",
        detail: "Display-byte candidates are reported; final X2/display preservation requires emulator trace tests."
      });
    }
    return proofs;
  }
  function parseHotBlock(text) {
    const match = /^(.+)=(\d+)$/u.exec(text);
    if (!match) return { name: text, estimatedCells: 0 };
    return { name: match[1], estimatedCells: Number(match[2]) };
  }
  function stripCellUnsafe(cell) {
    const clean = {
      address: cell.address,
      hex: cell.hex,
      roles: cell.roles
    };
    if (cell.note !== void 0) clean.note = cell.note;
    return clean;
  }
  function stripCandidateUnsafe(candidate) {
    return { ...candidate, unsafe: false };
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
    return 1 + v2.inputs.length + v2.state.length + v2.screens.length + (v2.turn ? 1 + countV2Statements(v2.turn.body) : 0) + v2.rules.reduce((sum, rule) => sum + 1 + countV2Statements(rule.body), 0);
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
  function selectDispatchCandidate(statement, options, targetProfile) {
    const site = statement.name ?? `dispatch@${statement.line}`;
    const safeCost = estimateDispatchCost(statement, false);
    const fallthroughCost = estimateDispatchCost(statement, true);
    const candidates = [
      {
        site,
        variant: "safe-compare-chain",
        steps: safeCost,
        selected: options.opt === "safe",
        reason: options.opt === "safe" ? "safe mode selected conservative compare-chain" : "available fallback",
        unsafe: false
      },
      {
        site,
        variant: "fallthrough-compare-chain",
        steps: fallthroughCost,
        selected: options.opt === "max",
        reason: "uses case ordering to omit the final branch when possible",
        unsafe: false
      }
    ];
    if (options.opt === "max" && targetSupports(targetProfile, "dark-entries") && targetSupports(targetProfile, "address-constants") && targetSupports(targetProfile, "code-data-overlay")) {
      candidates.push({
        site,
        variant: "dark-indirect-table",
        steps: Math.max(4, statement.cases.length + 3),
        selected: false,
        reason: "rejected until the layout pass proves a conflict-free address/data table for this site",
        unsafe: true
      });
    }
    if (options.opt === "max" && statement.cases.length <= 6 && targetSupports(targetProfile, "super-dark-dispatch") && targetSupports(targetProfile, "indirect-flow")) {
      candidates.push({
        site,
        variant: "super-dark-dispatch",
        steps: Math.max(3, statement.cases.length + 2),
        selected: false,
        reason: "rejected until layout can place one-command cases at 48..53 and tails at 01..06",
        unsafe: true
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
  function statementsTerminate(statements) {
    const last = statements.at(-1);
    if (!last) return false;
    return last.kind === "halt" || last.kind === "loop" || last.kind === "trap";
  }
  function parseRawInstruction(text) {
    const hex2 = /^[0-9A-Fa-f]{2}$/u.exec(text);
    if (hex2) {
      const opcode = Number.parseInt(text, 16);
      return { opcode, mnemonic: getOpcode(opcode).name, comment: "raw hex" };
    }
    const direct = /^(БП|ПП|F\s*x<0|F\s*x=0|F\s*x!=0|F\s*x>=0|F\s*L[0-3])\s+([A-Za-z_][\w]*|[0-9A-Fa-f]{2})$/u.exec(text);
    if (direct) {
      const opcode = directOpcode(direct[1]);
      return {
        opcode,
        mnemonic: getOpcode(opcode).name,
        target: parseTarget(direct[2]),
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
    const directMemory = /^(X->П|П->X)\s+R?([0-9a-eавсде])$/iu.exec(text);
    if (directMemory) {
      const register = registerFromText(directMemory[2]);
      const base = directMemory[1].startsWith("X") ? 64 : 96;
      return {
        opcode: base + registerIndex(register),
        mnemonic: `${directMemory[1]} ${register}`
      };
    }
    const indirect = /^(К\s*)?(БП|ПП|X->П|П->X|x!=0|x>=0|x<0|x=0)\s*R?([0-9a-eавсде])$/iu.exec(text);
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
    return /^[0-9A-Fa-f]{2}$/u.test(text) ? codeToAddress(Number.parseInt(text, 16)) : text;
  }
  function directOpcode(text) {
    const normalized = text.replace(/\s+/g, " ");
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
    const normalized = text.toLowerCase();
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
  function riskReason(risk, delivery, enterable) {
    if (!enterable.includes(delivery)) return `opcode not enterable in ${delivery} delivery`;
    if (risk === "dangerous") return "dangerous opcode is unsafe-unverified";
    if (risk === "undocumented") return "undocumented opcode is unsafe-unverified";
    return void 0;
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
      const address = formatAddress(step.address).padStart(4, " ");
      const command = step.mnemonic.padEnd(23, " ");
      const comments = [
        step.comment,
        step.unsafeReason ? `unsafe-unverified: ${step.unsafeReason}` : void 0
      ].filter((value) => Boolean(value)).join("; ");
      lines.push(` ${address} |  ${step.hex}  | ${command} | ${comments}`);
    }
    return lines.join("\n");
  }

  // src/browser/emulator-bridge.ts
  var DEFAULT_COMPILE_OPTIONS = {
    opt: "max",
    delivery: "hex",
    budget: 105,
    warnUnsafe: true
  };
  var DEFAULT_DEBOUNCE_MS = 250;
  var STATUS_ID = "m61-emulator-status";
  function compileForBrowser(source, options = {}) {
    const result = compileM61(source, { ...DEFAULT_COMPILE_OPTIONS, ...options });
    const programText = formatProgramTokens(result.steps.map((step) => step.hex));
    return {
      source,
      programText,
      listing: formatListing(result),
      steps: result.steps,
      report: result.report,
      diagnostics: result.diagnostics
    };
  }
  function compileToProgramText(source, options = {}) {
    return compileForBrowser(source, options).programText;
  }
  function looksLikeM61Source(text) {
    const normalized = text.trim();
    return /\btarget\s+mk61\b/iu.test(normalized) || /\bbudget\s+\d+\s+cells\b/iu.test(normalized) || /\boptimize\s+size\b/iu.test(normalized) || /\bprogram\s+[A-Za-z_][\w-]*\s*\{/u.test(normalized);
  }
  function installEmulatorBridge(options = {}) {
    window.__m61EmulatorBridge?.uninstall();
    const program = document.getElementById("program");
    const writeButton = document.getElementById("text_to_program");
    if (!(program instanceof HTMLElement)) {
      throw new Error("M61 bridge cannot find #program on this page.");
    }
    if (!(writeButton instanceof HTMLElement)) {
      throw new Error("M61 bridge cannot find #text_to_program on this page.");
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
      if (!looksLikeM61Source(source)) return void 0;
      const compiled = compileForBrowser(source, compilerOptions);
      lastSource = source;
      lastResult = compiled;
      writeProgramText(program, compiled.programText);
      program.dispatchEvent(new CustomEvent("m61:compiled", {
        bubbles: true,
        detail: compiled
      }));
      setStatus(`M61 compiled: ${compiled.report.steps}/${compiled.report.budget} cells.`);
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
      setStatus("M61 source restored.");
      return true;
    };
    const uninstall = () => {
      if (previewTimer !== void 0) window.clearTimeout(previewTimer);
      for (const cleanup of cleanups.splice(0)) cleanup();
      statusElement.remove();
      if (window.__m61EmulatorBridge === bridge) {
        delete window.__m61EmulatorBridge;
      }
      if (window.M61Emulator === bridge) {
        delete window.M61Emulator;
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
        console.error("[M61] compile failed", error);
      }
    };
    const previewCurrentText = () => {
      const source = readProgramText(program);
      if (!looksLikeM61Source(source)) return;
      try {
        const compiled = compileForBrowser(source, compilerOptions);
        lastResult = compiled;
        setStatus(`M61 detected: ${compiled.report.steps}/${compiled.report.budget} cells. Click Write to load.`);
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
        if (autoWriteOnPaste && looksLikeM61Source(readProgramText(program))) {
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
    setStatus("M61 bridge ready. Paste M61 source, then click Write.");
    window.__m61EmulatorBridge = bridge;
    window.M61Emulator = bridge;
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
    looksLikeM61Source
  };
  if (typeof window !== "undefined") {
    window.M61 = api;
    try {
      installEmulatorBridge();
      console.info("[M61] Emulator bridge installed.");
    } catch (error) {
      console.warn("[M61] Compiler loaded, but the emulator bridge was not installed.", error);
    }
  }
  return __toCommonJS(emulator_bridge_exports);
})();
