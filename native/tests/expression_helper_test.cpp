#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>

namespace mkpro::tests {

namespace {

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) { return item.name == name; });
}

int count_optimization(const CompileResult& result, const std::string& name) {
  return static_cast<int>(
      std::count_if(result.optimizations.begin(), result.optimizations.end(),
                    [&](const OptimizationReport& item) { return item.name == name; }));
}

int count_steps_with_opcode_and_comment_prefix(const CompileResult& result, int opcode,
                                               const std::string& prefix) {
  return static_cast<int>(
      std::count_if(result.steps.begin(), result.steps.end(), [&](const ResolvedStep& step) {
        return step.opcode == opcode && step.comment.has_value() &&
               step.comment->rfind(prefix, 0) == 0;
      }));
}

const SizeHelperSummaryReport* find_size_helper(const CompileResult& result,
                                                const std::string& label) {
  const auto it = std::find_if(
      result.size_attribution.helpers.begin(), result.size_attribution.helpers.end(),
      [&](const SizeHelperSummaryReport& helper) { return helper.label == label; });
  return it == result.size_attribution.helpers.end() ? nullptr : &*it;
}

// Pin the shared-helper / direct-call (ПП) structure this suite verifies by
// suppressing the default-on aggressive post-layout indirect-flow repacking.
CompileOptions pinned_options() {
  CompileOptions options;
  options.disable_aggressive_post_layout = true;
  return options;
}

} // namespace

void expression_helpers_match_typescript_contract() {
  const CompileResult result = compile_source(R"mkpro(
program RepeatedExpression {
  state {
    pos: packed = 23
    map: packed = 123456789
    a: packed = 0
    b: packed = 0
    c: packed = 0
  }
  loop {
    a = digit_at(map, pos - int(pos / 10) * 10)
    b = digit_at(map, pos - int(pos / 10) * 10)
    c = digit_at(map, pos - int(pos / 10) * 10)
    halt(a + b + c)
  }
}
)mkpro",
                                              pinned_options());

  require(result.implemented, "native compiler should lower repeated pure expressions");
  for (const Diagnostic& diagnostic : result.diagnostics) {
    require(diagnostic.severity != DiagnosticSeverity::Error,
            "repeated expression compile should not report errors: " + diagnostic.message);
  }
  require(has_optimization(result, "expression-helper"),
          "repeated pure expression should emit the TS expression helper body");
  require(count_optimization(result, "expression-helper-call") >= 1,
          "repeated pure expression should report helper-call optimizations");
  require(count_steps_with_opcode_and_comment_prefix(result, 0x53, "expr ") >= 1,
          "repeated pure expression should call the shared expression helper");
  require(count_steps_with_opcode_and_comment_prefix(result, 0x52, "expression helper return") == 1,
          "repeated pure expression should emit exactly one helper return");
}

void expression_helper_size_report_counts_entry_y_materialization_coverage() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999;
  options.disable_candidate_search = true;
  options.disable_interprocedural_opts = true;
  const CompileResult result = compile_source(R"mkpro(
program EntrySlotCoverage {
  state {
    out: counter 0..999 = 0
  }
  fn mix(x, y) {
    return x + x + y
  }
  loop {
    x = read()
    y = read()
    out = mix(x, y)
    out += mix(x, y)
    halt(out)
  }
}
)mkpro",
                                              options);

  require(result.implemented, "entry-slot materialization fixture should compile");
  for (const Diagnostic& diagnostic : result.diagnostics) {
    require(diagnostic.severity != DiagnosticSeverity::Error,
            "entry-slot materialization fixture should not report errors: " +
                diagnostic.message);
  }

  const SizeHelperSummaryReport* mix = find_size_helper(result, "mix");
  require(mix != nullptr, "size report should include the value function helper");
  require(mix->details.contains("valueAwareSymbolicEntryStackByCallSite") &&
              mix->details.at("valueAwareSymbolicEntryStackByCallSite")
                      .find("X=y,Y=x") != std::string::npos,
          "fixture should prove the value function entry stack has caller values in X/Y");
  require(mix->details.contains("valueAwareExistingEntryStackInputSites") &&
              mix->details.at("valueAwareExistingEntryStackInputSites")
                      .find("y@call08<-entry-X") != std::string::npos &&
              mix->details.at("valueAwareExistingEntryStackInputSites")
                      .find("x@call08<-entry-Y") != std::string::npos,
          "materialization coverage should count non-X entry stack slots");
  require(mix->details.contains("valueAwareStackInputMaterializeCellsByName") &&
              mix->details.at("valueAwareStackInputMaterializeCellsByName") ==
                  "x:0m/0e/2c;y:0m/0e/2c",
          "entry-X/Y coverage should remove callsite materialization from both inputs; actual=" +
              (mix->details.contains("valueAwareStackInputMaterializeCellsByName")
                   ? mix->details.at("valueAwareStackInputMaterializeCellsByName")
                   : std::string{"<missing>"}));
  require(mix->details.contains("valueAwareStackInputProfitBreakdown") &&
              mix->details.at("valueAwareStackInputProfitBreakdown") ==
                  "x:1g/0m/+1n;y:1g/0m/+1n",
          "entry-X/Y coverage should expose positive stack-input savings");
  require(mix->details.contains("valueAwareEstimatedNetSavingsAfterMaterialization") &&
              mix->details.at("valueAwareEstimatedNetSavingsAfterMaterialization") == "2",
          "entry-X/Y coverage should carry through to the value-aware net estimate");
}

