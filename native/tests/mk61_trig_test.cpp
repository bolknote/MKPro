#include "mkpro/core/mk61_trig.hpp"

#include "test_support.hpp"

#include <cmath>
#include <string>
#include <string_view>

namespace mkpro::tests {

namespace {

using core::mk61_trig::AngleMode;
using core::mk61_trig::Function;

void expect_display(AngleMode mode,
                    Function function,
                    std::string_view value,
                    std::string_view expected) {
  const std::string actual = core::mk61_trig::calculate_display(mode, function, value);
  require(actual == expected,
          "MK-61 trig result mismatch for value " + std::string(value) + ": got " + actual +
              ", expected " + std::string(expected));
}

}  // namespace

void mk61_trig_matches_emulator_contract() {
  expect_display(AngleMode::Rad, Function::Sin, "0.25", "2,4740395-01");
  expect_display(AngleMode::Rad, Function::Sin, "-0.25", "-2,4740395-01");
  expect_display(AngleMode::Rad, Function::Tg, "-0.25", "-2,5534191-01");

  expect_display(AngleMode::Deg, Function::Sin, "30", "5,0000002-01");
  expect_display(AngleMode::Deg, Function::Sin, "-90", "-1,");
  expect_display(AngleMode::Deg, Function::Cos, "60", "5,0000002-01");
  expect_display(AngleMode::Deg, Function::Sin, "0.25", "4,3633093-03");

  expect_display(AngleMode::Grad, Function::Tg, "50", "1,");
  expect_display(AngleMode::Grad, Function::Tg, "-50", "-1,");
}

namespace {

void expect_value(AngleMode mode, Function function, double value, double expected) {
  const double actual = core::mk61_trig::calculate(mode, function, value);
  require(std::fabs(actual - expected) < 1e-7,
          "MK-61 trig numeric result mismatch: got " + std::to_string(actual) + ", expected " +
              std::to_string(expected));
}

}  // namespace

// The numeric calculate() API (used by the address-formula solver) must return
// the same ROM-faithful values the display API formats.
void mk61_trig_calculate_matches_rom_values() {
  expect_value(AngleMode::Rad, Function::Sin, 0.25, 0.24740395);
  expect_value(AngleMode::Rad, Function::Tg, -0.25, -0.25534191);
  expect_value(AngleMode::Deg, Function::Sin, 30.0, 0.50000002);
  expect_value(AngleMode::Deg, Function::Cos, 60.0, 0.50000002);
  expect_value(AngleMode::Grad, Function::Tg, 50.0, 1.0);
  expect_value(AngleMode::Grad, Function::Tg, -50.0, -1.0);
}

}  // namespace mkpro::tests
