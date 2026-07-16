#include "mkpro/core/shared_helper_wrapper.hpp"

#include "test_support.hpp"

#include <algorithm>
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

  std::vector<MachineItem> x2_restore = fixture();
  x2_restore.at(item_at_address(x2_restore, 6)) =
      MachineItem::op(0x0a, ".");
  const core::SharedHelperWrapperResult x2_rejected =
      core::optimize_shared_helper_wrapper(x2_restore);
  require(x2_rejected.applied == 0,
          "an observable X2 difference after the redirected call must fail closed");
}

} // namespace mkpro::tests
