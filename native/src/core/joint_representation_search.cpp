#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/compiler.hpp"
#include "mkpro/core/joint_proof_input.hpp"
#include "mkpro/core/joint_constraint_origins.hpp"
#include "mkpro/core/detail/cell_provenance.hpp"
#include "mkpro/core/detail/layout_jobs.hpp"
#include "mkpro/core/post_layout_packed_sign.hpp"
#include "mkpro/core/order_reduction_translation.hpp"

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
#include "mkpro/core/post_layout_indirect_flow.hpp"
#include "mkpro/core/shared_helper_dual_mode_layout.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <exception>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <set>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace mkpro::core {
namespace {

thread_local std::size_t contextual_negative_cache_hits = 0;
thread_local std::size_t contextual_completed_cache_hits = 0;
thread_local std::size_t joint_completed_cache_hits = 0;
thread_local std::size_t joint_literal_closure_reuses = 0;

std::string representation_context_key(const CompileOptions& options) {
  // These fields are consumed only by source/profile generation in
  // compiler.cpp, never by the machine-IR proof/placement dependencies here.
  // Their effects already belong to the complete typed artifact in the key.
  // Keep every other option, including physical reservations, model, input
  // policy and actual fast-frontier selection. Do not apply this projection
  // to the source compiler cache or to searches with arbitrary callbacks.
  auto frozen = options;
  const CompileOptions defaults;
  frozen.alternating_sign_toggle_args = defaults.alternating_sign_toggle_args;
  frozen.zero_underflow_optimizer_root = defaults.zero_underflow_optimizer_root;
  frozen.fast_candidate_threshold_ms = defaults.fast_candidate_threshold_ms;
  frozen.forced_logical_register_assignments.clear();
  frozen.forced_register_shares.clear();
  frozen.collect_logical_register_allocation = defaults.collect_logical_register_allocation;
  frozen.compact_logical_register_allocation = defaults.compact_logical_register_allocation;
  frozen.repair_logical_register_assignment_preferences = defaults.repair_logical_register_assignment_preferences;
  frozen.preserve_logical_register_instruction_classes = defaults.preserve_logical_register_instruction_classes;
  frozen.logical_register_dead_store_elimination = defaults.logical_register_dead_store_elimination;
  frozen.stack_argument_helper_entries = defaults.stack_argument_helper_entries;
  frozen.single_x_expression_helper_entries = defaults.single_x_expression_helper_entries;
  frozen.general_constant_preloads = defaults.general_constant_preloads;
  frozen.disable_candidate_search = defaults.disable_candidate_search;
  return compiler_options_fingerprint(frozen);
}

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
  std::shared_ptr<const ClosedNumericLayoutCertificate> numeric_layout_certificate = {};
};

// A lifetime equality is useful even when no preceding copy transaction gave
// its store a provenance tag. Prefer recent definition epochs fairly across
// homes and hardware classes; these are requests, never alias/death facts.
std::vector<passes::RegisterWebRecoloringRequest> hardware_epoch_storage_requests(
    const std::vector<IrOp>& code, std::size_t maximum_requests = 32) {
  std::vector<passes::RegisterWebRecoloringRequest> result;
  if (!maximum_requests) return result;
  std::map<std::string, std::vector<std::size_t>> epochs;
  std::vector<std::size_t> anchors;
  std::size_t depth_count = 0;
  constexpr std::string_view homes = "0123456789abcde";
  for (std::size_t at = 0; at < code.size(); ++at) {
    const auto& op = code[at];
    if (op.kind != IrKind::Store || op.opcode < 0x40 || op.opcode > 0x4e ||
        op.register_name != std::string(1, homes[static_cast<std::size_t>(op.opcode - 0x40)]) ||
        op.meta.raw || op.meta.tactic || op.meta.logical_register_analysis ||
        op.procedure_boundary || (op.meta.manual_interaction &&
            op.meta.manual_interaction->kind != ManualInteractionAnchorKind::SingleStepCommand))
      continue;
    auto& definitions = epochs[op.register_name];
    definitions.push_back(at);
    depth_count = std::max(depth_count, definitions.size());
  }
  for (const auto kind : {IrKind::IndirectStore, IrKind::IndirectRecall, IrKind::Loop})
    for (std::size_t at = 0; at < code.size(); ++at)
      if (code[at].kind == kind && !code[at].meta.raw && !code[at].meta.tactic &&
          !code[at].meta.logical_register_analysis && !code[at].meta.manual_interaction)
        anchors.push_back(at);
  for (std::size_t depth = 0; depth < depth_count; ++depth)
    for (const auto anchor : anchors)
      for (const auto& [home, definitions] : epochs) {
        (void)home;
        if (depth >= definitions.size()) continue;
        result.push_back({{definitions[definitions.size() - 1U - depth], anchor}});
        if (result.size() == maximum_requests) return result;
      }
  return result;
}

// Private to this transaction: created only after the relational reduction
// theorem has proved this complete source, never reconstructed from a role.
struct OrderReductionLiteralDomain {
  std::string family_id;
  int original_seed = -1;
  int minimum_seed = -1;
  int maximum_seed = -1;
  bool operator==(const OrderReductionLiteralDomain&) const = default;
};

using LiteralLayout = passes::DeferredSelectorDataLiteralLayout;

auto joint_preload_fields(const PreloadReport& p) {
  return std::tie(p.register_name, p.value, p.counts_against_program,
      p.retunable_natural_fractional_prefix, p.setup_target_name, p.setup_expression,
      p.setup_expression_text, p.setup_source_line, p.lowered_data_value);
}

bool same_joint_preloads(const std::vector<PreloadReport>& a, const std::vector<PreloadReport>& b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const auto& left, const auto& right) {
    return joint_preload_fields(left) == joint_preload_fields(right);
  });
}

std::size_t mix_joint_key(std::size_t seed, std::size_t value) {
  return (seed ^ value) * 1099511628211ULL;
}

// Only a bucket index: every hit below compares the owned semantic input.
std::size_t joint_item_key(const std::vector<MachineItem>& items) {
  std::size_t key = items.size();
  for (const auto& item : items) {
    key = mix_joint_key(key, static_cast<std::size_t>(item.kind));
    key = mix_joint_key(key, static_cast<std::size_t>(item.opcode));
    key = mix_joint_key(key, std::hash<IrTarget>{}(item.target));
    if (item.kind == MachineItemKind::Label)
      key = mix_joint_key(key, std::hash<std::string>{}(item.name));
  }
  return key;
}

bool same_joint_limits(const TransactionSearchLimits& a, const TransactionSearchLimits& b) {
  return a.maximum_expansions == b.maximum_expansions &&
      a.maximum_candidates == b.maximum_candidates && a.maximum_depth == b.maximum_depth &&
      a.maximum_growth_cells == b.maximum_growth_cells &&
      a.prioritize_composition_depth == b.prioritize_composition_depth;
}

bool same_joint_equations(const ConstrainedRepresentation& a, const ConstrainedRepresentation& b) {
  return a.targets == b.targets && a.encoded_selectors == b.encoded_selectors &&
      a.numeric_layout_certificate == b.numeric_layout_certificate;
}

// Fix candidate order at the caller. Neither a cache hit, a racing miss nor
// eviction may consume a search slot. Only independent nonrecursive
// subproofs can wait on an identical in-flight proof.
template <class Key, class Value>
class JointWorkMemo {
  struct Entry {
    std::size_t hash;
    Key key;
    std::shared_ptr<const Value> value;
    std::exception_ptr error;
    std::atomic<bool> ready{false};

    Entry(std::size_t signature, Key input)
        : hash(signature), key(std::move(input)) {}
  };
  struct Shard {
    std::mutex mutex;
    std::unordered_multimap<std::size_t, std::shared_ptr<Entry>> entries;
    std::deque<std::shared_ptr<Entry>> completed;

    void erase(const Entry& entry) {
      const auto [begin, end] = entries.equal_range(entry.hash);
      for (auto it = begin; it != end; ++it) {
        if (it->second.get() == &entry) {
          entries.erase(it);
          break;
        }
      }
    }
  };

 public:
  explicit JointWorkMemo(std::size_t per_shard) : per_shard_(per_shard) {}

  template <class Match, class Capture, class Compute>
  std::shared_ptr<const Value> get(std::size_t hash, Match match, Capture capture, Compute compute) {
    auto& shard = shards_[hash % shards_.size()];
    std::shared_ptr<Entry> entry;
    {
      std::unique_lock lock(shard.mutex);
      const auto [begin, end] = shard.entries.equal_range(hash);
      for (auto it = begin; it != end; ++it) {
        if (!match(it->second->key)) continue;
        auto found = it->second; // the build may finish and be evicted while we wait
        lock.unlock();
        if (!found->ready.load(std::memory_order_acquire)) {
          waits_.fetch_add(1, std::memory_order_relaxed);
          found->ready.wait(false, std::memory_order_acquire);
        }
        if (found->error) std::rethrow_exception(found->error);
        hits_.fetch_add(1, std::memory_order_relaxed);
        return found->value;
      }
      entry = std::make_shared<Entry>(hash, capture());
      shard.entries.emplace(hash, entry);
    }
    builds_.fetch_add(1, std::memory_order_relaxed);
    try {
      auto value = std::make_shared<const Value>(compute());
      std::shared_ptr<Entry> retired;
      {
        const std::lock_guard lock(shard.mutex);
        shard.completed.push_back(entry);
        entry->value = value;
        if (shard.completed.size() > per_shard_) {
          retired = std::move(shard.completed.front());
          shard.erase(*retired);
          shard.completed.pop_front();
        }
      }
      entry->ready.store(true, std::memory_order_release);
      entry->ready.notify_all();
      return value;
    } catch (...) {
      {
        const std::lock_guard lock(shard.mutex);
        entry->error = std::current_exception();
        shard.erase(*entry);
      }
      entry->ready.store(true, std::memory_order_release);
      entry->ready.notify_all();
      throw;
    }
  }

  detail::JointLayoutWorkCounts counts() const {
    return {builds_.load(), hits_.load(), waits_.load()};
  }

 private:
  std::array<Shard, 8> shards_;
  const std::size_t per_shard_;
  std::atomic<std::size_t> builds_{0}, hits_{0}, waits_{0};
};

struct JointVariantsKey {
  ConstrainedRepresentation node;
  std::string options;
  TransactionSearchLimits limits;
  bool coupled_geometry;
};

struct JointLiteralKey {
  std::vector<MachineItem> items;
  PostLayoutControlFlowOptions control;
  std::size_t limit;
};

struct JointClosureKey {
  ConstrainedRepresentation proof;
  PostLayoutControlFlowOptions control;
  FeatureProfile profile;
  bool decimal_input, complete_preloads, projected, precise_entry;
  std::optional<LiteralLayout> literal;
  std::optional<OrderReductionLiteralDomain> domain;
  // A negative proof below this bound also excludes any tighter request.
  // The bound never changes the chosen geometry or the successful artifact.
  std::optional<int> result_limit;
};

struct JointClosureProof {
  std::optional<LayoutSearchArtifact> artifact;
  int layout_width = 0;
};

struct JointOrderProofKey {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
  std::shared_ptr<const AuthoritativePostLayoutControlFlow> flow;
  NumericRangeInputPolicy policy;
  std::size_t first, last;
};

bool same_order_proof_items(const std::vector<MachineItem>& a,
                            const std::vector<MachineItem>& b) {
  // This cache returns only a numeric interval or a rejection. Neither
  // that proof nor its transitive dependencies read source presentation
  // or pure cell-origin annotations. Keep every semantic role and target.
  const auto fields = [](const MachineItem& item) {
    return std::tie(item.kind, item.name, item.procedure_boundary,
        item.procedure_name, item.opcode, item.raw, item.stop_disposition,
        item.manual_interaction, item.indirect_flow_targets,
        item.indirect_flow_formal_targets, item.indirect_memory_targets,
        item.logical_register_name, item.logical_indirect_memory_targets,
        item.discarded_indirect_recall_value, item.borrowed_entry_phase_selector,
        item.semantic_call_origins, item.target, item.formal_opcode);
  };
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), [&](const auto& x, const auto& y) {
    if (fields(x) != fields(y)) return false;
    auto left = x.roles.begin(), right = y.roles.begin();
    for (;;) {
      while (left != x.roles.end() && detail::is_cell_origin_provenance(*left)) ++left;
      while (right != y.roles.end() && detail::is_cell_origin_provenance(*right)) ++right;
      if (left == x.roles.end() || right == y.roles.end())
        return left == x.roles.end() && right == y.roles.end();
      if (*left++ != *right++) return false;
    }
  });
}

} // namespace

namespace detail {

struct JointLayoutWork {
  JointWorkMemo<JointVariantsKey, std::vector<ConstrainedRepresentation>> variants{96};
  JointWorkMemo<JointLiteralKey, std::vector<LiteralLayout>> literals{128};
  JointWorkMemo<JointClosureKey, JointClosureProof> closures{96};
  JointWorkMemo<JointOrderProofKey, std::optional<OrderReductionTranslationProof>> order_proofs{16};
};

std::shared_ptr<JointLayoutWork> make_joint_layout_work() {
  return std::make_shared<JointLayoutWork>();
}

JointLayoutWorkStats joint_layout_work_stats(const JointLayoutWork& work) {
  return {work.variants.counts(), work.literals.counts(), work.closures.counts(),
          work.order_proofs.counts()};
}

} // namespace detail

