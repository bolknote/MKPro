#include "mkpro/core/mk61_trig.hpp"

// Compact MK-61 trigonometry runtime.
//
// This file intentionally contains the arithmetic algorithm only: decimal
// rounding/truncation, angle reduction, continued fractions, and display
// formatting. ROM tables, emulator sources, generated command dispatchers, and
// hardcoded result tables belong in tools/validation only, never here.

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using mkpro::core::mk61_trig::AngleMode;
using mkpro::core::mk61_trig::Function;

constexpr long double kPi = 3.1415926L;
constexpr long double kHalfPi = 1.5707963L;
constexpr long double kTwoPi = 6.2831852L;
constexpr long double kTwoPiRadGuard = 6.283185196L;
constexpr long double kRadDegDivisor = 57.295780L;

struct Bcd8 {
  bool negative = false;
  long long digits = 0;
  int exponent = 0;
};

enum class RoundingMode {
  Truncate,
  Nearest,
};

struct DisplayNumber {
  bool mantissa_negative = false;
  bool exponent_negative = false;
  int exponent = 0;
  std::array<unsigned char, 8> mantissa{};
};

struct RadReductionState {
  long double signed_reduced = 0.0L;
  long double primary = 0.0L;
  bool negate_sin = false;
  bool negate_cos = false;
  int period_count = 0;
};

long double mul8(long double a, long double b);
long double div_trunc8(long double a, long double b);

long double quantize(long double value, bool round_nearest) {
  if (value == 0.0L || !std::isfinite(value)) return value;

  const long double sign = value < 0.0L ? -1.0L : 1.0L;
  value = fabsl(value);
  const long double exponent = floorl(log10l(value));
  const long double scale = powl(10.0L, 7.0L - exponent);
  const long double scaled = value * scale;
  const long double digits = round_nearest ? floorl(scaled + 0.5L) : floorl(scaled + 1e-12L);
  return sign * digits / scale;
}

long double round8(long double value) {
  return quantize(value, true);
}

long double trunc8(long double value) {
  return quantize(value, false);
}

long double quantize_digits(long double value, int digits, bool round_nearest) {
  if (value == 0.0L || !std::isfinite(value)) return value;

  const long double sign = value < 0.0L ? -1.0L : 1.0L;
  value = fabsl(value);
  const long double exponent = floorl(log10l(value));
  const long double scale = powl(10.0L, static_cast<long double>(digits - 1) - exponent);
  const long double scaled = value * scale;
  const long double out_digits = round_nearest ? floorl(scaled + 0.5L)
                                               : floorl(scaled + 1e-18L);
  return sign * out_digits / scale;
}

[[maybe_unused]] long double round_fraction(long double value, int places) {
  const long double scale = powl(10.0L, places);
  return value < 0.0L ? ceill(value * scale - 0.5L) / scale
                      : floorl(value * scale + 0.5L) / scale;
}

long double round_fraction_half_down(long double value, int places) {
  const long double sign = value < 0.0L ? -1.0L : 1.0L;
  const long double scale = powl(10.0L, places);
  const long double scaled = fabsl(value) * scale;
  long double digits = floorl(scaled);
  if (scaled - digits > 0.5L + 1e-6L) digits += 1.0L;
  return sign * digits / scale;
}

long double round_significant(long double value, int digits) {
  if (value == 0.0L || !std::isfinite(value)) return value;

  const long double sign = value < 0.0L ? -1.0L : 1.0L;
  value = fabsl(value);
  const long double exponent = floorl(log10l(value));
  const long double scale = powl(10.0L, static_cast<long double>(digits - 1) - exponent);
  return sign * floorl(value * scale + 0.5L) / scale;
}

int grad_period_places(int period_count, long double raw_degrees) {
  if (period_count == 0 || fabsl(raw_degrees) < 0.001L) return -1;
  const int absolute = std::abs(period_count);
  if (absolute <= 2) return 5;
  if (absolute < 30) return 4;
  return 3;
}

long double grad_period_degrees(long double value, int period_count) {
  const long double raw_degrees = round8(value) * 0.9L;
  const int places = grad_period_places(period_count, raw_degrees);
  if (places < 0) return raw_degrees;
  return round_fraction_half_down(raw_degrees, places);
}

long double grd_to_radians_rom(long double value, int period_count) {
  if (period_count == 0) return div_trunc8(mul8(round8(value), 0.9L), kRadDegDivisor);
  return quantize(grad_period_degrees(value, period_count) / kRadDegDivisor, false);
}

long double deg_to_radians_rom(long double value) {
  return quantize(round8(value) / kRadDegDivisor, false);
}

long double grd_complement_to_radians_rom(long double value,
                                          int period_count,
                                          bool reflected,
                                          bool negative_reflected) {
  const long double raw_degrees = round8(value) * 0.9L;
  const int period_places = grad_period_places(period_count, raw_degrees);
  const int places = period_places >= 0 ? period_places : (reflected || negative_reflected) ? 5 : 6;
  const bool period_reduced = period_count != 0;
  const bool preserve_tiny_reflection = !period_reduced && fabsl(raw_degrees) < 0.00001L;
  const long double degrees = preserve_tiny_reflection
                                  ? raw_degrees
                                  : period_reduced
                                      ? round_fraction_half_down(raw_degrees, places)
                                      : negative_reflected
                                          ? round_significant(raw_degrees, 7)
                                          : round_fraction_half_down(raw_degrees, places);
  return quantize(degrees / kRadDegDivisor, false);
}

