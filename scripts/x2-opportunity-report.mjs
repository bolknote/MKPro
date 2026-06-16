#!/usr/bin/env node

import { readdirSync, readFileSync } from "node:fs";
import { availableParallelism } from "node:os";
import { join, relative } from "node:path";
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";
import { compileMKPro, opcodeCatalog } from "../src/core/index.ts";
import { raiseMachineToIr } from "../src/core/ir.ts";
import {
  computeX2RegisterStates,
  computeX2ValueStates,
  directReturnAnalysisContext,
  planRecallRemovalWithStackScheduler,
  removableRecallValueRegister,
} from "../src/core/passes/helpers.ts";

const PROGRAM_LIMIT = 105;
const EXAMPLE_DIRS = ["examples", "examples/pending-optimizer"];
const X2_NAME_RE = /\b(?:x2|vp-|display-byte-x2)/iu;
const DEFAULT_WORKERS = Math.max(1, Math.min(4, availableParallelism()));
const DEFAULT_WORKER_TIMEOUT_MS = 120_000;
const ASSERT_CLEAN = process.env.X2_REPORT_ASSERT_CLEAN === "1";
const opcodeByCode = new Map(opcodeCatalog.map((item) => [item.code, item]));

const DOT = 0x0a;
const SIGN = 0x0b;
const VP = 0x0c;
const LIFT = 0x0e;
const STOP = 0x50;
const DIRECT_RECALL_START = 0x60;
const DIRECT_RECALL_END = 0x6f;
const INDIRECT_RECALL_START = 0xd0;
const INDIRECT_RECALL_END = 0xdf;
const FORMAL_ADDRESS_TARGET_RE = /\bformal\s+[0-9a-f]+->(\d+)\b/iu;

function exampleFiles() {
  return EXAMPLE_DIRS.flatMap((dir) =>
    readdirSync(dir, { withFileTypes: true })
      .filter((entry) => entry.isFile() && entry.name.endsWith(".mkpro"))
      .map((entry) => join(dir, entry.name))
      .sort(),
  );
}

function requestedExampleFiles(files) {
  const raw = process.env.X2_REPORT_FILES;
  if (raw === undefined || raw.trim() === "") return files;
  const requested = raw.split(",").map((item) => item.trim()).filter(Boolean);
  const selected = [];
  const unmatched = [];
  for (const item of requested) {
    const matches = files.filter((file) =>
      file === item ||
      relative(".", file) === item ||
      relative("examples", file) === item ||
      rowName({ file }) === item ||
      file.endsWith(`/${item}`));
    if (matches.length === 0) {
      unmatched.push(item);
      continue;
    }
    selected.push(...matches);
  }
  if (unmatched.length > 0) {
    throw new Error(`X2_REPORT_FILES did not match example(s): ${unmatched.join(", ")}`);
  }
  return [...new Set(selected)];
}

