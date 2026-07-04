#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::string read_file(const std::filesystem::path& path) {
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

CompileResult compile_example(const std::filesystem::path& path, bool analysis_budgeted) {
  CompileOptions options;
  if (analysis_budgeted) {
    options.analysis = true;
    options.budget = 999999;
  }
  const CompileResult result = compile_source(read_file(path), options);
  require(result.implemented, "native compiler should implement example: " + path.string());
  require(!has_error_diagnostic(result), "example compile diagnostics should not include errors: " + path.string());
  return result;
}

std::size_t example_steps(const std::filesystem::path& path, bool analysis_budgeted) {
  return compile_example(path, analysis_budgeted).steps.size();
}

const CandidateReport* find_candidate(const std::vector<CandidateReport>& candidates,
                                      const std::string& variant) {
  const auto it = std::find_if(candidates.begin(), candidates.end(),
                               [&](const CandidateReport& candidate) {
                                 return candidate.variant == variant;
                               });
  return it == candidates.end() ? nullptr : &*it;
}

const SizeAttributionEntry* find_size_entry(const CompileResult& result,
                                            const std::string& kind,
                                            const std::string& label) {
  const auto it = std::find_if(result.size_attribution.entries.begin(),
                               result.size_attribution.entries.end(),
                               [&](const SizeAttributionEntry& entry) {
                                 return entry.kind == kind && entry.label == label;
                               });
  return it == result.size_attribution.entries.end() ? nullptr : &*it;
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

const SizeSelectedOptimizationReport* find_size_selected_optimization(
    const CompileResult& result, const std::string& variant) {
  const auto it = std::find_if(
      result.size_attribution.selected_optimizations.begin(),
      result.size_attribution.selected_optimizations.end(),
      [&](const SizeSelectedOptimizationReport& selected) {
        return selected.variant == variant;
      });
  return it == result.size_attribution.selected_optimizations.end() ? nullptr : &*it;
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

const SizeBlockerSummaryReport* find_size_blocker(const CompileResult& result,
                                                  const std::string& blocker_kind) {
  const auto it = std::find_if(result.size_attribution.blockers.begin(),
                               result.size_attribution.blockers.end(),
                               [&](const SizeBlockerSummaryReport& blocker) {
                                 return blocker.blocker_kind == blocker_kind;
                               });
  return it == result.size_attribution.blockers.end() ? nullptr : &*it;
}

const SizeSpillSummaryReport* find_size_spill(const CompileResult& result,
                                              const std::string& name) {
  const auto it = std::find_if(result.size_attribution.spills.begin(),
                               result.size_attribution.spills.end(),
                               [&](const SizeSpillSummaryReport& spill) {
                                 return spill.name == name;
                               });
  return it == result.size_attribution.spills.end() ? nullptr : &*it;
}

const SizeHelperSummaryReport* find_size_helper(const CompileResult& result,
                                                const std::string& label) {
  const auto it = std::find_if(result.size_attribution.helpers.begin(),
                               result.size_attribution.helpers.end(),
                               [&](const SizeHelperSummaryReport& helper) {
                                 return helper.label == label;
                               });
  return it == result.size_attribution.helpers.end() ? nullptr : &*it;
}

const SizeHelperSpillSummaryReport* find_size_helper_spill(const CompileResult& result,
                                                           const std::string& helper_label,
                                                           const std::string& name) {
  const auto it = std::find_if(result.size_attribution.helper_spills.begin(),
                               result.size_attribution.helper_spills.end(),
                               [&](const SizeHelperSpillSummaryReport& spill) {
                                 return spill.helper_label == helper_label && spill.name == name;
                               });
  return it == result.size_attribution.helper_spills.end() ? nullptr : &*it;
}

const SizeAbiBlockerReport* find_size_abi_blocker(const CompileResult& result,
                                                  const std::string& kind,
                                                  const std::string& label) {
  const auto it = std::find_if(result.size_attribution.abi_blockers.begin(),
                               result.size_attribution.abi_blockers.end(),
                               [&](const SizeAbiBlockerReport& blocker) {
                                 return blocker.kind == kind && blocker.label == label;
                               });
  return it == result.size_attribution.abi_blockers.end() ? nullptr : &*it;
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

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& optimization) {
                       return optimization.name == name;
                     });
}

std::vector<std::string> example_file_names(const std::filesystem::path& dir) {
  std::vector<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".mkpro")
      names.push_back(entry.path().stem().string());
  }
  std::sort(names.begin(), names.end());
  return names;
}

} // namespace

