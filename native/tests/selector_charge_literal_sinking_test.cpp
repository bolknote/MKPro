#include "mkpro/core/passes/selector_charge_literal_sinking.hpp"
#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/late_bound_decimal_selector.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/search_frontier.hpp"
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
  const auto bound = core::rebind_late_bound_decimal_selectors(
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
  const auto bound = core::rebind_late_bound_decimal_selectors(
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


std::vector<IrOp> fallthrough_fixture(const std::string& prefix, int literal,
                                      bool other_input_live = false) {
  auto ops = fixture(prefix, literal, other_input_live);
  ops.erase(std::remove_if(ops.begin(), ops.end(), [&](const IrOp& op) {
    return op.kind == IrKind::Label && op.name == prefix + "last-digit";
  }), ops.end());
  const auto position = [&](const std::string& suffix) {
    const auto found = std::find_if(ops.begin(), ops.end(), [&](const IrOp& op) {
      return op.kind == IrKind::Label && op.name == prefix + suffix;
    });
    require(found != ops.end(), "synthetic region must exist");
    return static_cast<std::size_t>(found - ops.begin());
  };
  const auto data = position("data");
  const auto discarded = position("discarded");
  const auto shared = position("shared");
  const auto sum = position("sum");
  require(ops[discarded - 2].kind == IrKind::Call &&
              ops[discarded - 1].kind == IrKind::Return,
          "fixture tail call must have its ordinary return");
  std::vector<IrOp> result;
  const auto append = [&](std::size_t begin, std::size_t end) {
    result.insert(result.end(), ops.begin() + static_cast<std::ptrdiff_t>(begin),
                  ops.begin() + static_cast<std::ptrdiff_t>(end));
  };
  append(0, data);
  append(discarded, shared);
  // Keep every indirect destination before the rewritten entry. A post-layout
  // rewrite may not silently change a selector's physical address.
  append(sum, ops.size());
  append(data, discarded - 2);
  append(shared, sum);
  const auto transfer = std::find_if(result.begin(), result.end(), [&](const IrOp& op) {
    return op.kind == IrKind::Call &&
           op.target == IrTarget(prefix + "shared");
  });
  require(transfer != result.end(), "alternative entry must keep its explicit transfer");
  IrOp alias;
  alias.kind = IrKind::Label;
  alias.name = prefix + "unused-transfer";
  result.insert(transfer, std::move(alias));
  return result;
}

std::vector<MachineItem> concrete_fixture(const std::vector<IrOp>& ops) {
  const auto bound = core::rebind_late_bound_decimal_selectors(
      lower_ir_to_machine(ops), {.minimum_target_address = 0});
  require(bound.diagnostics.empty(), "post-layout fixture must bind");
  std::map<std::string, int> labels;
  int address = 0;
  for (const auto& item : bound.items) {
    if (item.kind == MachineItemKind::Label) labels[item.name] = address;
    else ++address;
  }
  auto result = bound.items;
  for (auto& item : result) {
    const auto numeric = [&](IrTarget& target) {
      if (const auto* name = std::get_if<std::string>(&target))
        target = labels.at(*name);
    };
    if (item.kind == MachineItemKind::Address) numeric(item.target);
    if (item.indirect_flow_targets)
      for (auto& target : *item.indirect_flow_targets) numeric(target);
  }
  return result;
}

void fallthrough_and_final_layout_contracts() {
  for (const int literal : {7, 42, 98}) {
    const std::string prefix = "entry-" + std::to_string(literal) + "/";
    const auto source = fallthrough_fixture(prefix, literal);
    const CompileOptions options;
    const auto early = core::passes::selector_charge_literal_sinking(source, {.options = options});
    require(early.applied == 1, "fallthrough and unused labels must not hide a valid entry");
    final_sunk_literal_proof(early.ops, prefix);
    const auto final = core::passes::optimize_post_layout_selector_charge_literal_sinking(
        concrete_fixture(source));
    require(final.applied == 1 && final.removed_cells == 2 &&
                final.final_control_flow.proved,
            "the final artifact must preserve exact indirect addresses and save two cells");
    const auto final_ir = raise_machine_to_ir(final.items);
    final_sunk_literal_proof(final_ir, prefix);
    require(core::passes::optimize_post_layout_selector_charge_literal_sinking(final.items)
                .applied == 0, "final entry closure must reach a fixed point");
    for (const auto& seed : {"0", "-7", "3.25"}) {
      const auto expected = run(bytes(source), seed);
      require(run(bytes(early.ops), seed) == expected &&
                  run(bytes(final_ir), seed) == expected,
              "both entry forms must preserve ROM stack, X1/X2 and returned results");
    }
  }
  const CompileOptions options;
  const auto live = fallthrough_fixture("live-tail/", 42, true);
  require(core::passes::selector_charge_literal_sinking(live, {.options = options}).applied == 0 &&
              core::passes::optimize_post_layout_selector_charge_literal_sinking(
                  concrete_fixture(live)).applied == 0,
          "a live alternate input must reject both early and late entry rewrites");
  auto entered = fallthrough_fixture("side-entry/", 7);
  IrOp side_call;
  side_call.kind = IrKind::Call;
  side_call.opcode = 0x53;
  side_call.target = "side-entry/unused-transfer";
  entered.insert(entered.begin(), std::move(side_call));
  require(core::passes::selector_charge_literal_sinking(entered, {.options = options}).applied == 0,
          "a genuinely addressed transfer cannot inherit the preceding charge");
  auto raw = concrete_fixture(fallthrough_fixture("raw-tail/", 7));
  raw.front().raw = true;
  require(core::passes::optimize_post_layout_selector_charge_literal_sinking(raw).applied == 0,
          "raw code must not acquire a symbolic final-layout proof");
  auto moving = fixture("moving-target/", 7);
  require(core::passes::optimize_post_layout_selector_charge_literal_sinking(
              concrete_fixture(moving)).applied == 0,
          "moving a runtime indirect destination requires a separate layout transaction");

  auto empty = fallthrough_fixture("empty-return/", 7);
  IrOp ret;
  ret.kind = IrKind::Return;
  ret.opcode = 0x52;
  empty.insert(empty.begin(), ret);
  const auto concrete = concrete_fixture(empty);
  require(core::passes::optimize_post_layout_selector_charge_literal_sinking(concrete).applied == 0,
          "empty-stack return behavior must not be inferred from an opcode");
  const auto explicit_policy =
      core::passes::optimize_post_layout_selector_charge_literal_sinking(
          concrete, {.empty_return_target = 1});
  require(explicit_policy.applied == 1 &&
              explicit_policy.final_control_flow.empty_return_target &&
              explicit_policy.final_control_flow.empty_return_target->address == 1,
          "an explicit hardware policy must preserve the same physical continuation");
  require(run(bytes(raise_machine_to_ir(explicit_policy.items)), "3.25") ==
              run(bytes(empty), "3.25"),
          "ROM must confirm unchanged empty-return startup and later caller returns");
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
struct Candidate {
    int cells;
    bool proved;
  };
  std::optional<Candidate> incumbent;
  const auto retain = [&](Candidate candidate) {
    return core::retain_proved_search_incumbent(
        incumbent, candidate,
        [](const Candidate& left, const Candidate& right) { return left.cells < right.cells; },
        [](const Candidate& value) { return value.proved; });
  };
  require(retain({41, true}), "initial proved artifact must be retained");
  require(!retain({35, false}) && incumbent->cells == 41,
          "a smaller invalid intermediate must not discard the valid incumbent");
  require(retain({39, true}) && incumbent->cells == 39,
          "a later valid closure must be allowed to improve the incumbent");
  require(!retain({39, true}) && !retain({40, true}) && incumbent->cells == 39,
          "equal or larger artifacts must not replace the deterministic incumbent");
  decimal_entry_proof_requires_known_phase();
  fallthrough_and_final_layout_contracts();
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
