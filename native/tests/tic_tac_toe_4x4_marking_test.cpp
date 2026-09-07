#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace mkpro::tests {
namespace {

void require_marking(bool condition, const std::string& message) {
  if (!condition)
    throw std::runtime_error("4x4 marking contract: " + message);
}

std::string source_mark_one() {
  const auto path = std::filesystem::path(__FILE__).parent_path() /
                    "../../examples/pending-optimizer/tic-tac-toe-4x4.mkpro";
  std::ifstream input(path);
  require_marking(input.good(), "cannot open the high-level source");
  const std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
  const auto start = source.find("  fn mark_one() {");
  require_marking(start != std::string::npos, "mark_one is missing");
  const auto end = source.find("  fn mark_lines_and_check(", start);
  require_marking(end != std::string::npos, "mark_one boundary is missing");
  return source.substr(start, end - start);
}

std::string run_marking_source(const std::string& function, const std::string& index,
                               int sign, const std::string& value,
                               bool expect_computed_coefficient = true) {
  const std::string source =
      "program MarkingContract {\n"
      "  state {\n"
      "    lines: packed[4..7] = [" + value + ", " + value + ", " + value +
      ", " + value + "]\n"
      "    slot: packed = 8\n"
      "    line: packed = " + index + "\n"
      "    best_score: packed = " + std::to_string(sign) + "\n"
      "    report: packed\n"
      "    display_x: packed = 0\n"
      "  }\n"
      "  mark_one()\n"
      "  halt(lines[7])\n" + function + "}\n";
  CompileOptions options;
  options.analysis = true;
  options.budget = 105;
  options.disable_candidate_search = true;
  const auto result = compile_source(source, options);
  require_marking(result.implemented && result.diagnostics.empty(),
                  "the actual marking function did not compile");
  require_marking(result.steps.size() <= 105U, "marking fixture exceeds hardware memory");
  const bool computed_coefficient = std::any_of(
      result.optimizations.begin(), result.optimizations.end(), [](const auto& optimization) {
        return optimization.name == "stack-carried-pow10-computed-coefficient";
      });
  require_marking(computed_coefficient == expect_computed_coefficient,
                  "unexpected computed-coefficient lowering selection");
  std::vector<int> codes;
  for (const auto& step : result.steps)
    codes.push_back(step.opcode);
  emulator::MK61 calculator;
  require_marking(calculator.load_program(codes).diagnostics.empty(), "fixture load failed");
  for (const auto& preload : result.preloads)
    calculator.set_register(preload.register_name, preload.value);
  const std::array<std::string, 3> untouched_banks = {
      calculator.read_register("4"), calculator.read_register("5"),
      calculator.read_register("6")};
  calculator.press_sequence({"В/О", "С/П"});
  require_marking(calculator.run_until_stable(10000, 8).stopped, "fixture did not stop");
  for (std::size_t bank = 0; bank < untouched_banks.size(); ++bank) {
    require_marking(calculator.read_register(std::to_string(bank + 4U)) == untouched_banks[bank],
                    "an unrelated array bank was modified");
  }
  return calculator.read_register("7");
}

std::string run_original_marking(const std::string& index, int sign,
                                  const std::string& value) {
  emulator::MK61 calculator;
  // Original cells 88..91: pow10(line), recall the signed hexadecimal
  // coefficient, multiply, add to the packed line bank. The fixture uses
  // a direct store because the destination is fixed to bank 7 here.
  require_marking(calculator.load_program({0x67, 0x61, 0x15, 0x6a, 0x12, 0x10,
                                          0x47, 0x50}).diagnostics.empty(),
                  "reference load failed");
  calculator.set_register("7", value)
      .set_register("1", index)
      .set_register("a", sign < 0 ? "-ГE-2" : "ГE-2");
  calculator.press_sequence({"В/О", "С/П"});
  require_marking(calculator.run_until_stable(10000, 8).stopped,
                  "reference did not stop");
  return calculator.read_register("7");
}

} // namespace

void tic_tac_toe_4x4_source_marking_matches_original_listing() {
  const std::string function = source_mark_one();
  // Fractional coordinates also pin operation order and eight-digit
  // arithmetic; replacing pow10(line) / 10 with pow10(line - 1) is not
  // assumed to be an exact calculator identity.
  const std::array<std::string, 8> indices = {
      "1", "2", "3", "4", "0.4", "1.2", "2.8", "3.6"};
  // A zero bank keeps low-order rounding differences visible instead of
  // absorbing them in the much larger normal game initialization.
  for (const std::string& value : {"44444.4", "0"}) {
    for (const int sign : {-1, 1}) {
      for (const std::string& index : indices) {
        const std::string expected = run_original_marking(index, sign, value);
        const std::string actual = run_marking_source(function, index, sign, value);
        require_marking(actual == expected,
                        "bank=" + value + ", line=" + index +
                        ", sign=" + std::to_string(sign) +
                        ": expected " + expected + ", got " + actual);
      }
    }
  }
  // Independent negative cases for the generic compiler rule. The first
  // coefficient reads the carried index a second time; the second needs
  // three temporary slots and would evict the old array element. Both must
  // retain the ordinary expression lowering, not take the stack-only path.
  for (const std::string& coefficient : {
           "best_score / line", "best_score / (slot + line)",
           "best_score / (slot + 1)", "best_score + random()"}) {
    const std::string independent =
        "  fn mark_one() {\n"
        "    slot--\n"
        "    lines[slot] = packed_add(lines[slot], line, " + coefficient + ")\n"
        "  }\n";
    (void)run_marking_source(independent, "2", 1, "44444.4", false);
  }
  for (const std::string& coefficient : {
           "(best_score + 2) / 10", "-(best_score / 10)"}) {
    const std::string independent =
        "  fn mark_one() {\n"
        "    slot--\n"
        "    lines[slot] = packed_add(lines[slot], line, " + coefficient + ")\n"
        "  }\n";
    require_marking(run_marking_source(independent, "3", -1, "44444.4") ==
                        run_original_marking("3", 1, "44444.4"),
                    "compound coefficient did not preserve arithmetic association");
  }
}

} // namespace mkpro::tests
