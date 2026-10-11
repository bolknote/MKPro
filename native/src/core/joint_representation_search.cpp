#include "mkpro/core/joint_representation_search.hpp"

#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/passes/common_processor_factoring.hpp"
#include "mkpro/core/passes/indirect_selector_seed_reuse.hpp"
#include "mkpro/core/passes/register_coalesce.hpp"
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
    const LayoutSearchArtifact& artifact, const CompileOptions& options) {
  const auto reject = [](std::string_view reason) -> std::optional<std::vector<IrOp>> {
    if (std::getenv("MKPRO_NATIVE_TRACE_FINITE_MODE_REPRESENTATION"))
      std::cerr << "[finite-mode-discovery] representation-view rejected: " << reason << '\n';
    return std::nullopt;
  };
  const auto model = address_space_model_for_feature_profile(effective_optimizer_feature_profile(options));
  auto normalized = normalize_natural_target_overflow_formals(artifact.items, model);
  if (!normalized || has_executable_address_words(*normalized))
    return reject("unsupported overflow or executable address contract");
  const auto bound = rebind_late_bound_decimal_selectors(
      *normalized, {.minimum_target_address = 0, .maximum_target_address = 99});
  if (!bound.diagnostics.empty() || bound.items.size() != normalized->size())
    return reject(bound.diagnostics.empty() ? "selector binding changed item count"
                                            : bound.diagnostics.front().message);
  for (std::size_t i = 0; i < bound.items.size(); ++i)
    if (bound.items[i].opcode != (*normalized)[i].opcode)
      return reject("stale executable selector charge");
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
      return role.starts_with("finalization-cell-origin:");
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

std::optional<LayoutSearchArtifact> close_representation_constraints(
    const ConstrainedRepresentation& node, const passes::PassContext& context,
    const PostLayoutControlFlowOptions& control_options, bool complete_preloads,
    bool project_shared_tail = false,
    const passes::DeferredSelectorDataLiteralLayout* pending_literal = nullptr) {
  const auto& input = pending_literal ? pending_literal->items : node.artifact.items;
  const auto flow = build_post_layout_control_flow(input, control_options);
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
  std::map<std::string, int> fixed;
  if (pending_literal)
    fixed.emplace(pending_literal->target_label, pending_literal->literal_value);
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
            rebind_stable_preloaded_indirect_flow_selector(
                input, node.artifact.preloads[preload->second], flow,
                selector.old_target, probe, control_options.address_space_model,
                &node.artifact.preloads)) {
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
      if (!rebind_stable_preloaded_indirect_flow_selector(
              input, preload, flow, selector.old_target, last,
              control_options.address_space_model, &node.artifact.preloads))
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
  if (!placed)
    return std::nullopt;
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
    const auto value = rebind_stable_preloaded_indirect_flow_selector(
        input, preload, flow, selector.old_target, *address,
        control_options.address_space_model, &node.artifact.preloads);
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

TransactionSearchResult<LayoutSearchArtifact> search_joint_representation_constraints(
    const LayoutSearchArtifact& root, const CompileOptions& options,
    const JointRepresentationFinalizer& finalize,
    const TransactionSearchLimits& limits, bool preload_environment_complete) {
  TransactionSearchResult<LayoutSearchArtifact> output;
  CompileOptions configured = options;
  configured.defer_return_suffix_until_callee_hole = true;
  configured.coalesce_copies = true;
  const passes::PassContext context{configured};
  PostLayoutControlFlowOptions control_options;
  control_options.address_space_model = address_space_model_for_feature_profile(
      effective_optimizer_feature_profile(options));
  if (preload_environment_complete)
    control_options.empty_return_target = 1;

  auto root_ops = preload_environment_complete ? representation_view(root, options)
      : std::optional{raise_machine_to_ir(root.items,
                                         effective_optimizer_feature_profile(options))};
  if (!root_ops || !build_post_layout_control_flow(
          lower_ir_to_machine(*root_ops), control_options).proved)
    return output;
  ConstrainedRepresentation initial{root, {}};
  initial.artifact.items = lower_ir_to_machine(*root_ops);
  if (!refresh_constraint_targets(initial))
    return output;
  using Edge = ProvedSearchTransition<ConstrainedRepresentation>;
  const auto expand = [&](const ConstrainedRepresentation& parent) {
    std::vector<Edge> edges;
    const auto ops = raise_machine_to_ir(parent.artifact.items,
                                         effective_optimizer_feature_profile(options));
    const auto add = [&](passes::PassResult proposal, const char* rule,
                         const std::map<std::string, int>* bindings = nullptr) {
      auto child = apply(parent.artifact, std::move(proposal));
      if (!child)
        return;
      ConstrainedRepresentation next{std::move(*child),
          bindings ? *bindings : parent.targets, parent.encoded_selectors};
      if (refresh_constraint_targets(next) &&
          (!same_layout_search_artifact(parent.artifact, next.artifact) ||
           parent.targets != next.targets))
        edges.push_back({std::move(next), rule, true});
    };
    for (auto& proposal : passes::deferred_finite_mode_representation_candidates(
             ops, context, {
                 .maximum_growth_cells = std::min(4, limits.maximum_growth_cells),
                 .maximum_candidates = 6, .allow_target_relayout = true,
                 .runtime_preloads = preload_environment_complete
                     ? &parent.artifact.preloads : nullptr,
                 .preserve_compiler_selector_charges = preload_environment_complete,
                 .assumed_target_addresses = &parent.targets}))
      add(std::move(proposal.transformation), "representation-equations",
          &proposal.required_target_addresses);

    const auto requests = passes::semantic_region_register_requests(ops, true);
    passes::for_each_register_web_recoloring(ops, context, requests,
        [&](std::vector<IrOp> recolored) {
          add({.ops = std::move(recolored), .applied = 1,
               .optimizations = {{"joint-register-web-equations",
                   "Solved interference-checked operand equalities without committing "
                   "a representation or physical layout winner."}}},
              "register-web-equations");
        });
    add(passes::semantic_region_folding_with_deferred_layout(
            ops, context, control_options), "region-equations");
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
              cells(root), cells(*child), limits.maximum_growth_cells))
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
              cells(root), cells(*child), limits.maximum_growth_cells))
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
  // Recompute web interference after representation and whole-family choices.
  // Keep the original closure first: a local copy saving must not replace a
  // neutral variant whose geometry wins only after shared-tail projection.
  // Selector digits still belong to their encoded carrier, not its provisional
  // destination. Conflicting origins cannot certify a pruned execution graph.
  const auto coalesced_variants = [&](const ConstrainedRepresentation& node) {
    std::vector<ConstrainedRepresentation> variants{node};
    if (!preload_environment_complete) return variants;
    auto origins = node.targets;
    std::map<std::string, int> overrides;
    for (const auto& item : node.artifact.items) {
      if (!item.indirect_flow_targets || charged_constraint_consumer(item)) continue;
      const int reg = item.opcode & 15;
      if (reg < 7 || reg > 14) continue;
      const std::string name(1, "0123456789abcde"[reg]);
      if (!node.encoded_selectors.contains(name)) continue;
      for (const auto& target : *item.indirect_flow_targets) {
        const auto* label = std::get_if<std::string>(&target);
        if (!label) return variants;
        const int origin = node.encoded_selectors.at(name);
        const auto [old, inserted] = overrides.emplace(*label, origin);
        if (!inserted && old->second != origin) return variants;
      }
    }
    for (const auto& [label, origin] : overrides) origins[label] = origin;
    auto copied = passes::deferred_register_web_copy_coalesce(
        raise_machine_to_ir(node.artifact.items,
            effective_optimizer_feature_profile(options)),
        context, control_options, origins);
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
                 context, control_options, origins,
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
      for (const bool projected : {false, true}) {
        auto closed = close_representation_constraints(
            node, context, control_options, true, projected, &pending);
        if (!closed) continue;
        // A temporarily larger placement can still repay its bounded debt in
        // finalization. Only the complete artifact competes with incumbents.
        if (finalize) closed = finalize(*closed);
        if (closed && cells(*closed) < cells(root) &&
            (!data_control_incumbent || cells(*closed) < cells(*data_control_incumbent)))
          data_control_incumbent = std::move(*closed);
      }
    }
  };

  const auto close_node = [&](const ConstrainedRepresentation& node, const std::vector<std::string>& path)
          -> std::optional<ConstrainedRepresentation> {
        std::optional<ConstrainedRepresentation> best;
        // Each closure owns its downstream finalization. A smaller intermediate
        // must not hide a different placement that wins only after composition.
        const auto variants = coalesced_variants(node);
        for (const auto& variant : variants) for (const bool projected : {false, true}) {
          auto closed = close_representation_constraints(
              variant, context, control_options, preload_environment_complete, projected);
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
          if (finalize) closed = finalize(*closed);
          if (!closed) continue;
          ConstrainedRepresentation result{std::move(*closed), {}};
          if (refresh_constraint_targets(result) &&
              (!best || cells(result.artifact) < cells(best->artifact)))
            best = std::move(result);
        }
        for (const auto& variant : variants) close_data_control_literals(variant);
        return best;
      };
  const auto search = search_proved_transactions(
      std::move(seeds), std::vector<ConstrainedRepresentation>{initial}, cells(root),
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
        unrotated_allocation_roots, allocation_roots, cells(root), product_limits,
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
    const std::size_t cap = std::min<std::size_t>(24, limits.maximum_candidates);
    std::size_t roots = 0;
    for (const auto& source : allocation_roots) {
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
      const auto allocations = deferred_natural_target_selector_allocations(
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
                cells(root), cells(allocated.artifact), limits.maximum_growth_cells)) {
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
                      cells(root), cells(*artifact), limits.maximum_growth_cells))
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
        for (const auto& variant : variants)
          for (bool projected : {false, true}) {
            auto closed = close_representation_constraints(
                variant, context, control_options, true, projected);
            // Geometry alone is not a saving. Avoid running the full compiler
            // finalizer on an unpaid release/bridge, even if its CFG is valid.
            if (!closed || cells(*closed) >= cells(root)) continue;
            closed->optimizations.push_back({"joint-selector-allocation-layout",
                "Closed complete-family allocation, region geometry and pending continuation "
                "sharing atomically; independently checked delivered selectors and callbacks."});
            if (finalize) closed = finalize(*closed);
            if (closed && cells(*closed) < cells(root) &&
                (!output.incumbent || cells(*closed) < cells(*output.incumbent)))
              output.incumbent = std::move(*closed);
          }
        for (const auto& variant : variants) close_data_control_literals(variant);
      }
    }
  }
  if (!projection_seeds.empty()) {
    const auto projection_search = search_proved_transactions(
        std::move(projection_seeds), original_allocation_roots, cells(root),
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
  if (!topology_seeds.empty()) {
    auto topology_limits = limits;
    topology_limits.maximum_expansions = std::min<std::size_t>(4, limits.maximum_expansions);
    topology_limits.maximum_candidates = std::min<std::size_t>(24, limits.maximum_candidates);
    topology_limits.maximum_depth = std::min<std::size_t>(3, limits.maximum_depth);
    const auto topology_search = search_proved_transactions(
        std::move(topology_seeds), unrotated_allocation_roots, cells(root),
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
  // The input itself may already own a valid literal family, without an
  // unrelated representation rewrite to serve as an artificial search seed.
  close_data_control_literals(initial);
  if (data_control_incumbent &&
      (!output.incumbent || cells(*data_control_incumbent) < cells(*output.incumbent)))
    output.incumbent = std::move(*data_control_incumbent);
  return output;
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
