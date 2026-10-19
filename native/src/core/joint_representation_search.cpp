#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/joint_proof_input.hpp"
#include "mkpro/core/joint_constraint_origins.hpp"
#include "mkpro/core/detail/cell_provenance.hpp"
#include "mkpro/core/post_layout_packed_sign.hpp"

#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/stack_value_equivalence.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/passes/common_processor_factoring.hpp"
#include "mkpro/core/passes/contextual_copy_layout.hpp"
#include "mkpro/core/passes/contextual_copy_transport.hpp"
#include "mkpro/core/passes/zero_underflow_constant.hpp"
#include "mkpro/core/helper_invariant_recall_hoist.hpp"
#include "mkpro/core/passes/liveness_analysis.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/core/passes/indirect_selector_seed_reuse.hpp"
#include "mkpro/core/passes/register_coalesce.hpp"
#include "mkpro/core/passes/idempotent_entry_prologue.hpp"
#include "mkpro/core/passes/selector_charge_literal_sinking.hpp"
#include "mkpro/core/passes/semantic_region_folding.hpp"
#include "mkpro/core/passes/shared_straight_line_helper.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/shared_helper_dual_mode_layout.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace mkpro::core {
namespace {

int cells(const LayoutSearchArtifact& artifact) {
  return static_cast<int>(std::count_if(artifact.items.begin(), artifact.items.end(),
      [](const MachineItem& item) { return item.kind != MachineItemKind::Label; }));
}

// Re-enter symbolic data optimization without erasing real layout contracts.
// Only obsolete per-pass cell-number provenance is discarded. Executable
// operands, raw instructions, dark aliases and malformed/stale charges remain
// unsupported; compiler selector roles themselves are never removed.
std::optional<std::vector<IrOp>> representation_view(
    const LayoutSearchArtifact& artifact, const CompileOptions& options,
    bool pending_equations = false) {
  const auto reject = [](std::string_view reason) -> std::optional<std::vector<IrOp>> {
    if (std::getenv("MKPRO_NATIVE_TRACE_FINITE_MODE_REPRESENTATION"))
      std::cerr << "[finite-mode-discovery] representation-view rejected: " << reason << '\n';
    return std::nullopt;
  };
  const auto model = address_space_model_for_feature_profile(effective_optimizer_feature_profile(options));
  auto normalized = normalize_natural_target_overflow_formals(artifact.items, model);
  if (!normalized || has_executable_address_words(*normalized))
    return reject("unsupported overflow or executable address contract");
  if (pending_equations) {
    // This is an equation-bearing search state, not executable publication.
    // Validate the same charge grammar while retaining the encoded origins.
    const auto charges = inspect_deferred_decimal_selector_charges(*normalized);
    if (!charges.diagnostics.empty()) return reject(charges.diagnostics.front().message);
    for (const auto& item : *normalized) {
      if (item.kind == MachineItemKind::Address && !std::holds_alternative<std::string>(item.target))
        return reject("pending direct target lost its command identity");
      if (item.indirect_flow_targets) for (const auto& target : *item.indirect_flow_targets)
        if (!std::holds_alternative<std::string>(target))
          return reject("pending indirect target lost its command identity");
    }
  } else {
    const auto bound = rebind_late_bound_decimal_selectors(
        *normalized, {.minimum_target_address = 0, .maximum_target_address = 99});
    if (!bound.diagnostics.empty() || bound.items.size() != normalized->size())
      return reject(bound.diagnostics.empty() ? "selector binding changed item count"
                                              : bound.diagnostics.front().message);
    for (std::size_t i = 0; i < bound.items.size(); ++i)
      if (bound.items[i].opcode != (*normalized)[i].opcode)
        return reject("stale executable selector charge");
  }
  std::map<int, std::size_t> commands;
  std::map<int, std::string> aliases;
  std::set<std::string> names;
  int address = 0;
  for (std::size_t i = 0; i < normalized->size(); ++i) {
    const auto& item = (*normalized)[i];
    if (item.kind == MachineItemKind::Label) {
      if (!names.insert(item.name).second)
        return reject("duplicate command label");
      aliases.try_emplace(address, item.name);
    } else {
      if (item.kind == MachineItemKind::Op)
        commands.emplace(address, i);
      ++address;
    }
  }
  std::map<std::size_t, std::string> generated;
  const auto symbolize = [&](IrTarget& target) {
    const auto* numeric = std::get_if<int>(&target);
    if (!numeric)
      return true;
    const auto found = commands.find(*numeric);
    if (found == commands.end())
      return false;
    auto alias = aliases.find(*numeric);
    if (alias == aliases.end()) {
      std::string name = "__representation_entry_" + std::to_string(*numeric);
      while (names.contains(name)) name += '_';
      names.insert(name);
      generated.emplace(found->second, name);
      alias = aliases.emplace(*numeric, std::move(name)).first;
    }
    target = alias->second;
    return true;
  };
  for (auto& item : *normalized) {
    if (item.kind == MachineItemKind::Address && !symbolize(item.target))
      return reject("direct target is not an executable command");
    if (item.indirect_flow_targets)
      for (auto& target : *item.indirect_flow_targets)
        if (!symbolize(target))
          return reject("indirect target is not an executable command");
    std::erase_if(item.roles, [](const auto& role) {
      return detail::is_cell_origin_provenance(role);
    });
  }
  std::vector<MachineItem> symbolic;
  for (std::size_t i = 0; i < normalized->size(); ++i) {
    if (const auto label = generated.find(i); label != generated.end())
      symbolic.push_back(MachineItem::label(label->second));
    symbolic.push_back(std::move((*normalized)[i]));
  }
  return raise_machine_to_ir(symbolic, effective_optimizer_feature_profile(options));
}

std::optional<LayoutSearchArtifact> apply(const LayoutSearchArtifact& parent,
                                         passes::PassResult result) {
  if (result.applied <= 0)
    return std::nullopt;
  LayoutSearchArtifact child = parent;
  child.items = lower_ir_to_machine(result.ops);
  for (auto& preload : result.preloads) {
    const auto found = std::find_if(child.preloads.begin(), child.preloads.end(),
        [&](const auto& old) { return old.register_name == preload.register_name; });
    if (found != child.preloads.end())
      return std::nullopt; // No implicit takeover of an entry-live register.
    child.preloads.push_back(std::move(preload));
  }
  for (auto& report : result.optimizations)
    child.optimizations.push_back({std::move(report.name), std::move(report.detail)});
  return child;
}

} // namespace


std::optional<LayoutSearchArtifact> transport_layout_search_artifact(
    const LayoutSearchArtifact& artifact, FeatureProfile source_profile,
    FeatureProfile target_profile, std::string* rejection) {
  const auto reject = [&](const std::string& reason) -> std::optional<LayoutSearchArtifact> {
    if (rejection) *rejection = reason;
    return std::nullopt;
  };
  if (rejection) rejection->clear();
  const auto source_model = address_space_model_for_feature_profile(source_profile);
  const auto target_model = address_space_model_for_feature_profile(target_profile);
  const int register_limit = std::min(feature_profile_max_register_index(source_profile),
                                      feature_profile_max_register_index(target_profile));
  const auto reg_index = [](const std::string& name) -> int {
    if (name.size() != 1U) return -1;
    const char digit = name.front();
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    return -1;
  };
  for (const auto& item : artifact.items) {
    if (item.raw)
      return reject("raw instructions have no cross-model effect certificate");
    if (item.kind == MachineItemKind::Op) {
      const int family = item.opcode & 0xf0;
      if ((family == 0x40 || family == 0x60 ||
           (family >= 0x70 && family <= 0xe0)) &&
          (item.opcode & 0x0f) > register_limit)
        return reject("register opcode changes meaning between target models");
      if (item.indirect_memory_targets)
        for (const int reg : *item.indirect_memory_targets)
          if (reg < 0 || reg > register_limit)
            return reject("indirect memory target is outside the shared register bank");
    }
  }
  std::set<int> preload_registers;
  for (const auto& preload : artifact.preloads) {
    const int reg = reg_index(preload.register_name);
    if (reg < 0 || reg > register_limit || !preload_registers.insert(reg).second)
      return reject("preload is outside the shared bank or has an ambiguous owner");
  }
  auto normalized = normalize_natural_target_overflow_formals(artifact.items, source_model);
  if (!normalized || has_executable_address_words(*normalized))
    return reject("opaque address/code words cannot cross memory models");
  const auto rebound = rebind_late_bound_decimal_selectors(
      *normalized, {.minimum_target_address = 0, .maximum_target_address = 99});
  if (!rebound.diagnostics.empty() || rebound.items.size() != normalized->size())
    return reject("runtime decimal selector obligations are not closed");
  for (std::size_t i = 0; i < normalized->size(); ++i)
    if (rebound.items[i].opcode != (*normalized)[i].opcode)
      return reject("runtime decimal selector charge is stale");

  PostLayoutControlFlowOptions flow_options;
  flow_options.address_space_model = source_model;
  flow_options.empty_return_target = 1;
  const auto before = build_post_layout_control_flow(*normalized, flow_options);
  flow_options.address_space_model = target_model;
  const auto after = build_post_layout_control_flow(*normalized, flow_options);
  // Require identity, including encoded counters, fetched operand identities,
  // manual observations and every return frame. Logical oversized code stays
  // logical in both graphs; crossing a physical-window boundary is not guessed.
  if (!before.proved || !after.proved ||
      before.external_entries != after.external_entries ||
      before.indirect_flow_targets != after.indirect_flow_targets ||
      before.indirect_memory_targets != after.indirect_memory_targets ||
      before.empty_return_target != after.empty_return_target ||
      before.execution_states != after.execution_states ||
      before.execution_successors != after.execution_successors ||
      before.execution_edges != after.execution_edges ||
      before.maximum_observed_return_depth != after.maximum_observed_return_depth)
    return reject("memory models disagree on exact execution or return contexts");

  for (const auto& preload : artifact.preloads) {
    const int reg = reg_index(preload.register_name);
    const bool used = std::any_of(before.indirect_flow_targets.begin(),
        before.indirect_flow_targets.end(), [&](const auto& entry) {
          return ((*normalized)[entry.first].opcode & 0xf) == reg;
        });
    if (!used) continue;
    if (preload.setup_expression || preload.setup_target_name ||
        preload.setup_expression_text)
      return reject("generated flow preload needs a cross-model value proof");
    const auto old_value = evaluate_indirect_address(
        preload.register_name, preload.value, IndirectOperationKind::Flow, source_model);
    const auto new_value = evaluate_indirect_address(
        preload.register_name, preload.value, IndirectOperationKind::Flow, target_model);
    if (!old_value || !new_value ||
        old_value->actual_flow_target != new_value->actual_flow_target ||
        old_value->result_value != new_value->result_value)
      return reject("preloaded flow word changes target or selector writeback");
  }
  auto result = artifact;
  result.items = std::move(*normalized);
  return result;
}

std::vector<ProvedSearchTransition<LayoutSearchArtifact>>
contextual_copy_layout_transactions(const LayoutSearchArtifact& artifact,
                                    const CompileOptions& options) {
  // A cheap instruction-shape gate, independent of source names, constants
  // or equality requests. An already coalesced copy can also be redundant.
  bool copy = false;
  for (std::size_t i = 1; i < artifact.items.size(); ++i)
    if (artifact.items[i - 1U].kind == MachineItemKind::Op &&
        artifact.items[i].kind == MachineItemKind::Op &&
        !artifact.items[i - 1U].raw && !artifact.items[i].raw &&
        artifact.items[i - 1U].opcode >= 0x60 &&
        artifact.items[i - 1U].opcode <= 0x6e &&
        artifact.items[i].opcode >= 0x40 && artifact.items[i].opcode <= 0x4e)
      copy = true;
  if (!copy) return {};
  const auto ops = representation_view(artifact, options);
  if (!ops) return {};
  CompileOptions configured = options;
  configured.defer_return_suffix_until_callee_hole = true;
  configured.coalesce_copies = true;
  std::vector<ProvedSearchTransition<LayoutSearchArtifact>> result;
  for (auto& proposal : passes::contextual_copy_layout_candidates(
           *ops, {configured}, {.runtime_preloads = &artifact.preloads}))
    if (auto child = apply(artifact, std::move(proposal));
        child && cells(*child) < cells(artifact))
      result.push_back({std::move(*child), "joint-contextual-copy-layout", true});
  return result;
}

std::vector<ProvedSearchTransition<LayoutSearchArtifact>>
expand_joint_representation_transactions(const LayoutSearchArtifact& artifact,
                                         const CompileOptions& options,
                                         bool preload_environment_complete) {
  using Edge = ProvedSearchTransition<LayoutSearchArtifact>;
  std::vector<Edge> result;
  CompileOptions context_options = options;
  context_options.defer_return_suffix_until_callee_hole = true;
  // A requested, independently proved recoloring is part of this search's
  // contract. It must not silently depend on whether an earlier source-level
  // copy-coalescing candidate happened to win. No copy is erased here.
  context_options.coalesce_copies = true;
  const passes::PassContext context{context_options};
  const auto ops = raise_machine_to_ir(artifact.items, effective_optimizer_feature_profile(options));
  const auto add = [&](passes::PassResult candidate, const std::string& rule) {
    if (auto child = apply(artifact, std::move(candidate));
        child && !same_layout_search_artifact(artifact, *child))
      result.push_back({std::move(*child), rule, true});
  };
  add(passes::semantic_region_folding(ops, context), "semantic-region-fold");
  const auto requests = passes::semantic_region_register_requests(ops);
  passes::for_each_register_web_recoloring(ops, context, requests,
      [&](std::vector<IrOp> recolored) {
        add(passes::PassResult{.ops = std::move(recolored), .applied = 1,
            .optimizations = {{.name = "semantic-region-web-recolor",
                .detail = "Recolored proved reaching-definition webs to unify region operands; "
                          "kept the equal-size enabling alternative until composition."}}},
            "semantic-region-web-recolor");
      });
  // Keep the original IR search unchanged. A complete artifact can additionally
  // carry late-bound callback charges and physical-01 empty-return edges.
  const auto laid_out = preload_environment_complete ? representation_view(artifact, options)
      : std::optional<std::vector<IrOp>>{};
  if (std::getenv("MKPRO_NATIVE_TRACE_FINITE_MODE_REPRESENTATION"))
    std::cerr << "[finite-mode-discovery] complete=" << preload_environment_complete
              << " representation-view=" << laid_out.has_value() << '\n';
  for (auto& alternative : passes::finite_mode_representation_candidates(
           laid_out ? *laid_out : ops, context, {.maximum_growth_cells = 2, .maximum_candidates = 6,
                          .allow_target_relayout = true,
                          .runtime_preloads = preload_environment_complete
                              ? &artifact.preloads : nullptr,
                          .preserve_compiler_selector_charges = laid_out.has_value()}))
    add(std::move(alternative), "joint-word-and-target-synthesis");

  // Contextual copy/layout closure runs after the complete ordinary root
  // search, not in this older bounded beam. Its independent final lane must
  // not evict an enabling route before that route receives final pricing.

  // This is a separate complete-artifact edge, not a wider greedy geometry
  // rule. A locally smaller stitch can force a worse later selector layout;
  // the caller's full finalizer must compare it with the unchanged incumbent.
  bool annotated_jump = false;
  bool annotation = false;
  for (const auto& item : artifact.items) {
    if (item.kind == MachineItemKind::Label) {
      annotation = true;
      continue;
    }
    annotated_jump = annotated_jump ||
        (annotation && item.kind == MachineItemKind::Op && !item.raw &&
         (item.opcode == 0x51 || (item.opcode >= 0x87 && item.opcode <= 0x8e)));
    annotation = false;
  }
  if (preload_environment_complete && annotated_jump) {
    const auto model = address_space_model_for_feature_profile(
        effective_optimizer_feature_profile(options));
    auto normalized = normalize_natural_target_overflow_formals(artifact.items, model);
    if (normalized) {
      PostLayoutControlFlowOptions flow_options;
      flow_options.address_space_model = model;
      flow_options.empty_return_target = 1;
      const auto flow = build_post_layout_control_flow(*normalized, flow_options);
      NaturalTargetComponentLayoutOptions layout_options;
      layout_options.address_space_model = model;
      layout_options.maximum_subset_states = 2000;
      layout_options.maximum_anchor_combinations = 32;
      layout_options.allow_standalone_fallthrough_jump_fold = true;
      layout_options.allow_unreferenced_jump_labels = true;
      const auto placed = optimize_natural_target_component_layout(
          *normalized, artifact.preloads, flow, layout_options);
      if (placed.applied > 0 && placed.plan.proved && placed.plan.final_artifact_proved &&
          placed.plan.fallthrough_jump_folds > 0 && placed.plan.final_control_flow.proved &&
          placed.plan.output_cells < cells(artifact)) {
        auto candidate = artifact;
        candidate.items = placed.items;
        candidate.preloads = placed.preloads;
        candidate.optimizations.push_back({"unreferenced-jump-label-layout",
            "Stitched a proved transparent jump without treating unused annotations as "
            "executable entries; transported labels and re-proved selectors, CFG, stack/X2 "
            "and return contexts. Kept as a separate complete-artifact alternative."});
        if (auto closed = rebind_layout_search_artifact(std::move(candidate));
            closed && cells(*closed) < cells(artifact))
          result.push_back({std::move(*closed), "unreferenced-jump-label-layout", true});
      }
    }
  }

  return result;
}


