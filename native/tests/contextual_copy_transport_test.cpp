#include "mkpro/compiler.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/search_frontier.hpp"
#include "mkpro/core/passes/semantic_region_folding.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/helper_invariant_recall_hoist.hpp"
#include "mkpro/core/shared_helper_dual_mode_layout.hpp"
#include "mkpro/core/late_bound_decimal_selector.hpp"
#include "mkpro/core/stable_register_value_flow.hpp"
#include "mkpro/core/joint_constraint_origins.hpp"
#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/post_layout_packed_sign.hpp"
#include "mkpro/core/passes/liveness_analysis.hpp"
#include "mkpro/core/passes/register_coalesce.hpp"
#include "mkpro/core/passes/idempotent_entry_prologue.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/core/detail/cell_provenance.hpp"
#include "mkpro/core/passes/contextual_copy_transport.hpp"
#include "mkpro/core/passes/zero_underflow_constant.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <exception>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
namespace mkpro {
std::optional<CompileResult> materialize_proved_layout_for_testing(
    const std::string&,const CompileResult&,const CompileOptions&,
    const std::vector<MachineItem>&,const std::vector<PreloadReport>&,bool,bool=true);
}
namespace {
using namespace mkpro;using namespace mkpro::core;using mkpro::tests::require;
enum class Kind { Normal, ChangedSource, Alias, ObservedStack, Untyped, Raw };
struct Fixture{std::vector<MachineItem> items;std::vector<PreloadReport> preloads;};
Fixture fixture(Kind kind=Kind::Normal,int source=8,int copied=0,bool extra_mask=false,bool expose_shadow=false){
  MachineEmitter e;
  e.emit_jump(0x53,"first independent invocation","shared");
  if(kind==Kind::ObservedStack){e.emit_op(0x0a);e.emit_op(0x4a);}
  for(int i=0;i<4;++i)e.emit_op(0x67);
  e.emit_jump(0x53,"observe copied phase through a shared read","read_value");e.emit_op(0x4a);
  e.emit_number("0");e.emit_op(0x40+copied);
  e.emit_jump(0x53,"second independent invocation","shared");
  for(int i=0;i<4;++i)e.emit_op(0x67);
  e.emit_op(0x67);e.emit_op(0x40+copied);
  e.emit_jump(0x53,"observe the ordinary dynamic epoch through the same read","read_value");e.emit_op(0x4b);
  for(const int r:{1,11,4,10})e.emit_op(0x60+r);
  e.emit_stop(StopDisposition::Terminal);
  e.emit_label("shared");
  e.emit_op(0x60+source);e.emit_op(0x0b);e.emit_op(0x40+source);e.emit_op(0x40+copied);
  if(kind==Kind::Raw)e.items.back().raw=true;
  if(kind==Kind::Untyped){e.emit_stop(StopDisposition::Resumable);e.items.back().roles.clear();}
  for(int i=0;i<3;++i){
    e.emit_op(0x69);e.emit_op(0x60+copied);e.emit_number("10");e.emit_op(0x13);e.emit_op(0x12);e.emit_op(0x44+i);
  }
  if(kind==Kind::ChangedSource){e.emit_number("0");e.emit_op(0x40+source);}
  if(kind==Kind::Alias){e.emit_number(std::to_string(copied));e.emit_op(0x4b);e.emit_op(0xdb);e.items.back().indirect_memory_targets=std::vector<int>{copied};}
  if(extra_mask){e.emit_op(0x69);e.emit_number("88888834");e.emit_op(0x37);if(expose_shadow)e.emit_op(0x0a);e.emit_op(0x44);}
  e.emit_op(0x52);
  e.emit_label("read_value");e.emit_op(0x60+copied);e.emit_op(0x52);
  Fixture f{e.items,{}};
  for(int r=0;r<15;++r)f.preloads.push_back({.register_name=std::string(1,"0123456789abcde"[r]),.value=std::to_string(r+17)});
  f.preloads[7].value="23";f.preloads[9].value="100";f.preloads[static_cast<std::size_t>(source)].value="1";
  return f;
}
std::vector<std::string> observe(const LayoutSearchArtifact& a,int carrier,bool public_only=false){
  auto image=resolve_machine_items(a.items);require(image.diagnostics.empty(),"fixture resolves");
  std::vector<int> bytes;for(const auto& s:image.steps)bytes.push_back(s.opcode);
  emulator::MK61 calc;require(calc.command_limit()==105 && calc.load_program(bytes).diagnostics.empty(),"stock ROM fixture");
  for(const auto& p:a.preloads)calc.set_register(p.register_name,p.value);
  calc.set_register("X","31");calc.set_register("Y","41");calc.set_register("Z","53");calc.set_register("T","67");
  calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});
  require(calc.run_until_stable(5000,8).stopped,"both invocations stop");
  std::vector<std::string> result;
  // The typed display contract is X/Y. A test of later Z/T or last-X use
  // must put that read in the program; private slots are not an extra UI.
  for(const char* r:{"X","Y","Z","T"})
    if(!public_only || r[0]=='X' || r[0]=='Y')result.push_back(calc.read_register(r));
  for(int r=0;!public_only && r<15;++r)if(r!=carrier)result.push_back(calc.read_register(std::string(1,"0123456789abcde"[r])));
  return result;
}
int cells(const std::vector<MachineItem>& items){return static_cast<int>(std::count_if(items.begin(),items.end(),[](const auto& x){return x.kind!=MachineItemKind::Label;}));}
void deferred_charges(){
  MachineEmitter e;
  e.emit_op(9);e.items.back().roles={make_late_bound_decimal_selector_role(LateBoundDecimalSelectorPart::High,"target")};
  e.emit_op(8);e.items.back().roles={make_late_bound_decimal_selector_role(LateBoundDecimalSelectorPart::Low,"target")};
  while(cells(e.items)<100)e.emit_op(0x54);
  e.emit_label("target");e.emit_op(0x52);
  const auto original=e.items;
  const auto deferred=inspect_deferred_decimal_selector_charges(original);
  require(deferred.diagnostics.empty() && deferred.charges.size()==1U &&
      deferred.charges.front().target_address==98,"unplaced address 100 retains encoded equation 98");
  require(!rebind_late_bound_decimal_selectors(original,{0,99}).diagnostics.empty(),
      "analysis never makes an out-of-range final binding legal");
  auto placed=original;placed.erase(placed.begin()+2,placed.begin()+4);
  const auto bound=rebind_late_bound_decimal_selectors(placed,{0,99});
  require(bound.diagnostics.empty() && bound.applied==1 && bound.proofs.front().target_address==98,
      "placement must independently satisfy the original equation");
  for(int kind=0;kind<5;++kind){
    auto bad=original;
    if(kind==0)bad[0].raw=true;
    if(kind==1)bad[1].roles={make_late_bound_decimal_selector_role(LateBoundDecimalSelectorPart::Low,"missing")};
    if(kind==2)bad.erase(bad.end()-2);
    if(kind==3)bad.push_back(original[original.size()-2]);
    if(kind==4){auto high=bad[0],low=bad[1];high.opcode=4;high.mnemonic="4";low.opcode=2;low.mnemonic="2";bad.insert(bad.begin()+2,{high,low});}
    const auto rejected=inspect_deferred_decimal_selector_charges(bad);
    require(!rejected.diagnostics.empty() && rejected.charges.empty(),
        "raw, malformed, missing, duplicate and conflicting deferred charges fail atomically");
  }
}
void stale_preload_owner(){
  const std::string source=R"MKPRO(program AlternatingProbe {
    state { total: packed = 7 }
    loop {
      update(-3)
      show(total)
      update(3)
      show(total)
    }
    fn update(amount) { total += amount }
  })MKPRO";
  CompileOptions o;o.analysis=true;o.disable_candidate_search=true;
  o.alternating_sign_toggle_args=true;
  o.forced_logical_register_assignments={
      {.name="amount",.register_name="c"},
      {.name="eliminated_constant_owner",.register_name="c",.preload_owner=true}};
  const auto result=compile_source(source,o);
  require(result.implemented,"regenerated ABI with an extinct preload owner compiles");
  const auto phase=std::find_if(result.preloads.begin(),result.preloads.end(),[](const auto& p){return p.register_name=="c";});
  require(phase!=result.preloads.end() && phase->value=="3",
      "obsolete owner must not suppress the live phase initializer");
  std::vector<int> bytes;for(const auto& step:result.steps)bytes.push_back(step.opcode);
  emulator::MK61 calc;require(calc.load_program(bytes).diagnostics.empty(),"phase probe fits stock ROM");
  for(const auto& p:result.preloads)calc.set_register(p.register_name,p.value);
  calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});
  require(calc.run_until_stable(5000,8).stopped,"first phase stops");
  require(calc.read_register("X").starts_with("4,"),"first phase uses the initialized negative argument");
  calc.press_sequence({"\u0421/\u041f"});
  require(calc.run_until_stable(5000,8).stopped && calc.read_register("X").starts_with("7,"),
      "second phase restores the original argument and accumulated result");
}
void indirect_store_entry_facts(){
  for(int r=0;r<15;++r)for(const bool exponent:{false,true}){
    emulator::MK61 calc;std::vector<int> code{2,3};
    if(exponent)code.insert(code.end(),{0x0c,2});
    code.insert(code.end(),{0xb0+r,1,2,0x50});
    require(calc.load_program(code).diagnostics.empty(),"stock indirect-store entry fact");
    calc.set_register(std::string(1,"0123456789abcde"[r]),r<4?"8":r<7?"6":"7");
    calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});
    require(calc.run_until_stable(5000,8).stopped && calc.read_register("X").starts_with("12,") &&
        calc.read_register("Y").starts_with(exponent?"2300,":"23,"),
        "every stock indirect store ends an open mantissa/exponent and enables a fresh lift");
  }
  for(const int kind:{0,1,2}){
    MachineEmitter e;e.emit_op(0xb0);e.items.back().indirect_memory_targets=std::vector<int>{7};
    if(kind==1)e.items.back().raw=true;
    if(kind==2)e.emit_op(0x0a);else{e.emit_op(1);e.emit_op(2);}
    e.emit_op(0x37);for(int i=0;i<4;++i)e.emit_op(0x61);e.emit_stop(StopDisposition::Terminal);
    const auto flow=build_post_layout_control_flow(e.items);
    const StackValueEqualityState initial{{true,false,true,true},false,true};
    if(kind==0){
      const auto old_lift=analyze_post_layout_automatic_entry_lift(e.items,flow);
      const auto memory_lift=analyze_post_layout_automatic_entry_lift_with_indirect_store_facts(e.items,flow);
      require(old_lift && memory_lift,"both entry analyses have complete context domains");
      for(std::size_t s=0;s<flow.execution_states.size();++s)if(flow.execution_states[s].item_index==1U)
        require(!old_lift->at(s) && memory_lift->at(s),"memory-entry precision stays independent of the old greedy policy");
    }
    require(!prove_post_layout_stack_entry_equality(e.items,flow,0,initial),
        "legacy greedy proof retains its independent conservative entry policy");
    const bool proved=prove_post_layout_stack_entry_equality(e.items,flow,0,initial,
        {.refine_runtime_selectors=false,.use_indirect_store_entry_facts=true});
    require(proved==(kind==0),"indirect-store fact never admits raw code or an unequal decimal shadow restore");
  }
}
void closed_toggle_sign_facts(){
  require(detail::is_cell_origin_provenance("contextual-copy-store-origin:12") &&
      !detail::is_cell_origin_provenance("contextual-copy-store-origin:") &&
      !detail::is_cell_origin_provenance("contextual-copy-store-origin:1:semantic"),
      "only complete numeric transport origins are descriptive metadata");
  for(int kind=0;kind<5;++kind){
    MachineEmitter e;e.emit_jump(0x53,"first phase",kind==4?"neg_entry":"toggle");
    e.emit_op(0x68);e.emit_op(0x32);e.emit_op(0x4a);
    e.emit_jump(0x53,"second phase","toggle");e.emit_op(0x68);e.emit_op(0x32);e.emit_stop(StopDisposition::Terminal);
    e.emit_label("toggle");e.emit_op(0x68);e.emit_label("neg_entry");e.emit_op(0x0b);
    if(kind==1)e.items.back().raw=true;
    e.emit_op(0x48);
    if(kind==2){e.emit_op(1);e.emit_op(0x48);}
    if(kind==3){e.emit_op(0xb7);e.items.back().indirect_memory_targets=std::vector<int>{8};}
    e.emit_op(0x52);
    const auto flow=build_post_layout_control_flow(e.items);std::vector<std::size_t> queries;
    for(std::size_t s=0;s<flow.execution_states.size();++s)
      if(e.items[flow.execution_states[s].item_index].opcode==0x32)queries.push_back(s);
    const std::vector<PreloadReport> words{{.register_name="8",.value="0.25"}};
    const auto proof=prove_post_layout_toggle_sign_inputs(e.items,flow,queries,words);
    if(kind==0){
      require(proof.signs.size()==2U && proof.signs.begin()->second==-1 && proof.signs.rbegin()->second==1,
          "closed negation automaton derives both caller-specific signs");
      require(prove_post_layout_toggle_sign_inputs(e.items,flow,queries,{}).signs.empty(),"no implicit initial phase");
      const auto exhausted=prove_post_layout_toggle_sign_inputs(e.items,flow,queries,words,1);
      require(exhausted.exhausted && exhausted.signs.empty(),"work exhaustion exports no partial signs");
    }else require(proof.signs.empty(),"raw transitions, dynamic writes, aliases and side entries reject finite sign facts");
  }
}
void formal_toggle_sign_facts(){
  for(int kind=0;kind<7;++kind){
    MachineEmitter e;
    e.emit_jump(0x53,"formal first phase",kind==3?"neg_entry":kind==2?"prefix_entry":"toggle");
    e.items.back().formal_opcode=kind==3?0xec:kind==2?0xea:kind==6?0x100:0xeb;
    e.emit_op(0x6c);e.emit_op(0x32);e.emit_op(0x48);
    e.emit_jump(0x53,"canonical second phase","toggle");
    e.emit_op(0x6c);e.emit_op(0x32);e.emit_stop(StopDisposition::Terminal);
    while(cells(e.items)<38)e.emit_op(0x54);
    e.emit_label("prefix_entry");e.emit_op(0x54);
    e.emit_label("toggle");e.emit_op(0x6c);
    e.emit_label("neg_entry");e.emit_op(0x0b);
    if(kind==1)e.items.back().raw=true;
    if(kind==4)e.items.back().formal_opcode=0x0b;
    e.emit_op(0x4c);
    if(kind==5){e.emit_op(2);e.emit_op(0x4c);}
    e.emit_op(0x52);
    const auto flow=build_post_layout_control_flow(e.items);
    std::vector<std::size_t> queries;
    for(std::size_t s=0;s<flow.execution_states.size();++s)
      if(e.items[flow.execution_states[s].item_index].opcode==0x32)queries.push_back(s);
    const std::vector<PreloadReport> words{{.register_name="c",.value="0.25"}};
    const auto proof=prove_post_layout_toggle_sign_inputs(e.items,flow,queries,words);
    if(kind!=0 && kind!=2){
      require(proof.signs.empty(),"invalid formal routes, raw/overridden commands, transition side entries and dynamic writes reject: "+std::to_string(kind));
      continue;
    }
    require(flow.proved && proof.signs.size()==2U &&
        proof.signs.begin()->second==-1 && proof.signs.rbegin()->second==1,
        "one finite automaton preserves exact signs across formal and canonical call contexts");
    const auto exhausted=prove_post_layout_toggle_sign_inputs(e.items,flow,queries,words,1);
    require(exhausted.exhausted && exhausted.signs.empty(),"formal route exhaustion exports no partial fact");
    const auto bytes=resolve_machine_items(e.items);
    require(bytes.diagnostics.empty(),"formal finite-state ROM fixture resolves");
    std::vector<int> code;for(const auto& step:bytes.steps)code.push_back(step.opcode);
    emulator::MK61 calc;require(calc.load_program(code).diagnostics.empty(),"formal finite-state fixture fits stock memory");
    calc.set_register("c","0.25").press_sequence({"\u0412/\u041e","\u0421/\u041f"});
    require(calc.run_until_stable(5000,8).stopped &&
        calc.read_register("8").starts_with("-1,") && calc.read_register("X").starts_with("1,"),
        "stock ROM confirms both signs and the canonical continuation after the formal call");
  }
}
void transition_store_sign_facts(){
  for(int kind=0;kind<4;++kind){
    MachineEmitter e;e.emit_jump(0x53,"first phase","toggle");e.emit_op(0x48);
    e.emit_jump(0x53,"second phase","toggle");e.emit_stop(StopDisposition::Terminal);
    e.emit_label("toggle");e.emit_op(0x6c);e.emit_op(0x0b);e.emit_op(0x4c);
    if(kind==1)e.items.back().raw=true;
    if(kind==2)e.emit_op(0x6d);
    if(kind==3){e.emit_number("0");e.emit_op(0x4c);}
    e.emit_op(0x32);e.emit_op(0x52);
    const auto flow=build_post_layout_control_flow(e.items);std::vector<std::size_t> queries;
    for(std::size_t s=0;s<flow.execution_states.size();++s)
      if(e.items[flow.execution_states[s].item_index].opcode==0x32)queries.push_back(s);
    const std::vector<PreloadReport> words{{.register_name="c",.value="0.125"},{.register_name="d",.value="3"}};
    const auto proof=prove_post_layout_toggle_sign_inputs(e.items,flow,queries,words);
    require((proof.signs.size()==2U)==(kind==0),
        "same-word transition stores transport a closed sign, never raw, changed-X or dynamic-store paths");
    if(kind!=0)continue;
    require(proof.signs.begin()->second==-1 && proof.signs.rbegin()->second==1 &&
        std::all_of(proof.carriers.begin(),proof.carriers.end(),[](const auto& c){return c.second=="c";}),
        "post-store signs use the new phase row and retain the physical dependency");
    auto bytes=resolve_machine_items(e.items);require(bytes.diagnostics.empty(),"transition-sign ROM fixture resolves");
    std::vector<int> code;for(const auto& step:bytes.steps)code.push_back(step.opcode);emulator::MK61 calc;
    require(calc.load_program(code).diagnostics.empty(),"transition-sign ROM fixture fits");
    for(const auto& word:words)calc.set_register(word.register_name,word.value);
    calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});require(calc.run_until_stable(5000,8).stopped,"both exact phase contexts finish");
    require(calc.read_register("8").starts_with("-1,") && calc.read_register("X").starts_with("1,"),
        "ROM confirms the sign after each exact Recall/Negate/Store transition");
  }
}
void finite_value_dependency_facts(){
  MachineEmitter e;e.emit_jump(0x53,"first phase","toggle");e.emit_op(0x48);
  e.emit_jump(0x53,"second phase","toggle");e.emit_stop(StopDisposition::Terminal);
  e.emit_label("toggle");e.emit_op(0x6c);e.emit_op(0x0b);e.emit_op(0x4c);e.emit_op(0x52);
  const auto flow=build_post_layout_control_flow(e.items);std::vector<std::size_t> demands;
  for(std::size_t s=0;s<flow.execution_states.size();++s)
    if(e.items[flow.execution_states[s].item_index].opcode==0x4c)demands.push_back(s);
  const auto exact=prove_post_layout_toggle_sign_inputs(e.items,flow,demands,{{.register_name="c",.value="1"}});
  require(exact.signs.empty() && exact.values.size()==2U && exact.value_carriers.size()==2U &&
      exact.values.begin()->second==-1 && exact.values.rbegin()->second==1,
      "a closed integral carrier certifies actual post-store values without manufacturing Ksign commands");
  require(prove_post_layout_toggle_sign_inputs(e.items,flow,demands,{{.register_name="c",.value="0.25"}}).values.empty(),
      "a nonintegral carrier is never replaced by its sign or an outward integer bound");
  const auto exhausted=prove_post_layout_toggle_sign_inputs(e.items,flow,demands,{{.register_name="c",.value="1"}},1);
  require(exhausted.exhausted && exhausted.values.empty() && exhausted.value_carriers.empty(),
      "exhaustion discards all exact value facts and dependencies");
}
void encoded_guarded_lifetime(){
  for(const int kind:{0,1,2,3,4}){
    MachineEmitter e;
    if(kind==4){e.emit_jump(0x53,"warm first context","toggle");e.emit_jump(0x53,"warm second context","toggle");}
    e.emit_jump(0x53,"phase","toggle");
    if(kind!=3 && kind!=4){e.emit_op(0x6c);e.emit_op(0x32);}
    e.emit_op(0x48);
    e.emit_op(8);e.emit_op(0x40);
    e.emit_op(0x69);e.emit_op(0x68);e.emit_op(0x36);e.emit_op(0x48);e.emit_op(0x11);
    e.emit_jump(0x5e,"unequal candidate skips assignment","next");e.emit_op(3);e.emit_op(0x40);
    e.emit_label("next");e.emit_op(0x68);e.emit_jump(kind==2?0x59:0x5c,"guard payload observation","use");
    e.emit_op(0);e.emit_stop(StopDisposition::Terminal);
    e.emit_label("use");e.emit_op(0x60);e.emit_op(0x4a);e.emit_stop(StopDisposition::Terminal);
    e.emit_label("toggle");e.emit_op(0x6c);e.emit_op(0x0b);e.emit_op(0x4c);
    if(kind==3)e.emit_op(0x32);
    e.emit_op(0x52);
    const auto ops=raise_machine_to_ir(e.items);const auto flow=build_post_layout_control_flow(e.items);
    std::size_t definition=ops.size(),use=ops.size();
    for(std::size_t i=0;i<ops.size();++i){if(ops[i].kind==IrKind::Store && ops[i].register_name=="0" && definition==ops.size())definition=i;
      if(ops[i].kind==IrKind::Recall && ops[i].register_name=="0")use=i;}
    const passes::GuardedDefinitionUseQuery query{definition,use,"0"};
    passes::LivenessOptions options;options.equal_entry_value_classes={{"c",kind==1?"literal:0":kind==4?"literal:1":"literal:0.25"}};
    require(passes::prove_unreachable_definition_uses(ops,flow,{query},options).empty(),"legacy guard has no unproved phase assumption");
    options.discover_toggle_projection_signs=true;
    const auto proved=passes::prove_unreachable_definition_uses(ops,flow,{query},options);
    require(proved.contains(query)==(kind==0 || kind==3 || kind==4),"derived sign splits an old cursor epoch only under the matching guard, also after an exact transition store");
  }
}
void literal_recall_boundary_rom(){
  for(const int opcode:{0x10,0x11,0x12,0x13,0x37,0x38,0x39}){
    std::vector<std::string> first;
    for(const bool recall:{false,true}){
      // Main entry does not itself certify a fresh-digit stack lift.
      // Match the pooling precondition with an actual closing store first.
      emulator::MK61 calc;std::vector<int> code{0x40};
      if(recall)code.push_back(0x68);else code.insert(code.end(),{2,3});
      code.insert(code.end(),{opcode,1,2,0x50});
      require(calc.load_program(code).diagnostics.empty(),"literal/recall binary ROM boundary");
      calc.set_register("8","23");calc.set_register("X","230");
      calc.set_register("Y","31");calc.set_register("Z","41");calc.set_register("T","53");
      calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});
      require(calc.run_until_stable(5000,8).stopped && calc.read_register("X").starts_with("12,"),
          "supported arithmetic/logical binary commands synchronize open and recalled entry modes");
      std::vector<std::string> words;for(const char* r:{"X","Y","Z","T"})words.push_back(calc.read_register(r));
      if(!recall)first=words;else require(first==words,"binary boundary preserves every visible stack word");
    }
  }
}
void allocation_exposed_pool(){
  CompileOptions options;options.analysis=true;options.defer_return_suffix_until_callee_hole=true;
  for(const int kind:{0,1,2}){
    auto f=fixture(Kind::Normal,8,0,true,kind==2);
    if(kind!=1)std::erase_if(f.preloads,[](const auto& p){return p.register_name=="e";});
    const auto edges=contextual_copy_transport_layout_transactions({f.items,f.preloads,{}},options);
    const auto pool=std::find_if(edges.begin(),edges.end(),[](const auto& edge){return std::any_of(
        edge.node.optimizations.begin(),edge.node.optimizations.end(),[](const auto& p){return p.name=="allocation-exposed-literal-pool";});});
    if(kind==0){
      require(pool!=edges.end(),"deferred copy, finite encoding, vacant home and literal pooling compose");
      require(cells(pool->node.items)<=cells(f.items)-14,"encoding and constant pooling must repay all decoders and reloads");
      require(observe({f.items,f.preloads,{}},8,true)==observe(pool->node,8,true),
          "pooled runtime initializer and all subsequent results agree in stock ROM");
    }else if(kind==1){
      for(const auto& edge:edges){const auto owner=std::find_if(edge.node.preloads.begin(),edge.node.preloads.end(),[](const auto& word){return word.register_name=="e";});
        require(owner!=edge.node.preloads.end() && owner->value==f.preloads.back().value,"an arbitrary unused source owner remains reserved even when another proved epoch frees a different home");}
      if(pool!=edges.end())require(observe({f.items,f.preloads,{}},-1,true)==observe(pool->node,-1,true),"pooling in another independently retired home preserves all future results");
    }else require(pool==edges.end(),"an observed entry shadow forbids unproved pooling");
  }
}
void arbitrary_epoch_deadness(){
  for(int kind=0;kind<6;++kind){
    MachineEmitter e;e.emit_jump(0x53,"phase","toggle");e.emit_op(0x6c);e.emit_op(0x32);e.emit_op(0x48);
    e.emit_op(8);e.emit_op(0x40);e.emit_op(9);e.emit_op(0x4a);
    const std::size_t birth_item=e.items.size()-1U;
    if(kind==3){e.emit_op(0x60);e.emit_op(0x4d);}
    e.emit_op(0x69);e.emit_op(0x68);e.emit_op(0x36);e.emit_op(0x48);e.emit_op(0x11);
    e.emit_jump(0x5e,"selection skips new coordinate","next");e.emit_op(3);e.emit_op(0x40);
    e.emit_label("next");e.emit_op(0x68);e.emit_jump(kind==2?0x59:0x5c,"old coordinate guard","use");
    e.emit_op(0);e.emit_stop(StopDisposition::Terminal);
    e.emit_label("use");e.emit_op(0x60);e.emit_op(0x4b);e.emit_stop(StopDisposition::Terminal);
    e.emit_label("toggle");e.emit_op(0x6c);e.emit_op(0x0b);e.emit_op(0x4c);e.emit_op(0x52);
    if(kind==4)for(auto& item:e.items)if(item.opcode==0x50)item.roles.clear();
    const auto ops=raise_machine_to_ir(e.items),prefix=raise_machine_to_ir(
        std::vector<MachineItem>(e.items.begin(),e.items.begin()+static_cast<std::ptrdiff_t>(birth_item)));
    const passes::GuardedOldWordQuery query{prefix.size(),"0"};
    const auto flow=build_post_layout_control_flow(e.items);passes::LivenessOptions options;
    options.equal_entry_value_classes={{"c",kind==1?"literal:0":"literal:0.25"}};
    options.discover_toggle_projection_signs=true;
    const auto proof=passes::analyze_dead_old_words(ops,flow,{query},options,kind==5?1U:400000U);
    require(proof.dead_words.contains(query)==(kind==0),
        "arbitrary birth requires old-word death on every guarded path, with real terminal observations");
    if(kind==5)require(proof.exhausted && proof.dead_words.empty(),"old epoch exhaustion exports no partial safety");
    const auto malformed=passes::analyze_dead_old_words(ops,flow,{{ops.size(),"0"}},options);
    require(malformed.dead_words.empty(),"malformed birth cannot waive an interference edge");
    for(const char* invalid:{"", "f", "X", "15"})
      require(passes::analyze_dead_old_words(ops,flow,{{prefix.size(),invalid}},options).dead_words.empty(),
          "stack words, nonexistent registers and mk61s-only Rf are not stock data homes");
  }
}
void deferred_sentinel_materialization(){
  for(int kind=0;kind<7;++kind)for(const int destination:{3,10}){
    if(destination==3 && kind==1)continue;
    MachineEmitter e;e.emit_op(0x69);e.emit_op(0x69);e.emit_op(kind==3?0x14:0x11);
    e.emit_jump(0x5e,"normalized zero path","other");
    for(int n=0;n<8;++n){if(kind==4 && n==3)e.emit_label("interior");e.emit_op(kind==6?8:9);if(kind==5 && n==2)e.items.back().raw=true;}
    e.emit_op(0x0b);e.emit_op(0x40+destination);
    if(kind==1)e.emit_op(0x63);
    e.emit_op(kind==2?0x0a:0x60+destination);e.emit_op(0x0f);e.emit_op(0x4b);e.emit_stop(StopDisposition::Terminal);
    e.emit_label("other");e.emit_stop(StopDisposition::Terminal);
    const std::vector<PreloadReport> words{{.register_name="9",.value="100"},{.register_name="3",.value="11"}};
    const auto ops=raise_machine_to_ir(e.items);const auto flow=build_post_layout_control_flow(e.items);
    const auto transformed=passes::deferred_zero_underflow_literal(ops,flow,words);
    require((transformed.applied>0)==(kind==0),
        "deferred sentinel requires normalized zero, exclusive entry, dead scratch and hidden-entry nonobservation");
    if(kind==0){const LayoutSearchArtifact after{lower_ir_to_machine(transformed.ops),words,{}};
      require(cells(after.items)==cells(e.items)-(destination==3?8:7),"sentinel lowering saves its complete materialization cost");
      require(observe({e.items,words,{}},3,true)==observe(after,3,true),
          "stock ROM agrees on X/Y/Z/T and a subsequent explicit physical last-X read");
      require(passes::deferred_zero_underflow_literal(ops,flow,words,1).applied==0 || destination==3,
          "scratch proof exhaustion cannot authorize a borrowed register");}
  }
}
void charged_alias_privacy(){
  MachineEmitter e;e.emit_op(0x40);
  const auto charge=[&](int high,int low,const char* label){
    e.emit_op(high);e.items.back().roles={make_late_bound_decimal_selector_role(LateBoundDecimalSelectorPart::High,label)};
    e.emit_op(low);e.items.back().roles={make_late_bound_decimal_selector_role(LateBoundDecimalSelectorPart::Low,label)};
    e.emit_jump(0x53,"charge entry","shared");
  };
  charge(2,0,"first");charge(4,0,"second");e.emit_op(0x69);e.emit_stop(StopDisposition::Terminal);
  while(cells(e.items)<20)e.emit_op(0x54);
  e.emit_label("first");e.emit_label("first_alias");for(int i=0;i<4;++i)e.emit_op(0x61);e.emit_op(0x52);
  while(cells(e.items)<40)e.emit_op(0x54);
  e.emit_label("second");e.emit_label("second_alias");e.emit_op(0x25);e.emit_op(0x14);e.emit_op(0x49);e.emit_op(0x52);
  e.emit_label("shared");e.emit_op(0x4b);e.emit_op(0x67);e.emit_op(0x61);e.emit_op(0xab);
  e.items.back().roles={"late-decimal-selector-consumer"};e.items.back().indirect_flow_targets=std::vector<IrTarget>{std::string("first_alias"),std::string("second_alias")};e.emit_op(0x52);
  // Move both commands without changing their independently encoded origins.
  e.items.erase(e.items.begin()+11,e.items.begin()+14);
  const auto flow=build_post_layout_control_flow(e.items);
  const std::vector<PreloadReport> words{{.register_name="1",.value="2"},{.register_name="7",.value="8"}};
  const detail::JointOriginMap origins{{"first_alias",20},{"second_alias",40}};
  const auto precise=detail::protected_joint_runtime_charge_targets(e.items,&flow,&words,true,&origins);
  require(precise && !precise->contains("first") && precise->at("second")==40,
      "alias command identities distinguish a private callback mode from a data-bearing one after relayout");
  auto conflicting=origins;conflicting["first"]=21;
  const auto rejected=detail::protected_joint_runtime_charge_targets(e.items,&flow,&words,true,&conflicting);
  require(!rejected || rejected->contains("first"),"conflicting charge origins cannot release a callback address");
}
void padded_symbolic_packing(){
  for(const bool empty_return:{false,true}){
    MachineEmitter e;if(empty_return)e.emit_op(0x52);
    e.emit_op(0xa7);e.items.back().indirect_flow_targets=std::vector<IrTarget>{std::string("first")};
    e.emit_op(0x0d);e.emit_stop(StopDisposition::Terminal);
    const int old_target=cells(e.items);e.emit_label("first");e.emit_op(0x61);e.emit_op(0x52);
    e.emit_label("second");e.emit_op(0x62);e.emit_op(0x52);
    const auto ops=raise_machine_to_ir(e.items);CompileOptions options;
    PostLayoutControlFlowOptions control;if(empty_return)control.empty_return_target=1;
    passes::SymbolicRegionProjection projection;for(const auto& op:ops)projection.cell_widths.push_back(passes::cells_per_op(op));
    projection.defer_indirect_entries=true;projection.maximum_padding_cells=2;
    const std::map<std::string,int> targets{{"first",old_target+5}};
    require(!passes::layout_symbolic_regions(ops,targets,{options},4096,&control,true,&projection),
        "a placement cannot borrow more neutral cells than its declared budget");
    projection.maximum_padding_cells=3;
    const auto layout=passes::layout_symbolic_regions(ops,targets,{options},4096,&control,true,&projection);
    require(layout && layout->projected_cells==cells(e.items)+3 && layout->projected_targets.at("first")==old_target+5,
        "fallback packs closed chains with the minimum fully priced unreachable padding");
    require(!passes::layout_symbolic_regions(ops,targets,{options},1,&control,true,&projection),
        "padding search exhaustion never returns a partial layout");
    const std::vector<PreloadReport> words{{.register_name="1",.value="23"},{.register_name="2",.value="31"},
      {.register_name="7",.value=std::to_string(old_target)}};
    auto after_words=words;after_words.back().value=std::to_string(old_target+5);
    const auto items=lower_ir_to_machine(layout->ops);
    require(observe({e.items,words,{}},7,true)==observe({items,after_words,{}},7,true),
        "ROM preserves the same called body, continuation and visible stack after explicitly rebinding its private address");
    if(empty_return)require(items[1].opcode==0xa7,"physical empty-return 01 still enters its original command, never padding");
  }
}
void whole_family_permutation(){
  MachineEmitter e;e.emit_op(0xa7);e.items.back().indirect_flow_targets=std::vector<IrTarget>{std::string("first")};
  e.emit_op(0xa8);e.items.back().indirect_flow_targets=std::vector<IrTarget>{std::string("second")};
  e.emit_op(0x67);e.emit_op(0x68);e.emit_op(0x10);e.emit_stop(StopDisposition::Terminal);
  e.emit_label("first");e.emit_op(0x61);e.emit_op(0x52);e.emit_label("second");e.emit_op(0x62);e.emit_op(0x52);
  const auto flow=build_post_layout_control_flow(e.items);PostLayoutControlFlowOptions control;
  const std::vector<PreloadReport> words{{.register_name="1",.value="23"},{.register_name="2",.value="31"},
      {.register_name="7",.value="17"},{.register_name="8",.value="23"}};
  const auto choices=permuted_deferred_natural_target_selector_allocations(e.items,words,flow,{{"7",17},{"8",23}},control);
  const auto swapped=std::find_if(choices.begin(),choices.end(),[](const auto& c){return c.items[0].opcode==0xa8 && c.items[1].opcode==0xa7;});
  require(swapped!=choices.end() && swapped->cell_delta==0,"neutral complete-family exchange has its own frontier without direct conversions or releases");
  CompileOptions options;
  const auto place=[&](const std::vector<MachineItem>& items,bool swap){
    const auto ops=raise_machine_to_ir(items);passes::SymbolicRegionProjection projection;
    for(const auto& op:ops)projection.cell_widths.push_back(passes::cells_per_op(op));
    projection.defer_indirect_entries=true;projection.maximum_padding_cells=15;
    return passes::layout_symbolic_regions(ops,{{"first",swap?23:17},{"second",swap?17:23}},{options},4096,&control,true,&projection);
  };
  const auto before=place(e.items,false),after=place(swapped->items,true);
  require(before && after,"both conditional family assignments close through explicit placement");
  require(observe({lower_ir_to_machine(before->ops),words,{}},7,true)==
      observe({lower_ir_to_machine(after->ops),words,{}},7,true),
      "ROM agrees on both callbacks and later data reads of the unmodified selector words");
  auto raw=e.items;raw[0].raw=true;
  require(permuted_deferred_natural_target_selector_allocations(raw,words,flow,{{"7",17},{"8",23}},control).empty(),
      "raw flow ownership cannot authorize a family permutation");
}
void typed_resume_family_permutation(){
  for(int kind=0;kind<4;++kind){
    MachineEmitter e;e.emit_op(0x69);e.emit_stop(StopDisposition::Resumable);
    e.items.back().manual_interaction=ManualInteractionAnchor{.protocol_id=0,.phase=-1,.kind=ManualInteractionAnchorKind::PromptStop};
    e.emit_op(0xa7);e.items.back().indirect_flow_targets=std::vector<IrTarget>{std::string("first")};
    e.items.back().manual_interaction=ManualInteractionAnchor{.protocol_id=kind==2?1:0,.phase=0,
        .kind=kind==1?ManualInteractionAnchorKind::SingleStepCommand:ManualInteractionAnchorKind::ContinuousResume};
    if(kind==3)e.items.back().raw=true;
    e.emit_op(0xa8);e.items.back().indirect_flow_targets=std::vector<IrTarget>{std::string("second")};
    e.emit_op(0x67);e.emit_op(0x68);e.emit_op(0x10);e.emit_stop(StopDisposition::Terminal);
    e.emit_label("first");e.emit_op(0x22);e.emit_op(0x52);
    e.emit_label("second");e.emit_op(0x61);e.emit_op(0x10);e.emit_op(0x52);
    const auto flow=build_post_layout_control_flow(e.items);PostLayoutControlFlowOptions control;
    const std::vector<PreloadReport> words{{.register_name="1",.value="23"},{.register_name="9",.value="0"},
        {.register_name="7",.value="17"},{.register_name="8",.value="23"}};
    const auto choices=permuted_deferred_natural_target_selector_allocations(e.items,words,flow,{{"7",17},{"8",23}},control);
    const auto swapped=std::find_if(choices.begin(),choices.end(),[](const auto& c){return c.items[2].opcode==0xa8 && c.items[3].opcode==0xa7;});
    require((swapped!=choices.end())==(kind==0),
        "only a validated continuous resume admits equal-effect stable-family exchange, never single step, mismatched protocol or raw code");
    if(kind!=0)continue;
    CompileOptions options;
    const auto place=[&](const std::vector<MachineItem>& items,bool swap){
      const auto ops=raise_machine_to_ir(items);passes::SymbolicRegionProjection projection;
      for(const auto& op:ops)projection.cell_widths.push_back(passes::cells_per_op(op));
      projection.defer_indirect_entries=true;projection.maximum_padding_cells=16;
      return passes::layout_symbolic_regions(ops,{{"first",swap?23:17},{"second",swap?17:23}},{options},4096,&control,true,&projection);
    };
    const auto before=place(e.items,false),after=place(swapped->items,true);
    require(before && after,"typed resume families close without moving any input phase");
    const auto play=[&](const passes::SymbolicRegionLayout& layout,const char* number){
      auto image=resolve_machine_items(lower_ir_to_machine(layout.ops));require(image.diagnostics.empty(),"typed family image resolves");
      std::vector<int> bytes;for(const auto& step:image.steps)bytes.push_back(step.opcode);emulator::MK61 calc;
      require(calc.load_program(bytes).diagnostics.empty(),"typed family fixture fits stock ROM");
      for(const auto& word:words)calc.set_register(word.register_name,word.value);
      calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});require(calc.run_until_stable(5000,8).stopped,"initial prompt remains");
      calc.press_sequence({number,"\u0421/\u041f"});require(calc.run_until_stable(5000,8).stopped,"same numeric input resumes to terminal result");
      std::vector<std::string> result;for(const char* r:{"X","Y","Z","T","X1","7","8"})result.push_back(calc.read_register(r));return result;
    };
    for(const char* number:{"4","7"})require(play(*before,number)==play(*after,number),
        "ROM preserves entered value, call/return contexts, complete visible stack, last-X and later selector data");
  }
}
void aliased_helper_operand_abi(){
  for(int kind=0;kind<7;++kind){
    MachineEmitter e;
    for(int n=1;n<=(kind==6?4:3);++n){e.emit_op(0x69);e.emit_op(0x60+(n==4?1:n));
      e.emit_jump(kind==2 && n==3?0x51:0x53,"common operand helper",
          kind==6 && n==4?"padding":kind==1 && n==3?"interior":n==1?"enter_a":n==2?"enter_b":"enter_c");e.emit_op(0x49+n);}
    e.emit_op(0x0f);e.emit_op(0x4e);e.emit_op(0x6a);e.emit_op(0x6b);e.emit_op(0x6c);e.emit_stop(StopDisposition::Terminal);
    if(kind>=5){e.emit_label("padding");e.emit_op(0x54);e.emit_op(0x54);}
    e.emit_label("routine");e.emit_label("enter_a");e.emit_label("enter_b");e.emit_label("enter_c");
    if(kind==3)e.emit_label("enter_a");e.emit_op(0x22);
    if(kind==1)e.emit_label("interior");e.emit_op(0x54);e.emit_op(0x52);
    HelperInvariantRecallHoistOptions options;options.allow_before_call_commutative_tail=false;options.allow_x_preserving_root=true;
    if(kind>=5)options.complete_program_control=PostLayoutControlFlowOptions{};
    const auto result=rewrite_helper_invariant_recall_hoist(e.items,kind==4?"enter_b":"routine",options);
    if((result.applied>0)!=(kind==0 || kind==4 || kind==5)){
      std::cerr<<"helper alias fixture kind="<<kind<<" applied="<<result.applied<<"\n";
      for(const auto& reason:result.proof.reasons)std::cerr<<reason<<"\n";
    }
    require((result.applied>0)==(kind==0 || kind==4 || kind==5),
        "same-command aliases are one complete helper entry, while interior entries, non-calls and ambiguous labels remain barriers");
    if(result.applied==0)continue;
    require(result.proof.final_artifact_proved && result.proof.calls.size()==3U && cells(result.items)==cells(e.items)-1,
        "all alias calls share the re-proved root ABI and complete cost");
    const std::vector<PreloadReport> words{{.register_name="1",.value="2"},{.register_name="2",.value="3"},
        {.register_name="3",.value="4"},{.register_name="9",.value="8"}};
    require(observe({e.items,words,{}},-1)==observe({result.items,words,{}},-1),
        "ROM agrees on all public stack and physical memory reads, including an explicit last-X probe after aliased calls");
  }
}
void contextual_helper_continuation(){
  int precise_cells=0,legacy_cells=0;
  for(int kind=0;kind<6;++kind){
    MachineEmitter e;e.emit_jump(0x51,"start","main");
    e.emit_label("good_callback");
    if(kind!=2){e.emit_number("12");e.emit_op(0x6a);e.emit_op(0x6b);e.emit_op(0x10);e.emit_op(0x4e);}
    e.emit_op(0x52);
    e.emit_label("unselected_callback");e.emit_op(0x25);e.emit_op(0x4e);e.emit_op(0x52);
    e.emit_label("main");e.emit_op(0x0d);e.emit_number("2");e.emit_op(0x48);
    for(int n=1;n<=3;++n){
      e.emit_op(0x69);e.emit_op(0x60+n);e.emit_jump(0x53,"independent helper invocation","operand_helper");
      e.emit_op(0x37);e.emit_op(0xb0);
      if(kind!=3)e.items.back().indirect_memory_targets=std::vector<int>{4,5,6};
      else e.items.back().raw=true;
      if(kind==1)e.emit_op(0x0a);
      e.emit_op(0xa8);e.items.back().indirect_flow_targets=
          std::vector<IrTarget>{std::string("good_callback"),std::string("unselected_callback")};
    }
    for(int r=10;r<14;++r)e.emit_op(0x60+r);e.emit_op(0x0f);e.emit_op(0x4e);
    e.emit_stop(StopDisposition::Terminal);
    e.emit_label("operand_helper");e.emit_op(0x22);e.emit_op(0x52);
    std::map<std::string,int> positions;int pc=0;
    for(const auto& item:e.items)if(item.kind==MachineItemKind::Label)positions[item.name]=pc;else ++pc;
    require(positions.at("good_callback")==2,"the selector target is before every rewritten cell");
    HelperInvariantRecallHoistOptions options;
    options.prefer_before_return_plan=true;options.allow_x_preserving_root=kind!=2;
    for(std::size_t i=0;i<e.items.size();++i)if(e.items[i].kind==MachineItemKind::Op && e.items[i].opcode==0xa8)
      options.proved_indirect_flow_targets[i]={positions.at("good_callback"),positions.at("unselected_callback")};
    if(kind!=5){options.complete_program_control=PostLayoutControlFlowOptions{};options.deferred_target_origins=positions;}
    if(kind==4)options.max_continuation_cells=1;
    const auto result=rewrite_helper_invariant_recall_hoist(e.items,"operand_helper",options);
    if(kind!=5 && (result.applied>0)!=(kind==0)){std::cerr<<"contextual ABI fixture kind="<<kind<<" applied="<<result.applied<<'\n';for(const auto& reason:result.proof.reasons)std::cerr<<reason<<'\n';}
    require(kind==5 || (result.applied>0)==(kind==0),
        "exact-context ABI proof removes an impossible callback, not X2 observation, another changed invocation, opaque code or exhaustion");
    if(kind==5)legacy_cells=result.applied?cells(result.items):cells(e.items);
    if(!result.applied)continue;
    if(kind==0)precise_cells=cells(result.items);
    require(result.proof.final_artifact_proved && result.proof.calls.size()==3U &&
        cells(result.items)<cells(e.items),"contextual continuation retains complete-family relocation and complete code cost");
    Fixture before{e.items,{}};
    for(int r=0;r<15;++r)before.preloads.push_back({.register_name=std::string(1,"0123456789abcde"[r]),
        .value=std::to_string(r==0?7:r+2)});
    require(observe({before.items,before.preloads,{}},-1)==observe({result.items,before.preloads,{}},-1),
        "ROM agrees on all physical memory, visible stack and explicit last-X after the complete helper ABI composition");
  }
  require(precise_cells<legacy_cells,"context-sensitive transfer keeps the older valid ABI and additionally proves a smaller one");
}
void stack_equality_work_bound(){
  MachineEmitter e;e.emit_op(0x61);e.emit_op(0x62);e.emit_op(0x63);e.emit_op(0x64);e.emit_op(0x22);
  e.emit_stop(StopDisposition::Terminal);const auto flow=build_post_layout_control_flow(e.items);
  const StackValueEqualityState initial{{false,false,false,false},false,false};
  require(!prove_post_layout_stack_entry_equality(e.items,flow,0,initial,{.maximum_work=0}),
      "zero work cannot export a partial stack equality certificate");
  require(prove_post_layout_stack_entry_equality(e.items,flow,0,initial,{.maximum_work=8}),
      "the same complete relational proof succeeds with enough finite work");
}
void retired_setup_home_pool(){
  CompileOptions options;options.analysis=true;options.defer_return_suffix_until_callee_hole=true;
  for(int kind=0;kind<2;++kind){
    auto f=fixture(Kind::Normal,8,0,true);
    for(auto& item:f.items)if(item.kind==MachineItemKind::Op && item.opcode==0x67)item.opcode=0x6e;
    if(kind==1){f.items.insert(f.items.begin()+2,MachineItem::op(0,"0"));f.items.insert(f.items.begin()+3,MachineItem::op(0x47,"changed source"));
      for(auto& item:f.items)if(item.opcode==0x50)item.roles.clear();}
    f.items.insert(f.items.begin(),MachineItem::op(0x4e,"temporary epoch in owned home"));
    f.items.insert(f.items.begin(),MachineItem::op(0x67,"source word"));
    const auto candidates=contextual_copy_transport_layout_transactions({f.items,f.preloads,{}},options);
    const auto pooled=std::find_if(candidates.begin(),candidates.end(),[](const auto& edge){
      return std::any_of(edge.node.optimizations.begin(),edge.node.optimizations.end(),[](const auto& report){return report.name=="retired-allocated-setup-homes";}) &&
          std::any_of(edge.node.preloads.begin(),edge.node.preloads.end(),[](const auto& word){return word.register_name=="e" && word.value=="88888834";});
    });
    require((pooled!=candidates.end())==(kind==0),
        "a proved eliminated epoch retires its literal owner, while public terminal-bank observations keep the old home live");
    if(kind==0)require(observe({f.items,f.preloads,{}},-1,true)==observe(pooled->node,-1,true),
        "ROM preserves every displayed result and subsequent stack value after setup ownership and pooling compose");
  }
}
void allocation_copy_cleanup_feedback(){
  MachineEmitter e;e.emit_op(0x61);e.emit_op(0x40);e.emit_op(0x62);e.emit_op(0x40);
  e.emit_op(0x60);e.emit_op(0x6a);e.emit_op(0x6b);e.emit_op(0x6c);e.emit_op(0x6d);
  e.emit_stop(StopDisposition::Terminal);
  const std::vector<PreloadReport> words{{.register_name="0",.value="31"},{.register_name="1",.value="7"},
      {.register_name="2",.value="11"},{.register_name="a",.value="13"},{.register_name="b",.value="17"},
      {.register_name="c",.value="19"},{.register_name="d",.value="23"}};
  CompileOptions options;options.analysis=true;const auto candidates=contextual_copy_transport_layout_transactions({e.items,words,{}},options);
  const auto cleaned=std::find_if(candidates.begin(),candidates.end(),[](const auto& edge){return std::any_of(
      edge.node.optimizations.begin(),edge.node.optimizations.end(),[](const auto& report){return report.name=="deferred-contextual-copy-store-elision";});});
  require(cleaned!=candidates.end() && cells(cleaned->node.items)<cells(e.items),
      "a new copy opportunity feeds back before any physical layout is frozen");
  require(observe({e.items,words,{}},-1,true)==observe(cleaned->node,-1,true),
      "store-only cleanup retains exact stack/entry effects and every future program observation in ROM");
}
void repaired_allocation_preferences(){
  const std::string source=R"(program RepairedAllocation {
    state {
      first: packed = 1
      second: packed = 2
    }
    show(first)
    show(second)
    halt(0)
  })";
  CompileOptions hard;hard.analysis=true;hard.budget=999999;hard.disable_candidate_search=true;
  hard.forced_logical_register_assignments={{.name="first",.register_name="0",.preload_owner=true},
      {.name="second",.register_name="0",.preload_owner=true}};
  require(!compile_source(source,hard).implemented,"conflicting inherited hard assignments remain invalid");
  auto collect=hard;collect.collect_logical_register_allocation=true;
  collect.repair_logical_register_assignment_preferences=true;
  const auto probe=compile_source(source,collect);
  require(probe.implemented && !probe.logical_register_assignments.empty(),"a changed graph can repair preferences without inheriting a sharing proof");
  auto rebuilt=hard;rebuilt.forced_logical_register_assignments=probe.logical_register_assignments;
  const auto program=compile_source(source,rebuilt);
  require(program.implemented && !program.steps.empty(),"the repaired assignment independently regenerates under ordinary hard verification");
  emulator::MK61 calc;std::vector<int> bytes;for(const auto& step:program.steps)bytes.push_back(step.opcode);
  require(calc.load_program(bytes).diagnostics.empty(),"repaired allocation fixture fits ROM");
  for(const auto& word:program.preloads)calc.set_register(word.register_name,word.value);
  calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});require(calc.run_until_stable(5000,8).stopped && std::stod(calc.read_register("X"))==1,
      "ROM keeps the first simultaneously live initializer");
  calc.press("\u0421/\u041f");require(calc.run_until_stable(5000,8).stopped && std::stod(calc.read_register("X"))==2,
      "ROM keeps the second initializer after the first stop");
}
void idempotent_input_prologue(){
  for(const int mode:{0,1,2,3})for(int kind=0;kind<8;++kind){
    const bool memory=mode!=0,compact=mode>=2;
    MachineEmitter e;e.emit_op(0x52);e.emit_label("main");if(mode==3&&kind==7){e.emit_op(0x6a);e.emit_op(0xb7);e.items.back().indirect_memory_targets=std::vector<int>{6};}e.emit_op(0x62);e.emit_op(0x63);e.emit_stop(StopDisposition::Resumable);
    e.items.back().manual_interaction=ManualInteractionAnchor{.protocol_id=0,.phase=-1,.kind=ManualInteractionAnchorKind::PromptStop};
    e.emit_op(compact?0x41:0x43);e.items.back().manual_interaction=ManualInteractionAnchor{.protocol_id=0,.phase=0,.kind=ManualInteractionAnchorKind::SingleStepCommand};
    e.emit_jump(0x53,"normalize second entered value","normalize");
    e.items[e.items.size()-2U].manual_interaction=ManualInteractionAnchor{.protocol_id=kind==5?1:0,.phase=1,.kind=ManualInteractionAnchorKind::ContinuousResume};
    e.emit_op(0x42);e.emit_op(compact?0x61:0x63);if(kind==3)e.items.back().raw=true;
    e.emit_jump(0x53,"normalize first entered value","normalize");if(memory)e.emit_op(0x41);e.emit_jump(0x53,"first ordinary entry","helper");
    e.emit_op(0x38);e.emit_op(0x49);e.emit_op(0x0d);
    if(compact){
      e.emit_op(3);e.emit_op(0x44);e.emit_op(2);e.emit_op(0x43);
      e.emit_op(0x63);e.emit_op(0x41);e.emit_op(kind==4?0x6a:0x64);e.emit_op(0x42);
    }else{
      if(kind==4)e.emit_op(0x6a);else e.emit_op(2);e.emit_op(0x43);e.emit_op(3);
      e.emit_op(0x42);e.emit_op(0x63);if(memory)e.emit_op(0x41);
    }
    e.emit_jump(0x53,"second ordinary entry","helper");
    e.emit_op(0x38);e.emit_op(0x49);if(mode==3&&kind!=6)for(int i=0;i<4;++i)e.emit_op(0x65);if(!memory)e.emit_op(0x61);e.emit_jump(0x53,"independent self-copy entry","helper");
    e.emit_stop(StopDisposition::Terminal);
    e.emit_label("normalize");e.emit_op(kind==1?0x35:(kind==6&&!compact)||(kind==7&&mode!=3)?0x0f:0x34);e.emit_op(0x52);
    if(kind==0)e.emit_op(0x54);
    e.emit_label("helper");if(mode==2&&kind!=6)for(int i=0;i<4;++i)e.emit_op(0x65);e.emit_op(memory?0x61:0x41);
    if(mode==3){e.emit_op(0x15);e.emit_op(0x62);e.emit_op(0x66);e.emit_op(0x12);e.emit_op(0x15);e.emit_op(0x34);if(kind==2)e.emit_op(0x0f);e.emit_op(0x10);e.emit_op(0x69);e.emit_op(0x14);}
    else{e.emit_op(0x62);if(kind==2)e.emit_op(0x0f);e.emit_op(0x10);}
    e.emit_op(0x52);
    const std::vector<PreloadReport> words{{.register_name="1",.value="0"},{.register_name="2",.value="0"},
        {.register_name="3",.value="0"},{.register_name="4",.value="0"},{.register_name="5",.value="9"},{.register_name="6",.value=mode==3?"1.25E-1":"0.125"},{.register_name="7",.value="6"},{.register_name="9",.value="0"}};
    PostLayoutControlFlowOptions control;control.empty_return_target=1;std::map<std::string,int> origins;int pc=0;
    for(const auto& item:e.items){if(item.kind==MachineItemKind::Label)origins.emplace(item.name,pc);else ++pc;}
    const auto candidates=passes::deferred_idempotent_entry_prologue_candidates(raise_machine_to_ir(e.items),words,control,origins);
    if ((!candidates.empty())!=(kind==0))std::cerr<<"ENTRY mode="<<mode<<" kind="<<kind<<" candidates="<<candidates.size()<<std::endl;
    require(!candidates.empty()==(kind==0),"only a pure total leaf with a complete idempotent input image and intact typed entry may share its prologue");
    if(kind!=0)continue;
    const auto changed=lower_ir_to_machine(candidates.front().ops);
    require(cells(changed)<=cells(e.items)-(compact?1:memory?3:1),"prologue and redundant self-copy removal save real cells");
    const auto changed_flow=build_post_layout_control_flow(changed,control);
    require(prepare_shared_helper_dual_mode_layout(changed,changed_flow,control.address_space_model).applied>0,
        "an unreachable padding envelope does not block composing the new prologue with a two-plus-one entry");
    int padding=-1,address=0;for(const auto& item:changed){if(item.kind==MachineItemKind::Label)continue;if(item.opcode==0x54)padding=address;++address;}
    require(padding>=0,"padding fixture remains explicit");auto entered_padding=control;entered_padding.main_entry=padding;
    const auto reached=build_post_layout_control_flow(changed,entered_padding);
    require(reached.proved && prepare_shared_helper_dual_mode_layout(changed,reached,entered_padding.address_space_model).applied==0,
        "a real entry into the padding is not certified as an unreachable envelope");
    const auto play=[&](const std::vector<MachineItem>& items,const char* x,const char* y){
      const auto image=resolve_machine_items(items);require(image.diagnostics.empty(),"idempotent prologue resolves");
      std::vector<int> bytes;for(const auto& step:image.steps)bytes.push_back(step.opcode);emulator::MK61 calc;
      require(calc.load_program(bytes).diagnostics.empty(),"prologue fixture fits stock ROM");for(const auto& word:words)calc.set_register(word.register_name,word.value);
      calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});require(calc.run_until_stable(5000,8).stopped,"input prompt retained");
      calc.input_number(x).press("\u041f\u041f");calc.input_number(y).press("\u0421/\u041f");
      require(calc.run_until_stable(5000,8).stopped,"both ordinary entries and third self-copy entry return");
      std::vector<std::string> state;for(const char* r:{"X","Y","Z","T","X1","1","2","3","6","9"})state.push_back(calc.read_register(r));return state;
    };
    for(const auto& pair:std::vector<std::pair<const char*,const char*>>{{"1","2"},{"3.75","-2.5"},{"0","7"}})
      require(play(e.items,pair.first,pair.second)==play(lower_ir_to_machine(candidates.front().ops),pair.first,pair.second),
          "ROM preserves normal and fractional/negative keyboard input, stack, last-X and all later program reads");
  }
}
void guarded_selected_value_ranges(){
  for(int kind=0;kind<4;++kind){
    MachineEmitter e;if(kind==0)for(int i=0;i<30;++i)e.emit_op(0x4e);
    e.emit_op(0x0d);e.emit_number("1");e.emit_op(0x0b);e.emit_op(0x40);
    if(kind==3){e.emit_op(0x60);e.emit_jump(0x59,"negative initializer excludes early caller","after_early");
      e.emit_jump(0x53,"infeasible initial definition context","read_selected");e.emit_label("after_early");}
    e.emit_number("4");e.emit_op(0x41);e.emit_label("scan");
    // An unrelated addressed write must not disable the whole-bank proof.
    e.emit_op(0x6e);e.emit_op(0xb7);e.items.back().indirect_memory_targets=std::vector<int>{4};
    e.emit_op(0x61);e.emit_op(0x60);e.emit_op(0x36);e.emit_op(0x40);e.emit_op(0x11);
    e.emit_jump(0x5e,"not selected","next");
    if(kind==1){e.emit_op(0x68);e.emit_jump(0x5e,"unknown skip of the selected output","next");}
    e.emit_op(0x61);e.emit_op(0x43);e.emit_label("next");
    e.emit_jump(0x5b,"counted scan","scan");e.emit_op(0x60);
    e.emit_jump(0x5c,"selected result","selected");e.emit_op(0);e.emit_stop(StopDisposition::Terminal);
    e.emit_label("selected");
    if(kind==2){e.emit_op(0x68);e.emit_op(0xbb);e.items.back().indirect_memory_targets=std::vector<int>{3};}
    if(kind==3)e.emit_jump(0x53,"selected definition context","read_selected");else e.emit_op(0x63);
    const auto use=e.items.size();e.emit_op(0x4a);e.emit_stop(StopDisposition::Terminal);
    if(kind==3){e.emit_label("read_selected");e.emit_op(0x63);e.emit_op(0x52);}
    std::vector<PreloadReport> words{{.register_name="3",.value="0"},{.register_name="7",.value="4"},
        {.register_name="b",.value="3"}};
    PostLayoutControlFlowOptions control;control.empty_return_target=1;
    const auto flow=build_post_layout_control_flow(e.items,control);
    const auto lift=analyze_post_layout_automatic_entry_lift_with_indirect_store_facts(e.items,flow,100000);
    require(flow.proved && lift.has_value(),"selected-value fixture has complete CFG and entry facts");
    const auto proof=prove_post_layout_numeric_ranges(e.items,flow,*lift,{use},&words,400000,
        NumericRangeInputPolicy::ValidatedRegisterTransport);
    const auto range=proof.x_before.find(use);
    const bool selected=range!=proof.x_before.end() && range->second.integral &&
        range->second.lower>=1 && range->second.upper<=4;
    require(selected==(kind==0 || kind==3),"only a complete selected definition image excludes the initial word; skipped copies and alias writes reject");
    bool canonical_memory=false;
    for(std::size_t s=0;s<flow.execution_states.size();++s)
      if(e.items[flow.execution_states[s].item_index].opcode==0x63 &&
          proof.canonical_memory_load_states.contains(s))canonical_memory=true;
    require(canonical_memory==(kind==0 || kind==3),
        "guarded selection certifies the raw source word, not just the normalized X result");
    bool excluded_read=false;
    for(std::size_t state=0;state<flow.execution_states.size();++state)
      if(e.items[flow.execution_states[state].item_index].opcode==0x63 &&
          proof.unreachable_states.contains(state))excluded_read=true;
    require(!excluded_read || kind==3,
        "excluded reads must belong to the impossible early caller; unknown values remain reachable");
    if(kind==0 || kind==3)require(std::stod(observe({e.items,words,{}},-1,true).front())==4,
        "ROM confirms the selected output despite an unrelated indirect write");
    if(kind==1){words.push_back({.register_name="8",.value="3"});
      require(std::stod(observe({e.items,words,{}},-1,true).front())==0,
          "ROM exposes the old initializer when a separate branch skips the output copy");}
  }
}
void dense_definition_rows(){
  for(const bool cyclic:{false,true}){
  MachineEmitter e;e.emit_op(0x0d);e.emit_op(1);e.emit_op(0x44);
  for(int i=0;i<(cyclic?110:45);++i)e.emit_op(0x45);
  for(int i=0;i<(cyclic?45:20);++i)e.emit_op(0x44);
  e.emit_op(0x68);e.emit_jump(0x5e,"two late reaching definitions","join");
  e.emit_op(2);e.emit_op(0x44);e.emit_label("join");e.emit_op(0x64);e.emit_op(0x46);
  e.emit_op(0x66);
  if(cyclic){
    e.emit_op(0x69);e.emit_jump(0x5e,"late definitions across a backedge","join");e.emit_op(0x66);
  }
  e.emit_stop(StopDisposition::Terminal);
  const auto ops=raise_machine_to_ir(e.items);std::size_t copy=ops.size();
  for(std::size_t i=1;i<ops.size();++i)if(ops[i-1U].kind==IrKind::Recall && ops[i-1U].register_name=="4" &&
      ops[i].kind==IrKind::Store && ops[i].register_name=="6")copy=i;
  require(copy<ops.size(),"dense-row copy anchor exists");
  CompileOptions options;options.coalesce_copies=true;PostLayoutControlFlowOptions control;control.empty_return_target=1;
  std::map<std::string,int> origins;int pc=0;for(const auto& item:e.items){if(item.kind==MachineItemKind::Label)origins.emplace(item.name,pc);else ++pc;}
  const std::vector<PreloadReport> environment;
  std::vector<std::vector<IrOp>> recolored;
  passes::for_each_deferred_register_web_recoloring(ops,{options},{{{copy-1U,copy}}},control,origins,
      [&](auto out){recolored.push_back(std::move(out));},&environment);
  require(!recolored.empty(),"dense definitions and cyclic liveness preserve both late reaching words");
  // The larger cyclic pre-layout fixture crosses two 64-bit words.
  // The physical-size fixture independently checks the same equations in ROM.
  if(!cyclic)for(const char* input:{"0","3"}){
    const std::vector<PreloadReport> words{{.register_name="8",.value=input}};
    require(observe({e.items,words,{}},-1,true)==observe({lower_ir_to_machine(recolored.front()),words,{}},-1,true),
        "ROM preserves both branches after dense-bit reaching and joint coloring");
  }
}
}
void memory_root_operand_abi(){
  for(const bool observe_shadow:{false,true}){
    MachineEmitter e;e.emit_op(0x52);e.emit_label("main");
    for(int call=0;call<3;++call){
      if(call==2)e.emit_op(0x69);
      e.emit_jump(0x53,"memory-backed helper","arithmetic");
      if(call<2){e.emit_op(0x69);e.emit_op(0x14);}
      e.emit_op(call==2?0x37:0x38);e.emit_op(0x4a);
      if(call<2){e.emit_op(0x11);e.emit_op(0x48);}
      if(observe_shadow)e.emit_op(0x0a);
      for(int i=0;i<4;++i)e.emit_op(0x67);
    }
    e.emit_stop(StopDisposition::Terminal);
    e.emit_label("arithmetic");e.emit_op(0x61);e.emit_op(0x62);e.emit_op(0x10);e.emit_op(0x52);
    PostLayoutControlFlowOptions c;c.empty_return_target=1;
    const auto flow=build_post_layout_control_flow(e.items,c);
    HelperInvariantRecallHoistOptions options;options.complete_program_control=c;
    options.allow_x_preserving_root=true;options.allow_swapped_return=true;
    options.allow_callsite_operand_swap=true;options.allow_before_call_commutative_tail=false;
    const auto root=rewrite_helper_invariant_recall_hoist(e.items,"arithmetic",options);
    if(observe_shadow){require(root.applied==0,"an observed decimal shadow cannot justify erasing a root caller swap");continue;}
    require(root.applied>0 && root.proof.proved && root.proof.final_artifact_proved &&
        root.proof.insertion==HelperInvariantRecallInsertion::HelperRoot && !root.proof.preserve_entry_x &&
        root.proof.erased_permutation_items.size()==2U && cells(root.items)<=cells(e.items)-4,
        "a stored-argument root jointly removes the two former result/common caller swaps");
    const std::vector<PreloadReport> words{{.register_name="1",.value="3"},{.register_name="2",.value="7"},
        {.register_name="7",.value="11"},{.register_name="9",.value="13"}};
    require(observe({e.items,words,{}},-1)==observe({root.items,words,{}},-1),
        "ROM preserves the retained operand used by subtraction, all memory reads and the full future stack");
  }
}
void exact_cfg_cache_contract(){
  const char* old=std::getenv("MKPRO_NATIVE_CFG_CACHE");
  const std::optional<std::string> previous=old ? std::optional<std::string>(old):std::nullopt;
  MachineEmitter e;e.emit_op(0x52);e.emit_label("main");e.emit_jump(0x53,"memoized call","leaf");
  e.emit_stop(StopDisposition::Terminal);e.emit_label("leaf");e.emit_op(0x60);e.emit_op(0x52);
  PostLayoutControlFlowOptions control;control.empty_return_target=1;
  const auto equal=[](const auto& a,const auto& b){
    return a.proved==b.proved && a.address_space_model==b.address_space_model &&
      a.external_entries==b.external_entries && a.indirect_flow_targets==b.indirect_flow_targets &&
      a.indirect_memory_targets==b.indirect_memory_targets && a.empty_return_target==b.empty_return_target &&
      a.execution_states==b.execution_states && a.execution_successors==b.execution_successors &&
      a.execution_edges==b.execution_edges && a.maximum_observed_return_depth==b.maximum_observed_return_depth &&
      a.explored_states==b.explored_states && a.reasons==b.reasons;
  };
  for(int kind=0;kind<9;++kind){
    auto items=e.items;auto options=control;
    if(kind==1)options.maximum_return_depth=0;
    if(kind==2)options.maximum_execution_states=1;
    if(kind==3)options.main_entry=std::string("leaf");
    if(kind==4)items[4].stop_disposition=StopDisposition::Resumable;
    if(kind==5)items[2].opcode=0x51;
    if(kind==6)options.address_space_model=AddressSpaceModel::Mk61SMiniExpanded;
    if(kind==7)options.opcode_address_words={2};
    if(kind==8)options.proved_indirect_formal_targets={{2,{0x99}}};
    ::setenv("MKPRO_NATIVE_CFG_CACHE","0",1);const auto oracle=build_post_layout_control_flow(items,options);
    ::setenv("MKPRO_NATIVE_CFG_CACHE","1",1);auto first=build_post_layout_control_flow(items,options);
    require(equal(first,oracle),"cached CFG preserves all options, metadata, exact counter/return frames and failure reasons");
    first.execution_states.clear();first.reasons.push_back("mutated private copy");
    require(equal(build_post_layout_control_flow(items,options),oracle),"returned CFG ownership cannot poison an exact cache entry");
  }
  if(previous)::setenv("MKPRO_NATIVE_CFG_CACHE",previous->c_str(),1);else ::unsetenv("MKPRO_NATIVE_CFG_CACHE");
}
void allocation_preferences_are_not_lowering_aliases(){
  const std::string source=R"MKPRO(program FreshAllocation {
    state { left: packed = 7
            right: packed = 11 }
    loop {
      show(left)
      right = 3
      left += right
      show(left)
      show(right)
    }
  })MKPRO";
  CompileOptions options;options.analysis=true;options.disable_candidate_search=true;
  options.forced_logical_register_assignments={{.name="left",.register_name="0",.preload_owner=true},
      {.name="right",.register_name="0",.preload_owner=true}};
  options.repair_logical_register_assignment_preferences=true;
  require(!compile_source(source,options).implemented,"repair hints never relax a non-collection hard assignment");
  options.collect_logical_register_allocation=true;options.logical_register_dead_store_elimination=true;
  options.preserve_logical_register_instruction_classes=true;
  const auto collected=compile_source(source,options);
  require(collected.implemented && !collected.logical_register_assignments.empty(),
      "fresh lowering must not fold distinct logical reads through an obsolete physical alias");
  options.collect_logical_register_allocation=false;options.repair_logical_register_assignment_preferences=false;
  options.forced_logical_register_assignments=collected.logical_register_assignments;
  const auto compiled=compile_source(source,options);require(compiled.implemented,"fresh preference assignment re-verifies as ordinary hard pins");
  std::vector<int> code;for(const auto& step:compiled.steps)code.push_back(step.opcode);emulator::MK61 calc;
  require(calc.load_program(code).diagnostics.empty(),"fresh preference fixture fits stock ROM");
  for(const auto& word:compiled.preloads)calc.set_register(word.register_name,word.value);
  calc.press_sequence({"В/О","С/П"});
  for(const char* expected:{"7,","10,","3,"}){
    require(calc.run_until_stable(5000,8).stopped && calc.read_register("X").starts_with(expected),
        "ROM preserves independent logical initialization, updates and later reads after repairing old aliases");
    calc.press("С/П");
  }
}
void proved_neutral_layout_materialization(){
  const std::string source=R"MKPRO(program NeutralLayout {
    state { value: packed = 7 }
    loop { show(value) }
  })MKPRO";
  CompileOptions options;options.analysis=true;options.disable_candidate_search=true;
  const auto root=compile_source(source,options);require(root.implemented,"neutral materialization source compiles");
  require(!materialize_proved_layout_for_testing(source,root,options,root.items,root.preloads,false),
      "ordinary publication still rejects a merely equal-size layout");
  for(int growth=0;growth<=5;++growth){
    auto items=root.items;for(int i=0;i<growth;++i)items.push_back(MachineItem::op(0x54,"unreachable proof padding"));
    const auto checkpoint=materialize_proved_layout_for_testing(source,root,options,items,root.preloads,true);
    require(checkpoint.has_value()==(growth<=4),"only proof-valid nonwinning layouts inside the original joint growth budget are materialized");
    if(!checkpoint)continue;
    const auto proof_only=materialize_proved_layout_for_testing(
        source,root,options,items,root.preloads,true,false);
    require(proof_only && same_layout_search_artifact(
        {checkpoint->items,checkpoint->preloads,{}},{proof_only->items,proof_only->preloads,{}}) &&
        checkpoint->hex==proof_only->hex && checkpoint->listing==proof_only->listing &&
        checkpoint->steps.size()==proof_only->steps.size() &&
        checkpoint->implemented==proof_only->implemented &&
        checkpoint->setup_hex==proof_only->setup_hex,
        "internal proof-only materialization preserves code, setup, validity and real cost");
    require(checkpoint->ir.layout_cells==static_cast<int>(checkpoint->steps.size()),
        "published materialization always rebuilds the complete current public report");
    require(checkpoint->steps.size()==root.steps.size()+static_cast<std::size_t>(growth),
        "an intermediate retains its real step count, never a synthetic comparison length");
    PostLayoutControlFlowOptions control;
    const auto before=build_post_layout_control_flow(root.items,control),after=build_post_layout_control_flow(checkpoint->items,control);
    std::vector<std::optional<std::size_t>> mapping(root.items.size());for(std::size_t i=0;i<mapping.size();++i)mapping[i]=i;
    require(before.proved && after.proved && prove_post_layout_execution_relocation(
        root.items,checkpoint->items,before,after,mapping).proved,
        "neutral/grown fixture carries the same exact rooted CFG and return frames");
    const auto play=[](const CompileResult& result){
      emulator::MK61 calc;std::vector<int> code;for(const auto& step:result.steps)code.push_back(step.opcode);
      require(calc.load_program(code).diagnostics.empty(),"neutral fixture fits stock ROM");
      for(const auto& word:result.preloads)calc.set_register(word.register_name,word.value);
      calc.press_sequence({"В/О","С/П"});std::vector<std::string> out;
      for(int stop=0;stop<2;++stop){require(calc.run_until_stable(5000,8).stopped,"neutral fixture retains resumable output");
        for(const char* r:{"X","Y","Z","T","X1"})out.push_back(calc.read_register(r));calc.press("С/П");}
      return out;
    };
    require(play(root)==play(*checkpoint),"ROM preserves neutral checkpoint output, stack and later resume behavior");
  }
  auto malformed=root.items;
  const auto operand=std::find_if(malformed.begin(),malformed.end(),[](const auto& i){return i.kind==MachineItemKind::Address;});
  require(operand!=malformed.end(),"loop fixture has an address operand");operand->target=std::string("missing_command");
  require(!materialize_proved_layout_for_testing(source,root,options,malformed,root.preloads,true),
      "the intermediate policy never bypasses address and final CFG validation");
}
void prologue_reopens_argument_lifetimes(){
  MachineEmitter e;e.emit_op(0x52);e.emit_label("main");e.emit_op(0x62);e.emit_op(0x69);e.emit_stop(StopDisposition::Resumable);
  e.items.back().manual_interaction=ManualInteractionAnchor{.protocol_id=0,.phase=-1,.kind=ManualInteractionAnchorKind::PromptStop};
  e.emit_op(0x41);e.items.back().manual_interaction=ManualInteractionAnchor{.protocol_id=0,.phase=0,.kind=ManualInteractionAnchorKind::SingleStepCommand};
  e.emit_jump(0x53,"normalize entered Y","normalize");
  e.items[e.items.size()-2U].manual_interaction=ManualInteractionAnchor{.protocol_id=0,.phase=1,.kind=ManualInteractionAnchorKind::ContinuousResume};
  e.emit_op(0x42);e.emit_op(0x61);e.emit_jump(0x53,"normalize entered X","normalize");e.emit_op(0x41);
  e.emit_jump(0x53,"first common entry","helper");e.emit_op(0x38);e.emit_op(0x49);
  e.emit_op(2);e.emit_op(0x40);e.emit_op(3);e.emit_op(0x44);
  e.emit_jump(0x53,"observe the old parameter while the chosen output is live","helper");e.emit_op(0x37);e.emit_op(0x48);
  e.emit_op(0x60);e.emit_op(0x41);e.emit_op(0x64);e.emit_op(0x42);
  e.emit_jump(0x53,"second common entry","helper");e.emit_op(0x38);e.emit_op(0x49);
  for(int i=0;i<4;++i)e.emit_op(0x67);e.emit_stop(StopDisposition::Terminal);
  e.items.back().roles.push_back(kTypedDisplayObservationRole);
  e.emit_label("normalize");e.emit_op(0x34);e.emit_op(0x52);
  e.emit_label("helper");for(int i=0;i<4;++i)e.emit_op(0x65);e.emit_op(0x61);e.emit_op(0x62);e.emit_op(0x10);e.emit_op(0x52);
  const std::vector<PreloadReport> words{{.register_name="1",.value="0"},{.register_name="2",.value="0"},
      {.register_name="5",.value="9"},{.register_name="7",.value="11"},{.register_name="9",.value="0"}};
  PostLayoutControlFlowOptions control;control.empty_return_target=1;
  const auto origins=[](const std::vector<MachineItem>& items){std::map<std::string,int> result;int pc=0;
    for(const auto& i:items){if(i.kind==MachineItemKind::Label)result.emplace(i.name,pc);else ++pc;}return result;};
  CompileOptions options;options.coalesce_copies=true;options.defer_return_suffix_until_callee_hole=true;
  const auto code=raise_machine_to_ir(e.items);
  const auto shared=passes::deferred_idempotent_entry_prologue_candidates(code,words,control,origins(e.items));
  require(!shared.empty(),"memory-backed entry factoring exposes a separate scalar argument web");
  const auto factored=lower_ir_to_machine(shared.front().ops);
  const auto after=passes::deferred_register_web_copy_coalesce(shared.front().ops,{options},control,origins(factored),words);
  require(after.applied>0 && cells(lower_ir_to_machine(after.ops))<cells(factored),
      "allocation must consume the new lifetime proof before geometry is frozen");
  const auto play=[&](const std::vector<MachineItem>& items){
    const auto image=resolve_machine_items(items);require(image.diagnostics.empty(),"ABI-feedback fixture resolves");
    std::vector<int> bytes;for(const auto& s:image.steps)bytes.push_back(s.opcode);emulator::MK61 calc;
    require(calc.load_program(bytes).diagnostics.empty(),"ABI-feedback fixture fits stock ROM");
    for(const auto& w:words)calc.set_register(w.register_name,w.value);
    calc.press_sequence({"В/О","С/П"});require(calc.run_until_stable(5000,8).stopped,"ABI-feedback prompt retained");
    calc.input_number("4.75").press("ПП");calc.input_number("2.5").press("С/П");
    require(calc.run_until_stable(5000,8).stopped,"all three entries and their distinct continuations return");
    std::vector<std::string> out;for(const char* r:{"X","Y","Z","T","X1"})out.push_back(calc.read_register(r));return out;
  };
  require(play(e.items)==play(lower_ir_to_machine(after.ops)),"ROM preserves the complete public continuation after entry/lifetime feedback");
  const auto requests = passes::idempotent_entry_prologue_register_requests(code);
  bool source_demand = false;
  for (const auto& request : requests)
    for (const auto& [left, right] : request)
      if (code[left].kind == IrKind::Recall && code[left].register_name == "1" &&
          code[right].kind == IrKind::Recall && code[right].register_name == "0")
        source_demand = true;
  require(source_demand, "resident-source factoring demands the incoming source web, not its overwritten destination");
  std::optional<std::vector<MachineItem>> resident;
  passes::for_each_deferred_register_web_recoloring(
      code, {options}, requests, control, origins(e.items), [&](const auto& colored) {
        for (const auto& candidate : passes::deferred_idempotent_entry_prologue_candidates(
                 colored, words, control, origins(e.items))) {
          auto items = lower_ir_to_machine(candidate.ops);
          if (cells(items) <= cells(e.items) - 3 &&
              (!resident || cells(items) < cells(*resident))) resident = std::move(items);
        }
      }, &words, true);
  require(resident.has_value(), "whole source-copy removal composes recoloring and entry ABI for at least three cells");
  require(play(e.items) == play(*resident),
          "ROM retains the two-phase UI and complete later stack observation after resident-source entry factoring");

  auto frozen_formal = shared.front().ops;
  bool froze_call = false;
  for (auto& op : frozen_formal) {
    if (op.kind != IrKind::Call || !std::holds_alternative<std::string>(op.target)) continue;
    const auto physical = origins(factored).at(std::get<std::string>(op.target));
    if (physical < 0 || physical > 99) continue;
    op.target_meta.formal_opcode = (physical / 10) * 16 + physical % 10;
    froze_call = true;
    break;
  }
  require(froze_call,"the frozen-entry negative fixture owns a real formal operand");
  require(passes::deferred_register_web_copy_coalesce(
      frozen_formal,{options},control,origins(factored),words).applied==0,
      "copy removal cannot move an encoded formal entry or a side-space boundary");
  const auto joint = search_joint_representation_constraints(
      {e.items,words,{}},options,{},
      {.maximum_expansions=6,.maximum_candidates=24,.maximum_depth=4,.maximum_growth_cells=4},true);
  require(joint.incumbent && cells(joint.incumbent->items) <= cells(lower_ir_to_machine(after.ops)),
      "the public joint frontier must compose entry ABI and newly exposed lifetime copies");
  const auto joint_flow = build_post_layout_control_flow(joint.incumbent->items,control);
  require(joint_flow.proved && prove_post_layout_input_boundaries(joint.incumbent->items,joint_flow).proved,
      "the joint winner closes every call/return and retains both typed input phases");
  const auto original_observation=play(e.items), joint_observation=play(joint.incumbent->items);
  if(!std::equal(original_observation.begin(),original_observation.begin()+2,joint_observation.begin())){
    std::cerr<<"joint ABI original:";for(const auto& value:original_observation)std::cerr<<' '<<value;
    std::cerr<<"\njoint ABI selected:";for(const auto& value:joint_observation)std::cerr<<' '<<value;std::cerr<<'\n';
    for(const auto& report:joint.incumbent->optimizations)std::cerr<<report.name<<": "<<report.detail<<'\n';
  }
  require(std::equal(original_observation.begin(),original_observation.begin()+2,joint_observation.begin()),
      "the automatically selected ABI/allocation/layout composition preserves declared X/Y observations");
  for(const bool observe_last_x:{false,true}){
    auto observing=e.items;
    const auto stop=std::find_if(observing.begin(),observing.end(),[](const auto& item){
      return item.kind==MachineItemKind::Op && item.opcode==0x50 && item.stop_disposition==StopDisposition::Terminal;});
    require(stop!=observing.end(),"future-stack fixture has a terminal display");
    const auto at=static_cast<std::size_t>(stop-observing.begin());
    const auto commands=observe_last_x?std::vector<int>{0x0f}:std::vector<int>{0x25,0x25};
    std::vector<MachineItem> reads;
    for(const int opcode:commands)reads.push_back(MachineItem::op(opcode,"explicit later stack read"));
    observing.insert(observing.begin()+static_cast<std::ptrdiff_t>(at),reads.begin(),reads.end());
    const auto checked=search_joint_representation_constraints({observing,words,{}},options,{},
        {.maximum_expansions=6,.maximum_candidates=24,.maximum_depth=4,.maximum_growth_cells=4},true);
    if(!checked.incumbent)continue;
    const auto before=play(observing), selected=play(checked.incumbent->items);
    require(std::equal(before.begin(),before.begin()+2,selected.begin()),
        "later rotation or last-X makes the formerly private slots observable and blocks unsafe erasure");
  }
}

