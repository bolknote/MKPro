import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";

const require = createRequire(import.meta.url);

interface Mk61Instance {
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  setRegister(register: string, value: string): Mk61Instance;
  readRegister(register: string): string;
  pressSequence(keys: string[]): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): { stopped: boolean; frames: number };
  displayText(): string;
}
type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;
const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

const IP1 = 0x61;
const IP2 = 0x62;
const ADD = 0x10;
const SUBTRACT = 0x11;
const MULTIPLY = 0x12;
const DIVIDE = 0x13;
const STACK_LIFT = 0x0e;
const DOT = 0x0a;
const F_PI = 0x20;
const SQUARE = 0x22;
const K_SIGN = 0x32;
const F0 = 0xf0;
const STOP = 0x50;

// Adds R1 and R2 via ИП1 ИП2 + so that the second-recalled register lands in X.
function addRegisters(r1: string, r2: string): string {
  const calc = new MK61();
  calc.setRegister("1", r1);
  calc.setRegister("2", r2);
  calc.loadProgram([IP1, IP2, ADD, STOP]);
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 300, stableFrames: 4 });
  return calc.displayText().replace(/\s/gu, "");
}

function runUnaryRegisterProgram(r1: string, codes: number[]): string {
  const calc = new MK61();
  calc.setRegister("1", r1);
  calc.loadProgram([...codes, STOP]);
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 300, stableFrames: 4 });
  return calc.displayText().replace(/\s/gu, "");
}

function multiplyRegisters(r1: string, r2: string): string {
  const calc = new MK61();
  calc.setRegister("1", r1);
  calc.setRegister("2", r2);
  calc.loadProgram([IP1, IP2, MULTIPLY, STOP]);
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 300, stableFrames: 4 });
  return calc.displayText().replace(/\s/gu, "");
}

function divideRegisters(r1: string, r2: string): string {
  const calc = new MK61();
  calc.setRegister("1", r1);
  calc.setRegister("2", r2);
  calc.loadProgram([IP1, IP2, DIVIDE, STOP]);
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 300, stableFrames: 4 });
  return calc.displayText().replace(/\s/gu, "");
}

