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
