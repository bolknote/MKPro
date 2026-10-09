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

enum class NumericRangeInputPolicy {
  Forget,
  // Preserve predecessor registers, never input X or setup-state assumptions.
  // Requires exact typed input/resume edges including their return contexts.
  ValidatedRegisterTransport,
};

// Demand-driven fixed point on the authoritative return-context graph, without
// a recursive call-depth cutoff. Bottom and unknown are distinct: inductive
// constants may survive loops, but ungrounded cycles never prove a value.
// Ordinary arithmetic uses outward integer enclosures, including fractions.
// Positive integral FL counters use their labelled branch/exit semantics.
// Input register transport is opt-in and requires the shared protocol proof.
// Unknown branches
// join both alternatives; expanding ranges widen to unknown after eight changes.
// Preload facts apply only to Main, never to resume/manual entries. No host
// floating point. Budget exhaustion discards all facts, never a partial iterate.
PostLayoutNumericRangeProof prove_post_layout_numeric_ranges(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<bool>& automatic_entry_lift,
    const std::vector<std::size_t>& requested_items,
    const std::vector<PreloadReport>* preloads = nullptr,
    std::size_t maximum_work = 100000,
    NumericRangeInputPolicy input_policy = NumericRangeInputPolicy::Forget);

} // namespace mkpro::core
