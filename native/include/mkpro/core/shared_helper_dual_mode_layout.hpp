#pragma once

#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/shared_helper_continuation.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core {

struct SharedHelperDualModePreparation {
  std::vector<MachineItem> items;
  SharedHelperContinuationProof continuation;
  std::string marker;
  std::size_t helper_target_item_index = 0;
  int required_final_start_address = -1;
  int trailing_store_opcode = -1;
  std::vector<std::size_t> trailing_store_item_indices;
  int applied = 0;
  std::vector<std::string> reasons;
};

struct SharedHelperDualModeLayoutProof {
  bool continuation_proved = false;
  bool exact_layout_proved = false;
  bool side_entry_proved = false;
  bool selector_rebind_proved = false;
  bool final_control_flow_proved = false;
  bool proved = false;
  int input_cells = 0;
  int output_cells = 0;
  int removed_cells = 0;
  int body_start_address = -1;
  int body_end_address = -1;
  int shared_join_address = -1;
  int shared_store_address = -1;
  int shared_trailing_store_address = -1;
  int official_return_address = -1;
  int rebound_preloads = 0;
  AuthoritativePostLayoutControlFlow final_control_flow;
  std::vector<std::string> reasons;
};

struct SharedHelperDualModeLayoutResult {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
  SharedHelperDualModeLayoutProof proof;
  std::vector<passes::AppliedOptimization> optimizations;
  int applied = 0;
  int removed_cells = 0;
};

struct SharedHelperDualModeSelectorExchangeResult {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
  AuthoritativePostLayoutControlFlow control_flow;
  std::vector<passes::AppliedOptimization> optimizations;
  int applied = 0;
  std::vector<std::string> reasons;
};

// Independently proved selector-allocation alternatives. A fixed
// data constant can serve a different complete flow family while a retunable
// selector follows the displaced family. Calls and conditional/unconditional
// jumps are treated uniformly; R0-R6 and runtime-written selectors are excluded.
// These are transaction seeds, not standalone size wins. The caller must rank
// the complete downstream artifact against its original incumbent.
// Non-growing alternatives are retained independently. An explicit positive
// maximum_transactional_growth_cells also admits bounded temporary growth;
// the caller must repay it before publication. Zero is the standalone default,
// and a negative remaining allowance rejects the transaction.
// A stable selector whose complete family consists of proved empty-stack loop
// jumps also supplies a release alternative: those jumps become same-width
// returns, and the selector can adopt another family without an inverse swap.
std::vector<SharedHelperDualModeSelectorExchangeResult>
reassign_stable_indirect_selector_families(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control_flow,
    const NaturalTargetComponentLayoutOptions& options = {});

// Release complete immutable selector families through proved direct flows
// within the caller's residual growth allowance. Run as an extension after
// retaining the existing search winner, never competing for its beam slots.
// Only a smaller complete downstream artifact may be published.
std::vector<SharedHelperDualModeSelectorExchangeResult>
release_stable_indirect_selector_families(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control_flow,
    const NaturalTargetComponentLayoutOptions& options = {});

// Mark one source-agnostic three-call helper family for an atomic layout.
// The operation is analysis-only: command order and machine semantics are not
// changed before the natural component solver has proved an exact placement.
SharedHelperDualModePreparation prepare_shared_helper_dual_mode_layout(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& control_flow,
    AddressSpaceModel model = AddressSpaceModel::Standard);

// Exact per-MachineItem widths of this pending tail rewrite, including erased
// caller tails, the expanded return and a possible new divergent operand.
// They are sizing metadata only, never an executable intermediate artifact.
std::optional<std::vector<int>> shared_helper_dual_mode_projected_widths(
    const SharedHelperDualModePreparation& preparation);

// Normalize a late artifact in which a dual-mode helper is still selected by
// a fixed-address register. If the helper is immediately followed by another
// independently returning procedure selected through a retunable decimal
// prefix, exchange the two complete target families atomically: move the
// second component onto the fixed address, move the dual-mode helper into its
// vacated range, swap only the corresponding indirect-flow register operands,
// and retune the flexible selector. The transaction is source-agnostic and is
// accepted only after rebuilding authoritative command-identity control flow.
SharedHelperDualModeSelectorExchangeResult
exchange_shared_helper_dual_mode_selector_families(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control_flow,
    const SharedHelperDualModePreparation& preparation,
    AddressSpaceModel model = AddressSpaceModel::Standard);

// Exchange the call encodings of two ordinary closed procedures when one is
// reached by several direct PP operands and the other by fewer calls through
// one stable, retunable KPP selector. The procedure bodies stay in place: the
// first family adopts the one-cell selector calls, while the second adopts
// direct calls. The transaction is accepted only when it removes cells and
// every relocated direct operand, indirect target, selector preload, external
// entry, and return continuation survives authoritative post-layout proof.
SharedHelperDualModeSelectorExchangeResult
exchange_profitable_direct_indirect_call_families(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control_flow,
    AddressSpaceModel model = AddressSpaceModel::Standard);

// A deferred inverse of the established dual-mode continuation law. This is
// not an executable laid-out artifact: unchanged selector words still refer
// to their old command identities. The caller must retain those equations,
// charge added_cells against the immutable root and close the final layout.
struct SharedHelperDualModeReopening {
  std::vector<MachineItem> items;
  SharedHelperContinuationProof continuation;
  std::vector<passes::AppliedOptimization> optimizations;
  std::vector<std::string> reasons;
  int added_cells = 0;
  int applied = 0;
};

SharedHelperDualModeReopening reopen_shared_helper_dual_mode_layout(
    const std::vector<MachineItem>& items,
    const PostLayoutControlFlowOptions& control_options);

// Complete an exact placement transaction. Two identical `join; store`
// continuations are moved into one official helper tail. When both ordinary
// continuations immediately perform one more identical direct store, that
// store is moved into the same tail as well. The divergent call is
// rebound to the same straight-line body through its B2..F9 side-space alias,
// so it reaches physical 00 and returns before the official tail. Stable
// selector preloads and typed runtime targets are rebound by command identity.
SharedHelperDualModeLayoutResult finalize_shared_helper_dual_mode_layout(
    const SharedHelperDualModePreparation& preparation,
    const std::vector<MachineItem>& placed_items,
    const std::vector<PreloadReport>& placed_preloads,
    const AuthoritativePostLayoutControlFlow& placed_control_flow,
    AddressSpaceModel model = AddressSpaceModel::Standard,
    // For deferred geometry, these are the selector's encoded origins, not
    // the current provisional positions. Every non-runtime family is required.
    const std::map<std::string, int>* encoded_selector_targets = nullptr);

} // namespace mkpro::core
