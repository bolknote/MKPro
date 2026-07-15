#include "mkpro/core/helper_semantic_alias.hpp"

#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/post_layout_indirect_flow.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <tuple>
#include <utility>
#include <variant>

namespace mkpro::core {

namespace {

constexpr int kDirectCall = 0x53;
constexpr int kReturn = 0x52;

struct Rational {
  std::int64_t numerator = 0;
  std::int64_t denominator = 1;
  bool operator==(const Rational&) const = default;
};

std::optional<Rational> rational(std::int64_t numerator, std::int64_t denominator = 1) {
  if (denominator == 0 || numerator == std::numeric_limits<std::int64_t>::min() ||
      denominator == std::numeric_limits<std::int64_t>::min())
    return std::nullopt;
  if (denominator < 0) {
    if (numerator == std::numeric_limits<std::int64_t>::min() ||
        denominator == std::numeric_limits<std::int64_t>::min())
      return std::nullopt;
    numerator = -numerator;
    denominator = -denominator;
  }
  const std::int64_t divisor = std::gcd(numerator, denominator);
  return Rational{numerator / divisor, denominator / divisor};
}

std::optional<std::int64_t> checked_binary(std::int64_t left, std::int64_t right, char op) {
  if (op == '+') {
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right))
      return std::nullopt;
    return left + right;
  }
  if (op == '-') {
    if (right == std::numeric_limits<std::int64_t>::min())
      return left >= 0 ? std::nullopt : std::optional<std::int64_t>{left - right};
    return checked_binary(left, -right, '+');
  }
  if (left != 0) {
    if ((left == -1 && right == std::numeric_limits<std::int64_t>::min()) ||
        (right == -1 && left == std::numeric_limits<std::int64_t>::min()))
      return std::nullopt;
    if (left > 0) {
      if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() / right) ||
          (right < 0 && right < std::numeric_limits<std::int64_t>::min() / left))
        return std::nullopt;
    } else if ((right > 0 && left < std::numeric_limits<std::int64_t>::min() / right) ||
               (right < 0 && left < std::numeric_limits<std::int64_t>::max() / right)) {
      return std::nullopt;
    }
  }
  return left * right;
}

std::optional<Rational> add(Rational left, Rational right, bool subtract) {
  const auto a = checked_binary(left.numerator, right.denominator, '*');
  const auto b = checked_binary(right.numerator, left.denominator, '*');
  const auto d = checked_binary(left.denominator, right.denominator, '*');
  if (!a || !b || !d)
    return std::nullopt;
  const auto n = checked_binary(*a, *b, subtract ? '-' : '+');
  return n ? rational(*n, *d) : std::nullopt;
}

std::optional<Rational> multiply(Rational left, Rational right, bool divide) {
  const auto n = checked_binary(left.numerator, divide ? right.denominator : right.numerator, '*');
  const auto d =
      checked_binary(left.denominator, divide ? right.numerator : right.denominator, '*');
  return n && d ? rational(*n, *d) : std::nullopt;
}

std::int64_t truncate(Rational value) {
  return value.numerator / value.denominator;
}

std::optional<Rational> evaluate(const HelperSemanticExprPtr& expression, std::int64_t input) {
  if (!expression)
    return std::nullopt;
  switch (expression->op) {
  case HelperSemanticOp::InputX:
    return rational(input);
  case HelperSemanticOp::Integer:
    return rational(expression->integer);
  case HelperSemanticOp::Truncate: {
    const auto value = evaluate(expression->left, input);
    return value ? rational(truncate(*value)) : std::nullopt;
  }
  case HelperSemanticOp::Fraction: {
    const auto value = evaluate(expression->left, input);
    return value ? add(*value, Rational{truncate(*value), 1}, true) : std::nullopt;
  }
  case HelperSemanticOp::OneBasedModulo: {
    const auto value = evaluate(expression->left, input);
    if (!value || expression->width <= 0)
      return std::nullopt;
    const std::int64_t integer = truncate(*value);
    std::int64_t remainder = integer % expression->width;
    if (remainder <= 0)
      remainder += expression->width;
    return rational(remainder);
  }
  case HelperSemanticOp::Add:
  case HelperSemanticOp::Subtract:
  case HelperSemanticOp::Multiply:
  case HelperSemanticOp::Divide: {
    const auto left = evaluate(expression->left, input);
    const auto right = evaluate(expression->right, input);
    if (!left || !right)
      return std::nullopt;
    if (expression->op == HelperSemanticOp::Add)
      return add(*left, *right, false);
    if (expression->op == HelperSemanticOp::Subtract)
      return add(*left, *right, true);
    return multiply(*left, *right, expression->op == HelperSemanticOp::Divide);
  }
  }
  return std::nullopt;
}

bool decimal_representable(Rational value) {
  std::int64_t denominator = value.denominator;
  int twos = 0;
  int fives = 0;
  while (denominator % 2 == 0) {
    denominator /= 2;
    ++twos;
  }
  while (denominator % 5 == 0) {
    denominator /= 5;
    ++fives;
  }
  if (denominator != 1)
    return false;
  std::int64_t digits = value.numerator;
  const int scale_twos = std::max(0, fives - twos);
  const int scale_fives = std::max(0, twos - fives);
  for (int index = 0; index < scale_twos; ++index) {
    const auto scaled = checked_binary(digits, 2, '*');
    if (!scaled)
      return false;
    digits = *scaled;
  }
  for (int index = 0; index < scale_fives; ++index) {
    const auto scaled = checked_binary(digits, 5, '*');
    if (!scaled)
      return false;
    digits = *scaled;
  }
  if (digits == 0)
    return true;
  while (digits % 10 == 0)
    digits /= 10;
  if (digits < 0)
    digits = -digits;
  int significant = 0;
  while (digits != 0) {
    digits /= 10;
    ++significant;
  }
  return significant <= 8;
}