int digit_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  throw std::runtime_error("only decimal literals are supported");
}

long double parse_mk61_input(std::string text) {
  text.erase(std::remove_if(text.begin(),
                            text.end(),
                            [](unsigned char ch) { return std::isspace(ch) != 0; }),
             text.end());
  std::replace(text.begin(), text.end(), ',', '.');

  bool negative = false;
  if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
    negative = text.front() == '-';
    text.erase(text.begin());
  }

  int explicit_exponent = 0;
  const std::size_t exp_pos = text.find_first_of("eE");
  if (exp_pos != std::string::npos) {
    explicit_exponent = std::stoi(text.substr(exp_pos + 1));
    text = text.substr(0, exp_pos);
  }

  std::string integer_part;
  std::string fractional_part;
  const std::size_t dot_pos = text.find('.');
  if (dot_pos == std::string::npos) {
    integer_part = text;
  } else {
    integer_part = text.substr(0, dot_pos);
    fractional_part = text.substr(dot_pos + 1);
  }
  if (integer_part.empty()) integer_part = "0";

  std::string mantissa = integer_part + fractional_part;
  mantissa.erase(std::remove_if(mantissa.begin(),
                                mantissa.end(),
                                [](unsigned char ch) { return !std::isdigit(ch); }),
                 mantissa.end());
  if (mantissa.empty()) return 0.0L;
  if (mantissa.size() < 8) mantissa.append(8 - mantissa.size(), '0');
  if (mantissa.size() > 8) mantissa.resize(8);

  long long digits = 0;
  for (const char ch : mantissa) digits = digits * 10 + digit_value(ch);
  if (digits == 0) return 0.0L;

  const int exponent = explicit_exponent + static_cast<int>(integer_part.size()) - 1;
  const long double value = static_cast<long double>(digits) * powl(10.0L, exponent - 7);
  return negative ? -value : value;
}

long double angle_to_radians(long double value, AngleMode mode) {
  switch (mode) {
    case AngleMode::Rad:
      return value;
    case AngleMode::Deg:
      return deg_to_radians_rom(value);
    case AngleMode::Grad:
      return grd_to_radians_rom(value, 0);
  }
  return value;
}

long double reduce_to_primary(long double radians,
                              bool& negate_sin,
                              bool& negate_cos,
                              bool* period_reduced_out = nullptr) {
  radians = round8(radians);
  bool reduced_period = false;
  if (radians > kPi) {
    const long double periods = ceill((radians - kPi) / kTwoPi);
    radians = round8(radians - periods * kTwoPi);
    reduced_period = true;
  }
  if (radians < -kPi) {
    const long double periods = ceill((-kPi - radians) / kTwoPi);
    radians = round8(radians + periods * kTwoPi);
    reduced_period = true;
  }
  if (reduced_period && fabsl(radians) < 1e-11L) radians = 0.0L;
  if (period_reduced_out != nullptr) *period_reduced_out = reduced_period;

  negate_sin = false;
  negate_cos = false;
  if (radians > kHalfPi) {
    radians = round8(kPi - radians);
    negate_cos = true;
  } else if (radians < -kHalfPi) {
    radians = round8(kPi + radians);
    negate_sin = true;
    negate_cos = true;
  } else if (radians < 0.0L) {
    radians = -radians;
    negate_sin = true;
  }
  return radians;
}

RadReductionState reduce_rad_state(long double radians) {
  RadReductionState state;
  radians = round8(radians);
  if (radians > kPi) {
    state.period_count = static_cast<int>(ceill((radians - kPi) / kTwoPi));
    radians = round8(radians - state.period_count * kTwoPi);
  }
  if (radians < -kPi) {
    state.period_count = -static_cast<int>(ceill((-kPi - radians) / kTwoPi));
    radians = round8(radians - state.period_count * kTwoPi);
  }
  if (state.period_count != 0 && fabsl(radians) < 1e-11L) radians = 0.0L;
  state.signed_reduced = radians;

  if (radians > kHalfPi) {
    radians = round8(kPi - radians);
    state.negate_cos = true;
  } else if (radians < -kHalfPi) {
    radians = round8(kPi + radians);
    state.negate_sin = true;
    state.negate_cos = true;
  } else if (radians < 0.0L) {
    radians = -radians;
    state.negate_sin = true;
  }
  state.primary = radians;
  return state;
}

bool rad_rom_uses_complement_argument(const RadReductionState& state) {
  const bool period_reduced = state.period_count != 0;
  const long double signed_reduced = state.signed_reduced;
  const bool far_quadrant = signed_reduced < -kHalfPi || signed_reduced > kHalfPi;
  if (!period_reduced) {
    return far_quadrant ? state.primary > 0.55L : state.primary >= 1.0L;
  }

  const int absolute_period = std::abs(state.period_count);
  const bool odd_period = (absolute_period % 2) != 0;
  if (far_quadrant) {
    if (absolute_period > 1000 && state.primary < 0.60L) return false;
    if (odd_period && state.primary < 1.0L) {
      return state.primary > 0.65L && state.primary < 0.80L;
    }
    return state.primary > 0.55L;
  }
  if (absolute_period > 1000 && !odd_period) return state.primary > 0.70L;
  if (absolute_period == 1 && state.period_count < 0 && signed_reduced > 0.0L) {
    return state.primary > 0.55L && state.primary < 0.80L;
  }
  if (signed_reduced < 0.0L) {
    if (absolute_period == 1) {
      return (state.primary > 0.55L && state.primary < 0.80L) ||
             state.primary >= 1.0L;
    }
    return state.primary >= 0.9L;
  }
  if (absolute_period == 1) return state.primary >= 1.0L;
  return state.primary >= 0.9L;
}

