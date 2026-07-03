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
      require(rejected_dead_integer == nullptr,
              "tic-tac-toe-4x4 should not report the unsafe dead-integer selector once the safe "
              "selector-layout refinement reaches the same size");
      require(result.size_attribution.total_cells == static_cast<int>(result.steps.size()),
              "tic-tac-toe-4x4 size attribution should report the final cell count");
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
                  !cell_mask_helper->details.contains("valueAwareStateOutputNames"),
              "tic-tac-toe-4x4 helper summary should aggregate helper-local register traffic "
              "for value-aware scheduler attribution");
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
                      "exceeds-x-y-z-t-capacity" &&
                  !candidate_score_zero->details.contains("valueAwareStateOutputNames"),
              "tic-tac-toe-4x4 candidate_score helper should classify helper-local traffic as "
              "stack input candidates for the value-aware scheduler");
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
                  mark_lines_helper->details.contains("valueAwareStateOutputNames") &&
                  mark_lines_helper->details.at("valueAwareStateOutputNames")
                          .find("best_score") != std::string::npos &&
                  mark_lines_helper->details.at("valueAwareStateOutputNames").find("slot") !=
                      std::string::npos &&
                  mark_lines_helper->details.contains("valueAwareSchedulerTrafficShape") &&
                  mark_lines_helper->details.at("valueAwareSchedulerTrafficShape") ==
                      "stack-inputs-and-deferred-state-outputs",
              "tic-tac-toe-4x4 mark_lines_and_check helper should classify stack inputs "
              "separately from deferred state outputs");
      const SizeOpportunityReport* cell_mask_register_traffic = find_size_opportunity_detail(
          result, "helper-register-traffic", "helperLabel", "expr cell_mask(x, y)");
      require(cell_mask_register_traffic != nullptr &&
                  cell_mask_register_traffic->site == "helper" &&
                  cell_mask_register_traffic->savings ==
                      cell_mask_helper->register_traffic_cells &&
                  cell_mask_register_traffic->candidate_steps ==
                      static_cast<int>(result.steps.size()) -
                          cell_mask_helper->register_traffic_cells &&
                  cell_mask_register_traffic->blocker_kind ==
                      "value-aware-stack-register-scheduler" &&
                  cell_mask_register_traffic->details.contains("savingsModel") &&
                  cell_mask_register_traffic->details.at("savingsModel") ==
                      "gross-helper-register-traffic-before-callsite-proof" &&
                  cell_mask_register_traffic->details.contains("estimateKind") &&
                  cell_mask_register_traffic->details.at("estimateKind") ==
                      "gross-upper-bound" &&
                  cell_mask_register_traffic->details.contains("candidateStepsStatus") &&
                  cell_mask_register_traffic->details.at("candidateStepsStatus") ==
                      "synthetic-upper-bound-not-compiled" &&
                  cell_mask_register_traffic->details.contains("sizeImpactStatus") &&
                  cell_mask_register_traffic->details.at("sizeImpactStatus") ==
                      "blocked-unmeasured" &&
                  cell_mask_register_traffic->details.contains("netSavingsStatus") &&
                  cell_mask_register_traffic->details.at("netSavingsStatus") ==
                      "unproved-before-callsite-stack-proof" &&
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
              "tic-tac-toe-4x4 size attribution should rank helper-local register traffic as a "
              "gross value-aware scheduler opportunity");
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
                  mark_one_register_traffic->savings == 1 &&
                  mark_one_register_traffic->details.contains("registerTrafficNames") &&
                  mark_one_register_traffic->details.at("registerTrafficNames").find("slot") ==
                      std::string::npos &&
                  mark_one_register_traffic->details.at("registerTrafficNames")
                          .find("best_score") != std::string::npos &&
                  mark_one_register_traffic->details.contains(
                      "selectorBoundRegisterTrafficNames") &&
                  mark_one_register_traffic->details.at("selectorBoundRegisterTrafficNames") ==
                      "slot",
              "tic-tac-toe-4x4 value-aware scheduler opportunity should not count the indirect "
              "selector as removable stack/register traffic");
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
      require(value_aware_scheduler_blocker != nullptr &&
                  value_aware_scheduler_blocker->opportunities >= 1 &&
                  value_aware_scheduler_blocker->potential_savings >=
                      cell_mask_helper->register_traffic_cells &&
                  value_aware_scheduler_blocker->best_savings >=
                      cell_mask_helper->register_traffic_cells &&
                  value_aware_scheduler_blocker->best_variant == "helper-register-traffic" &&
                  value_aware_scheduler_blocker->best_details.contains("estimateKind") &&
                  value_aware_scheduler_blocker->best_details.contains("sizeImpactStatus") &&
                  value_aware_scheduler_blocker->best_details.contains("proofStatus") &&
                  value_aware_scheduler_blocker->best_details.contains("schedulerScope") &&
                  value_aware_scheduler_blocker->best_details.contains("registerTrafficBreakdown") &&
                  value_aware_scheduler_blocker->best_details.contains(
                      "valueAwareStackInputNames") &&
                  value_aware_scheduler_blocker->best_details.contains(
                      "valueAwareSchedulerTrafficShape"),
              "tic-tac-toe-4x4 size attribution should summarize helper-local register traffic "
              "as a value-aware stack/register scheduler blocker");
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
      const SizeNextActionSummaryReport* value_aware_scheduler_action = find_size_next_action(
          result, "requiredAction", "value-aware-stack-register-scheduling");
      require(value_aware_scheduler_action != nullptr &&
                  value_aware_scheduler_action->opportunities >= 1 &&
                  value_aware_scheduler_action->potential_savings >=
                      cell_mask_helper->register_traffic_cells &&
                  value_aware_scheduler_action->best_savings >=
                      cell_mask_helper->register_traffic_cells &&
                  value_aware_scheduler_action->best_blocker_kind ==
                      "value-aware-stack-register-scheduler" &&
                  value_aware_scheduler_action->best_variant == "helper-register-traffic" &&
                  value_aware_scheduler_action->best_details.contains("savingsModel") &&
                  value_aware_scheduler_action->best_details.contains("estimateKind") &&
                  value_aware_scheduler_action->best_details.contains("candidateStepsStatus") &&
                  value_aware_scheduler_action->best_details.contains("sizeImpactStatus") &&
                  value_aware_scheduler_action->best_details.contains("schedulerScope") &&
                  value_aware_scheduler_action->best_details.contains("proofStatus") &&
                  value_aware_scheduler_action->best_details.at("proofStatus") ==
                      "stack-inputs-exceed-x-y-z-t-capacity" &&
                  value_aware_scheduler_action->best_details.contains("registerTrafficBreakdown") &&
                  value_aware_scheduler_action->best_details.contains(
                      "valueAwareStackInputNames") &&
                  value_aware_scheduler_action->best_details.contains(
                      "valueAwareStackCapacityStatus") &&
                  value_aware_scheduler_action->best_details.at(
                      "valueAwareStackCapacityStatus") == "exceeds-x-y-z-t-capacity" &&
                  value_aware_scheduler_action->best_details.contains(
                      "valueAwareSchedulerTrafficShape"),
              "tic-tac-toe-4x4 size attribution should rank value-aware stack/register "
              "scheduling as a next action when helper-local register traffic is visible");
      const SizeNextActionSummaryReport* stack_input_scheduler_action = find_size_next_action(
          result, "trafficShapeAction", "schedule-stack-input-helper-values");
      require(stack_input_scheduler_action != nullptr &&
                  stack_input_scheduler_action->opportunities >= 2 &&
                  stack_input_scheduler_action->potential_savings >=
                      cell_mask_helper->register_traffic_cells &&
                  stack_input_scheduler_action->best_details.contains("helperLabel") &&
                  stack_input_scheduler_action->best_details.at("helperLabel") ==
                      "expr cell_mask(x, y)" &&
                  stack_input_scheduler_action->best_details.contains(
                      "valueAwareSchedulerTrafficShape") &&
                  stack_input_scheduler_action->best_details.at(
                      "valueAwareSchedulerTrafficShape") == "stack-inputs-only",
              "tic-tac-toe-4x4 size attribution should separately rank stack-input-only helper "
              "traffic for the value-aware scheduler");
      const SizeNextActionSummaryReport* staged_stack_input_action = find_size_next_action(
          result, "trafficShapeAction", "split-or-stage-stack-input-helper-values");
      require(staged_stack_input_action != nullptr &&
                  staged_stack_input_action->opportunities >= 1 &&
                  staged_stack_input_action->best_details.contains("helperLabel") &&
                  staged_stack_input_action->best_details.at("helperLabel") ==
                      "candidate_score zero-accumulator entry" &&
                  staged_stack_input_action->best_details.contains(
                      "valueAwareStackCapacityStatus") &&
                  staged_stack_input_action->best_details.at("valueAwareStackCapacityStatus") ==
                      "exceeds-x-y-z-t-capacity",
              "tic-tac-toe-4x4 size attribution should not rank six-input candidate_score as "
              "a direct stack-input scheduling action");
      const SizeNextActionSummaryReport* split_scheduler_action = find_size_next_action(
          result, "trafficShapeAction", "split-stack-inputs-from-deferred-state-outputs");
      require(split_scheduler_action != nullptr &&
                  split_scheduler_action->opportunities >= 1 &&
                  split_scheduler_action->best_details.contains("helperLabel") &&
                  split_scheduler_action->best_details.at("helperLabel") ==
                      "mark_lines_and_check" &&
                  split_scheduler_action->best_details.contains("valueAwareStackInputNames") &&
                  split_scheduler_action->best_details.contains("valueAwareStateOutputNames"),
              "tic-tac-toe-4x4 size attribution should split mixed helper traffic into a "
              "separate scheduler action");
    }
  }
}

} // namespace mkpro::tests
