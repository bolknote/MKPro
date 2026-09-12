#include "mkpro/compiler.hpp"

#include "test_support.hpp"
#include "example_selection.hpp"

#include <algorithm>
#include <array>
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

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& optimization) {
                       return optimization.name == name;
                     });
}

// Candidate presence, addresses and register allocation change with valid
// lowering/proof improvements. Validate the context of every reported control
// opportunity instead of requiring one obsolete candidate from a named game.
void require_selector_opportunity_context(const CompileResult& result) {
  for (const auto& opportunity : result.size_attribution.opportunities) {
    if (opportunity.variant != "fractional-constant-selector-dead-int" ||
        opportunity.blocker_kind != "indirect-address-control-use")
      continue;
    const auto& details = opportunity.details;
    for (const char* key : {
             "consumerAddress", "selectorTarget", "fractionalSelectorConsumer",
             "consumerControlKind", "fractionalSelectorSourceRegister",
             "deadIntegerSelectorCarrierRegister", "integerPartUseRole",
             "fractionalPartUseRole", "deadIntegerProofRequiredArtifact",
             "deadIntegerConsumerRegister", "requiredAction", "proofEffortPriority",
             "proofEffortReason", "sizeFirstAction"}) {
      require(details.contains(key) && !details.at(key).empty(),
              std::string("control-selector opportunity lacks proof context: ") + key);
    }
    for (const char* key : {"consumerAddress", "selectorTarget"}) {
      const auto& address = details.at(key);
      require(std::all_of(address.begin(), address.end(),
                          [](char ch) { return ch >= '0' && ch <= '9'; }),
              std::string("control-selector address must be an explicit numeric identity: ") + key);
    }
    for (const char* key : {"fractionalSelectorSourceRegister",
                            "deadIntegerSelectorCarrierRegister", "deadIntegerConsumerRegister"}) {
      const auto& reg = details.at(key);
      require(reg.size() == 1U && std::string("0123456789abcdef").find(reg) != std::string::npos,
              std::string("control-selector context must name its actual register: ") + key);
    }
    if (opportunity.candidate_steps > opportunity.current_steps) {
      require(details.at("proofEffortPriority") == "defer-until-size-positive" &&
                  details.at("proofEffortReason") == "candidate-larger-than-current-before-proof" &&
                  details.at("sizeFirstAction") == "find-size-positive-candidate-shape-before-proof",
              "a size-negative control-selector opportunity must defer expensive proof work");
    }
  }
}

void require_selector_data_opportunity_context(const CompileResult& result) {
  for (const auto& opportunity : result.size_attribution.opportunities) {
    const auto& details = opportunity.details;
    const auto failure = details.find("proofFailure");
    if (failure == details.end() || failure->second != "selector-register-used-as-data")
      continue;
    for (const char* key : {"proofFamily", "missingProof", "selectorRegister",
                             "consumerAddress", "consumerOpcodeHex",
                             "selectorDataConflictKind", "selectorDataConflictPrecision"}) {
      require(details.contains(key) && !details.at(key).empty(),
              std::string("selector/data overlap must retain its actual proof context: ") + key);
    }
    require(opportunity.blocker_kind == "static-proof-gate" &&
                details.at("proofFamily") == "indirect-flow-targets" &&
                details.at("missingProof") == "selector-register-preservation",
            "selector/data overlap must remain a proof rejection, not an accepted saving");
    require(opportunity.current_steps == static_cast<int>(result.steps.size()) &&
                opportunity.savings == opportunity.current_steps - opportunity.candidate_steps,
            "selector/data cost accounting must use the actual selected layout");
    if (details.contains("selectorDataPayloadMinPackedAccessOverheadCells")) {
      for (const char* key : {"selectorDataPayloadPackingOverheadBudgetCells",
                               "selectorDataPayloadPackingNetLowerBoundCells",
                               "estimatedCandidateStepsAfterPayloadPackingLowerBound"})
        require(details.contains(key),
                std::string("payload packing lacks lower-bound accounting: ") + key);
      const int overhead = std::stoi(details.at("selectorDataPayloadMinPackedAccessOverheadCells"));
      const int budget = std::stoi(details.at("selectorDataPayloadPackingOverheadBudgetCells"));
      const int estimate = opportunity.current_steps - budget + overhead;
      require(overhead >= 0 && budget >= 0 &&
                  std::stoi(details.at("selectorDataPayloadPackingNetLowerBoundCells")) ==
                      budget - overhead &&
                  std::stoi(details.at("estimatedCandidateStepsAfterPayloadPackingLowerBound")) ==
                      estimate && opportunity.candidate_steps == estimate,
              "payload packing must charge its complete access-cost lower bound");
    }
    if (opportunity.savings <= 0)
      require(std::none_of(result.size_attribution.next_actions.begin(),
                           result.size_attribution.next_actions.end(), [&](const auto& action) {
                             return action.best_site == opportunity.site &&
                                    action.best_variant == opportunity.variant;
                           }),
              "a nonpositive selector/data estimate must not rank as a profitable next action");
  }
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
  if (std::getenv("MKPRO_NATIVE_EXAMPLE_SIZE_ONLY") == nullptr) {
    require_selector_opportunity_context(result);
    require_selector_data_opportunity_context(result);
  }
  return result;
}

