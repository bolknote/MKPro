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

struct LoopResult {
  std::string iterations;
  std::string counter;
};

LoopResult run_loop(const std::string& n) {
  emulator::MK61 calc;
  calc.load_program({0x61, 0x01, 0x10, 0x41, 0x5d, 0x00, 0x50});
  calc.set_register("0", n);
  calc.set_register("1", "0");
  calc.press_sequence({"В/О", "С/П"});
  calc.run_until_stable(4000, 5);
  return LoopResult{.iterations = compact(calc.read_register("1")),
                    .counter = compact(calc.read_register("0"))};
}

} // namespace

void emulator_fl_counter_facts_match_typescript_contract() {
  for (const std::string n : {"1", "2", "3", "5"}) {
    const LoopResult result = run_loop(n);
    require(std::stoi(result.iterations) == std::stoi(n),
            "F L0 loop should run exactly N iterations");
    require(std::stoi(result.counter) == 1,
            "F L0 loop should exit with the counter register equal to 1");
  }
}

} // namespace mkpro::tests
