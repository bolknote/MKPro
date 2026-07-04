#include "mkpro/compiler.hpp"
#include "mkpro/core/emit/stack_residency_analysis.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

Expression id(std::string name) {
  Expression expression;
  expression.kind = "identifier";
  expression.name = std::move(name);
  return expression;
}

Expression binary(Expression left, std::string op, Expression right) {
  Expression expression;
  expression.kind = "binary";
  expression.op = std::move(op);
  expression.left = std::make_shared<Expression>(std::move(left));
  expression.right = std::make_shared<Expression>(std::move(right));
  return expression;
}

V2Statement assign(std::string target, std::string expr, int line) {
  V2Statement statement;
  statement.kind = "v2_assign";
  statement.target = std::move(target);
  statement.expr = std::move(expr);
  statement.line = line;
  return statement;
}

V2Statement update(std::string target, std::string op, std::string expr, int line) {
  V2Statement statement;
  statement.kind = "v2_update";
  statement.target = std::move(target);
  statement.op = std::move(op);
  statement.expr = std::move(expr);
  statement.line = line;
  return statement;
}

V2Statement empty_if(std::string left, std::string op, std::string right, int line) {
  V2Statement statement;
  statement.kind = "v2_if";
  statement.predicate = V2Predicate{
      .kind = "v2_compare",
      .left = std::move(left),
      .op = std::move(op),
      .right = std::move(right),
  };
  statement.line = line;
  return statement;
}

CompileResult compile_stack_variant(const std::string& source, bool stack_resident_temps = true) {
  CompileOptions options;
  options.budget = 999999;
  options.stack_resident_temps = stack_resident_temps;
  options.disable_candidate_search = true;
  return compile_source(source, options);
}

CompileResult compile_stack_analysis(const std::string& source) {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999999;
  options.stack_resident_temps = true;
  options.disable_candidate_search = true;
  return compile_source(source, options);
}

CompileResult compile_with_candidates(const std::string& source) {
  CompileOptions options;
  options.budget = 999999;
  // This block verifies that candidate search picks the smallest stack-resident
  // dual-temp lowering relative to the non-aggressive baseline/enabled variants
  // (both compiled with disable_candidate_search). The aggressive post-layout
  // indirect-flow rescue is enabled by default and would shrink the selection
  // further, so pin it off to keep the comparison apples-to-apples.
  options.disable_aggressive_post_layout = true;
  return compile_source(source, options);
}

void require_clean_compile(const CompileResult& result, const std::string& context) {
  require(result.implemented, context + " should compile");
  for (const Diagnostic& diagnostic : result.diagnostics) {
    require(diagnostic.severity != DiagnosticSeverity::Error,
            context + " should not report errors: " + diagnostic.message);
  }
}

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) { return item.name == name; });
}

int count_steps_with_comment(const CompileResult& result, const std::string& comment) {
  return static_cast<int>(
      std::count_if(result.steps.begin(), result.steps.end(), [&](const ResolvedStep& step) {
        return step.comment.has_value() && *step.comment == comment;
      }));
}

int count_steps_with_comment_prefix_and_opcode(const CompileResult& result,
                                               const std::string& prefix, int opcode) {
  return static_cast<int>(
      std::count_if(result.steps.begin(), result.steps.end(), [&](const ResolvedStep& step) {
        return step.opcode == opcode && step.comment.has_value() &&
               step.comment->starts_with(prefix);
      }));
}

const SizeAbiBlockerReport* find_size_abi_blocker(const CompileResult& result,
                                                  const std::string& kind,
                                                  const std::string& label) {
  const auto it = std::find_if(result.size_attribution.abi_blockers.begin(),
                               result.size_attribution.abi_blockers.end(),
                               [&](const SizeAbiBlockerReport& blocker) {
                                 return blocker.kind == kind && blocker.label == label;
                               });
  return it == result.size_attribution.abi_blockers.end() ? nullptr : &*it;
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

const CandidateReport* find_candidate(const std::vector<CandidateReport>& candidates,
                                      const std::string& variant) {
  const auto it = std::find_if(candidates.begin(), candidates.end(),
                               [&](const CandidateReport& candidate) {
                                 return candidate.variant == variant;
                               });
  return it == candidates.end() ? nullptr : &*it;
}

const SizeNextActionSummaryReport* find_size_next_action(const CompileResult& result,
                                                         const std::string& source,
                                                         const std::string& action) {
  const auto it = std::find_if(result.size_attribution.next_actions.begin(),
                               result.size_attribution.next_actions.end(),
                               [&](const SizeNextActionSummaryReport& next_action) {
                                 return next_action.source == source &&
                                        next_action.action == action;
                               });
  return it == result.size_attribution.next_actions.end() ? nullptr : &*it;
}

} // namespace

