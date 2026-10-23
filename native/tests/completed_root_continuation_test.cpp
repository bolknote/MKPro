#include "mkpro/compiler.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace mkpro {
CompileResult continue_optimizer_layout_root_for_testing(
    const std::string&, CompileResult, const CompileOptions&, const CompileOptions&);
std::optional<CompileOptions> completed_optimizer_options_for_testing(const CompileResult&);
}

namespace {
using namespace mkpro;
using mkpro::tests::require;

CompileResult fixture(bool opaque = false) {
  CompileResult root;
  const auto append = [&](std::initializer_list<int> bytes) {
    for (int byte : bytes) root.items.push_back(MachineItem::op(byte, "independent root"));
  };
  append({0x67, 0x44, 0x53});
  root.items.push_back(MachineItem::address(std::string("project")));
  append({0, 0x44, 2, 0x44, 0x53});
  root.items.push_back(MachineItem::address(std::string("project")));
  // Overwrite every deep stack slot before the public observation, rather
  // than comparing unspecified scratch values from the two helper callers.
  append({0x0d, 0x0e, 0x0e, 0x0e, 0x65, 0x64, 0x50});
  root.items.back().stop_disposition = StopDisposition::Terminal;
  root.items.back().roles.push_back(kTypedDisplayObservationRole);
  root.items.push_back(MachineItem::label("project"));
  append({0x64, 0x31, 0x41, 0x61, 0x44, 0x52});
  if (opaque) root.items[root.items.size() - 2U].roles.push_back("unknown-root-observation");
  root.preloads = {{.register_name = "4", .value = "0"},
      {.register_name = "5", .value = "17"}, {.register_name = "7", .value = "-3"}};
  const auto listing = resolve_machine_items(root.items);
  require(listing.diagnostics.empty(), "independent root resolves");
  root.steps = listing.steps;
  root.implemented = true;
  return root;
}

std::vector<std::string> observe(const CompileResult& root) {
  const auto listing = resolve_machine_items(root.items);
  require(listing.diagnostics.empty(), "continued root resolves");
  std::vector<int> bytes;
  for (const auto& step : listing.steps) bytes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(bytes).diagnostics.empty(), "root fits stock ROM without Rf");
  for (const auto& preload : root.preloads)
    calc.set_register(preload.register_name, preload.value);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(3000, 8).stopped, "root stops in ROM");
  std::vector<std::string> state;
  for (const auto* name : {"X", "Y", "Z", "T", "X1", "4", "5", "7"})
    state.push_back(calc.read_register(name));
  calc.press_sequence({"."});
  state.push_back(calc.read_register("X"));
  return state;
}

void continuation_contracts() {
  const std::string source = "program Independent { loop { halt(0) } }";
  CompileOptions owner;
  owner.analysis = true;
  owner.budget = 999999;
  owner.fast_candidate_search = true;
  owner.alternating_sign_toggle_args = true;
  owner.single_x_expression_helper_entries = true;
  owner.coalesce_copies = true;
  CompileOptions full = owner;
  full.fast_candidate_search = false;
  full.budget = 21;
  const auto original = fixture();
  const auto continued = continue_optimizer_layout_root_for_testing(source, original, owner, full);
  require(original.steps.size() == 23 && continued.implemented && continued.steps.size() == 21,
          "full exploration continues an emitted bounded root: 23 -> 21");
  require(observe(original) == observe(continued), "continued root preserves complete ROM observations");
  const auto context = completed_optimizer_options_for_testing(continued);
  require(context && !context->fast_candidate_search && context->coalesce_copies &&
          context->alternating_sign_toggle_args && context->single_x_expression_helper_entries,
          "continuation changes exploration policy, never the owning lowering choices");
  const auto trace = std::find_if(continued.candidates.begin(), continued.candidates.end(),
      [](const auto& candidate) { return candidate.site == "optimizer-search-root-continuation"; });
  require(trace != continued.candidates.end() && trace->selected && trace->variant == "completed-IR",
          "continued IR and its actual final price appear in public reports");

  auto expanded = full;
  expanded.feature_profile = FeatureProfile::Mk61SMiniExpanded;
  const auto incompatible = continue_optimizer_layout_root_for_testing(source, original, owner, expanded);
  require(incompatible.steps.size() == original.steps.size() &&
          incompatible.candidates.back().variant == "rejected-IR",
          "same-model continuation cannot reinterpret stock cells as an expanded image");
  auto tight = full;
  tight.analysis = false;
  tight.budget = 20;
  const auto over_budget = continue_optimizer_layout_root_for_testing(source, original, owner, tight);
  require(over_budget.steps.size() == original.steps.size(), "a 21-cell closure cannot pass a 20-cell budget");
  const auto opaque = fixture(true);
  const auto rejected = continue_optimizer_layout_root_for_testing(source, opaque, owner, full);
  require(rejected.steps.size() == opaque.steps.size(), "unknown observation obligations cannot be erased");
  require(observe(opaque) == observe(rejected), "a rejected candidate retains the independent root");
}

void completed_cache_contracts() {
  const std::string source = R"(program RootOwner {
    state { value: packed = 3 }
    loop {
      show(value)
      value = entered()
      value = abs(value)
    }
  })";
  for (const auto profile : {FeatureProfile::Standard, FeatureProfile::Mk61SMiniExpanded}) {
    CompileOptions options;
    options.analysis = true;
    options.budget = 999999;
    options.feature_profile = profile;
    const auto first = compile_source(source, options);
    const auto cached = compile_source(source, options);
    const auto owner = completed_optimizer_options_for_testing(first);
    const auto cached_owner = completed_optimizer_options_for_testing(cached);
    require(first.implemented && cached.implemented && owner && cached_owner,
            "public completion and cache hits retain exact IR ownership");
    require(owner->feature_profile == profile && cached_owner->feature_profile == profile &&
            owner->fast_candidate_search == cached_owner->fast_candidate_search,
            "cached ownership belongs to the delivered profile and original search policy");
  }
}
}

int main() {
  try {
    continuation_contracts();
    completed_cache_contracts();
    std::cout << "completed root continuation and ROM contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
