#include "mkpro/core/passes/finite_mode_representation.hpp"

#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/register_allocator.hpp"
#include "mkpro/core/stack_value_equivalence.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {
namespace {

constexpr std::size_t kMaximumModeValues = 8;
constexpr std::size_t kMaximumProofStates = 100000;

bool ordinary(const IrOp& op) {
  return !has_rewrite_barrier(op) && op.meta.roles.empty() &&
         op.target_meta.roles.empty() && !op.meta.tactic.has_value() &&
         !op.procedure_boundary.has_value() && !op.meta.logical_register_analysis;
}

bool digit(const IrOp& op) {
  return ordinary(op) && op.kind == IrKind::Plain &&
         op.opcode >= 0 && op.opcode <= 9;
}

struct Literal {
  std::size_t begin = 0;
  std::size_t end = 0; // exclusive
  std::string value;
};

std::optional<Literal> literal_at(const std::vector<IrOp>& ops, std::size_t begin) {
  if (begin >= ops.size() || !digit(ops[begin]))
    return std::nullopt;
  Literal result{begin, begin, {}};
  bool point = false;
  std::size_t digits = 0;
  while (result.end < ops.size()) {
    const auto& op = ops[result.end];
    if (digit(op)) {
      result.value += static_cast<char>('0' + op.opcode);
      if (++digits > 8)
        return std::nullopt;
    } else if (ordinary(op) && op.kind == IrKind::Plain && op.opcode == 0x0a && !point) {
      point = true;
      result.value += '.';
    } else {
      break;
    }
    ++result.end;
  }
  if (result.end < ops.size() && ordinary(ops[result.end]) &&
      ops[result.end].kind == IrKind::Plain && ops[result.end].opcode == 0x0b) {
    result.value.insert(result.value.begin(), '-');
    ++result.end;
  }
  return result;
}

// Always start with a digit. A bare decimal point after a closed instruction
// can restore X2 on the real calculator; it is not a fresh entry of zero.
std::optional<std::vector<int>> encode_decimal(std::string value) {
  bool negative = false;
  if (!value.empty() && value.front() == '-') {
    negative = true;
    value.erase(value.begin());
  }
  if (value.empty())
    return std::nullopt;
  if (value.front() == '.')
    value.insert(value.begin(), '0');
  std::vector<int> result;
  bool point = false;
  int entered_digits = 0;
  for (const char c : value) {
    if (c == '.' && !point && !result.empty()) {
      result.push_back(0x0a);
      point = true;
    } else if (c >= '0' && c <= '9') {
      // The keyboard limit counts the leading zero too. 0.77777777 does
      // not enter the same mantissa as the arithmetic result 7.7777777e-1.
      if (++entered_digits > 8)
        return std::nullopt;
      result.push_back(c - '0');
    } else {
      return std::nullopt;
    }
    if (result.size() > 16U)
      return std::nullopt;
  }
  if (negative)
    result.push_back(0x0b);
  return result;
}

struct ExactDecimal {
  std::int64_t numerator = 0;
  std::int64_t denominator = 1;
  int high = 0;
  int low = 0;
};

std::optional<ExactDecimal> exact_decimal(const std::string& text) {
  ExactDecimal result;
  bool negative = false;
  bool point = false;
  int digits = 0;
  int whole_digits = 0;
  int first = -1;
  int last = -1;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (i == 0 && c == '-') {
      negative = true;
    } else if (c == '.' && !point) {
      point = true;
    } else if (c >= '0' && c <= '9') {
      if (++digits > 8)
        return std::nullopt;
      if (!point)
        ++whole_digits;
      else
        result.denominator *= 10;
      result.numerator = result.numerator * 10 + c - '0';
      if (c != '0') {
        if (first < 0)
          first = digits - 1;
        last = digits - 1;
      }
    } else {
      return std::nullopt;
    }
  }
  // A typed -0 and a negative arithmetic zero do not have interchangeable
  // machine encodings. Neither participates in this ordinary-decimal codec.
  if (digits == 0 || (negative && result.numerator == 0))
    return std::nullopt;
  if (first >= 0) {
    result.high = whole_digits - 1 - first;
    result.low = whole_digits - 1 - last;
  }
  if (negative)
    result.numerator = -result.numerator;
  return result;
}