namespace {

// Conditional semantic state. No user of LayoutSearchArtifact can accidentally
// receive this object before the physical obligations have been discharged.
struct ConstrainedRepresentation {
  LayoutSearchArtifact artifact;
  std::map<std::string, int> targets;
  // Word provenance belongs to its carrier, not the target label to which
  // a speculative family assignment currently attaches it.
  std::map<std::string, int> encoded_selectors{};
};

std::optional<std::map<std::string, int>> constraint_positions(
    const std::vector<MachineItem>& items) {
  std::map<std::string, int> result;
  int address = 0;
  for (const auto& item : items) {
    if (item.kind == MachineItemKind::Label) {
      if (!result.emplace(item.name, address).second)
        return std::nullopt;
    } else {
      ++address;
    }
  }
  return result;
}

// Retain obligations by command identity, not their speculative cell numbers.
// New compiler-generated consumers acquire a binding; removed consumers do not
// leave a spurious global placement constraint behind.
bool refresh_constraint_targets(ConstrainedRepresentation& node) {
  const auto positions = constraint_positions(node.artifact.items);
  if (!positions)
    return false;
  std::map<std::string, int> targets;
  for (const auto& item : node.artifact.items)
    if (item.indirect_flow_targets)
      for (const auto& target : *item.indirect_flow_targets) {
        const auto* label = std::get_if<std::string>(&target);
        if (!label || !positions->contains(*label))
          return false;
        const auto previous = node.targets.find(*label);
        targets.emplace(*label, previous == node.targets.end()
            ? positions->at(*label) : previous->second);
      }
  node.targets = std::move(targets);
  return true;
}

bool charged_constraint_consumer(const MachineItem& item) {
  return std::find(item.roles.begin(), item.roles.end(),
                   "late-decimal-selector-consumer") != item.roles.end();
}

// Consume a register vacancy only as part of a complete allocation/layout
// transaction. Literal/recall equivalence includes entry mode, X1/X2, all side
// entries and matched returns; adding a setup word requires its own Main proof.
std::vector<ConstrainedRepresentation> pool_allocation_exposed_literals(
    const ConstrainedRepresentation& parent, const passes::PassContext& context,
    const PostLayoutControlFlowOptions& control) {
  std::vector<ConstrainedRepresentation> result;
  const auto& items=parent.artifact.items;
  const auto ops=raise_machine_to_ir(items,effective_optimizer_feature_profile(context.options));
  std::set<std::string> used;
  for(const auto& op:ops){
    const auto effect=passes::register_effects(op);
    if(effect.uses_all_registers || effect.may_define_any_register)return result;
    used.insert(effect.uses.begin(),effect.uses.end());
    used.insert(effect.must_defs.begin(),effect.must_defs.end());
    used.insert(effect.may_defs.begin(),effect.may_defs.end());
  }
  for(const auto& word:parent.artifact.preloads)used.insert(word.register_name);
  std::optional<int> free;
  for(int r=7;r<15;++r){const std::string name(1,"0123456789abcde"[r]);
    if(!used.contains(name) && !context.options.preloaded_constant_registers.contains(name)){free=r;break;}}
  if(!free)return result;
  const auto flow=build_post_layout_control_flow(items,control);
  if(!flow.proved || flow.execution_states.size()!=flow.execution_successors.size())return result;
  const auto lift=analyze_post_layout_automatic_entry_lift_with_indirect_store_facts(items,flow);
  if(!lift)return result;
  const auto ordinary=[&](std::size_t at){
    if(at>=items.size())return false;
    const auto& x=items[at];
    return x.kind==MachineItemKind::Op && !x.raw && !x.manual_interaction &&
        !x.procedure_boundary && !x.formal_opcode && !x.logical_register_name &&
        !x.indirect_flow_targets && !x.indirect_memory_targets &&
        x.semantic_call_origins.empty() &&
        std::all_of(x.roles.begin(),x.roles.end(),detail::is_cell_origin_provenance);
  };
  const auto binary_boundary=[](int op){return (op>=0x10 && op<=0x13) || (op>=0x37 && op<=0x39);};
  std::vector<std::vector<std::size_t>> predecessors(flow.execution_states.size());
  std::map<std::size_t,std::vector<std::size_t>> contexts;
  std::vector<int> addresses(items.size());int address=0;
  for(std::size_t i=0;i<items.size();++i){addresses[i]=address;if(items[i].kind!=MachineItemKind::Label)++address;}
  for(std::size_t s=0;s<flow.execution_states.size();++s){
    contexts[flow.execution_states[s].item_index].push_back(s);
    for(const auto next:flow.execution_successors[s])predecessors[next].push_back(s);
  }
  struct Site{std::size_t begin,end;};
  std::map<std::string,std::vector<Site>> groups;
  for(std::size_t at=0;at<items.size();++at){
    if(!ordinary(at) || items[at].opcode<1 || items[at].opcode>9)continue;
    std::size_t end=at;std::string word;
    while(end<items.size() && ordinary(end) && items[end].opcode>=0 && items[end].opcode<=9){
      word+=static_cast<char>('0'+items[end].opcode);++end;
    }
    if(word.size()<3U || word.size()>8U || !ordinary(end) || !binary_boundary(items[end].opcode) ||
        !contexts.contains(at) || !bound_ordinary_decimal(word))continue;
    bool valid=true;
    for(const auto s:contexts[at])valid=valid && lift->at(s);
    for(std::size_t i=at;i<=end && valid;++i){
      if(!contexts.contains(i)){valid=false;break;}
      for(const auto s:contexts[i]){
        if(i>at){valid=valid && !predecessors[s].empty();
          for(const auto prev:predecessors[s])valid=valid && flow.execution_states[prev].item_index==i-1U;}
        if(i<end){const auto& next=flow.execution_successors[s];
          valid=valid && next.size()==1U && flow.execution_states[next.front()].item_index==i+1U &&
              flow.execution_states[next.front()].return_stack==flow.execution_states[s].return_stack &&
              flow.execution_states[next.front()].formal_return_stack==flow.execution_states[s].formal_return_stack;}
      }
    }
    for(const auto& state:flow.execution_states){
      if(state.operand_item_index && *state.operand_item_index>=at && *state.operand_item_index<end)valid=false;
      for(const int frame:state.return_stack)if(frame>addresses[at] && frame<addresses[end])valid=false;
    }
    if(valid)groups[word].push_back({at,end});
  }
  std::vector<std::pair<std::string,std::vector<Site>>> ordered(groups.begin(),groups.end());
  std::stable_sort(ordered.begin(),ordered.end(),[](const auto&a,const auto&b){
    return (a.first.size()-1U)*a.second.size()>(b.first.size()-1U)*b.second.size();});
  for(const auto& [word,sites]:ordered){
    auto guarded=items;
    for(const auto& site:sites)for(std::size_t i=site.begin;i<site.end;++i)guarded[i].raw=true;
    bool valid=true;
    for(const auto& site:sites){
      StackValueEqualityState equality{{true,true,true,true},false,true};
      valid=valid && transfer_stack_value_equality(equality,items[site.end].opcode,
          StackValueEqualityStepKind::Plain)!=StackValueEqualityTransfer::Rejected;
      std::vector<std::size_t> roots;
      for(const auto s:contexts.at(site.end)){
        const auto& next=flow.execution_successors[s];if(next.empty())valid=false;
        roots.insert(roots.end(),next.begin(),next.end());
      }
      std::sort(roots.begin(),roots.end());roots.erase(std::unique(roots.begin(),roots.end()),roots.end());
      valid=valid && !roots.empty() && prove_post_layout_stack_state_equality(guarded,flow,roots,equality,
          {.refine_runtime_selectors=false,.numeric_entry_closed=true,
           .transport_closed_entry_through_control=true,.use_indirect_store_entry_facts=true});
    }
    if(!valid)continue;
    auto next=parent;next.artifact.items.clear();
    std::map<std::size_t,std::size_t> ends;for(const auto& site:sites)ends[site.begin]=site.end;
    for(std::size_t i=0;i<items.size();){
      if(const auto site=ends.find(i);site!=ends.end()){
        auto recall=items[i];recall.opcode=0x60+*free;recall.mnemonic=opcode_by_code(recall.opcode).name;
        recall.comment="allocation-exposed literal constant";next.artifact.items.push_back(std::move(recall));i=site->second;
      }else next.artifact.items.push_back(items[i++]);
    }
    const std::string name(1,"0123456789abcde"[*free]);
    next.artifact.preloads.push_back({.register_name=name,.value=word,.counts_against_program=false,.lowered_data_value=word});
    const auto delivered=build_post_layout_control_flow(next.artifact.items,control);
    const auto external=resolve_post_layout_external_entry_contexts(delivered);
    if(!delivered.proved || !external)continue;
    std::vector<std::size_t> main;
    for(std::size_t s=0;s<external->main.size();++s)if(external->main[s])main.push_back(s);
    if(main.empty() || !prove_post_layout_stack_state_equality(next.artifact.items,delivered,main,
        {{false,false,false,false},false,false},
        {.refine_runtime_selectors=false,.transport_closed_entry_through_control=true,
         .use_indirect_store_entry_facts=true}))continue;
    next.artifact.optimizations.push_back({"allocation-exposed-literal-pool",
        "Used vacant R"+name+" for "+word+" after whole-web coloring; proved no original memory alias or "
        "pool ownership, complete decimal entry boundaries, every stack/X1/X2 continuation and private Main setup effects. "
        "Deferred all selector and placement equations; removed "+std::to_string(cells(parent.artifact)-cells(next.artifact))+" local cells."});
    if(refresh_constraint_targets(next))result.push_back(std::move(next));
    if(result.size()>=2U)break;
  }
  return result;
}

// A literal may pay for a different placement of a whole stable selector family.
// This is a conditional semantic edge: no intermediate geometry is executable.
// The existing joint solver must close every target and preload before publishing.

std::vector<ConstrainedRepresentation> literal_selector_equations(
    const ConstrainedRepresentation& parent, const PostLayoutControlFlowOptions& options,
    std::size_t maximum_candidates) {
  std::vector<ConstrainedRepresentation> result;
  if (maximum_candidates == 0) return result;
  const auto& items = parent.artifact.items;
  const auto ordinary = [&](std::size_t at) {
    if (at >= items.size()) return false;
    const auto& item = items[at];
    return item.kind == MachineItemKind::Op && !item.raw && item.roles.empty() &&
        !item.manual_interaction && !item.indirect_flow_targets &&
        !item.indirect_memory_targets && !item.indirect_flow_formal_targets &&
        !item.logical_register_name && !item.logical_indirect_memory_targets &&
        !item.procedure_boundary && item.semantic_call_origins.empty() &&
        !item.discarded_indirect_recall_value && !item.borrowed_entry_phase_selector &&
        !item.formal_opcode;
  };
  const auto literal_start = [&](std::size_t at) {
    return at + 2U < items.size() && ordinary(at) && ordinary(at + 1U) &&
        ordinary(at + 2U) && items[at].opcode >= 1 && items[at].opcode <= 9 &&
        items[at + 1U].opcode >= 0 && items[at + 1U].opcode <= 9 &&
        items[at + 2U].opcode >= 0x10 && items[at + 2U].opcode <= 0x13;
  };
  bool potential = false;
  for (std::size_t at = 0; at < items.size() && !potential; ++at)
    potential = literal_start(at);
  if (!potential) return result;
  const auto flow = build_post_layout_control_flow(items, options);
  if (!flow.proved) return result;
  const auto lift = analyze_post_layout_automatic_entry_lift(items, flow);
  if (!lift) return result;
  std::vector<std::vector<std::size_t>> predecessors(flow.execution_states.size());
  std::map<std::size_t, std::vector<std::size_t>> contexts;
  for (std::size_t state = 0; state < flow.execution_states.size(); ++state) {
    contexts[flow.execution_states[state].item_index].push_back(state);
    for (const auto next : flow.execution_successors[state])
      predecessors[next].push_back(state);
  }
  std::set<std::size_t> entries;
  for (const auto& entry : flow.external_entries) entries.insert(entry.entry.item_index);
  std::map<int, std::vector<std::size_t>> literals;
  for (std::size_t at = 0; at + 2U < items.size(); ++at) {
    if (!literal_start(at) || !contexts.contains(at) || !contexts.contains(at + 1U) ||
        !contexts.contains(at + 2U) || entries.contains(at) || entries.contains(at + 1U))
      continue;
    bool safe = std::all_of(contexts.at(at).begin(), contexts.at(at).end(),
        [&](std::size_t state) { return (*lift)[state]; });
    for (const auto state : contexts[at + 1U]) {
      safe = safe && !predecessors[state].empty();
      for (const auto previous : predecessors[state])
        safe = safe && flow.execution_states[previous].item_index == at;
    }
    // Digit entry and a recall have identical value and stack lift here, but
    // not X2 or entry mode. The retained arithmetic closes both entry modes;
    // every exact continuation must independently kill the old X2 difference.
    StackValueEqualityState equal{{true, true, true, true}, false, true};
    safe = safe && transfer_stack_value_equality(
        equal, items[at + 2U].opcode, StackValueEqualityStepKind::Plain) !=
            StackValueEqualityTransfer::Rejected;
    std::set<int> continuations;
    for (const auto state : contexts.at(at + 2U)) {
      safe = safe && !flow.execution_successors[state].empty();
      for (const auto next : flow.execution_successors[state])
        continuations.insert(flow.execution_states[next].address);
    }
    for (const int address : continuations)
      safe = safe && prove_post_layout_stack_entry_equality(items, flow, address, equal,
          {.refine_runtime_selectors = false, .numeric_entry_closed = true});
    if (safe) literals[10 * items[at].opcode + items[at + 1U].opcode].push_back(at);
  }
  for (const auto& [value, sites] : literals) {
    const auto spelling = std::to_string(value);
    for (std::size_t p = 0; p < parent.artifact.preloads.size(); ++p) {
      const auto& preload = parent.artifact.preloads[p];
      if (preload.register_name.size() != 1U ||
          std::string("789abcde").find(preload.register_name.front()) == std::string::npos)
        continue;
      const int reg = static_cast<int>(std::string("0123456789abcde").find(
          preload.register_name.front()));
      const auto decoded = evaluate_indirect_address(preload.register_name, preload.value,
          IndirectOperationKind::Flow, options.address_space_model);
      if (!decoded || !decoded->actual_flow_target) continue;
      const int old_target = *decoded->actual_flow_target;
      const auto known = parent.encoded_selectors.find(preload.register_name);
      if (known != parent.encoded_selectors.end() && known->second != old_target) continue;
      const auto word = rebind_stable_preloaded_indirect_flow_selector(items, preload, flow,
          old_target, value, options.address_space_model, &parent.artifact.preloads);
      if (!word || *word != spelling) continue;
      std::set<std::string> labels;
      bool consistent = true;
      for (const auto& item : items) {
        if (!item.indirect_flow_targets || (item.opcode & 15) != reg) continue;
        if (charged_constraint_consumer(item)) { consistent = false; break; }
        for (const auto& target : *item.indirect_flow_targets) {
          const auto* label = std::get_if<std::string>(&target);
          if (!label || !parent.targets.contains(*label) ||
              (known == parent.encoded_selectors.end() &&
               parent.targets.at(*label) != old_target)) {
            consistent = false; break;
          }
          labels.insert(*label);
        }
      }
      if (!consistent || labels.empty()) continue;
      ConstrainedRepresentation next = parent;
      next.artifact.preloads[p].value = spelling;
      // Preserve immutable source/setup provenance; only the proved runtime word changes.
      next.encoded_selectors[preload.register_name] = value;
      for (const auto& label : labels) next.targets[label] = value;
      std::set<std::size_t> starts(sites.begin(), sites.end());
      next.artifact.items.clear();
      for (std::size_t at = 0; at < items.size(); ++at) {
        if (starts.contains(at)) {
          auto recall = items[at];
          recall.opcode = 0x60 + reg;
          recall.mnemonic = opcode_by_code(recall.opcode).name;
          recall.comment = "joint literal/address constant";
          next.artifact.items.push_back(std::move(recall));
          ++at;
        } else next.artifact.items.push_back(items[at]);
      }
      if (!refresh_constraint_targets(next) ||
          !build_post_layout_control_flow(next.artifact.items, options).proved) continue;
      next.artifact.optimizations.push_back({"joint-literal-selector-co-synthesis",
          "Shared numeric literal " + spelling + " with stable selector R" +
          preload.register_name + "; removed " + std::to_string(sites.size()) +
          " cells under one deferred target equation and full stack/X1/X2 continuation proofs."});
      if (std::getenv("MKPRO_NATIVE_TRACE_JOINT_LAYOUT_SEARCH"))
        std::cerr << "[literal-selector] R" << preload.register_name << "=" << value
                  << " removed=" << sites.size() << '\n';
      result.push_back(std::move(next));
      if (result.size() == maximum_candidates) return result;
    }
  }
  return result;
}

using detail::PreparedJointProofInput;
using detail::JointProofPreparationCounts;

bool literal_family_owns_charges(
    const passes::DeferredSelectorDataLiteralLayout& preparation,
    const std::vector<MachineItem>& input) {
  const auto carrier=detail::joint_origin_register(preparation.selector_register);
  if (!carrier || *carrier<7 || *carrier>14) return false;
  const std::set<std::string> certified_targets(preparation.charge_targets.begin(),
                                                preparation.charge_targets.end());
  if (!certified_targets.contains(preparation.target_label)) return false;
  const std::string owner_role="late-decimal-selector-register:"+
                              preparation.selector_register;
  for (const auto& item:input) {
    bool names_target=false;
    for (const auto& certified_target:certified_targets)
    for (const auto part:{LateBoundDecimalSelectorPart::High,
                          LateBoundDecimalSelectorPart::Low,
                          LateBoundDecimalSelectorPart::Single})
      names_target=names_target ||
          std::find(item.roles.begin(),item.roles.end(),
              make_late_bound_decimal_selector_role(part,certified_target))
              !=item.roles.end();
    if (!names_target) continue;
    int owners=0;
    for (const auto& role:item.roles)
      if (role.starts_with("late-decimal-selector-register:")) {
        if (role!=owner_role) return false;
        ++owners;
      }
    if (owners!=1) return false;
  }
  return true;
}

std::optional<LayoutSearchArtifact> close_representation_constraints(
    const ConstrainedRepresentation& node, const passes::PassContext& context,
    const PostLayoutControlFlowOptions& control_options, bool complete_preloads,
    bool project_shared_tail,
    const passes::DeferredSelectorDataLiteralLayout* pending_literal,
    PreparedJointProofInput& proof_input, bool precise_entry_facts = false) {
  const auto& input = proof_input.items();
  const auto& flow = proof_input.flow();
  if (!flow.proved)
    return std::nullopt;
  const auto positions = constraint_positions(input);
  if (!positions)
    return std::nullopt;
  std::optional<SharedHelperDualModePreparation> preparation;
  if (project_shared_tail) {
    if (!complete_preloads) return std::nullopt;
    preparation = prepare_shared_helper_dual_mode_layout(
        input, flow, control_options.address_space_model);
    if (preparation->applied <= 0) return std::nullopt;
  }
  std::map<std::string, std::size_t> preload_by_register;
  for (std::size_t i = 0; i < node.artifact.preloads.size(); ++i)
    if (!preload_by_register.emplace(node.artifact.preloads[i].register_name, i).second)
      return std::nullopt;

  struct Selector {
    std::string reg;
    int old_target = -1;
    std::set<std::string> labels;
    bool flexible = true;
  };
  std::map<std::string, Selector> selectors;
  const auto pending_origins = precise_entry_facts
      ? detail::joint_target_origins(input,node.targets,node.encoded_selectors)
      : std::optional<detail::JointOriginMap>{};
  if (precise_entry_facts && !pending_origins) return std::nullopt;
  const auto precise_charges = precise_entry_facts
      ? detail::protected_joint_runtime_charge_targets(input, &flow, &node.artifact.preloads, true, &*pending_origins)
      : std::optional<detail::JointOriginMap>{};
  const auto& protected_charges = precise_entry_facts
      ? precise_charges : proof_input.protected_runtime_charge_targets();
  if (!protected_charges) {
    if(std::getenv("MKPRO_NATIVE_TRACE_COPY_TRANSPORT"))std::cerr<<"[copy-transport-layout] protected charge preparation rejected\n";
    return std::nullopt;
  }
  std::map<std::string, int> fixed = *protected_charges;
  if (pending_literal) {
    // This target is intentionally data, not an arbitrary private word.
    // Discharge only the owned family through its per-mode entry theorem;
    // the data target keeps an exact literal equation. Re-prove after finalization.
    if (!literal_family_owns_charges(*pending_literal,input)) return std::nullopt;
    for (const auto& certified_target:pending_literal->charge_targets) fixed.erase(certified_target);

    const auto [binding,inserted] = fixed.emplace(
        pending_literal->target_label,pending_literal->literal_value);
    if (!inserted && binding->second != pending_literal->literal_value) return std::nullopt;
  }
  constexpr std::string_view names = "0123456789abcde";
  for (const auto& [source, destinations] : flow.indirect_flow_targets) {
    const auto& item = input[source];
    if (charged_constraint_consumer(item))
      continue; // The owned decimal binder, not a frozen address, closes it.
    const int reg = item.opcode & 15;
    if (reg < 0 || reg >= static_cast<int>(names.size()) ||
        !item.indirect_flow_targets || destinations.empty())
      return std::nullopt;
    const std::string name(1, names[static_cast<std::size_t>(reg)]);
    auto& selector = selectors[name];
    selector.reg = name;
    for (const auto& target : *item.indirect_flow_targets) {
      const auto* label = std::get_if<std::string>(&target);
      if (!label || !node.targets.contains(*label))
        return std::nullopt;
      const auto encoded = node.encoded_selectors.find(name);
      const int address = encoded == node.encoded_selectors.end()
          ? node.targets.at(*label) : encoded->second;
      if (selector.old_target >= 0 && selector.old_target != address)
        selector.flexible = false;
      if (selector.old_target < 0)
        selector.old_target = address;
      selector.labels.insert(*label);
    }
  }
  for (auto& [name, selector] : selectors) {
    const auto preload = preload_by_register.find(name);
    selector.flexible = selector.flexible && complete_preloads &&
        preload != preload_by_register.end();
    if (selector.flexible) {
      // Probe permission, not geometry. The actual chosen word receives its
      // own full data-projection/writeback certificate below.
      selector.flexible = false;
      // Zero is not representable by every fractional selector codec.
      // Failure of that one probe is not evidence of an immutable word.
      for (const int probe : {1, 2, 0}) {
        if (probe != selector.old_target &&
            proof_input.rebind_selector(node.artifact.preloads[preload->second],selector.old_target,probe)) {
          selector.flexible = true;
          break;
        }
      }
    }
    if (!selector.flexible)
      for (const auto& label : selector.labels) {
        const auto encoded = node.encoded_selectors.find(name);
        const int address = encoded == node.encoded_selectors.end()
            ? node.targets.at(label) : encoded->second;
        const auto [old, inserted] = fixed.emplace(label, address);
        if (!inserted && old->second != address)
          return std::nullopt;
      }
  }
  // A single constraint solution sees every codec's bounds and every remaining
  // immutable selector. No individual transformation chooses the block order.
  const auto& neutral_items = preparation ? preparation->items : input;
  const auto neutral_ops = raise_machine_to_ir(
      neutral_items, effective_optimizer_feature_profile(context.options));
  std::optional<passes::SymbolicRegionProjection> projection;
  // Runtime callback charges have a codec too. Give their complete target
  // domains to the same solver rather than accepting its first geometrically
  // valid order and discovering an unencodable callback only in the binder.
  std::set<std::string> decimal_charge_targets;
  for (const auto& item : neutral_items) {
    if (!charged_constraint_consumer(item)) continue;
    if (!item.indirect_flow_targets || item.indirect_flow_targets->empty())
      return std::nullopt;
    if (!projection) projection.emplace();
    for (const auto& target : *item.indirect_flow_targets) {
      const auto* label = std::get_if<std::string>(&target);
      if (!label) return std::nullopt;
      projection->target_bounds.emplace(*label, std::pair{0, 99});
      decimal_charge_targets.insert(*label);
    }
  }
  if (precise_entry_facts) {
    if (!projection) projection.emplace();
    projection->defer_indirect_entries = true;
    projection->maximum_padding_cells = 16;
  }
  if (preparation) {
    const auto widths = shared_helper_dual_mode_projected_widths(*preparation);
    if (!widths) return std::nullopt;
    if (!projection) projection.emplace();
    std::size_t item = 0;
    for (const auto& op : neutral_ops) {
      const auto length = op.kind == IrKind::Label
          ? 1U : static_cast<std::size_t>(passes::cells_per_op(op));
      if (item + length > widths->size()) return std::nullopt;
      int width = 0;
      for (std::size_t cell = 0; cell < length; ++cell) width += (*widths)[item++];
      projection->cell_widths.push_back(width);
    }
    if (item != widths->size()) return std::nullopt;
    const auto [entry, inserted] = fixed.emplace(
        preparation->continuation.helper_label, preparation->required_final_start_address);
    if (!inserted && entry->second != preparation->required_final_start_address)
      return std::nullopt;
    for (const auto& [name, selector] : selectors) {
      if (!selector.flexible) continue;
      const auto& preload = node.artifact.preloads[preload_by_register.at(name)];
      const int minimum = preload.retunable_natural_fractional_prefix &&
          preload.retunable_natural_fractional_prefix->starts_with("0.") ? 1 : 0;
      for (const auto& label : selector.labels) {
        auto [bound, new_bound] = projection->target_bounds.emplace(label, std::pair{minimum, 99});
        if (!new_bound) bound->second.first = std::max(bound->second.first, minimum);
      }
    }
  } else if (projection) {
    for (const auto& op : neutral_ops) projection->cell_widths.push_back(passes::cells_per_op(op));
  }
  auto placed = passes::layout_symbolic_regions(
      neutral_ops, fixed, context, 4096, &control_options, true,
      projection ? &*projection : nullptr);
  bool widened_preload_domain = false;
  if (!placed && complete_preloads) {
    // Retain the historical geometry and tie order. Only after it fails,
    // retry with independently certified official BCD preload destinations.
    // A typed runtime charge to the same identity still intersects at 99.
    auto wider = projection.value_or(passes::SymbolicRegionProjection{});
    if (wider.cell_widths.empty())
      for (const auto& op : neutral_ops)
        wider.cell_widths.push_back(passes::cells_per_op(op));
    bool changed = false;
    const int last = official_program_last_address(control_options.address_space_model);
    for (const auto& [name, selector] : selectors) {
      if (!selector.flexible || !preload_by_register.contains(name)) continue;
      const auto& preload = node.artifact.preloads[preload_by_register.at(name)];
      if (!proof_input.rebind_selector(preload,selector.old_target,last))
        continue;
      for (const auto& label : selector.labels) {
        if (decimal_charge_targets.contains(label)) continue;
        const auto found = wider.target_bounds.find(label);
        if (found == wider.target_bounds.end()) {
          wider.target_bounds.emplace(label, std::pair{0, last});
          changed = true;
        } else if (found->second.second < last) {
          found->second.second = last;
          changed = true;
        }
      }
    }
    if (changed) {
      placed = passes::layout_symbolic_regions(
          neutral_ops, fixed, context, 4096, &control_options, true, &wider);
      widened_preload_domain = placed.has_value();
    }
  }
  if (!placed) {
    if(std::getenv("MKPRO_NATIVE_TRACE_COPY_TRANSPORT")) {
      std::cerr<<"[copy-transport-layout] no placement; fixed="<<fixed.size()<<" selectors="<<selectors.size();
      for(const auto& [label,word]:fixed)std::cerr<<" "<<label<<"="<<word;
      std::cerr<<"\n";
    }
    return std::nullopt;
  }
  LayoutSearchArtifact output = node.artifact;
  output.items = lower_ir_to_machine(placed->ops);
  if (widened_preload_domain)
    output.optimizations.push_back({"joint-official-selector-domain",
        "Solved placement with certified BCD address-only preloads through " +
        format_official_address(official_program_last_address(control_options.address_space_model),
                                control_options.address_space_model) +
        "; runtime decimal charges remain bounded by 99 and stock memory is not extended."});
  if (preparation) {
    std::map<std::string, int> encoded_targets;
    for (const auto& [name, selector] : selectors)
      encoded_targets.emplace(name, selector.old_target);
    const auto merged = finalize_shared_helper_dual_mode_layout(
        *preparation, output.items, output.preloads,
        build_post_layout_control_flow(output.items, control_options),
        control_options.address_space_model, &encoded_targets);
    if (!merged.proof.proved || merged.applied <= 0) return std::nullopt;
    output.items = merged.items;
    output.preloads = merged.preloads;
    if (cells(output) != placed->projected_cells ||
        constraint_positions(output.items) !=
            std::optional{placed->projected_targets})
      return std::nullopt;
    for (const auto& report : merged.optimizations)
      output.optimizations.push_back({report.name, report.detail});
    output.optimizations.push_back({"joint-projected-tail-layout",
        "Solved selector bindings and component order in certified final cell widths; "
        "kept neutral command-identity CFG transport separate, then checked the actual "
        "tail rewrite, every projected label, selector data use and hardware return."});
  }
  const auto final_positions = constraint_positions(output.items);
  if (!final_positions)
    return std::nullopt;
  for (const auto& [name, selector] : selectors) {
    if (!selector.flexible || preparation)
      continue;
    std::optional<int> address;
    for (const auto& label : selector.labels) {
      const auto found = final_positions->find(label);
      if (found == final_positions->end() || (address && *address != found->second))
        return std::nullopt;
      address = found->second;
    }
    if (!address)
      return std::nullopt;
    auto& preload = output.preloads[preload_by_register.at(name)];
    const auto value = proof_input.rebind_selector(preload,selector.old_target,*address);
    if (!value)
      return std::nullopt;
    if (preload.value != *value) {
      output.optimizations.push_back({"joint-selector-constraint-synthesis",
          "Solved R" + name + " with placement: " + preload.value + " -> " +
          *value + "; certified all data uses and exact selector writeback."});
      preload.value = *value;
    }
  }
  auto rebound = rebind_layout_search_artifact(std::move(output));
  // This matched, rooted graph proves return depth and continuations including
  // the explicit hardware empty-return contract. Do not follow it with the
  // legacy symbolic walk, which rejects every empty return even under that
  // independently established contract.
  if (!rebound) return std::nullopt;
  const auto final_flow = build_post_layout_control_flow(rebound->items, control_options);
  if (!final_flow.proved || (pending_literal &&
      !passes::prove_deferred_selector_data_literal_layout(
          *pending_literal, rebound->items, final_flow)))
    return std::nullopt;
  if (preparation && (cells(*rebound) != placed->projected_cells ||
      constraint_positions(rebound->items) != std::optional{placed->projected_targets}))
    return std::nullopt;
  if (pending_literal)
    rebound->optimizations.push_back({"joint-data-control-literal-layout",
        "Solved a compiler-owned literal/address equality together with selector allocation "
        "and pending tail widths; removed " + std::to_string(pending_literal->removed_cells) +
        " literal cells and re-proved every final charge, entry, alias and stack/X1/X2 "
        "continuation. The independent historical search retains its complete budget."});
  return rebound;
}

} // namespace