void stack_residency_matches_typescript_contract() {
  using core::emit::can_lower_stack_resident_expression;
  using core::emit::find_stack_resident_fusion_site;
  using core::emit::stack_resident_restore_ops;
  using core::emit::StackResidentRestoreOp;
  using core::emit::statement_preserves_stack_residency;
  using core::emit::summarize_stack_residency_candidates_in_block;

  {
    const std::vector<V2Statement> body = {
        assign("a", "x", 1),
        assign("b", "y", 2),
        assign("c", "a + b", 3),
        empty_if("c", "==", "0", 4),
    };
    const core::emit::StackResidencySummary summary =
        summarize_stack_residency_candidates_in_block(body);
    require(summary.fusion_sites == 1, "stack-residency should detect straight-line fusion");
    require(summary.control_flow_fusions == 0,
            "straight-line fusion should not count as control-flow fusion");
    require(summary.max_live_temps >= 2, "stack-residency should count live temps");
  }

  {
    require(stack_resident_restore_ops(1, 2).empty(),
            "top stack-resident temp should need no restore");
    require(stack_resident_restore_ops(0, 2) ==
                std::vector<StackResidentRestoreOp>{StackResidentRestoreOp::Swap},
            "Y stack-resident temp should restore via swap");
    require(stack_resident_restore_ops(0, 3) ==
                std::vector<StackResidentRestoreOp>{StackResidentRestoreOp::Reverse,
                                                    StackResidentRestoreOp::Swap},
            "Z stack-resident temp should restore via reverse+swap");
    require(stack_resident_restore_ops(0, 4) ==
                std::vector<StackResidentRestoreOp>{StackResidentRestoreOp::Reverse,
                                                    StackResidentRestoreOp::Reverse,
                                                    StackResidentRestoreOp::Swap},
            "T stack-resident temp should restore via reverse+reverse+swap");
  }

  {
    const Expression expr = binary(id("a"), "-", id("b"));
    require(can_lower_stack_resident_expression(expr, {"a", "b"}),
            "stack-residency should accept a binary consumer over two temps");
  }

  {
    const std::vector<V2Statement> body = {
        assign("a", "x", 1),
        assign("b", "y", 2),
        empty_if("x", "==", "0", 3),
        assign("c", "a + b", 4),
    };
    const std::optional<core::emit::StackResidentFusionSite> site =
        find_stack_resident_fusion_site(body, 0);
    require(site.has_value(), "stack-residency should find a control-flow fusion site");
    require(site->crosses_control_flow,
            "stack-residency should mark fusion across stack-preserving control flow");
    require(site->temps.size() == 2, "stack-residency should capture both temps");
    require(site->temps.at(0).assign.target == "a" && site->temps.at(1).assign.target == "b",
            "stack-residency should preserve temp order");
  }

  {
    const std::vector<V2Statement> body = {
        assign("sx", "x", 1),
        assign("sy", "y", 2),
        assign("line", "cell_mask(sx, sy)", 3),
        update("out", "+=", "line", 4),
        assign("sx", "x", 5),
        assign("sy", "y", 6),
        assign("line", "cell_mask(sx, sy)", 7),
    };
    const std::optional<core::emit::StackResidentFusionSite> site =
        find_stack_resident_fusion_site(body, 0);
    require(site.has_value(),
            "stack-residency should treat future reads after overwrite as dead for the current "
            "temp value");
    require(site->consumer_index == 2, "stack-residency should fuse the first cell_mask consumer");
  }

  {
    const V2Statement statement = empty_if("x", "==", "0", 1);
    require(statement_preserves_stack_residency(statement, std::set<std::string>{"a"}),
            "empty if/else should preserve unrelated stack-resident temps");
  }

  const std::string dual_stack = R"mkpro(
program DualStack {
  state {
    x: packed = 1
    y: packed = 2
    z: packed = 0
  }
  loop {
    a = x
    b = y
    z = a + b
    halt(z)
  }
}
)mkpro";

  {
    const CompileResult baseline = compile_stack_variant(dual_stack, false);
    const CompileResult enabled = compile_stack_variant(dual_stack, true);
    const CompileResult selected = compile_with_candidates(dual_stack);
    require_clean_compile(baseline, "baseline dual-temp program");
    require_clean_compile(enabled, "stack-resident dual-temp program");
    require_clean_compile(selected, "candidate-selected dual-temp program");
    require(enabled.steps.size() <= baseline.steps.size(),
            "stack-resident dual-temp program should not grow versus baseline");
    require(selected.steps.size() == std::min(baseline.steps.size(), enabled.steps.size()),
            "candidate search should select the smallest supported dual-temp lowering");
    if (enabled.steps.size() < baseline.steps.size()) {
      require(has_optimization(selected, "stack-resident-temps"),
              "candidate search should report selected stack-resident-temps");
    }
  }

  {
    const CompileResult result = compile_stack_variant(dual_stack, true);
    require_clean_compile(result, "stack-resident dual-temp add");
    require(has_optimization(result, "stack-resident-temps"),
            "stack-resident dual-temp add should report stack-resident-temps");
    require(result.steps.size() < 20, "stack-resident dual-temp add should stay compact");
  }

  {
    const std::string stack_helper_abi_source = R"mkpro(
program StackHelperAbiAggregation {
  state {
    x: counter 1..4 = 1
    y: counter 1..4 = 2
    sx: counter 1..4 = 1
    sy: counter 1..4 = 1
    line: packed = 0
    out: packed = 0
  }

  loop {
    part_a()
    part_b()
    part_c()
    halt(out)
  }

  fn part_a() {
    sx = x
    sy = y
    line = cell_mask(sx, sy)
    out += line
    sx = x
    sy = y
    line = cell_mask(sx, sy)
    out += line
  }

  fn part_b() {
    sx = x
    sy = y
    line = cell_mask(sx, sy)
    out += line
    sx = x
    sy = y
    line = cell_mask(sx, sy)
    out += line
  }

  fn part_c() {
    sx = x
    sy = y
    line = cell_mask(sx, sy)
    out += line
    sx = x
    sy = y
    line = cell_mask(sx, sy)
    out += line
  }
}
)mkpro";
    const CompileResult result = compile_stack_analysis(stack_helper_abi_source);
    require_clean_compile(result, "stack helper ABI aggregation");
    require(count_steps_with_comment(result, "expr cell_mask(sx, sy)") >= 6,
            "aggregation fixture should reuse the shared cell_mask helper");
    const SizeAbiBlockerReport* blocker =
        find_size_abi_blocker(result, "stack-helper-abi", "cell_mask(sx, sy)");
    require(blocker != nullptr, "size attribution should report stack helper ABI blockers");
    require(blocker->materialize_cells == 12 &&
                blocker->details.contains("stackEntryCandidateCallSites") &&
                blocker->details.at("stackEntryCandidateCallSites") == "6" &&
                blocker->details.contains("grossMaterializeCells") &&
                blocker->details.at("grossMaterializeCells") == "12" &&
                blocker->details.contains("materializeCellsPerCallSite") &&
                blocker->details.at("materializeCellsPerCallSite") == "2" &&
                blocker->details.contains("correctnessStatus") &&
                blocker->details.at("correctnessStatus") ==
                    "requires-stack-argument-entry-or-materialization" &&
                blocker->details.contains("safeFallbackAction") &&
                blocker->details.at("safeFallbackAction") ==
                    "materialize-stack-resident-temps-before-register-entry" &&
                blocker->details.contains("schedulerAction") &&
                blocker->details.at("schedulerAction") == "prove-stack-aware-helper-call" &&
                blocker->details.contains("estimatedStackEntryOverheadCells") &&
                blocker->details.at("estimatedStackEntryOverheadCells") == "11" &&
                blocker->details.contains("estimatedNetSavings") &&
                blocker->details.at("estimatedNetSavings") == "1" &&
                blocker->details.contains("estimatedBreakEvenCallSites") &&
                blocker->details.at("estimatedBreakEvenCallSites") == "6" &&
                blocker->details.contains("additionalCallSitesToBreakEven") &&
                blocker->details.at("additionalCallSitesToBreakEven") == "0" &&
                blocker->details.contains("firstLine") &&
                blocker->details.contains("lastLine") &&
                blocker->details.contains("costModelAction") &&
                blocker->details.at("costModelAction") ==
                    "implement-stack-argument-helper-entry",
            "repeated stack helper ABI report should recognize positive stack-entry "
            "sites");
    const SizeOpportunityReport* opportunity = find_size_opportunity(result, "stack-helper-abi");
    require(opportunity != nullptr && opportunity->savings == 1 &&
                opportunity->candidate_steps == static_cast<int>(result.steps.size()) - 1 &&
                opportunity->details.contains("estimatedNetSavings") &&
                opportunity->details.at("estimatedNetSavings") == "1" &&
                opportunity->details.contains("correctnessStatus") &&
                opportunity->details.at("correctnessStatus") ==
                    "requires-stack-argument-entry-or-materialization" &&
                opportunity->details.contains("safeFallbackAction") &&
                opportunity->details.at("safeFallbackAction") ==
                    "materialize-stack-resident-temps-before-register-entry" &&
                opportunity->details.contains("schedulerAction") &&
                opportunity->details.at("schedulerAction") == "prove-stack-aware-helper-call" &&
                opportunity->details.contains("stackEntryCandidateCallSites") &&
                opportunity->details.at("stackEntryCandidateCallSites") == "6",
            "stack helper ABI opportunity should expose repeated sites as a positive entry");
    const SizeNextActionSummaryReport* cost_action = find_size_next_action(
        result, "costModelAction", "implement-stack-argument-helper-entry");
    require(cost_action != nullptr && cost_action->potential_savings >= 1 &&
                cost_action->best_savings >= 1 &&
                cost_action->best_variant == "stack-helper-abi",
            "positive stack helper ABI estimate should rank implementation as a next action");

    CompileOptions stack_entry_options;
    stack_entry_options.analysis = true;
    stack_entry_options.budget = 999999;
    stack_entry_options.stack_resident_temps = true;
    stack_entry_options.stack_argument_helper_entries = true;
    stack_entry_options.disable_candidate_search = true;
    const CompileResult stack_entry = compile_source(stack_helper_abi_source, stack_entry_options);
    require_clean_compile(stack_entry, "stack helper argument-entry variant");
    require(static_cast<int>(stack_entry.steps.size()) == static_cast<int>(result.steps.size()) - 1,
            "stack helper argument-entry variant should realize the one-cell stack-entry saving");
    require(has_optimization(stack_entry, "expression-helper-stack-entry-primary"),
            "stack helper argument-entry variant should emit a primary stack helper entry when "
            "all calls use the stack ABI");
    require(has_optimization(stack_entry, "expression-helper-stack-entry-call"),
            "stack helper argument-entry variant should call the stack helper entry");
    require(count_steps_with_comment_prefix_and_opcode(
                stack_entry, "expr cell_mask(sx, sy) stack entry", 0x53) == 6,
            "stack helper argument-entry variant should route stack-resident cell_mask calls "
            "through the stack entry");
    require(find_size_abi_blocker(stack_entry, "stack-helper-abi", "cell_mask(sx, sy)") ==
                nullptr,
            "stack helper argument-entry variant should satisfy the stack-helper ABI blocker");

    CompileOptions selected_options;
    selected_options.analysis = true;
    selected_options.budget = 999999;
    selected_options.disable_aggressive_post_layout = true;
    const CompileResult selected = compile_source(stack_helper_abi_source, selected_options);
    require_clean_compile(selected, "candidate-searched stack helper argument-entry variant");
    const CandidateReport* stack_entry_candidate =
        find_candidate(selected.rejected_candidates, "stack-resident-helper-entries");
    require(stack_entry_candidate != nullptr && !stack_entry_candidate->selected &&
                stack_entry_candidate->steps == static_cast<int>(stack_entry.steps.size()),
            "candidate search should evaluate stack helper argument-entry lowering even when a "
            "larger shared-body rewrite wins");
  }

  {
    const CompileResult result = compile_stack_analysis(R"mkpro(
program ValueAwareDirectStackInputNestedCall {
  state {
    x: packed = 2
    seed: packed = 1
    scratch: packed = 0
    score: packed = 0
  }

  fn cold() {
    scratch = seed + 1
  }

  fn hot() {
    score = sum(x, x, x, x)
    cold()
  }

  loop {
    hot()
    hot()
    cold()
    halt(score)
  }
}
)mkpro");
    require_clean_compile(result, "direct stack-input nested-call attribution");
    const SizeHelperSummaryReport* hot = find_size_helper(result, "hot");
    require(hot != nullptr, "size attribution should summarize the hot helper");
    require(hot->details.contains("valueAwareNestedCallLabels") &&
                hot->details.at("valueAwareNestedCallLabels").find("cold") !=
                    std::string::npos,
            "hot helper should expose its nested cold call");
    require(hot->details.contains("valueAwareDirectStackInputNames") &&
                hot->details.at("valueAwareDirectStackInputNames").find("x") !=
                    std::string::npos,
            "x should be classified as directly schedulable before the nested call");
    require(!hot->details.contains("valueAwareCallPreservationInputNames"),
            "x should not require call-preserving proof when it is not recalled after cold");
    require(hot->details.contains("valueAwareStackInputPlanStatus") &&
                hot->details.at("valueAwareStackInputPlanStatus") == "direct-stack-fit",
            "nested calls should not block profitable stack inputs that are dead before them");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackResidentRepeatedSum {
  state {
    x: packed = 2
    y: packed = 3
    tmp: packed = 0
    out: packed = 0
    guard: flag = false
  }

  loop {
    tmp = x + y
    if guard {
    }
    out = sum(tmp, tmp, tmp, tmp)
    halt(out)
  }
}
)mkpro");
    require_clean_compile(result, "stack-resident repeated sum");
    require(has_optimization(result, "stack-resident-repeated-sum"),
            "repeated sum should use stack-resident duplication instead of recalling tmp");
    require(count_steps_with_comment(result, "duplicate repeated stack input") == 3,
            "sum(tmp, tmp, tmp, tmp) should duplicate the stack-resident temp three times");
    require(count_steps_with_comment(result, "stack-resident repeated sum") == 3,
            "sum(tmp, tmp, tmp, tmp) should use three stack-resident additions");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "stack-resident repeated sum should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "stack-resident repeated sum should not recall tmp");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedImmediateConsumer {
  state {
    x: packed = 2
    y: packed = 3
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    halt(tmp)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried immediate consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "immediate consumer should keep tmp in X instead of storing it");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "stack-carried immediate consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "stack-carried immediate consumer should not recall tmp");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedDelayedThroughSafeControlFlow {
  state {
    x: packed = 2
    y: packed = 3
    z: packed = 0
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    if z == 0 {
    }
    halt(tmp + z)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "delayed stack-carried assignment");
    require(has_optimization(result, "stack-carried-assignment-delayed"),
            "safe intervening control flow should delay the assignment until its consumer");
    require(has_optimization(result, "stack-current-x-scheduling"),
            "delayed assignment consumer should reuse tmp from current X");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "delayed stack-carried assignment should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "delayed stack-carried assignment should not recall tmp");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedDelayedRejectsOperandWrite {
  state {
    x: packed = 2
    y: packed = 3
    z: packed = 4
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    x += 1
    halt(tmp + z)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "delayed stack-carried assignment operand write reject");
    require(!has_optimization(result, "stack-carried-assignment-delayed"),
            "delayed assignment must reject intervening writes to producer operands");
    require(count_steps_with_comment(result, "set tmp") == 1,
            "unsafe delayed assignment should keep the original tmp store");
    require(count_steps_with_comment(result, "recall tmp") == 1,
            "unsafe delayed assignment should recall the stored tmp");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedExpressionConsumer {
  state {
    x: packed = 2
    y: packed = 3
    z: packed = 4
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    halt(tmp + z)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried expression consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "expression consumer should keep tmp in X instead of storing it");
    require(has_optimization(result, "stack-current-x-scheduling"),
            "expression consumer should reuse tmp already in X for the following expression");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "stack-carried expression consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "stack-carried expression consumer should not recall tmp");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedRegisterPressure {
  state {
    v0: packed = 1
    v1: packed = 2
    v2: packed = 3
    v3: packed = 4
    v4: packed = 5
    v5: packed = 6
    v6: packed = 7
    v7: packed = 8
    v8: packed = 9
    v9: packed = 10
    v10: packed = 11
    v11: packed = 12
    v12: packed = 13
    v13: packed = 14
    v14: packed = 15
  }

  loop {
    tmp = v0 + v1
    halt(sum(tmp, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14))
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried register pressure");
    require(has_optimization(result, "stack-carried-assignment"),
            "register-pressure temp should use stack-carried assignment");
    require(has_optimization(result, "stack-only-state-field"),
            "register-pressure temp should be proven stack-only before allocation");
    require(!result.registers.contains("tmp"),
            "register-pressure stack-only temp should not allocate a physical register");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "register-pressure stack-only temp should not be stored");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "register-pressure stack-only temp should not be recalled");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedReadRegisterPressure {
  state {
    v0: packed = 1
    v1: packed = 2
    v2: packed = 3
    v3: packed = 4
    v4: packed = 5
    v5: packed = 6
    v6: packed = 7
    v7: packed = 8
    v8: packed = 9
    v9: packed = 10
    v10: packed = 11
    v11: packed = 12
    v12: packed = 13
    v13: packed = 14
    v14: packed = 15
  }

  loop {
    key = read()
    halt(sum(key, v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14))
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried read register pressure");
    require(has_optimization(result, "stack-carried-read"),
            "register-pressure read target should use stack-carried read");
    require(has_optimization(result, "stack-only-state-field"),
            "register-pressure read target should be proven stack-only before allocation");
    require(!result.registers.contains("key"),
            "register-pressure read target should not allocate a physical register");
    require(count_steps_with_comment(result, "read key") == 1,
            "register-pressure read target should not emit a following store");
    require(count_steps_with_comment(result, "recall key") == 0,
            "register-pressure read target should not be recalled");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedShowReadRegisterPressure {
  state {
    v0: packed = 1
    v1: packed = 2
    v2: packed = 3
    v3: packed = 4
    v4: packed = 5
    v5: packed = 6
    v6: packed = 7
    v7: packed = 8
    v8: packed = 9
    v9: packed = 10
    v10: packed = 11
    v11: packed = 12
    v12: packed = 13
    v13: packed = 14
    v14: packed = 15
  }

  loop {
    show(v0)
    key = read()
    if key != 1 {
      halt(v1)
    }
    show(v2)
    key = read()
    if key != 3 {
      halt(v3)
    }
    halt(sum(v2, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14))
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried show/read register pressure");
    require(has_optimization(result, "show-read-fusion"),
            "register-pressure show/read target should still fuse show and read");
    require(has_optimization(result, "stack-carried-read"),
            "register-pressure show/read target should be carried from the fused stop");
    require(has_optimization(result, "stack-only-state-field"),
            "register-pressure show/read target should be proven stack-only before allocation");
    require(!result.registers.contains("key"),
            "register-pressure show/read target should not allocate a physical register");
    require(count_steps_with_comment(result, "read key") == 0,
            "register-pressure show/read target should not be stored");
    require(count_steps_with_comment(result, "recall key") == 0,
            "register-pressure show/read target should not be recalled");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program ShowReadWithoutRegisterPressureKeepsStore {
  state {
    screen: packed = 1
  }

  loop {
    show(screen)
    key = read()
    if key != 1 {
      halt(screen)
    }
    show(screen)
    key = read()
    if key != 2 {
      halt(screen)
    }
    halt(0)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "show/read without register pressure");
    require(has_optimization(result, "show-read-fusion"),
            "non-pressure show/read target should still fuse show and read");
    require(!has_optimization(result, "stack-carried-read"),
            "non-pressure show/read target should not use the separator-costly stack-only path");
    require(!has_optimization(result, "stack-only-state-field"),
            "non-pressure show/read target should keep ordinary storage");
    require(result.registers.contains("key"),
            "non-pressure show/read target should keep a physical register");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedInlineExpressionOperandConsumer {
  state {
    x: packed = 2
    y: packed = 3
    z: packed = 4
    w: packed = 5
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    halt(tmp + (z + w))
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried inline expression operand consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "inline expression operand consumer should keep tmp in X");
    require(has_optimization(result, "stack-current-x-scheduling"),
            "inline expression operand consumer should reuse tmp while lowering the other "
            "operand inline");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "inline expression operand consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "inline expression operand consumer should not recall tmp");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedRightInlineExpressionOperandConsumer {
  state {
    x: packed = 8
    y: packed = 3
    z: packed = 20
    w: packed = 7
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    halt((z + w) - tmp)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried right inline expression operand consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "right inline expression operand consumer should keep tmp in X");
    require(has_optimization(result, "stack-current-x-scheduling"),
            "right inline expression operand consumer should reuse tmp for a noncommutative "
            "expression");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "right inline expression operand consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "right inline expression operand consumer should not recall tmp");
    require(count_steps_with_comment(result, "current-X operand order") == 1,
            "right inline expression operand consumer should swap after lowering the other "
            "operand");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedUnaryConsumer {
  state {
    x: packed = 2
    y: packed = 3
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    halt(-tmp)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried unary consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "unary consumer should keep tmp in X instead of storing it");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "stack-carried unary consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "stack-carried unary consumer should not recall tmp");
    require(count_steps_with_comment(result, "unary minus") == 1,
            "stack-carried unary consumer should apply unary minus to the current X value");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedUnaryBuiltinConsumer {
  state {
    x: packed = -8
    y: packed = 3
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    halt(abs(tmp))
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried unary builtin consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "unary builtin consumer should keep tmp in X instead of storing it");
    require(has_optimization(result, "current-x-unary-derivation"),
            "unary builtin consumer should derive abs() from current X");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "stack-carried unary builtin consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "stack-carried unary builtin consumer should not recall tmp");
    require(count_steps_with_comment(result, "current-X abs") == 1,
            "stack-carried unary builtin consumer should apply abs() to current X");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackOnlyXParamReturnDirectHaltConsumer {
  state {
    x: packed = 2
    y: packed = 3
    line: packed = 0
  }

  fn normalize(raw_line) {
    line = frac((raw_line + 3) / 4) * 4 + 1
  }

  loop {
    normalize(x + y)
    halt(line + 1)
    normalize(x - y)
    halt(line + 2)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-only X-param return direct halt consumer");
    require(has_optimization(result, "stack-carried-return"),
            "direct halt consumer should keep returned line in X");
    require(has_optimization(result, "stack-only-state-field"),
            "direct halt consumer should prove returned line stack-only");
    require(!result.registers.contains("line"),
            "direct halt consumer should not allocate a register for line");
    require(count_steps_with_comment(result, "set line") == 0,
            "direct halt consumer should not store line");
    require(count_steps_with_comment(result, "recall line") == 0,
            "direct halt consumer should not recall line");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackOnlyNoArgPackedDigitConsumer {
  state {
    lines: packed[4..7] = [44444.4, 44444.4, 44444.4, 44444.4]
    x: counter 0..7 = 2
    y: counter 0..7 = 3
    line: packed = 0
    slot: counter 0..8 = 8
    best_score: packed = 1
  }

  fn mark_one() {
    slot--
    lines[slot] = packed_add(lines[slot], line, best_score)
  }

  loop {
    line = x
    mark_one()
    line = y
    mark_one()
    halt(0)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-only no-arg packed digit consumer");
    require(has_optimization(result, "stack-only-state-field"),
            "no-arg packed digit consumer should prove line stack-only before allocation");
    require(!result.registers.contains("line"),
            "no-arg packed digit consumer should not allocate a register for line");
    require(count_steps_with_comment(result, "set line") == 0,
            "no-arg packed digit consumer should not store line");
    require(count_steps_with_comment(result, "recall line") == 0,
            "no-arg packed digit consumer should not recall line");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedUnaryDerivationBinaryConsumer {
  state {
    x: packed = -8
    y: packed = 3
    z: packed = 4
    tmp: packed = 0
  }

  loop {
    tmp = x * y
    halt(abs(tmp) + z)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried unary derivation binary consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "unary derivation binary consumer should keep tmp in X instead of storing it");
    require(has_optimization(result, "stack-current-x-scheduling"),
            "unary derivation binary consumer should report current-X scheduling");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "stack-carried unary derivation binary consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "stack-carried unary derivation binary consumer should not recall tmp");
    require(count_steps_with_comment(result, "current-X abs") == 1,
            "stack-carried unary derivation binary consumer should derive abs() from current X");
    require(count_steps_with_comment(result, "expr +") == 1,
            "stack-carried unary derivation binary consumer should emit one addition");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedLeftNonCommutativeConsumer {
  state {
    x: packed = 8
    y: packed = 3
    z: packed = 2
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    halt(tmp - z)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried left noncommutative consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "left noncommutative consumer should keep tmp in X instead of storing it");
    require(has_optimization(result, "stack-current-x-scheduling"),
            "left noncommutative consumer should report current-X scheduling");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "stack-carried left noncommutative consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "stack-carried left noncommutative consumer should not recall tmp");
    require(count_steps_with_comment(result, "expr -") == 1,
            "stack-carried left noncommutative consumer should emit one subtraction");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedRightNonCommutativeConsumer {
  state {
    x: packed = 8
    y: packed = 3
    z: packed = 20
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    halt(z - tmp)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried right noncommutative consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "right noncommutative consumer should keep tmp in X instead of storing it");
    require(has_optimization(result, "stack-current-x-scheduling"),
            "right noncommutative consumer should report current-X scheduling");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "stack-carried right noncommutative consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "stack-carried right noncommutative consumer should not recall tmp");
    require(count_steps_with_comment(result, "current-X operand order") == 1,
            "right noncommutative consumer should swap stack order without recalling tmp");
    require(count_steps_with_comment(result, "expr -") == 1,
            "stack-carried right noncommutative consumer should emit one subtraction");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedRightUnaryDerivationNonCommutativeConsumer {
  state {
    x: packed = 8.75
    y: packed = 3
    z: packed = 20
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    halt(z - frac(tmp))
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried right unary derivation noncommutative consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "right unary derivation noncommutative consumer should keep tmp in X");
    require(has_optimization(result, "stack-current-x-scheduling"),
            "right unary derivation noncommutative consumer should report current-X scheduling");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "right unary derivation noncommutative consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "right unary derivation noncommutative consumer should not recall tmp");
    require(count_steps_with_comment(result, "current-X frac") == 1,
            "right unary derivation noncommutative consumer should derive frac() from current X");
    require(count_steps_with_comment(result, "current-X operand order") == 1,
            "right unary derivation noncommutative consumer should swap stack order");
    require(count_steps_with_comment(result, "expr -") == 1,
            "right unary derivation noncommutative consumer should emit one subtraction");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedAssignmentConsumer {
  state {
    x: packed = 2
    y: packed = 3
    scale: packed = 4
    tmp: packed = 0
    out: packed = 0
  }

  loop {
    tmp = x + y
    out = tmp * scale
    halt(out)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried assignment consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "assignment consumer should keep tmp in X for the following expression");
    require(has_optimization(result, "stack-current-x-scheduling"),
            "assignment consumer should reuse tmp already in X for the assigned expression");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "stack-carried assignment consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "stack-carried assignment consumer should not recall tmp");
    require(count_steps_with_comment(result, "set out") == 0,
            "immediately consumed assignment output should also stay in X");
    require(count_steps_with_comment(result, "recall out") == 0,
            "immediately consumed assignment output should not be recalled");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedUnaryAssignmentConsumer {
  state {
    x: packed = 2
    y: packed = 3
    tmp: packed = 0
    out: packed = 0
  }

  loop {
    tmp = x + y
    out = -tmp
    halt(out)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried unary assignment consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "unary assignment consumer should keep tmp in X for the following expression");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "stack-carried unary assignment consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "stack-carried unary assignment consumer should not recall tmp");
    require(count_steps_with_comment(result, "unary minus") == 1,
            "stack-carried unary assignment consumer should apply unary minus to current X");
    require(count_steps_with_comment(result, "set out") == 0,
            "immediately consumed unary assignment output should also stay in X");
    require(count_steps_with_comment(result, "recall out") == 0,
            "immediately consumed unary assignment output should not be recalled");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedUnaryBuiltinAssignmentConsumer {
  state {
    x: packed = 8.75
    y: packed = 3
    tmp: packed = 0
    out: packed = 0
  }

  loop {
    tmp = x + y
    out = frac(tmp)
    halt(out)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried unary builtin assignment consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "unary builtin assignment consumer should keep tmp in X for the following expression");
    require(has_optimization(result, "current-x-unary-derivation"),
            "unary builtin assignment consumer should derive frac() from current X");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "stack-carried unary builtin assignment consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "stack-carried unary builtin assignment consumer should not recall tmp");
    require(count_steps_with_comment(result, "current-X frac") == 1,
            "stack-carried unary builtin assignment consumer should apply frac() to current X");
    require(count_steps_with_comment(result, "set out") == 0,
            "immediately consumed unary builtin assignment output should also stay in X");
    require(count_steps_with_comment(result, "recall out") == 0,
            "immediately consumed unary builtin assignment output should not be recalled");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedSumConsumer {
  state {
    x: packed = 2
    y: packed = 3
    left: packed = 10
    right: packed = 20
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    halt(sum(left, tmp, right))
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried sum consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "sum consumer should keep tmp in X for the following sum(...) expression");
    require(has_optimization(result, "sum-primitive-lowering"),
            "sum consumer should lower through the explicit sum primitive");
    require(has_optimization(result, "stack-current-x-scheduling"),
            "sum consumer should reuse tmp already in X inside the addition chain");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "stack-carried sum consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "stack-carried sum consumer should not recall tmp");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedSumInlineExpressionConsumer {
  state {
    x: packed = 2
    y: packed = 3
    left: packed = 10
    right: packed = 20
    tail: packed = 5
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    halt(sum(left + tail, tmp, right))
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried sum inline expression consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "sum inline expression consumer should keep tmp in X");
    require(has_optimization(result, "sum-primitive-lowering"),
            "sum inline expression consumer should lower through the explicit sum primitive");
    require(has_optimization(result, "stack-current-x-scheduling"),
            "sum inline expression consumer should reuse tmp inside the addition chain");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "stack-carried sum inline expression consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "stack-carried sum inline expression consumer should not recall tmp");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedRepeatedSumConsumer {
  state {
    x: packed = 2
    y: packed = 3
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    halt(sum(tmp, tmp))
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried repeated sum consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "sum consumer should keep repeated current-X reads and duplicate explicitly");
    require(has_optimization(result, "sum-primitive-lowering"),
            "repeated sum consumer should lower through the explicit sum primitive");
    require(has_optimization(result, "stack-current-x-scheduling"),
            "repeated sum consumer should report current-X stack scheduling");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "repeated sum consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "repeated sum consumer should not recall tmp");
    require(count_steps_with_comment(result, "duplicate repeated operand through stack") == 1,
            "repeated sum consumer should duplicate the current X value through the stack");
    require(count_steps_with_comment(result, "expr +") == 2,
            "repeated sum consumer should emit one source addition and one duplicated addition");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedRightDivisionAssignmentConsumer {
  state {
    x: packed = 8
    y: packed = 2
    z: packed = 100
    tmp: packed = 0
    out: packed = 0
  }

  loop {
    tmp = x + y
    out = z / tmp
    halt(out)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried right division assignment consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "right division assignment consumer should keep tmp in X for the following expression");
    require(has_optimization(result, "stack-current-x-scheduling"),
            "right division assignment consumer should report current-X scheduling");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "stack-carried right division assignment consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "stack-carried right division assignment consumer should not recall tmp");
    require(count_steps_with_comment(result, "current-X operand order") == 1,
            "right division assignment consumer should swap stack order without recalling tmp");
    require(count_steps_with_comment(result, "expr /") == 1,
            "stack-carried right division assignment consumer should emit one division");
    require(count_steps_with_comment(result, "set out") == 0,
            "immediately consumed division assignment output should also stay in X");
    require(count_steps_with_comment(result, "recall out") == 0,
            "immediately consumed division assignment output should not be recalled");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedUpdateConsumer {
  state {
    x: packed = 2
    y: packed = 3
    score: packed = 10
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    score += tmp
    halt(score)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried update consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "update consumer should keep tmp in X for the following += update");
    require(has_optimization(result, "stack-current-x-scheduling"),
            "update consumer should reuse tmp already in X while updating score");
    require(has_optimization(result, "stack-carried-update"),
            "update consumer should keep the updated score in X for halt(score)");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "stack-carried update consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "stack-carried update consumer should not recall tmp");
    require(count_steps_with_comment(result, "set score") == 0,
            "stack-carried update consumer should not store immediately consumed score");
    require(count_steps_with_comment(result, "recall score") == 1,
            "stack-carried update consumer should recall score only to compute the update");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedDirectUpdateConsumer {
  state {
    score: packed = 10
    delta: packed = 4
  }

  loop {
    score += delta
    halt(score)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried direct update consumer");
    require(has_optimization(result, "stack-carried-update"),
            "direct update consumer should keep updated score in X for halt(score)");
    require(count_steps_with_comment(result, "set score") == 0,
            "direct update consumer should not store immediately consumed score");
    require(count_steps_with_comment(result, "recall score") == 1,
            "direct update consumer should recall score only to compute the update");
    require(count_steps_with_comment(result, "recall delta") == 1,
            "direct update consumer should recall delta once");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedUpdateBranchConsumer {
  state {
    score: packed = 10
    delta: packed = 4
    target: packed = 14
  }

  loop {
    score += delta
    if score == target {
      halt(1)
    }
    halt(0)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried update branch consumer");
    require(has_optimization(result, "stack-carried-update"),
            "branch consumer should keep updated score in X for the following condition");
    require(has_optimization(result, "condition-current-x-reuse"),
            "branch consumer should compare the updated score from current X");
    require(count_steps_with_comment(result, "set score") == 0,
            "branch-consumed update should not store the updated score");
    require(count_steps_with_comment(result, "recall score") == 1,
            "branch-consumed update should recall score only to compute the update");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedDelayedUpdateConsumer {
  state {
    score: packed = 10
    delta: packed = 4
    gate: packed = 0
  }

  loop {
    score += delta
    if gate == 0 {
    }
    halt(score)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "delayed stack-carried update consumer");
    require(has_optimization(result, "stack-carried-update-delayed"),
            "safe intervening control flow should delay the update until its consumer");
    require(count_steps_with_comment(result, "set score") == 0,
            "delayed stack-carried update should not store the updated score");
    require(count_steps_with_comment(result, "recall score") == 1,
            "delayed stack-carried update should recall score only to compute the update");
    require(count_steps_with_comment(result, "recall delta") == 1,
            "delayed stack-carried update should recall delta once");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedDelayedUpdateBranchConsumer {
  state {
    score: packed = 10
    delta: packed = 4
    gate: packed = 0
    target: packed = 14
  }

  loop {
    score += delta
    if gate == 0 {
    }
    if score == target {
      halt(1)
    }
    halt(0)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "delayed stack-carried update branch consumer");
    require(has_optimization(result, "stack-carried-update-delayed"),
            "safe intervening control flow should delay the update until the branch consumer");
    require(has_optimization(result, "condition-current-x-reuse"),
            "delayed branch consumer should compare the updated score from current X");
    require(count_steps_with_comment(result, "set score") == 0,
            "delayed branch-consumed update should not store the updated score");
    require(count_steps_with_comment(result, "recall score") == 1,
            "delayed branch-consumed update should recall score only to compute the update");
    require(count_steps_with_comment(result, "recall delta") == 1,
            "delayed branch-consumed update should recall delta once");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedDelayedUpdateRejectsDeltaWrite {
  state {
    score: packed = 10
    delta: packed = 4
  }

  loop {
    score += delta
    delta += 1
    halt(score)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "delayed stack-carried update delta write reject");
    require(!has_optimization(result, "stack-carried-update-delayed"),
            "delayed update must reject intervening writes to delta operands");
    require(count_steps_with_comment(result, "set score") == 1,
            "unsafe delayed update should keep the original score store");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedUnitIncrementConsumer {
  state {
    score: packed = 10
  }

  loop {
    score += 1
    halt(score)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried unit increment consumer");
    require(has_optimization(result, "stack-carried-update"),
            "unit increment consumer should keep updated score in X for halt(score)");
    require(count_steps_with_comment(result, "set score") == 0,
            "unit increment consumer should not store immediately consumed score");
    require(count_steps_with_comment(result, "recall score") == 1,
            "unit increment consumer should recall score only to compute the update");
    require(count_steps_with_comment(result, "expr +") == 1,
            "unit increment consumer should still emit the arithmetic increment");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedIndirectUnitDecrementConsumer {
  state {
    score: counter 1..14 = 10
  }

  loop {
    score -= 1
    halt(score)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried indirect unit decrement consumer");
    require(has_optimization(result, "indirect-incdec-counter"),
            "unit decrement consumer should keep the compact indirect decrement");
    require(has_optimization(result, "stack-carried-update"),
            "unit decrement consumer should keep decremented score in X for halt(score)");
    require(count_steps_with_comment(result, "decrement score") == 1,
            "unit decrement consumer should emit one indirect decrement");
    require(count_steps_with_comment(result, "set score") == 0,
            "unit decrement consumer should not add a store after indirect decrement");
    require(count_steps_with_comment(result, "recall score") == 0,
            "unit decrement consumer should not recall decremented score for halt(score)");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedUnaryUpdateConsumer {
  state {
    x: packed = 2.75
    y: packed = 3
    score: packed = 10
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    score -= frac(tmp)
    halt(score)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried unary update consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "unary update consumer should keep tmp in X for the following -= update");
    require(has_optimization(result, "stack-current-x-scheduling"),
            "unary update consumer should reuse tmp already in X while updating score");
    require(has_optimization(result, "stack-carried-update"),
            "unary update consumer should keep the updated score in X for halt(score)");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "stack-carried unary update consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "stack-carried unary update consumer should not recall tmp");
    require(count_steps_with_comment(result, "current-X frac") == 1,
            "stack-carried unary update consumer should derive frac() from current X");
    require(count_steps_with_comment(result, "current-X operand order") == 1,
            "unary -= update consumer should swap stack order without recalling tmp");
    require(count_steps_with_comment(result, "set score") == 0,
            "stack-carried unary update consumer should not store immediately consumed score");
    require(count_steps_with_comment(result, "recall score") == 1,
            "stack-carried unary update consumer should recall score only to compute the update");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedReadAssignmentConsumer {
  state {
    key: packed = 0
    base: packed = 9
    out: packed = 0
  }

  loop {
    key = read()
    out = base - key
    halt(out)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried read assignment consumer");
    require(has_optimization(result, "stack-carried-read"),
            "read consumer should keep key in X for the following expression");
    require(has_optimization(result, "stack-current-x-scheduling"),
            "read consumer should reuse key already in X");
    require(count_steps_with_comment(result, "read key") == 1,
            "stack-carried read consumer should not store key");
    require(count_steps_with_comment(result, "recall key") == 0,
            "stack-carried read consumer should not recall key");
    require(count_steps_with_comment(result, "set out") == 0,
            "immediately consumed read-derived assignment should stay in X");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedReadFutureReadRejected {
  state {
    key: packed = 0
    base: packed = 9
    out: packed = 0
  }

  loop {
    key = read()
    out = base - key
    halt(key + out)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried read future-read rejection");
    require(!has_optimization(result, "stack-carried-read"),
            "future read before write should keep the read target stored");
    require(has_optimization(result, "intent-read-lowering"),
            "stored read path should still report intent-read-lowering");
    require(count_steps_with_comment(result, "read key") == 2,
            "future read before write should emit both the read stop and key store");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedIfZeroConsumer {
  state {
    x: packed = 2
    y: packed = -2
    score: packed = 0
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    if tmp == 0 {
      score += 1
    }
    halt(score)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried if-zero consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "if-zero consumer should keep tmp in X for the following branch");
    require(has_optimization(result, "zero-condition-test"),
            "if-zero consumer should branch directly on the current X value");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "stack-carried if-zero consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "stack-carried if-zero consumer should not recall tmp");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedIfEqualityConsumer {
  state {
    x: packed = 2
    y: packed = 3
    target: packed = 5
    score: packed = 0
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    if target == tmp {
      score += 1
    }
    halt(score)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried if-equality consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "if-equality consumer should keep tmp in X for the following branch");
    require(has_optimization(result, "condition-current-x-reuse"),
            "if-equality consumer should compare against the current X value");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "stack-carried if-equality consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "stack-carried if-equality consumer should not recall tmp");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedIfInlineExpressionConsumer {
  state {
    x: packed = 2
    y: packed = 3
    z: packed = 4
    w: packed = 1
    score: packed = 0
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    if tmp == z + w {
      score += 1
    }
    halt(score)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried if inline expression consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "if inline expression consumer should keep tmp in X for the following branch");
    require(has_optimization(result, "condition-current-x-reuse"),
            "if inline expression consumer should compare the inline operand against current X");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "if inline expression consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "if inline expression consumer should not recall tmp");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedIfBodyReadBeforeWriteRejected {
  state {
    command: packed = 7
    step: packed = 0
  }

  loop {
    step = command - 5
    if step == 0 {
      halt(0)
    } else {
      step = int(1 / step)
      halt(step)
    }
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried if body read before write rejection");
    require(!has_optimization(result, "stack-carried-assignment"),
            "branch body read before write should keep the temporary stored");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedRepeatedConsumer {
  state {
    x: packed = 2
    y: packed = 3
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    halt(tmp / tmp)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried repeated consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "repeated expression consumer should keep tmp in X and duplicate it explicitly");
    require(has_optimization(result, "stack-current-x-scheduling"),
            "repeated expression consumer should report current-X stack scheduling");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "repeated expression consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "repeated expression consumer should not recall tmp");
    require(count_steps_with_comment(result, "duplicate repeated operand through stack") == 1,
            "repeated expression consumer should duplicate the current X value through the stack");
    require(count_steps_with_comment(result, "expr /") == 1,
            "repeated expression consumer should emit one division after duplication");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedRepeatedSquareConsumer {
  state {
    x: packed = 2
    y: packed = 3
    tmp: packed = 0
  }

  loop {
    tmp = x + y
    halt(tmp * tmp)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried repeated square consumer");
    require(has_optimization(result, "stack-carried-assignment"),
            "repeated square consumer should keep tmp in X");
    require(has_optimization(result, "square-expression-lowering"),
            "repeated square consumer should use F x^2 instead of a register spill");
    require(count_steps_with_comment(result, "set tmp") == 0,
            "repeated square consumer should not store tmp");
    require(count_steps_with_comment(result, "recall tmp") == 0,
            "repeated square consumer should not recall tmp");
    require(count_steps_with_comment(result, "square repeated operand") == 1,
            "repeated square consumer should square the current X value");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackCarriedLoopPrefixRead {
  state {
    x: packed = 2
    y: packed = 3
    tmp: packed = 0
  }

  loop {
    show(tmp)
    tmp = x + y
    halt(tmp)
  }
}
)mkpro",
                                                       false);
    require_clean_compile(result, "stack-carried loop prefix read");
    require(!has_optimization(result, "stack-carried-assignment"),
            "loop-carried read before the next write should keep the tmp store");
    require(count_steps_with_comment(result, "set tmp") == 1,
            "loop-carried read before write should store tmp");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackTempProcOverwrite {
  state {
    cells: packed[1..2] = [1, 2]
    index: counter 1..2 = 1
    tmp: packed = 0
    out: packed = 0
  }

  loop {
    tmp = 3
    cells[index] = cells[index] + tmp
    reset_tmp()
    halt(cells[index] + out)
  }

  fn reset_tmp() {
    tmp = 0
    out = tmp
  }
}
)mkpro",
                                                       true);
    require_clean_compile(result, "indexed stack temp overwritten before read");
    require(has_optimization(result, "stack-resident-indexed-temp"),
            "indexed stack temp should report stack-resident-indexed-temp");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackTempProcReadBeforeWrite {
  state {
    cells: packed[1..2] = [1, 2]
    index: counter 1..2 = 1
    tmp: packed = 0
    out: packed = 0
  }

  loop {
    tmp = 3
    cells[index] = cells[index] + tmp
    use_tmp()
    halt(cells[index] + out)
  }

  fn use_tmp() {
    out = tmp
    tmp = 0
  }
}
)mkpro",
                                                       true);
    require_clean_compile(result, "indexed stack temp read before write");
    require(!has_optimization(result, "stack-resident-indexed-temp"),
            "indexed stack temp should not fire when a later proc reads before writing");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program CrossFlowIf {
  state {
    x: packed = 1
    y: packed = 2
    z: packed = 0
    gate: packed = 0
  }
  loop {
    a = x
    b = y
    if gate == 1 {
      loop {
      }
    }
    z = a + b
    halt(z)
  }
}
)mkpro",
                                                       true);
    require_clean_compile(result, "stack-resident control-flow fusion");
    require(has_optimization(result, "stack-resident-control-flow"),
            "stack-resident control-flow fusion should report stack-resident-control-flow");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackResidentRepeatedSumControlFlow {
  state {
    x: packed = 2
    gate: packed = 0
    z: packed = 0
  }
  loop {
    a = x
    if gate == 1 {
      loop {
      }
    }
    z = sum(a, a)
    halt(z)
  }
}
)mkpro",
                                                       true);
    require_clean_compile(result, "stack-resident repeated sum control-flow");
    require(has_optimization(result, "stack-resident-control-flow"),
            "single temp across control flow should stay stack-resident");
    require(has_optimization(result, "sum-primitive-lowering"),
            "stack-resident repeated sum should lower through sum(...)");
    require(count_steps_with_comment(result, "duplicate repeated operand through stack") == 1,
            "stack-resident repeated sum should duplicate the restored temp through the stack");
    require(count_steps_with_comment(result, "recall a") == 0,
            "stack-resident repeated sum should not recall the temp from memory");
  }

  {
    const CompileResult result = compile_stack_variant(R"mkpro(
program StackResidentUnaryCallControlFlow {
  state {
    x: packed = 8.75
    y: packed = 3
    gate: packed = 0
    z: packed = 0
  }
  loop {
    a = x
    b = y
    if gate == 1 {
      loop {
      }
    }
    z = frac(a) + b
    halt(z)
  }
}
)mkpro",
                                                       true);
    require_clean_compile(result, "stack-resident unary call control-flow");
    require(has_optimization(result, "stack-resident-control-flow"),
            "unary call over stack temps should stay stack-resident across control flow");
    require(count_steps_with_comment(result, "stack-resident frac") == 1,
            "stack-resident unary call should apply frac() to the restored stack temp");
    require(count_steps_with_comment(result, "recall a") == 0,
            "stack-resident unary call should not recall the temp from memory");
  }
}

} // namespace mkpro::tests
