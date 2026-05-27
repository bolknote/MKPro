import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";

const require = createRequire(import.meta.url);
const ROM = require("./rom.cjs");
const { MK61 } = require("./mk61.cjs");

const CHIP_NAMES = ["ИК1302", "ИК1303", "ИК1306"] as const;

function sync4(commandWord: number): number {
  return (commandWord >>> 14) & 0xff;
}

function hex(code: number): string {
  return code.toString(16).toUpperCase().padStart(2, "0");
}

function runSingleOpcode(
  code: number,
  options: { extended?: boolean; x?: string; y?: string } = {},
) {
  const calc = new MK61({ extended: options.extended });
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

  it("does not collapse the F0..FF range to one command word", () => {
    const dispatcherWords = new Set<number>();
    const extensionWords = new Set<number>();

    for (let opcode = 0xf0; opcode <= 0xff; opcode += 1) {
      dispatcherWords.add(ROM.ИК1302.команды[opcode]);
      extensionWords.add(ROM.ИК1306.команды[opcode]);
      expect(ROM.ИК1302.команды[opcode]).not.toBe(ROM.ИК1306.команды[opcode]);
    }

    expect(dispatcherWords.size).toBeGreaterThan(1);
    expect(extensionWords.size).toBeGreaterThan(1);
  });

  it("marks control-flow opcodes with the wide sync4 path used by dark-address handling", () => {
    expect(sync4(ROM.ИК1302.команды[0x51])).toBeGreaterThan(31);
    expect(sync4(ROM.ИК1302.команды[0x52])).toBeGreaterThan(31);
    expect(sync4(ROM.ИК1302.команды[0x80])).toBeGreaterThan(31);
  });

  it("executes formal side-branch addresses through Anvarov's address-space map", () => {
    for (const { formal, actual } of [
      { formal: 0xa8, actual: 3 },
      { formal: 0xc5, actual: 13 },
    ]) {
      const program = new Array(105).fill(0x50);
      program[0] = 0x51;
      program[1] = formal;
      program[actual] = 0x07;
      program[actual + 1] = 0x50;

      const result = runProgram(program);

      expect(result.stopped).toBe(true);
      expect(result.display).toContain("7");
    }
  });

  it("executes only one command at super-dark formal addresses before the extra address", () => {
    const program = new Array(105).fill(0x50);
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
});
