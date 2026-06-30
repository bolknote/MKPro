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

Expression call(std::string callee, std::vector<Expression> args) {
  Expression expression;
  expression.kind = "call";
  expression.callee = std::move(callee);
  expression.args = std::move(args);
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

V2Statement invoke(std::string name, int line) {
  V2Statement statement;
  statement.kind = "v2_invoke";
  statement.name = std::move(name);
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
    const Expression expr = call("cell_mask", {id("a"), id("b")});
    require(!can_lower_stack_resident_expression(expr, {"a", "b"}),
            "stack-residency should reject call consumers without a stack-aware calling convention");
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
        assign("a", "x", 1),
        assign("b", "y", 2),
        assign("c", "a + b", 3),
        invoke("uses_state", 4),
    };
    require(!find_stack_resident_fusion_site(body, 0).has_value(),
            "stack-residency should not prove temps dead across an unknown procedure call");
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

  const std::string call_consumer = R"mkpro(
program StackResidentCallConsumer {
  grid: board(1..4, 1..4)
  state {
    x: counter 0..5 = 1
    y: counter 0..5 = 1
    slot: counter 0..5 = 2
    best_y: counter 0..5 = 3
    line: packed = 0
    occupied: packed = 0
  }
  loop {
    x = slot
    y = best_y
    line = cell_mask(x, y)
    occupied = bit_or(occupied, line)
    halt(occupied + x + y)
  }
}
)mkpro";

  {
    const CompileResult result = compile_stack_variant(call_consumer, true);
    require_clean_compile(result, "stack-resident call-consumer program");
    require(!has_optimization(result, "stack-resident-temps"),
            "stack-resident call-consumer program should not fuse call arguments");
    require(result.listing.find("set x") != std::string::npos,
            "call-consumer program should still store x before cell_mask");
    require(result.listing.find("set y") != std::string::npos,
            "call-consumer program should still store y before cell_mask");
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
}

} // namespace mkpro::tests
