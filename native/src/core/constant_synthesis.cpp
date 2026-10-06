#include "mkpro/core/constant_synthesis.hpp"

#include "mkpro/core/indirect_addressing.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <numeric>
#include <set>
#include <string>
#include <utility>

namespace mkpro::core {
namespace {

using Integer = std::int64_t;
constexpr Integer kMaximumDigits = 99999999;
constexpr std::array<Integer, 8> kScales{
    1, 10, 100, 1000, 10000, 100000, 1000000, 10000000};

struct Decimal {
  Integer numerator = 0;
  Integer scale = 1;
};

std::optional<Decimal> parse(const std::string& text) {
  if (text.empty())
    return std::nullopt;
  Decimal result;
  std::size_t i = text.front() == '-' ? 1U : 0U;
  const bool negative = i != 0;
  bool point = false;
  std::size_t digits = 0;
  for (; i < text.size(); ++i) {
    const char ch = text[i];
    if (ch == '.' && !point && digits != 0 && i + 1U < text.size()) {
      point = true;
    } else if (ch >= '0' && ch <= '9') {
      if (++digits > 8U)
        return std::nullopt;
      result.numerator = result.numerator * 10 + ch - '0';
      if (point)
        result.scale *= 10;
    } else {
      return std::nullopt;
    }
  }
  if (digits == 0 || (negative && result.numerator == 0))
    return std::nullopt;
  if (negative)
    result.numerator = -result.numerator;
  return result;
}

std::string spelling(Integer numerator, Integer scale) {
  const bool negative = numerator < 0;
  if (negative)
    numerator = -numerator;
  std::string result = (negative ? "-" : "") + std::to_string(numerator / scale);
  Integer remainder = numerator % scale;
  if (remainder != 0) {
    result += '.';
    while (remainder != 0) {
      remainder *= 10;
      result += static_cast<char>('0' + remainder / scale);
      remainder %= scale;
    }
  }
  return result;
}

bool equal(const Decimal& left, const Decimal& right) {
  return left.numerator * right.scale == right.numerator * left.scale;
}

Decimal observe(Decimal value, ConstantObservationKind kind) {
  switch (kind) {
  case ConstantObservationKind::Identity:
    return value;
  case ConstantObservationKind::Negate:
    value.numerator = -value.numerator;
    return value;
  case ConstantObservationKind::Sign:
    return {(value.numerator > 0) - (value.numerator < 0), 1};
  case ConstantObservationKind::Integer:
    return {value.numerator / value.scale, 1};
  case ConstantObservationKind::Fraction:
    return {value.numerator % value.scale, value.scale};
  }
  return value;
}

bool supported_kind(ConstantObservationKind kind) {
  switch (kind) {
  case ConstantObservationKind::Identity:
  case ConstantObservationKind::Negate:
  case ConstantObservationKind::Sign:
  case ConstantObservationKind::Integer:
  case ConstantObservationKind::Fraction:
    return true;
  }
  return false;
}

bool supported_address(const ConstantAddressConstraint& address) {
  // This domain has ordinary integer selectors only. In particular Rf must
  // not leak from the mk61s profile into the stock machine's register bank.
  return address.selector.size() == 1U &&
         std::string("789abcde").find(address.selector.front()) != std::string::npos &&
         address.target >= 0 && address.target <= 99;
}

bool address_matches(const std::string& value, const ConstantAddressConstraint& address) {
  if (!supported_address(address))
    return false;
  const auto result = evaluate_indirect_address(
      address.selector, value, IndirectOperationKind::Flow, address.model);
  return result && result->flow_target == address.target &&
         result->actual_flow_target == address.target &&
         indirect_writeback_preserves_literal_value(*result, value);
}

Integer positive_mod(Integer value, Integer modulus) {
  value %= modulus;
  return value < 0 ? value + modulus : value;
}

struct Domain {
  Integer low = -kMaximumDigits;
  Integer high = kMaximumDigits;
  Integer modulus = 1;
  Integer residue = 0;

