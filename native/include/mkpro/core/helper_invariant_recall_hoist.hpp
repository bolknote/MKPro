#pragma once

#include "mkpro/core/ir.hpp"
#include "mkpro/core/passes/helpers.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mkpro::core {

// A direct or proved single-target indirect helper call whose common register
// recall can move to the helper root or tail. Contiguous direct recalls may
// prepare arguments between the common recall and the call; bounded stack
// permutations may precede a K AND/K OR join. Every plan requires a relational
// stack/register/X1/X2 proof through the complete observable continuation.
enum class HelperInvariantRecallPlacement {
  BeforeCall,
  BeforeCallBeforeCommutative,
  AfterReturnBeforeCommutative,
};

enum class HelperInvariantRecallInsertion {
  HelperRoot,
  BeforeReturn,
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
  // These commands stay at the call site, in their original order. The proof
  // replays them on both sides of the moved recall rather than assuming the
  // helper takes no arguments or that a commutative join kills the whole stack.
  std::vector<std::size_t> argument_preparation_items;
  std::vector<std::size_t> join_permutation_items;
};

struct HelperInvariantRecallHoistOptions {
  // The proof deliberately remains small and deterministic.  A candidate
  // outside either bound is rejected rather than guessed about.
  std::size_t max_helper_body_cells = 16;
  std::size_t max_continuation_cells = 128;

  // Internal search axis: allow a recall found before a call to move to the
  // helper tail when the returned value is consumed by a commutative join.
  // The public verifier also evaluates the root-placement fallback and keeps
  // whichever proved plan removes more cells.
  bool allow_before_call_commutative_tail = true;

  // Keep a proved tail plan even when the independently evaluated root plan
  // is locally smaller. This is an internal composition axis: callers may
  // apply another proved rewrite to the tail plan and compare the complete
  // result against the original artifact. It never weakens either proof.
  bool prefer_before_return_plan = false;

  // Optional proof input for an atomic composition: every listed call is
  // independently proved to enter with this direct-register value already in
  // X before the common recall is moved. The relational helper proof may then
  // erase the matching first helper recall while evaluating the original and
  // final artifacts directly. A partial call set or any other insertion mode
  // is rejected.
  std::optional<int> simultaneous_entry_recall_opcode;
  std::set<std::size_t> entry_x_proved_call_items;

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
  HelperInvariantRecallInsertion insertion = HelperInvariantRecallInsertion::HelperRoot;
  std::size_t helper_label_item_index = 0;
  std::size_t helper_body_begin_item_index = 0;
  std::size_t helper_return_item_index = 0;
  std::size_t helper_body_cells = 0;
  std::optional<std::size_t> erased_helper_entry_recall_item;
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

// Insert one copy of the proved recall at the helper root/tail and erase its copy
// at every call site.  A failed pre- or post-rewrite proof returns `items`
// unchanged.
HelperInvariantRecallHoistResult
rewrite_helper_invariant_recall_hoist(const std::vector<MachineItem>& items,
                                      const std::string& helper_label,
                                      const HelperInvariantRecallHoistOptions& options = {});

// Compare all eligible helpers and apply the smallest profitable proved rewrite.
// Equal-size plans retain deterministic label/candidate order.
HelperInvariantRecallHoistResult
optimize_helper_invariant_recall_hoist(const std::vector<MachineItem>& items,
                                       const HelperInvariantRecallHoistOptions& options = {});

} // namespace mkpro::core