namespace {

enum class ProloguePartitionLane { Disabled, Enabled };

TransactionSearchResult<LayoutSearchArtifact> search_joint_representation_constraints_impl(
    const LayoutSearchArtifact& root, const CompileOptions& options,
    const JointRepresentationFinalizer& finalize,
    const TransactionSearchLimits& limits, bool preload_environment_complete,
    ProloguePartitionLane partition_lane, JointProofPreparationCounts& proof_counts,
    const JointRepresentationFinalizer& finalize_intermediate,
    bool coupled_geometry = false,
    const ConstrainedRepresentation* pending_root = nullptr,
    int comparison_origin_cells = -1) {
  TransactionSearchResult<LayoutSearchArtifact> output;
  const int origin_cells = comparison_origin_cells >= 0 ? comparison_origin_cells : cells(root);
  if (pending_root && (!preload_environment_complete ||
      !same_layout_search_artifact(root, pending_root->artifact) ||
      !detail::joint_target_origins(root.items, pending_root->targets, pending_root->encoded_selectors)))
    return output;
  CompileOptions configured = options;
  configured.defer_return_suffix_until_callee_hole = true;
  configured.coalesce_copies = true;
  const passes::PassContext context{configured};
  PostLayoutControlFlowOptions control_options;
  control_options.address_space_model = address_space_model_for_feature_profile(
      effective_optimizer_feature_profile(options));
  if (preload_environment_complete)
    control_options.empty_return_target = 1;

  auto root_ops = preload_environment_complete ? representation_view(root, options, pending_root != nullptr)
      : std::optional{raise_machine_to_ir(root.items,
                                         effective_optimizer_feature_profile(options))};
  if (!root_ops || !build_post_layout_control_flow(
          lower_ir_to_machine(*root_ops), control_options).proved)
    return output;
  ConstrainedRepresentation initial = pending_root ? *pending_root : ConstrainedRepresentation{root, {}};
  initial.artifact.items = lower_ir_to_machine(*root_ops);
  if (!refresh_constraint_targets(initial))
    return output;
  using Edge = ProvedSearchTransition<ConstrainedRepresentation>;
  const auto expand = [&](const ConstrainedRepresentation& parent) {
    std::vector<Edge> edges;
    const auto ops = raise_machine_to_ir(parent.artifact.items,
                                         effective_optimizer_feature_profile(options));
    const auto add = [&](passes::PassResult proposal, const char* rule,
                         const std::map<std::string, int>* bindings = nullptr,
                         const std::map<std::string, int>* new_selector_origins = nullptr) {
      auto child = apply(parent.artifact, std::move(proposal));
      if (!child)
        return;
      ConstrainedRepresentation next{std::move(*child),
          bindings ? *bindings : parent.targets, parent.encoded_selectors};
      if(new_selector_origins)for(const auto& [reg,origin]:*new_selector_origins)next.encoded_selectors[reg]=origin;
      if (bindings) {
        const auto encoded = detail::transport_joint_selector_word_origins(
            next.artifact.items, next.targets, parent.artifact.preloads,
            next.artifact.preloads, next.encoded_selectors,
            control_options.address_space_model);
        if (!encoded) return;
        next.encoded_selectors = *encoded;
      }
      if (refresh_constraint_targets(next) &&
          (!same_layout_search_artifact(parent.artifact, next.artifact) ||
           parent.targets != next.targets))
        edges.push_back({std::move(next), rule, true});
    };
    const auto data_origins = detail::joint_target_origins(
        parent.artifact.items, parent.targets, parent.encoded_selectors);
    if (data_origins)
    for (auto& proposal : passes::deferred_finite_mode_representation_candidates(
             ops, context, {
                 .maximum_growth_cells = std::min(4, limits.maximum_growth_cells),
                 .maximum_candidates = 6, .allow_target_relayout = true,
                 .runtime_preloads = preload_environment_complete
                     ? &parent.artifact.preloads : nullptr,
                 .preserve_compiler_selector_charges = preload_environment_complete,
                 .assumed_target_addresses = &*data_origins,
                 .normalize_unit_arguments = coupled_geometry,
                 .precise_entry_facts = coupled_geometry && preload_environment_complete}))
      add(std::move(proposal.transformation), "representation-equations",
          &proposal.required_target_addresses);

    // A value rewrite can change the cheapest helper ABI, and moving that ABI
    // changes definition webs again. These are graph edges, not a one-way
    // pipeline: retain their unresolved command identities until placement.
    if (coupled_geometry && preload_environment_complete && data_origins) {
      for (auto& transported : passes::contextual_copy_transport_candidates(
               ops, context, {.maximum_work=200000,.maximum_candidates=2,
                   .maximum_copy_sites=8,.maximum_growth_cells=std::min(4,limits.maximum_growth_cells),
                   .control=&control_options,.encoded_target_origins=&*data_origins}))
        add(std::move(transported.transformation), "coupled-contextual-copy-transport");
      for (auto& redundant : passes::contextual_redundant_copy_store_candidates(
               ops, context, {.maximum_work=200000,.maximum_candidates=2,
                   .maximum_copy_sites=8,.maximum_growth_cells=std::min(4,limits.maximum_growth_cells),
                   .control=&control_options,.encoded_target_origins=&*data_origins,
                   .runtime_preloads=&parent.artifact.preloads}))
        add(std::move(redundant.transformation), "coupled-contextual-word-store");
      for(auto& completed:passes::contextual_pure_return_completion_candidates(ops,context,
          {.maximum_work=200000,.maximum_candidates=2,.maximum_copy_sites=8,.control=&control_options,
           .encoded_target_origins=&*data_origins,.runtime_preloads=&parent.artifact.preloads}))
        add(std::move(completed.transformation),"coupled-pure-return-completion");
      for(auto& returned:passes::contextual_finite_return_selector_candidates(ops,context,
          {.maximum_work=100000,.maximum_candidates=2,.control=&control_options,
           .encoded_target_origins=&*data_origins,.runtime_preloads=&parent.artifact.preloads}))
        add(std::move(returned.transformation),"coupled-finite-return-selector",nullptr,&returned.selector_origins);
      for (auto& codec : passes::deferred_finite_mode_representation_candidates(
               ops,context,{.maximum_growth_cells=std::min(4,limits.maximum_growth_cells),
                   .maximum_candidates=6,.allow_target_relayout=true,
                   .runtime_preloads=&parent.artifact.preloads,
                   .preserve_compiler_selector_charges=true,
                   .assumed_target_addresses=&*data_origins,
                   .encode_preloaded_state=true,.precise_entry_facts=true})) {
        if(codec.preload_replacements.empty()) continue;
        auto encoded_artifact=apply(parent.artifact,std::move(codec.transformation));
        if(!encoded_artifact) continue;
        bool owned=true;
        for(const auto& replacement:codec.preload_replacements){
          auto owner=std::find_if(encoded_artifact->preloads.begin(),encoded_artifact->preloads.end(),
              [&](const auto& word){return word.register_name==replacement.before.register_name;});
          if(owner==encoded_artifact->preloads.end() ||
              replacement.before.register_name!=replacement.after.register_name ||
              !same_layout_search_artifact({.preloads={*owner}},{.preloads={replacement.before}})){
            owned=false;break;
          }
          *owner=replacement.after;
        }
        if(!owned) continue;
        ConstrainedRepresentation encoded_node{std::move(*encoded_artifact),
            codec.required_target_addresses,parent.encoded_selectors};
        const auto encoded_origins=detail::transport_joint_selector_word_origins(
            encoded_node.artifact.items,encoded_node.targets,parent.artifact.preloads,
            encoded_node.artifact.preloads,encoded_node.encoded_selectors,
            control_options.address_space_model);
        if(!encoded_origins) continue;
        encoded_node.encoded_selectors=*encoded_origins;
        if(refresh_constraint_targets(encoded_node) &&
            !same_layout_search_artifact(parent.artifact,encoded_node.artifact))
          edges.push_back({std::move(encoded_node),"coupled-preloaded-carrier-equations",true});
      }
      for(auto& pooled_node:pool_allocation_exposed_literals(parent,context,control_options))
        edges.push_back({std::move(pooled_node),"coupled-allocation-exposed-literal",true});
      add(passes::deferred_register_web_copy_coalesce(
              ops, context, control_options, *data_origins, parent.artifact.preloads),
          "coupled-epoch-copy-coalescing");
      for (auto& entry_proposal : passes::deferred_idempotent_entry_prologue_candidates(
               ops, parent.artifact.preloads, control_options, *data_origins))
        add(std::move(entry_proposal), "coupled-entry-abi");
      const auto abi_flow = build_post_layout_control_flow(parent.artifact.items, control_options);
      if (abi_flow.proved) {
        HelperInvariantRecallHoistOptions abi_options;
        abi_options.complete_program_control = control_options;
        abi_options.deferred_target_origins = *data_origins;
        abi_options.allow_x_preserving_root = true;
        abi_options.allow_swapped_return = true;
        abi_options.allow_callsite_operand_swap = true;
        abi_options.allow_size_neutral = true;
        for (const auto& [site, destinations] : abi_flow.indirect_flow_targets)
          for (const auto& destination : destinations)
            abi_options.proved_indirect_flow_targets[site].push_back(destination.address);
        for (const bool at_return : {false, true}) {
          abi_options.prefer_before_return_plan = at_return;
          for (auto& abi_candidate : helper_invariant_recall_hoist_candidates(
                   parent.artifact.items, abi_options, 4)) {
          if (abi_candidate.applied <= 0 || !abi_candidate.proof.proved ||
              !abi_candidate.proof.final_artifact_proved) continue;
          passes::PassResult abi_proposal;
          abi_proposal.ops = raise_machine_to_ir(abi_candidate.items,
              effective_optimizer_feature_profile(options));
          abi_proposal.applied = abi_candidate.applied;
          for (const auto& report : abi_candidate.optimizations)
            abi_proposal.optimizations.push_back({report.name, report.detail});
          add(std::move(abi_proposal), "coupled-helper-operand-abi");
          }
        }
      }
    }

    auto requests = passes::semantic_region_register_requests(ops, true);
    if (coupled_geometry) {
      const auto processor_requests = passes::common_processor_factoring_register_requests(ops);
      requests.insert(requests.end(), processor_requests.begin(), processor_requests.end());
      const auto entry_requests = passes::idempotent_entry_prologue_register_requests(ops);
      requests.insert(requests.end(), entry_requests.begin(), entry_requests.end());
      // A transported scalar may now occupy a cursor/FL epoch that was live
      // in the old helper. These are bounded equality requests, not liveness
      // assumptions; the complete graph and list-color solver prove each one.
      std::size_t cursor_requests=0;
      for(std::size_t i=1;i<ops.size() && cursor_requests<12U;++i){
        if(ops[i].kind!=IrKind::Store ||
            (ops[i-1U].kind!=IrKind::Recall && !ops[i].meta.manual_interaction)) continue;
        std::set<std::string> cursor_names;
        for(std::size_t at=0;at<ops.size() && cursor_requests<12U;++at){
          const auto& cursor=ops[at];
          if(cursor.kind!=IrKind::Loop && cursor.kind!=IrKind::IndirectRecall &&
              cursor.kind!=IrKind::IndirectStore) continue;
          if(cursor.kind!=IrKind::Loop){
            const auto reg=detail::joint_origin_register(cursor.register_name);
            if(!reg || *reg>3) continue;
          }
          const auto name=cursor.kind==IrKind::Loop?cursor.counter:cursor.register_name;
          if(!cursor_names.insert(name).second) continue;
          requests.push_back({{i,at}});++cursor_requests;
        }
      }
    }
    const auto tail_requests = passes::semantic_region_tail_register_requests(ops, true);
    requests.insert(requests.end(),tail_requests.begin(),tail_requests.end());
    if (data_origins)
    passes::for_each_deferred_register_web_recoloring(ops, context, requests, control_options, *data_origins,
        [&](std::vector<IrOp> recolored) {
          add({.ops = std::move(recolored), .applied = 1,
               .optimizations = {{"joint-register-web-equations",
                   "Solved interference-checked operand equalities without committing "
                   "a representation or physical layout winner."}}},
              "register-web-equations");
        }, coupled_geometry && preload_environment_complete ? &parent.artifact.preloads : nullptr,
        coupled_geometry && preload_environment_complete);
    add(passes::semantic_region_folding_with_deferred_layout(
            ops, context, control_options), "region-equations");
    if (coupled_geometry)
      for (auto& exchanged : passes::conditional_region_exchange_candidates(
               ops, context, control_options, 2))
        add(std::move(exchanged), "coupled-conditional-region-exchange");
    if (preload_environment_complete && limits.maximum_growth_cells > 0)
      for (auto& proposal : passes::stable_target_split_bridge_candidates(
               ops, parent.artifact.preloads, parent.targets, context, control_options))
        add(std::move(proposal), "stable-target-region-split");
    if (preload_environment_complete)
      for (auto& next : literal_selector_equations(parent, control_options, 6))
        edges.push_back({std::move(next), "literal-selector-equations", true});
    return edges;
  };

  // Initial rule families receive independent expansion lanes. A smaller
  // encoding cannot starve the equal-size allocator that enables its sharing.
  auto seeds = expand(initial);
  std::vector<ConstrainedRepresentation> allocation_roots{initial};
  // Range certificates are obtained only from the complete original artifact,
  // never from a descendant whose selector equations are still unresolved.
  if (preload_environment_complete) {
    for (auto& proposal : passes::common_processor_factoring_candidates(
             *root_ops, context, {.runtime_preloads = &root.preloads})) {
      auto child = apply(initial.artifact, std::move(proposal));
      if (!child) continue;
      ConstrainedRepresentation next{std::move(*child), initial.targets};
      if (refresh_constraint_targets(next)) {
        allocation_roots.push_back(next);
        seeds.push_back({std::move(next), "finite-domain-common-processor", true});
      }
    }
  }
  // Keep the established search independent: neutral producer placement must
  // not consume its beam or replace its equal-size incumbent.
  std::vector<Edge> projection_seeds;
  const auto original_allocation_roots = allocation_roots;
  if (limits.maximum_expansions > 0 && limits.maximum_candidates > 0 &&
      limits.maximum_depth > 0) {
    auto seed_options = configured;
    seed_options.allow_size_neutral_selector_seed_reuse = true;
    const passes::PassContext seed_context{seed_options};
    for (const auto& source : original_allocation_roots) {
      auto proposal = passes::deferred_indirect_selector_seed_reuse(
          raise_machine_to_ir(source.artifact.items,
              effective_optimizer_feature_profile(options)),
          seed_context, control_options, source.targets);
      auto child = apply(source.artifact, std::move(proposal.transformation));
      if (!child || !transaction_growth_fits(
              origin_cells, cells(*child), limits.maximum_growth_cells))
        continue;
      ConstrainedRepresentation next{
          std::move(*child), std::move(proposal.encoded_target_origins),
          source.encoded_selectors};
      if (refresh_constraint_targets(next)) {
        allocation_roots.push_back(next);
        projection_seeds.push_back({std::move(next), "selector-projection-equations", true});
      }
    }
  }
  // Neutral changes of fallthrough boundaries get their own bounded lane.
  // Append allocation roots only after every established root, so an enabling
  // polarity choice cannot steal the old search's budget or equal-size winner.
  const auto unrotated_allocation_roots = allocation_roots;
  std::vector<Edge> topology_seeds;
  const auto topology_edges = [&](const ConstrainedRepresentation& parent) {
    std::vector<Edge> edges;
    for (auto& proposal : passes::conditional_region_exchange_candidates(
             raise_machine_to_ir(parent.artifact.items,
                 effective_optimizer_feature_profile(options)),
             context, control_options, 2)) {
      auto child = apply(parent.artifact, std::move(proposal));
      if (!child || !transaction_growth_fits(
              origin_cells, cells(*child), limits.maximum_growth_cells))
        continue;
      ConstrainedRepresentation next{
          std::move(*child), parent.targets, parent.encoded_selectors};
      if (refresh_constraint_targets(next))
        edges.push_back({std::move(next), "conditional-region-exchange", true});
    }
    return edges;
  };
  if (limits.maximum_expansions > 0 && limits.maximum_candidates > 0 &&
      limits.maximum_depth > 0) {
    for (const auto& source : unrotated_allocation_roots) {
      for (auto& edge : topology_edges(source)) {
        if (topology_seeds.size() == 4U) break;
        allocation_roots.push_back(edge.node);
        topology_seeds.push_back(std::move(edge));
      }
      if (topology_seeds.size() == 4U) break;
    }
  }
  if (coupled_geometry) {
  std::vector<std::vector<ConstrainedRepresentation>> product_queues;
  const auto coupled_root_limit = std::min<std::size_t>(6, limits.maximum_expansions);
  for(const auto& source:unrotated_allocation_roots) {
    std::vector<ConstrainedRepresentation> queue;
    for(auto& edge:expand(source)) {
      if (edge.rule != "representation-equations" ||
          std::none_of(edge.node.artifact.items.begin(), edge.node.artifact.items.end(),
              [](const auto& item) {
                return std::find(item.roles.begin(), item.roles.end(),
                                 kUnitProjectionDemandRole) != item.roles.end();
              })) continue;
      if(auto hoisted=apply(edge.node.artifact,passes::distribute_constant_call_prefix(
          raise_machine_to_ir(edge.node.artifact.items,effective_optimizer_feature_profile(options)),context,control_options))) {
        ConstrainedRepresentation distributed{std::move(*hoisted),edge.node.targets,edge.node.encoded_selectors};
        if(refresh_constraint_targets(distributed)) queue.push_back(std::move(distributed));
      }
      queue.push_back(edge.node);
      auto seed_options=configured;seed_options.allow_size_neutral_selector_seed_reuse=true;
      const passes::PassContext seed_context{seed_options};
      auto seed=passes::deferred_indirect_selector_seed_reuse(
          raise_machine_to_ir(edge.node.artifact.items,effective_optimizer_feature_profile(options)),
          seed_context,control_options,edge.node.targets);
      if(auto child=apply(edge.node.artifact,std::move(seed.transformation))) {
        ConstrainedRepresentation seeded{std::move(*child),std::move(seed.encoded_target_origins),edge.node.encoded_selectors};
        if(refresh_constraint_targets(seeded)) queue.push_back(std::move(seeded));
      }
    }
    product_queues.push_back(std::move(queue));
  }
  std::vector<ConstrainedRepresentation> coupled_roots;
  for(std::size_t depth=0;coupled_roots.size()<coupled_root_limit;++depth) {
    bool found=false;
    for(auto& queue:product_queues) if(depth<queue.size()) {
      found=true;coupled_roots.push_back(std::move(queue[depth]));
      if(coupled_roots.size()==coupled_root_limit) break;
    }
    if(!found) break;
  }
  // These constant-prefix roots are additional enabling alternatives, not
  // an admission requirement for every other value/lifetime/ABI composition.
  // Keep their historical order and budget without discarding ordinary seeds.
  allocation_roots.insert(allocation_roots.begin(),coupled_roots.begin(),coupled_roots.end());
  }
  // Recompute web interference after representation and whole-family choices.
  // Keep the original closure first: a local copy saving must not replace a
  // neutral variant whose geometry wins only after shared-tail projection.
  // Selector digits still belong to their encoded carrier, not its provisional
  // destination. Conflicting origins cannot certify a pruned execution graph.
  const auto coalesced_variants = [&](const ConstrainedRepresentation& node) {
    std::vector<ConstrainedRepresentation> variants{node};
    if (!preload_environment_complete) return variants;
    const auto origins = detail::joint_target_origins(
        node.artifact.items, node.targets, node.encoded_selectors);
    if (!origins) return variants;
    auto copied = passes::deferred_register_web_copy_coalesce(
        raise_machine_to_ir(node.artifact.items,
            effective_optimizer_feature_profile(options)),
        context, control_options, *origins);
    if (auto artifact = apply(node.artifact, std::move(copied))) {
      ConstrainedRepresentation alternative{
          std::move(*artifact), node.targets, node.encoded_selectors};
      if (refresh_constraint_targets(alternative))
        variants.push_back(std::move(alternative));
    }
    // Erasure changes the geometry that allocation, numeric literals and
    // projected tails solve together. Preserve every historical alternative
    // first, then bounded deletion subsets instead of freezing a greedy DCE
    // winner or waiting until the complete layout has already been selected.
    if (limits.maximum_expansions > 0 && limits.maximum_candidates > 0 &&
        limits.maximum_depth > 0) {
      const auto bases = variants;
      for (const auto& base : bases)
        for (auto& proposal : passes::deferred_stack_producer_erasure_candidates(
                 raise_machine_to_ir(base.artifact.items,
                     effective_optimizer_feature_profile(options)),
                 context, control_options, *origins,
                 {.maximum_candidates = std::min<std::size_t>(6, limits.maximum_candidates),
                  .maximum_erased_cells = std::min<std::size_t>(2, limits.maximum_depth)})) {
          auto artifact = apply(base.artifact, std::move(proposal));
          if (!artifact) continue;
          ConstrainedRepresentation alternative{
              std::move(*artifact), base.targets, base.encoded_selectors};
          if (refresh_constraint_targets(alternative))
            variants.push_back(std::move(alternative));
        }
    }
    if (coupled_geometry) {
    const auto seed_bases=variants;
    auto seed_options=configured; seed_options.allow_size_neutral_selector_seed_reuse=true;
    const passes::PassContext seed_context{seed_options};
    for(const auto& base:seed_bases) {
      const auto seed_origins=detail::joint_target_origins(base.artifact.items,base.targets,base.encoded_selectors);
      if(!seed_origins) continue;
      auto seed=passes::deferred_indirect_selector_seed_reuse(raise_machine_to_ir(base.artifact.items,effective_optimizer_feature_profile(options)),seed_context,control_options,*seed_origins);
      auto child=apply(base.artifact,std::move(seed.transformation));
      if(!child) continue;
      ConstrainedRepresentation seeded{std::move(*child),std::move(seed.encoded_target_origins),base.encoded_selectors};
      if(!refresh_constraint_targets(seeded)) continue;
      variants.push_back(seeded);
      const auto next_origins=detail::joint_target_origins(seeded.artifact.items,seeded.targets,seeded.encoded_selectors);
      if(!next_origins) continue;
      for(auto& erased:passes::deferred_stack_producer_erasure_candidates(raise_machine_to_ir(seeded.artifact.items,effective_optimizer_feature_profile(options)),context,control_options,*next_origins,{.maximum_candidates=2,.maximum_erased_cells=2,.maximum_proof_attempts=16})) {
        auto erased_child=apply(seeded.artifact,std::move(erased)); if(!erased_child) continue;
        ConstrainedRepresentation after{std::move(*erased_child),seeded.targets,seeded.encoded_selectors};
        if(refresh_constraint_targets(after)) variants.push_back(std::move(after));
      }
    }
    }
    return variants;
  };

  // Conditional data/control rewrites must not alter the historical beam,
  // pruning incumbent or equal-size winner. Evaluate them in a separate
  // closure lane, borrowing exactly the already-admitted representation and
  // allocation states. No larger search limits or source-specific seeds.
  std::optional<LayoutSearchArtifact> data_control_incumbent;
  const auto close_data_control_literals = [&](const ConstrainedRepresentation& node) {
    if (!preload_environment_complete || limits.maximum_expansions == 0 ||
        limits.maximum_candidates == 0 || limits.maximum_depth == 0)
      return;
    for (const auto& pending : passes::prepare_deferred_selector_data_literal_layouts(
             node.artifact.items, control_options,
             std::min<std::size_t>(4, limits.maximum_candidates))) {
      PreparedJointProofInput proof_input(pending.items,node.artifact.preloads,
                                         control_options,&proof_counts);
      for (const bool projected : {false, true}) {
        auto closed = close_representation_constraints(
            node, context, control_options, true, projected, &pending, proof_input, pending_root != nullptr);
        if (!closed) continue;
        // A temporarily larger placement can still repay its bounded debt in
        // finalization. Only the complete artifact competes with incumbents.
        if (finalize) closed = finalize(*closed);
        if (closed) {
          const auto delivered_flow=build_post_layout_control_flow(closed->items,control_options);
          if (!literal_family_owns_charges(pending,closed->items) ||
              !delivered_flow.proved || !passes::prove_deferred_selector_data_literal_layout(
                  pending,closed->items,delivered_flow)) closed.reset();
        }
        if (closed && cells(*closed) < origin_cells &&
            (!data_control_incumbent || cells(*closed) < cells(*data_control_incumbent)))
          data_control_incumbent = std::move(*closed);
      }
    }
  };

  const auto retain_closed_continuation = [&](const LayoutSearchArtifact& artifact,
                                              std::vector<ConstrainedRepresentation>* continuations) {
    if (!continuations) return;
    ConstrainedRepresentation continuation{artifact, {}};
    if (!refresh_constraint_targets(continuation)) return;
    // Reopen only a target-closed, rebound transaction. Conditional selector
    // origins must never be inferred from provisional label geometry.
    if (auto view = representation_view(continuation.artifact, options)) {
      continuation.artifact.items = lower_ir_to_machine(*view);
      if (!refresh_constraint_targets(continuation) ||
          !build_post_layout_control_flow(continuation.artifact.items, control_options).proved)
        return;
      continuation.artifact.optimizations.push_back({
          "joint-proof-closed-continuation",
          "Reopened a target-closed transaction as symbolic command identities; "
          "retired only obsolete cell-origin bookkeeping and retained all "
          "selector, input, data and return obligations. Publication still "
          "requires the caller's independent complete-artifact gate."});
    }
    if (std::none_of(continuations->begin(), continuations->end(),
        [&](const auto& old) {
          return old.targets == continuation.targets &&
              old.encoded_selectors == continuation.encoded_selectors &&
              same_layout_search_artifact(old.artifact, continuation.artifact);
        }))
      continuations->push_back(std::move(continuation));
  };

  const auto close_node = [&](const ConstrainedRepresentation& node, const std::vector<std::string>& path, bool composition = false,
                              std::vector<ConstrainedRepresentation>* continuations = nullptr)
          -> std::optional<ConstrainedRepresentation> {
        std::optional<ConstrainedRepresentation> best;
        // Each closure owns its downstream finalization. A smaller intermediate
        // must not hide a different placement that wins only after composition.
        const auto variants = coalesced_variants(node);
        for (const auto& variant : variants) {
          PreparedJointProofInput proof_input(variant.artifact.items,variant.artifact.preloads,
                                             control_options,&proof_counts);
          for (const bool projected : {false, true}) {
          auto closed = close_representation_constraints(
              variant, context, control_options, preload_environment_complete, projected, nullptr, proof_input, pending_root != nullptr);
          if (!closed) continue;
          std::string chain;
          for (const auto& rule : path) {
            if (!chain.empty()) chain += " + ";
            chain += rule;
          }
          closed->optimizations.push_back({"joint-representation-constraint-search",
              "Closed representation, register and target equations together (" + chain +
              "); every immutable binding, flexible preload and matched return is "
              "certified before final size comparison."});
          // The finalizer can greedily lower away an otherwise useful seam.
          // Keep its proof-closed input as well as its output. This is an
          // exploration edge only; no incumbent bypasses final publication.
          if (composition) retain_closed_continuation(*closed, continuations);
          if (composition && finalize_intermediate) closed = finalize_intermediate(*closed);
          else if (finalize) closed = finalize(*closed);
          if (!closed) continue;
          ConstrainedRepresentation result{std::move(*closed), {}};
          if (!refresh_constraint_targets(result)) continue;
          retain_closed_continuation(result.artifact, continuations);
          if (!best || cells(result.artifact) < cells(best->artifact))
            best = std::move(result);
          }
        }
        for (const auto& variant : variants) close_data_control_literals(variant);
        return best;
      };
  auto composition_seeds = seeds;
  for (const auto& edge : projection_seeds) composition_seeds.push_back(edge);
  for (const auto& edge : topology_seeds) composition_seeds.push_back(edge);
  const auto search = search_proved_transactions(
      std::move(seeds), std::vector<ConstrainedRepresentation>{initial}, origin_cells,
      limits, [](const auto& node) { return cells(node.artifact); },
      [](const auto& a, const auto& b) {
        return a.targets == b.targets && a.encoded_selectors == b.encoded_selectors &&
            same_layout_search_artifact(a.artifact, b.artifact);
      }, expand,
      close_node);
  if (search.incumbent)
    output.incumbent = search.incumbent->artifact;
  output.expanded = search.expanded;
  output.admitted = search.admitted;
  output.duplicate = search.duplicate;
  output.rejected = search.rejected;
  output.budget_exhausted = search.budget_exhausted;

  // The historical topology prefix can fill up before reaching the projected
  // representation roots. Preserve that prefix and its tie order, but do not
  // leave allocation slots unused: compose the remaining proved alternatives
  // fairly across parents. In particular, a neutral producer choice and a
  // neutral polarity choice must be allowed to meet the same allocation/tail
  // closure, even when neither improves an independent greedy incumbent.
  if (preload_environment_complete && topology_seeds.size() == 4U &&
      allocation_roots.size() < limits.maximum_expansions) {
    auto product_limits = limits;
    product_limits.maximum_candidates = std::min(
        limits.maximum_candidates, limits.maximum_expansions - allocation_roots.size());
    auto product = collect_proved_product_frontier(
        unrotated_allocation_roots, allocation_roots, origin_cells, product_limits,
        [](const auto& node) { return cells(node.artifact); },
        [](const auto& a, const auto& b) {
          return a.targets == b.targets && a.encoded_selectors == b.encoded_selectors &&
              same_layout_search_artifact(a.artifact, b.artifact);
        }, topology_edges);
    output.expanded += product.expanded;
    output.admitted += product.candidates.size();
    output.duplicate += product.duplicate;
    output.rejected += product.rejected;
    output.budget_exhausted = output.budget_exhausted || product.budget_exhausted;
    for (auto& edge : product.candidates) {
      edge.node.artifact.optimizations.push_back({"joint-representation-product-frontier",
          "Retained a proved cross-representation continuation in an unused allocation "
          "slot; selector projection, region polarity and whole-family allocation "
          "remain subject to one final layout/return-state proof."});
      allocation_roots.push_back(std::move(edge.node));
    }
  }

  // Keep the historical search and its incumbent independent. Within this
  // separate bounded lane an allocation, optional fallthrough cut and pending
  // tail sharing close together. No eager natural-target layout is frozen
  // before another rewrite can repay a released family's extra operands.
  if (preload_environment_complete && limits.maximum_expansions > 0 &&
      limits.maximum_depth > 0 && limits.maximum_candidates > 0) {
    const std::size_t cap = coupled_geometry
        ? std::min<std::size_t>(128, std::min<std::size_t>(128, limits.maximum_candidates) *
                                      std::min<std::size_t>(128, limits.maximum_expansions))
        : std::min<std::size_t>(24, limits.maximum_candidates);
    std::size_t roots = 0;
    for (const auto& source : allocation_roots) {
      if (coupled_geometry && std::none_of(
              source.artifact.items.begin(), source.artifact.items.end(), [](const auto& item) {
                return std::find(item.roles.begin(), item.roles.end(),
                                 kTransportedConstantPrefixRole) != item.roles.end();
              }))
        continue;
      if (roots++ >= limits.maximum_expansions) break;
      const auto source_flow = build_post_layout_control_flow(source.artifact.items, control_options);
      std::map<std::string, int> encoded = source.encoded_selectors;
      bool consistent = source_flow.proved;
      for (const auto& [command, targets] : source_flow.indirect_flow_targets) {
        const auto& item = source.artifact.items[command];
        if (charged_constraint_consumer(item)) continue;
        const int reg = item.opcode & 15;
        if (reg < 7 || reg > 14 || !item.indirect_flow_targets) continue;
        const std::string name(1, "0123456789abcde"[reg]);
        for (const auto& target : *item.indirect_flow_targets) {
          const auto* label = std::get_if<std::string>(&target);
          if (!label || !source.targets.contains(*label)) {
            consistent = false;
            break;
          }
          const auto [old, inserted] = encoded.emplace(name, source.targets.at(*label));
          if (!inserted && old->second != source.targets.at(*label)) consistent = false;
        }
        (void)targets;
      }
      if (!consistent) continue;
      const auto allocate = coupled_geometry
          ? balanced_deferred_natural_target_selector_allocations
          : deferred_natural_target_selector_allocations;
      const auto allocations = allocate(
          source.artifact.items, source.artifact.preloads, source_flow, encoded,
          control_options, {.maximum_assignments = 128, .maximum_candidates = cap,
                            .maximum_growth_cells = limits.maximum_growth_cells});
      if (allocations.empty()) continue;
      ++output.expanded;
      for (const auto& allocation : allocations) {
        ConstrainedRepresentation allocated = source;
        allocated.artifact.items = allocation.items;
        // Factoring may already have borrowed cells. Reallocation cannot
        // reset that debt by measuring only its local delta from source.
        if (!transaction_growth_fits(
                origin_cells, cells(allocated.artifact), limits.maximum_growth_cells)) {
          ++output.rejected;
          continue;
        }
        allocated.encoded_selectors = encoded;
        for (const auto& [reg, origin] : allocation.encoded_selector_targets)
          allocated.encoded_selectors[reg] = origin;
        if (!refresh_constraint_targets(allocated)) continue;
        ++output.admitted;
        allocated.artifact.optimizations.push_back({"joint-selector-family-allocation",
            "Assigned whole stable flow families and direct-flow groups together (" +
            std::to_string(allocation.changed_flows) + " commands, deferred cell delta " +
            std::to_string(allocation.cell_delta) +
            "); preserved carrier words, all data aliases, stack/X2 and return identities."});
        std::vector<ConstrainedRepresentation> variants{allocated};
        if (limits.maximum_growth_cells > 0) {
          auto bridge_bindings = allocated.targets;
          bool consistent_bridges = true;
          std::map<std::string, int> new_bindings;
          for (const auto& item : allocated.artifact.items) {
            if (!item.indirect_flow_targets || charged_constraint_consumer(item)) continue;
            const int reg = item.opcode & 15;
            if (reg < 7 || reg > 14) continue;
            const std::string name(1, "0123456789abcde"[reg]);
            if (!allocated.encoded_selectors.contains(name)) continue;
            for (const auto& target : *item.indirect_flow_targets) {
              const auto* label = std::get_if<std::string>(&target);
              if (!label) { consistent_bridges = false; break; }
              const int origin = allocated.encoded_selectors.at(name);
              const auto [old, inserted] = new_bindings.emplace(*label, origin);
              if (!inserted && old->second != origin) consistent_bridges = false;
            }
          }
          for (const auto& [label, origin] : new_bindings) bridge_bindings[label] = origin;
          if (consistent_bridges)
            for (auto& bridge : passes::stable_target_split_bridge_candidates(
                     raise_machine_to_ir(allocated.artifact.items,
                         effective_optimizer_feature_profile(options)),
                     allocated.artifact.preloads, bridge_bindings, context, control_options)) {
              auto artifact = apply(allocated.artifact, std::move(bridge));
              if (!artifact || !transaction_growth_fits(
                      origin_cells, cells(*artifact), limits.maximum_growth_cells))
                continue;
              ConstrainedRepresentation split{std::move(*artifact), allocated.targets,
                                               allocated.encoded_selectors};
              if (refresh_constraint_targets(split)) variants.push_back(std::move(split));
            }
        }
        const auto uncoalesced = variants;
        for (const auto& source_variant : uncoalesced) {
          auto alternatives = coalesced_variants(source_variant);
          for (std::size_t i = 1; i < alternatives.size(); ++i)
            variants.push_back(std::move(alternatives[i]));
        }
        if (coupled_geometry) {
        auto geometry_bases=variants;
        std::stable_sort(geometry_bases.begin(),geometry_bases.end(),[](const auto& a,const auto& b){return cells(a.artifact)<cells(b.artifact);});
        std::size_t geometry_count=0;
        for(const auto& base:geometry_bases) {
          if(geometry_count>=4U) break;
          for(auto& edge:topology_edges(base)) {
            if(geometry_count++>=4U) break;
            variants.push_back(edge.node);
            const auto origins=detail::joint_target_origins(edge.node.artifact.items,edge.node.targets,edge.node.encoded_selectors);
            if(!origins)continue;
            for(auto& bridge:passes::deferred_stable_target_split_bridge_candidates(raise_machine_to_ir(edge.node.artifact.items,effective_optimizer_feature_profile(options)),edge.node.artifact.preloads,*origins,context,control_options,2)) {
              if(auto child=apply(edge.node.artifact,std::move(bridge))) {
                ConstrainedRepresentation next{std::move(*child),edge.node.targets,edge.node.encoded_selectors};
                if(refresh_constraint_targets(next)) variants.push_back(std::move(next));
              }
            }
          }
        }
        }
        for (const auto& variant : variants) {
          PreparedJointProofInput proof_input(variant.artifact.items,variant.artifact.preloads,
                                             control_options,&proof_counts);
          for (bool projected : {false, true}) {
            auto closed = close_representation_constraints(
                variant, context, control_options, true, projected, nullptr, proof_input, pending_root != nullptr);
            // Geometry alone is not a saving. Avoid running the full compiler
            // finalizer on an unpaid release/bridge, even if its CFG is valid.
            if (!closed || cells(*closed) >= origin_cells) continue;
            closed->optimizations.push_back({"joint-selector-allocation-layout",
                "Closed complete-family allocation, region geometry and pending continuation "
                "sharing atomically; independently checked delivered selectors and callbacks."});
            if (finalize) closed = finalize(*closed);
            if (closed && cells(*closed) < origin_cells &&
                (!output.incumbent || cells(*closed) < cells(*output.incumbent)))
              output.incumbent = std::move(*closed);
          }
        }
        for (const auto& variant : variants) close_data_control_literals(variant);
      }
    }
  }
  // The independent invocation already owns the individual historical
  // lanes. The coupled invocation skips those duplicates, but must continue
  // into the full allocation/closure composition below: an early return here
  // made value/ABI descendants unable to renegotiate their selector families.
  if (!coupled_geometry && !projection_seeds.empty()) {
    const auto projection_search = search_proved_transactions(
        std::move(projection_seeds), original_allocation_roots, origin_cells,
        limits, [](const auto& node) { return cells(node.artifact); },
        [](const auto& a, const auto& b) {
          return a.targets == b.targets && a.encoded_selectors == b.encoded_selectors &&
              same_layout_search_artifact(a.artifact, b.artifact);
        }, expand, close_node);
    if (projection_search.incumbent &&
        (!output.incumbent ||
         cells(projection_search.incumbent->artifact) < cells(*output.incumbent)))
      output.incumbent = projection_search.incumbent->artifact;
    output.expanded += projection_search.expanded;
    output.admitted += projection_search.admitted;
    output.duplicate += projection_search.duplicate;
    output.rejected += projection_search.rejected;
    output.budget_exhausted = output.budget_exhausted || projection_search.budget_exhausted;
  }
  if (!coupled_geometry && !topology_seeds.empty()) {
    auto topology_limits = limits;
    topology_limits.maximum_expansions = std::min<std::size_t>(4, limits.maximum_expansions);
    topology_limits.maximum_candidates = std::min<std::size_t>(24, limits.maximum_candidates);
    topology_limits.maximum_depth = std::min<std::size_t>(3, limits.maximum_depth);
    const auto topology_search = search_proved_transactions(
        std::move(topology_seeds), unrotated_allocation_roots, origin_cells,
        topology_limits, [](const auto& node) { return cells(node.artifact); },
        [](const auto& a, const auto& b) {
          return a.targets == b.targets && a.encoded_selectors == b.encoded_selectors &&
              same_layout_search_artifact(a.artifact, b.artifact);
        }, [&](const ConstrainedRepresentation& parent) {
          auto edges = expand(parent);
          for (auto& edge : topology_edges(parent)) edges.push_back(std::move(edge));
          return edges;
        }, close_node);
    if (topology_search.incumbent &&
        (!output.incumbent ||
         cells(topology_search.incumbent->artifact) < cells(*output.incumbent)))
      output.incumbent = topology_search.incumbent->artifact;
    output.expanded += topology_search.expanded;
    output.admitted += topology_search.admitted;
    output.duplicate += topology_search.duplicate;
    output.rejected += topology_search.rejected;
    output.budget_exhausted = output.budget_exhausted || topology_search.budget_exhausted;
  }

  // Complete every historical lane before composing semantic choices with
  // whole-family allocation. A closed neutral artifact is an exploration
  // state, not a publishable winner. Neither lane can spend the other's beam.
  if (preload_environment_complete && limits.maximum_expansions &&
      limits.maximum_candidates && limits.maximum_depth > 1) {
    const auto allocation_edges = [&](const ConstrainedRepresentation& source) {
      std::vector<Edge> edges;
      const auto allocation_growth = transaction_remaining_growth_budget(
          origin_cells, cells(source.artifact), limits.maximum_growth_cells);
      if (!allocation_growth) return edges;
      const auto source_flow = build_post_layout_control_flow(source.artifact.items, control_options);
      std::map<std::string, int> encoded = source.encoded_selectors;
      bool consistent = source_flow.proved;
      for (const auto& [command, targets] : source_flow.indirect_flow_targets) {
        const auto& item = source.artifact.items[command];
        if (charged_constraint_consumer(item)) continue;
        const int reg = item.opcode & 15;
        if (reg < 7 || reg > 14 || !item.indirect_flow_targets) continue;
        const std::string name(1, "0123456789abcde"[reg]);
        for (const auto& target : *item.indirect_flow_targets) {
          const auto* label = std::get_if<std::string>(&target);
          if (!label || !source.targets.contains(*label)) {
            consistent = false;
            break;
          }
          const auto [old, inserted] = encoded.emplace(name, source.targets.at(*label));
          if (!inserted && old->second != source.targets.at(*label)) consistent = false;
        }
        (void)targets;
      }
      if (!consistent) return edges;
      const auto allocations = deferred_natural_target_selector_allocations(
          source.artifact.items, source.artifact.preloads, source_flow, encoded,
          control_options, {.maximum_assignments = 128, .maximum_candidates = 4,
                            .maximum_growth_cells = *allocation_growth});
      if (allocations.empty()) return edges;

      for (const auto& allocation : allocations) {
        ConstrainedRepresentation next = source;
        next.artifact.items = allocation.items;
        if (!transaction_growth_fits(
                origin_cells, cells(next.artifact), limits.maximum_growth_cells))
          continue;
        next.encoded_selectors = encoded;
        for (const auto& [name, origin] : allocation.encoded_selector_targets)
          next.encoded_selectors[name] = origin;
        if (!refresh_constraint_targets(next)) continue;
        next.artifact.optimizations.push_back({"joint-composed-selector-allocation",
            "Retained complete-family allocation as a proved conditional edge; "
            "representation, register webs, region sharing and final geometry "
            "still close together against the immutable root's growth allowance."});
        edges.push_back({std::move(next), "complete-family-equations", true});
      }
      return edges;
    };
    auto composition_limits = limits;
    composition_limits.prioritize_composition_depth = coupled_geometry;
    // Closure continuations are real search steps. Do not silently halve the
    // caller's expansion/depth allowance: one first-generation node per rule
    // family can otherwise consume every slot before any techniques compose.
    std::optional<LayoutSearchArtifact> composition_incumbent;
    const auto composed = search_proved_transaction_closures(
        std::move(composition_seeds), std::vector<ConstrainedRepresentation>{initial},
        origin_cells, composition_limits,
        [](const auto& node) { return cells(node.artifact); },
        [](const auto& left, const auto& right) {
          return left.targets == right.targets &&
              left.encoded_selectors == right.encoded_selectors &&
              same_layout_search_artifact(left.artifact, right.artifact);
        },
        [&](const ConstrainedRepresentation& parent) {
          auto edges = expand(parent);
          for (auto& edge : allocation_edges(parent))
            edges.push_back(std::move(edge));
          return edges;
        },
        [&](const auto& node, const auto& path)
            -> std::vector<ConstrainedRepresentation> {
          std::vector<ConstrainedRepresentation> continuations;
          (void)close_node(node, path, true, &continuations);
          std::erase_if(continuations, [&](const auto& explored) {
            return !transaction_growth_fits(
                origin_cells, cells(explored.artifact), limits.maximum_growth_cells);
          });
          for (const auto& explored : continuations) {
          if (cells(explored.artifact) < origin_cells) {
            std::optional<LayoutSearchArtifact> published = explored.artifact;
            if (finalize) published = finalize(*published);
            if (published && cells(*published) < origin_cells &&
                build_post_layout_control_flow(published->items, control_options).proved &&
                (!composition_incumbent ||
                 cells(*published) < cells(*composition_incumbent)))
              composition_incumbent = std::move(*published);
          }
          }
          return continuations;
        });
    if (composition_incumbent &&
        (!output.incumbent || cells(*composition_incumbent) < cells(*output.incumbent))) {
      composition_incumbent->optimizations.push_back({
          "joint-composed-representation-frontier",
          "Composed representation, complete-family allocation and all distinct proved "
          "closure continuations without spending the historical search's budget; paid growth "
          "against the immutable root and independently authorized the final saving."});
      output.incumbent = std::move(*composition_incumbent);
    }
    output.expanded += composed.expanded;
    output.admitted += composed.admitted;
    output.duplicate += composed.duplicate;
    output.rejected += composed.rejected;
    output.budget_exhausted = output.budget_exhausted || composed.budget_exhausted;
  }
  // The input itself may already own a valid literal family, without an
  // unrelated representation rewrite to serve as an artificial search seed.
  close_data_control_literals(initial);
  if (data_control_incumbent &&
      (!output.incumbent || cells(*data_control_incumbent) < cells(*output.incumbent)))
    output.incumbent = std::move(*data_control_incumbent);
  // Finish every established lane before admitting partitioned prologues.
  // Their roots must not reduce an older root's product slots, beam capacity,
  // expansion budget or equal-size incumbent. Recursion is disabled in this
  // one additional bounded layer, not controlled by a source-specific marker.
  if (partition_lane == ProloguePartitionLane::Enabled && preload_environment_complete &&
      limits.maximum_expansions && limits.maximum_candidates && limits.maximum_depth) {
    for (auto& proposal : passes::common_processor_partitioned_entry_candidates(
             *root_ops, context,
             {.runtime_preloads = &root.preloads,
              .maximum_candidates = std::min<std::size_t>(2, limits.maximum_candidates)})) {
      auto child = apply(initial.artifact, std::move(proposal));
      if (!child || !transaction_growth_fits(origin_cells, cells(*child), limits.maximum_growth_cells))
        continue;
      ConstrainedRepresentation partitioned_root{*child, initial.targets};
      if (!refresh_constraint_targets(partitioned_root)) continue;
      // The proved root can already enable placement/tail sharing. Do not
      // demand an unrelated second rewrite just to try its complete closure.
      if (auto closed = close_node(partitioned_root, {"partitioned-common-processor"});
          closed && cells(closed->artifact) < origin_cells &&
          (!output.incumbent || cells(closed->artifact) < cells(*output.incumbent)))
        output.incumbent = closed->artifact;
      auto partition_limits = limits;
      --partition_limits.maximum_depth; // Pay for this root's enabling edge.
      if (cells(*child) > origin_cells)
        partition_limits.maximum_growth_cells -=
            static_cast<std::size_t>(cells(*child) - origin_cells);
      const auto partitioned = search_joint_representation_constraints_impl(
          *child, options, finalize, partition_limits, preload_environment_complete,
          ProloguePartitionLane::Disabled, proof_counts, finalize_intermediate);
      if (partitioned.incumbent && cells(*partitioned.incumbent) < origin_cells &&
          (!output.incumbent || cells(*partitioned.incumbent) < cells(*output.incumbent)))
        output.incumbent = partitioned.incumbent;
      output.expanded += partitioned.expanded;
      output.admitted += partitioned.admitted;
      output.duplicate += partitioned.duplicate;
      output.rejected += partitioned.rejected;
      output.budget_exhausted = output.budget_exhausted || partitioned.budget_exhausted;
    }
  }
  return output;
}

} // namespace


