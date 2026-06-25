#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

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

std::vector<int> step_opcodes(const std::vector<ResolvedStep>& steps) {
  std::vector<int> codes;
  codes.reserve(steps.size());
  for (const ResolvedStep& step : steps)
    codes.push_back(step.opcode);
  return codes;
}

struct GuardRun {
  std::string display;
  std::size_t steps = 0;
  bool fired_domain_guard = false;
};

GuardRun run_guard(const std::string& op, int value) {
  const std::string source = R"mkpro(
program G {
  state { result: counter 0..99 = 0 }
  loop {
    x = read()
    if x )mkpro" + op + R"mkpro( 0 { halt("ЕГГОГ") }
    result = 7
    halt(result)
  }
}
)mkpro";

  CompileOptions options;
  options.budget = 999;
  options.analysis = true;
  options.domain_error_guards = true;
  const CompileResult compiled = compile_source(source, options);
  require(compiled.implemented, "domain-error guard program should compile");
  require(compiled.diagnostics.empty(), "domain-error guard program should not report diagnostics");

  emulator::MK61 calc;
  const emulator::ProgramLoadResult loaded = calc.load_program(step_opcodes(compiled.steps));
  require(loaded.diagnostics.empty(), "domain-error guard program should load");
  calc.press_sequence({"В/О", "С/П"});
  calc.input_number(std::to_string(std::abs(value)));
  if (value < 0)
    calc.press_sequence({"/-/"});
  calc.press_sequence({"С/П"});
  (void)calc.run_until_stable(400, 5);

  return GuardRun{
      .display = calc.display_text(),
      .steps = compiled.steps.size(),
      .fired_domain_guard = has_optimization(compiled, "domain-error-guard"),
  };
}

struct RangedUpperRun {
  std::string display;
  std::size_t steps = 0;
  bool fired_arc_trap = false;
};

RangedUpperRun run_ranged_upper_guard(int value) {
  const std::string source = R"mkpro(
program G {
  state {
    y: counter 0..5 = stack.X
    result: counter 0..99 = 0
  }

  loop {
    if y > 1 { halt("ЕГГОГ") }
    result = 7
    halt(result)
  }
}
)mkpro";

  CompileOptions options;
  options.budget = 999;
  options.analysis = true;
  options.domain_error_guards = true;
  const CompileResult compiled = compile_source(source, options);
  require(compiled.implemented, "ranged upper domain guard should compile");
  require(compiled.diagnostics.empty(), "ranged upper domain guard should not report diagnostics");

  emulator::MK61 calc;
  const emulator::ProgramLoadResult loaded = calc.load_program(step_opcodes(compiled.steps));
  require(loaded.diagnostics.empty(), "ranged upper domain guard should load");
  const auto reg = compiled.registers.find("y");
  require(reg != compiled.registers.end(), "ranged upper guard should allocate y");
  calc.set_register(reg->second, std::to_string(value));
  calc.press_sequence({"В/О", "С/П"});
  (void)calc.run_until_stable(400, 5);

  return RangedUpperRun{
      .display = calc.display_text(),
      .steps = compiled.steps.size(),
      .fired_arc_trap = std::any_of(compiled.steps.begin(), compiled.steps.end(),
                                    [](const ResolvedStep& step) {
                                      return step.opcode == 0x19;
                                    }),
  };
}

struct RangedLowerRun {
  std::string display;
  bool fired_arc_trap = false;
};

RangedLowerRun run_ranged_lower_guard(int value) {
  const std::string source = R"mkpro(
program G {
  state {
    y: counter -5..0 = stack.X
    result: counter 0..99 = 0
  }

  loop {
    if y < -1 { halt("ЕГГОГ") }
    result = 7
    halt(result)
  }
}
)mkpro";

  CompileOptions options;
  options.budget = 999;
  options.analysis = true;
  options.domain_error_guards = true;
  const CompileResult compiled = compile_source(source, options);
  require(compiled.implemented, "ranged lower domain guard should compile");
  require(compiled.diagnostics.empty(), "ranged lower domain guard should not report diagnostics");

  emulator::MK61 calc;
  const emulator::ProgramLoadResult loaded = calc.load_program(step_opcodes(compiled.steps));
  require(loaded.diagnostics.empty(), "ranged lower domain guard should load");
  const auto reg = compiled.registers.find("y");
  require(reg != compiled.registers.end(), "ranged lower guard should allocate y");
  calc.set_register(reg->second, std::to_string(value));
  calc.press_sequence({"В/О", "С/П"});
  (void)calc.run_until_stable(400, 5);

  return RangedLowerRun{
      .display = calc.display_text(),
      .fired_arc_trap = std::any_of(compiled.steps.begin(), compiled.steps.end(),
                                    [](const ResolvedStep& step) {
                                      return step.opcode == 0x19;
                                    }),
  };
}