void reversible_dual_mode_continuation(){
  for(const bool trailing_store:{false,true}){
    MachineEmitter e;e.emit_op(0x52);e.emit_label("public_entry");
    const auto common=[&]{e.emit_jump(0x53,"ordinary helper invocation","numeric_leaf");
      e.emit_op(0x38);e.emit_op(0x49);if(trailing_store)e.emit_op(0x42);};
    e.emit_op(0x60);common();e.emit_op(0x61);
    e.emit_jump(0x53,"divergent helper invocation","numeric_leaf");e.emit_op(0x37);e.emit_op(0x4a);
    e.emit_op(0x60);common();for(int i=0;i<4;++i)e.emit_op(0x67);
    e.emit_stop(StopDisposition::Terminal);
    const int tail=trailing_store?3:2;
    while(cells(e.items)<41+2*tail)e.emit_op(0x54);
    e.emit_label("numeric_leaf");for(int i=0;i<7;++i)e.emit_op(0x31);e.emit_op(0x52);
    const std::vector<PreloadReport> words{{.register_name="0",.value="3"},{.register_name="1",.value="5"},
        {.register_name="7",.value="11"},{.register_name="9",.value="0"}};
    PostLayoutControlFlowOptions control;control.empty_return_target=1;
    const auto flow=build_post_layout_control_flow(e.items,control);
    const auto prepared=prepare_shared_helper_dual_mode_layout(e.items,flow);
    require(prepared.applied>0,"generic three-call continuation prepares without source names");
    const auto folded=finalize_shared_helper_dual_mode_layout(
        prepared,prepared.items,words,build_post_layout_control_flow(prepared.items,control));
    require(folded.applied>0 && folded.proof.proved,"fixture reaches the real side-space layout");
    const auto reopened=reopen_shared_helper_dual_mode_layout(folded.items,control);
    if(!reopened.applied)for(const auto& reason:reopened.reasons)std::cerr<<"reopening: "<<reason<<'\n';
    require(reopened.applied>0 && reopened.continuation.proved && reopened.added_cells==tail &&
        cells(reopened.items)==cells(folded.items)+tail,"inverse law charges every duplicated cell");
    require(observe({e.items,words,{}},-1)==observe({folded.items,folded.preloads,{}},-1) &&
        observe({e.items,words,{}},-1)==observe({reopened.items,words,{}},-1),
        "ROM preserves full stack, all future memory reads and all data registers through fold and reopen");
    for(const auto kind:{0,1,2,3}){
      auto invalid=folded.items;
      const auto shared=std::find_if(invalid.begin(),invalid.end(),[](const auto& item){
        return std::any_of(item.roles.begin(),item.roles.end(),[](const auto& role){return role.ends_with(":shared-join");});});
      require(shared!=invalid.end(),"negative fixture owns a genuine shared tail");
      if(kind==0)shared->raw=true;
      if(kind==1){
        auto call=std::find_if(invalid.begin(),invalid.end(),[](const auto& item){
          return std::any_of(item.roles.begin(),item.roles.end(),[](const auto& role){return role.ends_with(":divergent-call");});});
        require(call!=invalid.end(),"divergent caller identity exists");
        const auto address=std::next(call);
        require(address!=invalid.end() && address->formal_opcode,"divergent operand exists");
        address->formal_opcode=0;
      }
      if(kind==2)shared->roles.clear();
      if(kind==3){
        const auto source=std::find_if(invalid.begin(),invalid.end(),[](const auto& item){return item.opcode==0x61;});
        require(source!=invalid.end(),"first later recall exists");source->opcode=0x0a;source->mnemonic=".";
      }
      require(reopen_shared_helper_dual_mode_layout(invalid,control).applied==0,
          "raw, mismatched formal, incomplete provenance and observable X2 reject reopening");
    }
  }
}