void example_sizes_match_typescript_baselines() {
  // Traceability:
  // - tests/compiler/example-sizes.test.ts
  // - tests/compiler/example-baselines.ts
  const std::map<std::string, std::size_t> EXAMPLE_BASELINE{
      {"99-bottles", 52},
      {"alaram", 66},
      {"basic", 7},
      {"cave-highlevel-baseline", 104},
      {"cave-sketch", 38},
      {"cave-treasure", 104},
      {"clock", 33},
      {"dangerous-loading", 84},
      {"dungeon", 75},
      {"e-94-digits", 64},
      {"functions-demo", 21},
      {"fox-hunt-100", 103},
      {"fox-hunt-mk61", 65},
      {"game-100-pig", 97},
      {"giants-country", 103},
      {"human", 23},
      {"jack-pot", 96},
      {"labyrinth777", 105},
      {"lunar", 44},
      {"minesweeper-9x7", 79},
      {"minesweeper-9x9", 79},
      {"raja-yoga", 77},
      {"rambo-iii", 104},
      {"river-battle", 95},
      {"sea-battle", 65},
      {"teleport", 96},
      {"tic-tac-toe", 99},
      {"tiny-game", 23},
      {"treasure-hunter-2", 99},
      {"wumpus", 105},
  };
  const std::map<std::string, std::size_t> PENDING_BASELINE{
      {"tic-tac-toe-4x4", 141},
  };

  const std::filesystem::path root = std::filesystem::current_path();
  const std::filesystem::path examples_root = root / "examples";
  const std::filesystem::path pending_root = examples_root / "pending-optimizer";

  const std::vector<std::string> expected_examples = [&] {
    std::vector<std::string> names;
    names.reserve(EXAMPLE_BASELINE.size());
    for (const auto& entry : EXAMPLE_BASELINE)
      names.push_back(entry.first);
    return names;
  }();

  const std::vector<std::string> expected_pending = [&] {
    std::vector<std::string> names;
    names.reserve(PENDING_BASELINE.size());
    for (const auto& entry : PENDING_BASELINE)
      names.push_back(entry.first);
    return names;
  }();

  require(example_file_names(examples_root) == expected_examples,
          "native examples list should exactly match TS baseline keys");
  require(example_file_names(pending_root) == expected_pending,
          "native pending-optimizer examples list should exactly match TS baseline keys");

  const bool progress = std::getenv("MKPRO_NATIVE_EXAMPLE_PROGRESS") != nullptr;
  std::size_t progress_index = 0;
  const std::size_t progress_total = EXAMPLE_BASELINE.size() + PENDING_BASELINE.size();
  for (const auto& [name, expected] : EXAMPLE_BASELINE) {
    if (progress) {
      ++progress_index;
      std::cerr << "[example-size] " << progress_index << "/" << progress_total << " " << name
                << std::endl;
    }
    const std::filesystem::path path = examples_root / (name + ".mkpro");
    const std::size_t actual = example_steps(path, /*analysis_budgeted=*/false);
    require(actual == expected, "top-level example " + name + " step count should match TS baseline");
    if (name == "fox-hunt-mk61") {
      const CompileResult result = compile_example(path, /*analysis_budgeted=*/true);
      require(std::any_of(result.steps.begin(), result.steps.end(), [](const ResolvedStep& step) {
                return step.address == 14 && step.comment.has_value() &&
                       step.comment->find("coord_list fused candidate; "
                                          "indirect-memory-targets=6,7,8,9,a,b,c,d,e") !=
                           std::string::npos;
              }),
              "fox-hunt-mk61 coord_list fused indirect recall should annotate its proved "
              "memory target range");
      const SizeOpportunityReport* indirect_flow =
          find_size_opportunity(result, "aggressive-post-layout-indirect-flow");
      require(indirect_flow != nullptr && indirect_flow->current_steps == 65 &&
                  indirect_flow->candidate_steps == 66 && indirect_flow->savings == -1 &&
                  indirect_flow->blocker_kind == "static-proof-gate" &&
                  indirect_flow->details.contains("proofFamily") &&
                  indirect_flow->details.at("proofFamily") == "indirect-flow-targets" &&
                  indirect_flow->details.contains("missingProof") &&
                  indirect_flow->details.at("missingProof") ==
                      "selector-register-preservation" &&
                  indirect_flow->details.contains("proofFailure") &&
                  indirect_flow->details.at("proofFailure") ==
                      "selector-register-used-as-data" &&
                  indirect_flow->details.contains("selectorRegister") &&
                  indirect_flow->details.at("selectorRegister") == "7" &&
                  indirect_flow->details.contains("candidateSelectorRegisters") &&
                  indirect_flow->details.at("candidateSelectorRegisters") == "7+8+9" &&
                  indirect_flow->details.contains("conflictingSelectorRegisters") &&
                  indirect_flow->details.at("conflictingSelectorRegisters") == "7+8+9" &&
                  indirect_flow->details.contains("allocatedName") &&
                  indirect_flow->details.at("allocatedName") == "__coord_list_foxes_1" &&
                  indirect_flow->details.contains("conflictingAllocatedNames") &&
                  indirect_flow->details.at("conflictingAllocatedNames") ==
                      "__coord_list_foxes_1+__coord_list_foxes_2+__coord_list_foxes_3" &&
                  indirect_flow->details.contains("consumerAddress") &&
                  indirect_flow->details.at("consumerAddress") == "14" &&
                  indirect_flow->details.contains("consumerOpcodeHex") &&
                  indirect_flow->details.at("consumerOpcodeHex") == "D5" &&
                  indirect_flow->details.contains("consumerOpcode") &&
                  indirect_flow->details.at("consumerOpcode") == "К П->X 5" &&
                  indirect_flow->details.contains("selectorDataConflictKind") &&
                  indirect_flow->details.at("selectorDataConflictKind") ==
                      "indirect-memory-recall" &&
                  indirect_flow->details.contains("selectorDataConflictTargets") &&
                  indirect_flow->details.at("selectorDataConflictTargets") ==
                      "6+7+8+9+a+b+c+d+e" &&
                  indirect_flow->details.contains("selectorDataConflictPrecision") &&
                  indirect_flow->details.at("selectorDataConflictPrecision") ==
                      "annotated-indirect-memory-targets" &&
                  indirect_flow->details.contains("selectorDataConflictAccesses") &&
                  indirect_flow->details.at("selectorDataConflictAccesses")
                          .find("7/__coord_list_foxes_1@14/D5/indirect-memory-recall/"
                                "targets:6+7+8+9+a+b+c+d+e") != std::string::npos &&
                  indirect_flow->details.at("selectorDataConflictAccesses")
                          .find("9/__coord_list_foxes_3@14/D5/indirect-memory-recall/"
                                "targets:6+7+8+9+a+b+c+d+e") != std::string::npos &&
                  indirect_flow->details.contains("selectorDataAllConflictTargets") &&
                  indirect_flow->details.at("selectorDataAllConflictTargets") ==
                      "6+7+8+9+a+b+c+d+e" &&
                  indirect_flow->details.contains("selectorDataOverlapRegisters") &&
                  indirect_flow->details.at("selectorDataOverlapRegisters") == "7+8+9" &&
                  indirect_flow->details.contains("selectorDataOverlapCount") &&
                  indirect_flow->details.at("selectorDataOverlapCount") == "3" &&
                  indirect_flow->details.contains("selectorDataPayloadLayout") &&
                  indirect_flow->details.at("selectorDataPayloadLayout") ==
                      "contiguous-indirect-window" &&
                  indirect_flow->details.contains("selectorDataPayloadTargetRange") &&
                  indirect_flow->details.at("selectorDataPayloadTargetRange") == "6..e" &&
                  indirect_flow->details.contains("selectorDataPayloadRegisterCount") &&
                  indirect_flow->details.at("selectorDataPayloadRegisterCount") == "9" &&
                  indirect_flow->details.contains("selectorDataRequiredFreeSelectorCount") &&
                  indirect_flow->details.at("selectorDataRequiredFreeSelectorCount") == "3" &&
                  indirect_flow->details.contains("selectorDataPayloadRegistersToFree") &&
                  indirect_flow->details.at("selectorDataPayloadRegistersToFree") ==
                      "7+8+9" &&
                  indirect_flow->details.contains(
                      "selectorDataPayloadRegisterBudgetAfterFreeingSelectors") &&
                  indirect_flow->details.at(
                      "selectorDataPayloadRegisterBudgetAfterFreeingSelectors") == "6" &&
                  indirect_flow->details.contains(
                      "selectorDataPayloadCompressionRequirement") &&
                  indirect_flow->details.at("selectorDataPayloadCompressionRequirement") ==
                      "9->6" &&
                  indirect_flow->details.contains("selectorDataPayloadCompressionReason") &&
                  indirect_flow->details.at("selectorDataPayloadCompressionReason") ==
                      "free-overlapping-flow-selectors" &&
                  indirect_flow->details.contains("selectorDataContiguousRelocationWindows") &&
                  indirect_flow->details.at("selectorDataContiguousRelocationWindows")
                          .find("5..d:overlaps-flow-selectors") != std::string::npos &&
                  indirect_flow->details.at("selectorDataContiguousRelocationWindows")
                          .find("6..e:overlaps-flow-selectors") != std::string::npos &&
                  indirect_flow->details.contains("selectorDataContiguousRelocationStatus") &&
                  indirect_flow->details.at("selectorDataContiguousRelocationStatus") ==
                      "no-selector-free-contiguous-window" &&
                  indirect_flow->details.contains("selectorDataPayloadPackingRequirement") &&
                  indirect_flow->details.at("selectorDataPayloadPackingRequirement") ==
                      "pack-or-split-contiguous-indirect-payload" &&
                  indirect_flow->details.contains("selectorDataPayloadPackingReason") &&
                  indirect_flow->details.at("selectorDataPayloadPackingReason") ==
                      "contiguous-window-overlaps-flow-selectors" &&
                  indirect_flow->details.contains("selectorDataProofGap") &&
                  indirect_flow->details.at("selectorDataProofGap") ==
                      "annotated-target-overlaps-selector-data" &&
                  indirect_flow->details.contains("selectorDataNextProofAction") &&
                  indirect_flow->details.at("selectorDataNextProofAction") ==
                      "split-selector-register-or-pack-data-away-from-flow-selectors" &&
                  indirect_flow->details.contains("selectorDataConflictResolutionStatus") &&
                  indirect_flow->details.at("selectorDataConflictResolutionStatus") ==
                      "proved-selector-data-overlap-requires-payload-repacking" &&
                  indirect_flow->details.contains("proofDisposition") &&
                  indirect_flow->details.at("proofDisposition") ==
                      "proved-conflict-needs-layout-change" &&
                  indirect_flow->details.contains("freeStableSelectorRegisters") &&
                  indirect_flow->details.at("freeStableSelectorRegisters") == "none" &&
                  indirect_flow->details.contains("selectorSplitStatus") &&
                  indirect_flow->details.at("selectorSplitStatus") ==
                      "no-free-stable-selector-register" &&
                  indirect_flow->details.contains("layoutAction") &&
                  indirect_flow->details.at("layoutAction") ==
                      "free-stable-selector-registers" &&
                  indirect_flow->details.contains("costModelAction") &&
                  indirect_flow->details.at("costModelAction") ==
                      "find-nonpacked-selector-layout-or-reduce-packed-access-overhead" &&
                  indirect_flow->details.contains(
                      "selectorDataPayloadPackingOverheadBudgetCells") &&
                  indirect_flow->details.at("selectorDataPayloadPackingOverheadBudgetCells") ==
                      "5" &&
                  indirect_flow->details.contains(
                      "selectorDataPayloadPackingBreakEvenCells") &&
                  indirect_flow->details.at("selectorDataPayloadPackingBreakEvenCells") ==
                      "5" &&
                  indirect_flow->details.contains(
                      "selectorDataPayloadPackingCostModelStatus") &&
                  indirect_flow->details.at("selectorDataPayloadPackingCostModelStatus") ==
                      "minimum-packed-access-overhead-not-positive" &&
                  indirect_flow->details.contains(
                      "selectorDataPayloadPackingCostModelRequirement") &&
                  indirect_flow->details.at(
                      "selectorDataPayloadPackingCostModelRequirement") ==
                      "find-nonpacked-selector-layout-or-reduce-packed-access-overhead" &&
                  indirect_flow->details.contains(
                      "selectorDataPayloadRegistersToPackMinimum") &&
                  indirect_flow->details.at("selectorDataPayloadRegistersToPackMinimum") ==
                      "3" &&
                  indirect_flow->details.contains(
                      "selectorDataPayloadMinPackedLogicalAccesses") &&
                  indirect_flow->details.at("selectorDataPayloadMinPackedLogicalAccesses") ==
                      "6" &&
                  indirect_flow->details.contains(
                      "selectorDataPayloadMinPackedAccessOverheadCells") &&
                  indirect_flow->details.at(
                      "selectorDataPayloadMinPackedAccessOverheadCells") == "6" &&
                  indirect_flow->details.contains(
                      "selectorDataPayloadPackingLowerBoundStatus") &&
                  indirect_flow->details.at(
                      "selectorDataPayloadPackingLowerBoundStatus") ==
                      "exceeds-candidate-savings" &&
                  indirect_flow->details.contains(
                      "selectorDataPayloadPackingNetLowerBoundCells") &&
                  indirect_flow->details.at("selectorDataPayloadPackingNetLowerBoundCells") ==
                      "-1" &&
                  indirect_flow->details.contains(
                      "estimatedCandidateStepsAfterPayloadPackingLowerBound") &&
                  indirect_flow->details.at(
                      "estimatedCandidateStepsAfterPayloadPackingLowerBound") == "66" &&
                  indirect_flow->details.contains("candidateStepsStatus") &&
                  indirect_flow->details.at("candidateStepsStatus") ==
                      "estimated-payload-packing-lower-bound-larger-than-current" &&
                  indirect_flow->details.contains("sizeImpactStatus") &&
                  indirect_flow->details.at("sizeImpactStatus") == "estimated-nonpositive-net" &&
                  indirect_flow->details.contains("netSavingsStatus") &&
                  indirect_flow->details.at("netSavingsStatus") ==
                      "payload-packing-lower-bound-exceeds-candidate-savings" &&
                  indirect_flow->details.contains("savingsModel") &&
                  indirect_flow->details.at("savingsModel") ==
                      "candidate-steps-plus-minimum-payload-packing-overhead" &&
                  indirect_flow->details.contains("requiredAction") &&
                  indirect_flow->details.at("requiredAction") ==
                      "find-nonpacked-selector-layout-or-reduce-payload-access-overhead",
              "fox-hunt-mk61 size attribution should explain why the 60-cell indirect-flow "
              "candidate is blocked by proved selector/data overlap");
      require(indirect_flow->savings == -1 &&
                  indirect_flow->candidate_steps ==
                      static_cast<int>(result.steps.size()) + 1,
              "fox-hunt-mk61 size attribution should rank selector payload packing by the "
              "minimum packed-access overhead lower bound");
      const SizeNextActionSummaryReport* selector_action = find_size_next_action(
          result, "requiredAction", "pack-data-away-from-flow-selectors");
      require(selector_action == nullptr,
              "fox-hunt-mk61 size attribution should not rank selector/data payload packing as "
              "a positive next action when minimum extraction overhead exceeds savings");
      const SizeNextActionSummaryReport* reduce_payload_action = find_size_next_action(
          result, "requiredAction",
          "find-nonpacked-selector-layout-or-reduce-payload-access-overhead");
      require(reduce_payload_action == nullptr,
              "fox-hunt-mk61 size attribution should keep overbudget selector payload work "
              "visible only on the nonpositive opportunity");
      const SizeNextActionSummaryReport* layout_action =
          find_size_next_action(result, "layoutAction", "free-stable-selector-registers");
      require(layout_action == nullptr,
              "fox-hunt-mk61 size attribution should not rank register relayout as positive "
              "when the required payload packing lower bound is nonpositive");
      const SizeNextActionSummaryReport* cost_model_action = find_size_next_action(
          result, "costModelAction", "estimate-payload-packing-for-selector-freeing");
      require(cost_model_action == nullptr,
              "fox-hunt-mk61 size attribution should not rank the old unestimated payload "
              "packing cost-model action after lower-bound accounting");
      const SizeNextActionSummaryReport* reduce_packing_cost_action = find_size_next_action(
          result, "costModelAction",
          "find-nonpacked-selector-layout-or-reduce-packed-access-overhead");
      require(reduce_packing_cost_action == nullptr,
              "fox-hunt-mk61 size attribution should keep the payload lower-bound cost model "
              "visible only on the nonpositive opportunity");
    }
    if (name == "dangerous-loading") {
      const CompileResult result = compile_example(path, /*analysis_budgeted=*/true);
      const SizeHelperSummaryReport* resolve_turn = find_size_helper(result, "resolve_turn");
      require(resolve_turn != nullptr &&
                  resolve_turn->details.contains("valueAwareStateOutputNames") &&
                  resolve_turn->details.at("valueAwareStateOutputNames") == "loaded" &&
                  resolve_turn->details.contains("valueAwareStateOutputPlanStatus") &&
                  resolve_turn->details.at("valueAwareStateOutputPlanStatus") ==
                      "requires-persistent-state-store" &&
                  resolve_turn->details.contains("valueAwareStateOutputNetCells") &&
                  resolve_turn->details.at("valueAwareStateOutputNetCells") == "0" &&
                  resolve_turn->details.contains("valueAwareEstimatedNetSavingsAfterMaterialization") &&
                  resolve_turn->details.at("valueAwareEstimatedNetSavingsAfterMaterialization") ==
                      "0" &&
                  resolve_turn->details.contains("valueAwareEstimatedNetSavingsExcludes") &&
                  resolve_turn->details.at("valueAwareEstimatedNetSavingsExcludes") ==
                      "persistent-state-output-stores",
              "dangerous-loading should not count persistent loaded stores as direct "
              "value-aware scheduler savings");
      const SizeOpportunityReport* resolve_turn_register_traffic =
          find_size_opportunity_detail(result, "helper-register-traffic", "helperLabel",
                                       "resolve_turn");
      require(resolve_turn_register_traffic != nullptr &&
                  resolve_turn_register_traffic->savings == 0 &&
                  resolve_turn_register_traffic->details.contains("sizeImpactStatus") &&
                  resolve_turn_register_traffic->details.at("sizeImpactStatus") ==
                      "estimated-nonpositive-net",
              "dangerous-loading should keep resolve_turn traffic visible while ranking its "
              "persistent state output as non-positive");
      const SizeNextActionSummaryReport* state_output_action = find_size_next_action(
          result, "trafficShapeAction", "split-stack-inputs-from-deferred-state-outputs");
      require(state_output_action == nullptr,
              "dangerous-loading should not rank persistent state-output stores as a positive "
              "next scheduler action");
    }
    if (name == "game-100-pig") {
      const CompileResult result = compile_example(path, /*analysis_budgeted=*/true);
      const SizeHelperSummaryReport* roll_die = find_size_helper(result, "roll_die");
      require(roll_die != nullptr &&
                  roll_die->details.contains("valueAwareSchedulerTrafficShape") &&
                  roll_die->details.at("valueAwareSchedulerTrafficShape") ==
                      "deferred-state-outputs-only" &&
                  roll_die->details.contains("valueAwareStateOutputNames") &&
                  roll_die->details.at("valueAwareStateOutputNames") == "die" &&
                  roll_die->details.contains("valueAwareStateOutputPlanStatus") &&
                  roll_die->details.at("valueAwareStateOutputPlanStatus") ==
                      "requires-persistent-state-store" &&
                  roll_die->details.contains("valueAwareStateOutputNetCells") &&
                  roll_die->details.at("valueAwareStateOutputNetCells") == "0" &&
                  roll_die->details.contains("valueAwareEstimatedNetSavingsAfterMaterialization") &&
                  roll_die->details.at("valueAwareEstimatedNetSavingsAfterMaterialization") ==
                      "0",
              "game-100-pig should expose roll_die's persistent die store without counting it as "
              "direct scheduler savings");
      const SizeOpportunityReport* roll_die_register_traffic =
          find_size_opportunity_detail(result, "helper-register-traffic", "helperLabel",
                                       "roll_die");
      require(roll_die_register_traffic != nullptr &&
                  roll_die_register_traffic->savings == 0 &&
                  roll_die_register_traffic->details.contains("sizeImpactStatus") &&
                  roll_die_register_traffic->details.at("sizeImpactStatus") ==
                      "estimated-nonpositive-net",
              "game-100-pig should keep roll_die traffic visible while ranking persistent state "
              "output as non-positive");
      const SizeNextActionSummaryReport* state_output_action =
          find_size_next_action(result, "trafficShapeAction", "defer-helper-state-output-stores");
      require(state_output_action == nullptr,
              "game-100-pig should not rank persistent die stores as a positive next scheduler "
              "action");
    }
    if (name == "rambo-iii") {
      const CompileResult result = compile_example(path, /*analysis_budgeted=*/true);
      const SizeHelperSummaryReport* front_stop = find_size_helper(result, "front_stop");
      require(front_stop != nullptr &&
                  front_stop->details.contains("valueAwareMixedStateNames") &&
                  front_stop->details.at("valueAwareMixedStateNames") == "cells_7,scratch" &&
                  front_stop->details.contains("valueAwareMixedStateCells") &&
                  front_stop->details.at("valueAwareMixedStateCells") == "4" &&
                  front_stop->details.contains("valueAwareMixedStateBreakdown") &&
                  front_stop->details.at("valueAwareMixedStateBreakdown")
                          .find("cells_7:2c/1r/1s@39..42") != std::string::npos &&
                  front_stop->details.at("valueAwareMixedStateBreakdown")
                          .find("scratch:2c/1r/1s@38..45") != std::string::npos &&
                  front_stop->details.contains("valueAwareMixedStateAccessOrder") &&
                  front_stop->details.at("valueAwareMixedStateAccessOrder")
                          .find("cells_7:R@39/S@42") != std::string::npos &&
                  front_stop->details.at("valueAwareMixedStateAccessOrder")
                          .find("scratch:S@38/R@45") != std::string::npos &&
                  front_stop->details.contains("valueAwareMixedStateLocalLifetimeNames") &&
                  front_stop->details.at("valueAwareMixedStateLocalLifetimeNames") ==
                      "cells_7,scratch" &&
                  front_stop->details.contains("valueAwareMixedStateLocalLifetimeCells") &&
                  front_stop->details.at("valueAwareMixedStateLocalLifetimeCells") == "4" &&
                  front_stop->details.contains("valueAwareMixedStateLifetimeStatus") &&
                  front_stop->details.at("valueAwareMixedStateLifetimeStatus") ==
                      "local-to-helper-without-nested-calls" &&
                  front_stop->details.contains("valueAwareMixedStateProofAction") &&
                  front_stop->details.at("valueAwareMixedStateProofAction") ==
                      "prove-local-stack-value-flow-through-mutating-ops" &&
                  front_stop->details.contains("valueAwareMixedStateTempCarrierNames") &&
                  front_stop->details.at("valueAwareMixedStateTempCarrierNames") == "scratch" &&
                  front_stop->details.contains("valueAwareMixedStateTempCarrierCells") &&
                  front_stop->details.at("valueAwareMixedStateTempCarrierCells") == "2" &&
                  front_stop->details.contains("valueAwareMixedStateTempCarrierGrossCells") &&
                  front_stop->details.at("valueAwareMixedStateTempCarrierGrossCells") == "2" &&
                  front_stop->details.contains(
                      "valueAwareMixedStateTempCarrierMaterializeCells") &&
                  front_stop->details.at(
                      "valueAwareMixedStateTempCarrierMaterializeCells") == "2" &&
                  front_stop->details.contains("valueAwareMixedStateTempCarrierNetCells") &&
                  front_stop->details.at("valueAwareMixedStateTempCarrierNetCells") == "0" &&
                  front_stop->details.contains("valueAwareMixedStateTempCarrierPlanStatus") &&
                  front_stop->details.at("valueAwareMixedStateTempCarrierPlanStatus") ==
                      "break-even-after-stack-preservation" &&
                  front_stop->details.contains("valueAwareMixedStateRequiredUpdateNames") &&
                  front_stop->details.at("valueAwareMixedStateRequiredUpdateNames") ==
                      "cells_7" &&
                  front_stop->details.contains("valueAwareMixedStateRequiredUpdateCells") &&
                  front_stop->details.at("valueAwareMixedStateRequiredUpdateCells") == "2" &&
                  front_stop->details.contains("valueAwareEstimatedNetSavingsAfterMaterialization") &&
                  front_stop->details.at("valueAwareEstimatedNetSavingsAfterMaterialization") ==
                      "0" &&
                  front_stop->details.contains("valueAwareEstimatedNetSavingsModel") &&
                  front_stop->details.at("valueAwareEstimatedNetSavingsModel") ==
                      "local-temp-carrier-register-traffic-minus-stack-preservation" &&
                  front_stop->details.contains("valueAwareEstimatedNetSavingsExcludes") &&
                  front_stop->details.at("valueAwareEstimatedNetSavingsExcludes") ==
                      "persistent-state-updates-and-nested-call-inputs" &&
                  !front_stop->details.contains("valueAwareMixedStateNestedCrossingNames") &&
                  front_stop->details.contains("valueAwareNestedCallInputNames") &&
                  front_stop->details.at("valueAwareNestedCallInputNames") == "random_state",
              "rambo-iii front_stop size attribution should split local mixed-state lifetimes "
              "from nested-call state inputs for the value-aware scheduler");
      const SizeOpportunityReport* front_stop_register_traffic =
          find_size_opportunity_detail(result, "helper-register-traffic", "helperLabel",
                                       "front_stop");
      require(front_stop_register_traffic != nullptr &&
                  front_stop_register_traffic->savings == 0 &&
                  front_stop_register_traffic->candidate_steps ==
                      static_cast<int>(result.steps.size()) &&
                  front_stop_register_traffic->details.contains("savingsModel") &&
                  front_stop_register_traffic->details.at("savingsModel") ==
                      "estimated-net-after-callsite-materialization" &&
                  front_stop_register_traffic->details.contains("candidateStepsStatus") &&
                  front_stop_register_traffic->details.at("candidateStepsStatus") ==
                      "synthetic-net-estimate-not-compiled" &&
                  front_stop_register_traffic->details.contains("sizeImpactStatus") &&
                  front_stop_register_traffic->details.at("sizeImpactStatus") ==
                      "estimated-nonpositive-net" &&
                  front_stop_register_traffic->details.contains("netSavingsStatus") &&
                  front_stop_register_traffic->details.at("netSavingsStatus") ==
                      "estimated-nonpositive-after-callsite-materialization" &&
                  front_stop_register_traffic->details.contains("trafficShapeAction") &&
                  front_stop_register_traffic->details.at("trafficShapeAction") ==
                      "prove-local-temp-carrier-through-state-update-guard" &&
                  front_stop_register_traffic->details.contains(
                      "valueAwareMixedStateLifetimeStatus") &&
                  front_stop_register_traffic->details.at(
                      "valueAwareMixedStateLifetimeStatus") ==
                      "local-to-helper-without-nested-calls",
              "rambo-iii should keep the front_stop temp-carrier proof visible while estimating "
              "it as break-even after required stack preservation");
      const SizeNextActionSummaryReport* mixed_state_action = find_size_next_action(
          result, "trafficShapeAction", "prove-local-temp-carrier-through-state-update-guard");
      require(mixed_state_action == nullptr,
              "rambo-iii should not rank a break-even local temp-carrier rewrite as a positive "
              "next scheduler action");
    }
  }

  for (const auto& [name, expected] : PENDING_BASELINE) {
    if (progress) {
      ++progress_index;
      std::cerr << "[example-size] " << progress_index << "/" << progress_total << " " << name
                << std::endl;
    }
    const std::filesystem::path path = pending_root / (name + ".mkpro");
    const CompileResult result = compile_example(path, /*analysis_budgeted=*/true);
    require(result.steps.size() == expected,
            "pending example " + name + " step count should match TS baseline");
    if (name == "tic-tac-toe-4x4") {
      require(has_optimization(result, "cell-mask-occupied-test-set"),
              "tic-tac-toe-4x4 should report cell_mask/occupied test-and-set fusion");
      const CandidateReport* packed_line_update_tail =
          find_candidate(result.rejected_candidates, "packed-line-family-update-check-tail");
      require(packed_line_update_tail != nullptr,
              "tic-tac-toe-4x4 should report nonwinning packed-line update/check tail candidate");
      require(packed_line_update_tail->steps >= static_cast<int>(result.steps.size()),
              "tic-tac-toe-4x4 packed-line update/check tail should not be reported as a hidden "
              "smaller candidate");
      const CandidateReport* generic_packed_score_fallback =
          find_candidate(result.rejected_candidates, "generic-packed-score-accumulator-fallback");
      require(generic_packed_score_fallback != nullptr &&
                  generic_packed_score_fallback->steps > static_cast<int>(result.steps.size()),
              "tic-tac-toe-4x4 should measure the generic packed_score accumulator fallback and "
              "keep the current specialized scorer when the fallback is larger");
      const CandidateReport* rejected_dead_integer =
          find_candidate(result.rejected_candidates, "fractional-constant-selector-dead-int");
      require(rejected_dead_integer == nullptr,
              "tic-tac-toe-4x4 should not report the unsafe dead-integer selector once the safe "
              "selector-layout refinement reaches the same size");
      require(result.size_attribution.total_cells == static_cast<int>(result.steps.size()),
              "tic-tac-toe-4x4 size attribution should report the final cell count");
      const SizeSelectedOptimizationReport* selected_indirect_flow =
          find_size_selected_optimization(result, "preloaded-indirect-flow");
      require(selected_indirect_flow != nullptr && selected_indirect_flow->site == "flow" &&
                  selected_indirect_flow->current_steps ==
                      static_cast<int>(result.steps.size()) &&
                  selected_indirect_flow->baseline_steps ==
                      static_cast<int>(result.steps.size()) + 5 &&
                  selected_indirect_flow->savings == 5 &&
                  selected_indirect_flow->details.contains("estimateKind") &&
                  selected_indirect_flow->details.at("estimateKind") ==
                      "gross-main-cell-direct-to-indirect-flow" &&
                  selected_indirect_flow->details.contains("replacementCount") &&
                  selected_indirect_flow->details.at("replacementCount") == "5" &&
                  selected_indirect_flow->details.contains("savingsModel"),
              "tic-tac-toe-4x4 size attribution should expose selected indirect-flow savings");
      const SizeAttributionEntry* candidate_score =
          find_size_entry(result, "helper-region", "packed-line score accumulator helper");
      require(candidate_score != nullptr && candidate_score->cells >= 5 &&
                  candidate_score->detail.find("calls=") != std::string::npos,
              "tic-tac-toe-4x4 size attribution should expose the packed_score helper body");
      const SizeAttributionEntry* cell_mask =
          find_size_entry(result, "helper-region", "expr cell_mask(x, y)");
      require(cell_mask != nullptr && cell_mask->cells >= 4,
              "tic-tac-toe-4x4 size attribution should expose the cell_mask helper body");
      const SizeAttributionEntry* cell_mask_calls =
          find_size_entry(result, "helper-call-sites", "expr cell_mask(x, y)");
      require(cell_mask_calls != nullptr && cell_mask_calls->cells == 6 &&
                  cell_mask_calls->occurrences == 3 &&
                  cell_mask_calls->detail.find("target=") != std::string::npos,
              "tic-tac-toe-4x4 size attribution should expose the cell_mask call-site cost");
      const SizeAttributionEntry* packed_score_calls =
          find_size_entry(result, "helper-call-sites", "packed-line score accumulator helper");
      require(packed_score_calls != nullptr && packed_score_calls->cells == 2 &&
                  packed_score_calls->occurrences == 2 &&
                  packed_score_calls->detail.find("target=") != std::string::npos,
              "tic-tac-toe-4x4 size attribution should expose the packed_score call-site cost");
      const SizeHelperSummaryReport* packed_score_helper =
          find_size_helper(result, "packed-line score accumulator helper");
      require(packed_score_helper != nullptr &&
                  packed_score_helper->body_cells == candidate_score->cells &&
                  packed_score_helper->call_site_cells == packed_score_calls->cells &&
                  packed_score_helper->call_occurrences == packed_score_calls->occurrences &&
                  packed_score_helper->total_cells ==
                      candidate_score->cells + packed_score_calls->cells,
              "tic-tac-toe-4x4 size attribution should summarize packed_score helper body and "
              "call-site costs");
      require(packed_score_helper->details.contains("role") &&
                  packed_score_helper->details.at("role") == "packed-score-accumulator" &&
                  packed_score_helper->details.contains("pipelineShape") &&
                  packed_score_helper->details.at("pipelineShape") ==
                      "packed-line-family-score" &&
                  packed_score_helper->details.contains("accumulatorTerms") &&
                  packed_score_helper->details.at("accumulatorTerms") == "4" &&
                  packed_score_helper->details.contains("sharedTailTerms") &&
                  packed_score_helper->details.at("sharedTailTerms") == "4" &&
                  packed_score_helper->details.contains("pow10ScaleLineValuePolicy") &&
                  packed_score_helper->details.at("pow10ScaleLineValuePolicy") ==
                      "line-value-carried-under-index" &&
                  packed_score_helper->details.contains("pow10ScaleCorrectnessAction") &&
                  packed_score_helper->details.at("pow10ScaleCorrectnessAction") ==
                      "keep-line-value-under-index-before-helper" &&
                  packed_score_helper->details.contains("nextPipelineAction") &&
                  packed_score_helper->details.at("nextPipelineAction") ==
                      "value-aware-stack-register-scheduling" &&
                  packed_score_helper->details.contains("bodyCells") &&
                  packed_score_helper->details.contains("callSiteCells") &&
                  packed_score_helper->details.contains("callOccurrences") &&
                  packed_score_helper->details.contains("bodyCellsPerAccumulatorTerm"),
              "tic-tac-toe-4x4 packed_score helper summary should expose accumulator term and "
              "cost details");
      const SizeHelperSummaryReport* cell_mask_helper =
          find_size_helper(result, "expr cell_mask(x, y)");
      require(cell_mask_helper != nullptr && cell_mask_helper->body_cells == cell_mask->cells &&
                  cell_mask_helper->call_site_cells == cell_mask_calls->cells &&
                  cell_mask_helper->call_occurrences == cell_mask_calls->occurrences &&
                  cell_mask_helper->total_cells == cell_mask->cells + cell_mask_calls->cells,
              "tic-tac-toe-4x4 size attribution should summarize cell_mask helper body and "
              "call-site costs");
      const SizeHelperSpillSummaryReport* cell_mask_x_spill =
          find_size_helper_spill(result, "expr cell_mask(x, y)", "x");
      const SizeHelperSpillSummaryReport* cell_mask_y_spill =
          find_size_helper_spill(result, "expr cell_mask(x, y)", "y");
      require(cell_mask_x_spill != nullptr && cell_mask_x_spill->recall_cells >= 1 &&
                  cell_mask_x_spill->store_cells == 0,
              "tic-tac-toe-4x4 size attribution should expose x recalls inside the cell_mask "
              "helper body");
      require(cell_mask_y_spill != nullptr && cell_mask_y_spill->recall_cells >= 1 &&
                  cell_mask_y_spill->store_cells == 0,
              "tic-tac-toe-4x4 size attribution should expose y recalls inside the cell_mask "
              "helper body");
      require(cell_mask_helper->register_traffic_cells ==
                      cell_mask_x_spill->total_cells + cell_mask_y_spill->total_cells &&
                  cell_mask_helper->register_recall_cells ==
                      cell_mask_x_spill->recall_cells + cell_mask_y_spill->recall_cells &&
                  cell_mask_helper->register_store_cells ==
                      cell_mask_x_spill->store_cells + cell_mask_y_spill->store_cells &&
                  cell_mask_helper->register_traffic_occurrences ==
                      cell_mask_x_spill->recall_occurrences +
                          cell_mask_y_spill->recall_occurrences +
                          cell_mask_x_spill->store_occurrences +
                          cell_mask_y_spill->store_occurrences &&
                  cell_mask_helper->details.contains("registerTrafficAction") &&
                  cell_mask_helper->details.at("registerTrafficAction") ==
                      "try-value-aware-stack-register-scheduling" &&
                  cell_mask_helper->details.contains("registerTrafficEstimateKind") &&
                  cell_mask_helper->details.at("registerTrafficEstimateKind") ==
                      "gross-local-helper-traffic" &&
                  cell_mask_helper->details.contains("registerTrafficProofStatus") &&
                  cell_mask_helper->details.at("registerTrafficProofStatus") ==
                      "missing-callsite-stack-value-proof" &&
                  cell_mask_helper->details.contains("registerTrafficNames") &&
                  cell_mask_helper->details.at("registerTrafficNames").find("x") !=
                      std::string::npos &&
                  cell_mask_helper->details.at("registerTrafficNames").find("y") !=
                      std::string::npos &&
                  cell_mask_helper->details.contains("registerTrafficBreakdown") &&
                  cell_mask_helper->details.at("registerTrafficBreakdown").find("x:") !=
                      std::string::npos &&
                  cell_mask_helper->details.at("registerTrafficBreakdown").find("y:") !=
                      std::string::npos &&
                  cell_mask_helper->details.contains("valueAwareStackInputNames") &&
                  cell_mask_helper->details.at("valueAwareStackInputNames").find("x") !=
                      std::string::npos &&
                  cell_mask_helper->details.at("valueAwareStackInputNames").find("y") !=
                      std::string::npos &&
                  cell_mask_helper->details.contains("valueAwareStackInputCells") &&
                  cell_mask_helper->details.at("valueAwareStackInputCells") == "2" &&
                  cell_mask_helper->details.contains("valueAwareSchedulerTrafficShape") &&
                  cell_mask_helper->details.at("valueAwareSchedulerTrafficShape") ==
                      "stack-inputs-only" &&
                  cell_mask_helper->details.contains(
                      "valueAwareProfitableStackInputPlanStatus") &&
                  cell_mask_helper->details.at("valueAwareProfitableStackInputPlanStatus") ==
                      "no-profitable-stack-input-materialization" &&
                  cell_mask_helper->details.contains("valueAwareUnprofitableStackInputNames") &&
                  cell_mask_helper->details.at("valueAwareUnprofitableStackInputNames")
                          .find("x") != std::string::npos &&
                  cell_mask_helper->details.at("valueAwareUnprofitableStackInputNames")
                          .find("y") != std::string::npos &&
                  !cell_mask_helper->details.contains("valueAwareStateOutputNames"),
              "tic-tac-toe-4x4 helper summary should aggregate helper-local register traffic "
              "and callsite materialization costs for value-aware scheduler attribution");
      const SizeHelperSummaryReport* candidate_score_zero =
          find_size_helper(result, "candidate_score zero-accumulator entry");
      require(candidate_score_zero != nullptr &&
                  candidate_score_zero->details.contains("valueAwareStackInputNames") &&
                  candidate_score_zero->details.at("valueAwareStackInputNames").find("x") !=
                      std::string::npos &&
                  candidate_score_zero->details.at("valueAwareStackInputNames").find("y") !=
                      std::string::npos &&
                  candidate_score_zero->details.at("valueAwareStackInputNames").find("lines_4") !=
                      std::string::npos &&
                  candidate_score_zero->details.at("valueAwareStackInputNames").find("lines_7") !=
                      std::string::npos &&
                  candidate_score_zero->details.contains("valueAwareSchedulerTrafficShape") &&
                  candidate_score_zero->details.at("valueAwareSchedulerTrafficShape") ==
                      "stack-inputs-only" &&
                  candidate_score_zero->details.contains("valueAwareStackInputUniqueCount") &&
                  candidate_score_zero->details.at("valueAwareStackInputUniqueCount") == "6" &&
                  candidate_score_zero->details.contains("valueAwareStackCapacityStatus") &&
                  candidate_score_zero->details.at("valueAwareStackCapacityStatus") ==
                      "fits-x-y-z-t-capacity" &&
                  candidate_score_zero->details.contains("valueAwareAllStackCapacityStatus") &&
                  candidate_score_zero->details.at("valueAwareAllStackCapacityStatus") ==
                      "exceeds-x-y-z-t-capacity" &&
                  candidate_score_zero->details.contains("valueAwareStackCapacityBasis") &&
                  candidate_score_zero->details.at("valueAwareStackCapacityBasis") ==
                      "profitable-stack-inputs-after-materialization" &&
                  candidate_score_zero->details.contains("valueAwareStackInputRanking") &&
                  candidate_score_zero->details.at("valueAwareStackInputRanking").find("y:3") !=
                      std::string::npos &&
                  candidate_score_zero->details.at("valueAwareStackInputRanking").find("x:2") !=
                      std::string::npos &&
                  candidate_score_zero->details.contains("valueAwareStackInputPlanStatus") &&
                  candidate_score_zero->details.at("valueAwareStackInputPlanStatus") ==
                      "blocked-by-stack-mutating-callee" &&
                  candidate_score_zero->details.contains("valueAwareStackInputPlanBlocker") &&
                  candidate_score_zero->details.at("valueAwareStackInputPlanBlocker") ==
                      "nested-helper-calls-are-stack-mutating" &&
                  candidate_score_zero->details.contains(
                      "valueAwareCallPreservationInputNames") &&
                  candidate_score_zero->details.at("valueAwareCallPreservationInputNames")
                          .find("x") != std::string::npos &&
                  candidate_score_zero->details.at("valueAwareCallPreservationInputNames")
                          .find("y") != std::string::npos &&
                  candidate_score_zero->details.contains(
                      "valueAwareCallPreservationMatrix") &&
                  candidate_score_zero->details.at("valueAwareCallPreservationMatrix")
                          .find("packed-line score accumulator helper") !=
                      std::string::npos &&
                  candidate_score_zero->details.contains(
                      "valueAwareCallPreservationSites") &&
                  candidate_score_zero->details.at("valueAwareCallPreservationSites")
                          .find("packed-line score accumulator helper@71:y@73/76/80") !=
                      std::string::npos &&
                  candidate_score_zero->details.at("valueAwareCallPreservationSites")
                          .find("packed-line score accumulator helper@74:x@82") !=
                      std::string::npos &&
                  candidate_score_zero->details.contains(
                      "valueAwareCallPreservationRecallAddresses") &&
                  candidate_score_zero->details.at("valueAwareCallPreservationRecallAddresses") ==
                      "x:82;y:73,76,80" &&
                  candidate_score_zero->details.contains(
                      "valueAwareCallPreservationProofAction") &&
                  candidate_score_zero->details.at("valueAwareCallPreservationProofAction") ==
                      "refactor-stack-mutating-callee-abi" &&
                  candidate_score_zero->details.contains(
                      "valueAwareCallPreservationCalleeEffects") &&
                  candidate_score_zero->details.at("valueAwareCallPreservationCalleeEffects")
                          .find("packed-line score accumulator helper:stack-mutating") !=
                      std::string::npos &&
                  candidate_score_zero->details.at("valueAwareCallPreservationCalleeEffects")
                          .find("consumeYDrop=3") != std::string::npos &&
                  candidate_score_zero->details.contains(
                      "valueAwareCallPreservationMutatingCells") &&
                  candidate_score_zero->details.at("valueAwareCallPreservationMutatingCells") ==
                      "packed-line score accumulator helper:4" &&
                  candidate_score_zero->details.contains(
                      "valueAwareCallPreservationMutatingOpcodes") &&
                  candidate_score_zero->details.at("valueAwareCallPreservationMutatingOpcodes")
                          .find("86://consume-y-drop") != std::string::npos &&
                  candidate_score_zero->details.at("valueAwareCallPreservationMutatingOpcodes")
                          .find("91:+/consume-y-drop") != std::string::npos &&
                  candidate_score_zero->details.contains(
                      "valueAwareCallPreservationCalleeStatus") &&
                  candidate_score_zero->details.at("valueAwareCallPreservationCalleeStatus") ==
                      "blocked-by-callee-stack-mutation" &&
                  candidate_score_zero->details.contains(
                      "valueAwareCalleeAbiRefactorKind") &&
                  candidate_score_zero->details.at("valueAwareCalleeAbiRefactorKind") ==
                      "stack-preserving-entry-for-live-caller-inputs" &&
                  candidate_score_zero->details.contains(
                      "valueAwareCalleeAbiRefactorTargets") &&
                  candidate_score_zero->details.at("valueAwareCalleeAbiRefactorTargets") ==
                      "packed-line score accumulator helper" &&
                  candidate_score_zero->details.contains(
                      "valueAwareCalleeAbiPreservationPlan") &&
                  candidate_score_zero->details.at("valueAwareCalleeAbiPreservationPlan")
                          .find("packed-line score accumulator helper:x,y") !=
                      std::string::npos &&
                  candidate_score_zero->details.contains(
                      "valueAwareCalleeAbiPreserveDepthByCallee") &&
                  candidate_score_zero->details.at("valueAwareCalleeAbiPreserveDepthByCallee") ==
                      "packed-line score accumulator helper:2" &&
                  candidate_score_zero->details.contains(
                      "valueAwareCalleeAbiMaxPreserveDepth") &&
                  candidate_score_zero->details.at("valueAwareCalleeAbiMaxPreserveDepth") ==
                      "2" &&
                  candidate_score_zero->details.contains(
                      "valueAwareCalleeAbiPreserveDepthBasis") &&
                  candidate_score_zero->details.at("valueAwareCalleeAbiPreserveDepthBasis") ==
                      "live-caller-stack-inputs-after-nested-call" &&
                  candidate_score_zero->details.contains(
                      "valueAwareCalleeAbiSafetyProof") &&
                  candidate_score_zero->details.at("valueAwareCalleeAbiSafetyProof") ==
                      "prove-live-stack-inputs-survive-nested-callee-entry" &&
                  candidate_score_zero->details.contains(
                      "valueAwareCalleeAbiImplementationStatus") &&
                  candidate_score_zero->details.at(
                      "valueAwareCalleeAbiImplementationStatus") ==
                      "required-before-stack-input-scheduling" &&
                  candidate_score_zero->details.contains(
                      "valueAwareCalleeAbiOverheadBudgetCells") &&
                  candidate_score_zero->details.at("valueAwareCalleeAbiOverheadBudgetCells") ==
                      "1" &&
                  candidate_score_zero->details.contains(
                      "valueAwareCalleeAbiBreakEvenAddedCells") &&
                  candidate_score_zero->details.at("valueAwareCalleeAbiBreakEvenAddedCells") ==
                      "1" &&
                  candidate_score_zero->details.contains(
                      "valueAwareCalleeAbiMutationSurfaceCells") &&
                  candidate_score_zero->details.at(
                      "valueAwareCalleeAbiMutationSurfaceCells") == "4" &&
                  candidate_score_zero->details.contains(
                      "valueAwareCalleeAbiMutationSurfaceStatus") &&
                  candidate_score_zero->details.at(
                      "valueAwareCalleeAbiMutationSurfaceStatus") ==
                      "exceeds-overhead-budget" &&
                  candidate_score_zero->details.contains(
                      "valueAwareCalleeAbiCostModelStatus") &&
                  candidate_score_zero->details.at("valueAwareCalleeAbiCostModelStatus") ==
                      "mutation-surface-exceeds-overhead-budget" &&
                  candidate_score_zero->details.contains(
                      "valueAwareCalleeAbiCostModelRequirement") &&
                  candidate_score_zero->details.at(
                      "valueAwareCalleeAbiCostModelRequirement") ==
                      "prove-stack-preserving-callee-abi-overhead-below-mutation-surface-"
                      "before-ranking" &&
                  candidate_score_zero->details.contains(
                      "valueAwareSuggestedResidentInputNames") &&
                  candidate_score_zero->details.at("valueAwareSuggestedResidentInputNames")
                          .find("y") != std::string::npos &&
                  candidate_score_zero->details.at("valueAwareSuggestedResidentInputNames")
                          .find("x") != std::string::npos &&
                  !candidate_score_zero->details.contains(
                      "valueAwareSuggestedStagedInputNames") &&
                  candidate_score_zero->details.contains("valueAwareStackInputPressure") &&
                  candidate_score_zero->details.at("valueAwareStackInputPressure") == "0" &&
                  candidate_score_zero->details.contains("valueAwareAllStackInputPressure") &&
                  candidate_score_zero->details.at("valueAwareAllStackInputPressure") == "2" &&
                  candidate_score_zero->details.contains(
                      "valueAwareEstimatedNetSavingsBeforeArgumentPreservation") &&
                  candidate_score_zero->details.at(
                      "valueAwareEstimatedNetSavingsBeforeArgumentPreservation") == "3" &&
                  candidate_score_zero->details.contains("valueAwareCallArgumentInputNames") &&
                  candidate_score_zero->details.at("valueAwareCallArgumentInputNames")
                          .find("x") != std::string::npos &&
                  candidate_score_zero->details.at("valueAwareCallArgumentInputNames")
                          .find("y") != std::string::npos &&
                  candidate_score_zero->details.contains("valueAwareCallArgumentSites") &&
                  candidate_score_zero->details.at("valueAwareCallArgumentSites")
                          .find("packed-line score accumulator helper@71:x<-recall@70") !=
                      std::string::npos &&
                  candidate_score_zero->details.at("valueAwareCallArgumentSites")
                          .find("packed-line score accumulator helper@74:y<-recall@73") !=
                      std::string::npos &&
                  candidate_score_zero->details.contains(
                      "valueAwareCallArgumentPreservationCells") &&
                  candidate_score_zero->details.at("valueAwareCallArgumentPreservationCells") ==
                      "2" &&
                  candidate_score_zero->details.contains(
                      "valueAwareEstimatedNetSavingsAfterArgumentPreservation") &&
                  candidate_score_zero->details.at(
                      "valueAwareEstimatedNetSavingsAfterArgumentPreservation") == "1" &&
                  candidate_score_zero->details.contains(
                      "valueAwareEstimatedNetSavingsAfterMaterialization") &&
                  candidate_score_zero->details.at(
                      "valueAwareEstimatedNetSavingsAfterMaterialization") == "1" &&
                  candidate_score_zero->details.contains("valueAwareEstimatedNetSavingsModel") &&
                  candidate_score_zero->details.at("valueAwareEstimatedNetSavingsModel") ==
                      "profitable-stack-input-recalls-minus-callsite-materialization-minus-"
                      "argument-preservation-excluding-persistent-state-outputs" &&
                  candidate_score_zero->details.contains("valueAwareProfitableStackInputNames") &&
                  candidate_score_zero->details.at("valueAwareProfitableStackInputNames")
                          .find("y") != std::string::npos &&
                  candidate_score_zero->details.at("valueAwareProfitableStackInputNames")
                          .find("x") != std::string::npos &&
                  candidate_score_zero->details.contains("valueAwareBreakEvenStackInputNames") &&
                  candidate_score_zero->details.at("valueAwareBreakEvenStackInputNames")
                          .find("lines_4") != std::string::npos &&
                  candidate_score_zero->details.at("valueAwareBreakEvenStackInputNames")
                          .find("lines_7") != std::string::npos &&
                  candidate_score_zero->details.contains(
                      "valueAwareProfitableStackInputPlanStatus") &&
                  candidate_score_zero->details.at("valueAwareProfitableStackInputPlanStatus") ==
                      "blocked-by-stack-mutating-callee" &&
                  candidate_score_zero->details.contains("valueAwareNestedCallLabels") &&
                  candidate_score_zero->details.at("valueAwareNestedCallLabels")
                          .find("packed-line score accumulator helper") != std::string::npos &&
                  candidate_score_zero->details.at("valueAwareNestedCallLabels")
                          .find("normalize") != std::string::npos &&
                  !candidate_score_zero->details.contains("valueAwareStateOutputNames"),
              "tic-tac-toe-4x4 candidate_score helper should classify helper-local traffic as "
              "stack input candidates and net materialization savings for the value-aware "
              "scheduler");
      const SizeHelperSummaryReport* normalize_helper = find_size_helper(result, "normalize");
      require(normalize_helper != nullptr &&
                  !normalize_helper->details.contains("valueAwareStackInputNames"),
              "tic-tac-toe-4x4 normalize helper should not inherit packed-line score traffic "
              "from a BCD/direct-address collision in size attribution");
      const SizeHelperSummaryReport* mark_lines_helper =
          find_size_helper(result, "mark_lines_and_check");
      require(mark_lines_helper != nullptr &&
                  mark_lines_helper->details.contains("valueAwareStackInputNames") &&
                  mark_lines_helper->details.at("valueAwareStackInputNames").find("x") !=
                      std::string::npos &&
                  mark_lines_helper->details.at("valueAwareStackInputNames").find("y") !=
                      std::string::npos &&
                  !mark_lines_helper->details.contains("valueAwareStateOutputNames") &&
                  mark_lines_helper->details.contains("valueAwareNestedCallInputNames") &&
                  mark_lines_helper->details.at("valueAwareNestedCallInputNames")
                          .find("best_score") != std::string::npos &&
                  mark_lines_helper->details.at("valueAwareNestedCallInputNames").find("slot") !=
                      std::string::npos &&
                  mark_lines_helper->details.contains("valueAwareNestedCallInputCells") &&
                  mark_lines_helper->details.at("valueAwareNestedCallInputCells") == "2" &&
                  mark_lines_helper->details.contains("valueAwareNestedCallInputReason") &&
                  mark_lines_helper->details.contains("valueAwareSchedulerTrafficShape") &&
                  mark_lines_helper->details.at("valueAwareSchedulerTrafficShape") ==
                      "stack-inputs-and-nested-call-inputs" &&
                  mark_lines_helper->details.contains("valueAwareStackInputPlanStatus") &&
                  mark_lines_helper->details.at("valueAwareStackInputPlanStatus") ==
                      "blocked-by-stack-mutating-callee" &&
                  mark_lines_helper->details.contains("valueAwareStackInputPlanBlocker") &&
                  mark_lines_helper->details.at("valueAwareStackInputPlanBlocker") ==
                      "nested-helper-calls-are-stack-mutating" &&
                  mark_lines_helper->details.contains("valueAwareCallPreservationInputNames") &&
                  mark_lines_helper->details.at("valueAwareCallPreservationInputNames")
                          .find("x") != std::string::npos &&
                  mark_lines_helper->details.at("valueAwareCallPreservationInputNames")
                          .find("y") != std::string::npos &&
                  mark_lines_helper->details.contains("valueAwareCallPreservationMatrix") &&
                  mark_lines_helper->details.at("valueAwareCallPreservationMatrix")
                          .find("mark_one") != std::string::npos &&
                  mark_lines_helper->details.contains("valueAwareCallPreservationSites") &&
                  mark_lines_helper->details.at("valueAwareCallPreservationSites")
                          .find("mark_one@97:x@A2/>6C") != std::string::npos &&
                  mark_lines_helper->details.at("valueAwareCallPreservationSites")
                          .find("normalize@>69:y@>6D") != std::string::npos &&
                  mark_lines_helper->details.contains(
                      "valueAwareCallPreservationRecallAddresses") &&
                  mark_lines_helper->details.at("valueAwareCallPreservationRecallAddresses")
                          .find("x:A2,>6C") != std::string::npos &&
                  mark_lines_helper->details.at("valueAwareCallPreservationRecallAddresses")
                          .find("y:99,A3,>6D") != std::string::npos &&
                  mark_lines_helper->details.contains(
                      "valueAwareCallPreservationProofAction") &&
                  mark_lines_helper->details.at("valueAwareCallPreservationProofAction") ==
                      "refactor-stack-mutating-callee-abi" &&
                  mark_lines_helper->details.contains(
                      "valueAwareCallPreservationCalleeEffects") &&
                  mark_lines_helper->details.at("valueAwareCallPreservationCalleeEffects")
                          .find("mark_one:stack-mutating") != std::string::npos &&
                  mark_lines_helper->details.at("valueAwareCallPreservationCalleeEffects")
                          .find("normalize:stack-mutating") != std::string::npos &&
                  mark_lines_helper->details.contains(
                      "valueAwareCallPreservationMutatingCells") &&
                  mark_lines_helper->details.at("valueAwareCallPreservationMutatingCells")
                          .find("mark_one:") != std::string::npos &&
                  mark_lines_helper->details.at("valueAwareCallPreservationMutatingCells")
                          .find("normalize:") != std::string::npos &&
                  mark_lines_helper->details.contains(
                      "valueAwareCallPreservationCalleeStatus") &&
                  mark_lines_helper->details.at("valueAwareCallPreservationCalleeStatus") ==
                      "blocked-by-callee-stack-mutation" &&
                  mark_lines_helper->details.contains("valueAwareCalleeAbiRefactorTargets") &&
                  mark_lines_helper->details.at("valueAwareCalleeAbiRefactorTargets")
                          .find("mark_one") != std::string::npos &&
                  mark_lines_helper->details.at("valueAwareCalleeAbiRefactorTargets")
                          .find("normalize") != std::string::npos &&
                  mark_lines_helper->details.contains("valueAwareCalleeAbiPreservationPlan") &&
                  mark_lines_helper->details.at("valueAwareCalleeAbiPreservationPlan")
                          .find("mark_one:x,y") != std::string::npos &&
                  mark_lines_helper->details.at("valueAwareCalleeAbiPreservationPlan")
                          .find("normalize:x,y") != std::string::npos &&
                  mark_lines_helper->details.contains(
                      "valueAwareCalleeAbiPreserveDepthByCallee") &&
                  mark_lines_helper->details.at("valueAwareCalleeAbiPreserveDepthByCallee")
                          .find("mark_one:2") != std::string::npos &&
                  mark_lines_helper->details.at("valueAwareCalleeAbiPreserveDepthByCallee")
                          .find("normalize:2") != std::string::npos &&
                  mark_lines_helper->details.contains("valueAwareCalleeAbiMaxPreserveDepth") &&
                  mark_lines_helper->details.at("valueAwareCalleeAbiMaxPreserveDepth") == "2" &&
                  mark_lines_helper->details.contains("valueAwareNestedCallLabels") &&
                  mark_lines_helper->details.at("valueAwareNestedCallLabels").find("mark_one") !=
                      std::string::npos &&
                  mark_lines_helper->details.at("valueAwareNestedCallLabels").find("normalize") !=
                      std::string::npos &&
                  mark_lines_helper->details.contains(
                      "valueAwareEstimatedNetSavingsBeforeArgumentPreservation") &&
                  mark_lines_helper->details.at(
                      "valueAwareEstimatedNetSavingsBeforeArgumentPreservation") == "2" &&
                  mark_lines_helper->details.contains("valueAwareCallArgumentInputNames") &&
                  mark_lines_helper->details.at("valueAwareCallArgumentInputNames")
                          .find("x") != std::string::npos &&
                  mark_lines_helper->details.at("valueAwareCallArgumentInputNames")
                          .find("y") != std::string::npos &&
                  mark_lines_helper->details.contains("valueAwareCallArgumentSites") &&
                  mark_lines_helper->details.at("valueAwareCallArgumentSites")
                          .find("mark_one@97:x<-recall@96") != std::string::npos &&
                  mark_lines_helper->details.at("valueAwareCallArgumentSites")
                          .find("mark_one@A0:y<-recall@99") != std::string::npos &&
                  mark_lines_helper->details.contains(
                      "valueAwareCallArgumentPreservationCells") &&
                  mark_lines_helper->details.at("valueAwareCallArgumentPreservationCells") ==
                      "2" &&
                  mark_lines_helper->details.contains(
                      "valueAwareEstimatedNetSavingsAfterArgumentPreservation") &&
                  mark_lines_helper->details.at(
                      "valueAwareEstimatedNetSavingsAfterArgumentPreservation") == "0" &&
                  mark_lines_helper->details.contains(
                      "valueAwareEstimatedNetSavingsAfterMaterialization") &&
                  mark_lines_helper->details.at(
                      "valueAwareEstimatedNetSavingsAfterMaterialization") == "0",
              "tic-tac-toe-4x4 mark_lines_and_check helper should classify stack inputs "
              "separately from nested-call state inputs and nested helper-call blockers");
      const SizeOpportunityReport* cell_mask_register_traffic = find_size_opportunity_detail(
          result, "helper-register-traffic", "helperLabel", "expr cell_mask(x, y)");
      require(cell_mask_register_traffic != nullptr &&
                  cell_mask_register_traffic->site == "helper" &&
                  cell_mask_register_traffic->savings == 0 &&
                  cell_mask_register_traffic->candidate_steps ==
                      static_cast<int>(result.steps.size()) &&
                  cell_mask_register_traffic->blocker_kind ==
                      "value-aware-stack-register-scheduler" &&
                  cell_mask_register_traffic->details.contains("savingsModel") &&
                  cell_mask_register_traffic->details.at("savingsModel") ==
                      "estimated-net-after-callsite-materialization" &&
                  cell_mask_register_traffic->details.contains("estimateKind") &&
                  cell_mask_register_traffic->details.at("estimateKind") ==
                      "estimated-net-after-materialization" &&
                  cell_mask_register_traffic->details.contains("candidateStepsStatus") &&
                  cell_mask_register_traffic->details.at("candidateStepsStatus") ==
                      "not-a-positive-size-opportunity" &&
                  cell_mask_register_traffic->details.contains("sizeImpactStatus") &&
                  cell_mask_register_traffic->details.at("sizeImpactStatus") ==
                      "estimated-nonpositive-net" &&
                  cell_mask_register_traffic->details.contains("netSavingsStatus") &&
                  cell_mask_register_traffic->details.at("netSavingsStatus") ==
                      "no-profitable-stack-inputs-after-callsite-materialization" &&
                  cell_mask_register_traffic->details.contains(
                      "estimatedGrossRegisterTrafficCells") &&
                  cell_mask_register_traffic->details.at(
                      "estimatedGrossRegisterTrafficCells") ==
                      std::to_string(cell_mask_helper->register_traffic_cells) &&
                  cell_mask_register_traffic->details.contains(
                      "valueAwareEstimatedNetSavingsAfterMaterialization") &&
                  cell_mask_register_traffic->details.at(
                      "valueAwareEstimatedNetSavingsAfterMaterialization") == "0" &&
                  cell_mask_register_traffic->details.contains("proofStatus") &&
                  cell_mask_register_traffic->details.at("proofStatus") ==
                      "missing-callsite-stack-value-proof" &&
                  cell_mask_register_traffic->details.contains("requiredAction") &&
                  cell_mask_register_traffic->details.at("requiredAction") ==
                      "value-aware-stack-register-scheduling" &&
                  cell_mask_register_traffic->details.contains("registerTrafficNames") &&
                  cell_mask_register_traffic->details.at("registerTrafficNames").find("x") !=
                      std::string::npos &&
                  cell_mask_register_traffic->details.at("registerTrafficNames").find("y") !=
                      std::string::npos &&
                  cell_mask_register_traffic->details.contains("registerTrafficBreakdown") &&
                  cell_mask_register_traffic->details.at("registerTrafficBreakdown").find("x:") !=
                      std::string::npos &&
                  cell_mask_register_traffic->details.at("registerTrafficBreakdown").find("y:") !=
                      std::string::npos,
              "tic-tac-toe-4x4 size attribution should keep net-zero helper-local register "
              "traffic visible without ranking it as a positive value-aware scheduler "
              "opportunity");
      const SizeHelperSummaryReport* mark_one_helper = find_size_helper(result, "mark_one");
      require(mark_one_helper != nullptr &&
                  mark_one_helper->details.contains("selectorBoundRegisterTrafficNames") &&
                  mark_one_helper->details.at("selectorBoundRegisterTrafficNames") == "slot" &&
                  mark_one_helper->details.contains("selectorBoundRegisterTrafficCells") &&
                  mark_one_helper->details.at("selectorBoundRegisterTrafficCells") == "2" &&
                  mark_one_helper->details.contains("valueAwareRegisterTrafficNames") &&
                  mark_one_helper->details.at("valueAwareRegisterTrafficNames").find("slot") ==
                      std::string::npos &&
                  mark_one_helper->details.at("valueAwareRegisterTrafficNames")
                          .find("best_score") != std::string::npos,
              "tic-tac-toe-4x4 mark_one helper summary should split indirect-selector traffic "
              "from value-aware scheduler traffic");
      const SizeOpportunityReport* mark_one_register_traffic = find_size_opportunity_detail(
          result, "helper-register-traffic", "helperLabel", "mark_one");
      require(mark_one_register_traffic != nullptr &&
                  mark_one_register_traffic->savings == 0 &&
                  mark_one_register_traffic->candidate_steps ==
                      static_cast<int>(result.steps.size()) &&
                  mark_one_register_traffic->details.contains("savingsModel") &&
                  mark_one_register_traffic->details.at("savingsModel") ==
                      "estimated-net-after-callsite-materialization" &&
                  mark_one_register_traffic->details.contains("netSavingsStatus") &&
                  mark_one_register_traffic->details.at("netSavingsStatus") ==
                      "no-profitable-stack-inputs-after-callsite-materialization" &&
                  mark_one_register_traffic->details.contains("registerTrafficNames") &&
                  mark_one_register_traffic->details.at("registerTrafficNames").find("slot") ==
                      std::string::npos &&
                  mark_one_register_traffic->details.at("registerTrafficNames")
                          .find("best_score") != std::string::npos &&
                  mark_one_register_traffic->details.contains(
                      "selectorBoundRegisterTrafficNames") &&
                  mark_one_register_traffic->details.at("selectorBoundRegisterTrafficNames") ==
                      "slot",
              "tic-tac-toe-4x4 value-aware scheduler attribution should keep net-zero helper "
              "traffic visible and should not count the indirect selector as removable "
              "stack/register traffic");
      const SizeAbiBlockerReport* stack_helper_abi = find_size_abi_blocker(
          result, "stack-helper-abi", "cell_mask(x, y)");
      require(stack_helper_abi != nullptr && stack_helper_abi->line == 103 &&
                  stack_helper_abi->materialize_cells == 2 &&
                  stack_helper_abi->details.contains("requiredAction") &&
                  stack_helper_abi->details.at("requiredAction") ==
                      "stack-argument-helper-entry",
              "tic-tac-toe-4x4 size attribution should expose the stack-resident helper ABI "
              "blocker instead of hiding it behind stack-resident-temps");
      require(stack_helper_abi->details.contains("grossMaterializeCells") &&
                  stack_helper_abi->details.at("grossMaterializeCells") == "2" &&
                  stack_helper_abi->details.contains("entryOverheadStatus") &&
                  stack_helper_abi->details.at("entryOverheadStatus") ==
                      "estimated-stack-entry-helper-cost" &&
                  stack_helper_abi->details.contains("estimatedStackEntryOverheadCells") &&
                  stack_helper_abi->details.at("estimatedStackEntryOverheadCells") == "11" &&
                  stack_helper_abi->details.contains("estimatedNetSavings") &&
                  stack_helper_abi->details.at("estimatedNetSavings") == "-9" &&
                  stack_helper_abi->details.contains("stackEntryCandidateCallSites") &&
                  stack_helper_abi->details.at("stackEntryCandidateCallSites") == "1" &&
                  stack_helper_abi->details.contains("materializeCellsPerCallSite") &&
                  stack_helper_abi->details.at("materializeCellsPerCallSite") == "2" &&
                  stack_helper_abi->details.contains("correctnessStatus") &&
                  stack_helper_abi->details.at("correctnessStatus") ==
                      "requires-stack-argument-entry-or-materialization" &&
                  stack_helper_abi->details.contains("safeFallbackAction") &&
                  stack_helper_abi->details.at("safeFallbackAction") ==
                      "materialize-stack-resident-temps-before-register-entry" &&
                  stack_helper_abi->details.contains("schedulerAction") &&
                  stack_helper_abi->details.at("schedulerAction") ==
                      "prove-stack-aware-helper-call" &&
                  stack_helper_abi->details.contains("estimatedEntryOverheadAmortization") &&
                  stack_helper_abi->details.at("estimatedEntryOverheadAmortization") ==
                      "shared-helper-entry-body" &&
                  stack_helper_abi->details.contains("estimatedBreakEvenCallSites") &&
                  stack_helper_abi->details.at("estimatedBreakEvenCallSites") == "6" &&
                  stack_helper_abi->details.contains("additionalCallSitesToBreakEven") &&
                  stack_helper_abi->details.at("additionalCallSitesToBreakEven") == "5" &&
                  stack_helper_abi->details.contains("netSavingsStatus") &&
                  stack_helper_abi->details.at("netSavingsStatus") ==
                      "estimated-negative-after-entry-cost" &&
                  stack_helper_abi->details.contains("costModelAction") &&
                  stack_helper_abi->details.at("costModelAction") ==
                      "find-more-stack-entry-call-sites-or-inline-helper",
              "tic-tac-toe-4x4 stack-helper ABI blocker should distinguish gross materialization "
              "savings from estimated stack-entry body cost");
      const SizeOpportunityReport* stack_helper_abi_opportunity =
          find_size_opportunity(result, "stack-helper-abi");
      require(stack_helper_abi_opportunity != nullptr &&
                  stack_helper_abi_opportunity->site == "abi" &&
                  stack_helper_abi_opportunity->savings == -9 &&
                  stack_helper_abi_opportunity->candidate_steps ==
                      static_cast<int>(result.steps.size()) + 9 &&
                  stack_helper_abi_opportunity->blocker_kind == "stack-helper-abi" &&
                  stack_helper_abi_opportunity->details.contains("abiStatus") &&
                  stack_helper_abi_opportunity->details.at("abiStatus") ==
                      "missing-stack-argument-entry" &&
                  stack_helper_abi_opportunity->details.contains("candidateBasis") &&
                  stack_helper_abi_opportunity->details.at("candidateBasis") ==
                      "avoid-materializing-stack-resident-temps" &&
                  stack_helper_abi_opportunity->details.contains("savingsModel") &&
                  stack_helper_abi_opportunity->details.at("savingsModel") ==
                      "estimated-net-after-entry-cost" &&
                  stack_helper_abi_opportunity->details.contains("grossMaterializeCells") &&
                  stack_helper_abi_opportunity->details.at("grossMaterializeCells") == "2" &&
                  stack_helper_abi_opportunity->details.contains("correctnessStatus") &&
                  stack_helper_abi_opportunity->details.at("correctnessStatus") ==
                      "requires-stack-argument-entry-or-materialization" &&
                  stack_helper_abi_opportunity->details.contains("safeFallbackAction") &&
                  stack_helper_abi_opportunity->details.at("safeFallbackAction") ==
                      "materialize-stack-resident-temps-before-register-entry" &&
                  stack_helper_abi_opportunity->details.contains("schedulerAction") &&
                  stack_helper_abi_opportunity->details.at("schedulerAction") ==
                      "prove-stack-aware-helper-call" &&
                  stack_helper_abi_opportunity->details.contains("costModelAction") &&
                  stack_helper_abi_opportunity->details.at("costModelAction") ==
                      "find-more-stack-entry-call-sites-or-inline-helper" &&
                  stack_helper_abi_opportunity->details.contains("estimatedNetSavings") &&
                  stack_helper_abi_opportunity->details.at("estimatedNetSavings") == "-9" &&
                  stack_helper_abi_opportunity->details.contains(
                      "stackEntryCandidateCallSites") &&
                  stack_helper_abi_opportunity->details.at("stackEntryCandidateCallSites") ==
                      "1" &&
                  stack_helper_abi_opportunity->details.contains(
                      "estimatedBreakEvenCallSites") &&
                  stack_helper_abi_opportunity->details.at("estimatedBreakEvenCallSites") ==
                      "6" &&
                  stack_helper_abi_opportunity->details.contains(
                      "additionalCallSitesToBreakEven") &&
                  stack_helper_abi_opportunity->details.at("additionalCallSitesToBreakEven") ==
                      "5" &&
                  stack_helper_abi_opportunity->details.contains("estimatedCandidateSteps") &&
                  stack_helper_abi_opportunity->details.at("estimatedCandidateSteps") ==
                      std::to_string(static_cast<int>(result.steps.size()) + 9) &&
                  stack_helper_abi_opportunity->details.contains("requiredAction") &&
                  stack_helper_abi_opportunity->details.at("requiredAction") ==
                      "stack-argument-helper-entry",
              "tic-tac-toe-4x4 size attribution should keep the stack-helper ABI blocker visible "
              "while ranking it by estimated net savings");
      const SizeAttributionEntry* recall_occupied =
          find_size_entry(result, "listing", "recall occupied");
      require(recall_occupied != nullptr && recall_occupied->cells >= 1,
              "tic-tac-toe-4x4 size attribution should expose recall costs by state name");
      const SizeAttributionEntry* set_occupied =
          find_size_entry(result, "listing", "set occupied");
      require(set_occupied != nullptr && set_occupied->cells >= 1,
              "tic-tac-toe-4x4 size attribution should expose store costs by state name");
      const SizeSpillSummaryReport* occupied_spill = find_size_spill(result, "occupied");
      require(occupied_spill != nullptr && occupied_spill->total_cells >= 4 &&
                  occupied_spill->recall_cells >= 3 && occupied_spill->store_cells >= 1,
              "tic-tac-toe-4x4 size attribution should summarize recall/store spill costs by "
              "state name");
      const SizeOpportunityReport* dead_integer_opportunity =
          find_size_opportunity(result, "fractional-constant-selector-dead-int");
      require(dead_integer_opportunity == nullptr,
              "tic-tac-toe-4x4 should not keep the unsafe dead-integer selector as a positive "
              "size opportunity after the safe selector-layout candidate reaches 136 cells");
      const SizeBlockerSummaryReport* data_arithmetic_blocker =
          find_size_blocker(result, "data-arithmetic");
      require(data_arithmetic_blocker == nullptr,
              "tic-tac-toe-4x4 should not summarize dead-integer data arithmetic as a positive "
              "blocker once the safe 136-cell selector-layout candidate is selected");
      const SizeBlockerSummaryReport* stack_helper_abi_blocker =
          find_size_blocker(result, "stack-helper-abi");
      require(stack_helper_abi_blocker == nullptr,
              "tic-tac-toe-4x4 size attribution should not summarize net-negative stack-helper "
              "ABI savings as a positive optimizer blocker");
      const SizeBlockerSummaryReport* value_aware_scheduler_blocker =
          find_size_blocker(result, "value-aware-stack-register-scheduler");
      require(value_aware_scheduler_blocker == nullptr,
              "tic-tac-toe-4x4 size attribution should not summarize overbudget "
              "stack-mutating callee ABI work as a positive scheduler blocker");
      const SizeOpportunityReport* candidate_score_register_traffic =
          find_size_opportunity_detail(result, "helper-register-traffic", "helperLabel",
                                       "candidate_score zero-accumulator entry");
      require(candidate_score_register_traffic != nullptr &&
                  candidate_score_register_traffic->site == "helper" &&
                  candidate_score_register_traffic->savings == 0 &&
                  candidate_score_register_traffic->candidate_steps ==
                      static_cast<int>(result.steps.size()) &&
                  candidate_score_register_traffic->blocker_kind ==
                      "value-aware-stack-register-scheduler" &&
                  candidate_score_register_traffic->details.contains("savingsModel") &&
                  candidate_score_register_traffic->details.at("savingsModel") ==
                      "estimated-net-after-callee-abi-surface-budget" &&
                  candidate_score_register_traffic->details.contains("estimateKind") &&
                  candidate_score_register_traffic->details.at("estimateKind") ==
                      "estimated-net-after-callee-abi-surface" &&
                  candidate_score_register_traffic->details.contains("candidateStepsStatus") &&
                  candidate_score_register_traffic->details.at("candidateStepsStatus") ==
                      "not-a-positive-size-opportunity" &&
                  candidate_score_register_traffic->details.contains("sizeImpactStatus") &&
                  candidate_score_register_traffic->details.at("sizeImpactStatus") ==
                      "estimated-nonpositive-net" &&
                  candidate_score_register_traffic->details.contains("netSavingsStatus") &&
                  candidate_score_register_traffic->details.at("netSavingsStatus") ==
                      "stack-preserving-callee-abi-mutation-surface-exceeds-budget" &&
                  candidate_score_register_traffic->details.contains("requiredAction") &&
                  candidate_score_register_traffic->details.at("requiredAction") ==
                      "prove-or-reduce-stack-preserving-callee-abi-overhead" &&
                  candidate_score_register_traffic->details.contains("costModelAction") &&
                  candidate_score_register_traffic->details.at("costModelAction") ==
                      "estimate-stack-preserving-callee-abi-overhead-from-mutation-surface" &&
                  candidate_score_register_traffic->details.contains(
                      "valueAwareCalleeAbiMutationSurfaceCells") &&
                  candidate_score_register_traffic->details.at(
                      "valueAwareCalleeAbiMutationSurfaceCells") == "4" &&
                  candidate_score_register_traffic->details.contains(
                      "valueAwareCalleeAbiOverheadBudgetCells") &&
                  candidate_score_register_traffic->details.at(
                      "valueAwareCalleeAbiOverheadBudgetCells") == "1",
              "tic-tac-toe-4x4 size attribution should keep the candidate_score scheduler "
              "opportunity visible while ranking the overbudget callee ABI surface as "
              "nonpositive");
      const SizeNextActionSummaryReport* required_action = find_size_next_action(
          result, "requiredAction", "keep-fractional-erase-before-data-arithmetic");
      require(required_action == nullptr,
              "tic-tac-toe-4x4 should not rank dead-integer fractional erase ordering as a "
              "positive next action after the safe selector-layout candidate reaches 136 cells");
      const SizeNextActionSummaryReport* layout_action = find_size_next_action(
          result, "layoutAction", "relayout-or-overlay-flow-to-natural-target");
      require(layout_action == nullptr,
              "tic-tac-toe-4x4 should not rank dead-integer natural-target relayout as a "
              "positive next action after the safe selector-layout candidate reaches 136 cells");
      const SizeNextActionSummaryReport* safe_savings_action = find_size_next_action(
          result, "safeSavingsAction", "align-selector-flow-to-natural-target");
      require(safe_savings_action == nullptr,
              "tic-tac-toe-4x4 should not rank dead-integer natural-target selector alignment "
              "after the safe selector-layout candidate reaches 136 cells");
      const SizeNextActionSummaryReport* discovery_action = find_size_next_action(
          result, "candidateDiscoveryAction", "allow-natural-target-layout-candidate");
      require(discovery_action == nullptr,
              "tic-tac-toe-4x4 should not rank dead-integer natural-target candidate discovery "
              "after the safe selector-layout candidate reaches 136 cells");
      const SizeNextActionSummaryReport* stack_helper_action = find_size_next_action(
          result, "requiredAction", "stack-argument-helper-entry");
      require(stack_helper_action == nullptr,
              "tic-tac-toe-4x4 size attribution should not rank net-negative stack-entry ABI "
              "implementation as a positive next action");
      const SizeNextActionSummaryReport* callee_abi_required_action = find_size_next_action(
          result, "requiredAction", "refactor-stack-mutating-callee-abi");
      require(callee_abi_required_action == nullptr,
              "tic-tac-toe-4x4 size attribution should not rank overbudget callee ABI "
              "refactoring as a positive required action");
      const SizeNextActionSummaryReport* callee_abi_proof_action = find_size_next_action(
          result, "requiredAction", "prove-or-reduce-stack-preserving-callee-abi-overhead");
      require(callee_abi_proof_action == nullptr,
              "tic-tac-toe-4x4 size attribution should keep overbudget callee ABI proof work "
              "visible only on the nonpositive opportunity");
      const SizeNextActionSummaryReport* callee_abi_cost_model_action = find_size_next_action(
          result, "costModelAction",
          "estimate-stack-preserving-callee-abi-overhead-from-mutation-surface");
      require(callee_abi_cost_model_action == nullptr,
              "tic-tac-toe-4x4 size attribution should not rank the overbudget callee ABI cost "
              "model as a positive next action");
      const SizeNextActionSummaryReport* stack_input_scheduler_action = find_size_next_action(
          result, "trafficShapeAction", "schedule-stack-input-helper-values");
      require(stack_input_scheduler_action == nullptr,
              "tic-tac-toe-4x4 size attribution should not rank direct stack-input helper "
              "scheduling when callsite materialization makes it net-zero");
      const SizeNextActionSummaryReport* staged_stack_input_action = find_size_next_action(
          result, "trafficShapeAction", "split-or-stage-stack-input-helper-values");
      require(staged_stack_input_action == nullptr,
              "tic-tac-toe-4x4 size attribution should not rank break-even line inputs as a "
              "positive staged stack-input action");
      const SizeNextActionSummaryReport* callee_abi_action = find_size_next_action(
          result, "trafficShapeAction", "refactor-stack-mutating-callee-abi");
      require(callee_abi_action == nullptr,
              "tic-tac-toe-4x4 size attribution should not rank stack-mutating callee ABI work "
              "as a positive traffic-shape action when the mutation surface exceeds the budget");
    }
  }
}

} // namespace mkpro::tests
