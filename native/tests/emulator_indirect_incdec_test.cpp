#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <array>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

constexpr std::array<const char*, 15> kDataRegisters = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e",
};

std::string compact(std::string value) {
  std::string out;
  for (const char ch : value) {
    if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r')
      out.push_back(ch);
  }
  return out;
}

std::string after_indirect_access(int register_index, const std::string& init) {
  emulator::MK61 calc;
  calc.load_program({0xd0 + register_index, 0x50});
  const std::string name = std::to_string(register_index);
  calc.set_register(name, init);
  calc.press_sequence({"В/О", "С/П"});
  calc.run_until_stable(100, 5);
  return compact(calc.read_register(name));
}

int stable_indirect_recall_value(const std::string& selector) {
  emulator::MK61 calc;
  calc.load_program({0xd7, 0x50});
  for (std::size_t index = 0; index < kDataRegisters.size(); ++index)
    calc.set_register(kDataRegisters[index], std::to_string(4000 + index));
  calc.set_register("7", selector);
  calc.press_sequence({"В/О", "С/П"});
  calc.run_until_stable(200, 5);
  return std::stoi(compact(calc.read_register("X")));
}

std::array<std::string, 5> predecrement_selector_trace(const std::string& selector) {
  emulator::MK61 calc;
  calc.load_program({0xd0, 0x4a, 0xd0, 0x4b, 0xd0, 0x4c, 0xd0, 0x4d, 0x50});
  for (int index = 4; index <= 7; ++index)
    calc.set_register(std::to_string(index), std::to_string(4000 + index));
  calc.set_register("0", selector);
  calc.press_sequence({"В/О", "С/П"});
  calc.run_until_stable(400, 5);
  return {
      compact(calc.read_register("0")), compact(calc.read_register("a")),
      compact(calc.read_register("b")), compact(calc.read_register("c")),
      compact(calc.read_register("d")),
  };
}

} // namespace

void emulator_indirect_incdec_facts_match_typescript_contract() {
  for (const int r : {0, 1, 2, 3})
    require(std::stoi(after_indirect_access(r, "5")) == 4,
            "R0..R3 should pre-decrement on indirect access");

  for (const int r : {4, 5, 6})
    require(std::stoi(after_indirect_access(r, "5")) == 6,
            "R4..R6 should pre-increment on indirect access");

  for (const int r : {7, 9})
    require(std::stoi(after_indirect_access(r, "5")) == 5,
            "R7..RE should remain unchanged on indirect access");

  // Stable registers still receive the transformed selector value back: a
  // negative selector is rewritten to its nine-padded form on first use, and
  // only the eight-digit form is a fixed point of that write-back.  This is
  // why a sign-toggled selector must carry an eight-digit magnitude to keep
  // its observable data value stable.
  for (const int r : {7, 8})
    require(std::stoi(after_indirect_access(r, "-18")) == -99999918,
            "stable registers should normalize negative selectors to the nine-padded form");
  for (const int r : {7, 8})
    require(std::stoi(after_indirect_access(r, "-99999918")) == -99999918,
            "the nine-padded negative form should be a write-back fixed point");

  for (const int r : {0, 1, 2, 3})
    require(std::stoi(after_indirect_access(r, "1")) == 0,
            "R0..R3 pre-decrement should reach zero from one");

  for (const int r : {0, 1, 2, 3})
    require(std::stoi(after_indirect_access(r, "0")) == -99999999,
            "R0..R3 pre-decrement should write the negative sentinel from zero");

  const std::array<std::string, 5> integral_selector =
      predecrement_selector_trace("8");
  const std::array<std::string, 5> logical_result_selector =
      predecrement_selector_trace("8.1234567");
  require(std::equal(integral_selector.begin() + 1, integral_selector.end(),
                     logical_result_selector.begin() + 1) &&
              std::stoi(logical_result_selector.front()) == 4,
          "a logical-result-shaped 8.HHHHHHH value in R0 should address R7..R4 exactly like "
          "the integer selector 8 while preserving only an irrelevant fractional residue");

  struct TargetCase {
    std::string selector;
    int target_index;
  };

  const std::vector<TargetCase> cases = {
      {"4", 4},    {"10", 10},  {"15", 0},  {"16", 0},  {"17", 1},
      {"23", 13},  {"99", 3},   {"123", 13}, {"-1", 11}, {"-123", 13},
  };

  for (const TargetCase& test_case : cases) {
    require(stable_indirect_recall_value(test_case.selector) == 4000 + test_case.target_index,
            "R7 selector should choose the same indirect memory target as TS");
  }
}

} // namespace mkpro::tests
