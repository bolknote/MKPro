import { getOpcode } from "./opcodes.ts";
import type { LayoutIrCell } from "./types.ts";

export interface SuperDarkDispatchCell {
  address: number;
  opcode: number;
  register: string;
  tactic: string;
  selectorValue?: string;
}

export interface SuperDarkLayoutPair {
  formal: number;
  entryAddress: number;
  continuationAddress: number;
  entryOpcode: number;
  continuationOpcode: number;
}

export interface SuperDarkLayoutProof {
  proved: boolean;
  pairs: SuperDarkLayoutPair[];
  dispatchCells: SuperDarkDispatchCell[];
  reasons: string[];
}

export interface SuperDarkLayoutOptions {
  selectorValues?: Readonly<Record<string, string>>;
}

function hex(value: number): string {
  return value.toString(16).toUpperCase().padStart(2, "0");
}

export function verifySuperDarkSuffixLayout(
  layout: readonly LayoutIrCell[],
  options: SuperDarkLayoutOptions = {},
): SuperDarkLayoutProof {
  const byAddress = new Map(layout.map((cell) => [cell.address, cell]));
  const pairs: SuperDarkLayoutPair[] = [];
  const dispatchCells = collectSuperDarkDispatchCells(layout, options.selectorValues ?? {});
  const provedDispatchCells = dispatchCells.filter((cell) => isSuperDarkSelectorValue(cell.selectorValue));
  const requiredOffsets = requiredSuperDarkOffsets(provedDispatchCells);
  const reasons: string[] = [];

  if (dispatchCells.length === 0) {
    reasons.push("no super-dark К БП R dispatch cell is marked in the layout");
  } else if (provedDispatchCells.length === 0) {
    reasons.push("no super-dark dispatch register has a proved FA..FF selector value");
  }

  for (const offset of requiredOffsets) {
    const formal = 0xfa + offset;
    const entryAddress = 48 + offset;
    const continuationAddress = 1 + offset;
    const entry = byAddress.get(entryAddress);
    const continuation = byAddress.get(continuationAddress);

    if (entry === undefined) {
      reasons.push(`${hex(formal)} has no physical entry cell at ${entryAddress}`);
      continue;
    }
    if (continuation === undefined) {
      reasons.push(`${hex(formal)} has no continuation cell at ${continuationAddress}`);
      continue;
    }
    if (!entry.roles.includes("exec")) {
      reasons.push(`${hex(formal)} entry ${entryAddress} is not executable`);
      continue;
    }
    if (getOpcode(entry.opcode).takesAddress) {
      reasons.push(`${hex(formal)} entry ${entryAddress} is a two-cell address-taking command`);
      continue;
    }
    if (!continuation.roles.includes("exec")) {
      reasons.push(`${hex(formal)} continuation ${continuationAddress} is not executable`);
      continue;
    }

    pairs.push({
      formal,
      entryAddress,
      continuationAddress,
      entryOpcode: entry.opcode,
      continuationOpcode: continuation.opcode,
    });
  }

  if (pairs.length !== requiredOffsets.length && reasons.length === 0) {
    reasons.push("FA..FF did not produce the required super-dark entry/continuation pairs");
  }

  return {
    proved: provedDispatchCells.length > 0 && pairs.length === requiredOffsets.length && reasons.length === 0,
    pairs,
    dispatchCells,
    reasons,
  };
}

function requiredSuperDarkOffsets(dispatchCells: readonly SuperDarkDispatchCell[]): number[] {
  const offsets = new Set<number>();
  for (const cell of dispatchCells) {
    const value = cell.selectorValue?.trim().toUpperCase().replace(/\s+/gu, "");
    const formal = value === undefined ? undefined : /^F([A-F])$/u.exec(value);
    if (formal) {
      offsets.add(Number.parseInt(formal[1]!, 16) - 0x0a);
    } else if (isSuperDarkSelectorValue(value)) {
      for (let offset = 0; offset <= 5; offset += 1) offsets.add(offset);
    }
  }
  return [...offsets].sort((a, b) => a - b);
}

function collectSuperDarkDispatchCells(
  layout: readonly LayoutIrCell[],
  selectorValues: Readonly<Record<string, string>>,
): SuperDarkDispatchCell[] {
  const cells: SuperDarkDispatchCell[] = [];
  for (const cell of layout) {
    if (cell.opcode < 0x87 || cell.opcode > 0x8e) continue;
    if (!/\bsuper[- ]dark\b/iu.test(cell.tactic)) continue;
    const register = registerForIndirectJumpOpcode(cell.opcode);
    const selectorValue = selectorValueForRegister(selectorValues, register);
    cells.push({
      address: cell.address,
      opcode: cell.opcode,
      register,
      tactic: cell.tactic,
      ...(selectorValue === undefined ? {} : { selectorValue }),
    });
  }
  return cells;
}

function registerForIndirectJumpOpcode(opcode: number): string {
  const registers = ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"];
  return registers[opcode - 0x80] ?? "?";
}

function selectorValueForRegister(
  selectorValues: Readonly<Record<string, string>>,
  register: string,
): string | undefined {
  const aliases = [
    register,
    register.toUpperCase(),
    `R${register}`,
    `R${register.toUpperCase()}`,
    `r${register}`,
  ];
  for (const alias of aliases) {
    const value = selectorValues[alias];
    if (value !== undefined) return value;
  }
  return undefined;
}

function isSuperDarkSelectorValue(value: string | undefined): boolean {
  if (value === undefined) return false;
  const normalized = value.trim().toUpperCase().replace(/\s+/gu, "");
  if (/^F[A-F]$/u.test(normalized)) return true;
  return normalized === "FA..FF" ||
    normalized === "FA-FF" ||
    normalized === "FA…FF" ||
    normalized === "SUPER-DARK" ||
    normalized === "SUPER_DARK";
}
