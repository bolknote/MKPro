#pragma once

#include <stdexcept>
#include <string>

namespace mkpro::tests {

inline void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

}  // namespace mkpro::tests
