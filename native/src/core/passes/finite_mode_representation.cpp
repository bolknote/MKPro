#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/post_layout_packed_sign.hpp"

#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/constant_synthesis.hpp"
#include "mkpro/core/multirole_word_synthesis.hpp"
#include "mkpro/core/passes/semantic_region_folding.hpp"
#include "mkpro/core/passes/liveness_analysis.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/core/post_layout_progress_bounds.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/layout_search_artifact.hpp"
#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/passes/common_processor_factoring.hpp"
#include "mkpro/core/register_allocator.hpp"
#include "mkpro/core/stack_value_equivalence.hpp"
#include "mkpro/core/stable_register_value_flow.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <iterator>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <tuple>
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
  // A complete read/transform/write of the encoded carrier. The transition
  // table, rather than equality of its intermediate X, certifies the update.
  bool transition = false;
  bool coefficient_definition = false;
  bool coefficient_product = false;
  bool state_identity = false;
  bool inline_product = false;
  bool normalizing_max = false;
  bool predicate_observation = false;
  std::optional<int> replacement_recall;
  std::optional<int> finite_input_row;
  std::vector<int> finite_transition_rows;
  bool unit_store = false;
  bool unit_literal_elision = false;
};

enum class ExitRecoveryKind {
  ExponentAdjustment, LiteralRematerialization,
  ExponentObservation, UnaryObservation,
};

struct ExitRecovery {
  ExitRecoveryKind kind = ExitRecoveryKind::ExponentAdjustment;
  std::vector<int> code;
};

struct Plan {
  int reg = 0;
  int saved_cells = 0;
  std::size_t value_count = 0;
  std::map<std::size_t, Macro> macros;
  bool unit_input_normalization = false;
  bool joint = false;
  bool synthesized = false;
  std::string encoding_summary;
  std::set<std::size_t> flow_reads;
  std::map<std::string, int> fixed_targets;
  std::size_t transition_count = 0;
  std::optional<int> coefficient_reg;
  bool raw_coefficient = false;
  std::vector<PreloadReport> preloads;
  bool inline_coefficient = false;
  std::optional<int> seed_reg;
  std::size_t normalizing_max_count = 0;
  std::size_t opaque_max_count = 0;
  std::size_t predicate_observation_count = 0;
  std::vector<std::string> finite_values;
  std::vector<std::string> finite_encoding;
  std::map<std::size_t, ExitRecovery> exit_recoveries;
  std::size_t epoch_pruned_edges = 0;
  std::set<std::size_t> dead_manual_items;
  std::set<std::size_t> retired_manual_items;
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
          op.kind == IrKind::IndirectCall || op.kind == IrKind::IndirectJump ||
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



// A source-independent producer/consumer composition. The input is not a new
// stored representation: every ordinary carrier value remains bit-identical.
std::vector<Plan> discover_unit_input_normalization(
    const std::vector<IrOp>& ops, int reg,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<std::size_t>& machine,
    const std::vector<bool>& entry_lift,
    const FiniteModeCandidateOptions& options) {
  std::vector<Plan> plans;
  const auto uses = analyze_mode_uses(ops, reg, flow, machine, false);
  if (!uses) return plans;
  const auto inputs = literal_inputs(ops, flow, machine, *uses);
  if (!options.runtime_preloads || inputs.empty() || std::none_of(inputs.begin(), inputs.end(), [](const auto& entry) {
        return std::all_of(entry.second.begin(), entry.second.end(), [](const auto& input) { return input.value == "1" || input.value == "-1"; });
      })) return plans;
  const auto items = lower_ir_to_machine(ops);
  std::set<std::size_t> query_items;
  for(const auto& [store,literals]:inputs) {
    (void)store; for(const auto& literal:literals) query_items.insert(machine[literal.begin]);
  }
  std::vector<std::size_t> queries;
  for(std::size_t state=0;state<flow.execution_states.size();++state)
    if(query_items.contains(flow.execution_states[state].item_index)) queries.push_back(state);
  const auto signs=prove_post_layout_packed_sign_inputs(
      items,flow,entry_lift,queries,options.runtime_preloads,kMaximumProofStates);
  if(signs.exhausted) return plans;
  const auto sign_before=[&](std::size_t at,int wanted) {
    bool reached=false;
    for(const auto state:queries) if(flow.execution_states[state].item_index==machine[at]) {
      reached=true; const auto found=signs.signs.find(state);
      if(found==signs.signs.end() || found->second!=wanted) return false;
    }
    return reached;
  };
  for (const auto& [store, literals] : inputs) {
    if (std::any_of(literals.begin(), literals.end(), [](const auto& literal) {
          return literal.value != "1" && literal.value != "-1";
        })) continue;
    Plan plan;
    plan.reg = reg;
    plan.unit_input_normalization = true;
    plan.value_count = 2;
    plan.saved_cells = -1; // the shared K sign
    for (const auto& literal : literals) {
      Macro input;
      input.end = literal.end;
      input.input_literal = true;
      input.identical_value = true;
      input.unit_literal_elision = sign_before(literal.begin, literal.value == "1" ? 1 : -1);
      if (input.unit_literal_elision) plan.saved_cells += static_cast<int>(literal.end - literal.begin);
      else for (std::size_t i=literal.begin; i<literal.end; ++i) input.replacement_literal.push_back(ops[i].opcode);
      plan.macros.emplace(literal.begin, std::move(input));
    }
    if (plan.saved_cells <= 0 || plan.saved_cells < -options.maximum_growth_cells ||
        (options.maximum_growth_cells == 0 && plan.saved_cells <= 0)) continue;
    Macro normalization;
    normalization.end = store + 1U;
    normalization.unit_store = true;
    plan.macros.emplace(store, std::move(normalization));
    plans.push_back(std::move(plan));
  }
  return plans;
}

// Prove a right-multiplication operand domain on the matched return-context
// graph. This is a backwards must proof, not sampling or a guessed loop bound.
// Unknown roots, number-entry state and destructive register aliases reject.
bool power10_input(const std::vector<IrOp>& ops,
                   const AuthoritativePostLayoutControlFlow& flow,
                   const std::vector<std::size_t>& machine,
                   const std::vector<bool>& automatic_entry_lift,
                   std::size_t read, const std::vector<PreloadReport>& preloads) {
  enum class Property { Power, Exponent };
  struct Demand {
    std::size_t state;
    int reg; // -1 is X immediately before this instruction.
    Property property;
  };
  const auto satisfies = [](const std::string& value, Property property) {
    const auto number = exact_decimal(value);
    if (!number)
      return false;
    if (property == Property::Exponent)
      return number->numerator % number->denominator == 0 &&
             number->numerator / number->denominator >= -8 &&
             number->numerator / number->denominator <= 8;
    auto numerator = number->numerator;
    auto denominator = number->denominator;
    if (numerator <= 0)
      return false;
    const auto common = std::gcd(numerator, denominator);
    numerator /= common;
    denominator /= common;
    int exponent = 0;
    while (numerator > 1 && numerator % 10 == 0) {
      numerator /= 10;
      ++exponent;
    }
    while (denominator > 1 && denominator % 10 == 0) {
      denominator /= 10;
      --exponent;
    }
    return numerator == 1 && denominator == 1 && exponent >= -8 && exponent <= 8;
  };
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
  std::deque<Demand> pending;
  for (std::size_t i = 0; i < flow.execution_states.size(); ++i)
    if (flow.execution_states[i].item_index == machine[read])
      pending.push_back({i, -1, Property::Power});
  if (pending.empty())
    return false;
  std::set<std::tuple<std::size_t, int, Property>> seen;
  std::size_t work = 0;
  bool grounded = false;
  while (!pending.empty()) {
    const auto demand = pending.front();
    pending.pop_front();
    if (!seen.emplace(demand.state, demand.reg, demand.property).second)
      continue;
    if (++work > kMaximumProofStates)
      return false;
    const auto item = flow.execution_states[demand.state].item_index;
    // A preload is only an entry fact at the actual main entry, never at a
    // resume/manual entry where the player may have changed the registers.
    if (external.contains(item) || predecessors[demand.state].empty()) {
      if (item != machine.front() || demand.reg < 0)
        return false;
      const auto name = register_name_for_index(demand.reg);
      const auto preload = std::find_if(preloads.begin(), preloads.end(),
          [&](const auto& value) { return value.register_name == name; });
      if (preload == preloads.end() || preload->setup_expression ||
          preload->setup_target_name || !satisfies(preload->value, demand.property))
        return false;
      grounded = true;
    }
    for (const auto before : predecessors[demand.state]) {
      const auto found = ir_at.find(flow.execution_states[before].item_index);
      if (found == ir_at.end())
        return false;
      const auto index = found->second;
      const auto& op = ops[index];
      if (!ordinary(op) || op.target_meta.formal_opcode)
        return false;
      const int opcode = op.opcode;
      const int family = opcode & 0xf0;
      if (demand.reg >= 0) {
        constexpr std::array<int, 4> loops{0x5d, 0x5b, 0x58, 0x5a};
        if (op.kind == IrKind::Store && opcode == 0x40 + demand.reg) {
          pending.push_back({before, -1, demand.property});
          continue;
        }
        if ((family >= 0x70 && family <= 0xe0 && (opcode & 15) == demand.reg) ||
            (demand.reg < 4 && opcode == loops[static_cast<std::size_t>(demand.reg)]) ||
            opcode == 0x4f || opcode == 0x6f || opcode == 0x50)
          return false;
        if (family == 0xb0) {
          const auto aliases = flow.indirect_memory_targets.find(machine[index]);
          if (aliases == flow.indirect_memory_targets.end() ||
              std::find(aliases->second.begin(), aliases->second.end(), demand.reg) !=
                  aliases->second.end())
            return false;
        }
        pending.push_back({before, demand.reg, demand.property});
        continue;
      }
      if (const auto literal = ending.find(index); literal != ending.end()) {
        if (!satisfies(literal->second.value, demand.property))
          return false;
        auto start = before;
        for (std::size_t i = index; i > literal->second.begin; --i) {
          if (external.contains(machine[i]) || predecessors[start].size() != 1U)
            return false;
          start = predecessors[start].front();
          if (flow.execution_states[start].item_index != machine[i - 1U])
            return false;
        }
        if (!automatic_entry_lift[start])
          return false;
        grounded = true;
      } else if (op.kind == IrKind::Recall) {
        pending.push_back({before, opcode - 0x60, demand.property});
      } else if (op.kind == IrKind::Plain && opcode == 0x15 &&
                 demand.property == Property::Power) {
        pending.push_back({before, -1, Property::Exponent});
      } else if (op.kind == IrKind::Plain && opcode == 0x0d &&
                 demand.property == Property::Exponent) {
        grounded = true;
      } else if (op.kind == IrKind::Store || op.kind == IrKind::Call ||
                 op.kind == IrKind::Return || op.kind == IrKind::Jump ||
                 op.kind == IrKind::CondJump || op.kind == IrKind::Loop ||
                 (op.kind == IrKind::Plain && opcode == 0x54)) {
        pending.push_back({before, -1, demand.property});
      } else {
        return false;
      }
    }
  }
  return grounded;
}

// Fuse an immutable projection cache with its finite carrier. The relation
// between the two registers is versioned in the product proof below: a new
// phase invalidates the cache, even when both values still belong to the same
// finite set. The raw alternative additionally needs an exhaustive operand
// domain and a delivered seed in the eliminated cache register.
std::vector<Plan> discover_coefficient_carriers(
    const std::vector<IrOp>& ops, int reg,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<std::size_t>& machine,
    const std::vector<bool>& automatic_entry_lift,
    const FiniteModeCandidateOptions& options) {
  std::set<int> coefficients;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    if (ops[i].kind != IrKind::Recall || ops[i].opcode != 0x60 + reg || !ordinary(ops[i]))
      continue;
    const auto projection = projection_after(ops, i);
    if (projection && projection->end < ops.size() &&
        ops[projection->end].kind == IrKind::Store && ordinary(ops[projection->end]) &&
        ops[projection->end].opcode != 0x40 + reg)
      coefficients.insert(ops[projection->end].opcode - 0x40);
  }
  if (coefficients.empty())
    return {};
  const auto uses = analyze_mode_uses(ops, reg, flow, machine, false);
  if (!uses)
    return {};
  const auto inputs = literal_inputs(ops, flow, machine, *uses);
  std::map<std::size_t, Literal> literals;
  std::set<std::size_t> stores, transitions;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    if (ops[i].kind != IrKind::Store || ops[i].opcode != 0x40 + reg)
      continue;
    if (!ordinary(ops[i]))
      return {};
    if (i >= 2U && ordinary(ops[i - 2U]) && ordinary(ops[i - 1U]) &&
        ops[i - 2U].kind == IrKind::Recall && ops[i - 2U].opcode == 0x60 + reg &&
        ops[i - 1U].kind == IrKind::Plain && ops[i - 1U].opcode == 0x0b) {
      transitions.insert(i - 2U);
    } else {
      const auto sources = inputs.find(i);
      if (sources == inputs.end())
        return {};
      stores.insert(i);
      for (const auto& literal : sources->second) {
        if (literal.value != "1" && literal.value != "-1")
          return {};
        literals.emplace(literal.begin, literal);
      }
    }
  }
  if (literals.empty())
    return {};
  std::vector<Plan> plans;
  for (const int coefficient : coefficients) {
    if (coefficient < 0 || coefficient >= 15)
      continue;
    std::map<std::size_t, Projection> producers;
    std::set<std::size_t> producer_stores, products, identities;
    std::optional<int> exponent;
    bool complete = true;
    for (std::size_t i = 0; i < ops.size(); ++i) {
      if (ops[i].kind == IrKind::Recall && ops[i].opcode == 0x60 + reg) {
        if (!ordinary(ops[i])) {
          complete = false;
          break;
        }
        if (transitions.contains(i))
          continue;
        const auto projection = projection_after(ops, i);
        if (projection && projection->end < ops.size() &&
            ops[projection->end].kind == IrKind::Store &&
            ops[projection->end].opcode == 0x40 + coefficient &&
            ordinary(ops[projection->end])) {
          const auto positive = projection->apply("1");
          const auto negative = projection->apply("-1");
          constexpr std::array<const char*, 4> powers{"0.001", "0.01", "0.1", "1"};
          const auto found = std::find_if(powers.begin(), powers.end(),
              [&](const char* power) { return positive && *positive == power; });
          if (found == powers.end() || !negative || *negative != "-" + *positive) {
            complete = false;
            break;
          }
          const int power = static_cast<int>(found - powers.begin()) - 3;
          if (exponent && *exponent != power) {
            complete = false;
            break;
          }
          exponent = power;
          producers.emplace(i, *projection);
          producer_stores.insert(projection->end);
        } else {
          identities.insert(i);
        }
      }
      if (ops[i].kind == IrKind::Recall && ops[i].opcode == 0x60 + coefficient) {
        if (!ordinary(ops[i]) || i + 1U >= ops.size() || !ordinary(ops[i + 1U]) ||
            ops[i + 1U].kind != IrKind::Plain || ops[i + 1U].opcode != 0x12) {
          complete = false;
          break;
        }
        products.insert(i);
      }
    }
    for (std::size_t i = 0; i < ops.size(); ++i)
      if (ops[i].kind == IrKind::Store && ops[i].opcode == 0x40 + coefficient &&
          !producer_stores.contains(i))
        complete = false;
    if (!complete || producers.empty() || products.empty() || !exponent)
      continue;
    const auto positive = *producers.begin()->second.apply("1");
    std::vector<std::pair<bool, std::string>> encodings{{false, positive}};
    const auto preload_conflict = [&] {
      const auto name = register_name_for_index(coefficient);
      return std::any_of(options.runtime_preloads->begin(), options.runtime_preloads->end(),
          [&](const auto& preload) {
            std::string old = preload.register_name;
            for (char& c : old)
              if (c >= 'A' && c <= 'F')
                c = static_cast<char>(c - 'A' + 'a');
            return old == name;
          });
    };
    if (options.runtime_preloads && !preload_conflict() &&
        std::all_of(products.begin(), products.end(), [&](std::size_t read) {
          return power10_input(ops, flow, machine, automatic_entry_lift, read,
                               *options.runtime_preloads);
        })) {
      RawWordSynthesisRequest request;
      for (const int sign : {1, -1}) {
        RawWordMode mode;
        mode.sign = sign;
        for (int input = -8; input <= 8; ++input)
          mode.right_products.push_back({input, input + *exponent, sign});
        request.modes.push_back(std::move(mode));
      }
      request.transitions = {{0, 1, RawWordTransitionKind::Negate},
                             {1, 0, RawWordTransitionKind::Negate}};
      const auto proposed = synthesize_raw_multirole_words(
          request, {.maximum_work = 4096, .maximum_candidates = 1});
      for (const auto& encoding : proposed.encodings)
        if (certify_raw_multirole_words(encoding, request))
          encodings.emplace_back(true, encoding.words.front());
    }
    for (const auto& [raw, word] : encodings) {
      Plan plan;
      plan.reg = reg;
      plan.coefficient_reg = coefficient;
      plan.raw_coefficient = raw;
      plan.synthesized = true;
      plan.value_count = 2;
      plan.transition_count = transitions.size();
      plan.encoding_summary = "1 -> " + word + ", -1 -> -" + word;
      if (raw)
        plan.preloads.push_back({.register_name = register_name_for_index(coefficient),
                                 .value = word, .lowered_data_value = word});
      for (const auto& [begin, literal] : literals) {
        Macro definition;
        definition.end = literal.end;
        definition.input_literal = true;
        definition.identical_value = !raw && positive == "1";
        if (raw) {
          definition.replacement_literal = {0x60 + coefficient};
          if (literal.value == "-1")
            definition.replacement_literal.push_back(0x0b);
        } else {
          definition.replacement_literal =
              *encode_decimal(literal.value == "1" ? positive : "-" + positive);
        }
        plan.saved_cells += static_cast<int>(literal.end - begin) -
                            static_cast<int>(definition.replacement_literal.size());
        plan.macros.emplace(begin, std::move(definition));
      }
      for (const auto store : stores) {
        Macro definition;
        definition.end = store + 1U;
        definition.input_store = true;
        plan.macros.emplace(store, std::move(definition));
      }
      for (const auto read : transitions) {
        Macro update;
        update.end = read + 3U;
        update.transition = true;
        update.identical_value = !raw && positive == "1";
        update.replacement_projection = {0x0b};
        plan.macros.emplace(read, std::move(update));
      }
      for (const auto& [read, projection] : producers) {
        Macro producer;
        producer.end = projection.end + 1U;
        producer.coefficient_definition = true;
        plan.saved_cells += static_cast<int>(producer.end - read) - 1;
        plan.macros.emplace(read, std::move(producer));
      }
      for (const auto read : products) {
        Macro product;
        product.end = read + 2U;
        product.coefficient_product = true;
        product.replacement_recall = reg;
        product.replacement_projection = {0x12};
        plan.macros.emplace(read, std::move(product));
      }
      for (const auto read : identities) {
        Macro identity;
        identity.end = read + 1U;
        identity.state_identity = true;
        identity.replacement_projection = {0x32};
        if (identity.end < ops.size() && ordinary(ops[identity.end]) &&
            ops[identity.end].kind == IrKind::Plain && ops[identity.end].opcode == 0x32)
          ++identity.end;
        plan.saved_cells += static_cast<int>(identity.end - read) - 2;
        plan.macros.emplace(read, std::move(identity));
      }
      std::size_t end = 0;
      bool disjoint = true;
      for (const auto& [begin, macro] : plan.macros) {
        disjoint = disjoint && begin >= end;
        end = macro.end;
      }
      if (disjoint && plan.saved_cells >= -options.maximum_growth_cells &&
          (options.maximum_growth_cells > 0 || plan.saved_cells > 0))
        plans.push_back(std::move(plan));
    }
  }
  return plans;
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