std::optional<Rational> evaluate_decimal_exact(const HelperSemanticExprPtr& expression,
                                               std::int64_t input) {
  if (!expression)
    return std::nullopt;
  std::optional<Rational> result;
  switch (expression->op) {
  case HelperSemanticOp::InputX:
    result = rational(input);
    break;
  case HelperSemanticOp::Integer:
    result = rational(expression->integer);
    break;
  case HelperSemanticOp::Truncate: {
    const auto value = evaluate_decimal_exact(expression->left, input);
    result = value ? rational(truncate(*value)) : std::nullopt;
    break;
  }
  case HelperSemanticOp::Fraction: {
    const auto value = evaluate_decimal_exact(expression->left, input);
    result = value ? add(*value, Rational{truncate(*value), 1}, true) : std::nullopt;
    break;
  }
  case HelperSemanticOp::OneBasedModulo: {
    const auto value = evaluate_decimal_exact(expression->left, input);
    if (!value || expression->width <= 0)
      return std::nullopt;
    const Rational integer{truncate(*value), 1};
    const auto quotient = multiply(integer, Rational{expression->width, 1}, true);
    if (!quotient || !decimal_representable(*quotient))
      return std::nullopt;
    const auto fraction = add(*quotient, Rational{truncate(*quotient), 1}, true);
    if (!fraction || !decimal_representable(*fraction))
      return std::nullopt;
    const auto scaled = multiply(*fraction, Rational{expression->width, 1}, false);
    if (!scaled || !decimal_representable(*scaled))
      return std::nullopt;
    result = scaled->numerator <= 0 ? add(*scaled, Rational{expression->width, 1}, false) : scaled;
    break;
  }
  case HelperSemanticOp::Add:
  case HelperSemanticOp::Subtract:
  case HelperSemanticOp::Multiply:
  case HelperSemanticOp::Divide: {
    const auto left = evaluate_decimal_exact(expression->left, input);
    const auto right = evaluate_decimal_exact(expression->right, input);
    if (!left || !right)
      return std::nullopt;
    if (expression->op == HelperSemanticOp::Add)
      result = add(*left, *right, false);
    else if (expression->op == HelperSemanticOp::Subtract)
      result = add(*left, *right, true);
    else
      result = multiply(*left, *right, expression->op == HelperSemanticOp::Divide);
    break;
  }
  }
  return result && decimal_representable(*result) ? result : std::nullopt;
}

bool semantics_equal(const HelperSemanticContract& left, const HelperSemanticContract& right,
                     std::size_t maximum_values) {
  const bool input_may_be_zero = left.admitted_input.valid() && left.admitted_input.minimum <= 0 &&
                                 left.admitted_input.maximum >= 0;
  if (!left.expression || !right.expression || !left.admitted_input.valid() ||
      !right.admitted_input.valid() || right.admitted_input.minimum > left.admitted_input.minimum ||
      right.admitted_input.maximum < left.admitted_input.maximum || !left.decimal_execution_exact ||
      !right.decimal_execution_exact || !left.input_decimal_derivation_exact ||
      !right.input_decimal_derivation_exact ||
      (input_may_be_zero &&
       (!left.input_zero_canonical_positive || !right.input_zero_canonical_positive)) ||
      left.admitted_input.cardinality() > maximum_values)
    return false;
  for (std::int64_t value = left.admitted_input.minimum;; ++value) {
    const auto a = evaluate(left.expression, value);
    const auto b = evaluate(right.expression, value);
    if (!a || !b || *a != *b)
      return false;
    // Rational equality deliberately has no signed-zero representation. On
    // the MK-61, however, +0 and -0 remain distinguishable by later number
    // entry (notably VP). Until a separate machine-shape proof accompanies a
    // contract, a zero result cannot justify a semantic helper alias.
    if (a->numerator == 0)
      return false;
    if (value == left.admitted_input.maximum)
      break;
  }
  return true;
}

struct ArtifactIndex {
  std::vector<int> addresses;
  std::map<std::string, std::size_t> labels;
  std::map<int, std::size_t> cells;
  std::set<std::string> duplicates;
  int cell_count = 0;
};

ArtifactIndex index_artifact(const std::vector<MachineItem>& items) {
  ArtifactIndex result;
  result.addresses.resize(items.size());
  int address = 0;
  for (std::size_t index = 0; index < items.size(); ++index) {
    result.addresses.at(index) = address;
    const MachineItem& item = items.at(index);
    if (item.kind == MachineItemKind::Label) {
      if (result.labels.contains(item.name))
        result.duplicates.insert(item.name);
      result.labels[item.name] = index;
    } else {
      result.cells[address++] = index;
    }
  }
  result.cell_count = address;
  return result;
}

struct HelperRegion {
  bool valid = false;
  std::size_t first_item = 0;
  std::size_t first_cell = 0;
  std::size_t return_item = 0;
  int first_address = -1;
  int last_address = -1;
  int cells = 0;
};

bool direct_register_access(int opcode) {
  return (opcode >= 0x40 && opcode <= 0x4f) || (opcode >= 0x60 && opcode <= 0x6f);
}

std::optional<int> target_address(const MachineItem& item, const ArtifactIndex& index);