void composition_depth_scheduling(){
  const auto regular_limits=joint_representation_search_limits(false);
  const auto fast_limits=joint_representation_search_limits(true);
  require(regular_limits.maximum_expansions==kJointRepresentationConstraintLimits.maximum_expansions &&
      regular_limits.maximum_candidates==kJointRepresentationConstraintLimits.maximum_candidates &&
      regular_limits.maximum_depth==kJointRepresentationConstraintLimits.maximum_depth,
      "ordinary joint search keeps every historical exploration budget");
  require(fast_limits.maximum_expansions<regular_limits.maximum_expansions &&
      fast_limits.maximum_candidates<regular_limits.maximum_candidates &&
      fast_limits.maximum_depth<regular_limits.maximum_depth &&
      fast_limits.maximum_growth_cells==regular_limits.maximum_growth_cells,
      "an explicitly fast portfolio bounds exploration only, never changes growth accounting or proof semantics");

  struct Node{int cells=100;int stage=0;int family=0;};
  const auto cells=[](const Node& node){return node.cells;};
  const auto equal=[](const Node& a,const Node& b){
    return a.cells==b.cells && a.stage==b.stage && a.family==b.family;
  };
  const auto run=[&](bool compose,bool prove_intermediate=true,int intermediate_cells=102){
    std::vector<ProvedSearchTransition<Node>> seeds;
    seeds.push_back({{102,1,0},"representation",true});
    for(int i=1;i<6;++i)seeds.push_back({{100,1,i},"independent-"+std::to_string(i),true});
    TransactionSearchLimits limits{.maximum_expansions=6,.maximum_candidates=24,
        .maximum_depth=4,.maximum_growth_cells=4,.prioritize_composition_depth=compose};
    return search_proved_transaction_closures(std::move(seeds),std::vector<Node>{{100,0,0}},
        100,limits,cells,equal,[&](const Node& parent){
          std::vector<ProvedSearchTransition<Node>> children;
          if(parent.family==0 && parent.stage==1)
            children.push_back({{intermediate_cells,2,0},"allocation",prove_intermediate});
          if(parent.family==0 && parent.stage==2)
            children.push_back({{90,3,0},"shared-layout",true});
          return children;
        },[](const Node& node,const std::vector<std::string>&){
          return node.stage==3?std::vector<Node>{node}:std::vector<Node>{};
        });
  };
  const auto historical=run(false),composed=run(true);
  require(!historical.incumbent && historical.expanded==6,
      "breadth policy spends the fixed budget on independent first-generation families");
  require(composed.incumbent && composed.incumbent->cells==90 && composed.expanded<=6,
      "composition policy reaches a proved three-technique closure in the same budget");
  require(!run(true,false).incumbent && !run(true,true,105).incumbent,
      "deeper scheduling never bypasses equivalence or immutable-root growth admission");
  const auto repeated=run(true);
  require(repeated.incumbent && repeated.expanded==composed.expanded &&
      repeated.admitted==composed.admitted && repeated.incumbent->cells==composed.incumbent->cells,
      "composition scheduling is deterministic");
}


