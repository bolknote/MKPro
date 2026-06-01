import { readdirSync, readFileSync } from "node:fs";
import { join, relative } from "node:path";
import { describe, expect, it } from "vitest";
import {
  compileLoweringVariantForTest,
  compileMKPro,
} from "../../src/core/compiler.ts";
import {
  formatExplain,
  formatHex,
  formatListing,
  formatSetupProgram,
} from "../../src/core/format.ts";
import type { CompileResult } from "../../src/core/types.ts";

// Golden-output guardrail (Stage 2, step 2.0).
//
// The size report only checks `steps <= budget`; this snapshot captures the
// EXACT compiled output (hex + mnemonics + key report fields) for every example
// AND every lowering variant. The decomposition of EmitContext must keep this
// byte-for-byte identical: if any extraction perturbs lowering, X-tracking, the
// analysis maps or the runtime-helper synthesis, the snapshot diverges and the
// offending step is reverted.

const EXAMPLE_DIRS = ["examples", "examples/pending-optimizer"];

// Every explicit lowering variant probed by compileMKPro (kept stable so the
// fingerprint covers non-selected paths too). `{}` is the primary lowering.
const LOWERING_VARIANTS: ReadonlyArray<{ name: string; options: Record<string, unknown> }> = [
  { name: "primary", options: {} },
  { name: "aggressiveTerminalDirect", options: { aggressiveTerminalDirect: true } },
  { name: "invertBranchOrder", options: { invertBranchOrder: true } },
  { name: "aggressiveTerminalDirect+invertBranchOrder", options: { aggressiveTerminalDirect: true, invertBranchOrder: true } },
  { name: "hoistSharedHelpers", options: { hoistSharedHelpers: true } },
  { name: "hoistSharedHelpers+hoistProcs", options: { hoistSharedHelpers: true, hoistProcs: true } },
  { name: "canonicalizeIfChains", options: { canonicalizeIfChains: true } },
  { name: "freeResidualDispatchScratch", options: { freeResidualDispatchScratch: true } },
  { name: "aliasXReuse", options: { aliasXReuse: true } },
  { name: "coalesceCopies", options: { coalesceCopies: true } },
  { name: "freeResidualDispatchScratch+canonicalizeIfChains", options: { freeResidualDispatchScratch: true, canonicalizeIfChains: true } },
  { name: "shareRandomCell", options: { shareRandomCell: true } },
  { name: "shareRandomCell+hoistSharedHelpers", options: { shareRandomCell: true, hoistSharedHelpers: true } },
  { name: "tailBranchInversion", options: { tailBranchInversion: true } },
  { name: "hoistSharedHelpers+canonicalizeIfChains+tailBranchInversion", options: { hoistSharedHelpers: true, canonicalizeIfChains: true, tailBranchInversion: true } },
  { name: "guardedPrologueGadgets", options: { guardedPrologueGadgets: true } },
  { name: "guardedPrologueGadgets+hoistSharedHelpers+hoistProcs", options: { guardedPrologueGadgets: true, hoistSharedHelpers: true, hoistProcs: true } },
  { name: "sharedBitMaskHelperCalls", options: { sharedBitMaskHelperCalls: true } },
  { name: "sharedBitMaskHelperCalls+hoistSharedHelpers", options: { sharedBitMaskHelperCalls: true, hoistSharedHelpers: true } },
];

function exampleFiles(): string[] {
  return EXAMPLE_DIRS.flatMap((dir) =>
    readdirSync(dir, { withFileTypes: true })
      .filter((entry) => entry.isFile() && entry.name.endsWith(".mkpro"))
      .map((entry) => join(dir, entry.name))
      .sort(),
  );
}

function fullDump(result: CompileResult): string {
  const sections = ["## Listing", formatListing(result), "", "## Hex", formatHex(result)];
  const setup = formatSetupProgram(result);
  if (setup !== undefined) sections.push("", "## Setup Program", setup);
  sections.push("", "## Explain", formatExplain(result));
  return sections.join("\n");
}

function variantFingerprint(file: string): string {
  const source = readFileSync(file, "utf8");
  const lines: string[] = [];
  for (const variant of LOWERING_VARIANTS) {
    try {
      const result = compileLoweringVariantForTest(source, { budget: 999999, analysis: true }, variant.options);
      const hex = result.steps.map((step) => step.hex).join(" ");
      const setup = formatSetupProgram(result)?.split("\n").join(" ");
      lines.push(`${variant.name}: steps=${result.steps.length} | ${hex}${setup ? ` || setup ${setup}` : ""}`);
    } catch (error) {
      lines.push(`${variant.name}: throws ${error instanceof Error ? error.message : String(error)}`);
    }
  }
  return lines.join("\n");
}

describe("golden listing", () => {
  const files = exampleFiles();

  for (const file of files) {
    const name = relative(".", file);

    it(`selected output is stable for ${name}`, () => {
      const source = readFileSync(file, "utf8");
      const result = compileMKPro(source, { budget: 999999, analysis: true });
      expect(fullDump(result)).toMatchSnapshot();
    }, 20000);

    it(`lowering variants are stable for ${name}`, () => {
      expect(variantFingerprint(file)).toMatchSnapshot();
    }, 20000);
  }
});
