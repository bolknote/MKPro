#include "mkpro/compiler.hpp"
#include "mkpro/core/hardware_allocation_opportunities.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using mkpro::tests::require;

bool reported(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
      [&](const auto& item) { return item.name == name; });
}

void opportunity_tests() {
  auto load = MachineItem::op(0x6c, "constant");
  auto store = MachineItem::op(0x48, "destination");
  store.logical_register_name = "unrelated_result";
  const std::map<std::string, std::string> constants{{"c", "-99999999"}};
  const auto requests = core::hardware_allocation_opportunities({load, store}, constants);
  require(requests.size() == 1 && requests[0].logical_name == "unrelated_result" &&
              requests[0].register_index == 3,
          "hardware opportunity must use logical identity, not a game name or current color");
  require(core::hardware_allocation_opportunities({load, store, load, store}, constants).size() == 1,
          "one logical destination must produce one placement request");
  require(core::hardware_allocation_opportunities({load, store}, {{"c", "99999999"}}).empty(),
          "different constants do not have the same hardware fact");
  store.raw = true;
  require(core::hardware_allocation_opportunities({load, store}, constants).empty(),
          "raw writes cannot request logical relocation");
  store.raw = false;
  store.logical_register_name.reset();
  require(core::hardware_allocation_opportunities({load, store}, constants).empty(),
          "physical-only writes are not a logical allocation request");

  const std::set<std::string> names{"value", "other"};
  const auto pins = core::merge_logical_register_pins({{"other", 2}}, {{"value", 3}}, names, 15);
  require(pins && pins->at("other") == 2 && pins->at("value") == 3,
          "new pin must preserve old hardware anchors");
  require(!core::merge_logical_register_pins({{"value", 2}}, {{"value", 3}}, names, 15),
          "a request cannot override a fixed source or hardware location");
  require(!core::merge_logical_register_pins({}, {{"missing", 3}}, names, 15),
          "a missing logical value cannot silently satisfy a placement request");
  require(!core::merge_logical_register_pins({}, {{"value", 15}}, names, 15),
          "standard MK-61 cannot allocate Rf");
}

const char* source = R"MKPRO(
program Placement {
  state {
    shown: packed = 0
    temporary: packed = 0
  }
  loop {
    show(shown)
    temporary = entered()
    temporary -= 1
    if temporary == 0 {
      shown = -99999999
    } else {
      shown = temporary
    }
  }
}
)MKPRO";

CompileOptions pinned_options(const std::string& reg, bool rematerialize) {
  CompileOptions options;
  options.analysis = true;
  options.general_constant_preloads = true;
  options.zero_underflow_constant_rematerialization = rematerialize;
  options.forced_logical_register_assignments = {{.name = "shown", .register_name = reg}};
  return options;
}

std::vector<std::string> observe(const CompileResult& result) {
  require(result.implemented, "allocation fixture must compile");
  emulator::MK61 calc;
  std::vector<int> codes;
  for (const auto& step : result.steps)
    codes.push_back(step.opcode);
  require(calc.load_program(codes).diagnostics.empty(), "allocation fixture must fit ROM memory");
  for (const auto& preload : result.preloads)
    calc.set_register(preload.register_name, preload.value);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(2000, 8).stopped, "allocation fixture must reach its prompt");
  std::vector<std::string> values{calc.display_text(true)};
  for (const char* input : {"1", "2", "1", "0", "-4"}) {
    calc.input_number(input);
    calc.press("С/П");
    require(calc.run_until_stable(2000, 8).stopped, "allocation fixture must resume and stop");
    values.push_back(calc.display_text(true));
  }
  return values;
}

void compiler_and_rom_tests() {
  auto probe_options = pinned_options("3", false);
  probe_options.collect_logical_register_allocation = true;
  const auto probe = compile_source(source, probe_options);
  require(probe.implemented, "constrained logical probe must compile");
  const auto destination = std::find_if(probe.logical_register_assignments.begin(),
      probe.logical_register_assignments.end(), [](const auto& item) { return item.name == "shown"; });
  require(destination != probe.logical_register_assignments.end() && destination->register_name == "3",
          "logical coloring must honor a requested hardware pin");

  const auto baseline = compile_source(source, pinned_options("8", false));
  const auto optimized = compile_source(source, pinned_options("3", true));
  require(reported(optimized, "zero-underflow-constant-rematerialization"),
          "regenerated hardware destination must enable the actual rematerializer");
  require(observe(baseline) == observe(optimized),
          "allocation/rematerialization must preserve interactive ROM observations");
  require(optimized.steps.size() <= baseline.steps.size(),
          "joint allocation must not add a scratch destination store");

  std::string unknown(source);
  const auto subtract = unknown.find("    temporary -= 1\n");
  require(subtract != std::string::npos, "negative fixture must contain its normalizer");
  unknown.erase(subtract, std::string("    temporary -= 1\n").size());
  const auto rejected = compile_source(unknown, pinned_options("3", true));
  require(rejected.implemented && !reported(rejected, "zero-underflow-constant-rematerialization"),
          "a chosen hardware register is not proof that raw keyboard zero is canonical");
}

void pending_test(const char* path) {
  std::ifstream input(path);
  require(static_cast<bool>(input), "pending regression source must be readable");
  const std::string program((std::istreambuf_iterator<char>(input)), {});
  CompileOptions options;
  options.analysis = true;
  options.budget = 999999;
  const auto result = compile_source(program, options);
  std::cout << "pending joint allocation cells=" << result.steps.size() << '\n';
  require(result.implemented && result.steps.size() < 138U,
          "joint representation/allocation/layout must improve the actual 138-cell regression");
}
} // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2)
      pending_test(argv[1]);
    else {
      opportunity_tests();
      compiler_and_rom_tests();
    }
    std::cout << "hardware allocation cooptimization contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
