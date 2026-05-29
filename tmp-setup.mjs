import { readFileSync } from "node:fs";
const { compileMKPro } = await import("./src/core/index.ts");
const src = readFileSync("examples/wumpus.mkpro", "utf8");
const r = compileMKPro(src, { budget: 999999, analysis: true });
console.log("preloads:", JSON.stringify(r.report.preloads, null, 1));
console.log("setup:", JSON.stringify(r.report.setup, null, 1));
