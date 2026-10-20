#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/core/post_layout_progress_bounds.hpp"
#include "mkpro/core/post_layout_packed_sign.hpp"
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
  for (const int code : codes)
    items.push_back(MachineItem::op(code, "quantized range contract"));
}
void target(std::vector<MachineItem>& items, int code, const std::string& label) {
  append(items, {code});
  items.push_back(MachineItem::address(label));
}
void stop(std::vector<MachineItem>& items) {
  append(items, {0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
}
std::vector<int> bytecode(const std::vector<MachineItem>& items) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "quantized fixture must resolve");
  std::vector<int> codes;
  for (const auto& step : resolved.steps)
    codes.push_back(step.opcode);
  return codes;
}
PostLayoutNumericRangeProof ranges(
    const std::vector<MachineItem>& items, const std::vector<std::size_t>& requested,
    const std::vector<PreloadReport>& preloads = {}) {
  const auto flow = build_post_layout_control_flow(items);
  require(flow.proved, "quantized fixture needs an authoritative CFG");
  const auto lift = analyze_post_layout_automatic_entry_lift(items, flow, 100000);
  require(lift.has_value(), "quantized fixture needs entry-lift facts");
  return prove_post_layout_numeric_ranges(
      items, flow, *lift, requested, &preloads, 100000,
      NumericRangeInputPolicy::ValidatedRegisterTransport);
}
double number(std::string value) {
  std::replace(value.begin(), value.end(), ',', '.');
  std::istringstream stream(value);
  double mantissa = 0;
  int exponent = 0;
  require(static_cast<bool>(stream >> mantissa), "ROM must return an ordinary number: " + value);
  if (stream >> exponent)
    mantissa *= std::pow(10.0, exponent);
  return mantissa;
}
struct Fixture {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads{
      {.register_name="7", .value="3"},
      {.register_name="8", .value="0"},
      {.register_name="9", .value="0"}};
  std::size_t query = 0;
};
Fixture fixture(bool interactive, bool coefficient=false) {
  Fixture f; auto& v=f.items;
  append(v,{0x45});
  if(coefficient) {
    append(v,{1,0x42});
    for(int i=0;i<3;++i) append(v,{0x6b,0x62,1,0,0x13,0x12,0x4a});
  }
  v.push_back(MachineItem::label("body"));
  append(v,{0x69,0x68,0x38,0x49,0x11});
  target(v,0x5e,"changed");
  append(v,{0x67});
  f.query=v.size();
  if(coefficient) {
    append(v,{0x62,0x36,0x42,0x62,0x44,0x6b,0x6c,0x6d,0x6e,0x6b,0x10});
  }
  stop(v);
  v.push_back(MachineItem::label("changed"));
  append(v,{0x67,1,0x10,0x47});
  if(interactive){
    append(v,{0x50});v.back().stop_disposition=StopDisposition::Resumable;
    append(v,{0x48});
  }
  target(v,0x51,"body");
  return f;
}
int cells(const std::vector<MachineItem>& v){
  return static_cast<int>(std::count_if(v.begin(),v.end(),[](const auto& x){return x.kind!=MachineItemKind::Label;}));
}
MonotoneProgressProof progress(const Fixture& f, std::size_t budget=100000) {
  const auto control=build_post_layout_control_flow(f.items);
  require(control.proved,"progress fixture must have a complete CFG");
  const auto lift=analyze_post_layout_automatic_entry_lift(f.items,control,100000);
  require(lift.has_value(),"progress fixture needs lift facts");
  return prove_post_layout_monotone_progress(f.items,control,*lift,f.preloads,budget);
}
std::string word(const std::array<unsigned,7>& digits) {
  static const std::array<const char*,16> names{
    "0","1","2","3","4","5","6","7","8","9","-","L","\u0421","\u0413","\u0415","_"};
  std::string result="8.";
  for(const auto digit:digits)result+=names.at(digit);
  return result;
}
void native_rank_facts() {
  for(unsigned left=0;left<16;++left)
    for(unsigned right=0;right<16;++right) {
      std::array<unsigned,7> a{},b{},c{};
      bool grows=false;
      for(std::size_t i=0;i<7;++i) {
        a[i]=(left+static_cast<unsigned>(3*i))%16;
        b[i]=(right+static_cast<unsigned>(5*i))%16;
        c[i]=a[i]|b[i];grows=grows||c[i]!=a[i];
      }
      emulator::MK61 calc;
      require(calc.load_program({0x61,0x62,0x38,0x43,0x11,0x44,0x63,0x63,0x11,0x50}).diagnostics.empty(),
              "native OR rank fact must load");
      calc.set_register("1",word(a));calc.set_register("2",word(b));calc.set_register("5",word(c));
      calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});
      require(calc.run_until_stable(2000,8).stopped,"native OR rank fact must stop");
      require(calc.read_register("3")==calc.read_register("5"),
              "OR must retain every existing payload bit and canonicalize its result");
      require(number(calc.read_register("X"))==0,
              "subtracting identical canonical words must yield zero, including raw BCD digits");
      if(!grows)require(number(calc.read_register("4"))==0,
              "a nonzero retained-operand comparison must imply payload growth");
    }
}
void bounded_ranges_and_input() {
  for(bool interactive:{false,true}) {
    auto f=fixture(interactive);
    const auto p=progress(f);
    require(p.witness && p.witness->register_index==9 && p.witness->maximum_traversals==29,
            "OR feedback must yield its finite payload resource");
    const auto proof=ranges(f.items,{f.query},f.preloads);
    require(!proof.exhausted && proof.x_before.at(f.query)==OrdinaryNumericRange{3,32,true},
            "a bounded growth loop must preserve an ordinary accumulator range");
    auto again=progress(f);
    require(again.work==p.work && again.witness->source_state==p.witness->source_state &&
            again.witness->target_state==p.witness->target_state,
            "progress witnesses and work counts must be deterministic");
    require(progress(f,1).exhausted && !progress(f,1).witness,
            "resource analysis exhaustion must not export a witness");
    const auto control=build_post_layout_control_flow(f.items);
    const auto lift=analyze_post_layout_automatic_entry_lift(f.items,control,100000);
    require(!expand_post_layout_progress(f.items,control,*lift,*p.witness,1),
            "state-product exhaustion must fail closed");
  }

  auto f=fixture(true);
  emulator::MK61 calc;
  require(calc.load_program(bytecode(f.items)).diagnostics.empty(),"rank loop ROM load");
  for(const auto& p:f.preloads)calc.set_register(p.register_name,p.value);
  calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});
  require(calc.run_until_stable(2000,8).stopped && number(calc.read_register("X"))==4,
          "the first OR can canonicalize zero without setting a payload bit");
  int expected=4;
  for(std::size_t position=0;position<7;++position)
    for(unsigned bit=0;bit<4;++bit) {
      std::array<unsigned,7> digits{};digits[position]=1U<<bit;
      calc.input_number(word(digits));
      calc.press("\u0421/\u041f");
      require(calc.run_until_stable(2000,8).stopped &&
                  number(calc.read_register("X"))==++expected,
              "each new payload bit must fund exactly one guarded update");
    }
  calc.input_number("0");calc.press("\u0421/\u041f");
  require(calc.run_until_stable(2000,8).stopped && number(calc.read_register("X"))==32,
          "all 28 bits plus canonicalization must exhaust the proved 29-step resource");
}
void indirect_progress_contracts() {
  for(const int condition:{0x7e,0xee}) {
    Fixture f;auto& v=f.items;append(v,{0x45});
    v.push_back(MachineItem::label("body"));append(v,{0x69,0x68,0x38,0x49,0x11,0x43});
    const auto guard=v.size();append(v,{condition});
    v.back().indirect_flow_targets=std::vector<IrTarget>{condition==0x7e?"done":"changed"};
    const auto done=[&]{v.push_back(MachineItem::label("done"));append(v,{0x67});stop(v);};
    if(condition==0xee)done();
    v.push_back(MachineItem::label("changed"));append(v,{0x32,0x67,1,0x10,0x47});
    target(v,0x51,"body");if(condition==0x7e)done();
    int pc=0,target_pc=-1;
    for(const auto& item:v)if(item.kind==MachineItemKind::Label){
      if(item.name==(condition==0x7e?"done":"changed"))target_pc=pc;
    }else ++pc;
    require(target_pc>=0,"indirect progress target resolves");
    f.preloads.push_back({.register_name="e",.value=std::to_string(target_pc)});
    const auto witness=progress(f);
    require(witness.witness && witness.witness->register_index==9 &&
        witness.witness->edge_kind==(condition==0x7e?PostLayoutExecutionEdgeKind::Fallthrough:
            PostLayoutExecutionEdgeKind::IndirectTarget),"indirect predicates retain exact growth polarity");
    const auto flow=build_post_layout_control_flow(v);
    const auto lift=analyze_post_layout_automatic_entry_lift(v,flow,100000);
    require(lift.has_value(),"indirect progress entry facts");
    std::vector<std::size_t> queries;
    for(std::size_t s=0;s<flow.execution_states.size();++s)
      if(v[flow.execution_states[s].item_index].opcode==0x32)queries.push_back(s);
    const auto sign=prove_post_layout_packed_sign_inputs(v,flow,*lift,queries,&f.preloads);
    require(!queries.empty() && sign.signs.size()==queries.size() &&
        std::all_of(sign.signs.begin(),sign.signs.end(),[](const auto& row){return row.second==-1;}),
        "a transparent store and indirect guard retain the negative producer certificate");
    emulator::MK61 calc;require(calc.load_program(bytecode(v)).diagnostics.empty(),"indirect progress ROM load");
    for(const auto& word:f.preloads)calc.set_register(word.register_name,word.value);
    calc.press_sequence({"В/О","С/П"});
    require(calc.run_until_stable(2000,8).stopped && number(calc.read_register("X"))==4,
        "ROM executes exactly one canonicalizing growth and takes the zero exit");
    auto aliased=f;aliased.items[guard].opcode=(condition&0xf0)+9;
    require(!progress(aliased).witness,"a progress carrier cannot be its own flow selector");
    auto wrong=f;wrong.items[guard].opcode=0x9e;
    require(!progress(wrong).witness,"a sign predicate cannot borrow an equality growth theorem");
    auto reversed=f;
    reversed.items.insert(reversed.items.begin()+static_cast<std::ptrdiff_t>(guard-2U),MachineItem::op(0x14,"reverse comparison"));
    const auto reverse_flow=build_post_layout_control_flow(reversed.items);
    const auto reverse_lift=analyze_post_layout_automatic_entry_lift(reversed.items,reverse_flow,100000);
    require(reverse_lift.has_value(),"reversed comparison entry facts");
    std::vector<std::size_t> reverse_queries;
    for(std::size_t s=0;s<reverse_flow.execution_states.size();++s)
      if(reversed.items[reverse_flow.execution_states[s].item_index].opcode==0x32)reverse_queries.push_back(s);
    require(prove_post_layout_packed_sign_inputs(reversed.items,reverse_flow,*reverse_lift,reverse_queries,&reversed.preloads).signs.empty(),
        "new-minus-old cannot inherit an old-minus-new negative certificate");
    const auto exhausted=progress(f,1);
    require(exhausted.exhausted && !exhausted.witness,"indirect progress exhaustion exports no partial witness");
  }
}

