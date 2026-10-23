#pragma once

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/ir.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core {

// Exact command identity in the current post-layout artifact. Labels are
// opaque aliases and are never interpreted by spelling. Item identity lets a
// later block mover rebind numeric-only facts before changing physical layout.
struct PostLayoutCommandIdentity {
  std::size_t item_index = 0;
  int address = -1;
  std::vector<std::string> labels;

  bool operator==(const PostLayoutCommandIdentity&) const = default;
};

enum class ExternalEntryKind {
  Main,
  ResumableStop,
  ManualSingleStep,
  ManualContinuous,
};

// `return_stack.back()` is the next В/О destination. Every slot carries both
// current numeric address and stable item identity for later relocation.
struct PostLayoutExternalEntryState {
  PostLayoutCommandIdentity entry;
  std::vector<PostLayoutCommandIdentity> return_stack;
  ExternalEntryKind kind = ExternalEntryKind::Main;
  std::optional<ManualInteractionAnchor> manual_interaction;
  std::optional<int> formal_opcode;
  std::vector<std::optional<int>> formal_return_stack;

  bool operator==(const PostLayoutExternalEntryState&) const = default;
};

// One reachable calculator execution state. The same command may appear more
// than once with different return stacks; keeping those states separate is
// what makes downstream liveness precise across helper calls and returns.
struct PostLayoutExecutionState {
  std::size_t item_index = 0;
  int address = -1;
  std::vector<int> return_stack;
  // Exact hardware counter and return frames. nullopt denotes the separate
  // logical layout space, not a physical alias past the selected model's limit.
  std::optional<int> formal_opcode;
  std::vector<std::optional<int>> formal_return_stack;
  // Address operands can be fetched across a side-branch discontinuity.
  std::optional<std::size_t> operand_item_index;

  bool operator==(const PostLayoutExecutionState&) const = default;
};

enum class PostLayoutExecutionEdgeKind {
  Fallthrough,
  DirectTarget,
  IndirectTarget,
  Return,
  Resume,
};

struct PostLayoutExecutionEdge {
  std::size_t target_state = 0;
  PostLayoutExecutionEdgeKind kind = PostLayoutExecutionEdgeKind::Fallthrough;
  // The runtime entry counter for an indirect alternative. nullopt is the
  // separate unplaced logical target space, not an unspecified dark alias.
  std::optional<int> indirect_formal_opcode;
  bool operator==(const PostLayoutExecutionEdge&) const = default;
};

struct PostLayoutControlFlowOptions {
  AddressSpaceModel address_space_model = AddressSpaceModel::Standard;
  int maximum_return_depth = 5;
  std::size_t maximum_execution_states = 20000;
  // Exact compiler-owned entry command.  The default preserves the ordinary
  // physical-00 program entry.  A layout that deliberately places a return at
  // 00 (for example, to use cyclic address-space continuation) must name its
  // real entry explicitly; labels are opaque identities, never interpreted by
  // spelling.
  IrTarget main_entry = 0;
  // An exact formal entry must decode to the main_entry command identity.
  std::optional<int> main_formal_opcode;
  // Complete encoded entry counters for indirect consumers whose proved
  // runtime selectors use noncanonical aliases. Physical target facts remain
  // mandatory and must have exactly the same physical projection. Attached
  // instruction metadata supplies these facts by default; an explicit map
  // must agree with it. With neither representation, ordinary physical or
  // unplaced logical targets retain their canonical-entry contract.
  std::map<std::size_t, std::vector<int>> proved_indirect_formal_targets;
  // Exact opt-in hardware continuation for В/О with no return frame.  The
  // ordinary proof remains fail-closed; profiles that have separately pinned
  // the MK-61 empty-return behavior may name physical 01 (or an opaque label)
  // here. The target must resolve if an empty-stack return is reachable. An
  // unused policy does not create an external entry or invalidate a layout
  // whose returns all have proved caller frames.
  std::optional<IrTarget> empty_return_target;
  // Exact byte-image view used by overlay transport proofs: these Op cells
  // also serve as address words. The caller must first encode the real final
  // operand byte, not its numeric target. Default typed-IR validation is unchanged.
  std::vector<std::size_t> opcode_address_words;
};

// A private executable view of compiler-owned address/code overlays. Item
// indices and labels stay unchanged; an executable operand is encoded from
// its actual final target, never from its numeric target interpreted as code.
struct PostLayoutByteImage {
  std::vector<MachineItem> items;
  PostLayoutControlFlowOptions options;
};

bool has_executable_address_words(const std::vector<MachineItem>& items);
std::optional<PostLayoutByteImage> materialize_post_layout_byte_image(
    const std::vector<MachineItem>& items,
    const PostLayoutControlFlowOptions& options = {});

// One authoritative, fail-closed fact set for post-layout consumers. Indirect
// maps are total over their respective opcode families when `proved` is true.
struct AuthoritativePostLayoutControlFlow {
  bool proved = false;
  AddressSpaceModel address_space_model = AddressSpaceModel::Standard;
  std::vector<PostLayoutExternalEntryState> external_entries;
  std::map<std::size_t, std::vector<PostLayoutCommandIdentity>> indirect_flow_targets;
  std::map<std::size_t, std::vector<int>> indirect_memory_targets;
  std::optional<PostLayoutCommandIdentity> empty_return_target;
  std::vector<PostLayoutExecutionState> execution_states;
  std::vector<std::vector<std::size_t>> execution_successors;
  // Labelled alternatives retain branch direction and encoded selector
  // provenance even when the legacy successor projection deduplicates them.
  std::vector<std::vector<PostLayoutExecutionEdge>> execution_edges;
  int maximum_observed_return_depth = 0;
  std::size_t explored_states = 0;
  std::vector<std::string> reasons;
};


// External entry is an exact PC/return-frame context, not every execution of
// its physical command. In particular, a called return at 00 is not Main.
// Product graphs can repeat a context with different data rows; retain every
// matching row conservatively instead of guessing its selector valuation.
struct PostLayoutExternalEntryContexts {
  std::vector<bool> external;
  std::vector<bool> main;
};

bool post_layout_execution_matches_external_entry(
    const PostLayoutExecutionState& execution,
    const PostLayoutExternalEntryState& entry);
std::optional<PostLayoutExternalEntryContexts>
resolve_post_layout_external_entry_contexts(const AuthoritativePostLayoutControlFlow& control);

// Control certificate only. The caller separately proves total pure execution
// and non-observation of all changed stack/X1/X2 values on every continuation.
struct PostLayoutReturnCompletion {
  std::size_t old_return_item = 0;
  // New consecutive pure command items, including their final plain V/O.
  std::vector<std::size_t> body_items;
  // Empty preserves the historical physical-fallthrough completion contract.
  // Otherwise only these direct conditional target edges enter the existing
  // pure body; other callers still reach the unchanged retained V/O.
  std::vector<std::size_t> redirected_direct_branch_items = {};
};

struct PostLayoutExecutionRelocationOptions {
  std::size_t maximum_state_pairs = 50000;
  // For ambiguous aliases of one physical target, a caller may supply the
  // selector-value transport that its separate data/preload proof established.
  // Keys are original instruction indices and encoded before/after counters.
  std::map<std::size_t, std::map<int, int>> indirect_entry_remap;
  // Separately proved stable-selector replacements of direct branches/calls.
  // Only matching opcode families through R7..Re are admitted. The source
  // operand must disappear, and every labelled continuation and return frame
  // must still follow the command-identity mapping. Selector values and X2
  // observability remain obligations of the caller's data proof.
  std::vector<std::size_t> direct_to_indirect_flow_items;
  // Complementary direct F conditions may exchange their target/fallthrough
  // edges. The proof also requires X2 convergence on BOTH continuations:
  // taken and skipped direct conditions do not have identical hidden effects.
  // Indirect conditions, FL counters, raw/manual commands and arbitrary
  // opcode replacements are not authorized by this certificate.
  std::vector<std::size_t> inverted_direct_conditional_items;
  // A hardware continuation may bypass an existing unconditional jump.
  // Admit only a single-target BP with no external/manual observation; calls,
  // returns, conditions and state-changing instructions are never transparent.
  bool allow_bypassed_direct_jumps = false;
  // Control transport for one separately data-certified recall/Enter erasure.
  // Map the removed producer to its retained fallthrough successor, including
  // return frames that used to resume at the producer. This is NOT a data
  // certificate: the caller must prove stack/X1/X2 nonobservation separately.
  // External/manual entries, fetched operands and non-fallthrough contexts
  // remain ineligible. Composing deletions requires one proof per deletion.
  std::optional<std::size_t> erased_stack_producer_item;
  // Separate control-only certificate for a data-proved fresh digit 1..9
  // immediately followed by the unchanged direct store. This does not widen
  // recall/Enter erasure or prove canonical word/stack/X1/X2 equivalence.
  std::optional<std::size_t> erased_redundant_literal_item;
  // Separately data-certified direct transfers may be stutters in the rooted
  // control relation. Each maps to its retained fallthrough command. This is
  // NOT a memory/stack certificate; the caller owns those and entry-mode proof.
  // Fetched operands, external entries and noncanonical side paths reject.
  std::vector<std::size_t> erased_data_effect_items;
  // New direct BP bridges may stutter only with unchanged return frames,
  // closed numeric entry on every invocation and independently proved X2
  // non-observation. They must have no preserved command identity or external
  // entry, and cannot form a chain. This composes with the old jump bypass.
  std::vector<std::size_t> inserted_transparent_jump_items;
  // Data certificates remain the caller's obligation. These direct recalls
  // retain their operation/entry shape but read a separately proved equal word.
  std::vector<std::size_t> retargeted_direct_recall_items;
  // New, separately data/stack-certified Recall;Store prefixes. Return frames
  // may name only the first command; the proof canonicalizes it to the retained
  // continuation, never an arbitrary return address or a prefix interior.
  std::vector<std::pair<std::size_t, std::size_t>> inserted_copy_prefixes;
  // Explicit aliases of a plain V/O, indexed in the old and new artifacts.
  // Both return commands, every formal path and all frames are rechecked.
  // No frame address or external interaction is canonicalized by this list.
  std::vector<std::pair<std::size_t, std::size_t>> equivalent_return_items;
  std::vector<PostLayoutReturnCompletion> pure_return_completions;

};

struct PostLayoutExecutionRelocationProof {
  bool proved = false;
  std::size_t state_pairs = 0;
  std::vector<std::string> reasons;
};

// ROM-certified direct-condition inversion preserves visible stack/X1 but can
// change X2. Prove the latter unobservable through every admitted continuation,
// without relying on physical selector values in a possibly deferred layout.
bool prove_post_layout_direct_conditional_inversion_effects(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& control, std::size_t command_item);

// Compare rooted, labelled execution contexts under a command relocation.
// This is a control-flow transport proof, not a replacement for the caller's
// data/preload/X2 proof or a proof that raw return-address digits are unobserved.
PostLayoutExecutionRelocationProof prove_post_layout_execution_relocation(
    const std::vector<MachineItem>& before,
    const std::vector<MachineItem>& after,
    const AuthoritativePostLayoutControlFlow& before_control,
    const AuthoritativePostLayoutControlFlow& after_control,
    const std::vector<std::optional<std::size_t>>& old_to_new_item,
    const PostLayoutExecutionRelocationOptions& options = {});

// A setup-time selector may temporarily occupy an allocated stable register
// only while its ordinary entry value is dead. Cycles are part of the proof,
// so a borrow that works only on the first iteration is rejected.
struct PostLayoutBorrowedSelectorProof {
  bool proved = false;
  std::size_t selector_registers = 0;
  std::size_t selector_states = 0;
  std::size_t entry_value_states = 0;
  std::vector<std::string> reasons;
};

// Build exact indirect target and externally admitted entry facts solely from
// opcodes plus compiler-owned MachineItem metadata. The "exec" operand role
// requests exact final-byte decoding, not permission to invent an instruction.
// Comments, semantic names, preloads, and optimizer switches are not consulted.
AuthoritativePostLayoutControlFlow
build_post_layout_control_flow(const std::vector<MachineItem>& items,
                               const PostLayoutControlFlowOptions& options = {});

// Immutable owning view of the same exact facts and bounded full-key cache.
// Read-only consumers share storage; the value API above still returns an
// independent mutable copy. Snapshots remain valid after cache eviction.
std::shared_ptr<const AuthoritativePostLayoutControlFlow>
shared_post_layout_control_flow(const std::vector<MachineItem>& items,
                               const PostLayoutControlFlowOptions& options = {});

// A separate product graph for relational data proofs. Several product nodes
// may refer to the same exact PC/return context with different stable-selector
// choices. Do not substitute this view for the command-identity relocation CFG.
struct SelectorConsistentExecutionGraph {
  bool proved = false;
  std::vector<std::size_t> original_states;
  std::vector<std::vector<std::size_t>> successors;
  // Preserve branch/resume kinds after product-state expansion. Data proofs
  // must not reconstruct these labels from physical adjacency.
  std::vector<std::vector<PostLayoutExecutionEdge>> execution_edges;
  std::size_t pruned_edges = 0;
};

SelectorConsistentExecutionGraph build_selector_consistent_execution_graph(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& control,
    std::size_t maximum_states = 20000);

// A conditional data-proof view, not an executable CFG certificate. Origins
// name the addresses currently encoded in selector words, before pending
// command relocation. Every symbolic indirect target must have one origin;
// aliases must agree and distinct commands must not collide. Formal/dark
// counters are deliberately excluded. Callers must retain these equations,
// rebind the final words and independently validate the published artifact.
struct DeferredSelectorExecutionGraph {
  SelectorConsistentExecutionGraph execution;
  std::map<std::string, int> encoded_target_origins;
};

DeferredSelectorExecutionGraph build_deferred_selector_consistent_execution_graph(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& control,
    const std::map<std::string, int>& encoded_target_origins,
    std::size_t maximum_states = 20000,
    // Caller separately preserves every direct formal execution frame.
    // Ordinary clients retain the opaque-formal rejection.
    bool preserve_direct_formal_contexts = false);

PostLayoutBorrowedSelectorProof
prove_post_layout_borrowed_entry_selectors(const std::vector<MachineItem>& items,
                                           const PostLayoutControlFlowOptions& options = {});

} // namespace mkpro::core
