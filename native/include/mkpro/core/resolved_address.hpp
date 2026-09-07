#pragma once

#include "mkpro/core/formal_address.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mkpro {

// A position in a provisional layout is not an MK-61 address byte. In the
// symbolic IR it is still represented by an opaque label; linking preserves
// its domain here instead of truncating it to a byte for analysis output.
struct LogicalCodeAddress {
  int index = 0;
  bool operator==(const LogicalCodeAddress&) const = default;
};

// Keep the actual entry byte, not just its aliased physical cell: short-side,
// long-side and one-command entries have distinct machine semantics.
struct FormalCodeAddress {
  int opcode = 0;
  bool operator==(const FormalCodeAddress&) const = default;
};

using ResolvedAddress = std::variant<LogicalCodeAddress, FormalCodeAddress>;

struct ResolvedStep;

bool resolved_step_takes_address(const ResolvedStep& step);

// These readers reject stale typed targets that disagree with an encoded
// operand. Metadata must never hide a mutation of the delivered bytecode.
std::optional<int> resolved_step_target(
    const ResolvedStep& operand,
    AddressSpaceModel model = AddressSpaceModel::Standard);
std::optional<int> resolved_logical_target(
    const ResolvedStep& operand,
    AddressSpaceModel model = AddressSpaceModel::Standard);

std::optional<std::string> physical_program_image_rejection(
    const std::vector<ResolvedStep>& steps,
    AddressSpaceModel model = AddressSpaceModel::Standard);

} // namespace mkpro
