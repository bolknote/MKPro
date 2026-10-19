#include "mkpro/core/passes/register_coalesce.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <exception>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core::passes;
using mkpro::tests::require;
void edge(RegisterInterferenceGraph& graph, const std::string& a, const std::string& b) {
  graph.neighbors[a].insert(b); graph.neighbors[b].insert(a);
}
void validate(const RegisterInterferenceGraph& graph,
               const PrecoloredRegisterAllocationOptions& options,
               const std::map<std::string, int>& colors) {
  for (const auto& [node, neighbors] : graph.neighbors) {
    require(colors.contains(node), "every logical lifetime receives a register");
    const int color = colors.at(node);
    require(color >= 0 && color < options.color_count, "no invented extra register");
    const auto domain = options.allowed_colors.find(node);
    require(domain == options.allowed_colors.end() || domain->second.contains(color),
            "every hardware instruction domain is retained");
    const auto fixed = options.fixed_colors.find(node);
    require(fixed == options.fixed_colors.end() || fixed->second == color,
            "hardware and entry anchors never move");
    for (const auto& other : neighbors)
      require(colors.at(other) != color, "no overlapping lifetimes share a register");
  }
}
std::vector<std::string> observe(const std::map<std::string, int>& colors) {
  const auto a = colors.at("a"), b = colors.at("b"), c = colors.at("c");
  emulator::MK61 calc;
  require(calc.load_program({4, 0x40 + a, 7, 0x40 + b,
      0x60 + a, 0x60 + b, 0x10, 0x4b, 9, 0x40 + c,
      0x60 + b, 0x60 + c, 0x10, 0x4c, 0x6b, 0x6c, 0x10, 0x50})
              .diagnostics.empty(), "recolored compiler operands fit stock ROM");
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(2000, 8).stopped, "recolored operands finish");
  std::vector<std::string> result{calc.display_text(true)};
  calc.press("."); result.push_back(calc.display_text(true));
  for (const char* name : {"X", "Y", "Z", "T", "X1", "b", "c"})
    result.push_back(calc.read_register(name));
  return result;
}
void run() {
  RegisterInterferenceGraph path;
  edge(path, "a", "b"); edge(path, "b", "c");
  PrecoloredRegisterAllocationOptions options;
  options.color_count = 2;
  options.greedy_only = true;
  options.preferred_colors = {{"a", 1}, {"b", 0}};
  options.allowed_colors["c"] = {0};
  require(!color_precolored_register_graph(path, options), "historical greedy fixture must fail");
  options.maximum_recoloring_work = 1;
  require(!color_precolored_register_graph(path, options), "exhaustion publishes no partial coloring");
  options.maximum_recoloring_work = 4096;
  const auto repaired = color_precolored_register_graph(path, options);
  require(repaired && repaired->at("a") == 0 && repaired->at("b") == 1 && repaired->at("c") == 0,
          "repair must interchange the complete two-value chain, not one conflicting node");
  validate(path, options, *repaired);
  require(observe({{"a", 8}, {"b", 9}, {"c", 10}}) == observe(*repaired),
          "two-register lifetime allocation preserves results, stack, X1 and X2");
  auto blocked = options;
  blocked.fixed_colors["a"] = 1;
  require(!color_precolored_register_graph(path, blocked), "a fixed member blocks the entire chain");
  blocked = options; blocked.allowed_colors["a"] = {1};
  require(!color_precolored_register_graph(path, blocked), "a restricted member blocks the chain");
  require(color_precolored_register_graph(path, options) == repaired, "repair is deterministic");

  // A planted three-register allocation exists, but no legal two-color chain
  // can repair the first MRV/DSATUR choices. This is a graph/list-domain case,
  // independent of source names, arithmetic formulas and program layout.
  RegisterInterferenceGraph lists;
  const std::vector<std::string> adjacency={"cfhj","cdei","abj","bej","bdhj","agj","fhi","aeg","bgj","acdefi"};
  const std::vector<std::string> domains={"02","012","12","02","01","02","012","12","02","012"};
  const std::vector<int> preferences={2,0,2,0,1,0,2,0,2,1};
  PrecoloredRegisterAllocationOptions bounded;bounded.color_count=3;bounded.greedy_only=true;
  bounded.prioritize_constrained_domains=true;
  for(std::size_t i=0;i<adjacency.size();++i){
    const std::string name(1,static_cast<char>('a'+i));lists.neighbors[name];
    for(const char adjacent:adjacency[i])edge(lists,name,std::string(1,adjacent));
    for(const char color:domains[i])bounded.allowed_colors[name].insert(color-'0');
    bounded.preferred_colors[name]=preferences[i];
  }
  require(!color_precolored_register_graph(lists,bounded),"the legacy greedy list-color witness fails");
  bounded.maximum_recoloring_work=1;
  require(!color_precolored_register_graph(lists,bounded),"a bounded search never exports a partially colored graph");
  bounded.maximum_recoloring_work=4096;
  const auto joint=color_precolored_register_graph(lists,bounded);
  require(joint.has_value(),"bounded recoloring must escape a non-Kempe list-color local minimum");
  validate(lists,bounded,*joint);
  require(color_precolored_register_graph(lists,bounded)==joint,"bounded coloring is deterministic");
  auto impossible=bounded;impossible.allowed_colors["a"]={0};impossible.allowed_colors["c"]={0};
  require(!color_precolored_register_graph(lists,impossible),"bounded search never violates a list-domain contradiction");

  // Exhaust every labelled five-node graph. Compare with the independent
  // legacy exact solver; success is always a full witness, failure inconclusive.
  for (unsigned mask = 0; mask < 1024; ++mask) {
    RegisterInterferenceGraph graph;
    unsigned bit = 0;
    for (int a = 0; a < 5; ++a) {
      const auto x = "v" + std::to_string(a);
      graph.neighbors.try_emplace(x);
      for (int b = a + 1; b < 5; ++b, ++bit)
        if ((mask & (1U << bit)) != 0) edge(graph, x, "v" + std::to_string(b));
    }
    PrecoloredRegisterAllocationOptions base;
    base.color_count = 3; base.greedy_only = true;
    for (int a = 0; a < 5; ++a) base.preferred_colors["v" + std::to_string(a)] = a % 3;
    if ((mask & 1U) != 0) base.allowed_colors["v4"] = {0};
    if ((mask & 2U) != 0) base.fixed_colors["v0"] = 1;
    const auto greedy = color_precolored_register_graph(graph, base);
    auto improved = base; improved.maximum_recoloring_work = 4096;
    const auto actual = color_precolored_register_graph(graph, improved);
    if (greedy) require(actual == greedy, "successful historical allocation keeps its exact order");
    if (actual) validate(graph, improved, *actual);
    auto exact = base; exact.greedy_only = false;
    const auto witness = color_precolored_register_graph(graph, exact);
    require(!actual || witness.has_value(), "repair never accepts an uncolorable graph");
    require(color_precolored_register_graph(graph, improved) == actual,
            "graph and work ordering must be reproducible");
  }
}
} // namespace
int main() {
  try { run(); std::cout << "register chain recoloring compiler and ROM contracts passed\n"; return 0; }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
