#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/core/post_layout_progress_bounds.hpp"

#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/stable_register_value_flow.hpp"
#include "mkpro/core/passes/liveness_analysis.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <utility>

namespace mkpro::core {
namespace {
using Range = std::optional<OrdinaryNumericRange>;
constexpr std::int64_t limit = 99999999;

bool transports_untouched_registers(int code) {
  if (code < 0 || code == 0x1f || code == 0x4f || code == 0x6f ||
      (code >= 0x70 && (code & 15) == 15))
    return false;
  const int family = code & 0xf0;
  return code <= 0x3b || (code >= 0x40 && code <= 0x4e) ||
      (code >= 0x51 && code <= 0x5e) || (code >= 0x60 && code <= 0x6e) ||
      (family >= 0x70 && family <= 0xe0);
}


Range bounded(std::int64_t lower, std::int64_t upper, bool integral) {
  if (lower < -limit || upper > limit || lower > upper)
    return std::nullopt;
  return OrdinaryNumericRange{lower, upper, integral};
}

Range join(const Range& left, const Range& right) {
  if (!left || !right)
    return std::nullopt;
  return bounded(std::min(left->lower, right->lower),
                 std::max(left->upper, right->upper),
                 left->integral && right->integral);
}

Range unary(int opcode, const Range& input) {
  if (!input)
    return std::nullopt;
  const auto [lo, hi, integral] = *input;
  if (opcode == 0x0b)
    return bounded(-hi, -lo, integral);
  if (opcode == 0x31) // abs
    return bounded(lo >= 0 ? lo : hi <= 0 ? -hi : 0,
                   std::max(-lo, hi), integral);
  if (opcode == 0x32)
    return bounded((lo > 0) - (lo < 0), (hi > 0) - (hi < 0), true);
  if (opcode == 0x34)
    return bounded(lo, hi, true);
  if (opcode == 0x35)
    return integral ? bounded(0, 0, true)
                    : bounded(lo >= 0 ? 0 : -1, hi <= 0 ? 0 : 1, false);
  if (opcode == 0x22) {
    const auto a = lo * lo, b = hi * hi;
    return bounded(lo <= 0 && hi >= 0 ? 0 : std::min(a, b), std::max(a, b), integral);
  }
  if (opcode == 0x15 && integral && lo >= 0 && hi <= 7) {
    std::int64_t a = 1, b = 1;
    for (std::int64_t i = 0; i < lo; ++i)
      a *= 10;
    for (std::int64_t i = 0; i < hi; ++i)
      b *= 10;
    return bounded(a, b, true);
  }
  return std::nullopt;
}

Range binary(int opcode, const Range& y, const Range& x) {
  if (!x || !y)
    return std::nullopt;
  if (opcode == 0x36) {
    // Kmax may prefer zero to a positive operand. It still returns one of
    // its inputs; do not import the host's max ordering into this analysis.
    return join(x, y);
  }
  const bool integral = x->integral && y->integral;
  if (opcode == 0x10)
    return bounded(y->lower + x->lower, y->upper + x->upper, integral);
  if (opcode == 0x11)
    return bounded(y->lower - x->upper, y->upper - x->lower, integral);
  if (opcode == 0x12) {
    const std::array<std::int64_t, 4> products{
        x->lower * y->lower, x->lower * y->upper,
        x->upper * y->lower, x->upper * y->upper};
    const auto [lo, hi] = std::minmax_element(products.begin(), products.end());
    return bounded(*lo, *hi, integral);
  }
  if (opcode == 0x13 && (x->lower > 0 || x->upper < 0)) {
    std::int64_t lo = limit, hi = -limit;
    for (const auto numerator : {y->lower, y->upper})
      for (const auto denominator : {x->lower, x->upper}) {
        const auto q = numerator / denominator;
        const auto r = numerator % denominator;
        const bool negative = (numerator < 0) != (denominator < 0);
        lo = std::min(lo, q - (r != 0 && negative ? 1 : 0));
        hi = std::max(hi, q + (r != 0 && !negative ? 1 : 0));
      }
    // Integer endpoints are exactly representable within the admitted
    // eight-digit domain. Outward floor/ceil encloses decimal rounding too;
    // a fractional interval never becomes integral from its endpoints alone.
    const bool exact_integer = integral && y->lower == y->upper &&
        x->lower == x->upper && y->lower % x->lower == 0;
    return bounded(lo, hi, exact_integer);
  }
  return std::nullopt;
}


// A must lattice of ordinary values n / denominator. Each integer n between
// the endpoints is admitted. Non-integral lattices survive only operations
// whose complete result set is exactly representable with eight digits.
struct QuantizedRange {
  std::int64_t lower, upper, denominator;
  bool operator==(const QuantizedRange&) const = default;
};
using Quantum = std::optional<QuantizedRange>;

Quantum quantized(std::int64_t lo, std::int64_t hi, std::int64_t denominator) {
  if (denominator <= 0 || lo > hi)
    return std::nullopt;
  if (lo == hi) {
    const auto common = std::gcd(lo < 0 ? -lo : lo, denominator);
    lo /= common;
    hi = lo;
    denominator /= common;
  }
  if (denominator > 100000000 || lo < -limit || hi > limit)
    return std::nullopt;
  auto rest = denominator;
  int twos = 0, fives = 0;
  while (rest % 2 == 0) { rest /= 2; ++twos; }
  while (rest % 5 == 0) { rest /= 5; ++fives; }
  if (rest != 1 || std::max(twos, fives) > 8)
    return std::nullopt;
  std::int64_t decimal = 1;
  for (int i = 0; i < std::max(twos, fives); ++i)
    decimal *= 10;
  const auto multiplier = decimal / denominator;
  if (std::max(-lo, hi) > limit / multiplier)
    return std::nullopt;
  return QuantizedRange{lo, hi, denominator};
}

Quantum quantized_range(const Range& range) {
  return range && range->integral ? quantized(range->lower, range->upper, 1)
                                 : std::nullopt;
}

Range quantized_enclosure(const QuantizedRange& q) {
  return bounded(q.lower / q.denominator - (q.lower % q.denominator < 0 ? 1 : 0),
                 q.upper / q.denominator + (q.upper % q.denominator > 0 ? 1 : 0),
                 q.denominator == 1);
}

Quantum join_quantized(const Quantum& a, const Quantum& b) {
  if (!a || !b)
    return std::nullopt;
  const auto common = std::gcd(a->denominator, b->denominator);
  const auto left_scale = b->denominator / common;
  const auto right_scale = a->denominator / common;
  return quantized(std::min(a->lower * left_scale, b->lower * right_scale),
                   std::max(a->upper * left_scale, b->upper * right_scale),
                   a->denominator * left_scale);
}

Quantum unary_quantized(int opcode, const Quantum& q) {
  if (!q)
    return std::nullopt;
  const auto [lo, hi, den] = *q;
  if (opcode == 0x0b)
    return quantized(-hi, -lo, den);
  if (opcode == 0x31)
    return quantized(lo >= 0 ? lo : hi <= 0 ? -hi : 0, std::max(-lo, hi), den);
  if (opcode == 0x34)
    return quantized(lo / den, hi / den, 1);
  if (opcode == 0x35) {
    if (lo / den == hi / den)
      return quantized(lo % den, hi % den, den);
    return quantized(lo < 0 ? -std::min(den - 1, -lo) : 0,
                     hi > 0 ? std::min(den - 1, hi) : 0, den);
  }
  if (opcode == 0x22)
    return quantized(lo <= 0 && hi >= 0 ? 0 : std::min(lo * lo, hi * hi),
                     std::max(lo * lo, hi * hi), den * den);
  return std::nullopt;
}

Quantum binary_quantized(int opcode, const Quantum& y, const Quantum& x) {
  if (!x || !y)
    return std::nullopt;
  if (opcode == 0x36)
    return join_quantized(x, y);
  if (opcode == 0x10 || opcode == 0x11) {
    const auto common = std::gcd(x->denominator, y->denominator);
    const auto xs = y->denominator / common, ys = x->denominator / common;
    return quantized(y->lower * ys + (opcode == 0x10 ? x->lower : -x->upper) * xs,
                     y->upper * ys + (opcode == 0x10 ? x->upper : -x->lower) * xs,
                     x->denominator * xs);
  }
  if (opcode == 0x12) {
    if (x->lower == x->upper || y->lower == y->upper) {
      const auto& scalar = x->lower == x->upper ? *x : *y;
      const auto& interval = x->lower == x->upper ? *y : *x;
      const auto denominator = scalar.denominator * interval.denominator;
      const auto common = std::gcd(scalar.lower < 0 ? -scalar.lower : scalar.lower,
                                   denominator);
      const auto factor = scalar.lower / common;
      return quantized(std::min(interval.lower * factor, interval.upper * factor),
                       std::max(interval.lower * factor, interval.upper * factor),
                       denominator / common);
    }
    const std::array<std::int64_t, 4> products{
        x->lower * y->lower, x->lower * y->upper,
        x->upper * y->lower, x->upper * y->upper};
    const auto [lo, hi] = std::minmax_element(products.begin(), products.end());
    return quantized(*lo, *hi, x->denominator * y->denominator);
  }
  if (opcode == 0x13 && x->lower == x->upper && x->lower != 0) {
    const auto denominator = y->denominator * (x->lower < 0 ? -x->lower : x->lower);
    const auto common = std::gcd(denominator, x->denominator);
    const auto factor = (x->lower < 0 ? -1 : 1) * (x->denominator / common);
    return quantized(std::min(y->lower * factor, y->upper * factor),
                     std::max(y->lower * factor, y->upper * factor),
                     denominator / common);
  }
  return std::nullopt;
}

Quantum quantized_literal(std::string_view text) {
  if (!bound_ordinary_decimal(text))
    return std::nullopt;
  std::size_t at = 0;
  const bool negative = text[0] == '-';
  if (negative || text[0] == '+') ++at;
  std::int64_t mantissa = 0;
  int scale = 0;
  bool point = false;
  for (; at < text.size() && text[at] != 'e' && text[at] != 'E'; ++at) {
    if (text[at] == '.') point = true;
    else {
      mantissa = mantissa * 10 + text[at] - '0';
      if (point) ++scale;
    }
  }
  if (at < text.size()) {
    ++at;
    const bool inverse = at < text.size() && text[at] == '-';
    if (at < text.size() && (text[at] == '-' || text[at] == '+')) ++at;
    int exponent = 0;
    for (; at < text.size(); ++at) exponent = exponent * 10 + text[at] - '0';
    scale += inverse ? exponent : -exponent;
  }
  if (mantissa == 0) return quantized(0, 0, 1);
  while (scale > 0 && mantissa % 10 == 0) { --scale; mantissa /= 10; }
  if (scale > 8) return std::nullopt;
  for (; scale < 0; ++scale) {
    if (mantissa > limit / 10) return std::nullopt;
    mantissa *= 10;
  }
  std::int64_t denominator = 1;
  for (int i = 0; i < scale; ++i) denominator *= 10;
  if (negative) mantissa = -mantissa;
  return quantized(mantissa, mantissa, denominator);
}


// A finite domain of possible digits in each payload tetrad. It is independent
// of numeric bounds: an unknown/raw operand may still be sanitized by K AND,
// and a bitwise result with hexadecimal digits is not an ordinary decimal.
// normalized_eight certifies the output format (positive 8.x, order zero),
// never inferred from a may-bit mask or an arbitrary value with the same bits.
struct MantissaShape {
  std::array<std::uint16_t, 7> digits{
      0xffffU, 0xffffU, 0xffffU, 0xffffU, 0xffffU, 0xffffU, 0xffffU};
  bool normalized_eight = false;
  bool operator==(const MantissaShape&) const = default;
};

MantissaShape ordinary_mantissa() {
  MantissaShape shape;
  shape.digits.fill(0x03ffU);
  return shape;
}

MantissaShape literal_mantissa(std::string_view text) {
  const auto range = bound_ordinary_decimal(text);
  if (!range)
    return {};
  std::string digits;
  for (const char c : text) {
    if (c == 'e' || c == 'E') break;
    if (c >= '0' && c <= '9') digits.push_back(c);
  }
  const auto first = digits.find_first_not_of('0');
  digits = first == std::string::npos ? "0" : digits.substr(first);
  digits.resize(8, '0');
  MantissaShape shape;
  for (std::size_t i = 0; i < shape.digits.size(); ++i)
    shape.digits[i] = static_cast<std::uint16_t>(1U << (digits[i + 1U] - '0'));
  shape.normalized_eight = range->lower == 8 && range->upper <= 9;
  return shape;
}

MantissaShape join_mantissa(const MantissaShape& a, const MantissaShape& b) {
  MantissaShape result;
  for (std::size_t i = 0; i < result.digits.size(); ++i)
    result.digits[i] = a.digits[i] | b.digits[i];
  result.normalized_eight = a.normalized_eight && b.normalized_eight;
  return result;
}

std::uint16_t bitwise_digit_set(int code, std::uint16_t y, std::uint16_t x) {
  std::uint16_t result = 0;
  for (unsigned a = 0; a < 16; ++a) {
    if ((y & (1U << a)) == 0) continue;
    for (unsigned b = 0; b < 16; ++b) {
      if ((x & (1U << b)) == 0) continue;
      const unsigned digit = code == 0x37 ? a & b : code == 0x38 ? a | b : a ^ b;
      result |= static_cast<std::uint16_t>(1U << digit);
    }
  }
  return result;
}

MantissaShape bitwise_mantissa(int code, const MantissaShape& y, const MantissaShape& x) {
  MantissaShape result;
  result.normalized_eight = true;
  for (std::size_t i = 0; i < result.digits.size(); ++i)
    result.digits[i] = bitwise_digit_set(code, y.digits[i], x.digits[i]);
  return result;
}

Quantum decimal_bitwise_enclosure(const MantissaShape& shape) {
  if (!shape.normalized_eight)
    return std::nullopt;
  std::int64_t lo = 8, hi = 8;
  for (const auto digits : shape.digits) {
    if (digits == 0 || (digits & 0xfc00U) != 0)
      return std::nullopt;
    int minimum = 0, maximum = 9;
    while ((digits & (1U << minimum)) == 0) ++minimum;
    while ((digits & (1U << maximum)) == 0) --maximum;
    lo = 10 * lo + minimum;
    hi = 10 * hi + maximum;
  }
  return quantized(lo, hi, 10000000);
}

struct Literal {
  std::size_t begin;
  OrdinaryNumericRange value;
  Quantum quantum;
  MantissaShape mantissa;
};

} // namespace

Range bound_ordinary_decimal(std::string_view text) {
  if (text.empty())
    return std::nullopt;
  std::size_t at = 0;
  const bool negative = text[at] == '-';
  if (negative || text[at] == '+')
    ++at;
  std::int64_t mantissa = 0;
  int significant = 0, fractional = 0;
  bool point = false, digit = false;
  for (; at < text.size() && text[at] != 'e' && text[at] != 'E'; ++at) {
    const char c = text[at];
    if (c == '.' && !point) {
      point = true;
    } else if (c >= '0' && c <= '9') {
      digit = true;
      if (mantissa != 0 || c != '0') {
        if (++significant > 8)
          return std::nullopt;
      }
      mantissa = mantissa * 10 + c - '0';
      if (point && ++fractional > 107)
        return std::nullopt;
    } else {
      return std::nullopt;
    }
  }
  if (!digit || (negative && mantissa == 0))
    return std::nullopt;
  int exponent = 0;
  if (at < text.size()) {
    ++at;
    const bool inverse = at < text.size() && text[at] == '-';
    if (at < text.size() && (text[at] == '+' || text[at] == '-'))
      ++at;
    const auto begin = at;
    for (; at < text.size(); ++at) {
      const char c = text[at];
      if (c < '0' || c > '9' || exponent > 99)
        return std::nullopt;
      exponent = exponent * 10 + c - '0';
    }
    if (at == begin || exponent > 99)
      return std::nullopt;
    if (inverse)
      exponent = -exponent;
  }
  const int shift = exponent - fractional;
  const int order = significant - 1 + shift;
  if (mantissa != 0 && (order < -99 || order > 99))
    return std::nullopt;
  std::int64_t divisor = 1;
  if (shift >= 0) {
    for (int i = 0; i < shift; ++i) {
      if (mantissa > limit / 10)
        return std::nullopt;
      mantissa *= 10;
    }
  } else if (shift < -8) {
    if (mantissa == 0)
      return bounded(0, 0, true);
    return negative ? bounded(-1, 0, false) : bounded(0, 1, false);
  } else {
    for (int i = 0; i < -shift; ++i)
      divisor *= 10;
  }
  if (negative)
    mantissa = -mantissa;
  const auto truncated = mantissa / divisor;
  return bounded(truncated - (mantissa % divisor < 0 ? 1 : 0),
                 truncated + (mantissa % divisor > 0 ? 1 : 0),
                 mantissa % divisor == 0);
}


namespace {
struct GuardedRecallRanges {
  // Exact execution-state definitions, not a union across unrelated callers.
  // An empty vector proves this read context has no feasible reaching value.
  std::map<std::size_t, std::vector<std::size_t>> definitions;
  std::vector<bool> reachable;
  std::size_t work = 0;
};

GuardedRecallRanges guarded_recall_ranges(
    const std::vector<MachineItem>& items, const AuthoritativePostLayoutControlFlow& flow,
    const std::set<std::size_t>& reads, const std::vector<PreloadReport>* preloads,
    std::size_t maximum_work) {
  GuardedRecallRanges result;
  if (reads.empty() || !flow.proved ||
      std::none_of(items.begin(), items.end(), [](const auto& item) {
        return item.kind == MachineItemKind::Op && item.opcode == 0x36 && !item.raw;
      }))
    return result;
  if (std::any_of(items.begin(), items.end(), [](const auto& item) {
        return item.raw || (item.kind == MachineItemKind::Op &&
            !transports_untouched_registers(item.opcode) &&
            item.opcode != 0x50 && item.opcode != 0x29);
      }))
    return result;
  const auto boundaries = prove_post_layout_input_boundaries(items, flow);
  if (!boundaries.proved)
    return result;
  const auto spend = [&] {
    if (result.work >= maximum_work)
      return false;
    ++result.work;
    return true;
  };
  const auto ops = raise_machine_to_ir(items);
  std::map<int, std::size_t> instruction_at;
  std::vector<passes::RegisterEffects> effects;
  int address = 0;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    if (ops[i].kind != IrKind::Label)
      instruction_at.emplace(address, i);
    address += passes::cells_per_op(ops[i]);
    effects.push_back(passes::register_effects(ops[i]));
  }
  const auto count = flow.execution_states.size();
  std::vector<std::size_t> sites;
  std::vector<std::vector<std::size_t>> predecessors(count);
  std::vector<bool> main(count, false);
  for (const auto state : boundaries.main_states)
    main.at(state) = true;
  for (std::size_t state = 0; state < count; ++state) {
    const auto at = instruction_at.find(flow.execution_states[state].address);
    if (at == instruction_at.end() || !spend())
      return result;
    sites.push_back(at->second);
    for (const auto next : flow.execution_successors[state]) {
      if (next >= count)
        return result;
      predecessors[next].push_back(state);
    }
  }
  std::map<std::size_t, std::vector<std::size_t>> candidates;
  std::set<passes::GuardedDefinitionUseQuery> queries;
  for (const auto use : reads) {
    const auto& op = ops.at(sites.at(use));
    if (op.kind != IrKind::Recall || op.meta.manual_interaction)
      continue;
    std::vector<std::size_t> pending{use};
    std::set<std::size_t> seen, definitions;
    bool complete = true;
    while (!pending.empty() && complete) {
      const auto state = pending.back();
      pending.pop_back();
      if (!seen.insert(state).second)
        continue;
      if (!spend())
        return result;
      // Entry values are not static stores. They must not disappear just
      // because the queried register also has compiler-owned definitions.
      if (main[state] || predecessors[state].empty()) {
        complete = false;
        break;
      }
      for (const auto from : predecessors[state]) {
        const auto& effect = effects[sites[from]];
        if (ops[sites[from]].meta.raw || effect.may_define_any_register ||
            effect.may_defs.contains(op.register_name)) {
          complete = false;
          break;
        }
        if (effect.must_defs.contains(op.register_name))
          definitions.insert(from);
        else
          pending.push_back(from);
      }
    }
    if (!complete || definitions.empty())
      continue;
    auto proposed = queries;
    for (const auto definition : definitions)
      proposed.insert({sites[definition], sites[use], op.register_name});
    if (proposed.size() > 64U)
      continue;
    queries = std::move(proposed);
    candidates.emplace(use, std::vector<std::size_t>(definitions.begin(), definitions.end()));
  }
  passes::LivenessOptions options;
  if (preloads)
    for (const auto& preload : *preloads)
      if (!preload.setup_expression && !preload.setup_target_name)
        options.equal_entry_value_classes.emplace(
            preload.register_name, "literal:" + preload.value);
  const auto proof = passes::analyze_unreachable_definition_uses(
      ops, flow, {queries.begin(), queries.end()}, options, maximum_work - result.work);
  result.work += proof.work;
  if (proof.exhausted || proof.excluded_uses.empty())
    return result;
  for (const auto& [use, definitions] : candidates) {
    std::vector<std::size_t> kept;
    bool removed = false, supported = true;
    for (const auto definition : definitions) {
      if (proof.excluded_uses.contains(
              {sites[definition], sites[use], ops[sites[use]].register_name})) {
        removed = true;
        continue;
      }
      // The numeric value of a direct store is its incoming X. Other strong
      // writes (FL, indirect mutation) retain their ordinary transfer rules.
      if (ops[sites[definition]].kind != IrKind::Store ||
          ops[sites[definition]].meta.manual_interaction) {
        supported = false;
        break;
      }
      kept.push_back(definition);
    }
    if (removed && supported)
      result.definitions.emplace(use, std::move(kept));
  }
  if (result.definitions.empty())
    return result;