function compileFile(file) {
  const source = readFileSync(file, "utf8");
  try {
    const result = compileMKPro(source, { budget: 999999, analysis: true });
    const optimizations = result.report.optimizations ?? [];
    const x2Optimizations = [...new Set(optimizations.map((item) => item.name).filter((name) => X2_NAME_RE.test(name)))]
      .sort();
    const x2Surface = analyzeResidualX2Surface(result.steps);
    return {
      file,
      steps: result.report.steps,
      budget: result.report.budget,
      over: result.report.steps - PROGRAM_LIMIT,
      pending: file.includes("/pending-optimizer/"),
      x2Optimizations,
      x2Surface,
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

function analyzeResidualX2Surface(steps) {
  const counts = {
    dot: 0,
    sign: 0,
    vp: 0,
    entryRestore: 0,
    directRecallSync: 0,
    indirectRecallSync: 0,
    stackLiftSync: 0,
    x2Affecting: 0,
    conditionalX2: 0,
    unknownX2: 0,
    requiredRecallRestore: 0,
    displayRecallRestore: 0,
    displayEmptyBeforeVp: 0,
    stackExposingLiftSync: 0,
    requiredRecallRestoreNoPlan: 0,
    requiredRecallRestoreNoProof: 0,
    requiredRecallRestoreVisibleX: 0,
    requiredRecallRestoreX2Proof: 0,
    requiredRecallRestoreStack: 0,
    requiredRecallRestoreX2: 0,
    requiredRecallRestoreStackAndX2: 0,
    requiredRecallRestoreOther: 0,
  };
  const patterns = new Map();
  const blockedPatterns = new Map();
  const { ops, stepToIrIndex } = raiseStepsToIr(steps);
  const x2RegisterStates = computeX2RegisterStates(ops);
  const x2ValueStates = computeX2ValueStates(ops, { trackRegisterMemory: true });
  const context = directReturnAnalysisContext(ops);

  for (let index = 0; index < steps.length; index += 1) {
    const step = steps[index];
    const info = opcodeByCode.get(step.opcode);
    if (step.opcode === DOT) counts.dot += 1;
    if (step.opcode === SIGN) counts.sign += 1;
    if (step.opcode === VP) counts.vp += 1;
    if (step.opcode >= 0 && step.opcode <= VP) counts.entryRestore += 1;
    if (isDirectRecall(step.opcode)) counts.directRecallSync += 1;
    if (isIndirectRecall(step.opcode)) counts.indirectRecallSync += 1;
    if (step.opcode === LIFT) counts.stackLiftSync += 1;
    if (info?.x2Effect === "affects") counts.x2Affecting += 1;
    if (info?.conditionalX2Effect !== undefined) counts.conditionalX2 += 1;
    if (info?.x2Effect === "unknown") counts.unknownX2 += 1;

    const next = steps[index + 1];
    const afterNext = steps[index + 2];
    if (next === undefined) continue;
    if (step.opcode === VP && next.opcode === VP) addPattern(patterns, "adjacent-vp", steps, index);
    if (isVpEmptySeparator(step.opcode) && next.opcode === VP) {
      if (isDisplayStep(step) || isDisplayStep(next)) {
        counts.displayEmptyBeforeVp += 1;
        addPattern(blockedPatterns, "display:empty->vp", steps, index);
      } else {
        addPattern(patterns, "empty-before-vp", steps, index);
      }
    }
    if (step.opcode === SIGN && next.opcode === SIGN) addPattern(patterns, "sign-pair", steps, index);
    if (step.opcode === VP && next.opcode === SIGN && afterNext?.opcode === SIGN) {
      addPattern(patterns, "vp-sign-pair", steps, index);
    }
    if (step.opcode === DOT && next.opcode === VP) addPattern(patterns, "dot-vp-context", steps, index);
    if (step.opcode === VP && next.opcode === DOT) addPattern(patterns, "vp-dot-context", steps, index);
    if (isRecall(step.opcode) && isContextSensitiveRestore(next.opcode)) {
      const opIndex = stepToIrIndex.get(index);
      const classification = classifyRecallBeforeRestore(
        ops,
        opIndex,
        x2RegisterStates,
        x2ValueStates,
        context,
        steps,
        index,
      );
      if (classification === "candidate") {
        addPattern(patterns, "recall-before-restore", steps, index);
      } else if (classification === "display") {
        counts.displayRecallRestore += 1;
        addPattern(blockedPatterns, "display:recall->restore", steps, index);
      } else {
        counts.requiredRecallRestore += 1;
        incrementRequiredRecallRestoreReason(counts, classification);
        addPattern(blockedPatterns, classification, steps, index);
      }
    }
    if (step.opcode === LIFT && isX2SyncingOpcode(next.opcode)) {
      const stopKind = next.opcode === STOP ? classifyStopStep(next) : undefined;
      const nextInfo = opcodeByCode.get(next.opcode);
      if (stopKind === "terminal") {
        addPattern(patterns, "lift-before-terminal-halt", steps, index);
      } else if (stopKind === "unknown") {
        addPattern(patterns, "lift-before-unknown-stop", steps, index);
      } else if (nextInfo?.stackEffect === "exposes" || nextInfo?.stackEffect === "unknown") {
        counts.stackExposingLiftSync += 1;
        addPattern(blockedPatterns, "stack:lift-sync", steps, index);
      } else if (stopKind === undefined) {
        addPattern(patterns, "lift-before-x2-sync", steps, index);
      }
    }
  }

  const x2RestoreSurface = counts.dot + counts.sign + counts.vp;
  const recallSyncSurface = counts.directRecallSync + counts.indirectRecallSync;
  const patternCount = [...patterns.values()].reduce((total, item) => total + item.count, 0);
  const score =
    x2RestoreSurface * 3 +
    counts.stackLiftSync +
    Math.ceil(recallSyncSurface / 3) +
    counts.conditionalX2 +
    patternCount * 5;
  return {
    counts,
    patternCount,
    score,
    patterns: [...patterns.entries()].map(([name, data]) => ({ name, ...data })),
    blockedPatterns: [...blockedPatterns.entries()].map(([name, data]) => ({ name, ...data })),
  };
}

function raiseStepsToIr(steps) {
  const items = [];
  const stepToIrIndex = new Map();
  let irIndex = 0;
  for (let index = 0; index < steps.length; index += 1) {
    const step = steps[index];
    items.push(machineOpFromStep(step));
    stepToIrIndex.set(index, irIndex);
    const info = opcodeByCode.get(step.opcode);
    const next = steps[index + 1];
    if (info?.takesAddress === true && next !== undefined) {
      const address = machineAddressFromStep(next);
      items.push(address);
      index += 1;
    }
    irIndex += 1;
  }
  return { ops: raiseMachineToIr(items), stepToIrIndex };
}

function machineOpFromStep(step) {
  return {
    kind: "op",
    opcode: step.opcode,
    mnemonic: step.mnemonic,
    ...(step.comment === undefined ? {} : { comment: step.comment }),
  };
}

function machineAddressFromStep(step) {
  const formal = FORMAL_ADDRESS_TARGET_RE.exec(step.comment ?? "");
  const target = formal === null ? bcdAddressTarget(step.opcode) : Number.parseInt(formal[1], 10);
  return {
    kind: "address",
    target,
    ...(step.comment === undefined ? {} : { comment: step.comment }),
    ...(formal === null ? {} : { formalOpcode: step.opcode }),
  };
}

function bcdAddressTarget(opcode) {
  const high = opcode >> 4;
  const low = opcode & 0x0f;
  return high <= 9 && low <= 9 ? high * 10 + low : opcode;
}

function classifyRecallBeforeRestore(ops, index, x2RegisterStates, x2ValueStates, context, steps, stepIndex) {
  if (isDisplayStep(steps[stepIndex]) || isDisplayStep(steps[stepIndex + 1])) return "display";
  if (index === undefined) return "required:no-plan";
  const register = removableRecallValueRegister(ops[index]);
  if (register === undefined) return "required:no-plan";
  const plan = planRecallRemovalWithStackScheduler(
    ops,
    index,
    x2RegisterStates[index],
    x2ValueStates[index],
    context,
  );
  if (plan === undefined) return "required:no-plan";
  if (plan.removable === true) return "candidate";
  if (plan.analysis.valueProof === undefined) {
    return "required:no-proof";
  }
  if (plan.analysis.valueProof.inX !== true) return "required:visible-x";
  if (plan.analysis.x2SyncRedundant !== true) return "required:x2-proof";
  const stackBlocked = plan.analysis.exposesStackLift === true && plan.stackLiftAlreadySupplied !== true;
  const x2Blocked = plan.analysis.exposesX2Restore === true;
  if (stackBlocked && x2Blocked) return "required:stack+x2";
  if (stackBlocked) return "required:stack";
  if (x2Blocked) return "required:x2";
  return "required:other";
}

function incrementRequiredRecallRestoreReason(counts, classification) {
  switch (classification) {
    case "required:no-plan":
      counts.requiredRecallRestoreNoPlan += 1;
      break;
    case "required:no-proof":
      counts.requiredRecallRestoreNoProof += 1;
      break;
    case "required:visible-x":
      counts.requiredRecallRestoreVisibleX += 1;
      break;
    case "required:x2-proof":
      counts.requiredRecallRestoreX2Proof += 1;
      break;
    case "required:stack":
      counts.requiredRecallRestoreStack += 1;
      break;
    case "required:x2":
      counts.requiredRecallRestoreX2 += 1;
      break;
    case "required:stack+x2":
      counts.requiredRecallRestoreStackAndX2 += 1;
      break;
    default:
      counts.requiredRecallRestoreOther += 1;
      break;
  }
}

function addPattern(patterns, name, steps, index) {
  const current = patterns.get(name) ?? { count: 0, samples: [] };
  current.count += 1;
  if (current.samples.length < 3) {
    current.samples.push(formatStepWindow(steps, index));
  }
  patterns.set(name, current);
}

function formatStepWindow(steps, index) {
  return steps
    .slice(index, Math.min(steps.length, index + 3))
    .map((step) => `${step.address}:${step.mnemonic}`)
    .join(" ");
}

function isDirectRecall(opcode) {
  return opcode >= DIRECT_RECALL_START && opcode <= DIRECT_RECALL_END;
}

function isIndirectRecall(opcode) {
  return opcode >= INDIRECT_RECALL_START && opcode <= INDIRECT_RECALL_END;
}

function isRecall(opcode) {
  return isDirectRecall(opcode) || isIndirectRecall(opcode);
}

function isContextSensitiveRestore(opcode) {
  return opcode === DOT || opcode === SIGN || opcode === VP;
}

function isVpEmptySeparator(opcode) {
  return opcode === 0x54 ||
    opcode === 0x55 ||
    opcode === 0x56 ||
    opcode === 0x1f ||
    opcode === 0x2f ||
    opcode === 0x3f ||
    (opcode >= 0xf0 && opcode <= 0xff);
}

function isX2SyncingOpcode(opcode) {
  const info = opcodeByCode.get(opcode);
  return info?.x2Effect === "affects" || info?.conditionalX2Effect !== undefined;
}

function classifyStopStep(step) {
  const comment = step.comment?.toLowerCase() ?? "";
  if (comment.startsWith("halt") || comment.startsWith("implicit final stop") || comment.includes("implicit stop")) {
    return "terminal";
  }
  if (
    comment.startsWith("show") ||
    comment.startsWith("ask") ||
    comment.startsWith("input") ||
    comment.startsWith("read") ||
    comment.startsWith("pause")
  ) {
    return "resumable";
  }
  return "unknown";
}

function isDisplayStep(step) {
  const comment = step?.comment?.toLowerCase() ?? "";
  return /\b(?:display|screen|show|template|inline_show|вп)\b/iu.test(comment);
}

function requestedWorkerCount(fileCount) {
  const raw = process.env.X2_REPORT_WORKERS;
  const requested = raw === undefined ? DEFAULT_WORKERS : Number.parseInt(raw, 10);
  if (!Number.isFinite(requested) || requested < 1) {
    return 1;
  }
  return Math.min(fileCount, Math.trunc(requested));
}

function requestedWorkerTimeoutMs() {
  const raw = process.env.X2_REPORT_TIMEOUT_MS;
  if (raw === undefined) return DEFAULT_WORKER_TIMEOUT_MS;
  const requested = Number.parseInt(raw, 10);
  if (!Number.isFinite(requested) || requested < 0) return DEFAULT_WORKER_TIMEOUT_MS;
  return Math.trunc(requested);
}

async function compileFiles(files, workers, timeoutMs = requestedWorkerTimeoutMs()) {
  if (files.length === 0) return [];
  if (timeoutMs <= 0 && (workers <= 1 || files.length <= 1)) {
    return files.map(compileFile);
  }

  const workerSlots = Math.max(1, workers);
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
      let timedOut = false;
      const timeout = timeoutMs <= 0
        ? undefined
        : setTimeout(() => {
          timedOut = true;
          rows[index] = {
            file: files[index],
            error: `compile timeout after ${timeoutMs}ms`,
          };
          void worker.terminate();
        }, timeoutMs);
      worker.on("message", (row) => {
        if (timedOut) return;
        rows[index] = row;
      });
      worker.on("error", (error) => {
        if (timedOut) return;
        fail(error);
      });
      worker.on("exit", (code) => {
        if (timeout !== undefined) clearTimeout(timeout);
        active -= 1;
        if (timedOut) {
          launchNext();
          return;
        }
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

    for (let i = 0; i < workerSlots; i += 1) {
      launchNext();
    }
  });
}

function markdown(rows, workers, timeoutMs = requestedWorkerTimeoutMs(), options = {}) {
  const measured = rows.filter((row) => !("error" in row));
  const withX2 = measured.filter((row) => row.x2Optimizations.length > 0);
  const pendingOverBudget = measured.filter((row) => row.pending && row.over > 0);
  const pendingOverBudgetWithoutX2 = pendingOverBudget.filter((row) => row.x2Optimizations.length === 0);
  const withResidualX2Patterns = measured.filter((row) => row.x2Surface.patternCount > 0);
  const withActionableBlockedX2 = measured.filter((row) => actionableBlockedX2Count(row.x2Surface.counts) > 0);
  const topResidualX2Surface = [...measured]
    .filter((row) => row.x2Surface.score > 0)
    .sort((left, right) => right.x2Surface.score - left.x2Surface.score)
    .slice(0, 12);

  const lines = [
    "# X2 Opportunity Report",
    "",
    "Generated with `npm run x2:report`.",
    "",
    "## Snapshot",
    "",
    `- Programs compiled: ${measured.length}/${rows.length}.`,
    `- Worker threads: ${workers}.`,
    `- Worker timeout: ${timeoutMs <= 0 ? "disabled" : `${timeoutMs}ms`}.`,
    ...(options.fileFilter === undefined ? [] : [`- File filter: \`${options.fileFilter}\`.`]),
    `- Programs with X2-related optimizer hits: ${withX2.length}/${measured.length}.`,
    `- Programs with residual X2 candidate patterns: ${withResidualX2Patterns.length}/${measured.length}.`,
    `- Programs with actionable blocked local X2: ${withActionableBlockedX2.length}/${measured.length}.`,
    `- Pending programs over ${PROGRAM_LIMIT} cells: ${pendingOverBudget.length}.`,
    `- Pending programs over ${PROGRAM_LIMIT} cells without X2 hits: ${pendingOverBudgetWithoutX2.length}.`,
    "",
    "## Measurements",
    "",
    "| Example | Cells | Over 105 | X2-related optimizations | X2 surface | Residual patterns | Actionable X2 blockers | Non-actionable audit |",
    "| --- | ---: | ---: | --- | ---: | --- | --- | --- |",
  ];

  for (const row of rows) {
    if ("error" in row) {
      lines.push(`| \`${rowName(row)}\` | - | - | compile error: ${row.error.replace(/\|/gu, "\\|")} | - | - | - | - |`);
      continue;
    }
    lines.push(
      `| \`${rowName(row)}\` | ${row.steps} | ${row.over > 0 ? `+${row.over}` : row.over} | ${formatList(row.x2Optimizations)} | ${row.x2Surface.score} | ${formatPatterns(row.x2Surface.patterns)} | ${formatActionableBlockedX2Surface(row.x2Surface.counts, row.x2Surface.blockedPatterns)} | ${formatNonActionableAuditSurface(row.x2Surface.counts, row.x2Surface.blockedPatterns)} |`,
    );
  }

  if (topResidualX2Surface.length > 0) {
    lines.push(
      "",
      "## Largest Residual X2 Surfaces",
      "",
      "This is a static post-optimization scan. A high score is not a proof that cells can be removed; it marks programs where X2 restores, recall-syncs, stack-lifts, conditionals, or nearby VP/sign/dot patterns remain visible after all passes.",
      "",
      "| Example | Score | Restore cells (`.` `/-/` `ВП`) | Recall syncs | Lifts | Conditional X2 | Top patterns | Actionable X2 blockers | Non-actionable audit |",
      "| --- | ---: | ---: | ---: | ---: | ---: | --- | --- | --- |",
    );
    for (const row of topResidualX2Surface) {
      const counts = row.x2Surface.counts;
      lines.push(
        `| \`${rowName(row)}\` | ${row.x2Surface.score} | ${counts.dot}/${counts.sign}/${counts.vp} | ${counts.directRecallSync + counts.indirectRecallSync} | ${counts.stackLiftSync} | ${counts.conditionalX2} | ${formatPatterns(row.x2Surface.patterns)} | ${formatActionableBlockedX2Surface(counts, row.x2Surface.blockedPatterns)} | ${formatNonActionableAuditSurface(counts, row.x2Surface.blockedPatterns)} |`,
      );
    }
  }

  if (pendingOverBudgetWithoutX2.length > 0) {
    lines.push(
      "",
      "## First Non-X2 Bottlenecks",
      "",
      "These pending programs are still over budget but currently do not report an X2 optimizer hit. The residual surface columns distinguish likely X2-shaped follow-up work from programs that first need non-X2 lowering changes.",
      "",
    );
    for (const row of pendingOverBudgetWithoutX2) {
      lines.push(
        `- \`${rowName(row)}\`: ${row.steps} cells (+${row.over}), residual X2 surface ${row.x2Surface.score}, patterns ${formatPatterns(row.x2Surface.patterns)}.`,
      );
    }
  }

  return `${lines.join("\n")}\n`;
}

function assertCleanX2Report(rows) {
  const measured = rows.filter((row) => !("error" in row));
  const withResidualX2Patterns = measured.filter((row) => row.x2Surface.patternCount > 0);
  const withActionableBlockedX2 = measured.filter((row) => actionableBlockedX2Count(row.x2Surface.counts) > 0);
  if (withResidualX2Patterns.length === 0 && withActionableBlockedX2.length === 0) return;

  const lines = ["X2 report is not clean:"];
  if (withResidualX2Patterns.length > 0) {
    lines.push(`- residual candidate patterns: ${withResidualX2Patterns.map(rowName).join(", ")}`);
  }
  if (withActionableBlockedX2.length > 0) {
    lines.push(`- actionable blocked local X2: ${withActionableBlockedX2.map(rowName).join(", ")}`);
  }
  throw new Error(lines.join("\n"));
}

function formatPatterns(patterns) {
  if (patterns.length === 0) return "-";
  return patterns
    .map((item) => `${item.name}=${item.count}${item.samples.length === 0 ? "" : ` (${item.samples.join("; ")})`}`)
    .join(", ")
    .replace(/\|/gu, "\\|");
}

function actionableBlockedX2Count(counts) {
  return counts.requiredRecallRestoreX2 +
    counts.requiredRecallRestoreX2Proof +
    counts.requiredRecallRestoreStackAndX2;
}

function nonActionableRequiredRecallRestoreCount(counts) {
  return counts.requiredRecallRestore -
    actionableBlockedX2Count(counts);
}

const ACTIONABLE_BLOCKED_PATTERN_NAMES = new Set([
  "required:x2",
  "required:x2-proof",
  "required:stack+x2",
]);

function formatActionableBlockedX2Surface(counts, blockedPatterns = []) {
  const count = actionableBlockedX2Count(counts);
  const parts = [];
  if (count > 0) {
    parts.push(`recall->restore=${count}${formatRequiredRecallRestoreReasons(counts, "actionable")}`);
  }
  const samples = blockedPatterns.filter((item) => ACTIONABLE_BLOCKED_PATTERN_NAMES.has(item.name));
  if (samples.length > 0) parts.push(`samples ${formatPatterns(samples)}`);
  return parts.length === 0 ? "-" : parts.join(", ");
}

function formatNonActionableAuditSurface(counts, blockedPatterns = []) {
  const parts = [];
  const requiredNonActionable = nonActionableRequiredRecallRestoreCount(counts);
  if (requiredNonActionable > 0) {
    parts.push(`required recall->restore=${requiredNonActionable}${formatRequiredRecallRestoreReasons(counts, "non-actionable")}`);
  }
  if (counts.displayRecallRestore > 0) parts.push(`display recall->restore=${counts.displayRecallRestore}`);
  if (counts.displayEmptyBeforeVp > 0) parts.push(`display empty->vp=${counts.displayEmptyBeforeVp}`);
  if (counts.stackExposingLiftSync > 0) parts.push(`stack-exposing lift sync=${counts.stackExposingLiftSync}`);
  const samples = blockedPatterns.filter((item) => !ACTIONABLE_BLOCKED_PATTERN_NAMES.has(item.name));
  if (samples.length > 0) parts.push(`samples ${formatPatterns(samples)}`);
  return parts.length === 0 ? "-" : parts.join(", ");
}

function formatRequiredRecallRestoreReasons(counts, mode = "all") {
  const reasons = [];
  const includeActionable = mode === "all" || mode === "actionable";
  const includeNonActionable = mode === "all" || mode === "non-actionable";
  if (includeActionable && counts.requiredRecallRestoreX2 > 0) reasons.push(`x2=${counts.requiredRecallRestoreX2}`);
  if (includeNonActionable && counts.requiredRecallRestoreStack > 0) reasons.push(`stack=${counts.requiredRecallRestoreStack}`);
  if (includeActionable && counts.requiredRecallRestoreStackAndX2 > 0) reasons.push(`stack+x2=${counts.requiredRecallRestoreStackAndX2}`);
  if (includeNonActionable && counts.requiredRecallRestoreVisibleX > 0) reasons.push(`visible-x=${counts.requiredRecallRestoreVisibleX}`);
  if (includeActionable && counts.requiredRecallRestoreX2Proof > 0) reasons.push(`x2-proof=${counts.requiredRecallRestoreX2Proof}`);
  if (includeNonActionable && counts.requiredRecallRestoreNoProof > 0) reasons.push(`no-proof=${counts.requiredRecallRestoreNoProof}`);
  if (includeNonActionable && counts.requiredRecallRestoreNoPlan > 0) reasons.push(`no-plan=${counts.requiredRecallRestoreNoPlan}`);
  if (includeNonActionable && counts.requiredRecallRestoreOther > 0) reasons.push(`other=${counts.requiredRecallRestoreOther}`);
  return reasons.length === 0 ? "" : ` [${reasons.join(", ")}]`;
}

if (isMainThread) {
  const fileFilter = process.env.X2_REPORT_FILES;
  let files;
  try {
    files = requestedExampleFiles(exampleFiles());
  } catch (error) {
    console.error(error instanceof Error ? error.message : String(error));
    process.exitCode = 1;
    process.exit();
  }
  const workers = requestedWorkerCount(files.length);
  const timeoutMs = requestedWorkerTimeoutMs();
  const rows = await compileFiles(files, workers, timeoutMs);
  process.stdout.write(markdown(rows, workers, timeoutMs, { fileFilter }));
  if (ASSERT_CLEAN) {
    try {
      assertCleanX2Report(rows);
    } catch (error) {
      console.error(error instanceof Error ? error.message : String(error));
      process.exitCode = 1;
    }
  }
} else {
  parentPort.postMessage(compileFile(workerData.file));
}