void exact_finalization_memo(){
  struct Node{int code=0;int proof=0;int environment=0;int report=0;};
  int calls=0;
  const auto callback=[&](const Node& n)->std::optional<Node>{
    ++calls;if(n.proof<0)return std::nullopt;
    auto result=n;result.code+=10;return result;
  };
  const auto equal=[](const Node&a,const Node&b){
    return a.code==b.code && a.proof==b.proof && a.environment==b.environment && a.report==b.report;
  };
  ExactSearchFinalizerMemo<Node,decltype(callback),decltype(equal)> memo(callback,equal,2);
  const Node root{1,2,3,4};
  auto first=memo(root);require(first && calls==1,"first complete query invokes the finalizer");
  first->code=99;
  const auto second=memo(root);
  require(second && second->code==11 && calls==1 && memo.hits()==1,
      "memoized proof results are independent copies");
  for(int changed=0;changed<3;++changed){
    auto next=root;if(changed==0)++next.proof;if(changed==1)++next.environment;if(changed==2)++next.report;
    require(memo(next).has_value(),"different complete proof environments are independently evaluated");
  }
  require(calls==4 && memo.evaluations()==4,"proof, environment and report changes cannot hit a stale proof");
  require(memo(root).has_value() && calls==5,"bounded eviction only recomputes a complete query");
  const Node invalid{1,-1,3,4};
  require(!memo(invalid) && !memo(invalid) && calls==6,"a complete failed query is reusable, not a partial iterate");
  ExactSearchFinalizerMemo<Node,decltype(callback),decltype(equal)> uncached(callback,equal,0);
  require(uncached(root).has_value() && uncached(root).has_value() && calls==8,
      "zero capacity preserves uncached callback behavior");
}


