#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::string trim_ascii(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    value.pop_back();
  return value;
}

std::string mk61_hex_literal(const std::string& text) {
  std::string out;
  for (char ch : text) {
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(ch)))) {
      case 'A':
        out.push_back('-');
        break;
      case 'B':
        out.push_back('L');
        break;
      case 'C':
        out += "С";
        break;
      case 'D':
        out += "Г";
        break;
      case 'E':
        out += "Е";
        break;
      case 'F':
        out.push_back('_');
        break;
      default:
        out.push_back(ch);
        break;
    }
  }
  return out;
}

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& entry) { return entry.name == name; });
}

struct RunOutcome {
  bool stopped = false;
  std::string display;
  emulator::MK61 calc;
};

RunOutcome run_compiled(const CompileResult& result, const std::string& context) {
  require(result.implemented, context + " should compile");
  require(result.diagnostics.empty(), context + " should not report diagnostics");
  std::vector<int> codes;
  codes.reserve(result.steps.size());
  for (const ResolvedStep& step : result.steps)
    codes.push_back(step.opcode);
  RunOutcome outcome;
  const emulator::ProgramLoadResult loaded = outcome.calc.load_program(codes);
  require(loaded.diagnostics.empty(), context + " should load without diagnostics");
  for (const PreloadReport& preload : result.preloads)
    outcome.calc.set_register(preload.register_name, mk61_hex_literal(preload.value));
  outcome.calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult run = outcome.calc.run_until_stable(3000, 6);
  outcome.stopped = run.stopped;
  outcome.display = trim_ascii(outcome.calc.display_text());
  return outcome;
}

} // namespace

void emulator_indexed_packed_pow10_y_stack_semantics() {
  CompileOptions options;
  options.budget = 999999;
  options.disable_candidate_search = true;

  // A stack-carried pow10 index (digit_index stays in X across the helper call)
  // must still scale the packed_add delta by 10^digit_index before updating the
  // selected bank register.
  const std::string mark_source = R"mkpro(
program IndexedPackedPow10Probe {
  state {
    buckets: packed[4..7] = [44444.4, 44444.4, 44444.4, 44444.4]
    cursor: counter 0..8 = 8
    delta: packed = -1
    digit_index: packed = 0
    first_index: counter 0..5 = 3
    second_index: counter 0..5 = 2
    report: packed = 0
  }

  fn apply_delta() {
    cursor--
    buckets[cursor] = packed_add(buckets[cursor], digit_index, delta)
    report = bit_and(buckets[cursor], 88888834)
    if frac(report) != 0 {
      halt(report)
    }
  }

  loop {
    digit_index = first_index
    apply_delta()
    digit_index = second_index
    apply_delta()
    halt(buckets[7] + buckets[6] / 100000000)
  }
}
)mkpro";
  const CompileResult mark = compile_source(mark_source, options);
  require(has_optimization(mark, "indexed-packed-y-stack-pow10-delta"),
          "mark fixture should consume the stack-carried index from Y");
  RunOutcome mark_run = run_compiled(mark, "stack-carried pow10 mark variant");
  require(mark_run.stopped, "mark variant should halt with the combined lines report");
  // lines[7] = 44444.4 - 10^3 = 43444.4 and lines[6] = 44444.4 - 10^2 = 44344.4.
  require(trim_ascii(mark_run.calc.read_register("7")) == "43444,4",
          "helper should subtract 10^3 from buckets[7], got " +
              trim_ascii(mark_run.calc.read_register("7")));
  require(trim_ascii(mark_run.calc.read_register("6")) == "44344,4",
          "helper should subtract 10^2 from buckets[6], got " +
              trim_ascii(mark_run.calc.read_register("6")));

  struct DeltaCase {
    std::string expression;
    std::string first;
    std::string second;
    std::optional<bool> stack_carried;
  };
  const std::vector<DeltaCase> cases = {
      {"digit_add(buckets[cursor], digit_index, delta)", "44344,4", "44434,4", true},
      {"packed_add(buckets[cursor], digit_index - 1, delta)", "44344,4", "44434,4", true},
      {"buckets[cursor] + delta * pow10(digit_index - 1)", "44344,4", "44434,4", true},
      {"buckets[cursor] + delta * pow(10, digit_index + 1)", "34444,4", "43444,4", true},
      {"buckets[cursor] + pow10((digit_index - 1) + 2) * delta", "34444,4", "43444,4", true},
      {"buckets[cursor] + pow10(4 - digit_index) * delta", "44434,4", "44344,4", true},
      {"buckets[cursor] + (pow10(digit_index) * delta) * 2", "42444,4", "44244,4", true},
      // A frontend may group the independent coefficients into a composite
      // expression. That requires a separate deeper-stack liveness proof;
      // either ordinary or stack-carried lowering must retain all factors.
      {"buckets[cursor] + delta * (pow10(digit_index) * 2)", "42444,4", "44244,4", std::nullopt},
      // This larger update makes the report fractional (0.8), so the first
      // call terminates before the second bucket can be changed.
      {"buckets[cursor] + ((pow10(digit_index) * delta) * 2) * 3", "38444,4", "44444,4", std::nullopt},
      {"buckets[cursor] - pow10(digit_index) * delta", "45444,4", "44544,4", true},
      // The second use of the input must not be silently treated as a saved
      // coefficient after the stack-only input has already been consumed.
      {"buckets[cursor] + pow10(digit_index) * digit_index", "47444,4", "44644,4", false},
  };
  const std::string original = "packed_add(buckets[cursor], digit_index, delta)";
  for (const DeltaCase& test : cases) {
    std::string source = mark_source;
    const auto offset = source.find(original);
    require(offset != std::string::npos, "delta fixture must contain its update expression");
    source.replace(offset, original.size(), test.expression);
    const CompileResult compiled = compile_source(source, options);
    if (test.stack_carried.has_value())
      require(has_optimization(compiled, "indexed-packed-y-stack-pow10-delta") == *test.stack_carried,
              "stack-input eligibility for " + test.expression);
    RunOutcome run = run_compiled(compiled, test.expression);
    require(run.stopped, "scalar delta case should halt: " + test.expression);
    require(trim_ascii(run.calc.read_register("7")) == test.first,
            test.expression + ": first bucket should be " + test.first + ", got " +
                trim_ascii(run.calc.read_register("7")));
    require(trim_ascii(run.calc.read_register("6")) == test.second,
            test.expression + ": second bucket should be " + test.second + ", got " +
                trim_ascii(run.calc.read_register("6")));
    require(trim_ascii(run.calc.read_register("4")) == "44444,4" &&
                trim_ascii(run.calc.read_register("5")) == "44444,4",
            "unselected buckets must survive " + test.expression);
  }
}

} // namespace mkpro::tests
