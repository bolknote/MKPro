#include "mkpro/core/passes/contextual_copy_transport.hpp"
#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/detail/cell_provenance.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/stable_register_value_flow.hpp"
#include "mkpro/core/passes/liveness_analysis.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/core/post_layout_packed_sign.hpp"
#include "mkpro/core/formal_address.hpp"
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
    if(limits.preserve_formal_execution_frames && (!limits.control || !limits.encoded_target_origins)) return;
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
          (op.target_meta.formal_opcode && !limits.preserve_formal_execution_frames) ||
          op.meta.indirect_flow_formal_targets) {
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
    std::map<std::string,int> positions;int pc=0;
    for(const auto& op:ops){if(op.kind==IrKind::Label)positions.emplace(op.name,pc);pc+=cells_per_op(op);}
    const bool closed_geometry=!limits.encoded_target_origins ||
        (limits.preserve_formal_execution_frames && std::all_of(limits.encoded_target_origins->begin(),limits.encoded_target_origins->end(),
            [&](const auto& entry){return positions.contains(entry.first) && positions.at(entry.first)==entry.second;}));
    const auto selected = closed_geometry
        ? build_selector_consistent_execution_graph(items,original)
        : build_deferred_selector_consistent_execution_graph(items,original,*limits.encoded_target_origins,
              20000,limits.preserve_formal_execution_frames).execution;
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
Plan discover(const Model& m, Id store, Id budget,
    const std::string* grounded_source = nullptr,
    const std::vector<bool>* grounded_roots = nullptr) {
  Plan plan; plan.store = store;
  if (!m.valid || store == 0 || !direct(m.ops[store]) || m.ops[store].kind != IrKind::Store ||
      static_cast<bool>(grounded_source) != static_cast<bool>(grounded_roots)) return plan;
  if (grounded_source) {
    if (grounded_roots->size()!=m.instruction.size()) return plan;
    plan.source = *grounded_source;
  } else {
    if (!direct(m.ops[store-1U])) return plan;
    plan.source = m.ops[store-1U].register_name;
  }
  plan.destination = m.ops[store].register_name;
  if (plan.source==plan.destination) return plan;
  const auto spend = [&] { if (plan.work >= budget) { plan.exhausted=true; return false; } ++plan.work; return true; };
  const auto count = m.instruction.size();
  std::vector<Id> roots;
  for (Id s=0;s<count;++s) if(m.instruction[s]==store) {
    if (!spend()) return plan;
    if (m.entries.external[s] || m.predecessors[s].empty()) return plan;
    if (grounded_roots) {
      if (!(*grounded_roots)[s]) return plan;
    } else if (!std::all_of(m.predecessors[s].begin(),m.predecessors[s].end(),
        [&](Id p){return m.instruction[p]==store-1U;})) return plan;
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
        // A selector product may repeat the same PC/return frames for distinct
        // data rows. Bind the identity to the actual invocation path, never a
        // return with merely matching physical frames (or an unreachable caller
        // whose greatest-fixed-point input still contains top equalities).
        if(result.unreachable.contains(call.call))continue;
        Id returned=call.entry, expected=m.instruction[call.entry];
        bool exact_path=true;
        for(const auto& command:call.body){
          if(!spend())return result;
          while(expected<m.ops.size() && m.ops[expected].kind==IrKind::Label)++expected;
          if(result.unreachable.contains(returned) || m.entries.external[returned] ||
              m.instruction[returned]!=expected || m.ops[expected].opcode!=command.opcode ||
              m.flow.execution_successors[returned].size()!=1U){exact_path=false;break;}
          returned=m.flow.execution_successors[returned].front();++expected;
        }
        if(!exact_path || result.unreachable.contains(returned) || m.entries.external[returned] ||
            m.flow.execution_states[returned].item_index!=call.ret)continue;
        identities[returned].insert(call.call);
        identity_users[call.call].push_back(returned);++result.finite_returns;
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

std::vector<ContextualCopyTransportCandidate> contextual_grounded_copy_transport_candidates(
    const std::vector<IrOp>& ops,const PassContext& context,const ContextualCopyTransportOptions& limits) {
  std::vector<ContextualCopyTransportCandidate> result;
  if (!limits.runtime_preloads || !limits.maximum_work || !limits.maximum_candidates ||
      !limits.maximum_copy_sites || limits.maximum_growth_cells<0) return result;
  std::vector<Id> stores;
  for (Id at=1; at<ops.size(); ++at)
    if (direct(ops[at]) && ops[at].kind==IrKind::Store && !direct(ops[at-1U]))
      stores.push_back(at);
  if (stores.empty()) return result;
  Model model(ops,context,limits);
  if (!model.valid) return result;
  const auto equality=prove_context_words(model,*limits.runtime_preloads,limits.maximum_work);
  if (!equality.proved || equality.exhausted) return result;
  Id remaining=limits.maximum_work-std::min(limits.maximum_work,equality.work),attempts=0;
  // First-entry argument stores precede incidental equal-valued local stores
  // in this independent bounded family. Neither is a source-name recognizer.
  std::vector<bool> call_entries(ops.size(),false);
  for (Id state=0; state<model.instruction.size(); ++state)
    for (const auto before:model.predecessors[state]) {
      const auto kind=ops[model.instruction[before]].kind;
      if (kind==IrKind::Call || kind==IrKind::IndirectCall)
        call_entries[model.instruction[state]]=true;
    }
  std::stable_sort(stores.begin(),stores.end(),[&](Id a,Id b){return call_entries[a]>call_entries[b];});
  for (const auto store:stores) {
    if (!remaining || result.size()>=limits.maximum_candidates) break;
    for (int reg=0; reg<15 && remaining && result.size()<limits.maximum_candidates; ++reg) {
      const auto source=std::string(1,"0123456789abcde"[reg]);
      if (source==ops[store].register_name) continue;
      std::vector<bool> grounded(model.instruction.size(),false);
      bool reached=false,valid=true;
      for (Id state=0; state<model.instruction.size(); ++state) {
        if (model.instruction[state]!=store) continue;
        if (!remaining) return result;
        --remaining;
        reached=true;
        grounded[state]=equality.unreachable.contains(state) ||
            (equality.before[state].rows[0] & (std::uint64_t{1}<<(4+reg)))!=0;
        valid=valid && grounded[state];
      }
      if (!reached || !valid) continue;
      if (attempts++>=limits.maximum_copy_sites) return result;
      auto plan=discover(model,store,remaining,&source,&grounded);
      remaining-=std::min(remaining,plan.work);
      if (!plan.valid || plan.exhausted ||
          2*static_cast<int>(plan.sinks.size())-1>limits.maximum_growth_cells) continue;
      ContextualCopyTransportCandidate candidate;
      candidate.transformation=rewrite(model,plan,candidate.materialized_stores);
      if (candidate.transformation.applied<=0) continue;
      for (Id at=0; at<candidate.materialized_stores.size(); ++at)
        candidate.transformation.ops.at(candidate.materialized_stores[at]).meta.roles.push_back(
            std::string(detail::kContextualCopyStoreOriginPrefix)+std::to_string(at));
      candidate.transformation.optimizations.push_back({"grounded-contextual-copy-transport",
          "Derived a non-adjacent copy from grounded full-machine-word X/R"+source+
          " congruence at every exact store context, including matched callee entries. "
          "Re-proved source stability, every retained destination observation, stack/X1/X2 "
          "and complete rooted return relocation; unknown recall normalization and raw/ungrounded "
          "initial values supply no equality. Selector placement remains independently mandatory."});
      result.push_back(std::move(candidate));
    }
  }
  return result;
}


std::vector<ContextualCopyTransportCandidate> contextual_anticipatory_copy_store_candidates(
    const std::vector<IrOp>& ops,const PassContext& context,const ContextualCopyTransportOptions& limits){
  std::vector<ContextualCopyTransportCandidate> result;
  if(!limits.runtime_preloads||!limits.maximum_work||!limits.maximum_candidates||!limits.maximum_copy_sites)return result;
  bool demand=false;
  for(Id i=1;i<ops.size();++i)demand=demand||(direct(ops[i])&&ops[i].kind==IrKind::Store&&
      direct(ops[i-1U])&&ops[i-1U].kind==IrKind::Recall&&ops[i].register_name!=ops[i-1U].register_name);
  if(!demand)return result;
  Model m(ops,context,limits);if(!m.valid)return result;
  const auto deaths=prove_post_layout_register_deadness(ops,m.flow,limits.maximum_work);
  const auto lift=analyze_post_layout_automatic_entry_lift_with_indirect_store_facts(m.items,m.original,limits.maximum_work);
  if(!deaths.proved||deaths.exhausted||!lift)return result;
  Id remaining=limits.maximum_work-std::min(limits.maximum_work,deaths.work),attempts=0;
  const auto original_words=prove_context_words(m,*limits.runtime_preloads,remaining);
  if(!original_words.proved||original_words.exhausted)return result;
  remaining-=std::min(remaining,original_words.work);
  for(Id copy=1;copy<ops.size()&&remaining&&result.size()<limits.maximum_candidates;++copy){
    if(!direct(ops[copy])||ops[copy].kind!=IrKind::Store||!direct(ops[copy-1U])||
        ops[copy-1U].kind!=IrKind::Recall||ops[copy].register_name==ops[copy-1U].register_name)continue;
    const auto old_plan=redundant_copy_plan(m,original_words,copy,remaining,*limits.runtime_preloads);
    remaining-=std::min(remaining,old_plan.work);
    if(old_plan.exhausted)return result;
    if(old_plan.valid)continue; // The ordinary GVN already owns this deletion.
    for(Id producer=0;producer+1U<ops.size()&&remaining&&result.size()<limits.maximum_candidates;++producer){
      if(producer==copy||!direct(ops[producer])||ops[producer].kind!=IrKind::Store||
          ops[producer].register_name!=ops[copy-1U].register_name)continue;
      const auto& destination=ops[copy].register_name;bool reached=false,safe=true;
      for(Id state=0;state<m.instruction.size();++state)if(m.instruction[state]==producer){
        reached=true;
        for(const auto next:m.flow.execution_successors[state])safe=safe&&deaths.before_state[next].contains(destination);
      }
      for(Id state=0;state<m.original.execution_states.size();++state)
        if(m.original.execution_states[state].item_index==m.item[producer])
          for(const auto next:m.original.execution_successors[state])safe=safe&&lift->at(next);
      if(!reached||!safe)continue;
      if(attempts++>=limits.maximum_copy_sites)return result;
      auto store=ops[copy];store.meta.roles.clear();store.meta.comment="anticipatory private copy at value producer";
      store.meta.source_line=ops[producer].meta.source_line;store.semantic.clear();store.target_meta={};
      auto changed=ops;changed.insert(changed.begin()+static_cast<std::ptrdiff_t>(producer+1U),std::move(store));
      const auto image=lower_ir_to_machine(changed);const auto flow=build_post_layout_control_flow(image,m.control);
      std::vector<std::optional<Id>> backward(image.size());Id from=0,to=0,inserted=0;
      for(Id i=0;i<changed.size();++i){
        const auto length=lower_ir_to_machine({changed[i]}).size();
        if(i==producer+1U)inserted=from;
        else{for(Id cell=0;cell<length;++cell)backward[from+cell]=to+cell;to+=length;}
        from+=length;
      }
      Id continuation=producer+1U;while(continuation<ops.size()&&ops[continuation].kind==IrKind::Label)++continuation;
      if(continuation==ops.size())continue;
      backward[inserted]=m.item[continuation];
      PostLayoutExecutionRelocationOptions transport;transport.erased_data_effect_items.push_back(inserted);
      const auto cfg=prove_post_layout_execution_relocation(image,m.items,flow,m.original,backward,transport);
      if(!cfg.proved)continue;
      Model proposed(changed,context,limits);if(!proposed.valid)continue;
      const auto equality=prove_context_words(proposed,*limits.runtime_preloads,remaining);
      remaining-=std::min(remaining,equality.work);
      if(!equality.proved||equality.exhausted)continue;
      const auto target=copy+(producer<copy?1U:0U);
      const auto plan=redundant_copy_plan(proposed,equality,target,remaining,*limits.runtime_preloads);
      remaining-=std::min(remaining,plan.work);
      if(!plan.valid||plan.exhausted)continue;
      ContextualCopyTransportCandidate candidate;
      candidate.transformation=rewrite(proposed,plan,candidate.materialized_stores);
      if(candidate.transformation.applied<=0)continue;
      candidate.transformation.optimizations.clear();
      candidate.transformation.optimizations.push_back({"anticipatory-contextual-copy",
          "Inserted one store beside an existing value producer only after its old destination is dead on every exact caller continuation and numeric entry is already closed. Independently proved that the later copied store is now redundant as a complete machine word or guarded non-observation, and transported every original CFG/return frame in both stages. The neutral artifact remains an alternative for stack-load elimination, finite encoding and final placement; no register/input contract or source program is changed."});
      const auto candidate_items=lower_ir_to_machine(candidate.transformation.ops);
      if(std::none_of(result.begin(),result.end(),[&](const auto& old){
          const auto items=lower_ir_to_machine(old.transformation.ops);
          return items.size()==candidate_items.size()&&std::equal(items.begin(),items.end(),candidate_items.begin(),machine_items_equal);
        }))result.push_back(std::move(candidate));
    }
  }return result;
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
  // Two captured copies can need the same home without either one being a
  // transitive ancestor of the other. Preserve the historical preferences
  // first; use only their unspent budget for these sibling affinities. A common
  // source spelling is NOT a value equality or an interference exemption.
  std::map<std::string,std::vector<Id>> siblings;
  for(Id i=1;i<ops.size();++i)
    if(direct(ops[i]) && ops[i].kind==IrKind::Store &&
        direct(ops[i-1U]) && ops[i-1U].kind==IrKind::Recall &&
        ops[i].register_name!=ops[i-1U].register_name)
      siblings[ops[i-1U].register_name].push_back(i);
  for(const auto& [source,sites]:siblings){
    (void)source;
    for(Id left=0;left<sites.size() && result.size()<maximum_requests;++left)
      for(Id right=left+1U;right<sites.size() && result.size()<maximum_requests;++right){
        const auto a=sites[left],b=sites[right];
        if(ops[a].register_name==ops[b].register_name || !unique.insert({a,b}).second)continue;
        result.push_back({{a,b}});
      }
    if(result.size()>=maximum_requests)break;
  }
  return result;
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
    const auto canonical=normalized_finite_ordinary_word(initial.value);if(!canonical)continue;
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

namespace {
// Constants stay readonly in the whole original program, including alias and
// selector write-back observations. A stored numeric equality is not enough
// to authorize an address-word mutation.
std::map<std::string, OrdinaryNumericRange> readonly_completion_constants(
    const Model& m, const std::vector<PreloadReport>& preloads, Id& work, Id maximum_work) {
  std::map<std::string, OrdinaryNumericRange> result;
  std::set<std::string> owners;
  for (const auto& word : preloads) {
    if(work>=maximum_work)return {};++work;
    if (!physical_word_register(word.register_name) ||
        !owners.insert(word.register_name).second) return {};
    const auto range = bound_ordinary_decimal(word.value);
    const auto canonical = normalized_finite_ordinary_word(word.value);
    if (!range || !canonical || word.setup_expression || word.setup_target_name ||
        word.setup_expression_text) continue;
    const int reg = register_index(word.register_name);
    bool valid = true;
    for (Id i = 0; i < m.ops.size() && valid; ++i) {
      if(work>=maximum_work)return {};++work;
      const auto& op = m.ops[i];
      const auto& effect = m.effects[i];
      if (effect.may_define_any_register) {valid=false;break;}
      if (op.kind==IrKind::IndirectStore) {
        const auto aliases=m.flow.indirect_memory_targets.find(m.item[i]);
        if(aliases==m.flow.indirect_memory_targets.end() ||
            std::find(aliases->second.begin(),aliases->second.end(),reg)!=aliases->second.end()){valid=false;break;}
      }
      if (!effect.must_defs.contains(word.register_name) && !effect.may_defs.contains(word.register_name)) continue;
      if (op.kind != IrKind::IndirectCall && op.kind != IrKind::IndirectJump &&
          op.kind != IrKind::IndirectCondJump && op.kind != IrKind::IndirectRecall &&
          op.kind != IrKind::IndirectStore) {valid=false;break;}
      if ((op.opcode & 15) != reg || reg < 7) {valid=false;break;}
      const bool memory = op.kind == IrKind::IndirectRecall || op.kind == IrKind::IndirectStore;
      const auto decoded = evaluate_indirect_address(word.register_name, *canonical,
          memory ? IndirectOperationKind::Memory : IndirectOperationKind::Flow, m.control.address_space_model);
      valid = decoded && indirect_writeback_preserves_literal_word(*decoded,*canonical);
    }
    if (valid) result.emplace(word.register_name,*range);
  }
  return result;
}

bool total_readonly_completion(const Model& m, const std::vector<Id>& body,
    std::array<OrdinaryStackValue,4> values,
    const std::map<std::string,OrdinaryNumericRange>& constants, Id& work, Id maximum_work) {
  const auto push = [&](OrdinaryStackValue value) {
    for (Id slot=3;slot>0;--slot) values[slot]=values[slot-1U];
    values[0]=std::move(value);
  };
  const auto drop = [&](OrdinaryStackValue value) {
    values[0]=std::move(value);values[1]=values[2];values[2]=values[3];
  };
  bool fresh = true;
  for (Id n=0;n+1U<body.size();++n) {
    if(work>=maximum_work)return false;++work;
    const auto& op=m.ops[body[n]];const int code=op.opcode;
    if (op.kind==IrKind::Recall) {
      const auto word=constants.find(op.register_name);if(word==constants.end())return false;
      push({word->second,true});fresh=true;continue;
    }
    if (op.kind!=IrKind::Plain) return false;
    if(code>=0 && code<=9) {
      if(!fresh)return false;
      std::string number;bool point=false;Id end=n;
      for(;end+1U<body.size();++end){
        const auto& next=m.ops[body[end]];if(next.kind!=IrKind::Plain)break;
        if(next.opcode>=0&&next.opcode<=9)number.push_back(static_cast<char>('0'+next.opcode));
        else if(next.opcode==0x0a&&!point){point=true;number.push_back('.');}
        else break;
      }
      if(end+1U<body.size()&&m.ops[body[end]].kind==IrKind::Plain&&m.ops[body[end]].opcode==0x0b){number.insert(number.begin(),'-');++end;}
      const auto range=bound_ordinary_decimal(number);if(!range)return false;
      push({*range,true});fresh=false;n=end-1U;continue;
    }
    if(code==0x14){std::swap(values[0],values[1]);fresh=false;continue;}
    if(code==0x25){const auto x=values[0];values[0]=values[1];values[1]=values[2];values[2]=values[3];values[3]=x;fresh=false;continue;}
    if(code==0x0e){push(values[0]);fresh=false;continue;}
    if(code==0x0d){values[0]={OrdinaryNumericRange{0,0,true},true};fresh=false;continue;}
    if(code>=0x10&&code<=0x13){
      const auto x=values[0],y=values[1];if(!x.ordinary||!y.ordinary)return false;
      std::optional<OrdinaryNumericRange> range;
      if(x.range&&y.range)range=bound_ordinary_binary(code,*y.range,*x.range);
      bool total=range.has_value();
      // Eight-digit decimal arithmetic: adding/subtracting a value bounded
      // by 99999999 cannot overflow an otherwise finite ordinary operand.
      // Above order 90 it is below half an ULP; below it there is exponent
      // headroom. Subnormal decimal results round to zero, not an error.
      if((code==0x10||code==0x11)&&x.range)total=true;
      // A divisor whose magnitude is at least one cannot overflow a finite
      // numerator. Decimal underflow is admitted only as a numeric value;
      // no canonical stored-word or selector fact is manufactured.
      if(code==0x13&&x.range&&(x.range->lower>=1||x.range->upper<=-1))total=true;
      if(!total)return false;drop({range,true});fresh=true;continue;
    }
    const auto x=values[0];if(!x.ordinary)return false;
    std::optional<OrdinaryNumericRange> range;
    if(x.range)range=bound_ordinary_unary(code,*x.range);
    if(code==0x35&&!range)range=OrdinaryNumericRange{-1,1,false};
    bool total=code==0x31||code==0x32||code==0x34||code==0x35;
    if(code==0x15)total=x.range&&x.range->lower>=-99&&x.range->upper<=7&&range.has_value();
    if(code==0x22)total=range.has_value();
    if(!total)return false;values[0]={range,true};fresh=selector_charge_entry_closer_opcode(code);
  }
  return true;
}
} // namespace

static std::vector<ContextualCopyTransportCandidate> contextual_return_completion_candidates_impl(
    const std::vector<IrOp>& ops,const PassContext& context,const ContextualCopyTransportOptions& limits,
    bool scalar_leaf){
  namespace terms=detail::exact_terms;
  std::vector<ContextualCopyTransportCandidate> result;
  if(!limits.runtime_preloads||!limits.maximum_work||!limits.maximum_candidates||!limits.maximum_copy_sites)return result;
  const auto returns_count=std::count_if(ops.begin(),ops.end(),[](const auto& op){return op.kind==IrKind::Return;});
  bool quantizer=false;
  for(Id i=1;i<ops.size();++i)quantizer=quantizer||
      (ops[i].kind==IrKind::Plain&&ops[i].opcode==0x34&&ops[i-1U].kind==IrKind::Label);
  if(returns_count<2||(!scalar_leaf&&!quantizer))return result;
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
  const auto prove_ranges=scalar_leaf ? prove_post_layout_stack_numeric_values : prove_post_layout_numeric_ranges;
  const auto ranges=prove_ranges(m.items,m.flow,*lift,queries,limits.runtime_preloads,
      std::min<Id>(100000,limits.maximum_work/2U),NumericRangeInputPolicy::ValidatedRegisterTransport);
  if(ranges.exhausted)return result;
  Id work=ranges.work;
  const auto constants=scalar_leaf ? readonly_completion_constants(m,*limits.runtime_preloads,work,limits.maximum_work) :
      std::map<std::string,OrdinaryNumericRange>{};
  if(work>=limits.maximum_work)return result;
  const auto spend=[&]{if(work>=limits.maximum_work)return false;++work;return true;};
  struct Body{Id begin=0,first=0,end=0;std::vector<Id> commands;};
  std::vector<Body> bodies;
  for(Id first=1;first<ops.size();++first){
    if(!spend())return result;
    if(ops[first-1U].kind!=IrKind::Label)continue;
    if(!scalar_leaf && (ops[first].kind!=IrKind::Plain||ops[first].opcode!=0x34))continue;
    if(scalar_leaf && (ops[first].kind!=IrKind::Plain && ops[first].kind!=IrKind::Recall))continue;
    if(scalar_leaf && ops[first].kind==IrKind::Plain && ops[first].opcode==0x34)continue;
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
      if(scalar_leaf && direct(op) && op.kind==IrKind::Recall && constants.contains(op.register_name)){body.commands.push_back(at);continue;}
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
    std::set<std::int64_t> domain;std::vector<Id> source_states;bool complete=true,reached=false;
    for(Id state=0;state<m.instruction.size();++state)if(m.instruction[state]==source){
      if(!spend())return result;
      if(ranges.unreachable_states.contains(state))continue;
      if(!lift->at(state)){complete=false;break;}
      reached=true;source_states.push_back(state);
      if(scalar_leaf){if(!ranges.stack_before_state.contains(state)){complete=false;break;}domain.insert(0);continue;}
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
      if(scalar_leaf)for(const auto state:source_states){
        if(!spend()||!total_readonly_completion(m,body.commands,ranges.stack_before_state.at(state),constants,work,limits.maximum_work)){total=false;break;}
      }
      else for(const auto row:domain){
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
      transformed.optimizations.push_back({scalar_leaf?"total-readonly-return-completion":"quantized-pure-return-completion",
          (scalar_leaf?"Removed a return by moving an existing readonly scalar helper into its physical fallthrough. Proved every added arithmetic operation total over every original caller stack domain and immutable word; no finite samples or input assumptions. ":"Removed a return by moving an existing pure quantizing helper into its physical fallthrough. Proved ordinary input bounds and exhaustive total evaluation of "+std::to_string(domain.size())+" complete post-quantizer rows. ")+
          "Every changed stack/X1/X2 value is unobserved on all original continuations. Branch labels still reach the retained final V/O; the rooted CFG, all frames and the additional pure execution are independently transported. All original data/selector identities remain placement obligations."});
      result.push_back(std::move(candidate));if(result.size()>=limits.maximum_candidates)return result;
    }
  }
  return result;
}

std::vector<ContextualCopyTransportCandidate> contextual_pure_return_completion_candidates(
    const std::vector<IrOp>& ops,const PassContext& context,const ContextualCopyTransportOptions& limits){
  return contextual_return_completion_candidates_impl(ops,context,limits,false);
}
std::vector<ContextualCopyTransportCandidate> contextual_total_return_completion_candidates(
    const std::vector<IrOp>& ops,const PassContext& context,const ContextualCopyTransportOptions& limits){
  return contextual_return_completion_candidates_impl(ops,context,limits,true);
}

std::vector<ContextualCopyTransportCandidate> contextual_edge_return_completion_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const ContextualCopyTransportOptions& limits) {
  std::vector<ContextualCopyTransportCandidate> result;
  if (!limits.runtime_preloads || !limits.maximum_work || !limits.maximum_candidates ||
      !limits.maximum_copy_sites) return result;
  std::map<std::string, Id> labels;
  for (Id i = 0; i < ops.size(); ++i) if (ops[i].kind == IrKind::Label) {
    Id first = i + 1U;
    while (first < ops.size() && ops[first].kind == IrKind::Label) ++first;
    if (!labels.emplace(ops[i].name, first).second) return result;
  }
  std::vector<std::pair<Id, Id>> branches;
  for (Id i = 0; i < ops.size(); ++i) {
    const auto& op = ops[i];
    if (op.kind != IrKind::CondJump || op.meta.raw || op.meta.manual_interaction ||
        op.procedure_boundary || !op.meta.semantic_call_origins.empty() ||
        !descriptive(op.meta.roles) || !descriptive(op.target_meta.roles) ||
        (op.opcode != 0x57 && op.opcode != 0x5e && op.opcode != 0x59 && op.opcode != 0x5c)) continue;
    const auto* label = std::get_if<std::string>(&op.target);
    if (!label || !labels.contains(*label)) continue;
    const auto destination = labels.at(*label);
    if (destination < ops.size() && ops[destination].kind == IrKind::Return &&
        ops[destination].opcode == 0x52 && !ops[destination].meta.raw &&
        !ops[destination].meta.manual_interaction && !ops[destination].procedure_boundary)
      branches.emplace_back(i, destination);
  }
  if (branches.empty()) return result;
  Model m(ops, context, limits);
  if (!m.valid) return result;
  const auto lift = analyze_post_layout_automatic_entry_lift_with_indirect_store_facts(
      m.items, m.flow, limits.maximum_work);
  if (!lift) return result;
  std::vector<Id> queries;
  for (const auto& [branch, target] : branches) { (void)branch; queries.push_back(m.item[target]); }
  std::sort(queries.begin(), queries.end());
  queries.erase(std::unique(queries.begin(), queries.end()), queries.end());
  const auto ranges = prove_post_layout_stack_numeric_values(m.items, m.flow, *lift,
      queries, limits.runtime_preloads, std::min<Id>(100000, limits.maximum_work / 2U),
      NumericRangeInputPolicy::ValidatedRegisterTransport);
  if (ranges.exhausted) return result;
  Id work = ranges.work;
  const auto constants = readonly_completion_constants(m, *limits.runtime_preloads, work,
      limits.maximum_work);
  const auto spend = [&] { if (work >= limits.maximum_work) return false; ++work; return true; };
  Id tried = 0;
  for (const auto& [branch, target] : branches) {
    if (tried++ >= limits.maximum_copy_sites) break;
    std::set<Id> return_states;
    bool valid = true;
    for (Id state = 0; state < m.instruction.size(); ++state) if (m.instruction[state] == branch) {
      if (!spend()) return result;
      for (const auto& edge : m.flow.execution_edges[state])
        if (edge.kind == PostLayoutExecutionEdgeKind::DirectTarget) {
          const auto next = edge.target_state;
          if (m.instruction[next] != target) { valid = false; break; }
          if (!ranges.unreachable_states.contains(next)) return_states.insert(next);
        }
    }
    if (!valid || return_states.empty()) continue;
    std::vector<Id> continuations;
    for (const auto state : return_states) {
      if (!lift->at(state) || !ranges.stack_before_state.contains(state) || m.entries.external[state]) {
        valid = false; break;
      }
      continuations.insert(continuations.end(), m.flow.execution_successors[state].begin(),
          m.flow.execution_successors[state].end());
    }
    // The selected edge may forget its result. Other invocations of this same
    // physical return do not inherit that permission.
    if (!valid || continuations.empty() || !prove_post_layout_stack_state_equality(
        m.items, m.flow, continuations, {{false, false, false, false}, false, false},
        {.refine_runtime_selectors = false, .numeric_entry_closed = true,
         .transport_closed_entry_through_control = true})) continue;
    std::set<Id> first_commands;
    for (const auto& [label, first] : labels) { (void)label; if (first < target) first_commands.insert(first); }
    for (const auto first : first_commands) {
      if (!spend()) return result;
      std::vector<Id> body;
      bool complete = false;
      for (Id i = first; i <= target && body.size() < 17U; ++i) {
        if (!spend()) return result;
        const auto& op = ops[i];
        if (op.kind == IrKind::Label) continue;
        if (i == target) { body.push_back(i); complete = true; break; }
        if (op.kind == IrKind::Recall && direct(op) && constants.contains(op.register_name)) {
          body.push_back(i); continue;
        }
        if (op.kind != IrKind::Plain || op.meta.raw || op.meta.manual_interaction ||
            op.procedure_boundary || !op.meta.semantic_call_origins.empty() ||
            !descriptive(op.meta.roles) || !detail::exact_terms::modeled_plain(op.opcode)) break;
        body.push_back(i);
      }
      if (!complete || body.size() < 2U) continue;
      // Both paths must close numeric entry, not merely produce equal-looking
      // numbers with a potentially stale exponent/decimal shadow.
      if (!selector_charge_entry_closer_opcode(ops[body[body.size() - 2U]].opcode)) continue;
      bool total = true;
      for (const auto state : return_states)
        if (!spend() || !total_readonly_completion(m, body, ranges.stack_before_state.at(state),
            constants, work, limits.maximum_work)) { total = false; break; }
      if (!total) continue;
      const auto label = std::find_if(labels.begin(), labels.end(),
          [&](const auto& entry) { return entry.second == first; });
      if (label == labels.end()) continue;
      ContextualCopyTransportCandidate candidate;
      auto& transformed = candidate.transformation;
      transformed.ops = ops;
      transformed.ops[branch].target = label->first;
      std::vector<std::optional<Id>> mapping(m.items.size());
      for (Id i = 0; i < mapping.size(); ++i) mapping[i] = i;
      PostLayoutExecutionRelocationOptions transport;
      PostLayoutReturnCompletion completion;
      completion.old_return_item = m.item[target];
      for (const auto command : body) completion.body_items.push_back(m.item[command]);
      completion.redirected_direct_branch_items.push_back(m.item[branch]);
      transport.pure_return_completions.push_back(std::move(completion));
      const auto image = lower_ir_to_machine(transformed.ops);
      const auto flow = build_post_layout_control_flow(image, m.control);
      const auto proof = prove_post_layout_execution_relocation(m.items, image, m.original,
          flow, mapping, transport);
      if (!proof.proved) continue;
      transformed.applied = 1;
      transformed.optimizations.push_back({"edge-qualified-return-completion",
          "Redirected one ignored conditional return edge through an existing total readonly leaf. "
          "Proved every caller domain, changed stack/X1/X2 non-observation, numeric entry closure "
          "and exact rooted return frames. Other callers reach the unchanged V/O; the neutral "
          "candidate requires a separately profitable selector/layout transaction."});
      result.push_back(std::move(candidate));
      if (result.size() >= limits.maximum_candidates) return result;
    }
  }
  return result;
}

std::vector<ContextualCopyTransportCandidate> contextual_redundant_literal_store_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const ContextualCopyTransportOptions& limits) {
  std::vector<ContextualCopyTransportCandidate> result;
  if (!limits.maximum_work || !limits.maximum_candidates || !limits.maximum_copy_sites)
    return result;
  std::vector<Id> candidates;
  for (Id i = 0; i + 1U < ops.size() && candidates.size() < limits.maximum_copy_sites; ++i) {
    const auto& literal = ops[i];
    if (literal.kind == IrKind::Plain && literal.opcode >= 1 && literal.opcode <= 9 &&
        !has_rewrite_barrier(literal) && literal.meta.roles.empty() &&
        literal.meta.semantic_call_origins.empty() && direct(ops[i + 1U]) &&
        ops[i + 1U].kind == IrKind::Store)
      candidates.push_back(i);
  }
  if (candidates.empty()) return result;
  const Model m(ops, context, limits);
  if (!m.valid) return result;
  const auto lift = analyze_post_layout_automatic_entry_lift_with_indirect_store_facts(
      m.items, m.flow, limits.maximum_work);
  if (!lift) return result;
  std::vector<Id> queries;
  for (const auto i : candidates) queries.push_back(m.item[i]);
  const auto facts = prove_post_layout_stack_numeric_values(
      m.items, m.flow, *lift, queries, limits.runtime_preloads, limits.maximum_work,
      NumericRangeInputPolicy::ValidatedRegisterTransport);
  if (facts.exhausted) return result;
  for (const auto literal : candidates) {
    bool valid = true, reached = false;
    std::vector<Id> continuations;
    for (Id state = 0; state < m.instruction.size(); ++state) {
      if (m.instruction[state] != literal) continue;
      reached = true;
      const auto x = facts.x_before_state.find(state);
      if (m.entries.external[state] || !lift->at(state) ||
          !facts.canonical_small_integer_states.contains(state) ||
          x == facts.x_before_state.end() || x->second.lower != ops[literal].opcode ||
          x->second.upper != ops[literal].opcode ||
          m.flow.execution_successors[state].size() != 1U) {
        valid = false;
        break;
      }
      const auto stored = m.flow.execution_successors[state].front();
      if (m.instruction[stored] != literal + 1U) {
        valid = false;
        break;
      }
      continuations.insert(continuations.end(), m.flow.execution_successors[stored].begin(),
                           m.flow.execution_successors[stored].end());
    }
    std::sort(continuations.begin(), continuations.end());
    continuations.erase(std::unique(continuations.begin(), continuations.end()), continuations.end());
    // The unchanged store writes the same canonical word and closes entry.
    // It does not certify X2: omitting the fresh digit may preserve a different
    // entry shadow. Last-X is unchanged; every Y/Z/T/X2 difference must die.
    if (!valid || !reached || continuations.empty() ||
        !prove_post_layout_stack_state_equality(m.items, m.flow, continuations,
            {.stack_equal = {true, false, false, false},
             .x2_equal = false, .x1_equal = true},
            {.refine_runtime_selectors = false, .numeric_entry_closed = true,
             .transport_closed_entry_through_control = true}))
      continue;
    ContextualCopyTransportCandidate candidate;
    auto& transformation = candidate.transformation;
    transformation.ops = ops;
    transformation.ops.erase(transformation.ops.begin() + static_cast<std::ptrdiff_t>(literal));
    const auto image = lower_ir_to_machine(transformation.ops);
    std::vector<std::optional<Id>> mapping(m.items.size());
    for (Id i = 0; i < m.items.size(); ++i)
      mapping[i] = i - (i > m.item[literal] ? 1U : 0U);
    PostLayoutExecutionRelocationOptions relocation;
    relocation.erased_redundant_literal_item = m.item[literal];
    const auto after = build_post_layout_control_flow(image, m.control);
    if (!prove_post_layout_execution_relocation(m.items, image, m.original, after,
                                                mapping, relocation).proved)
      continue;
    transformation.applied = 1;
    transformation.optimizations.push_back({"context-qualified-redundant-literal-store",
        "Removed a fresh one-digit literal before an unchanged direct store. Every exact caller "
        "already has its canonical machine word in X; all removed stack-lift effects die before "
        "observation, with entry/X1/X2 and rooted command/return-frame transport proved separately. "
        "Original data and selector equations remain mandatory for final placement."});
    result.push_back(std::move(candidate));
    if (result.size() >= limits.maximum_candidates) break;
  }
  return result;
}



std::vector<ContextualCopyTransportCandidate> contextual_memory_backed_entry_candidates(
    const std::vector<IrOp>& ops,const PassContext& context,const ContextualCopyTransportOptions& limits){
  std::vector<ContextualCopyTransportCandidate> result;
  if(!limits.runtime_preloads||!limits.control||!limits.encoded_target_origins||!limits.maximum_candidates)return result;
  Model m(ops,context,limits);if(!m.valid)return result;
  const auto lift=analyze_post_layout_automatic_entry_lift_with_indirect_store_facts(m.items,m.flow,limits.maximum_work);
  if(!lift)return result;
  for(Id root=1;root+3U<ops.size();++root){
    // The leading recall keeps its command identity and all provenance. Its
    // annotations are not a reason to reject an independent argument store;
    // only actual observations/opaque execution prevent moving across it.
    const bool unchanged_recall=ops[root].kind==IrKind::Recall &&
        ops[root].opcode>=0x60 && ops[root].opcode<=0x6e &&
        ops[root].register_name==std::string(1,"0123456789abcde"[ops[root].opcode&15]) &&
        !ops[root].meta.raw && !ops[root].meta.manual_interaction &&
        !ops[root].meta.logical_register_analysis && !ops[root].procedure_boundary;
    if(!unchanged_recall||ops[root+1U].kind!=IrKind::Plain||
        ops[root+1U].opcode!=0x14||ops[root+1U].meta.raw||ops[root+1U].meta.manual_interaction||
        ops[root+1U].meta.logical_register_analysis||ops[root+1U].procedure_boundary||
        !ops[root+1U].meta.semantic_call_origins.empty()||!direct(ops[root+2U])||
        ops[root+2U].kind!=IrKind::Store||ops[root].register_name==ops[root+2U].register_name)continue;
    const auto reg=ops[root+2U].register_name;
    Id labels=root;while(labels>0&&ops[labels-1U].kind==IrKind::Label)--labels;
    if(labels==root||labels==0)continue;
    std::set<Id> callers,erased_recalls,bypassed_stores;
    std::vector<Id> called,fallthrough;
    bool valid=true,reached=false;
    for(Id s=0;s<m.instruction.size()&&valid;++s){
      const auto i=m.instruction[s];if(i<root||i>root+2U)continue;
      if(m.entries.external[s]||m.predecessors[s].empty()){valid=false;break;}
      if(m.flow.execution_states[s].formal_opcode &&
          formal_address_info(*m.flow.execution_states[s].formal_opcode,m.control.address_space_model).kind!=FormalAddressKind::Official){valid=false;break;}
      if(i!=root){for(const auto p:m.predecessors[s])if(m.instruction[p]+1U!=i)valid=false;continue;}
      reached=true;bool called_entry=false,physical_entry=false;
      for(const auto p:m.predecessors[s]){
        const auto at=m.instruction[p];const auto& caller=ops[at];
        const bool call=caller.kind==IrKind::Call && std::any_of(m.flow.execution_edges[p].begin(),m.flow.execution_edges[p].end(),
            [&](const auto& edge){return edge.target_state==s&&edge.kind==PostLayoutExecutionEdgeKind::DirectTarget;});
        if(call){
          if(at==0||!direct(ops[at-1U])||ops[at-1U].kind!=IrKind::Recall||ops[at-1U].register_name!=reg||
              caller.meta.raw||caller.meta.manual_interaction||caller.target_meta.formal_opcode||m.predecessors[p].empty()){valid=false;break;}
          for(const auto q:m.predecessors[p])if(m.instruction[q]!=at-1U||m.entries.external[q]||m.flow.execution_successors[q]!=std::vector<Id>{p})valid=false;
          callers.insert(at);erased_recalls.insert(at-1U);called_entry=true;
        }else{
          physical_entry=true;
          for(const auto& edge:m.flow.execution_edges[p])if(edge.target_state==s){
            if(edge.kind==PostLayoutExecutionEdgeKind::Fallthrough){
              if(at+1U!=labels)valid=false;
            }else if(edge.kind==PostLayoutExecutionEdgeKind::Return){
              const auto& before=m.flow.execution_states[p];
              if(before.return_stack.empty()||before.return_stack.back()!=m.flow.execution_states[s].address||
                  (ops[labels-1U].kind!=IrKind::Call&&ops[labels-1U].kind!=IrKind::IndirectCall))valid=false;
              if(!before.formal_return_stack.empty()&&before.formal_return_stack.back()&&
                  formal_address_info(*before.formal_return_stack.back(),m.control.address_space_model).kind!=FormalAddressKind::Official)valid=false;
            }else valid=false;
          }
        }
      }
      if(called_entry&&physical_entry){valid=false;break;}
      auto tail=s;Id definition=s;
      for(Id n=0;n<3U;++n){if(n==2U)definition=tail;if(m.flow.execution_successors[tail].size()!=1U){valid=false;break;}tail=m.flow.execution_successors[tail].front();}
      if(valid){if(m.instruction[tail]!=root+3U)valid=false;else{
        (called_entry?called:fallthrough).push_back(tail);if(called_entry)bypassed_stores.insert(definition);
      }}
    }
    if(!valid||!reached||callers.empty()||fallthrough.empty())continue;
    // No computed/indirect entry may silently inherit the memory ABI. Every
    // bypassed caller explicitly supplied the same canonical memory word.
    for(const auto& [source,targets]:m.original.indirect_flow_targets){(void)source;
      for(const auto& target:targets)if(target.item_index==m.item[root])valid=false;}
    if(!valid)continue;
    std::vector<Id> queries;for(const auto call:callers){queries.push_back(m.item[call-1U]);queries.push_back(m.item[call]);}
    const auto facts=prove_post_layout_numeric_ranges(m.items,m.flow,*lift,queries,limits.runtime_preloads,
        limits.maximum_work,context.options.ordinary_decimal_keyboard_input?NumericRangeInputPolicy::ValidatedOrdinaryDecimalInput:NumericRangeInputPolicy::ValidatedRegisterTransport);
    if(facts.exhausted)continue;
    bool all_canonical=true;
    for(Id s=0;s<m.instruction.size();++s)if(erased_recalls.contains(m.instruction[s])){
      if(!facts.canonical_memory_load_states.contains(s))all_canonical=false;
      if(m.flow.execution_states[s].formal_opcode &&
          formal_address_info(*m.flow.execution_states[s].formal_opcode,m.control.address_space_model).kind!=FormalAddressKind::Official)valid=false;
    }
    if(!valid)continue;

    if(!all_canonical){
      // The source store normalizes a recalled one-digit counter. Its raw
      // memory may instead be the denormalized FL write-back. Keep that
      // distinction: permit only recall/positive-FL observations of this
      // definition epoch, never arbitrary selector or physical-word reads.
      const int argument=register_index(reg);
      std::map<Id,std::vector<int>> memory_queries;
      for(const auto recall:erased_recalls)memory_queries[m.item[recall]]={argument};
      std::set<Id> visited;std::vector<Id> pending=called;
      while(!pending.empty()&&valid){
        if(visited.size()>=limits.maximum_work){valid=false;break;}
        const auto s=pending.back();pending.pop_back();if(!visited.insert(s).second)continue;
        const auto i=m.instruction[s];const auto& op=ops[i];
        if(op.kind==IrKind::Store && op.register_name==reg && !bypassed_stores.contains(s))continue;
        if(op.meta.raw || (op.kind==IrKind::Stop &&
            std::find(op.meta.roles.begin(),op.meta.roles.end(),kTypedDisplayObservationRole)==op.meta.roles.end())){valid=false;break;}
        const int family=op.opcode&0xf0;
        if((family>=0x70&&family<=0xe0)&&(op.opcode&15)==argument){valid=false;break;}
        if(op.kind==IrKind::IndirectStore||op.kind==IrKind::IndirectRecall){
          const auto aliases=m.flow.indirect_memory_targets.find(m.item[i]);
          if(aliases==m.flow.indirect_memory_targets.end() ||
              std::find(aliases->second.begin(),aliases->second.end(),argument)!=aliases->second.end()){valid=false;break;}
        }
        constexpr std::array<int,4> loops{0x5d,0x5b,0x58,0x5a};
        const bool counter=argument<4 && op.kind==IrKind::Loop &&
            op.opcode==loops[static_cast<Id>(argument)] && op.counter=="L"+reg;
        if(counter)memory_queries[m.item[i]]={argument};
        else if(op.kind!=IrKind::Recall && (m.effects[i].uses.contains(reg)||m.effects[i].uses_all_registers) &&
            op.kind!=IrKind::IndirectStore && op.kind!=IrKind::IndirectRecall){valid=false;break;}
        pending.insert(pending.end(),m.flow.execution_successors[s].begin(),m.flow.execution_successors[s].end());
      }
      if(!valid)continue;
      const auto memory=prove_post_layout_register_numeric_values(m.items,m.flow,*lift,memory_queries,
          limits.runtime_preloads,limits.maximum_work,
          context.options.ordinary_decimal_keyboard_input?NumericRangeInputPolicy::ValidatedOrdinaryDecimalInput:NumericRangeInputPolicy::ValidatedRegisterTransport);
      if(memory.exhausted)continue;
      for(Id s=0;s<m.instruction.size();++s){
        if(!memory_queries.contains(m.item[m.instruction[s]]))continue;
        // A loop query is required only in the changed definition epoch.
        if(!erased_recalls.contains(m.instruction[s]) && !visited.contains(s))continue;
        const auto state=memory.values.find(s);
        if(state==memory.values.end()||!state->second.contains(argument)){valid=false;break;}
        const auto& value=state->second.at(argument);
        if(!value.ordinary||!value.range||!value.range->integral||value.range->lower<1||value.range->upper>9){valid=false;break;}
      }
      if(!valid)continue;
    }
    auto proof_items=m.items;
    proof_items[m.item[root+1U]].raw=true;proof_items[m.item[root+2U]].raw=true;
    for(const auto recall:erased_recalls)proof_items[m.item[recall]].raw=true;
    const PostLayoutStackEntryProofOptions proof_options{.refine_runtime_selectors=false,.numeric_entry_closed=true,
        .transport_closed_entry_through_control=true,.use_indirect_store_entry_facts=true};
    if(!prove_post_layout_stack_state_equality(proof_items,m.flow,called,
        {{true,true,true,true},false,true},proof_options)||
        !prove_post_layout_stack_state_equality(proof_items,m.flow,fallthrough,
        {{true,true,false,false},false,true},proof_options))continue;
    ContextualCopyTransportCandidate candidate;auto& out=candidate.transformation;
    for(Id i=0;i<ops.size();++i){
      if(i==labels)out.ops.push_back(ops[root+2U]);
      if(i==root+2U||erased_recalls.contains(i))continue;
      if(i==root+1U){auto recall=make_recall(reg);recall.meta.source_line=ops[i].meta.source_line;out.ops.push_back(std::move(recall));}
      else out.ops.push_back(ops[i]);
    }
    out.applied=1;out.optimizations.push_back({"contextual-memory-backed-helper-entry",
        "Replaced caller-recall/shared-swap-store argument materialization with a memory-backed helper entry. Bypassing callers prove either canonical-word equality or a closed ordinary positive-integral recall/FL epoch relation up to a retained overwrite, without canonicalizing the actual counter word. Physical fallthrough and ordinary saved-return entries retain the moved store. Proved all changed deeper-stack and X2 values unobserved, excluded indirect/external and unproved word observations, and barred changed instructions from circular convergence. Final ABI geometry and formal frames remain independent placement obligations."});
    result.push_back(std::move(candidate));if(result.size()>=limits.maximum_candidates)return result;
  }
  return result;
}

namespace {
struct LazySignPlan {
  Id projection=0, store=0; int reg=-1;
  std::set<Id> calls, reads;
};
std::optional<LazySignPlan> lazy_sign_plan(const Model& m, Id projection,
    const ContextualCopyTransportOptions& limits) {
  const auto trace = [&](std::string_view stage) {
    if (std::getenv("MKPRO_NATIVE_TRACE_COPY_TRANSPORT"))
      std::cerr << "[lazy-unit-projection] at=" << projection << ' ' << stage << '\n';
  };
  const auto& op=m.ops[projection];
  if(op.kind!=IrKind::Plain || op.opcode!=0x32 || op.meta.raw || op.meta.manual_interaction ||
      projection+1U>=m.ops.size() || !direct(m.ops[projection+1U]) ||
      m.ops[projection+1U].kind!=IrKind::Store) return std::nullopt;
  const Id store=projection+1U; const int reg=register_index(m.ops[store].register_name);
  trace("shape proved");
  const auto lift=analyze_post_layout_automatic_entry_lift_with_indirect_store_facts(m.items,m.flow,limits.maximum_work);
  if(!lift)return std::nullopt;
  std::vector<Id> states;
  for(Id s=0;s<m.instruction.size();++s)if(m.instruction[s]==projection)states.push_back(s);
  const auto signs=prove_post_layout_packed_sign_inputs(m.items,m.flow,*lift,states,
      limits.runtime_preloads,limits.maximum_work);
  if(states.empty()||signs.exhausted)return std::nullopt;
  trace("entry numeric proof completed");
  LazySignPlan plan{projection,store,reg,{},{}};
  std::optional<PostLayoutNumericRangeProof> ordinary_inputs;
  const auto ordinary_positive = [&](Id state) {
    if (!ordinary_inputs) {
      const auto remaining = limits.maximum_work - std::min(limits.maximum_work, signs.work);
      if (!remaining) return false;
      ordinary_inputs = prove_post_layout_stack_numeric_values(m.items, m.flow, *lift,
          {m.item[projection]}, limits.runtime_preloads, remaining,
          NumericRangeInputPolicy::ValidatedRegisterTransport);
    }
    if (ordinary_inputs->exhausted || !ordinary_inputs->stack_before_state.contains(state))
      return false;
    const auto& value = ordinary_inputs->stack_before_state.at(state).front();
    return value.ordinary && ((value.range && value.range->lower > 0) ||
        (value.enclosure && value.enclosure->lower > 0));
  };
  std::vector<Id> raw_after,normalized_after;
  std::set<Id> producer_states;
  for(const auto s:states){
    if (std::getenv("MKPRO_NATIVE_TRACE_COPY_TRANSPORT"))
      std::cerr << "[lazy-unit-projection] state=" << s << " external=" << m.entries.external[s]
                << " predecessors=" << m.predecessors[s].size()
                << " successors=" << m.flow.execution_successors[s].size()
                << " sign=" << (signs.signs.contains(s) ? signs.signs.at(s) : 9) << '\n';
    if(m.entries.external[s] || m.predecessors[s].size()!=1U ||
        m.flow.execution_successors[s].size()!=1U)return std::nullopt;
    const auto next=m.flow.execution_successors[s].front();
    if(m.instruction[next]!=store || m.predecessors[next]!=std::vector<Id>{s})return std::nullopt;
    producer_states.insert(next);
    const auto previous=m.predecessors[s].front();
    const auto& caller=m.ops[m.instruction[previous]];
    const bool direct_call=caller.kind==IrKind::Call &&
        std::any_of(m.flow.execution_edges[previous].begin(),m.flow.execution_edges[previous].end(),
            [&](const auto& e){return e.target_state==s && e.kind==PostLayoutExecutionEdgeKind::DirectTarget;});
    auto& roots=direct_call?normalized_after:raw_after;
    if(direct_call){
      if(caller.meta.raw||caller.meta.manual_interaction||caller.target_meta.formal_opcode)return std::nullopt;
      plan.calls.insert(m.instruction[previous]);
    }else if((!signs.signs.contains(s)||signs.signs.at(s)!=1) &&
        !ordinary_positive(s))return std::nullopt;
    roots.insert(roots.end(),m.flow.execution_successors[next].begin(),m.flow.execution_successors[next].end());
  }
  if(raw_after.empty()||plan.calls.empty())return std::nullopt;
  trace("raw and normalized caller classes proved");
  // Source-unit reaching definitions, on matched return contexts. Bottom is
  // not a proved unit. Entry and every unrelated definition are non-unit.
  std::vector<std::optional<bool>> unit(m.instruction.size());std::vector<Id> work;
  const auto merge=[&](Id s,bool value){if(unit[s]){value=value&&*unit[s];if(value==*unit[s])return;}unit[s]=value;work.push_back(s);};
  for(Id s=0;s<unit.size();++s)if(m.entries.external[s]||m.entries.main[s])merge(s,false);
  Id spent=signs.work+(ordinary_inputs ? ordinary_inputs->work : 0);
  while(!work.empty()){
    if(++spent>=limits.maximum_work)return std::nullopt;
    const auto s=work.back();work.pop_back();const auto i=m.instruction[s];const auto& effect=m.effects[i];
    bool value=*unit[s];
    if(i==store)value=true;
    else if(effect.must_defs.contains(m.ops[store].register_name)||effect.may_defs.contains(m.ops[store].register_name))value=false;
    if(effect.may_define_any_register){
      const auto aliases=m.flow.indirect_memory_targets.find(m.item[i]);
      if(m.ops[i].kind!=IrKind::IndirectStore || aliases==m.flow.indirect_memory_targets.end() ||
          std::find(aliases->second.begin(),aliases->second.end(),reg)!=aliases->second.end())value=false;
    }
    for(const auto next:m.flow.execution_successors[s])merge(next,value);
  }
  std::set<Id> visited;work=raw_after;
  while(!work.empty()){
    if(++spent>=limits.maximum_work)return std::nullopt;
    const auto s=work.back();work.pop_back();if(!visited.insert(s).second)continue;
    const auto i=m.instruction[s];const auto& here=m.ops[i];
    if(here.meta.raw || (here.kind==IrKind::Stop &&
        std::find(here.meta.roles.begin(),here.meta.roles.end(),kTypedDisplayObservationRole)==here.meta.roles.end()))return std::nullopt;
    if(here.kind==IrKind::Store && here.register_name==m.ops[store].register_name)continue;
    const int family=here.opcode&0xf0;
    if((family>=0x70&&family<=0xe0)&&(here.opcode&15)==reg)return std::nullopt;
    if(here.kind==IrKind::IndirectStore||here.kind==IrKind::IndirectRecall){
      const auto aliases=m.flow.indirect_memory_targets.find(m.item[i]);
      if(aliases==m.flow.indirect_memory_targets.end() ||
          std::find(aliases->second.begin(),aliases->second.end(),reg)!=aliases->second.end())return std::nullopt;
    }
    if(here.kind==IrKind::Recall && here.register_name==m.ops[store].register_name){
      if(!unit[s].value_or(false))return std::nullopt;plan.reads.insert(i);
    }else if(m.effects[i].uses_all_registers || m.effects[i].uses.contains(m.ops[store].register_name)){
      if(here.kind!=IrKind::IndirectStore && here.kind!=IrKind::IndirectRecall)return std::nullopt;
    }
    work.insert(work.end(),m.flow.execution_successors[s].begin(),m.flow.execution_successors[s].end());
  }
  if(plan.reads.empty() || static_cast<int>(plan.calls.size()+plan.reads.size())-1>limits.maximum_growth_cells)return std::nullopt;
  trace("all reaching definitions and projected reads proved");
  auto proof_items=m.items;proof_items[m.item[projection]].raw=true;
  const PostLayoutStackEntryProofOptions proof_options{.refine_runtime_selectors=false,
      .numeric_entry_closed=true,.transport_closed_entry_through_control=true,.use_indirect_store_entry_facts=true};
  // A decoded recall returns the same canonical unit. Its extra physical
  // last-X effect is checked independently below; it is not an equality axiom.
  if(!prove_post_layout_stack_state_equality(proof_items,m.flow,raw_after,
      {{false,true,true,true},false,false},proof_options) ||
      !prove_post_layout_stack_state_equality(proof_items,m.flow,normalized_after,
      {{true,true,true,true},false,true},proof_options))return std::nullopt;
  trace("caller hidden-state continuations proved");
  for(const auto read:plan.reads){
    std::vector<Id> continuations;
    for(Id s=0;s<m.instruction.size();++s)if(m.instruction[s]==read){
      if(!unit[s].value_or(false))return std::nullopt;
      continuations.insert(continuations.end(),m.flow.execution_successors[s].begin(),m.flow.execution_successors[s].end());
    }
    if(continuations.empty()||!prove_post_layout_stack_state_equality(proof_items,m.flow,continuations,
        {{true,true,true,true},true,false},proof_options))return std::nullopt;
  }
  trace("every decoder continuation proved");
  return plan;
}

bool zero_return_entry(const Model& m,Id ret) {
  bool reached=false;
  for(Id s=0;s<m.instruction.size();++s)if(m.instruction[s]==ret){
    if(m.entries.external[s]||m.predecessors[s].empty())return false;
    for(const auto p:m.predecessors[s]){
      const auto& branch=m.ops[m.instruction[p]];const int reg=branch.opcode&15;
      if(branch.kind!=IrKind::IndirectCondJump||branch.opcode<0x70||branch.opcode>0x73||
          branch.meta.raw||branch.meta.manual_interaction||m.predecessors[p].empty())return false;
      for(const auto q:m.predecessors[p]){
        if(m.ops[m.instruction[q]].kind!=IrKind::Store||m.ops[m.instruction[q]].opcode!=0x40+reg||m.predecessors[q].empty())return false;
        for(const auto r:m.predecessors[q])if(m.ops[m.instruction[r]].kind!=IrKind::Plain||
            m.ops[m.instruction[r]].opcode!=0x11||m.ops[m.instruction[r]].meta.raw)return false;
      }
      bool edge=false;for(const auto& e:m.flow.execution_edges[p])if(e.target_state==s){
        if(e.kind!=PostLayoutExecutionEdgeKind::IndirectTarget)return false;edge=true;
      }
      if(!edge)return false;
    }
    reached=true;
  }
  return reached;
}

struct NullableBodyProof {bool proved=false;std::set<int> reads;Id work=0;};
NullableBodyProof total_nullable_body(const Model& m,Id start,int selector,
    std::array<OrdinaryStackValue,4> values,const std::map<int,OrdinaryStackValue>& registers,
    Id base_depth,const ContextualCopyTransportOptions& limits){
  NullableBodyProof result;std::map<std::string,Id> labels;
  for(Id i=0;i<m.ops.size();++i)if(m.ops[i].kind==IrKind::Label){Id next=i+1U;while(next<m.ops.size()&&m.ops[next].kind==IrKind::Label)++next;labels.emplace(m.ops[i].name,next);}
  const auto target=[&](const IrTarget& destination)->std::optional<Id>{const auto* name=std::get_if<std::string>(&destination);return name&&labels.contains(*name)?std::optional<Id>{labels.at(*name)}:std::nullopt;};
  const auto next=[&](Id i){do{++i;}while(i<m.ops.size()&&m.ops[i].kind==IrKind::Label);return i;};
  const auto push=[&](OrdinaryStackValue value){for(Id i=3;i>0;--i)values[i]=values[i-1U];values[0]=std::move(value);};
  const auto drop=[&](OrdinaryStackValue value){values[0]=std::move(value);values[1]=values[2];values[2]=values[3];};
  std::vector<Id> returns;std::set<std::pair<Id,std::vector<Id>>> visited;
  std::string null_word="0";bool fresh=true;Id pc=start;values[0]={OrdinaryNumericRange{0,0,true},true};
  while(pc<m.ops.size()){
    if(++result.work>=limits.maximum_work || !visited.emplace(pc,returns).second)return result;
    const auto& op=m.ops[pc];const int code=op.opcode;
    if(op.meta.raw||op.meta.manual_interaction||op.procedure_boundary)return result;
    if(op.kind==IrKind::Store){if(pc!=start||code!=0x40+selector)return result;fresh=true;pc=next(pc);continue;}
    if(op.kind==IrKind::Recall){
      const int reg=register_index(op.register_name);if(reg==selector)return result;
      result.reads.insert(reg);const auto value=registers.find(reg);if(value==registers.end())return result;
      push(value->second);fresh=true;pc=next(pc);continue;
    }
    if(op.kind==IrKind::Call||op.kind==IrKind::Jump||op.kind==IrKind::IndirectCall||op.kind==IrKind::IndirectJump){
      const bool call=op.kind==IrKind::Call||op.kind==IrKind::IndirectCall;
      if(op.kind==IrKind::IndirectCall||op.kind==IrKind::IndirectJump){
        const int reg=code&15;if(reg<7||reg>=15)return result;
        if(reg==selector){
          const auto decoded=evaluate_indirect_address(op.register_name,null_word,IndirectOperationKind::Flow,m.control.address_space_model);
          if(!decoded||decoded->actual_flow_target!=std::optional<int>{0}||!decoded->result_value)return result;
          null_word=*decoded->result_value;
          if(call){if(base_depth+returns.size()+1U>static_cast<Id>(m.control.maximum_return_depth))return result;fresh=true;pc=next(pc);continue;}
          if(returns.empty()){result.proved=true;return result;}
          pc=returns.back();returns.pop_back();fresh=true;continue;
        }
        const PreloadReport* owner=nullptr;
        for(const auto& word:*limits.runtime_preloads)if(word.register_name==op.register_name){if(owner)return result;owner=&word;}
        if(!owner||owner->setup_expression||owner->setup_expression_text||owner->setup_target_name)return result;
        const auto decoded=evaluate_indirect_address(op.register_name,owner->value,IndirectOperationKind::Flow,m.control.address_space_model);
        if(!decoded||!decoded->actual_flow_target||!indirect_writeback_preserves_literal_word(*decoded,owner->value))return result;
        for(Id i=0;i<m.ops.size();++i){const auto& eff=m.effects[i];
          if(eff.must_defs.contains(op.register_name)||eff.may_defs.contains(op.register_name)){
            const auto kind=m.ops[i].kind;
            if((kind!=IrKind::IndirectCall&&kind!=IrKind::IndirectJump&&kind!=IrKind::IndirectCondJump&&kind!=IrKind::IndirectRecall&&kind!=IrKind::IndirectStore)||(m.ops[i].opcode&15)!=reg)return result;
          }
          if(eff.may_define_any_register){const auto aliases=m.flow.indirect_memory_targets.find(m.item[i]);
            if(m.ops[i].kind!=IrKind::IndirectStore||aliases==m.flow.indirect_memory_targets.end()||std::find(aliases->second.begin(),aliases->second.end(),reg)!=aliases->second.end())return result;}
        }
        if(!op.meta.indirect_flow_targets||op.meta.indirect_flow_targets->size()!=1U)return result;
        const auto dest=target(op.meta.indirect_flow_targets->front());if(!dest)return result;
        const auto* label=std::get_if<std::string>(&op.meta.indirect_flow_targets->front());
        if(!label||!limits.encoded_target_origins||!limits.encoded_target_origins->contains(*label)||limits.encoded_target_origins->at(*label)!=*decoded->actual_flow_target)return result;
        if(call){if(base_depth+returns.size()+1U>static_cast<Id>(m.control.maximum_return_depth))return result;returns.push_back(next(pc));}
        pc=*dest;fresh=true;continue;
      }
      const auto dest=target(op.target);if(!dest)return result;
      if(call){if(base_depth+returns.size()+1U>static_cast<Id>(m.control.maximum_return_depth))return result;returns.push_back(next(pc));}
      pc=*dest;fresh=true;continue;
    }
    if(op.kind==IrKind::Return){if(returns.empty()){result.proved=true;return result;}pc=returns.back();returns.pop_back();fresh=true;continue;}
    if(op.kind!=IrKind::Plain)return result;
    if(code>=0&&code<=9){
      if(!fresh)return result;std::string spelling;bool point=false;Id end=pc;
      while(end<m.ops.size()&&m.ops[end].kind==IrKind::Plain){const int digit=m.ops[end].opcode;
        if(digit>=0&&digit<=9)spelling.push_back(static_cast<char>('0'+digit));
        else if(digit==0x0a&&!point){point=true;spelling.push_back('.');}else break;
        end=next(end);
      }
      const auto range=bound_ordinary_decimal(spelling);if(!range)return result;push({*range,true});fresh=false;pc=end;continue;
    }
    if(code==0x14)std::swap(values[0],values[1]);
    else if(code==0x25){const auto x=values[0];values[0]=values[1];values[1]=values[2];values[2]=values[3];values[3]=x;}
    else if(code==0x0e)push(values[0]);
    else if(code==0x0d)values[0]={OrdinaryNumericRange{0,0,true},true};
    else if(code==0x0b){if(!values[0].ordinary)return result;if(values[0].range){const auto r=*values[0].range;values[0].range=OrdinaryNumericRange{-r.upper,-r.lower,r.integral};}}
    else if(code>=0x10&&code<=0x13){
      const auto x=values[0],y=values[1];if(!x.ordinary||!y.ordinary)return result;
      std::optional<OrdinaryNumericRange> range;if(x.range&&y.range)range=bound_ordinary_binary(code,*y.range,*x.range);
      bool total=range.has_value();if((code==0x10||code==0x11)&&x.range)total=true;
      if(code==0x13&&x.range&&(x.range->lower>=1||x.range->upper<=-1))total=true;
      if(!total)return result;drop({range,true});
    }else{
      const auto x=values[0];if(!x.ordinary)return result;std::optional<OrdinaryNumericRange> range;
      if(x.range)range=bound_ordinary_unary(code,*x.range);if(code==0x35&&!range)range=OrdinaryNumericRange{-1,1,false};
      bool total=code==0x31||code==0x32||code==0x34||code==0x35;
      if(code==0x15)total=x.range&&x.range->lower>=-99&&x.range->upper<=7&&range.has_value();
      if(code==0x22)total=range.has_value();if(!total)return result;values[0]={range,true};
    }
    fresh=selector_charge_entry_closer_opcode(code);pc=next(pc);
  }
  return result;
}
} // namespace

std::vector<ContextualCopyTransportCandidate> contextual_lazy_unit_projection_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const ContextualCopyTransportOptions& limits) {
  std::vector<ContextualCopyTransportCandidate> result;
  if (!limits.runtime_preloads || !limits.control || !limits.encoded_target_origins ||
      !limits.maximum_work || !limits.maximum_candidates || limits.maximum_growth_cells < 0)
    return result;
  if (std::none_of(ops.begin(), ops.end(), [](const auto& op) {
        return op.kind == IrKind::Plain && !op.meta.raw && op.opcode == 0x32;
      })) return result;
  Model model(ops, context, limits);
  if (std::getenv("MKPRO_NATIVE_TRACE_COPY_TRANSPORT"))
    std::cerr << "[lazy-unit-projection] model=" << model.valid << '\n';
  if (!model.valid) return result;
  const auto projection = [] {
    IrOp op;
    op.kind = IrKind::Plain;
    op.opcode = 0x32;
    op.meta.mnemonic = opcode_by_code(0x32).name;
    return op;
  };
  for (Id at = 0; at < ops.size() && result.size() < limits.maximum_candidates; ++at) {
    if (ops[at].opcode != 0x32) continue;
    const auto plan = lazy_sign_plan(model, at, limits);
    if (!plan) continue;
    ContextualCopyTransportCandidate candidate;
    auto& out = candidate.transformation;
    for (Id i = 0; i < ops.size(); ++i) {
      if (i == plan->projection) continue;
      if (plan->calls.contains(i)) out.ops.push_back(projection());
      out.ops.push_back(ops[i]);
      if (plan->reads.contains(i)) out.ops.push_back(projection());
    }
    out.applied = 1;
    out.optimizations.push_back({"contextual-lazy-unit-projection",
        "Delayed a sign-only storage projection without moving any return or callback entry. "
        "Reused the exact packed-sign, reaching-definition, alias, caller normalization and "
        "stack/X1/X2 proofs of the compound nullable law. Every affected recall decodes "
        "the original unit; all existing selector and placement equations remain pending."});
    result.push_back(std::move(candidate));
  }
  return result;
}

std::vector<ContextualCopyTransportCandidate> contextual_nullable_callback_projection_candidates(
    const std::vector<IrOp>& ops,const PassContext& context,const ContextualCopyTransportOptions& limits){
  std::vector<ContextualCopyTransportCandidate> result;
  if(!limits.runtime_preloads||!limits.control||!limits.encoded_target_origins||!limits.maximum_candidates)return result;
  const bool zero_edge=std::any_of(ops.begin(),ops.end(),[](const auto& op) {
    return op.kind==IrKind::IndirectCondJump && op.opcode>=0x70 && op.opcode<=0x73;
  });
  const bool callback=std::any_of(ops.begin(),ops.end(),[](const auto& op) {
    return op.kind==IrKind::Store && op.opcode>=0x47 && op.opcode<=0x4e &&
        std::any_of(op.meta.roles.begin(),op.meta.roles.end(),[](const auto& role) {
          return role.starts_with("selector-sunk-literal-id:");
        });
  });
  if(!zero_edge || !callback) return result;
  Model m(ops,context,limits);if(!m.valid)return result;
  std::vector<std::optional<LazySignPlan>> signs;
  for(Id i=0;i<ops.size();++i)if(ops[i].opcode==0x32)if(auto plan=lazy_sign_plan(m,i,limits)){
    signs.push_back(std::move(*plan));if(signs.size()>=2U)break;
  }
  // Null completion has its own complete totality/death/control proof. Do
  // not require an unrelated lazy sign rewrite just to admit that proof.
  // Historical projection alternatives keep their original order and budget.
  signs.push_back(std::nullopt);
  Id null_return=ops.size();int pc=0;for(Id i=0;i<ops.size();++i){
    if(pc==0 && ops[i].kind==IrKind::Return && ops[i].opcode==0x52)null_return=i;
    pc+=cells_per_op(ops[i]);
  }
  if(null_return==ops.size())return result;
  const auto lift=analyze_post_layout_automatic_entry_lift_with_indirect_store_facts(m.items,m.flow,limits.maximum_work);if(!lift)return result;
  std::vector<Id> sources;std::map<Id,std::vector<int>> query;
  for(Id i=0;i<ops.size();++i)if(i!=null_return&&ops[i].kind==IrKind::Return&&ops[i].opcode==0x52&&zero_return_entry(m,i)){
    sources.push_back(i);query[m.item[i]]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14};
  }
  if(sources.empty())return result;
  const auto policy=context.options.ordinary_decimal_keyboard_input?NumericRangeInputPolicy::ValidatedOrdinaryDecimalInput:NumericRangeInputPolicy::ValidatedRegisterTransport;
  const auto registers=prove_post_layout_register_numeric_values(m.items,m.flow,*lift,query,limits.runtime_preloads,limits.maximum_work,policy);
  std::vector<Id> source_items;for(const auto source:sources)source_items.push_back(m.item[source]);
  const auto stacks=prove_post_layout_stack_numeric_values(m.items,m.flow,*lift,source_items,limits.runtime_preloads,limits.maximum_work,policy);
  if(registers.exhausted||stacks.exhausted)return result;
  std::set<std::string> names;for(const auto& op:ops)if(op.kind==IrKind::Label)names.insert(op.name);
  std::string null_label="__nullable_callback_plain_return";while(names.contains(null_label))null_label+='_';
  const auto unary=[](){IrOp op;op.kind=IrKind::Plain;op.opcode=0x32;op.meta.mnemonic=opcode_by_code(0x32).name;return op;};
  for(const auto source:sources)for(Id store=0;store<ops.size();++store){
    if(ops[store].kind!=IrKind::Store||ops[store].opcode<0x47||ops[store].opcode>0x4e||
        !std::any_of(ops[store].meta.roles.begin(),ops[store].meta.roles.end(),[](const auto& role){return role.starts_with("selector-sunk-literal-id:");}) || ops[store].meta.raw)continue;
    const int selector=ops[store].opcode&15;const auto selector_name=ops[store].register_name;
    LivenessOptions life;life.discover_toggle_projection_signs=true;
    for(const auto& word:*limits.runtime_preloads)if(!word.setup_expression&&!word.setup_expression_text&&!word.setup_target_name)
      life.equal_entry_value_classes.emplace(word.register_name,"literal:"+word.value);
    const GuardedOldWordQuery death{source,selector_name};const auto dead=analyze_dead_old_words(ops,m.flow,{death},life,limits.maximum_work);
    if(dead.exhausted||!dead.dead_words.contains(death))continue;
    std::vector<Id> after;std::set<int> body_reads;bool total=true,reached=false;
    for(Id s=0;s<m.instruction.size();++s)if(m.instruction[s]==source){
      if(!registers.values.contains(s)||!stacks.stack_before_state.contains(s)){total=false;break;}
      auto proof=total_nullable_body(m,store,selector,stacks.stack_before_state.at(s),registers.values.at(s),
          m.flow.execution_states[s].return_stack.size(),limits);
      if(!proof.proved){total=false;break;}body_reads.insert(proof.reads.begin(),proof.reads.end());
      after.insert(after.end(),m.flow.execution_successors[s].begin(),m.flow.execution_successors[s].end());reached=true;
    }
    if(!reached||!total||after.empty()||!prove_post_layout_stack_state_equality(m.items,m.flow,after,
        {{false,false,false,false},false,false},{.refine_runtime_selectors=false,.numeric_entry_closed=true,
         .transport_closed_entry_through_control=true,.use_indirect_store_entry_facts=true}))continue;
    Id first_label=source;while(first_label>0&&ops[first_label-1U].kind==IrKind::Label)--first_label;
    if(first_label==source)continue;
    for(const auto& sign:signs){
      if(sign && (sign->reg==selector||body_reads.contains(sign->reg)))continue;
      ContextualCopyTransportCandidate candidate;auto& out=candidate.transformation;
      for(Id i=0;i<ops.size();++i){
        if(i==source||(sign && i==sign->projection)||(i>=first_label&&i<source))continue;
        if(i==null_return){IrOp label;label.kind=IrKind::Label;label.name=null_label;out.ops.push_back(std::move(label));}
        if(i==store)for(Id j=first_label;j<source;++j)out.ops.push_back(ops[j]);
        if(sign && sign->calls.contains(i))out.ops.push_back(unary());
        auto op=ops[i];
        if((op.kind==IrKind::IndirectCall||op.kind==IrKind::IndirectJump)&&
            (op.opcode&15)==selector&&op.meta.indirect_flow_targets)
          op.meta.indirect_flow_targets->push_back(null_label);
        out.ops.push_back(std::move(op));if(sign && sign->reads.contains(i))out.ops.push_back(unary());
      }
      candidate.target_origins.emplace(null_label,0);
      out.applied=1;
      out.optimizations.push_back({sign ? "contextual-nullable-callback-projection-composition" :
          "contextual-nullable-readonly-return-completion",
          std::string(sign ? "Composed lazy unit projection with a " : "Independently proved a ") +
          "zero-selector readonly return completion. Proved every extra decoder/hoisted-call X1/X2 effect, "
          "canonical-zero entry, exact readonly arithmetic totality, balanced bounded calls and private-selector death. "
          "Address/data equations, the actual null return at 00 and every noncanonical helper frame remain mandatory final-layout obligations."});
      result.push_back(std::move(candidate));if(result.size()>=limits.maximum_candidates)return result;
    }
  }
  return result;
}

} // namespace mkpro::core::passes
