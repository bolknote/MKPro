#include "mkpro/compiler.hpp"
#include "mkpro/core/format.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
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
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
    value.pop_back();
  return value;
}

using VariantConfigurator = void (*)(CompileOptions&);

void configure_primary(CompileOptions&) {}
void configure_aggressive_terminal_direct(CompileOptions& options) { options.aggressive_terminal_direct = true; }
void configure_invert_branch_order(CompileOptions& options) { options.invert_branch_order = true; }
void configure_hoist_shared_helpers(CompileOptions& options) { options.hoist_shared_helpers = true; }
void configure_hoist_shared_helpers_and_procs(CompileOptions& options) {
  options.hoist_shared_helpers = true;
  options.hoist_procs = true;
}
void configure_canonicalize_if_chains(CompileOptions& options) { options.canonicalize_if_chains = true; }
void configure_free_residual_dispatch_scratch(CompileOptions& options) {
  options.free_residual_dispatch_scratch = true;
}
void configure_alias_x_reuse(CompileOptions& options) { options.alias_x_reuse = true; }
void configure_coalesce_copies(CompileOptions& options) { options.coalesce_copies = true; }
void configure_free_residual_dispatch_scratch_and_canonicalize_if_chains(CompileOptions& options) {
  options.free_residual_dispatch_scratch = true;
  options.canonicalize_if_chains = true;
}
void configure_repeated_unary_update_arg_temp(CompileOptions& options) {
  options.canonicalize_repeated_unary_update_args = true;
}
void configure_x_param_value_function(CompileOptions& options) { options.x_param_value_functions = true; }
void configure_x_param_value_function_and_repeated_unary_update_arg_temp(
    CompileOptions& options) {
  options.x_param_value_functions = true;
  options.canonicalize_repeated_unary_update_args = true;
}
void configure_x_param_value_function_repeated_unary_update_arg_temp_and_coalesce_copies(
    CompileOptions& options) {
  options.x_param_value_functions = true;
  options.canonicalize_repeated_unary_update_args = true;
  options.coalesce_copies = true;
}
void configure_share_random_cell(CompileOptions& options) { options.share_random_cell = true; }
void configure_share_random_cell_and_hoist_shared_helpers(CompileOptions& options) {
  options.share_random_cell = true;
  options.hoist_shared_helpers = true;
}
void configure_tail_branch_inversion(CompileOptions& options) { options.tail_branch_inversion = true; }
void configure_hoist_shared_helpers_canonicalize_if_chains_tail_branch_inversion(
    CompileOptions& options) {
  options.hoist_shared_helpers = true;
  options.canonicalize_if_chains = true;
  options.tail_branch_inversion = true;
}
void configure_guarded_prologue_gadgets(CompileOptions& options) { options.guarded_prologue_gadgets = true; }
void configure_guarded_prologue_gadgets_with_shared_helpers_and_procs(CompileOptions& options) {
  options.guarded_prologue_gadgets = true;
  options.hoist_shared_helpers = true;
  options.hoist_procs = true;
}
void configure_shared_bit_mask_helper_calls(CompileOptions& options) {
  options.shared_bit_mask_helper_calls = true;
}
void configure_shared_bit_mask_helper_calls_with_shared_helpers(CompileOptions& options) {
  options.shared_bit_mask_helper_calls = true;
  options.hoist_shared_helpers = true;
}

struct LoweringVariant {
  const char* name;
  VariantConfigurator configure;
};

