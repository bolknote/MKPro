import { readFileSync } from "node:fs";
const { compileMKPro } = await import("./src/core/index.ts");
const src = readFileSync("examples/pending-optimizer/wumpus.mkpro", "utf8");
const r = compileMKPro(src, { budget: 999999, analysis: true });
console.log("total:", r.report.steps, "| target reg:", r.report.registers?.target);
let addr = 0;
for (const s of r.steps) {
  const isCell = s.hex !== undefined;
  const t = (s.text || s.mnemonic || "");
  console.log(String(isCell ? addr : "  ").toString().padStart(3), t.padEnd(12), (s.comment || "").slice(0, 50));
  if (isCell) addr += 1;
}
