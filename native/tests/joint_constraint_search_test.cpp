
#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/joint_proof_input.hpp"
#include "mkpro/core/joint_constraint_origins.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/passes/indirect_selector_seed_reuse.hpp"
#include "mkpro/core/passes/semantic_region_folding.hpp"
#include "mkpro/core/passes/register_coalesce.hpp"

#include "mkpro/core/passes/tail_branch_inversion.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;

void emit(std::vector<MachineItem>& items, std::initializer_list<int> code) {
  for (const int opcode : code) items.push_back(MachineItem::op(opcode, "constraint fixture"));
}

int cells(const LayoutSearchArtifact& artifact) {
  return static_cast<int>(std::count_if(artifact.items.begin(), artifact.items.end(),
      [](const auto& item) { return item.kind != MachineItemKind::Label; }));
}

LayoutSearchArtifact fixture(bool expose_selector = false, bool different_effect = false) {
  LayoutSearchArtifact result;
  auto& items = result.items;
  emit(items, {0x4b});
  for (int mode = 1; mode <= 8; ++mode) {
    emit(items, {mode, 0, 0x53});
    items.push_back(MachineItem::address(std::string{mode % 2 ? "red" : "blue"}));
  }
  emit(items, {0xa7});
  items.back().indirect_flow_targets = std::vector<IrTarget>{std::string{"sink"}};
  if (expose_selector) emit(items, {0x67, 0x43});
  // End both private scratch epochs before observing the final hardware bank.
  emit(items, {0x6b, 0x6c, 0x6d, 0x6e, 0x6b, 0x10, 0x44, 0x45, 0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
  for (const std::string name : {"red", "blue"}) {
    const int reg = name == "red" ? 4 : 5;
    items.push_back(MachineItem::label(name));
    emit(items, {0x48, 0x68, 1, 0, 0x12, 0x40 + reg,
                 0x68, 1, 0, 0, 0x12, 0x60 + reg,
                 different_effect && name == "blue" ? 0x11 : 0x10, 0x4a, 0x52});
  }
  const int address = cells(result);
  items.push_back(MachineItem::label("sink"));
  emit(items, {0x6a, 0x49, 0x52});
  result.preloads.push_back({.register_name = "7", .value = std::to_string(address)});
  return result;
}

std::vector<std::string> observe(const LayoutSearchArtifact& artifact) {
  const auto resolved = resolve_machine_items(artifact.items);
  require(resolved.diagnostics.empty(), "joint constraint fixture must resolve");
  std::vector<int> code;
  for (const auto& step : resolved.steps) code.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(code).diagnostics.empty(), "joint fixture must fit stock memory");
  for (const char* reg : {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
                          "a", "b", "c", "d", "e"})
    calc.set_register(reg, "7");
  for (const auto& preload : artifact.preloads)
    calc.set_register(preload.register_name, preload.value);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(16000, 8).stopped, "joint fixture must terminate");
  std::vector<std::string> result{calc.display_text(true)};
  for (const char* reg : {"X", "Y", "Z", "T", "X1", "3", "9", "a"})
    result.push_back(calc.read_register(reg));
  calc.press(".");
  result.push_back(calc.display_text(true));
  return result;
}

bool report(const LayoutSearchArtifact& artifact, const std::string& name) {
  return std::any_of(artifact.optimizations.begin(), artifact.optimizations.end(),
      [&](const auto& item) { return item.name == name; });
}


