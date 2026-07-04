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

const CandidateReport* find_candidate(const std::vector<CandidateReport>& candidates,
                                      const std::string& variant) {
  const auto it = std::find_if(candidates.begin(), candidates.end(),
                               [&](const CandidateReport& candidate) {
                                 return candidate.variant == variant;
                               });
  return it == candidates.end() ? nullptr : &*it;
}

const SizeOpportunityReport* find_size_opportunity(const CompileResult& result,
                                                   const std::string& variant) {
  const auto it = std::find_if(result.size_attribution.opportunities.begin(),
                               result.size_attribution.opportunities.end(),
                               [&](const SizeOpportunityReport& opportunity) {
                                 return opportunity.variant == variant;
                               });
  return it == result.size_attribution.opportunities.end() ? nullptr : &*it;
}

const SizeHelperSummaryReport* find_size_helper(const CompileResult& result,
                                                const std::string& label) {
  const auto it = std::find_if(result.size_attribution.helpers.begin(),
                               result.size_attribution.helpers.end(),
                               [&](const SizeHelperSummaryReport& helper) {
                                 return helper.label == label;
                               });
  return it == result.size_attribution.helpers.end() ? nullptr : &*it;
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
  const SizeHelperSummaryReport* direct_accumulator_helper =
      find_size_helper(direct_expression_sum, "packed_score accumulator helper");
  require(direct_accumulator_helper != nullptr,
          "direct packed_score expression should expose accumulator helper size summary");
  require(direct_accumulator_helper->details.contains("role") &&
              direct_accumulator_helper->details.at("role") == "packed-score-accumulator" &&
              direct_accumulator_helper->details.contains("accumulatorTerms") &&
              direct_accumulator_helper->details.at("accumulatorTerms") == "3" &&
              direct_accumulator_helper->details.contains("selectionThresholdTerms") &&
              direct_accumulator_helper->details.at("selectionThresholdTerms") == "3" &&
              direct_accumulator_helper->details.contains("pipelineShape") &&
              direct_accumulator_helper->details.at("pipelineShape") ==
                  "expression-accumulator" &&
              direct_accumulator_helper->details.contains("bodyCells") &&
              direct_accumulator_helper->details.contains("callSiteCells") &&
              direct_accumulator_helper->details.contains("callCellsPerOccurrence"),
          "direct packed_score helper summary should expose accumulator cost and threshold details");

  const CompileResult repeated_expression_sum = compile_source(R"mkpro(
program PackedScoreRepeatedExpressionAccumulator {
  state {
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
  }
  loop {
    preview(sum(packed_score(a, 1), packed_score(b, 2), packed_score(c, 3)))
    halt(sum(packed_score(a, 1), packed_score(b, 2), packed_score(c, 3)))
  }
}
)mkpro",
                                                         pinned_options());
  require(repeated_expression_sum.implemented,
          "native compiler should lower repeated packed_score sums");
  require(repeated_expression_sum.diagnostics.empty(),
          "repeated packed_score sums should not report diagnostics");
  require(has_optimization(repeated_expression_sum, "packed-score-sum-accumulator"),
          "repeated packed_score sums should prefer accumulator lowering");
  require(count_optimization(repeated_expression_sum, "expression-helper-call") == 0,
          "repeated packed_score sums should not be captured by generic expression helpers");
  require(count_optimization(repeated_expression_sum, "packed-score-stack-helper-call") == 0,
          "repeated packed_score sums should not use standalone packed_score helper calls");
  require(count_packed_score_helper_jumps(repeated_expression_sum) == 0,
          "repeated packed_score sums should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(repeated_expression_sum) == 6,
          "repeated packed_score sums should emit accumulator helper calls for both sums");

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

  const CompileResult direct_expression_sum_with_negative_initial = compile_source(R"mkpro(
program PackedScoreDirectExpressionAccumulatorWithNegativeInitial {
  state {
    penalty: packed = 7
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
  }
  loop {
    halt(packed_score(a, 1) + packed_score(b, 2) + packed_score(c, 3) - penalty)
  }
}
)mkpro",
                                                                            pinned_options());

  require(direct_expression_sum_with_negative_initial.implemented,
          "native compiler should lower packed_score expressions with a negative initial addend");
  require(direct_expression_sum_with_negative_initial.diagnostics.empty(),
          "negative-initial packed_score expression compile should not report diagnostics");
  require(has_optimization(direct_expression_sum_with_negative_initial,
                           "packed-score-sum-accumulator"),
          "negative-initial packed_score expression should report accumulator lowering");
  require(count_optimization(direct_expression_sum_with_negative_initial,
                             "packed-score-stack-helper-call") == 0,
          "negative-initial packed_score expression should not use standalone helper calls");
  require(count_packed_score_helper_jumps(direct_expression_sum_with_negative_initial) == 0,
          "negative-initial packed_score expression should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(
              direct_expression_sum_with_negative_initial) == 3,
          "negative-initial packed_score expression should emit three accumulator helper calls");
  require(count_steps_with_comment(direct_expression_sum_with_negative_initial,
                                   "packed_score accumulator zero") == 0,
          "negative-initial packed_score expression should use -penalty as accumulator");

  const CompileResult direct_expression_sum_with_indexed_initial = compile_source(R"mkpro(
program PackedScoreDirectExpressionAccumulatorWithIndexedInitial {
  state {
    bonus: packed[1..4] = [1, 2, 3, 4]
    selector: counter 1..4 = 2
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
  }
  loop {
    halt(sum(bonus[selector], packed_score(a, 1), packed_score(b, 2), packed_score(c, 3)))
  }
}
)mkpro",
                                                                           pinned_options());

  require(direct_expression_sum_with_indexed_initial.implemented,
          "native compiler should lower packed_score expressions with an indexed initial addend");
  require(direct_expression_sum_with_indexed_initial.diagnostics.empty(),
          "indexed-initial packed_score expression compile should not report diagnostics");
  require(has_optimization(direct_expression_sum_with_indexed_initial,
                           "packed-score-sum-accumulator"),
          "indexed-initial packed_score expression should report accumulator lowering");
  require(count_optimization(direct_expression_sum_with_indexed_initial,
                             "packed-score-stack-helper-call") == 0,
          "indexed-initial packed_score expression should not use standalone helper calls");
  require(count_packed_score_helper_jumps(direct_expression_sum_with_indexed_initial) == 0,
          "indexed-initial packed_score expression should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(
              direct_expression_sum_with_indexed_initial) == 3,
          "indexed-initial packed_score expression should emit three accumulator helper calls");
  require(count_steps_with_comment(direct_expression_sum_with_indexed_initial,
                                   "packed_score accumulator zero") == 0,
          "indexed-initial packed_score expression should use the addend as accumulator");

  const CompileResult direct_expression_sum_with_late_indexed_initial = compile_source(R"mkpro(
program PackedScoreDirectExpressionAccumulatorWithLateIndexedInitial {
  state {
    bonus: packed[1..4] = [1, 2, 3, 4]
    selector: counter 1..4 = 2
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
  }
  loop {
    halt(sum(packed_score(a, 1), packed_score(b, 2), bonus[selector], packed_score(c, 3)))
  }
}
)mkpro",
                                                                                pinned_options());

  require(direct_expression_sum_with_late_indexed_initial.implemented,
          "native compiler should lower packed_score expressions with a late indexed initial "
          "addend");
  require(direct_expression_sum_with_late_indexed_initial.diagnostics.empty(),
          "late-indexed-initial packed_score expression compile should not report diagnostics");
  require(has_optimization(direct_expression_sum_with_late_indexed_initial,
                           "packed-score-sum-accumulator"),
          "late-indexed-initial packed_score expression should report accumulator lowering");
  require(count_optimization(direct_expression_sum_with_late_indexed_initial,
                             "packed-score-stack-helper-call") == 0,
          "late-indexed-initial packed_score expression should not use standalone helper calls");
  require(count_packed_score_helper_jumps(direct_expression_sum_with_late_indexed_initial) == 0,
          "late-indexed-initial packed_score expression should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(
              direct_expression_sum_with_late_indexed_initial) == 3,
          "late-indexed-initial packed_score expression should emit three accumulator helper "
          "calls");
  require(count_steps_with_comment(direct_expression_sum_with_late_indexed_initial,
                                   "packed_score accumulator zero") == 0,
          "late-indexed-initial packed_score expression should use the indexed addend as "
          "accumulator");

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

  const CompileResult direct_expression_sum_with_dynamic_bank_line = compile_source(R"mkpro(
program PackedScoreDirectExpressionAccumulatorWithDynamicBankLine {
  state {
    lines: packed[1..3] = [44444.4, 44445.4, 44446.4]
    selector: counter 1..3 = 2
    slot: counter 0..7 = 4
  }
  loop {
    halt(sum(packed_score(lines[selector], slot), packed_score(lines[selector], slot + 1), packed_score(lines[selector], slot + 2)))
  }
}
)mkpro",
                                                                                 pinned_options());

  require(direct_expression_sum_with_dynamic_bank_line.implemented,
          "native compiler should lower packed_score sums with dynamic bank line expressions");
  require(direct_expression_sum_with_dynamic_bank_line.diagnostics.empty(),
          "dynamic-bank-line packed_score compile should not report diagnostics");
  require(has_optimization(direct_expression_sum_with_dynamic_bank_line,
                           "packed-score-sum-accumulator"),
          "dynamic-bank-line packed_score sum should report accumulator lowering");
  require(count_optimization(direct_expression_sum_with_dynamic_bank_line,
                             "packed-score-stack-helper-call") == 0,
          "dynamic-bank-line packed_score sum should not use standalone helper calls");
  require(count_packed_score_helper_jumps(direct_expression_sum_with_dynamic_bank_line) == 0,
          "dynamic-bank-line packed_score sum should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(
              direct_expression_sum_with_dynamic_bank_line) == 3,
          "dynamic-bank-line packed_score sum should emit three accumulator helper calls");

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

  const CompileResult continuation_with_initial = compile_source(R"mkpro(
program PackedScoreContinuationInitialAccumulator {
  state {
    line: packed = 44444.4
    slot_a: counter 0..7 = 4
    slot_b: counter 0..7 = 5
    slot_c: counter 0..7 = 6
    slot_d: counter 0..7 = 7
    score: packed = 0
    bonus_a: packed = 2
    bonus_b: packed = 3
  }
  loop {
    score = packed_score(line, slot_a)
    score = sum(score, bonus_a, packed_score(line, slot_b))
    score += sum(bonus_b, packed_score(line, slot_c), packed_score(line, slot_d))
    halt(score)
  }
}
)mkpro",
                                                               pinned_options());
  require(continuation_with_initial.implemented,
          "native compiler should lower continuation packed_score accumulators with initial "
          "addends");
  require(continuation_with_initial.diagnostics.empty(),
          "continuation packed_score initial addends should not report diagnostics");
  require(has_optimization(continuation_with_initial,
                           "packed-score-sequence-stack-accumulator"),
          "continuation packed_score initial addends should report sequence stack lowering");
  require(count_optimization(continuation_with_initial, "packed-score-stack-helper-call") == 0,
          "continuation packed_score initial addends should not use standalone helper calls");
  require(count_packed_score_helper_jumps(continuation_with_initial) == 0,
          "continuation packed_score initial addends should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(continuation_with_initial) == 4,
          "continuation packed_score initial addends should emit four accumulator helper calls");
  require(count_steps_with_comment(continuation_with_initial, "set score") == 0,
          "continuation initial addends should keep score on the stack for the direct consumer");
  require(count_steps_with_comment(continuation_with_initial, "recall score") == 0,
          "continuation initial addends should not recall the stack-resident score");
  require(count_steps_with_comment(continuation_with_initial, "recall bonus_a") == 1 &&
              count_steps_with_comment(continuation_with_initial, "recall bonus_b") == 1,
          "continuation initial addends should fold both bonuses into the initial accumulator");

  const CompileResult continuation_with_negative_initial = compile_source(R"mkpro(
program PackedScoreContinuationNegativeInitialAccumulator {
  state {
    line: packed = 44444.4
    slot_a: counter 0..7 = 4
    slot_b: counter 0..7 = 5
    slot_c: counter 0..7 = 6
    score: packed = 0
    penalty: packed = 2
  }
  loop {
    score = packed_score(line, slot_a)
    score = score + packed_score(line, slot_b) - penalty
    score += packed_score(line, slot_c)
    halt(score)
  }
}
)mkpro",
                                                                        pinned_options());
  require(continuation_with_negative_initial.implemented,
          "native compiler should lower continuation packed_score accumulators with negative "
          "initial addends");
  require(continuation_with_negative_initial.diagnostics.empty(),
          "continuation packed_score negative addends should not report diagnostics");
  require(has_optimization(continuation_with_negative_initial,
                           "packed-score-sequence-stack-accumulator"),
          "continuation packed_score negative addends should report sequence stack lowering");
  require(count_optimization(continuation_with_negative_initial,
                             "packed-score-stack-helper-call") == 0,
          "continuation packed_score negative addends should not use standalone helper calls");
  require(count_packed_score_helper_jumps(continuation_with_negative_initial) == 0,
          "continuation packed_score negative addends should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(continuation_with_negative_initial) == 3,
          "continuation packed_score negative addends should emit three accumulator helper calls");
  require(count_steps_with_comment(continuation_with_negative_initial, "set score") == 0,
          "continuation negative addends should keep score on the stack for the direct consumer");
  require(count_steps_with_comment(continuation_with_negative_initial, "recall score") == 0,
          "continuation negative addends should not recall the stack-resident score");
  require(count_steps_with_comment(continuation_with_negative_initial, "recall penalty") == 1,
          "continuation negative addends should fold the penalty into the initial accumulator");

  const CompileResult continuation_with_standalone_addends = compile_source(R"mkpro(
program PackedScoreContinuationStandaloneAddendsAccumulator {
  state {
    line: packed = 44444.4
    slot_a: counter 0..7 = 4
    slot_b: counter 0..7 = 5
    slot_c: counter 0..7 = 6
    score: packed = 0
    bonus: packed = 3
    penalty: packed = 2
  }
  loop {
    score = packed_score(line, slot_a)
    score += bonus
    score += packed_score(line, slot_b)
    score -= penalty
    score = score + packed_score(line, slot_c)
    halt(score)
  }
}
)mkpro",
                                                                         pinned_options());
  require(continuation_with_standalone_addends.implemented,
          "native compiler should keep standalone add/sub updates inside packed_score "
          "accumulator runs");
  require(continuation_with_standalone_addends.diagnostics.empty(),
          "standalone packed_score accumulator addends should not report diagnostics");
  require(has_optimization(continuation_with_standalone_addends,
                           "packed-score-sequence-stack-accumulator"),
          "standalone addends should still report sequence stack lowering");
  require(count_optimization(continuation_with_standalone_addends,
                             "packed-score-stack-helper-call") == 0,
          "standalone addends should not force standalone packed_score helper calls");
  require(count_packed_score_helper_jumps(continuation_with_standalone_addends) == 0,
          "standalone addends should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(continuation_with_standalone_addends) == 3,
          "standalone addends should preserve the three accumulator helper calls");
  require(count_steps_with_comment(continuation_with_standalone_addends,
                                   "packed_score accumulator addend") == 2,
          "standalone add/sub updates should be emitted as ordered accumulator addends");
  require(count_steps_with_comment(continuation_with_standalone_addends, "set score") == 0,
          "standalone addends should keep score on the stack for the direct consumer");
  require(count_steps_with_comment(continuation_with_standalone_addends, "recall score") == 0,
          "standalone addends should not recall the stack-resident score");
  require(count_steps_with_comment(continuation_with_standalone_addends, "recall bonus") == 1 &&
              count_steps_with_comment(continuation_with_standalone_addends, "recall penalty") ==
                  1,
          "standalone addends should load each addend once inside the accumulator pipeline");

  const CompileResult unsafe_standalone_addend = compile_source(R"mkpro(
program PackedScoreContinuationUnsafeStandaloneAddend {
  state {
    line: packed = 44444.4
    slot_a: counter 0..7 = 4
    slot_b: counter 0..7 = 5
    slot_c: counter 0..7 = 6
    bonus_bank: packed[1..3] = [3, 4, 5]
    pick: counter 1..3 = 2
    score: packed = 0
  }
  loop {
    score = packed_score(line, slot_a)
    score += bonus_bank[pick]
    score += packed_score(line, slot_b)
    score += packed_score(line, slot_c)
    halt(score)
  }
}
)mkpro",
                                                                 pinned_options());
  require(unsafe_standalone_addend.implemented,
          "native compiler should still compile accumulator runs split by unsafe addends");
  require(unsafe_standalone_addend.diagnostics.empty(),
          "unsafe standalone addend split should not report diagnostics");
  require(count_steps_with_comment(unsafe_standalone_addend,
                                   "packed_score accumulator addend") == 0,
          "dynamic indexed addends should not be emitted as ordered accumulator addends");

  const CompileResult nested_binary_continuation = compile_source(R"mkpro(
program PackedScoreNestedBinaryContinuation {
  state {
    line: packed = 44444.4
    slot_a: counter 0..7 = 4
    slot_b: counter 0..7 = 5
    slot_c: counter 0..7 = 6
    score: packed = 0
  }
  loop {
    score = packed_score(line, slot_a)
    score = score + packed_score(line, slot_b) + packed_score(line, slot_c)
    halt(score)
  }
}
)mkpro",
                                                                pinned_options());
  require(nested_binary_continuation.implemented,
          "native compiler should lower nested binary packed_score continuations");
  require(nested_binary_continuation.diagnostics.empty(),
          "nested binary packed_score continuation should not report diagnostics");
  require(has_optimization(nested_binary_continuation,
                           "packed-score-sequence-stack-accumulator"),
          "nested binary packed_score continuation should report sequence accumulator lowering");
  require(count_optimization(nested_binary_continuation, "packed-score-stack-helper-call") == 0,
          "nested binary packed_score continuation should not use standalone helper calls");
  require(count_packed_score_helper_jumps(nested_binary_continuation) == 0,
          "nested binary packed_score continuation should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(nested_binary_continuation) == 3,
          "nested binary packed_score continuation should emit three accumulator helper calls");
  require(count_steps_with_comment(nested_binary_continuation, "set score") == 0,
          "nested binary accumulator should keep score on the stack for the direct consumer");
  require(count_steps_with_comment(nested_binary_continuation, "recall score") == 0,
          "nested binary continuation should reuse the stack accumulator instead of recalling it");

  const CompileResult self_assignment_accumulator = compile_source(R"mkpro(
program PackedScoreSelfAssignmentAccumulator {
  state {
    line: packed = 44444.4
    slot_a: counter 0..7 = 4
    slot_b: counter 0..7 = 5
    slot_c: counter 0..7 = 6
    score: packed = 7
  }
  loop {
    score = score + packed_score(line, slot_a) + packed_score(line, slot_b) + packed_score(line, slot_c)
    halt(score)
  }
}
)mkpro",
                                                                pinned_options());
  require(self_assignment_accumulator.implemented,
          "native compiler should lower self-referential packed_score accumulator assignment");
  require(self_assignment_accumulator.diagnostics.empty(),
          "self-referential packed_score accumulator should not report diagnostics");
  require(has_optimization(self_assignment_accumulator,
                           "packed-score-assignment-stack-accumulator"),
          "self-referential packed_score accumulator should report assignment stack lowering");
  require(count_optimization(self_assignment_accumulator, "packed-score-stack-helper-call") == 0,
          "self-referential packed_score accumulator should not use standalone helper calls");
  require(count_packed_score_helper_jumps(self_assignment_accumulator) == 0,
          "self-referential packed_score accumulator should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(self_assignment_accumulator) == 3,
          "self-referential packed_score accumulator should emit three accumulator helper calls");
  require(count_steps_with_comment(self_assignment_accumulator, "set score") == 0,
          "self-referential accumulator should keep score on the stack for the direct consumer");
  require(count_steps_with_comment(self_assignment_accumulator, "recall score") == 1,
          "self-referential accumulator should recall the incoming score exactly once");

  const CompileResult self_assignment_with_initial = compile_source(R"mkpro(
program PackedScoreSelfAssignmentInitialAccumulator {
  state {
    line: packed = 44444.4
    slot_a: counter 0..7 = 4
    slot_b: counter 0..7 = 5
    slot_c: counter 0..7 = 6
    score: packed = 7
    bonus: packed = 3
  }
  loop {
    score = sum(score, bonus, packed_score(line, slot_a), packed_score(line, slot_b), packed_score(line, slot_c))
    halt(score)
  }
}
)mkpro",
                                                               pinned_options());
  require(self_assignment_with_initial.implemented,
          "native compiler should lower self-referential packed_score assignment with an initial "
          "addend");
  require(self_assignment_with_initial.diagnostics.empty(),
          "self-referential packed_score initial addend should not report diagnostics");
  require(has_optimization(self_assignment_with_initial,
                           "packed-score-assignment-stack-accumulator"),
          "self-referential packed_score initial addend should report assignment stack lowering");
  require(count_optimization(self_assignment_with_initial, "packed-score-stack-helper-call") == 0,
          "self-referential packed_score initial addend should not use standalone helper calls");
  require(count_packed_score_helper_jumps(self_assignment_with_initial) == 0,
          "self-referential packed_score initial addend should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(self_assignment_with_initial) == 3,
          "self-referential packed_score initial addend should emit three accumulator helper calls");
  require(count_steps_with_comment(self_assignment_with_initial, "set score") == 0,
          "self-referential initial addend accumulator should keep score on the stack");
  require(count_steps_with_comment(self_assignment_with_initial, "recall score") == 1,
          "self-referential initial addend should recall the incoming score exactly once");
  require(count_steps_with_comment(self_assignment_with_initial, "recall bonus") == 1,
          "self-referential initial addend should fold bonus into the initial accumulator once");

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

  const CompileResult paid_helper_pair = compile_source(R"mkpro(
program PackedScoreAccumulatorReusesPaidHelperForPair {
  state {
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
    d: packed = 44447.4
    e: packed = 44448.4
    value: packed = 0
    extra: packed = 0
  }
  loop {
    value = packed_score(a, 1) + packed_score(b, 2) + packed_score(c, 3)
    extra = packed_score(d, 4) + packed_score(e, 5)
    halt(value + extra)
  }
}
)mkpro",
                                                     pinned_options());
  require(paid_helper_pair.implemented,
          "native compiler should reuse an already-paid packed_score accumulator helper");
  require(paid_helper_pair.diagnostics.empty(),
          "already-paid packed_score pair compile should not report diagnostics");
  require(has_optimization(paid_helper_pair, "packed-score-accumulator-helper"),
          "already-paid packed_score pair should keep the accumulator helper body");
  require(has_optimization(paid_helper_pair, "packed-score-assignment-stack-accumulator"),
          "already-paid packed_score pair should lower the pair as a stack accumulator");
  require(count_optimization(paid_helper_pair, "packed-score-stack-helper-call") == 0,
          "already-paid packed_score pair should not introduce the standalone helper");
  require(count_packed_score_helper_jumps(paid_helper_pair) == 0,
          "already-paid packed_score pair should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(paid_helper_pair) == 5,
          "already-paid packed_score pair should emit accumulator helper calls for all five "
          "terms");
  require(count_steps_with_comment(paid_helper_pair, "set extra") == 0,
          "already-paid packed_score pair should keep extra on the stack for the direct consumer");

  const CompileResult planned_helper_pair_first = compile_source(R"mkpro(
program PackedScoreAccumulatorReusesPlannedHelperForEarlierPair {
  state {
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
    d: packed = 44447.4
    e: packed = 44448.4
    value: packed = 0
    extra: packed = 0
  }
  loop {
    extra = packed_score(d, 4) + packed_score(e, 5)
    value = packed_score(a, 1) + packed_score(b, 2) + packed_score(c, 3)
    halt(value + extra)
  }
}
)mkpro",
                                                           pinned_options());
  require(planned_helper_pair_first.implemented,
          "native compiler should reuse a planned packed_score accumulator helper before the "
          "3-term group creates it");
  require(planned_helper_pair_first.diagnostics.empty(),
          "planned packed_score pair compile should not report diagnostics");
  require(has_optimization(planned_helper_pair_first, "packed-score-accumulator-helper"),
          "planned packed_score pair should keep the accumulator helper body");
  require(has_optimization(planned_helper_pair_first, "packed-score-sum-accumulator"),
          "planned packed_score pair should lower the earlier pair as an expression accumulator");
  require(count_optimization(planned_helper_pair_first, "packed-score-stack-helper-call") == 0,
          "planned packed_score pair should not introduce the standalone helper");
  require(count_packed_score_helper_jumps(planned_helper_pair_first) == 0,
          "planned packed_score pair should not emit standalone helper calls");
  require(count_packed_score_accumulator_helper_jumps(planned_helper_pair_first) == 5,
          "planned packed_score pair should emit accumulator helper calls for all five terms");
  require(count_steps_with_comment(planned_helper_pair_first, "set extra") == 1,
          "planned packed_score pair should store the earlier extra once before the later "
          "accumulator overwrites X/Y");

  CompileOptions pair_search_options;
  pair_search_options.analysis = true;
  pair_search_options.budget = 999999;
  pair_search_options.disable_aggressive_post_layout = true;
  const CompileResult searched_pair_groups = compile_source(R"mkpro(
program PackedScoreAccumulatorSearchesRepeatedPairs {
  state {
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
    d: packed = 44447.4
    e: packed = 44448.4
    f: packed = 44449.4
    g: packed = 44450.4
    h: packed = 44451.4
    first: packed = 0
    second: packed = 0
    third: packed = 0
    fourth: packed = 0
  }
  loop {
    first = packed_score(a, 1) + packed_score(b, 2)
    second = packed_score(c, 3) + packed_score(d, 4)
    third = packed_score(e, 5) + packed_score(f, 6)
    fourth = packed_score(g, 1) + packed_score(h, 2)
    halt(first + second + third + fourth)
  }
}
)mkpro",
                                                        pair_search_options);
  require(searched_pair_groups.implemented,
          "analysis candidate search should compile repeated packed_score pairs");
  require(searched_pair_groups.diagnostics.empty(),
          "analysis candidate search should not report diagnostics for repeated packed_score "
          "pairs");
  const CandidateReport* rejected_pair_accumulator =
      find_candidate(searched_pair_groups.rejected_candidates, "packed-score-accumulator-helper");
  require(rejected_pair_accumulator != nullptr,
          "analysis candidate search should consider the accumulator helper for repeated "
          "packed_score pairs");
  require(rejected_pair_accumulator->steps >= static_cast<int>(searched_pair_groups.steps.size()),
          "repeated packed_score pair accumulator candidate should not hide a smaller listing");
  const SizeOpportunityReport* pair_accumulator_opportunity =
      find_size_opportunity(searched_pair_groups, "packed-score-accumulator-helper");
  require(pair_accumulator_opportunity != nullptr,
          "size attribution should surface the repeated-pair packed_score accumulator candidate");
  require(pair_accumulator_opportunity->details.contains("candidateStepsStatus") &&
              pair_accumulator_opportunity->details.at("candidateStepsStatus") ==
                  "larger-than-current",
          "repeated-pair accumulator candidate should report that it is larger than current");
  const CandidateReport* rejected_pair_accumulator_stack_entries =
      find_candidate(searched_pair_groups.rejected_candidates,
                     "packed-score-accumulator-stack-helper-entries");
  require(rejected_pair_accumulator_stack_entries != nullptr,
          "analysis candidate search should consider packed_score accumulator helpers together "
          "with stack-resident helper entries");
  require(rejected_pair_accumulator_stack_entries->steps >=
              static_cast<int>(searched_pair_groups.steps.size()),
          "combined packed_score/stack-entry candidate should not hide a smaller listing");

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

  const CompileResult x_param_mixed_prefix_sequence = compile_source(R"mkpro(
program PackedScoreXParamMixedPrefixAccumulatorHelper {
  state {
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
    d: packed = 44447.4
    bonus: packed = 3
    x: counter 0..7 = 2
    y: counter 0..7 = 3
    line: packed = 0
    score: packed = 0
  }
  loop {
    score = 0
    normalize(x + y)
    score += sum(bonus, packed_score(a, 1), packed_score(b, line))
    normalize(x - y)
    score = score + packed_score(c, 2) + packed_score(d, line)
    halt(score)
  }

  fn normalize(raw_line) {
    line = frac((raw_line + 3) / 4) * 4 + 1
  }
}
)mkpro",
                                                                  pinned_options());
  require(x_param_mixed_prefix_sequence.implemented,
          "x-param mixed-prefix packed_score accumulator-helper program should compile");
  require(x_param_mixed_prefix_sequence.diagnostics.empty(),
          "x-param mixed-prefix packed_score compile should not report diagnostics");
  require(count_optimization(x_param_mixed_prefix_sequence,
                             "x-param-packed-score-line-stack-accumulate") == 2,
          "x-param mixed-prefix packed_score sequence should keep both returned-index updates "
          "stack-carried");
  require(count_packed_score_accumulator_helper_jumps(x_param_mixed_prefix_sequence) == 4,
          "x-param mixed-prefix packed_score sequence should call the accumulator helper for "
          "prefix and returned-index terms");
  require(count_packed_score_helper_jumps(x_param_mixed_prefix_sequence) == 0,
          "x-param mixed-prefix packed_score sequence should not use the standalone helper "
          "fallback");
  require(count_steps_with_comment(x_param_mixed_prefix_sequence,
                                   "packed_score stack accumulator") == 0,
          "x-param mixed-prefix packed_score sequence should not need separate accumulator add "
          "cells");
  require(count_steps_with_comment(x_param_mixed_prefix_sequence, "set line") == 0 &&
              count_steps_with_comment(x_param_mixed_prefix_sequence, "recall line") == 0,
          "x-param mixed-prefix packed_score sequence should keep returned indexes stack-only");

  const CompileResult x_param_expression_line_sequence = compile_source(R"mkpro(
program PackedScoreXParamExpressionLineAccumulatorHelper {
  state {
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
    offset_a: packed = 1
    offset_b: packed = 2
    offset_c: packed = 3
    x: counter 0..7 = 2
    y: counter 0..7 = 3
    line: packed = 0
    score: packed = 0
  }
  loop {
    score = 0
    normalize(x + y)
    score += packed_score(a + offset_a, line)
    normalize(x - y)
    score += packed_score(b + offset_b, line)
    normalize(y + 1)
    score += packed_score(c + offset_c, line)
    halt(score)
  }

  fn normalize(raw_line) {
    line = frac((raw_line + 3) / 4) * 4 + 1
  }
}
)mkpro",
                                                                 pinned_options());
  require(x_param_expression_line_sequence.implemented,
          "x-param packed_score expression-line accumulator-helper program should compile");
  require(x_param_expression_line_sequence.diagnostics.empty(),
          "x-param packed_score expression-line compile should not report diagnostics");
  require(count_optimization(x_param_expression_line_sequence,
                             "x-param-packed-score-line-stack-accumulate") == 3,
          "x-param packed_score expression-line sequence should keep returned-index updates "
          "stack-carried");
  require(count_packed_score_accumulator_helper_jumps(x_param_expression_line_sequence) == 3,
          "x-param packed_score expression-line sequence should call the accumulator helper for "
          "each term");
  require(count_packed_score_helper_jumps(x_param_expression_line_sequence) == 0,
          "x-param packed_score expression-line sequence should not use the standalone helper "
          "fallback");
  require(count_steps_with_comment(x_param_expression_line_sequence,
                                   "packed_score stack accumulator") == 0,
          "x-param packed_score expression-line sequence should not need separate accumulator add "
          "cells");

  const CompileResult x_param_returned_index_expression_sequence = compile_source(R"mkpro(
program PackedScoreXParamReturnedIndexExpressionAccumulatorHelper {
  state {
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
    offset_a: packed = 1
    offset_b: packed = 2
    offset_c: packed = 3
    x: counter 0..7 = 2
    y: counter 0..7 = 3
    line: packed = 0
    score: packed = 0
  }
  loop {
    score = 0
    normalize(x + y)
    score += packed_score(a + offset_a, line + 1)
    normalize(x - y)
    score += packed_score(b + offset_b, line + 1)
    normalize(y + 1)
    score += packed_score(c + offset_c, line + 1)
    halt(score)
  }

  fn normalize(raw_line) {
    line = frac((raw_line + 3) / 4) * 4 + 1
  }
}
)mkpro",
                                                                            pinned_options());
  require(x_param_returned_index_expression_sequence.implemented,
          "x-param packed_score returned-index expression accumulator-helper program should "
          "compile");
  require(x_param_returned_index_expression_sequence.diagnostics.empty(),
          "x-param packed_score returned-index expression compile should not report diagnostics");
  require(count_optimization(x_param_returned_index_expression_sequence,
                             "x-param-packed-score-line-stack-accumulate") == 3,
          "x-param packed_score returned-index expression sequence should keep returned-index "
          "updates stack-carried");
  require(count_packed_score_accumulator_helper_jumps(
              x_param_returned_index_expression_sequence) == 3,
          "x-param packed_score returned-index expression sequence should call the accumulator "
          "helper for each term");
  require(count_packed_score_helper_jumps(x_param_returned_index_expression_sequence) == 0,
          "x-param packed_score returned-index expression sequence should not use the standalone "
          "helper fallback");
  require(count_steps_with_comment(x_param_returned_index_expression_sequence,
                                   "packed_score stack accumulator") == 0,
          "x-param packed_score returned-index expression sequence should not need separate "
          "accumulator add cells");
  require(count_steps_with_comment(x_param_returned_index_expression_sequence, "set line") == 0 &&
              count_steps_with_comment(x_param_returned_index_expression_sequence,
                                       "recall line") == 0,
          "x-param packed_score returned-index expression sequence should keep returned indexes "
          "stack-only");

  const CompileResult x_param_initial_addend_sequence = compile_source(R"mkpro(
program PackedScoreXParamInitialAddendAccumulatorHelper {
  state {
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
    bonus_a: packed = 1
    bonus_b: packed = 2
    bonus_c: packed = 3
    offset_a: packed = 1
    offset_b: packed = 2
    offset_c: packed = 3
    x: counter 0..7 = 2
    y: counter 0..7 = 3
    line: packed = 0
    score: packed = 0
  }
  loop {
    score = 0
    normalize(x + y)
    score += sum(bonus_a, packed_score(a + offset_a, line))
    normalize(x - y)
    score = sum(score, bonus_b, packed_score(b + offset_b, line))
    normalize(y + 1)
    score = score + bonus_c + packed_score(c + offset_c, line)
    halt(score)
  }

  fn normalize(raw_line) {
    line = frac((raw_line + 3) / 4) * 4 + 1
  }
}
)mkpro",
                                                                 pinned_options());
  require(x_param_initial_addend_sequence.implemented,
          "x-param packed_score initial-addend accumulator-helper program should compile");
  require(x_param_initial_addend_sequence.diagnostics.empty(),
          "x-param packed_score initial-addend compile should not report diagnostics");
  require(count_optimization(x_param_initial_addend_sequence,
                             "x-param-packed-score-line-stack-accumulate") == 3,
          "x-param packed_score initial-addend sequence should keep returned-index updates "
          "stack-carried");
  require(count_packed_score_accumulator_helper_jumps(x_param_initial_addend_sequence) == 3,
          "x-param packed_score initial-addend sequence should call the accumulator helper for "
          "each term");
  require(count_packed_score_helper_jumps(x_param_initial_addend_sequence) == 0,
          "x-param packed_score initial-addend sequence should not use the standalone helper "
          "fallback");
  require(count_steps_with_comment(x_param_initial_addend_sequence,
                                   "packed_score stack accumulator") == 0,
          "x-param packed_score initial-addend sequence should not need separate accumulator add "
          "cells");

  const CompileResult x_param_negative_initial_addend_sequence = compile_source(R"mkpro(
program PackedScoreXParamNegativeInitialAddendAccumulatorHelper {
  state {
    a: packed = 44444.4
    b: packed = 44445.4
    c: packed = 44446.4
    bonus_a: packed = 7
    bonus_b: packed = 8
    bonus_c: packed = 9
    penalty_a: packed = 1
    penalty_b: packed = 2
    penalty_c: packed = 3
    offset_a: packed = 1
    offset_b: packed = 2
    offset_c: packed = 3
    x: counter 0..7 = 2
    y: counter 0..7 = 3
    line: packed = 0
    score: packed = 0
  }
  loop {
    score = 0
    normalize(x + y)
    score += sum(bonus_a - penalty_a, packed_score(a + offset_a, line))
    normalize(x - y)
    score = sum(score, bonus_b - penalty_b, packed_score(b + offset_b, line))
    normalize(y + 1)
    score = score + bonus_c - penalty_c + packed_score(c + offset_c, line)
    halt(score)
  }

  fn normalize(raw_line) {
    line = frac((raw_line + 3) / 4) * 4 + 1
  }
}
)mkpro",
                                                                                 pinned_options());
  require(x_param_negative_initial_addend_sequence.implemented,
          "x-param packed_score negative-initial accumulator-helper program should compile");
  require(x_param_negative_initial_addend_sequence.diagnostics.empty(),
          "x-param packed_score negative-initial compile should not report diagnostics");
  require(count_optimization(x_param_negative_initial_addend_sequence,
                             "x-param-packed-score-line-stack-accumulate") == 3,
          "x-param packed_score negative-initial sequence should keep returned-index updates "
          "stack-carried");
  require(count_packed_score_accumulator_helper_jumps(
              x_param_negative_initial_addend_sequence) == 3,
          "x-param packed_score negative-initial sequence should call the accumulator helper for "
          "each term");
  require(count_packed_score_helper_jumps(x_param_negative_initial_addend_sequence) == 0,
          "x-param packed_score negative-initial sequence should not use the standalone helper "
          "fallback");
  require(count_steps_with_comment(x_param_negative_initial_addend_sequence,
                                   "packed_score stack accumulator") == 0,
          "x-param packed_score negative-initial sequence should not need separate accumulator add "
          "cells");

  const CompileResult mixed = compile_source(R"mkpro(
program PackedScoreMixedAccumulatorSingletonReuse {
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
  require(mixed.implemented, "mixed accumulator/singleton program should compile");
  require(mixed.diagnostics.empty(),
          "mixed accumulator/singleton program should not report diagnostics");
  require(has_optimization(mixed, "packed-score-accumulator-helper"),
          "mixed packed_score should keep the accumulator helper body");
  require(!has_optimization(mixed, "packed-score-stack-helper"),
          "mixed packed_score should not need the standalone helper fallback");
  require(count_optimization(mixed, "packed-score-stack-helper-call") == 0,
          "mixed packed_score should not call the standalone helper fallback");
  require(count_packed_score_accumulator_helper_jumps(mixed) == 4,
          "mixed packed_score should emit accumulator helper calls for the three-term group and "
          "the singleton");
  require(count_packed_score_helper_jumps(mixed) == 0,
          "mixed packed_score should not emit standalone helper calls");

  CompileOptions search_options;
  search_options.analysis = true;
  search_options.budget = 999999;
  const CompileResult searched_mixed = compile_source(R"mkpro(
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
                                                    search_options);
  require(searched_mixed.implemented,
          "analysis candidate search should compile the mixed packed_score program");
  require(searched_mixed.diagnostics.empty(),
          "analysis candidate search should not report diagnostics for mixed packed_score");
  const CandidateReport* rejected_accumulator = find_candidate(
      searched_mixed.rejected_candidates, "packed-score-accumulator-aggressive-post-layout");
  require(rejected_accumulator != nullptr,
          "analysis should keep the nonwinning packed_score accumulator candidate visible");
  require(rejected_accumulator->steps >= static_cast<int>(searched_mixed.steps.size()),
          "reported packed_score accumulator candidate should not hide a smaller listing");
  require(rejected_accumulator->reason.find("final candidate-search result") != std::string::npos,
          "reported packed_score accumulator candidate should explain that it lost final search");
  const SizeOpportunityReport* accumulator_opportunity =
      find_size_opportunity(searched_mixed, "packed-score-accumulator-aggressive-post-layout");
  require(accumulator_opportunity != nullptr,
          "size attribution should surface the nonwinning packed_score accumulator candidate");
  require(accumulator_opportunity->candidate_steps == rejected_accumulator->steps,
          "size attribution should preserve the packed_score accumulator candidate size");
  require(accumulator_opportunity->details.contains("candidateStepsStatus") &&
              accumulator_opportunity->details.at("candidateStepsStatus") ==
                  "larger-than-current",
          "nonwinning packed_score candidate should report that it is larger than current");
  require(accumulator_opportunity->details.contains("sizeImpactStatus") &&
              accumulator_opportunity->details.at("sizeImpactStatus") ==
                  "negative-rejected-candidate-delta",
          "nonwinning packed_score candidate should report its negative size impact");
  require(accumulator_opportunity->details.contains("requiredAction") &&
              accumulator_opportunity->details.at("requiredAction") ==
                  "keep-current-smaller-candidate",
          "nonwinning packed_score candidate should tell the optimizer to keep the smaller "
          "current candidate");
}

} // namespace mkpro::tests
