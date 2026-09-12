#include "mkpro/compiler.hpp"
#include "mkpro/core/parser.hpp"
#include "mkpro/core/v2_const.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::string program(const std::string& function_name) {
  return R"mkpro(
program SafeMinMax {
  state {
    a: counter -99..99 = 0
    b: counter -99..99 = 0
    out: packed = 0
  }
  loop {
    out = )mkpro" + function_name +
         R"mkpro((a, b)
    halt(out)
  }
}
)mkpro";
}

bool has_opcode(const CompileResult& result, int opcode) {
  return std::any_of(result.steps.begin(), result.steps.end(),
                     [&](const ResolvedStep& step) { return step.opcode == opcode; });
}

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) { return item.name == name; });
}

void require_no_errors(const CompileResult& result, const std::string& label) {
  for (const Diagnostic& diagnostic : result.diagnostics) {
    require(diagnostic.severity != DiagnosticSeverity::Error,
            label + " should compile without errors: " + diagnostic.message);
  }
  require(result.implemented, label + " should be implemented");
}

std::optional<double> folded_value(const std::string& text) {
  const Expression expression = parse_expression(text, 1);
  return core::numeric_value_of_expression(expression, std::map<std::string, Expression>{});
}

void require_folded(const std::string& text, double expected) {
  const std::optional<double> value = folded_value(text);
  require(value.has_value(), text + " should fold as a constant expression");
  require(std::fabs(*value - expected) < 1e-12, text + " folded to the wrong value");
}

void require_rom_integer(const CompileResult& compiled,
                         const std::map<std::string, int>& inputs, int expected,
                         const std::string& label, const std::string& mode = "grad") {
  require_no_errors(compiled, label);
  require(compiled.steps.size() <= 105U, label + " must fit real program memory");
  std::vector<int> code;
  for (const ResolvedStep& step : compiled.steps)
    code.push_back(step.opcode);
  emulator::MK61 calc({.angle_mode = mode});
  require(calc.load_program(code).diagnostics.empty(), label + " must load without truncation");
  for (const PreloadReport& preload : compiled.preloads)
    calc.set_register(preload.register_name, preload.value);
  for (const auto& [name, value] : inputs)
    calc.set_register(compiled.registers.at(name), std::to_string(value));
  calc.press_sequence({"В/О", "БП", "0", "0", "С/П"});
  require(calc.run_until_stable(300, 6).stopped, label + " must stop");
  require(std::stod(calc.display_text()) == expected,
          label + ": expected " + std::to_string(expected) + ", got " + calc.display_text());
}

} // namespace

