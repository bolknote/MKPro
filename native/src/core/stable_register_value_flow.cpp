#include "mkpro/core/stable_register_value_flow.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <stdexcept>

namespace mkpro::core {

namespace {

constexpr std::size_t kTrackedRegisters = 8;  // R7..Re

bool is_indirect_flow_opcode(int opcode) {
  const int family = opcode & 0xf0;
  return family == 0x70 || family == 0x80 || family == 0x90 || family == 0xa0 ||
         family == 0xc0 || family == 0xe0;
}

bool is_indirect_memory_opcode(int opcode) {
  const int family = opcode & 0xf0;
  return family == 0xb0 || family == 0xd0;
}

std::string tracked_register_name(std::size_t slot) {
  static const char* const names[kTrackedRegisters] = {"7", "8", "9", "a",
                                                       "b", "c", "d", "e"};
  return names[slot];
}

// Number-entry buffer: absent while no entry is in progress, a digit string
// while an exact integer entry is being typed, and poisoned when the entry
// contains anything the analysis does not model (fraction, exponent, an
// in-entry sign change, or an ambiguous join).
struct EntryBuffer {
  bool poisoned = false;
  std::optional<std::string> digits;

  bool operator==(const EntryBuffer&) const = default;
};

struct AbstractState {
  std::optional<std::string> x;
  EntryBuffer entry;
  std::array<std::optional<std::string>, kTrackedRegisters> registers;

