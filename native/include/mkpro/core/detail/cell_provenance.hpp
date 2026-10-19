#pragma once

#include <algorithm>
#include <string_view>

namespace mkpro::core::detail {

inline constexpr std::string_view kContextualCopyStoreOriginPrefix =
    "contextual-copy-store-origin:";

// Only these complete schemas are descriptive cell origins. A matching
// prefix is not authority to discard a new semantic or malformed role.
inline bool is_cell_origin_provenance(std::string_view role) {
  const auto digits = [](std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char digit) {
      return digit >= '0' && digit <= '9';
    });
  };
  if (role.starts_with(kContextualCopyStoreOriginPrefix))
    return digits(role.substr(kContextualCopyStoreOriginPrefix.size()));
  constexpr std::string_view cell = "finalization-cell-origin:";
  if (role.starts_with(cell)) return digits(role.substr(cell.size()));
  constexpr std::string_view group = "shared-helper-dual-mode-";
  if (!role.starts_with(group)) return false;
  const auto rest = role.substr(group.size());
  const auto separator = rest.find(":origin:");
  return separator != std::string_view::npos && digits(rest.substr(0, separator)) &&
      digits(rest.substr(separator + 8U));
}

} // namespace mkpro::core::detail
