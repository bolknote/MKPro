#pragma once

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/ir.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace mkpro::core {

// One direct call in a complete three-call helper family.  Two calls are
// ordinary entries whose first two continuation cells are byte-identical; the
// remaining call is the divergent entry that must return before that shared
// continuation in the eventual dual-mode layout.
struct SharedHelperContinuationCall {
  std::size_t call_item_index = 0;
  std::size_t operand_item_index = 0;
  std::size_t join_item_index = 0;
  std::size_t store_item_index = 0;
  int call_address = -1;
  bool ordinary = false;
};

struct SharedHelperContinuationOptions {
  AddressSpaceModel address_space_model = AddressSpaceModel::Standard;

  // Complete resolved target set for every indirect control-flow MachineItem.
  // The verifier fails closed when an indirect flow item has no entry here.
  std::map<std::size_t, std::vector<int>> proved_indirect_flow_targets;

  // Bound for the path-sensitive X2 convergence proof after each ordinary
  // continuation.  This is a safety limit, not an optimization heuristic.
  int maximum_convergence_states = 512;
};

struct SharedHelperContinuationProof {
  bool proved = false;
  std::string helper_label;
  int input_cells = 0;
  int helper_body_start_address = -1;
  int helper_body_end_address = -1;
  int helper_body_cells = 0;
  int helper_return_address = -1;
  std::size_t helper_label_item_index = 0;
  std::size_t helper_return_item_index = 0;
  std::size_t helper_block_end_item_index = 0;
  int join_opcode = -1;
  int store_opcode = -1;
  int continuation_cells = 0;
  std::vector<SharedHelperContinuationCall> calls;
  std::vector<std::size_t> ordinary_call_item_indices;
  std::size_t divergent_call_item_index = 0;
  std::vector<std::string> reasons;
};

// Prove a purely structural candidate.  The helper must be straight-line with
// one explicit return and a complete set of exactly three direct calls.  Two
// calls must continue with the same supported commutative join followed by the
// same direct store; the third continuation must differ.  No source names,
// constants, comments, or absolute program-size signature participate.
SharedHelperContinuationProof
verify_shared_helper_continuation(const std::vector<MachineItem>& items,
                                  const std::string& helper_label,
                                  const SharedHelperContinuationOptions& options = {});

// Scan every label and return the first fully proved candidate.  This function
// is intentionally analysis-only. shared_helper_wrapper consumes this
// certificate and performs a transactional wrapper rewrite without changing
// the divergent call.
SharedHelperContinuationProof
find_shared_helper_continuation(const std::vector<MachineItem>& items,
                                const SharedHelperContinuationOptions& options = {});

} // namespace mkpro::core
