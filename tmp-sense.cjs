const fs = require("fs");
const { MK61 } = require("./tests/emulator/mk61.cjs");

(async () => {
  const { compileMKPro } = await import("./src/core/index.ts");
  const src = fs.readFileSync("tmp-sense.mkpro", "utf8");
  const r = compileMKPro(src, { budget: 999999 });
  console.log("size:", r.steps.length, "opts:", r.report.optimizations.map(o=>o.name).join(","));
  const codes = r.steps.map((s) => s.opcode);

  // registers: room=6 wumpus=8 pit1=4 pit2=5 bat1=0 bat2=1 arrows=2 clue=3
  function show(label, regs) {
    const c = new MK61();
    c.loadProgram(codes);
    for (const [rg, v] of Object.entries(regs)) c.setRegister(rg, String(v));
    c.pressSequence(["В/О", "С/П"]);
    c.runUntilStable({ maxFrames: 600, stableFrames: 5 });
    console.log(label.padEnd(34), "display=", JSON.stringify(c.displayText().trim()));
  }

  // figure register map from listing
  for (const s of r.steps) {
    if ((s.comment||"").includes("display")) console.log("  >", String(s.address).padStart(2,"0"), s.hex, s.mnemonic, s.comment);
  }
  const base = { "6": 10, "8": 20, "4": 18, "5": 17, "0": 2, "1": 3, "2": 5 };
  show("quiet  (ожидаем clue=0)      :", base);
  show("wumpus (ожидаем clue=1)      :", { ...base, "8": 10 });
  show("pit    (ожидаем clue=2)      :", { ...base, "4": 10 });
  show("bat    (ожидаем clue=3)      :", { ...base, "0": 11 });
})().catch((e) => { console.error(e); process.exit(1); });