void expression_helper_size_report_tracks_selected_stack_carried_pow10_index() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999999;
  options.disable_candidate_search = true;
  const CompileResult result = compile_source(R"mkpro(
program SelectedStackCarriedPow10 {
  state {
    lines: packed[4..7] = [44444.4, 44444.4, 44444.4, 44444.4]
    slot: counter 0..8 = 8
    best_score: packed = -1
    line: packed = 0
    x: counter 0..5 = 3
    y: counter 0..5 = 2
  }

  fn mark_one() {
    slot--
    lines[slot] = packed_add(lines[slot], line, best_score)
    report = bit_and(lines[slot], 88888834)
    if frac(report) != 0 {
      halt(report)
    }
  }

  loop {
    slot = 8
    line = x
    mark_one()
    line = y
    mark_one()
    halt(lines[7])
  }
}
)mkpro",
                                            options);

  require(result.implemented, "stack-carried pow10 fixture should compile");
  for (const Diagnostic& diagnostic : result.diagnostics) {
    require(diagnostic.severity != DiagnosticSeverity::Error,
            "stack-carried pow10 fixture should not report errors: " +
                diagnostic.message);
  }
  require(has_optimization(result, "predecrement-indexed-stacked-value-update"),
          "fixture should select the proved predecrement stacked-value lowering");
  require(count_optimization(result, "predecrement-indexed-stacked-value-preload") == 2,
          "every proved call site should emit its indexed-value preload");
  require(has_optimization(result, "selected-stack-carried-pow10-index"),
          "selected stack-carried pow10 path should be reported as an optimization");

  const SizeHelperSummaryReport* mark_one = find_size_helper(result, "mark_one");
  require(mark_one != nullptr, "size report should include mark_one helper");
  require(mark_one->details.contains("valueAwareSelectedStackCarriedPlan") &&
              mark_one->details.at("valueAwareSelectedStackCarriedPlan") ==
                  "stack-carried-pow10-index",
          "mark_one should report the selected stack-carried pow10 plan");
  require(mark_one->details.contains("valueAwareSelectedStackCarriedStatus") &&
              mark_one->details.at("valueAwareSelectedStackCarriedStatus") ==
                  "selected-stack-carried-helper-input",
          "mark_one should distinguish a selected helper input from a rejected scheduler "
          "candidate");
  require(mark_one->details.contains("valueAwareSelectedStackCarriedInputNames") &&
              mark_one->details.at("valueAwareSelectedStackCarriedInputNames") == "line",
          "mark_one should report line as the stack-carried pow10 input");
  require(mark_one->details.contains("valueAwareSelectedStackCarriedSelectorNames") &&
              mark_one->details.at("valueAwareSelectedStackCarriedSelectorNames") == "slot",
          "mark_one should report the indirect packed selector");
  require(mark_one->details.contains("valueAwareSelectedStackCarriedTargets") &&
              mark_one->details.at("valueAwareSelectedStackCarriedTargets") == "lines",
          "mark_one should report the packed bank target");
  require(mark_one->details.contains("valueAwareSelectedStackCarriedSites") &&
              mark_one->details.at("valueAwareSelectedStackCarriedSites")
                      .find("input=line,selector=slot,target=lines,slot=X/Y") !=
                  std::string::npos &&
              mark_one->details.at("valueAwareSelectedStackCarriedSites")
                      .find("action=stacked-old-value-through-predecrement-store") !=
                  std::string::npos,
          "mark_one should report the selected site, stack slot, and action");
}

} // namespace mkpro::tests
