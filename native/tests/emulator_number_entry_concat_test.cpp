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
  if (compiled.setup_program.has_value()) {
    std::vector<int> setup_codes;
    for (const ResolvedStep& step : compiled.setup_program->steps)
      setup_codes.push_back(step.opcode);
    require(calc.load_program(setup_codes).diagnostics.empty(),
            "number-entry-concat setup should load without diagnostics");
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(10000, 6).stopped,
            "number-entry-concat setup should stop");
  } else {
    for (const PreloadReport& preload : compiled.preloads) {
      require(!preload.setup_expression,
              "number-entry-concat setup expressions require an executable setup");
      calc.set_register(preload.register_name, preload.value);
    }
  }
  require(calc.load_program(step_opcodes(compiled)).diagnostics.empty(),
          "number-entry-concat program should load without diagnostics");
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
  // S/P does not finish manual numeric entry: without the otherwise dead
  // input store, the program's 5 is appended to the user's 6, producing 65.
  // Cover the generic show/read fusion, not a particular game's dispatch.
  const std::string fused_read = R"mkpro(program InputBoundary {
  state {
    screen: counter 0..9 = 0
  }
  loop {
    show(screen)
    command = read()
    command -= 5
    halt(command)
  }
}
)mkpro";
  for (const auto& [digits, expected] :
       std::vector<std::pair<std::vector<std::string>, std::string>>{
           {{"6"}, "1,"}, {{"1", "6"}, "11,"}, {{"6", ",", "5"}, "1,5"}}) {
    const RunTrace fused = run_with_input(fused_read, digits);
    require(fused.display == expected,
            "fused show/read should terminate manual input before a literal, got " +
                fused.display + "; " + fused.trace);
  }

  const RunTrace transformed = run_with_input(program("abs(x) - 5"), {"6"});
  require(transformed.display == "1,",
          "an arithmetic consumer should still terminate numeric input, got " +
              transformed.display + "; " + transformed.trace);

  const std::string reusable_constant = R"mkpro(program InputConstant {
  state {
    screen: counter 0..9 = 8
    key: counter 0..9 = 2
  }
  loop {
    show(screen)
    command = read()
    command -= 2
    show(command)
    halt(key)
  }
}
)mkpro";
  const CompileResult reused = compile_source(reusable_constant);
  require(std::any_of(reused.optimizations.begin(), reused.optimizations.end(),
                      [](const OptimizationReport& optimization) {
                        return optimization.name == "resumable-input-constant-reuse";
                      }),
          "a proved immutable initialized value should close input through a recall");
  const RunTrace reused_input = run_with_input(reusable_constant, {"6"});
  require(reused_input.display == "4,",
          "reused input constant should preserve subtraction, got " + reused_input.trace);

  std::string mutable_constant = reusable_constant;
  mutable_constant.insert(mutable_constant.find("    show(screen)"), "    key = 3\n");
  const CompileResult mutable_result = compile_source(mutable_constant);
  require(std::none_of(mutable_result.optimizations.begin(), mutable_result.optimizations.end(),
                       [](const OptimizationReport& optimization) {
                         return optimization.name == "resumable-input-constant-reuse";
                       }),
          "a state initializer must not substitute a literal after a possible write");
  const RunTrace mutable_input = run_with_input(mutable_constant, {"6"});
  require(mutable_input.display == "4,",
          "mutable state must not replace the requested constant, got " + mutable_input.trace);

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