void negative_witnesses() {
  for(int variant=0;variant<8;++variant) {
    auto f=fixture(false);
    if(variant==0) {
      for(auto& i:f.items)if(i.kind==MachineItemKind::Op && i.opcode==0x38)i.opcode=0x39;
    } else if(variant==1) {
      f.preloads.pop_back();
    } else if(variant==2) {
      f.preloads.push_back({.register_name="9",.value="-0"});
    } else if(variant==3) {
      f.preloads.back().value="A-2";
    } else if(variant==4) {
      auto at=std::find_if(f.items.begin(),f.items.end(),[](const auto& i){
        return i.kind==MachineItemKind::Label && i.name=="changed";});
      f.items.insert(at+1,{MachineItem::op(0,"reset"),MachineItem::op(0x49,"reset")});
    } else if(variant==5) {
      auto at=std::find_if(f.items.begin(),f.items.end(),[](const auto& i){
        return i.kind==MachineItemKind::Label && i.name=="changed";});
      auto alias=MachineItem::op(0xb6,"possible alias");
      alias.indirect_memory_targets=std::vector<int>{7,9};
      f.items.insert(at+1,alias);
    } else if(variant==6) {
      auto at=std::find_if(f.items.begin(),f.items.end(),[](const auto& i){
        return i.kind==MachineItemKind::Op && i.opcode==0x5e;});
      f.items.insert(at,MachineItem::label("reused"));
      f.items.back().target=std::string("reused");
    } else {
      f.items[0].raw=true;
    }
    require(!progress(f).witness,"reset, stale guard, alias or unsupported input must reject rank proof: "+
            std::to_string(variant));
  }

  // Both outcomes have the same physical successor. Only the nonzero edge
  // spends the resource; the zero edge may execute arbitrary many updates.
  auto f=fixture(false);
  const auto branch=std::find_if(f.items.begin(),f.items.end(),[](const auto& i){
    return i.kind==MachineItemKind::Op && i.opcode==0x5e;});
  const auto changed=std::find_if(f.items.begin(),f.items.end(),[](const auto& i){
    return i.kind==MachineItemKind::Label && i.name=="changed";});
  f.items.erase(branch+2,changed);
  f.query=f.items.size()-2;
  const auto p=progress(f);
  require(p.witness.has_value(),"coincident branch destinations still have a valid growth edge");
  require(ranges(f.items,{f.query},f.preloads).x_before.empty(),
          "the non-consuming alternative must not inherit the growth edge budget");

  auto inverted=fixture(false);
  auto b=std::find_if(inverted.items.begin(),inverted.items.end(),[](const auto& i){
    return i.kind==MachineItemKind::Op && i.opcode==0x5e;});
  const auto index=static_cast<std::size_t>(b-inverted.items.begin());
  b->opcode=0x57;
  inverted.items[index+1].target=std::string("unchanged");
  std::vector<MachineItem> jump{MachineItem::op(0x51,"inverted growth"),
                               MachineItem::address("changed"),MachineItem::label("unchanged")};
  inverted.items.insert(inverted.items.begin()+static_cast<std::ptrdiff_t>(index+2),jump.begin(),jump.end());
  inverted.query+=jump.size();
  const auto inv=progress(inverted);
  require(inv.witness && inv.witness->edge_kind==PostLayoutExecutionEdgeKind::Fallthrough &&
              ranges(inverted.items,{inverted.query},inverted.preloads).x_before.at(inverted.query)==
                  OrdinaryNumericRange{3,32,true},
          "inverted zero tests must use the fallthrough growth edge");
}
std::vector<std::string> observations(
    const std::vector<MachineItem>& items,const std::vector<PreloadReport>& preloads,
    const char* mask) {
  emulator::MK61 calc;
  require(calc.load_program(bytecode(items)).diagnostics.empty(),"composed rank ROM load");
  constexpr std::array<const char*,15> names{
    "0","1","2","3","4","5","6","7","8","9","a","b","c","d","e"};
  for(std::size_t i=0;i<names.size();++i)calc.set_register(names[i],std::to_string(i+2U));
  for(const auto& p:preloads)calc.set_register(p.register_name,p.value);
  calc.set_register("8",mask);
  calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});
  require(calc.run_until_stable(15000,8).stopped,"composed rank fixture must stop");
  std::vector<std::string> values{calc.display_text(true)};
  calc.press(".");values.push_back(calc.display_text(true));
  for(const char* name:{"X","Y","Z","T","X1"})values.push_back(calc.read_register(name));
  for(const char* name:names)values.push_back(calc.read_register(name));
  return values;
}
bool mixed(const passes::PassResult& r) {
  return std::any_of(r.optimizations.begin(),r.optimizations.end(),[](const auto& o){
    return o.name=="mixed-epoch-coefficient-state-carrier";});
}
void compiler_composition_and_size() {
  CompileOptions options;options.defer_return_suffix_until_callee_hole=true;
  for(int carrier:{2,6}) {
    auto f=fixture(false,true);
    for(auto& i:f.items) {
      if(i.kind==MachineItemKind::Op && i.opcode==0x42)i.opcode=0x40+carrier;
      else if(i.kind==MachineItemKind::Op && i.opcode==0x62)i.opcode=0x60+carrier;
    }
    const auto cs=passes::finite_mode_representation_candidates(raise_machine_to_ir(f.items),{options},
        {.maximum_candidates=32,.runtime_preloads=&f.preloads});
    const auto found=std::find_if(cs.begin(),cs.end(),mixed);
    require(found!=cs.end(),"rank bounds must enable the independent coefficient-carrier proof");
    const auto after=lower_ir_to_machine(found->ops);
    require(cells(f.items)==50 && cells(after)==43 && found->preloads.empty(),
            "composed finite-resource baseline must shrink 50 to 43 without additional setup");
    require(build_post_layout_control_flow(after).proved,"composed rank final CFG must remain valid");
    for(const char* mask:{"0","8.0000001","8.8888888","-13"})
      require(observations(f.items,f.preloads,mask)==observations(after,f.preloads,mask),
              "rank-enabled recoding must preserve all registers, X1, stack and decimal entry");
    f.preloads.front().value="0";
    const auto unsafe=passes::finite_mode_representation_candidates(raise_machine_to_ir(f.items),{options},
        {.maximum_candidates=32,.runtime_preloads=&f.preloads});
    require(std::none_of(unsafe.begin(),unsafe.end(),mixed),
            "a bounded loop must not imply a positive lower bound it does not prove");
  }
  auto f=fixture(false,true);
  const LayoutSearchArtifact root{.items=f.items,.preloads=f.preloads};
  const auto incomplete=expand_joint_representation_transactions(root,{},false);
  require(std::none_of(incomplete.begin(),incomplete.end(),[](const auto& edge){
    return std::any_of(edge.node.optimizations.begin(),edge.node.optimizations.end(),[](const auto& o){
      return o.name=="mixed-epoch-coefficient-state-carrier";});}),
      "an incomplete preload environment must not borrow a rank certificate");
  const auto edges=expand_joint_representation_transactions(root,{},true);
  require(std::any_of(edges.begin(),edges.end(),[](const auto& edge){
    return cells(edge.node.items)==43 &&
       std::any_of(edge.node.optimizations.begin(),edge.node.optimizations.end(),[](const auto& o){
         return o.name=="mixed-epoch-coefficient-state-carrier";});}),
      "the joint search must combine finite-resource analysis with representation changes");
}
void matched_call_contexts() {
  const auto inline_form=fixture(false,true);
  Fixture called;
  const auto body=std::find_if(inline_form.items.begin(),inline_form.items.end(),[](const auto& i){
    return i.kind==MachineItemKind::Label && i.name=="body";});
  const auto end=std::find_if(inline_form.items.begin()+static_cast<std::ptrdiff_t>(inline_form.query),
      inline_form.items.end(),[](const auto& i){return i.kind==MachineItemKind::Op && i.opcode==0x50;})+1;
  called.items.assign(inline_form.items.begin(),body);
  target(called.items,0x53,"body");
  called.query=called.items.size();
  called.items.insert(called.items.end(),
      inline_form.items.begin()+static_cast<std::ptrdiff_t>(inline_form.query),end);
  called.items.insert(called.items.end(),body,
      inline_form.items.begin()+static_cast<std::ptrdiff_t>(inline_form.query));
  append(called.items,{0x52});
  called.items.insert(called.items.end(),end,inline_form.items.end());
  require(ranges(called.items,{called.query},called.preloads).x_before.at(called.query)==
              OrdinaryNumericRange{3,32,true},
          "a bounded callee must return its range through the matched caller frame");
  CompileOptions options;options.defer_return_suffix_until_callee_hole=true;
  const auto cs=passes::finite_mode_representation_candidates(raise_machine_to_ir(called.items),{options},
      {.maximum_candidates=32,.runtime_preloads=&called.preloads});
  const auto found=std::find_if(cs.begin(),cs.end(),mixed);
  require(found!=cs.end(),"a bounded callee must enable the same carrier representation");
  const auto after=lower_ir_to_machine(found->ops);
  require(cells(called.items)==53 && cells(after)==46,
          "matched-return composition must retain the seven-cell saving");
  require(observations(called.items,called.preloads,"8.0000001")==
              observations(after,called.preloads,"8.0000001"),
          "resource expansion must preserve matched returns and all final machine observations");
}
}
int main() {
  try {
    native_rank_facts();
    bounded_ranges_and_input();
    indirect_progress_contracts();
    negative_witnesses();
    compiler_composition_and_size();
    matched_call_contexts();
    std::cout<<"finite-resource range compiler and ROM contracts passed\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
