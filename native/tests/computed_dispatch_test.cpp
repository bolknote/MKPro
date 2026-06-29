#include "mkpro/compiler.hpp"
#include "mkpro/core/ir.hpp"
#include "mkpro/core/passes/dead_code_after_halt.hpp"
#include "mkpro/core/result.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

using core::passes::dead_code_after_halt;
using core::passes::PassContext;
using core::passes::PassResult;

IrOp make_plain(std::string name) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.name = std::move(name);
  op.opcode = 0x00;
  return op;
}

IrOp make_label(std::string name) {
  IrOp op;
  op.kind = IrKind::Label;
  op.name = std::move(name);
  return op;
}

IrOp make_computed_dispatch(const std::string& comment) {
  IrOp op;
  op.kind = IrKind::IndirectJump;
  op.opcode = 0x8e;  // К БП e
  op.meta.comment = comment;
  return op;
}

bool has_plain(const std::vector<IrOp>& ops, const std::string& name) {
  return std::any_of(ops.begin(), ops.end(), [&](const IrOp& op) {
    return op.kind == IrKind::Plain && op.name == name;
  });
}

// entry -> computed К БП (no static fallthrough) -> three case bodies whose only
// reachability is the computed jump's advertised target labels.
std::vector<IrOp> dispatch_ir(const std::string& jump_comment) {
  std::vector<IrOp> ops;
  ops.push_back(make_label("entry"));
  ops.push_back(make_plain("load_selector"));
  ops.push_back(make_computed_dispatch(jump_comment));
  ops.push_back(make_label("case_a"));
  ops.push_back(make_plain("body_a"));
  ops.push_back(make_label("case_b"));
  ops.push_back(make_plain("body_b"));
  ops.push_back(make_label("case_c"));
  ops.push_back(make_plain("body_c"));
  return ops;
}

}  // namespace

void computed_dispatch_targets_survive_dead_code_elimination() {
  const CompileOptions options;
  const PassContext context{.options = options};

  // Control: without the target marker the computed jump exposes no successors,
  // so the case bodies are genuinely unreachable and the DCE pass removes them.
  // This proves the survival assertion below is meaningful.
  {
    const PassResult removed = dead_code_after_halt(dispatch_ir("computed dispatch"), context);
    require(!has_plain(removed.ops, "body_a"),
            "unmarked computed-jump case body A must be dead-code eliminated");
    require(!has_plain(removed.ops, "body_b"),
            "unmarked computed-jump case body B must be dead-code eliminated");
    require(!has_plain(removed.ops, "body_c"),
            "unmarked computed-jump case body C must be dead-code eliminated");
  }

  // With the target marker the reachability scan follows the advertised edges, so
  // every dispatched case body is retained even though no direct jump references
  // it. This is the unblock that makes a computed dispatch legal in the pipeline.
  {
    const PassResult kept = dead_code_after_halt(
        dispatch_ir("computed dispatch; computed-dispatch-targets=case_a,case_b,case_c"), context);
    require(has_plain(kept.ops, "body_a"),
            "marked computed-dispatch must keep case body A reachable");
    require(has_plain(kept.ops, "body_b"),
            "marked computed-dispatch must keep case body B reachable");
    require(has_plain(kept.ops, "body_c"),
            "marked computed-dispatch must keep case body C reachable");
  }
}

namespace {

bool has_error_diagnostic(const CompileResult& result) {
  return std::any_of(
      result.diagnostics.begin(), result.diagnostics.end(),
      [](const Diagnostic& item) { return item.severity == DiagnosticSeverity::Error; });
}

}  // namespace

// Forcing the computed-dispatch rescue (via an artificially tiny budget) must
// never break a program: the discovery fixpoint either converges to a formula
// the behavioral-equivalence gate accepts, or it is rejected and the compiler
// keeps the compare-chain lowering. Either way the result stays implemented and
// error-free. This guards the discovery + lowering plumbing against regressions
// independently of whether the dispatch happens to win on size.
void computed_dispatch_discovery_keeps_program_correct() {
  const std::string source = R"mkpro(
program DispatchProbe {
  state {
    score: counter 0..99 = 0
    sel: counter 0..2 = 0
  }
  loop {
    sel = read()
    match sel {
      0 => bump_a()
      1 => bump_b()
      2 => bump_c()
    }
  }
  fn bump_a() { score = score + 4 }
  fn bump_b() { score = score + 5 }
  fn bump_c() { score = score + 6 }
}
)mkpro";

  CompileOptions baseline_options;
  const CompileResult baseline = compile_source(source, baseline_options);
  require(baseline.implemented, "baseline match program should compile");

  CompileOptions rescued_options;
  rescued_options.budget = 4;  // force the size-rescue path, exercising discovery
  const CompileResult rescued = compile_source(source, rescued_options);
  require(rescued.implemented,
          "forcing computed-dispatch discovery must keep the program implemented");
  require(!has_error_diagnostic(rescued),
          "computed-dispatch discovery must not introduce error diagnostics");
}

}  // namespace mkpro::tests
