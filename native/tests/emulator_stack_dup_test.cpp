#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

const char* kSource = R"mkpro(program RepeatedQuotient {
  state {
    result: counter 0..99 = 0
  }

  loop {
    x = read()
    y = read()
    result = (x + y) / (x + y)
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

std::string compact_display(std::string text) {
  text.erase(std::remove_if(text.begin(), text.end(),
                            [](unsigned char ch) { return std::isspace(ch) != 0; }),
             text.end());
  return text;
}

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& optimization) {
                       return optimization.name == name;
                     });
}

std::size_t count_opcode(const CompileResult& result, int opcode) {
  return static_cast<std::size_t>(
      std::count_if(result.steps.begin(), result.steps.end(),
                    [opcode](const ResolvedStep& step) { return step.opcode == opcode; }));
}

} // namespace

void emulator_stack_dup_equivalence_matches_typescript_contract() {
  const CompileResult result = compile_source(kSource);
  require(result.implemented, "stack-dup source should compile");
  require(result.diagnostics.empty(), "stack-dup source should compile without diagnostics");
  require(has_optimization(result, "stack-current-x-scheduling"),
          "stack-dup source should report stack-current-x-scheduling");
  require(count_opcode(result, 0x0e) >= 1,
          "stack-dup source should emit at least one stack lift");
  require(count_opcode(result, 0x10) == 1,
          "stack-dup source should compute x+y exactly once");

  emulator::MK61 calc;
  const emulator::ProgramLoadResult loaded = calc.load_program(step_opcodes(result.steps));
  require(loaded.diagnostics.empty(), "stack-dup program should load without diagnostics");
  for (const PreloadReport& preload : result.preloads)
    calc.set_register(preload.register_name, preload.value);

  emulator::RunResult run;
  for (const std::string& key : {"В/О", "С/П", "3", "С/П", "4", "С/П"}) {
    calc.press_sequence({key});
    run = calc.run_until_stable(400, 3);
  }
  require(run.stopped, "stack-dup program should stop after both inputs");
  const std::string display = compact_display(calc.display_text());
  require(display.find("1") != std::string::npos,
          "stack-dup program should compute (3+4)/(3+4)=1, got " + display);
}

} // namespace mkpro::tests
