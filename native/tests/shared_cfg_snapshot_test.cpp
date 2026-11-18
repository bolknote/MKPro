#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/detail/layout_jobs.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;

class CacheMode {
 public:
  explicit CacheMode(const char* mode) {
    if (const auto* old = std::getenv("MKPRO_NATIVE_CFG_CACHE")) old_ = old;
    set(mode);
  }
  ~CacheMode() { set(old_ ? old_->c_str() : nullptr); }
 private:
  static void set(const char* mode) {
#ifdef _WIN32
    (void)_putenv_s("MKPRO_NATIVE_CFG_CACHE", mode ? mode : "");
#else
    if (mode) (void)setenv("MKPRO_NATIVE_CFG_CACHE", mode, 1);
    else (void)unsetenv("MKPRO_NATIVE_CFG_CACHE");
#endif
  }
  std::optional<std::string> old_;
};

bool same_facts(const AuthoritativePostLayoutControlFlow& a,
                const AuthoritativePostLayoutControlFlow& b) {
  const auto fields = [](const auto& flow) {
    return std::tie(flow.proved, flow.address_space_model, flow.external_entries,
        flow.indirect_flow_targets, flow.indirect_memory_targets, flow.empty_return_target,
        flow.execution_states, flow.execution_successors, flow.execution_edges,
        flow.maximum_observed_return_depth, flow.explored_states, flow.reasons);
  };
  return fields(a) == fields(b);
}

std::vector<MachineItem> nested_calls(int depth) {
  std::vector<MachineItem> items{MachineItem::op(0x53, "call"),
      MachineItem::address(std::string("leaf0")), MachineItem::op(0x50, "stop")};
  items.back().stop_disposition = StopDisposition::Terminal;
  for (int level = 0; level < depth; ++level) {
    items.push_back(MachineItem::label("leaf" + std::to_string(level)));
    if (level + 1 < depth) {
      items.push_back(MachineItem::op(0x53, "call"));
      items.push_back(MachineItem::address("leaf" + std::to_string(level + 1)));
    } else {
      items.push_back(MachineItem::op(7, "value"));
    }
    items.push_back(MachineItem::op(0x52, "return"));
  }
  return items;
}

void compact_row_contracts() {
  using Kind = PostLayoutExecutionEdgeKind;
  const PostLayoutExecutionEdge fallthrough{2, Kind::Fallthrough, std::nullopt};
  const PostLayoutExecutionEdge target{2, Kind::IndirectTarget, 0xfb};
  const PostLayoutExecutionEdge alias{2, Kind::IndirectTarget, 0xeb};
  PostLayoutEdges original{{fallthrough, target, alias}};
  auto copy = original;
  copy.front().erase_if([&](const auto& edge) { return edge == target; });
  require(copy.front() == std::vector{fallthrough, alias} &&
          original.front() == std::vector{fallthrough, target, alias},
          "overflow edges retain distinct formal aliases and independent value copies");
  copy.front().clear();
  copy.front().push_back(alias);
  copy.front().push_back(target);
  copy.front().erase_if([&](const auto& edge) { return edge == alias; });
  require(copy.front() == std::vector{target},
          "inline and overflow rows preserve the order of surviving edges");
  detail::InlineProofRow<int, 5> frames{6, 7, 8, 9, 10, 11};
  auto moved = std::move(frames);
  require(moved == std::vector{6, 7, 8, 9, 10, 11}, "deep supplied frame rows never truncate");
}