LayoutSearchArtifact allocation_fixture(bool observe_carriers = true) {
  LayoutSearchArtifact f;
  auto& items = f.items;
  for (const auto& [reg, target] :
       std::vector<std::pair<int, std::string>>{{7, "first"}, {8, "second"}}) {
    emit(items, {0xa0 + reg});
    items.back().indirect_flow_targets = std::vector<IrTarget>{target};
  }
  for (int call = 0; call < 3; ++call) {
    emit(items, {0x53});
    items.push_back(MachineItem::address(std::string{"third"}));
  }
  emit(items, {0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
  int reg = 0;
  for (const char* label : {"first", "second", "third"}) {
    items.push_back(MachineItem::label(label));
    // Both carrier words feed the final sum. Merely comparing an unused
    // internal address register would incorrectly forbid valid rebinding.
    const int data_opcode = reg == 0 ? 0x67 : reg == 1 ? 0x68 : 0x10;
    emit(items, {observe_carriers ? data_opcode : 0x60 + reg, 0x52});
    ++reg;
  }
  f.preloads = {{.register_name = "7", .value = "77770009"},
                {.register_name = "8", .value = "88880011"}};
  return f;
}

std::vector<std::string> allocation_observe(const LayoutSearchArtifact& f, int seed) {
  const auto resolved = resolve_machine_items(f.items);
  require(resolved.diagnostics.empty(), "allocation fixture resolves");
  std::vector<int> code;
  for (const auto& step : resolved.steps) code.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(code).diagnostics.empty(), "allocation fixture fits stock memory");
  for (int reg = 0; reg < 15; ++reg)
    calc.set_register(std::string(1, "0123456789abcde"[reg]), std::to_string(seed + reg));
  for (const char* reg : {"X", "Y", "Z", "T", "X1"}) calc.set_register(reg, "-12.75");
  for (const auto& preload : f.preloads) calc.set_register(preload.register_name, preload.value);
  calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  require(calc.run_until_stable(6000, 8).stopped, "allocated callers return and stop");
  std::vector<std::string> result;
  for (const char* reg : {"X", "Y", "Z", "T", "X1"}) result.push_back(calc.read_register(reg));
  for (int reg = 0; reg < 15; ++reg)
    result.push_back(calc.read_register(std::string(1, "0123456789abcde"[reg])));
  calc.press(".");
  result.push_back(calc.display_text(true));
  return result;
}


void prepared_joint_proof_input_contracts() {
  std::vector<MachineItem> items{
      MachineItem::op(0x52,"empty return"),MachineItem::op(0xa7,"stable call"),
      MachineItem::op(0x50,"terminal"),MachineItem::label("worker"),
      MachineItem::op(0x62,"value"),MachineItem::op(0x52,"return")};
  items[1].indirect_flow_targets = std::vector<IrTarget>{std::string{"worker"}};
  items[2].stop_disposition = StopDisposition::Terminal;
  std::vector<PreloadReport> environment{
      {.register_name="7",.value="3"},{.register_name="2",.value="11"}};
  const auto original_items = items;
  const auto original_environment = environment;
  PostLayoutControlFlowOptions options;
  options.empty_return_target = 1;
  detail::JointProofPreparationCounts counts;
  detail::PreparedJointProofInput prepared(items,environment,options,&counts);
  require(prepared.flow().proved && counts.inputs == 1 &&
              &prepared.flow() == &prepared.flow(),
          "alternative placements share one authoritative source/return graph");
  options.maximum_return_depth = 0;
  items.front().raw = true;
  environment.back().value="-9";
  require(prepared.options().maximum_return_depth == 5 &&
              prepared.items().front().raw == false && prepared.preloads().back().value == "11",
          "entry, model, source metadata and environment are immutable owned snapshots");
  const auto expected = rebind_stable_preloaded_indirect_flow_selector(
      original_items,original_environment.front(),prepared.flow(),3,4,
      prepared.options().address_space_model,&original_environment);
  require(expected && prepared.rebind_selector(original_environment.front(),3,4) == expected &&
              prepared.rebind_selector(original_environment.front(),3,4) == expected &&
              counts.selector_proofs == 1 && counts.selector_reuses == 1,
          "both placements reuse a completed exact word/writeback certificate");
  auto selected = original_environment.front(); selected.value="4";
  require(!prepared.rebind_selector(selected,3,4) && prepared.selector_proofs() == 2,
          "selected word remains independent from the frozen environment and invalidates reuse");
  const auto rejected_proofs = prepared.selector_proofs();
  require(!prepared.rebind_selector(selected,3,4) &&
              prepared.selector_proofs() == rejected_proofs,
          "a complete deterministic rejection is reused but never becomes acceptance");
  auto setup = original_environment.front(); setup.setup_expression_text="generated setup";
  const auto setup_expected = rebind_stable_preloaded_indirect_flow_selector(
      original_items,setup,prepared.flow(),3,4,prepared.options().address_space_model,
      &original_environment);
  require(prepared.rebind_selector(setup,3,4) == setup_expected &&
              prepared.selector_proofs() == 3,
          "every setup and delivery obligation belongs to the selected-word key");
  prepared.rebind_selector(original_environment.front(),3,5);
  require(prepared.selector_proofs() == 4, "different physical target needs its own certificate");
  require(!prepared.rebind_selector(original_environment.front(),2,4) &&
              prepared.selector_proofs() == 5,
          "contradictory encoded origin never borrows a previous proof");

  PostLayoutControlFlowOptions normal;
  normal.empty_return_target = 1;
  detail::PreparedJointProofInput uncached(original_items,original_environment,normal,nullptr,0);
  require(uncached.rebind_selector(original_environment.front(),3,4) == expected &&
              uncached.rebind_selector(original_environment.front(),3,4) == expected &&
              uncached.retained() == 0 && uncached.selector_proofs() == 2,
          "zero retention preserves the independent proof contract");
  detail::PreparedJointProofInput bounded(original_items,original_environment,normal,nullptr,1);
  bounded.rebind_selector(original_environment.front(),3,4);
  bounded.rebind_selector(original_environment.front(),3,5);
  bounded.rebind_selector(original_environment.front(),3,5);
  require(bounded.retained() == 1 && bounded.selector_proofs() == 3,
          "local proof retention is bounded; an omitted certificate is recomputed");
  detail::PreparedJointProofInput strict(original_items,original_environment,{});
  require(!strict.flow().proved,
          "a different empty-return policy cannot inherit the hardware entry opt-in");
  detail::PreparedJointProofInput shallow(original_items,original_environment,options);
  require(!shallow.flow().proved,
          "a different return-depth/work policy owns an independent proof source");
  auto observed = original_items; observed[4].roles.push_back("observer:independent");
  detail::PreparedJointProofInput observable(observed,original_environment,normal);
  require(observable.items()[4].roles != prepared.items()[4].roles,
          "typed observer metadata is retained, not reduced to an opcode fingerprint");
}

void transaction_growth_budget_contracts() {
  require(transaction_growth_fits(100, 104, 4), "exact root-relative growth is allowed");
  require(transaction_growth_fits(103, 105, 4) &&
              !transaction_growth_fits(100, 105, 4),
          "a larger intermediate must not reset the root's growth budget");
  require(transaction_growth_fits(100, 99, 0), "a shrinking atomic allocation needs no credit");
  require(!transaction_growth_fits(100, 101, 0) &&
              !transaction_growth_fits(100, 99, -1) &&
              !transaction_growth_fits(-1, 0, 4) &&
              !transaction_growth_fits(100, -1, 4),
          "zero growth and invalid counts fail closed");
  const int maximum = std::numeric_limits<int>::max();
  require(transaction_growth_fits(maximum - 1, maximum, 1) &&
              !transaction_growth_fits(maximum - 2, maximum, 1),
          "budget arithmetic cannot overflow near the integer limit");

  TransactionSearchLimits limits;
  limits.maximum_growth_cells = 4;
  std::vector<int> finalized;
  using Edge = ProvedSearchTransition<int>;
  const auto search = search_proved_transactions(
      std::vector<Edge>{{103, "first-stage", true}}, std::vector<int>{100}, 100,
      limits, [](int size) { return size; }, [](int a, int b) { return a == b; },
      [](int size) {
        if (size == 103) return std::vector<Edge>{{105, "over-budget", true},
                                                 {104, "within-budget", true}};
        if (size == 104) return std::vector<Edge>{{99, "repaid", true}};
        return std::vector<Edge>{};
      },
      [&](int size, const std::vector<std::string>&) -> std::optional<int> {
        finalized.push_back(size);
        return size;
      });
  require(search.incumbent == 99 && search.rejected > 0 &&
              std::find(finalized.begin(), finalized.end(), 105) == finalized.end(),
          "unpaid chained growth must be rejected before finalization");
}

void product_frontier_contracts() {
  struct Node {
    int parent;
    int alternative;
    int size;
    bool operator==(const Node&) const = default;
  };
  using Edge = ProvedSearchTransition<Node>;
  const std::vector<Node> parents{{0, -1, 100}, {1, -1, 101},
                                  {2, -1, 100}, {3, -1, 101}};
  const auto original = parents;
  const std::vector<Node> established{{0, 0, 100}, {0, 1, 100},
                                      {1, 0, 101}, {1, 1, 101}};
  TransactionSearchLimits limits{
      .maximum_expansions = 4, .maximum_candidates = 4,
      .maximum_depth = 1, .maximum_growth_cells = 4};
  const auto size = [](const Node& node) { return node.size; };
  const auto equal = [](const Node& a, const Node& b) { return a == b; };
  std::size_t expanded = 0;
  const auto expand = [&](const Node& parent) {
    ++expanded;
    const Node first{parent.parent, 0, parent.size};
    return std::vector<Edge>{
        {parent, "cycle", true},
        {{parent.parent, 7, 100}, "unproved", false},
        {{parent.parent, 8, 105}, "unpaid-growth", true},
        {first, "next-axis", true}, {first, "duplicate", true},
        {{parent.parent, 1, parent.parent == 3 ? 104 : parent.size}, "next-axis", true}};
  };
  const auto collect = [&](const TransactionSearchLimits& budget) {
    return collect_proved_product_frontier(
        parents, established, 100, budget, size, equal, expand);
  };
  const auto product = collect(limits);
  const std::vector<Node> expected{{2, 0, 100}, {3, 0, 101},
                                  {2, 1, 100}, {3, 1, 104}};
  require(product.candidates.size() == expected.size() && expanded == 4 &&
              product.expanded == 4 && product.rejected == 8 && product.duplicate > 0,
          "product admission rejects unproved/growing/duplicate edges without spending slots");
  for (std::size_t i = 0; i < expected.size(); ++i)
    require(product.candidates[i].node == expected[i] && product.candidates[i].proved,
            "later parent representations receive deterministic round-robin slots");
  require(!product.budget_exhausted && parents == original,
          "complete product admission leaves all parent artifacts immutable");
  const auto repeated = collect(limits);
  for (std::size_t i = 0; i < expected.size(); ++i)
    require(repeated.candidates[i].node == expected[i], "product order is repeatable");

  const auto no_children = [](const Node&) { return std::vector<Edge>{}; };
  const auto closed = search_proved_transactions(
      product.candidates, parents, 100, limits, size, equal, no_children,
      [](const Node& node, const std::vector<std::string>&) -> std::optional<Node> {
        if (node.parent == 3 && node.alternative == 1) return Node{3, 2, 97};
        return std::nullopt;
      });
  require(closed.incumbent && closed.incumbent->size == 97,
          "a larger late product can win only after the independent atomic closure");
  const auto rejected = search_proved_transactions(
      product.candidates, parents, 100, limits, size, equal, no_children,
      [](const Node&, const std::vector<std::string>&) -> std::optional<Node> {
        return std::nullopt;
      });
  require(!rejected.incumbent, "product admission is not a final-artifact proof");

  auto small = limits;
  small.maximum_candidates = 3;
  const auto cut = collect(small);
  require(cut.candidates.size() == 3 && cut.budget_exhausted &&
              std::none_of(cut.candidates.begin(), cut.candidates.end(),
                           [&](const auto& edge) { return edge.node == expected.back(); }),
          "candidate work cap is explicit even when it cuts the winning combination");
  small = limits;
  small.maximum_expansions = 2;
  const auto parent_cut = collect(small);
  require(parent_cut.candidates.empty() && parent_cut.expanded == 2 &&
              parent_cut.budget_exhausted,
          "the product cannot expand parents beyond the caller's work budget");
  for (int disabled = 0; disabled < 3; ++disabled) {
    auto none = limits;
    if (disabled == 0) none.maximum_expansions = 0;
    if (disabled == 1) none.maximum_candidates = 0;
    if (disabled == 2) none.maximum_depth = 0;
    const auto before = expanded;
    require(collect(none).candidates.empty() && expanded == before,
            "every disabled product budget avoids expansion work");
  }
  const auto crowded = collect_proved_product_frontier(
      std::vector<Node>{{0, -1, 100}, {1, -1, 100}, {2, -1, 100}},
      std::vector<Node>{}, 100, small = {
          .maximum_expansions = 3, .maximum_candidates = 3,
          .maximum_depth = 1, .maximum_growth_cells = 4}, size, equal,
      [](const Node& parent) {
        std::vector<Edge> edges;
        for (int alternative = 0; alternative < (parent.parent == 0 ? 12 : 1); ++alternative)
          edges.push_back({{parent.parent, alternative, 100}, "axis", true});
        return edges;
      });
  require(crowded.candidates.size() == 3 && crowded.budget_exhausted &&
              crowded.candidates[0].node.parent == 0 &&
              crowded.candidates[1].node.parent == 1 &&
              crowded.candidates[2].node.parent == 2,
          "one prolific parent cannot consume every remaining product slot");
}

void selector_allocation_contracts() {
  const auto f = allocation_fixture();
  const std::map<std::string, int> origins{{"7", 9}, {"8", 11}};
  const auto alternatives = [&](const LayoutSearchArtifact& value,
                                const std::map<std::string, int>& encoded,
                                std::size_t budget = 128) {
    return deferred_natural_target_selector_allocations(
        value.items, value.preloads, build_post_layout_control_flow(value.items), encoded,
        {}, {.maximum_assignments = budget});
  };
  const auto candidates = alternatives(f, origins);
  require(!candidates.empty(), "whole-family allocation must produce semantic alternatives");
  require(std::any_of(candidates.begin(), candidates.end(), [](const auto& candidate) {
            return candidate.cell_delta == -2;
          }), "release one call and shorten three calls: 15 -> 13 cells");
  const auto repeated_candidates = alternatives(f, origins);
  require(candidates.size() == repeated_candidates.size(), "allocation cardinality is deterministic");
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    require(machine_items_to_json(candidates[i].items) ==
                machine_items_to_json(repeated_candidates[i].items) &&
            candidates[i].encoded_selector_targets ==
                repeated_candidates[i].encoded_selector_targets,
            "allocation order and carrier origins are deterministic");
    require(build_post_layout_control_flow(candidates[i].items).proved,
            "deferred candidate preserves logical control and matched return contexts");
  }

  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  std::size_t closures = 0;
  const auto result = search_joint_representation_constraints(f, options,
      [&](const LayoutSearchArtifact& closed) -> std::optional<LayoutSearchArtifact> {
        if (report(closed, "joint-selector-family-allocation")) {
          ++closures;
          require(cells(closed) < cells(f), "unpaid allocation must not run the downstream finalizer");
          for (int seed : {-9, 0, 3})
            require(allocation_observe(closed, seed) == allocation_observe(f, seed),
                    "closed allocation preserves every register, stack/X1 and exposed X2");
        }
        return closed;
      });
  require(result.incumbent && closures > 0 && cells(*result.incumbent) == 13,
          "allocator and layout must jointly close the 15 -> 13 baseline");
  require(report(*result.incumbent, "joint-selector-family-allocation") &&
              report(*result.incumbent, "joint-selector-allocation-layout"),
          "explain records the combined allocation/placement proof");
  require(same_layout_search_artifact(f, allocation_fixture()), "allocation leaves input untouched");
  const auto again = search_joint_representation_constraints(f, options);
  require(again.incumbent && same_layout_search_artifact(*result.incumbent, *again.incumbent),
          "closed allocation is deterministic");

  auto renamed = f;
  const std::map<std::string, std::string> rename{
      {"first", "unrelated_z"}, {"second", "unrelated_a"}, {"third", "unrelated_m"}};
  for (auto& item : renamed.items) {
    if (item.kind == MachineItemKind::Label) item.name = rename.at(item.name);
    if (item.kind == MachineItemKind::Address)
      item.target = rename.at(std::get<std::string>(item.target));
    if (item.indirect_flow_targets)
      for (auto& target : *item.indirect_flow_targets)
        target = rename.at(std::get<std::string>(target));
  }
  const auto anonymous = search_joint_representation_constraints(renamed, options);
  require(anonymous.incumbent && cells(*anonymous.incumbent) == 13 &&
              allocation_observe(*anonymous.incumbent, 4) == allocation_observe(f, 4),
          "neither program nor target names participate in discovery");

  require(alternatives(f, origins, 0).empty(), "zero assignment budget fails closed");
  require(alternatives(f, {}).empty(), "existing families require encoded origins");
  require(alternatives(f, {{"7", 10}, {"8", 11}}).empty(),
          "a forged carrier origin is not a word certificate");
  auto invalid = f;
  invalid.preloads.push_back(invalid.preloads.front());
  require(alternatives(invalid, origins).empty(), "duplicate preload delivery is ambiguous");
  invalid = f;
  invalid.preloads.front().setup_expression_text = "generated";
  require(alternatives(invalid, origins).empty(), "generated setup cannot become a literal carrier");
  invalid = f;
  invalid.items[0].raw = true;
  require(alternatives(invalid, origins).empty(), "opaque effects cannot be reallocated");
  invalid = f;
  invalid.items[10].opcode = 0x47;
  require(alternatives(invalid, origins).empty(), "runtime-written families are not stable");
  invalid = f;
  invalid.items[0].indirect_flow_targets =
      std::vector<IrTarget>{std::string{"first"}, std::string{"second"}};
  require(alternatives(invalid, origins).empty(), "partial multi-target families cannot be reassigned");
  invalid = f;
  invalid.items[10].opcode = 0xb0;
  invalid.items[10].indirect_memory_targets = std::vector<int>{7, 8};
  require(alternatives(invalid, origins).empty(), "indirect writes aliasing carriers forbid allocation");

  // A terminal sink cannot expose X2. Insert a real observer to distinguish
  // that valid conversion from the same branch with a live hidden value.
  for (bool observes_x2 : {false, true}) {
    auto conditional = allocation_fixture(false);
    conditional.items[6].opcode = 0x59;
    conditional.items[6].roles.push_back("test-observed-conditional");
    conditional.items[7].target = std::string{"finish"};
    conditional.items.insert(conditional.items.begin() + 8, MachineItem::label("finish"));
    conditional.items.insert(conditional.items.begin() + 6,
        {MachineItem::op(0x60, "input"), MachineItem::op(0x22, "square preserves X2")});
    if (observes_x2)
      conditional.items.insert(conditional.items.begin() + 11,
          {MachineItem::op(0x0c, "restore X2"), MachineItem::op(2, "exponent")});
    const int first = observes_x2 ? 13 : 11;
    const int second = first + 2;
    conditional.preloads[0].value = "777700" + std::to_string(first);
    conditional.preloads[1].value = "888800" + std::to_string(second);
    const auto guarded = alternatives(conditional, {{"7", first}, {"8", second}});
    require(!guarded.empty(), "unrelated whole-family alternatives remain available");
    bool converted = false;
    for (const auto& candidate : guarded)
      for (const auto& item : candidate.items)
        if (std::find(item.roles.begin(), item.roles.end(),
                      "test-observed-conditional") != item.roles.end()) {
          converted = converted || item.opcode != 0x59;
          if (observes_x2)
            require(item.opcode == 0x59, "observable conditional X2 prevents shortening");
        }
    if (!observes_x2) {
      require(converted, "a terminal stop must not invent a hidden-X2 observation");
      continue;
    }

    // Deliberately bypass the proof to make the negative test's observation
    // concrete on ROM. Retarget both existing call families after removing
    // the conditional operand, so no unrelated address mismatch explains it.
    auto unsafe = conditional;
    unsafe.items[8].opcode = 0x9b;
    unsafe.items[8].indirect_flow_targets = std::vector<IrTarget>{std::string{"finish"}};
    unsafe.items.erase(unsafe.items.begin() + 9);
    unsafe.preloads[0].value = "777700" + std::to_string(first - 1);
    unsafe.preloads[1].value = "888800" + std::to_string(second - 1);
    unsafe.preloads.push_back({.register_name = "b", .value = "9"});
    require(allocation_observe(unsafe, -9).front() !=
                allocation_observe(conditional, -9).front(),
            "ROM confirms that the rejected conversion changes the displayed result");
  }
  TransactionSearchLimits none;
  none.maximum_expansions = 0;
  require(!search_joint_representation_constraints(f, options, {}, none).incumbent,
          "the new lane obeys the caller's disabled-search budget");
  TransactionSearchLimits no_growth;
  no_growth.maximum_growth_cells = 0;
  const auto shrinking = search_joint_representation_constraints(f, options, {}, no_growth);
  require(shrinking.incumbent && cells(*shrinking.incumbent) == 13,
          "a profitable whole-family transaction does not need a growth allowance");
  std::cout << "selector-family allocation size baseline: 15 -> 13 cells\n";
}