std::optional<std::string> exact_projection(int opcode, const std::string& value,
                                          const std::optional<std::string>& operand) {
  const auto left = exact_decimal(value);
  if (!left)
    return std::nullopt;
  std::int64_t numerator = left->numerator;
  std::int64_t denominator = left->denominator;
  if (operand) {
    const auto right = exact_decimal(*operand);
    if (!right)
      return std::nullopt;
    if (opcode == 0x10 || opcode == 0x11) {
      // MK-61 alignment can discard a low digit even when the mathematical
      // answer fits eight significant digits. Prove both aligned operands
      // fit before allowing cancellation, not just the final result.
      if (numerator != 0 && right->numerator != 0 &&
          std::max(left->high, right->high) - std::min(left->low, right->low) >= 8)
        return std::nullopt;
      numerator = numerator * right->denominator +
                  (opcode == 0x10 ? 1 : -1) * right->numerator * denominator;
      denominator *= right->denominator;
    } else if (opcode == 0x12) {
      numerator *= right->numerator;
      denominator *= right->denominator;
    } else if (opcode == 0x13 && right->numerator != 0) {
      numerator *= right->denominator;
      denominator *= right->numerator;
    } else {
      return std::nullopt;
    }
  } else if (opcode == 0x0b) {
    numerator = -numerator;
  } else if (opcode == 0x32) {
    numerator = (numerator > 0) - (numerator < 0);
    denominator = 1;
  } else if (opcode == 0x34) {
    numerator /= denominator;
    denominator = 1;
  } else if (opcode == 0x35) {
    numerator %= denominator;
  } else {
    return std::nullopt;
  }
  // Arithmetic zero may retain a sign which fresh literal entry cannot
  // reproduce. A separate signed-zero representation proof would be needed.
  if (numerator == 0)
    return std::nullopt;
  if (denominator < 0) {
    numerator = -numerator;
    denominator = -denominator;
  }
  const auto divisor = std::gcd(numerator, denominator);
  numerator /= divisor;
  denominator /= divisor;
  const bool negative = numerator < 0;
  if (negative)
    numerator = -numerator;
  std::string result = (negative ? "-" : "") + std::to_string(numerator / denominator);
  std::int64_t remainder = numerator % denominator;
  if (remainder != 0)
    result += '.';
  // All parsed operands have at most eight digits. Products, common
  // denominators and remainder*10 are bounded by 2e16, safely within int64.
  for (int places = 0; remainder != 0 && places < 7; ++places) {
    remainder *= 10;
    result += static_cast<char>('0' + remainder / denominator);
    remainder %= denominator;
  }
  if (remainder != 0 || !encode_decimal(result))
    return std::nullopt;
  return result;
}

struct Projection {
  int opcode = 0;
  std::optional<std::string> operand;
  std::size_t end = 0;

  bool same_function(const Projection& other) const {
    return opcode == other.opcode && operand == other.operand;
  }

  std::optional<std::string> apply(const std::string& value) const {
    return exact_projection(opcode, value, operand);
  }
};

std::optional<Projection> projection_after(const std::vector<IrOp>& ops,
                                          std::size_t recall) {
  const auto next = recall + 1U;
  if (next >= ops.size() || !ordinary(ops[next]))
    return std::nullopt;
  if (ops[next].kind == IrKind::Plain &&
      (ops[next].opcode == 0x0b || ops[next].opcode == 0x32 ||
       ops[next].opcode == 0x34 || ops[next].opcode == 0x35)) {
    return Projection{ops[next].opcode, std::nullopt, next + 1U};
  }
  const auto literal = literal_at(ops, next);
  if (!literal || literal->end >= ops.size())
    return std::nullopt;
  const auto& operation = ops[literal->end];
  if (!ordinary(operation) || operation.kind != IrKind::Plain ||
      operation.opcode < 0x10 || operation.opcode > 0x13)
    return std::nullopt;
  return Projection{operation.opcode, literal->value, literal->end + 1U};
}