void snapshot_contracts(AddressSpaceModel model) {
  CacheMode enabled("1");
  auto items = nested_calls(5);
  PostLayoutControlFlowOptions options;
  options.address_space_model = model;
  const auto snapshot = shared_post_layout_control_flow(items, options);
  static_assert(std::is_const_v<std::remove_reference_t<decltype(*snapshot)>>);
  require(snapshot->proved && snapshot->maximum_observed_return_depth == 5,
          "all five exact return frames are explored");
  require(shared_post_layout_control_flow(items, options) == snapshot,
          "an exact full-key hit shares the immutable graph without copying");
  const auto deepest = std::find_if(snapshot->execution_states.begin(), snapshot->execution_states.end(),
      [](const auto& state) { return state.address == 15 && state.return_stack.size() == 5; });
  require(deepest != snapshot->execution_states.end() &&
          deepest->return_stack == std::vector<int>({2, 5, 8, 11, 14}),
          "inline keys retain exact frame order and every continuation");
  {
    CacheMode disabled("0");
    const auto first = shared_post_layout_control_flow(items, options);
    const auto second = shared_post_layout_control_flow(items, options);
    require(first != second && same_facts(*first, *snapshot) && same_facts(*second, *snapshot),
            "uncached exploration produces exactly the same complete fact set");
  }
  auto mutable_copy = build_post_layout_control_flow(items, options);
  require(same_facts(mutable_copy, *snapshot), "the value API retains identical facts");
  mutable_copy.proved = false;
  mutable_copy.execution_states.clear();
  mutable_copy.execution_edges.clear();
  require(snapshot->proved && !snapshot->execution_states.empty() &&
          same_facts(*shared_post_layout_control_flow(items, options), *snapshot),
          "mutating a value copy cannot corrupt a shared proof input");

  auto changed = items;
  changed.front().source_line = 29;
  require(shared_post_layout_control_flow(changed, options) != snapshot,
          "even descriptive item metadata remains part of exact cache identity");
  auto other_model = options;
  other_model.address_space_model = model == AddressSpaceModel::Standard
      ? AddressSpaceModel::Mk61SMiniExpanded : AddressSpaceModel::Standard;
  const auto other = shared_post_layout_control_flow(items, other_model);
  require(other != snapshot && other->address_space_model == other_model.address_space_model,
          "stock and 105+7 memory models cannot share a certificate");
  auto shallow = options;
  shallow.maximum_return_depth = 4;
  require(!shared_post_layout_control_flow(items, shallow)->proved,
          "configured depth limits still fail closed");
  auto invalid = options;
  invalid.maximum_return_depth = 6;
  require(!shared_post_layout_control_flow(items, invalid)->proved &&
          !shared_post_layout_control_flow(nested_calls(6), options)->proved,
          "neither configuration nor a sixth runtime frame can overrun the hardware bound");
  auto exhausted = options;
  exhausted.maximum_execution_states = 1;
  require(!shared_post_layout_control_flow(items, exhausted)->proved,
          "cache identity cannot bypass an exhausted exploration cap");
  auto bad_entry = options;
  bad_entry.main_entry = 1;
  require(!shared_post_layout_control_flow(items, bad_entry)->proved,
          "entry options cannot reuse an unrelated successful proof");
  for (int index = 1; index <= 17; ++index) {
    auto variant = items;
    variant.front().source_line = index;
    require(shared_post_layout_control_flow(variant, options)->proved, "eviction fixture is valid");
  }
  const auto renewed = shared_post_layout_control_flow(items, options);
  require(renewed != snapshot && same_facts(*renewed, *snapshot),
          "an owning snapshot survives bounded cache eviction unchanged");
}

