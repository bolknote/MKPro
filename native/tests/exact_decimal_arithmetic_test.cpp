#include "mkpro/core/passes/helpers.hpp"

#include "test_support.hpp"

#include <optional>
#include <string>

namespace mkpro::tests {

namespace {

using mkpro::core::passes::concrete_decimal_binary_value;
using mkpro::core::passes::concrete_decimal_unary_value;

void expect_binary(int opcode, const std::string& y, const std::string& x,
                   const std::optional<std::string>& expected) {
  const std::optional<std::string> actual = concrete_decimal_binary_value(opcode, y, x);
  const std::string actual_text = actual.has_value() ? *actual : std::string{"<undefined>"};
  const std::string expected_text = expected.has_value() ? *expected : std::string{"<undefined>"};
  require(actual == expected, "binary opcode " + std::to_string(opcode) + " (" + y + ", " + x +
                                  ") expected " + expected_text + " got " + actual_text);
}

void expect_unary(int opcode, const std::string& value,
                  const std::optional<std::string>& expected) {
  const std::optional<std::string> actual = concrete_decimal_unary_value(opcode, value);
  const std::string actual_text = actual.has_value() ? *actual : std::string{"<undefined>"};
  const std::string expected_text = expected.has_value() ? *expected : std::string{"<undefined>"};
  require(actual == expected, "unary opcode " + std::to_string(opcode) + " (" + value +
                                  ") expected " + expected_text + " got " + actual_text);
}

}  // namespace

void exact_decimal_arithmetic_matches_typescript_contract() {
  // Addition (0x10) and subtraction (0x11) operate on the common scale.
  expect_binary(0x10, "0.1", "0.2", "0.3");
  expect_binary(0x10, "1", "2", "3");
  expect_binary(0x10, "-2", "2", "0");
  expect_binary(0x11, "5", "3", "2");
  expect_binary(0x11, "0.3", "0.1", "0.2");
  expect_binary(0x11, "1", "2", "-1");

  // Multiplication (0x12) sums the scales.
  expect_binary(0x12, "2", "3", "6");
  expect_binary(0x12, "1.5", "1.5", "2.25");
  expect_binary(0x12, "-2", "3", "-6");

  // Division (0x13) is exact only when the reduced denominator factors into 2/5.
  expect_binary(0x13, "1", "4", "0.25");
  expect_binary(0x13, "1", "8", "0.125");
  expect_binary(0x13, "6", "2", "3");
  expect_binary(0x13, "10", "4", "2.5");
  expect_binary(0x13, "0", "5", "0");
  expect_binary(0x13, "1", "3", std::nullopt);
  expect_binary(0x13, "1", "0", std::nullopt);

  // x^y identity cases (0x24): exponent is y, base is x.
  expect_binary(0x24, "1", "5", "5");
  expect_binary(0x24, "0", "5", "1");
  expect_binary(0x24, "3", "1", "1");

  // max (0x36) — note the MK-61 quirk that max with zero yields zero.
  expect_binary(0x36, "3", "5", "5");
  expect_binary(0x36, "7", "5", "7");
  expect_binary(0x36, "0", "5", "0");

  // Bitwise mantissa ops (0x37..0x39) force a leading mantissa digit of 8.
  expect_binary(0x37, "0", "0", "8");

  // Square root (0x21) — exact perfect squares only, even scale.
  expect_unary(0x21, "4", "2");
  expect_unary(0x21, "9", "3");
  expect_unary(0x21, "1.44", "1.2");
  expect_unary(0x21, "0", "0");
  expect_unary(0x21, "2", std::nullopt);

  // Square (0x22) and reciprocal (0x23).
  expect_unary(0x22, "1.5", "2.25");
  expect_unary(0x22, "3", "9");
  expect_unary(0x22, "-2", "4");
  expect_unary(0x23, "8", "0.125");
  expect_unary(0x23, "4", "0.25");
  expect_unary(0x23, "2", "0.5");
  expect_unary(0x23, "3", std::nullopt);

  // Powers of ten (0x15).
  expect_unary(0x15, "2", "100");
  expect_unary(0x15, "0", "1");
  expect_unary(0x15, "-2", "0.01");
  expect_unary(0x15, "3", "1000");

  // abs (0x31), sign (0x32), integer part (0x34), fraction part (0x35).
  expect_unary(0x31, "-5", "5");
  expect_unary(0x31, "5", "5");
  expect_unary(0x31, "-1.5", "1.5");
  expect_unary(0x32, "-3", "-1");
  expect_unary(0x32, "0", "0");
  expect_unary(0x32, "7", "1");
  expect_unary(0x34, "3.7", "3");
  expect_unary(0x34, "-3.7", "-3");
  expect_unary(0x34, "0.5", "0");
  expect_unary(0x35, "3.25", "0.25");
  expect_unary(0x35, "-3.25", "-0.25");
  expect_unary(0x35, "5", "0");

  // Transcendental identity points (0x16 e^x, 0x18 ln).
  expect_unary(0x16, "0", "1");
  expect_unary(0x16, "1", std::nullopt);
  expect_unary(0x18, "1", "0");
  expect_unary(0x18, "2", std::nullopt);

  // Centesimal degree conversion (0x26): 1 degree 30' == 1.5 degrees.
  expect_unary(0x26, "1.3", "1.5");
}

}  // namespace mkpro::tests
