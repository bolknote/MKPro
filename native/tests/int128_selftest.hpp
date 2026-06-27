#pragma once

// Correctness checks for the software 128-bit integer fallback
// (mkpro::int128_detail::Int128). Shared by the registered unit test and the
// standalone CI self-test binary.
//
// Two layers of coverage:
//   * Fixed vectors with hand-computed decimal answers. These run on every
//     platform, including MSVC where no builtin __int128 oracle exists.
//   * A randomized differential check against the builtin __int128 (only where
//     __SIZEOF_INT128__ is defined, i.e. GCC/Clang/AppleClang). Arithmetic
//     oracles are computed in *unsigned* __int128 so wraparound is well defined
//     (signed overflow would be UB and a useless oracle); two's-complement bit
//     patterns are then compared limb for limb.

#include "mkpro/core/int128.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace mkpro::int128_test {

using Fallback = int128_detail::Int128;

inline void check(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(std::string("int128 fallback: ") + message);
}

inline Fallback make_fallback(std::uint64_t high, std::uint64_t low) {
  return Fallback{int128_detail::UInt128{low, high}};
}

// Decimal rendering using only the fallback's public operators (mirrors the
// production int128_to_string helpers).
inline std::string to_decimal(Fallback value) {
  const Fallback zero(0);
  const Fallback ten(10);
  if (value == zero)
    return "0";
  const bool negative = value < zero;
  Fallback rest = negative ? -value : value;
  std::string digits;
  while (rest > zero) {
    digits.push_back(static_cast<char>('0' + static_cast<int>(rest % ten)));
    rest = rest / ten;
  }
  if (negative)
    digits.push_back('-');
  std::reverse(digits.begin(), digits.end());
  return digits;
}

// Deterministic xorshift64* PRNG (reproducible failures, no <random> needed).
struct Rng {
  std::uint64_t state;
  std::uint64_t next() {
    std::uint64_t x = state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    state = x;
    return x * 0x2545F4914F6CDD1DULL;
  }
};

#if defined(__SIZEOF_INT128__)
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
using Wide = unsigned __int128;
using Signed = __int128;
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

inline std::uint64_t low_limb(Wide value) { return static_cast<std::uint64_t>(value); }
inline std::uint64_t high_limb(Wide value) { return static_cast<std::uint64_t>(value >> 64); }

inline bool same_bits(const Fallback& fallback, Wide builtin_value) {
  return fallback.rep.low == low_limb(builtin_value) &&
         fallback.rep.high == high_limb(builtin_value);
}

inline std::string builtin_to_decimal(Signed value) {
  if (value == 0)
    return "0";
  const bool negative = value < 0;
  Wide magnitude = negative ? (~static_cast<Wide>(value) + 1) : static_cast<Wide>(value);
  std::string digits;
  while (magnitude != 0) {
    digits.push_back(static_cast<char>('0' + static_cast<int>(magnitude % 10)));
    magnitude /= 10;
  }
  if (negative)
    digits.push_back('-');
  std::reverse(digits.begin(), digits.end());
  return digits;
}
#endif

