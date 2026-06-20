#include "mkpro/compiler.hpp"
#include "mkpro/core/parser.hpp"
#include "mkpro/core/v2_const.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>

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
}

} // namespace mkpro::tests