  bool operator==(const AbstractState&) const = default;
};

std::optional<std::string> join_value(const std::optional<std::string>& left,
                                      const std::optional<std::string>& right) {
  if (left.has_value() && right.has_value() && *left == *right)
    return left;
  return std::nullopt;
}

EntryBuffer join_entry(const EntryBuffer& left, const EntryBuffer& right) {
  if (left == right)
    return left;
  return EntryBuffer{.poisoned = true};
}

AbstractState join_state(const AbstractState& left, const AbstractState& right) {
  AbstractState result;
  result.x = join_value(left.x, right.x);
  result.entry = join_entry(left.entry, right.entry);
  for (std::size_t slot = 0; slot < kTrackedRegisters; ++slot)
    result.registers[slot] = join_value(left.registers[slot], right.registers[slot]);
  return result;
}

std::optional<std::string> canonical_integer(const std::string& digits) {
  if (digits.empty() || digits.size() > 8)
    return std::nullopt;
  try {
    return std::to_string(std::stoll(digits));
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<std::string> negated_value(const std::optional<std::string>& value) {
  if (!value.has_value() || value->empty())
    return std::nullopt;
  if (value->front() == '-')
    return value->substr(1U);
  if (*value == "0")
    return value;
  return "-" + *value;
}

std::optional<std::string> selector_write_back(const std::string& register_name_value,
                                               const std::optional<std::string>& value,
                                               IndirectOperationKind kind,
                                               AddressSpaceModel model) {
  if (!value.has_value())
    return std::nullopt;
  try {
    const std::optional<IndirectAddressEvaluation> evaluated =
        evaluate_indirect_address(register_name_value, *value, kind, model);
    if (!evaluated.has_value())
      return std::nullopt;
    return evaluated->result_value;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// Transfer of one executed command over the abstract state. Everything not
// explicitly modeled forgets X and ends any number entry while keeping the
// tracked registers, which is sound because only stores, indirect memory
// stores, and the loop opcodes write data registers, and all of those are
// handled explicitly.
AbstractState transfer(const AbstractState& input, const MachineItem& item,
                       std::size_t item_index,
                       const AuthoritativePostLayoutControlFlow& flow,
                       AddressSpaceModel model) {
  AbstractState state = input;
  const int opcode = item.opcode;

  if (opcode >= 0x00 && opcode <= 0x09) {
    if (state.entry.poisoned) {
      state.x.reset();
    } else {
      std::string digits = state.entry.digits.value_or("");
      digits.push_back(static_cast<char>('0' + opcode));
      state.entry.digits = digits;
      state.x = canonical_integer(digits);
      if (!state.x.has_value())
        state.entry.poisoned = true;
    }
    return state;
  }

  const bool entry_active = state.entry.digits.has_value() || state.entry.poisoned;
  state.entry = EntryBuffer{};

  if (opcode == 0x0a || opcode == 0x0c) {  // '.' and ВП extend an entry
    state.entry.poisoned = true;
    state.x.reset();
    return state;
  }
  if (opcode == 0x0b) {  // /-/ negates X, but an in-entry sign is not modeled
    if (entry_active) {
      state.entry.poisoned = true;
      state.x.reset();
    } else {
      state.x = negated_value(state.x);
    }
    return state;
  }
  if (opcode == 0x0e) {  // В↑ keeps X and terminates the entry
    return state;
  }
  if (opcode >= 0x40 && opcode <= 0x4e) {  // X->П r
    const int register_index_value = opcode - 0x40;
    if (register_index_value >= 7)
      state.registers[static_cast<std::size_t>(register_index_value - 7)] = state.x;
    return state;
  }
  if (opcode >= 0x60 && opcode <= 0x6e) {  // П->X r
    const int register_index_value = opcode - 0x60;
    state.x = register_index_value >= 7
                  ? state.registers[static_cast<std::size_t>(register_index_value - 7)]
                  : std::nullopt;
    return state;
  }
  if (opcode == 0x50 || opcode == 0x51 || opcode == 0x52 || opcode == 0x53 ||
      opcode == 0x54) {  // С/П, БП, В/О, ПП, К НОП keep X
    return state;
  }
  if (opcode >= 0x57 && opcode <= 0x5e) {  // conditionals and FL counters keep X
    return state;
  }
  if (is_indirect_flow_opcode(opcode)) {
    const int selector = opcode & 0x0f;
    if (selector >= 7 && selector <= 0x0e) {
      const std::size_t slot = static_cast<std::size_t>(selector - 7);
      state.registers[slot] = selector_write_back(
          tracked_register_name(slot), state.registers[slot],
          IndirectOperationKind::Flow, model);
    }
    return state;
  }
  if (is_indirect_memory_opcode(opcode)) {
    const int selector = opcode & 0x0f;
    if (selector >= 7 && selector <= 0x0e) {
      const std::size_t slot = static_cast<std::size_t>(selector - 7);
      state.registers[slot] = selector_write_back(
          tracked_register_name(slot), state.registers[slot],
          IndirectOperationKind::Memory, model);
    }
    if ((opcode & 0xf0) == 0xb0) {  // К X->П r may write a tracked register
      const auto targets = flow.indirect_memory_targets.find(item_index);
      if (targets == flow.indirect_memory_targets.end()) {
        for (std::size_t slot = 0; slot < kTrackedRegisters; ++slot)
          state.registers[slot].reset();
      } else {
        for (const int target : targets->second) {
          if (target >= 7 && target <= 0x0e)
            state.registers[static_cast<std::size_t>(target - 7)].reset();
        }
      }
    } else {  // К П->X r replaces X with an untracked memory value
      state.x.reset();
    }
    return state;
  }

  state.x.reset();
  return state;
}

std::optional<std::size_t> previous_executable_item(const std::vector<MachineItem>& items,
                                                    std::size_t before) {
  for (std::size_t item_index = before; item_index-- > 0;) {
    if (items.at(item_index).kind != MachineItemKind::Label)
      return item_index;
  }
  return std::nullopt;
}

}  // namespace

StableRegisterValueFlow analyze_stable_register_value_flow(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& flow, AddressSpaceModel model) {
  StableRegisterValueFlow result;
  const auto fail = [&](std::string reason) {
    result.reasons.push_back(std::move(reason));
    return result;
  };
  if (!flow.proved || flow.execution_states.empty() ||
      flow.execution_successors.size() != flow.execution_states.size()) {
    return fail("stable-register value flow requires an authoritative execution graph");
  }
  for (const PostLayoutExecutionState& state : flow.execution_states) {
    if (state.item_index >= items.size() ||
        items.at(state.item_index).kind != MachineItemKind::Op) {
      return fail("an execution state does not reference an ordinary command");
    }
  }

  // Root classification. The main entry starts from the preload values;
  // resumable stops keep the registers of their stop states and forget X;
  // any other entry kind is not modeled.
  AbstractState main_entry_state;
  for (const PreloadReport& preload : preloads) {
    const int register_index_value = register_index(preload.register_name);
    if (register_index_value >= 7 && register_index_value <= 0x0e)
      main_entry_state.registers[static_cast<std::size_t>(register_index_value - 7)] =
          preload.value;
  }

  for (std::size_t state = 0; state < flow.execution_successors.size(); ++state) {
    for (const std::size_t successor : flow.execution_successors.at(state)) {
      if (successor >= flow.execution_states.size())
        return fail("the execution graph references a missing state");
    }
  }

  // Match every external entry to the execution states carrying that exact
  // command identity and return stack.
  const auto matching_states = [&](const PostLayoutExternalEntryState& entry) {
    std::vector<std::size_t> matched;
    for (std::size_t state = 0; state < flow.execution_states.size(); ++state) {
      const PostLayoutExecutionState& candidate = flow.execution_states.at(state);
      if (candidate.item_index != entry.entry.item_index ||
          candidate.return_stack.size() != entry.return_stack.size()) {
        continue;
      }
      bool same_stack = true;
      for (std::size_t slot = 0; slot < entry.return_stack.size(); ++slot) {
        if (candidate.return_stack.at(slot) != entry.return_stack.at(slot).address) {
          same_stack = false;
          break;
        }
      }
      if (same_stack)
        matched.push_back(state);
    }
    return matched;
  };

  // Resume entries of ordinary stops keep the registers of their stop states,
  // so they are seeded lazily from the stop's out values inside the fixpoint.
  std::map<std::size_t, std::vector<std::size_t>> resume_states_by_stop_item;
  std::vector<std::pair<const PostLayoutExternalEntryState*, std::vector<std::size_t>>>
      seeded_entries;
  for (const PostLayoutExternalEntryState& entry : flow.external_entries) {
    if (entry.entry.item_index >= items.size())
      return fail("an external entry references a missing item");
    std::vector<std::size_t> matched = matching_states(entry);
    switch (entry.kind) {
    case ExternalEntryKind::Main:
    case ExternalEntryKind::ManualSingleStep:
    case ExternalEntryKind::ManualContinuous:
      seeded_entries.emplace_back(&entry, std::move(matched));
      break;
    case ExternalEntryKind::ResumableStop: {
      const std::optional<std::size_t> stop_item =
          previous_executable_item(items, entry.entry.item_index);
      if (!stop_item.has_value() || items.at(*stop_item).opcode != 0x50)
        return fail("a resumable entry does not continue an ordinary stop");
      std::vector<std::size_t>& targets = resume_states_by_stop_item[*stop_item];
      targets.insert(targets.end(), matched.begin(), matched.end());
      break;
    }
    }
  }

  std::vector<std::optional<AbstractState>> in_value(flow.execution_states.size());
  std::vector<std::optional<AbstractState>> out_value(flow.execution_states.size());

  std::deque<std::size_t> worklist;
  std::vector<char> queued(flow.execution_states.size(), 0);
  const auto enqueue = [&](std::size_t state) {
    if (!queued.at(state)) {
      queued.at(state) = 1;
      worklist.push_back(state);
    }
  };
  const auto merge_in = [&](std::size_t state, const AbstractState& incoming) {
    std::optional<AbstractState>& current = in_value.at(state);
    const AbstractState next =
        current.has_value() ? join_state(*current, incoming) : incoming;
    if (!current.has_value() || !(*current == next)) {
      current = next;
      enqueue(state);
    }
  };

  for (const auto& [entry, matched] : seeded_entries) {
    if (entry->kind == ExternalEntryKind::Main) {
      for (const std::size_t state : matched)
        merge_in(state, main_entry_state);
      continue;
    }
    // Manual protocol anchors: the operator interacts here, so nothing about
    // X, the number entry, or even the registers is assumed. Seeding with the
    // unknown state is strictly conservative; in-graph edges may still join
    // known values into these states without ever making them more precise
    // than the operator can produce.
    AbstractState manual_state;
    manual_state.entry.poisoned = true;
    for (const std::size_t state : matched)
      merge_in(state, manual_state);
  }

  // Fixpoint. Resume roots are re-seeded whenever a stop state's out value
  // changes, closing the stop -> resume dependency inside the same loop.
  std::size_t iterations = 0;
  const std::size_t iteration_limit = flow.execution_states.size() * 64U + 1024U;
  while (!worklist.empty()) {
    if (++iterations > iteration_limit)
      return fail("stable-register value flow did not reach a fixpoint");
    const std::size_t state = worklist.front();
    worklist.pop_front();
    queued.at(state) = 0;
    if (!in_value.at(state).has_value())
      continue;
    const std::size_t item_index = flow.execution_states.at(state).item_index;
    const AbstractState next_out = transfer(*in_value.at(state), items.at(item_index),
                                            item_index, flow, model);
    if (out_value.at(state).has_value() && *out_value.at(state) == next_out)
      continue;
    out_value.at(state) = next_out;
    for (const std::size_t successor : flow.execution_successors.at(state))
      merge_in(successor, next_out);
    if (items.at(item_index).opcode == 0x50) {
      const auto resume_states = resume_states_by_stop_item.find(item_index);
      if (resume_states != resume_states_by_stop_item.end()) {
        AbstractState resume_state = next_out;
        resume_state.x.reset();
        resume_state.entry = EntryBuffer{.poisoned = true};
        for (const std::size_t resume : resume_states->second)
          merge_in(resume, resume_state);
      }
    }
  }

  result.total_states = flow.execution_states.size();
  for (std::size_t state = 0; state < flow.execution_states.size(); ++state) {
    if (!in_value.at(state).has_value())
      continue;
    ++result.valued_states;
    const std::size_t item_index = flow.execution_states.at(state).item_index;
    const auto [existing, inserted] =
        result.before_item.emplace(item_index, in_value.at(state)->registers);
    if (!inserted) {
      for (std::size_t slot = 0; slot < kTrackedRegisters; ++slot) {
        existing->second[slot] =
            join_value(existing->second[slot], in_value.at(state)->registers[slot]);
      }
    }
  }
  result.proved = true;
  return result;
}

}  // namespace mkpro::core