LayoutSearchArtifact deferred_seed_fixture(int counter, int selector) {
  LayoutSearchArtifact f;
  auto& items = f.items;
  emit(items, {0x68, 0x69, 0x38, 0x4a, 1, 0x4e, 8, 0x40 + counter});
  const auto charge = [&](const std::string& target) {
    for (const auto part : {LateBoundDecimalSelectorPart::High,
                            LateBoundDecimalSelectorPart::Low}) {
      emit(items, {0});
      items.back().mnemonic = "0";
      items.back().roles.push_back(make_late_bound_decimal_selector_role(part, target));
      items.back().roles.push_back(
          "late-decimal-selector-register:" + std::string(1, "0123456789abcde"[selector]));
    }
    emit(items, {0x40 + selector, 0x53});
    items.push_back(MachineItem::address("dispatch"));
  };
  charge("advance");
  charge("inspect");
  emit(items, {0x60 + counter, 0x44, 0x65, 0x66, 0x6e, 0x65, 0x66, 0x10, 0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
  items.push_back(MachineItem::label("dispatch"));
  emit(items, {0x80 + selector});
  items.back().indirect_flow_targets =
      std::vector<IrTarget>{std::string("advance"), std::string("inspect")};
  items.back().roles.push_back("late-decimal-selector-consumer");
  items.push_back(MachineItem::label("advance"));
  emit(items, {0x68, 0x69, 0x68, 0x69, 0xd0 + counter});
  items.back().indirect_memory_targets = std::vector<int>{7};
  emit(items, {0x52});
  items.push_back(MachineItem::label("inspect"));
  emit(items, {0x60 + counter, 0x44, 0x52});
  const auto bound = rebind_layout_search_artifact(std::move(f));
  require(bound.has_value(), "callback fixture binds both runtime charges");
  return *bound;
}

std::map<std::string, int> seed_origins(const LayoutSearchArtifact& f) {
  std::map<std::string, int> result;
  int address = 0;
  for (const auto& item : f.items)
    if (item.kind == MachineItemKind::Label)
      result.emplace(item.name, address);
    else
      ++address;
  return result;
}

std::vector<std::string> deferred_seed_observe(
    const LayoutSearchArtifact& f, int selector, const std::string& packed) {
  const auto resolved = resolve_machine_items(f.items);
  require(resolved.diagnostics.empty(), "deferred seed final addresses resolve");
  std::vector<int> code;
  for (const auto& step : resolved.steps) code.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(code).diagnostics.empty(), "deferred seed fits stock memory");
  for (int r = 0; r < 15; ++r)
    calc.set_register(std::string(1, "0123456789abcde"[r]), std::to_string(11 + r));
  calc.set_register("8", packed);
  calc.set_register("9", "88888888");
  calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  require(calc.run_until_stable(4000, 8).stopped, "nested selected callbacks return and stop");
  std::vector<std::string> result;
  for (const char* reg : {"X", "Y", "Z", "T", "X1"}) result.push_back(calc.read_register(reg));
  for (int r = 0; r < 15; ++r)
    if (r != selector)
      result.push_back(calc.read_register(std::string(1, "0123456789abcde"[r])));
  require(static_cast<int>(std::stod(calc.read_register(
              std::string(1, "0123456789abcde"[selector])))) == seed_origins(f).at("inspect"),
          "final selector word must name the actual relocated callback");
  calc.press(".");
  result.push_back(calc.display_text(true));
  return result;
}

enum class DeferredModeObservation { None, LastX, EntryShadow };

LayoutSearchArtifact deferred_mode_fixture(
    int carrier, int selector,
    DeferredModeObservation observation = DeferredModeObservation::None) {
  LayoutSearchArtifact f;
  auto& items = f.items;
  const auto charge = [&](const std::string& target) {
    for (const auto part : {LateBoundDecimalSelectorPart::High,
                            LateBoundDecimalSelectorPart::Low}) {
      emit(items, {0});
      items.back().mnemonic = "0";
      items.back().roles.push_back(make_late_bound_decimal_selector_role(part, target));
      items.back().roles.push_back(
          "late-decimal-selector-register:" + std::string(1, "0123456789abcde"[selector]));
    }
    emit(items, {0x40 + selector, 0x53});
    items.push_back(MachineItem::address("dispatch"));
  };
  // The same physical register has a finite arithmetic epoch and a later
  // arbitrary-data epoch. Only exact callback selection separates their uses.
  emit(items, {0x6b, 0x4e, 2, 0x40 + carrier});
  charge("scale");
  emit(items, {0x66, 0x40 + carrier});
  charge("echo");
  emit(items, {0x6b, 0x6c, 0x6d, 0x6e, 0x6b, 0x10, 0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
  items.push_back(MachineItem::label("dispatch"));
  emit(items, {0xa0 + selector});
  items.back().indirect_flow_targets =
      std::vector<IrTarget>{std::string("scale"), std::string("echo")};
  items.back().roles.push_back("late-decimal-selector-consumer");
  emit(items, {0x52});
  items.push_back(MachineItem::label("scale"));
  for (const int destination : {9, 10})
    emit(items, {0x60 + carrier, 1, 0, 0x12, 0x40 + destination});
  if (observation == DeferredModeObservation::LastX)
    emit(items, {0x0f, 0x44});
  if (observation == DeferredModeObservation::EntryShadow)
    emit(items, {0x0c, 2, 0x44});
  emit(items, {0x52});
  items.push_back(MachineItem::label("echo"));
  emit(items, {0x60 + carrier, 0x45, 0x52});
  const auto bound = rebind_layout_search_artifact(std::move(f));
  require(bound.has_value(), "finite callback fixture binds both charges");
  return *bound;
}

LayoutSearchArtifact exchange_deferred_mode_regions(LayoutSearchArtifact f) {
  const auto scale = std::find_if(f.items.begin(), f.items.end(),
      [](const auto& item) { return item.kind == MachineItemKind::Label && item.name == "scale"; });
  const auto echo = std::find_if(f.items.begin(), f.items.end(),
      [](const auto& item) { return item.kind == MachineItemKind::Label && item.name == "echo"; });
  require(scale != f.items.end() && echo != f.items.end() && scale < echo,
          "deferred mode fixture has two independently closed callback regions");
  std::rotate(scale, echo, f.items.end());
  return f;
}

std::vector<std::string> deferred_mode_observe(
    const LayoutSearchArtifact& f, int selector, const std::string& input) {
  const auto resolved = resolve_machine_items(f.items);
  require(resolved.diagnostics.empty(), "finite callback addresses resolve");
  std::vector<int> code;
  for (const auto& step : resolved.steps) code.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(code).diagnostics.empty(), "finite callback fixture fits stock memory");
  for (int r = 0; r < 15; ++r)
    calc.set_register(std::string(1, "0123456789abcde"[r]), std::to_string(r + 11));
  calc.set_register("6", input);
  calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  require(calc.run_until_stable(4000, 8).stopped, "selected callbacks return and terminate");
  std::vector<std::string> observations{calc.display_text(true)};
  calc.press(".");
  observations.push_back(calc.display_text(true));
  for (const char* reg : {"X", "Y", "Z", "T", "X1"})
    observations.push_back(calc.read_register(reg));
  for (int r = 0; r < 15; ++r)
    if (r != selector)
      observations.push_back(calc.read_register(std::string(1, "0123456789abcde"[r])));
  require(static_cast<int>(std::stod(calc.read_register(
              std::string(1, "0123456789abcde"[selector])))) == seed_origins(f).at("echo"),
          "final runtime selector must name the actual callback, not its old position");
  return observations;
}













LayoutSearchArtifact epoch_recolor_fixture(bool expose_lifetime = false,
                                         bool expose_final_bank = false, bool flush_stack = true, bool shared_store = false) {
  LayoutSearchArtifact result;
  auto& items = result.items;
  const auto charge = [&](const std::string& target) {
    for (const auto part : {LateBoundDecimalSelectorPart::High,
                            LateBoundDecimalSelectorPart::Low}) {
      emit(items, {0});
      items.back().mnemonic="0";
      items.back().roles.push_back(make_late_bound_decimal_selector_role(part,target));
      items.back().roles.push_back("late-decimal-selector-register:b");
    }
    if (shared_store) emit(items,{0x53});
    else emit(items,{0x4b,0x53});
    items.push_back(MachineItem::address("dispatch"));
  };
  emit(items,{2,0x48});
  charge("scale");
  emit(items,{0x49,3,0x42});
  charge("echo");
  emit(items,{0x4a});
  if (expose_lifetime) emit(items,{0x68,0x4e});
  if (flush_stack) emit(items,{0x66,0x66,0x66,0x66});
  emit(items,{0,0x42});
  if (!expose_final_bank) emit(items,{0x48});
  emit(items,{0x50});
  items.back().stop_disposition=StopDisposition::Terminal;
  items.push_back(MachineItem::label("dispatch"));
  if (shared_store) emit(items,{0x4b});
  emit(items,{0xab});
  items.back().roles.push_back("late-decimal-selector-consumer");
  items.back().indirect_flow_targets=std::vector<IrTarget>{std::string{"scale"},std::string{"echo"}};
  emit(items,{0x52});
  for (const auto& [label,reg] : std::vector<std::pair<std::string,int>>{{"scale",8},{"echo",2}}) {
    items.push_back(MachineItem::label(label));
    if (label=="scale") emit(items,{0x66,0x4d});
    emit(items,{0x60+reg,1,0,0x12,0x52});
  }
  const auto bound=rebind_layout_search_artifact(std::move(result));
  require(bound.has_value(),"epoch recoloring fixture binds compiler-owned callback words");
  return *bound;
}

std::vector<passes::RegisterWebRecoloringRequest> epoch_recolor_request(
    const std::vector<IrOp>& ops) {
  std::optional<std::size_t> first,second;
  for (std::size_t i=0;i<ops.size();++i) {
    if (!first && ops[i].kind==IrKind::Store && ops[i].register_name=="8") first=i;
    if (!second && ops[i].kind==IrKind::Store && ops[i].register_name=="2") second=i;
  }
  require(first && second,"epoch fixture has two independent producer operands");
  return {{{*first,*second}}};
}

void mutable_selector_keeps_independent_charge_origins() {
  PostLayoutControlFlowOptions control; control.empty_return_target=1;
  for (const bool expose_first : {false,true}) {
    LayoutSearchArtifact original; auto& items=original.items;
    items.push_back(MachineItem::label("zero_return")); emit(items,{0x52});
    emit(items,{0x6c,0x0b,0x4c,0xac});
    items.back().indirect_flow_targets=std::vector<IrTarget>{std::string{"zero_return"},std::string{"opaque_return"}};
    const auto charge=[&](const std::string& target) {
      for (const auto part : {LateBoundDecimalSelectorPart::High,LateBoundDecimalSelectorPart::Low}) {
        emit(items,{0}); items.back().mnemonic="0"; items.back().roles.push_back(make_late_bound_decimal_selector_role(part,target));
        items.back().roles.push_back("late-decimal-selector-register:b");
      }
      emit(items,{0x4b,0xab}); items.back().roles.push_back("late-decimal-selector-consumer");
      items.back().indirect_flow_targets=std::vector<IrTarget>{std::string{"discard"},std::string{"observe"}};
    };
    charge("discard"); charge("observe");
    emit(items,{0x6a,0x50}); items.back().stop_disposition=StopDisposition::Terminal;
    // Moving these unreachable padding cells across the first callback changes
    // only its address. The second callback's observable word stays fixed.
    emit(items,{0x54,0x54,0x54});
    items.push_back(MachineItem::label("discard"));
    if (expose_first) emit(items,{0x49});
    emit(items,{0x67,0x67,0x67,0x67,0x52});
    items.push_back(MachineItem::label("observe")); emit(items,{0x4a,0x67,0x67,0x67,0x67,0x52});
    items.push_back(MachineItem::label("opaque_return")); emit(items,{0x52});
    original.preloads={{.register_name="c",.value="1E-1"},{.register_name="7",.value="13"}};
    const auto bound=rebind_layout_search_artifact(std::move(original));
    require(bound.has_value(),"mutable selector fixture has complete charge bindings"); original=*bound;
    const auto flow=build_post_layout_control_flow(original.items,control);
    require(flow.proved,"mutable selector and both exact callback families have a rooted CFG");
    detail::JointOriginMap origins; int address=0;
    for (const auto& item : original.items) {
      if (item.kind==MachineItemKind::Label) origins[item.name]=address;
      else ++address;
    }
    const auto selected=build_deferred_selector_consistent_execution_graph(original.items,flow,origins).execution;
    const auto opaque=std::find_if(original.items.begin(),original.items.end(),[](const auto& item){
      return item.kind==MachineItemKind::Label && item.name=="opaque_return";
    });
    const auto opaque_item=static_cast<std::size_t>(opaque-original.items.begin())+1U;
    require(selected.proved && std::any_of(selected.original_states.begin(),selected.original_states.end(),
        [&](const auto state){return flow.execution_states[state].item_index==opaque_item;}),
        "an unknown written selector retains its second declared callback; stale preload text cannot prune it");
    const auto pinned=detail::protected_joint_runtime_charge_targets(original.items,&flow,&original.preloads,true);
    require(pinned && pinned->contains("observe") && pinned->contains("discard")==expose_first,
        "a written independent selector never turns its stale Main word into an origin or disables exact callback privacy");
    if(expose_first)continue;
    auto moved=original;
    const auto first=std::find_if(moved.items.begin(),moved.items.end(),[](const auto& item){
      return item.kind==MachineItemKind::Label && item.name=="discard";});
    const auto second=std::find_if(moved.items.begin(),moved.items.end(),[](const auto& item){
      return item.kind==MachineItemKind::Label && item.name=="observe";});
    require(first!=moved.items.end() && second>first && first-moved.items.begin()>=3,
        "callback and skipped padding identities exist");
    const auto low=static_cast<std::size_t>(first-moved.items.begin());
    const auto high=static_cast<std::size_t>(second-moved.items.begin());
    std::rotate(moved.items.begin()+static_cast<std::ptrdiff_t>(low-3U),
                moved.items.begin()+static_cast<std::ptrdiff_t>(low),
                moved.items.begin()+static_cast<std::ptrdiff_t>(high));
    const auto relocated=rebind_layout_search_artifact(std::move(moved));
    require(relocated.has_value(),"separately movable private charge is rebound by identity");
    const auto play=[&](const LayoutSearchArtifact& artifact) {
      const auto image=resolve_machine_items(artifact.items); require(image.diagnostics.empty(),"callback fixture resolves");
      std::vector<int> bytes;for(const auto& step:image.steps)bytes.push_back(step.opcode);
      emulator::MK61 calc;require(calc.load_program(bytes).diagnostics.empty(),"callback fixture fits stock ROM");
      for(const auto& word:artifact.preloads)calc.set_register(word.register_name,word.value);
      calc.set_register("X","17");calc.set_register("Y","23");calc.set_register("Z","31");calc.set_register("T","43");
      calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});
      require(calc.run_until_stable(5000,8).stopped,"both callbacks return to the original terminal observation");
      std::vector<std::string> values;
      for(const char* reg:{"X","Y","Z","T","X1","a","b","c"})values.push_back(calc.read_register(reg));
      calc.press(".");values.push_back(calc.display_text(true));return values;
    };
    require(play(original)==play(*relocated),
        "ROM preserves observable callback data, every later register read, stack, last-X and entry shadow after moving only the private leaf");
    const auto altered_flow=build_post_layout_control_flow(relocated->items,control);
    const auto altered=detail::protected_joint_runtime_charge_targets(relocated->items,&altered_flow,&relocated->preloads,true);
    require(altered && !altered->contains("discard") && altered->contains("observe"),
        "the independent privacy proof survives the final callback relocation");
  }
}

