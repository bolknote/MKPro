#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <filesystem>
#include <fstream>
#include <regex>
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

} // namespace

void compiler_examples_match_typescript_contract() {
  // Traceability: tests/compiler/examples.test.ts
  const std::filesystem::path root = std::filesystem::current_path();

  CompileOptions baseline_options;
  baseline_options.analysis = true;

  {
    const CompileResult result =
        compile_source(read_text(root / "examples" / "basic.mkpro"), baseline_options);
    require(result.implemented, "compiler examples contract should compile basic.mkpro");
    require(result.diagnostics.empty(), "basic.mkpro compile should be warning-free");
    require(result.listing.find("read x") != std::string::npos,
            "basic.mkpro should use the read field path in listing");
    const std::string expected_hex = trim_newlines(
        read_text(root / "native" / "oracles" / "examples" / "basic" / "hex.txt"));
    require(result.hex == expected_hex, "basic.mkpro hex should match TS oracle");
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
    require(result.hex == expected_hex, "e-94-digits hex should match TS oracle");
  }
}

} // namespace mkpro::tests

