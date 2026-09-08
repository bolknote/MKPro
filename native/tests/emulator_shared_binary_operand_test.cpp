#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace mkpro::tests {
namespace {

std::vector<int> operand_codes(const std::vector<ResolvedStep>& steps) {
  std::vector<int> result;
  for (const auto& step : steps)
    result.push_back(step.opcode);
  return result;
}

void operand_stop(emulator::MK61& calculator) {
  require(calculator.run_until_stable(3000, 8).stopped,
          "binary operand fixture must reach its next stop");
}

void operand_load(emulator::MK61& calculator, const std::vector<int>& bytes) {
  require(calculator.load_program(bytes).diagnostics.empty(),
          "binary operand fixture must load without truncation");
}

void seed_operand_stack(emulator::MK61& calculator, const std::string& value) {
  calculator.input_number("7", true).press("В↑").input_number("5", true).press("+");
  for (const std::string seed : {"14", "13", "12", "11"})
    calculator.input_number(seed, true).press("В↑");
  calculator.input_number(value, true);
}

std::vector<std::string> operand_observation(emulator::MK61& calculator) {
  std::vector<std::string> result;
  for (const std::string reg : {"X", "Y", "Z", "T", "X1"})
    result.push_back(calculator.read_register(reg));
  calculator.press(".");
  result.push_back(calculator.display_text());
  return result;
}

} // namespace

void emulator_binary_call_shared_operand_matches_rom() {
  struct Operation {
    std::string name;
    int opcode;
  };
  const std::array<Operation, 4> operations{{
      {"bit_and", 0x37}, {"bit_or", 0x38}, {"bit_xor", 0x39}, {"max", 0x36},
  }};
  struct Fixture {
    std::string prefix;
    std::string right;
    std::vector<int> reference;
    std::string other = "1.234567";
    bool shared = true;
  };
  const std::vector<Fixture> fixtures{
      {"", "sin(value)", {0x0e, 0x1c}},
      {"", "abs(sin(value))", {0x0e, 0x1c, 0x31}},
      {"alias = value\n", "sin(alias)", {0x0e, 0x1c}},
      {"value = bit_and(value, 7.7777777)\n", "sin(value)",
       {0x61, 0x37, 0x0e, 0x1c}, "7.7777777"},
      {"", "sin(other)", {0x61, 0x1c}, "1.234567", false},
      {"", "value", {0x0e}},
  };
  for (const auto& operation : operations) {
    for (std::size_t fixture_index = 0; fixture_index < fixtures.size(); ++fixture_index) {
      const auto& fixture = fixtures.at(fixture_index);
      const std::string source =
          "program SharedOperand {\n state {\n value: packed = 0\n alias: packed = 0\n"
          " other: packed = " + fixture.other + "\n }\n value = read()\n" +
          fixture.prefix + " halt(" + operation.name + "(value, " + fixture.right + "))\n}\n";
      const auto compiled = compile_source(source);
      require(compiled.implemented && compiled.diagnostics.empty(),
              "shared binary operand source must compile: " + operation.name);
      const auto main = operand_codes(compiled.steps);
      if (fixture_index == 0U)
        require(main.size() <= 5U && std::count(main.begin(), main.end(), 0x0e) == 1,
                "a shared unary operand needs one duplicate, not a materialized store/recall pair");
      if (!fixture.shared)
        require(std::count(main.begin(), main.end(), 0x0e) == 0,
                "independent operands must not gain an unnecessary duplicate");

      auto reference = fixture.reference;
      reference.push_back(operation.opcode);
      reference.push_back(0x50);
      for (const std::string value : {"0", "1", "2", "3", "4", "7", "8", "9",
                                     "0.1234567", "7.7777777", "8.1234567",
                                     "8.7654321", "-3"}) {
        emulator::MK61 actual({.angle_mode = "grad"});
        emulator::MK61 expected({.angle_mode = "grad"});
        if (compiled.setup_program.has_value()) {
          operand_load(actual, operand_codes(compiled.setup_program->steps));
          actual.press_sequence({"В/О", "С/П"});
          operand_stop(actual);
        } else {
          for (const auto& preload : compiled.preloads)
            actual.set_register(preload.register_name, preload.value);
        }
        operand_load(actual, main);
        actual.press_sequence({"В/О", "С/П"});
        operand_stop(actual);
        seed_operand_stack(actual, value);
        actual.press("С/П");
        operand_stop(actual);

        operand_load(expected, reference);
        expected.set_register("1", fixture.other);
        expected.press("В/О");
        seed_operand_stack(expected, value);
        expected.press("С/П");
        operand_stop(expected);
        const auto observed = operand_observation(actual);
        auto wanted = operand_observation(expected);
        if (fixture_index == 2U && observed != wanted) {
          // Commutative calls may compute the aliased unary operand first.
          // Pin that complete ROM trace too, rather than prescribing one
          // argument order or ignoring its Y/X1/X2 effects.
          emulator::MK61 reordered({.angle_mode = "grad"});
          operand_load(reordered, {0x41, 0x1c, 0x61, operation.opcode, 0x50});
          reordered.press("В/О");
          seed_operand_stack(reordered, value);
          reordered.press("С/П");
          operand_stop(reordered);
          wanted = operand_observation(reordered);
        }
        std::string differences;
        const std::array<std::string, 6> names{"X", "Y", "Z", "T", "X1", "X2 probe"};
        for (std::size_t slot = 0; slot < names.size(); ++slot) {
          if (observed.at(slot) != wanted.at(slot))
            differences += "\n" + names.at(slot) + ": actual=" + observed.at(slot) +
                           ", expected=" + wanted.at(slot);
        }
        require(observed == wanted,
                "binary operand must preserve result, Y/Z/T, X1 and X2: operation=" +
                    operation.name + ", fixture=" + std::to_string(fixture_index) +
                    ", input=" + value + differences + "\n" + compiled.listing);
      }
    }
  }
}

} // namespace mkpro::tests
