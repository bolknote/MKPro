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
constexpr int kDiv = 0x13;
constexpr int kInt = 0x34;
constexpr int kFrac = 0x35;
constexpr int kSign = 0x32;
constexpr int kAbs = 0x31;
constexpr int kXy = 0x14;
constexpr int kFReverse = 0x25;
constexpr int kSt0 = 0x40;
constexpr int kSt1 = 0x41;
constexpr int kSt2 = 0x42;
constexpr int kStop = 0x50;

const char* kSource = R"mkpro(program StackDerived {
  state {
    raw: packed = 0
    whole: packed = 0
    part: packed = 0
    signum: packed = 0
    magnitude: packed = 0
  }

  loop {
    raw = read()
    whole = int(raw / 10)
    part = frac(raw / 10)
    signum = sign(raw / 10)
    magnitude = abs(raw / 10)
    halt(whole)
    halt(part)
    halt(signum)
    halt(magnitude)
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
  require(loaded.diagnostics.empty(), "Z-stack derived tail probe should load");
  calc.press_sequence({"В/О", "С/П"});
  calc.run_until_stable(200, 4);
  return compact_display(calc.display_text());
}

std::string ref_unary(int opcode, const std::string& value) {
  return display_of({kIp1, opcode, kStop}, value);
}

struct SharedStackTail {
  std::string integer;
  std::string fraction;
  std::string sign;
  std::string absolute;
};

SharedStackTail shared_stack_tail(const std::string& value) {
  return SharedStackTail{
      .integer = display_of({kIp1, kVup, kVup, kVup, kInt, kStop}, value),
      .fraction = display_of({kIp1, kVup, kVup, kVup, kInt, kSt0, kXy, kFrac, kStop}, value),
      .sign = display_of({kIp1, kVup, kVup, kVup, kInt, kSt0, kXy, kFrac, kSt1,
                          kFReverse, kXy, kSign, kStop},
                         value),
      .absolute = display_of({kIp1, kVup, kVup, kVup, kInt, kSt0, kXy, kFrac, kSt1,
                              kFReverse, kXy, kSign, kSt2, kFReverse, kXy, kAbs,
                              kStop},
                             value),
  };
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

void emulator_z_stack_derived_tail_matches_typescript_contract() {
  {
    const CompileResult result = compile_source(kSource);
    require(result.implemented, "Z-stack derived tail program should compile");
    require(result.diagnostics.empty(), "Z-stack derived tail program should not report diagnostics");
    require(has_optimization(result, "z-stack-derived-value-reuse"),
            "Z-stack derived tail should report z-stack-derived-value-reuse");
    require(count_opcode(result.steps, kDiv) == 1,
            "Z-stack derived tail should evaluate the shared division once");
    require(count_opcode(result.steps, kVup) >= 3,
            "Z-stack derived tail should duplicate the shared operand");
    require(count_opcode(result.steps, kFReverse) >= 2,
            "Z-stack derived tail should rotate saved operands from Z");
    require(contains_opcode(result.steps, kXy),
            "Z-stack derived tail should restore saved operands with X/Y swap");
  }

  for (const std::string value : {"2.5", "-2.5", "0.25", "-0.25", "8", "-8"}) {
    const SharedStackTail tail = shared_stack_tail(value);
    require_equal(tail.integer, ref_unary(kInt, value),
                  "Z-stack derived int tail for " + value);
    require_equal(tail.fraction, ref_unary(kFrac, value),
                  "Z-stack derived frac tail for " + value);
    require_equal(tail.sign, ref_unary(kSign, value),
                  "Z-stack derived sign tail for " + value);
    require_equal(tail.absolute, ref_unary(kAbs, value),
                  "Z-stack derived abs tail for " + value);
  }
}

} // namespace mkpro::tests
