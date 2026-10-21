#pragma once

#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/passes/register_coalesce.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"

#include <map>
#include <string>

namespace mkpro::core::passes {
struct ContextualCopyTransportOptions {
  std::size_t maximum_work = 200000;
  std::size_t maximum_candidates = 2;
  std::size_t maximum_copy_sites = 8;
  int maximum_growth_cells = 4;
  // Conditional complete-artifact analysis. Both pointers must be supplied
  // together. Encoded origins name the old selector words' command identities,
  // not the pending geometry; the caller must close every equation before
  // publication. Omission preserves the historical closed-layout API.
  const PostLayoutControlFlowOptions* control = nullptr;
  const std::map<std::string,int>* encoded_target_origins = nullptr;
  // Complete delivered initializer environment, never setup assumptions at a
  // manual resume. Required only for independent finite-identity/GVN queries.
  const std::vector<PreloadReport>* runtime_preloads = nullptr;
  // Complete-artifact opt-in. Direct formal entries retain their exact rooted
  // counter/return contexts in every emitted relocation proof. Indirect formal
  // aliases remain unsupported; both control and encoded origins are required.
  bool preserve_formal_execution_frames = false;
};
// Conditional, complete-program transactions. Forward only static reads whose
// every exact caller has the same source word. Sink a still-observed copied
// value past the matched return, never across an observation or source write.
// The caller must close all original selector/placement obligations before
// publication. No original initializer or arithmetic constant is retuned here.
struct ContextualCopyTransportCandidate {
  PassResult transformation;
  std::vector<std::size_t> materialized_stores;
  // Origins certified for newly introduced selector families, not a preload
  // overwrite or permission to retarget an existing data-bearing family.
  std::map<std::string,int> selector_origins = {};
};
std::vector<ContextualCopyTransportCandidate> contextual_copy_transport_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const ContextualCopyTransportOptions& limits = {});
// Independent per-context alternatives: the destination is dead after the
// store, X already equals its exact word, or every future use before overwrite
// is a direct normalizing recall with the same certified canonical result.
// Selector/FL/indirect-alias observations require raw-word equality. Pure leaf summaries are
// proved only on the complete finite integral domain of each actual caller.
// Mixed callers may combine equality with completed guarded exclusions of
// every possible destination observation in each unresolved definition state.
std::vector<ContextualCopyTransportCandidate> contextual_redundant_copy_store_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const ContextualCopyTransportOptions& limits = {});
// Remove a fresh 1..9 literal before an unchanged direct store only when every
// exact caller already has its canonical word in X and all removed stack-lift
// effects are unobservable. Numeric interval equality alone is insufficient.
std::vector<ContextualCopyTransportCandidate> contextual_redundant_literal_store_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const ContextualCopyTransportOptions& limits = {});
// Reuse a complete finite self-negating stable-word orbit as a selector for
// an existing plain return. The source data word is not recoded here; full
// word preservation, typed roots, X2 and exact return aliasing are mandatory.
std::vector<ContextualCopyTransportCandidate> contextual_finite_return_selector_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const ContextualCopyTransportOptions& limits = {});
// Splice an existing total pure quantizing helper into an ignored return.
// All changed stack/X1/X2 values must die before every later observation;
// source data and every pending selector identity remain unchanged.
std::vector<ContextualCopyTransportCandidate> contextual_pure_return_completion_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const ContextualCopyTransportOptions& limits = {});
// Independent totality lane: a readonly scalar leaf may replace an ignored
// return only after proving every extra operation total for all original
// caller stack values. Unknown input, mutable/raw data, possible zero division
// and live stack/X1/X2 reject. No caller/game recognizer is involved.
std::vector<ContextualCopyTransportCandidate> contextual_total_return_completion_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const ContextualCopyTransportOptions& limits = {});
// Add a store at an existing producer only when its old destination word is
// dead on every continuation, then independently eliminate later copies of
// that value. Neutral alternatives are intentional: dead stack loads and
// finite control/data encodings can repay them in the same layout transaction.
std::vector<ContextualCopyTransportCandidate> contextual_anticipatory_copy_store_candidates(
    const std::vector<IrOp>& ops,const PassContext& context,
    const ContextualCopyTransportOptions& limits = {});
// Bounded, fairly enumerated transitive value-origin preferences, followed by
// sibling-copy affinities in the remaining budget. Neither common ancestry nor
// a common source name is an equality claim; complete-web allocation and downstream data proofs are
// mandatory, including when a producer helper transforms its argument.
std::vector<RegisterWebRecoloringRequest> contextual_copy_origin_register_requests(
    const std::vector<IrOp>& ops,std::size_t maximum_requests=16);
// Neutral semantic candidate: redirect one independently proved ignored
// conditional return edge through an existing total readonly leaf ending
// at the same return. Publication requires a profitable final layout; other
// callers retain their original value and exact return-frame contracts.
std::vector<ContextualCopyTransportCandidate> contextual_edge_return_completion_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const ContextualCopyTransportOptions& limits = {});
} // namespace mkpro::core::passes
