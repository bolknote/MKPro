#pragma once

#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/passes/register_coalesce.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core::passes {

// Each component is a maximal physical fallthrough chain. Multiple labelled
// entries within a component are retained. No new jump, call or return is added.
struct SymbolicRegionLayout {
  std::vector<IrOp> ops;
  std::size_t work = 0;
  bool changed = false;
  int projected_cells = 0;
  std::map<std::string, int> projected_targets;
};

// Sizing obligations of a separately certified, pending rewrite. These widths
// do not change the returned neutral IR and are not an executable proof.
// The caller must apply its rewrite atomically, check every projected label
// and total width, then discharge final selectors and the real hardware CFG.
struct SymbolicRegionProjection {
  std::vector<int> cell_widths; // One per IR operation; labels have width zero.
  std::map<std::string, std::pair<int, int>> target_bounds;
  // Conditional command transport, not a preload/value proof. The owner
  // must rebind every moved selector and independently certify its data uses.
  bool defer_indirect_entries = false;
  // Fallback only, after the unchanged gapless search fails. Padding is
  // unreachable between closed fallthrough chains and is priced in full.
  int maximum_padding_cells = 0;
  // Freeze whole fallthrough components reached under direct formal counters.
  // No projected width may change there; rooted hardware CFG/return transport
  // must independently validate every other component permutation.
  bool preserve_formal_execution_frames = false;
};

struct DeferredStackProducerLimits {
  std::size_t maximum_candidates = 6;
  std::size_t maximum_erased_cells = 2;
  std::size_t maximum_proof_attempts = 32;
  bool allow_fresh_literal_replacement = false;
};

// Keep alternative deletion subsets, not only a greedy shortest prefix:
// removing another cell can destroy a useful data/address equality. Each
// deletion has independent relational data and rooted control/return proofs.
// Selector origins describe encoded words, not provisional positions. These
// are conditional layout candidates; the caller must rebind and prove the
// final artifact before publishing one, and retain the unchanged alternative.
std::vector<PassResult> deferred_stack_producer_erasure_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const PostLayoutControlFlowOptions& control_options,
    const std::map<std::string, int>& encoded_target_origins,
    const DeferredStackProducerLimits& limits = {});

// The caller owns the data proof for any changed runtime selectors. This
// routine proves only identity-preserving permutation and exact CFG/return
// transport. Every unchanged selector must also appear among required targets.
std::optional<SymbolicRegionLayout> layout_symbolic_regions(
    const std::vector<IrOp>& ops, const std::map<std::string, int>& required_targets,
    const PassContext& context, std::size_t maximum_work = 4096,
    const PostLayoutControlFlowOptions* control_options = nullptr,
    bool preserve_compiler_selector_charges = false,
    const SymbolicRegionProjection* projection = nullptr);

// Exchange a direct conditional's fallthrough suffix with a separately
// placed, fallthrough-closed target region. These neutral alternatives change
// component boundaries, not instruction count. The exact edge/return proof,
// X2 and numeric-entry convergence are mandatory before joint placement.
std::vector<PassResult> conditional_region_exchange_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const PostLayoutControlFlowOptions& control_options,
    std::size_t maximum_candidates = 4);

// Deferred enabling alternatives, never standalone size optimizations. The
// added one-cell bridge reuses an unwritten, exactly preserved R7..Re word.
// encoded_targets records the word's original target identities, not current
// speculative PCs. The caller must discharge these bindings in final layout.
std::vector<PassResult> stable_target_split_bridge_candidates(
    const std::vector<IrOp>& ops, const std::vector<PreloadReport>& preloads,
    const std::map<std::string, int>& encoded_targets,
    const PassContext& context, const PostLayoutControlFlowOptions& control_options,
    std::size_t maximum_candidates = 2);

// Operand equalities are only allocator requests. The existing web allocator
// proves interference, aliases and entry values; folding rechecks the result.
std::vector<RegisterWebRecoloringRequest> semantic_region_register_requests(
    const std::vector<IrOp>& ops, bool preserve_compiler_selector_charges = false);

// Additional bounded operand equalities for a complete smaller chain and an
// interior suffix of a larger chain. Requests are not equivalence certificates.
std::vector<RegisterWebRecoloringRequest> semantic_region_tail_register_requests(const std::vector<IrOp>& ops, bool preserve_compiler_selector_charges);

// Conditional region equivalence for a joint layout transaction. Every label
// remains an alias of the same command, but physical indirect destinations
// are obligations of the caller, not already discharged by this result.
PassResult semantic_region_folding_with_deferred_layout(
    const std::vector<IrOp>& ops, const PassContext& context,
    const PostLayoutControlFlowOptions& control_options);

PassResult semantic_region_folding(const std::vector<IrOp>& ops,
                                   const PassContext& context);
IrPass semantic_region_folding_pass();

} // namespace mkpro::core::passes

namespace mkpro::core::passes {
// Deferred geometry: immutable selector words retain their recorded origins
// until final layout. Every final destination still requires rebinding proof.
std::vector<PassResult> deferred_stable_target_split_bridge_candidates(
    const std::vector<IrOp>& ops, const std::vector<PreloadReport>& preloads,
    const std::map<std::string, int>& encoded_targets,
    const PassContext& context, const PostLayoutControlFlowOptions& control_options,
    std::size_t maximum_candidates = 2);

// Move a closed, exclusively called literal prefix across its call while
// preserving the shared entry store and exact numeric/return-state contract.
PassResult distribute_constant_call_prefix(const std::vector<IrOp>& ops,
    const PassContext& context, const PostLayoutControlFlowOptions& control_options);
} // namespace mkpro::core::passes
