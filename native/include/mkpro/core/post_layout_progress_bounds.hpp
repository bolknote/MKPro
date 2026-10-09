#pragma once

#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/result.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace mkpro::core {

struct MonotoneProgressWitness {
  int register_index = -1;
  std::size_t source_state = 0;
  std::size_t target_state = 0;
  PostLayoutExecutionEdgeKind edge_kind = PostLayoutExecutionEdgeKind::DirectTarget;
  std::size_t maximum_traversals = 0;
  std::vector<std::size_t> update_states;
};

struct MonotoneProgressProof {
  std::optional<MonotoneProgressWitness> witness;
  std::size_t work = 0;
  bool exhausted = false;
};

// Prove a finite resource, not a likely trip count: an OR-only register has
// at most 28 strict payload growth steps after its first canonicalization.
// The guard must compare the immediately preceding and updated words, and a
// consumed comparison cannot reach the same guard again without a new update.
// All writes, aliases, entry protocols and matched return contexts are checked.
MonotoneProgressProof prove_post_layout_monotone_progress(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<bool>& automatic_entry_lift,
    const std::vector<PreloadReport>& preloads,
    std::size_t maximum_work = 100000);

struct ProgressExpandedControlFlow {
  AuthoritativePostLayoutControlFlow control;
  std::vector<bool> automatic_entry_lift;
  std::vector<std::size_t> analysis_order;
};

// An analysis-only product graph. It removes just the proved impossible
// (bound + 1)th growth edge; it is never an executable replacement CFG.
std::optional<ProgressExpandedControlFlow> expand_post_layout_progress(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<bool>& automatic_entry_lift,
    const MonotoneProgressWitness& witness,
    std::size_t maximum_states = 20000);

} // namespace mkpro::core