struct Macro {
  std::size_t end = 0; // exclusive
  bool definition = false;
  bool identical_value = false;
  bool binary_projection = false;
  std::vector<int> replacement_literal;
};

struct Plan {
  int reg = 0;
  int saved_cells = 0;
  std::size_t value_count = 0;
  std::map<std::size_t, Macro> macros;
};

// A physical register is not a single source value. Build reaching definitions
// on the return-context graph before selecting a representation: a later
// ordinary assignment starts a different lifetime and need not use this codec.
std::optional<Plan> discover(const std::vector<IrOp>& ops, int reg,
                             const AuthoritativePostLayoutControlFlow& flow,
                             const std::vector<std::size_t>& machine) {
  std::map<std::size_t, std::size_t> ir_at;
  for (std::size_t i = 0; i < ops.size(); ++i)
    ir_at.emplace(machine[i], i);
  std::map<std::size_t, Projection> projections;
  std::vector<Projection> functions;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    if (ops[i].kind != IrKind::Recall || ops[i].opcode != 0x60 + reg ||
        !ordinary(ops[i]))
      continue;
    if (const auto projection = projection_after(ops, i)) {
      projections.emplace(i, *projection);
      if (std::none_of(functions.begin(), functions.end(), [&](const Projection& other) {
            return other.same_function(*projection);
          }))
        functions.push_back(*projection);
    }
  }
  if (functions.empty())
    return std::nullopt;

  const std::size_t unknown = ops.size();
  std::set<std::size_t> external;
  for (const auto& entry : flow.external_entries)
    external.insert(entry.entry.item_index);
  std::vector<std::set<std::size_t>> reaching(flow.execution_states.size());
  std::vector<bool> queued(flow.execution_states.size(), false);
  std::deque<std::size_t> work;
  for (std::size_t i = 0; i < flow.execution_states.size(); ++i) {
    if (external.contains(flow.execution_states[i].item_index)) {
      reaching[i].insert(unknown);
      queued[i] = true;
      work.push_back(i);
    }
  }
  std::size_t processed = 0;
  while (!work.empty()) {
    if (++processed > kMaximumProofStates)
      return std::nullopt;
    const auto state = work.front();
    work.pop_front();
    queued[state] = false;
    auto definitions = reaching[state];
    const auto ir = ir_at.find(flow.execution_states[state].item_index);
    if (ir != ir_at.end() && ops[ir->second].kind == IrKind::Store &&
        ops[ir->second].opcode == 0x40 + reg)
      definitions = {ir->second};
    for (const auto next : flow.execution_successors[state]) {
      const auto before = reaching[next].size();
      reaching[next].insert(definitions.begin(), definitions.end());
      if (before != reaching[next].size() && !queued[next]) {
        queued[next] = true;
        work.push_back(next);
      }
    }
  }

  std::map<std::size_t, std::set<std::size_t>> read_definitions;
  std::map<std::size_t, std::set<std::size_t>> definition_reads;
  for (std::size_t state = 0; state < flow.execution_states.size(); ++state) {
    const auto ir = ir_at.find(flow.execution_states[state].item_index);
    if (ir == ir_at.end() || reaching[state].empty())
      continue;
    const auto read = ir->second;
    if (ops[read].kind != IrKind::Recall || ops[read].opcode != 0x60 + reg)
      continue;
    read_definitions[read].insert(reaching[state].begin(), reaching[state].end());
    for (const auto definition : reaching[state])
      definition_reads[definition].insert(read);
  }

  std::optional<Plan> best;
  for (const auto& function : functions) {
    std::map<std::size_t, Literal> literals;
    std::map<std::size_t, std::vector<int>> replacements;
    std::map<std::size_t, std::string> encoded_values;
    for (const auto& [store, reads] : definition_reads) {
      if (store == unknown || !ordinary(ops[store]) || reads.empty())
        continue;
      if (std::any_of(reads.begin(), reads.end(), [&](const auto read) {
            const auto projection = projections.find(read);
            return projection == projections.end() ||
                   !function.same_function(projection->second);
          }))
        continue;

      std::size_t begin = store;
      if (begin > 0 && ordinary(ops[begin - 1]) &&
          ops[begin - 1].kind == IrKind::Plain && ops[begin - 1].opcode == 0x0b)
        --begin;
      while (begin > 0 && ordinary(ops[begin - 1]) &&
             ops[begin - 1].kind == IrKind::Plain &&
             ops[begin - 1].opcode >= 0 && ops[begin - 1].opcode <= 0x0a)
        --begin;
      const auto literal = literal_at(ops, begin);
      if (!literal || literal->end != store)
        continue;
      const auto encoded = function.apply(literal->value);
      const auto replacement = encoded ? encode_decimal(*encoded) : std::nullopt;
      if (!replacement)
        continue;
      literals.emplace(store, *literal);
      replacements.emplace(store, *replacement);
      encoded_values.emplace(store, *encoded);
    }

    // A merged read must receive the same representation from every reaching
    // definition. Repeated pruning closes the entire def-use web, including
    // cycles; an entry-live, dynamic or differently consumed definition poisons
    // only its connected web, not all other uses of the physical register.
    bool changed = true;
    while (changed) {
      changed = false;
      for (auto it = literals.begin(); it != literals.end();) {
        bool complete = true;
        for (const auto read : definition_reads.at(it->first)) {
          for (const auto definition : read_definitions.at(read))
            complete = complete && literals.contains(definition);
        }
        if (!complete) {
          it = literals.erase(it);
          changed = true;
        } else {
          ++it;
        }
      }
    }
    if (literals.empty())
      continue;

    Plan plan;
    plan.reg = reg;
    std::set<std::string> values;
    std::set<std::size_t> reads;
    for (const auto& [store, literal] : literals) {
      values.insert(literal.value);
      const auto& replacement = replacements.at(store);
      plan.macros.emplace(literal.begin, Macro{store + 1U, true,
          literal.value == encoded_values.at(store), false, replacement});
      plan.saved_cells += static_cast<int>(store - literal.begin) -
                          static_cast<int>(replacement.size());
      reads.insert(definition_reads.at(store).begin(), definition_reads.at(store).end());
    }
    if (values.size() > kMaximumModeValues)
      continue;
    for (const auto read : reads) {
      const auto& projection = projections.at(read);
      plan.macros.emplace(read, Macro{projection.end, false, false,
                                      projection.operand.has_value(), {}});
      plan.saved_cells += static_cast<int>(projection.end - read - 1U);
    }
    std::size_t last_end = 0;
    bool disjoint = true;
    for (const auto& [begin, macro] : plan.macros) {
      disjoint = disjoint && begin >= last_end;
      last_end = macro.end;
    }
    plan.value_count = values.size();
    if (disjoint && plan.saved_cells > 0 &&
        (!best || plan.saved_cells > best->saved_cells))
      best = std::move(plan);
  }
  return best;
}

