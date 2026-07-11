#include "mkpro/compiler.hpp"
#include "mkpro/core/compiler_static_proof_gate.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/passes/shared_straight_line_helper.hpp"
#include "mkpro/core/register_allocator.hpp"
#include "mkpro/core/stack_value_equivalence.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::string trim_ascii_text(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    value.pop_back();
  return value;
}

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& entry) { return entry.name == name; });
}

bool has_proof(const CompileResult& result, const std::string& id) {
  return std::any_of(result.proofs.begin(), result.proofs.end(), [&](const ProofReport& entry) {
    return entry.id == id && entry.status == "proved";
  });
}

IrOp plain(int opcode) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = opcode;
  return op;
}

IrOp call(const std::string& label) {
  IrOp op;
  op.kind = IrKind::Call;
  op.opcode = 0x53;
  op.target = label;
  return op;
}

IrOp label(const std::string& name) {
  IrOp op;
  op.kind = IrKind::Label;
  op.name = name;
  return op;
}

IrOp terminal(IrKind kind, int opcode) {
  IrOp op;
  op.kind = kind;
  op.opcode = opcode;
  return op;
}

ResolvedStep step(int address, int opcode, std::optional<std::string> comment = std::nullopt) {
  return ResolvedStep{.address = address, .opcode = opcode, .comment = std::move(comment)};
}

std::string run_display(const CompileResult& result, const std::string& context) {
  require(result.implemented, context + " should compile");
  require(result.diagnostics.empty(), context + " should not report diagnostics");
  std::vector<int> codes;
  codes.reserve(result.steps.size());
  for (const ResolvedStep& step : result.steps)
    codes.push_back(step.opcode);
  emulator::MK61 calc;
  const emulator::ProgramLoadResult loaded = calc.load_program(codes);
  require(loaded.diagnostics.empty(), context + " should load without diagnostics");
  for (const PreloadReport& preload : result.preloads)
    calc.set_register(preload.register_name, preload.value);
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult run = calc.run_until_stable(5000, 6);
  require(run.stopped, context + " should halt");
  return trim_ascii_text(calc.display_text());
}

// Two straight-line walks identical except for the leaf they call: the exact
// shape the callee-hole skeleton extraction targets. Register recalls (not
// digit literals) feed the walk so the regions stay X2-boundary clean.
constexpr const char* kCalleeHoleSource = R"mkpro(
program CalleeHoleProbe {
  state {
    total: packed = 0
    line: packed = 0
    q1: packed = 3
    q2: packed = 5
    q3: packed = 7
    q4: packed = 9
  }

  fn leaf_add() {
    total += line
  }

  fn leaf_double_sub() {
    total -= line * 2
  }

  loop {
    line = q1
    leaf_add()
    line = q2
    leaf_add()
    line = q3
    leaf_add()
    line = q4
    leaf_add()
    line = q1
    leaf_double_sub()
    line = q2
    leaf_double_sub()
    line = q3
    leaf_double_sub()
    line = q4
    leaf_double_sub()
    halt(total)
  }
}
)mkpro";

} // namespace