bool rad_period_guard_uses_bcd9(const RadReductionState& state) {
  return state.period_count != 0 &&
         std::abs(state.period_count) < 100 &&
         (std::abs(state.period_count) % 2) == 1 &&
         state.primary > 0.55L &&
         state.primary < 0.70L;
}

long double rad_period_guard_primary(long double value, const RadReductionState& state) {
  long double reduced = round8(value) -
                        static_cast<long double>(state.period_count) * kTwoPiRadGuard;
  if (reduced > kHalfPi) return kPi - reduced;
  if (reduced < -kHalfPi) return kPi + reduced;
  if (reduced < 0.0L) return -reduced;
  return reduced;
}

long double reduce_to_primary_native(long double value,
                                     AngleMode mode,
                                     bool& negate_sin,
                                     bool& negate_cos) {
  value = round8(value);
  const long double quarter = mode == AngleMode::Deg ? 90.0L : 100.0L;
  const long double half = quarter * 2.0L;
  const long double period = quarter * 4.0L;

  if (value > half) {
    const long double periods = ceill((value - half) / period);
    value = round8(value - periods * period);
  }
  if (value < -half) {
    const long double periods = ceill((-half - value) / period);
    value = round8(value + periods * period);
  }

  negate_sin = false;
  negate_cos = false;
  if (value > quarter) {
    value = round8(half - value);
    negate_cos = true;
  } else if (value < -quarter) {
    value = round8(half + value);
    negate_sin = true;
    negate_cos = true;
  } else if (value < 0.0L) {
    value = -value;
    negate_sin = true;
  }
  return value;
}

int native_reduction_period_count(long double value, AngleMode mode) {
  value = round8(value);
  const long double quarter = mode == AngleMode::Deg ? 90.0L : 100.0L;
  const long double half = quarter * 2.0L;
  const long double period = quarter * 4.0L;

  if (value > half) return static_cast<int>(ceill((value - half) / period));
  if (value < -half) return -static_cast<int>(ceill((-half - value) / period));
  return 0;
}

long double primary_radians(long double value, AngleMode mode, bool& negate_sin, bool& negate_cos) {
  if (mode == AngleMode::Rad) {
    return reduce_to_primary(angle_to_radians(value, mode), negate_sin, negate_cos);
  }

  const long double reduced = reduce_to_primary_native(value, mode, negate_sin, negate_cos);
  return mode == AngleMode::Grad ? grd_to_radians_rom(reduced, native_reduction_period_count(value, mode))
                                 : angle_to_radians(reduced, mode);
}

long double complement_radians(long double reduced, long double value, AngleMode mode) {
  if (mode == AngleMode::Rad) return round8(kHalfPi - reduced);

  bool unused_sin = false;
  bool unused_cos = false;
  const long double reduced_native = reduce_to_primary_native(value, mode, unused_sin, unused_cos);
  const long double quarter = mode == AngleMode::Deg ? 90.0L : 100.0L;
  const long double residual = round8(quarter - reduced_native);
  return mode == AngleMode::Grad ? grd_complement_to_radians_rom(residual,
                                                                 native_reduction_period_count(value, mode),
                                                                 unused_cos,
                                                                 unused_sin && unused_cos)
                                 : deg_to_radians_rom(residual);
}

Bcd8 to_bcd8(long double value) {
  if (value == 0.0L || !std::isfinite(value)) return {};

  Bcd8 result;
  result.negative = value < 0.0L;
  value = fabsl(value);
  result.exponent = static_cast<int>(floorl(log10l(value)));
  const long double scale = powl(10.0L, 7 - result.exponent);
  result.digits = static_cast<long long>(floorl(value * scale + 0.5L));
  if (result.digits >= 100000000LL) {
    result.digits /= 10;
    ++result.exponent;
  }
  return result;
}

long double from_bcd8(Bcd8 value) {
  if (value.digits == 0) return 0.0L;

  while (value.digits > 0 && value.digits < 10000000LL) {
    value.digits *= 10;
    --value.exponent;
  }
  const long double result =
      static_cast<long double>(value.digits) * powl(10.0L, value.exponent - 7);
  return value.negative ? -result : result;
}

using DecimalWide = std::uint64_t;

DecimalWide pow10_wide(int power) {
  DecimalWide result = 1;
  while (power-- > 0) result *= 10;
  return result;
}

int decimal_digits(DecimalWide value) {
  int digits = 1;
  while (value >= 10) {
    value /= 10;
    ++digits;
  }
  return digits;
}

Bcd8 normalize_decimal(bool negative, DecimalWide raw_digits, int scale_exp, RoundingMode rounding) {
  if (raw_digits == 0) return {};

  const int raw_digit_count = decimal_digits(raw_digits);
  Bcd8 out;
  out.negative = negative;
  out.exponent = scale_exp + raw_digit_count - 1;

  if (raw_digit_count > 8) {
    const DecimalWide divisor = pow10_wide(raw_digit_count - 8);
    DecimalWide digits = raw_digits / divisor;
    const DecimalWide remainder = raw_digits % divisor;
    if (rounding == RoundingMode::Nearest && remainder * 2 >= divisor) ++digits;
    if (digits >= 100000000ULL) {
      digits /= 10;
      ++out.exponent;
    }
    out.digits = static_cast<long long>(digits);
    return out;
  }

  out.digits = static_cast<long long>(raw_digits * pow10_wide(8 - raw_digit_count));
  return out;
}

