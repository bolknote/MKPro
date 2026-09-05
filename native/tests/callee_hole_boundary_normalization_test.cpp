#include "mkpro/compiler.hpp"
#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/compiler_static_proof_gate.hpp"
#include "mkpro/core/late_bound_decimal_selector.hpp"
#include "mkpro/core/passes/shared_straight_line_helper.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace mkpro::tests {
namespace {

IrOp operation(int opcode) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = opcode;
  return op;
}

IrOp recall(int reg) {
  IrOp op;
  op.kind = IrKind::Recall;
  op.opcode = 0x60 + reg;
  op.register_name = std::string(1, "0123456789abcde"[reg]);
  return op;
}

IrOp named(const std::string& name) {
  IrOp op;
  op.kind = IrKind::Label;
  op.name = name;
  return op;
}

IrOp transfer(IrKind kind, const std::string& name) {
  IrOp op;
  op.kind = kind;
  op.opcode = kind == IrKind::Call ? 0x53 : 0x51;
  op.target = name;
  return op;
}

IrOp returning() {
  IrOp op;
  op.kind = IrKind::Return;
  op.opcode = 0x52;
  return op;
}

IrOp stopping(bool terminal) {
  IrOp op;
  op.kind = IrKind::Stop;
  op.opcode = 0x50;
  if (terminal) op.meta.stop_disposition = StopDisposition::Terminal;
  return op;
}

std::vector<IrOp> fixture(bool observe_last_x = false) {
  std::vector<IrOp> ops{transfer(IrKind::Jump, "start"), named("walk_quotients")};
  for (int i = 0; i < 4; ++i) {
    ops.push_back(recall(7 - i));
    ops.push_back(recall(i == 1 ? 2 : 1));
    if (i >= 2) {
      ops.push_back(recall(2));
      ops.push_back(operation(i == 2 ? 0x10 : 0x11));
      ops.push_back(transfer(IrKind::Call, "positive_index"));
    }
    ops.push_back(transfer(IrKind::Call, "quotient_leaf"));
  }
  ops.push_back(returning());
  ops.push_back(named("walk_products"));
  for (int i = 0; i < 2; ++i) {
    ops.push_back(recall(7 - i));
    ops.push_back(recall(i == 0 ? 1 : 2));
    ops.push_back(transfer(IrKind::Call, "product_leaf"));
  }
  ops.push_back(recall(5));
  ops.push_back(recall(2));
  ops.push_back(transfer(IrKind::Call, "product_index_tail"));
  ops.push_back(recall(4));
  ops.push_back(recall(2));
  ops.push_back(operation(0x0b));
  ops.push_back(named("product_index_tail"));
  ops.push_back(recall(1));
  ops.push_back(operation(0x10));
  ops.push_back(transfer(IrKind::Call, "positive_index"));
  ops.push_back(transfer(IrKind::Jump, "product_leaf"));
  ops.push_back(named("positive_index"));
  ops.push_back(operation(0x31));
  ops.push_back(operation(0x01));
  ops.push_back(operation(0x10));
  ops.push_back(returning());
  ops.push_back(named("quotient_leaf"));
  if (observe_last_x) ops.push_back(operation(0x0f));
  ops.push_back(operation(0x22));
  ops.push_back(operation(0x13));
  ops.push_back(operation(0x10));
  ops.push_back(returning());
  ops.push_back(named("product_leaf"));
  ops.push_back(operation(0x22));
  ops.push_back(operation(0x12));
  ops.push_back(operation(0x10));
  ops.push_back(returning());
  ops.push_back(named("start"));
  ops.push_back(transfer(IrKind::Call, "walk_quotients"));
  ops.push_back(stopping(false));
  ops.push_back(transfer(IrKind::Call, "walk_products"));
  ops.push_back(stopping(true));
  return ops;
}

