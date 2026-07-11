#pragma once

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/ir.hpp"
#include "mkpro/core/passes/helpers.hpp"

#include <cstddef>
#include <map>
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
};

struct DarkSideSuffixHelperOptions {
  AddressSpaceModel address_space_model = AddressSpaceModel::Standard;

  // Indirect control flow is unknowable from opcodes alone.  A caller may
  // provide a complete set of resolved physical targets for every indirect
  // flow MachineItem.  Missing entries make the verifier fail closed.
  std::map<std::size_t, std::vector<int>> proved_indirect_flow_targets;
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
  std::vector<DarkSideSuffixEntry> entries;
  std::vector<DarkSideSuffixCall> calls;
  std::vector<std::string> reasons;
};

struct DarkSideSuffixHelperResult {
  std::vector<MachineItem> items;
  DarkSideSuffixHelperProof proof;
  std::vector<passes::AppliedOptimization> optimizations;
  int applied = 0;
};

// Verifies the pre-rewrite artifact.  The helper must already be co-laid out:
// its executable body occupies a suffix ending at physical cell 47 and its
// explicit В/О is physical cell 48.  The verifier accepts internal entry
// labels, but no executable branch inside the body.
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