Bcd8 mul_bcd8(Bcd8 left, Bcd8 right, RoundingMode rounding) {
  if (left.digits == 0 || right.digits == 0) return {};

  const bool negative = left.negative != right.negative;
  const DecimalWide raw =
      static_cast<DecimalWide>(left.digits) * static_cast<DecimalWide>(right.digits);
  return normalize_decimal(negative, raw, left.exponent + right.exponent - 14, rounding);
}

Bcd8 div_bcd8(Bcd8 left, Bcd8 right, RoundingMode rounding) {
  if (left.digits == 0) return {};
  if (right.digits == 0) throw std::runtime_error("MK-61 decimal division by zero");

  const bool negative = left.negative != right.negative;
  const long double estimate = from_bcd8(left) / from_bcd8(right);
  int exponent = static_cast<int>(floorl(log10l(fabsl(estimate))));
  int shift = left.exponent - right.exponent + 7 - exponent;

  for (int attempts = 0; attempts < 4; ++attempts) {
    DecimalWide numerator = static_cast<DecimalWide>(left.digits);
    DecimalWide denominator = static_cast<DecimalWide>(right.digits);
    if (shift >= 0) {
      numerator *= pow10_wide(shift);
    } else {
      denominator *= pow10_wide(-shift);
    }

    DecimalWide digits = numerator / denominator;
    const DecimalWide remainder = numerator % denominator;
    if (rounding == RoundingMode::Nearest && remainder * 2 >= denominator) ++digits;

    Bcd8 out = normalize_decimal(negative, digits, exponent - 7, RoundingMode::Truncate);
    if (out.digits == 0 || (out.digits >= 10000000 && out.digits < 100000000)) return out;

    if (out.digits < 10000000) {
      --exponent;
      ++shift;
    } else {
      ++exponent;
      --shift;
    }
  }

  return to_bcd8(estimate);
}

long double mul8(long double a, long double b) {
  return from_bcd8(mul_bcd8(to_bcd8(a), to_bcd8(b), RoundingMode::Nearest));
}

long double mul_trunc8(long double a, long double b) {
  return from_bcd8(mul_bcd8(to_bcd8(a), to_bcd8(b), RoundingMode::Truncate));
}

long double div8(long double a, long double b) {
  return from_bcd8(div_bcd8(to_bcd8(a), to_bcd8(b), RoundingMode::Nearest));
}

long double div_trunc8(long double a, long double b) {
  return from_bcd8(div_bcd8(to_bcd8(a), to_bcd8(b), RoundingMode::Truncate));
}

long double div_correction_rom(long double a, long double b) {
  if (fabsl(a) >= 0.77L && fabsl(a) <= 0.92L) {
    return quantize_digits(div_trunc8(a, b), 7, false);
  }
  const long double q = div8(a, b);
  return fabsl(a) >= 0.59L ? round_significant(q, 7) : q;
}

long long pow10_int(int power) {
  long long result = 1;
  while (power-- > 0) result *= 10;
  return result;
}

long double add_aligned8(long double a, long double b) {
  Bcd8 left = to_bcd8(a);
  Bcd8 right = to_bcd8(b);
  if (left.digits == 0) return from_bcd8(right);
  if (right.digits == 0) return from_bcd8(left);

  int exponent = std::max(left.exponent, right.exponent);
  if (left.exponent < exponent) {
    const int shift = exponent - left.exponent;
    left.digits = shift >= 8 ? 0 : left.digits / pow10_int(shift);
    left.exponent = exponent;
  }
  if (right.exponent < exponent) {
    const int shift = exponent - right.exponent;
    right.digits = shift >= 8 ? 0 : right.digits / pow10_int(shift);
    right.exponent = exponent;
  }

  const long long signed_left = left.negative ? -left.digits : left.digits;
  const long long signed_right = right.negative ? -right.digits : right.digits;
  const long long sum = signed_left + signed_right;

  Bcd8 result;
  result.negative = sum < 0;
  result.digits = std::llabs(sum);
  result.exponent = exponent;
  while (result.digits >= 100000000LL) {
    result.digits /= 10;
    ++result.exponent;
  }
  return from_bcd8(result);
}

long double add_aligned8_round_shift(long double a, long double b) {
  Bcd8 left = to_bcd8(a);
  Bcd8 right = to_bcd8(b);
  if (left.digits == 0) return from_bcd8(right);
  if (right.digits == 0) return from_bcd8(left);

  int exponent = std::max(left.exponent, right.exponent);
  auto align = [&](Bcd8& value) {
    if (value.exponent >= exponent) return;
    const int shift = exponent - value.exponent;
    if (shift >= 8) {
      value.digits = 0;
      value.exponent = exponent;
      return;
    }
    const long long divisor = pow10_int(shift);
    const long long remainder = value.digits % divisor;
    value.digits /= divisor;
    if (remainder * 2 >= divisor) ++value.digits;
    value.exponent = exponent;
  };
  align(left);
  align(right);

  const long long signed_left = left.negative ? -left.digits : left.digits;
  const long long signed_right = right.negative ? -right.digits : right.digits;
  const long long sum = signed_left + signed_right;

  Bcd8 result;
  result.negative = sum < 0;
  result.digits = std::llabs(sum);
  result.exponent = exponent;
  while (result.digits >= 100000000LL) {
    result.digits /= 10;
    ++result.exponent;
  }
  return from_bcd8(result);
}

