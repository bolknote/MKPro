#include "mkpro/core/joint_representation_search.hpp"

#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/passes/register_coalesce.hpp"
#include "mkpro/core/passes/semantic_region_folding.hpp"
#include "mkpro/core/passes/shared_straight_line_helper.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"

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
  return result;
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
