#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) { return item.name == name; });
}

std::vector<std::string> comments(const CompileResult& result) {
  std::vector<std::string> values;
  values.reserve(result.steps.size());
  for (const ResolvedStep& step : result.steps)
    values.push_back(step.comment.value_or(""));
  return values;
}

int count_comment(const std::vector<std::string>& values, const std::string& comment) {
  return static_cast<int>(std::count(values.begin(), values.end(), comment));
}

int count_bit_mask_helper_calls(const CompileResult& result) {
  return static_cast<int>(std::count_if(result.steps.begin(), result.steps.end(),
                                        [](const ResolvedStep& step) {
                                          return step.opcode == 0x53 &&
                                                 step.comment == "bit_mask helper";
                                        }));
}

} // namespace

void bit_mask_quotient_reuse_matches_typescript_contract() {
  const CompileResult result = compile_source(R"mkpro(
program AdjacentSetUpdates {
  grid: board(1..4, 1..4)
  state {
    cell: coord(grid) = 11
    mine: cells(grid)
    seen: cells(grid)
  }
  loop {
    mine += cell
    seen += cell
    halt(mine + seen)
  }
}
)mkpro");

  require(result.implemented, "adjacent cells set update program should compile");
  for (const Diagnostic& diagnostic : result.diagnostics) {
    require(diagnostic.severity != DiagnosticSeverity::Error,
            "adjacent cells set update should not report errors: " + diagnostic.message);
  }

  require(has_optimization(result, "bit-set-mask-cse"),
          "adjacent cells set updates should report bit-set-mask-cse");
  require(has_optimization(result, "mask-stack-op-reuse"),
          "adjacent cells set updates should report mask-stack-op-reuse");

  const std::vector<std::string> step_comments = comments(result);
  const auto scratch_index =
      std::find(step_comments.begin(), step_comments.end(), "cell bit mask scratch");
  const auto first_set_index =
      std::find(step_comments.begin(), step_comments.end(), "bit_set with reused mask");
  require(scratch_index != step_comments.end(), "shared mask scratch store should be emitted");
  require(first_set_index != step_comments.end(), "first bit_set should use the shared mask");
  require(first_set_index > scratch_index, "first reused bit_set should follow scratch store");
  require(std::find(scratch_index + 1, first_set_index, "reuse cell bit mask") ==
              first_set_index,
          "first reused bit_set should consume the still-live stack mask without recalling it");
  require(count_bit_mask_helper_calls(result) == 1,
          "adjacent cells set updates should compute bit_mask once");
  require(count_comment(step_comments, "bit_set with reused mask") == 2,
          "both cells set updates should use the reused mask");
  require(count_comment(step_comments, "reuse cell bit mask") == 1,
          "only the second cells set update should recall the reused mask");

  const CompileResult shared = compile_source(R"mkpro(
program SharedBitMaskHelper {
  grid: board(1..7, 1..4)
  state {
    cell: coord(grid)
    first: cells(grid)
    second: cells(grid)
    score: counter 0..9 = 0
  }
  loop {
    cell = read()
    if cell in first {
      score++
    }
    if cell in second {
      score++
    }
    halt(score)
  }
}
)mkpro");

  require(shared.implemented, "shared bit_mask helper program should compile");
  for (const Diagnostic& diagnostic : shared.diagnostics) {
    require(diagnostic.severity != DiagnosticSeverity::Error,
            "shared bit_mask helper should not report errors: " + diagnostic.message);
  }
  require(shared.steps.size() == 43,
          "shared bit_mask helper should compile to 43 steps, got " +
              std::to_string(shared.steps.size()));
  require(has_optimization(shared, "shared-bit-mask-helper-layout"),
          "shared bit_mask helper should report shared-bit-mask-helper-layout");
  require(has_optimization(shared, "bit-mask-condition-helper"),
          "shared bit_mask helper should report bit-mask-condition-helper");
  require(has_optimization(shared, "bit-mask-helper"),
          "shared bit_mask helper should report bit-mask-helper");

  CompileOptions variant_options;
  variant_options.analysis = true;
  variant_options.budget = 999999;
  const CompileResult repeated_clear = compile_source(R"mkpro(
program RepeatedBitClearScratch {
  grid: board(1..7, 1..4)
  state {
    pos: coord(grid) = 11
    plans: cells(grid)
    answer: counter 0..9 = 0
    memory: counter 0..9 = 1
    a: counter 0..9 = 1
    b: counter 0..9 = 2
    c: counter 0..9 = 3
    d: counter 0..9 = 4
    e: counter 0..9 = 5
    f: counter 0..9 = 6
    g: counter 0..9 = 7
    h: counter 0..9 = 8
    i: counter 0..9 = 9
  }
  loop {
    if answer == memory {
      plans -= pos
    }
    if a != b {
      plans -= pos
    }
    if c != d {
      plans -= pos
    }
    if e != f {
      plans -= pos
    }
    halt(plans + answer + memory + a + b + c + d + e + f + g + h + i)
  }
}
)mkpro",
                                                      variant_options);

  require(repeated_clear.implemented, "repeated bit clear scratch program should compile");
  for (const Diagnostic& diagnostic : repeated_clear.diagnostics) {
    require(diagnostic.severity != DiagnosticSeverity::Error,
            "repeated bit clear scratch should not report errors: " + diagnostic.message);
  }
  const int single_bit_ops =
      static_cast<int>(std::count_if(repeated_clear.optimizations.begin(),
                                     repeated_clear.optimizations.end(),
                                     [](const OptimizationReport& item) {
                                       return item.name == "single-bit-mask-op";
                                     }));
  require(single_bit_ops == 4,
          "repeated bit clear scratch should report four single-bit-mask-op optimizations, got " +
              std::to_string(single_bit_ops));
  require(repeated_clear.registers.contains("plans"),
          "repeated bit clear scratch should allocate plans");
  require(repeated_clear.registers.contains("pos"),
          "repeated bit clear scratch should allocate pos");
}

} // namespace mkpro::tests