void compilation_scope_contracts() {
  CacheMode enabled("1");
  const auto cache = detail::make_post_layout_proof_cache();
  const detail::PostLayoutProofCacheScope scope(cache);
  auto items = nested_calls(5);
  std::vector<std::shared_ptr<const AuthoritativePostLayoutControlFlow>> graphs(4);
  detail::evaluate_layout_jobs(graphs.size(), [&](std::size_t index) {
    auto variant = items;
    variant[0].comment = std::to_string(index);
    variant[0].source_line = static_cast<int>(index);
    graphs[index] = shared_post_layout_control_flow(variant);
  }, 4);
  auto stats = detail::post_layout_proof_cache_stats(*cache);
  require(graphs.front()->proved && std::all_of(graphs.begin(), graphs.end(), [&](const auto& graph) {
            return graph == graphs.front();
          }) && stats.cfg_builds == 1 && stats.cfg_hits == 3,
          "one exact CFG is published to all workers, independent of comments and source lines");

  auto changed_opcode = items;
  auto digit = std::find_if(changed_opcode.begin(), changed_opcode.end(), [](const auto& item) {
    return item.kind == MachineItemKind::Op && item.opcode == 7;
  });
  digit->opcode = 8;
  require(shared_post_layout_control_flow(changed_opcode) == graphs.front(),
          "ordinary hardware digits share the control-only snapshot");
  auto changed_data = changed_opcode;
  changed_data[static_cast<std::size_t>(digit - changed_opcode.begin())].opcode = 0x34;
  require(shared_post_layout_control_flow(changed_data) == graphs.front(),
          "ordinary data operators cannot change the control-only snapshot");
  {
    const auto isolated = detail::make_post_layout_proof_cache();
    const detail::PostLayoutProofCacheScope isolated_scope(isolated);
    const auto separate = shared_post_layout_control_flow(changed_data);
    require(separate != graphs.front() && same_facts(*separate, *graphs.front()) &&
            shared_post_layout_control_flow(changed_data) == separate,
            "a recent worker-local hit belongs only to its compilation owner");
    const auto isolated_stats = detail::post_layout_proof_cache_stats(*isolated);
    require(isolated_stats.cfg_builds == 1 && isolated_stats.cfg_hits == 1,
            "worker-local hits are counted without recomputing the graph");
  }
  require(shared_post_layout_control_flow(changed_data) == graphs.front(),
          "leaving a nested compilation restores its parent's snapshots");
  std::vector<MachineItem> encoded{MachineItem::op(0x53, "call"),
      MachineItem::op(4, "operand"), MachineItem::op(0x50, "stop"),
      MachineItem::op(0x52, "return"), MachineItem::op(0x52, "return")};
  encoded[2].stop_disposition = StopDisposition::Terminal;
  PostLayoutControlFlowOptions encoded_control;
  encoded_control.opcode_address_words = {1};
  const auto original_operand = shared_post_layout_control_flow(encoded, encoded_control);
  encoded[1].opcode = 3;
  const auto changed_operand_byte = shared_post_layout_control_flow(encoded, encoded_control);
  require(original_operand->proved && changed_operand_byte->proved &&
          original_operand != changed_operand_byte &&
          original_operand->execution_states[1].address == 4 &&
          changed_operand_byte->execution_states[1].address == 3,
          "a command fetched as an address operand must retain its exact byte");
  for (const auto model : {AddressSpaceModel::Standard, AddressSpaceModel::Mk61SMiniExpanded}) {
    const auto last = official_program_last_address(model);
    std::vector<MachineItem> wrapped(static_cast<std::size_t>(last + 1), MachineItem::op(0x54, "nop"));
    wrapped.front() = MachineItem::op(4, "wrapped operand");
    wrapped[1] = encoded[2];
    wrapped[3] = wrapped[4] = MachineItem::op(0x52, "return");
    wrapped.back() = encoded.front();
    PostLayoutControlFlowOptions wrap_control;
    wrap_control.address_space_model = model;
    wrap_control.main_entry = last;
    const auto before = shared_post_layout_control_flow(wrapped, wrap_control);
    wrapped.front().opcode = 3;
    const auto after = shared_post_layout_control_flow(wrapped, wrap_control);
    require(before->proved && after->proved && before != after &&
            before->execution_states[1].address == 4 && after->execution_states[1].address == 3,
            "a wrapped fetch must not alias different address bytes in either memory profile");
  }
  auto changed_mode = items;
  changed_mode[2].stop_disposition = StopDisposition::Unknown;
  require(!shared_post_layout_control_flow(changed_mode)->proved,
          "removing a typed stop disposition cannot use an unrelated CFG");
  auto changed_operand = items;
  changed_operand[1].target = std::string("missing");
  require(!shared_post_layout_control_flow(changed_operand)->proved,
          "an unresolved call operand cannot use a successful CFG");

  auto oversized = items;
  const auto ordinary_item = static_cast<std::size_t>(digit - changed_opcode.begin());
  for (int i = 0; i < 120; ++i) oversized.push_back(MachineItem::op(0x54, "nop"));
  const auto linear = shared_post_layout_control_flow(oversized);
  oversized[ordinary_item].opcode = 0x6e;
  require(linear->proved && shared_post_layout_control_flow(oversized) == linear,
          "non-control one-cell operations share the logical linear CFG");
  {
    CacheMode disabled("0");
    require(same_facts(*linear, build_post_layout_control_flow(oversized)),
            "linear opcode projection preserves every control fact");
  }

  auto terminal = MachineItem::op(0x50, "stop");
  terminal.stop_disposition = StopDisposition::Terminal;
  std::vector selector_items{MachineItem::op(1, "digit"), MachineItem::op(0x47, "store"),
      MachineItem::op(0x87, "jump"), MachineItem::label("a"), terminal,
      MachineItem::label("b"), terminal};
  selector_items[2].indirect_flow_targets = std::vector<IrTarget>{std::string("a"), std::string("b")};
  const auto cfg = shared_post_layout_control_flow(selector_items);
  require(cfg->proved, "synthetic selector CFG has authoritative alternatives");
  const std::map<std::string, int> bindings{{"a", 1}, {"b", 2}};
  const auto before = detail::post_layout_proof_cache_stats(*cache);
  std::vector<std::shared_ptr<const SelectorConsistentExecutionGraph>> selections(4);
  detail::evaluate_layout_jobs(selections.size(), [&](std::size_t index) {
    selections[index] = shared_deferred_selector_consistent_execution_graph(
        selector_items, cfg, bindings);
  }, 4);
  stats = detail::post_layout_proof_cache_stats(*cache);
  require(selections.front()->proved &&
          std::all_of(selections.begin(), selections.end(), [&](const auto& graph) {
            return graph == selections.front();
          }) && stats.selector_builds == before.selector_builds + 1 &&
              stats.selector_hits == before.selector_hits + 3,
          "one fully checked selector product graph is published to all workers");
  auto unrelated = bindings;
  unrelated.emplace("not-a-selector-target", -1);
  require(shared_deferred_selector_consistent_execution_graph(selector_items, cfg, unrelated) ==
              selections.front(),
          "unused layout bindings do not create another selector proof");
  auto annotations = selector_items;
  annotations.front().source_line = 42;
  annotations.front().comment = "changed display annotation";
  require(shared_deferred_selector_consistent_execution_graph(annotations, cfg, bindings) ==
              selections.front(),
          "annotation changes do not introduce a different selector machine");
  {
    auto changed = selector_items;
    changed.front().opcode = 0x60;
    const auto recall = shared_deferred_selector_consistent_execution_graph(changed, cfg, bindings);
    changed.front().opcode = 0x10;
    require(recall->proved &&
            shared_deferred_selector_consistent_execution_graph(changed, cfg, bindings) == recall,
            "identical selector entry and transfer effects reuse the checked product graph");
    changed.front().opcode = 2;
    const auto changed_digit = shared_deferred_selector_consistent_execution_graph(changed, cfg, bindings);
    require(changed_digit->proved && changed_digit != recall && changed_digit != selections.front(),
            "selector keys must retain exact digits");
    CacheMode disabled("0");
    changed.front().opcode = 0x10;
    require(*recall == build_deferred_selector_consistent_execution_graph(changed, *cfg, bindings).execution,
            "selector opcode projection matches uncached proof");
  }
  const auto projection = shared_post_layout_projected_flow(cfg, selections.front());
  const PostLayoutExecutionView view{*cfg, selections.front().get()};
  require(projection && shared_post_layout_projected_flow(cfg, selections.front()) == projection &&
              view.size() == projection->execution_states.size() &&
              view.successors() == projection->execution_successors &&
              view.edges() == projection->execution_edges,
          "consumers share the exact selected flow rather than copying it again");
  const auto trivial = shared_selector_consistent_execution_graph(items, graphs.front());
  require(trivial->proved && trivial->identity_projection && trivial->original_states.empty() &&
              trivial->successors.empty() && trivial->execution_edges.empty() &&
              shared_post_layout_projected_flow(graphs.front(), trivial) == graphs.front(),
          "an identity proof borrows the original flow and never allocates rows");
  const auto value = build_selector_consistent_execution_graph(items, *graphs.front());
  require(value.original_states.size() == graphs.front()->execution_states.size() &&
              value.successors == graphs.front()->execution_successors &&
              value.execution_edges == graphs.front()->execution_edges,
          "the value API still materializes all rows in original order");
  auto removed = bindings;
  removed.erase("b");
  require(!shared_deferred_selector_consistent_execution_graph(selector_items, cfg, removed)->proved,
          "missing a used selector binding still fails closed");
  const std::map<std::string, int> swapped{{"a", 2}, {"b", 1}};
  const auto other = shared_deferred_selector_consistent_execution_graph(selector_items, *cfg, swapped);
  require(other->proved && other != selections.front(),
          "different selector equations do not share product graphs");
  auto modified_cfg = *cfg;
  modified_cfg.execution_edges[2].clear();
  require(!shared_deferred_selector_consistent_execution_graph(
              selector_items, modified_cfg, bindings)->proved,
          "a caller's changed labelled edges cannot reuse a valid product graph");
  require(!shared_deferred_selector_consistent_execution_graph(
              selector_items, *cfg, bindings, 1)->proved,
          "a cached selector result cannot bypass the state cap");
  {
    CacheMode disabled("0");
    require(*selections.front() == build_deferred_selector_consistent_execution_graph(
                selector_items, *cfg, bindings).execution &&
            *other == build_deferred_selector_consistent_execution_graph(
                selector_items, *cfg, swapped).execution,
            "cached and uncached selector effects and labelled edges match exactly");
  }
}
}

int main() {
  try {
    compact_row_contracts();
    snapshot_contracts(AddressSpaceModel::Standard);
    snapshot_contracts(AddressSpaceModel::Mk61SMiniExpanded);
    compilation_scope_contracts();
    std::cout << "shared CFG snapshots and exact return-stack contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
