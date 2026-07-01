#include "mkpro/core/emit/lowering/proc_raw_setup.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

bool has_setup_optimization_detail(const SetupProgramReport& report, const std::string& name,
                                   const std::string& detail) {
  return std::any_of(report.optimizations.begin(), report.optimizations.end(),
                     [&](const OptimizationReport& item) {
                       return item.name == name && item.detail.find(detail) != std::string::npos;
                     });
}

std::vector<int> step_opcodes(const std::vector<ResolvedStep>& steps) {
  std::vector<int> codes;
  codes.reserve(steps.size());
  for (const ResolvedStep& step : steps)
    codes.push_back(step.opcode);
  return codes;
}

} // namespace

void setup_program_matches_typescript_contract() {
  const CompileOptions options;
  const std::vector<PreloadReport> duplicate_preloads = {
      PreloadReport{.register_name = "7", .value = "42", .counts_against_program = false},
      PreloadReport{.register_name = "8", .value = "42", .counts_against_program = false},
      PreloadReport{.register_name = "7", .value = "42", .counts_against_program = false},
  };

  const SetupProgramReport report = core::emit::lowering::compile_setup_program_with_preloads(
      {}, {}, duplicate_preloads, options);

  require(has_setup_optimization_detail(report, "duplicate-preload-store-reuse", "R7, R8"),
          "duplicate setup preloads should report TS duplicate-preload-store-reuse");
  require(has_setup_optimization_detail(report, "duplicate-preload-register-elision", "R7"),
          "duplicate setup preloads should report TS duplicate-preload-register-elision");

  const std::size_t stores_to_r7 =
      static_cast<std::size_t>(std::count_if(report.steps.begin(), report.steps.end(),
                                            [](const ResolvedStep& step) {
                                              return step.comment == "setup R7";
                                            }));
  require(stores_to_r7 == 1U, "duplicate setup preload should store R7 only once");

  for (const std::string mode : {"rad", "deg", "grd"}) {
    const SetupProgramReport mode_report = core::emit::lowering::compile_setup_program_with_preloads(
        {}, {}, {}, options, mode);
    require(has_setup_optimization_detail(mode_report, "expected-mode-setup-check",
                                          "expected_mode(\"" + mode + "\")"),
            "expected mode setup check should report mode " + mode);
    require(std::any_of(mode_report.steps.begin(), mode_report.steps.end(),
                        [&](const ResolvedStep& step) {
                          return step.opcode == 0x1d && step.comment.has_value() &&
                                 *step.comment == "expected_mode(\"" + mode + "\") cosine";
                        }),
            "expected mode setup check should emit cosine for " + mode);
    require(std::any_of(mode_report.steps.begin(), mode_report.steps.end(),
                        [&](const ResolvedStep& step) {
                          return step.opcode == 0x18 && step.comment.has_value() &&
                                 *step.comment == "expected_mode(\"" + mode + "\") domain guard";
                        }),
            "expected mode setup check should emit domain guard for " + mode);
    const auto first_commented_it =
        std::find_if(mode_report.steps.begin(), mode_report.steps.end(),
                     [](const ResolvedStep& step) {
                       return step.comment.has_value() && !step.comment->empty();
                     });
    require(first_commented_it != mode_report.steps.end() &&
                first_commented_it->comment->starts_with("expected_mode(\"" + mode + "\")"),
            "expected mode setup guard should be first when setup has no input preloads");
  }

  const SetupProgramReport guarded_preload_report =
      core::emit::lowering::compile_setup_program_with_preloads(
          {}, {}, {PreloadReport{.register_name = "1", .value = "100"}}, options, "rad");
  const auto guard_it = std::find_if(guarded_preload_report.steps.begin(),
                                    guarded_preload_report.steps.end(),
                                    [](const ResolvedStep& step) {
                                      return step.comment == "expected_mode(\"rad\") domain guard";
                                    });
  const auto setup_it = std::find_if(guarded_preload_report.steps.begin(),
                                    guarded_preload_report.steps.end(),
                                    [](const ResolvedStep& step) {
                                      return step.comment == "setup R1";
                                    });
  require(guard_it != guarded_preload_report.steps.end() &&
              setup_it != guarded_preload_report.steps.end() && guard_it < setup_it,
          "expected mode setup guard should run before non-input state preload setup");
  require(has_setup_optimization_detail(guarded_preload_report, "expected-mode-setup-check",
                                        "guard with probe 100"),
          "first expected mode setup guard should use its own probe instead of a future preload");

  const SetupProgramReport stack_guarded_preload_report =
      core::emit::lowering::compile_setup_program_with_preloads(
          {},
          {},
          {
              PreloadReport{.register_name = "0", .value = "stack.X"},
              PreloadReport{.register_name = "1", .value = "100"},
          },
          options, "rad");
  const auto stack_setup_it =
      std::find_if(stack_guarded_preload_report.steps.begin(),
                   stack_guarded_preload_report.steps.end(),
                   [](const ResolvedStep& step) { return step.comment == "setup R0"; });
  const auto stack_guard_it =
      std::find_if(stack_guarded_preload_report.steps.begin(),
                   stack_guarded_preload_report.steps.end(), [](const ResolvedStep& step) {
                     return step.comment == "expected_mode(\"rad\") domain guard";
                   });
  const auto numeric_setup_it =
      std::find_if(stack_guarded_preload_report.steps.begin(),
                   stack_guarded_preload_report.steps.end(),
                   [](const ResolvedStep& step) { return step.comment == "setup R1"; });
  require(stack_setup_it != stack_guarded_preload_report.steps.end() &&
              stack_guard_it != stack_guarded_preload_report.steps.end() &&
              numeric_setup_it != stack_guarded_preload_report.steps.end() &&
              stack_setup_it < stack_guard_it && stack_guard_it < numeric_setup_it,
          "expected mode setup guard should run immediately after stack input setup and before "
          "other state preload setup");

  const SetupProgramReport x2_guarded_preload_report =
      core::emit::lowering::compile_setup_program_with_preloads(
          {},
          {},
          {
              PreloadReport{.register_name = "0", .value = "stack.X2"},
              PreloadReport{.register_name = "1", .value = "stack.X"},
              PreloadReport{.register_name = "2", .value = "100"},
          },
          options, "rad");
  const auto x_store_it =
      std::find_if(x2_guarded_preload_report.steps.begin(),
                   x2_guarded_preload_report.steps.end(),
                   [](const ResolvedStep& step) { return step.comment == "setup R1"; });
  const auto x2_restore_it =
      std::find_if(x2_guarded_preload_report.steps.begin(),
                   x2_guarded_preload_report.steps.end(), [](const ResolvedStep& step) {
                     return step.opcode == 0x0a && step.comment == "setup R0 from stack.X2";
                   });
  const auto x2_store_it =
      std::find_if(x2_guarded_preload_report.steps.begin(),
                   x2_guarded_preload_report.steps.end(),
                   [](const ResolvedStep& step) { return step.comment == "setup R0"; });
  const auto x2_guard_it =
      std::find_if(x2_guarded_preload_report.steps.begin(),
                   x2_guarded_preload_report.steps.end(), [](const ResolvedStep& step) {
                     return step.comment == "expected_mode(\"rad\") domain guard";
                   });
  const auto x2_numeric_setup_it =
      std::find_if(x2_guarded_preload_report.steps.begin(),
                   x2_guarded_preload_report.steps.end(),
                   [](const ResolvedStep& step) { return step.comment == "setup R2"; });
  require(x_store_it != x2_guarded_preload_report.steps.end() &&
              x2_restore_it != x2_guarded_preload_report.steps.end() &&
              x2_store_it != x2_guarded_preload_report.steps.end() &&
              x2_guard_it != x2_guarded_preload_report.steps.end() &&
              x2_numeric_setup_it != x2_guarded_preload_report.steps.end() &&
              x_store_it < x2_restore_it && x2_restore_it < x2_store_it &&
              x2_store_it < x2_guard_it && x2_guard_it < x2_numeric_setup_it,
          "stack.X2 setup should run after stack.X storage and before mode/numeric setup");

  const SetupProgramReport full_stack_report =
      core::emit::lowering::compile_setup_program_with_preloads(
          {},
          {},
          {
              PreloadReport{.register_name = "0", .value = "stack.X"},
              PreloadReport{.register_name = "1", .value = "stack.Y"},
              PreloadReport{.register_name = "2", .value = "stack.Z"},
              PreloadReport{.register_name = "3", .value = "stack.T"},
              PreloadReport{.register_name = "5", .value = "99"},
          },
          options);
  emulator::MK61 calc;
  calc.set_register("x", "11");
  calc.set_register("y", "22");
  calc.set_register("z", "33");
  calc.set_register("t", "44");
  calc.load_program(step_opcodes(full_stack_report.steps));
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult full_stack_run = calc.run_until_stable(500, 5);
  require(full_stack_run.stopped, "full stack setup program should stop");
  const std::string stored_x = calc.read_register("0");
  const std::string stored_y = calc.read_register("1");
  const std::string stored_z = calc.read_register("2");
  const std::string stored_t = calc.read_register("3");
  require(stored_x == "11,", "stack.X setup should store initial X, got " + stored_x);
  require(stored_y == "22,", "stack.Y setup should store initial Y, got " + stored_y);
  require(stored_z == "33,", "stack.Z setup should store initial Z, got " + stored_z);
  require(stored_t == "44,", "stack.T setup should store initial T, got " + stored_t);
  require(calc.read_register("5") == "99,", "numeric setup should still run after stack setup");
}

} // namespace mkpro::tests
