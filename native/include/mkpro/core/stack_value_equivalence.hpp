#pragma once

#include "mkpro/core/opcodes.hpp"

#include <array>

namespace mkpro::core {

// Equality domain for two executions of the same straight-line code. The
// callee-hole mutating-selector proof starts with only X and X2 different and
// accepts the rewrite once every observable stack component is equal again.
struct StackValueEqualityState {
  std::array<bool, 4> stack_equal = {false, true, true, true};
  bool x2_equal = false;
};

enum class StackValueEqualityStepKind {
  Plain,
  Recall,
  Store,
  Flow,
};

enum class StackValueEqualityTransfer {
  Continue,
  Converged,
  Rejected,
};

inline bool stack_values_fully_equal(const StackValueEqualityState& state) {
  return state.x2_equal && state.stack_equal.at(0) && state.stack_equal.at(1) &&
         state.stack_equal.at(2) && state.stack_equal.at(3);
}

// Transfer one identical opcode in both executions. `reads_distinct_register`
// is true when a recall/indirect operation observes the selector register whose
// hypothetical stable charge differs from the actual mutating charge.
//
// This is intentionally a proof, not an optimistic simulation: operations that
// would compute from a differing operand or expose control flow are rejected.
inline StackValueEqualityTransfer transfer_stack_value_equality(
    StackValueEqualityState& state, int opcode, StackValueEqualityStepKind kind,
    bool reads_distinct_register = false) {
  if (kind == StackValueEqualityStepKind::Flow)
    return stack_values_fully_equal(state) ? StackValueEqualityTransfer::Converged
                                           : StackValueEqualityTransfer::Rejected;

  const std::array<bool, 4> old = state.stack_equal;
  if (kind == StackValueEqualityStepKind::Recall) {
    if (reads_distinct_register)
      return StackValueEqualityTransfer::Rejected;
    state.stack_equal = {true, old.at(0), old.at(1), old.at(2)};
    // A register recall saves the previous X in X2 before replacing X.
    state.x2_equal = old.at(0);
    return stack_values_fully_equal(state) ? StackValueEqualityTransfer::Converged
                                           : StackValueEqualityTransfer::Continue;
  }
  if (kind == StackValueEqualityStepKind::Store) {
    if (reads_distinct_register || !old.at(0))
      return StackValueEqualityTransfer::Rejected;
    return stack_values_fully_equal(state) ? StackValueEqualityTransfer::Converged
                                           : StackValueEqualityTransfer::Continue;
  }

  // The following stack-only operations can safely move or erase an unequal
  // value without evaluating it.
  if (opcode == 0x0d) {  // Cx
    state.stack_equal.at(0) = true;
    state.x2_equal = old.at(0);
  } else if (opcode == 0x0e) {  // B-up: X, X, Y, Z
    state.stack_equal = {old.at(0), old.at(0), old.at(1), old.at(2)};
    state.x2_equal = old.at(0);
  } else if (opcode == 0x0f) {  // F Bx: Y, Z, T, T
    state.stack_equal = {old.at(1), old.at(2), old.at(3), old.at(3)};
    state.x2_equal = old.at(0);
  } else if (opcode == 0x14) {  // X <-> Y
    state.stack_equal = {old.at(1), old.at(0), old.at(2), old.at(3)};
  } else {
    const OpcodeInfo& info = opcode_by_code(opcode);
    if (info.takes_address || info.stack_effect == StackEffect::Barrier ||
        info.stack_effect == StackEffect::Unknown || info.x2_effect == X2Effect::Restores ||
        info.x2_effect == X2Effect::Unknown) {
      return StackValueEqualityTransfer::Rejected;
    }

    switch (info.stack_effect) {
      case StackEffect::Preserves:
        // A deterministic unary operation is safe only when its X operands are
        // already equal. Direct stores are classified separately above.
        if (!old.at(0))
          return StackValueEqualityTransfer::Rejected;
        break;
      case StackEffect::Shifts:
        // Constant producers (not recalls) push an equal value in both runs.
        state.stack_equal = {true, old.at(0), old.at(1), old.at(2)};
        break;
      case StackEffect::ConsumeYDrop:
        if (!old.at(0) || !old.at(1))
          return StackValueEqualityTransfer::Rejected;
        state.stack_equal = {true, old.at(2), old.at(3), old.at(3)};
        break;
      case StackEffect::ConsumeYKeep:
        if (!old.at(0) || !old.at(1))
          return StackValueEqualityTransfer::Rejected;
        state.stack_equal = {true, old.at(1), old.at(2), old.at(3)};
        break;
      case StackEffect::Exposes:
        state.stack_equal = {old.at(1), old.at(2), old.at(3), old.at(3)};
        break;
      case StackEffect::Barrier:
      case StackEffect::Unknown:
        return StackValueEqualityTransfer::Rejected;
    }

    if (info.x2_effect == X2Effect::Affects)
      state.x2_equal = old.at(0);
  }

  return stack_values_fully_equal(state) ? StackValueEqualityTransfer::Converged
                                         : StackValueEqualityTransfer::Continue;
}

}  // namespace mkpro::core
