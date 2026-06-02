import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";
import { evaluateIndirectAddress } from "../../src/core/indirect-addressing.ts";
import { registerIndex } from "../../src/core/opcodes.ts";
import type { RegisterName } from "../../src/core/types.ts";

const require = createRequire(import.meta.url);

interface ChipRom {
  команды: number[];
}

type RomModule = Record<"ИК1302" | "ИК1303" | "ИК1306", ChipRom>;

interface RunResult {
  stopped: boolean;
  frames: number;
}

interface Mk61Instance {
  setRegister(register: string, value: string): void;
  loadProgram(codes: number[]): void;
  press(key: string): void;
  pressSequence(keys: string[]): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): RunResult;
  displayText(): string;
  readRegister(register: string): string;
  programCounter(): number;
}

type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;

const ROM = require("./rom.cjs") as RomModule;
const { MK61, parseProgramText } = require("./mk61.cjs") as {
  MK61: Mk61Constructor;
  parseProgramText: (source: string) => { codes: number[]; diagnostics: string[] };
};

const CHIP_NAMES = ["ИК1302", "ИК1303", "ИК1306"] as const;
const MK61_HEX_DIGITS: Record<string, string> = {
  A: "-",
  B: "L",
  C: "С",
  D: "Г",
  E: "Е",
  F: "_",
};

function sync4(commandWord: number): number {
  return (commandWord >>> 14) & 0xff;
}

function hex(code: number): string {
  return code.toString(16).toUpperCase().padStart(2, "0");
}

function mk61HexLiteral(text: string): string {
  return [...text.toUpperCase()].map((digit) => MK61_HEX_DIGITS[digit] ?? digit).join("");
}

function runSingleOpcode(
  code: number,
  options: { extended?: boolean; x?: string; y?: string } = {},
) {
  const calc = options.extended === undefined
    ? new MK61()
    : new MK61({ extended: options.extended });
  calc.setRegister("x", options.x ?? "5");
  if (options.y) calc.setRegister("y", options.y);
  calc.loadProgram([code, 0x50]);
  calc.press("В/О");
  calc.press("С/П");
  const run = calc.runUntilStable({ maxFrames: 500, stableFrames: 5 });
  return {
    display: calc.displayText(),
    x: calc.readRegister("x"),
    r0: calc.readRegister("0"),
    stopped: run.stopped,
    frames: run.frames,
  };
}

function runProgram(codes: number[]) {
  const calc = new MK61();
  calc.loadProgram(codes);
  calc.press("В/О");
  calc.press("С/П");
  const run = calc.runUntilStable({ maxFrames: 500, stableFrames: 5 });
  return {
    display: calc.displayText(),
    pc: calc.programCounter(),
    stopped: run.stopped,
    frames: run.frames,
  };
}

function runProgramRepeated(codes: number[], count: number): string[] {
  const calc = new MK61();
  calc.loadProgram(codes);
  const displays: string[] = [];
  for (let index = 0; index < count; index += 1) {
    calc.pressSequence(["В/О", "С/П"]);
    calc.runUntilStable({ maxFrames: 100, stableFrames: 3 });
    displays.push(compactDisplay(calc.displayText()));
  }
  return displays;
}

function compactDisplay(text: string): string {
  return text.replace(/\s/gu, "");
}

function runProgramWithRegisters(codes: number[], registers: Record<string, string>) {
  const calc = new MK61();
  calc.loadProgram(codes);
  for (const [register, value] of Object.entries(registers)) calc.setRegister(register, value);
  calc.press("В/О");
  calc.press("С/П");
  const run = calc.runUntilStable({ maxFrames: 700, stableFrames: 5 });
  return {
    calc,
    display: calc.displayText(),
    pc: calc.programCounter(),
    stopped: run.stopped,
    frames: run.frames,
  };
}

function preloadNegativeZeroOne(calc: Mk61Instance, register: RegisterName): void {
  const registerCode = registerIndex(register);
  calc.loadProgram([
    0x54, 0x01, 0x03, 0x40 + registerCode, 0x01, 0x08, 0x38, 0x35, 0x0b,
    0x0c, 0x02, 0x15, 0x0e, 0x0c, 0x0b, 0x05, 0x00, 0x40 + registerCode,
    0x50,
  ]);
  calc.press("В/О");
  calc.press("С/П");
  calc.runUntilStable({ maxFrames: 1000, stableFrames: 5 });
}

