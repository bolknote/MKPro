#!/usr/bin/env node

import { createHash } from "node:crypto";
import {
  existsSync,
  mkdirSync,
  readdirSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import {
  compileLoweringVariantCached as compileLoweringVariantForTest,
  compileMKProCached as compileMKPro,
} from "../tests/helpers/compile-cache.ts";
import {
  formatExplain,
  formatHex,
  formatJson,
  formatKeys,
  formatListing,
  formatSetupProgram,
} from "../src/core/format.ts";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = resolve(HERE, "..");
const CORE_DIR = join(REPO_ROOT, "src", "core");
const ORACLE_ROOT = join(REPO_ROOT, "native", "oracles", "examples");
const INDEX_PATH = join(ORACLE_ROOT, "index.json");
const EXAMPLE_DIRS = ["examples", "examples/pending-optimizer"];
const COMPILE_OPTIONS = { budget: 999999, analysis: true };

// Must stay in lockstep with tests/compiler/golden-listing.test.ts.
const LOWERING_VARIANTS = [
  { name: "primary", options: {} },
  { name: "aggressiveTerminalDirect", options: { aggressiveTerminalDirect: true } },
  { name: "invertBranchOrder", options: { invertBranchOrder: true } },
  {
    name: "aggressiveTerminalDirect+invertBranchOrder",
    options: { aggressiveTerminalDirect: true, invertBranchOrder: true },
  },
  { name: "hoistSharedHelpers", options: { hoistSharedHelpers: true } },
  {
    name: "hoistSharedHelpers+hoistProcs",
    options: { hoistSharedHelpers: true, hoistProcs: true },
  },
  { name: "canonicalizeIfChains", options: { canonicalizeIfChains: true } },
  {
    name: "freeResidualDispatchScratch",
    options: { freeResidualDispatchScratch: true },
  },
  { name: "aliasXReuse", options: { aliasXReuse: true } },
  { name: "coalesceCopies", options: { coalesceCopies: true } },
  {
    name: "freeResidualDispatchScratch+canonicalizeIfChains",
    options: { freeResidualDispatchScratch: true, canonicalizeIfChains: true },
  },
  {
    name: "repeatedUnaryUpdateArgTemp",
    options: { canonicalizeRepeatedUnaryUpdateArgs: true },
  },
  { name: "xParamValueFunction", options: { xParamValueFunctions: true } },
  {
    name: "xParamValueFunction+repeatedUnaryUpdateArgTemp",
    options: { xParamValueFunctions: true, canonicalizeRepeatedUnaryUpdateArgs: true },
  },
  {
    name: "xParamValueFunction+repeatedUnaryUpdateArgTemp+coalesceCopies",
    options: {
      xParamValueFunctions: true,
      canonicalizeRepeatedUnaryUpdateArgs: true,
      coalesceCopies: true,
    },
  },
  { name: "shareRandomCell", options: { shareRandomCell: true } },
  {
    name: "shareRandomCell+hoistSharedHelpers",
    options: { shareRandomCell: true, hoistSharedHelpers: true },
  },
  { name: "tailBranchInversion", options: { tailBranchInversion: true } },
  {
    name: "hoistSharedHelpers+canonicalizeIfChains+tailBranchInversion",
    options: {
      hoistSharedHelpers: true,
      canonicalizeIfChains: true,
      tailBranchInversion: true,
    },
  },
  { name: "guardedPrologueGadgets", options: { guardedPrologueGadgets: true } },
  {
    name: "guardedPrologueGadgets+hoistSharedHelpers+hoistProcs",
    options: {
      guardedPrologueGadgets: true,
      hoistSharedHelpers: true,
      hoistProcs: true,
    },
  },
  { name: "sharedBitMaskHelperCalls", options: { sharedBitMaskHelperCalls: true } },
  {
    name: "sharedBitMaskHelperCalls+hoistSharedHelpers",
    options: { sharedBitMaskHelperCalls: true, hoistSharedHelpers: true },
  },
];

const verify = process.argv.includes("--verify");

function sha256(input) {
  return createHash("sha256").update(input).digest("hex");
}

function collectSourceFiles(dir, out) {
  for (const entry of readdirSync(dir, { withFileTypes: true }).sort((a, b) =>
    a.name.localeCompare(b.name)
  )) {
    const full = join(dir, entry.name);
    if (entry.isDirectory()) collectSourceFiles(full, out);
    else if (entry.isFile() && entry.name.endsWith(".ts")) out.push(full);
  }
}

function compilerHash() {
  const hash = createHash("sha256");
  const files = [];
  collectSourceFiles(CORE_DIR, files);
  files.push(fileURLToPath(import.meta.url));
  for (const file of files) {
    hash.update(relative(REPO_ROOT, file));
    hash.update("\0");
    hash.update(readFileSync(file));
    hash.update("\0");
  }
  return hash.digest("hex");
}

function exampleFiles() {
  return EXAMPLE_DIRS.flatMap((dir) =>
    readdirSync(resolve(REPO_ROOT, dir), { withFileTypes: true })
      .filter((entry) => entry.isFile() && entry.name.endsWith(".mkpro"))
      .map((entry) => join(dir, entry.name))
      .sort()
  );
}

function artifactSetFor(result, sourcePath) {
  const setup = formatSetupProgram(result) ?? "";
  return {
    "bytes.txt": `${result.steps.map((step) => step.hex).join(" ")}\n`,
    "hex.txt": `${formatHex(result)}\n`,
    "listing.txt": `${formatListing(result)}\n`,
    "keys.txt": `${formatKeys(result)}\n`,
    "setup.txt": setup.length > 0 ? `${setup}\n` : "",
    "explain.txt": `${formatExplain(result)}\n`,
    "report.json": `${formatJson(result)}\n`,
    "variants.txt": `${variantFingerprint(sourcePath)}\n`,
  };
}

function variantFingerprint(relativeSourcePath) {
  const source = readFileSync(resolve(REPO_ROOT, relativeSourcePath), "utf8");
  const lines = [];
  for (const variant of LOWERING_VARIANTS) {
    process.stderr.write(`    variant ${variant.name}\n`);
    try {
      const result = compileLoweringVariantForTest(source, COMPILE_OPTIONS, variant.options);
      const hex = result.steps.map((step) => step.hex).join(" ");
      const setup = formatSetupProgram(result)?.split("\n").join(" ");
      lines.push(
        `${variant.name}: steps=${result.steps.length} | ${hex}${setup ? ` || setup ${setup}` : ""}`,
      );
    } catch (error) {
      lines.push(`${variant.name}: throws ${error instanceof Error ? error.message : String(error)}`);
    }
  }
  return lines.join("\n");
}

function writeOrCompare(path, content, changes) {
  if (verify) {
    if (!existsSync(path)) {
      changes.push(`missing ${relative(REPO_ROOT, path)}`);
      return;
    }
    const current = readFileSync(path, "utf8");
    if (current !== content) changes.push(`changed ${relative(REPO_ROOT, path)}`);
    return;
  }

  mkdirSync(dirname(path), { recursive: true });
  writeFileSync(path, content);
}

function buildOracles() {
  const entries = [];
  const files = exampleFiles();
  for (const [index, sourcePath] of files.entries()) {
    process.stderr.write(`[${index + 1}/${files.length}] ${sourcePath}\n`);
    const source = readFileSync(resolve(REPO_ROOT, sourcePath), "utf8");
    const id = sourcePath.replace(/^examples\//u, "").replace(/\.mkpro$/u, "");
    const result = compileMKPro(source, COMPILE_OPTIONS);
    const artifacts = artifactSetFor(result, sourcePath);
    const artifactPaths = Object.fromEntries(
      Object.keys(artifacts).map((name) => [name.replace(/\.(txt|json)$/u, ""), join(id, name)]),
    );

    entries.push({
      id,
      sourcePath,
      sourceSha256: sha256(source),
      steps: result.steps.length,
      artifacts,
      indexEntry: {
        id,
        sourcePath,
        sourceSha256: sha256(source),
        steps: result.steps.length,
        artifacts: artifactPaths,
      },
    });
  }

  const index = {
    schemaVersion: 1,
    generatedBy: "scripts/export-native-oracles.mjs",
    compilerHash: compilerHash(),
    compileOptions: COMPILE_OPTIONS,
    exampleCount: entries.length,
    examples: entries.map((entry) => entry.indexEntry),
  };

  return { entries, indexText: `${JSON.stringify(index, null, 2)}\n` };
}

const changes = [];
const { entries, indexText } = buildOracles();

if (!verify) {
  rmSync(ORACLE_ROOT, { recursive: true, force: true });
}

writeOrCompare(INDEX_PATH, indexText, changes);
for (const entry of entries) {
  for (const [name, content] of Object.entries(entry.artifacts)) {
    writeOrCompare(join(ORACLE_ROOT, entry.id, name), content, changes);
  }
}

if (changes.length > 0) {
  process.stderr.write(`Native oracle verification failed (${changes.length} change(s)):\n`);
  for (const change of changes) process.stderr.write(`  - ${change}\n`);
  process.exit(1);
}

process.stdout.write(
  `${verify ? "Verified" : "Exported"} ${entries.length} native example oracle(s).\n`,
);