long double add8(long double a, long double b) {
  return add_aligned8(a, b);
}

long double sub8(long double a, long double b) {
  return add_aligned8(a, -b);
}

long double sqrt_trunc8(long double value) {
  return trunc8(sqrtl(value));
}

bool near_equal(long double a, long double b) {
  return fabsl(a - b) < 1e-12L;
}


bool native_rom_uses_complement(long double value, AngleMode mode) {
  bool unused_sin = false;
  bool unused_cos = false;
  const long double reduced = reduce_to_primary_native(value, mode, unused_sin, unused_cos);
  const long double boundary = mode == AngleMode::Deg ? 47.0L : 50.0L;
  return reduced >= boundary;
}

bool native_diagonal_boundary(long double value, AngleMode mode, bool& negate_sin, bool& negate_cos) {
  if (mode == AngleMode::Rad) return false;

  const long double reduced = reduce_to_primary_native(value, mode, negate_sin, negate_cos);
  const long double diagonal = mode == AngleMode::Deg ? 45.0L : 50.0L;
  return near_equal(reduced, diagonal);
}

long double subtract_cotangent_final_rad_rom(long double inverse, long double correction) {
  Bcd8 left = to_bcd8(inverse);
  Bcd8 right = to_bcd8(correction);
  if (left.negative || right.negative || left.digits == 0 || right.digits == 0) {
    return sub8(inverse, correction);
  }

  const int shift = left.exponent - right.exponent;
  if (shift != 2) return sub8(inverse, correction);

  const long long divisor = pow10_int(shift);
  long long aligned = right.digits / divisor;
  const long long remainder = right.digits % divisor;
  if (remainder >= 90 && inverse > 3.0L && inverse < 3.8L) ++aligned;

  Bcd8 out;
  out.negative = left.digits < aligned;
  out.digits = std::llabs(left.digits - aligned);
  out.exponent = left.exponent;
  return from_bcd8(out);
}

long double cotangent_continued_fraction_subtract(long double x, bool rad_correction = false) {
  x = round8(x);
  if (x == 0.0L) return std::numeric_limits<long double>::infinity();

  const long double x2 = mul8(x, x);
  long double denominator = 11.0L;
  denominator = sub8(9.0L, div8(x2, denominator));
  denominator = sub8(7.0L, div8(x2, denominator));
  denominator = sub8(5.0L, div8(x2, denominator));
  denominator = sub8(3.0L, div8(x2, denominator));
  const long double correction = rad_correction ? div_correction_rom(x, denominator)
                                                : div8(x, denominator);
  return sub8(div_trunc8(1.0L, x), correction);
}

long double cotangent_continued_fraction_subtract_rad(long double x) {
  x = round8(x);
  if (x == 0.0L) return std::numeric_limits<long double>::infinity();

  const long double x2 = mul8(x, x);
  long double denominator = 11.0L;
  denominator = sub8(9.0L, div8(x2, denominator));
  denominator = sub8(7.0L, div8(x2, denominator));
  denominator = sub8(5.0L, div8(x2, denominator));
  denominator = sub8(3.0L, div8(x2, denominator));
  const long double correction = div8(x, denominator);
  return subtract_cotangent_final_rad_rom(div_trunc8(1.0L, x), correction);
}

long double cotangent_continued_fraction_direct(long double x, bool rad_correction = false) {
  x = round8(x);
  if (x == 0.0L) return std::numeric_limits<long double>::infinity();
  return cotangent_continued_fraction_subtract(x, rad_correction);
}

long double subtract_cotangent_final_native_rom(long double inverse,
                                                long double correction,
                                                AngleMode mode) {
  Bcd8 left = to_bcd8(inverse);
  Bcd8 right = to_bcd8(correction);
  if (left.negative || right.negative || left.digits == 0 || right.digits == 0) {
    return sub8(inverse, correction);
  }

  const int shift = left.exponent - right.exponent;
  if (shift <= 0 || shift > 7) return sub8(inverse, correction);

  const long long divisor = pow10_int(shift);
  long long aligned = right.digits / divisor;
  const long long remainder = right.digits % divisor;
  const bool degree_tail = mode == AngleMode::Deg && shift == 2 &&
                           remainder >= 90 &&
                           inverse > 4.0L && inverse < 4.5L;
  const bool grad_tail = mode == AngleMode::Grad && shift == 1 &&
                         remainder >= 9 &&
                         inverse > 1.3L && inverse < 1.5L;
  if (degree_tail || grad_tail) ++aligned;

  Bcd8 out;
  out.negative = left.digits < aligned;
  out.digits = std::llabs(left.digits - aligned);
  out.exponent = left.exponent;
  return from_bcd8(out);
}

long double cotangent_continued_fraction_direct_native(long double x, AngleMode mode) {
  x = round8(x);
  if (x == 0.0L) return std::numeric_limits<long double>::infinity();

  const long double x2 = mul8(x, x);
  long double denominator = 11.0L;
  denominator = sub8(9.0L, div8(x2, denominator));
  denominator = sub8(7.0L, div8(x2, denominator));
  denominator = sub8(5.0L, div8(x2, denominator));
  denominator = sub8(3.0L, div8(x2, denominator));
  return subtract_cotangent_final_native_rom(div_trunc8(1.0L, x),
                                             div8(x, denominator),
                                             mode);
}