namespace {

struct JointLiteralLayouts {
  std::shared_ptr<const std::vector<LiteralLayout>> layouts;
  auto begin() const { return layouts->begin(); }
  auto end() const { return layouts->end(); }
};

JointLiteralLayouts cached_literal_layouts(const std::vector<MachineItem>& items,
    const PostLayoutControlFlowOptions& control, std::size_t limit) {
  auto* work = detail::active_joint_layout_work.get();
  const auto compute = [&] {
    return passes::prepare_deferred_selector_data_literal_layouts(items, control, limit);
  };
  if (!limit || std::none_of(items.begin(), items.end(), [](const auto& item) {
        return std::any_of(item.roles.begin(), item.roles.end(), [](const auto& role) {
          return role.starts_with("selector-sunk-literal-id:");
        });
      })) {
    static const auto empty = std::make_shared<const std::vector<LiteralLayout>>();
    return {empty};
  }
  if (!work || items.size() > 512U)
    return {std::make_shared<const std::vector<LiteralLayout>>(compute())};
  auto& memo = work->literals;
  return {memo.get(mix_joint_key(joint_item_key(items), limit),
      [&](const JointLiteralKey& key) {
        return key.limit == limit && key.control == control && same_layout_search_items(key.items, items);
      }, [&] { return JointLiteralKey{items, control, limit}; }, compute)};
}

// A newly normalized identity root is not permission to replay every older
// portfolio. Admit it for this compound family's actual producers/resources.
// Historical symbolic identity roots and real transformations are unaffected.
bool contextual_identity_expansion_demand(const std::vector<IrOp>& code,
    const std::vector<PreloadReport>& preloads, const CompileOptions& options,
    const AuthoritativePostLayoutControlFlow* flow = nullptr) {
  for (std::size_t at = 0; at < code.size(); ++at) {
    const auto& op = code[at];
    for (const auto& role : op.meta.roles)
      if (role.starts_with(detail::kContextualCopyStoreOriginPrefix) &&
          detail::is_cell_origin_provenance(role)) return true;
    if (at + 1U < code.size() && op.kind == IrKind::IndirectRecall &&
        op.opcode >= 0xd0 && op.opcode <= 0xd3 && !op.meta.raw &&
        code[at+1U].kind == IrKind::Store && !code[at+1U].meta.raw &&
        op.register_name != code[at+1U].register_name) return true;
    if (at + 2U < code.size() && op.kind == IrKind::Recall && !op.meta.raw &&
        code[at+1U].kind == IrKind::Plain && code[at+1U].opcode == 0x0b &&
        !code[at+1U].meta.raw && code[at+2U].kind == IrKind::Store &&
        !code[at+2U].meta.raw && code[at+2U].register_name == op.register_name &&
        std::any_of(preloads.begin(), preloads.end(), [&](const auto& word) {
          return word.register_name == op.register_name;
        })) return true;
  }
  std::set<std::string> occupied;
  for (const auto& word : preloads) occupied.insert(word.register_name);
  for (const auto& [home, word] : options.preloaded_constant_registers) {
    (void)word;
    occupied.insert(home);
  }
  bool poolable_literal = false;
  std::size_t digits = 0;
  std::size_t machine = 0;
  for (const auto& op : code) {
    const auto site = machine;
    machine += lower_ir_to_machine({op}).size();
    if (op.meta.raw) return true;
    // This is a whole-program physical footprint, not a callee summary.
    // Ordinary call bodies are scanned independently below; the control
    // instruction itself touches only its explicit selector, if any.
    if (op.kind == IrKind::Call || op.kind == IrKind::Return ||
        op.kind == IrKind::Jump || op.kind == IrKind::CondJump) {
      digits = 0;
      continue;
    }
    if (op.kind == IrKind::IndirectCall || op.kind == IrKind::IndirectJump ||
        op.kind == IrKind::IndirectCondJump) {
      if (op.register_name.empty()) return true;
      occupied.insert(op.register_name);
      digits = 0;
      continue;
    }
    if (op.kind == IrKind::Stop && std::find(op.meta.roles.begin(), op.meta.roles.end(),
            kTypedDisplayObservationRole) != op.meta.roles.end()) {
      digits = 0;
      continue;
    }
    if (op.kind == IrKind::Store || op.kind == IrKind::Recall || op.kind == IrKind::Loop) {
      if (op.register_name.empty()) return true;
      occupied.insert(op.register_name);
      digits = 0;
      continue;
    }
    if (op.kind == IrKind::IndirectStore || op.kind == IrKind::IndirectRecall) {
      if (op.register_name.empty()) return true;
      occupied.insert(op.register_name);
      auto aliases = op.meta.indirect_memory_targets;
      if (flow && flow->proved) {
        const auto proved = flow->indirect_memory_targets.find(site);
        if (proved != flow->indirect_memory_targets.end()) aliases = proved->second;
      }
      if (!aliases || aliases->empty()) return true;
      for (const auto home : *aliases) {
        if (home < 0 || home > 14) return true;
        occupied.insert(std::string(1, "0123456789abcde"[home]));
      }
      digits = 0;
      continue;
    }
    // An unknown physical footprint is not evidence that storage is full.
    // Leave it to the existing exact proof instead of pruning such a root.
    if (op.kind != IrKind::Plain && op.kind != IrKind::Label) {
      const auto effect = passes::register_effects(op);
      if (effect.uses_all_registers || effect.may_define_any_register) return true;
      occupied.insert(effect.uses.begin(), effect.uses.end());
      occupied.insert(effect.must_defs.begin(), effect.must_defs.end());
      occupied.insert(effect.may_defs.begin(), effect.may_defs.end());
    }
    const bool digit = op.kind == IrKind::Plain && !op.meta.raw && op.opcode >= 0 &&
        op.opcode <= 9 && !op.meta.manual_interaction &&
        std::all_of(op.meta.roles.begin(), op.meta.roles.end(), detail::is_cell_origin_provenance);
    digits = digit ? digits + 1U : 0U;
    poolable_literal = poolable_literal || digits >= 2U;
  }
  constexpr std::string_view homes = "0123456789abcde";
  return poolable_literal && std::any_of(homes.begin(), homes.end(), [&](char home) {
        return !occupied.contains(std::string(1, home));
      });
}

// Resolve a hardware-home move and its displaced ordinary epoch as one
// preference cycle. Connected first-store products can miss the later epoch
// that actually occupies the requested home; no interference is assumed here.
std::vector<passes::RegisterWebRecoloringRequest> hardware_home_eviction_requests(
    const std::vector<IrOp>& code,
    const std::vector<passes::RegisterWebRecoloringRequest>& primary,
    const std::vector<passes::RegisterWebRecoloringRequest>& copies,
    const std::map<std::string, std::vector<std::size_t>>& epochs,
    std::size_t maximum_requests = 32) {
  std::vector<passes::RegisterWebRecoloringRequest> result;
  if (!maximum_requests || primary.empty() || copies.empty()) return result;
  std::size_t maximum_depth = 0;
  for (const auto& [home, definitions] : epochs) {
    (void)home;
    maximum_depth = std::max(maximum_depth, definitions.size());
  }
  for (std::size_t diagonal = 0;
       diagonal < primary.size() + copies.size() + maximum_depth && result.size() < maximum_requests;
       ++diagonal)
    for (std::size_t p = 0; p < primary.size() && p <= diagonal && result.size() < maximum_requests; ++p) {
      if (primary[p].empty()) continue;
      const auto [producer, anchor] = primary[p].front();
      if (producer >= code.size() || anchor >= code.size()) continue;
      const auto& old_home = code[anchor].register_name;
      if (old_home.empty() || code[producer].register_name == old_home) continue;
      const auto occupants = epochs.find(old_home);
      if (occupants == epochs.end()) continue;
      for (std::size_t c = 0; c < copies.size() && p + c <= diagonal && result.size() < maximum_requests; ++c) {
        if (copies[c].empty()) continue;
        const auto [read, destination] = copies[c].front();
        if (read >= code.size() || destination >= code.size() || code[read].register_name == old_home)
          continue;
        const auto depth = diagonal - p - c;
        if (depth >= occupants->second.size()) continue;
        const auto displaced = occupants->second[occupants->second.size() - 1U - depth];
        if (displaced >= code.size() || code[displaced].meta.raw) continue;
        auto request = primary[p];
        request.insert(request.end(), copies[c].begin(), copies[c].end());
        request.push_back({displaced, destination});
        if (std::find(result.begin(), result.end(), request) == result.end())
          result.push_back(std::move(request));
      }
    }
  return result;
}

// Storage affinities only: the web allocator still proves interference and
// every indirect effect. A self-indexed destructive reload followed by a
// direct copy is a useful hardware home even when it is not a plain Recall.
std::vector<passes::RegisterWebRecoloringRequest> self_reload_copy_requests(
    const std::vector<IrOp>& code, const PostLayoutControlFlowOptions& control) {
  std::vector<passes::RegisterWebRecoloringRequest> result;
  const auto items = lower_ir_to_machine(code);
  const auto flow_owner = shared_post_layout_control_flow(items, control);
  const auto& flow = *flow_owner;
  if (!flow.proved) return result;
  std::size_t item = 0;
  for (std::size_t i = 0; i + 1U < code.size() && result.size() < 8U; ++i) {
    const auto& read = code[i]; const auto& store = code[i+1U];
    const int reg = read.opcode & 15;
    const auto memory = flow.indirect_memory_targets.find(item);
    if (read.kind == IrKind::IndirectRecall && read.opcode >= 0xd0 && read.opcode <= 0xd3 &&
        store.kind == IrKind::Store && store.opcode >= 0x40 && store.opcode <= 0x4e &&
        (store.opcode & 15) != reg && !read.meta.raw && !store.meta.raw &&
        !read.meta.manual_interaction && !store.meta.manual_interaction &&
        memory != flow.indirect_memory_targets.end() && memory->second == std::vector<int>{reg})
      result.push_back({{i,i+1U}});
    item += lower_ir_to_machine({read}).size();
  }
  return result;
}

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

std::optional<ConstrainedRepresentation> contextual_transport_root(
    const LayoutSearchArtifact& parent, const std::vector<MachineItem>& symbolic,
    const std::map<std::string, int>& positions, const passes::PassResult& proposal,
    bool normalize_identity = true) {
  auto child = proposal.applied > 0 ? apply(parent, proposal)
                                   : std::optional<LayoutSearchArtifact>{parent};
  if (!child) return std::nullopt;
  // A neutral edge still belongs to the already validated symbolic view.
  // Restoring the delivered numeric operands here loses their command
  // identities and rejects the root before any allocation/layout can run.
  if (proposal.applied <= 0 && normalize_identity) child->items = symbolic;
  ConstrainedRepresentation root{std::move(*child), positions};
  if (!refresh_constraint_targets(root)) return std::nullopt;
  return root;
}

bool charged_constraint_consumer(const MachineItem& item) {
  return std::find(item.roles.begin(), item.roles.end(),
                   "late-decimal-selector-consumer") != item.roles.end();
}

std::optional<ConstrainedRepresentation> reopened_allocation_topology(
    const ConstrainedRepresentation& root, const PostLayoutControlFlowOptions& control,
    int maximum_growth_cells) {
  const auto opened = reopen_shared_helper_dual_mode_layout(root.artifact.items, control);
  if (opened.applied <= 0 || !opened.continuation.proved ||
      opened.added_cells > maximum_growth_cells)
    return std::nullopt;
  auto next = root;
  next.artifact.items = opened.items;
  for (const auto& report : opened.optimizations)
    next.artifact.optimizations.push_back({report.name, report.detail});
  // The inverse moves commands, not the words that already encode their old
  // addresses. Keep those bindings as equations until the final placement.
  if (!refresh_constraint_targets(next)) return std::nullopt;
  next.artifact.optimizations.push_back({"allocation-before-continuation-layout",
      "Reopened a fully proved shared continuation before rebuilding physical definition webs. "
      "Retained every encoded target origin and charged all duplicated cells to the same "
      "transaction; neither the enlarged root nor a locally cheaper coloring is publishable."});
  return next;
}

// Identical V/O instructions have no data effects, but their incoming edges
// and return frames are not interchangeable by assumption. Keep each proposed
// many-to-one command relocation only after the rooted labelled proof accepts
// it. Addresses here are conditional identities; the owner must close the
// unchanged selector words before this becomes executable output.
std::vector<ConstrainedRepresentation> pool_addressed_return_targets(
    const ConstrainedRepresentation& parent,
    const PostLayoutControlFlowOptions& control,
    std::size_t maximum_candidates = 4) {
  std::vector<ConstrainedRepresentation> result;
  const auto& items = parent.artifact.items;
  const auto plain = [](const MachineItem& item) {
    return item.kind == MachineItemKind::Op && item.opcode == 0x52 &&
        !item.raw && !item.manual_interaction && !item.procedure_boundary &&
        !item.formal_opcode && item.semantic_call_origins.empty() &&
        std::all_of(item.roles.begin(), item.roles.end(), [](const auto& role) {
          return detail::is_cell_origin_provenance(role) ||
              role == "empty-return-startup-edge";
        });
  };
  std::vector<std::size_t> returns;
  for (std::size_t i = 0; i < items.size(); ++i)
    if (plain(items[i])) returns.push_back(i);
  if (returns.size() < 2 || maximum_candidates == 0) return result;
  const auto flow = build_post_layout_control_flow(items, control);
  const auto external = resolve_post_layout_external_entry_contexts(flow);
  if (!flow.proved || !external ||
      flow.execution_edges.size() != flow.execution_states.size()) return result;
  std::map<std::size_t, std::vector<std::size_t>> aliases;
  std::vector<std::size_t> pending;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (items[i].kind == MachineItemKind::Label) pending.push_back(i);
    else { aliases.emplace(i, std::move(pending)); pending.clear(); }
  }
  for (const auto removed : returns) {
    bool reached = false, safe = true;
    for (const auto label : aliases.at(removed)) {
      const auto& item = items[label];
      safe = safe && !item.procedure_boundary && !item.manual_interaction &&
          !item.raw && item.roles.empty() && item.semantic_call_origins.empty();
    }
    for (std::size_t s = 0; s < flow.execution_states.size(); ++s) {
      const auto& state = flow.execution_states[s];
      if (state.item_index == removed) {
        reached = true;
        safe = safe && !external->external[s];
      }
      if (state.operand_item_index == removed) safe = false;
      for (const auto& edge : flow.execution_edges[s]) {
        if (edge.target_state >= flow.execution_states.size()) return {};
        if (flow.execution_states[edge.target_state].item_index != removed) continue;
        safe = safe && (edge.kind == PostLayoutExecutionEdgeKind::DirectTarget ||
                        edge.kind == PostLayoutExecutionEdgeKind::IndirectTarget);
      }
    }
    if (!reached || !safe) continue;
    for (const auto retained : returns) {
      if (retained == removed) continue;
      std::vector<bool> move(items.size(), false);
      for (const auto label : aliases.at(removed)) move[label] = true;
      std::vector<MachineItem> rewritten;
      std::vector<std::optional<std::size_t>> mapping(items.size());
      for (std::size_t i = 0; i < items.size(); ++i) {
        if (i == removed || move[i]) continue;
        if (i == retained)
          for (const auto label : aliases.at(removed)) {
            mapping[label] = rewritten.size();
            rewritten.push_back(items[label]);
          }
        mapping[i] = rewritten.size();
        rewritten.push_back(items[i]);
      }
      mapping[removed] = mapping[retained];
      const auto after = build_post_layout_control_flow(rewritten, control);
      const auto proof = prove_post_layout_execution_relocation(
          items, rewritten, flow, after, mapping);
      if (!proof.proved) continue;
      auto next = parent;
      next.artifact.items = std::move(rewritten);
      if (!refresh_constraint_targets(next)) continue;
      next.artifact.optimizations.push_back({"joint-addressed-return-pooling",
          "Shared an independently addressed plain V/O with an existing identical return; "
          "proved the complete many-to-one rooted command relocation, labelled branches, "
          "external entries, formal counters and every saved return frame. No stack, "
          "X1/X2 or memory operation changed. Selector words and final placement remain "
          "separate mandatory obligations."});
      result.push_back(std::move(next));
      if (result.size() == maximum_candidates) return result;
    }
  }
  return result;
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
  // Keep historical high homes first; a genuinely unused low home is also
  // legal for direct data. The complete alias/setup proof below is unchanged.
  for(const int r:{7,8,9,10,11,12,13,14,0,1,2,3,4,5,6}){const std::string name(1,"0123456789abcde"[r]);
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
using detail::BorrowedJointProofInput;
using detail::JointProofPreparationCounts;

bool literal_family_owns_charges(
    const passes::DeferredSelectorDataLiteralLayout& preparation,
    const std::vector<MachineItem>& input) {
  const auto carrier=detail::joint_origin_register(preparation.selector_register);
  if (!carrier || *carrier<7 || *carrier>14) return false;
  const std::set<std::string, std::less<>> certified_targets(
      preparation.charge_targets.begin(), preparation.charge_targets.end());
  if (!certified_targets.contains(preparation.target_label)) return false;
  constexpr std::string_view owner_prefix = "late-decimal-selector-register:";
  constexpr std::array<std::string_view, 3> charge_prefixes{
      "late-decimal-selector-high:", "late-decimal-selector-low:", "late-decimal-selector-single:"};
  for (const auto& item:input) {
    const bool names_target = std::any_of(item.roles.begin(), item.roles.end(), [&](std::string_view role) {
      for (const auto prefix : charge_prefixes)
        if (role.starts_with(prefix) &&
            certified_targets.contains(role.substr(prefix.size()))) return true;
      return false;
    });
    if (!names_target) continue;
    int owners=0;
    for (const std::string_view role : item.roles)
      if (role.starts_with(owner_prefix)) {
        if (role.substr(owner_prefix.size()) != preparation.selector_register) return false;
        ++owners;
      }
    if (owners!=1) return false;
  }
  return true;
}

// Projecting both ordinary tails removes at most six cells and replaces one
// return by at least three (four in the six-cell case). A divergent operand
// or padding can only increase the size. See shared_helper_dual_mode_projected_widths.
constexpr int kSharedTailMaximumSaving = 3;

constexpr int kJointMaximumRegionPadding = 16;

struct JointSelectorConstraints {
  struct Selector {
    int old_target = -1;
    std::set<std::string> labels;
    bool flexible = true;
  };
  std::map<std::string, std::size_t> preload_by_register;
  std::map<std::string, Selector> selectors;
  std::map<std::string, int> fixed;
};

std::optional<JointSelectorConstraints> prepare_joint_selector_constraints(
    const ConstrainedRepresentation& node, PreparedJointProofInput& proof_input,
    bool complete_preloads, bool precise_entry_facts, const LiteralLayout* pending_literal,
    bool parametric_literal) {
  const auto& input = proof_input.items();
  const auto& flow = proof_input.flow();
  if (!flow.proved) return std::nullopt;
  JointSelectorConstraints result;
  auto& preload_by_register = result.preload_by_register;
  auto& selectors = result.selectors;
  for (std::size_t i = 0; i < node.artifact.preloads.size(); ++i)
    if (!preload_by_register.emplace(node.artifact.preloads[i].register_name, i).second)
      return std::nullopt;

  const auto pending_origins = precise_entry_facts
      ? detail::joint_target_origins(input,node.targets,node.encoded_selectors)
      : std::optional<detail::JointOriginMap>{};
  if (precise_entry_facts && !pending_origins) return std::nullopt;
  const auto& protected_charges = precise_entry_facts
      ? proof_input.precise_runtime_charge_targets(*pending_origins)
      : proof_input.protected_runtime_charge_targets();
  if (!protected_charges) {
    if(std::getenv("MKPRO_NATIVE_TRACE_COPY_TRANSPORT"))std::cerr<<"[copy-transport-layout] protected charge preparation rejected\n";
    return std::nullopt;
  }
  auto& fixed = result.fixed;
  fixed = *protected_charges;
  if (pending_literal) {
    // This target is intentionally data, not an arbitrary private word.
    // Discharge only the owned family through its per-mode entry theorem;
    // the data target keeps an exact literal equation. Re-prove after finalization.
    if (!literal_family_owns_charges(*pending_literal,input)) return std::nullopt;
    for (const auto& certified_target:pending_literal->charge_targets) fixed.erase(certified_target);

    if (!parametric_literal) {
      const auto [binding,inserted] = fixed.emplace(
          pending_literal->target_label,pending_literal->literal_value);
      if (!inserted && binding->second != pending_literal->literal_value) return std::nullopt;
    }
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
  return result;
}

std::optional<std::vector<int>> region_widths_for_machine_projection(
    const std::vector<IrOp>& ops, const std::vector<int>& widths) {
  std::vector<int> result;
  result.reserve(ops.size());
  std::size_t item = 0;
  for (const auto& op : ops) {
    const auto length = op.kind == IrKind::Label
        ? 1U : static_cast<std::size_t>(passes::cells_per_op(op));
    if (item + length > widths.size()) return std::nullopt;
    int width = 0;
    for (std::size_t cell = 0; cell < length; ++cell) width += widths[item++];
    result.push_back(width);
  }
  if (item != widths.size()) return std::nullopt;
  return result;
}

std::optional<LayoutSearchArtifact> close_representation_constraints_uncached(
    const ConstrainedRepresentation& node, const passes::PassContext& context,
    const PostLayoutControlFlowOptions& control_options, bool complete_preloads,
    bool project_shared_tail,
    const passes::DeferredSelectorDataLiteralLayout* pending_literal,
    PreparedJointProofInput& proof_input, bool precise_entry_facts,
    const OrderReductionLiteralDomain* literal_domain,
    std::optional<int> exclusive_result_limit, int* proved_layout_width = nullptr,
    std::optional<SharedHelperDualModePreparation>* prepared_tail = nullptr,
    const JointSelectorConstraints* prepared_bindings = nullptr) {
  if (literal_domain && (!pending_literal ||
      literal_domain->family_id != pending_literal->family_id ||
      literal_domain->original_seed != pending_literal->literal_value ||
      literal_domain->minimum_seed < 10 || literal_domain->maximum_seed > 99 ||
      literal_domain->minimum_seed > literal_domain->maximum_seed))
    return std::nullopt;
  const auto& input = proof_input.items();
  // Ordinary placement only permutes cells and optionally adds padding. A
  // caller that cannot use a neutral result need not prove such a placement.
  if (exclusive_result_limit && !project_shared_tail &&
      machine_cell_count(input) >= *exclusive_result_limit)
    return std::nullopt;
  const auto profile = effective_optimizer_feature_profile(context.options);
  const bool conditional_underflow = std::any_of(input.begin(),input.end(),[](const auto& item) {
    return std::find(item.roles.begin(),item.roles.end(),"zero-underflow-conditional-return")!=item.roles.end();
  });
  const bool complete_formal = complete_preloads && std::any_of(input.begin(), input.end(),
      [](const auto& item) { return item.formal_opcode.has_value(); });
  const int maximum_padding = precise_entry_facts || conditional_underflow || complete_formal
      ? kJointMaximumRegionPadding : 0;
  std::optional<std::vector<IrOp>> ordinary_ir;
  if (pending_literal && !project_shared_tail) {
    ordinary_ir = raise_machine_to_ir(input, profile);
    const int first = literal_domain ? literal_domain->minimum_seed : pending_literal->literal_value;
    const int last = literal_domain ? literal_domain->maximum_seed : pending_literal->literal_value;
    if (!passes::unprojected_symbolic_region_target_may_fit(
            *ordinary_ir, pending_literal->target_label, first, last, maximum_padding))
      return std::nullopt;
  }
  const auto& flow = proof_input.flow();
  if (!flow.proved)
    return std::nullopt;
  const auto positions = constraint_positions(input);
  if (!positions)
    return std::nullopt;
  std::optional<SharedHelperDualModePreparation> preparation;
  std::optional<std::vector<int>> projected_widths;
  if (project_shared_tail) {
    if (!complete_preloads) return std::nullopt;
    if (prepared_tail && *prepared_tail)
      preparation = std::move(*prepared_tail);
    else
      preparation = prepare_shared_helper_dual_mode_layout(
          input, flow, control_options.address_space_model);
    if (preparation->applied <= 0) return std::nullopt;
    projected_widths = shared_helper_dual_mode_projected_widths(*preparation);
    if (!projected_widths || (exclusive_result_limit &&
        std::accumulate(projected_widths->begin(), projected_widths->end(), 0) >=
            *exclusive_result_limit))
      return std::nullopt;
  }
  std::optional<JointSelectorConstraints> owned;
  if (!prepared_bindings) {
    owned = prepare_joint_selector_constraints(node, proof_input, complete_preloads,
        precise_entry_facts, pending_literal, literal_domain != nullptr);
    if (!owned) return std::nullopt;
    prepared_bindings = &*owned;
  }
  const auto& preload_by_register = prepared_bindings->preload_by_register;
  const auto& selectors = prepared_bindings->selectors;
  auto fixed = owned ? std::move(owned->fixed) : prepared_bindings->fixed;
  // A single constraint solution sees every codec's bounds and every remaining
  // immutable selector. No individual transformation chooses the block order.
  const auto& neutral_items = preparation ? preparation->items : input;
  const auto neutral_ops = ordinary_ir ? std::move(*ordinary_ir)
                                      : raise_machine_to_ir(neutral_items, profile);
  std::optional<passes::SymbolicRegionProjection> projection;
  if (node.numeric_layout_certificate) {
    if (!node.numeric_layout_certificate->matches(neutral_items, node.artifact.preloads,
        control_options.address_space_model, context.options.ordinary_decimal_keyboard_input)) {
      if(std::getenv("MKPRO_NATIVE_TRACE_JOINT_COMPOSITION"))
        std::cerr<<"[joint-composition] numeric-domain expired; shared-preparation="<<bool(preparation)<<'\n';
      return std::nullopt;
    }
    projection.emplace();
    projection->numeric_layout_certificate = node.numeric_layout_certificate;
    projection->numeric_layout_preloads = &node.artifact.preloads;
  }
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
  if (literal_domain) {
    if (!projection) projection.emplace();
    const auto interval = std::pair{literal_domain->minimum_seed,
                                    literal_domain->maximum_seed};
    auto [bound, inserted] = projection->target_bounds.emplace(
        pending_literal->target_label, interval);
    if (!inserted) {
      bound->second.first = std::max(bound->second.first, interval.first);
      bound->second.second = std::min(bound->second.second, interval.second);
      if (bound->second.first > bound->second.second) return std::nullopt;
    }
  }
  if (precise_entry_facts || conditional_underflow) {
    if (!projection) projection.emplace();
    projection->defer_indirect_entries = true;
    projection->maximum_padding_cells = maximum_padding;
    projection->preserve_formal_execution_frames = true;
    projection->relocate_canonical_formal_frames = complete_preloads;
  }
  if (complete_formal) {
    if (!projection) projection.emplace();
    projection->preserve_formal_execution_frames = true;
    projection->relocate_canonical_formal_frames = true;
    projection->maximum_padding_cells = maximum_padding;
  }
  if (preparation) {
    if (!projection) projection.emplace();
    auto widths = region_widths_for_machine_projection(neutral_ops, *projected_widths);
    if (!widths) return std::nullopt;
    projection->cell_widths = std::move(*widths);
    const auto [entry, inserted] = fixed.emplace(
        preparation->continuation.helper_label, preparation->required_final_start_address);
    if (!inserted && entry->second != preparation->required_final_start_address)
      return std::nullopt;

  } else if (projection) {
    for (const auto& op : neutral_ops) projection->cell_widths.push_back(passes::cells_per_op(op));
  }
  // Every flexible preload has an encoding domain, not only those in a
  // projected shared-tail transaction. Rejecting the first unencodable order
  // after placement hides other valid component orders. Put all domains in
  // the same geometry problem; the independent official-BCD fallback below
  // widens only preload families that actually certify that larger domain.
  for (const auto& [name, selector] : selectors) {
    if (!selector.flexible) continue;
    if (!projection) {
      projection.emplace();
      for (const auto& op : neutral_ops) projection->cell_widths.push_back(passes::cells_per_op(op));
    }
    const auto& preload = node.artifact.preloads[preload_by_register.at(name)];
    const int minimum = preload.retunable_natural_fractional_prefix &&
        preload.retunable_natural_fractional_prefix->starts_with("0.") ? 1 : 0;
    for (const auto& label : selector.labels) {
      auto [bound, fresh] = projection->target_bounds.emplace(label, std::pair{minimum, 99});
      if (!fresh) {
        bound->second.first = std::max(bound->second.first, minimum);
        bound->second.second = std::min(bound->second.second, 99);
      }
    }
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
  // Do not cap the geometry solver itself: a valid padded placement stops
  // the old solver before its widened-domain retry. Reject only that chosen
  // result, in lanes that would already reject it before any continuation.
  if (proved_layout_width) *proved_layout_width = placed->projected_cells;
  if (exclusive_result_limit && placed->projected_cells >= *exclusive_result_limit)
    return std::nullopt;
  LayoutSearchArtifact output = node.artifact;
  output.items = lower_ir_to_machine(placed->ops);
  std::optional<passes::DeferredSelectorDataLiteralLayout> selected_literal;
  if (literal_domain) {
    const auto address = placed->projected_targets.find(pending_literal->target_label);
    if (address == placed->projected_targets.end() ||
        address->second < literal_domain->minimum_seed ||
        address->second > literal_domain->maximum_seed) return std::nullopt;
    selected_literal = *pending_literal;
    selected_literal->literal_value = address->second;
    const std::string identity = "selector-sunk-literal-id:" + literal_domain->family_id;
    const std::string old_value = "selector-sunk-literal-value:" +
        std::to_string(literal_domain->original_seed);
    std::size_t owners = 0;
    for (auto& item : output.items) {
      if (std::find(item.roles.begin(), item.roles.end(), identity) == item.roles.end()) continue;
      const auto value = std::find(item.roles.begin(), item.roles.end(), old_value);
      if (value == item.roles.end() || ++owners != 1U) return std::nullopt;
      *value = "selector-sunk-literal-value:" + std::to_string(address->second);
      if (std::find(item.roles.begin(), item.roles.end(), "order-only-reduction-rebased") ==
          item.roles.end()) item.roles.push_back("order-only-reduction-rebased");
    }
    if (owners != 1U) return std::nullopt;
    // All later geometry/data checks use the chosen value, not the origin.
    pending_literal = &*selected_literal;
    output.optimizations.push_back({"joint-parametric-order-reduction-layout",
        "Selected private reduction seed " + std::to_string(address->second) +
        " from the freshly proved interval " + std::to_string(literal_domain->minimum_seed) +
        ".." + std::to_string(literal_domain->maximum_seed) +
        " in the same minimum-cell layout that places its callback. Original seed " +
        std::to_string(literal_domain->original_seed) +
        "; retained all rounding, tie, future-read and complete-family obligations. "
        "The strict final checker must still prove the selected data/address equality."});
  }
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
        *shared_post_layout_control_flow(output.items, control_options),
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
  const auto final_flow = shared_post_layout_control_flow(rebound->items, control_options);
  if (!final_flow->proved || (pending_literal &&
      !passes::prove_deferred_selector_data_literal_layout(
          *pending_literal, rebound->items, *final_flow)))
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

std::optional<LayoutSearchArtifact> close_representation_constraints(
    const ConstrainedRepresentation& node, const passes::PassContext& context,
    const PostLayoutControlFlowOptions& control, bool complete_preloads,
    bool projected, const LiteralLayout* literal, PreparedJointProofInput& proof,
    bool precise_entry = false, const OrderReductionLiteralDomain* domain = nullptr,
    std::optional<int> result_limit = std::nullopt,
    std::optional<SharedHelperDualModePreparation>* prepared_tail = nullptr,
    const JointSelectorConstraints* prepared_bindings = nullptr) {
  const auto compute = [&](const ConstrainedRepresentation& source,
                           int* proved_layout_width = nullptr) {
    return close_representation_constraints_uncached(source, context, control,
        complete_preloads, projected, literal, proof, precise_entry, domain, result_limit,
        proved_layout_width, prepared_tail, prepared_bindings);
  };
  auto* work = detail::active_joint_layout_work.get();
  const auto& items = proof.items();
  if (!work || items.size() > 512U || proof.options() != control)
    return compute(node);
  // A cheap structural rejection is not worth owning a full proof key.
  if ((result_limit && machine_cell_count(items) -
          (projected ? kSharedTailMaximumSaving : 0) >= *result_limit) ||
      (projected && (!complete_preloads || !has_shared_helper_continuation_shape(items))))
    return std::nullopt;
  const auto profile = effective_optimizer_feature_profile(context.options);
  const auto decimal = context.options.ordinary_decimal_keyboard_input;
  // The input carried by PreparedJointProofInput must describe the same
  // preload environment as the equations being solved. If a caller carries
  // a different proof environment, run it without sharing.
  if (&proof.preloads() != &node.artifact.preloads &&
      !same_joint_preloads(proof.preloads(), node.artifact.preloads))
    return compute(node);
  const auto same_literal = [&](const std::optional<LiteralLayout>& other) {
    return other.has_value() == (literal != nullptr) && (!literal ||
        std::tie(other->family_id, other->selector_register, other->target_label,
                 other->literal_value, other->removed_cells, other->charge_targets) ==
        std::tie(literal->family_id, literal->selector_register, literal->target_label,
                 literal->literal_value, literal->removed_cells, literal->charge_targets));
  };
  const auto hash = mix_joint_key(joint_item_key(items), projected ? 1U : 0U);
  const auto result = work->closures.get(hash,
      [&](const JointClosureKey& key) {
        return key.control == control && key.profile == profile && key.decimal_input == decimal &&
            key.complete_preloads == complete_preloads && key.projected == projected &&
            key.precise_entry == precise_entry &&
            (!key.result_limit || (result_limit && *result_limit <= *key.result_limit)) &&
            key.domain.has_value() == (domain != nullptr) && (!domain || *key.domain == *domain) &&
            same_literal(key.literal) && same_joint_equations(key.proof, node) &&
            same_joint_preloads(key.proof.artifact.preloads, node.artifact.preloads) &&
            same_layout_search_items(key.proof.artifact.items, items);
      }, [&] {
        auto certificate = literal ? std::optional{*literal} : std::nullopt;
        if (certificate) certificate->items.clear(); // the owned proof items are the actual image
        return JointClosureKey{{{items, node.artifact.preloads, {}}, node.targets,
                    node.encoded_selectors, node.numeric_layout_certificate}, control, profile,
                decimal, complete_preloads, projected, precise_entry, std::move(certificate),
                domain ? std::optional{*domain} : std::nullopt, result_limit};
      }, [&] {
        // No report is read inside this proof. Keep only its newly proved
        // suffix; the caller's report path is added before its finalizer.
        ConstrainedRepresentation source{{{}, node.artifact.preloads, {}},
            node.targets, node.encoded_selectors, node.numeric_layout_certificate};
        JointClosureProof proved;
        proved.artifact = compute(source, &proved.layout_width);
        return proved;
      });
  if (!result->artifact || (result_limit && result->layout_width >= *result_limit))
    return std::nullopt;
  auto owned = *result->artifact;
  owned.optimizations.insert(owned.optimizations.begin(),
      node.artifact.optimizations.begin(), node.artifact.optimizations.end());
  return owned;
}

} // namespace

namespace {

// Fuse a zero-tested rematerialization with its returning error path. This is
// a conditional layout transaction: K x!=0 R3 decrements a canonical zero to
// -99999999 and branches to 99 without recalling the word into X. The caller
// must close the new return address and every displaced preload atomically.
std::optional<ConstrainedRepresentation> zero_underflow_conditional_return(
    const ConstrainedRepresentation& parent, const passes::PassContext& context,
    const PostLayoutControlFlowOptions& control) {
  if(control.address_space_model!=AddressSpaceModel::Standard ||
      std::none_of(parent.artifact.items.begin(),parent.artifact.items.end(),[](const auto& item){
        return item.kind==MachineItemKind::Op && item.opcode==0xd3;
      }))return std::nullopt;
  const auto ops=raise_machine_to_ir(parent.artifact.items,effective_optimizer_feature_profile(context.options));
  const auto input=lower_ir_to_machine(ops);
  const auto full_owner=shared_post_layout_control_flow(input,control);
  const auto& full=*full_owner;
  const auto origins=detail::joint_target_origins(input,parent.targets,parent.encoded_selectors);
  if(!full.proved || !origins)return std::nullopt;
  const auto selected=shared_deferred_selector_consistent_execution_graph(input,full_owner,*origins,20000,true);
  const auto data_owner=shared_post_layout_projected_flow(full_owner,selected);
  if(!data_owner)return std::nullopt;
  const auto& data=*data_owner;
  const auto entries=resolve_post_layout_external_entry_contexts(full);
  if(!entries)return std::nullopt;
  std::vector<std::size_t> machine;std::map<std::size_t,std::size_t> ir_at;
  std::map<std::string,std::size_t> labels;std::size_t item=0;
  for(std::size_t i=0;i<ops.size();++i){machine.push_back(item);ir_at.emplace(item,i);
    if(ops[i].kind==IrKind::Label)labels.emplace(ops[i].name,i);
    item+=ops[i].kind==IrKind::Label?1U:static_cast<std::size_t>(passes::cells_per_op(ops[i]));}
  const auto next=[&](std::size_t i){do{++i;}while(i<ops.size() && ops[i].kind==IrKind::Label);return i;};
  const auto plain=[](const auto& op, bool empty_return=false){return !op.meta.raw && !op.meta.manual_interaction &&
      !op.meta.logical_register_analysis && !op.meta.tactic && !op.procedure_boundary &&
      !op.target_meta.formal_opcode && op.target_meta.roles.empty() &&
      std::all_of(op.meta.roles.begin(),op.meta.roles.end(),[&](const auto& role){
        return detail::is_cell_origin_provenance(role) ||
            (empty_return && role=="empty-return-startup-edge");
      });};
  std::vector<std::vector<std::size_t>> predecessors(full.execution_states.size());
  for(std::size_t s=0;s<full.execution_successors.size();++s)
    for(auto n:full.execution_successors[s])predecessors[n].push_back(s);
  for(std::size_t branch=1;branch<ops.size();++branch){
    if(ops[branch].kind!=IrKind::CondJump || ops[branch].opcode!=0x57 || !plain(ops[branch]) ||
        ops[branch-1U].kind!=IrKind::Plain || ops[branch-1U].opcode!=0x11 || !plain(ops[branch-1U]))continue;
    const auto* target=std::get_if<std::string>(&ops[branch].target);
    if(!target || !labels.contains(*target))continue;
    auto store=labels.at(*target);while(store<ops.size() && ops[store].kind==IrKind::Label)++store;
    if(store>=ops.size())continue;const auto recall=next(store);const auto ret=next(recall);
    if(ret>=ops.size() || ops[store].kind!=IrKind::Store || ops[store].opcode!=0x43 ||
        ops[recall].kind!=IrKind::IndirectRecall || ops[recall].opcode!=0xd3 ||
        ops[ret].kind!=IrKind::Return || ops[ret].opcode!=0x52 ||
        !plain(ops[store]) || !plain(ops[recall]) || !plain(ops[ret],control.empty_return_target.has_value()))continue;
    const auto aliases=full.indirect_memory_targets.find(machine[recall]);
    if(aliases==full.indirect_memory_targets.end() || aliases->second!=std::vector<int>{3})continue;
    bool safe=true,reached=false;
    for(std::size_t s=0;s<full.execution_states.size();++s){
      const auto at=full.execution_states[s].item_index;
      if(at!=machine[branch] && at!=machine[store] && at!=machine[recall] && at!=machine[ret])continue;
      if(entries->external[s] || predecessors[s].empty() || (full.execution_states[s].formal_opcode &&
          std::any_of(ops.begin(),ops.end(),[](const auto& op){return op.target_meta.formal_opcode.has_value();})))safe=false;
      const auto expected=at==machine[branch]?machine[branch-1U]:at==machine[store]?machine[branch]:
          at==machine[recall]?machine[store]:machine[recall];
      for(auto p:predecessors[s])if(full.execution_states[p].item_index!=expected)safe=false;
      reached=reached || at==machine[store];
    }
    if(!safe || !reached)continue;
    passes::LivenessOptions lifetime;lifetime.discover_toggle_projection_signs=true;
    // Source provenance reserves ownership; it does not erase an actual literal
    // entry fact. Only generated-word retuning uses joint_literal_word.
    for(const auto& word:parent.artifact.preloads)if(!word.counts_against_program &&
        !word.setup_target_name && !word.setup_expression && !word.setup_expression_text)
      lifetime.equal_entry_value_classes.emplace(word.register_name,"literal:"+word.value);
    const passes::GuardedOldWordQuery query{branch,"3"};
    const auto death=passes::analyze_dead_old_words(ops,data,{query},lifetime,400000);
    if(death.exhausted || !death.dead_words.contains(query))continue;
    std::vector<std::size_t> normal,returns;
    for(std::size_t s=0;s<data.execution_states.size();++s){
      if(data.execution_states[s].item_index==machine[branch])for(const auto& edge:data.execution_edges[s])
        if(edge.kind==PostLayoutExecutionEdgeKind::Fallthrough)normal.push_back(edge.target_state);
      if(data.execution_states[s].item_index==machine[ret])returns.push_back(s);
    }
    if(normal.empty() || returns.empty() ||
        !prove_post_layout_stack_state_equality(input,data,normal,{{true,true,true,true},false,true},
            {.refine_runtime_selectors=false,.numeric_entry_closed=true,.transport_closed_entry_through_control=true}) ||
        !prove_post_layout_stack_state_equality(input,data,returns,{{false,false,false,false},false,false},
            {.refine_runtime_selectors=false,.numeric_entry_closed=true,.transport_closed_entry_through_control=true}))continue;
    std::string return_label="__zero_underflow_conditional_return_"+std::to_string(branch);
    while(labels.contains(return_label))return_label+='_';
    std::vector<IrOp> rewritten;
    for(std::size_t i=0;i<ops.size();++i){
      if(i==store || i==recall)continue;
      if(i==branch){
        auto seed=make_store("3");seed.meta.source_line=ops[i].meta.source_line;rewritten.push_back(std::move(seed));
        auto jump=MachineItem::op(0x73,opcode_by_code(0x73).name);
        jump.indirect_flow_targets=std::vector<IrTarget>{return_label};
        jump.roles.push_back("zero-underflow-conditional-return");jump.source_line=ops[i].meta.source_line;
        auto code=raise_machine_to_ir({jump},effective_optimizer_feature_profile(context.options));
        rewritten.insert(rewritten.end(),code.begin(),code.end());
      }else{
        if(i==ret){auto label=IrOp{};label.kind=IrKind::Label;label.name=return_label;rewritten.push_back(std::move(label));}
        rewritten.push_back(ops[i]);
      }
    }
    auto next_node=parent;next_node.artifact.items=lower_ir_to_machine(rewritten);
    const auto positions=constraint_positions(next_node.artifact.items);if(!positions)continue;
    // This node is conditional, not executable. Preserve every delivered
    // preload and its encoded origin until one common final placement exists.
    // Retuning against this smaller intermediate geometry would reject a
    // data-bearing selector that the final layout can keep byte-identical.
    next_node.targets[return_label]=99;
    if(!refresh_constraint_targets(next_node) || !build_post_layout_control_flow(next_node.artifact.items,control).proved)continue;
    next_node.artifact.optimizations.push_back({"zero-underflow-conditional-return",
        "Fused a normalized zero branch and private Store-R3/D3/return into Store-R3; "
        "K x!=0 R3. Proved every predecessor, dead old R3, both hidden-state continuations "
        "and exact underflow writeback. Preserved delivered preload words and all encoded "
        "origin equations for one independent final placement, including return 99."});
    return next_node;
  }
  return std::nullopt;
}

// A separate closure family: changing a private reduction seed may enable
// address/data sharing only after placement. Never let these alternatives
// consume the historical beam or replace its equal-size winner.
std::optional<ConstrainedRepresentation> reopen_order_reduction_input(
    const ConstrainedRepresentation& node,std::size_t store,const std::string& literal,
    const PostLayoutControlFlowOptions& control) {
  const auto& items=node.artifact.items;
  const auto next_command=[&](std::size_t at) {
    do{++at;}while(at<items.size() && items[at].kind==MachineItemKind::Label);
    return at;
  };
  const auto first=next_command(store),last=next_command(first);
  if(last<items.size() && items[first].kind==MachineItemKind::Op &&
      items[last].kind==MachineItemKind::Op && !items[first].raw && !items[last].raw &&
      items[first].opcode==literal[0]-'0' && items[last].opcode==literal[1]-'0')return node;
  constexpr std::string_view id_prefix="selector-sunk-literal-id:";
  std::string family;
  for(const auto& role:items[store].roles)if(role.starts_with(id_prefix)) {
    if(!family.empty())return std::nullopt;
    family=role.substr(id_prefix.size());
  }
  if(family.empty())return std::nullopt;
  auto opened=node;
  std::erase(opened.artifact.items[store].roles,std::string("selector-sunk-literal-coalesced"));
  std::vector<MachineItem> digits;
  for(const char digit:literal)digits.push_back(MachineItem::op(digit-'0',std::string(1,digit)));
  opened.artifact.items.insert(opened.artifact.items.begin()+static_cast<std::ptrdiff_t>(store+1U),
                              digits.begin(),digits.end());
  auto coalesced=items;
  std::erase(coalesced[store].roles,std::string("selector-sunk-literal-coalesced"));
  coalesced[store].roles.push_back("selector-sunk-literal-coalesced");
  const auto flow=build_post_layout_control_flow(coalesced,control);
  if(!flow.proved)return std::nullopt;
  bool inverse=false;
  for(const auto& pending:cached_literal_layouts(
          opened.artifact.items,control,4))
    if(pending.family_id==family && passes::prove_deferred_selector_data_literal_layout(
          pending,coalesced,flow))inverse=true;
  if(!inverse || !refresh_constraint_targets(opened))return std::nullopt;
  opened.artifact.optimizations.push_back({"reopened-order-data-address-entry",
      "Reconstructed a compiler-owned implicit data/address input by the inverse "
      "complete-family entry theorem. Preserved every mode, labelled entry, stack, "
      "X1/X2 and matched return; the temporary literal debt must be repaid by final layout."});
  return opened;
}

struct OrderReductionInput {
  ConstrainedRepresentation source;
  std::size_t store = 0, first = 0, last = 0;
  std::string family_id;
  int seed = 0;
};

std::shared_ptr<const std::optional<OrderReductionTranslationProof>> prove_order_reduction_shape(
    const std::vector<MachineItem>& input, const std::vector<PreloadReport>& preloads,
    const std::shared_ptr<const AuthoritativePostLayoutControlFlow>& projection,
    NumericRangeInputPolicy policy, std::size_t first, std::size_t last) {
  using Proof = std::optional<OrderReductionTranslationProof>;
  const auto& flow = *projection;
  const auto compute = [&]() -> Proof {
    const auto lift = analyze_post_layout_automatic_entry_lift_with_indirect_store_facts(input, flow, 200000);
    if (!lift) return std::nullopt;
    if (order_reduction_translation_is_structurally_impossible(input, flow, *lift, first, last))
      return std::nullopt;
    std::vector<std::size_t> queries;
    for (std::size_t at = 0; at < input.size(); ++at)
      if (input[at].kind == MachineItemKind::Op) queries.push_back(at);
    const auto bounds = prove_post_layout_bounded_stack_numeric_values(
        input, flow, *lift, queries, preloads, 2000000, policy);
    return prove_order_reduction_translation(input, flow, *lift, bounds, first, last);
  };
  auto* work = detail::active_joint_layout_work.get();
  const char* numeric_cache = std::getenv("MKPRO_NATIVE_NUMERIC_PROOF_CACHE");
  // Retain only the compact relational answer, not the bounded-product graph
  // or its numeric facts. Own the immutable input as long as it is cached.
  if (!work || input.size() > 512U || flow.execution_states.size() > 2048U ||
      (numeric_cache && std::string_view(numeric_cache) == "0"))
    return std::make_shared<const Proof>(compute());
  auto key = mix_joint_key(joint_item_key(input), first);
  key = mix_joint_key(key, last);
  key = mix_joint_key(key, static_cast<std::size_t>(policy));
  key = mix_joint_key(key, flow.execution_states.size());
  for (const auto& word : preloads) {
    key = mix_joint_key(key, std::hash<std::string>{}(word.register_name));
    key = mix_joint_key(key, std::hash<std::string>{}(word.value));
  }
  return work->order_proofs.get(key, [&](const JointOrderProofKey& entry) {
    return entry.first == first && entry.last == last && entry.policy == policy &&
        same_joint_preloads(entry.preloads, preloads) &&
        same_order_proof_items(entry.items, input) &&
        (entry.flow == projection || *entry.flow == flow);
  }, [&] { return JointOrderProofKey{input, preloads, projection, policy, first, last}; }, compute);
}

std::vector<OrderReductionInput> order_reduction_inputs(
    const ConstrainedRepresentation& node, const CompileOptions& options,
    const PostLayoutControlFlowOptions& control, bool reconsider_rebased = false) {
  std::vector<OrderReductionInput> result;
  if (!options.ordinary_decimal_keyboard_input && std::any_of(node.artifact.items.begin(),
      node.artifact.items.end(), [](const auto& item) { return item.manual_interaction.has_value(); }))
    return result;
  // The relational proof requires an actually used typed Kmax. Reject this
  // impossible case before building the expensive resource product graph.
  if (std::none_of(node.artifact.items.begin(), node.artifact.items.end(), [](const auto& item) {
        return item.kind == MachineItemKind::Op && !item.raw && item.opcode == 0x36;
      })) return result;
  constexpr std::string_view prefix = "selector-sunk-literal-value:";
  for (std::size_t store = 0; store < node.artifact.items.size(); ++store) {
    const auto& owner = node.artifact.items[store];
    if (owner.raw || owner.kind != MachineItemKind::Op || owner.opcode < 0x47 || owner.opcode > 0x4e ||
        (!reconsider_rebased && std::find(owner.roles.begin(), owner.roles.end(),
             "order-only-reduction-rebased") != owner.roles.end())) continue;
    const auto marker = std::find_if(owner.roles.begin(), owner.roles.end(),
        [&](const auto& role) { return role.starts_with(prefix); });
    if (marker == owner.roles.end()) continue;
    const auto literal = marker->substr(prefix.size());
    if (literal.size() != 2 || literal[0] < '1' || literal[0] > '9' || literal[1] < '0' || literal[1] > '9') continue;
    auto opened=reopen_order_reduction_input(node,store,literal,control);
    if(!opened)continue;
    const auto& input=opened->artifact.items;
    const auto next_command = [&](std::size_t at) {
      do { ++at; } while (at < input.size() && input[at].kind == MachineItemKind::Label);
      return at;
    };
    const auto first = next_command(store), last = next_command(first);
    if (last >= input.size() || input[first].opcode != literal[0] - '0' ||
        input[last].opcode != literal[1] - '0') continue;
    std::string identity;
    constexpr std::string_view identity_prefix = "selector-sunk-literal-id:";
    for (const auto& role : owner.roles)
      if (role.starts_with(identity_prefix)) identity = role.substr(identity_prefix.size());
    const int original_seed = (literal[0] - '0') * 10 + literal[1] - '0';
    result.push_back({std::move(*opened), store, first, last, std::move(identity), original_seed});
  }
  return result;
}

std::optional<OrderReductionLiteralDomain> prove_order_reduction_domain(
    const OrderReductionInput& family, const CompileOptions& options,
    const PostLayoutControlFlowOptions& control) {
  const auto& source = family.source;
  const auto& input = source.artifact.items;
  const auto cfg = shared_post_layout_control_flow(input, control);
  const auto origins = detail::joint_target_origins(input, source.targets, source.encoded_selectors);
  if (!cfg->proved || !origins) return std::nullopt;
  const auto selected = shared_deferred_selector_consistent_execution_graph(input, cfg, *origins, 20000, true);
  const auto projection = shared_post_layout_projected_flow(cfg, selected);
  if (!projection) return std::nullopt;
  const auto policy = options.ordinary_decimal_keyboard_input
      ? NumericRangeInputPolicy::ValidatedOrdinaryDecimalInput
      : NumericRangeInputPolicy::ValidatedRegisterTransport;
  const auto proved = prove_order_reduction_shape(
      input, source.artifact.preloads, projection, policy, family.first, family.last);
  if (!*proved) return std::nullopt;
  const auto& proof = **proved;
  if (!proof.proved) {
    if (std::getenv("MKPRO_NATIVE_TRACE_ORDER_REDUCTION"))
      for (const auto& reason : proof.reasons) std::cerr << "[order-reduction] " << reason << '\n';
    return std::nullopt;
  }
  return OrderReductionLiteralDomain{family.family_id, family.seed,
      std::max(10, proof.minimum_seed), std::min(99, proof.maximum_seed)};
}

std::vector<ConstrainedRepresentation> order_reduction_translation_roots(
    const ConstrainedRepresentation& node, const CompileOptions& options,
    const PostLayoutControlFlowOptions& control, bool reconsider_rebased = false,
    const std::set<int>* demanded_seeds = nullptr) {
  std::vector<ConstrainedRepresentation> result;
  for (const auto& family : order_reduction_inputs(node, options, control, reconsider_rebased)) {
    const auto proved = prove_order_reduction_domain(family, options, control);
    if (!proved) continue;
    const auto& domain = *proved;
    for (int seed = domain.minimum_seed; seed <= domain.maximum_seed; ++seed) {
      if (seed == domain.original_seed || (demanded_seeds && !demanded_seeds->contains(seed))) continue;
      auto candidate = family.source;
      auto& changed = candidate.artifact.items;
      changed[family.first].opcode = seed / 10;
      changed[family.first].mnemonic = std::to_string(seed / 10);
      changed[family.last].opcode = seed % 10;
      changed[family.last].mnemonic = std::to_string(seed % 10);
      for (auto& annotation : changed[family.store].roles)
        if (annotation.starts_with("selector-sunk-literal-value:"))
          annotation = "selector-sunk-literal-value:" + std::to_string(seed);
      if (std::find(changed[family.store].roles.begin(), changed[family.store].roles.end(),
          "order-only-reduction-rebased") == changed[family.store].roles.end())
        changed[family.store].roles.push_back("order-only-reduction-rebased");
      candidate.artifact.optimizations.push_back({"order-only-reduction-translation",
          "Translated private order-only reduction seed " + std::to_string(domain.original_seed) +
          " to " + std::to_string(seed) +
          "; proved identical exponent bands, maximum/tie/branch decisions and all resumed stack/X1/X2 uses. " +
          (options.ordinary_decimal_keyboard_input ? "Uses explicit ordinary-decimal keyboard input contract. " : "No keyboard assumption. ") +
          "Final data/address sharing still requires the independent complete-artifact layout proof."});
      result.push_back(std::move(candidate));
    }
  }
  return result;
}

std::vector<ProvedSearchTransition<LayoutSearchArtifact>>
parametric_order_reduction_layout_transactions(const ConstrainedRepresentation& root,
    const CompileOptions& options, const PostLayoutControlFlowOptions& control,
    int comparison_origin_cells = -1,
    const JointRepresentationFinalizer& finalize = {}) {
  std::vector<ProvedSearchTransition<LayoutSearchArtifact>> result;
  const int origin_cells = comparison_origin_cells >= 0
      ? comparison_origin_cells : cells(root.artifact);
  auto configured = options;
  configured.defer_return_suffix_until_callee_hole = true;
  const passes::PassContext context{configured};
  for (const auto& family : order_reduction_inputs(root, options, control, true)) {
    if (family.family_id.empty()) continue;
    // The literal inverse is proved before moving any callback. A previous
    // translation is not a frozen choice: prove its full domain again.
    std::vector<ConstrainedRepresentation> sources{family.source};
    const auto reopened = reopen_shared_helper_dual_mode_layout(family.source.artifact.items, control);
    if (reopened.applied > 0 && reopened.added_cells <= 4) {
      auto source = family.source;
      source.artifact.items = reopened.items;
      for (const auto& report : reopened.optimizations)
        source.artifact.optimizations.push_back({report.name, report.detail});
      if (refresh_constraint_targets(source)) sources.push_back(std::move(source));
    }
    // Keep the family domain open while choosing a return continuation. A
    // return shared at a hardware address can constrain the callback's start;
    // fixing the scalar first would reject an otherwise valid joint layout.
    const auto projected_count = sources.size();
    for (std::size_t at = 0; at < projected_count; ++at) {
      const auto source = sources[at];
      const auto origins = detail::joint_target_origins(
          source.artifact.items, source.targets, source.encoded_selectors);
      if (!origins) continue;
      const auto code = raise_machine_to_ir(source.artifact.items,
          effective_optimizer_feature_profile(options));
      for (auto& proposal : passes::contextual_lazy_unit_projection_candidates(code, context,
               {.maximum_work=400000, .maximum_candidates=2, .maximum_copy_sites=8,
                .maximum_growth_cells=4, .control=&control, .encoded_target_origins=&*origins,
                .runtime_preloads=&source.artifact.preloads,
                .preserve_formal_execution_frames=true})) {
        if (auto image = apply(source.artifact, std::move(proposal.transformation))) {
          auto next = source;
          next.artifact = std::move(*image);
          if (refresh_constraint_targets(next)) sources.push_back(std::move(next));
        }
      }
    }
    const auto unpooled_count = sources.size();
    for (std::size_t at = 0; at < unpooled_count; ++at) {
      const auto source = sources[at];
      for (auto& pooled : pool_addressed_return_targets(source, control))
        sources.push_back(std::move(pooled));
      if (auto folded = zero_underflow_conditional_return(source, context, control)) {
        for (auto& pooled : pool_addressed_return_targets(*folded, control))
          sources.push_back(std::move(pooled));
        sources.push_back(std::move(*folded));
      }
    }
    std::optional<OrderReductionLiteralDomain> domain;
    bool rejected = false;
    for (const auto& source : sources) {
      for (const auto& pending : cached_literal_layouts(
          source.artifact.items, control, 4)) {
        if (pending.family_id != family.family_id || pending.literal_value != family.seed)
          continue;
        const auto width = machine_cell_count(pending.items);
        const auto limit = finalize ? std::nullopt : std::optional{origin_cells};
        const auto profile = effective_optimizer_feature_profile(options);
        std::optional<std::vector<IrOp>> ordinary;
        bool fits_without_tail = finalize || width < origin_cells;
        if (fits_without_tail) {
          ordinary = raise_machine_to_ir(pending.items, profile);
          fits_without_tail = passes::unprojected_symbolic_region_target_may_fit(
              *ordinary, pending.target_label, 10, 99, kJointMaximumRegionPadding);
        }
        bool fits_with_tail = (finalize || width - kSharedTailMaximumSaving < origin_cells) &&
            has_shared_helper_continuation_shape(pending.items);
        if (!fits_without_tail && !fits_with_tail) continue;
        PreparedJointProofInput proof(BorrowedJointProofInput{pending.items, source.artifact.preloads}, control);
        const auto bindings = prepare_joint_selector_constraints(source, proof, true, true, &pending, true);
        if (!bindings) continue;
        if (fits_without_tail)
          fits_without_tail = passes::symbolic_region_constraints_may_fit(
              *ordinary, {}, bindings->fixed, pending.target_label, 10, 99,
              kJointMaximumRegionPadding, limit);

        std::optional<SharedHelperDualModePreparation> prepared_tail;
        // When only the projected lane remains, test its actual widths
        // and helper anchor. Keep its preparation for the exact closure.
        if (!fits_without_tail && fits_with_tail) {
          prepared_tail = prepare_shared_helper_dual_mode_layout(
              pending.items, proof.flow(), control.address_space_model);
          const auto widths = shared_helper_dual_mode_projected_widths(*prepared_tail);
          if (!widths) continue;
          const auto ops = raise_machine_to_ir(prepared_tail->items, profile);
          const auto ir_widths = region_widths_for_machine_projection(ops, *widths);
          if (!ir_widths) continue;
          auto fixed = bindings->fixed;
          const int address = prepared_tail->required_final_start_address;
          const auto [anchor, added] = fixed.emplace(prepared_tail->continuation.helper_label, address);
          fits_with_tail = (added || anchor->second == address) &&
              passes::symbolic_region_constraints_may_fit(ops, *ir_widths, fixed,
                  pending.target_label, 10, 99, kJointMaximumRegionPadding, limit);
        }
        if (!fits_without_tail && !fits_with_tail) continue;
        // These are only necessary conditions on the layout. Prove the
        // actual domain on the original, untransformed literal input and
        // never replace the full placement with the optimistic 10..99 check.
        if (!domain) {
          domain = prove_order_reduction_domain(family, options, control);
          if (!domain) { rejected = true; break; }
        }
        for (const bool projected : {false, true}) {
           if (projected ? !fits_with_tail : !fits_without_tail) continue;
           auto closed = close_representation_constraints(source, context, control,
               true, projected, &pending, proof, true, &*domain,
               limit, projected ? &prepared_tail : nullptr, &*bindings);
          if (!closed) continue;
          // The parameter has become a concrete data/address equality. Retain
          // that witness across the caller's finalizer, not the old seed from
          // the pending input. A finalizer is never permission to skip binding.
          auto delivered = pending;
          std::optional<int> selected_seed;
          for (const auto& owner : closed->items) {
            if (std::find(owner.roles.begin(), owner.roles.end(),
                    "selector-sunk-literal-id:" + pending.family_id) == owner.roles.end())
              continue;
            constexpr std::string_view value_prefix = "selector-sunk-literal-value:";
            for (const auto& role : owner.roles) {
              if (!role.starts_with(value_prefix)) continue;
              const auto value = std::string_view(role).substr(value_prefix.size());
              if (selected_seed || value.size() != 2U || value[0] < '1' ||
                  value[0] > '9' || value[1] < '0' || value[1] > '9') {
                closed.reset();
                break;
              }
              selected_seed = (value[0] - '0') * 10 + value[1] - '0';
            }
            if (!closed) break;
          }
          if (!closed || !selected_seed || *selected_seed < domain->minimum_seed ||
              *selected_seed > domain->maximum_seed) continue;
          delivered.literal_value = *selected_seed;
          if (finalize) closed = finalize(*closed);
          if (!closed || cells(*closed) >= origin_cells) continue;
          const auto final_flow = build_post_layout_control_flow(closed->items, control);
          if (!final_flow.proved || !literal_family_owns_charges(delivered, closed->items) ||
              !passes::prove_deferred_selector_data_literal_layout(
                  delivered, closed->items, final_flow)) continue;
          if (std::none_of(result.begin(), result.end(), [&](const auto& old) {
                return same_layout_search_artifact(old.node, *closed);
              })) result.push_back({std::move(*closed), "parametric-order-reduction-layout", true});
        }
      }
      if (rejected) break;
    }
  }
  return result;
}

enum class ProloguePartitionLane { Disabled, Enabled };

TransactionSearchResult<LayoutSearchArtifact> search_joint_representation_constraints_impl(
    const LayoutSearchArtifact& root, const CompileOptions& options,
    const JointRepresentationFinalizer& finalize,
    const TransactionSearchLimits& limits, bool preload_environment_complete,
    ProloguePartitionLane partition_lane, JointProofPreparationCounts& proof_counts,
    const JointRepresentationFinalizer& finalize_intermediate,
    bool coupled_geometry = false,
    const ConstrainedRepresentation* pending_root = nullptr,
    int comparison_origin_cells = -1,
    bool contextual_formal_frames = false) {
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
          bindings ? *bindings : parent.targets, parent.encoded_selectors,parent.numeric_layout_certificate};
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
    for (const bool sign_cuts : {false, true})
    for (auto& proposal : passes::deferred_finite_mode_representation_candidates(
             ops, context, {
                 .maximum_growth_cells = std::min(4, limits.maximum_growth_cells),
                 .maximum_candidates = 6, .allow_target_relayout = true,
                 .runtime_preloads = preload_environment_complete
                     ? &parent.artifact.preloads : nullptr,
                 .preserve_compiler_selector_charges = preload_environment_complete,
                 .assumed_target_addresses = &*data_origins,
                 .normalize_unit_arguments = coupled_geometry,
                 .precise_entry_facts = coupled_geometry && preload_environment_complete,
                 .nonnegative_max_cuts = sign_cuts,
                 .preserve_formal_execution_frames = coupled_geometry &&
                     preload_environment_complete,
                 .relocate_canonical_formal_frames = coupled_geometry && preload_environment_complete}))
      add(std::move(proposal.transformation), sign_cuts ? "nonnegative-max-representation-equations" : "representation-equations",
          &proposal.required_target_addresses);

    // A value rewrite can change the cheapest helper ABI, and moving that ABI
    // changes definition webs again. These are graph edges, not a one-way
    // pipeline: retain their unresolved command identities until placement.
    if (coupled_geometry && preload_environment_complete && data_origins) {
      // Materialization is an enabling edge, not an early layout decision.
      // Removing the last data read may release a stable selector's geometry;
      // every address/preload equation is still closed by the same finalizer.
      if (std::any_of(parent.artifact.preloads.begin(), parent.artifact.preloads.end(),
          [](const auto& word) { return detail::joint_literal_word(word) && word.value == "-99999999"; })) {
        const auto cfg = shared_post_layout_control_flow(parent.artifact.items, control_options);
        const auto selected = shared_deferred_selector_consistent_execution_graph(
            parent.artifact.items, cfg, *data_origins, 20000, true);
        if (const auto flow = shared_post_layout_projected_flow(cfg, selected)) {
          std::map<std::string, std::string> words;
          for (const auto& word : parent.artifact.preloads)
            if (detail::joint_literal_word(word)) words.emplace(word.register_name, word.value);
          add(passes::rematerialize_zero_underflow_constant(ops, words,
                  control_options.address_space_model, flow.get()),
              "pooled-constant-materialization-equations");
          add(passes::permuted_zero_underflow_constant(ops, parent.artifact.preloads,
                  control_options), "direct-home-materialization-equations");
        }
      }
      for (auto& transported : passes::contextual_copy_transport_candidates(
               ops, context, {.maximum_work=200000,.maximum_candidates=2,
                   .maximum_copy_sites=8,.maximum_growth_cells=std::min(4,limits.maximum_growth_cells),
                   .control=&control_options,.encoded_target_origins=&*data_origins,.preserve_formal_execution_frames=contextual_formal_frames}))
        add(std::move(transported.transformation), "coupled-contextual-copy-transport");
      for (auto& redundant : passes::contextual_redundant_copy_store_candidates(
               ops, context, {.maximum_work=200000,.maximum_candidates=2,
                   .maximum_copy_sites=8,.maximum_growth_cells=std::min(4,limits.maximum_growth_cells),
                   .control=&control_options,.encoded_target_origins=&*data_origins,
                   .runtime_preloads=&parent.artifact.preloads,.preserve_formal_execution_frames=contextual_formal_frames}))
        add(std::move(redundant.transformation), "coupled-contextual-word-store");
      for(auto& completed:passes::contextual_pure_return_completion_candidates(ops,context,
          {.maximum_work=200000,.maximum_candidates=2,.maximum_copy_sites=8,.control=&control_options,
           .encoded_target_origins=&*data_origins,.runtime_preloads=&parent.artifact.preloads,.preserve_formal_execution_frames=contextual_formal_frames}))
        add(std::move(completed.transformation),"coupled-pure-return-completion");
      for(auto& returned:passes::contextual_finite_return_selector_candidates(ops,context,
          {.maximum_work=100000,.maximum_candidates=2,.control=&control_options,
           .encoded_target_origins=&*data_origins,.runtime_preloads=&parent.artifact.preloads,.preserve_formal_execution_frames=contextual_formal_frames}))
        add(std::move(returned.transformation),"coupled-finite-return-selector",nullptr,&returned.selector_origins);
      for(auto& nullable:passes::contextual_nullable_callback_projection_candidates(ops,context,
          {.maximum_work=200000,.maximum_candidates=2,.maximum_copy_sites=8,.control=&control_options,
           .encoded_target_origins=&*data_origins,.runtime_preloads=&parent.artifact.preloads,
           .preserve_formal_execution_frames=contextual_formal_frames})) {
        auto bindings=parent.targets;
        bindings.insert(nullable.target_origins.begin(),nullable.target_origins.end());
        add(std::move(nullable.transformation),"coupled-nullable-readonly-continuation",&bindings);
      }
      for (auto& codec : passes::deferred_finite_mode_representation_candidates(
               ops,context,{.maximum_growth_cells=std::min(4,limits.maximum_growth_cells),
                   .maximum_candidates=6,.allow_target_relayout=true,
                   .runtime_preloads=&parent.artifact.preloads,
                   .preserve_compiler_selector_charges=true,
                   .assumed_target_addresses=&*data_origins,
                   .encode_preloaded_state=true,.precise_entry_facts=true,
                   .preserve_formal_execution_frames=contextual_formal_frames,
                   .relocate_canonical_formal_frames=contextual_formal_frames})) {
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
            codec.required_target_addresses,parent.encoded_selectors,codec.numeric_layout_certificate};
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

    if (coupled_geometry && preload_environment_complete && data_origins)
      for (auto& proposal : passes::indirect_conditional_region_exchange_candidates(
          ops, parent.artifact.preloads, *data_origins, context, control_options, 2)) {
        auto child = apply(parent.artifact, std::move(proposal.transformation));
        if (!child) continue;
        ConstrainedRepresentation next{std::move(*child), parent.targets, parent.encoded_selectors};
        for (const auto& [label, origin] : proposal.encoded_target_origins) next.targets[label] = origin;
        if (refresh_constraint_targets(next))
          edges.push_back({std::move(next), "indirect-conditional-region-exchange", true});
      }
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
    if (preload_environment_complete)
      if (auto folded=zero_underflow_conditional_return(parent,context,control_options))
        edges.push_back({std::move(*folded),"zero-underflow-conditional-return-equations",true});
    if (coupled_geometry && preload_environment_complete)
      for (auto& pooled : pool_addressed_return_targets(parent, control_options, 2))
        edges.push_back({std::move(pooled), "addressed-return-pooling-equations", true});
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

  // Keep the established direct-polarity lane intact. Stable indirect arms
  // receive their own bounded roots rather than stealing its seed budget.
  if (limits.maximum_expansions > 0 && limits.maximum_candidates > 0 && limits.maximum_depth > 0) {
    std::size_t indirect_seeds = 0;
    for (const auto& source : unrotated_allocation_roots) {
      const auto origins = detail::joint_target_origins(source.artifact.items, source.targets, source.encoded_selectors);
      if (!origins) continue;
      for (auto& proposal : passes::indirect_conditional_region_exchange_candidates(
          raise_machine_to_ir(source.artifact.items, effective_optimizer_feature_profile(options)),
          source.artifact.preloads, *origins, context, control_options, 2)) {
        auto image = apply(source.artifact, std::move(proposal.transformation));
        if (!image || !transaction_growth_fits(origin_cells, cells(*image), limits.maximum_growth_cells)) continue;
        ConstrainedRepresentation next{std::move(*image), source.targets, source.encoded_selectors};
        for (const auto& [label, origin] : proposal.encoded_target_origins) next.targets[label] = origin;
        if (!refresh_constraint_targets(next)) continue;
        allocation_roots.push_back(next);
        topology_seeds.push_back({std::move(next), "indirect-conditional-region-exchange", true});
        if (++indirect_seeds == 2U) break;
      }
      if (indirect_seeds == 2U) break;
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
  const auto build_coalesced_variants = [&](const ConstrainedRepresentation& node) {
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
    // A GVN-erased store can expose a recall immediately before a fresh
    // literal, even when no anticipatory store was needed. Compose that
    // independent proof before demanding a placement, not after publishing.
    if(preload_environment_complete&&limits.maximum_depth>0&&limits.maximum_expansions>0){
      const auto literal_bases=variants;
      for(const auto& base:literal_bases){
        const auto code=raise_machine_to_ir(base.artifact.items,effective_optimizer_feature_profile(options));
        bool demand=false;
        for(std::size_t i=1;i<code.size();++i)demand=demand||
            ((code[i-1U].kind==IrKind::Recall||(code[i-1U].kind==IrKind::Plain&&code[i-1U].opcode==0x0e))&&
             code[i].kind==IrKind::Plain&&code[i].opcode>=0&&code[i].opcode<=9);
        if(!demand)continue;
        const auto literal_origins=detail::joint_target_origins(base.artifact.items,base.targets,base.encoded_selectors);
        if(!literal_origins)continue;
        for(auto& erased:passes::deferred_stack_producer_erasure_candidates(code,context,control_options,*literal_origins,
            {.maximum_candidates=2,.maximum_erased_cells=1,.maximum_proof_attempts=16,.allow_fresh_literal_replacement=true})){
          if(auto rewritten=apply(base.artifact,std::move(erased))){
            auto next=base;next.artifact=std::move(*rewritten);if(!refresh_constraint_targets(next))continue;
            if(std::none_of(variants.begin(),variants.end(),[&](const auto& old){
                return same_layout_search_artifact(old.artifact,next.artifact)&&old.targets==next.targets&&old.encoded_selectors==next.encoded_selectors;
              }))variants.push_back(std::move(next));
          }
        }
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
  auto* joint_work = detail::active_joint_layout_work.get();
  const auto variant_options = joint_work ? representation_context_key(configured) : std::string{};
  const auto coalesced_variants = [&](const ConstrainedRepresentation& node) {
    if (!joint_work || !preload_environment_complete || node.artifact.items.size() > 512U)
      return build_coalesced_variants(node);
    const auto hash = mix_joint_key(joint_item_key(node.artifact.items),
        std::hash<std::string>{}(variant_options));
    const auto alternatives = joint_work->variants.get(hash, [&](const JointVariantsKey& key) {
      return key.options == variant_options && key.coupled_geometry == coupled_geometry &&
          same_joint_limits(key.limits, limits) && same_joint_equations(key.node, node) &&
          same_layout_search_artifact(key.node.artifact, node.artifact);
    }, [&] {
      auto key = node;
      key.artifact.optimizations.clear();
      return JointVariantsKey{std::move(key), variant_options, limits, coupled_geometry};
    }, [&] {
      // These transforms inspect IR and address/data facts, not the incoming
      // history. Store relative reports and never substitute another path.
      auto input = node;
      input.artifact.optimizations.clear();
      return build_coalesced_variants(input);
    });
    auto owned = *alternatives;
    for (auto& variant : owned)
      variant.artifact.optimizations.insert(variant.artifact.optimizations.begin(),
          node.artifact.optimizations.begin(), node.artifact.optimizations.end());
    return owned;
  };

  // Conditional data/control rewrites must not alter the historical beam,
  // pruning incumbent or equal-size winner. Evaluate them in a separate
  // closure lane, borrowing exactly the already-admitted representation and
  // allocation states. No larger search limits or source-specific seeds.
  std::optional<LayoutSearchArtifact> data_control_incumbent;
  std::unordered_map<std::size_t, std::vector<ConstrainedRepresentation>> closed_parametric_roots;
  const auto needs_literal_closure = [&](const ConstrainedRepresentation& node) {
    if (!preload_environment_complete || limits.maximum_expansions == 0 ||
        limits.maximum_candidates == 0 || limits.maximum_depth == 0)
      return false;
    const auto same_pending = [&](const auto& old) {
      return old.targets == node.targets && old.encoded_selectors == node.encoded_selectors &&
          old.numeric_layout_certificate == node.numeric_layout_certificate &&
          same_layout_search_artifact(old.artifact, node.artifact) &&
          old.artifact.optimizations.size() == node.artifact.optimizations.size() &&
          std::equal(old.artifact.optimizations.begin(), old.artifact.optimizations.end(),
              node.artifact.optimizations.begin(), [](const auto& a, const auto& b) {
                return a.name == b.name && a.detail == b.detail;
              });
    };
    auto& bucket = closed_parametric_roots[joint_item_key(node.artifact.items)];
    if (std::any_of(bucket.begin(), bucket.end(), same_pending)) {
      ++joint_literal_closure_reuses;
      return false;
    }
    // Both the discrete and parametric closures are pure in this invocation.
    // In particular the final initial-root visit must not replay all discrete
    // placements merely because only the parametric half used to be memoized.
    bucket.push_back(node);
    return true;
  };
  const auto prove_literal_closures = [&](const ConstrainedRepresentation& literal_root,
      JointProofPreparationCounts& counts, const LayoutSearchArtifact* prior = nullptr)
          -> std::optional<LayoutSearchArtifact> {
    std::optional<LayoutSearchArtifact> best;
    const int incumbent_cells = prior ? std::min(origin_cells, cells(*prior)) : origin_cells;
    // The discrete family removes a private one/two-digit literal. No
    // rejected size can be returned as a continuation from this lane.
    const bool possibly_smaller = finalize ||
        cells(literal_root.artifact) - 2 - kSharedTailMaximumSaving < incumbent_cells;
    const auto layouts = possibly_smaller ? cached_literal_layouts(
             literal_root.artifact.items, control_options,
             std::min<std::size_t>(4, limits.maximum_candidates)) : JointLiteralLayouts{};
    if (layouts.layouts) for (const auto& pending : layouts) {
      PreparedJointProofInput proof_input(BorrowedJointProofInput{pending.items, literal_root.artifact.preloads},
                                          control_options,&counts);
      for (const bool projected : {false, true}) {
        auto closed = close_representation_constraints(
             literal_root, context, control_options, true, projected, &pending, proof_input,
             pending_root != nullptr, nullptr,
             finalize ? std::nullopt : std::optional{best ? cells(*best) : incumbent_cells});
        if (!closed) continue;
        // A temporarily larger placement can still repay its bounded debt in
        // finalization. Only the complete artifact competes with incumbents.
        if (finalize) closed = finalize(*closed);
        if (closed && !literal_family_owns_charges(pending,closed->items)) closed.reset();
        // Closing already proves the bound literal on the exact rebound
        // image. Only an external finalizer can change that proof input here.
        if (closed && finalize) {
          const auto delivered_flow=build_post_layout_control_flow(closed->items,control_options);
          if (!delivered_flow.proved || !passes::prove_deferred_selector_data_literal_layout(
                  pending,closed->items,delivered_flow)) closed.reset();
        }
        if (closed && cells(*closed) < incumbent_cells &&
            (!best || cells(*closed) < cells(*best)))
          best = std::move(closed);
      }
    }
    for (auto& edge : parametric_order_reduction_layout_transactions(
             literal_root, options, control_options, origin_cells, finalize))
      if (cells(edge.node) < incumbent_cells && (!best || cells(edge.node) < cells(*best)))
        best = std::move(edge.node);
    return best;
  };
  const auto offer_literal_closure = [&](std::optional<LayoutSearchArtifact> candidate) {
    if (candidate && (!data_control_incumbent ||
        cells(*candidate) < cells(*data_control_incumbent)))
      data_control_incumbent = std::move(candidate);
  };
  const auto add_proof_counts = [&](const JointProofPreparationCounts& counts) {
    proof_counts.inputs += counts.inputs;
    proof_counts.selector_proofs += counts.selector_proofs;
    proof_counts.selector_reuses += counts.selector_reuses;
  };
  const auto close_data_control_literals = [&](const ConstrainedRepresentation& node) {
    if (needs_literal_closure(node))
      offer_literal_closure(prove_literal_closures(node, proof_counts,
          data_control_incumbent ? &*data_control_incumbent : nullptr));
  };
  // This independent closure is useful even when the old discrete frontier
  // has no profitable first edge. Do not demand a sacrificial rewrite before
  // the domain and geometry are allowed to meet.
  close_data_control_literals(initial);

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
          PreparedJointProofInput proof_input(BorrowedJointProofInput{variant.artifact.items, variant.artifact.preloads},
                                             control_options,&proof_counts);
          for (const bool projected : {false, true}) {
          auto closed = close_representation_constraints(
              variant, context, control_options, preload_environment_complete, projected, nullptr,
              proof_input, pending_root != nullptr);
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
    // Prepare/close geometry without running any external finalizer on a
    // worker. Only publish on the search thread, with the old family,
    // allocation, variant and projection order (including equal-size ties).
    struct AllocationProofs {
      std::vector<ConstrainedRepresentation> variants;
      std::vector<LayoutSearchArtifact> closures;
      std::vector<const ConstrainedRepresentation*> literal_roots;
      std::optional<LayoutSearchArtifact> literal_winner;
      JointProofPreparationCounts literal_counts;
      JointProofPreparationCounts counts;
      bool rejected = false;
    };
    std::vector<const ConstrainedRepresentation*> roots;
    for (const auto& source : allocation_roots) {
      if (coupled_geometry && std::none_of(
              source.artifact.items.begin(), source.artifact.items.end(), [](const auto& item) {
                return std::find(item.roles.begin(), item.roles.end(),
                                 kTransportedConstantPrefixRole) != item.roles.end();
              }))
        continue;
      if (roots.size() == limits.maximum_expansions) break;
      roots.push_back(&source);
    }
    const auto calculate_allocations = [&](const ConstrainedRepresentation& source,
                                            std::map<std::string, int>& encoded)
        -> std::vector<DeferredNaturalTargetSelectorAllocation> {
      const auto source_flow = build_post_layout_control_flow(source.artifact.items, control_options);
      encoded = source.encoded_selectors;
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
      const auto allocate = coupled_geometry
          ? balanced_deferred_natural_target_selector_allocations
          : deferred_natural_target_selector_allocations;
      if (!consistent) return {};
      return allocate(
          source.artifact.items, source.artifact.preloads, source_flow, encoded,
          control_options, {.allow_typed_resume_conversion = coupled_geometry,
                            .maximum_assignments = 128, .maximum_candidates = cap,
                            .maximum_growth_cells = limits.maximum_growth_cells});
    };
    const auto prove_allocation = [&](const ConstrainedRepresentation& source,
                                       const auto& allocation,
                                       const std::map<std::string, int>& encoded,
                                       int exclusive_limit,
                                       AllocationProofs& proof) {
        ConstrainedRepresentation allocated = source;
        allocated.artifact.items = allocation.items;
        // Factoring may already have borrowed cells. Reallocation cannot
        // reset that debt by measuring only its local delta from source.
        if (!transaction_growth_fits(
                origin_cells, cells(allocated.artifact), limits.maximum_growth_cells)) {
          proof.rejected = true;
          return;
        }
        allocated.encoded_selectors = encoded;
        for (const auto& [reg, origin] : allocation.encoded_selector_targets)
          allocated.encoded_selectors[reg] = origin;
        if (!refresh_constraint_targets(allocated)) return;
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
          PreparedJointProofInput proof_input(BorrowedJointProofInput{variant.artifact.items, variant.artifact.preloads},
                                             control_options,&proof.counts);
          for (bool projected : {false, true}) {
            auto closed = close_representation_constraints(
                 variant, context, control_options, true, projected, nullptr, proof_input,
                 pending_root != nullptr, nullptr, exclusive_limit);
            // Geometry alone is not a saving. Avoid running the full compiler
            // finalizer on an unpaid release/bridge, even if its CFG is valid.
            if (!closed || cells(*closed) >= exclusive_limit) continue;
            closed->optimizations.push_back({"joint-selector-allocation-layout",
                "Closed complete-family allocation, region geometry and pending continuation "
                "sharing atomically; independently checked delivered selectors and callbacks."});
            proof.closures.push_back(std::move(*closed));
          }
        }
        proof.variants = std::move(variants);
    };
    for (const auto* source : roots) {
      std::map<std::string, int> encoded;
      const auto allocations = calculate_allocations(*source, encoded);
      if (allocations.empty()) continue;
      ++output.expanded;
      // Keep the transient variant vectors bounded even if a coupled root
      // admits the full 128 allocation alternatives.
      const auto batch_size = static_cast<std::size_t>(detail::layout_worker_count()) * 2U;
      for (std::size_t first = 0; first < allocations.size();) {
        const auto count = std::min(batch_size, allocations.size() - first);
        const int exclusive_limit = !finalize && output.incumbent
            ? std::min(origin_cells, cells(*output.incumbent)) : origin_cells;
        std::vector<AllocationProofs> batch(count);
        detail::evaluate_layout_jobs(count, [&](std::size_t at) {
          prove_allocation(*source, allocations[first + at], encoded, exclusive_limit, batch[at]);
        });
        if (!finalize) {
          // The deduplication ledger belongs to the search thread. Workers
          // evaluate unique immutable roots only, without sharing a winner
          // or invoking any caller-owned function.
          for (auto& proof : batch)
            for (const auto& variant : proof.variants)
              if (needs_literal_closure(variant))
                proof.literal_roots.push_back(&variant);
          detail::evaluate_layout_jobs(count, [&](std::size_t at) {
            auto& proof = batch[at];
            for (const auto* pending_variant : proof.literal_roots) {
              const auto* prior = proof.literal_winner ? &*proof.literal_winner :
                  (data_control_incumbent ? &*data_control_incumbent : nullptr);
              auto closed = prove_literal_closures(*pending_variant, proof.literal_counts, prior);
              if (closed && (!proof.literal_winner ||
                  cells(*closed) < cells(*proof.literal_winner)))
                proof.literal_winner = std::move(closed);
            }
          });
        }
        for (auto& proof : batch) {
          if (proof.rejected) ++output.rejected;
          if (proof.variants.empty()) continue;
          ++output.admitted;
          add_proof_counts(proof.counts);
          for (auto& closed : proof.closures) {
            auto delivered = finalize ? finalize(closed) : std::optional{std::move(closed)};
            if (delivered && cells(*delivered) < origin_cells &&
                (!output.incumbent || cells(*delivered) < cells(*output.incumbent)))
              output.incumbent = std::move(delivered);
          }
          if (finalize) {
            for (const auto& variant : proof.variants) close_data_control_literals(variant);
          } else {
            add_proof_counts(proof.literal_counts);
            offer_literal_closure(std::move(proof.literal_winner));
          }
        }
        first += count;
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
addressed_return_pooling_layout_transactions(
    const LayoutSearchArtifact& artifact, const CompileOptions& options) {
  const auto view = representation_view(artifact, options);
  if (!view) return {};
  auto configured = options;
  configured.defer_return_suffix_until_callee_hole = true;
  PostLayoutControlFlowOptions control;
  control.empty_return_target = 1;
  control.address_space_model = address_space_model_for_feature_profile(
      effective_optimizer_feature_profile(options));
  ConstrainedRepresentation root{artifact, {}, {}};
  root.artifact.items = lower_ir_to_machine(*view);
  if (!refresh_constraint_targets(root)) return {};
  std::vector<ProvedSearchTransition<LayoutSearchArtifact>> result;
  for (const auto& candidate : pool_addressed_return_targets(root, control)) {
    PreparedJointProofInput proof(BorrowedJointProofInput{candidate.artifact.items, candidate.artifact.preloads}, control);
    auto closed = close_representation_constraints(
        candidate, {configured}, control, true, false, nullptr, proof, true, nullptr, cells(artifact));
    if (closed && cells(*closed) < cells(artifact))
      result.push_back({std::move(*closed), "addressed-return-pooling-layout", true});
  }
  return result;
}

std::vector<ProvedSearchTransition<LayoutSearchArtifact>>
zero_underflow_conditional_return_layout_transactions(
    const LayoutSearchArtifact& artifact, const CompileOptions& options) {
  if(std::none_of(artifact.items.begin(),artifact.items.end(),[](const auto& item){
      return item.kind==MachineItemKind::Op && item.opcode==0xd3;
    }))return {};
  const auto view=representation_view(artifact,options);if(!view)return {};
  auto configured=options;configured.defer_return_suffix_until_callee_hole=true;
  const passes::PassContext context{configured};
  PostLayoutControlFlowOptions control;control.empty_return_target=1;
  control.address_space_model=address_space_model_for_feature_profile(effective_optimizer_feature_profile(options));
  auto positions=constraint_positions(lower_ir_to_machine(*view));if(!positions)return {};
  ConstrainedRepresentation root{artifact,*positions};root.artifact.items=lower_ir_to_machine(*view);
  std::vector<std::pair<ConstrainedRepresentation,bool>> seeds{{root,false}};
  // Inverses have their own geometry preconditions. Open an implicit
  // data/address literal while its original, completely bound layout still
  // exists, not after a different inverse has moved one of its callbacks.
  // Keep this debt-bearing root private until the same transaction repays it.
  std::vector<ConstrainedRepresentation> literal_roots{root};
  for(std::size_t store=0;store<root.artifact.items.size();++store) {
    const auto& owner=root.artifact.items[store];
    if(std::find(owner.roles.begin(),owner.roles.end(),"selector-sunk-literal-coalesced")==
        owner.roles.end())continue;
    constexpr std::string_view prefix="selector-sunk-literal-value:";
    const auto value=std::find_if(owner.roles.begin(),owner.roles.end(),[prefix](const auto& role){
      return role.starts_with(prefix);
    });
    if(value==owner.roles.end() || value->size()!=prefix.size()+2U)continue;
    if(auto opened=reopen_order_reduction_input(root,store,value->substr(prefix.size()),control)) {
      literal_roots.push_back(*opened);
      seeds.push_back({std::move(*opened),false});
    }
  }
  // Keep the original geometry and its budgets intact. Reopening is a
  // separately proved inverse, not permission to relax a frozen side frame.
  // Fold only after that inverse, then repay every added cell by completing
  // the same shared-tail law in one final placement transaction.
  for(const auto& source:literal_roots) {
  const auto reopened=reopen_shared_helper_dual_mode_layout(source.artifact.items,control);
  if(reopened.applied>0 && reopened.added_cells<=4) {
    auto next=source;next.artifact.items=reopened.items;
    for(const auto& report:reopened.optimizations)
      next.artifact.optimizations.push_back({report.name,report.detail});
    if(refresh_constraint_targets(next))seeds.push_back({std::move(next),true});
  }
  }
  std::vector<std::pair<ConstrainedRepresentation,bool>> candidates;
  for(const auto& [seed,refold]:seeds) {
    auto folded=zero_underflow_conditional_return(seed,context,control);if(!folded)continue;
    candidates.push_back({*folded,refold});
    for(auto& pooled:pool_addressed_return_targets(*folded,control))
      candidates.push_back({std::move(pooled),refold});
  }
  std::vector<ProvedSearchTransition<LayoutSearchArtifact>> result;
  for (const auto& [candidate,refold] : candidates) {
    PreparedJointProofInput proof(BorrowedJointProofInput{candidate.artifact.items, candidate.artifact.preloads}, control);
    auto closed=close_representation_constraints(candidate,context,control,true,refold,
        nullptr,proof,true,nullptr,cells(artifact));
    if(!closed || cells(*closed)>=cells(artifact))continue;
    closed=rebind_layout_search_artifact(std::move(*closed));
    if(!closed || !build_post_layout_control_flow(closed->items,control).proved)continue;
    if(refold)closed->optimizations.push_back({"reopen-fold-refold-continuation",
        "Composed an independently proved shared-tail inverse, zero-underflow branch fold and "
        "the original dual-entry continuation law before final placement. Preserved encoded "
        "selector origins, repaid every duplicated cell and rechecked physical return 99, "
        "all side counters and complete rooted return frames without weakening any fold gate."});
    result.push_back({std::move(*closed),"zero-underflow-conditional-return-layout",true});
  }
  // A pinned return also determines the possible entry addresses in its
  // fallthrough component. Feed those affine equations into the existing
  // order-only translation theorem; never freeze a seed selected by an
  // earlier geometry, and never replace its fresh relational proof by a tag.
  for(const auto& [candidate,refold]:candidates) {
    const auto code=raise_machine_to_ir(candidate.artifact.items,
        effective_optimizer_feature_profile(options));
    std::map<std::string,std::pair<int,int>> labels;
    int component=0,offset=0;
    for(const auto& op:code) {
      if(op.kind==IrKind::Label)labels.emplace(op.name,std::pair{component,offset});
      offset+=passes::cells_per_op(op);
      if(op.kind==IrKind::Return || op.kind==IrKind::Jump || op.kind==IrKind::IndirectJump ||
          (op.kind==IrKind::Stop && op.meta.stop_disposition==StopDisposition::Terminal)) {
        ++component;offset=0;
      }
    }
    std::set<int> demanded_seed_values;
    for(const auto& op:code) {
      if(std::find(op.meta.roles.begin(),op.meta.roles.end(),"zero-underflow-conditional-return")==
          op.meta.roles.end() || !op.meta.indirect_flow_targets)continue;
      for(const auto& target:*op.meta.indirect_flow_targets) {
        const auto* name=std::get_if<std::string>(&target);
        if(!name || !labels.contains(*name) || !candidate.targets.contains(*name))continue;
        const auto [part,at]=labels.at(*name);
        for(const auto& [label,position]:labels) {
          (void)label;
          if(position.first==part)demanded_seed_values.insert(candidate.targets.at(*name)-at+position.second);
        }
      }
    }
    if(demanded_seed_values.empty())continue;
    for(const auto& translated:order_reduction_translation_roots(
        candidate,configured,control,true,&demanded_seed_values))
      for(const auto& pending:cached_literal_layouts(
          translated.artifact.items,control,4)) {
        PreparedJointProofInput proof(BorrowedJointProofInput{pending.items, translated.artifact.preloads}, control);
        auto closed=close_representation_constraints(translated,context,control,
             true,refold,&pending,proof,true,nullptr,cells(artifact));
        if(!closed || cells(*closed)>=cells(artifact))continue;
        closed=rebind_layout_search_artifact(std::move(*closed));if(!closed)continue;
        const auto flow=build_post_layout_control_flow(closed->items,control);
        if(!flow.proved || !literal_family_owns_charges(pending,closed->items) ||
            !passes::prove_deferred_selector_data_literal_layout(pending,closed->items,flow))continue;
        if(std::any_of(result.begin(),result.end(),[&](const auto& old){
            return same_layout_search_artifact(old.node,*closed);
          }))continue;
        closed->optimizations.push_back({"return-constrained-reduction-translation",
            "Derived reduction-entry candidates from a pinned return and exact fallthrough offsets; "
            "freshly re-proved the complete integer-translation interval even for an earlier rebased seed. "
            "Composed return pooling, any certified sharing inverse and final shared-tail placement; "
            "rechecked all data/address, hidden-state and rooted-return obligations before final-cell pricing."});
        result.push_back({std::move(*closed),"return-constrained-reduction-translation",true});
      }
  }
  return result;
}

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
               .encode_preloaded_state = true, .precise_entry_facts = true,
               .preserve_formal_execution_frames = true,.relocate_canonical_formal_frames = true})) {
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
    ConstrainedRepresentation node{std::move(*child), proposal.required_target_addresses,
        {},proposal.numeric_layout_certificate};
    PreparedJointProofInput proof_input(BorrowedJointProofInput{node.artifact.items, node.artifact.preloads}, control);
    auto closed = close_representation_constraints(
        node, context, control, true, false, nullptr, proof_input, true, nullptr, cells(artifact));
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
  const auto input=lower_ir_to_machine(code);
  const auto cfg=shared_post_layout_control_flow(input,control);
  const auto& flow=*cfg;
  const auto origins=detail::joint_target_origins(input,root.targets,root.encoded_selectors);
  if(!flow.proved || !origins)return result;
  const auto selected=shared_deferred_selector_consistent_execution_graph(input,cfg,*origins);
  const auto data_owner=shared_post_layout_projected_flow(cfg,selected);
  if(!data_owner)return result;
  const auto& data=*data_owner;
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


namespace {
TransactionSearchResult<LayoutSearchArtifact> close_quantized_return_composition(
    const LayoutSearchArtifact& root,const CompileOptions& options,
    const JointRepresentationFinalizer& finalize,const TransactionSearchLimits& limits,
    bool scalar_leaf = false) {
  TransactionSearchResult<LayoutSearchArtifact> result;
  if(!limits.maximum_expansions || !limits.maximum_candidates || !limits.maximum_depth ||
      std::none_of(root.items.begin(),root.items.end(),[](const auto& item){
        return item.kind==MachineItemKind::Address && item.formal_opcode.has_value();}))return result;
  bool quantizer=false;int returns=0;
  for(const auto& item:root.items)if(item.kind==MachineItemKind::Op&&!item.raw){
    quantizer=quantizer||item.opcode==0x34;returns+=item.opcode==0x52;
  }
  if((!scalar_leaf&&!quantizer)||returns<2)return result;
  auto view=representation_view(root,options);if(!view)return result;
  CompileOptions configured=options;configured.coalesce_copies=true;
  configured.defer_return_suffix_until_callee_hole=true;
  const passes::PassContext context{configured};PostLayoutControlFlowOptions control;
  control.empty_return_target=1;control.address_space_model=address_space_model_for_feature_profile(
      effective_optimizer_feature_profile(options));
  ConstrainedRepresentation initial{root,{},{}};initial.artifact.items=lower_ir_to_machine(*view);
  if(!refresh_constraint_targets(initial))return result;
  std::vector<ConstrainedRepresentation> seeds{initial};
  if(auto permuted=apply(initial.artifact,passes::permuted_zero_underflow_constant(
      *view,root.preloads,control))){
    auto child=initial;child.artifact=std::move(*permuted);
    if(refresh_constraint_targets(child))seeds.push_back(std::move(child));
  }
  const auto ordinary=seeds;
  for(const auto& seed:ordinary){
    auto reopened=reopen_shared_helper_dual_mode_layout(seed.artifact.items,control);
    if(reopened.applied<=0 || !transaction_growth_fits(cells(root),
        cells({.items=reopened.items}),limits.maximum_growth_cells))continue;
    auto child=seed;child.artifact.items=std::move(reopened.items);
    for(const auto& report:reopened.optimizations)
      child.artifact.optimizations.push_back({report.name,report.detail});
    if(refresh_constraint_targets(child))seeds.push_back(std::move(child));
  }
  // This is one atomic edge family. Internal proof/closure alternatives
  // spend candidate slots, not subsequent frontier expansions. In particular
  // the fast two-expansion policy still owns its twelve candidate slots.
  const auto candidate_limit=std::min<std::size_t>(12,limits.maximum_candidates);
  std::size_t attempted=0;result.expanded=1;
  for(const auto& seed:seeds){
    if(attempted>=candidate_limit)break;
    const auto origins=detail::joint_target_origins(seed.artifact.items,seed.targets,seed.encoded_selectors);
    if(!origins)continue;
    const auto completion_candidates=scalar_leaf ? passes::contextual_total_return_completion_candidates :
        passes::contextual_pure_return_completion_candidates;
    for(auto& completion:completion_candidates(
        raise_machine_to_ir(seed.artifact.items,effective_optimizer_feature_profile(options)),context,
        {.maximum_work=200000,.maximum_candidates=2,.maximum_copy_sites=8,
         .maximum_growth_cells=limits.maximum_growth_cells,.control=&control,
         .encoded_target_origins=&*origins,.runtime_preloads=&seed.artifact.preloads,
         .preserve_formal_execution_frames=true})){
      auto artifact=apply(seed.artifact,std::move(completion.transformation));if(!artifact)continue;
      auto completed=seed;completed.artifact=std::move(*artifact);
      if(!refresh_constraint_targets(completed))continue;
      std::vector<ConstrainedRepresentation> polarities{completed};
      for(auto& exchanged:passes::conditional_region_exchange_candidates(
          raise_machine_to_ir(completed.artifact.items,effective_optimizer_feature_profile(options)),
          context,control,2)){
        if(auto image=apply(completed.artifact,std::move(exchanged))){
          auto child=completed;child.artifact=std::move(*image);
          if(refresh_constraint_targets(child))polarities.push_back(std::move(child));
        }
      }
      for(const auto& candidate:polarities){
        if(attempted++>=candidate_limit)return result;
        if(!transaction_growth_fits(cells(root),cells(candidate.artifact),limits.maximum_growth_cells))continue;
        ++result.admitted;
        PreparedJointProofInput proof(BorrowedJointProofInput{candidate.artifact.items, candidate.artifact.preloads}, control);
        for(bool shared:{false,true}){
           auto closed=close_representation_constraints(candidate,context,control,true,shared,
               nullptr,proof,true,nullptr,cells(root));
          if(!closed || cells(*closed)>=cells(root))continue;
          if(finalize)closed=finalize(*closed);
          if(!closed || cells(*closed)>=cells(root))continue;
          if(!result.incumbent || cells(*closed)<cells(*result.incumbent)){
            closed->optimizations.push_back({scalar_leaf?"joint-total-return-composition":"joint-quantized-return-composition",
                "Closed a return-completion, optional neutral home permutation, branch-polarity "
                "and shared-continuation layout as one independently priced transaction. "
                "Charged every expanded cell and retained all selector, formal-PC, "
                "stack/X1/X2 and rooted return obligations."});
            result.incumbent=std::move(*closed);
          }
        }
      }
    }
  }
  return result;
}
}

namespace {
// Run only at the complete-artifact boundary, not inside every intermediate
// lowering/finalization profile. Its independent search cannot consume the
// historical copy/representation candidate budgets.
std::optional<LayoutSearchArtifact> close_final_stable_tail_layout(
    const LayoutSearchArtifact& root, const CompileOptions& options) {
  const auto model = address_space_model_for_feature_profile(
      effective_optimizer_feature_profile(options));
  if (cells(root) <= official_program_step_limit(model) ||
      std::none_of(root.items.begin(), root.items.end(), [](const auto& item) {
        return item.kind == MachineItemKind::Op && !item.raw &&
            item.opcode >= 0x87 && item.opcode <= 0x8e;
      }))
    return std::nullopt;
  const auto view = representation_view(root, options);
  if (!view) return std::nullopt;
  const auto input = lower_ir_to_machine(*view);
  PostLayoutControlFlowOptions control;
  control.address_space_model = model;
  control.empty_return_target = 1;
  const auto flow = build_post_layout_control_flow(input, control);
  const auto bounded = late_bound_decimal_selector_target_labels(input);
  if (!flow.proved || !bounded) return std::nullopt;
  NaturalTargetComponentLayoutOptions layout;
  layout.address_space_model = model;
  layout.maximum_subset_states = options.fast_candidate_search ? 4096 : 16384;
  layout.maximum_anchor_combinations = options.fast_candidate_search ? 8 : 32;
  layout.maximum_anchors = options.maximum_natural_target_anchors;
  layout.maximum_rejection_reasons = 0;
  layout.allow_standalone_fallthrough_jump_fold = true;
  layout.isolate_stable_tail_folds = true;
  layout.allow_unreferenced_jump_labels = true;
  layout.required_bounded_target_labels = *bounded;
  layout.maximum_bounded_target_address = 99;
  layout.allow_size_neutral_bounded_layout = !bounded->empty();
  layout.maximum_transactional_growth_cells = 1;
  const auto folded = optimize_natural_target_component_layout(
      input, root.preloads, flow, layout);
  if (folded.applied <= 0 || folded.removed_cells <= 0 ||
      !folded.plan.proved || !folded.plan.control_flow_equivalent ||
      !folded.plan.call_return_equivalent || !folded.plan.stack_and_x2_equivalent ||
      !folded.plan.indirect_memory_equivalent || !folded.plan.data_projection_equivalent ||
      !folded.plan.final_artifact_proved || !folded.plan.final_control_flow.proved)
    return std::nullopt;
  auto result = root;
  result.items = folded.items;
  result.preloads = folded.preloads;
  auto rebound = rebind_layout_search_artifact(std::move(result));
  if (!rebound || cells(*rebound) >= cells(root) ||
      !build_post_layout_control_flow(rebound->items, control).proved)
    return std::nullopt;
  rebound->optimizations.push_back({"joint-isolated-stable-tail-layout",
      "Independently priced each existing stable indirect tail at the complete "
      "artifact boundary, after representation/return composition. Re-proved "
      "placement, delivered selector words, data projections, numeric entry, X2 "
      "and rooted call/return traces. Historical lowering-profile budgets remain independent."});
  return rebound;
}
}

static std::vector<ProvedSearchTransition<LayoutSearchArtifact>>
contextual_copy_transport_layout_transactions_impl(const LayoutSearchArtifact& artifact,
    const CompileOptions& options, bool preserve_formal_frames = false) {
  bool demand=false;
  for(std::size_t i=1;i<artifact.items.size() && !demand;++i) {
    const auto& a=artifact.items[i-1U];const auto& b=artifact.items[i];
    demand=a.kind==MachineItemKind::Op && b.kind==MachineItemKind::Op && !a.raw && !b.raw &&
        b.opcode>=0x40 && b.opcode<=0x4e &&
        ((a.opcode>=0x40 && a.opcode<=0x4e) || (a.opcode>=0x60 && a.opcode<=0x6e));
  }
  if (!demand) {
    std::set<std::string> argument_entries;
    std::set<int> argument_addresses;
    std::vector<std::string> labels;
    int address=0;
    for (const auto& item:artifact.items) {
      if (item.kind==MachineItemKind::Label) { labels.push_back(item.name); continue; }
      if (item.kind==MachineItemKind::Op && !item.raw && !item.manual_interaction &&
          item.opcode>=0x40 && item.opcode<=0x4e) {
        argument_entries.insert(labels.begin(),labels.end());
        argument_addresses.insert(address);
      }
      labels.clear();
      ++address;
    }
    const auto argument_entry=[&](const IrTarget& target) {
      if (const auto* name=std::get_if<std::string>(&target)) return argument_entries.contains(*name);
      return argument_addresses.contains(std::get<int>(target));
    };
    for (std::size_t at=0; at<artifact.items.size() && !demand; ++at) {
      const auto& item=artifact.items[at];
      if (item.kind!=MachineItemKind::Op || item.raw) continue;
      if (item.opcode==0x53) {
        auto operand=at+1U;
        while (operand<artifact.items.size() && artifact.items[operand].kind==MachineItemKind::Label) ++operand;
        if (operand<artifact.items.size() && artifact.items[operand].kind==MachineItemKind::Address)
          demand=argument_entry(artifact.items[operand].target);
      } else if (item.opcode>=0xa0 && item.opcode<=0xae && item.indirect_flow_targets) {
        for (const auto& target:*item.indirect_flow_targets)
          demand=demand || argument_entry(target);
      }
    }
  }
  const auto return_limits = joint_representation_search_limits(options.fast_candidate_search);
  TransactionSearchResult<LayoutSearchArtifact> composed;
  if (!preserve_formal_frames) {
    composed=close_quantized_return_composition(artifact,options,{},return_limits);
    auto scalar_composed=close_quantized_return_composition(artifact,options,{},return_limits,true);
    if(scalar_composed.incumbent && (!composed.incumbent ||
        cells(*scalar_composed.incumbent)<cells(*composed.incumbent)))
      composed.incumbent=std::move(scalar_composed.incumbent);
    if (auto tail = close_final_stable_tail_layout(artifact, options))
      if (!composed.incumbent || cells(*tail) < cells(*composed.incumbent))
        composed.incumbent = std::move(*tail);
    if (composed.incumbent)
      if (auto tail = close_final_stable_tail_layout(*composed.incumbent, options))
        composed.incumbent = std::move(*tail);
  }
  if(!demand){
    if(composed.incumbent)return {{std::move(*composed.incumbent),"final-root-return-composition",true}};
    return {};
  }
  auto ops=representation_view(artifact,options);if(!ops)return {};
  CompileOptions configured=options;configured.coalesce_copies=true;
  configured.defer_return_suffix_until_callee_hole=true;
  const passes::PassContext context{configured};
  PostLayoutControlFlowOptions control;control.empty_return_target=1;
  const auto recolor_deferred = [&](const auto& code, const auto& pass_context,
      const auto& requests, const auto& flow_options, const auto& origins,
      const auto& consume, const std::vector<PreloadReport>* words, bool equal_homes) {
    passes::for_each_deferred_register_web_recoloring(code, pass_context, requests,
        flow_options, origins, consume, words, equal_homes, preserve_formal_frames);
  };
  control.address_space_model=address_space_model_for_feature_profile(effective_optimizer_feature_profile(options));
  const auto symbolic=lower_ir_to_machine(*ops);
  auto positions=constraint_positions(symbolic);if(!positions)return {};
  const auto encoded_origins=detail::joint_target_origins(symbolic,*positions,{});
  if(preserve_formal_frames && !encoded_origins)return {};
  std::vector<ProvedSearchTransition<LayoutSearchArtifact>> result;
  std::vector<LayoutSearchArtifact> expanded_closed_roots;
  using TransportOutcome = ProvedSearchTransition<LayoutSearchArtifact>;
  std::vector<TransportOutcome> ordered_outcomes;
  std::vector<std::size_t> joint_roots;
  const passes::ContextualCopyTransportOptions transport_options{
      .control=preserve_formal_frames?&control:nullptr,
      .encoded_target_origins=preserve_formal_frames?&*encoded_origins:nullptr,
      .runtime_preloads=&artifact.preloads,
      .preserve_formal_execution_frames=preserve_formal_frames};
  auto transports=preserve_formal_frames
      ? passes::contextual_copy_transport_candidates(*ops,context,transport_options)
      : passes::contextual_copy_transport_candidates(*ops,context);
  auto redundant=passes::contextual_redundant_copy_store_candidates(*ops,context,
      transport_options);
  transports.insert(transports.end(),std::make_move_iterator(redundant.begin()),
      std::make_move_iterator(redundant.end()));
  // A previous round can already have removed the copy. Its new lifetime,
  // literal and helper choices still deserve the same joint closure; do not
  // require an unrelated second copy to compose the remaining mechanisms.
  const auto identity_flow = shared_post_layout_control_flow(symbolic, control);
  const bool identity_work = contextual_identity_expansion_demand(
      *ops, artifact.preloads, options, identity_flow.get());
  if(transports.empty() && (!preserve_formal_frames || identity_work))
    transports.push_back({{.ops=*ops},{}});
  // Keep every historical proposal (including its neutral fallback) first.
  // Grounded argument transport has an independent proof/candidate budget.
  auto grounded=passes::contextual_grounded_copy_transport_candidates(*ops,context,transport_options);
  transports.insert(transports.end(),std::make_move_iterator(grounded.begin()),
      std::make_move_iterator(grounded.end()));
  const bool transport_trace=std::getenv("MKPRO_NATIVE_TRACE_COPY_TRANSPORT")!=nullptr;
  const bool composition_trace=std::getenv("MKPRO_NATIVE_TRACE_JOINT_COMPOSITION")!=nullptr;
  if(composition_trace) {
    std::cerr<<"[joint-composition] root="<<cells(artifact)<<" formal="<<preserve_formal_frames
             <<" transports="<<transports.size()
             <<" neutral="<<std::count_if(transports.begin(),transports.end(),[](const auto& p){return p.transformation.applied<=0;})
             <<" normalized-neutral="<<identity_work
             <<" preloads:";
    for(const auto& word:artifact.preloads) std::cerr<<' '<<word.register_name<<'='<<word.value;
    std::cerr<<'\n';
  }
  if(transport_trace)std::cerr<<"[copy-transport-layout] proposals="<<transports.size()<<"\n";
  for(auto& proposal:transports) {
    auto prepared_root = contextual_transport_root(
        artifact, symbolic, *positions, proposal.transformation, identity_work);
    if (!prepared_root) continue;
    auto base = std::move(*prepared_root);
    std::vector<ConstrainedRepresentation> allocations{base};
    // All entry facts must name the same normalized command identities as
    // the lifetime/transport proof. The delivered image can still carry an
    // older logical target beside an exact hardware counter.
    const auto& source_flow=*identity_flow;
    auto entry=resolve_post_layout_external_entry_contexts(source_flow);
    const auto deaths=passes::prove_post_layout_register_deadness(*ops,
        build_post_layout_control_flow(lower_ir_to_machine(*ops),control),100000);
    // New setup words are allowed only in an independently dead entry epoch.
    // Their setup effects must also disappear before every real observation.
    bool hidden_setup=false;
    if(source_flow.proved && entry) {
      std::vector<std::size_t> mains;
      for(std::size_t s=0;s<entry->main.size();++s)if(entry->main[s])mains.push_back(s);
      hidden_setup=prove_post_layout_stack_state_equality(symbolic,source_flow,mains,
          {{false,false,false,false},false,false});
      if(!hidden_setup && options.ordinary_decimal_keyboard_input &&
          std::all_of(artifact.preloads.begin(),artifact.preloads.end(),[](const auto& word){
            return detail::joint_literal_word(word) && bound_ordinary_decimal(word.value).has_value();
          }))
        hidden_setup=prove_post_layout_stack_state_equality(symbolic,source_flow,mains,
            {{false,false,false,false},false,false},
            {.numeric_entry_closed=true,.transport_closed_entry_through_control=true,
             .use_indirect_store_entry_facts=true});
    }
    if(composition_trace)
      std::cerr<<"[joint-composition] entry-facts flow="<<source_flow.proved
               <<" lifetime="<<deaths.proved<<" hidden-setup="<<hidden_setup<<'\n';
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
          if(composition_trace)
            std::cerr<<"[joint-composition] seeded-entry-homes="
                     <<seeded.artifact.preloads.size()-base.artifact.preloads.size()<<'\n';
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
      recolor_deferred(transported_ops,context,requests,control,
          root.targets,[&](std::vector<IrOp> recolored){
            if(allocations.size()>=8U)return;
            auto next=root;next.artifact.items=lower_ir_to_machine(recolored);retire_moved_initializers(next);
            if(std::none_of(allocations.begin(),allocations.end(),[&](const auto& old){
                return same_layout_search_artifact(old.artifact,next.artifact);}))allocations.push_back(std::move(next));
          },&root.artifact.preloads,true);
    }
    // A folded side-space tail changes the reaching definitions seen by the
    // allocator. Its certified inverse must precede the hardware/copy epoch
    // products, not be deferred until after one frozen coloring has won.
    // Retain the historical roots and give this new topology its own two
    // complete setup alternatives; no speculative root is published here.
    if (preserve_formal_frames && identity_work &&
        cells(artifact) > official_program_step_limit(control.address_space_model)) {
      std::vector<ConstrainedRepresentation> prospective;
      for (std::size_t n=0; n<seed_count && prospective.size()<2U; ++n) {
        auto opened = reopened_allocation_topology(allocations[n], control, 4);
        if (!opened || !transaction_growth_fits(cells(artifact), cells(opened->artifact), 4)) continue;
        const auto code = raise_machine_to_ir(opened->artifact.items,
            effective_optimizer_feature_profile(options));
        auto demands = hardware_epoch_storage_requests(code);
        const auto copies = self_reload_copy_requests(code, control);
        std::map<std::string,std::vector<std::size_t>> epochs;
        for (std::size_t at=0; at<code.size(); ++at)
          if (code[at].kind==IrKind::Store) epochs[code[at].register_name].push_back(at);
        auto cycles = hardware_home_eviction_requests(code, demands, copies, epochs);
        demands.insert(demands.end(), cycles.begin(), cycles.end());
        recolor_deferred(code, context, demands, control, opened->targets,
            [&](std::vector<IrOp> recolored) {
              if (prospective.size()>=2U) return;
              auto next = *opened;
              next.artifact.items = lower_ir_to_machine(recolored);
              retire_moved_initializers(next);
              const auto same = [&](const auto& old) {
                return same_layout_search_artifact(old.artifact, next.artifact);
              };
              if (same(*opened) || !transaction_growth_fits(cells(artifact),cells(next.artifact),4) ||
                  std::any_of(allocations.begin(),allocations.end(),same) ||
                  std::any_of(prospective.begin(),prospective.end(),same)) return;
              next.artifact.optimizations.push_back({"prospective-continuation-register-epochs",
                  "Proved a changed complete hardware/copy epoch allocation on the reopened continuation. "
                  "Only this enabling allocation, never a bare inverse, enters representation and placement closure."});
              prospective.push_back(std::move(next));
            }, &opened->artifact.preloads, true);
      }
      if (composition_trace)
        std::cerr << "[joint-composition] prospective-epoch roots=" << prospective.size() << '\n';
      for (auto& next : prospective) allocations.push_back(std::move(next));
    }
    const auto allocated_count=allocations.size();
    if(transport_trace)std::cerr<<"[copy-transport-layout] allocations="<<allocated_count<<"\n";
    for(std::size_t n=0;n<allocated_count && allocations.size()<16U;++n) {
      const auto root=allocations[n];
      const auto code=raise_machine_to_ir(root.artifact.items,effective_optimizer_feature_profile(options));
      for(auto& codec:passes::deferred_finite_mode_representation_candidates(code,context,
          {.maximum_growth_cells=4,.maximum_candidates=2,.allow_target_relayout=true,
           .runtime_preloads=&root.artifact.preloads,.preserve_compiler_selector_charges=true,
           .assumed_target_addresses=&root.targets,.encode_preloaded_state=true,
           .preserve_formal_execution_frames=preserve_formal_frames,
           .relocate_canonical_formal_frames=preserve_formal_frames})) {
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
        if(valid)allocations.push_back({std::move(*encoded),codec.required_target_addresses,
            root.encoded_selectors,codec.numeric_layout_certificate});
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
           .precise_entry_facts=true,.preserve_formal_execution_frames=preserve_formal_frames,
           .relocate_canonical_formal_frames=preserve_formal_frames})) {
        auto encoded=apply(root.artifact,std::move(codec.transformation)); if(!encoded)continue;
        if(!codec.preload_replacements.empty())continue;
        ConstrainedRepresentation next{std::move(*encoded),codec.required_target_addresses,
            root.encoded_selectors,codec.numeric_layout_certificate};
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
    std::vector<ConstrainedRepresentation> hardware_epoch_allocations;
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
      if(!unique || (stores.empty() && !identity_work))continue;
      std::vector<passes::RegisterWebRecoloringRequest> demands;
      for(const auto store:stores)for(const auto kind:{IrKind::IndirectStore,IrKind::IndirectRecall,IrKind::Loop})
        for(std::size_t i=0;i<code.size() && demands.size()<12U;++i)
          if(code[i].kind==kind)demands.push_back({{store,i}});
      // Source-ABI and finite-encoding roots need not descend from a copy
      // rewrite. Give that previously absent family its own ordinary hardware
      // epoch requests, then compose the same self-reload/third-home products.
      // Tagged historical roots keep their exact old request order/budgets.
      if (stores.empty()) demands = hardware_epoch_storage_requests(code);
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
      // Keep a separate fair product: a hardware cursor equality can require
      // both a destructive-reload output home and a later ordinary epoch to
      // move together. First-definition-only pairs cannot express that join.
      const auto hardware_copies = self_reload_copy_requests(code,control);
      std::vector<std::vector<passes::RegisterWebRecoloringRequest>> connected(hardware_copies.size());
      for (std::size_t h=0;h<hardware_copies.size();++h) {
        const auto destination = code[hardware_copies[h].front().second].register_name;
        const auto remember = [&](const passes::RegisterWebRecoloringRequest& request) {
          if (request.size()!=1U) return;
          const auto [a,b]=request.front();
          if (code[a].register_name==destination || code[b].register_name==destination)
            if (std::find(connected[h].begin(),connected[h].end(),request)==connected[h].end())
              connected[h].push_back(request);
        };
        for (const auto& pair:ordinary_equalities) remember({pair});
        for (const auto& request:epoch_demands) remember(request);
      }
      std::vector<passes::RegisterWebRecoloringRequest> joint_demands;
      std::size_t maximum_connected=0;
      for (const auto& family:connected) maximum_connected=std::max(maximum_connected,family.size());
      for (std::size_t diagonal=0;
           diagonal<primary_demands.size()+hardware_copies.size()+maximum_connected && joint_demands.size()<32U;
           ++diagonal)
        for (std::size_t p=0;p<primary_demands.size() && p<=diagonal && joint_demands.size()<32U;++p)
          for (std::size_t h=0;h<hardware_copies.size() && p+h<=diagonal && joint_demands.size()<32U;++h) {
            const auto ordinary=diagonal-p-h;
            if (ordinary>=connected[h].size()) continue;
            auto request=primary_demands[p];
            request.insert(request.end(),hardware_copies[h].begin(),hardware_copies[h].end());
            request.insert(request.end(),connected[h][ordinary].begin(),connected[h][ordinary].end());
            joint_demands.push_back(std::move(request));
          }
      if (stores.empty()) {
        // This is the new untagged-ABI family, not the historical tagged
        // request portfolio. Close the actual home-eviction chain instead of
        // spending its bounded product on unrelated first-store pairs.
        auto cycles = hardware_home_eviction_requests(
            code, primary_demands, hardware_copies, store_epochs);
        if (!cycles.empty()) joint_demands = std::move(cycles);
      }
      if(composition_trace && !joint_demands.empty())
        std::cerr<<"[joint-composition] hardware-epoch requests="<<joint_demands.size()
                 <<" self-reloads="<<hardware_copies.size()<<'\n';
      recolor_deferred(code,context,demands,control,root.targets,
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
      recolor_deferred(code,context,joint_demands,control,root.targets,
          [&](std::vector<IrOp> recolored) {
            auto next=root; next.artifact.items=lower_ir_to_machine(recolored);
            retire_moved_initializers(next);
            if(cells(next.artifact)>cells(artifact)+4) return;
            const auto same=[&](const auto& old) {
              return same_layout_search_artifact(old.artifact,next.artifact);
            };
            if(std::any_of(allocations.begin(),allocations.end(),same) ||
                std::any_of(encoded_allocations.begin(),encoded_allocations.end(),same) ||
                std::any_of(hardware_epoch_allocations.begin(),hardware_epoch_allocations.end(),same)) return;
            next.artifact.optimizations.push_back({"hardware-copy-epoch-affinity-product",
                "Jointly requested a hardware cursor home, a self-indexed reload/copy home and a connected later definition epoch. "
                "These are allocation preferences, not word equalities: re-proved complete interference, aliases, typed input "
                "and every hardware effect before independently pricing literal storage, finite coding and final layout."});
            hardware_epoch_allocations.push_back(std::move(next));
            std::stable_sort(hardware_epoch_allocations.begin(),hardware_epoch_allocations.end(),
                [&](const auto& a,const auto& b){return resource_key(a)<resource_key(b);});
            if(hardware_epoch_allocations.size()>8U) hardware_epoch_allocations.resize(8U);
          },&root.artifact.preloads,true);
    }
    for(auto& next:encoded_allocations)allocations.push_back(std::move(next));
    if(composition_trace)
      std::cerr<<"[joint-composition] hardware-epoch results="<<hardware_epoch_allocations.size()<<'\n';
    for(auto& next:hardware_epoch_allocations)allocations.push_back(std::move(next));
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
      const auto cfg=shared_post_layout_control_flow(root.artifact.items,control);
      const auto origins=detail::joint_target_origins(root.artifact.items,root.targets,root.encoded_selectors);
      if(!origins)continue;
      const auto selected=shared_deferred_selector_consistent_execution_graph(root.artifact.items,cfg,*origins);
      const auto flow=shared_post_layout_projected_flow(cfg, selected);
      if(!flow)continue;
      if(auto materialized_child=apply(root.artifact,passes::deferred_zero_underflow_literal(code,*flow,root.artifact.preloads))){
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
        recolor_deferred(code,context,demands,control,root.targets,
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
      if(current_origins)recolor_deferred(updated,context,demands,control,*current_origins,
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
      recolor_deferred(code,context,origin_web_requests,control,*origins,
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
        recolor_deferred(rewritten,context,demands,control,*new_origins,
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
    // An early private copy can make a later load/store pair disappear only
    // after lifetime repair. Keep this product independent of the old beam.
    std::vector<std::size_t> anticipation_order;
    for(std::size_t n=0;n<allocations.size();++n)anticipation_order.push_back(n);
    std::stable_sort(anticipation_order.begin(),anticipation_order.end(),[&](auto a,auto b){return resource_key(allocations[a])<resource_key(allocations[b]);});
    std::vector<ConstrainedRepresentation> anticipations;
    for(const auto n:anticipation_order){
      if(anticipations.size()>=4U)break;
      const auto root=allocations[n];
      const auto origins=detail::joint_target_origins(root.artifact.items,root.targets,root.encoded_selectors);
      if(!origins)continue;
      const auto code=raise_machine_to_ir(root.artifact.items,effective_optimizer_feature_profile(options));
      for(auto& anticipated:passes::contextual_anticipatory_copy_store_candidates(code,context,
          {.maximum_work=200000,.maximum_candidates=2,.maximum_copy_sites=8,.control=&control,
           .encoded_target_origins=&*origins,.runtime_preloads=&root.artifact.preloads})){
        auto rewritten=apply(root.artifact,std::move(anticipated.transformation));if(!rewritten)continue;
        auto next=root;next.artifact=std::move(*rewritten);if(!refresh_constraint_targets(next))continue;
        std::vector<ConstrainedRepresentation> family{next};
        const auto next_origins=detail::joint_target_origins(next.artifact.items,next.targets,next.encoded_selectors);
        if(next_origins)for(auto& erased:passes::deferred_stack_producer_erasure_candidates(
            raise_machine_to_ir(next.artifact.items,effective_optimizer_feature_profile(options)),context,control,*next_origins,
            {.maximum_candidates=2,.maximum_erased_cells=2,.maximum_proof_attempts=16,.allow_fresh_literal_replacement=true})){
          if(auto erased_artifact=apply(next.artifact,std::move(erased))){
            auto reduced=next;reduced.artifact=std::move(*erased_artifact);
            if(refresh_constraint_targets(reduced))family.push_back(std::move(reduced));
          }
        }
        for(auto& alternative:family){
          const auto same=[&](const auto& old){return same_layout_search_artifact(old.artifact,alternative.artifact)&&old.targets==alternative.targets&&old.encoded_selectors==alternative.encoded_selectors;};
          if(std::none_of(allocations.begin(),allocations.end(),same)&&std::none_of(anticipations.begin(),anticipations.end(),same))anticipations.push_back(std::move(alternative));
          if(anticipations.size()>=4U)break;
        }
        if(anticipations.size()>=4U)break;
      }
    }
    for(auto& next:anticipations)allocations.push_back(std::move(next));
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
    std::vector<std::size_t> nullable_order;
    for(std::size_t n=0;n<allocations.size();++n) nullable_order.push_back(n);
    std::stable_sort(nullable_order.begin(),nullable_order.end(),[&](auto a,auto b) {
      return resource_key(allocations[a])<resource_key(allocations[b]);
    });
    std::vector<ConstrainedRepresentation> nullable_continuations;
    std::size_t nullable_attempts=0;
    for(const auto n:nullable_order) {
      if(nullable_attempts>=8U || nullable_continuations.size()>=8U) break;
      const auto root=allocations[n];
      const auto origins=detail::joint_target_origins(root.artifact.items,root.targets,root.encoded_selectors);
      if(!origins) continue;
      const auto code=raise_machine_to_ir(root.artifact.items,effective_optimizer_feature_profile(options));
      ++nullable_attempts;
      for(auto& candidate:passes::contextual_nullable_callback_projection_candidates(code,context,
          {.maximum_work=200000,.maximum_candidates=2,.maximum_copy_sites=8,.control=&control,
           .encoded_target_origins=&*origins,.runtime_preloads=&root.artifact.preloads,
           .preserve_formal_execution_frames=preserve_formal_frames})) {
        auto rewritten=apply(root.artifact,std::move(candidate.transformation)); if(!rewritten) continue;
        auto next=root; next.artifact=std::move(*rewritten);
        for(const auto& [name,origin]:candidate.target_origins) next.targets[name]=origin;
        if(!refresh_constraint_targets(next)) continue;
        const auto same=[&](const auto& old) {
          return same_layout_search_artifact(old.artifact,next.artifact) &&
              old.targets==next.targets && old.encoded_selectors==next.encoded_selectors;
        };
        if(std::any_of(allocations.begin(),allocations.end(),same) ||
            std::any_of(nullable_continuations.begin(),nullable_continuations.end(),same)) continue;
        next.artifact.optimizations.push_back({"post-allocation-nullable-continuation-product",
            "Re-proved a canonical-zero readonly continuation after storage lifetime repair, "
            "before stable selector-family assignment and finite numeric encoding. Retained exact "
            "totality, selector death, hidden-state and return proofs; final address equations remain pending."});
        nullable_continuations.push_back(std::move(next));
        if(nullable_continuations.size()>=8U) break;
      }
    }
    for(auto& next:nullable_continuations) allocations.push_back(std::move(next));
    if(composition_trace)
      std::cerr<<"[joint-composition] nullable results="<<nullable_continuations.size()<<'\n';
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
    // Coloring, copy cleanup and pooling can expose a finite orbit that did
    // not exist in the earlier producer beam. Encode it after those choices,
    // preserving every historical root and its budgets independently.
    std::vector<std::size_t> late_order;
    for(std::size_t n=0;n<allocations.size();++n) late_order.push_back(n);
    std::stable_sort(late_order.begin(),late_order.end(),[&](auto a,auto b) {
      return resource_key(allocations[a])<resource_key(allocations[b]);
    });
    std::vector<ConstrainedRepresentation> late_encodings;
    std::size_t late_attempts=0;
    for(const auto n:late_order) {
      if(late_attempts>=8U || late_encodings.size()>=16U) break;
      const auto root=allocations[n];
      if(root.numeric_layout_certificate) continue;
      const auto code=raise_machine_to_ir(root.artifact.items,effective_optimizer_feature_profile(options));
      bool has_late_orbit=false;
      for(std::size_t at=0;at+2U<code.size();++at)
        has_late_orbit=has_late_orbit || (code[at].kind==IrKind::Recall && code[at+1U].opcode==0x0b &&
            code[at+2U].kind==IrKind::Store && code[at].register_name==code[at+2U].register_name);
      if(!has_late_orbit) continue;
      ++late_attempts;
      for(auto& codec:passes::deferred_finite_mode_representation_candidates(code,context,
          {.maximum_growth_cells=4,.maximum_candidates=2,.allow_target_relayout=true,
           .runtime_preloads=&root.artifact.preloads,.preserve_compiler_selector_charges=true,
           .assumed_target_addresses=&root.targets,.encode_preloaded_state=true,
           .precise_entry_facts=true,.preserve_formal_execution_frames=preserve_formal_frames,
           .relocate_canonical_formal_frames=preserve_formal_frames})) {
        auto encoded=apply(root.artifact,std::move(codec.transformation)); if(!encoded) continue;
        bool owned=true;
        for(const auto& replacement:codec.preload_replacements) {
          const auto owner=std::find_if(encoded->preloads.begin(),encoded->preloads.end(),[&](const auto& word) {
            return word.register_name==replacement.before.register_name;
          });
          if(owner==encoded->preloads.end() || !same_layout_search_artifact(
              {.preloads={*owner}}, {.preloads={replacement.before}})) {owned=false;break;}
          *owner=replacement.after;
        }
        if(!owned) continue;
        ConstrainedRepresentation next{std::move(*encoded),codec.required_target_addresses,
            root.encoded_selectors,codec.numeric_layout_certificate};
        if(!refresh_constraint_targets(next)) continue;
        const auto same=[&](const auto& old) {
          return same_layout_search_artifact(old.artifact,next.artifact) &&
              old.targets==next.targets && old.encoded_selectors==next.encoded_selectors;
        };
        if(std::any_of(allocations.begin(),allocations.end(),same) ||
            std::any_of(late_encodings.begin(),late_encodings.end(),same)) continue;
        next.artifact.optimizations.push_back({"late-allocation-finite-layout-product",
            "Re-encoded a closed finite orbit after independently proved lifetime, copy and literal-storage choices. "
            "A producer-owned numeric invariant may admit exact opaque bytes to placement only; raw arithmetic "
            "simplification remains forbidden and full data/selector/CFG/return proofs still close the transaction."});
        late_encodings.push_back(std::move(next));
        if(late_encodings.size()>=16U) break;
      }
    }
    if(composition_trace)
      std::cerr<<"[joint-composition] late encodings="<<late_encodings.size()<<" certified-raw="
               <<std::count_if(late_encodings.begin(),late_encodings.end(),[](const auto& node) {
                 return node.numeric_layout_certificate!=nullptr;
               })<<'\n';
    for(auto& next:late_encodings) allocations.push_back(std::move(next));
    if(transport_trace)std::cerr<<"[copy-transport-layout] with-codecs="<<allocations.size()<<"\n";
    for(auto node:allocations) {
      if(!refresh_constraint_targets(node))continue;
      // Allocation is still an equation-bearing state here. Price the scalar
      // domain, shared continuation and selector geometry before an ordinary
      // placement failure can discard this complete lifetime alternative.
      for (auto& edge : parametric_order_reduction_layout_transactions(
               node, options, control, cells(artifact))) {
        if (std::none_of(result.begin(), result.end(), [&](const auto& old) {
              return same_layout_search_artifact(old.node, edge.node);
            })) {
          edge.node.optimizations.push_back({"joint-allocation-parametric-continuation-layout",
              "Closed a proved scalar domain, complete register allocation and addressed "
              "continuation against the immutable transaction root. No intermediate "
              "geometry, local size or historical scalar seed is a publication criterion."});
           ordered_outcomes.push_back(std::move(edge));
        }
      }
      if(transport_trace){
        const auto has=[&](std::string_view name){return std::any_of(node.artifact.optimizations.begin(),node.artifact.optimizations.end(),[&](const auto& r){return r.name==name;});};
        std::cerr<<"[copy-transport-node] cells="<<cells(node.artifact)<<" homes="<<resource_key(node).second
                 <<" pool="<<has("allocation-exposed-literal-pool")<<" remat="<<has("deferred-zero-underflow-literal")
                 <<" abi="<<has("deferred-helper-operand-abi")<<"\n";
      }
      PreparedJointProofInput input(BorrowedJointProofInput{node.artifact.items, node.artifact.preloads}, control);
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
       joint_roots.push_back(ordered_outcomes.size());
       ordered_outcomes.push_back({std::move(*closed),"contextual-copy-lifetime-codec-layout",true});
      }
    }
  }
   // These searches have no external finalizer and do not read the result of
   // any sibling. Keep every original budget and replay the mixed direct/
   // searched outcomes in discovery order for the same deduplication and ties.
   detail::evaluate_layout_jobs(joint_roots.size(), [&](std::size_t index) {
     auto& root = ordered_outcomes[joint_roots[index]].node;
     auto search = search_joint_representation_constraints(root, options, {},
         {.maximum_expansions=6,.maximum_candidates=24,.maximum_depth=4,.maximum_growth_cells=4}, true);
     if (search.incumbent && cells(*search.incumbent) < cells(root))
       root = std::move(*search.incumbent);
   });
   for (auto& outcome : ordered_outcomes)
     if (cells(outcome.node) < cells(artifact) &&
         std::none_of(result.begin(), result.end(), [&](const auto& old) {
           return same_layout_search_artifact(old.node, outcome.node);
         }))
       result.push_back(std::move(outcome));
   if(composed.incumbent && cells(*composed.incumbent)<cells(artifact) &&
      std::none_of(result.begin(),result.end(),[&](const auto& old){
        return same_layout_search_artifact(old.node,*composed.incumbent);
      }))result.push_back({std::move(*composed.incumbent),"final-root-return-composition",true});
  std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return cells(a.node)<cells(b.node);});
  return result;
}

std::vector<ProvedSearchTransition<LayoutSearchArtifact>>
contextual_copy_transport_layout_transactions(const LayoutSearchArtifact& artifact,
                                              const CompileOptions& options);

static std::vector<ProvedSearchTransition<LayoutSearchArtifact>>
contextual_copy_transport_layout_transactions_uncached(const LayoutSearchArtifact& artifact,
                                                       const CompileOptions& options);

std::size_t contextual_negative_cache_hits_for_testing() {
  return contextual_negative_cache_hits;
}

std::size_t contextual_completed_cache_hits_for_testing() {
  return contextual_completed_cache_hits;
}

bool contextual_identity_expansion_demand_for_testing(const std::vector<IrOp>& code,
    const std::vector<PreloadReport>& preloads, const CompileOptions& options) {
  return contextual_identity_expansion_demand(code, preloads, options);
}

std::vector<ProvedSearchTransition<LayoutSearchArtifact>>
contextual_formal_copy_transport_layout_transactions_for_testing(
    const LayoutSearchArtifact& artifact, const CompileOptions& options) {
  return contextual_copy_transport_layout_transactions_impl(artifact, options, true);
}

std::size_t joint_completed_cache_hits_for_testing() {
  return joint_completed_cache_hits;
}

std::size_t joint_literal_closure_reuses_for_testing() {
  return joint_literal_closure_reuses;
}

std::optional<LayoutSearchArtifact> joint_closure_for_testing(
    const LayoutSearchArtifact& artifact, const CompileOptions& options,
    const PostLayoutControlFlowOptions& control, bool complete_preloads,
    bool projected, bool precise, const std::map<std::string, int>& encoded,
    const passes::DeferredSelectorDataLiteralLayout* pending, std::optional<int> exclusive_result_limit) {
  ConstrainedRepresentation node{artifact, {}, encoded};
  if (!refresh_constraint_targets(node)) return std::nullopt;
  PreparedJointProofInput input(pending ? pending->items : artifact.items,
                                artifact.preloads, control);
  return close_representation_constraints(node, passes::PassContext{options}, control,
      complete_preloads, projected, pending, input, precise, nullptr, exclusive_result_limit);
}

std::vector<passes::RegisterWebRecoloringRequest> hardware_epoch_storage_requests_for_testing(
    const std::vector<IrOp>& code, std::size_t maximum_requests) {
  return hardware_epoch_storage_requests(code, maximum_requests);
}

std::vector<passes::RegisterWebRecoloringRequest> hardware_home_eviction_requests_for_testing(
    const std::vector<IrOp>& code,
    const std::vector<passes::RegisterWebRecoloringRequest>& primary,
    const std::vector<passes::RegisterWebRecoloringRequest>& copies,
    std::size_t maximum_requests) {
  std::map<std::string, std::vector<std::size_t>> epochs;
  for (std::size_t at = 0; at < code.size(); ++at)
    if (code[at].kind == IrKind::Store && !code[at].meta.raw)
      epochs[code[at].register_name].push_back(at);
  return hardware_home_eviction_requests(code, primary, copies, epochs, maximum_requests);
}

std::optional<LayoutSearchArtifact> contextual_transport_identity_for_testing(
    const LayoutSearchArtifact& artifact, const CompileOptions& options,
    bool normalize_identity) {
  const auto view = representation_view(artifact, options);
  if (!view) return std::nullopt;
  const auto symbolic = lower_ir_to_machine(*view);
  const auto positions = constraint_positions(symbolic);
  if (!positions) return std::nullopt;
  const auto root = contextual_transport_root(
      artifact, symbolic, *positions, {.ops = *view}, normalize_identity);
  return root ? std::optional<LayoutSearchArtifact>{root->artifact} : std::nullopt;
}

std::optional<LayoutSearchArtifact> contextual_transport_identity_for_testing(
    const LayoutSearchArtifact& artifact, const CompileOptions& options) {
  return contextual_transport_identity_for_testing(artifact, options, true);
}

std::optional<std::pair<LayoutSearchArtifact, std::map<std::string,int>>>
contextual_reopened_allocation_root_for_testing(const LayoutSearchArtifact& artifact,
    const CompileOptions& options, int maximum_growth_cells) {
  const auto view = representation_view(artifact, options);
  if (!view) return std::nullopt;
  ConstrainedRepresentation root{artifact, {}};
  root.artifact.items = lower_ir_to_machine(*view);
  if (!refresh_constraint_targets(root)) return std::nullopt;
  PostLayoutControlFlowOptions control;
  control.empty_return_target = 1;
  control.address_space_model = address_space_model_for_feature_profile(
      effective_optimizer_feature_profile(options));
  auto opened = reopened_allocation_topology(root, control, maximum_growth_cells);
  if (!opened) return std::nullopt;
  return std::pair{std::move(opened->artifact), std::move(opened->targets)};
}

std::vector<ProvedSearchTransition<LayoutSearchArtifact>>
contextual_copy_transport_layout_transactions(const LayoutSearchArtifact& artifact,
                                              const CompileOptions& options) {
  struct Entry {
    std::string options;
    LayoutSearchArtifact root;
    std::vector<ProvedSearchTransition<LayoutSearchArtifact>> result;
  };
  static thread_local std::vector<Entry> completed;
  const auto key=representation_context_key(options);
  const auto same_reports=[](const auto& a,const auto& b){
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](const auto& x,const auto& y){
      return x.name==y.name && x.detail==y.detail;
    });
  };
  for(auto at=completed.begin();at!=completed.end();++at)
    if(at->options==key && same_layout_search_artifact(at->root,artifact) &&
        same_reports(at->root.optimizations,artifact.optimizations)) {
      ++contextual_completed_cache_hits;
      if (at->result.empty()) ++contextual_negative_cache_hits;
      std::rotate(completed.begin(),at,std::next(at));
      if(std::getenv("MKPRO_NATIVE_TRACE_JOINT_CACHE"))
        std::cerr<<"[joint-layout-cache] completed contextual search reused\n";
      return completed.front().result;
    }
  auto result=contextual_copy_transport_layout_transactions_uncached(artifact,options);
  // Cache only completed outcomes. No in-progress/throwing search or partial
  // witness escapes, and no frontier, proof or candidate budget is reduced.
  if(completed.size()>=32U)completed.pop_back();
  completed.insert(completed.begin(),Entry{key,artifact,result});
  return result;
}

static std::vector<ProvedSearchTransition<LayoutSearchArtifact>>
contextual_copy_transport_layout_transactions_uncached(const LayoutSearchArtifact& artifact,
                                                       const CompileOptions& options) {
  auto result = contextual_copy_transport_layout_transactions_impl(artifact, options);
  const auto model = address_space_model_for_feature_profile(effective_optimizer_feature_profile(options));
  // Independent complete-artifact portfolio: historical candidates retain
  // their entire budgets. No extra multiplier is added to fitting profiles.
  if (cells(artifact) <= official_program_step_limit(model)) return result;
  // A separate complete-artifact lane supplies the formal relocation theorem
  // that the historical IR-only API deliberately lacks. Never spend the old
  // lane's budgets on it or treat a formal byte as an ordinary numeric target.
  if ((options.ordinary_decimal_keyboard_input ||
       std::none_of(artifact.items.begin(), artifact.items.end(), [](const auto& item) {
         return item.manual_interaction.has_value();
       })) &&
      std::any_of(artifact.items.begin(), artifact.items.end(), [](const auto& item) {
        return item.kind == MachineItemKind::Address && item.formal_opcode.has_value();
      })) {
    auto formal = contextual_copy_transport_layout_transactions_impl(artifact, options, true);
    result.insert(result.end(), std::make_move_iterator(formal.begin()),
                  std::make_move_iterator(formal.end()));
  }
  auto pooled = addressed_return_pooling_layout_transactions(artifact, options);
  result.insert(result.end(), std::make_move_iterator(pooled.begin()),
                std::make_move_iterator(pooled.end()));
  for(auto& folded:zero_underflow_conditional_return_layout_transactions(artifact,options))
    if(std::none_of(result.begin(),result.end(),[&](const auto& old){
      return same_layout_search_artifact(old.node,folded.node);
    }))result.push_back(std::move(folded));
  auto view = representation_view(artifact, options);
  if (!view) return result;
  ConstrainedRepresentation root{artifact, {}, {}};
  root.artifact.items = lower_ir_to_machine(*view);
  if (!refresh_constraint_targets(root)) return result;
  const auto origins = detail::joint_target_origins(root.artifact.items, root.targets, root.encoded_selectors);
  if (!origins) return result;
  CompileOptions configured = options;
  configured.coalesce_copies = true;
  configured.defer_return_suffix_until_callee_hole = true;
  PostLayoutControlFlowOptions control;
  control.empty_return_target = 1;
  control.address_space_model = model;
  const auto candidates = passes::contextual_edge_return_completion_candidates(*view, {configured},
      {.maximum_work=200000, .maximum_candidates=2, .maximum_copy_sites=8,
       .control=&control, .encoded_target_origins=&*origins,
       .runtime_preloads=&artifact.preloads, .preserve_formal_execution_frames=true});
  for (const auto& candidate : candidates) {
    auto semantic = apply(root.artifact, candidate.transformation);
    if (!semantic) continue;
    // The historical lane has already priced value/allocation alternatives.
    // Close this neutral edge with the independent stable-tail/selector layout,
    // not another recursive replay of the entire representation portfolio.
    // Every accepted fold strictly decreases cells and has a complete final
    // artifact proof. Repeated folds compose without a growth ratchet.
    bool folded = false;
    while (auto next = close_final_stable_tail_layout(*semantic, options)) {
      semantic = std::move(next);
      folded = true;
    }
    if (!folded || cells(*semantic) >= cells(artifact) ||
        std::any_of(result.begin(), result.end(), [&](const auto& old) {
          return same_layout_search_artifact(old.node, *semantic);
        })) continue;
    semantic->optimizations.push_back({"joint-edge-return-selector-layout",
        "Closed a neutral edge-qualified total leaf with independently proved stable-tail, "
        "selector and placement transactions. Rechecked all data/selector obligations, "
        "formal counters, numeric entry, X1/X2 and rooted return frames. Every fold "
        "strictly decreases complete artifact size; the historical representation "
        "portfolio is not recursively replayed."});
    result.push_back({std::move(*semantic), "joint-edge-return-selector-layout", true});
  }
  std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
    return cells(a.node) < cells(b.node);
  });
  return result;
}

std::vector<ProvedSearchTransition<LayoutSearchArtifact>>
order_reduction_selector_layout_transactions(const LayoutSearchArtifact& root,
                                             const CompileOptions& options) {
  std::vector<ProvedSearchTransition<LayoutSearchArtifact>> result;
  if (std::none_of(root.items.begin(), root.items.end(), [](const auto& item) {
        return item.kind == MachineItemKind::Op && !item.raw && item.opcode == 0x36;
      })) return result;
  const auto view = representation_view(root, options);
  if (!view) return result;
  auto symbolic = lower_ir_to_machine(*view);
  const auto origins = constraint_positions(symbolic);
  if (!origins) return result;
  CompileOptions configured = options;
  configured.coalesce_copies = true;
  configured.defer_return_suffix_until_callee_hole = true;
  const passes::PassContext context{configured};
  PostLayoutControlFlowOptions control;
  control.address_space_model = address_space_model_for_feature_profile(
      effective_optimizer_feature_profile(options));
  control.empty_return_target = 1;
  ConstrainedRepresentation initial{{std::move(symbolic), root.preloads,
                                      root.optimizations}, *origins, {}};
  if (!refresh_constraint_targets(initial)) return result;
  std::vector<ConstrainedRepresentation> sources{initial};
  auto reopened = reopen_shared_helper_dual_mode_layout(initial.artifact.items, control);
  if (reopened.applied > 0 && transaction_growth_fits(cells(root),
          cells({.items = reopened.items}), kJointRepresentationConstraintLimits.maximum_growth_cells)) {
    auto source = initial;
    source.artifact.items = std::move(reopened.items);
    for (const auto& report : reopened.optimizations)
      source.artifact.optimizations.push_back({report.name, report.detail});
    if (refresh_constraint_targets(source)) sources.push_back(std::move(source));
  }
  for (const auto& source : sources)
    for (const auto& candidate : order_reduction_translation_roots(source, configured, control))
      for (const auto& pending : cached_literal_layouts(
               candidate.artifact.items, control, 4)) {
        PreparedJointProofInput proof_input(BorrowedJointProofInput{pending.items, candidate.artifact.preloads}, control);
        for (const bool projected : {false, true}) {
          auto closed = close_representation_constraints(candidate, context, control,
               true, projected, &pending, proof_input, true, nullptr, cells(root));
          if (!closed || cells(*closed) >= cells(root)) continue;
          const auto flow = build_post_layout_control_flow(closed->items, control);
          if (!flow.proved || !literal_family_owns_charges(pending, closed->items) ||
              !passes::prove_deferred_selector_data_literal_layout(pending, closed->items, flow))
            continue;
          if (std::any_of(result.begin(), result.end(), [&](const auto& old) {
                return same_layout_search_artifact(old.node, *closed);
              })) continue;
          closed->optimizations.push_back({"order-reduction-selector-layout",
              "Atomically translated a proved private order-only reduction, reopened "
              "certified sharing when necessary, and closed data/selector placement. "
              "Preserved the independently completed ordinary optimizer root; "
              "no intermediate seed replays the whole finalization portfolio."});
          result.push_back({std::move(*closed), "order-reduction-selector-layout", true});
        }
      }
  // This independent lane keeps the scalar as a proved layout domain rather
  // than enumerating/fixing a value before final component placement.
  for (auto& candidate : parametric_order_reduction_layout_transactions(initial, configured, control))
    if (std::none_of(result.begin(), result.end(), [&](const auto& old) {
          return same_layout_search_artifact(old.node, candidate.node);
        })) result.push_back(std::move(candidate));
  std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
    return cells(a.node) < cells(b.node);
  });
  return result;
}


// One compound edge: reopen independently certified sharing, then compose
// value representation and ignored-return work before choosing any geometry.
// Intermediate debt is private; only smaller fully closed artifacts escape.
std::vector<ProvedSearchTransition<LayoutSearchArtifact>>
nullable_callback_projection_layout_transactions(const LayoutSearchArtifact& root,
                                                  const CompileOptions& options) {
  using Edge=ProvedSearchTransition<LayoutSearchArtifact>;
  std::vector<Edge> result;
  // This traversal needs ordinary numeric domains at external keyboard
  // epochs. Preserve the independent unrestricted-input search instead of
  // repeatedly building the expensive joint proof for an unknown manual ABI.
  // Pure internal programs may still prove their domains without this opt-in.
  if (!options.ordinary_decimal_keyboard_input &&
      std::any_of(root.items.begin(), root.items.end(),
          [](const auto& item) { return item.manual_interaction.has_value(); }))
    return result;
  bool unit=false,literal=false;
  for(std::size_t i=0;i<root.items.size();++i){
    const auto& item=root.items[i];
    auto next=i+1U;while(next<root.items.size()&&root.items[next].kind==MachineItemKind::Label)++next;
    unit=unit || (item.kind==MachineItemKind::Op && !item.raw && item.opcode==0x32 &&
        next<root.items.size() && root.items[next].kind==MachineItemKind::Op &&
        root.items[next].opcode>=0x40 && root.items[next].opcode<=0x4e);
    literal=literal || std::any_of(item.roles.begin(),item.roles.end(),
        [](const auto& role){return role.starts_with("selector-sunk-literal-id:");});
  }
  if(!unit || !literal)return result;
  const bool trace=std::getenv("MKPRO_NATIVE_TRACE_COPY_TRANSPORT")!=nullptr;
  if(trace)std::cerr<<"[nullable-root] cells="<<cells(root)
      <<" ordinary="<<options.ordinary_decimal_keyboard_input<<'\n';
  const auto view=representation_view(root,options);if(!view)return result;
  auto configured=options;configured.defer_return_suffix_until_callee_hole=true;
  const passes::PassContext context{configured};
  PostLayoutControlFlowOptions control;control.empty_return_target=1;
  control.address_space_model=address_space_model_for_feature_profile(effective_optimizer_feature_profile(options));
  auto positions=constraint_positions(lower_ir_to_machine(*view));if(!positions)return result;
  ConstrainedRepresentation source{{lower_ir_to_machine(*view),root.preloads,root.optimizations},*positions,{}};
  if(!refresh_constraint_targets(source))return result;
  // Restoring the old literal and helper ABI is not an independently admitted
  // growth edge. Its complete debt must be repaid by this same transaction.
  for(std::size_t i=0;i<source.artifact.items.size();++i){
    const auto& item=source.artifact.items[i];
    if(std::find(item.roles.begin(),item.roles.end(),"selector-sunk-literal-coalesced")==item.roles.end())continue;
    std::optional<std::string> value;
    for(const auto& role:item.roles)if(role.starts_with("selector-sunk-literal-value:"))
      value=role.substr(std::string("selector-sunk-literal-value:").size());
    if(!value)return result;
    const auto opened=reopen_order_reduction_input(source,i,*value,control);
    if(!opened)return result;source=*opened;break;
  }
  const auto reopened=reopen_shared_helper_dual_mode_layout(source.artifact.items,control);
  if(reopened.applied>0){
    source.artifact.items=reopened.items;
    for(const auto& report:reopened.optimizations)source.artifact.optimizations.push_back({report.name,report.detail});
    if(!refresh_constraint_targets(source))return result;
  }
  std::vector<ConstrainedRepresentation> entry_abis{source};
  if(const auto encoded=detail::joint_target_origins(source.artifact.items,source.targets,source.encoded_selectors))
    for(auto& proposal:passes::contextual_memory_backed_entry_candidates(
        raise_machine_to_ir(source.artifact.items,effective_optimizer_feature_profile(options)),context,
        {.maximum_work=400000,.maximum_candidates=2,.maximum_copy_sites=8,.maximum_growth_cells=4,
         .control=&control,.encoded_target_origins=&*encoded,.runtime_preloads=&source.artifact.preloads,
         .preserve_formal_execution_frames=true})){
      if(auto image=apply(source.artifact,std::move(proposal.transformation))){
        auto variant=source;variant.artifact=std::move(*image);
        if(refresh_constraint_targets(variant))entry_abis.push_back(std::move(variant));
      }
    }
  if(trace)std::cerr<<"[nullable-root] entry-abis="<<entry_abis.size()<<'\n';
  for(const auto& entry_abi:entry_abis){
  auto folded=zero_underflow_conditional_return(entry_abi,context,control);if(!folded)continue;
  const auto origins=detail::joint_target_origins(folded->artifact.items,folded->targets,folded->encoded_selectors);
  if(!origins)continue;
  const auto proposals=passes::contextual_nullable_callback_projection_candidates(
      raise_machine_to_ir(folded->artifact.items,effective_optimizer_feature_profile(options)),context,
      {.maximum_work=400000,.maximum_candidates=2,.maximum_copy_sites=8,.maximum_growth_cells=4,
       .control=&control,.encoded_target_origins=&*origins,.runtime_preloads=&folded->artifact.preloads,
       .preserve_formal_execution_frames=true});
  if(trace)std::cerr<<"[nullable-root] completion-proposals="<<proposals.size()<<'\n';
  for(const auto& proposal:proposals){
    auto image=apply(folded->artifact,proposal.transformation);if(!image)continue;
    ConstrainedRepresentation next{std::move(*image),folded->targets,folded->encoded_selectors};
    for(const auto& [label,origin]:proposal.target_origins)next.targets[label]=origin;
    if(!refresh_constraint_targets(next) || !transaction_growth_fits(cells(root),cells(next.artifact),4))continue;
    for(const auto& pending:cached_literal_layouts(next.artifact.items,control,2)){
      PreparedJointProofInput proof(BorrowedJointProofInput{pending.items, next.artifact.preloads}, control);
       auto closed=close_representation_constraints(next,context,control,true,true,
           &pending,proof,true,nullptr,cells(root));
      if(trace)std::cerr<<"[nullable-root] closed="<<bool(closed)
          <<" cells="<<(closed?cells(*closed):0)<<'\n';
      if(!closed || cells(*closed)>=cells(root) || shared_helper_dual_mode_closure_rejection_reason(closed->items,control.address_space_model))continue;
      closed=rebind_layout_search_artifact(std::move(*closed));if(!closed)continue;
      const auto final=build_post_layout_control_flow(closed->items,control);
      if(!final.proved || !passes::prove_deferred_selector_data_literal_layout(pending,closed->items,final))continue;
      result.push_back({std::move(*closed),"nullable-callback-projection-layout",true});
    }
  }
  }
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
  struct Completed {
    std::string options;
    LayoutSearchArtifact root;
    TransactionSearchLimits limits;
    bool complete_preloads = false;
    TransactionSearchResult<LayoutSearchArtifact> result;
  };
  static thread_local std::vector<Completed> completed;
  // Arbitrary callbacks have no cacheable identity. Only the entirely pure
  // search may share completed outcomes across independent layout roots.
  const bool cacheable = !finalize && !finalize_intermediate;
  const auto cache_key = cacheable ? representation_context_key(options) : std::string{};
  if (cacheable)
    for (auto at = completed.begin(); at != completed.end(); ++at)
      if (at->options == cache_key && at->complete_preloads == preload_environment_complete &&
          same_joint_limits(at->limits, limits) &&
          same_layout_search_artifact(at->root, root)) {
        ++joint_completed_cache_hits;
        std::rotate(completed.begin(), at, std::next(at));
        if (std::getenv("MKPRO_NATIVE_TRACE_JOINT_CACHE"))
          std::cerr << "[joint-layout-cache] completed pure frontier reused\n";
        auto reused = completed.front().result;
        if (reused.incumbent) {
          // Pure machine search does not inspect the incoming report history.
          // Its completed witness is relative to that history, not permission
          // to publish a different caller's source/static-gate evidence.
          auto& reports = reused.incumbent->optimizations;
          reports.erase(reports.begin(), reports.begin() +
              static_cast<std::ptrdiff_t>(completed.front().root.optimizations.size()));
          reports.insert(reports.begin(), root.optimizations.begin(), root.optimizations.end());
        }
        return reused;
      }
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
  // New observer precision must never consume the historical beam. Run its
  // formal-capable contextual alternatives as an independent complete lane;
  // equal-size results cannot replace the already proved incumbent.
  const bool formal_context_demand=preload_environment_complete &&
      std::any_of(root.items.begin(),root.items.end(),[](const auto& item){
        return item.kind==MachineItemKind::Address && item.formal_opcode.has_value();
      });
  if(formal_context_demand && limits.maximum_expansions && limits.maximum_candidates && limits.maximum_depth){
    auto formal=search_joint_representation_constraints_impl(root,options,strict,limits,true,
        ProloguePartitionLane::Disabled,proof_counts,intermediate,true,nullptr,-1,true);
    if(formal.incumbent && (!result.incumbent || cells(*formal.incumbent)<cells(*result.incumbent))){
      formal.incumbent->optimizations.push_back({"joint-formal-context-portfolio",
          "Composed formal-preserving contextual transformations in a separate bounded frontier; "
          "kept the historical lane's budget, finalizer and equal-size incumbent unchanged."});
      result.incumbent=std::move(formal.incumbent);
    }
    result.expanded+=formal.expanded;result.admitted+=formal.admitted;
    result.duplicate+=formal.duplicate;result.rejected+=formal.rejected;
    result.budget_exhausted=result.budget_exhausted||formal.budget_exhausted;
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
  if(preload_environment_complete)for(bool scalar_leaf:{false,true}){
    auto composed=close_quantized_return_composition(root,options,strict,limits,scalar_leaf);
    if(composed.incumbent && (!result.incumbent || cells(*composed.incumbent)<cells(*result.incumbent)))
      result.incumbent=std::move(composed.incumbent);
    result.expanded+=composed.expanded;result.admitted+=composed.admitted;
  }
  // Reopen physical sharing, erase a proved redundant literal, then solve
  // complete selector families and projected geometry together. A local cell
  // saving is not a publication result. This independent lane cannot consume
  // the historical beams or replace their equal-size incumbents.
  if (preload_environment_complete && limits.maximum_expansions &&
      limits.maximum_candidates && limits.maximum_depth) {
    const auto view = representation_view(root, options);
    if (view) {
      CompileOptions configured = options;
      configured.coalesce_copies = true;
      configured.defer_return_suffix_until_callee_hole = true;
      PostLayoutControlFlowOptions control;
      control.address_space_model = address_space_model_for_feature_profile(
          effective_optimizer_feature_profile(options));
      control.empty_return_target = 1;
      ConstrainedRepresentation base{root, {}, {}};
      base.artifact.items = lower_ir_to_machine(*view);
      if (refresh_constraint_targets(base)) {
        std::vector<ConstrainedRepresentation> sources{base};
        auto reopened = reopen_shared_helper_dual_mode_layout(base.artifact.items, control);
        if (reopened.applied > 0 && reopened.added_cells <= limits.maximum_growth_cells) {
          auto expanded = base;
          expanded.artifact.items = std::move(reopened.items);
          if (refresh_constraint_targets(expanded)) sources.push_back(std::move(expanded));
        }
        for (const auto& source : sources) {
          const auto origins = detail::joint_target_origins(
              source.artifact.items, source.targets, source.encoded_selectors);
          if (!origins) continue;
          for (auto& erased : passes::contextual_redundant_literal_store_candidates(
                   raise_machine_to_ir(source.artifact.items), {configured},
                   {.maximum_work=200000, .maximum_candidates=2, .maximum_copy_sites=16,
                    .control=&control, .encoded_target_origins=&*origins,
                    .runtime_preloads=&source.artifact.preloads,
                    .preserve_formal_execution_frames=true})) {
            auto candidate = apply(source.artifact, std::move(erased.transformation));
            if (!candidate || !transaction_growth_fits(
                    cells(root), cells(*candidate), limits.maximum_growth_cells)) continue;
            auto pending = source;
            pending.artifact = std::move(*candidate);
            if (!refresh_constraint_targets(pending)) continue;
            auto composed = search_joint_representation_constraints_impl(
                pending.artifact, options, strict, limits, true,
                ProloguePartitionLane::Disabled, proof_counts, intermediate, true,
                &pending, cells(root));
            if (composed.incumbent && (!result.incumbent ||
                cells(*composed.incumbent) < cells(*result.incumbent)))
              result.incumbent = std::move(composed.incumbent);
            result.expanded += composed.expanded;
            result.admitted += composed.admitted;
            result.duplicate += composed.duplicate;
            result.rejected += composed.rejected;
            result.budget_exhausted = result.budget_exhausted || composed.budget_exhausted;
          }
        }
      }
    }
  }

  const bool retained_report_prefix = !result.incumbent ||
      (result.incumbent->optimizations.size() >= root.optimizations.size() &&
       std::equal(root.optimizations.begin(), root.optimizations.end(),
           result.incumbent->optimizations.begin(), [](const auto& a, const auto& b) {
             return a.name == b.name && a.detail == b.detail;
           }));
  if (cacheable && retained_report_prefix) {
    if (completed.size() >= 32U) completed.pop_back();
    completed.insert(completed.begin(), Completed{
        cache_key, root, limits, preload_environment_complete, result});
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
