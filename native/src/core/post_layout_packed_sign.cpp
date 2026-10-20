#include "mkpro/core/post_layout_packed_sign.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/post_layout_progress_bounds.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/core/stable_register_value_flow.hpp"

#include <algorithm>
#include <deque>
#include <exception>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <set>
#include <string>

namespace mkpro::core {

PackedSignInputProof prove_post_layout_packed_sign_inputs(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<bool>& automatic_entry_lift,
    const std::vector<std::size_t>& requested_states,
    const std::vector<PreloadReport>* preloads, std::size_t maximum_work) {
  PackedSignInputProof result;
  constexpr std::string_view registers = "0123456789abcde";
  const auto count = flow.execution_states.size();
  if (requested_states.empty()) return result;
  if (!flow.proved || automatic_entry_lift.size() != count ||
      flow.execution_successors.size() != count) return result;

  std::map<std::string, const PreloadReport*> words;
  if (preloads) {
    for (const auto& preload : *preloads) {
      try {
        const auto name = register_from_text(preload.register_name);
        if (name.size() != 1U ||
            registers.find(name[0]) == std::string_view::npos ||
            !words.emplace(name, &preload).second)
          return {};
      } catch (const std::exception&) {
        return {};
      }
    }
  }
  const auto charge = [&] {
    if (result.work >= maximum_work) {
      result.exhausted = true;
      result.signs.clear();
      return false;
    }
    ++result.work;
    return true;
  };
  std::vector<std::vector<std::size_t>> predecessors(count);
  for (std::size_t state = 0; state < count; ++state) {
    if (!charge()) return result;
    if (flow.execution_states[state].item_index >= items.size()) return {};
    for (const auto next : flow.execution_successors[state]) {
      if (!charge()) return result;
      if (next >= count) return {};
      predecessors[next].push_back(state);
    }
  }
  const auto entries = resolve_post_layout_external_entry_contexts(flow);
  if (!entries) return result;
  const auto transparent = [&](std::size_t state) {
    const auto& item = items[flow.execution_states[state].item_index];
    if (item.kind != MachineItemKind::Op || item.raw || item.manual_interaction)
      return false;
    const int family = item.opcode & 0xf0;
    return (item.opcode >= 0x40 && item.opcode <= 0x4e) || family == 0xb0 ||
        item.opcode == 0x51 || item.opcode == 0x52 || item.opcode == 0x53 ||
        item.opcode == 0x54 || family == 0x80 || family == 0xa0;
  };
  // Backward must proof with grounded producer leaves. A cycle cannot prove
  // itself; every predecessor dependency must independently become ready.
  const auto originate = [&](const std::vector<std::size_t>& roots,
                             const auto& producer) {
    if (roots.empty()) return false;
    std::deque<std::size_t> pending(roots.begin(), roots.end()), ready;
    std::set<std::size_t> seen, proved;
    std::map<std::size_t, std::size_t> unresolved;
    std::map<std::size_t, std::vector<std::size_t>> users;
    while (!pending.empty()) {
      const auto state = pending.front();
      pending.pop_front();
      if (!seen.insert(state).second) continue;
      if (!charge() || entries->external[state])
        return false;
      if (producer(state)) {
        proved.insert(state);
        ready.push_back(state);
        continue;
      }
      if (!transparent(state) || predecessors[state].empty()) return false;
      unresolved[state] = predecessors[state].size();
      for (const auto previous : predecessors[state]) {
        if (!charge()) return false;
        users[previous].push_back(state);
        pending.push_back(previous);
      }
    }
    while (!ready.empty()) {
      const auto state = ready.front();
      ready.pop_front();
      for (const auto next : users[state]) {
        if (!charge()) return false;
        if (--unresolved[next] == 0U) {
          proved.insert(next);
          ready.push_back(next);
        }
      }
    }
    return std::all_of(roots.begin(), roots.end(),
                       [&](auto root) { return proved.contains(root); });
  };

  std::set<std::size_t> remaining(requested_states.begin(), requested_states.end());
  for (auto it = remaining.begin(); it != remaining.end();) {
    const auto state = *it;
    if (state >= count) return {};
    if (entries->external[state]) {
      it = remaining.erase(it);
      continue;
    }
    const bool positive = originate(predecessors[state], [&](auto previous) {
      const auto& item = items[flow.execution_states[previous].item_index];
      return !item.raw && item.opcode >= 0x37 && item.opcode <= 0x3a;
    });
    if (result.exhausted) return result;
    if (positive) {
      result.signs.emplace(state, 1);
      it = remaining.erase(it);
    } else {
      ++it;
    }
  }
  if (remaining.empty() || !preloads) return result;
  const auto progress = prove_post_layout_monotone_progress(
      items, flow, automatic_entry_lift, *preloads, maximum_work - result.work);
  if (progress.work > maximum_work - result.work) {
    result.work = maximum_work;
    result.exhausted = true;
    result.signs.clear();
    return result;
  }
  result.work += progress.work;
  if (progress.exhausted) {
    result.exhausted = true;
    result.signs.clear();
    return result;
  }
  if (!progress.witness) return result;
  const auto& witness = *progress.witness;
  if (witness.source_state >= count || witness.target_state >= count) return result;
  if (static_cast<std::size_t>(witness.register_index) >= registers.size()) return result;
  const auto word = words.find(std::string(1, registers[static_cast<std::size_t>(witness.register_index)]));
  if (word == words.end() || word->second->value != "0" ||
      predecessors[witness.source_state].size() != 1U ||
      predecessors[witness.target_state].size() != 1U ||
      predecessors[witness.target_state].front() != witness.source_state)
    return result;
  // A direct store or another X-preserving transport between subtraction
  // and its guard does not change operand orientation. Ground every path
  // at an actual old-minus-updated subtraction; a reversed/unknown producer
  // or a cycle is not a negative-sign certificate.
  const bool oriented = originate(predecessors[witness.source_state], [&](auto subtraction) {
    const auto& operation = items[flow.execution_states[subtraction].item_index];
    return !operation.raw && operation.opcode == 0x11 &&
        originate(predecessors[subtraction], [&](auto previous) {
          const auto& item = items[flow.execution_states[previous].item_index];
          return !item.raw && item.opcode == 0x40 + witness.register_index;
        });
  });
  if (!oriented) {
    if (result.exhausted) result.signs.clear();
    return result;
  }
  for (const auto state : remaining) {
    // This is an edge certificate, not a fact on every incoming path to an
    // arbitrary join. The singleton predecessor check above is mandatory.
    const bool negative = state == witness.target_state ||
        originate(predecessors[state], [&](auto previous) {
          return previous == witness.target_state && transparent(previous);
        });
    if (result.exhausted) {
      result.signs.clear();
      return result;
    }
    if (negative) result.signs.emplace(state, -1);
  }
  return result;
}

ToggleSignInputProof prove_post_layout_toggle_sign_inputs(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<std::size_t>& requested_states,
    const std::vector<PreloadReport>& preloads, std::size_t maximum_work) {
  ToggleSignInputProof result;
  const auto unproved=[&]{result.signs.clear();result.carriers.clear();result.values.clear();result.value_carriers.clear();return result;};
  const bool trace=std::getenv("MKPRO_NATIVE_TRACE_REGISTER_WEBS")!=nullptr;
  const auto count=flow.execution_states.size();
  if(requested_states.empty() || !flow.proved || !count ||
      flow.execution_successors.size()!=count) {if(trace){std::cerr<<"[toggle-sign] initial CFG="<<flow.proved<<" states="<<count<<" demands="<<requested_states.size()<<"\n";for(const auto& reason:flow.reasons)std::cerr<<reason<<"\n";}return result;}
  const auto charge=[&] {
    if(result.work>=maximum_work){result.exhausted=true;result.signs.clear();result.carriers.clear();result.values.clear();result.value_carriers.clear();return false;}
    ++result.work;return true;
  };
  const auto entries=resolve_post_layout_external_entry_contexts(flow);
  const auto inputs=prove_post_layout_input_boundaries(items,flow);
  if(!entries || !inputs.proved) {if(trace)std::cerr<<"[toggle-sign] entry/boundary rejected\n";return result;}
  std::set<int> protocols;
  for(const auto& item:items) {
    // A formal counter on an Address is an already resolved call/jump target,
    // not a raw instruction. The authoritative graph and input-boundary proof
    // retain its exact route and every formal return frame. Executable aliases
    // and opcode overrides still need their own data semantics and stay opaque.
    if(item.raw || (item.formal_opcode && item.kind!=MachineItemKind::Address) ||
        item.logical_register_name) {if(trace)std::cerr<<"[toggle-sign] opaque metadata\n";return unproved();}
    if(item.manual_interaction && item.manual_interaction->kind==ManualInteractionAnchorKind::PromptStop &&
        std::find(item.roles.begin(),item.roles.end(),kTypedDisplayObservationRole)!=item.roles.end())
      protocols.insert(item.manual_interaction->protocol_id);
  }
  std::vector<std::vector<std::size_t>> predecessors(count);
  for(std::size_t s=0;s<count;++s){
    if(!charge())return result;
    if(flow.execution_states[s].item_index>=items.size())return unproved();
    for(const auto next:flow.execution_successors[s]){
      if(!charge())return result;
      if(next>=count)return unproved();
      predecessors[next].push_back(s);
    }
  }
  bool main=false;
  for(std::size_t s=0;s<count;++s) {
    const auto& item=items[flow.execution_states[s].item_index];
    main=main || entries->main[s];
    if(entries->external[s] && !entries->main[s]) {
      if(!item.manual_interaction || !protocols.contains(item.manual_interaction->protocol_id) ||
          (item.manual_interaction->kind!=ManualInteractionAnchorKind::PromptStop &&
           !inputs.input_boundary_states[s]) || predecessors[s].empty()){if(trace)std::cerr<<"[toggle-sign] external "<<s<<" pc="<<flow.execution_states[s].address<<"\n";return unproved();}
    }
    if(item.manual_interaction && !protocols.contains(item.manual_interaction->protocol_id))return unproved();
  }
  if(!main){if(trace)std::cerr<<"[toggle-sign] no Main\n";return result;}
  std::vector<std::optional<std::size_t>> previous(items.size());
  std::optional<std::size_t> last;
  for(std::size_t i=0;i<items.size();++i)if(items[i].kind!=MachineItemKind::Label){previous[i]=last;last=i;}
  std::set<std::string> owners;
  for(const auto& word:preloads)
    if(word.register_name.size()!=1U || std::string_view{"0123456789abcde"}.find(word.register_name[0])==std::string_view::npos ||
        !owners.insert(word.register_name).second)return unproved();
  for(const auto& word:preloads) {
    if(!charge())return result;
    if(trace)std::cerr<<"[toggle-sign] word "<<word.register_name<<"="<<word.value<<"\n";
    if(word.setup_expression || word.setup_target_name || word.setup_expression_text ||
        word.counts_against_program || word.retunable_natural_fractional_prefix ||
        (word.lowered_data_value && *word.lowered_data_value!=word.value))continue;
    const auto value=bound_ordinary_decimal(word.value);
    // The range API encloses fractions in integer endpoints: (0, 1) does
    // not mean that a validated literal such as 0.25 might be zero.
    const auto mantissa=std::string_view(word.value).substr(0,word.value.find_first_of("eE"));
    if(!value || !std::any_of(mantissa.begin(),mantissa.end(),[](const char digit){return digit>='1' && digit<='9';}))continue;
    const int sign=word.value.starts_with('-')?-1:1;
    const int reg=static_cast<int>(std::string_view{"0123456789abcde"}.find(word.register_name[0]));
    std::map<std::size_t,std::pair<std::size_t,std::size_t>> updates;
    bool valid=true;
    for(std::size_t i=0;i<items.size() && valid;++i) {
      if(!charge())return result;
      const auto& item=items[i];if(item.kind!=MachineItemKind::Op)continue;
      const int op=item.opcode,family=op&0xf0;
      if(op==0x4f || op==0x6f || (op>=0x70 && (op&15)==15)){valid=false;break;}
      if(op==0x40+reg) {
        const auto neg=previous[i];
        const auto read=neg?previous[*neg]:std::nullopt;
        if(!neg || !read || items[*neg].kind!=MachineItemKind::Op || items[*neg].opcode!=0x0b ||
            items[*read].kind!=MachineItemKind::Op || items[*read].opcode!=0x60+reg ||
            item.manual_interaction || items[*neg].manual_interaction || items[*read].manual_interaction){valid=false;break;}
        updates.emplace(i,std::pair{*read,*neg});
      }
      if(family==0xb0) {
        const auto aliases=flow.indirect_memory_targets.find(i);
        if(aliases==flow.indirect_memory_targets.end() ||
            std::find(aliases->second.begin(),aliases->second.end(),reg)!=aliases->second.end())valid=false;
      }
      if(family>=0x70 && family<=0xe0 && (op&15)==reg && reg<=6)valid=false;
      if(reg<4 && op==(reg==0?0x5d:reg==1?0x5b:reg==2?0x58:0x5a))valid=false;
    }
    if(trace)std::cerr<<"[toggle-sign] R"<<reg<<" updates="<<updates.size()<<" valid="<<valid<<"\n";
    if(!valid || updates.empty())continue;
    for(std::size_t s=0;s<count && valid;++s) {
      const auto item=flow.execution_states[s].item_index;
      for(const auto& [store,pair]:updates) {
        const auto expected=item==store?std::optional{pair.second}:
            item==pair.second?std::optional{pair.first}:std::nullopt;
        if(!expected)continue;
        if(predecessors[s].empty()){valid=false;break;}
        for(const auto from:predecessors[s])
          if(flow.execution_states[from].item_index!=*expected ||
              flow.execution_states[from].return_stack!=flow.execution_states[s].return_stack ||
              flow.execution_states[from].formal_return_stack!=flow.execution_states[s].formal_return_stack)
            valid=false;
      }
    }
    if(!valid){if(trace)std::cerr<<"[toggle-sign] transition side entry R"<<reg<<"\n";continue;}
    std::vector<unsigned> rows(count,0);
    std::vector<bool> queued(count,false);
    std::deque<std::size_t> pending;
    for(std::size_t s=0;s<count;++s)if(entries->main[s]){rows[s]=1;queued[s]=true;pending.push_back(s);}
    while(!pending.empty()) {
      if(!charge())return result;
      const auto s=pending.front();pending.pop_front();queued[s]=false;
      const unsigned in=rows[s];
      const unsigned out=updates.contains(flow.execution_states[s].item_index)
          ? ((in&1U)?2U:0U)|((in&2U)?1U:0U):in;
      for(const auto next:flow.execution_successors[s]) {
        if(!charge())return result;
        const unsigned joined=rows[next]|out;
        if(joined==rows[next])continue;
        rows[next]=joined;if(!queued[next]){queued[next]=true;pending.push_back(next);}
      }
    }
    for(const auto s:requested_states) {
      if(!charge())return result;
      if(s>=count)return unproved();
      const auto& item=items[flow.execution_states[s].item_index];
      if(trace)std::cerr<<"[toggle-sign] R"<<reg<<" query="<<s<<" rows="<<rows[s]<<" predecessors="<<predecessors[s].size()<<"\n";
      if(item.kind!=MachineItemKind::Op || item.manual_interaction ||
          (rows[s]!=1U && rows[s]!=2U) || predecessors[s].empty())continue;
      // This component is a value certificate, not a sign substituted for an
      // arbitrary number. Each producer reads the exact closed carrier; the
      // source transition and all of its side entries were verified above.
      if(value->integral && value->lower==value->upper &&
          value->lower>=-1024 && value->upper<=1024){
        const int before=static_cast<int>(rows[s]==1U?value->lower:-value->lower);
        std::optional<int> after;
        if(item.opcode==0x60+reg)after=before;
        else if(item.opcode==0x40+reg && updates.contains(flow.execution_states[s].item_index))after=-before;
        else if(item.opcode==0x0b && std::any_of(updates.begin(),updates.end(),[&](const auto& update){
            return update.second.second==flow.execution_states[s].item_index;
          }))after=-before;
        if(after){result.values.emplace(s,*after);result.value_carriers.emplace(s,word.register_name);}
      }
      if(item.opcode!=0x32)continue;
      if(std::all_of(predecessors[s].begin(),predecessors[s].end(),[&](const auto from){
          const auto& read=items[flow.execution_states[from].item_index];
          // A direct store preserves X exactly. For a certified transition,
          // the just-written carrier therefore has the same sign as X here;
          // rows[s] already includes that transition, not its old value.
          const bool same_word=read.opcode==0x60+reg ||
              (read.opcode==0x40+reg && updates.contains(flow.execution_states[from].item_index));
          return read.kind==MachineItemKind::Op && same_word && !read.manual_interaction &&
              flow.execution_states[from].return_stack==flow.execution_states[s].return_stack &&
              flow.execution_states[from].formal_return_stack==flow.execution_states[s].formal_return_stack;
        })){result.signs.emplace(s,rows[s]==1U?sign:-sign);result.carriers.emplace(s,word.register_name);}
    }
  }
  return result;
}

} // namespace mkpro::core
