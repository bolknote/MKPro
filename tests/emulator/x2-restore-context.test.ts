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

describe("X2 restore context", () => {
  it("X->П immediately before ВП changes the restored X2 value even when the register is otherwise dead", () => {
    const withStore = [0x20, 0x35, 0x41, 0x0c, 0x50]; // F pi; К {x}; X->П 1; ВП; С/П
    const withoutStore = [0x20, 0x35, 0x0c, 0x50]; // F pi; К {x}; ВП; С/П

    expect(runX(withStore)).toBe("1,");
    expect(runX(withoutStore)).toBe("4,");
  });

  it("П->X immediately before ВП syncs X2 even when X already holds the same value", () => {
    const withRecall = [0x20, 0x35, 0x41, 0x61, 0x0c, 0x50]; // F pi; К {x}; X->П 1; П->X 1; ВП; С/П
    const withoutRecall = [0x20, 0x35, 0x41, 0x0c, 0x50]; // F pi; К {x}; X->П 1; ВП; С/П

    expect(runX(withRecall)).toBe("1,415926-1");
    expect(runX(withoutRecall)).toBe("1,");
  });
});
