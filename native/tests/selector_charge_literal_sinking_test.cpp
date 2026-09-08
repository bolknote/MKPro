#include "mkpro/core/passes/selector_charge_literal_sinking.hpp"
#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/late_bound_decimal_selector.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace mkpro::tests {
namespace {

using core::LateBoundDecimalSelectorPart;
using core::passes::PassContext;

std::vector<IrOp> fixture(const std::string& prefix, int literal, bool other_input_live = false) {
  std::vector<MachineItem> items;
  const auto op = [&](int code) {
    items.push_back(MachineItem::op(code, code <= 9 ? std::to_string(code) : ""));
  };
  const auto label = [&](const std::string& name) {
    items.push_back(MachineItem::label(prefix + name));
  };
  const auto call = [&](const std::string& name) {
    op(0x53); items.push_back(MachineItem::address(prefix + name));
  };
  const auto charge = [&](const std::string& name) {
    for (const auto part : {LateBoundDecimalSelectorPart::High,
                            LateBoundDecimalSelectorPart::Low}) {
      op(0);
      items.back().roles = {
          core::make_late_bound_decimal_selector_role(part, prefix + name),
          "late-decimal-selector-register:a"};
    }
    call("shared");
  };
  const auto clear_stack = [&] {
    op(0x0d); op(0x0e); op(0x0e); op(0x0e);
  };
  op(2); op(0x41); op(3); op(0x42);
  call("data");
  op(0x43);
  clear_stack();
  call("discarded");
  op(0x44);
  clear_stack();
  op(0x63);
  op(0x50); items.back().stop_disposition = StopDisposition::Terminal;
  label("data");
  for (char digit : std::to_string(literal)) {
    if (digit == std::to_string(literal).back() && literal >= 10)
      label("last-digit");
    op(digit - '0');
  }
  op(0x0e); items.back().roles = {"callee-hole-entry-lift"};
  charge("sum");
  op(0x52);
  label("discarded");
  op(0x61);
  charge("erase");
  op(0x52);
  label("shared");
  op(0x4a);
  op(0x25);
  op(0x61); op(0x62);
  op(0xaa);
  items.back().indirect_flow_targets =
      std::vector<IrTarget>{prefix + "sum", prefix + "erase"};
  op(0x52);
  label("sum");
  op(0x10); op(0x10); op(0x52);
  label("erase");
  if (other_input_live) { op(0x10); op(0x10); }
  else { clear_stack(); op(0x10); }
  op(0x52);
  return raise_machine_to_ir(items);
}

std::vector<int> bytes(const std::vector<IrOp>& ops) {
  const auto bound = core::bind_late_bound_decimal_selectors(
      lower_ir_to_machine(ops), {.minimum_target_address = 0});
  require(bound.diagnostics.empty(), "synthetic selector charge must bind");
  std::map<std::string, int> labels;
  int address = 0;
  for (const auto& item : bound.items) {
    if (item.kind == MachineItemKind::Label) labels[item.name] = address;
    else ++address;
  }
  std::vector<int> result;
  for (const auto& item : bound.items) {
    if (item.kind == MachineItemKind::Label) continue;
    if (item.kind == MachineItemKind::Op) result.push_back(item.opcode);
    else {
      const auto target = labels.at(std::get<std::string>(item.target));
      result.push_back((target / 10) * 16 + target % 10);
    }
  }
  return result;
}

std::vector<std::string> run(const std::vector<int>& program, const std::string& seed) {
  emulator::MK61 calc;
  require(calc.load_program(program).diagnostics.empty(), "fixture must fit MK-61");
  for (const auto& reg : {"X", "Y", "Z", "T"}) calc.set_register(reg, seed);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(5000, 6).stopped, "fixture must stop");
  std::vector<std::string> result;
  for (const auto& reg : {"X", "Y", "Z", "T", "1", "2", "3", "4"})
    result.push_back(calc.read_register(reg));
  result.push_back(calc.read_register("X1"));
  calc.press(".");
  result.push_back(calc.display_text(true));
  return result;
}

