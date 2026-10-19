#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <exception>
#include <iostream>
#include <map>
#include <set>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;

std::vector<MachineItem> fixture(bool known, int entry_closer, bool raw_digit) {
  MachineEmitter e;
  for (int call = 0; call < 2; ++call) {
    e.emit_op(entry_closer);
    if (known) {
      e.emit_op(call == 0 ? 3 : 4);
      e.emit_op(call == 0 ? 1 : 7);
      e.items.back().raw = raw_digit;
    } else {
      e.emit_op(0x60 + call);
    }
    e.emit_op(0x47);
    e.emit_op(0xa7);
    e.items.back().indirect_flow_targets = std::vector<IrTarget>{"left", "right"};
  }
  e.emit_stop(StopDisposition::Terminal);
  const auto pad_to = [&](int address) {
    int cells = 0;
    for (const auto& item : e.items)
      if (item.kind != MachineItemKind::Label) ++cells;
    require(cells <= address, "fixture helper placement");
    while (cells++ < address) e.emit_op(0x54);
  };
  pad_to(31);
  e.emit_label("left");
  e.emit_op(2); e.emit_op(0x42); e.emit_op(0x52);
  pad_to(47);
  e.emit_label("right");
  e.emit_op(4); e.emit_op(0x43); e.emit_op(0x52);
  return e.items;
}

void contract(bool known, int entry_closer = 0x0d, bool raw_digit = false) {
  const auto items = fixture(known, entry_closer, raw_digit);
  const bool exact = known && !raw_digit;
  const auto flow = build_post_layout_control_flow(items);
  require(flow.proved, "selector fixture requires a complete CFG");
  const auto product = build_selector_consistent_execution_graph(items, flow);
  require(product.proved, "selector product must be available");
  require(exact ? product.pruned_edges > 0 : product.pruned_edges == 0,
          "exact word reuse must not invent facts for unknown recalled input");
  std::set<std::vector<int>> return_contexts;
  for (const auto original : product.original_states) {
    const auto& state = flow.execution_states[original];
    if (state.address == 33) return_contexts.insert(state.return_stack);
  }
  require(return_contexts.size() == (exact ? 1U : 2U),
          "only an independently known selector may discard a caller alternative");
  if (known) {
    const auto lift = analyze_post_layout_automatic_entry_lift(items, flow);
    require(lift.has_value(), "entry-lift proof must be available");
    bool reached = false;
    for (std::size_t i = 0; i < flow.execution_states.size(); ++i)
      if (flow.execution_states[i].address == 1) {
        reached = true;
        require(!lift->at(i), "numeric-entry closure is not a stack-lift certificate");
      }
    require(reached, "first digit must be reachable");
  }
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "selector fixture must resolve");
  std::vector<int> code;
  for (const auto& step : resolved.steps) code.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(code).diagnostics.empty(), "selector fixture must fit stock ROM");
  calc.set_register("0", "31"); calc.set_register("1", "47");
  calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  require(calc.run_until_stable(5000, 8).stopped, "both helper calls must return");
  require(calc.read_register("2").starts_with("2,") &&
              calc.read_register("3").starts_with("4,"),
          "ROM must execute the selected helpers in caller order");
}

void transported_prefix_contract(bool marked, bool raw_digit, bool open_entry) {
  MachineEmitter e;
  for (int call = 0; call < 2; ++call) {
    e.emit_op(open_entry ? 9 : 0x0d);
    e.emit_op(call == 0 ? 0x8e : 0x8d);
    e.items.back().indirect_flow_targets = std::vector<IrTarget>{
        call == 0 ? "first_digits" : "second_digits"};
    e.emit_label(call == 0 ? "first_digits" : "second_digits");
    e.emit_op(call == 0 ? 3 : 4);
    if (marked) e.items.back().roles.push_back(std::string(kTransportedConstantPrefixRole));
    e.items.back().raw = raw_digit;
    e.emit_op(call == 0 ? 1 : 7);
    e.emit_op(0x47);
    e.emit_op(0xa7);
    e.items.back().indirect_flow_targets = std::vector<IrTarget>{"left", "right"};
  }
  e.emit_stop(StopDisposition::Terminal);
  const auto pad_to = [&](int wanted) {
    int address = 0;
    for (const auto& item : e.items)
      if (item.kind != MachineItemKind::Label) ++address;
    require(address <= wanted, "transport fixture placement");
    while (address++ < wanted) e.emit_op(0x54);
  };
  pad_to(31);
  e.emit_label("left");
  e.emit_op(2); e.emit_op(0x42); e.emit_op(0x52);
  pad_to(47);
  e.emit_label("right");
  e.emit_op(4); e.emit_op(0x43); e.emit_op(0x52);
  const auto flow = build_post_layout_control_flow(e.items);
  require(flow.proved, "transport fixture CFG");
  const std::map<std::string, int> origins{
      {"first_digits", 2}, {"second_digits", 8}, {"left", 31}, {"right", 47}};
  const auto closed = build_selector_consistent_execution_graph(e.items, flow);
  const auto deferred = build_deferred_selector_consistent_execution_graph(
      e.items, flow, origins).execution;
  const bool exact = marked && !raw_digit && !open_entry;
  for (const auto* product : {&closed, &deferred}) {
    require(product->proved, "both selector geometries must be proved");
    require((product->pruned_edges > 0) == exact,
            "stable flow must transport, never invent, a fresh literal buffer");
    std::set<std::vector<int>> callers;
    for (const auto original : product->original_states) {
      const auto& state = flow.execution_states.at(original);
      if (state.address == 33) callers.insert(state.return_stack);
    }
    require(callers.size() == (exact ? 1U : 2U),
            "closed geometry must retain the same caller precision as deferred geometry");
  }
  if (!open_entry) {
    const auto resolved = resolve_machine_items(e.items);
    require(resolved.diagnostics.empty(), "transport fixture resolves");
    std::vector<int> code;
    for (const auto& step : resolved.steps) code.push_back(step.opcode);
    emulator::MK61 calc;
    require(calc.load_program(code).diagnostics.empty(), "transport fixture fits stock ROM");
    calc.set_register("e", "2"); calc.set_register("d", "8");
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(5000, 8).stopped, "transported calls return");
    require(calc.read_register("2").starts_with("2,") &&
                calc.read_register("3").starts_with("4,"), "transported ROM caller order");
  }
}
}

int main() {
  try {
    contract(true);
    contract(true, 0x0e);
    contract(true, 0x0d, true);
    contract(false);
    transported_prefix_contract(true, false, false);
    transported_prefix_contract(false, false, false);
    transported_prefix_contract(true, true, false);
    transported_prefix_contract(true, false, true);
    std::cout << "selector word and independent entry-lift contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