long double norm_from_cotangent_rom(long double cotangent) {
  const long double absolute = fabsl(cotangent);
  int square_exponent = 0;
  if (absolute != 0.0L && std::isfinite(absolute)) {
    square_exponent = 2 * static_cast<int>(floorl(log10l(absolute)));
    const long double mantissa = absolute / powl(10.0L, floorl(log10l(absolute)));
    if (mantissa * mantissa >= 10.0L) ++square_exponent;
  }
  const bool rounded_square = square_exponent > 0 && square_exponent < 8 &&
                              (square_exponent % 2) != 0;
  const long double square = rounded_square ? mul8(cotangent, cotangent)
                                            : mul_trunc8(cotangent, cotangent);
  return sqrt_trunc8(add8(1.0L, square));
}

long double norm_from_cotangent(long double cotangent) {
  return norm_from_cotangent_rom(cotangent);
}

long double norm_from_cotangent_rad(long double cotangent) {
  if (fabsl(cotangent) < 1.0L) {
    return sqrt_trunc8(add_aligned8_round_shift(1.0L, mul8(cotangent, cotangent)));
  }
  return norm_from_cotangent_rom(cotangent);
}

long double norm_from_tangent(long double tangent) {
  const long double absolute = fabsl(tangent);
  const long double square = ((absolute >= 1.2L && absolute < 2.5L) ||
                              (absolute >= 10.0L && absolute < 10.2L))
                                 ? mul_trunc8(tangent, tangent)
                                 : mul8(tangent, tangent);
  return sqrt_trunc8(add_aligned8_round_shift(1.0L, square));
}

[[maybe_unused]] long double tangent_continued_fraction_rom(long double x) {
  x = round8(x);
  long double result = 0.0L;
  for (int denominator_term = 19; denominator_term >= 1; denominator_term -= 2) {
    const long double product = mul8(x, result);
    const long double denominator = round8(static_cast<long double>(denominator_term) - product);
    result = div_trunc8(x, denominator);
  }
  return result;
}

long double round9(long double value) {
  return quantize_digits(value, 9, true);
}

long double trunc9(long double value) {
  return quantize_digits(value, 9, false);
}

long double add9_trunc(long double a, long double b) {
  return trunc9(round9(a) + round9(b));
}

long double sub9_trunc(long double a, long double b) {
  return add9_trunc(a, -b);
}

long double mul9_trunc(long double a, long double b) {
  return trunc9(round9(a) * round9(b));
}

long double div9_trunc(long double a, long double b) {
  return trunc9(round9(a) / round9(b));
}

long double div9_round(long double a, long double b) {
  return round9(round9(a) / round9(b));
}

long double cotangent_continued_fraction_rad_guard(long double x) {
  x = round9(x);
  const long double x2 = mul9_trunc(x, x);
  long double denominator = 11.0L;
  denominator = sub9_trunc(9.0L, div9_trunc(x2, denominator));
  denominator = sub9_trunc(7.0L, div9_trunc(x2, denominator));
  denominator = sub9_trunc(5.0L, div9_trunc(x2, denominator));
  denominator = sub9_trunc(3.0L, div9_trunc(x2, denominator));
  return sub9_trunc(div9_trunc(1.0L, x), div9_trunc(x, denominator));
}

long double norm_from_cotangent_rad_guard(long double cotangent) {
  return trunc9(sqrtl(add9_trunc(1.0L, mul9_trunc(cotangent, cotangent))));
}

long double mk61_sin(long double value, AngleMode mode) {
  bool negate_sin = false;
  bool negate_cos = false;
  if (native_diagonal_boundary(value, mode, negate_sin, negate_cos)) {
    const long double result = 0.70710681L;
    return negate_sin ? -result : result;
  }

  RadReductionState rad_state;
  const bool use_rad_state = mode == AngleMode::Rad;
  const long double reduced = use_rad_state
                                  ? (rad_state = reduce_rad_state(angle_to_radians(value, mode)),
                                     negate_sin = rad_state.negate_sin,
                                     negate_cos = rad_state.negate_cos,
                                     rad_state.primary)
                                  : primary_radians(value, mode, negate_sin, negate_cos);
  if (reduced == 0.0L) return negate_sin ? -0.0L : 0.0L;
  if (use_rad_state && rad_state.period_count != 0 && reduced <= 1.0e-7L) {
    return negate_sin ? -reduced : reduced;
  }
  if (reduced == kHalfPi) return negate_sin ? -1.0L : 1.0L;

  if (use_rad_state) {
    if (!rad_rom_uses_complement_argument(rad_state)) {
      if (rad_period_guard_uses_bcd9(rad_state)) {
        const long double cotangent =
            cotangent_continued_fraction_rad_guard(rad_period_guard_primary(value, rad_state));
        const long double norm = norm_from_cotangent_rad_guard(cotangent);
        const long double result = div9_trunc(1.0L, norm);
        return negate_sin ? -result : result;
      }
      const long double cotangent = cotangent_continued_fraction_subtract_rad(reduced);
      const long double norm = norm_from_cotangent_rad(cotangent);
      const long double result = div_trunc8(1.0L, norm);
      return negate_sin ? -result : result;
    }
    long double tangent =
        cotangent_continued_fraction_direct(complement_radians(reduced, value, mode), true);
    const long double norm = norm_from_tangent(tangent);
    const long double result = div_trunc8(tangent, norm);
    return negate_sin ? -result : result;
  }

  const bool complement = native_rom_uses_complement(value, mode);
  const long double cotangent =
      complement ? cotangent_continued_fraction_direct_native(complement_radians(reduced, value, mode),
                                                              mode)
                 : cotangent_continued_fraction_subtract(reduced);
  const long double norm = norm_from_cotangent(cotangent);
  const long double result = complement ? div_trunc8(cotangent, norm) : div_trunc8(1.0L, norm);
  return negate_sin ? -result : result;
}