void neutral_helper_abi(){
  MachineEmitter e;e.emit_op(0x69);
  e.emit_jump(0x53,"single neutral ABI call","arithmetic");e.emit_op(0x4a);
  e.emit_op(0x6a);e.emit_stop(StopDisposition::Terminal);
  e.emit_label("arithmetic");e.emit_op(0x61);e.emit_op(0x62);e.emit_op(0x10);e.emit_op(0x52);
  HelperInvariantRecallHoistOptions options;
  options.allow_before_call_commutative_tail=false;
  require(rewrite_helper_invariant_recall_hoist(e.items,"arithmetic",options).applied==0,
      "the historical greedy ABI still rejects a neutral local rewrite");
  options.allow_size_neutral=true;
  const auto candidates=helper_invariant_recall_hoist_candidates(e.items,options,4);
  require(candidates.size()==1 && candidates.front().proof.output_cells==cells(e.items) &&
      candidates.front().proof.proved && candidates.front().proof.final_artifact_proved,
      "an independently proved same-size ABI survives for joint placement");
  const std::vector<PreloadReport> words{{.register_name="1",.value="3"},{.register_name="2",.value="7"},
      {.register_name="9",.value="13"}};
  require(observe({e.items,words,{}},-1)==observe({candidates.front().items,words,{}},-1),
      "the neutral ABI preserves stack, future reads, X1 and physical registers in ROM");
  require(helper_invariant_recall_hoist_candidates(e.items,options,0).empty(),
      "zero candidate budget exports no neutral ABI");
  auto opaque=e.items;
  for(auto&item:opaque)if(item.kind==MachineItemKind::Op && item.opcode==0x69)item.raw=true;
  require(helper_invariant_recall_hoist_candidates(opaque,options,4).empty(),
      "neutral profitability never relaxes a raw/proof barrier");

  MachineEmitter growth;growth.emit_op(0x69);growth.emit_op(0x61);
  growth.emit_jump(0x53,"growing input ABI","arithmetic");
  growth.emit_op(0x4a);growth.emit_op(0x6a);growth.emit_stop(StopDisposition::Terminal);
  growth.emit_label("arithmetic");growth.emit_op(0x22);growth.emit_op(0x61);
  growth.emit_op(0x10);growth.emit_op(0x52);
  options.allow_x_preserving_root=true;
  require(helper_invariant_recall_hoist_candidates(growth.items,options,4).empty(),
      "neutral opt-in does not admit an extra-cell argument-preserving ABI");
}


