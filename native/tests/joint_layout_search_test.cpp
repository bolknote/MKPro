#include "mkpro/core/layout_search_artifact.hpp"
#include "mkpro/core/search_frontier.hpp"

#include <iostream>
#include <map>
#include <stdexcept>

using namespace mkpro;
using namespace mkpro::core;

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
struct State {
  std::string name;
  int cells = 0;
  bool closed = true;
  bool operator==(const State&) const = default;
};
using Edge = ProvedSearchTransition<State>;
using Graph = std::map<std::string, std::vector<Edge>>;
auto run(const Graph& graph, std::vector<Edge> seeds,
         TransactionSearchLimits limits = {}, std::vector<std::string>* visits = nullptr) {
  return search_proved_transactions(
      std::move(seeds), std::vector<State>{}, 20, limits,
      [](const State& value) { return value.cells; },
      [](const State& a, const State& b) { return a == b; },
      [&](const State& parent) {
        const auto found = graph.find(parent.name);
        return found == graph.end() ? std::vector<Edge>{} : found->second;
      },
      [&](const State& value, const std::vector<std::string>&) -> std::optional<State> {
        if (visits) visits->push_back(value.name);
        return value.closed ? std::optional<State>{value} : std::nullopt;
      });
}
void chain_tests() {
  for (const auto& order : {std::vector<std::string>{"reassign", "literal"},
                             std::vector<std::string>{"literal", "reassign"}}) {
    Graph graph{
      {"root", {{{"temporary", 22, false}, order[0], true}}},
      {"temporary", {{{"neutral", 22, false}, "layout", true}}},
      {"neutral", {{{"winner", 17, true}, order[1], true}}}
    };
    const auto result = run(graph, {{{"root", 20, true}, "root", true}});
    require(result.incumbent && result.incumbent->name == "winner",
            "both pass orders must compose through larger and neutral states");
    require(result.expanded == 4, "a finalized identity must not be expanded twice");
  }
  const Graph forbidden{
    {"root", {{{"unproved", 10, true}, "unsound", false},
               {{"incomplete", 9, false}, "valid-but-not-closed", true},
               {{"too-large", 25, true}, "excess-debt", true}}}
  };
  const auto rejected = run(forbidden, {{{"root", 20, true}, "root", true}});
  require(!rejected.incumbent && rejected.rejected == 2,
          "shorter but unproved/unclosed candidates must not be published");
}
void fairness_and_budget_tests() {
  const Graph graph{
    {"cheap", {{{"cheap2", 20, true}, "cheap", true}}},
    {"cheap2", {{{"cheap3", 20, true}, "cheap", true}}},
    {"debt", {{{"winner", 16, true}, "repayment", true}}},
    {"winner", {{{"debt", 22, true}, "cycle", true}}}
  };
  TransactionSearchLimits limits;
  limits.maximum_expansions = 4;
  const std::vector<Edge> seeds{{{"cheap", 20, true}, "cheap-family", true},
                                 {{"debt", 22, true}, "enabling-family", true}};
  std::vector<std::string> visits;
  const auto result = run(graph, seeds, limits, &visits);
  require(result.incumbent && result.incumbent->cells == 16,
          "a larger seed family must not starve behind cheaper neutral states");
  require(visits == std::vector<std::string>{"cheap", "debt", "cheap2", "winner"},
          "family scheduling must be deterministic");
  require(result.duplicate > 0 && result.expanded == 4 && result.budget_exhausted,
          "cycles must deduplicate and expansion budget must be hard");
  std::vector<std::string> repeated;
  run(graph, seeds, limits, &repeated);
  require(visits == repeated, "repeated search must visit the same identities");
  limits.maximum_candidates = 1;
  const auto capped = run(graph, seeds, limits);
  require(capped.admitted == 1 && capped.expanded == 1 && capped.budget_exhausted,
          "candidate budget must bound retained history as well as the queue");
  limits.maximum_candidates = 48;
  limits.maximum_depth = 1;
  const auto shallow = run(graph, seeds, limits);
  require(!shallow.incumbent && shallow.expanded == 2,
          "maximum depth must prevent additional enabling transitions");
  limits.maximum_expansions = 0;
  require(run(graph, seeds, limits).expanded == 0, "zero expansion budget must do no work");
}
void incumbent_and_completion_tests() {
  const Graph graph{
    {"early", {{{"debt", 23, false}, "enable", true}}},
    {"debt", {{{"best", 14, true}, "repay", true}}}
  };
  const auto result = run(graph, {{{"early", 18, true}, "root", true}});
  require(result.incumbent && result.incumbent->cells == 14,
          "a smaller incumbent must not shrink the original debt allowance");
  const std::vector<State> completed{{"root", 20, true}};
  int expanded = 0;
  const auto duplicate = search_proved_transactions(
      std::vector<Edge>{{{"root", 20, true}, "root", true}}, completed, 20, {},
      [](const State& value) { return value.cells; },
      [](const State& a, const State& b) { return a == b; },
      [&](const State&) { ++expanded; return std::vector<Edge>{}; },
      [](const State&, const std::vector<std::string>&) { return std::optional<State>{}; });
  require(expanded == 0 && duplicate.duplicate == 1,
          "already completed first-generation roots must not be repeated");
}
void rebound_admission_tests() {
  LayoutSearchArtifact input;
  auto high = MachineItem::op(0, "0");
  high.roles.push_back(make_late_bound_decimal_selector_role(
      LateBoundDecimalSelectorPart::High, "leaf"));
  auto low = MachineItem::op(0, "0");
  low.roles.push_back(make_late_bound_decimal_selector_role(
      LateBoundDecimalSelectorPart::Low, "leaf"));
  input.items = {high, low, MachineItem::op(0x4a, "store")};
  while (input.items.size() < 10U)
    input.items.push_back(MachineItem::op(0x54, "nop"));
  input.items.push_back(MachineItem::label("leaf"));
  input.items.push_back(MachineItem::op(0x52, "return"));
  input.preloads.push_back({.register_name = "8", .value = "17"});
  input.optimizations.push_back({"proved-layout", "fixture history"});
  const auto rebound = rebind_layout_search_artifact(input);
  require(rebound && rebound->items[0].opcode == 1 && rebound->items[1].opcode == 0,
          "a successful neutral permutation must enter the composition frontier");
  require(rebound->preloads.size() == 1U && rebound->preloads[0].value == "17" &&
              rebound->optimizations.size() == 1U,
          "selector rebinding must retain the rest of the proved transaction");
  auto invalid = input;
  invalid.items[1].roles.clear();
  require(!rebind_layout_search_artifact(invalid),
          "a broken selector pair must never be admitted as an enabling layout");
  invalid = input;
  invalid.items[10].name = "different";
  require(!rebind_layout_search_artifact(invalid),
          "missing selector destination must reject the whole artifact");
  invalid = input;
  invalid.items.insert(invalid.items.begin() + 10, 90,
                       MachineItem::op(0x54, "nop"));
  require(!rebind_layout_search_artifact(invalid),
          "an out-of-range selector must reject even an otherwise proved permutation");
}

