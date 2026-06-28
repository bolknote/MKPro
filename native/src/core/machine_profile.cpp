#include "mkpro/core/machine_profile.hpp"

#include <algorithm>

namespace mkpro {

const MachineProfile& mk61_profile() {
  static const MachineProfile profile{
      .id = "mk61",
      .features =
          {
              {"branch-removal", "machine",
               "Documented arithmetic, sign, extrema, and zero-test opcodes can replace "
               "provably equivalent branches."},
              {"fl-decrement-branch", "machine",
               "F L0..F L3 are available for compact decrement-and-continue/decrement-and-branch "
               "forms."},
              {"return-empty-stack-jump", "machine",
               "В/О can be used as БП 01 when static control-flow proves that no return frame is "
               "pending."},
              {"undocumented-opcodes", "machine",
               "F0..FF and undocumented aliases are available when exact-machine preconditions "
               "are proved."},
              {"dark-entries", "machine",
               "Formal dark/super-dark entry addresses are available to the layout solver."},
              {"super-dark-dispatch", "machine",
               "Indirect К БП R to FA..FF can execute one command at 48..53 and continue at "
               "01..06."},
              {"indirect-flow", "machine",
               "К БП/К ПП/К x?0 indirect flow commands are available to the optimizer."},
              {"indirect-memory", "machine",
               "К X->П/К П->X indirect memory commands are available when a selector register is "
               "proved live."},
              {"code-data-overlay", "machine",
               "Address operands, constants, opcodes, and display bytes may share cells after "
               "conflict checks."},
              {"address-constants", "machine",
               "Address cells may double as constants for indirect flow and data transforms."},
              {"display-bytes", "machine",
               "Packed display bytes, hexadecimal mantissa digits, and sign-digit forms are "
               "available."},
              {"x2-register", "machine",
               "The hidden X2 display register can be scheduled when observable display semantics "
               "are preserved; opcode metadata distinguishes X2-preserving, X2-syncing/"
               "normalizing, and X2-restoring commands."},
              {"negative-zero-degree", "machine",
               "Negative-zero exponent values such as 1|-00 can act as constants or threshold "
               "sentinels when X2-normalization boundaries are controlled."},
              {"grd-angle-mode", "machine",
               "Trigonometric identities that depend on the Р-ГРД-Г switch are valid only when "
               "the source program declares expected_mode(\"grd\")."},
              {"extra-cells", "machine",
               "Extra physical cells are tracked separately from official program cells."},
              {"error-stops", "machine",
               "Domain-error stops are available only when the source semantics permits trap-like "
               "termination."},
              {"r0-t-alias", "machine",
               "R0/T and *F alias behavior is available, including normal R0 transformation; "
               "aliases do not preserve R0."},
              {"r0-fractional-sentinel", "machine",
               "Fractional positive R0 states can select R3 or jump to 99 while leaving the "
               "-99999999 sentinel."},
              {"raw-display-5f", "machine",
               "Opcode 5F is a display/raw-state transform in this ROM, not a hang."},
          },
      .emulator_facts =
          {
              {"return-empty-stack-jumps-to-01", "probed",
               "В/О with an empty return stack behaves as one-cell БП 01 in continuous "
               "execution."},
              {"r0-star-f-aliases", "probed",
               "*F aliases behave like the corresponding *0 commands, including R0 "
               "transformation; they do not preserve R0."},
              {"super-dark-fa-ff-indirect", "probed",
               "Indirect К БП R with R=FA..FF executes one command at 48..53, then continues at "
               "01..06."},
              {"fa-direct-vs-indirect", "probed",
               "Direct БП FA consumes/overwrites the following operand byte, while indirect К БП "
               "R leaves 01..06 usable as tail code."},
              {"r0-fractional-jump-99", "probed",
               "К БП 0 with R0 in fractional positive/small states jumps to 99 and leaves "
               "R0=-99999999."},
              {"r0-fractional-selects-r3", "probed",
               "К П->X 0 and К X->П 0 with 0<R0<1 access R3 and leave R0=-99999999."},
              {"x2-restore-boundaries", "probed",
               "ВП, '.', '/-/', and digit-entry X2 restoration boundaries are modeled as "
               "display-state boundaries."},
              {"negative-zero-degree-threshold", "probed",
               "With 1|-00 in Y, multiplying by X and then normalizing through В↑ yields a "
               "zero/nonzero threshold at |X|=1."},
              {"grd-angle-trig-identities", "documented",
               "In ГРД angle mode, cos(100) is zero and acos(0) is 100; optimizer use must be "
               "guarded by expected_mode(\"grd\")."},
              {"step-vs-run-delta", "probed",
               "Continuous-run behavior is the default profile; step-only divergences are "
               "explicit exact-machine facts."},
              {"raw-display-5f", "probed",
               "Opcode 5F leaves internal X intact but mutates display/raw state; it is usable "
               "only as an explicit display-state trick."},
          },
  };
  return profile;
}

bool machine_supports(const MachineProfile& profile, std::string_view feature_id) {
  return std::ranges::any_of(profile.features, [feature_id](const auto& feature) {
    return feature.id == feature_id;
  });
}

}  // namespace mkpro