// MK-61 mantissa digit values 10..15 are the famous "letter" glyphs (hex digits
// A..F). These ROM facts pin the undocumented hex-mantissa arithmetic that the
// hex-mantissa-arithmetic capability models.
describe("undocumented MK-61 hex mantissa arithmetic", () => {
  it("stores and renders a hex mantissa digit (Г = digit 13)", () => {
    const calc = new MK61();
    calc.setRegister("1", "Г");
    expect(calc.readRegister("1").trim()).toBe("Г,");
  });

  it("hex digit in Y adds as its decimal value (D + 3 = 16)", () => {
    // R1 = Г(13) recalled first -> ends up in Y; R2 = 3 -> X. Y + X = 13 + 3.
    expect(addRegisters("Г", "3")).toContain("16");
    expect(addRegisters("С", "4")).toContain("16");
  });

  it("hex digit in X collapses the sum to a clean zero in either pairing", () => {
    // R2 = Г(13) recalled second -> lands in X; the BCD correction zeroes the
    // result regardless of the decimal partner. This is the documented
    // hex+decimal zero-generator.
    expect(addRegisters("3", "Г")).toBe("0,");
    expect(addRegisters("4", "С")).toBe("0,");
  });

  it("single hex digit addition and subtraction tables are operand-order-sensitive", () => {
    expect(addRegisters("B", "0")).toBe("11,");
    expect(addRegisters("0", "B")).toBe("1,");
    expect(addRegisters("Г", "4")).toBe("17,");
    expect(addRegisters("3", "С")).toBe("5,");
    expect(addRegisters("-", "18")).toBe("28,");
    expect(addRegisters("18", "-")).toBe("28,");
    expect(addRegisters("С", "16")).toBe("28,");
    expect(addRegisters("16", "С")).toBe("28,");
    expect(addRegisters("Е", "18")).toBe("32,");
    expect(addRegisters("18", "Е")).toBe("32,");
    expect(addRegisters("-", "B")).toBe("5,");
    expect(addRegisters("Г", "Е")).toBe("11,");

    const subtractRegisters = (r1: string, r2: string): string => {
      const calc = new MK61();
      calc.setRegister("1", r1);
      calc.setRegister("2", r2);
      calc.loadProgram([IP1, IP2, SUBTRACT, STOP]);
      calc.pressSequence(["В/О", "С/П"]);
      calc.runUntilStable({ maxFrames: 300, stableFrames: 4 });
      return calc.displayText().replace(/\s/gu, "");
    };

    expect(subtractRegisters("-", "0")).toBe("10,");
    expect(subtractRegisters("B", "1")).toBe("0,");
    expect(subtractRegisters("0", "B")).toBe("-1,");
    expect(subtractRegisters("С", "2")).toBe("0,");
    expect(subtractRegisters("Е", "5")).toBe("9,");
    expect(subtractRegisters("-", "18")).toBe("-8,");
    expect(subtractRegisters("18", "-")).toBe("8,");
    expect(subtractRegisters("B", "18")).toBe("-7,");
    expect(subtractRegisters("18", "B")).toBe("23,");
    expect(subtractRegisters("С", "16")).toBe("-4,");
    expect(subtractRegisters("16", "С")).toBe("20,");
    expect(subtractRegisters("10", "Е")).toBe("12,");
    expect(subtractRegisters("0", "С")).toBe("-2,");
    expect(subtractRegisters("2", "С")).toBe("-10,");
    expect(subtractRegisters("4", "Е")).toBe("-10,");
    expect(subtractRegisters("-", "Е")).toBe("-4,");
    expect(subtractRegisters("Е", "-")).toBe("4,");
  });

  it("hex C/D/E subtract one as decimal 1/2/3 before ordinary square", () => {
    expect(runUnaryRegisterProgram("С", [IP1, 0x01, SUBTRACT])).toBe("1,");
    expect(runUnaryRegisterProgram("Г", [IP1, 0x01, SUBTRACT])).toBe("2,");
    expect(runUnaryRegisterProgram("Е", [IP1, 0x01, SUBTRACT])).toBe("3,");
    expect(runUnaryRegisterProgram("С", [IP1, 0x01, SUBTRACT, SQUARE])).toBe("1,");
    expect(runUnaryRegisterProgram("Г", [IP1, 0x01, SUBTRACT, SQUARE])).toBe("4,");
    expect(runUnaryRegisterProgram("Е", [IP1, 0x01, SUBTRACT, SQUARE])).toBe("9,");
  });

  it("single significant hex digit square table is pinned", () => {
    expect(runUnaryRegisterProgram("-", [IP1, SQUARE])).toBe("00,");
    expect(runUnaryRegisterProgram("B", [IP1, SQUARE])).toBe("10,");
    expect(runUnaryRegisterProgram("С", [IP1, SQUARE])).toBe("20,");
    expect(runUnaryRegisterProgram("Г", [IP1, SQUARE])).toBe("30,");
    expect(runUnaryRegisterProgram("Е", [IP1, SQUARE])).toBe("0,");
    expect(runUnaryRegisterProgram("_", [IP1, SQUARE])).toBe("0,");
    expect(runUnaryRegisterProgram("0С", [IP1, SQUARE])).toBe("20,");
    expect(runUnaryRegisterProgram("-0Г", [IP1, SQUARE])).toBe("30,");
    expect(runUnaryRegisterProgram("B0", [IP1, SQUARE])).toBe("1000,");
  });

  it("X2-affecting sync normalizes non-normal decimal display shapes", () => {
    expect(runUnaryRegisterProgram("-", [IP1, SQUARE, F0, F_PI, DOT])).toBe("0,");
    expect(runUnaryRegisterProgram("-", [IP1, STACK_LIFT, 0x01, 0x08, MULTIPLY])).toBe("020,");
    expect(runUnaryRegisterProgram("-", [IP1, STACK_LIFT, 0x01, 0x08, MULTIPLY, F0, F_PI, DOT])).toBe("20,");
  });

  it("hex A multiplied by 18 renders a non-normal leading zero", () => {
    const calc = new MK61();
    calc.setRegister("1", "-");
    calc.setRegister("2", "18");
    calc.loadProgram([IP1, IP2, 0x12, STOP]);
    calc.pressSequence(["В/О", "С/П"]);
    calc.runUntilStable({ maxFrames: 300, stableFrames: 4 });
    expect(calc.displayText()).toBe("020,");
    expect(calc.readRegister("x").trim()).toBe("20,");
  });

  it("hex A multiplication table is operand-order-sensitive", () => {
    const calc = new MK61();
    calc.setRegister("1", "18");
    calc.setRegister("2", "-");
    calc.loadProgram([IP1, IP2, 0x12, STOP]);
    calc.pressSequence(["В/О", "С/П"]);
    calc.runUntilStable({ maxFrames: 300, stableFrames: 4 });
    expect(calc.displayText()).toBe("180,");
    expect(calc.readRegister("x").trim()).toBe("180,");
  });

  it("single hex digit multiplication table is operand-order-sensitive", () => {
    const leftHexCases: ReadonlyArray<readonly [string, string, string]> = [
      ["-", "1", "0,"],
      ["-", "-", "00,"],
      ["B", "Г", "10,"],
      ["-", "18", "020,"],
      ["-", "16", "000,"],
      ["B", "18", "054,"],
      ["B", "17", "043,"],
      ["С", "5", "32,"],
      ["С", "16", "904,"],
      ["Г", "3", "23,"],
      ["Г", "15", "923,"],
      ["Е", "18", "948,"],
      ["Е", "17", "934,"],
      ["Е", "С", "40,"],
    ];
    for (const [left, right, display] of leftHexCases) {
      expect(multiplyRegisters(left, right)).toBe(display.replace(/\s/gu, ""));
    }

    const rightHexCases: ReadonlyArray<readonly [string, string, string]> = [
      ["1", "-", "10,"],
      ["16", "-", "160,"],
      ["18", "-", "180,"],
      ["18", "B", "180,"],
      ["17", "B", "170,"],
      ["9", "С", "90,"],
      ["16", "С", "160,"],
      ["5", "Г", "50,"],
      ["15", "Г", "150,"],
      ["18", "Е", "0,"],
      ["17", "Е", "0,"],
    ];
    for (const [left, right, display] of rightHexCases) {
      expect(multiplyRegisters(left, right)).toBe(display.replace(/\s/gu, ""));
    }
  });

  it("single hex digit division table is operand-order-sensitive", () => {
    expect(divideRegisters("-", "-")).toBe("1,");
    expect(divideRegisters("-", "Г")).toBe("4,-01");
    expect(divideRegisters("B", "Г")).toBe("6,-01");
    expect(divideRegisters("С", "B")).toBe("1,2525252");
    expect(divideRegisters("Г", "С")).toBe("1,23");
    expect(divideRegisters("Е", "Г")).toBe("1,2");
    expect(divideRegisters("-", "С")).toBe("ЕГГ0Г");
    expect(divideRegisters("B", "2")).toBe("5,5");
    expect(divideRegisters("Г", "8")).toBe("1,625");
    expect(divideRegisters("Г", "16")).toBe("8,125-01");
    expect(divideRegisters("Е", "17")).toBe("8,2352941-01");
    expect(divideRegisters("Е", "18")).toBe("7,7777777-01");
    expect(divideRegisters("-", "10")).toBe("0,-01");
    expect(divideRegisters("1", "-")).toBe("ЕГГ0Г");
    expect(divideRegisters("10", "-")).toBe("ЕГГ0Г");
    expect(divideRegisters("0", "-")).toBe("9,090909-01");
    expect(divideRegisters("9", "B")).toBe("0,4444443-01");
    expect(divideRegisters("16", "B")).toBe("9,2525252");
    expect(divideRegisters("10", "С")).toBe("9,9099099");
    expect(divideRegisters("12", "Г")).toBe("0,");
    expect(divideRegisters("15", "Е")).toBe("0,2292929");
    expect(divideRegisters("5", "Г")).toBe("0,-01");
    expect(divideRegisters("3", "С")).toBe("ЕГГ0Г");
  });

  it("K ЗН has a pinned sign result for verified structural hex mantissas", () => {
    expect(runUnaryRegisterProgram("8F", [IP1, K_SIGN])).toBe("1,");
    expect(runUnaryRegisterProgram("-8F", [IP1, K_SIGN])).toBe("-1,");
    expect(runUnaryRegisterProgram("0A", [IP1, K_SIGN])).toBe("1,");
    expect(runUnaryRegisterProgram("-0A", [IP1, K_SIGN])).toBe("-1,");
    expect(runUnaryRegisterProgram("A", [IP1, 0x0c, 0x02, 0x0b, K_SIGN])).toBe("1,");
    expect(runUnaryRegisterProgram("Г", [IP1, 0x0c, 0x02, 0x0b, K_SIGN])).toBe("1,");
    expect(runUnaryRegisterProgram("0F", [IP1, K_SIGN])).not.toBe("1,");
    expect(runUnaryRegisterProgram("F", [IP1, 0x0c, 0x02, 0x0b, K_SIGN])).not.toBe("1,");
  });

  it("only emulator-pinned A/B/C single hex digits are safe structural dot restores", () => {
    const restoreWithRecall = (literal: string): string => runUnaryRegisterProgram(literal, [IP1]);
    const restoreWithDot = (literal: string): string => runUnaryRegisterProgram(literal, [IP1, 0x42, 0x20, 0x0a]);

    expect(restoreWithDot("A")).toBe(restoreWithRecall("A"));
    expect(restoreWithDot("B")).toBe(restoreWithRecall("B"));
    expect(restoreWithDot("C")).toBe(restoreWithRecall("C"));
    expect(restoreWithDot("D")).toBe("ЕГГ0Г");
    expect(restoreWithDot("F")).not.toBe(restoreWithRecall("F"));
  });

  it("only emulator-pinned A/B/C single hex digits stay dot-safe after a closed sign pair", () => {
    const restoreWithRecall = (literal: string): string => runUnaryRegisterProgram(literal, [IP1]);
    const restoreAfterSignPair = (literal: string): string => runUnaryRegisterProgram(literal, [
      IP1,
      0x0b,
      0x0b,
      0x0a,
    ]);

    expect(restoreAfterSignPair("A")).toBe(restoreWithRecall("A"));
    expect(restoreAfterSignPair("B")).toBe(restoreWithRecall("B"));
    expect(restoreAfterSignPair("C")).toBe(restoreWithRecall("C"));
    expect(restoreAfterSignPair("D")).toBe("ЕГГ0Г");
    expect(restoreAfterSignPair("F")).not.toBe(restoreWithRecall("F"));
  });

  it("single hex exponent minus two multiplication table is operand-order-sensitive", () => {
    expect(multiplyRegisters("AE-2", "1")).toBe("0,-02");
    expect(multiplyRegisters("AE-2", "18")).toBe("0,2");
    expect(multiplyRegisters("BE-2", "18")).toBe("0,54");
    expect(multiplyRegisters("CE-2", "16")).toBe("9,04");
    expect(multiplyRegisters("AE-2", "15")).toBe("9,9");
    expect(multiplyRegisters("CE-2", "17")).toBe("9,");
    expect(multiplyRegisters("1", "ГE-2")).toBe("1,-01");
    expect(multiplyRegisters("2", "ГE-2")).toBe("2,-01");
    expect(multiplyRegisters("4", "ГE-2")).toBe("4,-01");
    expect(multiplyRegisters("5", "ГE-2")).toBe("5,-01");
    expect(multiplyRegisters("8", "ГE-2")).toBe("8,-01");
    expect(multiplyRegisters("16", "ГE-2")).toBe("1,6");
    expect(multiplyRegisters("18", "BE-2")).toBe("1,8");
    expect(multiplyRegisters("17", "CE-2")).toBe("1,7");
    expect(multiplyRegisters("18", "ЕE-2")).toBe("0,");
    expect(multiplyRegisters("ГE-2", "1")).toBe("3,-02");
    expect(multiplyRegisters("ГE-2", "5")).toBe("5,3-01");
    expect(multiplyRegisters("ГE-2", "16")).toBe("9,2");
    expect(multiplyRegisters("ЕE-2", "18")).toBe("9,48");
  });

  it("hex D exponent minus two addition and subtraction tables are operand-order-sensitive", () => {
    const subtractRegisters = (r1: string, r2: string): string => {
      const calc = new MK61();
      calc.setRegister("1", r1);
      calc.setRegister("2", r2);
      calc.loadProgram([IP1, IP2, SUBTRACT, STOP]);
      calc.pressSequence(["В/О", "С/П"]);
      calc.runUntilStable({ maxFrames: 300, stableFrames: 4 });
      return calc.displayText().replace(/\s/gu, "");
    };

    expect(addRegisters("ГE-2", "0")).toBe("1,3-01");
    expect(addRegisters("9", "ГE-2")).toBe("9,13");
    expect(addRegisters("BE-2", "0")).toBe("1,1-01");
    expect(addRegisters("ЕE-2", "6")).toBe("6,14");
    expect(subtractRegisters("ГE-2", "1")).toBe("-8,7-01");
    expect(subtractRegisters("ГE-2", "16")).toBe("-15,87");
    expect(subtractRegisters("BE-2", "1")).toBe("-8,9-01");
    expect(subtractRegisters("0", "BE-2")).toBe("5,-02");
    expect(subtractRegisters("0", "ГE-2")).toBe("3,-02");
    expect(subtractRegisters("0", "ЕE-2")).toBe("2,-02");
    expect(subtractRegisters("18", "ГE-2")).toBe("18,03");
  });

  it("single hex exponent minus two division table is operand-order-sensitive", () => {
    expect(divideRegisters("AE-2", "1")).toBe("0,-02");
    expect(divideRegisters("BE-2", "18")).toBe("6,1111111-03");
    expect(divideRegisters("CE-2", "16")).toBe("7,5-03");
    expect(divideRegisters("AE-2", "10")).toBe("0,-03");
    expect(divideRegisters("ГE-2", "17")).toBe("7,6470588-03");
    expect(divideRegisters("ГE-2", "0")).toBe("ЕГГ0Г");
    expect(divideRegisters("ГE-2", "2")).toBe("6,5-02");
    expect(divideRegisters("ГE-2", "16")).toBe("8,125-03");
    expect(divideRegisters("ЕE-2", "18")).toBe("7,7777777-03");
    expect(divideRegisters("0", "AE-2")).toBe("90,90909");
    expect(divideRegisters("1", "AE-2")).toBe("ЕГГ0Г");
    expect(divideRegisters("9", "AE-2")).toBe("89,90909");
    expect(divideRegisters("18", "BE-2")).toBe("943,43434");
    expect(divideRegisters("15", "BE-2")).toBe("900,");
    expect(divideRegisters("3", "CE-2")).toBe("ЕГГ0Г");
    expect(divideRegisters("12", "ГE-2")).toBe("000,");
    expect(divideRegisters("0", "ГE-2")).toBe("99,099099");
    expect(divideRegisters("5", "ГE-2")).toBe("00,");
    expect(divideRegisters("16", "ГE-2")).toBe("920,");
    expect(divideRegisters("16", "ЕE-2")).toBe("052,92929");
  });
});
