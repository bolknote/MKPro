import { registerIndex } from "./opcodes.ts";
import {
  formalAddressInfo,
  officialAddressToOpcode,
  type FormalAddressInfo,
} from "./formal-address.ts";
import type { RegisterName } from "./types.ts";

const MEMORY_TARGET_WITH_ZERO_TENS: RegisterName[] = [
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
  "0",
];

const MEMORY_TARGET_WITH_NONZERO_TENS: RegisterName[] = [
  "a",
  "b",
  "c",
  "d",
  "e",
  "0",
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
  const tail = transformedTailPair(transformed);
  if (tail === undefined) return undefined;
  const targetTable = tail.tens === 0 ? MEMORY_TARGET_WITH_ZERO_TENS : MEMORY_TARGET_WITH_NONZERO_TENS;
  return targetTable[tail.ones];
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
    return transformDecimalSelectorValue(value, mutation);
  }

  const normalized = value.trim().replace(/^0x/iu, "").replace(",", ".").toLowerCase();
  if (!/^-?[0-9a-f]+(?:\.\d+)?$/iu.test(normalized)) return undefined;
  if (mutation === "stable" && !normalized.startsWith("-") && /[a-f]/iu.test(normalized)) return normalized;
  const decimal = Number(normalized);
  if (!Number.isFinite(decimal)) return undefined;
  return transformDecimalSelectorValue(decimal, mutation);
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
  const tail = transformedTailPair(transformed);
  if (tail === undefined) return 0;
  return tail.hex ? tail.tens * 16 + tail.ones : tail.tens * 10 + tail.ones;
}

function formalOpcodeForFlowTarget(transformed: string, flowTarget: number): number {
  const normalized = transformed.trim().toLowerCase();
  if (/^-?[0-9a-f]+$/iu.test(normalized) && /[a-f]/iu.test(normalized)) {
    return flowTarget;
  }
  return officialAddressToOpcode(flowTarget);
}

function transformDecimalSelectorValue(
  value: number,
  mutation: IndirectSelectorMutation,
): string | undefined {
  const integer = Math.trunc(value);
  const delta = mutationDelta(mutation);
  if (integer >= 0) {
    if (integer === 0 && delta < 0) return "-99999999";
    return String(integer + delta);
  }

  const transformed = negativeIntegerMantissa(integer);
  if (delta === 0) return `-${transformed}`;
  const next = transformed + delta;
  if (next <= 0) return "0";
  if (next >= 100000000) return "0";
  return `-${String(next).padStart(8, "0")}`;
}

function negativeIntegerMantissa(value: number): number {
  const digits = String(Math.abs(Math.trunc(value))).slice(-8);
  const padded = digits.padStart(8, "9");
  return Number(padded);
}

function transformedTailPair(transformed: string): { tens: number; ones: number; hex: boolean } | undefined {
  const normalized = transformed.trim().toLowerCase();
  if (!/^-?[0-9a-f]+$/iu.test(normalized)) return undefined;
  const negative = normalized.startsWith("-");
  const digits = negative ? normalized.slice(1) : normalized;
  if (digits.length === 0) return undefined;
  const fill = negative ? "9" : "0";
  const pair = digits.length === 1 ? `${fill}${digits}` : digits.slice(-2);
  const tens = Number.parseInt(pair[0]!, 16);
  const ones = Number.parseInt(pair[1]!, 16);
  if (!Number.isFinite(tens) || !Number.isFinite(ones) || tens < 0 || tens > 0xf || ones < 0 || ones > 0xf) {
    return undefined;
  }
  return { tens, ones, hex: /[a-f]/iu.test(pair) };
}

export const IndirectAddressModel = {
  evaluate: evaluateIndirectAddress,
  memoryTargetFromTransformed,
  mutationForRegister: indirectSelectorMutation,
  stableSelector: isStableIndirectSelector,
  superDarkTarget,
} as const;
