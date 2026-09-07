#pragma once

#include "mkpro/core/feature_profile.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace mkpro {

enum class AddressSpaceModel {
  Standard,
  Mk61SMiniExpanded,
};

enum class FormalAddressKind {
  Official,
  ShortSide,
  LongSide,
  Dark,
  SuperDark,
};

struct FormalAddressInfo {
  int opcode = 0;
  std::string label;
  int ordinal = 0;
  int actual = 0;
  FormalAddressKind kind = FormalAddressKind::Official;
  bool one_command = false;
  std::optional<int> extra;
};

AddressSpaceModel address_space_model_for_feature_profile(FeatureProfile profile);
int official_program_step_limit(AddressSpaceModel model = AddressSpaceModel::Standard);
int official_program_last_address(AddressSpaceModel model = AddressSpaceModel::Standard);
int formal_address_ordinal(int opcode);
// Increment the calculator's encoded decimal counter, retaining its side
// branch. Physical projection depends on the selected model: A5..B1 are
// ordinary cells in the 112-cell profile and short-side aliases on stock MK-61.
// A non-decimal low nibble is carried into the next canonical counter.
int formal_address_successor_opcode(int opcode);
int official_address_to_opcode(int address,
                               AddressSpaceModel model = AddressSpaceModel::Standard);
std::string format_formal_address_opcode(int opcode);
std::string format_official_address(int address,
                                    AddressSpaceModel model = AddressSpaceModel::Standard);
std::optional<int> parse_formal_address_opcode(std::string_view text);
FormalAddressInfo formal_address_info(int opcode,
                                      AddressSpaceModel model = AddressSpaceModel::Standard);
std::string formal_address_kind_name(FormalAddressKind kind);

}  // namespace mkpro