std::vector<ProvedSearchTransition<LayoutSearchArtifact>>
preloaded_finite_state_layout_transactions(const LayoutSearchArtifact& artifact,
                                         const CompileOptions& options) {
  // No return-context construction for artifacts without a delivered toggle.
  bool demand = false;
  for (std::size_t at = 2; at < artifact.items.size() && !demand; ++at) {
    const auto& read = artifact.items[at - 2U];
    const auto& negate = artifact.items[at - 1U];
    const auto& store = artifact.items[at];
    if (read.kind != MachineItemKind::Op || negate.kind != MachineItemKind::Op ||
        store.kind != MachineItemKind::Op || read.raw || negate.raw || store.raw ||
        read.opcode < 0x60 || read.opcode > 0x6e || negate.opcode != 0x0b ||
        store.opcode != read.opcode - 0x20) continue;
    const std::string name(1, "0123456789abcde"[read.opcode - 0x60]);
    demand = std::any_of(artifact.preloads.begin(), artifact.preloads.end(),
        [&](const auto& preload) { return preload.register_name == name; });
  }
  if (!demand) return {};
  const auto ops = representation_view(artifact, options);
  if (!ops) return {};
  CompileOptions configured = options;
  configured.defer_return_suffix_until_callee_hole = true;
  const passes::PassContext context{configured};
  PostLayoutControlFlowOptions control;
  control.address_space_model = address_space_model_for_feature_profile(
      effective_optimizer_feature_profile(options));
  control.empty_return_target = 1;
  std::vector<ProvedSearchTransition<LayoutSearchArtifact>> result;
  for (auto& proposal : passes::deferred_finite_mode_representation_candidates(
           *ops, context, {.maximum_growth_cells = 4, .maximum_candidates = 4,
               .runtime_preloads = &artifact.preloads,
               .preserve_compiler_selector_charges = true,
               .encode_preloaded_state = true})) {
    if (proposal.preload_replacements.empty()) continue;
    auto child = apply(artifact, std::move(proposal.transformation));
    if (!child) continue;
    bool valid = true;
    std::set<std::string> changed;
    for (const auto& replacement : proposal.preload_replacements) {
      auto owner = std::find_if(child->preloads.begin(), child->preloads.end(),
          [&](const auto& old) { return old.register_name == replacement.before.register_name; });
      if (owner == child->preloads.end() ||
          replacement.before.register_name != replacement.after.register_name ||
          !changed.insert(owner->register_name).second ||
          !same_layout_search_artifact(LayoutSearchArtifact{.preloads = {*owner}},
              LayoutSearchArtifact{.preloads = {replacement.before}})) {
        valid = false;
        break;
      }
      *owner = replacement.after;
    }
    if (!valid) continue;
    ConstrainedRepresentation node{std::move(*child), proposal.required_target_addresses};
    detail::PreparedJointProofInput proof_input(node.artifact.items, node.artifact.preloads, control);
    auto closed = close_representation_constraints(
        node, context, control, true, false, nullptr, proof_input);
    if (!closed || cells(*closed) >= cells(artifact)) continue;
    closed = rebind_layout_search_artifact(std::move(*closed));
    if (closed && build_post_layout_control_flow(closed->items, control).proved)
      result.push_back({std::move(*closed), "preloaded-finite-state-layout", true});
  }
  return result;
}


