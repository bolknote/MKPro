#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

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

int display_integer(const emulator::MK61& calc) {
  std::string text = compact_display(calc.display_text());
  std::replace(text.begin(), text.end(), ',', '.');
  while (!text.empty() && text.back() == '.')
    text.pop_back();
  return std::stoi(text);
}

int run_with_input(const std::string& source, int value) {
  CompileOptions options;
  options.budget = 999;
  const CompileResult result = compile_source(source, options);
  require(result.implemented, "function equivalence program should compile");
  for (const Diagnostic& diagnostic : result.diagnostics) {
    require(diagnostic.severity != DiagnosticSeverity::Error,
            "function equivalence program should not report errors: " + diagnostic.message);
  }

  emulator::MK61 calc;
  for (const PreloadReport& preload : result.preloads)
    calc.set_register(preload.register_name, preload.value);
  const emulator::ProgramLoadResult loaded = calc.load_program(step_opcodes(result.steps));
  require(loaded.diagnostics.empty(), "function equivalence program should load");
  calc.press_sequence({"В/О", "С/П"});
  calc.input_number(std::to_string(value));
  calc.press_sequence({"С/П"});
  const emulator::RunResult run = calc.run_until_stable(400, 5);
  require(run.stopped, "function equivalence program should stop");
  return display_integer(calc);
}

void require_result(const std::string& source, int input, int expected,
                    const std::string& context) {
  const int actual = run_with_input(source, input);
  require(actual == expected,
          context + " expected " + std::to_string(expected) + ", got " +
              std::to_string(actual));
}

} // namespace

void emulator_function_equivalence_matches_typescript_contract() {
  {
    const std::string source = R"mkpro(
program Double {
  state {
    result: counter 0..99 = 0
  }
  fn double(n) {
    return n + n
  }
  loop {
    x = read()
    result = double(x)
    halt(result)
  }
}
)mkpro";
    require_result(source, 3, 6, "single function call should return 3 + 3");
    require_result(source, 9, 18, "single function call should return 9 + 9");
  }

  {
    const std::string source = R"mkpro(
program Nested {
  state {
    result: counter 0..99 = 0
  }
  fn inc(n) {
    return n + 1
  }
  fn dbl(n) {
    return n + n
  }
  loop {
    x = read()
    result = dbl(inc(x))
    halt(result)
  }
}
)mkpro";
    require_result(source, 3, 8, "nested function calls should compute dbl(inc(3))");
    require_result(source, 10, 22, "nested function calls should compute dbl(inc(10))");
  }

  {
    const std::string source = R"mkpro(
program Mixed {
  state {
    result: counter 0..99 = 0
  }
  fn triple(n) {
    return n + n + n
  }
  loop {
    x = read()
    result = 1 + triple(x)
    halt(result)
  }
}
)mkpro";
    require_result(source, 2, 7, "function call in expression should compute 1 + triple(2)");
    require_result(source, 5, 16, "function call in expression should compute 1 + triple(5)");
  }

  {
    const std::string source = R"mkpro(
program Sign {
  state {
    result: counter 0..99 = 0
  }
  fn sign(n) {
    if n < 1 {
      return 0
    }
    else {
      return 1
    }
  }
  loop {
    x = read()
    result = sign(x)
    halt(result)
  }
}
)mkpro";
    require_result(source, 0, 0, "early return should take the low branch");
    require_result(source, 5, 1, "early return should take the high branch");
  }

  {
    const std::string source = R"mkpro(
program TailCount {
  state {
    result: counter 0..99 = 0
  }
  fn count_down(n, acc) {
    if n <= 0 {
      return acc
    }
    else {
      return count_down(n - 1, acc + 1)
    }
  }
  loop {
    x = read()
    result = count_down(x, 0)
    halt(result)
  }
}
)mkpro";
    require_result(source, 3, 3, "direct tail recursion should count down 3");
    require_result(source, 7, 7, "direct tail recursion should count down 7");
  }

  {
    const std::string source = R"mkpro(
program TailMutual {
  state {
    result: counter 0..99 = 0
  }
  fn even(n) {
    if n <= 0 {
      return 1
    }
    else {
      return odd(n - 1)
    }
  }
  fn odd(n) {
    if n <= 0 {
      return 0
    }
    else {
      return even(n - 1)
    }
  }
  loop {
    x = read()
    result = even(x)
    halt(result)
  }
}
)mkpro";
    require_result(source, 6, 1, "mutual tail recursion should classify 6 as even");
    require_result(source, 7, 0, "mutual tail recursion should classify 7 as odd");
  }

  {
    const std::string source = R"mkpro(
program TailFib {
  state {
    result: counter 0..99 = 0
  }
  fn fib_step(n, a, b) {
    if n <= 0 {
      return a
    }
    else {
      return fib_step(n - 1, b, a + b)
    }
  }
  loop {
    x = read()
    result = fib_step(x, 0, 1)
    halt(result)
  }
}
)mkpro";
    require_result(source, 6, 8,
                   "tail-recursive arguments should be evaluated before rebinding for fib(6)");
    require_result(source, 7, 13,
                   "tail-recursive arguments should be evaluated before rebinding for fib(7)");
  }
}

} // namespace mkpro::tests
