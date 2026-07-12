#include "mkpro/compiler.hpp"
#include "mkpro/core/emit/lowering_helpers.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>
#include <variant>
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

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& report) { return report.name == name; });
}

bool has_error(const CompileResult& result) {
  return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                     [](const Diagnostic& diagnostic) {
                       return diagnostic.severity == DiagnosticSeverity::Error;
                     });
}

double calculator_number(std::string value) {
  int exponent = 0;
  const bool scientific = value.size() > 12U;
  if (scientific) {
    exponent = std::stoi(value.substr(12));
    value.resize(12);
  }
  value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
                return std::isspace(ch) != 0;
              }),
              value.end());
  std::replace(value.begin(), value.end(), ',', '.');
  const double mantissa = std::stod(value);
  return scientific ? mantissa * std::pow(10.0, exponent) : mantissa;
}

void input_signed_number(emulator::MK61& calc, std::string value) {
  const bool negative = !value.empty() && value.front() == '-';
  if (negative)
    value.erase(value.begin());
  calc.input_number(value, true);
  if (negative)
    calc.press("/-/");
}

CompileResult compile_grid_norm_probe(int width, bool shared) {
  const std::string calculation =
      shared ? "first = grid_norm(raw, " + std::to_string(width) + ")\n"
                   "    result = grid_wrap(first, " + std::to_string(width) + ")"
             : "result = grid_norm(raw, " + std::to_string(width) + ")";
  const std::string source =
      "program GridNormProbe {\n"
      "  state {\n"
      "    raw: packed = 0\n"
      "    first: packed = 0\n"
      "    result: packed = 0\n"
      "  }\n"
      "  loop {\n"
      "    show(0)\n"
      "    raw = entered()\n"
      "    " + calculation + "\n"
      "    halt(result)\n"
      "  }\n"
      "}\n";
  CompileOptions options;
  options.budget = 999;
  options.disable_candidate_search = true;
  return compile_source(source, options);
}

struct Observation {
  double x = 0;
  double visible = 0;
  double y = 0;
  double z = 0;
  double t = 0;
};

Observation run_grid_norm_probe(const CompileResult& result, const std::string& input) {
  emulator::MK61 calc;
  for (const PreloadReport& preload : result.preloads)
    calc.set_register(preload.register_name, preload.value);
  const emulator::ProgramLoadResult loaded = calc.load_program(step_opcodes(result.steps));
  require(loaded.diagnostics.empty(), "grid_norm probe should load without truncation");

  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult prompt = calc.run_until_stable(400, 5);
  require(prompt.stopped, "grid_norm probe should stop for input");

  input_signed_number(calc, input);
  // The primitive consumes only X. Seed the rest of the data stack after
  // keyboard entry so the full inline/shared sequence has an explicit RPN
  // boundary contract: Y/Z survive, while a temporary stack lift gives the
  // MK-61's normal sticky-T result T <- old Z.
  calc.set_register("Y", "73");
  calc.set_register("Z", "82");
  calc.set_register("T", "91");
  calc.press("С/П");
  const emulator::RunResult output = calc.run_until_stable(1200, 6);
  require(output.stopped, "grid_norm probe should stop with its result");

  return Observation{
      .x = calculator_number(calc.read_register("X")),
      .visible = calculator_number(calc.display_text()),
      .y = calculator_number(calc.read_register("Y")),
      .z = calculator_number(calc.read_register("Z")),
      .t = calculator_number(calc.read_register("T")),
  };
}

void require_probe(const CompileResult& result, const std::string& input, int expected,
                   const std::string& context) {
  const Observation actual = run_grid_norm_probe(result, input);
  require(std::fabs(actual.x - expected) < 1e-9,
          context + " expected X=" + std::to_string(expected) +
              ", got " + std::to_string(actual.x));
  require(std::fabs(actual.visible - expected) < 1e-9,
          context + " should expose the result through the X2/display boundary, got " +
              std::to_string(actual.visible));
  require(actual.y == 73 && actual.z == 82 && actual.t == 82,
          context + " should preserve Y/Z and apply the documented sticky-T boundary, got " +
              std::to_string(actual.y) + "/" + std::to_string(actual.z) + "/" +
              std::to_string(actual.t));
}

} // namespace

