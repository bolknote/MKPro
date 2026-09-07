#include "mkpro/core/helper_invariant_recall_hoist.hpp"

#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/stack_value_equivalence.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mkpro::tests {

namespace {

constexpr std::string_view kRoot = "q7";

void append_forget_y(std::vector<MachineItem>& items) {
  // K AND/K OR retain Y.  Three stack lifts push the one deliberately
  // reordered Y value out of X/Y/Z/T without evaluating it; the first lift
  // also overwrites X2 from the already-equal X value.
  items.push_back(MachineItem::op(0x0e, "В↑"));
  items.push_back(MachineItem::op(0x0e, "В↑"));
  items.push_back(MachineItem::op(0x0e, "В↑"));
}

std::vector<MachineItem> alpha_fixture() {
  std::vector<MachineItem> items;

  // Two before-call forms.
  items.push_back(MachineItem::op(0x68, "П→X 8"));
  items.push_back(MachineItem::op(0x53, "ПП"));
  items.push_back(MachineItem::address(std::string(kRoot)));
  items.push_back(MachineItem::op(0x38, "К ∨"));
  append_forget_y(items);
  items.push_back(MachineItem::op(0x40, "X→П 0"));

  items.push_back(MachineItem::op(0x68, "П→X 8"));
  items.push_back(MachineItem::op(0x53, "ПП"));
  items.push_back(MachineItem::address(std::string(kRoot)));
  items.push_back(MachineItem::op(0x37, "К ∧"));
  append_forget_y(items);
  items.push_back(MachineItem::op(0x42, "X→П 2"));

  // One after-return form.  The symbolic proof, rather than a comment or a
  // source identifier, proves that the commutative join and the three lifts
  // erase the changed stack order.  The unrelated R7 immediately before PP
  // must not hide the globally common R8 candidate on the other side.
  items.push_back(MachineItem::op(0x67, "П→X 7"));
  items.push_back(MachineItem::op(0x53, "ПП"));
  items.push_back(MachineItem::address(std::string(kRoot)));
  items.push_back(MachineItem::op(0x68, "П→X 8"));
  items.push_back(MachineItem::op(0x38, "К ∨"));
  append_forget_y(items);
  items.push_back(MachineItem::op(0x43, "X→П 3"));
  items.push_back(MachineItem::op(0x50, "С/П"));

  // The stop is also the official fallthrough fence for the root.  The body
  // pushes a value computed solely from a different direct register.  A
  // zero-width preceding procedure-end marker is metadata, not another entry.
  MachineItem end_metadata = MachineItem::label("m2");
  end_metadata.procedure_boundary = "end";
  items.push_back(end_metadata);
  items.push_back(MachineItem::label(std::string(kRoot)));
  items.push_back(MachineItem::op(0x61, "П→X 1"));
  items.push_back(MachineItem::op(0x22, "F x²"));
  items.push_back(MachineItem::op(0x52, "В/О"));
  return items;
}

std::vector<MachineItem> after_return_fixture() {
  std::vector<MachineItem> items;
  int input = 1;
  for (const int store : {0x40, 0x42, 0x43}) {
    items.push_back(MachineItem::op(input++, "distinct caller input"));
    items.push_back(MachineItem::op(0x53, "ПП"));
    items.push_back(MachineItem::address(std::string(kRoot)));
    items.push_back(MachineItem::op(0x68, "П->X 8"));
    items.push_back(MachineItem::op(0x38, "К OR"));
    append_forget_y(items);
    items.push_back(MachineItem::op(store, "store result"));
  }
  items.push_back(MachineItem::op(0x50, "С/П"));
  items.push_back(MachineItem::label(std::string(kRoot)));
  items.push_back(MachineItem::op(0x61, "П->X 1"));
  items.push_back(MachineItem::op(0x22, "F x^2"));
  items.push_back(MachineItem::op(0x52, "В/О"));
  return items;
}

int cell_count(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(), [](const MachineItem& item) {
    return item.kind != MachineItemKind::Label;
  }));
}

int opcode_count(const std::vector<MachineItem>& items, int opcode) {
  return static_cast<int>(std::count_if(items.begin(), items.end(), [&](const MachineItem& item) {
    return item.kind == MachineItemKind::Op && item.opcode == opcode;
  }));
}

int item_address(const std::vector<MachineItem>& items, std::size_t wanted_item) {
  int address = 0;
  for (std::size_t index = 0; index < items.size(); ++index) {
    if (index == wanted_item)
      return address;
    if (items.at(index).kind != MachineItemKind::Label)
      ++address;
  }
  throw std::runtime_error("fixture item is absent");
}

std::size_t label_index(const std::vector<MachineItem>& items, std::string_view name) {
  for (std::size_t index = 0; index < items.size(); ++index)
    if (items.at(index).kind == MachineItemKind::Label && items.at(index).name == name)
      return index;
  throw std::runtime_error("fixture label is absent");
}

bool contains_reason(const core::HelperInvariantRecallHoistProof& proof, std::string_view needle) {
  return std::any_of(proof.reasons.begin(), proof.reasons.end(), [&](const std::string& reason) {
    return reason.find(needle) != std::string::npos;
  });
}

std::string compact(std::string value) {
  value.erase(std::remove_if(value.begin(), value.end(),
                             [](unsigned char ch) { return std::isspace(ch) != 0; }),
              value.end());
  return value;
}

struct EmulatorOutcome {
  bool stopped = false;
  std::array<std::string, 7> values;

  bool operator==(const EmulatorOutcome&) const = default;
};

EmulatorOutcome run(const std::vector<MachineItem>& items) {
  const ResolvedProgram resolved = resolve_machine_items(items, {});
  require(resolved.diagnostics.empty(), "recall-hoist fixture should resolve");
  std::vector<int> codes;
  for (const ResolvedStep& step : resolved.steps)
    codes.push_back(step.opcode);

  emulator::MK61 calc({.extended = true, .angle_mode = "deg"});
  const emulator::ProgramLoadResult loaded = calc.load_program(codes);
  require(loaded.diagnostics.empty(), "recall-hoist fixture should load");
  calc.set_register("1", "3");
  calc.set_register("8", "4");
  if (opcode_count(items, 0xab) > 0)
    calc.set_register("b", std::to_string(item_address(items, label_index(items, kRoot))));
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult stable = calc.run_until_stable(1200, 6);
  return EmulatorOutcome{
      .stopped = stable.stopped,
      .values = {compact(calc.read_register("x")), compact(calc.read_register("y")),
                 compact(calc.read_register("z")), compact(calc.read_register("t")),
                 compact(calc.read_register("0")), compact(calc.read_register("2")),
                 compact(calc.read_register("3"))},
  };
}

