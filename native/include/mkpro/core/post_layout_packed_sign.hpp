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

struct ToggleSignInputProof {
  std::map<std::size_t, int> signs;
  // Exact physical dependencies. A demand slice may reuse a sign only while
  // leaving this closed carrier's transitions and definitions unchanged.
  std::map<std::size_t, std::string> carriers;
  std::size_t work = 0;
  bool exhausted = false;
  // Exact post-command values for small integral members of the same closed
  // automaton. A fraction is not rounded into this component. The dependency
  // is retained separately so a sliced guard cannot prove itself.
  std::map<std::size_t, int> values;
  std::map<std::size_t, std::string> value_carriers;
};

// Read-only finite-state certificate for a delivered nonzero scalar carrier
// whose only writes are complete Recall/Negate/Store transitions. Derive each
// queried immediate-recall or transition-store sign from Main and validated typed resumes, retaining
// every branch and exact physical/formal return context. Resolved formal
// address operands are supported by the same authoritative graph; raw command
// overrides, unknown rows, memory aliases, transition side entries, untyped
// input and exhausted work export no assumptions or partial facts.
ToggleSignInputProof prove_post_layout_toggle_sign_inputs(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<std::size_t>& requested_states,
    const std::vector<PreloadReport>& preloads,
    std::size_t maximum_work = 100000);

} // namespace mkpro::core
