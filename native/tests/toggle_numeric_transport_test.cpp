#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/core/post_layout_packed_sign.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;

struct Fixture {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
  std::vector<std::size_t> observations;
};

Fixture fixture(const std::string& initial,bool indirect=true,bool overwrite=false) {
  MachineEmitter e;
  e.emit_label("return");e.emit_op(0x52);
  std::vector<std::size_t> observations;
  for(int phase=0;phase<2;++phase) {
    e.emit_op(0x6b);e.emit_op(0x0b);e.emit_op(0x4b);
    if(indirect) {
      e.emit_op(0xab);
      e.items.back().indirect_flow_targets=std::vector<IrTarget>{std::string("return")};
    }
    if(overwrite && phase==0) {e.emit_op(0x62);e.emit_op(0x4b);}
    e.emit_op(0x6b);
    observations.push_back(e.items.size());e.emit_op(0x40+phase);
  }
  e.emit_stop(StopDisposition::Terminal);
  return {e.items,{{.register_name="b",.value=initial}},observations};
}

PostLayoutControlFlowOptions control() {
  PostLayoutControlFlowOptions options;options.empty_return_target=1;return options;
}

std::vector<std::size_t> all_queries(const AuthoritativePostLayoutControlFlow& flow) {
  std::vector<std::size_t> result;
  for(std::size_t state=0;state<flow.execution_states.size();++state)result.push_back(state);
  return result;
}

void stable_fractional_carrier() {
  for(const auto* seed:{"0.1","-0.1","1.0000000e-1","-1.0000000e-1"}) {
    const auto f=fixture(seed);
    const auto flow=build_post_layout_control_flow(f.items,control());
    require(flow.proved,"closed fractional carrier has an exact call/return graph");
    const auto proof=prove_post_layout_toggle_numeric_inputs(f.items,flow,all_queries(flow),f.preloads);
    require(!proof.exhausted && !proof.values.empty(),"stable fractional selector keeps numeric values");
    for(const auto& [state,value]:proof.values) {
      require(proof.carriers.at(state)=="b","numeric dependency identifies the physical carrier");
      require(value.size()==1U && (value.front()=="1.0000000e-1" || value.front()=="-1.0000000e-1"),
              "numeric phase values do not acquire a signed selector fill");
    }
    const auto lift=analyze_post_layout_automatic_entry_lift_with_indirect_store_facts(f.items,flow);
    require(lift.has_value(),"fractional selector entry mode converges");
    const auto ranges=prove_post_layout_bounded_stack_numeric_values(
        f.items,flow,*lift,f.observations,f.preloads,200000,
        NumericRangeInputPolicy::ValidatedRegisterTransport);
    require(!ranges.exhausted,"fractional carrier range proof converges");
    const bool initially_negative=std::string(seed).starts_with('-');
    for(std::size_t phase=0;phase<f.observations.size();++phase) {
      const bool negative=(phase==0)!=initially_negative;
      const auto found=ranges.x_before.find(f.observations[phase]);
      require(found!=ranges.x_before.end() &&
              found->second==OrdinaryNumericRange{negative?-1:0,negative?0:1,false},
              "numeric facts survive stable indirect calls in both signed phases");
    }
    const auto resolved=resolve_machine_items(f.items);
    require(resolved.diagnostics.empty(),"closed fractional fixture resolves");
    std::vector<int> bytes;for(const auto& step:resolved.steps)bytes.push_back(step.opcode);
    emulator::MK61 calc;
    require(calc.load_program(bytes).diagnostics.empty(),"fractional fixture fits stock ROM");
    calc.set_register("b",seed);
    calc.press_sequence({"В/О","С/П"});
    require(calc.run_until_stable(6000,8).stopped,"fractional selector calls return and stop");
    for(int phase=0;phase<2;++phase) {
      emulator::MK61 expected;
      expected.set_register("0",((phase==0)!=initially_negative)?"-1.0000000e-1":"1.0000000e-1");
      require(calc.read_register(std::to_string(phase))==expected.read_register("0"),
              "ROM agrees with both closed fractional phase values");
    }
    // Recall normalization must never certify the source register word.
    for(std::size_t state=0;state<flow.execution_states.size();++state)
      if(f.items[flow.execution_states[state].item_index].opcode==0x6b)
        require(!ranges.canonical_memory_load_states.contains(state),
                "numeric equality is not canonical source-memory shape");
  }
}