std::vector<MachineItem> staged_operand_fixture(
    int argument_count, const std::vector<int>& permutations = {0x14}) {
  std::vector<MachineItem> items;
  for (int site = 0; site < 3; ++site) {
    if (site == 0)
      items.push_back(MachineItem::op(0x68, "common operand"));
    // Deliberately vary argument registers. In the three-argument fixture R3
    // is also present at every call, but moving it fails the operand proof.
    for (int argument = 1; argument <= argument_count; ++argument)
      items.push_back(MachineItem::op(0x60 + site + argument, "argument"));
    items.push_back(MachineItem::op(0x53, "call"));
    items.push_back(MachineItem::address(std::string(kRoot)));
    if (site != 0)
      items.push_back(MachineItem::op(0x68, "common operand"));
    if (site == 2)
      for (const int opcode : permutations)
        items.push_back(MachineItem::op(opcode, "stack permutation"));
    items.push_back(MachineItem::op(site == 1 ? 0x37 : 0x38, "commutative join"));
    append_forget_y(items);
    items.push_back(MachineItem::op(0x49 + site, "save result"));
  }
  MachineItem prompt = MachineItem::op(0x50, "prompt");
  prompt.stop_disposition = StopDisposition::Resumable;
  items.push_back(prompt);
  items.push_back(MachineItem::op(0x0c, "VP"));
  items.push_back(MachineItem::op(2, "2"));
  MachineItem finish = MachineItem::op(0x50, "finish");
  finish.stop_disposition = StopDisposition::Terminal;
  items.push_back(finish);
  items.push_back(MachineItem::label(std::string(kRoot)));
  if (argument_count == 1)
    items.push_back(MachineItem::op(0x22, "square"));
  else
    for (int argument = 1; argument < argument_count; ++argument)
      items.push_back(MachineItem::op(0x10, "add"));
  items.push_back(MachineItem::op(0x52, "return"));
  return items;
}

std::vector<std::string> observe_staged_operand(const std::vector<MachineItem>& items,
                                               const std::string& common) {
  const ResolvedProgram resolved = resolve_machine_items(items, {});
  require(resolved.diagnostics.empty() && resolved.steps.size() <= 105U,
          "staged helper fixture must resolve within stock MK-61 memory");
  std::vector<int> codes;
  for (const ResolvedStep& step : resolved.steps)
    codes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(codes).diagnostics.empty(), "staged helper fixture must load");
  for (int reg = 1; reg <= 5; ++reg)
    calc.set_register(std::to_string(reg), std::to_string(reg + 1));
  calc.set_register("8", common);
  if (opcode_count(items, 0xab) > 0)
    calc.set_register("b", std::to_string(item_address(items, label_index(items, kRoot))));
  calc.set_register("X", "13");
  calc.set_register("Y", "29");
  calc.set_register("Z", "31");
  calc.set_register("T", "47");
  calc.press_sequence({"В/О", "С/П"});
  std::vector<std::string> observations;
  for (int phase = 0; phase < 2; ++phase) {
    require(calc.run_until_stable(2000, 6).stopped,
            "staged recall movement must preserve helper returns and both stops");
    observations.push_back(calc.display_text());
    for (const char* reg : {"X", "Y", "Z", "T", "X1", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
                            "a", "b", "c", "d", "e"})
      observations.push_back(calc.read_register(reg));
    if (phase == 0)
      calc.press("С/П");
  }
  return observations;
}

std::vector<MachineItem> entry_operand_fixture(bool signed_literals) {
  auto items = staged_operand_fixture(1);
  if (!signed_literals)
    return items;
  for (std::size_t item = 0; item + 3U < items.size(); ++item) {
    if (items.at(item).kind != MachineItemKind::Op || items.at(item).opcode != 0x0e ||
        items.at(item + 1U).kind != MachineItemKind::Op || items.at(item + 1U).opcode != 0x0e ||
        items.at(item + 2U).kind != MachineItemKind::Op || items.at(item + 2U).opcode != 0x0e)
      continue;
    const MachineItem save = items.at(item + 3U);
    items.erase(items.begin() + static_cast<std::ptrdiff_t>(item),
                items.begin() + static_cast<std::ptrdiff_t>(item + 4U));
    items.insert(items.begin() + static_cast<std::ptrdiff_t>(item),
                 {save, MachineItem::op(1, "literal"), MachineItem::op(0x0b, "literal sign"),
                  MachineItem::op(0x61, "flush"), MachineItem::op(0x62, "flush"),
                  MachineItem::op(0x63, "flush")});
    item += 5U;
  }
  return items;
}

std::vector<MachineItem> return_order_fixture() {
  std::vector<MachineItem> items;
  for (int site = 0; site < 3; ++site) {
    if (site == 0)
      items.push_back(MachineItem::op(0x68, "common operand"));
    items.push_back(MachineItem::op(0x61 + site, "argument"));
    items.push_back(MachineItem::op(0x53, "call"));
    items.push_back(MachineItem::address(std::string(kRoot)));
    if (site != 0) {
      items.push_back(MachineItem::op(0x68, "common operand"));
      items.push_back(MachineItem::op(0x14, "retain common operand in Y"));
    }
    items.push_back(MachineItem::op(site == 1 ? 0x37 : 0x38, "join"));
    // No stack flushing: both retained Y and the return's X2 remain observable.
    items.push_back(MachineItem::op(0x49 + site, "save result"));
  }
  MachineItem prompt = MachineItem::op(0x50, "prompt");
  prompt.stop_disposition = StopDisposition::Resumable;
  items.push_back(prompt);
  items.push_back(MachineItem::op(0x0c, "VP"));
  items.push_back(MachineItem::op(2, "2"));
  MachineItem finish = MachineItem::op(0x50, "finish");
  finish.stop_disposition = StopDisposition::Terminal;
  items.push_back(finish);
  items.push_back(MachineItem::label(std::string(kRoot)));
  items.push_back(MachineItem::op(0x22, "square"));
  items.push_back(MachineItem::op(0x52, "return"));
  return items;
}

std::vector<MachineItem> helper_with_inserted_op(int opcode) {
  std::vector<MachineItem> items = alpha_fixture();
  const std::size_t root = label_index(items, kRoot);
  items.insert(items.begin() + static_cast<std::ptrdiff_t>(root + 1U),
               MachineItem::op(opcode, "alpha"));
  return items;
}

std::vector<MachineItem> noncommutative_after_return_fixture() {
  std::vector<MachineItem> items = alpha_fixture();
  for (std::size_t index = 0; index + 3U < items.size(); ++index) {
    if (items.at(index).kind == MachineItemKind::Op && items.at(index).opcode == 0x53 &&
        items.at(index + 1U).kind == MachineItemKind::Address &&
        items.at(index + 2U).kind == MachineItemKind::Op && items.at(index + 2U).opcode == 0x68) {
      items.at(index + 3U) = MachineItem::op(0x11, "−");
      return items;
    }
  }
  throw std::runtime_error("after-return fixture site is absent");
}

