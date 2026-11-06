#include "mkpro/compiler.hpp"
#include "mkpro/core/compiler_static_proof_gate.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/passes/contextual_copy_transport.hpp"
#include "mkpro/core/passes/selector_charge_literal_sinking.hpp"
#include "mkpro/core/passes/zero_underflow_constant.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mkpro {
std::optional<CompileOptions> completed_optimizer_options_for_testing(const CompileResult& result);
bool refresh_proved_layout_annotations_for_testing(CompileResult& result,const CompileOptions& options);
}

using namespace mkpro;
using mkpro::tests::require;
namespace {
std::string compact(std::string value) {
  value.erase(std::remove_if(value.begin(),value.end(),[](unsigned char c){return std::isspace(c)!=0;}),value.end());
  std::replace(value.begin(),value.end(),',','.');
  return value;
}
std::vector<MachineItem> zero_entry_fixture() {
  std::vector<MachineItem> items;
  for(int i=0;i<28;++i)items.push_back(MachineItem::op(0x54,"padding"));
  items[0]=MachineItem::op(0x52,"return");
  items[1]=MachineItem::op(0x60,"recall");items[2]=MachineItem::op(0x60,"recall");
  items[3]=MachineItem::op(0x11,"subtract");items[4]=MachineItem::op(0x43,"store");
  items[5]=MachineItem::op(0x73,"zero edge");items[5].indirect_flow_targets=std::vector<IrTarget>{"entry"};
  items[6]=MachineItem::op(0x50,"stop");items[6].stop_disposition=StopDisposition::Terminal;
  items[20]=MachineItem::op(0x4a,"selector");
  items[20].roles={"selector-sunk-literal-id:test","selector-sunk-literal-value:1"};
  items[21]=MachineItem::op(1,"literal");
  for(int i=0;i<4;++i)items[static_cast<std::size_t>(22+i)]=MachineItem::op(0x61+i,"recall");
  items[26]=MachineItem::op(0x10,"add");items[27]=MachineItem::op(0x50,"stop");
  items[27].stop_disposition=StopDisposition::Terminal;
  items.insert(items.begin()+20,MachineItem::label("entry"));
  return items;
}
void zero_entry_proof() {
  const auto original=zero_entry_fixture();
  core::PostLayoutControlFlowOptions options;options.empty_return_target=1;
  const auto prove=[&](const std::vector<MachineItem>& items,bool final=false){
    return core::passes::prove_selector_zero_flow_sunk_literal_entry(items,
        core::build_post_layout_control_flow(items,options),21,5,final);
  };
  require(prove(original),"normalized equality edge must admit a deferred zero selector entry");
  require(!prove(original,true),"unplaced zero entry must not claim final address 99");
  auto changed=original;changed[3]=MachineItem::op(0x60,"unknown recall");
  require(!prove(changed),"a recalled or typed zero is not a canonical zero word proof");
  changed=original;changed[4]=MachineItem::op(0x42,"wrong selector store");
  require(!prove(changed),"zero X alone does not initialize the branch selector");
  changed=original;changed[5].opcode=0xe3;
  require(!prove(changed),"nonzero branch cannot inherit the zero-edge certificate");
  changed=original;changed[3].raw=true;
  require(!prove(changed),"opaque subtraction cannot supply normalization facts");
  changed=original;changed[26]=MachineItem::op(0x0f,"observe physical last-X");
  require(!prove(changed),"an observed displaced last-X must reject literal erasure");
}
void final_null_entry_proof() {
  auto items=zero_entry_fixture();
  items.insert(items.begin()+20,79,MachineItem::op(0x54,"padding"));
  auto call=MachineItem::op(0xaa,"null callback");
  call.indirect_flow_targets=std::vector<IrTarget>{"null-return"};
  items.insert(items.begin()+103,std::move(call));
  items.insert(items.begin(),MachineItem::label("null-return"));
  core::PostLayoutControlFlowOptions control;control.empty_return_target=1;
  const auto prove=[&](const std::vector<MachineItem>& candidate){
    return core::passes::prove_selector_zero_flow_null_callback_entry(candidate,
        core::build_post_layout_control_flow(candidate,control),101,6);
  };
  require(prove(items),"final semantic-zero entry must bind physical 99 and the actual null return at 00");
  auto changed=items;changed[1].raw=true;
  require(!prove(changed),"a raw command at 00 cannot certify the null-return ABI");
  changed=items;changed[1].opcode=0x54;
  require(!prove(changed),"a descriptive null-return label cannot replace the actual return command");
  changed=items;changed[4].opcode=0x60;
  require(!prove(changed),"typed or recalled zero must not inherit the normalized subtraction certificate");
  changed=items;changed.insert(changed.begin()+100,MachineItem::op(0x54,"stale placement"));
  require(!prove(changed),"moving the entry off physical 99 must invalidate its final certificate");
}
void ordinary_leaf_alias_annotations() {
  CompileResult result;result.implemented=true;
  result.items={MachineItem::op(0x52,"return"),MachineItem::op(0xaa,"call"),
      MachineItem::op(0x50,"stop"),MachineItem::label("first"),MachineItem::label("alias"),
      MachineItem::op(0x52,"first return"),MachineItem::label("second"),MachineItem::op(0x52,"second return")};
  result.items[1].comment="callee-hole indirect call; proof=fixture; leaf-targets=3:first,4:second";
  result.items[1].indirect_flow_targets=std::vector<IrTarget>{"alias","second"};
  result.items[2].stop_disposition=StopDisposition::Terminal;
  CompileOptions options;options.analysis=true;options.budget=999999;
  require(refresh_proved_layout_annotations_for_testing(result,options),
      "ordinary typed entry aliases must retain the historical annotation path");
  require(result.items[1].comment->find("leaf-targets=3:first,4:second")!=std::string::npos,
      "an ordinary label alias is not a new dispatch mode: "+result.items[1].comment.value_or("<missing>"));
  result.items.insert(result.items.begin(),MachineItem::label("null-alias"));
  result.items[2].indirect_flow_targets->push_back("null-alias");
  require(refresh_proved_layout_annotations_for_testing(result,options),
      "a conservative zero target without a semantic-zero entry must not perturb old annotations");
  require(result.items[2].comment->find("0:null-alias")==std::string::npos,
      "a typed target and descriptive label alone must not invent a proved null charge");
}
void register_domain_queries() {
  std::vector<MachineItem> items{MachineItem::op(0x61,"recall"),MachineItem::op(0x43,"store"),MachineItem::op(0x50,"stop")};
  items.back().stop_disposition=StopDisposition::Terminal;
  const std::vector<PreloadReport> preloads{{.register_name="1",.value="2.75"},{.register_name="2",.value="0x1E3"}};
  const auto flow=core::build_post_layout_control_flow(items);
  const auto lift=core::analyze_post_layout_automatic_entry_lift_with_indirect_store_facts(items,flow,100000);
  require(lift.has_value(),"register query fixture must have a complete entry-mode proof");
  const std::map<std::size_t,std::vector<int>> query{{1,{1,2,3}}};
  const auto proof=core::prove_post_layout_register_numeric_values(items,flow,*lift,query,&preloads);
  require(!proof.exhausted && !proof.values.empty(),"fractional register domain must be derived on the unchanged CFG");
  for(const auto& [state,regs]:proof.values){
    (void)state;
    require(regs.at(1).ordinary && regs.at(1).range && regs.at(1).range->lower==2 && regs.at(1).range->upper==3 && !regs.at(1).range->integral,
        "fractional value must not acquire an integral or canonical-word fact");
    require(!regs.at(2).ordinary,"a raw BCD word must not become an ordinary number");
    require(!regs.at(3).ordinary,"the query is before the store, not after it");
  }
  const auto exhausted=core::prove_post_layout_register_numeric_values(items,flow,*lift,query,&preloads,0);
  require(exhausted.exhausted && exhausted.values.empty(),"exhaustion must export no partial register facts");
  const auto invalid=core::prove_post_layout_register_numeric_values(items,flow,*lift,{{1,{15}}},&preloads);
  require(invalid.values.empty(),"stock register facts must never invent Rf");
}
void canonical_zero_rom() {
  for(const auto& seed:std::vector<std::string>{"0","-0","3.25","-6.5","9.9999999E98"}){
    std::vector<int> bytes(105,0x54);
    bytes[0]=0x52;bytes[1]=0x60;bytes[2]=0x60;bytes[3]=0x11;bytes[4]=0x43;bytes[5]=0x73;
    bytes[6]=9;bytes[7]=0x50;bytes[99]=0x4a;bytes[100]=0x50;
    emulator::MK61 machine;
    require(machine.load_program(bytes).diagnostics.empty(),"zero-edge ROM fixture must fit stock memory");
    machine.set_register("0",seed).press_sequence({"В/О","С/П"});
    // The calculator renders hexadecimal A as a dash: "-1" is counter A1.
    require(machine.run_until_stable(5000,6).stopped && machine.program_counter()=="-1",
        "normalized subtraction zero must select exactly physical 99: seed="+seed+
        " pc="+machine.program_counter()+" display="+machine.display_text());
    require(std::stod(compact(machine.read_register("a")))==0.0,"zero edge must deliver positive numeric zero to its callback store");
    require(std::stod(compact(machine.read_register("3")))==-99999999.0,"low selector underflow must preserve its sentinel write-back");
  }
}
std::vector<std::string> null_callback_rom(bool completion) {
  std::vector<int> bytes(24,0x54);
  bytes[0]=0x52;bytes[1]=5;bytes[2]=0x43;bytes[3]=0x0d;bytes[4]=0x4a;
  bytes[5]=0x53;bytes[6]=0x20;bytes[7]=0x63;bytes[8]=0x50;
  bytes[20]=completion?0xaa:0x52;bytes[21]=0xaa;bytes[22]=0x8a;
  emulator::MK61 machine;
  require(machine.load_program(bytes).diagnostics.empty(),"null callback ROM fixture must fit stock memory");
  for(const auto* reg:{"X","Y","Z","T","X1"})machine.set_register(reg,"-3.25");
  machine.press_sequence({"В/О","С/П"});
  require(machine.run_until_stable(5000,6).stopped && machine.program_counter()=="09",
      "two null calls and a null tail call must retain the exact outer return frame");
  std::vector<std::string> result;
  for(const auto* reg:{"X","Y","Z","T","X1","3"})result.push_back(machine.read_register(reg));
  machine.press("7");result.push_back(machine.display_text());
  return result;
}

std::vector<MachineItem> memory_abi_fixture() {
  std::vector<MachineItem> items;
  const auto op=[&](int code){items.push_back(MachineItem::op(code,"fixture"));};
  const auto label=[&](const std::string& name){items.push_back(MachineItem::label(name));};
  const auto call=[&](const std::string& name){op(0x53);items.push_back(MachineItem::address(name));};
  call("outer");op(0x43);op(2);op(0x41);op(0x61);call("body");op(0x44);op(0x63);op(0x64);op(0x50);
  items.back().stop_disposition=StopDisposition::Terminal;
  label("outer");call("producer");
  label("body");op(0x69);op(0x14);op(0x41);op(2);op(0x12);op(0x10);op(0x46);
  op(0x0d);op(0x0e);op(0x0e);op(0x0e);op(0x66);op(0x52);
  label("producer");op(0x62);op(0x52);
  return items;
}
std::vector<std::string> run_abi(const std::vector<MachineItem>& items,const std::vector<PreloadReport>& preloads) {
  const auto image=resolve_machine_items(items);require(image.diagnostics.empty(),"memory ABI fixture must resolve");
  std::vector<int> bytes;for(const auto& step:image.steps)bytes.push_back(step.opcode);
  emulator::MK61 machine;require(machine.load_program(bytes).diagnostics.empty(),"memory ABI fixture must fit stock ROM");
  for(const auto* reg:{"X","Y","Z","T","X1"})machine.set_register(reg,"-1.25");
  for(const auto& word:preloads)machine.set_register(word.register_name,word.value);
  machine.press_sequence({"В/О","С/П"});require(machine.run_until_stable(5000,6).stopped,"memory ABI fixture must return and stop");
  std::vector<std::string> state;
  for(const auto* reg:{"X","Y","Z","T","X1","1","2","3","4","6","9"})state.push_back(machine.read_register(reg));
  const auto terminal=std::find(bytes.begin(),bytes.end(),0x50);
  require(terminal!=bytes.end() && machine.program_counter()==format_address(
      static_cast<int>(terminal-bytes.begin())+1),"each layout must return to its own same terminal instruction");
  machine.press("7");state.push_back(machine.display_text());return state;
}
void memory_abi_proof_and_rom() {
  const auto original=memory_abi_fixture();
  const std::vector<PreloadReport> words{{.register_name="1",.value="2"},{.register_name="2",.value="3.25"},{.register_name="9",.value="0.5"}};
  core::PostLayoutControlFlowOptions control;control.empty_return_target=1;
  CompileOptions options;const core::passes::PassContext context{options};
  const auto candidates=[&](const std::vector<MachineItem>& items){
    std::map<std::string,int> origins;int address=0;
    for(const auto& item:items){if(item.kind==MachineItemKind::Label)origins.emplace(item.name,address);else ++address;}
    return core::passes::contextual_memory_backed_entry_candidates(raise_machine_to_ir(items),context,
        {.maximum_work=200000,.maximum_candidates=2,.maximum_copy_sites=8,.maximum_growth_cells=4,
         .control=&control,.encoded_target_origins=&origins,.runtime_preloads=&words});
  };
  const auto valid=candidates(original);require(valid.size()==1U,"memory ABI must compose one canonical caller with a physical return entry");
  const auto rewritten=lower_ir_to_machine(valid.front().transformation.ops);
  require(resolve_machine_items(rewritten).steps.size()+1U==resolve_machine_items(original).steps.size(),"memory ABI must remove one actual cell");
  const auto before=run_abi(original,words),after=run_abi(rewritten,words);
  std::string differences;
  for(std::size_t i=0;i<before.size();++i)if(before[i]!=after[i])
    differences+=" slot "+std::to_string(i)+" ["+before[i]+"] != ["+after[i]+"]";
  require(before==after,"memory ABI must preserve fractional arguments, all visible words, entry shadow and exact returns on ROM:"+differences);
  auto changed=original;
  const auto body=std::find_if(changed.begin(),changed.end(),[](const auto& item){return item.kind==MachineItemKind::Op&&item.opcode==0x69;});
  require(body!=changed.end(),"body fixture must exist");const auto index=static_cast<std::size_t>(body-changed.begin());
  changed[index].opcode=0x61;
  require(candidates(changed).empty(),"moving a store across an aliasing recall must reject");
  changed=original;changed[index+3U].opcode=0x0c;
  require(candidates(changed).empty(),"a body that observes displaced X2 must reject");
  changed=original;changed[3].opcode=0x67;
  require(candidates(changed).empty(),"an unknown source memory word must not inherit canonicality");
}


std::vector<MachineItem> memory_loop_fixture(int home,int count,bool overwrite) {
  const auto base=memory_abi_fixture();
  const auto tail=std::find_if(base.begin(),base.end(),[](const auto& item){return item.kind==MachineItemKind::Label&&item.name=="outer";});
  std::vector<MachineItem> items;
  const auto op=[&](int code){items.push_back(MachineItem::op(code,"loop fixture"));};
  const auto call=[&](const std::string& name){op(0x53);items.push_back(MachineItem::address(name));};
  call("outer");op(0x43);op(count);op(0x41);
  items.push_back(MachineItem::label("scan"));op(0x61);call("body");
  constexpr std::array<int,4> loops{0x5d,0x5b,0x58,0x5a};
  op(loops[static_cast<std::size_t>(home)]);items.push_back(MachineItem::address("scan"));
  op(0x61);op(0x44);
  if(overwrite){op(7);op(0x41);}
  op(0x63);op(0x64);op(0x50);items.back().stop_disposition=StopDisposition::Terminal;
  items.insert(items.end(),tail,base.end());
  for(auto& item:items)if(item.kind==MachineItemKind::Op){
    if(item.opcode==0x61)item.opcode=0x60+home;
    else if(item.opcode==0x41)item.opcode=0x40+home;
  }
  return items;
}
void recall_fl_epoch_proof_and_rom() {
  for(int home=0;home<4;++home)for(int count=1;count<=9;++count){
    const auto original=memory_loop_fixture(home,count,true);
    std::vector<PreloadReport> words{{.register_name="2",.value="3.25"},{.register_name="9",.value="0.5"}};
    if(home!=2)words.push_back({.register_name=std::to_string(home),.value="2"});
    core::PostLayoutControlFlowOptions control;control.empty_return_target=1;
    CompileOptions options;const core::passes::PassContext context{options};
    const auto candidates=[&](const std::vector<MachineItem>& items){
      std::map<std::string,int> origins;int address=0;
      for(const auto& item:items){if(item.kind==MachineItemKind::Label)origins.emplace(item.name,address);else ++address;}
      return core::passes::contextual_memory_backed_entry_candidates(raise_machine_to_ir(items),context,
          {.maximum_work=200000,.maximum_candidates=2,.maximum_copy_sites=8,.maximum_growth_cells=4,
           .control=&control,.encoded_target_origins=&origins,.runtime_preloads=&words});
    };
    const auto valid=candidates(original);
    require(valid.size()==1U,"all nine positive rows and each FL home must admit the independent recall/FL word relation: home="+
        std::to_string(home)+" count="+std::to_string(count));
    require(run_abi(original,words)==run_abi(lower_ir_to_machine(valid.front().transformation.ops),words),
        "canonical and denormalized positive FL epochs must preserve actual ROM behavior");
    if(count==4){
      require(candidates(memory_loop_fixture(home,count,false)).empty(),"a physical-word observation before overwrite must reject FL view equality");
      auto changed=original;changed[3].opcode=0x67;words.push_back({.register_name="7",.value="2.5"});
      require(candidates(changed).empty(),"fractional counters must not inherit the positive integral FL relation");
      words.back().value="0x1E3";
      require(candidates(changed).empty(),"raw counter words must not inherit ordinary numeric equivalence");
    }
  }
}
std::vector<MachineItem> zero_literal_fixture(bool normalized=true,bool last_x=false,bool shadow=false) {
  std::vector<MachineItem> items;
  const auto op=[&](int code){items.push_back(MachineItem::op(code,"literal fixture"));};
  op(0x60);op(0x60);op(normalized?0x11:0x10);
  op(0x57);items.push_back(MachineItem::address("literal"));
  op(0x51);items.push_back(MachineItem::address("done"));
  items.push_back(MachineItem::label("literal"));
  for(int i=0;i<8;++i)op(9);
  op(0x0b);op(0x43);
  if(last_x){op(0x0f);op(0x4e);}
  if(shadow){op(0x0c);op(7);op(0x4e);}
  for(int home=6;home<=9;++home)op(0x60+home);
  op(0x10);items.push_back(MachineItem::label("done"));op(0x50);
  items.back().stop_disposition=StopDisposition::Terminal;
  return items;
}
void zero_literal_rematerialization_and_rom() {
  const auto original=zero_literal_fixture();
  const auto rewrite=[](const std::vector<MachineItem>& items){
    return core::passes::rematerialize_zero_underflow_constant(raise_machine_to_ir(items),{});
  };
  const auto result=rewrite(original);
  const auto changed=lower_ir_to_machine(result.ops);
  require(result.applied==1 && changed.size()+8U==original.size(),
      "a proved canonical-zero decimal literal must release eight cells without a preload owner");
  for(const auto& seed:std::vector<std::string>{"0","3.25","-6.5","9.9999999E98"}){
    const std::vector<PreloadReport> words{{.register_name="0",.value=seed},
        {.register_name="6",.value="2"},{.register_name="7",.value="3"},
        {.register_name="8",.value="4"},{.register_name="9",.value="5"}};
    require(run_abi(original,words)==run_abi(changed,words),
        "inline literal and self-indexed zero underflow must preserve full ROM observations and future entry: "+seed);
  }
  require(rewrite(zero_literal_fixture(false)).applied==0,
      "a numerical zero guess without a normalization certificate must not rematerialize the literal");
  require(rewrite(zero_literal_fixture(true,true)).applied==0,
      "the literal's different physical last-X must remain observable when saved before convergence");
  require(rewrite(zero_literal_fixture(true,false,true)).applied==0,
      "live decimal-entry shadow state must reject literal rematerialization");
  auto split=original;split.insert(split.begin()+12,MachineItem::label("interior"));
  require(rewrite(split).applied==0,"an interior addressable literal boundary must reject the replacement");

  auto pooled=original;
  pooled.erase(pooled.begin()+8,pooled.begin()+17);
  pooled.insert(pooled.begin()+8,MachineItem::op(0x6c,"constant recall"));
  auto writer=MachineItem::op(0xb7,"unrelated indexed store");
  writer.indirect_memory_targets=std::vector<int>{6};
  pooled.insert(pooled.begin(),std::move(writer));
  const std::map<std::string,std::string> immutable{{"c","-99999999"}};
  auto code=raise_machine_to_ir(pooled);
  require(core::passes::rematerialize_zero_underflow_constant(code,immutable).applied==1,
      "an already certified typed memory alias must retain the historical pooled-word optimization");
  code.front().meta.indirect_memory_targets.reset();
  require(core::passes::rematerialize_zero_underflow_constant(code,immutable).applied==0,
      "an unresolved indexed writer must not grant constant-pool immutability");
  core::PostLayoutControlFlowOptions control;control.empty_return_target=1;
  const auto flow=core::build_post_layout_control_flow(pooled,control);
  const auto with_aliases=core::passes::rematerialize_zero_underflow_constant(
      code,immutable,AddressSpaceModel::Standard,&flow);
  require(with_aliases.applied==1,"an exact disjoint indexed-store alias must retain the old pooled-word theorem");
  std::string pointer;
  for(int value=1;value<=99 && pointer.empty();++value){
    const auto address=core::evaluate_indirect_address("7",std::to_string(value),core::IndirectOperationKind::Memory);
    if(address && address->memory_target==6)pointer=std::to_string(value);
  }
  require(!pointer.empty(),"the ROM alias fixture must have a concrete stock R6 memory selector");
  const std::vector<PreloadReport> pooled_words{{.register_name="0",.value="3.25"},
      {.register_name="c",.value="-99999999"},{.register_name="7",.value=pointer},
      {.register_name="8",.value="4"},{.register_name="9",.value="5"}};
  require(run_abi(pooled,pooled_words)==run_abi(lower_ir_to_machine(with_aliases.ops),pooled_words),
      "the contextual pooled rewrite must preserve actual ROM words and the unrelated indexed write");
  pooled.front().indirect_memory_targets=std::vector<int>{6,12};
  const auto aliased=core::build_post_layout_control_flow(pooled,control);
  require(core::passes::rematerialize_zero_underflow_constant(code,immutable,
      AddressSpaceModel::Standard,&aliased).applied==0,
      "a possible write into the pool must invalidate its immutability certificate");
}
void achieved_game_size() {
  const std::string path=std::string(MKPRO_CALLBACK_REPO_ROOT)+"/examples/pending-optimizer/tic-tac-toe-4x4.mkpro";
  std::ifstream stream(path);require(stream.good(),"actual unchanged game source must be available");
  const std::string source{std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
  CompileOptions options;options.analysis=true;options.budget=999999;options.ordinary_decimal_keyboard_input=true;
  const auto result=compile_source(source,options);
  require(result.implemented && result.diagnostics.empty(),"actual game must compile without diagnostics");
  std::cout<<"4x4 stock ordinary-decimal: "<<result.steps.size()<<" cells\n";
  if(const auto* dump=std::getenv("MKPRO_NATIVE_CALLBACK_ARTIFACT_DUMP")){
    std::ofstream out(dump);out<<"{\"items\":"<<machine_items_to_json(result.items)<<",\"preloads\":[";
    bool first=true;
    for(const auto& word:result.preloads){
      if(!first)out<<',';first=false;
      out<<"{\"register_name\":"<<std::quoted(word.register_name)<<",\"value\":"<<std::quoted(word.value);
      if(word.retunable_natural_fractional_prefix)out<<",\"retunable_natural_fractional_prefix\":"<<std::quoted(*word.retunable_natural_fractional_prefix);
      if(word.lowered_data_value)out<<",\"lowered_data_value\":"<<std::quoted(*word.lowered_data_value);
      out<<'}';
    }
    out<<"]}";
  }
  require(result.steps.size()<=117U,"achieved 4x4 regression baseline is 117 cells; 105 remains a separate goal");
  const auto owner=completed_optimizer_options_for_testing(result);
  require(owner.has_value() && owner->ordinary_decimal_keyboard_input,
      "the actual winning ABI must retain its requested runtime input contract");
  const auto rejection=optimizer_static_proof_gate_rejection_reason_for_testing(*owner,result);
  require(!rejection,"the actual final game must independently pass its winning ABI proof gate: "+rejection.value_or(""));
  require(result.feature_profile==FeatureProfile::Standard &&
      std::none_of(result.preloads.begin(),result.preloads.end(),[](const auto& word){return word.register_name=="f";}),
      "the achieved size must belong to stock MK-61, without Rf");
  core::PostLayoutControlFlowOptions control;control.empty_return_target=1;
  const auto flow=core::build_post_layout_control_flow(result.items,control);
  require(flow.proved,"the published game must retain a complete final CFG");
  bool null_entry=false;
  for(const auto& [branch,targets]:flow.indirect_flow_targets)
    for(const auto& target:targets)
      null_entry=null_entry||core::passes::prove_selector_zero_flow_null_callback_entry(
          result.items,flow,target.item_index,branch);
  if(null_entry){
    const auto first=std::find_if(result.items.begin(),result.items.end(),[](const auto& item){return item.kind!=MachineItemKind::Label;});
    const auto index=static_cast<std::size_t>(first-result.items.begin());
    const auto reject_tampered=[&](CompileResult changed){
      const auto image=resolve_machine_items(changed.items,*owner);
      require(image.diagnostics.empty(),"null-return tampering must remain structurally resolvable");
      changed.steps=image.steps;
      require(optimizer_static_proof_gate_rejection_reason_for_testing(*owner,changed).has_value(),
          "the actual final verifier must reject a forged null-return ABI, even when both annotation views agree");
    };
    auto changed=result;changed.items[index].opcode=0x54;reject_tampered(std::move(changed));
    changed=result;changed.items[index].raw=true;reject_tampered(std::move(changed));
  }
  auto unrestricted_options=options;
  unrestricted_options.ordinary_decimal_keyboard_input=false;
  const auto unrestricted=compile_source(source,unrestricted_options);
  std::cout<<"4x4 stock unrestricted: "<<unrestricted.steps.size()<<" cells\n";
  require(unrestricted.implemented && unrestricted.steps.size()>=result.steps.size(),
      "a narrower input contract must retain the independently completed unrestricted incumbent");
  const auto has_input_contract=[](const CompileResult& compiled){
    return std::any_of(compiled.optimizations.begin(),compiled.optimizations.end(),[](const auto& report){
      return report.name=="ordinary-decimal-keyboard-input-contract";
    });
  };
  require(has_input_contract(result) && !has_input_contract(unrestricted),
      "completed-result caches must not exchange artifacts with different runtime input contracts");
  const auto cached=compile_source(source,options);
  require(cached.steps.size()==result.steps.size() && has_input_contract(cached) &&
          machine_items_to_json(cached.items)==machine_items_to_json(result.items),
      "revisiting the decimal contract after an unrestricted cache lookup must retain the exact proved artifact");
}
} // namespace
int main(int argc,char** argv) {
  try {
    if(argc==2 && std::string(argv[1])=="--game"){achieved_game_size();return 0;}
    zero_entry_proof();final_null_entry_proof();ordinary_leaf_alias_annotations();register_domain_queries();canonical_zero_rom();memory_abi_proof_and_rom();recall_fl_epoch_proof_and_rom();zero_literal_rematerialization_and_rom();
    require(null_callback_rom(false)==null_callback_rom(true),"null callback completion must preserve stack, X1, future numeric entry and exact caller continuation");
    std::cout<<"nullable callback/projection proofs and stock ROM facts passed\n";
  } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
