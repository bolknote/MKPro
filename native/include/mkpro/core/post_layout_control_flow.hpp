#pragma once

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/ir.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
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
  // A hardware continuation may bypass an existing unconditional jump.
  // Admit only a single-target BP with no external/manual observation; calls,
  // returns, conditions and state-changing instructions are never transparent.
  bool allow_bypassed_direct_jumps = false;
};

struct PostLayoutExecutionRelocationProof {
  bool proved = false;
  std::size_t state_pairs = 0;
  std::vector<std::string> reasons;
};

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
// opcodes plus compiler-owned MachineItem metadata. Comments, roles, semantic
// source names, preloads, and optimizer switches are not consulted.
AuthoritativePostLayoutControlFlow
build_post_layout_control_flow(const std::vector<MachineItem>& items,
                               const PostLayoutControlFlowOptions& options = {});

PostLayoutBorrowedSelectorProof
prove_post_layout_borrowed_entry_selectors(const std::vector<MachineItem>& items,
                                           const PostLayoutControlFlowOptions& options = {});

} // namespace mkpro::core