namespace {
// A data-certified store is revisited after coloring, before requiring the
// current selector geometry to close. Keep its recall, so every stack/X1/X2
// value is unchanged; the recall also independently closes numeric entry.
std::vector<ConstrainedRepresentation> deferred_recolored_copy_stores(
    const ConstrainedRepresentation& root, const passes::PassContext& context,
    const PostLayoutControlFlowOptions& control) {
  std::vector<ConstrainedRepresentation> result;
  const auto code=raise_machine_to_ir(root.artifact.items,effective_optimizer_feature_profile(context.options));
  const auto input=lower_ir_to_machine(code);const auto flow=build_post_layout_control_flow(input,control);
  const auto origins=detail::joint_target_origins(input,root.targets,root.encoded_selectors);
  if(!flow.proved || !origins)return result;
  const auto selected=build_deferred_selector_consistent_execution_graph(input,flow,*origins).execution;
  if(!selected.proved)return result;
  auto data=flow;data.execution_states.clear();
  for(auto s:selected.original_states)data.execution_states.push_back(flow.execution_states[s]);
  data.execution_successors=selected.successors;data.execution_edges=selected.execution_edges;
  std::vector<std::size_t> machine;std::size_t item=0;
  for(const auto& op:code){machine.push_back(item);item+=lower_ir_to_machine({op}).size();}
  std::size_t attempts=0;
  for(std::size_t i=1;i<code.size() && result.size()<4U;++i){
    if(code[i].kind!=IrKind::Store || code[i-1U].kind!=IrKind::Recall ||
        code[i].register_name==code[i-1U].register_name || code[i].meta.manual_interaction)continue;
    if(attempts++>=8U)break;
    const auto store=machine[i],recall=machine[i-1U];
    bool rooted=false,closed=true;
    for(const auto& entry:flow.external_entries)if(entry.entry.item_index==store)closed=false;
    for(std::size_t s=0;s<flow.execution_states.size();++s){
      for(const auto& edge:flow.execution_edges[s]){
        const auto& target=flow.execution_states[edge.target_state];
        if(target.item_index!=store)continue;rooted=true;
        if(flow.execution_states[s].item_index!=recall || edge.kind!=PostLayoutExecutionEdgeKind::Fallthrough ||
            flow.execution_states[s].return_stack!=target.return_stack ||
            flow.execution_states[s].formal_return_stack!=target.formal_return_stack)closed=false;
      }
    }
    if(!rooted || !closed)continue;
    const auto proof=passes::prove_contextual_copy_store_elision(code,data,i,root.artifact.preloads,200000,true);
    if(!proof.proved || proof.exhausted)continue;
    std::size_t retained=store+1U;while(retained<input.size() && input[retained].kind==MachineItemKind::Label)++retained;
    if(retained==input.size())continue;
    auto output=input;output.erase(output.begin()+static_cast<std::ptrdiff_t>(store));
    std::vector<std::optional<std::size_t>> mapping(input.size());
    for(std::size_t n=0;n<input.size();++n)mapping[n]=n<store?n:n>store?n-1U:retained-1U;
    const auto after=build_post_layout_control_flow(output,control);if(!after.proved)continue;
    PostLayoutExecutionRelocationOptions transport;transport.erased_data_effect_items={store};
    using Endpoint=std::pair<std::size_t,std::size_t>;
    std::map<Endpoint,std::set<std::optional<int>>> entries;
    for(std::size_t s=0;s<after.execution_states.size();++s)for(const auto& edge:after.execution_edges[s])
      if(edge.kind==PostLayoutExecutionEdgeKind::IndirectTarget)
        entries[{after.execution_states[s].item_index,after.execution_states[edge.target_state].item_index}].insert(edge.indirect_formal_opcode);
    bool compatible=true;
    for(std::size_t s=0;s<flow.execution_states.size() && compatible;++s)for(const auto& edge:flow.execution_edges[s]){
      if(edge.kind!=PostLayoutExecutionEdgeKind::IndirectTarget || !edge.indirect_formal_opcode)continue;
      const auto source=flow.execution_states[s].item_index,target=flow.execution_states[edge.target_state].item_index;
      const auto found=entries.find({*mapping[source],*mapping[target]});
      if(found==entries.end() || found->second.size()!=1U || !*found->second.begin()){compatible=false;break;}
      const int counter=**found->second.begin();auto& remap=transport.indirect_entry_remap[source];
      const auto [at,inserted]=remap.emplace(*edge.indirect_formal_opcode,counter);
      if(!inserted && at->second!=counter){compatible=false;break;}
    }
    if(!compatible || !prove_post_layout_execution_relocation(input,output,flow,after,mapping,transport).proved)continue;
    auto next=root;next.artifact.items=std::move(output);if(!refresh_constraint_targets(next))continue;
    next.artifact.optimizations.push_back({"deferred-contextual-copy-store-elision",
        "Revisited a recolored copy before freezing geometry; proved every dead/fixed-point caller, unchanged stack/X1/X2 and closed entry, then independently transported the complete rooted CFG and return frames. Retained every selector equation for final binding."});
    result.push_back(std::move(next));
  }
  return result;
}
}