void emulator_grid_norm_signed_modulo_matches_language_contract() {
  const CompileResult inline_width4 = compile_grid_norm_probe(4, false);
  require(inline_width4.implemented && !has_error(inline_width4),
          "single-use grid_norm probe should compile");
  require(has_optimization(inline_width4, "signed-grid-normalization-inline"),
          "single-use grid_norm should choose the 12-cell inline form");
  require(!has_optimization(inline_width4, "signed-grid-normalization-helper"),
          "single-use grid_norm should not pay for a helper");

  CompileOptions one_eval_options;
  one_eval_options.budget = 999;
  one_eval_options.disable_candidate_search = true;
  const CompileResult one_eval = compile_source(R"mkpro(
program GridNormOneEvaluation {
  loop {
    halt(grid_norm(random(), 4))
  }
}
)mkpro", one_eval_options);
  require(one_eval.implemented && !has_error(one_eval),
          "grid_norm should accept a computed calculator value");
  require(std::count_if(one_eval.steps.begin(), one_eval.steps.end(),
                        [](const ResolvedStep& step) { return step.opcode == 0x3b; }) == 1,
          "grid_norm must evaluate a side-effecting argument exactly once");

  const CompileResult phased_input = compile_source(R"mkpro(
program GridNormPhasedInput {
  state {
    x: packed = 0
    y: packed = 0
  }
  loop {
    show(0)
    x = entered()
    x = grid_norm(x)
    y = entered()
    y = grid_norm(y)
    halt(x * 10 + y)
  }
}
)mkpro", one_eval_options);
  require(phased_input.implemented && !has_error(phased_input),
          "interleaved entered/grid_norm syntax should still lower as ordinary code");
  require(phased_input.interaction_protocols.size() == 1U &&
              phased_input.interaction_protocols.front().phases.size() == 1U,
          "an X-mutating grid_norm between entered() stores must not be certified as a two-phase "
          "X PP Y C/P protocol; the second keyboard X would already be pending");

  const CompileResult shared_width4 = compile_grid_norm_probe(4, true);
  require(shared_width4.implemented && !has_error(shared_width4),
          "repeated width-4 grid_norm probe should compile");
  require(has_optimization(shared_width4, "signed-grid-normalization-helper"),
          "two width-4 calls should share one helper");
  const int shared_calls = static_cast<int>(std::count_if(
      shared_width4.steps.begin(), shared_width4.steps.end(), [](const ResolvedStep& step) {
        return step.opcode == 0x53 && step.comment == "grid_norm shared helper";
      }));
  require(shared_calls == 2, "the shared probe should contain exactly two helper calls, got " +
                                 std::to_string(shared_calls));
  require(std::count_if(shared_width4.steps.begin(), shared_width4.steps.end(),
                        [](const ResolvedStep& step) {
                          return step.comment == "grid_norm integer part";
                        }) == 1,
          "the shared probe should emit one normalization body");

  const std::vector<std::pair<std::string, int>> width4_cases = {
      {"-9", 3}, {"-5", 3}, {"-4", 4}, {"-3", 1}, {"-1", 3}, {"0", 4},
      {"1", 1},  {"3", 3},  {"4", 4},  {"5", 1},  {"9", 1},  {"1.9", 1},
      {"-5.9", 3},
  };
  for (const auto& [input, expected] : width4_cases)
    require_probe(shared_width4, input, expected, "grid_norm width 4 input " + input);

  // These deliberately pin the real eight-digit rounding boundary rather
  // than a mathematical modulo expectation. The fractional results are why
  // flow analysis cannot treat grid_norm as an unconditional sanitizer.
  const Observation rounded_positive = run_grid_norm_probe(inline_width4, "9999999");
  const Observation rounded_next = run_grid_norm_probe(inline_width4, "9999997");
  const Observation rounded_negative = run_grid_norm_probe(inline_width4, "-9999999");
  const Observation rounded_negative_next = run_grid_norm_probe(inline_width4, "-9999997");
  require(std::fabs(rounded_positive.x - 2.8) < 1e-9 &&
              std::fabs(rounded_next.x - 0.8) < 1e-9 &&
              std::fabs(rounded_negative.x - 1.2) < 1e-9 &&
              std::fabs(rounded_negative_next.x - 3.2) < 1e-9,
          "large width-4 inputs should expose the hardware's fractional rounded remainders, got " +
              std::to_string(rounded_positive.x) + "/" + std::to_string(rounded_next.x) + "/" +
              std::to_string(rounded_negative.x) + "/" +
              std::to_string(rounded_negative_next.x));

  const CompileResult shared_width3 = compile_grid_norm_probe(3, true);
  require(shared_width3.implemented && !has_error(shared_width3),
          "repeated width-3 grid_norm probe should compile");
  const CompileResult inline_width3 = compile_grid_norm_probe(3, false);
  require(inline_width3.implemented && !has_error(inline_width3),
          "single width-3 grid_norm probe should compile");
  require_probe(inline_width3, "-4", 2, "inline grid_norm width 3 input -4");
  for (const auto& [input, expected] :
       std::vector<std::pair<std::string, int>>{{"-4", 2}, {"-3", 3}, {"-1", 2},
                                                {"0", 3},  {"4", 1},  {"7.9", 1}}) {
    require_probe(shared_width3, input, expected, "grid_norm width 3 input " + input);
  }

  const CompileResult shared_width5 = compile_grid_norm_probe(5, true);
  require(shared_width5.implemented && !has_error(shared_width5),
          "repeated width-5 grid_norm probe should compile");
  for (const auto& [input, expected] :
       std::vector<std::pair<std::string, int>>{{"-11", 4}, {"-5", 5}, {"-1", 4},
                                                {"0", 5},   {"6", 1},  {"11.9", 1}}) {
    require_probe(shared_width5, input, expected, "grid_norm width 5 input " + input);
  }

  const std::string fallback = expression_to_json(core::emit::grid_norm_expression(
      Expression{.kind = "identifier", .name = "value"}, 4));
  require(fallback.find("\"callee\":\"max\"") == std::string::npos,
          "grid_norm AST fallback must not use the MK-61 zero-is-greatest K max opcode");

  CompileOptions options;
  options.budget = 999;
  options.disable_candidate_search = true;
  const CompileResult dynamic_width = compile_source(R"mkpro(
program DynamicWidth {
  state {
    value: packed = 1
    width: packed = 4
  }
  loop {
    halt(grid_norm(value, width))
  }
}
)mkpro", options);
  require(has_error(dynamic_width), "grid_norm should reject a runtime-varying width");
}