  // Excluding one def/use edge does NOT remove a CFG alternative. A read
  // context is unreachable only if its COMPLETE reaching-definition set was
  // excluded, with no entry value or possible alias omitted above.
  std::vector<std::size_t> pending;
  result.reachable.assign(count, false);
  const auto blocked = [&](std::size_t state) {
    const auto found = result.definitions.find(state);
    return found != result.definitions.end() && found->second.empty();
  };
  for (std::size_t state = 0; state < count; ++state) {
    // Retain all contexts at an external command as an overapproximation.
    // No external root may be removed by a register-value proof.
    const bool external = std::any_of(flow.external_entries.begin(), flow.external_entries.end(),
        [&](const auto& entry) {
          return entry.entry.item_index == flow.execution_states[state].item_index;
        });
    if (!external)
      continue;
    if (blocked(state)) {
      result.definitions.clear();
      result.reachable.clear();
      return result;
    }
    result.reachable[state] = true;
    pending.push_back(state);
  }
  while (!pending.empty()) {
    const auto state = pending.back();
    pending.pop_back();
    if (!spend()) {
      result.definitions.clear();
      result.reachable.clear();
      return result;
    }
    for (const auto next : flow.execution_successors[state])
      if (!result.reachable[next] && !blocked(next)) {
        result.reachable[next] = true;
        pending.push_back(next);
      }
  }
  return result;
}
} // namespace

