#include "mkpro/core/passes/contextual_copy_transport.hpp"
#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/detail/cell_provenance.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/stable_register_value_flow.hpp"
#include "mkpro/core/passes/liveness_analysis.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/core/detail/exact_value_terms.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include <array>
#include <cstdint>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <deque>
#include <map>
#include <set>
#include <string_view>

namespace mkpro::core::passes {
namespace {
using Id = std::size_t;
bool descriptive(const std::vector<CellRole>& roles) {
  return std::all_of(roles.begin(), roles.end(), detail::is_cell_origin_provenance);
}
bool direct(const IrOp& op) {
  return (op.kind == IrKind::Store || op.kind == IrKind::Recall) &&
      op.register_name.size() == 1U &&
      std::string_view("0123456789abcde").find(op.register_name[0]) != std::string_view::npos &&
      op.opcode == (op.kind == IrKind::Store ? 0x40 : 0x60) + register_index(op.register_name) &&
      !op.meta.raw && !op.meta.logical_register_analysis && !op.meta.manual_interaction &&
      !op.procedure_boundary && op.meta.semantic_call_origins.empty() &&
      descriptive(op.meta.roles) && descriptive(op.target_meta.roles);
}
struct Model {
  const std::vector<IrOp>& ops;
  std::vector<MachineItem> items;
  AuthoritativePostLayoutControlFlow original, flow;
  PostLayoutControlFlowOptions control;
  PostLayoutExternalEntryContexts entries;
  std::vector<Id> item, instruction;
  std::vector<RegisterEffects> effects;
  std::vector<std::vector<Id>> predecessors;
  bool valid = false;
  Model(const std::vector<IrOp>& code, const PassContext& context,
        const ContextualCopyTransportOptions& limits) : ops(code), items(lower_ir_to_machine(code)) {
    const auto model=address_space_model_for_feature_profile(effective_optimizer_feature_profile(context.options));
    if(limits.encoded_target_origins && !limits.control) return;
    if(limits.control){control=*limits.control;if(control.address_space_model!=model)return;}
    else{control.empty_return_target=1;control.address_space_model=model;}
    if(limits.encoded_target_origins){
      std::set<std::string> labels;
      for(const auto& op:ops)if(op.kind==IrKind::Label)labels.insert(op.name);
      for(const auto& [label,origin]:*limits.encoded_target_origins)
        if(origin<0 || !labels.contains(label))return;
    }
    std::map<Id,Id> at;
    Id machine = 0;
    for (Id i = 0; i < ops.size(); ++i) {
      const auto& op = ops[i];
      // Formal-PC edits need their own placement theorem. Do not turn an
      // ordinary physical destination into permission to move a dark alias.
      if (op.meta.raw || op.meta.logical_register_analysis || op.kind == IrKind::OrphanAddress ||
          op.target_meta.formal_opcode || op.meta.indirect_flow_formal_targets) {
        if(std::getenv("MKPRO_NATIVE_TRACE_COPY_TRANSPORT"))std::cerr<<"[copy-transport] unsupported IR "<<i<<" opcode="<<op.opcode<<" formal="<<op.target_meta.formal_opcode.has_value()<<"\n";
        return;
      }
      if((op.kind==IrKind::Store || op.kind==IrKind::Recall) &&
          (op.register_name.size()!=1U || std::string_view("0123456789abcde").find(op.register_name[0])==std::string_view::npos ||
           op.opcode!=(op.kind==IrKind::Store?0x40:0x60)+register_index(op.register_name)))return;
      item.push_back(machine);
      if (op.kind != IrKind::Label) at.emplace(machine, i);
      effects.push_back(register_effects(op));
      machine += lower_ir_to_machine({op}).size();
    }
    original = build_post_layout_control_flow(items, control);
    if (!original.proved || !prove_post_layout_input_boundaries(items, original).proved) return;
    const auto selected = limits.encoded_target_origins
        ? build_deferred_selector_consistent_execution_graph(
              items,original,*limits.encoded_target_origins).execution
        : build_selector_consistent_execution_graph(items, original);
    if (!selected.proved) return;
    flow = original;
    flow.execution_states.clear();
    for (const auto state : selected.original_states) flow.execution_states.push_back(original.execution_states[state]);
    flow.execution_successors = selected.successors;
    flow.execution_edges = selected.execution_edges;
    const auto external = resolve_post_layout_external_entry_contexts(flow);
    if (!external) return;
    entries = *external;
    predecessors.resize(flow.execution_states.size());
    for (Id state = 0; state < flow.execution_states.size(); ++state) {
      const auto found = at.find(flow.execution_states[state].item_index);
      if (found == at.end()) return;
      instruction.push_back(found->second);
      for (const auto next : flow.execution_successors[state]) {
        if (next >= predecessors.size()) return;
        predecessors[next].push_back(state);
      }
    }
    valid = true;
  }
};
bool observes(const IrOp& op, const RegisterEffects& effects, const std::string& reg) {
  if (effects.uses_all_registers || effects.uses.contains(reg)) return true;
  return op.kind == IrKind::Stop && std::find(op.meta.roles.begin(),op.meta.roles.end(),
      kTypedDisplayObservationRole) == op.meta.roles.end();
}
struct Plan {
  std::vector<Id> reads;
  std::set<Id> sinks;
  std::string source, destination;
  Id store = 0, work = 0, dead_contexts = 0, observational_contexts = 0, guarded_contexts = 0;
  bool exhausted = false, valid = false;
};
Plan discover(const Model& m, Id store, Id budget) {
  Plan plan; plan.store = store;
  if (!m.valid || store == 0 || !direct(m.ops[store]) || m.ops[store].kind != IrKind::Store ||
      !direct(m.ops[store-1U]) || m.ops[store-1U].register_name == m.ops[store].register_name) return plan;
  plan.source = m.ops[store-1U].register_name;
  plan.destination = m.ops[store].register_name;
  const auto spend = [&] { if (plan.work >= budget) { plan.exhausted=true; return false; } ++plan.work; return true; };
  const auto count = m.instruction.size();
  std::vector<Id> roots;
  for (Id s=0;s<count;++s) if(m.instruction[s]==store) {
    if (!spend()) return plan;
    if (m.entries.external[s] || m.predecessors[s].empty() ||
        !std::all_of(m.predecessors[s].begin(),m.predecessors[s].end(),[&](Id p){return m.instruction[p]==store-1U;})) return plan;
    roots.push_back(s);
  }
  if (roots.empty()) return plan;
  // Greatest must fixed point, grounded by the unique Main entry and by the
  // copied definition. Validated input edges carry memory, never a new seed.
  std::vector<bool> before(count,true), after(count,true), queued(count,true);
  std::deque<Id> pending;
  for(Id s=0;s<count;++s) pending.push_back(s);
  while(!pending.empty()) {
    if(!spend()) return plan;
    const auto s=pending.front();pending.pop_front();queued[s]=false;
    bool same=!m.entries.main[s] && !m.predecessors[s].empty();
    for(auto p:m.predecessors[s]) same=same && after[p];
    before[s]=same;
    const auto i=m.instruction[s];const auto& e=m.effects[i];
    bool outgoing=same;
    if(e.may_define_any_register || e.must_defs.contains(plan.source) || e.may_defs.contains(plan.source) ||
       e.must_defs.contains(plan.destination) || e.may_defs.contains(plan.destination)) outgoing=false;
    if(i==store) outgoing=true;
    if(after[s]!=outgoing) {
      after[s]=outgoing;
      for(auto next:m.flow.execution_successors[s]) if(!queued[next]) {queued[next]=true;pending.push_back(next);}
    }
  }
  std::map<Id,bool> qualified;
  for(Id s=0;s<count;++s) {
    if(!spend()) return plan;
    const auto i=m.instruction[s];const auto& op=m.ops[i];
    if(op.kind==IrKind::Recall && op.register_name==plan.destination && direct(op)) {
      auto [it,fresh]=qualified.emplace(i,before[s]);
      if(!fresh) it->second=it->second && before[s];
    }
  }
  for(const auto& [read,same]:qualified) if(same) plan.reads.push_back(read);
  if(plan.reads.empty()) return plan;
  auto changed=m.ops;
  for(auto read:plan.reads) {
    changed[read].register_name=plan.source;
    changed[read].opcode=0x60+register_index(plan.source);
  }
  std::vector<RegisterEffects> effects;
  for(const auto& op:changed) effects.push_back(register_effects(op));
  // Compute old-word demand. A possible write cannot kill it. This safety
  // fixed point permits read-free cycles without inventing eventual progress.
  std::vector<bool> live(count,false);
  pending.clear();std::fill(queued.begin(),queued.end(),true);
  for(Id s=0;s<count;++s) pending.push_back(s);
  while(!pending.empty()) {
    if(!spend()) return plan;
    const auto s=pending.front();pending.pop_front();queued[s]=false;
    const auto i=m.instruction[s];const auto& e=effects[i];
    bool wanted=observes(changed[i],e,plan.destination);
    if(!e.must_defs.contains(plan.destination))
      for(auto next:m.flow.execution_successors[s]) wanted=wanted || live[next];
    if(wanted!=live[s]) { live[s]=wanted; for(auto prev:m.predecessors[s]) if(!queued[prev]) {queued[prev]=true;pending.push_back(prev);} }
  }
  for(auto root:roots) {
    std::set<Id> seen;
    pending.assign(m.flow.execution_successors[root].begin(),m.flow.execution_successors[root].end());
    bool needs_sink=false;
    const auto& caller=m.flow.execution_states[root].return_stack;
    while(!pending.empty()) {
      if(!spend()) return plan;
      auto s=pending.front();pending.pop_front();if(!seen.insert(s).second || !live[s]) continue;
      const auto i=m.instruction[s];const auto& e=effects[i];
      const auto& frames=m.flow.execution_states[s].return_stack;
      if(frames.size()<caller.size() && std::equal(frames.begin(),frames.end(),caller.begin())) {
        if(!before[s]) return plan;
        plan.sinks.insert(i);needs_sink=true;continue;
      }
      if(observes(changed[i],e,plan.destination)) return plan;
      if(e.must_defs.contains(plan.destination)) continue;
      pending.insert(pending.end(),m.flow.execution_successors[s].begin(),m.flow.execution_successors[s].end());
    }
    if(!needs_sink) ++plan.dead_contexts;
  }
  if(plan.sinks.empty()) {plan.valid=true;return plan;}
  const auto lift=analyze_post_layout_automatic_entry_lift(m.items,m.flow,budget-plan.work);
  if(!lift) return plan;
  for(auto sink:plan.sinks) {
    std::vector<Id> entries;
    for(Id s=0;s<count;++s) if(m.instruction[s]==sink) {
      if(!spend()) return plan;
      if(!before[s] || m.entries.external[s] || !lift->at(s)) return plan;
      entries.push_back(s);
    }
    // The delayed recall can replace X and shift all four stack slots. It
    // preserves last-X1, but not X2. Prove all these differences forgotten
    // through every actual continuation, not just the selected caller.
    if(!prove_post_layout_stack_state_equality(lower_ir_to_machine(changed),m.flow,entries,
        {{false,false,false,false},false,true},
        {.refine_runtime_selectors=false,.numeric_entry_closed=true,
         .transport_closed_entry_through_control=true})) return plan;
  }
  plan.valid=true;return plan;
}

constexpr int word_slots=36, last_x_slot=19, shadow_slot=20, recall_slot=21;
struct WordPartition {
  std::array<std::uint64_t,word_slots> rows{};
  bool operator==(const WordPartition&) const=default;
};
WordPartition independent_words(){
  WordPartition p;for(int i=0;i<word_slots;++i)p.rows[static_cast<Id>(i)]=std::uint64_t{1}<<i;
  return p;
}
WordPartition all_words_equal(){
  WordPartition p;p.rows.fill((std::uint64_t{1}<<word_slots)-1U);return p;
}
WordPartition remap_words(const WordPartition& old,const std::array<int,word_slots>& origins){
  auto p=independent_words();
  for(int i=0;i<word_slots;++i)for(int j=0;j<word_slots;++j)
    if(origins[static_cast<Id>(i)]>=0 && origins[static_cast<Id>(j)]>=0 &&
        (old.rows[static_cast<Id>(origins[static_cast<Id>(i)])]&
         (std::uint64_t{1}<<origins[static_cast<Id>(j)])))
      p.rows[static_cast<Id>(i)]|=std::uint64_t{1}<<j;
  return p;
}
void unite_words(WordPartition& p,int a,int b){
  const auto group=p.rows[static_cast<Id>(a)]|p.rows[static_cast<Id>(b)];
  for(int i=0;i<word_slots;++i)if(group&(std::uint64_t{1}<<i))p.rows[static_cast<Id>(i)]=group;
}
std::optional<int> physical_word_register(const std::string& name){
  if(name.size()!=1U)return std::nullopt;
  const auto at=std::string_view{"0123456789abcde"}.find(name[0]);
  return at==std::string_view::npos?std::nullopt:std::optional<int>{static_cast<int>(at)};
}
struct ContextWordProof {
  bool proved=false,exhausted=false;
  Id work=0,finite_returns=0;
  std::vector<WordPartition> before;
  std::set<Id> canonical_inputs,canonical_memory_loads,unreachable;
};
ContextWordProof prove_context_words(const Model& m,const std::vector<PreloadReport>& preloads,Id maximum_work){
  namespace terms=detail::exact_terms;
  ContextWordProof result;
  const auto count=m.instruction.size();
  const auto spend=[&]{
    if(result.work>=maximum_work){result.exhausted=true;return false;}
    ++result.work;return true;
  };
  if(!count)return result;
  std::set<std::string> owners;
  for(const auto& word:preloads)
    if(!physical_word_register(word.register_name) || !owners.insert(word.register_name).second)return result;
  for(const auto& op:m.ops)
    if(op.opcode==0x4f || op.opcode==0x6f || (op.opcode>=0x70 && (op.opcode&15)==15))return result;
  const auto boundaries=prove_post_layout_input_boundaries(m.items,m.flow);
  if(!boundaries.proved)return result;
  // Ground the greatest must fixed point. A disconnected external cycle is
  // not an independent initializer or permission to assume word equality.
  std::vector<bool> reachable(count,false);
  std::deque<Id> pending;
  for(Id s=0;s<count;++s)if(m.entries.main[s]){reachable[s]=true;pending.push_back(s);}
  while(!pending.empty()){
    if(!spend())return result;
    const auto s=pending.front();pending.pop_front();
    for(auto next:m.flow.execution_successors[s])if(!reachable[next]){reachable[next]=true;pending.push_back(next);}
  }
  if(std::find(reachable.begin(),reachable.end(),false)!=reachable.end())return result;

  struct IdentityCall{Id call=0,entry=0,ret=0;std::vector<IrOp> body;};
  std::vector<IdentityCall> calls;
  std::vector<Id> queries;
  for(Id i=0;i<m.ops.size();++i)
    if(direct(m.ops[i]) && m.ops[i].kind==IrKind::Store)queries.push_back(m.item[i]);
  for(Id s=0;s<count;++s){
    if(!spend())return result;
    const auto i=m.instruction[s];const auto& call=m.ops[i];
    if(call.kind!=IrKind::Call && call.kind!=IrKind::IndirectCall)continue;
    if(m.flow.execution_successors[s].size()!=1U || m.predecessors[s].empty())continue;
    const auto entry=m.flow.execution_successors[s].front();
    const auto begin=m.instruction[entry];
    IdentityCall candidate{s,entry,0,{}};
    bool found=false;
    for(Id at=begin;at<m.ops.size() && candidate.body.size()<16U;++at){
      if(!spend())return result;
      const auto& op=m.ops[at];
      if(op.kind==IrKind::Label)continue;
      if(op.kind==IrKind::Return && op.opcode==0x52){candidate.ret=m.item[at];found=true;break;}
      if(op.kind!=IrKind::Plain || op.meta.raw || op.meta.manual_interaction ||
          !descriptive(op.meta.roles) || !descriptive(op.target_meta.roles) ||
          !terms::modeled_plain(op.opcode))break;
      candidate.body.push_back(op);
    }
    if(!found || candidate.body.empty())continue;
    const int last=candidate.body.back().opcode;
    // The final positive one-digit result must be a normalized ordinary word,
    // not a numerically equal denormalized alias. All consumed intermediate
    // values below are concrete ordinary decimals, not uninterpreted raw BCD.
    if(!((last>=0x10 && last<=0x13) || last==0x15 || last==0x22 || last==0x32))continue;
    queries.push_back(m.item[i]);calls.push_back(std::move(candidate));
  }
  std::map<Id,std::set<Id>> identities;
  std::map<Id,std::vector<Id>> identity_users;
  if(!queries.empty() && result.work<maximum_work){
    const auto lift=analyze_post_layout_automatic_entry_lift_with_indirect_store_facts(
        m.items,m.flow,maximum_work-result.work);
    if(lift){
      std::sort(queries.begin(),queries.end());queries.erase(std::unique(queries.begin(),queries.end()),queries.end());
      const auto ranges=prove_post_layout_numeric_ranges(m.items,m.flow,*lift,queries,&preloads,
          std::min<Id>(100000,(maximum_work-result.work)/2),NumericRangeInputPolicy::ValidatedRegisterTransport);
      result.work+=ranges.work;
      if(!ranges.exhausted){
        result.canonical_inputs=ranges.canonical_small_integer_states;
        result.canonical_memory_loads=ranges.canonical_memory_load_states;
        result.unreachable=ranges.unreachable_states;
        for(const auto s:result.unreachable)if(s>=count || m.entries.main[s])return result;
      }
      if(!ranges.exhausted)for(const auto& call:calls){
        const auto domain=ranges.x_before_state.find(call.call);
        if(domain==ranges.x_before_state.end() ||
            !ranges.canonical_small_integer_states.contains(call.call) ||
            !domain->second.integral || domain->second.lower<1 || domain->second.upper>9)continue;
        bool identity=true;
        for(auto value=domain->second.lower;value<=domain->second.upper;++value){
          terms::Evaluation evaluator;
          const auto input=terms::number(std::to_string(value));
          if(!input){identity=false;break;}
          evaluator.state.stack[0]=*input;evaluator.synchronize_call();
          for(const auto& op:call.body){if(!spend())return result;evaluator.execute(op);}
          if(!evaluator.valid || !evaluator.observations.empty() || !evaluator.state.literal.empty() ||
              evaluator.state.stack[0]!=*input){identity=false;break;}
        }
        if(!identity)continue;
        const auto& entry=m.flow.execution_states[call.entry];
        for(Id s=0;s<count;++s){
          if(!spend())return result;
          const auto& state=m.flow.execution_states[s];
          if(!result.unreachable.contains(s) && state.item_index==call.ret && state.return_stack==entry.return_stack &&
              state.formal_return_stack==entry.formal_return_stack && !m.entries.external[s]){
            identities[s].insert(call.call);identity_users[call.call].push_back(s);++result.finite_returns;
          }
        }
      }
    }
  }
  const auto transfer=[&](const WordPartition& input,Id s){
    std::array<int,word_slots> from;for(int i=0;i<word_slots;++i)from[static_cast<Id>(i)]=i;
    const auto& op=m.ops[m.instruction[s]];const auto& effects=m.effects[m.instruction[s]];
    const auto forget_register=[&](const std::string& name){
      if(const auto r=physical_word_register(name)){
        from[static_cast<Id>(4+*r)]=-1;from[static_cast<Id>(recall_slot+*r)]=-1;
      }else for(int other=0;other<15;++other){
        from[static_cast<Id>(4+other)]=-1;from[static_cast<Id>(recall_slot+other)]=-1;
      }
    };
    if(effects.may_define_any_register)for(int r=0;r<15;++r){
      from[static_cast<Id>(4+r)]=-1;from[static_cast<Id>(recall_slot+r)]=-1;
    }
    for(const auto& name:effects.must_defs)forget_register(name);
    for(const auto& name:effects.may_defs)forget_register(name);
    const int code=op.opcode,family=code&0xf0;
    const auto reg=physical_word_register(op.register_name);
    if(op.kind==IrKind::Recall && reg){
      from[0]=recall_slot+*reg;from[1]=0;from[2]=1;from[3]=2;from[shadow_slot]=0;
    }else if(op.kind==IrKind::Store && reg){
      from[static_cast<Id>(4+*reg)]=0;
      from[static_cast<Id>(recall_slot+*reg)]=-1;
      if(result.canonical_inputs.contains(s))from[static_cast<Id>(recall_slot+*reg)]=0;
      else for(int other=0;other<15;++other)
        if(input.rows[0]&(std::uint64_t{1}<<(4+other))){
          from[static_cast<Id>(recall_slot+*reg)]=recall_slot+other;break;
        }
    }
    else if(code==0x14){from[0]=1;from[1]=0;}
    else if(code==0x25){from[0]=1;from[1]=2;from[2]=3;from[3]=0;}
    else if(code==0x0e){from[0]=0;from[1]=0;from[2]=1;from[3]=2;from[shadow_slot]=0;}
    else if(code==0x0f){from[0]=last_x_slot;from[1]=0;from[2]=1;from[3]=2;from[shadow_slot]=0;}
    else if((code>=0x51 && code<=0x5e) || (family>=0x70 && family<=0xe0)){
      from[shadow_slot]=-1;
      if(family>=0x70 && family<=0xe0 && (code&15)<15){
        from[static_cast<Id>(4+(code&15))]=-1;
        from[static_cast<Id>(recall_slot+(code&15))]=-1;
      }
      if(op.kind==IrKind::IndirectRecall){from[0]=-1;from[1]=0;from[2]=1;from[3]=2;}
    }else{
      const auto& info=opcode_by_code(code);
      if(code<=0x0c || info.stack_effect==StackEffect::Barrier || info.stack_effect==StackEffect::Unknown ||
          info.x2_effect==X2Effect::Restores || info.x2_effect==X2Effect::Unknown){
        for(int i=0;i<4;++i)from[static_cast<Id>(i)]=-1;
        from[shadow_slot]=-1;from[last_x_slot]=-1;
      }else{
        if(opcode_saves_physical_x1(code))from[last_x_slot]=0;
        if(info.stack_effect==StackEffect::ConsumeYDrop){from[0]=-1;from[1]=2;from[2]=3;from[3]=3;}
        else if(info.stack_effect==StackEffect::ConsumeYKeep)from[0]=-1;
        else if(info.stack_effect==StackEffect::Preserves)from[0]=-1;
        else if(info.stack_effect==StackEffect::Shifts){from[0]=-1;from[1]=0;from[2]=1;from[3]=2;}
        else if(info.stack_effect==StackEffect::Exposes){from[0]=1;from[1]=2;from[2]=3;from[3]=3;}
        if(info.x2_effect==X2Effect::Affects)from[shadow_slot]=0;
      }
    }
    auto out=remap_words(input,from);
    if(const auto fact=identities.find(s);fact!=identities.end())
      for(const auto call_state:fact->second){
        const auto& caller=m.ops[m.instruction[call_state]];
        const auto selector=caller.kind==IrKind::IndirectCall
            ?physical_word_register(caller.register_name):std::optional<int>{};
        for(int r=0;r<15;++r)
          if((!selector || *selector!=r) &&
              (result.before[call_state].rows[0]&(std::uint64_t{1}<<(4+r))))
            unite_words(out,0,4+r);
        for(int r=0;r<15;++r)
          if((!selector || *selector!=r) &&
              (result.before[call_state].rows[0]&(std::uint64_t{1}<<(recall_slot+r))))
            unite_words(out,0,recall_slot+r);
      }
    return out;
  };
  result.before.assign(count,all_words_equal());
  std::vector<WordPartition> after(count,all_words_equal());
  std::vector<bool> queued(count,true);
  for(Id s=0;s<count;++s)pending.push_back(s);
  while(!pending.empty()){
    if(!spend())return result;
    const auto s=pending.front();pending.pop_front();queued[s]=false;
    if(result.unreachable.contains(s))continue;
    auto incoming=m.entries.main[s]?independent_words():all_words_equal();
    if(m.entries.main[s])for(const auto& word:preloads)
      if(!word.setup_expression && !word.setup_target_name && !word.setup_expression_text &&
          !word.retunable_natural_fractional_prefix &&
          (!word.lowered_data_value || *word.lowered_data_value==word.value) &&
          word.value.size()==1U && word.value[0]>='1' && word.value[0]<='9'){
        const int r=*physical_word_register(word.register_name);
        unite_words(incoming,4+r,recall_slot+r);
      }
    for(auto previous:m.predecessors[s])if(!result.unreachable.contains(previous))
      for(int i=0;i<word_slots;++i)incoming.rows[static_cast<Id>(i)]&=after[previous].rows[static_cast<Id>(i)];
    if(boundaries.input_boundary_states[s]){
      std::array<int,word_slots> map;for(int i=0;i<word_slots;++i)map[static_cast<Id>(i)]=i;
      for(int i=0;i<4;++i)map[static_cast<Id>(i)]=-1;
      map[last_x_slot]=-1;map[shadow_slot]=-1;incoming=remap_words(incoming,map);
    }
    if(result.canonical_memory_loads.contains(s)){
      const auto& read=m.ops[m.instruction[s]];
      if(read.kind!=IrKind::Recall)return result;
      const auto r=physical_word_register(read.register_name);if(!r)return result;
      unite_words(incoming,4+*r,recall_slot+*r);
    }
    const bool incoming_changed=result.before[s]!=incoming;
    result.before[s]=incoming;
    if(incoming_changed)if(const auto users=identity_users.find(s);users!=identity_users.end())
      for(auto next:users->second)if(!queued[next]){queued[next]=true;pending.push_back(next);}
    const auto outgoing=transfer(incoming,s);
    if(outgoing==after[s])continue;
    after[s]=outgoing;
    for(auto next:m.flow.execution_successors[s])if(!result.unreachable.contains(next) && !queued[next]){queued[next]=true;pending.push_back(next);}
  }
  result.proved=true;return result;
}
Plan redundant_copy_plan(const Model& m,const ContextWordProof& equality,Id store,Id budget,
    const std::vector<PreloadReport>& preloads){
  Plan plan;plan.store=store;
  if(!equality.proved || store==0 || !direct(m.ops[store]) || m.ops[store].kind!=IrKind::Store ||
      !direct(m.ops[store-1U]) || m.ops[store-1U].kind!=IrKind::Recall ||
      m.ops[store-1U].register_name==m.ops[store].register_name)return plan;
  plan.source=m.ops[store-1U].register_name;plan.destination=m.ops[store].register_name;
  const auto spend=[&]{if(plan.work>=budget){plan.exhausted=true;return false;}++plan.work;return true;};
  const auto count=m.instruction.size();
  std::vector<bool> live(count,false),unsafe(count,false),queued(count,true);
  std::deque<Id> pending;for(Id s=0;s<count;++s)pending.push_back(s);
  while(!pending.empty()){
    if(!spend())return plan;
    const auto s=pending.front();pending.pop_front();queued[s]=false;
    if(equality.unreachable.contains(s))continue;
    const auto i=m.instruction[s];const auto& effects=m.effects[i];
    const bool use=observes(m.ops[i],effects,plan.destination);
    bool wanted=use;
    bool sensitive=use && !(direct(m.ops[i]) && m.ops[i].kind==IrKind::Recall &&
        m.ops[i].register_name==plan.destination);
    if(!effects.must_defs.contains(plan.destination))
      for(auto next:m.flow.execution_successors[s]){wanted=wanted || live[next];sensitive=sensitive || unsafe[next];}
    if(wanted==live[s] && sensitive==unsafe[s])continue;
    live[s]=wanted;unsafe[s]=sensitive;
    for(auto previous:m.predecessors[s])if(!queued[previous]){queued[previous]=true;pending.push_back(previous);}
  }
  bool reached=false;
  std::vector<Id> needs_guarded_deadness;
  const int destination=4+*physical_word_register(plan.destination);
  for(Id s=0;s<count;++s)if(m.instruction[s]==store && !equality.unreachable.contains(s)){
    if(!spend())return plan;
    reached=true;
    if(m.entries.external[s] || m.predecessors[s].empty())return plan;
    for(auto p:m.predecessors[s])
      if(m.instruction[p]!=store-1U || m.flow.execution_states[p].return_stack!=m.flow.execution_states[s].return_stack ||
          m.flow.execution_states[p].formal_return_stack!=m.flow.execution_states[s].formal_return_stack)return plan;
    bool observed=false,sensitive=false;
    for(auto next:m.flow.execution_successors[s]){observed=observed || live[next];sensitive=sensitive || unsafe[next];}
    const bool same=equality.before[s].rows[0]&(std::uint64_t{1}<<destination);
    const int view=recall_slot+*physical_word_register(plan.destination);
    const bool same_view=equality.canonical_inputs.contains(s) && !sensitive &&
        (equality.before[s].rows[0]&(std::uint64_t{1}<<view));
    if(observed && !same && !same_view)needs_guarded_deadness.push_back(s);
    if(observed && !same && same_view)++plan.observational_contexts;
    if(!observed)++plan.dead_contexts;
  }
  if(!needs_guarded_deadness.empty()){
    // Join neither dynamic definitions nor unrelated invocations of this
    // shared store. Each unresolved context must exclude EVERY possible old
    // destination observation; an omitted alias or opaque escape is not dead.
    std::vector<Id> observers;
    for(Id i=0;i<m.ops.size();++i)if(observes(m.ops[i],m.effects[i],plan.destination))observers.push_back(i);
    if(observers.empty())return plan;
    std::vector<GuardedDefinitionUseQuery> queries;
    for(const auto state:needs_guarded_deadness)for(const auto use:observers){
      if(queries.size()>=64U)return plan;
      queries.push_back({store,use,plan.destination,state,{}});
    }
    LivenessOptions options;options.discover_toggle_projection_signs=true;
    for(const auto& word:preloads)
      if(!word.setup_expression && !word.setup_target_name && !word.setup_expression_text)
        options.equal_entry_value_classes[word.register_name]="literal:"+word.value;
    const auto proof=analyze_unreachable_definition_uses(m.ops,m.flow,queries,options,budget-plan.work);
    plan.work+=proof.work;
    if(proof.exhausted){plan.exhausted=true;return plan;}
    if(std::any_of(queries.begin(),queries.end(),[&](const auto& q){return !proof.excluded_uses.contains(q);}))return plan;
    plan.guarded_contexts=needs_guarded_deadness.size();
  }
  plan.valid=reached;return plan;
}

PassResult rewrite(const Model& m,const Plan& plan,std::vector<Id>& materialized) {
  PassResult out;
  std::vector<std::optional<Id>> operation(m.ops.size());
  std::vector<std::pair<Id,Id>> prefixes;
  std::set<Id> reads(plan.reads.begin(),plan.reads.end());
  for(Id i=0;i<m.ops.size();++i) {
    if(plan.sinks.contains(i)) {
      const auto begin=out.ops.size();
      auto recall=make_recall(plan.source);
      recall.meta.source_line=m.ops[plan.store].meta.source_line;
      recall.meta.comment="context-qualified delayed copy source";
      out.ops.push_back(std::move(recall));
      auto store=make_store(plan.destination);
      store.meta.source_line=m.ops[plan.store].meta.source_line;
      store.meta.comment="copy materialized only in an observing continuation";
      out.ops.push_back(std::move(store));
      prefixes.emplace_back(begin,begin+1U);
      materialized.push_back(begin+1U);
    }
    if(i==plan.store) continue;
    operation[i]=out.ops.size();
    auto op=m.ops[i];
    if(reads.contains(i)) {
      op.register_name=plan.source;op.opcode=0x60+register_index(plan.source);
      op.meta.mnemonic=opcode_by_code(op.opcode).name;
    }
    out.ops.push_back(std::move(op));
  }
  const auto image=lower_ir_to_machine(out.ops);
  std::vector<Id> item(out.ops.size());Id machine=0;
  for(Id i=0;i<out.ops.size();++i){item[i]=machine;machine+=lower_ir_to_machine({out.ops[i]}).size();}
  std::vector<std::optional<Id>> mapping(m.items.size());
  for(Id i=0;i<m.ops.size();++i) if(operation[i])
    for(Id k=0;k<lower_ir_to_machine({m.ops[i]}).size();++k) mapping[m.item[i]+k]=item[*operation[i]]+k;
  auto next=plan.store+1U;
  while(next<m.ops.size() && m.ops[next].kind==IrKind::Label)++next;
  if(next==m.ops.size() || !operation[next]) return {};
  mapping[m.item[plan.store]]=item[*operation[next]];
  PostLayoutExecutionRelocationOptions transport;
  transport.erased_data_effect_items={m.item[plan.store]};
  for(auto read:plan.reads) transport.retargeted_direct_recall_items.push_back(m.item[read]);
  for(auto [a,b]:prefixes) transport.inserted_copy_prefixes.emplace_back(item[a],item[b]);
  const auto final=build_post_layout_control_flow(image,m.control);
  const auto relocated=prove_post_layout_execution_relocation(m.items,image,m.original,final,mapping,transport);
  if(!relocated.proved) {
    if(std::getenv("MKPRO_NATIVE_TRACE_COPY_TRANSPORT"))for(const auto& reason:relocated.reasons)std::cerr<<"[copy-transport] relocation: "<<reason<<"\n";
    return {};
  }
  out.applied=1;
  out.optimizations.push_back({"context-qualified-copy-transport",
      "Forwarded "+std::to_string(plan.reads.size())+" exact-word reads without joining dynamic epochs; "
      "moved a copied definition to "+std::to_string(plan.sinks.size())+" observing return continuations, "
      "with "+std::to_string(plan.dead_contexts)+" dead caller contexts. Independently proved "
      "source stability, every stack/X1/X2 continuation and rooted return-frame relocation. "
      "Charged all inserted recalls/stores; final allocation and placement remain required."});
  return out;
}
} // namespace
std::vector<ContextualCopyTransportCandidate> contextual_copy_transport_candidates(
    const std::vector<IrOp>& ops,const PassContext& context,const ContextualCopyTransportOptions& limits) {
  std::vector<ContextualCopyTransportCandidate> result;
  if(!limits.maximum_candidates || !limits.maximum_work || !limits.maximum_copy_sites || limits.maximum_growth_cells<0) return result;
  bool demand=false;
  for(Id i=1;i<ops.size();++i) demand=demand || (direct(ops[i]) && direct(ops[i-1U]) &&
      ops[i].kind==IrKind::Store && ops[i].register_name!=ops[i-1U].register_name);
  if(!demand) return result;
  Model model(ops,context,limits);if(std::getenv("MKPRO_NATIVE_TRACE_COPY_TRANSPORT"))std::cerr<<"[copy-transport] model="<<model.valid<<" states="<<model.instruction.size()<<"\n";if(!model.valid)return result;
  Id remaining=limits.maximum_work,attempts=0;
  for(Id i=1;i<ops.size() && result.size()<limits.maximum_candidates && remaining;++i) {
    if(!direct(ops[i]) || !direct(ops[i-1U]) || ops[i].kind!=IrKind::Store ||
       ops[i].register_name==ops[i-1U].register_name) continue;
    if(attempts++>=limits.maximum_copy_sites)break;
    auto plan=discover(model,i,remaining);remaining-=std::min(remaining,plan.work);
    if(std::getenv("MKPRO_NATIVE_TRACE_COPY_TRANSPORT"))std::cerr<<"[copy-transport] store="<<i<<" source="<<plan.source<<" destination="<<plan.destination<<" reads="<<plan.reads.size()<<" sinks="<<plan.sinks.size()<<" valid="<<plan.valid<<" work="<<plan.work<<"\n";
    if(!plan.valid || plan.exhausted || 2*static_cast<int>(plan.sinks.size())-1>limits.maximum_growth_cells)continue;
    ContextualCopyTransportCandidate candidate;
    candidate.transformation=rewrite(model,plan,candidate.materialized_stores);
    if(candidate.transformation.applied>0) {
      std::size_t identity=0;
      for(const auto store:candidate.materialized_stores)
        candidate.transformation.ops.at(store).meta.roles.push_back(
            std::string(detail::kContextualCopyStoreOriginPrefix)+std::to_string(identity++));
      result.push_back(std::move(candidate));
    }
  }
  return result;
}

std::vector<ContextualCopyTransportCandidate> contextual_redundant_copy_store_candidates(
    const std::vector<IrOp>& ops,const PassContext& context,const ContextualCopyTransportOptions& limits){
  std::vector<ContextualCopyTransportCandidate> result;
  if(!limits.runtime_preloads || !limits.maximum_work || !limits.maximum_candidates || !limits.maximum_copy_sites)return result;
  bool demand=false;
  for(Id i=1;i<ops.size();++i)demand=demand || (direct(ops[i]) && ops[i].kind==IrKind::Store &&
      direct(ops[i-1U]) && ops[i-1U].kind==IrKind::Recall && ops[i].register_name!=ops[i-1U].register_name);
  if(!demand)return result;
  Model model(ops,context,limits);if(!model.valid)return result;
  const auto equality=prove_context_words(model,*limits.runtime_preloads,limits.maximum_work);
  if(!equality.proved || equality.exhausted)return result;
  Id remaining=limits.maximum_work-std::min(limits.maximum_work,equality.work),attempts=0;
  for(Id i=1;i<ops.size() && remaining && result.size()<limits.maximum_candidates;++i){
    if(!direct(ops[i]) || ops[i].kind!=IrKind::Store || !direct(ops[i-1U]) ||
        ops[i-1U].kind!=IrKind::Recall || ops[i].register_name==ops[i-1U].register_name)continue;
    if(attempts++>=limits.maximum_copy_sites)break;
    const auto plan=redundant_copy_plan(model,equality,i,remaining,*limits.runtime_preloads);
    remaining-=std::min(remaining,plan.work);
    if(!plan.valid || plan.exhausted)continue;
    ContextualCopyTransportCandidate candidate;
    candidate.transformation=rewrite(model,plan,candidate.materialized_stores);
    if(candidate.transformation.applied<=0)continue;
    candidate.transformation.optimizations.clear();
    candidate.transformation.optimizations.push_back({"context-qualified-redundant-copy-store",
        "Removed a copied store only after every exact caller proves its old destination dead or its "
        "complete machine word already equal to X, or a canonical one-digit X with identical normalized recall view and no later selector/loop/alias observation before overwrite. Proved "+std::to_string(plan.observational_contexts)+" recall-view contexts and "+std::to_string(plan.guarded_contexts)+" predicate-qualified dead caller contexts. Propagated grounded must equalities through "
        "register/stack copies and separately certified "+std::to_string(equality.finite_returns)+
        " finite, total pure-helper identity returns on canonical one-digit inputs; unknown/raw/denormalized "
        "inputs and ungrounded cycles provide no identity facts. Excluded only "+std::to_string(equality.unreachable.size())+" exact execution states by complete guarded definition/use proofs; numeric absence is never an exclusion. Independently re-proved the complete "
        "rooted CFG, every return frame and closed numeric-entry deletion."});
    result.push_back(std::move(candidate));
  }
  return result;
}


std::vector<RegisterWebRecoloringRequest> contextual_copy_origin_register_requests(
    const std::vector<IrOp>& ops,std::size_t maximum_requests){
  std::vector<RegisterWebRecoloringRequest> result;
  if(!maximum_requests)return result;
  // These are allocation proposals, never a data-equivalence certificate.
  // A helper can transform its argument arbitrarily: the complete allocator
  // and the later contextual GVN still have to prove the composed artifact.
  const auto parent=[&](Id store)->std::optional<std::string>{
    if(!store)return std::nullopt;
    Id before=store-1U;
    if(direct(ops[before]))return ops[before].register_name;
    if((ops[before].kind==IrKind::Call || ops[before].kind==IrKind::IndirectCall) &&
        !ops[before].meta.raw && before>0){
      --before;
      while(before>0 && ops[before].kind==IrKind::Plain && ops[before].opcode==0x54 &&
          !ops[before].meta.raw)--before;
      if(direct(ops[before]) && ops[before].kind==IrKind::Recall)
        return ops[before].register_name;
    }
    return std::nullopt;
  };
  std::map<std::string,std::set<std::string>> parents;
  std::map<std::string,std::vector<Id>> definitions;
  for(Id i=0;i<ops.size();++i)if(direct(ops[i]) && ops[i].kind==IrKind::Store){
    definitions[ops[i].register_name].push_back(i);
    if(const auto source=parent(i);source && *source!=ops[i].register_name)
      parents[ops[i].register_name].insert(*source);
  }
  struct Hint{Id sink=0,origin=0;bool copy_origin=false;};
  std::map<std::string,std::vector<Hint>> families;
  std::set<std::pair<Id,Id>> unique;
  for(Id sink=1;sink<ops.size();++sink){
    if(!direct(ops[sink]) || ops[sink].kind!=IrKind::Store ||
        !direct(ops[sink-1U]) || ops[sink-1U].kind!=IrKind::Recall ||
        ops[sink].register_name==ops[sink-1U].register_name)continue;
    const auto& destination=ops[sink].register_name;
    const auto& source=ops[sink-1U].register_name;
    std::set<std::string> seen{source};
    std::deque<std::string> pending{source};
    while(!pending.empty()){
      const auto name=pending.front();pending.pop_front();
      const auto found=parents.find(name);
      if(found==parents.end())continue;
      for(const auto& origin:found->second){
        if(!seen.insert(origin).second)continue;
        pending.push_back(origin);
        if(origin==destination || origin==source)continue;
        if(const auto stores=definitions.find(origin);stores!=definitions.end())
          for(const auto definition:stores->second){
            const std::pair<Id,Id> pair{std::min(sink,definition),std::max(sink,definition)};
            if(sink!=definition && unique.insert(pair).second)
              families[destination].push_back({sink,definition,
                  definition>0 && direct(ops[definition-1U]) &&
                      ops[definition-1U].kind==IrKind::Recall});
          }
      }
    }
  }
  for(auto& [name,hints]:families){
    (void)name;
    std::stable_sort(hints.begin(),hints.end(),[](const auto& a,const auto& b){
      return a.copy_origin>b.copy_origin;
    });
  }
  // Do not spend the bounded preference portfolio on the first scalar's
  // history. Every destination family gets its next origin deterministically.
  for(Id round=0;result.size()<maximum_requests;++round){
    bool added=false;
    for(const auto& [name,hints]:families){
      (void)name;
      if(round>=hints.size())continue;
      result.push_back({{hints[round].sink,hints[round].origin}});
      added=true;if(result.size()==maximum_requests)break;
    }
    if(!added)break;
  }
  return result;
}

namespace {
// Exact canonical decimal spelling for a finite ordinary literal. No rounding,
// host floating point, raw nibbles or over-eight-digit mantissas are admitted.
std::optional<std::string> normalized_finite_word(const std::string& spelling){
  std::string word=spelling;bool negative=false;
  if(!word.empty()&&word.front()=='-'){negative=true;word.erase(0,1);}
  const auto exponent_at=word.find_first_of("eE");int exponent=0;
  if(exponent_at!=std::string::npos){
    auto suffix=word.substr(exponent_at+1U);word.resize(exponent_at);bool minus=false;
    if(!suffix.empty()&&(suffix.front()=='-'||suffix.front()=='+')){minus=suffix.front()=='-';suffix.erase(0,1);}
    if(suffix.empty()||suffix.size()>2U)return std::nullopt;
    for(const char c:suffix){if(c<'0'||c>'9')return std::nullopt;exponent=exponent*10+c-'0';}
    if(minus)exponent=-exponent;
  }
  std::string digits;std::optional<int> point;
  for(const char c:word){
    if(c=='.'){if(point)return std::nullopt;point=static_cast<int>(digits.size());}
    else if(c>='0'&&c<='9')digits+=c;else return std::nullopt;
  }
  if(digits.empty())return std::nullopt;
  const int integer_digits=point.value_or(static_cast<int>(digits.size()));
  const auto first=digits.find_first_not_of('0');if(first==std::string::npos)return std::nullopt;
  exponent+=integer_digits-static_cast<int>(first)-1;digits.erase(0,first);
  while(digits.size()>1U&&digits.back()=='0')digits.pop_back();
  if(digits.size()>8U||exponent<-99||exponent>99)return std::nullopt;
  digits.append(8U-digits.size(),'0');
  return std::string(negative?"-":"")+digits.substr(0,1)+"."+digits.substr(1)+"e"+std::to_string(exponent);
}
}

std::vector<ContextualCopyTransportCandidate> contextual_finite_return_selector_candidates(
    const std::vector<IrOp>& ops,const PassContext& context,const ContextualCopyTransportOptions& limits){
  std::vector<ContextualCopyTransportCandidate> result;
  if(!limits.runtime_preloads||!limits.maximum_candidates||!limits.maximum_work)return result;
  Id zero=0;while(zero<ops.size()&&ops[zero].kind==IrKind::Label)++zero;
  if(zero==ops.size()||ops[zero].kind!=IrKind::Return||ops[zero].opcode!=0x52)return result;
  const bool demanded=std::any_of(ops.begin(),ops.end(),[](const auto& op){return op.kind==IrKind::CondJump;});
  if(!demanded)return result;
  Model m(ops,context,limits);if(!m.valid)return result;
  Id work=0;const auto spend=[&]{if(work>=limits.maximum_work)return false;++work;return true;};
  std::vector<bool> rooted(m.instruction.size(),false);std::deque<Id> roots;
  for(Id state=0;state<m.instruction.size();++state)if(m.entries.main[state]){rooted[state]=true;roots.push_back(state);}
  while(!roots.empty()){
    if(!spend())return result;
    const auto state=roots.front();roots.pop_front();
    for(const auto next:m.flow.execution_successors[state])if(!rooted[next]){rooted[next]=true;roots.push_back(next);}
  }
  if(std::find(rooted.begin(),rooted.end(),false)!=rooted.end())return result;
  std::set<std::string> owners;
  for(const auto& word:*limits.runtime_preloads)
    if(!physical_word_register(word.register_name)||!owners.insert(word.register_name).second)return result;
  std::map<std::string,Id> labels;
  for(Id i=0;i<ops.size();++i)if(ops[i].kind==IrKind::Label){
    auto next=i+1U;while(next<ops.size()&&ops[next].kind==IrKind::Label)++next;
    if(next<ops.size())labels[ops[i].name]=next;
  }
  std::string entry="__finite_selector_return_zero";
  while(labels.contains(entry))entry+='_';
  for(const auto& initial:*limits.runtime_preloads){
    if(!spend())return result;
    const auto reg=physical_word_register(initial.register_name);
    if(!reg||*reg<7||initial.setup_expression||initial.setup_target_name||initial.setup_expression_text||
        initial.retunable_natural_fractional_prefix||
        (initial.lowered_data_value&&*initial.lowered_data_value!=initial.value)||!bound_ordinary_decimal(initial.value))continue;
    const auto canonical=normalized_finite_word(initial.value);if(!canonical)continue;
    const std::string opposite=canonical->starts_with("-")?canonical->substr(1):"-"+*canonical;
    bool valid=true;
    for(const auto& word:std::array<std::string,2>{*canonical,opposite}){
      const auto address=evaluate_indirect_address(initial.register_name,word,IndirectOperationKind::Flow,m.control.address_space_model);
      if(!address||address->actual_flow_target!=0||!indirect_writeback_preserves_literal_word(*address,word)||
          noncanonical_indirect_flow_entries(address))valid=false;
    }
    if(!valid)continue;
    std::set<Id> stores;
    for(Id i=0;i<ops.size()&&valid;++i){
      if(!spend())return result;
      const auto& effect=m.effects[i];
      // Adding a new selector read must not assume an untracked old alias or
      // pre-existing selector writeback. All admitted writes are closed self
      // negations, verified again in every exact execution context below.
      if(effect.may_define_any_register||effect.uses_all_registers||effect.may_defs.contains(initial.register_name)||
          ((ops[i].kind==IrKind::IndirectCall||ops[i].kind==IrKind::IndirectJump||
            ops[i].kind==IrKind::IndirectCondJump||ops[i].kind==IrKind::IndirectRecall||
            ops[i].kind==IrKind::IndirectStore)&&ops[i].register_name==initial.register_name)){
        valid=false;break;
      }
      if(!effect.must_defs.contains(initial.register_name))continue;
      if(i<2U||!direct(ops[i])||ops[i].kind!=IrKind::Store||
          ops[i-1U].kind!=IrKind::Plain||ops[i-1U].opcode!=0x0b||ops[i-1U].meta.manual_interaction||
          !descriptive(ops[i-1U].meta.roles)||!direct(ops[i-2U])||
          ops[i-2U].kind!=IrKind::Recall||ops[i-2U].register_name!=initial.register_name){valid=false;break;}
      stores.insert(i);
    }
    if(!valid||stores.empty())continue;
    for(Id state=0;state<m.instruction.size()&&valid;++state){
      if(!spend())return result;
      const auto site=m.instruction[state];
      if(!stores.contains(site)&&!(site+1U<ops.size()&&stores.contains(site+1U)))continue;
      if(m.entries.external[state]||m.predecessors[state].empty())valid=false;
      for(const auto previous:m.predecessors[state])
        if(m.instruction[previous]+1U!=site||
            m.flow.execution_states[previous].return_stack!=m.flow.execution_states[state].return_stack||
            m.flow.execution_states[previous].formal_return_stack!=m.flow.execution_states[state].formal_return_stack)valid=false;
    }
    if(!valid)continue;
    // A recall followed by sign and store produces the canonical orbit word.
    // Require such a definition on every path before each new selector use;
    // the spelling/shape of an unnormalized Main preload is never assumed.
    std::vector<bool> normalized(m.instruction.size(),true),out(m.instruction.size(),true),queued(m.instruction.size(),true);
    std::deque<Id> pending;for(Id state=0;state<m.instruction.size();++state)pending.push_back(state);
    while(!pending.empty()){
      if(!spend())return result;
      const auto state=pending.front();pending.pop_front();queued[state]=false;
      bool before_word=!m.entries.main[state]&&!m.predecessors[state].empty();
      for(const auto previous:m.predecessors[state])before_word=before_word&&out[previous];
      normalized[state]=before_word;
      const bool after_word=before_word||stores.contains(m.instruction[state]);
      if(after_word==out[state])continue;out[state]=after_word;
      for(const auto next:m.flow.execution_successors[state])if(!queued[next]){queued[next]=true;pending.push_back(next);}
    }
    const auto lift=analyze_post_layout_automatic_entry_lift_with_indirect_store_facts(
        m.items,m.original,limits.maximum_work-work);
    if(!lift)continue;
    std::set<Id> branches,returns;
    for(Id i=0;i<ops.size();++i){
      if(!spend())return result;
      const auto& op=ops[i];if(op.kind!=IrKind::CondJump||op.meta.raw||op.meta.manual_interaction||
          op.procedure_boundary||!descriptive(op.meta.roles)||!descriptive(op.target_meta.roles))continue;
      const auto* label=std::get_if<std::string>(&op.target);if(!label||!labels.contains(*label))continue;
      const auto target=labels.at(*label);const auto& ret=ops[target];
      if(ret.kind!=IrKind::Return||ret.opcode!=0x52||ret.meta.raw||ret.meta.manual_interaction||ret.procedure_boundary||
          !ret.meta.semantic_call_origins.empty())continue;
      bool reached=false,ready=true;
      for(Id state=0;state<m.instruction.size();++state)if(m.instruction[state]==i){reached=true;ready=ready&&normalized[state];}
      for(Id state=0;state<m.original.execution_states.size();++state)
        if(m.original.execution_states[state].item_index==m.item[i])ready=ready&&lift->at(state);
      if(!reached||!ready||!prove_post_layout_direct_conditional_inversion_effects(m.items,m.original,m.item[i]))continue;
      if(op.opcode!=0x57&&op.opcode!=0x59&&op.opcode!=0x5c&&op.opcode!=0x5e)continue;
      branches.insert(i);if(target!=zero)returns.insert(target);
    }
    if(branches.empty())continue;
    ContextualCopyTransportCandidate candidate;auto& transformed=candidate.transformation;
    std::vector<Id> relocated(ops.size());
    for(Id i=0;i<ops.size();++i){
      if(i==zero){IrOp label;label.kind=IrKind::Label;label.name=entry;transformed.ops.push_back(std::move(label));}
      relocated[i]=transformed.ops.size();auto op=ops[i];
      if(branches.contains(i)){
        const int family=op.opcode==0x57?0x70:op.opcode==0x59?0x90:op.opcode==0x5c?0xc0:0xe0;
        op.kind=IrKind::IndirectCondJump;op.opcode=family+*reg;op.register_name=initial.register_name;
        op.meta.mnemonic=opcode_by_code(op.opcode).name;op.target_meta={};
        op.meta.indirect_flow_targets=std::vector<IrTarget>{entry};
        op.meta.indirect_flow_formal_targets=std::nullopt;op.target=entry;
      }
      transformed.ops.push_back(std::move(op));
    }
    std::vector<Id> item(transformed.ops.size());Id index=0;
    for(Id i=0;i<transformed.ops.size();++i){item[i]=index;index+=lower_ir_to_machine({transformed.ops[i]}).size();}
    std::vector<std::optional<Id>> mapping(m.items.size());
    for(Id i=0;i<ops.size();++i){
      const auto length=branches.contains(i)?1U:lower_ir_to_machine({ops[i]}).size();
      for(Id cell=0;cell<length;++cell)mapping[m.item[i]+cell]=item[relocated[i]]+cell;
    }
    PostLayoutExecutionRelocationOptions transport;
    for(const auto branch:branches)transport.direct_to_indirect_flow_items.push_back(m.item[branch]);
    for(const auto ret:returns)transport.equivalent_return_items.emplace_back(m.item[ret],item[relocated[zero]]);
    const auto image=lower_ir_to_machine(transformed.ops);const auto flow=build_post_layout_control_flow(image,m.control);
    const auto proof=prove_post_layout_execution_relocation(m.items,image,m.original,flow,mapping,transport);
    if(!proof.proved)continue;
    // A second converted condition must not invalidate the first site's X2
    // convergence. Repeat every continuation proof on the combined FINAL CFG,
    // rather than composing independently valid old-graph kill certificates.
    bool final_effects=true;
    for(const auto branch:branches){
      std::set<Id> successors;
      for(Id state=0;state<flow.execution_states.size();++state)
        if(flow.execution_states[state].item_index==item[relocated[branch]])
          successors.insert(flow.execution_successors[state].begin(),flow.execution_successors[state].end());
      final_effects=final_effects&&!successors.empty()&&prove_post_layout_stack_state_equality(image,flow,
          {successors.begin(),successors.end()},{{true,true,true,true},false,true},
          {.refine_runtime_selectors=false,.numeric_entry_closed=true,.transport_closed_entry_through_control=true});
    }
    if(!final_effects)continue;
    candidate.selector_origins.emplace(initial.register_name,0);
    transformed.applied=static_cast<int>(branches.size());
    transformed.optimizations.push_back({"finite-orbit-return-selector",
        "Reused R"+initial.register_name+" only after a complete inductive self-negation orbit, canonical-definition dominance and full-word selector-writeback proof. Replaced "+std::to_string(branches.size())+
        " direct conditional return operands without a new register or initializer. Independently proved every original CFG/return frame, explicit plain-return aliases and both X2 continuations; no numeric equality or raw-word guess authorizes the rewrite."});
    result.push_back(std::move(candidate));if(result.size()>=limits.maximum_candidates)break;
  }
  return result;
}

std::vector<ContextualCopyTransportCandidate> contextual_pure_return_completion_candidates(
    const std::vector<IrOp>& ops,const PassContext& context,const ContextualCopyTransportOptions& limits){
  namespace terms=detail::exact_terms;
  std::vector<ContextualCopyTransportCandidate> result;
  if(!limits.runtime_preloads||!limits.maximum_work||!limits.maximum_candidates||!limits.maximum_copy_sites)return result;
  const auto returns_count=std::count_if(ops.begin(),ops.end(),[](const auto& op){return op.kind==IrKind::Return;});
  bool quantizer=false;
  for(Id i=1;i<ops.size();++i)quantizer=quantizer||
      (ops[i].kind==IrKind::Plain&&ops[i].opcode==0x34&&ops[i-1U].kind==IrKind::Label);
  if(returns_count<2||!quantizer)return result;
  Model m(ops,context,limits);if(!m.valid)return result;
  const auto lift=analyze_post_layout_automatic_entry_lift_with_indirect_store_facts(m.items,m.flow,limits.maximum_work);
  if(!lift)return result;
  std::vector<Id> returns,queries;
  for(Id i=0;i<ops.size();++i)if(ops[i].kind==IrKind::Return&&ops[i].opcode==0x52&&
      !ops[i].meta.raw&&!ops[i].meta.manual_interaction&&!ops[i].procedure_boundary&&
      ops[i].meta.semantic_call_origins.empty()&&descriptive(ops[i].meta.roles)){
    bool external=false;
    for(Id state=0;state<m.instruction.size();++state)
      if(m.instruction[state]==i)external=external||m.entries.external[state];
    if(!external){returns.push_back(i);queries.push_back(m.item[i]);}
  }
  if(returns.empty())return result;
  const auto ranges=prove_post_layout_numeric_ranges(m.items,m.flow,*lift,queries,limits.runtime_preloads,
      std::min<Id>(100000,limits.maximum_work/2U),NumericRangeInputPolicy::ValidatedRegisterTransport);
  if(ranges.exhausted)return result;
  Id work=ranges.work;const auto spend=[&]{if(work>=limits.maximum_work)return false;++work;return true;};
  struct Body{Id begin=0,first=0,end=0;std::vector<Id> commands;};
  std::vector<Body> bodies;
  for(Id first=1;first<ops.size();++first){
    if(!spend())return result;
    if(ops[first].kind!=IrKind::Plain||ops[first].opcode!=0x34||ops[first-1U].kind!=IrKind::Label)continue;
    Id begin=first;while(begin>0&&ops[begin-1U].kind==IrKind::Label)--begin;
    if(!begin)continue;
    const auto& previous=ops[begin-1U];
    if(previous.kind!=IrKind::Return&&previous.kind!=IrKind::Jump&&previous.kind!=IrKind::IndirectJump&&
        !(previous.kind==IrKind::Stop&&previous.meta.stop_disposition==StopDisposition::Terminal))continue;
    Body body{begin,first,0,{}};bool complete=false;
    for(Id at=first;at<ops.size()&&body.commands.size()<17U;++at){
      if(!spend())return result;
      const auto& op=ops[at];if(op.kind==IrKind::Label)continue;
      if(op.kind==IrKind::Return&&op.opcode==0x52&&!op.meta.raw&&!op.meta.manual_interaction&&
          !op.procedure_boundary&&op.meta.semantic_call_origins.empty()){
        body.end=at;body.commands.push_back(at);complete=true;break;
      }
      if(op.kind!=IrKind::Plain||op.meta.raw||op.meta.manual_interaction||op.procedure_boundary||
          !op.meta.semantic_call_origins.empty()||!descriptive(op.meta.roles)||!terms::modeled_plain(op.opcode))break;
      const int code=op.opcode;
      if(!((code>=0&&code<=0x0e)||(code>=0x10&&code<=0x15)||code==0x22||code==0x25||(code>=0x30&&code<=0x35)))break;
      body.commands.push_back(at);
    }
    if(complete&&body.commands.size()>=2U){
      const int last=ops[body.commands[body.commands.size()-2U]].opcode;
      if((last>=0x10&&last<=0x13)||last==0x15||last==0x22||last==0x32||last==0x34||last==0x35)
        bodies.push_back(std::move(body));
    }
    if(bodies.size()>=8U)break;
  }
  Id tried=0;
  for(const auto source:returns){
    if(tried++>=limits.maximum_copy_sites)break;
    std::set<std::int64_t> domain;bool complete=true,reached=false;
    for(Id state=0;state<m.instruction.size();++state)if(m.instruction[state]==source){
      if(!spend())return result;
      if(ranges.unreachable_states.contains(state))continue;
      if(!lift->at(state)){complete=false;break;}
      reached=true;
      const auto value=ranges.x_before_state.find(state);
      if(value==ranges.x_before_state.end()||value->second.lower<-99999999||value->second.upper>99999999||
          value->second.upper-value->second.lower>7){complete=false;break;}
      for(auto row=value->second.lower;row<=value->second.upper;++row){domain.insert(row);if(domain.size()>8U){complete=false;break;}}
      if(!complete)break;
    }
    if(!reached||!complete||domain.empty())continue;
    std::vector<Id> continuations;
    for(Id state=0;state<m.flow.execution_states.size();++state)
      if(m.instruction[state]==source)
        continuations.insert(continuations.end(),m.flow.execution_successors[state].begin(),m.flow.execution_successors[state].end());
    // Data observations use the complete selector-qualified graph; an
    // impossible callback must not manufacture an observed return value.
    // The independent relocation below still transports the full old CFG.
    if(continuations.empty()||!prove_post_layout_stack_state_equality(m.items,m.flow,continuations,
        {{false,false,false,false},false,false},
        {.refine_runtime_selectors=false,.numeric_entry_closed=true,.transport_closed_entry_through_control=true}))continue;
    Id source_labels=source;while(source_labels>0&&ops[source_labels-1U].kind==IrKind::Label)--source_labels;
    for(const auto& body:bodies){
      if(source>=body.begin&&source<=body.end)continue;
      if(source_labels<body.end+1U&&source>=body.begin)continue;
      bool total=true;
      for(const auto row:domain){
        terms::Evaluation evaluation;const auto number=terms::number(std::to_string(row));if(!number){total=false;break;}
        evaluation.state.stack[0]=*number;evaluation.synchronize_call();
        // After int(X) the old fractional input and hidden entry copy are
        // unknown, not the chosen integer representative. Any actual use
        // must fail exact evaluation rather than invent a finite value.
        evaluation.state.x1=terms::symbolic("quantizer-input");
        evaluation.state.x2=terms::symbolic("quantizer-shadow");
        for(Id i=1;i+1U<body.commands.size();++i){
          if(!spend())return result;
          evaluation.execute(ops[body.commands[i]]);
          if(!evaluation.valid||!evaluation.observations.empty()){total=false;break;}
        }
        if(!evaluation.valid||!evaluation.observations.empty()||!evaluation.state.literal.empty())total=false;
        if(!total)break;
      }
      if(!total)continue;
      ContextualCopyTransportCandidate candidate;auto& transformed=candidate.transformation;
      std::vector<std::optional<Id>> relocated(ops.size());
      const auto append=[&](Id old){relocated[old]=transformed.ops.size();transformed.ops.push_back(ops[old]);};
      for(Id i=0;i<ops.size();++i){
        if(i==source){
          for(Id moved=body.begin;moved<=body.end;++moved){
            if(moved==body.end){
              for(Id label=source_labels;label<source;++label)append(label);
              relocated[source]=transformed.ops.size();
            }
            append(moved);
          }
        }else if((i>=body.begin&&i<=body.end)||(i>=source_labels&&i<source))continue;
        else append(i);
      }
      std::vector<Id> item(transformed.ops.size());Id index=0;
      for(Id i=0;i<transformed.ops.size();++i){item[i]=index;index+=lower_ir_to_machine({transformed.ops[i]}).size();}
      std::vector<std::optional<Id>> mapping(m.items.size());bool mapped=true;
      for(Id i=0;i<ops.size();++i){
        if(!relocated[i]){mapped=false;break;}
        for(Id cell=0;cell<lower_ir_to_machine({ops[i]}).size();++cell)mapping[m.item[i]+cell]=item[*relocated[i]]+cell;
      }
      if(!mapped)continue;
      PostLayoutExecutionRelocationOptions transport;PostLayoutReturnCompletion completion;
      completion.old_return_item=m.item[source];
      for(const auto command:body.commands)completion.body_items.push_back(item[*relocated[command]]);
      transport.pure_return_completions.push_back(std::move(completion));
      const auto image=lower_ir_to_machine(transformed.ops);const auto flow=build_post_layout_control_flow(image,m.control);
      const auto proof=prove_post_layout_execution_relocation(m.items,image,m.original,flow,mapping,transport);
      if(!proof.proved)continue;
      transformed.applied=1;
      transformed.optimizations.push_back({"quantized-pure-return-completion",
          "Removed a return by moving an existing pure quantizing helper into its physical fallthrough. Proved ordinary input bounds and exhaustive total evaluation of "+std::to_string(domain.size())+
          " complete post-quantizer rows, not floating samples or a formula recognizer. Every changed stack/X1/X2 value is unobserved on all original continuations. Branch labels still reach the retained final V/O; the rooted CFG, all frames and the additional pure execution are independently transported. All original data/selector identities remain placement obligations."});
      result.push_back(std::move(candidate));if(result.size()>=limits.maximum_candidates)return result;
    }
  }
  return result;
}

} // namespace mkpro::core::passes

