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

const SizeOpportunityReport* find_size_opportunity(const CompileResult& result,
                                                   const std::string& variant) {
  const auto it = std::find_if(result.size_attribution.opportunities.begin(),
                               result.size_attribution.opportunities.end(),
                               [&](const SizeOpportunityReport& opportunity) {
                                 return opportunity.variant == variant;
                               });
  return it == result.size_attribution.opportunities.end() ? nullptr : &*it;
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
  options.budget = 999;
  const std::filesystem::path root = std::filesystem::current_path();
  const CompileResult result = compile_source(
      read_text(root / "examples" / "pending-optimizer" / "tic-tac-toe-4x4.mkpro"),
      options);

  require(result.implemented, "tic-tac-toe-4x4 should compile for size-attribution proof");
  require(result.steps.size() == 141,
          "tic-tac-toe-4x4 size should stay stable while reporting entry-stack proof");

  const SizeHelperSummaryReport* candidate_score =
      find_size_helper(result, "candidate_score zero-accumulator entry");
  require(candidate_score != nullptr,
          "size report should include candidate_score zero-accumulator helper");
  const SizeHelperSummaryReport* packed_line_score =
      find_size_helper(result, "packed-line score accumulator helper");
  require(packed_line_score != nullptr,
          "size report should include the selected packed-line score helper");
  require(packed_line_score->details.contains("valueAwareSymbolicEntryStackByCallSite") &&
              packed_line_score->details.at("valueAwareSymbolicEntryStackByCallSite")
                      .find("71:X=x,Y=lines_7,Z=0,T=occupied/"
                            "via=candidate_score zero-accumulator entry") !=
                  std::string::npos &&
              packed_line_score->details.at("valueAwareSymbolicEntryStackByCallSite")
                      .find("74:X=y,Y=lines_6,Z=?,T=occupied/"
                            "via=candidate_score zero-accumulator entry") !=
                  std::string::npos,
          "nested packed-line score calls should use the enclosing helper symbolic stack, "
          "not only the global unknown entry stack");
  require(packed_line_score->details.contains("pipelineShape") &&
              packed_line_score->details.at("pipelineShape") == "packed-line-family-score" &&
              packed_line_score->details.contains("accumulatorStatePolicy") &&
              packed_line_score->details.at("accumulatorStatePolicy") ==
                  "stack-accumulator-no-score-state-store" &&
              packed_line_score->details.contains("interleavedPipelineShape") &&
              packed_line_score->details.at("interleavedPipelineShape") ==
                  "shared-returned-index-tail" &&
              packed_line_score->details.contains("interleavedPipelineBlocker") &&
              packed_line_score->details.at("interleavedPipelineBlocker") ==
                  "live-inputs-cross-stack-mutating-normalizer-and-score-helper" &&
              packed_line_score->details.contains("nextPipelineProofTarget") &&
              packed_line_score->details.at("nextPipelineProofTarget") ==
                  "x-y-residency-through-normalizer-and-accumulator-helper",
          "packed-line score helper report should distinguish solved score accumulation from "
          "the remaining live-input ABI blocker");
  require(candidate_score->details.contains("valueAwareSymbolicEntryStackByCallSite") &&
              candidate_score->details.at("valueAwareSymbolicEntryStackByCallSite") ==
                  "28:X=0,Y=occupied,Z=occupied,T=occupied",
          "candidate_score callsite should prove zero accumulator in X and preserve caller "
          "stack values");
  require(candidate_score->details.contains("valueAwareSymbolicEntryStackSeed") &&
              candidate_score->details.at("valueAwareSymbolicEntryStackSeed") ==
                  "X=0,Y=occupied,Z=occupied,T=occupied",
          "candidate_score merged entry stack should retain the proved stack facts");
  require(candidate_score->details.contains("valueAwareSymbolicEntryStackSeedStatus") &&
              candidate_score->details.at("valueAwareSymbolicEntryStackSeedStatus") ==
                  "proved-uniform-callsite-stack",
          "candidate_score entry-stack proof should model known callee stack effects");
  require(candidate_score->details.contains("valueAwareSymbolicKnownCalleeStackEffects") &&
              candidate_score->details.at("valueAwareSymbolicKnownCalleeStackEffects")
                      .find("71:packed-line score accumulator helper/effect=X:-,Y:T,Z:T,T:T/"
                            "before=X=x,Y=lines_7,Z=0,T=occupied/"
                            "after=X=?,Y=occupied,Z=occupied,T=occupied") !=
                  std::string::npos &&
              candidate_score->details.at("valueAwareSymbolicKnownCalleeStackEffects")
                      .find("74:packed-line score accumulator helper/effect=X:-,Y:T,Z:T,T:T/"
                            "before=X=y,Y=lines_6,Z=?,T=occupied/"
                            "after=X=?,Y=occupied,Z=occupied,T=occupied") !=
                  std::string::npos,
          "candidate_score should apply known nested callee stack effects before scheduler "
          "proofs");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntryModeledPlacementByCallee") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntryModeledPlacementByCallee") ==
                  "packed-line score accumulator helper:4cells(lowerBound=1,"
                  "preCallRewrite=4,unmodelled=0)",
          "candidate_score cost model should include the pre-call rewrite estimate");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntryPreCallPlacementRewriteModel") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntryPreCallPlacementRewriteModel") ==
                  "bounded-stack-search(recall-known-stack-inputs,X<->Y,"
                  "preserve-lower-slots,maxCells=6)",
          "candidate_score pre-call rewrite estimate should come from bounded stack search");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntryPreCallPlacementProofStatus") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntryPreCallPlacementProofStatus") ==
                  "packed-line score accumulator helper:"
                  "conflicting-precall-slot-values-covered-by-bounded-stack-rewrite"
                  "(proved=0,unknown=0,conflict=2,overwritten=0,rewrite=2,"
                  "unmodelledRewrite=0)",
          "candidate_score pre-call proof status should distinguish known conflicts from "
          "unknown stack slots");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntryPreCallPlacementProofAction") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntryPreCallPlacementProofAction") ==
                  "use-modelled-precall-stack-rewrite-cost",
          "candidate_score pre-call proof action should point at the priced rewrite path");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiNaturalFirstRecallChoiceSearchByCallee") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiNaturalFirstRecallChoiceSearchByCallee") ==
                  "packed-line score accumulator helper:2sites/2candidates/2g/2r/"
                  "4rewrite/-4n selected=71:selected=y(net=-2,rewrite=2,"
                  "ops=recall+swap-preserve-X);candidates=y(3future,rewrite=2,"
                  "net=-2,ops=recall+swap-preserve-X),74:selected=x(net=-2,"
                  "rewrite=2,ops=recall+swap-preserve-X);candidates=x(1future,"
                  "rewrite=2,net=-2,ops=recall+swap-preserve-X)",
          "candidate_score report should enumerate survivor placement candidates");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiNaturalFirstRecallChoiceSearchStatusByCallee") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiNaturalFirstRecallChoiceSearchStatusByCallee") ==
                  "packed-line score accumulator helper:"
                  "negative-after-survivor-choice-search(sites=2,candidates=2,"
                  "unmodelled=0)",
          "candidate_score survivor choice search should show no cheaper positive choice");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiNaturalPreservedSlotFinalSlotsByCallee") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiNaturalPreservedSlotFinalSlotsByCallee") ==
                  "packed-line score accumulator helper:T:Y,Z,T",
          "candidate_score report should show where the natural survivor returns");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiNaturalFirstRecallUseShapeByCallee") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiNaturalFirstRecallUseShapeByCallee") ==
                  "packed-line score accumulator helper:survivor=T/finalSlots=T:Y,Z,T/"
                  "return=X:accumulator/nextUse=packed_score-call-argument",
          "candidate_score survivor use-shape should show accumulator return conflict");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiNaturalFirstRecallUseShapeStatus") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiNaturalFirstRecallUseShapeStatus") ==
                  "packed-line score accumulator helper:"
                  "survivor-restores-through-stack-while-accumulator-returns-in-X",
          "candidate_score survivor use-shape should block zero-copy next-call assumptions");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiNaturalFirstRecallUseShapeAction") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiNaturalFirstRecallUseShapeAction") ==
                  "packed-line score accumulator helper:"
                  "model-next-call-argument-use-with-accumulator-preservation-before-lowering",
          "candidate_score survivor use-shape should route the next scheduler work");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotSearchByCallee") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotSearchByCallee")
                      .find("Z:requires-callee-preserve-refactor/2sites/"
                            "2candidates/2rewrite") != std::string::npos &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotSearchByCallee")
                      .find("T:natural-preserved/2sites/2candidates/4rewrite/"
                            "2restore/-4n") != std::string::npos,
          "candidate_score slot search should expose the cheaper Z-shaped ABI candidate");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotSearchStatus") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotSearchStatus") ==
                  "packed-line score accumulator helper:"
                  "lower-placement-slot-requires-callee-preserve-refactor(best=Z:2,"
                  "currentNatural=T:4)+unmodelled-candidates-4",
          "candidate_score slot search status should point at the ABI-shape refactor");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotSearchAction") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotSearchAction") ==
                  "evaluate-callee-abi-shape-that-preserves-cheaper-entry-slot",
          "candidate_score slot search action should route the next optimization step");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeCandidateByCallee") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeCandidateByCallee") ==
                  "packed-line score accumulator helper:slot=Z/preCallRewrite=2/"
                  "calleeShapeLowerBound=1/total=3/currentNatural=T:4/"
                  "callsiteRewriteDelta=-2/roleConflict=selected-slot-overlaps-current-"
                  "accumulator-role/displacedRole=accumulator/displacedRoleTarget=T/"
                  "basis=selected-slot-not-naturally-preserved-by-current-callee-stack-effect",
          "candidate_score slot-shape model should price the cheaper Z candidate");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeCostBreakdown") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeCostBreakdown") ==
                  "gross:5/materialize:2/arg-preserve:2/entry-lower-bound:0/"
                  "slot-shape:3/net:-2/need:3",
          "candidate_score slot-shape cost should include callee-side preservation");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntryNetAfterSlotShapeCells") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntryNetAfterSlotShapeCells") == "-2",
          "candidate_score slot-shape model should improve but remain negative");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeModelStatus") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeModelStatus") ==
                  "negative-after-primary-entry-modeled-placement+improves-current-modeled-"
                  "placement+role-conflict-unproved+body-relocation-blocked",
          "candidate_score slot-shape status should distinguish improvement from profitability");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeRequiredAction") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeRequiredAction") ==
                  "introduce-verified-helper-body-variant-or-find-additional-stack-input-"
                  "savings",
          "candidate_score slot-shape action should not imply the lowering is ready");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeSafeFallbackByCallee") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeSafeFallbackByCallee") ==
                  "packed-line score accumulator helper:slot=T/preCallRewrite=4/"
                  "calleeShapeLowerBound=0/total=4/bodyRelocation=none/"
                  "currentNatural=T:4/basis=non-conflicting-natural-preserved-slot",
          "candidate_score slot-shape report should keep the current non-conflicting "
          "natural slot as the fallback baseline");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeSafeFallbackStatus") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeSafeFallbackStatus") ==
                  "packed-line score accumulator helper:"
                  "current-natural-slot-is-nonconflicting-but-still-negative(total=4)",
          "candidate_score slot-shape fallback should state that the safe current placement "
          "is still not profitable");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeSafeFallbackAction") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeSafeFallbackAction") ==
                  "packed-line score accumulator helper:"
                  "find-additional-stack-input-savings-before-helper-body-variant",
          "candidate_score slot-shape fallback should route work back to generic stack-input "
          "savings");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeRoleRequirementByCallee") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeRoleRequirementByCallee") ==
                  "packed-line score accumulator helper:"
                  "currentBodyRequires=entry-Z-accumulator/"
                  "entryProtocol=X:index,Y:line,Z:accumulator/"
                  "afterDivide=Y:accumulator/finalAddConsumes=Y/"
                  "selectedCandidate=entry-Z/"
                  "conflict=selected-and-accumulator-share-entry-slot",
          "candidate_score slot-shape report should explain why the Z candidate conflicts "
          "with the current accumulator helper body");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeRoleRequirementStatus") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeRoleRequirementStatus") ==
                  "packed-line score accumulator helper:"
                  "current-helper-body-requires-accumulator-in-entry-Z",
          "candidate_score slot-shape report should name the semantic accumulator slot "
          "requirement");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeRoleRequirementAction") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeRoleRequirementAction") ==
                  "prove-cheaper-accumulator-relocation-or-use-non-Z-preserved-slot",
          "candidate_score slot-shape report should point at the next general ABI proof");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeBodyRelocationByCallee") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeBodyRelocationByCallee") ==
                  "packed-line score accumulator helper:slot=Z/start=X=term,Y=selected,"
                  "Z=accumulator,T=accumulator/target=X=term,Y=accumulator,Z|T=selected/"
                  "maxCells=6/result=blocked(ops=swap,reverse,lift)",
          "candidate_score slot-shape body model should prove stack-only relocation is blocked");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeBodyRelocationStatus") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeBodyRelocationStatus") ==
                  "packed-line score accumulator helper:"
                  "no-stack-only-accumulator-relocation-plan(maxCells=6,"
                  "ops=swap,reverse,lift)",
          "candidate_score slot-shape body status should name the missing helper-body plan");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeBodyRelocationModel") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeBodyRelocationModel") ==
                  "role-stack-search(ops=swap,reverse,lift,maxCells=6,scratch=none)",
          "candidate_score slot-shape body proof should use the generic role-stack planner");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeBodyRelocationAction") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeBodyRelocationAction") ==
                  "introduce-verified-helper-body-variant-or-explicit-temp-copy",
          "candidate_score slot-shape body action should require a verified helper variant");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyByCallee") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyByCallee") ==
                  "packed-line score accumulator helper:slot=Z/start=X=term,Y=selected,"
                  "Z=accumulator,T=accumulator/target=X=term,Y=accumulator,Z|T=selected/"
                  "maxCells=8/result=4cells(store-temp+reverse+swap+recall-temp)/"
                  "final=X=term,Y=accumulator,Z=selected,T=accumulator/temp=term",
          "candidate_score explicit-temp model should find the concrete helper-body copy");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyCells") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyCells") == "4",
          "candidate_score explicit-temp model should price the helper-body copy");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyModel") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyModel") ==
                  "role-stack-search(ops=store-temp,reverse,swap,recall-temp,lift,"
                  "maxCells=8,scratch=one-x-copy)",
          "candidate_score explicit-temp proof should use the generic role-stack planner");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyCostBreakdown") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyCostBreakdown") ==
                  "gross:5/materialize:2/arg-preserve:2/entry-lower-bound:0/"
                  "slot-shape-precall:2/helper-body-copy:4/net:-5/need:6",
          "candidate_score explicit-temp cost should supersede the optimistic shape lower "
          "bound");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntryNetAfterSlotShapeExplicitTempCopyCells") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntryNetAfterSlotShapeExplicitTempCopyCells") ==
                  "-5",
          "candidate_score explicit-temp model should be worse than the current placement");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyModelStatus") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyModelStatus") ==
                  "negative-after-explicit-temp-copy-helper-body-variant+"
                  "worse-than-current-modeled-placement",
          "candidate_score explicit-temp status should close the apparent Z-slot opportunity");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyRequiredAction") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyRequiredAction") ==
                  "keep-current-natural-slot-or-find-additional-stack-input-savings-before-"
                  "helper-body-copy",
          "candidate_score explicit-temp action should avoid lowering a worse variant");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntryNetAfterModeledPlacementCells") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntryNetAfterModeledPlacementCells") == "-3",
          "candidate_score modeled placement estimate should not reuse the optimistic lower "
          "bound");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntryModeledPlacementCostBreakdown") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntryModeledPlacementCostBreakdown") ==
                  "gross:5/materialize:2/arg-preserve:2/entry-lower-bound:0/"
                  "modeled-placement:4/net:-3/need:4",
          "candidate_score modeled placement cost should expose the real current blocker");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiPrimaryEntryModeledPlacementRequiredAction") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiPrimaryEntryModeledPlacementRequiredAction") ==
                  "reduce-primary-entry-precall-rewrite-cost-or-find-additional-stack-input-"
                  "savings",
          "candidate_score modeled placement action should point at the pre-call rewrite cost");
  require(candidate_score->details.contains(
              "valueAwareCallArgumentPreservationRawCells") &&
              candidate_score->details.at("valueAwareCallArgumentPreservationRawCells") ==
                  "2",
          "candidate_score argument-preservation report should keep the raw lower bound");
  require(candidate_score->details.contains(
              "valueAwareCallArgumentPreservationZeroCopyCells") &&
              candidate_score->details.at(
                  "valueAwareCallArgumentPreservationZeroCopyCells") == "0",
          "candidate_score should not invent a zero-copy argument-preservation proof");
  require(candidate_score->details.contains(
              "valueAwareCallArgumentPreservationZeroCopyStatus") &&
              candidate_score->details.at(
                  "valueAwareCallArgumentPreservationZeroCopyStatus") ==
                  "no-existing-resident-argument-copy",
          "candidate_score should report that resident argument copies are not already placed");
  require(candidate_score->details.contains(
              "valueAwareCallArgumentPreservationZeroCopyBlockers") &&
              candidate_score->details.at(
                  "valueAwareCallArgumentPreservationZeroCopyBlockers")
                      .find("packed-line score accumulator helper@71:x="
                            "no-existing-resident-copy") != std::string::npos &&
              candidate_score->details.at(
                  "valueAwareCallArgumentPreservationZeroCopyBlockers")
                      .find("packed-line score accumulator helper@74:y="
                            "no-existing-resident-copy") != std::string::npos,
          "candidate_score should name both argument-preservation zero-copy blockers");
  require(candidate_score->details.contains("argumentRecallSitesByName") &&
              candidate_score->details.at("argumentRecallSitesByName") ==
                  "y:3@73,76,80;x:2@70,82;lines_4:1@79;lines_5:1@75;"
                  "lines_6:1@72;lines_7:1@69",
          "candidate_score should report exact helper-local argument recall sites");
  require(candidate_score->details.contains("repeatedArgumentRecallSites") &&
              candidate_score->details.at("repeatedArgumentRecallSites") ==
                  "y:3@73,76,80;x:2@70,82",
          "candidate_score should identify repeated argument recalls for scheduler work");
  require(candidate_score->details.contains("topRepeatedArgumentRecall") &&
              candidate_score->details.at("topRepeatedArgumentRecall") == "y:3@73,76,80",
          "candidate_score should rank y as the next repeated materialization target");
  require(candidate_score->details.contains("schedulerNextMaterializationTarget") &&
              candidate_score->details.at("schedulerNextMaterializationTarget") ==
                  "keep-repeated-helper-argument-resident-across-related-operations",
          "candidate_score should route repeated argument recalls to the value-aware scheduler");
  require(candidate_score->details.contains("repeatedArgumentSchedulerFeasibility") &&
              candidate_score->details.at("repeatedArgumentSchedulerFeasibility") ==
                  "y:3recalls/profitableNet=+0/modelNet=+0/positive=0,breakEven=0,"
                  "negative=0,absent=0,unknown=3/73:not-proved-resident,"
                  "76:not-proved-resident,80:not-proved-resident",
          "candidate_score should model top repeated argument stack-residency feasibility");
  require(candidate_score->details.contains("valueAwareRepeatedArgumentSchedulerResidencyBlockers") &&
              candidate_score->details.at("valueAwareRepeatedArgumentSchedulerResidencyBlockers")
                      .find("73:no-prior-resident-value") != std::string::npos &&
              candidate_score->details.at("valueAwareRepeatedArgumentSchedulerResidencyBlockers")
                      .find("76:lastKnown=74:X") != std::string::npos &&
              candidate_score->details.at("valueAwareRepeatedArgumentSchedulerResidencyBlockers")
                      .find("afterStep=74:") != std::string::npos &&
              candidate_score->details.at("valueAwareRepeatedArgumentSchedulerResidencyBlockers")
                      .find("80:lastKnown=77:X") != std::string::npos &&
              candidate_score->details.at("valueAwareRepeatedArgumentSchedulerResidencyBlockers")
                      .find("lostBefore=79") != std::string::npos &&
              candidate_score->details.at("valueAwareRepeatedArgumentSchedulerResidencyBlockers")
                      .find("afterStep=77:") != std::string::npos,
          "candidate_score should explain why repeated y recalls are not yet stack-resident");
  require(candidate_score->details.contains("repeatedArgumentSchedulerStatus") &&
              candidate_score->details.at("repeatedArgumentSchedulerStatus") ==
                  "missing-stack-residency-proof",
          "candidate_score should not claim a repeated-argument rewrite without residency proof");
  require(candidate_score->details.contains("repeatedArgumentSchedulerAction") &&
              candidate_score->details.at("repeatedArgumentSchedulerAction") ==
                  "prove-stack-survival-through-intervening-ops-or-keep-direct-recalls",
          "candidate_score should point the scheduler at stack-survival proof before lowering");
  require(candidate_score->details.contains("repeatedArgumentSchedulerProfitableNetCells") &&
              candidate_score->details.at("repeatedArgumentSchedulerProfitableNetCells") == "0",
          "candidate_score should report no profitable repeated-argument scheduler cells yet");
  require(candidate_score->details.contains("repeatedArgumentSchedulerModelNetCells") &&
              candidate_score->details.at("repeatedArgumentSchedulerModelNetCells") == "0",
          "candidate_score should report a neutral model net when no resident sites are proved");
  require(candidate_score->details.contains(
              "valueAwareCallArgumentX2PreloadLiteralReplacementByCallee") &&
              candidate_score->details.at(
                  "valueAwareCallArgumentX2PreloadLiteralReplacementByCallee") ==
                  "packed-line score accumulator helper:88:0.41200076/recall=1/"
                  "literal=10/delta=+9/x2=literal-restores/argSavings=2/net=-7",
          "candidate_score should price the rejected literal replacement for the X2-clobbering "
          "preloaded constant");
  require(candidate_score->details.contains(
              "valueAwareCallArgumentX2PreloadLiteralReplacementStatus") &&
              candidate_score->details.at(
                  "valueAwareCallArgumentX2PreloadLiteralReplacementStatus") ==
                  "packed-line score accumulator helper:"
                  "literal-replacement-negative-even-before-x2-proof+literal-x2-restores",
          "candidate_score should explain that replacing the preload recall with a literal is "
          "not the profitable X2 proof");
  require(candidate_score->details.contains(
              "valueAwareCallArgumentX2PreloadLiteralReplacementHypotheticalNetCells") &&
              candidate_score->details.at(
                  "valueAwareCallArgumentX2PreloadLiteralReplacementHypotheticalNetCells") ==
                  "-7",
          "candidate_score should compare literal replacement cost against argument "
          "preservation savings");
  require(candidate_score->details.contains("valueAwareCalleeAbiPositiveLevers") &&
              candidate_score->details.at("valueAwareCalleeAbiPositiveLevers") ==
                  "reduce-callsite-materialization-by-1,"
                  "reduce-call-argument-preservation-by-1,"
                  "elide-callee-entry-overhead-by-1",
          "candidate_score ABI report should expose the nearest general positive levers");
  require(candidate_score->details.contains("valueAwareCalleeAbiPositiveGapReason") &&
              candidate_score->details.at("valueAwareCalleeAbiPositiveGapReason") ==
                  "current-plan-breaks-even-after-materialization-argument-preservation-and-"
                  "callee-entry-lower-bound",
          "candidate_score ABI report should name the remaining positive gap");
  require(candidate_score->details.contains("valueAwareCalleeAbiNextProofTarget") &&
              candidate_score->details.at("valueAwareCalleeAbiNextProofTarget") ==
                  "remove-call-argument-preservation-by-1,"
                  "reduce-callsite-materialization-by-1,"
                  "prove-callee-entry-overhead-below-lower-bound-by-1",
          "candidate_score ABI report should identify the next proof target");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiBestSubsetPositiveLevers") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiBestSubsetPositiveLevers") ==
                  "reduce-callsite-materialization-by-1,"
                  "reduce-call-argument-preservation-by-1,"
                  "elide-callee-entry-overhead-by-1",
          "candidate_score subset ABI report should retain the same nearest levers");
  require(candidate_score->details.contains(
              "valueAwareCalleeAbiBestSubsetNextProofTarget") &&
              candidate_score->details.at(
                  "valueAwareCalleeAbiBestSubsetNextProofTarget") ==
                  "remove-call-argument-preservation-by-1,"
                  "reduce-callsite-materialization-by-1,"
                  "prove-callee-entry-overhead-below-lower-bound-by-1",
          "candidate_score subset ABI report should identify the same next proof target");
  const SizeOpportunityReport* stack_function_entry =
      find_size_opportunity(result, "stack-resident-function-entries");
  require(stack_function_entry != nullptr &&
              stack_function_entry->details.contains("stackResidentEntryDeltaCells") &&
              stack_function_entry->details.at("stackResidentEntryDeltaCells") == "5" &&
              stack_function_entry->details.contains("stackResidentEntryAbiContext") &&
              stack_function_entry->details.at("stackResidentEntryAbiContext")
                      .find("expr=cell_mask(x, y)/net=-9/entryOverhead=11") !=
                  std::string::npos &&
              stack_function_entry->details.contains("stackResidentEntryValueAwareContext") &&
              stack_function_entry->details.at("stackResidentEntryValueAwareContext")
                      .find("candidate_score zero-accumulator entry:"
                            "callee-abi-lower-bound-not-positive") != std::string::npos &&
              stack_function_entry->details.contains("stackResidentEntryRequiredAction") &&
              stack_function_entry->details.at("stackResidentEntryRequiredAction") ==
                  "keep-current-layout-until-stack-entry-overhead-or-callsite-count-improves",
          "stack-resident function entry rejection should point back to the current ABI and "
          "value-aware scheduler blockers");
}

} // namespace mkpro::tests
