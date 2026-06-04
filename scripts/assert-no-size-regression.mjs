#!/usr/bin/env node

// No-size-regression guard for the candidate composition engine.
//
// Compiles every example and pending-optimizer program and compares the emitted
// cell count (`report.steps`) against the committed baselines in
// `tests/compiler/example-baselines.ts`:
//
//   - steps > baseline  -> REGRESSION, exit 1 (the engine may only ever shrink).
//   - steps < baseline  -> improvement available; reported so the matching
//                          baseline (and golden snapshot) can be updated downward.
//   - steps === baseline -> ok.
//
// This is intentionally weaker than the exact-lock `example-sizes.test.ts`: it
// never fails on a shrink, so it can be run after speculative engine changes to
// confirm nothing grew before the baselines are refreshed.

import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { compileMKPro } from "../src/core/index.ts";
import { EXAMPLE_BASELINE, PENDING_BASELINE } from "../tests/compiler/example-baselines.ts";

function steps(relativePath, analysis) {
  const source = readFileSync(resolve(`examples/${relativePath}.mkpro`), "utf8");
  const options = analysis ? { budget: 999999, analysis: true } : {};
  return compileMKPro(source, options).report.steps;
}

const regressions = [];
const improvements = [];

function check(name, baseline, relativePath, analysis) {
  let actual;
  try {
    actual = steps(relativePath, analysis);
  } catch (error) {
    regressions.push(`${name}: compile failed (${error instanceof Error ? error.message : String(error)})`);
    return;
  }
  if (actual > baseline) regressions.push(`${name}: ${actual} cells > baseline ${baseline} (REGRESSION)`);
  else if (actual < baseline) improvements.push(`${name}: ${actual} cells < baseline ${baseline} (update baseline)`);
}

for (const [name, baseline] of Object.entries(EXAMPLE_BASELINE)) check(name, baseline, name, false);
for (const [name, baseline] of Object.entries(PENDING_BASELINE)) check(name, baseline, `pending-optimizer/${name}`, true);

if (improvements.length > 0) {
  process.stdout.write(`Improvements available (${improvements.length}):\n`);
  for (const line of improvements) process.stdout.write(`  - ${line}\n`);
}

if (regressions.length > 0) {
  process.stderr.write(`Size regressions detected (${regressions.length}):\n`);
  for (const line of regressions) process.stderr.write(`  - ${line}\n`);
  process.exit(1);
}

process.stdout.write(`No size regressions: ${Object.keys(EXAMPLE_BASELINE).length + Object.keys(PENDING_BASELINE).length} programs at or below baseline.\n`);
