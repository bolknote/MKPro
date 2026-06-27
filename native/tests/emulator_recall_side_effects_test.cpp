#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <string>
#include <vector>

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

std::string run_x(const std::vector<int>& codes) {
  emulator::MK61 calc;
  calc.load_program(codes);
  calc.press_sequence({"В/О", "С/П"});
  calc.run_until_stable(300, 5);
  return compact(calc.read_register("x"));
}

} // namespace

void emulator_recall_side_effects_match_typescript_contract() {
  const std::vector<int> with_recall = {0x20, 0x35, 0x41, 0x61, 0x10, 0x50};
  const std::vector<int> without_recall = {0x20, 0x35, 0x41, 0x10, 0x50};

  require(run_x(with_recall) == "2,831852-1",
          "П->X immediately after X->П should lift the stack for following binary ops");
  require(run_x(without_recall) == "1,415926-1",
          "X->П without recall should keep the TS-observed stack behavior");

  const std::vector<int> x_only_with_recall = {0x20, 0x35, 0x41, 0x61,
                                               0x35, 0x10, 0x50};
  const std::vector<int> x_only_without_recall = {0x20, 0x35, 0x41, 0x35, 0x10, 0x50};

  require(run_x(x_only_with_recall) == "2,831852-1",
          "П->X stack lift should survive X-only ops before a later binary op");
  require(run_x(x_only_without_recall) == "1,415926-1",
          "X-only path without recall should preserve the TS-observed stack behavior");
}

} // namespace mkpro::tests
