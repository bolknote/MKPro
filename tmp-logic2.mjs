import { createRequire } from "node:module";
const require = createRequire(import.meta.url);
const { MK61 } = require("./tests/emulator/mk61.cjs");

const OR = 0x38, AND = 0x37, XOR = 0x39 /*?*/;
function op2(a, b, opcode) {
  const calc = new MK61();
  // program: П->X a (0x6A), П->X b (0x6B), <op>, С/П
  calc.loadProgram([0x6a, 0x6b, opcode, 0x50]);
  calc.setRegister("a", a);
  calc.setRegister("b", b);
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 500, stableFrames: 5 });
  return { disp: calc.displayText(), reg: null };
}
function show(label, a, b, opcode) {
  const r = op2(a, b, opcode);
  console.log(`${label}: a=${a} b=${b} -> "${r.disp}"`);
}
console.log("=== OR ===");
show("int 8 | 8", "8", "8", OR);
show("int 1 | 2", "1", "2", OR);
show("frac .0000008 | .0000008", "0,0000008", "0,0000008", OR);
show("8.0000001 | 8.0000002", "8,0000001", "8,0000002", OR);
show("8.0000001 | 8.0000010", "8,0000001", "8,0000010", OR);
show("8.0000008 | 8.0000004", "8,0000008", "8,0000004", OR);
console.log("=== AND ===");
show("8.0000003 & 8.0000001", "8,0000003", "8,0000001", AND);
show("8.0000003 & 8.0000004", "8,0000003", "8,0000004", AND);
show("8.000000F & 8.0000008", "8,000000_", "8,0000008", AND);
console.log("=== readback of set value ===");
const c = new MK61();
c.setRegister("a", "8,000000_");
console.log("readRegister a (set 8.000000F) =", c.readRegister("a"));