std::vector<int> bytecode(const std::vector<IrOp>& ops) {
  std::map<std::string, int> labels;
  int address = 0;
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Label) labels.emplace(op.name, address);
    else address += core::passes::cells_per_op(op);
  }
  require(address <= 105, "unrelated boundary fixture must fit the physical calculator");
  std::vector<int> result;
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Label) continue;
    int opcode = op.opcode;
    for (const auto& role : op.meta.roles) {
      constexpr const char* high = "late-decimal-selector-high:";
      constexpr const char* low = "late-decimal-selector-low:";
      if (role.starts_with(high)) opcode = labels.at(role.substr(std::string(high).size())) / 10;
      if (role.starts_with(low)) opcode = labels.at(role.substr(std::string(low).size())) % 10;
    }
    result.push_back(opcode);
    if (opcode_by_code(op.opcode).takes_address) {
      const int target = std::holds_alternative<std::string>(op.target)
                             ? labels.at(std::get<std::string>(op.target))
                             : std::get<int>(op.target);
      require(target >= 0 && target <= 104, "fixture target must be an official address");
      result.push_back((target / 10) * 16 + target % 10);
    }
  }
  return result;
}

using Snapshot = std::array<std::string, 6>;

std::vector<Snapshot> observations(const std::vector<IrOp>& ops, const std::string& input) {
  emulator::MK61 calc;
  require(calc.load_program(bytecode(ops)).diagnostics.empty(), "boundary fixture should load");
  calc.set_register("1", "2");
  calc.set_register("2", "3");
  for (int r = 4; r <= 7; ++r) calc.set_register(std::to_string(r), std::to_string(r + 1));
  calc.set_register("Y", "73");
  calc.set_register("Z", "29");
  calc.set_register("T", "17");
  calc.input_number(input, true);
  calc.press_sequence({"В/О", "С/П"});
  std::vector<Snapshot> result;
  for (int round = 0; round < 2; ++round) {
    require(calc.run_until_stable(5000, 6).stopped, "shared walk should return and stop");
    result.push_back({calc.display_text(), calc.read_register("X"), calc.read_register("Y"),
                      calc.read_register("Z"), calc.read_register("T"), calc.read_register("X1")});
    if (round == 0) {
      calc.input_number(input, true);
      calc.press("С/П");
    }
  }
  return result;
}

bool fused(const core::passes::PassResult& result) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(), [](const auto& op) {
    return op.name == "callee-hole-boundary-normalization";
  });
}

} // namespace

