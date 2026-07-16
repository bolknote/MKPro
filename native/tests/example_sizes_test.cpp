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

const SizeHelperSummaryReport* find_size_helper(const CompileResult& result,
                                                const std::string& label) {
  const auto it = std::find_if(result.size_attribution.helpers.begin(),
                               result.size_attribution.helpers.end(),
                               [&](const SizeHelperSummaryReport& helper) {
                                 return helper.label == label;
                               });
  return it == result.size_attribution.helpers.end() ? nullptr : &*it;
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
      {"clock", 32},
      {"dangerous-loading", 75},
      {"dungeon", 75},
      {"e-94-digits", 64},
      {"functions-demo", 13},
      {"fox-hunt-100", 102},
      {"fox-hunt-mk61", 65},
      {"game-100-pig", 97},
      {"giants-country", 102},
      {"human", 23},
      {"jack-pot", 94},
      {"labyrinth777", 105},
      {"lunar", 44},
      {"minesweeper-9x7", 76},
      {"minesweeper-9x9", 76},
      {"raja-yoga", 77},
      {"rambo-iii", 104},
      {"river-battle", 95},
      {"sea-battle", 65},
      {"teleport", 96},
      {"tic-tac-toe", 99},
      {"tiny-game", 23},
      {"treasure-hunter-2", 98},
      {"wumpus", 105},
      {"zagaday-tsifru", 104},
  };
  const std::map<std::string, std::size_t> PENDING_BASELINE{
      {"tic-tac-toe-4x4", 146},
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
  const bool size_only = std::getenv("MKPRO_NATIVE_EXAMPLE_SIZE_ONLY") != nullptr;
  std::size_t progress_index = 0;
  const std::size_t progress_total = EXAMPLE_BASELINE.size() + PENDING_BASELINE.size();
  std::string size_mismatches;
  const auto record_size_mismatch = [&](const std::string& category, const std::string& name,
                                        std::size_t expected, std::size_t actual) {
    if (actual == expected)
      return;
    if (!size_mismatches.empty())
      size_mismatches += "; ";
    size_mismatches += category + " " + name + " expected=" + std::to_string(expected) +
                       " actual=" + std::to_string(actual);
  };
  for (const auto& [name, expected] : EXAMPLE_BASELINE) {
    if (progress) {
      ++progress_index;
      std::cerr << "[example-size] " << progress_index << "/" << progress_total << " " << name
                << std::endl;
    }
    const std::filesystem::path path = examples_root / (name + ".mkpro");
    const std::size_t actual = example_steps(path, /*analysis_budgeted=*/false);
    record_size_mismatch("top-level example", name, expected, actual);
    if (size_only)
      continue;
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
                      "find-nonpacked-selector-layout-or-reduce-payload-access-overhead" &&
                  indirect_flow->details.contains("blockedProof") &&
                  indirect_flow->details.at("blockedProof") ==
                      "indirect-flow-targets:selector-register-used-as-data" &&
                  indirect_flow->details.contains("blockedProofAction") &&
                  indirect_flow->details.at("blockedProofAction") ==
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
    if (name == "cave-treasure") {
      const CompileResult result = compile_example(path, /*analysis_budgeted=*/true);
      const SizeOpportunityReport* dead_integer_selector =
          find_size_opportunity(result, "fractional-constant-selector-dead-int");
      require(dead_integer_selector != nullptr &&
                  dead_integer_selector->blocker_kind == "indirect-address-control-use" &&
                  dead_integer_selector->details.contains("consumerAddress") &&
                  dead_integer_selector->details.at("consumerAddress") == "55" &&
                  dead_integer_selector->details.contains("selectorTarget") &&
                  dead_integer_selector->details.at("selectorTarget") == "43" &&
                  dead_integer_selector->details.contains("fractionalSelectorConsumer") &&
                  dead_integer_selector->details.at("fractionalSelectorConsumer") == "К БП 7" &&
                  dead_integer_selector->details.contains("consumerControlKind") &&
                  dead_integer_selector->details.at("consumerControlKind") ==
                      "direct-indirect-jump" &&
                  dead_integer_selector->details.contains("fractionalSelectorSourceRegister") &&
                  dead_integer_selector->details.at("fractionalSelectorSourceRegister") == "e" &&
                  dead_integer_selector->details.contains("deadIntegerSelectorCarrierRegister") &&
                  dead_integer_selector->details.at("deadIntegerSelectorCarrierRegister") == "e" &&
                  dead_integer_selector->details.contains("integerPartUseRole") &&
                  dead_integer_selector->details.at("integerPartUseRole") ==
                      "live-x-carrier-crosses-control-flow" &&
                  dead_integer_selector->details.contains("fractionalPartUseRole") &&
                  dead_integer_selector->details.at("fractionalPartUseRole") ==
                      "constant-data" &&
                  dead_integer_selector->details.contains("deadIntegerProofRequiredArtifact") &&
                  dead_integer_selector->details.at("deadIntegerProofRequiredArtifact") ==
                      "control-successor-x-liveness-or-erase-before-consumer" &&
                  dead_integer_selector->details.contains("deadIntegerConsumerRegister") &&
                  dead_integer_selector->details.at("deadIntegerConsumerRegister") == "7" &&
                  dead_integer_selector->details.contains("requiredAction") &&
                  dead_integer_selector->details.at("requiredAction") ==
                      "prove-control-successor-erases-live-x-or-erase-before-use" &&
                  dead_integer_selector->details.contains("proofEffortPriority") &&
                  dead_integer_selector->details.at("proofEffortPriority") ==
                      "defer-until-size-positive" &&
                  dead_integer_selector->details.contains("proofEffortReason") &&
                  dead_integer_selector->details.at("proofEffortReason") ==
                      "candidate-larger-than-current-before-proof" &&
                  dead_integer_selector->details.contains("sizeFirstAction") &&
                  dead_integer_selector->details.at("sizeFirstAction") ==
                      "find-size-positive-candidate-shape-before-proof",
              "cave-treasure dead-integer selector attribution should split consumer and target "
              "proof context fields and defer proof work for size-negative candidates");
    }
    if (name == "functions-demo") {
      const CompileResult result = compile_example(path, /*analysis_budgeted=*/true);
      const SizeHelperSummaryReport* sum_of_squares_stack_entry =
          find_size_helper(result, "sum_of_squares stack entry");
      require(sum_of_squares_stack_entry == nullptr,
              "functions-demo should not attribute a fully inlined sum_of_squares body as a "
              "remaining helper");
      const SizeSelectedOptimizationReport* stack_entry =
          find_size_selected_optimization(result, "stack-resident-function-entries");
      require(stack_entry != nullptr && stack_entry->current_steps == 13 &&
                  stack_entry->baseline_steps == 19 && stack_entry->savings == 6 &&
                  stack_entry->details.contains("estimateKind") &&
                  stack_entry->details.at("estimateKind") == "measured-selected-candidate-delta",
              "functions-demo value-aware attribution should report the selected stack-entry "
              "function ABI as a measured size win");
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
                      "0" &&
                  roll_die->details.contains("valueAwareSchedulerPlanStatus") &&
                  roll_die->details.at("valueAwareSchedulerPlanStatus") ==
                      "requires-persistent-state-store",
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
                  front_stop->details.contains("valueAwareSchedulerPlanStatus") &&
                  front_stop->details.at("valueAwareSchedulerPlanStatus") ==
                      "break-even-after-stack-preservation" &&
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
    record_size_mismatch("pending example", name, expected, result.steps.size());
  }
  require(size_mismatches.empty(), "example size mismatches: " + size_mismatches);
}

} // namespace mkpro::tests