void artifact_identity_tests() {
  LayoutSearchArtifact original;
  original.items = {MachineItem::label("entry"), MachineItem::op(0x50, "stop")};
  PreloadReport preload;
  preload.register_name = "a";
  preload.value = "26";
  preload.lowered_data_value = "26";
  original.preloads.push_back(preload);
  auto changed = original;
  changed.optimizations.push_back({"history", "different search path"});
  require(same_layout_search_artifact(original, changed),
          "explanation history must not make duplicate executable states");
  const auto check = [&](const auto& mutate) {
    auto other = original;
    mutate(other);
    require(!same_layout_search_artifact(original, other),
            "proof/observation/preload identities must never be merged by bytes alone");
  };
  check([](auto& value) { value.items[0].roles.push_back("fixed-address:00"); });
  check([](auto& value) { value.items[1].stop_disposition = StopDisposition::Resumable; });
  check([](auto& value) { value.items[1].manual_interaction = ManualInteractionAnchor{}; });
  check([](auto& value) { value.items[1].indirect_flow_targets = std::vector<IrTarget>{1}; });
  check([](auto& value) { value.items[1].indirect_flow_formal_targets = std::vector<int>{0xee}; });
  check([](auto& value) { value.items[1].indirect_memory_targets = std::vector<int>{2}; });
  check([](auto& value) { value.items[1].borrowed_entry_phase_selector = true; });
  check([](auto& value) { value.items[1].semantic_call_origins = {42}; });
  check([](auto& value) { value.preloads[0].value = "27"; });
  check([](auto& value) { value.preloads[0].lowered_data_value.reset(); });
  check([](auto& value) { value.preloads[0].retunable_natural_fractional_prefix = "0.25"; });
  check([](auto& value) { value.preloads[0].setup_expression = true; });
}
} // namespace

int main() {
  try {
    chain_tests();
    fairness_and_budget_tests();
    incumbent_and_completion_tests();
    rebound_admission_tests();
    artifact_identity_tests();
    std::cout << "joint layout transaction search tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
