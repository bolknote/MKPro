#pragma once

#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/passes/register_coalesce.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/detail/cell_provenance.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core::passes {

// A ROM-specific rematerialization fact, not an algebraic identity for an
// arbitrary fractional selector: storing canonical zero in R3 and executing
// D3 writes -99999999 back to R3 and recalls that same register. The pair has
// the same visible stack, X1, X2 and entry-state effect as recall-constant,
// store-R3. A different destination may borrow a proved-dead R3 at the cost
// of one extra store. Keep either candidate separate until final layout has
// compared the complete code, selectors and preloads with the incumbent.
inline PassResult rematerialize_zero_underflow_constant(
    const std::vector<IrOp>& ops,
    const std::map<std::string, std::string>& immutable_preloads,
    AddressSpaceModel model = AddressSpaceModel::Standard) {
  PassResult result{.ops = ops};
  if (ops.size() < 3 || immutable_preloads.empty())
    return result;
  for (const auto& op : ops)
    if (op.meta.raw || op.meta.logical_register_analysis)
      return result;

  std::vector<std::size_t> candidates;
  for (std::size_t i = 0; i + 1 < ops.size(); ++i) {
    const auto& recall = ops[i];
    const auto& store = ops[i + 1];
    if (recall.kind != IrKind::Recall || store.kind != IrKind::Store ||
        store.opcode < 0x40 || store.opcode > 0x4e ||
        store.register_name != std::string(1, "0123456789abcde"[store.opcode - 0x40]) ||
        (store.register_name != "3" && immutable_preloads.contains("3")) ||
        recall.opcode < 0x67 || recall.opcode > 0x6e ||
        recall.register_name != std::string(1, "0123456789abcde"[recall.opcode - 0x60]) ||
        has_rewrite_barrier(recall) || has_rewrite_barrier(store) ||
        // Typed display/manual roles are barriers; words in source comments
        // are not. The admitted replacement proves the complete X/X2 effect.
        !recall.meta.roles.empty() || !store.meta.roles.empty() ||
        !recall.meta.semantic_call_origins.empty() ||
        !store.meta.semantic_call_origins.empty())
      continue;
    const auto value = immutable_preloads.find(recall.register_name);
    if (value == immutable_preloads.end() || value->second != "-99999999")
      continue;
    bool immutable = true;
    for (const auto& op : ops) {
      const auto effects = register_effects(op);
      if (effects.may_define_any_register ||
          effects.must_defs.contains(recall.register_name) ||
          effects.may_defs.contains(recall.register_name)) {
        immutable = false;
        break;
      }
    }
    if (immutable)
      candidates.push_back(i);
  }
  if (candidates.empty())
    return result;

  PostLayoutControlFlowOptions options;
  options.address_space_model = model;
  const auto flow = build_post_layout_control_flow(lower_ir_to_machine(ops), options);
  if (!flow.proved || flow.execution_states.empty())
    return result;
  std::map<int, std::size_t> instruction_at;
  int address = 0;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    if (ops[i].kind != IrKind::Label)
      instruction_at.emplace(address, i);
    address += cells_per_op(ops[i]);
  }
  std::vector<std::size_t> sites;
  for (const auto& state : flow.execution_states) {
    const auto site = instruction_at.find(state.address);
    if (site == instruction_at.end())
      return result;
    sites.push_back(site->second);
  }
  struct Incoming {
    std::size_t state;
    PostLayoutExecutionEdgeKind kind;
  };
  std::vector<std::vector<Incoming>> predecessors(sites.size());
  for (std::size_t state = 0; state < sites.size(); ++state)
    for (const auto& edge : flow.execution_edges[state])
      predecessors[edge.target_state].push_back({state, edge.kind});

  const auto external = [&](std::size_t state) {
    const int state_address = flow.execution_states[state].address;
    return std::any_of(flow.external_entries.begin(), flow.external_entries.end(),
        [&](const auto& entry) { return entry.entry.address == state_address; });
  };

  // Must information about the delivered X representation, not only its
  // mathematical value. Unknown dominates joins. Normalized admits zero only
  // after the appropriate conditional edge; an arbitrary input zero does not.
  enum class XFact { Unknown, Normalized, Zero };
  std::vector<std::optional<XFact>> before(sites.size());
  std::vector<bool> queued(sites.size(), false);
  std::deque<std::size_t> pending;
  const auto merge = [&](std::size_t state, XFact value) {
    const auto old = before[state];
    XFact combined = value;
    if (old.has_value() && *old != value)
      combined = *old == XFact::Unknown || value == XFact::Unknown
          ? XFact::Unknown : XFact::Normalized;
    if (old.has_value() && *old == combined)
      return;
    before[state] = combined;
    if (!queued[state]) {
      queued[state] = true;
      pending.push_back(state);
    }
  };
  for (std::size_t state = 0; state < sites.size(); ++state)
    if (state == 0 || external(state))
      merge(state, XFact::Unknown);
  while (!pending.empty()) {
    const auto state = pending.front();
    pending.pop_front();
    queued[state] = false;
    const auto& op = ops[sites[state]];
    XFact out = *before[state];
    if (has_rewrite_barrier(op)) {
      out = XFact::Unknown;
    } else if (op.kind == IrKind::Plain && op.opcode == 0x0d) {
      out = XFact::Zero;
    } else if (op.kind == IrKind::Plain && op.opcode == 0x11) {
      out = XFact::Normalized;
    } else if (op.kind != IrKind::Store && op.kind != IrKind::Jump &&
               op.kind != IrKind::CondJump && op.kind != IrKind::Call &&
               op.kind != IrKind::Return &&
               !(op.kind == IrKind::Plain && op.opcode == 0x54)) {
      out = XFact::Unknown;
    }
    for (const auto& edge : flow.execution_edges[state]) {
      XFact transported = out;
      const bool zero_edge = op.kind == IrKind::CondJump &&
          ((op.opcode == 0x5e && edge.kind == PostLayoutExecutionEdgeKind::Fallthrough) ||
           (op.opcode == 0x57 && edge.kind == PostLayoutExecutionEdgeKind::DirectTarget));
      if (zero_edge && out != XFact::Unknown)
        transported = XFact::Zero;
      merge(edge.target_state, transported);
    }
  }
  const auto canonical_zero = [&](std::size_t state) {
    return before[state] == XFact::Zero && !external(state);
  };

  // The private scratch value may change only when no execution can read it
  // before a definite overwrite. Exact call/return contexts include loops and
  // resumable prompts; unknown memory effects fail closed. A cycle with no
  // read or observation of R3 is harmless, not an excuse to guess a loop count.
  const auto r3_unobserved_until_overwrite = [&](std::size_t start) {
    std::vector<bool> visited(sites.size(), false);
    std::vector<std::size_t> work{start};
    while (!work.empty()) {
      const auto state = work.back();
      work.pop_back();
      if (visited[state])
        continue;
      visited[state] = true;
      const auto effects = register_effects(ops[sites[state]]);
      if (effects.uses_all_registers || effects.uses.contains("3"))
        return false;
      if (effects.must_defs.contains("3"))
        continue;
      for (const auto next : flow.execution_successors[state])
        work.push_back(next);
    }
    return true;
  };

  // The optional extra cell is inserted before physical binding. Refuse
  // numeric/formal flow ownership instead of shifting an encoded address by
  // guesswork. Late selector values are still independently rebound and
  // checked by the compiler's final-artifact pipeline.
  const bool growth_relocatable = std::all_of(ops.begin(), ops.end(), [](const IrOp& op) {
    if (op.kind == IrKind::OrphanAddress || op.target_meta.formal_opcode.has_value() ||
        op.meta.indirect_flow_formal_targets.has_value())
      return false;
    if ((op.kind == IrKind::Jump || op.kind == IrKind::CondJump ||
         op.kind == IrKind::Call || op.kind == IrKind::Loop) &&
        !std::holds_alternative<std::string>(op.target))
      return false;
    if (op.meta.indirect_flow_targets.has_value())
      for (const auto& target : *op.meta.indirect_flow_targets)
        if (!std::holds_alternative<std::string>(target))
          return false;
    return true;
  });
  std::map<std::size_t, std::vector<IrOp>> replacements;

  for (const auto candidate : candidates) {
    bool reached = false;
    const bool scratch = ops[candidate + 1].register_name != "3";
    bool proved = !scratch || growth_relocatable;
    for (std::size_t state = 0; state < sites.size(); ++state) {
      if (sites[state] == candidate) {
        reached = true;
        proved = proved && canonical_zero(state) &&
            (!scratch || r3_unobserved_until_overwrite(state));
      } else if (sites[state] == candidate + 1) {
        if (external(state) || predecessors[state].empty()) {
          proved = false;
          break;
        }
        for (const auto& incoming : predecessors[state])
          proved = proved && sites[incoming.state] == candidate &&
              incoming.kind == PostLayoutExecutionEdgeKind::Fallthrough;
      }
    }
    if (!reached || !proved)
      continue;
    IrOp seed = make_store("3");
    seed.meta.source_line = ops[candidate + 1].meta.source_line;
    std::vector<IrOp> replacement{std::move(seed)};
    IrOp recall;
    recall.kind = IrKind::IndirectRecall;
    recall.register_name = "3";
    recall.opcode = 0xd3;
    recall.meta.mnemonic = opcode_by_code(0xd3).name;
    recall.meta.source_line = ops[candidate + 1].meta.source_line;
    recall.meta.indirect_memory_targets = std::vector<int>{3};
    replacement.push_back(std::move(recall));
    if (scratch)
      replacement.push_back(ops[candidate + 1]);
    replacements.emplace(candidate, std::move(replacement));
    ++result.applied;
  }
  if (result.applied == 0)
    return result;
  result.ops.clear();
  for (std::size_t site = 0; site < ops.size(); ++site) {
    const auto replacement = replacements.find(site);
    if (replacement == replacements.end()) {
      result.ops.push_back(ops[site]);
    } else {
      result.ops.insert(result.ops.end(), replacement->second.begin(), replacement->second.end());
      ++site;
    }
  }
  if (result.applied > 0)
    result.optimizations.push_back({"zero-underflow-constant-rematerialization",
        "Rematerialized " + std::to_string(result.applied) +
        " immutable constant read(s) through canonical zero and self-indexed R3; "
        "proved all call/return contexts, scratch liveness and exclusive pair entry. "
        "Released data uses may be rebound only by the independent final-layout proof."});
  return result;
}