void callee_hole_boundary_fusion_preserves_stack_and_control() {
  CompileOptions options;
  options.callee_hole_straight_line_helper = true;
  options.callee_hole_boundary_normalization = true;
  const auto source = fixture();
  const auto normalized = core::normalize_callee_hole_boundaries(source);
  require(normalized.expanded_calls > 0 && normalized.arithmetic_groups > 0,
          "structural wrapper expansion and arithmetic normalization should compose");
  const auto result = core::passes::callee_hole_straight_line_helper(source, {options});
  require(fused(result), "live-X walks with unequal leaf calls should share one repaired skeleton");
  require(bytecode(result.ops).size() + 5 <= bytecode(source).size(),
          "boundary expansion must repay its cells with a nontrivial reduction");
  require(std::count_if(result.ops.begin(), result.ops.end(), [](const IrOp& op) {
            return op.kind == IrKind::IndirectCall || op.kind == IrKind::IndirectJump;
          }) == 4,
          "the last leaf must share the skeleton despite call/return versus tail-jump syntax");
  for (const auto& input : {"0", "11", "-7", "0.125", "12345"})
    require(observations(source, input) == observations(result.ops, input),
            std::string("shared walk must preserve X/Y/Z/T/X1 and display for ") + input);

  auto renamed = source;
  for (IrOp& op : renamed) {
    op.meta.comment.reset();
    if (op.kind == IrKind::Label) op.name = "unrelated_" + op.name;
    if (auto* target = std::get_if<std::string>(&op.target)) *target = "unrelated_" + *target;
  }
  require(fused(core::passes::callee_hole_straight_line_helper(renamed, {options})),
          "outlining must not recognize source names or comments");
  require(!fused(core::passes::callee_hole_straight_line_helper(fixture(true), {options})),
          "a leaf observing old X1 must block selector stack repair");
  auto opaque = source;
  for (IrOp& op : opaque)
    if (op.kind == IrKind::Call && op.target == IrTarget(std::string("product_index_tail")))
      op.meta.raw = true;
  require(!fused(core::passes::callee_hole_straight_line_helper(opaque, {options})),
          "raw call boundaries must remain opaque");

  auto manual = source;
  for (IrOp& op : manual)
    if (op.kind == IrKind::Stop || op.kind == IrKind::Store)
      op.meta.manual_interaction.emplace();
  require(core::callee_hole_return_stack_fits(manual),
          "preserved typed manual-input sites do not make return flow opaque");
  require(!core::callee_hole_return_stack_fits(opaque),
          "raw calls must reject the return-stack proof before following their targets");

  std::vector<IrOp> too_deep{transfer(IrKind::Call, "depth_0"), stopping(true)};
  for (int depth = 0; depth < 6; ++depth) {
    too_deep.push_back(named("depth_" + std::to_string(depth)));
    if (depth < 5) too_deep.push_back(transfer(IrKind::Call, "depth_" + std::to_string(depth + 1)));
    too_deep.push_back(returning());
  }
  require(!core::callee_hole_return_stack_fits(too_deep),
          "a sixth live return address must reject outlining");
  const std::vector<IrOp> recursive{transfer(IrKind::Call, "recursive"), stopping(true),
                                   named("recursive"), transfer(IrKind::Call, "recursive"), returning()};
  require(!core::callee_hole_return_stack_fits(recursive), "recursive call depth is not guessed");
  require(!core::callee_hole_return_stack_fits({recall(1)}),
          "falling off the known IR is not a proved terminal continuation");

  std::vector<IrOp> tail_only{transfer(IrKind::Jump, "begin")};
  for (int mode = 0; mode < 2; ++mode) {
    tail_only.push_back(named(mode == 0 ? "first_walk" : "second_walk"));
    for (int i = 0; i < 4; ++i) {
      tail_only.push_back(recall(7 - i));
      tail_only.push_back(recall(2));
      tail_only.push_back(transfer(mode == 1 && i == 3 ? IrKind::Jump : IrKind::Call,
                                   mode == 0 ? "first_leaf" : "second_leaf"));
    }
    if (mode == 0) tail_only.push_back(returning());
  }
  for (int mode = 0; mode < 2; ++mode) {
    tail_only.push_back(named(mode == 0 ? "first_leaf" : "second_leaf"));
    tail_only.push_back(operation(0x22));
    tail_only.push_back(operation(mode == 0 ? 0x13 : 0x12));
    tail_only.push_back(operation(0x10));
    tail_only.push_back(returning());
  }
  tail_only.push_back(named("begin"));
  tail_only.push_back(transfer(IrKind::Call, "first_walk"));
  tail_only.push_back(stopping(false));
  tail_only.push_back(transfer(IrKind::Call, "second_walk"));
  tail_only.push_back(stopping(true));
  const auto tail_normalized = core::normalize_callee_hole_boundaries(tail_only);
  require(tail_normalized.expanded_calls > 0,
          "tail equivalence must be exposed without an outlined arithmetic wrapper");
  require(observations(tail_only, "11") == observations(tail_normalized.ops, "11"),
          "standalone tail expansion must preserve the caller continuation");
  const auto tail_result = core::passes::callee_hole_straight_line_helper(tail_only, {options});
  require(bytecode(tail_result.ops).size() <= bytecode(tail_only).size(),
          "tail expansion must never displace an equally good ordinary skeleton");
  const auto tail_expected = observations(tail_only, "11");
  const auto tail_actual = observations(tail_result.ops, "11");
  std::string tail_difference;
  for (std::size_t stop = 0; stop < tail_expected.size(); ++stop)
    for (std::size_t slot = 0; slot < tail_expected[stop].size(); ++slot)
      if (tail_expected[stop][slot] != tail_actual[stop][slot])
        tail_difference += " stop=" + std::to_string(stop) + " slot=" + std::to_string(slot) +
            " expected=" + tail_expected[stop][slot] + " actual=" + tail_actual[stop][slot];
  require(tail_expected == tail_actual,
          "standalone tail normalization must preserve the live accumulator and caller stack; "
          "applied=" + std::to_string(tail_result.applied) +
          " boundary=" + std::to_string(fused(tail_result)) + tail_difference);

  std::vector<IrOp> selector_ir;
  IrOp entry_store;
  entry_store.kind = IrKind::Store;
  entry_store.register_name = "3";
  entry_store.opcode = 0x43;
  require(core::selector_charge_has_automatic_entry_lift({entry_store, operation(2)}, 1),
          "a direct store closes decimal entry before a selector charge");
  entry_store.meta.raw = true;
  require(!core::selector_charge_has_automatic_entry_lift({entry_store, operation(2)}, 1),
          "raw entry protocols must not acquire an inferred automatic-lift proof");
  for (int prior : {1, 0x0e})
    require(!core::selector_charge_has_automatic_entry_lift({operation(prior), operation(2)}, 1),
            "active digits and Enter cannot prove an automatic selector-entry lift");
  std::vector<IrOp> called_entry{transfer(IrKind::Call, "leaf"), stopping(true),
                                returning(), named("leaf"), operation(2)};
  require(core::selector_charge_has_automatic_entry_lift(called_entry, 4),
          "a call closes decimal entry even when the caller entered a literal");
  called_entry.insert(called_entry.begin() + 3, operation(1));
  require(!core::selector_charge_has_automatic_entry_lift(called_entry, 5),
          "one open fallthrough predecessor blocks a mixed-entry automatic lift");

  const auto charge_stack = [](bool explicit_lift, int call, int closer) {
    std::vector<int> program{0x64, 0x63, 0x62, 0x61, 0x10};
    if (closer >= 0) program.push_back(closer);
    int target = 0;
    if (call) {
      if (call == 1) program.insert(program.end(), {0x53, 0, 0x50});
      else program.insert(program.end(), {0xa8, 0x50});
      target = static_cast<int>(program.size());
      if (call == 1) program[program.size() - 2] = (target / 10) * 16 + target % 10;
    }
    if (explicit_lift) program.push_back(0x0e);
    program.insert(program.end(), {2, 0, 0x4e, 0x25, 0x4a, 0x25, 0x4b,
                                    0x25, 0x4c, 0x25, 0x4d, call ? 0x52 : 0x50});
    emulator::MK61 machine;
    require(machine.load_program(program).diagnostics.empty(), "selector fact must load");
    machine.set_register("1", "2"); machine.set_register("2", "15");
    machine.set_register("3", "5"); machine.set_register("4", "7");
    machine.set_register("8", std::to_string(target));
    machine.press("В/О"); machine.press("С/П");
    require(machine.run_until_stable(1000, 6).stopped, "selector fact must stop");
    return std::array<std::string, 5>{machine.read_register("a"), machine.read_register("b"),
                                    machine.read_register("c"), machine.read_register("d"),
                                    machine.read_register("e")};
  };
  for (int closer : {0x43, 1, 0x0e}) {
    for (int call : {0, 1, 2}) {
      const bool equivalent = charge_stack(true, call, closer) == charge_stack(false, call, closer);
      require(equivalent == (call || closer == 0x43),
              "automatic lift must preserve X/Y/Z/T and selector after a closer, "
              "but not after an open digit or an unclosed Enter");
    }
  }
  for (const std::string name : {"opaque:alpha", "opaque:beta"}) {
    for (const auto part : {core::LateBoundDecimalSelectorPart::High,
                           core::LateBoundDecimalSelectorPart::Low}) {
      IrOp digit = operation(0);
      digit.meta.mnemonic = "0";
      digit.meta.roles = {core::make_late_bound_decimal_selector_role(part, name)};
      digit.meta.comment = "callee-hole selector-value=0 indirect-target=0; selector-scope=dead";
      selector_ir.push_back(digit);
    }
  }
  IrOp dispatch;
  dispatch.kind = IrKind::IndirectCall;
  dispatch.register_name = "7";
  dispatch.opcode = 0xa7;
  dispatch.meta.comment = "callee-hole indirect call; proof=opaque; "
                          "leaf-targets=0:opaque:alpha,0:opaque:beta";
  selector_ir.push_back(dispatch);
  selector_ir.push_back(named("opaque:alpha"));
  selector_ir.push_back(returning());
  selector_ir.push_back(named("opaque:beta"));
  selector_ir.push_back(returning());
  const auto bound = core::rebind_late_bound_decimal_selectors(
      lower_ir_to_machine(selector_ir), {.minimum_target_address = 0});
  require(bound.diagnostics.empty() && bound.proofs.size() == 2,
          "typed selector fixture should bind both opaque leaf names");
  auto relocated = bound.items;
  relocated.insert(relocated.begin() + 4, MachineItem::op(0x54, "KNOP"));
  const auto rebound = core::rebind_late_bound_decimal_selectors(
      relocated, {.minimum_target_address = 0});
  require(rebound.diagnostics.empty() && rebound.proofs.size() == 2,
          "moving leaf commands should atomically rebind both selectors");
  std::string delivered_leaves;
  for (std::size_t i = 0; i < rebound.proofs.size(); ++i) {
    const auto& proof = rebound.proofs[i];
    require(proof.target_address == bound.proofs[i].target_address + 1,
            "relocation must change the selector's executable value");
    const auto& comment = rebound.items[proof.low_item_index].comment;
    require(comment.has_value() && comment->find("selector-value=" +
                std::to_string(proof.target_address)) != std::string::npos &&
                comment->find("selector-scope=dead") != std::string::npos,
            "rebound proof annotations must retain ownership and describe the delivered digits");
    if (!delivered_leaves.empty()) delivered_leaves += ",";
    delivered_leaves += std::to_string(proof.target_address) + ":" + proof.target_label;
  }
  require(std::any_of(rebound.items.begin(), rebound.items.end(), [&](const MachineItem& item) {
            return item.comment.has_value() &&
                   item.comment->find("leaf-targets=" + delivered_leaves) != std::string::npos;
          }), "dispatch annotations must move with opaque colon-containing leaf labels");
  auto raw_selector = relocated;
  raw_selector[0].raw = true;
  const auto rejected = core::rebind_late_bound_decimal_selectors(
      raw_selector, {.minimum_target_address = 0});
  require(!rejected.diagnostics.empty() && rejected.proofs.empty() &&
              rejected.items.size() == raw_selector.size() &&
              std::equal(raw_selector.begin(), raw_selector.end(), rejected.items.begin(),
                         machine_items_equal),
          "failed rebinding must not publish either changed digits or changed annotations");

  const std::string entry_source = R"mkpro(
program FunctionEntryAlternatives {
  state {
    value: packed = 2
  }
  fn adjust(direction) {
    value = (value + direction) * 2 + 3
  }
  loop {
    adjust(1)
    show(value)
    adjust(-1)
    show(value)
  }
}
)mkpro";
  const auto run_entry = [](const CompileResult& compiled) {
    require(compiled.implemented && compiled.steps.size() <= 105,
            "generic entry-ABI fixture must fit the physical emulator");
    std::vector<int> codes;
    for (const auto& step : compiled.steps) codes.push_back(step.opcode);
    emulator::MK61 calc;
    require(calc.load_program(codes).diagnostics.empty(), "entry-ABI fixture must load");
    for (const auto& preload : compiled.preloads) {
      std::string value;
      for (char ch : preload.value) {
        switch (ch) {
          case 'A': value += '-'; break;
          case 'B': value += 'L'; break;
          case 'C': value += "С"; break;
          case 'D': value += "Г"; break;
          case 'E': value += "Е"; break;
          case 'F': value += '_'; break;
          default: value += ch; break;
        }
      }
      calc.set_register(preload.register_name, value);
    }
    calc.press_sequence({"В/О", "С/П"});
    std::vector<std::string> values;
    for (int round = 0; round < 4; ++round) {
      require(calc.run_until_stable(2000, 6).stopped,
              "each entry ABI must preserve the return continuation across stops");
      values.push_back(calc.display_text());
      values.push_back(calc.read_register(compiled.registers.at("value")));
      if (round != 3) calc.press("С/П");
    }
    return values;
  };
  CompileOptions entry_options;
  entry_options.analysis = true;
  entry_options.budget = 999999;
  entry_options.disable_candidate_search = true;
  entry_options.hoist_procs = true;
  const auto entry_expected = run_entry(compile_source(entry_source, entry_options));
  for (unsigned mask = 1; mask < 8; ++mask) {
    auto variant = entry_options;
    variant.stack_argument_helper_entries = (mask & 1U) != 0;
    variant.single_x_expression_helper_entries = (mask & 1U) != 0;
    variant.x_param_value_functions = (mask & 6U) != 0;
    variant.sign_normalized_x_param = (mask & 2U) != 0;
    variant.x_param_y_stack_stored_entry = (mask & 4U) != 0;
    require(run_entry(compile_source(entry_source, variant)) == entry_expected,
            "entry ABI subsets must preserve displays, persistent state and repeated returns");
  }
}

