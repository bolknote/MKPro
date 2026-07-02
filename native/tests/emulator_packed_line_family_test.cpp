#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
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

std::string score_accumulator_source(int x, int y) {
  return std::string(R"mkpro(
program PackedLineScoreProc {
  state {
    lines: packed[4..7] = [44434.4, 44344.2, 43444.1, 34444.3]
    x: counter 0..5 = )mkpro") +
         std::to_string(x) + R"mkpro(
    y: counter 0..5 = )mkpro" + std::to_string(y) + R"mkpro(
    line: packed = 0
    score: packed = 0
  }

  fn score_move() {
    score = packed_score(lines[7], x) + packed_score(lines[6], y)
    normalize(x + y)
    score += packed_score(lines[5], line)
    normalize(x - y)
    score += packed_score(lines[4], line)
  }

  fn normalize(raw_line) {
    line = frac((raw_line + 3) / 4) * 4 + 1
  }

  loop {
    score_move()
    halt(score)
  }
}
)mkpro";
}

// Same scoring semantics, but shaped so the packed-line-family score
// accumulator rule cannot match and every packed_score term lowers through
// the generic pipeline.
std::string score_generic_source(int x, int y) {
  return std::string(R"mkpro(
program PackedLineScoreProcB {
  state {
    lines: packed[4..7] = [44434.4, 44344.2, 43444.1, 34444.3]
    x: counter 0..5 = )mkpro") +
         std::to_string(x) + R"mkpro(
    y: counter 0..5 = )mkpro" + std::to_string(y) + R"mkpro(
    line: packed = 0
    score: packed = 0
    dummy: packed = 0
  }

  fn score_move() {
    score = packed_score(lines[7], x)
    dummy = 1
    score += packed_score(lines[6], y)
    normalize(x + y)
    score += packed_score(lines[5], line)
    normalize(x - y)
    score += packed_score(lines[4], line)
  }

  fn normalize(raw_line) {
    line = frac((raw_line + 3) / 4) * 4 + 1
  }

  loop {
    score_move()
    halt(score)
  }
}
)mkpro";
}

} // namespace

void emulator_packed_line_family_pow10_semantics_match_generic_lowering() {
  CompileOptions options;
  options.budget = 999999;
  options.disable_candidate_search = true;

  // The packed-line score accumulator helper must compute the same score as
  // the generic packed_score lowering, including the diagonal terms whose
  // line values (lines[5]/lines[4]) it recalls inside the shared tail.
  for (const auto [x, y] : {std::pair{4, 4}, std::pair{2, 3}, std::pair{1, 4}}) {
    const CompileResult accumulator = compile_source(score_accumulator_source(x, y), options);
    require(has_optimization(accumulator, "packed-line-family-score-accumulator"),
            "score fixture should use the packed-line score accumulator");
    const CompileResult generic = compile_source(score_generic_source(x, y), options);
    require(!has_optimization(generic, "packed-line-family-score-accumulator"),
            "generic score fixture should not use the packed-line score accumulator");

    const RunOutcome accumulator_run = run_compiled(accumulator, "score accumulator variant");
    const RunOutcome generic_run = run_compiled(generic, "score generic variant");
    require(accumulator_run.stopped && generic_run.stopped,
            "score variants should halt on the final score");
    require(accumulator_run.display == generic_run.display,
            "packed-line score accumulator should match generic lowering for x=" +
                std::to_string(x) + " y=" + std::to_string(y) + ": accumulator=" +
                accumulator_run.display + " generic=" + generic_run.display);
  }

  // A stack-carried pow10 index (line stays in X across the mark_one call)
  // must still scale the packed_add delta by 10^line before updating the
  // selected bank register.
  const std::string mark_source = R"mkpro(
program MarkOneProbe {
  state {
    lines: packed[4..7] = [44444.4, 44444.4, 44444.4, 44444.4]
    slot: counter 0..8 = 8
    best_score: packed = -1
    line: packed = 0
    x: counter 0..5 = 3
    y: counter 0..5 = 2
  }

  fn mark_one() {
    slot--
    lines[slot] = packed_add(lines[slot], line, best_score)
    report = bit_and(lines[slot], 88888834)
    if frac(report) != 0 {
      halt(report)
    }
  }

  loop {
    line = x
    mark_one()
    line = y
    mark_one()
    halt(lines[7] + lines[6] / 100000000)
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
          "mark_one should subtract 10^3 from lines[7], got " +
              trim_ascii(mark_run.calc.read_register("7")));
  require(trim_ascii(mark_run.calc.read_register("6")) == "44344,4",
          "mark_one should subtract 10^2 from lines[6], got " +
              trim_ascii(mark_run.calc.read_register("6")));
}

} // namespace mkpro::tests
