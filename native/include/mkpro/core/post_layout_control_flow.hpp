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

struct PostLayoutControlFlowOptions {
  AddressSpaceModel address_space_model = AddressSpaceModel::Standard;
  int maximum_return_depth = 5;
  std::size_t maximum_execution_states = 20000;
};

// One authoritative, fail-closed fact set for post-layout consumers. Indirect
// maps are total over their respective opcode families when `proved` is true.
struct AuthoritativePostLayoutControlFlow {
  bool proved = false;
  std::vector<PostLayoutExternalEntryState> external_entries;
  std::map<std::size_t, std::vector<PostLayoutCommandIdentity>> indirect_flow_targets;
  std::map<std::size_t, std::vector<int>> indirect_memory_targets;
  int maximum_observed_return_depth = 0;
  std::size_t explored_states = 0;
  std::vector<std::string> reasons;
};

// Build exact indirect target and externally admitted entry facts solely from
// opcodes plus compiler-owned MachineItem metadata. Comments, roles, semantic
// source names, preloads, and optimizer switches are not consulted.
AuthoritativePostLayoutControlFlow
build_post_layout_control_flow(const std::vector<MachineItem>& items,
                               const PostLayoutControlFlowOptions& options = {});

} // namespace mkpro::core
