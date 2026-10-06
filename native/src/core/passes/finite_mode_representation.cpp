#include "mkpro/core/passes/finite_mode_representation.hpp"

#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/constant_synthesis.hpp"
#include "mkpro/core/indirect_addressing.hpp"
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
  bool input_literal = false;
  bool input_store = false;
  std::vector<int> replacement_projection;
  bool replacement_binary = false;
};

struct Plan {
  int reg = 0;
  int saved_cells = 0;
  std::size_t value_count = 0;
  std::map<std::size_t, Macro> macros;
  bool joint = false;
  bool synthesized = false;
  std::string encoding_summary;
  std::set<std::size_t> flow_reads;
  std::map<std::string, int> fixed_targets;
};

// A physical register is not a single source value. Build reaching definitions

bool flow_read(const IrOp& op, int reg) {
  const int family = op.opcode & 0xf0;
  return (op.kind == IrKind::IndirectCall || op.kind == IrKind::IndirectJump ||
          op.kind == IrKind::IndirectCondJump) &&
         (op.opcode & 15) == reg &&
         (family == 0x70 || family == 0x80 || family == 0x90 ||
          family == 0xa0 || family == 0xc0 || family == 0xe0);
}

struct ModeUses {
  std::map<std::size_t, std::set<std::size_t>> read_definitions;
  std::map<std::size_t, std::set<std::size_t>> definition_reads;
};

std::optional<ModeUses> analyze_mode_uses(
    const std::vector<IrOp>& ops, int reg,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<std::size_t>& machine, bool include_flow) {
  std::map<std::size_t, std::size_t> ir_at;
  for (std::size_t i = 0; i < ops.size(); ++i)
    ir_at.emplace(machine[i], i);
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
    if (!((ops[read].kind == IrKind::Recall && ops[read].opcode == 0x60 + reg) ||
          (include_flow && flow_read(ops[read], reg))))
      continue;
    read_definitions[read].insert(reaching[state].begin(), reaching[state].end());
    for (const auto definition : reaching[state])
      definition_reads[definition].insert(read);
  }

  return ModeUses{std::move(read_definitions), std::move(definition_reads)};
}

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
  const auto uses = analyze_mode_uses(ops, reg, flow, machine, false);
  if (!uses)
    return std::nullopt;
  const auto& read_definitions = uses->read_definitions;
  const auto& definition_reads = uses->definition_reads;

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


// Recover the literal producers of X at each store on the exact return-context
// graph. A callee entry store is not required to be adjacent to its producers.
// Only instructions that preserve X are crossed; all other sources are unknown.
// This is provenance, not permission to change other observations of that X.
std::map<std::size_t, std::vector<Literal>> literal_inputs(
    const std::vector<IrOp>& ops, const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<std::size_t>& machine, const ModeUses& uses) {
  std::map<std::size_t, std::size_t> ir_at;
  std::map<std::size_t, Literal> ending;
  for (std::size_t i = 0; i < ops.size(); ++i)
    ir_at.emplace(machine[i], i);
  for (std::size_t i = 0; i < ops.size();) {
    if (const auto literal = literal_at(ops, i)) {
      ending.emplace(literal->end - 1U, *literal);
      i = literal->end;
    } else {
      ++i;
    }
  }
  std::vector<std::vector<std::size_t>> predecessors(flow.execution_states.size());
  for (std::size_t i = 0; i < flow.execution_successors.size(); ++i)
    for (const auto next : flow.execution_successors[i])
      predecessors[next].push_back(i);
  std::set<std::size_t> external;
  for (const auto& entry : flow.external_entries)
    external.insert(entry.entry.item_index);
  std::map<std::size_t, std::vector<Literal>> result;
  std::size_t processed = 0;
  for (const auto& [store, unused_reads] : uses.definition_reads) {
    (void)unused_reads;
    if (store >= ops.size() || !ordinary(ops[store]))
      continue;
    std::deque<std::size_t> pending;
    bool complete = true;
    for (std::size_t i = 0; i < flow.execution_states.size(); ++i) {
      if (flow.execution_states[i].item_index != machine[store])
        continue;
      if (external.contains(machine[store]) || predecessors[i].empty())
        complete = false;
      pending.insert(pending.end(), predecessors[i].begin(), predecessors[i].end());
    }
    std::set<std::size_t> seen;
    std::map<std::size_t, Literal> sources;
    while (complete && !pending.empty()) {
      const auto state = pending.front();
      pending.pop_front();
      if (!seen.insert(state).second)
        continue;
      if (++processed > kMaximumProofStates)
        return {};
      const auto at = flow.execution_states[state].item_index;
      const auto found = ir_at.find(at);
      if (found == ir_at.end() || external.contains(at)) {
        complete = false;
        break;
      }
      const auto index = found->second;
      if (const auto literal = ending.find(index); literal != ending.end()) {
        sources.emplace(literal->second.begin, literal->second);
        continue;
      }
      const auto& op = ops[index];
      const bool transparent = op.kind == IrKind::Store ||
          op.kind == IrKind::Call || op.kind == IrKind::Jump ||
          op.kind == IrKind::Return || (op.kind == IrKind::Plain && op.opcode == 0x54);
      if (!transparent || op.meta.raw || op.meta.manual_interaction ||
          op.target_meta.formal_opcode || predecessors[state].empty()) {
        complete = false;
        break;
      }
      pending.insert(pending.end(), predecessors[state].begin(), predecessors[state].end());
    }
    if (complete && !sources.empty()) {
      auto& literals = result[store];
      for (const auto& [unused_begin, literal] : sources) {
        (void)unused_begin;
        literals.push_back(literal);
      }
    }
  }
  return result;
}

