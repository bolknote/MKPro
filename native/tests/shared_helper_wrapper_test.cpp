#include "mkpro/core/shared_helper_wrapper.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mkpro::tests {

namespace {

int cell_count(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(),
                                        [](const MachineItem& item) {
                                          return item.kind != MachineItemKind::Label;
                                        }));
}

std::size_t item_at_address(const std::vector<MachineItem>& items, int wanted) {
  int address = 0;
  for (std::size_t item = 0; item < items.size(); ++item) {
    if (items.at(item).kind == MachineItemKind::Label)
      continue;
    if (address == wanted)
      return item;
    ++address;
  }
  throw std::runtime_error("fixture address is absent: " +
                           std::to_string(wanted));
}

int label_address(const std::vector<MachineItem>& items,
                  const std::string& label) {
  int address = 0;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label && item.name == label)
      return address;
    if (item.kind != MachineItemKind::Label)
      ++address;
  }
  return -1;
}

bool contains_reason(const std::vector<std::string>& reasons,
                     std::string_view needle) {
  return std::any_of(reasons.begin(), reasons.end(),
                     [&](const std::string& reason) {
                       return reason.find(needle) != std::string::npos;
                     });
}

std::vector<MachineItem> fixture(std::string helper = "value_kernel") {
  std::vector<MachineItem> items = {
      MachineItem::op(0x52, "В/О"),
      MachineItem::op(0x60, "П->X 0"),
      MachineItem::op(0x53, "ПП"),
      MachineItem::address(helper),
      MachineItem::op(0x38, "К ∨"),
      MachineItem::op(0x49, "X->П 9"),
      MachineItem::op(0x60, "П->X 0"),
      MachineItem::op(0x54, "К НОП"),
      MachineItem::op(0x53, "ПП"),
      MachineItem::address(helper),
      MachineItem::op(0x37, "К ∧"),
      MachineItem::op(0x61, "П->X 1"),
  };
  while (cell_count(items) < 41)
    items.push_back(MachineItem::op(0x54, "К НОП"));
  items.push_back(MachineItem::op(0x53, "ПП"));
  items.push_back(MachineItem::address(helper));
  items.push_back(MachineItem::op(0x38, "К ∨"));
  items.push_back(MachineItem::op(0x49, "X->П 9"));
  items.push_back(MachineItem::op(0x52, "В/О"));
  while (cell_count(items) < 60)
    items.push_back(MachineItem::op(0x54, "К НОП"));
  items.push_back(MachineItem::label(helper));
  for (int cell = 0; cell < 9; ++cell)
    items.push_back(MachineItem::op(0x31, "К |x|"));
  items.push_back(MachineItem::op(0x52, "В/О"));
  return items;
}

} // namespace