void safe_minmax_matches_typescript_contract() {
  const CompileResult safe_max = compile_source(program("safe_max"));
  require_no_errors(safe_max, "safe_max");
  require(!has_opcode(safe_max, 0x36), "safe_max should not emit К max (0x36)");
  require(has_optimization(safe_max, "quirk-free-minmax-lowering"),
          "safe_max should report quirk-free-minmax-lowering");

  const CompileResult safe_min = compile_source(program("safe_min"));
  require_no_errors(safe_min, "safe_min");
  require(!has_opcode(safe_min, 0x36), "safe_min should not emit К max (0x36)");

  const CompileResult plain_max = compile_source(program("max"));
  require_no_errors(plain_max, "plain max");
  require(has_opcode(plain_max, 0x36), "plain max should still lower through К max (0x36)");

  const CompileResult impure = compile_source(R"mkpro(
program P {
  state { out: packed = 0 }
  loop {
    out = safe_max(random(), 5)
    halt(out)
  }
}
)mkpro");
  require(!impure.implemented, "safe_max(random(), 5) should be rejected");
  require(std::any_of(impure.diagnostics.begin(), impure.diagnostics.end(),
                      [](const Diagnostic& diagnostic) {
                        return diagnostic.severity == DiagnosticSeverity::Error &&
                               diagnostic.message.find("duplicable operands") != std::string::npos;
                      }),
          "safe_max(random(), 5) should explain the duplicable operand requirement");

  require_folded("safe_max(5, 0)", 5);
  require_folded("safe_max(0, 5)", 5);
  require_folded("safe_max(-5, 0)", 0);
  require_folded("safe_min(5, 0)", 0);
  require_folded("safe_min(-5, 0)", -5);
  require_folded("safe_min(3, 7)", 3);
  require_folded("max(5, 0)", 0);

  // Compiler-created min/max must preserve comparison semantics, unlike an
  // explicit max() call, whose hardware zero quirk is part of the language.
  CompileOptions options;
  options.disable_candidate_search = true;
  for (const bool bounded : {false, true}) {
    for (const bool minimum : {false, true}) {
      const std::string type = bounded ? "counter 1..9" : "packed";
      const std::string label = std::string(bounded ? "nonzero " : "unknown ") +
                                (minimum ? "minimum" : "maximum");
      const CompileResult conditional = compile_source(
          "program Conditional {\n  state {\n    left: " + type +
          " = 3\n    right: " + type + " = 5\n    out: packed = 0\n  }\n" +
          "  if left " + (minimum ? "<" : ">") + " right { out = left }\n" +
          "  else { out = right }\n  halt(out)\n}\n", options);
      require_no_errors(conditional, label);
      if (bounded)
        require(has_optimization(conditional, minimum ? "arithmetic-if-min" : "arithmetic-if-max"),
                label + " should retain the proved compact lowering");
      const std::vector<int> values = bounded ? std::vector<int>{1, 3, 9}
                                              : std::vector<int>{-5, -1, 0, 1, 5};
      for (const int a : values) {
        for (const int b : values)
          require_rom_integer(conditional, {{"left", a}, {"right", b}},
                              minimum ? std::min(a, b) : std::max(a, b), label);
      }
    }
  }

  for (const bool lower : {false, true}) {
    const CompileResult clamp = compile_source(
        "program Clamp {\n  state { value: packed = 0 }\n  if value " +
        std::string(lower ? "<" : ">") +
        " 0 { value = 0 }\n  halt(value)\n}\n", options);
    for (const int value : {-5, -1, 0, 1, 5})
      require_rom_integer(clamp, {{"value", value}},
                          lower ? std::max(value, 0) : std::min(value, 0), "zero clamp");
  }

  const CompileResult double_clamp = compile_source(R"mkpro(
program ClampAndDelta {
  state {
    expected_mode("grd")
    depth: counter 0..3 = 0
    next: counter -1..4 = 0
    movement: counter -1..1 = 0
  }
  next = depth + sign(cos(100))
  if next < 0 { next = 0 }
  if next > 3 { next = 3 }
  movement = next - depth
  depth = next
  halt(10 * depth + movement)
}
)mkpro", options);
  for (const std::string mode : {"rad", "grad", "deg"}) {
    const int change = mode == "rad" ? 1 : mode == "deg" ? -1 : 0;
    for (int depth = 0; depth <= 3; ++depth) {
      const int next = std::clamp(depth + change, 0, 3);
      require_rom_integer(double_clamp, {{"depth", depth}}, 10 * next + next - depth,
                          "clamp and retained delta in " + mode, mode);
    }
  }

  const CompileResult guarded_decrement = compile_source(R"mkpro(
program GuardedDecrement {
  state { value: counter 1..99 = 1 }
  if value > 1 { value-- }
  halt(value)
}
)mkpro", options);
  require(has_optimization(guarded_decrement, "residual-guarded-update"),
          "a bounded decrement should reuse the reversed comparison residual");
  for (int value = 1; value <= 4; ++value)
    require_rom_integer(guarded_decrement, {{"value", value}}, std::max(value - 1, 1),
                        "guarded decrement must not turn the lower bound into zero");

  const CompileResult indirect_decrement = compile_source(R"mkpro(
program IndirectDecrement {
  state { value: counter 1..4 = 1 }
  if value > 1 { value-- }
  halt(value)
}
)mkpro", options);
  require(!has_optimization(indirect_decrement, "residual-guarded-update"),
          "residual reuse must not displace a one-cell indirect update");
  require(has_optimization(indirect_decrement, "indirect-incdec-counter"),
          "a suitable counter should retain its cheaper indirect update");
  for (int value = 1; value <= 4; ++value)
    require_rom_integer(indirect_decrement, {{"value", value}}, std::max(value - 1, 1),
                        "guarded indirect decrement");

  for (const std::string comparison : {">", "<="}) {
    for (const int bound : {-2, 1, 3}) {
      const CompileResult residual = compile_source(
          "program ReversedResidual {\n  state { value: counter -99..99 = 1 }\n" +
          std::string("  if value ") + comparison + " " + std::to_string(bound) +
          " { value -= " + std::to_string(bound) + " }\n  halt(value)\n}\n", options);
      for (const int value : {-9, -3, -2, -1, 0, 1, 2, 3, 4, 9}) {
        const bool taken = comparison == ">" ? value > bound : value <= bound;
        require_rom_integer(residual, {{"value", value}}, taken ? value - bound : value,
                            "reversed residual " + comparison);
      }
    }
    const CompileResult unknown = compile_source(
        "program UnknownResidual {\n  state { value: packed = 1 }\n  if value " +
        comparison + " 1 { value-- }\n  halt(value)\n}\n", options);
    require(!has_optimization(unknown, "residual-guarded-update"),
            "unknown domains must retain the original arithmetic");
    for (const int value : {-1, 0, 1, 2, 9}) {
      const bool taken = comparison == ">" ? value > 1 : value <= 1;
      require_rom_integer(unknown, {{"value", value}}, taken ? value - 1 : value,
                          "unproved reversed residual");
    }
  }

  const CompileResult boolean_or = compile_source(R"mkpro(
program BooleanOr {
  state {
    flag: flag = 0
    other: flag = 1
    out: flag = 0
  }
  if flag == 1 { out = 1 }
  else { out = other }
  halt(out)
}
)mkpro", options);
  for (const int a : {0, 1}) {
    for (const int b : {0, 1})
      require_rom_integer(boolean_or, {{"flag", a}, {"other", b}},
                          a || b ? 1 : 0, "boolean OR truth table");
  }
}

} // namespace mkpro::tests
