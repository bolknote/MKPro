#pragma once

#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace mkpro::core {

// Inclusive integer bounds around ordinary decimal values, not raw BCD words.
// integral means every represented value is integral; it is not inferred from
// integral endpoints. Unknown values have no range.
struct OrdinaryNumericRange {
  std::int64_t lower = 0;
  std::int64_t upper = 0;
  bool integral = false;

  bool operator==(const OrdinaryNumericRange&) const = default;
};

std::optional<OrdinaryNumericRange> bound_ordinary_decimal(std::string_view value);

struct PostLayoutNumericRangeProof {
  // X immediately before an item, joined over every matched call context.
  // Absence means unknown, including unreachable or externally entered values.
  std::map<std::size_t, OrdinaryNumericRange> x_before;
  std::size_t work = 0;
  bool exhausted = false;
};

// Demand-driven must analysis on the authoritative return-context graph.
// Only exact eight-digit integral arithmetic is propagated. Unknown branches
// join both alternatives, unresolved cycles fail closed, and preload facts
// apply only to Main, never to resume/manual entries. No host floating point.
PostLayoutNumericRangeProof prove_post_layout_numeric_ranges(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<bool>& automatic_entry_lift,
    const std::vector<std::size_t>& requested_items,
    const std::vector<PreloadReport>* preloads = nullptr,
    std::size_t maximum_work = 100000);

} // namespace mkpro::core