std::vector<MachineItem> forward_jump_continuation_fixture() {
  std::vector<MachineItem> items = alpha_fixture();
  for (std::size_t index = 0; index + 4U < items.size(); ++index) {
    if (items.at(index).kind == MachineItemKind::Op && items.at(index).opcode == 0x53 &&
        items.at(index + 1U).kind == MachineItemKind::Address &&
        items.at(index + 2U).kind == MachineItemKind::Op &&
        items.at(index + 2U).opcode == 0x68 &&
        items.at(index + 3U).kind == MachineItemKind::Op &&
        items.at(index + 3U).opcode == 0x38) {
      items.insert(items.begin() + static_cast<std::ptrdiff_t>(index + 4U),
                   {MachineItem::op(0x51, "БП"),
                    MachineItem::address("after_forward_join"),
                    MachineItem::label("after_forward_join")});
      return items;
    }
  }
  throw std::runtime_error("forward-jump continuation fixture site is absent");
}

std::vector<MachineItem> x2_live_fixture() {
  std::vector<MachineItem> items = alpha_fixture();
  // Keep one site on the root-placement ABI so this remains a negative X2
  // fixture rather than becoming exactly equivalent through the new common
  // commutative tail.
  for (std::size_t index = 0; index + 3U < items.size(); ++index) {
    if (items.at(index).kind == MachineItemKind::Op && items.at(index).opcode == 0x68 &&
        items.at(index + 1U).kind == MachineItemKind::Op &&
        items.at(index + 1U).opcode == 0x53 &&
        items.at(index + 2U).kind == MachineItemKind::Address &&
        items.at(index + 3U).kind == MachineItemKind::Op &&
        (items.at(index + 3U).opcode == 0x37 ||
         items.at(index + 3U).opcode == 0x38)) {
      items.at(index + 3U) = MachineItem::op(0x11, "−");
      break;
    }
  }
  for (std::size_t index = 0; index + 6U < items.size(); ++index) {
    if (items.at(index).kind == MachineItemKind::Op && items.at(index).opcode == 0x53 &&
        items.at(index + 1U).kind == MachineItemKind::Address &&
        items.at(index + 2U).kind == MachineItemKind::Op && items.at(index + 2U).opcode == 0x68 &&
        items.at(index + 3U).kind == MachineItemKind::Op && items.at(index + 3U).opcode == 0x38) {
      items.at(index + 4U) = MachineItem::op(0x0f, "F Вx");
      items.at(index + 5U) = MachineItem::op(0x0d, "Cx");
      items.at(index + 6U) = MachineItem::op(0x0a, ".");
      return items;
    }
  }
  throw std::runtime_error("X2-live fixture site is absent");
}

struct IndirectCallFixture {
  std::vector<MachineItem> items;
  core::HelperInvariantRecallHoistOptions options;
  std::vector<std::size_t> call_items;
};

IndirectCallFixture indirect_call_fixture() {
  IndirectCallFixture fixture{.items = alpha_fixture()};
  for (std::size_t index = 0; index + 1U < fixture.items.size();) {
    if (fixture.items.at(index).kind == MachineItemKind::Op &&
        fixture.items.at(index).opcode == 0x53 &&
        fixture.items.at(index + 1U).kind == MachineItemKind::Address &&
        std::holds_alternative<std::string>(fixture.items.at(index + 1U).target) &&
        std::get<std::string>(fixture.items.at(index + 1U).target) == kRoot) {
      fixture.items.at(index) = MachineItem::op(0xab, "К ПП b");
      fixture.items.erase(fixture.items.begin() + static_cast<std::ptrdiff_t>(index + 1U));
      fixture.call_items.push_back(index);
    }
    ++index;
  }
  const int target = item_address(fixture.items, label_index(fixture.items, kRoot));
  for (const std::size_t call : fixture.call_items)
    fixture.options.proved_indirect_flow_targets.emplace(call, std::vector<int>{target});
  return fixture;
}

} // namespace

