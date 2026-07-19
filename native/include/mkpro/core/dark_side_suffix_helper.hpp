#pragma once

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/ir.hpp"
#include "mkpro/core/passes/helpers.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core {

// A proved direct-call entry into a straight-line helper whose last physical
// command is cell 47.  The formal B2..F9 side-space alias starts at
// `entry_address`; after the boundary it reaches physical 00, which the proof
// requires to contain В/О.
struct DarkSideSuffixEntry {
  std::string label;
  int entry_address = -1;
  int formal_opcode = -1;
};

struct DarkSideSuffixCall {
  std::size_t call_item_index = 0;
  std::size_t operand_item_index = 0;
  std::string entry_label;
  int entry_address = -1;
  int formal_opcode = -1;
};

struct DarkSideShiftedDirectTarget {
  std::size_t operand_item_index = 0;
  std::size_t target_item_index = 0;
  int original_target_address = -1;
};

// An external control-flow proof for the dual-mode F9 layout.  The named
// physical entry is reached in two deliberately different ways:
//
// - ordinary execution falls through from `predecessor_item_index`, traverses
//   the suffix, and continues at physical 48 after the explicit return cell is
//   removed;
// - direct ПП callers are rebound to a B2..F9 side-space alias, so crossing F9
//   wraps to physical 00 and executes the shared В/О instead.
//
// The verifier accepts only the one immediate predecessor recorded here and
// still rejects every official address operand or proved indirect-flow target
// into the suffix. Targets after the removed return are accepted only with a
// complete, independently proved selector rebinding map. Merely enabling the
// option therefore cannot turn an unproved second entry into a natural path.
struct DarkSideOfficialFallthroughEntry {
  std::size_t predecessor_item_index = 0;
  std::string entry_label;
};

struct DarkSideSuffixHelperOptions {
  AddressSpaceModel address_space_model = AddressSpaceModel::Standard;

  // Absent means the traditional helper-only mode: official linear control
  // must be fenced off immediately before the helper.  Present enables the
  // dual-mode natural-entry proof described above.
  std::optional<DarkSideOfficialFallthroughEntry> proved_official_fallthrough;

  // Indirect control flow is unknowable from opcodes alone.  A caller may
  // provide a complete set of resolved physical targets for every indirect
  // flow MachineItem.  Missing entries make the verifier fail closed.
  std::map<std::size_t, std::vector<int>> proved_indirect_flow_targets;
  // Optional complete post-erasure targets, keyed by the original source item.
  // A target after physical 48 must be exactly one lower; callers must prove
  // the corresponding runtime selector preload separately.
  std::map<std::size_t, std::vector<int>> proved_rebound_indirect_flow_targets;
};

struct DarkSideSuffixHelperProof {
  bool proved = false;
  bool final_artifact_proved = false;
  std::string helper_label;
  int input_cells = 0;
  int body_start_address = -1;
  int body_end_address = -1;
  int body_cells = 0;
  int explicit_return_address = -1;
  std::size_t explicit_return_item_index = 0;
  bool official_fallthrough_proved = false;
  std::size_t official_fallthrough_predecessor_item_index = 0;
  int official_fallthrough_predecessor_address = -1;
  std::string official_fallthrough_entry_label;
  int official_continuation_address = -1;
  std::size_t official_continuation_item_index = 0;
  int official_continuation_opcode = -1;
  std::vector<DarkSideSuffixEntry> entries;
  std::vector<DarkSideSuffixCall> calls;
  std::vector<DarkSideShiftedDirectTarget> shifted_direct_targets;
  std::vector<std::string> reasons;
};

struct DarkSideSuffixHelperResult {
  std::vector<MachineItem> items;
  DarkSideSuffixHelperProof proof;
  std::vector<passes::AppliedOptimization> optimizations;
  int applied = 0;
};

struct DarkSideSuffixLayoutCandidate {
  std::string helper_label;
  std::size_t target_item_index = 0;
  int body_cells = 0;
  int required_start_address = -1;
};

// Finds only structural pre-layout opportunities: a label-rooted straight
// line body ending in explicit В/О and reached by at least one direct ПП.
// Names and op semantics inside the body are opaque. The result merely states
// where the body would have to start to end at F9; component placement and the
// full dark-side final-artifact proof remain separate mandatory stages.
std::vector<DarkSideSuffixLayoutCandidate>
find_dark_side_suffix_layout_candidates(const std::vector<MachineItem>& items);

// Verifies the pre-rewrite artifact.  The helper must already be co-laid out:
// its executable body occupies a suffix ending at physical cell 47 and its
// explicit В/О is physical cell 48.  The verifier accepts internal entry
// labels, but no executable branch inside the body.  With an explicit
// `proved_official_fallthrough`, exactly one ordinary predecessor may enter the
// root of the suffix; after the rewrite that path continues at physical 48,
// while aliased ПП entries still return through physical 00.
DarkSideSuffixHelperProof
verify_dark_side_suffix_helper(const std::vector<MachineItem>& items,
                               const std::string& helper_label,
                               const DarkSideSuffixHelperOptions& options = {});

// Rewrites every proved direct ПП entry to its B2..F9 formal alias, deletes the
// explicit В/О, and independently rechecks the final MachineItem artifact.
// Any failed obligation returns the original items unchanged.
DarkSideSuffixHelperResult
rewrite_dark_side_suffix_helper(const std::vector<MachineItem>& items,
                                const std::string& helper_label,
                                const DarkSideSuffixHelperOptions& options = {});

// Scans labels and applies at most one transformation.  There is only one F9
// boundary in an MK-61 layout, so two independent helpers cannot both satisfy
// the physical proof at once.
DarkSideSuffixHelperResult
optimize_dark_side_suffix_helper(const std::vector<MachineItem>& items,
                                 const DarkSideSuffixHelperOptions& options = {});

} // namespace mkpro::core
