#pragma once

#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/result.hpp"

#include <cstddef>
#include <map>
#include <string_view>
#include <vector>

namespace mkpro::core {

// Demand marker only. The certificate still checks the real producers and
// every matched predecessor; this marker supplies no numerical assumption.
inline constexpr std::string_view kUnitProjectionDemandRole =
    "unit-argument-normalizer";

struct PackedSignInputProof {
  std::map<std::size_t, int> signs; // Execution-state IDs; values are -1 or +1.
  std::size_t work = 0;
  bool exhausted = false;
};

// Canonical logical words have positive sign even with hexadecimal payloads.
// A negative difference additionally needs an OR-only, zero-seeded strict
// progress edge and the proved old-minus-new operand orientation. No fact is
// inferred for an arbitrary raw word. Exhaustion exports no partial facts.
PackedSignInputProof prove_post_layout_packed_sign_inputs(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<bool>& automatic_entry_lift,
    const std::vector<std::size_t>& requested_states,
    const std::vector<PreloadReport>* preloads = nullptr,
    std::size_t maximum_work = 100000);

} // namespace mkpro::core
