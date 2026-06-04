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

  it("В/О syncs X2 on a direct subroutine return before a following ВП", () => {
    const withRecall = [0x20, 0x35, 0x41, 0x61, 0x53, 0x08, 0x0c, 0x50, 0x52]; // ... П->X 1; ПП 08; ВП; С/П; В/О
    const withoutRecall = [0x20, 0x35, 0x41, 0x53, 0x07, 0x0c, 0x50, 0x52]; // ... ПП 07; ВП; С/П; В/О

    expect(runX(withRecall)).toBe("1,415926-1");
    expect(runX(withoutRecall)).toBe("1,415926-1");
  });

  it("F0..FF preserve X but sync the current X into X2", () => {
    // 2; К {x} leaves X=0 while X2 still holds the old digit-entry 2.
    // КНОП does not sync X2, so `.` restores 2. F* empty opcodes do sync X2,
    // so the same `.` keeps the current 0.
    expect(runX([0x02, 0x35, 0x54, 0x0a, 0x50])).toBe("2,");

    for (let opcode = 0xf0; opcode <= 0xff; opcode += 1) {
      expect(runX([0x02, 0x35, opcode, 0x50])).toBe("0,");
      expect(runX([0x02, 0x35, opcode, 0x0a, 0x50])).toBe("0,");
    }
  });

  it("В↑ preserves X but syncs the current X into X2", () => {
    // Same setup as the F* test: after К {x}, X is 0 and X2 is still 2.
    // В↑ shifts the stack, but X remains 0 and the following `.` observes
    // the synchronized 0 rather than restoring the old 2.
    expect(runX([0x02, 0x35, 0x54, 0x0a, 0x50])).toBe("2,");
    expect(runX([0x02, 0x35, 0x0e, 0x50])).toBe("0,");
    expect(runX([0x02, 0x35, 0x0e, 0x0a, 0x50])).toBe("0,");
  });

  it("closed-context /-/ updates proved decimal X2 and makes a following dot redundant", () => {
    expect(runX([0x00, 0x02, 0xf0, 0x0b, 0x50])).toBe("-2,");
    expect(runX([0x00, 0x02, 0xf0, 0x0b, 0x0a, 0x50])).toBe("-2,");
    expect(runX([0x00, 0x02, 0x0b, 0xf0, 0x0b, 0x50])).toBe("2,");
    expect(runX([0x0d, 0x0b, 0x50])).toBe("0,");
    expect(runX([0x0d, 0x0b, 0x0a, 0x50])).toBe("0,");
  });
});
