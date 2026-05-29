#!/usr/bin/env node

import { readdirSync, readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { join, relative } from "node:path";
import {
  compileMKPro,
  formatProgramTokens,
  formatSetupProgram,
} from "../src/core/index.ts";

const require = createRequire(import.meta.url);
const { MK61 } = require("../tests/emulator/mk61.cjs");

const PROGRAM_LIMIT = 105;
const EXAMPLE_DIRS = ["examples", "examples/pending-optimizer"];

function exampleFiles() {
  return EXAMPLE_DIRS.flatMap((dir) =>
    readdirSync(dir, { withFileTypes: true })
      .filter((entry) => entry.isFile() && entry.name.endsWith(".mkpro"))
      .map((entry) => join(dir, entry.name))
      .sort(),
  );
}

function loadStatus(result) {
  const mainCodes = result.steps.map((step) => step.opcode);
  const setup = formatSetupProgram(result);
  const setupStatus = setup === undefined ? undefined : loadProgram(setup);

  if (mainCodes.length > PROGRAM_LIMIT) {
    return setupStatus === "load ok"
      ? "setup load ok; main >105"
      : "not loaded: main >105";
  }

  const mainStatus = loadProgram(mainCodes, mainCodes);
  if (setupStatus !== undefined && setupStatus !== "load ok") return `setup ${setupStatus}; main ${mainStatus}`;
  if (setupStatus === "load ok") return `main+setup ${mainStatus}`;
  return mainStatus;
}

function loadProgram(program, expectedCodes) {
  const calc = new MK61();
  const loaded = calc.loadProgram(program);
  if (loaded.diagnostics.length > 0) return `load failed: ${loaded.diagnostics.join("; ")}`;
  if (expectedCodes !== undefined) {
    const actualCodes = calc.readProgramCodes(expectedCodes.length);
    const mismatch = actualCodes.findIndex((code, index) => code !== expectedCodes[index]);
    if (mismatch !== -1) return `load mismatch @${mismatch}`;
  }
  return "load ok";
}

function measure(file) {
  const source = readFileSync(file, "utf8");
  try {
    const result = compileMKPro(source, { budget: 999999, analysis: true });
    const reference = result.report.reference;
    return {
      file,
      steps: result.report.steps,
      reference: reference?.referenceSpan,
      delta: reference === undefined ? undefined : result.report.steps - reference.referenceSpan,
      status: sizeStatus(file, result.report.steps, reference?.referenceSpan),
      emulator: loadStatus(result),
    };
  } catch (error) {
    return {
      file,
      error: error instanceof Error ? error.message : String(error),
    };
  }
}

function sizeStatus(file, steps, reference) {
  const pending = file.includes("/pending-optimizer/");
  if (pending && steps > PROGRAM_LIMIT) return "pending optimizer";
  if (steps > PROGRAM_LIMIT) return "broken: >105";
  if (reference === undefined) return "ok: no reference";
  if (steps <= reference) return "ok: <= reference";
  return "broken: > reference";
}

function formatNumber(value) {
  return value === undefined ? "-" : String(value);
}

function formatDelta(value) {
  if (value === undefined) return "-";
  if (value > 0) return `+${value}`;
  return String(value);
}

function rowName(row) {
  return relative("examples", row.file);
}

function pendingRow(row) {
  return row.file.includes("/pending-optimizer/");
}

function measuredRows(rows) {
  return rows.filter((row) => !("error" in row));
}

function formatProgramSize(row) {
  return `\`${rowName(row)}\` (${row.steps})`;
}

function snapshot(rows) {
  const measured = measuredRows(rows);
  const topLevel = measured.filter((row) => !pendingRow(row));
  const pending = measured.filter((row) => pendingRow(row));
  const fittingTopLevel = topLevel.filter((row) => row.steps <= PROGRAM_LIMIT);
  const loadedTopLevel = topLevel.filter(
    (row) =>
      row.emulator.includes("load ok") &&
      !row.emulator.includes("load failed") &&
      !row.emulator.includes("load mismatch") &&
      !row.emulator.includes("not loaded"),
  );
  const referencedTopLevel = topLevel.filter((row) => row.reference !== undefined);
  const noLargerThanReference = referencedTopLevel.filter((row) => row.delta <= 0);
  const tightestTopLevel = [...topLevel]
    .sort((left, right) => right.steps - left.steps)
    .slice(0, 3)
    .map(formatProgramSize)
    .join(", ");
  const pendingOverLimit = pending.filter((row) => row.steps > PROGRAM_LIMIT);
  const nearestPending = [...pendingOverLimit].sort((left, right) => left.steps - right.steps)[0];

  const lines = [
    "## Snapshot",
    "",
    `- Top-level examples: ${fittingTopLevel.length}/${topLevel.length} fit in the ${PROGRAM_LIMIT}-cell MK-61 window; ${loadedTopLevel.length}/${topLevel.length} pass the headless load check.`,
  ];

  if (referencedTopLevel.length > 0) {
    lines.push(
      `- Referenced top-level examples: ${noLargerThanReference.length}/${referencedTopLevel.length} are no larger than the original MK-61 listing.`,
    );
  }
  if (tightestTopLevel.length > 0) {
    lines.push(`- Tightest runnable examples: ${tightestTopLevel}.`);
  }
  if (pendingOverLimit.length > 0) {
    lines.push(
      `- Pending optimizer: ${pendingOverLimit.length} programs still exceed the MK-61 window; nearest is ${formatProgramSize(nearestPending)}.`,
    );
  }

  return lines;
}

function markdown(rows) {
  const lines = [
    "# Example Size Report",
    "",
    "Generated with `npm run examples:size`.",
    "",
    ...snapshot(rows),
    "",
    "## Measurements",
    "",
    "`MK-Pro` is the current compiled program size in MK-61 cells. `MK-61 ref` is the span of the referenced original listing under `games/` when the source declares `reference ...`. `Delta` is `MK-Pro - MK-61 ref`. The emulator column is a mechanical headless MK-61 load check; it is not a full game-script equivalence proof.",
    "",
    "| Example | MK-Pro | MK-61 ref | Delta | Size status | Emulator |",
    "| --- | ---: | ---: | ---: | --- | --- |",
  ];

  for (const row of rows) {
    const name = relative("examples", row.file);
    if ("error" in row) {
      lines.push(`| \`${name}\` | - | - | - | compile error | ${row.error.replace(/\|/gu, "\\|")} |`);
      continue;
    }
    lines.push(
      `| \`${name}\` | ${row.steps} | ${formatNumber(row.reference)} | ${formatDelta(row.delta)} | ${row.status} | ${row.emulator} |`,
    );
  }

  return `${lines.join("\n")}\n`;
}

const rows = exampleFiles().map(measure);
process.stdout.write(markdown(rows));
