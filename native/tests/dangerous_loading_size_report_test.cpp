#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace mkpro::tests {

namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("cannot read fixture: " + path.string());
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

bool has_error_diagnostic(const CompileResult& result) {
  return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                     [](const Diagnostic& diagnostic) {
                       return diagnostic.severity == DiagnosticSeverity::Error;
                     });
}

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& optimization) {
                       return optimization.name == name;
                     });
}

const SizeHelperSummaryReport* find_size_helper(const CompileResult& result,
                                                const std::string& label) {
  const auto it = std::find_if(
      result.size_attribution.helpers.begin(), result.size_attribution.helpers.end(),
      [&](const SizeHelperSummaryReport& helper) { return helper.label == label; });
  return it == result.size_attribution.helpers.end() ? nullptr : &*it;
}

const SizeOpportunityReport* find_size_opportunity_detail(const CompileResult& result,
                                                          const std::string& variant,
                                                          const std::string& key,
                                                          const std::string& value) {
  const auto it = std::find_if(result.size_attribution.opportunities.begin(),
                               result.size_attribution.opportunities.end(),
                               [&](const SizeOpportunityReport& opportunity) {
                                 const auto detail = opportunity.details.find(key);
                                 return opportunity.variant == variant &&
                                        detail != opportunity.details.end() &&
                                        detail->second == value;
                               });
  return it == result.size_attribution.opportunities.end() ? nullptr : &*it;
}

const SizeNextActionSummaryReport* find_size_next_action(const CompileResult& result,
                                                         const std::string& source,
                                                         const std::string& action) {
  const auto it = std::find_if(result.size_attribution.next_actions.begin(),
                               result.size_attribution.next_actions.end(),
                               [&](const SizeNextActionSummaryReport& next_action) {
                                 return next_action.source == source &&
                                        next_action.action == action;
                               });
  return it == result.size_attribution.next_actions.end() ? nullptr : &*it;
}

std::string detail(const SizeHelperSummaryReport& helper, const std::string& key) {
  const auto it = helper.details.find(key);
  return it == helper.details.end() ? std::string() : it->second;
}

}  // namespace

