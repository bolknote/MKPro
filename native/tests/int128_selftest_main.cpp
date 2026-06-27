// Standalone self-test for the software 128-bit integer fallback.
//
// Built (and run via CTest as "int128_selftest") on platforms without a builtin
// __int128 such as MSVC, and in the forced-fallback CI job where GCC/Clang are
// compiled with MKPRO_FORCE_INT128_FALLBACK so the differential check against
// the builtin still runs.

#include "int128_selftest.hpp"

#include <exception>
#include <iostream>

int main() {
  try {
    mkpro::int128_test::run_int128_checks();
  } catch (const std::exception& error) {
    std::cerr << "int128 self-test FAILED: " << error.what() << '\n';
    return 1;
  }
  std::cout << "int128 self-test OK\n";
  return 0;
}
