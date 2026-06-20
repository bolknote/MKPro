#include "mkpro/oracle.hpp"

#include <filesystem>
#include <stdexcept>

namespace mkpro::tests {

void oracle_index_loads_committed_artifacts() {
  const std::filesystem::path root = std::filesystem::current_path();
  const std::filesystem::path oracle_root = root / "native" / "oracles" / "examples";
  const OracleIndex index = load_oracle_index(oracle_root / "index.json");
  verify_oracle_artifacts(index, oracle_root);

  if (index.example_ids.size() < 30) {
    throw std::runtime_error("oracle index contains fewer examples than expected");
  }
}

}  // namespace mkpro::tests
