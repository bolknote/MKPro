#pragma once

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/ir.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/result.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core {

enum class NaturalTargetSelectorOrigin {
  ExistingPreload,
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

struct NaturalTargetComponentLayoutOptions {
  AddressSpaceModel address_space_model = AddressSpaceModel::Standard;
  std::size_t maximum_subset_states = 20000;
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
  // A caller may use a proved zero-saving selector reassignment as one stage
  // of a larger atomic machine-layout transaction. Ordinary standalone
  // natural-target optimization keeps requiring a positive size saving.
  bool allow_size_neutral_flow_rebind = false;
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
  int x2_reconvergence_flows = 0;
  int terminal_shared_return_folds = 0;
  int bounded_targets = 0;
  bool bounded_targets_proved = false;
  bool size_neutral_bounded_layout = false;
  bool size_neutral_flow_rebind = false;
  int absolute_targets = 0;
  bool absolute_targets_proved = false;
  bool size_neutral_absolute_layout = false;
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
// wrapped formal operands whose targets currently lie beyond the official
// window. Opaque/raw and super-dark multi-command operands fail closed.
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
// eligible. Their entry must also prove X2=X on every path when direct and
// indirect fallthrough effects differ; FL0..FL3 are not rewritten. Numeric direct targets,
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

} // namespace mkpro::core