void x2_join_idempotence(){
  const auto initial=passes::empty_x2_value_dataflow_state(true);
  for(const int opcode:{0x61,0x49,0x14,0x25,0x10,0x37,0x52,0xe7}){
    IrOp op;op.kind=opcode==0x61?IrKind::Recall:opcode==0x49?IrKind::Store:IrKind::Plain;
    op.opcode=opcode;op.register_name=opcode==0x61?"1":"9";
    for(const auto edge:{passes::X2DataflowEdgeKind::Normal,
        passes::X2DataflowEdgeKind::Fallthrough,passes::X2DataflowEdgeKind::Jump}){
      const auto output=passes::transfer_x2_value_state_for_edge(
          initial,op,edge,{.track_register_memory=true},17);
      if(!output)continue;
      const auto joined=passes::join_x2_value_dataflow_states(*output,*output,true);
      require(passes::same_x2_value_dataflow_state(*output,joined),
          "an identical complete X2 state is an idempotent join, including memory/shape components");
      const auto repeated=passes::transfer_x2_value_state_for_edge(
          initial,op,edge,{.track_register_memory=true},17);
      require(repeated && passes::same_x2_value_dataflow_state(*output,*repeated),
          "a command transfer is pure for the same complete input and labelled edge kind");
    }
  }
}



void contextual_word_identity(){
  CompileOptions options;options.analysis=true;
  for(const int kind:{0,1,2,3,4,5,6,7,8,9,10}){
    MachineEmitter e;
    e.emit_number("5");e.emit_op(0x41);
    e.emit_jump(0x53,"first caller: copied result is dead","shared_copy");
    if(kind==5){e.emit_op(0x62);e.emit_op(0x4b);}
    if(kind==8 || kind==9){
      e.emit_op(4);e.emit_op(0x42);
      e.emit_jump(0x58,"destination retains a denormalized counter word","counter_done");
      e.emit_label("counter_done");e.emit_op(0x62);e.emit_op(0x41);
    }else{
      if(kind==7){
        e.emit_op(4);e.emit_op(0x43);
        e.emit_jump(0x5a,"normalizing load of a counter","counter_done");
        e.emit_label("counter_done");e.emit_op(0x63);
      }else if(kind==2 || kind==4 || kind==10)e.emit_op(0x63);
      else e.emit_number(kind==3?"3.25":"3");
      e.emit_op(0x42);e.emit_op(0x62);e.emit_op(0x41);
    }
    e.emit_jump(0x53,"second caller: destination already has the same recall view","shared_copy");
    if(kind==9){e.emit_op(0xd2);e.items.back().indirect_memory_targets=std::vector<int>{2};}
    if(kind==8 || kind==9){e.emit_op(0x62);e.emit_op(0x42);}
    for(int i=0;i<4;++i)e.emit_op(0x62);
    if(kind==5)e.emit_op(0x6b);
    e.emit_stop(StopDisposition::Terminal);
    e.emit_label("shared_copy");
    e.emit_op(0x61);e.emit_op(0x54);
    e.emit_jump(0x53,"context-dependent pure result","pure_leaf");
    e.emit_op(0x41);e.emit_op(0x61);e.emit_op(0x42);e.emit_op(0x52);
    e.emit_label("pure_leaf");
    if(kind==1){e.emit_op(1);e.emit_op(0x10);}
    else for(const int code:{0x34,4,0x13,0x35,1,0x11,0x35,1,0x10,4,0x12})e.emit_op(code);
    if(kind==6)e.items.back().raw=true;
    e.emit_op(0x52);
    std::vector<PreloadReport> words;
    for(int r=0;r<15;++r)if(kind!=2 || r!=3)
      words.push_back({.register_name=std::string(1,"0123456789abcde"[r]),
          .value=r==3 && kind==4?"30E-1":r==3 && kind==10?"AE-2":std::to_string(r+17)});
    const auto ops=raise_machine_to_ir(e.items);
    const auto candidates=passes::contextual_redundant_copy_store_candidates(
        ops,{options},{.runtime_preloads=&words});
    const bool positive=kind==0 || kind==4 || kind==7 || kind==8;
    require(!candidates.empty()==positive,
        "shared store erasure composes dead callers, total finite identities and normalized recall views; "
        "changed, unknown, fractional, observed, raw and selector-sensitive cases reject");
    if(candidates.empty())continue;
    require(candidates.size()==1U,"one independently proved shared store is redundant");
    const auto output=lower_ir_to_machine(candidates.front().transformation.ops);
    require(cells(output)==cells(e.items)-1,"only the redundant one-cell store is erased");
    require(observe({e.items,words,{}},-1)==observe({output,words,{}},-1),
        "ROM preserves display, all stack slots and every physical register after later normalization");
    const auto& detail=candidates.front().transformation.optimizations.front().detail;
    require(detail.find("1 finite, total")!=std::string::npos,
        "the same physical helper is an identity only in its canonical second caller");
    if(kind==8)require(detail.find("1 recall-view contexts")!=std::string::npos,
        "a denormalized destination is not mistaken for an equal raw word");
    require(passes::contextual_redundant_copy_store_candidates(ops,{options}).empty(),
        "missing complete runtime environment never grants an identity");
    require(passes::contextual_redundant_copy_store_candidates(
        ops,{options},{.maximum_work=1,.runtime_preloads=&words}).empty(),
        "budget exhaustion discards every partial equality/identity fact");
    if(kind==8){
      const auto closed=contextual_copy_transport_layout_transactions({e.items,words,{}},options);
      require(!closed.empty() && cells(closed.front().node.items)<cells(e.items),
          "normalizing-load GVN participates in complete ABI/allocation/layout closure");
      const auto before=observe({e.items,words,{}},-1,true);
      require(before==observe(closed.front().node,-1,true),
          "joint normalizing GVN preserves the visible result after future explicit reads");
    }
    if(kind==0){
      auto malformed=ops;
      const auto read=std::find_if(malformed.begin(),malformed.end(),[](const auto& op){return op.kind==IrKind::Recall;});
      require(read!=malformed.end(),"malformed-operand fixture has a recall");
      read->register_name="b";
      require(passes::contextual_redundant_copy_store_candidates(
          malformed,{options},{.runtime_preloads=&words}).empty(),
          "numeric facts cannot be attached to an inconsistent physical register operand");
    }
    auto duplicate=words;duplicate.push_back(words.front());
    require(passes::contextual_redundant_copy_store_candidates(
        ops,{options},{.runtime_preloads=&duplicate}).empty(),
        "conflicting initializer ownership cannot seed a finite domain");
  }

  // Counter storage is not normalized by decrement. Recall is a separate
  // machine operation and must not make raw memory slots equal in the GVN.
  constexpr int loops[]{0x5d,0x5b,0x58,0x5a};
  for(int r=0;r<4;++r)for(int n=1;n<=9;++n){
    MachineEmitter e;e.emit_jump(loops[r],"positive counter","taken");
    e.emit_stop(StopDisposition::Terminal);
    e.emit_label("taken");e.emit_stop(StopDisposition::Terminal);
    const auto image=resolve_machine_items(e.items);
    require(image.diagnostics.empty(),"finite FL word fixture resolves");
    std::vector<int> bytes;for(const auto& step:image.steps)bytes.push_back(step.opcode);
    emulator::MK61 calc;require(calc.load_program(bytes).diagnostics.empty(),"finite FL word fixture fits");
    const std::string name(1,"0123"[r]);calc.set_register(name,std::to_string(n));
    const auto digit=std::to_string(n>1?n-1:1);
    const auto expected=n==1?digit+",":std::string(8U-digit.size(),'0')+digit+",";
    calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});
    require(calc.run_until_stable(5000,8).stopped && calc.read_register(name)==expected,
        "FL exit preserves one but decrement writes an eight-digit denormalized word");
  }
  for(int r=0;r<7;++r)for(int n=1;n<=9;++n){
    emulator::MK61 calc;
    require(calc.load_program({0xd0+r,0x50}).diagnostics.empty(),"stock indirect counter fixture fits");
    const std::string name(1,"0123456"[r]);calc.set_register(name,std::to_string(n));
    const auto digit=std::to_string(n+(r<4?-1:1));
    const auto expected=std::string(8U-digit.size(),'0')+digit+",";
    calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});
    require(calc.run_until_stable(5000,8).stopped && calc.read_register(name)==expected,
        "indirect counter writeback creates an eight-digit denormalized word");
  }

  // All possible positions of the only nonzero mantissa digit, across the
  // complete stock bank. Check the stored loaded word, not merely display
  // formatting: normalizing recall leaves its source memory unchanged.
  for(int r=0;r<15;++r)for(int n=1;n<=9;++n){
    const int destination=(r+1)%15;
    const std::string source_name(1,"0123456789abcde"[r]);
    const std::string destination_name(1,"0123456789abcde"[destination]);
    const std::vector<int> code{0x60+r,0x40+destination,0x50};
    emulator::MK61 expected;
    require(expected.load_program(code).diagnostics.empty(),"canonical recall reference fits");
    expected.set_register(source_name,std::to_string(n));
    expected.press_sequence({"\u0412/\u041e","\u0421/\u041f"});
    require(expected.run_until_stable(5000,8).stopped,"canonical reference stops");
    for(std::size_t leading=0;leading<8U;++leading){
      emulator::MK61 calc;
      require(calc.load_program(code).diagnostics.empty(),"normalizing recall fixture fits");
      const auto trailing=7U-leading;
      const auto word=std::string(leading,'0')+std::to_string(n)+std::string(trailing,'0')+
          (trailing?"E-"+std::to_string(trailing):std::string{});
      calc.set_register(source_name,word);
      const auto original_word=calc.read_register(source_name);
      calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});
      require(calc.run_until_stable(5000,8).stopped &&
          calc.read_register(source_name)==original_word &&
          calc.read_register(destination_name)==expected.read_register(destination_name) &&
          calc.read_register("X")==expected.read_register("X") &&
          calc.display_text()==expected.display_text() &&
          calc.program_counter()==expected.program_counter(),
          "every ordinary one-digit integer recall normalizes its loaded word, not its source memory");
    }
  }
}


void transitive_value_origin_preferences(){
  CompileOptions options;options.analysis=true;options.coalesce_copies=true;
  for(const int kind:{0,1,2}){
    MachineEmitter e;
    e.emit_op(0x69);e.emit_op(0x40,std::nullopt,"origin-definition");
    e.emit_op(0x60);e.emit_jump(0x53,"arbitrary producer helper","arbitrary_leaf");
    e.emit_op(0x41);e.emit_op(0x61);e.emit_op(0x4a,std::nullopt,"origin-sink");
    if(kind==2){e.emit_op(0x60);e.emit_op(0x4b);}
    for(int i=0;i<4;++i)e.emit_op(0x6a);
    if(kind==2)e.emit_op(0x6b);
    e.emit_stop(StopDisposition::Terminal);
    e.emit_label("arbitrary_leaf");
    if(kind==0){e.emit_op(1);e.emit_op(0x12);}else e.emit_op(0x22);
    e.emit_op(0x52);
    const auto ops=raise_machine_to_ir(e.items);
    std::size_t definition=ops.size(),sink=ops.size();
    for(std::size_t i=0;i<ops.size();++i){
      if(ops[i].meta.comment=="origin-definition")definition=i;
      if(ops[i].meta.comment=="origin-sink")sink=i;
    }
    require(definition<ops.size() && sink<ops.size(),"origin fixture has independent definition sites");
    const auto requests=passes::contextual_copy_origin_register_requests(ops,16);
    const auto wanted=std::find_if(requests.begin(),requests.end(),[&](const auto& request){
      return request.size()==1U && request.front()==std::pair{sink,definition};
    });
    require(wanted!=requests.end(),"nonadjacent copy provenance proposes a complete definition-web equality");
    require(passes::contextual_copy_origin_register_requests(ops,0).empty(),
        "zero preference budget creates no origin allocation requests");
    PostLayoutControlFlowOptions control;
    std::map<std::string,int> origins;int pc=0;
    for(const auto& item:e.items){if(item.kind==MachineItemKind::Label)origins[item.name]=pc;else ++pc;}
    const std::vector<PreloadReport> words{{.register_name="9",.value="3"}};
    std::vector<std::vector<IrOp>> recolored;
    passes::for_each_deferred_register_web_recoloring(ops,{options},{*wanted},control,origins,
        [&](auto candidate){recolored.push_back(std::move(candidate));},&words,true);
    require(!recolored.empty()==(kind!=2),
        "a preference never permits merging a distinct still-observed origin with its transformed result");
    bool erased=false;
    for(const auto& candidate:recolored){
      const auto before=observe({e.items,words,{}},-1,true);
      const auto after=observe({lower_ir_to_machine(candidate),words,{}},-1,true);
      require(before==after,"complete-web origin allocation preserves ROM X/Y and every future read");
      const auto reduced=passes::contextual_redundant_copy_store_candidates(
          candidate,{options},{.runtime_preloads=&words});
      if(kind==1)require(reduced.empty(),
          "provenance is not an identity assertion: a squaring helper still needs its result store");
      for(const auto& result:reduced){
        erased=true;
        require(cells(lower_ir_to_machine(result.transformation.ops))<cells(e.items) &&
            observe({lower_ir_to_machine(result.transformation.ops),words,{}},-1,true)==before,
            "allocation plus separately proved contextual GVN produces an actual ROM-equivalent saving");
      }
    }
    if(kind==0)require(erased,"an independently proved identity enables store erasure only after allocation");
  }

  MachineEmitter fair;
  for(int i=0;i<20;++i){fair.emit_op(0x69);fair.emit_op(0x40);}
  fair.emit_op(0x60);fair.emit_op(0x41);fair.emit_op(0x61);fair.emit_op(0x4a);
  const auto first_sink=raise_machine_to_ir(fair.items).size()-1U;
  fair.emit_op(0x68);fair.emit_op(0x43);
  const auto second_origin=raise_machine_to_ir(fair.items).size()-1U;
  fair.emit_op(0x63);fair.emit_op(0x42);fair.emit_op(0x62);fair.emit_op(0x4b);
  const auto fair_ops=raise_machine_to_ir(fair.items);
  const auto second_sink=fair_ops.size()-1U;
  const auto bounded=passes::contextual_copy_origin_register_requests(fair_ops,2);
  require(bounded.size()==2U && bounded[0].front().first==first_sink &&
      bounded[1].front()==std::pair{second_sink,second_origin},
      "one destination's many earlier definitions cannot starve another family");
  auto opaque=fair_ops;opaque[first_sink-1U].meta.raw=true;
  const auto remaining=passes::contextual_copy_origin_register_requests(opaque,1);
  require(remaining.size()==1U && remaining.front().front().first==second_sink,
      "opaque copy operands do not invent provenance preferences");
}


void sibling_value_origin_preferences(){
  CompileOptions options;options.analysis=true;options.coalesce_copies=true;
  for(const int kind:{0,1,2}){
    MachineEmitter e;e.emit_op(0x69);e.emit_op(0x40);
    const auto first=e.items.size()-1U;
    e.emit_op(0x60);e.emit_op(0x41);
    e.emit_op(2);e.emit_op(0x49); // Same source, deliberately different word.
    e.emit_op(0x69);e.emit_op(0x4a);
    const auto second=e.items.size()-1U;
    if(kind==1){e.emit_op(0x60);e.emit_op(0x4b);}
    if(kind==2)e.items[first-1U].raw=true;
    e.emit_op(kind==1?0x6b:0x61);e.emit_op(0x6a);e.emit_stop(StopDisposition::Terminal);
    const auto ops=raise_machine_to_ir(e.items);
    const auto requests=passes::contextual_copy_origin_register_requests(ops,16);
    const auto wanted=std::find(requests.begin(),requests.end(),
        passes::RegisterWebRecoloringRequest{{first,second}});
    if(kind==2){require(wanted==requests.end(),"raw ancestry never creates a sibling preference");continue;}
    require(wanted!=requests.end(),"independent captures of a common source propose a sibling affinity");
    const std::vector<PreloadReport> words{{.register_name="9",.value="3"}};
    std::vector<std::vector<IrOp>> recolored;
    passes::for_each_deferred_register_web_recoloring(ops,{options},{*wanted},{},{},
        [&](auto next){recolored.push_back(std::move(next));},&words,true);
    require(recolored.empty()==(kind==1),
        "complete liveness rejects overlapping siblings even when their source names agree");
    for(const auto& next:recolored)
      require(observe({e.items,words,{}},-1,true)==observe({lower_ir_to_machine(next),words,{}},-1,true),
          "disjoint siblings with different captured values preserve all later ROM observations");
  }
}
void guarded_context_word_identity(){
  for(int kind=0;kind<3;++kind){
    MachineEmitter e;e.emit_op(0x0d);
    if(kind==1)e.emit_op(0x68);else {e.emit_number("1");if(kind==0)e.emit_op(0x0b);}
    e.emit_op(0x40);
    e.emit_op(0x60);e.emit_jump(0x59,"only a proved negative initializer excludes this caller","after_early");
    e.emit_jump(0x53,"potential early use of old destination","shared_copy");
    e.emit_op(0x6c);e.emit_op(0x4d);e.emit_label("after_early");
    e.emit_number("4");e.emit_op(0x41);e.emit_label("scan");
    e.emit_op(0x61);e.emit_op(0x60);e.emit_op(0x36);e.emit_op(0x40);e.emit_op(0x11);
    e.emit_jump(0x5e,"retain selected coordinate","next");
    e.emit_op(0x61);e.emit_op(0x43);e.emit_label("next");
    e.emit_jump(0x5b,"finite scan","scan");e.emit_op(0x60);
    e.emit_jump(0x5c,"selected coordinate exists","selected");
    e.emit_op(0);e.emit_stop(StopDisposition::Terminal);e.emit_label("selected");
    e.emit_op(0x63);e.emit_op(0x4c);
    e.emit_jump(0x53,"idempotent later use","shared_copy");
    e.emit_op(0x6d);e.emit_op(0x6c);e.emit_stop(StopDisposition::Terminal);
    e.emit_label("shared_copy");e.emit_op(0x63);e.emit_op(0x4c);
    e.items.back().comment="guarded shared observable copy";e.emit_op(0x52);
    const std::vector<PreloadReport> words{{.register_name="3",.value="0"},
        {.register_name="c",.value="2"},{.register_name="d",.value="9"}};
    CompileOptions options;const auto ops=raise_machine_to_ir(e.items);
    const auto marked=[](const auto& code){return std::count_if(code.begin(),code.end(),[](const auto& op){
      return op.meta.comment=="guarded shared observable copy";
    });};
    require(marked(ops)==1,"the separately addressed shared store is present");
    const auto candidates=passes::contextual_redundant_copy_store_candidates(ops,{options},
        {.maximum_work=400000,.maximum_candidates=16,.maximum_copy_sites=32,.runtime_preloads=&words});
    bool erased_shared=false;
    for(const auto& candidate:candidates){
      if(marked(candidate.transformation.ops)!=0)continue;
      erased_shared=true;
      require(observe({e.items,words,{}},-1,true)==
          observe({lower_ir_to_machine(candidate.transformation.ops),words,{}},-1,true),
          "ROM preserves public output and later reads when only infeasible contexts are excluded");
      require(std::any_of(candidate.transformation.optimizations.begin(),candidate.transformation.optimizations.end(),
          [](const auto& report){return report.detail.find("numeric absence is never an exclusion")!=std::string::npos;}),
          "explain distinguishes explicit unreachability from unknown numeric facts");
    }
    require(erased_shared==(kind==0),
        "only a complete guarded impossibility proof may remove the shared copy; unknown and feasible callers retain it");
  }
}