bool is_error_stop(const std::string& display) {
  std::string upper = display;
  std::transform(upper.begin(), upper.end(), upper.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  return upper.find("ЕГГ") != std::string::npos;
}

void require_contains(const std::string& value, const std::string& needle,
                      const std::string& context) {
  require(compact_display(value).find(needle) != std::string::npos,
          context + " expected display containing " + needle + ", got " + value);
}

} // namespace

void emulator_domain_error_guard_matches_typescript_contract() {
  {
    const GuardRun positive = run_guard("<", 5);
    require(positive.fired_domain_guard, "x < 0 guard should report domain-error-guard");
    require(!is_error_stop(positive.display), "x < 0 positive input should not trap");
    require_contains(positive.display, "7", "x < 0 positive input");
    require(!is_error_stop(run_guard("<", 0).display),
            "x < 0 zero boundary should not trap");
    require(is_error_stop(run_guard("<", -4).display),
            "x < 0 negative input should trap");
  }

  {
    const GuardRun positive = run_guard("<=", 5);
    require(positive.fired_domain_guard, "x <= 0 guard should report domain-error-guard");
    require(!is_error_stop(positive.display), "x <= 0 positive input should not trap");
    require_contains(positive.display, "7", "x <= 0 positive input");
    require(is_error_stop(run_guard("<=", 0).display),
            "x <= 0 zero boundary should trap");
    require(is_error_stop(run_guard("<=", -4).display),
            "x <= 0 negative input should trap");
  }

  {
    const GuardRun positive = run_guard("==", 5);
    require(positive.fired_domain_guard, "x == 0 guard should report domain-error-guard");
    require(!is_error_stop(positive.display), "x == 0 positive input should not trap");
    require_contains(positive.display, "7", "x == 0 positive input");
    require(is_error_stop(run_guard("==", 0).display),
            "x == 0 zero boundary should trap");
    require(!is_error_stop(run_guard("==", -3).display),
            "x == 0 negative nonzero input should not trap");
  }

  {
    const RangedUpperRun zero = run_ranged_upper_guard(0);
    require(zero.fired_arc_trap, "x > 1 ranged upper guard should use F asin trap");
    require(!is_error_stop(zero.display), "x > 1 value 0 should not trap");
    require_contains(zero.display, "7", "x > 1 value 0");
    require(!is_error_stop(run_ranged_upper_guard(1).display),
            "x > 1 value 1 should not trap");
    require(is_error_stop(run_ranged_upper_guard(2).display),
            "x > 1 value 2 should trap");
    require(is_error_stop(run_ranged_upper_guard(5).display),
            "x > 1 value 5 should trap");
  }

  {
    const RangedLowerRun zero = run_ranged_lower_guard(0);
    require(zero.fired_arc_trap, "x < -1 ranged lower guard should use F asin trap");
    require(!is_error_stop(zero.display), "x < -1 value 0 should not trap");
    require_contains(zero.display, "7", "x < -1 value 0");
    require(!is_error_stop(run_ranged_lower_guard(-1).display),
            "x < -1 value -1 should not trap");
    require(is_error_stop(run_ranged_lower_guard(-2).display),
            "x < -1 value -2 should trap");
    require(is_error_stop(run_ranged_lower_guard(-5).display),
            "x < -1 value -5 should trap");
  }

  require(run_guard("<", 5).steps == 6,
          "one-cell domain opcode should replace compare + branch + shared trap");
}

} // namespace mkpro::tests