void grid_norm_unary_wrapper_uses_generic_x_entry_and_forwarding() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999;
  options.disable_candidate_search = true;

  const CompileResult forwarded = compile_source(R"mkpro(
program OpaqueGridWrapper {
  state {
    seed: packed = 1
    line: packed = 0
    total: packed = 0
  }

  loop {
    normalize(seed)
    total += line
    normalize(seed)
    total += line
    total += grid_norm(seed)
    total += grid_norm(seed)
    halt(total)
  }

  fn normalize(value) {
    line = grid_norm(value)
  }
}
)mkpro",
                                                 options);
  require(forwarded.implemented && !has_error(forwarded),
          "generic unary grid wrapper fixture should compile");
  require(forwarded.registers.find("value") == forwarded.registers.end() &&
              forwarded.registers.find("line") == forwarded.registers.end(),
          "grid wrapper parameter and stack-only result should not allocate registers");
  require(has_optimization(forwarded, "x-param-proc-entry"),
          "grid wrapper should consume its one argument from current X");
  require(has_optimization(forwarded, "jump-thread"),
          "direct calls should bypass a procedure that only tail-forwards to grid_norm");
  require(has_optimization(forwarded, "dead-proc-elimination"),
          "the unreferenced forwarding wrapper should be removed after call threading");
  require(std::none_of(forwarded.items.begin(), forwarded.items.end(),
                       [](const MachineItem& item) {
                         if (item.kind == MachineItemKind::Label && item.name == "__fn_normalize")
                           return true;
                         const auto* target = std::get_if<std::string>(&item.target);
                         return target != nullptr && *target == "__fn_normalize";
                       }),
          "the final artifact should contain neither the wrapper nor an incoming edge to it");

  const CompileResult parked_y = compile_source(R"mkpro(
program OpaqueLedgerNormalizerAccumulator {
  state {
    ledger_a: packed = 12345.6
    ledger_b: packed = 23456.7
    ledger_c: packed = 34567.8
    ledger_d: packed = 45678.9
    column: packed = 2
    normalized_column: packed = 0
    checksum: packed = 0
  }

  loop {
    checksum = packed_score(ledger_a, column) + packed_score(ledger_b, column)
    canonicalize(column)
    checksum += packed_score(ledger_c, normalized_column)
    canonicalize(column)
    checksum += packed_score(ledger_d, normalized_column)
    halt(checksum)
  }

  fn canonicalize(value) {
    normalized_column = grid_norm(value)
  }
}
)mkpro",
                                                options);
  require(parked_y.implemented && !has_error(parked_y),
          "grid wrapper should compile while an accumulator is parked in caller Y");
  require(parked_y.registers.find("normalized_column") == parked_y.registers.end(),
          "the proved Y-preserving wrapper should keep its result stack-only");
  require(std::count_if(parked_y.optimizations.begin(), parked_y.optimizations.end(),
                        [](const OptimizationReport& report) {
                          return report.name == "x-param-packed-score-line-stack-accumulate";
                        }) == 2,
          "both ledger wrapper calls should preserve the packed-score accumulator parked in Y");

  const CompileResult unrelated_call = compile_source(R"mkpro(
program OpaqueUnaryCall {
  state {
    seed: packed = 2
    out: packed = 0
  }

  loop {
    wrapper(seed)
    wrapper(seed)
    wrapper(seed)
    wrapper(seed)
    halt(out)
  }

  fn identity(inner) {
    return inner
  }

  fn wrapper(argument) {
    out = identity(argument)
  }
}
)mkpro",
                                                      options);
  require(unrelated_call.implemented && !has_error(unrelated_call),
          "unrelated one-argument user-call wrapper should compile normally");
  require(unrelated_call.registers.find("argument") != unrelated_call.registers.end(),
          "only typed grid_norm/grid_wrap calls may use the grid wrapper X-entry lowering");
}

} // namespace mkpro::tests
