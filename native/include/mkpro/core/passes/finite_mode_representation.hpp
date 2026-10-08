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
  // Non-null means the complete delivered preload environment, even if empty.
  // IR-only callers must leave it null: absence of metadata is not free storage.
  const std::vector<PreloadReport>* runtime_preloads = nullptr;
  // The caller has symbolized physical references and validated all existing
  // compiler-owned decimal charges. These charges stay rewrite barriers and
  // every unchanged indirect target retains its exact address. The policy for
  // an observed stock-machine empty return is physical 01 in this context.
  bool preserve_compiler_selector_charges = false;
};

// Each alternative is independently proved. A neutral/larger representation
// is an enabling search node, never a locally published regression.
std::vector<PassResult> finite_mode_representation_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const FiniteModeCandidateOptions& options = {});

IrPass finite_mode_representation_pass();

} // namespace mkpro::core::passes
