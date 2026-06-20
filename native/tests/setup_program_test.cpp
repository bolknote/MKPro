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
}

} // namespace mkpro::tests