void dangerous_loading_size_report_tracks_flow_entry_stack() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999999;

  const std::filesystem::path root = std::filesystem::current_path();
  const CompileResult result =
      compile_source(read_text(root / "examples" / "dangerous-loading.mkpro"), options);

  require(result.implemented, "dangerous-loading should compile");
  require(!has_error_diagnostic(result), "dangerous-loading should not report compile errors");
  require(result.steps.size() == 75, "dangerous-loading size should stay at the baseline");
  require(has_optimization(result, "aggressive-post-layout-branch-underflow-call-count"),
          "dangerous-loading should keep the selected compact post-layout candidate");

  const SizeHelperSummaryReport* resolve_turn = find_size_helper(result, "resolve_turn");
  require(resolve_turn != nullptr, "dangerous-loading should summarize resolve_turn");
  require(resolve_turn->body_cells == 11 && resolve_turn->call_site_cells == 1,
          "resolve_turn size attribution mismatch");
  require(detail(*resolve_turn, "valueAwareSymbolicEntryStackByCallSite")
                  .find("22:X=boat,Y=?,Z=?,T=?") != std::string::npos,
          "resolve_turn should keep the partial callsite entry-X fact");
  require(detail(*resolve_turn, "valueAwareSymbolicFlowEntryStack")
                  .find("30:X=?,Y=?,Z=?,T=?") != std::string::npos,
          "resolve_turn should merge indirect/default flow into the entry stack");
  require(detail(*resolve_turn, "valueAwareSymbolicEntryStackSeed") == "X=?,Y=?,Z=?,T=?",
          "resolve_turn merged entry stack should not claim boat is always in X");
  require(detail(*resolve_turn, "valueAwareSymbolicEntryStackSeedStatus") ==
              "unknown-callsite-stack",
          "resolve_turn entry stack status should reject incomplete entry-X proof");
  require(detail(*resolve_turn, "valueAwareEntryStackLostKnownFacts")
                  .find("boat@call22<-entry-X/blockedBy=X=?@30") != std::string::npos,
          "resolve_turn should report that the known callsite X value is killed by the "
          "unknown flow-entry stack");
  require(detail(*resolve_turn, "valueAwareEntryStackLostKnownFactCount") == "1" &&
              detail(*resolve_turn, "valueAwareEntryStackLostKnownFactSlots") == "X",
          "resolve_turn should count the lost entry-X fact");
  require(detail(*resolve_turn, "valueAwareEntryStackLostKnownFactStatus") ==
                  "known-callsite-facts-killed-by-unknown-flow-entry" &&
              detail(*resolve_turn, "valueAwareEntryStackLostKnownFactProofRequirement") ==
                  "flow-entry-stack-value-proof" &&
              detail(*resolve_turn, "valueAwareEntryStackLostKnownFactNextProofTarget") ==
                  "prove-flow-entry-X-values-or-split-entry-seeds" &&
              detail(*resolve_turn, "valueAwareEntryStackLostKnownFactRequiredAction") ==
                  "prove-flow-entry-stack-or-split-helper-entry-seeds-before-stack-input-rewrite",
          "resolve_turn should name the proof needed before treating entry-X as resident");
  require(detail(*resolve_turn, "valueAwareExistingEntryStackInputSites")
                  .find("boat@call22<-entry-X") != std::string::npos,
          "resolve_turn should still report the partial entry-X callsite");
  require(detail(*resolve_turn, "valueAwareStackInputMaterializeCellsByName")
                  .find("boat:1m/0e/1c") != std::string::npos,
          "boat should still pay materialization cost without full flow-entry proof");
  require(detail(*resolve_turn, "valueAwareStackInputProfitBreakdown")
                  .find("boat:1g/1m/+0n") != std::string::npos,
          "boat stack input should be break-even, not profitable");
  require(detail(*resolve_turn, "valueAwareSplitEntryStackInputSites")
                  .find("boat@call22<-entry-X/requires-split-entry-seed") !=
              std::string::npos,
          "resolve_turn should model the partial entry-X fact as a split-entry seed candidate");
  require(detail(*resolve_turn, "valueAwareSplitEntryStackInputCandidateNames") == "boat" &&
              detail(*resolve_turn, "valueAwareSplitEntryStackInputPositiveNames") == "boat" &&
              detail(*resolve_turn, "valueAwareSplitEntryStackInputMaterializeSavedCells") ==
                  "1" &&
              detail(*resolve_turn, "valueAwareSplitEntryStackInputPositiveNetCells") == "1",
          "resolve_turn split-entry model should show that boat becomes positive after saving "
          "one materialization cell");
  require(detail(*resolve_turn, "valueAwareSplitEntryStackInputProfitBreakdown") ==
                  "boat:1g/0m/1s/+1n" &&
              detail(*resolve_turn, "valueAwareSplitEntryStackInputPlanStatus") ==
                  "positive-if-split-entry-seed-proved" &&
              detail(*resolve_turn, "valueAwareSplitEntryStackInputProofStatus") ==
                  "requires-flow-entry-stack-proof-or-entry-seed-splitting" &&
              detail(*resolve_turn, "valueAwareSplitEntryStackInputNextProofTarget") ==
                  "prove-flow-entry-stack-values-or-split-helper-entry-seeds" &&
              detail(*resolve_turn, "valueAwareSplitEntryStackInputRequiredAction") ==
                  "split-helper-entry-seeds-or-prove-flow-entry-stack-before-stack-input-rewrite",
          "resolve_turn split-entry scheduler model should name the remaining proof and action");
  require(detail(*resolve_turn, "valueAwareBreakEvenStackInputNames") == "boat,threat" &&
              detail(*resolve_turn, "valueAwareBreakEvenStackInputCount") == "2" &&
              detail(*resolve_turn, "valueAwareBreakEvenStackInputGrossCells") == "2" &&
              detail(*resolve_turn, "valueAwareBreakEvenStackInputMaterializeCells") == "2" &&
              detail(*resolve_turn, "valueAwareBreakEvenStackInputNetCells") == "0",
          "resolve_turn should summarize break-even stack inputs separately from negative "
          "scheduler inputs");
  require(detail(*resolve_turn, "valueAwareBreakEvenStackInputPlanStatus") ==
                  "break-even-after-callsite-materialization" &&
              detail(*resolve_turn, "valueAwareBreakEvenStackInputAdditionalNetCellsToPositive") ==
                  "1" &&
              detail(*resolve_turn, "valueAwareBreakEvenStackInputPositiveGapReason") ==
                  "current-input-breaks-even-after-callsite-materialization" &&
              detail(*resolve_turn, "valueAwareBreakEvenStackInputNextProofTarget") ==
                  "reduce-callsite-materialization-by-1,"
                  "or-find-additional-helper-local-recall-savings-by-1" &&
              detail(*resolve_turn, "valueAwareBreakEvenStackInputRequiredAction") ==
                  "use-break-even-inputs-only-as-enabling-carriers-or-reduce-materialization-cost",
          "resolve_turn break-even scheduler attribution should name the one-cell path to a "
          "profitable stack input");
  require(detail(*resolve_turn, "valueAwareSchedulerPlanStatus") ==
              "no-profitable-stack-input-materialization",
          "resolve_turn should not rank direct-stack-fit after merging all entry flows");

  const SizeOpportunityReport* register_traffic = find_size_opportunity_detail(
      result, "helper-register-traffic", "helperLabel", "resolve_turn");
  require(register_traffic != nullptr && register_traffic->savings == 0 &&
              register_traffic->details.contains("sizeImpactStatus") &&
              register_traffic->details.at("sizeImpactStatus") == "estimated-nonpositive-net",
          "resolve_turn register-traffic opportunity should be nonpositive");

  const SizeNextActionSummaryReport* action = find_size_next_action(
      result, "costModelAction",
      "find-profitable-stack-input-call-sites-or-reduce-materialization-cost");
  require(action != nullptr && action->status == "stalled-nonpositive" &&
              action->potential_savings == 0 && action->best_savings == 0,
          "resolve_turn scheduler action should be stalled after the stricter proof");
  require(action != nullptr &&
              action->best_details.contains("valueAwareBreakEvenStackInputNames") &&
              action->best_details.at("valueAwareBreakEvenStackInputNames") == "boat,threat",
          "resolve_turn stalled scheduler action should retain break-even stack-input details");
  require(action != nullptr &&
              action->best_details.contains("valueAwareEntryStackLostKnownFacts") &&
              action->best_details.at("valueAwareEntryStackLostKnownFacts")
                      .find("boat@call22<-entry-X/blockedBy=X=?@30") != std::string::npos,
          "resolve_turn stalled scheduler action should retain lost entry-stack facts");
  require(action != nullptr &&
              action->best_details.contains("valueAwareSplitEntryStackInputPositiveNames") &&
              action->best_details.at("valueAwareSplitEntryStackInputPositiveNames") ==
                  "boat" &&
              action->best_details.at("valueAwareSplitEntryStackInputPositiveNetCells") == "1",
          "resolve_turn stalled scheduler action should retain the split-entry positive model");
}