  bool restrict_residue(Integer next_modulus, Integer next_residue) {
    next_residue = positive_mod(next_residue, next_modulus);
    // All supported moduli are powers of ten, so one divides the other.
    const Integer common = std::min(modulus, next_modulus);
    if (positive_mod(residue - next_residue, common) != 0)
      return false;
    if (next_modulus > modulus) {
      modulus = next_modulus;
      residue = next_residue;
    }
    return true;
  }
};

bool constrain(Domain& domain, Integer scale, ConstantObservationKind kind,
               Decimal wanted) {
  if (kind == ConstantObservationKind::Negate) {
    wanted.numerator = -wanted.numerator;
    kind = ConstantObservationKind::Identity;
  }
  if (kind == ConstantObservationKind::Identity) {
    const Integer scaled = wanted.numerator * scale;
    if (scaled % wanted.scale != 0)
      return false;
    const Integer exact = scaled / wanted.scale;
    domain.low = std::max(domain.low, exact);
    domain.high = std::min(domain.high, exact);
  } else if (kind == ConstantObservationKind::Sign) {
    if (wanted.numerator % wanted.scale != 0)
      return false;
    const Integer sign = wanted.numerator / wanted.scale;
    if (sign == 0) {
      domain.low = std::max(domain.low, Integer{0});
      domain.high = std::min(domain.high, Integer{0});
    } else if (sign == 1) {
      domain.low = std::max(domain.low, Integer{1});
    } else if (sign == -1) {
      domain.high = std::min(domain.high, Integer{-1});
    } else {
      return false;
    }
  } else if (kind == ConstantObservationKind::Integer) {
    if (wanted.numerator % wanted.scale != 0)
      return false;
    const Integer integer = wanted.numerator / wanted.scale;
    const Integer low = integer > 0 ? integer * scale : (integer - 1) * scale + 1;
    const Integer high = integer < 0 ? integer * scale : (integer + 1) * scale - 1;
    domain.low = std::max(domain.low, low);
    domain.high = std::min(domain.high, high);
  } else if (kind == ConstantObservationKind::Fraction) {
    if (wanted.numerator <= -wanted.scale || wanted.numerator >= wanted.scale)
      return false;
    const Integer scaled = wanted.numerator * scale;
    if (scaled % wanted.scale != 0 ||
        !domain.restrict_residue(scale, scaled / wanted.scale))
      return false;
    if (wanted.numerator > 0)
      domain.low = std::max(domain.low, Integer{1});
    if (wanted.numerator < 0)
      domain.high = std::min(domain.high, Integer{-1});
  }
  return domain.low <= domain.high;
}

bool cheaper(const std::string& left, const std::string& right) {
  return left.size() != right.size() ? left.size() < right.size() : left < right;
}

} // namespace

bool constant_satisfies_observations(const std::string& value,
                                    const ConstantSynthesisRequest& request) {
  const auto word = parse(value);
  if (!word || (request.observations.empty() && !request.address))
    return false;
  for (const auto& constraint : request.observations) {
    const auto wanted = parse(constraint.value);
    if (!wanted || !supported_kind(constraint.kind) ||
        !equal(observe(*word, constraint.kind), *wanted))
      return false;
  }
  return !request.address || address_matches(value, *request.address);
}

ConstantSynthesisResult synthesize_constant(const ConstantSynthesisRequest& request) {
  ConstantSynthesisResult result;
  if ((request.observations.empty() && !request.address) ||
      request.observations.size() > 16U ||
      (request.address && !supported_address(*request.address)))
    return result;
  std::vector<Decimal> wanted;
  for (const auto& constraint : request.observations) {
    const auto value = parse(constraint.value);
    if (!value || !supported_kind(constraint.kind))
      return result;
    wanted.push_back(*value);
  }
  const auto charge = [&]() {
    if (result.work >= request.maximum_work) {
      result.status = ConstantSynthesisStatus::Exhausted;
      // A partial search must not claim the minimum-cost representative.
      result.value.reset();
      return false;
    }
    ++result.work;
    return true;
  };
  result.status = ConstantSynthesisStatus::Unsatisfiable;
  for (const Integer scale : kScales) {
    if (!charge())
      return result;
    Domain domain;
    bool feasible = true;
    for (std::size_t i = 0; i < wanted.size(); ++i) {
      if (!charge())
        return result;
      if (!constrain(domain, scale, request.observations[i].kind, wanted[i])) {
        feasible = false;
        break;
      }
    }
    if (!feasible)
      continue;
    if (request.address) {
      // Stable ordinary decimal write-back retains only a nonnegative integer.
      // Encode its last two decimal digits as a congruence, then independently
      // confirm the literal and its write-back with the authoritative model.
      domain.low = std::max(domain.low, Integer{0});
      if (!domain.restrict_residue(100 * scale, request.address->target * scale))
        continue;
    }
    const auto consider = [&](Integer low, Integer high, bool descending) {
      if (low > high)
        return true;
      const Integer candidate = descending
          ? high - positive_mod(high - domain.residue, domain.modulus)
          : low + positive_mod(domain.residue - low, domain.modulus);
      if (candidate < low || candidate > high)
        return true;
      if (!charge())
        return false;
      const std::string value = spelling(candidate, scale);
      if (constant_satisfies_observations(value, request) &&
          (!result.value || cheaper(value, *result.value)))
        result.value = value;
      return true;
    };
    if (!consider(std::max(domain.low, Integer{0}), domain.high, false) ||
        !consider(domain.low, std::min(domain.high, Integer{-1}), true))
      return result;
  }
  if (result.value)
    result.status = ConstantSynthesisStatus::Solved;
  return result;
}

FiniteConstantSynthesisResult synthesize_finite_constant_encodings(
    const std::vector<std::vector<std::string>>& consumer_values,
    const FiniteConstantSynthesisOptions& options) {
  FiniteConstantSynthesisResult result;
  if (consumer_values.empty() || consumer_values.size() > 8U ||
      consumer_values.front().empty() || consumer_values.front().size() > 8U)
    return result;
  const std::size_t rows = consumer_values.front().size();
  std::vector<std::vector<Decimal>> parsed;
  for (const auto& column : consumer_values) {
    if (column.size() != rows)
      return result;
    std::vector<Decimal> values;
    for (const auto& text : column) {
      const auto value = parse(text);
      if (!value)
        return result;
      values.push_back(*value);
    }
    parsed.push_back(std::move(values));
  }
  std::set<std::vector<std::string>> seen;
  const auto charge = [&]() {
    if (result.work >= options.maximum_work) {
      result.exhausted = true;
      return false;
    }
    ++result.work;
    return true;
  };
  const auto append = [&](const std::vector<std::string>& encoding) {
    if (encoding.size() != rows ||
        std::any_of(encoding.begin(), encoding.end(), [](const auto& word) {
          const auto value = parse(word);
          return !value || value->numerator == 0;
        }))
      return;
    if (seen.contains(encoding))
      return;
    if (result.encodings.size() >= options.maximum_candidates) {
      result.exhausted = true;
      return;
    }
    seen.insert(encoding);
    result.encodings.push_back(encoding);
  };

  // Divide each exact rational column by its lattice unit. This can produce
  // short constants not present in any observed column: e.g. 100,200,... and
  // 1000,2000,... admit the shared representation 1,2,... .
  for (const auto& column : parsed) {
    if (!charge())
      return result;
    Integer scale = 1;
    for (const auto& value : column)
      scale = std::max(scale, value.scale);
    std::vector<Integer> ticks;
    Integer unit = 0;
    for (const auto& value : column) {
      const Integer tick = value.numerator * (scale / value.scale);
      ticks.push_back(tick);
      unit = std::gcd(unit, tick);
    }
    if (unit != 0) {
      std::vector<std::string> encoding;
      for (const Integer tick : ticks)
        encoding.push_back(std::to_string(tick / unit));
      append(encoding);
    }
    const Integer first = *std::min_element(ticks.begin(), ticks.end());
    Integer stride = 0;
    for (const Integer tick : ticks)
      stride = std::gcd(stride, tick - first);
    if (stride != 0) {
      std::vector<std::string> encoding;
      for (const Integer tick : ticks)
        encoding.push_back(std::to_string((tick - first) / stride + 1));
      append(encoding);
    }
  }

  // Invert every admissible combination of cheap unary decoders. Each row
  // solves the same decoder tuple jointly; no row gets its own decoder.
  std::vector<std::vector<ConstantObservationKind>> choices;
  for (const auto& column : parsed) {
    std::vector<ConstantObservationKind> kinds{
        ConstantObservationKind::Identity, ConstantObservationKind::Negate};
    if (std::all_of(column.begin(), column.end(), [](const auto& value) {
          return value.numerator % value.scale == 0;
        }))
      kinds.push_back(ConstantObservationKind::Integer);
    if (std::all_of(column.begin(), column.end(), [](const auto& value) {
          return value.numerator > -value.scale && value.numerator < value.scale;
        }))
      kinds.push_back(ConstantObservationKind::Fraction);
    if (std::all_of(column.begin(), column.end(), [](const auto& value) {
          return value.numerator == value.scale || value.numerator == -value.scale;
        }))
      kinds.push_back(ConstantObservationKind::Sign);
    choices.push_back(std::move(kinds));
  }
  std::vector<ConstantObservationKind> selected(choices.size());
  std::function<void(std::size_t)> visit = [&](std::size_t column) {
    if (result.exhausted || !charge())
      return;
    if (column < choices.size()) {
      for (const auto kind : choices[column]) {
        selected[column] = kind;
        visit(column + 1U);
        if (result.exhausted)
          return;
      }
      return;
    }
    std::vector<std::string> encoding;
    for (std::size_t row = 0; row < rows; ++row) {
      ConstantSynthesisRequest request;
      request.maximum_work = options.maximum_work - result.work;
      for (std::size_t index = 0; index < selected.size(); ++index)
        request.observations.push_back({selected[index], consumer_values[index][row]});
      const auto solved = synthesize_constant(request);
      result.work += solved.work;
      if (solved.status == ConstantSynthesisStatus::Exhausted) {
        result.exhausted = true;
        return;
      }
      if (!solved.value)
        return;
      encoding.push_back(*solved.value);
    }
    append(encoding);
  };
  visit(0);
  return result;
}

} // namespace mkpro::core