std::size_t example_steps(const std::filesystem::path& path, bool analysis_budgeted) {
  return compile_example(path, analysis_budgeted).steps.size();
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
      {"cave-highlevel-baseline", 103},
      {"cave-sketch", 38},
      {"cave-treasure", 105},
      {"clock", 33}, // The incremented counter needs a value recall after D0..D6.
      {"dangerous-loading", 87},
      {"dungeon", 75},
      {"e-94-digits", 64},
      {"functions-demo", 13},
      {"fox-hunt-100", 103},
      {"fox-hunt-mk61", 65},
      {"game-100-pig", 103},
      {"giants-country", 103}, // Preserve the same indirect-counter value contract.
      {"human", 27},
      {"jack-pot", 94},
      {"labyrinth777", 105},
      {"lunar", 44},
      {"minesweeper-9x7", 76},
      {"minesweeper-9x9", 76},
      {"raja-yoga", 85},
      {"rambo-iii", 103},
      {"river-battle", 90},
      {"sea-battle", 67},
      {"teleport", 96},
      {"tic-tac-toe", 100},
      {"tiny-game", 23},
      {"treasure-hunter-2", 103},
      {"wumpus", 105},
      {"zagaday-tsifru", 105},
  };
  const std::map<std::string, std::size_t> PENDING_BASELINE{
      {"nekromant", 138},
      {"tic-tac-toe-4x4", 139},
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
  require(example_matches_selection("tic-tac-toe", "tic-tac-toe", nullptr) &&
              !example_matches_selection("pending-optimizer/tic-tac-toe-4x4",
                                         "tic-tac-toe", nullptr) &&
              example_matches_selection("pending-optimizer/tic-tac-toe-4x4",
                                        nullptr, "tic-tac-toe") &&
              !example_matches_selection("clock", "human", "clock") &&
              example_matches_selection("clock", nullptr, nullptr),
          "exact example selection must take priority over substring filtering");
  const auto included = [&](const std::string& name, bool pending = false) {
    return example_selected(pending ? "pending-optimizer/" + name : name);
  };
  std::size_t progress_index = 0;
  const std::size_t progress_total = static_cast<std::size_t>(
      std::count_if(EXAMPLE_BASELINE.begin(), EXAMPLE_BASELINE.end(),
                    [&](const auto& entry) { return included(entry.first); }) +
      std::count_if(PENDING_BASELINE.begin(), PENDING_BASELINE.end(),
                    [&](const auto& entry) { return included(entry.first, true); }));
  require(progress_total != 0U, "no examples matched the requested size-baseline selection");
  std::string size_mismatches;
  const auto record_size_mismatch = [&](const std::string& category, const std::string& name,
                                        std::size_t expected, std::size_t actual) {
    if (progress)
      std::cerr << "[example-size-result] " << category << " " << name
                << " expected=" << expected << " actual=" << actual << std::endl;
    if (actual == expected)
      return;
    if (!size_mismatches.empty())
      size_mismatches += "; ";
    size_mismatches += category + " " + name + " expected=" + std::to_string(expected) +
                       " actual=" + std::to_string(actual);
  };
  for (const auto& [name, expected] : EXAMPLE_BASELINE) {
    if (!included(name)) continue;
    if (progress) {
      ++progress_index;
      std::cerr << "[example-size] " << progress_index << "/" << progress_total << " " << name
                << std::endl;
    }
    const std::filesystem::path path = examples_root / (name + ".mkpro");
    const std::size_t actual = example_steps(path, /*analysis_budgeted=*/false);
    // Hardware capacity is independent of regenerated exact-size snapshots.
    require(actual <= 105U, "standard MK-61 example exceeds 105 cells: " + name +
                               " actual=" + std::to_string(actual));
    record_size_mismatch("top-level example", name, expected, actual);
    if (size_only)
      continue;
    if (name == "zagaday-tsifru") {
      const CompileResult result = compile_example(path, /*analysis_budgeted=*/true);
      require(result.steps.size() == 105U,
              "corrected zagaday-tsifru semantic source should fit in addresses 00..A4");
      require(!has_optimization(result, "borrowed-entry-phase-selector"),
              "zagaday-tsifru must not borrow R9 only for its first iteration: input overwrites "
              "R9 before the program loops back to the same branch");
      require(has_optimization(result, "packed-bcd-horner-threshold-loop"),
              "zagaday-tsifru should select the proved packed-BCD majority threshold");
      require(std::any_of(result.steps.begin(), result.steps.end(), [](const ResolvedStep& step) {
                return step.opcode == 0x1c;
              }) &&
                  std::none_of(result.steps.begin(), result.steps.end(),
                               [](const ResolvedStep& step) { return step.opcode == 0x3b; }),
              "final zagaday-tsifru learning should contain F sin and no K random command");
      require(std::none_of(result.steps.begin(), result.steps.end(), [](const ResolvedStep& step) {
                return step.comment == "set correction" || step.comment == "recall correction";
              }),
              "corrected zagaday-tsifru should keep the deterministic correction in X");
      require(std::none_of(result.steps.begin(), result.steps.end(),
                           [](const ResolvedStep& step) {
                             return step.comment == "packed BCD remove anchor";
                           }),
              "biased threshold lowering should divide its anchored fold by 19 without an "
              "extra anchor-removal pair");
      require(std::any_of(result.items.begin(), result.items.end(), [](const MachineItem& item) {
                return item.indirect_memory_targets == std::optional<std::vector<int>>{{1, 2, 3}};
              }),
              "packed BCD indexed weights should retain exact typed memory-target proof facts");

      int resumable_stops = 0;
      int prompt_anchors = 0;
      int single_step_anchors = 0;
      int continuous_resume_anchors = 0;
      for (const MachineItem& item : result.items) {
        if (item.kind != MachineItemKind::Op)
          continue;
        if (item.opcode == 0x50 && item.stop_disposition == StopDisposition::Resumable)
          ++resumable_stops;
        if (!item.manual_interaction.has_value())
          continue;
        switch (item.manual_interaction->kind) {
        case ManualInteractionAnchorKind::PromptStop:
          ++prompt_anchors;
          break;
        case ManualInteractionAnchorKind::SingleStepCommand:
          ++single_step_anchors;
          break;
        case ManualInteractionAnchorKind::ContinuousResume:
          ++continuous_resume_anchors;
          break;
        }
      }
      const std::array<std::string, 5> expected_ui_comments{
          "display __inline_show_64_0 source", "show __inline_show_64_0", "set player",
          "display __inline_show_66_1 source", "show __inline_show_66_1"};
      bool expected_ui_sequence = false;
      for (std::size_t start = 0; start + expected_ui_comments.size() <= result.steps.size();
           ++start) {
        if (std::equal(expected_ui_comments.begin(), expected_ui_comments.end(),
                       result.steps.begin() + static_cast<std::ptrdiff_t>(start),
                       [](const std::string& comment, const ResolvedStep& step) {
                         return step.comment == comment;
                       })) {
          expected_ui_sequence = true;
          break;
        }
      }
      require(resumable_stops == 2 && prompt_anchors == 1 && single_step_anchors == 0 &&
                  continuous_resume_anchors == 1 && expected_ui_sequence &&
                  result.interaction_protocols.size() == 1U &&
                  result.interaction_protocols.front().phases.size() == 1U &&
                  result.interaction_protocols.front().phases.front().admitted_domain.minimum ==
                      0 &&
                  result.interaction_protocols.front().phases.front().admitted_domain.maximum ==
                      7,
              "zagaday-tsifru semantic source should show a positive octal prediction after "
              "bounded input");

      std::string unsafe_source = read_file(path);
      const std::string proved_threshold = "return int((ones + 8) / 19)";
      const std::size_t threshold = unsafe_source.find(proved_threshold);
      require(threshold != std::string::npos,
              "zagaday-tsifru fixture should contain the biased threshold under test");
      unsafe_source.replace(threshold, proved_threshold.size(),
                            "return int((ones + 12) / 19)");
      CompileOptions unsafe_options;
      unsafe_options.analysis = true;
      unsafe_options.budget = 999999;
      unsafe_options.disable_candidate_search = true;
      const CompileResult unsafe_bias = compile_source(unsafe_source, unsafe_options);
      require(unsafe_bias.implemented && !has_error_diagnostic(unsafe_bias) &&
                  !has_optimization(unsafe_bias, "packed-bcd-horner-threshold-loop"),
              "packed BCD threshold recognition should reject a bias whose effective threshold "
              "is not a proved majority bit");
    }
    if (name == "fox-hunt-mk61") {
      const CompileResult result = compile_example(path, /*analysis_budgeted=*/true);
      require(std::any_of(result.items.begin(), result.items.end(), [](const MachineItem& item) {
                return item.kind == MachineItemKind::Op && (item.opcode & 0xf0) == 0xd0 &&
                       item.indirect_memory_targets.has_value() &&
                       item.indirect_memory_targets->size() == 9U &&
                       std::all_of(item.indirect_memory_targets->begin(),
                                   item.indirect_memory_targets->end(),
                                   [](int target) { return target >= 0 && target <= 0x0e; });
              }),
              "the coordinate-list read must carry its complete typed nine-register payload");
      // The unsafe 60-cell artifact is no longer generated. Its former report
      // is not an optimization contract; actual overlap opportunities are
      // checked generically, and the synthetic ROM/data-flow test exercises
      // the prohibition even when search never constructs an invalid layout.
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
                  !front_stop->details.contains("valueAwareMixedStateNames") &&
                  front_stop->details.contains("valueAwareRegisterTrafficNames") &&
                  front_stop->details.at("valueAwareRegisterTrafficNames") ==
                      "cells_7,random_state" &&
                  front_stop->details.contains("valueAwareSchedulerPlanStatus") &&
                  front_stop->details.at("valueAwareSchedulerPlanStatus") ==
                      "requires-persistent-state-store" &&
                  front_stop->details.contains("valueAwareStateOutputNames") &&
                  front_stop->details.at("valueAwareStateOutputNames") == "cells_7" &&
                  front_stop->details.contains("valueAwareNestedCallInputNames") &&
                  front_stop->details.at("valueAwareNestedCallInputNames") == "random_state",
              "rambo-iii front_stop attribution should exclude register traffic already removed "
              "by generic forwarding and retain the persistent output/nested-input split");
      const SizeOpportunityReport* front_stop_register_traffic =
          find_size_opportunity_detail(result, "helper-register-traffic", "helperLabel",
                                       "front_stop");
      require(front_stop_register_traffic != nullptr &&
                  front_stop_register_traffic->savings == 3 &&
                  front_stop_register_traffic->candidate_steps + 3 ==
                      static_cast<int>(result.steps.size()) &&
                  front_stop_register_traffic->details.contains("savingsModel") &&
                  front_stop_register_traffic->details.at("savingsModel") ==
                      "gross-helper-register-traffic-before-callsite-proof" &&
                  front_stop_register_traffic->details.contains("candidateStepsStatus") &&
                  front_stop_register_traffic->details.at("candidateStepsStatus") ==
                      "synthetic-upper-bound-not-compiled" &&
                  front_stop_register_traffic->details.contains("sizeImpactStatus") &&
                  front_stop_register_traffic->details.at("sizeImpactStatus") ==
                      "blocked-unmeasured" &&
                  front_stop_register_traffic->details.contains("netSavingsStatus") &&
                  front_stop_register_traffic->details.at("netSavingsStatus") ==
                      "unproved-before-callsite-stack-proof" &&
                  front_stop_register_traffic->details.contains("trafficShapeAction") &&
                  front_stop_register_traffic->details.at("trafficShapeAction") ==
                      "split-stack-inputs-from-deferred-state-outputs",
              "rambo-iii should keep the remaining front_stop traffic visible as a proof-gated "
              "upper bound after generic forwarding");
      const SizeNextActionSummaryReport* mixed_state_action = find_size_next_action(
          result, "trafficShapeAction", "split-stack-inputs-from-deferred-state-outputs");
      require(mixed_state_action != nullptr,
              "rambo-iii should rank the remaining proof-gated stack/state split as the next "
              "scheduler action");
    }
  }

  for (const auto& [name, expected] : PENDING_BASELINE) {
    if (!included(name, true)) continue;
    if (progress) {
      ++progress_index;
      std::cerr << "[example-size] " << progress_index << "/" << progress_total << " " << name
                << std::endl;
    }
    const std::filesystem::path path = pending_root / (name + ".mkpro");
    const CompileResult result = compile_example(path, /*analysis_budgeted=*/true);
    record_size_mismatch("pending example", name, expected, result.steps.size());
    if (size_only)
      continue;
    if (name == "tic-tac-toe-4x4") {
      const SizeHelperSummaryReport* packed_score =
          find_size_helper(result, "packed_score accumulator helper");
      require(packed_score != nullptr && packed_score->body_cells == 8 &&
                  !packed_score->details.contains("valueAwareMixedStateTempCarrierNames"),
              "tic-tac-toe packed_score attribution must stop at its straight-line return "
              "instead of absorbing caller loop state");
      const SizeHelperSummaryReport* mark_lines =
          find_size_helper(result, "mark_lines_and_check");
      require(mark_lines != nullptr, "tic-tac-toe should retain its line-update helper");
      const auto& mark_details = mark_lines->details;
      const auto detail_has_name = [&](const std::string& key, const std::string& field) {
        const auto found = mark_details.find(key);
        return found != mark_details.end() &&
               ("," + found->second + ",").find("," + field + ",") != std::string::npos;
      };
      const bool control_crossing =
          mark_details.contains("valueAwareMixedStateControlCrossingNames") &&
          mark_details.at("valueAwareMixedStateControlCrossingNames") == "best_score" &&
          mark_details.contains("valueAwareMixedStateLifetimeStatus") &&
          mark_details.at("valueAwareMixedStateLifetimeStatus") ==
              "crosses-control-flow-or-external-entry" &&
          mark_details.contains("valueAwareSchedulerPlanStatus") &&
          mark_details.at("valueAwareSchedulerPlanStatus") ==
              "control-crossing-state-not-stack-carrier";
      // The ordinary-call layout stores the sign here and reads it in mark_one;
      // the callee-hole layout additionally exposes a mixed control-crossing
      // lifetime. Both require persistent storage, not a removable local value.
      const bool nested_input =
          mark_details.contains("valueAwareNestedCallInputNames") &&
          mark_details.at("valueAwareNestedCallInputNames") == "best_score" &&
          mark_details.contains("valueAwareSchedulerPlanStatus") &&
          (mark_details.at("valueAwareSchedulerPlanStatus") ==
               "blocked-by-stack-mutating-callee" ||
           (mark_details.at("valueAwareSchedulerPlanStatus") ==
                "nested-call-inputs-not-direct-scheduler-savings" &&
            mark_details.contains("valueAwareSchedulerTrafficShape") &&
            mark_details.at("valueAwareSchedulerTrafficShape") ==
                "nested-call-inputs-only" &&
            mark_details.contains("valueAwareEstimatedNetSavingsExcludes") &&
            mark_details.at("valueAwareEstimatedNetSavingsExcludes") ==
                "persistent-nested-call-input-stores"));
      // The faithful response protocol adds a persistent output to this helper.
      // Its report can now discuss other stack inputs (such as Y) as well as
      // the sign. A positive gross estimate for those inputs is not permission
      // to remove best_score: the complete callee-ABI cost still blocks it.
      const bool nested_input_with_output =
          detail_has_name("valueAwareNestedCallInputNames", "best_score") &&
          detail_has_name("valueAwareStateOutputNames", "display_x") &&
          !detail_has_name("valueAwareProfitableStackInputNames", "best_score") &&
          !detail_has_name("valueAwareSuggestedResidentInputNames", "best_score") &&
          mark_details.contains("valueAwareStateOutputPlanStatus") &&
          mark_details.at("valueAwareStateOutputPlanStatus") == "requires-persistent-state-store" &&
          mark_details.contains("valueAwareSchedulerPlanStatus") &&
          ((mark_details.at("valueAwareSchedulerPlanStatus") == "callee-abi-lower-bound-not-positive" &&
            mark_details.contains("valueAwareCalleeAbiNetAfterLowerBoundCells") &&
            std::stoi(mark_details.at("valueAwareCalleeAbiNetAfterLowerBoundCells")) <= 0) ||
           (mark_details.at("valueAwareSchedulerPlanStatus") ==
                "no-profitable-stack-input-materialization" &&
            mark_details.contains("valueAwareProfitableStackInputCount") &&
            mark_details.at("valueAwareProfitableStackInputCount") == "0" &&
            mark_details.contains("valueAwareEstimatedNetSavingsAfterMaterialization") &&
            std::stoi(mark_details.at("valueAwareEstimatedNetSavingsAfterMaterialization")) <= 0) ||
           (mark_details.at("valueAwareSchedulerPlanStatus") == "blocked-by-stack-mutating-callee" &&
            mark_details.contains("valueAwareEstimatedNetSavingsAfterMaterialization") &&
            std::stoi(mark_details.at("valueAwareEstimatedNetSavingsAfterMaterialization")) <= 0));
      const bool old_nonpositive_model =
          (control_crossing || nested_input) &&
          mark_details.contains("valueAwareEstimatedNetSavingsAfterMaterialization") &&
          std::stoi(mark_details.at("valueAwareEstimatedNetSavingsAfterMaterialization")) <= 0;
      require((old_nonpositive_model || nested_input_with_output) &&
                  !detail_has_name("valueAwareMixedStateTempCarrierNames", "best_score"),
              "tic-tac-toe best_score must remain persistent across nested calls "
              "instead of being reported as a removable local stack carrier");
      const SizeOpportunityReport* mark_lines_traffic =
          find_size_opportunity_detail(result, "helper-register-traffic", "helperLabel",
                                       "mark_lines_and_check");
      require(mark_lines_traffic != nullptr && mark_lines_traffic->savings <= 0 &&
                  mark_lines_traffic->details.contains("sizeImpactStatus") &&
                  mark_lines_traffic->details.at("sizeImpactStatus") ==
                      "estimated-nonpositive-net",
              "tic-tac-toe persistent best_score traffic must remain a nonpositive-saving "
              "blocker instead of a positive optimizer opportunity");
    }
    if (name == "nekromant") {
      const SizeHelperSummaryReport* draw = find_size_helper(result, "draw stack entry");
      require(draw != nullptr &&
                  draw->details.contains("valueAwareMixedStateRequiredUpdateNames") &&
                  draw->details.at("valueAwareMixedStateRequiredUpdateNames")
                          .find("random_state") != std::string::npos &&
                  draw->details.contains(
                      "valueAwareEstimatedNetSavingsAfterMaterialization") &&
                  draw->details.at(
                      "valueAwareEstimatedNetSavingsAfterMaterialization") == "0" &&
                  !draw->details.contains("valueAwareMixedStateTempCarrierNames"),
              "nekromant random_state recall-before-store must remain among persistent RNG "
              "updates");
      const SizeOpportunityReport* draw_traffic =
          find_size_opportunity_detail(result, "helper-register-traffic", "helperLabel",
                                       "draw stack entry");
      require(draw_traffic != nullptr && draw_traffic->savings == 0 &&
                  draw_traffic->details.contains("sizeImpactStatus") &&
                  draw_traffic->details.at("sizeImpactStatus") ==
                      "estimated-nonpositive-net",
              "nekromant RNG state update must not be ranked as removable register traffic");
    }
  }
  require(size_mismatches.empty(), "example size mismatches: " + size_mismatches);
}

} // namespace mkpro::tests