void wumpus_size_report_tracks_split_entry_repeated_argument() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999999;

  const std::filesystem::path root = std::filesystem::current_path();
  const CompileResult result =
      compile_source(read_text(root / "examples" / "wumpus.mkpro"), options);

  require(result.implemented, "wumpus should compile");
  require(!has_error_diagnostic(result), "wumpus should not report compile errors");
  require(result.steps.size() == 105, "wumpus size should stay at the baseline");

  const SizeHelperSummaryReport* inspect = find_size_helper(result, "inspect");
  require(inspect != nullptr, "wumpus should summarize inspect");
  require(inspect->body_cells == 37 && inspect->call_site_cells == 2,
          "inspect size attribution mismatch");
  require(detail(*inspect, "valueAwareTopRepeatedArgumentRecall") ==
              "room:6@68,72,75,80,83,99",
          "inspect should keep room as the top repeated argument recall");
  require(detail(*inspect, "valueAwareRepeatedArgumentSchedulerFeasibility")
                  .find("unknown=6/68:not-proved-resident") != std::string::npos,
          "inspect should keep the conservative repeated-argument scheduler proof blocked");
  require(detail(*inspect, "valueAwareEntryStackLostKnownFacts")
                  .find("room@call97/via=inspect<-entry-X/blockedBy=X=?@68") !=
              std::string::npos,
          "inspect should report that the recursive entry-X room fact is killed by the "
          "unknown flow-entry stack");
  require(detail(*inspect, "valueAwareSplitEntryRepeatedArgumentCandidate") == "room" &&
              detail(*inspect, "valueAwareSplitEntryRepeatedArgumentSeedSlots") == "X",
          "inspect should model room as a split-entry repeated-argument candidate");
  require(detail(*inspect, "valueAwareSplitEntryRepeatedArgumentSeedSites")
                  .find("room@call97/via=inspect<-entry-X/blockedBy=X=?@68") !=
              std::string::npos,
          "inspect split repeated model should name the blocked entry-X seed site");
  require(detail(*inspect, "valueAwareSplitEntryRepeatedArgumentFeasibility")
                      .find("room:6recalls/profitableNet=+1/modelNet=+0") !=
                  std::string::npos &&
              detail(*inspect, "valueAwareSplitEntryRepeatedArgumentFeasibility")
                      .find("68:X(restore=0,net=+1)") != std::string::npos &&
              detail(*inspect, "valueAwareSplitEntryRepeatedArgumentFeasibility")
                      .find("75:Z(restore=2,net=-1)") != std::string::npos,
          "inspect split repeated model should price resident room recalls under the split "
          "entry seed");
  require(detail(*inspect, "valueAwareSplitEntryRepeatedArgumentStatus") ==
                  "has-profitable-x-resident-recall-sites" &&
              detail(*inspect, "valueAwareSplitEntryRepeatedArgumentProfitableNetCells") ==
                  "1" &&
              detail(*inspect, "valueAwareSplitEntryRepeatedArgumentModelNetCells") == "0" &&
              detail(*inspect, "valueAwareSplitEntryRepeatedArgumentProofStatus") ==
                  "requires-flow-entry-stack-proof-or-entry-seed-splitting" &&
              detail(*inspect, "valueAwareSplitEntryRepeatedArgumentRequiredAction") ==
                  "split-helper-entry-seeds-or-prove-flow-entry-stack-before-repeated-argument-rewrite",
          "inspect split repeated model should name the remaining proof and action");

  const SizeNextActionSummaryReport* action = find_size_next_action(
      result, "costModelAction",
      "find-profitable-stack-input-call-sites-or-reduce-materialization-cost");
  require(action != nullptr && action->best_details.contains(
                                "valueAwareSplitEntryRepeatedArgumentCandidate") &&
              action->best_details.at("valueAwareSplitEntryRepeatedArgumentCandidate") ==
                  "room" &&
              action->best_details.at(
                  "valueAwareSplitEntryRepeatedArgumentProfitableNetCells") == "1",
          "wumpus stalled scheduler action should retain the split repeated model");
}

}  // namespace mkpro::tests
