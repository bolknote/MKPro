#pragma once

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/ir.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/result.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace mkpro::core {

enum class NaturalTargetSelectorOrigin {
  ExistingPreload,
};

struct NaturalTargetComponentLayoutOptions {
  AddressSpaceModel address_space_model = AddressSpaceModel::Standard;
  std::size_t maximum_subset_states = 20000;
  int maximum_execution_states = 20000;
};

struct NaturalTargetCallRewrite {
  std::size_t original_call_item = 0;
  std::size_t original_target_item = 0;
  std::string selector_register;
  int target_address = -1;

  bool operator==(const NaturalTargetCallRewrite&) const = default;
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
  std::vector<NaturalTargetCallRewrite> calls;
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

// Reorder generic fallthrough-closed machine segments so a called command
// identity lands on an address already represented by a stable internal
// register.  Every converted direct PP keeps its continuation identity and is
// replaced by the one-cell indirect K PP opcode.  Numeric direct targets,
// complete indirect facts, external entries, and return-stack slots are rebound
// by command identity and the final artifact is independently reconstructed.
//
// No spelling from labels, comments, roles, source variables, or procedures is
// interpreted.  The optimizer fails closed on dynamic/unproved indirect flow,
// selector mutation, executable operands, unsupported preload encodings, or a
// control/stack/X2 mismatch.
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
NaturalTargetComponentLayoutResult optimize_natural_target_component_layout(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control_flow,
    const NaturalTargetComponentLayoutOptions& options = {});

} // namespace mkpro::core