static PostLayoutNumericRangeProof prove_numeric_ranges_on_graph(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<bool>& automatic_entry_lift,
    const std::vector<std::size_t>& requested_items,
    const std::vector<PreloadReport>* preloads,
    std::size_t maximum_work,
    NumericRangeInputPolicy input_policy,
    const std::vector<std::size_t>* analysis_order = nullptr,
    std::set<std::size_t>* widened_queries = nullptr,
    const GuardedRecallRanges* guarded = nullptr,
    std::set<std::size_t>* recalled_states = nullptr) {
  PostLayoutNumericRangeProof result;
  const auto count = flow.execution_states.size();
  if (!flow.proved || automatic_entry_lift.size() != count ||
      flow.execution_successors.size() != count)
    return result;
  std::vector<std::vector<std::size_t>> predecessors(count);
  std::map<std::size_t, std::vector<std::size_t>> contexts;
  for (std::size_t i = 0; i < count; ++i) {
    if (guarded && !guarded->reachable.at(i))
      continue;
    contexts[flow.execution_states[i].item_index].push_back(i);
    for (const auto next : flow.execution_successors[i]) {
      if (next >= count)
        return result;
      if (!guarded || guarded->reachable.at(next))
        predecessors[next].push_back(i);
    }
  }
  std::map<std::size_t, bool> external;
  for (const auto& entry : flow.external_entries) {
    const bool main = entry.kind == ExternalEntryKind::Main;
    const auto [found, inserted] = external.emplace(entry.entry.item_index, main);
    if (!inserted)
      found->second = found->second && main;
  }
  const bool transport = input_policy == NumericRangeInputPolicy::ValidatedRegisterTransport;
  PostLayoutInputBoundaryProof boundaries;
  StableRegisterValueFlow stable;
  std::vector<bool> main_states(count, false);
  if (transport) {
    boundaries = prove_post_layout_input_boundaries(items, flow);
    if (!boundaries.proved)
      return result;
    for (const auto state : boundaries.main_states)
      main_states[state] = true;
    std::vector<PreloadReport> literals_only;
    if (preloads)
      for (const auto& preload : *preloads)
        if (!preload.setup_expression && !preload.setup_target_name)
          literals_only.push_back(preload);
    stable = analyze_stable_register_value_flow(items, literals_only, flow,
                                                flow.address_space_model);
  }
  const auto command_item = [&](std::size_t at) {
    return at < items.size() && items[at].kind == MachineItemKind::Op &&
        !items[at].raw && (transport || !items[at].manual_interaction);
  };
  const auto ordinary_item = [&](std::size_t at) {
    return command_item(at) && !items[at].manual_interaction;
  };
  std::map<std::size_t, Literal> literals;
  for (std::size_t i = 0; i < items.size();) {
    if (!ordinary_item(i) || items[i].opcode < 0 || items[i].opcode > 9) {
      ++i;
      continue;
    }
    const auto begin = i;
    std::string value;
    bool point = false;
    while (ordinary_item(i)) {
      const int code = items[i].opcode;
      if (code >= 0 && code <= 9)
        value += static_cast<char>('0' + code);
      else if (code == 0x0a && !point) {
        point = true;
        value += '.';
      } else {
        break;
      }
      ++i;
    }
    if (ordinary_item(i) && items[i].opcode == 0x0b) {
      value.insert(value.begin(), '-');
      ++i;
    }
    if (const auto range = bound_ordinary_decimal(value))
      literals.emplace(i - 1U, Literal{begin, *range, quantized_literal(value), literal_mantissa(value)});
  }
  using Key = std::pair<std::size_t, int>; // X,Y,Z,T, then R0..Re
  // Bottom (no grounded path yet) is distinct from unknown (a real path with
  // no ordinary-number certificate). Starting at bottom permits inductive
  // constants around loops without treating an ungrounded cycle as a fact.
  struct Fact {
    bool grounded = false;
    Range range;
    Quantum quantum;
    // Numeric equality alone does not certify indirect BCD write-back.
    bool integer_selector_word = false;
    MantissaShape mantissa{};
    bool operator==(const Fact&) const = default;
  };
  const Fact unknown{true, std::nullopt, std::nullopt};
  const auto constant = [](Range range, Quantum quantum = std::nullopt) {
    if (quantum && (!range || quantum->denominator == 1)) {
      const auto precise = quantized_enclosure(*quantum);
      if (range && precise)
        range = bounded(std::max(range->lower, precise->lower),
                        std::min(range->upper, precise->upper),
                        range->integral || precise->integral);
      else
        range = precise;
    }
    if (!range)
      quantum.reset();
    else if (!quantum)
      quantum = quantized_range(range);
    const bool integer_word = range && range->integral && range->lower >= 1;
    const auto mantissa = range ? ordinary_mantissa() : MantissaShape{};
    return Fact{true, std::move(range), std::move(quantum), integer_word, mantissa};
  };
  const auto decimal = [&](std::string_view text) {
    auto fact = constant(bound_ordinary_decimal(text), quantized_literal(text));
    fact.mantissa = literal_mantissa(text);
    if (fact.integer_selector_word) {
      const auto decoded = evaluate_indirect_address("0", text, IndirectOperationKind::Memory);
      const auto updated = decoded && decoded->result_value
          ? bound_ordinary_decimal(*decoded->result_value) : Range{};
      fact.integer_selector_word = updated && updated->integral &&
          updated->lower == fact.range->lower - 1 &&
          updated->upper == fact.range->upper - 1;
    }
    return fact;
  };
  const auto merge = [&](const Fact& a, const Fact& b) {
    if (!a.grounded) return b;
    if (!b.grounded) return a;
    auto joined = constant(join(a.range, b.range), join_quantized(a.quantum, b.quantum));
    joined.integer_selector_word = a.integer_selector_word && b.integer_selector_word;
    joined.mantissa = join_mantissa(a.mantissa, b.mantissa);
    return joined;
  };
  const auto is_top = [&](const Fact& fact) {
    return fact.grounded && !fact.range && fact.mantissa == unknown.mantissa;
  };
  const auto bitwise = [&](int code, const Fact& y, const Fact& x) {
    if (!y.grounded || !x.grounded) return Fact{};
    const auto shape = bitwise_mantissa(code, y.mantissa, x.mantissa);
    auto fact = constant(std::nullopt, decimal_bitwise_enclosure(shape));
    fact.mantissa = shape;
    return fact;
  };
  std::map<Key, Fact> cache;
  std::map<Key, std::set<Key>> dependents;
  std::map<Key, std::size_t> changes;
  std::set<Key> widened;
  std::set<Key> queued;
  std::deque<Key> work;
  std::set<std::pair<std::size_t, Key>> ordered_work;
  std::optional<Key> evaluating;
  const auto enqueue = [&](const Key& key) {
    if (queued.insert(key).second) {
      if (analysis_order) ordered_work.emplace(analysis_order->at(key.first), key);
      else work.push_back(key);
    }
  };
  const auto before = [&](std::size_t state, int slot) {
    const Key key{state, slot};
    const auto [found, inserted] = cache.try_emplace(key);
    if (inserted)
      enqueue(key);
    if (evaluating)
      dependents[key].insert(*evaluating);
    return found->second;
  };
  const auto after = [&](std::size_t state, int slot, std::size_t successor) -> Fact {
    const auto at = flow.execution_states[state].item_index;
    if (!command_item(at))
      return unknown;
    const int code = items[at].opcode;
    const int family = code & 0xf0, reg = code & 15;
    const auto input = [&](int index) { return before(state, index); };
    if (code == 0x50 || code == 0x29)
      return transport && slot >= 4 ? input(slot) : unknown;
    if (code == 0x4f || code == 0x6f ||
        (family >= 0x70 && family <= 0xe0 && reg == 15))
      return unknown;
    // Keep exact entry results on their incoming edge, before different
    // literal results meet at one successor and lose exact-word equality.
    if (stable.proved && slot == 0 && stable.x_after_state.at(state))
      return decimal(*stable.x_after_state[state]);
    if (slot >= 4) {
      const int wanted = slot - 4;
      if (family == 0x40 && reg == wanted)
        return input(0);
      // Ordinary positive integer counters have an exact selector write-back.
      // Fractional, negative, raw, boundary and conditional cases stay unknown.
      if (family >= 0x70 && family <= 0xe0 && reg == wanted) {
        if ((family != 0xb0 && family != 0xd0) || reg > 6)
          return unknown;
        const auto counter = input(slot);
        if (!counter.grounded)
          return {};
        if (!counter.integer_selector_word || !counter.range || !counter.range->integral ||
            counter.range->lower < 1 || counter.range->upper >= limit)
          return unknown;
        const auto mutation = indirect_selector_mutation(reg);
        const int delta = mutation == IndirectSelectorMutation::PreDecrement ? -1 : 1;
        const auto updated = constant(bounded(counter.range->lower + delta,
                                             counter.range->upper + delta, true));
        if (family == 0xb0) {
          const auto targets = flow.indirect_memory_targets.find(at);
          if (targets == flow.indirect_memory_targets.end() || targets->second.empty())
            return unknown;
          if (std::find(targets->second.begin(), targets->second.end(), reg) != targets->second.end())
            return targets->second.size() == 1U ? input(0) : merge(updated, input(0));
        }
        return updated;
      }
      constexpr std::array<int, 4> loops{0x5d, 0x5b, 0x58, 0x5a};
      if (wanted < 4 && code == loops[static_cast<std::size_t>(wanted)]) {
        const auto counter = input(slot);
        if (!counter.grounded)
          return {};
        if (!counter.range || !counter.range->integral || counter.range->lower < 1 ||
            flow.execution_edges.size() != count)
          return unknown;
        // ROM fact: positive integer 1 falls through unchanged; n > 1
        // decrements and branches. Zero, negatives and fractions have
        // different BCD behavior and are deliberately outside this rule.
        Fact outgoing;
        bool labelled = false;
        for (const auto& edge : flow.execution_edges[state]) {
          if (edge.target_state != successor)
            continue;
          labelled = true;
          if (edge.kind == PostLayoutExecutionEdgeKind::DirectTarget) {
            if (counter.range->upper >= 2)
              outgoing = merge(outgoing, constant(bounded(
                  std::max<std::int64_t>(2, counter.range->lower) - 1,
                  counter.range->upper - 1, true)));
          } else if (edge.kind == PostLayoutExecutionEdgeKind::Fallthrough) {
            if (counter.range->lower <= 1)
              outgoing = merge(outgoing, constant(bounded(1, 1, true)));
          } else {
            return unknown;
          }
        }
        return labelled ? outgoing : unknown;
      }
      if (family == 0xb0) {
        const auto targets = flow.indirect_memory_targets.find(at);
        if (targets == flow.indirect_memory_targets.end() || targets->second.empty())
          return unknown;
        if (std::find(targets->second.begin(), targets->second.end(), wanted) != targets->second.end())
          return targets->second.size() == 1U ? input(0) : merge(input(slot), input(0));
      }
      // Unknown/dark opcodes cannot carry register facts across themselves.
      if (transports_untouched_registers(code))
        return input(slot);
      return unknown;
    }
    if (const auto literal = literals.find(at); literal != literals.end()) {
      auto start = state;
      bool valid = true;
      for (std::size_t i = at; i > literal->second.begin; --i) {
        if (external.contains(i) || predecessors[start].size() != 1U) {
          valid = false;
          break;
        }
        start = predecessors[start].front();
        if (flow.execution_states[start].item_index != i - 1U) {
          valid = false;
          break;
        }
      }
      // Direct recall closes numeric entry and enables the next digit's
      // automatic lift independently of the previous entry mode. Require
      // that command on every immediate incoming context, never a raw/manual
      // command or an external numeric-entry boundary.
      const bool recalled = valid && !predecessors[start].empty() &&
          std::all_of(predecessors[start].begin(), predecessors[start].end(),
              [&](std::size_t previous) {
                const auto item = flow.execution_states[previous].item_index;
                return ordinary_item(item) &&
                    items[item].opcode >= 0x60 && items[item].opcode <= 0x6e;
              });
      if (valid && (automatic_entry_lift[start] || recalled) &&
          !external.contains(literal->second.begin)) {
        if (slot != 0) return before(start, slot - 1);
        auto fact = constant(Range{literal->second.value}, literal->second.quantum);
        fact.mantissa = literal->second.mantissa;
        return fact;
      }
    }
    if (family == 0x60) {
      if (slot == 0) {
        if (recalled_states)
          recalled_states->insert(state);
        if (guarded) {
          const auto found = guarded->definitions.find(state);
          if (found != guarded->definitions.end()) {
            Fact selected;
            for (const auto definition : found->second)
              selected = merge(selected, before(definition, 0));
            return selected;
          }
        }
      }
      return slot == 0 ? input(4 + reg) : input(slot - 1);
    }
    if (family == 0xd0) {
      if (slot != 0)
        return input(slot - 1);
      const auto targets = flow.indirect_memory_targets.find(at);
      if (targets == flow.indirect_memory_targets.end() || targets->second.empty())
        return unknown;
      Fact range;
      for (const int target : targets->second) {
        if (target < 0 || target >= 15 || (reg <= 6 && target == reg))
          return unknown;
        const auto value = input(4 + target);
        range = merge(range, value);
      }
      return range;
    }
    if (family == 0x40 || family == 0xb0 || code == 0x51 || code == 0x52 ||
        code == 0x53 || code == 0x54 || opcode_by_code(code).takes_address ||
        (family >= 0x70 && family <= 0xe0))
      return input(slot);
    if (code == 0x0d)
      return slot == 0 ? constant(bounded(0, 0, true)) : input(slot);
    if (code == 0x0e || code == 0x0f)
      return slot == 0 ? (code == 0x0e ? input(0) : unknown) : input(slot - 1);
    if (code == 0x0b) {
      // Entry-mode sensitivity concerns X and its hidden buffer, not Y/Z/T.
      // A barrier is not a proof that every independent stack slot is lost.
      if (slot > 0)
        return input(slot);
      // Fresh signed literals were handled above. For a separate sign edit,
      // admit numeric negation only immediately after a proved direct recall.
      // In particular VP/exponent entry must not masquerade as unary minus.
      const bool recalled = !external.contains(at) &&
          !predecessors[state].empty() &&
          std::all_of(predecessors[state].begin(), predecessors[state].end(),
              [&](std::size_t previous) {
                const auto item = flow.execution_states[previous].item_index;
                return ordinary_item(item) &&
                    items[item].opcode >= 0x60 && items[item].opcode <= 0x6e;
              });
      const auto x = input(0);
      return !x.grounded ? Fact{} : recalled
          ? constant(unary(code, x.range), unary_quantized(code, x.quantum)) : unknown;
    }
    if (code == 0x14)
      return input(slot < 2 ? 1 - slot : slot);
    if (code == 0x25)
      return input((slot + 1) % 4);
    // Decimal entry with no complete fresh-literal proof is unknown.
    if (code <= 0x0c && code != 0x0b)
      return unknown;
    const auto effect = opcode_by_code(code).stack_effect;
    if (slot == 0) {
      if (effect == StackEffect::ConsumeYDrop || effect == StackEffect::ConsumeYKeep) {
        const auto y = input(1);
        const auto x = input(0);
        if (code >= 0x37 && code <= 0x39) return bitwise(code, y, x);
        return !y.grounded || !x.grounded ? Fact{} : constant(binary(code, y.range, x.range), binary_quantized(code, y.quantum, x.quantum));
      }
      const auto x = input(0);
      if (x.grounded && code == 0x34 && x.mantissa.normalized_eight)
        return decimal("8");
      if (code == 0x3a) {
        auto ones = unknown;
        ones.mantissa.digits.fill(0x8000U);
        return bitwise(0x39, ones, x);
      }
      return x.grounded ? constant(unary(code, x.range), unary_quantized(code, x.quantum)) : Fact{};
    }
    if (effect == StackEffect::ConsumeYDrop || effect == StackEffect::Exposes)
      return input(std::min(slot + 1, 3));
    if (effect == StackEffect::ConsumeYKeep || effect == StackEffect::Preserves)
      return input(slot);
    if (effect == StackEffect::Shifts)
      return input(slot - 1);
    return unknown;
  };
  for (const auto item : requested_items)
    if (const auto found = contexts.find(item); found != contexts.end())
      for (const auto state : found->second)
        (void)before(state, 0);
  while (!work.empty() || !ordered_work.empty()) {
    if (result.work >= maximum_work) {
      result.exhausted = true;
      return result; // Nothing has been exported before convergence.
    }
    ++result.work;
    const auto key = analysis_order ? ordered_work.begin()->second : work.front();
    if (analysis_order) ordered_work.erase(ordered_work.begin());
    else work.pop_front();
    queued.erase(key);
    auto& previous = cache.at(key);
    if (is_top(previous))
      continue; // Only the entire reduced product at top is stable under joins.
    evaluating = key;
    const auto [state, slot] = key;
    const auto at = flow.execution_states[state].item_index;
    Fact value;
    const auto root = external.find(at);
    const bool is_main = transport ? main_states[state]
        : root != external.end() && root->second;
    const bool input_boundary = transport && boundaries.input_boundary_states[state];
    if (is_main || (!transport && root != external.end()) || (input_boundary && slot < 4)) {
      value = unknown;
      if (is_main && preloads && slot >= 4 &&
          flow.execution_states[state].return_stack.empty()) {
        constexpr std::string_view names = "0123456789abcde";
        for (const auto& preload : *preloads)
          if (preload.register_name == std::string(1, names[static_cast<std::size_t>(slot - 4)]) &&
              !preload.setup_expression && !preload.setup_target_name)
            value = decimal(preload.value);
      }
    }
    // Exact stable-register interpretation uses the machine's selector
    // write-back, not the apparent decimal text or setup-only data role.
    bool exact_stable = false;
    // Entry-buffer value and automatic stack lift are independent facts.
    // A fresh integer after clear/duplicate is known even with unproved lift.
    // Reuse the context-specific entry interpreter for X only, never Y/Z/T.
    if (stable.proved && slot == 0) {
      const auto& word = stable.x_before_state.at(state);
      if (word) {
        value = decimal(*word);
        exact_stable = true;
      }
    }
    if (stable.proved && slot >= 11) {
      const auto found = stable.before_item.find(at);
      if (found != stable.before_item.end()) {
        const auto& word = found->second[static_cast<std::size_t>(slot - 11)];
        if (word) {
          value = decimal(*word);
          exact_stable = true;
        }
      }
    }
    if (!exact_stable)
      for (const auto predecessor : predecessors[state]) {
        value = merge(value, after(predecessor, slot, state));
        if (is_top(value))
          break;
      }
    evaluating.reset();
    value = merge(previous, value);
    if (value == previous)
      continue;
    // A growing recurrence must not enumerate every integer. Widen to unknown,
    // never to an assumed machine limit or an invented iteration bound.
    // Losing only the word-representation certificate is not range growth.
    if (previous.grounded && previous.range && value.range &&
        (previous.range != value.range || previous.quantum != value.quantum) && ++changes[key] > 8U) {
      value.range.reset();
      value.quantum.reset();
      value.integer_selector_word = false;
      // The digit-set domain is finite. Keep its independently proved facts
      // when widening a growing numeric interval; it needs no loop bound.
      if (widened_queries) widened.insert(key);
    }
    previous = std::move(value);
    if (const auto users = dependents.find(key); users != dependents.end())
      for (const auto& user : users->second)
        enqueue(user);
  }
  if (widened_queries && !widened.empty()) {
    std::deque<Key> affected(widened.begin(), widened.end());
    while (!affected.empty()) {
      const auto key = affected.front(); affected.pop_front();
      if (const auto found = dependents.find(key); found != dependents.end())
        for (const auto& user : found->second)
          if (widened.insert(user).second) affected.push_back(user);
    }
    for (const auto item : requested_items)
      if (const auto found = contexts.find(item); found != contexts.end())
        for (const auto state : found->second)
          if (widened.contains(Key{state, 0})) widened_queries->insert(item);
  }
  for (const auto item : requested_items) {
    const auto found = contexts.find(item);
    if (found == contexts.end())
      continue;
    Range range;
    bool first = true;
    for (const auto state : found->second) {
      const auto& fact = cache.at(Key{state, 0});
      const auto value = fact.grounded ? fact.range : Range{};
      range = first ? value : join(range, value);
      first = false;
      if (!range)
        break;
    }
    if (range)
      result.x_before.emplace(item, *range);
    if (result.exhausted) {
      result.x_before.clear();
      break;
    }
  }
  return result;
}

