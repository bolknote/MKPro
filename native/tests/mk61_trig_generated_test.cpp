#include "mkpro/core/mk61_trig_generated.hpp"

#include "test_support.hpp"

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
          "generated MK-61 trig result mismatch for value " + std::string(value) +
              ": got " + actual + ", expected " + std::string(expected));
}

}  // namespace

void mk61_trig_generated_matches_rom_derived_contract() {
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

}  // namespace mkpro::tests
