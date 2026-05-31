import { formatAddress, getOpcode } from "./opcodes.ts";
import type {
  DeliveryMode,
  ProgramPatchReport,
  ProgramPatchStepReport,
  ResolvedStep,
} from "./types.ts";

const PLACEHOLDER_OPCODE = 0x54;

const F_PREFIX_REGISTERS = [
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
] as const;

export function buildProgramPatchReport(
  steps: readonly ResolvedStep[],
  delivery: DeliveryMode,
): ProgramPatchReport | undefined {
  if (delivery !== "manual") return undefined;
  return buildManualProgramPatchReport(steps);
}

export function buildManualProgramPatchReport(
  steps: readonly ResolvedStep[],
): ProgramPatchReport | undefined {
  const patchSteps: ProgramPatchStepReport[] = [];
  const warnings: string[] = [];

  for (const step of steps) {
    if (getOpcode(step.opcode).enterable.includes("manual")) continue;
    const patch = patchStep(step);
    if (patch !== undefined) {
      patchSteps.push(patch);
      continue;
    }
    warnings.push(`No supported manual patch sequence for ${step.hex} at ${formatPatchAddress(step.address)}.`);
  }

  if (patchSteps.length === 0 && warnings.length === 0) return undefined;
  return {
    steps: patchSteps,
    warnings,
    reason: "manual delivery needs service-mode patching for not-normally-entered opcodes",
  };
}

function patchStep(step: ResolvedStep): ProgramPatchStepReport | undefined {
  return eggFPrefixPatch(step) ?? returnFPrefixPatch(step);
}

function eggFPrefixPatch(step: ResolvedStep): ProgramPatchStepReport | undefined {
  if (step.address < 50 || step.address > 59) return undefined;
  if (step.opcode < 0xf0 || step.opcode > 0xf9) return undefined;
  const addressDigit = step.address - 50;
  const opcodeDigit = step.opcode - 0xf0;
  return makePatchStep(
    step,
    "egg-f-prefix",
    [
      "F АВТ",
      "5",
      "0",
      "F 10^x",
      "F x^2",
      "ВП",
      String(addressDigit),
      String(opcodeDigit),
      ".",
      "0",
    ],
    `make ЕГГ0Г, then ВП ${addressDigit} ${opcodeDigit} . 0 writes ${step.hex} at ${formatPatchAddress(step.address)}`,
  );
}

function returnFPrefixPatch(step: ResolvedStep): ProgramPatchStepReport | undefined {
  if (step.address < 34 || step.address > 44) return undefined;
  const suffix = step.address - 30;
  if (step.opcode !== 0xf0 + suffix || suffix > 0xe) return undefined;
  const register = F_PREFIX_REGISTERS[suffix];
  if (register === undefined) return undefined;
  return makePatchStep(
    step,
    "return-f-prefix",
    [
      "F АВТ",
      "В/О",
      `К ПП ${register}`,
    ],
    `В/О К ПП ${register} writes ${step.hex} at ${formatPatchAddress(step.address)}`,
  );
}

function makePatchStep(
  step: ResolvedStep,
  method: ProgramPatchStepReport["method"],
  keys: string[],
  note: string,
): ProgramPatchStepReport {
  const placeholder = getOpcode(PLACEHOLDER_OPCODE);
  return {
    address: step.address,
    opcode: step.opcode,
    hex: step.hex,
    mnemonic: step.mnemonic,
    placeholderOpcode: PLACEHOLDER_OPCODE,
    placeholderHex: placeholder.hex,
    placeholderMnemonic: placeholder.name,
    method,
    keys,
    note,
  };
}

export function formatPatchAddress(address: number): string {
  try {
    return formatAddress(address);
  } catch {
    return `>${address.toString(16).toUpperCase()}`;
  }
}
