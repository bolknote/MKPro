#include "mkpro/core/raw_bcd_unary_selector.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

using core::mk61_trig::AngleMode;
using core::mk61_trig::Function;

std::string compact(std::string value) {
  value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
  return value;
}

std::string canonical_emulator_number(std::string value) {
  value = compact(std::move(value));
  if (value == "0,")
    return "0";
  std::replace(value.begin(), value.end(), ',', '.');
  const std::size_t exponent = value.rfind('-');
  if (exponent != std::string::npos && exponent != 0U)
    value.insert(exponent, "E");
  return value;
}

std::string emulator_deg_tg_raw_result(const std::string& seed) {
  emulator::MK61 calc({.extended = true, .angle_mode = "deg"});
  const emulator::ProgramLoadResult loaded = calc.load_program({0x6a, 0x1e, 0x4e, 0x50});
  require(loaded.diagnostics.empty(), "raw BCD Ftg oracle program should load");
  calc.set_register("a", seed);
  calc.press("В/О");
  calc.press("С/П");
  require(calc.run_until_stable(500, 4).stopped,
          "raw BCD Ftg oracle program should stop");
  return canonical_emulator_number(calc.read_register("e"));
}

int emulator_deg_tg_flow_target(const std::string& seed) {
  std::vector<int> program(105, 0x50);
  program.at(0) = 0x6a;
  program.at(1) = 0x1e;
  program.at(2) = 0x4e;
  program.at(3) = 0xae;

  emulator::MK61 calc({.extended = true, .angle_mode = "deg"});
  const emulator::ProgramLoadResult loaded = calc.load_program(program);
  require(loaded.diagnostics.empty(), "raw BCD indirect-flow oracle program should load");
  calc.set_register("a", seed);
  calc.press("В/О");
  calc.press("С/П");
  require(calc.run_until_stable(800, 4).stopped,
          "raw BCD indirect-flow oracle program should stop");

  // Target zero re-enters the four-cell probe, so program_counter is not a
  // useful oracle for the A-family. Ftg(AE-n) is exact zero and stable Re=0,
  // which independently proves target zero.
  if (compact(calc.read_register("e")) == "0,")
    return 0;

  const std::string pc = calc.program_counter();
  require(pc.size() == 2U && pc.at(0) >= '0' && pc.at(0) <= '9' && pc.at(1) >= '0' &&
              pc.at(1) <= '9',
          "supported raw BCD selector should stop in the official 00..99 address range");
  return std::stoi(pc) - 1;
}

const core::RawBcdUnaryIndirectSelectorResult& require_result(
    const std::optional<core::RawBcdUnaryIndirectSelectorResult>& result,
    const std::string& context) {
  require(result.has_value(), context + " should be statically supported");
  return *result;
}

} // namespace

void raw_bcd_unary_selector_matches_emulator_oracle() {
  const std::array<std::string, 4> digits{"A", "B", "C", "Г"};
  for (const std::string& digit : digits) {
    for (int exponent = 1; exponent <= 4; ++exponent) {
      const std::string magnitude_seed = digit + "E-" + std::to_string(exponent);
      const auto positive = core::evaluate_raw_bcd_unary_indirect_selector(
          AngleMode::Deg, Function::Tg, magnitude_seed, "e");
      const auto negative = core::evaluate_raw_bcd_unary_indirect_selector(
          AngleMode::Deg, Function::Tg, "-" + magnitude_seed, "e");
      const auto& positive_result = require_result(positive, magnitude_seed);
      const auto& negative_result = require_result(negative, "-" + magnitude_seed);

      require(positive_result.raw_result == emulator_deg_tg_raw_result(magnitude_seed),
              magnitude_seed + " raw Ftg result should match the emulator oracle");
      require(negative_result.raw_result == emulator_deg_tg_raw_result("-" + magnitude_seed),
              "-" + magnitude_seed + " raw Ftg result should match the emulator oracle");
      require(positive_result.actual_flow_target == emulator_deg_tg_flow_target(magnitude_seed),
              magnitude_seed + " flow target should match the emulator oracle");
      require(negative_result.actual_flow_target ==
                  emulator_deg_tg_flow_target("-" + magnitude_seed),
              "-" + magnitude_seed + " flow target should match the emulator oracle");
      require(positive_result.formal_flow_target == negative_result.formal_flow_target &&
                  positive_result.actual_flow_target == negative_result.actual_flow_target,
              magnitude_seed + " selector target should be invariant under sign");
    }
  }

  const auto ge2_positive = core::evaluate_raw_bcd_unary_indirect_selector(
      AngleMode::Deg, Function::Tg, "ГE-2", "e");
  const auto ge2_negative = core::evaluate_raw_bcd_unary_indirect_selector(
      AngleMode::Deg, Function::Tg, "-ГE-2", "7");
  const auto ge3_positive = core::evaluate_raw_bcd_unary_indirect_selector(
      AngleMode::Deg, Function::Tg, "ГE-3", "Re");
  const auto ge3_negative = core::evaluate_raw_bcd_unary_indirect_selector(
      AngleMode::Deg, Function::Tg, "-ГE-3", "a");
  require(require_result(ge2_positive, "+ГE-2").actual_flow_target == 88 &&
              require_result(ge2_negative, "-ГE-2").actual_flow_target == 88,
          "Ftg(±ГE-2) in DEG should select address 88");
  require(require_result(ge3_positive, "+ГE-3").actual_flow_target == 98 &&
              require_result(ge3_negative, "-ГE-3").actual_flow_target == 98,
          "Ftg(±ГE-3) in DEG should select address 98");

  require(!core::evaluate_raw_bcd_unary_indirect_selector(
               AngleMode::Rad, Function::Tg, "ГE-2", "e")
               .has_value(),
          "RAD Ftg seed should fail closed");
  require(!core::evaluate_raw_bcd_unary_indirect_selector(
               AngleMode::Grad, Function::Tg, "ГE-2", "e")
               .has_value(),
          "GRAD Ftg seed should fail closed");
  require(!core::evaluate_raw_bcd_unary_indirect_selector(
               AngleMode::Deg, Function::Sin, "ГE-2", "e")
               .has_value(),
          "unsupported unary operation should fail closed");
  require(!core::evaluate_raw_bcd_unary_indirect_selector(
               AngleMode::Deg, Function::Tg, "ГE-2", "3")
               .has_value(),
          "mutating indirect selector should fail closed");
  require(!core::evaluate_raw_bcd_unary_indirect_selector(
               AngleMode::Deg, Function::Tg, "ЕE-2", "e")
               .has_value(),
          "unsupported structural hex digit should fail closed");
  require(!core::evaluate_raw_bcd_unary_indirect_selector(
               AngleMode::Deg, Function::Tg, "ГE-5", "e")
               .has_value(),
          "unsupported structural exponent should fail closed");
}

} // namespace mkpro::tests
