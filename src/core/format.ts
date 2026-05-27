import { formatAddress } from "./opcodes.ts";
import type { CompileResult } from "./types.ts";

const HEX_COLUMNS = 8;

export function formatListing(result: CompileResult): string {
  const lines = [
    " Step | Code | Command                 | Comment",
    "------+------+-------------------------+----------------",
  ];
  for (const step of result.steps) {
    const address = formatAddress(step.address).padStart(4, " ");
    const command = step.mnemonic.padEnd(23, " ");
    const comments = [
      step.comment,
      step.unsafeReason ? `unsafe-unverified: ${step.unsafeReason}` : undefined,
    ]
      .filter((value): value is string => Boolean(value))
      .join("; ");
    lines.push(` ${address} |  ${step.hex}  | ${command} | ${comments}`);
  }
  return lines.join("\n");
}

export function formatHex(result: CompileResult): string {
  const rows: string[] = [];
  for (let i = 0; i < result.steps.length; i += HEX_COLUMNS) {
    const chunk = result.steps.slice(i, i + HEX_COLUMNS).map((step) => step.hex);
    const address = formatAddress(i).padStart(2, "0");
    rows.push(`${address}: ${chunk.join(" ")}`);
  }
  return rows.join("\n");
}

export function formatJson(result: CompileResult): string {
  const { ir, ...report } = result.report;
  return JSON.stringify(
    {
      steps: result.steps,
      report: {
        ...report,
        ir: {
          compact: ir.v1,
          highLevel: ir.v2,
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

export function formatExplain(result: CompileResult): string {
  const lines = [
    `M61 compile report`,
    `Steps: ${result.report.steps}/${result.report.budget}`,
    `Delivery: ${result.report.delivery}`,
    `Optimizer: ${result.report.opt}`,
    `Target profile: ${result.report.targetProfile}`,
    `Optimizer rules: automatic=${result.report.optimizer.automatic ? "yes" : "no"}; active=${result.report.optimizer.active}, candidates=${result.report.optimizer.candidate}, blocked=${result.report.optimizer.blocked}, planned=${result.report.optimizer.planned}`,
    `Budget: official=${result.report.budgetReport.officialSteps}, extra=${result.report.budgetReport.extraCells}, physical=${result.report.budgetReport.totalPhysicalCells}`,
    `Intent IR: compact=${result.report.ir.v1 ? "yes" : "no"}, highLevel=${result.report.ir.v2 ? "yes" : "no"}; intent=${result.report.ir.intentNodes}, effects=${result.report.ir.effectOps}, cells=${result.report.ir.layoutCells}`,
  ];
  if (result.report.reference) {
    const reference = result.report.reference;
    lines.splice(
      4,
      0,
      `Reference: ${reference.name}; reference=${reference.referenceSteps}, compiled=${reference.compiledSteps}, delta=${reference.delta}, parity=${reference.parity}`,
    );
  }
  if (result.report.preloads.length > 0) {
    lines.push("", "Preloads:");
    for (const preload of result.report.preloads) {
      lines.push(`  R${preload.register}: ${preload.value}${preload.countsAgainstProgram ? "" : " (outside program cells)"}`);
    }
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
    lines.push(
      `  - ${optimization.name}: ${optimization.detail}${optimization.unsafe ? " [unsafe-unverified]" : ""}`,
    );
  }

  if (result.report.candidates.length > 0) {
    lines.push("", "Candidates:");
    for (const candidate of result.report.candidates) {
      const marker = candidate.selected ? "*" : "-";
      lines.push(
        `  ${marker} ${candidate.site}/${candidate.variant}: ${candidate.steps} steps; ${candidate.reason}${candidate.unsafe ? " [unsafe-unverified]" : ""}`,
      );
    }
  }

  if ((result.report.ir.v1 || result.report.ir.v2) && result.report.optimizer.capabilities.length > 0) {
    lines.push("", "Optimizer Capabilities:");
    for (const capability of result.report.optimizer.capabilities.slice(0, 12)) {
      lines.push(
        `  - ${capability.id}: ${capability.status}; ${capability.detail}${capability.unsafe ? " [unsafe-unverified]" : ""}`,
      );
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
    const interesting = result.report.cellRoles.filter((cell) => cell.roles.length > 1 || cell.unsafe).slice(0, 12);
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
  if (result.report.unsafeUnverified.length > 0) {
    lines.push("", "Unsafe/unverified:");
    for (const item of result.report.unsafeUnverified) lines.push(`  - ${item}`);
  }
  return lines.join("\n");
}

export function formatAll(result: CompileResult): string {
  return [
    "# Listing",
    formatListing(result),
    "",
    "# Hex",
    formatHex(result),
    "",
    "# JSON",
    formatJson(result),
  ].join("\n");
}