function runNegativeZeroThresholdSelector(value: string): string {
  const calc = new MK61();
  preloadNegativeZeroOne(calc, "9");
  calc.setRegister("0", value);
  calc.loadProgram([0x69, 0x60, 0x12, 0x0e, 0x32, 0x50]);
  calc.press("В/О");
  calc.press("С/П");
  calc.runUntilStable({ maxFrames: 500, stableFrames: 5 });
  return calc.readRegister("x");
}

describe("emulator ROM discoveries", () => {
  it("uses different command ROM words across chips for every user opcode", () => {
    const differingOpcodes: number[] = [];

    for (let opcode = 0; opcode <= 0xff; opcode += 1) {
      const words = CHIP_NAMES.map((name) => ROM[name].команды[opcode]);
      if (new Set(words).size > 1) differingOpcodes.push(opcode);
    }

    expect(differingOpcodes).toHaveLength(256);
  });

  it("has dispatcher-zero opcodes on ИК1302 that still have microcode on later chips", () => {
    const zeroOnDispatcher = ROM.ИК1302.команды
      .map((word: number, opcode: number) => (word === 0 ? opcode : undefined))
      .filter((opcode: number | undefined) => opcode !== undefined);

    expect(zeroOnDispatcher.map(hex)).toEqual([
      "0B",
      "0C",
      "0D",
      "0E",
      "3B",
      "3C",
      "3D",
      "3E",
    ]);

    for (const opcode of zeroOnDispatcher) {
      expect(ROM.ИК1303.команды[opcode]).not.toBe(0);
    }
  });

  it("shows selected blue/extended opcodes need ИК1306 behavior", () => {
    for (const opcode of [0x34, 0x3b, 0x3d, 0x3e]) {
      const extended = runSingleOpcode(opcode, { extended: true, x: "5" });
      const basic = runSingleOpcode(opcode, { extended: false, x: "5" });

      expect(extended.display).not.toContain("ЕГГ");
      expect(basic.display).toContain("ЕГГ");
    }
  });

  it("keeps ROM-distinct aliases behaviorally equivalent for checked inputs", () => {
    for (const chipName of CHIP_NAMES) {
      expect(ROM[chipName].команды[0x2a]).not.toBe(ROM[chipName].команды[0x3d]);
      expect(ROM[chipName].команды[0x40]).not.toBe(ROM[chipName].команды[0x4f]);
    }

    expect(runSingleOpcode(0x2a, { x: "1,5" }).x).toBe(
      runSingleOpcode(0x3d, { x: "1,5" }).x,
    );

    const store0 = new MK61();
    store0.setRegister("x", "99");
    store0.loadProgram([0x40, 0x50]);
    store0.press("В/О");
    store0.press("С/П");
    store0.runUntilStable({ maxFrames: 200, stableFrames: 4 });

    const storeAlias = new MK61();
    storeAlias.setRegister("x", "99");
    storeAlias.loadProgram([0x4f, 0x50]);
    storeAlias.press("В/О");
    storeAlias.press("С/П");
    storeAlias.runUntilStable({ maxFrames: 200, stableFrames: 4 });

    expect(store0.readRegister("0")).toBe("99,");
    expect(storeAlias.readRegister("0")).toBe("99,");
  });

  it("does not model 5F as a non-terminating hang in this ROM wrapper", () => {
    const result = runSingleOpcode(0x5f);

    expect(result.stopped).toBe(true);
    expect(result.frames).toBeLessThan(20);
    expect(result.display).toBe("0,5000000000,0,");
  });

  it("turns 1|-00 multiplication into a normalized threshold selector", () => {
    expect(runNegativeZeroThresholdSelector("0")).toBe("0,");
    expect(runNegativeZeroThresholdSelector("0.7")).toBe("0,");
    expect(runNegativeZeroThresholdSelector("0.999")).toBe("0,");
    expect(runNegativeZeroThresholdSelector("1")).toBe("1,");
    expect(runNegativeZeroThresholdSelector("2")).toBe("1,");
  });

  it("matches observed К СЧ edge cases: no ordinary 1, hex-Y zero, and Кmax reset", () => {
    const calc = new MK61();
    const drawRandoms = (count: number): string[] => {
      calc.loadProgram([0x3b, 0x50]);
      const displays: string[] = [];
      for (let index = 0; index < count; index += 1) {
        calc.pressSequence(["В/О", "С/П"]);
        calc.runUntilStable({ maxFrames: 100, stableFrames: 3 });
        displays.push(compactDisplay(calc.displayText()));
      }
      return displays;
    };

    const first = drawRandoms(8);
    expect(first).not.toContain("1,");
    expect(first).not.toContain("0,");
    drawRandoms(8);

    calc.loadProgram([0x36, 0x50]);
    calc.setRegister("y", "0");
    calc.pressSequence(["В/О", "С/П"]);
    calc.runUntilStable({ maxFrames: 100, stableFrames: 3 });
    expect(drawRandoms(8)).toEqual(first);

    const hexY = new MK61();
    hexY.loadProgram([0x3b, 0x50]);
    hexY.setRegister("y", "_,0");
    hexY.pressSequence(["В/О", "С/П"]);
    hexY.runUntilStable({ maxFrames: 100, stableFrames: 3 });
    expect(compactDisplay(hexY.displayText())).toBe("0,");
  });

  it("shows integerizing К СЧ with К [x] can enter a short cycle", () => {
    expect(runProgramRepeated([0x3b, 0x09, 0x12, 0x34, 0x01, 0x10, 0x50], 6)).toEqual([
      "4,",
      "4,",
      "4,",
      "4,",
      "4,",
      "4,",
    ]);
  });

  it("keeps integer random draws moving when flooring via x-frac(x)", () => {
    expect(runProgramRepeated([0x3b, 0x09, 0x12, 0x0e, 0x35, 0x11, 0x01, 0x10, 0x50], 6)).toEqual([
      "4,",
      "6,",
      "9,",
      "1,",
      "8,",
      "9,",
    ]);
  });

  it("does not collapse the F0..FF range to one command word", () => {
    const dispatcherWords = new Set<number>();
    const extensionWords = new Set<number>();

    for (let opcode = 0xf0; opcode <= 0xff; opcode += 1) {
      dispatcherWords.add(ROM.ИК1302.команды[opcode]!);
      extensionWords.add(ROM.ИК1306.команды[opcode]!);
      expect(ROM.ИК1302.команды[opcode]).not.toBe(ROM.ИК1306.команды[opcode]);
    }

    expect(dispatcherWords.size).toBeGreaterThan(1);
    expect(extensionWords.size).toBeGreaterThan(1);
  });

  it("marks control-flow opcodes with the wide sync4 path used by dark-address handling", () => {
    expect(sync4(ROM.ИК1302.команды[0x51]!)).toBeGreaterThan(31);
    expect(sync4(ROM.ИК1302.команды[0x52]!)).toBeGreaterThan(31);
    expect(sync4(ROM.ИК1302.команды[0x80]!)).toBeGreaterThan(31);
  });

  it("executes formal side-branch addresses through the dark-address space map", () => {
    for (const { formal, actual } of [
      { formal: 0xa8, actual: 3 },
      { formal: 0xc5, actual: 13 },
    ]) {
      const program = Array<number>(105).fill(0x50);
      program[0] = 0x51;
      program[1] = formal;
      program[actual] = 0x07;
      program[actual + 1] = 0x50;

      const result = runProgram(program);

      expect(result.stopped).toBe(true);
      expect(result.display).toContain("7");
    }
  });

  it("loads formal dark addressed source lines at their physical cells", () => {
    const parsed = parseProgramText(`
C5 7
C6 8
`);

    expect(parsed.diagnostics).toEqual([]);
    expect(parsed.codes[13]).toBe(0x07);
    expect(parsed.codes[14]).toBe(0x08);
  });

  it("executes only one command at super-dark formal addresses before the extra address", () => {
    const program = Array<number>(105).fill(0x50);
    program[0] = 0x51;
    program[1] = 0xfb;
    // FB maps to actual address 49, then goes to extra address 02.
    program[49] = 0x08;
    program[50] = 0x09;
    program[2] = 0x50;

    const result = runProgram(program);

    expect(result.stopped).toBe(true);
    expect(result.display).toContain("8");
    expect(result.display).not.toContain("9");
  });

  it("executes stable-register indirect flow used by the optimizer", () => {
    const program = Array<number>(105).fill(0x50);
    program[0] = 0x01;
    program[1] = 0x02;
    program[2] = 0x47;
    program[3] = 0x87;
    program[4] = 0x09;
    program[12] = 0x07;
    program[13] = 0x50;

    const result = runProgram(program);

    expect(result.stopped).toBe(true);
    expect(result.display).toContain("7");
    expect(result.display).not.toContain("9");
  });

  it("executes stable-register indirect memory access used by the optimizer", () => {
    const recallProgram = [0x05, 0x42, 0x0d, 0x02, 0x47, 0xd7, 0x50];
    const storeProgram = [0x02, 0x47, 0x0d, 0x09, 0xb7, 0x0d, 0xd7, 0x50];

    const recall = runProgram(recallProgram);
    const store = runProgram(storeProgram);

    expect(recall.stopped).toBe(true);
    expect(recall.display).toContain("5");
    expect(store.stopped).toBe(true);
    expect(store.display).toContain("9");
  });

  it("matches the indirect address model for representative integer flow selectors", () => {
    for (const { selector, value } of [
      { selector: "0" as RegisterName, value: "13" },
      { selector: "4" as RegisterName, value: "11" },
      { selector: "7" as RegisterName, value: "12" },
    ]) {
      const model = evaluateIndirectAddress(selector, value, "flow");
      expect(model?.actualFlowTarget).toBe(12);

      const program = Array<number>(105).fill(0x50);
      program[0] = 0x80 + registerIndex(selector);
      program[1] = 0x09;
      program[12] = 0x07;
      program[13] = 0x50;
      const result = runProgramWithRegisters(program, { [selector]: value });

      expect(result.stopped).toBe(true);
      expect(result.display).toContain("7");
      expect(result.display).not.toContain("9");
      expect(result.calc.readRegister(selector).trim()).toMatch(/^0*12,/u);
    }
  });

  it("matches the indirect address model for representative integer memory selectors", () => {
    for (const { selector, value } of [
      { selector: "0" as RegisterName, value: "3" },
      { selector: "4" as RegisterName, value: "1" },
      { selector: "7" as RegisterName, value: "2" },
    ]) {
      const model = evaluateIndirectAddress(selector, value, "memory");
      expect(model?.memoryTarget).toBe("2");

      const result = runProgramWithRegisters(
        [0xd0 + registerIndex(selector), 0x50],
        { [selector]: value, "2": "7" },
      );

      expect(result.stopped).toBe(true);
      expect(result.display).toContain("7");
      expect(result.calc.readRegister(selector).trim()).toMatch(/^0*2,/u);
    }
  });

  it("matches the indirect address model for negative integer memory selectors", () => {
    const model = evaluateIndirectAddress("7", "-7", "memory");
    expect(model?.memoryTarget).toBe("1");

    const result = runProgramWithRegisters([0xd7, 0x50], { "7": "-7", "1": "8" });

    expect(result.stopped).toBe(true);
    expect(result.display).toContain("8");
    expect(result.calc.readRegister("7").trim()).toMatch(/^-99999997,/u);
  });

  it("matches the fractional R0 sentinel model for flow and memory", () => {
    const flowModel = evaluateIndirectAddress("0", "0,5", "flow");
    const recallModel = evaluateIndirectAddress("0", "0,5", "memory");
    expect(flowModel?.actualFlowTarget).toBe(99);
    expect(recallModel?.memoryTarget).toBe("3");

    const flowProgram = Array<number>(105).fill(0x50);
    flowProgram[0] = 0x80;
    flowProgram[99] = 0x06;
    flowProgram[100] = 0x50;
    const flow = runProgramWithRegisters(flowProgram, { "0": "0,5" });
    const recall = runProgramWithRegisters([0xd0, 0x50], { "0": "0,5", "3": "8" });

    expect(flow.stopped).toBe(true);
    expect(flow.display).toContain("6");
    expect(flow.calc.readRegister("0").trim()).toMatch(/^-99999999,/u);
    expect(recall.stopped).toBe(true);
    expect(recall.display).toContain("8");
    expect(recall.calc.readRegister("0").trim()).toMatch(/^-99999999,/u);
  });

  it("matches the indirect address model for FA..FF super-dark dispatch", () => {
    for (let offset = 0; offset <= 5; offset += 1) {
      const value = (0xfa + offset).toString(16).toUpperCase();
      const model = evaluateIndirectAddress("7", value, "flow");
      expect(model?.superDark).toEqual({
        formal: 0xfa + offset,
        entryAddress: 48 + offset,
        continuationAddress: 1 + offset,
      });

      const marker = offset + 1;
      const program = Array<number>(105).fill(0x50);
      program[0] = 0x87;
      program[1 + offset] = 0x50;
      program[48 + offset] = marker;
      program[49 + offset] = 0x09;
      const result = runProgramWithRegisters(program, { "7": mk61HexLiteral(value) });

      expect(result.stopped).toBe(true);
      expect(result.display).toContain(String(marker));
      expect(result.display).not.toContain("9");
    }
  });
});
