import type { DeliveryMode, OpcodeInfo, RegisterName } from "./types.ts";

const HEX = "0123456789ABCDEF";
const REGISTERS: RegisterName[] = [
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

function hex(code: number): string {
  return code.toString(16).toUpperCase().padStart(2, "0");
}

function info(
  code: number,
  name: string,
  keys = name,
  extra: Partial<OpcodeInfo> = {},
): OpcodeInfo {
  return {
    code,
    hex: hex(code),
    name,
    keys,
    enterable: extra.enterable ?? ["manual", "loader", "hex"],
    takesAddress: extra.takesAddress ?? false,
    x2Effect: extra.x2Effect ?? "affects",
    risk: extra.risk ?? "documented",
  };
}

function onlyHex(): DeliveryMode[] {
  return ["hex"];
}

function loaderOrHex(): DeliveryMode[] {
  return ["loader", "hex"];
}

export const opcodeCatalog: OpcodeInfo[] = buildOpcodeCatalog();

export const opcodeByCode = new Map<number, OpcodeInfo>(
  opcodeCatalog.map((item) => [item.code, item]),
);

export const opcodeByName = new Map<string, OpcodeInfo>();

for (const item of opcodeCatalog) {
  for (const key of normalizeAliases(item)) {
    opcodeByName.set(key, item);
  }
}

export function getOpcode(code: number): OpcodeInfo {
  const item = opcodeByCode.get(code);
  if (!item) {
    throw new Error(`Unknown opcode ${hex(code)}`);
  }
  return item;
}

export function findOpcodeName(name: string): OpcodeInfo | undefined {
  return opcodeByName.get(normalizeName(name));
}

export function registerIndex(register: RegisterName): number {
  const index = REGISTERS.indexOf(register);
  if (index === -1) {
    throw new Error(`Unknown register ${register}`);
  }
  return index;
}

export function registerFromText(text: string): RegisterName {
  const normalized = text.trim().toLowerCase();
  const cyrMap: Record<string, RegisterName> = {
    а: "a",
    в: "b",
    с: "c",
    д: "d",
    е: "e",
  };
  const mapped = cyrMap[normalized] ?? normalized;
  if ((REGISTERS as string[]).includes(mapped)) {
    return mapped as RegisterName;
  }
  throw new Error(`Unknown register ${text}`);
}

export function addressToOpcode(address: number): number {
  if (address < 0 || address > 0xff) {
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

export function formatAddress(address: number): string {
  return hex(addressToOpcode(address));
}

export function codeToAddress(code: number): number {
  const high = code >> 4;
  const low = code & 0x0f;
  if (high <= 9 && low <= 9) {
    return high * 10 + low;
  }
  if (high >= 10 && low <= 9) {
    return 100 + (high - 10) * 10 + low;
  }
  return code;
}

function buildOpcodeCatalog(): OpcodeInfo[] {
  const result: OpcodeInfo[] = [];
  for (let code = 0; code <= 0xff; code += 1) {
    result.push(
      info(code, `undoc ${hex(code)}`, `undoc ${hex(code)}`, {
        enterable: onlyHex(),
        risk: "undocumented",
        x2Effect: "unknown",
      }),
    );
  }

  const set = (item: OpcodeInfo) => {
    result[item.code] = item;
  };

  for (let i = 0; i <= 9; i += 1) set(info(i, String(i), String(i), { x2Effect: "restores" }));
  set(info(0x0a, ".", ".", { x2Effect: "restores" }));
  set(info(0x0b, "/-/", "/-/", { x2Effect: "restores" }));
  set(info(0x0c, "ВП", "ВП", { x2Effect: "restores" }));
  set(info(0x0d, "Cx", "Cx", { x2Effect: "preserves" }));
  set(info(0x0e, "В↑", "В↑", { x2Effect: "affects" }));
  set(info(0x0f, "F Вx", "F Вx", { x2Effect: "affects" }));

  set(info(0x10, "+"));
  set(info(0x11, "-"));
  set(info(0x12, "*"));
  set(info(0x13, "/"));
  set(info(0x14, "<->"));
  set(info(0x15, "F 10^x"));
  set(info(0x16, "F e^x"));
  set(info(0x17, "F lg"));
  set(info(0x18, "F ln"));
  set(info(0x19, "F sin^-1"));
  set(info(0x1a, "F cos^-1"));
  set(info(0x1b, "F tg^-1"));
  set(info(0x1c, "F sin"));
  set(info(0x1d, "F cos"));
  set(info(0x1e, "F tg"));
  set(info(0x1f, "empty 1F", "1F", { enterable: loaderOrHex(), risk: "undocumented" }));

  set(info(0x20, "F pi"));
  set(info(0x21, "F sqrt"));
  set(info(0x22, "F x^2"));
  set(info(0x23, "F 1/x"));
  set(info(0x24, "F x^y"));
  set(info(0x25, "F reverse"));
  set(info(0x26, "К °->′"));
  set(info(0x27, "К -", "К -", { risk: "dangerous" }));
  set(info(0x28, "К *", "К *", { risk: "dangerous" }));
  set(info(0x29, "К /", "К /", { risk: "dangerous" }));
  set(info(0x2a, "К °->′\""));
  for (let code = 0x2b; code <= 0x2e; code += 1) {
    set(info(code, `error ${hex(code)}`, hex(code), { enterable: loaderOrHex(), risk: "dangerous" }));
  }
  set(info(0x2f, "empty 2F", "2F", { enterable: loaderOrHex(), risk: "undocumented" }));

  set(info(0x30, "К °<-′\""));
  set(info(0x31, "К |x|"));
  set(info(0x32, "К ЗН"));
  set(info(0x33, "К °<-′"));
  set(info(0x34, "К [x]"));
  set(info(0x35, "К {x}"));
  set(info(0x36, "К max"));
  set(info(0x37, "К ∧"));
  set(info(0x38, "К ∨"));
  set(info(0x39, "К ⊕"));
  set(info(0x3a, "К ИНВ"));
  set(info(0x3b, "К СЧ"));
  set(info(0x3c, "error 3C", "3C", { enterable: loaderOrHex(), risk: "dangerous" }));
  set(info(0x3d, "alias 3D", "3D", { enterable: loaderOrHex(), risk: "undocumented" }));
  set(info(0x3e, "Y->X", "3E", { enterable: loaderOrHex(), risk: "undocumented", x2Effect: "preserves" }));
  set(info(0x3f, "empty 3F", "3F", { enterable: loaderOrHex(), risk: "undocumented" }));

  for (let i = 0; i <= 0xe; i += 1) {
    const r = REGISTERS[i]!;
    set(info(0x40 + i, `X->П ${r}`));
  }
  set(info(0x4f, "X->П 0 alias", "4F", { enterable: loaderOrHex(), risk: "undocumented" }));

  set(info(0x50, "С/П"));
  set(info(0x51, "БП", "БП", { takesAddress: true }));
  set(info(0x52, "В/О"));
  set(info(0x53, "ПП", "ПП", { takesAddress: true }));
  set(info(0x54, "К НОП"));
  set(info(0x55, "К 1"));
  set(info(0x56, "К 2"));
  set(info(0x57, "F x!=0", "F x!=0", { takesAddress: true }));
  set(info(0x58, "F L2", "F L2", { takesAddress: true }));
  set(info(0x59, "F x>=0", "F x>=0", { takesAddress: true }));
  set(info(0x5a, "F L3", "F L3", { takesAddress: true }));
  set(info(0x5b, "F L1", "F L1", { takesAddress: true }));
  set(info(0x5c, "F x<0", "F x<0", { takesAddress: true }));
  set(info(0x5d, "F L0", "F L0", { takesAddress: true }));
  set(info(0x5e, "F x=0", "F x=0", { takesAddress: true }));
  set(info(0x5f, "raw display 5F", "5F", { enterable: loaderOrHex(), risk: "undocumented" }));

  for (let i = 0; i <= 0xe; i += 1) {
    const r = REGISTERS[i]!;
    set(info(0x60 + i, `П->X ${r}`));
  }
  set(info(0x6f, "П->X 0 alias", "6F", { enterable: loaderOrHex(), risk: "undocumented" }));

  const indirectBlocks: Array<[number, string]> = [
    [0x70, "К x!=0"],
    [0x80, "К БП"],
    [0x90, "К x>=0"],
    [0xa0, "К ПП"],
    [0xb0, "К X->П"],
    [0xc0, "К x<0"],
    [0xd0, "К П->X"],
    [0xe0, "К x=0"],
  ];
  for (const [base, name] of indirectBlocks) {
    for (let i = 0; i <= 0xe; i += 1) {
      const r = REGISTERS[i]!;
      set(info(base + i, `${name} ${r}`));
    }
    set(info(base + 0xf, `${name} 0 alias`, hex(base + 0xf), {
      enterable: loaderOrHex(),
      risk: "undocumented",
    }));
  }

  for (let code = 0xf0; code <= 0xff; code += 1) {
    set(info(code, `F* empty ${hex(code)}`, hex(code), {
      enterable: loaderOrHex(),
      risk: "undocumented",
      x2Effect: "affects",
    }));
  }

  return result;
}

function normalizeAliases(item: OpcodeInfo): string[] {
  const aliases = new Set<string>([
    item.name,
    item.keys,
    item.hex,
    item.name.replaceAll("->", "→"),
    item.name.replaceAll("П->X", "Пх"),
    item.name.replaceAll("X->П", "хП"),
  ]);

  if (item.name === "К ∧") aliases.add("KΛ");
  if (item.name === "К ∨") aliases.add("KV");
  if (item.name === "К ⊕") aliases.add("K⊕");
  if (item.name === "К ИНВ") aliases.add("Кинв");
  if (item.name === "К ЗН") aliases.add("Кзн");
  if (item.name === "К |x|") aliases.add("K|x|");
  if (item.name === "К [x]") aliases.add("K[x]");
  if (item.name === "К {x}") aliases.add("K{x}");
  if (item.name === "С/П") aliases.add("STOP");
  if (item.name === "В/О") aliases.add("RTN");

  return [...aliases].map(normalizeName);
}

function normalizeName(name: string): string {
  return name
    .trim()
    .replace(/\s+/g, " ")
    .replaceAll("×", "*")
    .replaceAll("÷", "/")
    .replaceAll("−", "-")
    .replaceAll("Х", "X")
    .replaceAll("K", "К")
    .replaceAll("k", "к")
    .toLowerCase();
}
