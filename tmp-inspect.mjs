import { readFileSync } from "node:fs";
const { compileMKPro } = await import("./src/core/index.ts");
const src = readFileSync("examples/pending-optimizer/wumpus.mkpro", "utf8");
const r = compileMKPro(src, { budget: 999999, analysis: true });
console.log("total steps:", r.report.steps);
console.log("=== preloads (setup, free) ===");
for (const p of r.report.preloads ?? []) console.log(`  R${p.register} = ${p.value}  counts=${p.countsAgainstProgram}`);
console.log("=== allocation ===");
console.log(JSON.stringify(r.report.allocation ?? r.allocation ?? "(n/a)", null, 2).slice(0, 2000));
console.log("=== listing ===");
let i = 0;
for (const s of r.steps) {
  const roles = (s.roles || []).join(",");
  console.log(String(i).padStart(3, "0"), (s.text || s.mnemonic || "").padEnd(10), s.hex ?? "", roles);
  i++;
}
