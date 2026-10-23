#include "mkpro/compiler.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;

void emit(std::vector<MachineItem>& items, std::initializer_list<int> bytes) {
  for (const auto byte : bytes) items.push_back(MachineItem::op(byte, "return pooling fixture"));
}

LayoutSearchArtifact fixture(bool fallthrough = false, bool raw = false, bool manual = false) {
  LayoutSearchArtifact result;
  auto& items = result.items;
  emit(items, {0x53});
  items.push_back(MachineItem::address(std::string("outer")));
  emit(items, {0x68, 0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
  items.push_back(MachineItem::label("outer"));
  emit(items, {0x53});
  items.push_back(MachineItem::address(std::string("inner")));
  emit(items, {0x0d, 0x59});
  items.push_back(MachineItem::address(std::string("branch_return")));
  emit(items, {2});
  if (!fallthrough) {
    emit(items, {0x51});
    items.push_back(MachineItem::address(std::string("branch_return")));
  }
  items.push_back(MachineItem::label("branch_return"));
  emit(items, {0x52});
  items.back().raw = raw;
  if (manual) items.back().roles.push_back("unproved-return-observation");
  items.push_back(MachineItem::label("inner"));
  emit(items, {7, 0x52});
  result.preloads.push_back({.register_name="8", .value="17", .lowered_data_value="17"});
  return result;
}

std::vector<std::string> observe(const LayoutSearchArtifact& value) {
  const auto resolved = resolve_machine_items(value.items);
  require(resolved.diagnostics.empty(), "return pooling image resolves");
  std::vector<int> bytes;
  for (const auto& step : resolved.steps) bytes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(bytes).diagnostics.empty(), "return pooling image fits stock ROM");
  for (const auto* name : {"X", "Y", "Z", "T"}) calc.set_register(name, "23");
  for (const auto& word : value.preloads) calc.set_register(word.register_name, word.value);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(2000, 8).stopped, "pooled nested return stops");
  std::vector<std::string> result;
  for (const auto* name : {"X", "Y", "Z", "T", "X1", "8"}) result.push_back(calc.read_register(name));
  calc.press(".");
  result.push_back(calc.display_text(true));
  return result;
}

void search_root_contracts() {
  const std::string source = R"(
program SearchRoots {
  state { value: packed = 3 }
  loop {
    show(value)
    value = entered()
    value = abs(value)
  }
}
)";
  for (const auto profile : {FeatureProfile::Standard, FeatureProfile::Mk61SMiniExpanded}) {
    CompileOptions bounded;
    bounded.feature_profile = profile;
    bounded.analysis = true;
    bounded.budget = 999999;
    const auto fast = compile_source(source, bounded);
    auto wider = bounded;
    wider.fast_candidate_search = false;
    const auto exhaustive = compile_source(source, wider);
    require(fast.implemented && exhaustive.implemented &&
                exhaustive.feature_profile == profile && exhaustive.steps.size() <= fast.steps.size(),
            "wider search preserves the completed bounded root under each exact memory model");
    require(std::any_of(exhaustive.optimizations.begin(), exhaustive.optimizations.end(),
                [](const auto& report) { return report.name == "optimizer-search-root-portfolio"; }),
            "JSON/explain reports the independent root comparison");
    const auto again = compile_source(source, wider);
    require(again.steps.size() == exhaustive.steps.size() && again.preloads.size() == exhaustive.preloads.size(),
            "completed-root cache preserves the selected portfolio result");
  }
  CompileOptions explicit_options;
  explicit_options.fast_candidate_search = false;
  explicit_options.disable_candidate_search = true;
  const auto explicit_result = compile_source(source, explicit_options);
  require(explicit_result.implemented &&
              std::none_of(explicit_result.optimizations.begin(), explicit_result.optimizations.end(),
                  [](const auto& report) { return report.name == "optimizer-search-root-portfolio"; }),
          "an explicitly disabled search does not introduce an automatic root");
}
} // namespace

int main() {
  try {
    CompileOptions options;
    const auto original = fixture();
    const auto proposals = addressed_return_pooling_layout_transactions(original, options);
    require(!proposals.empty(), "independently addressed return shares an existing tail");
    const auto input = resolve_machine_items(original.items);
    for (const auto& proposal : proposals) {
      const auto output = resolve_machine_items(proposal.node.items);
      require(proposal.proved && output.diagnostics.empty() && output.steps.size() < input.steps.size(),
              "only smaller fully closed return layouts are exported");
      require(observe(original) == observe(proposal.node),
              "nested frames, full stack, physical last-X and hidden decimal state remain equal");
    }
    require(addressed_return_pooling_layout_transactions(fixture(true), options).empty(),
            "fallthrough return cannot be removed");
    require(addressed_return_pooling_layout_transactions(fixture(false, true), options).empty(),
            "raw return cannot be aliased");
    require(addressed_return_pooling_layout_transactions(fixture(false, false, true), options).empty(),
            "unknown return observation cannot be discarded");
    search_root_contracts();
    std::cout << "addressed return pooling and nested ROM contracts passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
