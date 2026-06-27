#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

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

std::string trim_newlines(std::string value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
    value.pop_back();
  }
  return value;
}

bool has_optimization(const CompileResult& result, const std::string& name) {
  for (const auto& item : result.optimizations) {
    if (item.name == name)
      return true;
  }
  return false;
}

bool has_proof(const CompileResult& result, const std::string& id) {
  return std::any_of(result.proofs.begin(), result.proofs.end(), [&](const ProofReport& proof) {
    return proof.id == id;
  });
}

bool has_machine_feature(const CompileResult& result, const std::string& id) {
  return std::any_of(result.machine_features_used.begin(), result.machine_features_used.end(),
                     [&](const MachineFeatureUseReport& feature) { return feature.id == id; });
}

bool has_capability(const CompileResult& result, const std::string& id) {
  return std::any_of(result.optimizer.capabilities.begin(), result.optimizer.capabilities.end(),
                     [&](const OptimizerCapabilityReport& capability) {
                       return capability.id == id;
                     });
}

std::string capability_status(const CompileResult& result, const std::string& id) {
  const auto it =
      std::find_if(result.optimizer.capabilities.begin(), result.optimizer.capabilities.end(),
                   [&](const OptimizerCapabilityReport& capability) {
                     return capability.id == id;
                   });
  return it == result.optimizer.capabilities.end() ? "" : it->status;
}

const CandidateReport* find_candidate(const std::vector<CandidateReport>& candidates,
                                      const std::string& variant) {
  const auto it = std::find_if(candidates.begin(), candidates.end(),
                               [&](const CandidateReport& candidate) {
                                 return candidate.variant == variant;
                               });
  return it == candidates.end() ? nullptr : &*it;
}

bool warnings_contain(const CompileResult& result, const std::string& text) {
  return std::any_of(result.warnings.begin(), result.warnings.end(),
                     [&](const std::string& warning) {
                       return warning.find(text) != std::string::npos;
                     });
}

std::string replace_reference_line(std::string source, const std::string& replacement) {
  const std::regex reference_line(R"(^\s*reference\s+\S+.*$)", std::regex_constants::multiline);
  return std::regex_replace(source, reference_line, replacement,
                            std::regex_constants::format_first_only);
}

std::string remove_reference_line(std::string source) {
  const std::regex reference_line(R"(^\s*reference\s+\S+.*\n?)",
                                  std::regex_constants::multiline);
  return std::regex_replace(source, reference_line, "", std::regex_constants::format_first_only);
}

} // namespace

