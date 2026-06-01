import { formalAddressInfo } from "../formal-address.ts";
import { formatAddress, getOpcode } from "../opcodes.ts";
import type { MachineAddressRef, MachineItem, MachineOp } from "../types.ts";

/**
 * Owns the mutable machine-code emission state for one lowering attempt:
 * the emitted item stream, the label counter, and the X-register tracking
 * facts that the lowering passes consult and update. The low-level emit
 * primitives (op/jump/address/label/number) and their X-tracking side effects
 * live here; higher-level helpers that also need register allocation or report
 * collection stay in `EmitContext` and delegate to these primitives.
 *
 * Behaviour is intentionally byte-for-byte identical to the previous in-class
 * implementation; this is a structural extraction only.
 *
 * The dashed-coord report body is carried opaquely (the emitter only stores and
 * clears it), so its concrete shape is a type parameter supplied by the caller.
 */
export class MachineEmitter<TDashed = unknown> {
  readonly items: MachineItem[] = [];
  private labelCounter = 0;

  currentXVariable: string | undefined;
  // Every variable known to hold the value currently in X (a copy-equivalence
  // class for the straight-line region). After `A = B`, X equals both A and B,
  // so a read of either can reuse X. Reset by any X-clobbering op and at
  // control-flow boundaries; grown only by stores that copy X into a name.
  currentXAliases = new Set<string>();
  currentXKnownZero = false;
  coordListCounterKnownOne = false;
  readonly zeroAddressLabels = new Set<string>();
  // Meet of the X-variable carried by every recorded branch/jump edge into a
  // label (undefined value = edges disagree / unknown). Used by emitLabel to
  // keep stack-reuse facts sound across control-flow joins.
  readonly labelEdgeX = new Map<string, string | undefined>();
  currentXDashedCoordReportBody: TDashed | undefined;
  // True when the machine is mid number-entry, so the next number literal would
  // concatenate digits (e.g. 1 then 3 -> 13) instead of starting a new value.
  machineEntryOpen = false;

  emitNumber(raw: string): void {
    this.currentXVariable = undefined;
    this.currentXAliases.clear();
    this.currentXKnownZero = false;
    this.currentXDashedCoordReportBody = undefined;
    // If the machine is still in number-entry mode, a fresh literal would
    // concatenate onto the previous value (1 then 3 -> 13) or onto a just-read
    // input. Push the previous value with В↑ so the new number starts clean.
    if (this.machineEntryOpen) {
      this.emitOp(0x0e, "В↑", "separate adjacent number entry");
    }
    const normalized = raw.trim().toLowerCase();
    const negative = normalized.startsWith("-");
    const unsigned = negative ? normalized.slice(1) : normalized;
    const [mantissa, exponent] = unsigned.split("e");
    for (const char of mantissa ?? "0") {
      if (char === ".") this.emitOp(0x0a, ".");
      else if (/\d/u.test(char)) this.emitOp(Number(char), char);
    }
    if (exponent !== undefined) {
      this.emitOp(0x0c, "ВП", "exponent");
      const expNegative = exponent.startsWith("-");
      const expDigits =
        expNegative || exponent.startsWith("+") ? exponent.slice(1) : exponent;
      for (const char of expDigits) {
        if (/\d/u.test(char)) this.emitOp(Number(char), char);
      }
      if (expNegative) this.emitOp(0x0b, "/-/", "negative exponent");
    }
    if (negative) this.emitOp(0x0b, "/-/", "negative number");
    this.currentXKnownZero = Number(raw) === 0;
  }

  emitJump(
    opcode: number,
    mnemonic: string,
    target: string | number,
    comment?: string,
    sourceLine?: number,
  ): void {
    // Capture the X-fact on this edge *before* the branch/jump opcode clears
    // it, so merge points can verify all predecessors agree (see emitLabel).
    if (typeof target === "string") this.recordLabelEdge(target, this.currentXVariable);
    this.emitOp(opcode, mnemonic, comment, sourceLine);
    this.emitAddress(target, comment ?? mnemonic, sourceLine);
  }

  emitAddress(
    target: string | number,
    comment?: string,
    sourceLine?: number,
  ): void {
    const item: MachineAddressRef = { kind: "address", target };
    if (comment !== undefined) item.comment = comment;
    if (sourceLine !== undefined) item.sourceLine = sourceLine;
    this.items.push(item);
  }

  emitFormalAddress(
    opcode: number,
    comment?: string,
    sourceLine?: number,
  ): void {
    const info = formalAddressInfo(opcode);
    const item: MachineAddressRef = { kind: "address", target: info.ordinal, formalOpcode: opcode };
    if (comment !== undefined) item.comment = `${comment}; formal ${info.label}->${formatAddress(info.actual)}`;
    if (sourceLine !== undefined) item.sourceLine = sourceLine;
    this.items.push(item);
  }

  emitOp(
    opcode: number,
    mnemonic?: string,
    comment?: string,
    sourceLine?: number,
    raw = false,
  ): void {
    const info = getOpcode(opcode);
    const op: MachineOp = {
      kind: "op",
      opcode,
      mnemonic: mnemonic ?? info.name,
    };
    if (comment !== undefined) op.comment = comment;
    if (sourceLine !== undefined) op.sourceLine = sourceLine;
    if (raw) op.raw = true;
    this.items.push(op);
    if (opcode >= 0x80 && opcode <= 0xfe) this.coordListCounterKnownOne = false;
    this.currentXVariable = undefined;
    this.currentXAliases.clear();
    this.currentXKnownZero = false;
    this.currentXDashedCoordReportBody = undefined;
    // Digit / '.' / sign / ВП opcodes (0x00..0x0c) keep the machine in
    // number-entry mode; every other op finalizes it.
    this.machineEntryOpen = opcode <= 0x0c;
  }

  recordLabelEdge(label: string, fact: string | undefined): void {
    if (this.labelEdgeX.has(label)) {
      if (this.labelEdgeX.get(label) !== fact) this.labelEdgeX.set(label, undefined);
    } else {
      this.labelEdgeX.set(label, fact);
    }
  }

  emitLabel(
    name: string,
    metadata: { procedureBoundary?: "start" | "end"; procedureName?: string; hidden?: boolean } = {},
  ): void {
    if (this.items.every((item) => item.kind === "label")) this.zeroAddressLabels.add(name);
    this.coordListCounterKnownOne = false;
    this.items.push({ kind: "label", name, ...metadata });
    // A label is a control-flow merge point. The "current X" fact tracked
    // textually only reflects the fall-through edge; reusing it across a join
    // is sound only if every incoming branch/jump edge agrees on the same
    // variable. Otherwise the value in X is path-dependent and must not be
    // reused (this is what made display-stack-reuse pick up garbage left by a
    // sibling branch). Labels with no recorded edge keep the fall-through fact.
    if (this.labelEdgeX.has(name)) {
      const edgeFact = this.labelEdgeX.get(name);
      this.currentXVariable = this.currentXVariable === edgeFact ? this.currentXVariable : undefined;
      this.currentXKnownZero = false;
    }
    // Copy-equivalence aliases never survive a merge: other predecessors reach
    // this label with X clobbered, so only the proven single fact may remain.
    this.currentXAliases = this.currentXVariable !== undefined ? new Set([this.currentXVariable]) : new Set();
    this.currentXDashedCoordReportBody = undefined;
  }

  freshLabel(prefix: string): string {
    const label = `__${prefix}_${this.labelCounter}`;
    this.labelCounter += 1;
    return label;
  }
}