HelperRegion helper_region(const std::vector<MachineItem>& items, const ArtifactIndex& index,
                           const std::string& label, std::size_t maximum_cells) {
  HelperRegion result;
  if (index.duplicates.contains(label))
    return result;
  const auto root = index.labels.find(label);
  if (root == index.labels.end())
    return result;
  result.first_item = root->second;
  result.first_address = index.addresses.at(root->second);
  std::size_t cursor = root->second;
  while (cursor < items.size() && items.at(cursor).kind == MachineItemKind::Label)
    ++cursor;
  result.first_cell = cursor;
  for (; cursor < items.size(); ++cursor) {
    const MachineItem& item = items.at(cursor);
    if (item.kind == MachineItemKind::Label) {
      if (item.procedure_boundary == "start" && index.addresses.at(cursor) != result.first_address)
        return HelperRegion{};
      continue;
    }
    if (item.kind == MachineItemKind::Address) {
      if (cursor == 0 || items.at(cursor - 1U).kind != MachineItemKind::Op ||
          !opcode_by_code(items.at(cursor - 1U).opcode).takes_address)
        return HelperRegion{};
      ++result.cells;
      if (result.cells > static_cast<int>(maximum_cells))
        return HelperRegion{};
      continue;
    }
    if (item.kind != MachineItemKind::Op || item.raw || direct_register_access(item.opcode) ||
        (item.opcode & 0xf0) >= 0x70 || item.opcode == kDirectCall)
      return HelperRegion{};
    ++result.cells;
    if (result.cells > static_cast<int>(maximum_cells))
      return HelperRegion{};
    if (item.opcode == kReturn) {
      result.return_item = cursor;
      result.last_address = index.addresses.at(cursor);
      for (std::size_t item_index = result.first_cell; item_index <= cursor; ++item_index) {
        const MachineItem& operand = items.at(item_index);
        if (operand.kind != MachineItemKind::Address)
          continue;
        int target = -1;
        if (const auto* fixed = std::get_if<int>(&operand.target)) {
          target = *fixed;
        } else {
          const auto found = index.labels.find(std::get<std::string>(operand.target));
          if (found == index.labels.end())
            return HelperRegion{};
          target = index.addresses.at(found->second);
        }
        if (target < result.first_address || target > result.last_address)
          return HelperRegion{};
      }
      result.valid = true;
      return result;
    }
    if (item.opcode == 0x50 || item.opcode == 0x51)
      return HelperRegion{};
  }
  return HelperRegion{};
}

std::optional<std::string> helper_body_key(const std::vector<MachineItem>& items,
                                           const ArtifactIndex& index, const HelperRegion& region) {
  if (!region.valid)
    return std::nullopt;
  std::string result;
  for (std::size_t item_index = region.first_cell; item_index <= region.return_item; ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind == MachineItemKind::Label)
      continue;
    if (item.kind == MachineItemKind::Op) {
      result += "O" + std::to_string(item.opcode) + ";";
      continue;
    }
    if (item.kind != MachineItemKind::Address)
      return std::nullopt;
    const std::optional<int> target = target_address(item, index);
    if (!target.has_value() || *target < region.first_address || *target > region.last_address)
      return std::nullopt;
    result += "A" + std::to_string(*target - region.first_address) + ";";
  }
  return result.empty() ? std::nullopt : std::optional<std::string>{std::move(result)};
}

void reason(HelperSemanticAliasProof& proof, std::string text) {
  if (std::find(proof.reasons.begin(), proof.reasons.end(), text) == proof.reasons.end())
    proof.reasons.push_back(std::move(text));
}

std::optional<std::size_t> next_cell(const std::vector<MachineItem>& items, std::size_t after) {
  for (std::size_t index = after; index < items.size(); ++index)
    if (items.at(index).kind != MachineItemKind::Label)
      return index;
  return std::nullopt;
}

std::optional<std::size_t> previous_cell(const std::vector<MachineItem>& items,
                                         std::size_t before) {
  while (before > 0) {
    --before;
    if (items.at(before).kind != MachineItemKind::Label)
      return before;
  }
  return std::nullopt;
}

std::optional<int> target_address(const MachineItem& item, const ArtifactIndex& index) {
  if (item.kind != MachineItemKind::Address)
    return std::nullopt;
  if (const auto* fixed = std::get_if<int>(&item.target))
    return *fixed;
  const auto found = index.labels.find(std::get<std::string>(item.target));
  return found == index.labels.end() ? std::nullopt
                                     : std::optional<int>{index.addresses.at(found->second)};
}

bool fallthrough_fenced(const std::vector<MachineItem>& items, const HelperRegion& region) {
  const auto previous = previous_cell(items, region.first_item);
  if (!previous)
    return false;
  const MachineItem& item = items.at(*previous);
  if (item.kind == MachineItemKind::Op)
    return item.opcode == 0x50 || item.opcode == kReturn || (item.opcode & 0xf0) == 0x80;
  const auto flow = previous_cell(items, *previous);
  return flow && items.at(*flow).kind == MachineItemKind::Op && items.at(*flow).opcode == 0x51;
}

struct AliasCall {
  std::size_t call_item = 0;
  std::optional<std::size_t> operand_item;
  std::size_t continuation_item = 0;
  bool indirect = false;
};

struct IncomingCalls {
  bool valid = true;
  std::vector<AliasCall> calls;
  std::vector<std::string> reasons;
};

bool indirect_call_selector_is_stable(int opcode) {
  // On the MK-61, indirect addressing through R0..R3 decrements the selector
  // and R4..R6 increments it.  Directizing such a call would silently remove
  // that register mutation.  R7..Re have no selector side effect.
  return opcode >= 0xa7 && opcode <= 0xae;
}

void incoming_reason(IncomingCalls& result, std::string text) {
  result.valid = false;
  if (std::find(result.reasons.begin(), result.reasons.end(), text) == result.reasons.end())
    result.reasons.push_back(std::move(text));
}