// Conditional counterpart for a long literal exposed after allocation. The
// canonical zero comes from a normalized subtraction and its equality edge,
// not from an arbitrary typed zero. Geometry and all selector equations remain
// the caller's obligations; hidden entry-state equality is proved separately.
inline PassResult deferred_zero_underflow_literal(
    const std::vector<IrOp>& ops, const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<PreloadReport>& preloads, std::size_t maximum_work = 400000U) {
  PassResult result{.ops=ops};
  if (!flow.proved || flow.execution_states.size()!=flow.execution_successors.size() ||
      flow.execution_states.size()!=flow.execution_edges.size()) return result;
  const auto items=lower_ir_to_machine(ops);
  const auto plain=[](const IrOp& op) {
    return !op.meta.raw && !op.meta.manual_interaction && !op.meta.logical_register_analysis &&
        !op.target_meta.formal_opcode && !op.procedure_boundary &&
        !op.meta.tactic && op.meta.semantic_call_origins.empty() &&
        std::all_of(op.meta.roles.begin(),op.meta.roles.end(),detail::is_cell_origin_provenance) &&
        op.target_meta.roles.empty();
  };
  for(const auto& op:ops)if(op.meta.raw || op.meta.logical_register_analysis)return result;
  std::map<int,std::size_t> by_address;int address=0;
  for(std::size_t i=0;i<ops.size();++i){if(ops[i].kind!=IrKind::Label)by_address.emplace(address,i);address+=cells_per_op(ops[i]);}
  std::vector<std::size_t> sites;
  std::vector<std::vector<std::pair<std::size_t,PostLayoutExecutionEdgeKind>>> predecessors(flow.execution_states.size());
  for(std::size_t s=0;s<flow.execution_states.size();++s){
    const auto at=by_address.find(flow.execution_states[s].address);if(at==by_address.end())return result;
    sites.push_back(at->second);
    for(const auto& edge:flow.execution_edges[s]){if(edge.target_state>=predecessors.size())return result;
      predecessors[edge.target_state].emplace_back(s,edge.kind);}
  }
  const auto external=[&](std::size_t s){return std::any_of(flow.external_entries.begin(),flow.external_entries.end(),
      [&](const auto& entry){return post_layout_execution_matches_external_entry(flow.execution_states[s],entry);});};
  LivenessOptions lifetime;lifetime.discover_toggle_projection_signs=true;
  for(const auto& word:preloads)if(!word.setup_expression && !word.setup_expression_text && !word.setup_target_name)
    lifetime.equal_entry_value_classes.emplace(word.register_name,"literal:"+word.value);
  std::map<std::size_t,std::vector<IrOp>> replacements;
  for(std::size_t first=2;first+9U<ops.size();++first){
    const std::size_t store=first+9U;const auto& destination=ops[store];
    if(destination.kind!=IrKind::Store || !plain(destination) || destination.opcode<0x40 || destination.opcode>0x4e ||
        destination.register_name!=std::string(1,"0123456789abcde"[destination.opcode-0x40]))continue;
    bool literal=true;for(std::size_t i=first;i<first+8U;++i)literal=literal && plain(ops[i]) && ops[i].kind==IrKind::Plain && ops[i].opcode==9;
    literal=literal && plain(ops[first+8U]) && ops[first+8U].kind==IrKind::Plain && ops[first+8U].opcode==0x0b;
    const auto& guard=ops[first-1U];const auto& difference=ops[first-2U];
    if(!literal || !plain(guard) || guard.kind!=IrKind::CondJump || guard.opcode!=0x5e ||
        !plain(difference) || difference.kind!=IrKind::Plain || difference.opcode!=0x11)continue;
    bool reached=false,valid=true;std::vector<std::size_t> roots;
    for(std::size_t s=0;s<sites.size();++s){
      if(sites[s]<first-1U || sites[s]>store)continue;
      if(external(s) || predecessors[s].empty()){valid=false;break;}
      for(const auto& [before,kind]:predecessors[s])
        if(sites[before]+1U!=sites[s] || kind!=PostLayoutExecutionEdgeKind::Fallthrough)valid=false;
      if(sites[s]==first)reached=true;
      if(sites[s]==store)roots.insert(roots.end(),flow.execution_successors[s].begin(),flow.execution_successors[s].end());
    }
    if(!reached || !valid || roots.empty())continue;
    if(destination.register_name!="3"){
      const GuardedOldWordQuery query{store,"3"};
      const auto dead=analyze_dead_old_words(ops,flow,{query},lifetime,maximum_work);
      if(dead.exhausted || !dead.dead_words.contains(query))continue;
    }
    // Literal entry and D3 have identical X/Y/Z/T and physical last-X, but
    // their decimal shadow need not agree. It must die on every continuation.
    if(!prove_post_layout_stack_state_equality(items,flow,roots,{{true,true,true,true},false,true},
        {.refine_runtime_selectors=false,.numeric_entry_closed=true,
         .transport_closed_entry_through_control=true,.use_indirect_store_entry_facts=true}))continue;
    IrOp seed=make_store("3");seed.meta.source_line=destination.meta.source_line;
    IrOp recall;recall.kind=IrKind::IndirectRecall;recall.register_name="3";recall.opcode=0xd3;
    recall.meta.mnemonic=opcode_by_code(0xd3).name;recall.meta.source_line=destination.meta.source_line;
    recall.meta.indirect_memory_targets=std::vector<int>{3};
    std::vector<IrOp> replacement{std::move(seed),std::move(recall)};
    if(destination.register_name!="3")replacement.push_back(destination);
    replacements.emplace(first,std::move(replacement));++result.applied;
    // Prove one producer at a time: another replaced literal must not serve
    // as a spurious X2 convergence witness for this transaction.
    break;
  }
  if(!result.applied)return result;
  result.ops.clear();
  for(std::size_t i=0;i<ops.size();++i)if(const auto at=replacements.find(i);at!=replacements.end()){
    result.ops.insert(result.ops.end(),at->second.begin(),at->second.end());i+=9U;
  }else result.ops.push_back(ops[i]);
  result.optimizations.push_back({"deferred-zero-underflow-literal",
      "Composed canonical-zero literal materialization with exact guarded scratch death and "
      "stack/X1/X2 continuation proofs before binding selector geometry."});
  return result;
}

} // namespace mkpro::core::passes