void callee_hole_boundary_fusion_final_artifact_contract() {
  const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
  std::ifstream input(root / "examples/pending-optimizer/tic-tac-toe-4x4.mkpro");
  require(input.is_open(), "4x4 regression source must be available");
  const std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
  CompileOptions options;
  options.analysis = true;
  options.budget = 999999;
  options.disable_candidate_search = true;
  options.hoist_procs = true;
  options.proc_layout_strategy = "reverse";
  options.callee_hole_straight_line_helper = true;
  options.callee_hole_boundary_normalization = true;
  options.packed_score_accumulator_helpers = true;
  options.outline_single_use_packed_score_shared_tails = true;
  options.defer_return_suffix_until_callee_hole = true;
  options.x_param_value_functions = true;
  options.allow_size_neutral_selector_seed_reuse = true;
  const CompileResult result = compile_source(source, options);
  require(result.implemented &&
              std::none_of(result.diagnostics.begin(), result.diagnostics.end(),
                           [](const Diagnostic& d) { return d.severity == DiagnosticSeverity::Error; }),
          "the fused compiler artifact must be implemented without error diagnostics");
  const auto rejection = optimizer_static_proof_gate_rejection_reason_for_testing(options, result);
  require(!rejection.has_value(), "complete fused artifact must pass its final proof: " +
                                 rejection.value_or(""));
  require(result.steps.size() <= 133,
          "traversal sharing must compose with the retained selector-store fallthrough");
  const auto helper_summary = [&](const std::string& label) -> const SizeHelperSummaryReport* {
    const auto& helpers = result.size_attribution.helpers;
    const auto it = std::find_if(helpers.begin(), helpers.end(), [&](const auto& helper) {
      return helper.label == label;
    });
    return it == helpers.end() ? nullptr : &*it;
  };
  const auto* score_summary = helper_summary("packed_score accumulator helper");
  require(score_summary != nullptr && score_summary->body_cells == 8,
          "callback-only helpers must retain their original attribution and return boundary");
  const auto* suffix_summary = helper_summary("return suffix gadget");
  require(suffix_summary != nullptr && suffix_summary->body_cells == 4,
          "an indirect tail dispatch must not attribute the following caller code to its helper");
  const auto* mark_summary = helper_summary("mark_lines_and_check");
  require(mark_summary != nullptr &&
              mark_summary->details.contains("valueAwareNestedCallInputNames") &&
              mark_summary->details.at("valueAwareNestedCallInputNames") == "best_score" &&
              mark_summary->details.contains("valueAwareEstimatedNetSavingsAfterMaterialization") &&
              mark_summary->details.at("valueAwareEstimatedNetSavingsAfterMaterialization") == "0" &&
              !mark_summary->details.contains("valueAwareMixedStateTempCarrierNames"),
          "reads through callback and tail-helper chains must keep stored state persistent");

  const auto find_step = [&](std::string_view marker) {
    const auto found = std::find_if(result.steps.begin(), result.steps.end(),
                                  [&](const ResolvedStep& step) {
      return step.comment.has_value() && step.comment->find(marker) != std::string::npos;
    });
    require(found != result.steps.end(), "missing fused protocol step: " + std::string(marker));
    return static_cast<std::size_t>(found - result.steps.begin());
  };
  const auto reject_opcode_mutation = [&](std::size_t index, int opcode, const char* message) {
    CompileResult changed = result;
    changed.steps[index].opcode = opcode;
    std::size_t executable = 0;
    for (MachineItem& item : changed.items) {
      if (item.kind == MachineItemKind::Label) continue;
      if (executable++ == index) { item.opcode = opcode; break; }
    }
    require(optimizer_static_proof_gate_rejection_reason_for_testing(options, changed).has_value(),
            message);
  };
  reject_opcode_mutation(find_step("set best_score from X parameter"), 0x01,
                         "final proof must reject an automatic lift after an open number entry");
  reject_opcode_mutation(find_step("restored X/Y/Z"), 0x54,
                         "final proof must reject a missing entry rotation");
  reject_opcode_mutation(find_step("callee-hole leaf entry __packed_score_accumulator"), 0x0f,
                         "final proof must reject a leaf observing the overwritten X1");
  std::optional<std::size_t> fallthrough_entry;
  for (std::size_t index = 1; index < result.steps.size(); ++index) {
    const auto& step = result.steps[index];
    const auto& previous = result.steps[index - 1];
    if (step.comment.has_value() &&
        step.comment->starts_with("callee-hole charge-entry store;") &&
        previous.opcode >= 0 && previous.opcode <= 9 &&
        previous.address + 1 == step.address)
      fallthrough_entry = index;
  }
  require(fallthrough_entry.has_value(),
          "the final artifact must actually enter a retained selector store without a jump");
  reject_opcode_mutation(*fallthrough_entry - 1,
                         (result.steps[*fallthrough_entry - 1].opcode + 1) % 10,
                         "fallthrough must reject digits inconsistent with the bound selector");
  for (bool raw : {false, true}) {
    CompileResult opaque = result;
    std::size_t executable = 0;
    for (MachineItem& item : opaque.items) {
      if (item.kind == MachineItemKind::Label) continue;
      if (executable++ != *fallthrough_entry) continue;
      if (raw) item.raw = true;
      else item.manual_interaction.emplace();
      break;
    }
    require(optimizer_static_proof_gate_rejection_reason_for_testing(options, opaque).has_value(),
            "raw or operator-anchored selector stores cannot prove fallthrough entry");
  }
  CompileResult stale = result;
  stale.steps[find_step("callee-hole selector-value=")].comment =
      "callee-hole selector-value=0 indirect-target=0";
  require(optimizer_static_proof_gate_rejection_reason_for_testing(options, stale).has_value(),
          "a stale selector annotation must not pass the delivered-address proof");
  CompileResult unmarked = result;
  unmarked.steps[find_step("callee-hole charge-entry tail transfer;")].comment =
      "return suffix gadget";
  require(optimizer_static_proof_gate_rejection_reason_for_testing(options, unmarked).has_value(),
          "losing one charge-entry marker must fail the complete-predecessor proof");

  CompileOptions normalized_options = options;
  normalized_options.sign_normalized_x_param = true;
  const auto normalized = compile_source(source, normalized_options);
  require(normalized.implemented && normalized.steps.size() <= 132 &&
              !optimizer_static_proof_gate_rejection_reason_for_testing(normalized_options,
                                                                       normalized).has_value(),
          "shared traversal must compose with proved sign-only parameter normalization");
  CompileOptions automatic_options;
  automatic_options.analysis = true;
  automatic_options.budget = 999999;
  const auto automatic = compile_source(source, automatic_options);
  require(automatic.implemented && automatic.steps.size() <= normalized.steps.size(),
          "automatic final-ABI refinement must not discard a smaller proved entry composition");
}

} // namespace mkpro::tests
