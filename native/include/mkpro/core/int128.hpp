#ifndef MKPRO_CORE_INT128_HPP
#define MKPRO_CORE_INT128_HPP

// Portable 128-bit integer.
//
// GCC/Clang/AppleClang expose a builtin __int128, which we use directly. MSVC
// has no 128-bit integer type, so a software fallback (mkpro::int128_detail::*)
// is provided. The fallback struct is ALWAYS compiled (on every platform) so it
// stays warning-clean under the project's strict flags and can be cross-checked
// against the builtin in tests. Define MKPRO_FORCE_INT128_FALLBACK to make the
// fallback the active type even where a builtin exists (used by the test build).

#include <cstdint>
#include <type_traits>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace mkpro {

#if defined(__SIZEOF_INT128__) && !defined(MKPRO_FORCE_INT128_FALLBACK)
#define MKPRO_HAS_BUILTIN_INT128 1
#else
#define MKPRO_HAS_BUILTIN_INT128 0
#endif

namespace int128_detail {

struct U64Pair {
  std::uint64_t low = 0;
  std::uint64_t high = 0;
};

// Full 64x64 -> 128 unsigned multiply.
constexpr U64Pair mul_64x64(std::uint64_t a, std::uint64_t b) {
#if defined(_MSC_VER) && defined(_M_X64)
  if (!std::is_constant_evaluated()) {
    std::uint64_t hi = 0;
    const std::uint64_t lo = _umul128(a, b, &hi);
    return U64Pair{lo, hi};
  }
#endif
  const std::uint64_t mask = 0xFFFFFFFFULL;
  const std::uint64_t a0 = a & mask;
  const std::uint64_t a1 = a >> 32;
  const std::uint64_t b0 = b & mask;
  const std::uint64_t b1 = b >> 32;
  const std::uint64_t p00 = a0 * b0;
  const std::uint64_t p01 = a0 * b1;
  const std::uint64_t p10 = a1 * b0;
  const std::uint64_t p11 = a1 * b1;
  const std::uint64_t mid = (p00 >> 32) + (p01 & mask) + (p10 & mask);
  const std::uint64_t lo = (p00 & mask) | (mid << 32);
  const std::uint64_t hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
  return U64Pair{lo, hi};
}

// Unsigned 128-bit, two 64-bit limbs.
struct UInt128 {
  std::uint64_t low = 0;
  std::uint64_t high = 0;

  constexpr UInt128() = default;
  constexpr UInt128(std::uint64_t low_value, std::uint64_t high_value)
      : low(low_value), high(high_value) {}

  static constexpr UInt128 from_u64(std::uint64_t value) { return UInt128{value, 0}; }

  constexpr bool is_zero() const { return low == 0 && high == 0; }

  constexpr UInt128 operator~() const { return UInt128{~low, ~high}; }

  constexpr UInt128 operator-() const {
    const UInt128 inverted = ~*this;
    return inverted + UInt128{1, 0};
  }

  constexpr UInt128 operator+(const UInt128& other) const {
    const std::uint64_t lo = low + other.low;
    const std::uint64_t carry = lo < low ? 1ULL : 0ULL;
    const std::uint64_t hi = high + other.high + carry;
    return UInt128{lo, hi};
  }

  constexpr UInt128 operator-(const UInt128& other) const {
    const std::uint64_t lo = low - other.low;
    const std::uint64_t borrow = low < other.low ? 1ULL : 0ULL;
    const std::uint64_t hi = high - other.high - borrow;
    return UInt128{lo, hi};
  }

  constexpr UInt128 operator*(const UInt128& other) const {
    const U64Pair lo_product = mul_64x64(low, other.low);
    const std::uint64_t hi = lo_product.high + low * other.high + high * other.low;
    return UInt128{lo_product.low, hi};
  }

  constexpr UInt128 operator<<(unsigned shift) const {
    if (shift == 0)
      return *this;
    if (shift >= 128)
      return UInt128{};
    if (shift >= 64)
      return UInt128{0, low << (shift - 64)};
    return UInt128{low << shift, (high << shift) | (low >> (64 - shift))};
  }

  constexpr UInt128 operator>>(unsigned shift) const {
    if (shift == 0)
      return *this;
    if (shift >= 128)
      return UInt128{};
    if (shift >= 64)
      return UInt128{high >> (shift - 64), 0};
    return UInt128{(low >> shift) | (high << (64 - shift)), high >> shift};
  }

  constexpr bool operator==(const UInt128& other) const {
    return low == other.low && high == other.high;
  }
  constexpr bool operator!=(const UInt128& other) const { return !(*this == other); }
  constexpr bool operator<(const UInt128& other) const {
    return high != other.high ? high < other.high : low < other.low;
  }
  constexpr bool operator>(const UInt128& other) const { return other < *this; }
  constexpr bool operator<=(const UInt128& other) const { return !(other < *this); }
  constexpr bool operator>=(const UInt128& other) const { return !(*this < other); }

