import { formalAddressInfo } from "./formal-address.ts";
import { formatAddress } from "./opcodes.ts";
import type { CompileResult, PreloadReport, ResolvedStep } from "./types.ts";

const HEX_COLUMNS = 8;
const MK61_HEX_SETUP_DIGITS: Record<string, string> = {
  A: "-",
  B: "L",
  C: "С",
  D: "Г",
  E: "Е",
  F: "_",
};

export function formatListing(result: CompileResult): string {
  const setupProgram = result.report.setupProgram;
  const manualRows = formatManualSetupRows(result);
  if (setupProgram !== undefined || manualRows.length > 0) {
    return [
      "# Setup Listing",
      formatListingRows([
        ...manualRows,
        ...(setupProgram?.steps ?? []).map(stepToListingRow),
      ]),
      "",
      "# Main Listing",
      formatListingSteps(result.steps),
    ].join("\n");
  }

  const setupBlock = formatSetupBlock(result);
  if (setupBlock !== undefined) {
    return [
      "Setup Block:",
      `  ${setupBlock}`,
      "",
      formatListingSteps(result.steps),
    ].join("\n");
  }

  return formatListingSteps(result.steps);
}

interface ListingRow {
  address: number | string;
  hex: string;
  mnemonic: string;
  comment?: string;
}

function formatListingSteps(steps: readonly ResolvedStep[]): string {
  return formatListingRows(steps.map(stepToListingRow));
}

function formatListingRows(rows: readonly ListingRow[]): string {
  const lines = [
    " Step | Code | Command                 | Comment",
    "------+------+-------------------------+----------------",
  ];
  for (const row of rows) {
    const address = formatListingAddress(row.address).padStart(4, " ");
    const code = row.hex.padStart(2, " ");
    const command = row.mnemonic.padEnd(23, " ");
    const comments = [row.comment]
      .filter((value): value is string => Boolean(value))
      .join("; ");
    lines.push(comments.length > 0
      ? ` ${address} |  ${code}  | ${command} | ${comments}`
      : ` ${address} |  ${code}  | ${command} |`);
  }
  return lines.join("\n");
}

function stepToListingRow(step: ResolvedStep): ListingRow {
  const row: ListingRow = {
    address: step.address,
    hex: step.hex,
    mnemonic: step.mnemonic,
  };
  if (step.comment !== undefined) row.comment = step.comment;
  return row;
}

export function formatHex(result: CompileResult): string {
  return formatHexSteps(result.steps);
}

function formatHexSteps(steps: readonly ResolvedStep[]): string {
  const rows: string[] = [];
  for (let i = 0; i < steps.length; i += HEX_COLUMNS) {
    const chunk = steps.slice(i, i + HEX_COLUMNS).map((step) => step.hex);
    const address = formatStepAddress(i).padStart(2, "0");
    rows.push(`${address}: ${chunk.join(" ")}`);
  }
  return rows.join("\n");
}

export function formatProgramTokens(steps: readonly ResolvedStep[]): string {
  return steps.map((step) => step.hex).join("\n");
}

function formatStepAddress(address: number): string {
  try {
    return formatAddress(address);
  } catch {
    return `>${address.toString(16).toUpperCase()}`;
  }
}

function formatListingAddress(address: number | string): string {
  return typeof address === "number" ? formatStepAddress(address) : address;
}

export function formatJson(result: CompileResult): string {
  const { ir, ...report } = result.report;
  return JSON.stringify(
    {
      steps: result.steps,
      report: {
        ...report,
        ir: {
          lowered: ir.lowered,
          v2: ir.v2,
          intentNodes: ir.intentNodes,
          effectOps: ir.effectOps,
          layoutCells: ir.layoutCells,
        },
      },
      diagnostics: result.diagnostics,
    },
    null,
    2,
  );
}

export function formatSetupBlock(result: CompileResult): string | undefined {
  const assignments = result.report.preloads
    .map(formatSetupAssignment)
    .filter((assignment): assignment is string => assignment !== undefined);
  if (assignments.length === 0) return undefined;
  return `\`${assignments.join("; ")}\``;
}

export function formatSetupProgram(result: CompileResult): string | undefined {
  if (result.report.setupProgram === undefined) return undefined;
  return formatProgramTokens(result.report.setupProgram.steps);
}

export function formatKeys(result: CompileResult): string {
  const lines: string[] = [];
  const setupProgram = result.report.setupProgram;
  const manualSetupKeys = formatManualSetupKeys(result);
  if (setupProgram !== undefined) {
    lines.push(...formatStepKeys(setupProgram.steps));
    lines.push("В/О", ...manualSetupKeys, "С/П");
  } else {
    lines.push(...manualSetupKeys);
    lines.push(...formatSetupPreloadKeys(result.report.preloads));
  }
  lines.push(...formatStepKeys(result.steps));
  lines.push("В/О", "С/П");
  return lines.join("\n");
}