void deferred_web_recoloring_contracts() {
  CompileOptions options;
  options.coalesce_copies=true;
  options.defer_return_suffix_until_callee_hole=true;
  PostLayoutControlFlowOptions control;
  control.empty_return_target=1;
  const auto leaky=epoch_recolor_fixture(false,false,false);
  const auto leaky_result=search_joint_representation_constraints(leaky,options);
  if (leaky_result.incumbent)
    require(deferred_mode_observe(leaky,11,"0")==
                deferred_mode_observe(*leaky_result.incumbent,11,"0"),
            "publication cannot expose a retuned runtime address through Z");
  const auto leaky_ops=raise_machine_to_ir(leaky.items);
  std::vector<std::vector<IrOp>> leaky_allocations;
  passes::for_each_deferred_register_web_recoloring(leaky_ops,{options},
      passes::semantic_region_tail_register_requests(leaky_ops,true),control,seed_origins(leaky),
      [&](std::vector<IrOp> candidate){leaky_allocations.push_back(std::move(candidate));});
  require(leaky_allocations.size()==1,"the negative differs only in stack observability");
  const auto leaky_fold=passes::semantic_region_folding_with_deferred_layout(
      leaky_allocations.front(),{options},control);
  require(leaky_fold.applied>0,"conditional fold carries an undischarged literal privacy obligation");
  const auto guarded=detail::protected_joint_runtime_charge_targets(lower_ir_to_machine(leaky_fold.ops));
  require(guarded && guarded->contains("echo"),
          "the new folded-entry obligation pins a runtime address residue in Z");

  auto selective=epoch_recolor_fixture(false,false,true,true);
  auto echo=std::find_if(selective.items.begin(),selective.items.end(),[](const auto& item){
    return item.kind==MachineItemKind::Label && item.name=="echo";
  });
  require(echo!=selective.items.end(),"selective stack fixture has the second callback");
  selective.items.erase(echo+1,selective.items.end());
  emit(selective.items,{0x22,0x52});
  const auto selective_bound=rebind_layout_search_artifact(std::move(selective));
  require(selective_bound.has_value(),"selective stack fixture binds both source words");
  const auto selective_flow=build_post_layout_control_flow(selective_bound->items,control);

  const StackValueEqualityState equal{{true,true,true,true},true,true};
  require(prove_post_layout_stack_state_equality(selective_bound->items,selective_flow,{0},equal),
          "an explicitly admitted equal root keeps the legacy proof valid");
  require(!prove_post_layout_stack_state_equality(selective_bound->items,selective_flow,{},equal) &&
              !prove_post_layout_stack_state_equality(selective_bound->items,selective_flow,{0,0},equal) &&
              !prove_post_layout_stack_state_equality(selective_bound->items,selective_flow,
                  {selective_flow.execution_states.size()},equal),
          "empty, duplicate and out-of-range context roots fail closed");

  const auto charges=rebind_late_bound_decimal_selectors(
      selective_bound->items,{.minimum_target_address=0,.maximum_target_address=99});
  const auto first_charge=std::find_if(charges.proofs.begin(),charges.proofs.end(),
      [](const auto& charge){return charge.target_label=="scale";});
  require(first_charge!=charges.proofs.end(),"source proof identifies the first literal");
  const std::map<int,std::set<std::string>> families{{11,{"scale","echo"}}};
  require(!detail::joint_runtime_charge_stack_is_private(
              selective_bound->items,selective_flow,*first_charge,families),
          "the conservative graph includes a wrong callback that consumes address X");
  const auto selective_guards=detail::protected_joint_runtime_charge_targets(
      selective_bound->items,&selective_flow);
  require(selective_guards && !selective_guards->contains("scale") &&
              selective_guards->contains("echo"),
          "exact callback contexts permit the dead first word and pin the observed second word");

  const auto input=epoch_recolor_fixture();
  const auto private_words=detail::protected_joint_runtime_charge_targets(input.items);
  require(private_words && private_words->empty(),"dead address stack residues permit retuning");
  const auto untouched=input;
  const auto ops=raise_machine_to_ir(input.items);
  const auto requests=passes::semantic_region_tail_register_requests(ops,true);
  require(!requests.empty(),"discover operand constraints at an interior suffix, not only whole equal-length regions");
  std::vector<std::vector<IrOp>> ordinary,candidates;
  passes::for_each_register_web_recoloring(ops,{options},requests,
      [&](std::vector<IrOp> candidate){ordinary.push_back(std::move(candidate));});
  passes::for_each_deferred_register_web_recoloring(ops,{options},requests,control,
      seed_origins(input),[&](std::vector<IrOp> candidate){candidates.push_back(std::move(candidate));});
  require(ordinary.empty() && candidates.size()==1,
          "only exact callback epochs establish the noninterfering operand equality");
  auto selected=input;
  selected.items=lower_ir_to_machine(candidates.front());
  require(cells(selected)==cells(input),"epoch equality is a neutral enabling edge, not copy deletion");
  for (const auto* seed : {"0","-17.5","8.ABCDAB1"})
    require(deferred_mode_observe(input,11,seed)==deferred_mode_observe(selected,11,seed),
            "neutral epoch allocation preserves the whole bank, stack, X1/X2 and matched returns");
  const auto folded=passes::semantic_region_folding_with_deferred_layout(candidates.front(),{options},control);
  require(folded.applied>0,"neutral epoch allocation must enable generic shared-region folding");
  auto folded_artifact=selected;
  folded_artifact.items=lower_ir_to_machine(folded.ops);
  const auto alias_privacy=detail::protected_joint_runtime_charge_targets(folded_artifact.items);
  const auto closed=rebind_layout_search_artifact(std::move(folded_artifact));
  require(alias_privacy && alias_privacy->empty(),
          "dead stack residues discharge the new aliased-entry obligations");
  require(closed && cells(*closed)<cells(input),"shared epochs close to a genuinely smaller artifact");
  for (const auto* seed : {"0","-17.5","8.ABCDAB1"})
    require(deferred_mode_observe(input,11,seed)==deferred_mode_observe(*closed,11,seed),
            "composed epoch allocation and folding preserve every external observation");
  for (const auto& bad : {epoch_recolor_fixture(true,false),epoch_recolor_fixture(false,true)}) {
    const auto bad_ops=raise_machine_to_ir(bad.items);
    std::size_t delivered=0;
    passes::for_each_deferred_register_web_recoloring(bad_ops,{options},epoch_recolor_request(bad_ops),
        control,seed_origins(bad),[&](std::vector<IrOp>){++delivered;});
    require(delivered==0,"overlapping epochs and final hardware-bank observations prohibit reuse");
  }
  auto wrong=seed_origins(input);
  ++wrong.at("scale");
  std::size_t delivered=0;
  passes::for_each_deferred_register_web_recoloring(ops,{options},requests,control,wrong,
      [&](std::vector<IrOp>){++delivered;});
  require(delivered==0,"inconsistent literal origins cannot prove a shorter lifetime");
  const auto composed=search_joint_representation_constraints(input,options);
  require(composed.incumbent && cells(*composed.incumbent)<cells(input),
          "the compiler transaction must compose epoch allocation, interior-tail sharing and selector closure");
  for (const auto* seed : {"0","-17.5","8.ABCDAB1"})
    require(deferred_mode_observe(input,11,seed)==deferred_mode_observe(*composed.incumbent,11,seed),
            "published combined candidate preserves every external ROM observation");
  require(same_layout_search_artifact(input,untouched),"epoch recoloring never mutates its input artifact");

  LayoutSearchArtifact root_alias;
  emit(root_alias.items,{0x8a});
  root_alias.items.back().indirect_flow_targets=std::vector<IrTarget>{std::string{"body"}};
  root_alias.items.push_back(MachineItem::label("body"));
  emit(root_alias.items,{0x5b});
  root_alias.items.push_back(MachineItem::address("visit"));
  emit(root_alias.items,{0x50});
  root_alias.items.back().stop_disposition=StopDisposition::Terminal;
  root_alias.items.push_back(MachineItem::label("visit"));
  emit(root_alias.items,{0x51});
  root_alias.items.push_back(MachineItem::address("back"));
  root_alias.items.push_back(MachineItem::label("back"));
  emit(root_alias.items,{0x8a});
  root_alias.items.back().indirect_flow_targets=std::vector<IrTarget>{std::string{"body"}};
  root_alias.preloads={{.register_name="a",.value="1"},{.register_name="1",.value="3"}};
  const auto root_ir=raise_machine_to_ir(root_alias.items);
  require(passes::semantic_region_folding(root_ir,{options}).applied==0,
          "ordinary folding retains its historical root-entry policy");
  const auto root_fold=passes::semantic_region_folding_with_deferred_layout(root_ir,{options},control);
  require(root_fold.applied>0,"a closed repeated jump can alias an unchanged root instruction");
  auto root_aliased=root_alias;
  root_aliased.items=lower_ir_to_machine(root_fold.ops);
  require(cells(root_alias)==7 && cells(root_aliased)==6,
          "root-entry alias removes one repeated jump without adding a transfer");
  for (const int seed:{0,7,-31})
    require(allocation_observe(root_alias,seed)==allocation_observe(root_aliased,seed),
            "root-entry alias preserves ROM loop exit, every register, stack and X1/X2");

  std::cout << "deferred epoch recoloring: " << cells(input) << " -> " << cells(*closed) << " cells\n";
}

