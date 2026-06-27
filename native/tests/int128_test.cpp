#include "int128_selftest.hpp"

namespace mkpro::tests {

void int128_fallback_matches_builtin() {
  mkpro::int128_test::run_int128_checks();
}

} // namespace mkpro::tests