bool symbolic(const std::vector<IrOp>& ops) {
  for (const auto& op : ops) {
    if (op.meta.raw || op.meta.logical_register_analysis ||
        op.kind == IrKind::OrphanAddress || op.target_meta.formal_opcode ||
        op.meta.indirect_flow_formal_targets)
      return false;
    if ((op.kind == IrKind::Call || op.kind == IrKind::Jump ||
         op.kind == IrKind::CondJump || op.kind == IrKind::Loop) &&
        !std::holds_alternative<std::string>(op.target))
      return false;
    if (op.meta.indirect_flow_targets)
      for (const auto& target : *op.meta.indirect_flow_targets)
        if (!std::holds_alternative<std::string>(target))
          return false;
    for (const auto& role : op.meta.roles)
      if (role.starts_with("late-decimal-selector-") ||
          role.starts_with("finalization-cell-origin:") ||
          role == kResumableErrorPaddingRole)
        return false;
  }
  return true;
}

struct Equality {
  StackValueEqualityState stack{{true, true, true, true}, true, true};
  bool initialized = false;
  bool entry = false;

  int key() const {
    return stack_value_equality_key(stack) | (initialized ? 64 : 0) | (entry ? 128 : 0);
  }
};

bool prove(const std::vector<MachineItem>& items,
           const AuthoritativePostLayoutControlFlow& flow,
           const std::vector<std::size_t>& machine, const Plan& plan,
           std::string& reason) {
  const auto fail = [&](const char* why) { reason = why; return false; };
  std::map<std::size_t, const Macro*> starts;
  std::set<std::size_t> interiors;
  std::map<std::size_t, std::size_t> final_item;
  std::map<std::size_t, std::size_t> required_predecessor;
  for (const auto& [begin, macro] : plan.macros) {
    starts.emplace(machine[begin], &macro);
    final_item.emplace(machine[begin], machine[macro.end - 1U]);
    for (std::size_t i = begin + 1U; i < macro.end; ++i) {
      interiors.insert(machine[i]);
      required_predecessor.emplace(machine[i], machine[i - 1U]);
    }
  }
  std::set<std::size_t> external_items;
  for (const auto& entry : flow.external_entries) {
    external_items.insert(entry.entry.item_index);
    if (interiors.contains(entry.entry.item_index) || starts.contains(entry.entry.item_index))
      return fail("independent entry into a representation transaction");
  }
  for (std::size_t source = 0; source < flow.execution_successors.size(); ++source) {
    const auto before = flow.execution_states[source].item_index;
    for (const auto successor : flow.execution_successors[source]) {
      const auto next = flow.execution_states[successor].item_index;
      if (const auto required = required_predecessor.find(next);
          required != required_predecessor.end() && required->second != before)
        return fail("side entry into a literal or projection");
      if (const auto start = starts.find(next);
          start != starts.end() && start->second->definition &&
          !selector_charge_entry_closer_opcode(items[before].opcode))
        return fail("definition does not start a proved fresh numeric entry");
    }
  }
  for (std::size_t i = 0; i < items.size(); ++i) {
    const auto& item = items[i];
    if (item.kind != MachineItemKind::Op)
      continue;
    const int family = item.opcode & 0xf0;
    if (family >= 0x70 && family <= 0xe0 && (item.opcode & 15) == plan.reg)
      return fail("mode is also an indirect selector");
    if (family == 0xb0 || family == 0xd0) {
      const auto targets = flow.indirect_memory_targets.find(i);
      if (targets == flow.indirect_memory_targets.end() ||
          std::find(targets->second.begin(), targets->second.end(), plan.reg) != targets->second.end())
        return fail("mode has an indirect memory alias");
    }
    constexpr std::array<int, 4> loops{0x5d, 0x5b, 0x58, 0x5a};
    if (plan.reg < 4 && item.opcode == loops[static_cast<std::size_t>(plan.reg)])
      return fail("mode is a destructive loop counter");
    if (item.opcode == 0x4f || item.opcode == 0x6f)
      return fail("extended register alias is outside this proof");
  }

  std::vector<std::set<int>> seen(flow.execution_states.size());
  std::deque<std::pair<std::size_t, Equality>> work;
  const auto enqueue = [&](std::size_t index, const Equality& state) {
    if (seen[index].insert(state.key()).second)
      work.emplace_back(index, state);
  };
  for (std::size_t i = 0; i < flow.execution_states.size(); ++i)
    if (external_items.contains(flow.execution_states[i].item_index))
      enqueue(i, Equality{});
  if (work.empty())
    return fail("no authoritative external entry");
  std::set<std::size_t> reached_macros;
  std::size_t processed = 0;
  while (!work.empty()) {
    if (++processed > kMaximumProofStates)
      return fail("finite product proof budget exhausted");
    auto [index, state] = work.front();
    work.pop_front();
    const auto item_index = flow.execution_states[index].item_index;
    const auto& item = items[item_index];
    if (const auto found = starts.find(item_index); found != starts.end()) {
      const auto& macro = *found->second;
      reached_macros.insert(item_index);
      const auto old = state.stack.stack_equal;
      if (macro.definition) {
        state.stack.stack_equal = {macro.identical_value, old[0], old[1], old[2]};
        state.stack.x2_equal = macro.identical_value;
        state.initialized = true;
      } else {
        if (!state.initialized)
          return fail("projection has an entry-live or unproved mode definition");
        state.stack.stack_equal = {true, old[0], old[1],
                                   macro.binary_projection ? false : old[2]};
        // Deleted arithmetic may have saved a different last-X or entry
        // shadow. Neither is assumed dead merely because X now agrees.
        state.stack.x1_equal = false;
        state.stack.x2_equal = false;
      }
      state.entry = false;
      const auto last = final_item.at(item_index);
      std::size_t remaining = items.size();
      while (flow.execution_states[index].item_index != last) {
        if (remaining-- == 0U || flow.execution_successors[index].size() != 1U)
          return fail("transaction has a non-linear internal continuation");
        index = flow.execution_successors[index].front();
      }
    } else {
      const int opcode = item.opcode;
      if (item.manual_interaction &&
          (!stack_values_fully_equal(state.stack) || state.initialized))
        return fail("manual input crosses a live encoded lifetime");
      if (opcode == 0x60 + plan.reg && state.initialized)
        return fail("unprojected read observes an encoded lifetime");
      if (opcode == 0x40 + plan.reg) {
        if (!state.stack.stack_equal[0])
          return fail("ordinary definition observes an encoded stack value");
        state.initialized = false;
      }
      if (opcode == 0x50) {
        if (state.initialized && item.stop_disposition != StopDisposition::Terminal)
          return fail("encoded lifetime crosses an independent resume entry");
        if (!stack_values_fully_equal(state.stack))
          return fail("encoded mode leaks into displayed or resumable stack state");
        state.entry = false;
      } else if (opcode <= 9) {
        if (transfer_decimal_digit_equality(state.stack, state.entry) ==
            StackValueEqualityTransfer::Rejected)
          return fail("numeric entry observes a different stack value");
        state.entry = true;
      } else if (opcode == 0x51 || opcode == 0x52 || opcode == 0x53 ||
                 opcode_by_code(opcode).takes_address ||
                 ((opcode & 0xf0) >= 0x70 && (opcode & 0xf0) <= 0xe0 &&
                  (opcode & 0xf0) != 0xb0 && (opcode & 0xf0) != 0xd0)) {
        const bool unconditional = opcode == 0x51 || opcode == 0x52 || opcode == 0x53 ||
                                   (opcode & 0xf0) == 0x80 || (opcode & 0xf0) == 0xa0;
        if (!unconditional && !state.stack.stack_equal[0])
          return fail("conditional flow observes a different value");
        // Preserve an unequal entry shadow conservatively on both edges.
        if (!unconditional && !stack_values_fully_equal(state.stack))
          state.stack.x2_equal = false;
        if (selector_charge_entry_closer_opcode(opcode))
          state.entry = false;
      } else {
        if (opcode == 0x3b)
          return fail("random-state effects are outside the representation proof");
        if (!stack_values_fully_equal(state.stack)) {
          auto kind = StackValueEqualityStepKind::Plain;
          if ((opcode >= 0x60 && opcode <= 0x6e) || (opcode >= 0xd0 && opcode <= 0xde))
            kind = StackValueEqualityStepKind::Recall;
          else if ((opcode >= 0x40 && opcode <= 0x4e) || (opcode >= 0xb0 && opcode <= 0xbe))
            kind = StackValueEqualityStepKind::Store;
          const auto result = opcode == 0x0b && state.entry
              ? transfer_decimal_sign_equality(state.stack, true)
              : transfer_stack_value_equality(state.stack, opcode, kind);
          if (result == StackValueEqualityTransfer::Rejected)
            return fail("continuation observes a changed stack/X1/X2 value");
        }
        if (selector_charge_entry_closer_opcode(opcode))
          state.entry = false;
      }
    }
    if (flow.execution_successors[index].empty() && !stack_values_fully_equal(state.stack))
      return fail("unproved exit with changed hidden state");
    for (const auto successor : flow.execution_successors[index])
      enqueue(successor, state);
  }
  if (reached_macros.size() != starts.size())
    return fail("unreachable definitions or consumers lack a representation proof");
  return true;
}

} // namespace