IncomingCalls collect_incoming_calls(const std::vector<MachineItem>& items,
                                     const ArtifactIndex& index, const HelperRegion& region,
                                     const AuthoritativePostLayoutControlFlow& flow) {
  IncomingCalls result;
  if (!fallthrough_fenced(items, region))
    incoming_reason(result, "official control can fall through into removed helper");

  std::set<std::size_t> call_items;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind != MachineItemKind::Address)
      continue;
    const auto target = target_address(item, index);
    if (!target || *target < region.first_address || *target > region.last_address)
      continue;
    const auto command = previous_cell(items, item_index);
    if (command && index.addresses.at(*command) >= region.first_address &&
        index.addresses.at(*command) <= region.last_address)
      continue;
    if (*target != region.first_address || !command ||
        items.at(*command).kind != MachineItemKind::Op ||
        items.at(*command).opcode != kDirectCall) {
      incoming_reason(result, "direct jump or alternate entry targets removed helper");
      continue;
    }
    const auto continuation = next_cell(items, item_index + 1U);
    if (!continuation) {
      incoming_reason(result, "direct helper call has no continuation");
      continue;
    }
    if (call_items.insert(*command).second)
      result.calls.push_back(AliasCall{*command, item_index, *continuation, false});
  }

  for (const auto& [flow_item, targets] : flow.indirect_flow_targets) {
    if (flow_item < index.addresses.size() &&
        index.addresses.at(flow_item) >= region.first_address &&
        index.addresses.at(flow_item) <= region.last_address)
      continue;
    bool enters = false;
    bool exact_root = !targets.empty();
    for (const PostLayoutCommandIdentity& target : targets) {
      const bool inside =
          target.address >= region.first_address && target.address <= region.last_address;
      enters = enters || inside;
      exact_root = exact_root && target.address == region.first_address;
    }
    if (!enters)
      continue;
    if (flow_item >= items.size() || items.at(flow_item).kind != MachineItemKind::Op ||
        (items.at(flow_item).opcode & 0xf0) != 0xa0 || !exact_root) {
      incoming_reason(result, "indirect jump/multitarget entry targets removed helper");
      continue;
    }
    if (!indirect_call_selector_is_stable(items.at(flow_item).opcode)) {
      incoming_reason(result, "indirect helper call uses a mutating R0..R6 selector");
      continue;
    }
    const auto continuation = next_cell(items, flow_item + 1U);
    if (!continuation) {
      incoming_reason(result, "indirect helper call has no continuation");
      continue;
    }
    if (call_items.insert(flow_item).second)
      result.calls.push_back(AliasCall{flow_item, std::nullopt, *continuation, true});
  }

  for (const PostLayoutExternalEntryState& entry : flow.external_entries) {
    if (entry.entry.address >= region.first_address && entry.entry.address <= region.last_address)
      incoming_reason(result, "manual/external entry reaches removed helper");
    for (const PostLayoutCommandIdentity& destination : entry.return_stack)
      if (destination.address >= region.first_address && destination.address <= region.last_address)
        incoming_reason(result, "external return stack reaches removed helper");
  }
  if (result.calls.empty())
    incoming_reason(result, "removed helper has no complete call set");
  std::sort(result.calls.begin(), result.calls.end(),
            [](const AliasCall& left, const AliasCall& right) {
              return left.call_item < right.call_item;
            });
  return result;
}

// These are deliberately physical-X1 facts, not OpcodeInfo::x2_effect facts.
// Emulator probes pin binary arithmetic and F 10^x as reconvergence points:
// from equal visible X/Y/Z/T and register state, their result no longer depends
// on a differing physical X1 context.
bool x1_reconverges_from_equal_visible_state(int opcode) {
  return (opcode >= 0x10 && opcode <= 0x13) || opcode == 0x15;
}

// This is the intentionally small transfer whitelist needed before a proved
// reconvergence point. Decimal digits and direct register recalls preserve the
// invariant "visible state, hidden X2, and number-entry context are equal; only
// physical X1 may differ". In particular, recalls do not reconverge X1.
// Unknown commands fail closed.
bool x1_preserves_equal_visible_state(int opcode) {
  return (opcode >= 0x00 && opcode <= 0x09) || (opcode >= 0x60 && opcode <= 0x6e);
}

struct X1State {
  std::size_t item = 0;
  std::vector<std::size_t> returns;
  bool operator<(const X1State& other) const {
    return std::tie(item, returns) < std::tie(other.item, other.returns);
  }
};

// Conservative bounded proof for a differing physical X1 context. V1 walks
// straight-line code plus exact call/return and unconditional-jump edges to an
// emulator-pinned reconvergence command. Any conditional flow, interaction,
// stop, restore, or unknown transfer is rejected rather than conflating X1
// with the separately modelled X2 effect.
bool continuation_forgets_x1(const std::vector<MachineItem>& items, const ArtifactIndex& index,
                             std::size_t start, std::size_t maximum_states) {
  std::vector<X1State> work{{start, {}}};
  std::set<X1State> seen;
  while (!work.empty()) {
    X1State state = std::move(work.back());
    work.pop_back();
    if (!seen.insert(state).second)
      continue;
    if (seen.size() > maximum_states || state.item >= items.size())
      return false;
    const MachineItem& item = items.at(state.item);
    if (item.kind == MachineItemKind::Label) {
      ++state.item;
      work.push_back(std::move(state));
      continue;
    }
    if (item.kind != MachineItemKind::Op || item.raw || item.manual_interaction.has_value())
      return false;
    const int opcode = item.opcode;
    if (x1_reconverges_from_equal_visible_state(opcode))
      continue;
    if (opcode == kDirectCall) {
      const auto operand = next_cell(items, state.item + 1U);
      if (!operand || items.at(*operand).kind != MachineItemKind::Address ||
          state.returns.size() >= 5U)
        return false;
      const auto address = target_address(items.at(*operand), index);
      const auto target = address ? index.cells.find(*address) : index.cells.end();
      const auto continuation = next_cell(items, *operand + 1U);
      if (target == index.cells.end() || !continuation)
        return false;
      state.returns.push_back(*continuation);
      state.item = target->second;
      work.push_back(std::move(state));
      continue;
    }
    if (opcode == kReturn) {
      if (state.returns.empty())
        return false;
      state.item = state.returns.back();
      state.returns.pop_back();
      work.push_back(std::move(state));
      continue;
    }
    if (!x1_preserves_equal_visible_state(opcode))
      return false;
    const auto next = next_cell(items, state.item + 1U);
    if (!next)
      return false;
    state.item = *next;
    work.push_back(std::move(state));
  }
  return true;
}

bool is_indirect_flow(int opcode) {
  const int family = opcode & 0xf0;
  return family == 0x70 || family == 0x80 || family == 0x90 || family == 0xa0 || family == 0xc0 ||
         family == 0xe0;
}