void helper_invariant_recall_hoist_rewrites_only_proved_calls() {
  {
    core::StackValueEqualityState equality;
    equality.stack_equal = {true, true, false, false};
    equality.x1_equal = false;
    equality.x2_equal = true;
    const int before = core::stack_value_equality_key(equality);
    require(core::transfer_decimal_sign_equality(equality, true) ==
                core::StackValueEqualityTransfer::Continue &&
                core::stack_value_equality_key(equality) == before,
            "a proved literal sign edit must not invent equality for parked stack or last-X");
    require(core::transfer_decimal_sign_equality(equality, false) ==
                core::StackValueEqualityTransfer::Rejected,
            "a sign edit outside proved mantissa entry must remain opaque");
    equality.x2_equal = false;
    require(core::transfer_decimal_sign_equality(equality, true) ==
                core::StackValueEqualityTransfer::Rejected,
            "equal visible literal X does not authorize an unequal hidden entry context");
    equality.x2_equal = true;
    equality.stack_equal[0] = false;
    require(core::transfer_decimal_sign_equality(equality, true) ==
                core::StackValueEqualityTransfer::Rejected,
            "a sign edit must not consume different visible values");
  }

  for (const bool signed_literals : {false, true}) {
    const auto original = entry_operand_fixture(signed_literals);
    core::HelperInvariantRecallHoistOptions options;
    options.allow_before_call_commutative_tail = false;
    options.allow_x_preserving_root = true;
    const auto accepted =
        core::rewrite_helper_invariant_recall_hoist(original, std::string(kRoot), options);
    require(accepted.applied == 1 && accepted.proof.final_artifact_proved &&
                accepted.proof.insertion == core::HelperInvariantRecallInsertion::HelperRoot &&
                accepted.proof.preserve_entry_x && !accepted.proof.swap_return_operands &&
                cell_count(accepted.items) == cell_count(original) - 2,
            "root recall/swap must preserve the existing X argument and save two cells");
    const auto root = label_index(accepted.items, kRoot);
    require(accepted.items.at(root + 1U).opcode == 0x68 &&
                accepted.items.at(root + 2U).opcode == 0x14,
            "the delivered root must contain the proved recall/swap, not a swapped return");
    for (const auto& common : {"0", "4", "7.7777777", "8.1234567", "-0.25"})
      require(observe_staged_operand(original, common) ==
                  observe_staged_operand(accepted.items, common),
              "entry ABI must preserve all stock registers, stack, X1 and post-stop VP");

    auto live_x2 = original;
    const auto join = std::find_if(live_x2.begin(), live_x2.end(), [](const MachineItem& item) {
      return item.kind == MachineItemKind::Op && (item.opcode == 0x37 || item.opcode == 0x38);
    });
    require(join != live_x2.end(), "entry fixture must have an observed commutative join");
    live_x2.insert(join + 1, {MachineItem::op(0x0c, "observe hidden entry context"),
                             MachineItem::op(2, "exponent")});
    require(core::rewrite_helper_invariant_recall_hoist(
                live_x2, std::string(kRoot), options).applied == 0,
            "entry permutation must reject VP before an independent hidden-context sync");

    for (const bool manual : {false, true}) {
      auto anchored = original;
      require(!accepted.proof.erased_permutation_items.empty(),
              "entry fixture must exercise removal of a caller swap");
      auto& swap = anchored.at(*accepted.proof.erased_permutation_items.begin());
      if (manual)
        swap.manual_interaction.emplace();
      else
        swap.raw = true;
      require(core::rewrite_helper_invariant_recall_hoist(
                  anchored, std::string(kRoot), options).applied == 0,
              "raw/manual caller swaps cannot be absorbed by the input ABI");
    }

    auto indirect = original;
    for (std::size_t item = indirect.size(); item-- > 0;) {
      if (indirect.at(item).kind == MachineItemKind::Address) {
        indirect.at(item - 1U) = MachineItem::op(0xab, "indirect call");
        indirect.erase(indirect.begin() + static_cast<std::ptrdiff_t>(item));
      }
    }
    const int target = item_address(indirect, label_index(indirect, kRoot));
    auto indirect_options = options;
    for (std::size_t item = 0; item < indirect.size(); ++item)
      if (indirect.at(item).kind == MachineItemKind::Op && indirect.at(item).opcode == 0xab)
        indirect_options.proved_indirect_flow_targets.emplace(item, std::vector<int>{target});
    const auto moved = core::rewrite_helper_invariant_recall_hoist(
        indirect, std::string(kRoot), indirect_options);
    require(moved.applied == 1 && moved.proof.final_artifact_proved &&
                observe_staged_operand(indirect, "4") == observe_staged_operand(moved.items, "4"),
            "entry ABI must preserve and retarget the complete indirect call family");
    const int moved_target = item_address(moved.items, label_index(moved.items, kRoot));
    for (const auto& [site, targets] : indirect_options.proved_indirect_flow_targets) {
      (void)targets;
      const auto mapped = moved.proof.old_to_new_item_indices.at(site);
      require(mapped.has_value() && moved.proof.final_indirect_flow_targets.at(*mapped) ==
                                       std::vector<int>{moved_target},
              "the final indirect proof must follow surviving command identities");
    }
    indirect_options.fixed_indirect_flow_targets = indirect_options.proved_indirect_flow_targets;
    require(core::rewrite_helper_invariant_recall_hoist(
                indirect, std::string(kRoot), indirect_options).applied == 0,
            "a fixed helper target cannot be moved to make the input ABI cheaper");
  }

  for (const auto& [metadata, expected] :
       std::array<std::pair<std::string, std::vector<std::string>>, 2>{
           std::pair{std::string("callee-hole indirect call; proof=p; "
                                 "leaf-labels=leaf_a,leaf_b"),
                     std::vector<std::string>{"leaf_a", "leaf_b"}},
           std::pair{std::string("callee-hole indirect call; proof=p; "
                                 "leaf-targets=30:leaf_a,40:leaf_b"),
                     std::vector<std::string>{"leaf_a", "leaf_b"}},
       }) {
    MachineItem dispatch = MachineItem::op(0xae, "К ПП e");
    dispatch.comment = metadata;
    const std::vector<IrOp> raised = raise_machine_to_ir({dispatch});
    require(raised.size() == 1U &&
                core::passes::computed_dispatch_target_labels(raised.front()) == expected,
            "callee-hole CFG metadata should retain leaf identities before and after binding");
  }

  {
    const auto original = return_order_fixture();
    const auto plain =
        core::rewrite_helper_invariant_recall_hoist(original, std::string(kRoot));
    require(plain.applied == 1 && cell_count(plain.items) == cell_count(original) - 2,
            "the existing plain-tail candidate stays available when the stop resynchronizes X2");
    core::HelperInvariantRecallHoistOptions options;
    options.allow_swapped_return = true;
    const auto accepted =
        core::rewrite_helper_invariant_recall_hoist(original, std::string(kRoot), options);
    require(accepted.applied == 1 && accepted.proof.final_artifact_proved &&
                accepted.proof.swap_return_operands &&
                accepted.proof.erased_permutation_items.size() == 2U &&
                cell_count(accepted.items) == cell_count(original) - 3 &&
                opcode_count(accepted.items, 0x68) == 1 &&
                opcode_count(accepted.items, 0x14) == 1,
            "the result/common-operand ABI must save three cells and prove live Y and X2 convergence");
    for (const auto& common : {"0", "4", "7.7777777"})
      require(observe_staged_operand(original, common) ==
                  observe_staged_operand(accepted.items, common),
              "return ABI must preserve full stock-MK61 stack, X1, VP/X2 and matched returns");
    for (const auto& call : accepted.proof.calls) {
      const auto mapped = accepted.proof.old_to_new_item_indices.at(call.call_item_index);
      require(mapped.has_value() && accepted.items.at(*mapped).opcode == 0x53,
              "the emitted identity map must account for both inserted and erased swaps");
    }

    const auto with_live_vp = [](std::vector<MachineItem> items) {
      const auto store = std::find_if(items.begin(), items.end(), [](const MachineItem& item) {
        return item.kind == MachineItemKind::Op && item.opcode == 0x4b;
      });
      require(store != items.end(), "return ABI fixture must retain its final result store");
      items.insert(store, {MachineItem::op(0x0c, "observe return X2"),
                           MachineItem::op(2, "2")});
      return items;
    };
    const auto live_x2 = with_live_vp(original);
    const auto unsafe_abi = with_live_vp(accepted.items);
    const auto source_observation = observe_staged_operand(live_x2, "4");
    const auto unsafe_observation = observe_staged_operand(unsafe_abi, "4");
    require(compact(source_observation.front()) == "100," &&
                compact(unsafe_observation.front()) == "1600,",
            "stock MK61 fact: equal returned X does not imply equal delayed VP restoration");
    const auto rejected_x2 =
        core::rewrite_helper_invariant_recall_hoist(live_x2, std::string(kRoot), options);
    require(rejected_x2.applied == 0 && contains_reason(rejected_x2.proof, "X2"),
            "return-operand permutation must fail closed before an unsynchronized VP");

    auto wrong_y = original;
    wrong_y.erase(wrong_y.begin() + static_cast<std::ptrdiff_t>(
                      *accepted.proof.erased_permutation_items.rbegin()));
    const auto retained_y =
        core::rewrite_helper_invariant_recall_hoist(wrong_y, std::string(kRoot), options);
    require(retained_y.applied == 0 || !retained_y.proof.swap_return_operands,
            "a commutative result must not hide a different live retained operand");

    for (const bool manual : {false, true}) {
      auto anchored = original;
      auto& swap = anchored.at(*accepted.proof.erased_permutation_items.begin());
      if (manual)
        swap.manual_interaction.emplace();
      else
        swap.raw = true;
      require(core::rewrite_helper_invariant_recall_hoist(
                  anchored, std::string(kRoot), options).applied == 0,
              "raw/manual caller permutations cannot be folded into a return ABI");
    }

    auto external = original;
    const auto target_item = *accepted.proof.erased_permutation_items.begin();
    const auto root = label_index(external, kRoot);
    external.insert(external.begin() + static_cast<std::ptrdiff_t>(root),
                    {MachineItem::op(0x51, "external jump"),
                     MachineItem::address(item_address(original, target_item))});
    auto external_options = options;
    external_options.fixed_direct_address_targets.emplace(
        root + 1U, item_address(original, target_item));
    external_options.retargetable_direct_address_items.insert(root + 1U);
    const auto external_result = core::rewrite_helper_invariant_recall_hoist(
        external, std::string(kRoot), external_options);
    require(external_result.applied == 0 || !external_result.proof.swap_return_operands,
            "an independently addressable caller swap cannot disappear");
  }

  {
    auto indirect = return_order_fixture();
    for (std::size_t item = indirect.size(); item-- > 0;) {
      if (indirect.at(item).kind == MachineItemKind::Address) {
        indirect.at(item - 1U) = MachineItem::op(0xab, "indirect call");
        indirect.erase(indirect.begin() + static_cast<std::ptrdiff_t>(item));
      }
    }
    core::HelperInvariantRecallHoistOptions options;
    options.allow_swapped_return = true;
    const int target = item_address(indirect, label_index(indirect, kRoot));
    for (std::size_t item = 0; item < indirect.size(); ++item)
      if (indirect.at(item).kind == MachineItemKind::Op && indirect.at(item).opcode == 0xab)
        options.proved_indirect_flow_targets.emplace(item, std::vector<int>{target});
    const auto accepted = core::rewrite_helper_invariant_recall_hoist(
        indirect, std::string(kRoot), options);
    require(accepted.applied == 1 && accepted.proof.final_artifact_proved &&
                accepted.proof.swap_return_operands &&
                cell_count(accepted.items) == cell_count(indirect) - 3 &&
                observe_staged_operand(indirect, "4") ==
                    observe_staged_operand(accepted.items, "4"),
            "return ABI must retarget complete indirect call families and preserve emulator behavior");
    const int moved_target = item_address(accepted.items, label_index(accepted.items, kRoot));
    for (const auto& [old_item, targets] : options.proved_indirect_flow_targets) {
      (void)targets;
      const auto mapped = accepted.proof.old_to_new_item_indices.at(old_item);
      require(mapped.has_value() &&
                  accepted.proof.final_indirect_flow_targets.at(*mapped) ==
                      std::vector<int>{moved_target},
              "every final indirect target must follow the surviving command identity");
    }
    options.fixed_indirect_flow_targets = options.proved_indirect_flow_targets;
    require(core::rewrite_helper_invariant_recall_hoist(
                indirect, std::string(kRoot), options).applied == 0,
            "a fixed helper address cannot shift merely because its return ABI is cheaper");
  }

  for (const int arguments : {1, 2, 3}) {
    const auto staged = staged_operand_fixture(arguments);
    const auto accepted =
        core::rewrite_helper_invariant_recall_hoist(staged, std::string(kRoot));
    require(accepted.applied == 1 && accepted.proof.final_artifact_proved &&
                accepted.proof.insertion == core::HelperInvariantRecallInsertion::BeforeReturn &&
                accepted.proof.recall_opcode == 0x68 && accepted.proof.calls.size() == 3U,
            "all staged candidates must be proved, not just the first common argument register");
    require(cell_count(accepted.items) == cell_count(staged) - 2 &&
                opcode_count(accepted.items, 0x68) == 1 &&
                opcode_count(accepted.items, 0x14) == 1 &&
                accepted.proof.calls.front().argument_preparation_items.size() ==
                    static_cast<std::size_t>(arguments) &&
                accepted.proof.calls.back().join_permutation_items.size() == 1U,
            "move only the common recall, retaining every argument and caller-side permutation");
    for (const auto& common : {"0", "4", "7.7777777"})
      require(observe_staged_operand(staged, common) ==
                  observe_staged_operand(accepted.items, common),
              "staged hoisting must preserve results, full stack, X1, VP/X2 and matched returns");
    const auto repeated = core::rewrite_helper_invariant_recall_hoist(staged, std::string(kRoot));
    require(repeated.proof.recall_opcode == accepted.proof.recall_opcode &&
                repeated.proof.output_cells == accepted.proof.output_cells &&
                repeated.proof.insertion == accepted.proof.insertion,
            "candidate selection must be deterministic");
  }

  {
    const auto rotations = staged_operand_fixture(2, {0x25, 0x25, 0x25, 0x25});
    const auto accepted =
        core::rewrite_helper_invariant_recall_hoist(rotations, std::string(kRoot));
    require(accepted.applied == 1 && opcode_count(accepted.items, 0x25) == 4 &&
                observe_staged_operand(rotations, "4") ==
                    observe_staged_operand(accepted.items, "4"),
            "bounded pure stack permutations require their real X1/X2 transfer, not a swap special case");
    const auto too_many = staged_operand_fixture(2, {0x25, 0x25, 0x25, 0x25, 0x25});
    require(core::rewrite_helper_invariant_recall_hoist(too_many, std::string(kRoot)).applied == 0,
            "permutation discovery must remain bounded");
  }

  {
    const auto staged = staged_operand_fixture(1);
    const auto accepted =
        core::verify_helper_invariant_recall_hoist(staged, std::string(kRoot));
    require(accepted.proved, "negative staged fixtures require a proved starting point");
    std::vector<std::size_t> protected_items = {
        accepted.calls.front().recall_item_index,
        accepted.calls.front().argument_preparation_items.front(),
        accepted.calls.front().call_item_index,
        accepted.calls.front().operand_item_index,
        accepted.calls.back().join_permutation_items.front(),
        accepted.helper_return_item_index};
    for (const std::size_t protected_item : protected_items) {
      for (const bool manual : {false, true}) {
        auto anchored = staged;
        if (manual)
          anchored.at(protected_item).manual_interaction.emplace();
        else
          anchored.at(protected_item).raw = true;
        require(core::rewrite_helper_invariant_recall_hoist(anchored, std::string(kRoot)).applied ==
                    0,
                "raw/manual recalls, arguments, calls, returns and permutations must remain anchored");
      }
    }
    auto live_y = staged;
    live_y.insert(live_y.begin() +
                      static_cast<std::ptrdiff_t>(accepted.calls.front().continuation_item_index),
                  MachineItem::op(0x10, "observe retained Y"));
    require(core::rewrite_helper_invariant_recall_hoist(live_y, std::string(kRoot)).applied == 0,
            "commutativity does not prove the retained operand dead");

    auto noncommutative = staged;
    noncommutative.at(accepted.calls.back().continuation_item_index - 1U) =
        MachineItem::op(0x11, "subtract");
    require(core::rewrite_helper_invariant_recall_hoist(noncommutative, std::string(kRoot)).applied ==
                0,
            "a stack permutation must not authorize a noncommutative join");

    auto restore_x2 = staged;
    restore_x2.insert(
        restore_x2.begin() + static_cast<std::ptrdiff_t>(
                                accepted.calls.back().join_permutation_items.front()),
        MachineItem::op(0x0c, "VP"));
    require(core::rewrite_helper_invariant_recall_hoist(restore_x2, std::string(kRoot)).applied == 0,
            "X2 restoration is not a pure stack permutation");

    auto writes_operand = staged;
    writes_operand.insert(
        writes_operand.begin() + static_cast<std::ptrdiff_t>(accepted.helper_return_item_index),
        MachineItem::op(0x48, "overwrite common operand"));
    require(core::rewrite_helper_invariant_recall_hoist(writes_operand, std::string(kRoot)).applied ==
                0,
            "an invariant recall cannot cross a helper write of its register");

    for (const std::size_t entry : {accepted.calls.front().argument_preparation_items.front(),
                                    accepted.calls.front().call_item_index}) {
      for (const bool indirect : {false, true}) {
        auto entered = staged;
        const std::size_t root = label_index(entered, kRoot);
        const int target = item_address(entered, entry);
        core::HelperInvariantRecallHoistOptions options;
        if (indirect) {
          entered.insert(entered.begin() + static_cast<std::ptrdiff_t>(root),
                         MachineItem::op(0x8e, "unrelated indirect entry"));
          options.proved_indirect_flow_targets.emplace(root, std::vector<int>{target});
        } else {
          entered.insert(entered.begin() + static_cast<std::ptrdiff_t>(root),
                         {MachineItem::op(0x51, "unrelated direct entry"),
                          MachineItem::address(target)});
          options.fixed_direct_address_targets.emplace(root + 1U, target);
          options.retargetable_direct_address_items.insert(root + 1U);
        }
        const auto rejected =
            core::rewrite_helper_invariant_recall_hoist(entered, std::string(kRoot), options);
        require(rejected.applied == 0 && contains_reason(rejected.proof, "bypasses a moved recall"),
                "a separately addressed argument or call may bypass the moved common recall");
      }
    }

    auto labelled = staged;
    labelled.insert(labelled.begin() + static_cast<std::ptrdiff_t>(
                                          accepted.calls.front().argument_preparation_items.front()),
                    MachineItem::label("separate_argument_entry"));
    const std::size_t root = label_index(labelled, kRoot);
    labelled.insert(labelled.begin() + static_cast<std::ptrdiff_t>(root),
                    {MachineItem::op(0x51, "enter preparation"),
                     MachineItem::address("separate_argument_entry")});
    require(core::rewrite_helper_invariant_recall_hoist(labelled, std::string(kRoot)).applied == 0,
            "a referenced label must fence argument staging");

    core::HelperInvariantRecallHoistOptions entry_proof;
    entry_proof.simultaneous_entry_recall_opcode = 0x61;
    for (const auto& call : accepted.calls)
      entry_proof.entry_x_proved_call_items.insert(call.call_item_index);
    require(core::rewrite_helper_invariant_recall_hoist(staged, std::string(kRoot), entry_proof)
                    .applied == 0,
            "an entry-X fact must not be reused across unproved argument preparation");
  }

  for (const bool retarget : {false, true}) {
    auto addressed = staged_operand_fixture(1);
    addressed.insert(addressed.begin(),
                     {MachineItem::op(0x51, "enter first caller"), MachineItem::address(2)});
    core::HelperInvariantRecallHoistOptions options;
    options.fixed_direct_address_targets.emplace(1U, 2);
    if (retarget)
      options.retargetable_direct_address_items.insert(1U);
    const auto accepted =
        core::rewrite_helper_invariant_recall_hoist(addressed, std::string(kRoot), options);
    require(accepted.applied == 1 && accepted.proof.final_artifact_proved &&
                cell_count(accepted.items) == cell_count(addressed) - (retarget ? 2 : 1),
            "staged call entries must preserve fixed geometry or retarget to the first argument");
    const auto resolved = resolve_machine_items(accepted.items, {});
    require(resolved.diagnostics.empty() && resolved.steps.at(2).opcode == (retarget ? 0x61 : 0x54),
            "a removed entry recall must not redirect past argument preparation to PP");
    require(observe_staged_operand(addressed, "4") ==
                observe_staged_operand(accepted.items, "4"),
            "emulator entry through a fixed/retargeted caller must execute all arguments");
  }

  const std::vector<MachineItem> baseline = alpha_fixture();
  const core::HelperInvariantRecallHoistProof proof =
      core::verify_helper_invariant_recall_hoist(baseline, std::string(kRoot));
  require(proof.proved && proof.reasons.empty(),
          "three alpha call sites should pass the bounded recall-hoist proof");
  require(proof.calls.size() == 3U && proof.recall_opcode == 0x68 && proof.register_index == 8 &&
              proof.helper_body_cells == 2U,
          "proof should expose the complete call set, register, and helper body");
  require(
      std::count_if(proof.calls.begin(), proof.calls.end(),
                    [](const auto& call) {
                      return call.placement ==
                             core::HelperInvariantRecallPlacement::
                                 BeforeCallBeforeCommutative;
                    }) == 2 &&
          std::count_if(proof.calls.begin(), proof.calls.end(),
                        [](const auto& call) {
                          return call.placement ==
                                 core::HelperInvariantRecallPlacement::AfterReturnBeforeCommutative;
                        }) == 1,
      "proof should distinguish both commutative recall placements");

  const core::HelperInvariantRecallHoistResult rewritten =
      core::rewrite_helper_invariant_recall_hoist(baseline, std::string(kRoot));
  require(rewritten.applied == 1 && rewritten.proof.final_artifact_proved,
          "proved recall hoist should survive the final artifact check");
  require(rewritten.proof.insertion ==
              core::HelperInvariantRecallInsertion::BeforeReturn,
          "mixed before-call/after-return commutative sites should share one tail recall");
  require(cell_count(rewritten.items) == cell_count(baseline) - 2 &&
              opcode_count(baseline, 0x68) == 3 && opcode_count(rewritten.items, 0x68) == 1,
          "three call-site recalls should become one helper-tail recall and save two cells");
  const std::size_t rewritten_root = label_index(rewritten.items, kRoot);
  require(rewritten_root + 1U < rewritten.items.size() &&
              rewritten.items.at(rewritten_root + 1U).kind == MachineItemKind::Op &&
              rewritten.items.at(rewritten_root + 1U).opcode == 0x61 &&
              rewritten.items.at(rewritten_root + 3U).opcode == 0x68 &&
              rewritten.items.at(rewritten_root + 4U).opcode == 0x52,
          "the retained recall should be immediately before the helper return");
  require(run(baseline) == run(rewritten.items),
          "baseline and hoisted alpha fixtures should be emulator-equivalent");

  {
    const std::vector<MachineItem> after_return = after_return_fixture();
    const auto accepted =
        core::rewrite_helper_invariant_recall_hoist(after_return, std::string(kRoot));
    require(accepted.applied == 1 && accepted.proof.final_artifact_proved &&
                accepted.proof.insertion ==
                    core::HelperInvariantRecallInsertion::BeforeReturn,
            "all after-return commutative recalls should be inserted at the helper tail");
    const std::size_t root = label_index(accepted.items, kRoot);
    require(accepted.items.at(root + 1U).opcode == 0x61 &&
                accepted.items.at(root + 3U).opcode == 0x68 &&
                accepted.items.at(root + 4U).opcode == 0x52,
            "tail insertion must leave helper inputs untouched and place recall before V/O");
    require(cell_count(accepted.items) == cell_count(after_return) - 2 &&
                run(after_return) == run(accepted.items),
            "tail recall insertion should save two cells and remain emulator-equivalent");
  }

  {
    const std::vector<MachineItem> forward = forward_jump_continuation_fixture();
    const auto accepted =
        core::rewrite_helper_invariant_recall_hoist(forward, std::string(kRoot));
    require(accepted.applied == 1 && accepted.proof.final_artifact_proved,
            "bounded symbolic continuation should follow a proved forward direct jump");
    require(run(forward) == run(accepted.items),
            "forward-jump recall hoist should remain emulator-equivalent");
  }

  {
    std::vector<MachineItem> second_entry = alpha_fixture();
    const std::size_t root = label_index(second_entry, kRoot);
    second_entry.insert(second_entry.begin() + static_cast<std::ptrdiff_t>(root + 2U),
                        MachineItem::label("z4"));
    const auto accepted =
        core::rewrite_helper_invariant_recall_hoist(second_entry, std::string(kRoot));
    require(accepted.applied == 1 && accepted.proof.final_artifact_proved &&
                run(second_entry) == run(accepted.items),
            "an unreferenced internal helper label should not be mistaken for an entry");

    second_entry.insert(second_entry.begin(),
                        {MachineItem::op(0x51, "external branch"),
                         MachineItem::address("z4")});
    const auto rejected =
        core::rewrite_helper_invariant_recall_hoist(second_entry, std::string(kRoot));
    require(rejected.applied == 0 && rejected.items.size() == second_entry.size() &&
                contains_reason(rejected.proof, "second referenced entry"),
            "a genuinely referenced second helper entry should fail closed");
  }

  for (const int opcode : {0x68, 0x48}) {
    const std::vector<MachineItem> access = helper_with_inserted_op(opcode);
    const auto rejected = core::rewrite_helper_invariant_recall_hoist(access, std::string(kRoot));
    require(rejected.applied == 0 &&
                contains_reason(rejected.proof, "reads or writes the register"),
            "helper read/write of the hoisted register should fail closed");
  }

  {
    const std::vector<MachineItem> noncommutative = noncommutative_after_return_fixture();
    const auto rejected =
        core::rewrite_helper_invariant_recall_hoist(noncommutative, std::string(kRoot));
    require(rejected.applied == 0 &&
                contains_reason(rejected.proof, "no single direct-register recall"),
            "a noncommutative after-return consumer should fail closed");
  }

  {
    const std::vector<MachineItem> x2_live = x2_live_fixture();
    const auto rejected = core::rewrite_helper_invariant_recall_hoist(x2_live, std::string(kRoot));
    require(rejected.applied == 0 &&
                contains_reason(rejected.proof, "X2 is observed before a proved overwrite"),
            "an X2 restore before the symbolic kill should fail closed");
  }

  {
    std::vector<MachineItem> fixed_geometry = alpha_fixture();
    const std::size_t root = label_index(fixed_geometry, kRoot);
    std::size_t first_call_recall = 0;
    while (first_call_recall < fixed_geometry.size() &&
           (fixed_geometry.at(first_call_recall).kind != MachineItemKind::Op ||
            fixed_geometry.at(first_call_recall).opcode != 0x68)) {
      ++first_call_recall;
    }
    fixed_geometry.insert(fixed_geometry.begin() + static_cast<std::ptrdiff_t>(root),
                          MachineItem::op(0x80, "К БП 0"));
    const std::size_t fixed_flow_item = root;
    const int fixed_target = item_address(fixed_geometry, first_call_recall);
    core::HelperInvariantRecallHoistOptions options;
    options.proved_indirect_flow_targets.emplace(fixed_flow_item,
                                                 std::vector<int>{fixed_target});
    options.fixed_indirect_flow_targets = options.proved_indirect_flow_targets;
    const auto accepted = core::rewrite_helper_invariant_recall_hoist(
        fixed_geometry, std::string(kRoot), options);
    require(accepted.applied == 1 && accepted.proof.final_artifact_proved &&
                accepted.proof.nop_recall_items.size() == 1U &&
                opcode_count(accepted.items, 0x54) == 1 &&
                cell_count(accepted.items) == cell_count(fixed_geometry) - 1,
            "fixed targets should retain only the minimum NOP padding and still save a cell");
    require(accepted.proof.final_indirect_flow_targets.size() == 1U &&
                accepted.proof.final_indirect_flow_targets.begin()->second ==
                    std::vector<int>{fixed_target},
            "fixed-target NOP padding should preserve the target address exactly");
    require(run(fixed_geometry) == run(accepted.items),
            "fixed-target padded recall hoist should remain emulator-equivalent");
  }

  {
    std::vector<MachineItem> retargetable = alpha_fixture();
    std::vector<std::size_t> before_call_recalls;
    for (std::size_t index = 0; index + 1U < retargetable.size(); ++index) {
      if (retargetable.at(index).kind == MachineItemKind::Op &&
          retargetable.at(index).opcode == 0x68 &&
          retargetable.at(index + 1U).kind == MachineItemKind::Op &&
          retargetable.at(index + 1U).opcode == 0x53) {
        before_call_recalls.push_back(index);
      }
    }
    require(before_call_recalls.size() == 2U,
            "retarget fixture should expose two before-call recalls");
    const int old_target = item_address(retargetable, before_call_recalls.back());
    const std::size_t root = label_index(retargetable, kRoot);
    MachineItem operand = MachineItem::address(old_target);
    operand.comment = "retargetable call-entry fixture";
    retargetable.insert(retargetable.begin() + static_cast<std::ptrdiff_t>(root),
                        {MachineItem::op(0x51, "БП"), operand});
    const std::size_t operand_item = root + 1U;
    core::HelperInvariantRecallHoistOptions options;
    options.fixed_direct_address_targets.emplace(operand_item, old_target);
    options.retargetable_direct_address_items.insert(operand_item);
    const auto accepted = core::rewrite_helper_invariant_recall_hoist(
        retargetable, std::string(kRoot), options);
    require(accepted.applied == 1 && accepted.proof.final_artifact_proved &&
                accepted.proof.nop_recall_items.empty() &&
                cell_count(accepted.items) == cell_count(retargetable) - 2,
            "ordinary direct operands should follow a removed recall to its call and save two cells");
    const auto retargeted_operand = std::find_if(
        accepted.items.begin(), accepted.items.end(), [](const MachineItem& item) {
          return item.kind == MachineItemKind::Address && item.comment.has_value() &&
                 *item.comment == "retargetable call-entry fixture";
        });
    require(retargeted_operand != accepted.items.end() &&
                std::holds_alternative<int>(retargeted_operand->target) &&
                std::get<int>(retargeted_operand->target) == old_target - 1,
            "direct call-entry operand should be retargeted to the surviving call identity");
    require(run(retargetable) == run(accepted.items),
            "retargeted direct-entry recall hoist should remain emulator-equivalent");
  }

  {
    const IndirectCallFixture indirect_calls = indirect_call_fixture();
    const auto accepted = core::rewrite_helper_invariant_recall_hoist(
        indirect_calls.items, std::string(kRoot), indirect_calls.options);
    require(accepted.applied == 1 && accepted.proof.final_artifact_proved &&
                accepted.proof.calls.size() == 3U &&
                std::all_of(accepted.proof.calls.begin(), accepted.proof.calls.end(),
                            [](const auto& call) { return call.indirect; }),
            "proved single-target KPP calls should participate in a recall hoist");
    require(cell_count(accepted.items) == cell_count(indirect_calls.items) - 2 &&
                run(indirect_calls.items) == run(accepted.items),
            "indirect-call recall hoist should save two cells and remain emulator-equivalent");
  }

  {
    IndirectCallFixture ambiguous = indirect_call_fixture();
    ambiguous.options.proved_indirect_flow_targets.at(ambiguous.call_items.front()).push_back(0);
    const auto rejected = core::rewrite_helper_invariant_recall_hoist(
        ambiguous.items, std::string(kRoot), ambiguous.options);
    require(rejected.applied == 0 && contains_reason(rejected.proof, "helper body"),
            "a multi-target KPP that may enter the helper should fail closed");
  }

  {
    IndirectCallFixture unknown = indirect_call_fixture();
    unknown.options.proved_indirect_flow_targets.erase(unknown.call_items.front());
    const auto rejected = core::rewrite_helper_invariant_recall_hoist(
        unknown.items, std::string(kRoot), unknown.options);
    require(rejected.applied == 0 && contains_reason(rejected.proof, "unknown indirect flow"),
            "an unproved KPP target should fail closed");
  }

  {
    std::vector<MachineItem> proved_indirect = alpha_fixture();
    const std::size_t root = label_index(proved_indirect, kRoot);
    std::size_t main_stop = root;
    while (main_stop > 0) {
      --main_stop;
      if (proved_indirect.at(main_stop).kind == MachineItemKind::Op &&
          proved_indirect.at(main_stop).opcode == 0x50) {
        break;
      }
    }
    proved_indirect.insert(proved_indirect.begin() + static_cast<std::ptrdiff_t>(main_stop),
                           MachineItem::op(0x80, "К БП 0"));
    const std::size_t old_flow_item = main_stop;
    const int old_target = item_address(proved_indirect, main_stop + 1U);
    core::HelperInvariantRecallHoistOptions options;
    options.proved_indirect_flow_targets.emplace(old_flow_item, std::vector<int>{old_target});
    const auto accepted =
        core::rewrite_helper_invariant_recall_hoist(proved_indirect, std::string(kRoot), options);
    const std::size_t new_flow_item = old_flow_item - 3U;
    require(accepted.applied == 1 && accepted.proof.final_artifact_proved &&
                accepted.proof.final_indirect_flow_targets.contains(new_flow_item) &&
                accepted.proof.final_indirect_flow_targets.at(new_flow_item) ==
                    std::vector<int>{old_target - 3},
            "a complete unrelated indirect-flow proof should be retained and reindexed");
  }

  {
    std::vector<MachineItem> removed_target = alpha_fixture();
    removed_target.insert(removed_target.begin(), MachineItem::op(0x80, "К БП 0"));
    std::size_t after_return_recall = 0;
    for (std::size_t index = 0; index + 2U < removed_target.size(); ++index) {
      if (removed_target.at(index).kind == MachineItemKind::Op &&
          removed_target.at(index).opcode == 0x53 &&
          removed_target.at(index + 1U).kind == MachineItemKind::Address &&
          removed_target.at(index + 2U).kind == MachineItemKind::Op &&
          removed_target.at(index + 2U).opcode == 0x68) {
        after_return_recall = index + 2U;
        break;
      }
    }
    core::HelperInvariantRecallHoistOptions options;
    options.proved_indirect_flow_targets.emplace(
        0U, std::vector<int>{item_address(removed_target, after_return_recall)});
    const auto rejected =
        core::rewrite_helper_invariant_recall_hoist(removed_target, std::string(kRoot), options);
    require(rejected.applied == 0 && contains_reason(rejected.proof, "removed call-site recall"),
            "an indirect target into a removed recall should fail closed");
  }

  {
    std::vector<MachineItem> helper_target = alpha_fixture();
    helper_target.insert(helper_target.begin(), MachineItem::op(0x80, "К БП 0"));
    const std::size_t root = label_index(helper_target, kRoot);
    core::HelperInvariantRecallHoistOptions options;
    options.proved_indirect_flow_targets.emplace(
        0U, std::vector<int>{item_address(helper_target, root)});
    const auto rejected =
        core::rewrite_helper_invariant_recall_hoist(helper_target, std::string(kRoot), options);
    require(rejected.applied == 0 && contains_reason(rejected.proof, "can enter the helper body"),
            "an indirect target into the helper should fail closed");
  }

  {
    std::vector<MachineItem> indirect = alpha_fixture();
    indirect.insert(indirect.begin(), MachineItem::op(0x80, "К БП 0"));
    const auto rejected = core::rewrite_helper_invariant_recall_hoist(indirect, std::string(kRoot));
    require(rejected.applied == 0 && contains_reason(rejected.proof, "unknown indirect flow"),
            "unknown indirect flow should prevent a complete entry proof");
  }
}

} // namespace mkpro::tests
