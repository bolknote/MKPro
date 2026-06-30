#include "mkpro/core/emit/lowering/proc_raw_setup.hpp"

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
}

} // namespace mkpro::tests