std::vector<ProvedSearchTransition<LayoutSearchArtifact>>
contextual_copy_transport_layout_transactions(const LayoutSearchArtifact& artifact,
                                              const CompileOptions& options) {
  bool demand=false;
  for(std::size_t i=1;i<artifact.items.size() && !demand;++i) {
    const auto& a=artifact.items[i-1U];const auto& b=artifact.items[i];
    demand=a.kind==MachineItemKind::Op && b.kind==MachineItemKind::Op && !a.raw && !b.raw &&
        b.opcode>=0x40 && b.opcode<=0x4e &&
        ((a.opcode>=0x40 && a.opcode<=0x4e) || (a.opcode>=0x60 && a.opcode<=0x6e));
  }
  if(!demand)return {};
  auto ops=representation_view(artifact,options);if(!ops)return {};
  CompileOptions configured=options;configured.coalesce_copies=true;
  configured.defer_return_suffix_until_callee_hole=true;
  const passes::PassContext context{configured};
  PostLayoutControlFlowOptions control;control.empty_return_target=1;
  control.address_space_model=address_space_model_for_feature_profile(effective_optimizer_feature_profile(options));
  auto positions=constraint_positions(lower_ir_to_machine(*ops));if(!positions)return {};
  std::vector<ProvedSearchTransition<LayoutSearchArtifact>> result;
  std::vector<LayoutSearchArtifact> expanded_closed_roots;
  auto transports=passes::contextual_copy_transport_candidates(*ops,context);
  auto redundant=passes::contextual_redundant_copy_store_candidates(*ops,context,
      {.runtime_preloads=&artifact.preloads});
  transports.insert(transports.end(),std::make_move_iterator(redundant.begin()),
      std::make_move_iterator(redundant.end()));
  // A previous round can already have removed the copy. Its new lifetime,
  // literal and helper choices still deserve the same joint closure; do not
  // require an unrelated second copy to compose the remaining mechanisms.
  if(transports.empty())transports.push_back({{.ops=*ops},{}});
  const bool transport_trace=std::getenv("MKPRO_NATIVE_TRACE_COPY_TRANSPORT")!=nullptr;
  if(transport_trace)std::cerr<<"[copy-transport-layout] proposals="<<transports.size()<<"\n";
  for(auto& proposal:transports) {
    auto child=proposal.transformation.applied>0 ? apply(artifact,proposal.transformation)
        : std::optional<LayoutSearchArtifact>{artifact};
    if(!child)continue;
    ConstrainedRepresentation base{std::move(*child),*positions};
    if(!refresh_constraint_targets(base))continue;
    std::vector<ConstrainedRepresentation> allocations{base};
    auto source_flow=build_post_layout_control_flow(artifact.items,control);
    auto entry=resolve_post_layout_external_entry_contexts(source_flow);
    const auto deaths=passes::prove_post_layout_register_deadness(*ops,
        build_post_layout_control_flow(lower_ir_to_machine(*ops),control),100000);
    // New setup words are allowed only in an independently dead entry epoch.
    // Their setup effects must also disappear before every real observation.
    bool hidden_setup=false;
    if(source_flow.proved && entry) {
      std::vector<std::size_t> mains;
      for(std::size_t s=0;s<entry->main.size();++s)if(entry->main[s])mains.push_back(s);
      hidden_setup=prove_post_layout_stack_state_equality(artifact.items,source_flow,mains,
          {{false,false,false,false},false,false});
    }
    const auto touched_homes=[&](const LayoutSearchArtifact& value)->std::optional<std::set<std::string>>{
      std::set<std::string> homes;
      for(const auto& op:raise_machine_to_ir(value.items,effective_optimizer_feature_profile(options))){
        const auto effect=passes::register_effects(op);
        if(effect.uses_all_registers || effect.may_define_any_register)return std::nullopt;
        homes.insert(effect.uses.begin(),effect.uses.end());homes.insert(effect.must_defs.begin(),effect.must_defs.end());homes.insert(effect.may_defs.begin(),effect.may_defs.end());
      }return homes;
    };
    const auto original_homes=touched_homes(artifact);
    std::set<int> private_protocols;
    for(const auto& item:artifact.items)if(item.manual_interaction &&
        item.manual_interaction->kind==ManualInteractionAnchorKind::PromptStop &&
        std::find(item.roles.begin(),item.roles.end(),kTypedDisplayObservationRole)!=item.roles.end())
      private_protocols.insert(item.manual_interaction->protocol_id);
    const bool private_bank=std::all_of(artifact.items.begin(),artifact.items.end(),[&](const auto& item){
      if(item.raw)return false;
      if(item.manual_interaction && !private_protocols.contains(item.manual_interaction->protocol_id))return false;
      return item.kind!=MachineItemKind::Op || (item.opcode!=0x50 && item.opcode!=0x29) ||
          std::find(item.roles.begin(),item.roles.end(),kTypedDisplayObservationRole)!=item.roles.end();
    });
    const auto pure_initializers=[](const auto& words){return std::all_of(words.begin(),words.end(),[](const auto& word){
      return detail::joint_literal_word(word) && bound_ordinary_decimal(word.value).has_value();
    });};
    const bool original_initializers_pure=pure_initializers(artifact.preloads);
    const auto retire_moved_initializers=[&](ConstrainedRepresentation& node){
      if(!hidden_setup || !original_homes || !private_bank || !original_initializers_pure ||
          !pure_initializers(node.artifact.preloads))return;
      const auto used=touched_homes(node.artifact);if(!used)return;
      std::vector<std::string> retired;
      std::erase_if(node.artifact.preloads,[&](const auto& word){
        const auto reg=detail::joint_origin_register(word.register_name);
        if(!reg || *reg>14 || !original_homes->contains(word.register_name) || used->contains(word.register_name) ||
            !detail::joint_literal_word(word) || !bound_ordinary_decimal(word.value))return false;
        const auto owner=std::find_if(artifact.preloads.begin(),artifact.preloads.end(),[&](const auto& old){return old.register_name==word.register_name;});
        if(owner==artifact.preloads.end() || !same_layout_search_artifact(
            LayoutSearchArtifact{.preloads={*owner}},LayoutSearchArtifact{.preloads={word}}))return false;
        retired.push_back(word.register_name);return true;
      });
      if(!retired.empty())node.artifact.optimizations.push_back({"retired-allocated-setup-homes",
          "Retired "+std::to_string(retired.size())+" literal initializer homes only after their original physical epochs were eliminated or moved by proved value/allocation transactions. Rechecked every direct and indirect effect; arbitrary unused owners remain reserved. Independently proved all changed Main setup stack/X1/X2 unobservable."});
    };
    retire_moved_initializers(base);allocations.front()=base;
    const auto resource_key=[&](const ConstrainedRepresentation& node){
      std::set<std::string> homes;
      for(const auto& word:node.artifact.preloads)
        if(std::any_of(artifact.preloads.begin(),artifact.preloads.end(),[&](const auto& old){return old.register_name==word.register_name;}))homes.insert(word.register_name);
      const auto code=raise_machine_to_ir(node.artifact.items,effective_optimizer_feature_profile(options));
      for(const auto& op:code){const auto effect=passes::register_effects(op);
        if(effect.uses_all_registers || effect.may_define_any_register)return std::pair{cells(node.artifact),std::size_t{16}};
        homes.insert(effect.uses.begin(),effect.uses.end());homes.insert(effect.must_defs.begin(),effect.must_defs.end());
        homes.insert(effect.may_defs.begin(),effect.may_defs.end());
      }
      return std::pair{cells(node.artifact),homes.size()};
    };
    if(hidden_setup && deaths.proved) {
      std::map<std::string,std::size_t> counts;
      std::set<std::string> owners;
      for(const auto& word:base.artifact.preloads) {
        owners.insert(word.register_name);
        if(!word.setup_expression && !word.setup_target_name && !word.setup_expression_text &&
            !word.counts_against_program && bound_ordinary_decimal(word.value))++counts[word.value];
      }
      std::optional<std::pair<std::string,std::size_t>> common;
      for(const auto& [word,count]:counts)
        if(!common || count>common->second)common=std::pair{word,count};
      if(common) {
        auto seeded=base;
        const auto root_entries=resolve_post_layout_external_entry_contexts(
            build_post_layout_control_flow(lower_ir_to_machine(*ops),control));
        if(root_entries)for(int r=0;r<15;++r) {
          const std::string name(1,"0123456789abcde"[r]);
          if(owners.contains(name))continue;
          bool dead=true,main=false;
          for(std::size_t s=0;s<root_entries->main.size();++s)if(root_entries->main[s]) {
            main=true;dead=dead && deaths.before_state[s].contains(name);
          }
          if(dead && main)seeded.artifact.preloads.push_back({.register_name=name,.value=common->first,
              .lowered_data_value=common->first});
        }
        if(seeded.artifact.preloads.size()!=base.artifact.preloads.size()) {
          seeded.artifact.optimizations.push_back({"dead-entry-preload-homes",
              "Seeded only proved-dead initial register epochs with an existing ordinary word; "
              "proved changed setup stack/X1/X2 unobserved before every continuation."});
          allocations.push_back(std::move(seeded));
        }
      }
    }
    const auto transported_ops=raise_machine_to_ir(base.artifact.items,effective_optimizer_feature_profile(options));
    std::vector<passes::RegisterWebRecoloringRequest> requests;
    // Cursor/FL anchors name exact use/definition epochs, not a preferred
    // physical register. Recolor the entire interference graph jointly.
    for(auto store:proposal.materialized_stores)
      for(const auto family:{IrKind::IndirectStore,IrKind::IndirectRecall,IrKind::Loop})
        for(std::size_t i=0;i<transported_ops.size() && requests.size()<12U;++i)
          if(transported_ops[i].kind==family)requests.push_back({{store,i}});
    auto ordinary_requests=passes::common_processor_factoring_register_requests(transported_ops);
    auto region_requests=passes::semantic_region_register_requests(transported_ops,true);
    ordinary_requests.insert(ordinary_requests.end(),region_requests.begin(),region_requests.end());
    for(auto& request:ordinary_requests)if(requests.size()<20U)requests.push_back(std::move(request));
    const auto seed_count=allocations.size();
    for(std::size_t n=0;n<seed_count;++n) {
      auto root=allocations[n];
      passes::for_each_deferred_register_web_recoloring(transported_ops,context,requests,control,
          root.targets,[&](std::vector<IrOp> recolored){
            if(allocations.size()>=8U)return;
            auto next=root;next.artifact.items=lower_ir_to_machine(recolored);retire_moved_initializers(next);
            if(std::none_of(allocations.begin(),allocations.end(),[&](const auto& old){
                return same_layout_search_artifact(old.artifact,next.artifact);}))allocations.push_back(std::move(next));
          },&root.artifact.preloads,true);
    }
    const auto allocated_count=allocations.size();
    if(transport_trace)std::cerr<<"[copy-transport-layout] allocations="<<allocated_count<<"\n";
    for(std::size_t n=0;n<allocated_count && allocations.size()<16U;++n) {
      const auto root=allocations[n];
      const auto code=raise_machine_to_ir(root.artifact.items,effective_optimizer_feature_profile(options));
      for(auto& codec:passes::deferred_finite_mode_representation_candidates(code,context,
          {.maximum_growth_cells=4,.maximum_candidates=2,.allow_target_relayout=true,
           .runtime_preloads=&root.artifact.preloads,.preserve_compiler_selector_charges=true,
           .assumed_target_addresses=&root.targets,.encode_preloaded_state=true})) {
        auto encoded=apply(root.artifact,std::move(codec.transformation));if(!encoded)continue;
        bool valid=true;
        for(const auto& change:codec.preload_replacements) {
          auto owner=std::find_if(encoded->preloads.begin(),encoded->preloads.end(),[&](const auto& p){
            return p.register_name==change.before.register_name;});
          if(owner==encoded->preloads.end() || !same_layout_search_artifact(
              LayoutSearchArtifact{.preloads={*owner}},LayoutSearchArtifact{.preloads={change.before}})) {
            valid=false;break;
          }
          *owner=change.after;
        }
        if(valid)allocations.push_back({std::move(*encoded),codec.required_target_addresses,root.encoded_selectors});
        if(allocations.size()>=16U)break;
      }
    }
    // The initializer lane must not hide mixed finite/dynamic epochs in an
    // ordinary register. Keep an independent bounded portfolio, with complete
    // preloads and precise entry facts, before choosing allocation or geometry.
    std::vector<ConstrainedRepresentation> epoch_encodings;
    for (std::size_t n=0;n<allocated_count && epoch_encodings.size()<4U;++n) {
      const auto root=allocations[n];
      const auto code=raise_machine_to_ir(root.artifact.items,effective_optimizer_feature_profile(options));
      for (auto& codec:passes::deferred_finite_mode_representation_candidates(code,context,
          {.maximum_growth_cells=4,.maximum_candidates=2,.allow_target_relayout=true,
           .runtime_preloads=&root.artifact.preloads,.preserve_compiler_selector_charges=true,
           .assumed_target_addresses=&root.targets,.normalize_unit_arguments=true,
           .precise_entry_facts=true})) {
        auto encoded=apply(root.artifact,std::move(codec.transformation)); if(!encoded)continue;
        if(!codec.preload_replacements.empty())continue;
        ConstrainedRepresentation next{std::move(*encoded),codec.required_target_addresses,root.encoded_selectors};
        const auto same=[&](const auto& old){return same_layout_search_artifact(old.artifact,next.artifact);};
        if(std::none_of(allocations.begin(),allocations.end(),same) &&
           std::none_of(epoch_encodings.begin(),epoch_encodings.end(),same))
          epoch_encodings.push_back(std::move(next));
        if(epoch_encodings.size()>=4U)break;
      }
    }
    for(auto& next:epoch_encodings)allocations.push_back(std::move(next));
    // A representation can prove a guard that splits an older hardware epoch.
    // Re-run allocation on that encoded program rather than freezing the first
    // coloring. Origins only identify requests; every lifetime fact is re-proved.
    const auto encoded_count=allocations.size();
    std::vector<ConstrainedRepresentation> encoded_allocations;
    std::size_t allocation_round_begin=0, allocation_round_end=encoded_count;
    for(int allocation_round=0;allocation_round<2;++allocation_round) {
    encoded_allocations.clear();
    for(std::size_t n=allocation_round_begin;n<allocation_round_end;++n) {
      const auto root=allocations[n];
      const auto code=raise_machine_to_ir(root.artifact.items,effective_optimizer_feature_profile(options));
      std::vector<std::size_t> stores;
      std::set<std::string> identities;
      bool unique=true;
      for(std::size_t i=0;i<code.size();++i)for(const auto& role:code[i].meta.roles)
        if(role.starts_with(detail::kContextualCopyStoreOriginPrefix)) {
          if(!detail::is_cell_origin_provenance(role) || code[i].kind!=IrKind::Store ||
              !identities.insert(role).second)unique=false;
          stores.push_back(i);
        }
      if(!unique || stores.empty())continue;
      std::vector<passes::RegisterWebRecoloringRequest> demands;
      for(const auto store:stores)for(const auto kind:{IrKind::IndirectStore,IrKind::IndirectRecall,IrKind::Loop})
        for(std::size_t i=0;i<code.size() && demands.size()<12U;++i)
          if(code[i].kind==kind)demands.push_back({{store,i}});
      // One hardware equality may move an ordinary epoch into a third home.
      // Price simultaneous, cost-equivalent storage equalities as well; a
      // greedy first coloring is not a reason to freeze that third home.
      std::map<std::string,std::size_t> ordinary_stores;
      std::map<std::string,std::vector<std::size_t>> store_epochs;
      for(std::size_t i=0;i<code.size();++i) {
        const auto& op=code[i];
        if(op.kind!=IrKind::Store || op.meta.raw ||
            (op.meta.manual_interaction && op.meta.manual_interaction->kind!=ManualInteractionAnchorKind::SingleStepCommand) ||
            op.procedure_boundary || op.meta.tactic ||
            !std::all_of(op.meta.roles.begin(),op.meta.roles.end(),detail::is_cell_origin_provenance))continue;
        ordinary_stores.try_emplace(op.register_name,i);store_epochs[op.register_name].push_back(i);
      }
      const auto primary_demands=demands;
      std::vector<std::pair<std::size_t,std::size_t>> ordinary_equalities;
      for(auto a=ordinary_stores.begin();a!=ordinary_stores.end();++a)
        for(auto b=std::next(a);b!=ordinary_stores.end();++b) {
          const auto high=[](const auto& name){return name.size()==1U && std::string_view{"789abcde"}.find(name[0])!=std::string_view::npos;};
          if(high(a->first) || high(b->first))ordinary_equalities.emplace_back(a->second,b->second);
        }
      // Fair product order: all ordinary pairs can meet each hardware demand
      // before one early pair consumes the entire compound-request budget.
      for(const auto& equality:ordinary_equalities) {
        if(demands.size()>=32U)break;
        demands.push_back({equality});
      }
      // A separate product lane is essential: filling the ordinary request
      // budget first used to make this loop execute zero useful requests.
      // Enumerate diagonals so neither operand family monopolizes the lane.
      std::size_t products=0;
      for(std::size_t diagonal=0;diagonal<primary_demands.size()+ordinary_equalities.size() && products<32U;++diagonal)
        for(std::size_t left=0;left<primary_demands.size() && left<=diagonal && products<32U;++left){
          const auto right=diagonal-left;if(right>=ordinary_equalities.size())continue;
          auto request=primary_demands[left];request.push_back(ordinary_equalities[right]);
          demands.push_back(std::move(request));++products;
        }
      // A physical register can hold several independent definition webs.
      // Keep the historical first-anchor requests, then fairly enumerate a
      // separate bounded portfolio of later epoch anchors.
      std::size_t maximum_epochs=0;for(const auto& [name,sites]:store_epochs){(void)name;maximum_epochs=std::max(maximum_epochs,sites.size());}
      std::vector<passes::RegisterWebRecoloringRequest> epoch_demands;
      for(std::size_t depth=1;depth<maximum_epochs*2U && epoch_demands.size()<32U;++depth)
        for(auto a=store_epochs.begin();a!=store_epochs.end() && epoch_demands.size()<32U;++a)
          for(auto b=std::next(a);b!=store_epochs.end() && epoch_demands.size()<32U;++b){
            const auto high=[](const auto& name){return name.size()==1U && std::string_view{"789abcde"}.find(name[0])!=std::string_view::npos;};
            if(!high(a->first) && !high(b->first))continue;
            for(std::size_t left=0;left<a->second.size() && left<=depth && epoch_demands.size()<32U;++left){
              const auto right=depth-left;if(right>=b->second.size())continue;
              epoch_demands.push_back({{a->second[left],b->second[right]}});
            }
          }
      demands.insert(demands.end(),epoch_demands.begin(),epoch_demands.end());
      passes::for_each_deferred_register_web_recoloring(code,context,demands,control,root.targets,
          [&](std::vector<IrOp> recolored){
            auto next=root;next.artifact.items=lower_ir_to_machine(recolored);retire_moved_initializers(next);
            if(cells(next.artifact)>cells(artifact)+4)return;
            const auto same=[&](const auto& old){return same_layout_search_artifact(old.artifact,next.artifact);};
            if(std::none_of(allocations.begin(),allocations.end(),same) &&
                std::none_of(encoded_allocations.begin(),encoded_allocations.end(),same)) {
              next.artifact.optimizations.push_back({"encoded-state-lifetime-allocation",
                  "Re-proved conditional def/use exclusions after finite encoding; jointly recolored "
                  "hardware cursor and ordinary epochs before choosing geometry."});
              encoded_allocations.push_back(std::move(next));
              std::stable_sort(encoded_allocations.begin(),encoded_allocations.end(),[&](const auto& a,const auto& b){return resource_key(a)<resource_key(b);});
              if(encoded_allocations.size()>8U)encoded_allocations.resize(8U);
            }
          },&root.artifact.preloads,true);
    }
    for(auto& next:encoded_allocations)allocations.push_back(std::move(next));
    // A first joint equality can expose a third compatible epoch. Feed its
    // complete, independently re-proved allocation back once, rather than
    // forcing all useful equalities to fit in one first-generation request.
    allocation_round_begin=allocation_round_end;allocation_round_end=allocations.size();
    if(allocation_round_begin==allocation_round_end)break;
    }
    std::vector<std::size_t> cleanup_order;for(std::size_t n=0;n<allocations.size();++n)cleanup_order.push_back(n);
    std::stable_sort(cleanup_order.begin(),cleanup_order.end(),[&](auto a,auto b){return resource_key(allocations[a])<resource_key(allocations[b]);});
    std::vector<ConstrainedRepresentation> cleaned;
    for(const auto n:cleanup_order){
      if(cleaned.size()>=8U)break;
      for(auto& next:deferred_recolored_copy_stores(allocations[n],context,control)){
        retire_moved_initializers(next);
        const auto same=[&](const auto& old){return same_layout_search_artifact(old.artifact,next.artifact);};
        if(std::none_of(allocations.begin(),allocations.end(),same) && std::none_of(cleaned.begin(),cleaned.end(),same))cleaned.push_back(std::move(next));
        if(cleaned.size()>=8U)break;
      }
    }
    for(auto& next:cleaned)allocations.push_back(std::move(next));
    const auto materialization_roots=allocations.size();
    std::size_t materialized=0;
    std::vector<std::size_t> materialization_order;
    for(std::size_t n=0;n<materialization_roots;++n)materialization_order.push_back(n);
    std::stable_sort(materialization_order.begin(),materialization_order.end(),[&](auto a,auto b){return resource_key(allocations[a])<resource_key(allocations[b]);});
    for(const auto n:materialization_order){
      if(materialized>=8U)break;
      const auto root=allocations[n];
      const auto code=raise_machine_to_ir(root.artifact.items,effective_optimizer_feature_profile(options));
      auto flow=build_post_layout_control_flow(root.artifact.items,control);
      const auto origins=detail::joint_target_origins(root.artifact.items,root.targets,root.encoded_selectors);
      if(!origins)continue;
      const auto selected=build_deferred_selector_consistent_execution_graph(root.artifact.items,flow,*origins).execution;
      if(!selected.proved)continue;
      const auto states=flow.execution_states;flow.execution_states.clear();
      for(auto s:selected.original_states)flow.execution_states.push_back(states[s]);
      flow.execution_successors=selected.successors;flow.execution_edges=selected.execution_edges;
      if(auto materialized_child=apply(root.artifact,passes::deferred_zero_underflow_literal(code,flow,root.artifact.preloads))){
        ConstrainedRepresentation next{std::move(*materialized_child),root.targets,root.encoded_selectors};
        if(refresh_constraint_targets(next)){allocations.push_back(std::move(next));++materialized;}
      }
    }
    const auto pool_roots=allocations.size();
    std::vector<ConstrainedRepresentation> pooled;
    std::vector<std::size_t> pooling_order;
    for(std::size_t n=0;n<pool_roots;++n)pooling_order.push_back(n);
    std::stable_sort(pooling_order.begin(),pooling_order.end(),[&](auto a,auto b){return resource_key(allocations[a])<resource_key(allocations[b]);});
    for(const auto n:pooling_order){
      if(pooled.size()>=8U)break;
      auto pooling_root=allocations[n];
      // Equal-entry witnesses may need a temporary setup home during coloring.
      // Once the entire emitted program no longer touches that home, release
      // only this lane's new seed before pricing a constant there. Source
      // initializers and immutable pools are never candidates for takeover.
      if(hidden_setup){
        const auto code=raise_machine_to_ir(pooling_root.artifact.items,effective_optimizer_feature_profile(options));
        std::erase_if(pooling_root.artifact.preloads,[&](const auto& word){
          if(std::any_of(artifact.preloads.begin(),artifact.preloads.end(),[&](const auto& original){return original.register_name==word.register_name;}))return false;
          for(const auto& op:code){const auto effect=passes::register_effects(op);
            if(effect.uses_all_registers || effect.may_define_any_register ||
                effect.uses.contains(word.register_name) || effect.must_defs.contains(word.register_name) ||
                effect.may_defs.contains(word.register_name))return false;
          }
          return true;
        });
      }
      for(auto& next:pool_allocation_exposed_literals(pooling_root,context,control)){
        pooled.push_back(std::move(next));if(pooled.size()>=8U)break;
      }
    }
    for(auto& next:pooled)allocations.push_back(std::move(next));
    // Allocation and literal choices can expose a common-operand call ABI.
    // Reuse the established complete-family relational proof before solving
    // physical addresses, retaining every older alternative independently.
    std::vector<std::size_t> abi_order;for(std::size_t n=0;n<allocations.size();++n)abi_order.push_back(n);
    std::stable_sort(abi_order.begin(),abi_order.end(),[&](auto a,auto b){return resource_key(allocations[a])<resource_key(allocations[b]);});
    std::vector<ConstrainedRepresentation> operand_abis;
    for(const auto n:abi_order){
      if(operand_abis.size()>=8U)break;
      const auto root=allocations[n];const auto flow=build_post_layout_control_flow(root.artifact.items,control);
      if(!flow.proved)continue;
      HelperInvariantRecallHoistOptions hoist_options;hoist_options.complete_program_control=control;
      hoist_options.deferred_target_origins=detail::joint_target_origins(root.artifact.items,root.targets,root.encoded_selectors);
      if(!hoist_options.deferred_target_origins)continue;
      hoist_options.allow_x_preserving_root=true;hoist_options.allow_swapped_return=true;
      hoist_options.allow_callsite_operand_swap=true;
      for(const auto& [site,targets]:flow.indirect_flow_targets)
        for(const auto& target:targets)hoist_options.proved_indirect_flow_targets[site].push_back(target.address);
      for(const bool tail:{false,true}){
        hoist_options.prefer_before_return_plan=tail;
        auto hoist=optimize_helper_invariant_recall_hoist(root.artifact.items,hoist_options);
        if(hoist.applied<=0 || !hoist.proof.proved || !hoist.proof.final_artifact_proved)continue;
        auto next=root;next.artifact.items=std::move(hoist.items);
        for(const auto& report:hoist.optimizations)next.artifact.optimizations.push_back({report.name,report.detail});
        if(!refresh_constraint_targets(next) || cells(next.artifact)>cells(artifact)+4)continue;
        const auto same=[&](const auto& old){return same_layout_search_artifact(old.artifact,next.artifact);};
        if(std::any_of(allocations.begin(),allocations.end(),same) || std::any_of(operand_abis.begin(),operand_abis.end(),same))continue;
        next.artifact.optimizations.push_back({"deferred-helper-operand-abi",
            "Composed a fully proved common-operand root/tail ABI after value and lifetime choices; "
            "physical data/address constraints remain pending until joint placement."});
        operand_abis.push_back(std::move(next));if(operand_abis.size()>=8U)break;
      }
    }
    const auto entry_abi_begin=allocations.size();
    for(auto& next:operand_abis)allocations.push_back(std::move(next));
    // Input correction, epoch equalities and the common helper ABI form one
    // enabling transaction. Keep this bounded family independent of earlier
    // coloring/codec beams; price it only after final region placement.
    std::vector<ConstrainedRepresentation> entry_prologues;
    auto entry_order=abi_order;
    for(std::size_t n=entry_abi_begin;n<allocations.size();++n)entry_order.push_back(n);
    std::stable_sort(entry_order.begin(),entry_order.end(),[&](auto a,auto b){return resource_key(allocations[a])<resource_key(allocations[b]);});
    for(const auto n:entry_order){
      if(entry_prologues.size()>=8U)break;
      const auto root=allocations[n];
        const auto code=raise_machine_to_ir(root.artifact.items,effective_optimizer_feature_profile(options));
        const auto demands=passes::idempotent_entry_prologue_register_requests(code);if(demands.empty())continue;
        const auto flow=build_post_layout_control_flow(root.artifact.items,control);
        if(!flow.proved || prepare_shared_helper_dual_mode_layout(root.artifact.items,flow,control.address_space_model).applied<=0)continue;
        const auto accept_entry=[&](const std::vector<IrOp>& recolored){
              if(entry_prologues.size()>=8U)return;
              for(auto& proposal:passes::deferred_idempotent_entry_prologue_candidates(recolored,root.artifact.preloads,control,root.targets)){
                auto child=apply(root.artifact,std::move(proposal));if(!child)continue;
                auto next=root;next.artifact=std::move(*child);
                if(!refresh_constraint_targets(next))continue;retire_moved_initializers(next);
                const auto same=[&](const auto& old){return same_layout_search_artifact(old.artifact,next.artifact);};
                if(std::none_of(entry_prologues.begin(),entry_prologues.end(),same))entry_prologues.push_back(std::move(next));
                if(entry_prologues.size()>=8U)break;
              }
            };
        // An already unified entry does not need a changed coloring to be useful.
        accept_entry(code);
        passes::for_each_deferred_register_web_recoloring(code,context,demands,control,root.targets,
            accept_entry,&root.artifact.preloads,true);
    }
    const auto post_entry_begin=allocations.size();
    for(auto& next:entry_prologues)allocations.push_back(std::move(next));
    const auto post_entry_end=allocations.size();
    // Moving a prologue changes which definitions reach its old argument
    // stores. Rebuild webs before binding any address: a scalar argument can
    // now share an output epoch without uniting the persistent helper field.
    std::vector<ConstrainedRepresentation> entry_feedback;
    const auto remember_feedback=[&](ConstrainedRepresentation next){
      if(entry_feedback.size()>=8U || !refresh_constraint_targets(next))return;
      retire_moved_initializers(next);
      const auto same=[&](const auto& old){return same_layout_search_artifact(old.artifact,next.artifact);};
      if(std::any_of(allocations.begin(),allocations.end(),same) ||
         std::any_of(entry_feedback.begin(),entry_feedback.end(),same))return;
      next.artifact.optimizations.push_back({"post-entry-allocation-feedback",
          "Rebuilt exact definition webs after moving the common entry prologue; composed new argument/output lifetime equalities, copy elimination and literal storage before fixing selector addresses. Every hardware/input and final layout proof remains mandatory."});
      entry_feedback.push_back(std::move(next));
    };
    for(std::size_t n=post_entry_begin;n<post_entry_end && entry_feedback.size()<8U;++n){
      const auto root=allocations[n];
      const auto origins=detail::joint_target_origins(root.artifact.items,root.targets,root.encoded_selectors);
      if(!origins)continue;
      const auto code=raise_machine_to_ir(root.artifact.items,effective_optimizer_feature_profile(options));
      auto compacted=apply(root.artifact,passes::deferred_register_web_copy_coalesce(
          code,context,control,*origins,root.artifact.preloads));
      auto feedback_root=root;
      if(compacted){feedback_root.artifact=std::move(*compacted);remember_feedback(feedback_root);}
      if(!refresh_constraint_targets(feedback_root))continue;
      const auto updated=raise_machine_to_ir(feedback_root.artifact.items,effective_optimizer_feature_profile(options));
      std::vector<passes::RegisterWebRecoloringRequest> demands;
      std::set<int> anchors;
      for(std::size_t i=0;i<updated.size();++i){
        const auto& input=updated[i];
        if(input.kind!=IrKind::Store || !input.meta.manual_interaction ||
           input.meta.manual_interaction->kind!=ManualInteractionAnchorKind::SingleStepCommand)continue;
        anchors.clear();
        for(std::size_t at=0;at<updated.size() && demands.size()<8U;++at){
          const auto& op=updated[at];
          if((op.kind!=IrKind::IndirectStore && op.kind!=IrKind::IndirectRecall) || op.meta.raw)continue;
          const auto reg=detail::joint_origin_register(op.register_name);
          if(reg && *reg<4 && anchors.insert(*reg).second)demands.push_back({{i,at}});
        }
      }
      const auto current_origins=detail::joint_target_origins(feedback_root.artifact.items,feedback_root.targets,feedback_root.encoded_selectors);
      if(current_origins)passes::for_each_deferred_register_web_recoloring(updated,context,demands,control,*current_origins,
          [&](auto recolored){
            if(entry_feedback.size()>=8U)return;
            auto next=feedback_root;next.artifact.items=lower_ir_to_machine(recolored);
            const auto final_origins=detail::joint_target_origins(next.artifact.items,next.targets,next.encoded_selectors);
            if(!final_origins)return;
            if(auto erased=apply(next.artifact,passes::deferred_register_web_copy_coalesce(
                recolored,context,control,*final_origins,next.artifact.preloads)))next.artifact=std::move(*erased);
            remember_feedback(std::move(next));
          },&feedback_root.artifact.preloads,true);
      if(entry_feedback.size()<8U)for(auto& feedback_pool:pool_allocation_exposed_literals(feedback_root,context,control)){
        remember_feedback(std::move(feedback_pool));if(entry_feedback.size()>=8U)break;
      }
    }
    for(auto& next:entry_feedback)allocations.push_back(std::move(next));
    // Retain nonadjacent value-origin allocations in an independent lane.
    // They are only preferences: complete def/use, hardware, alias and input
    // proofs run before GVN may use a newly available old destination word.
    const auto origin_parent_count=allocations.size();
    std::vector<std::size_t> origin_order;
    for(std::size_t n=0;n<origin_parent_count;++n)origin_order.push_back(n);
    std::stable_sort(origin_order.begin(),origin_order.end(),[&](auto a,auto b){
      return resource_key(allocations[a])<resource_key(allocations[b]);
    });
    std::vector<ConstrainedRepresentation> origin_allocations;
    for(const auto n:origin_order){
      if(origin_allocations.size()>=8U)break;
      const auto root=allocations[n];
      const auto code=raise_machine_to_ir(root.artifact.items,effective_optimizer_feature_profile(options));
      const auto origin_web_requests=passes::contextual_copy_origin_register_requests(code,16);
      if(origin_web_requests.empty())continue;
      const auto origins=detail::joint_target_origins(root.artifact.items,root.targets,root.encoded_selectors);
      if(!origins)continue;
      passes::for_each_deferred_register_web_recoloring(code,context,origin_web_requests,control,*origins,
          [&](auto recolored){
            if(origin_allocations.size()>=8U)return;
            auto next=root;next.artifact.items=lower_ir_to_machine(recolored);
            if(!refresh_constraint_targets(next))return;
            retire_moved_initializers(next);
            const auto same=[&](const auto& old){
              return same_layout_search_artifact(old.artifact,next.artifact) &&
                  old.targets==next.targets && old.encoded_selectors==next.encoded_selectors;
            };
            if(std::any_of(allocations.begin(),allocations.end(),same) ||
                std::any_of(origin_allocations.begin(),origin_allocations.end(),same))return;
            next.artifact.optimizations.push_back({"transitive-value-origin-allocation",
                "Proposed nonadjacent complete-web homes from copy/helper argument provenance, "
                "fairly enumerated by destination family. Re-proved hardware domains, aliases, "
                "typed phases and reaching definitions without assuming a helper is an identity. "
                "Contextual GVN and final layout remain independent publication obligations."});
            origin_allocations.push_back(std::move(next));
          },&root.artifact.preloads,true);
    }
    for(auto& next:origin_allocations)allocations.push_back(std::move(next));
    // Closing an ABI can expose equal normalized recall views only after
    // complete-web recoloring. Keep this feedback family independent of the
    // older cleanup beam and rebuild allocation before binding any geometry.
    const auto word_parent_count=allocations.size();
    std::vector<ConstrainedRepresentation> word_feedback;
    std::vector<std::size_t> word_order;
    for(std::size_t n=0;n<word_parent_count;++n)word_order.push_back(n);
    std::stable_sort(word_order.begin(),word_order.end(),[&](auto a,auto b){
      return resource_key(allocations[a])<resource_key(allocations[b]);
    });
    const auto remember_word=[&](ConstrainedRepresentation next){
      if(word_feedback.size()>=8U || !refresh_constraint_targets(next))return;
      retire_moved_initializers(next);
      const auto same=[&](const auto& old){
        return same_layout_search_artifact(old.artifact,next.artifact) &&
            old.targets==next.targets && old.encoded_selectors==next.encoded_selectors;
      };
      if(std::any_of(allocations.begin(),allocations.end(),same) ||
          std::any_of(word_feedback.begin(),word_feedback.end(),same))return;
      next.artifact.optimizations.push_back({"post-abi-normalizing-gvn-feedback",
          "Re-proved grounded raw-word and normalizing-load congruences after ABI/allocation, "
          "then reopened complete definition webs before pricing geometry. Kept every old "
          "candidate and encoded selector identity; only final certified layout may publish."});
      word_feedback.push_back(std::move(next));
    };
    for(const auto n:word_order){
      if(word_feedback.size()>=8U)break;
      const auto root=allocations[n];
      const auto origins=detail::joint_target_origins(root.artifact.items,root.targets,root.encoded_selectors);
      if(!origins)continue;
      const auto code=raise_machine_to_ir(root.artifact.items,effective_optimizer_feature_profile(options));
      for(auto& candidate:passes::contextual_redundant_copy_store_candidates(code,context,
          {.maximum_work=200000,.maximum_candidates=2,.maximum_copy_sites=8,
           .control=&control,.encoded_target_origins=&*origins,.runtime_preloads=&root.artifact.preloads})){
        auto erased=apply(root.artifact,std::move(candidate.transformation));if(!erased)continue;
        auto next=root;next.artifact=std::move(*erased);
        if(!refresh_constraint_targets(next))continue;
        remember_word(next);
        const auto rewritten=raise_machine_to_ir(next.artifact.items,effective_optimizer_feature_profile(options));
        auto demands=passes::semantic_region_register_requests(rewritten,true);
        auto common=passes::common_processor_factoring_register_requests(rewritten);
        demands.insert(demands.end(),common.begin(),common.end());
        if(demands.size()>20U)demands.resize(20U);
        const auto new_origins=detail::joint_target_origins(next.artifact.items,next.targets,next.encoded_selectors);
        if(!new_origins)continue;
        passes::for_each_deferred_register_web_recoloring(rewritten,context,demands,control,*new_origins,
            [&](auto recolored){
              if(word_feedback.size()>=8U)return;
              auto allocated=next;allocated.artifact.items=lower_ir_to_machine(recolored);
              const auto relocated_origins=detail::joint_target_origins(
                  allocated.artifact.items,allocated.targets,allocated.encoded_selectors);
              if(!relocated_origins)return;
              if(auto coalesced=apply(allocated.artifact,passes::deferred_register_web_copy_coalesce(
                  recolored,context,control,*relocated_origins,allocated.artifact.preloads)))
                allocated.artifact=std::move(*coalesced);
              remember_word(std::move(allocated));
            },&next.artifact.preloads,true);
        if(word_feedback.size()<8U)
          for(auto& word_pool:pool_allocation_exposed_literals(next,context,control)){
            remember_word(std::move(word_pool));if(word_feedback.size()>=8U)break;
          }
        if(word_feedback.size()>=8U)break;
      }
    }
    for(auto& next:word_feedback)allocations.push_back(std::move(next));
    // A newly encoded finite word may also supply a return address. Keep
    // these semantic alternatives before choosing/pinning any final geometry.
    const auto selector_parent_count=allocations.size();
    std::vector<ConstrainedRepresentation> finite_returns;
    for(std::size_t n=0;n<selector_parent_count&&finite_returns.size()<4U;++n){
      const auto root=allocations[n];
      const auto origins=detail::joint_target_origins(root.artifact.items,root.targets,root.encoded_selectors);
      if(!origins)continue;
      const auto code=raise_machine_to_ir(root.artifact.items,effective_optimizer_feature_profile(options));
      for(auto& returned:passes::contextual_finite_return_selector_candidates(code,context,
          {.maximum_work=100000,.maximum_candidates=2,.control=&control,
           .encoded_target_origins=&*origins,.runtime_preloads=&root.artifact.preloads})){
        auto rewritten=apply(root.artifact,std::move(returned.transformation));if(!rewritten)continue;
        auto next=root;next.artifact=std::move(*rewritten);
        for(const auto& [reg,origin]:returned.selector_origins)next.encoded_selectors[reg]=origin;
        if(!refresh_constraint_targets(next))continue;
        const auto same=[&](const auto& old){return same_layout_search_artifact(old.artifact,next.artifact)&&
            old.targets==next.targets&&old.encoded_selectors==next.encoded_selectors;};
        if(std::any_of(allocations.begin(),allocations.end(),same)||std::any_of(finite_returns.begin(),finite_returns.end(),same))continue;
        finite_returns.push_back(std::move(next));if(finite_returns.size()>=4U)break;
      }
    }
    for(auto& next:finite_returns)allocations.push_back(std::move(next));
    std::vector<std::size_t> completion_order;
    for(std::size_t n=0;n<allocations.size();++n)completion_order.push_back(n);
    std::stable_sort(completion_order.begin(),completion_order.end(),[&](auto a,auto b){return resource_key(allocations[a])<resource_key(allocations[b]);});
    std::vector<ConstrainedRepresentation> completions;
    for(const auto n:completion_order){
      if(completions.size()>=8U)break;
      const auto root=allocations[n];
      const auto origins=detail::joint_target_origins(root.artifact.items,root.targets,root.encoded_selectors);
      if(!origins)continue;
      const auto code=raise_machine_to_ir(root.artifact.items,effective_optimizer_feature_profile(options));
      for(auto& completed:passes::contextual_pure_return_completion_candidates(code,context,
          {.maximum_work=200000,.maximum_candidates=2,.maximum_copy_sites=8,.control=&control,
           .encoded_target_origins=&*origins,.runtime_preloads=&root.artifact.preloads})){
        auto artifact_after=apply(root.artifact,std::move(completed.transformation));if(!artifact_after)continue;
        auto next=root;next.artifact=std::move(*artifact_after);
        if(!refresh_constraint_targets(next))continue;
        const auto same=[&](const auto& old){return same_layout_search_artifact(old.artifact,next.artifact)&&
            old.targets==next.targets&&old.encoded_selectors==next.encoded_selectors;};
        if(std::any_of(allocations.begin(),allocations.end(),same)||std::any_of(completions.begin(),completions.end(),same))continue;
        completions.push_back(std::move(next));if(completions.size()>=8U)break;
      }
    }
    for(auto& next:completions)allocations.push_back(std::move(next));
    std::vector<std::size_t> permutation_order;for(std::size_t n=0;n<allocations.size();++n)permutation_order.push_back(n);
    std::stable_sort(permutation_order.begin(),permutation_order.end(),[&](auto a,auto b){return resource_key(allocations[a])<resource_key(allocations[b]);});
    std::vector<ConstrainedRepresentation> permutations;
    for(const auto n:permutation_order){
      if(permutations.size()>=8U)break;
      const auto root=allocations[n];const auto flow=build_post_layout_control_flow(root.artifact.items,control);
      if(!flow.proved)continue;
      std::map<std::string,int> encoded;
      for(const auto& word:root.artifact.preloads){
        if(!detail::joint_literal_word(word))continue;
        const auto reg=detail::joint_origin_register(word.register_name);if(!reg || *reg<7 || *reg>14)continue;
        try{const auto address=evaluate_indirect_address(word.register_name,word.value,IndirectOperationKind::Flow,control.address_space_model);
          if(address && address->actual_flow_target && address->formal_address && address->formal_address->kind==FormalAddressKind::Official)
            encoded.emplace(word.register_name,*address->actual_flow_target);
        }catch(const std::exception&){}
      }
      for(auto& permutation:permuted_deferred_natural_target_selector_allocations(
          root.artifact.items,root.artifact.preloads,flow,encoded,control,
          {.maximum_assignments=128,.maximum_candidates=8,.maximum_growth_cells=4})){
        auto next=root;next.artifact.items=std::move(permutation.items);
        for(const auto& [reg,origin]:permutation.encoded_selector_targets)next.encoded_selectors[reg]=origin;
        if(!refresh_constraint_targets(next))continue;
        const auto same=[&](const auto& old){return same_layout_search_artifact(old.artifact,next.artifact) && old.encoded_selectors==next.encoded_selectors;};
        if(std::any_of(allocations.begin(),allocations.end(),same) || std::any_of(permutations.begin(),permutations.end(),same))continue;
        next.artifact.optimizations.push_back({"deferred-complete-family-permutation",
            "Retained a proved neutral permutation of whole stable selector families independently "
            "of locally cheaper direct-flow conversions; data words and final layout remain certified."});
        permutations.push_back(std::move(next));if(permutations.size()>=8U)break;
      }
    }
    for(auto& next:permutations)allocations.push_back(std::move(next));
    if(transport_trace)std::cerr<<"[copy-transport-layout] with-codecs="<<allocations.size()<<"\n";
    for(auto node:allocations) {
      if(!refresh_constraint_targets(node))continue;
      if(transport_trace){
        const auto has=[&](std::string_view name){return std::any_of(node.artifact.optimizations.begin(),node.artifact.optimizations.end(),[&](const auto& r){return r.name==name;});};
        std::cerr<<"[copy-transport-node] cells="<<cells(node.artifact)<<" homes="<<resource_key(node).second
                 <<" pool="<<has("allocation-exposed-literal-pool")<<" remat="<<has("deferred-zero-underflow-literal")
                 <<" abi="<<has("deferred-helper-operand-abi")<<"\n";
      }
      detail::PreparedJointProofInput input(node.artifact.items,node.artifact.preloads,control);
      // Pending tail widths and selector equations must meet in the very
      // first placement attempt, not only after a gapless layout has won.
      for(const bool projected:{false,true}) {
      auto closed=close_representation_constraints(node,context,control,true,projected,nullptr,input,true);
      if(transport_trace)std::cerr<<"[copy-transport-layout] close="<<bool(closed)<<" cells="<<(closed?cells(*closed):-1)<<" flow="<<input.flow().proved<<"\n";
      if(!closed && !projected && std::any_of(node.artifact.optimizations.begin(),node.artifact.optimizations.end(),
          [](const auto& report){return report.name=="allocation-exposed-literal-pool";})) {
        // A value rewrite may make the current block order temporarily
        // impossible. Compose whole-family allocation and region sharing
        // before demanding placement; all debt belongs to the immutable root.
        JointProofPreparationCounts counts;
        const auto pending = search_joint_representation_constraints_impl(
            node.artifact, options, {},
            {.maximum_expansions=6,.maximum_candidates=24,.maximum_depth=4,.maximum_growth_cells=4},
            true, ProloguePartitionLane::Disabled, counts, {}, false, &node, cells(artifact));
        if(pending.incumbent)closed=pending.incumbent;
      }
      if(!closed)continue;
      if(hidden_setup) {
        const auto final_ops=raise_machine_to_ir(closed->items,effective_optimizer_feature_profile(options));
        std::erase_if(closed->preloads,[&](const auto& word){
          if(std::any_of(artifact.preloads.begin(),artifact.preloads.end(),[&](const auto& old){return old.register_name==word.register_name;}))return false;
          for(const auto& op:final_ops){const auto e=passes::register_effects(op);
            if(e.uses_all_registers || e.may_define_any_register || e.uses.contains(word.register_name) ||
               e.must_defs.contains(word.register_name) || e.may_defs.contains(word.register_name))return false;
          }
          return true;
        });
      }
      // Different allocation paths and projected/unprojected placements may
      // close to exactly the same artifact. Re-proving that identical root
      // spends search work but cannot discover a different continuation.
      if(std::any_of(expanded_closed_roots.begin(),expanded_closed_roots.end(),[&](const auto& old){
          return same_layout_search_artifact(old,*closed);}))continue;
      expanded_closed_roots.push_back(*closed);
      // Finish value/allocation/region sharing from this jointly closed root.
      // The established search keeps its original beam and incumbent.
      const auto search=search_joint_representation_constraints(*closed,options,{},
          {.maximum_expansions=6,.maximum_candidates=24,.maximum_depth=4,.maximum_growth_cells=4},true);
      if(search.incumbent && cells(*search.incumbent)<cells(*closed))closed=search.incumbent;
      if(cells(*closed)>=cells(artifact))continue;
      const auto same=[&](const auto& old){return same_layout_search_artifact(old.node,*closed);};
      if(std::none_of(result.begin(),result.end(),same))
        result.push_back({std::move(*closed),"contextual-copy-lifetime-codec-layout",true});
      }
    }
  }
  std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return cells(a.node)<cells(b.node);});
  return result;
}

