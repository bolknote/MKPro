#include "mkpro/core/passes/idempotent_entry_prologue.hpp"
#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/detail/exact_value_terms.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/core/shared_helper_dual_mode_layout.hpp"
#include "mkpro/core/stable_register_value_flow.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <map>
#include <set>
namespace mkpro::core::passes {
namespace {
namespace terms = detail::exact_terms;
using Index = std::size_t;
std::optional<std::string> target(const IrOp& op) {
  if (op.meta.raw || op.target_meta.formal_opcode || op.meta.indirect_flow_formal_targets)
    return {};
  if (op.kind == IrKind::Call)
    return std::get_if<std::string>(&op.target) ? std::optional{std::get<std::string>(op.target)}
                                                : std::nullopt;
  if (op.kind != IrKind::IndirectCall || !op.meta.indirect_flow_targets ||
      op.meta.indirect_flow_targets->size() != 1U)
    return {};
  const auto* name = std::get_if<std::string>(&op.meta.indirect_flow_targets->front());
  return name ? std::optional{*name} : std::nullopt;
}
struct Shape {
  Index first = 0, second = 0;
  std::string helper, normalizer;
  bool memory_argument = false;
  bool compact_memory_entry = false;
  bool resident_first_source = false;
  Index first_normalizer() const {
    return first - (memory_argument ? 5U : 4U);
  }
  Index first_y() const {
    return first - (memory_argument ? 4U : 3U);
  }
  Index first_x() const {
    return first - (memory_argument ? 3U : 2U);
  }
  Index second_y() const {
    return second - (compact_memory_entry ? 1U : memory_argument ? 3U : 2U);
  }
  Index second_x() const {
    return second - (compact_memory_entry ? 3U : memory_argument ? 2U : 1U);
  }
  Index aligned_x() const {
    return resident_first_source ? second - 4U : second_x();
  }
  Index second_region_begin() const {
    return resident_first_source ? second - 4U : second_y();
  }
};
std::vector<Shape> shapes(const std::vector<IrOp>& ops) {
  std::vector<Shape> result;
  for (const bool memory : {false, true}) {
    const Index shift = memory ? 1U : 0U;
    for (Index i = 5U + shift; i < ops.size() && result.size() < 8U; ++i) {
      const Index tail = i - shift;
      const auto h = target(ops[i]), n = target(ops[tail - 4U]);
      if (!h || !n || target(ops[tail - 1U]) != n || ops[tail - 3U].kind != IrKind::Store ||
          ops[tail - 2U].kind != IrKind::Recall || (memory && ops[i - 1U].kind != IrKind::Store))
        continue;
      const auto& input = ops[tail - 5U];
      const auto& resume = ops[tail - 4U];
      if (input.kind != IrKind::Store || input.register_name != ops[tail - 2U].register_name ||
          !input.meta.manual_interaction || !resume.meta.manual_interaction ||
          input.meta.manual_interaction->kind != ManualInteractionAnchorKind::SingleStepCommand ||
          resume.meta.manual_interaction->kind != ManualInteractionAnchorKind::ContinuousResume ||
          input.meta.manual_interaction->protocol_id !=
              resume.meta.manual_interaction->protocol_id ||
          input.meta.manual_interaction->phase + 1 != resume.meta.manual_interaction->phase)
        continue;
      for (Index j = 2U + shift; j < ops.size() && result.size() < 8U; ++j) {
        const Index other = j - shift;
        if (i == j || target(ops[j]) != h || ops[other - 2U].kind != IrKind::Store ||
            ops[other - 1U].kind != IrKind::Recall ||
            ops[other - 2U].register_name != ops[tail - 3U].register_name ||
            (memory && (ops[j - 1U].kind != IrKind::Store ||
                        ops[j - 1U].register_name != ops[i - 1U].register_name)))
          continue;
        result.push_back({i, j, *h, *n, memory, false});
      }
      if (memory)
        for (Index j = 4; j < ops.size() && result.size() < 8U; ++j) {
          // Both arguments are already stored; entry X is the second argument.
          // Keep the first store in place and share only its following Y store.
          if (i == j || target(ops[j]) != h || ops[j - 4U].kind != IrKind::Recall ||
              ops[j - 3U].kind != IrKind::Store || ops[j - 2U].kind != IrKind::Recall ||
              ops[j - 1U].kind != IrKind::Store ||
              ops[j - 3U].register_name != ops[i - 1U].register_name ||
              ops[j - 1U].register_name != ops[tail - 3U].register_name)
            continue;
          result.push_back({i, j, *h, *n, true, true});
        }
    }
  }
  // Preserve historical shapes; the source-resident ABI is an independent
  // alternative with its own source-web equality and complete memory proof.
  const auto established = result;
  for (auto shape : established)
    if (shape.compact_memory_entry) {
      shape.resident_first_source = true;
      result.push_back(std::move(shape));
    }
  return result;
}
struct Leaf {
  Index begin = 0, end = 0;
  std::vector<IrOp> body;
};
std::optional<Leaf> leaf(const std::vector<IrOp>& ops, const std::string& name) {
  std::optional<Index> label;
  for (Index i = 0; i < ops.size(); ++i)
    if (ops[i].kind == IrKind::Label && ops[i].name == name) {
      if (label)
        return {};
      label = i;
    }
  if (!label)
    return {};
  Index start = *label + 1U;
  while (start < ops.size() && ops[start].kind == IrKind::Label)
    ++start;
  Leaf result;
  result.begin = start;
  for (Index i = start; i < ops.size() && result.body.size() < 32U; ++i) {
    const auto& op = ops[i];
    if (op.kind == IrKind::Return && !op.meta.raw && !op.meta.manual_interaction) {
      result.end = i;
      return result;
    }
    if (op.kind == IrKind::Label) {
      const bool referenced = std::any_of(ops.begin(), ops.end(), [&](const auto& other) {
        if (const auto* name = std::get_if<std::string>(&other.target); name && *name == op.name)
          return true;
        return other.meta.indirect_flow_targets &&
               std::any_of(other.meta.indirect_flow_targets->begin(),
                           other.meta.indirect_flow_targets->end(), [&](const auto& target) {
                             const auto* name = std::get_if<std::string>(&target);
                             return name && *name == op.name;
                           });
      });
      if (referenced)
        return {};
      continue;
    }
    if (op.meta.raw || op.meta.manual_interaction || op.target_meta.formal_opcode ||
        (op.kind != IrKind::Plain && op.kind != IrKind::Recall && op.kind != IrKind::Store))
      return {};
    if (op.kind == IrKind::Plain && !terms::modeled_plain(op.opcode))
      return {};
    result.body.push_back(op);
  }
  return {};
}
// Totality is independent of the finite identity proof. An ordinary but
// unbounded first keyboard argument must not acquire a new error/return-stack
// observation merely because the same leaf is idempotent on a small domain.
bool ordinary_total_leaf(const Leaf& function) {
  struct Bound {
    bool ordinary = false;
    std::optional<std::int64_t> magnitude;
  };
  std::array<Bound, 4> stack{};
  stack[0].ordinary = true;
  std::string literal;
  for (const auto& op : function.body) {
    if (op.kind != IrKind::Plain)
      return false;
    const int code = op.opcode;
    const auto old = stack;
    if (code >= 0 && code <= 9) {
      if (literal.empty())
        stack = {old[0], old[0], old[1], old[2]};
      literal.push_back(static_cast<char>('0' + code));
      const auto bound = bound_ordinary_decimal(literal);
      if (!bound || literal.size() > 8U)
        return false;
      stack[0] = {true, std::max(std::abs(bound->lower), std::abs(bound->upper))};
      continue;
    }
    if (code == 0x0a) {
      if (literal.empty() || literal.find('.') != std::string::npos)
        return false;
      literal += '.';
      continue;
    }
    if (code == 0x14) {
      stack = {old[1], old[0], old[2], old[3]};
      literal.clear();
      continue;
    }
    if (code == 0x25) {
      stack = {old[1], old[2], old[3], old[0]};
      literal.clear();
      continue;
    }
    if (!old[0].ordinary)
      return false;
    if (code == 0x34 || code == 0x0b) {
      literal.clear();
      continue;
    }
    if (code == 0x35 || code == 0x32) {
      stack[0] = {true, 1};
      literal.clear();
      continue;
    }
    if (code >= 0x10 && code <= 0x13) {
      if (!old[1].ordinary)
        return false;
      Bound result{true, {}};
      if (code == 0x13) {
        const auto divisor = literal.empty() ? std::nullopt : bound_ordinary_decimal(literal);
        if (!divisor || !((divisor->lower >= 1) || (divisor->upper <= -1)))
          return false;
        result.magnitude = old[1].magnitude;
      } else {
        if (!old[0].magnitude || !old[1].magnitude)
          return false;
        const auto x = *old[0].magnitude, y = *old[1].magnitude;
        if (x > 100000000 || y > 100000000)
          return false;
        const auto maximum = code == 0x12 ? x * y : x + y;
        if (maximum > 100000000)
          return false;
        result.magnitude = maximum;
      }
      stack = {result, old[2], old[3], old[3]};
      literal.clear();
      continue;
    }
    return false;
  }
  return literal.empty() && stack[0].ordinary;
}
bool stable_call(const IrOp& op, const std::vector<IrOp>& ops,
                 const std::vector<PreloadReport>& words, AddressSpaceModel model) {
  if (op.kind == IrKind::Call)
    return true;
  if (op.kind != IrKind::IndirectCall)
    return false;
  const auto found = std::find_if(words.begin(), words.end(), [&](const auto& word) {
    return word.register_name == op.register_name;
  });
  if (found == words.end() || found->setup_expression || found->setup_target_name ||
      found->setup_expression_text)
    return false;
  for (const auto& instruction : ops) {
    const auto effect = register_effects(instruction);
    if (effect.may_define_any_register || effect.must_defs.contains(op.register_name) ||
        effect.may_defs.contains(op.register_name))
      return false;
  }
  try {
    const auto address = evaluate_indirect_address(op.register_name, found->value,
                                                   IndirectOperationKind::Flow, model);
    return address && indirect_writeback_preserves_literal_word(*address, found->value);
  } catch (const std::exception&) {
    return false;
  }
}
// Prove totality on the complete finite caller image, before forgetting any
// concrete decimal used only as an uninterpreted operation key. Unknown
// registers may be transported but never consumed by potentially failing math.
bool bounded_total_leaf(const Leaf& function, const terms::State& entry) {
  struct Bound {
    bool ordinary = false;
    std::optional<std::int64_t> magnitude;
  };
  const auto bounded = [](const terms::Value& value) {
    Bound result;
    if (value.decimal)
      if (const auto bound = bound_ordinary_decimal(*value.decimal))
        result = {true, std::max(std::abs(bound->lower), std::abs(bound->upper))};
    return result;
  };
  std::array<Bound, 4> stack;
  std::array<Bound, 15> registers;
  for (Index i = 0; i < stack.size(); ++i)
    stack[i] = bounded(entry.stack[i]);
  for (Index i = 0; i < registers.size(); ++i)
    registers[i] = bounded(entry.registers[i]);
  for (const auto& op : function.body) {
    const auto old = stack;
    if (op.kind == IrKind::Recall) {
      stack = {registers.at(static_cast<Index>(register_index(op.register_name))), old[0], old[1],
               old[2]};
      continue;
    }
    if (op.kind == IrKind::Store) {
      registers.at(static_cast<Index>(register_index(op.register_name))) = old[0];
      continue;
    }
    if (op.kind != IrKind::Plain)
      return false;
    if (op.opcode == 0x14) {
      stack = {old[1], old[0], old[2], old[3]};
      continue;
    }
    if (op.opcode == 0x25) {
      stack = {old[1], old[2], old[3], old[0]};
      continue;
    }
    if (!old[0].ordinary)
      return false;
    if (op.opcode == 0x34 || op.opcode == 0x0b)
      continue;
    if (op.opcode == 0x35 || op.opcode == 0x32) {
      stack[0] = {true, 1};
      continue;
    }
    if (op.opcode == 0x15) {
      if (!old[0].magnitude || *old[0].magnitude > 8)
        return false;
      std::int64_t maximum = 1;
      for (std::int64_t i = 0; i < *old[0].magnitude; ++i)
        maximum *= 10;
      stack[0] = {true, maximum};
      continue;
    }
    if (op.opcode != 0x10 && op.opcode != 0x11 && op.opcode != 0x12)
      return false;
    if (!old[1].ordinary || !old[0].magnitude || !old[1].magnitude)
      return false;
    const auto x = *old[0].magnitude, y = *old[1].magnitude;
    if (x > 1000000000 || y > 1000000000)
      return false;
    const auto maximum = op.opcode == 0x12 ? x * y : x + y;
    if (maximum > 1000000000)
      return false;
    stack = {Bound{true, maximum}, old[2], old[3], old[3]};
  }
  return true;
}
void evaluate_helper(terms::Evaluation& e, const Leaf& h, bool finite_total, bool called) {
  if (called)
    e.synchronize_call();
  const bool total = finite_total && bounded_total_leaf(h, e.state);
  const auto observations = e.observations.size();
  for (const auto& op : h.body) {
    // The decimal evaluator deliberately has no transcendental oracle. Keep
    // the exact operand key, but use an uninterpreted total operation instead
    // of host floating point after the independent exponent bound succeeded.
    if (total && op.kind == IrKind::Plain && op.opcode == 0x15)
      e.state.stack[0].decimal.reset();
    e.execute(op);
  }
  if (total && e.valid)
    e.observations.resize(observations);
  if (called) {
    if (!e.state.literal.empty())
      e.valid = false;
    e.synchronize_call();
  }
}
// The small exact-term evaluator accepts plain decimals. Convert a validated
// bounded scientific spelling by moving its decimal point, never via float.
std::optional<terms::Value> ordinary_preload_term(const std::string& word) {
  if (const auto value = terms::number(word))
    return value;
  if (!bound_ordinary_decimal(word))
    return {};
  const auto exponent_at = word.find_first_of("eE");
  if (exponent_at == std::string::npos)
    return {};
  const bool negative = word.front() == '-';
  const Index start = (negative || word.front() == '+') ? 1U : 0U;
  Index e = exponent_at + 1U;
  bool inverse = false;
  if (e < word.size() && (word[e] == '-' || word[e] == '+'))
    inverse = word[e++] == '-';
  int exponent = 0;
  for (; e < word.size(); ++e) {
    exponent = exponent * 10 + word[e] - '0';
    if (exponent > 8)
      return {};
  }
  if (inverse)
    exponent = -exponent;
  std::string digits;
  int point = 0;
  bool seen_point = false;
  for (Index i = start; i < exponent_at; ++i) {
    if (word[i] == '.') {
      seen_point = true;
      continue;
    }
    digits += word[i];
    if (!seen_point)
      ++point;
  }
  point += exponent;
  if (digits.empty() || digits.size() > 16U || point < -8 || point > 16)
    return {};
  std::string decimal = negative ? "-" : "";
  if (point <= 0)
    decimal += "0." + std::string(static_cast<Index>(-point), '0') + digits;
  else if (static_cast<Index>(point) >= digits.size())
    decimal += digits + std::string(static_cast<Index>(point) - digits.size(), '0');
  else
    decimal += digits.substr(0, static_cast<Index>(point)) + "." +
               digits.substr(static_cast<Index>(point));
  return terms::number(decimal);
}
std::map<int, terms::Value> invariant_preload_terms(const std::vector<IrOp>& ops,
                                                    const std::vector<PreloadReport>& words) {
  std::map<int, terms::Value> result;
  for (const auto& word : words) {
    if (word.setup_expression || word.setup_target_name || word.setup_expression_text)
      continue;
    const auto value = ordinary_preload_term(word.value);
    if (!value)
      continue;
    bool fixed = true;
    for (const auto& op : ops) {
      const auto effect = register_effects(op);
      if (effect.may_define_any_register || effect.must_defs.contains(word.register_name) ||
          effect.may_defs.contains(word.register_name)) {
        fixed = false;
        break;
      }
    }
    const int reg = register_index(word.register_name);
    if (fixed && reg >= 0 && reg < 15)
      result.emplace(reg, *value);
  }
  return result;
}
bool same_evaluation(const terms::Evaluation& a, const terms::Evaluation& b) {
  return a.valid && b.valid && a.state == b.state && a.observations == b.observations;
}
void original_prefix(terms::Evaluation& e, const std::vector<IrOp>& ops, const Shape& shape,
                     const Leaf& n, const Leaf& h, bool first, bool finite_total = false) {
  if (first) {
    e.call(n.body);
    e.execute(ops[shape.first_y()]);
    e.execute(ops[shape.first_x()]);
    e.call(n.body);
  } else {
    if (shape.resident_first_source) {
      e.execute(ops[shape.second - 4U]);
      e.execute(ops[shape.second - 3U]);
      e.execute(ops[shape.second - 2U]);
    }
    e.execute(ops[shape.second_y()]);
    if (!shape.compact_memory_entry)
      e.execute(ops[shape.second_x()]);
  }
  if (shape.memory_argument && (first || !shape.compact_memory_entry))
    e.execute(ops[(first ? shape.first : shape.second) - 1U]);
  evaluate_helper(e, h, finite_total, true);
}
void new_prefix(terms::Evaluation& e, const std::vector<IrOp>& ops, const Shape& shape,
                const Leaf& n, const Leaf& h, bool finite_total = false, bool first = true) {
  if (!first && shape.resident_first_source)
    e.execute(ops[shape.second - 2U]);
  e.synchronize_call();
  e.call(n.body);
  e.execute(ops[shape.first_y()]);
  e.execute(ops[shape.first_x()]);
  e.call(n.body);
  if (shape.memory_argument)
    e.execute(ops[shape.first - 1U]);
  evaluate_helper(e, h, finite_total, false);
  e.synchronize_call();
}
} // namespace
std::vector<RegisterWebRecoloringRequest>
idempotent_entry_prologue_register_requests(const std::vector<IrOp>& ops) {
  std::vector<RegisterWebRecoloringRequest> result;
  for (const auto& shape : shapes(ops)) {
    const RegisterWebRecoloringRequest base{{shape.first_x(), shape.aligned_x()}};
    result.push_back(base);
    const auto h = leaf(ops, shape.helper);
    if (!h || h->body.empty() || (!shape.memory_argument && h->body.front().kind != IrKind::Store))
      continue;
    const auto& parameter = shape.memory_argument ? ops[shape.first - 1U] : h->body.front();
    for (Index i = 1; i < ops.size() && result.size() < 8U; ++i)
      if (ops[i - 1U].kind == IrKind::Recall && ops[i].kind == IrKind::Store &&
          ops[i - 1U].register_name == parameter.register_name &&
          ops[i].register_name != ops[i - 1U].register_name) {
        auto combined = base;
        combined.push_back({shape.aligned_x(), i});
        result.push_back(std::move(combined));
      }
  }
  // Reply/input/source equalities name lifetimes, not equal values.
  std::size_t reply_requests = 0;
  for (const auto& shape : shapes(ops)) {
    if (!shape.resident_first_source || reply_requests >= 8U) continue;
    const auto input = shape.first_normalizer() - 1U;
    if (input < 2U || !ops[input].meta.manual_interaction ||
        ops[input - 1U].kind != IrKind::Stop || !ops[input - 1U].meta.manual_interaction ||
        ops[input - 1U].meta.manual_interaction->kind != ManualInteractionAnchorKind::PromptStop ||
        ops[input - 1U].meta.manual_interaction->protocol_id !=
            ops[input].meta.manual_interaction->protocol_id ||
        ops[input - 2U].kind != IrKind::Recall) continue;
    result.push_back({{shape.first_x(), shape.aligned_x()},
                      {shape.first_x(), input - 2U}});
    ++reply_requests;
  }
  return result;
}
std::vector<PassResult> deferred_idempotent_entry_prologue_candidates(
    const std::vector<IrOp>& ops, const std::vector<PreloadReport>& words,
    const PostLayoutControlFlowOptions& control, const std::map<std::string, int>& origins) {
  std::vector<PassResult> result;
  const auto candidates = shapes(ops);
  if (candidates.empty())
    return result;
  const auto input = lower_ir_to_machine(ops);
  if (std::any_of(input.begin(), input.end(), [](const auto& item) { return item.raw; }) ||
      std::any_of(words.begin(), words.end(),
                  [](const auto& word) { return !bound_ordinary_decimal(word.value); }))
    return result;
  std::map<std::string, int> positions;
  int pc = 0;
  for (const auto& op : ops) {
    if (op.kind == IrKind::Label)
      positions.emplace(op.name, pc);
    pc += cells_per_op(op);
  }
  const auto original = build_post_layout_control_flow(input, control);
  if (!original.proved || !prove_post_layout_input_boundaries(input, original).proved)
    return result;
  const auto selected =
      build_deferred_selector_consistent_execution_graph(input, original, origins).execution;
  if (!selected.proved)
    return result;
  auto flow = original;
  flow.execution_states.clear();
  for (auto state : selected.original_states)
    flow.execution_states.push_back(original.execution_states[state]);
  flow.execution_successors = selected.successors;
  flow.execution_edges = selected.execution_edges;
  const auto lift =
      analyze_post_layout_automatic_entry_lift_with_indirect_store_facts(input, flow, 200000);
  if (!lift)
    return result;
  std::vector<Index> machine;
  Index cursor = 0;
  for (const auto& op : ops) {
    machine.push_back(cursor);
    cursor += lower_ir_to_machine({op}).size();
  }
  std::map<Index, Index> instruction;
  for (Index i = 0; i < machine.size(); ++i)
    instruction.emplace(machine[i], i);
  const auto prepared =
      prepare_shared_helper_dual_mode_layout(input, original, control.address_space_model);
  if (prepared.applied <= 0)
    return result;
  for (const auto& shape : candidates) {
    if (ops[shape.first_x()].register_name != ops[shape.aligned_x()].register_name)
      continue;
    const auto n = leaf(ops, shape.normalizer), h = leaf(ops, shape.helper);
    if (shape.resident_first_source &&
        ops[shape.second - 4U].register_name == ops[shape.second - 2U].register_name)
      continue; // Independent finite images cannot be invented for aliases.
    if (!n || !h || !ordinary_total_leaf(*n) || h->body.empty() ||
        (!shape.memory_argument && h->body.front().kind != IrKind::Store) ||
        (shape.memory_argument && std::any_of(h->body.begin(), h->body.end(), [](const auto& op) {
           return op.kind == IrKind::Store;
         })))
      continue;
    if (!std::all_of(prepared.continuation.calls.begin(), prepared.continuation.calls.end(),
                     [&](const auto& c) {
                       return c.entry_address == prepared.continuation.helper_body_start_address;
                     }) ||
        (prepared.continuation.helper_label != shape.helper &&
         positions.at(shape.helper) != prepared.continuation.helper_body_start_address))
      continue;
    std::set<Index> ordinary;
    std::optional<Index> divergent;
    for (const auto& call : prepared.continuation.calls) {
      const auto found = instruction.find(call.call_item_index);
      if (found == instruction.end())
        continue;
      if (call.ordinary)
        ordinary.insert(found->second);
      else
        divergent = found->second;
    }
    if (ordinary != std::set<Index>{shape.first, shape.second} || !divergent ||
        (!shape.memory_argument &&
         (*divergent == 0 || ops[*divergent - 1U].kind != IrKind::Recall ||
          ops[*divergent - 1U].register_name != h->body.front().register_name)))
      continue;
    if (!stable_call(ops[*divergent], ops, words, control.address_space_model) ||
        !stable_call(ops[shape.first_normalizer()], ops, words, control.address_space_model) ||
        !stable_call(ops[shape.first - (shape.memory_argument ? 2U : 1U)], ops, words,
                     control.address_space_model))
      continue;
    // No external/interior route may enter either moved straight prefix.
    bool closed = true;
    std::set<Index> interiors;
    for (Index i = shape.first_normalizer() + 1U; i <= shape.first; ++i)
      interiors.insert(machine[i]);
    for (Index i = shape.compact_memory_entry ? shape.second - 3U : shape.second_y() + 1U;
         i <= shape.second; ++i)
      interiors.insert(machine[i]);
    for (const auto& entry : original.external_entries)
      if (interiors.contains(entry.entry.item_index))
        closed = false;
    for (Index s = 0; s < original.execution_states.size(); ++s)
      for (const auto& edge : original.execution_edges[s]) {
        const auto to = original.execution_states[edge.target_state].item_index;
        if (!interiors.contains(to))
          continue;
        const auto found = instruction.find(to);
        if (found == instruction.end() || found->second == 0) {
          closed = false;
          continue;
        }
        const auto previous = found->second - 1U;
        const auto source = original.execution_states[s].item_index;
        if (source != machine[previous] && edge.kind != PostLayoutExecutionEdgeKind::Return)
          closed = false;
      }
    if (!closed)
      continue;
    const auto ranges = prove_post_layout_numeric_ranges(
        input, flow, *lift,
        {machine[shape.second_y()],
         machine[shape.compact_memory_entry ? shape.second - 3U : shape.second]},
        &words, 400000, NumericRangeInputPolicy::ValidatedRegisterTransport);
    const auto y = ranges.x_before.find(machine[shape.second_y()]),
               x = ranges.x_before.find(
                   machine[shape.compact_memory_entry ? shape.second - 3U : shape.second]);
    if (ranges.exhausted || x == ranges.x_before.end() || y == ranges.x_before.end() ||
        !x->second.integral || !y->second.integral || x->second.lower < -16 ||
        x->second.upper > 16 || y->second.lower < -16 || y->second.upper > 16 ||
        (x->second.upper - x->second.lower + 1) * (y->second.upper - y->second.lower + 1) > 64)
      continue;
    terms::Evaluation first_old, first_new;
    original_prefix(first_old, ops, shape, *n, *h, true);
    new_prefix(first_new, ops, shape, *n, *h);
    if (!same_evaluation(first_old, first_new))
      continue;
    bool equivalent = true;
    StackValueEqualityState continuation{{true, true, true, true}, true, true};
    const auto invariants = invariant_preload_terms(ops, words);
    for (auto xv = x->second.lower; xv <= x->second.upper && equivalent; ++xv)
      for (auto yv = y->second.lower; yv <= y->second.upper && equivalent; ++yv) {
        terms::Evaluation before, after;
        for (const auto& [reg, value] : invariants)
          before.state.registers.at(static_cast<Index>(reg)) = value;
        const auto vx = terms::number(std::to_string(xv)), vy = terms::number(std::to_string(yv));
        if (!vx || !vy) {
          equivalent = false;
          break;
        }
        if (shape.resident_first_source)
          before.state.registers.at(static_cast<Index>(
              register_index(ops[shape.second - 2U].register_name))) = *vy;
        else
          before.state.stack[0] = *vy;
        before.state.registers.at(
            static_cast<Index>(register_index(ops[shape.first_x()].register_name))) = *vx;
        after = before;
        original_prefix(before, ops, shape, *n, *h, false, shape.compact_memory_entry);
        new_prefix(after, ops, shape, *n, *h, shape.compact_memory_entry, false);
        equivalent = before.valid && after.valid && before.observations == after.observations &&
                     before.state.registers == after.state.registers &&
                     before.state.literal == after.state.literal;
        for (Index slot = 0; slot < 4U; ++slot)
          continuation.stack_equal[slot] =
              continuation.stack_equal[slot] && before.state.stack[slot] == after.state.stack[slot];
        continuation.x1_equal = continuation.x1_equal && before.state.x1 == after.state.x1;
        continuation.x2_equal = continuation.x2_equal && before.state.x2 == after.state.x2;
      }
    if (!equivalent)
      continue;
    if (!stack_values_fully_equal(continuation)) {
      if (!shape.compact_memory_entry || shape.second + 1U >= machine.size())
        continue;
      std::vector<Index> returns;
      for (Index state = 0; state < original.execution_states.size(); ++state)
        if (original.execution_states[state].item_index == machine[shape.second + 1U])
          returns.push_back(state);
      auto future = input;
      // A moved/rewritten operation cannot serve as its own convergence witness.
      for (Index i = shape.first_normalizer(); i <= shape.first; ++i)
        future[machine[i]].raw = true;
      for (Index i = shape.second_region_begin(); i <= shape.second; ++i)
        future[machine[i]].raw = true;
      future[machine[h->begin]].raw = true;
      future[machine[*divergent]].raw = true;
      if (!prove_post_layout_stack_state_equality(future, original, returns, continuation,
                                                  {.refine_runtime_selectors = false,
                                                   .numeric_entry_closed = true,
                                                   .transport_closed_entry_through_control = true,
                                                   .use_indirect_store_entry_facts = true}))
        continue;
    }
    std::string body = "__idempotent_entry_body";
    while (std::any_of(ops.begin(), ops.end(),
                       [&](const auto& op) { return op.kind == IrKind::Label && op.name == body; }))
      body += '_';
    std::vector<IrOp> output;
    std::set<Index> erased;
    for (Index i = shape.second_region_begin(); i < shape.second; ++i)
      if (!shape.resident_first_source || i != shape.second - 2U)
        erased.insert(i);
    if (!shape.memory_argument)
      erased.insert(h->begin);
    for (Index i = shape.first_normalizer(); i < shape.first; ++i)
      erased.insert(i);
    const auto body_label = [&] {
      IrOp op;
      op.kind = IrKind::Label;
      op.name = body;
      return op;
    };
    for (Index i = 0; i < ops.size(); ++i) {
      if (i == h->begin) {
        for (Index prefix = shape.first_normalizer(); prefix < shape.first; ++prefix) {
          auto op = ops[prefix];
          op.meta.manual_interaction.reset();
          output.push_back(std::move(op));
        }
        if (!shape.memory_argument)
          output.push_back(h->body.front());
        output.push_back(body_label());
      }
      if (erased.contains(i))
        continue;
      auto op = ops[i];
      if (i == shape.first)
        op.meta.manual_interaction = ops[shape.first_normalizer()].meta.manual_interaction;
      if (i == *divergent) {
        op.kind = IrKind::Call;
        op.opcode = 0x53;
        op.register_name.clear();
        op.target = body;
        op.target_meta = {};
        op.meta.indirect_flow_targets.reset();
        op.meta.indirect_flow_formal_targets.reset();
        op.meta.mnemonic = "PP";
      }
      output.push_back(std::move(op));
    }
    const auto final_items = lower_ir_to_machine(output);
    const auto final_flow = build_post_layout_control_flow(final_items, control);
    if (!final_flow.proved || !prove_post_layout_input_boundaries(final_items, final_flow).proved)
      continue;
    result.push_back(
        {.ops = std::move(output),
         .applied = 1,
         .optimizations = {
             {"idempotent-entry-prologue",
              "Factored a typed two-input pure prologue across two common-leaf entries; proved "
              "ordinary-input totality, a complete finite identity domain, identical operation "
              "error observations and register state; independently proved any remaining "
              "stack/X1/X2 differences unobservable on every exact continuation. Supported stored "
              "and "
              "one-X parameter ABIs, retaining all memory effects; checked all entry routes, "
              "with an independently proved resident-source ABI able to omit the "
              "first-argument copy and the second-argument store; "
              "unchanged selector writeback, "
              "final call/return depth and input protocol. Physical selector and side-entry "
              "placement remain mandatory."}}});
  }
  return result;
}
} // namespace mkpro::core::passes

