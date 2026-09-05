#include "mkpro/core/passes/indirect_selector_seed_reuse.hpp"

#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/stack_value_equivalence.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {
namespace {

// K OR writes the fixed positive 8.<seven BCD nibbles> representation.
// Unlike an ordinary interval, this domain admits non-decimal fractional
// nibbles. A mutating memory selector truncates that tail before decrementing
// 8 to 7. Do not extend this fact to AND, arithmetic, or error/supernumbers by
// interpreting their display as a C++ floating-point number.
bool produces_seed_class(const MachineItem& item, int seed) {
  return seed == 8 && item.kind == MachineItemKind::Op &&
         item.opcode == 0x38 && !item.raw && !item.manual_interaction.has_value();
}

bool indirect_memory(int opcode) {
  return (opcode >= 0xb0 && opcode <= 0xbe) ||
         (opcode >= 0xd0 && opcode <= 0xde);
}

bool indirect_flow(int opcode) {
  const int family = opcode & 0xf0;
  return (opcode & 0x0f) <= 14 &&
         (family == 0x70 || family == 0x80 || family == 0x90 ||
          family == 0xa0 || family == 0xc0 || family == 0xe0);
}

bool direct_store(int opcode) { return opcode >= 0x40 && opcode <= 0x4e; }
bool direct_recall(int opcode) { return opcode >= 0x60 && opcode <= 0x6e; }

bool counter_loop(int opcode, int reg) {
  constexpr std::array<int, 4> loops = {0x5d, 0x5b, 0x58, 0x5a};
  return reg >= 0 && reg < 4 && opcode == loops.at(static_cast<std::size_t>(reg));
}

bool touches_register(const MachineItem& item, std::size_t item_index, int reg,
                      const AuthoritativePostLayoutControlFlow& flow) {
  if ((direct_store(item.opcode) || direct_recall(item.opcode) ||
       indirect_memory(item.opcode) || indirect_flow(item.opcode)) &&
      (item.opcode & 0x0f) == reg)
    return true;
  if (counter_loop(item.opcode, reg))
    return true;
  if (indirect_memory(item.opcode)) {
    const auto targets = flow.indirect_memory_targets.find(item_index);
    return targets == flow.indirect_memory_targets.end() ||
           std::find(targets->second.begin(), targets->second.end(), reg) !=
               targets->second.end();
  }
  return false;
}

// Physical targets, overlays and layout-bound selector charges belong to a
// later transactional optimizer. This pass changes positions only while all
// flow references are symbolic; it never edits a frozen numerical address.
bool symbolic_geometry(const std::vector<MachineItem>& items) {
  for (const MachineItem& item : items) {
    if (item.raw)
      return false;
    if (item.kind == MachineItemKind::Address &&
        (!std::holds_alternative<std::string>(item.target) ||
         item.formal_opcode.has_value() || !item.roles.empty()))
      return false;
    if (item.indirect_flow_targets.has_value()) {
      for (const IrTarget& target : *item.indirect_flow_targets) {
        if (!std::holds_alternative<std::string>(target))
          return false;
      }
    }
    if (std::any_of(item.roles.begin(), item.roles.end(), [](const CellRole& role) {
          return role.starts_with("late-decimal-selector-") ||
                 role.starts_with("finalization-cell-origin:") ||
                 role == kResumableErrorPaddingRole;
        }))
      return false;
  }
  return true;
}

using Predecessors = std::vector<std::vector<std::size_t>>;

std::set<std::size_t> external_states(const AuthoritativePostLayoutControlFlow& flow) {
  std::set<std::size_t> result;
  for (std::size_t index = 0; index < flow.execution_states.size(); ++index) {
    const auto& state = flow.execution_states.at(index);
    for (const auto& entry : flow.external_entries) {
      if (entry.entry.item_index != state.item_index ||
          entry.return_stack.size() != state.return_stack.size())
        continue;
      bool same = true;
      for (std::size_t slot = 0; slot < state.return_stack.size(); ++slot)
        same = same && entry.return_stack.at(slot).address == state.return_stack.at(slot);
      if (same)
        result.insert(index);
    }
  }
  return result;
}

// Every call context reaching the initialization must have passed a suitable
// producer without observing/redefining the selector. Cycles with no proved
// dominating seed and independent manual entries are deliberately rejected.
std::optional<std::set<std::size_t>> dominating_producers(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow, const Predecessors& predecessors,
    const std::set<std::size_t>& entries, std::size_t literal, std::size_t store,
    int reg, int seed, std::size_t maximum_producers, std::string& failure) {
  std::vector<unsigned char> color(flow.execution_states.size(), 0);
  std::set<std::size_t> producers;
  const auto visit = [&](const auto& self, std::size_t index, std::size_t depth) -> bool {
    if (depth > 256 || entries.contains(index)) {
      failure = depth > 256 ? "predecessor bound" : "independent entry";
      return false;
    }
    if (color.at(index) == 2)
      return true;
    if (color.at(index) == 1) {
      failure = "unseeded predecessor cycle";
      return false;
    }
    color.at(index) = 1;
    if (predecessors.at(index).empty()) {
      failure = "predecessor has no seed";
      return false;
    }
    for (const std::size_t before : predecessors.at(index)) {
      const std::size_t item_index = flow.execution_states.at(before).item_index;
      const MachineItem& item = items.at(item_index);
      if (produces_seed_class(item, seed)) {
        producers.insert(item_index);
        if (producers.size() > maximum_producers) {
          failure = "producer count exceeds local repayment limit";
          return false;
        }
        continue;
      }
      if (item_index == literal || item_index == store || item.raw ||
          item.manual_interaction.has_value() || item.opcode == 0x50 ||
          touches_register(item, item_index, reg, flow)) {
        failure = "observed/redefined selector or entry barrier at item " +
                  std::to_string(item_index) + " opcode " + std::to_string(item.opcode);
        return false;
      }
      if (!self(self, before, depth + 1U))
        return false;
    }
    color.at(index) = 2;
    return true;
  };
  bool reached = false;
  for (std::size_t index = 0; index < flow.execution_states.size(); ++index) {
    const auto& state = flow.execution_states.at(index);
    if (state.item_index == store) {
      if (entries.contains(index) || predecessors.at(index).empty())
        return std::nullopt;
      for (const std::size_t before : predecessors.at(index)) {
        if (flow.execution_states.at(before).item_index != literal)
          return std::nullopt;
      }
    }
    if (state.item_index != literal)
      continue;
    // Removing a digit only models a fresh entry, never an extension of an
    // existing mantissa/exponent or the user's unfinished manual input.
    for (const std::size_t before : predecessors.at(index)) {
      const MachineItem& previous = items.at(flow.execution_states.at(before).item_index);
      if (previous.opcode <= 0x0c || previous.opcode == 0x50 ||
          previous.manual_interaction.has_value())
        return std::nullopt;
    }
    if (!visit(visit, index, 0))
      return std::nullopt;
    reached = true;
  }
  return reached && !producers.empty()
             ? std::optional<std::set<std::size_t>>(producers) : std::nullopt;
}

enum class RegisterRelation { Equal, Speculative, SelectorEquivalent };

struct EqualityState {
  StackValueEqualityState stack{{true, true, true, true}, true};
  RegisterRelation reg = RegisterRelation::Equal;
  bool decimal_entry = false;

