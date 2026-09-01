#pragma once

#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/shared_helper_continuation.hpp"

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

// Mark one source-agnostic three-call helper family for an atomic layout.
// The operation is analysis-only: command order and machine semantics are not
// changed before the natural component solver has proved an exact placement.
SharedHelperDualModePreparation prepare_shared_helper_dual_mode_layout(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& control_flow,
    AddressSpaceModel model = AddressSpaceModel::Standard);

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
    AddressSpaceModel model = AddressSpaceModel::Standard);

} // namespace mkpro::core
