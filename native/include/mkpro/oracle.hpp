#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mkpro {

struct OracleIndex {
  int schema_version = 0;
  std::string compiler_hash;
  std::vector<std::string> example_ids;
  std::vector<std::filesystem::path> artifact_paths;
};

OracleIndex load_oracle_index(const std::filesystem::path& index_path);
void verify_oracle_artifacts(const OracleIndex& index, const std::filesystem::path& root);

}  // namespace mkpro
