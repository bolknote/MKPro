#pragma once

#include "mkpro/core/ir.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mkpro {

struct SuperDarkDispatchCell {
  int address = 0;
  int opcode = 0;
  std::string register_name;
  std::string tactic;
  std::optional<std::string> selector_value;
};

struct SuperDarkLayoutPair {
  int formal = 0;
  int entry_address = 0;
  int continuation_address = 0;
  int entry_opcode = 0;
  int continuation_opcode = 0;
};

struct SuperDarkLayoutProof {
  bool proved = false;
  std::vector<SuperDarkLayoutPair> pairs;
  std::vector<SuperDarkDispatchCell> dispatch_cells;
  std::vector<std::string> reasons;
};

struct SuperDarkLayoutOptions {
  std::map<std::string, std::string> selector_values;
};

SuperDarkLayoutProof verify_super_dark_suffix_layout(
    const std::vector<LayoutIrCell>& layout,
    const SuperDarkLayoutOptions& options = {});

} // namespace mkpro