void compiler_examples_match_typescript_contract() {
  // Traceability:
  // - tests/compiler.test.ts
  // - tests/compiler/examples.test.ts
  const std::filesystem::path root = std::filesystem::current_path();

  CompileOptions baseline_options;
  baseline_options.analysis = true;

  {
    const CompileResult result =
        compile_source(read_text(root / "examples" / "basic.mkpro"), baseline_options);
    require(result.implemented, "compiler examples contract should compile basic.mkpro");
    require(result.diagnostics.empty(), "basic.mkpro compile should be warning-free");
    require(result.ir.v2, "basic.mkpro should report the V2 surface");
    require(result.ir.lowered, "basic.mkpro should report lowered IR");
    require(result.optimizer.automatic, "basic.mkpro should report automatic optimizer rules");
    require(has_proof(result, "value-ranges"), "basic.mkpro should report value-range proofs");
    require(has_optimization(result, "intent-read-lowering"),
            "basic.mkpro should report intent-read-lowering");
    require(result.listing.find("read x") != std::string::npos,
            "basic.mkpro should use the read field path in listing");
    const std::string expected_hex = trim_newlines(
        read_text(root / "native" / "oracles" / "examples" / "basic" / "hex.txt"));
    require(result.hex == expected_hex, "basic.mkpro hex should match the committed native oracle");
  }

  {
    CompileOptions tiny_options;
    tiny_options.analysis = true;
    tiny_options.budget = 999;
    const CompileResult result =
        compile_source(read_text(root / "examples" / "tiny-game.mkpro"), tiny_options);
    require(result.implemented, "compiler examples contract should compile tiny-game.mkpro");
    require(result.diagnostics.empty(), "tiny-game compile should be warning-free");
    require(has_optimization(result, "ephemeral-input-dispatch"),
            "tiny-game should use ephemeral input dispatch");
    require(result.steps.size() <= 105, "tiny-game should stay within the TS step budget");
    require(!result.hex.empty(), "tiny-game should emit non-empty hex");
    const CandidateReport* selected_dispatch =
        find_candidate(result.candidates, "fallthrough-compare-chain");
    require(selected_dispatch != nullptr && selected_dispatch->selected,
            "tiny-game should report the selected fallthrough dispatch candidate");
    require(selected_dispatch->reason.find("key-based dispatch") != std::string::npos,
            "tiny-game dispatch candidate should explain the key-based selection");
    const CandidateReport* rejected_indirect =
        find_candidate(result.rejected_candidates, "indirect-register-flow");
    require(rejected_indirect != nullptr,
            "tiny-game should report rejected indirect-register dispatch");
    require(rejected_indirect->reason.find("key-valued, not address-valued") !=
                std::string::npos,
            "tiny-game rejected dispatch should report the TS reason");
    require(result.optimizer.active > 0, "tiny-game optimizer report should have active entries");
    for (const OptimizerCapabilityReport& capability : result.optimizer.capabilities) {
      require(capability.status != "planned" || !capability.detail.empty(),
              "planned optimizer capability should carry detail");
      require(capability.detail.find("unsafe") == std::string::npos,
              "optimizer capability details should not expose unsafe fields");
    }
  }

  {
    CompileOptions lunar_options;
    lunar_options.analysis = true;
    lunar_options.budget = 999;
    const CompileResult result = compile_source(read_text(root / "examples" / "lunar.mkpro"), lunar_options);
    require(result.implemented, "compiler examples contract should compile lunar.mkpro");
    require(result.diagnostics.empty(), "lunar.mkpro compile should be warning-free");
    require(result.listing.find("show __inline_show_") != std::string::npos,
            "lunar.mkpro should use inline show strategies");
    require(result.steps.size() <= 105, "lunar.mkpro should stay within the TS step budget");
    require(has_optimization(result, "stack-current-x-scheduling"),
            "lunar.mkpro should report stack-current-x-scheduling");
    require(capability_status(result, "stack-current-x-scheduling") == "active",
            "stack-current-x-scheduling capability should become active");
  }

  {
    const std::string source = read_text(root / "examples" / "e-94-digits.mkpro");
    const std::regex forbidden{R"((raw|listing|machine|decimal|e_digits))", std::regex_constants::icase};
    require(!std::regex_search(source, forbidden), "e-94-digits should not include forbidden TS directives");
    CompileOptions e94_options;
    e94_options.analysis = true;
    e94_options.budget = 999;
    const CompileResult result = compile_source(source, e94_options);
    require(result.implemented, "compiler examples contract should compile e-94-digits.mkpro");
    require(result.diagnostics.empty(), "e-94-digits compile should be warning-free");
    require(has_optimization(result, "decimal-series-lowering"),
            "e-94-digits should use decimal series lowering");
    const std::string expected_hex =
        trim_newlines(read_text(root / "native" / "oracles" / "examples" / "e-94-digits" / "hex.txt"));
    require(result.hex == expected_hex,
            "e-94-digits hex should match the committed native oracle");
  }

  {
    const std::string source = read_text(root / "examples" / "human.mkpro");
    const CompileResult result = compile_source(source, baseline_options);
    require(result.implemented, "compiler examples contract should compile human.mkpro");
    require(result.ir.v2, "human.mkpro should report V2 IR");
    require(result.ir.lowered, "human.mkpro should report lowered IR");
    require(result.steps.size() == 27, "human.mkpro should keep the TS step count");
    require(has_optimization(result, "ephemeral-input-dispatch"),
            "human.mkpro should report ephemeral input dispatch");
    require(has_optimization(result, "indirect-incdec-counter"),
            "human.mkpro should report indirect increment/decrement counter lowering");
    require(has_optimization(result, "terminal-loop-screen-elision"),
            "human.mkpro should report terminal loop screen elision");
    require(has_capability(result, "x2-display-register"),
            "human.mkpro should report the x2-display-register optimizer capability");
    require(has_capability(result, "r0-alias-indirect"),
            "human.mkpro should report the r0-alias-indirect optimizer capability");
    require(has_capability(result, "super-dark-dispatch"),
            "human.mkpro should report the super-dark-dispatch optimizer capability");
    require(has_machine_feature(result, "code-data-overlay"),
            "human.mkpro should report code-data-overlay machine feature use");
    require(has_proof(result, "value-ranges"), "human.mkpro should report value-range proofs");
    // The TS oracle surfaces, for human.mkpro, a "numeric-dispatch-residual-chain" optimization
    // label plus "dark-indirect-table" / "super-dark-dispatch" considered-but-rejected dispatch
    // candidates (mirroring selectDispatchCandidate). These are report-only entries; the emitted
    // code stays byte-identical (the golden_listing gate is byte-exact).
    require(has_optimization(result, "numeric-dispatch-residual-chain"),
            "human.mkpro should report the numeric-dispatch-residual-chain dispatch lowering");
    const CandidateReport* rejected_dark_table =
        find_candidate(result.rejected_candidates, "dark-indirect-table");
    require(rejected_dark_table != nullptr,
            "human.mkpro should report the rejected dark-indirect-table dispatch candidate");
    require(rejected_dark_table->reason.find(
                "layout proof did not establish a conflict-free address/data table") !=
                std::string::npos,
            "human.mkpro dark-indirect-table rejection should report the TS layout-proof reason");
    const CandidateReport* rejected_super_dark =
        find_candidate(result.rejected_candidates, "super-dark-dispatch");
    require(rejected_super_dark != nullptr,
            "human.mkpro should report the rejected super-dark-dispatch dispatch candidate");
    require(rejected_super_dark->reason.find(
                "did not place one-command cases at 48..53 with tails at 01..06") !=
                std::string::npos,
            "human.mkpro super-dark-dispatch rejection should report the TS layout-proof reason");
  }

  {
    const std::string source = read_text(root / "examples" / "99-bottles.mkpro");
    const CompileResult original = compile_source(source, baseline_options);
    const CompileResult renamed =
        compile_source(replace_reference_line(source, "reference renamed_metadata_only"),
                       baseline_options);
    const CompileResult anonymous = compile_source(remove_reference_line(source), baseline_options);

    require(original.implemented && renamed.implemented && anonymous.implemented,
            "reference metadata variants should compile");
    require(original.hex == renamed.hex,
            "renaming reference metadata must not affect emitted bytecode");
    require(original.hex == anonymous.hex,
            "removing reference metadata must not affect emitted bytecode");
    require(original.reference.has_value(), "99-bottles should report reference metadata");
    require(original.reference->name == "bolknote_99_bottles",
            "99-bottles reference name should match TS contract");
    require(original.reference->reference_span == 53,
            "99-bottles reference span should match TS contract");
    require(original.reference->parity != "larger",
            "99-bottles should remain no larger than its reference");
    require(renamed.reference.has_value() &&
                renamed.reference->name == "renamed_metadata_only",
            "renamed reference should be report-only metadata");
    require(warnings_contain(renamed, "was not found under games"),
            "renamed reference should warn about missing source reference");
    require(!anonymous.reference.has_value(), "anonymous source should omit reference report");
  }

  {
    const std::string source = read_text(root / "examples" / "dangerous-loading.mkpro");
    const CompileResult result = compile_source(source, baseline_options);
    require(result.implemented, "dangerous-loading.mkpro should compile");
    require(result.steps.size() == 84, "dangerous-loading step count should match TS");
    require(result.reference.has_value(), "dangerous-loading should report reference metadata");
    require(result.reference->reference_span == 103,
            "dangerous-loading reference span should match TS contract");
    require(has_optimization(result, "dispatch-default-merge"),
            "dangerous-loading should report dispatch-default-merge");
    require(has_optimization(result, "tail-call-lowering"),
            "dangerous-loading should report tail-call-lowering");
    const CandidateReport* default_merge =
        find_candidate(result.candidates, "dispatch-default-merge");
    require(default_merge != nullptr && default_merge->selected,
            "dispatch-default-merge should be visible as a selected report candidate");
  }

  {
    const std::string compiler_source = read_text(root / "native" / "src" / "core" /
                                                  "compiler.cpp");
    require(compiler_source.find("is_named_example_shape") == std::string::npos,
            "native compiler must not contain named-example bytecode shortcuts");
    require(compiler_source.find("CaveHighlevelBaseline") == std::string::npos,
            "native compiler must not special-case cave-highlevel-baseline bytecode");
    require(compiler_source.find("cave_treasure") == std::string::npos,
            "native compiler must not contain cave_treasure-specific codegen");
  }
}

} // namespace mkpro::tests
