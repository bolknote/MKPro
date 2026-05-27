import type {
  EmulatorFactReport,
  MachineFeatureUseReport,
  MachineId,
} from "./types.ts";

export interface MachineProfile {
  id: MachineId;
  features: MachineFeatureUseReport[];
  emulatorFacts: EmulatorFactReport[];
}

export const MK61_PROFILE: MachineProfile = {
  id: "mk61",
  features: [
    {
      id: "branch-removal",
      source: "machine",
      detail: "Documented arithmetic, sign, extrema, and zero-test opcodes can replace provably equivalent branches.",
    },
    {
      id: "fl-decrement-branch",
      source: "machine",
      detail: "F L0..F L3 are available for compact decrement-and-continue/decrement-and-branch forms.",
    },
    {
      id: "return-empty-stack-jump",
      source: "machine",
      detail: "В/О can be used as БП 01 when static control-flow proves that no return frame is pending.",
    },
    {
      id: "undocumented-opcodes",
      source: "machine",
      detail: "F0..FF and undocumented aliases are available when exact-machine preconditions are proved.",
    },
    {
      id: "dark-entries",
      source: "machine",
      detail: "Formal dark/super-dark entry addresses are available to the layout solver.",
    },
    {
      id: "super-dark-dispatch",
      source: "machine",
      detail: "Indirect К БП R to FA..FF can execute one command at 48..53 and continue at 01..06.",
    },
    {
      id: "indirect-flow",
      source: "machine",
      detail: "К БП/К ПП/К x?0 indirect flow commands are available to the optimizer.",
    },
    {
      id: "code-data-overlay",
      source: "machine",
      detail: "Address operands, constants, opcodes, and display bytes may share cells after conflict checks.",
    },
    {
      id: "address-constants",
      source: "machine",
      detail: "Address cells may double as constants for indirect flow and data transforms.",
    },
    {
      id: "display-bytes",
      source: "machine",
      detail: "Packed display bytes, hexadecimal mantissa digits, and sign-digit forms are available.",
    },
    {
      id: "x2-register",
      source: "machine",
      detail: "The hidden X2 display register can be scheduled when observable display semantics are preserved.",
    },
    {
      id: "extra-cells",
      source: "machine",
      detail: "Extra physical cells are tracked separately from official program cells.",
    },
    {
      id: "error-stops",
      source: "machine",
      detail: "Domain-error stops are available only when the source semantics permits trap-like termination.",
    },
    {
      id: "r0-t-alias",
      source: "machine",
      detail: "R0/T and *F alias behavior is available, including normal R0 transformation; aliases do not preserve R0.",
    },
    {
      id: "r0-fractional-sentinel",
      source: "machine",
      detail: "Fractional positive R0 states can select R3 or jump to 99 while leaving the -99999999 sentinel.",
    },
    {
      id: "raw-display-5f",
      source: "machine",
      detail: "Opcode 5F is a display/raw-state transform in this ROM, not a hang.",
    },
  ],
  emulatorFacts: [
    {
      id: "return-empty-stack-jumps-to-01",
      status: "probed",
      detail: "В/О with an empty return stack behaves as one-cell БП 01 in continuous execution.",
    },
    {
      id: "r0-star-f-aliases",
      status: "probed",
      detail: "*F aliases behave like the corresponding *0 commands, including R0 transformation; they do not preserve R0.",
    },
    {
      id: "super-dark-fa-ff-indirect",
      status: "probed",
      detail: "Indirect К БП R with R=FA..FF executes one command at 48..53, then continues at 01..06.",
    },
    {
      id: "fa-direct-vs-indirect",
      status: "probed",
      detail: "Direct БП FA consumes/overwrites the following operand byte, while indirect К БП R leaves 01..06 usable as tail code.",
    },
    {
      id: "r0-fractional-jump-99",
      status: "probed",
      detail: "К БП 0 with R0 in fractional positive/small states jumps to 99 and leaves R0=-99999999.",
    },
    {
      id: "r0-fractional-selects-r3",
      status: "probed",
      detail: "К П->X 0 and К X->П 0 with 0<R0<1 access R3 and leave R0=-99999999.",
    },
    {
      id: "x2-restore-boundaries",
      status: "probed",
      detail: "ВП, '.', '/-/', and digit-entry X2 restoration boundaries are modeled as display-state boundaries.",
    },
    {
      id: "step-vs-run-delta",
      status: "probed",
      detail: "Continuous-run behavior is the default profile; step-only divergences are explicit exact-machine facts.",
    },
    {
      id: "raw-display-5f",
      status: "probed",
      detail: "Opcode 5F leaves internal X intact but mutates display/raw state; it is usable only as an explicit display-state trick.",
    },
  ],
};

export function machineSupports(profile: MachineProfile, featureId: string): boolean {
  return profile.features.some((feature) => feature.id === featureId);
}
