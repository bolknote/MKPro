#pragma once

#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/stack_value_equivalence.hpp"

#include <algorithm>
#include <deque>
#include <set>
#include <tuple>
#include <vector>

namespace mkpro::core {

// This is a machine-state nonobservation proof, not an interpretation of the
// source-level discarded-value annotation. The caller separately proves that
// the changed register is an unwritten, ordinary decimal address selector.
inline bool prove_discarded_indirect_selector_reads_unobserved(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow, int changed_register,
    std::size_t maximum_equality_states = 20000) {
  if (!flow.proved || changed_register < 7 || changed_register > 0x0e ||
      flow.execution_states.empty() ||
      flow.execution_edges.size() != flow.execution_states.size()) {
    return false;
  }

  std::set<std::size_t> affected_reads;
  for (std::size_t i = 0; i < items.size(); ++i) {
    const MachineItem& item = items.at(i);
    if (item.raw)
      return false;
    if (item.kind != MachineItemKind::Op)
      continue;
    if (item.opcode == 0x60 + changed_register ||
        item.opcode == 0x40 + changed_register) {
      return false;
    }
    const int family = item.opcode & 0xf0;
    if (family != 0xb0 && family != 0xd0)
      continue;
    const auto memory = flow.indirect_memory_targets.find(i);
    if (memory == flow.indirect_memory_targets.end() || memory->second.empty())
      return false;
    if (std::find(memory->second.begin(), memory->second.end(),
                  changed_register) == memory->second.end() &&
        (item.opcode & 0x0f) != changed_register) {
      continue;
    }
    if (family != 0xd0 || (item.opcode & 0x0f) == changed_register ||
        (item.opcode & 0x0f) == 0x0f ||
        !item.discarded_indirect_recall_value ||
        item.manual_interaction.has_value()) {
      return false;
    }
    // The current command, not a caller's stale target map, must justify the
    // complete alias set. The implicit set for a discarded read is R0..Re.
    std::vector<int> expected;
    if (item.indirect_memory_targets.has_value()) {
      expected = *item.indirect_memory_targets;
      std::sort(expected.begin(), expected.end());
    } else {
      for (int reg = 0; reg <= 0x0e; ++reg)
        expected.push_back(reg);
    }
    if (expected != memory->second)
      return false;
    affected_reads.insert(i);
  }
  if (affected_reads.empty())
    return false;

  struct Pending {
    std::size_t state = 0;
    StackValueEqualityState equality;
    bool number_entry_active = false;
    bool digit_lift_proved = true;
  };
  std::deque<Pending> pending;
  for (std::size_t s = 0; s < flow.execution_states.size(); ++s) {
    if (!affected_reads.contains(flow.execution_states.at(s).item_index))
      continue;
    const auto& edges = flow.execution_edges.at(s);
    if (edges.size() != 1U ||
        edges.front().kind != PostLayoutExecutionEdgeKind::Fallthrough) {
      return false;
    }
    Pending start;
    start.state = edges.front().target_state;
    // A recall replaces X and saves the equal old X in X2. Y/Z/T and X1
    // remain pairwise equal; the unknown data word itself is not evaluated.
    start.equality.x2_equal = true;
    pending.push_back(start);
  }

  std::set<std::tuple<std::size_t, int, bool, bool>> visited;
  while (!pending.empty()) {
    Pending current = pending.front();
    pending.pop_front();
    if (stack_values_fully_equal(current.equality))
      continue;
    if (current.state >= flow.execution_states.size())
      return false;
    const auto key = std::tuple{current.state,
                               stack_value_equality_key(current.equality),
                               current.number_entry_active,
                               current.digit_lift_proved};
    if (!visited.insert(key).second)
      continue;  // A closed, observation-free fixed point is harmless.
    if (visited.size() > maximum_equality_states)
      return false;

    const auto& execution = flow.execution_states.at(current.state);
    if (execution.item_index >= items.size())
      return false;
    const MachineItem& item = items.at(execution.item_index);
    if (item.kind != MachineItemKind::Op || item.raw ||
        item.manual_interaction.has_value() ||
        item.stop_disposition != StopDisposition::Unknown) {
      return false;
    }
    const int opcode = item.opcode;
    const int family = opcode & 0xf0;
    const OpcodeInfo& info = opcode_by_code(opcode);
    const auto& edges = flow.execution_edges.at(current.state);
    if (edges.empty())
      return false;
    if (affected_reads.contains(execution.item_index)) {
      if (edges.size() != 1U ||
          edges.front().kind != PostLayoutExecutionEdgeKind::Fallthrough)
        return false;
      // A later discarded read introduces another unequal X. It does not
      // invalidate earlier stack taint, nor may it reset that taint to the
      // single-read seed. Registers still evolve identically on both paths.
      const auto old = current.equality.stack_equal;
      current.equality.stack_equal = {false, old.at(0), old.at(1), old.at(2)};
      current.equality.x2_equal = old.at(0);
      current.number_entry_active = false;
      current.digit_lift_proved = true;
      current.state = edges.front().target_state;
      pending.push_back(current);
      continue;
    }
    const bool indirect_flow =
        family == 0x70 || family == 0x80 || family == 0x90 ||
        family == 0xa0 || family == 0xc0 || family == 0xe0;
    const bool flow_command =
        indirect_flow || opcode == 0x51 || opcode == 0x52 ||
        opcode == 0x53 || (opcode >= 0x57 && opcode <= 0x5e);

    if (flow_command) {
      const bool conditional = info.conditional_x2_effect.has_value();
      if ((indirect_flow && (opcode & 0x0f) == 0x0f) ||
          (conditional && !current.equality.stack_equal.at(0))) {
        return false;
      }
      for (const auto& edge : edges) {
        if (edge.kind == PostLayoutExecutionEdgeKind::Resume)
          return false;
        Pending next = current;
        next.state = edge.target_state;
        X2Effect effect = info.x2_effect;
        if (conditional) {
          effect = edge.kind == PostLayoutExecutionEdgeKind::Fallthrough
                       ? info.conditional_x2_effect->fallthrough
                       : info.conditional_x2_effect->jump;
        }
        if (effect == X2Effect::Affects)
          next.equality.x2_equal = current.equality.stack_equal.at(0);
        else if (effect != X2Effect::Preserves)
          return false;
        // Never infer fresh numeric entry from a call/return or from a flow
        // interrupting an open literal. A subsequent recall reestablishes it.
        if (current.number_entry_active || opcode == 0x52 || opcode == 0x53 ||
            family == 0xa0) {
          next.digit_lift_proved = false;
        }
        next.number_entry_active = false;
        pending.push_back(next);
      }
      continue;
    }

    if (edges.size() != 1U ||
        edges.front().kind != PostLayoutExecutionEdgeKind::Fallthrough) {
      return false;
    }
    StackValueEqualityTransfer transfer = StackValueEqualityTransfer::Rejected;
    if (opcode >= 0 && opcode <= 9) {
      if (!current.number_entry_active && !current.digit_lift_proved)
        return false;
      transfer = transfer_decimal_digit_equality(
          current.equality, current.number_entry_active);
      current.number_entry_active = true;
    } else if (opcode == 0x0b) {
      transfer = transfer_decimal_sign_equality(
          current.equality, current.number_entry_active);
    } else {
      StackValueEqualityStepKind kind = StackValueEqualityStepKind::Plain;
      if ((opcode >= 0x60 && opcode <= 0x6e) || family == 0xd0)
        kind = StackValueEqualityStepKind::Recall;
      else if ((opcode >= 0x40 && opcode <= 0x4e) || family == 0xb0)
        kind = StackValueEqualityStepKind::Store;
      if (info.risk == OpcodeRisk::Dangerous ||
          info.risk == OpcodeRisk::Undocumented || opcode == 0x3b) {
        return false;
      }
      transfer = transfer_stack_value_equality(current.equality, opcode, kind);
      current.number_entry_active = false;
      current.digit_lift_proved =
          kind == StackValueEqualityStepKind::Recall ||
          (kind == StackValueEqualityStepKind::Plain &&
           opcode >= 0x10 && opcode <= 0x3a);
    }
    if (transfer == StackValueEqualityTransfer::Rejected)
      return false;
    current.state = edges.front().target_state;
    pending.push_back(current);
  }
  return true;
}

}  // namespace mkpro::core
