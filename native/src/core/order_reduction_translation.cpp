#include "mkpro/core/order_reduction_translation.hpp"

#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/stack_value_equivalence.hpp"
#include "mkpro/core/stable_register_value_flow.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <set>
#include <tuple>

namespace mkpro::core {
namespace {
enum Relation : unsigned char { Equal, EqualNegative, Translated, Different };
struct Cell {
  Relation relation = Equal;
  OrdinaryDecimalEnclosure source;
  Cell() = default;
  Cell(Relation r) : relation(r) {}
  Cell(Relation r, OrdinaryDecimalEnclosure interval) : relation(r), source(interval) {}
  operator Relation() const { return relation; }
  bool operator==(Relation r) const { return relation == r; }
  bool operator==(const Cell&) const = default;
  bool operator<(const Cell& other) const {
    return std::tie(relation, source.lower, source.upper) <
        std::tie(other.relation, other.source.lower, other.source.upper);
  }
};
bool equal(Relation value) { return value == Equal || value == EqualNegative; }
constexpr std::size_t last_x = 19, entry_x = 20;
using Values = std::array<Cell, 21>; // stack, R0..RE, X1, X2
using Visit = std::pair<std::size_t, Values>;
bool fully_equal(const Values& values) {
  return std::all_of(values.begin(), values.end(), equal);
}
bool role(const MachineItem& item, const char* value) {
  return std::find(item.roles.begin(), item.roles.end(), value) != item.roles.end();
}
} // namespace

OrderReductionTranslationProof prove_order_reduction_translation(
    const std::vector<MachineItem>& items, const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<bool>& automatic_entry_lift, const PostLayoutNumericRangeProof& bounds,
    std::size_t first_digit, std::size_t last_digit, std::size_t maximum_work) {
  OrderReductionTranslationProof result;
  const auto reject = [&](const std::string& reason) {
    result.reasons.push_back(reason); return result;
  };
  const auto count = flow.execution_states.size();
  if (!flow.proved || bounds.exhausted || count == 0 || automatic_entry_lift.size() != count ||
      flow.execution_edges.size() != count || first_digit >= items.size() || last_digit >= items.size() ||
      first_digit >= last_digit || items[first_digit].kind != MachineItemKind::Op ||
      items[last_digit].kind != MachineItemKind::Op || items[first_digit].opcode < 1 ||
      items[first_digit].opcode > 9 || items[last_digit].opcode < 0 || items[last_digit].opcode > 9)
    return reject("incomplete literal, numeric proof or execution graph");
  for (const auto& item : items) if (item.kind == MachineItemKind::Op) {
    const int family = item.opcode & 0xf0;
    if (item.opcode == 0x4f || item.opcode == 0x6f ||
        (family >= 0x70 && family <= 0xe0 && (item.opcode & 15) == 15))
      return reject("Rf and undocumented register aliases are outside this proof");
  }
  for (std::size_t at = first_digit + 1; at < last_digit; ++at)
    if (items[at].kind != MachineItemKind::Label) return reject("literal is not two digits");
  const int seed = items[first_digit].opcode * 10 + items[last_digit].opcode;
  // The closed family is deliberately restricted to the positive two-digit
  // band. This also excludes the zero-sensitive Kmax ordering anomaly.
  int minimum_delta = 10 - seed, maximum_delta = 99 - seed;
  constexpr auto scale = OrdinaryDecimalEnclosure::scale;
  const auto constrain = [&](const OrdinaryStackValue& value) {
    auto enclosure = value.enclosure;
    if (!enclosure && value.range)
      enclosure = OrdinaryDecimalEnclosure{value.range->lower * scale, value.range->upper * scale};
    if (!value.ordinary || !enclosure || enclosure->lower < 10 * scale ||
        enclosure->upper >= 100 * scale) return false;
    const auto ceil = [](std::int64_t n) { return n / scale + (n % scale > 0 ? 1 : 0); };
    const auto floor = [](std::int64_t n) { return n / scale - (n % scale < 0 ? 1 : 0); };
    minimum_delta = std::max(minimum_delta, static_cast<int>(ceil(10 * scale - enclosure->lower)));
    maximum_delta = std::min(maximum_delta, static_cast<int>(floor(100 * scale - 1 - enclosure->upper)));
    return minimum_delta <= maximum_delta;
  };
  const auto value_at = [&](std::size_t state, std::size_t slot) -> OrdinaryStackValue {
    const auto found = bounds.stack_before_state.find(state);
    return found == bounds.stack_before_state.end() ? OrdinaryStackValue{} : found->second[slot];
  };
  const auto negative_at = [&](std::size_t state, std::size_t slot) {
    const auto value = value_at(state, slot);
    return value.ordinary && ((value.enclosure && value.enclosure->upper < 0) ||
                              (value.range && value.range->upper < 0));
  };
  std::vector<std::vector<std::size_t>> predecessors(count);
  for (std::size_t state = 0; state < count; ++state)
    for (const auto next : flow.execution_successors[state]) {
      if (next >= count) return reject("invalid successor");
      predecessors[next].push_back(state);
    }
  const auto external = resolve_post_layout_external_entry_contexts(flow);
  const auto boundaries = prove_post_layout_input_boundaries(items, flow);
  if (!external || !boundaries.proved) return reject("unproved input protocol");
  std::map<std::size_t, std::size_t> literal_exits;
  for (std::size_t state = 0; state < count; ++state) {
    const auto at = flow.execution_states[state].item_index;
    if (at == last_digit) {
      if (external->external[state] || predecessors[state].size() != 1U ||
          flow.execution_states[predecessors[state][0]].item_index != first_digit)
        return reject("side entry into translated literal");
    }
    if (at != first_digit) continue;
    if (items[at].raw || items[last_digit].raw || items[at].manual_interaction ||
        items[last_digit].manual_interaction || !automatic_entry_lift[state] ||
        flow.execution_successors[state].size() != 1U)
      return reject("literal has no proved fresh entry");
    const auto second = flow.execution_successors[state][0];
    if (flow.execution_states[second].item_index != last_digit ||
        flow.execution_successors[second].size() != 1U)
      return reject("literal has a nonlocal continuation");
    // Entry in the formal address space may fetch a discontinuous command.
    // The exact execution graph above must still contain both real digits.
    literal_exits.emplace(state, flow.execution_successors[second][0]);
  }
  if (literal_exits.empty()) return reject("unreachable initializer");
  std::deque<Visit> pending;
  std::set<Visit> seen;
  for (const auto state : boundaries.main_states) pending.emplace_back(state, Values{});
  if (pending.empty()) return reject("missing main entry");
  bool used_maximum = false;
  while (!pending.empty()) {
    auto [state, values] = pending.front(); pending.pop_front();
    if (!seen.emplace(state, values).second) continue;
    if (++result.work > maximum_work) return reject("relational proof exhausted");
    const auto at = flow.execution_states[state].item_index;
    const auto& item = items[at];
    const int code = item.opcode, family = code & 0xf0, reg = code & 15;
    bool positive_condition = false;
    const auto fail = [&](const char* message) { return reject(std::string(message) + " at item " + std::to_string(at)); };
    if (boundaries.input_boundary_states[state]) {
      // Keyboard edits are equal only if the visible input pair was equal.
      // Do not erase old last-X or an unequal deeper stack at a prompt.
      if (!equal(values[0]) || !equal(values[1])) return fail("observable input difference");
      values[0] = Equal;
      // Input does not by itself reset X1/X2. Nor does it promise a single
      // particular lift/inhibit editing sequence for the deeper stack.
      if (!equal(values[3]) || !equal(values[2])) values[3] = Different;
      if (!equal(values[2]) || !equal(values[1])) values[2] = Different;
    }
    if (const auto literal = literal_exits.find(state); literal != literal_exits.end()) {
      const auto old = values;
      values[0] = Cell{Translated, {seed * scale, seed * scale}};
      values[1] = old[0]; values[2] = old[1]; values[3] = old[2];
      // The first (changed) digit also changes the hidden entry buffer; its
      // difference is not the completed literal's translation.
      values[entry_x] = Different;
      pending.emplace_back(literal->second, values); continue;
    }
    if (item.kind != MachineItemKind::Op || item.raw) {
      if (!fully_equal(values)) return fail("opaque command observes a difference");
    } else if (code == 0x50 || code == 0x29) {
      const bool typed = role(item, kTypedDisplayObservationRole);
      if (!equal(values[0]) || !equal(values[1]) || (!typed && !fully_equal(values)))
        return fail("observable stop difference");
      values[entry_x] = values[0];
    } else if (family == 0x40 && reg < 15) {
      values[4U + static_cast<std::size_t>(reg)] = values[0];
    } else if (family == 0x60 && reg < 15) {
      const auto old = values;
      values[0] = old[4U + static_cast<std::size_t>(reg)];
      values[1] = old[0]; values[2] = old[1]; values[3] = old[2]; values[entry_x] = old[0];
    } else if (family == 0xb0 || family == 0xd0) {
      if (reg == 15 || !equal(values[4U + static_cast<std::size_t>(reg)]))
        return fail("translated memory selector");
      const auto targets = flow.indirect_memory_targets.find(at);
      if (targets == flow.indirect_memory_targets.end() || targets->second.empty())
        return fail("unknown memory aliases");
      auto selected_targets = targets->second;
      if (const auto known = bounds.memory_selector_before_state.find(state);
          known != bounds.memory_selector_before_state.end()) {
        const auto& word = known->second;
        std::optional<int> selected;
        if (reg < 4 && word.normalized_or_payload) selected = 7;
        else if (word.integer_selector_word && word.range && word.range->integral &&
                 word.range->lower == word.range->upper) {
          const auto decoded = evaluate_indirect_address(std::string(1, "0123456789abcde"[reg]),
              std::to_string(word.range->lower), IndirectOperationKind::Memory, flow.address_space_model);
          if (decoded) selected = decoded->memory_target;
        }
        if (selected) {
          if (std::find(selected_targets.begin(), selected_targets.end(), *selected) == selected_targets.end())
            return fail("selector contradicts authoritative aliases");
          selected_targets = {*selected};
        }
      }
      // Split complete alias alternatives rather than joining away the
      // correlation between the selected bank and its stored relation.
      for (const int target : selected_targets) {
        if (target < 0 || target >= 15) return fail("invalid memory alias");
        auto next_values = values;
        if (reg <= 6) next_values[4U + static_cast<std::size_t>(reg)] = Equal;
        if (family == 0xb0) next_values[4U + static_cast<std::size_t>(target)] = values[0];
        else {
          // Mutation precedes a self recall. Equal selector WORDS produce
          // equal updated words, even at the zero-underflow boundary; they
          // need not retain the old numeric value or its negative class.
          next_values[0] = target == reg && reg <= 6 ? Cell{Equal}
              : values[4U + static_cast<std::size_t>(target)];
          next_values[1] = values[0]; next_values[2] = values[1]; next_values[3] = values[2];
          next_values[entry_x] = values[0];
        }
        for (const auto next : flow.execution_successors[state]) pending.emplace_back(next, next_values);
      }
      continue;
    } else if (code == 0x51 || code == 0x52 || code == 0x53 ||
               (family >= 0x70 && family <= 0xe0) || opcode_by_code(code).takes_address) {
      if (family >= 0x70 && family <= 0xe0 &&
          (reg == 15 || !equal(values[4U + static_cast<std::size_t>(reg)])))
        return fail("translated control selector");
      if (family >= 0x70 && family <= 0xe0 && reg <= 6)
        values[4U + static_cast<std::size_t>(reg)] = Equal;
      if (code == 0x5d || code == 0x5b || code == 0x58 || code == 0x5a) {
        const int counter = code == 0x5d ? 0 : code == 0x5b ? 1 : code == 0x58 ? 2 : 3;
        if (!equal(values[4U + static_cast<std::size_t>(counter)])) return fail("translated loop counter");
        values[4U + static_cast<std::size_t>(counter)] = Equal;
      } else if ((code >= 0x57 && code <= 0x5e) || family == 0x70 || family == 0x90 ||
                 family == 0xc0 || family == 0xe0) {
        if (!equal(values[0]) && values[0] != Translated)
          return fail("unproved branch truth value");
        positive_condition = values[0] == Translated;
      }
      // A direct conditional may restore X2 on one arm. Do not manufacture
      // equality of that hidden value; differing X2 must be proved overwritten.
      if (opcode_by_code(code).x2_effect == X2Effect::Restores && !equal(values[entry_x]))
        return fail("control observes hidden entry buffer");
      if (opcode_by_code(code).x2_effect == X2Effect::Affects) values[entry_x] = values[0];
    } else if (code >= 0 && code <= 9) {
      if (!automatic_entry_lift[state]) {
        if (!equal(values[0]) || !equal(values[entry_x])) return fail("live decimal entry difference");
        values[0] = Equal;
      } else {
        const auto old = values;
        values[0] = Equal; values[1] = old[0]; values[2] = old[1]; values[3] = old[2];
        values[entry_x] = Equal;
      }
    } else if (code >= 0x0a && code <= 0x0c) {
      const bool recalled_sign = code == 0x0b && !predecessors[state].empty() &&
          std::all_of(predecessors[state].begin(), predecessors[state].end(), [&](std::size_t previous) {
            const int prior = items[flow.execution_states[previous].item_index].opcode;
            return prior >= 0x60 && prior <= 0x6e;
          });
      if (!equal(values[0]) || (!recalled_sign && !equal(values[entry_x])))
        return fail("decimal edit observes hidden difference");
      values[0] = Equal; values[entry_x] = Equal;
    } else if (code == 0x0d || code == 0x0e || code == 0x0f || code == 0x14 || code == 0x25) {
      const auto old = values;
      if (code == 0x0d) { values[0] = Equal; values[entry_x] = old[0]; }
      if (code == 0x0e || code == 0x0f) {
        values[0] = code == 0x0e ? old[0] : old[last_x];
        values[1] = old[0]; values[2] = old[1]; values[3] = old[2]; values[entry_x] = old[0];
      }
      if (code == 0x14) std::swap(values[0], values[1]);
      if (code == 0x25) { values[0] = old[1]; values[1] = old[2]; values[2] = old[3]; values[3] = old[0]; }
    } else {
      const auto old = values;
      const auto& info = opcode_by_code(code);
      if (code == 0x3b) return fail("random-state effects are outside this proof");
      const bool binary = info.stack_effect == StackEffect::ConsumeYDrop || info.stack_effect == StackEffect::ConsumeYKeep;
      Cell output = Equal;
      if (binary) {
        if (equal(old[0]) && equal(old[1])) output = Equal;
        else if (code == 0x10 && ((old[0] == Translated && equal(old[1])) ||
                                  (old[1] == Translated && equal(old[0])))) {
          // Every Translated producer already established the positive band.
          // A scalar join here may also contain the unrelated negative seed;
          // it must not erase the relational path's stronger invariant.
          const auto shifted_slot = old[0] == Translated ? 0U : 1U;
          const auto term = value_at(state, 1U - shifted_slot);
          auto increment = term.enclosure;
          if (!increment && term.range)
            increment = OrdinaryDecimalEnclosure{term.range->lower * scale, term.range->upper * scale};
          if (!term.ordinary || !increment) return fail("addition has no independent ordinary increment bound");
          // Carry the source interval with the relational value, not with a
          // scalar join that also contains the unrelated negative sentinel.
          // In this band one ULP is one millionth. Two outward units enclose
          // normal decimal addition; band preservation is checked afterwards.
          const OrdinaryDecimalEnclosure sum{
              old[shifted_slot].source.lower + increment->lower - 2,
              old[shifted_slot].source.upper + increment->upper + 2};
          if (!constrain({.ordinary = true, .enclosure = sum}))
            return fail("sum crosses an exponent boundary");
          output = Cell{Translated, sum};
        } else if (code == 0x11 && old[0] == Translated && old[1] == Translated) {
          output = Equal;
        } else if (code == 0x36) {
          if (!(old[0] == Translated && old[1] == Translated)) {
            const auto shifted_slot = old[0] == Translated ? 0U : 1U;
            const auto other = 1U - shifted_slot;
            if ((old[shifted_slot] != Translated) ||
                !(old[other] == EqualNegative || (equal(old[other]) && negative_at(state, other))))
              return fail("maximum crosses an unproved sentinel");
          }
          if (old[0] == Translated && old[1] == Translated)
            output = Cell{Translated, {std::max(old[0].source.lower, old[1].source.lower),
                                      std::max(old[0].source.upper, old[1].source.upper)}};
          else output = old[old[0] == Translated ? 0U : 1U];
          used_maximum = true;
        } else return fail("non-order consumer of translated value");
        values[0] = output;
        if (info.stack_effect == StackEffect::ConsumeYDrop) {
          values[1] = old[2]; values[2] = old[3]; values[3] = old[3];
        }
      } else if (!equal(old[0])) {
        if (code == 0x32 && old[0] == Translated) output = Equal;
        else return fail("unproved unary translated consumer");
        values[0] = output;
      } else if (info.stack_effect == StackEffect::Shifts) {
        values[0] = Equal; values[1] = old[0]; values[2] = old[1]; values[3] = old[2];
      } else if (info.stack_effect == StackEffect::Exposes) {
        values[0] = old[1]; values[1] = old[2]; values[2] = old[3]; values[3] = old[3];
      } else {
        // Equality survives a deterministic unary operation; negativity does
        // not (abs, square, sign and other projections have different images).
        values[0] = Equal;
      }
      if (info.stack_effect == StackEffect::Barrier || info.stack_effect == StackEffect::Unknown ||
          info.x2_effect == X2Effect::Unknown) {
        if (!fully_equal(old)) return fail("opaque state effect");
      }
      if (info.x2_effect == X2Effect::Restores && !equal(old[entry_x]))
        return fail("live hidden entry-buffer read");
      if (info.x2_effect == X2Effect::Affects) values[entry_x] = old[0];
      if (opcode_saves_physical_x1(code)) values[last_x] = old[0];
    }
    if (flow.execution_successors[state].empty() &&
        !(code == 0x50 && item.stop_disposition == StopDisposition::Terminal &&
          role(item, kTypedDisplayObservationRole)) && !fully_equal(values))
      return fail("unproved exit with live differences");
    for (const auto& edge : flow.execution_edges[state]) {
      const auto next = edge.target_state;
      if (positive_condition) {
        // These MK-61 predicates fall through when their named condition is
        // true and take the address when false. Both translated values are
        // strictly positive, including the negative-sentinel max join.
        const bool truth = code == 0x57 || code == 0x59 || family == 0x70 || family == 0x90;
        const bool fallthrough = edge.kind == PostLayoutExecutionEdgeKind::Fallthrough;
        if (truth != fallthrough) continue;
      }
      auto next_values = values;
      if (const auto& conditional = opcode_by_code(code).conditional_x2_effect; conditional) {
        const auto effect = edge.kind == PostLayoutExecutionEdgeKind::Fallthrough
            ? conditional->fallthrough : conditional->jump;
        if (effect == X2Effect::Affects) next_values[entry_x] = values[0];
        if (effect == X2Effect::Restores || effect == X2Effect::Unknown)
          return fail("unsupported conditional hidden effect");
      }
      for (std::size_t slot = 0; slot < 4; ++slot)
        if (equal(next_values[slot]) && negative_at(next, slot)) next_values[slot] = EqualNegative;
      pending.emplace_back(next, next_values);
    }
  }
  if (!used_maximum || (minimum_delta == 0 && maximum_delta == 0)) return reject("no nontrivial order-only family");
  result.proved = true;
  result.minimum_seed = seed + minimum_delta; result.maximum_seed = seed + maximum_delta;
  return result;
}
} // namespace mkpro::core
