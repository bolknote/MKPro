#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace mkpro::core {

// A deliberately finite, ROM-checked representation family, not a conversion
// of an arbitrary BCD word into a host floating-point value.
struct RawSingleHexWord {
  std::string canonical_word;
  int digit = 0;
  int exponent = 0;
  bool negative = false;
};

// Signed A..D with order -1..-4. Accepts the corresponding calculator glyphs
// for C/D and an explicit 0x tag; emits unambiguous ASCII scientific spelling.
std::optional<RawSingleHexWord> decode_raw_single_hex_word(std::string_view word);

struct RawPower10Product {
  int exponent = 0;
  bool negative = false;
  std::string last_x_word;
};

// Exact finite operand domain: positive 10^p, -8 <= p <= 8, in Y, and the
// decoded raw word in X. Operand order is part of the contract. X1 receives
// the raw operand, NOT the ordinary coefficient with the same product.
// This certifies a numeric observation only, not stack/X2 equivalence or a
// rewrite on unknown inputs. Every admitted pair is covered by ROM tests.
std::optional<RawPower10Product> evaluate_raw_right_power10_product(
    int left_exponent, std::string_view right_word);

} // namespace mkpro::core