TransactionSearchResult<LayoutSearchArtifact> search_joint_representation_constraints(
    const LayoutSearchArtifact& root, const CompileOptions& options,
    const JointRepresentationFinalizer& finalize,
    const TransactionSearchLimits& limits, bool preload_environment_complete,
    const JointRepresentationFinalizer& finalize_intermediate) {
  // A finalizer is pure within this immutable root/options invocation. Keep
  // reports in the key too: existing static gates may consume proof reports.
  const auto same_input = [](const LayoutSearchArtifact& a,const LayoutSearchArtifact& b) {
    return same_layout_search_artifact(a,b) && a.optimizations.size()==b.optimizations.size() &&
        std::equal(a.optimizations.begin(),a.optimizations.end(),b.optimizations.begin(),
            [](const auto& x,const auto& y){return x.name==y.name && x.detail==y.detail;});
  };
  ExactSearchFinalizerMemo<LayoutSearchArtifact,JointRepresentationFinalizer,decltype(same_input)>
      strict_memo(finalize,same_input,16), intermediate_memo(finalize_intermediate,same_input,16);
  const JointRepresentationFinalizer strict = finalize
      ? JointRepresentationFinalizer{[&](const auto& input){return strict_memo(input);}} : finalize;
  const JointRepresentationFinalizer intermediate = finalize_intermediate
      ? JointRepresentationFinalizer{[&](const auto& input){return intermediate_memo(input);}} : finalize_intermediate;
  JointProofPreparationCounts proof_counts;
  auto result = search_joint_representation_constraints_impl(
      root, options, strict, limits, preload_environment_complete,
      ProloguePartitionLane::Enabled, proof_counts, intermediate);
  // A new representation/layout lane cannot spend the established search's
  // beam or displace its equal-size incumbent. Its cheap demand gate names
  // only a producer/call shape, never a source function or example.
  bool unit_call_demand = false;
  for (std::size_t at = 0; at < root.items.size(); ++at) {
    if (root.items[at].kind != MachineItemKind::Op ||
        root.items[at].raw || root.items[at].opcode != 1) continue;
    auto next = at + 1U;
    if (next < root.items.size() && root.items[next].kind == MachineItemKind::Op &&
        !root.items[next].raw && root.items[next].opcode == 0x0b) ++next;
    if (next < root.items.size() && root.items[next].kind == MachineItemKind::Op) {
      const int opcode = root.items[next].opcode;
      unit_call_demand = unit_call_demand || opcode == 0x53 ||
          (opcode >= 0xa7 && opcode <= 0xae);
    }
  }
  // Producer normalization may already have erased the literal call prefix.
  // The typed producer demand and paired-input ABI remain structural evidence
  // that a joint value/lifetime/ABI frontier is useful; neither is a value fact.
  if (!unit_call_demand)
    unit_call_demand = std::any_of(root.items.begin(), root.items.end(), [](const auto& item) {
      return std::find(item.roles.begin(), item.roles.end(), kUnitProjectionDemandRole) != item.roles.end();
    });
  if (!unit_call_demand && preload_environment_complete &&
      std::any_of(root.items.begin(), root.items.end(), [](const auto& item) {
        return item.manual_interaction &&
            item.manual_interaction->kind == ManualInteractionAnchorKind::SingleStepCommand;
      })) {
    const auto abi_view = representation_view(root, options);
    unit_call_demand = abi_view &&
        !passes::idempotent_entry_prologue_register_requests(*abi_view).empty();
  }
  if (!unit_call_demand && preload_environment_complete)
    for(std::size_t i=2;i<root.items.size() && !unit_call_demand;++i){
      const auto& read=root.items[i-2U];
      const auto& neg=root.items[i-1U];
      const auto& store=root.items[i];
      if(read.kind!=MachineItemKind::Op || neg.kind!=MachineItemKind::Op ||
          store.kind!=MachineItemKind::Op || read.raw || neg.raw || store.raw ||
          read.opcode<0x60 || read.opcode>0x6e || neg.opcode!=0x0b ||
          store.opcode!=read.opcode-0x20) continue;
      const int reg=read.opcode-0x60;
      unit_call_demand=std::any_of(root.preloads.begin(),root.preloads.end(),[&](const auto& word){
        const auto home=detail::joint_origin_register(word.register_name);
        return home && *home==reg;
      });
    }
  if (preload_environment_complete && unit_call_demand && limits.maximum_expansions &&
      limits.maximum_candidates && limits.maximum_depth) {
    auto coupled = search_joint_representation_constraints_impl(
        root, options, strict, limits, preload_environment_complete,
        ProloguePartitionLane::Disabled, proof_counts, intermediate, true);
    if (coupled.incumbent && (!result.incumbent ||
        cells(*coupled.incumbent) < cells(*result.incumbent)))
      result.incumbent = std::move(coupled.incumbent);
    result.expanded += coupled.expanded;
    result.admitted += coupled.admitted;
    result.duplicate += coupled.duplicate;
    result.rejected += coupled.rejected;
    result.budget_exhausted = result.budget_exhausted || coupled.budget_exhausted;
  }
  // A finalized side-space overlay is not a terminal choice. Its certified
  // inverse has a separate bounded frontier: do not let smaller folded roots
  // starve the temporary expansion needed by new value and register choices.
  if (preload_environment_complete && limits.maximum_expansions &&
      limits.maximum_candidates && limits.maximum_depth &&
      std::any_of(root.items.begin(),root.items.end(),[](const auto& item) {
        return std::any_of(item.roles.begin(),item.roles.end(),[](const auto& role) {
          return role.starts_with("shared-helper-dual-mode-") && role.ends_with(":shared-join");
        });
      })) {
    const auto reopening_view = representation_view(root,options);
    if (reopening_view) {
      const auto symbolic_items = lower_ir_to_machine(*reopening_view);
      PostLayoutControlFlowOptions reopening_control;
      reopening_control.address_space_model = address_space_model_for_feature_profile(
          effective_optimizer_feature_profile(options));
      reopening_control.empty_return_target = 1;
      auto reopening = reopen_shared_helper_dual_mode_layout(symbolic_items,reopening_control);
      const auto encoded_origins = constraint_positions(symbolic_items);
      if (reopening.applied > 0 && encoded_origins &&
          reopening.added_cells <= limits.maximum_growth_cells) {
        ConstrainedRepresentation reopened{{std::move(reopening.items),root.preloads,root.optimizations},
            *encoded_origins,{}};
        for (const auto& report : reopening.optimizations)
          reopened.artifact.optimizations.push_back({report.name,report.detail});
        if (refresh_constraint_targets(reopened)) {
          auto continued = search_joint_representation_constraints_impl(
              reopened.artifact,options,strict,limits,true,
              ProloguePartitionLane::Disabled,proof_counts,intermediate,true,
              &reopened,cells(root));
          if (continued.incumbent && (!result.incumbent ||
              cells(*continued.incumbent) < cells(*result.incumbent)))
            result.incumbent = std::move(continued.incumbent);
          result.expanded += continued.expanded;
          result.admitted += continued.admitted;
          result.duplicate += continued.duplicate;
          result.rejected += continued.rejected;
          result.budget_exhausted = result.budget_exhausted || continued.budget_exhausted;
        }
      }
    }
  }
  if (std::getenv("MKPRO_NATIVE_TRACE_JOINT_PROOF_INPUT"))
    std::cerr << "[joint-proof-input] inputs=" << proof_counts.inputs
              << " selector-proofs=" << proof_counts.selector_proofs
              << " selector-reuses=" << proof_counts.selector_reuses
              << " finalizer-hits=" << strict_memo.hits()+intermediate_memo.hits()
              << " finalizer-evaluations=" << strict_memo.evaluations()+intermediate_memo.evaluations() << '\n';
  return result;
}