  constexpr bool bit(unsigned index) const {
    return index < 64 ? ((low >> index) & 1ULL) != 0 : ((high >> (index - 64)) & 1ULL) != 0;
  }
  constexpr void set_bit(unsigned index) {
    if (index < 64)
      low |= (1ULL << index);
    else
      high |= (1ULL << (index - 64));
  }

  // Unsigned division: quotient and remainder via binary long division.
  static constexpr void divmod(const UInt128& numerator, const UInt128& denominator,
                               UInt128& quotient, UInt128& remainder) {
    quotient = UInt128{};
    remainder = UInt128{};
    if (denominator.is_zero())
      return;
    if (numerator.high == 0 && denominator.high == 0) {
      quotient = UInt128{numerator.low / denominator.low, 0};
      remainder = UInt128{numerator.low % denominator.low, 0};
      return;
    }
    for (int index = 127; index >= 0; --index) {
      remainder = remainder << 1U;
      if (numerator.bit(static_cast<unsigned>(index)))
        remainder.low |= 1ULL;
      if (remainder >= denominator) {
        remainder = remainder - denominator;
        quotient.set_bit(static_cast<unsigned>(index));
      }
    }
  }
};

// Signed 128-bit, two's complement over UInt128.
struct Int128 {
  UInt128 rep;

  constexpr Int128() = default;
  constexpr explicit Int128(UInt128 bits) : rep(bits) {}

  template <class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
  constexpr Int128(T value) {  // NOLINT(google-explicit-constructor)
    if constexpr (std::is_signed_v<T>) {
      const long long wide = static_cast<long long>(value);
      rep.low = static_cast<std::uint64_t>(wide);
      rep.high = wide < 0 ? ~0ULL : 0ULL;
    } else {
      rep.low = static_cast<std::uint64_t>(value);
      rep.high = 0;
    }
  }

  constexpr bool is_negative() const { return (rep.high >> 63) != 0; }

  explicit constexpr operator long long() const { return static_cast<long long>(rep.low); }
  explicit constexpr operator unsigned long long() const { return rep.low; }
  explicit constexpr operator int() const { return static_cast<int>(rep.low); }

  constexpr Int128 operator-() const { return Int128{-rep}; }
  constexpr Int128 operator+() const { return *this; }

  constexpr Int128 operator+(const Int128& other) const { return Int128{rep + other.rep}; }
  constexpr Int128 operator-(const Int128& other) const { return Int128{rep - other.rep}; }
  constexpr Int128 operator*(const Int128& other) const { return Int128{rep * other.rep}; }

  static constexpr void divmod(const Int128& a, const Int128& b, Int128& quotient,
                               Int128& remainder) {
    const bool a_negative = a.is_negative();
    const bool b_negative = b.is_negative();
    const UInt128 ua = a_negative ? (-a).rep : a.rep;
    const UInt128 ub = b_negative ? (-b).rep : b.rep;
    UInt128 uq;
    UInt128 ur;
    UInt128::divmod(ua, ub, uq, ur);
    quotient.rep = (a_negative != b_negative) ? -uq : uq;
    remainder.rep = a_negative ? -ur : ur;
  }

  constexpr Int128 operator/(const Int128& other) const {
    Int128 quotient;
    Int128 remainder;
    divmod(*this, other, quotient, remainder);
    return quotient;
  }
  constexpr Int128 operator%(const Int128& other) const {
    Int128 quotient;
    Int128 remainder;
    divmod(*this, other, quotient, remainder);
    return remainder;
  }

  constexpr Int128 operator<<(int shift) const { return Int128{rep << static_cast<unsigned>(shift)}; }

  constexpr Int128& operator+=(const Int128& other) { return *this = *this + other; }
  constexpr Int128& operator-=(const Int128& other) { return *this = *this - other; }
  constexpr Int128& operator*=(const Int128& other) { return *this = *this * other; }
  constexpr Int128& operator/=(const Int128& other) { return *this = *this / other; }
  constexpr Int128& operator%=(const Int128& other) { return *this = *this % other; }

  constexpr bool operator==(const Int128& other) const { return rep == other.rep; }
  constexpr bool operator!=(const Int128& other) const { return rep != other.rep; }
  constexpr bool operator<(const Int128& other) const {
    const bool a_negative = is_negative();
    const bool b_negative = other.is_negative();
    return a_negative != b_negative ? a_negative : rep < other.rep;
  }
  constexpr bool operator>(const Int128& other) const { return other < *this; }
  constexpr bool operator<=(const Int128& other) const { return !(other < *this); }
  constexpr bool operator>=(const Int128& other) const { return !(*this < other); }
};

}  // namespace int128_detail

#if MKPRO_HAS_BUILTIN_INT128
using Int128 = __int128;
using UInt128 = unsigned __int128;
#else
using Int128 = int128_detail::Int128;
using UInt128 = int128_detail::UInt128;
#endif

}  // namespace mkpro

#endif  // MKPRO_CORE_INT128_HPP