PostLayoutNumericRangeProof prove_post_layout_numeric_ranges(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<bool>& automatic_entry_lift,
    const std::vector<std::size_t>& requested_items,
    const std::vector<PreloadReport>* preloads, std::size_t maximum_work,
    NumericRangeInputPolicy input_policy) {
  const bool can_refine = preloads &&
      input_policy == NumericRangeInputPolicy::ValidatedRegisterTransport &&
      std::any_of(items.begin(), items.end(), [](const auto& item) {
        return item.kind == MachineItemKind::Op && item.opcode == 0x38 && !item.raw;
      });
  std::set<std::size_t> widened_queries, recalled_states;
  auto result = prove_numeric_ranges_on_graph(items, flow, automatic_entry_lift,
      requested_items, preloads, maximum_work, input_policy, nullptr, can_refine ? &widened_queries : nullptr,
      nullptr, input_policy == NumericRangeInputPolicy::ValidatedRegisterTransport
          ? &recalled_states : nullptr);
  const bool needs_refinement = std::any_of(requested_items.begin(), requested_items.end(),
      [&](std::size_t item) {
        const auto found = result.x_before.find(item);
        return found == result.x_before.end() || !found->second.integral ||
            found->second.lower != found->second.upper;
      });
  if (!result.exhausted && needs_refinement && !recalled_states.empty() &&
      result.work < maximum_work) {
    const auto guarded = guarded_recall_ranges(
        items, flow, recalled_states, preloads, maximum_work - result.work);
    result.work += guarded.work;
    if (!guarded.definitions.empty() && result.work < maximum_work) {
      const auto refined = prove_numeric_ranges_on_graph(
          items, flow, automatic_entry_lift, requested_items, preloads,
          maximum_work - result.work, input_policy, nullptr, nullptr, &guarded);
      result.work += refined.work;
      // Optional refinement cannot erase already converged ordinary facts.
      // Both proofs are must bounds; their intersection is the stronger fact.
      if (!refined.exhausted)
        for (const auto& [item, value] : refined.x_before) {
          const auto old = result.x_before.find(item);
          if (old == result.x_before.end()) {
            result.x_before.emplace(item, value);
          } else if (const auto intersection = bounded(
                         std::max(value.lower, old->second.lower),
                         std::min(value.upper, old->second.upper),
                         value.integral || old->second.integral)) {
            old->second = *intersection;
          }
        }
    }
  }
  if (result.exhausted || !can_refine)
    return result;
  std::vector<std::size_t> missing;
  for (const auto item : requested_items)
    if (!result.x_before.contains(item) && widened_queries.contains(item)) missing.push_back(item);
  if (missing.empty() || result.work >= maximum_work) return result;
  const auto progress = prove_post_layout_monotone_progress(
      items, flow, automatic_entry_lift, *preloads, maximum_work - result.work);
  result.work += progress.work;
  if (!progress.witness || progress.exhausted || result.work >= maximum_work) return result;
  const auto expanded = expand_post_layout_progress(items, flow, automatic_entry_lift,
      *progress.witness, std::min<std::size_t>(20000, maximum_work - result.work));
  if (!expanded) return result;
  const auto bounded = prove_numeric_ranges_on_graph(
      items, expanded->control, expanded->automatic_entry_lift, missing,
      preloads, maximum_work - result.work, input_policy, &expanded->analysis_order);
  result.work += bounded.work;
  // The extension is optional. Exhaustion cannot invalidate already converged
  // ordinary facts, and no partial bounded-product fact is ever exported.
  if (!bounded.exhausted)
    result.x_before.insert(bounded.x_before.begin(), bounded.x_before.end());
  return result;
}

} // namespace mkpro::core