void joint_origin_transport_contracts() {
  require(transaction_remaining_growth_budget(50,45,4) == std::optional{9} &&
              transaction_remaining_growth_budget(50,53,4) == std::optional{1} &&
              transaction_remaining_growth_budget(50,54,4) == std::optional{0} &&
              !transaction_remaining_growth_budget(50,55,4) &&
              !transaction_remaining_growth_budget(-1,0,4),
          "allocation keeps earned space and pays every borrowed root cell");
  const int largest = std::numeric_limits<int>::max();
  require(transaction_remaining_growth_budget(largest,0,largest) ==
              std::optional{largest},
          "remaining growth saturation cannot overflow or admit an invalid count");
  std::vector<MachineItem> items{MachineItem::op(0xa7,"recorded family")};
  items.front().indirect_flow_targets = std::vector<IrTarget>{std::string{"omega"}};
  const detail::JointOriginMap geometry{{"omega",44}}, encoded{{"7",3}};
  const auto origins = detail::joint_target_origins(items,geometry,encoded);
  require(origins && origins->at("omega") == 3,
          "source word origin is independent of speculative command geometry");
  std::vector<PreloadReport> before{{.register_name="7",.value="3"}};
  auto after = before;
  after.front().value="44";
  require(detail::transport_joint_selector_word_origins(
              items,geometry,before,after,encoded,AddressSpaceModel::Standard) ==
              std::optional{detail::JointOriginMap{{"7",44}}},
          "a separately proved literal change gets its own exact encoded origin");
  require(!detail::transport_joint_selector_word_origins(
              items,{{"omega",45}},before,after,encoded,AddressSpaceModel::Standard) &&
              !detail::transport_joint_selector_word_origins(
              items,geometry,before,after,{{"7",4}},AddressSpaceModel::Standard),
          "new equations and old recorded words must each agree with machine decoding");
  after.front().value="A0";
  require(detail::transport_joint_selector_word_origins(
              items,{{"omega",100}},before,after,encoded,AddressSpaceModel::Standard) ==
              std::optional{detail::JointOriginMap{{"7",100}}},
          "official BCD A0 retains its machine-word type");
  after.front().value="100";
  require(!detail::transport_joint_selector_word_origins(
              items,{{"omega",100}},before,after,encoded,AddressSpaceModel::Standard),
          "decimal ordinal 100 is not a BCD A0 address");
  after=before;
  after.front().setup_expression_text="generated address";
  require(!detail::transport_joint_selector_word_origins(
              items,geometry,before,after,encoded,AddressSpaceModel::Standard),
          "literal-origin bookkeeping cannot certify generated delivery");
  after=before;
  after.push_back(after.front());
  require(!detail::transport_joint_selector_word_origins(
              items,geometry,before,after,encoded,AddressSpaceModel::Standard),
          "ambiguous preload owners cannot supply a word origin");
  auto collision=items;
  collision.push_back(MachineItem::op(0xa8,"conflicting family"));
  collision.back().indirect_flow_targets=items.front().indirect_flow_targets;
  require(!detail::joint_target_origins(collision,geometry,{{"7",3},{"8",4}}),
          "conflicting family origins cannot prune away a real callback");
  require(!detail::joint_target_origins(items,{},encoded) &&
              !detail::joint_target_origins(items,geometry,{{"f",3}}),
          "missing command identities and Rf are not stock selector permissions");
  auto released=items;
  released.front().indirect_flow_targets.reset();
  require(detail::transport_joint_selector_word_origins(
              released,geometry,before,before,encoded,AddressSpaceModel::Standard) ==
              std::optional{detail::JointOriginMap{}},
          "a fully released family has no stale runtime-origin equation");
}

LayoutSearchArtifact composed_family_fixture(int carrier, int repetitions,
                                             bool expose_word = false, bool expose_charge = false) {
  auto result=deferred_mode_fixture(carrier,11);
  auto& items=result.items;
  if (!expose_charge)
    for (auto& item : items)
      if (item.kind == MachineItemKind::Op && item.opcode == 0x6b) item.opcode=0x6e;
  for (auto& item : items)
    if (item.kind == MachineItemKind::Op && item.opcode == 0x6c)
      item.opcode=0x6b;
  auto first=MachineItem::op(0xa7,"stable callback");
  first.indirect_flow_targets=std::vector<IrTarget>{std::string{"echo"}};
  auto second=MachineItem::op(0xac,"independent stable callback");
  second.indirect_flow_targets=std::vector<IrTarget>{std::string{"other"}};
  items.insert(items.begin(),{second,first});
  const auto terminal=std::find_if(items.begin(),items.end(),
      [](const auto& item) { return item.stop_disposition == StopDisposition::Terminal; });
  std::vector<MachineItem> calls;
  for (int count=0;count<repetitions;++count) {
    emit(calls,{0x53});
    calls.push_back(MachineItem::address(std::string{"spare"}));
  }
  if (expose_word) emit(calls,{0x67});
  items.insert(terminal,calls.begin(),calls.end());
  items.push_back(MachineItem::label("spare"));
  emit(items,{0x61,0x52});
  items.push_back(MachineItem::label("other"));
  emit(items,{0x62,0x52});
  int address=0;
  for (const auto& item : items) {
    if (item.kind != MachineItemKind::Label) { ++address; continue; }
    if (item.name == "echo" || item.name == "other")
      result.preloads.push_back({.register_name=item.name == "echo" ? "7" : "c",
                                .value=std::to_string(address)});
  }
  const auto bound=rebind_layout_search_artifact(std::move(result));
  require(bound.has_value(),"composed fixture binds both runtime charges and stable words");
  return *bound;
}

std::vector<std::string> composed_family_observe(
    const LayoutSearchArtifact& artifact, const std::string& input) {
  const auto resolved=resolve_machine_items(artifact.items);
  require(resolved.diagnostics.empty(),"composed family artifact resolves");
  std::vector<int> code;
  for (const auto& step : resolved.steps) code.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(code).diagnostics.empty(),"composed family artifact fits stock");
  for (int reg=0;reg<15;++reg)
    calc.set_register(std::string(1,"0123456789abcde"[reg]),std::to_string(reg+11));
  calc.set_register("6",input);
  for (const char* name : {"X","Y","Z","T","X1"}) calc.set_register(name,"-12.75");
  for (const auto& preload : artifact.preloads)
    calc.set_register(preload.register_name,preload.value);
  std::vector<std::string> observations;
  for (int run=0;run<2;++run) {
    calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});
    require(calc.run_until_stable(6000,8).stopped,"all composed nested calls return");
    for (const char* name : {"X","Y","Z","T","X1"})
      observations.push_back(calc.read_register(name));
    for (int reg=0;reg<15;++reg) {
      if (reg == 7 || reg == 11 || reg == 12) continue; // Proved internal address representations.
      observations.push_back(calc.read_register(std::string(1,"0123456789abcde"[reg])));
    }
    calc.press(".");
    observations.push_back(calc.display_text(true));
  }
  return observations;
}

void composed_family_contracts() {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole=true;
  for (const int carrier : {2,8}) {
    const auto original=composed_family_fixture(carrier,6);
    const auto preserved=original;
    const auto result=search_joint_representation_constraints(original,options);
    require(result.incumbent && cells(original) == 57 && cells(*result.incumbent) <= 41 &&
                report(*result.incumbent,"finite-mode-representation") &&
                report(*result.incumbent,"joint-composed-selector-allocation") &&
                report(*result.incumbent,"joint-composed-representation-frontier") &&
                build_post_layout_control_flow(result.incumbent->items).proved,
            "representation/family/finalization product beats the historical 46-cell winner");
    for (const char* input : {"0","-2","7","12345678","8.ABCDAB1"})
      require(composed_family_observe(original,input) ==
                  composed_family_observe(*result.incumbent,input),
              "composition preserves repeated ROM calls, every data register, stack and X1/X2");
    require(same_layout_search_artifact(original,preserved),"composition never mutates its root");
    const auto repeated=search_joint_representation_constraints(original,options);
    require(repeated.incumbent &&
                same_layout_search_artifact(*repeated.incumbent,*result.incumbent),
            "joint composition and encoded words are deterministic");

    const auto exposed=composed_family_fixture(carrier,6,true);
    const auto protected_result=search_joint_representation_constraints(exposed,options);
    if (protected_result.incumbent) {
      const auto word=std::find_if(protected_result.incumbent->preloads.begin(),
          protected_result.incumbent->preloads.end(),
          [](const auto& preload) { return preload.register_name == "7"; });
      const auto old_word=std::find_if(exposed.preloads.begin(),exposed.preloads.end(),
          [](const auto& preload) { return preload.register_name == "7"; });
      require(word != protected_result.incumbent->preloads.end() && word->value == old_word->value,
              "an observed raw selector word cannot be retuned as internal data");
      require(composed_family_observe(exposed,"-2") ==
                  composed_family_observe(*protected_result.incumbent,"-2"),
              "a live word remains visible through finalization and allocation");
    }
    const auto live_charge=composed_family_fixture(carrier,6,false,true);
    const auto live_result=search_joint_representation_constraints(live_charge,options);
    if (live_result.incumbent)
      require(composed_family_observe(live_charge,"0") ==
                  composed_family_observe(*live_result.incumbent,"0"),
              "a runtime charge exposed through last-X cannot change its numeric value");
    std::size_t intermediate_calls=0, publication_vetoes=0;
    const auto vetoed=search_joint_representation_constraints(original,options,
        [&](const LayoutSearchArtifact&) -> std::optional<LayoutSearchArtifact> {
          ++publication_vetoes; return std::nullopt;
        },{},true,
        [&](const LayoutSearchArtifact& artifact) -> std::optional<LayoutSearchArtifact> {
          ++intermediate_calls; return artifact;
        });
    require(!vetoed.incumbent && intermediate_calls > 0 && publication_vetoes > 0,
            "validated private intermediates never bypass the final publication veto");
    std::size_t reopened_publications = 0;
    const auto reopened = search_joint_representation_constraints(original, options,
        [&](const LayoutSearchArtifact& artifact) -> std::optional<LayoutSearchArtifact> {
          if (!report(artifact, "joint-proof-closed-continuation")) return std::nullopt;
          require(std::none_of(artifact.items.begin(), artifact.items.end(),
              [](const auto& item) {
                return std::any_of(item.roles.begin(), item.roles.end(), [](const auto& role) {
                  return role.starts_with("finalization-cell-origin:");
                });
              }), "a reopened closure must not freeze obsolete finalization geometry");
          require(build_post_layout_control_flow(artifact.items).proved &&
                      composed_family_observe(original, "-2") ==
                          composed_family_observe(artifact, "-2"),
                  "reopening retains the final CFG, full ROM bank, stack and X1/X2");
          ++reopened_publications;
          return artifact;
        }, {}, true,
        [](const LayoutSearchArtifact& artifact) -> std::optional<LayoutSearchArtifact> {
          auto tagged = artifact;
          for (std::size_t i = 0; i < tagged.items.size(); ++i)
            tagged.items[i].roles.push_back("finalization-cell-origin:" + std::to_string(i));
          return tagged;
        });
    require(reopened.incumbent && reopened_publications > 0,
            "proof-closed continuations must reopen independently of the publication lane");
    const auto before_irreversible_lowering = search_joint_representation_constraints(original, options,
        [](const LayoutSearchArtifact& artifact) -> std::optional<LayoutSearchArtifact> {
          if (!report(artifact, "joint-proof-closed-continuation") ||
              std::any_of(artifact.items.begin(), artifact.items.end(),
                  [](const auto& item) { return item.raw; }))
            return std::nullopt;
          return artifact;
        }, {}, true,
        [](const LayoutSearchArtifact& artifact) -> std::optional<LayoutSearchArtifact> {
          auto opaque = artifact;
          for (auto& item : opaque.items)
            if (item.kind == MachineItemKind::Op) item.raw = true;
          return opaque;
        });
    require(before_irreversible_lowering.incumbent &&
                composed_family_observe(original, "7") ==
                    composed_family_observe(*before_irreversible_lowering.incumbent, "7"),
            "a greedy opaque finalization must not destroy its independently closed input; "
            "publication must still authorize the actual nonopaque candidate");
    TransactionSearchLimits stopped;
    stopped.maximum_depth=1;
    const auto shallow=search_joint_representation_constraints(original,options,{},stopped);
    require(!shallow.incumbent ||
                !report(*shallow.incumbent,"joint-composed-representation-frontier"),
            "the composition lane pays depth rather than reopening an exhausted search");
  }
  std::cout << "joint family/representation composition: 57 -> 41 cells (previously 46)\n";
}