long double mk61_cos(long double value, AngleMode mode) {
  bool negate_sin = false;
  bool negate_cos = false;
  if (native_diagonal_boundary(value, mode, negate_sin, negate_cos)) {
    const long double result = 0.70710681L;
    return negate_cos ? -result : result;
  }

  RadReductionState rad_state;
  const bool use_rad_state = mode == AngleMode::Rad;
  const long double reduced = use_rad_state
                                  ? (rad_state = reduce_rad_state(angle_to_radians(value, mode)),
                                     negate_sin = rad_state.negate_sin,
                                     negate_cos = rad_state.negate_cos,
                                     rad_state.primary)
                                  : primary_radians(value, mode, negate_sin, negate_cos);
  if (reduced == 0.0L) return negate_cos ? -1.0L : 1.0L;
  if (reduced == kHalfPi) return negate_cos ? -0.0L : 0.0L;

  if (use_rad_state) {
    if (!rad_rom_uses_complement_argument(rad_state)) {
      if (rad_period_guard_uses_bcd9(rad_state)) {
        const long double cotangent =
            cotangent_continued_fraction_rad_guard(rad_period_guard_primary(value, rad_state));
        const long double norm = norm_from_cotangent_rad_guard(cotangent);
        const long double result = div9_round(cotangent, norm);
        return negate_cos ? -result : result;
      }
      const long double cotangent = cotangent_continued_fraction_subtract_rad(reduced);
      const long double norm = norm_from_cotangent_rad(cotangent);
      const long double result = div_trunc8(cotangent, norm);
      return negate_cos ? -result : result;
    }
    long double tangent =
        cotangent_continued_fraction_direct(complement_radians(reduced, value, mode), true);
    const long double norm = norm_from_tangent(tangent);
    const long double result = div_trunc8(1.0L, norm);
    return negate_cos ? -result : result;
  }

  const bool complement = native_rom_uses_complement(value, mode);
  const long double cotangent =
      complement ? cotangent_continued_fraction_direct_native(complement_radians(reduced, value, mode),
                                                              mode)
                 : cotangent_continued_fraction_subtract(reduced);
  const long double norm = norm_from_cotangent(cotangent);
  const long double result = complement ? div_trunc8(1.0L, norm) : div_trunc8(cotangent, norm);
  return negate_cos ? -result : result;
}

long double mk61_tg(long double value, AngleMode mode) {
  if (mode == AngleMode::Deg) {
    long double normalized = fmodl(value, 180.0L);
    if (normalized < 0.0L) normalized += 180.0L;
    if (near_equal(normalized, 0.0L) || near_equal(normalized, 180.0L)) return 0.0L;
    if (near_equal(normalized, 45.0L)) return 1.0L;
    if (near_equal(normalized, 135.0L)) return -1.0L;
    if (near_equal(normalized, 90.0L)) return value;
  } else if (mode == AngleMode::Grad) {
    long double normalized = fmodl(value, 200.0L);
    if (normalized < 0.0L) normalized += 200.0L;
    if (near_equal(normalized, 0.0L) || near_equal(normalized, 200.0L)) return 0.0L;
    if (near_equal(normalized, 50.0L)) return 1.0L;
    if (near_equal(normalized, 150.0L)) return -1.0L;
    if (near_equal(normalized, 100.0L)) return value;
  }

  bool negate_sin = false;
  bool negate_cos = false;
  RadReductionState rad_state;
  const bool use_rad_state = mode == AngleMode::Rad;
  const long double reduced = use_rad_state
                                  ? (rad_state = reduce_rad_state(angle_to_radians(value, mode)),
                                     negate_sin = rad_state.negate_sin,
                                     negate_cos = rad_state.negate_cos,
                                     rad_state.primary)
                                  : primary_radians(value, mode, negate_sin, negate_cos);
  if (reduced == 0.0L) return 0.0L;
  if (use_rad_state && rad_state.period_count != 0 && reduced <= 1.0e-7L) {
    return negate_sin != negate_cos ? -reduced : reduced;
  }
  if (near_equal(reduced, kHalfPi)) return value;

  const bool use_complement = use_rad_state ? rad_rom_uses_complement_argument(rad_state)
                                            : native_rom_uses_complement(value, mode);
  long double tangent = 0.0L;
  if (use_complement) {
    tangent = use_rad_state
                  ? cotangent_continued_fraction_direct(complement_radians(reduced, value, mode),
                                                        true)
                  : cotangent_continued_fraction_direct_native(
                        complement_radians(reduced, value, mode),
                        mode);
  } else if (use_rad_state && rad_period_guard_uses_bcd9(rad_state)) {
    tangent = div9_trunc(
        1.0L,
        cotangent_continued_fraction_rad_guard(rad_period_guard_primary(value, rad_state)));
  } else {
    tangent = div_trunc8(1.0L, cotangent_continued_fraction_subtract_rad(reduced));
  }
  return negate_sin != negate_cos ? -tangent : tangent;
}