TransactionSearchResult<LayoutSearchArtifact> search_joint_representation_constraints(
    const LayoutSearchArtifact& root, const CompileOptions& options,
    const JointRepresentationFinalizer& finalize, const TransactionSearchLimits& limits,
    bool preload_environment_complete) {
  return search_joint_representation_constraints(
      root,options,finalize,limits,preload_environment_complete,{});
}

namespace passes {

PassResult joint_representation_search(const std::vector<IrOp>& ops,
                                       const PassContext& context) {
  if (!context.options.defer_return_suffix_until_callee_hole)
    return {.ops = ops};
  LayoutSearchArtifact root{.items = lower_ir_to_machine(ops)};
  using Edge = ProvedSearchTransition<LayoutSearchArtifact>;
  TransactionSearchLimits limits;
  limits.maximum_expansions = 6;
  limits.maximum_candidates = 24;
  limits.maximum_depth = 3;
  const auto search = search_proved_transactions(
      std::vector<Edge>{{root, "symbolic-region-root", true}},
      std::vector<LayoutSearchArtifact>{}, cells(root), limits, cells,
      same_layout_search_artifact,
      [&](const LayoutSearchArtifact& node) {
        auto edges = expand_joint_representation_transactions(node, context.options, false);
        // Region extraction can repay a neutral/larger representation or a
        // register recoloring. It must see each node, not only the local winner.
        if (auto child = apply(node, callee_hole_straight_line_helper(
                raise_machine_to_ir(node.items, effective_optimizer_feature_profile(context.options)),
                context)))
          edges.push_back({std::move(*child), "callee-hole-region-extraction", true});
        return edges;
      },
      [&](const LayoutSearchArtifact& node, const std::vector<std::string>&)
          -> std::optional<LayoutSearchArtifact> {
        auto rebound = rebind_layout_search_artifact(node);
        if (!rebound)
          return std::nullopt;
        PostLayoutControlFlowOptions options;
        options.address_space_model = address_space_model_for_feature_profile(
            effective_optimizer_feature_profile(context.options));
        if (!build_post_layout_control_flow(rebound->items, options).proved ||
            !callee_hole_return_stack_fits(raise_machine_to_ir(
                rebound->items, effective_optimizer_feature_profile(context.options))))
          return std::nullopt;
        return rebound;
      });
  if (!search.incumbent)
    return {.ops = ops};
  PassResult result;
  result.ops = raise_machine_to_ir(search.incumbent->items,
                                  effective_optimizer_feature_profile(context.options));
  result.preloads = search.incumbent->preloads;
  result.applied = 1;
  for (const auto& report : search.incumbent->optimizations)
    result.optimizations.push_back({report.name, report.detail});
  result.optimizations.push_back({
      .name = "joint-representation-region-search",
      .detail = "Composed data/address synthesis, web recoloring and multi-entry region "
                "sharing before physical layout: " + std::to_string(cells(root)) + " -> " +
                std::to_string(cells(*search.incumbent)) + " cells; expanded " +
                std::to_string(search.expanded) + " proof-valid nodes. The ordinary compiler "
                "candidate remains an independent incumbent."});
  return result;
}

IrPass joint_representation_search_pass() {
  return {.name = "joint-representation-region-search", .run = joint_representation_search,
          .layout_safe = false};
}

} // namespace passes
} // namespace mkpro::core
