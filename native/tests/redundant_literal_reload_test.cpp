#include "mkpro/core/passes/redundant_literal_reload.hpp"
#include "mkpro/core/late_bound_decimal_selector.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "ir_pass_test_support.hpp"
#include "test_support.hpp"

#include <array>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

using namespace mkpro::tests::irbuild;

core::passes::PassResult run(const std::vector<IrOp>& ops) {
  return core::passes::redundant_literal_reload(
      ops, core::passes::PassContext{.options = noop_options()});
}

std::array<std::string, 9> run_machine(const std::vector<int>& program) {
  emulator::MK61 calc;
  calc.set_register("x", "9");
  calc.set_register("y", "8");
  calc.set_register("z", "7");
  calc.set_register("t", "6");
  calc.set_register("1", "111");
  calc.set_register("2", "222");
  calc.set_register("9", "333");
  calc.load_program(program);
  calc.press_sequence({"В/О", "С/П"});
  (void)calc.run_until_stable(500, 5);
  return {
      calc.read_register("x"), calc.read_register("y"), calc.read_register("z"),
      calc.read_register("t"), calc.read_register("x1"), calc.read_register("1"),
      calc.read_register("2"), calc.read_register("9"), calc.display_text(),
  };
}

} // namespace

void redundant_literal_reload_is_generic_and_proof_gated() {
  const std::vector<IrOp> positive = {
      plain(0x0d, "Cx"), plain(0x04, "4"), store("2"), plain(0x04, "4"),
      store("1"), recall("9"), recall("1"), recall("2"), plain(0x0a, "."), halt(),
  };
  const std::vector<IrOp> expected = {
      plain(0x0d, "Cx"), plain(0x04, "4"), store("2"), store("1"),
      recall("9"), recall("1"), recall("2"), plain(0x0a, "."), halt(),
  };
  const auto positive_result = run(positive);
  require_applied(positive_result.applied, 1,
                  "repeated literal with dead stack and X2 lift");
  require_ops_equal(positive_result.ops, expected,
                    "repeated literal with dead stack and X2 lift");

  const std::vector<IrOp> internal_marker = {
      plain(0x0d, "Cx"), plain(0x04, "4"), label("marker_a"), store("2"),
      label("marker_b"), plain(0x04, "4"), label("marker_c"), store("1"),
      recall("9"), recall("1"), recall("2"), halt(),
  };
  require_applied(run(internal_marker).applied, 1,
                  "unreferenced internal labels do not break a linear proof");

  const std::vector<IrOp> stack_observed = {
      plain(0x0d, "Cx"), plain(0x04, "4"), store("2"), plain(0x04, "4"),
      store("1"), plain(0x10, "+"), halt(),
  };
  require_applied(run(stack_observed).applied, 0,
                  "reload whose stack lift reaches arithmetic");

  const std::vector<IrOp> x2_observed = {
      plain(0x0d, "Cx"), plain(0x04, "4"), store("2"), plain(0x04, "4"),
      store("1"), plain(0x0a, "."), halt(),
  };
  require_applied(run(x2_observed).applied, 0,
                  "reload whose X2 sync reaches restore");

  const std::vector<IrOp> converges_in_helper = {
      plain(0x0d, "Cx"), plain(0x04, "4"), store("2"), plain(0x04, "4"),
      store("1"), call("helper"), halt(), label("helper"), recall("9"),
      recall("1"), recall("2"), ret(),
  };
  const std::vector<IrOp> converges_in_helper_expected = {
      plain(0x0d, "Cx"), plain(0x04, "4"), store("2"), store("1"),
      call("helper"), halt(), label("helper"), recall("9"), recall("1"),
      recall("2"), ret(),
  };
  const auto helper_result = run(converges_in_helper);
  require_applied(helper_result.applied, 1,
                  "repeated literal whose stack lift dies inside a called helper");
  require_ops_equal(helper_result.ops, converges_in_helper_expected,
                    "interprocedural repeated literal reload");

  const std::vector<IrOp> helper_observes_stack = {
      plain(0x0d, "Cx"), plain(0x04, "4"), store("2"), plain(0x04, "4"),
      store("1"), call("helper"), halt(), label("helper"), plain(0x10, "+"), ret(),
  };
  require_applied(run(helper_observes_stack).applied, 0,
                  "called helper that consumes the differing stack value");

  const std::vector<IrOp> different_literal = {
      plain(0x0d, "Cx"), plain(0x04, "4"), store("2"), plain(0x05, "5"),
      store("1"), recall("9"), recall("1"), recall("2"), halt(),
  };
  require_applied(run(different_literal).applied, 0,
                  "different literal value");

  const std::vector<IrOp> externally_entered = {
      plain(0x0d, "Cx"), plain(0x04, "4"), store("2"), label("entry"),
      plain(0x04, "4"), store("1"), recall("9"), recall("1"), recall("2"),
      jump("entry"),
  };
  require_applied(run(externally_entered).applied, 0,
                  "reload at an independently addressable entry");

  IrOp raw_reload = plain(0x04, "4");
  raw_reload.meta.raw = true;
  const std::vector<IrOp> raw = {
      plain(0x0d, "Cx"), plain(0x04, "4"), store("2"), raw_reload,
      store("1"), recall("9"), recall("1"), recall("2"), halt(),
  };
  require_applied(run(raw).applied, 0, "raw literal reload");

  const std::vector<IrOp> shifted_numeric_target = {
      numeric_jump(9), plain(0x04, "4"), store("2"), plain(0x04, "4"),
      store("1"), recall("9"), recall("1"), recall("2"), halt(),
  };
  require_applied(run(shifted_numeric_target).applied, 0,
                  "physical target shifted by reload removal");

  const std::vector<int> original = {
      0x04, 0x42, 0x04, 0x41, 0x69, 0x61, 0x62, 0x0a, 0x50,
  };
  const std::vector<int> optimized = {
      0x04, 0x42, 0x41, 0x69, 0x61, 0x62, 0x0a, 0x50,
  };
  require(run_machine(original) == run_machine(optimized),
          "emulator must confirm X/Y/Z/T/X1/register/display equivalence after convergence");

  const std::vector<int> original_call = {
      0x0d, 0x04, 0x42, 0x04, 0x41, 0x53, 0x08, 0x50,
      0x69, 0x61, 0x62, 0x52,
  };
  const std::vector<int> optimized_call = {
      0x0d, 0x04, 0x42, 0x41, 0x53, 0x07, 0x50,
      0x69, 0x61, 0x62, 0x52,
  };
  require(run_machine(original_call) == run_machine(optimized_call),
          "emulator must confirm interprocedural stack/X2 convergence");

  const auto late_digit = [](core::LateBoundDecimalSelectorPart part,
                             const std::string& target, int opcode) {
    MachineItem item = MachineItem::op(opcode, std::to_string(opcode));
    item.roles.push_back(core::make_late_bound_decimal_selector_role(part, target));
    return item;
  };
  MachineItem terminal_stop = MachineItem::op(0x50, "C/P");
  terminal_stop.stop_disposition = StopDisposition::Terminal;
  std::vector<MachineItem> compactable_selector = {
      MachineItem::op(0x41, "X->P 1"),
      late_digit(core::LateBoundDecimalSelectorPart::High, "early", 0),
      late_digit(core::LateBoundDecimalSelectorPart::Low, "early", 6),
      MachineItem::op(0x0d, "Cx"),
      terminal_stop,
      MachineItem::op(0x54, "K NOP"),
      MachineItem::label("early"),
      MachineItem::op(0x52, "B/O"),
  };
  const auto selector_plan =
      core::passes::post_layout_single_digit_late_selector_plan(
          compactable_selector);
  require(selector_plan.has_value() && selector_plan->target_address == 6 &&
              selector_plan->leading_zero_address == 1,
          "one-digit selector should be compactable after X2 synchronization");

  compactable_selector.at(3) = MachineItem::op(0x0a, ".");
  require(!core::passes::post_layout_single_digit_late_selector_plan(
               compactable_selector)
               .has_value(),
          "one-digit selector must retain its leading zero when dot observes X2");

  const std::vector<int> original_selector = {0x41, 0x00, 0x06, 0x0d, 0x50};
  const std::vector<int> compact_selector = {0x41, 0x06, 0x0d, 0x50};
  require(run_machine(original_selector) == run_machine(compact_selector),
          "emulator must confirm leading-zero selector equivalence after X2 sync");
}

} // namespace mkpro::tests
