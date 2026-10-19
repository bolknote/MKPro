#pragma once

#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
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
  // Same original execution-state IDs as the supplied authoritative graph.
  // Keep distinct caller domains even when their physical item joins unknown.
  // Optional expanded progress graphs never export their private state IDs.
  std::map<std::size_t, OrdinaryNumericRange> x_before_state;
  // Stronger word-shape fact, not inferred from integer interval endpoints.
  std::set<std::size_t> canonical_small_integer_states;
  // At these original direct-recall states the source MEMORY word itself
  // is canonical, before recall normalization. Never inferred from X alone.
  std::set<std::size_t> canonical_memory_load_states;
  // Complete guarded definition/use proofs may exclude an execution state.
  // This is distinct from an absent/unknown numeric value; no CFG is edited.
  std::set<std::size_t> unreachable_states;
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
// Ordinary 10^x uses exact integer-power endpoints for -99 <= x <= 7;
// integral exponents additionally preserve an eight-digit decimal lattice
// when representable. Fractional exponents never imply a rational result.
// Recall-established fresh-literal lift survives proved direct BP chains and
// joins; ungrounded cycles, unsupported paths and external entries reject.
// Positive integral FL counters use their labelled branch/exit semantics.
// Input register transport is opt-in and requires the shared protocol proof.
// Unknown branches
// join both alternatives; expanding ranges widen to unknown after eight changes.
// A separate finite digit-set component keeps word shape through copies/joins.
// Logical operations may certify ordinary 8.x results even for unknown inputs;
// hexadecimal alternatives remain nonnumeric. Widening an interval does not
// erase the independent digit invariant or assume an input range.
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
