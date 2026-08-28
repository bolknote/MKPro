#pragma once

#include "mkpro/core/ir.hpp"
#include "mkpro/core/passes/helpers.hpp"

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mkpro::core {

// A direct or proved single-target indirect helper call whose common register
// recall can be moved to the root of the helper.  A before-call recall is
// stack-identical after the helper returns.  An after-return recall is accepted
// only immediately before K AND or K OR and only when the bounded relational
// CFG proof shows that every observable continuation is equivalent.
enum class HelperInvariantRecallPlacement {
  BeforeCall,
  AfterReturnBeforeCommutative,
};

struct HelperInvariantRecallCall {
  std::size_t call_item_index = 0;
  std::size_t operand_item_index = 0;
  bool indirect = false;
  std::size_t recall_item_index = 0;
  std::size_t continuation_item_index = 0;
  std::size_t proved_continuation_cells = 0;
  HelperInvariantRecallPlacement placement = HelperInvariantRecallPlacement::BeforeCall;
  int commutative_opcode = -1;
};

struct HelperInvariantRecallHoistOptions {
  // The proof deliberately remains small and deterministic.  A candidate
  // outside either bound is rejected rather than guessed about.
  std::size_t max_helper_body_cells = 16;
  std::size_t max_continuation_cells = 128;

  // A complete target set is required for every indirect-flow MachineItem.
  // Targets are physical cell addresses in the input artifact.  The rewrite
  // rejects targets into the helper or a removed recall and records their
  // reindexed final-artifact addresses in the proof.
  std::map<std::size_t, std::vector<int>> proved_indirect_flow_targets;

  // Subset of the proved map whose selector values are already materialized
  // and therefore may not be retargeted.  The rewrite may retain removed
  // call-site cells as semantic NOP padding when that preserves anchored
  // target geometry and the overall transformation still saves cells.
  std::map<std::size_t, std::vector<int>> fixed_indirect_flow_targets;

  // Resolved physical target for every numeric/formal direct-address item.
  // The caller obtains these through the authoritative address decoder.  The
  // rewrite preserves both the operand byte and target command identity.
  std::map<std::size_t, int> fixed_direct_address_targets;
  std::set<std::size_t> retargetable_direct_address_items;
};

struct HelperInvariantRecallHoistProof {
  bool proved = false;
  bool final_artifact_proved = false;
  std::string helper_label;
  int input_cells = 0;
  int output_cells = 0;
  int recall_opcode = -1;
  int register_index = -1;
  std::size_t helper_label_item_index = 0;
  std::size_t helper_body_begin_item_index = 0;
  std::size_t helper_return_item_index = 0;
  std::size_t helper_body_cells = 0;
  std::vector<HelperInvariantRecallCall> calls;
  std::set<std::size_t> erased_recall_items;
  std::set<std::size_t> nop_recall_items;
  std::map<std::size_t, std::vector<int>> final_indirect_flow_targets;
  std::vector<std::string> reasons;
};

struct HelperInvariantRecallHoistResult {
  std::vector<MachineItem> items;
  HelperInvariantRecallHoistProof proof;
  std::vector<passes::AppliedOptimization> optimizations;
  int applied = 0;
};

// Prove a closed, straight-line helper and the complete set of its direct PP
// and single-target indirect KPP call sites.  The proof does not use comments,
// source identifiers, or procedure names.  Fixed/formal operands and unproved
// indirect flow fail closed.
HelperInvariantRecallHoistProof
verify_helper_invariant_recall_hoist(const std::vector<MachineItem>& items,
                                     const std::string& helper_label,
                                     const HelperInvariantRecallHoistOptions& options = {});

// Insert one copy of the proved recall at the helper root and erase its copy
// at every call site.  A failed pre- or post-rewrite proof returns `items`
// unchanged.
HelperInvariantRecallHoistResult
rewrite_helper_invariant_recall_hoist(const std::vector<MachineItem>& items,
                                      const std::string& helper_label,
                                      const HelperInvariantRecallHoistOptions& options = {});

// Scan labels and apply at most one profitable proved rewrite.
HelperInvariantRecallHoistResult
optimize_helper_invariant_recall_hoist(const std::vector<MachineItem>& items,
                                       const HelperInvariantRecallHoistOptions& options = {});

} // namespace mkpro::core