struct StateTransition {
  std::size_t read = 0;
  std::size_t store = 0;
  Projection function;
};

// A branch observes a predicate, not necessarily the complete carrier word.
// Do not include FL (a destructive register operation), indirect selectors or
// a branch with an independently addressed interior in this observation.
std::optional<int> predicate_after(const std::vector<IrOp>& ops, std::size_t read) {
  if (read + 1U >= ops.size() || !ordinary(ops[read]) ||
      !ordinary(ops[read + 1U]) || ops[read + 1U].kind != IrKind::CondJump)
    return std::nullopt;
  const int opcode = ops[read + 1U].opcode;
  if (opcode == 0x57 || opcode == 0x59 || opcode == 0x5c || opcode == 0x5e)
    return opcode;
  return std::nullopt;
}

std::optional<bool> ordinary_predicate(int opcode, const std::string& word) {
  const auto number = exact_decimal(word);
  if (!number)
    return std::nullopt;
  // Equality of these truth values preserves both hardware successors,
  // regardless of which edge the lowering uses for the source-language arm.
  switch (opcode) {
    case 0x57: return number->numerator != 0;
    case 0x59: return number->numerator >= 0;
    case 0x5c: return number->numerator < 0;
    case 0x5e: return number->numerator == 0;
    default: return std::nullopt;
  }
}


// A physical register can have several unrelated epochs. Close only the
// def/use component reached from one finite literal seed, including transition
// outputs and every incoming definition at a merged read. Dynamic definitions
// in another component are not a reason to reject this one.
struct FiniteCarrierWeb {
  std::map<std::size_t, Literal> literals;
  std::set<std::size_t> seeds;
  std::map<std::size_t, StateTransition> updates;
  std::set<std::size_t> reads;
  std::set<std::string> values;
  std::map<std::size_t, std::size_t> normalizers;
  std::map<std::size_t, int> predicates;
};

std::vector<FiniteCarrierWeb> finite_carrier_webs(
    const std::vector<IrOp>& ops, int reg,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<std::size_t>& machine) {
  const auto uses = analyze_mode_uses(ops, reg, flow, machine, true);
  if (!uses)
    return {};
  const auto inputs = literal_inputs(ops, flow, machine, *uses);
  std::map<std::size_t, StateTransition> transitions;
  std::map<std::size_t, std::size_t> transition_reads;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    if (ops[i].kind != IrKind::Recall || ops[i].opcode != 0x60 + reg || !ordinary(ops[i]))
      continue;
    const auto projection = projection_after(ops, i);
    if (projection && projection->end < ops.size() &&
        ops[projection->end].kind == IrKind::Store &&
        ops[projection->end].opcode == 0x40 + reg && ordinary(ops[projection->end])) {
      transitions.emplace(projection->end, StateTransition{i, projection->end, *projection});
      transition_reads.emplace(i, projection->end);
    }
  }
  // A reduction write can end an encoded epoch rather than extending its
  // finite state table. Other reaching definitions of this particular read
  // may be ordinary dynamic values. Admission still needs an independent
  // numeric absorption certificate and the relational continuation proof.
  std::map<std::size_t, std::size_t> normalizers;
  for (std::size_t i = 0; i + 2U < ops.size(); ++i)
    if (ops[i].kind == IrKind::Recall && ops[i].opcode == 0x60 + reg &&
        ops[i + 1U].kind == IrKind::Plain && ops[i + 1U].opcode == 0x36 &&
        ops[i + 2U].kind == IrKind::Store && ops[i + 2U].opcode == 0x40 + reg &&
        ordinary(ops[i]) && ordinary(ops[i + 1U]) && ordinary(ops[i + 2U]))
      normalizers.emplace(i, i + 2U);
  std::vector<FiniteCarrierWeb> result;
  std::set<std::size_t> visited;
  for (const auto& [seed, unused_literals] : inputs) {
    (void)unused_literals;
    if (visited.contains(seed))
      continue;
    FiniteCarrierWeb web;
    std::set<std::size_t> stores;
    std::deque<std::size_t> pending{seed};
    bool complete = true;
    while (!pending.empty()) {
      const auto store = pending.front();
      pending.pop_front();
      if (!stores.insert(store).second)
        continue;
      visited.insert(store);
      const auto consumers = uses->definition_reads.find(store);
      if (store >= ops.size() || !ordinary(ops[store]) ||
          (!inputs.contains(store) && !transitions.contains(store)) ||
          consumers == uses->definition_reads.end()) {
        complete = false;
        continue;
      }
      for (const auto read : consumers->second) {
        if (!web.reads.insert(read).second)
          continue;
        if (const auto sink = normalizers.find(read); sink != normalizers.end()) {
          web.normalizers.emplace(read, sink->second);
          continue;
        }
        if (const auto predicate = predicate_after(ops, read)) {
          // Ordinary dynamic definitions reaching this same instruction stay
          // unchanged. The codec must separately preserve the finite rows'
          // predicate, and the product proof tracks the unequal numeric X
          // through BOTH continuations. This is not a numerical decoder.
          web.predicates.emplace(read, *predicate);
          continue;
        }
        const auto& definitions = uses->read_definitions.at(read);
        pending.insert(pending.end(), definitions.begin(), definitions.end());
        if (const auto update = transition_reads.find(read); update != transition_reads.end())
          pending.push_back(update->second);
      }
    }
    if (!complete)
      continue;
    for (const auto store : stores) {
      if (const auto update = transitions.find(store); update != transitions.end()) {
        web.updates.emplace(update->second.read, update->second);
      } else {
        web.seeds.insert(store);
        for (const auto& literal : inputs.at(store)) {
          web.literals.emplace(literal.begin, literal);
          web.values.insert(literal.value);
        }
      }
    }
    if (web.values.empty() || web.values.size() > kMaximumModeValues)
      continue;
    std::deque<std::string> closure(web.values.begin(), web.values.end());
    while (complete && !closure.empty()) {
      const auto value = closure.front();
      closure.pop_front();
      for (const auto& [unused_read, update] : web.updates) {
        (void)unused_read;
        const auto next = update.function.apply(value);
        if (!next || !encode_decimal(*next)) {
          complete = false;
          break;
        }
        if (web.values.insert(*next).second) {
          if (web.values.size() > kMaximumModeValues) {
            complete = false;
            break;
          }
          closure.push_back(*next);
        }
      }
    }
    if (complete)
      result.push_back(std::move(web));
  }
  return result;
}