  bool operator==(const EqualityState& other) const {
    return stack.stack_equal == other.stack.stack_equal &&
           stack.x2_equal == other.stack.x2_equal && stack.x1_equal == other.stack.x1_equal &&
           reg == other.reg &&
           decimal_entry == other.decimal_entry;
  }
};

// Exact finite product: six equality bits, entry state and three register
// relations. No widening to an optimistic numeric interval is permitted.
bool continuation_equivalent(const std::vector<MachineItem>& items,
                             const AuthoritativePostLayoutControlFlow& flow,
                             const std::set<std::size_t>& producers, std::size_t literal,
                             std::size_t store, int reg, std::string& failure) {
  std::vector<std::vector<EqualityState>> seen(flow.execution_states.size());
  std::deque<std::pair<std::size_t, EqualityState>> work;
  const auto enqueue = [&](std::size_t index, const EqualityState& state) {
    auto& states = seen.at(index);
    if (std::find(states.begin(), states.end(), state) == states.end()) {
      states.push_back(state);
      work.emplace_back(index, state);
    }
  };
  for (std::size_t index = 0; index < flow.execution_states.size(); ++index) {
    if (!producers.contains(flow.execution_states.at(index).item_index))
      continue;
    EqualityState state;
    state.reg = RegisterRelation::Speculative;
    for (const std::size_t successor : flow.execution_successors.at(index))
      enqueue(successor, state);
  }
  bool canonicalized = false;
  std::size_t processed = 0;
  while (!work.empty()) {
    if (++processed > 100000U)
      return false;
    auto [index, state] = work.front();
    work.pop_front();
    const auto& execution = flow.execution_states.at(index);
    const MachineItem& item = items.at(execution.item_index);
    const int opcode = item.opcode;
    failure = "continuation item " + std::to_string(execution.item_index) +
              " opcode " + std::to_string(opcode) + " equal=" +
              std::to_string(state.stack.stack_equal.at(0)) +
              std::to_string(state.stack.stack_equal.at(1)) +
              std::to_string(state.stack.stack_equal.at(2)) +
              std::to_string(state.stack.stack_equal.at(3)) +
              "/X2=" + std::to_string(state.stack.x2_equal) +
              "/register=" + std::to_string(static_cast<int>(state.reg));
    if (item.raw ||
        (item.manual_interaction.has_value() && !stack_values_fully_equal(state.stack)))
      return false;

    if (execution.item_index == literal) {
      if (state.reg == RegisterRelation::Equal)
        return false;
      state.stack = StackValueEqualityState{{false, false, false, false}, false};
      state.decimal_entry = false;
    } else if (execution.item_index == store) {
      if (state.reg == RegisterRelation::Equal)
        return false;
      state.reg = RegisterRelation::SelectorEquivalent;
      state.decimal_entry = false;
    } else {
      const bool was_equal = stack_values_fully_equal(state.stack);
      if (opcode == 0x50 && item.stop_disposition == StopDisposition::Terminal) {
        // A typed halt has no source continuation. Its result/display pair
        // must agree, as must the saved X2 and the canonical selector. Z/T
        // cannot be read by later source code and are not displayed outputs.
        // This does not apply to a resumable stop or an untyped/raw command.
        if (state.reg != RegisterRelation::Equal ||
            !state.stack.stack_equal.at(0) || !state.stack.stack_equal.at(1) ||
            !state.stack.x2_equal)
          return false;
        continue;
      }
      if (opcode == 0x50 &&
          (!was_equal || item.stop_disposition != StopDisposition::Resumable))
        return false;
      // A typed prompt observes the stack, not every allocated scratch
      // register. Keep a differing register relation alive across its exact
      // resume edge: a later ordinary/aliased read still rejects the rewrite.
      // Equal stacks plus the same operator input remain equal at validated
      // manual phase anchors. Never use input to erase an unequal stack.
      if (state.reg != RegisterRelation::Equal) {
        if (direct_recall(opcode) && (opcode & 0x0f) == reg)
          return false;
        if (counter_loop(opcode, reg) ||
            (indirect_flow(opcode) && (opcode & 0x0f) == reg))
          return false;
        if (indirect_memory(opcode)) {
          const auto targets = flow.indirect_memory_targets.find(execution.item_index);
          if (targets == flow.indirect_memory_targets.end())
            return false;
          if (std::find(targets->second.begin(), targets->second.end(), reg) !=
              targets->second.end())
            return false;
          if ((opcode & 0x0f) == reg) {
            if (state.reg != RegisterRelation::SelectorEquivalent ||
                std::find(targets->second.begin(), targets->second.end(), 7) ==
                    targets->second.end())
              return false;
            state.reg = RegisterRelation::Equal;
            canonicalized = true;
          }
        }
        if (direct_store(opcode) && (opcode & 0x0f) == reg) {
          if (!state.stack.stack_equal.at(0))
            return false;
          state.reg = RegisterRelation::Equal;
        }
      }

      if (!was_equal) {
        if (opcode == 0x0a && !state.decimal_entry &&
            state.stack.stack_equal.at(0) && state.stack.x2_equal) {
          // Closed-entry dot restores an equal X2 into an already equal X.
          // It cannot observe the possibly different Z/T below the pair.
        } else if (opcode >= 0x00 && opcode <= 0x09) {
          if (transfer_decimal_digit_equality(state.stack, state.decimal_entry) ==
              StackValueEqualityTransfer::Rejected)
            return false;
        } else if (opcode == 0x52 || opcode == 0x51 || opcode == 0x53 ||
                   indirect_flow(opcode) || opcode_by_code(opcode).takes_address) {
          // Register/return-stack transitions are identical. A condition may
          // only inspect an equal X. No numeric address or empty return is
          // admitted by the enclosing symbolic CFG proof.
          if (opcode != 0x52 && opcode != 0x51 && opcode != 0x53 &&
              opcode != 0x80 + (opcode & 0x0f) &&
              opcode != 0xa0 + (opcode & 0x0f) &&
              !state.stack.stack_equal.at(0))
            return false;
        } else {
          // K random depends on stack state, not just its visible output.
          if (opcode == 0x3b)
            return false;
          StackValueEqualityStepKind kind = StackValueEqualityStepKind::Plain;
          if (direct_recall(opcode) || (opcode >= 0xd0 && opcode <= 0xde))
            kind = StackValueEqualityStepKind::Recall;
          else if (direct_store(opcode) || (opcode >= 0xb0 && opcode <= 0xbe))
            kind = StackValueEqualityStepKind::Store;
          if (transfer_stack_value_equality(state.stack, opcode, kind) ==
              StackValueEqualityTransfer::Rejected)
            return false;
        }
      }
      state.decimal_entry = opcode <= 0x0c;
      if (producers.contains(execution.item_index))
        state.reg = RegisterRelation::Speculative;
    }
    if (state.reg == RegisterRelation::Equal && stack_values_fully_equal(state.stack))
      continue;
    if (flow.execution_successors.at(index).empty())
      return false;
    for (const std::size_t successor : flow.execution_successors.at(index))
      enqueue(successor, state);
  }
  return canonicalized;
}

} // namespace