std::optional<std::vector<int>> projection_code(const Projection& projection) {
  if (projection.opcode < 0)
    return std::vector<int>{};
  std::vector<int> code;
  if (projection.operand) {
    const auto literal = encode_decimal(*projection.operand);
    if (!literal)
      return std::nullopt;
    code = *literal;
  }
  code.push_back(projection.opcode);
  return code;
}

struct Decoder {
  std::vector<int> code;
  bool binary = false;
};

// Synthesize a small decoder by its entire finite truth table, not by the
// spelling of a source expression. Constants are derived from the table.
// Every row is independently checked with the ordinary-decimal certificate.
std::optional<Decoder> choose_decoder(
    const std::vector<std::string>& encoded, const std::vector<std::string>& wanted,
    const std::vector<Projection>& observed) {
  if (encoded.empty() || encoded.size() != wanted.size())
    return std::nullopt;
  std::vector<Projection> candidates{{-1, std::nullopt, 0}};
  for (const int opcode : {0x0b, 0x32, 0x34, 0x35})
    candidates.push_back({opcode, std::nullopt, 0});
  candidates.insert(candidates.end(), observed.begin(), observed.end());
  if (const auto offset = exact_projection(0x11, wanted.front(), encoded.front()))
    candidates.push_back({0x10, *offset, 0});
  if (const auto offset = exact_projection(0x11, encoded.front(), wanted.front()))
    candidates.push_back({0x11, *offset, 0});
  if (const auto factor = exact_projection(0x13, wanted.front(), encoded.front()))
    candidates.push_back({0x12, *factor, 0});
  if (const auto divisor = exact_projection(0x13, encoded.front(), wanted.front()))
    candidates.push_back({0x13, *divisor, 0});
  std::optional<Decoder> best;
  for (const auto& candidate : candidates) {
    const auto code = projection_code(candidate);
    if (!code || (best && (code->size() > best->code.size() ||
                          (code->size() == best->code.size() && *code >= best->code))))
      continue;
    bool exact = true;
    for (std::size_t i = 0; i < encoded.size(); ++i) {
      const auto actual = candidate.opcode < 0
          ? std::optional<std::string>{encoded[i]} : candidate.apply(encoded[i]);
      exact = exact && actual && *actual == wanted[i];
    }
    if (exact)
      best = Decoder{*code, candidate.operand.has_value()};
  }
  return best;
}

std::map<std::string, int> label_positions(const std::vector<IrOp>& ops) {
  std::map<std::string, int> positions;
  int address = 0;
  for (const auto& op : ops) {
    if (op.kind == IrKind::Label)
      positions.emplace(op.name, address);
    address += cells_per_op(op);
  }
  return positions;
}