// A new preload may use only a globally untouched stock-machine register.
// All indirect targets are read from the authoritative CFG, never inferred
// from a conveniently named variable or a partial preload map.
std::optional<int> free_carrier_seed(
    const std::vector<IrOp>& ops, const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<PreloadReport>* preloads) {
  if (!preloads)
    return std::nullopt;
  // A typed input protocol changes X and executes its declared commands, not
  // an arbitrary register bank. Its stores/selectors are accounted for below.
  // Keep the old rejection for malformed or opaque manual entry boundaries.
  const bool has_manual = std::any_of(ops.begin(), ops.end(), [](const IrOp& op) {
    return op.meta.manual_interaction.has_value();
  });
  if (has_manual &&
      !prove_post_layout_input_boundaries(lower_ir_to_machine(ops), flow).proved)
    return std::nullopt;
  std::array<bool, 15> used{};
  for (const auto& preload : *preloads) {
    std::string name = preload.register_name;
    for (char& c : name)
      if (c >= 'A' && c <= 'F')
        c = static_cast<char>(c - 'A' + 'a');
    bool recognized = false;
    for (int reg = 0; reg < 15; ++reg)
      if (name == register_name_for_index(reg)) {
        used[static_cast<std::size_t>(reg)] = true;
        recognized = true;
      }
    if (!recognized)
      return std::nullopt;
  }
  for (const auto& op : ops) {
    if (op.meta.raw || op.kind == IrKind::OrphanAddress)
      return std::nullopt;
    const int family = op.opcode & 0xf0;
    if (op.kind != IrKind::Label &&
        (family == 0x40 || (family >= 0x60 && family <= 0xe0))) {
      const int reg = op.opcode & 15;
      if (reg == 15)
        return std::nullopt;
      used[static_cast<std::size_t>(reg)] = true;
    }
    constexpr std::array<int, 4> loops{0x5d, 0x5b, 0x58, 0x5a};
    for (std::size_t reg = 0; reg < loops.size(); ++reg)
      if (op.opcode == loops[reg])
        used[reg] = true;
  }
  for (const auto& [unused_item, targets] : flow.indirect_memory_targets) {
    (void)unused_item;
    for (const int target : targets) {
      if (target < 0 || target >= 15)
        return std::nullopt;
      used[static_cast<std::size_t>(target)] = true;
    }
  }
  for (int reg = 0; reg < 15; ++reg)
    if (!used[static_cast<std::size_t>(reg)])
      return reg;
  return std::nullopt;
}

// Inline consumers need no materialized coefficient register. Select their
// finite projection as the state representation and synthesize all remaining
// observations/transitions from the same table. The whole inline product is
// one proof macro, because erasing a binary projection also changes Z and T.
std::vector<Plan> discover_inline_coefficient_carriers(
    const std::vector<IrOp>& ops, int reg,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<std::size_t>& machine,
    const std::vector<bool>& automatic_entry_lift,
    const FiniteModeCandidateOptions& options) {
  bool has_product = false;
  for (std::size_t i = 0; i < ops.size() && !has_product; ++i) {
    if (ops[i].kind != IrKind::Recall || ops[i].opcode != 0x60 + reg)
      continue;
    const auto projection = projection_after(ops, i);
    has_product = projection && projection->end < ops.size() &&
        ordinary(ops[projection->end]) && ops[projection->end].kind == IrKind::Plain &&
        ops[projection->end].opcode == 0x12;
  }
  if (!has_product)
    return {};
  std::vector<Plan> plans;
  const auto free_seed = free_carrier_seed(ops, flow, options.runtime_preloads);
  const auto webs = finite_carrier_webs(ops, reg, flow, machine);
  std::vector<std::size_t> reduction_items;
  for (const auto& web : webs)
    for (const auto& [read, unused_store] : web.normalizers) {
      (void)unused_store;
      reduction_items.push_back(machine[read]);
    }
  std::sort(reduction_items.begin(), reduction_items.end());
  reduction_items.erase(std::unique(reduction_items.begin(), reduction_items.end()),
                        reduction_items.end());
  const auto numeric = reduction_items.empty() ? PostLayoutNumericRangeProof{}
      : prove_post_layout_numeric_ranges(lower_ir_to_machine(ops), flow,
            automatic_entry_lift, reduction_items, options.runtime_preloads,
             kMaximumProofStates, NumericRangeInputPolicy::ValidatedRegisterTransport);
  if (std::getenv("MKPRO_NATIVE_TRACE_FINITE_MODE_REPRESENTATION") && !webs.empty())
    std::cerr << "[finite-mode-discovery] R" << register_name_for_index(reg)
              << " webs=" << webs.size() << " reductions=" << reduction_items.size()
              << " ranges=" << numeric.x_before.size() << '\n';
  for (const auto& web : webs) {
    std::map<std::size_t, Projection> observations;
    std::set<std::size_t> products;
    std::vector<Projection> encoders, functions;
    bool complete = true;
    for (const auto read : web.reads) {
      if (web.updates.contains(read) || web.normalizers.contains(read) ||
          web.predicates.contains(read))
        continue;
      if (!ordinary(ops[read]) || flow_read(ops[read], reg)) {
        complete = false;
        break;
      }
      auto projection = projection_after(ops, read);
      if (!projection)
        projection = Projection{-1, std::nullopt, read + 1U}; // logical identity
      const bool product = projection->opcode >= 0 && projection->end < ops.size() &&
          ordinary(ops[projection->end]) && ops[projection->end].kind == IrKind::Plain &&
          ops[projection->end].opcode == 0x12;
      observations.emplace(read, *projection);
      functions.push_back(*projection);
      if (product) {
        products.insert(read);
        if (std::none_of(encoders.begin(), encoders.end(), [&](const auto& old) {
              return old.same_function(*projection);
            }))
          encoders.push_back(*projection);
      }
    }
    if (!complete || products.empty())
      continue;
    for (const auto& [unused_read, update] : web.updates) {
      (void)unused_read;
      functions.push_back(update.function);
    }
    const std::vector<std::string> values(web.values.begin(), web.values.end());
    for (const auto& encoder : encoders) {
      std::vector<std::string> encoded;
      for (const auto& value : values) {
        const auto word = encoder.apply(value);
        if (!word)
          break;
        encoded.push_back(*word);
      }
      if (encoded.size() != values.size())
        continue;
      struct Encoding {
        std::vector<std::string> words;
        bool raw = false;
        bool preload = false;
      };
      std::vector<Encoding> alternatives{{encoded, false, false}};
      const bool unit_signs = web.values == std::set<std::string>{"-1", "1"};
      const bool symmetric = unit_signs && encoded[0] == "-" + encoded[1];
      if (free_seed && symmetric)
        alternatives.push_back({encoded, false, true});
      if (free_seed && symmetric && web.normalizers.empty() && web.predicates.empty() &&
          std::all_of(web.updates.begin(), web.updates.end(), [](const auto& update) {
            return update.second.function.opcode == 0x0b && !update.second.function.operand;
          }) &&
          std::all_of(products.begin(), products.end(), [&](const auto read) {
            return encoder.same_function(observations.at(read)) &&
                power10_input(ops, flow, machine, automatic_entry_lift, read,
                              *options.runtime_preloads);
          })) {
        constexpr std::array<const char*, 4> powers{"0.001", "0.01", "0.1", "1"};
        const auto power = std::find_if(powers.begin(), powers.end(),
            [&](const char* word) { return encoded[1] == word; });
        if (power != powers.end()) {
          const int exponent = static_cast<int>(power - powers.begin()) - 3;
          RawWordSynthesisRequest request;
          for (const int sign : {-1, 1}) {
            RawWordMode mode;
            mode.sign = sign;
            for (int input = -8; input <= 8; ++input)
              mode.right_products.push_back({input, input + exponent, sign});
            request.modes.push_back(std::move(mode));
          }
          request.transitions = {{0, 1, RawWordTransitionKind::Negate},
                                 {1, 0, RawWordTransitionKind::Negate}};
          const auto proposed = synthesize_raw_multirole_words(
              request, {.maximum_work = 4096, .maximum_candidates = 1});
          for (const auto& encoding : proposed.encodings)
            if (certify_raw_multirole_words(encoding, request))
              alternatives.push_back({encoding.words, true, true});
        }
      }
      for (const auto& encoding : alternatives) {
        Plan plan;
        plan.reg = reg;
        plan.inline_coefficient = true;
        plan.raw_coefficient = encoding.raw;
        plan.synthesized = true;
        plan.value_count = values.size();
        plan.finite_values = values;
        plan.finite_encoding = encoding.words;
        plan.transition_count = web.updates.size();
        std::map<std::string, std::string> codebook;
        for (std::size_t row = 0; row < values.size(); ++row) {
          codebook.emplace(values[row], encoding.words[row]);
          if (!plan.encoding_summary.empty())
            plan.encoding_summary += ", ";
          plan.encoding_summary += values[row] + " -> " + encoding.words[row];
        }
        if (encoding.preload) {
          plan.seed_reg = free_seed;
          plan.preloads.push_back({.register_name = register_name_for_index(*free_seed),
              .value = encoding.words[1], .lowered_data_value = encoding.words[1]});
        }
        bool valid = true;
        for (const auto& [begin, literal] : web.literals) {
          Macro definition;
          definition.end = literal.end;
          definition.input_literal = true;
          definition.identical_value = !encoding.raw && literal.value == codebook.at(literal.value);
          definition.finite_input_row = static_cast<int>(
              std::find(values.begin(), values.end(), literal.value) - values.begin());
          if (encoding.preload) {
            definition.replacement_literal = {0x60 + *free_seed};
            if (literal.value == "-1")
              definition.replacement_literal.push_back(0x0b);
          } else {
            const auto code = encode_decimal(codebook.at(literal.value));
            if (!code) {
              valid = false;
              break;
            }
            definition.replacement_literal = *code;
          }
          plan.saved_cells += static_cast<int>(literal.end - begin) -
                              static_cast<int>(definition.replacement_literal.size());
          plan.macros.emplace(begin, std::move(definition));
        }
        for (const auto store : web.seeds) {
          Macro definition;
          definition.end = store + 1U;
          definition.input_store = true;
          plan.macros.emplace(store, std::move(definition));
        }
        for (const auto& [read, projection] : observations) {
          std::vector<std::string> wanted;
          for (const auto& value : values) {
            const auto output = projection.opcode < 0
                ? std::optional<std::string>{value} : projection.apply(value);
            if (!output)
              break;
            wanted.push_back(*output);
          }
          if (!valid || wanted.size() != values.size()) {
            valid = false;
            break;
          }
          const bool product = products.contains(read);
          std::optional<Decoder> decoder;
          if (!encoding.raw) {
            decoder = choose_decoder(encoding.words, wanted, functions);
          } else if (product) {
            decoder = Decoder{};
          } else if (const auto suffix = projection_code(projection)) {
            decoder = Decoder{{0x32}, projection.operand.has_value()};
            decoder->code.insert(decoder->code.end(), suffix->begin(), suffix->end());
          }
          if (!decoder) {
            valid = false;
            break;
          }
          Macro consumer;
          consumer.end = projection.end + (product ? 1U : 0U);
          consumer.binary_projection = projection.operand.has_value();
          consumer.replacement_projection = decoder->code;
          consumer.replacement_binary = decoder->binary;
          consumer.inline_product = product;
          if (product)
            consumer.replacement_projection.push_back(0x12);
          plan.saved_cells += static_cast<int>(consumer.end - read - 1U) -
                              static_cast<int>(consumer.replacement_projection.size());
          plan.macros.emplace(read, std::move(consumer));
        }
        for (const auto& [read, update] : web.updates) {
          std::vector<std::string> wanted;
          bool identical = !encoding.raw;
          for (const auto& value : values) {
            const auto next = update.function.apply(value);
            if (!next || !codebook.contains(*next)) {
              valid = false;
              break;
            }
            wanted.push_back(codebook.at(*next));
            identical = identical && *next == codebook.at(*next);
          }
          if (!valid)
            break;
          const auto decoder = encoding.raw ? std::optional<Decoder>{Decoder{{0x0b}, false}}
              : choose_decoder(encoding.words, wanted, functions);
          if (!decoder) {
            valid = false;
            break;
          }
          Macro transition;
          transition.end = update.store + 1U;
          transition.transition = true;
          for (const auto& value : values)
            transition.finite_transition_rows.push_back(static_cast<int>(
                std::find(values.begin(), values.end(), *update.function.apply(value)) -
                values.begin()));
          transition.identical_value = identical;
          transition.binary_projection = update.function.operand.has_value();
          transition.replacement_projection = decoder->code;
          transition.replacement_binary = decoder->binary;
          plan.saved_cells += static_cast<int>(update.store - read - 1U) -
                              static_cast<int>(decoder->code.size());
          plan.macros.emplace(read, std::move(transition));
        }
        for (const auto& [read, store] : web.normalizers) {
          const auto domain = numeric.x_before.find(machine[read]);
          // Exclude zero explicitly: Kmax does not implement the ordinary
          // real-number ordering at zero. Raw words need a different theorem.
          bool absorbs = !encoding.raw && domain != numeric.x_before.end() &&
                         domain->second.lower > 0;
          for (std::size_t row = 0; absorbs && row < values.size(); ++row) {
            const auto original = bound_ordinary_decimal(values[row]);
            const auto encoded_value = bound_ordinary_decimal(encoding.words[row]);
            absorbs = original && encoded_value &&
                (original->lower != 0 || original->upper != 0) &&
                (encoded_value->lower != 0 || encoded_value->upper != 0) &&
                domain->second.lower > original->upper &&
                domain->second.lower > encoded_value->upper;
          }
          if (!absorbs) {
            // This is an ordinary observation, NOT an absorbing boundary.
            // Leave its instructions outside the representation macros. The
            // relational proof must restore the original carrier first; an
            // unknown, zero or raw score never acquires an ordering theorem.
            ++plan.opaque_max_count;
            continue;
          }
          Macro reduction;
          reduction.end = store + 1U;
          reduction.normalizing_max = true;
          plan.macros.emplace(read, std::move(reduction));
          ++plan.normalizing_max_count;
        }
        for (const auto& [read, opcode] : web.predicates) {
          // Raw words and signed zero do not acquire ordinary-real ordering
          // merely because their rendered text has a familiar sign.
          if (encoding.raw) {
            valid = false;
            break;
          }
          for (std::size_t row = 0; row < values.size(); ++row) {
            const auto before = ordinary_predicate(opcode, values[row]);
            const auto after = ordinary_predicate(opcode, encoding.words[row]);
            if (!before || !after || *before != *after) {
              valid = false;
              break;
            }
          }
          if (!valid)
            break;
          Macro predicate;
          predicate.end = read + 2U;
          predicate.predicate_observation = true;
          plan.macros.emplace(read, std::move(predicate));
          ++plan.predicate_observation_count;
        }
        std::size_t end = 0;
        for (const auto& [begin, macro] : plan.macros) {
          valid = valid && begin >= end;
          end = macro.end;
        }
        if (valid && plan.saved_cells >= -options.maximum_growth_cells &&
            (options.maximum_growth_cells > 0 || plan.saved_cells > 0))
          plans.push_back(std::move(plan));
      }
    }
  }
  return plans;
}

