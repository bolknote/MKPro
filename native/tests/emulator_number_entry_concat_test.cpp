#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::string program(const std::string& body) {
  return R"mkpro(program P {
  state {
    r: counter 0..9999 = 0
  }

  loop {
    x = read()
    r = )mkpro" + body + R"mkpro(
    halt(r)
  }
}
)mkpro";
}

std::string compact(std::string value) {
  std::string out;
  for (const char ch : value) {
    if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r')
      out.push_back(ch);
  }
  return out;
}

std::vector<int> step_opcodes(const CompileResult& result) {
  std::vector<int> codes;
  codes.reserve(result.steps.size());
  for (const ResolvedStep& step : result.steps)
    codes.push_back(step.opcode);
  return codes;
}

struct RunTrace {
  std::string display;
  std::string trace;
};

std::string format_steps(const CompileResult& result) {
  std::ostringstream out;
  const std::size_t limit = std::min<std::size_t>(result.steps.size(), 28U);
  for (std::size_t index = 0; index < limit; ++index) {
    const ResolvedStep& step = result.steps.at(index);
    out << step.hex << ":" << step.mnemonic;
    if (step.comment.has_value())
      out << "(" << *step.comment << ")";
    if (index + 1U < limit)
      out << " ";
  }
  return out.str();
}

RunTrace run_with_input(const std::string& source, const std::vector<std::string>& input_digits) {
  const CompileResult compiled = compile_source(source);
  require(compiled.implemented, "number-entry-concat source should compile");
  require(compiled.diagnostics.empty(), "number-entry-concat source should compile without diagnostics");

  emulator::MK61 calc;
  calc.load_program(step_opcodes(compiled));
  std::ostringstream trace;
  trace << "steps: " << format_steps(compiled);

  std::vector<std::string> keys = {"В/О", "С/П"};
  keys.insert(keys.end(), input_digits.begin(), input_digits.end());
  keys.push_back("С/П");

  for (const std::string& key : keys) {
    calc.press_sequence({key});
    calc.run_until_stable(400, 3);
    trace << " | " << key << "=>" << compact(calc.display_text()) << "@"
          << calc.program_counter();
  }
  return RunTrace{.display = compact(calc.display_text()), .trace = trace.str()};
}

} // namespace

void emulator_number_entry_concat_matches_typescript_contract() {
  const RunTrace multiply_plus = run_with_input(program("x * 3 + 1"), {"1", "0"});
  require(multiply_plus.display == "31,",
          "x * 3 + 1 should keep the two literals separate, got " +
              multiply_plus.display + "; " + multiply_plus.trace);

  const RunTrace divide = run_with_input(program("x / 4"), {"1", "0"});
  require(divide.display.find("2,5") != std::string::npos,
          "x / 4 should finalize read input before the literal, got " + divide.display +
              "; " + divide.trace);

  const RunTrace distributed = run_with_input(program("2 * (2 + x)"), {"5"});
  require(distributed.display == "14,",
          "2 * (2 + x) should distribute without merging 2 and 4, got " +
              distributed.display + "; " + distributed.trace);

  const RunTrace linear = run_with_input(program("(x + 2) * 3"), {"4"});
  require(linear.display == "18,",
          "(x + 2) * 3 should stay correct with two constants in the linear form, got " +
              linear.display + "; " + linear.trace);
}

} // namespace mkpro::tests
