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

  bool operator==(const PostLayoutExternalEntryState&) const = default;
};

// One reachable calculator execution state. The same command may appear more
// than once with different return stacks; keeping those states separate is
// what makes downstream liveness precise across helper calls and returns.
struct PostLayoutExecutionState {
  std::size_t item_index = 0;
  int address = -1;
  std::vector<int> return_stack;

  bool operator==(const PostLayoutExecutionState&) const = default;
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
  // Exact opt-in hardware continuation for В/О with no return frame.  The
  // ordinary proof remains fail-closed; profiles that have separately pinned
  // the MK-61 empty-return behavior may name physical 01 (or an opaque label)
  // here.
  std::optional<IrTarget> empty_return_target;
};

// One authoritative, fail-closed fact set for post-layout consumers. Indirect
// maps are total over their respective opcode families when `proved` is true.
struct AuthoritativePostLayoutControlFlow {
  bool proved = false;
  std::vector<PostLayoutExternalEntryState> external_entries;
  std::map<std::size_t, std::vector<PostLayoutCommandIdentity>> indirect_flow_targets;
  std::map<std::size_t, std::vector<int>> indirect_memory_targets;
  std::optional<PostLayoutCommandIdentity> empty_return_target;
  std::vector<PostLayoutExecutionState> execution_states;
  std::vector<std::vector<std::size_t>> execution_successors;
  int maximum_observed_return_depth = 0;
  std::size_t explored_states = 0;
  std::vector<std::string> reasons;
};

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