void nonpreserving_selector() {
  MachineEmitter e;
  e.emit_op(0x6b);e.emit_op(0x0b);e.emit_op(0x4b);
  const auto addressed=evaluate_indirect_address("b","-9.1000000e1",IndirectOperationKind::Memory);
  require(addressed && addressed->memory_target,"signed integer fixture has a real memory projection");
  e.emit_op(0xdb);e.items.back().indirect_memory_targets=std::vector<int>{*addressed->memory_target};
  e.emit_op(0x6b);e.emit_stop(StopDisposition::Terminal);
  const std::vector<PreloadReport> words{{.register_name="b",.value="91"}};
  const auto flow=build_post_layout_control_flow(e.items);
  require(flow.proved,"nonpreserving selector graph is complete");
  const auto numeric=prove_post_layout_toggle_numeric_inputs(e.items,flow,all_queries(flow),words);
  const auto small=prove_post_layout_toggle_sign_inputs(e.items,flow,all_queries(flow),words);
  require(!numeric.exhausted && numeric.values.empty() && small.values.empty(),
          "a signed integer selector writeback must invalidate numeric and small-integral phase facts");
  const auto resolved=resolve_machine_items(e.items);
  std::vector<int> bytes;for(const auto& step:resolved.steps)bytes.push_back(step.opcode);
  emulator::MK61 calc;require(calc.load_program(bytes).diagnostics.empty(),"signed selector fixture loads");
  calc.set_register("b","91");calc.press_sequence({"В/О","С/П"});
  require(calc.run_until_stable(6000,8).stopped,"signed selector fixture stops");
  emulator::MK61 expected;expected.set_register("b","-91");
  require(calc.read_register("b")!=expected.read_register("b"),"ROM confirms that negative selector writeback is not numeric negation");
}

void rejected_inputs() {
  const auto good=fixture("0.1");
  const auto closed=build_post_layout_control_flow(good.items,control());
  const auto queries=all_queries(closed);
  const auto exhausted=prove_post_layout_toggle_numeric_inputs(good.items,closed,queries,good.preloads,1);
  require(exhausted.exhausted && exhausted.values.empty() && exhausted.carriers.empty(),
          "exhaustion exports no partial numeric facts");
  auto raw=good;raw.items[1].raw=true;
  auto wrong=fixture("0.1",false,true);
  for(const auto& f:{raw,wrong}) {
    const auto flow=build_post_layout_control_flow(f.items,control());
    require(flow.proved,"negative fixture retains its complete CFG");
    const auto proof=prove_post_layout_toggle_numeric_inputs(f.items,flow,all_queries(flow),f.preloads);
    require(proof.values.empty(),"opaque commands and non-toggle writes reject a closed numeric orbit");
  }
  const auto ordinary=fixture("0.25",false);
  const auto ordinary_flow=build_post_layout_control_flow(ordinary.items,control());
  require(!prove_post_layout_toggle_numeric_inputs(ordinary.items,ordinary_flow,
      all_queries(ordinary_flow),ordinary.preloads).values.empty(),
      "the carrier proof is not restricted to a particular fractional coefficient");
}