std::vector<Plan> discover_joint(
    const std::vector<IrOp>& ops, int reg,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<std::size_t>& machine, AddressSpaceModel model) {
  std::vector<Plan> plans;
  const auto uses = analyze_mode_uses(ops, reg, flow, machine, true);
  if (!uses)
    return plans;
  const auto inputs = literal_inputs(ops, flow, machine, *uses);
  if (inputs.empty())
    return plans;
  const auto positions = label_positions(ops);
  std::set<std::size_t> visited;
  for (const auto& [seed, unused_inputs] : inputs) {
    (void)unused_inputs;
    if (visited.contains(seed))
      continue;
    std::set<std::size_t> stores, reads;
    std::deque<std::size_t> pending{seed};
    bool complete = true;
    while (!pending.empty()) {
      const auto store = pending.front();
      pending.pop_front();
      if (!stores.insert(store).second)
        continue;
      visited.insert(store);
      if (!inputs.contains(store))
        complete = false;
      const auto found = uses->definition_reads.find(store);
      if (found == uses->definition_reads.end()) {
        complete = false;
        continue;
      }
      for (const auto read : found->second) {
        if (!reads.insert(read).second)
          continue;
        const auto& definitions = uses->read_definitions.at(read);
        pending.insert(pending.end(), definitions.begin(), definitions.end());
      }
    }
    if (!complete)
      continue;
    std::map<std::size_t, Literal> literals;
    std::set<std::string> values;
    for (const auto store : stores)
      for (const auto& literal : inputs.at(store)) {
        literals.emplace(literal.begin, literal);
        values.insert(literal.value);
      }
    if (values.empty() || values.size() > kMaximumModeValues)
      continue;
    std::map<std::size_t, Projection> projections;
    std::vector<Projection> functions;
    std::set<std::size_t> flow_reads;
    for (const auto read : reads) {
      if (flow_read(ops[read], reg)) {
        if (reg < 7 || !ops[read].meta.indirect_flow_targets ||
            ops[read].meta.indirect_flow_targets->empty())
          complete = false;
        flow_reads.insert(read);
      } else if (const auto projection = projection_after(ops, read)) {
        projections.emplace(read, *projection);
        if (std::none_of(functions.begin(), functions.end(), [&](const Projection& other) {
              return other.same_function(*projection);
            }))
          functions.push_back(*projection);
      } else {
        // A bare read is an observation, not a license to change its meaning.
        complete = false;
      }
    }
    if (!complete || functions.empty())
      continue;
    // Preserve observed representations as incumbents, but do not restrict
    // the search to them. Derive new words jointly from all consumer tables.
    std::vector<std::vector<std::string>> encodings;
    for (const auto& encoder : functions) {
      std::vector<std::string> encoded;
      for (const auto& value : values) {
        const auto word = encoder.apply(value);
        if (!word || !encode_decimal(*word))
          break;
        encoded.push_back(*word);
      }
      if (encoded.size() == values.size() &&
          std::find(encodings.begin(), encodings.end(), encoded) == encodings.end())
        encodings.push_back(std::move(encoded));
    }
    const std::size_t observed_encodings = encodings.size();
    std::vector<std::vector<std::string>> consumer_values;
    for (const auto& function : functions) {
      std::vector<std::string> wanted;
      for (const auto& value : values) {
        const auto output = function.apply(value);
        if (!output)
          break;
        wanted.push_back(*output);
      }
      if (wanted.size() != values.size()) {
        complete = false;
        break;
      }
      consumer_values.push_back(std::move(wanted));
    }
    if (complete) {
      const auto synthesized = synthesize_finite_constant_encodings(consumer_values);
      for (const auto& encoded : synthesized.encodings)
        if (std::find(encodings.begin(), encodings.end(), encoded) == encodings.end())
          encodings.push_back(encoded);
    }
    for (std::size_t encoding_index = 0; encoding_index < encodings.size(); ++encoding_index) {
      const auto& encoded = encodings[encoding_index];
      Plan plan;
      plan.reg = reg;
      plan.joint = true;
      plan.synthesized = encoding_index >= observed_encodings;
      plan.value_count = values.size();
      std::map<std::string, std::string> codebook;
      bool valid = true;
      std::size_t row = 0;
      for (const auto& value : values) {
        const auto& word = encoded[row++];
        if (!encode_decimal(word)) {
          valid = false;
          break;
        }
        codebook.emplace(value, word);
        if (plan.synthesized) {
          if (!plan.encoding_summary.empty())
            plan.encoding_summary += ", ";
          plan.encoding_summary += value + " -> " + word;
        }
      }
      if (!valid)
        continue;
      for (const auto& [begin, literal] : literals) {
        const auto code = *encode_decimal(codebook.at(literal.value));
        Macro source;
        source.end = literal.end;
        source.identical_value = literal.value == codebook.at(literal.value);
        source.replacement_literal = code;
        source.input_literal = true;
        plan.macros.emplace(begin, std::move(source));
        plan.saved_cells += static_cast<int>(literal.end - begin) - static_cast<int>(code.size());
      }
      for (const auto store : stores) {
        Macro definition;
        definition.end = store + 1U;
        definition.input_store = true;
        plan.macros.emplace(store, std::move(definition));
      }
      for (const auto& [read, projection] : projections) {
        std::vector<std::string> wanted;
        for (const auto& value : values) {
          const auto output = projection.apply(value);
          if (!output) {
            valid = false;
            break;
          }
          wanted.push_back(*output);
        }
        if (!valid)
          break;
        const auto decoder = choose_decoder(encoded, wanted, functions);
        if (!decoder) {
          valid = false;
          break;
        }
        Macro consumer;
        consumer.end = projection.end;
        consumer.binary_projection = projection.operand.has_value();
        consumer.replacement_projection = decoder->code;
        consumer.replacement_binary = decoder->binary;
        plan.macros.emplace(read, std::move(consumer));
        plan.saved_cells += static_cast<int>(projection.end - read - 1U) -
                            static_cast<int>(decoder->code.size());
      }
      for (const auto read : flow_reads) {
        if (!valid)
          break;
        std::set<int> declared;
        for (const auto& target : *ops[read].meta.indirect_flow_targets) {
          const auto* label = std::get_if<std::string>(&target);
          if (!label || !positions.contains(*label)) {
            valid = false;
            break;
          }
          declared.insert(positions.at(*label));
          plan.fixed_targets.emplace(*label, positions.at(*label));
        }
        for (const auto& [original, replacement] : codebook) {
          const auto before = evaluate_indirect_address(register_name_for_index(reg), original,
                                                        IndirectOperationKind::Flow, model);
          const auto after = evaluate_indirect_address(register_name_for_index(reg), replacement,
                                                       IndirectOperationKind::Flow, model);
          if (!before || !after || !before->actual_flow_target || !after->actual_flow_target ||
              before->actual_flow_target != after->actual_flow_target ||
              before->flow_target != before->actual_flow_target ||
              after->flow_target != after->actual_flow_target ||
              !declared.contains(*before->actual_flow_target) ||
              !indirect_writeback_preserves_literal_value(*before, original) ||
              !indirect_writeback_preserves_literal_value(*after, replacement)) {
            valid = false;
            break;
          }
        }
        plan.flow_reads.insert(machine[read]);
      }
      std::size_t end = 0;
      for (const auto& [begin, macro] : plan.macros) {
        valid = valid && begin >= end;
        end = macro.end;
      }
      if (valid && plan.saved_cells > 0)
        plans.push_back(std::move(plan));
    }
  }
  return plans;
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
  bool encoded_input = false;

  int key() const {
    return stack_value_equality_key(stack) | (initialized ? 64 : 0) |
           (entry ? 128 : 0) | (encoded_input ? 256 : 0);
  }
};