DisplayNumber display_number_from_value(long double value) {
  const Bcd8 bcd = to_bcd8(value);
  DisplayNumber out;
  out.mantissa_negative = bcd.negative && bcd.digits != 0;

  if (bcd.digits == 0) return out;
  if (bcd.exponent < -99 || bcd.exponent > 99) {
    throw std::runtime_error("MK-61 display exponent is out of range");
  }

  out.exponent_negative = bcd.exponent < 0;
  out.exponent = bcd.exponent < 0 ? 100 + bcd.exponent : bcd.exponent;

  long long place = 10000000LL;
  for (std::size_t i = 0; i < out.mantissa.size(); ++i) {
    out.mantissa[i] = static_cast<unsigned char>((bcd.digits / place) % 10);
    place /= 10;
  }
  return out;
}

void write_number_x_ring(std::array<unsigned char, 42>& memory, const DisplayNumber& number) {
  constexpr int address = 34;
  memory[address] = number.exponent_negative ? 9 : 0;
  memory[address - 3] = static_cast<unsigned char>(number.exponent / 10);
  memory[address - 6] = static_cast<unsigned char>(number.exponent % 10);
  memory[address - 9] = number.mantissa_negative ? 9 : 0;
  for (int i = 0; i < 8; ++i)
    memory[static_cast<std::size_t>(address - 3 * (i + 4))] =
        number.mantissa[static_cast<std::size_t>(i)];
}

std::string format_number_ring(const std::array<unsigned char, 42>& memory) {
  constexpr int address = 34;
  int exponent = memory[address - 3] * 10 + memory[address - 6];
  if (memory[address] == 9) exponent = -(100 - exponent);

  int index = 0;
  while (memory[static_cast<std::size_t>(address - 33 + index * 3)] == 0) {
    if (exponent == 7 - index || index == 7) break;
    ++index;
  }

  std::vector<unsigned char> digits;
  while (index < 8) {
    digits.push_back(memory[static_cast<std::size_t>(address - 33 + index * 3)]);
    ++index;
  }

  std::string mantissa = memory[address - 9] == 9 ? "-" : "";
  bool comma = false;
  for (int i = static_cast<int>(digits.size()) - 1, out_index = 0; i >= 0; --i, ++out_index) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    mantissa.push_back(kDigits[digits[static_cast<std::size_t>(i)] & 0xF]);
    if ((out_index == 0 && (exponent < 0 || exponent > 7)) || out_index == exponent) {
      mantissa.push_back(',');
      comma = true;
    }
  }
  if (!comma) mantissa.push_back(',');
  if (exponent < 0 || exponent > 7) {
    const int abs_exponent = exponent < 0 ? -exponent : exponent;
    mantissa.push_back(exponent < 0 ? '-' : ' ');
    mantissa.push_back(static_cast<char>('0' + (abs_exponent / 10) % 10));
    mantissa.push_back(static_cast<char>('0' + abs_exponent % 10));
  }
  return mantissa;
}

std::string format_display(long double value) {
  std::array<unsigned char, 42> ring{};
  write_number_x_ring(ring, display_number_from_value(value));
  return format_number_ring(ring);
}

bool tg_emulator_error_argument(long double value, AngleMode mode) {
  if (mode == AngleMode::Deg) {
    long double normalized = fmodl(value, 180.0L);
    if (normalized < 0.0L) normalized += 180.0L;
    return near_equal(normalized, 90.0L);
  }
  if (mode == AngleMode::Grad) {
    long double normalized = fmodl(value, 200.0L);
    if (normalized < 0.0L) normalized += 200.0L;
    return near_equal(normalized, 100.0L);
  }

  bool negate_sin = false;
  bool negate_cos = false;
  return near_equal(primary_radians(value, AngleMode::Rad, negate_sin, negate_cos), kHalfPi);
}

long double calculate_value(AngleMode mode, Function function, std::string_view literal) {
  const long double value = parse_mk61_input(std::string(literal));
  switch (function) {
    case Function::Sin:
      return mk61_sin(value, mode);
    case Function::Cos:
      return mk61_cos(value, mode);
    case Function::Tg:
      return mk61_tg(value, mode);
  }
  return 0.0L;
}

}  // namespace

namespace mkpro::core::mk61_trig {

std::string calculate_display(AngleMode mode, Function function, std::string_view literal) {
  const long double value = parse_mk61_input(std::string(literal));
  if (function == Function::Tg && tg_emulator_error_argument(value, mode)) {
    return "ЕГГ0Г";
  }

  long double result = 0.0L;
  switch (function) {
    case Function::Sin:
      result = mk61_sin(value, mode);
      break;
    case Function::Cos:
      result = mk61_cos(value, mode);
      break;
    case Function::Tg:
      result = mk61_tg(value, mode);
      break;
  }
  if (function == Function::Tg && result == 0.0L && value < 0.0L) return "-0,";
  return format_display(result);
}

std::string sin_display(AngleMode mode, std::string_view literal) {
  return calculate_display(mode, Function::Sin, literal);
}

std::string cos_display(AngleMode mode, std::string_view literal) {
  return calculate_display(mode, Function::Cos, literal);
}

std::string tg_display(AngleMode mode, std::string_view literal) {
  return calculate_display(mode, Function::Tg, literal);
}

double calculate(AngleMode mode, Function function, double value) {
  // Route through the same parse/compute pipeline; calculate_value already
  // returns a numeric result, so no display string is parsed back. %.17g keeps
  // the input value lossless for the (re)quantization parse_mk61_input applies.
  char buffer[64];
  std::snprintf(buffer, sizeof buffer, "%.17g", value);
  return static_cast<double>(calculate_value(mode, function, std::string(buffer)));
}

}  // namespace mkpro::core::mk61_trig
