#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

constexpr int kIp1 = 0x61;
constexpr int kVup = 0x0e;
constexpr int kInt = 0x34;
constexpr int kFrac = 0x35;
constexpr int kXy = 0x14;
constexpr int kSt0 = 0x40;
constexpr int kAdd = 0x10;
constexpr int kDiv = 0x13;
constexpr int kMul = 0x12;
constexpr int kStop = 0x50;

const char* kSource = R"mkpro(program Split {
  state {
    hi: counter 0..999 = 0
    lo: counter 0..999 = 0
  }

  loop {
    x = read()
    hi = int(x / 4)
    lo = frac(x / 4)
    halt(hi)
    halt(lo)
  }
})mkpro";

std::string compact_display(std::string value) {
  value.erase(std::remove_if(value.begin(), value.end(),
                             [](unsigned char ch) { return std::isspace(ch) != 0; }),
              value.end());
  return value;
}

std::string display_of(const std::vector<int>& codes, const std::string& value) {
  emulator::MK61 calc;
  calc.set_register("1", value);
  const emulator::ProgramLoadResult loaded = calc.load_program(codes);
  require(loaded.diagnostics.empty(), "int/frac shared-tail probe should load");
  calc.press_sequence({"В/О", "С/П"});
  calc.run_until_stable(200, 4);
  return compact_display(calc.display_text());
}

std::string ref_int(const std::string& value) {
  return display_of({kIp1, kInt, kStop}, value);
}

std::string ref_frac(const std::string& value) {
  return display_of({kIp1, kFrac, kStop}, value);
}

struct SharedTail {
  std::string integer;
  std::string fraction;
};

SharedTail shared_tail(const std::string& value) {
  emulator::MK61 calc;
  calc.set_register("1", value);
  const emulator::ProgramLoadResult loaded =
      calc.load_program({kIp1, kVup, kInt, kSt0, kXy, kFrac, kStop});
  require(loaded.diagnostics.empty(), "int/frac shared-tail idiom should load");
  calc.press_sequence({"В/О", "С/П"});
  calc.run_until_stable(200, 4);
  return SharedTail{
      .integer = calc.read_register("0"),
      .fraction = compact_display(calc.display_text()),
  };
}

std::string one_based_modulo_4(const std::string& value) {
  return display_of({kIp1, kInt, 0x03, kAdd, 0x04, kDiv, kFrac, 0x04, kMul, 0x01,
                     kAdd, kStop},
                    value);
}

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) { return item.name == name; });
}

int count_opcode(const std::vector<ResolvedStep>& steps, int opcode) {
  return static_cast<int>(std::count_if(steps.begin(), steps.end(),
                                        [&](const ResolvedStep& step) {
                                          return step.opcode == opcode;
                                        }));
}

bool contains_opcode(const std::vector<ResolvedStep>& steps, int opcode) {
  return std::any_of(steps.begin(), steps.end(),
                     [&](const ResolvedStep& step) { return step.opcode == opcode; });
}

void require_equal(const std::string& actual, const std::string& expected,
                   const std::string& context) {
  require(actual == expected, context + " expected " + expected + ", got " + actual);
}

} // namespace

void emulator_int_frac_shared_tail_matches_typescript_contract() {
  {
    const CompileResult result = compile_source(kSource);
    require(result.implemented, "int/frac shared-tail program should compile");
    require(result.diagnostics.empty(),
            "int/frac shared-tail program should not report diagnostics");
    require(has_optimization(result, "int-frac-shared-tail"),
            "int/frac shared-tail program should report int-frac-shared-tail");
    require(count_opcode(result.steps, kDiv) == 1,
            "int/frac shared-tail program should evaluate the shared division once");
    require(contains_opcode(result.steps, kVup),
            "int/frac shared-tail program should duplicate the shared operand");
    require(contains_opcode(result.steps, kXy),
            "int/frac shared-tail program should restore the saved operand");
    require(contains_opcode(result.steps, kInt),
            "int/frac shared-tail program should emit integer part extraction");
    require(contains_opcode(result.steps, kFrac),
            "int/frac shared-tail program should emit fractional part extraction");
  }

  for (const std::string value : {"2.5", "-2.5", "0.25", "-0.25", "8", "-8"}) {
    const SharedTail tail = shared_tail(value);
    require_equal(tail.integer, ref_int(value), "shared int tail for " + value);
    require_equal(tail.fraction, ref_frac(value), "shared frac tail for " + value);
  }

  const std::vector<std::pair<std::string, std::string>> modulo_cases = {
      {"0", "4,"}, {"1", "1,"}, {"2", "2,"}, {"3", "3,"},
      {"4", "4,"}, {"5", "1,"}, {"8", "4,"}, {"9", "1,"},
  };
  for (const auto& [value, expected] : modulo_cases)
    require_equal(one_based_modulo_4(value), expected,
                  "one-based modulo for non-negative input " + value);
}

} // namespace mkpro::tests