void callee_hole_helper_matches_direct_call_semantics() {
  CompileOptions options;
  options.budget = 999999;
  options.disable_candidate_search = true;
  options.hoist_procs = true;

  CompileOptions hole_options = options;
  hole_options.callee_hole_straight_line_helper = true;

  const CompileResult baseline = compile_source(kCalleeHoleSource, options);
  require(!has_optimization(baseline, "callee-hole-straight-line-helper"),
          "pass should stay off without its option flag");

  const CompileResult hole = compile_source(kCalleeHoleSource, hole_options);
  require(has_optimization(hole, "callee-hole-straight-line-helper"),
          "walks differing only in their leaf call should merge into a skeleton");
  require(has_proof(hole, "callee-hole-indirect-call-targets"),
          "callee-hole dispatch should discharge its leaf-address proof");
  require(hole.steps.size() < baseline.steps.size(),
          "callee-hole skeleton should shrink the program: hole=" +
              std::to_string(hole.steps.size()) +
              " baseline=" + std::to_string(baseline.steps.size()));

  // total = (3+5+7+9) - 2*(3+5+7+9) = -24 per iteration.
  const std::string baseline_display = run_display(baseline, "direct-call baseline");
  const std::string hole_display = run_display(hole, "callee-hole variant");
  require(baseline_display.find("24") != std::string::npos &&
              baseline_display.front() == '-',
          "direct-call baseline should halt with -24, got " + baseline_display);
  require(hole_display == baseline_display,
          "callee-hole variant should match the direct-call baseline: hole=" + hole_display +
              " baseline=" + baseline_display);

  core::StackValueEqualityState equality;
  for (int index = 0; index < 4; ++index) {
    const core::StackValueEqualityTransfer transfer = core::transfer_stack_value_equality(
        equality, 0x61, core::StackValueEqualityStepKind::Recall);
    require(transfer != core::StackValueEqualityTransfer::Rejected,
            "independent recalls should be valid equality transfers");
  }
  require(core::stack_values_fully_equal(equality),
          "four independent recalls should erase the charged X/X2 difference");

  const auto run_recall_restore = [](const std::string& initial_x) {
    emulator::MK61 calc;
    const emulator::ProgramLoadResult loaded =
        calc.load_program({0x61, 0x61, 0x61, 0x61, 0x0a, 0x50});
    require(loaded.diagnostics.empty(), "recall/X2 probe should load");
    calc.set_register("1", "5");
    calc.input_number(initial_x, true);
    calc.press_sequence({"В/О", "С/П"});
    const emulator::RunResult run = calc.run_until_stable(1000, 6);
    require(run.stopped, "recall/X2 probe should halt");
    return trim_ascii_text(calc.display_text());
  };
  require(run_recall_restore("7") == run_recall_restore("8"),
          "four recalls should erase an initial X difference even when the next opcode restores X2");

  std::vector<IrOp> ir;
  ir.push_back(label("leaf_a"));
  ir.push_back(plain(0x0d));
  ir.push_back(terminal(IrKind::Return, 0x52));
  ir.push_back(label("leaf_b"));
  ir.push_back(plain(0x0d));
  ir.push_back(terminal(IrKind::Return, 0x52));
  // Occupy every stable selector and R1..R6. R0 is the only globally unused
  // register, so a successful skeleton must discharge the mutating proof.
  for (int index = 1; index <= 0x0e; ++index) {
    IrOp store;
    store.kind = IrKind::Store;
    store.register_name = core::register_name_for_index(index);
    store.opcode = 0x40 + index;
    ir.push_back(std::move(store));
  }
  ir.push_back(terminal(IrKind::Stop, 0x50));
  const auto append_region = [&](const std::string& leaf) {
    for (int index = 0; index < 10; ++index) {
      IrOp recall;
      recall.kind = IrKind::Recall;
      recall.register_name = "1";
      recall.opcode = 0x61;
      ir.push_back(std::move(recall));
    }
    ir.push_back(call(leaf));
  };
  append_region("leaf_a");
  ir.push_back(terminal(IrKind::Stop, 0x50));
  append_region("leaf_b");
  ir.push_back(terminal(IrKind::Stop, 0x50));

  CompileOptions mutating_options;
  mutating_options.callee_hole_straight_line_helper = true;
  const core::passes::PassResult mutating = core::passes::callee_hole_straight_line_helper(
      ir, core::passes::PassContext{.options = mutating_options});
  require(mutating.applied >= 2,
          "callee-hole pass should use a proved mutating selector when stable registers are busy");
  const auto mutating_hole = std::find_if(mutating.ops.begin(), mutating.ops.end(),
                                          [](const IrOp& op) {
                                            return op.kind == IrKind::IndirectCall &&
                                                   op.register_name == "0";
                                          });
  require(mutating_hole != mutating.ops.end(),
          "mutating callee-hole fixture should dispatch through free R0");
  require(std::any_of(mutating.ops.begin(), mutating.ops.end(), [](const IrOp& op) {
            return op.meta.comment.has_value() &&
                   op.meta.comment->find("callee-hole entry-X equivalence") != std::string::npos;
          }),
          "mutating skeleton should carry its entry-stack proof marker");

  CompileOptions gate_options;
  gate_options.callee_hole_straight_line_helper = true;
  CompileResult proved_mutating;
  proved_mutating.optimizations.push_back(
      OptimizationReport{.name = "callee-hole-straight-line-helper"});
  proved_mutating.steps = {
      step(0, 3, "callee-hole selector-value=31 indirect-target=30"),
      step(1, 1, "callee-hole selector-value=31 indirect-target=30"),
      step(2, 0x40, "callee-hole selector-value=31 indirect-target=30"),
      step(3, 0x53, "callee-hole skeleton call"),
      step(4, 4, "callee-hole selector-value=41 indirect-target=40"),
      step(5, 1, "callee-hole selector-value=41 indirect-target=40"),
      step(6, 0x40, "callee-hole selector-value=41 indirect-target=40"),
      step(7, 0x53, "callee-hole skeleton call"),
      step(20, 0x61, "callee-hole entry-X equivalence proof_a"),
      step(21, 0x61),
      step(22, 0x61),
      step(23, 0x61),
      step(24, 0xa0,
           "callee-hole indirect call; proof=proof_a; leaf-targets=30:leaf_a,40:leaf_b"),
      step(30, 0x0d, "callee-hole leaf entry leaf_a"),
      step(40, 0x0d, "callee-hole leaf entry leaf_b"),
  };
  require(optimizer_static_proof_gate_accepts_for_testing(gate_options, proved_mutating),
          "final static gate should accept a resolved R0 charge after four recalls erase X/X2");

  CompileResult live_entry_x = proved_mutating;
  live_entry_x.steps.erase(live_entry_x.steps.begin() + 11);
  require(!optimizer_static_proof_gate_accepts_for_testing(gate_options, live_entry_x),
          "final static gate should reject R0 when one stack slot still carries the charge");
}

} // namespace mkpro::tests
