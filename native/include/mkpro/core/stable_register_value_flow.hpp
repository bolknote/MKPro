#pragma once

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/ir.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/result.hpp"

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core {

// Flow-sensitive abstract interpretation of the stable data registers R7..Re
// over the authoritative post-layout execution-state graph. The analysis
// tracks, for every explored execution state, whether a stable register
// provably holds one exact decimal literal on every path reaching that state.
//
// Value sources are deliberately narrow and fail closed:
// - a literal runtime preload initializes its register at the main entry;
// - `X->П r` stores the tracked X value, and X is known only after an
//   uninterrupted integer digit-entry run, a recall of a tracked register, or
//   a `/-/` negation of an already-known X;
// - an indirect access through a stable register replaces the tracked value
//   with the machine's write-back (the transformed selector value).
//
// Everything else poisons the touched slot: fractional/exponent entry, any
// arithmetic, indirect memory stores that may reach a tracked register, and
// manual external entries. A resumable stop keeps the tracked registers of
// its stop states but forgets X, matching the system-wide contract that user
// interaction at a stop enters data through X without rewriting program
// registers. If any external entry is not the main entry or a resumable stop
// whose preceding executable command is `С/П`, the whole analysis is
// unproved.
struct StableRegisterValueFlow {
  bool proved = false;
  std::vector<std::string> reasons;
  std::size_t total_states = 0;
  std::size_t valued_states = 0;
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

}  // namespace mkpro::core