void shared_helper_wrapper_rewrites_only_proved_continuations() {
  const std::vector<MachineItem> baseline = fixture();
  const core::SharedHelperWrapperResult rewritten =
      core::optimize_shared_helper_wrapper(baseline);
  require(rewritten.applied == 1 && rewritten.removed_cells == 2 &&
              rewritten.proof.proved &&
              rewritten.proof.continuation_proved &&
              rewritten.proof.terminal_wrapper_proved &&
              rewritten.proof.input_control_flow_proved &&
              rewritten.proof.final_control_flow_proved &&
              cell_count(rewritten.items) == cell_count(baseline) - 2,
          "generic terminal wrapper should remove one duplicated continuation" +
              (rewritten.proof.reasons.empty()
                   ? std::string{}
                   : ": " + rewritten.proof.reasons.front()));
  require(label_address(rewritten.items, rewritten.proof.wrapper_label) == 39 &&
              label_address(rewritten.items, "value_kernel") == 58,
          "wrapper should reuse the existing terminal call sequence without moving the helper");
  const MachineItem& redirected_operand =
      rewritten.items.at(item_at_address(rewritten.items, 3));
  require(redirected_operand.kind == MachineItemKind::Address &&
              std::get<std::string>(redirected_operand.target) ==
                  rewritten.proof.wrapper_label,
          "the nonterminal ordinary call should target the proved wrapper");

  const core::SharedHelperWrapperResult alpha =
      core::optimize_shared_helper_wrapper(fixture("alpha_kernel"));
  require(alpha.applied == 1 && alpha.removed_cells == 2 &&
              alpha.proof.helper_label == "alpha_kernel",
          "alpha-renaming must not affect wrapper discovery");

  std::vector<MachineItem> different_store = fixture();
  different_store.at(item_at_address(different_store, 44)) =
      MachineItem::op(0x48, "X->П 8");
  const core::SharedHelperWrapperResult store_rejected =
      core::optimize_shared_helper_wrapper(different_store);
  require(store_rejected.applied == 0,
          "different continuation stores must fail closed");

  std::vector<MachineItem> no_terminal = fixture();
  no_terminal.at(item_at_address(no_terminal, 45)) =
      MachineItem::op(0x54, "К НОП");
  const core::SharedHelperWrapperResult terminal_rejected =
      core::optimize_shared_helper_wrapper(no_terminal);
  require(terminal_rejected.applied == 0 &&
              contains_reason(terminal_rejected.proof.reasons,
                              "not followed by a bare return"),
          "a nonterminal equal call must not become a wrapper");

  std::vector<MachineItem> x2_overwrite = fixture();
  x2_overwrite.at(item_at_address(x2_overwrite, 6)) =
      MachineItem::op(0x32, "К ЗН");
  const core::SharedHelperWrapperResult x2_converged =
      core::optimize_shared_helper_wrapper(x2_overwrite);
  require(x2_converged.applied == 1 && x2_converged.removed_cells == 2 &&
              x2_converged.proof.proved,
          "an X2-affecting unary command should prove convergence before the next call");

  std::vector<MachineItem> conditional_x2_overwrite = fixture();
  const std::size_t branch = item_at_address(conditional_x2_overwrite, 6);
  const std::size_t overwrite = item_at_address(conditional_x2_overwrite, 7);
  conditional_x2_overwrite.at(branch) = MachineItem::op(0x5e, "F x=0");
  conditional_x2_overwrite.at(overwrite) = MachineItem::op(0x32, "К ЗН");
  conditional_x2_overwrite.insert(
      conditional_x2_overwrite.begin() + static_cast<std::ptrdiff_t>(overwrite),
      {MachineItem::address("__x2_overwrite"),
       MachineItem::label("__x2_overwrite")});
  const core::SharedHelperWrapperResult conditional_x2_converged =
      core::optimize_shared_helper_wrapper(conditional_x2_overwrite);
  require(conditional_x2_converged.applied == 1 &&
              conditional_x2_converged.removed_cells == 2 &&
              conditional_x2_converged.proof.proved,
          "a direct conditional should use its path-specific X2 effects before a common "
          "overwriting command");

  std::vector<MachineItem> x2_restore = fixture();
  x2_restore.at(item_at_address(x2_restore, 6)) =
      MachineItem::op(0x0a, ".");
  const core::SharedHelperWrapperResult x2_rejected =
      core::optimize_shared_helper_wrapper(x2_restore);
  require(x2_rejected.applied == 0,
          "an observable X2 difference after the redirected call must fail closed");

  // The common continuation is followed by two nested calls before its first
  // X2 overwrite. The proof must inspect their bodies instead of treating a
  // call as either an unconditional barrier or an opaque preserving summary.
  std::vector<MachineItem> nested = {
      MachineItem::op(0x52, "return"), MachineItem::label("main"),
      MachineItem::op(0x60, "recall 0"),
      MachineItem::op(0x53, "call"), MachineItem::address(std::string("route")),
      MachineItem::op(0x4a, "store a"), MachineItem::op(0x60, "recall 0"),
      MachineItem::op(0x53, "call"), MachineItem::address(std::string("commit")),
      MachineItem::op(0x4b, "store b"), MachineItem::op(0x60, "recall 0"),
      MachineItem::op(0x53, "call"), MachineItem::address(std::string("kernel")),
      MachineItem::op(0x4c, "store c"), MachineItem::op(0x0a, "decimal X2"),
      MachineItem::op(0x4d, "store d"), MachineItem::op(0x50, "stop"),
      MachineItem::op(0x51, "jump"), MachineItem::address(std::string("main")),
      MachineItem::label("route"), MachineItem::op(0x60, "recall 0"),
      MachineItem::op(0x53, "call"), MachineItem::address(std::string("kernel")),
      MachineItem::op(0x38, "or"), MachineItem::op(0x49, "store 9"),
      MachineItem::label("nested_call"),
      MachineItem::op(0x53, "call"), MachineItem::address(std::string("sanitize")),
      MachineItem::op(0x42, "store 2"), MachineItem::op(0x52, "return"),
      MachineItem::label("commit"),
      MachineItem::op(0x53, "call"), MachineItem::address(std::string("kernel")),
      MachineItem::op(0x38, "or"), MachineItem::op(0x49, "store 9"),
      MachineItem::op(0x52, "return"),
      MachineItem::label("kernel"), MachineItem::op(0x31, "abs"),
      MachineItem::op(0x52, "return"), MachineItem::label("sanitize"),
      MachineItem::op(0x53, "call"), MachineItem::address(std::string("relay")),
      MachineItem::op(0x52, "return"), MachineItem::label("relay"),
      MachineItem::op(0x54, "nop"), MachineItem::op(0x03, "3"),
      MachineItem::op(0x52, "return"),
  };
  for (auto& item : nested)
    if (item.kind == MachineItemKind::Op && item.opcode == 0x50)
      item.stop_disposition = StopDisposition::Resumable;
  const auto nested_proof = core::verify_shared_helper_continuation(nested, "kernel");
  require(nested_proof.proved,
          "a literal inside a known nested callee must prove X2 convergence");
  const auto nested_rewrite = core::optimize_shared_helper_wrapper(nested);
  require(nested_rewrite.applied == 1 && nested_rewrite.removed_cells == 2 &&
              nested_rewrite.proof.final_control_flow_proved,
          "interprocedural convergence must enable the ordinary structural wrapper");
  const auto observe = [](const std::vector<MachineItem>& items, const std::string& input) {
    const auto resolved = resolve_machine_items(items, {});
    require(resolved.diagnostics.empty(), "nested continuation fixture must resolve");
    std::vector<int> codes;
    for (const auto& step : resolved.steps) codes.push_back(step.opcode);
    emulator::MK61 calc;
    require(calc.load_program(codes).diagnostics.empty(), "nested fixture must fit");
    calc.set_register("0", input);
    calc.set_register("Y", "73"); calc.set_register("Z", "29");
    calc.set_register("T", "17");
    calc.input_number(input, true);
    calc.press_sequence({"В/О", "С/П"});
    std::vector<std::array<std::string, 12>> result;
    for (int round = 0; round < 2; ++round) {
      require(calc.run_until_stable(2000, 6).stopped,
              "nested continuations must return through both callers and stop");
      result.push_back({calc.display_text(), calc.read_register("X"),
                        calc.read_register("Y"), calc.read_register("Z"),
                        calc.read_register("T"), calc.read_register("X1"),
                        calc.read_register("2"), calc.read_register("9"),
                        calc.read_register("a"), calc.read_register("b"),
                        calc.read_register("c"), calc.read_register("d")});
      if (round == 0) calc.press("С/П");
    }
    return result;
  };
  for (const std::string input : {"0", "12", "-7", "8.1234567"})
    require(observe(nested, input) == observe(nested_rewrite.items, input),
            "nested wrapper must preserve display, stack, last-X, X2 observations and state: " + input);

  auto unsafe_nested = nested;
  unsafe_nested.at(item_at_address(unsafe_nested, label_address(unsafe_nested, "relay"))) =
      MachineItem::op(0x0a, "decimal X2");
  require(!core::verify_shared_helper_continuation(unsafe_nested, "kernel").proved,
          "reading X2 inside a callee before overwriting it must reject sharing");
  auto recursive_nested = nested;
  const auto relay_item = item_at_address(recursive_nested,
                                          label_address(recursive_nested, "relay"));
  recursive_nested.at(relay_item) = MachineItem::op(0x53, "call");
  recursive_nested.at(relay_item + 1) = MachineItem::address(std::string("relay"));
  require(!core::verify_shared_helper_continuation(recursive_nested, "kernel").proved,
          "recursive calls before X2 convergence cannot be assumed to return");
  auto opaque_nested = nested;
  opaque_nested.at(item_at_address(opaque_nested,
                                   label_address(opaque_nested, "relay"))).raw = true;
  require(!core::verify_shared_helper_continuation(opaque_nested, "kernel").proved,
          "an opaque callee entry cannot supply a convergence proof");

  auto indirect_nested = nested;
  const auto indirect_item = item_at_address(indirect_nested,
                                             label_address(indirect_nested, "nested_call"));
  indirect_nested.at(indirect_item) = MachineItem::op(0xae, "indirect call e");
  indirect_nested.erase(indirect_nested.begin() + static_cast<std::ptrdiff_t>(indirect_item + 1));
  core::SharedHelperContinuationOptions indirect_options;
  indirect_options.proved_indirect_flow_targets[indirect_item] = {
      label_address(indirect_nested, "sanitize")};
  require(core::verify_shared_helper_continuation(indirect_nested, "kernel", indirect_options).proved,
          "a complete known indirect target must admit the same callee-body proof");
  require(!core::verify_shared_helper_continuation(indirect_nested, "kernel").proved,
          "an indirect call without a complete target set must remain opaque");
  indirect_nested.push_back(MachineItem::label("unsafe_target"));
  indirect_nested.push_back(MachineItem::op(0x0a, "decimal X2"));
  indirect_nested.push_back(MachineItem::op(0x52, "return"));
  indirect_options.proved_indirect_flow_targets[indirect_item].push_back(
      label_address(indirect_nested, "unsafe_target"));
  require(!core::verify_shared_helper_continuation(indirect_nested, "kernel", indirect_options).proved,
          "every possible indirect callee, not just the first target, must converge");
}

} // namespace mkpro::tests
