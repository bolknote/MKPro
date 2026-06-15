import { createHash } from "node:crypto";
import { mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { deserialize, serialize } from "node:v8";
import { compileLoweringVariantForTest, compileMKPro } from "../../src/core/compiler.ts";
import type { CompileOptions, CompileResult } from "../../src/core/types.ts";

// Disk-backed compile cache for the test suite.
//
// Several test files compile the same heavy example programs (rambo-iii alone is
// ~20s per full compile because its primary lowering overflows the 105-cell
// window and triggers the whole size-rescue candidate matrix). example-sizes,
// ir-round-trip, golden-listing and the emulator equivalence tests each pay that
// cost again, and every local re-run pays it from scratch.
//
// This wraps compileMKPro / compileLoweringVariantForTest with a content-keyed
// disk cache so an identical (compiler source, MK-Pro source, options) tuple is
// only compiled once, then reused across test files and across runs.
//
// Invalidation: every entry lives under a per-compiler-version subdirectory
// (a hash of all of src/core plus this wrapper). Changing the translator changes
// the hash, so entries are looked up in a fresh subdirectory and recompiled;
// stale version directories are pruned on first use. Serialization uses v8
// (preserves Map/Set/typed arrays); any serialize/deserialize failure falls back
// to a live compile, so the cache can never change a result — only skip
// recomputing it.

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = resolve(HERE, "..", "..");
const CORE_DIR = join(REPO_ROOT, "src", "core");
const CACHE_ROOT = join(REPO_ROOT, "node_modules", ".cache", "mkpro-test-compile");

const DISABLED = process.env.MKPRO_TEST_COMPILE_CACHE === "0";

function collectSourceFiles(dir: string, out: string[]): void {
  for (const entry of readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
    const full = join(dir, entry.name);
    if (entry.isDirectory()) {
      collectSourceFiles(full, out);
    } else if (entry.isFile() && entry.name.endsWith(".ts")) {
      out.push(full);
    }
  }
}

let cachedCompilerHash: string | undefined;

function compilerVersionHash(): string {
  if (cachedCompilerHash !== undefined) return cachedCompilerHash;
  const hash = createHash("sha256");
  const files: string[] = [];
  collectSourceFiles(CORE_DIR, files);
  for (const file of files) {
    hash.update(file.slice(REPO_ROOT.length));
    hash.update("\0");
    hash.update(readFileSync(file));
    hash.update("\0");
  }
  // The wrapper logic itself participates in the version so changes here invalidate.
  hash.update(readFileSync(fileURLToPath(import.meta.url)));
  cachedCompilerHash = hash.digest("hex").slice(0, 32);
  return cachedCompilerHash;
}

let versionDir: string | undefined;

// Returns the cache directory for the current compiler version, creating it and
// pruning any stale version directories on first use within the process.
function ensureVersionDir(): string {
  if (versionDir !== undefined) return versionDir;
  const version = compilerVersionHash();
  const dir = join(CACHE_ROOT, version);
  mkdirSync(dir, { recursive: true });
  try {
    for (const entry of readdirSync(CACHE_ROOT, { withFileTypes: true })) {
      if (entry.isDirectory() && entry.name !== version) {
        rmSync(join(CACHE_ROOT, entry.name), { recursive: true, force: true });
      }
    }
  } catch {
    // Pruning is best-effort; a failure here must never fail a test.
  }
  versionDir = dir;
  return dir;
}

function cachePath(kind: string, source: string, optionsKey: string): string {
  const key = createHash("sha256")
    .update(kind)
    .update("\0")
    .update(optionsKey)
    .update("\0")
    .update(source)
    .digest("hex");
  return join(ensureVersionDir(), `${key}.v8`);
}

function readCache(file: string): CompileResult | undefined {
  try {
    return deserialize(readFileSync(file)) as CompileResult;
  } catch {
    return undefined;
  }
}

function writeCache(file: string, result: CompileResult): void {
  try {
    writeFileSync(file, serialize(result));
  } catch {
    // A cache write failure must never fail a test; just skip caching.
  }
}

/** compileMKPro with a disk cache keyed by compiler version + source + options. */
export function compileMKProCached(source: string, options: Partial<CompileOptions> = {}): CompileResult {
  if (DISABLED) return compileMKPro(source, options);
  const file = cachePath("full", source, JSON.stringify(options));
  const hit = readCache(file);
  if (hit !== undefined) return hit;
  const result = compileMKPro(source, options);
  writeCache(file, result);
  return result;
}

/** compileLoweringVariantForTest with a disk cache keyed by compiler version + source + options + lowering. */
export function compileLoweringVariantCached(
  source: string,
  options: Partial<CompileOptions>,
  lowering: Record<string, unknown>,
): CompileResult {
  if (DISABLED) return compileLoweringVariantForTest(source, options, lowering);
  const file = cachePath("variant", source, `${JSON.stringify(options)}|${JSON.stringify(lowering)}`);
  const hit = readCache(file);
  if (hit !== undefined) return hit;
  const result = compileLoweringVariantForTest(source, options, lowering);
  writeCache(file, result);
  return result;
}
