#include "mkpro/core/post_layout_numeric_ranges.hpp"

#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace mkpro::core {
namespace {
using Range = std::optional<OrdinaryNumericRange>;
constexpr std::int64_t limit = 99999999;
constexpr std::size_t maximum_depth = 256;

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
    return integral ? bounded(0, 0, true) : bounded(-1, 1, false);
  if (opcode == 0x22 && integral) {
    const auto a = lo * lo, b = hi * hi;
    return bounded(lo <= 0 && hi >= 0 ? 0 : std::min(a, b), std::max(a, b), true);
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
  if (!x->integral || !y->integral)
    return std::nullopt;
  if (opcode == 0x10)
    return bounded(y->lower + x->lower, y->upper + x->upper, true);
  if (opcode == 0x11)
    return bounded(y->lower - x->upper, y->upper - x->lower, true);
  if (opcode == 0x12) {
    const std::array<std::int64_t, 4> products{
        x->lower * y->lower, x->lower * y->upper,
        x->upper * y->lower, x->upper * y->upper};
    const auto [lo, hi] = std::minmax_element(products.begin(), products.end());
    return bounded(*lo, *hi, true);
  }
  if (opcode == 0x13 && x->lower == x->upper && x->lower != 0 &&
      y->lower == y->upper && y->lower % x->lower == 0)
    return bounded(y->lower / x->lower, y->lower / x->lower, true);
  return std::nullopt;
}

struct Literal {
  std::size_t begin;
  OrdinaryNumericRange value;
};

} // namespace

Range bound_ordinary_decimal(std::string_view text) {
  if (text.empty())
    return std::nullopt;
  std::int64_t n = 0, d = 1;
  bool negative = false, point = false;
  int digits = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (i == 0 && c == '-') {
      negative = true;
    } else if (c == '.' && !point) {
      point = true;
    } else if (c >= '0' && c <= '9') {
      if (++digits > 8)
        return std::nullopt;
      n = n * 10 + c - '0';
      if (point)
        d *= 10;
    } else {
      return std::nullopt;
    }
  }
  if (digits == 0 || (negative && n == 0))
    return std::nullopt;
  if (negative)
    n = -n;
  const auto truncated = n / d;
  return bounded(truncated - (n % d < 0 ? 1 : 0),
                 truncated + (n % d > 0 ? 1 : 0), n % d == 0);
}