void predicate_qualified_store_identity(){
  for(int kind=0;kind<3;++kind){
    MachineEmitter e;e.emit_op(0x0d);
    if(kind==1)e.emit_op(0x68);else {e.emit_number("1");e.emit_op(0x0b);}
    e.emit_op(0x40);e.emit_jump(0x53,"first invocation carries a different word","shared");
    if(kind==2){e.emit_op(0xdb);e.items.back().indirect_memory_targets=std::vector<int>{12};e.emit_op(0x4d);}
    e.emit_op(0x60);e.emit_jump(0x59,"negative guard excludes only this old-word read","after_read");
    e.emit_op(0x6c);e.emit_op(0x4d);e.emit_label("after_read");
    e.emit_op(0x63);e.emit_op(0x4c);e.emit_jump(0x53,"second invocation is idempotent","shared");
    e.emit_op(0x6d);e.emit_op(0x6c);e.emit_stop(StopDisposition::Terminal);
    e.items.back().roles.push_back(kTypedDisplayObservationRole);
    e.emit_label("shared");e.emit_op(0x63);e.emit_op(0x4c);
    e.items.back().comment="predicate-qualified shared copy";e.emit_op(0x52);
    const std::vector<PreloadReport> words{{.register_name="3",.value="3"},
        {.register_name="b",.value="12"},{.register_name="c",.value="2"},{.register_name="d",.value="9"}};
    CompileOptions options;const auto ops=raise_machine_to_ir(e.items);
    const auto marked=[](const auto& code){return std::count_if(code.begin(),code.end(),[](const auto& op){
      return op.meta.comment=="predicate-qualified shared copy";
    });};
    const auto candidates=passes::contextual_redundant_copy_store_candidates(ops,{options},
        {.maximum_work=400000,.maximum_candidates=16,.maximum_copy_sites=32,.runtime_preloads=&words});
    bool erased_shared=false;
    for(const auto& candidate:candidates){
      if(marked(candidate.transformation.ops)!=0)continue;
      erased_shared=true;
      require(observe({e.items,words,{}},-1,true)==
          observe({lower_ir_to_machine(candidate.transformation.ops),words,{}},-1,true),
          "ROM preserves outputs after composing guarded deadness in one caller with equality in another");
      require(std::any_of(candidate.transformation.optimizations.begin(),candidate.transformation.optimizations.end(),
          [](const auto& report){return report.detail.find("1 predicate-qualified dead caller contexts")!=std::string::npos;}),
          "the removed store has an explicit per-definition/per-caller non-observation certificate");
    }
    require(erased_shared==(kind==0),
        "unknown guards and a real indirect old-word read block removal even if the other caller is idempotent");
    require(passes::contextual_redundant_copy_store_candidates(ops,{options},
        {.maximum_work=1,.runtime_preloads=&words}).empty(),
        "an exhausted composed word/lifetime proof cannot erase a shared store");
  }
}


void finite_return_selector_orbits(){
  for(const int carrier:{7,12,14})for(int kind=0;kind<5;++kind){
    MachineEmitter e;e.emit_op(0x52);e.emit_label("main");
    if(kind==1){e.emit_op(0x66);e.emit_op(0x40+carrier);}
    if(kind==2){e.emit_op(0x68);e.emit_op(0xbb);e.items.back().indirect_memory_targets=std::vector<int>{carrier};}
    e.emit_jump(0x53,"first exact return context","helper");
    e.emit_op(0x4a);e.emit_jump(0x53,"second exact return context","helper");
    e.emit_op(0x6a);e.emit_stop(StopDisposition::Terminal);
    e.items.back().roles.push_back(kTypedDisplayObservationRole);
    e.emit_label("helper");e.emit_op(0x60+carrier);e.emit_op(0x0b);e.emit_op(0x40+carrier);
    e.emit_op(0x68);e.emit_jump(0x57,"same V/O through another physical cell","return_here");
    if(kind==3){e.emit_op(0x0a);e.emit_op(0x46);}
    e.emit_op(0x69);e.emit_label("return_here");e.emit_op(kind==4?0x54:0x52);
    if(kind==4)e.emit_op(0x52);
    CompileOptions options;
    for(const char* input:{"0","1","-1"}){
      const std::vector<PreloadReport> words{{.register_name=std::string(1,"0123456789abcde"[carrier]),.value="0.1"},
          {.register_name="8",.value=input},{.register_name="9",.value="23"},
          {.register_name="b",.value=std::to_string(carrier)}};
      const auto ops=raise_machine_to_ir(e.items);
      const auto candidates=passes::contextual_finite_return_selector_candidates(ops,{options},
          {.maximum_work=100000,.maximum_candidates=2,.runtime_preloads=&words});
      require(!candidates.empty()==(kind==0),
          "only a closed canonical sign orbit with an actual plain return and unobserved X2 can be reused");
      for(const auto& candidate:candidates){
        require(candidate.selector_origins==std::map<std::string,int>{{words.front().register_name,0}}&&
            cells(lower_ir_to_machine(candidate.transformation.ops))==cells(e.items)-1,
            "a finite return selector saves a real operand and records only its new origin");
        require(observe({e.items,words,{}},-1)==
            observe({lower_ir_to_machine(candidate.transformation.ops),words,{}},-1),
            "ROM preserves both branch outcomes, both matched returns, stack and every data register");
      }
      require(passes::contextual_finite_return_selector_candidates(ops,{options},
          {.maximum_work=1,.runtime_preloads=&words}).empty(),"orbit proof exhaustion is not a selector certificate");
    }
  }
}


void quantized_pure_return_completions(){
  for(int kind=0;kind<6;++kind)for(const char* input:{"0","0.25","1"}){
    MachineEmitter e;e.emit_op(0x52);e.emit_label("main");
    if(kind!=2&&kind!=3){e.emit_op(0x68);e.emit_jump(0x53,"existing observed use of the pure helper","quantize");e.emit_op(0x4b);}
    e.emit_jump(0x53,"ignored return value","work");
    if(kind==1)e.emit_op(0x4c);
    if(kind==5){e.emit_op(2);e.emit_op(0x4c);}
    for(int i=0;i<4;++i)e.emit_op(0x69);e.emit_op(0x10);e.emit_op(0x0f);e.emit_op(0x46);
    e.emit_stop(StopDisposition::Terminal);e.items.back().roles.push_back(kTypedDisplayObservationRole);
    e.emit_label("work");
    if(kind==5)e.emit_op(1);
    else {e.emit_op(0x68);e.emit_op(0x35);e.emit_jump(0x5e,"an addressed return must still skip the added body","work_return");e.emit_op(0x35);}
    e.emit_label("work_return");e.emit_op(0x52);e.items.back().comment="candidate ignored return";
    if(kind==4){e.emit_label("separate_component");e.emit_op(0x69);e.emit_op(0x52);}
    e.emit_label("quantize");e.emit_op(0x34);
    if(kind==3)e.emit_op(0x67);else e.emit_op(kind==2?0:4);
    e.emit_op(kind==2||kind==3?0x13:0x10);e.emit_op(0x52);
    const std::vector<PreloadReport> words{{.register_name="8",.value=input},{.register_name="9",.value="23"}};
    CompileOptions options;const auto ops=raise_machine_to_ir(e.items);
    const auto candidates=passes::contextual_pure_return_completion_candidates(ops,{options},
        {.maximum_work=200000,.maximum_candidates=4,.maximum_copy_sites=8,.runtime_preloads=&words});
    require(!candidates.empty()==(kind==0||kind==4),
        "only total pure tails and closed ignored returns compose; observed X, division by zero, memory and open entry reject");
    for(const auto& candidate:candidates){
      require(cells(lower_ir_to_machine(candidate.transformation.ops))==cells(e.items)-1,
          "moving the retained pure body removes exactly one actual return cell");
      require(observe({e.items,words,{}},-1)==observe({lower_ir_to_machine(candidate.transformation.ops),words,{}},-1),
          "ROM preserves direct return labels, existing helper calls, later X1 use, visible stack and every memory word");
    }
    require(passes::contextual_pure_return_completion_candidates(ops,{options},
        {.maximum_work=1,.runtime_preloads=&words}).empty(),"partial totality/range proofs cannot publish return completion");
  }
}

void typed_reply_web_transport(){
  for(int kind=0;kind<5;++kind){
    MachineEmitter e;e.emit_op(7);e.emit_op(0x48);e.emit_op(0x68);
    e.items.back().comment="display reply";
    if(kind==1)e.items.back().roles.push_back("display-byte");
    e.emit_stop(StopDisposition::Resumable);
    if(kind!=2)e.items.back().manual_interaction=ManualInteractionAnchor{
        .protocol_id=31,.phase=-1,.kind=ManualInteractionAnchorKind::PromptStop};
    e.emit_op(0x43);e.items.back().manual_interaction=ManualInteractionAnchor{
        .protocol_id=31,.phase=0,.kind=ManualInteractionAnchorKind::SingleStepCommand};
    e.emit_jump(0x53,"resume normalization","reply_normalize");
    e.items[e.items.size()-2U].manual_interaction=ManualInteractionAnchor{
        .protocol_id=31,.phase=1,.kind=ManualInteractionAnchorKind::ContinuousResume};
    e.emit_op(1);e.emit_op(5);e.emit_op(0x40);e.emit_op(0x80);
    e.items.back().indirect_flow_targets=std::vector<IrTarget>{std::string{"reply_done"}};
    e.emit_stop(StopDisposition::Terminal);e.emit_op(0x54);e.emit_op(0x54);
    e.emit_label("reply_done");e.emit_op(kind==3?0x68:0x63);e.emit_stop(StopDisposition::Terminal);
    if(kind==4)e.items.back().roles.clear();
    e.emit_label("reply_normalize");e.emit_op(0x34);e.emit_op(0x52);
    const auto ops=raise_machine_to_ir(e.items);
    std::size_t reply=ops.size(),fixed=ops.size();
    for(std::size_t i=0;i<ops.size();++i){
      if(ops[i].kind==IrKind::Recall&&ops[i].meta.comment==std::optional<std::string>{"display reply"})reply=i;
      if(ops[i].kind==IrKind::Store&&ops[i].register_name=="0")fixed=i;
    }
    CompileOptions o;o.coalesce_copies=true;PostLayoutControlFlowOptions control;
    const std::vector<PreloadReport> words;
    const std::map<std::string,int> origins{{"reply_done",14},{"reply_normalize",16}};
    std::vector<std::vector<IrOp>> candidates;
    passes::for_each_deferred_register_web_recoloring(ops,{o},{{{reply,fixed}}},control,origins,
        [&](auto candidate){candidates.push_back(std::move(candidate));},&words,true);
    require(!candidates.empty()==(kind==0),
        "only a dead typed reply web may override a comment hint; byte roles, untyped entry, later reads and public banks stay fixed");
    if(kind!=0)continue;
    require(candidates.front()[reply].register_name=="0",
        "the typed reply recall follows its producer into the shared physical home");
    const auto run=[&](const auto& code){
      const auto image=resolve_machine_items(lower_ir_to_machine(code));
      require(image.diagnostics.empty(),"typed reply allocation resolves");
      std::vector<int> bytes;for(const auto&s:image.steps)bytes.push_back(s.opcode);
      emulator::MK61 calc;require(calc.load_program(bytes).diagnostics.empty(),"typed reply fixture fits ROM");
      calc.press_sequence({"В/О","С/П"});
      require(calc.run_until_stable(5000,8).stopped,"typed reply prompt retained");
      std::vector<std::string> out{calc.read_register("X"),calc.read_register("Y")};
      calc.input_number("4.75").press("ПП");calc.input_number("2.5").press("С/П");
      require(calc.run_until_stable(5000,8).stopped,"typed reply continuation terminates");
      for(const char*r:{"X","Y","Z","T","X1","0","3"})out.push_back(calc.read_register(r));
      calc.press(".");out.push_back(calc.display_text(true));return out;
    };
    require(run(ops)==run(candidates.front()),
        "ROM preserves the displayed reply, two input phases and every subsequent program/stack observation");
  }
}

void memory_cursor_web_domains(){
  for(int kind=0;kind<6;++kind){
    const int source=kind==3?4:kind==5?1:0;
    const int target_cursor=kind==5?0:3;
    MachineEmitter e;e.emit_op(source==4?6:8);e.emit_op(0x40+source);
    e.emit_op(0xd0+source);
    if(kind!=2)e.items.back().indirect_memory_targets=kind==1?
        std::vector<int>{0,7}:std::vector<int>{7};
    e.emit_op(0x4a);e.emit_op(1);e.emit_op(1);e.emit_op(0x40+target_cursor);
    e.emit_op(0x80+target_cursor);e.items.back().indirect_flow_targets=std::vector<IrTarget>{std::string{"cursor_done"}};
    e.emit_stop(StopDisposition::Terminal);e.emit_op(0x54);
    e.emit_label("cursor_done");e.emit_op(0x6a);e.emit_stop(StopDisposition::Terminal);
    if(kind!=4)e.items.back().roles.push_back(kTypedDisplayObservationRole);
    else e.items.back().roles.clear(); // The emitter supplies this private-display role by default.
    const auto ops=raise_machine_to_ir(e.items);
    std::size_t cursor=ops.size(),destination=ops.size();
    for(std::size_t i=0;i<ops.size();++i){
      if(ops[i].kind==IrKind::IndirectRecall)cursor=i;
      if(ops[i].kind==IrKind::Store&&ops[i].register_name==std::string(1,"0123456789abcde"[target_cursor]))destination=i;
    }
    require(cursor<ops.size()&&destination<ops.size(),"cursor fixture owns distinct exact lifetime operands");
    const std::vector<PreloadReport> words{{.register_name="7",.value="42"}};
    const std::map<std::string,int> origins{{"cursor_done",10}};
    CompileOptions o;o.coalesce_copies=true;PostLayoutControlFlowOptions control;
    std::vector<std::vector<IrOp>> proposals;
    passes::for_each_deferred_register_web_recoloring(ops,{o},{{{cursor,destination}}},control,origins,
        [&](auto candidate){proposals.push_back(std::move(candidate));},&words,true);
    require(!proposals.empty()==(kind==0),
        "only a private complete non-self-aliasing predecrement cursor may share a later fixed epoch without entering R0 from R1..R3");
    if(kind!=0)continue;
    const auto& changed=proposals.front();
    require(changed[cursor].opcode==0xd3&&changed[cursor].register_name=="3"&&
        changed[cursor].meta.indirect_memory_targets==ops[cursor].meta.indirect_memory_targets,
        "the selector opcode moves but its indexed physical bank targets do not");
    const auto run=[&](const auto& code){
      const auto image=resolve_machine_items(lower_ir_to_machine(code));
      require(image.diagnostics.empty(),"cursor allocation resolves");
      std::vector<int> bytes;for(const auto&s:image.steps)bytes.push_back(s.opcode);
      emulator::MK61 calc;require(calc.load_program(bytes).diagnostics.empty(),"cursor fixture fits stock ROM");
      calc.set_register("7","42");calc.press_sequence({"В/О","С/П"});
      require(calc.run_until_stable(5000,8).stopped,"memory cursor and later control epoch both terminate");
      std::vector<std::string> out;for(const char*reg:{"X","Y","Z","T","X1","7","a"})out.push_back(calc.read_register(reg));
      calc.press(".");out.push_back(calc.display_text(true));return out;
    };
    require(run(ops)==run(changed),"ROM preserves every future observation, stack, X1 and hidden entry after cursor recoloring");
  }
}