bool symbolically_relocatable(const std::vector<MachineItem>& items,
                              bool identity_check_numeric_indirect_flow,
                              bool* fully_symbolic = nullptr, std::string* rejection = nullptr) {
  if (fully_symbolic != nullptr)
    *fully_symbolic = true;
  const auto reject = [&](std::string why) {
    if (rejection != nullptr)
      *rejection = std::move(why);
    return false;
  };
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind == MachineItemKind::Address) {
      if (!std::holds_alternative<std::string>(item.target))
        return reject("numeric Address at item " + std::to_string(item_index));
      if (item.raw || item.formal_opcode.has_value() || !item.roles.empty())
        return reject("formal/raw/role Address at item " + std::to_string(item_index));
    }
    if (item.kind != MachineItemKind::Op || !is_indirect_flow(item.opcode))
      continue;
    if (!item.indirect_flow_targets.has_value() || item.indirect_flow_targets->empty())
      return reject("unknown indirect flow at item " + std::to_string(item_index));
    if (std::any_of(
            item.indirect_flow_targets->begin(), item.indirect_flow_targets->end(),
            [](const IrTarget& target) { return !std::holds_alternative<std::string>(target); })) {
      if (!identity_check_numeric_indirect_flow)
        return reject("numeric indirect-flow fact at item " + std::to_string(item_index));
      if (fully_symbolic != nullptr)
        *fully_symbolic = false;
    }
  }
  return true;
}

using ExternalIdentity =
    std::tuple<std::size_t, std::vector<std::size_t>, int, bool, int, int, int>;

