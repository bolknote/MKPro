#pragma once

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/ir.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/result.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core {

// Validated input roots shared by value analyses. Setup state belongs only
// to main_states. Input boundaries inherit register values from exact labelled
// predecessor contexts; numeric entry itself supplies no value/stack facts.
struct PostLayoutInputBoundaryProof {
  bool proved = false;
  std::vector<std::string> reasons;
  std::vector<std::size_t> main_states;
  std::vector<bool> input_boundary_states;
};

PostLayoutInputBoundaryProof prove_post_layout_input_boundaries(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow);

// Flow-sensitive abstract interpretation of the stable data registers R7..Re
// over the authoritative post-layout execution-state graph. The analysis
// tracks, for every explored execution state, whether a stable register
// provably holds one exact decimal literal on every path reaching that state.
//
// Value sources are deliberately narrow and fail closed:
// - a literal runtime preload initializes its register at the main entry;
// - `X->П r` stores the tracked X value, and X is known only after an
//   uninterrupted integer digit-entry run, a recall of a tracked register, or
//   a `/-/` negation of an already-known X. For a proved integer entry, sign
//   closes the entry: `1 /-/ 2` is 2, not -12. Typed signed zero stays unknown;
//   this exact-X fact does not certify automatic lift or deeper stack slots;
// - an indirect access through a stable register replaces the tracked value
//   with the machine's write-back (the transformed selector value).
//
// Everything else poisons the touched slot: fractional/exponent entry,
// arithmetic, and indirect stores that may reach a tracked register. Ordinary
// and error resumes and validated entered() phases preserve exact predecessor
// register values while forgetting X and number-entry state. Resume edges,
// not physical adjacency, identify the source stop (an error skips one cell).
// Each manual phase must also match its typed prompt/store chain. Distinct
// caller contexts stay separate until their real CFG join, including formal
// return frames. Unlinked or untyped external entries fail closed; user input
// is never treated as a fresh setup-state assumption.
struct StableRegisterValueFlow {
  bool proved = false;
  std::vector<std::string> reasons;
  std::size_t total_states = 0;
  std::size_t valued_states = 0;
  // Exact X before each flow.execution_states entry, valid only when proved.
  // Caller contexts stay separate. This certifies neither automatic stack
  // lift nor any deeper stack slot; unsupported numeric entry remains unknown.
  std::vector<std::optional<std::string>> x_before_state;
  // Before edge effects: a resume/manual input boundary must still forget X.
  // Keeping outgoing words separate lets range joins retain differing literals.
  std::vector<std::optional<std::string>> x_after_state;
  // Joined known values of R7..Re (index 0 = R7) before executing each item,
  // over every execution state of that item. Items with no execution state
  // are absent.
  std::map<std::size_t, std::array<std::optional<std::string>, 8>> before_item;
};

StableRegisterValueFlow analyze_stable_register_value_flow(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& flow,
    AddressSpaceModel model = AddressSpaceModel::Standard);


struct PostLayoutMantissaBitProof {
  // Possible one bits in the seven payload nibbles (leading digit excluded).
  // Missing items and an exhausted analysis never authorize a rewrite.
  std::map<std::size_t, std::uint32_t> x_possible_bits;
  std::size_t work = 0;
  bool exhausted = false;
};

// A finite may-bit lattice on the exact return-context graph. Joins use OR;
// K AND intersects possible bits, K OR/XOR conservatively union them.
// Unknown arithmetic/entry forgets affected stack values, not independent
// registers. Input boundaries inherit registers only through a validated
// protocol. Constants use a deliberately narrow, exact eight-digit grammar:
// decimal equality alone does not establish a stored mantissa's shape.
PostLayoutMantissaBitProof prove_post_layout_mantissa_bits(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<std::size_t>& requested_items,
    std::size_t maximum_work = 100000);

}  // namespace mkpro::core
