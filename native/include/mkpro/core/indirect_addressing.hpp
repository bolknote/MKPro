#pragma once

#include "mkpro/core/formal_address.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mkpro::core {

enum class IndirectSelectorMutation {
  PreDecrement,
  PreIncrement,
  Stable, // No +/-1 counter update; this does not imply data-word preservation.
};

enum class IndirectOperationKind {
  Flow,
  Memory,
};

struct SuperDarkIndirectTarget {
  int formal = 0;
  int entry_address = 0;
  int continuation_address = 0;
};

struct IndirectAddressEvaluation {
  std::string selector;
  IndirectSelectorMutation mutation = IndirectSelectorMutation::Stable;
  IndirectOperationKind operation = IndirectOperationKind::Memory;
  std::string transformed;
  std::optional<FormalAddressInfo> formal_address;
  std::optional<int> flow_target;
  std::optional<int> actual_flow_target;
  std::optional<int> memory_target;
  // Replayable selector spelling; nondecimal BCD words retain a 0x type tag.
  std::optional<std::string> result_value;
  std::optional<SuperDarkIndirectTarget> super_dark;
};

IndirectSelectorMutation indirect_selector_mutation(int register_index);
IndirectSelectorMutation indirect_selector_mutation(std::string_view register_name);
bool is_stable_indirect_selector(std::string_view register_name);

std::optional<IndirectAddressEvaluation> evaluate_indirect_address(
    std::string_view selector, double value, IndirectOperationKind operation,
    AddressSpaceModel model = AddressSpaceModel::Standard);
// Interpret delivered MK-61 word spelling, not just its mathematical value.
// In particular 0.5 (order zero) and 5E-1 have distinct indirect write-back.
// Scientific notation takes precedence over ambiguous raw BCD (use 0x for
// the latter). Negative orders are shifted by their BCD units digit, with
// sign-filled mantissas and exact write-back, including denormalized boundary
// words produced by a prior access. Unsupported carry/borrow fails closed.
std::optional<IndirectAddressEvaluation> evaluate_indirect_address(
    std::string_view selector, std::string_view value, IndirectOperationKind operation,
    AddressSpaceModel model = AddressSpaceModel::Standard);

// Required before an immutable data literal can also supply a flow address.
// Stable excludes counter updates, but does not establish this property.
// Fractional/sign-filled write-back is never justified by floating equality.
bool indirect_writeback_preserves_literal_value(
    const IndirectAddressEvaluation& evaluation, std::string_view original_value);

// Preserve noncanonical runtime entry counters in typed flow metadata.
// nullopt retains the ordinary relocatable physical/logical target contract.
// A missing, non-flow, or incomplete evaluation yields an explicitly invalid
// empty fact, never an invented canonical entry.
std::optional<std::vector<int>> noncanonical_indirect_flow_entries(
    const std::optional<IndirectAddressEvaluation>& evaluation);

std::optional<int> memory_target_from_transformed(std::string_view transformed);
std::optional<SuperDarkIndirectTarget> super_dark_target(
    int formal_target, AddressSpaceModel model = AddressSpaceModel::Standard);

std::string indirect_selector_mutation_name(IndirectSelectorMutation mutation);

}  // namespace mkpro::core