inline void run_int128_checks() {
  using Fb = Fallback;

  // ---- Fixed vectors (every platform, incl. MSVC) ----
  check(to_decimal(Fb(0)) == "0", "fixed zero");
  check(to_decimal(Fb(-1)) == "-1", "fixed -1");
  check(to_decimal(Fb(1234567890123LL)) == "1234567890123", "fixed long long");

  const Fb e18(1000000000000000000LL);
  const Fb e36 = e18 * e18;
  check(to_decimal(e36) == "1000000000000000000000000000000000000", "fixed 1e36");
  check(to_decimal(e36 / Fb(7)) == "142857142857142857142857142857142857", "fixed 1e36/7");
  check(to_decimal(e36 % Fb(7)) == "1", "fixed 1e36%7");
  check(e36 / Fb(7) * Fb(7) + e36 % Fb(7) == e36, "fixed div identity");

  const Fb p120 = Fb(1) << 120;
  check(to_decimal(p120) == "1329227995784915872903807060280344576", "fixed 2^120");
  check(to_decimal(p120 / Fb(2)) == "664613997892457936451903530140172288", "fixed 2^120/2");
  check(p120 % Fb(2) == Fb(0), "fixed 2^120%2");

  // signed truncation toward zero; remainder sign matches dividend
  check(to_decimal(Fb(-7) / Fb(2)) == "-3", "fixed -7/2");
  check(to_decimal(Fb(-7) % Fb(2)) == "-1", "fixed -7%2");
  check(to_decimal(Fb(7) / Fb(-2)) == "-3", "fixed 7/-2");
  check(to_decimal(Fb(7) % Fb(-2)) == "1", "fixed 7%-2");
  check(to_decimal(Fb(-7) / Fb(-2)) == "3", "fixed -7/-2");

  check(Fb(-5) < Fb(3), "fixed cmp neg<pos");
  check(Fb(3) > Fb(-5), "fixed cmp pos>neg");
  check(static_cast<long long>(Fb(-123456789LL)) == -123456789LL, "fixed to_ll");

#if defined(__SIZEOF_INT128__)
  // ---- Randomized differential check vs builtin __int128 ----
  Rng rng{0x9E3779B97F4A7C15ULL};
  const int iterations = 300000;
  for (int i = 0; i < iterations; ++i) {
    std::uint64_t a_high = rng.next();
    std::uint64_t a_low = rng.next();
    std::uint64_t b_high = rng.next();
    std::uint64_t b_low = rng.next();

    const std::uint64_t shape = rng.next();
    if ((shape & 3U) == 0U)
      a_high = 0;  // 64-bit dividend
    if ((shape & 12U) == 0U)
      b_high = 0;  // 64-bit divisor
    if ((shape & 48U) == 0U) {
      b_high = 0;
      b_low = (b_low % 9U) + 2U;  // small divisor 2..10 (covers /10, /2, /5)
    }

    const Wide ua = (static_cast<Wide>(a_high) << 64) | a_low;
    const Wide ub = (static_cast<Wide>(b_high) << 64) | b_low;
    const Signed sa = static_cast<Signed>(ua);
    const Signed sb = static_cast<Signed>(ub);
    const Fb fa = make_fallback(a_high, a_low);
    const Fb fb = make_fallback(b_high, b_low);

    check(same_bits(fa + fb, ua + ub), "rand add");
    check(same_bits(fa - fb, ua - ub), "rand sub");
    check(same_bits(fa * fb, ua * ub), "rand mul");
    check(same_bits(-fa, static_cast<Wide>(0) - ua), "rand neg");

    check((fa < fb) == (sa < sb), "rand lt");
    check((fa > fb) == (sa > sb), "rand gt");
    check((fa <= fb) == (sa <= sb), "rand le");
    check((fa >= fb) == (sa >= sb), "rand ge");
    check((fa == fb) == (sa == sb), "rand eq");
    check((fa != fb) == (sa != sb), "rand ne");

    check(static_cast<long long>(fa) == static_cast<long long>(sa), "rand to_ll");
    check(static_cast<int>(fa) == static_cast<int>(sa), "rand to_int");
    check(to_decimal(fa) == builtin_to_decimal(sa), "rand to_string");

    const unsigned shifts[] = {0U, 1U, 7U, 31U, 63U, 64U, 65U, 96U, 120U, 127U};
    for (const unsigned shift : shifts)
      check(same_bits(fa << static_cast<int>(shift), ua << shift), "rand shl");

    const bool a_is_min = (a_high == 0x8000000000000000ULL && a_low == 0);
    const bool b_is_neg_one = (b_high == ~0ULL && b_low == ~0ULL);
    const bool divisor_zero = (b_high == 0 && b_low == 0);
    if (!divisor_zero && !(a_is_min && b_is_neg_one)) {
      check(same_bits(fa / fb, static_cast<Wide>(sa / sb)), "rand div");
      check(same_bits(fa % fb, static_cast<Wide>(sa % sb)), "rand mod");
    }
  }
#endif
}

}  // namespace mkpro::int128_test