PostLayoutNumericRangeProof prove_post_layout_numeric_ranges(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<bool>& automatic_entry_lift,
    const std::vector<std::size_t>& requested_items,
    const std::vector<PreloadReport>* preloads,
    std::size_t maximum_work) {
  PostLayoutNumericRangeProof result;
  const auto count = flow.execution_states.size();
  if (!flow.proved || automatic_entry_lift.size() != count ||
      flow.execution_successors.size() != count)
    return result;
  std::vector<std::vector<std::size_t>> predecessors(count);
  std::map<std::size_t, std::vector<std::size_t>> contexts;
  for (std::size_t i = 0; i < count; ++i) {
    contexts[flow.execution_states[i].item_index].push_back(i);
    for (const auto next : flow.execution_successors[i])
      predecessors[next].push_back(i);
  }
  std::map<std::size_t, bool> external;
  for (const auto& entry : flow.external_entries) {
    const bool main = entry.kind == ExternalEntryKind::Main;
    const auto [found, inserted] = external.emplace(entry.entry.item_index, main);
    if (!inserted)
      found->second = found->second && main;
  }
  const auto ordinary_item = [&](std::size_t at) {
    return at < items.size() && items[at].kind == MachineItemKind::Op &&
        !items[at].raw && !items[at].manual_interaction;
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
      literals.emplace(i - 1U, Literal{begin, *range});
  }
  using Key = std::pair<std::size_t, int>; // X,Y,Z,T, then R0..Re
  std::map<Key, Range> cache;
  std::set<Key> active;
  std::function<Range(std::size_t, int, std::size_t)> before;
  std::function<Range(std::size_t, int, std::size_t)> after;
  after = [&](std::size_t state, int slot, std::size_t depth) -> Range {
    const auto at = flow.execution_states[state].item_index;
    if (!ordinary_item(at))
      return std::nullopt;
    const int code = items[at].opcode;
    const int family = code & 0xf0, reg = code & 15;
    const auto input = [&](int index) { return before(state, index, depth + 1U); };
    if (code == 0x50 || code == 0x4f || code == 0x6f ||
        (family >= 0x70 && family <= 0xe0 && reg == 15))
      return std::nullopt;
    if (slot >= 4) {
      const int wanted = slot - 4;
      if (family == 0x40 && reg == wanted)
        return input(0);
      if (family >= 0x70 && family <= 0xe0 && reg <= 6 && reg == wanted)
        return std::nullopt;
      constexpr std::array<int, 4> loops{0x5d, 0x5b, 0x58, 0x5a};
      if (wanted < 4 && code == loops[static_cast<std::size_t>(wanted)])
        return std::nullopt;
      if (family == 0xb0) {
        const auto targets = flow.indirect_memory_targets.find(at);
        if (targets == flow.indirect_memory_targets.end() ||
            std::find(targets->second.begin(), targets->second.end(), wanted) != targets->second.end())
          return std::nullopt;
      }
      // Unknown/dark opcodes cannot carry register facts across themselves.
      if ((code >= 0 && code <= 0x3b && code != 0x1f) ||
          (code >= 0x40 && code <= 0x4e) || (code >= 0x51 && code <= 0x5e) ||
          (code >= 0x60 && code <= 0x6e) || (family >= 0x70 && family <= 0xe0))
        return input(slot);
      return std::nullopt;
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
      if (valid && automatic_entry_lift[start] &&
          !external.contains(literal->second.begin))
        return slot == 0 ? Range{literal->second.value}
                         : before(start, slot - 1, depth + 1U);
    }
    if (family == 0x60)
      return slot == 0 ? input(4 + reg) : input(slot - 1);
    if (family == 0xd0) {
      if (slot != 0)
        return input(slot - 1);
      const auto targets = flow.indirect_memory_targets.find(at);
      if (targets == flow.indirect_memory_targets.end() || targets->second.empty())
        return std::nullopt;
      Range range;
      bool first = true;
      for (const int target : targets->second) {
        if (target < 0 || target >= 15 || (reg <= 6 && target == reg))
          return std::nullopt;
        const auto value = input(4 + target);
        range = first ? value : join(range, value);
        first = false;
      }
      return range;
    }
    if (family == 0x40 || family == 0xb0 || code == 0x51 || code == 0x52 ||
        code == 0x53 || code == 0x54 || opcode_by_code(code).takes_address ||
        (family >= 0x70 && family <= 0xe0))
      return input(slot);
    if (code == 0x0d)
      return slot == 0 ? bounded(0, 0, true) : input(slot);
    if (code == 0x0e || code == 0x0f)
      return slot == 0 ? (code == 0x0e ? input(0) : Range{}) : input(slot - 1);
    if (code == 0x14)
      return input(slot < 2 ? 1 - slot : slot);
    if (code == 0x25)
      return input((slot + 1) % 4);
    // Decimal entry with no complete fresh-literal proof is unknown.
    if (code <= 0x0c)
      return std::nullopt;
    const auto effect = opcode_by_code(code).stack_effect;
    if (slot == 0) {
      if (effect == StackEffect::ConsumeYDrop || effect == StackEffect::ConsumeYKeep)
        return binary(code, input(1), input(0));
      return unary(code, input(0));
    }
    if (effect == StackEffect::ConsumeYDrop || effect == StackEffect::Exposes)
      return input(std::min(slot + 1, 3));
    if (effect == StackEffect::ConsumeYKeep || effect == StackEffect::Preserves)
      return input(slot);
    if (effect == StackEffect::Shifts)
      return input(slot - 1);
    return std::nullopt;
  };
  before = [&](std::size_t state, int slot, std::size_t depth) -> Range {
    const Key key{state, slot};
    if (const auto found = cache.find(key); found != cache.end())
      return found->second;
    if (depth >= maximum_depth || ++result.work > maximum_work) {
      result.exhausted = true;
      return std::nullopt;
    }
    if (!active.insert(key).second)
      return std::nullopt; // An ungrounded recurrence is not an entry fact.
    Range range;
    bool first = true;
    const auto at = flow.execution_states[state].item_index;
    if (const auto root = external.find(at); root != external.end()) {
      if (root->second && preloads && slot >= 4 &&
          flow.execution_states[state].return_stack.empty()) {
        constexpr std::string_view names = "0123456789abcde";
        for (const auto& preload : *preloads)
          if (preload.register_name == std::string(1, names[static_cast<std::size_t>(slot - 4)]) &&
              !preload.setup_expression && !preload.setup_target_name)
            range = bound_ordinary_decimal(preload.value);
      }
      first = false;
    }
    for (const auto previous : predecessors[state]) {
      const auto value = after(previous, slot, depth + 1U);
      range = first ? value : join(range, value);
      first = false;
      if (!range)
        break;
    }
    active.erase(key);
    cache.emplace(key, range);
    return range;
  };
  for (const auto item : requested_items) {
    const auto found = contexts.find(item);
    if (found == contexts.end())
      continue;
    Range range;
    bool first = true;
    for (const auto state : found->second) {
      const auto value = before(state, 0, 0);
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

} // namespace mkpro::core
