#pragma once

#include "mkpro/core/callee_hole_boundary_normalization.hpp"

namespace mkpro::core::detail {

// Conditional continuation of a compiler-owned selector charge. Raw
// barriers and edge restrictions describe just this proof; they do not
// modify or certify the caller's machine image/CFG for any other use.
struct StackEntryRestriction {
  std::vector<bool> barriers;
  int jump_opcode = -1;
  int call_opcode = -1;
  int target_address = -1;
};

bool prove_restricted_post_layout_stack_equality(
    const std::vector<MachineItem>& items, const PostLayoutExecutionView& view,
    const StackEntryRestriction& restriction, int entry_address,
    const std::vector<std::size_t>* entry_states, StackValueEqualityState initial,
    const PostLayoutStackEntryProofOptions& options);

} // namespace mkpro::core::detail