const std::vector<LoweringVariant> kLoweringVariants = {
    {"primary", configure_primary},
    {"aggressiveTerminalDirect", configure_aggressive_terminal_direct},
    {"invertBranchOrder", configure_invert_branch_order},
    {"aggressiveTerminalDirect+invertBranchOrder",
     [](CompileOptions& options) {
       configure_aggressive_terminal_direct(options);
       configure_invert_branch_order(options);
     }},
    {"hoistSharedHelpers", configure_hoist_shared_helpers},
    {"hoistSharedHelpers+hoistProcs", configure_hoist_shared_helpers_and_procs},
    {"canonicalizeIfChains", configure_canonicalize_if_chains},
    {"freeResidualDispatchScratch", configure_free_residual_dispatch_scratch},
    {"aliasXReuse", configure_alias_x_reuse},
    {"coalesceCopies", configure_coalesce_copies},
    {"freeResidualDispatchScratch+canonicalizeIfChains",
     configure_free_residual_dispatch_scratch_and_canonicalize_if_chains},
    {"repeatedUnaryUpdateArgTemp", configure_repeated_unary_update_arg_temp},
    {"xParamValueFunction", configure_x_param_value_function},
    {"xParamValueFunction+repeatedUnaryUpdateArgTemp",
     configure_x_param_value_function_and_repeated_unary_update_arg_temp},
    {"xParamValueFunction+repeatedUnaryUpdateArgTemp+coalesceCopies",
     configure_x_param_value_function_repeated_unary_update_arg_temp_and_coalesce_copies},
    {"shareRandomCell", configure_share_random_cell},
    {"shareRandomCell+hoistSharedHelpers", configure_share_random_cell_and_hoist_shared_helpers},
    {"tailBranchInversion", configure_tail_branch_inversion},
    {"hoistSharedHelpers+canonicalizeIfChains+tailBranchInversion",
     configure_hoist_shared_helpers_canonicalize_if_chains_tail_branch_inversion},
    {"guardedPrologueGadgets", configure_guarded_prologue_gadgets},
    {"guardedPrologueGadgets+hoistSharedHelpers+hoistProcs",
     configure_guarded_prologue_gadgets_with_shared_helpers_and_procs},
    {"sharedBitMaskHelperCalls", configure_shared_bit_mask_helper_calls},
    {"sharedBitMaskHelperCalls+hoistSharedHelpers",
     configure_shared_bit_mask_helper_calls_with_shared_helpers},
};

std::vector<std::filesystem::path> collect_examples(const std::filesystem::path& dir) {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".mkpro") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::string variant_fingerprint(const std::string& source, const CompileOptions& base_options) {
  std::ostringstream lines;
  for (const auto& variant : kLoweringVariants) {
    CompileOptions options = base_options;
    variant.configure(options);
    const CompileResult result = compile_source(source, options);
    require(result.implemented, std::string(variant.name) + " lowering variant should compile");
    if (!result.diagnostics.empty()) {
      throw std::runtime_error(std::string(variant.name) + " lowering variant reported diagnostics");
    }
    const std::string hex = format_program_tokens(result.steps);
    std::string setup;
    if (result.setup_program.has_value()) {
      setup = format_program_tokens(result.setup_program->steps);
    }
    lines << variant.name << ": steps=" << result.steps.size() << " | " << hex;
    if (!setup.empty()) {
      lines << " || setup " << setup;
    }
    lines << '\n';
  }

  std::string value = lines.str();
  if (!value.empty()) {
    value.pop_back();
  }
  return value;
}

} // namespace

void golden_listing_contract_matches_typescript_contract() {
  // Traceability: tests/compiler/golden-listing.test.ts
  const std::filesystem::path root = std::filesystem::current_path();
  const std::filesystem::path examples_root = root / "examples";
  const std::filesystem::path pending_root = root / "examples" / "pending-optimizer";
  const std::filesystem::path oracle_root = root / "native" / "oracles" / "examples";

  std::vector<std::filesystem::path> example_files;
  {
    auto examples = collect_examples(examples_root);
    example_files.insert(example_files.end(), examples.begin(), examples.end());
  }
  {
    auto pending_examples = collect_examples(pending_root);
    example_files.insert(example_files.end(), pending_examples.begin(), pending_examples.end());
  }

  const CompileOptions base_options = [] {
    CompileOptions options;
    options.analysis = true;
    options.budget = 999999;
    return options;
  }();

  for (const std::filesystem::path& source_path : example_files) {
    const std::string name = source_path.stem().string();
    const std::filesystem::path oracle_example_root = oracle_root / name;

    const std::string source = read_text(source_path);
    const CompileResult result = compile_source(source, base_options);

    require(result.implemented, "golden listing should compile example: " + name);
    require(result.diagnostics.empty(), "golden listing example should not report diagnostics: " + name);

    require(result.listing == trim_newlines(read_text(oracle_example_root / "listing.txt")),
            "example listing should match the TS oracle for " + name);
    require(result.hex == trim_newlines(read_text(oracle_example_root / "hex.txt")),
            "example hex should match the TS oracle for " + name);
    require(result.setup_listing == trim_newlines(read_text(oracle_example_root / "setup.txt")),
            "example setup listing should match the TS oracle for " + name);
    require(variant_fingerprint(source, base_options) == trim_newlines(read_text(oracle_example_root / "variants.txt")),
            "example lowering variants should match TS oracle for " + name);
  }
}

} // namespace mkpro::tests