function formatStepKeys(steps: readonly ResolvedStep[]): string[] {
  return steps.map((step) => step.mnemonic);
}

function formatSetupPreloadKeys(preloads: readonly PreloadReport[]): string[] {
  return preloads.flatMap((preload) => {
    const value = executableSetupValue(preload.value);
    return value === undefined ? [] : [value, `X->П ${preload.register}`];
  });
}

function formatManualSetupRows(result: CompileResult): ListingRow[] {
  return manualSetupInputs(result).map((input) => ({
    address: "--",
    hex: "-",
    mnemonic: `enter ${input.name}`,
    comment: `${formatManualInputRange(input)} in ${input.stack}`,
  }));
}

function formatManualSetupKeys(result: CompileResult): string[] {
  return manualSetupInputs(result).map((input) =>
    `<enter ${input.name}: ${formatManualInputValue(input)} in ${input.stack}>`
  );
}

interface ManualSetupInput {
  name: string;
  stack: "X" | "Y";
  min?: number;
  max?: number;
}

function manualSetupInputs(result: CompileResult): ManualSetupInput[] {
  const inputs: ManualSetupInput[] = [];
  const seen = new Set<string>();
  for (const preload of result.report.preloads) {
    const stack = stackPreloadSource(preload.value);
    if (stack === undefined) continue;
    const field = result.ast.v2?.state.find((candidate) =>
      result.report.registers[candidate.name] === preload.register &&
      candidate.initial === preload.value
    );
    const name = field?.name ?? `R${preload.register}`;
    const key = `${name}:${stack}`;
    if (seen.has(key)) continue;
    seen.add(key);
    const input: ManualSetupInput = {
      name,
      stack,
    };
    if (field?.min !== undefined) input.min = field.min;
    if (field?.max !== undefined) input.max = field.max;
    inputs.push(input);
  }
  return inputs;
}

function stackPreloadSource(value: string): "X" | "Y" | undefined {
  const match = /^stack\.(X|Y)$/u.exec(value.trim());
  return match?.[1] as "X" | "Y" | undefined;
}

function formatManualInputRange(input: ManualSetupInput): string {
  return `enter ${formatManualInputValue(input)}`;
}

function formatManualInputValue(input: ManualSetupInput): string {
  if (input.min !== undefined && input.max !== undefined) return `any value ${input.min}..${input.max}`;
  return "a value";
}

function formatSetupAssignment(preload: PreloadReport): string | undefined {
  if (!isSetupLiteral(preload.value)) return undefined;
  return `R${preload.register}=${formatSetupValue(preload.value)}`;
}

function isSetupLiteral(value: string): boolean {
  return /^-?[0-9A-FАВСДЕLСГЕ_\-,.]+(?:E-?[0-9]{1,2})?$/iu.test(value);
}

function formatSetupValue(value: string): string {
  const normalized = value.toUpperCase();
  if (isScientificDecimal(normalized)) return value;
  if (!/^[0-9A-F]+$/iu.test(normalized) || !/[A-F]/iu.test(normalized)) return value;
  return [...normalized].map((digit) => MK61_HEX_SETUP_DIGITS[digit] ?? digit).join("");
}

function executableSetupValue(value: string): string | undefined {
  const normalized = value.trim().toUpperCase();
  if (/^-?\d+(?:[,.]\d+)?(?:E-?\d{1,2})?$/iu.test(normalized)) return normalized.replace(",", ".");
  if (/^[A-F][0-9A-F]$/iu.test(normalized)) return String(formalAddressInfo(Number.parseInt(normalized, 16)).actual);
  return undefined;
}

function isScientificDecimal(value: string): boolean {
  return /^-?\d+(?:[,.]\d+)?E-?\d{1,2}$/iu.test(value);
}