PassResult finite_mode_representation(const std::vector<IrOp>& ops,
                                      const PassContext& context) {
  const auto unchanged = [&] { return PassResult{.ops = ops}; };
  // This is part of the separately regenerated shared-region candidate. The
  // ordinary lowering remains an incumbent: a local win can lose address
  // coincidences, so final layout, not this pass, chooses the program.
  if (!context.options.defer_return_suffix_until_callee_hole || !symbolic(ops))
    return unchanged();
  // Avoid constructing the context graph for programs with no projected reads.
  bool has_projection = false;
  for (std::size_t i = 0; i < ops.size() && !has_projection; ++i)
    has_projection = ops[i].kind == IrKind::Recall && projection_after(ops, i).has_value();
  if (!has_projection)
    return unchanged();
  const auto items = lower_ir_to_machine(ops);
  PostLayoutControlFlowOptions options;
  options.address_space_model = address_space_model_for_feature_profile(
      effective_optimizer_feature_profile(context.options));
  const auto flow = build_post_layout_control_flow(items, options);
  if (!flow.proved || flow.execution_states.size() != flow.execution_successors.size())
    return unchanged();
  std::vector<std::size_t> machine;
  std::size_t item = 0;
  for (const auto& op : ops) {
    machine.push_back(item);
    item += op.kind == IrKind::Label ? 1U : static_cast<std::size_t>(cells_per_op(op));
  }
  std::vector<Plan> plans;
  for (int reg = 0; reg < 15; ++reg)
    if (auto plan = discover(ops, reg, flow, machine))
      plans.push_back(std::move(*plan));
  std::stable_sort(plans.begin(), plans.end(), [](const Plan& a, const Plan& b) {
    return a.saved_cells > b.saved_cells;
  });
  for (const auto& plan : plans) {
    std::string reason;
    const bool accepted = prove(items, flow, machine, plan, reason);
    if (std::getenv("MKPRO_NATIVE_TRACE_FINITE_MODE_REPRESENTATION") != nullptr)
      std::cerr << "[finite-mode-representation] R" << register_name_for_index(plan.reg)
                << " values=" << plan.value_count << " local-saving=" << plan.saved_cells
                << " proof=" << (accepted ? "accepted" : reason) << '\n';
    if (!accepted)
      continue;
    std::vector<IrOp> rewritten;
    for (std::size_t i = 0; i < ops.size();) {
      const auto found = plan.macros.find(i);
      if (found == plan.macros.end()) {
        rewritten.push_back(ops[i++]);
        continue;
      }
      const auto& macro = found->second;
      if (macro.definition) {
        for (const int opcode : macro.replacement_literal) {
          auto literal = ops[i];
          literal.opcode = opcode;
          literal.meta.mnemonic = opcode_by_code(opcode).name;
          literal.meta.comment = "finite mode projected representation";
          literal.semantic.clear();
          rewritten.push_back(std::move(literal));
        }
        rewritten.push_back(ops[macro.end - 1U]);
      } else {
        rewritten.push_back(ops[i]);
      }
      i = macro.end;
    }
    if (!build_post_layout_control_flow(lower_ir_to_machine(rewritten), options).proved)
      continue;
    return PassResult{
        .ops = std::move(rewritten), .applied = 1,
        .optimizations = {{
            .name = "finite-mode-representation",
            .detail = "Stored the common consumer projection of " +
                      std::to_string(plan.value_count) + " finite mode values in one reaching-definition web of R" +
                      register_name_for_index(plan.reg) + "; jointly proved all definitions, "
                      "consumers, call/return contexts and stack/X1/X2 continuations; removed " +
                      std::to_string(plan.saved_cells) + " local cells. The complete "
                      "shared-region candidate still competes on final layout size.",
        }},
    };
  }
  return unchanged();
}

IrPass finite_mode_representation_pass() {
  return IrPass{.name = "finite-mode-representation",
                .run = finite_mode_representation, .layout_safe = false};
}

} // namespace mkpro::core::passes
