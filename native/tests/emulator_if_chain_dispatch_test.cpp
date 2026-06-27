#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

const char* kSource = R"mkpro(program IfChainProbe {
  state {
    a: counter 0..9 = 0
    b: counter 0..9 = 0
    result: counter 0..99 = 0
  }
  loop {
    if a + b == 1 {
      result = 11
    }
    else {
      if a + b == 2 {
        result = 22
      }
      else {
        if a + b == 3 {
          result = 33
        }
        else {
          result = 99
        }
      }
    }
    halt(result)
  }
})mkpro";

const char* kNegativeSource = R"mkpro(program NegativeIfChainProbe {
  state {
    a: counter 0..9 = 0
    b: counter 0..9 = 0
    result: counter 0..99 = 0
  }
  loop {
    if a + b != 1 {
      if a + b != 2 {
        if a + b != 3 {
          result = 99
        }
        else {
          result = 33
        }
      }
      else {
        result = 22
      }
    }
    else {
      result = 11
    }
    halt(result)
  }
})mkpro";

std::vector<int> step_opcodes(const std::vector<ResolvedStep>& steps) {
  std::vector<int> codes;
  codes.reserve(steps.size());
  for (const ResolvedStep& step : steps)
    codes.push_back(step.opcode);
  return codes;
}

std::string compact_display(std::string value) {
  value.erase(std::remove_if(value.begin(), value.end(),
                             [](unsigned char ch) { return std::isspace(ch) != 0; }),
              value.end());
  return value;
}

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) { return item.name == name; });
}

CompileResult compile_if_chain_source(const char* source, const std::string& context) {
  CompileOptions options;
  options.budget = 999;
  // Pin the if-chain-dispatch-canonicalization structure this suite verifies;
  // the default-on aggressive post-layout indirect-flow rescue would otherwise
  // repack the dispatch and drop the canonicalization optimization report.
  options.disable_aggressive_post_layout = true;
  const CompileResult result = compile_source(source, options);
  require(result.implemented, context + " should compile");
  require(result.diagnostics.empty(), context + " should not report diagnostics");
  require(has_optimization(result, "if-chain-dispatch-canonicalization"),
          context + " should report if-chain-dispatch-canonicalization");
  return result;
}

int expected_result(int sum) {
  if (sum == 1)
    return 11;
  if (sum == 2)
    return 22;
  if (sum == 3)
    return 33;
  return 99;
}

int run_program(const CompileResult& result, int a, int b) {
  emulator::MK61 calc;
  const emulator::ProgramLoadResult loaded = calc.load_program(step_opcodes(result.steps));
  require(loaded.diagnostics.empty(), "if-chain dispatch program should load without diagnostics");
  for (const PreloadReport& preload : result.preloads) {
    if (!preload.counts_against_program)
      calc.set_register(preload.register_name, preload.value);
  }

  const auto a_register = result.registers.find("a");
  const auto b_register = result.registers.find("b");
  const auto result_register = result.registers.find("result");
  require(a_register != result.registers.end(), "if-chain dispatch should allocate a");
  require(b_register != result.registers.end(), "if-chain dispatch should allocate b");
  require(result_register != result.registers.end(), "if-chain dispatch should allocate result");

  calc.set_register(a_register->second, std::to_string(a));
  calc.set_register(b_register->second, std::to_string(b));
  calc.set_register(result_register->second, "0");
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult run = calc.run_until_stable(400, 5);
  require(run.stopped, "if-chain dispatch program should stop");

  std::string display = compact_display(calc.display_text());
  if (!display.empty() && display.back() == ',')
    display.pop_back();
  std::replace(display.begin(), display.end(), ',', '.');
  return static_cast<int>(std::lround(std::stod(display)));
}

void require_case(const CompileResult& result, int a, int b, const std::string& context) {
  const int actual = run_program(result, a, b);
  const int expected = expected_result(a + b);
  require(actual == expected, context + " for a=" + std::to_string(a) + ", b=" +
                                  std::to_string(b) + " expected " +
                                  std::to_string(expected) + ", got " +
                                  std::to_string(actual));
}

} // namespace

void emulator_if_chain_dispatch_matches_typescript_contract() {
  const CompileResult positive = compile_if_chain_source(kSource, "positive if-chain dispatch");
  const CompileResult negative =
      compile_if_chain_source(kNegativeSource, "negative if-chain dispatch");

  const std::vector<std::pair<int, int>> cases = {
      {0, 1}, {1, 1}, {2, 1}, {0, 0}, {4, 5}, {3, 0}, {0, 2}, {1, 2},
  };
  for (const auto& [a, b] : cases) {
    require_case(positive, a, b, "positive if-chain dispatch");
    require_case(negative, a, b, "negative if-chain dispatch");
  }
}

} // namespace mkpro::tests
