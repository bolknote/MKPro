#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
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

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("cannot read fixture: " + path.string());
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
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
                  "x:0m/0e/1c;y:0m/0e/1c",
          "entry-X/Y coverage should remove callsite materialization from both inputs");
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
  require(has_optimization(result, "indexed-packed-y-stack-pow10-delta"),
          "fixture should select the Y-stack pow10 delta lowering");
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
                      .find("input=line,selector=slot,target=lines,slot=Y") !=
                  std::string::npos &&
              mark_one->details.at("valueAwareSelectedStackCarriedSites")
                      .find("action=stack-carried-pow10-index-through-self-decrement") !=
                  std::string::npos,
          "mark_one should report the selected site, stack slot, and action");
}

void expression_helper_size_report_tracks_symbolic_entry_stack() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 105;
  options.fast_candidate_search = true;
  const std::filesystem::path root = std::filesystem::current_path();
  const std::string source =
      read_text(root / "examples" / "pending-optimizer" / "tic-tac-toe-4x4.mkpro");
  const CompileResult result = compile_source(source, options);

  require(result.implemented, "tic-tac-toe-4x4 should compile for size-attribution proof");
  require(result.steps.size() == 120,
          "ordinary generic candidate search should keep the current 120-cell checkpoint, got " +
              std::to_string(result.steps.size()));
  require(has_optimization(result, "joint-packed-line-family-walk"),
          "automatic candidate search should select the reusable packed-line family pass");

  CompileOptions mutating_options;
  mutating_options.analysis = true;
  mutating_options.budget = 999;
  mutating_options.disable_candidate_search = true;
  mutating_options.canonicalize_packed_line_bank_walks = true;
  mutating_options.packed_line_family_mutating_selector_update_check_tail = true;
  mutating_options.stack_resident_temps = true;
  const CompileResult mutating = compile_source(source, mutating_options);
  require(mutating.implemented && mutating.diagnostics.empty(),
          "forced mutating packed-line candidate should compile");
  require(mutating.steps.size() == 140,
          "the pinned mid-level candidate should stay at its independently tested 140-cell "
          "layout, got " +
              std::to_string(mutating.steps.size()));
  require(!mutating.registers.contains("__bank_slot") &&
              !mutating.registers.contains("__bank_selector_lines"),
          "the mutating family rewrite should eliminate its private selector state");
  require(has_optimization(mutating,
                           "packed-line-family-mutating-selector-update-check-tail") &&
              has_optimization(mutating, "packed-line-family-elided-leaf-register-state"),
          "the pinned candidate should report both the walk rewrite and proved state elision");
  require(std::any_of(mutating.steps.begin(), mutating.steps.end(),
                      [](const ResolvedStep& step) {
                        return step.opcode == 0xb3 && step.comment.has_value() &&
                               step.comment->find("indirect-memory-targets=4,5,6,7") !=
                                   std::string::npos;
                      }),
          "mutating packed-bank stores should expose their exact target range to static proof");

  const SizeHelperSummaryReport* candidate_score =
      find_size_helper(mutating, "candidate_score zero-accumulator entry");
  const SizeHelperSummaryReport* packed_line_score =
      find_size_helper(mutating, "packed-line score accumulator helper");
  require(candidate_score != nullptr && packed_line_score != nullptr,
          "the pinned mid-level candidate should retain both score-helper summaries");
  require(candidate_score->details.contains("valueAwareSymbolicEntryStackByCallSite") &&
              candidate_score->details.at("valueAwareSymbolicEntryStackByCallSite")
                      .find("X=0,Y=occupied,Z=occupied,T=occupied") != std::string::npos,
          "candidate_score should retain its proved zero-accumulator call-site stack");
  require(candidate_score->details.contains("valueAwareSymbolicKnownCalleeStackEffects") &&
              candidate_score->details.at("valueAwareSymbolicKnownCalleeStackEffects")
                      .find("packed-line score accumulator helper/effect=X:-,Y:T,Z:T,T:T") !=
                  std::string::npos,
          "candidate_score should account for the nested score helper's stack effect");
  require(packed_line_score->details.contains("valueAwareSymbolicEntryStackByCallSite") &&
              packed_line_score->details.at("valueAwareSymbolicEntryStackByCallSite")
                      .find("X=x,Y=lines_7") != std::string::npos &&
              packed_line_score->details.at("valueAwareSymbolicEntryStackByCallSite")
                      .find("X=y,Y=lines_6") != std::string::npos,
          "score-helper analysis should describe both orthogonal packed-bank call sites without "
          "pinning their physical addresses");
  require(packed_line_score->details.contains("pipelineShape") &&
              packed_line_score->details.at("pipelineShape") ==
                  "packed-line-family-score" &&
              packed_line_score->details.contains("accumulatorStatePolicy") &&
              packed_line_score->details.at("accumulatorStatePolicy") ==
                  "stack-accumulator-no-score-state-store",
          "score-helper analysis should retain its structural pipeline and accumulator policy");

  std::string shared_selector_source = source;
  const std::string show_best = "show(best_y)";
  const std::size_t show_best_pos = shared_selector_source.find(show_best);
  require(show_best_pos != std::string::npos,
          "4x4 fixture should contain the display insertion point");
  shared_selector_source.insert(show_best_pos + show_best.size(),
                                "\n    best_score = lines[slot]");
  const CompileResult shared_selector =
      compile_source(shared_selector_source, mutating_options);
  require(shared_selector.registers.contains("__bank_selector_lines") &&
              !has_optimization(shared_selector,
                                "packed-line-family-elided-leaf-register-state"),
          "a dynamic bank access outside the leaf must make selector-state elision fail closed");
}

} // namespace mkpro::tests
