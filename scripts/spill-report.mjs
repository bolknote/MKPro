#!/usr/bin/env node

// Stack-spill measurement harness (supports the stack-resident temp scheduler).
//
// For every example it reports the compiled cell count and how many of those
// cells are register spills: scalar stores `X->П n` (0x40-0x4e), scalar recalls
// `П->X n` (0x60-0x6e), and their indexed `К` counterparts (0xb0-0xbe store,
// 0xd0-0xde recall). Spills are the cells a stack-residency scheduler can hope
// to reclaim by keeping a value live on X/Y/Z/T instead of a numbered register.
//
// Also reports static stack-residency candidate windows from source analysis.
//
// Usage:  node scripts/spill-report.mjs [--pending]
//   --pending  restrict to examples/pending-optimizer/*

import { readdirSync, readFileSync } from "node:fs";
import { join, relative } from "node:path";
import { compileMKPro } from "../src/core/index.ts";
import { analyzeSourceStackResidency } from "../src/core/emit/stack-residency-analysis.ts";

const PROGRAM_LIMIT = 105;
const pendingOnly = process.argv.includes("--pending");
const EXAMPLE_DIRS = pendingOnly
  ? ["examples/pending-optimizer"]
  : ["examples", "examples/pending-optimizer"];

function exampleFiles() {
  return EXAMPLE_DIRS.flatMap((dir) =>
    readdirSync(dir, { withFileTypes: true })
      .filter((entry) => entry.isFile() && entry.name.endsWith(".mkpro"))
      .map((entry) => join(dir, entry.name))
      .sort(),
  );
}

function inRange(opcode, lo, hi) {
  return opcode >= lo && opcode <= hi;
}

function classify(opcode) {
  if (inRange(opcode, 0x40, 0x4e)) return "store";
  if (inRange(opcode, 0x60, 0x6e)) return "recall";
  if (inRange(opcode, 0xb0, 0xbe)) return "indexedStore";
  if (inRange(opcode, 0xd0, 0xde)) return "indexedRecall";
  return undefined;
}

function measure(file) {
  const source = readFileSync(file, "utf8");
  try {
    const result = compileMKPro(source, { budget: 999999, analysis: true });
    const candidates = analyzeSourceStackResidency(source);
    const counts = { store: 0, recall: 0, indexedStore: 0, indexedRecall: 0 };
    for (const step of result.steps) {
      const kind = classify(step.opcode);
      if (kind !== undefined) counts[kind] += 1;
    }
    const steps = result.steps.length;
    const spill = counts.store + counts.recall + counts.indexedStore + counts.indexedRecall;
    return { file, steps, spill, counts, pct: steps === 0 ? 0 : Math.round((spill / steps) * 100), candidates };
  } catch (error) {
    return { file, error: error instanceof Error ? error.message : String(error) };
  }
}

const rows = exampleFiles().map(measure);

const header =
  "Example".padEnd(42) +
  "steps  spill  %    st/rc  idx(st/rc)  over105  s1  dual  multi  idx";
process.stdout.write(`${header}\n${"-".repeat(header.length)}\n`);
for (const row of rows) {
  const name = relative("examples", row.file);
  if (row.error) {
    process.stdout.write(`${name.padEnd(42)}ERROR ${row.error}\n`);
    continue;
  }
  const c = row.counts;
  const cand = row.candidates;
  const over = row.steps > PROGRAM_LIMIT ? `+${row.steps - PROGRAM_LIMIT}` : "";
  process.stdout.write(
    `${name.padEnd(42)}${String(row.steps).padStart(5)}  ${String(row.spill).padStart(5)}  ${String(row.pct).padStart(3)}  ` +
      `${c.store}/${c.recall}`.padEnd(7) +
      `${c.indexedStore}/${c.indexedRecall}`.padEnd(12) +
      `${over}`.padEnd(9) +
      `${String(cand.singleUsePairs).padStart(3)}  ` +
      `${String(cand.dualTempTriples).padStart(4)}  ` +
      `${String(cand.multiTempRuns).padStart(5)}  ` +
      `${String(cand.indexedConsumers).padStart(3)}` +
      "\n",
  );
}