export function formatExplain(result: CompileResult): string {
  const lines = [
    `MK-Pro compile report`,
    `Steps: ${result.report.steps}/${result.report.budget}`,
    `Delivery: ${result.report.delivery}`,
    `Optimizer: exact maximum`,
    `Machine: ${result.report.machine}`,
    `Optimizer rules: automatic=${result.report.optimizer.automatic ? "yes" : "no"}; active=${result.report.optimizer.active}, considered=${result.report.optimizer.considered}, candidates=${result.report.optimizer.candidate}, planned=${result.report.optimizer.planned}`,
    `Budget: official=${result.report.budgetReport.officialSteps}, extra=${result.report.budgetReport.extraCells}, physical=${result.report.budgetReport.totalPhysicalCells}`,
    `Intent IR: lowered=${result.report.ir.lowered ? "yes" : "no"}, v2=${result.report.ir.v2 ? "yes" : "no"}; intent=${result.report.ir.intentNodes}, effects=${result.report.ir.effectOps}, cells=${result.report.ir.layoutCells}`,
  ];
  if (result.report.reference) {
    const reference = result.report.reference;
    lines.splice(
      4,
      0,
      `Reference: ${reference.name}; span=${reference.referenceSpan}, entries=${reference.referenceEntries}, gaps=${reference.referenceGaps.length > 0 ? reference.referenceGaps.join(",") : "none"}, compiled=${reference.compiledSteps}, delta=${reference.delta}, parity=${reference.parity}`,
    );
  }
  if (result.report.preloads.length > 0) {
    lines.push("", "Preloads:");
    for (const preload of result.report.preloads) {
      const label = preloadSourceLabel(result, preload);
      const target = label === undefined ? `R${preload.register}` : `${label} -> R${preload.register}`;
      lines.push(`  ${target}: ${preload.value}${preload.countsAgainstProgram ? "" : " (outside program cells)"}`);
      if (preload.setupProgram !== undefined) {
        lines.push(`    setup: ${preload.setupProgram}`);
      }
      if (preload.setupNote !== undefined) {
        lines.push(`    note: ${preload.setupNote}`);
      }
    }
    const setupBlock = formatSetupBlock(result);
    if (setupBlock !== undefined) {
      lines.push("", "Setup Block:");
      lines.push(`  ${setupBlock}`);
    }
  }
  if (result.report.setupProgram !== undefined) {
    lines.push("", "Setup Program:");
    lines.push(`  Reason: ${result.report.setupProgram.reason.trimEnd()}`);
    lines.push(formatHexSteps(result.report.setupProgram.steps).split("\n").map((line) => `  ${line}`).join("\n"));
  }
  lines.push("", "Registers:");
  const registerEntries = Object.entries(result.report.registers).sort(
    ([a], [b]) => a.localeCompare(b),
  );
  for (const [name, register] of registerEntries) {
    lines.push(`  ${name}: R${register}`);
  }

  lines.push("", "Labels:");
  for (const [name, address] of Object.entries(result.report.labels)) {
    lines.push(`  ${name}: ${address}`);
  }

  lines.push("", "Optimizations:");
  for (const optimization of result.report.optimizations) {
    lines.push(`  - ${optimization.name}: ${optimization.detail}`);
  }

  if (result.report.candidates.length > 0) {
    lines.push("", "Candidates:");
    for (const candidate of result.report.candidates) {
      const marker = candidate.selected ? "*" : "-";
      lines.push(`  ${marker} ${candidate.site}/${candidate.variant}: ${candidate.steps} steps; ${candidate.reason}`);
    }
  }

  if ((result.report.ir.lowered || result.report.ir.v2) && result.report.optimizer.capabilities.length > 0) {
    lines.push("", "Optimizer Capabilities:");
    for (const capability of result.report.optimizer.capabilities.slice(0, 12)) {
      lines.push(`  - ${capability.id}: ${capability.status}; ${capability.detail}`);
    }
  }

  if (result.report.machineFeaturesUsed.length > 0) {
    lines.push("", "Machine Features Used:");
    for (const feature of result.report.machineFeaturesUsed) {
      lines.push(`  - ${feature.id}: ${feature.detail}`);
    }
  }

  if (result.report.proofs.length > 0) {
    lines.push("", "Proofs:");
    for (const proof of result.report.proofs) {
      lines.push(`  - ${proof.id}: ${proof.status}; ${proof.detail}`);
    }
  }

  if (result.report.rejectedCandidates.length > 0) {
    lines.push("", "Rejected Candidates:");
    for (const candidate of result.report.rejectedCandidates.slice(0, 8)) {
      lines.push(`  - ${candidate.site}/${candidate.variant}: ${candidate.steps} steps; ${candidate.reason}`);
    }
  }

  if (result.report.cellRoles.length > 0) {
    const interesting = result.report.cellRoles.filter((cell) => cell.roles.length > 1).slice(0, 12);
    if (interesting.length > 0) {
      lines.push("", "Cell Roles:");
      for (const cell of interesting) {
        lines.push(`  ${cell.address}: ${cell.hex} ${cell.roles.join(", ")}${cell.note ? ` - ${cell.note}` : ""}`);
      }
    }
  }

  if (result.report.warnings.length > 0) {
    lines.push("", "Warnings:");
    for (const warning of result.report.warnings) lines.push(`  - ${warning}`);
  }
  return lines.join("\n");
}

function preloadSourceLabel(result: CompileResult, preload: PreloadReport): string | undefined {
  const field = result.ast.v2?.state.find((candidate) =>
    result.report.registers[candidate.name] === preload.register &&
    candidate.initial === preload.value
  );
  if (field !== undefined) return field.name;
  return undefined;
}

export function formatAll(result: CompileResult): string {
  const sections = [
    "# Listing",
    formatListing(result),
    "",
    "# Hex",
    formatHex(result),
  ];
  const setupProgram = formatSetupProgram(result);
  if (setupProgram !== undefined) {
    sections.push("", "# Setup Program", setupProgram);
  }
  sections.push(
    "",
    "# JSON",
    formatJson(result),
  );
  return sections.join("\n");
}
