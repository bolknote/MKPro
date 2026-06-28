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

std::size_t example_steps(const std::filesystem::path& path, bool analysis_budgeted) {
  CompileOptions options;
  if (analysis_budgeted) {
    options.analysis = true;
    options.budget = 999999;
  }
  const CompileResult result = compile_source(read_file(path), options);
  require(result.implemented, "native compiler should implement example: " + path.string());
  require(!has_error_diagnostic(result), "example compile diagnostics should not include errors: " + path.string());
  return result.steps.size();
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
      {"clock", 34},
      {"dangerous-loading", 84},
      {"dungeon", 75},
      {"e-94-digits", 64},
      {"functions-demo", 21},
      {"fox-hunt-100", 103},
      {"fox-hunt-mk61", 65},
      {"game-100-pig", 97},
      {"giants-country", 104},
      {"human", 23},
      {"jack-pot", 96},
      {"labyrinth777", 105},
      {"lunar", 44},
      {"minesweeper-9x7", 79},
      {"minesweeper-9x9", 79},
      {"raja-yoga", 77},
      {"rambo-iii", 103},
      {"river-battle", 95},
      {"sea-battle", 66},
      {"teleport", 96},
      {"tic-tac-toe", 99},
      {"tiny-game", 23},
      {"treasure-hunter-2", 99},
      {"wumpus", 105},
  };
  const std::map<std::string, std::size_t> PENDING_BASELINE{
      {"tic-tac-toe-4x4", 130},
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
    const std::size_t actual = example_steps(path, /*analysis_budgeted=*/true);
    require(actual == expected,
            "pending example " + name + " step count should match TS baseline");
  }
}

} // namespace mkpro::tests
