#pragma once

#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/shared_helper_continuation.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace mkpro::core {

enum class NaturalComponentTailKind {
  BareReturn,
  EmptyStackJumpToOne,
};

struct NaturalComponentLayoutOptions {
  SharedHelperContinuationOptions continuation_options;

  // Required only for the jump-to-01 tail form.  A bare caller return is a
  // normal tail-call identity and does not need an empty-stack assumption.
  bool proved_empty_return_stack = false;
};

struct NaturalComponentLayoutProof {
  bool proved = false;
  bool final_artifact_proved = false;
  SharedHelperContinuationProof continuation;
  std::size_t natural_call_item_index = 0;
  std::size_t natural_operand_item_index = 0;
  std::size_t natural_tail_item_index = 0;
  std::vector<std::size_t> natural_tail_removed_item_indices;
  NaturalComponentTailKind tail_kind = NaturalComponentTailKind::BareReturn;
  int input_cells = 0;
  int output_cells = 0;
  int natural_call_input_address = -1;
  int relocated_helper_start_address = -1;
  int relocated_helper_body_end_address = -1;
  int relocated_continuation_start_address = -1;
  int relocated_return_address = -1;
  int divergent_formal_opcode = -1;
  std::map<std::size_t, std::vector<int>> final_indirect_flow_targets;
  std::vector<std::string> reasons;
};

struct NaturalComponentLayoutResult {
  std::vector<MachineItem> items;
  NaturalComponentLayoutProof proof;
  std::vector<passes::AppliedOptimization> optimizations;
  int applied = 0;
  int removed_cells = 0;
};

// Verify the hardware-independent graph part plus the two hardware layout
// obligations: the relocated helper body must end at physical 47/F9 and a bare
// return must occupy physical 00.  `natural_call_item_index` must name one of
// the two ordinary calls from the shared-continuation certificate and be a
// proved tail call (Q followed by a bare return, or by an exhaustive
// empty-stack flow to physical 01).
NaturalComponentLayoutProof verify_natural_component_layout(
    const std::vector<MachineItem>& items, const std::string& helper_label,
    std::size_t natural_call_item_index, const NaturalComponentLayoutOptions& options = {});

// Atomically performs both logical continuation sharing and physical layout:
// the natural call and its tail disappear, the helper body is moved into that
// call hole, one Q is placed after F9, and the divergent call is encoded with a
// proved side-space alias.  Failure returns the original artifact unchanged.
NaturalComponentLayoutResult rewrite_natural_component_layout(
    const std::vector<MachineItem>& items, const std::string& helper_label,
    std::size_t natural_call_item_index, const NaturalComponentLayoutOptions& options = {});

// Scan all proved helper candidates and both possible natural calls; apply at
// most one transaction because there is only one physical F9 boundary.
NaturalComponentLayoutResult
optimize_natural_component_layout(const std::vector<MachineItem>& items,
                                  const NaturalComponentLayoutOptions& options = {});

} // namespace mkpro::core
