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
  const std::optional<std::string> hole_gate_rejection =
      optimizer_static_proof_gate_rejection_reason_for_testing(hole_options, hole);
  require(!hole_gate_rejection.has_value(),
          "generated callee-hole final artifact should pass its static gate: " +
              hole_gate_rejection.value_or("unknown rejection"));
  require(std::count_if(hole.steps.begin(), hole.steps.end(), [](const ResolvedStep& step) {
            return step.comment.has_value() &&
                   step.comment->starts_with("callee-hole charge-entry store;");
          }) == 1,
          "stable callee-hole selector should use one shared charge-entry store");
  require(std::count_if(hole.steps.begin(), hole.steps.end(), [](const ResolvedStep& step) {
            return step.comment.has_value() &&
                   step.comment->starts_with("callee-hole charge-entry call;");
          }) == 2,
          "each merged region should call the shared charge entry");
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
  // register. Their later recalls keep the values live across both regions,
  // so a successful skeleton must still discharge the mutating R0 proof.
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
      if (index == 0)
        recall.meta.roles = {"shared-body-role"};
      ir.push_back(std::move(recall));
    }
    IrOp leaf_call = call(leaf);
    leaf_call.meta.roles = {"source-call-role:" + leaf};
    leaf_call.target_meta.roles = {"source-address-role:" + leaf};
    leaf_call.meta.semantic_call_origins = {leaf == "leaf_a" ? 101U : 202U};
    ir.push_back(std::move(leaf_call));
  };
  append_region("leaf_a");
  ir.push_back(terminal(IrKind::Stop, 0x50));
  append_region("leaf_b");
  for (int index = 1; index <= 0x0e; ++index) {
    IrOp recall;
    recall.kind = IrKind::Recall;
    recall.register_name = core::register_name_for_index(index);
    recall.opcode = 0x60 + index;
    ir.push_back(std::move(recall));
  }
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
  require(mutating_hole->meta.roles.empty() && mutating_hole->target_meta.roles.empty() &&
              mutating_hole->meta.semantic_call_origins ==
                  std::vector<std::uint64_t>({101U, 202U}),
          "shared callback should union opaque call origins without promoting source-specific "
          "cell roles");
  require(std::any_of(mutating.ops.begin(), mutating.ops.end(), [](const IrOp& op) {
            return op.kind == IrKind::Recall &&
                   op.meta.roles == std::vector<CellRole>({"shared-body-role"});
          }),
          "identical semantic roles on ordinary skeleton commands should be preserved");
  require(std::any_of(mutating.ops.begin(), mutating.ops.end(), [](const IrOp& op) {
            return op.meta.comment.has_value() &&
                   op.meta.comment->find("callee-hole entry-X equivalence") != std::string::npos;
          }),
          "mutating skeleton should carry its entry-stack proof marker");

  std::vector<IrOp> scoped_ir;
  scoped_ir.push_back(label("scoped_leaf_a"));
  scoped_ir.push_back(plain(0x0d));
  scoped_ir.push_back(terminal(IrKind::Return, 0x52));
  scoped_ir.push_back(label("scoped_leaf_b"));
  scoped_ir.push_back(plain(0x0d));
  scoped_ir.push_back(terminal(IrKind::Return, 0x52));
  IrOp use_r7;
  use_r7.kind = IrKind::Recall;
  use_r7.register_name = "7";
  use_r7.opcode = 0x67;
  scoped_ir.push_back(use_r7);
  scoped_ir.push_back(terminal(IrKind::Stop, 0x50));
  const auto append_scoped_region = [&](const std::string& leaf) {
    for (int index = 0; index < 10; ++index) {
      IrOp recall;
      recall.kind = IrKind::Recall;
      recall.register_name = "1";
      recall.opcode = 0x61;
      scoped_ir.push_back(std::move(recall));
    }
    scoped_ir.push_back(call(leaf));
  };
  append_scoped_region("scoped_leaf_a");
  scoped_ir.push_back(terminal(IrKind::Stop, 0x50));
  append_scoped_region("scoped_leaf_b");
  scoped_ir.push_back(terminal(IrKind::Stop, 0x50));
  const core::passes::PassResult scoped = core::passes::callee_hole_straight_line_helper(
      scoped_ir, core::passes::PassContext{.options = mutating_options});
  require(scoped.applied >= 2,
          "callee-hole pass should reuse a globally used selector when CFG liveness proves it dead");
  require(std::any_of(scoped.ops.begin(), scoped.ops.end(), [](const IrOp& op) {
            return op.kind == IrKind::IndirectCall && op.register_name == "7" &&
                   op.meta.comment.has_value() &&
                   op.meta.comment->find("selector-scope=dead") != std::string::npos;
          }),
          "locally dead stable selector should carry a final-proof scope marker");

  std::vector<IrOp> late_ir;
  const auto append_late_region = [&](const std::string& leaf) {
    for (int index = 0; index < 10; ++index) {
      IrOp recall;
      recall.kind = IrKind::Recall;
      recall.register_name = "1";
      recall.opcode = 0x61;
      late_ir.push_back(std::move(recall));
    }
    late_ir.push_back(call(leaf));
  };
  append_late_region("late_leaf_a");
  late_ir.push_back(terminal(IrKind::Stop, 0x50));
  append_late_region("late_leaf_b");
  late_ir.push_back(terminal(IrKind::Stop, 0x50));
  late_ir.push_back(label("late_leaf_a"));
  late_ir.push_back(plain(0x0d));
  late_ir.push_back(terminal(IrKind::Return, 0x52));
  late_ir.push_back(label("late_leaf_b"));
  late_ir.push_back(plain(0x0d));
  late_ir.push_back(terminal(IrKind::Return, 0x52));

  const core::passes::PassResult late = core::passes::callee_hole_straight_line_helper(
      late_ir, core::passes::PassContext{.options = mutating_options});
  require(late.applied >= 2,
          "callee-hole pass should defer stable selector digits until final layout");
  const auto late_hole = std::find_if(late.ops.begin(), late.ops.end(), [](const IrOp& op) {
    return op.kind == IrKind::IndirectCall &&
           std::find(op.meta.roles.begin(), op.meta.roles.end(),
                     "late-decimal-selector-consumer") != op.meta.roles.end();
  });
  require(late_hole != late.ops.end() && late_hole->meta.indirect_flow_targets.has_value(),
          "late-bound callee-hole call should carry an authoritative symbolic target set");
  const auto has_late_target = [&](const std::string& label_name) {
    return std::any_of(late_hole->meta.indirect_flow_targets->begin(),
                       late_hole->meta.indirect_flow_targets->end(),
                       [&](const IrTarget& target) {
                         const auto* label_target = std::get_if<std::string>(&target);
                         return label_target != nullptr && *label_target == label_name;
                       });
  };
  require(late_hole->meta.indirect_flow_targets->size() == 2U &&
              has_late_target("late_leaf_a") && has_late_target("late_leaf_b"),
          "late-bound callee-hole call should preserve every leaf label exactly");
  const auto role_count = [&](const std::string& prefix) {
    return std::count_if(late.ops.begin(), late.ops.end(), [&](const IrOp& op) {
      return std::any_of(op.meta.roles.begin(), op.meta.roles.end(), [&](const CellRole& role) {
        return role.starts_with(prefix);
      });
    });
  };
  require(role_count("late-decimal-selector-high:") == 2 &&
              role_count("late-decimal-selector-low:") == 2,
          "each late selector charge should expose independently bindable decimal digits");

  std::vector<IrOp> fixed_indirect_ir;
  fixed_indirect_ir.push_back(label("fixed_leaf_a"));
  fixed_indirect_ir.push_back(plain(0x0d));
  fixed_indirect_ir.push_back(terminal(IrKind::Return, 0x52));
  fixed_indirect_ir.push_back(label("fixed_leaf_b"));
  fixed_indirect_ir.push_back(plain(0x0d));
  fixed_indirect_ir.push_back(terminal(IrKind::Return, 0x52));
  const auto append_fixed_indirect_region = [&](const std::string& leaf,
                                                 const std::string& fixed_semantic) {
    for (int index = 0; index < 6; ++index) {
      IrOp recall;
      recall.kind = IrKind::Recall;
      recall.register_name = "1";
      recall.opcode = 0x61;
      fixed_indirect_ir.push_back(std::move(recall));
    }
    fixed_indirect_ir.push_back(call(leaf));
    IrOp fixed_call;
    fixed_call.kind = IrKind::IndirectCall;
    fixed_call.register_name = "b";
    fixed_call.opcode = 0xab;
    fixed_call.meta.indirect_flow_targets =
        std::vector<IrTarget>{IrTarget{std::string("fixed_shared_leaf")}};
    fixed_indirect_ir.push_back(std::move(fixed_call));
    IrOp fixed_direct_call = call("fixed_shared_leaf");
    fixed_direct_call.semantic = fixed_semantic;
    fixed_indirect_ir.push_back(std::move(fixed_direct_call));
    fixed_indirect_ir.push_back(call(leaf));
    fixed_indirect_ir.push_back(terminal(IrKind::Stop, 0x50));
  };
  append_fixed_indirect_region("fixed_leaf_a", "source-function-call");
  append_fixed_indirect_region("fixed_leaf_b", "source-procedure-call");
  fixed_indirect_ir.push_back(label("fixed_shared_leaf"));
  fixed_indirect_ir.push_back(plain(0x0d));
  fixed_indirect_ir.push_back(terminal(IrKind::Return, 0x52));

  const core::passes::PassResult fixed_indirect =
      core::passes::callee_hole_straight_line_helper(
          fixed_indirect_ir, core::passes::PassContext{.options = mutating_options});
  require(fixed_indirect.applied >= 2,
          "callee-hole pass should share a skeleton across an invariant indirect call");
  require(std::count_if(fixed_indirect.ops.begin(), fixed_indirect.ops.end(), [](const IrOp& op) {
            return op.kind == IrKind::IndirectCall && op.register_name == "b";
          }) == 1,
          "the invariant indirect call should occur once in the shared skeleton");

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

  CompileResult natural_skeleton_calls = proved_mutating;
  natural_skeleton_calls.steps.at(3).opcode = 0xa7;
  natural_skeleton_calls.steps.at(3).comment =
      "callee-hole skeleton call; preloaded R7=20 indirect-target=20 indirect flow";
  natural_skeleton_calls.steps.at(7).opcode = 0xa7;
  natural_skeleton_calls.steps.at(7).comment =
      "callee-hole skeleton call; preloaded R7=20 indirect-target=20 indirect flow";
  require(!optimizer_static_proof_gate_accepts_for_testing(gate_options,
                                                            natural_skeleton_calls),
          "callee-hole gate should reject an indirect skeleton dispatch without its layout proof");
  natural_skeleton_calls.optimizations.push_back(
      OptimizationReport{.name = "natural-target-component-layout"});
  require(optimizer_static_proof_gate_accepts_for_testing(gate_options,
                                                           natural_skeleton_calls),
          "callee-hole gate should compose with a proved natural-target skeleton dispatch");

  CompileResult natural_charge_entry_calls;
  natural_charge_entry_calls.optimizations = {
      OptimizationReport{.name = "callee-hole-straight-line-helper"},
      OptimizationReport{.name = "natural-target-component-layout"},
  };
  natural_charge_entry_calls.steps = {
      step(0, 3, "callee-hole selector-value=30 indirect-target=30"),
      step(1, 0, "callee-hole selector-value=30 indirect-target=30"),
      step(2, 0xa7,
           "callee-hole charge-entry call; proof=proof_entry; selector=e; "
           "preloaded R7=20 indirect-target=20 indirect flow"),
      step(3, 0x51),
      step(4, 0),
      step(5, 4, "callee-hole selector-value=40 indirect-target=40"),
      step(6, 0, "callee-hole selector-value=40 indirect-target=40"),
      step(7, 0xa7,
           "callee-hole charge-entry call; proof=proof_entry; selector=e; "
           "preloaded R7=20 indirect-target=20 indirect flow"),
      step(8, 0x51),
      step(9, 0),
      step(20, 0x4e,
           "callee-hole charge-entry store; proof=proof_entry; selector=e"),
      step(21, 0x61),
      step(22, 0xae,
           "callee-hole indirect call; proof=proof_entry; "
           "leaf-targets=30:leaf_a,40:leaf_b"),
      step(23, 0x52),
      step(30, 0x0d, "callee-hole leaf entry leaf_a"),
      step(31, 0x52),
      step(40, 0x0d, "callee-hole leaf entry leaf_b"),
      step(41, 0x52),
  };
  require(optimizer_static_proof_gate_accepts_for_testing(gate_options,
                                                           natural_charge_entry_calls),
          "callee-hole gate should preserve a charge-entry marker followed by proved "
          "natural-target metadata");
  natural_charge_entry_calls.steps.at(2).comment =
      "callee-hole charge-entry call; proof=proof_entry; selector=ee; "
      "preloaded R7=20 indirect-target=20 indirect flow";
  require(!optimizer_static_proof_gate_accepts_for_testing(gate_options,
                                                            natural_charge_entry_calls),
          "callee-hole gate should reject a charge-entry marker with a noncanonical selector");
  natural_charge_entry_calls.steps.at(2).comment =
      "callee-hole charge-entry call; proof=proof_entry; selector=e; "
      "preloaded R7=20 indirect-target=20 indirect flow";

  CompileResult typed_terminal_entry = natural_charge_entry_calls;
  typed_terminal_entry.steps.at(8).opcode = 0xb0;
  typed_terminal_entry.steps.at(8).mnemonic = "К X->П 0";
  typed_terminal_entry.steps.at(8).comment = "typed indirect store";
  typed_terminal_entry.steps.at(9).opcode = 0x50;
  typed_terminal_entry.steps.at(9).mnemonic = "С/П";
  typed_terminal_entry.steps.at(9).comment = "terminal stop";
  require(!optimizer_static_proof_gate_accepts_for_testing(gate_options,
                                                            typed_terminal_entry),
          "callee-hole gate should fail closed without typed memory targets and terminal-stop "
          "metadata");
  for (const ResolvedStep& resolved : typed_terminal_entry.steps) {
    MachineItem item = MachineItem::op(resolved.opcode, resolved.mnemonic);
    item.comment = resolved.comment;
    if (resolved.address == 8)
      item.indirect_memory_targets = std::vector<int>{4, 5, 6, 7};
    if (resolved.address == 9)
      item.stop_disposition = StopDisposition::Terminal;
    typed_terminal_entry.items.push_back(std::move(item));
  }
  require(optimizer_static_proof_gate_accepts_for_testing(gate_options,
                                                           typed_terminal_entry),
          "callee-hole gate should use final typed memory targets and terminal-stop metadata "
          "without relying on comments");

  CompileOptions preloaded_hole_options = gate_options;
  preloaded_hole_options.preloaded_indirect_flow = true;
  CompileResult preloaded_hole = natural_skeleton_calls;
  preloaded_hole.preloads.push_back(PreloadReport{
      .register_name = "7",
      .value = "20",
  });
  const std::optional<std::string> combined_rejection =
      optimizer_static_proof_gate_rejection_reason_for_testing(preloaded_hole_options,
                                                                preloaded_hole);
  require(!combined_rejection.has_value(),
          "combined callee-hole/preloaded-flow gate should accept both proved final artifacts: " +
              combined_rejection.value_or("unknown rejection"));
  preloaded_hole.preloads.clear();
  require(!optimizer_static_proof_gate_accepts_for_testing(preloaded_hole_options,
                                                            preloaded_hole),
          "combined callee-hole/preloaded-flow gate should reject a missing preload proof");

  CompileResult live_entry_x = proved_mutating;
  live_entry_x.steps.erase(live_entry_x.steps.begin() + 11);
  require(!optimizer_static_proof_gate_accepts_for_testing(gate_options, live_entry_x),
          "final static gate should reject R0 when one stack slot still carries the charge");

  CompileResult proved_scoped;
  proved_scoped.optimizations.push_back(
      OptimizationReport{.name = "callee-hole-straight-line-helper"});
  proved_scoped.steps = {
      step(0, 3, "callee-hole selector-value=30 indirect-target=30; selector-scope=dead"),
      step(1, 0, "callee-hole selector-value=30 indirect-target=30; selector-scope=dead"),
      step(2, 0x47, "callee-hole selector-value=30 indirect-target=30; selector-scope=dead"),
      step(3, 0x53, "callee-hole skeleton call; selector-scope=dead"),
      step(4, 0x20),
      step(5, 0x47),
      step(6, 4, "callee-hole selector-value=40 indirect-target=40; selector-scope=dead"),
      step(7, 0, "callee-hole selector-value=40 indirect-target=40; selector-scope=dead"),
      step(8, 0x47, "callee-hole selector-value=40 indirect-target=40; selector-scope=dead"),
      step(9, 0x53, "callee-hole skeleton call; selector-scope=dead"),
      step(10, 0x20),
      step(11, 0x47),
      step(20, 0x61, "callee-hole selector-scope entry proof_s"),
      step(21, 0x61),
      step(22, 0x61),
      step(23, 0x61),
      step(24, 0xa7,
           "callee-hole indirect call; proof=proof_s; leaf-targets=30:leaf_a,40:leaf_b; "
           "selector-scope=dead"),
      step(25, 0x52, "callee-hole helper return; callee-hole selector-scope end proof_s"),
      step(30, 0x0d, "callee-hole leaf entry leaf_a"),
      step(31, 0x52),
      step(40, 0x0d, "callee-hole leaf entry leaf_b"),
      step(41, 0x52),
  };
  require(optimizer_static_proof_gate_accepts_for_testing(gate_options, proved_scoped),
          "final static gate should accept a reused selector killed on every return path");

  CompileResult leaked_scoped = proved_scoped;
  leaked_scoped.steps.at(11).opcode = 0x67;
  require(!optimizer_static_proof_gate_accepts_for_testing(gate_options, leaked_scoped),
          "final static gate should reject a reused selector read after the skeleton returns");
}

} // namespace mkpro::tests
