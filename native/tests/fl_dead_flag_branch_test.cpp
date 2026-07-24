#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) { return item.name == name; });
}

std::vector<int> opcodes(const CompileResult& result) {
  std::vector<int> result_opcodes;
  result_opcodes.reserve(result.steps.size());
  for (const ResolvedStep& step : result.steps)
    result_opcodes.push_back(step.opcode);
  return result_opcodes;
}

CompileResult compile_probe_source(const std::string& source) {
  CompileOptions options;
  options.budget = 999;
  options.disable_candidate_search = true;
  return compile_source(source, options);
}

CompileResult compile_dead_flag_probe(const std::string& predicate,
                                      const std::string& register_name,
                                      const std::string& continuation = "") {
  (void)register_name;
  const std::string source =
      "program FlDeadFlagProbe {\n"
      "  state {\n"
      "    flag: flag = 0\n"
      "    result: counter 0..99 = 0\n"
      "  }\n"
      "  loop {\n"
      "    if " +
      predicate +
      " {\n"
      "      result = 11\n"
      "    } else {\n"
      "      result = 22\n"
      "    }\n" +
      continuation +
      "    halt(result)\n"
      "  }\n"
      "}\n";
  return compile_probe_source(source);
}

std::string multi_flag_source(int flag_count) {
  std::string source = "program FlDeadFlagSet {\n  state {\n";
  for (int index = 0; index < flag_count; ++index)
    source += "    flag" + std::to_string(index) + ": flag = 0\n";
  source += "  }\n  loop {\n";
  const std::vector<std::string> predicates = {" == 1", " != 0", " == 0", " != 1"};
  for (int index = 0; index < flag_count; ++index) {
    const std::string name = "flag" + std::to_string(index);
    source += "    if " + name + predicates.at(static_cast<std::size_t>(index) % predicates.size()) +
              " {\n      show(" + std::to_string(10 + index) +
              ")\n    } else {\n      show(" + std::to_string(20 + index) +
              ")\n    }\n    " + name + " = 0\n";
  }
  source += "    halt(0)\n  }\n}\n";
  return source;
}

int physical_register_index(const std::string& name) {
  if (name.size() != 1U)
    return -1;
  if (name.front() >= '0' && name.front() <= '9')
    return name.front() - '0';
  return -1;
}

int fl_opcode_for_register(int index) {
  switch (index) {
  case 0:
    return 0x5d;
  case 1:
    return 0x5b;
  case 2:
    return 0x58;
  case 3:
    return 0x5a;
  default:
    return -1;
  }
}

bool has_dead_flag_report(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& report) {
                       return report.name == "fl-dead-flag-branch" &&
                              report.detail.find(name + " ") != std::string::npos;
                     });
}

int run_dead_flag_probe(const CompileResult& compiled, int flag) {
  emulator::MK61 calculator;
  for (const PreloadReport& preload : compiled.preloads)
    calculator.set_register(preload.register_name, preload.value);
  calculator.set_register(compiled.registers.at("flag"), std::to_string(flag));
  const emulator::ProgramLoadResult loaded = calculator.load_program(opcodes(compiled));
  require(loaded.diagnostics.empty(), "dead-flag probe should load without truncation");
  calculator.press_sequence({"В/О", "С/П"});
  require(calculator.run_until_stable(1000, 5).stopped,
          "dead-flag probe should reach its terminal halt");
  std::string display = calculator.display_text();
  display.erase(std::remove_if(display.begin(), display.end(),
                               [](unsigned char ch) { return ch == ' ' || ch == '\t'; }),
                display.end());
  const std::size_t separator = display.find_first_of(",.");
  require(separator != std::string::npos,
          "dead-flag probe should expose an integer display, got " + display);
  return std::stoi(display.substr(0, separator));
}

} // namespace