bool prove(const std::vector<MachineItem>& items,
           const AuthoritativePostLayoutControlFlow& flow,
           const std::vector<std::size_t>& machine, const Plan& plan,
           const std::vector<bool>& automatic_entry_lift,
           std::string& reason) {
  const auto fail = [&](const char* why) { reason = why; return false; };
  if (automatic_entry_lift.size() != flow.execution_states.size())
    return fail("fresh numeric entry facts have the wrong context domain");
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
          start != starts.end() && (start->second->definition || start->second->input_literal) &&
          !automatic_entry_lift[successor])
        return fail("definition does not start a proved fresh numeric entry");
    }
  }
  for (std::size_t i = 0; i < items.size(); ++i) {
    const auto& item = items[i];
    if (item.kind != MachineItemKind::Op)
      continue;
    const int family = item.opcode & 0xf0;
    if (family >= 0x70 && family <= 0xe0 && (item.opcode & 15) == plan.reg &&
        !plan.joint)
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
      if (macro.input_store) {
        if (!state.encoded_input)
          return fail("callee store has no proved encoded literal input");
        state.initialized = true;
      } else if (macro.definition || macro.input_literal) {
        state.stack.stack_equal = {macro.identical_value, old[0], old[1], old[2]};
        state.stack.x2_equal = macro.identical_value;
        if (macro.definition)
          state.initialized = true;
        state.encoded_input = macro.input_literal;
      } else {
        if (!state.initialized)
          return fail("projection has an entry-live or unproved mode definition");
        state.stack.stack_equal = {true, old[0], old[1],
            macro.binary_projection == macro.replacement_binary
                ? old[macro.binary_projection ? 1U : 2U] : false};
        state.encoded_input = false;
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
      const int family = opcode & 0xf0;
      const bool selector = family >= 0x70 && family <= 0xe0 &&
                            family != 0xb0 && family != 0xd0;
      if (selector && (opcode & 15) == plan.reg && state.initialized &&
          !plan.flow_reads.contains(item_index))
        return fail("indirect consumer has no joint encoding/address certificate");
      const bool transports_input = (opcode >= 0x40 && opcode <= 0x4e) ||
          opcode == 0x51 || opcode == 0x52 || opcode == 0x53 || opcode == 0x54;
      if (!transports_input)
        state.encoded_input = false;
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
  const auto automatic_entry_lift =
      analyze_post_layout_automatic_entry_lift(items, flow, kMaximumProofStates);
  if (!automatic_entry_lift)
    return unchanged();
  std::vector<std::size_t> machine;
  std::size_t item = 0;
  for (const auto& op : ops) {
    machine.push_back(item);
    item += op.kind == IrKind::Label ? 1U : static_cast<std::size_t>(cells_per_op(op));
  }
  std::vector<Plan> plans;
  for (int reg = 0; reg < 15; ++reg) {
    if (auto plan = discover(ops, reg, flow, machine))
      plans.push_back(std::move(*plan));
    auto joint = discover_joint(ops, reg, flow, machine, options.address_space_model);
    plans.insert(plans.end(), std::make_move_iterator(joint.begin()),
                 std::make_move_iterator(joint.end()));
  }
  std::stable_sort(plans.begin(), plans.end(), [](const Plan& a, const Plan& b) {
    return a.saved_cells > b.saved_cells;
  });
  for (const auto& plan : plans) {
    std::string reason;
    const bool accepted = prove(items, flow, machine, plan, *automatic_entry_lift, reason);
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
      if (macro.definition || macro.input_literal) {
        for (const int opcode : macro.replacement_literal) {
          auto literal = ops[i];
          literal.opcode = opcode;
          literal.meta.mnemonic = opcode_by_code(opcode).name;
          literal.meta.comment = "finite mode projected representation";
          literal.semantic.clear();
          rewritten.push_back(std::move(literal));
        }
        if (macro.definition)
          rewritten.push_back(ops[macro.end - 1U]);
      } else {
        rewritten.push_back(ops[i]);
        for (const int opcode : macro.replacement_projection) {
          IrOp decoder;
          decoder.kind = IrKind::Plain;
          decoder.opcode = opcode;
          decoder.meta = ops[i].meta;
          decoder.meta.mnemonic = opcode_by_code(opcode).name;
          decoder.meta.comment = "finite mode consumer decoder";
          rewritten.push_back(std::move(decoder));
        }
      }
      i = macro.end;
    }
    const auto positions = label_positions(rewritten);
    if (std::any_of(plan.fixed_targets.begin(), plan.fixed_targets.end(),
                    [&](const auto& target) {
                      const auto at = positions.find(target.first);
                      return at == positions.end() || at->second != target.second;
                    }))
      continue;
    if (!build_post_layout_control_flow(lower_ir_to_machine(rewritten), options).proved)
      continue;
    return PassResult{
        .ops = std::move(rewritten), .applied = 1,
        .optimizations = {{
            .name = plan.synthesized ? "finite-mode-constraint-synthesis" :
                    plan.joint ? "finite-mode-multirole-representation" : "finite-mode-representation",
            .detail = std::string(plan.joint
                ? "Synthesized a shared encoding and independently certified consumer decoders for "
                : "Stored the common consumer projection of ") +
                      std::to_string(plan.value_count) + " finite mode values in one reaching-definition web of R" +
                      register_name_for_index(plan.reg) + "; jointly proved all definitions, "
                      "consumers, call/return contexts, exact indirect target/write-back constraints "
                      "and stack/X1/X2 continuations; removed " +
                      std::to_string(plan.saved_cells) + " local cells. The complete "
                      "shared-region candidate still competes on final layout size." +
                      (plan.synthesized ? " Constructed constants: " + plan.encoding_summary + "."
                                        : std::string{}),
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
