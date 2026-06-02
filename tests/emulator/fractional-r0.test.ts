import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";

const require = createRequire(import.meta.url);

interface Mk61Instance {
  setRegister(register: string, value: string): void;
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  press(key: string): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): { stopped: boolean; frames: number };
  displayText(): string;
  readRegister(register: string): string;
}
type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;
const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

// Ground-truth facts that the r0-fractional-sentinel and fractional-indirect-
// addressing capabilities depend on: a fractional R0 (0 < R0 < 1) used as an
// indirect selector addresses R3 (data) or jumps to 99 (flow), and in both
// cases overwrites R0 with the -99999999 sentinel.
describe("ROM fact: fractional R0 indirect addressing", () => {
  it("К П->X 0 with fractional R0 recalls R3 and leaves the -99999999 sentinel in R0", () => {
    for (const r0 of ["0.5", "0.1", "0.7"]) {
      const calc = new MK61();
      calc.loadProgram([0xd0, 0x50]); // К П->X 0 ; С/П
      calc.setRegister("0", r0);
      calc.setRegister("3", "42");
      calc.press("В/О");
      calc.press("С/П");
      const run = calc.runUntilStable({ maxFrames: 200, stableFrames: 5 });
      expect(run.stopped).toBe(true);
      expect(calc.readRegister("x").trim()).toBe("42,");
      expect(calc.readRegister("0").trim()).toBe("-99999999,");
    }
  });

  it("К БП 0 with fractional R0 jumps to address 99 and leaves the -99999999 sentinel in R0", () => {
    const program = Array.from({ length: 105 }, () => 0x50);
    program[0] = 0x80; // К БП 0
    program[1] = 0x01; // marker that must be skipped
    program[99] = 0x07; // marker reached only by jumping to 99
    program[100] = 0x50;
    const calc = new MK61();
    calc.loadProgram(program);
    calc.setRegister("0", "0.5");
    calc.press("В/О");
    calc.press("С/П");
    const run = calc.runUntilStable({ maxFrames: 300, stableFrames: 5 });
    expect(run.stopped).toBe(true);
    expect(calc.displayText().trim()).toContain("7");
    expect(calc.readRegister("0").trim()).toBe("-99999999,");
  });

  it("fractional R0 indirect conditionals jump to address 99 only on the false branch", () => {
    const cases = [
      { opcode: 0xe0, fallthroughX: "0", jumpX: "5" }, // К x=0 0
      { opcode: 0x70, fallthroughX: "5", jumpX: "0" }, // К x!=0 0
      { opcode: 0x90, fallthroughX: "5", jumpX: "-5" }, // К x>=0 0
      { opcode: 0xc0, fallthroughX: "-5", jumpX: "5" }, // К x<0 0
    ];

    for (const { opcode, fallthroughX, jumpX } of cases) {
      const fallthrough = new MK61();
      const fallthroughProgram = Array.from({ length: 105 }, () => 0x50);
      fallthroughProgram[0] = opcode;
      fallthroughProgram[1] = 0x01;
      fallthroughProgram[2] = 0x02;
      fallthroughProgram[99] = 0x07;
      fallthroughProgram[100] = 0x50;
      fallthrough.loadProgram(fallthroughProgram);
      fallthrough.setRegister("0", "0.5");
      fallthrough.setRegister("x", fallthroughX);
      fallthrough.press("В/О");
      fallthrough.press("С/П");
      expect(fallthrough.runUntilStable({ maxFrames: 300, stableFrames: 5 }).stopped).toBe(true);
      expect(fallthrough.displayText().trim()).toContain("12");
      expect(fallthrough.readRegister("0").trim()).toBe("0,5");

      const jumping = new MK61();
      const jumpProgram = Array.from({ length: 105 }, () => 0x50);
      jumpProgram[0] = opcode;
      jumpProgram[1] = 0x01;
      jumpProgram[2] = 0x02;
      jumpProgram[99] = 0x07;
      jumpProgram[100] = 0x50;
      jumping.loadProgram(jumpProgram);
      jumping.setRegister("0", "0.5");
      jumping.setRegister("x", jumpX);
      jumping.press("В/О");
      jumping.press("С/П");
      expect(jumping.runUntilStable({ maxFrames: 300, stableFrames: 5 }).stopped).toBe(true);
      expect(jumping.displayText().trim()).toContain("7");
      expect(jumping.readRegister("0").trim()).toBe("-99999999,");
    }
  });
});