void deferred_mode_contracts() {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  const auto candidates = [&](const LayoutSearchArtifact& artifact,
                              const std::map<std::string, int>* origins) {
    return passes::deferred_finite_mode_representation_candidates(
        raise_machine_to_ir(artifact.items), {options},
        {.maximum_candidates = 8, .allow_target_relayout = true,
         .runtime_preloads = &artifact.preloads,
         .preserve_compiler_selector_charges = true,
         .assumed_target_addresses = origins});
  };
  for (const auto& [carrier, selector] : std::vector<std::pair<int, int>>{{8, 7}, {2, 8}}) {
    const auto original = deferred_mode_fixture(carrier, selector);
    const auto origins = seed_origins(original);
    const auto pending = exchange_deferred_mode_regions(original);
    const auto preserved = pending;
    require(cells(original) == 39 && seed_origins(pending) != origins,
            "finite-mode baseline starts at 39 with unresolved callback geometry");
    require(candidates(pending, nullptr).empty(),
            "provisional physical positions cannot prove the selected finite epoch");
    const auto proposals = candidates(pending, &origins);
    require(!proposals.empty(), "encoded callback origins must enable finite representation proof");
    int best = cells(original);
    for (const auto& proposal : proposals) {
      for (const std::string target : {"scale", "echo"})
        require(proposal.required_target_addresses.at(target) == origins.at(target),
                "conditional data proof must retain every unchanged callback equation");
      require(std::any_of(proposal.transformation.optimizations.begin(),
                          proposal.transformation.optimizations.end(),
                  [](const auto& report) {
                    return report.name == "finite-mode-deferred-selector-proof";
                  }), "explain must identify the conditional callback proof");
      const auto closed = rebind_layout_search_artifact({
          .items = lower_ir_to_machine(proposal.transformation.ops),
          .preloads = proposal.transformation.preloads});
      require(closed && build_post_layout_control_flow(closed->items).proved,
              "publication needs separately rebound words and an authoritative final CFG");
      best = std::min(best, cells(*closed));
      for (const std::string input : {"0", "-2", "7", "12345678", "8.ABCDAB1"})
        require(deferred_mode_observe(original, selector, input) ==
                    deferred_mode_observe(*closed, selector, input),
                "both epochs keep numeric/raw data, stack, X1/X2 and nested return behavior");
    }
    require(best == 34, "conditional finite-mode size baseline must shrink 39 to 34");
    const auto repeated = candidates(pending, &origins);
    require(repeated.size() == proposals.size(), "conditional candidate count is deterministic");
    for (std::size_t i = 0; i < repeated.size(); ++i)
      require(repeated[i].required_target_addresses == proposals[i].required_target_addresses &&
                  same_layout_search_artifact(
                      {.items = lower_ir_to_machine(repeated[i].transformation.ops),
                       .preloads = repeated[i].transformation.preloads},
                      {.items = lower_ir_to_machine(proposals[i].transformation.ops),
                       .preloads = proposals[i].transformation.preloads}),
              "conditional word choices and origin obligations are deterministic");
    require(same_layout_search_artifact(pending, preserved),
            "conditional proof must not mutate its search parent");

    auto missing = origins;
    missing.erase("scale");
    require(candidates(pending, &missing).empty(), "incomplete origins cannot remove a callback");
    auto collision = origins;
    collision["echo"] = collision["scale"];
    require(candidates(pending, &collision).empty(), "colliding command origins fail closed");
    auto contradictory = origins;
    ++contradictory["scale"];
    require(candidates(pending, &contradictory).empty(),
            "a contradicted runtime selector cannot make its finite epoch vacuously safe");
    auto raw = pending;
    raw.items[2].raw = true;
    require(candidates(raw, &origins).empty(), "raw finite producers need an independent proof");
    auto unknown = pending;
    unknown.items[2] = MachineItem::op(0x66, "unknown callback input");
    require(candidates(unknown, &origins).empty(),
            "precise callback selection is not a finite numeric-value assumption");
    for (const auto observation : {DeferredModeObservation::LastX,
                                   DeferredModeObservation::EntryShadow}) {
      const auto observable = deferred_mode_fixture(carrier, selector, observation);
      const auto observable_origins = seed_origins(observable);
      require(candidates(exchange_deferred_mode_regions(observable),
                         &observable_origins).empty(),
              "deferred context must not bypass observed X1 or entry-shadow differences");
    }
  }
  std::cout << "conditional finite representation baseline: 39 -> 34 cells\n";
}

void deferred_selector_contracts() {
  const CompileOptions options;
  for (const auto& [counter, selector] : std::vector<std::pair<int, int>>{{0, 11}, {2, 12}}) {
    const auto original = deferred_seed_fixture(counter, selector);
    require(cells(original) == 37, "selected-callback seed size baseline starts at 37");
    const auto origins = seed_origins(original);
    auto pending = original;
    const auto advance = std::find_if(pending.items.begin(), pending.items.end(),
        [](const auto& item) { return item.kind == MachineItemKind::Label && item.name == "advance"; });
    const auto inspect = std::find_if(pending.items.begin(), pending.items.end(),
        [](const auto& item) { return item.kind == MachineItemKind::Label && item.name == "inspect"; });
    std::rotate(advance, inspect, pending.items.end());
    const auto flow = build_post_layout_control_flow(pending.items);
    require(flow.proved, "conditional fixture retains command-identity CFG");
    const auto product = build_deferred_selector_consistent_execution_graph(pending.items, flow, origins, 20000);
    require(product.execution.proved && product.execution.pruned_edges > 0,
            "encoded origins, not speculative positions, select the callback");
    const auto proposal = passes::deferred_indirect_selector_seed_reuse(
        raise_machine_to_ir(pending.items), {options}, {}, origins);
    require(proposal.encoded_target_origins == origins,
            "conditional transformation must retain every encoded-origin obligation");
    require(proposal.transformation.applied == 1, "selected first callback must prove the mutating seed");
    auto closed = rebind_layout_search_artifact({.items = lower_ir_to_machine(proposal.transformation.ops)});
    require(closed && cells(*closed) == cells(original) - 1,
            "deferred seed must remove one real cell after final binding");
    require(build_post_layout_control_flow(closed->items).proved,
            "final selected callback and nested return CFG proves independently");
    for (const std::string packed : {"12345678", "8.0000001", "8.ABCDAB1"})
      require(deferred_seed_observe(original, selector, packed) ==
                  deferred_seed_observe(*closed, selector, packed),
              "deferred seed keeps numeric/raw inputs, memory, stack, X1/X2 and nested returns");

    auto missing = origins;
    missing.erase("advance");
    require(!build_deferred_selector_consistent_execution_graph(pending.items, flow, missing, 20000).execution.proved,
            "missing conditional origin is not physical-address fallback");
    auto collision = origins;
    collision["inspect"] = collision["advance"];
    require(!build_deferred_selector_consistent_execution_graph(pending.items, flow, collision, 20000).execution.proved,
            "different command identities cannot have one encoded origin");
    auto contradictory = origins;
    ++contradictory["advance"];
    require(!build_deferred_selector_consistent_execution_graph(pending.items, flow, contradictory, 20000).execution.proved,
            "a contradicted reachable callback cannot disappear vacuously");
    require(!build_deferred_selector_consistent_execution_graph(pending.items, flow, origins, 1).execution.proved,
            "exhausted conditional product fails closed");

    auto outside = origins;
    outside["advance"] = 100;
    require(!build_deferred_selector_consistent_execution_graph(
                pending.items, flow, outside).execution.proved,
            "a two-digit deferred origin cannot use an overflow address");
    auto formal = pending;
    formal.items.front().formal_opcode = 0xf9;
    require(!build_deferred_selector_consistent_execution_graph(
                formal.items, flow, origins).execution.proved,
            "opaque formal counters need a separate transport proof");
    auto raw = pending;
    raw.items.at(2).raw = true;
    require(passes::deferred_indirect_selector_seed_reuse(
                raise_machine_to_ir(raw.items), {options}, {}, origins).transformation.applied == 0,
            "raw producers are not conditional seed permissions");
    auto alias = pending;
    const auto alias_at = std::find_if(alias.items.begin(), alias.items.end(),
        [](const auto& item) { return item.kind == MachineItemKind::Label && item.name == "advance"; });
    alias.items.insert(alias_at, MachineItem::label("same-command"));
    int dispatch_calls = 0;
    for (auto& item : alias.items) {
      if (item.indirect_flow_targets)
        item.indirect_flow_targets->front() = std::string("same-command");
      if (item.kind == MachineItemKind::Address &&
          item.target == IrTarget{std::string("dispatch")} && ++dispatch_calls == 2)
        item.target = std::string("second-dispatch");
    }
    alias.items.push_back(MachineItem::label("second-dispatch"));
    emit(alias.items, {0x80 + selector});
    alias.items.back().indirect_flow_targets =
        std::vector<IrTarget>{std::string("advance"), std::string("inspect")};
    auto aliases = origins;
    aliases["same-command"] = aliases["advance"];
    const auto alias_flow = build_post_layout_control_flow(alias.items);
    require(build_deferred_selector_consistent_execution_graph(
                alias.items, alias_flow, aliases).execution.proved,
            "equal encoded origins transport aliases of the same command");
    ++aliases["same-command"];
    require(!build_deferred_selector_consistent_execution_graph(
                alias.items, alias_flow, aliases).execution.proved,
            "aliases of one command cannot contradict each other's encoded origins");

    const auto combined = search_joint_representation_constraints(original, options);
    require(combined.incumbent && cells(*combined.incumbent) < cells(original) &&
                report(*combined.incumbent, "indirect-selector-seed-reuse"),
            "real joint search must close the seed alternative, not just expose an unused API");
    require(deferred_seed_observe(original, selector, "12345678") ==
                deferred_seed_observe(*combined.incumbent, selector, "12345678"),
            "production finalizer preserves the same selected-callback observations");

    auto observed = pending;
    const auto seed = std::find_if(observed.items.begin(), observed.items.end(), [](const auto& item) {
      return item.kind == MachineItemKind::Op && item.opcode == 8;
    });
    observed.items.insert(seed, MachineItem::op(0x60 + counter, "early read"));
    require(passes::deferred_indirect_selector_seed_reuse(
                raise_machine_to_ir(observed.items), {options}, {}, origins).transformation.applied == 0,
            "speculative selector cannot be observed before canonicalization");

    auto unknown = pending;
    for (auto& item : unknown.items)
      if (item.kind == MachineItemKind::Op && item.opcode == 0x40 + selector) {
        const auto at = static_cast<std::size_t>(&item - unknown.items.data());
        unknown.items[at - 1].opcode = 0x6d;
        unknown.items[at - 1].roles.clear();
        break;
      }
    require(passes::deferred_indirect_selector_seed_reuse(
                raise_machine_to_ir(unknown.items), {options}, {}, origins).transformation.applied == 0,
            "unknown callback cannot inherit the intended target from metadata");
  }
}


int complementary_condition(int opcode) {
  switch (opcode) {
  case 0x57: return 0x5e;
  case 0x5e: return 0x57;
  case 0x59: return 0x5c;
  case 0x5c: return 0x59;
  default: throw std::runtime_error("not a direct condition");
  }
}

