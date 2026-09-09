#pragma once

#include <cstdlib>
#include <string_view>

namespace mkpro::tests {

inline bool example_matches_selection(std::string_view name, const char* exact,
                                      const char* substring) {
  if (exact != nullptr)
    return name == exact;
  return substring == nullptr || name.find(substring) != std::string_view::npos;
}

inline bool example_selected(std::string_view name) {
  return example_matches_selection(name, std::getenv("MKPRO_NATIVE_EXAMPLE_EXACT"),
                                    std::getenv("MKPRO_NATIVE_EXAMPLE_FILTER"));
}

} // namespace mkpro::tests
