#pragma once

#include "mkpro/core/passes/helpers.hpp"

namespace mkpro::core::passes {

// Select the stored representation of a finite, internal mode from its uses.
// Definitions may receive finite literal inputs through exact call/return
// contexts. A shared encoding is selected jointly with separately synthesized
// arithmetic consumer decoders and stable indirect-address constraints.
// Unsupported or unprojected observations fail closed. Calls retain their exact
// return contexts; aliases, external entries and hidden stack state are proof
// obligations, not assumptions based on source names.
PassResult finite_mode_representation(const std::vector<IrOp>& ops,
                                      const PassContext& context);
struct FiniteModeCandidateOptions {
  int maximum_growth_cells = 0;
  std::size_t maximum_candidates = 8;
  bool allow_target_relayout = false;
};

// Each alternative is independently proved. A neutral/larger representation
// is an enabling search node, never a locally published regression.
std::vector<PassResult> finite_mode_representation_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const FiniteModeCandidateOptions& options = {});

IrPass finite_mode_representation_pass();

} // namespace mkpro::core::passes
