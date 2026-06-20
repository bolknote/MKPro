#include "mkpro/oracle.hpp"

#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

namespace mkpro {

namespace {

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open oracle file: " + path.string());
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

int extract_schema_version(const std::string& text) {
  const std::regex schema_regex(R"json("schemaVersion"\s*:\s*([0-9]+))json");
  std::smatch match;
  if (!std::regex_search(text, match, schema_regex)) {
    throw std::runtime_error("oracle index does not contain schemaVersion");
  }
  return std::stoi(match[1].str());
}

std::string extract_compiler_hash(const std::string& text) {
  const std::regex hash_regex(R"json("compilerHash"\s*:\s*"([^"]+)")json");
  std::smatch match;
  if (!std::regex_search(text, match, hash_regex)) {
    throw std::runtime_error("oracle index does not contain compilerHash");
  }
  return match[1].str();
}

std::vector<std::string> extract_example_ids(const std::string& text) {
  const std::regex id_regex(R"json("id"\s*:\s*"([^"]+)")json");
  std::vector<std::string> ids;
  for (auto it = std::sregex_iterator(text.begin(), text.end(), id_regex);
       it != std::sregex_iterator(); ++it) {
    ids.push_back((*it)[1].str());
  }
  return ids;
}

std::vector<std::filesystem::path> extract_artifact_paths(const std::string& text) {
  const std::regex artifact_regex(R"json("\w+"\s*:\s*"([^"]+\.(txt|json))")json");
  std::vector<std::filesystem::path> artifacts;
  for (auto it = std::sregex_iterator(text.begin(), text.end(), artifact_regex);
       it != std::sregex_iterator(); ++it) {
    artifacts.emplace_back((*it)[1].str());
  }
  return artifacts;
}

}  // namespace

OracleIndex load_oracle_index(const std::filesystem::path& index_path) {
  const std::string text = read_text_file(index_path);
  OracleIndex index;
  index.schema_version = extract_schema_version(text);
  index.compiler_hash = extract_compiler_hash(text);
  index.example_ids = extract_example_ids(text);
  index.artifact_paths = extract_artifact_paths(text);

  if (index.schema_version != 1) {
    throw std::runtime_error("unsupported oracle schema version: " +
                             std::to_string(index.schema_version));
  }
  if (index.compiler_hash.empty()) {
    throw std::runtime_error("oracle compiler hash is empty");
  }
  if (index.example_ids.empty()) {
    throw std::runtime_error("oracle index does not contain examples");
  }
  if (index.artifact_paths.empty()) {
    throw std::runtime_error("oracle index does not contain artifact paths");
  }
  return index;
}

void verify_oracle_artifacts(const OracleIndex& index, const std::filesystem::path& root) {
  for (const auto& artifact : index.artifact_paths) {
    const std::filesystem::path path = root / artifact;
    if (!std::filesystem::is_regular_file(path)) {
      throw std::runtime_error("missing oracle artifact: " + path.string());
    }
    (void)read_text_file(path);
  }
}

}  // namespace mkpro
