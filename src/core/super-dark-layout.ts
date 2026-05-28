import { getOpcode } from "./opcodes.ts";
import type { LayoutIrCell } from "./types.ts";

export interface SuperDarkDispatchCell {
  address: number;
  opcode: number;
  register: string;
  tactic: string;
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

function hex(value: number): string {
  return value.toString(16).toUpperCase().padStart(2, "0");
}

export function verifySuperDarkSuffixLayout(layout: readonly LayoutIrCell[]): SuperDarkLayoutProof {
  const byAddress = new Map(layout.map((cell) => [cell.address, cell]));
  const pairs: SuperDarkLayoutPair[] = [];
  const dispatchCells = collectSuperDarkDispatchCells(layout);
  const reasons: string[] = [];

  if (dispatchCells.length === 0) {
    reasons.push("no super-dark К БП R dispatch cell is marked in the layout");
  }

  for (let offset = 0; offset <= 5; offset += 1) {
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

  if (pairs.length !== 6 && reasons.length === 0) {
    reasons.push("FA..FF did not produce all six super-dark entry/continuation pairs");
  }

  return {
    proved: dispatchCells.length > 0 && pairs.length === 6 && reasons.length === 0,
    pairs,
    dispatchCells,
    reasons,
  };
}

function collectSuperDarkDispatchCells(layout: readonly LayoutIrCell[]): SuperDarkDispatchCell[] {
  const cells: SuperDarkDispatchCell[] = [];
  for (const cell of layout) {
    if (cell.opcode < 0x87 || cell.opcode > 0x8e) continue;
    if (!/\bsuper[- ]dark\b/iu.test(cell.tactic)) continue;
    cells.push({
      address: cell.address,
      opcode: cell.opcode,
      register: registerForIndirectJumpOpcode(cell.opcode),
      tactic: cell.tactic,
    });
  }
  return cells;
}

function registerForIndirectJumpOpcode(opcode: number): string {
  const registers = ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"];
  return registers[opcode - 0x80] ?? "?";
}
