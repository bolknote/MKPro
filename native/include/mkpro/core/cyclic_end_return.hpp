#pragma once

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/ir.hpp"
#include "mkpro/core/passes/helpers.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace mkpro::core {

// A direct-call entry into a straight-line helper that can be relocated to the
// end of the standard 105-cell MK-61 address space.  Once the helper's final
// body command occupies A4, normal program-counter wrap reaches В/О at 00 and
// makes the helper's explicit В/О redundant.
struct CyclicEndReturnEntry {
  std::string label;
  int original_address = -1;
  int relocated_address = -1;
};

struct CyclicEndReturnCall {
  std::size_t call_item_index = 0;
  std::size_t operand_item_index = 0;
  std::string entry_label;
};

struct CyclicEndReturnOptions {
  AddressSpaceModel address_space_model = AddressSpaceModel::Standard;
};

struct CyclicEndReturnProof {
  bool proved = false;
  bool final_artifact_proved = false;
  std::string helper_label;
  int input_cells = 0;
  int output_cells = 0;
  int original_body_start_address = -1;
  int original_body_end_address = -1;
  int body_cells = 0;
  int original_explicit_return_address = -1;
  std::size_t helper_block_begin_item_index = 0;
  std::size_t helper_block_end_item_index = 0;
  std::size_t original_explicit_return_item_index = 0;
  int relocated_body_start_address = -1;
  int relocated_body_end_address = -1;
  int relocated_explicit_return_address = -1;
  std::vector<CyclicEndReturnEntry> entries;
  std::vector<CyclicEndReturnCall> calls;
  std::vector<std::string> reasons;
};

struct CyclicEndReturnResult {
  std::vector<MachineItem> items;
  CyclicEndReturnProof proof;
  std::vector<passes::AppliedOptimization> optimizations;
  int applied = 0;
};

// Verify that `helper_label` names an isolated straight-line helper with an
// explicit В/О, that every entry is referenced only by direct ПП, and that
// relocating the helper to the end of this one-cell-over-limit artifact is
// safe.  The verifier deliberately rejects indirect flow and fixed numeric or
// formal address operands: relocation cannot silently retarget either.
CyclicEndReturnProof
verify_cyclic_end_return(const std::vector<MachineItem>& items,
                         const std::string& helper_label,
                         const CyclicEndReturnOptions& options = {});

// Relocate the proved helper, delete its explicit В/О at logical cell 105, and
// independently recheck the final 00..A4 artifact.  A failed obligation returns
// the original items unchanged.
CyclicEndReturnResult
rewrite_cyclic_end_return(const std::vector<MachineItem>& items,
                          const std::string& helper_label,
                          const CyclicEndReturnOptions& options = {});

// Scan labels and apply at most one proved cyclic-end return rewrite.
CyclicEndReturnResult
optimize_cyclic_end_return(const std::vector<MachineItem>& items,
                           const CyclicEndReturnOptions& options = {});

} // namespace mkpro::core
