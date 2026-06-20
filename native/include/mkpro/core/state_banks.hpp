#pragma once

#include "mkpro/core/ast.hpp"

#include <optional>
#include <string>

namespace mkpro::core {

struct AffineIndexIdentifierOffset {
  std::string name;
  int offset = 0;
  bool integer_part = false;
};

std::string bank_member_key(const std::string& base, const std::optional<std::string>& field);
std::string bank_selector_variable_name(const std::string& base,
                                        const std::optional<std::string>& field);
std::string state_bank_key(const V2StateField& field);
std::string state_bank_element_name(const V2StateField& field, int index);
std::optional<int> numeric_index_value(const Expression& expression);
std::optional<AffineIndexIdentifierOffset> affine_index_identifier_offset(
    const Expression& expression);

}  // namespace mkpro::core
