#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

constexpr int kIp1 = 0x61;
constexpr int kIp2 = 0x62;
constexpr int kIp3 = 0x63;
constexpr int kVup = 0x0e;
constexpr int kInt = 0x34;
constexpr int kFrac = 0x35;
constexpr int kXy = 0x14;
constexpr int kReverse = 0x25;
constexpr int kSt0 = 0x40;
constexpr int kSt1 = 0x41;
constexpr int kSt2 = 0x42;
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

struct DivmodResult {
  std::string sum;
  std::string left;
  std::string right;
};

DivmodResult run_divmod_program(const std::vector<int>& codes, const std::string& left,
                                const std::string& right, const std::string& divisor) {
  emulator::MK61 calc;
  calc.set_register("1", left);
  calc.set_register("2", right);
  calc.set_register("3", divisor);
  const emulator::ProgramLoadResult loaded = calc.load_program(codes);
  require(loaded.diagnostics.empty(), "divmod probe should load");
  calc.press_sequence({"В/О", "С/П"});
  calc.run_until_stable(400, 4);
  return DivmodResult{
      .sum = calc.read_register("0"),
      .left = calc.read_register("1"),
      .right = calc.read_register("2"),
  };
}

DivmodResult reference_divmod(const std::string& left, const std::string& right,
                              const std::string& divisor) {
  return run_divmod_program(
      {kIp1, kIp3, kDiv, kInt, kIp2, kIp3, kDiv, kInt, kAdd, kSt0,
       kIp1, kIp3, kDiv, kFrac, kIp3, kMul, kSt1,
       kIp2, kIp3, kDiv, kFrac, kIp3, kMul, kSt2, kStop},
      left, right, divisor);
}

DivmodResult fused_divmod(const std::string& left, const std::string& right,
                          const std::string& divisor) {
  return run_divmod_program(
      {kIp1, kIp3, kDiv, kVup, kFrac, kIp3, kMul, kSt1, kReverse, kInt,
       kIp2, kIp3, kDiv, kVup, kFrac, kIp3, kMul, kSt2, kReverse, kInt,
       kAdd, kSt0, kStop},
      left, right, divisor);
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

  const CompileResult divmod_pair = compile_source(R"mkpro(
program DivmodPair {
  state {
    left: packed = 1234
    right: packed = 5678
    quotient_sum: packed = 0
  }
  loop {
    quotient_sum = int(left / 1000) + int(right / 1000)
    left = left - int(left / 1000) * 1000
    right = right - int(right / 1000) * 1000
    halt(quotient_sum)
  }
}
)mkpro");
  require(divmod_pair.implemented, "divmod pair program should compile");
  require(divmod_pair.diagnostics.empty(), "divmod pair program should not report diagnostics");
  require(has_optimization(divmod_pair, "divmod-pair-fusion"),
          "matching quotient/remainder assignments should report divmod-pair-fusion");
  const int fused_divisions = count_opcode(divmod_pair.steps, kDiv);
  require(fused_divisions >= 1 && fused_divisions <= 2,
          "divmod pair fusion should emit at most one division body per pair");
  require(contains_opcode(divmod_pair.steps, kReverse),
          "divmod pair fusion should restore each saved quotient through F reverse");

  const CompileResult mismatched_divmod = compile_source(R"mkpro(
program MismatchedDivmod {
  state {
    left: packed = 1234
    right: packed = 5678
    quotient_sum: packed = 0
  }
  loop {
    quotient_sum = int(left / 1000) + int(right / 1000)
    left = left - int(left / 100) * 100
    right = right - int(right / 1000) * 1000
    halt(quotient_sum)
  }
}
)mkpro");
  require(mismatched_divmod.implemented, "mismatched divmod program should compile");
  require(mismatched_divmod.diagnostics.empty(),
          "mismatched divmod program should not report diagnostics");
  require(!has_optimization(mismatched_divmod, "divmod-pair-fusion"),
          "different quotient and remainder divisors must reject divmod-pair-fusion");

  const std::vector<std::array<std::string, 3>> divmod_cases = {
      {"1234", "5678", "1000"},
      {"99999999", "1", "1000"},
      {"-1234", "5678", "1000"},
      {"1234.5", "5678.5", "1000"},
  };
  for (const auto& values : divmod_cases) {
    const DivmodResult expected = reference_divmod(values[0], values[1], values[2]);
    const DivmodResult actual = fused_divmod(values[0], values[1], values[2]);
    require_equal(actual.sum, expected.sum, "fused divmod quotient sum for " + values[0]);
    require_equal(actual.left, expected.left, "fused divmod left remainder for " + values[0]);
    require_equal(actual.right, expected.right,
                  "fused divmod right remainder for " + values[1]);
  }
}

} // namespace mkpro::tests
