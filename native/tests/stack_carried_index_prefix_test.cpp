#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace mkpro::tests {

void compiler_stack_carried_index_update_prefix_preserves_continuation() {
  struct Case {
    std::string tail;
    std::string helper;
    bool decrement;
    bool stack_only;
    int expected;
  };
  const std::vector<Case> cases = {
      {"seen = bank[selector] + 3\n", "", true, true, 113},
      {"seen = bank[selector] + 3\n", "", false, true, 113},
      {"if bank[selector] > 105 {\n seen = 17\n }\n else {\n seen = 19\n }\n",
       "", true, true, 17},
      {"if bank[selector] > 115 {\n seen = 17\n }\n else {\n seen = 19\n }\n",
       "", true, true, 19},
      {"seen = index\n", "", true, false, 2},
      {"remember()\n", "fn remember() {\n seen = index\n }\n", true, false, 2},
  };
  const auto require = [](bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
  };
  for (const auto& test : cases) {
    const std::string source =
        "program IndexUpdatePrefix {\n"
        "  state {\n"
        "    bank: packed[4..7] = [100, 100, 100, 100]\n"
        "    index: packed\n"
        "    selector: packed = " + std::string(test.decrement ? "8" : "7") + "\n"
        "    coefficient: packed = 1\n"
        "    seen: packed = 0\n"
        "  }\n"
        "  index = 2\n"
        "  update()\n"
        "  halt(seen)\n"
        "  fn update() {\n" + std::string(test.decrement ? "selector--\n" : "") +
        "    bank[selector] = packed_add(bank[selector], index, coefficient / 10)\n" +
        test.tail + "  }\n" + test.helper + "}\n";
    CompileOptions options;
    options.analysis = true;
    options.budget = 105;
    options.disable_candidate_search = true;
    options.hoist_procs = true;
    options.x_param_value_functions = true;
    const auto result = compile_source(source, options);
    std::string diagnostics;
    for (const auto& diagnostic : result.diagnostics)
      diagnostics += diagnostic.message + "; ";
    require(result.implemented && result.diagnostics.empty(),
            "indexed update continuation must compile (" + test.tail + test.helper + "): " + diagnostics);
    const bool forwarded = std::any_of(result.optimizations.begin(), result.optimizations.end(),
                                      [](const auto& report) {
      return report.name == "stack-carried-index-update-prefix";
    });
    require(forwarded == test.stack_only,
            "only a dead index may be consumed before the continuation: " + test.tail);
    require(result.steps.size() <= 105U, "indexed update closure must fit the real emulator");
    std::vector<int> opcodes;
    for (const auto& step : result.steps) opcodes.push_back(step.opcode);
    emulator::MK61 calc({.extended = true, .angle_mode = "deg"});
    require(calc.load_program(opcodes).diagnostics.empty(), "indexed update closure must load");
    for (const auto& preload : result.preloads)
      calc.set_register(preload.register_name, preload.value);
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(12000, 8).stopped, "indexed update closure must stop");
    require(std::stoi(calc.display_text()) == test.expected,
            "indexed update continuation result changed: " + calc.display_text());
    require(std::stoi(calc.read_register("7")) == 110, "indexed update selected the wrong bank");
    for (const auto* reg : {"4", "5", "6"})
      require(std::stoi(calc.read_register(reg)) == 100, "indexed update damaged another bank");
  }

  struct PreloadedCase {
    std::string tail;
    std::string helper;
    std::string coefficient;
    bool preloaded;
    double expected;
    int bank6;
    int bank7;
    int selector;
  };
  const std::vector<PreloadedCase> preloaded_cases = {
      {"seen = bank[selector] + 3\n", "", "coefficient / 10", true, 113, 110, 110, 6},
      {"seen = bank[selector] - 3\n", "", "coefficient / 10", true, 107, 110, 110, 6},
      {"seen = 300 - bank[selector]\n", "", "coefficient / 10", true, 190, 110, 110, 6},
      {"seen = bank[selector] / 2\n", "", "coefficient / 10", true, 55, 110, 110, 6},
      {"seen = 220 / bank[selector]\n", "", "coefficient / 10", true, 2, 110, 110, 6},
      // A direct stock K OR probe of 110 and 3 produces 8.1, not integer 113.
      {"seen = bit_or(bank[selector], 3)\n", "", "coefficient / 10", true, 8.1, 110, 110, 6},
      {"seen = 3\n seen += bank[selector]\n", "", "coefficient / 10",
       true, 113, 110, 110, 6},
      {"if bank[selector] > 105 {\n seen = 17\n }\n else {\n seen = 19\n }\n",
       "", "coefficient / 10", true, 17, 110, 110, 6},
      {"if bank[selector] > 115 {\n seen = 17\n }\n else {\n seen = 19\n }\n",
       "", "coefficient / 10", true, 19, 110, 110, 6},
      {"seen = bank[selector] + 3\n selector = 8\n", "", "coefficient / 10",
       false, 123, 100, 120, 8},
      {"seen = bank[selector] + 3\n reset_selector()\n",
       "fn reset_selector() {\n selector = 8\n }\n", "coefficient / 10",
       false, 123, 100, 120, 8},
      {"seen = bank[selector] + 3\n", "", "selector / 10", false, 163, 160, 170, 6},
      {"seen = index\n", "", "coefficient / 10", false, 2, 110, 110, 6},
  };
  std::string automatic_source;
  std::size_t smallest_explicit = 105U;
  for (std::size_t case_index = 0; case_index < preloaded_cases.size() * 4U; ++case_index) {
    const auto& test = preloaded_cases.at(case_index / 4U);
    const bool allow_preloaded = case_index % 2U != 0;
    const std::string source =
        "program PreloadedUpdateContinuation {\n"
        "  state {\n"
        "    bank: packed[4..7] = [100, 100, 100, 100]\n"
        "    index: packed\n"
        "    selector: packed\n"
        "    coefficient: packed = 1\n"
        "    seen: packed = 0\n"
        "  }\n"
        "  selector = 8\n"
        "  index = 2\n"
        "  update()\n"
        "  index = 2\n"
        "  update()\n"
        "  halt(seen)\n"
        "  fn update() {\n"
        "    selector--\n"
        "    bank[selector] = packed_add(bank[selector], index, " + test.coefficient + ")\n" +
        test.tail + "  }\n" + test.helper + "}\n";
    CompileOptions options;
    options.feature_profile = FeatureProfile::Standard;
    options.analysis = true;
    options.budget = 105;
    options.disable_candidate_search = true;
    options.hoist_procs = true;
    options.x_param_value_functions = true;
    options.preloaded_indexed_update_prefix = allow_preloaded;
    options.cached_expression_operand_forwarding = (case_index & 2U) != 0;
    const auto result = compile_source(source, options);
    std::string diagnostics;
    for (const auto& diagnostic : result.diagnostics)
      diagnostics += diagnostic.message + "; ";
    require(result.implemented && result.diagnostics.empty() && result.steps.size() <= 105U,
            "preloaded continuation must fit stock MK-61: " + diagnostics);
    const bool preloaded = std::any_of(result.optimizations.begin(), result.optimizations.end(),
                                       [](const auto& report) {
      return report.name == "predecrement-indexed-stacked-value-update";
    });
    require(preloaded == (allow_preloaded && test.preloaded),
            "preloaded prefix requires a dead input and an independent single-decrement selector: " +
                test.tail + test.coefficient);
    for (const auto& [name, reg] : result.registers)
      require(reg != "f", "stock preloaded continuation allocated RF for " + name);
    for (const auto& preload : result.preloads)
      require(preload.register_name != "f", "stock continuation preloaded RF");
    std::vector<int> codes;
    for (const auto& step : result.steps) codes.push_back(step.opcode);
    emulator::MK61 calc;
    require(calc.load_program(codes).diagnostics.empty(), "preloaded continuation must load");
    for (const auto& preload : result.preloads)
      calc.set_register(preload.register_name, preload.value);
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(12000, 8).stopped, "preloaded continuation must return and stop");
    std::string display = calc.display_text();
    std::replace(display.begin(), display.end(), ',', '.');
    require(std::stod(display) == test.expected,
            "preloaded continuation changed its report: " + calc.display_text() +
                "; expected=" + std::to_string(test.expected) + "; tail=" + test.tail +
                "; coefficient=" + test.coefficient);
    require(std::stoi(calc.read_register("6")) == test.bank6 &&
                std::stoi(calc.read_register("7")) == test.bank7,
            "preloaded continuation lost an update or used a stale selector");
    require(std::stoi(calc.read_register(result.registers.at("selector"))) == test.selector,
            "preloaded continuation changed the selector's persistent value");
    for (const auto* reg : {"4", "5"})
      require(std::stoi(calc.read_register(reg)) == 100,
              "preloaded continuation damaged an unrelated bank");
    if (case_index < 4U) {
      automatic_source = source;
      smallest_explicit = std::min(smallest_explicit, result.steps.size());
    }
  }
  CompileOptions automatic_options;
  automatic_options.feature_profile = FeatureProfile::Standard;
  automatic_options.analysis = true;
  automatic_options.budget = 105;
  const auto automatic = compile_source(automatic_source, automatic_options);
  require(automatic.implemented && automatic.diagnostics.empty() &&
              automatic.steps.size() <= smallest_explicit,
          "automatic ABI selection must preserve the smaller finalized stock candidate");
  for (const auto& [name, reg] : automatic.registers)
    require(reg != "f", "automatic indexed-update ABI allocated RF for " + name);
  for (const auto& preload : automatic.preloads)
    require(preload.register_name != "f", "automatic indexed-update ABI preloaded RF");
}

} // namespace mkpro::tests
