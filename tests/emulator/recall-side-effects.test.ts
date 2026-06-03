import { createRequire } from "node:module";
import { describe, expect, it } from "vitest";

const require = createRequire(import.meta.url);

interface Mk61Instance {
  loadProgram(codes: number[] | string): { diagnostics: string[] };
  pressSequence(keys: string[]): void;
  runUntilStable(options: { maxFrames: number; stableFrames: number }): { stopped: boolean; frames: number };
  readRegister(register: string): string;
}
type Mk61Constructor = new (options?: { extended?: boolean }) => Mk61Instance;
const { MK61 } = require("./mk61.cjs") as { MK61: Mk61Constructor };

function runX(codes: number[]): string {
  const calc = new MK61();
  calc.loadProgram(codes);
  calc.pressSequence(["В/О", "С/П"]);
  calc.runUntilStable({ maxFrames: 300, stableFrames: 5 });
  return calc.readRegister("x").replace(/\s/gu, "");
}

describe("recall side effects", () => {
  it("П->X immediately after X->П still lifts the stack for following binary ops", () => {
    const withRecall = [0x20, 0x35, 0x41, 0x61, 0x10, 0x50]; // F pi; К {x}; X->П 1; П->X 1; +; С/П
    const withoutRecall = [0x20, 0x35, 0x41, 0x10, 0x50]; // F pi; К {x}; X->П 1; +; С/П

    expect(runX(withRecall)).toBe("2,831852-1");
    expect(runX(withoutRecall)).toBe("1,415926-1");
  });

  it("П->X stack lift can survive X-only ops before a later binary op", () => {
    const withRecall = [0x20, 0x35, 0x41, 0x61, 0x35, 0x10, 0x50]; // F pi; К {x}; X->П 1; П->X 1; К {x}; +; С/П
    const withoutRecall = [0x20, 0x35, 0x41, 0x35, 0x10, 0x50]; // F pi; К {x}; X->П 1; К {x}; +; С/П

    expect(runX(withRecall)).toBe("2,831852-1");
    expect(runX(withoutRecall)).toBe("1,415926-1");
  });
});