std::optional<std::vector<ExternalIdentity>>
external_identities(const AuthoritativePostLayoutControlFlow& flow,
                    const std::map<std::size_t, std::size_t>& origin_by_item) {
  std::vector<ExternalIdentity> result;
  for (const PostLayoutExternalEntryState& entry : flow.external_entries) {
    const auto command = origin_by_item.find(entry.entry.item_index);
    if (command == origin_by_item.end())
      return std::nullopt;
    std::vector<std::size_t> returns;
    for (const PostLayoutCommandIdentity& slot : entry.return_stack) {
      const auto origin = origin_by_item.find(slot.item_index);
      if (origin == origin_by_item.end())
        return std::nullopt;
      returns.push_back(origin->second);
    }
    const bool manual = entry.manual_interaction.has_value();
    result.emplace_back(command->second, std::move(returns), static_cast<int>(entry.kind), manual,
                        manual ? entry.manual_interaction->protocol_id : -1,
                        manual ? entry.manual_interaction->phase : -1,
                        manual ? static_cast<int>(entry.manual_interaction->kind) : -1);
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::optional<std::map<std::size_t, std::vector<std::size_t>>>
indirect_flow_identities(const AuthoritativePostLayoutControlFlow& flow,
                         const std::map<std::size_t, std::size_t>& origin_by_item) {
  std::map<std::size_t, std::vector<std::size_t>> result;
  for (const auto& [command, targets] : flow.indirect_flow_targets) {
    const auto command_origin = origin_by_item.find(command);
    if (command_origin == origin_by_item.end())
      return std::nullopt;
    std::vector<std::size_t> target_origins;
    for (const PostLayoutCommandIdentity& target : targets) {
      const auto target_origin = origin_by_item.find(target.item_index);
      if (target_origin == origin_by_item.end())
        return std::nullopt;
      target_origins.push_back(target_origin->second);
    }
    std::sort(target_origins.begin(), target_origins.end());
    if (!result.emplace(command_origin->second, std::move(target_origins)).second)
      return std::nullopt;
  }
  return result;
}

std::optional<std::map<std::size_t, std::vector<int>>>
indirect_memory_identities(const AuthoritativePostLayoutControlFlow& flow,
                           const std::map<std::size_t, std::size_t>& origin_by_item) {
  std::map<std::size_t, std::vector<int>> result;
  for (const auto& [command, targets] : flow.indirect_memory_targets) {
    const auto command_origin = origin_by_item.find(command);
    if (command_origin == origin_by_item.end() ||
        !result.emplace(command_origin->second, targets).second)
      return std::nullopt;
  }
  return result;
}

std::optional<std::size_t>
empty_return_identity(const AuthoritativePostLayoutControlFlow& flow,
                      const std::map<std::size_t, std::size_t>& origin_by_item) {
  if (!flow.empty_return_target.has_value())
    return std::numeric_limits<std::size_t>::max();
  const auto found = origin_by_item.find(flow.empty_return_target->item_index);
  return found == origin_by_item.end() ? std::nullopt : std::optional<std::size_t>{found->second};
}

struct AliasCandidate {
  HelperSemanticAliasResult result;
  int source_address = -1;
};

} // namespace

std::uint64_t ExactIntegralDomain::cardinality() const {
  if (!valid())
    return 0;
  if (minimum < 0 && maximum >= 0) {
    const std::uint64_t negative = static_cast<std::uint64_t>(-(minimum + 1)) + 1U;
    const std::uint64_t positive = static_cast<std::uint64_t>(maximum) + 1U;
    if (negative > std::numeric_limits<std::uint64_t>::max() - positive)
      return std::numeric_limits<std::uint64_t>::max();
    return negative + positive;
  }
  return static_cast<std::uint64_t>(maximum - minimum) + 1U;
}

HelperSemanticExprPtr helper_semantic_input() {
  return std::make_shared<HelperSemanticExpr>();
}

HelperSemanticExprPtr helper_semantic_integer(std::int64_t value) {
  auto result = std::make_shared<HelperSemanticExpr>();
  result->op = HelperSemanticOp::Integer;
  result->integer = value;
  return result;
}

HelperSemanticExprPtr helper_semantic_unary(HelperSemanticOp op, HelperSemanticExprPtr value) {
  auto result = std::make_shared<HelperSemanticExpr>();
  result->op = op;
  result->left = std::move(value);
  return result;
}

HelperSemanticExprPtr helper_semantic_binary(HelperSemanticOp op, HelperSemanticExprPtr left,
                                             HelperSemanticExprPtr right) {
  auto result = std::make_shared<HelperSemanticExpr>();
  result->op = op;
  result->left = std::move(left);
  result->right = std::move(right);
  return result;
}

HelperSemanticExprPtr helper_semantic_one_based_modulo(HelperSemanticExprPtr value, int width) {
  auto result = std::make_shared<HelperSemanticExpr>();
  result->op = HelperSemanticOp::OneBasedModulo;
  result->left = std::move(value);
  result->width = width;
  return result;
}

bool helper_semantic_decimal_execution_exact(const HelperSemanticExprPtr& expression,
                                             const ExactIntegralDomain& domain,
                                             std::size_t maximum_values) {
  if (!expression || !domain.valid() || domain.cardinality() > maximum_values)
    return false;
  for (std::int64_t value = domain.minimum;; ++value) {
    if (!evaluate_decimal_exact(expression, value))
      return false;
    if (value == domain.maximum)
      break;
  }
  return true;
}

std::optional<std::string> helper_semantic_alias_body_key(const std::vector<MachineItem>& items,
                                                          const std::string& entry_label,
                                                          std::size_t maximum_helper_cells) {
  const ArtifactIndex index = index_artifact(items);
  const HelperRegion region = helper_region(items, index, entry_label, maximum_helper_cells);
  return helper_body_key(items, index, region);
}

HelperSemanticAliasResult
optimize_helper_semantic_alias(const std::vector<MachineItem>& items,
                               const std::vector<HelperSemanticContract>& contracts,
                               const HelperSemanticAliasOptions& options) {
  HelperSemanticAliasResult rejection;
  rejection.items = items;
  rejection.preloads = options.effective_preloads;
  rejection.proof.input_cells = machine_cell_count(items);
  const ArtifactIndex index = index_artifact(items);
  if (!options.effective_preloads.empty() && !options.defer_preload_rebind) {
    reason(rejection.proof, "helper alias requires an empty effective-preload ledger unless "
                            "downstream rebinding is explicit");
    return rejection;
  }
  std::string relocation_rejection;
  bool fully_symbolic = false;
  if (!symbolically_relocatable(items, options.identity_check_numeric_indirect_flow,
                                &fully_symbolic, &relocation_rejection)) {
    reason(rejection.proof, "helper alias requires symbolic or identity-rebound flow targets: " +
                                relocation_rejection);
    return rejection;
  }

  PostLayoutControlFlowOptions pre_options;
  pre_options.address_space_model = options.address_space_model;
  pre_options.maximum_execution_states =
      std::max<std::size_t>(20000U, options.maximum_continuation_states);
  pre_options.main_entry = options.main_entry;
  pre_options.empty_return_target = options.empty_return_target;
  const AuthoritativePostLayoutControlFlow pre_flow =
      build_post_layout_control_flow(items, pre_options);
  rejection.proof.pre_control_flow_proved = pre_flow.proved;
  if (!pre_flow.proved) {
    reason(rejection.proof, "authoritative pre-alias control flow is not proved");
    for (const std::string& why : pre_flow.reasons)
      reason(rejection.proof, "pre CFG: " + why);
    return rejection;
  }

  std::map<std::size_t, std::size_t> original_origin_by_item;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index)
    original_origin_by_item.emplace(item_index, item_index);
  const auto original_external = external_identities(pre_flow, original_origin_by_item);
  const auto original_indirect_flow = indirect_flow_identities(pre_flow, original_origin_by_item);
  const auto original_indirect_memory =
      indirect_memory_identities(pre_flow, original_origin_by_item);
  const auto original_empty_return = empty_return_identity(pre_flow, original_origin_by_item);
  if (!original_external || !original_indirect_flow || !original_indirect_memory ||
      !original_empty_return) {
    reason(rejection.proof, "pre-alias command identities are incomplete");
    return rejection;
  }

  std::optional<AliasCandidate> best;
  int complete_sources = 0;
  int source_regions = 0;
  int semantic_pairs = 0;
  int target_regions = 0;
  int complete_incoming_sets = 0;
  int exact_call_sets = 0;
  int exact_origin_sets = 0;
  int x1_safe_pairs = 0;
  int removable_boundaries = 0;
  for (const HelperSemanticContract& source : contracts) {
    if (!source.all_source_entries_accounted)
      continue;
    ++complete_sources;
    const HelperRegion source_region =
        helper_region(items, index, source.entry_label, options.maximum_helper_cells);
    const std::optional<std::string> source_body_key = helper_body_key(items, index, source_region);
    if (!source_region.valid || source.certified_body_key.empty() || !source_body_key.has_value() ||
        *source_body_key != source.certified_body_key)
      continue;
    ++source_regions;
    for (const HelperSemanticContract& target : contracts) {
      if (&source == &target || source.abi != target.abi ||
          !semantics_equal(source, target, options.maximum_domain_values))
        continue;
      ++semantic_pairs;
      const HelperRegion target_region =
          helper_region(items, index, target.entry_label, options.maximum_helper_cells);
      const std::optional<std::string> target_body_key =
          helper_body_key(items, index, target_region);
      if (!target_region.valid || target.certified_body_key.empty() ||
          !target_body_key.has_value() || *target_body_key != target.certified_body_key ||
          source_region.first_address == target_region.first_address ||
          !source.hidden_x2_return_sync_proved || !target.hidden_x2_return_sync_proved ||
          !source.x1_effect_proved || !target.x1_effect_proved || source.x1_effect_key.empty() ||
          target.x1_effect_key.empty())
        continue;
      ++target_regions;

      IncomingCalls incoming = collect_incoming_calls(items, index, source_region, pre_flow);
      if (!incoming.valid)
        continue;
      ++complete_incoming_sets;
      std::set<std::size_t> admitted(source.admitted_call_items.begin(),
                                     source.admitted_call_items.end());
      std::set<std::size_t> discovered;
      for (const AliasCall& call : incoming.calls)
        discovered.insert(call.call_item);
      if (admitted.size() != source.admitted_call_items.size() || admitted != discovered)
        continue;
      ++exact_call_sets;
      const std::set<std::uint64_t> admitted_origins(source.admitted_call_origins.begin(),
                                                     source.admitted_call_origins.end());
      if (admitted_origins.size() != source.admitted_call_origins.size())
        continue;
      std::map<std::uint64_t, int> origin_occurrences;
      bool origins_complete = true;
      if (!admitted_origins.empty()) {
        for (const AliasCall& call : incoming.calls) {
          if (call.call_item >= items.size() ||
              items.at(call.call_item).semantic_call_origins.empty()) {
            origins_complete = false;
            break;
          }
          for (const std::uint64_t origin : items.at(call.call_item).semantic_call_origins) {
            if (!admitted_origins.contains(origin)) {
              origins_complete = false;
              break;
            }
            ++origin_occurrences[origin];
          }
          if (!origins_complete)
            break;
        }
        for (const std::uint64_t origin : admitted_origins)
          origins_complete = origins_complete && origin_occurrences[origin] == 1;
      }
      if (!origins_complete)
        continue;
      ++exact_origin_sets;

      bool x1_safe = true;
      if (source.x1_effect_key != target.x1_effect_key) {
        for (const AliasCall& call : incoming.calls) {
          if (!continuation_forgets_x1(items, index, call.continuation_item,
                                       options.maximum_continuation_states)) {
            x1_safe = false;
            break;
          }
        }
      }
      if (!x1_safe)
        continue;
      ++x1_safe_pairs;

      if (source.procedure_boundary_id.empty())
        continue;
      std::size_t erase_first = source_region.first_item;
      bool unrelated_start = false;
      while (erase_first > 0 && items.at(erase_first - 1U).kind == MachineItemKind::Label &&
             index.addresses.at(erase_first - 1U) == source_region.first_address &&
             items.at(erase_first - 1U).procedure_boundary != "end") {
        --erase_first;
      }
      for (std::size_t label = erase_first; label < source_region.first_cell; ++label) {
        if (items.at(label).kind == MachineItemKind::Label &&
            items.at(label).procedure_boundary == "start" &&
            items.at(label).procedure_name != source.procedure_boundary_id)
          unrelated_start = true;
      }
      if (unrelated_start)
        continue;
      std::size_t erase_after = source_region.return_item + 1U;
      if (erase_after >= items.size() || items.at(erase_after).kind != MachineItemKind::Label ||
          items.at(erase_after).procedure_boundary != "end" ||
          items.at(erase_after).procedure_name != source.procedure_boundary_id)
        continue;
      while (erase_after < items.size() && items.at(erase_after).kind == MachineItemKind::Label &&
             items.at(erase_after).procedure_boundary == "end" &&
             items.at(erase_after).procedure_name == source.procedure_boundary_id)
        ++erase_after;
      ++removable_boundaries;

      std::set<std::size_t> call_operands;
      std::set<std::size_t> indirect_calls;
      bool calls_well_formed = true;
      for (const AliasCall& call : incoming.calls) {
        if (call.call_item >= erase_first && call.call_item < erase_after) {
          calls_well_formed = false;
          break;
        }
        if (call.indirect) {
          if (call.operand_item.has_value()) {
            calls_well_formed = false;
            break;
          }
          indirect_calls.insert(call.call_item);
        } else if (!call.operand_item.has_value() || *call.operand_item >= items.size() ||
                   (*call.operand_item >= erase_first && *call.operand_item < erase_after)) {
          calls_well_formed = false;
          break;
        } else {
          call_operands.insert(*call.operand_item);
        }
      }
      if (!calls_well_formed ||
          call_operands.size() + indirect_calls.size() != incoming.calls.size())
        continue;

      std::vector<MachineItem> rewritten;
      rewritten.reserve(items.size() - (erase_after - erase_first) + indirect_calls.size());
      std::map<std::size_t, std::size_t> new_item_by_origin;
      for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
        if (item_index >= erase_first && item_index < erase_after)
          continue;
        MachineItem copy = items.at(item_index);
        if (indirect_calls.contains(item_index)) {
          copy.opcode = kDirectCall;
          copy.mnemonic = opcode_by_code(kDirectCall).name;
          copy.indirect_flow_targets.reset();
          new_item_by_origin.emplace(item_index, rewritten.size());
          rewritten.push_back(std::move(copy));
          MachineItem operand = MachineItem::address(target.entry_label);
          operand.source_line = items.at(item_index).source_line;
          rewritten.push_back(std::move(operand));
          continue;
        }
        if (call_operands.contains(item_index))
          copy.target = target.entry_label;
        new_item_by_origin.emplace(item_index, rewritten.size());
        rewritten.push_back(std::move(copy));
      }
      if (machine_cell_count(rewritten) != rejection.proof.input_cells - source_region.cells +
                                               static_cast<int>(indirect_calls.size()))
        continue;

      std::map<std::size_t, std::size_t> rewritten_origin_by_item;
      for (const auto& [origin, item_index] : new_item_by_origin)
        rewritten_origin_by_item.emplace(item_index, origin);
      const ArtifactIndex final_index = index_artifact(rewritten);
      bool numeric_indirect_rebound = true;
      for (const auto& [origin, rewritten_item] : new_item_by_origin) {
        if (origin >= items.size() || rewritten_item >= rewritten.size() ||
            indirect_calls.contains(origin))
          continue;
        const MachineItem& before = items.at(origin);
        MachineItem& after = rewritten.at(rewritten_item);
        if (!before.indirect_flow_targets.has_value())
          continue;
        if (!after.indirect_flow_targets.has_value() ||
            after.indirect_flow_targets->size() != before.indirect_flow_targets->size()) {
          numeric_indirect_rebound = false;
          break;
        }
        for (std::size_t target_index = 0; target_index < before.indirect_flow_targets->size();
             ++target_index) {
          const auto* numeric = std::get_if<int>(&before.indirect_flow_targets->at(target_index));
          if (numeric == nullptr)
            continue;
          const auto old_target = index.cells.find(*numeric);
          if (old_target == index.cells.end()) {
            numeric_indirect_rebound = false;
            break;
          }
          const auto new_target = new_item_by_origin.find(old_target->second);
          if (new_target == new_item_by_origin.end()) {
            numeric_indirect_rebound = false;
            break;
          }
          after.indirect_flow_targets->at(target_index) =
              final_index.addresses.at(new_target->second);
        }
        if (!numeric_indirect_rebound)
          break;
      }
      if (!numeric_indirect_rebound)
        continue;
      PostLayoutControlFlowOptions final_options;
      final_options.address_space_model = options.address_space_model;
      final_options.maximum_execution_states = pre_options.maximum_execution_states;
      const auto pre_main =
          std::find_if(pre_flow.external_entries.begin(), pre_flow.external_entries.end(),
                       [](const PostLayoutExternalEntryState& entry) {
                         return entry.kind == ExternalEntryKind::Main;
                       });
      if (pre_main == pre_flow.external_entries.end())
        continue;
      const auto rebound_main = new_item_by_origin.find(pre_main->entry.item_index);
      if (rebound_main == new_item_by_origin.end())
        continue;
      final_options.main_entry = final_index.addresses.at(rebound_main->second);
      if (pre_flow.empty_return_target.has_value()) {
        const auto rebound = new_item_by_origin.find(pre_flow.empty_return_target->item_index);
        if (rebound == new_item_by_origin.end())
          continue;
        final_options.empty_return_target = final_index.addresses.at(rebound->second);
      }
      const AuthoritativePostLayoutControlFlow final_flow =
          build_post_layout_control_flow(rewritten, final_options);
      if (!final_flow.proved ||
          final_flow.maximum_observed_return_depth != pre_flow.maximum_observed_return_depth)
        continue;

      const auto final_external = external_identities(final_flow, rewritten_origin_by_item);
      const auto final_indirect_flow =
          indirect_flow_identities(final_flow, rewritten_origin_by_item);
      const auto final_indirect_memory =
          indirect_memory_identities(final_flow, rewritten_origin_by_item);
      const auto final_empty_return = empty_return_identity(final_flow, rewritten_origin_by_item);
      std::map<std::size_t, std::vector<std::size_t>> expected_indirect_flow =
          *original_indirect_flow;
      for (const std::size_t call_item : indirect_calls)
        expected_indirect_flow.erase(call_item);
      if (!final_external || !final_indirect_flow || !final_indirect_memory ||
          !final_empty_return || *final_external != *original_external ||
          *final_indirect_flow != expected_indirect_flow ||
          *final_indirect_memory != *original_indirect_memory ||
          *final_empty_return != *original_empty_return)
        continue;

      AliasCandidate candidate;
      candidate.source_address = source_region.first_address;
      candidate.result.items = std::move(rewritten);
      candidate.result.preloads = options.effective_preloads;
      candidate.result.applied = 1;
      HelperSemanticAliasProof& proof = candidate.result.proof;
      proof.rewrite_proved = true;
      proof.preload_rebind_deferred = !options.effective_preloads.empty();
      proof.proved = !proof.preload_rebind_deferred;
      proof.semantic_equivalent = true;
      proof.machine_abi_equivalent = true;
      proof.source_entries_complete = true;
      proof.pre_control_flow_proved = true;
      proof.final_control_flow_proved = true;
      proof.external_entries_equivalent = true;
      proof.indirect_memory_equivalent = true;
      proof.symbolic_relocation_proved = fully_symbolic;
      proof.fixed_targets_equivalent = true;
      proof.final_artifact_proved = !proof.preload_rebind_deferred;
      proof.removed_entry_label = source.entry_label;
      proof.replacement_entry_label = target.entry_label;
      proof.domain = source.admitted_input;
      proof.input_cells = rejection.proof.input_cells;
      proof.output_cells = machine_cell_count(candidate.result.items);
      proof.removed_body_cells = source_region.cells;
      proof.added_call_cells = static_cast<int>(indirect_calls.size());
      proof.source_call_origins = static_cast<int>(admitted_origins.size());
      proof.final_control_flow = final_flow;
      for (const AliasCall& call : incoming.calls) {
        proof.calls.push_back(HelperSemanticAliasCallProof{
            .original_call_item = call.call_item,
            .original_continuation_item = call.continuation_item,
            .was_indirect = call.indirect,
            .return_stack_equivalent = true,
            .hidden_x2_equivalent = true,
            .x1_continuation_safe = true,
        });
      }
      if (proof.preload_rebind_deferred)
        reason(proof, "effective preloads require downstream identity-based rebinding");
      if (!best.has_value() ||
          std::tie(candidate.result.proof.output_cells, candidate.source_address) <
              std::tie(best->result.proof.output_cells, best->source_address))
        best = std::move(candidate);
    }
  }
  if (best.has_value())
    return std::move(best->result);
  reason(rejection.proof,
         "candidate gates complete/source-region/semantic/target/incoming/calls/origins/X1/"
         "boundary=" +
             std::to_string(complete_sources) + "/" + std::to_string(source_regions) + "/" +
             std::to_string(semantic_pairs) + "/" + std::to_string(target_regions) + "/" +
             std::to_string(complete_incoming_sets) + "/" + std::to_string(exact_call_sets) + "/" +
             std::to_string(exact_origin_sets) + "/" + std::to_string(x1_safe_pairs) + "/" +
             std::to_string(removable_boundaries));
  reason(rejection.proof, "no exact typed helper semantic pair");
  return rejection;
}

} // namespace mkpro::core