void typed_direct_webs_ignore_comment_homes(){
  for(int kind=0;kind<5;++kind){
    MachineEmitter e;e.emit_op(5);const auto first=e.items.size();e.emit_op(0x41);e.items.back().comment="display value producer";
    if(kind==1)e.items.back().roles.push_back("display-byte");
    if(kind==2)e.items.back().raw=true;
    e.emit_op(0x61);e.emit_op(0x49);e.emit_op(6);
    const auto second=e.items.size();e.emit_op(0x42);e.items.back().comment="screen reply producer";
    if(kind==1)e.items.back().roles.push_back("display-byte");
    for(int n=0;n<4;++n)e.emit_op(0x67);e.emit_op(0x69);e.emit_op(0x62);e.emit_stop(StopDisposition::Terminal);
    if(kind==3)e.items.back().roles.clear();
    const auto ops=raise_machine_to_ir(e.items);const std::vector<PreloadReport> words{{.register_name="7",.value="11"}};
    const std::map<std::string,int> origins;CompileOptions options;options.coalesce_copies=true;
    std::vector<std::vector<IrOp>> candidates;
    passes::for_each_deferred_register_web_recoloring(ops,{options},{{{first,second}}},{},origins,
        [&](auto next){candidates.push_back(std::move(next));},kind==4?nullptr:&words,true);
    require(!candidates.empty()==(kind==0),
        "typed private direct webs may ignore comment-only home hints; explicit byte roles, raw/public banks and missing ownership reject");
    if(kind!=0)continue;
    require(candidates.front()[first].register_name==candidates.front()[second].register_name,
        "noninterfering producer epochs receive one actual hardware home");
    const auto run=[&](const auto& code){const auto image=resolve_machine_items(lower_ir_to_machine(code));
      require(image.diagnostics.empty(),"typed direct allocation resolves");std::vector<int> bytes;
      for(const auto& step:image.steps)bytes.push_back(step.opcode);emulator::MK61 calc;
      require(calc.load_program(bytes).diagnostics.empty(),"typed direct allocation fits ROM");
      calc.set_register("7","11");calc.press_sequence({"В/О","С/П"});
      require(calc.run_until_stable(5000,8).stopped,"typed direct allocation reaches output");
      std::vector<std::string> values;for(const char* reg:{"X","Y","Z","T","X1","7","9"})values.push_back(calc.read_register(reg));
      calc.press(".");values.push_back(calc.display_text(true));return values;
    };
    require(run(ops)==run(candidates.front()),
        "the whole web moves every later program read while preserving displayed values, full future stack, X1 and decimal shadow");
  }
}
void anticipatory_copy_and_literal_loads(){
  const auto make=[](bool reads_old_destination=false,bool raw=false){
    MachineEmitter e;
    for(const int input:{5,6}){
      e.emit_op(0x60+input);e.emit_jump(0x53,"independent producer invocation","producer");
      e.emit_op(0x61);e.emit_op(0x49);
      e.emit_op(input==5?2:4);e.emit_op(input==5?3:5);e.emit_op(0x4b);
      for(int i=0;i<4;++i)e.emit_op(0x67);
      if(input==5){e.emit_op(0x69);e.emit_op(0x48);}
    }
    e.emit_op(0x68);e.emit_op(0x69);e.emit_stop(StopDisposition::Terminal);
    e.emit_label("producer");e.emit_op(0x41);e.items.back().raw=raw;
    if(reads_old_destination)e.emit_op(0x69);
    e.emit_op(0x67);e.emit_op(0x52);return e.items;
  };
  const std::vector<PreloadReport> words{{.register_name="5",.value="2"},{.register_name="6",.value="6"},
      {.register_name="7",.value="11"},{.register_name="9",.value="0"}};
  CompileOptions options;options.coalesce_copies=true;options.defer_return_suffix_until_callee_hole=true;
  PostLayoutControlFlowOptions control;
  const auto positions=[](const auto& items){std::map<std::string,int> result;int pc=0;
    for(const auto& item:items){if(item.kind==MachineItemKind::Label)result.emplace(item.name,pc);else ++pc;}return result;};
  const auto early_copy=[](const auto& ops){
    for(std::size_t i=1;i<ops.size();++i){
      if(ops[i-1U].kind==IrKind::Store&&ops[i-1U].register_name=="1"&&ops[i].kind==IrKind::Store&&ops[i].register_name=="9")return true;
    }return false;
  };
  const auto play=[&](const auto& items){const auto resolved=resolve_machine_items(items);
    require(resolved.diagnostics.empty(),"anticipated fixture resolves");std::vector<int> bytes;
    for(const auto& step:resolved.steps)bytes.push_back(step.opcode);emulator::MK61 calc;
    require(calc.load_program(bytes).diagnostics.empty(),"anticipated fixture fits stock memory");
    for(const auto& word:words)calc.set_register(word.register_name,word.value);
    calc.press_sequence({"В/О","С/П"});
    require(calc.run_until_stable(10000,8).stopped,"anticipated copy reaches its observable result");
    std::vector<std::string> observed;
    for(const char* reg:{"X","Y","Z","T","X1","0","1","2","3","4","5","6","7","8","9","a","b","c","d","e"})observed.push_back(calc.read_register(reg));
    calc.press(".");observed.push_back(calc.read_register("X"));return observed;};
  const auto items=make();const auto origins=positions(items);
  const auto candidates=passes::contextual_anticipatory_copy_store_candidates(raise_machine_to_ir(items),{options},
      {.maximum_work=400000,.maximum_candidates=8,.maximum_copy_sites=16,.control=&control,
       .encoded_target_origins=&origins,.runtime_preloads=&words});
  require(!candidates.empty(),"a producer and later copy compose without a fixed source/register pattern");
  bool combined=false;
  for(const auto& candidate:candidates){
    require(play(items)==play(lower_ir_to_machine(candidate.transformation.ops)),
        "early private write and independently removed store preserve every final register, full stack, X1 and decimal shadow");
    if(!early_copy(candidate.transformation.ops))continue;
    const auto image=lower_ir_to_machine(candidate.transformation.ops);
    const auto after_origins=positions(image);
    const auto erased=passes::deferred_stack_producer_erasure_candidates(candidate.transformation.ops,{options},control,after_origins,
        {.maximum_candidates=8,.maximum_erased_cells=2,.maximum_proof_attempts=32,.allow_fresh_literal_replacement=true});
    for(const auto& reduced:erased){
      require(play(items)==play(lower_ir_to_machine(reduced.ops)),
          "the composed early copy and fresh-literal deletion preserve the complete ROM observations");
      combined=combined||cells(lower_ir_to_machine(reduced.ops))<cells(items);
    }
  }
  require(combined,"the neutral lifetime alternative repays itself through general stack-load elimination");
  for(const bool raw:{false,true}){
    const auto blocked=make(!raw,raw);const auto blocked_origins=positions(blocked);
    const auto rejected=passes::contextual_anticipatory_copy_store_candidates(raise_machine_to_ir(blocked),{options},
        {.maximum_work=400000,.maximum_candidates=8,.maximum_copy_sites=16,.control=&control,
         .encoded_target_origins=&blocked_origins,.runtime_preloads=&words});
    require(std::none_of(rejected.begin(),rejected.end(),[&](const auto& value){return early_copy(value.transformation.ops);}),
        "an old-word read before overwrite or raw producer forbids anticipation at that producer");
  }
  require(passes::contextual_anticipatory_copy_store_candidates(raise_machine_to_ir(items),{options},
      {.maximum_work=1,.runtime_preloads=&words}).empty(),"exhaustion cannot export a partially checked early write");
  require(passes::contextual_anticipatory_copy_store_candidates(raise_machine_to_ir(items),{options}).empty(),
      "absent runtime ownership never authorizes anticipation");
}
void fresh_literal_producer_proof(){
  for(const int kind:{0,1,2,3}){
    MachineEmitter e;if(kind==1){e.emit_op(2);e.emit_op(0x0e);}else e.emit_op(0x68);
    const auto producer=e.items.size();e.emit_op(0x65);
    if(kind==3)e.items.back().raw=true;
    e.emit_op(1);e.emit_op(2);e.emit_op(0x40);
    if(kind==2)e.emit_op(0x14);else for(int i=0;i<4;++i)e.emit_op(0x67);
    e.emit_stop(StopDisposition::Terminal);
    const auto flow=build_post_layout_control_flow(e.items,{});require(flow.proved,"literal-erasure fixture has an exact CFG");
    require(!prove_post_layout_stack_producer_erasure(e.items,flow,producer),
        "historical clients retain their original literal replacement policy");
    require(prove_post_layout_stack_producer_erasure(e.items,flow,producer,
        {.refine_runtime_selectors=false,.transport_closed_entry_through_control=true,
         .use_indirect_store_entry_facts=true,.allow_fresh_literal_replacement=true})==(kind==0),
        "only a grounded fresh literal with unobserved complete stack/X2 permits erasure");
    require(!prove_post_layout_stack_producer_erasure(e.items,flow,producer,
        {.refine_runtime_selectors=false,.allow_fresh_literal_replacement=true,.maximum_work=1}),
        "a literal mode proof must complete within its work budget");
  }
}
void selector_product_identity_returns(){
  for(const bool equal:{false,true}){
    MachineEmitter e;
    e.emit_op(0x0d);
    if(equal)e.emit_op(4);
    e.emit_op(0x43);e.emit_op(0x48);
    e.emit_op(1);e.emit_op(0x0b);e.emit_op(0x40);
    e.emit_jump(0x53,"first invocation has no surviving output","shared");
    e.emit_op(4);e.emit_op(0x42);e.emit_label("scan");
    e.emit_op(0x69);e.emit_jump(0x5e,"skip candidate","next");
    e.emit_op(0x6d);e.emit_op(0x4a);e.emit_op(0xaa);
    e.items.back().indirect_flow_targets=std::vector<IrTarget>{"alpha","beta"};
    e.emit_op(0x62);e.emit_op(0x60);e.emit_op(0x36);e.emit_op(0x40);e.emit_op(0x11);
    e.emit_jump(0x5e,"retain maximum","next");
    e.emit_op(equal?0x62:2);e.emit_op(0x43);e.emit_op(0x62);e.emit_op(0x48);
    e.emit_label("next");e.emit_jump(0x58,"finite scan","scan");
    e.emit_op(0x60);e.emit_jump(0x5c,"selected value exists","chosen");
    e.emit_op(0);e.emit_stop(StopDisposition::Terminal);
    e.emit_label("chosen");e.emit_jump(0x53,"observable invocation","shared");
    e.emit_op(0x68);e.emit_stop(StopDisposition::Terminal);
    e.emit_label("shared");
    e.emit_op(0x68);e.emit_jump(0x53,"normalize first value","normalize");e.emit_op(0x42);
    e.emit_op(0x63);e.emit_jump(0x53,"normalize independent second value","normalize");e.emit_op(0x41);
    e.emit_op(0x61);e.emit_op(0x48,std::nullopt,"product-qualified-copy");e.emit_op(0x52);
    e.emit_label("normalize");
    for(const int code:{0x34,4,0x13,0x35,1,0x11,0x35,1,0x10,4,0x12})e.emit_op(code);
    e.emit_op(0x52);e.emit_label("alpha");e.emit_op(0x52);
    e.emit_label("beta");e.emit_op(0x52);
    const auto preloads=[](const auto& items){
      std::vector<PreloadReport> words{{.register_name="9",.value="0"}};
      int address=0;
      for(const auto& item:items){
        if(item.kind!=MachineItemKind::Label){++address;continue;}
        if(item.name=="alpha" || item.name=="beta")
          words.push_back({.register_name=item.name=="alpha"?"a":"d",.value=std::to_string(address)});
      }
      return words;
    };
    const auto play=[&](const auto& items){
      const auto program=resolve_machine_items(items);
      require(program.diagnostics.empty(),"product-context fixture resolves after selector rebinding");
      std::vector<int> bytes;for(const auto& step:program.steps)bytes.push_back(step.opcode);
      emulator::MK61 calc;
      require(calc.load_program(bytes).diagnostics.empty(),"independent product-context fixture fits stock ROM");
      for(const auto& word:preloads(items))calc.set_register(word.register_name,word.value);
      calc.press_sequence({"В/О","С/П"});
      require(calc.run_until_stable(5000,8).stopped,"both invocations return to the observer");
      std::vector<std::string> values;
      for(const char* reg:{"X","Y","Z","T","X1","1","2","3","8"})values.push_back(calc.read_register(reg));
      calc.press(".");values.push_back(calc.display_text(true));return values;
    };
    const auto ops=raise_machine_to_ir(e.items);
    const auto words=preloads(e.items);CompileOptions options;options.analysis=true;
    const auto candidates=passes::contextual_redundant_copy_store_candidates(ops,{options},
        {.maximum_work=400000,.maximum_candidates=16,.maximum_copy_sites=32,.runtime_preloads=&words});
    bool erased=false;
    for(const auto& candidate:candidates){
      const bool removes=std::none_of(candidate.transformation.ops.begin(),candidate.transformation.ops.end(),
          [](const auto& op){return op.meta.comment=="product-qualified-copy";});
      if(!removes)continue;
      erased=true;
      require(play(e.items)==play(lower_ir_to_machine(candidate.transformation.ops)),
          "same-PC return rows preserve independent values, all future stack slots, X1 and decimal shadow");
    }
    require(!erased || equal,
        "an identity cannot import equality from a different selector row or an unreachable caller");
    require(std::stod(play(e.items).front())==(equal?4:2),
        "the negative fixture observes genuinely unequal selected values");
  }
}

void data_and_rom(){
  selector_product_identity_returns();
  typed_direct_webs_ignore_comment_homes();
  anticipatory_copy_and_literal_loads();
  fresh_literal_producer_proof();
  typed_reply_web_transport();
  memory_cursor_web_domains();
  quantized_pure_return_completions();
  finite_return_selector_orbits();
  predicate_qualified_store_identity();
  guarded_context_word_identity();
  transitive_value_origin_preferences();
  sibling_value_origin_preferences();
  contextual_word_identity();
  x2_join_idempotence();
  neutral_helper_abi();
  exact_finalization_memo();
  composition_depth_scheduling();
  reversible_dual_mode_continuation();
  prologue_reopens_argument_lifetimes();
  proved_neutral_layout_materialization();
  allocation_preferences_are_not_lowering_aliases();
  exact_cfg_cache_contract();
  memory_root_operand_abi();
  repaired_allocation_preferences();
  idempotent_input_prologue();
  guarded_selected_value_ranges();
  dense_definition_rows();
  retired_setup_home_pool();
  allocation_copy_cleanup_feedback();
  contextual_helper_continuation();
  stack_equality_work_bound();
  aliased_helper_operand_abi();
  typed_resume_family_permutation();
  padded_symbolic_packing();
  whole_family_permutation();
  charged_alias_privacy();
  deferred_sentinel_materialization();
  arbitrary_epoch_deadness();
  literal_recall_boundary_rom();
  allocation_exposed_pool();
  closed_toggle_sign_facts();
  formal_toggle_sign_facts();
  transition_store_sign_facts();
  finite_value_dependency_facts();
  encoded_guarded_lifetime();
  indirect_store_entry_facts();
  deferred_charges();
  stale_preload_owner();
  for(const int carrier:{8,12})for(const int copied:{0,3}){
    const auto f=fixture(Kind::Normal,carrier,copied);CompileOptions o;o.analysis=true;
    const auto candidates=passes::contextual_copy_transport_candidates(raise_machine_to_ir(f.items),{o});
    require(!candidates.empty(),"forward equal reads and sink only the observing caller");
    const auto& c=candidates.front();
    require(c.materialized_stores.size()==1U,"dead second caller needs no materialization");
    require(cells(lower_ir_to_machine(c.transformation.ops))==cells(f.items)+1,"one removed store, two charged prefix cells");
    require(observe({f.items,f.preloads,{}},carrier)==observe({lower_ir_to_machine(c.transformation.ops),f.preloads,{}},carrier),
        "exact returned values, future memory reads and visible stack agree in ROM");
    if(carrier==8 && copied==0){
      PostLayoutControlFlowOptions deferred_control;deferred_control.empty_return_target=1;
      std::map<std::string,int> origins;int pc=0;
      for(const auto& item:f.items){
        if(item.kind==MachineItemKind::Label)origins.emplace(item.name,pc);else ++pc;
      }
      const auto deferred=passes::contextual_copy_transport_candidates(raise_machine_to_ir(f.items),{o},
          {.maximum_work=200000,.maximum_candidates=2,.maximum_copy_sites=8,.maximum_growth_cells=4,
           .control=&deferred_control,.encoded_target_origins=&origins});
      require(!deferred.empty() &&
          cells(lower_ir_to_machine(deferred.front().transformation.ops))==
              cells(lower_ir_to_machine(candidates.front().transformation.ops)),
          "explicit encoded origins preserve the ordinary copy-transport certificate");
      require(passes::contextual_copy_transport_candidates(raise_machine_to_ir(f.items),{o},
          {.encoded_target_origins=&origins}).empty(),
          "partial deferred environment cannot authorize selector pruning");
      auto tiny_control=deferred_control;tiny_control.maximum_execution_states=1;
      require(passes::contextual_copy_transport_candidates(raise_machine_to_ir(f.items),{o},
          {.control=&tiny_control,.encoded_target_origins=&origins}).empty(),
          "incomplete deferred execution proof exports no copy candidate");
      const auto general=search_joint_representation_constraints({f.items,f.preloads,{}},o,{},
          {.maximum_expansions=6,.maximum_candidates=24,.maximum_depth=4,.maximum_growth_cells=4},true);
      require(general.incumbent && cells(general.incumbent->items)<=cells(f.items)-7,
          "general graph search composes copy transport, a delivered finite carrier and final layout");
      require(observe({f.items,f.preloads,{}},carrier,true)==observe(*general.incumbent,carrier,true),
          "the independent coupled graph preserves all declared future observations in ROM");
    }
    const auto composed=contextual_copy_transport_layout_transactions({f.items,f.preloads,{}},o);
    require(!composed.empty(),"codec, lifetime and placement must compose before comparing size");
    require(cells(composed.front().node.items)<=cells(f.items)-7,"three projected consumers pay for the return-prefix decoder");
    require(observe({f.items,f.preloads,{}},carrier,true)==observe(composed.front().node,carrier,true),"joint codec preserves all later observations");
  }
  for(const auto kind:{Kind::ChangedSource,Kind::Alias,Kind::ObservedStack,Kind::Untyped,Kind::Raw}){
    const auto f=fixture(kind);CompileOptions o;
    require(passes::contextual_copy_transport_candidates(raise_machine_to_ir(f.items),{o}).empty(),
        "source changes, alias reads, hidden stack observations and untyped/raw entries reject");
  }
  const auto f=fixture();CompileOptions o;
  require(passes::contextual_copy_transport_candidates(raise_machine_to_ir(f.items),{o},
      {.maximum_work=1}).empty(),"exhaustion never exports a partial transport certificate");
}
void pending(){
  std::ifstream input(std::string(MKPRO_CONTEXT_REPO_ROOT)+"/examples/pending-optimizer/tic-tac-toe-4x4.mkpro");
  require(bool(input),"unchanged high-level port exists");
  const std::string source((std::istreambuf_iterator<char>(input)),{});
  CompileOptions options;options.analysis=true;options.budget=999999;
  const auto result=compile_source(source,options);
  std::cout<<"automatic 4x4 cells "<<result.steps.size()<<'\n';
  for(const auto& d:result.diagnostics)std::cerr<<d.message<<'\n';
  require(result.implemented,"automatic high-level port compiles");
  require(result.steps.size()<=115U,"coupled general optimization must save at least ten cells from 125");
}
}
int main(int argc,char** argv){try{if(argc>1 && std::string(argv[1])=="--pending")pending();else data_and_rom();return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

