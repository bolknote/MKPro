#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>

namespace mkpro::tests {

namespace {

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) { return item.name == name; });
}

int count_optimization(const CompileResult& result, const std::string& name) {
  return static_cast<int>(
      std::count_if(result.optimizations.begin(), result.optimizations.end(),
                    [&](const OptimizationReport& item) { return item.name == name; }));
}

int count_packed_score_helper_jumps(const CompileResult& result) {
  return static_cast<int>(
      std::count_if(result.steps.begin(), result.steps.end(), [](const ResolvedStep& step) {
        return step.opcode == 0x53 && step.comment == "packed_score helper";
      }));
}

int count_packed_score_accumulator_helper_jumps(const CompileResult& result) {
  return static_cast<int>(
      std::count_if(result.steps.begin(), result.steps.end(), [](const ResolvedStep& step) {
        return step.opcode == 0x53 && step.comment == "packed_score accumulator helper";
      }));
}

int count_steps_with_comment(const CompileResult& result, const std::string& comment) {
  return static_cast<int>(
      std::count_if(result.steps.begin(), result.steps.end(), [&](const ResolvedStep& step) {
        return step.comment.has_value() && *step.comment == comment;
      }));
}

// Pin the shared-helper / direct-call (ПП) structure this suite verifies by
// suppressing the default-on aggressive post-layout indirect-flow repacking.
CompileOptions pinned_options() {
  CompileOptions options;
  options.disable_aggressive_post_layout = true;
  options.packed_score_accumulator_helpers = true;
  return options;
}

} // namespace

