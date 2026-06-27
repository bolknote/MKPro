#include "mkpro/compiler.hpp"
#include "mkpro/core/format.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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

// Native oracle "bless" mode. When MKPRO_NATIVE_BLESS=1 is set, the oracle
// contract tests re-anchor the committed oracle files to the current NATIVE
// compiler output instead of asserting equality. This is how the native oracle
// becomes the source of truth after the TypeScript-parity constraint is lifted.
// The mode is a no-op when the environment variable is unset.
void write_oracle_file(const std::filesystem::path& path, const std::string& content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error("cannot write native oracle: " + path.string());
  output << content;
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

std::string token_line(const std::vector<ResolvedStep>& steps) {
  std::string value = format_program_tokens(steps);
  std::replace(value.begin(), value.end(), '\n', ' ');
  return value;
}

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

bool env_flag_enabled(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr)
    return false;
  const std::string text_value = value;
  return !text_value.empty() && text_value != "0" && text_value != "false" &&
         text_value != "FALSE";
}

bool verify_variant_fingerprints() {
  return env_flag_enabled("MKPRO_NATIVE_VERIFY_VARIANT_FINGERPRINTS") ||
         env_flag_enabled("CI");
}

std::string variant_fingerprint(const std::string& source, const CompileOptions& base_options) {
  std::ostringstream lines;
  for (const auto& variant : kLoweringVariants) {
    CompileOptions options = base_options;
    options.disable_candidate_search = true;
    variant.configure(options);
    const CompileResult result = compile_source(source, options);
    if (!result.diagnostics.empty()) {
      lines << variant.name << ": throws ";
      for (std::size_t index = 0; index < result.diagnostics.size(); ++index) {
        if (index > 0)
          lines << '\n';
        lines << result.diagnostics.at(index).message;
      }
      lines << '\n';
      continue;
    }
    require(result.implemented, std::string(variant.name) + " lowering variant should compile");
    const std::string hex = token_line(result.steps);
    std::string setup;
    if (result.setup_program.has_value()) {
      setup = token_line(result.setup_program->steps);
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

std::string example_oracle_id(const std::filesystem::path& source_path,
                              const std::filesystem::path& pending_root) {
  if (source_path.parent_path() == pending_root)
    return "pending-optimizer/" + source_path.stem().string();
  return source_path.stem().string();
}

std::string first_different_line(const std::string& actual, const std::string& expected) {
  std::istringstream actual_lines(actual);
  std::istringstream expected_lines(expected);
  std::string actual_line;
  std::string expected_line;
  int line = 1;
  while (true) {
    const bool has_actual = static_cast<bool>(std::getline(actual_lines, actual_line));
    const bool has_expected = static_cast<bool>(std::getline(expected_lines, expected_line));
    if (!has_actual && !has_expected)
      return "";
    if (!has_actual || !has_expected || actual_line != expected_line) {
      std::ostringstream out;
      out << " at line " << line << "; actual='" << (has_actual ? actual_line : "<missing>")
          << "' expected='" << (has_expected ? expected_line : "<missing>") << "'";
      return out.str();
    }
    ++line;
  }
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

  const bool verify_variants = verify_variant_fingerprints();
  const bool progress = std::getenv("MKPRO_NATIVE_EXAMPLE_PROGRESS") != nullptr;
  const bool bless = std::getenv("MKPRO_NATIVE_BLESS") != nullptr;
  const char* filter_env = std::getenv("MKPRO_NATIVE_EXAMPLE_FILTER");
  const std::string filter = filter_env != nullptr ? filter_env : "";
  std::size_t progress_index = 0;
  for (const std::filesystem::path& source_path : example_files) {
    const std::string name = example_oracle_id(source_path, pending_root);
    if (!filter.empty() && name.find(filter) == std::string::npos)
      continue;
    if (progress) {
      ++progress_index;
      std::cerr << "[golden-listing] " << progress_index << "/" << example_files.size() << " "
                << name << std::endl;
    }
    const std::filesystem::path oracle_example_root = oracle_root / name;

    const std::string source = read_text(source_path);
    const CompileResult result = compile_source(source, base_options);

    require(result.implemented, "golden listing should compile example: " + name);
    require(result.diagnostics.empty(), "golden listing example should not report diagnostics: " + name);

    const std::string setup_program =
        result.setup_program.has_value() ? format_program_tokens(result.setup_program->steps) : "";
    const std::string actual_variants = variant_fingerprint(source, base_options);

    if (bless) {
      // Re-anchor the committed oracle files to native output. We rewrite the
      // content-checked artifacts (listing/hex/setup/variants) plus the cheap
      // space-joined bytes view. keys.txt has no native formatter and is left
      // untouched (it is existence-checked only, not content-checked).
      write_oracle_file(oracle_example_root / "listing.txt", result.listing + "\n");
      write_oracle_file(oracle_example_root / "hex.txt", result.hex + "\n");
      write_oracle_file(oracle_example_root / "setup.txt",
                        setup_program.empty() ? std::string() : setup_program + "\n");
      write_oracle_file(oracle_example_root / "variants.txt", actual_variants + "\n");
      write_oracle_file(oracle_example_root / "bytes.txt", token_line(result.steps) + "\n");
      if (progress) {
        std::cerr << "[golden-listing] blessed " << name << " (" << result.steps.size()
                  << " steps)" << std::endl;
      }
      continue;
    }

    const std::string expected_listing = trim_newlines(read_text(oracle_example_root / "listing.txt"));
    require(result.listing == expected_listing,
            "example listing should match the committed native oracle for " + name +
                first_different_line(result.listing, expected_listing));
    const std::string expected_hex = trim_newlines(read_text(oracle_example_root / "hex.txt"));
    require(result.hex == expected_hex,
            "example hex should match the committed native oracle for " + name +
                first_different_line(result.hex, expected_hex));
    const std::string expected_setup = trim_newlines(read_text(oracle_example_root / "setup.txt"));
    require(setup_program == expected_setup,
            "example setup listing should match the committed native oracle for " + name +
                first_different_line(setup_program, expected_setup));
    const std::string expected_variants =
        trim_newlines(read_text(oracle_example_root / "variants.txt"));
    require(actual_variants == expected_variants,
            "example lowering variants should match the committed native oracle for " + name +
                first_different_line(actual_variants, expected_variants));
  if (verify_variants) {
    const std::string actual_variants = variant_fingerprint(source, base_options);
    const std::string expected_variants =
        trim_newlines(read_text(oracle_example_root / "variants.txt"));
    require(actual_variants == expected_variants,
            "example lowering variants should match TS oracle for " + name +
                first_different_line(actual_variants, expected_variants));
  }
  }
}

} // namespace mkpro::tests