// Discover a closed finite transition system before selecting its physical
// encoding. Reaching definitions alone cannot do this: the first backedge
// looks like a dynamic definition, even for an exact two-cycle such as -v.
// No source names, distinguished constants or presumed iteration count enter
// the proof. Closure over all admitted transitions is an overapproximation of
// the reachable states, so a path correlation is never silently assumed.
std::vector<Plan> discover_state_machine(
    const std::vector<IrOp>& ops, int reg,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<std::size_t>& machine,
    const FiniteModeCandidateOptions& candidate_options) {
  std::map<std::size_t, StateTransition> transitions;
  std::map<std::size_t, std::size_t> transition_reads;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    if (!ordinary(ops[i]) || ops[i].kind != IrKind::Recall ||
        ops[i].opcode != 0x60 + reg)
      continue;
    const auto projection = projection_after(ops, i);
    if (!projection || projection->end >= ops.size())
      continue;
    const auto& store = ops[projection->end];
    if (!ordinary(store) || store.kind != IrKind::Store || store.opcode != 0x40 + reg)
      continue;
    transitions.emplace(projection->end, StateTransition{i, projection->end, *projection});
    transition_reads.emplace(i, projection->end);
  }
  if (transitions.empty())
    return {};
  const auto uses = analyze_mode_uses(ops, reg, flow, machine, true);
  if (!uses)
    return {};
  const auto inputs = literal_inputs(ops, flow, machine, *uses);
  if (inputs.empty())
    return {};

  std::vector<Plan> plans;
  std::set<std::size_t> visited;
  for (const auto& [seed, unused_literals] : inputs) {
    (void)unused_literals;
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
      if (!inputs.contains(store) && !transitions.contains(store))
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
        // Include a transition's output even if its last definition is dead.
        // The changed input cannot be left flowing into an ordinary store.
        if (const auto next = transition_reads.find(read); next != transition_reads.end())
          pending.push_back(next->second);
      }
    }
    if (!complete)
      continue;

    std::map<std::size_t, Literal> literals;
    std::map<std::size_t, StateTransition> updates;
    std::map<std::size_t, Projection> consumers;
    std::vector<Projection> functions;
    std::set<std::string> values;
    for (const auto store : stores) {
      if (const auto transition = transitions.find(store); transition != transitions.end()) {
        updates.emplace(transition->second.read, transition->second);
      } else {
        for (const auto& literal : inputs.at(store)) {
          literals.emplace(literal.begin, literal);
          values.insert(literal.value);
        }
      }
    }
    for (const auto read : reads) {
      if (updates.contains(read))
        continue;
      if (flow_read(ops[read], reg)) {
        // A destructive control observation needs its own transition table.
        // In particular, stable-address decoding must not be assumed to keep
        // the sign/fraction of an arbitrary representation unchanged.
        complete = false;
        break;
      }
      const auto projection = projection_after(ops, read);
      if (!ordinary(ops[read]) || !projection) {
        complete = false;
        break;
      }
      consumers.emplace(read, *projection);
      if (std::none_of(functions.begin(), functions.end(), [&](const Projection& other) {
            return other.same_function(*projection);
          }))
        functions.push_back(*projection);
    }
    if (!complete || updates.empty() || consumers.empty() || values.empty() ||
        values.size() > kMaximumModeValues)
      continue;

    // A deterministic finite work bound, not a time limit. An open recurrence
    // is unsupported, not incorrectly truncated to its first eight values.
    std::deque<std::string> closure(values.begin(), values.end());
    while (complete && !closure.empty()) {
      const auto value = closure.front();
      closure.pop_front();
      for (const auto& [unused_read, update] : updates) {
        (void)unused_read;
        const auto next = update.function.apply(value);
        if (!next || !encode_decimal(*next)) {
          complete = false;
          break;
        }
        if (values.insert(*next).second) {
          if (values.size() > kMaximumModeValues) {
            complete = false;
            break;
          }
          closure.push_back(*next);
        }
      }
    }
    if (!complete)
      continue;
    std::vector<std::vector<std::string>> columns;
    for (const auto& function : functions) {
      std::vector<std::string> column;
      for (const auto& value : values) {
        const auto output = function.apply(value);
        if (!output) {
          complete = false;
          break;
        }
        column.push_back(*output);
      }
      columns.push_back(std::move(column));
    }
    if (!complete)
      continue;
    const auto proposed = synthesize_finite_constant_encodings(columns);
    for (const auto& encoded : proposed.encodings) {
      if (encoded.size() != values.size())
        continue;
      Plan plan;
      plan.reg = reg;
      plan.joint = true;
      plan.synthesized = true;
      plan.value_count = values.size();
      plan.transition_count = updates.size();
      std::map<std::string, std::string> codebook;
      std::size_t row = 0;
      bool valid = true;
      for (const auto& value : values) {
        const auto& word = encoded[row++];
        if (!encode_decimal(word)) {
          valid = false;
          break;
        }
        codebook.emplace(value, word);
        if (!plan.encoding_summary.empty())
          plan.encoding_summary += ", ";
        plan.encoding_summary += value + " -> " + word;
      }
      if (!valid)
        continue;
      for (const auto& [begin, literal] : literals) {
        Macro definition;
        definition.end = literal.end;
        definition.input_literal = true;
        definition.identical_value = literal.value == codebook.at(literal.value);
        definition.replacement_literal = *encode_decimal(codebook.at(literal.value));
        plan.saved_cells += static_cast<int>(literal.end - begin) -
                            static_cast<int>(definition.replacement_literal.size());
        plan.macros.emplace(begin, std::move(definition));
      }
      for (const auto store : stores) {
        if (transitions.contains(store))
          continue;
        Macro definition;
        definition.end = store + 1U;
        definition.input_store = true;
        plan.macros.emplace(store, std::move(definition));
      }
      for (const auto& [read, projection] : consumers) {
        std::vector<std::string> wanted;
        for (const auto& value : values)
          wanted.push_back(*projection.apply(value));
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
        plan.saved_cells += static_cast<int>(projection.end - read - 1U) -
                            static_cast<int>(decoder->code.size());
        plan.macros.emplace(read, std::move(consumer));
      }
      for (const auto& [read, update] : updates) {
        if (!valid)
          break;
        std::vector<std::string> wanted;
        bool identical = true;
        for (const auto& value : values) {
          const auto next = *update.function.apply(value);
          wanted.push_back(codebook.at(next));
          identical = identical && next == codebook.at(next);
        }
        auto available = functions;
        available.push_back(update.function);
        const auto decoder = choose_decoder(encoded, wanted, available);
        if (!decoder) {
          valid = false;
          break;
        }
        Macro transition;
        transition.end = update.store + 1U;
        transition.transition = true;
        transition.identical_value = identical;
        transition.binary_projection = update.function.operand.has_value();
        transition.replacement_projection = decoder->code;
        transition.replacement_binary = decoder->binary;
        plan.saved_cells += static_cast<int>(update.store - read - 1U) -
                            static_cast<int>(decoder->code.size());
        plan.macros.emplace(read, std::move(transition));
      }
      std::size_t last_end = 0;
      for (const auto& [begin, macro] : plan.macros) {
        valid = valid && begin >= last_end;
        last_end = macro.end;
      }
      if (valid && plan.saved_cells >= -candidate_options.maximum_growth_cells &&
          (candidate_options.maximum_growth_cells > 0 || plan.saved_cells > 0))
        plans.push_back(std::move(plan));
    }
  }
  return plans;
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
    const std::vector<std::size_t>& machine, AddressSpaceModel model,
    const FiniteModeCandidateOptions& candidate_options) {
  std::vector<Plan> plans;
  const auto uses = analyze_mode_uses(ops, reg, flow, machine, true);
  if (!uses)
    return plans;
  const auto inputs = literal_inputs(ops, flow, machine, *uses);
  if (inputs.empty())
    return plans;
  auto positions = label_positions(ops);
  if (candidate_options.assumed_target_addresses)
    for (const auto& [label, address] : *candidate_options.assumed_target_addresses)
      positions[label] = address;
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
    std::vector<FiniteWordAddressDomain> address_domains;
    std::vector<std::string> destination_labels;
    if (candidate_options.allow_target_relayout && !flow_reads.empty()) {
      for (const auto& value : values) {
        const auto decoded = evaluate_indirect_address(register_name_for_index(reg), value,
                                                       IndirectOperationKind::Flow, model);
        if (!decoded || !decoded->actual_flow_target ||
            decoded->flow_target != decoded->actual_flow_target ||
            !indirect_writeback_preserves_literal_value(*decoded, value)) {
          complete = false;
          break;
        }
        const int target = *decoded->actual_flow_target;
        std::string identity;
        for (const auto read : flow_reads) {
          bool found = false;
          for (const auto& advertised : *ops[read].meta.indirect_flow_targets) {
            const auto* label = std::get_if<std::string>(&advertised);
            if (label && positions.contains(*label) && positions.at(*label) == target) {
              found = true;
              if (identity.empty())
                identity = *label;
            }
          }
          complete = complete && found;
        }
        if (!complete || identity.empty())
          break;
        address_domains.push_back({std::to_string(target), register_name_for_index(reg),
                                   {target}, model});
        destination_labels.push_back(identity);
      }
      if (!complete || address_domains.size() != values.size())
        continue;
    }
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
    std::size_t address_synthesis_rounds = 0;
    const auto append_address_solutions = [&](const std::vector<FiniteWordAddressDomain>& domains) {
      if (domains.empty() || address_synthesis_rounds >= 3U)
        return;
      ++address_synthesis_rounds;
      const auto synthesized = synthesize_multirole_words(
          consumer_values, domains, {.maximum_work = 4096, .maximum_candidates = 16});
      for (const auto& proposal : synthesized.encodings)
        if (encodings.size() < 64U &&
            std::find(encodings.begin(), encodings.end(), proposal.words) == encodings.end())
          encodings.push_back(proposal.words);
    };
    if (complete)
      append_address_solutions(address_domains);
    for (std::size_t encoding_index = 0; encoding_index < encodings.size(); ++encoding_index) {
      // Layout-guided synthesis may append alternatives; do not retain a
      // reference into the proposal vector across that operation.
      const auto encoded = encodings[encoding_index];
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
      if (valid && !address_domains.empty() && address_synthesis_rounds < 3U) {
        // The shorter decoder/literal geometry proposes new physical entries.
        // Solve the data and address equations again, rather than freezing the
        // original digits or blindly patching an address-shaped data constant.
        std::map<std::string, int> proposed_positions;
        int at = 0;
        for (std::size_t i = 0; i < ops.size();) {
          const auto macro = plan.macros.find(i);
          if (macro != plan.macros.end()) {
            const auto& replacement = macro->second;
            if (replacement.definition || replacement.input_literal)
              at += static_cast<int>(replacement.replacement_literal.size()) +
                    (replacement.definition ? 1 : 0);
            else
              at += cells_per_op(ops[i]) +
                    static_cast<int>(replacement.replacement_projection.size());
            i = replacement.end;
          } else {
            if (ops[i].kind == IrKind::Label)
              proposed_positions.emplace(ops[i].name, at);
            at += cells_per_op(ops[i++]);
          }
        }
        auto domains = address_domains;
        bool supported = true;
        for (std::size_t row_index = 0; row_index < domains.size(); ++row_index) {
          const auto position = proposed_positions.find(destination_labels[row_index]);
          if (position == proposed_positions.end() || position->second < 0 || position->second > 99) {
            supported = false;
            break;
          }
          domains[row_index].targets = {position->second};
        }
        if (supported)
          append_address_solutions(domains);
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
          if (!candidate_options.allow_target_relayout)
            plan.fixed_targets.emplace(*label, positions.at(*label));
        }
        for (const auto& [original, replacement] : codebook) {
          const auto before = evaluate_indirect_address(register_name_for_index(reg), original,
                                                        IndirectOperationKind::Flow, model);
          const auto after = evaluate_indirect_address(register_name_for_index(reg), replacement,
                                                       IndirectOperationKind::Flow, model);
          if (!before || !after || !before->actual_flow_target || !after->actual_flow_target ||
              (!candidate_options.allow_target_relayout &&
               before->actual_flow_target != after->actual_flow_target) ||
              before->flow_target != before->actual_flow_target ||
              after->flow_target != after->actual_flow_target ||
              !declared.contains(*before->actual_flow_target) ||
              !indirect_writeback_preserves_literal_value(*before, original) ||
              !indirect_writeback_preserves_literal_value(*after, replacement)) {
            valid = false;
            break;
          }
          for (const auto& target : *ops[read].meta.indirect_flow_targets) {
            const auto& label = std::get<std::string>(target);
            if (positions.at(label) != *before->actual_flow_target)
              continue;
            const auto [binding, inserted] =
                plan.fixed_targets.emplace(label, *after->actual_flow_target);
            if (!inserted && binding->second != *after->actual_flow_target)
              valid = false;
          }
        }
        plan.flow_reads.insert(machine[read]);
      }
      std::size_t end = 0;
      for (const auto& [begin, macro] : plan.macros) {
        valid = valid && begin >= end;
        end = macro.end;
      }
      if (valid && plan.saved_cells >= -candidate_options.maximum_growth_cells &&
          (candidate_options.maximum_growth_cells > 0 || plan.saved_cells > 0))
        plans.push_back(std::move(plan));
    }
  }
  return plans;
}

