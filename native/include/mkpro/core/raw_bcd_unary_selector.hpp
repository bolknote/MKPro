#pragma once

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/mk61_trig.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace mkpro::core {

// A statically proved raw-register identity for a unary MK-61 operation whose
// result is consumed by stable indirect flow. `raw_result` deliberately keeps
// the ROM-visible significant digits and exponent; normalizing it through a
// host floating-point value loses selector digits used by the calculator.
struct RawBcdUnaryIndirectSelectorResult {
  std::string canonical_seed;
  std::string raw_result;
  std::string selector;
  int formal_flow_target = 0;
  int actual_flow_target = 0;
};

// First fail-closed family: F tg in DEG mode over a signed, single-hex-digit
// exponent seed A..D (MK-61 glyphs A, B, C, Г), exponent -1..-4, followed by
// indirect flow through a stable R7..Re selector. Unsupported modes,
// operations, seed shapes, and mutating selectors return nullopt.
std::optional<RawBcdUnaryIndirectSelectorResult>
evaluate_raw_bcd_unary_indirect_selector(
    mk61_trig::AngleMode mode, mk61_trig::Function operation, std::string_view raw_seed,
    std::string_view selector, AddressSpaceModel model = AddressSpaceModel::Standard);

} // namespace mkpro::core
