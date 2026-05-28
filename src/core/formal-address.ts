export type FormalAddressKind =
  | "official"
  | "short-side"
  | "long-side"
  | "dark"
  | "super-dark";

export interface FormalAddressInfo {
  readonly opcode: number;
  readonly label: string;
  readonly ordinal: number;
  readonly actual: number;
  readonly kind: FormalAddressKind;
  readonly oneCommand: boolean;
  readonly extra?: number;
}

function hexByte(value: number): string {
  return value.toString(16).toUpperCase().padStart(2, "0");
}

function assertByte(value: number): void {
  if (!Number.isInteger(value) || value < 0 || value > 0xff) {
    throw new Error(`Formal MK-61 address byte ${value} is out of range`);
  }
}

export function formalAddressOrdinal(opcode: number): number {
  assertByte(opcode);
  const high = opcode >> 4;
  const low = opcode & 0x0f;
  return high * 10 + low;
}

export function officialAddressToOpcode(address: number): number {
  if (!Number.isInteger(address) || address < 0 || address > 104) {
    throw new Error(`Physical MK-61 program address ${address} is outside 00..A4`);
  }
  if (address <= 99) {
    const tens = Math.floor(address / 10);
    const ones = address % 10;
    return tens * 16 + ones;
  }
  return 0xa0 + (address - 100);
}

export function formatFormalAddressOpcode(opcode: number): string {
  assertByte(opcode);
  return hexByte(opcode);
}

export function formatOfficialAddress(address: number): string {
  return formatFormalAddressOpcode(officialAddressToOpcode(address));
}

export function parseFormalAddressOpcode(text: string): number | undefined {
  const normalized = text.trim().replace(/[.-]/gu, "A").toUpperCase();
  if (!/^[0-9A-F]{2}$/u.test(normalized)) return undefined;
  return Number.parseInt(normalized, 16);
}

export function formalAddressInfo(opcode: number): FormalAddressInfo {
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
      oneCommand: false,
    };
  }

  if (ordinal <= 111) {
    return {
      opcode,
      label,
      ordinal,
      actual: ordinal - 105,
      kind: "short-side",
      oneCommand: false,
    };
  }

  if (ordinal <= 159) {
    return {
      opcode,
      label,
      ordinal,
      actual: ordinal - 112,
      kind: ordinal >= 120 ? "dark" : "long-side",
      oneCommand: false,
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
      extra: ordinal - 159,
    };
  }

  // The two-nibble command byte can encode ordinals up to 165 (FF).
  throw new Error(`Formal MK-61 address ${label} maps past the known address space`);
}