void packed_score_helpers_match_typescript_contract() {
  const CompileResult direct_expression_sum = compile_source(R"mkpro(
program PackedScoreDirectExpressionAccumulator {
  state {
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
  }
  loop {
    halt(packed_score(a, 1) + packed_score(b, 2) + packed_score(c, 3))
  }
}
)mkpro",
                                                       pinned_options());

  require(direct_expression_sum.implemented,
          "native compiler should lower direct repeated packed_score expressions");
  require(direct_expression_sum.diagnostics.empty(),
          "direct repeated packed_score expression compile should not report diagnostics");
  require(has_optimization(direct_expression_sum, "packed-score-accumulator-helper"),
          "direct packed_score expression should report the accumulator helper body strategy name");
  require(has_optimization(direct_expression_sum, "packed-score-sum-accumulator"),
          "direct packed_score expression should report expression accumulator lowering");
  require(count_optimization(direct_expression_sum, "packed-score-stack-helper-call") == 0,
          "direct packed_score expression should not use standalone helper-call lowering");
  require(count_packed_score_helper_jumps(direct_expression_sum) == 0,
          "direct packed_score expression should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(direct_expression_sum) == 3,
          "direct packed_score expression should emit three accumulator helper calls");

  const CompileResult explicit_sum_expression = compile_source(R"mkpro(
program PackedScoreExplicitSumAccumulator {
  state {
    base: packed = 7
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
  }
  loop {
    halt(sum(base, packed_score(a, 1), packed_score(b, 2), 0, packed_score(c, 3)))
  }
}
)mkpro",
                                                        pinned_options());

  require(explicit_sum_expression.implemented,
          "native compiler should lower explicit sum(...) packed_score expressions");
  require(explicit_sum_expression.diagnostics.empty(),
          "explicit sum(...) packed_score compile should not report diagnostics");
  require(has_optimization(explicit_sum_expression, "packed-score-sum-accumulator"),
          "explicit sum(...) packed_score expression should report accumulator lowering");
  require(count_optimization(explicit_sum_expression, "packed-score-stack-helper-call") == 0,
          "explicit sum(...) packed_score expression should not use standalone helper calls");
  require(count_packed_score_helper_jumps(explicit_sum_expression) == 0,
          "explicit sum(...) packed_score expression should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(explicit_sum_expression) == 3,
          "explicit sum(...) packed_score expression should emit three accumulator helper calls");

  const CompileResult direct_expression_sum_with_zero = compile_source(R"mkpro(
program PackedScoreDirectExpressionAccumulatorWithZero {
  state {
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
  }
  loop {
    halt(0 + packed_score(a, 1) + packed_score(b, 2) + 0 + packed_score(c, 3))
  }
}
)mkpro",
                                                                 pinned_options());

  require(direct_expression_sum_with_zero.implemented,
          "native compiler should lower repeated packed_score expressions with neutral zeros");
  require(direct_expression_sum_with_zero.diagnostics.empty(),
          "neutral-zero packed_score expression compile should not report diagnostics");
  require(has_optimization(direct_expression_sum_with_zero, "packed-score-accumulator-helper"),
          "neutral-zero packed_score expression should report the accumulator helper body");
  require(has_optimization(direct_expression_sum_with_zero, "packed-score-sum-accumulator"),
          "neutral-zero packed_score expression should report expression accumulator lowering");
  require(count_optimization(direct_expression_sum_with_zero, "packed-score-stack-helper-call") == 0,
          "neutral-zero packed_score expression should not use standalone helper-call lowering");
  require(count_packed_score_helper_jumps(direct_expression_sum_with_zero) == 0,
          "neutral-zero packed_score expression should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(direct_expression_sum_with_zero) == 3,
          "neutral-zero packed_score expression should emit three accumulator helper calls");
  require(count_steps_with_comment(direct_expression_sum_with_zero,
                                   "packed_score accumulator zero") == 1,
          "neutral-zero packed_score expression should keep one accumulator zero");

  const CompileResult direct_expression_sum_with_initial = compile_source(R"mkpro(
program PackedScoreDirectExpressionAccumulatorWithInitial {
  state {
    base: packed = 7
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
  }
  loop {
    halt(base + packed_score(a, 1) + packed_score(b, 2) + packed_score(c, 3))
  }
}
)mkpro",
                                                                   pinned_options());

  require(direct_expression_sum_with_initial.implemented,
          "native compiler should lower packed_score expressions with an initial addend");
  require(direct_expression_sum_with_initial.diagnostics.empty(),
          "initial-addend packed_score expression compile should not report diagnostics");
  require(has_optimization(direct_expression_sum_with_initial, "packed-score-sum-accumulator"),
          "initial-addend packed_score expression should report accumulator lowering");
  require(count_optimization(direct_expression_sum_with_initial,
                             "packed-score-stack-helper-call") == 0,
          "initial-addend packed_score expression should not use standalone helper calls");
  require(count_packed_score_helper_jumps(direct_expression_sum_with_initial) == 0,
          "initial-addend packed_score expression should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(direct_expression_sum_with_initial) == 3,
          "initial-addend packed_score expression should emit three accumulator helper calls");
  require(count_steps_with_comment(direct_expression_sum_with_initial,
                                   "packed_score accumulator zero") == 0,
          "initial-addend packed_score expression should use the addend as accumulator");

  const CompileResult direct_expression_sum_with_expression_index = compile_source(R"mkpro(
program PackedScoreDirectExpressionAccumulatorWithExpressionIndex {
  state {
    line: packed = 44444.4
    x: counter 0..7 = 2
    y: counter 0..7 = 3
  }
  loop {
    halt(packed_score(line, x + y - 1) + packed_score(line, x + y) + packed_score(line, x + y + 1))
  }
}
)mkpro",
                                                                       pinned_options());

  require(direct_expression_sum_with_expression_index.implemented,
          "native compiler should lower packed_score sums with stack-preserving index expressions");
  require(direct_expression_sum_with_expression_index.diagnostics.empty(),
          "expression-index packed_score compile should not report diagnostics");
  require(has_optimization(direct_expression_sum_with_expression_index,
                           "packed-score-sum-accumulator"),
          "expression-index packed_score sum should report accumulator lowering");
  require(count_optimization(direct_expression_sum_with_expression_index,
                             "packed-score-stack-helper-call") == 0,
          "expression-index packed_score sum should not use standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(
              direct_expression_sum_with_expression_index) == 3,
          "expression-index packed_score sum should emit three accumulator helper calls");

  const CompileResult direct_expression_sum_with_expression_line = compile_source(R"mkpro(
program PackedScoreDirectExpressionAccumulatorWithExpressionLine {
  state {
    a: packed = 44444.4
    b: packed = 10
    c: packed = 20
    slot: counter 0..7 = 4
  }
  loop {
    halt(packed_score(a + b, slot) + packed_score(a + c, slot) + packed_score(a + b + c, slot))
  }
}
)mkpro",
                                                                      pinned_options());

  require(direct_expression_sum_with_expression_line.implemented,
          "native compiler should lower packed_score sums with stack-preserving line expressions");
  require(direct_expression_sum_with_expression_line.diagnostics.empty(),
          "expression-line packed_score compile should not report diagnostics");
  require(has_optimization(direct_expression_sum_with_expression_line,
                           "packed-score-sum-accumulator"),
          "expression-line packed_score sum should report accumulator lowering");
  require(count_optimization(direct_expression_sum_with_expression_line,
                             "packed-score-stack-helper-call") == 0,
          "expression-line packed_score sum should not use standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(
              direct_expression_sum_with_expression_line) == 3,
          "expression-line packed_score sum should emit three accumulator helper calls");

  const CompileResult assignment_sum = compile_source(R"mkpro(
program PackedScoreAssignmentAccumulator {
  state {
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
    value: packed = 0
  }
  loop {
    value = packed_score(a, 1) + packed_score(b, 2) + packed_score(c, 3)
    halt(value)
  }
}
)mkpro",
                                              pinned_options());

  require(assignment_sum.implemented, "native compiler should lower repeated packed_score calls");
  require(assignment_sum.diagnostics.empty(),
          "repeated packed_score compile should not report diagnostics");
  require(has_optimization(assignment_sum, "packed-score-accumulator-helper"),
          "repeated packed_score should report the accumulator helper body strategy name");
  require(has_optimization(assignment_sum, "packed-score-assignment-stack-accumulator"),
          "three packed_score calls assigned to an immediate consumer should stay on the stack");
  require(count_optimization(assignment_sum, "packed-score-stack-helper-call") == 0,
          "three packed_score calls in one sum should not use standalone helper-call lowering");
  require(count_packed_score_helper_jumps(assignment_sum) == 0,
          "three packed_score calls in one sum should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(assignment_sum) == 3,
          "three packed_score calls in one sum should emit three accumulator helper calls");
  require(count_steps_with_comment(assignment_sum, "set value") == 0,
          "immediately consumed packed_score accumulator assignment should not store value");
  require(count_steps_with_comment(assignment_sum, "recall value") == 0,
          "immediately consumed packed_score accumulator assignment should not recall value");

  const CompileResult statement_sequence = compile_source(R"mkpro(
program PackedScoreStatementAccumulator {
  state {
    line: packed = 44444.4
    slot: counter 0..7 = 4
    score: packed = 0
  }
  loop {
    score = packed_score(line, slot)
    score = score + packed_score(line, slot)
    score += packed_score(line, slot)
    halt(score)
  }
}
)mkpro",
                                                          pinned_options());
  require(statement_sequence.implemented,
          "native compiler should lower statement-level packed_score accumulation");
  require(statement_sequence.diagnostics.empty(),
          "statement-level packed_score accumulation should not report diagnostics");
  require(has_optimization(statement_sequence, "packed-score-sequence-stack-accumulator"),
          "statement-level packed_score accumulation should report sequence accumulator lowering");
  require(count_optimization(statement_sequence, "packed-score-stack-helper-call") == 0,
          "statement-level packed_score accumulation should not use standalone helper calls");
  require(count_packed_score_helper_jumps(statement_sequence) == 0,
          "statement-level packed_score accumulation should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(statement_sequence) == 3,
          "statement-level packed_score accumulation should emit three accumulator helper calls");

  const CompileResult sum_continuation_sequence = compile_source(R"mkpro(
program PackedScoreSumContinuationStatementAccumulator {
  state {
    line: packed = 44444.4
    slot: counter 0..7 = 4
    score: packed = 0
  }
  loop {
    score = packed_score(line, slot)
    score = sum(score, packed_score(line, slot), 0)
    score = sum(packed_score(line, slot), score)
    halt(score)
  }
}
)mkpro",
                                                                 pinned_options());
  require(sum_continuation_sequence.implemented,
          "native compiler should lower sum(score, packed_score(...)) continuations");
  require(sum_continuation_sequence.diagnostics.empty(),
          "sum-continuation packed_score accumulation should not report diagnostics");
  require(has_optimization(sum_continuation_sequence,
                           "packed-score-sequence-stack-accumulator"),
          "sum-continuation packed_score accumulation should report sequence accumulator "
          "lowering");
  require(count_optimization(sum_continuation_sequence, "packed-score-stack-helper-call") == 0,
          "sum-continuation packed_score accumulation should not use standalone helper calls");
  require(count_packed_score_helper_jumps(sum_continuation_sequence) == 0,
          "sum-continuation packed_score accumulation should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(sum_continuation_sequence) == 3,
          "sum-continuation packed_score accumulation should emit three accumulator helper calls");
  require(count_steps_with_comment(sum_continuation_sequence, "set score") == 0,
          "immediately consumed sum-continuation accumulator should not store score");
  require(count_steps_with_comment(sum_continuation_sequence, "recall score") == 0,
          "sum-continuation accumulator should keep score on the stack instead of recalling it");

  const CompileResult zero_started_sequence = compile_source(R"mkpro(
program PackedScoreZeroStartedStatementAccumulator {
  state {
    line: packed = 44444.4
    slot: counter 0..7 = 4
    score: packed = 0
  }
  loop {
    score = 0
    score += packed_score(line, slot)
    score += packed_score(line, slot)
    score += packed_score(line, slot)
    halt(score)
  }
}
)mkpro",
                                                            pinned_options());
  require(zero_started_sequence.implemented,
          "native compiler should lower zero-started packed_score accumulation");
  require(zero_started_sequence.diagnostics.empty(),
          "zero-started packed_score accumulation should not report diagnostics");
  require(has_optimization(zero_started_sequence, "packed-score-sequence-stack-accumulator"),
          "zero-started packed_score accumulation should report sequence accumulator lowering");
  require(count_optimization(zero_started_sequence, "packed-score-stack-helper-call") == 0,
          "zero-started packed_score accumulation should not use standalone helper calls");
  require(count_packed_score_helper_jumps(zero_started_sequence) == 0,
          "zero-started packed_score accumulation should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(zero_started_sequence) == 3,
          "zero-started packed_score accumulation should emit three accumulator helper calls");
  require(count_steps_with_comment(zero_started_sequence, "packed_score accumulator zero") == 1,
          "zero-started packed_score accumulation should use one explicit accumulator zero");
  require(count_steps_with_comment(zero_started_sequence, "set score") == 0,
          "immediately consumed zero-started accumulator should not store score");
  require(count_steps_with_comment(zero_started_sequence, "recall score") == 0,
          "immediately consumed zero-started accumulator should not recall score");

  const CompileResult update_started_sequence = compile_source(R"mkpro(
program PackedScoreUpdateStartedStatementAccumulator {
  state {
    line: packed = 44444.4
    slot: counter 0..7 = 4
    score: packed = 7
  }
  loop {
    score += packed_score(line, slot)
    score += packed_score(line, slot)
    score += packed_score(line, slot)
    halt(score)
  }
}
)mkpro",
                                                              pinned_options());
  require(update_started_sequence.implemented,
          "native compiler should lower update-started packed_score accumulation");
  require(update_started_sequence.diagnostics.empty(),
          "update-started packed_score accumulation should not report diagnostics");
  require(has_optimization(update_started_sequence, "packed-score-sequence-stack-accumulator"),
          "update-started packed_score accumulation should report sequence accumulator lowering");
  require(count_optimization(update_started_sequence, "packed-score-stack-helper-call") == 0,
          "update-started packed_score accumulation should not use standalone helper calls");
  require(count_packed_score_helper_jumps(update_started_sequence) == 0,
          "update-started packed_score accumulation should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(update_started_sequence) == 3,
          "update-started packed_score accumulation should emit three accumulator helper calls");
  require(count_steps_with_comment(update_started_sequence, "packed_score accumulator zero") == 0,
          "update-started packed_score accumulation should use the existing score as accumulator");
  require(count_steps_with_comment(update_started_sequence, "recall score") == 1,
          "update-started packed_score accumulation should recall score only once");
  require(count_steps_with_comment(update_started_sequence, "set score") == 0,
          "immediately consumed update-started accumulator should not store score");

  const CompileResult x_param_sequence = compile_source(R"mkpro(
program PackedScoreXParamAccumulatorHelper {
  state {
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
    x: counter 0..7 = 2
    y: counter 0..7 = 3
    line: packed = 0
    score: packed = 0
  }
  loop {
    score = 0
    normalize(x + y)
    score += packed_score(a, line)
    normalize(x - y)
    score += packed_score(b, line)
    normalize(y + 1)
    score += packed_score(c, line)
    halt(score)
  }

  fn normalize(raw_line) {
    line = frac((raw_line + 3) / 4) * 4 + 1
  }
}
)mkpro",
                                                     pinned_options());
  require(x_param_sequence.implemented,
          "x-param packed_score accumulator-helper program should compile");
  require(x_param_sequence.diagnostics.empty(),
          "x-param packed_score accumulator-helper program should not report diagnostics");
  require(has_optimization(x_param_sequence, "packed-score-accumulator-helper"),
          "x-param packed_score sequence should emit the accumulator helper body");
  require(count_optimization(x_param_sequence,
                             "x-param-packed-score-line-stack-accumulate") == 3,
          "x-param packed_score sequence should keep all returned-index updates stack-carried");
  require(count_packed_score_accumulator_helper_jumps(x_param_sequence) == 3,
          "x-param packed_score sequence should call the accumulator helper for each term");
  require(count_packed_score_helper_jumps(x_param_sequence) == 0,
          "x-param packed_score sequence should not use the standalone helper fallback");
  require(count_steps_with_comment(x_param_sequence, "packed_score stack accumulator") == 0,
          "x-param packed_score sequence should not need separate accumulator add cells");

  const CompileResult mixed = compile_source(R"mkpro(
program PackedScoreMixedAccumulatorFallback {
  state {
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
    d: packed = 44447.4
    value: packed = 0
    extra: packed = 0
  }
  loop {
    value = packed_score(a, 1) + packed_score(b, 2) + packed_score(c, 3)
    extra = packed_score(d, 4)
    halt(value + extra)
  }
}
)mkpro",
                                             pinned_options());
  require(mixed.implemented, "mixed accumulator/helper program should compile");
  require(mixed.diagnostics.empty(), "mixed accumulator/helper program should not report diagnostics");
  require(has_optimization(mixed, "packed-score-accumulator-helper"),
          "mixed repeated packed_score should keep the accumulator helper body");
  require(has_optimization(mixed, "packed-score-stack-helper"),
          "mixed repeated packed_score should keep the standalone helper fallback");
  require(count_packed_score_accumulator_helper_jumps(mixed) == 3,
          "mixed repeated packed_score should emit three accumulator helper calls");
  require(count_packed_score_helper_jumps(mixed) == 1,
          "mixed repeated packed_score should emit one standalone helper call");
}

} // namespace mkpro::tests
