#pragma once

#include "mkpro/core/ir.hpp"
#include "mkpro/core/stack_value_equivalence.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

namespace mkpro::core {

struct AuthoritativePostLayoutControlFlow;

struct StackEntryProofNode {
  int opcode = -1;
  StackValueEqualityStepKind kind = StackValueEqualityStepKind::Flow;
  std::optional<std::size_t> next;
  std::vector<std::size_t> call_targets;
  bool call = false;
  bool barrier = false;
};

using StackEntryProofReader =
    std::function<std::optional<StackEntryProofNode>(std::size_t)>;

// A bounded equality proof, shared by IR selection and final-code validation.
// An unequal value may be discarded, but never consumed or observed. Calls
// must converge on every possible callee entry before any observation there.
bool prove_stack_entry_equality(const StackEntryProofReader& reader,
                                std::size_t entry, StackValueEqualityState state);
// Continue the equality proof through exact caller/return contexts. Both
// executions have equal memory and decimal-entry modes at entry; every
// reachable instance of the entry address must hide the differing stack
// components before a consumer or externally observable stop.
bool prove_post_layout_stack_entry_equality(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow, int entry_address,
    StackValueEqualityState state);
bool prove_ir_stack_entry_equality(const std::vector<IrOp>& ops,
                                   std::size_t entry, StackValueEqualityState state);

// B-up; decimal selector; store; F-reverse restores X/Y/Z. T and physical
// last-X1 are clobbered, and X2 is conservatively unknown until synchronized.
StackValueEqualityState xyz_preserving_selector_charge_state();

// These entry-closing commands let the first decimal digit supply the lift.
// A literal/Enter/manual-input predecessor is deliberately not such a proof.
bool selector_charge_entry_closer_opcode(int opcode);
bool selector_charge_has_automatic_entry_lift(const std::vector<IrOp>& ops,
                                             std::size_t entry,
                                             AddressSpaceModel model = AddressSpaceModel::Standard);

struct CalleeHoleBoundaryNormalization {
  std::vector<IrOp> ops;
  int expanded_calls = 0;
  int arithmetic_groups = 0;
};

// Expose short, symbolic tail-call wrappers for a larger outlining transaction.
// This is NOT a standalone optimization: the caller must repay every expanded
// cell by a strictly smaller, proof-valid shared region.
CalleeHoleBoundaryNormalization
normalize_callee_hole_boundaries(const std::vector<IrOp>& ops);

// Exact bounded return-stack exploration; recursion, unresolved flow and proof
// budget exhaustion reject the proposed outline rather than guessing a bound.
bool callee_hole_return_stack_fits(const std::vector<IrOp>& ops);

} // namespace mkpro::core
