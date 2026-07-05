#pragma once

#include "mkpro/core/formal_address.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace mkpro::core {

enum class IndirectSelectorMutation {
  PreDecrement,
  PreIncrement,
  Stable,
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
  std::optional<std::string> result_value;
  std::optional<SuperDarkIndirectTarget> super_dark;
};

IndirectSelectorMutation indirect_selector_mutation(int register_index);
IndirectSelectorMutation indirect_selector_mutation(std::string_view register_name);
bool is_stable_indirect_selector(std::string_view register_name);

std::optional<IndirectAddressEvaluation> evaluate_indirect_address(
    std::string_view selector, double value, IndirectOperationKind operation,
    AddressSpaceModel model = AddressSpaceModel::Standard);
std::optional<IndirectAddressEvaluation> evaluate_indirect_address(
    std::string_view selector, std::string_view value, IndirectOperationKind operation,
    AddressSpaceModel model = AddressSpaceModel::Standard);

std::optional<int> memory_target_from_transformed(std::string_view transformed);
std::optional<SuperDarkIndirectTarget> super_dark_target(
    int formal_target, AddressSpaceModel model = AddressSpaceModel::Standard);

std::string indirect_selector_mutation_name(IndirectSelectorMutation mutation);

}  // namespace mkpro::core
