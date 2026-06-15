#!/usr/bin/env node

import { readdirSync, readFileSync } from "node:fs";
import { availableParallelism } from "node:os";
import { join, relative } from "node:path";
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";
import { compileMKPro } from "../src/core/index.ts";

const PROGRAM_LIMIT = 105;
const EXAMPLE_DIRS = ["examples", "examples/pending-optimizer"];
const X2_NAME_RE = /\b(?:x2|vp-(?:splice|fraction)|display-byte-x2)\b/iu;
const DEFAULT_WORKERS = Math.max(1, Math.min(4, availableParallelism()));

function exampleFiles() {
  return EXAMPLE_DIRS.flatMap((dir) =>
    readdirSync(dir, { withFileTypes: true })
      .filter((entry) => entry.isFile() && entry.name.endsWith(".mkpro"))
      .map((entry) => join(dir, entry.name))
      .sort(),
  );
}

function compileFile(file) {
  const source = readFileSync(file, "utf8");
  try {
    const result = compileMKPro(source, { budget: 999999, analysis: true });
    const optimizations = result.report.optimizations ?? [];
    const x2Optimizations = [...new Set(optimizations.map((item) => item.name).filter((name) => X2_NAME_RE.test(name)))]
      .sort();
    return {
      file,
      steps: result.report.steps,
      budget: result.report.budget,
      over: result.report.steps - PROGRAM_LIMIT,
      pending: file.includes("/pending-optimizer/"),
      x2Optimizations,
    };
  } catch (error) {
    return {
      file,
      error: error instanceof Error ? error.message : String(error),
    };
  }
}

function rowName(row) {
  return relative("examples", row.file);
}

function formatList(items) {
  return items.length === 0 ? "-" : items.map((item) => `\`${item}\``).join(", ");
}

function requestedWorkerCount(fileCount) {
  const raw = process.env.X2_REPORT_WORKERS;
  const requested = raw === undefined ? DEFAULT_WORKERS : Number.parseInt(raw, 10);
  if (!Number.isFinite(requested) || requested < 1) {
    return 1;
  }
  return Math.min(fileCount, Math.trunc(requested));
}

async function compileFiles(files, workers) {
  if (workers <= 1 || files.length <= 1) {
    return files.map(compileFile);
  }

  const rows = new Array(files.length);
  let next = 0;
  let active = 0;

  return await new Promise((resolve, reject) => {
    let settled = false;

    function fail(error) {
      if (settled) {
        return;
      }
      settled = true;
      reject(error);
    }

    function launchNext() {
      if (settled) {
        return;
      }
      if (next >= files.length) {
        if (active === 0) {
          settled = true;
          resolve(rows);
        }
        return;
      }

      const index = next;
      next += 1;
      active += 1;

      const worker = new Worker(new URL(import.meta.url), { workerData: { file: files[index] } });
      worker.on("message", (row) => {
        rows[index] = row;
      });
      worker.on("error", fail);
      worker.on("exit", (code) => {
        active -= 1;
        if (code !== 0) {
          fail(new Error(`worker exited with code ${code} while compiling ${files[index]}`));
          return;
        }
        if (rows[index] === undefined) {
          fail(new Error(`worker exited without a result while compiling ${files[index]}`));
          return;
        }
        launchNext();
      });
    }

    for (let i = 0; i < workers; i += 1) {
      launchNext();
    }
  });
}

function markdown(rows, workers) {
  const measured = rows.filter((row) => !("error" in row));
  const withX2 = measured.filter((row) => row.x2Optimizations.length > 0);
  const pendingOverBudget = measured.filter((row) => row.pending && row.over > 0);
  const pendingOverBudgetWithoutX2 = pendingOverBudget.filter((row) => row.x2Optimizations.length === 0);

  const lines = [
    "# X2 Opportunity Report",
    "",
    "Generated with `npm run x2:report`.",
    "",
    "## Snapshot",
    "",
    `- Programs compiled: ${measured.length}/${rows.length}.`,
    `- Worker threads: ${workers}.`,
    `- Programs with X2-related optimizer hits: ${withX2.length}/${measured.length}.`,
    `- Pending programs over ${PROGRAM_LIMIT} cells: ${pendingOverBudget.length}.`,
    `- Pending programs over ${PROGRAM_LIMIT} cells without X2 hits: ${pendingOverBudgetWithoutX2.length}.`,
    "",
    "## Measurements",
    "",
    "| Example | Cells | Over 105 | X2-related optimizations |",
    "| --- | ---: | ---: | --- |",
  ];

  for (const row of rows) {
    if ("error" in row) {
      lines.push(`| \`${rowName(row)}\` | - | - | compile error: ${row.error.replace(/\|/gu, "\\|")} |`);
      continue;
    }
    lines.push(
      `| \`${rowName(row)}\` | ${row.steps} | ${row.over > 0 ? `+${row.over}` : row.over} | ${formatList(row.x2Optimizations)} |`,
    );
  }

  if (pendingOverBudgetWithoutX2.length > 0) {
    lines.push(
      "",
      "## First Non-X2 Bottlenecks",
      "",
      "These pending programs are still over budget but currently do not exercise an X2 optimizer pass. They are poor evidence for further X2 work until their IR exposes X2-shaped opportunities.",
      "",
    );
    for (const row of pendingOverBudgetWithoutX2) {
      lines.push(`- \`${rowName(row)}\`: ${row.steps} cells (+${row.over}).`);
    }
  }

  return `${lines.join("\n")}\n`;
}

if (isMainThread) {
  const files = exampleFiles();
  const workers = requestedWorkerCount(files.length);
  const rows = await compileFiles(files, workers);
  process.stdout.write(markdown(rows, workers));
} else {
  parentPort.postMessage(compileFile(workerData.file));
}
