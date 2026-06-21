#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <filesystem>
#include <fstream>
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

std::string rstrip_newlines(std::string value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
    value.pop_back();
  return value;
}

} // namespace

void supported_examples_match_native_oracles() {
  const std::filesystem::path root = std::filesystem::current_path();
  const std::vector<std::string> supported_examples = {
      "99-bottles",   "basic",          "cave-sketch", "clock", "dungeon",   "e-94-digits",
      "fox-hunt-100", "functions-demo", "human",       "lunar", "tiny-game",
  };

  for (const std::string& name : supported_examples) {
    const std::string source = read_text(root / "examples" / (name + ".mkpro"));
    const std::string oracle_hex =
        rstrip_newlines(read_text(root / "native" / "oracles" / "examples" / name / "hex.txt"));
    const CompileResult result = compile_source(source);
    require(result.implemented, "native compiler should implement example: " + name);
    require(result.diagnostics.empty(), "native example diagnostics should be empty: " + name);
    require(result.hex == oracle_hex, "native example hex mismatch: " + name);
  }
}

} // namespace mkpro::tests
