#pragma once

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/ir.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/result.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core {

enum class NaturalTargetSelectorOrigin {
  ExistingPreload,
  LateBoundDecimalCharge,
};

struct NaturalTargetRequiredFlowSelector {
  std::size_t command_item = 0;
  std::string register_name;

  bool operator==(const NaturalTargetRequiredFlowSelector&) const = default;
};

struct NaturalTargetRequiredSelectorTarget {
  std::size_t target_item = 0;
  std::string register_name;

  bool operator==(const NaturalTargetRequiredSelectorTarget&) const = default;
};

struct NaturalTargetRequiredAbsoluteTarget {
  std::size_t target_item = 0;
  int target_address = -1;

  bool operator==(const NaturalTargetRequiredAbsoluteTarget&) const = default;
};

// One stage of an atomic geometry transaction may temporarily move a typed
// indirect target away from the address decoded by its unchanged preload.
// This contract is admissible only together with an exact absolute placement
// of the same target identity at final_target_address. The final runtime
// selector proof must decode the unchanged preload to that exact identity.
struct NaturalTargetDeferredSelectorReconciliation {
  std::size_t source_item = 0;
  std::size_t target_item = 0;
  int final_target_address = -1;

  bool operator==(
      const NaturalTargetDeferredSelectorReconciliation&) const = default;
};

// Completed must-analysis only, scoped to one compiler layout transaction.
// It is not a search cache: eviction or disabling it cannot change candidates.
struct NaturalTargetFlowProofCache;
struct NaturalTargetFlowProofCacheStats {
  std::size_t hits = 0;
  std::size_t misses = 0;
  std::size_t insertions = 0;
  std::size_t effect_hits = 0;
  std::size_t effect_evaluations = 0;
  std::size_t resident_entries = 0;
  std::size_t capacity = 0;
};
std::shared_ptr<NaturalTargetFlowProofCache> make_natural_target_flow_proof_cache(
    std::size_t capacity = 16);
NaturalTargetFlowProofCacheStats natural_target_flow_proof_cache_stats(
    const std::shared_ptr<NaturalTargetFlowProofCache>& cache);

struct NaturalTargetComponentLayoutOptions {
  std::shared_ptr<NaturalTargetFlowProofCache> flow_proof_cache;
  AddressSpaceModel address_space_model = AddressSpaceModel::Standard;
  std::size_t maximum_subset_states = 20000;
  // Pure geometry cache, scoped to this invocation. Disabling it is useful
  // for differential tests; it never changes candidate/proof search limits.
  bool memoize_layout_geometry = true;
  // Pure effect proofs ignore physical placement. Keep their complete result
  // per exact trace and ordered opcode/selector rewrite, including failures.
  bool memoize_flow_effects = true;
  // Differential testing: eager and demand-driven X2 proofs have identical
  // facts, candidate budgets and acceptance. Production computes on demand.
  bool eager_symbolic_flow_analysis = false;
  // Anchor assignment and component-placement DP are different search
  // spaces. Zero preserves the historical behavior (reuse the subset-state
  // cap); compiler candidate search can bound only the already ranked anchor
  // combinations without starving the exact placement proof.
  std::size_t maximum_anchor_combinations = 0;
  int maximum_execution_states = 20000;
  std::size_t maximum_anchors = 0;
  std::size_t maximum_rejection_reasons = 64;
  // Symbolic targets that must remain representable by a fixed-width decimal
  // selector after the final component order is chosen.  A size-neutral order
  // is admissible only when explicitly enabled and every target satisfies the
  // bound under the same final-artifact proof as ordinary natural anchors.
  std::vector<std::string> required_bounded_target_labels;
  int maximum_bounded_target_address = 99;
  bool allow_size_neutral_bounded_layout = false;
  // Atomic clients can temporarily widen runtime-charge placement while
  // stable preloaded companions still require their own decimal domain.
  // Derive those independent bounds from the existing relocation proof.
  bool constrain_rebindable_selector_targets = false;
  // Run the separately bounded late-runtime-selector search after the best
  // ordinary geometry has been selected.  Callers enable this only for the
  // final publishable layout, never while evaluating speculative descendants.
  bool enable_late_bound_runtime_selector_composition = false;
  // A caller may use a proved zero-saving selector reassignment as one stage
  // of a larger atomic machine-layout transaction. Ordinary standalone
  // natural-target optimization keeps requiring a positive size saving.
  bool allow_size_neutral_flow_rebind = false;
  // Internal bounded composition stage. The enabled ordinary search defers dynamic
  // decimal selectors until it has selected its best stable-preload geometry;
  // this stage then considers only those runtime selectors while carrying the
  // existing indirect targets as preservation anchors.
  bool late_bound_runtime_selector_phase = false;
  // Atomic callers may require a particular direct-flow identity to use a
  // particular selector while the generic solver remains free to optimize all
  // unrelated targets with other selectors. Invalid, conflicting, or
  // unsatisfied bindings fail closed.
  std::vector<NaturalTargetRequiredFlowSelector> required_flow_selectors;
  // A caller that already owns a typed indirect consumer may require its
  // stable selector to address a command identity even when no direct flow to
  // that target exists. This is a zero-flow layout anchor, not a saving by
  // itself, and is accepted only as part of a separately profitable layout.
  std::vector<NaturalTargetRequiredSelectorTarget> required_selector_targets;
  bool allow_size_neutral_selector_target_layout = false;
  // A larger atomic transaction may temporarily spend a bounded number of
  // bridge/padding cells. The caller must consume that growth immediately in
  // a separately proved downstream rewrite and publish only a net reduction.
  int maximum_transactional_growth_cells = 0;
  // Expand complete stable, literal-preloaded R7..Re flow families to direct
  // commands. Their data words remain unchanged, but no longer pin targets.
  // This is a bounded transaction; partial families and hidden-state changes
  // fail closed under the ordinary final-artifact proof.
  std::vector<int> released_flow_selector_registers;
  // Opt-in only for an isolated complete-family allocation transaction.
  // Preserve the ordinary candidate order and its bounded-search incumbent.
  bool reallocate_released_address_only_selectors = false;
  // A larger atomic layout transaction may require an executable command
  // identity at one exact physical address before applying a separately
  // proved size-reducing machine rewrite. This mode never claims savings on
  // its own: it is admitted only when explicitly enabled, preserves every
  // target/preload identity, and keeps the executable cell count unchanged.
  std::vector<NaturalTargetRequiredAbsoluteTarget> required_absolute_targets;
  bool allow_size_neutral_absolute_layout = false;
  // Atomic callers that will apply a separate proved size-reducing rewrite
  // may request only the neutral geometric solution. This skips unrelated
  // natural-flow anchor combinations that the caller could not accept.
  bool require_size_neutral_absolute_layout = false;
  // Permit the ordinary two-cell `BP <target>` component stitch to run as a
  // complete post-layout transaction even when no selector rewrite is
  // profitable. The fold still has to preserve every command identity,
  // external/return entry, stack/X2 effect, indirect target, and preload under
  // the final authoritative control-flow proof.
  bool allow_standalone_fallthrough_jump_fold = false;
  // Complete-artifact search only: widen the stitch proof to neutral,
  // unreferenced labels while retaining the ordinary greedy layout lane.
  bool allow_unreferenced_jump_labels = false;
  std::vector<NaturalTargetDeferredSelectorReconciliation>
      deferred_selector_reconciliations;
};

struct NaturalTargetFlowRewrite {
  std::size_t original_command_item = 0;
  std::size_t original_target_item = 0;
  std::string selector_register;
  int original_opcode = -1;
  int target_address = -1;

  bool operator==(const NaturalTargetFlowRewrite&) const = default;
};

struct NaturalTargetPreloadRewrite {
  std::string register_name;
  std::string old_value;
  std::string new_value;
  bool fractional_projection_only = false;

  bool operator==(const NaturalTargetPreloadRewrite&) const = default;
};

struct NaturalTargetRuntimeSelectorProof {
  std::size_t original_command_item = 0;
  std::size_t final_command_item = 0;
  std::size_t original_target_item = 0;
  std::string register_name;
  std::string delivered_preload;
  int decoded_target = -1;
  int final_target_address = -1;
  bool stable_mutation_class = false;
  bool selector_unwritten = false;
  // True when the selector register is written at runtime, but every write is
  // a proved uninterruptible `П->X r; /-/; X->П r` toggle and both signed
  // spellings of the preload decode to the same flow target.
  bool selector_sign_toggle_invariant = false;
  bool typed_target_matches_runtime_decode = false;

  bool operator==(const NaturalTargetRuntimeSelectorProof&) const = default;
};

struct NaturalTargetComponentLayoutPlan {
  bool proved = false;
  bool control_flow_equivalent = false;
  bool call_return_equivalent = false;
  bool stack_and_x2_equivalent = false;
  bool indirect_memory_equivalent = false;
  bool data_projection_equivalent = false;
  bool final_artifact_proved = false;

  NaturalTargetSelectorOrigin selector_origin =
      NaturalTargetSelectorOrigin::ExistingPreload;
  std::string selector_register;
  int natural_target = -1;
  int input_cells = 0;
  int output_cells = 0;
  int removed_cells = 0;
  int moved_segments = 0;
  int rebound_indirect_flows = 0;
  int transparent_trampolines = 0;
  int transparent_split_bridges = 0;
  int reused_split_bridge_commands = 0;
  int fallthrough_jump_folds = 0;
  int x2_reconvergence_flows = 0;
  int terminal_shared_return_folds = 0;
  int bounded_targets = 0;
  bool bounded_targets_proved = false;
  bool size_neutral_bounded_layout = false;
  bool size_neutral_flow_rebind = false;
  int absolute_targets = 0;
  bool absolute_targets_proved = false;
  bool size_neutral_absolute_layout = false;
  bool size_neutral_selector_target_layout = false;
  bool transactional_selector_target_layout = false;
  int transactional_growth_cells = 0;
  int transactional_downstream_removed_cells = 0;
  int transactional_overlay_removed_cells = 0;
  int transactional_terminal_removed_cells = 0;
  int deferred_selector_reconciliations = 0;
  bool deferred_selector_reconciliations_proved = false;
  std::vector<NaturalTargetFlowRewrite> flows;
  std::vector<NaturalTargetPreloadRewrite> preloads;
  std::vector<NaturalTargetRuntimeSelectorProof> runtime_selectors;
  AuthoritativePostLayoutControlFlow final_control_flow;
  std::vector<std::string> reasons;
};

struct NaturalTargetComponentLayoutResult {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
  NaturalTargetComponentLayoutPlan plan;
  int applied = 0;
  int removed_cells = 0;
};

// Proof and preload rewrite for removing one executable cell while preserving
// every existing indirect-flow command identity. Targets after the removed
// cell move back by one; selectors are changed only when their register is
// stable, unwritten, and either address-only or carries a proved retunable
// data projection.
struct PreloadedIndirectFlowCellErasurePlan {
  bool proved = false;
  std::vector<PreloadReport> preloads;
  std::map<std::size_t, std::vector<int>> original_targets;
  std::map<std::size_t, std::vector<int>> rebound_targets;
  std::vector<NaturalTargetPreloadRewrite> preload_rewrites;
  std::vector<std::string> reasons;
};

// Recover authoritative label or numeric identities from resolver-generated
// ordinary address operands whose targets currently lie beyond the official
// window. Genuine side counters retain their hardware mode, even if their
// symbolic target is temporarily outside that window. Opaque/raw and
// super-dark multi-command operands fail closed.
std::optional<std::vector<MachineItem>> normalize_natural_target_overflow_formals(
    const std::vector<MachineItem>& items,
    AddressSpaceModel model = AddressSpaceModel::Standard);

// Retarget a compiler-proved natural fractional selector family while keeping
// every ordinary data recall in the same certified family. Untagged literals,
// generated setup values, raw-sensitive uses, and stale old targets fail
// closed.
std::optional<std::string> rebind_proved_natural_fractional_selector_preload(
    const std::vector<MachineItem>& items, const PreloadReport& preload,
    int old_target, int new_target,
    AddressSpaceModel model = AddressSpaceModel::Standard);

// Retarget one stable, unwritten indirect-flow selector. Address-only
// selectors use decimal words for 00..99 and canonical BCD words for the
// remaining official cells (A0..A4 on stock, A0..B1 on mk61s). A short-side
// alias is not extra storage. Runtime decimal charges keep their own codec.
// Selectors with ordinary data
// reads must belong to a proved fractional or masked-bit projection. Mask
// proofs require the original runtime_preloads and forget every R7..Re seed
// so independently retuned selectors cannot justify each other's dead bits.
// Indirect memory
// target aliases also count as data uses: checking only the encoded selector
// would let a pointer expose a changed address word. Unknown aliases fail
// closed, including in fractional/numeric projection alternatives.
std::optional<std::string> rebind_stable_preloaded_indirect_flow_selector(
    const std::vector<MachineItem>& items, const PreloadReport& preload,
    const AuthoritativePostLayoutControlFlow& control_flow, int old_target,
    int new_target, AddressSpaceModel model = AddressSpaceModel::Standard,
    const std::vector<PreloadReport>* runtime_preloads = nullptr);

PreloadedIndirectFlowCellErasurePlan
plan_preloaded_indirect_flow_cell_erasure(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control_flow,
    std::size_t erased_item_index, int erased_address,
    AddressSpaceModel model = AddressSpaceModel::Standard);

// Reorder generic fallthrough-closed machine segments so a direct flow target
// lands on an address already represented by a stable internal register. Every
// converted direct PP, BP, or compatible conditional keeps its flow identity
// and is replaced by the corresponding one-cell indirect opcode. Only direct
// x!=0, x>=0, x<0, and x=0 conditionals with exact indirect counterparts are
// eligible. When direct and indirect fallthrough X2 effects differ, each
// converted conditional (independently of how many flows share its target)
// must prove X2=X at entry, or that every fallthrough path overwrites the
// changed X2 value before it can be observed, or full paired direct/indirect
// dataflow equality; FL0..FL3 are not rewritten. Numeric direct targets,
// complete indirect facts, external entries, and return-stack slots are rebound
// by command identity and the final artifact is independently reconstructed.
//
// No spelling from labels, comments, roles, source variables, or procedures is
// interpreted.  The optimizer fails closed on dynamic/unproved indirect flow,
// selector mutation, executable operands, unsupported preload encodings, or a
// control/stack/X2 mismatch.
//
// A stable selector register is ordinarily required to be unwritten. The one
// proved exception is a sign-toggled register: every runtime write is the
// exact contiguous triple recall/negate/store of that same register, the
// execution-state graph proves the two trailing triple commands are reachable
// only by falling through the triple, and both signed spellings of the
// literal preload decode to the same flow target while remaining fixed points
// of the machine's selector write-back (for example, ±99999999 -> 99 and
// ±99999918 -> 18, but not ±18, whose negative phase would be rewritten to
// -99999918 on first use). Such a register keeps its exact preload value:
// rebinding and retunable-family rewrites stay rejected for it.
//
// A compiler-owned preload's raw integer part may be rebound because raw
// internal registers are not UI.  This is allowed only when every machine use
// is enumerated and is either an exact runtime address decode or an adjacent
// recall+K{X} projection whose full stack and X2 transfer is proved equal.  The
// fractional/data projection and every future command identity therefore stay
// equal even though the unobserved raw register representation may differ.
// Computed/generated setup preloads are not literals: any setup expression or
// setup target metadata makes the candidate fail closed because changing only
// PreloadReport::value would not prove the value delivered by the setup code.
// A positive fractional literal may also be re-entered in its canonical
// eight-digit scientific form when that representation exposes a useful BCD
// flow target. This is accepted only when every non-flow recall is consumed
// immediately by ordinary arithmetic; raw-sensitive uses fail closed.
// Existing two-digit raw BCD selectors are retained only when the authoritative
// machine decoder maps them back to the same command identity before and after
// layout. When two fixed natural targets would overlap, a profitable selector
// may instead land on a two-cell direct-jump trampoline. Such a trampoline is
// accepted only after its exact structure is proved and collapsed from the
// identity trace without changing the incoming flow kind or return stack.
// A fallthrough-closed helper may likewise be split before a later fixed target:
// its prefix receives either a proved direct-jump bridge or a one-cell indirect
// bridge when the suffix begins at an independently proved stable selector target.
// The suffix remains freely placeable and candidate cuts are derived only from
// target geometry and typed selector identity.
NaturalTargetComponentLayoutResult optimize_natural_target_component_layout(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control_flow,
    const NaturalTargetComponentLayoutOptions& options = {});

// A semantic allocation alternative, not an executable layout. Typed command
// destinations are preserved; each selector's encoded origin remains attached
// to the carrier, even after it serves a different target identity. The caller
// must close every placement/preload equation before exposing these items.
struct DeferredNaturalTargetSelectorAllocation {
  std::vector<MachineItem> items;
  std::map<std::string, int> encoded_selector_targets;
  int changed_flows = 0;
  int cell_delta = 0;
};

struct DeferredNaturalTargetSelectorAllocationOptions {
  // Exact two-phase input proof may transport a ContinuousResume call to
  // the same indirect call identity. Single-step and prompt anchors remain fixed.
  bool allow_typed_resume_conversion = false;
  std::size_t maximum_assignments = 128;
  std::size_t maximum_candidates = 24;
  int maximum_growth_cells = 4;
};

// Assign complete stable R7..Re families and direct-flow groups together.
// Reuses natural-target flow-effect, selector-writeback and identity-trace
// proofs, but deliberately does not choose a physical component order. No
// preload is changed here. Unknown aliases, generated setup and formal side
// entries cannot be used as allocator permissions.
std::vector<DeferredNaturalTargetSelectorAllocation>
deferred_natural_target_selector_allocations(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control_flow,
    const std::map<std::string, int>& encoded_selector_targets,
    const PostLayoutControlFlowOptions& control_options,
    const DeferredNaturalTargetSelectorAllocationOptions& options = {});

// Independent neutral lane: permute only already owned complete families.
// Keep it separate from conversions/releases whose lower immediate delta can
// otherwise consume the whole frontier before any geometry is considered.
std::vector<DeferredNaturalTargetSelectorAllocation>
permuted_deferred_natural_target_selector_allocations(
    const std::vector<MachineItem>& items, const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control_flow,
    const std::map<std::string,int>& encoded_selector_targets,
    const PostLayoutControlFlowOptions& control_options,
    const DeferredNaturalTargetSelectorAllocationOptions& options = {});

} // namespace mkpro::core

// Bounded, target-set-diverse matching; historical ordering remains separate.
namespace mkpro::core { std::vector<DeferredNaturalTargetSelectorAllocation>
balanced_deferred_natural_target_selector_allocations(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control_flow,
    const std::map<std::string, int>& encoded_selector_targets,
    const PostLayoutControlFlowOptions& control_options,
    const DeferredNaturalTargetSelectorAllocationOptions& options = {}); }
