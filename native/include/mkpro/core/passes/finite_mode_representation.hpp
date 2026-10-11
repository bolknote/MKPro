#pragma once

#include "mkpro/core/passes/helpers.hpp"

#include <map>
#include <string>

namespace mkpro::core::passes {

// Select the stored representation of a finite, internal mode from its uses.
// Definitions may receive finite literal inputs through exact call/return
// contexts. A shared encoding is selected jointly with separately synthesized
// arithmetic consumer decoders and stable indirect-address constraints.
// Predicate-only reads may join finite and ordinary dynamic epochs when every
// finite row keeps its truth value and both numeric continuations prove safe.
// Opaque max boundaries require restoration instead of an ordering assumption:
// a singleton finite integer may be rematerialized at a proved fresh-entry
// boundary, with its code cost and all changed stack/X1/X2 slots accounted for.
// Other unsupported or unprojected observations fail closed. Calls retain their exact
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
  // Conditional command-identity bindings owned by the joint search. They
  // describe the runtime destinations, not the current speculative geometry.
  // Complete encoded origins may refine callback contexts for the relational
  // data proof only; the unrefined CFG still owns relocation and publication.
  // A caller must discharge every binding before publishing an artifact.
  const std::map<std::string, int>* assumed_target_addresses = nullptr;
};

struct DeferredFiniteModeRepresentation {
  PassResult transformation;
  std::map<std::string, int> required_target_addresses;
};

// The relational data/stack proof is complete, but placement is deliberately
// NOT performed. This type is not an executable artifact. Only a joint-search
// finalizer that proves all required destinations may publish its contents.
std::vector<DeferredFiniteModeRepresentation>
deferred_finite_mode_representation_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const FiniteModeCandidateOptions& options = {});

// Each alternative is independently proved. A neutral/larger representation
// is an enabling search node, never a locally published regression.
std::vector<PassResult> finite_mode_representation_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const FiniteModeCandidateOptions& options = {});

IrPass finite_mode_representation_pass();

} // namespace mkpro::core::passes