void fl_dead_flag_branch_compiler_contract() {
  const CompileResult four_flags = compile_probe_source(multi_flag_source(4));
  require(four_flags.implemented && four_flags.diagnostics.empty(),
          "four-flag FL probe should compile");
  std::string four_flag_reports;
  for (const OptimizationReport& report : four_flags.optimizations) {
    if (report.name == "fl-dead-flag-branch")
      four_flag_reports += "[" + report.detail + "]";
  }
  std::vector<bool> occupied(4, false);
  for (int index = 0; index < 4; ++index) {
    const std::string name = "flag" + std::to_string(index);
    const int physical = physical_register_index(four_flags.registers.at(name));
    require(physical >= 0 && physical <= 3,
            "four simultaneously live flags should occupy R0..R3, but " + name + " uses R" +
                four_flags.registers.at(name));
    occupied.at(static_cast<std::size_t>(physical)) = true;
    require(has_dead_flag_report(four_flags, name),
            "compiler should report dead-flag FL lowering for " + name + ": " +
                four_flag_reports);
    require(std::any_of(four_flags.steps.begin(), four_flags.steps.end(),
                        [&](const ResolvedStep& step) {
                          return step.opcode == fl_opcode_for_register(physical) &&
                                 step.comment == "dead flag branch " + name;
                        }),
            "compiler should emit the FL opcode matching " + name + "'s register");
  }
  require(std::all_of(occupied.begin(), occupied.end(), [](bool value) { return value; }),
          "four-flag probe should exercise every FL register R0..R3");

  const CompileResult five_flags = compile_probe_source(multi_flag_source(5));
  require(five_flags.implemented && five_flags.diagnostics.empty(),
          "five-flag FL-range probe should compile");
  const auto outside = std::find_if(
      five_flags.registers.begin(), five_flags.registers.end(), [](const auto& allocation) {
        return allocation.first.starts_with("flag") &&
               physical_register_index(allocation.second) >= 4;
      });
  require(outside != five_flags.registers.end(),
          "five simultaneously live flags should place one flag outside R0..R3");
  require(!has_dead_flag_report(five_flags, outside->first),
          "a dead boolean state outside R0..R3 must retain ordinary branch lowering");

  const CompileResult live_after_branch =
      compile_dead_flag_probe("flag == 1", "0", "    result += flag\n");
  require(live_after_branch.implemented && live_after_branch.diagnostics.empty(),
          "live dead-flag negative probe should compile normally");
  require(!has_optimization(live_after_branch, "fl-dead-flag-branch"),
          "flag read after the branch must reject destructive FL lowering");

  const CompileResult overwritten_one_path = compile_probe_source(R"mkpro(
program FlDeadFlagOnePath {
  state {
    flag: flag = 0
    result: counter 0..99 = 0
  }
  loop {
    if flag == 1 {
      flag = 0
    } else {
      result = 22
    }
    result += flag
    halt(result)
  }
}
)mkpro");
  require(overwritten_one_path.implemented && overwritten_one_path.diagnostics.empty(),
          "one-path overwrite negative probe should compile normally");
  require(!has_optimization(overwritten_one_path, "fl-dead-flag-branch"),
          "an overwrite on only one path must not prove the old flag dead");
}

void emulator_fl_dead_flag_branch_matches_source_contract() {
  const CompileResult true_when_one =
      compile_dead_flag_probe("flag != 0", "0", "    flag = 0\n");
  require(true_when_one.implemented && true_when_one.diagnostics.empty() &&
              has_optimization(true_when_one, "fl-dead-flag-branch"),
          "true-when-one emulator probe should select FL lowering");
  require(run_dead_flag_probe(true_when_one, 0) == 22,
          "FL dead-flag false path should preserve source behavior");
  require(run_dead_flag_probe(true_when_one, 1) == 11,
          "FL dead-flag true path should preserve source behavior");

  const CompileResult true_when_zero =
      compile_dead_flag_probe("flag == 0", "3", "    flag = 0\n");
  require(true_when_zero.implemented && true_when_zero.diagnostics.empty() &&
              has_optimization(true_when_zero, "fl-dead-flag-branch"),
          "inverted emulator probe should select FL lowering");
  require(run_dead_flag_probe(true_when_zero, 0) == 11,
          "inverted FL dead-flag zero path should preserve source behavior");
  require(run_dead_flag_probe(true_when_zero, 1) == 22,
          "inverted FL dead-flag one path should preserve source behavior");

  const CompileResult live_fallback =
      compile_dead_flag_probe("flag == 1", "0", "    result += flag\n");
  require(live_fallback.implemented && live_fallback.diagnostics.empty() &&
              !has_optimization(live_fallback, "fl-dead-flag-branch"),
          "live-flag emulator probe should retain ordinary comparison lowering");
  require(run_dead_flag_probe(live_fallback, 0) == 22,
          "ordinary false-path fallback should preserve the live flag");
  require(run_dead_flag_probe(live_fallback, 1) == 12,
          "ordinary true-path fallback should preserve the live flag");
}

} // namespace mkpro::tests
