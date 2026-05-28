import { registerIndex } from "./opcodes.ts";
import {
  formalAddressInfo,
  officialAddressToOpcode,
  type FormalAddressInfo,
} from "./formal-address.ts";
import type { RegisterName } from "./types.ts";

const REGISTERS_BY_INDEX: RegisterName[] = [
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

export type IndirectSelectorMutation = "pre-decrement" | "pre-increment" | "stable";

export type IndirectOperationKind = "flow" | "memory";

export interface SuperDarkIndirectTarget {
  formal: number;
  entryAddress: number;
  continuationAddress: number;
}

export interface IndirectAddressEvaluation {
  selector: RegisterName;
  mutation: IndirectSelectorMutation;
  operation: IndirectOperationKind;
  transformed: string;
  formalAddress?: FormalAddressInfo;
  flowTarget?: number;
  actualFlowTarget?: number;
  memoryTarget?: RegisterName;
  resultValue?: string;
  superDark?: SuperDarkIndirectTarget;
}

export function indirectSelectorMutation(register: RegisterName): IndirectSelectorMutation {
  const index = registerIndex(register);
  if (index <= 3) return "pre-decrement";
  if (index <= 6) return "pre-increment";
  return "stable";
}

export function isStableIndirectSelector(register: RegisterName): boolean {
  return indirectSelectorMutation(register) === "stable";
}

export function evaluateIndirectAddress(
  selector: RegisterName,
  value: number | string,
  operation: IndirectOperationKind,
): IndirectAddressEvaluation | undefined {
  const mutation = indirectSelectorMutation(selector);
  const fractional = isPositiveFractional(value);
  if (selector === "0" && fractional) {
    const formalAddress = formalAddressInfo(0x99);
    return {
      selector,
      mutation,
      operation,
      transformed: "99",
      ...(operation === "flow"
        ? { flowTarget: 99, actualFlowTarget: formalAddress.actual, formalAddress }
        : { memoryTarget: "3" as RegisterName }),
      resultValue: "-99999999",
    };
  }

  const transformed = transformSelectorValue(value, mutation);
  if (transformed === undefined) return undefined;

  if (operation === "flow") {
    const flowTarget = flowTargetFromTransformed(transformed);
    const formalAddress = formalAddressInfo(formalOpcodeForFlowTarget(transformed, flowTarget));
    const result: IndirectAddressEvaluation = {
      selector,
      mutation,
      operation,
      transformed,
      formalAddress,
      flowTarget,
      actualFlowTarget: formalAddress.actual,
      resultValue: transformed,
    };
    const superDark = superDarkTarget(formalAddress.opcode);
    if (superDark !== undefined) result.superDark = superDark;
    return result;
  }

  const memoryTarget = memoryTargetFromTransformed(transformed);
  if (memoryTarget === undefined) return undefined;
  return {
    selector,
    mutation,
    operation,
    transformed,
    memoryTarget,
    resultValue: transformed,
  };
}

export function memoryTargetFromTransformed(transformed: string): RegisterName | undefined {
  const normalized = transformed.trim().toLowerCase();
  if (/^-?\d+$/u.test(normalized)) {
    return REGISTERS_BY_INDEX[positiveModulo(Number(normalized), 10)];
  }
  const last = normalized.at(-1);
  if (last === undefined) return undefined;
  const nibble = Number.parseInt(last, 16);
  if (!Number.isFinite(nibble) || nibble < 0 || nibble > 0xf) return undefined;
  if (nibble === 0xf) return "0";
  return REGISTERS_BY_INDEX[nibble];
}

export function superDarkTarget(formalTarget: number): SuperDarkIndirectTarget | undefined {
  const info = formalAddressInfo(formalTarget);
  if (info.kind !== "super-dark" || info.extra === undefined) return undefined;
  return {
    formal: info.opcode,
    entryAddress: info.actual,
    continuationAddress: info.extra,
  };
}

function transformSelectorValue(
  value: number | string,
  mutation: IndirectSelectorMutation,
): string | undefined {
  if (typeof value === "number") {
    if (!Number.isFinite(value)) return undefined;
    const integer = Math.trunc(value) + mutationDelta(mutation);
    return String(integer);
  }

  const normalized = value.trim().replace(/^0x/iu, "").toLowerCase();
  if (!/^[0-9a-f]+$/iu.test(normalized)) return undefined;
  if (mutation === "stable" && /[a-f]/iu.test(normalized)) return normalized;
  const decimal = Number(normalized);
  if (!Number.isFinite(decimal)) return undefined;
  return String(Math.trunc(decimal) + mutationDelta(mutation));
}

function isPositiveFractional(value: number | string): boolean {
  const numeric = typeof value === "number" ? value : Number(value.trim().replace(",", "."));
  return numeric !== undefined && numeric > 0 && numeric < 1;
}

function mutationDelta(mutation: IndirectSelectorMutation): number {
  if (mutation === "pre-decrement") return -1;
  if (mutation === "pre-increment") return 1;
  return 0;
}

function flowTargetFromTransformed(transformed: string): number {
  const normalized = transformed.trim().toLowerCase();
  if (/^[0-9a-f]+$/iu.test(normalized) && /[a-f]/iu.test(normalized)) {
    return Number.parseInt(normalized.slice(-2), 16);
  }
  const numeric = Number(normalized);
  if (!Number.isFinite(numeric)) return 0;
  return positiveModulo(Math.trunc(numeric), 100);
}

function formalOpcodeForFlowTarget(transformed: string, flowTarget: number): number {
  const normalized = transformed.trim().toLowerCase();
  if (/^[0-9a-f]+$/iu.test(normalized) && /[a-f]/iu.test(normalized)) {
    return flowTarget;
  }
  return officialAddressToOpcode(flowTarget);
}

function positiveModulo(value: number, modulus: number): number {
  return ((value % modulus) + modulus) % modulus;
}

export const IndirectAddressModel = {
  evaluate: evaluateIndirectAddress,
  memoryTargetFromTransformed,
  mutationForRegister: indirectSelectorMutation,
  stableSelector: isStableIndirectSelector,
  superDarkTarget,
} as const;
