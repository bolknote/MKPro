#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

const char* kProcedureStateSource = R"mkpro(program ProcedureStateStore {
  state {
    value: counter 0..9 = 0
  }

  loop {
    update()
    update()
    halt(value)
  }

  fn update() {
    value = 7
    show(value)
  }
})mkpro";

std::vector<int> procedure_state_opcodes(const CompileResult& compiled) {
  std::vector<int> opcodes;
  opcodes.reserve(compiled.steps.size());
  for (const ResolvedStep& step : compiled.steps)
    opcodes.push_back(step.opcode);
  return opcodes;
}

int register_index(const std::string& name) {
  if (name.size() != 1U)
    return -1;
  const unsigned char ch = static_cast<unsigned char>(name.front());
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  const int lower = std::tolower(ch);
  return lower >= 'a' && lower <= 'f' ? 10 + lower - 'a' : -1;
}

std::string compact_procedure_state_display(std::string text) {
  text.erase(std::remove_if(text.begin(), text.end(),
                            [](unsigned char ch) { return std::isspace(ch) != 0; }),
             text.end());
  return text;
}

} // namespace

void emulator_procedure_stack_carried_assignment_persists_state() {
  CompileOptions options;
  options.disable_candidate_search = true;
  const CompileResult compiled = compile_source(kProcedureStateSource, options);
  require(compiled.implemented && compiled.diagnostics.empty(),
          "procedure-state source should compile without diagnostics");

  const int value_index = register_index(compiled.registers.at("value"));
  require(value_index >= 0, "procedure-state value should have a physical register");
  require(std::any_of(compiled.steps.begin(), compiled.steps.end(),
                      [value_index](const ResolvedStep& step) {
                        return step.opcode == 0x40 + value_index;
                      }),
          "procedure assignment consumed by show must still store persistent state");

  emulator::MK61 calculator;
  const emulator::ProgramLoadResult loaded =
      calculator.load_program(procedure_state_opcodes(compiled));
  require(loaded.diagnostics.empty(), "procedure-state program should load without truncation");
  calculator.set_register(compiled.registers.at("value"), "0");

  calculator.press_sequence({"В/О", "С/П"});
  require(calculator.run_until_stable(2000, 5).stopped,
          "procedure-state program should stop at its first show");
  calculator.press("С/П");
  require(calculator.run_until_stable(2000, 5).stopped,
          "procedure-state program should stop at its second show");
  calculator.press("С/П");
  require(calculator.run_until_stable(2000, 5).stopped,
          "procedure-state program should stop at its final halt");
  const std::string display = compact_procedure_state_display(calculator.display_text());
  require(display.find("7") != std::string::npos,
          "state written inside a procedure should remain 7 after return, got " + display);
}

} // namespace mkpro::tests