void final_sunk_literal_proof(const std::vector<IrOp>& ops, const std::string& prefix) {
  const auto bound = core::bind_late_bound_decimal_selectors(
      lower_ir_to_machine(ops), {.minimum_target_address = 0});
  require(bound.diagnostics.empty(), "final sunk literal must bind");
  const auto verify = [&](const std::vector<MachineItem>& items) {
    const auto flow = core::build_post_layout_control_flow(items);
    std::size_t store = items.size();
    for (std::size_t index = 0; index < items.size(); ++index)
      for (const auto& role : items[index].roles)
        if (role == "selector-sunk-literal-id:" + prefix + "shared") store = index;
    if (store == items.size()) return false;
    int proved = 0;
    for (std::size_t index = 0; index < items.size(); ++index) {
      const bool charge = std::any_of(items[index].roles.begin(), items[index].roles.end(),
          [](const std::string& role) { return role.starts_with("late-decimal-selector-high:"); });
      if (!charge) continue;
      std::size_t low = index + 1;
      while (low < items.size() && items[low].kind == MachineItemKind::Label) ++low;
      if (low == items.size() || !core::passes::prove_selector_charge_sunk_literal_entry(
              items, flow, store, index, items[index].opcode * 10 + items[low].opcode))
        return false;
      ++proved;
    }
    return proved == 2;
  };
  require(verify(bound.items), "both final selector entries must independently prove convergence");
  auto changed = bound.items;
  for (std::size_t index = 0; index + 1 < changed.size(); ++index)
    if (std::find(changed[index].roles.begin(), changed[index].roles.end(),
                  "selector-sunk-literal-id:" + prefix + "shared") != changed[index].roles.end()) {
      changed[index + 1].opcode = (changed[index + 1].opcode + 1) % 10;
      break;
    }
  require(!verify(changed), "a changed final literal must not inherit its old proof");
  changed = bound.items;
  for (auto& item : changed)
    item.roles.erase(std::remove_if(item.roles.begin(), item.roles.end(),
        [](const std::string& role) { return role.starts_with("selector-sunk-literal-source:"); }),
        item.roles.end());
  require(!verify(changed), "a missing producer identity must reject the final proof");
  changed = bound.items;
  for (std::size_t index = 0; index + 2 < changed.size(); ++index)
    if (changed[index].kind == MachineItemKind::Label && changed[index].name == prefix + "erase") {
      changed[index + 1] = MachineItem::op(0x10, "+");
      changed[index + 2] = MachineItem::op(0x10, "+");
      break;
    }
  require(!verify(changed), "an alternative that now observes the input must reject the final proof");
}

void decimal_entry_proof_requires_known_phase() {
  std::vector<MachineItem> items{
      MachineItem::op(0x61, "recall"), MachineItem::op(0x62, "recall"),
      MachineItem::op(1, "1"), MachineItem::op(0, "0"), MachineItem::op(0x50, "stop")};
  items.back().stop_disposition = StopDisposition::Terminal;
  const core::StackValueEqualityState equal_x{{true, false, false, false}, false, true};
  const auto prove = [&](const std::vector<MachineItem>& program,
                         core::StackValueEqualityState state) {
    return core::prove_post_layout_stack_entry_equality(
        program, core::build_post_layout_control_flow(program), 0, state);
  };
  require(prove(items, equal_x),
          "recall closes entry, so only the first digit lifts the equal X into T");
  auto unequal_x1 = equal_x;
  unequal_x1.x1_equal = false;
  require(!prove(items, unequal_x1), "numeric entry must not erase a live physical X1");
  auto unknown = items;
  unknown.insert(unknown.begin() + 2,
                 {MachineItem::op(0x51, "jump"), MachineItem::address("digits"),
                  MachineItem::label("digits")});
  require(!prove(unknown, equal_x), "an unproved branch boundary must keep entry mode unknown");
  auto entered = items;
  entered.insert(entered.begin() + 2, MachineItem::op(0x0e, "enter"));
  require(!prove(entered, {{false, false, false, false}, false, true}),
          "Enter suppresses the next digit's lift and must not invent convergence");
  const auto observe = [](bool enter, const std::string& x, const std::string& rest) {
    emulator::MK61 calc;
    std::vector<int> code{0x61, 0x62};
    if (enter) code.push_back(0x0e);
    code.insert(code.end(), {1, 0, 0x50});
    calc.load_program(code);
    calc.set_register("1", "2"); calc.set_register("2", "3");
    calc.set_register("X", x); calc.set_register("X1", "5");
    for (const auto& reg : {"Y", "Z", "T"}) calc.set_register(reg, rest);
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(5000, 6).stopped, "decimal-entry fact must stop");
    std::vector<std::string> values;
    for (const auto& reg : {"X", "Y", "Z", "T", "X1"})
      values.push_back(calc.read_register(reg));
    calc.press("."); values.push_back(calc.display_text(true));
    return values;
  };
  require(observe(false, "7", "13") == observe(false, "7", "-92"),
          "ROM must confirm a single fresh-entry lift after direct recall");
  require(observe(true, "7", "13") != observe(true, "8", "-92"),
          "ROM must preserve differing T when Enter suppresses the digit's lift");
}

} // namespace

