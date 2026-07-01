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
      const CandidateReport* rejected_dead_integer =
          find_candidate(result.rejected_candidates, "fractional-constant-selector-dead-int");
      require(rejected_dead_integer != nullptr,
              "tic-tac-toe-4x4 should report rejected dead-integer fractional selector rescue");
      require(rejected_dead_integer->steps == 140,
              "tic-tac-toe-4x4 rejected dead-integer fractional selector should show 140 cells");
      require(rejected_dead_integer->reason.find("before K {x}") != std::string::npos,
              "tic-tac-toe-4x4 dead-integer rejection should explain the unsafe consumer");
      require(rejected_dead_integer->reason.find("consumerAddress=") != std::string::npos &&
                  rejected_dead_integer->reason.find("selectorTarget=") != std::string::npos &&
                  rejected_dead_integer->reason.find("naturalTarget=") != std::string::npos,
              "tic-tac-toe-4x4 dead-integer rejection should expose selector/layout context");
      require(result.size_attribution.total_cells == static_cast<int>(result.steps.size()),
              "tic-tac-toe-4x4 size attribution should report the final cell count");
      const SizeAttributionEntry* candidate_score =
          find_size_entry(result, "helper-region", "packed-line score accumulator helper");
      require(candidate_score != nullptr && candidate_score->cells >= 8 &&
                  candidate_score->detail.find("calls=") != std::string::npos,
              "tic-tac-toe-4x4 size attribution should expose the packed_score helper body");
      const SizeAttributionEntry* cell_mask =
          find_size_entry(result, "helper-region", "expr cell_mask(x, y)");
      require(cell_mask != nullptr && cell_mask->cells >= 8,
              "tic-tac-toe-4x4 size attribution should expose the cell_mask helper body");
      const SizeAttributionEntry* cell_mask_calls =
          find_size_entry(result, "helper-call-sites", "expr cell_mask(x, y)");
      require(cell_mask_calls != nullptr && cell_mask_calls->cells == 6 &&
                  cell_mask_calls->occurrences == 3 &&
                  cell_mask_calls->detail.find("target=") != std::string::npos,
              "tic-tac-toe-4x4 size attribution should expose the cell_mask call-site cost");
      const SizeAttributionEntry* packed_score_calls =
          find_size_entry(result, "helper-call-sites", "packed-line score accumulator helper");
      require(packed_score_calls != nullptr && packed_score_calls->cells == 4 &&
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
                  packed_score_helper->details.contains("accumulatorTerms") &&
                  packed_score_helper->details.at("accumulatorTerms") == "4" &&
                  packed_score_helper->details.contains("sharedTailTerms") &&
                  packed_score_helper->details.at("sharedTailTerms") == "4" &&
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
                  stack_helper_abi->details.at("estimatedStackEntryOverheadCells") == "3" &&
                  stack_helper_abi->details.contains("estimatedNetSavings") &&
                  stack_helper_abi->details.at("estimatedNetSavings") == "-1" &&
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
                  stack_helper_abi_opportunity->savings == -1 &&
                  stack_helper_abi_opportunity->candidate_steps ==
                      static_cast<int>(result.steps.size()) + 1 &&
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
                  stack_helper_abi_opportunity->details.contains("costModelAction") &&
                  stack_helper_abi_opportunity->details.at("costModelAction") ==
                      "find-more-stack-entry-call-sites-or-inline-helper" &&
                  stack_helper_abi_opportunity->details.contains("estimatedNetSavings") &&
                  stack_helper_abi_opportunity->details.at("estimatedNetSavings") == "-1" &&
                  stack_helper_abi_opportunity->details.contains("estimatedCandidateSteps") &&
                  stack_helper_abi_opportunity->details.at("estimatedCandidateSteps") ==
                      std::to_string(static_cast<int>(result.steps.size()) + 1) &&
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
      require(dead_integer_opportunity != nullptr && dead_integer_opportunity->savings == 1 &&
                  dead_integer_opportunity->blocker_kind == "data-arithmetic" &&
                  dead_integer_opportunity->reason.find("before K {x}") != std::string::npos &&
                  dead_integer_opportunity->reason.find("consumerAddress=") !=
                      std::string::npos,
              "tic-tac-toe-4x4 size attribution should expose the blocked 140-cell rescue");
      require(dead_integer_opportunity->details.contains("consumerAddress") &&
                  dead_integer_opportunity->details.contains("selectorTarget") &&
                  dead_integer_opportunity->details.contains("naturalTarget") &&
                  dead_integer_opportunity->details.contains("recoveryFreeLayout") &&
                  dead_integer_opportunity->details.contains("fractionalSelectorSource") &&
                  dead_integer_opportunity->details.contains("fractionalSelectorConsumer") &&
                  dead_integer_opportunity->details.contains("consumerKind") &&
                  dead_integer_opportunity->details.contains("integerPartStatus") &&
                  dead_integer_opportunity->details.contains("selectorDataUse") &&
                  dead_integer_opportunity->details.contains("requiredAction") &&
                  dead_integer_opportunity->details.contains("savingsAggregation") &&
                  dead_integer_opportunity->details.contains("proofOnlySavingsStatus") &&
                  dead_integer_opportunity->details.contains("safeSavingsAction") &&
                  dead_integer_opportunity->details.contains("safeSelectorStrategy") &&
                  dead_integer_opportunity->details.contains("safeSelectorTarget") &&
                  dead_integer_opportunity->details.contains("safeSelectorCandidateStatus") &&
                  dead_integer_opportunity->details.contains("safeSelectorCandidateAction") &&
                  dead_integer_opportunity->details.contains("integerPartConsumerOpcode") &&
                  dead_integer_opportunity->details.contains("fractionalEraseOpcode") &&
                  dead_integer_opportunity->details.contains("integerPartHazard") &&
                  dead_integer_opportunity->details.contains("currentNaturalTargetFlowCount") &&
                  dead_integer_opportunity->details.contains("currentNaturalTargetOccupant") &&
                  dead_integer_opportunity->details.contains("currentNaturalTargetOccupantKind") &&
                  dead_integer_opportunity->details.contains("layoutConflictKind") &&
                  dead_integer_opportunity->details.contains("layoutAction"),
              "tic-tac-toe-4x4 size opportunity should expose structured selector-layout "
              "details");
      require(dead_integer_opportunity->details.at("proofDisposition") == "not-proof-only" &&
                  !dead_integer_opportunity->details.at("fractionalSelectorSource").empty() &&
                  !dead_integer_opportunity->details.at("fractionalSelectorConsumer").empty() &&
                  dead_integer_opportunity->details.at("consumerKind") == "data arithmetic" &&
                  dead_integer_opportunity->details.at("selectorDataUse") == "data-arithmetic" &&
                  dead_integer_opportunity->details.at("requiredAction") ==
                      "keep-fractional-erase-before-data-arithmetic" &&
                  dead_integer_opportunity->details.at("savingsAggregation") ==
                      "alternative-candidate" &&
                  dead_integer_opportunity->details.at("proofOnlySavingsStatus") ==
                      "blocked-by-data-arithmetic" &&
                  dead_integer_opportunity->details.at("safeSavingsAction") ==
                      "align-selector-flow-to-natural-target" &&
                  dead_integer_opportunity->details.at("safeSelectorStrategy") ==
                      "recovery-free-natural-fractional-selector" &&
                  dead_integer_opportunity->details.at("safeSelectorTarget") ==
                      dead_integer_opportunity->details.at("naturalTarget") &&
                  dead_integer_opportunity->details.at("safeSelectorCandidateStatus") ==
                      "missing-natural-target-flow" &&
                  dead_integer_opportunity->details.at("safeSelectorCandidateAction") ==
                      "create-natural-target-flow-or-code-data-overlay" &&
                  dead_integer_opportunity->details.at("integerPartConsumerOpcode") ==
                      dead_integer_opportunity->details.at("fractionalSelectorConsumer") &&
                  dead_integer_opportunity->details.at("fractionalEraseOpcode") == "K {x}" &&
                  dead_integer_opportunity->details.at("integerPartHazard") ==
                      "arithmetic-before-fractional-erase" &&
                  dead_integer_opportunity->details.at("currentNaturalTargetFlowCount") == "0" &&
                  dead_integer_opportunity->details.at("currentNaturalTargetOccupant") != "none" &&
                  dead_integer_opportunity->details.at("currentNaturalTargetOccupantKind") !=
                      "none" &&
                  dead_integer_opportunity->details.at("layoutAction") ==
                      "relayout-or-overlay-flow-to-natural-target",
              "tic-tac-toe-4x4 dead-integer arithmetic blocker should say this needs codegen or "
              "layout work instead of proof weakening");
      const bool has_operand_conflict =
          std::any_of(result.size_attribution.opportunities.begin(),
                      result.size_attribution.opportunities.end(),
                      [](const SizeOpportunityReport& opportunity) {
                        if (opportunity.variant != "fractional-constant-selector-dead-int")
                          return false;
                        const auto kind = opportunity.details.find(
                            "currentNaturalTargetOccupantKind");
                        const auto conflict = opportunity.details.find("layoutConflictKind");
                        const auto executable =
                            opportunity.details.find("currentNaturalTargetOperandExecutable");
                        const auto flow_target =
                            opportunity.details.find("currentNaturalTargetOperandFlowTarget");
                        const auto next_executable =
                            opportunity.details.find("currentNaturalTargetNextExecutable");
                        const auto next =
                            opportunity.details.find("currentNaturalTargetNextOccupant");
                        const auto compatibility =
                            opportunity.details.find("currentNaturalTargetOverlayCompatibility");
                        const auto blocker =
                            opportunity.details.find("currentNaturalTargetOverlayBlocker");
                        const auto action =
                            opportunity.details.find("currentNaturalTargetOverlayAction");
                        return kind != opportunity.details.end() && conflict != opportunity.details.end() &&
                               executable != opportunity.details.end() &&
                               flow_target != opportunity.details.end() &&
                               next_executable != opportunity.details.end() &&
                               next != opportunity.details.end() &&
                               compatibility != opportunity.details.end() &&
                               blocker != opportunity.details.end() &&
                               action != opportunity.details.end() &&
                               kind->second == "address-operand" &&
                               conflict->second == "code-data-overlay-candidate" &&
                               compatibility->second == "next-opcode-mismatch" &&
                               blocker->second == "overlaid-opcode-would-retarget-owner-branch" &&
                               action->second ==
                                   "relayout-owner-branch-or-place-compatible-executable";
                      });
      require(has_operand_conflict,
              "tic-tac-toe-4x4 size attribution should classify natural-target address-byte "
              "conflicts as code-data overlay candidates and expose local opcode compatibility");
      const SizeBlockerSummaryReport* data_arithmetic_blocker =
          find_size_blocker(result, "data-arithmetic");
      require(data_arithmetic_blocker != nullptr &&
                  data_arithmetic_blocker->opportunities == 2 &&
                  data_arithmetic_blocker->potential_savings == 1 &&
                  data_arithmetic_blocker->best_savings == 1 &&
                  data_arithmetic_blocker->best_variant ==
                      "fractional-constant-selector-dead-int" &&
                  data_arithmetic_blocker->best_reason.find("before K {x}") !=
                      std::string::npos &&
                  data_arithmetic_blocker->best_reason.find("naturalTarget=") !=
                      std::string::npos,
              "tic-tac-toe-4x4 size attribution should summarize positive-savings proof "
              "blockers by blockerKind");
      require(data_arithmetic_blocker->best_details.contains("naturalTarget") &&
                  data_arithmetic_blocker->best_details.contains("selectorTarget") &&
                  data_arithmetic_blocker->best_details.contains("requiredAction") &&
                  data_arithmetic_blocker->best_details.contains("currentNaturalTargetFlowCount") &&
                  data_arithmetic_blocker->best_details.contains("currentNaturalTargetOccupant") &&
                  data_arithmetic_blocker->best_details.contains("currentNaturalTargetOccupantKind"),
              "tic-tac-toe-4x4 size blocker summary should keep structured best blocker "
              "details");
      const SizeBlockerSummaryReport* stack_helper_abi_blocker =
          find_size_blocker(result, "stack-helper-abi");
      require(stack_helper_abi_blocker == nullptr,
              "tic-tac-toe-4x4 size attribution should not summarize net-negative stack-helper "
              "ABI savings as a positive optimizer blocker");
      const SizeNextActionSummaryReport* required_action = find_size_next_action(
          result, "requiredAction", "keep-fractional-erase-before-data-arithmetic");
      require(required_action != nullptr && required_action->opportunities == 2 &&
                  required_action->potential_savings == 1 &&
                  required_action->best_savings == 1 &&
                  required_action->best_blocker_kind == "data-arithmetic" &&
                  required_action->best_variant == "fractional-constant-selector-dead-int" &&
                  required_action->best_details.contains("requiredAction"),
              "tic-tac-toe-4x4 size attribution should aggregate required compiler actions for "
              "blocked positive-savings candidates");
      const SizeNextActionSummaryReport* layout_action = find_size_next_action(
          result, "layoutAction", "relayout-or-overlay-flow-to-natural-target");
      require(layout_action != nullptr && layout_action->opportunities == 2 &&
                  layout_action->potential_savings == 1 &&
                  layout_action->best_savings == 1 &&
                  layout_action->best_blocker_kind == "data-arithmetic" &&
                  layout_action->best_details.contains("layoutAction") &&
                  layout_action->best_details.contains("layoutConflictKind"),
              "tic-tac-toe-4x4 size attribution should aggregate layout/code-data overlay next "
              "actions for blocked positive-savings candidates");
      const SizeNextActionSummaryReport* safe_savings_action = find_size_next_action(
          result, "safeSavingsAction", "align-selector-flow-to-natural-target");
      require(safe_savings_action != nullptr && safe_savings_action->opportunities == 2 &&
                  safe_savings_action->potential_savings == 1 &&
                  safe_savings_action->best_savings == 1 &&
                  safe_savings_action->best_blocker_kind == "data-arithmetic" &&
                  safe_savings_action->best_details.contains("safeSelectorTarget") &&
                  safe_savings_action->best_details.contains("safeSelectorStrategy") &&
                  safe_savings_action->best_details.contains("safeSelectorCandidateStatus"),
              "tic-tac-toe-4x4 size attribution should aggregate recovery-free natural-target "
              "selector moves separately from proof-only action labels");
      const SizeNextActionSummaryReport* stack_helper_action = find_size_next_action(
          result, "requiredAction", "stack-argument-helper-entry");
      require(stack_helper_action == nullptr,
              "tic-tac-toe-4x4 size attribution should not rank net-negative stack-entry ABI "
              "implementation as a positive next action");
    }
  }
}

} // namespace mkpro::tests