PassResult indirect_selector_seed_reuse(const std::vector<IrOp>& ops,
                                        const PassContext& context) {
  const auto unchanged = [&] { return PassResult{.ops = ops}; };
  std::vector<std::size_t> candidates;
  for (std::size_t index = 0; index + 1U < ops.size(); ++index) {
    const IrOp& digit = ops.at(index);
    const IrOp& store = ops.at(index + 1U);
    if (digit.kind == IrKind::Plain && digit.opcode == 8 &&
        store.kind == IrKind::Store && store.opcode >= 0x40 && store.opcode <= 0x43 &&
        !has_rewrite_barrier(digit) && !has_rewrite_barrier(store) &&
        digit.meta.roles.empty() && store.meta.roles.empty() &&
        !digit.meta.tactic.has_value() && !store.meta.tactic.has_value())
      candidates.push_back(index);
  }
  if (candidates.empty() || std::none_of(ops.begin(), ops.end(), [](const IrOp& op) {
        return op.kind == IrKind::Plain && op.opcode == 0x38;
      }))
    return unchanged();
  const std::vector<MachineItem> items = lower_ir_to_machine(ops);
  if (!symbolic_geometry(items))
    return unchanged();
  const AuthoritativePostLayoutControlFlow flow = build_post_layout_control_flow(items);
  if (!flow.proved || flow.execution_states.size() != flow.execution_successors.size())
    return unchanged();
  Predecessors predecessors(flow.execution_states.size());
  for (std::size_t index = 0; index < flow.execution_successors.size(); ++index) {
    for (const std::size_t successor : flow.execution_successors.at(index))
      predecessors.at(successor).push_back(index);
  }
  const std::set<std::size_t> entries = external_states(flow);
  std::vector<std::size_t> item_for_op;
  item_for_op.reserve(ops.size());
  std::size_t next_item = 0;
  for (const IrOp& op : ops) {
    item_for_op.push_back(next_item);
    next_item += op.kind == IrKind::Label ? 1U : static_cast<std::size_t>(cells_per_op(op));
  }
  const bool trace = std::getenv("MKPRO_NATIVE_TRACE_SELECTOR_SEED_REUSE") != nullptr;
  for (const std::size_t candidate : candidates) {
    const std::size_t literal = item_for_op.at(candidate);
    const std::size_t store = item_for_op.at(candidate + 1U);
    const int reg = items.at(store).opcode & 0x0f;
    std::string failure;
    const auto producers = dominating_producers(
        items, flow, predecessors, entries, literal, store, reg, 8,
        context.options.allow_size_neutral_selector_seed_reuse ? 2U : 1U, failure);
    const bool proved = producers.has_value() &&
        continuation_equivalent(items, flow, *producers, literal, store, reg, failure);
    if (trace)
      std::cerr << "[indirect-selector-seed-reuse] R" << reg
                << " literal-item=" << literal << " producers="
                << (producers.has_value() ? std::to_string(producers->size()) : "unproved")
                << " continuation=" << (proved ? "proved" : "unproved")
                << " reason=" << (proved ? "accepted" : failure) << '\n';
    if (!proved)
      continue;
    std::set<std::size_t> insert_after;
    for (std::size_t index = 0; index < ops.size(); ++index) {
      if (!producers->contains(item_for_op.at(index)))
        continue;
      std::size_t end = index;
      // Preserve the producer's existing store continuation for downstream
      // helper sharing. Crossing only adjacent non-observing stores is exact:
      // X/X2 are unchanged and, in a symbolic CFG, no label can enter the
      // middle of this chain. The selector itself was proved untouched.
      while (end + 1U < ops.size()) {
        const IrOp& next = ops.at(end + 1U);
        if (next.kind != IrKind::Store || (next.opcode & 0x0f) == reg ||
            has_rewrite_barrier(next) || is_display_focus_sensitive(next))
          break;
        ++end;
      }
      insert_after.insert(end);
    }
    std::vector<IrOp> result;
    result.reserve(ops.size() - 2U + producers->size());
    for (std::size_t index = 0; index < ops.size(); ++index) {
      if (index != candidate && index != candidate + 1U)
        result.push_back(ops.at(index));
      if (insert_after.contains(index))
        result.push_back(ops.at(candidate + 1U));
    }
    // Labels retain command identity. A newly inserted store is reached only
    // by fallthrough from the producer, never by an old edge to its successor.
    if (!build_post_layout_control_flow(lower_ir_to_machine(result)).proved)
      continue;
    return PassResult{
        .ops = std::move(result),
        .applied = 1,
        .optimizations = {{
            .name = "indirect-selector-seed-reuse",
            .detail = "Reused a packed producer as the initial value of R" +
                      std::to_string(reg) + "; proved every incoming call context, "
                      "speculative-store liveness, first-use truncation, and stack/X2 "
                      "convergence before observation; " +
                      (producers->size() == 1U
                           ? "removed one cell before layout."
                           : "two producer stores are locally size-neutral and require "
                             "strict full-layout repayment before automatic selection."),
        }},
    };
  }
  return unchanged();
}

IrPass indirect_selector_seed_reuse_pass() {
  return IrPass{.name = "indirect-selector-seed-reuse",
                .run = indirect_selector_seed_reuse, .layout_safe = false};
}

} // namespace mkpro::core::passes
