#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::string run_with(const std::string& reg, const std::vector<int>& codes) {
  emulator::MK61 calc;
  calc.set_register("1", reg);
  calc.load_program(codes);
  calc.press_sequence({"В/О", "С/П"});
  calc.run_until_stable(200, 4);
  return calc.display_text();
}

} // namespace

void emulator_packed_position_facts_match_typescript_contract() {
  require(run_with("3.0204", {0x61, 0x34, 0x50}) == "3,",
          "К [x] should recover the integer position from a packed value");
  require(run_with("3.0204", {0x61, 0x35, 0x50}) == "2,04     -02",
          "К {x} should recover packed fractional sub-coordinates");
  require(run_with("3.0204", {0x61, 0x50}) == "3,0204",
          "packed position value should echo unchanged");
}

} // namespace mkpro::tests