bool symbolic(const std::vector<IrOp>& ops, bool preserve_compiler_selector_charges) {
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
      if ((!preserve_compiler_selector_charges && role.starts_with("late-decimal-selector-")) ||
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
  bool coefficient_ready = false;
  // A dead private word is physically unequal until its next write.
  bool retired = false;

  int key() const {
    return stack_value_equality_key(stack) | (initialized ? 64 : 0) |
           (entry ? 128 : 0) | (encoded_input ? 256 : 0) |
           (coefficient_ready ? 512 : 0) | (retired ? 1024 : 0);
  }
};

bool prove(const std::vector<MachineItem>& items,
           const AuthoritativePostLayoutControlFlow& flow,
           const std::vector<std::size_t>& machine, Plan& plan,
           const std::vector<bool>& automatic_entry_lift,
           std::string& reason,
           const std::vector<std::size_t>* roots = nullptr) {
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
    if (plan.coefficient_reg) {
      const int coefficient = *plan.coefficient_reg;
      if (family >= 0x70 && family <= 0xe0 && (item.opcode & 15) == coefficient)
        return fail("coefficient cache is also an indirect selector");
      if (coefficient < 4 && item.opcode == loops[static_cast<std::size_t>(coefficient)])
        return fail("coefficient cache is a destructive loop counter");
      if (family == 0xb0 || family == 0xd0) {
        const auto targets = flow.indirect_memory_targets.find(i);
        if (targets == flow.indirect_memory_targets.end() ||
            std::find(targets->second.begin(), targets->second.end(), coefficient) !=
                targets->second.end())
          return fail("coefficient cache has an indirect memory alias");
      }
    }
    if (item.opcode == 0x4f || item.opcode == 0x6f)
      return fail("extended register alias is outside this proof");
  }

  std::vector<std::set<int>> seen(flow.execution_states.size());
  std::deque<std::pair<std::size_t, Equality>> work;
  const auto enqueue = [&](std::size_t index, const Equality& state) {
    if (seen[index].insert(state.key()).second)
      work.emplace_back(index, state);
  };
  if (roots) {
    for (const auto root : *roots)
      enqueue(root, Equality{});
  } else {
    for (std::size_t i = 0; i < flow.execution_states.size(); ++i)
      if (external_items.contains(flow.execution_states[i].item_index))
        enqueue(i, Equality{});
  }
  if (work.empty())
    return fail("no authoritative external entry");
  std::set<std::size_t> reached_macros;
  std::set<std::size_t> retired_manual_items;
  std::map<std::size_t, const ExitRecovery*> recovery_items;
  for (const auto& [begin, recovery] : plan.exit_recoveries)
    recovery_items.emplace(machine[begin], &recovery);
  std::size_t processed = 0;
  while (!work.empty()) {
    if (++processed > kMaximumProofStates)
      return fail("finite product proof budget exhausted");
    auto [index, state] = work.front();
    work.pop_front();
    const auto item_index = flow.execution_states[index].item_index;
    const auto& item = items[item_index];
    if (const auto recovery = recovery_items.find(item_index);
        recovery != recovery_items.end()) {
      if (!roots || !state.initialized || state.entry)
        return fail("exit recovery requires a proved finite epoch and closed numeric entry");
      const auto kind = recovery->second->kind;
      if (kind == ExitRecoveryKind::UnaryObservation) {
        // Decode only the predicate's recalled X. The carrier is STILL encoded:
        // a later read, input or exit must prove its own representation relation.
        // Equal numerical output does not restore last-X or the entry shadow.
        for (const int code : recovery->second->code) {
          if (opcode_saves_physical_x1(code)) state.stack.x1_equal = false;
          if (opcode_by_code(code).x2_effect != X2Effect::Preserves)
            state.stack.x2_equal = false;
        }
        state.stack.stack_equal[0] = true;
      } else if (kind == ExitRecoveryKind::LiteralRematerialization) {
        // Every incoming finite-row context independently certifies this
        // exact original integer and fresh entry. Only the register relation
        // is restored. Do not identify the newly entered X with the original
        // stack or claim that the pushed values/entry shadow are unobserved.
        state.stack.stack_equal = {false, false, false, false};
        state.stack.x1_equal = false;
        state.stack.x2_equal = false;
      } else {
        // The independent finite-epoch graph proves an immediate true
        // predicate fallthrough with the encoded word still in X. Exponent
        // entry preserves Y/Z/T and physical last-X, but not necessarily X2.
        state.stack.stack_equal[0] = true;
        state.stack.x2_equal = false;
      }
      if (kind != ExitRecoveryKind::ExponentObservation &&
          kind != ExitRecoveryKind::UnaryObservation) {
        state.initialized = false;
        state.coefficient_ready = false;
      }
      state.encoded_input = false;
    }
    if (const auto found = starts.find(item_index); found != starts.end()) {
      const auto& macro = *found->second;
      if (state.retired && !macro.definition && !macro.input_literal && !macro.input_store)
        return fail("retired carrier is read before its proved overwrite");
      reached_macros.insert(item_index);
      const auto old = state.stack.stack_equal;
      if (macro.unit_literal_elision) {
        state.stack.stack_equal.fill(false);
        state.stack.x2_equal = false;
        state.encoded_input = true;
      } else if (macro.unit_store) {
        if (!state.encoded_input) return fail("unit normalizer has an unproved input");
        state.stack.stack_equal[0] = true;
        state.stack.x1_equal = false;
        state.initialized = false;
        state.retired = false;
        state.encoded_input = false;
      } else if (macro.predicate_observation) {
        // Only the decision is equal in an encoded epoch. In particular, do
        // not pretend the recalled word, last-X or entry shadow were decoded.
        // In an ordinary epoch the unchanged instructions remain identical.
        state.stack.stack_equal = {!state.initialized, old[0], old[1], old[2]};
        state.stack.x2_equal = old[0];
        if (!stack_values_fully_equal(state.stack))
          state.stack.x2_equal = false;
        state.encoded_input = false;
      } else if (macro.normalizing_max) {
        if (!old[0])
          return fail("max epoch boundary has unequal dynamic operands");
        // In an encoded epoch the independent range certificate proves Y
        // absorbs both carrier spellings. In an ordinary epoch both inputs
        // already agree. Kmax keeps Y; it does not pop the stack like +.
        state.stack.stack_equal = {true, old[0], old[1], old[2]};
        state.stack.x1_equal = !state.initialized;
        state.stack.x2_equal = old[0]; // recall saves old X; Kmax preserves X2
        state.initialized = false;
        state.coefficient_ready = false;
        state.encoded_input = false;
      } else if (macro.inline_product) {
        if (!state.initialized || !old[0])
          return fail("inline coefficient has an unproved state or unequal left operand");
        if (macro.binary_projection && macro.replacement_binary)
          state.stack.stack_equal = {true, old[1], old[1], old[1]};
        else if (!macro.binary_projection && !macro.replacement_binary)
          state.stack.stack_equal = {true, old[1], old[2], old[2]};
        else
          state.stack.stack_equal = {true, old[1], false, false};
        // Both multiplies save the equal ordinary coefficient in last-X.
        // A raw coefficient has only an equal product, not an equal last-X.
        state.stack.x1_equal = !plan.raw_coefficient;
        state.stack.x2_equal = false;
        state.encoded_input = false;
      } else if (macro.coefficient_definition) {
        if (!state.initialized)
          return fail("coefficient projection has an unproved state input");
        state.stack.stack_equal = {!plan.raw_coefficient, old[0], old[1], false};
        state.stack.x1_equal = false;
        state.stack.x2_equal = false;
        state.encoded_input = false;
        state.coefficient_ready = true;
      } else if (macro.coefficient_product) {
        if (!state.initialized || !state.coefficient_ready || !old[0])
          return fail("coefficient read has a stale state version or unequal multiplicand");
        // Value equality uses either the identical ordinary coefficient or
        // the exhaustive, independently certified raw right-product domain.
        state.stack.stack_equal = {true, old[1], old[2], old[2]};
        state.stack.x1_equal = false;
        state.stack.x2_equal = false;
        state.encoded_input = false;
      } else if (macro.state_identity) {
        if (!state.initialized)
          return fail("state decoder has an unproved finite input");
        state.stack.stack_equal = {true, old[0], old[1], old[2]};
        state.stack.x1_equal = false;
        state.stack.x2_equal = false;
        state.encoded_input = false;
      } else if (macro.transition) {
        state.coefficient_ready = false;
        if (!state.initialized)
          return fail("transition has an entry-live or unproved mode definition");
        // The independently checked full transition table preserves the
        // relation between the two carrier values, not their bit patterns.
        state.stack.stack_equal = {macro.identical_value, old[0], old[1],
            macro.binary_projection == macro.replacement_binary
                ? old[macro.binary_projection ? 1U : 2U] : false};
        state.stack.x1_equal = false;
        state.stack.x2_equal = false;
        state.encoded_input = false;
      } else if (macro.input_store) {
        state.coefficient_ready = false;
        if (!state.encoded_input)
          return fail("callee store has no proved encoded literal input");
        state.initialized = true;
        state.retired = false;
      } else if (macro.definition || macro.input_literal) {
        state.coefficient_ready = false;
        if (plan.raw_coefficient)
          state.stack.x1_equal = false;
        state.stack.stack_equal = {macro.identical_value, old[0], old[1], old[2]};
        state.stack.x2_equal = macro.identical_value;
        if (macro.definition) {
          state.initialized = true;
          state.retired = false;
        }
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
      if (state.retired && family >= 0x70 && family <= 0xe0 &&
          (opcode & 15) == plan.reg)
        return fail("retired carrier is observed as an indirect selector");
      if (selector && (opcode & 15) == plan.reg && state.initialized &&
          !plan.flow_reads.contains(item_index))
        return fail("indirect consumer has no joint encoding/address certificate");
      const bool transports_input = (opcode >= 0x40 && opcode <= 0x4e) ||
          opcode == 0x51 || opcode == 0x52 || opcode == 0x53 || opcode == 0x54 ||
          family == 0x80 || family == 0xa0;
      if (!transports_input)
        state.encoded_input = false;
      if (item.manual_interaction) {
        if (!stack_values_fully_equal(state.stack))
          return fail("manual input observes unequal stack/X1/X2 state");
        if (state.initialized) {
          if (!plan.dead_manual_items.contains(item_index))
            return fail("manual input crosses a live encoded lifetime");
          // End the representation relation, not the physical difference.
          state.initialized = false;
          state.retired = true;
          state.coefficient_ready = false;
          state.encoded_input = false;
          retired_manual_items.insert(item_index);
        }
        if (state.retired && !plan.dead_manual_items.contains(item_index))
          return fail("retired carrier crosses an unproved input continuation");
      }
      if (opcode == 0x60 + plan.reg && (state.initialized || state.retired))
        return fail("unprojected read observes an encoded lifetime");
      if (opcode == 0x40 + plan.reg) {
        if (!state.stack.stack_equal[0])
          return fail("ordinary definition observes an encoded stack value");
        state.initialized = false;
        state.retired = false;
        state.coefficient_ready = false;
      }
      if (opcode == 0x50) {
        if (state.retired && !plan.dead_manual_items.contains(item_index))
          return fail("retired carrier reaches an untyped or terminal stop");
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
        // FL tests its own equal counter, not X. The carrier/counter alias
        // was independently excluded before entering the product proof.
        const bool counter_condition = opcode == 0x5d || opcode == 0x5b ||
                                       opcode == 0x58 || opcode == 0x5a;
        if (!unconditional && !counter_condition && !state.stack.stack_equal[0])
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
    if (flow.execution_successors[index].empty() &&
        (!stack_values_fully_equal(state.stack) || state.retired))
      return fail("unproved exit with changed hidden state or an unclosed register lifetime");
    for (const auto successor : flow.execution_successors[index])
      enqueue(successor, state);
  }
  if (reached_macros.size() != starts.size())
    return fail("unreachable definitions or consumers lack a representation proof");
  plan.retired_manual_items = std::move(retired_manual_items);
  return true;
}

// A conditional data-proof product, never a replacement for the relocation
// CFG. Keep finite rows, certified positive epochs, unknown ordinary values,
// and pending literal arguments separate in each exact return context.
struct FiniteEpochGraph {
  AuthoritativePostLayoutControlFlow flow;
  std::vector<bool> entry_lift;
  std::vector<std::size_t> roots;
  std::vector<int> rows;
  std::size_t pruned_edges = 0;
};

std::optional<FiniteEpochGraph> finite_epoch_graph(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<std::size_t>& machine, const Plan& plan,
    const std::vector<bool>& entry_lift) {
  constexpr int unknown = -1;
  constexpr int positive = -2;
  const auto count = flow.execution_states.size();
  if (plan.raw_coefficient || plan.finite_values.empty() ||
      flow.execution_edges.size() != count || entry_lift.size() != count)
    return std::nullopt;
  std::map<std::size_t, const Macro*> ends;
  std::set<std::size_t> macro_items, entries;
  for (const auto& [begin, macro] : plan.macros) {
    ends.emplace(machine[macro.end - 1U], &macro);
    for (auto i = begin; i < macro.end; ++i)
      macro_items.insert(machine[i]);
  }
  for (const auto& entry : flow.external_entries)
    entries.insert(entry.entry.item_index);

  FiniteEpochGraph result;
  result.flow = flow;
  result.flow.execution_states.clear();
  result.flow.execution_successors.clear();
  result.flow.execution_edges.clear();
  using State = std::tuple<std::size_t, int, int>;
  std::map<State, std::size_t> identities;
  std::vector<State> pending;
  const auto add = [&](const State& state) {
    const auto [found, fresh] = identities.emplace(state, pending.size());
    if (fresh) {
      const auto original = std::get<0>(state);
      pending.push_back(state);
      result.flow.execution_states.push_back(flow.execution_states[original]);
      result.flow.execution_successors.emplace_back();
      result.flow.execution_edges.emplace_back();
      result.entry_lift.push_back(entry_lift[original]);
      result.rows.push_back(std::get<1>(state));
    }
    return found->second;
  };
  for (std::size_t i = 0; i < count; ++i)
    if (entries.contains(flow.execution_states[i].item_index))
      result.roots.push_back(add({i, unknown, unknown}));
  if (result.roots.empty())
    return std::nullopt;
  for (std::size_t index = 0; index < pending.size(); ++index) {
    if (pending.size() > kMaximumProofStates)
      return std::nullopt; // Never export a partially explored product.
    auto [original, row, input] = pending[index];
    const auto item_index = flow.execution_states[original].item_index;
    const auto& item = items[item_index];
    std::optional<bool> predicate;
    if (const auto end = ends.find(item_index); end != ends.end()) {
      const auto& macro = *end->second;
      if (macro.input_literal || macro.definition) {
        input = macro.finite_input_row.value_or(unknown);
        if (macro.definition)
          row = input;
      } else if (macro.input_store) {
        row = input;
      } else if (macro.transition) {
        row = row >= 0 && static_cast<std::size_t>(row) < macro.finite_transition_rows.size()
            ? macro.finite_transition_rows[static_cast<std::size_t>(row)] : unknown;
        input = unknown;
      } else if (macro.normalizing_max) {
        // A positive absorbing operand does not prove positivity if the old
        // ordinary carrier was unknown (it might be raw or hardware zero).
        row = row >= 0 || row == positive ? positive : unknown;
        input = unknown;
      } else {
        input = unknown;
      }
      if (macro.predicate_observation && row != unknown)
        predicate = ordinary_predicate(item.opcode,
            row == positive ? "1" : plan.finite_values[static_cast<std::size_t>(row)]);
    } else if (!macro_items.contains(item_index)) {
      const int code = item.opcode, family = code & 0xf0;
      const bool transports = (code >= 0x40 && code <= 0x4e) ||
          code == 0x51 || code == 0x52 || code == 0x53 || code == 0x54 ||
          family == 0x80 || family == 0xa0;
      if (!transports)
        input = unknown;
      if (code == 0x40 + plan.reg)
        row = unknown;
      if (family == 0xb0) {
        const auto targets = flow.indirect_memory_targets.find(item_index);
        if (targets == flow.indirect_memory_targets.end() ||
            std::find(targets->second.begin(), targets->second.end(), plan.reg) != targets->second.end())
          row = unknown;
      }
      if (item.manual_interaction)
        row = input = unknown;
    }
    for (auto edge : flow.execution_edges[original]) {
      if (predicate) {
        if (edge.kind != PostLayoutExecutionEdgeKind::Fallthrough &&
            edge.kind != PostLayoutExecutionEdgeKind::DirectTarget)
          return std::nullopt;
        // True skips the address operand; false jumps to that address.
        if ((edge.kind == PostLayoutExecutionEdgeKind::Fallthrough) != *predicate) {
          ++result.pruned_edges;
          continue;
        }
      }
      edge.target_state = add({edge.target_state, row, input});
      result.flow.execution_successors[index].push_back(edge.target_state);
      result.flow.execution_edges[index].push_back(std::move(edge));
    }
  }
  return result;
}

// ROM-backed decoder after a true recall/predicate fallthrough. Admit only
// exact positive exponent adjustments to nonzero ordinary integer rows.
// Fractional destination formatting and taken-branch entry shadows are not
// interchangeable with this contract. No arithmetic may clobber last-X.
std::optional<std::vector<int>> exponent_exit_decoder(
    const Plan& plan, const std::set<int>& rows) {
  std::optional<int> adjustment;
  for (const int row : rows) {
    const auto before = exact_decimal(plan.finite_encoding[static_cast<std::size_t>(row)]);
    const auto wanted = exact_decimal(plan.finite_values[static_cast<std::size_t>(row)]);
    if (!before || !wanted || before->numerator == 0 ||
        wanted->numerator == 0 || wanted->denominator != 1 ||
        (before->numerator < 0) != (wanted->numerator < 0))
      return std::nullopt;
    auto significant = [](std::int64_t n) {
      while (n != 0 && n % 10 == 0)
        n /= 10;
      return n;
    };
    const int shift = wanted->high - before->high;
    if (significant(before->numerator) != significant(wanted->numerator) ||
        shift < 1 || shift > 7 || (adjustment && *adjustment != shift))
      return std::nullopt;
    adjustment = shift;
  }
  if (!adjustment)
    return std::nullopt;
  return std::vector<int>{0x0c, *adjustment, 0x40 + plan.reg};
}

std::map<std::size_t, ExitRecovery> finite_exit_recoveries(
    const std::vector<IrOp>& ops, const std::vector<std::size_t>& machine,
    const Plan& plan, const FiniteEpochGraph& graph,
    std::vector<std::pair<std::size_t, ExitRecovery>>* observations = nullptr) {
  std::set<std::size_t> covered, predicate_ends;
  for (const auto& [begin, macro] : plan.macros) {
    for (auto i = begin; i < macro.end; ++i)
      covered.insert(machine[i]);
    if (macro.predicate_observation)
      predicate_ends.insert(machine[macro.end - 1U]);
  }
  std::map<std::size_t, std::set<int>> rows;
  std::set<std::size_t> rejected, literal_rejected;
  for (std::size_t state = 0; state < graph.rows.size(); ++state)
    if (graph.rows[state] < 0 || !graph.entry_lift[state])
      literal_rejected.insert(graph.flow.execution_states[state].item_index);
  for (const auto root : graph.roots)
    literal_rejected.insert(graph.flow.execution_states[root].item_index);
  for (const auto root : graph.roots)
    rejected.insert(graph.flow.execution_states[root].item_index);
  for (std::size_t state = 0; state < graph.rows.size(); ++state) {
    const auto item = graph.flow.execution_states[state].item_index;
    if (graph.rows[state] < 0)
      rejected.insert(item);
    else
      rows[item].insert(graph.rows[state]);
    for (const auto& edge : graph.flow.execution_edges[state]) {
      const auto next = graph.flow.execution_states[edge.target_state].item_index;
      // VP after a taken branch can edit a stale mantissa. An extra store
      // is not a universal repair for that entry context either.
      if (!predicate_ends.contains(item) ||
          edge.kind != PostLayoutExecutionEdgeKind::Fallthrough)
        rejected.insert(next);
    }
  }
  std::map<std::size_t, ExitRecovery> result;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    if (ops[i].kind == IrKind::Label || !ordinary(ops[i]) ||
        covered.contains(machine[i]) || !rows.contains(machine[i]))
      continue;
    if (!rejected.contains(machine[i])) {
      const auto& admitted = rows.at(machine[i]);
      if (auto code = exponent_exit_decoder(plan, admitted)) {
        // Removing the recovery store also removes the entry closer. Without
        // an original store here a following digit would extend the exponent,
        // not start a fresh mantissa. This is not numeric-value equivalence.
        if (observations && ops[i].kind == IrKind::Store) {
          auto value_code = *code;
          value_code.pop_back(); // no implicit permission to change the carrier
          observations->emplace_back(i, ExitRecovery{
              ExitRecoveryKind::ExponentObservation, std::move(value_code)});
        }
        result.emplace(i, ExitRecovery{ExitRecoveryKind::ExponentAdjustment, std::move(*code)});
      }
      if (observations) {
        // A finite truth table certifies only the decoded value. The product
        // proof below separately checks every changed stack/X1/X2 continuation.
        for (const int code : {0x0b, 0x32, 0x34, 0x35}) {
          const bool exact = std::all_of(admitted.begin(), admitted.end(), [&](int row) {
            const auto at = static_cast<std::size_t>(row);
            const auto value = exact_projection(code, plan.finite_encoding.at(at), std::nullopt);
            return value && *value == plan.finite_values.at(at);
          });
          if (exact)
            observations->emplace_back(i, ExitRecovery{ExitRecoveryKind::UnaryObservation, {code}});
        }
      }
    }

    // An opaque arithmetic boundary may be preceded by a singleton finite
    // epoch even when no numeric score bound exists. Restore that original
    // integer at a common fresh-entry point, before the dynamic web starts.
    // Unknown/positive epochs, different incoming rows and external entries
    // cannot supply a literal. This is an enabling candidate, not a DSE hint:
    // its full cost and changed stack still have to pass the product proof.
    const auto& at_rows = rows.at(machine[i]);
    if (plan.opaque_max_count == 0 || result.contains(i) ||
        literal_rejected.contains(machine[i]) || at_rows.size() != 1U)
      continue;
    const auto& word = plan.finite_values.at(static_cast<std::size_t>(*at_rows.begin()));
    const auto value = exact_decimal(word);
    if (value && value->denominator == 1 && value->numerator != 0)
      if (auto code = encode_decimal(word)) {
        code->push_back(0x40 + plan.reg);
        result.emplace(i, ExitRecovery{ExitRecoveryKind::LiteralRematerialization,
                                      std::move(*code)});
      }
  }
  return result;
}

bool prove_with_finite_exits(
    const std::vector<IrOp>& ops, const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<std::size_t>& machine, Plan& plan,
    const std::vector<bool>& entry_lift, int maximum_growth,
    std::string& reason) {
  if (!plan.inline_coefficient ||
      (plan.predicate_observation_count == 0 && plan.opaque_max_count == 0))
    return false;
  const auto graph = finite_epoch_graph(items, flow, machine, plan, entry_lift);
  if (!graph)
    return false;
  if (prove(items, graph->flow, machine, plan, graph->entry_lift, reason, &graph->roots)) {
    plan.epoch_pruned_edges = graph->pruned_edges;
    return true;
  }
  std::vector<std::pair<std::size_t, ExitRecovery>> observations;
  const auto exits = finite_exit_recoveries(ops, machine, plan, *graph, &observations);
  using Site = std::pair<std::size_t, ExitRecovery>;
  using Choice = std::pair<int, std::vector<std::size_t>>;
  const std::vector<Site> legacy(exits.begin(), exits.end());
  std::optional<Plan> best;
  const auto search_recoveries = [&](const std::vector<Site>& sites) {
    // Keep the historical restoration lane and its complete budget independent.
    // A new, cheaper decoder must not starve an already proved restoration.
    std::set<Choice> frontier;
    for (std::size_t i = 0; i < sites.size(); ++i)
      frontier.emplace(static_cast<int>(sites[i].second.code.size()), std::vector<std::size_t>{i});
    for (int attempt = 0; !frontier.empty() && attempt < 32; ++attempt) {
      const auto [cost, selected] = *frontier.begin();
      frontier.erase(frontier.begin());
      if (plan.saved_cells - cost < -maximum_growth ||
          (maximum_growth == 0 && plan.saved_cells - cost <= 0) ||
          (best && plan.saved_cells - cost <= best->saved_cells))
        break;
      Plan candidate = plan;
      candidate.saved_cells -= cost;
      bool distinct = true;
      for (const auto site : selected)
        distinct = candidate.exit_recoveries.emplace(sites[site]).second && distinct;
      if (distinct && prove(items, graph->flow, machine, candidate, graph->entry_lift,
                            reason, &graph->roots)) {
        candidate.epoch_pruned_edges = graph->pruned_edges;
        best = std::move(candidate);
        break; // Frontier cost is monotone; equal cost retains the earlier proof.
      }
      for (auto next = selected.back() + 1U; next < sites.size(); ++next) {
        if (std::any_of(selected.begin(), selected.end(), [&](auto old) {
              return sites[old].first == sites[next].first;
            }))
          continue;
        auto extended = selected;
        extended.push_back(next);
        frontier.emplace(cost + static_cast<int>(sites[next].second.code.size()), std::move(extended));
      }
    }
  };
  search_recoveries(legacy);
  if (!observations.empty()) {
    observations.insert(observations.end(), legacy.begin(), legacy.end());
    search_recoveries(observations);
  }
  if (!best) return false;
  plan = std::move(*best);
  return true;
}

} // namespace

static std::vector<DeferredFiniteModeRepresentation> finite_mode_candidates_impl(
    const std::vector<IrOp>& ops, const PassContext& context,
    const FiniteModeCandidateOptions& candidate_options, bool defer_layout, bool units_only = false) {
  const auto unchanged = [] { return std::vector<DeferredFiniteModeRepresentation>{}; };
  std::vector<DeferredFiniteModeRepresentation> alternatives;
  if (candidate_options.assumed_target_addresses) {
    const auto positions = label_positions(ops);
    for (const auto& [label, address] : *candidate_options.assumed_target_addresses)
      if (address < 0 || !positions.contains(label))
        return unchanged();
  }
  if (candidate_options.maximum_candidates == 0 ||
      candidate_options.maximum_growth_cells < 0 || candidate_options.maximum_growth_cells > 4)
    return unchanged();
  // This is part of the separately regenerated shared-region candidate. The
  // ordinary lowering remains an incumbent: a local win can lose address
  // coincidences, so final layout, not this pass, chooses the program.
  if (!context.options.defer_return_suffix_until_callee_hole ||
      !symbolic(ops, candidate_options.preserve_compiler_selector_charges)) {
    if (std::getenv("MKPRO_NATIVE_TRACE_FINITE_MODE_REPRESENTATION"))
      std::cerr << "[finite-mode-discovery] unsupported IR/phase; late-context="
                << candidate_options.preserve_compiler_selector_charges << '\n';
    return unchanged();
  }
  // Avoid constructing the context graph for programs with no projected reads.
  bool has_projection = false;
  for (std::size_t i = 0; i < ops.size() && !has_projection; ++i)
    has_projection = ops[i].kind == IrKind::Recall && projection_after(ops, i).has_value();
  const bool has_unit_inputs = candidate_options.runtime_preloads && std::any_of(ops.begin(),ops.end(),[](const auto& op){return op.kind==IrKind::Plain && op.opcode==1;});
  if (!has_projection && !units_only && !has_unit_inputs)
    return unchanged();
  const auto items = lower_ir_to_machine(ops);
  PostLayoutControlFlowOptions options;
  options.address_space_model = address_space_model_for_feature_profile(
      effective_optimizer_feature_profile(context.options));
  if (candidate_options.preserve_compiler_selector_charges)
    options.empty_return_target = 1;
  auto flow = build_post_layout_control_flow(items, options);
  if (!flow.proved || flow.execution_states.size() != flow.execution_successors.size()) {
    if (std::getenv("MKPRO_NATIVE_TRACE_FINITE_MODE_REPRESENTATION"))
      for (const auto& reason : flow.reasons)
        std::cerr << "[finite-mode-discovery] CFG: " << reason << '\n';
    return unchanged();
  }
  // Use the complete graph before conditional pruning. Intersect all exact
  // caller contexts: one invocation cannot authorize a different invocation.
  std::map<std::size_t, RegisterValueSet> overwritten_before_manual;
  if (std::any_of(items.begin(), items.end(), [](const auto& item) {
        return item.manual_interaction.has_value();
      })) {
    const auto deaths = prove_post_layout_register_deadness(ops, flow, kMaximumProofStates);
    if (deaths.proved)
      for (std::size_t state = 0; state < flow.execution_states.size(); ++state) {
        const auto at = flow.execution_states[state].item_index;
        if (!items[at].manual_interaction)
          continue;
        const auto& values = deaths.before_state[state];
        const auto [found, fresh] = overwritten_before_manual.emplace(at, values);
        if (!fresh) {
          RegisterValueSet common;
          std::set_intersection(found->second.begin(), found->second.end(),
                                values.begin(), values.end(),
                                std::inserter(common, common.end()));
          found->second = std::move(common);
        }
      }
  }
  // This product is a conditional data-proof view, never the relocation CFG.
  // Physical positions may select callbacks only when all assumed bindings
  // are already closed. Otherwise use the separately proved encoded origins;
  // a missing/colliding origin cannot turn an unselected callback into fact.
  const auto current_positions = label_positions(ops);
  const bool closed_geometry = !candidate_options.assumed_target_addresses ||
      std::all_of(candidate_options.assumed_target_addresses->begin(),
                  candidate_options.assumed_target_addresses->end(),
          [&](const auto& binding) { return current_positions.at(binding.first) == binding.second; });
  const auto selector_graph = closed_geometry
      ? build_selector_consistent_execution_graph(items, flow)
      : build_deferred_selector_consistent_execution_graph(
            items, flow, *candidate_options.assumed_target_addresses).execution;
  if (selector_graph.proved && selector_graph.pruned_edges != 0) {
    const auto original = flow.execution_states;
    flow.execution_states.clear();
    for (const auto state : selector_graph.original_states)
      flow.execution_states.push_back(original[state]);
    flow.execution_successors = selector_graph.successors;
    flow.execution_edges = selector_graph.execution_edges;
  }
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
    if (candidate_options.runtime_preloads && defer_layout && candidate_options.normalize_unit_arguments) {
      auto units = discover_unit_input_normalization(ops, reg, flow, machine, *automatic_entry_lift, candidate_options);
      plans.insert(plans.end(), std::make_move_iterator(units.begin()), std::make_move_iterator(units.end()));
    }
    if (units_only) continue;
    if (auto plan = discover(ops, reg, flow, machine))
      plans.push_back(std::move(*plan));
    auto joint = discover_joint(ops, reg, flow, machine, options.address_space_model, candidate_options);
    plans.insert(plans.end(), std::make_move_iterator(joint.begin()),
                 std::make_move_iterator(joint.end()));
    auto carriers = discover_coefficient_carriers(
        ops, reg, flow, machine, *automatic_entry_lift, candidate_options);
    plans.insert(plans.end(), std::make_move_iterator(carriers.begin()),
                 std::make_move_iterator(carriers.end()));
    auto inline_carriers = discover_inline_coefficient_carriers(
        ops, reg, flow, machine, *automatic_entry_lift, candidate_options);
    plans.insert(plans.end(), std::make_move_iterator(inline_carriers.begin()),
                 std::make_move_iterator(inline_carriers.end()));
    auto state_machines = discover_state_machine(ops, reg, flow, machine, candidate_options);
    plans.insert(plans.end(), std::make_move_iterator(state_machines.begin()),
                 std::make_move_iterator(state_machines.end()));
  }
  std::stable_sort(plans.begin(), plans.end(), [](const Plan& a, const Plan& b) {
    return a.saved_cells > b.saved_cells;
  });
  std::vector<Plan> proved_plans;
  for (auto plan : plans) {
    for (const auto& [at, dead] : overwritten_before_manual)
      if (dead.contains(register_name_for_index(plan.reg)))
        plan.dead_manual_items.insert(at);
    std::string reason;
    const bool accepted = prove(items, flow, machine, plan, *automatic_entry_lift, reason) ||
        prove_with_finite_exits(ops, items, flow, machine, plan, *automatic_entry_lift,
                               candidate_options.maximum_growth_cells, reason);
    if (std::getenv("MKPRO_NATIVE_TRACE_FINITE_MODE_REPRESENTATION") != nullptr)
      std::cerr << "[finite-mode-representation] R" << register_name_for_index(plan.reg)
                << " values=" << plan.value_count << " local-saving=" << plan.saved_cells
                << " proof=" << (accepted ? "accepted" : reason) << '\n';
    if (accepted)
      proved_plans.push_back(std::move(plan));
  }
  // Recovery is executable code, not a free proof hint. Compare complete
  // local costs before limiting the number of published alternatives.
  std::stable_sort(proved_plans.begin(), proved_plans.end(), [](const Plan& a, const Plan& b) {
    return a.saved_cells > b.saved_cells;
  });
  for (const auto& plan : proved_plans) {
    std::vector<IrOp> rewritten;
    for (std::size_t i = 0; i < ops.size();) {
      if (const auto recovery = plan.exit_recoveries.find(i);
          recovery != plan.exit_recoveries.end()) {
        for (const int opcode : recovery->second.code) {
          IrOp restored;
          restored.kind = opcode == 0x40 + plan.reg ? IrKind::Store : IrKind::Plain;
          if (restored.kind == IrKind::Store)
            restored.register_name = register_name_for_index(plan.reg);
          restored.opcode = opcode;
          restored.meta.mnemonic = opcode_by_code(opcode).name;
          restored.meta.comment = "finite epoch exit recovery";
          restored.meta.source_line = ops[i].meta.source_line;
          rewritten.push_back(std::move(restored));
        }
      }
      const auto found = plan.macros.find(i);
      if (found == plan.macros.end()) {
        rewritten.push_back(ops[i++]);
        continue;
      }
      const auto& macro = found->second;
      if (macro.unit_store) {
        IrOp sign; sign.kind = IrKind::Plain; sign.opcode = 0x32;
        sign.meta.mnemonic = opcode_by_code(0x32).name;
        sign.meta.comment = "shared unit argument normalization";
        sign.meta.roles.push_back(std::string(kUnitProjectionDemandRole));
        sign.meta.source_line = ops[i].meta.source_line;
        rewritten.push_back(std::move(sign));
        rewritten.push_back(ops[i]);
        i = macro.end;
        continue;
      }
      if (macro.normalizing_max || macro.predicate_observation) {
        // The boundary's machine code is unchanged, including typed store
        // and provenance metadata needed by subsequent lifetime passes.
        rewritten.insert(rewritten.end(), ops.begin() + static_cast<std::ptrdiff_t>(i),
                         ops.begin() + static_cast<std::ptrdiff_t>(macro.end));
        i = macro.end;
        continue;
      }
      const auto output_begin = rewritten.size();
      if (macro.definition || macro.input_literal) {
        for (const int opcode : macro.replacement_literal) {
          auto literal = opcode >= 0x60 && opcode <= 0x6e
              ? make_recall(register_name_for_index(opcode - 0x60), ops[i].meta) : ops[i];
          literal.opcode = opcode;
          literal.meta.mnemonic = opcode_by_code(opcode).name;
          literal.meta.comment = "finite mode projected representation";
          literal.semantic.clear();
          rewritten.push_back(std::move(literal));
        }
        if (macro.definition)
          rewritten.push_back(ops[macro.end - 1U]);
      } else {
        rewritten.push_back(macro.replacement_recall
            ? make_recall(register_name_for_index(*macro.replacement_recall), ops[i].meta) : ops[i]);
        for (const int opcode : macro.replacement_projection) {
          IrOp decoder;
          decoder.kind = IrKind::Plain;
          decoder.opcode = opcode;
          decoder.meta = ops[i].meta;
          decoder.meta.mnemonic = opcode_by_code(opcode).name;
          decoder.meta.comment = "finite mode consumer decoder";
          rewritten.push_back(std::move(decoder));
        }
        if (macro.transition)
          rewritten.push_back(ops[macro.end - 1U]);
      }
      if (plan.raw_coefficient)
        for (std::size_t emitted = output_begin; emitted < rewritten.size(); ++emitted)
          rewritten[emitted].meta.raw = true;
      i = macro.end;
    }
    auto required_targets = plan.fixed_targets;
    auto original_positions = label_positions(ops);
    if (candidate_options.assumed_target_addresses)
      for (const auto& [label, address] : *candidate_options.assumed_target_addresses)
        original_positions[label] = address;
    bool compatible = true;
    // A changed web has no authority over any other runtime selector.
    for (std::size_t i = 0; i < ops.size(); ++i)
      if (ops[i].meta.indirect_flow_targets && !plan.flow_reads.contains(machine[i]))
        for (const auto& target : *ops[i].meta.indirect_flow_targets) {
          const auto& label = std::get<std::string>(target);
          const auto old = original_positions.find(label);
          if (old == original_positions.end()) {
            compatible = false;
            break;
          }
          const auto [binding, inserted] = required_targets.emplace(label, old->second);
          compatible = compatible && (inserted || binding->second == old->second);
        }
    if (!compatible)
      continue;
    const auto positions = label_positions(rewritten);
    const bool needs_layout = std::any_of(
        required_targets.begin(), required_targets.end(), [&](const auto& target) {
          const auto at = positions.find(target.first);
          return at == positions.end() || at->second != target.second;
        });
    if (needs_layout && !defer_layout) {
      if (!candidate_options.allow_target_relayout)
        continue;
      auto layout = layout_symbolic_regions(rewritten, required_targets, context, 4096,
          &options, candidate_options.preserve_compiler_selector_charges);
      if (!layout)
        continue;
      rewritten = std::move(layout->ops);
    }
    if (!build_post_layout_control_flow(lower_ir_to_machine(rewritten), options).proved)
      continue;
    alternatives.push_back(DeferredFiniteModeRepresentation{
      .transformation = PassResult{
        .ops = std::move(rewritten), .applied = 1,
        .optimizations = {{
            .name = plan.unit_input_normalization ? "unit-argument-producer-normalization" :
                    plan.normalizing_max_count > 0 ? "mixed-epoch-coefficient-state-carrier" :
                    plan.inline_coefficient
                        ? (plan.raw_coefficient ? "raw-inline-coefficient-state-carrier"
                                                : "inline-coefficient-state-carrier") :
                    plan.coefficient_reg
                        ? (plan.raw_coefficient ? "raw-coefficient-state-carrier"
                                                : "coefficient-state-carrier") :
                    plan.transition_count > 0 ? "finite-mode-state-machine" :
                    candidate_options.allow_target_relayout && !plan.flow_reads.empty()
                        ? "finite-mode-data-address-synthesis" :
                    plan.synthesized ? "finite-mode-constraint-synthesis" :
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
                                        : std::string{}) +
                      (plan.transition_count > 0
                          ? " Certified closure and synthesized " +
                                std::to_string(plan.transition_count) +
                                " state transitions over the complete finite domain."
                          : std::string{}) +
                      (plan.coefficient_reg
                          ? " Fused coefficient cache R" + register_name_for_index(*plan.coefficient_reg) +
                                " with the finite carrier; proved cache-version dominance on every path" +
                                (plan.raw_coefficient
                                    ? ", exhaustive positive-power right operands and explicit raw seed delivery."
                                    : " and identical ordinary coefficient values.")
                          : std::string{}) +
                      (plan.inline_coefficient
                          ? " Fused inline coefficient products without a materialized cache; "
                            "proved the complete finite def/use epoch, unchanged multiplication order "
                            "and every transition/observation. " +
                                (plan.seed_reg
                                    ? "Explicit immutable seed in unused R" +
                                          register_name_for_index(*plan.seed_reg) + "."
                                    : "No extra register or preload.")
                          : std::string{}) +
                      (plan.normalizing_max_count > 0
                          ? " Proved " + std::to_string(plan.normalizing_max_count) +
                                " max epoch boundaries: an ordinary strictly positive operand "
                                "dominates every original and encoded finite value. Dynamic "
                                "epochs are unchanged; last-X remains unequal until independently erased."
                          : std::string{}) +
                      (plan.predicate_observation_count > 0
                          ? " Certified " + std::to_string(plan.predicate_observation_count) +
                                " predicate-only observations shared with unchanged dynamic epochs; "
                                "checked the complete finite truth table and both continuations "
                                "without asserting equality of the recalled numeric word."
                          : std::string{}),
        }},
        .preloads = plan.preloads,
      },
      .required_target_addresses = std::move(required_targets),
    });
    if (plan.epoch_pruned_edges != 0 || !plan.exit_recoveries.empty())
      alternatives.back().transformation.optimizations.push_back({
          "finite-mode-exit-recovery",
          "Proved finite-row/ordinary-epoch call-context paths; excluded " +
              std::to_string(plan.epoch_pruned_edges) +
              " impossible predicate edges in the data proof only, without changing the CFG. " +
              "Inserted " + std::to_string(plan.exit_recoveries.size()) +
              " independently certified exit recoveries; charged every added cell and "
              "re-proved register, stack/X1/X2 and all continuation observations."});
    const auto observation_recoveries = std::count_if(
        plan.exit_recoveries.begin(), plan.exit_recoveries.end(), [](const auto& recovery) {
          return recovery.second.kind == ExitRecoveryKind::ExponentObservation ||
              recovery.second.kind == ExitRecoveryKind::UnaryObservation;
        });
    if (observation_recoveries != 0)
      alternatives.back().transformation.optimizations.push_back({
          "finite-mode-exit-observation-decoder",
          "Decoded " + std::to_string(observation_recoveries) +
              " finite predicate exits in X only. The carrier remains encoded until its "
              "independent overwrite or non-observation proof; no store or physical-register "
              "equality is invented. Certified every finite row, exact true-fallthrough "
              "context and stack/X1/X2 continuation; retained the complete historical "
              "restoration search as an independent incumbent."});
    const auto literal_recoveries = std::count_if(
        plan.exit_recoveries.begin(), plan.exit_recoveries.end(), [](const auto& recovery) {
          return recovery.second.kind == ExitRecoveryKind::LiteralRematerialization;
        });
    if (literal_recoveries != 0)
      alternatives.back().transformation.optimizations.push_back({
          "finite-mode-boundary-rematerialization",
          "Restored " + std::to_string(literal_recoveries) +
              " singleton finite epochs with exact integer/store code before opaque dynamic "
              "observations. Proved every incoming row and fresh numeric entry, without "
              "assuming positive scores or ordinary Kmax ordering at zero. Kept all changed "
              "stack/X1/X2 slots unequal until independently overwritten; restored stores "
              "separate definition webs for subsequent allocation and layout search."});
    if (!plan.retired_manual_items.empty())
      alternatives.back().transformation.optimizations.push_back({
          "finite-mode-dead-input-epoch",
          "Retired " + std::to_string(plan.retired_manual_items.size()) +
              " private carrier epochs at typed input boundaries without recovery code; "
              "proved no observation before overwrite on every complete call/return/resume "
              "path, including cycles; retained physical inequality until the write and "
              "preserved stack/X1/X2."});
    if (!closed_geometry && selector_graph.proved && selector_graph.pruned_edges != 0)
      alternatives.back().transformation.optimizations.push_back({
          "finite-mode-deferred-selector-proof",
          "Proved the finite representation over exact selected callback contexts using "
          "encoded command origins, not speculative physical addresses; retained every "
          "unchanged destination equation for independent final layout and return proof."});
    if (alternatives.size() >= candidate_options.maximum_candidates)
      break;
  }
  return alternatives;
}

std::vector<DeferredFiniteModeRepresentation>
deferred_finite_mode_representation_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const FiniteModeCandidateOptions& options) {
  return finite_mode_candidates_impl(ops, context, options, true);
}

std::vector<PassResult> finite_mode_representation_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const FiniteModeCandidateOptions& options) {
  std::vector<PassResult> result;
  for (auto& candidate : finite_mode_candidates_impl(ops, context, options, false))
    result.push_back(std::move(candidate.transformation));

  return result;
}


PassResult finite_mode_representation(const std::vector<IrOp>& ops,
                                      const PassContext& context) {
  auto alternatives = finite_mode_representation_candidates(
      ops, context, {.maximum_growth_cells = 0, .maximum_candidates = 1});
  return alternatives.empty() ? PassResult{.ops = ops} : std::move(alternatives.front());
}

IrPass finite_mode_representation_pass() {
  return IrPass{.name = "finite-mode-representation",
                .run = finite_mode_representation, .layout_safe = false};
}

} // namespace mkpro::core::passes
