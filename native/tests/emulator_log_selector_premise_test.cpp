#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <string>

namespace mkpro::tests {

namespace {

std::string compact(std::string value) {
  std::string out;
  for (const char ch : value) {
    if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r')
      out.push_back(ch);
  }
  return out;
}

std::string log_of(int opcode, const std::string& x) {
  emulator::MK61 calc;
  calc.load_program({opcode, 0x50});
  calc.set_register("x", x);
  calc.press_sequence({"В/О", "С/П"});
  calc.run_until_stable(200, 5);
  return compact(calc.read_register("x"));
}

} // namespace

void emulator_log_selector_premise_matches_typescript_contract() {
  {
    emulator::MK61 calc;
    calc.load_program({0x50});
    calc.set_register("x", "-5");
    calc.press_sequence({"В/О", "С/П"});
    calc.run_until_stable(50, 5);
    require(compact(calc.read_register("x")).starts_with("-"),
            "emulator should represent negative mantissas");
  }

  const std::string lg_half = log_of(0x17, "0.5");
  require(!lg_half.starts_with("-"), "F lg of sub-unit value should not be negative");
  require(lg_half == log_of(0x17, "2"),
          "F lg of 0.5 should have the same magnitude as F lg of 2");

  require(!log_of(0x18, "0.5").starts_with("-"),
          "F ln of sub-unit value should not be negative");
}

} // namespace mkpro::tests