LayoutSearchArtifact branch_fixture(int condition, bool local = false,
                                   bool expose_x2 = false, bool unsafe_flip = false) {
  LayoutSearchArtifact result;
  auto& items = result.items;
  for (int call = 0; call < 2; ++call) {
    emit(items, {0x53});
    items.push_back(MachineItem::address(std::string{"handler"}));
  }
  emit(items, {0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
  items.push_back(MachineItem::label("handler"));
  emit(items, {0x60, 0x61, 0x10,
               unsafe_flip ? complementary_condition(condition) : condition});
  items.push_back(MachineItem::address(unsafe_flip ? std::string{"a"} : std::string{"b"}));
  if (local && !unsafe_flip) {
    emit(items, {0x51});
    items.push_back(MachineItem::address(std::string{"a"}));
  }
  const std::vector<std::string> order = local || unsafe_flip
      ? std::vector<std::string>{"b", "a"} : std::vector<std::string>{"a", "b"};
  for (const auto& label : order) {
    items.push_back(MachineItem::label(label));
    emit(items, {expose_x2 ? 0x0a : label == "a" ? 0x65 : 0x66,
                 label == "a" ? 0x48 : 0x49, 0x52});
  }
  return result;
}

std::vector<std::string> branch_observe(const LayoutSearchArtifact& artifact,
                                      const std::string& input) {
  const auto resolved = resolve_machine_items(artifact.items);
  require(resolved.diagnostics.empty(), "branch fixture resolves");
  std::vector<int> code;
  for (const auto& step : resolved.steps) code.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(code).diagnostics.empty(), "branch fixture fits stock memory");
  for (int reg = 0; reg < 15; ++reg)
    calc.set_register(std::string(1, "0123456789abcde"[reg]), std::to_string(reg + 3));
  calc.set_register("0", input);
  calc.set_register("1", "2");
  for (const char* name : {"X", "Y", "Z", "T", "X1"})
    calc.set_register(name, "-12.75");
  calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  require(calc.run_until_stable(4000, 8).stopped, "both caller continuations must return");
  std::vector<std::string> result;
  for (const char* name : {"X", "Y", "Z", "T", "X1"})
    result.push_back(calc.read_register(name));
  for (int reg = 0; reg < 15; ++reg)
    result.push_back(calc.read_register(std::string(1, "0123456789abcde"[reg])));
  calc.press(".");
  result.push_back(calc.display_text(true));
  return result;
}


void conditional_numeric_entry_rom_fact() {
  const std::vector<std::vector<int>> prefixes{
      {0x60, 0x22}, {0x60}, {5}, {5, 0x44}, {5, 0x0e},
      {1, 0x0c, 3}, {0x60, 0x61, 0x10}};
  std::size_t cases = 0;
  for (const int condition : {0x57, 0x5e, 0x59, 0x5c}) {
    for (const auto& prefix : prefixes) {
      const auto fixture = [&](bool inverted) {
        LayoutSearchArtifact result;
        auto& items = result.items;
        emit(items, {0x53});
        items.push_back(MachineItem::address(std::string{"handler"}));
        emit(items, {0x50});
        items.back().stop_disposition = StopDisposition::Terminal;
        items.push_back(MachineItem::label("handler"));
        for (const int opcode : prefix) emit(items, {opcode});
        emit(items, {inverted ? complementary_condition(condition) : condition});
        items.push_back(MachineItem::address(
            inverted ? std::string{"a"} : std::string{"b"}));
        const std::vector<std::string> order = inverted
            ? std::vector<std::string>{"b", "a"} : std::vector<std::string>{"a", "b"};
        for (const auto& label : order) {
          items.push_back(MachineItem::label(label));
          emit(items, {7, 2, label == "a" ? 0x48 : 0x49, 0x52});
        }
        return result;
      };
      const auto before = fixture(false), after = fixture(true);
      for (const char* input : {"0", "1", "-1", "0.5", "-0.5", "1E30"}) {
        const auto original = branch_observe(before, input);
        require(original.front() == "72," && original == branch_observe(after, input),
                "both F outcomes close recalled/open/stored/Enter/exponent/arithmetic entry");
        ++cases;
      }
      CompileOptions options;
      const auto choices = passes::conditional_region_exchange_candidates(
          raise_machine_to_ir(before.items), {options}, {});
      require(!choices.empty(), "certified fresh digits close the inversion proof");
    }
  }
  require(cases == 168, "the direct-condition entry fact covers every prefix and polarity");
}

void conditional_region_contracts() {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  options.tail_branch_inversion = true;
  for (const int opcode : {0x57, 0x5e, 0x59, 0x5c}) {
    const auto original = branch_fixture(opcode);
    const auto ir = raise_machine_to_ir(original.items);
    const std::map<std::string, int> bindings{{"handler", 5}, {"b", 10}};
    require(!passes::layout_symbolic_regions(ir, bindings, {options}),
            "whole-chain placement cannot overlap an anchored arm with its predecessor");
    const auto choices = passes::conditional_region_exchange_candidates(ir, {options}, {});
    require(choices.size() == 1, "one proved complementary closed-arm exchange is available");
    require(cells({.items = lower_ir_to_machine(choices.front().ops)}) == cells(original),
            "branch exchange itself neither spends nor claims cells");
    const auto placed = passes::layout_symbolic_regions(
        choices.front().ops, bindings, {options});
    require(placed && placed->projected_targets.at("handler") == 5 &&
                placed->projected_targets.at("b") == 10,
            "polarity and component boundaries must solve the same address equations");
    const LayoutSearchArtifact exchanged{.items = lower_ir_to_machine(placed->ops)};
    const auto final_flow = build_post_layout_control_flow(exchanged.items);
    require(final_flow.proved && final_flow.maximum_observed_return_depth == 1,
            "inversion keeps both exact caller/return contexts");
    for (const std::string value : {"-3", "-2", "0", "1", "0.25", "-0.25"})
      require(branch_observe(original, value) == branch_observe(exchanged, value),
              "both branch outcomes preserve every register, stack, X1 and dot-observed X2");
    const auto repeated = passes::conditional_region_exchange_candidates(ir, {options}, {});
    require(repeated.size() == 1 &&
                same_layout_search_artifact(
                    {.items = lower_ir_to_machine(choices.front().ops)},
                    {.items = lower_ir_to_machine(repeated.front().ops)}),
            "neutral branch alternatives are deterministic");
    require(passes::conditional_region_exchange_candidates(ir, {options}, {}, 0).empty(),
            "zero candidate budget performs no exchange");
    auto raw = ir;
    raw.front().meta.raw = true;
    require(passes::conditional_region_exchange_candidates(raw, {options}, {}).empty(),
            "raw code is not a polarity/layout permission");
    auto loop = ir;
    for (auto& op : loop)
      if (op.kind == IrKind::CondJump) {
        op.kind = IrKind::Loop;
        op.opcode = 0x5d;
        op.counter = "0";
      }
    require(passes::conditional_region_exchange_candidates(loop, {options}, {}).empty(),
            "destructive FL counters are not complementary pure conditions");

    const auto visible = branch_fixture(opcode, false, true);
    require(passes::conditional_region_exchange_candidates(
                raise_machine_to_ir(visible.items), {options}, {}).empty(),
            "a dot can expose the hidden taken/skipped difference");
    const auto local = branch_fixture(opcode, true);
    const auto folded = passes::tail_branch_inversion(raise_machine_to_ir(local.items), {options});
    require(folded.applied == 1 &&
                cells({.items = lower_ir_to_machine(folded.ops)}) == cells(local) - 2,
            "safe local branch inversion retains its two-cell saving");
    for (const std::string value : {"-3", "-2", "1"})
      require(branch_observe(local, value) ==
                  branch_observe({.items = lower_ir_to_machine(folded.ops)}, value),
              "local inversion also transports both returns and full hidden state");
    const auto unsafe = branch_fixture(opcode, true, true);
    const auto refused = passes::tail_branch_inversion(
        raise_machine_to_ir(unsafe.items), {options});
    require(refused.applied == 0,
            "legacy tail inversion must reject an observable hidden-state change");
  }

  const auto unsafe = branch_fixture(0x57, true, true);
  const auto broken = branch_fixture(0x57, true, true, true);
  require(branch_observe(unsafe, "-1") != branch_observe(broken, "-1"),
          "ROM regression pins the real wrong-result case, not just a conservative guard");

  const auto source = branch_fixture(0x57);
  std::size_t finalized_exchanges = 0;
  const auto search = search_joint_representation_constraints(source, options,
      [&](const LayoutSearchArtifact& closed) -> std::optional<LayoutSearchArtifact> {
        if (report(closed, "conditional-region-exchange")) {
          ++finalized_exchanges;
          require(branch_observe(source, "-3") == branch_observe(closed, "-3") &&
                      branch_observe(source, "1") == branch_observe(closed, "1"),
                  "the production closure, not just the isolated API, preserves both arms");
        }
        return closed;
      });
  require(finalized_exchanges > 0, "the independent joint lane must actually close exchanged regions");
  if (search.incumbent)
    require(cells(*search.incumbent) < cells(source),
            "neutral geometry never replaces the independent incumbent by itself");
}

void product_frontier_rom_contracts() {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  const auto source = branch_fixture(0x57);
  std::vector<LayoutSearchArtifact> parents;
  for (int identity = 0; identity < 3; ++identity) {
    auto parent = source;
    const std::string prefix = "opaque_" + std::to_string(identity) + "_";
    for (auto& item : parent.items) {
      if (item.kind == MachineItemKind::Label) item.name = prefix + item.name;
      if (item.kind == MachineItemKind::Address)
        item.target = prefix + std::get<std::string>(item.target);
    }
    parents.push_back(std::move(parent));
  }
  const auto expand = [&](const LayoutSearchArtifact& parent) {
    std::vector<ProvedSearchTransition<LayoutSearchArtifact>> edges;
    for (const auto& proposal : passes::conditional_region_exchange_candidates(
             raise_machine_to_ir(parent.items), {options}, {}))
      edges.push_back({{.items = lower_ir_to_machine(proposal.ops)},
                       "conditional-region-exchange", true});
    return edges;
  };
  std::vector<LayoutSearchArtifact> established;
  for (std::size_t i = 0; i < 2; ++i) {
    const auto choices = expand(parents[i]);
    require(choices.size() == 1, "ROM product has one proved exchange per parent");
    established.push_back(choices.front().node);
  }
  const auto product = collect_proved_product_frontier(
      parents, established, cells(source), {
          .maximum_expansions = 3, .maximum_candidates = 1,
          .maximum_depth = 1, .maximum_growth_cells = 0},
      [](const auto& node) { return cells(node); }, same_layout_search_artifact, expand);
  require(product.candidates.size() == 1 && product.duplicate == 2,
          "late proved branch representation must fill the remaining product slot");
  const auto closed = rebind_layout_search_artifact(product.candidates.front().node);
  require(closed && build_post_layout_control_flow(closed->items).proved,
          "retained branch still requires independent selector and return closure");
  for (const std::string value : {"-3", "-2", "0", "1", "0.25", "-0.25"})
    require(branch_observe(source, value) == branch_observe(*closed, value),
            "late product preserves both branch arms, all data, stack, X1/X2 and returns");
  auto unsafe = branch_fixture(0x57, false, true);
  const auto rejected = collect_proved_product_frontier(
      std::vector<LayoutSearchArtifact>{unsafe}, std::vector<LayoutSearchArtifact>{},
      cells(unsafe), {}, [](const auto& node) { return cells(node); },
      same_layout_search_artifact, expand);
  require(rejected.candidates.empty(), "a product cannot bypass the hidden-X2 proof");
}

LayoutSearchArtifact official_selector_fixture(bool observe_address = false) {
  LayoutSearchArtifact result;
  auto& items = result.items;
  for (const auto& [reg, label] :
       std::vector<std::pair<int, std::string>>{{7, "alpha"}, {8, "beta"}}) {
    emit(items, {0xa0 + reg});
    items.back().indirect_flow_targets = std::vector<IrTarget>{label};
  }
  if (observe_address) emit(items, {0x68, 0x43});
  while (cells(result) < 99) emit(items, {0x54});
  emit(items, {0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
  for (const std::string name : {"alpha", "beta"}) {
    items.push_back(MachineItem::label(name));
    emit(items, {0x60, 0x52});
  }
  result.preloads.push_back({.register_name = "7", .value = "A0"});
  result.preloads.push_back({.register_name = "8", .value = "A2"});
  return result;
}

std::vector<std::string> official_selector_observe(
    const LayoutSearchArtifact& artifact, const std::string& seed) {
  const auto resolved = resolve_machine_items(artifact.items);
  require(resolved.diagnostics.empty(), "official BCD selector fixture resolves");
  std::vector<int> code;
  for (const auto& step : resolved.steps) code.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(code).diagnostics.empty(), "official selector fixture fits stock memory");
  for (int reg = 0; reg < 15; ++reg)
    calc.set_register(std::string(1, "0123456789abcde"[reg]), std::to_string(reg + 2));
  for (const auto& preload : artifact.preloads)
    calc.set_register(preload.register_name, preload.value);
  calc.set_register("0", seed);
  for (const char* name : {"X", "Y", "Z", "T", "X1"})
    calc.set_register(name, "-12.5");
  calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  require(calc.run_until_stable(12000, 8).stopped, "both official-address calls return");
  std::vector<std::string> result;
  for (const char* name : {"X", "Y", "Z", "T", "X1"})
    result.push_back(calc.read_register(name));
  for (int reg = 0; reg < 15; ++reg) {
    if (reg == 7 || reg == 8) continue; // Internal address representation.
    result.push_back(calc.read_register(std::string(1, "0123456789abcde"[reg])));
  }
  calc.press(".");
  result.push_back(calc.display_text(true));
  return result;
}

void official_selector_domains() {
  const auto original = official_selector_fixture();
  const auto flow = build_post_layout_control_flow(original.items);
  require(flow.proved && cells(original) == 104, "official selector baseline is 104 cells");
  for (const auto model : {AddressSpaceModel::Standard, AddressSpaceModel::Mk61SMiniExpanded}) {
    PostLayoutControlFlowOptions options;
    options.address_space_model = model;
    const auto model_flow = build_post_layout_control_flow(original.items, options);
    require(model_flow.proved, "both profiles prove their own rooted control flow");
    const int last = official_program_last_address(model);
    for (int target = 100; target <= last; ++target) {
      const auto word = rebind_stable_preloaded_indirect_flow_selector(
          original.items, original.preloads.front(), model_flow, 100, target, model);
      require(word == std::optional{format_official_address(target, model)},
              "preload codec uses an official BCD word, not its decimal ordinal");
      auto value = *word;
      for (int use = 0; use < 3; ++use) {
        const auto decoded = evaluate_indirect_address(
            "7", value, IndirectOperationKind::Flow, model);
        require(decoded && decoded->formal_address &&
                    decoded->formal_address->kind == FormalAddressKind::Official &&
                    decoded->actual_flow_target == target && decoded->result_value,
                "repeated stable BCD calls retain target identity and raw-word type");
        value = *decoded->result_value;
      }
    }
    require(!rebind_stable_preloaded_indirect_flow_selector(
                original.items, original.preloads.front(), model_flow, 100, last + 1, model),
            "an address-only preload cannot make storage beyond this profile");
    auto generated = original.preloads.front();
    generated.setup_expression_text = "generated address";
    require(!rebind_stable_preloaded_indirect_flow_selector(
                original.items, generated, model_flow, 100, 101, model),
            "changing a literal report cannot rewrite generated setup");
    require(!rebind_stable_preloaded_indirect_flow_selector(
                original.items, original.preloads.front(), model_flow, 99, 101, model),
            "old encoded origin remains an independently checked obligation");
  }
  const auto observed = official_selector_fixture(true);
  require(!rebind_stable_preloaded_indirect_flow_selector(
              observed.items, observed.preloads.back(),
              build_post_layout_control_flow(observed.items), 102, 100),
          "observed raw data is not an address-only BCD word");
  auto written = original;
  written.items.insert(written.items.begin(), MachineItem::op(0x47, "observed selector write"));
  require(!rebind_stable_preloaded_indirect_flow_selector(
              written.items, written.preloads.front(),
              build_post_layout_control_flow(written.items), 100, 101),
          "a runtime-written carrier cannot acquire an unproved address word");

  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  const auto result = search_joint_representation_constraints(original, options);
  require(result.incumbent && cells(*result.incumbent) == 102 &&
              report(*result.incumbent, "semantic-multi-entry-region-folding") &&
              report(*result.incumbent, "joint-selector-constraint-synthesis"),
          "production joint folding and BCD selector rebinding shrink 104 to 102");
  require(build_post_layout_control_flow(result.incumbent->items).proved,
          "the published official-address artifact has a proved final CFG");
  for (const char* seed : {"0", "1", "-17.5", "8.ABCDAB1"})
    require(official_selector_observe(original, seed) ==
                official_selector_observe(*result.incumbent, seed),
            "composed official-address folding preserves all data, stack, X1/X2 and both returns");
  const auto repeat = search_joint_representation_constraints(original, options);
  require(repeat.incumbent &&
              same_layout_search_artifact(*result.incumbent, *repeat.incumbent),
          "official BCD selector synthesis and placement are deterministic");
  std::cout << "official selector composition: 104 -> 102 cells\n";
}


void checkpoint_model_transport_contracts() {
  const auto stock = FeatureProfile::Standard;
  const auto expanded = FeatureProfile::Mk61SMiniExpanded;
  const auto original = official_selector_fixture();
  std::string reason;
  const auto transported = transport_layout_search_artifact(original, stock, expanded, &reason);
  require(transported && reason.empty() && same_layout_search_artifact(original, *transported),
          "compatible model continuation preserves complete IR, preloads and proof roles");
  CompileOptions options;
  options.feature_profile = expanded;
  options.defer_return_suffix_until_callee_hole = true;
  const auto closed = search_joint_representation_constraints(*transported, options);
  require(closed.incumbent && cells(*closed.incumbent) == 102,
          "transported checkpoint composes sharing and selector placement: 104 -> 102");
  for (const char* seed : {"0", "-17.5", "8.ABCDAB1"})
    require(official_selector_observe(original, seed) ==
                official_selector_observe(*closed.incumbent, seed),
            "checkpoint composition preserves ROM stack, X1/X2, data and matched returns");

  auto virtual_root = original;
  while (cells(virtual_root) < 120) {
    emit(virtual_root.items, {0x50});
    virtual_root.items.back().stop_disposition = StopDisposition::Terminal;
  }
  require(transport_layout_search_artifact(virtual_root, stock, expanded).has_value(),
          "oversized logical storage is not mistaken for short-side hardware aliases");

  auto raw = original;
  raw.items.front().raw = true;
  require(!transport_layout_search_artifact(raw, stock, expanded, &reason) && !reason.empty(),
          "opaque raw commands cannot inherit a model proof");
  auto rf = original;
  rf.items.front().opcode = 0x6f;
  require(!transport_layout_search_artifact(rf, stock, expanded),
          "RF recall and stock R0 alias are not the same operation");
  auto memory = original;
  memory.items.front().indirect_memory_targets = std::vector<int>{15};
  require(!transport_layout_search_artifact(memory, stock, expanded),
          "indirect RF access cannot hide in shared-bank metadata");
  auto generated = original;
  generated.preloads.front().setup_expression_text = "generated flow";
  require(!transport_layout_search_artifact(generated, stock, expanded),
          "setup expression requires its own model-compatible value proof");
  auto ambiguous = original;
  ambiguous.preloads.push_back(ambiguous.preloads.front());
  require(!transport_layout_search_artifact(ambiguous, stock, expanded),
          "duplicate preload owners fail closed");

  LayoutSearchArtifact alias;
  emit(alias.items, {0x87});
  alias.items.back().indirect_flow_targets = std::vector<IrTarget>{5};
  while (cells(alias) < 120) {
    emit(alias.items, {0x50});
    alias.items.back().stop_disposition = StopDisposition::Terminal;
  }
  alias.preloads = {{.register_name = "7", .value = "A5"}};
  require(build_post_layout_control_flow(alias.items).proved &&
              !transport_layout_search_artifact(alias, stock, expanded, &reason) &&
              reason.find("preloaded flow word") != std::string::npos,
          "matching metadata cannot hide A5 changing its physical target from 5 to 105");
  alias.preloads.front().value = "5";
  alias.items.front().indirect_flow_formal_targets = std::vector<int>{0xa5};
  require(!transport_layout_search_artifact(alias, stock, expanded),
          "explicit short-side entry cannot become ordinary expanded memory");

  LayoutSearchArtifact boundary;
  while (cells(boundary) < 106) {
    emit(boundary.items, {0x50});
    boundary.items.back().stop_disposition = StopDisposition::Terminal;
  }
  require(!transport_layout_search_artifact(boundary, stock, expanded),
          "logical-to-physical return/counter conversion needs a separate proof");
  std::cout << "checkpoint model transport: 104 -> 102 cells; unsafe models rejected\n";
}

void set_valued_closure_frontier_contracts() {
  struct Node {
    int identity;
    int size;
    bool operator==(const Node&) const = default;
  };
  const auto count = [](const Node& node) { return node.size; };
  const auto equal = [](const Node& a, const Node& b) { return a == b; };
  const auto expand = [](const Node& node) {
    std::vector<ProvedSearchTransition<Node>> edges;
    if (node.identity == 3) {
      edges.push_back({Node{4, 22}, "shared-tail", true});
      edges.push_back({Node{8, 1}, "unproved-shortcut", false});
    }
    return edges;
  };
  const auto close = [](const Node& node, const std::vector<std::string>&) {
    if (node.identity == 1)
      return std::vector<Node>{{2, 28}, {3, 31}, {6, 35}, {2, 28}};
    if (node.identity == 4)
      return std::vector<Node>{{5, 21}};
    return std::vector<Node>{};
  };
  const std::vector<ProvedSearchTransition<Node>> seeds{{Node{1, 32}, "codec", true}};
  const TransactionSearchLimits limits{
      .maximum_expansions = 8, .maximum_candidates = 16,
      .maximum_depth = 5, .maximum_growth_cells = 4};
  const auto run_search = [&] {
    return search_proved_transaction_closures(
        seeds, std::vector<Node>{}, 30, limits, count, equal, expand, close);
  };
  const auto result = run_search();
  require(result.incumbent == std::optional<Node>{Node{5, 21}} &&
              result.duplicate > 0 && result.rejected > 0,
          "a larger proved closure must survive local canonicalization, while duplicate, "
          "unproved and over-budget alternatives remain rejected");
  const auto repeated = run_search();
  require(repeated.incumbent == result.incumbent && repeated.expanded == result.expanded &&
              repeated.admitted == result.admitted && repeated.rejected == result.rejected,
          "set-valued closure exploration must be deterministic");
  const auto greedy = search_proved_transactions(
      seeds, std::vector<Node>{}, 30, limits, count, equal, expand,
      [&](const Node& node, const std::vector<std::string>& path) -> std::optional<Node> {
        const auto closures = close(node, path);
        return closures.empty() ? std::nullopt : std::optional{closures.front()};
      });
  require(greedy.incumbent == std::optional<Node>{Node{2, 28}},
          "the legacy optional-finalizer lane must retain its old choice and ordering");
  auto shallow = limits;
  shallow.maximum_depth = 2;
  const auto bounded = search_proved_transaction_closures(
      seeds, std::vector<Node>{}, 30, shallow, count, equal, expand, close);
  require(bounded.incumbent == std::optional<Node>{Node{2, 28}},
          "closure edges must spend the same depth budget as other transformations");
  const auto vetoed = search_proved_transaction_closures(
      seeds, std::vector<Node>{}, 30, limits, count, equal, expand,
      [](const Node&, const std::vector<std::string>&) { return std::vector<Node>{}; });
  require(!vetoed.incumbent,
          "an absent complete-artifact certificate must never become a closure edge");
}

void run() {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  const auto input = fixture();
  const auto original = input;
  const auto ir = raise_machine_to_ir(input.items);
  const auto words = passes::finite_mode_representation_candidates(
      ir, {options}, {.maximum_growth_cells = 4, .maximum_candidates = 16,
                     .allow_target_relayout = true, .runtime_preloads = &input.preloads});
  require(std::all_of(words.begin(), words.end(), [&](const auto& candidate) {
            return cells(LayoutSearchArtifact{.items = lower_ir_to_machine(candidate.ops)}) >=
                cells(input);
          }), "a frozen final selector must block a standalone size improvement");
  require(passes::semantic_region_folding(ir, {options}).applied == 0,
          "different physical temporaries must block standalone sharing");
  std::size_t finalized = 0;
  const auto result = search_joint_representation_constraints(input, options,
      [&](const LayoutSearchArtifact& closed) -> std::optional<LayoutSearchArtifact> {
        ++finalized;
        require(build_post_layout_control_flow(closed.items).proved,
                "finalizer must receive a closed CFG");
        require(observe(input) == observe(closed),
                "every publishable candidate must match ROM stack, X1/X2 and results");
        return closed;
      });
  require(result.incumbent && finalized > 0, "joint constraints must produce an executable solution");
  const auto& best = *result.incumbent;
  require(cells(best) < cells(input) - 15,
          "joint selection must beat sharing one 15-cell region alone");
  require(report(best, "joint-register-web-equations") &&
          report(best, "semantic-multi-entry-region-folding") &&
          report(best, "joint-selector-constraint-synthesis") &&
          report(best, "joint-representation-constraint-search"),
          "winning proof must compose allocation, sharing and selector synthesis");
  require(std::any_of(best.optimizations.begin(), best.optimizations.end(),
      [](const auto& item) { return item.name.starts_with("finite-mode-"); }),
      "winning proof must also choose a finite representation");
  require(same_layout_search_artifact(input, original), "search must never mutate its root");
  std::cout << "joint constraints: " << cells(input) << " -> " << cells(best) << " cells\n";

  const auto repeated = search_joint_representation_constraints(input, options);
  require(repeated.incumbent && same_layout_search_artifact(best, *repeated.incumbent),
          "search and final selector words must be deterministic");
  std::size_t finalizer_vetoes = 0;
  const auto vetoed = search_joint_representation_constraints(input,options,
      [&](const LayoutSearchArtifact&) -> std::optional<LayoutSearchArtifact> {
        ++finalizer_vetoes; return std::nullopt;
      });
  require(!vetoed.incumbent && finalizer_vetoes > 0,
          "prepared local proofs never bypass the downstream complete-artifact contract");
  const auto fixed = fixture(true);
  const auto rejected = search_joint_representation_constraints(fixed, options);
  require(!rejected.incumbent,
          "observed selector cannot be silently retuned to close an impossible layout");
  const auto other = fixture(false, true);
  const auto distinct = search_joint_representation_constraints(other, options);
  if (distinct.incumbent)
    require(observe(other) == observe(*distinct.incumbent),
            "different arithmetic must never be folded as equivalent");
  auto raw = input;
  raw.items[1].raw = true;
  require(!search_joint_representation_constraints(raw, options).incumbent,
          "raw instructions must not enter the conditional representation proof");
  TransactionSearchLimits none;
  none.maximum_expansions = 0;
  require(!search_joint_representation_constraints(input, options, {}, none).incumbent,
          "exhausted search must keep the original independent incumbent");
  auto generated = input;
  generated.preloads.front().setup_expression_text = "generated value";
  require(!search_joint_representation_constraints(generated, options).incumbent,
          "literal reports cannot rewrite a generated setup expression");
}
} // namespace

int main() {
  try {
    set_valued_closure_frontier_contracts();
    mutable_selector_keeps_independent_charge_origins();
    deferred_web_recoloring_contracts();
    prepared_joint_proof_input_contracts();
    joint_origin_transport_contracts();
    composed_family_contracts();
    official_selector_domains();
    checkpoint_model_transport_contracts();
    conditional_numeric_entry_rom_fact();
    conditional_region_contracts();
    product_frontier_rom_contracts();
    deferred_selector_contracts();
    deferred_mode_contracts();
    run();
    transaction_growth_budget_contracts();
    product_frontier_contracts();
    selector_allocation_contracts();
    std::cout << "joint constraint search: compiler and ROM contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
