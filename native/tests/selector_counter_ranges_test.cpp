#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;
void append(std::vector<MachineItem>& items, std::initializer_list<int> codes) {
  for (int code : codes) items.push_back(MachineItem::op(code,"counter range fixture"));
}
void stop(std::vector<MachineItem>& items) {
  append(items,{0x50});items.back().stop_disposition=StopDisposition::Terminal;
}
struct Fixture {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
  std::size_t query=0;
};
PostLayoutNumericRangeProof ranges(const Fixture& f) {
  const auto flow=build_post_layout_control_flow(f.items);
  require(flow.proved,"counter fixture CFG");
  const auto lift=analyze_post_layout_automatic_entry_lift(f.items,flow,100000);
  require(lift.has_value(),"counter fixture lift");
  return prove_post_layout_numeric_ranges(f.items,flow,*lift,{f.query},&f.preloads,
      100000,NumericRangeInputPolicy::ValidatedRegisterTransport);
}
void memory(std::vector<MachineItem>& items,int opcode,std::vector<int> targets) {
  append(items,{opcode});items.back().indirect_memory_targets=std::move(targets);
}
Fixture single(int reg,const std::string& value,bool store) {
  Fixture f;
  f.preloads.push_back({.register_name=std::to_string(reg),.value=value});
  const auto address=evaluate_indirect_address(std::to_string(reg),value,IndirectOperationKind::Memory);
  append(f.items,{0x0d,3});
  memory(f.items,(store?0xb0:0xd0)+reg,
         {address && address->memory_target ? *address->memory_target : 7});
  append(f.items,{0x60+reg});f.query=f.items.size();stop(f.items);
  return f;
}
std::vector<int> bytecode(const std::vector<MachineItem>& items) {
  const auto resolved=resolve_machine_items(items);
  require(resolved.diagnostics.empty(),"counter fixture resolves");
  std::vector<int> codes;for(const auto& step:resolved.steps)codes.push_back(step.opcode);
  return codes;
}
double number(std::string value) {
  std::replace(value.begin(),value.end(),',','.');
  std::istringstream input(value);double mantissa=0;int exponent=0;
  require(static_cast<bool>(input>>mantissa),"ordinary ROM observation");
  if(input>>exponent)mantissa*=std::pow(10.,exponent);
  return mantissa;
}
double execute(const Fixture& f) {
  emulator::MK61 calc;
  require(calc.load_program(bytecode(f.items)).diagnostics.empty(),"counter ROM load");
  for(int r=0;r<15;++r)calc.set_register(std::string(1,"0123456789abcde"[r]),"27");
  for(const auto& p:f.preloads)calc.set_register(p.register_name,p.value);
  calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});
  require(calc.run_until_stable(2000,8).stopped,"counter ROM stop");
  return number(calc.read_register("X"));
}
void exact_writeback_and_aliases() {
  for(int r=0;r<=6;++r)for(bool store:{false,true})
    for(const std::string value:{"1","9","19","1E1","99999998"}) {
      const auto f=single(r,value,store);
      const auto address=evaluate_indirect_address(std::to_string(r),value,IndirectOperationKind::Memory);
      require(address && address->result_value,"integer model writeback");
      const auto expected=store && address->memory_target==r
          ? OrdinaryNumericRange{3,3,true} : *bound_ordinary_decimal(*address->result_value);
      require(ranges(f).x_before.at(f.query)==expected,"integer pointer must retain its exact updated range");
      require(execute(f)==static_cast<double>(expected.lower),"counter range must agree with ROM");
    }
  auto resumed=single(0,"8",true);
  auto prompt=MachineItem::op(0x50,"input between pointer setup and access");
  prompt.stop_disposition=StopDisposition::Resumable;
  resumed.items.insert(resumed.items.begin()+2,prompt);++resumed.query;
  const auto first=ranges(resumed),again=ranges(resumed);
  require(first.x_before.at(resumed.query)==OrdinaryNumericRange{7,7,true} &&
              first.x_before==again.x_before && first.work==again.work,
          "resumed input must preserve the pointer range deterministically, not its X argument");
  const auto aliased=single(0,"1",true);
  require(ranges(aliased).x_before.at(aliased.query)==OrdinaryNumericRange{3,3,true} &&
              execute(aliased)==3,"self-targeting store overwrites the decremented counter");

  auto possible=single(0,"8",true);
  possible.items[2].indirect_memory_targets=std::vector<int>{0,7};
  require(ranges(possible).x_before.at(possible.query)==OrdinaryNumericRange{3,7,true},
          "possible self-alias joins the written X and pointer writeback");
  possible.items.erase(possible.items.begin(),possible.items.begin()+2);possible.query-=2;
  require(!ranges(possible).x_before.contains(possible.query),
          "possible unknown self-store must destroy the counter bound");
}
void unsupported_words_and_flows() {
  for(const std::string value:{"0","-0","-8","8.25","99999999","0x8","0xA","10E-1","10000000E-7"}) {
    const auto f=single(0,value,false);
    require(!ranges(f).x_before.contains(f.query),"unsupported selector word must stay unknown: "+value);
  }
  for(int opcode:{0x80,0x70,0x90,0xc0,0xe0}) {
    Fixture f;f.preloads.push_back({.register_name="0",.value="5"});
    append(f.items,{opcode});f.items.back().indirect_flow_targets=std::vector<IrTarget>{4};
    append(f.items,{0x54,0x54,0x54,0x60});f.query=f.items.size();stop(f.items);
    require(!ranges(f).x_before.contains(f.query),"unproved flow mutation must not inherit memory semantics");
  }
}
void interval_join_and_nested_calls() {
  Fixture f;
  append(f.items,{0x5e});f.items.push_back(MachineItem::address("right"));
  append(f.items,{0x0d,9,0x40,0x51});f.items.push_back(MachineItem::address("join"));
  f.items.push_back(MachineItem::label("right"));append(f.items,{0x0d,1,1,0x40});
  f.items.push_back(MachineItem::label("join"));memory(f.items,0xd0,{8,10});
  append(f.items,{0x60});f.query=f.items.size();stop(f.items);
  require(ranges(f).x_before.at(f.query)==OrdinaryNumericRange{8,10,true},
          "joined integer counter intervals must transfer monotonically");

  Fixture called;called.preloads.push_back({.register_name="0",.value="8"});
  append(called.items,{0x0d,3});
  for(int i=0;i<4;++i){append(called.items,{0x53});called.items.push_back(MachineItem::address("leaf"));}
  append(called.items,{0x60});called.query=called.items.size();stop(called.items);
  called.items.push_back(MachineItem::label("leaf"));memory(called.items,0xb0,{4,5,6,7});
  append(called.items,{0x52});
  require(ranges(called).x_before.at(called.query)==OrdinaryNumericRange{4,4,true} &&
              execute(called)==4,"matched caller frames must preserve all four decrements");
}
int cells(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(),items.end(),[](const auto& i){
    return i.kind!=MachineItemKind::Label;}));
}
std::vector<std::string> observations(const Fixture& f) {
  emulator::MK61 calc;
  require(calc.load_program(bytecode(f.items)).diagnostics.empty(),"composed counter ROM load");
  for(int r=0;r<15;++r)calc.set_register(std::string(1,"0123456789abcde"[r]),std::to_string(r+2));
  for(const auto& p:f.preloads)calc.set_register(p.register_name,p.value);
  calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});
  require(calc.run_until_stable(5000,8).stopped,"composed counter ROM stop");
  std::vector<std::string> result{calc.display_text(true)};
  calc.press(".");result.push_back(calc.display_text(true));
  for(const char* name:{"X","Y","Z","T","X1"})result.push_back(calc.read_register(name));
  for(int r=0;r<15;++r)result.push_back(calc.read_register(std::string(1,"0123456789abcde"[r])));
  return result;
}
void representation_composition() {
  for(int carrier:{2,3}) {
    Fixture f;f.preloads.push_back({.register_name="0",.value="8"});
    append(f.items,{0x45,1,0x40+carrier});
    for(int repeat=0;repeat<3;++repeat)append(f.items,{0x6b,0x60+carrier,1,0,0x13,0x12,0x4a});
    for(int target=7;target>=4;--target)memory(f.items,0xd0,{target});
    append(f.items,{0x60});f.query=f.items.size();
    append(f.items,{0x60+carrier,0x36,0x40+carrier,0x60+carrier,0x44,
                   0x6b,0x6c,0x6d,0x6e,0x6b,0x10});stop(f.items);
    CompileOptions options;options.defer_return_suffix_until_callee_hole=true;
    const auto candidates=passes::finite_mode_representation_candidates(raise_machine_to_ir(f.items),
       {options},{.maximum_candidates=32,.runtime_preloads=&f.preloads});
    const auto selected=std::find_if(candidates.begin(),candidates.end(),[](const auto& c){
      return std::any_of(c.optimizations.begin(),c.optimizations.end(),[](const auto& report){
        return report.name=="mixed-epoch-coefficient-state-carrier";});});
    require(selected!=candidates.end(),"counter bounds must compose with coefficient-state recoding");
    Fixture after=f;after.items=lower_ir_to_machine(selected->ops);
    std::cout<<"counter carrier "<<carrier<<": "<<cells(f.items)<<" -> "<<cells(after.items)<<'\n';
    require(cells(f.items)==41 && cells(after.items)==34,
            "counter/coefficient composition size baseline must stay 41 to 34");
    const LayoutSearchArtifact root{.items=f.items,.preloads=f.preloads};
    const auto complete=expand_joint_representation_transactions(root,{},true);
    require(std::any_of(complete.begin(),complete.end(),[](const auto& edge) {
      return cells(edge.node.items)==34;
    }),"the complete frontier must consume the new counter certificate");
    const auto incomplete=expand_joint_representation_transactions(root,{},false);
    require(std::none_of(incomplete.begin(),incomplete.end(),[](const auto& edge) {
      return std::any_of(edge.node.optimizations.begin(),edge.node.optimizations.end(),[](const auto& o) {
        return o.name=="mixed-epoch-coefficient-state-carrier";
      });
    }),"an incomplete preload environment cannot borrow counter bounds");
    require(observations(f)==observations(after),"composed rewrite must preserve registers, stack, X1 and decimal-entry X2");
  }
}
}
int main() {
  try {
    exact_writeback_and_aliases();
    unsupported_words_and_flows();
    interval_join_and_nested_calls();
    representation_composition();
    std::cout<<"selector counter range compiler/ROM contracts passed\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