void selector_charge_literal_sinking_is_generic_and_proof_gated() {
  decimal_entry_proof_requires_known_phase();
  CompileOptions options;
  PassContext context{.options = options};
  for (const int literal : {7, 42, 98}) {
    auto source = fixture("opaque-" + std::to_string(literal) + "/", literal);
    // An unused label must not be mistaken for a semantic value/selector name.
    source.erase(std::remove_if(source.begin(), source.end(), [](const IrOp& op) {
      return op.kind == IrKind::Label && op.name.ends_with("last-digit");
    }), source.end());
    const auto result = core::passes::selector_charge_literal_sinking(source, context);
    require(result.applied == 1, "dead alternative input must allow literal sinking");
    final_sunk_literal_proof(result.ops, "opaque-" + std::to_string(literal) + "/");
    const auto before = bytes(source);
    const auto after = bytes(result.ops);
    require(after.size() + 2 == before.size(), "sinking removes the rotation and entry lift");
    for (const auto& seed : {"0", "-7", "3.25"}) {
      const auto expected = run(before, seed);
      const auto actual = run(after, seed);
      require(actual == expected, "sinking must preserve ROM-visible stack and data");
      require(actual[6] == std::to_string(literal + 5) + ",",
              "accumulator must preserve the original literal");
    }
    require(core::passes::selector_charge_literal_sinking(result.ops, context).applied == 0,
            "sinking must reach a fixed point");
  }
  {
    auto source = fixture("live/", 42, true);
    source.erase(std::remove_if(source.begin(), source.end(), [](const IrOp& op) {
      return op.kind == IrKind::Label && op.name.ends_with("last-digit");
    }), source.end());
    require(core::passes::selector_charge_literal_sinking(source, context).applied == 0,
            "an alternative consuming the displaced input must reject sinking");
  }
  {
    auto source = fixture("raw/", 7);
    for (auto& op : source)
      if (op.kind == IrKind::Plain && op.opcode == 7) op.meta.raw = true;
    require(core::passes::selector_charge_literal_sinking(source, context).applied == 0,
            "raw literal cells must not move");
  }
  {
    auto source = fixture("entry/", 42);
    IrOp call;
    call.kind = IrKind::Call; call.opcode = 0x53; call.target = "entry/last-digit";
    source.insert(source.begin(), call);
    require(core::passes::selector_charge_literal_sinking(source, context).applied == 0,
            "independent entry into a producer must not be erased");
  }
  {
    auto source = fixture("restore/", 7);
    const auto rotate = std::find_if(source.begin(), source.end(), [](const IrOp& op) {
      return op.kind == IrKind::Plain && op.opcode == 0x25;
    });
    IrOp dot;
    dot.kind = IrKind::Plain; dot.opcode = 0x0a;
    source.insert(rotate + 1, dot);
    require(core::passes::selector_charge_literal_sinking(source, context).applied == 0,
            "decimal restore before the entry-closing recall must reject sinking");
  }
  {
    auto source = fixture("resume/", 7);
    const auto literal = std::find_if(source.begin(), source.end(), [](const IrOp& op) {
      return op.kind == IrKind::Plain && op.opcode == 7;
    });
    IrOp prompt;
    prompt.kind = IrKind::Stop; prompt.opcode = 0x50;
    prompt.meta.stop_disposition = StopDisposition::Resumable;
    source.insert(literal, prompt);
    require(core::passes::selector_charge_literal_sinking(source, context).applied == 0,
            "manual input may leave an open mantissa and must not move across the charge");
  }
}

} // namespace mkpro::tests