void indirect_ordinary_type_closure() {
  for(int variant=0;variant<3;++variant) {
    MachineEmitter e;e.emit_label("repeat");
    e.emit_number("5");e.emit_op(0x43);
    e.emit_op(0x64);e.emit_op(0x6e);
    if(variant!=2)e.emit_op(0x15);
    e.emit_op(0x10);
    e.emit_op(0xb3);e.items.back().indirect_memory_targets=std::vector<int>{4};
    e.emit_op(0x64);e.emit_op(0x35);
    const auto query=e.items.size();e.emit_op(0x42);
    e.emit_op(0x69);e.emit_jump(0x57,"opaque repeat condition","repeat");
    e.emit_op(0x62);e.emit_stop(StopDisposition::Terminal);
    std::vector<PreloadReport> preloads;
    if(variant!=1)preloads.push_back({.register_name="4",.value="0.25"});
    const auto flow=build_post_layout_control_flow(e.items);
    require(flow.proved,"indirect recurrence has a complete CFG");
    const auto lift=analyze_post_layout_automatic_entry_lift_with_indirect_store_facts(e.items,flow);
    require(lift.has_value(),"indirect recurrence has a proved number-entry mode");
    const auto proof=prove_post_layout_bounded_stack_numeric_values(
        e.items,flow,*lift,{query},preloads,500000,NumericRangeInputPolicy::ValidatedRegisterTransport);
    require(!proof.exhausted,"indirect ordinary-type recurrence converges");
    const auto found=proof.x_before.find(query);
    require((found!=proof.x_before.end())==(variant==0),
            "only a grounded ordinary initializer and closed ordinary updates certify the recurrence");
    if(variant==0) {
      require(found->second.lower>=-1 && found->second.upper<=1,
              "the type invariant recovers only the signed fractional enclosure");
      const auto resolved=resolve_machine_items(e.items);
      std::vector<int> bytes;for(const auto& step:resolved.steps)bytes.push_back(step.opcode);
      for(const auto* exponent:{"-2","0","2"}) {
        emulator::MK61 calc;require(calc.load_program(bytes).diagnostics.empty(),"recurrence fits stock ROM");
        calc.set_register("4","0.25");calc.set_register("e",exponent);calc.set_register("9","1");
        calc.press_sequence({"В/О","С/П"});
        require(calc.run_until_stable(6000,8).stopped,"ordinary recurrence completes on stock ROM");
        const auto word=calc.read_register("2");
        require(word.find("ЕГГ")==std::string::npos && word.find("EГГ")==std::string::npos,
                "ordinary recurrence does not create an error word");
      }
    }
  }
}


void fractional_sign_projection() {
  for(const auto* value:{"-0.125","-0.0000001","0","0.0000001","0.125"}) {
    MachineEmitter e;e.emit_op(0x6b);e.emit_op(0x32);
    const auto query=e.items.size();e.emit_stop(StopDisposition::Terminal);
    const std::vector<PreloadReport> words{{.register_name="b",.value=value}};
    const auto flow=build_post_layout_control_flow(e.items);
    const auto lift=analyze_post_layout_automatic_entry_lift(e.items,flow);
    require(flow.proved && lift.has_value(),"fractional sign graph and entry mode");
    const auto proof=prove_post_layout_bounded_stack_numeric_values(e.items,flow,*lift,{query},words,
        100000,NumericRangeInputPolicy::ValidatedRegisterTransport);
    const int sign=std::string(value).starts_with('-')?-1:std::string(value)=="0"?0:1;
    require(!proof.exhausted && proof.x_before.contains(query) &&
            proof.x_before.at(query)==OrdinaryNumericRange{sign,sign,true},
            "strictly signed fractional lattices project to an exact sign without including zero");
    const auto resolved=resolve_machine_items(e.items);std::vector<int> bytes;
    for(const auto& step:resolved.steps)bytes.push_back(step.opcode);
    emulator::MK61 calc;require(calc.load_program(bytes).diagnostics.empty(),"fractional sign fits ROM");
    calc.set_register("b",value);calc.press_sequence({"В/О","С/П"});
    require(calc.run_until_stable(3000,6).stopped,"fractional sign stops");
    emulator::MK61 expected;expected.set_register("X",std::to_string(sign));
    require(calc.read_register("X")==expected.read_register("X"),"ROM confirms fractional sign, including zero");
  }
}

} // namespace
int main() {
  try {
    stable_fractional_carrier();nonpreserving_selector();rejected_inputs();indirect_ordinary_type_closure();fractional_sign_projection();
    std::cout<<"closed toggle numeric compiler and ROM contracts passed\n";return 0;
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
