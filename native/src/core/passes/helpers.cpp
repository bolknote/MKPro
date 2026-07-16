#include "mkpro/core/passes/helpers.hpp"

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/passes/liveness_analysis.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

bool contains_word_ascii(const std::string& value, std::string_view word) {
  std::size_t position = value.find(word);
  while (position != std::string::npos) {
    const bool left_ok =
        position == 0 || std::isalnum(static_cast<unsigned char>(value.at(position - 1U))) == 0;
    const std::size_t right = position + word.size();
    const bool right_ok =
        right >= value.size() || std::isalnum(static_cast<unsigned char>(value.at(right))) == 0;
    if (left_ok && right_ok)
      return true;
    position = value.find(word, position + 1U);
  }
  return false;
}

bool proof_marker_boundary_after_number(const std::string& comment, std::size_t cursor) {
  if (cursor >= comment.size())
    return true;
  const char ch = comment.at(cursor);
  return std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == ';' || ch == ',';
}

bool has_role(const std::vector<CellRole>& roles, std::string_view role) {
  return std::any_of(roles.begin(), roles.end(),
                     [&](const CellRole& candidate) { return candidate == role; });
}

bool has_opcode(const IrOp& op) {
  return op.kind != IrKind::Label;
}

std::optional<IrTarget> direct_flow_target(const IrOp& op) {
  switch (op.kind) {
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Call:
  case IrKind::Loop:
  case IrKind::OrphanAddress:
    return op.target;
  default:
    return std::nullopt;
  }
}

bool is_indirect_flow_op(const IrOp& op) {
  return op.kind == IrKind::IndirectJump || op.kind == IrKind::IndirectCall ||
         op.kind == IrKind::IndirectCondJump;
}

RegisterDataflowState empty_register_dataflow_state() {
  return RegisterDataflowState{};
}

RegisterDataflowState clone_register_dataflow_state(const RegisterDataflowState& input) {
  return RegisterDataflowState{.x = input.x, .y = input.y, .x2 = input.x2};
}

RegisterValueSet add_register_value(const RegisterValueSet& input,
                                    const std::string& register_name) {
  RegisterValueSet output = input;
  output.insert(register_name);
  return output;
}

RegisterValueSet remove_register_value(const RegisterValueSet& input,
                                       const std::string& register_name) {
  RegisterValueSet output = input;
  output.erase(register_name);
  return output;
}

bool sets_intersect(const RegisterValueSet& left, const RegisterValueSet& right) {
  for (const std::string& value : left) {
    if (right.contains(value))
      return true;
  }
  return false;
}

RegisterValueSet add_stored_x2_alias(const RegisterDataflowState& input,
                                     const std::string& register_name) {
  RegisterValueSet output = remove_register_value(input.x2, register_name);
  if (sets_intersect(input.x, input.x2))
    output.insert(register_name);
  return output;
}

RegisterValueSet transfer_store_y_register_set(const RegisterDataflowState& input,
                                               const std::string& register_name) {
  if (input.x.contains(register_name))
    return input.y;
  return remove_register_value(input.y, register_name);
}

std::string loop_counter_register_name(const std::string& counter) {
  if (counter == "L0")
    return "0";
  if (counter == "L1")
    return "1";
  if (counter == "L2")
    return "2";
  if (counter == "L3")
    return "3";
  return "";
}

X2Effect plain_x2_effect(const IrOp& op) {
  if (op.kind != IrKind::Plain || has_rewrite_barrier(op))
    return X2Effect::Unknown;
  return opcode_by_code(op.opcode).x2_effect;
}

X2Effect conditional_x2_effect_for_graph_edge(const IrOp& op, X2DataflowEdgeKind edge) {
  if (edge != X2DataflowEdgeKind::Fallthrough && edge != X2DataflowEdgeKind::Jump)
    return X2Effect::Unknown;
  const OpcodeInfo& info = opcode_by_code(op.opcode);
  if (!info.conditional_x2_effect.has_value())
    return X2Effect::Unknown;
  return edge == X2DataflowEdgeKind::Fallthrough ? info.conditional_x2_effect->fallthrough
                                                 : info.conditional_x2_effect->jump;
}

RegisterValueSet transfer_plain_x2_register_set(const RegisterDataflowState& input,
                                                const RegisterValueSet& x, X2Effect effect) {
  if (effect == X2Effect::Preserves)
    return input.x2;
  if (effect == X2Effect::Affects)
    return x;
  return {};
}

RegisterValueSet transfer_plain_y_register_set(const RegisterDataflowState& input,
                                               const RegisterValueSet& source_x, const IrOp& op) {
  const OpcodeInfo& info = opcode_by_code(op.opcode);
  if (info.stack_effect == StackEffect::Shifts)
    return source_x;
  if (info.stack_effect == StackEffect::Preserves)
    return input.y;
  if (info.stack_effect == StackEffect::ConsumeYKeep && info.risk == OpcodeRisk::Documented)
    return input.y;
  return {};
}

RegisterValueSet transfer_conditional_x2_register_set(const RegisterDataflowState& input,
                                                      X2Effect effect) {
  if (effect == X2Effect::Preserves)
    return input.x2;
  if (effect == X2Effect::Affects)
    return input.x;
  return {};
}

RegisterDataflowState drop_mutated_selector_fact(const RegisterDataflowState& input,
                                                 const std::string& register_name) {
  return RegisterDataflowState{
      .x = remove_register_value(input.x, register_name),
      .y = remove_register_value(input.y, register_name),
      .x2 = remove_register_value(input.x2, register_name),
  };
}

RegisterDataflowState transfer_indirect_flow_register_state(const RegisterDataflowState& input,
                                                            const IrOp& op) {
  if (mkpro::core::is_stable_indirect_selector(op.register_name))
    return clone_register_dataflow_state(input);
  return drop_mutated_selector_fact(input, op.register_name);
}

RegisterDataflowState
transfer_indirect_conditional_register_state(const RegisterDataflowState& input, const IrOp& op,
                                             X2DataflowEdgeKind edge) {
  const X2Effect effect = conditional_x2_effect_for_graph_edge(op, edge);
  if (effect == X2Effect::Unknown)
    return empty_register_dataflow_state();

  RegisterDataflowState output{
      .x = input.x,
      .y = input.y,
      .x2 = transfer_conditional_x2_register_set(input, effect),
  };
  if (edge == X2DataflowEdgeKind::Jump &&
      !mkpro::core::is_stable_indirect_selector(op.register_name)) {
    return drop_mutated_selector_fact(output, op.register_name);
  }
  return output;
}

RegisterDataflowState transfer_indirect_store_register_state(const RegisterDataflowState& input,
                                                             const IrOp& op) {
  const std::optional<std::string> target = known_indirect_memory_target(op);
  if (!target.has_value()) {
    return mkpro::core::is_stable_indirect_selector(op.register_name)
               ? clone_register_dataflow_state(input)
               : drop_mutated_selector_fact(input, op.register_name);
  }

  RegisterDataflowState output{
      .x = add_register_value(input.x, *target),
      .y = transfer_store_y_register_set(input, *target),
      .x2 = add_stored_x2_alias(input, *target),
  };
  return mkpro::core::is_stable_indirect_selector(op.register_name)
             ? output
             : drop_mutated_selector_fact(output, op.register_name);
}

RegisterDataflowState transfer_plain_register_dataflow_state(const RegisterDataflowState& input,
                                                             const IrOp& op) {
  if (op.opcode == 0x14) {
    const X2Effect effect = plain_x2_effect(op);
    const RegisterValueSet x = input.y;
    return RegisterDataflowState{
        .x = x,
        .y = input.x,
        .x2 = transfer_plain_x2_register_set(input, x, effect),
    };
  }
  if (op.opcode == 0x3e) {
    const X2Effect effect = plain_x2_effect(op);
    const RegisterValueSet x = input.y;
    return RegisterDataflowState{
        .x = x,
        .y = input.y,
        .x2 = transfer_plain_x2_register_set(input, x, effect),
    };
  }

  const X2Effect effect = plain_x2_effect(op);
  const RegisterValueSet x = plain_preserves_x_value(op) ? input.x : RegisterValueSet{};
  return RegisterDataflowState{
      .x = x,
      .y = transfer_plain_y_register_set(input, input.x, op),
      .x2 = transfer_plain_x2_register_set(input, x, effect),
  };
}

RegisterDataflowState transfer_register_dataflow_state(const RegisterDataflowState& input,
                                                       const IrOp& op, X2DataflowEdgeKind edge) {
  if (has_rewrite_barrier(op))
    return empty_register_dataflow_state();

  switch (op.kind) {
  case IrKind::Label:
  case IrKind::Jump:
  case IrKind::Call:
  case IrKind::OrphanAddress:
    return clone_register_dataflow_state(input);
  case IrKind::Store:
    return RegisterDataflowState{
        .x = add_register_value(input.x, op.register_name),
        .y = transfer_store_y_register_set(input, op.register_name),
        .x2 = add_stored_x2_alias(input, op.register_name),
    };
  case IrKind::IndirectStore:
    return transfer_indirect_store_register_state(input, op);
  case IrKind::Recall:
    return RegisterDataflowState{
        .x = RegisterValueSet{op.register_name},
        .y = input.x,
        .x2 = RegisterValueSet{op.register_name},
    };
  case IrKind::IndirectRecall: {
    const std::optional<std::string> target = known_indirect_memory_target(op);
    const RegisterValueSet registers =
        target.has_value() ? RegisterValueSet{*target} : RegisterValueSet{};
    return RegisterDataflowState{.x = registers, .y = input.x, .x2 = registers};
  }
  case IrKind::Plain:
    return transfer_plain_register_dataflow_state(input, op);
  case IrKind::CondJump: {
    const X2Effect effect = conditional_x2_effect_for_graph_edge(op, edge);
    return RegisterDataflowState{
        .x = input.x,
        .y = input.y,
        .x2 = transfer_conditional_x2_register_set(input, effect),
    };
  }
  case IrKind::Loop: {
    const X2Effect effect = conditional_x2_effect_for_graph_edge(op, edge);
    const std::string counter = loop_counter_register_name(op.counter);
    const RegisterValueSet x = remove_register_value(input.x, counter);
    const RegisterValueSet y = remove_register_value(input.y, counter);
    const RegisterValueSet x2 = remove_register_value(input.x2, counter);
    return RegisterDataflowState{
        .x = x,
        .y = y,
        .x2 = effect == X2Effect::Preserves
                  ? x2
                  : (effect == X2Effect::Affects ? x : RegisterValueSet{}),
    };
  }
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
    return transfer_indirect_flow_register_state(input, op);
  case IrKind::IndirectCondJump:
    return transfer_indirect_conditional_register_state(input, op, edge);
  case IrKind::Stop:
    return empty_register_dataflow_state();
  case IrKind::Return:
    return RegisterDataflowState{.x = input.x, .y = input.y, .x2 = input.x};
  }
  return empty_register_dataflow_state();
}

RegisterValueSet join_register_value_sets(const std::optional<RegisterValueSet>& current,
                                          const RegisterValueSet& incoming) {
  if (!current.has_value())
    return incoming;
  RegisterValueSet joined;
  for (const std::string& register_name : *current) {
    if (incoming.contains(register_name))
      joined.insert(register_name);
  }
  return joined;
}

RegisterDataflowState
join_register_dataflow_states(const std::optional<RegisterDataflowState>& current,
                              const RegisterDataflowState& incoming) {
  if (!current.has_value())
    return clone_register_dataflow_state(incoming);
  return RegisterDataflowState{
      .x = join_register_value_sets(current->x, incoming.x),
      .y = join_register_value_sets(current->y, incoming.y),
      .x2 = join_register_value_sets(current->x2, incoming.x2),
  };
}

bool same_register_value_set(const RegisterValueSet& left, const RegisterValueSet& right) {
  return left == right;
}

bool same_register_dataflow_state(const std::optional<RegisterDataflowState>& left,
                                  const std::optional<RegisterDataflowState>& right) {
  if (!left.has_value() || !right.has_value())
    return left.has_value() == right.has_value();
  return same_register_value_set(left->x, right->x) && same_register_value_set(left->y, right->y) &&
         same_register_value_set(left->x2, right->x2);
}

struct RegisterValueEdge {
  int target = 0;
  X2DataflowEdgeKind kind = X2DataflowEdgeKind::Normal;
};

std::vector<std::vector<RegisterValueEdge>>
build_register_value_graph(const std::vector<IrOp>& ops) {
  const std::map<std::string, int> labels = label_indexes(ops);
  const std::map<int, int> addresses = address_indexes(ops);
  std::vector<std::vector<RegisterValueEdge>> successors(ops.size());
  std::vector<int> call_returns;

  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    const int next = static_cast<int>(index + 1U);
    if ((op.kind == IrKind::Call ||
         (op.kind == IrKind::IndirectCall && known_indirect_flow_target(op).has_value())) &&
        next < static_cast<int>(ops.size())) {
      call_returns.push_back(next);
    }
  }

  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    const int next = static_cast<int>(index + 1U);
    auto fallthrough = [&]() {
      if (next < static_cast<int>(ops.size())) {
        successors.at(index).push_back(
            RegisterValueEdge{.target = next, .kind = X2DataflowEdgeKind::Fallthrough});
      }
    };
    auto normal = [&](int target) {
      successors.at(index).push_back(
          RegisterValueEdge{.target = target, .kind = X2DataflowEdgeKind::Normal});
    };
    auto jump_to_address = [&](int target) {
      const auto found = addresses.find(target);
      if (found != addresses.end()) {
        successors.at(index).push_back(
            RegisterValueEdge{.target = found->second, .kind = X2DataflowEdgeKind::Jump});
      }
    };
    auto jump_to = [&](const IrTarget& target) {
      if (const auto* address = std::get_if<int>(&target)) {
        jump_to_address(*address);
        return;
      }
      const auto* label = std::get_if<std::string>(&target);
      if (label == nullptr)
        return;
      const auto found = labels.find(*label);
      if (found != labels.end()) {
        successors.at(index).push_back(
            RegisterValueEdge{.target = found->second, .kind = X2DataflowEdgeKind::Jump});
      }
    };

    switch (op.kind) {
    case IrKind::Label:
    case IrKind::Store:
    case IrKind::Recall:
    case IrKind::IndirectStore:
    case IrKind::IndirectRecall:
    case IrKind::Plain:
    case IrKind::OrphanAddress:
      fallthrough();
      break;
    case IrKind::Stop:
      if (op.semantic != "halt" && op.semantic != "unknown")
        fallthrough();
      break;
    case IrKind::Jump:
      jump_to(op.target);
      break;
    case IrKind::CondJump:
    case IrKind::Loop:
      jump_to(op.target);
      fallthrough();
      break;
    case IrKind::Call:
      jump_to(op.target);
      break;
    case IrKind::IndirectJump:
    case IrKind::IndirectCall:
      if (const std::optional<int> target = known_indirect_flow_target(op))
        jump_to_address(*target);
      for (const std::string& label : computed_dispatch_target_labels(op))
        jump_to(label);
      break;
    case IrKind::IndirectCondJump:
      if (const std::optional<int> target = known_indirect_flow_target(op))
        jump_to_address(*target);
      for (const std::string& label : computed_dispatch_target_labels(op))
        jump_to(label);
      fallthrough();
      break;
    case IrKind::Return:
      for (const int target : call_returns)
        normal(target);
      break;
    }
  }

  return successors;
}

RegisterValueSet add_projected_stored_x2_alias(const RegisterValueSet& x2,
                                               const RegisterValueSet& x,
                                               const std::string& register_name) {
  RegisterValueSet output = remove_register_value(x2, register_name);
  if (sets_intersect(x, x2))
    output.insert(register_name);
  return output;
}

std::optional<RegisterValueSet>
projected_plain_visible_x_register_set(const X2RegisterEdgeState& input, const IrOp& op) {
  if (op.opcode == 0x14 || op.opcode == 0x3e) {
    if (!input.y.has_value())
      return std::nullopt;
    return *input.y;
  }
  if (plain_preserves_x_value(op)) {
    if (!input.x.has_value())
      return std::nullopt;
    return *input.x;
  }
  return RegisterValueSet{};
}

std::optional<RegisterValueSet>
transfer_plain_x2_register_set_for_known_edge(const X2RegisterEdgeState& input, const IrOp& op) {
  const X2Effect effect = plain_x2_effect(op);
  if (effect == X2Effect::Preserves)
    return input.x2;
  if (effect != X2Effect::Affects)
    return RegisterValueSet{};
  return projected_plain_visible_x_register_set(input, op);
}

std::optional<RegisterValueSet>
transfer_conditional_x2_register_set_for_known_edge(const X2RegisterEdgeState& input,
                                                    X2Effect effect) {
  if (effect == X2Effect::Preserves)
    return input.x2;
  if (effect == X2Effect::Affects)
    return input.x;
  return RegisterValueSet{};
}

} // namespace

int cells_per_op(const IrOp& op) {
  switch (op.kind) {
  case IrKind::Label:
    return 0;
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Call:
  case IrKind::Loop:
    return 2;
  case IrKind::OrphanAddress:
    return 1;
  case IrKind::Store:
  case IrKind::Recall:
  case IrKind::IndirectStore:
  case IrKind::IndirectRecall:
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
  case IrKind::IndirectCondJump:
  case IrKind::Return:
  case IrKind::Stop:
  case IrKind::Plain:
    return 1;
  }
  return 1;
}

std::map<std::string, int> calculate_label_addresses(const std::vector<IrOp>& ops) {
  std::map<std::string, int> labels;
  int address = 0;
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Label) {
      labels[op.name] = address;
      continue;
    }
    address += cells_per_op(op);
  }
  return labels;
}

std::map<std::string, int> label_indexes(const std::vector<IrOp>& ops) {
  std::map<std::string, int> result;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (op.kind == IrKind::Label)
      result[op.name] = static_cast<int>(index);
  }
  return result;
}

std::map<int, int> address_indexes(const std::vector<IrOp>& ops) {
  std::map<int, int> result;
  int address = 0;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (op.kind != IrKind::Label) {
      result[address] = static_cast<int>(index);
      address += cells_per_op(op);
    }
  }
  return result;
}

std::set<int> compute_label_entry_indexes(const std::vector<IrOp>& ops) {
  std::set<std::string> string_targets;
  std::set<int> numeric_targets;
  bool unknown_indirect_flow = false;

  for (const IrOp& op : ops) {
    if (const std::optional<IrTarget> target = direct_flow_target(op)) {
      if (const auto* label = std::get_if<std::string>(&*target))
        string_targets.insert(*label);
      if (const auto* numeric = std::get_if<int>(&*target))
        numeric_targets.insert(*numeric);
    }
    if (is_indirect_flow_op(op)) {
      const std::optional<int> indirect_target = known_indirect_flow_target(op);
      if (!indirect_target.has_value()) {
        unknown_indirect_flow = true;
      } else {
        numeric_targets.insert(*indirect_target);
      }
    }
  }

  std::set<int> entries;
  int address = 0;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (op.kind == IrKind::Label) {
      const bool is_procedure_entry = op.procedure_boundary.has_value();
      if (is_procedure_entry || unknown_indirect_flow || string_targets.contains(op.name) ||
          numeric_targets.contains(address)) {
        entries.insert(static_cast<int>(index));
      }
      continue;
    }
    address += cells_per_op(op);
  }
  return entries;
}

DirectReturnAnalysisContext direct_return_analysis_context(const std::vector<IrOp>& ops) {
  return DirectReturnAnalysisContext{
      .label_entries = compute_label_entry_indexes(ops),
      .labels = label_indexes(ops),
      .addresses = address_indexes(ops),
  };
}

std::optional<int> target_address(const IrTarget& target,
                                  const std::map<std::string, int>& labels) {
  if (const auto* numeric = std::get_if<int>(&target))
    return *numeric;
  const auto* label = std::get_if<std::string>(&target);
  if (label == nullptr)
    return std::nullopt;
  const auto found = labels.find(*label);
  if (found == labels.end())
    return std::nullopt;
  return found->second;
}

std::optional<std::string> known_indirect_memory_target(const IrOp& op) {
  if (op.kind != IrKind::IndirectRecall && op.kind != IrKind::IndirectStore)
    return std::nullopt;
  if (!op.meta.comment.has_value())
    return std::nullopt;

  constexpr std::string_view kMarker = "indirect-memory-target=";
  const std::string lowered = lower_ascii(*op.meta.comment);
  const std::size_t marker = lowered.find(kMarker);
  if (marker == std::string::npos)
    return std::nullopt;

  const std::size_t register_position = marker + kMarker.size();
  if (register_position >= lowered.size())
    return std::nullopt;

  const char register_name = lowered.at(register_position);
  const bool is_valid_register = (register_name >= '0' && register_name <= '9') ||
                                 (register_name >= 'a' && register_name <= 'e');
  if (!is_valid_register)
    return std::nullopt;

  const std::size_t after_register = register_position + 1U;
  if (after_register < lowered.size() &&
      std::isalnum(static_cast<unsigned char>(lowered.at(after_register))) != 0) {
    return std::nullopt;
  }
  return std::string(1, register_name);
}

std::optional<std::set<std::string>> known_indirect_memory_targets(const IrOp& op) {
  if (op.meta.indirect_memory_targets.has_value()) {
    const auto& indices = *op.meta.indirect_memory_targets;
    if (indices.empty())
      return std::nullopt;

    std::set<std::string> targets;
    for (const int index : indices) {
      if (index < 0 || index > 14)
        return std::nullopt;
      if (index < 10) {
        targets.insert(std::to_string(index));
      } else {
        targets.insert(std::string(1, static_cast<char>('a' + index - 10)));
      }
    }
    return targets;
  }

  if (const std::optional<std::string> single = known_indirect_memory_target(op))
    return std::set<std::string>{*single};
  if (op.kind != IrKind::IndirectRecall && op.kind != IrKind::IndirectStore)
    return std::nullopt;
  if (!op.meta.comment.has_value())
    return std::nullopt;

  constexpr std::string_view kMarker = "indirect-memory-targets=";
  const std::string lowered = lower_ascii(*op.meta.comment);
  const std::size_t marker = lowered.find(kMarker);
  if (marker == std::string::npos)
    return std::nullopt;

  std::size_t cursor = marker + kMarker.size();
  std::set<std::string> targets;
  bool expect_register = true;
  while (cursor < lowered.size()) {
    const char ch = lowered.at(cursor);
    const bool is_valid_register = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'e');
    if (expect_register) {
      if (!is_valid_register)
        break;
      targets.insert(std::string(1, ch));
      expect_register = false;
      ++cursor;
      continue;
    }
    if (ch != ',')
      break;
    expect_register = true;
    ++cursor;
  }
  if (targets.empty() || expect_register)
    return std::nullopt;
  return targets;
}

std::optional<int> known_indirect_flow_target(const IrOp& op, AddressSpaceModel model) {
  if (op.kind != IrKind::IndirectJump && op.kind != IrKind::IndirectCall &&
      op.kind != IrKind::IndirectCondJump) {
    return std::nullopt;
  }
  if (!op.meta.comment.has_value())
    return std::nullopt;

  constexpr std::string_view kMarker = "indirect-target=";
  const std::string& comment = *op.meta.comment;
  const std::size_t marker = comment.find(kMarker);
  if (marker == std::string::npos)
    return std::nullopt;

  std::size_t cursor = marker + kMarker.size();
  if (cursor >= comment.size() || std::isdigit(static_cast<unsigned char>(comment.at(cursor))) == 0)
    return std::nullopt;

  int target = 0;
  while (cursor < comment.size() &&
         std::isdigit(static_cast<unsigned char>(comment.at(cursor))) != 0) {
    target = (target * 10) + (comment.at(cursor) - '0');
    ++cursor;
  }
  if (!proof_marker_boundary_after_number(comment, cursor))
    return std::nullopt;
  if (target < 0 || target > official_program_last_address(model))
    return std::nullopt;
  return target;
}

std::optional<int> known_indirect_flow_target(const IrOp& op) {
  return known_indirect_flow_target(op, AddressSpaceModel::Standard);
}

std::vector<std::string> computed_dispatch_target_labels(const IrOp& op) {
  std::vector<std::string> labels;
  if (op.kind != IrKind::IndirectJump && op.kind != IrKind::IndirectCall &&
      op.kind != IrKind::IndirectCondJump) {
    return labels;
  }
  if (!op.meta.comment.has_value())
    return labels;
  // Callee-hole skeleton dispatches advertise their leaf entries as
  // `leaf-targets=<address>:<label>,...`; the labels keep the leaves alive
  // in the CFG exactly like computed-dispatch target labels.
  if (op.meta.comment->starts_with("callee-hole indirect call;")) {
    constexpr std::string_view kLeafMarker = "leaf-targets=";
    const std::string& comment = *op.meta.comment;
    const std::size_t marker = comment.find(kLeafMarker);
    if (marker == std::string::npos)
      return labels;
    std::size_t cursor = marker + kLeafMarker.size();
    std::string current;
    bool in_label = false;
    while (cursor < comment.size()) {
      const char ch = comment.at(cursor);
      if (ch == ':') {
        in_label = true;
        current.clear();
      } else if (ch == ',') {
        if (in_label && !current.empty())
          labels.push_back(current);
        current.clear();
        in_label = false;
      } else if (std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == ';') {
        break;
      } else {
        current.push_back(ch);
      }
      ++cursor;
    }
    if (in_label && !current.empty())
      labels.push_back(current);
    return labels;
  }
  if (!op.meta.comment->starts_with("computed dispatch;"))
    return labels;

  constexpr std::string_view kMarker = "computed-dispatch-targets=";
  const std::string& comment = *op.meta.comment;
  const std::size_t marker = comment.find(kMarker);
  if (marker == std::string::npos)
    return labels;

  std::size_t cursor = marker + kMarker.size();
  std::string current;
  while (cursor < comment.size()) {
    const char ch = comment.at(cursor);
    if (ch == ',') {
      if (!current.empty()) {
        labels.push_back(current);
        current.clear();
      }
    } else if (std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == ';') {
      break;
    } else {
      current.push_back(ch);
    }
    ++cursor;
  }
  if (!current.empty())
    labels.push_back(current);
  return labels;
}

std::optional<std::string> removable_recall_value_register(const IrOp& op) {
  if (has_rewrite_barrier(op))
    return std::nullopt;
  if (op.kind == IrKind::Recall)
    return op.register_name;
  if (op.kind != IrKind::IndirectRecall)
    return std::nullopt;
  if (!mkpro::core::is_stable_indirect_selector(op.register_name))
    return std::nullopt;
  return known_indirect_memory_target(op);
}

std::optional<std::string> stored_current_x_value_register(const IrOp& op) {
  if (has_rewrite_barrier(op))
    return std::nullopt;
  if (op.kind == IrKind::Store)
    return op.register_name;
  if (op.kind != IrKind::IndirectStore)
    return std::nullopt;
  return known_indirect_memory_target(op);
}

std::optional<int> next_executable_index(const std::vector<IrOp>& ops, int start) {
  for (int index = start; index < static_cast<int>(ops.size()); ++index) {
    if (ops.at(static_cast<std::size_t>(index)).kind != IrKind::Label)
      return index;
  }
  return std::nullopt;
}

bool is_display_focus_sensitive(const IrOp& op) {
  if (has_role(op.meta.roles, "display-byte"))
    return true;
  if (!op.meta.comment.has_value())
    return false;
  const std::string lowered = lower_ascii(*op.meta.comment);
  return contains_word_ascii(lowered, "display") || contains_word_ascii(lowered, "screen") ||
         contains_word_ascii(lowered, "show") || contains_word_ascii(lowered, "x2") ||
         lowered.find("вп") != std::string::npos;
}

bool is_x2_affecting_op(const IrOp& op) {
  return has_opcode(op) && opcode_by_code(op.opcode).x2_effect == X2Effect::Affects;
}

bool is_x2_restore_op(const IrOp& op) {
  return has_opcode(op) && opcode_by_code(op.opcode).x2_effect == X2Effect::Restores;
}

bool plain_preserves_x_value(const IrOp& op) {
  if (op.kind != IrKind::Plain || has_rewrite_barrier(op))
    return false;
  if (op.opcode == 0x0e)
    return true;
  if (op.opcode >= 0xf0 && op.opcode <= 0xff)
    return true;
  return op.opcode >= 0x54 && op.opcode <= 0x56;
}

X2StackEffectAnalysis analyze_x2_stack_effect(const IrOp* op) {
  X2Effect x2_effect = X2Effect::Preserves;
  StackEffect stack_effect = StackEffect::Preserves;
  if (op == nullptr || has_rewrite_barrier(*op)) {
    x2_effect = X2Effect::Unknown;
    stack_effect = StackEffect::Unknown;
  } else if (has_opcode(*op)) {
    const OpcodeInfo& info = opcode_by_code(op->opcode);
    x2_effect = info.x2_effect;
    stack_effect = info.stack_effect;
  }

  const bool stack_consumes =
      stack_effect == StackEffect::ConsumeYDrop || stack_effect == StackEffect::ConsumeYKeep;
  const bool stack_barrier =
      stack_effect == StackEffect::Barrier || stack_effect == StackEffect::Unknown;
  const bool hard_x2_overwrite_without_stack_use =
      op != nullptr && op->kind == IrKind::Plain && x2_effect == X2Effect::Affects &&
      stack_effect == StackEffect::Preserves && !plain_preserves_x_value(*op);

  return X2StackEffectAnalysis{
      .x2_effect = x2_effect,
      .stack_effect = stack_effect,
      .stack_shifts = stack_effect == StackEffect::Shifts,
      .stack_preserves = stack_effect == StackEffect::Preserves,
      .stack_consumes = stack_consumes,
      .stack_exposes = stack_effect == StackEffect::Exposes,
      .stack_barrier = stack_barrier,
      .x2_affects = x2_effect == X2Effect::Affects,
      .x2_preserves = x2_effect == X2Effect::Preserves,
      .x2_restores = x2_effect == X2Effect::Restores,
      .hard_x2_overwrite_without_stack_use = hard_x2_overwrite_without_stack_use,
      .stack_lift_and_x2_sync =
          op != nullptr && stack_effect == StackEffect::Shifts && x2_effect == X2Effect::Affects,
  };
}

X2StackEffectAnalysis analyze_x2_stack_effect(const IrOp& op) {
  return analyze_x2_stack_effect(&op);
}

namespace {

using StackDifferenceDepth = int;

std::optional<StackDifferenceDepth> shift_difference(StackDifferenceDepth depth) {
  if (depth == 1)
    return 2;
  if (depth == 2)
    return 3;
  return std::nullopt;
}

std::optional<StackDifferenceDepth> drop_difference(StackDifferenceDepth depth) {
  if (depth == 1)
    return std::nullopt;
  if (depth == 2)
    return 1;
  return 2;
}

std::vector<int> stack_difference_call_return_indexes(const std::vector<IrOp>& ops) {
  std::vector<int> returns;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    const int next = static_cast<int>(index + 1U);
    if (next >= static_cast<int>(ops.size()))
      continue;
    if (op.kind == IrKind::Call ||
        (op.kind == IrKind::IndirectCall && known_indirect_flow_target(op).has_value()))
      returns.push_back(next);
  }
  return returns;
}

std::optional<int> address_stable_flow_target_index(const std::map<int, int>& addresses,
                                                    std::optional<int> target,
                                                    std::optional<int> must_be_before) {
  if (!target.has_value())
    return std::nullopt;
  const auto found = addresses.find(*target);
  if (found == addresses.end())
    return std::nullopt;
  if (must_be_before.has_value() && found->second >= *must_be_before)
    return std::nullopt;
  return found->second;
}

bool stack_difference_can_reach_consumer(
    const std::vector<IrOp>& ops, int start, StackDifferenceDepth initial_depth,
    std::optional<int> numeric_target_must_be_before_index = std::nullopt) {
  const std::map<std::string, int> labels = label_indexes(ops);
  const std::map<int, int> addresses = address_indexes(ops);
  const std::vector<int> call_return_indexes = stack_difference_call_return_indexes(ops);
  std::set<std::string> visited;
  const int direct_numeric_limit = numeric_target_must_be_before_index.value_or(start);
  const std::optional<int> indirect_numeric_limit = numeric_target_must_be_before_index;

  std::function<bool(int, StackDifferenceDepth, std::vector<int>)> visit =
      [&](int visit_start, StackDifferenceDepth depth_value,
          std::vector<int> return_stack) -> bool {
    std::optional<StackDifferenceDepth> depth = depth_value;
    for (int index = visit_start; index < static_cast<int>(ops.size()); ++index) {
      if (!depth.has_value())
        return false;
      std::string key = std::to_string(index) + ":" + std::to_string(*depth) + ":";
      for (const int value : return_stack)
        key += std::to_string(value) + ",";
      if (visited.contains(key))
        return false;
      visited.insert(std::move(key));

      const IrOp& op = ops.at(static_cast<std::size_t>(index));
      if (has_rewrite_barrier(op))
        return true;

      switch (op.kind) {
      case IrKind::Label:
      case IrKind::Store:
      case IrKind::IndirectStore:
      case IrKind::OrphanAddress:
        break;
      case IrKind::Recall:
      case IrKind::IndirectRecall:
        depth = shift_difference(*depth);
        break;
      case IrKind::Plain: {
        const X2StackEffectAnalysis effect = analyze_x2_stack_effect(op);
        if (effect.stack_effect == StackEffect::Unknown || effect.stack_exposes)
          return true;
        if (effect.stack_effect == StackEffect::Barrier) {
          if (*depth == 1)
            return false;
          break;
        }
        if (effect.stack_shifts) {
          depth = shift_difference(*depth);
          break;
        }
        if (effect.stack_effect == StackEffect::ConsumeYDrop) {
          if (*depth == 1)
            return true;
          depth = drop_difference(*depth);
          break;
        }
        if (effect.stack_effect == StackEffect::ConsumeYKeep) {
          if (*depth == 1)
            return true;
          break;
        }
        break;
      }
      case IrKind::Jump: {
        if (const auto* address = std::get_if<int>(&op.target)) {
          const auto target_index = addresses.find(*address);
          return target_index == addresses.end() || target_index->second >= direct_numeric_limit
                     ? true
                     : visit(target_index->second, *depth, return_stack);
        }
        const auto* label = std::get_if<std::string>(&op.target);
        if (label == nullptr)
          return true;
        const auto target = labels.find(*label);
        return target == labels.end() ? true : visit(target->second + 1, *depth, return_stack);
      }
      case IrKind::CondJump:
      case IrKind::Loop: {
        std::optional<int> target_index;
        if (const auto* label = std::get_if<std::string>(&op.target)) {
          const auto found = labels.find(*label);
          if (found != labels.end())
            target_index = found->second;
        } else if (const auto* address = std::get_if<int>(&op.target)) {
          const auto found = addresses.find(*address);
          if (found != addresses.end())
            target_index = found->second;
          if (!target_index.has_value() || *target_index >= direct_numeric_limit)
            return true;
        }
        return (!target_index.has_value()
                    ? true
                    : visit(std::holds_alternative<std::string>(op.target) ? *target_index + 1
                                                                           : *target_index,
                            *depth, return_stack)) ||
               visit(index + 1, *depth, return_stack);
      }
      case IrKind::Call: {
        std::optional<int> target_index;
        if (const auto* label = std::get_if<std::string>(&op.target)) {
          const auto found = labels.find(*label);
          if (found != labels.end())
            target_index = found->second;
        } else if (const auto* address = std::get_if<int>(&op.target)) {
          const auto found = addresses.find(*address);
          if (found != addresses.end())
            target_index = found->second;
          if (!target_index.has_value() || *target_index >= direct_numeric_limit)
            return true;
        }
        if (!target_index.has_value() || return_stack.size() >= 5U)
          return true;
        std::vector<int> next_stack = return_stack;
        next_stack.insert(next_stack.begin(), index + 1);
        return visit(std::holds_alternative<std::string>(op.target) ? *target_index + 1
                                                                    : *target_index,
                     *depth, std::move(next_stack));
      }
      case IrKind::IndirectJump: {
        const std::optional<int> target = known_indirect_flow_target(op);
        const std::optional<int> target_index =
            address_stable_flow_target_index(addresses, target, indirect_numeric_limit);
        return !target_index.has_value() ? true : visit(*target_index, *depth, return_stack);
      }
      case IrKind::IndirectCall: {
        const std::optional<int> target = known_indirect_flow_target(op);
        const std::optional<int> target_index =
            address_stable_flow_target_index(addresses, target, indirect_numeric_limit);
        if (!target_index.has_value() || return_stack.size() >= 5U)
          return true;
        std::vector<int> next_stack = return_stack;
        next_stack.insert(next_stack.begin(), index + 1);
        return visit(*target_index, *depth, std::move(next_stack));
      }
      case IrKind::IndirectCondJump: {
        const std::optional<int> target = known_indirect_flow_target(op);
        const std::optional<int> target_index =
            address_stable_flow_target_index(addresses, target, indirect_numeric_limit);
        return (!target_index.has_value() ? true : visit(*target_index, *depth, return_stack)) ||
               visit(index + 1, *depth, return_stack);
      }
      case IrKind::Return:
        if (return_stack.empty()) {
          for (const int target : call_return_indexes) {
            if (visit(target, *depth, {}))
              return true;
          }
          return false;
        } else {
          const int target = return_stack.front();
          std::vector<int> next_stack(return_stack.begin() + 1, return_stack.end());
          return visit(target, *depth, std::move(next_stack));
        }
      case IrKind::Stop:
        return false;
      }
    }
    return false;
  };

  return visit(start, initial_depth, {});
}

bool is_context_sensitive_x2_restore(const IrOp& op) {
  return op.kind == IrKind::Plain && (op.opcode == 0x0a || op.opcode == 0x0b || op.opcode == 0x0c);
}

X2Effect conditional_x2_effect_for_restore_scan(const IrOp& op, X2DataflowEdgeKind edge) {
  return conditional_x2_effect_for_graph_edge(op, edge);
}

} // namespace

bool removing_recall_can_expose_stack_lift(const std::vector<IrOp>& ops, int recall_index) {
  return stack_difference_can_reach_consumer(ops, recall_index + 1, 1);
}

bool removing_stack_lift_can_expose_stack(const std::vector<IrOp>& ops, int lift_index) {
  return stack_difference_can_reach_consumer(ops, lift_index + 1, 1);
}

bool removing_pre_shift_lift_can_expose_stack(const std::vector<IrOp>& ops, int producer_index) {
  return stack_difference_can_reach_consumer(ops, producer_index + 1, 2);
}

bool replacing_number_entry_can_expose_stack_lift(
    const std::vector<IrOp>& ops, int number_entry_end_index,
    std::optional<int> numeric_target_must_be_before_index) {
  return stack_difference_can_reach_consumer(ops, number_entry_end_index + 1, 1,
                                             numeric_target_must_be_before_index);
}

bool x2_sync_can_expose_context_sensitive_restore(const std::vector<IrOp>& ops, int sync_index,
                                                  const X2RestoreExposureOptions& options) {
  const std::map<std::string, int> labels = label_indexes(ops);
  const std::map<int, int> addresses = address_indexes(ops);
  std::set<std::string> visited;
  const int direct_numeric_limit = options.numeric_target_must_be_before_index.value_or(sync_index);
  const std::optional<int> indirect_numeric_limit = options.numeric_target_must_be_before_index;

  std::function<bool(int, std::vector<int>, bool)> visit =
      [&](int visit_start, std::vector<int> return_stack, bool saw_executable_after_sync) -> bool {
    for (int index = visit_start; index < static_cast<int>(ops.size()); ++index) {
      std::string key = std::to_string(index) + ":" +
                        (saw_executable_after_sync ? std::string{"1"} : std::string{"0"}) + ":";
      for (const int value : return_stack)
        key += std::to_string(value) + ",";
      if (visited.contains(key))
        return false;
      visited.insert(std::move(key));

      const IrOp& op = ops.at(static_cast<std::size_t>(index));
      if (has_rewrite_barrier(op))
        return true;

      switch (op.kind) {
      case IrKind::Plain: {
        const X2Effect effect = plain_x2_effect(op);
        if (effect == X2Effect::Unknown)
          return true;
        if (effect == X2Effect::Restores && is_context_sensitive_x2_restore(op)) {
          const bool redundant_sync = options.redundant_sync_register.has_value() ||
                                      options.redundant_sync_value ||
                                      options.redundant_sync_shape ||
                                      (options.redundant_sync_display_value && op.opcode == 0x0a) ||
                                      (options.redundant_sync_vp_shape && op.opcode == 0x0c);
          return redundant_sync && saw_executable_after_sync ? false : true;
        }
        if (effect == X2Effect::Restores)
          return false;
        if (effect == X2Effect::Affects)
          return false;
        saw_executable_after_sync = true;
        break;
      }
      case IrKind::Recall:
      case IrKind::IndirectRecall:
      case IrKind::Stop:
      case IrKind::Return:
        return false;
      case IrKind::Jump: {
        if (const auto* address = std::get_if<int>(&op.target)) {
          const auto target_index = addresses.find(*address);
          return target_index == addresses.end() || target_index->second >= direct_numeric_limit
                     ? true
                     : visit(target_index->second, return_stack, true);
        }
        const auto* label = std::get_if<std::string>(&op.target);
        if (label == nullptr)
          return true;
        const auto target = labels.find(*label);
        return target == labels.end() ? true : visit(target->second + 1, return_stack, true);
      }
      case IrKind::CondJump:
      case IrKind::Loop: {
        const X2Effect fallthrough =
            conditional_x2_effect_for_restore_scan(op, X2DataflowEdgeKind::Fallthrough);
        const X2Effect jump = conditional_x2_effect_for_restore_scan(op, X2DataflowEdgeKind::Jump);
        if (fallthrough == X2Effect::Unknown || jump == X2Effect::Unknown)
          return true;

        std::optional<int> target_index;
        if (const auto* label = std::get_if<std::string>(&op.target)) {
          const auto found = labels.find(*label);
          if (found != labels.end())
            target_index = found->second;
        } else if (const auto* address = std::get_if<int>(&op.target)) {
          const auto found = addresses.find(*address);
          if (found != addresses.end())
            target_index = found->second;
          if (jump == X2Effect::Preserves &&
              (!target_index.has_value() || *target_index >= direct_numeric_limit))
            return true;
        }

        return (jump == X2Effect::Preserves &&
                (!target_index.has_value()
                     ? true
                     : visit(std::holds_alternative<std::string>(op.target) ? *target_index + 1
                                                                            : *target_index,
                             return_stack, true))) ||
               (fallthrough == X2Effect::Preserves && visit(index + 1, return_stack, true));
      }
      case IrKind::Call: {
        std::optional<int> target_index;
        if (const auto* label = std::get_if<std::string>(&op.target)) {
          const auto found = labels.find(*label);
          if (found != labels.end())
            target_index = found->second;
        } else if (const auto* address = std::get_if<int>(&op.target)) {
          const auto found = addresses.find(*address);
          if (found != addresses.end())
            target_index = found->second;
          if (!target_index.has_value() || *target_index >= direct_numeric_limit)
            return true;
        }
        if (!target_index.has_value() || return_stack.size() >= 5U)
          return true;
        std::vector<int> next_stack = return_stack;
        next_stack.insert(next_stack.begin(), index + 1);
        return visit(std::holds_alternative<std::string>(op.target) ? *target_index + 1
                                                                    : *target_index,
                     std::move(next_stack), true);
      }
      case IrKind::IndirectJump: {
        const std::optional<int> target = known_indirect_flow_target(op);
        const std::optional<int> target_index =
            address_stable_flow_target_index(addresses, target, indirect_numeric_limit);
        return !target_index.has_value() ? true : visit(*target_index, return_stack, true);
      }
      case IrKind::IndirectCall: {
        const std::optional<int> target = known_indirect_flow_target(op);
        const std::optional<int> target_index =
            address_stable_flow_target_index(addresses, target, indirect_numeric_limit);
        if (!target_index.has_value() || return_stack.size() >= 5U)
          return true;
        std::vector<int> next_stack = return_stack;
        next_stack.insert(next_stack.begin(), index + 1);
        return visit(*target_index, std::move(next_stack), true);
      }
      case IrKind::IndirectCondJump: {
        const std::optional<int> target = known_indirect_flow_target(op);
        const std::optional<int> target_index =
            address_stable_flow_target_index(addresses, target, indirect_numeric_limit);
        const X2Effect fallthrough =
            conditional_x2_effect_for_restore_scan(op, X2DataflowEdgeKind::Fallthrough);
        const X2Effect jump = conditional_x2_effect_for_restore_scan(op, X2DataflowEdgeKind::Jump);
        if (fallthrough == X2Effect::Unknown || jump == X2Effect::Unknown)
          return true;
        return (jump == X2Effect::Preserves &&
                (!target_index.has_value() ? true : visit(*target_index, return_stack, true))) ||
               (fallthrough == X2Effect::Preserves && visit(index + 1, return_stack, true));
      }
      case IrKind::Label:
        break;
      case IrKind::Store:
      case IrKind::IndirectStore:
      case IrKind::OrphanAddress:
        saw_executable_after_sync = true;
        break;
      }
    }
    return false;
  };

  return visit(sync_index + 1, {}, false);
}

bool removing_recall_can_expose_x2_restore(const std::vector<IrOp>& ops, int recall_index,
                                           const X2RestoreExposureOptions& options) {
  return x2_sync_can_expose_context_sensitive_restore(ops, recall_index, options);
}

std::string recall_value_fact_for_register(const std::string& register_name) {
  return "reg:" + register_name;
}

bool x2_value_set_has_register(const X2ValueSet& input, const std::string& register_name) {
  return input.contains(recall_value_fact_for_register(register_name));
}

bool x2_value_sets_intersect(const X2ValueSet& left, const X2ValueSet& right) {
  for (const X2ValueFact& fact : left) {
    if (right.contains(fact))
      return true;
  }
  return false;
}

bool optional_x2_value_sets_intersect(const X2ValueSet& left,
                                      const std::optional<X2ValueSet>& right) {
  if (!right.has_value())
    return false;
  return x2_value_sets_intersect(left, *right);
}

bool optional_shape_sets_intersect(const X2ShapeSet& left,
                                   const std::optional<X2ShapeSet>& right) {
  if (!right.has_value())
    return false;
  for (const X2ShapeFact& fact : left) {
    if (right->contains(fact))
      return true;
  }
  return false;
}

namespace {

std::string trim_ascii_copy(std::string value) {
  const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  });
  const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  }).base();
  if (first >= last)
    return {};
  return std::string(first, last);
}

std::string replace_char_copy(std::string value, char from, char to) {
  std::replace(value.begin(), value.end(), from, to);
  return value;
}

// Hand-rolled scanners replacing hot std::regex_match numeric patterns. libc++'s
// std::regex execution dominates the X2 dataflow fixpoint hot path; these scanners
// replicate the exact full-string matching semantics of the original patterns.
inline bool tx_scan_all_ascii_digits(const std::string& value) {
  for (const char ch : value) {
    if (ch < '0' || ch > '9')
      return false;
  }
  return true;
}

struct TxSignedDecimalScan {
  bool neg = false;
  std::string integer;   // empty only for the leading-dot ("\.digits") form
  std::string fraction;  // empty when no fractional part present
  bool has_dot = false;
};

// Matches ^(-?)([0-9]+)(?:\.([0-9]+))?$ : a signed decimal that must have at
// least one integer digit and, if a dot is present, at least one fraction digit.
inline std::optional<TxSignedDecimalScan> tx_scan_signed_int_frac(const std::string& value) {
  std::size_t i = 0;
  const std::size_t n = value.size();
  TxSignedDecimalScan result;
  if (i < n && value[i] == '-') {
    result.neg = true;
    ++i;
  }
  const std::size_t int_start = i;
  while (i < n && value[i] >= '0' && value[i] <= '9')
    ++i;
  if (i == int_start)
    return std::nullopt;
  result.integer = value.substr(int_start, i - int_start);
  if (i < n && value[i] == '.') {
    ++i;
    const std::size_t frac_start = i;
    while (i < n && value[i] >= '0' && value[i] <= '9')
      ++i;
    if (i == frac_start)
      return std::nullopt;
    result.has_dot = true;
    result.fraction = value.substr(frac_start, i - frac_start);
  }
  if (i != n)
    return std::nullopt;
  return result;
}

// Matches ^(-?)(?:([0-9]+)(?:\.([0-9]+))?|\.([0-9]+))$ : like the above but also
// permits a leading-dot form with no integer digits (integer left empty).
inline std::optional<TxSignedDecimalScan> tx_scan_plain_decimal(const std::string& value) {
  std::size_t i = 0;
  const std::size_t n = value.size();
  TxSignedDecimalScan result;
  if (i < n && value[i] == '-') {
    result.neg = true;
    ++i;
  }
  const std::size_t int_start = i;
  while (i < n && value[i] >= '0' && value[i] <= '9')
    ++i;
  const std::size_t int_len = i - int_start;
  if (int_len > 0) {
    result.integer = value.substr(int_start, int_len);
    if (i < n && value[i] == '.') {
      ++i;
      const std::size_t frac_start = i;
      while (i < n && value[i] >= '0' && value[i] <= '9')
        ++i;
      if (i == frac_start)
        return std::nullopt;
      result.has_dot = true;
      result.fraction = value.substr(frac_start, i - frac_start);
    }
  } else {
    if (i < n && value[i] == '.') {
      ++i;
      const std::size_t frac_start = i;
      while (i < n && value[i] >= '0' && value[i] <= '9')
        ++i;
      if (i == frac_start)
        return std::nullopt;
      result.has_dot = true;
      result.fraction = value.substr(frac_start, i - frac_start);
    } else {
      return std::nullopt;
    }
  }
  if (i != n)
    return std::nullopt;
  return result;
}

std::optional<std::string> normalize_plain_decimal_impl(std::string raw) {
  raw = trim_ascii_copy(raw);
  const std::optional<TxSignedDecimalScan> scan = tx_scan_plain_decimal(raw);
  if (!scan.has_value())
    return std::nullopt;

  const std::string sign = scan->neg ? "-" : "";
  std::string integer = scan->integer.empty() ? "0" : scan->integer;
  std::string fraction = scan->fraction;

  const auto non_zero_integer = integer.find_first_not_of('0');
  integer = non_zero_integer == std::string::npos ? "0" : integer.substr(non_zero_integer);
  while (!fraction.empty() && fraction.back() == '0')
    fraction.pop_back();

  const std::string normalized = fraction.empty() ? integer : integer + "." + fraction;
  if (normalized == "0")
    return std::string{"0"};
  return sign + normalized;
}

std::optional<std::string> normalize_plain_decimal(std::string raw) {
  // Mirrors TS NORMALIZE_PLAIN_DECIMAL_CACHE: pure string->string transform on
  // the dataflow hot path.
  static thread_local std::unordered_map<std::string, std::optional<std::string>> cache;
  const auto found = cache.find(raw);
  if (found != cache.end())
    return found->second;
  std::optional<std::string> result = normalize_plain_decimal_impl(raw);
  cache.emplace(std::move(raw), result);
  return result;
}

std::optional<std::string> scaled_decimal_digits(const std::string& digits, int scale) {
  if (digits.empty() ||
      !std::all_of(digits.begin(), digits.end(),
                   [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
    return std::nullopt;
  }
  if (scale <= 0)
    return digits + std::string(static_cast<std::size_t>(-scale), '0');
  const int point = static_cast<int>(digits.size()) - scale;
  if (point > 0)
    return digits.substr(0, static_cast<std::size_t>(point)) + "." +
           digits.substr(static_cast<std::size_t>(point));
  return "0." + std::string(static_cast<std::size_t>(-point), '0') + digits;
}

std::optional<std::string> normalize_preloaded_decimal_literal(std::string input) {
  input = replace_char_copy(trim_ascii_copy(std::move(input)), ',', '.');
  static const std::regex pattern(
      R"(^(-?)(?:([0-9]+)(?:\.([0-9]*))?|\.([0-9]+))(?:[eE]([+-]?[0-9]{1,2}))?$)");
  std::smatch match;
  if (!std::regex_match(input, match, pattern))
    return std::nullopt;

  const std::string sign = match[1].str();
  const std::string integer = match[2].matched ? match[2].str() : "0";
  const std::string fraction = match[3].matched ? match[3].str()
                               : match[4].matched ? match[4].str()
                                                  : std::string{};
  const int exponent = match[5].matched ? std::stoi(match[5].str()) : 0;
  if (std::abs(exponent) > 64)
    return std::nullopt;

  std::string digits = integer + fraction;
  const auto non_zero = digits.find_first_not_of('0');
  digits = non_zero == std::string::npos ? "0" : digits.substr(non_zero);
  const int scale = static_cast<int>(fraction.size()) - exponent;
  if (static_cast<int>(digits.size()) + std::max(0, -scale) > 80)
    return std::nullopt;
  const std::optional<std::string> unsigned_value = scaled_decimal_digits(digits, scale);
  if (!unsigned_value.has_value())
    return std::nullopt;
  return normalize_plain_decimal(sign + *unsigned_value);
}

int significant_decimal_digits(const std::string& input) {
  const std::string unsigned_value = input.starts_with('-') ? input.substr(1) : input;
  const std::size_t dot = unsigned_value.find('.');
  std::string digits = dot == std::string::npos
                           ? unsigned_value
                           : unsigned_value.substr(0, dot) + unsigned_value.substr(dot + 1U);
  const auto first = digits.find_first_not_of('0');
  if (first == std::string::npos)
    return 1;
  digits = digits.substr(first);
  if (dot == std::string::npos) {
    while (!digits.empty() && digits.back() == '0')
      digits.pop_back();
  }
  return static_cast<int>(std::max<std::size_t>(1U, digits.size()));
}

// ---------------------------------------------------------------------------
// Arbitrary-precision signed integer (faithful stand-in for the TS BigInt used
// by the exact-decimal concrete-evaluation engine). Magnitude is stored as a
// big-endian decimal digit string ("0" for zero, no leading zeros otherwise);
// `negative_` is never set when the value is zero. Operations are schoolbook
// over decimal digits which is more than fast enough for the small (<~60 digit)
// values that arise from the calculator's 8-significant-digit domain.
// ---------------------------------------------------------------------------
class BigInt {
 public:
  BigInt() = default;

  static BigInt from_i64(long long value) {
    BigInt result;
    const unsigned long long magnitude =
        value < 0 ? (~static_cast<unsigned long long>(value) + 1ULL)
                  : static_cast<unsigned long long>(value);
    result.mag_ = magnitude == 0ULL ? std::string{"0"} : std::to_string(magnitude);
    result.negative_ = value < 0;
    return result;
  }

  static BigInt from_unsigned_digits(const std::string& digits) {
    BigInt result;
    result.mag_ = strip_leading_zeros(digits);
    result.negative_ = false;
    return result;
  }

  bool is_zero() const { return mag_ == "0"; }
  bool is_negative() const { return negative_; }
  const std::string& magnitude() const { return mag_; }

  BigInt negated() const {
    BigInt result = *this;
    if (!result.is_zero())
      result.negative_ = !result.negative_;
    return result;
  }

  BigInt abs() const {
    BigInt result = *this;
    result.negative_ = false;
    return result;
  }

  std::string to_decimal_string() const { return negative_ ? "-" + mag_ : mag_; }

  bool equals_i64(long long value) const {
    return to_decimal_string() == BigInt::from_i64(value).to_decimal_string();
  }

  // Signed comparison: -1 if a<b, 0 if a==b, +1 if a>b.
  static int compare(const BigInt& a, const BigInt& b) {
    if (a.negative_ != b.negative_)
      return a.negative_ ? -1 : 1;
    const int magnitude = compare_mag(a.mag_, b.mag_);
    return a.negative_ ? -magnitude : magnitude;
  }

  static BigInt add(const BigInt& a, const BigInt& b) {
    if (a.negative_ == b.negative_)
      return make(add_mag(a.mag_, b.mag_), a.negative_);
    const int cmp = compare_mag(a.mag_, b.mag_);
    if (cmp == 0)
      return BigInt::from_i64(0);
    if (cmp > 0)
      return make(sub_mag(a.mag_, b.mag_), a.negative_);
    return make(sub_mag(b.mag_, a.mag_), b.negative_);
  }

  static BigInt sub(const BigInt& a, const BigInt& b) { return add(a, b.negated()); }

  static BigInt mul(const BigInt& a, const BigInt& b) {
    return make(mul_mag(a.mag_, b.mag_), a.negative_ != b.negative_);
  }

  // Truncated-toward-zero division, matching JS BigInt `/` and `%`. The
  // remainder takes the sign of the dividend. `divisor` must be non-zero.
  static std::pair<BigInt, BigInt> divmod(const BigInt& a, const BigInt& b) {
    auto qr = divmod_mag(a.mag_, b.mag_);
    return {make(qr.first, a.negative_ != b.negative_), make(qr.second, a.negative_)};
  }

  std::optional<long long> to_int64_safe() const {
    static const std::string kMax = "9007199254740991";  // 2^53 - 1
    if (mag_.size() > kMax.size() || (mag_.size() == kMax.size() && mag_ > kMax))
      return std::nullopt;
    long long value = 0;
    for (const char digit : mag_)
      value = value * 10 + (digit - '0');
    return negative_ ? -value : value;
  }

 private:
  std::string mag_ = "0";
  bool negative_ = false;

  static BigInt make(std::string mag, bool negative) {
    BigInt result;
    result.mag_ = strip_leading_zeros(mag);
    result.negative_ = negative && result.mag_ != "0";
    return result;
  }

  static std::string strip_leading_zeros(const std::string& input) {
    std::size_t first = 0;
    while (first + 1 < input.size() && input.at(first) == '0')
      ++first;
    std::string trimmed = input.substr(first);
    return trimmed.empty() ? std::string{"0"} : trimmed;
  }

  static std::vector<int> to_le(const std::string& mag) {
    std::vector<int> digits;
    digits.reserve(mag.size());
    for (auto it = mag.rbegin(); it != mag.rend(); ++it)
      digits.push_back(*it - '0');
    return digits;
  }

  static std::string from_le(const std::vector<int>& digits) {
    std::size_t top = digits.size();
    while (top > 1 && digits.at(top - 1) == 0)
      --top;
    std::string output;
    output.reserve(top);
    for (std::size_t i = top; i-- > 0;)
      output.push_back(static_cast<char>('0' + digits.at(i)));
    return output.empty() ? std::string{"0"} : output;
  }

  static int compare_mag(const std::string& a, const std::string& b) {
    if (a.size() != b.size())
      return a.size() < b.size() ? -1 : 1;
    if (a == b)
      return 0;
    return a < b ? -1 : 1;
  }

  static std::string add_mag(const std::string& a, const std::string& b) {
    const std::vector<int> x = to_le(a);
    const std::vector<int> y = to_le(b);
    std::vector<int> result;
    int carry = 0;
    const std::size_t count = std::max(x.size(), y.size());
    for (std::size_t i = 0; i < count || carry != 0; ++i) {
      int sum = carry;
      if (i < x.size())
        sum += x.at(i);
      if (i < y.size())
        sum += y.at(i);
      result.push_back(sum % 10);
      carry = sum / 10;
    }
    return from_le(result);
  }

  // Requires a >= b (by magnitude).
  static std::string sub_mag(const std::string& a, const std::string& b) {
    const std::vector<int> x = to_le(a);
    const std::vector<int> y = to_le(b);
    std::vector<int> result;
    int borrow = 0;
    for (std::size_t i = 0; i < x.size(); ++i) {
      int diff = x.at(i) - borrow - (i < y.size() ? y.at(i) : 0);
      if (diff < 0) {
        diff += 10;
        borrow = 1;
      } else {
        borrow = 0;
      }
      result.push_back(diff);
    }
    return from_le(result);
  }

  static std::string mul_mag(const std::string& a, const std::string& b) {
    if (a == "0" || b == "0")
      return "0";
    const std::vector<int> x = to_le(a);
    const std::vector<int> y = to_le(b);
    std::vector<long long> acc(x.size() + y.size(), 0);
    for (std::size_t i = 0; i < x.size(); ++i)
      for (std::size_t j = 0; j < y.size(); ++j)
        acc.at(i + j) += static_cast<long long>(x.at(i)) * static_cast<long long>(y.at(j));
    std::vector<int> result(acc.size(), 0);
    long long carry = 0;
    for (std::size_t i = 0; i < acc.size(); ++i) {
      const long long current = acc.at(i) + carry;
      result.at(i) = static_cast<int>(current % 10);
      carry = current / 10;
    }
    while (carry != 0) {
      result.push_back(static_cast<int>(carry % 10));
      carry /= 10;
    }
    return from_le(result);
  }

  // Long division producing {quotient, remainder} magnitudes.
  static std::pair<std::string, std::string> divmod_mag(const std::string& a,
                                                        const std::string& b) {
    if (compare_mag(a, b) < 0)
      return {std::string{"0"}, a};
    std::string remainder = "0";
    std::string quotient;
    quotient.reserve(a.size());
    for (const char digit : a) {
      remainder = remainder == "0" ? std::string(1, digit) : remainder + digit;
      remainder = strip_leading_zeros(remainder);
      int chosen = 0;
      std::string accumulated = "0";
      for (int trial = 1; trial <= 9; ++trial) {
        const std::string candidate = add_mag(accumulated, b);
        if (compare_mag(candidate, remainder) <= 0) {
          accumulated = candidate;
          chosen = trial;
        } else {
          break;
        }
      }
      remainder = strip_leading_zeros(sub_mag(remainder, accumulated));
      quotient.push_back(static_cast<char>('0' + chosen));
    }
    return {strip_leading_zeros(quotient), remainder};
  }
};

BigInt big_pow10(int power) {
  std::string magnitude = "1";
  if (power > 0)
    magnitude.append(static_cast<std::size_t>(power), '0');
  return BigInt::from_unsigned_digits(magnitude);
}

BigInt big_pow(const BigInt& base, int power) {
  BigInt result = BigInt::from_i64(1);
  for (int i = 0; i < power; ++i)
    result = BigInt::mul(result, base);
  return result;
}

BigInt big_gcd(BigInt a, BigInt b) {
  while (!b.is_zero()) {
    BigInt remainder = BigInt::divmod(a, b).second;
    a = b;
    b = remainder;
  }
  return a;
}

std::optional<BigInt> exact_bigint_square_root(const BigInt& input) {
  if (input.is_negative())
    return std::nullopt;
  if (BigInt::compare(input, BigInt::from_i64(2)) < 0)
    return input;
  BigInt low = BigInt::from_i64(1);
  BigInt high = input;
  const BigInt one = BigInt::from_i64(1);
  const BigInt two = BigInt::from_i64(2);
  while (BigInt::compare(low, high) <= 0) {
    const BigInt mid = BigInt::divmod(BigInt::add(low, high), two).first;
    const BigInt square = BigInt::mul(mid, mid);
    const int cmp = BigInt::compare(square, input);
    if (cmp == 0)
      return mid;
    if (cmp < 0)
      low = BigInt::add(mid, one);
    else
      high = BigInt::sub(mid, one);
  }
  return std::nullopt;
}

struct DecimalDenominatorFactors {
  int twos = 0;
  int fives = 0;
};

std::optional<DecimalDenominatorFactors> decimal_denominator_factors(BigInt value) {
  const BigInt two = BigInt::from_i64(2);
  const BigInt five = BigInt::from_i64(5);
  DecimalDenominatorFactors factors;
  while (BigInt::divmod(value, two).second.is_zero()) {
    value = BigInt::divmod(value, two).first;
    factors.twos += 1;
  }
  while (BigInt::divmod(value, five).second.is_zero()) {
    value = BigInt::divmod(value, five).first;
    factors.fives += 1;
  }
  return value.equals_i64(1) ? std::optional<DecimalDenominatorFactors>{factors} : std::nullopt;
}

struct ExactDecimalParts {
  BigInt num;
  int scale = 0;
};

std::optional<ExactDecimalParts> parse_exact_decimal(const std::string& value) {
  const std::optional<TxSignedDecimalScan> scan = tx_scan_signed_int_frac(value);
  if (!scan.has_value())
    return std::nullopt;
  const std::string& fraction = scan->fraction;
  const BigInt unsigned_value = BigInt::from_unsigned_digits(scan->integer + fraction);
  ExactDecimalParts parts;
  parts.num = scan->neg ? unsigned_value.negated() : unsigned_value;
  parts.scale = static_cast<int>(fraction.size());
  return parts;
}

std::string pad_start(const std::string& value, int width, char fill) {
  if (static_cast<int>(value.size()) >= width)
    return value;
  return std::string(static_cast<std::size_t>(width) - value.size(), fill) + value;
}

std::optional<std::string> exact_decimal_to_normalized(const BigInt& num, int scale) {
  const std::string sign = num.is_negative() ? "-" : "";
  const std::string unsigned_value = num.abs().magnitude();
  const std::string raw_digits = pad_start(unsigned_value, scale + 1, '0');
  const std::size_t point = raw_digits.size() - static_cast<std::size_t>(scale);
  const std::string raw =
      scale == 0 ? sign + raw_digits
                 : sign + raw_digits.substr(0, point) + "." + raw_digits.substr(point);
  const std::optional<std::string> normalized = normalize_plain_decimal(raw);
  if (!normalized.has_value() || significant_decimal_digits(*normalized) > 8)
    return std::nullopt;
  return normalized;
}

std::optional<std::string> exact_decimal_division_to_normalized(const ExactDecimalParts& left,
                                                                const ExactDecimalParts& right) {
  if (right.num.is_zero())
    return std::nullopt;
  BigInt numerator = BigInt::mul(left.num, big_pow10(right.scale));
  BigInt denominator = BigInt::mul(right.num, big_pow10(left.scale));
  if (denominator.is_negative()) {
    numerator = numerator.negated();
    denominator = denominator.negated();
  }
  const BigInt divisor = big_gcd(numerator.abs(), denominator);
  numerator = BigInt::divmod(numerator, divisor).first;
  denominator = BigInt::divmod(denominator, divisor).first;
  const std::optional<DecimalDenominatorFactors> factors = decimal_denominator_factors(denominator);
  if (!factors.has_value())
    return std::nullopt;
  const int scale = std::max(factors->twos, factors->fives);
  const BigInt scaled_numerator =
      BigInt::mul(BigInt::mul(numerator, big_pow(BigInt::from_i64(2), scale - factors->twos)),
                  big_pow(BigInt::from_i64(5), scale - factors->fives));
  return exact_decimal_to_normalized(scaled_numerator, scale);
}

std::optional<std::string> exact_decimal_max_to_normalized(const ExactDecimalParts& left,
                                                           const ExactDecimalParts& right) {
  if (left.num.is_zero() || right.num.is_zero())
    return std::string{"0"};
  const int scale = std::max(left.scale, right.scale);
  const BigInt left_num = BigInt::mul(left.num, big_pow10(scale - left.scale));
  const BigInt right_num = BigInt::mul(right.num, big_pow10(scale - right.scale));
  return exact_decimal_to_normalized(BigInt::compare(left_num, right_num) >= 0 ? left_num : right_num,
                                     scale);
}

std::optional<std::string> exact_decimal_power_identity_to_normalized(
    const ExactDecimalParts& exponent, const ExactDecimalParts& base) {
  if (base.num.is_zero())
    return std::string{"0"};
  if (exponent.num.equals_i64(1) && exponent.scale == 0)
    return exact_decimal_to_normalized(base.num, base.scale);
  if (base.num.equals_i64(1) && base.scale == 0)
    return std::string{"1"};
  if (exponent.num.is_zero() && !base.num.is_negative() && !base.num.is_zero())
    return std::string{"1"};
  return std::nullopt;
}

std::optional<std::vector<int>> decimal_mantissa_digits(const std::string& value) {
  const std::optional<ExactDecimalParts> normalized = parse_exact_decimal(value);
  if (!normalized.has_value())
    return std::nullopt;
  if (normalized->num.is_zero())
    return std::vector<int>(8, 0);
  std::string digits = normalized->num.abs().magnitude();
  if (digits.size() > 8U)
    digits = digits.substr(0, 8);
  while (digits.size() < 8U)
    digits.push_back('0');
  std::vector<int> output;
  output.reserve(8);
  for (const char digit : digits)
    output.push_back(digit - '0');
  return output;
}

std::optional<std::string> decimal_from_mantissa_digits(const std::vector<int>& digits) {
  if (digits.size() != 8U)
    return std::nullopt;
  std::string text;
  for (const int digit : digits) {
    if (digit < 0 || digit > 9)
      return std::nullopt;
    text.push_back(static_cast<char>('0' + digit));
  }
  return exact_decimal_to_normalized(BigInt::from_unsigned_digits(text), 7);
}

std::optional<int> bitwise_mantissa_digit(int opcode, int left, int right) {
  switch (opcode) {
    case 0x37:
      return left & right;
    case 0x38:
      return left | right;
    case 0x39:
      return left ^ right;
    default:
      return std::nullopt;
  }
}

std::optional<std::string> exact_decimal_bitwise_to_normalized(int opcode, const std::string& y,
                                                               const std::string& x) {
  const std::optional<std::vector<int>> left = decimal_mantissa_digits(y);
  const std::optional<std::vector<int>> right = decimal_mantissa_digits(x);
  if (!left.has_value() || !right.has_value())
    return std::nullopt;
  std::vector<int> result;
  result.push_back(8);
  for (int index = 1; index < 8; ++index) {
    const std::optional<int> digit = bitwise_mantissa_digit(opcode, left->at(static_cast<std::size_t>(index)),
                                                            right->at(static_cast<std::size_t>(index)));
    if (!digit.has_value() || *digit > 9)
      return std::nullopt;
    result.push_back(*digit);
  }
  return decimal_from_mantissa_digits(result);
}

bool decimal_is_zero(const std::string& value) {
  const std::optional<ExactDecimalParts> parsed = parse_exact_decimal(value);
  return parsed.has_value() && parsed->num.is_zero();
}

bool decimal_is_one(const std::string& value) {
  const std::optional<ExactDecimalParts> parsed = parse_exact_decimal(value);
  return parsed.has_value() && parsed->num.equals_i64(1) && parsed->scale == 0;
}

std::optional<std::string> concrete_decimal_binary_value_impl(int opcode, const std::string& y,
                                                              const std::string& x) {
  const std::optional<ExactDecimalParts> left = parse_exact_decimal(y);
  const std::optional<ExactDecimalParts> right = parse_exact_decimal(x);
  if (!left.has_value() || !right.has_value())
    return std::nullopt;
  if (opcode == 0x12)
    return exact_decimal_to_normalized(BigInt::mul(left->num, right->num), left->scale + right->scale);
  if (opcode == 0x13)
    return exact_decimal_division_to_normalized(*left, *right);
  if (opcode == 0x24)
    return exact_decimal_power_identity_to_normalized(*left, *right);
  if (opcode == 0x36)
    return exact_decimal_max_to_normalized(*left, *right);
  if (opcode >= 0x37 && opcode <= 0x39)
    return exact_decimal_bitwise_to_normalized(opcode, y, x);
  if (opcode != 0x10 && opcode != 0x11)
    return std::nullopt;
  const int scale = std::max(left->scale, right->scale);
  const BigInt y_num = BigInt::mul(left->num, big_pow10(scale - left->scale));
  const BigInt x_num = BigInt::mul(right->num, big_pow10(scale - right->scale));
  return exact_decimal_to_normalized(opcode == 0x10 ? BigInt::add(y_num, x_num)
                                                     : BigInt::sub(y_num, x_num),
                                     scale);
}

std::optional<std::string> decimal_abs(const std::string& value) {
  if (!tx_scan_signed_int_frac(value).has_value())
    return std::nullopt;
  return value.starts_with('-') ? value.substr(1) : value;
}

std::optional<std::string> decimal_sign(const std::string& value) {
  if (!tx_scan_signed_int_frac(value).has_value())
    return std::nullopt;
  if (value == "0")
    return std::string{"0"};
  return value.starts_with('-') ? std::string{"-1"} : std::string{"1"};
}

std::optional<std::string> decimal_power_of_ten(const std::string& value) {
  const std::optional<ExactDecimalParts> exponent = parse_exact_decimal(value);
  if (!exponent.has_value() || exponent->scale != 0)
    return std::nullopt;
  const std::optional<long long> power = exponent->num.to_int64_safe();
  if (!power.has_value() || *power < -99 || *power > 99)
    return std::nullopt;
  return *power >= 0 ? exact_decimal_to_normalized(big_pow10(static_cast<int>(*power)), 0)
                     : exact_decimal_to_normalized(BigInt::from_i64(1), static_cast<int>(-*power));
}

std::optional<std::string> decimal_square_root(const std::string& value) {
  const std::optional<ExactDecimalParts> input = parse_exact_decimal(value);
  if (!input.has_value() || input->num.is_negative() || input->scale % 2 != 0)
    return std::nullopt;
  const std::optional<BigInt> root = exact_bigint_square_root(input->num);
  return root.has_value() ? exact_decimal_to_normalized(*root, input->scale / 2) : std::nullopt;
}

std::optional<std::string> decimal_square(const std::string& value) {
  const std::optional<ExactDecimalParts> input = parse_exact_decimal(value);
  return input.has_value()
             ? exact_decimal_to_normalized(BigInt::mul(input->num, input->num), input->scale * 2)
             : std::nullopt;
}

std::optional<std::string> decimal_reciprocal(const std::string& value) {
  const std::optional<ExactDecimalParts> input = parse_exact_decimal(value);
  if (!input.has_value())
    return std::nullopt;
  ExactDecimalParts one;
  one.num = BigInt::from_i64(1);
  one.scale = 0;
  return exact_decimal_division_to_normalized(one, *input);
}

struct DecimalWholeFractionParts {
  std::string sign;
  BigInt integer;
  std::string fraction;
};

std::optional<DecimalWholeFractionParts> decimal_whole_fraction_parts(const std::string& value) {
  const std::optional<TxSignedDecimalScan> scan = tx_scan_signed_int_frac(value);
  if (!scan.has_value())
    return std::nullopt;
  DecimalWholeFractionParts parts;
  parts.sign = scan->neg ? "-" : "";
  parts.integer = BigInt::from_unsigned_digits(scan->integer);
  parts.fraction = scan->fraction;
  return parts;
}

struct ScaledCentesimalValue {
  BigInt num;
  int scale = 0;
};

std::optional<ScaledCentesimalValue> centesimal_field_value(const std::string& raw, int width) {
  if (!tx_scan_all_ascii_digits(raw))
    return std::nullopt;
  ScaledCentesimalValue value;
  if (raw.empty()) {
    value.num = BigInt::from_i64(0);
    value.scale = 0;
    return value;
  }
  if (static_cast<int>(raw.size()) <= width) {
    value.num = BigInt::mul(BigInt::from_unsigned_digits(raw),
                            big_pow10(width - static_cast<int>(raw.size())));
    value.scale = 0;
    return value;
  }
  value.num = BigInt::from_unsigned_digits(raw);
  value.scale = static_cast<int>(raw.size()) - width;
  return value;
}

struct CentesimalMinuteSecondFields {
  BigInt minutes;
  ScaledCentesimalValue seconds;
};

std::optional<CentesimalMinuteSecondFields> centesimal_minute_second_fields(
    const std::string& fraction) {
  if (!tx_scan_all_ascii_digits(fraction))
    return std::nullopt;
  const std::string minute_raw = fraction.size() <= 2U ? fraction : fraction.substr(0, 2);
  const std::optional<ScaledCentesimalValue> minute_value = centesimal_field_value(minute_raw, 2);
  if (!minute_value.has_value() || minute_value->scale != 0)
    return std::nullopt;
  const std::string second_raw = fraction.size() <= 2U ? std::string{} : fraction.substr(2);
  const std::optional<ScaledCentesimalValue> seconds = centesimal_field_value(second_raw, 2);
  if (!seconds.has_value())
    return std::nullopt;
  CentesimalMinuteSecondFields fields;
  fields.minutes = minute_value->num;
  fields.seconds = *seconds;
  return fields;
}

std::optional<std::string> decimal_to_minutes(const std::string& value) {
  const std::optional<DecimalWholeFractionParts> input = decimal_whole_fraction_parts(value);
  if (!input.has_value())
    return std::nullopt;
  const std::optional<ScaledCentesimalValue> minutes = centesimal_field_value(input->fraction, 2);
  if (!minutes.has_value() ||
      BigInt::compare(minutes->num, BigInt::mul(BigInt::from_i64(60), big_pow10(minutes->scale))) >= 0)
    return std::nullopt;
  const BigInt denominator = BigInt::mul(BigInt::from_i64(60), big_pow10(minutes->scale));
  const BigInt numerator = BigInt::add(BigInt::mul(input->integer, denominator), minutes->num);
  ExactDecimalParts num_parts;
  num_parts.num = input->sign == "-" ? numerator.negated() : numerator;
  num_parts.scale = 0;
  ExactDecimalParts den_parts;
  den_parts.num = denominator;
  den_parts.scale = 0;
  return exact_decimal_division_to_normalized(num_parts, den_parts);
}

std::optional<std::string> decimal_to_minutes_seconds(const std::string& value) {
  const std::optional<DecimalWholeFractionParts> input = decimal_whole_fraction_parts(value);
  if (!input.has_value())
    return std::nullopt;
  const std::optional<CentesimalMinuteSecondFields> fields =
      centesimal_minute_second_fields(input->fraction);
  if (!fields.has_value())
    return std::nullopt;
  if (BigInt::compare(fields->minutes, BigInt::from_i64(60)) >= 0 ||
      BigInt::compare(fields->seconds.num,
                      BigInt::mul(BigInt::from_i64(60), big_pow10(fields->seconds.scale))) >= 0)
    return std::nullopt;
  const int scale = fields->seconds.scale;
  const BigInt denominator = BigInt::mul(BigInt::from_i64(3600), big_pow10(scale));
  const BigInt numerator = BigInt::add(
      BigInt::add(BigInt::mul(input->integer, denominator),
                  BigInt::mul(BigInt::mul(fields->minutes, BigInt::from_i64(60)), big_pow10(scale))),
      fields->seconds.num);
  ExactDecimalParts num_parts;
  num_parts.num = input->sign == "-" ? numerator.negated() : numerator;
  num_parts.scale = 0;
  ExactDecimalParts den_parts;
  den_parts.num = denominator;
  den_parts.scale = 0;
  return exact_decimal_division_to_normalized(num_parts, den_parts);
}

std::optional<std::string> decimal_from_minutes(const std::string& value) {
  const std::optional<DecimalWholeFractionParts> input = decimal_whole_fraction_parts(value);
  if (!input.has_value())
    return std::nullopt;
  const int scale = static_cast<int>(input->fraction.size());
  const BigInt fraction =
      input->fraction.empty() ? BigInt::from_i64(0) : BigInt::from_unsigned_digits(input->fraction);
  const int denominator_scale = scale + 2;
  const BigInt numerator = BigInt::add(BigInt::mul(input->integer, big_pow10(denominator_scale)),
                                       BigInt::mul(fraction, BigInt::from_i64(60)));
  return exact_decimal_to_normalized(input->sign == "-" ? numerator.negated() : numerator,
                                     denominator_scale);
}

std::optional<std::string> decimal_from_minutes_seconds(const std::string& value) {
  const std::optional<DecimalWholeFractionParts> input = decimal_whole_fraction_parts(value);
  if (!input.has_value())
    return std::nullopt;
  const int scale = static_cast<int>(input->fraction.size());
  const BigInt fraction =
      input->fraction.empty() ? BigInt::from_i64(0) : BigInt::from_unsigned_digits(input->fraction);
  const BigInt total_minutes_numerator = BigInt::mul(fraction, BigInt::from_i64(60));
  const BigInt scale_factor = big_pow10(scale);
  const BigInt minutes = BigInt::divmod(total_minutes_numerator, scale_factor).first;
  if (BigInt::compare(minutes, BigInt::from_i64(60)) >= 0)
    return std::nullopt;
  const BigInt minute_remainder =
      BigInt::sub(total_minutes_numerator, BigInt::mul(minutes, scale_factor));
  const int denominator_scale = scale + 4;
  const BigInt numerator = BigInt::add(
      BigInt::add(BigInt::mul(input->integer, big_pow10(denominator_scale)),
                  BigInt::mul(BigInt::mul(minutes, BigInt::from_i64(100)), scale_factor)),
      BigInt::mul(minute_remainder, BigInt::from_i64(60)));
  return exact_decimal_to_normalized(input->sign == "-" ? numerator.negated() : numerator,
                                     denominator_scale);
}

std::optional<std::string> decimal_bitwise_not(const std::string& value) {
  const std::optional<std::vector<int>> digits = decimal_mantissa_digits(value);
  if (!digits.has_value())
    return std::nullopt;
  std::vector<int> result;
  result.push_back(8);
  for (int index = 1; index < 8; ++index) {
    const int digit = (~digits->at(static_cast<std::size_t>(index))) & 0x0f;
    if (digit > 9)
      return std::nullopt;
    result.push_back(digit);
  }
  return decimal_from_mantissa_digits(result);
}

std::optional<std::string> decimal_integer_part(const std::string& value) {
  static const std::regex pattern(R"(^(-?)([0-9]+)(?:\.[0-9]+)?$)");
  std::smatch match;
  if (!std::regex_match(value, match, pattern))
    return std::nullopt;
  const std::string sign = match[1].str();
  std::string integer = match[2].str();
  const std::size_t first = integer.find_first_not_of('0');
  integer = first == std::string::npos ? std::string{"0"} : integer.substr(first);
  if (integer == "0")
    return std::string{"0"};
  return sign + integer;
}

std::optional<std::string> decimal_fraction_part(const std::string& value) {
  static const std::regex pattern(R"(^(-?)([0-9]+)(?:\.([0-9]+))?$)");
  std::smatch match;
  if (!std::regex_match(value, match, pattern))
    return std::nullopt;
  std::string fraction = match[3].matched ? match[3].str() : std::string{};
  while (!fraction.empty() && fraction.back() == '0')
    fraction.pop_back();
  if (fraction.empty())
    return std::string{"0"};
  return match[1].str() + "0." + fraction;
}

std::optional<std::string> concrete_decimal_unary_value_impl(int opcode, const std::string& value) {
  switch (opcode) {
    case 0x15:
      return decimal_power_of_ten(value);
    case 0x16:
      return decimal_is_zero(value) ? std::optional<std::string>{"1"} : std::nullopt;
    case 0x17:
      return decimal_is_one(value) ? std::optional<std::string>{"0"} : std::nullopt;
    case 0x18:
      return decimal_is_one(value) ? std::optional<std::string>{"0"} : std::nullopt;
    case 0x19:
      return decimal_is_zero(value) ? std::optional<std::string>{"0"} : std::nullopt;
    case 0x1a:
      return decimal_is_one(value) ? std::optional<std::string>{"0"} : std::nullopt;
    case 0x1b:
      return decimal_is_zero(value) ? std::optional<std::string>{"0"} : std::nullopt;
    case 0x1c:
      return decimal_is_zero(value) ? std::optional<std::string>{"0"} : std::nullopt;
    case 0x1d:
      return decimal_is_zero(value) ? std::optional<std::string>{"1"} : std::nullopt;
    case 0x1e:
      return decimal_is_zero(value) ? std::optional<std::string>{"0"} : std::nullopt;
    case 0x21:
      return decimal_square_root(value);
    case 0x22:
      return decimal_square(value);
    case 0x23:
      return decimal_reciprocal(value);
    case 0x26:
      return decimal_to_minutes(value);
    case 0x2a:
      return decimal_to_minutes_seconds(value);
    case 0x30:
      return decimal_from_minutes_seconds(value);
    case 0x33:
      return decimal_from_minutes(value);
    case 0x3a:
      return decimal_bitwise_not(value);
    case 0x31:
      return decimal_abs(value);
    case 0x32:
      return decimal_sign(value);
    case 0x34:
      return decimal_integer_part(value);
    case 0x35:
      return decimal_fraction_part(value);
    default:
      return std::nullopt;
  }
}

X2ValueFact decimal_value_fact(const std::string& value, std::string_view flavor) {
  return "decimal:" + value + ":" + std::string(flavor);
}

X2ShapeFact decimal_mantissa_shape_fact(const std::string& value) {
  return "mantissa:" + value + ":decimal";
}

X2ShapeFact decimal_exponent_shape_fact(const std::string& mantissa, const std::string& exponent) {
  return "exponent:" + mantissa + ":" + exponent + ":decimal";
}

std::optional<X2ShapeFact> exact_ordinary_decimal_mantissa_display_shape_fact(
    const std::string& value) {
  const std::optional<std::string> normalized = normalize_plain_decimal(value);
  if (!normalized.has_value())
    return std::nullopt;
  const std::string unsigned_value =
      normalized->starts_with('-') ? normalized->substr(1) : *normalized;
  const std::size_t dot = unsigned_value.find('.');
  const std::string integer = dot == std::string::npos ? unsigned_value : unsigned_value.substr(0, dot);
  const std::string fraction = dot == std::string::npos ? std::string{} : unsigned_value.substr(dot + 1U);
  if (integer == "0")
    return std::nullopt;
  if (integer.size() + fraction.size() > 8U)
    return std::nullopt;
  return decimal_mantissa_shape_fact(*normalized);
}

std::optional<X2ShapeFact> exact_scientific_decimal_display_shape_fact(const std::string& value) {
  const std::optional<ExactDecimalParts> parts = parse_exact_decimal(value);
  if (!parts.has_value() || parts->num.is_zero())
    return std::nullopt;
  const std::string sign = parts->num.is_negative() ? "-" : "";
  std::string digits = parts->num.abs().magnitude();
  int scale = parts->scale;
  while (digits.size() > 1U && digits.back() == '0') {
    digits.pop_back();
    scale -= 1;
  }
  const int exponent = static_cast<int>(digits.size()) - scale - 1;
  if (std::abs(exponent) > 99)
    return std::nullopt;
  const std::string mantissa =
      digits.size() == 1U ? sign + digits : sign + digits.substr(0, 1) + "." + digits.substr(1);
  return decimal_exponent_shape_fact(mantissa, std::to_string(exponent));
}

std::optional<X2ShapeFact> exact_decimal_display_shape_fact(const std::string& value) {
  const std::optional<std::string> normalized = normalize_plain_decimal(value);
  if (!normalized.has_value() || significant_decimal_digits(*normalized) > 8)
    return std::nullopt;
  if (*normalized == "0")
    return decimal_mantissa_shape_fact("0");
  const std::optional<X2ShapeFact> ordinary =
      exact_ordinary_decimal_mantissa_display_shape_fact(*normalized);
  if (ordinary.has_value())
    return ordinary;
  return exact_scientific_decimal_display_shape_fact(*normalized);
}

// ---------------------------------------------------------------------------
// X2 shape data model (faithful port of the TS X2MantissaDataModel /
// X2ShapeDataModel parser, serializer and canonicalizer). The structural-shape
// path uses Cyrillic hex digits (С/Г/Е -> 12/13/14) so all shape-raw handling
// iterates UTF-8 code points rather than bytes.
// ---------------------------------------------------------------------------

enum class X2MantissaRadix { Decimal, Hex, Super };
enum class X2ShapeSafety { DotSafeDecimal, StructuralOnly, ErrorProne, Unknown };
enum class X2ShapeModelKind { Mantissa, ExponentEntry, Unknown };

struct X2MantissaDataModel {
  X2MantissaRadix radix = X2MantissaRadix::Decimal;
  std::string raw;
  std::string canonical;
  std::string sign;  // "" or "-"
  bool hasDecimalPoint = false;
  bool hasLeadingZero = false;
  std::vector<std::string> digits;  // one UTF-8 code-point string per shape digit
  int significantDigits = 0;
  std::optional<std::string> normalizedDecimal;
  bool normalizedSameAsRaw = false;
  X2ShapeSafety safety = X2ShapeSafety::Unknown;
};

struct X2ShapeDataModel {
  X2ShapeModelKind kind = X2ShapeModelKind::Unknown;
  X2ShapeSafety safety = X2ShapeSafety::Unknown;
  std::string raw;  // populated for the "unknown" kind (original fact)
  // Holds the model itself for the mantissa kind, or the nested mantissa for
  // the exponent-entry kind.
  std::optional<X2MantissaDataModel> mantissa;
  // exponent-entry only:
  std::string exponentRaw;
  std::string exponentSign;
  std::vector<std::string> exponentDigits;
  std::optional<std::string> normalizedDecimal;     // decimal exponent entry
  std::optional<std::string> closedDecimalDisplay;  // decimal exponent entry
  std::optional<X2MantissaDataModel> closedStructuralMantissa;  // structural exponent entry
};

std::vector<std::string> utf8_codepoints(const std::string& input) {
  std::vector<std::string> points;
  std::size_t index = 0;
  const std::size_t size = input.size();
  while (index < size) {
    const unsigned char lead = static_cast<unsigned char>(input.at(index));
    std::size_t length = 1;
    if (lead < 0x80U)
      length = 1;
    else if ((lead >> 5) == 0x6U)
      length = 2;
    else if ((lead >> 4) == 0xEU)
      length = 3;
    else if ((lead >> 3) == 0x1EU)
      length = 4;
    if (index + length > size)
      length = 1;
    points.push_back(input.substr(index, length));
    index += length;
  }
  return points;
}

std::string utf8_uppercase_codepoint(const std::string& point) {
  if (point.size() == 1U) {
    const char ch = point.at(0);
    if (ch >= 'a' && ch <= 'z')
      return std::string(1, static_cast<char>(ch - 32));
    return point;
  }
  if (point.size() == 2U) {
    const unsigned int byte0 = static_cast<unsigned char>(point.at(0));
    const unsigned int byte1 = static_cast<unsigned char>(point.at(1));
    const unsigned int code = ((byte0 & 0x1FU) << 6) | (byte1 & 0x3FU);
    unsigned int upper = code;
    if (code >= 0x0430U && code <= 0x044FU)
      upper = code - 0x20U;  // Cyrillic а-я -> А-Я
    else if (code == 0x0451U)
      upper = 0x0401U;  // ё -> Ё
    if (upper != code) {
      std::string output;
      output.push_back(static_cast<char>(0xC0U | (upper >> 6)));
      output.push_back(static_cast<char>(0x80U | (upper & 0x3FU)));
      return output;
    }
    return point;
  }
  return point;
}

std::optional<int> structural_hex_nibble_value(const std::string& digit) {
  if (digit.size() == 1U) {
    static const std::string kHex = "0123456789ABCDEF";
    const std::size_t index = kHex.find(digit.at(0));
    if (index != std::string::npos)
      return static_cast<int>(index);
  }
  if (digit == "\xD0\xA1")  // С U+0421
    return 12;
  if (digit == "\xD0\x93")  // Г U+0413
    return 13;
  if (digit == "\xD0\x95")  // Е U+0415
    return 14;
  return std::nullopt;
}

bool is_structural_hex_digit(const std::string& point) {
  return structural_hex_nibble_value(point).has_value();
}

bool is_structural_hex_shape_char(const std::string& point) {
  return point == "." || point == "-" || is_structural_hex_digit(point);
}

bool has_structural_non_decimal_digit(const std::string& raw) {
  for (const std::string& point : utf8_codepoints(raw)) {
    const std::optional<int> value = structural_hex_nibble_value(point);
    if (value.has_value() && *value >= 10)
      return true;
  }
  return false;
}

std::string canonical_shape_raw(const std::string& raw) {
  // Mirrors TS CANONICAL_SHAPE_RAW_CACHE: pure canonicalization on the hot path.
  static thread_local std::unordered_map<std::string, std::string> cache;
  const auto found = cache.find(raw);
  if (found != cache.end())
    return found->second;
  std::string output;
  for (const std::string& point : utf8_codepoints(raw)) {
    if (point.size() == 1U) {
      const char ch = point.at(0);
      if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v')
        continue;
      if (ch == ',') {
        output.push_back('.');
        continue;
      }
    }
    output += utf8_uppercase_codepoint(point);
  }
  cache.emplace(raw, output);
  return output;
}

std::optional<std::string> canonical_exponent_shape_raw(const std::string& raw) {
  const std::string canonical = canonical_shape_raw(raw);
  static const std::regex pattern(R"(^-?[0-9]{0,2}$)");
  return std::regex_match(canonical, pattern) ? std::optional<std::string>{canonical} : std::nullopt;
}

std::vector<std::string> shape_digits(const std::string& raw) {
  std::vector<std::string> digits;
  for (const std::string& point : utf8_codepoints(raw)) {
    if (is_structural_hex_digit(point))
      digits.push_back(point);
  }
  return digits;
}

bool decimal_has_leading_zero(const std::string& raw, const std::optional<std::string>& normalized) {
  if (!normalized.has_value())
    return false;
  static const std::regex pattern(R"(^-?0[0-9])");
  return raw != *normalized && std::regex_search(raw, pattern);
}

bool shape_has_leading_zero(const std::string& raw) {
  const std::vector<std::string> points = utf8_codepoints(raw);
  std::size_t start = (!points.empty() && points.front() == "-") ? 1U : 0U;
  if (start >= points.size() || points.at(start) != "0")
    return false;
  return start + 1U < points.size() && is_structural_hex_digit(points.at(start + 1U));
}

int significant_shape_digits(const std::vector<std::string>& digits) {
  for (std::size_t index = 0; index < digits.size(); ++index) {
    if (digits.at(index) != "0")
      return static_cast<int>(digits.size() - index);
  }
  return 1;
}

int decimal_mantissa_digit_count(const std::string& raw) {
  std::string value = raw;
  if (value.starts_with('-'))
    value = value.substr(1);
  const std::size_t dot = value.find('.');
  if (dot != std::string::npos)
    value.erase(dot, 1);
  return static_cast<int>(utf8_codepoints(value).size());
}

std::optional<std::string> normalize_decimal_mantissa_entry(const std::string& raw) {
  static const std::regex pattern(R"(^(-?)([0-9]{1,8})(?:\.([0-9]*))?$)");
  std::smatch match;
  if (!std::regex_match(raw, match, pattern))
    return std::nullopt;
  if (decimal_mantissa_digit_count(raw) > 8)
    return std::nullopt;
  const std::string sign = match[1].str();
  const std::string integer = match[2].str();
  if (!match[3].matched || match[3].str().empty())
    return normalize_plain_decimal(sign + integer);
  return normalize_plain_decimal(sign + integer + "." + match[3].str());
}

bool decimal_mantissa_shape_raw_is_valid(const std::string& raw) {
  return normalize_decimal_mantissa_entry(canonical_shape_raw(raw)).has_value();
}

bool decimal_exponent_mantissa_raw_is_valid(const std::string& raw) {
  const std::string canonical = canonical_shape_raw(raw);
  return decimal_mantissa_shape_raw_is_valid(canonical) ||
         normalize_plain_decimal(canonical).has_value();
}

bool structural_shape_raw_is_valid(const std::string& raw) {
  bool saw_digit = false;
  bool saw_decimal_point = false;
  for (const std::string& point : utf8_codepoints(canonical_shape_raw(raw))) {
    if (is_structural_hex_digit(point)) {
      saw_digit = true;
      continue;
    }
    if (point == ".") {
      if (saw_decimal_point)
        return false;
      saw_decimal_point = true;
      continue;
    }
    if (!is_structural_hex_shape_char(point))
      return false;
  }
  return saw_digit;
}

bool super_shape_raw_is_valid(const std::string& raw) {
  static const std::regex pattern(R"(^-?F[A-F]$)");
  return std::regex_match(canonical_shape_raw(raw), pattern);
}

bool is_signed_zero_decimal_mantissa_raw(const std::string& raw, const std::string& normalized) {
  return normalized == "0" && canonical_shape_raw(raw).starts_with('-');
}

X2ShapeSafety decimal_mantissa_shape_safety(const std::string& raw,
                                            const std::optional<std::string>& normalized) {
  if (!normalized.has_value())
    return X2ShapeSafety::Unknown;
  return is_signed_zero_decimal_mantissa_raw(raw, *normalized) ? X2ShapeSafety::ErrorProne
                                                               : X2ShapeSafety::DotSafeDecimal;
}

std::optional<std::string> structural_mantissa_normalized_decimal(const std::string& raw) {
  if (has_structural_non_decimal_digit(raw) || !decimal_mantissa_shape_raw_is_valid(raw))
    return std::nullopt;
  const std::optional<std::string> normalized = normalize_decimal_mantissa_entry(raw);
  if (!normalized.has_value() || significant_decimal_digits(*normalized) > 8)
    return std::nullopt;
  return normalized;
}

X2MantissaDataModel decimal_mantissa_data_model(const std::string& raw) {
  const std::string canonical = canonical_shape_raw(raw);
  const std::optional<std::string> normalized = normalize_plain_decimal(canonical);
  X2MantissaDataModel model;
  model.radix = X2MantissaRadix::Decimal;
  model.raw = raw;
  model.canonical = canonical;
  model.sign = canonical.starts_with('-') ? "-" : "";
  model.hasDecimalPoint = canonical.find('.') != std::string::npos;
  model.hasLeadingZero = decimal_has_leading_zero(canonical, normalized);
  model.digits = shape_digits(canonical);
  model.significantDigits = normalized.has_value() ? significant_decimal_digits(*normalized) : 0;
  model.normalizedDecimal = normalized;
  model.normalizedSameAsRaw = normalized.has_value() && canonical == *normalized;
  model.safety = decimal_mantissa_shape_safety(canonical, normalized);
  return model;
}

X2MantissaDataModel structural_mantissa_data_model(X2MantissaRadix radix, const std::string& raw,
                                                   X2ShapeSafety safety) {
  const std::string canonical = canonical_shape_raw(raw);
  const std::vector<std::string> digits = shape_digits(canonical);
  const std::optional<std::string> normalized = structural_mantissa_normalized_decimal(canonical);
  X2MantissaDataModel model;
  model.radix = radix;
  model.raw = raw;
  model.canonical = canonical;
  model.sign = canonical.starts_with('-') ? "-" : "";
  model.hasDecimalPoint = canonical.find('.') != std::string::npos;
  model.hasLeadingZero = shape_has_leading_zero(canonical);
  model.digits = digits;
  model.significantDigits = significant_shape_digits(digits);
  model.normalizedDecimal = normalized;
  model.normalizedSameAsRaw = normalized.has_value() && canonical == *normalized;
  model.safety = safety;
  return model;
}

std::optional<int> parse_small_exponent_int(const std::string& value) {
  const bool negative = value.starts_with('-');
  const std::string digits = negative ? value.substr(1) : value;
  if (digits.empty())
    return std::nullopt;
  int magnitude = 0;
  for (const char digit : digits) {
    if (digit < '0' || digit > '9')
      return std::nullopt;
    magnitude = magnitude * 10 + (digit - '0');
  }
  return negative ? -magnitude : magnitude;
}

std::optional<std::string> shifted_structural_mantissa_raw(const std::string& raw,
                                                           const std::string& exponent_raw) {
  const std::optional<std::string> exponent = canonical_exponent_shape_raw(exponent_raw);
  if (!exponent.has_value())
    return std::nullopt;
  if (*exponent == "" || *exponent == "0" || *exponent == "00")
    return raw;
  const std::optional<int> shift = parse_small_exponent_int(*exponent);
  if (!shift.has_value() || std::abs(*shift) > 7)
    return std::nullopt;
  const std::string sign = raw.starts_with('-') ? "-" : "";
  const std::string unsigned_value = sign.empty() ? raw : raw.substr(1);
  const std::vector<std::string> points = utf8_codepoints(unsigned_value);
  std::vector<std::size_t> dot_positions;
  for (std::size_t index = 0; index < points.size(); ++index) {
    if (points.at(index) == ".")
      dot_positions.push_back(index);
  }
  if (dot_positions.size() > 1U)
    return std::nullopt;
  std::vector<std::string> integer_points;
  std::vector<std::string> fraction_points;
  if (dot_positions.empty()) {
    integer_points = points;
  } else {
    const std::size_t dot = dot_positions.front();
    integer_points.assign(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(dot));
    fraction_points.assign(points.begin() + static_cast<std::ptrdiff_t>(dot) + 1, points.end());
  }
  if (integer_points.empty() && fraction_points.empty())
    return std::nullopt;
  std::vector<std::string> digit_points = integer_points;
  digit_points.insert(digit_points.end(), fraction_points.begin(), fraction_points.end());
  for (const std::string& point : digit_points) {
    if (!is_structural_hex_digit(point))
      return std::nullopt;
  }
  const auto join = [](const std::vector<std::string>& parts, std::size_t begin, std::size_t end) {
    std::string output;
    for (std::size_t index = begin; index < end; ++index)
      output += parts.at(index);
    return output;
  };
  const int point = static_cast<int>(integer_points.size()) + *shift;
  const int digit_count = static_cast<int>(digit_points.size());
  std::string shifted;
  if (point >= digit_count) {
    shifted = join(digit_points, 0, digit_points.size()) +
              std::string(static_cast<std::size_t>(point - digit_count), '0');
  } else if (point > 0) {
    shifted = join(digit_points, 0, static_cast<std::size_t>(point)) + "." +
              join(digit_points, static_cast<std::size_t>(point), digit_points.size());
  } else {
    shifted = "0." + std::string(static_cast<std::size_t>(-point), '0') +
              join(digit_points, 0, digit_points.size());
  }
  if (static_cast<int>(shape_digits(shifted).size()) > 8)
    return std::nullopt;
  return sign + shifted;
}

std::optional<X2MantissaDataModel> closed_structural_exponent_mantissa_model(
    const X2MantissaDataModel& mantissa, const std::string& exponent_raw) {
  if (mantissa.radix != X2MantissaRadix::Hex && mantissa.radix != X2MantissaRadix::Super)
    return std::nullopt;
  const std::optional<std::string> shifted =
      shifted_structural_mantissa_raw(mantissa.canonical, exponent_raw);
  if (!shifted.has_value())
    return std::nullopt;
  const X2MantissaRadix radix =
      (mantissa.radix == X2MantissaRadix::Super && *shifted != mantissa.canonical)
          ? X2MantissaRadix::Hex
          : mantissa.radix;
  return structural_mantissa_data_model(radix, *shifted, X2ShapeSafety::StructuralOnly);
}

std::string effective_exponent_mantissa_digits(const std::string& raw_digits) {
  const std::size_t first = raw_digits.find_first_not_of('0');
  const std::string stripped = first == std::string::npos ? std::string{} : raw_digits.substr(first);
  if (!stripped.empty())
    return stripped;
  const std::size_t zeros = raw_digits.size() > 0U ? raw_digits.size() - 1U : 0U;
  return "1" + std::string(zeros, '0');
}

struct ExponentMantissaDecimalParts {
  std::string sign;
  std::string digits;
  int scale = 0;
};

std::optional<ExponentMantissaDecimalParts> exponent_entry_mantissa_decimal_parts(
    const std::string& mantissa) {
  static const std::regex integer_pattern(R"(^(-?)([0-9]{1,8})$)");
  std::smatch match;
  if (std::regex_match(mantissa, match, integer_pattern)) {
    ExponentMantissaDecimalParts parts;
    parts.sign = match[1].str() == "-" ? "-" : "";
    parts.digits = effective_exponent_mantissa_digits(match[2].str());
    parts.scale = 0;
    return parts;
  }
  static const std::regex fractional_pattern(R"(^(-?)([0-9]{1,8})\.([0-9]+)$)");
  if (!std::regex_match(mantissa, match, fractional_pattern))
    return std::nullopt;
  const std::string integer_digits = match[2].str();
  const std::string fraction_digits = match[3].str();
  if (integer_digits.size() + fraction_digits.size() > 8U)
    return std::nullopt;
  ExponentMantissaDecimalParts parts;
  parts.sign = match[1].str() == "-" ? "-" : "";
  parts.digits = integer_digits + fraction_digits;
  parts.scale = static_cast<int>(fraction_digits.size());
  return parts;
}

std::optional<ExponentMantissaDecimalParts> exact_normalized_exponent_mantissa_decimal_parts(
    const std::string& mantissa) {
  const std::string canonical = canonical_shape_raw(mantissa);
  const std::optional<std::string> normalized = normalize_plain_decimal(canonical);
  if (!normalized.has_value() || *normalized != canonical ||
      significant_decimal_digits(*normalized) > 8)
    return std::nullopt;
  const std::optional<ExactDecimalParts> parts = parse_exact_decimal(*normalized);
  if (!parts.has_value())
    return std::nullopt;
  const std::string digits = parts->num.abs().magnitude();
  if (digits.size() > 80U)
    return std::nullopt;
  ExponentMantissaDecimalParts result;
  result.sign = parts->num.is_negative() ? "-" : "";
  result.digits = digits;
  result.scale = parts->scale;
  return result;
}

std::optional<ExponentMantissaDecimalParts> exponent_mantissa_decimal_parts(
    const std::string& mantissa) {
  const std::optional<ExponentMantissaDecimalParts> entry =
      exponent_entry_mantissa_decimal_parts(mantissa);
  if (entry.has_value())
    return entry;
  return exact_normalized_exponent_mantissa_decimal_parts(mantissa);
}

std::optional<std::string> normalized_exponent_entry_value(const std::string& mantissa,
                                                           const std::string& exponent) {
  static const std::regex exponent_pattern(R"(^(-?)([0-9]{1,2})$)");
  std::smatch match;
  const bool exponent_ok = std::regex_match(exponent, match, exponent_pattern);
  const std::optional<ExponentMantissaDecimalParts> parts = exponent_mantissa_decimal_parts(mantissa);
  if (!parts.has_value() || !exponent_ok)
    return std::nullopt;
  const std::string exponent_sign = match[1].str();
  const int shift = std::stoi(match[2].str());
  const int scale = exponent_sign == "-" ? parts->scale + shift : parts->scale - shift;
  if (static_cast<int>(parts->digits.size()) + std::max(0, -scale) > 80)
    return std::nullopt;
  const std::optional<std::string> unsigned_value = scaled_decimal_digits(parts->digits, scale);
  if (!unsigned_value.has_value())
    return std::nullopt;
  const std::optional<std::string> normalized = normalize_plain_decimal(parts->sign + *unsigned_value);
  if (!normalized.has_value() || significant_decimal_digits(*normalized) > 8)
    return std::nullopt;
  return normalized;
}

// Forward declarations for the mutually-recursive serializer / parser cluster.
X2ShapeDataModel x2_shape_data_model_for_fact(const X2ShapeFact& fact);
std::optional<X2ShapeFact> x2_shape_fact_from_data_model(const X2ShapeDataModel& model);
std::optional<X2ShapeFact> x2_exponent_shape_fact_from_mantissa_fact(const X2ShapeFact& fact,
                                                                     const std::string& exponent_raw);
std::optional<X2ShapeDataModel> x2_closed_exponent_display_data_model(const X2ShapeDataModel& model);

X2ShapeDataModel make_unknown_shape_model(const X2ShapeFact& fact) {
  X2ShapeDataModel model;
  model.kind = X2ShapeModelKind::Unknown;
  model.safety = X2ShapeSafety::Unknown;
  model.raw = fact;
  return model;
}

X2ShapeDataModel make_mantissa_shape_model(X2MantissaDataModel mantissa) {
  X2ShapeDataModel model;
  model.kind = X2ShapeModelKind::Mantissa;
  model.safety = mantissa.safety;
  model.mantissa = std::move(mantissa);
  return model;
}

X2ShapeDataModel x2_shape_data_model_for_fact_impl(const X2ShapeFact& fact) {
  std::smatch match;
  static const std::regex mantissa_pattern(R"(^mantissa:(.*):decimal$)");
  if (std::regex_match(fact, match, mantissa_pattern)) {
    const std::string raw = match[1].str();
    return decimal_mantissa_shape_raw_is_valid(raw) ? make_mantissa_shape_model(decimal_mantissa_data_model(raw))
                                                     : make_unknown_shape_model(fact);
  }
  static const std::regex exponent_pattern(R"(^exponent:([^:]*):([^:]*):decimal$)");
  if (std::regex_match(fact, match, exponent_pattern)) {
    const std::string mantissa_raw = match[1].str();
    const std::optional<std::string> exponent_raw = canonical_exponent_shape_raw(match[2].str());
    if (!decimal_exponent_mantissa_raw_is_valid(mantissa_raw) || !exponent_raw.has_value())
      return make_unknown_shape_model(fact);
    const std::optional<std::string> normalized_decimal =
        normalized_exponent_entry_value(mantissa_raw, *exponent_raw);
    X2ShapeDataModel model;
    model.kind = X2ShapeModelKind::ExponentEntry;
    model.safety = X2ShapeSafety::ErrorProne;
    model.mantissa = decimal_mantissa_data_model(mantissa_raw);
    model.exponentRaw = *exponent_raw;
    model.exponentSign = exponent_raw->starts_with('-') ? "-" : "";
    model.exponentDigits = shape_digits(*exponent_raw);
    model.normalizedDecimal = normalized_decimal;
    model.closedDecimalDisplay =
        normalized_decimal.has_value() ? exact_decimal_display_shape_fact(*normalized_decimal)
                                       : std::nullopt;
    return model;
  }
  static const std::regex hex_pattern(R"(^hex:(.*):mantissa$)");
  if (std::regex_match(fact, match, hex_pattern)) {
    const std::string raw = match[1].str();
    return structural_shape_raw_is_valid(raw)
               ? make_mantissa_shape_model(
                     structural_mantissa_data_model(X2MantissaRadix::Hex, raw, X2ShapeSafety::StructuralOnly))
               : make_unknown_shape_model(fact);
  }
  static const std::regex hex_exponent_pattern(R"(^hex-exponent:([^:]*):([^:]*)$)");
  if (std::regex_match(fact, match, hex_exponent_pattern)) {
    const std::optional<std::string> exponent_raw = canonical_exponent_shape_raw(match[2].str());
    if (!structural_shape_raw_is_valid(match[1].str()) || !exponent_raw.has_value())
      return make_unknown_shape_model(fact);
    const X2MantissaDataModel mantissa =
        structural_mantissa_data_model(X2MantissaRadix::Hex, match[1].str(), X2ShapeSafety::StructuralOnly);
    X2ShapeDataModel model;
    model.kind = X2ShapeModelKind::ExponentEntry;
    model.safety = X2ShapeSafety::StructuralOnly;
    model.mantissa = mantissa;
    model.exponentRaw = *exponent_raw;
    model.exponentSign = exponent_raw->starts_with('-') ? "-" : "";
    model.exponentDigits = shape_digits(*exponent_raw);
    model.closedStructuralMantissa = closed_structural_exponent_mantissa_model(mantissa, *exponent_raw);
    return model;
  }
  static const std::regex super_pattern(R"(^super:(.*)$)");
  if (std::regex_match(fact, match, super_pattern)) {
    const std::string raw = match[1].str();
    return super_shape_raw_is_valid(raw)
               ? make_mantissa_shape_model(structural_mantissa_data_model(X2MantissaRadix::Super, raw,
                                                                          X2ShapeSafety::StructuralOnly))
               : make_unknown_shape_model(fact);
  }
  static const std::regex super_exponent_pattern(R"(^super-exponent:([^:]*):([^:]*)$)");
  if (std::regex_match(fact, match, super_exponent_pattern)) {
    const std::optional<std::string> exponent_raw = canonical_exponent_shape_raw(match[2].str());
    if (!super_shape_raw_is_valid(match[1].str()) || !exponent_raw.has_value())
      return make_unknown_shape_model(fact);
    const X2MantissaDataModel mantissa =
        structural_mantissa_data_model(X2MantissaRadix::Super, match[1].str(), X2ShapeSafety::StructuralOnly);
    X2ShapeDataModel model;
    model.kind = X2ShapeModelKind::ExponentEntry;
    model.safety = X2ShapeSafety::StructuralOnly;
    model.mantissa = mantissa;
    model.exponentRaw = *exponent_raw;
    model.exponentSign = exponent_raw->starts_with('-') ? "-" : "";
    model.exponentDigits = shape_digits(*exponent_raw);
    model.closedStructuralMantissa = closed_structural_exponent_mantissa_model(mantissa, *exponent_raw);
    return model;
  }
  return make_unknown_shape_model(fact);
}

X2ShapeDataModel x2_shape_data_model_for_fact(const X2ShapeFact& fact) {
  // Mirrors TS X2_SHAPE_DATA_MODEL_CACHE: this fact->model parse is pure and
  // dominates the X2 dataflow fixpoint (regex-heavy), so memoize it.
  static thread_local std::unordered_map<std::string, X2ShapeDataModel> cache;
  const auto found = cache.find(fact);
  if (found != cache.end())
    return found->second;
  X2ShapeDataModel result = x2_shape_data_model_for_fact_impl(fact);
  cache.emplace(fact, result);
  return result;
}

std::optional<X2ShapeFact> x2_mantissa_shape_fact_from_parts(X2MantissaRadix radix,
                                                             const std::string& raw) {
  if (radix == X2MantissaRadix::Decimal)
    return decimal_mantissa_shape_raw_is_valid(raw) ? std::optional<X2ShapeFact>{decimal_mantissa_shape_fact(raw)}
                                                    : std::nullopt;
  if (radix == X2MantissaRadix::Hex)
    return structural_shape_raw_is_valid(raw)
               ? std::optional<X2ShapeFact>{"hex:" + canonical_shape_raw(raw) + ":mantissa"}
               : std::nullopt;
  if (radix == X2MantissaRadix::Super)
    return super_shape_raw_is_valid(raw)
               ? std::optional<X2ShapeFact>{"super:" + canonical_shape_raw(raw)}
               : std::nullopt;
  return std::nullopt;
}

std::optional<X2ShapeFact> x2_mantissa_shape_fact_from_model(const X2MantissaDataModel& model) {
  return x2_mantissa_shape_fact_from_parts(model.radix, model.canonical);
}

std::optional<X2ShapeFact> x2_exponent_shape_fact_from_mantissa_fact(const X2ShapeFact& fact,
                                                                     const std::string& exponent_raw) {
  const std::optional<std::string> exponent = canonical_exponent_shape_raw(exponent_raw);
  if (!exponent.has_value())
    return std::nullopt;
  std::smatch match;
  static const std::regex decimal_mantissa_pattern(R"(^mantissa:(.*):decimal$)");
  if (std::regex_match(fact, match, decimal_mantissa_pattern)) {
    const std::string mantissa = canonical_shape_raw(match[1].str());
    return decimal_exponent_mantissa_raw_is_valid(mantissa)
               ? std::optional<X2ShapeFact>{decimal_exponent_shape_fact(mantissa, *exponent)}
               : std::nullopt;
  }
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
  if (model.kind != X2ShapeModelKind::Mantissa)
    return std::nullopt;
  if (model.mantissa->radix == X2MantissaRadix::Hex)
    return std::optional<X2ShapeFact>{"hex-exponent:" + model.mantissa->canonical + ":" + *exponent};
  if (model.mantissa->radix == X2MantissaRadix::Super)
    return std::optional<X2ShapeFact>{"super-exponent:" + model.mantissa->canonical + ":" + *exponent};
  return std::nullopt;
}

std::optional<X2ShapeFact> x2_shape_fact_from_data_model(const X2ShapeDataModel& model) {
  if (model.kind == X2ShapeModelKind::Mantissa)
    return x2_mantissa_shape_fact_from_model(*model.mantissa);
  if (model.kind != X2ShapeModelKind::ExponentEntry)
    return std::nullopt;
  if (model.mantissa->radix == X2MantissaRadix::Decimal) {
    const std::optional<std::string> exponent = canonical_exponent_shape_raw(model.exponentRaw);
    return (!exponent.has_value() || !decimal_exponent_mantissa_raw_is_valid(model.mantissa->canonical))
               ? std::nullopt
               : std::optional<X2ShapeFact>{decimal_exponent_shape_fact(model.mantissa->canonical, *exponent)};
  }
  const std::optional<X2ShapeFact> mantissa = x2_mantissa_shape_fact_from_model(*model.mantissa);
  return !mantissa.has_value()
             ? std::nullopt
             : x2_exponent_shape_fact_from_mantissa_fact(*mantissa, model.exponentRaw);
}

std::optional<X2ShapeFact> x2_canonical_shape_fact_if_valid(const X2ShapeFact& fact) {
  return x2_shape_fact_from_data_model(x2_shape_data_model_for_fact(fact));
}

X2ShapeFact x2_canonical_shape_fact(const X2ShapeFact& fact) {
  return x2_canonical_shape_fact_if_valid(fact).value_or(fact);
}

std::optional<X2ShapeDataModel> x2_closed_exponent_display_data_model(const X2ShapeDataModel& model) {
  if (model.kind != X2ShapeModelKind::ExponentEntry)
    return std::nullopt;
  if (model.mantissa->radix != X2MantissaRadix::Decimal)
    return model.closedStructuralMantissa.has_value()
               ? std::optional<X2ShapeDataModel>{make_mantissa_shape_model(*model.closedStructuralMantissa)}
               : std::nullopt;
  if (!model.closedDecimalDisplay.has_value())
    return std::nullopt;
  return x2_shape_data_model_for_fact(*model.closedDecimalDisplay);
}

std::optional<X2ShapeFact> x2_closed_exponent_display_shape_fact(const X2ShapeFact& fact) {
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
  const std::optional<X2ShapeDataModel> closed = x2_closed_exponent_display_data_model(model);
  return closed.has_value() ? x2_shape_fact_from_data_model(*closed) : std::nullopt;
}

std::optional<X2MantissaDataModel> x2_closed_structural_exponent_mantissa_model(
    const X2ShapeDataModel& model) {
  const std::optional<X2ShapeDataModel> closed = x2_closed_exponent_display_data_model(model);
  if (closed.has_value() && closed->kind == X2ShapeModelKind::Mantissa &&
      (closed->mantissa->radix == X2MantissaRadix::Hex ||
       closed->mantissa->radix == X2MantissaRadix::Super))
    return closed->mantissa;
  return std::nullopt;
}

std::optional<X2ShapeFact> x2_closed_structural_exponent_mantissa_shape_fact(const X2ShapeFact& fact) {
  const std::optional<X2MantissaDataModel> mantissa =
      x2_closed_structural_exponent_mantissa_model(x2_shape_data_model_for_fact(fact));
  return mantissa.has_value() ? x2_mantissa_shape_fact_from_model(*mantissa) : std::nullopt;
}

std::optional<X2ShapeFact> x2_closed_decimal_exponent_display_shape_fact(const X2ShapeFact& fact) {
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
  return model.kind == X2ShapeModelKind::ExponentEntry &&
                 model.mantissa->radix == X2MantissaRadix::Decimal
             ? x2_closed_exponent_display_shape_fact(fact)
             : std::nullopt;
}

X2ShapeSafety x2_shape_fact_safety(const X2ShapeFact& fact) {
  return x2_shape_data_model_for_fact(fact).safety;
}

std::string shape_radix_name(X2MantissaRadix radix) {
  switch (radix) {
    case X2MantissaRadix::Decimal:
      return "decimal";
    case X2MantissaRadix::Hex:
      return "hex";
    case X2MantissaRadix::Super:
      return "super";
  }
  return "decimal";
}

std::string shape_safety_name(X2ShapeSafety safety) {
  switch (safety) {
    case X2ShapeSafety::DotSafeDecimal:
      return "dotSafeDecimal";
    case X2ShapeSafety::StructuralOnly:
      return "structuralOnly";
    case X2ShapeSafety::ErrorProne:
      return "errorProne";
    case X2ShapeSafety::Unknown:
      return "unknown";
  }
  return "unknown";
}

std::string join_codepoints(const std::vector<std::string>& points) {
  std::string output;
  for (const std::string& point : points)
    output += point;
  return output;
}

// Deterministic serialization of the shape data model, used by the differential
// unit test against the TS oracle. Also exercises the closed-exponent helpers.
std::string x2_shape_data_model_debug_impl(const X2ShapeFact& fact) {
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
  const std::string canon = x2_canonical_shape_fact(fact);
  const std::string rt = x2_shape_fact_from_data_model(model).value_or("_");
  const std::string safety = shape_safety_name(x2_shape_fact_safety(fact));
  if (model.kind == X2ShapeModelKind::Unknown)
    return "unknown|safety=" + safety + "|canonFact=" + canon + "|rt=" + rt;
  if (model.kind == X2ShapeModelKind::Mantissa) {
    const X2MantissaDataModel& mantissa = *model.mantissa;
    return "mantissa|radix=" + shape_radix_name(mantissa.radix) + "|canon=" + mantissa.canonical +
           "|sign=" + mantissa.sign + "|dp=" + (mantissa.hasDecimalPoint ? "1" : "0") +
           "|lz=" + (mantissa.hasLeadingZero ? "1" : "0") +
           "|digits=" + join_codepoints(mantissa.digits) +
           "|sig=" + std::to_string(mantissa.significantDigits) +
           "|norm=" + mantissa.normalizedDecimal.value_or("_") +
           "|same=" + (mantissa.normalizedSameAsRaw ? "1" : "0") + "|safety=" + safety +
           "|canonFact=" + canon + "|rt=" + rt;
  }
  const X2MantissaDataModel& mantissa = *model.mantissa;
  const std::string closed_exp = x2_closed_exponent_display_shape_fact(fact).value_or("_");
  const std::string closed_dec_exp = x2_closed_decimal_exponent_display_shape_fact(fact).value_or("_");
  const std::string closed_struct_m = x2_closed_structural_exponent_mantissa_shape_fact(fact).value_or("_");
  return "exp|mradix=" + shape_radix_name(mantissa.radix) + "|mcanon=" + mantissa.canonical +
         "|eraw=" + model.exponentRaw + "|esign=" + model.exponentSign +
         "|edigits=" + join_codepoints(model.exponentDigits) +
         "|norm=" + model.normalizedDecimal.value_or("_") +
         "|closedDecDisp=" + model.closedDecimalDisplay.value_or("_") + "|closedStruct=" +
         (model.closedStructuralMantissa.has_value() ? model.closedStructuralMantissa->canonical : "_") +
         "|closedExp=" + closed_exp + "|closedDecExp=" + closed_dec_exp +
         "|closedStructM=" + closed_struct_m + "|safety=" + safety + "|canonFact=" + canon +
         "|rt=" + rt;
}

std::optional<std::string> normalize_preloaded_shape_literal(const std::string& input) {
  const std::string normalized = canonical_shape_raw(input);
  const std::size_t length = utf8_codepoints(normalized).size();
  return length == 0U || length > 32U ? std::nullopt : std::optional<std::string>{normalized};
}

std::optional<X2ShapeFact> normalize_preloaded_structural_exponent_shape(const std::string& input) {
  const std::optional<std::string> normalized = normalize_preloaded_shape_literal(input);
  if (!normalized.has_value())
    return std::nullopt;
  static const std::regex pattern(R"(^(.+)E([+-]?[0-9]{1,2})$)");
  std::smatch match;
  if (!std::regex_match(*normalized, match, pattern))
    return std::nullopt;
  const std::string mantissa = match[1].str();
  if (!structural_shape_raw_is_valid(mantissa) || !has_structural_non_decimal_digit(mantissa))
    return std::nullopt;
  std::string exponent = match[2].str();
  if (exponent.starts_with('+'))
    exponent = exponent.substr(1);
  const X2ShapeFact mantissa_fact = super_shape_raw_is_valid(mantissa)
                                        ? "super:" + canonical_shape_raw(mantissa)
                                        : "hex:" + mantissa + ":mantissa";
  return x2_exponent_shape_fact_from_mantissa_fact(mantissa_fact, exponent);
}

std::optional<std::string> preloaded_constant_literal(const IrOp* op) {
  if (op == nullptr || !op->meta.comment.has_value())
    return std::nullopt;
  const std::string& comment = *op->meta.comment;
  const std::string lower = lower_ascii(comment);
  std::size_t marker = lower.find("preload const");
  while (marker != std::string::npos) {
    if (marker == 0 || lower.at(marker - 1U) == ';')
      break;
    marker = lower.find("preload const", marker + 1U);
  }
  if (marker == std::string::npos)
    return std::nullopt;
  std::size_t start = marker + std::string_view{"preload const"}.size();
  while (start < comment.size() && std::isspace(static_cast<unsigned char>(comment.at(start))) != 0)
    ++start;
  const std::size_t semicolon = comment.find(';', start);
  std::string literal = trim_ascii_copy(comment.substr(start, semicolon == std::string::npos
                                                                 ? std::string::npos
                                                                 : semicolon - start));
  std::string lower_literal = lower_ascii(literal);
  std::size_t suffix = std::string::npos;
  for (std::string_view word : {" base", " left", " right", " stack"}) {
    const std::size_t found = lower_literal.find(word);
    if (found != std::string::npos)
      suffix = std::min(suffix, found);
  }
  if (suffix != std::string::npos)
    literal = trim_ascii_copy(literal.substr(0, suffix));
  return literal.empty() ? std::nullopt : std::optional<std::string>{literal};
}

X2ValueSet preloaded_constant_value_facts(const IrOp* op) {
  const std::optional<std::string> literal = preloaded_constant_literal(op);
  const std::optional<std::string> decimal =
      literal.has_value() ? normalize_preloaded_decimal_literal(*literal) : std::nullopt;
  return decimal.has_value() ? X2ValueSet{decimal_value_fact(*decimal, "normalized")} : X2ValueSet{};
}

X2ShapeSet preloaded_constant_shape_facts(const IrOp* op) {
  const std::optional<std::string> literal = preloaded_constant_literal(op);
  if (!literal.has_value())
    return {};
  const std::optional<std::string> decimal = normalize_preloaded_decimal_literal(*literal);
  if (decimal.has_value()) {
    const std::optional<X2ShapeFact> shape = exact_decimal_display_shape_fact(*decimal);
    return shape.has_value() ? X2ShapeSet{*shape} : X2ShapeSet{};
  }
  const std::optional<X2ShapeFact> structural_exponent =
      normalize_preloaded_structural_exponent_shape(*literal);
  if (structural_exponent.has_value())
    return X2ShapeSet{*structural_exponent};
  const std::optional<std::string> shape = normalize_preloaded_shape_literal(*literal);
  if (!shape.has_value())
    return {};
  if (super_shape_raw_is_valid(*shape))
    return X2ShapeSet{"super:" + canonical_shape_raw(*shape)};
  if (structural_shape_raw_is_valid(*shape) && has_structural_non_decimal_digit(*shape))
    return X2ShapeSet{"hex:" + *shape + ":mantissa"};
  return {};
}

} // namespace

std::optional<std::string> concrete_decimal_binary_value(int opcode, const std::string& y,
                                                         const std::string& x) {
  return concrete_decimal_binary_value_impl(opcode, y, x);
}

std::optional<std::string> concrete_decimal_unary_value(int opcode, const std::string& value) {
  return concrete_decimal_unary_value_impl(opcode, value);
}

std::string x2_shape_data_model_debug(const X2ShapeFact& fact) {
  return x2_shape_data_model_debug_impl(fact);
}

std::optional<std::string> x2_value_fact_restored_visible_decimal(const X2ValueFact& fact) {
  constexpr std::string_view prefix = "decimal:";
  constexpr std::string_view normalized_suffix = ":normalized";
  constexpr std::string_view unnormalized_suffix = ":unnormalized";
  if (!fact.starts_with(prefix))
    return std::nullopt;
  if (fact.ends_with(normalized_suffix)) {
    return normalize_plain_decimal(
        fact.substr(prefix.size(), fact.size() - prefix.size() - normalized_suffix.size()));
  }
  if (fact.ends_with(unnormalized_suffix)) {
    return normalize_plain_decimal(
        fact.substr(prefix.size(), fact.size() - prefix.size() - unnormalized_suffix.size()));
  }
  return std::nullopt;
}

std::optional<std::string> x2_shape_fact_restored_visible_decimal(const X2ShapeFact& fact) {
  constexpr std::string_view mantissa_prefix = "mantissa:";
  constexpr std::string_view decimal_suffix = ":decimal";
  if (!fact.starts_with(mantissa_prefix) || !fact.ends_with(decimal_suffix))
    return std::nullopt;
  return fact.substr(mantissa_prefix.size(),
                     fact.size() - mantissa_prefix.size() - decimal_suffix.size());
}

std::set<std::string>
x2_value_shape_set_restored_visible_decimals(const X2ValueSet* values,
                                             const X2ShapeSet* shapes) {
  std::set<std::string> output;
  if (values != nullptr) {
    for (const X2ValueFact& fact : *values) {
      if (const std::optional<std::string> visible =
              x2_value_fact_restored_visible_decimal(fact)) {
        output.insert(*visible);
      }
    }
  }
  if (shapes != nullptr) {
    for (const X2ShapeFact& fact : *shapes) {
      if (const std::optional<std::string> visible =
              x2_shape_fact_restored_visible_decimal(fact)) {
        output.insert(*visible);
      }
    }
  }
  return output;
}

bool string_sets_intersect(const std::set<std::string>& left,
                           const std::set<std::string>& right) {
  for (const std::string& value : left) {
    if (right.contains(value))
      return true;
  }
  return false;
}

bool x2_value_shape_sets_have_same_restored_visible_decimal(
    const X2ValueSet* left_values, const X2ShapeSet* left_shapes,
    const X2ValueSet* right_values, const X2ShapeSet* right_shapes) {
  const std::set<std::string> left =
      x2_value_shape_set_restored_visible_decimals(left_values, left_shapes);
  if (left.empty())
    return false;
  const std::set<std::string> right =
      x2_value_shape_set_restored_visible_decimals(right_values, right_shapes);
  return string_sets_intersect(left, right);
}

bool x2_state_has_same_visible_x_and_y(
    const std::optional<X2ValueDataflowState>& state) {
  if (!state.has_value())
    return false;
  const X2ValueSet* y_values = state->y.has_value() ? &*state->y : nullptr;
  const X2ShapeSet* y_shapes = state->yShape.has_value() ? &*state->yShape : nullptr;
  return optional_x2_value_sets_intersect(state->x, state->y) ||
         x2_value_shape_sets_have_same_restored_visible_decimal(&state->x, &state->xShape,
                                                                y_values, y_shapes);
}

namespace {
namespace x2eval {
std::optional<std::string>
recall_already_synced_in_x2_structural_shape(const IrOp& op,
                                             const std::optional<X2ValueDataflowState>& state);
std::optional<std::string>
recall_already_synced_in_x2_vp_shape(const IrOp& op,
                                     const std::optional<X2ValueDataflowState>& state);
std::optional<std::string>
recall_already_in_x_memory_value(const IrOp& op,
                                 const std::optional<X2ValueDataflowState>& state);
std::optional<std::string>
recall_already_in_x_preloaded_decimal(const IrOp& op,
                                      const std::optional<X2ValueDataflowState>& state);
std::optional<std::string>
recall_already_in_x_restored_visible_decimal(const IrOp& op,
                                             const std::optional<X2ValueDataflowState>& state);
std::optional<std::string>
recall_already_in_x_restored_display_shape(const IrOp& op,
                                           const std::optional<X2ValueDataflowState>& state);
std::optional<std::string>
recall_already_synced_in_x2_value(const IrOp& op,
                                  const std::optional<X2ValueDataflowState>& state);
std::optional<std::string>
recall_already_synced_in_x2_memory_value(const IrOp& op,
                                         const std::optional<X2ValueDataflowState>& state);
std::optional<std::string>
recall_already_synced_in_x2_preloaded_decimal(const IrOp& op,
                                              const std::optional<X2ValueDataflowState>& state);
std::optional<std::string>
recall_already_synced_in_x2_restored_visible_decimal(
    const IrOp& op, const std::optional<X2ValueDataflowState>& state);
bool recall_removal_preserves_immediate_vp_restore_context(
    const std::vector<IrOp>& ops, int recall_index,
    const std::optional<X2ValueDataflowState>& state,
    const std::optional<RecallValueProof>& value_proof,
    const DirectReturnAnalysisContext* context);
}  // namespace x2eval
}  // namespace

std::optional<RecallValueProof>
recall_value_proof(const IrOp& op, const std::optional<X2ValueDataflowState>& state) {
  const std::optional<std::string> register_name = removable_recall_value_register(op);
  if (!register_name.has_value() || !state.has_value())
    return std::nullopt;

  const bool in_x =
      x2_value_set_has_register(state->x, *register_name) ||
      x2eval::recall_already_in_x_memory_value(op, state) == register_name ||
      x2eval::recall_already_in_x_preloaded_decimal(op, state) == register_name ||
      x2eval::recall_already_in_x_restored_visible_decimal(op, state) == register_name ||
      x2eval::recall_already_in_x_restored_display_shape(op, state) == register_name;
  const std::optional<std::string> x2_sync_register =
      x2eval::recall_already_synced_in_x2_value(op, state);
  const bool x2_sync_value =
      x2eval::recall_already_synced_in_x2_memory_value(op, state).has_value() ||
      x2eval::recall_already_synced_in_x2_preloaded_decimal(op, state).has_value();
  const bool x2_sync_display_value =
      !x2_sync_value &&
      x2eval::recall_already_synced_in_x2_restored_visible_decimal(op, state) == register_name;
  const bool x2_sync_shape =
      x2eval::recall_already_synced_in_x2_structural_shape(op, state) == register_name;
  const bool x2_sync_vp_shape =
      !x2_sync_shape &&
      x2eval::recall_already_synced_in_x2_vp_shape(op, state) == register_name;
  return RecallValueProof{
      .register_name = *register_name,
      .in_x = in_x,
      .x2_sync_register = x2_sync_register,
      .x2_sync_value = x2_sync_value,
      .x2_sync_display_value = x2_sync_display_value,
      .x2_sync_shape = x2_sync_shape,
      .x2_sync_vp_shape = x2_sync_vp_shape,
  };
}

std::optional<RecallRemovalAnalysis>
analyze_recall_removal(const std::vector<IrOp>& ops, int recall_index,
                       const std::optional<RegisterValueSet>& x2_register_state,
                       const std::optional<X2ValueDataflowState>& x2_value_state,
                       const DirectReturnAnalysisContext* context) {
  if (recall_index < 0 || recall_index >= static_cast<int>(ops.size()))
    return std::nullopt;
  const IrOp& op = ops.at(static_cast<std::size_t>(recall_index));
  const std::optional<std::string> register_name = removable_recall_value_register(op);
  if (!register_name.has_value())
    return std::nullopt;

  std::optional<std::string> redundant_sync_register;
  if (x2_register_state.has_value() && x2_register_state->contains(*register_name))
    redundant_sync_register = *register_name;
  const std::optional<RecallValueProof> value_proof =
      recall_value_proof(op, x2_value_state);
  if (!redundant_sync_register.has_value() && value_proof.has_value() &&
      value_proof->x2_sync_register.has_value()) {
    redundant_sync_register = value_proof->x2_sync_register;
  }
  const bool redundant_sync_value =
      value_proof.has_value() && value_proof->x2_sync_value;
  const bool redundant_sync_display_value =
      value_proof.has_value() && value_proof->x2_sync_display_value;
  X2RestoreExposureOptions display_value_context_options;
  display_value_context_options.redundant_sync_display_value = true;
  const bool redundant_sync_display_value_for_context =
      redundant_sync_display_value &&
      !removing_recall_can_expose_x2_restore(ops, recall_index,
                                             display_value_context_options);
  const bool redundant_sync_shape =
      value_proof.has_value() && value_proof->x2_sync_shape;
  const bool redundant_sync_vp_shape =
      value_proof.has_value() && value_proof->x2_sync_vp_shape;

  const bool exposes_stack_lift = removing_recall_can_expose_stack_lift(ops, recall_index);
  X2RestoreExposureOptions exposure_options;
  exposure_options.redundant_sync_register = redundant_sync_register;
  exposure_options.redundant_sync_value = redundant_sync_value;
  exposure_options.redundant_sync_display_value = redundant_sync_display_value;
  exposure_options.redundant_sync_shape = redundant_sync_shape;
  exposure_options.redundant_sync_vp_shape = redundant_sync_vp_shape;
  const bool exposes_x2_restore =
      removing_recall_can_expose_x2_restore(ops, recall_index, exposure_options) &&
      !x2eval::recall_removal_preserves_immediate_vp_restore_context(ops, recall_index,
                                                                     x2_value_state, value_proof,
                                                                     context);

  return RecallRemovalAnalysis{
      .register_name = *register_name,
      .value_proof = value_proof,
      .redundant_sync_register = redundant_sync_register,
      .redundant_sync_value = redundant_sync_value,
      .redundant_sync_display_value = redundant_sync_display_value,
      .redundant_sync_shape = redundant_sync_shape,
      .x2_sync_redundant = redundant_sync_register.has_value() || redundant_sync_value ||
                           redundant_sync_display_value_for_context || redundant_sync_shape,
      .exposes_stack_lift = exposes_stack_lift,
      .exposes_x2_restore = exposes_x2_restore,
      .removable = !exposes_stack_lift && !exposes_x2_restore,
  };
}

bool x2_is_stack_lift_and_x2_sync_producer(const IrOp& op) {
  return !has_rewrite_barrier(op) && !is_display_focus_sensitive(op) &&
         analyze_x2_stack_effect(op).stack_lift_and_x2_sync;
}

bool x2_is_stack_x_and_x2_preserving_linear_op(const IrOp& op) {
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op))
    return false;
  switch (op.kind) {
  case IrKind::Label:
  case IrKind::Store:
  case IrKind::IndirectStore:
  case IrKind::OrphanAddress:
    return true;
  case IrKind::Plain: {
    const X2StackEffectAnalysis effect = analyze_x2_stack_effect(op);
    return effect.stack_preserves && effect.x2_preserves && plain_preserves_x_value(op);
  }
  default:
    return false;
  }
}

bool x2_is_strict_stack_preserving_linear_op(const IrOp& op) {
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op))
    return false;
  switch (op.kind) {
  case IrKind::Label:
  case IrKind::Store:
  case IrKind::IndirectStore:
  case IrKind::OrphanAddress:
    return true;
  case IrKind::Plain: {
    const X2StackEffectAnalysis effect = analyze_x2_stack_effect(op);
    return effect.stack_preserves && !effect.x2_restores;
  }
  default:
    return false;
  }
}

bool x2_known_return_call_preserves_stack_x_and_x2(
    const std::vector<IrOp>& ops, const IrOp& call,
    const DirectReturnAnalysisContext& context) {
  return known_return_call_returns_through_nested_transparent_range(
      ops, call, context, x2_is_stack_x_and_x2_preserving_linear_op);
}

bool x2_simple_direct_return_preserves_stack(const std::vector<IrOp>& ops,
                                            const IrOp& call,
                                            const DirectReturnAnalysisContext& context) {
  return known_return_call_returns_through_nested_transparent_range(
      ops, call, context, x2_is_strict_stack_preserving_linear_op);
}

bool x2_is_fallthrough_stack_preserving_gap_op(const IrOp& op) {
  if (!has_rewrite_barrier(op) &&
      (op.kind == IrKind::CondJump || op.kind == IrKind::Loop)) {
    return true;
  }
  if (op.kind == IrKind::IndirectCondJump && known_indirect_flow_target(op).has_value() &&
      !has_rewrite_barrier(op) && analyze_x2_stack_effect(op).stack_preserves) {
    return true;
  }
  return x2_is_strict_stack_preserving_linear_op(op);
}

bool x2_is_known_fallthrough_stack_preserving_conditional(const IrOp& op) {
  if (op.kind != IrKind::CondJump && op.kind != IrKind::Loop &&
      op.kind != IrKind::IndirectCondJump)
    return false;
  if (op.kind == IrKind::IndirectCondJump && !known_indirect_flow_target(op).has_value())
    return false;
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op))
    return false;
  return analyze_x2_stack_effect(op).stack_preserves;
}

bool x2_is_known_fallthrough_stack_x2_gap_op(const IrOp& op) {
  if (!x2_is_known_fallthrough_stack_preserving_conditional(op))
    return false;
  const X2Effect fallthrough =
      conditional_x2_effect_for_graph_edge(op, X2DataflowEdgeKind::Fallthrough);
  return fallthrough == X2Effect::Affects || fallthrough == X2Effect::Preserves;
}

bool x2_known_return_call_reaches_stack_lift_and_x2_sync(
    const std::vector<IrOp>& ops, const IrOp& call,
    const DirectReturnAnalysisContext& context);

bool x2_is_fallthrough_sync_conditional_op(const IrOp& op) {
  if (op.kind != IrKind::CondJump && op.kind != IrKind::Loop &&
      op.kind != IrKind::IndirectCondJump)
    return false;
  if (op.kind == IrKind::IndirectCondJump && !known_indirect_flow_target(op).has_value())
    return false;
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op))
    return false;
  const X2StackEffectAnalysis effect = analyze_x2_stack_effect(op);
  if (!effect.stack_preserves)
    return false;
  const X2Effect fallthrough =
      conditional_x2_effect_for_graph_edge(op, X2DataflowEdgeKind::Fallthrough);
  const X2Effect jump = conditional_x2_effect_for_graph_edge(op, X2DataflowEdgeKind::Jump);
  return fallthrough == X2Effect::Affects && jump == X2Effect::Preserves;
}

bool x2_is_plain_x_preserving_x2_sync(const IrOp& op) {
  if (op.kind != IrKind::Plain || has_rewrite_barrier(op) || is_display_focus_sensitive(op))
    return false;
  const X2StackEffectAnalysis effect = analyze_x2_stack_effect(op);
  return effect.stack_preserves && effect.x2_affects && plain_preserves_x_value(op);
}

bool x2_known_return_call_reaches_stack_preserving_x2_sync(
    const std::vector<IrOp>& ops, const IrOp& call,
    const DirectReturnAnalysisContext& context) {
  return known_return_call_returns_through_nested_transparent_range(
      ops, call, context, x2_is_strict_stack_preserving_linear_op);
}

bool x2_is_x_preserving_sync_op(const std::vector<IrOp>& ops, const IrOp& op,
                                const DirectReturnAnalysisContext& context) {
  if (x2_is_fallthrough_sync_conditional_op(op))
    return true;
  if (is_known_return_call_op(op) &&
      x2_known_return_call_preserves_stack_x_and_x2(ops, op, context))
    return true;
  if (op.kind == IrKind::Return && !has_rewrite_barrier(op) &&
      !is_display_focus_sensitive(op))
    return true;
  return x2_is_plain_x_preserving_x2_sync(op);
}

bool x2_is_previous_x_preserving_sync_op(const std::vector<IrOp>& ops,
                                         const IrOp& op,
                                         const DirectReturnAnalysisContext& context) {
  if (x2_is_fallthrough_sync_conditional_op(op))
    return true;
  if (is_known_return_call_op(op) &&
      x2_known_return_call_preserves_stack_x_and_x2(ops, op, context))
    return true;
  return x2_is_plain_x_preserving_x2_sync(op);
}

bool x2_conditional_jump_can_enter_scanned_range(const IrOp& op, int index, int end,
                                                 const DirectReturnAnalysisContext& context) {
  if (op.kind != IrKind::CondJump && op.kind != IrKind::Loop)
    return false;
  std::optional<int> target;
  if (const auto* label = std::get_if<std::string>(&op.target)) {
    const auto found = context.labels.find(*label);
    if (found != context.labels.end())
      target = found->second;
  } else if (const auto* address = std::get_if<int>(&op.target)) {
    const auto found = context.addresses.find(*address);
    if (found != context.addresses.end())
      target = found->second;
  }
  return target.has_value() && *target > index && *target <= end;
}

bool x2_is_fallthrough_x2_preserving_gap_op(const IrOp& op) {
  if (op.kind != IrKind::CondJump && op.kind != IrKind::Loop &&
      op.kind != IrKind::IndirectCondJump)
    return false;
  if (op.kind == IrKind::IndirectCondJump && !known_indirect_flow_target(op).has_value())
    return false;
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op))
    return false;
  const X2StackEffectAnalysis effect = analyze_x2_stack_effect(op);
  if (!effect.stack_preserves)
    return false;
  return conditional_x2_effect_for_graph_edge(op, X2DataflowEdgeKind::Fallthrough) ==
         X2Effect::Preserves;
}

bool x2_is_backward_stack_lift_x2_sync_gap_op(
    const std::vector<IrOp>& ops, const IrOp& op, int index,
    const DirectReturnAnalysisContext& context) {
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op))
    return false;
  if (op.kind == IrKind::Label)
    return !context.label_entries.contains(index);
  if (is_known_return_call_op(op))
    return x2_known_return_call_preserves_stack_x_and_x2(ops, op, context);
  if (x2_is_known_fallthrough_stack_x2_gap_op(op))
    return true;
  switch (op.kind) {
  case IrKind::Store:
  case IrKind::IndirectStore:
  case IrKind::OrphanAddress:
    return true;
  case IrKind::Plain: {
    const X2StackEffectAnalysis effect = analyze_x2_stack_effect(op);
    return effect.stack_preserves && effect.x2_preserves && plain_preserves_x_value(op);
  }
  default:
    return false;
  }
}

bool x2_is_stack_x_and_x2_preserving_gap_op(
    const std::vector<IrOp>& ops, const IrOp& op, int index,
    const DirectReturnAnalysisContext& context) {
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op))
    return false;
  if (op.kind == IrKind::Label)
    return !context.label_entries.contains(index);
  if (is_known_return_call_op(op))
    return x2_known_return_call_preserves_stack_x_and_x2(ops, op, context);
  if (x2_is_fallthrough_x2_preserving_gap_op(op))
    return true;
  switch (op.kind) {
  case IrKind::Store:
  case IrKind::IndirectStore:
  case IrKind::OrphanAddress:
    return true;
  case IrKind::Plain: {
    const X2StackEffectAnalysis effect = analyze_x2_stack_effect(op);
    return effect.stack_preserves && effect.x2_preserves && plain_preserves_x_value(op);
  }
  default:
    return false;
  }
}

bool x2_is_forward_return_x2_sync_gap_op(
    const std::vector<IrOp>& ops, const IrOp& op, int index,
    const DirectReturnAnalysisContext& context) {
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op))
    return false;
  if (op.kind == IrKind::Label)
    return !context.label_entries.contains(index);
  if (is_known_return_call_op(op))
    return x2_known_return_call_preserves_stack_x_and_x2(ops, op, context);
  if (x2_is_fallthrough_stack_preserving_gap_op(op))
    return true;
  switch (op.kind) {
  case IrKind::Store:
  case IrKind::IndirectStore:
  case IrKind::OrphanAddress:
    return true;
  case IrKind::Plain: {
    const X2StackEffectAnalysis effect = analyze_x2_stack_effect(op);
    return effect.stack_preserves && !effect.x2_restores;
  }
  default:
    return false;
  }
}

std::optional<int> x2_next_stack_shifting_producer_index(
    const std::vector<IrOp>& ops, int start, const DirectReturnAnalysisContext& context) {
  for (int index = start; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (analyze_x2_stack_effect(op).stack_shifts)
      return index;
    if (is_known_return_call_op(op) &&
        x2_known_return_call_reaches_stack_lift_and_x2_sync(ops, op, context))
      return index;
    if (is_known_return_call_op(op) &&
        x2_simple_direct_return_preserves_stack(ops, op, context))
      continue;
    if (!x2_is_fallthrough_stack_preserving_gap_op(op))
      return std::nullopt;
  }
  return std::nullopt;
}

std::optional<int> x2_next_hard_x2_overwrite_index(
    const std::vector<IrOp>& ops, int start, const DirectReturnAnalysisContext& context) {
  for (int index = start; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (analyze_x2_stack_effect(op).hard_x2_overwrite_without_stack_use)
      return index;
    if (is_known_return_call_op(op) &&
        x2_simple_direct_return_preserves_stack(ops, op, context))
      continue;
    if (!x2_is_fallthrough_stack_preserving_gap_op(op))
      return std::nullopt;
  }
  return std::nullopt;
}

std::optional<int> x2_next_x_preserving_x2_sync_index(
    const std::vector<IrOp>& ops, int start, const DirectReturnAnalysisContext& context) {
  for (int index = start; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (x2_is_x_preserving_sync_op(ops, op, context))
      return index;
    if (!x2_is_stack_x_and_x2_preserving_gap_op(ops, op, index, context))
      return std::nullopt;
  }
  return std::nullopt;
}

std::optional<int> x2_previous_x_preserving_x2_sync_index(
    const std::vector<IrOp>& ops, int end, const DirectReturnAnalysisContext& context) {
  for (int index = end - 1; index >= 0; --index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (x2_is_previous_x_preserving_sync_op(ops, op, context) &&
        !x2_conditional_jump_can_enter_scanned_range(op, index, end, context))
      return index;
    if (!x2_is_stack_x_and_x2_preserving_gap_op(ops, op, index, context))
      return std::nullopt;
  }
  return std::nullopt;
}

std::optional<int> x2_previous_hard_x2_overwrite_index(
    const std::vector<IrOp>& ops, int end, const DirectReturnAnalysisContext& context) {
  for (int index = end - 1; index >= 0; --index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (!has_rewrite_barrier(op) && !is_display_focus_sensitive(op) &&
        analyze_x2_stack_effect(op).hard_x2_overwrite_without_stack_use)
      return index;
    if (!x2_is_stack_x_and_x2_preserving_gap_op(ops, op, index, context))
      return std::nullopt;
  }
  return std::nullopt;
}

std::optional<int> x2_next_stack_preserving_return_x2_sync_index(
    const std::vector<IrOp>& ops, int start, const DirectReturnAnalysisContext& context) {
  for (int index = start; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (is_known_return_call_op(op) &&
        x2_known_return_call_reaches_stack_preserving_x2_sync(ops, op, context))
      return index;
    if (!x2_is_forward_return_x2_sync_gap_op(ops, op, index, context))
      return std::nullopt;
  }
  return std::nullopt;
}

std::optional<int> x2_previous_stack_preserving_return_x2_sync_index(
    const std::vector<IrOp>& ops, int end, const DirectReturnAnalysisContext& context) {
  for (int index = end - 1; index >= 0; --index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (is_known_return_call_op(op) &&
        x2_known_return_call_reaches_stack_preserving_x2_sync(ops, op, context))
      return index;
    if (!x2_is_stack_x_and_x2_preserving_gap_op(ops, op, index, context))
      return std::nullopt;
  }
  return std::nullopt;
}

std::optional<int> x2_previous_stack_lift_and_x2_sync_producer_index(
    const std::vector<IrOp>& ops, int end, const DirectReturnAnalysisContext& context) {
  for (int index = end - 1; index >= 0; --index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (x2_is_stack_lift_and_x2_sync_producer(op))
      return index;
    if (is_known_return_call_op(op) &&
        x2_known_return_call_reaches_stack_lift_and_x2_sync(ops, op, context))
      return index;
    if (!x2_is_backward_stack_lift_x2_sync_gap_op(ops, op, index, context))
      return std::nullopt;
  }
  return std::nullopt;
}

enum class X2ReturnStackLiftSyncState {
  Invalid,
  Transparent,
  Producer,
};

X2ReturnStackLiftSyncState x2_known_return_call_stack_lift_and_x2_sync_state(
    const std::vector<IrOp>& ops, const IrOp& call,
    const DirectReturnAnalysisContext& context,
    std::map<int, X2ReturnStackLiftSyncState>& memo, std::set<int>& active);

X2ReturnStackLiftSyncState x2_linear_return_range_stack_lift_and_x2_sync_state(
    const std::vector<IrOp>& ops, int target_index,
    const DirectReturnAnalysisContext& context,
    std::map<int, X2ReturnStackLiftSyncState>& memo, std::set<int>& active) {
  bool saw_producer = false;
  const int start_index = ops.at(static_cast<std::size_t>(target_index)).kind == IrKind::Label
                              ? target_index + 1
                              : target_index;
  for (int index = start_index; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label) {
      if (context.label_entries.contains(index))
        return X2ReturnStackLiftSyncState::Invalid;
      continue;
    }
    if (has_rewrite_barrier(op) || is_display_focus_sensitive(op))
      return X2ReturnStackLiftSyncState::Invalid;
    if (op.kind == IrKind::Return) {
      return saw_producer ? X2ReturnStackLiftSyncState::Producer
                          : X2ReturnStackLiftSyncState::Transparent;
    }
    if (is_known_return_call_op(op)) {
      const X2ReturnStackLiftSyncState nested =
          x2_known_return_call_stack_lift_and_x2_sync_state(ops, op, context, memo, active);
      if (nested == X2ReturnStackLiftSyncState::Invalid)
        return X2ReturnStackLiftSyncState::Invalid;
      if (nested == X2ReturnStackLiftSyncState::Producer) {
        if (saw_producer)
          return X2ReturnStackLiftSyncState::Invalid;
        saw_producer = true;
      }
      continue;
    }
    if (x2_is_stack_lift_and_x2_sync_producer(op)) {
      if (saw_producer)
        return X2ReturnStackLiftSyncState::Invalid;
      saw_producer = true;
      continue;
    }
    const bool transparent = saw_producer ? x2_is_stack_x_and_x2_preserving_linear_op(op)
                                          : x2_is_strict_stack_preserving_linear_op(op);
    if (!transparent)
      return X2ReturnStackLiftSyncState::Invalid;
  }
  return X2ReturnStackLiftSyncState::Invalid;
}

X2ReturnStackLiftSyncState x2_known_return_call_stack_lift_and_x2_sync_state(
    const std::vector<IrOp>& ops, const IrOp& call,
    const DirectReturnAnalysisContext& context,
    std::map<int, X2ReturnStackLiftSyncState>& memo, std::set<int>& active) {
  const std::optional<int> target_index = known_return_call_target_index(call, context);
  if (!target_index.has_value())
    return X2ReturnStackLiftSyncState::Invalid;
  const auto cached = memo.find(*target_index);
  if (cached != memo.end())
    return cached->second;
  if (active.contains(*target_index))
    return X2ReturnStackLiftSyncState::Invalid;

  active.insert(*target_index);
  const X2ReturnStackLiftSyncState state =
      x2_linear_return_range_stack_lift_and_x2_sync_state(ops, *target_index, context, memo,
                                                          active);
  active.erase(*target_index);
  memo[*target_index] = state;
  return state;
}

bool x2_known_return_call_reaches_stack_lift_and_x2_sync(
    const std::vector<IrOp>& ops, const IrOp& call,
    const DirectReturnAnalysisContext& context) {
  std::map<int, X2ReturnStackLiftSyncState> memo;
  std::set<int> active;
  return x2_known_return_call_stack_lift_and_x2_sync_state(ops, call, context, memo,
                                                           active) ==
         X2ReturnStackLiftSyncState::Producer;
}

bool x2_is_backward_duplicate_y_stack_gap_op(const std::vector<IrOp>& ops,
                                            const IrOp& op, int index,
                                            const DirectReturnAnalysisContext& context) {
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op))
    return false;
  if (op.kind == IrKind::Label)
    return !context.label_entries.contains(index);
  if (is_known_return_call_op(op))
    return x2_simple_direct_return_preserves_stack(ops, op, context);
  if (x2_is_known_fallthrough_stack_preserving_conditional(op))
    return true;
  switch (op.kind) {
  case IrKind::Store:
  case IrKind::IndirectStore:
  case IrKind::OrphanAddress:
    return true;
  case IrKind::Plain:
    return analyze_x2_stack_effect(op).stack_preserves;
  default:
    return false;
  }
}

std::optional<int> x2_previous_stack_lift_duplicate_y_stack_producer_index(
    const std::vector<IrOp>& ops, int end, const DirectReturnAnalysisContext& context) {
  for (int index = end - 1; index >= 0; --index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (x2_is_stack_lift_and_x2_sync_producer(op))
      return index;
    if (is_known_return_call_op(op) &&
        x2_known_return_call_reaches_stack_lift_and_x2_sync(ops, op, context))
      return index;
    if (!x2_is_backward_duplicate_y_stack_gap_op(ops, op, index, context))
      return std::nullopt;
  }
  return std::nullopt;
}

std::optional<int> x2_previous_stack_lift_duplicate_y_producer_index(
    const std::vector<IrOp>& ops, int start, int stack_exposure_end,
    const std::optional<X2ValueDataflowState>& state,
    const DirectReturnAnalysisContext& context) {
  if (!x2_state_has_same_visible_x_and_y(state))
    return std::nullopt;
  const std::optional<int> producer =
      x2_previous_stack_lift_duplicate_y_stack_producer_index(ops, start, context);
  if (!producer.has_value())
    return std::nullopt;
  return removing_pre_shift_lift_can_expose_stack(ops, stack_exposure_end) ? std::nullopt
                                                                          : producer;
}

X2ReplacementStackLiftPlan plan_x2_replacement_stack_lift(
    const std::vector<IrOp>& ops, int replacement_start, int stack_exposure_end,
    const std::optional<X2ValueDataflowState>& state, const DirectReturnAnalysisContext& context,
    bool initially_exposes_stack_lift, const X2ReplacementStackLiftOptions& options) {
  const std::optional<int> producer =
      (initially_exposes_stack_lift && options.allow_duplicate_y_stack_proof)
          ? x2_previous_stack_lift_duplicate_y_producer_index(ops, replacement_start,
                                                              stack_exposure_end, state, context)
          : std::nullopt;
  const bool invalidated =
      producer.has_value() && options.invalidated_producer_indexes != nullptr &&
      options.invalidated_producer_indexes->contains(*producer);
  const bool stack_lift_already_supplied = producer.has_value() && !invalidated;
  return X2ReplacementStackLiftPlan{
      .initially_exposes_stack_lift = initially_exposes_stack_lift,
      .stack_lift_producer_index = producer,
      .stack_lift_already_supplied = stack_lift_already_supplied,
      .exposes_stack_lift = initially_exposes_stack_lift && !stack_lift_already_supplied,
  };
}

std::optional<RecallRemovalStackSchedulerPlan>
plan_recall_removal_with_stack_scheduler(const std::vector<IrOp>& ops, int recall_index,
                                         const std::optional<RegisterValueSet>& x2_register_state,
                                         const std::optional<X2ValueDataflowState>& x2_value_state,
                                         const DirectReturnAnalysisContext& context,
                                         const RecallRemovalStackSchedulerOptions& options) {
  const std::optional<RecallRemovalAnalysis> analysis =
      analyze_recall_removal(ops, recall_index, x2_register_state, x2_value_state, &context);
  if (!analysis.has_value())
    return std::nullopt;

  const int stack_scheduler_start = options.stack_scheduler_start.value_or(recall_index);
  const int stack_exposure_end = options.stack_exposure_end.value_or(recall_index);
  const std::optional<X2ValueDataflowState>& stack_scheduler_state =
      options.has_stack_scheduler_state_override ? options.stack_scheduler_state : x2_value_state;
  const X2ReplacementStackLiftPlan stack_lift = plan_x2_replacement_stack_lift(
      ops, stack_scheduler_start, stack_exposure_end, stack_scheduler_state, context,
      analysis->exposes_stack_lift && !analysis->exposes_x2_restore,
      X2ReplacementStackLiftOptions{.invalidated_producer_indexes = options.removed_indexes});

  return RecallRemovalStackSchedulerPlan{
      .analysis = *analysis,
      .stack_lift_producer_index = stack_lift.stack_lift_producer_index,
      .stack_lift_already_supplied = stack_lift.stack_lift_already_supplied,
      .removable = analysis->removable || stack_lift.stack_lift_already_supplied,
  };
}

bool is_known_return_call_op(const IrOp& op) {
  return op.kind == IrKind::Call || op.kind == IrKind::IndirectCall;
}

std::optional<int> direct_call_target_index(const IrOp& call,
                                            const DirectReturnAnalysisContext& context) {
  if (call.kind != IrKind::Call)
    return std::nullopt;
  if (const auto* label = std::get_if<std::string>(&call.target)) {
    const auto found = context.labels.find(*label);
    if (found == context.labels.end())
      return std::nullopt;
    return found->second;
  }
  const auto* address = std::get_if<int>(&call.target);
  if (address == nullptr)
    return std::nullopt;
  const auto found = context.addresses.find(*address);
  if (found == context.addresses.end())
    return std::nullopt;
  return found->second;
}

std::optional<int> known_return_call_target_index(const IrOp& call,
                                                  const DirectReturnAnalysisContext& context) {
  if (call.kind == IrKind::Call)
    return direct_call_target_index(call, context);
  if (call.kind != IrKind::IndirectCall)
    return std::nullopt;
  const std::optional<int> target = known_indirect_flow_target(call);
  if (!target.has_value())
    return std::nullopt;
  const auto found = context.addresses.find(*target);
  if (found == context.addresses.end())
    return std::nullopt;
  return found->second;
}

bool linear_return_range_is_transparent(const std::vector<IrOp>& ops, int target_index,
                                        const std::set<int>& label_entries,
                                        const std::function<bool(const IrOp&)>& is_transparent) {
  const int start_index = ops.at(static_cast<std::size_t>(target_index)).kind == IrKind::Label
                              ? target_index + 1
                              : target_index;
  for (int index = start_index; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label) {
      if (label_entries.contains(index))
        return false;
      continue;
    }
    if (has_rewrite_barrier(op))
      return false;
    if (op.kind == IrKind::Return)
      return true;
    if (!is_transparent(op))
      return false;
  }
  return false;
}

bool direct_call_returns_through_transparent_range(
    const std::vector<IrOp>& ops, const IrOp& call, const DirectReturnAnalysisContext& context,
    const std::function<bool(const IrOp&)>& is_transparent) {
  const std::optional<int> target_index = direct_call_target_index(call, context);
  if (!target_index.has_value())
    return false;
  return linear_return_range_is_transparent(ops, *target_index, context.label_entries,
                                            is_transparent);
}

bool known_return_call_returns_through_transparent_range(
    const std::vector<IrOp>& ops, const IrOp& call, const DirectReturnAnalysisContext& context,
    const std::function<bool(const IrOp&)>& is_transparent) {
  const std::optional<int> target_index = known_return_call_target_index(call, context);
  if (!target_index.has_value())
    return false;
  return linear_return_range_is_transparent(ops, *target_index, context.label_entries,
                                            is_transparent);
}

bool nested_return_call_range_is_transparent(const std::vector<IrOp>& ops, const IrOp& call,
                                             const DirectReturnAnalysisContext& context,
                                             const std::function<bool(const IrOp&)>& is_transparent,
                                             std::map<int, bool>& memo, std::set<int>& active);

bool nested_linear_return_range_is_transparent(
    const std::vector<IrOp>& ops, int target_index, const DirectReturnAnalysisContext& context,
    const std::function<bool(const IrOp&)>& is_transparent, std::map<int, bool>& memo,
    std::set<int>& active) {
  const int start_index = ops.at(static_cast<std::size_t>(target_index)).kind == IrKind::Label
                              ? target_index + 1
                              : target_index;
  for (int index = start_index; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label) {
      if (context.label_entries.contains(index))
        return false;
      continue;
    }
    if (has_rewrite_barrier(op))
      return false;
    if (op.kind == IrKind::Return)
      return true;
    if (is_known_return_call_op(op)) {
      if (is_display_focus_sensitive(op))
        return false;
      if (!nested_return_call_range_is_transparent(ops, op, context, is_transparent, memo,
                                                   active)) {
        return false;
      }
      continue;
    }
    if (!is_transparent(op))
      return false;
  }
  return false;
}

bool nested_return_call_range_is_transparent(const std::vector<IrOp>& ops, const IrOp& call,
                                             const DirectReturnAnalysisContext& context,
                                             const std::function<bool(const IrOp&)>& is_transparent,
                                             std::map<int, bool>& memo, std::set<int>& active) {
  const std::optional<int> target_index = known_return_call_target_index(call, context);
  if (!target_index.has_value())
    return false;
  const auto cached = memo.find(*target_index);
  if (cached != memo.end())
    return cached->second;
  if (active.contains(*target_index))
    return false;

  active.insert(*target_index);
  const bool result = nested_linear_return_range_is_transparent(ops, *target_index, context,
                                                                is_transparent, memo, active);
  active.erase(*target_index);
  memo[*target_index] = result;
  return result;
}

bool known_return_call_returns_through_nested_transparent_range(
    const std::vector<IrOp>& ops, const IrOp& call, const DirectReturnAnalysisContext& context,
    const std::function<bool(const IrOp&)>& is_transparent) {
  std::map<int, bool> memo;
  std::set<int> active;
  return nested_return_call_range_is_transparent(ops, call, context, is_transparent, memo, active);
}

std::vector<std::optional<RegisterValueSet>>
compute_x2_register_states(const std::vector<IrOp>& ops) {
  if (ops.empty())
    return {};

  const std::vector<std::vector<RegisterValueEdge>> edges = build_register_value_graph(ops);
  std::vector<std::optional<RegisterDataflowState>> in_states(ops.size());
  in_states.at(0) = empty_register_dataflow_state();

  bool changed = true;
  int iterations = 0;
  while (changed && iterations < 200) {
    changed = false;
    ++iterations;

    for (std::size_t index = 0; index < ops.size(); ++index) {
      const std::optional<RegisterDataflowState>& input = in_states.at(index);
      if (!input.has_value())
        continue;
      for (const RegisterValueEdge& edge : edges.at(index)) {
        const RegisterDataflowState output =
            transfer_register_dataflow_state(*input, ops.at(index), edge.kind);
        const std::optional<RegisterDataflowState> previous =
            in_states.at(static_cast<std::size_t>(edge.target));
        const RegisterDataflowState joined = join_register_dataflow_states(previous, output);
        if (!same_register_dataflow_state(joined, previous)) {
          in_states.at(static_cast<std::size_t>(edge.target)) = joined;
          changed = true;
        }
      }
    }
  }

  std::vector<std::optional<RegisterValueSet>> result;
  result.reserve(in_states.size());
  for (const std::optional<RegisterDataflowState>& state : in_states) {
    if (!state.has_value()) {
      result.push_back(std::nullopt);
    } else {
      result.push_back(state->x2);
    }
  }
  return result;
}

std::optional<RegisterValueSet>
transfer_x2_register_state_for_edge(const X2RegisterEdgeState& input, const IrOp& op,
                                    X2DataflowEdgeKind edge) {
  if (!input.x2.has_value())
    return std::nullopt;
  if (has_rewrite_barrier(op))
    return RegisterValueSet{};

  switch (op.kind) {
  case IrKind::Label:
  case IrKind::Jump:
  case IrKind::Call:
  case IrKind::OrphanAddress:
    return *input.x2;
  case IrKind::Store:
    if (!input.x.has_value())
      return std::nullopt;
    return add_projected_stored_x2_alias(*input.x2, *input.x, op.register_name);
  case IrKind::IndirectStore: {
    const std::optional<std::string> target = known_indirect_memory_target(op);
    if (!target.has_value())
      return *input.x2;
    if (!input.x.has_value())
      return std::nullopt;
    return add_projected_stored_x2_alias(*input.x2, *input.x, *target);
  }
  case IrKind::Recall:
    return RegisterValueSet{op.register_name};
  case IrKind::IndirectRecall: {
    const std::optional<std::string> target = known_indirect_memory_target(op);
    return target.has_value() ? RegisterValueSet{*target} : RegisterValueSet{};
  }
  case IrKind::Plain:
    return transfer_plain_x2_register_set_for_known_edge(input, op);
  case IrKind::CondJump: {
    const X2Effect effect = conditional_x2_effect_for_graph_edge(op, edge);
    return transfer_conditional_x2_register_set_for_known_edge(input, effect);
  }
  case IrKind::Loop: {
    const X2Effect effect = conditional_x2_effect_for_graph_edge(op, edge);
    const std::string counter = loop_counter_register_name(op.counter);
    const RegisterValueSet x2 = remove_register_value(*input.x2, counter);
    if (effect == X2Effect::Preserves)
      return x2;
    if (effect != X2Effect::Affects)
      return RegisterValueSet{};
    if (!input.x.has_value())
      return std::nullopt;
    return remove_register_value(*input.x, counter);
  }
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
    if (mkpro::core::is_stable_indirect_selector(op.register_name))
      return *input.x2;
    return remove_register_value(*input.x2, op.register_name);
  case IrKind::IndirectCondJump: {
    const X2Effect effect = conditional_x2_effect_for_graph_edge(op, edge);
    std::optional<RegisterValueSet> projected =
        transfer_conditional_x2_register_set_for_known_edge(input, effect);
    if (!projected.has_value())
      return std::nullopt;
    if (edge == X2DataflowEdgeKind::Jump &&
        !mkpro::core::is_stable_indirect_selector(op.register_name)) {
      return remove_register_value(*projected, op.register_name);
    }
    return projected;
  }
  case IrKind::Return:
    if (!input.x.has_value())
      return std::nullopt;
    return *input.x;
  case IrKind::Stop:
    return RegisterValueSet{};
  }
  return RegisterValueSet{};
}

namespace {

constexpr const char* kSameUnknownValue = "same:unknown";
constexpr const char* kRegValuePrefix = "reg:";

std::string internal_x2_value_fact_for_register(const std::string& register_name) {
  return std::string{kRegValuePrefix} + register_name;
}

std::optional<std::set<std::string>> clone_optional_set(
    const std::optional<std::set<std::string>>& input) {
  if (!input.has_value()) {
    return std::nullopt;
  }
  return std::set<std::string>(input->begin(), input->end());
}

X2ValueDataflowState internal_empty_x2_value_dataflow_state(bool track_register_memory) {
  return X2ValueDataflowState{
      .x = X2ValueSet{},
      .y = X2ValueSet{},
      .xShape = X2ShapeSet{},
      .yShape = X2ShapeSet{},
      .x2Shape = X2ShapeSet{},
      .xDirectShape = X2ShapeSet{},
      .yDirectShape = X2ShapeSet{},
      .entry = X2EntryState{.kind = X2EntryState::Kind::Closed},
      .vpContext = X2VpContextState{.kind = X2VpContextState::Kind::None},
      .structuralEntry = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None},
      .structuralVpContext = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None},
      .memory = track_register_memory ? std::optional<X2ValueMemory>{X2ValueMemory{}}
                                      : std::nullopt,
      .shapeMemory = track_register_memory ? std::optional<X2ShapeMemory>{X2ShapeMemory{}}
                                           : std::nullopt,
  };
}

X2ValueDataflowState internal_clone_x2_value_dataflow_state(
    const X2ValueDataflowState& input) {
  return X2ValueDataflowState{
      .x = X2ValueSet{input.x.begin(), input.x.end()},
      .y = clone_optional_set(input.y),
      .x2 = X2ValueSet{input.x2.begin(), input.x2.end()},
      .xShape = X2ShapeSet{input.xShape.begin(), input.xShape.end()},
      .yShape = clone_optional_set(input.yShape),
      .x2Shape = X2ShapeSet{input.x2Shape.begin(), input.x2Shape.end()},
      .xDirectShape = clone_optional_set(input.xDirectShape),
      .yDirectShape = clone_optional_set(input.yDirectShape),
      .entry = input.entry,
      .vpContext = input.vpContext,
      .structuralEntry = input.structuralEntry,
      .structuralVpContext = input.structuralVpContext,
      .vpEntryMantissa = clone_optional_set(input.vpEntryMantissa),
      .vpEntryMantissaTransient = input.vpEntryMantissaTransient,
      .vpEntrySignMantissa = clone_optional_set(input.vpEntrySignMantissa),
      .vpEntryShape = clone_optional_set(input.vpEntryShape),
      .vpEntrySignShape = clone_optional_set(input.vpEntrySignShape),
      .vpEntryShapeTransient = input.vpEntryShapeTransient,
      .memory = input.memory,
      .shapeMemory = input.shapeMemory,
  };
}

X2ValueSet internal_recall_x2_values(const X2ValueDataflowState& input,
                                     const std::string& register_name,
                                     bool track_register_memory, const IrOp* op) {
  X2ValueSet output;
  if (track_register_memory && input.memory.has_value()) {
    const auto found = input.memory->find(register_name);
    if (found != input.memory->end() && !found->second.empty()) {
      output.insert(found->second.begin(), found->second.end());
    }
  }
  const X2ValueSet preloaded = preloaded_constant_value_facts(op);
  output.insert(preloaded.begin(), preloaded.end());
  output.insert(internal_x2_value_fact_for_register(register_name));
  return output;
}

X2ShapeSet internal_x2_shapes_from_value_facts(const X2ValueSet& values) {
  X2ShapeSet output;
  for (const X2ValueFact& value : values) {
    const std::optional<std::string> decimal = x2_value_fact_restored_visible_decimal(value);
    if (!decimal.has_value())
      continue;
    const std::optional<X2ShapeFact> shape = exact_decimal_display_shape_fact(*decimal);
    if (shape.has_value())
      output.insert(*shape);
  }
  return output;
}

X2ShapeSet internal_recall_x2_shape_facts(const X2ValueSet& values, const IrOp* op,
                                          const std::optional<X2ShapeSet>& memory_shapes) {
  X2ShapeSet output = internal_x2_shapes_from_value_facts(values);
  if (memory_shapes.has_value())
    output.insert(memory_shapes->begin(), memory_shapes->end());
  const X2ShapeSet preloaded = preloaded_constant_shape_facts(op);
  output.insert(preloaded.begin(), preloaded.end());
  return output;
}

X2ShapeSet internal_recall_direct_shape_facts(const IrOp* op,
                                              const std::optional<X2ShapeSet>& memory_shapes) {
  X2ShapeSet output = preloaded_constant_shape_facts(op);
  if (memory_shapes.has_value())
    output.insert(memory_shapes->begin(), memory_shapes->end());
  return output;
}

std::optional<RegisterValueSet> internal_vp_entry_mantissas_from_value_facts(
    const X2ValueSet& values) {
  RegisterValueSet output;
  constexpr std::string_view prefix = "decimal:";
  constexpr std::string_view suffix = ":normalized";
  for (const X2ValueFact& value : values) {
    if (!value.starts_with(prefix) || !value.ends_with(suffix))
      continue;
    const std::optional<std::string> decimal =
        normalize_plain_decimal(value.substr(prefix.size(), value.size() - prefix.size() -
                                                                suffix.size()));
    if (decimal.has_value())
      output.insert(*decimal);
  }
  return output.empty() ? std::nullopt : std::optional<RegisterValueSet>{output};
}

X2ValueSet internal_remove_x2_value(const X2ValueSet& input, const X2ValueFact& fact) {
  X2ValueSet output = input;
  output.erase(fact);
  return output;
}

bool internal_x2_value_fact_depends_on_register(const X2ValueFact& fact,
                                                const std::string& register_name) {
  const X2ValueFact register_fact = internal_x2_value_fact_for_register(register_name);
  if (fact == register_fact)
    return true;
  if (!fact.starts_with("expr-key:"))
    return false;
  return fact.find(register_fact) != std::string::npos;
}

X2ValueSet internal_remove_register_dependent_value_facts(const X2ValueSet& input,
                                                          const std::string& register_name) {
  X2ValueSet output;
  for (const X2ValueFact& fact : input) {
    if (!internal_x2_value_fact_depends_on_register(fact, register_name))
      output.insert(fact);
  }
  return output;
}

X2ValueMemory internal_remove_register_dependent_value_memory(
    const X2ValueMemory& input, const std::string& register_name) {
  X2ValueMemory output;
  for (const auto& [memory_register, values] : input) {
    X2ValueSet kept =
        internal_remove_register_dependent_value_facts(values, register_name);
    if (!kept.empty())
      output[memory_register] = std::move(kept);
  }
  return output;
}

bool internal_register_write_preserves_stored_value(const X2ValueDataflowState& input,
                                                    const std::string& register_name) {
  return input.x.contains(internal_x2_value_fact_for_register(register_name));
}

X2ValueSet internal_add_stored_x2_value_alias(const X2ValueDataflowState& input,
                                              const X2ValueFact& fact) {
  X2ValueSet output = input.x2;
  output.erase(fact);
  if (x2_value_sets_intersect(input.x, input.x2))
    output.insert(fact);
  return output;
}

namespace x2eval {
X2ValueSet storable_x2_memory_value_facts(const X2ValueSet& values);
}  // namespace x2eval

void internal_store_x2_value_memory(X2ValueDataflowState& output,
                                    const std::string& register_name,
                                    const X2ValueSet& values) {
  if (!output.memory.has_value())
    output.memory = X2ValueMemory{};
  // Faithful port of storeX2ValueMemory: only storable facts (concrete decimals,
  // expressions, restored decimals) are persisted; opaque aliases (reg:*) are
  // dropped, and an empty result deletes the slot.
  const X2ValueSet stored = x2eval::storable_x2_memory_value_facts(values);
  if (stored.empty()) {
    output.memory->erase(register_name);
  } else {
    (*output.memory)[register_name] = stored;
  }
}

void internal_store_x2_shape_memory(X2ValueDataflowState& output,
                                    const std::string& register_name,
                                    const X2ValueSet& values,
                                    const X2ShapeSet& shapes) {
  if (!output.shapeMemory.has_value())
    output.shapeMemory = X2ShapeMemory{};
  X2ShapeSet stored_shapes = internal_x2_shapes_from_value_facts(values);
  stored_shapes.insert(shapes.begin(), shapes.end());
  if (stored_shapes.empty()) {
    output.shapeMemory->erase(register_name);
  } else {
    (*output.shapeMemory)[register_name] = stored_shapes;
  }
}

namespace x2eval {
X2ValueDataflowState close_x2_value_entry(const X2ValueDataflowState& input);
}  // namespace x2eval

X2ValueDataflowState internal_close_x2_value_entry(const X2ValueDataflowState& input) {
  // Faithful port of closeX2ValueEntry: an open decimal/exponent or structural
  // exponent entry must be committed into X/X2 (and their shapes) before any
  // store/recall/control-flow op observes the value. Delegating to the shared
  // x2eval implementation keeps this byte-exact with the TypeScript oracle.
  return x2eval::close_x2_value_entry(input);
}

X2ValueDataflowState internal_invalidate_register_dependency(
    const X2ValueDataflowState& input, const std::string& register_name,
    bool track_register_memory) {
  X2ValueDataflowState output = internal_clone_x2_value_dataflow_state(input);
  output.x = internal_remove_register_dependent_value_facts(output.x, register_name);
  output.x2 = internal_remove_register_dependent_value_facts(output.x2, register_name);
  if (output.y.has_value()) {
    output.y = internal_remove_register_dependent_value_facts(*output.y, register_name);
  }
  if (track_register_memory && output.memory.has_value()) {
    output.memory =
        internal_remove_register_dependent_value_memory(*output.memory, register_name);
  }
  if (track_register_memory && output.shapeMemory.has_value()) {
    output.shapeMemory->erase(register_name);
  }
  return output;
}

namespace x2eval {
X2ValueDataflowState transfer_plain_x2_value_state(const X2ValueDataflowState& input, const IrOp& op,
                                                   std::optional<int> producer_index);
std::optional<X2ShapeSet> vp_entry_shapes_from_shape_facts(const X2ShapeSet* shapes);
std::optional<X2ShapeSet> vp_entry_sign_shapes_from_shape_facts(const X2ShapeSet* shapes);
std::optional<X2ShapeSet> vp_splice_shape_set_with_value_shapes(const X2ShapeSet* shapes,
                                                                const X2ValueSet* values);
std::optional<RegisterValueSet> vp_entry_mantissas_from_store_splice(const X2ShapeSet* shapes);
std::optional<RegisterValueSet> vp_entry_sign_mantissas_from_store_splice(const X2ShapeSet* shapes);
std::optional<X2ShapeSet> vp_entry_sign_shapes_from_store_splice(const X2ShapeSet* shapes);
std::optional<X2ShapeSet> vp_entry_shapes_from_store_splice(const X2ShapeSet* shapes);
X2ValueDataflowState with_store_vp_splice_source(const X2ValueDataflowState& input);
X2ValueDataflowState with_direct_flow_vp_splice_source(const X2ValueDataflowState& input);
X2ValueDataflowState with_indirect_flow_vp_splice_source(const X2ValueDataflowState& input);
X2ValueSet transfer_conditional_x2_value_set(const X2ValueDataflowState& input, const X2ValueSet& x,
                                             const X2ShapeSet* x_shape, X2Effect effect);
X2ShapeSet transfer_conditional_x2_shape_set(const X2ValueDataflowState& input, const X2ValueSet* x,
                                             const X2ShapeSet& x_shape, X2Effect effect);
X2VpContextState transfer_conditional_x2_vp_context_state(const X2ValueDataflowState& input,
                                                          X2Effect effect);
X2StructuralEntryState transfer_conditional_x2_structural_vp_context_state(
    const X2ValueDataflowState& input, X2Effect effect);
std::optional<RegisterValueSet> transfer_conditional_x2_vp_entry_mantissa_state(
    const X2ValueSet& x, const X2ShapeSet& x_shape, const X2ShapeSet& x2_shape, X2Effect effect);
std::optional<RegisterValueSet> transfer_conditional_x2_vp_entry_sign_mantissa_state(
    const X2ValueDataflowState& input, const X2ValueSet& x, const X2ShapeSet& x_shape,
    const X2ShapeSet& x2_shape, X2Effect effect);
std::optional<X2ShapeSet> transfer_conditional_x2_vp_entry_sign_shape_state(
    const X2ValueDataflowState& input, const X2ValueSet& x, const X2ShapeSet& x_shape,
    const X2ShapeSet& x2_shape, X2Effect effect);
std::optional<X2ShapeSet> transfer_conditional_x2_vp_entry_shape_state(const X2ValueSet& x,
                                                                       const X2ShapeSet& x_shape,
                                                                       const X2ShapeSet& x2_shape,
                                                                       X2Effect effect);
X2ValueSet x2_sync_value_set_from_visible_x(const X2ValueSet& values, const X2ShapeSet* shapes);
std::set<X2ShapeFact> x2_sync_shape_set_from_visible_x(const X2ShapeSet* input,
                                                       const X2ValueSet* values);
X2ValueSet sync_unknown_same_value(X2ValueSet x, X2Effect effect, std::optional<int> producer_index);
X2ValueSet canonical_x2_value_set(const X2ValueSet* input);
X2ValueSet clone_optional_value_set(const X2ValueSet* input);
X2ShapeSet clone_optional_shape_set(const X2ShapeSet* input);
std::optional<X2ValueMemory> clone_x2_value_memory_field(const std::optional<X2ValueMemory>& memory);
std::optional<X2ShapeMemory> clone_x2_shape_memory_field(const std::optional<X2ShapeMemory>& memory);
X2VpContextState clone_x2_vp_context_state(const X2VpContextState& input);
X2StructuralEntryState clone_x2_structural_entry_state(const X2StructuralEntryState& input);
}  // namespace x2eval

// dropMutatedSelectorX2ValueFact: invalidate the selector register's value
// facts and drop its whole memory slot (used by the indirect-flow edges).
X2ValueDataflowState internal_drop_mutated_selector_x2_value_fact(
    const X2ValueDataflowState& input, const std::string& register_name,
    bool track_register_memory) {
  X2ValueDataflowState stable =
      internal_invalidate_register_dependency(input, register_name, track_register_memory);
  if (!track_register_memory) {
    stable.memory = std::nullopt;
    stable.shapeMemory = std::nullopt;
    return stable;
  }
  if (stable.memory.has_value())
    stable.memory->erase(register_name);
  if (stable.shapeMemory.has_value())
    stable.shapeMemory->erase(register_name);
  return stable;
}

X2ValueDataflowState internal_transfer_x2_value_dataflow_state(
    const X2ValueDataflowState& input, const IrOp& op, X2DataflowEdgeKind edge,
    bool track_register_memory, std::optional<int> producer_index, bool target_starts_with_vp) {
  if (has_rewrite_barrier(op)) {
    return internal_empty_x2_value_dataflow_state(track_register_memory);
  }

  [[maybe_unused]] const auto vptr = [](const std::optional<X2ValueSet>& o) -> const X2ValueSet* {
    return o.has_value() ? &*o : nullptr;
  };
  [[maybe_unused]] const auto sptr = [](const std::optional<X2ShapeSet>& o) -> const X2ShapeSet* {
    return o.has_value() ? &*o : nullptr;
  };

  switch (op.kind) {
  case IrKind::Label:
  case IrKind::OrphanAddress:
    return internal_clone_x2_value_dataflow_state(input);
  case IrKind::Jump:
  case IrKind::Call: {
    const X2ValueDataflowState closed = internal_close_x2_value_entry(input);
    return target_starts_with_vp ? x2eval::with_direct_flow_vp_splice_source(closed) : closed;
  }
  case IrKind::Store: {
    const X2ValueDataflowState closed = internal_close_x2_value_entry(input);
    const X2ValueDataflowState stable =
        internal_register_write_preserves_stored_value(closed, op.register_name)
            ? closed
            : internal_invalidate_register_dependency(closed, op.register_name,
                                                      track_register_memory);
    X2ValueDataflowState output = internal_clone_x2_value_dataflow_state(stable);
    const X2ValueFact fact = internal_x2_value_fact_for_register(op.register_name);
    output.x.insert(fact);
    output.x2 = internal_add_stored_x2_value_alias(stable, fact);
    output.y = clone_optional_set(stable.y);
    output.yShape = clone_optional_set(stable.yShape);
    output.xShape = X2ShapeSet{stable.xShape.begin(), stable.xShape.end()};
    output.x2Shape = X2ShapeSet{stable.x2Shape.begin(), stable.x2Shape.end()};
    output.xDirectShape = clone_optional_set(stable.xDirectShape);
    output.yDirectShape = clone_optional_set(stable.yDirectShape);
    output.vpContext = x2eval::clone_x2_vp_context_state(stable.vpContext);
    output.structuralEntry = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None};
    output.structuralVpContext = x2eval::clone_x2_structural_entry_state(stable.structuralVpContext);
    const std::optional<X2ShapeSet> store_splice =
        x2eval::vp_splice_shape_set_with_value_shapes(&stable.x2Shape, &stable.x2);
    const X2ShapeSet* store_splice_ptr = store_splice.has_value() ? &*store_splice : nullptr;
    const std::optional<X2ShapeSet> store_vp_entry_shape =
        x2eval::vp_entry_shapes_from_store_splice(store_splice_ptr);
    output.vpEntryMantissa = x2eval::vp_entry_mantissas_from_store_splice(store_splice_ptr);
    output.vpEntryMantissaTransient = false;
    output.vpEntrySignMantissa = x2eval::vp_entry_sign_mantissas_from_store_splice(store_splice_ptr);
    output.vpEntryShape = store_vp_entry_shape;
    output.vpEntrySignShape = x2eval::vp_entry_sign_shapes_from_store_splice(store_splice_ptr);
    output.vpEntryShapeTransient = store_vp_entry_shape.has_value();
    if (track_register_memory) {
      internal_store_x2_value_memory(output, op.register_name, stable.x);
      internal_store_x2_shape_memory(output, op.register_name, stable.x, stable.xShape);
    }
    return output;
  }
  case IrKind::IndirectStore: {
    const bool stable_selector = mkpro::core::is_stable_indirect_selector(op.register_name);
    const std::optional<std::string> target = known_indirect_memory_target(op);
    if (!target.has_value()) {
      X2ValueDataflowState cleared =
          x2eval::with_store_vp_splice_source(internal_close_x2_value_entry(input));
      if (track_register_memory) {
        cleared.memory = X2ValueMemory{};
        cleared.shapeMemory = X2ShapeMemory{};
      }
      return stable_selector
                 ? cleared
                 : internal_drop_mutated_selector_x2_value_fact(cleared, op.register_name,
                                                                track_register_memory);
    }
    const X2ValueDataflowState closed = internal_close_x2_value_entry(input);
    const X2ValueDataflowState stable =
        internal_register_write_preserves_stored_value(closed, *target)
            ? closed
            : internal_invalidate_register_dependency(closed, *target, track_register_memory);
    X2ValueDataflowState output = internal_clone_x2_value_dataflow_state(stable);
    const X2ValueFact fact = internal_x2_value_fact_for_register(*target);
    output.x.insert(fact);
    output.x2 = internal_add_stored_x2_value_alias(stable, fact);
    output.y = clone_optional_set(stable.y);
    output.yShape = clone_optional_set(stable.yShape);
    output.xShape = X2ShapeSet{stable.xShape.begin(), stable.xShape.end()};
    output.x2Shape = X2ShapeSet{stable.x2Shape.begin(), stable.x2Shape.end()};
    output.xDirectShape = clone_optional_set(stable.xDirectShape);
    output.yDirectShape = clone_optional_set(stable.yDirectShape);
    output.vpContext = x2eval::clone_x2_vp_context_state(stable.vpContext);
    output.structuralEntry = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None};
    output.structuralVpContext = x2eval::clone_x2_structural_entry_state(stable.structuralVpContext);
    const std::optional<X2ShapeSet> store_splice =
        x2eval::vp_splice_shape_set_with_value_shapes(&stable.x2Shape, &stable.x2);
    const X2ShapeSet* store_splice_ptr = store_splice.has_value() ? &*store_splice : nullptr;
    const std::optional<X2ShapeSet> store_vp_entry_shape =
        x2eval::vp_entry_shapes_from_store_splice(store_splice_ptr);
    output.vpEntryMantissa = x2eval::vp_entry_mantissas_from_store_splice(store_splice_ptr);
    output.vpEntryMantissaTransient = false;
    output.vpEntrySignMantissa = x2eval::vp_entry_sign_mantissas_from_store_splice(store_splice_ptr);
    output.vpEntryShape = store_vp_entry_shape;
    output.vpEntrySignShape = x2eval::vp_entry_sign_shapes_from_store_splice(store_splice_ptr);
    output.vpEntryShapeTransient = store_vp_entry_shape.has_value();
    if (track_register_memory) {
      internal_store_x2_value_memory(output, *target, stable.x);
      internal_store_x2_shape_memory(output, *target, stable.x, stable.xShape);
    }
    return stable_selector
               ? output
               : internal_drop_mutated_selector_x2_value_fact(output, op.register_name,
                                                              track_register_memory);
  }
  case IrKind::Recall: {
    const X2ValueSet values =
        internal_recall_x2_values(input, op.register_name, track_register_memory, &op);
    const std::optional<X2ShapeSet> memory_shape =
        track_register_memory && input.shapeMemory.has_value()
            ? [&]() -> std::optional<X2ShapeSet> {
                const auto found = input.shapeMemory->find(op.register_name);
                return found == input.shapeMemory->end() ? std::nullopt
                                                         : std::optional<X2ShapeSet>{found->second};
              }()
            : std::nullopt;
    const X2ShapeSet shape = internal_recall_x2_shape_facts(values, &op, memory_shape);
    const X2ShapeSet direct_shape = internal_recall_direct_shape_facts(&op, memory_shape);
    X2ValueDataflowState output = internal_close_x2_value_entry(input);
    output.x = values;
    output.x2 = values;
    output.y = X2ValueSet{input.x.begin(), input.x.end()};
    output.yShape = X2ShapeSet{input.xShape.begin(), input.xShape.end()};
    output.xShape = shape;
    output.x2Shape = shape;
    output.xDirectShape = direct_shape;
    output.yDirectShape = input.xDirectShape;
    output.vpContext = X2VpContextState{.kind = X2VpContextState::Kind::None};
    output.structuralEntry = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None};
    output.structuralVpContext = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None};
    output.vpEntryMantissa = internal_vp_entry_mantissas_from_value_facts(values);
    output.vpEntrySignMantissa = std::nullopt;
    output.vpEntryShape = x2eval::vp_entry_shapes_from_shape_facts(&shape);
    output.vpEntrySignShape = x2eval::vp_entry_sign_shapes_from_shape_facts(&shape);
    output.vpEntryMantissaTransient = false;
    output.vpEntryShapeTransient = false;
    return output;
  }
  case IrKind::IndirectRecall: {
    const std::optional<std::string> target = known_indirect_memory_target(op);
    const std::optional<X2ShapeSet> memory_shape =
        target.has_value() && track_register_memory && input.shapeMemory.has_value()
            ? [&]() -> std::optional<X2ShapeSet> {
                const auto found = input.shapeMemory->find(*target);
                return found == input.shapeMemory->end() ? std::nullopt
                                                         : std::optional<X2ShapeSet>{found->second};
              }()
            : std::nullopt;
    const X2ValueSet values =
        target.has_value()
            ? internal_recall_x2_values(input, *target, track_register_memory, &op)
            : X2ValueSet{kSameUnknownValue};
    const X2ShapeSet shape = internal_recall_x2_shape_facts(values, &op, memory_shape);
    const X2ShapeSet direct_shape =
        target.has_value() ? internal_recall_direct_shape_facts(&op, memory_shape) : X2ShapeSet{};
    X2ValueDataflowState output = internal_close_x2_value_entry(input);
    output.x = values;
    output.x2 = values;
    output.y = X2ValueSet{input.x.begin(), input.x.end()};
    output.yShape = X2ShapeSet{input.xShape.begin(), input.xShape.end()};
    output.xShape = shape;
    output.x2Shape = shape;
    output.xDirectShape = direct_shape;
    output.yDirectShape = input.xDirectShape;
    output.vpContext = X2VpContextState{.kind = X2VpContextState::Kind::None};
    output.structuralEntry = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None};
    output.structuralVpContext = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None};
    output.vpEntryMantissa = internal_vp_entry_mantissas_from_value_facts(values);
    output.vpEntrySignMantissa = std::nullopt;
    output.vpEntryShape = x2eval::vp_entry_shapes_from_shape_facts(&shape);
    output.vpEntrySignShape = x2eval::vp_entry_sign_shapes_from_shape_facts(&shape);
    output.vpEntryMantissaTransient = false;
    output.vpEntryShapeTransient = false;
    return output;
  }
  case IrKind::Plain: {
    return x2eval::transfer_plain_x2_value_state(input, op, producer_index);
  }
  case IrKind::CondJump: {
    const X2ValueDataflowState closed = internal_close_x2_value_entry(input);
    const X2Effect effect = conditional_x2_effect_for_graph_edge(op, edge);
    const X2ValueSet x = x2eval::sync_unknown_same_value(
        x2eval::canonical_x2_value_set(&closed.x), effect, producer_index);
    const X2ShapeSet x_shape = x2eval::clone_optional_shape_set(&closed.xShape);
    const X2ShapeSet x2_shape = x2eval::transfer_conditional_x2_shape_set(closed, &x, x_shape, effect);
    X2ValueDataflowState output{
        .x = x,
        .y = x2eval::clone_optional_value_set(vptr(closed.y)),
        .x2 = x2eval::transfer_conditional_x2_value_set(closed, x, &x_shape, effect),
        .xShape = x_shape,
        .yShape = x2eval::clone_optional_shape_set(sptr(closed.yShape)),
        .x2Shape = x2_shape,
        .xDirectShape = x2eval::clone_optional_shape_set(sptr(closed.xDirectShape)),
        .yDirectShape = x2eval::clone_optional_shape_set(sptr(closed.yDirectShape)),
        .entry = X2EntryState{.kind = X2EntryState::Kind::Closed},
        .vpContext = x2eval::transfer_conditional_x2_vp_context_state(closed, effect),
        .structuralEntry = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None},
        .structuralVpContext = x2eval::transfer_conditional_x2_structural_vp_context_state(closed, effect),
        .vpEntryMantissa = x2eval::transfer_conditional_x2_vp_entry_mantissa_state(x, x_shape, x2_shape, effect),
        .vpEntryMantissaTransient = false,
        .vpEntrySignMantissa = x2eval::transfer_conditional_x2_vp_entry_sign_mantissa_state(closed, x, x_shape, x2_shape, effect),
        .vpEntryShape = x2eval::transfer_conditional_x2_vp_entry_shape_state(x, x_shape, x2_shape, effect),
        .vpEntrySignShape = x2eval::transfer_conditional_x2_vp_entry_sign_shape_state(closed, x, x_shape, x2_shape, effect),
        .vpEntryShapeTransient = false,
        .memory = x2eval::clone_x2_value_memory_field(closed.memory),
        .shapeMemory = x2eval::clone_x2_shape_memory_field(closed.shapeMemory),
    };
    return (edge == X2DataflowEdgeKind::Jump && effect == X2Effect::Preserves && target_starts_with_vp)
               ? x2eval::with_direct_flow_vp_splice_source(output)
               : output;
  }
  case IrKind::Loop: {
    const X2ValueDataflowState closed = internal_close_x2_value_entry(input);
    const X2Effect effect = conditional_x2_effect_for_graph_edge(op, edge);
    const std::string counter = loop_counter_register_name(op.counter);
    const X2ValueDataflowState stable =
        internal_invalidate_register_dependency(closed, counter, track_register_memory);
    const X2ValueFact counter_fact = internal_x2_value_fact_for_register(counter);
    const X2ValueSet x = x2eval::sync_unknown_same_value(
        internal_remove_x2_value(stable.x, counter_fact), effect, producer_index);
    const X2ShapeSet x_shape = x2eval::clone_optional_shape_set(&stable.xShape);
    X2ValueSet x2;
    if (effect == X2Effect::Preserves)
      x2 = internal_remove_x2_value(stable.x2, counter_fact);
    else if (effect == X2Effect::Affects)
      x2 = x2eval::x2_sync_value_set_from_visible_x(x, &x_shape);
    else
      x2 = X2ValueSet{};
    const X2ShapeSet x2_shape = x2eval::transfer_conditional_x2_shape_set(stable, &x, x_shape, effect);
    std::optional<X2ValueMemory> memory;
    std::optional<X2ShapeMemory> shape_memory;
    if (track_register_memory) {
      memory = x2eval::clone_x2_value_memory_field(stable.memory);
      if (memory.has_value())
        memory->erase(counter);
      shape_memory = x2eval::clone_x2_shape_memory_field(stable.shapeMemory);
      if (shape_memory.has_value())
        shape_memory->erase(counter);
    }
    X2ValueDataflowState output{
        .x = x,
        .y = x2eval::clone_optional_value_set(vptr(stable.y)),
        .x2 = x2,
        .xShape = x_shape,
        .yShape = x2eval::clone_optional_shape_set(sptr(stable.yShape)),
        .x2Shape = x2_shape,
        .xDirectShape = x2eval::clone_optional_shape_set(sptr(stable.xDirectShape)),
        .yDirectShape = x2eval::clone_optional_shape_set(sptr(stable.yDirectShape)),
        .entry = X2EntryState{.kind = X2EntryState::Kind::Closed},
        .vpContext = x2eval::transfer_conditional_x2_vp_context_state(stable, effect),
        .structuralEntry = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None},
        .structuralVpContext = x2eval::transfer_conditional_x2_structural_vp_context_state(stable, effect),
        .vpEntryMantissa = x2eval::transfer_conditional_x2_vp_entry_mantissa_state(x, x_shape, x2_shape, effect),
        .vpEntryMantissaTransient = false,
        .vpEntrySignMantissa = x2eval::transfer_conditional_x2_vp_entry_sign_mantissa_state(stable, x, x_shape, x2_shape, effect),
        .vpEntryShape = x2eval::transfer_conditional_x2_vp_entry_shape_state(x, x_shape, x2_shape, effect),
        .vpEntrySignShape = x2eval::transfer_conditional_x2_vp_entry_sign_shape_state(stable, x, x_shape, x2_shape, effect),
        .vpEntryShapeTransient = false,
        .memory = memory,
        .shapeMemory = shape_memory,
    };
    return (edge == X2DataflowEdgeKind::Jump && effect == X2Effect::Preserves && target_starts_with_vp)
               ? x2eval::with_indirect_flow_vp_splice_source(output)
               : output;
  }
  case IrKind::IndirectJump:
  case IrKind::IndirectCall: {
    const X2ValueDataflowState closed = internal_close_x2_value_entry(input);
    const X2ValueDataflowState stable =
        mkpro::core::is_stable_indirect_selector(op.register_name)
            ? closed
            : internal_drop_mutated_selector_x2_value_fact(closed, op.register_name,
                                                           track_register_memory);
    return target_starts_with_vp ? x2eval::with_indirect_flow_vp_splice_source(stable) : stable;
  }
  case IrKind::IndirectCondJump: {
    const X2Effect effect = conditional_x2_effect_for_graph_edge(op, edge);
    if (effect == X2Effect::Unknown)
      return internal_empty_x2_value_dataflow_state(track_register_memory);
    const X2ValueDataflowState closed = internal_close_x2_value_entry(input);
    const X2ValueSet x = x2eval::sync_unknown_same_value(
        x2eval::canonical_x2_value_set(&closed.x), effect, std::nullopt);
    const X2ShapeSet x_shape = x2eval::clone_optional_shape_set(&closed.xShape);
    const X2ShapeSet x2_shape = x2eval::transfer_conditional_x2_shape_set(closed, &x, x_shape, effect);
    X2ValueDataflowState output{
        .x = x,
        .y = x2eval::clone_optional_value_set(vptr(closed.y)),
        .x2 = x2eval::transfer_conditional_x2_value_set(closed, x, &x_shape, effect),
        .xShape = x_shape,
        .yShape = x2eval::clone_optional_shape_set(sptr(closed.yShape)),
        .x2Shape = x2_shape,
        .xDirectShape = x2eval::clone_optional_shape_set(sptr(closed.xDirectShape)),
        .yDirectShape = x2eval::clone_optional_shape_set(sptr(closed.yDirectShape)),
        .entry = X2EntryState{.kind = X2EntryState::Kind::Closed},
        .vpContext = x2eval::transfer_conditional_x2_vp_context_state(closed, effect),
        .structuralEntry = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None},
        .structuralVpContext = x2eval::transfer_conditional_x2_structural_vp_context_state(closed, effect),
        .vpEntryMantissa = x2eval::transfer_conditional_x2_vp_entry_mantissa_state(x, x_shape, x2_shape, effect),
        .vpEntryMantissaTransient = false,
        .vpEntrySignMantissa = x2eval::transfer_conditional_x2_vp_entry_sign_mantissa_state(closed, x, x_shape, x2_shape, effect),
        .vpEntryShape = x2eval::transfer_conditional_x2_vp_entry_shape_state(x, x_shape, x2_shape, effect),
        .vpEntrySignShape = x2eval::transfer_conditional_x2_vp_entry_sign_shape_state(closed, x, x_shape, x2_shape, effect),
        .vpEntryShapeTransient = false,
        .memory = x2eval::clone_x2_value_memory_field(closed.memory),
        .shapeMemory = x2eval::clone_x2_shape_memory_field(closed.shapeMemory),
    };
    if (edge != X2DataflowEdgeKind::Jump)
      return output;
    const X2ValueDataflowState stable =
        mkpro::core::is_stable_indirect_selector(op.register_name)
            ? output
            : internal_drop_mutated_selector_x2_value_fact(output, op.register_name,
                                                           track_register_memory);
    return target_starts_with_vp ? x2eval::with_indirect_flow_vp_splice_source(stable) : stable;
  }
  case IrKind::Stop:
    return internal_empty_x2_value_dataflow_state(track_register_memory);
  case IrKind::Return: {
    const X2ValueDataflowState closed = internal_close_x2_value_entry(input);
    const X2ValueSet x = x2eval::sync_unknown_same_value(
        x2eval::canonical_x2_value_set(&closed.x), X2Effect::Affects, producer_index);
    const X2ShapeSet x_shape = x2eval::clone_optional_shape_set(&closed.xShape);
    const std::set<X2ShapeFact> x2_shape_set = x2eval::x2_sync_shape_set_from_visible_x(&x_shape, &x);
    const X2ShapeSet x2_shape{x2_shape_set.begin(), x2_shape_set.end()};
    return X2ValueDataflowState{
        .x = x,
        .y = x2eval::clone_optional_value_set(vptr(closed.y)),
        .x2 = x2eval::x2_sync_value_set_from_visible_x(x, &x_shape),
        .xShape = x_shape,
        .yShape = x2eval::clone_optional_shape_set(sptr(closed.yShape)),
        .x2Shape = x2_shape,
        .xDirectShape = x2eval::clone_optional_shape_set(sptr(closed.xDirectShape)),
        .yDirectShape = x2eval::clone_optional_shape_set(sptr(closed.yDirectShape)),
        .entry = X2EntryState{.kind = X2EntryState::Kind::Closed},
        .vpContext = X2VpContextState{.kind = X2VpContextState::Kind::None},
        .structuralEntry = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None},
        .structuralVpContext = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None},
        .vpEntryMantissa = x2eval::transfer_conditional_x2_vp_entry_mantissa_state(x, x_shape, x2_shape, X2Effect::Affects),
        .vpEntryMantissaTransient = false,
        .vpEntrySignMantissa = x2eval::transfer_conditional_x2_vp_entry_sign_mantissa_state(closed, x, x_shape, x2_shape, X2Effect::Affects),
        .vpEntryShape = x2eval::transfer_conditional_x2_vp_entry_shape_state(x, x_shape, x2_shape, X2Effect::Affects),
        .vpEntrySignShape = x2eval::transfer_conditional_x2_vp_entry_sign_shape_state(closed, x, x_shape, x2_shape, X2Effect::Affects),
        .vpEntryShapeTransient = false,
        .memory = x2eval::clone_x2_value_memory_field(closed.memory),
        .shapeMemory = x2eval::clone_x2_shape_memory_field(closed.shapeMemory),
    };
  }
  }
  return internal_empty_x2_value_dataflow_state(track_register_memory);
}

// ===========================================================================
// Session A: concrete-evaluation + stable-expression-key engine (faithful port
// of the TS plainProducesConcrete* / stableExpressionKey* / restored-visible
// cluster). Everything lives in the `x2eval` namespace and is currently DEAD
// CODE (not wired into the Plain branch); it is validated in isolation against
// the TS oracle. Stage 1 arithmetic and Stage 2 shape-model helpers from the
// enclosing namespace are reused directly (no duplication).
// ===========================================================================
namespace x2eval {

std::set<X2ShapeFact> canonical_shape_set(const X2ShapeSet* input) {
  std::set<X2ShapeFact> output;
  if (input == nullptr)
    return output;
  for (const X2ShapeFact& fact : *input) {
    const std::optional<X2ShapeFact> canonical = x2_canonical_shape_fact_if_valid(fact);
    if (canonical.has_value())
      output.insert(*canonical);
  }
  return output;
}

std::vector<X2ShapeDataModel> x2_shape_data_models(const X2ShapeSet* input) {
  std::vector<X2ShapeDataModel> models;
  for (const X2ShapeFact& fact : canonical_shape_set(input))
    models.push_back(x2_shape_data_model_for_fact(fact));
  return models;
}

std::optional<X2ShapeFact> decimal_fraction_part_shape_fact(const std::string& value) {
  static const std::regex pattern(R"(^(-?)([0-9]+)(?:\.([0-9]+))?$)");
  std::smatch match;
  if (!std::regex_match(value, match, pattern))
    return std::nullopt;
  const std::string sign = match[1].str();
  std::string fraction = match[3].matched ? match[3].str() : std::string{};
  const std::size_t last_non_zero = fraction.find_last_not_of('0');
  fraction = last_non_zero == std::string::npos ? std::string{} : fraction.substr(0, last_non_zero + 1);
  if (fraction.empty())
    return decimal_mantissa_shape_fact(sign == "-" ? "-0" : "0");
  const std::size_t first_non_zero = fraction.find_first_not_of('0');
  const std::size_t leading_zeroes = first_non_zero == std::string::npos ? fraction.size() : first_non_zero;
  const std::string significant = fraction.substr(leading_zeroes);
  if (significant.empty())
    return decimal_mantissa_shape_fact(sign == "-" ? "-0" : "0");
  const std::string mantissa = significant.size() == 1
                                   ? sign + significant
                                   : sign + significant.substr(0, 1) + "." + significant.substr(1);
  return decimal_exponent_shape_fact(mantissa, "-" + std::to_string(leading_zeroes + 1));
}

std::set<X2ShapeFact> decimal_display_shape_facts(const X2ShapeSet* input) {
  std::set<X2ShapeFact> output;
  for (const X2ShapeDataModel& model : x2_shape_data_models(input)) {
    if (model.kind == X2ShapeModelKind::Mantissa && model.mantissa->radix == X2MantissaRadix::Decimal) {
      if (!model.mantissa->normalizedDecimal.has_value() || !model.mantissa->normalizedSameAsRaw)
        continue;
      const std::optional<X2ShapeFact> shape =
          exact_decimal_display_shape_fact(*model.mantissa->normalizedDecimal);
      if (shape.has_value() && shape == x2_shape_fact_from_data_model(model))
        output.insert(*shape);
      continue;
    }
    if (model.kind == X2ShapeModelKind::ExponentEntry &&
        model.mantissa->radix == X2MantissaRadix::Decimal) {
      if (model.closedDecimalDisplay.has_value())
        output.insert(*model.closedDecimalDisplay);
    }
  }
  return output;
}

std::optional<X2MantissaDataModel> structural_mantissa_of_model(const X2ShapeDataModel& model) {
  if (model.kind == X2ShapeModelKind::Mantissa)
    return model.mantissa;
  if (model.kind == X2ShapeModelKind::ExponentEntry)
    return model.closedStructuralMantissa;
  return std::nullopt;
}

std::optional<std::string> structural_shape_fact_restored_visible_decimal(const X2ShapeFact& fact) {
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
  const std::optional<X2MantissaDataModel> mantissa = structural_mantissa_of_model(model);
  if (!mantissa.has_value() ||
      (mantissa->radix != X2MantissaRadix::Hex && mantissa->radix != X2MantissaRadix::Super))
    return std::nullopt;
  if (!mantissa->normalizedDecimal.has_value() || !mantissa->normalizedSameAsRaw)
    return std::nullopt;
  const std::optional<X2ShapeFact> display =
      exact_decimal_display_shape_fact(*mantissa->normalizedDecimal);
  return display.has_value() && *display == decimal_mantissa_shape_fact(mantissa->canonical)
             ? mantissa->normalizedDecimal
             : std::nullopt;
}

std::optional<std::string> x2_shape_fact_restored_visible_decimal(const X2ShapeFact& fact) {
  const X2ShapeSet single{fact};
  for (const X2ShapeFact& shape : decimal_display_shape_facts(&single)) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(shape);
    if (model.kind == X2ShapeModelKind::Mantissa && model.mantissa->radix == X2MantissaRadix::Decimal)
      return model.mantissa->normalizedDecimal;
    if (model.kind == X2ShapeModelKind::ExponentEntry &&
        model.mantissa->radix == X2MantissaRadix::Decimal)
      return model.normalizedDecimal;
  }
  return structural_shape_fact_restored_visible_decimal(fact);
}

std::set<std::string> x2_shape_set_restored_visible_decimals(const X2ShapeSet* input) {
  std::set<std::string> output;
  if (input == nullptr)
    return output;
  for (const X2ShapeFact& fact : *input) {
    const std::optional<std::string> decimal = x2_shape_fact_restored_visible_decimal(fact);
    if (decimal.has_value())
      output.insert(*decimal);
  }
  return output;
}

bool structural_exact_decimal_display_shape_is_computable(const std::string& value) {
  if (exact_decimal_display_shape_fact(value).has_value())
    return true;
  static const std::regex pattern(R"(^-0\.[0-9]+$)");
  return std::regex_match(value, pattern) && decimal_fraction_part_shape_fact(value).has_value();
}

std::optional<std::string> x2_shape_fact_shape_only_exact_decimal_display(const X2ShapeFact& fact) {
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
  const std::optional<X2MantissaDataModel> mantissa = structural_mantissa_of_model(model);
  if (!mantissa.has_value() ||
      (mantissa->radix != X2MantissaRadix::Hex && mantissa->radix != X2MantissaRadix::Super) ||
      !mantissa->normalizedDecimal.has_value() || !mantissa->normalizedSameAsRaw)
    return std::nullopt;
  return structural_exact_decimal_display_shape_is_computable(*mantissa->normalizedDecimal)
             ? mantissa->normalizedDecimal
             : std::nullopt;
}

std::optional<std::string> x2_shape_fact_exact_decimal_display(const X2ShapeFact& fact) {
  const std::optional<std::string> restored = x2_shape_fact_restored_visible_decimal(fact);
  return restored.has_value() ? restored : x2_shape_fact_shape_only_exact_decimal_display(fact);
}

std::optional<std::string> x2_shape_fact_exact_non_negative_display_decimal(const X2ShapeFact& fact) {
  const std::optional<std::string> decimal = x2_shape_fact_exact_decimal_display(fact);
  return !decimal.has_value() || decimal->starts_with('-') ? std::nullopt : decimal;
}

// ===========================================================================
// Session A.2: structural-hex UNARY decimal engine (faithful port of the TS
// plainProducesConcreteStructuralUnaryDecimal* cluster: square / abs / sign /
// exact-display / bitwise-NOT, plus the structural restore + shift machinery
// they pull in). Dead code; validated against the TS oracle in isolation.
// ===========================================================================

struct StructuralHexDecimalProduct {
  std::string value;
  std::optional<std::string> display;
  std::optional<X2ShapeFact> displayShape;
};

struct StructuralBitwiseOperand {
  std::vector<int> nibbles;
  bool structural = false;
};

struct StructuralScaledHexSquareOperand {
  int digit = 0;
  int exponent = 0;
};

struct ZeroPaddedIntegerMatch {
  int digit = 0;
  int zeros = 0;
};

// Cyrillic hex digits, sensitive to ASCII-vs-Cyrillic representation just like
// the TS structural-hex square regexes.
const std::string kStructuralCyrEs = "\xD0\xA1";   // С U+0421 (12)
const std::string kStructuralCyrGhe = "\xD0\x93";  // Г U+0413 (13)
const std::string kStructuralCyrIe = "\xD0\x95";   // Е U+0415 (14)

bool structural_is_letter_hex_square_digit(const std::string& cp) {  // [A-FСГЕ]
  if (cp.size() == 1U) {
    const char c = cp.at(0);
    return c >= 'A' && c <= 'F';
  }
  return cp == kStructuralCyrEs || cp == kStructuralCyrGhe || cp == kStructuralCyrIe;
}

bool structural_is_zero_square_integer_digit(const std::string& cp) {  // [ЕEF]
  if (cp == kStructuralCyrIe)
    return true;
  if (cp.size() == 1U) {
    const char c = cp.at(0);
    return c == 'E' || c == 'F';
  }
  return false;
}

bool structural_is_square_integer_digit(const std::string& cp) {  // [BСГD]
  if (cp == kStructuralCyrEs || cp == kStructuralCyrGhe)
    return true;
  if (cp.size() == 1U) {
    const char c = cp.at(0);
    return c == 'B' || c == 'D';
  }
  return false;
}

bool is_verified_scaled_hex_square_digit(int digit) {
  return digit == 11 || digit == 12 || digit == 13;
}

bool is_verified_scaled_hex_zero_square_digit(int digit) {
  return digit == 10 || digit == 14 || digit == 15;
}

std::set<X2ShapeFact> structural_mantissa_shape_facts(const X2ShapeSet& input) {
  std::set<X2ShapeFact> output;
  for (const X2ShapeFact& fact : input) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind == X2ShapeModelKind::Mantissa &&
        (model.mantissa->radix == X2MantissaRadix::Hex ||
         model.mantissa->radix == X2MantissaRadix::Super)) {
      const std::optional<X2ShapeFact> canonical = x2_shape_fact_from_data_model(model);
      if (canonical.has_value())
        output.insert(*canonical);
    }
  }
  return output;
}

std::set<X2ShapeFact> structural_restore_shape_facts(const X2ShapeSet& input) {
  std::set<X2ShapeFact> output = structural_mantissa_shape_facts(input);
  for (const X2ShapeFact& fact : input) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind == X2ShapeModelKind::ExponentEntry &&
        model.safety == X2ShapeSafety::StructuralOnly) {
      const std::optional<X2ShapeFact> canonical = x2_shape_fact_from_data_model(model);
      if (canonical.has_value())
        output.insert(*canonical);
      const std::optional<X2ShapeFact> closed = x2_closed_exponent_display_shape_fact(fact);
      if (closed.has_value())
        output.insert(*closed);
    }
  }
  return output;
}

std::set<X2ShapeFact> canonical_structural_shape_facts(const X2ShapeSet* input) {
  std::set<X2ShapeFact> output;
  if (input == nullptr)
    return output;
  for (const X2ShapeFact& fact : *input) {
    const std::optional<X2ShapeFact> canonical =
        x2_shape_fact_from_data_model(x2_shape_data_model_for_fact(fact));
    if (canonical.has_value() && x2_shape_fact_safety(*canonical) == X2ShapeSafety::StructuralOnly)
      output.insert(*canonical);
  }
  return output;
}

// --- bitwise-NOT (fact-driven) --------------------------------------------
std::optional<std::vector<int>> structural_mantissa_nibbles(const X2ShapeFact& fact) {
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
  if (model.kind != X2ShapeModelKind::Mantissa ||
      (model.mantissa->radix != X2MantissaRadix::Hex &&
       model.mantissa->radix != X2MantissaRadix::Super))
    return std::nullopt;
  std::vector<int> nibbles;
  const std::vector<std::string>& digits = model.mantissa->digits;
  for (std::size_t index = 0; index < digits.size() && index < 8U; ++index) {
    const std::optional<int> value = structural_hex_nibble_value(digits.at(index));
    if (!value.has_value())
      return std::nullopt;
    nibbles.push_back(*value);
  }
  while (nibbles.size() < 8U)
    nibbles.push_back(0);
  return nibbles;
}

std::string bitwise_mantissa_raw(const std::vector<int>& digits) {
  static const std::string kHex = "0123456789ABCDEF";
  const auto render = [&](int digit) -> std::string {
    return (digit >= 0 && digit < 16) ? std::string(1, kHex.at(static_cast<std::size_t>(digit)))
                                      : std::string{"0"};
  };
  std::string output = render(digits.empty() ? 0 : digits.front());
  output += ".";
  for (std::size_t index = 1; index < digits.size(); ++index)
    output += render(digits.at(index));
  return output;
}

std::optional<std::string> decimal_value_from_bitwise_mantissa_nibbles(
    const std::vector<int>& nibbles) {
  if (nibbles.size() != 8U)
    return std::nullopt;
  for (const int digit : nibbles)
    if (digit < 0 || digit > 9)
      return std::nullopt;
  return decimal_from_mantissa_digits(nibbles);
}

std::optional<X2ShapeFact> decimal_display_shape_from_bitwise_mantissa_nibbles(
    const std::vector<int>& nibbles) {
  if (!decimal_value_from_bitwise_mantissa_nibbles(nibbles).has_value())
    return std::nullopt;
  return decimal_mantissa_shape_fact(bitwise_mantissa_raw(nibbles));
}

std::optional<std::vector<int>> structural_bitwise_not_nibbles(
    const StructuralBitwiseOperand& operand) {
  if (operand.nibbles.size() != 8U)
    return std::nullopt;
  std::vector<int> result{8};
  for (std::size_t index = 1; index < 8U; ++index)
    result.push_back((~operand.nibbles.at(index)) & 0x0f);
  return result;
}

std::optional<std::string> structural_bitwise_not_decimal_value_from_fact(const X2ShapeFact& fact) {
  const std::optional<std::vector<int>> nibbles = structural_mantissa_nibbles(fact);
  if (!nibbles.has_value())
    return std::nullopt;
  const std::optional<std::vector<int>> result =
      structural_bitwise_not_nibbles(StructuralBitwiseOperand{*nibbles, true});
  return result.has_value() ? decimal_value_from_bitwise_mantissa_nibbles(*result) : std::nullopt;
}

std::optional<X2ShapeFact> structural_bitwise_not_decimal_display_shape_fact(const X2ShapeFact& fact) {
  const std::optional<std::vector<int>> nibbles = structural_mantissa_nibbles(fact);
  if (!nibbles.has_value())
    return std::nullopt;
  const std::optional<std::vector<int>> result =
      structural_bitwise_not_nibbles(StructuralBitwiseOperand{*nibbles, true});
  return result.has_value() ? decimal_display_shape_from_bitwise_mantissa_nibbles(*result)
                            : std::nullopt;
}

// --- sign / abs ------------------------------------------------------------
std::optional<std::string> structural_hex_sign_decimal_value(const X2ShapeFact& fact) {
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
  if (model.kind != X2ShapeModelKind::Mantissa ||
      (model.mantissa->radix != X2MantissaRadix::Hex &&
       model.mantissa->radix != X2MantissaRadix::Super))
    return std::nullopt;
  if (model.mantissa->radix == X2MantissaRadix::Super)
    return std::optional<std::string>{"0"};
  for (const std::string& digit : model.mantissa->digits) {
    const std::optional<int> value = structural_hex_nibble_value(digit);
    if (!value.has_value())
      return std::nullopt;
    if (*value == 0)
      continue;
    if (*value == 15)
      return std::nullopt;
    return model.mantissa->sign == "-" ? std::optional<std::string>{"-1"}
                                       : std::optional<std::string>{"1"};
  }
  return std::optional<std::string>{"0"};
}

std::optional<std::string> direct_structural_sign_decimal_value(const X2ShapeFact& fact) {
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
  return (model.kind == X2ShapeModelKind::ExponentEntry &&
          model.mantissa->radix == X2MantissaRadix::Super && model.exponentSign == "")
             ? std::optional<std::string>{"0"}
             : std::nullopt;
}

std::optional<X2ShapeFact> structural_abs_mantissa_shape_fact(const X2ShapeFact& fact) {
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
  if (model.kind != X2ShapeModelKind::Mantissa ||
      (model.mantissa->radix != X2MantissaRadix::Hex &&
       model.mantissa->radix != X2MantissaRadix::Super))
    return std::nullopt;
  const std::string unsigned_value =
      model.mantissa->sign == "-" ? model.mantissa->canonical.substr(1) : model.mantissa->canonical;
  return x2_mantissa_shape_fact_from_model(
      structural_mantissa_data_model(model.mantissa->radix, unsigned_value,
                                     X2ShapeSafety::StructuralOnly));
}

std::optional<std::string> structural_abs_decimal_value(const X2ShapeFact& fact) {
  const std::optional<X2ShapeFact> abs = structural_abs_mantissa_shape_fact(fact);
  return abs.has_value() ? x2_shape_fact_exact_non_negative_display_decimal(*abs) : std::nullopt;
}

std::optional<X2ShapeFact> structural_abs_decimal_display_shape_fact(const X2ShapeFact& fact) {
  const std::optional<std::string> value = structural_abs_decimal_value(fact);
  return value.has_value() ? exact_decimal_display_shape_fact(*value) : std::nullopt;
}

// --- exact-display unary ([x] 0x34, {x} 0x35) ------------------------------
bool exact_decimal_display_shape_opcodes_has(int opcode) {
  static const std::set<int> opcodes{0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c,
                                      0x1d, 0x1e, 0x21, 0x22, 0x23, 0x31, 0x32, 0x34};
  return opcodes.count(opcode) != 0;
}

std::optional<X2ShapeFact> concrete_decimal_unary_display_shape_fact(int opcode,
                                                                     const std::string& value) {
  if (opcode == 0x35)
    return decimal_fraction_part_shape_fact(value);
  if (!exact_decimal_display_shape_opcodes_has(opcode))
    return std::nullopt;
  const std::optional<std::string> concrete = concrete_decimal_unary_value_impl(opcode, value);
  return concrete.has_value() ? exact_decimal_display_shape_fact(*concrete) : std::nullopt;
}

std::optional<std::string> structural_exact_display_unary_decimal_value(int opcode,
                                                                        const X2ShapeFact& fact) {
  const std::optional<std::string> value = x2_shape_fact_shape_only_exact_decimal_display(fact);
  return value.has_value() ? concrete_decimal_unary_value_impl(opcode, *value) : std::nullopt;
}

std::optional<X2ShapeFact> structural_exact_display_unary_decimal_shape_fact(int opcode,
                                                                             const X2ShapeFact& fact) {
  const std::optional<std::string> value = x2_shape_fact_shape_only_exact_decimal_display(fact);
  return value.has_value() ? concrete_decimal_unary_display_shape_fact(opcode, *value) : std::nullopt;
}

std::optional<X2ShapeFact> exact_plain_integer_decimal_mantissa_shape_fact(const std::string& value) {
  const std::optional<std::string> normalized = normalize_plain_decimal(value);
  if (!normalized.has_value())
    return std::nullopt;
  static const std::regex integer_pattern(R"(^-?[0-9]+$)");
  if (!std::regex_match(*normalized, integer_pattern))
    return std::nullopt;
  if (decimal_mantissa_digit_count(*normalized) > 8)
    return std::nullopt;
  return decimal_mantissa_shape_fact(*normalized);
}

// --- shift machinery (shared with the deferred binary engine) --------------
std::string trim_shifted_raw_fraction(const std::string& raw) {
  if (raw.find('.') == std::string::npos)
    return raw;
  std::string trimmed = raw;
  const std::size_t last = trimmed.find_last_not_of('0');
  trimmed = last == std::string::npos ? std::string{} : trimmed.substr(0, last + 1);
  if (!trimmed.empty() && trimmed.back() == '.')
    trimmed.pop_back();
  static const std::regex all_zero(R"(^0+$)");
  return (trimmed == "0" || std::regex_match(trimmed, all_zero)) ? std::string{"0"} : trimmed;
}

std::optional<std::string> shift_exact_decimal_value(const std::string& value, int shift) {
  const std::optional<ExactDecimalParts> parts = parse_exact_decimal(value);
  if (!parts.has_value())
    return std::nullopt;
  const int scale = parts->scale - shift;
  if (scale < 0)
    return exact_decimal_to_normalized(BigInt::mul(parts->num, big_pow10(-scale)), 0);
  return exact_decimal_to_normalized(parts->num, scale);
}

std::optional<X2ShapeFact> raw_shifted_decimal_exponent_display_shape(const std::string& sign,
                                                                      const std::string& raw) {
  std::string digits = raw;
  const std::size_t dot = digits.find('.');
  if (dot != std::string::npos)
    digits.erase(dot, 1);
  static const std::regex digit_pattern(R"(^[0-9]+$)");
  if (digits.size() <= 8U || !std::regex_match(digits, digit_pattern))
    return std::nullopt;
  const std::optional<std::string> exponent =
      canonical_exponent_shape_raw(std::to_string(static_cast<int>(digits.size()) - 1));
  if (!exponent.has_value())
    return std::nullopt;
  const std::string kept = digits.substr(0, 8);
  std::string fraction = kept.substr(1);
  const std::size_t last = fraction.find_last_not_of('0');
  fraction = last == std::string::npos ? std::string{} : fraction.substr(0, last + 1);
  const std::string mantissa = fraction.empty() ? sign + kept.substr(0, 1)
                                                : sign + kept.substr(0, 1) + "." + fraction;
  return decimal_exponent_shape_fact(mantissa, *exponent);
}

std::optional<X2ShapeFact> shift_raw_decimal_display_shape(const std::string& raw, int shift) {
  static const std::regex pattern(R"(^(-?)([0-9]+)(?:\.([0-9]+))?$)");
  std::smatch match;
  if (!std::regex_match(raw, match, pattern))
    return std::nullopt;
  const std::string sign = match[1].str();
  const std::string integer = match[2].str();
  const std::string fraction = match[3].matched ? match[3].str() : std::string{};
  if (integer.size() == 1U && fraction.empty() && integer == "0" && shift < 0) {
    const std::optional<std::string> exponent = canonical_exponent_shape_raw(std::to_string(shift));
    return exponent.has_value()
               ? std::optional<X2ShapeFact>{decimal_exponent_shape_fact(sign + "0", *exponent)}
               : std::nullopt;
  }
  const std::string digits = integer + fraction;
  const int point = static_cast<int>(integer.size()) + shift;
  if (point <= 0) {
    const std::optional<std::string> value = normalize_plain_decimal(raw);
    const std::optional<std::string> shifted_value =
        value.has_value() ? shift_exact_decimal_value(*value, shift) : std::nullopt;
    return shifted_value.has_value() ? exact_decimal_display_shape_fact(*shifted_value) : std::nullopt;
  }
  std::string unsigned_value;
  if (static_cast<std::size_t>(point) >= digits.size()) {
    unsigned_value = digits + std::string(static_cast<std::size_t>(point) - digits.size(), '0');
  } else {
    unsigned_value = trim_shifted_raw_fraction(digits.substr(0, static_cast<std::size_t>(point)) +
                                               "." + digits.substr(static_cast<std::size_t>(point)));
  }
  const std::optional<X2ShapeFact> exponent_shape =
      raw_shifted_decimal_exponent_display_shape(sign, unsigned_value);
  if (exponent_shape.has_value())
    return exponent_shape;
  return decimal_mantissa_shape_fact(sign + unsigned_value);
}

std::optional<X2ShapeFact> shift_decimal_product_display_shape(const X2ShapeFact& shape,
                                                               const std::string& value, int shift) {
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(shape);
  if (model.kind == X2ShapeModelKind::ExponentEntry &&
      model.mantissa->radix == X2MantissaRadix::Decimal) {
    int current = 0;
    if (model.exponentRaw != "") {
      const std::optional<int> parsed = parse_small_exponent_int(model.exponentRaw);
      if (!parsed.has_value())
        return std::nullopt;
      current = *parsed;
    }
    const std::optional<std::string> shifted_value = shift_exact_decimal_value(value, shift);
    const std::optional<X2ShapeFact> ordinary =
        shifted_value.has_value() ? exact_ordinary_decimal_mantissa_display_shape_fact(*shifted_value)
                                  : std::nullopt;
    if (ordinary.has_value())
      return ordinary;
    const std::optional<std::string> exponent =
        canonical_exponent_shape_raw(std::to_string(current + shift));
    return exponent.has_value()
               ? std::optional<X2ShapeFact>{decimal_exponent_shape_fact(model.mantissa->canonical,
                                                                        *exponent)}
               : std::nullopt;
  }
  if (model.kind == X2ShapeModelKind::Mantissa && model.mantissa->radix == X2MantissaRadix::Decimal &&
      model.mantissa->canonical == "0" &&
      model.mantissa->normalizedDecimal == std::optional<std::string>{"0"}) {
    const std::optional<std::string> exponent = canonical_exponent_shape_raw(std::to_string(shift));
    return exponent.has_value()
               ? std::optional<X2ShapeFact>{decimal_exponent_shape_fact("0", *exponent)}
               : std::nullopt;
  }
  const std::optional<std::string> shifted_value = shift_exact_decimal_value(value, shift);
  return shifted_value.has_value() ? exact_decimal_display_shape_fact(*shifted_value) : std::nullopt;
}

std::optional<X2ShapeFact> shift_structural_hex_exact_display_shape(
    const StructuralHexDecimalProduct& product, int shift) {
  std::optional<X2ShapeFact> source_shape;
  if (product.displayShape.has_value())
    source_shape = product.displayShape;
  else if (!product.display.has_value())
    source_shape = exact_decimal_display_shape_fact(product.value);
  else
    source_shape = decimal_mantissa_shape_fact(*product.display);
  const std::optional<X2ShapeFact> shifted_shape =
      source_shape.has_value() ? shift_decimal_product_display_shape(*source_shape, product.value, shift)
                               : std::nullopt;
  if (shifted_shape.has_value())
    return shifted_shape;
  const std::optional<std::string> value = shift_exact_decimal_value(product.value, shift);
  return value.has_value() ? exact_decimal_display_shape_fact(*value) : std::nullopt;
}

std::optional<X2ShapeFact> shift_structural_hex_raw_display_shape(
    const StructuralHexDecimalProduct& product, int shift) {
  if (product.display.has_value())
    return shift_raw_decimal_display_shape(*product.display, shift);
  return shift_structural_hex_exact_display_shape(product, shift);
}

std::optional<StructuralHexDecimalProduct> shift_structural_hex_decimal_product(
    const std::optional<StructuralHexDecimalProduct>& product, const std::string& exponent_raw,
    const std::string& mode) {
  if (!product.has_value())
    return std::nullopt;
  const std::optional<std::string> exponent = canonical_exponent_shape_raw(exponent_raw);
  if (!exponent.has_value())
    return std::nullopt;
  int shift = 0;
  if (*exponent != "") {
    const std::optional<int> parsed = parse_small_exponent_int(*exponent);
    if (!parsed.has_value())
      return std::nullopt;
    shift = *parsed;
  }
  if (shift == 0)
    return product;
  const std::optional<std::string> value = shift_exact_decimal_value(product->value, shift);
  if (!value.has_value())
    return std::nullopt;
  const std::optional<X2ShapeFact> display_shape =
      mode == "raw-display" ? shift_structural_hex_raw_display_shape(*product, shift)
                            : shift_structural_hex_exact_display_shape(*product, shift);
  if (!display_shape.has_value())
    return std::nullopt;
  StructuralHexDecimalProduct output;
  output.value = *value;
  output.displayShape = display_shape;
  return output;
}

std::optional<X2ShapeFact> structural_hex_decimal_product_display_shape(
    const std::optional<StructuralHexDecimalProduct>& product) {
  if (!product.has_value())
    return std::nullopt;
  if (product->displayShape.has_value())
    return product->displayShape;
  if (!product->display.has_value())
    return std::nullopt;
  return decimal_mantissa_shape_fact(*product->display);
}

// --- square (x^2, 0x22) ----------------------------------------------------
std::optional<StructuralHexDecimalProduct> structural_single_hex_digit_square_decimal_product(
    std::optional<int> digit) {
  if (!digit.has_value())
    return std::nullopt;
  switch (*digit) {
    case 10:
      return StructuralHexDecimalProduct{"0", std::string{"00"}, std::nullopt};
    case 11:
      return StructuralHexDecimalProduct{"10", std::string{"10"}, std::nullopt};
    case 12:
      return StructuralHexDecimalProduct{"20", std::string{"20"}, std::nullopt};
    case 13:
      return StructuralHexDecimalProduct{"30", std::string{"30"}, std::nullopt};
    case 14:
    case 15:
      return StructuralHexDecimalProduct{"0", std::string{"0"}, std::nullopt};
    default:
      return std::nullopt;
  }
}

// Matches /^0*([A-FСГЕ])$/u, returning the nibble value of the captured digit.
std::optional<int> structural_match_trailing_letter_hex(const std::string& raw) {
  const std::vector<std::string> points = utf8_codepoints(raw);
  if (points.empty())
    return std::nullopt;
  for (std::size_t index = 0; index + 1U < points.size(); ++index)
    if (points.at(index) != "0")
      return std::nullopt;
  const std::optional<int> value = structural_hex_nibble_value(points.back());
  if (!value.has_value() || *value < 10)
    return std::nullopt;
  return value;
}

// Matches /^0*(class)(0+)$/u over UTF-8 code points.
std::optional<ZeroPaddedIntegerMatch> structural_match_zero_padded_integer(
    const std::string& raw, bool (*in_class)(const std::string&)) {
  const std::vector<std::string> points = utf8_codepoints(raw);
  std::size_t index = 0;
  while (index < points.size() && points.at(index) == "0")
    ++index;
  if (index >= points.size() || !in_class(points.at(index)))
    return std::nullopt;
  const std::optional<int> value = structural_hex_nibble_value(points.at(index));
  ++index;
  std::size_t zeros = 0;
  for (; index < points.size(); ++index) {
    if (points.at(index) != "0")
      return std::nullopt;
    ++zeros;
  }
  if (zeros == 0 || !value.has_value())
    return std::nullopt;
  return ZeroPaddedIntegerMatch{*value, static_cast<int>(zeros)};
}

// Matches /^(?:0)?\.(0*)([A-FСГЕ])$/u over UTF-8 code points.
std::optional<StructuralScaledHexSquareOperand> structural_fractional_hex_square_operand_from_raw(
    const std::string& raw, int exponent, bool (*is_verified)(int)) {
  const std::vector<std::string> points = utf8_codepoints(raw);
  std::size_t index = 0;
  if (index < points.size() && points.at(index) == "0")
    ++index;
  if (index >= points.size() || points.at(index) != ".")
    return std::nullopt;
  ++index;
  std::size_t fraction_zeros = 0;
  while (index < points.size() && points.at(index) == "0") {
    ++fraction_zeros;
    ++index;
  }
  if (index >= points.size() || !structural_is_letter_hex_square_digit(points.at(index)))
    return std::nullopt;
  const std::optional<int> value = structural_hex_nibble_value(points.at(index));
  ++index;
  if (index != points.size())
    return std::nullopt;
  if (!value.has_value() || !is_verified(*value))
    return std::nullopt;
  return StructuralScaledHexSquareOperand{*value,
                                          exponent - (static_cast<int>(fraction_zeros) + 1)};
}

std::optional<int> structural_hex_square_exponent_number(const std::string& exponent_raw) {
  const std::optional<std::string> exponent = canonical_exponent_shape_raw(exponent_raw);
  if (!exponent.has_value())
    return std::nullopt;
  if (*exponent == "")
    return 0;
  return parse_small_exponent_int(*exponent);
}

std::optional<StructuralScaledHexSquareOperand> structural_hex_square_exponent_operand_from_mantissa(
    const X2MantissaDataModel& mantissa, int exponent, bool (*is_verified)(int)) {
  if (mantissa.radix != X2MantissaRadix::Hex)
    return std::nullopt;
  const std::string raw = mantissa.sign == "" ? mantissa.canonical : mantissa.canonical.substr(1);
  const std::optional<int> letter = structural_match_trailing_letter_hex(raw);
  if (letter.has_value())
    return is_verified(*letter)
               ? std::optional<StructuralScaledHexSquareOperand>{{*letter, exponent}}
               : std::nullopt;
  return structural_fractional_hex_square_operand_from_raw(raw, exponent, is_verified);
}

std::optional<StructuralScaledHexSquareOperand> structural_scaled_hex_square_operand_from_shape_model(
    const X2ShapeDataModel& model) {
  if (model.kind == X2ShapeModelKind::ExponentEntry) {
    const std::optional<int> exponent = structural_hex_square_exponent_number(model.exponentRaw);
    return exponent.has_value()
               ? structural_hex_square_exponent_operand_from_mantissa(
                     *model.mantissa, *exponent, is_verified_scaled_hex_square_digit)
               : std::nullopt;
  }
  if (model.kind != X2ShapeModelKind::Mantissa || model.mantissa->radix != X2MantissaRadix::Hex)
    return std::nullopt;
  const std::string raw =
      model.mantissa->sign == "" ? model.mantissa->canonical : model.mantissa->canonical.substr(1);
  const std::optional<ZeroPaddedIntegerMatch> integer =
      structural_match_zero_padded_integer(raw, structural_is_square_integer_digit);
  if (integer.has_value())
    return is_verified_scaled_hex_square_digit(integer->digit)
               ? std::optional<StructuralScaledHexSquareOperand>{{integer->digit, integer->zeros}}
               : std::nullopt;
  return structural_fractional_hex_square_operand_from_raw(raw, 0,
                                                           is_verified_scaled_hex_square_digit);
}

std::optional<StructuralScaledHexSquareOperand>
structural_scaled_hex_zero_square_operand_from_shape_model(const X2ShapeDataModel& model) {
  if (model.kind == X2ShapeModelKind::ExponentEntry) {
    const std::optional<int> exponent = structural_hex_square_exponent_number(model.exponentRaw);
    return exponent.has_value()
               ? structural_hex_square_exponent_operand_from_mantissa(
                     *model.mantissa, *exponent, is_verified_scaled_hex_zero_square_digit)
               : std::nullopt;
  }
  if (model.kind != X2ShapeModelKind::Mantissa || model.mantissa->radix != X2MantissaRadix::Hex)
    return std::nullopt;
  const std::string raw =
      model.mantissa->sign == "" ? model.mantissa->canonical : model.mantissa->canonical.substr(1);
  const std::optional<ZeroPaddedIntegerMatch> integer =
      structural_match_zero_padded_integer(raw, structural_is_zero_square_integer_digit);
  if (integer.has_value())
    return is_verified_scaled_hex_zero_square_digit(integer->digit)
               ? std::optional<StructuralScaledHexSquareOperand>{{integer->digit, integer->zeros}}
               : std::nullopt;
  return structural_fractional_hex_square_operand_from_raw(raw, 0,
                                                           is_verified_scaled_hex_zero_square_digit);
}

std::optional<StructuralHexDecimalProduct> structural_scaled_a_hex_square_decimal_product(
    int exponent) {
  if (exponent >= 0) {
    StructuralHexDecimalProduct product;
    product.value = "0";
    product.display = std::string(static_cast<std::size_t>(exponent * 2 + 2), '0');
    return product;
  }
  const std::optional<std::string> display_exponent =
      canonical_exponent_shape_raw(std::to_string(exponent * 2 + 1));
  if (!display_exponent.has_value())
    return std::nullopt;
  StructuralHexDecimalProduct product;
  product.value = "0";
  product.displayShape = decimal_exponent_shape_fact("0", *display_exponent);
  return product;
}

std::optional<StructuralHexDecimalProduct> structural_scaled_hex_zero_square_decimal_product(
    const X2ShapeDataModel& model) {
  const std::optional<StructuralScaledHexSquareOperand> operand =
      structural_scaled_hex_zero_square_operand_from_shape_model(model);
  if (!operand.has_value())
    return std::nullopt;
  if (operand->digit == 10)
    return structural_scaled_a_hex_square_decimal_product(operand->exponent);
  return StructuralHexDecimalProduct{"0", std::string{"0"}, std::nullopt};
}

std::optional<StructuralHexDecimalProduct> structural_scaled_hex_square_decimal_product(
    const X2ShapeDataModel& model) {
  const std::optional<StructuralScaledHexSquareOperand> operand =
      structural_scaled_hex_square_operand_from_shape_model(model);
  if (!operand.has_value())
    return std::nullopt;
  return shift_structural_hex_decimal_product(
      structural_single_hex_digit_square_decimal_product(operand->digit),
      std::to_string(operand->exponent * 2), "exact-display");
}

std::optional<StructuralHexDecimalProduct> structural_hex_square_decimal_product(
    const X2ShapeFact& fact) {
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
  const std::optional<StructuralHexDecimalProduct> scaled_zero =
      structural_scaled_hex_zero_square_decimal_product(model);
  if (scaled_zero.has_value())
    return scaled_zero;
  const std::optional<StructuralHexDecimalProduct> scaled =
      structural_scaled_hex_square_decimal_product(model);
  if (scaled.has_value())
    return scaled;
  if (model.kind != X2ShapeModelKind::Mantissa ||
      (model.mantissa->radix != X2MantissaRadix::Hex &&
       model.mantissa->radix != X2MantissaRadix::Super) ||
      model.mantissa->hasDecimalPoint)
    return std::nullopt;
  if (model.mantissa->radix == X2MantissaRadix::Super)
    return StructuralHexDecimalProduct{"0", std::string{"0"}, std::nullopt};
  const std::string raw =
      model.mantissa->sign == "" ? model.mantissa->canonical : model.mantissa->canonical.substr(1);
  const std::optional<int> significant = structural_match_trailing_letter_hex(raw);
  if (!significant.has_value())
    return std::nullopt;
  return structural_single_hex_digit_square_decimal_product(*significant);
}

std::optional<std::string> structural_hex_square_decimal_value(const X2ShapeFact& fact) {
  const std::optional<StructuralHexDecimalProduct> product = structural_hex_square_decimal_product(fact);
  return product.has_value() ? std::optional<std::string>{product->value} : std::nullopt;
}

std::optional<X2ShapeFact> structural_hex_square_decimal_display_shape_fact(const X2ShapeFact& fact) {
  return structural_hex_decimal_product_display_shape(structural_hex_square_decimal_product(fact));
}

// --- entry points ----------------------------------------------------------
std::set<std::string> plain_produces_concrete_structural_unary_decimal_values(
    int opcode, const X2ShapeSet* x_shape) {
  std::set<std::string> output;
  if (opcode != 0x22 && opcode != 0x31 && opcode != 0x32 && opcode != 0x34 && opcode != 0x35 &&
      opcode != 0x3a)
    return output;
  for (const X2ShapeFact& fact :
       structural_restore_shape_facts(canonical_structural_shape_facts(x_shape))) {
    std::optional<std::string> value;
    if (opcode == 0x22)
      value = structural_hex_square_decimal_value(fact);
    else if (opcode == 0x31)
      value = structural_abs_decimal_value(fact);
    else if (opcode == 0x32)
      value = structural_hex_sign_decimal_value(fact);
    else if (opcode == 0x34 || opcode == 0x35)
      value = structural_exact_display_unary_decimal_value(opcode, fact);
    else
      value = structural_bitwise_not_decimal_value_from_fact(fact);
    if (value.has_value())
      output.insert(*value);
  }
  return output;
}

std::set<X2ShapeFact> plain_produces_concrete_structural_unary_decimal_shape_facts(
    int opcode, const X2ShapeSet* x_shape) {
  std::set<X2ShapeFact> output;
  if (opcode != 0x22 && opcode != 0x31 && opcode != 0x32 && opcode != 0x34 && opcode != 0x35 &&
      opcode != 0x3a)
    return output;
  for (const X2ShapeFact& fact :
       structural_restore_shape_facts(canonical_structural_shape_facts(x_shape))) {
    std::optional<X2ShapeFact> result;
    if (opcode == 0x22)
      result = structural_hex_square_decimal_display_shape_fact(fact);
    else if (opcode == 0x31)
      result = structural_abs_decimal_display_shape_fact(fact);
    else if (opcode == 0x32)
      result = exact_plain_integer_decimal_mantissa_shape_fact(
          structural_hex_sign_decimal_value(fact).value_or(""));
    else if (opcode == 0x34 || opcode == 0x35)
      result = structural_exact_display_unary_decimal_shape_fact(opcode, fact);
    else
      result = structural_bitwise_not_decimal_display_shape_fact(fact);
    if (result.has_value())
      output.insert(*result);
  }
  return output;
}

std::set<std::string> plain_produces_concrete_direct_structural_unary_decimal_values(
    int opcode, const X2ShapeSet* direct_x_shape) {
  std::set<std::string> output;
  if (opcode != 0x32)
    return output;
  for (const X2ShapeFact& fact : canonical_structural_shape_facts(direct_x_shape)) {
    const std::optional<std::string> value = direct_structural_sign_decimal_value(fact);
    if (value.has_value())
      output.insert(*value);
  }
  return output;
}

std::set<X2ShapeFact> plain_produces_concrete_direct_structural_unary_decimal_shape_facts(
    int opcode, const X2ShapeSet* direct_x_shape) {
  std::set<X2ShapeFact> output;
  for (const std::string& value :
       plain_produces_concrete_direct_structural_unary_decimal_values(opcode, direct_x_shape)) {
    const std::optional<X2ShapeFact> fact = exact_plain_integer_decimal_mantissa_shape_fact(value);
    if (fact.has_value())
      output.insert(*fact);
  }
  return output;
}

// ===========================================================================
// Session A.2b (part 2): binary structural-hex arithmetic engine (faithful port
// of the TS structuralHexBinaryDecimalProducts cluster: +,-,*,/ over single
// hex-digit / scaled-exponent / carry-normalized operands against decimals).
// Dead code; validated against the TS oracle in isolation.
// ===========================================================================
enum class StructuralHexOperandKind { Digit, Exponent, CarryNormalized };

struct StructuralHexArithmeticOperand {
  StructuralHexOperandKind kind = StructuralHexOperandKind::Digit;
  int digit = 0;
  std::string exponent;
  StructuralHexDecimalProduct product;  // carry-normalized only
};

struct StructuralHexExponentOperand {
  int digit = 0;
  std::string exponent;
};

const std::set<std::string>& structural_hex_decimal_operands() {
  static const std::set<std::string> operands = [] {
    std::set<std::string> output;
    for (int value = 0; value < 19; ++value)
      output.insert(std::to_string(value));
    return output;
  }();
  return operands;
}

bool is_verified_arithmetic_hex_digit(int digit) {
  return digit == 10 || digit == 11 || digit == 12 || digit == 13 || digit == 14;
}

std::optional<int> verified_decimal_operand_value(const std::string& value) {
  return structural_hex_decimal_operands().count(value) != 0
             ? std::optional<int>{std::stoi(value)}
             : std::nullopt;
}

BigInt bigint_from_nonneg_decimal(const std::string& value) {
  return BigInt::from_unsigned_digits(value);
}

std::optional<StructuralHexDecimalProduct> structural_hex_decimal_integer_product(long long value) {
  const std::string display = std::to_string(value);
  const std::optional<std::string> normalized = normalize_plain_decimal(display);
  if (!normalized.has_value())
    return std::nullopt;
  StructuralHexDecimalProduct out;
  out.value = *normalized;
  out.display = display;
  return out;
}

std::optional<std::string> canonical_mk61_display_exponent_raw(const std::string& raw) {
  const std::optional<std::string> canonical = canonical_exponent_shape_raw(raw);
  if (!canonical.has_value())
    return std::nullopt;
  const bool negative = !canonical->empty() && canonical->front() == '-';
  const std::string digits = negative ? canonical->substr(1) : *canonical;
  const std::size_t pos = digits.find_first_not_of('0');
  std::string stripped = pos == std::string::npos ? "" : digits.substr(pos);
  if (stripped.empty())
    stripped = "0";
  if (stripped == "0")
    return std::string{"0"};
  return (negative ? std::string{"-"} : std::string{}) + stripped;
}

std::optional<std::string> normalized_mk61_display_exponent_value(const std::string& mantissa,
                                                                  const std::string& exponent_raw) {
  const std::optional<ExactDecimalParts> mantissa_parts = parse_exact_decimal(mantissa);
  const std::optional<std::string> canonical_exponent = canonical_mk61_display_exponent_raw(exponent_raw);
  if (!mantissa_parts.has_value() || !canonical_exponent.has_value())
    return std::nullopt;
  const std::optional<int> exponent = parse_small_exponent_int(*canonical_exponent);
  if (!exponent.has_value())
    return std::nullopt;
  const int shifted_scale = mantissa_parts->scale - *exponent;
  const BigInt num = shifted_scale < 0
                         ? BigInt::mul(mantissa_parts->num, big_pow10(-shifted_scale))
                         : mantissa_parts->num;
  return exact_decimal_to_normalized(num, std::max(0, shifted_scale));
}

std::optional<StructuralHexDecimalProduct> structural_hex_decimal_product_from_mk61_display(
    const std::string& display) {
  static const std::regex pattern(R"(^(-?)([0-9]+)(?:,([0-9]*))?([+-]?[0-9]{2})?$)");
  std::smatch match;
  if (!std::regex_match(display, match, pattern))
    return std::nullopt;
  const std::string sign = match[1].str();
  const std::string integer = match[2].str();
  const bool has_fraction = match[3].matched;
  const std::string fraction = match[3].str();
  const bool has_exponent = match[4].matched;
  const std::string exponent_raw = match[4].str();
  const std::string mantissa = (!has_fraction || fraction.empty())
                                   ? sign + integer
                                   : sign + integer + "." + fraction;
  const std::optional<std::string> value =
      has_exponent ? normalized_mk61_display_exponent_value(mantissa, exponent_raw)
                   : normalize_plain_decimal(mantissa);
  if (!value.has_value())
    return std::nullopt;
  if (has_exponent) {
    const std::optional<std::string> exponent = canonical_mk61_display_exponent_raw(exponent_raw);
    if (!exponent.has_value())
      return std::nullopt;
    StructuralHexDecimalProduct out;
    out.value = *value;
    out.displayShape = decimal_exponent_shape_fact(mantissa, *exponent);
    return out;
  }
  StructuralHexDecimalProduct out;
  out.value = *value;
  out.display = mantissa;
  return out;
}

std::map<std::string, StructuralHexDecimalProduct> structural_hex_decimal_product_table(
    const std::vector<std::pair<std::string, std::string>>& entries) {
  std::map<std::string, StructuralHexDecimalProduct> output;
  for (const auto& [key, display] : entries) {
    const std::optional<StructuralHexDecimalProduct> product =
        structural_hex_decimal_product_from_mk61_display(display);
    if (product.has_value())
      output.emplace(key, *product);
  }
  return output;
}

std::optional<StructuralHexDecimalProduct> structural_hex_decimal_product_from_exact(const BigInt& num,
                                                                                     int scale) {
  const std::optional<std::string> value = exact_decimal_to_normalized(num, scale);
  if (!value.has_value())
    return std::nullopt;
  const std::optional<X2ShapeFact> display_shape = exact_decimal_display_shape_fact(*value);
  if (!display_shape.has_value())
    return std::nullopt;
  StructuralHexDecimalProduct out;
  out.value = *value;
  out.displayShape = display_shape;
  return out;
}

std::optional<StructuralHexDecimalProduct> structural_hex_decimal_product_from_concrete_decimal_value(
    const std::optional<std::string>& value) {
  if (!value.has_value())
    return std::nullopt;
  const std::optional<X2ShapeFact> display_shape = exact_decimal_display_shape_fact(*value);
  if (!display_shape.has_value())
    return std::nullopt;
  StructuralHexDecimalProduct out;
  out.value = *value;
  out.displayShape = display_shape;
  return out;
}

// --- single-digit +/- products --------------------------------------------
std::optional<StructuralHexDecimalProduct> structural_hex_digit_plus_decimal_product(int left_digit,
                                                                                     const std::string& right) {
  if (!is_verified_arithmetic_hex_digit(left_digit))
    return std::nullopt;
  const std::optional<int> right_value = verified_decimal_operand_value(right);
  return right_value.has_value()
             ? structural_hex_decimal_integer_product(left_digit + *right_value)
             : std::nullopt;
}

std::optional<StructuralHexDecimalProduct> decimal_plus_structural_hex_digit_product(
    const std::string& left, int right_digit) {
  if (!is_verified_arithmetic_hex_digit(right_digit))
    return std::nullopt;
  const std::optional<int> left_value = verified_decimal_operand_value(left);
  if (!left_value.has_value())
    return std::nullopt;
  if (*left_value >= 10)
    return structural_hex_decimal_integer_product(*left_value + right_digit);
  const int value = (*left_value + right_digit) % 16;
  return structural_hex_decimal_integer_product(value >= 10 ? value - 10 : value);
}

std::optional<StructuralHexDecimalProduct> structural_hex_digit_minus_decimal_product(
    int left_digit, const std::string& right) {
  if (!is_verified_arithmetic_hex_digit(left_digit))
    return std::nullopt;
  const std::optional<int> right_value = verified_decimal_operand_value(right);
  if (!right_value.has_value())
    return std::nullopt;
  const int value = left_digit - *right_value;
  return structural_hex_decimal_integer_product(*right_value == 0 || value < 10 ? value : value - 10);
}

std::optional<StructuralHexDecimalProduct> decimal_minus_structural_hex_digit_product(
    const std::string& left, int right_digit) {
  if (!is_verified_arithmetic_hex_digit(right_digit))
    return std::nullopt;
  const std::optional<int> left_value = verified_decimal_operand_value(left);
  if (!left_value.has_value())
    return std::nullopt;
  if (*left_value >= 10 && right_digit > 10)
    return structural_hex_decimal_integer_product(*left_value - right_digit + 16);
  const int value = *left_value - right_digit;
  return structural_hex_decimal_integer_product(value <= -11 ? value + 10 : value);
}

std::optional<StructuralHexDecimalProduct> structural_hex_digit_plus_structural_hex_digit_product(
    int left_digit, int right_digit) {
  if (!is_verified_arithmetic_hex_digit(left_digit) || !is_verified_arithmetic_hex_digit(right_digit))
    return std::nullopt;
  return structural_hex_decimal_integer_product((left_digit + right_digit) % 16);
}

std::optional<StructuralHexDecimalProduct> structural_hex_digit_minus_structural_hex_digit_product(
    int left_digit, int right_digit) {
  if (!is_verified_arithmetic_hex_digit(left_digit) || !is_verified_arithmetic_hex_digit(right_digit))
    return std::nullopt;
  return structural_hex_decimal_integer_product(left_digit - right_digit);
}

// --- single-digit * / / products (table driven) ---------------------------
const std::map<std::string, StructuralHexDecimalProduct>& structural_hex_digit_times_decimal_table() {
  static const std::map<std::string, StructuralHexDecimalProduct> table = [] {
    const std::vector<std::pair<std::string, std::pair<std::string, std::string>>> entries = {
        {"10:0", {"0", "0"}},   {"10:1", {"0", "0"}},   {"10:2", {"4", "4"}},
        {"10:3", {"4", "4"}},   {"10:4", {"8", "8"}},   {"10:5", {"50", "50"}},
        {"10:6", {"0", "0"}},   {"10:7", {"10", "10"}}, {"10:8", {"20", "20"}},
        {"10:9", {"30", "30"}}, {"10:10", {"0", "00"}}, {"10:11", {"10", "10"}},
        {"10:12", {"4", "04"}}, {"10:13", {"14", "14"}}, {"10:14", {"8", "08"}},
        {"10:15", {"990", "990"}}, {"10:16", {"0", "000"}}, {"10:17", {"10", "010"}},
        {"10:18", {"20", "020"}},
        {"11:0", {"0", "0"}},   {"11:1", {"1", "1"}},   {"11:2", {"6", "6"}},
        {"11:3", {"1", "1"}},   {"11:4", {"2", "2"}},   {"11:5", {"11", "11"}},
        {"11:6", {"22", "22"}}, {"11:7", {"33", "33"}}, {"11:8", {"44", "44"}},
        {"11:9", {"55", "55"}}, {"11:10", {"10", "10"}}, {"11:11", {"21", "21"}},
        {"11:12", {"16", "16"}}, {"11:13", {"11", "11"}}, {"11:14", {"22", "22"}},
        {"11:15", {"21", "021"}}, {"11:16", {"32", "032"}}, {"11:17", {"43", "043"}},
        {"11:18", {"54", "054"}},
        {"12:0", {"0", "0"}},   {"12:1", {"2", "2"}},   {"12:2", {"8", "8"}},
        {"12:3", {"4", "4"}},   {"12:4", {"0", "0"}},   {"12:5", {"32", "32"}},
        {"12:6", {"44", "44"}}, {"12:7", {"40", "40"}}, {"12:8", {"52", "52"}},
        {"12:9", {"64", "64"}}, {"12:10", {"20", "20"}}, {"12:11", {"32", "32"}},
        {"12:12", {"28", "28"}}, {"12:13", {"24", "24"}}, {"12:14", {"20", "20"}},
        {"12:15", {"52", "052"}}, {"12:16", {"904", "904"}}, {"12:17", {"900", "900"}},
        {"12:18", {"912", "912"}},
        {"13:0", {"0", "0"}},   {"13:1", {"3", "3"}},   {"13:2", {"10", "10"}},
        {"13:3", {"23", "23"}}, {"13:4", {"20", "20"}}, {"13:5", {"53", "53"}},
        {"13:6", {"50", "50"}}, {"13:7", {"63", "63"}}, {"13:8", {"60", "60"}},
        {"13:9", {"73", "73"}}, {"13:10", {"30", "30"}}, {"13:11", {"43", "43"}},
        {"13:12", {"40", "40"}}, {"13:13", {"53", "53"}}, {"13:14", {"50", "50"}},
        {"13:15", {"923", "923"}}, {"13:16", {"920", "920"}}, {"13:17", {"933", "933"}},
        {"13:18", {"930", "930"}},
        {"14:0", {"0", "0"}},   {"14:1", {"4", "4"}},   {"14:2", {"12", "12"}},
        {"14:3", {"10", "10"}}, {"14:4", {"24", "24"}}, {"14:5", {"42", "42"}},
        {"14:6", {"40", "40"}}, {"14:7", {"54", "54"}}, {"14:8", {"68", "68"}},
        {"14:9", {"82", "82"}}, {"14:10", {"40", "40"}}, {"14:11", {"54", "54"}},
        {"14:12", {"52", "52"}}, {"14:13", {"50", "50"}}, {"14:14", {"4", "4"}},
        {"14:15", {"922", "922"}}, {"14:16", {"920", "920"}}, {"14:17", {"934", "934"}},
        {"14:18", {"948", "948"}},
    };
    std::map<std::string, StructuralHexDecimalProduct> output;
    for (const auto& [key, vd] : entries) {
      StructuralHexDecimalProduct product;
      product.value = vd.first;
      product.display = vd.second;
      output.emplace(key, product);
    }
    return output;
  }();
  return table;
}

std::optional<StructuralHexDecimalProduct> structural_hex_digit_times_decimal_product(
    int left_digit, const std::string& right) {
  const auto& table = structural_hex_digit_times_decimal_table();
  const auto it = table.find(std::to_string(left_digit) + ":" + right);
  return it == table.end() ? std::nullopt : std::optional<StructuralHexDecimalProduct>{it->second};
}

std::optional<StructuralHexDecimalProduct> decimal_times_structural_hex_digit_product(
    const std::string& left, int right_digit) {
  if (structural_hex_decimal_operands().count(left) == 0)
    return std::nullopt;
  if (right_digit == 10 || right_digit == 11 || right_digit == 12 || right_digit == 13) {
    const std::optional<std::string> value =
        exact_decimal_to_normalized(BigInt::mul(bigint_from_nonneg_decimal(left), BigInt::from_i64(10)), 0);
    if (!value.has_value())
      return std::nullopt;
    StructuralHexDecimalProduct out;
    out.value = *value;
    out.display = *value;
    return out;
  }
  if (right_digit == 14)
    return StructuralHexDecimalProduct{"0", std::string{"0"}, std::nullopt};
  return std::nullopt;
}

std::optional<StructuralHexDecimalProduct> structural_hex_digit_times_structural_hex_digit_product(
    int left_digit, int right_digit) {
  if (!is_verified_arithmetic_hex_digit(left_digit) || !is_verified_arithmetic_hex_digit(right_digit))
    return std::nullopt;
  if (right_digit == 14)
    return StructuralHexDecimalProduct{"0", std::string{"0"}, std::nullopt};
  const std::optional<std::string> value =
      exact_decimal_to_normalized(BigInt::from_i64((left_digit - 10) * 10), 0);
  if (!value.has_value())
    return std::nullopt;
  StructuralHexDecimalProduct out;
  out.value = *value;
  out.display = left_digit == 10 ? std::string{"00"} : *value;
  return out;
}

const std::map<std::string, StructuralHexDecimalProduct>& structural_hex_digit_divide_decimal_table() {
  static const std::map<std::string, StructuralHexDecimalProduct> table = structural_hex_decimal_product_table({
      {"10:1", "0,"},   {"10:2", "5,"},   {"10:3", "3,3333333"}, {"10:4", "2,5"},
      {"10:5", "2,"},   {"10:6", "1,6666666"}, {"10:7", "1,4285714"}, {"10:8", "1,25"},
      {"10:9", "1,1111111"}, {"10:10", "0,-01"}, {"10:11", "9,090909-01"}, {"10:12", "8,3333333-01"},
      {"10:13", "7,6923076-01"}, {"10:14", "7,1428571-01"}, {"10:15", "6,6666666-01"}, {"10:16", "6,25-01"},
      {"10:17", "5,8823529-01"}, {"10:18", "5,5555555-01"},
      {"11:1", "1,"},   {"11:2", "5,5"},  {"11:3", "3,6666666"}, {"11:4", "2,75"},
      {"11:5", "2,2"},  {"11:6", "1,8333333"}, {"11:7", "1,5714285"}, {"11:8", "1,375"},
      {"11:9", "1,2222222"}, {"11:10", "1,-01"}, {"11:11", "0,-01"}, {"11:12", "9,1666666-01"},
      {"11:13", "8,4615384-01"}, {"11:14", "7,8571428-01"}, {"11:15", "7,3333333-01"}, {"11:16", "6,875-01"},
      {"11:17", "6,4705882-01"}, {"11:18", "6,1111111-01"},
      {"12:1", "2,"},   {"12:2", "6,"},   {"12:3", "4,"},   {"12:4", "3,"},
      {"12:5", "2,4"},  {"12:6", "2,"},   {"12:7", "1,7142857"}, {"12:8", "1,5"},
      {"12:9", "1,3333333"}, {"12:10", "2,-01"}, {"12:11", "0,9090909-01"}, {"12:12", "0,-01"},
      {"12:13", "9,2307692-01"}, {"12:14", "8,5714285-01"}, {"12:15", "8,-01"}, {"12:16", "7,5-01"},
      {"12:17", "7,0588235-01"}, {"12:18", "6,6666666-01"},
      {"13:1", "3,"},   {"13:2", "6,5"},  {"13:3", "4,3333333"}, {"13:4", "3,25"},
      {"13:5", "2,6"},  {"13:6", "2,1666666"}, {"13:7", "1,8571428"}, {"13:8", "1,625"},
      {"13:9", "1,4444444"}, {"13:10", "3,-01"}, {"13:11", "1,8181818-01"}, {"13:12", "0,8333333-01"},
      {"13:13", "0,-01"}, {"13:14", "9,2857142-01"}, {"13:15", "8,6666666-01"}, {"13:16", "8,125-01"},
      {"13:17", "7,6470588-01"}, {"13:18", "7,2222222-01"},
      {"14:1", "4,"},   {"14:2", "7,"},   {"14:3", "4,6666666"}, {"14:4", "3,5"},
      {"14:5", "2,8"},  {"14:6", "2,3333333"}, {"14:7", "2,"},   {"14:8", "1,75"},
      {"14:9", "1,5555555"}, {"14:10", "4,-01"}, {"14:11", "2,7272727-01"}, {"14:12", "1,6666666-01"},
      {"14:13", "0,7692307-01"}, {"14:14", "0,-01"}, {"14:15", "9,3333333-01"}, {"14:16", "8,75-01"},
      {"14:17", "8,2352941-01"}, {"14:18", "7,7777777-01"},
  });
  return table;
}

std::optional<StructuralHexDecimalProduct> structural_hex_digit_divide_decimal_product(
    int left_digit, const std::string& right) {
  if (!is_verified_arithmetic_hex_digit(left_digit))
    return std::nullopt;
  const auto& table = structural_hex_digit_divide_decimal_table();
  const auto it = table.find(std::to_string(left_digit) + ":" + right);
  return it == table.end() ? std::nullopt : std::optional<StructuralHexDecimalProduct>{it->second};
}

const std::map<std::string, StructuralHexDecimalProduct>& decimal_divide_structural_hex_digit_table() {
  static const std::map<std::string, StructuralHexDecimalProduct> table = [] {
    const auto exp = [](const std::string& value, const std::string& mantissa,
                        const std::string& exponent) {
      StructuralHexDecimalProduct p;
      p.value = value;
      p.displayShape = decimal_exponent_shape_fact(mantissa, exponent);
      return p;
    };
    std::map<std::string, StructuralHexDecimalProduct> output;
    output.emplace("0:10", exp("0.9090909", "9.090909", "-1"));
    output.emplace("0:11", exp("0.99099099", "9.9099099", "-1"));
    output.emplace("0:12", exp("0.99099099", "9.9099099", "-1"));
    output.emplace("0:13", exp("0.99099099", "9.9099099", "-1"));
    output.emplace("0:14", exp("0.99099099", "9.9099099", "-1"));
    output.emplace("1:11", exp("0.9099099", "9.099099", "-1"));
    output.emplace("1:12", exp("0.99099099", "9.9099099", "-1"));
    output.emplace("1:13", exp("0.99099099", "9.9099099", "-1"));
    output.emplace("1:14", exp("0.99099099", "9.9099099", "-1"));
    output.emplace("2:11", exp("0.84444443", "8.4444443", "-1"));
    output.emplace("2:12", exp("0.9099099", "9.099099", "-1"));
    output.emplace("2:13", exp("0.99099099", "9.9099099", "-1"));
    output.emplace("2:14", exp("0.99099099", "9.9099099", "-1"));
    output.emplace("3:11", exp("0.64444443", "6.4444443", "-1"));
    output.emplace("3:13", exp("0.9099099", "9.099099", "-1"));
    output.emplace("3:14", exp("0.99099099", "9.9099099", "-1"));
    output.emplace("4:11", exp("0.44444443", "4.4444443", "-1"));
    output.emplace("4:13", exp("0.8", "8", "-1"));
    output.emplace("4:14", exp("0.9099099", "9.099099", "-1"));
    output.emplace("5:11", exp("0.24444443", "2.4444443", "-1"));
    output.emplace("5:13", exp("0", "0", "-1"));
    output.emplace("5:14", exp("0.02929292", "2.929292", "-2"));
    output.emplace("6:11", exp("0.64444443", "6.4444443", "-1"));
    output.emplace("6:13", exp("0.2", "2", "-1"));
    output.emplace("6:14", exp("0.32929292", "3.2929292", "-1"));
    output.emplace("7:11", exp("0.44444443", "4.4444443", "-1"));
    output.emplace("7:13", exp("0.4", "4", "-1"));
    output.emplace("7:14", exp("0.62929292", "6.2929292", "-1"));
    output.emplace("8:11", exp("0.24444443", "2.4444443", "-1"));
    output.emplace("8:13", StructuralHexDecimalProduct{"0", std::string{"0"}, std::nullopt});
    output.emplace("8:14", exp("0.92929292", "9.2929292", "-1"));
    output.emplace("9:10", exp("0.8990909", "8.990909", "-1"));
    output.emplace("9:11", exp("0.04444443", "0.4444443", "-1"));
    output.emplace("9:13", exp("0.2", "2", "-1"));
    output.emplace("9:14", exp("0.22929292", "2.2929292", "-1"));
    const auto disp = [](const std::string& value) {
      return StructuralHexDecimalProduct{value, value, std::nullopt};
    };
    output.emplace("10:11", disp("9.099099"));
    output.emplace("10:12", disp("9.9099099"));
    output.emplace("10:13", disp("9.9099099"));
    output.emplace("10:14", disp("9.9099099"));
    output.emplace("11:11", disp("9.099099"));
    output.emplace("11:13", disp("9.8"));
    output.emplace("11:14", disp("9.0292929"));
    output.emplace("12:11", disp("9.099099"));
    output.emplace("12:13", disp("0"));
    output.emplace("12:14", disp("9.3292929"));
    output.emplace("13:11", disp("9.099099"));
    output.emplace("13:13", disp("0.2"));
    output.emplace("13:14", disp("9.6292929"));
    output.emplace("14:11", disp("9.099099"));
    output.emplace("14:13", disp("0.4"));
    output.emplace("14:14", disp("9.9292929"));
    output.emplace("15:11", disp("9"));
    output.emplace("15:13", disp("9"));
    output.emplace("15:14", disp("0.2292929"));
    output.emplace("16:11", disp("9.2525252"));
    output.emplace("16:13", disp("9.2"));
    output.emplace("16:14", disp("0.5292929"));
    output.emplace("17:11", disp("9.3434343"));
    output.emplace("17:13", disp("9.4"));
    output.emplace("17:14", disp("9.2292929"));
    output.emplace("18:11", disp("9.4343434"));
    output.emplace("18:13", disp("9.6"));
    output.emplace("18:14", disp("9.5292929"));
    return output;
  }();
  return table;
}

std::optional<StructuralHexDecimalProduct> decimal_divide_structural_hex_digit_product(
    const std::string& left, int right_digit) {
  if (!is_verified_arithmetic_hex_digit(right_digit))
    return std::nullopt;
  const auto& table = decimal_divide_structural_hex_digit_table();
  const auto it = table.find(left + ":" + std::to_string(right_digit));
  return it == table.end() ? std::nullopt : std::optional<StructuralHexDecimalProduct>{it->second};
}

const std::map<std::string, std::string>& structural_hex_digit_divide_structural_hex_digit_table() {
  static const std::map<std::string, std::string> table = {
      {"10:10", "1"},   {"10:11", "0.84444443"}, {"10:13", "0.4"},   {"10:14", "0.52929292"},
      {"11:10", "1.1"}, {"11:11", "1"},   {"11:13", "0.6"},   {"11:14", "0.22929292"},
      {"12:10", "1.2"}, {"12:11", "1.2525252"}, {"12:12", "1"},   {"12:13", "0.8"},
      {"12:14", "0.52929292"}, {"13:10", "1.3"}, {"13:11", "1.3434343"}, {"13:12", "1.23"},
      {"13:13", "1"},   {"13:14", "0.82929292"}, {"14:10", "1.4"}, {"14:11", "1.4343434"},
      {"14:12", "1.3"}, {"14:13", "1.2"}, {"14:14", "1"},
  };
  return table;
}

std::optional<StructuralHexDecimalProduct> structural_hex_digit_divide_structural_hex_digit_product(
    int left_digit, int right_digit) {
  if (!is_verified_arithmetic_hex_digit(left_digit) || !is_verified_arithmetic_hex_digit(right_digit))
    return std::nullopt;
  const auto& table = structural_hex_digit_divide_structural_hex_digit_table();
  const auto it = table.find(std::to_string(left_digit) + ":" + std::to_string(right_digit));
  if (it == table.end())
    return std::nullopt;
  const std::optional<X2ShapeFact> display_shape = exact_decimal_display_shape_fact(it->second);
  if (!display_shape.has_value())
    return std::nullopt;
  StructuralHexDecimalProduct out;
  out.value = it->second;
  out.displayShape = display_shape;
  return out;
}

// --- scaled-exponent operands ----------------------------------------------
std::optional<int> structural_hex_exponent_shift_from_minus_two(const std::string& exponent_raw) {
  const std::optional<std::string> exponent = canonical_exponent_shape_raw(exponent_raw);
  if (!exponent.has_value())
    return std::nullopt;
  int value = 0;
  if (!exponent->empty()) {
    const std::optional<int> parsed = parse_small_exponent_int(*exponent);
    if (!parsed.has_value())
      return std::nullopt;
    value = *parsed;
  }
  return value >= -3 && value <= 9 ? std::optional<int>{value + 2} : std::nullopt;
}

bool is_verified_scaled_structural_hex_exponent(const std::string& exponent_raw) {
  return structural_hex_exponent_shift_from_minus_two(exponent_raw).has_value();
}

std::optional<int> structural_hex_exponent_zero_digit(const StructuralHexExponentOperand& operand) {
  if (!is_verified_arithmetic_hex_digit(operand.digit))
    return std::nullopt;
  const std::optional<std::string> exponent = canonical_exponent_shape_raw(operand.exponent);
  return (exponent.has_value() && *exponent == "0") ? std::optional<int>{operand.digit} : std::nullopt;
}

struct StructuralHexExponentAddSubOperand {
  BigInt num;
  int scale = 0;
};

std::optional<StructuralHexExponentAddSubOperand> structural_hex_exponent_add_sub_decimal_operand(
    const StructuralHexExponentOperand& operand) {
  if (!is_verified_arithmetic_hex_digit(operand.digit))
    return std::nullopt;
  const std::optional<std::string> exponent = canonical_exponent_shape_raw(operand.exponent);
  if (!exponent.has_value())
    return std::nullopt;
  const std::optional<int> exponent_value = parse_small_exponent_int(*exponent);
  if (!exponent_value.has_value() || *exponent_value < -9 || *exponent_value > -1)
    return std::nullopt;
  const std::string tail = operand.digit == 10 ? std::string{"1"} : std::to_string(operand.digit);
  StructuralHexExponentAddSubOperand out;
  out.num = bigint_from_nonneg_decimal(tail);
  out.scale = -*exponent_value + static_cast<int>(tail.size()) - 2;
  return out;
}

std::string pad_positive_hex_exponent_display(int value, bool min_two_digits) {
  const std::string display = std::to_string(value);
  return (min_two_digits && value >= 0 && value < 10) ? "0" + display : display;
}

std::optional<StructuralHexDecimalProduct> structural_hex_exponent_plus_one_add_sub_decimal_product(
    const StructuralHexExponentOperand& left, const std::string& right, const std::string& operation) {
  const std::optional<int> right_value = verified_decimal_operand_value(right);
  if (!is_verified_arithmetic_hex_digit(left.digit) || left.exponent != "1" || !right_value.has_value())
    return std::nullopt;
  const int base = (left.digit - 10) * 10;
  if (operation == "plus") {
    const int raw = *right_value < 10 ? base + *right_value : 100 + base + *right_value;
    return structural_hex_decimal_product_from_mk61_display(
        pad_positive_hex_exponent_display(raw, *right_value < 10));
  }
  if (*right_value == 0)
    return structural_hex_decimal_product_from_mk61_display(pad_positive_hex_exponent_display(base, true));
  if (*right_value < 10)
    return structural_hex_decimal_product_from_mk61_display(
        std::to_string(-((16 - left.digit) * 10 + *right_value)));
  return structural_hex_decimal_product_from_mk61_display(
      pad_positive_hex_exponent_display((100 + base - *right_value) % 100, true));
}

std::optional<StructuralHexDecimalProduct> structural_hex_exponent_add_sub_decimal_product(
    const StructuralHexExponentOperand& left, const std::string& right, const std::string& operation) {
  const std::optional<StructuralHexDecimalProduct> plus_one =
      structural_hex_exponent_plus_one_add_sub_decimal_product(left, right, operation);
  if (plus_one.has_value())
    return plus_one;
  const std::optional<StructuralHexExponentAddSubOperand> operand =
      structural_hex_exponent_add_sub_decimal_operand(left);
  if (!operand.has_value() || structural_hex_decimal_operands().count(right) == 0)
    return std::nullopt;
  const BigInt right_num = BigInt::mul(bigint_from_nonneg_decimal(right), big_pow10(operand->scale));
  const BigInt result =
      operation == "plus" ? BigInt::add(operand->num, right_num) : BigInt::sub(operand->num, right_num);
  return structural_hex_decimal_product_from_exact(result, operand->scale);
}

std::optional<StructuralHexDecimalProduct> structural_hex_exponent_plus_decimal_product(
    const StructuralHexExponentOperand& left, const std::string& right) {
  const std::optional<int> digit = structural_hex_exponent_zero_digit(left);
  if (digit.has_value())
    return structural_hex_digit_plus_decimal_product(*digit, right);
  return structural_hex_exponent_add_sub_decimal_product(left, right, "plus");
}

std::optional<StructuralHexDecimalProduct> structural_hex_exponent_minus_decimal_product(
    const StructuralHexExponentOperand& left, const std::string& right) {
  const std::optional<int> digit = structural_hex_exponent_zero_digit(left);
  if (digit.has_value())
    return structural_hex_digit_minus_decimal_product(*digit, right);
  return structural_hex_exponent_add_sub_decimal_product(left, right, "minus");
}

std::optional<StructuralHexDecimalProduct> decimal_plus_structural_hex_exponent_plus_one_product(
    const std::string& left, const StructuralHexExponentOperand& right) {
  const std::optional<int> left_value = verified_decimal_operand_value(left);
  if (!is_verified_arithmetic_hex_digit(right.digit) || right.exponent != "1" || !left_value.has_value())
    return std::nullopt;
  return structural_hex_decimal_product_from_mk61_display(
      pad_positive_hex_exponent_display((right.digit - 10) * 10 + *left_value, true));
}

std::optional<StructuralHexDecimalProduct> decimal_plus_structural_hex_exponent_product_from_pinned(
    const std::string& left, const StructuralHexExponentOperand& right) {
  const std::optional<StructuralHexDecimalProduct> plus_one =
      decimal_plus_structural_hex_exponent_plus_one_product(left, right);
  if (plus_one.has_value())
    return plus_one;
  return structural_hex_exponent_add_sub_decimal_product(right, left, "plus");
}

std::optional<StructuralHexDecimalProduct> decimal_plus_structural_hex_exponent_product(
    const std::string& left, const StructuralHexExponentOperand& right) {
  const std::optional<int> digit = structural_hex_exponent_zero_digit(right);
  if (digit.has_value())
    return decimal_plus_structural_hex_digit_product(left, *digit);
  return decimal_plus_structural_hex_exponent_product_from_pinned(left, right);
}

std::optional<StructuralHexDecimalProduct> decimal_minus_structural_hex_exponent_plus_one_product(
    const std::string& left, const StructuralHexExponentOperand& right) {
  const std::optional<int> left_value = verified_decimal_operand_value(left);
  if (!is_verified_arithmetic_hex_digit(right.digit) || right.exponent != "1" || !left_value.has_value())
    return std::nullopt;
  if (right.digit == 10)
    return structural_hex_decimal_product_from_mk61_display(std::to_string(*left_value - 100));
  if (right.digit == 11 && *left_value >= 10)
    return structural_hex_decimal_product_from_mk61_display(std::to_string(*left_value - 110));
  return structural_hex_decimal_product_from_mk61_display(
      std::to_string(*left_value - (right.digit - 10) * 10));
}

std::optional<StructuralHexDecimalProduct> decimal_minus_structural_hex_exponent_product_from_pinned(
    const std::string& left, const StructuralHexExponentOperand& right) {
  const std::optional<StructuralHexDecimalProduct> plus_one =
      decimal_minus_structural_hex_exponent_plus_one_product(left, right);
  if (plus_one.has_value())
    return plus_one;
  if (structural_hex_decimal_operands().count(left) == 0)
    return std::nullopt;
  const std::optional<StructuralHexExponentAddSubOperand> operand =
      structural_hex_exponent_add_sub_decimal_operand(right);
  if (!operand.has_value())
    return std::nullopt;
  if (right.digit == 10) {
    const BigInt result =
        BigInt::sub(BigInt::mul(bigint_from_nonneg_decimal(left), big_pow10(operand->scale)), operand->num);
    return structural_hex_decimal_product_from_exact(result, operand->scale);
  }
  const int scale = operand->scale;
  const BigInt result = BigInt::add(BigInt::mul(bigint_from_nonneg_decimal(left), big_pow10(scale)),
                                    BigInt::from_i64(16 - right.digit));
  return structural_hex_decimal_product_from_exact(result, scale);
}

std::optional<StructuralHexDecimalProduct> decimal_minus_structural_hex_exponent_product(
    const std::string& left, const StructuralHexExponentOperand& right) {
  const std::optional<int> digit = structural_hex_exponent_zero_digit(right);
  if (digit.has_value())
    return decimal_minus_structural_hex_digit_product(left, *digit);
  return decimal_minus_structural_hex_exponent_product_from_pinned(left, right);
}

std::optional<StructuralHexDecimalProduct> structural_hex_exponent_times_decimal_product(
    const StructuralHexExponentOperand& left, const std::string& right) {
  if (!is_verified_arithmetic_hex_digit(left.digit) ||
      !is_verified_scaled_structural_hex_exponent(left.exponent))
    return std::nullopt;
  return shift_structural_hex_decimal_product(structural_hex_digit_times_decimal_product(left.digit, right),
                                              left.exponent, "raw-display");
}

std::optional<StructuralHexDecimalProduct> decimal_times_structural_hex_exponent_product(
    const std::string& left, const StructuralHexExponentOperand& right) {
  if (!is_verified_scaled_structural_hex_exponent(right.exponent) ||
      structural_hex_decimal_operands().count(left) == 0)
    return std::nullopt;
  if (right.digit >= 10 && right.digit <= 13) {
    const std::optional<int> scale_shift = structural_hex_exponent_shift_from_minus_two(right.exponent);
    if (!scale_shift.has_value())
      return std::nullopt;
    return shift_structural_hex_decimal_product(
        structural_hex_decimal_product_from_exact(bigint_from_nonneg_decimal(left), 1),
        std::to_string(*scale_shift), "exact-display");
  }
  if (right.digit == 14)
    return structural_hex_decimal_product_from_mk61_display("0,");
  return std::nullopt;
}

const std::map<std::string, StructuralHexDecimalProduct>& decimal_divide_structural_hex_exponent_table() {
  static const std::map<std::string, StructuralHexDecimalProduct> table = structural_hex_decimal_product_table({
      {"0:10", "90,90909"}, {"9:10", "89,90909"}, {"10:11", "909,9099"}, {"11:11", "909,9099"},
      {"12:11", "909,9099"}, {"13:11", "909,9099"}, {"14:11", "909,9099"}, {"15:11", "900,"},
      {"17:11", "934,34343"}, {"0:11", "99,099099"}, {"1:11", "90,99099"}, {"2:11", "84,444443"},
      {"3:11", "64,444443"}, {"4:11", "44,444443"}, {"5:11", "24,444443"}, {"6:11", "64,444443"},
      {"7:11", "44,444443"}, {"8:11", "24,444443"}, {"9:11", "04,444443"}, {"16:11", "925,25252"},
      {"18:11", "943,43434"}, {"0:12", "99,099099"}, {"1:12", "99,099099"}, {"2:12", "90,99099"},
      {"10:12", "990,99099"}, {"0:13", "99,099099"}, {"1:13", "99,099099"}, {"2:13", "99,099099"},
      {"3:13", "90,99099"}, {"4:13", "80,"}, {"5:13", "00,"}, {"6:13", "20,"},
      {"7:13", "40,"}, {"8:13", "0,"}, {"9:13", "20,"}, {"10:13", "990,99099"},
      {"11:13", "980,"}, {"12:13", "000,"}, {"13:13", "020,"}, {"14:13", "040,"},
      {"15:13", "900,"}, {"16:13", "920,"}, {"17:13", "940,"}, {"18:13", "960,"},
      {"0:14", "99,099099"}, {"1:14", "99,099099"}, {"2:14", "99,099099"}, {"3:14", "99,099099"},
      {"4:14", "90,99099"}, {"5:14", "2,929292"}, {"6:14", "32,929292"}, {"7:14", "62,929292"},
      {"8:14", "92,929292"}, {"9:14", "22,929292"}, {"10:14", "990,99099"}, {"11:14", "902,92929"},
      {"12:14", "932,92929"}, {"13:14", "962,92929"}, {"14:14", "992,92929"}, {"15:14", "022,92929"},
      {"16:14", "052,92929"}, {"17:14", "922,92929"}, {"18:14", "952,92929"},
  });
  return table;
}

std::optional<StructuralHexDecimalProduct> structural_hex_exponent_divide_decimal_product(
    const StructuralHexExponentOperand& left, const std::string& right) {
  if (!is_verified_arithmetic_hex_digit(left.digit) ||
      !is_verified_scaled_structural_hex_exponent(left.exponent))
    return std::nullopt;
  return shift_structural_hex_decimal_product(structural_hex_digit_divide_decimal_product(left.digit, right),
                                              left.exponent, "exact-display");
}

std::optional<StructuralHexDecimalProduct> decimal_divide_structural_hex_exponent_product(
    const std::string& left, const StructuralHexExponentOperand& right) {
  if (!is_verified_scaled_structural_hex_exponent(right.exponent) ||
      !is_verified_arithmetic_hex_digit(right.digit))
    return std::nullopt;
  const std::optional<int> scale_shift = structural_hex_exponent_shift_from_minus_two(right.exponent);
  if (!scale_shift.has_value())
    return std::nullopt;
  const auto& table = decimal_divide_structural_hex_exponent_table();
  const auto it = table.find(left + ":" + std::to_string(right.digit));
  const std::optional<StructuralHexDecimalProduct> product =
      it == table.end() ? std::nullopt : std::optional<StructuralHexDecimalProduct>{it->second};
  return shift_structural_hex_decimal_product(product, std::to_string(-*scale_shift), "raw-display");
}

// --- carry-normalized integer operands -------------------------------------
std::optional<StructuralHexDecimalProduct> structural_hex_carry_normalized_binary_decimal_product(
    const StructuralHexDecimalProduct& left, const std::string& right, int opcode) {
  static const std::regex digits_only(R"(^[0-9]+$)");
  if (!std::regex_match(left.value, digits_only))
    return std::nullopt;
  if (!parse_exact_decimal(right).has_value())
    return std::nullopt;
  return structural_hex_decimal_product_from_concrete_decimal_value(
      concrete_decimal_binary_value(opcode, left.value, right));
}

std::optional<StructuralHexDecimalProduct> decimal_binary_structural_hex_carry_normalized_product(
    const std::string& left, const StructuralHexDecimalProduct& right, int opcode) {
  static const std::regex digits_only(R"(^[0-9]+$)");
  if (!std::regex_match(right.value, digits_only))
    return std::nullopt;
  if (!parse_exact_decimal(left).has_value())
    return std::nullopt;
  return structural_hex_decimal_product_from_concrete_decimal_value(
      concrete_decimal_binary_value(opcode, left, right.value));
}

std::optional<StructuralHexDecimalProduct> structural_hex_carry_normalized_plus_decimal_product(
    const StructuralHexDecimalProduct& left, const std::string& right) {
  return structural_hex_carry_normalized_binary_decimal_product(left, right, 0x10);
}

std::string structural_hex_carry_normalized_operand_key(const StructuralHexDecimalProduct& product) {
  return "carry:" + product.value + ":" + product.display.value_or("") + ":" +
         product.displayShape.value_or("");
}

std::optional<std::string> structural_hex_carry_normalized_integer_display(const std::string& raw) {
  const std::vector<std::string> digits = shape_digits(raw);
  if (digits.empty() || digits.size() > 8 || digits.size() != utf8_codepoints(raw).size())
    return std::nullopt;
  int carry = 0;
  std::string display;
  for (int index = static_cast<int>(digits.size()) - 1; index >= 0; index -= 1) {
    const std::optional<int> nibble = structural_hex_nibble_value(digits.at(static_cast<std::size_t>(index)));
    if (!nibble.has_value() || *nibble > 14)
      return std::nullopt;
    const int value = *nibble + carry;
    display = std::to_string(value % 10) + display;
    carry = value >= 10 ? 1 : 0;
  }
  if (carry > 0)
    display = std::to_string(carry) + display;
  return display.size() <= 8 ? std::optional<std::string>{display} : std::nullopt;
}

std::optional<StructuralHexDecimalProduct> structural_hex_carry_normalized_integer_product(
    const X2ShapeFact& fact) {
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
  if (model.kind != X2ShapeModelKind::Mantissa || !model.mantissa.has_value() ||
      model.mantissa->radix != X2MantissaRadix::Hex || model.mantissa->sign != "" ||
      model.mantissa->hasDecimalPoint || model.mantissa->digits.size() <= 1 ||
      !has_structural_non_decimal_digit(model.mantissa->canonical))
    return std::nullopt;
  const std::optional<std::string> display =
      structural_hex_carry_normalized_integer_display(model.mantissa->canonical);
  if (!display.has_value())
    return std::nullopt;
  const std::optional<std::string> value = normalize_plain_decimal(*display);
  if (!value.has_value())
    return std::nullopt;
  StructuralHexDecimalProduct out;
  out.value = *value;
  out.display = *display;
  return out;
}

std::set<X2ShapeFact> structural_hex_carry_source_shape_facts(const X2ShapeSet* shapes) {
  const std::set<X2ShapeFact> canonical = canonical_structural_shape_facts(shapes);
  std::set<X2ShapeFact> restored_from_exponent;
  for (const X2ShapeFact& fact : canonical) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind != X2ShapeModelKind::ExponentEntry || !model.closedStructuralMantissa.has_value())
      continue;
    const std::optional<X2ShapeFact> restored = x2_mantissa_shape_fact_from_model(*model.closedStructuralMantissa);
    if (restored.has_value())
      restored_from_exponent.insert(*restored);
  }
  std::set<X2ShapeFact> output;
  for (const X2ShapeFact& fact : canonical) {
    if (restored_from_exponent.count(fact) == 0)
      output.insert(fact);
  }
  return output;
}

std::vector<StructuralHexDecimalProduct> structural_hex_carry_normalized_integer_products(
    const X2ShapeSet* shapes) {
  std::map<std::string, StructuralHexDecimalProduct> output;
  for (const X2ShapeFact& fact : structural_hex_carry_source_shape_facts(shapes)) {
    const std::optional<StructuralHexDecimalProduct> product = structural_hex_carry_normalized_integer_product(fact);
    if (!product.has_value())
      continue;
    output.emplace(product->value + ":" + product->display.value_or("") + ":" +
                       product->displayShape.value_or(""),
                   *product);
  }
  std::vector<StructuralHexDecimalProduct> values;
  for (const auto& [key, product] : output)
    values.push_back(product);
  return values;
}

// --- single hex-digit / scaled-exponent operands ---------------------------
std::set<int> structural_single_hex_digit_values(const X2ShapeSet* shapes) {
  std::set<int> output;
  for (const X2ShapeFact& fact : structural_restore_shape_facts(canonical_structural_shape_facts(shapes))) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind != X2ShapeModelKind::Mantissa || !model.mantissa.has_value() ||
        model.mantissa->radix != X2MantissaRadix::Hex || model.mantissa->sign != "" ||
        model.mantissa->hasDecimalPoint || model.mantissa->digits.size() != 1)
      continue;
    const std::optional<int> value = structural_hex_nibble_value(model.mantissa->digits.at(0));
    if (value.has_value())
      output.insert(*value);
  }
  return output;
}

std::optional<StructuralHexExponentOperand> structural_single_hex_exponent_operand_from_shape_model(
    const X2ShapeDataModel& model) {
  if (model.kind == X2ShapeModelKind::ExponentEntry && model.mantissa.has_value() &&
      model.mantissa->radix == X2MantissaRadix::Hex && model.mantissa->sign == "" &&
      !model.mantissa->hasDecimalPoint && model.mantissa->digits.size() == 1) {
    const std::optional<int> digit = structural_hex_nibble_value(model.mantissa->digits.at(0));
    const std::optional<std::string> exponent = canonical_exponent_shape_raw(model.exponentRaw);
    if (!digit.has_value() || !exponent.has_value())
      return std::nullopt;
    return StructuralHexExponentOperand{*digit, *exponent};
  }
  if (model.kind != X2ShapeModelKind::Mantissa || !model.mantissa.has_value() ||
      model.mantissa->radix != X2MantissaRadix::Hex || model.mantissa->sign != "")
    return std::nullopt;
  // /^([A-FСГЕ])(0*)$/u  : a single hex letter followed by trailing zeros.
  const std::vector<std::string> points = utf8_codepoints(model.mantissa->canonical);
  if (!points.empty() && structural_is_letter_hex_square_digit(points.at(0))) {
    bool all_zeros = true;
    for (std::size_t index = 1; index < points.size(); ++index) {
      if (points.at(index) != "0") {
        all_zeros = false;
        break;
      }
    }
    if (all_zeros) {
      const int zeros = static_cast<int>(points.size()) - 1;
      if (zeros == 0)
        return std::nullopt;
      const std::optional<int> digit = structural_hex_nibble_value(points.at(0));
      if (!digit.has_value())
        return std::nullopt;
      return StructuralHexExponentOperand{*digit, std::to_string(zeros)};
    }
  }
  // /^(?:0)?\.(0*)([A-FСГЕ])$/u : optional leading 0, dot, zeros, single hex letter.
  std::size_t cursor = 0;
  if (!points.empty() && points.at(0) == "0")
    cursor = 1;
  if (cursor < points.size() && points.at(cursor) == ".") {
    cursor += 1;
    int leading_zeros = 0;
    while (cursor < points.size() && points.at(cursor) == "0") {
      leading_zeros += 1;
      cursor += 1;
    }
    if (cursor + 1 == points.size() && structural_is_letter_hex_square_digit(points.at(cursor))) {
      const std::optional<int> digit = structural_hex_nibble_value(points.at(cursor));
      if (!digit.has_value())
        return std::nullopt;
      return StructuralHexExponentOperand{*digit, "-" + std::to_string(leading_zeros + 1)};
    }
  }
  return std::nullopt;
}

std::vector<StructuralHexExponentOperand> structural_single_hex_exponent_operands(const X2ShapeSet* shapes) {
  std::map<std::string, StructuralHexExponentOperand> output;
  for (const X2ShapeFact& fact : canonical_structural_shape_facts(shapes)) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    const std::optional<StructuralHexExponentOperand> operand =
        structural_single_hex_exponent_operand_from_shape_model(model);
    if (!operand.has_value())
      continue;
    output.emplace(std::to_string(operand->digit) + ":" + operand->exponent, *operand);
  }
  std::vector<StructuralHexExponentOperand> values;
  for (const auto& [key, operand] : output)
    values.push_back(operand);
  return values;
}

std::vector<StructuralHexArithmeticOperand> structural_hex_arithmetic_operands(
    const X2ShapeSet* shapes, const X2ShapeSet* carry_shapes) {
  std::map<std::string, StructuralHexArithmeticOperand> output;
  for (const int digit : structural_single_hex_digit_values(shapes)) {
    StructuralHexArithmeticOperand operand;
    operand.kind = StructuralHexOperandKind::Digit;
    operand.digit = digit;
    output.emplace("digit:" + std::to_string(digit), operand);
  }
  for (const StructuralHexExponentOperand& exponent : structural_single_hex_exponent_operands(shapes)) {
    StructuralHexArithmeticOperand operand;
    operand.kind = StructuralHexOperandKind::Exponent;
    operand.digit = exponent.digit;
    operand.exponent = exponent.exponent;
    output.emplace("exponent:" + std::to_string(exponent.digit) + ":" + exponent.exponent, operand);
  }
  for (const StructuralHexDecimalProduct& product : structural_hex_carry_normalized_integer_products(carry_shapes)) {
    StructuralHexArithmeticOperand operand;
    operand.kind = StructuralHexOperandKind::CarryNormalized;
    operand.product = product;
    output.emplace(structural_hex_carry_normalized_operand_key(product), operand);
  }
  std::vector<StructuralHexArithmeticOperand> values;
  for (const auto& [key, operand] : output)
    values.push_back(operand);
  return values;
}

// --- operand dispatchers ----------------------------------------------------
std::optional<StructuralHexDecimalProduct> structural_hex_operand_plus_decimal_product(
    const StructuralHexArithmeticOperand& left, const std::string& right) {
  switch (left.kind) {
    case StructuralHexOperandKind::Digit:
      return structural_hex_digit_plus_decimal_product(left.digit, right);
    case StructuralHexOperandKind::Exponent:
      return structural_hex_exponent_plus_decimal_product({left.digit, left.exponent}, right);
    case StructuralHexOperandKind::CarryNormalized:
      return structural_hex_carry_normalized_plus_decimal_product(left.product, right);
  }
  return std::nullopt;
}

std::optional<StructuralHexDecimalProduct> decimal_plus_structural_hex_operand_product(
    const std::string& left, const StructuralHexArithmeticOperand& right) {
  switch (right.kind) {
    case StructuralHexOperandKind::Digit:
      return decimal_plus_structural_hex_digit_product(left, right.digit);
    case StructuralHexOperandKind::Exponent:
      return decimal_plus_structural_hex_exponent_product(left, {right.digit, right.exponent});
    case StructuralHexOperandKind::CarryNormalized:
      return structural_hex_carry_normalized_plus_decimal_product(right.product, left);
  }
  return std::nullopt;
}

std::optional<StructuralHexDecimalProduct> structural_hex_operand_minus_decimal_product(
    const StructuralHexArithmeticOperand& left, const std::string& right) {
  switch (left.kind) {
    case StructuralHexOperandKind::Digit:
      return structural_hex_digit_minus_decimal_product(left.digit, right);
    case StructuralHexOperandKind::Exponent:
      return structural_hex_exponent_minus_decimal_product({left.digit, left.exponent}, right);
    case StructuralHexOperandKind::CarryNormalized:
      return structural_hex_carry_normalized_binary_decimal_product(left.product, right, 0x11);
  }
  return std::nullopt;
}

std::optional<StructuralHexDecimalProduct> decimal_minus_structural_hex_operand_product(
    const std::string& left, const StructuralHexArithmeticOperand& right) {
  switch (right.kind) {
    case StructuralHexOperandKind::Digit:
      return decimal_minus_structural_hex_digit_product(left, right.digit);
    case StructuralHexOperandKind::Exponent:
      return decimal_minus_structural_hex_exponent_product(left, {right.digit, right.exponent});
    case StructuralHexOperandKind::CarryNormalized:
      return decimal_binary_structural_hex_carry_normalized_product(left, right.product, 0x11);
  }
  return std::nullopt;
}

std::optional<StructuralHexDecimalProduct> structural_hex_operand_times_decimal_product(
    const StructuralHexArithmeticOperand& left, const std::string& right) {
  switch (left.kind) {
    case StructuralHexOperandKind::Digit:
      return structural_hex_digit_times_decimal_product(left.digit, right);
    case StructuralHexOperandKind::Exponent:
      return structural_hex_exponent_times_decimal_product({left.digit, left.exponent}, right);
    case StructuralHexOperandKind::CarryNormalized:
      return structural_hex_carry_normalized_binary_decimal_product(left.product, right, 0x12);
  }
  return std::nullopt;
}

std::optional<StructuralHexDecimalProduct> decimal_times_structural_hex_operand_product(
    const std::string& left, const StructuralHexArithmeticOperand& right) {
  switch (right.kind) {
    case StructuralHexOperandKind::Digit:
      return decimal_times_structural_hex_digit_product(left, right.digit);
    case StructuralHexOperandKind::Exponent:
      return decimal_times_structural_hex_exponent_product(left, {right.digit, right.exponent});
    case StructuralHexOperandKind::CarryNormalized:
      return decimal_binary_structural_hex_carry_normalized_product(left, right.product, 0x12);
  }
  return std::nullopt;
}

std::optional<StructuralHexDecimalProduct> structural_hex_operand_divide_decimal_product(
    const StructuralHexArithmeticOperand& left, const std::string& right) {
  switch (left.kind) {
    case StructuralHexOperandKind::Digit:
      return structural_hex_digit_divide_decimal_product(left.digit, right);
    case StructuralHexOperandKind::Exponent:
      return structural_hex_exponent_divide_decimal_product({left.digit, left.exponent}, right);
    case StructuralHexOperandKind::CarryNormalized:
      return structural_hex_carry_normalized_binary_decimal_product(left.product, right, 0x13);
  }
  return std::nullopt;
}

std::optional<StructuralHexDecimalProduct> decimal_divide_structural_hex_operand_product(
    const std::string& left, const StructuralHexArithmeticOperand& right) {
  switch (right.kind) {
    case StructuralHexOperandKind::Digit:
      return decimal_divide_structural_hex_digit_product(left, right.digit);
    case StructuralHexOperandKind::Exponent:
      return decimal_divide_structural_hex_exponent_product(left, {right.digit, right.exponent});
    case StructuralHexOperandKind::CarryNormalized:
      return decimal_binary_structural_hex_carry_normalized_product(left, right.product, 0x13);
  }
  return std::nullopt;
}

std::optional<StructuralHexDecimalProduct> structural_hex_operand_plus_structural_hex_operand_product(
    const StructuralHexArithmeticOperand& left, const StructuralHexArithmeticOperand& right) {
  if (left.kind == StructuralHexOperandKind::Digit && right.kind == StructuralHexOperandKind::Digit)
    return structural_hex_digit_plus_structural_hex_digit_product(left.digit, right.digit);
  if (left.kind == StructuralHexOperandKind::CarryNormalized)
    return decimal_plus_structural_hex_operand_product(left.product.value, right);
  if (right.kind == StructuralHexOperandKind::CarryNormalized)
    return structural_hex_operand_plus_decimal_product(left, right.product.value);
  return std::nullopt;
}

std::optional<StructuralHexDecimalProduct> structural_hex_operand_minus_structural_hex_operand_product(
    const StructuralHexArithmeticOperand& left, const StructuralHexArithmeticOperand& right) {
  if (left.kind == StructuralHexOperandKind::Digit && right.kind == StructuralHexOperandKind::Digit)
    return structural_hex_digit_minus_structural_hex_digit_product(left.digit, right.digit);
  if (left.kind == StructuralHexOperandKind::CarryNormalized)
    return decimal_minus_structural_hex_operand_product(left.product.value, right);
  if (right.kind == StructuralHexOperandKind::CarryNormalized)
    return structural_hex_operand_minus_decimal_product(left, right.product.value);
  return std::nullopt;
}

std::optional<StructuralHexDecimalProduct> structural_hex_operand_times_structural_hex_operand_product(
    const StructuralHexArithmeticOperand& left, const StructuralHexArithmeticOperand& right) {
  if (left.kind == StructuralHexOperandKind::Digit && right.kind == StructuralHexOperandKind::Digit)
    return structural_hex_digit_times_structural_hex_digit_product(left.digit, right.digit);
  if (left.kind == StructuralHexOperandKind::CarryNormalized)
    return decimal_times_structural_hex_operand_product(left.product.value, right);
  if (right.kind == StructuralHexOperandKind::CarryNormalized)
    return structural_hex_operand_times_decimal_product(left, right.product.value);
  return std::nullopt;
}

std::optional<StructuralHexDecimalProduct> structural_hex_operand_divide_structural_hex_operand_product(
    const StructuralHexArithmeticOperand& left, const StructuralHexArithmeticOperand& right) {
  if (left.kind == StructuralHexOperandKind::Digit && right.kind == StructuralHexOperandKind::Digit)
    return structural_hex_digit_divide_structural_hex_digit_product(left.digit, right.digit);
  if (left.kind == StructuralHexOperandKind::CarryNormalized)
    return decimal_divide_structural_hex_operand_product(left.product.value, right);
  if (right.kind == StructuralHexOperandKind::CarryNormalized)
    return structural_hex_operand_divide_decimal_product(left, right.product.value);
  return std::nullopt;
}

// --- normalized decimal values cluster -------------------------------------
struct ConcreteEvaluationOptions {
  bool include_structural_shape_decimals = true;
  bool include_structural_exponent_closure_decimals = true;
};

std::optional<std::string> computation_decimal_value_from_fact(const X2ValueFact& fact) {
  return x2_value_fact_restored_visible_decimal(fact);
}

std::set<X2ShapeFact> structural_restore_shape_facts_without_exponent_closure_decimals(
    const std::set<X2ShapeFact>& input) {
  const std::set<X2ShapeFact> restored = structural_restore_shape_facts(input);
  std::set<X2ShapeFact> blocked;
  for (const X2ShapeFact& fact : input) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind != X2ShapeModelKind::ExponentEntry || !model.closedStructuralMantissa.has_value())
      continue;
    if (!model.mantissa.has_value() || !has_structural_non_decimal_digit(model.mantissa->canonical))
      continue;
    const std::optional<X2ShapeFact> canonical = x2_shape_fact_from_data_model(model);
    if (canonical.has_value())
      blocked.insert(*canonical);
    const std::optional<X2ShapeFact> closed = x2_mantissa_shape_fact_from_model(*model.closedStructuralMantissa);
    if (closed.has_value() && input.count(*closed) == 0)
      blocked.insert(*closed);
  }
  std::set<X2ShapeFact> output;
  for (const X2ShapeFact& fact : restored) {
    if (blocked.count(fact) == 0)
      output.insert(fact);
  }
  return output;
}

std::set<X2ShapeFact> computation_structural_restore_shape_facts(const X2ShapeSet* shapes,
                                                                 const ConcreteEvaluationOptions& options) {
  const std::set<X2ShapeFact> canonical = canonical_structural_shape_facts(shapes);
  return options.include_structural_exponent_closure_decimals
             ? structural_restore_shape_facts(canonical)
             : structural_restore_shape_facts_without_exponent_closure_decimals(canonical);
}

std::set<X2ShapeFact> canonical_structural_restore_source_key_facts_for_decimalization(
    const X2ShapeSet* shapes, const ConcreteEvaluationOptions& options) {
  const std::set<X2ShapeFact> restored =
      options.include_structural_exponent_closure_decimals
          ? structural_restore_shape_facts(canonical_structural_shape_facts(shapes))
          : structural_restore_shape_facts_without_exponent_closure_decimals(
                canonical_structural_shape_facts(shapes));
  std::set<X2ShapeFact> output;
  for (const X2ShapeFact& fact : restored) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind == X2ShapeModelKind::ExponentEntry && model.closedStructuralMantissa.has_value()) {
      const std::optional<X2ShapeFact> closed = x2_mantissa_shape_fact_from_model(*model.closedStructuralMantissa);
      if (closed.has_value() && restored.count(*closed) != 0)
        continue;
    }
    output.insert(fact);
  }
  return output;
}

std::set<std::string> normalized_decimal_shape_values(const X2ShapeSet* shapes,
                                                      const ConcreteEvaluationOptions& options) {
  if (!options.include_structural_shape_decimals) {
    const std::set<X2ShapeFact> display = decimal_display_shape_facts(shapes);
    std::set<std::string> output = x2_shape_set_restored_visible_decimals(&display);
    for (const X2ShapeFact& fact :
         canonical_structural_restore_source_key_facts_for_decimalization(shapes, options)) {
      const std::optional<std::string> decimal = structural_shape_fact_restored_visible_decimal(fact);
      if (decimal.has_value())
        output.insert(*decimal);
    }
    return output;
  }
  return x2_shape_set_restored_visible_decimals(shapes);
}

std::set<std::string> computation_decimal_shape_values(const X2ShapeSet* shapes,
                                                       const ConcreteEvaluationOptions& options) {
  std::set<std::string> output = normalized_decimal_shape_values(shapes, options);
  for (const X2ShapeFact& fact : computation_structural_restore_shape_facts(shapes, options)) {
    const std::optional<std::string> value = x2_shape_fact_shape_only_exact_decimal_display(fact);
    if (value.has_value())
      output.insert(*value);
  }
  return output;
}

std::set<std::string> normalized_decimal_values(const X2ValueSet* values, const X2ShapeSet* shapes,
                                                const ConcreteEvaluationOptions& options) {
  std::set<std::string> output;
  if (values != nullptr) {
    for (const X2ValueFact& fact : *values) {
      const std::optional<std::string> value = computation_decimal_value_from_fact(fact);
      if (value.has_value())
        output.insert(*value);
    }
  }
  for (const std::string& value : computation_decimal_shape_values(shapes, options))
    output.insert(value);
  return output;
}

// --- binary products entry points ------------------------------------------
void add_structural_hex_decimal_product(std::vector<StructuralHexDecimalProduct>& output,
                                        const std::optional<StructuralHexDecimalProduct>& product) {
  if (product.has_value())
    output.push_back(*product);
}

std::vector<StructuralHexDecimalProduct> structural_hex_binary_decimal_products(
    int opcode, const X2ValueSet* y, const X2ValueSet* x, const X2ShapeSet* y_shape,
    const X2ShapeSet* x_shape, const X2ShapeSet* direct_y_shape, const X2ShapeSet* direct_x_shape) {
  std::vector<StructuralHexDecimalProduct> output;
  if (opcode >= 0x37 && opcode <= 0x39)
    return output;
  if (opcode != 0x10 && opcode != 0x11 && opcode != 0x12 && opcode != 0x13)
    return output;
  const ConcreteEvaluationOptions options;
  const std::vector<StructuralHexArithmeticOperand> left_operands =
      structural_hex_arithmetic_operands(y_shape, direct_y_shape);
  const std::vector<StructuralHexArithmeticOperand> right_operands =
      structural_hex_arithmetic_operands(x_shape, direct_x_shape);
  const std::set<std::string> right_values = normalized_decimal_values(x, x_shape, options);
  const std::set<std::string> left_values = normalized_decimal_values(y, y_shape, options);
  for (const StructuralHexArithmeticOperand& left_operand : left_operands) {
    for (const std::string& right : right_values) {
      std::optional<StructuralHexDecimalProduct> product;
      if (opcode == 0x10)
        product = structural_hex_operand_plus_decimal_product(left_operand, right);
      else if (opcode == 0x11)
        product = structural_hex_operand_minus_decimal_product(left_operand, right);
      else if (opcode == 0x12)
        product = structural_hex_operand_times_decimal_product(left_operand, right);
      else
        product = structural_hex_operand_divide_decimal_product(left_operand, right);
      add_structural_hex_decimal_product(output, product);
    }
  }
  for (const std::string& left : left_values) {
    for (const StructuralHexArithmeticOperand& right_operand : right_operands) {
      std::optional<StructuralHexDecimalProduct> product;
      if (opcode == 0x10)
        product = decimal_plus_structural_hex_operand_product(left, right_operand);
      else if (opcode == 0x11)
        product = decimal_minus_structural_hex_operand_product(left, right_operand);
      else if (opcode == 0x12)
        product = decimal_times_structural_hex_operand_product(left, right_operand);
      else
        product = decimal_divide_structural_hex_operand_product(left, right_operand);
      add_structural_hex_decimal_product(output, product);
    }
  }
  for (const StructuralHexArithmeticOperand& left_operand : left_operands) {
    for (const StructuralHexArithmeticOperand& right_operand : right_operands) {
      std::optional<StructuralHexDecimalProduct> product;
      if (opcode == 0x10)
        product = structural_hex_operand_plus_structural_hex_operand_product(left_operand, right_operand);
      else if (opcode == 0x11)
        product = structural_hex_operand_minus_structural_hex_operand_product(left_operand, right_operand);
      else if (opcode == 0x12)
        product = structural_hex_operand_times_structural_hex_operand_product(left_operand, right_operand);
      else
        product = structural_hex_operand_divide_structural_hex_operand_product(left_operand, right_operand);
      add_structural_hex_decimal_product(output, product);
    }
  }
  return output;
}

std::set<X2ShapeFact> structural_hex_binary_decimal_display_shapes(
    int opcode, const X2ValueSet* y, const X2ValueSet* x, const X2ShapeSet* y_shape,
    const X2ShapeSet* x_shape, const X2ShapeSet* direct_y_shape, const X2ShapeSet* direct_x_shape) {
  std::set<X2ShapeFact> output;
  for (const StructuralHexDecimalProduct& product : structural_hex_binary_decimal_products(
           opcode, y, x, y_shape, x_shape, direct_y_shape, direct_x_shape)) {
    const std::optional<X2ShapeFact> shape = structural_hex_decimal_product_display_shape(product);
    if (shape.has_value())
      output.insert(*shape);
  }
  return output;
}

std::string structural_hex_binary_debug(int opcode, const X2ValueSet& y, const X2ValueSet& x,
                                        const X2ShapeSet& y_shape, const X2ShapeSet& x_shape,
                                        const X2ShapeSet& direct_y_shape, const X2ShapeSet& direct_x_shape) {
  const auto join = [](const std::set<std::string>& items) {
    if (items.empty())
      return std::string{"_"};
    std::string output;
    for (const std::string& item : items) {
      if (!output.empty())
        output += ",";
      output += item;
    }
    return output;
  };
  std::set<std::string> values;
  for (const StructuralHexDecimalProduct& product : structural_hex_binary_decimal_products(
           opcode, &y, &x, &y_shape, &x_shape, &direct_y_shape, &direct_x_shape))
    values.insert(product.value);
  return "v=" + join(values) + "|disp=" +
         join(structural_hex_binary_decimal_display_shapes(opcode, &y, &x, &y_shape, &x_shape,
                                                           &direct_y_shape, &direct_x_shape));
}

// ===========================================================================
// Session A.2b (part 1): binary structural bitwise engine (faithful port of the
// TS structuralBitwise* binary cluster: AND/OR/XOR over value+shape operands).
// Dead code; validated against the TS oracle in isolation.
// ===========================================================================
std::optional<std::string> normalized_decimal_value_from_fact(const X2ValueFact& fact) {
  static const std::regex pattern(
      R"(^decimal:(-?(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+)):normalized$)");
  std::smatch match;
  if (!std::regex_match(fact, match, pattern))
    return std::nullopt;
  return match[1].str();
}

std::vector<StructuralBitwiseOperand> bitwise_operands_from_values_and_shapes(
    const X2ValueSet* values, const X2ShapeSet* shapes) {
  std::vector<StructuralBitwiseOperand> output;
  if (values != nullptr) {
    for (const X2ValueFact& fact : *values) {
      const std::optional<std::string> value = normalized_decimal_value_from_fact(fact);
      const std::optional<std::vector<int>> nibbles =
          value.has_value() ? decimal_mantissa_digits(*value) : std::nullopt;
      if (nibbles.has_value())
        output.push_back(StructuralBitwiseOperand{*nibbles, false});
    }
  }
  for (const X2ShapeFact& fact :
       structural_restore_shape_facts(canonical_structural_shape_facts(shapes))) {
    const std::optional<std::vector<int>> nibbles = structural_mantissa_nibbles(fact);
    if (nibbles.has_value())
      output.push_back(StructuralBitwiseOperand{*nibbles, true});
  }
  return output;
}

std::optional<std::vector<int>> structural_bitwise_nibbles(int opcode,
                                                           const StructuralBitwiseOperand& left,
                                                           const StructuralBitwiseOperand& right) {
  if (left.nibbles.size() != 8U || right.nibbles.size() != 8U)
    return std::nullopt;
  std::vector<int> result{8};
  for (std::size_t index = 1; index < 8U; ++index) {
    const std::optional<int> digit =
        bitwise_mantissa_digit(opcode, left.nibbles.at(index), right.nibbles.at(index));
    if (!digit.has_value() || *digit < 0 || *digit > 15)
      return std::nullopt;
    result.push_back(*digit);
  }
  return result;
}

std::optional<X2ShapeFact> structural_bitwise_mantissa_shape_fact(
    int opcode, const StructuralBitwiseOperand& left, const StructuralBitwiseOperand& right) {
  const std::optional<std::vector<int>> result = structural_bitwise_nibbles(opcode, left, right);
  if (!result.has_value())
    return std::nullopt;
  const bool has_hex_cell = std::any_of(result->begin(), result->end(), [](int d) { return d > 9; });
  if (!has_hex_cell && !left.structural && !right.structural)
    return std::nullopt;
  return x2_mantissa_shape_fact_from_parts(X2MantissaRadix::Hex, bitwise_mantissa_raw(*result));
}

std::optional<std::string> structural_bitwise_decimal_value(int opcode,
                                                            const StructuralBitwiseOperand& left,
                                                            const StructuralBitwiseOperand& right) {
  if (!left.structural && !right.structural)
    return std::nullopt;
  const std::optional<std::vector<int>> result = structural_bitwise_nibbles(opcode, left, right);
  return result.has_value() ? decimal_value_from_bitwise_mantissa_nibbles(*result) : std::nullopt;
}

std::set<std::string> structural_bitwise_decimal_values(int opcode, const X2ValueSet* y,
                                                        const X2ValueSet* x, const X2ShapeSet* y_shape,
                                                        const X2ShapeSet* x_shape) {
  std::set<std::string> output;
  if (opcode < 0x37 || opcode > 0x39)
    return output;
  const std::vector<StructuralBitwiseOperand> left = bitwise_operands_from_values_and_shapes(y, y_shape);
  const std::vector<StructuralBitwiseOperand> right = bitwise_operands_from_values_and_shapes(x, x_shape);
  for (const StructuralBitwiseOperand& l : left)
    for (const StructuralBitwiseOperand& r : right) {
      const std::optional<std::string> result = structural_bitwise_decimal_value(opcode, l, r);
      if (result.has_value())
        output.insert(*result);
    }
  return output;
}

std::set<X2ShapeFact> structural_bitwise_decimal_display_shapes(int opcode, const X2ValueSet* y,
                                                                const X2ValueSet* x,
                                                                const X2ShapeSet* y_shape,
                                                                const X2ShapeSet* x_shape) {
  std::set<X2ShapeFact> output;
  if (opcode < 0x37 || opcode > 0x39)
    return output;
  const std::vector<StructuralBitwiseOperand> left = bitwise_operands_from_values_and_shapes(y, y_shape);
  const std::vector<StructuralBitwiseOperand> right = bitwise_operands_from_values_and_shapes(x, x_shape);
  for (const StructuralBitwiseOperand& l : left)
    for (const StructuralBitwiseOperand& r : right) {
      if (!l.structural && !r.structural)
        continue;
      const std::optional<std::vector<int>> result = structural_bitwise_nibbles(opcode, l, r);
      const std::optional<X2ShapeFact> shape =
          result.has_value() ? decimal_display_shape_from_bitwise_mantissa_nibbles(*result)
                             : std::nullopt;
      if (shape.has_value())
        output.insert(*shape);
    }
  return output;
}

std::string structural_bitwise_binary_debug(int opcode, const X2ValueSet& y, const X2ValueSet& x,
                                            const X2ShapeSet& y_shape, const X2ShapeSet& x_shape) {
  const auto join = [](const std::set<std::string>& items) {
    if (items.empty())
      return std::string{"_"};
    std::string output;
    for (const std::string& item : items) {
      if (!output.empty())
        output += ",";
      output += item;
    }
    return output;
  };
  std::set<X2ShapeFact> mantissa_shapes;
  const std::vector<StructuralBitwiseOperand> left = bitwise_operands_from_values_and_shapes(&y, &y_shape);
  const std::vector<StructuralBitwiseOperand> right = bitwise_operands_from_values_and_shapes(&x, &x_shape);
  for (const StructuralBitwiseOperand& l : left)
    for (const StructuralBitwiseOperand& r : right) {
      const std::optional<X2ShapeFact> fact = structural_bitwise_mantissa_shape_fact(opcode, l, r);
      if (fact.has_value())
        mantissa_shapes.insert(*fact);
    }
  return "v=" + join(structural_bitwise_decimal_values(opcode, &y, &x, &y_shape, &x_shape)) +
         "|disp=" + join(structural_bitwise_decimal_display_shapes(opcode, &y, &x, &y_shape, &x_shape)) +
         "|ms=" + join(mantissa_shapes);
}

// =====================================================================
// Session A.3 + A.4: decimal concrete-evaluation + stable/opaque
// expression-key machinery. Faithful 1:1 port of the TypeScript closure
// rooted at plainProducesStableExpressionValues / plainProducesConcrete*.
// Additive dead code in the x2eval namespace (not wired into Plain).
// =====================================================================

// --- opcode sets / small constants -----------------------------------
const std::set<int>& commutative_stable_expr_opcodes() {
  static const std::set<int> set{0x10, 0x12, 0x36, 0x37, 0x38, 0x39};
  return set;
}
const std::set<int>& stable_constant_expr_opcodes() {
  static const std::set<int> set{0x20};
  return set;
}
const std::set<int>& pure_opaque_expr_opcodes() {
  static const std::set<int> set{0x10, 0x11, 0x12, 0x13, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a,
                                 0x1b, 0x1c, 0x1d, 0x1e, 0x21, 0x22, 0x23, 0x24, 0x26, 0x2a,
                                 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
                                 0x3a};
  return set;
}
const std::set<int>& exact_decimal_binary_mantissa_shape_opcodes() {
  static const std::set<int> set{0x10, 0x11, 0x12, 0x13, 0x24, 0x36};
  return set;
}
constexpr int X2_SIGN_CHANGE_OPCODE = 0x0b;
constexpr std::size_t MAX_STABLE_BINARY_SOURCE_KEY_PAIRS = 16;

const std::vector<std::string>& register_names() {
  static const std::vector<std::string> names{"0", "1", "2", "3", "4", "5", "6", "7",
                                               "8", "9", "a", "b", "c", "d", "e"};
  return names;
}

std::string stable_expression_opcode_hex(int opcode) {
  static const char* digits = "0123456789ABCDEF";
  std::string out;
  int value = opcode;
  if (value < 0)
    value = 0;
  do {
    out.insert(out.begin(), digits[value & 0xf]);
    value >>= 4;
  } while (value != 0);
  while (out.size() < 2)
    out.insert(out.begin(), '0');
  return out;
}

bool has_ir_roles(const IrOp& op) { return !op.meta.roles.empty(); }

bool opcode_has_structural_operand_semantics(int opcode) {
  return opcode == 0x10 || opcode == 0x11 || opcode == 0x12 || opcode == 0x13 || opcode == 0x22 ||
         opcode == 0x32 || opcode == 0x37 || opcode == 0x38 || opcode == 0x39 || opcode == 0x3a;
}

bool opcode_preserves_pinned_structural_exponent_source(int opcode) {
  return opcode == 0x10 || opcode == 0x11 || opcode == 0x12 || opcode == 0x13 || opcode == 0x32;
}

ConcreteEvaluationOptions concrete_evaluation_options_for_opcode(int opcode,
                                                                 ConcreteEvaluationOptions options) {
  if (!opcode_has_structural_operand_semantics(opcode))
    return options;
  // TS: options.includeStructural*Decimals ?? false. The native bool models the
  // TS "undefined" state as true; no caller sets these explicitly to true, so a
  // structural opcode always forces them off.
  options.include_structural_shape_decimals = false;
  options.include_structural_exponent_closure_decimals = false;
  return options;
}

ConcreteEvaluationOptions concrete_evaluation_options_for_stable_expression_opcode(int opcode) {
  return concrete_evaluation_options_for_opcode(opcode, ConcreteEvaluationOptions{});
}

IrOp stable_expression_plain_op(int opcode) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = opcode;
  op.meta.mnemonic = "expr-key " + stable_expression_opcode_hex(opcode);
  return op;
}

// Forward declarations for the deeply mutually-recursive A.3/A.4 cluster.
X2ValueFact stable_expression_value_fact(const std::string& opcode, const std::string& source);
std::set<X2ValueFact> plain_produces_stable_expression_values(const IrOp& op, const X2ValueSet* x,
                                                              const X2ValueSet* y, const X2ShapeSet* x_shape,
                                                              const X2ShapeSet* y_shape,
                                                              const X2ShapeSet* direct_y_shape,
                                                              const X2ShapeSet* direct_x_shape);
std::optional<X2ValueFact> plain_produces_opaque_expression_value(const IrOp& op,
                                                                  std::optional<int> producer_index);
std::set<X2ShapeFact> plain_produces_stable_constant_shape_facts(const IrOp& op);

// --- stable constant expression values --------------------------------
std::optional<X2ValueFact> plain_produces_stable_constant_decimal_value(const IrOp& op) {
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op) || has_ir_roles(op))
    return std::nullopt;
  if (op.opcode != 0x20)
    return std::nullopt;
  const OpcodeInfo& info = opcode_by_code(op.opcode);
  if (info.risk != OpcodeRisk::Documented || info.x2_effect != X2Effect::Preserves ||
      info.stack_effect != StackEffect::Shifts)
    return std::nullopt;
  return decimal_value_fact("3.1415926", "normalized");
}

std::optional<X2ValueFact> plain_produces_stable_constant_expression_value(const IrOp& op) {
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op) || has_ir_roles(op))
    return std::nullopt;
  if (stable_constant_expr_opcodes().count(op.opcode) == 0)
    return std::nullopt;
  const OpcodeInfo& info = opcode_by_code(op.opcode);
  if (info.risk != OpcodeRisk::Documented || info.x2_effect != X2Effect::Preserves ||
      info.stack_effect != StackEffect::Shifts)
    return std::nullopt;
  return stable_expression_value_fact(stable_expression_opcode_hex(op.opcode), "");
}

std::set<X2ValueFact> plain_produces_stable_constant_expression_values(const IrOp& op) {
  std::set<X2ValueFact> output;
  const std::optional<X2ValueFact> constant = plain_produces_stable_constant_expression_value(op);
  if (constant.has_value())
    output.insert(*constant);
  const std::optional<X2ValueFact> decimal = plain_produces_stable_constant_decimal_value(op);
  if (decimal.has_value())
    output.insert(*decimal);
  return output;
}

std::set<X2ShapeFact> plain_produces_stable_constant_shape_facts(const IrOp& op) {
  std::set<X2ShapeFact> output;
  const std::optional<X2ValueFact> decimal = plain_produces_stable_constant_decimal_value(op);
  const std::optional<std::string> value =
      decimal.has_value() ? normalized_decimal_value_from_fact(*decimal) : std::nullopt;
  const std::optional<X2ShapeFact> shape =
      value.has_value() ? exact_decimal_display_shape_fact(*value) : std::nullopt;
  if (shape.has_value())
    output.insert(*shape);
  return output;
}

// --- value/shape fact builders ---------------------------------------
X2ValueFact expression_value_fact(int producer_index) {
  return "expr:" + std::to_string(producer_index);
}

// Hand-rolled scanners replacing hot std::regex calls in the recursive
// stable-expression-key evaluation path (libc++ std::regex dominates the X2
// dataflow fixpoint). Each replicates the exact semantics of its pattern.
inline bool tx_is_hex_upper(char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F');
}

struct TxShapeSourceToken {
  std::size_t pos = 0;
  std::size_t len = 0;
  std::string capture;
};

// Mirrors std::sregex_iterator over shape:([^,()]+): left-to-right,
// non-overlapping matches of "shape:" followed by >=1 chars that are not ',()'.
inline std::vector<TxShapeSourceToken> tx_find_shape_source_tokens(const std::string& text) {
  std::vector<TxShapeSourceToken> tokens;
  constexpr std::string_view needle = "shape:";
  std::size_t search = 0;
  while (search <= text.size()) {
    const std::size_t found = text.find(needle, search);
    if (found == std::string::npos)
      break;
    std::size_t cursor = found + needle.size();
    const std::size_t capture_start = cursor;
    while (cursor < text.size() && text[cursor] != ',' && text[cursor] != '(' &&
           text[cursor] != ')')
      ++cursor;
    if (cursor > capture_start) {
      TxShapeSourceToken token;
      token.pos = found;
      token.len = cursor - found;
      token.capture = text.substr(capture_start, cursor - capture_start);
      tokens.push_back(std::move(token));
      search = cursor;
    } else {
      search = found + 1;
    }
  }
  return tokens;
}

std::optional<std::string> decimal_from_fact_key(const std::string& key) {
  constexpr std::string_view prefix = "decimal:";
  constexpr std::string_view suffix = ":normalized";
  if (key.size() <= prefix.size() + suffix.size())
    return std::nullopt;
  if (!key.starts_with(prefix) || !key.ends_with(suffix))
    return std::nullopt;
  const std::string mid =
      key.substr(prefix.size(), key.size() - prefix.size() - suffix.size());
  if (mid.empty() || mid.find(':') != std::string::npos)
    return std::nullopt;
  return mid;
}

X2ValueFact register_value_fact(const std::string& register_name) { return "reg:" + register_name; }

std::string sign_changed_normalized_decimal_value(const std::string& raw) {
  const std::optional<std::string> normalized = normalize_plain_decimal(raw);
  if (!normalized.has_value() || *normalized == "0")
    return "0";
  return normalized->rfind('-', 0) == 0 ? normalized->substr(1) : "-" + *normalized;
}

bool is_signed_zero_decimal_mantissa_shape_fact(const X2ShapeFact& fact) {
  constexpr std::string_view prefix = "mantissa:-0";
  constexpr std::string_view suffix = ":decimal";
  if (fact.size() < prefix.size() + suffix.size())
    return false;
  if (!fact.starts_with(prefix) || !fact.ends_with(suffix))
    return false;
  const std::string mid =
      fact.substr(prefix.size(), fact.size() - prefix.size() - suffix.size());
  if (mid.empty())
    return true;
  if (mid[0] != '.')
    return false;
  for (std::size_t i = 1; i < mid.size(); ++i) {
    if (mid[i] != '0')
      return false;
  }
  return true;
}

// --- sign-change shape facts -----------------------------------------
std::optional<std::string> sign_changed_mantissa_shape(const std::string& raw) {
  const std::optional<std::string> normalized = normalize_decimal_mantissa_entry(raw);
  if (!normalized.has_value())
    return std::nullopt;
  if (*normalized == "0")
    return std::string{"-0"};
  return raw.rfind('-', 0) == 0 ? raw.substr(1) : "-" + raw;
}

std::string toggle_raw_sign(const std::string& raw) {
  return raw.rfind('-', 0) == 0 ? raw.substr(1) : "-" + raw;
}

std::optional<X2ShapeFact> x2_mantissa_sign_changed_shape_fact(const X2ShapeFact& fact) {
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
  if (model.kind != X2ShapeModelKind::Mantissa || !model.mantissa.has_value())
    return std::nullopt;
  if (model.mantissa->radix == X2MantissaRadix::Decimal) {
    const std::optional<std::string> signed_value = sign_changed_mantissa_shape(model.mantissa->canonical);
    return signed_value.has_value() ? std::optional<X2ShapeFact>{decimal_mantissa_shape_fact(*signed_value)}
                                     : std::nullopt;
  }
  if (model.mantissa->radix == X2MantissaRadix::Hex || model.mantissa->radix == X2MantissaRadix::Super)
    return x2_mantissa_shape_fact_from_parts(model.mantissa->radix, toggle_raw_sign(model.mantissa->canonical));
  return std::nullopt;
}

std::optional<X2ShapeFact> x2_exponent_mantissa_sign_changed_shape_fact(const X2ShapeFact& fact) {
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
  if (model.kind != X2ShapeModelKind::ExponentEntry || !model.mantissa.has_value())
    return std::nullopt;
  const std::optional<X2ShapeFact> mantissa = x2_mantissa_shape_fact_from_model(*model.mantissa);
  if (!mantissa.has_value())
    return std::nullopt;
  const std::optional<X2ShapeFact> signed_mantissa = x2_mantissa_sign_changed_shape_fact(*mantissa);
  return signed_mantissa.has_value()
             ? x2_exponent_shape_fact_from_mantissa_fact(*signed_mantissa, model.exponentRaw)
             : std::nullopt;
}

// --- structural-restore source-key facts -----------------------------
std::set<X2ShapeFact> canonical_structural_restore_source_key_facts(const X2ShapeSet* shapes) {
  const std::set<X2ShapeFact> restored = structural_restore_shape_facts(canonical_structural_shape_facts(shapes));
  std::set<X2ShapeFact> output;
  for (const X2ShapeFact& fact : restored) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind == X2ShapeModelKind::ExponentEntry && model.closedStructuralMantissa.has_value()) {
      const std::optional<X2ShapeFact> closed = x2_mantissa_shape_fact_from_model(*model.closedStructuralMantissa);
      if (closed.has_value() && restored.count(*closed) != 0)
        continue;
    }
    output.insert(fact);
  }
  return output;
}

std::set<X2ShapeFact> x2_restored_display_source_key_shape_facts(const X2ShapeSet* input) {
  std::set<X2ShapeFact> output = decimal_display_shape_facts(input);
  for (const X2ShapeFact& fact : canonical_structural_restore_source_key_facts(input))
    output.insert(fact);
  return output;
}

std::set<X2ShapeFact> x2_restored_display_shape_facts_from_source_key(const std::string& key) {
  constexpr std::string_view prefix = "shape:";
  if (!key.starts_with(prefix))
    return {};
  X2ShapeSet single{key.substr(prefix.size())};
  return x2_restored_display_source_key_shape_facts(&single);
}

std::set<X2ShapeFact> stable_expression_key_direct_shape_set(const std::string& key) {
  constexpr std::string_view prefix = "shape:";
  if (!key.starts_with(prefix))
    return {};
  const std::optional<X2ShapeFact> canonical = x2_canonical_shape_fact_if_valid(key.substr(prefix.size()));
  if (!canonical.has_value())
    return {};
  return {*canonical};
}

std::set<std::string> normalized_decimal_value_set(const X2ValueSet* values) {
  std::set<std::string> output;
  if (values != nullptr) {
    for (const X2ValueFact& fact : *values) {
      const std::optional<std::string> value = computation_decimal_value_from_fact(fact);
      if (value.has_value())
        output.insert(*value);
    }
  }
  return output;
}

std::string stable_structural_expression_source_key(const X2ShapeFact& fact) {
  return "shape:" + x2_canonical_shape_fact(fact);
}

std::string stable_expression_shape_source_key(const X2ShapeFact& fact) {
  std::optional<std::string> decimal = x2_shape_fact_restored_visible_decimal(fact);
  if (!decimal.has_value())
    decimal = x2_shape_fact_shape_only_exact_decimal_display(fact);
  return decimal.has_value() ? decimal_value_fact(*decimal, "normalized")
                             : stable_structural_expression_source_key(fact);
}

std::optional<std::string> pinned_structural_exponent_stable_shape_source_key(const X2ShapeFact& fact) {
  const std::optional<X2ShapeFact> canonical = x2_canonical_shape_fact_if_valid(fact);
  if (!canonical.has_value())
    return std::nullopt;
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(*canonical);
  if (model.kind != X2ShapeModelKind::ExponentEntry || !model.mantissa.has_value() ||
      (model.mantissa->radix != X2MantissaRadix::Hex && model.mantissa->radix != X2MantissaRadix::Super) ||
      !has_structural_non_decimal_digit(model.mantissa->canonical))
    return std::nullopt;
  return stable_structural_expression_source_key(*canonical);
}

struct PinnedStructuralExponentKeys {
  std::set<std::string> keys;
  std::set<std::string> blocked_restore_keys;
};

PinnedStructuralExponentKeys pinned_structural_exponent_stable_shape_source_keys(const X2ShapeSet* shapes) {
  PinnedStructuralExponentKeys output;
  for (const X2ShapeFact& fact : canonical_structural_shape_facts(shapes)) {
    const std::optional<std::string> key = pinned_structural_exponent_stable_shape_source_key(fact);
    if (!key.has_value())
      continue;
    output.keys.insert(*key);
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind != X2ShapeModelKind::ExponentEntry || !model.closedStructuralMantissa.has_value())
      continue;
    const std::optional<X2ShapeFact> closed = x2_mantissa_shape_fact_from_model(*model.closedStructuralMantissa);
    if (closed.has_value())
      output.blocked_restore_keys.insert(stable_expression_shape_source_key(*closed));
  }
  return output;
}

// --- canonicalization of stable expression value facts ----------------
std::optional<X2ValueFact> canonical_stable_expression_value_fact_if_valid(const X2ValueFact& fact);

std::optional<std::string> canonical_stable_shape_source_key(const X2ShapeFact& fact,
                                                             bool preserve_pinned_structural_exponent_source) {
  if (preserve_pinned_structural_exponent_source) {
    const std::optional<std::string> direct = pinned_structural_exponent_stable_shape_source_key(fact);
    if (direct.has_value())
      return direct;
  }
  X2ShapeSet single{fact};
  const std::set<X2ShapeFact> facts = x2_restored_display_source_key_shape_facts(&single);
  return facts.size() == 1 ? std::optional<std::string>{stable_expression_shape_source_key(*facts.begin())}
                           : std::nullopt;
}

std::optional<int> stable_expression_value_fact_opcode(const X2ValueFact& fact) {
  constexpr std::string_view prefix = "expr-key:";
  if (fact.size() < prefix.size() + 3)
    return std::nullopt;
  if (!fact.starts_with(prefix))
    return std::nullopt;
  if (!tx_is_hex_upper(fact[prefix.size()]) || !tx_is_hex_upper(fact[prefix.size() + 1]))
    return std::nullopt;
  if (fact[prefix.size() + 2] != '(')
    return std::nullopt;
  return static_cast<int>(std::stoi(fact.substr(prefix.size(), 2), nullptr, 16));
}

bool stable_expression_value_fact_has_invalid_shape_source(const X2ValueFact& fact) {
  for (const TxShapeSourceToken& token : tx_find_shape_source_tokens(fact)) {
    if (!canonical_stable_shape_source_key(token.capture, false).has_value())
      return true;
  }
  return false;
}

std::optional<X2ValueFact> canonical_stable_expression_value_fact_if_valid(const X2ValueFact& fact) {
  if (fact.rfind("expr-key:", 0) != 0)
    return fact;
  if (stable_expression_value_fact_has_invalid_shape_source(fact))
    return std::nullopt;
  const std::optional<int> opcode = stable_expression_value_fact_opcode(fact);
  const bool preserve = opcode.has_value() && opcode_preserves_pinned_structural_exponent_source(*opcode);
  std::string out;
  std::size_t last = 0;
  for (const TxShapeSourceToken& token : tx_find_shape_source_tokens(fact)) {
    out.append(fact, last, token.pos - last);
    const std::optional<std::string> canonical =
        canonical_stable_shape_source_key(token.capture, preserve);
    out += canonical.has_value() ? *canonical : fact.substr(token.pos, token.len);
    last = token.pos + token.len;
  }
  out.append(fact, last, fact.size() - last);
  return out;
}

X2ValueFact canonical_stable_expression_value_fact(const X2ValueFact& fact) {
  return canonical_stable_expression_value_fact_if_valid(fact).value_or(fact);
}

X2ValueFact stable_expression_value_fact(const std::string& opcode, const std::string& source) {
  return canonical_stable_expression_value_fact("expr-key:" + opcode + "(" + source + ")");
}

std::optional<X2ValueFact> stable_expression_source_key(const X2ValueFact& fact, bool include_opaque_expr) {
  if (fact.rfind("reg:", 0) == 0)
    return fact;
  const auto is_opaque_expr = [](const std::string& value) {
    constexpr std::string_view prefix = "expr:";
    if (!value.starts_with(prefix) || value.size() == prefix.size())
      return false;
    for (std::size_t i = prefix.size(); i < value.size(); ++i) {
      if (value[i] < '0' || value[i] > '9')
        return false;
    }
    return true;
  };
  if (include_opaque_expr && is_opaque_expr(fact))
    return fact;
  if (fact.rfind("expr-key:", 0) == 0)
    return canonical_stable_expression_value_fact_if_valid(fact);
  const std::optional<std::string> decimal = computation_decimal_value_from_fact(fact);
  if (decimal.has_value())
    return decimal_value_fact(*decimal, "normalized");
  return std::nullopt;
}

std::set<std::string> stable_expression_display_shape_source_keys(const X2ValueSet* values,
                                                                  const X2ShapeSet* shapes) {
  std::set<std::string> keys;
  const std::set<std::string> value_decimals = normalized_decimal_value_set(values);
  for (const X2ShapeFact& fact : x2_restored_display_source_key_shape_facts(shapes)) {
    const std::optional<std::string> decimal = x2_shape_fact_restored_visible_decimal(fact);
    if (decimal.has_value() && value_decimals.count(*decimal) != 0)
      continue;
    keys.insert(stable_expression_shape_source_key(fact));
  }
  return keys;
}

std::set<std::string> stable_expression_source_keys(const X2ValueSet* values, const X2ShapeSet* shapes,
                                                    bool include_opaque_expr) {
  std::set<std::string> keys;
  if (values != nullptr) {
    for (const X2ValueFact& fact : *values) {
      const std::optional<X2ValueFact> key = stable_expression_source_key(fact, include_opaque_expr);
      if (key.has_value())
        keys.insert(*key);
    }
  }
  for (const std::string& key : stable_expression_display_shape_source_keys(values, shapes))
    keys.insert(key);
  return keys;
}

std::set<std::string> stable_expression_source_keys_for_operand(int opcode, const X2ValueSet* values,
                                                                const X2ShapeSet* shapes,
                                                                bool include_opaque_expr) {
  std::set<std::string> keys = stable_expression_source_keys(values, shapes, include_opaque_expr);
  if (!opcode_preserves_pinned_structural_exponent_source(opcode))
    return keys;
  const PinnedStructuralExponentKeys direct = pinned_structural_exponent_stable_shape_source_keys(shapes);
  if (direct.keys.empty())
    return keys;
  std::set<std::string> output;
  for (const std::string& key : stable_expression_source_keys(values, nullptr, include_opaque_expr))
    output.insert(key);
  for (const std::string& key : stable_expression_display_shape_source_keys(values, shapes)) {
    if (direct.blocked_restore_keys.count(key) == 0)
      output.insert(key);
  }
  for (const std::string& key : direct.keys)
    output.insert(key);
  return output;
}

// --- recursive stable-expression-key evaluators -----------------------
struct ParsedStableExpressionKey {
  int opcode = 0;
  std::vector<std::string> operands;
};

std::optional<std::vector<std::string>> split_stable_expression_operands(const std::string& source) {
  if (source.empty())
    return std::vector<std::string>{};
  std::vector<std::string> operands;
  int depth = 0;
  std::size_t start = 0;
  for (std::size_t index = 0; index < source.size(); index += 1) {
    const char ch = source[index];
    if (ch == '(') {
      depth += 1;
      continue;
    }
    if (ch == ')') {
      depth -= 1;
      if (depth < 0)
        return std::nullopt;
      continue;
    }
    if (ch != ',' || depth != 0)
      continue;
    operands.push_back(source.substr(start, index - start));
    start = index + 1;
  }
  if (depth != 0)
    return std::nullopt;
  operands.push_back(source.substr(start));
  for (const std::string& operand : operands) {
    if (operand.empty())
      return std::nullopt;
  }
  return operands;
}

std::optional<ParsedStableExpressionKey> parse_stable_expression_key(const std::string& key) {
  constexpr std::string_view prefix = "expr-key:";
  // ^expr-key:([0-9A-F]{2})\((.*)\)$ : prefix, 2 hex digits, '(', body, ')'.
  if (key.size() < prefix.size() + 4)
    return std::nullopt;
  if (!key.starts_with(prefix))
    return std::nullopt;
  if (!tx_is_hex_upper(key[prefix.size()]) || !tx_is_hex_upper(key[prefix.size() + 1]))
    return std::nullopt;
  if (key[prefix.size() + 2] != '(' || key.back() != ')')
    return std::nullopt;
  const std::size_t body_start = prefix.size() + 3;
  const std::string body = key.substr(body_start, key.size() - body_start - 1);
  const std::optional<std::vector<std::string>> operands = split_stable_expression_operands(body);
  if (!operands.has_value())
    return std::nullopt;
  ParsedStableExpressionKey parsed;
  parsed.opcode = static_cast<int>(std::stoi(key.substr(prefix.size(), 2), nullptr, 16));
  parsed.operands = *operands;
  return parsed;
}

std::optional<int> stable_expression_opcode_arity(int opcode) {
  if (stable_constant_expr_opcodes().count(opcode) != 0)
    return 0;
  if (pure_opaque_expr_opcodes().count(opcode) == 0)
    return std::nullopt;
  const OpcodeInfo& info = opcode_by_code(opcode);
  if (info.risk != OpcodeRisk::Documented || info.x2_effect != X2Effect::Preserves)
    return std::nullopt;
  if (info.stack_effect == StackEffect::Preserves)
    return 1;
  if (info.stack_effect == StackEffect::ConsumeYDrop || info.stack_effect == StackEffect::ConsumeYKeep)
    return 2;
  return std::nullopt;
}

// Forward declarations for mutual recursion.
std::set<X2ValueFact> plain_produces_concrete_decimal_values(const IrOp& op, const X2ValueSet* x,
                                                             const X2ShapeSet* x_shape,
                                                             const ConcreteEvaluationOptions& options,
                                                             const X2ShapeSet* direct_x_shape);
std::set<X2ValueFact> plain_produces_concrete_binary_decimal_values(
    const IrOp& op, const X2ValueSet* y, const X2ValueSet* x, const X2ShapeSet* y_shape,
    const X2ShapeSet* x_shape, const ConcreteEvaluationOptions& options, const X2ShapeSet* direct_y_shape,
    const X2ShapeSet* direct_x_shape);
std::set<X2ShapeFact> plain_x_shape_after_non_preserving_op(
    const IrOp& op, const X2ValueSet* x, const X2ValueSet* y, const X2ShapeSet* x_shape,
    const X2ShapeSet* y_shape, const ConcreteEvaluationOptions& options, const X2ShapeSet* direct_y_shape,
    const X2ShapeSet* direct_x_shape);
std::set<X2ValueFact> stable_expression_key_value_set_for_evaluation(const std::string& key,
                                                                     std::set<std::string>& seen);
std::set<X2ShapeFact> stable_expression_key_shape_set_for_evaluation(const std::string& key,
                                                                     std::set<std::string>& seen);
std::set<std::string> stable_expression_key_concrete_decimal_values(const std::string& key,
                                                                    std::set<std::string>& seen);
std::set<X2ShapeFact> stable_expression_key_concrete_shape_facts(const std::string& key,
                                                                 std::set<std::string>& seen);
std::set<X2ValueFact> stable_expression_key_value_set_for_operand(int opcode, const std::string& key,
                                                                  std::set<std::string>& seen);
std::set<X2ShapeFact> stable_expression_key_shape_set_for_operand(int opcode, const std::string& key,
                                                                  std::set<std::string>& seen);
bool stable_expression_key_has_structural_shape_evidence(const std::string& key,
                                                         std::set<std::string>& seen);

std::set<X2ValueFact> stable_expression_key_value_set(const std::string& key) {
  // Mirrors TS STABLE_EXPRESSION_VALUE_SET_CACHE: the recursive evaluation is
  // pure in `key` (seen always starts empty), so memoize the top-level result.
  static thread_local std::unordered_map<std::string, std::set<X2ValueFact>> cache;
  const auto found = cache.find(key);
  if (found != cache.end())
    return found->second;
  std::set<std::string> seen;
  std::set<X2ValueFact> result = stable_expression_key_value_set_for_evaluation(key, seen);
  cache.emplace(key, result);
  return result;
}

std::set<X2ShapeFact> stable_expression_key_shape_set(const std::string& key) {
  // Mirrors TS STABLE_EXPRESSION_SHAPE_SET_CACHE.
  static thread_local std::unordered_map<std::string, std::set<X2ShapeFact>> cache;
  const auto found = cache.find(key);
  if (found != cache.end())
    return found->second;
  std::set<std::string> seen;
  std::set<X2ShapeFact> result = stable_expression_key_shape_set_for_evaluation(key, seen);
  cache.emplace(key, result);
  return result;
}

std::set<X2ValueFact> stable_expression_key_value_set_for_operand(int opcode, const std::string& key,
                                                                  std::set<std::string>& seen) {
  if (opcode_has_structural_operand_semantics(opcode) &&
      stable_expression_key_has_structural_shape_evidence(key, seen))
    return {};
  return stable_expression_key_value_set_for_evaluation(key, seen);
}

bool stable_expression_key_has_structural_shape_evidence(const std::string& key,
                                                         std::set<std::string>& seen) {
  for (const X2ShapeFact& fact : stable_expression_key_shape_set_for_evaluation(key, seen)) {
    if (x2_shape_fact_safety(fact) == X2ShapeSafety::StructuralOnly)
      return true;
  }
  return false;
}

std::set<X2ShapeFact> stable_expression_key_shape_set_for_operand(int opcode, const std::string& key,
                                                                  std::set<std::string>& seen) {
  if (opcode_has_structural_operand_semantics(opcode)) {
    const std::set<X2ShapeFact> direct = stable_expression_key_direct_shape_set(key);
    if (!direct.empty())
      return direct;
  }
  return stable_expression_key_shape_set_for_evaluation(key, seen);
}

std::set<X2ValueFact> stable_expression_key_value_set_for_evaluation(const std::string& key,
                                                                     std::set<std::string>& seen) {
  std::set<X2ValueFact> values;
  const std::optional<std::string> decimal = decimal_from_fact_key(key);
  if (decimal.has_value()) {
    values.insert(decimal_value_fact(*decimal, "normalized"));
    return values;
  }
  std::set<X2ShapeFact> restored = x2_restored_display_shape_facts_from_source_key(key);
  for (const std::string& visible : x2_shape_set_restored_visible_decimals(&restored))
    values.insert(decimal_value_fact(visible, "normalized"));
  for (const std::string& result : stable_expression_key_concrete_decimal_values(key, seen))
    values.insert(decimal_value_fact(result, "normalized"));
  return values;
}

std::set<X2ShapeFact> stable_expression_key_shape_set_for_evaluation(const std::string& key,
                                                                     std::set<std::string>& seen) {
  std::set<X2ShapeFact> shapes;
  for (const X2ShapeFact& fact : x2_restored_display_shape_facts_from_source_key(key))
    shapes.insert(fact);
  for (const X2ShapeFact& fact : stable_expression_key_concrete_shape_facts(key, seen))
    shapes.insert(fact);
  return shapes;
}

std::set<std::string> stable_sign_change_expression_decimal_values(const std::string& operand,
                                                                   std::set<std::string>& seen) {
  std::set<std::string> output;
  const std::optional<std::string> direct = decimal_from_fact_key(operand);
  if (direct.has_value()) {
    output.insert(sign_changed_normalized_decimal_value(*direct));
    return output;
  }
  for (const X2ValueFact& fact : stable_expression_key_value_set_for_evaluation(operand, seen)) {
    const std::optional<std::string> value = normalized_decimal_value_from_fact(fact);
    if (value.has_value())
      output.insert(sign_changed_normalized_decimal_value(*value));
  }
  return output;
}

bool structural_sign_change_shape_key_may_hide_exponent_context(const X2ShapeFact& fact) {
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
  if (model.kind != X2ShapeModelKind::Mantissa || !model.mantissa.has_value() ||
      model.mantissa->radix != X2MantissaRadix::Hex || model.mantissa->hasDecimalPoint ||
      !has_structural_non_decimal_digit(model.mantissa->canonical))
    return false;
  const std::string unsigned_value =
      model.mantissa->sign.empty() ? model.mantissa->canonical : model.mantissa->canonical.substr(1);
  static const std::regex re(R"(0+$)");
  return std::regex_search(unsigned_value, re);
}

std::set<X2ShapeFact> stable_sign_change_expression_shape_facts(const std::string& operand,
                                                                std::set<std::string>& seen) {
  std::set<X2ShapeFact> output;
  for (const X2ShapeFact& fact : stable_expression_key_shape_set_for_evaluation(operand, seen)) {
    if (x2_shape_fact_safety(fact) != X2ShapeSafety::StructuralOnly)
      continue;
    if (structural_sign_change_shape_key_may_hide_exponent_context(fact))
      continue;
    std::optional<X2ShapeFact> signed_fact = x2_exponent_mantissa_sign_changed_shape_fact(fact);
    if (!signed_fact.has_value())
      signed_fact = x2_mantissa_sign_changed_shape_fact(fact);
    if (!signed_fact.has_value())
      continue;
    const std::optional<X2ShapeFact> canonical = x2_canonical_shape_fact_if_valid(*signed_fact);
    if (canonical.has_value() && x2_shape_fact_safety(*canonical) == X2ShapeSafety::StructuralOnly)
      output.insert(*canonical);
  }
  return output;
}

std::set<std::string> stable_expression_key_concrete_decimal_values(const std::string& key,
                                                                    std::set<std::string>& seen) {
  std::set<std::string> output;
  const std::optional<ParsedStableExpressionKey> parsed = parse_stable_expression_key(key);
  if (!parsed.has_value() || seen.count(key) != 0)
    return output;
  seen.insert(key);
  if (parsed->opcode == X2_SIGN_CHANGE_OPCODE) {
    if (parsed->operands.size() == 1) {
      for (const std::string& value : stable_sign_change_expression_decimal_values(parsed->operands[0], seen))
        output.insert(value);
    }
    seen.erase(key);
    return output;
  }
  const std::optional<int> arity = stable_expression_opcode_arity(parsed->opcode);
  if (!arity.has_value() || static_cast<std::size_t>(*arity) != parsed->operands.size()) {
    seen.erase(key);
    return output;
  }
  const IrOp op = stable_expression_plain_op(parsed->opcode);
  const ConcreteEvaluationOptions options = concrete_evaluation_options_for_stable_expression_opcode(parsed->opcode);
  if (parsed->operands.empty()) {
    const std::optional<X2ValueFact> constant = plain_produces_stable_constant_decimal_value(op);
    const std::optional<std::string> value =
        constant.has_value() ? normalized_decimal_value_from_fact(*constant) : std::nullopt;
    if (value.has_value())
      output.insert(*value);
  } else if (parsed->operands.size() == 1) {
    const std::string& x_key = parsed->operands[0];
    const std::set<X2ValueFact> xv = stable_expression_key_value_set_for_operand(parsed->opcode, x_key, seen);
    const std::set<X2ShapeFact> xs = stable_expression_key_shape_set_for_operand(parsed->opcode, x_key, seen);
    const std::set<X2ShapeFact> xd = stable_expression_key_direct_shape_set(x_key);
    for (const X2ValueFact& fact : plain_produces_concrete_decimal_values(op, &xv, &xs, options, &xd)) {
      const std::optional<std::string> value = normalized_decimal_value_from_fact(fact);
      if (value.has_value())
        output.insert(*value);
    }
  } else if (parsed->operands.size() == 2) {
    const std::string& y_key = parsed->operands[0];
    const std::string& x_key = parsed->operands[1];
    const std::set<X2ValueFact> yv = stable_expression_key_value_set_for_operand(parsed->opcode, y_key, seen);
    const std::set<X2ValueFact> xv = stable_expression_key_value_set_for_operand(parsed->opcode, x_key, seen);
    const std::set<X2ShapeFact> ys = stable_expression_key_shape_set_for_operand(parsed->opcode, y_key, seen);
    const std::set<X2ShapeFact> xs = stable_expression_key_shape_set_for_operand(parsed->opcode, x_key, seen);
    const std::set<X2ShapeFact> yd = stable_expression_key_direct_shape_set(y_key);
    const std::set<X2ShapeFact> xd = stable_expression_key_direct_shape_set(x_key);
    for (const X2ValueFact& fact :
         plain_produces_concrete_binary_decimal_values(op, &yv, &xv, &ys, &xs, options, &yd, &xd)) {
      const std::optional<std::string> value = normalized_decimal_value_from_fact(fact);
      if (value.has_value())
        output.insert(*value);
    }
  }
  seen.erase(key);
  return output;
}

std::set<X2ShapeFact> stable_expression_key_concrete_shape_facts(const std::string& key,
                                                                 std::set<std::string>& seen) {
  std::set<X2ShapeFact> output;
  const std::optional<ParsedStableExpressionKey> parsed = parse_stable_expression_key(key);
  if (!parsed.has_value() || seen.count(key) != 0)
    return output;
  seen.insert(key);
  if (parsed->opcode == X2_SIGN_CHANGE_OPCODE) {
    if (parsed->operands.size() == 1) {
      for (const std::string& value : stable_sign_change_expression_decimal_values(parsed->operands[0], seen)) {
        const std::optional<X2ShapeFact> shape = exact_decimal_display_shape_fact(value);
        if (shape.has_value())
          output.insert(*shape);
      }
      for (const X2ShapeFact& shape : stable_sign_change_expression_shape_facts(parsed->operands[0], seen))
        output.insert(shape);
    }
    seen.erase(key);
    return output;
  }
  const std::optional<int> arity = stable_expression_opcode_arity(parsed->opcode);
  if (!arity.has_value() || static_cast<std::size_t>(*arity) != parsed->operands.size()) {
    seen.erase(key);
    return output;
  }
  const IrOp op = stable_expression_plain_op(parsed->opcode);
  const ConcreteEvaluationOptions options = concrete_evaluation_options_for_stable_expression_opcode(parsed->opcode);
  if (parsed->operands.empty()) {
    for (const X2ShapeFact& fact : plain_produces_stable_constant_shape_facts(op))
      output.insert(fact);
  } else if (parsed->operands.size() == 1) {
    const std::string& x_key = parsed->operands[0];
    const std::set<X2ValueFact> xv = stable_expression_key_value_set_for_operand(parsed->opcode, x_key, seen);
    const std::set<X2ShapeFact> xs = stable_expression_key_shape_set_for_operand(parsed->opcode, x_key, seen);
    const std::set<X2ShapeFact> xd = stable_expression_key_direct_shape_set(x_key);
    for (const X2ShapeFact& fact :
         plain_x_shape_after_non_preserving_op(op, &xv, nullptr, &xs, nullptr, options, nullptr, &xd))
      output.insert(fact);
  } else if (parsed->operands.size() == 2) {
    const std::string& y_key = parsed->operands[0];
    const std::string& x_key = parsed->operands[1];
    const std::set<X2ValueFact> yv = stable_expression_key_value_set_for_operand(parsed->opcode, y_key, seen);
    const std::set<X2ValueFact> xv = stable_expression_key_value_set_for_operand(parsed->opcode, x_key, seen);
    const std::set<X2ShapeFact> ys = stable_expression_key_shape_set_for_operand(parsed->opcode, y_key, seen);
    const std::set<X2ShapeFact> xs = stable_expression_key_shape_set_for_operand(parsed->opcode, x_key, seen);
    const std::set<X2ShapeFact> yd = stable_expression_key_direct_shape_set(y_key);
    const std::set<X2ShapeFact> xd = stable_expression_key_direct_shape_set(x_key);
    for (const X2ShapeFact& fact :
         plain_x_shape_after_non_preserving_op(op, &xv, &yv, &xs, &ys, options, &yd, &xd))
      output.insert(fact);
  }
  seen.erase(key);
  return output;
}

// --- shapeSetWithStableExpressionValueShapes --------------------------
std::set<X2ShapeFact> shape_set_with_stable_expression_value_shapes(const X2ShapeSet* shapes,
                                                                    const X2ValueSet* values) {
  std::set<X2ShapeFact> output = canonical_shape_set(shapes);
  if (values != nullptr) {
    for (const X2ValueFact& value : *values) {
      if (value.rfind("expr-key:", 0) != 0)
        continue;
      for (const X2ShapeFact& shape : stable_expression_key_shape_set(value))
        output.insert(shape);
    }
  }
  return output;
}

// --- A.3 decimal concrete-evaluation ----------------------------------
std::set<X2ValueFact> plain_produces_concrete_decimal_values(const IrOp& op, const X2ValueSet* x,
                                                             const X2ShapeSet* x_shape,
                                                             const ConcreteEvaluationOptions& options,
                                                             const X2ShapeSet* direct_x_shape) {
  std::set<X2ValueFact> output;
  const ConcreteEvaluationOptions effective_options = concrete_evaluation_options_for_opcode(op.opcode, options);
  const std::set<X2ShapeFact> effective_x_shape = shape_set_with_stable_expression_value_shapes(x_shape, x);
  const int opcode = op.opcode;
  if (opcode != 0x15 && opcode != 0x16 && opcode != 0x17 && opcode != 0x18 && opcode != 0x19 &&
      opcode != 0x1a && opcode != 0x1b && opcode != 0x1c && opcode != 0x1d && opcode != 0x1e &&
      opcode != 0x21 && opcode != 0x22 && opcode != 0x23 && opcode != 0x26 && opcode != 0x2a &&
      opcode != 0x30 && opcode != 0x31 && opcode != 0x32 && opcode != 0x33 && opcode != 0x34 &&
      opcode != 0x35 && opcode != 0x3a)
    return output;
  if (x != nullptr) {
    for (const X2ValueFact& fact : *x) {
      const std::optional<std::string> value = computation_decimal_value_from_fact(fact);
      const std::optional<std::string> concrete =
          value.has_value() ? concrete_decimal_unary_value(opcode, *value) : std::nullopt;
      if (concrete.has_value())
        output.insert(decimal_value_fact(*concrete, "normalized"));
    }
  }
  for (const std::string& value : computation_decimal_shape_values(&effective_x_shape, effective_options)) {
    const std::optional<std::string> concrete = concrete_decimal_unary_value(opcode, value);
    if (concrete.has_value())
      output.insert(decimal_value_fact(*concrete, "normalized"));
  }
  for (const std::string& value :
       plain_produces_concrete_structural_unary_decimal_values(opcode, &effective_x_shape))
    output.insert(decimal_value_fact(value, "normalized"));
  for (const std::string& value :
       plain_produces_concrete_direct_structural_unary_decimal_values(opcode, direct_x_shape))
    output.insert(decimal_value_fact(value, "normalized"));
  return output;
}

std::set<X2ShapeFact> plain_produces_concrete_decimal_shape_facts(const IrOp& op, const X2ValueSet* x,
                                                                  const X2ShapeSet* x_shape,
                                                                  const ConcreteEvaluationOptions& options,
                                                                  const X2ShapeSet* direct_x_shape) {
  std::set<X2ShapeFact> output = plain_produces_stable_constant_shape_facts(op);
  const ConcreteEvaluationOptions effective_options = concrete_evaluation_options_for_opcode(op.opcode, options);
  const std::set<X2ShapeFact> effective_x_shape = shape_set_with_stable_expression_value_shapes(x_shape, x);
  const int opcode = op.opcode;
  if (x != nullptr) {
    for (const X2ValueFact& fact : *x) {
      const std::optional<std::string> value = computation_decimal_value_from_fact(fact);
      const std::optional<X2ShapeFact> concrete =
          value.has_value() ? concrete_decimal_unary_display_shape_fact(opcode, *value) : std::nullopt;
      if (concrete.has_value())
        output.insert(*concrete);
    }
  }
  for (const std::string& value : computation_decimal_shape_values(&effective_x_shape, effective_options)) {
    const std::optional<X2ShapeFact> concrete = concrete_decimal_unary_display_shape_fact(opcode, value);
    if (concrete.has_value())
      output.insert(*concrete);
  }
  for (const X2ShapeFact& fact :
       plain_produces_concrete_structural_unary_decimal_shape_facts(opcode, &effective_x_shape))
    output.insert(fact);
  for (const X2ShapeFact& fact :
       plain_produces_concrete_direct_structural_unary_decimal_shape_facts(opcode, direct_x_shape))
    output.insert(fact);
  return output;
}

std::set<std::string> structural_hex_binary_decimal_values(int opcode, const X2ValueSet* y,
                                                           const X2ValueSet* x, const X2ShapeSet* y_shape,
                                                           const X2ShapeSet* x_shape,
                                                           const X2ShapeSet* direct_y_shape,
                                                           const X2ShapeSet* direct_x_shape) {
  std::set<std::string> output;
  for (const StructuralHexDecimalProduct& product : structural_hex_binary_decimal_products(
           opcode, y, x, y_shape, x_shape, direct_y_shape, direct_x_shape))
    output.insert(product.value);
  for (const std::string& value : structural_bitwise_decimal_values(opcode, y, x, y_shape, x_shape))
    output.insert(value);
  return output;
}

std::set<X2ValueFact> plain_produces_structural_binary_decimal_values(
    const IrOp& op, const X2ValueSet* y, const X2ValueSet* x, const X2ShapeSet* y_shape,
    const X2ShapeSet* x_shape, const X2ShapeSet* direct_y_shape, const X2ShapeSet* direct_x_shape) {
  std::set<X2ValueFact> output;
  for (const std::string& value :
       structural_hex_binary_decimal_values(op.opcode, y, x, y_shape, x_shape, direct_y_shape, direct_x_shape))
    output.insert(decimal_value_fact(value, "normalized"));
  return output;
}

std::set<X2ShapeFact> plain_produces_structural_binary_decimal_shapes(
    const IrOp& op, const X2ValueSet* y, const X2ValueSet* x, const X2ShapeSet* y_shape,
    const X2ShapeSet* x_shape, const X2ShapeSet* direct_y_shape, const X2ShapeSet* direct_x_shape) {
  std::set<X2ShapeFact> output;
  for (const X2ShapeFact& fact : structural_hex_binary_decimal_display_shapes(
           op.opcode, y, x, y_shape, x_shape, direct_y_shape, direct_x_shape))
    output.insert(fact);
  for (const X2ShapeFact& fact : structural_bitwise_decimal_display_shapes(op.opcode, y, x, y_shape, x_shape))
    output.insert(fact);
  return output;
}

std::set<X2ValueFact> plain_produces_concrete_binary_decimal_values(
    const IrOp& op, const X2ValueSet* y, const X2ValueSet* x, const X2ShapeSet* y_shape,
    const X2ShapeSet* x_shape, const ConcreteEvaluationOptions& options, const X2ShapeSet* direct_y_shape,
    const X2ShapeSet* direct_x_shape) {
  std::set<X2ValueFact> output;
  const ConcreteEvaluationOptions effective_options = concrete_evaluation_options_for_opcode(op.opcode, options);
  const std::set<X2ShapeFact> effective_y_shape = shape_set_with_stable_expression_value_shapes(y_shape, y);
  const std::set<X2ShapeFact> effective_x_shape = shape_set_with_stable_expression_value_shapes(x_shape, x);
  const int opcode = op.opcode;
  if ((opcode < 0x10 || opcode > 0x13) && opcode != 0x24 && opcode != 0x36 && opcode != 0x37 &&
      opcode != 0x38 && opcode != 0x39)
    return output;
  const std::set<std::string> y_values = normalized_decimal_values(y, &effective_y_shape, effective_options);
  const std::set<std::string> x_values = normalized_decimal_values(x, &effective_x_shape, effective_options);
  for (const std::string& y_value : y_values) {
    for (const std::string& x_value : x_values) {
      const std::optional<std::string> concrete = concrete_decimal_binary_value(opcode, y_value, x_value);
      if (concrete.has_value())
        output.insert(decimal_value_fact(*concrete, "normalized"));
    }
  }
  for (const X2ValueFact& fact : plain_produces_structural_binary_decimal_values(
           op, y, x, &effective_y_shape, &effective_x_shape, direct_y_shape, direct_x_shape))
    output.insert(fact);
  return output;
}

std::optional<X2ShapeFact> structural_bitwise_not_mantissa_shape_fact(const StructuralBitwiseOperand& operand) {
  const std::optional<std::vector<int>> result = structural_bitwise_not_nibbles(operand);
  if (!result.has_value())
    return std::nullopt;
  bool has_hex_cell = false;
  for (int digit : *result) {
    if (digit > 9) {
      has_hex_cell = true;
      break;
    }
  }
  if (!has_hex_cell && !operand.structural)
    return std::nullopt;
  return x2_mantissa_shape_fact_from_parts(X2MantissaRadix::Hex, bitwise_mantissa_raw(*result));
}

std::set<X2ShapeFact> structural_abs_mantissa_shape_facts(const X2ShapeSet* shapes) {
  std::set<X2ShapeFact> output;
  for (const X2ShapeFact& fact : structural_restore_shape_facts(canonical_structural_shape_facts(shapes))) {
    const std::optional<X2ShapeFact> result = structural_abs_mantissa_shape_fact(fact);
    if (result.has_value())
      output.insert(*result);
  }
  return output;
}

std::set<X2ShapeFact> plain_produces_concrete_unary_shape_facts(const IrOp& op, const X2ValueSet* x,
                                                                const X2ShapeSet* x_shape) {
  std::set<X2ShapeFact> output;
  const std::set<X2ShapeFact> effective_x_shape = shape_set_with_stable_expression_value_shapes(x_shape, x);
  if (op.opcode == 0x31) {
    for (const X2ShapeFact& fact : structural_abs_mantissa_shape_facts(&effective_x_shape))
      output.insert(fact);
    return output;
  }
  if (op.opcode != 0x3a)
    return output;
  for (const StructuralBitwiseOperand& operand : bitwise_operands_from_values_and_shapes(x, &effective_x_shape)) {
    const std::optional<X2ShapeFact> result = structural_bitwise_not_mantissa_shape_fact(operand);
    if (result.has_value())
      output.insert(*result);
  }
  return output;
}

std::set<X2ShapeFact> plain_produces_concrete_decimal_binary_shape_facts(
    const IrOp& op, const X2ValueSet* y, const X2ValueSet* x, const X2ShapeSet* y_shape,
    const X2ShapeSet* x_shape, const ConcreteEvaluationOptions& options) {
  std::set<X2ShapeFact> output;
  if (exact_decimal_binary_mantissa_shape_opcodes().count(op.opcode) == 0)
    return output;
  const std::set<std::string> y_values = normalized_decimal_values(y, y_shape, options);
  const std::set<std::string> x_values = normalized_decimal_values(x, x_shape, options);
  for (const std::string& y_value : y_values) {
    for (const std::string& x_value : x_values) {
      const std::optional<std::string> concrete = concrete_decimal_binary_value(op.opcode, y_value, x_value);
      const std::optional<X2ShapeFact> shape =
          concrete.has_value() ? exact_decimal_display_shape_fact(*concrete) : std::nullopt;
      if (shape.has_value())
        output.insert(*shape);
    }
  }
  return output;
}

std::set<X2ShapeFact> plain_produces_concrete_binary_shape_facts(
    const IrOp& op, const X2ValueSet* y, const X2ValueSet* x, const X2ShapeSet* y_shape,
    const X2ShapeSet* x_shape, const ConcreteEvaluationOptions& options, const X2ShapeSet* direct_y_shape,
    const X2ShapeSet* direct_x_shape) {
  std::set<X2ShapeFact> output;
  const ConcreteEvaluationOptions effective_options = concrete_evaluation_options_for_opcode(op.opcode, options);
  const std::set<X2ShapeFact> effective_y_shape = shape_set_with_stable_expression_value_shapes(y_shape, y);
  const std::set<X2ShapeFact> effective_x_shape = shape_set_with_stable_expression_value_shapes(x_shape, x);
  for (const X2ShapeFact& fact : plain_produces_concrete_decimal_binary_shape_facts(
           op, y, x, &effective_y_shape, &effective_x_shape, effective_options))
    output.insert(fact);
  for (const X2ShapeFact& fact : plain_produces_structural_binary_decimal_shapes(
           op, y, x, &effective_y_shape, &effective_x_shape, direct_y_shape, direct_x_shape))
    output.insert(fact);
  if (op.opcode < 0x37 || op.opcode > 0x39)
    return output;
  const std::vector<StructuralBitwiseOperand> left = bitwise_operands_from_values_and_shapes(y, &effective_y_shape);
  const std::vector<StructuralBitwiseOperand> right = bitwise_operands_from_values_and_shapes(x, &effective_x_shape);
  for (const StructuralBitwiseOperand& left_operand : left) {
    for (const StructuralBitwiseOperand& right_operand : right) {
      const std::optional<X2ShapeFact> result =
          structural_bitwise_mantissa_shape_fact(op.opcode, left_operand, right_operand);
      if (result.has_value())
        output.insert(*result);
    }
  }
  return output;
}

std::set<X2ValueFact> plain_x_value_after_non_preserving_op(const IrOp& op,
                                                            std::optional<int> producer_index,
                                                            const X2ValueSet* x, const X2ValueSet* y,
                                                            const X2ShapeSet* x_shape,
                                                            const X2ShapeSet* y_shape,
                                                            const X2ShapeSet* direct_y_shape,
                                                            const X2ShapeSet* direct_x_shape) {
  std::set<X2ValueFact> output =
      plain_produces_stable_expression_values(op, x, y, x_shape, y_shape, direct_y_shape, direct_x_shape);
  const std::optional<X2ValueFact> opaque = plain_produces_opaque_expression_value(op, producer_index);
  if (opaque.has_value())
    output.insert(*opaque);
  return output;
}

std::set<X2ShapeFact> plain_x_shape_after_non_preserving_op(
    const IrOp& op, const X2ValueSet* x, const X2ValueSet* y, const X2ShapeSet* x_shape,
    const X2ShapeSet* y_shape, const ConcreteEvaluationOptions& options, const X2ShapeSet* direct_y_shape,
    const X2ShapeSet* direct_x_shape) {
  std::set<X2ShapeFact> output = plain_produces_concrete_decimal_shape_facts(op, x, x_shape, options, direct_x_shape);
  for (const X2ShapeFact& fact : plain_produces_concrete_unary_shape_facts(op, x, x_shape))
    output.insert(fact);
  for (const X2ShapeFact& fact : plain_produces_concrete_binary_shape_facts(
           op, y, x, y_shape, x_shape, options, direct_y_shape, direct_x_shape))
    output.insert(fact);
  return output;
}

// --- A.4 stable / opaque expression values ----------------------------
std::optional<X2ValueFact> plain_produces_opaque_expression_value(const IrOp& op,
                                                                  std::optional<int> producer_index) {
  if (!producer_index.has_value())
    return std::nullopt;
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op) || has_ir_roles(op))
    return std::nullopt;
  if (pure_opaque_expr_opcodes().count(op.opcode) == 0)
    return std::nullopt;
  const OpcodeInfo& info = opcode_by_code(op.opcode);
  if (info.risk != OpcodeRisk::Documented || info.x2_effect != X2Effect::Preserves ||
      info.stack_effect == StackEffect::Barrier || info.stack_effect == StackEffect::Unknown ||
      info.stack_effect == StackEffect::Exposes || info.stack_effect == StackEffect::Shifts)
    return std::nullopt;
  return expression_value_fact(*producer_index);
}

bool stable_expression_can_canonicalize_binary_operand_order(int opcode, bool structural_operand) {
  if (commutative_stable_expr_opcodes().count(opcode) == 0)
    return false;
  if (!structural_operand)
    return true;
  return opcode == 0x37 || opcode == 0x38 || opcode == 0x39;
}

X2ValueFact stable_binary_expression_value_fact(const IrOp& op, const std::string& opcode,
                                                const std::string& y_key, const std::string& x_key) {
  std::set<std::string> y_seen;
  std::set<std::string> x_seen;
  const bool structural_operand =
      opcode_has_structural_operand_semantics(op.opcode) &&
      (stable_expression_key_has_structural_shape_evidence(y_key, y_seen) ||
       stable_expression_key_has_structural_shape_evidence(x_key, x_seen));
  std::vector<std::string> operands{y_key, x_key};
  if (stable_expression_can_canonicalize_binary_operand_order(op.opcode, structural_operand))
    std::sort(operands.begin(), operands.end());
  return stable_expression_value_fact(opcode, operands[0] + "," + operands[1]);
}

bool shape_set_has_additional_display_evidence(const X2ShapeSet* shapes, const X2ValueSet& values) {
  const std::set<std::string> normalized_values = normalized_decimal_value_set(&values);
  if (shapes != nullptr) {
    for (const X2ShapeFact& fact : *shapes) {
      if (is_signed_zero_decimal_mantissa_shape_fact(fact) && normalized_values.count("0") != 0)
        continue;
      const std::optional<std::string> restored = x2_shape_fact_restored_visible_decimal(fact);
      if (!restored.has_value() || normalized_values.count(*restored) == 0)
        return true;
    }
  }
  return false;
}

bool stable_unary_expression_key_has_additional_shape_result(const IrOp& op, const std::string& key,
                                                             const X2ValueSet& values,
                                                             const ConcreteEvaluationOptions& options) {
  std::set<std::string> v_seen;
  std::set<std::string> s_seen;
  const std::set<X2ValueFact> v = stable_expression_key_value_set_for_operand(op.opcode, key, v_seen);
  const std::set<X2ShapeFact> s = stable_expression_key_shape_set_for_operand(op.opcode, key, s_seen);
  const std::set<X2ShapeFact> d = stable_expression_key_direct_shape_set(key);
  const std::set<X2ShapeFact> shapes =
      plain_x_shape_after_non_preserving_op(op, &v, nullptr, &s, nullptr, options, nullptr, &d);
  return shape_set_has_additional_display_evidence(&shapes, values);
}

bool stable_binary_expression_key_has_additional_shape_result(const IrOp& op, const std::string& y_key,
                                                              const std::string& x_key,
                                                              const X2ValueSet& values,
                                                              const ConcreteEvaluationOptions& options) {
  std::set<std::string> yv_seen;
  std::set<std::string> xv_seen;
  std::set<std::string> ys_seen;
  std::set<std::string> xs_seen;
  const std::set<X2ValueFact> xv = stable_expression_key_value_set_for_operand(op.opcode, x_key, xv_seen);
  const std::set<X2ValueFact> yv = stable_expression_key_value_set_for_operand(op.opcode, y_key, yv_seen);
  const std::set<X2ShapeFact> xs = stable_expression_key_shape_set_for_operand(op.opcode, x_key, xs_seen);
  const std::set<X2ShapeFact> ys = stable_expression_key_shape_set_for_operand(op.opcode, y_key, ys_seen);
  const std::set<X2ShapeFact> yd = stable_expression_key_direct_shape_set(y_key);
  const std::set<X2ShapeFact> xd = stable_expression_key_direct_shape_set(x_key);
  const std::set<X2ShapeFact> shapes =
      plain_x_shape_after_non_preserving_op(op, &xv, &yv, &xs, &ys, options, &yd, &xd);
  return shape_set_has_additional_display_evidence(&shapes, values);
}

bool stable_expression_key_has_concrete_decimal_result(const IrOp& op, const std::string& key) {
  const ConcreteEvaluationOptions options = concrete_evaluation_options_for_stable_expression_opcode(op.opcode);
  std::set<std::string> v_seen;
  std::set<std::string> s_seen;
  const std::set<X2ValueFact> v = stable_expression_key_value_set_for_operand(op.opcode, key, v_seen);
  const std::set<X2ShapeFact> s = stable_expression_key_shape_set_for_operand(op.opcode, key, s_seen);
  const std::set<X2ShapeFact> d = stable_expression_key_direct_shape_set(key);
  const std::set<X2ValueFact> values = plain_produces_concrete_decimal_values(op, &v, &s, options, &d);
  return !values.empty() &&
         !stable_unary_expression_key_has_additional_shape_result(op, key, values, options);
}

bool stable_binary_expression_key_has_concrete_decimal_result(const IrOp& op, const std::string& y_key,
                                                              const std::string& x_key) {
  const ConcreteEvaluationOptions options = concrete_evaluation_options_for_stable_expression_opcode(op.opcode);
  std::set<std::string> yv_seen;
  std::set<std::string> xv_seen;
  std::set<std::string> ys_seen;
  std::set<std::string> xs_seen;
  const std::set<X2ValueFact> yv = stable_expression_key_value_set_for_operand(op.opcode, y_key, yv_seen);
  const std::set<X2ValueFact> xv = stable_expression_key_value_set_for_operand(op.opcode, x_key, xv_seen);
  const std::set<X2ShapeFact> ys = stable_expression_key_shape_set_for_operand(op.opcode, y_key, ys_seen);
  const std::set<X2ShapeFact> xs = stable_expression_key_shape_set_for_operand(op.opcode, x_key, xs_seen);
  const std::set<X2ShapeFact> yd = stable_expression_key_direct_shape_set(y_key);
  const std::set<X2ShapeFact> xd = stable_expression_key_direct_shape_set(x_key);
  const std::set<X2ValueFact> values =
      plain_produces_concrete_binary_decimal_values(op, &yv, &xv, &ys, &xs, options, &yd, &xd);
  return !values.empty() &&
         !stable_binary_expression_key_has_additional_shape_result(op, y_key, x_key, values, options);
}

std::pair<std::set<std::string>, std::set<std::string>> stable_binary_expression_source_key_sets(
    int opcode, const X2ValueSet* y, const X2ShapeSet* y_shape, const X2ValueSet* x,
    const X2ShapeSet* x_shape) {
  const std::set<std::string> y_keys = stable_expression_source_keys_for_operand(opcode, y, y_shape, true);
  const std::set<std::string> x_keys = stable_expression_source_keys_for_operand(opcode, x, x_shape, true);
  const std::set<std::string> y_stable_keys =
      stable_expression_source_keys_for_operand(opcode, y, y_shape, false);
  const std::set<std::string> x_stable_keys =
      stable_expression_source_keys_for_operand(opcode, x, x_shape, false);
  const std::set<std::string>& bounded_y_keys = y_stable_keys.empty() ? y_keys : y_stable_keys;
  const std::set<std::string>& bounded_x_keys = x_stable_keys.empty() ? x_keys : x_stable_keys;
  if (bounded_y_keys.size() * bounded_x_keys.size() <= MAX_STABLE_BINARY_SOURCE_KEY_PAIRS)
    return {bounded_y_keys, bounded_x_keys};
  return {y_stable_keys, x_stable_keys};
}

bool direct_structural_unary_decimal_proof_is_complete(const IrOp& op, const X2ShapeSet* direct_x_shape) {
  const std::set<X2ShapeFact> facts = canonical_structural_shape_facts(direct_x_shape);
  if (facts.empty())
    return false;
  for (const X2ShapeFact& fact : facts) {
    const std::optional<std::string> value =
        op.opcode == 0x32 ? direct_structural_sign_decimal_value(fact) : std::nullopt;
    if (!value.has_value())
      return false;
  }
  return true;
}

std::optional<std::set<std::string>> direct_structural_unary_concrete_stable_source_keys(
    const IrOp& op, const X2ShapeSet* direct_x_shape) {
  if (!direct_structural_unary_decimal_proof_is_complete(op, direct_x_shape))
    return std::nullopt;
  std::set<std::string> keys;
  for (const std::string& key : stable_expression_display_shape_source_keys(nullptr, direct_x_shape))
    keys.insert(key);
  for (const std::string& register_name : register_names())
    keys.insert(register_value_fact(register_name));
  return keys;
}

std::set<X2ValueFact> plain_produces_stable_expression_values(const IrOp& op, const X2ValueSet* x,
                                                              const X2ValueSet* y, const X2ShapeSet* x_shape,
                                                              const X2ShapeSet* y_shape,
                                                              const X2ShapeSet* direct_y_shape,
                                                              const X2ShapeSet* direct_x_shape) {
  std::set<X2ValueFact> constants = plain_produces_stable_constant_expression_values(op);
  if (!constants.empty())
    return constants;
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op) || has_ir_roles(op))
    return {};
  if (pure_opaque_expr_opcodes().count(op.opcode) == 0)
    return {};
  const OpcodeInfo& info = opcode_by_code(op.opcode);
  if (info.risk != OpcodeRisk::Documented || info.x2_effect != X2Effect::Preserves ||
      (info.stack_effect != StackEffect::Preserves && info.stack_effect != StackEffect::ConsumeYDrop &&
       info.stack_effect != StackEffect::ConsumeYKeep))
    return {};
  const std::string opcode = stable_expression_opcode_hex(op.opcode);
  const ConcreteEvaluationOptions options = concrete_evaluation_options_for_stable_expression_opcode(op.opcode);
  std::set<X2ValueFact> output = plain_produces_concrete_decimal_values(op, x, x_shape, options, direct_x_shape);
  const std::optional<std::set<std::string>> direct_concrete_keys =
      direct_structural_unary_concrete_stable_source_keys(op, direct_x_shape);
  if (info.stack_effect == StackEffect::Preserves) {
    for (const std::string& key : stable_expression_source_keys(x, x_shape, true)) {
      if (stable_expression_key_has_concrete_decimal_result(op, key))
        continue;
      if (direct_concrete_keys.has_value() && direct_concrete_keys->count(key) != 0)
        continue;
      output.insert(stable_expression_value_fact(opcode, key));
    }
  } else if (info.stack_effect == StackEffect::ConsumeYDrop ||
             info.stack_effect == StackEffect::ConsumeYKeep) {
    for (const X2ValueFact& fact : plain_produces_concrete_binary_decimal_values(
             op, y, x, y_shape, x_shape, options, direct_y_shape, direct_x_shape))
      output.insert(fact);
    const std::pair<std::set<std::string>, std::set<std::string>> key_sets =
        stable_binary_expression_source_key_sets(op.opcode, y, y_shape, x, x_shape);
    for (const std::string& y_key : key_sets.first) {
      for (const std::string& x_key : key_sets.second) {
        if (stable_binary_expression_key_has_concrete_decimal_result(op, y_key, x_key))
          continue;
        output.insert(stable_binary_expression_value_fact(op, opcode, y_key, x_key));
      }
    }
  }
  return output;
}

// === Session B: entry / VP / structural state-machine builders ============
// Faithful 1:1 port of the X2 entry/VP/structural state constructors and the
// advance / sign-change / closed-exponent state builders from helpers.ts.
// Additive dead code (not wired into the Plain branch yet); exercised by the
// x2_state_builders differential unit test.

// Forward declarations for mutual recursion within this layer.
X2ValueSet canonical_x2_value_set(const X2ValueSet* input);
X2EntryState x2_entry_state_from_open_raw(const RegisterValueSet* input);
X2EntryState x2_entry_state_from_exponent_parts(const RegisterValueSet* mantissa_input,
                                                const RegisterValueSet* exponent_input);
X2StructuralEntryState x2_structural_entry_state_from_parts(const X2ShapeSet* mantissa_input,
                                                            const RegisterValueSet* exponent_input);
X2VpContextState x2_vp_context_state_from_exponent_parts(const RegisterValueSet* mantissa_input,
                                                         const RegisterValueSet* exponent_input);
X2EntryState advance_exponent_digit_entry(const X2EntryState& input, const std::string& digit);
std::set<X2ValueFact> closed_exponent_entry_decimal_facts(const X2EntryState& input);
std::set<X2ShapeFact> closed_exponent_entry_shape_facts(const X2EntryState& input);
std::set<X2ShapeFact> structural_exponent_entry_shape_facts(const X2StructuralEntryState& input);
std::optional<X2ShapeSet> vp_entry_shapes_from_shape_facts(const X2ShapeSet* shapes);
std::optional<X2ShapeSet> vp_entry_sign_shapes_from_shape_facts(const X2ShapeSet* shapes);

// --- leaf set / fact helpers ----------------------------------------------
X2ValueSet canonical_x2_value_set(const X2ValueSet* input) {
  X2ValueSet output;
  if (input == nullptr)
    return output;
  for (const X2ValueFact& fact : *input) {
    const std::optional<X2ValueFact> canonical =
        fact.rfind("expr-key:", 0) == 0 ? canonical_stable_expression_value_fact_if_valid(fact)
                                        : std::optional<X2ValueFact>{fact};
    if (canonical.has_value())
      output.insert(*canonical);
  }
  return output;
}

X2ValueSet clone_optional_value_set(const X2ValueSet* input) {
  if (input == nullptr)
    return X2ValueSet{};
  return canonical_x2_value_set(input);
}

X2ShapeSet clone_optional_shape_set(const X2ShapeSet* input) {
  return canonical_shape_set(input);
}

std::optional<RegisterValueSet> clone_optional_string_set(const RegisterValueSet* input) {
  if (input == nullptr)
    return std::nullopt;
  return RegisterValueSet{input->begin(), input->end()};
}

std::optional<X2ShapeSet> merge_optional_shape_sources(
    std::initializer_list<const X2ShapeSet*> sources) {
  X2ShapeSet output;
  for (const X2ShapeSet* source : sources) {
    if (source == nullptr)
      continue;
    for (const X2ShapeFact& fact : *source) {
      const std::optional<X2ShapeFact> canonical = x2_canonical_shape_fact_if_valid(fact);
      if (canonical.has_value())
        output.insert(*canonical);
    }
  }
  return output.empty() ? std::nullopt : std::optional<X2ShapeSet>{output};
}

X2ValueMemory clone_x2_value_memory(const X2ValueMemory& input) {
  X2ValueMemory output;
  for (const auto& [register_name, values] : input) {
    if (values.empty())
      continue;
    output[register_name] = canonical_x2_value_set(&values);
  }
  return output;
}

X2ShapeMemory clone_x2_shape_memory(const X2ShapeMemory& input) {
  X2ShapeMemory output;
  for (const auto& [register_name, shapes] : input) {
    const X2ShapeSet canonical = canonical_shape_set(&shapes);
    if (!canonical.empty())
      output[register_name] = canonical;
  }
  return output;
}

std::optional<X2ValueMemory> clone_x2_value_memory_field(const std::optional<X2ValueMemory>& memory) {
  if (!memory.has_value())
    return std::nullopt;
  return clone_x2_value_memory(*memory);
}

std::optional<X2ShapeMemory> clone_x2_shape_memory_field(const std::optional<X2ShapeMemory>& memory) {
  if (!memory.has_value())
    return std::nullopt;
  return clone_x2_shape_memory(*memory);
}

// Faithful port of isUnstableOpaqueExpressionValueFact: matches /^expr:\d+$/.
bool is_unstable_opaque_expression_value_fact(const X2ValueFact& fact) {
  if (fact.rfind("expr:", 0) != 0 || fact.size() <= 5)
    return false;
  for (std::size_t index = 5; index < fact.size(); ++index) {
    if (!std::isdigit(static_cast<unsigned char>(fact[index])))
      return false;
  }
  return true;
}

// Faithful port of removeUnstableOpaqueExpressionValueFacts: canonicalizes
// surviving facts (matching canonical_x2_value_set), dropping `expr:N` facts.
X2ValueSet remove_unstable_opaque_expression_value_facts(const X2ValueSet* input) {
  X2ValueSet output;
  if (input == nullptr)
    return output;
  for (const X2ValueFact& fact : *input) {
    if (is_unstable_opaque_expression_value_fact(fact))
      continue;
    const std::optional<X2ValueFact> canonical =
        fact.rfind("expr-key:", 0) == 0 ? canonical_stable_expression_value_fact_if_valid(fact)
                                        : std::optional<X2ValueFact>{fact};
    if (canonical.has_value())
      output.insert(*canonical);
  }
  return output;
}

std::optional<X2ValueMemory> remove_unstable_opaque_expression_value_memory(
    const std::optional<X2ValueMemory>& input) {
  X2ValueMemory output;
  if (input.has_value()) {
    for (const auto& [register_name, values] : *input) {
      X2ValueSet kept = remove_unstable_opaque_expression_value_facts(&values);
      if (!kept.empty())
        output[register_name] = std::move(kept);
    }
  }
  return output;
}

// Faithful port of dropUnstableOpaqueExpressionX2ValueFacts.
X2ValueDataflowState drop_unstable_opaque_expression_x2_value_facts(
    const X2ValueDataflowState& input, bool track_register_memory) {
  X2ValueDataflowState output = input;
  output.x = remove_unstable_opaque_expression_value_facts(&input.x);
  output.y = remove_unstable_opaque_expression_value_facts(
      input.y.has_value() ? &*input.y : nullptr);
  output.x2 = remove_unstable_opaque_expression_value_facts(&input.x2);
  output.memory = track_register_memory
                      ? remove_unstable_opaque_expression_value_memory(input.memory)
                      : std::optional<X2ValueMemory>{std::nullopt};
  output.shapeMemory = track_register_memory
                           ? clone_x2_shape_memory_field(input.shapeMemory)
                           : std::optional<X2ShapeMemory>{std::nullopt};
  return output;
}

// Faithful port of x2ValueEdgeDropsUnstableOpaqueExpressionFacts.
bool x2_value_edge_drops_unstable_opaque_expression_facts(const IrOp& op,
                                                          const RegisterValueEdge& edge,
                                                          int source_index) {
  return edge.target <= source_index || op.kind == IrKind::Call ||
         op.kind == IrKind::IndirectCall || op.kind == IrKind::Return;
}

std::optional<RegisterValueSet> vp_entry_mantissas_from_value_facts(const X2ValueSet& values) {
  RegisterValueSet mantissas;
  for (const X2ValueFact& value : values) {
    const std::optional<std::string> decimal = normalized_decimal_value_from_fact(value);
    if (decimal.has_value())
      mantissas.insert(*decimal);
  }
  return mantissas.empty() ? std::nullopt : std::optional<RegisterValueSet>{mantissas};
}

std::optional<X2ShapeSet> vp_entry_shapes_from_shape_facts(const X2ShapeSet* shapes) {
  if (shapes == nullptr)
    return std::nullopt;
  X2ShapeSet structural;
  const X2ShapeSet canonical = canonical_structural_shape_facts(shapes);
  for (const X2ShapeFact& fact : structural_restore_shape_facts(canonical)) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind == X2ShapeModelKind::Mantissa && model.mantissa.has_value() &&
        (model.mantissa->radix == X2MantissaRadix::Hex ||
         model.mantissa->radix == X2MantissaRadix::Super))
      structural.insert(fact);
  }
  return structural.empty() ? std::nullopt : std::optional<X2ShapeSet>{structural};
}

std::optional<X2ShapeSet> vp_entry_sign_shapes_from_shape_facts(const X2ShapeSet* shapes) {
  if (shapes == nullptr)
    return std::nullopt;
  const X2ShapeSet decimal = decimal_display_shape_facts(shapes);
  const std::optional<X2ShapeSet> structural = vp_entry_shapes_from_shape_facts(shapes);
  return merge_optional_shape_sources(
      {&decimal, structural.has_value() ? &*structural : nullptr});
}

std::optional<X2ValueFact> x2_decimal_entry_fact(const std::string& raw) {
  const std::optional<std::string> normalized = normalize_decimal_mantissa_entry(raw);
  if (!normalized.has_value())
    return std::nullopt;
  if (raw == *normalized)
    return decimal_value_fact(raw, "normalized");
  return decimal_value_fact(raw, "unnormalized");
}

X2ShapeSet direct_structural_mantissa_shape_facts(const X2ShapeSet* shapes) {
  return structural_mantissa_shape_facts(canonical_structural_shape_facts(shapes));
}

// --- canonical entry-part sets --------------------------------------------
RegisterValueSet canonical_decimal_mantissa_entry_set(const RegisterValueSet* input) {
  RegisterValueSet output;
  if (input == nullptr)
    return output;
  for (const std::string& raw : *input) {
    const std::string canonical = canonical_shape_raw(raw);
    if (decimal_mantissa_shape_raw_is_valid(canonical))
      output.insert(canonical);
  }
  return output;
}

RegisterValueSet canonical_decimal_exponent_mantissa_set(const RegisterValueSet* input) {
  RegisterValueSet output;
  if (input == nullptr)
    return output;
  for (const std::string& raw : *input) {
    const std::string canonical = canonical_shape_raw(raw);
    if (decimal_exponent_mantissa_raw_is_valid(canonical))
      output.insert(canonical);
  }
  return output;
}

RegisterValueSet canonical_exponent_set(const RegisterValueSet* input) {
  RegisterValueSet output;
  if (input == nullptr)
    return output;
  for (const std::string& raw : *input) {
    const std::optional<std::string> canonical = canonical_exponent_shape_raw(raw);
    if (canonical.has_value())
      output.insert(*canonical);
  }
  return output;
}

// --- entry / VP / structural state constructors ---------------------------
X2EntryState closed_x2_entry_state() {
  return X2EntryState{.kind = X2EntryState::Kind::Closed};
}

X2EntryState unknown_x2_entry_state() {
  return X2EntryState{.kind = X2EntryState::Kind::Unknown};
}

X2VpContextState none_x2_vp_context_state() {
  return X2VpContextState{.kind = X2VpContextState::Kind::None};
}

X2VpContextState unknown_x2_vp_context_state() {
  return X2VpContextState{.kind = X2VpContextState::Kind::Unknown};
}

X2StructuralEntryState none_x2_structural_entry_state() {
  return X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None};
}

X2StructuralEntryState unknown_x2_structural_entry_state() {
  return X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::Unknown};
}

X2EntryState x2_entry_state_from_open_raw(const RegisterValueSet* input) {
  const RegisterValueSet raw = canonical_decimal_mantissa_entry_set(input);
  if (raw.empty())
    return unknown_x2_entry_state();
  return X2EntryState{.kind = X2EntryState::Kind::Open, .raw = raw};
}

X2EntryState x2_entry_state_from_exponent_parts(const RegisterValueSet* mantissa_input,
                                                const RegisterValueSet* exponent_input) {
  const RegisterValueSet mantissa = canonical_decimal_exponent_mantissa_set(mantissa_input);
  const RegisterValueSet exponent = canonical_exponent_set(exponent_input);
  if (mantissa.empty() || exponent.empty())
    return unknown_x2_entry_state();
  return X2EntryState{
      .kind = X2EntryState::Kind::Exponent, .mantissa = mantissa, .exponent = exponent};
}

X2StructuralEntryState x2_structural_entry_state_from_parts(const X2ShapeSet* mantissa_input,
                                                            const RegisterValueSet* exponent_input) {
  const X2ShapeSet mantissa = structural_mantissa_shape_facts(canonical_shape_set(mantissa_input));
  const RegisterValueSet exponent = canonical_exponent_set(exponent_input);
  if (mantissa.empty() || exponent.empty())
    return unknown_x2_structural_entry_state();
  return X2StructuralEntryState{
      .kind = X2StructuralEntryState::Kind::Exponent, .mantissa = mantissa, .exponent = exponent};
}

X2VpContextState x2_vp_context_state_from_exponent_parts(const RegisterValueSet* mantissa_input,
                                                         const RegisterValueSet* exponent_input) {
  const X2EntryState entry = x2_entry_state_from_exponent_parts(mantissa_input, exponent_input);
  if (entry.kind != X2EntryState::Kind::Exponent)
    return unknown_x2_vp_context_state();
  return X2VpContextState{.kind = X2VpContextState::Kind::Exponent,
                          .mantissa = entry.mantissa,
                          .exponent = entry.exponent};
}

X2EntryState clone_x2_entry_state(const X2EntryState& input) {
  if (input.kind == X2EntryState::Kind::Open)
    return x2_entry_state_from_open_raw(&input.raw);
  if (input.kind == X2EntryState::Kind::Exponent)
    return x2_entry_state_from_exponent_parts(&input.mantissa, &input.exponent);
  return input;
}

X2VpContextState clone_x2_vp_context_state(const X2VpContextState& input) {
  if (input.kind == X2VpContextState::Kind::None || input.kind == X2VpContextState::Kind::Unknown)
    return input;
  return x2_vp_context_state_from_exponent_parts(&input.mantissa, &input.exponent);
}

X2StructuralEntryState clone_x2_structural_entry_state(const X2StructuralEntryState& input) {
  if (input.kind == X2StructuralEntryState::Kind::None ||
      input.kind == X2StructuralEntryState::Kind::Unknown)
    return input;
  return x2_structural_entry_state_from_parts(&input.mantissa, &input.exponent);
}

X2VpContextState x2_vp_context_from_exponent_entry(const X2EntryState& input) {
  return x2_vp_context_state_from_exponent_parts(&input.mantissa, &input.exponent);
}

X2StructuralEntryState x2_structural_context_from_entry(const X2StructuralEntryState& input) {
  return x2_structural_entry_state_from_parts(&input.mantissa, &input.exponent);
}

// --- advance / sign-change entry transitions ------------------------------
X2EntryState advance_decimal_digit_entry(const X2EntryState& input, const std::string& digit) {
  if (input.kind == X2EntryState::Kind::Unknown)
    return unknown_x2_entry_state();
  if (input.kind == X2EntryState::Kind::Exponent)
    return advance_exponent_digit_entry(input, digit);
  RegisterValueSet source;
  if (input.kind == X2EntryState::Kind::Closed)
    source.insert("");
  else
    source = canonical_decimal_mantissa_entry_set(&input.raw);
  if (source.empty())
    return unknown_x2_entry_state();
  RegisterValueSet raw;
  for (const std::string& prefix : source) {
    const std::string next = prefix + digit;
    if (decimal_mantissa_digit_count(next) > 8)
      return unknown_x2_entry_state();
    raw.insert(next);
  }
  return x2_entry_state_from_open_raw(&raw);
}

X2EntryState advance_decimal_point_entry(const X2EntryState& input) {
  RegisterValueSet raw;
  const RegisterValueSet source = canonical_decimal_mantissa_entry_set(&input.raw);
  if (source.empty())
    return unknown_x2_entry_state();
  for (const std::string& prefix : source) {
    raw.insert(prefix.find('.') != std::string::npos ? prefix : prefix + ".");
  }
  return x2_entry_state_from_open_raw(&raw);
}

X2EntryState advance_exponent_digit_entry(const X2EntryState& input, const std::string& digit) {
  const X2EntryState entry = x2_entry_state_from_exponent_parts(&input.mantissa, &input.exponent);
  if (entry.kind != X2EntryState::Kind::Exponent)
    return unknown_x2_entry_state();
  RegisterValueSet exponent;
  for (const std::string& prefix : entry.exponent) {
    const std::string sign = prefix.rfind('-', 0) == 0 ? "-" : "";
    const std::string digits = prefix.substr(sign.size());
    if (digits.size() >= 2)
      return unknown_x2_entry_state();
    exponent.insert(prefix + digit);
  }
  return x2_entry_state_from_exponent_parts(&entry.mantissa, &exponent);
}

X2StructuralEntryState advance_structural_exponent_digit_entry(const X2StructuralEntryState& input,
                                                               const std::string& digit) {
  RegisterValueSet exponent;
  for (const std::string& prefix : input.exponent) {
    const std::string sign = prefix.rfind('-', 0) == 0 ? "-" : "";
    const std::string digits = prefix.substr(sign.size());
    if (digits.size() >= 2)
      return unknown_x2_structural_entry_state();
    exponent.insert(prefix + digit);
  }
  return x2_structural_entry_state_from_parts(&input.mantissa, &exponent);
}

X2EntryState sign_change_exponent_entry(const X2EntryState& input) {
  const X2EntryState entry = x2_entry_state_from_exponent_parts(&input.mantissa, &input.exponent);
  if (entry.kind != X2EntryState::Kind::Exponent)
    return unknown_x2_entry_state();
  RegisterValueSet exponent;
  for (const std::string& raw : entry.exponent)
    exponent.insert(raw.rfind('-', 0) == 0 ? raw.substr(1) : "-" + raw);
  return x2_entry_state_from_exponent_parts(&entry.mantissa, &exponent);
}

X2StructuralEntryState sign_change_structural_exponent_entry(const X2StructuralEntryState& input) {
  RegisterValueSet exponent;
  for (const std::string& raw : input.exponent)
    exponent.insert(raw.rfind('-', 0) == 0 ? raw.substr(1) : "-" + raw);
  return x2_structural_entry_state_from_parts(&input.mantissa, &exponent);
}

X2VpContextState sign_change_vp_context(const X2VpContextState& input) {
  const X2VpContextState context =
      x2_vp_context_state_from_exponent_parts(&input.mantissa, &input.exponent);
  if (context.kind != X2VpContextState::Kind::Exponent)
    return unknown_x2_vp_context_state();
  RegisterValueSet exponent;
  for (const std::string& raw : context.exponent)
    exponent.insert(raw.rfind('-', 0) == 0 ? raw.substr(1) : "-" + raw);
  return x2_vp_context_state_from_exponent_parts(&context.mantissa, &exponent);
}

std::string sign_changed_decimal_entry(const std::string& raw) {
  const std::optional<std::string> normalized = normalize_decimal_mantissa_entry(raw);
  if (!normalized.has_value())
    return "0";
  return raw.rfind('-', 0) == 0 ? raw.substr(1) : "-" + raw;
}

std::optional<RegisterValueSet> sign_changed_mantissa_shapes(const RegisterValueSet& input) {
  RegisterValueSet mantissas;
  for (const std::string& raw : input) {
    const std::optional<std::string> signed_value = sign_changed_mantissa_shape(raw);
    if (!signed_value.has_value())
      return std::nullopt;
    mantissas.insert(*signed_value);
  }
  return mantissas.empty() ? std::nullopt : std::optional<RegisterValueSet>{mantissas};
}

// --- closed-exponent / exponent-entry fact builders -----------------------
std::set<X2ValueFact> closed_exponent_entry_decimal_facts(const X2EntryState& input) {
  std::set<X2ValueFact> values;
  if (input.kind != X2EntryState::Kind::Exponent)
    return values;
  const X2EntryState entry = x2_entry_state_from_exponent_parts(&input.mantissa, &input.exponent);
  if (entry.kind != X2EntryState::Kind::Exponent)
    return values;
  for (const std::string& mantissa : entry.mantissa) {
    for (const std::string& exponent : entry.exponent) {
      const std::optional<std::string> value = normalized_exponent_entry_value(mantissa, exponent);
      if (value.has_value())
        values.insert(decimal_value_fact(*value, "normalized"));
    }
  }
  return values;
}

std::set<X2ShapeFact> exponent_entry_shape_facts(const X2EntryState& input) {
  const X2EntryState entry = x2_entry_state_from_exponent_parts(&input.mantissa, &input.exponent);
  std::set<X2ShapeFact> shapes;
  if (entry.kind != X2EntryState::Kind::Exponent)
    return shapes;
  for (const std::string& mantissa : entry.mantissa) {
    for (const std::string& exponent : entry.exponent) {
      const std::optional<X2ShapeFact> fact =
          x2_exponent_shape_fact_from_mantissa_fact(decimal_mantissa_shape_fact(mantissa), exponent);
      if (fact.has_value())
        shapes.insert(*fact);
    }
  }
  return shapes;
}

std::set<X2ShapeFact> closed_exponent_entry_shape_facts(const X2EntryState& input) {
  if (input.kind != X2EntryState::Kind::Exponent)
    return std::set<X2ShapeFact>{};
  const X2EntryState entry = x2_entry_state_from_exponent_parts(&input.mantissa, &input.exponent);
  if (entry.kind != X2EntryState::Kind::Exponent)
    return std::set<X2ShapeFact>{};
  std::set<X2ShapeFact> shapes = exponent_entry_shape_facts(entry);
  for (const std::string& mantissa : entry.mantissa) {
    for (const std::string& exponent : entry.exponent) {
      const std::optional<std::string> value = normalized_exponent_entry_value(mantissa, exponent);
      const std::optional<X2ShapeFact> display_shape =
          value.has_value() ? exact_decimal_display_shape_fact(*value) : std::nullopt;
      if (display_shape.has_value())
        shapes.insert(*display_shape);
    }
  }
  return shapes;
}

X2StructuralEntryState structural_exponent_entry_from_vp_entry_shapes(const X2ShapeSet& shapes) {
  const RegisterValueSet empty_exponent{std::string{""}};
  return x2_structural_entry_state_from_parts(&shapes, &empty_exponent);
}

std::set<X2ShapeFact> structural_exponent_entry_shape_facts(const X2StructuralEntryState& input) {
  const X2StructuralEntryState entry =
      x2_structural_entry_state_from_parts(&input.mantissa, &input.exponent);
  std::set<X2ShapeFact> shapes;
  if (entry.kind != X2StructuralEntryState::Kind::Exponent)
    return shapes;
  for (const std::string& mantissa : entry.mantissa) {
    for (const std::string& exponent : entry.exponent) {
      const std::optional<X2ShapeFact> fact =
          x2_exponent_shape_fact_from_mantissa_fact(mantissa, exponent);
      if (fact.has_value() && x2_shape_fact_safety(*fact) == X2ShapeSafety::StructuralOnly)
        shapes.insert(*fact);
    }
  }
  return shapes;
}

// --- X2ValueDataflowState builders ----------------------------------------
X2ValueDataflowState x2_value_state_from_open_decimal_entry(
    const X2EntryState& entry, const std::optional<X2ValueMemory>& memory,
    const std::optional<X2ShapeMemory>& shape_memory, const X2ValueSet* y, const X2ShapeSet* y_shape,
    const X2ShapeSet* y_direct_shape) {
  X2ValueSet x;
  X2ValueSet x2;
  X2ShapeSet x_shape;
  X2ShapeSet x2_shape;
  for (const std::string& raw : entry.raw) {
    const std::optional<std::string> normalized = normalize_decimal_mantissa_entry(raw);
    if (normalized.has_value()) {
      x.insert(decimal_value_fact(*normalized, "normalized"));
      x_shape.insert(decimal_mantissa_shape_fact(*normalized));
    }
    const std::optional<X2ValueFact> x2_fact = x2_decimal_entry_fact(raw);
    if (x2_fact.has_value())
      x2.insert(*x2_fact);
    x2_shape.insert(decimal_mantissa_shape_fact(raw));
  }
  return X2ValueDataflowState{
      .x = x,
      .y = clone_optional_value_set(y),
      .x2 = x2,
      .xShape = x_shape,
      .yShape = clone_optional_shape_set(y_shape),
      .x2Shape = x2_shape,
      .xDirectShape = X2ShapeSet{},
      .yDirectShape = clone_optional_shape_set(y_direct_shape),
      .entry = entry,
      .vpContext = none_x2_vp_context_state(),
      .structuralEntry = none_x2_structural_entry_state(),
      .structuralVpContext = none_x2_structural_entry_state(),
      .memory = clone_x2_value_memory_field(memory),
      .shapeMemory = clone_x2_shape_memory_field(shape_memory),
  };
}

X2ValueDataflowState x2_value_state_from_mantissa_shapes(
    const RegisterValueSet& mantissas, const std::optional<X2ValueMemory>& memory,
    const std::optional<X2ShapeMemory>& shape_memory, const X2ValueSet* y, const X2ShapeSet* y_shape,
    const X2ShapeSet* y_direct_shape, bool& ok) {
  ok = true;
  X2ValueSet x;
  X2ValueSet x2;
  X2ShapeSet x_shape;
  X2ShapeSet x2_shape;
  for (const std::string& raw : mantissas) {
    const std::optional<std::string> normalized = normalize_decimal_mantissa_entry(raw);
    const std::optional<X2ValueFact> x2_fact = x2_decimal_entry_fact(raw);
    if (!normalized.has_value() || !x2_fact.has_value()) {
      ok = false;
      return X2ValueDataflowState{};
    }
    x.insert(decimal_value_fact(*normalized, "normalized"));
    x2.insert(*x2_fact);
    x_shape.insert(decimal_mantissa_shape_fact(*normalized));
    x2_shape.insert(decimal_mantissa_shape_fact(raw));
  }
  return X2ValueDataflowState{
      .x = x,
      .y = clone_optional_value_set(y),
      .x2 = x2,
      .xShape = x_shape,
      .yShape = clone_optional_shape_set(y_shape),
      .x2Shape = x2_shape,
      .xDirectShape = X2ShapeSet{},
      .yDirectShape = clone_optional_shape_set(y_direct_shape),
      .entry = closed_x2_entry_state(),
      .vpContext = none_x2_vp_context_state(),
      .structuralEntry = none_x2_structural_entry_state(),
      .structuralVpContext = none_x2_structural_entry_state(),
      .vpEntryMantissa = RegisterValueSet{mantissas.begin(), mantissas.end()},
      .vpEntrySignMantissa = RegisterValueSet{mantissas.begin(), mantissas.end()},
      .vpEntrySignShape = vp_entry_sign_shapes_from_shape_facts(&x2_shape),
      .memory = clone_x2_value_memory_field(memory),
      .shapeMemory = clone_x2_shape_memory_field(shape_memory),
  };
}

X2ValueDataflowState x2_value_state_from_structural_shapes(
    const X2ShapeSet& shapes, const std::optional<X2ValueMemory>& memory,
    const std::optional<X2ShapeMemory>& shape_memory, const X2ValueSet* y, const X2ShapeSet* y_shape,
    const X2ShapeSet* y_direct_shape) {
  const X2ShapeSet shape_set = canonical_structural_shape_facts(&shapes);
  return X2ValueDataflowState{
      .x = X2ValueSet{},
      .y = clone_optional_value_set(y),
      .x2 = X2ValueSet{},
      .xShape = shape_set,
      .yShape = clone_optional_shape_set(y_shape),
      .x2Shape = X2ShapeSet{shape_set.begin(), shape_set.end()},
      .xDirectShape = direct_structural_mantissa_shape_facts(&shape_set),
      .yDirectShape = clone_optional_shape_set(y_direct_shape),
      .entry = closed_x2_entry_state(),
      .vpContext = none_x2_vp_context_state(),
      .structuralEntry = none_x2_structural_entry_state(),
      .structuralVpContext = none_x2_structural_entry_state(),
      .vpEntryShape = vp_entry_shapes_from_shape_facts(&shape_set),
      .vpEntrySignShape = vp_entry_sign_shapes_from_shape_facts(&shape_set),
      .memory = clone_x2_value_memory_field(memory),
      .shapeMemory = clone_x2_shape_memory_field(shape_memory),
  };
}

X2ValueDataflowState x2_value_state_from_structural_exponent_entry(
    const X2StructuralEntryState& input, const std::optional<X2ValueMemory>& memory,
    const std::optional<X2ShapeMemory>& shape_memory, const X2ValueSet* y, const X2ShapeSet* y_shape,
    const X2ShapeSet* y_direct_shape) {
  if (input.kind != X2StructuralEntryState::Kind::Exponent) {
    return X2ValueDataflowState{
        .x = X2ValueSet{},
        .y = clone_optional_value_set(y),
        .x2 = X2ValueSet{},
        .xShape = X2ShapeSet{},
        .yShape = clone_optional_shape_set(y_shape),
        .x2Shape = X2ShapeSet{},
        .xDirectShape = X2ShapeSet{},
        .yDirectShape = clone_optional_shape_set(y_direct_shape),
        .entry = closed_x2_entry_state(),
        .vpContext = none_x2_vp_context_state(),
        .structuralEntry = clone_x2_structural_entry_state(input),
        .structuralVpContext = unknown_x2_structural_entry_state(),
        .memory = clone_x2_value_memory_field(memory),
        .shapeMemory = clone_x2_shape_memory_field(shape_memory),
    };
  }
  const std::set<X2ShapeFact> shapes = structural_exponent_entry_shape_facts(input);
  return X2ValueDataflowState{
      .x = X2ValueSet{},
      .y = clone_optional_value_set(y),
      .x2 = X2ValueSet{},
      .xShape = X2ShapeSet{shapes.begin(), shapes.end()},
      .yShape = clone_optional_shape_set(y_shape),
      .x2Shape = X2ShapeSet{shapes.begin(), shapes.end()},
      .xDirectShape = X2ShapeSet{},
      .yDirectShape = clone_optional_shape_set(y_direct_shape),
      .entry = closed_x2_entry_state(),
      .vpContext = none_x2_vp_context_state(),
      .structuralEntry = clone_x2_structural_entry_state(input),
      .structuralVpContext = x2_structural_context_from_entry(input),
      .vpEntrySignShape = vp_entry_sign_shapes_from_shape_facts(&shapes),
      .memory = clone_x2_value_memory_field(memory),
      .shapeMemory = clone_x2_shape_memory_field(shape_memory),
  };
}

X2ValueDataflowState x2_value_state_from_exponent_entry(
    const X2EntryState& input, const std::optional<X2ValueMemory>& memory,
    const std::optional<X2ShapeMemory>& shape_memory, const X2ValueSet* y, const X2ShapeSet* y_shape,
    const X2ShapeSet* y_direct_shape) {
  if (input.kind != X2EntryState::Kind::Exponent) {
    return X2ValueDataflowState{
        .x = X2ValueSet{},
        .y = clone_optional_value_set(y),
        .x2 = X2ValueSet{},
        .xShape = X2ShapeSet{},
        .yShape = clone_optional_shape_set(y_shape),
        .x2Shape = X2ShapeSet{},
        .xDirectShape = X2ShapeSet{},
        .yDirectShape = clone_optional_shape_set(y_direct_shape),
        .entry = clone_x2_entry_state(input),
        .vpContext = unknown_x2_vp_context_state(),
        .structuralEntry = none_x2_structural_entry_state(),
        .structuralVpContext = none_x2_structural_entry_state(),
        .memory = clone_x2_value_memory_field(memory),
        .shapeMemory = clone_x2_shape_memory_field(shape_memory),
    };
  }
  const std::set<X2ValueFact> values = closed_exponent_entry_decimal_facts(input);
  const std::set<X2ShapeFact> shapes = closed_exponent_entry_shape_facts(input);
  return X2ValueDataflowState{
      .x = X2ValueSet{values.begin(), values.end()},
      .y = clone_optional_value_set(y),
      .x2 = X2ValueSet{},
      .xShape = X2ShapeSet{shapes.begin(), shapes.end()},
      .yShape = clone_optional_shape_set(y_shape),
      .x2Shape = X2ShapeSet{shapes.begin(), shapes.end()},
      .xDirectShape = X2ShapeSet{},
      .yDirectShape = clone_optional_shape_set(y_direct_shape),
      .entry = clone_x2_entry_state(input),
      .vpContext = x2_vp_context_from_exponent_entry(input),
      .structuralEntry = none_x2_structural_entry_state(),
      .structuralVpContext = none_x2_structural_entry_state(),
      .vpEntrySignShape = vp_entry_sign_shapes_from_shape_facts(&shapes),
      .memory = clone_x2_value_memory_field(memory),
      .shapeMemory = clone_x2_shape_memory_field(shape_memory),
  };
}

X2ValueDataflowState x2_value_state_from_signed_structural_vp_context(
    const X2ValueDataflowState& input, const X2StructuralEntryState& context) {
  const X2ValueSet* in_y = input.y.has_value() ? &*input.y : nullptr;
  const X2ShapeSet* in_y_shape = input.yShape.has_value() ? &*input.yShape : nullptr;
  const X2ShapeSet* in_y_direct = input.yDirectShape.has_value() ? &*input.yDirectShape : nullptr;
  if (context.kind != X2StructuralEntryState::Kind::Exponent) {
    return X2ValueDataflowState{
        .x = X2ValueSet{},
        .y = clone_optional_value_set(in_y),
        .x2 = X2ValueSet{},
        .xShape = X2ShapeSet{},
        .yShape = clone_optional_shape_set(in_y_shape),
        .x2Shape = X2ShapeSet{},
        .xDirectShape = X2ShapeSet{},
        .yDirectShape = clone_optional_shape_set(in_y_direct),
        .entry = closed_x2_entry_state(),
        .vpContext = none_x2_vp_context_state(),
        .structuralEntry = none_x2_structural_entry_state(),
        .structuralVpContext = clone_x2_structural_entry_state(context),
        .memory = clone_x2_value_memory_field(input.memory),
        .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
    };
  }
  const std::set<X2ShapeFact> shapes = structural_exponent_entry_shape_facts(context);
  return X2ValueDataflowState{
      .x = X2ValueSet{},
      .y = clone_optional_value_set(in_y),
      .x2 = X2ValueSet{},
      .xShape = X2ShapeSet{shapes.begin(), shapes.end()},
      .yShape = clone_optional_shape_set(in_y_shape),
      .x2Shape = X2ShapeSet{shapes.begin(), shapes.end()},
      .xDirectShape = X2ShapeSet{},
      .yDirectShape = clone_optional_shape_set(in_y_direct),
      .entry = closed_x2_entry_state(),
      .vpContext = none_x2_vp_context_state(),
      .structuralEntry = none_x2_structural_entry_state(),
      .structuralVpContext = x2_structural_context_from_entry(context),
      .vpEntryShape = vp_entry_shapes_from_shape_facts(&shapes),
      .vpEntrySignShape = vp_entry_sign_shapes_from_shape_facts(&shapes),
      .memory = clone_x2_value_memory_field(input.memory),
      .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
  };
}

X2ValueDataflowState x2_value_state_from_signed_decimal_vp_context(
    const X2ValueDataflowState& input, const X2VpContextState& context) {
  const X2ValueSet* in_y = input.y.has_value() ? &*input.y : nullptr;
  const X2ShapeSet* in_y_shape = input.yShape.has_value() ? &*input.yShape : nullptr;
  const X2ShapeSet* in_y_direct = input.yDirectShape.has_value() ? &*input.yDirectShape : nullptr;
  if (context.kind != X2VpContextState::Kind::Exponent) {
    return X2ValueDataflowState{
        .x = X2ValueSet{},
        .y = clone_optional_value_set(in_y),
        .x2 = X2ValueSet{},
        .xShape = X2ShapeSet{},
        .yShape = clone_optional_shape_set(in_y_shape),
        .x2Shape = X2ShapeSet{},
        .xDirectShape = X2ShapeSet{},
        .yDirectShape = clone_optional_shape_set(in_y_direct),
        .entry = closed_x2_entry_state(),
        .vpContext = clone_x2_vp_context_state(context),
        .structuralEntry = none_x2_structural_entry_state(),
        .structuralVpContext = clone_x2_structural_entry_state(input.structuralVpContext),
        .memory = clone_x2_value_memory_field(input.memory),
        .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
    };
  }
  const X2EntryState entry = x2_entry_state_from_exponent_parts(&context.mantissa, &context.exponent);
  if (entry.kind != X2EntryState::Kind::Exponent) {
    return X2ValueDataflowState{
        .x = X2ValueSet{},
        .y = clone_optional_value_set(in_y),
        .x2 = X2ValueSet{},
        .xShape = X2ShapeSet{},
        .yShape = clone_optional_shape_set(in_y_shape),
        .x2Shape = X2ShapeSet{},
        .xDirectShape = X2ShapeSet{},
        .yDirectShape = clone_optional_shape_set(in_y_direct),
        .entry = closed_x2_entry_state(),
        .vpContext = clone_x2_vp_context_state(context),
        .structuralEntry = none_x2_structural_entry_state(),
        .structuralVpContext = clone_x2_structural_entry_state(input.structuralVpContext),
        .memory = clone_x2_value_memory_field(input.memory),
        .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
    };
  }
  const std::set<X2ValueFact> values = closed_exponent_entry_decimal_facts(entry);
  const std::set<X2ShapeFact> shapes = closed_exponent_entry_shape_facts(entry);
  return X2ValueDataflowState{
      .x = X2ValueSet{values.begin(), values.end()},
      .y = clone_optional_value_set(in_y),
      .x2 = X2ValueSet{values.begin(), values.end()},
      .xShape = X2ShapeSet{shapes.begin(), shapes.end()},
      .yShape = clone_optional_shape_set(in_y_shape),
      .x2Shape = X2ShapeSet{shapes.begin(), shapes.end()},
      .xDirectShape = X2ShapeSet{},
      .yDirectShape = clone_optional_shape_set(in_y_direct),
      .entry = closed_x2_entry_state(),
      .vpContext = x2_vp_context_from_exponent_entry(entry),
      .structuralEntry = none_x2_structural_entry_state(),
      .structuralVpContext = clone_x2_structural_entry_state(input.structuralVpContext),
      .vpEntryMantissa = vp_entry_mantissas_from_value_facts(
          X2ValueSet{values.begin(), values.end()}),
      .vpEntryShape = vp_entry_shapes_from_shape_facts(&shapes),
      .vpEntrySignShape = vp_entry_sign_shapes_from_shape_facts(&shapes),
      .memory = clone_x2_value_memory_field(input.memory),
      .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
  };
}

X2ValueDataflowState close_x2_value_entry(const X2ValueDataflowState& input) {
  const X2ValueSet* in_y = input.y.has_value() ? &*input.y : nullptr;
  const X2ShapeSet* in_y_shape = input.yShape.has_value() ? &*input.yShape : nullptr;
  const X2ShapeSet* in_y_direct = input.yDirectShape.has_value() ? &*input.yDirectShape : nullptr;
  const std::set<X2ValueFact> closed_exponent_values = closed_exponent_entry_decimal_facts(input.entry);
  const std::set<X2ShapeFact> closed_exponent_shapes = closed_exponent_entry_shape_facts(input.entry);
  if (!closed_exponent_values.empty() || !closed_exponent_shapes.empty()) {
    return X2ValueDataflowState{
        .x = X2ValueSet{closed_exponent_values.begin(), closed_exponent_values.end()},
        .y = clone_optional_value_set(in_y),
        .x2 = X2ValueSet{closed_exponent_values.begin(), closed_exponent_values.end()},
        .xShape = X2ShapeSet{closed_exponent_shapes.begin(), closed_exponent_shapes.end()},
        .yShape = clone_optional_shape_set(in_y_shape),
        .x2Shape = X2ShapeSet{closed_exponent_shapes.begin(), closed_exponent_shapes.end()},
        .xDirectShape = X2ShapeSet{closed_exponent_shapes.begin(), closed_exponent_shapes.end()},
        .yDirectShape = clone_optional_shape_set(in_y_direct),
        .entry = closed_x2_entry_state(),
        .vpContext = clone_x2_vp_context_state(input.vpContext),
        .structuralEntry = none_x2_structural_entry_state(),
        .structuralVpContext = clone_x2_structural_entry_state(input.structuralVpContext),
        .vpEntryMantissa = vp_entry_mantissas_from_value_facts(
            X2ValueSet{closed_exponent_values.begin(), closed_exponent_values.end()}),
        .vpEntryShape = vp_entry_shapes_from_shape_facts(&closed_exponent_shapes),
        .vpEntrySignShape = vp_entry_sign_shapes_from_shape_facts(&closed_exponent_shapes),
        .memory = clone_x2_value_memory_field(input.memory),
        .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
    };
  }
  const X2StructuralEntryState structural_entry = input.structuralEntry;
  if (structural_entry.kind == X2StructuralEntryState::Kind::Exponent) {
    const std::set<X2ShapeFact> shapes = structural_exponent_entry_shape_facts(structural_entry);
    return X2ValueDataflowState{
        .x = X2ValueSet{},
        .y = clone_optional_value_set(in_y),
        .x2 = X2ValueSet{},
        .xShape = X2ShapeSet{shapes.begin(), shapes.end()},
        .yShape = clone_optional_shape_set(in_y_shape),
        .x2Shape = X2ShapeSet{shapes.begin(), shapes.end()},
        .xDirectShape = X2ShapeSet{shapes.begin(), shapes.end()},
        .yDirectShape = clone_optional_shape_set(in_y_direct),
        .entry = closed_x2_entry_state(),
        .vpContext = clone_x2_vp_context_state(input.vpContext),
        .structuralEntry = none_x2_structural_entry_state(),
        .structuralVpContext = x2_structural_context_from_entry(structural_entry),
        .vpEntryShape = vp_entry_shapes_from_shape_facts(&shapes),
        .vpEntrySignShape = vp_entry_sign_shapes_from_shape_facts(&shapes),
        .memory = clone_x2_value_memory_field(input.memory),
        .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
    };
  }
  const X2ShapeSet* in_x_shape = &input.xShape;
  const X2ShapeSet* in_x2_shape = &input.x2Shape;
  const X2ShapeSet* in_x_direct = input.xDirectShape.has_value() ? &*input.xDirectShape : nullptr;
  const RegisterValueSet* in_vp_mantissa =
      input.vpEntryMantissa.has_value() ? &*input.vpEntryMantissa : nullptr;
  const RegisterValueSet* in_vp_sign_mantissa =
      input.vpEntrySignMantissa.has_value() ? &*input.vpEntrySignMantissa : nullptr;
  const X2ShapeSet* in_vp_shape = input.vpEntryShape.has_value() ? &*input.vpEntryShape : nullptr;
  const X2ShapeSet* in_vp_sign_shape =
      input.vpEntrySignShape.has_value() ? &*input.vpEntrySignShape : nullptr;
  return X2ValueDataflowState{
      .x = canonical_x2_value_set(&input.x),
      .y = clone_optional_value_set(in_y),
      .x2 = canonical_x2_value_set(&input.x2),
      .xShape = clone_optional_shape_set(in_x_shape),
      .yShape = clone_optional_shape_set(in_y_shape),
      .x2Shape = clone_optional_shape_set(in_x2_shape),
      .xDirectShape = clone_optional_shape_set(in_x_direct),
      .yDirectShape = clone_optional_shape_set(in_y_direct),
      .entry = closed_x2_entry_state(),
      .vpContext = clone_x2_vp_context_state(input.vpContext),
      .structuralEntry = none_x2_structural_entry_state(),
      .structuralVpContext = clone_x2_structural_entry_state(input.structuralVpContext),
      .vpEntryMantissa =
          input.vpEntryMantissaTransient ? std::nullopt : clone_optional_string_set(in_vp_mantissa),
      .vpEntrySignMantissa = clone_optional_string_set(in_vp_sign_mantissa),
      // cloneOptionalShapeSet always yields a Set (empty when undefined), so
      // these stay present even when the source was absent.
      .vpEntryShape = input.vpEntryShapeTransient
                          ? std::nullopt
                          : std::optional<X2ShapeSet>{clone_optional_shape_set(in_vp_shape)},
      .vpEntrySignShape = std::optional<X2ShapeSet>{clone_optional_shape_set(in_vp_sign_shape)},
      .memory = clone_x2_value_memory_field(input.memory),
      .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
  };
}

// --- Session C: VP first-digit splice machinery ---------------------------
// Faithful port of the firstDigitVpSplice* / restoredVpFirstDigitSource* /
// *SpliceTargetShapeFacts cluster that feeds transferPlainX2VpEntry{Shape,
// Mantissa}State. Pure shape-set -> shape/mantissa-set transforms, additive
// dead code validated in isolation against the TS oracle.

std::optional<std::string> pure_mantissa_digit_run_from_shape_fact(const X2ShapeFact& fact) {
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
  if (model.kind != X2ShapeModelKind::Mantissa || !model.mantissa.has_value())
    return std::nullopt;
  const X2MantissaDataModel& mantissa = *model.mantissa;
  if ((mantissa.radix != X2MantissaRadix::Decimal && mantissa.radix != X2MantissaRadix::Hex &&
       mantissa.radix != X2MantissaRadix::Super) ||
      !mantissa.sign.empty() || mantissa.hasDecimalPoint)
    return std::nullopt;
  std::string output;
  for (const std::string& digit : mantissa.digits)
    output += digit;
  return output;
}

std::optional<X2MantissaDataModel> first_digit_splice_source_mantissa_model(const X2ShapeFact& fact) {
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
  if (model.kind == X2ShapeModelKind::Mantissa && model.mantissa.has_value())
    return *model.mantissa;
  if (model.kind == X2ShapeModelKind::ExponentEntry && model.mantissa.has_value())
    return *model.mantissa;
  return std::nullopt;
}

std::optional<X2MantissaDataModel> first_digit_splice_target_mantissa_model(const X2ShapeFact& fact) {
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
  if (model.kind == X2ShapeModelKind::Mantissa && model.mantissa.has_value())
    return *model.mantissa;
  const std::optional<X2ShapeFact> closed = x2_closed_exponent_display_shape_fact(fact);
  if (!closed.has_value())
    return std::nullopt;
  const X2ShapeDataModel closed_model = x2_shape_data_model_for_fact(*closed);
  return closed_model.kind == X2ShapeModelKind::Mantissa && closed_model.mantissa.has_value()
             ? std::optional<X2MantissaDataModel>{*closed_model.mantissa}
             : std::nullopt;
}

std::optional<std::string> decimal_vp_first_digit_source_digit_from_model(
    const X2MantissaDataModel& mantissa) {
  if (mantissa.radix == X2MantissaRadix::Decimal) {
    for (const std::string& digit : mantissa.digits)
      if (digit != "0")
        return digit;
    return std::nullopt;
  }
  if (!mantissa.sign.empty() || mantissa.digits.empty())
    return std::nullopt;
  const std::string& digit = mantissa.digits.front();
  return digit.size() == 1U && digit.at(0) >= '0' && digit.at(0) <= '9'
             ? std::optional<std::string>{digit}
             : std::nullopt;
}

std::optional<std::string> structural_vp_first_digit_source_digit_from_model(
    const X2MantissaDataModel& mantissa) {
  if (!mantissa.sign.empty() || mantissa.digits.empty())
    return std::nullopt;
  return mantissa.digits.front();
}

std::optional<std::string> replace_first_shape_digit(const std::string& raw, const std::string& digit) {
  bool replaced = false;
  std::string output;
  for (const std::string& point : utf8_codepoints(raw)) {
    if (!replaced && is_structural_hex_digit(point)) {
      output += digit;
      replaced = true;
      continue;
    }
    output += point;
  }
  return replaced ? std::optional<std::string>{output} : std::nullopt;
}

std::optional<X2MantissaRadix> structural_first_digit_splice_radix(const X2MantissaDataModel& target,
                                                                   const std::string& source_digit,
                                                                   const std::string& spliced) {
  const bool source_is_decimal =
      source_digit.size() == 1U && source_digit.at(0) >= '0' && source_digit.at(0) <= '9';
  if (target.radix == X2MantissaRadix::Decimal)
    return source_is_decimal ? std::nullopt : std::optional<X2MantissaRadix>{X2MantissaRadix::Hex};
  if (target.radix == X2MantissaRadix::Hex)
    return X2MantissaRadix::Hex;
  if (target.radix == X2MantissaRadix::Super)
    return spliced == target.canonical ? std::optional<X2MantissaRadix>{X2MantissaRadix::Super}
                                       : std::optional<X2MantissaRadix>{X2MantissaRadix::Hex};
  return std::nullopt;
}

std::optional<X2MantissaDataModel> x2_decimal_mantissa_first_digit_splice_model(
    const X2MantissaDataModel& source, const X2MantissaDataModel& target) {
  const std::optional<std::string> source_digit = decimal_vp_first_digit_source_digit_from_model(source);
  if (!source_digit.has_value() || target.radix != X2MantissaRadix::Decimal || !target.sign.empty() ||
      target.digits.empty())
    return std::nullopt;
  const std::optional<std::string> spliced = replace_first_shape_digit(target.canonical, *source_digit);
  if (!spliced.has_value() || decimal_mantissa_digit_count(*spliced) > 8)
    return std::nullopt;
  return normalize_decimal_mantissa_entry(*spliced).has_value()
             ? std::optional<X2MantissaDataModel>{decimal_mantissa_data_model(*spliced)}
             : std::nullopt;
}

std::optional<X2MantissaDataModel> x2_structural_mantissa_first_digit_splice_data_model(
    const X2MantissaDataModel& source, const X2MantissaDataModel& target) {
  const std::optional<std::string> source_digit = structural_vp_first_digit_source_digit_from_model(source);
  if (!source_digit.has_value() ||
      (target.radix != X2MantissaRadix::Decimal && target.radix != X2MantissaRadix::Hex &&
       target.radix != X2MantissaRadix::Super) ||
      !target.sign.empty() || target.digits.empty())
    return std::nullopt;
  const std::optional<std::string> spliced = replace_first_shape_digit(target.canonical, *source_digit);
  if (!spliced.has_value())
    return std::nullopt;
  const std::optional<X2MantissaRadix> radix =
      structural_first_digit_splice_radix(target, *source_digit, *spliced);
  if (!radix.has_value())
    return std::nullopt;
  return structural_mantissa_data_model(*radix, *spliced, X2ShapeSafety::StructuralOnly);
}

struct X2MantissaFirstDigitSpliceModel {
  std::optional<X2MantissaDataModel> decimal;
  std::optional<X2MantissaDataModel> structural;
};

std::optional<X2MantissaFirstDigitSpliceModel> x2_mantissa_first_digit_splice_model(
    const X2MantissaDataModel& source, const X2MantissaDataModel& target) {
  const std::optional<X2MantissaDataModel> decimal =
      x2_decimal_mantissa_first_digit_splice_model(source, target);
  const std::optional<X2MantissaDataModel> structural =
      x2_structural_mantissa_first_digit_splice_data_model(source, target);
  if (!decimal.has_value() && !structural.has_value())
    return std::nullopt;
  return X2MantissaFirstDigitSpliceModel{.decimal = decimal, .structural = structural};
}

std::set<X2ShapeFact> decimal_mantissa_shape_facts_for_splice(const X2ShapeSet* shapes) {
  std::set<X2ShapeFact> output;
  if (shapes == nullptr)
    return output;
  for (const X2ShapeFact& fact : *shapes) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind == X2ShapeModelKind::Mantissa && model.mantissa.has_value() &&
        model.mantissa->radix == X2MantissaRadix::Decimal) {
      const std::optional<X2ShapeFact> canonical = x2_shape_fact_from_data_model(model);
      if (canonical.has_value())
        output.insert(*canonical);
    }
  }
  return output;
}

std::set<X2ShapeFact> x2_structural_restore_shape_facts(const X2ShapeSet* input) {
  return structural_restore_shape_facts(canonical_structural_shape_facts(input));
}

std::set<X2ShapeFact> x2_restored_display_shape_facts(const X2ShapeSet* input) {
  std::set<X2ShapeFact> output = decimal_display_shape_facts(input);
  for (const X2ShapeFact& fact : x2_structural_restore_shape_facts(input))
    output.insert(fact);
  return output;
}

std::set<X2ShapeFact> restored_vp_first_digit_source_shape_facts(const X2ShapeSet* shapes) {
  return x2_restored_display_shape_facts(shapes);
}

std::set<X2ShapeFact> closed_exponent_mantissa_display_shape_facts(const X2ShapeSet* shapes) {
  std::set<X2ShapeFact> output;
  if (shapes == nullptr)
    return output;
  for (const X2ShapeFact& fact : *shapes) {
    const std::optional<X2ShapeDataModel> closed =
        x2_closed_exponent_display_data_model(x2_shape_data_model_for_fact(fact));
    if (!closed.has_value() || closed->kind != X2ShapeModelKind::Mantissa)
      continue;
    const std::optional<X2ShapeFact> canonical = x2_shape_fact_from_data_model(*closed);
    if (canonical.has_value())
      output.insert(*canonical);
  }
  return output;
}

std::set<X2ShapeFact> closed_decimal_exponent_pure_mantissa_digit_shape_facts(const X2ShapeSet* shapes) {
  std::set<X2ShapeFact> output;
  if (shapes == nullptr)
    return output;
  for (const X2ShapeFact& fact : *shapes) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind != X2ShapeModelKind::ExponentEntry || !model.mantissa.has_value() ||
        model.mantissa->radix != X2MantissaRadix::Decimal)
      continue;
    const std::optional<X2ShapeFact> closed = x2_closed_exponent_display_shape_fact(fact);
    if (!closed.has_value() || !pure_mantissa_digit_run_from_shape_fact(*closed).has_value())
      continue;
    output.insert(*closed);
  }
  return output;
}

std::set<X2ShapeFact> decimal_vp_first_digit_splice_target_shape_facts(const X2ShapeSet* shapes,
                                                                       bool include_exponent_targets) {
  std::set<X2ShapeFact> output = decimal_mantissa_shape_facts_for_splice(shapes);
  for (const X2ShapeFact& fact : closed_decimal_exponent_pure_mantissa_digit_shape_facts(shapes))
    output.insert(fact);
  if (!include_exponent_targets || shapes == nullptr)
    return output;
  for (const X2ShapeFact& fact : *shapes) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind != X2ShapeModelKind::ExponentEntry || !model.mantissa.has_value() ||
        model.mantissa->radix != X2MantissaRadix::Decimal)
      continue;
    const std::optional<X2ShapeFact> mantissa = x2_mantissa_shape_fact_from_model(*model.mantissa);
    if (mantissa.has_value())
      output.insert(*mantissa);
  }
  return output;
}

std::set<X2ShapeFact> vp_first_digit_splice_target_shape_facts(const X2ShapeSet* shapes,
                                                               bool include_exponent_targets) {
  const std::set<X2ShapeFact> structural_restore = x2_structural_restore_shape_facts(shapes);
  std::set<X2ShapeFact> output = structural_mantissa_shape_facts(structural_restore);
  for (const X2ShapeFact& fact : decimal_mantissa_shape_facts_for_splice(shapes))
    output.insert(fact);
  for (const X2ShapeFact& fact : closed_exponent_mantissa_display_shape_facts(shapes))
    output.insert(fact);
  if (!include_exponent_targets || shapes == nullptr)
    return output;
  for (const X2ShapeFact& fact : *shapes) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind != X2ShapeModelKind::ExponentEntry || !model.mantissa.has_value())
      continue;
    if (model.exponentRaw.starts_with('-'))
      continue;
    const std::optional<X2ShapeFact> mantissa = x2_mantissa_shape_fact_from_model(*model.mantissa);
    if (mantissa.has_value())
      output.insert(*mantissa);
  }
  return output;
}

std::vector<X2MantissaFirstDigitSpliceModel> first_digit_vp_splice_models(
    const std::set<X2ShapeFact>& sources, const std::set<X2ShapeFact>& targets) {
  std::vector<X2MantissaFirstDigitSpliceModel> output;
  for (const X2ShapeFact& source : sources) {
    const std::optional<X2MantissaDataModel> source_model = first_digit_splice_source_mantissa_model(source);
    if (!source_model.has_value())
      continue;
    for (const X2ShapeFact& target : targets) {
      const std::optional<X2MantissaDataModel> target_model = first_digit_splice_target_mantissa_model(target);
      if (!target_model.has_value())
        continue;
      const std::optional<X2MantissaFirstDigitSpliceModel> spliced =
          x2_mantissa_first_digit_splice_model(*source_model, *target_model);
      if (spliced.has_value())
        output.push_back(*spliced);
    }
  }
  return output;
}

std::optional<X2ShapeSet> structural_first_digit_vp_splice_shape_facts(const X2ShapeSet* x_shape,
                                                                       const X2ShapeSet* x2_shape,
                                                                       bool include_exponent_targets) {
  const std::set<X2ShapeFact> sources = restored_vp_first_digit_source_shape_facts(x_shape);
  const std::set<X2ShapeFact> targets =
      vp_first_digit_splice_target_shape_facts(x2_shape, include_exponent_targets);
  X2ShapeSet shapes;
  for (const X2MantissaFirstDigitSpliceModel& model : first_digit_vp_splice_models(sources, targets)) {
    if (!model.structural.has_value())
      continue;
    const std::optional<X2ShapeFact> fact = x2_mantissa_shape_fact_from_model(*model.structural);
    if (fact.has_value())
      shapes.insert(*fact);
  }
  return shapes.empty() ? std::nullopt : std::optional<X2ShapeSet>{shapes};
}

std::optional<RegisterValueSet> decimal_first_digit_vp_splice_mantissas(const X2ShapeSet* x_shape,
                                                                        const X2ShapeSet* x2_shape,
                                                                        bool include_exponent_targets) {
  const std::set<X2ShapeFact> sources = restored_vp_first_digit_source_shape_facts(x_shape);
  const std::set<X2ShapeFact> targets =
      decimal_vp_first_digit_splice_target_shape_facts(x2_shape, include_exponent_targets);
  RegisterValueSet mantissas;
  for (const X2MantissaFirstDigitSpliceModel& model : first_digit_vp_splice_models(sources, targets)) {
    if (model.decimal.has_value())
      mantissas.insert(model.decimal->canonical);
  }
  return mantissas.empty() ? std::nullopt : std::optional<RegisterValueSet>{mantissas};
}

// --- Session C.2: transferPlainX2VpEntry* + shared*/vpSplice cluster -------
// Faithful port of the shared value/shape comparison helpers, the
// value-derived display synthesis, and the six transferPlainX2VpEntry* state
// transfer functions. Additive dead code validated against the TS oracle.

bool is_plain_decimal_number(const std::string& value) {
  std::size_t index = 0;
  if (index < value.size() && value.at(index) == '-')
    ++index;
  if (index >= value.size())
    return false;
  const std::string rest = value.substr(index);
  const auto all_digits = [](const std::string& text) {
    if (text.empty())
      return false;
    for (const char ch : text)
      if (ch < '0' || ch > '9')
        return false;
    return true;
  };
  if (rest.at(0) == '.')
    return all_digits(rest.substr(1));
  const std::size_t dot = rest.find('.');
  if (dot == std::string::npos)
    return all_digits(rest);
  const std::string int_part = rest.substr(0, dot);
  const std::string frac_part = rest.substr(dot + 1);
  if (frac_part.find('.') != std::string::npos)
    return false;
  return all_digits(int_part) && all_digits(frac_part);
}

std::set<X2ShapeFact> x2_shapes_from_value_facts(const X2ValueSet& values) {
  std::set<X2ShapeFact> output;
  for (const X2ValueFact& value : values) {
    if (!value.starts_with("decimal:"))
      continue;
    std::string mid;
    bool normalized = false;
    if (value.ends_with(":normalized")) {
      normalized = true;
      mid = value.substr(8, value.size() - 8 - 11);
    } else if (value.ends_with(":unnormalized")) {
      mid = value.substr(8, value.size() - 8 - 13);
    } else {
      continue;
    }
    if (!is_plain_decimal_number(mid))
      continue;
    if (!normalized) {
      output.insert(decimal_mantissa_shape_fact(mid));
      continue;
    }
    const std::optional<X2ShapeFact> shape = exact_decimal_display_shape_fact(mid);
    if (shape.has_value())
      output.insert(*shape);
  }
  return shape_set_with_stable_expression_value_shapes(&output, &values);
}

std::optional<X2ShapeSet> shape_set_with_value_derived_display_shapes(const X2ShapeSet* shapes,
                                                                      const X2ValueSet* values) {
  X2ShapeSet output = clone_optional_shape_set(shapes);
  if (values != nullptr) {
    for (const X2ShapeFact& fact : x2_shapes_from_value_facts(*values))
      output.insert(fact);
  }
  return output.empty() ? std::nullopt : std::optional<X2ShapeSet>{output};
}

std::optional<X2ShapeSet> vp_splice_shape_set_with_value_shapes(const X2ShapeSet* shapes,
                                                                const X2ValueSet* values) {
  return shape_set_with_value_derived_display_shapes(shapes, values);
}

std::optional<X2ShapeSet> shape_set_with_fallback_value_derived_display_shapes(
    const X2ShapeSet* shapes, const X2ValueSet* values) {
  const X2ShapeSet stable = shape_set_with_stable_expression_value_shapes(shapes, values);
  if (!stable.empty())
    return stable;
  return shape_set_with_value_derived_display_shapes(nullptr, values);
}

std::optional<X2ShapeSet> effective_input_x_shape(const X2ShapeSet* x_shape, const X2ValueSet* x) {
  return shape_set_with_fallback_value_derived_display_shapes(x_shape, x);
}

std::optional<X2ShapeSet> effective_input_x2_shape(const X2ShapeSet* x2_shape, const X2ValueSet* x2) {
  return shape_set_with_fallback_value_derived_display_shapes(x2_shape, x2);
}

std::set<std::string> x2_shape_set_exact_decimal_displays(const X2ShapeSet* input) {
  std::set<std::string> output;
  const X2ShapeSet stable = shape_set_with_stable_expression_value_shapes(input, nullptr);
  for (const X2ShapeFact& fact : stable) {
    const std::optional<std::string> decimal = x2_shape_fact_exact_decimal_display(fact);
    if (decimal.has_value())
      output.insert(*decimal);
  }
  return output;
}

std::set<std::string> x2_value_set_restored_visible_decimals(const X2ValueSet* input) {
  std::set<std::string> output;
  if (input == nullptr)
    return output;
  for (const X2ValueFact& fact : *input) {
    const std::optional<std::string> visible = x2_value_fact_restored_visible_decimal(fact);
    if (visible.has_value())
      output.insert(*visible);
  }
  return output;
}

std::set<std::string> x2_value_shape_set_exact_decimal_displays(const X2ValueSet* values,
                                                                const X2ShapeSet* shapes) {
  std::set<std::string> output = x2_value_set_restored_visible_decimals(values);
  const X2ShapeSet stable = shape_set_with_stable_expression_value_shapes(shapes, values);
  for (const std::string& decimal : x2_shape_set_exact_decimal_displays(&stable))
    output.insert(decimal);
  return output;
}

bool x2_value_shape_set_has_exact_decimal_display(const X2ValueSet* values, const X2ShapeSet* shapes,
                                                  const X2ValueFact& fact) {
  const std::optional<std::string> visible = x2_value_fact_restored_visible_decimal(fact);
  if (!visible.has_value())
    return false;
  return x2_value_shape_set_exact_decimal_displays(values, shapes).contains(*visible);
}

std::optional<X2ShapeSet> shared_exact_decimal_display_shape_facts(const X2ValueSet* x,
                                                                   const X2ValueSet* x2,
                                                                   const X2ShapeSet* x_shape,
                                                                   const X2ShapeSet* x2_shape) {
  X2ShapeSet shapes;
  const std::optional<X2ShapeSet> eff_x = effective_input_x_shape(x_shape, x);
  const std::optional<X2ShapeSet> eff_x2 = effective_input_x2_shape(x2_shape, x2);
  const X2ShapeSet* eff_x_ptr = eff_x.has_value() ? &*eff_x : nullptr;
  const X2ShapeSet* eff_x2_ptr = eff_x2.has_value() ? &*eff_x2 : nullptr;
  for (const X2ShapeFact& fact : decimal_display_shape_facts(eff_x2_ptr)) {
    const std::optional<std::string> visible = x2_shape_fact_restored_visible_decimal(fact);
    if (!visible.has_value())
      continue;
    const X2ValueFact source_value = decimal_value_fact(*visible, "normalized");
    if (x2_value_shape_set_has_exact_decimal_display(x, eff_x_ptr, source_value))
      shapes.insert(fact);
  }
  for (const X2ShapeFact& fact : decimal_display_shape_facts(eff_x_ptr)) {
    const std::optional<std::string> visible = x2_shape_fact_restored_visible_decimal(fact);
    if (!visible.has_value())
      continue;
    const X2ValueFact source_value = decimal_value_fact(*visible, "normalized");
    if (x2_value_shape_set_has_exact_decimal_display(x2, eff_x2_ptr, source_value))
      shapes.insert(fact);
  }
  return shapes.empty() ? std::nullopt : std::optional<X2ShapeSet>{shapes};
}

bool structural_shape_set_has_intersection(const X2ShapeSet& left, const X2ShapeSet& right) {
  for (const X2ShapeFact& shape : left) {
    if (right.contains(shape))
      return true;
  }
  return false;
}

bool structural_shape_set_has_exact_decimal_display_intersection(const std::set<std::string>& left_displays,
                                                                 const X2ShapeSet& right) {
  if (left_displays.empty())
    return false;
  for (const X2ShapeFact& shape : right) {
    const std::optional<std::string> display = x2_shape_fact_exact_decimal_display(shape);
    if (display.has_value() && left_displays.contains(*display))
      return true;
  }
  return false;
}

std::set<X2ShapeFact> x2_shared_structural_restore_shape_facts(const X2ShapeSet* visible,
                                                               const X2ShapeSet* source) {
  const std::set<X2ShapeFact> visible_restore =
      structural_restore_shape_facts(canonical_structural_shape_facts(visible));
  const std::set<std::string> visible_exact = x2_shape_set_exact_decimal_displays(visible);
  std::set<X2ShapeFact> output;
  for (const X2ShapeFact& source_fact : canonical_structural_shape_facts(source)) {
    const X2ShapeSet single{source_fact};
    const std::set<X2ShapeFact> source_restore = structural_restore_shape_facts(single);
    if (!structural_shape_set_has_intersection(visible_restore, source_restore) &&
        !structural_shape_set_has_exact_decimal_display_intersection(visible_exact, source_restore))
      continue;
    for (const X2ShapeFact& fact : structural_mantissa_shape_facts(source_restore))
      output.insert(fact);
  }
  return output;
}

std::optional<X2ShapeSet> shared_structural_shape_facts(const X2ValueSet* x, const X2ValueSet* x2,
                                                        const X2ShapeSet* x_shape,
                                                        const X2ShapeSet* x2_shape) {
  const std::optional<X2ShapeSet> eff_x = effective_input_x_shape(x_shape, x);
  const std::optional<X2ShapeSet> eff_x2 = effective_input_x2_shape(x2_shape, x2);
  const X2ShapeSet shapes = x2_shared_structural_restore_shape_facts(
      eff_x.has_value() ? &*eff_x : nullptr, eff_x2.has_value() ? &*eff_x2 : nullptr);
  return shapes.empty() ? std::nullopt : std::optional<X2ShapeSet>{shapes};
}

std::optional<X2ShapeSet> shared_vp_entry_sign_shape_facts(const X2ValueSet* x, const X2ValueSet* x2,
                                                           const X2ShapeSet* x_shape,
                                                           const X2ShapeSet* x2_shape) {
  const std::optional<X2ShapeSet> structural = shared_structural_shape_facts(x, x2, x_shape, x2_shape);
  const std::optional<X2ShapeSet> exact =
      shared_exact_decimal_display_shape_facts(x, x2, x_shape, x2_shape);
  return merge_optional_shape_sources(
      {structural.has_value() ? &*structural : nullptr, exact.has_value() ? &*exact : nullptr});
}

std::set<std::string> signed_zero_decimal_mantissa_shapes(const X2ShapeSet* input) {
  std::set<std::string> output;
  if (input == nullptr)
    return output;
  for (const X2ShapeFact& fact : *input) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind == X2ShapeModelKind::Mantissa && model.mantissa.has_value() &&
        model.mantissa->radix == X2MantissaRadix::Decimal &&
        model.mantissa->safety == X2ShapeSafety::ErrorProne &&
        model.mantissa->normalizedDecimal.has_value() && *model.mantissa->normalizedDecimal == "0" &&
        model.mantissa->sign == "-")
      output.insert(model.mantissa->canonical);
  }
  return output;
}

std::optional<RegisterValueSet> shared_signed_zero_decimal_mantissas(const X2ShapeSet* x_shapes,
                                                                     const X2ShapeSet* x2_shapes) {
  const std::set<std::string> x_signed_zero = signed_zero_decimal_mantissa_shapes(x_shapes);
  const std::set<std::string> x2_signed_zero = signed_zero_decimal_mantissa_shapes(x2_shapes);
  RegisterValueSet mantissas;
  for (const std::string& raw : x2_signed_zero) {
    if (x_signed_zero.contains(raw))
      mantissas.insert(raw);
  }
  return mantissas.empty() ? std::nullopt : std::optional<RegisterValueSet>{mantissas};
}

std::optional<RegisterValueSet> shared_normalized_decimal_mantissas(const X2ValueSet* x,
                                                                    const X2ValueSet* x2) {
  RegisterValueSet mantissas;
  const X2ValueSet x_values = canonical_x2_value_set(x);
  for (const X2ValueFact& fact : canonical_x2_value_set(x2)) {
    if (!x_values.contains(fact))
      continue;
    const std::optional<std::string> decimal = normalized_decimal_value_from_fact(fact);
    if (decimal.has_value())
      mantissas.insert(*decimal);
  }
  return mantissas.empty() ? std::nullopt : std::optional<RegisterValueSet>{mantissas};
}

std::optional<RegisterValueSet> shared_exact_decimal_display_mantissas(const X2ValueSet* x,
                                                                       const X2ValueSet* x2,
                                                                       const X2ShapeSet* x_shape,
                                                                       const X2ShapeSet* x2_shape) {
  RegisterValueSet mantissas;
  const std::optional<X2ShapeSet> facts =
      shared_exact_decimal_display_shape_facts(x, x2, x_shape, x2_shape);
  if (facts.has_value()) {
    for (const X2ShapeFact& fact : *facts) {
      const std::optional<std::string> visible = x2_shape_fact_restored_visible_decimal(fact);
      if (visible.has_value())
        mantissas.insert(*visible);
    }
  }
  return mantissas.empty() ? std::nullopt : std::optional<RegisterValueSet>{mantissas};
}

std::optional<RegisterValueSet> shared_decimal_vp_entry_mantissas(const X2ValueSet* x,
                                                                  const X2ValueSet* x2,
                                                                  const X2ShapeSet* x_shape,
                                                                  const X2ShapeSet* x2_shape) {
  RegisterValueSet mantissas;
  const std::optional<X2ShapeSet> eff_x = effective_input_x_shape(x_shape, x);
  const std::optional<X2ShapeSet> eff_x2 = effective_input_x2_shape(x2_shape, x2);
  const std::optional<RegisterValueSet> signed_zero = shared_signed_zero_decimal_mantissas(
      eff_x.has_value() ? &*eff_x : nullptr, eff_x2.has_value() ? &*eff_x2 : nullptr);
  const std::optional<RegisterValueSet> normalized = shared_normalized_decimal_mantissas(x, x2);
  if (normalized.has_value()) {
    for (const std::string& raw : *normalized) {
      if (raw != "0" || !signed_zero.has_value())
        mantissas.insert(raw);
    }
  }
  const std::optional<RegisterValueSet> exact =
      shared_exact_decimal_display_mantissas(x, x2, x_shape, x2_shape);
  if (exact.has_value())
    for (const std::string& raw : *exact)
      mantissas.insert(raw);
  if (signed_zero.has_value())
    for (const std::string& raw : *signed_zero)
      mantissas.insert(raw);
  return mantissas.empty() ? std::nullopt : std::optional<RegisterValueSet>{mantissas};
}

std::optional<RegisterValueSet> merge_optional_string_sources(
    std::initializer_list<const RegisterValueSet*> sources) {
  RegisterValueSet output;
  for (const RegisterValueSet* source : sources) {
    if (source == nullptr)
      continue;
    for (const std::string& value : *source)
      output.insert(value);
  }
  return output.empty() ? std::nullopt : std::optional<RegisterValueSet>{output};
}

bool is_empty_plain_op(const IrOp& op) {
  return op.opcode >= 0x54 && op.opcode <= 0x56;
}

std::optional<RegisterValueSet> transfer_plain_x2_vp_entry_mantissa_state(
    const X2ValueDataflowState& input, const IrOp& op, const X2ValueSet& x, const X2ValueSet& x2,
    const X2ShapeSet& x_shape, const X2ShapeSet& x2_shape, X2Effect effect,
    const X2ShapeSet* first_digit_source_shape, const X2ShapeSet* first_digit_target_shape,
    bool include_exponent_targets, const X2ValueSet* first_digit_source_values,
    const X2ValueSet* first_digit_target_values) {
  if (effect == X2Effect::Affects)
    return shared_decimal_vp_entry_mantissas(&x, &x2, &x_shape, &x2_shape);
  if (effect == X2Effect::Preserves) {
    // TS default-parameter semantics: undefined (nullptr) resolves to xShape/x2Shape/x/x2.
    const X2ShapeSet* fdss = first_digit_source_shape ? first_digit_source_shape : &x_shape;
    const X2ShapeSet* fdts = first_digit_target_shape ? first_digit_target_shape : &x2_shape;
    const X2ValueSet* fdsv = first_digit_source_values ? first_digit_source_values : &x;
    const X2ValueSet* fdtv = first_digit_target_values ? first_digit_target_values : &x2;
    const bool inherit = is_empty_plain_op(op) && !input.vpEntryMantissaTransient;
    const RegisterValueSet* inherited =
        inherit && input.vpEntryMantissa.has_value() ? &*input.vpEntryMantissa : nullptr;
    const std::optional<X2ShapeSet> source_shape =
        vp_splice_shape_set_with_value_shapes(fdss, fdsv);
    const std::optional<X2ShapeSet> target_shape =
        vp_splice_shape_set_with_value_shapes(fdts, fdtv);
    const std::optional<RegisterValueSet> spliced = decimal_first_digit_vp_splice_mantissas(
        source_shape.has_value() ? &*source_shape : nullptr,
        target_shape.has_value() ? &*target_shape : nullptr, include_exponent_targets);
    return merge_optional_string_sources(
        {inherited, spliced.has_value() ? &*spliced : nullptr});
  }
  return std::nullopt;
}

bool transfer_plain_x2_vp_entry_mantissa_transient_state(
    const IrOp& op, X2Effect effect, const X2ShapeSet* first_digit_source_shape,
    const X2ShapeSet* first_digit_target_shape, bool include_exponent_targets,
    const X2ValueSet* first_digit_source_values, const X2ValueSet* first_digit_target_values) {
  if (effect != X2Effect::Preserves || is_empty_plain_op(op))
    return false;
  const std::optional<X2ShapeSet> source_shape =
      vp_splice_shape_set_with_value_shapes(first_digit_source_shape, first_digit_source_values);
  const std::optional<X2ShapeSet> target_shape =
      vp_splice_shape_set_with_value_shapes(first_digit_target_shape, first_digit_target_values);
  return decimal_first_digit_vp_splice_mantissas(source_shape.has_value() ? &*source_shape : nullptr,
                                                 target_shape.has_value() ? &*target_shape : nullptr,
                                                 include_exponent_targets)
      .has_value();
}

std::optional<RegisterValueSet> transfer_plain_x2_vp_entry_sign_mantissa_state(
    const X2ValueDataflowState& input, const IrOp& op, X2Effect effect) {
  if (effect == X2Effect::Preserves && is_empty_plain_op(op))
    return clone_optional_string_set(
        input.vpEntrySignMantissa.has_value() ? &*input.vpEntrySignMantissa : nullptr);
  return std::nullopt;
}

std::optional<X2ShapeSet> transfer_plain_x2_vp_entry_sign_shape_state(
    const X2ValueDataflowState& input, const IrOp& op, const X2ValueSet& x, const X2ValueSet& x2,
    const X2ShapeSet& x_shape, const X2ShapeSet& x2_shape, X2Effect effect) {
  if (effect == X2Effect::Affects)
    return shared_vp_entry_sign_shape_facts(&x, &x2, &x_shape, &x2_shape);
  if (effect == X2Effect::Preserves && is_empty_plain_op(op))
    return std::optional<X2ShapeSet>{clone_optional_shape_set(
        input.vpEntrySignShape.has_value() ? &*input.vpEntrySignShape : nullptr)};
  return std::nullopt;
}

std::optional<X2ShapeSet> transfer_plain_x2_vp_entry_shape_state(
    const X2ValueDataflowState& input, const IrOp& op, const X2ValueSet& x, const X2ValueSet& x2,
    const X2ShapeSet& x_shape, const X2ShapeSet& x2_shape, X2Effect effect,
    const X2ShapeSet* first_digit_source_shape, const X2ShapeSet* first_digit_target_shape,
    bool include_exponent_targets, const X2ValueSet* first_digit_source_values,
    const X2ValueSet* first_digit_target_values) {
  if (effect == X2Effect::Affects)
    return shared_structural_shape_facts(&x, &x2, &x_shape, &x2_shape);
  if (effect == X2Effect::Preserves) {
    // TS default-parameter semantics: undefined (nullptr) resolves to xShape/x2Shape/x/x2.
    const X2ShapeSet* fdss = first_digit_source_shape ? first_digit_source_shape : &x_shape;
    const X2ShapeSet* fdts = first_digit_target_shape ? first_digit_target_shape : &x2_shape;
    const X2ValueSet* fdsv = first_digit_source_values ? first_digit_source_values : &x;
    const X2ValueSet* fdtv = first_digit_target_values ? first_digit_target_values : &x2;
    const bool inherit = is_empty_plain_op(op) && !input.vpEntryShapeTransient;
    const X2ShapeSet* inherited =
        inherit && input.vpEntryShape.has_value() ? &*input.vpEntryShape : nullptr;
    const std::optional<X2ShapeSet> source_shape =
        vp_splice_shape_set_with_value_shapes(fdss, fdsv);
    const std::optional<X2ShapeSet> target_shape =
        vp_splice_shape_set_with_value_shapes(fdts, fdtv);
    const std::optional<X2ShapeSet> spliced = structural_first_digit_vp_splice_shape_facts(
        source_shape.has_value() ? &*source_shape : nullptr,
        target_shape.has_value() ? &*target_shape : nullptr, include_exponent_targets);
    return merge_optional_shape_sources(
        {inherited, spliced.has_value() ? &*spliced : nullptr});
  }
  return std::nullopt;
}

bool transfer_plain_x2_vp_entry_shape_transient_state(
    const IrOp& op, X2Effect effect, const X2ShapeSet* first_digit_source_shape,
    const X2ShapeSet* first_digit_target_shape, bool include_exponent_targets,
    const X2ValueSet* first_digit_source_values, const X2ValueSet* first_digit_target_values) {
  if (effect != X2Effect::Preserves || is_empty_plain_op(op))
    return false;
  const std::optional<X2ShapeSet> source_shape =
      vp_splice_shape_set_with_value_shapes(first_digit_source_shape, first_digit_source_values);
  const std::optional<X2ShapeSet> target_shape =
      vp_splice_shape_set_with_value_shapes(first_digit_target_shape, first_digit_target_values);
  return structural_first_digit_vp_splice_shape_facts(
             source_shape.has_value() ? &*source_shape : nullptr,
             target_shape.has_value() ? &*target_shape : nullptr, include_exponent_targets)
      .has_value();
}

// --- Session C.3: transfer dispatchers + transfer_plain_x2_value_state ------
// Faithful port of the seven transfer* dispatchers, the transferPlain set
// helpers, the sign-change subtree, and transfer_plain_x2_value_state. Additive
// dead code (NOT wired into the live Plain branch) validated against the TS
// oracle.

constexpr const char* kTxSameUnknownValue = "same:unknown";

bool is_opaque_shared_value_fact(const X2ValueFact& value) {
  return value == kTxSameUnknownValue || value.starts_with("reg:") || value.starts_with("expr:") ||
         value.starts_with("expr-key:");
}

bool x2_shape_sets_have_same_decimal_display_shape(const X2ShapeSet* left, const X2ShapeSet* right) {
  const std::set<X2ShapeFact> left_shapes = decimal_display_shape_facts(left);
  if (left_shapes.empty())
    return false;
  for (const X2ShapeFact& shape : decimal_display_shape_facts(right)) {
    if (left_shapes.contains(shape))
      return true;
  }
  return false;
}

std::set<std::string> tx_x2_value_shape_set_restored_visible_decimals(const X2ValueSet* values,
                                                                      const X2ShapeSet* shapes) {
  std::set<std::string> output = x2_value_set_restored_visible_decimals(values);
  const X2ShapeSet stable = shape_set_with_stable_expression_value_shapes(shapes, values);
  for (const std::string& decimal : x2_shape_set_restored_visible_decimals(&stable))
    output.insert(decimal);
  return output;
}

bool x2_value_shape_set_has_restored_visible_decimal(const X2ValueSet* values,
                                                     const X2ShapeSet* shapes,
                                                     const X2ValueFact& fact) {
  const std::optional<std::string> visible = x2_value_fact_restored_visible_decimal(fact);
  if (!visible.has_value())
    return false;
  return tx_x2_value_shape_set_restored_visible_decimals(values, shapes).contains(*visible);
}

X2ValueSet x2_sync_value_set_from_visible_x(const X2ValueSet& values, const X2ShapeSet* shapes) {
  X2ValueSet output = canonical_x2_value_set(&values);
  for (const std::string& decimal :
       tx_x2_value_shape_set_restored_visible_decimals(&values, shapes)) {
    output.insert(decimal_value_fact(decimal, "normalized"));
  }
  return canonical_x2_value_set(&output);
}

std::set<X2ShapeFact> normalize_x2_sync_shapes_from_x(const X2ShapeSet* input) {
  std::set<X2ShapeFact> output;
  for (const X2ShapeFact& fact : canonical_shape_set(input)) {
    constexpr std::string_view prefix = "mantissa:";
    constexpr std::string_view suffix = ":decimal";
    if (fact.starts_with(prefix) && fact.ends_with(suffix)) {
      const std::string raw =
          fact.substr(prefix.size(), fact.size() - prefix.size() - suffix.size());
      if (is_plain_decimal_number(raw)) {
        const std::optional<std::string> normalized = normalize_plain_decimal(raw);
        if (normalized.has_value() && *normalized == "0" &&
            canonical_shape_raw(raw).starts_with('-')) {
          output.insert(decimal_mantissa_shape_fact(raw));
          continue;
        }
        const std::optional<X2ShapeFact> shape =
            normalized.has_value() ? exact_decimal_display_shape_fact(*normalized) : std::nullopt;
        if (shape.has_value())
          output.insert(*shape);
        continue;
      }
    }
    output.insert(fact);
  }
  return output;
}

std::set<X2ShapeFact> x2_sync_shape_set_from_visible_x(const X2ShapeSet* input,
                                                       const X2ValueSet* values) {
  const X2ShapeSet stable = shape_set_with_stable_expression_value_shapes(input, values);
  if (!stable.empty())
    return normalize_x2_sync_shapes_from_x(&stable);
  const std::optional<X2ShapeSet> fallback =
      shape_set_with_value_derived_display_shapes(nullptr, values);
  return normalize_x2_sync_shapes_from_x(fallback.has_value() ? &*fallback : nullptr);
}

// ===========================================================================
// Session: faithful store / direct-flow / indirect-flow VP-splice source
// cluster + transferConditional* vpEntry wrappers (helpers.ts ~5787-6023,
// ~10800-10880, ~12024-12176). These feed the faithful edge transfers in
// internal_transfer_x2_value_dataflow_state. Reuses the existing shape-model,
// shared-fact, first-digit-splice, and sync-from-visible foundation; no
// duplication.
// ===========================================================================

// --- decimal store-splice mantissa (decimalStoreVpSpliceMantissa) ----------
std::optional<std::string> negative_decimal_store_vp_splice_mantissa(const std::string& integer,
                                                                     const std::string& fraction) {
  const std::string raw = fraction.empty() ? integer : integer + "." + fraction;
  bool replaced = false;
  std::string spliced;
  for (const char ch : raw) {
    if (ch != '.' && ch != '0' && !replaced) {
      spliced.push_back('9');
      replaced = true;
    } else {
      spliced.push_back(ch);
    }
  }
  if (!replaced)
    return std::optional<std::string>{"-1"};
  return normalize_decimal_mantissa_entry("-" + spliced);
}

std::optional<std::string> decimal_store_vp_splice_mantissa(const std::string& raw) {
  const std::string canonical = canonical_shape_raw(raw);
  if (decimal_mantissa_digit_count(canonical) > 8)
    return std::nullopt;
  static const std::regex pattern(R"(^(-?)([0-9]{1,8})(?:\.([0-9]*))?$)");
  std::smatch match;
  if (!std::regex_match(canonical, match, pattern))
    return std::nullopt;
  const std::string sign = match[1].str();
  const std::string integer = match[2].str();
  const bool has_point = match[3].matched;
  const std::string fraction = match[3].matched ? match[3].str() : std::string{};
  if (sign == "-")
    return negative_decimal_store_vp_splice_mantissa(integer, fraction);
  static const std::regex all_zero(R"(^0+$)");
  static const std::regex all_zero_star(R"(^0*$)");
  if (std::regex_match(integer, all_zero)) {
    if (fraction.empty() || std::regex_match(fraction, all_zero_star))
      return integer;
    return normalize_decimal_mantissa_entry("0." + fraction);
  }
  const std::string tail = integer.size() == 1U ? std::string{} : integer.substr(1);
  const bool zero_tail = tail.empty() || std::regex_match(tail, all_zero);
  const bool zero_fraction = fraction.empty() || std::regex_match(fraction, all_zero_star);
  if (zero_tail && zero_fraction)
    return std::optional<std::string>{"0."};
  if (tail.empty())
    return normalize_decimal_mantissa_entry(has_point ? "0." + fraction : std::string{"0."});
  return normalize_decimal_mantissa_entry(has_point ? tail + "." + fraction : tail);
}

// --- structural drop-first-digit model (dropFirstStructuralMantissaDigit) --
std::optional<std::string> drop_first_structural_mantissa_digit(const std::string& raw) {
  const std::string sign = raw.starts_with('-') ? "-" : "";
  const std::string unsigned_part = sign.empty() ? raw : raw.substr(1);
  bool removed = false;
  std::string output = sign;
  for (const std::string& point : utf8_codepoints(unsigned_part)) {
    if (!removed && is_structural_hex_digit(point)) {
      removed = true;
      continue;
    }
    output += point;
  }
  const std::size_t digit_count = shape_digits(output).size();
  if (!removed || digit_count == 0U || digit_count > 8U)
    return std::nullopt;
  return output;
}

// --- indirect-flow first-digit (indirectFlowVpFirstDigit/SpliceMantissa) ---
std::string indirect_flow_vp_first_digit(const X2MantissaDataModel& model) {
  for (const std::string& digit : model.digits)
    if (digit != "0")
      return "7";
  return "8";
}

std::optional<std::string> indirect_flow_vp_splice_mantissa(const X2MantissaDataModel& model) {
  if (model.radix != X2MantissaRadix::Decimal || model.digits.empty())
    return std::nullopt;
  const std::string source_digit = indirect_flow_vp_first_digit(model);
  const std::optional<std::string> spliced = replace_first_shape_digit(model.canonical, source_digit);
  if (!spliced.has_value() || decimal_mantissa_digit_count(*spliced) > 8)
    return std::nullopt;
  return normalize_decimal_mantissa_entry(*spliced).has_value() ? spliced : std::nullopt;
}

// --- vpEntry*FromStoreSplice ----------------------------------------------
std::optional<RegisterValueSet> vp_entry_mantissas_from_store_splice(const X2ShapeSet* shapes) {
  RegisterValueSet mantissas;
  if (shapes != nullptr) {
    for (const X2ShapeFact& fact : *shapes) {
      const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
      if (model.kind != X2ShapeModelKind::Mantissa || !model.mantissa.has_value() ||
          model.mantissa->radix != X2MantissaRadix::Decimal)
        continue;
      const std::optional<std::string> spliced =
          decimal_store_vp_splice_mantissa(model.mantissa->canonical);
      if (spliced.has_value())
        mantissas.insert(decimal_mantissa_data_model(*spliced).canonical);
    }
  }
  return mantissas.empty() ? std::nullopt : std::optional<RegisterValueSet>{mantissas};
}

std::optional<RegisterValueSet> vp_entry_sign_mantissas_from_store_splice(const X2ShapeSet* shapes) {
  RegisterValueSet mantissas;
  if (shapes != nullptr) {
    for (const X2ShapeFact& fact : *shapes) {
      const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
      if (model.kind != X2ShapeModelKind::Mantissa || !model.mantissa.has_value() ||
          model.mantissa->radix != X2MantissaRadix::Decimal)
        continue;
      if (decimal_mantissa_digit_count(model.mantissa->canonical) > 8)
        continue;
      if (!normalize_decimal_mantissa_entry(model.mantissa->canonical).has_value())
        continue;
      mantissas.insert(model.mantissa->canonical);
    }
  }
  return mantissas.empty() ? std::nullopt : std::optional<RegisterValueSet>{mantissas};
}

std::optional<X2ShapeSet> vp_entry_sign_shapes_from_store_splice(const X2ShapeSet* shapes) {
  return vp_entry_sign_shapes_from_shape_facts(shapes);
}

std::optional<X2ShapeSet> vp_entry_shapes_from_store_splice(const X2ShapeSet* shapes) {
  X2ShapeSet output;
  const X2ShapeSet canonical = canonical_structural_shape_facts(shapes);
  for (const X2ShapeFact& fact : structural_restore_shape_facts(canonical)) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind != X2ShapeModelKind::Mantissa || !model.mantissa.has_value())
      continue;
    if (model.mantissa->radix != X2MantissaRadix::Hex &&
        model.mantissa->radix != X2MantissaRadix::Super)
      continue;
    const std::optional<std::string> dropped =
        drop_first_structural_mantissa_digit(model.mantissa->canonical);
    if (!dropped.has_value())
      continue;
    const X2MantissaDataModel spliced =
        structural_mantissa_data_model(X2MantissaRadix::Hex, *dropped, X2ShapeSafety::StructuralOnly);
    const std::optional<X2ShapeFact> shape = x2_mantissa_shape_fact_from_model(spliced);
    if (shape.has_value())
      output.insert(*shape);
  }
  return output.empty() ? std::nullopt : std::optional<X2ShapeSet>{output};
}

// --- vpEntry*FromIndirectFlowSplice ---------------------------------------
std::optional<RegisterValueSet> vp_entry_mantissas_from_indirect_flow_splice(const X2ShapeSet* shapes) {
  RegisterValueSet mantissas;
  if (shapes != nullptr) {
    for (const X2ShapeFact& fact : *shapes) {
      const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
      if (model.kind != X2ShapeModelKind::Mantissa || !model.mantissa.has_value())
        continue;
      const std::optional<std::string> spliced = indirect_flow_vp_splice_mantissa(*model.mantissa);
      if (spliced.has_value())
        mantissas.insert(decimal_mantissa_data_model(*spliced).canonical);
    }
  }
  return mantissas.empty() ? std::nullopt : std::optional<RegisterValueSet>{mantissas};
}

std::optional<X2ShapeSet> vp_entry_shapes_from_indirect_flow_splice(const X2ShapeSet* shapes) {
  X2ShapeSet output;
  const X2ShapeSet canonical = canonical_structural_shape_facts(shapes);
  for (const X2ShapeFact& fact : structural_restore_shape_facts(canonical)) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind != X2ShapeModelKind::Mantissa || !model.mantissa.has_value())
      continue;
    if (model.mantissa->radix != X2MantissaRadix::Hex &&
        model.mantissa->radix != X2MantissaRadix::Super)
      continue;
    if (model.mantissa->digits.empty())
      continue;
    const std::optional<std::string> spliced =
        replace_first_shape_digit(model.mantissa->canonical, indirect_flow_vp_first_digit(*model.mantissa));
    if (!spliced.has_value() || shape_digits(*spliced).size() > 8U)
      continue;
    const X2MantissaDataModel model_out =
        structural_mantissa_data_model(X2MantissaRadix::Hex, *spliced, X2ShapeSafety::StructuralOnly);
    const std::optional<X2ShapeFact> shape = x2_mantissa_shape_fact_from_model(model_out);
    if (shape.has_value())
      output.insert(*shape);
  }
  return output.empty() ? std::nullopt : std::optional<X2ShapeSet>{output};
}

// --- with{Store,DirectFlow,IndirectFlow}VpSpliceSource --------------------
X2ValueDataflowState with_store_vp_splice_source(const X2ValueDataflowState& input) {
  X2ValueDataflowState output = internal_clone_x2_value_dataflow_state(input);
  const std::optional<X2ShapeSet> x2_shape =
      vp_splice_shape_set_with_value_shapes(&input.x2Shape, &input.x2);
  const X2ShapeSet* x2_ptr = x2_shape.has_value() ? &*x2_shape : nullptr;
  const std::optional<X2ShapeSet> vp_entry_shape = vp_entry_shapes_from_store_splice(x2_ptr);
  output.vpEntryMantissa = vp_entry_mantissas_from_store_splice(x2_ptr);
  output.vpEntrySignMantissa = vp_entry_sign_mantissas_from_store_splice(x2_ptr);
  output.vpEntryShape = vp_entry_shape;
  output.vpEntrySignShape = vp_entry_sign_shapes_from_store_splice(x2_ptr);
  if (vp_entry_shape.has_value())
    output.vpEntryShapeTransient = true;
  return output;
}

X2ValueDataflowState with_direct_flow_vp_splice_source(const X2ValueDataflowState& input) {
  X2ValueDataflowState output = internal_clone_x2_value_dataflow_state(input);
  const std::optional<X2ShapeSet> x_shape =
      vp_splice_shape_set_with_value_shapes(&input.xShape, &input.x);
  const std::optional<X2ShapeSet> x2_shape =
      vp_splice_shape_set_with_value_shapes(&input.x2Shape, &input.x2);
  const std::optional<RegisterValueSet> vp_entry_mantissa = decimal_first_digit_vp_splice_mantissas(
      x_shape.has_value() ? &*x_shape : nullptr, x2_shape.has_value() ? &*x2_shape : nullptr, false);
  const std::optional<X2ShapeSet> vp_entry_shape = structural_first_digit_vp_splice_shape_facts(
      x_shape.has_value() ? &*x_shape : nullptr, x2_shape.has_value() ? &*x2_shape : nullptr, false);
  output.vpEntryMantissa = vp_entry_mantissa;
  output.vpEntryShape = vp_entry_shape;
  if (vp_entry_mantissa.has_value())
    output.vpEntryMantissaTransient = true;
  if (vp_entry_shape.has_value())
    output.vpEntryShapeTransient = true;
  return output;
}

X2ValueDataflowState with_indirect_flow_vp_splice_source(const X2ValueDataflowState& input) {
  X2ValueDataflowState output = internal_clone_x2_value_dataflow_state(input);
  const std::optional<X2ShapeSet> x2_shape =
      vp_splice_shape_set_with_value_shapes(&input.x2Shape, &input.x2);
  const X2ShapeSet* x2_ptr = x2_shape.has_value() ? &*x2_shape : nullptr;
  const std::optional<RegisterValueSet> vp_entry_mantissa =
      vp_entry_mantissas_from_indirect_flow_splice(x2_ptr);
  const std::optional<X2ShapeSet> vp_entry_shape = vp_entry_shapes_from_indirect_flow_splice(x2_ptr);
  output.vpEntryMantissa = vp_entry_mantissa;
  output.vpEntryShape = vp_entry_shape;
  if (vp_entry_mantissa.has_value())
    output.vpEntryMantissaTransient = true;
  if (vp_entry_shape.has_value())
    output.vpEntryShapeTransient = true;
  return output;
}

// --- transferConditionalX2* wrappers --------------------------------------
X2ValueSet transfer_conditional_x2_value_set(const X2ValueDataflowState& input, const X2ValueSet& x,
                                             const X2ShapeSet* x_shape, X2Effect effect) {
  if (effect == X2Effect::Preserves)
    return canonical_x2_value_set(&input.x2);
  if (effect == X2Effect::Affects)
    return x2_sync_value_set_from_visible_x(x, x_shape);
  return {};
}

X2ShapeSet transfer_conditional_x2_shape_set(const X2ValueDataflowState& input, const X2ValueSet* x,
                                             const X2ShapeSet& x_shape, X2Effect effect) {
  if (effect == X2Effect::Preserves)
    return clone_optional_shape_set(&input.x2Shape);
  if (effect == X2Effect::Affects) {
    const std::set<X2ShapeFact> synced = x2_sync_shape_set_from_visible_x(&x_shape, x);
    return X2ShapeSet{synced.begin(), synced.end()};
  }
  return {};
}

X2VpContextState transfer_conditional_x2_vp_context_state(const X2ValueDataflowState& input,
                                                          X2Effect effect) {
  return effect == X2Effect::Preserves ? clone_x2_vp_context_state(input.vpContext)
                                       : none_x2_vp_context_state();
}

X2StructuralEntryState transfer_conditional_x2_structural_vp_context_state(
    const X2ValueDataflowState& input, X2Effect effect) {
  if (effect != X2Effect::Preserves)
    return none_x2_structural_entry_state();
  if (input.structuralEntry.kind == X2StructuralEntryState::Kind::Exponent)
    return x2_structural_context_from_entry(input.structuralEntry);
  return clone_x2_structural_entry_state(input.structuralVpContext);
}

std::optional<RegisterValueSet> transfer_conditional_x2_vp_entry_mantissa_state(
    const X2ValueSet& x, const X2ShapeSet& x_shape, const X2ShapeSet& x2_shape, X2Effect effect) {
  return effect == X2Effect::Affects ? shared_decimal_vp_entry_mantissas(&x, &x, &x_shape, &x2_shape)
                                     : std::nullopt;
}

std::optional<RegisterValueSet> transfer_conditional_x2_vp_entry_sign_mantissa_state(
    const X2ValueDataflowState& input, const X2ValueSet& x, const X2ShapeSet& x_shape,
    const X2ShapeSet& x2_shape, X2Effect effect) {
  if (effect == X2Effect::Affects)
    return shared_decimal_vp_entry_mantissas(&x, &x, &x_shape, &x2_shape);
  if (effect == X2Effect::Preserves)
    return clone_optional_string_set(
        input.vpEntrySignMantissa.has_value() ? &*input.vpEntrySignMantissa : nullptr);
  return std::nullopt;
}

std::optional<X2ShapeSet> transfer_conditional_x2_vp_entry_sign_shape_state(
    const X2ValueDataflowState& input, const X2ValueSet& x, const X2ShapeSet& x_shape,
    const X2ShapeSet& x2_shape, X2Effect effect) {
  if (effect == X2Effect::Affects)
    return shared_vp_entry_sign_shape_facts(&x, &x, &x_shape, &x2_shape);
  if (effect == X2Effect::Preserves)
    return std::optional<X2ShapeSet>{clone_optional_shape_set(
        input.vpEntrySignShape.has_value() ? &*input.vpEntrySignShape : nullptr)};
  return std::nullopt;
}

std::optional<X2ShapeSet> transfer_conditional_x2_vp_entry_shape_state(const X2ValueSet& x,
                                                                       const X2ShapeSet& x_shape,
                                                                       const X2ShapeSet& x2_shape,
                                                                       X2Effect effect) {
  return effect == X2Effect::Affects ? shared_structural_shape_facts(&x, &x, &x_shape, &x2_shape)
                                     : std::nullopt;
}

std::set<X2ValueFact> normalize_x2_restore_facts_for_x(const X2ValueSet& input) {
  std::set<X2ValueFact> output;
  for (const X2ValueFact& fact : input) {
    if (fact.starts_with("decimal:") &&
        (fact.ends_with(":normalized") || fact.ends_with(":unnormalized"))) {
      const std::size_t suffix_len = fact.ends_with(":normalized") ? 11 : 13;
      const std::string mid = fact.substr(8, fact.size() - 8 - suffix_len);
      if (is_plain_decimal_number(mid)) {
        const std::optional<std::string> normalized = normalize_plain_decimal(mid);
        if (normalized.has_value())
          output.insert(decimal_value_fact(*normalized, "normalized"));
        continue;
      }
    }
    output.insert(fact);
  }
  return output;
}

std::set<X2ShapeFact> normalize_x2_restore_shapes_for_x(const X2ShapeSet* input) {
  std::set<X2ShapeFact> output;
  for (const X2ShapeFact& fact : canonical_shape_set(input)) {
    constexpr std::string_view prefix = "mantissa:";
    constexpr std::string_view suffix = ":decimal";
    if (fact.starts_with(prefix) && fact.ends_with(suffix)) {
      const std::string raw =
          fact.substr(prefix.size(), fact.size() - prefix.size() - suffix.size());
      if (is_plain_decimal_number(raw)) {
        const std::optional<std::string> normalized = normalize_plain_decimal(raw);
        const std::optional<X2ShapeFact> shape =
            normalized.has_value() ? exact_decimal_display_shape_fact(*normalized) : std::nullopt;
        if (shape.has_value())
          output.insert(*shape);
        continue;
      }
    }
    output.insert(fact);
  }
  return output;
}

std::set<std::string> dot_safe_structural_mantissa_restore_keys(const X2ShapeSet* input) {
  std::set<std::string> keys;
  const X2ShapeSet canonical = canonical_structural_shape_facts(input);
  for (const X2ShapeFact& fact : structural_mantissa_shape_facts(canonical)) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind != X2ShapeModelKind::Mantissa || !model.mantissa.has_value() ||
        model.mantissa->radix != X2MantissaRadix::Hex || !model.mantissa->sign.empty() ||
        model.mantissa->hasDecimalPoint || model.mantissa->digits.size() != 1)
      continue;
    const std::optional<int> digit = structural_hex_nibble_value(model.mantissa->digits.at(0));
    if (digit.has_value() && (*digit == 10 || *digit == 11 || *digit == 12))
      keys.insert("hex-digit:" + std::to_string(*digit));
  }
  return keys;
}

bool x2_shape_set_has_only_dot_safe_structural_mantissas(const X2ShapeSet* input) {
  const X2ShapeSet shapes = canonical_structural_shape_facts(input);
  if (shapes.empty())
    return false;
  for (const X2ShapeFact& fact : shapes) {
    const X2ShapeSet single{fact};
    if (dot_safe_structural_mantissa_restore_keys(&single).empty())
      return false;
  }
  return true;
}

std::set<X2ShapeFact> dot_restore_direct_shape_facts(const X2ShapeSet* shapes) {
  const X2ShapeSet canonical = canonical_shape_set(shapes);
  if (canonical.empty())
    return {};
  const std::set<X2ShapeFact> direct = direct_structural_mantissa_shape_facts(&canonical);
  if (direct.size() != canonical.size())
    return {};
  const X2ShapeSet direct_set{direct.begin(), direct.end()};
  return x2_shape_set_has_only_dot_safe_structural_mantissas(&direct_set) ? direct
                                                                          : std::set<X2ShapeFact>{};
}

std::string signed_normalized_decimal_value(const std::string& raw) {
  const std::optional<std::string> normalized = normalize_plain_decimal(raw);
  if (!normalized.has_value() || *normalized == "0")
    return "0";
  return normalized->starts_with('-') ? normalized->substr(1) : "-" + *normalized;
}

X2ShapeFact sign_changed_structural_mantissa_shape_fact(const X2ShapeFact& fact) {
  const std::optional<X2ShapeFact> signed_fact = x2_mantissa_sign_changed_shape_fact(fact);
  return signed_fact.has_value() ? *signed_fact : fact;
}

std::set<X2ShapeFact> x2_sign_changed_shared_structural_shape_facts(const X2ShapeSet* x_shapes,
                                                                    const X2ShapeSet* x2_shapes) {
  std::set<X2ShapeFact> output;
  const std::set<X2ShapeFact> visible_restore =
      structural_restore_shape_facts(canonical_structural_shape_facts(x_shapes));
  const std::set<std::string> visible_exact = x2_shape_set_exact_decimal_displays(x_shapes);
  for (const X2ShapeFact& fact : canonical_structural_shape_facts(x2_shapes)) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    const X2ShapeSet single{fact};
    const std::set<X2ShapeFact> restore = structural_restore_shape_facts(single);
    if (!structural_shape_set_has_intersection(visible_restore, restore) &&
        !structural_shape_set_has_exact_decimal_display_intersection(visible_exact, restore))
      continue;
    if (model.kind == X2ShapeModelKind::Mantissa && model.mantissa.has_value() &&
        (model.mantissa->radix == X2MantissaRadix::Hex ||
         model.mantissa->radix == X2MantissaRadix::Super)) {
      output.insert(sign_changed_structural_mantissa_shape_fact(fact));
      continue;
    }
    if (model.kind == X2ShapeModelKind::ExponentEntry && model.mantissa.has_value() &&
        (model.mantissa->radix == X2MantissaRadix::Hex ||
         model.mantissa->radix == X2MantissaRadix::Super) &&
        model.safety == X2ShapeSafety::StructuralOnly) {
      const std::optional<X2ShapeFact> signed_fact = x2_exponent_mantissa_sign_changed_shape_fact(fact);
      if (signed_fact.has_value())
        output.insert(*signed_fact);
    }
  }
  return output;
}

std::set<std::string> shared_structural_restore_source_keys(const X2ShapeSet* x_shapes,
                                                            const X2ShapeSet* x2_shapes) {
  const std::set<X2ShapeFact> x_restore = canonical_structural_restore_source_key_facts(x_shapes);
  const std::set<std::string> x_exact = x2_shape_set_exact_decimal_displays(x_shapes);
  std::set<std::string> keys;
  for (const X2ShapeFact& fact : canonical_structural_restore_source_key_facts(x2_shapes)) {
    if (x_restore.contains(fact)) {
      keys.insert(stable_structural_expression_source_key(fact));
      continue;
    }
    const std::optional<std::string> display = x2_shape_fact_exact_decimal_display(fact);
    if (display.has_value() && x_exact.contains(*display))
      keys.insert(stable_expression_shape_source_key(fact));
  }
  return keys;
}

std::set<std::string> shared_restored_display_source_keys(const X2ShapeSet* x_shapes,
                                                          const X2ShapeSet* x2_shapes) {
  const std::set<X2ShapeFact> x_source = x2_restored_display_source_key_shape_facts(x_shapes);
  std::set<std::string> keys;
  for (const X2ShapeFact& fact : x2_restored_display_source_key_shape_facts(x2_shapes)) {
    if (x_source.contains(fact))
      keys.insert(stable_structural_expression_source_key(fact));
  }
  return keys;
}

std::optional<RegisterValueSet> vp_entry_mantissas_from_restored_value_facts(const X2ValueSet& values) {
  RegisterValueSet mantissas;
  for (const X2ValueFact& value : values) {
    const std::optional<std::string> decimal = computation_decimal_value_from_fact(value);
    if (decimal.has_value())
      mantissas.insert(*decimal);
  }
  return mantissas.empty() ? std::nullopt : std::optional<RegisterValueSet>{mantissas};
}

std::optional<RegisterValueSet> vp_entry_mantissas_from_x2_restore(const X2ValueSet& values,
                                                                   const X2ShapeSet* shapes) {
  RegisterValueSet mantissas;
  const std::set<std::string> signed_zero = signed_zero_decimal_mantissa_shapes(shapes);
  const std::optional<RegisterValueSet> restored = vp_entry_mantissas_from_restored_value_facts(values);
  if (restored.has_value()) {
    for (const std::string& raw : *restored) {
      if (raw != "0" || signed_zero.empty())
        mantissas.insert(raw);
    }
  }
  for (const std::string& raw : signed_zero)
    mantissas.insert(raw);
  return mantissas.empty() ? std::nullopt : std::optional<RegisterValueSet>{mantissas};
}

std::optional<RegisterValueSet> vp_entry_sign_mantissas_from_x2_restore(const X2ValueSet& values,
                                                                        const X2ShapeSet* shapes) {
  RegisterValueSet mantissas;
  for (const X2ValueFact& value : values) {
    if (!value.starts_with("decimal:"))
      continue;
    std::string mid;
    if (value.ends_with(":normalized"))
      mid = value.substr(8, value.size() - 8 - 11);
    else if (value.ends_with(":unnormalized"))
      mid = value.substr(8, value.size() - 8 - 13);
    else
      continue;
    if (is_plain_decimal_number(mid) && normalize_decimal_mantissa_entry(mid).has_value())
      mantissas.insert(mid);
  }
  for (const std::string& raw : signed_zero_decimal_mantissa_shapes(shapes))
    mantissas.insert(raw);
  return mantissas.empty() ? std::nullopt : std::optional<RegisterValueSet>{mantissas};
}

std::optional<RegisterValueSet> vp_entry_sign_source_mantissas(const X2ValueDataflowState& input) {
  if (input.vpEntrySignMantissa.has_value())
    return input.vpEntrySignMantissa;
  if (input.vpEntryMantissa.has_value())
    return input.vpEntryMantissa;
  return shared_normalized_decimal_mantissas(&input.x, &input.x2);
}

std::optional<RegisterValueSet> sign_changed_vp_entry_mantissas(const X2ValueDataflowState& input) {
  const std::optional<RegisterValueSet> sign_source = vp_entry_sign_source_mantissas(input);
  if (sign_source.has_value())
    return sign_changed_mantissa_shapes(*sign_source);
  return std::nullopt;
}

bool x_value_or_shape_can_feed_closed_decimal_sign_change(const X2ValueSet* x, const X2ShapeSet* x_shape,
                                                          const X2ShapeFact& x2_shape,
                                                          const std::string& normalized) {
  if (x_shape != nullptr && x_shape->contains(decimal_mantissa_shape_fact(normalized)))
    return true;
  if (x2_value_shape_set_has_exact_decimal_display(x, x_shape,
                                                   decimal_value_fact(normalized, "normalized")))
    return true;
  const X2ShapeSet single{x2_shape};
  return x2_shape_sets_have_same_decimal_display_shape(x_shape, &single);
}

// --- sign-change closed-decimal subtree -----------------------------------

std::optional<RegisterValueSet> sign_changed_closed_shape_mantissas(const X2ValueDataflowState& input) {
  RegisterValueSet mantissas;
  const X2ShapeSet x_shape = shape_set_with_stable_expression_value_shapes(&input.xShape, &input.x);
  const X2ShapeSet x2_shape = shape_set_with_stable_expression_value_shapes(&input.x2Shape, &input.x2);
  for (const X2ShapeFact& fact : x2_shape) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind != X2ShapeModelKind::Mantissa || !model.mantissa.has_value() ||
        model.mantissa->radix != X2MantissaRadix::Decimal ||
        model.mantissa->safety != X2ShapeSafety::DotSafeDecimal)
      continue;
    const std::string raw = model.mantissa->canonical;
    const std::optional<std::string> normalized = normalize_decimal_mantissa_entry(raw);
    if (!normalized.has_value() || !model.mantissa->normalizedDecimal.has_value())
      continue;
    if (!x_value_or_shape_can_feed_closed_decimal_sign_change(&input.x, &x_shape, fact, *normalized))
      continue;
    const std::optional<std::string> signed_raw = sign_changed_mantissa_shape(raw);
    if (!signed_raw.has_value())
      return std::nullopt;
    mantissas.insert(*signed_raw);
  }
  return mantissas.empty() ? std::nullopt : std::optional<RegisterValueSet>{mantissas};
}

std::optional<X2ValueDataflowState>
sign_changed_closed_decimal_exponent_shape_state(const X2ValueDataflowState& input) {
  X2ValueSet values;
  X2ShapeSet shapes;
  RegisterValueSet mantissas;
  const X2ValueSet x_values = canonical_x2_value_set(&input.x);
  const X2ValueSet x2_values = canonical_x2_value_set(&input.x2);
  const X2ShapeSet x_shape = shape_set_with_stable_expression_value_shapes(&input.xShape, &input.x);
  const X2ShapeSet x2_shape = shape_set_with_stable_expression_value_shapes(&input.x2Shape, &input.x2);
  for (const X2ShapeFact& fact : x2_shape) {
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind != X2ShapeModelKind::ExponentEntry || !model.mantissa.has_value() ||
        model.mantissa->radix != X2MantissaRadix::Decimal)
      continue;
    if (!model.normalizedDecimal.has_value())
      continue;
    const std::string normalized = *model.normalizedDecimal;
    const X2ValueFact source_value = decimal_value_fact(normalized, "normalized");
    const bool has_shared_value = x_values.contains(source_value) && x2_values.contains(source_value);
    const X2ShapeSet single{fact};
    const bool has_shared_display_shape =
        x2_shape_sets_have_same_decimal_display_shape(&x_shape, &single);
    const bool has_shared_restored =
        x2_value_shape_set_has_exact_decimal_display(&input.x, &x_shape, source_value);
    if (!has_shared_value && !has_shared_display_shape && !has_shared_restored)
      continue;
    const std::optional<X2ShapeFact> signed_shape = x2_exponent_mantissa_sign_changed_shape_fact(fact);
    if (!signed_shape.has_value())
      continue;
    const std::string signed_decimal = signed_normalized_decimal_value(normalized);
    shapes.insert(*signed_shape);
    const std::optional<X2ShapeFact> signed_display = exact_decimal_display_shape_fact(signed_decimal);
    if (signed_display.has_value())
      shapes.insert(*signed_display);
    if (has_shared_value) {
      values.insert(decimal_value_fact(signed_decimal, "normalized"));
      mantissas.insert(signed_decimal);
    }
    if (!has_shared_value) {
      std::set<std::string> keys = shared_restored_display_source_keys(&x_shape, &single);
      if (keys.empty() && has_shared_restored)
        keys.insert(stable_structural_expression_source_key(fact));
      for (const std::string& key : keys)
        values.insert(stable_expression_value_fact("0B", key));
    }
  }
  if (values.empty() && shapes.empty())
    return std::nullopt;
  return X2ValueDataflowState{
      .x = canonical_x2_value_set(&values),
      .y = clone_optional_value_set(input.y.has_value() ? &*input.y : nullptr),
      .x2 = canonical_x2_value_set(&values),
      .xShape = shapes,
      .yShape = clone_optional_shape_set(input.yShape.has_value() ? &*input.yShape : nullptr),
      .x2Shape = X2ShapeSet{shapes.begin(), shapes.end()},
      .xDirectShape = X2ShapeSet{},
      .yDirectShape =
          clone_optional_shape_set(input.yDirectShape.has_value() ? &*input.yDirectShape : nullptr),
      .entry = closed_x2_entry_state(),
      .vpContext = none_x2_vp_context_state(),
      .structuralEntry = none_x2_structural_entry_state(),
      .structuralVpContext = none_x2_structural_entry_state(),
      .vpEntryMantissa = mantissas.empty() ? std::optional<RegisterValueSet>{}
                                           : std::optional<RegisterValueSet>{mantissas},
      .vpEntrySignMantissa = mantissas.empty() ? std::optional<RegisterValueSet>{}
                                               : std::optional<RegisterValueSet>{mantissas},
      .vpEntryShape = vp_entry_shapes_from_shape_facts(&shapes),
      .vpEntrySignShape = vp_entry_sign_shapes_from_shape_facts(&shapes),
      .memory = clone_x2_value_memory_field(input.memory),
      .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
  };
}

std::optional<X2ShapeSet> sign_changed_closed_structural_shape_facts(const X2ValueDataflowState& input) {
  const X2ShapeSet x_shape = shape_set_with_stable_expression_value_shapes(&input.xShape, &input.x);
  const X2ShapeSet x2_shape = shape_set_with_stable_expression_value_shapes(&input.x2Shape, &input.x2);
  const std::set<X2ShapeFact> shapes = x2_sign_changed_shared_structural_shape_facts(&x_shape, &x2_shape);
  return shapes.empty() ? std::nullopt : std::optional<X2ShapeSet>{X2ShapeSet{shapes.begin(), shapes.end()}};
}

std::optional<X2ValueDataflowState>
sign_changed_vp_entry_decimal_shape_state(const X2ValueDataflowState& input) {
  if (!input.vpEntrySignShape.has_value())
    return std::nullopt;
  const X2ShapeSet& source = *input.vpEntrySignShape;
  X2ValueSet values;
  X2ShapeSet shapes;
  for (const X2ShapeFact& fact : decimal_display_shape_facts(&source)) {
    std::optional<X2ShapeFact> signed_shape = x2_exponent_mantissa_sign_changed_shape_fact(fact);
    if (!signed_shape.has_value())
      signed_shape = x2_mantissa_sign_changed_shape_fact(fact);
    if (!signed_shape.has_value())
      continue;
    shapes.insert(*signed_shape);
    const std::optional<std::string> visible = x2_shape_fact_restored_visible_decimal(fact);
    if (visible.has_value()) {
      const std::optional<X2ShapeFact> signed_display =
          exact_decimal_display_shape_fact(signed_normalized_decimal_value(*visible));
      if (signed_display.has_value())
        shapes.insert(*signed_display);
    }
    values.insert(stable_expression_value_fact("0B", stable_structural_expression_source_key(fact)));
  }
  if (values.empty() || shapes.empty())
    return std::nullopt;
  return X2ValueDataflowState{
      .x = canonical_x2_value_set(&values),
      .y = clone_optional_value_set(input.y.has_value() ? &*input.y : nullptr),
      .x2 = canonical_x2_value_set(&values),
      .xShape = shapes,
      .yShape = clone_optional_shape_set(input.yShape.has_value() ? &*input.yShape : nullptr),
      .x2Shape = X2ShapeSet{shapes.begin(), shapes.end()},
      .xDirectShape = X2ShapeSet{},
      .yDirectShape =
          clone_optional_shape_set(input.yDirectShape.has_value() ? &*input.yDirectShape : nullptr),
      .entry = closed_x2_entry_state(),
      .vpContext = none_x2_vp_context_state(),
      .structuralEntry = none_x2_structural_entry_state(),
      .structuralVpContext = none_x2_structural_entry_state(),
      .vpEntrySignShape = vp_entry_sign_shapes_from_shape_facts(&shapes),
      .memory = clone_x2_value_memory_field(input.memory),
      .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
  };
}

std::optional<X2ShapeSet> sign_changed_vp_entry_structural_shape_facts(const X2ValueDataflowState& input) {
  if (!input.vpEntrySignShape.has_value())
    return std::nullopt;
  const X2ShapeSet& source = *input.vpEntrySignShape;
  X2ShapeSet shapes;
  for (const X2ShapeFact& fact : canonical_structural_shape_facts(&source)) {
    std::optional<X2ShapeFact> signed_fact = x2_exponent_mantissa_sign_changed_shape_fact(fact);
    if (!signed_fact.has_value())
      signed_fact = x2_mantissa_sign_changed_shape_fact(fact);
    if (!signed_fact.has_value())
      continue;
    const X2ShapeFact canonical = x2_canonical_shape_fact(*signed_fact);
    if (x2_shape_fact_safety(canonical) == X2ShapeSafety::StructuralOnly)
      shapes.insert(canonical);
  }
  return shapes.empty() ? std::nullopt : std::optional<X2ShapeSet>{shapes};
}

std::optional<X2ValueDataflowState>
sign_changed_closed_structural_state(const X2ValueDataflowState& input, std::optional<int> producer_index) {
  const std::optional<X2ShapeSet> structural_shapes = sign_changed_closed_structural_shape_facts(input);
  if (!structural_shapes.has_value())
    return std::nullopt;
  X2ValueDataflowState state = x2_value_state_from_structural_shapes(
      *structural_shapes, input.memory, input.shapeMemory,
      input.y.has_value() ? &*input.y : nullptr, input.yShape.has_value() ? &*input.yShape : nullptr,
      input.yDirectShape.has_value() ? &*input.yDirectShape : nullptr);
  X2ValueSet values;
  if (producer_index.has_value())
    values.insert(expression_value_fact(*producer_index));
  const X2ShapeSet x_shape = shape_set_with_stable_expression_value_shapes(&input.xShape, &input.x);
  const X2ShapeSet x2_shape = shape_set_with_stable_expression_value_shapes(&input.x2Shape, &input.x2);
  for (const std::string& key : shared_structural_restore_source_keys(&x_shape, &x2_shape))
    values.insert(stable_expression_value_fact("0B", key));
  if (values.empty())
    return state;
  state.x = canonical_x2_value_set(&values);
  state.x2 = canonical_x2_value_set(&values);
  return state;
}

std::optional<X2ValueDataflowState>
sign_change_closed_decimal_state(const X2ValueDataflowState& input, std::optional<int> producer_index) {
  const std::optional<X2ValueDataflowState> exponent_shape_backed =
      sign_changed_closed_decimal_exponent_shape_state(input);
  if (exponent_shape_backed.has_value())
    return exponent_shape_backed;
  const X2ValueSet* yp = input.y.has_value() ? &*input.y : nullptr;
  const X2ShapeSet* ysp = input.yShape.has_value() ? &*input.yShape : nullptr;
  const X2ShapeSet* ydp = input.yDirectShape.has_value() ? &*input.yDirectShape : nullptr;
  const std::optional<RegisterValueSet> shaped = sign_changed_vp_entry_mantissas(input);
  if (shaped.has_value()) {
    bool ok = false;
    X2ValueDataflowState state =
        x2_value_state_from_mantissa_shapes(*shaped, input.memory, input.shapeMemory, yp, ysp, ydp, ok);
    if (ok)
      return state;
  }
  const std::optional<RegisterValueSet> shape_backed = sign_changed_closed_shape_mantissas(input);
  if (shape_backed.has_value()) {
    bool ok = false;
    X2ValueDataflowState state = x2_value_state_from_mantissa_shapes(*shape_backed, input.memory,
                                                                     input.shapeMemory, yp, ysp, ydp, ok);
    if (ok)
      return state;
  }
  const std::optional<X2ValueDataflowState> decimal_shape_source =
      sign_changed_vp_entry_decimal_shape_state(input);
  if (decimal_shape_source.has_value())
    return decimal_shape_source;
  const std::optional<X2ValueDataflowState> structural_state =
      sign_changed_closed_structural_state(input, producer_index);
  if (structural_state.has_value())
    return structural_state;
  const std::optional<X2ShapeSet> structural_source =
      sign_changed_vp_entry_structural_shape_facts(input);
  if (structural_source.has_value())
    return x2_value_state_from_structural_shapes(*structural_source, input.memory, input.shapeMemory,
                                                 yp, ysp, ydp);

  X2ValueSet values;
  const X2ValueFact opaque =
      producer_index.has_value() ? expression_value_fact(*producer_index) : kTxSameUnknownValue;
  const X2ValueSet x_values = canonical_x2_value_set(&input.x);
  const X2ValueSet x2_values = canonical_x2_value_set(&input.x2);
  if (x_values.contains(kTxSameUnknownValue) && x2_values.contains(kTxSameUnknownValue))
    values.insert(opaque);
  for (const X2ValueFact& fact : x2_values) {
    const bool same_visible =
        x_values.contains(fact) ||
        x2_value_shape_set_has_restored_visible_decimal(&x_values, &input.xShape, fact);
    if (!same_visible)
      continue;
    const std::optional<std::string> decimal = normalized_decimal_value_from_fact(fact);
    if (!decimal.has_value() && is_opaque_shared_value_fact(fact)) {
      values.insert(opaque);
      const std::optional<std::string> key = stable_expression_source_key(fact, true);
      if (key.has_value())
        values.insert(stable_expression_value_fact("0B", *key));
      continue;
    }
    if (!decimal.has_value())
      continue;
    values.insert(decimal_value_fact(signed_normalized_decimal_value(*decimal), "normalized"));
  }
  if (values.empty())
    return std::nullopt;
  X2ShapeSet shapes;
  for (const X2ValueFact& value : values) {
    const std::optional<std::string> decimal = normalized_decimal_value_from_fact(value);
    const std::optional<X2ShapeFact> shape =
        decimal.has_value() ? exact_decimal_display_shape_fact(*decimal) : std::nullopt;
    if (shape.has_value())
      shapes.insert(*shape);
  }
  return X2ValueDataflowState{
      .x = canonical_x2_value_set(&values),
      .y = clone_optional_value_set(yp),
      .x2 = canonical_x2_value_set(&values),
      .xShape = shapes,
      .yShape = clone_optional_shape_set(ysp),
      .x2Shape = X2ShapeSet{shapes.begin(), shapes.end()},
      .xDirectShape = X2ShapeSet{},
      .yDirectShape = clone_optional_shape_set(ydp),
      .entry = closed_x2_entry_state(),
      .vpContext = none_x2_vp_context_state(),
      .structuralEntry = none_x2_structural_entry_state(),
      .structuralVpContext = none_x2_structural_entry_state(),
      .vpEntryMantissa = vp_entry_mantissas_from_value_facts(values),
      .vpEntrySignMantissa = vp_entry_mantissas_from_value_facts(values),
      .vpEntrySignShape = vp_entry_sign_shapes_from_shape_facts(&shapes),
      .memory = clone_x2_value_memory_field(input.memory),
      .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
  };
}

// --- transferPlain set helpers + visible-stack helpers --------------------

X2ValueSet transfer_plain_x2_value_set(const X2ValueDataflowState& input, const X2ValueSet& x,
                                       const X2ShapeSet* x_shape, X2Effect effect) {
  if (effect == X2Effect::Preserves)
    return canonical_x2_value_set(&input.x2);
  if (effect == X2Effect::Affects)
    return x2_sync_value_set_from_visible_x(x, x_shape);
  return {};
}

X2ShapeSet transfer_plain_x2_shape_set(const X2ValueDataflowState& input, const X2ValueSet* x,
                                       const X2ShapeSet& x_shape, X2Effect effect,
                                       const X2ShapeSet* closed_entry_shape) {
  if (effect == X2Effect::Preserves)
    return clone_optional_shape_set(closed_entry_shape != nullptr ? closed_entry_shape : &input.x2Shape);
  if (effect == X2Effect::Affects) {
    const std::set<X2ShapeFact> synced = x2_sync_shape_set_from_visible_x(&x_shape, x);
    return X2ShapeSet{synced.begin(), synced.end()};
  }
  return {};
}

X2ValueSet transfer_plain_y_value_set(const X2ValueDataflowState& input, const X2ValueSet& source_x,
                                      const IrOp& op) {
  const OpcodeInfo& info = opcode_by_code(op.opcode);
  if (info.stack_effect == StackEffect::Shifts)
    return canonical_x2_value_set(&source_x);
  if (info.stack_effect == StackEffect::Preserves)
    return clone_optional_value_set(input.y.has_value() ? &*input.y : nullptr);
  if (info.stack_effect == StackEffect::ConsumeYKeep && info.risk == OpcodeRisk::Documented)
    return clone_optional_value_set(input.y.has_value() ? &*input.y : nullptr);
  return {};
}

X2ShapeSet transfer_plain_y_shape_set(const X2ValueDataflowState& input,
                                      const X2ShapeSet* source_x_shape, const IrOp& op) {
  const OpcodeInfo& info = opcode_by_code(op.opcode);
  if (info.stack_effect == StackEffect::Shifts)
    return clone_optional_shape_set(source_x_shape);
  if (info.stack_effect == StackEffect::Preserves)
    return clone_optional_shape_set(input.yShape.has_value() ? &*input.yShape : nullptr);
  if (info.stack_effect == StackEffect::ConsumeYKeep && info.risk == OpcodeRisk::Documented)
    return clone_optional_shape_set(input.yShape.has_value() ? &*input.yShape : nullptr);
  return {};
}

X2ShapeSet transfer_plain_y_direct_shape_set(const X2ValueDataflowState& input,
                                             const X2ShapeSet* source_x_direct_shape, const IrOp& op) {
  const OpcodeInfo& info = opcode_by_code(op.opcode);
  if (info.stack_effect == StackEffect::Shifts)
    return clone_optional_shape_set(source_x_direct_shape);
  if (info.stack_effect == StackEffect::Preserves)
    return clone_optional_shape_set(input.yDirectShape.has_value() ? &*input.yDirectShape : nullptr);
  if (info.stack_effect == StackEffect::ConsumeYKeep && info.risk == OpcodeRisk::Documented)
    return clone_optional_shape_set(input.yDirectShape.has_value() ? &*input.yDirectShape : nullptr);
  return {};
}

X2ShapeSet plain_direct_shape_after_plain_op(const IrOp& op, const X2ShapeSet* source_x_direct_shape,
                                             const X2ShapeSet* result_x_shape) {
  return plain_preserves_x_value(op) ? clone_optional_shape_set(source_x_direct_shape)
                                     : clone_optional_shape_set(result_x_shape);
}

X2ValueSet sync_unknown_same_value(X2ValueSet x, X2Effect effect, std::optional<int> producer_index) {
  if (effect == X2Effect::Affects && x.empty())
    x.insert(producer_index.has_value() ? expression_value_fact(*producer_index)
                                        : X2ValueFact{kTxSameUnknownValue});
  return canonical_x2_value_set(&x);
}

X2VpContextState transfer_plain_x2_vp_context_state(const X2ValueDataflowState& input, X2Effect effect) {
  return effect == X2Effect::Preserves ? clone_x2_vp_context_state(input.vpContext)
                                       : none_x2_vp_context_state();
}

X2StructuralEntryState transfer_plain_x2_structural_vp_context_state(const X2ValueDataflowState& input,
                                                                     X2Effect effect) {
  if (effect != X2Effect::Preserves)
    return none_x2_structural_entry_state();
  if (input.structuralEntry.kind == X2StructuralEntryState::Kind::Exponent)
    return x2_structural_context_from_entry(input.structuralEntry);
  return clone_x2_structural_entry_state(input.structuralVpContext);
}

X2EntryState next_x2_entry_state_for_plain_effect(X2Effect effect) {
  if (effect == X2Effect::Restores)
    return unknown_x2_entry_state();
  return closed_x2_entry_state();
}

X2ValueSet visible_x2_value_facts_for_stack(const X2ValueDataflowState& input) {
  const std::set<X2ValueFact> closed = closed_exponent_entry_decimal_facts(input.entry);
  if (!closed.empty())
    return X2ValueSet{closed.begin(), closed.end()};
  return input.x;
}

X2ShapeSet visible_x2_shape_facts_for_stack(const X2ValueDataflowState& input) {
  const std::set<X2ShapeFact> closed = closed_exponent_entry_shape_facts(input.entry);
  if (!closed.empty())
    return X2ShapeSet{closed.begin(), closed.end()};
  if (input.structuralEntry.kind == X2StructuralEntryState::Kind::Exponent) {
    const std::set<X2ShapeFact> structural = structural_exponent_entry_shape_facts(input.structuralEntry);
    return X2ShapeSet{structural.begin(), structural.end()};
  }
  const std::optional<X2ShapeSet> effective = effective_input_x_shape(&input.xShape, &input.x);
  return clone_optional_shape_set(effective.has_value() ? &*effective : nullptr);
}

X2ShapeSet visible_x2_direct_shape_facts_for_stack(const X2ValueDataflowState& input) {
  const std::set<X2ShapeFact> closed = closed_exponent_entry_shape_facts(input.entry);
  if (!closed.empty())
    return X2ShapeSet{closed.begin(), closed.end()};
  if (input.structuralEntry.kind == X2StructuralEntryState::Kind::Exponent) {
    const std::set<X2ShapeFact> structural = structural_exponent_entry_shape_facts(input.structuralEntry);
    return X2ShapeSet{structural.begin(), structural.end()};
  }
  return clone_optional_shape_set(input.xDirectShape.has_value() ? &*input.xDirectShape : nullptr);
}

// --- dispatchers ----------------------------------------------------------

X2ValueDataflowState transfer_decimal_digit_x2_value_state(const X2ValueDataflowState& input,
                                                           const std::string& digit) {
  const X2ValueSet* yp = input.y.has_value() ? &*input.y : nullptr;
  const X2ShapeSet* ysp = input.yShape.has_value() ? &*input.yShape : nullptr;
  const X2ShapeSet* ydp = input.yDirectShape.has_value() ? &*input.yDirectShape : nullptr;
  if (input.structuralEntry.kind == X2StructuralEntryState::Kind::Exponent) {
    const X2StructuralEntryState entry = advance_structural_exponent_digit_entry(input.structuralEntry, digit);
    return x2_value_state_from_structural_exponent_entry(entry, input.memory, input.shapeMemory, yp, ysp, ydp);
  }
  if (input.entry.kind == X2EntryState::Kind::Exponent) {
    const X2EntryState entry = advance_exponent_digit_entry(input.entry, digit);
    return x2_value_state_from_exponent_entry(entry, input.memory, input.shapeMemory, yp, ysp, ydp);
  }
  const X2EntryState entry = advance_decimal_digit_entry(input.entry, digit);
  if (entry.kind != X2EntryState::Kind::Open) {
    return X2ValueDataflowState{
        .x = X2ValueSet{},
        .y = clone_optional_value_set(yp),
        .x2 = X2ValueSet{},
        .xShape = X2ShapeSet{},
        .yShape = clone_optional_shape_set(ysp),
        .x2Shape = X2ShapeSet{},
        .xDirectShape = X2ShapeSet{},
        .yDirectShape = clone_optional_shape_set(ydp),
        .entry = entry,
        .vpContext = none_x2_vp_context_state(),
        .structuralEntry = none_x2_structural_entry_state(),
        .structuralVpContext = none_x2_structural_entry_state(),
        .memory = clone_x2_value_memory_field(input.memory),
        .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
    };
  }
  return x2_value_state_from_open_decimal_entry(entry, input.memory, input.shapeMemory, yp, ysp, ydp);
}

X2ValueDataflowState transfer_dot_restore_x2_value_state(const X2ValueDataflowState& input) {
  const X2ValueSet* yp = input.y.has_value() ? &*input.y : nullptr;
  const X2ShapeSet* ysp = input.yShape.has_value() ? &*input.yShape : nullptr;
  const X2ShapeSet* ydp = input.yDirectShape.has_value() ? &*input.yDirectShape : nullptr;
  if (input.entry.kind == X2EntryState::Kind::Open) {
    const X2EntryState entry = advance_decimal_point_entry(input.entry);
    if (entry.kind == X2EntryState::Kind::Open)
      return x2_value_state_from_open_decimal_entry(entry, input.memory, input.shapeMemory, yp, ysp, ydp);
    return X2ValueDataflowState{
        .x = X2ValueSet{},
        .y = clone_optional_value_set(yp),
        .x2 = X2ValueSet{},
        .xShape = X2ShapeSet{},
        .yShape = clone_optional_shape_set(ysp),
        .x2Shape = X2ShapeSet{},
        .xDirectShape = X2ShapeSet{},
        .yDirectShape = clone_optional_shape_set(ydp),
        .entry = entry,
        .vpContext = none_x2_vp_context_state(),
        .structuralEntry = none_x2_structural_entry_state(),
        .structuralVpContext = none_x2_structural_entry_state(),
        .memory = clone_x2_value_memory_field(input.memory),
        .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
    };
  }
  if (input.entry.kind != X2EntryState::Kind::Closed) {
    return X2ValueDataflowState{
        .x = X2ValueSet{},
        .y = clone_optional_value_set(yp),
        .x2 = X2ValueSet{},
        .xShape = X2ShapeSet{},
        .yShape = clone_optional_shape_set(ysp),
        .x2Shape = X2ShapeSet{},
        .xDirectShape = X2ShapeSet{},
        .yDirectShape = clone_optional_shape_set(ydp),
        .entry = unknown_x2_entry_state(),
        .vpContext = unknown_x2_vp_context_state(),
        .structuralEntry = unknown_x2_structural_entry_state(),
        .structuralVpContext = unknown_x2_structural_entry_state(),
        .memory = clone_x2_value_memory_field(input.memory),
        .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
    };
  }
  const std::optional<X2ShapeSet> restored_x2_shape = effective_input_x2_shape(&input.x2Shape, &input.x2);
  const X2ShapeSet* restored_ptr = restored_x2_shape.has_value() ? &*restored_x2_shape : nullptr;
  const std::set<X2ShapeFact> x_shape_set = normalize_x2_restore_shapes_for_x(restored_ptr);
  const X2ShapeSet x_shape{x_shape_set.begin(), x_shape_set.end()};
  const std::set<X2ValueFact> x_facts = normalize_x2_restore_facts_for_x(input.x2);
  const std::set<X2ShapeFact> x_direct = dot_restore_direct_shape_facts(&x_shape);
  return X2ValueDataflowState{
      .x = X2ValueSet{x_facts.begin(), x_facts.end()},
      .y = clone_optional_value_set(yp),
      .x2 = clone_optional_value_set(&input.x2),
      .xShape = x_shape,
      .yShape = clone_optional_shape_set(ysp),
      .x2Shape = clone_optional_shape_set(&input.x2Shape),
      .xDirectShape = X2ShapeSet{x_direct.begin(), x_direct.end()},
      .yDirectShape = clone_optional_shape_set(ydp),
      .entry = closed_x2_entry_state(),
      .vpContext = none_x2_vp_context_state(),
      .structuralEntry = none_x2_structural_entry_state(),
      .structuralVpContext = none_x2_structural_entry_state(),
      .vpEntryMantissa = vp_entry_mantissas_from_x2_restore(input.x2, restored_ptr),
      .vpEntrySignMantissa = vp_entry_sign_mantissas_from_x2_restore(input.x2, restored_ptr),
      .vpEntryShape = vp_entry_shapes_from_shape_facts(restored_ptr),
      .vpEntrySignShape = vp_entry_sign_shapes_from_shape_facts(restored_ptr),
      .memory = clone_x2_value_memory_field(input.memory),
      .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
  };
}

X2ValueDataflowState transfer_sign_change_x2_value_state(const X2ValueDataflowState& input,
                                                         std::optional<int> producer_index) {
  const X2ValueSet* yp = input.y.has_value() ? &*input.y : nullptr;
  const X2ShapeSet* ysp = input.yShape.has_value() ? &*input.yShape : nullptr;
  const X2ShapeSet* ydp = input.yDirectShape.has_value() ? &*input.yDirectShape : nullptr;
  if (input.structuralEntry.kind == X2StructuralEntryState::Kind::Exponent) {
    const X2StructuralEntryState entry = sign_change_structural_exponent_entry(input.structuralEntry);
    return x2_value_state_from_structural_exponent_entry(entry, input.memory, input.shapeMemory, yp, ysp, ydp);
  }
  if (input.entry.kind == X2EntryState::Kind::Exponent) {
    const X2EntryState entry = sign_change_exponent_entry(input.entry);
    return x2_value_state_from_exponent_entry(entry, input.memory, input.shapeMemory, yp, ysp, ydp);
  }
  if (input.entry.kind == X2EntryState::Kind::Closed &&
      input.structuralVpContext.kind == X2StructuralEntryState::Kind::Exponent) {
    const X2StructuralEntryState context = sign_change_structural_exponent_entry(input.structuralVpContext);
    return x2_value_state_from_signed_structural_vp_context(input, context);
  }
  if (input.entry.kind == X2EntryState::Kind::Closed &&
      input.vpContext.kind == X2VpContextState::Kind::Exponent) {
    const X2VpContextState context = sign_change_vp_context(input.vpContext);
    return x2_value_state_from_signed_decimal_vp_context(input, context);
  }
  if (input.entry.kind == X2EntryState::Kind::Closed &&
      (input.vpContext.kind == X2VpContextState::Kind::None)) {
    const std::optional<X2ValueDataflowState> state = sign_change_closed_decimal_state(input, producer_index);
    if (state.has_value())
      return *state;
  }
  if (input.entry.kind != X2EntryState::Kind::Open) {
    return X2ValueDataflowState{
        .x = X2ValueSet{},
        .y = clone_optional_value_set(yp),
        .x2 = X2ValueSet{},
        .xShape = X2ShapeSet{},
        .yShape = clone_optional_shape_set(ysp),
        .x2Shape = X2ShapeSet{},
        .xDirectShape = X2ShapeSet{},
        .yDirectShape = clone_optional_shape_set(ydp),
        .entry = unknown_x2_entry_state(),
        .vpContext = unknown_x2_vp_context_state(),
        .structuralEntry = unknown_x2_structural_entry_state(),
        .structuralVpContext = unknown_x2_structural_entry_state(),
        .memory = clone_x2_value_memory_field(input.memory),
        .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
    };
  }
  X2ValueSet x;
  X2ValueSet x2;
  X2ShapeSet x_shape;
  X2ShapeSet x2_shape;
  for (const std::string& raw : input.entry.raw) {
    const std::string signed_raw = sign_changed_decimal_entry(raw);
    const std::optional<std::string> normalized = normalize_decimal_mantissa_entry(signed_raw);
    if (normalized.has_value()) {
      x.insert(decimal_value_fact(*normalized, "normalized"));
      x_shape.insert(decimal_mantissa_shape_fact(*normalized));
    }
    const std::optional<X2ValueFact> x2_fact = x2_decimal_entry_fact(signed_raw);
    if (x2_fact.has_value())
      x2.insert(*x2_fact);
    x2_shape.insert(decimal_mantissa_shape_fact(signed_raw));
  }
  return X2ValueDataflowState{
      .x = x,
      .y = clone_optional_value_set(yp),
      .x2 = x2,
      .xShape = x_shape,
      .yShape = clone_optional_shape_set(ysp),
      .x2Shape = x2_shape,
      .xDirectShape = X2ShapeSet{},
      .yDirectShape = clone_optional_shape_set(ydp),
      .entry = closed_x2_entry_state(),
      .vpContext = none_x2_vp_context_state(),
      .structuralEntry = none_x2_structural_entry_state(),
      .structuralVpContext = none_x2_structural_entry_state(),
      .vpEntryMantissa = sign_changed_mantissa_shapes(input.entry.raw),
      .memory = clone_x2_value_memory_field(input.memory),
      .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
  };
}

X2ValueDataflowState transfer_vp_x2_value_state(const X2ValueDataflowState& input) {
  const X2ValueSet* yp = input.y.has_value() ? &*input.y : nullptr;
  const X2ShapeSet* ysp = input.yShape.has_value() ? &*input.yShape : nullptr;
  const X2ShapeSet* ydp = input.yDirectShape.has_value() ? &*input.yDirectShape : nullptr;
  if (input.entry.kind == X2EntryState::Kind::Open) {
    const RegisterValueSet mantissa = input.entry.raw;
    X2EntryState exp_entry{.kind = X2EntryState::Kind::Exponent,
                           .mantissa = mantissa,
                           .exponent = RegisterValueSet{""}};
    const std::set<X2ShapeFact> shape_facts = exponent_entry_shape_facts(exp_entry);
    return X2ValueDataflowState{
        .x = X2ValueSet{},
        .y = clone_optional_value_set(yp),
        .x2 = X2ValueSet{},
        .xShape = X2ShapeSet{},
        .yShape = clone_optional_shape_set(ysp),
        .x2Shape = X2ShapeSet{shape_facts.begin(), shape_facts.end()},
        .xDirectShape = X2ShapeSet{},
        .yDirectShape = clone_optional_shape_set(ydp),
        .entry = exp_entry,
        .vpContext = X2VpContextState{.kind = X2VpContextState::Kind::Exponent,
                                      .mantissa = mantissa,
                                      .exponent = RegisterValueSet{""}},
        .structuralEntry = none_x2_structural_entry_state(),
        .structuralVpContext = none_x2_structural_entry_state(),
        .memory = clone_x2_value_memory_field(input.memory),
        .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
    };
  }
  if (input.entry.kind == X2EntryState::Kind::Exponent)
    return x2_value_state_from_exponent_entry(input.entry, input.memory, input.shapeMemory, yp, ysp, ydp);
  if (input.structuralEntry.kind == X2StructuralEntryState::Kind::Exponent)
    return x2_value_state_from_structural_exponent_entry(input.structuralEntry, input.memory,
                                                         input.shapeMemory, yp, ysp, ydp);
  if (input.entry.kind == X2EntryState::Kind::Closed &&
      input.vpContext.kind == X2VpContextState::Kind::Exponent && input.vpEntryMantissa.has_value()) {
    const X2EntryState entry =
        x2_entry_state_from_exponent_parts(&*input.vpEntryMantissa, &input.vpContext.exponent);
    if (entry.kind == X2EntryState::Kind::Exponent)
      return x2_value_state_from_exponent_entry(entry, input.memory, input.shapeMemory, yp, ysp, ydp);
  }
  if (input.entry.kind == X2EntryState::Kind::Closed && input.vpEntryMantissa.has_value()) {
    const RegisterValueSet& mantissa = *input.vpEntryMantissa;
    X2EntryState exp_entry{.kind = X2EntryState::Kind::Exponent,
                           .mantissa = mantissa,
                           .exponent = RegisterValueSet{""}};
    const std::set<X2ShapeFact> shape_facts = exponent_entry_shape_facts(exp_entry);
    return X2ValueDataflowState{
        .x = X2ValueSet{},
        .y = clone_optional_value_set(yp),
        .x2 = X2ValueSet{},
        .xShape = X2ShapeSet{},
        .yShape = clone_optional_shape_set(ysp),
        .x2Shape = X2ShapeSet{shape_facts.begin(), shape_facts.end()},
        .xDirectShape = X2ShapeSet{},
        .yDirectShape = clone_optional_shape_set(ydp),
        .entry = exp_entry,
        .vpContext = X2VpContextState{.kind = X2VpContextState::Kind::Exponent,
                                      .mantissa = mantissa,
                                      .exponent = RegisterValueSet{""}},
        .structuralEntry = none_x2_structural_entry_state(),
        .structuralVpContext = none_x2_structural_entry_state(),
        .memory = clone_x2_value_memory_field(input.memory),
        .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
    };
  }
  if (input.entry.kind == X2EntryState::Kind::Closed &&
      input.structuralVpContext.kind == X2StructuralEntryState::Kind::Exponent &&
      input.vpEntryShape.has_value() && !input.vpEntryShape->empty()) {
    const X2StructuralEntryState structural_entry =
        x2_structural_entry_state_from_parts(&*input.vpEntryShape, &input.structuralVpContext.exponent);
    return x2_value_state_from_structural_exponent_entry(structural_entry, input.memory,
                                                         input.shapeMemory, yp, ysp, ydp);
  }
  if (input.entry.kind == X2EntryState::Kind::Closed && input.vpEntryShape.has_value() &&
      !input.vpEntryShape->empty()) {
    const X2StructuralEntryState structural_entry =
        structural_exponent_entry_from_vp_entry_shapes(*input.vpEntryShape);
    return x2_value_state_from_structural_exponent_entry(structural_entry, input.memory,
                                                         input.shapeMemory, yp, ysp, ydp);
  }
  return X2ValueDataflowState{
      .x = X2ValueSet{},
      .y = clone_optional_value_set(yp),
      .x2 = X2ValueSet{},
      .xShape = X2ShapeSet{},
      .yShape = clone_optional_shape_set(ysp),
      .x2Shape = X2ShapeSet{},
      .xDirectShape = X2ShapeSet{},
      .yDirectShape = clone_optional_shape_set(ydp),
      .entry = unknown_x2_entry_state(),
      .vpContext = unknown_x2_vp_context_state(),
      .structuralEntry = unknown_x2_structural_entry_state(),
      .structuralVpContext = unknown_x2_structural_entry_state(),
      .memory = clone_x2_value_memory_field(input.memory),
      .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
  };
}

X2ValueDataflowState transfer_exchange_xy_x2_value_state(const X2ValueDataflowState& input,
                                                         const IrOp& op) {
  const X2Effect effect = plain_x2_effect(op);
  const X2ValueSet source_x = visible_x2_value_facts_for_stack(input);
  const X2ShapeSet source_x_shape = visible_x2_shape_facts_for_stack(input);
  const X2ShapeSet source_x_direct_shape = visible_x2_direct_shape_facts_for_stack(input);
  const X2ValueSet x = clone_optional_value_set(input.y.has_value() ? &*input.y : nullptr);
  const X2ShapeSet x_shape = clone_optional_shape_set(input.yShape.has_value() ? &*input.yShape : nullptr);
  const X2ValueSet x2 = transfer_plain_x2_value_set(input, x, &x_shape, effect);
  const X2ShapeSet x2_shape = transfer_plain_x2_shape_set(input, &x, x_shape, effect, nullptr);
  const X2ShapeSet* x2sp = &input.x2Shape;
  const X2ValueSet* x2vp = &input.x2;
  return X2ValueDataflowState{
      .x = x,
      .y = clone_optional_value_set(&source_x),
      .x2 = x2,
      .xShape = x_shape,
      .yShape = X2ShapeSet{source_x_shape.begin(), source_x_shape.end()},
      .x2Shape = x2_shape,
      .xDirectShape = clone_optional_shape_set(input.yDirectShape.has_value() ? &*input.yDirectShape : nullptr),
      .yDirectShape = X2ShapeSet{source_x_direct_shape.begin(), source_x_direct_shape.end()},
      .entry = next_x2_entry_state_for_plain_effect(effect),
      .vpContext = transfer_plain_x2_vp_context_state(input, effect),
      .structuralEntry = none_x2_structural_entry_state(),
      .structuralVpContext = transfer_plain_x2_structural_vp_context_state(input, effect),
      .vpEntryMantissa = transfer_plain_x2_vp_entry_mantissa_state(
          input, op, x, x2, x_shape, x2_shape, effect, &source_x_shape, x2sp, false, &source_x, x2vp),
      .vpEntryMantissaTransient = transfer_plain_x2_vp_entry_mantissa_transient_state(
          op, effect, &source_x_shape, x2sp, false, &source_x, x2vp),
      .vpEntrySignMantissa = transfer_plain_x2_vp_entry_sign_mantissa_state(input, op, effect),
      .vpEntryShape = transfer_plain_x2_vp_entry_shape_state(
          input, op, x, x2, x_shape, x2_shape, effect, &source_x_shape, x2sp, false, &source_x, x2vp),
      .vpEntrySignShape = transfer_plain_x2_vp_entry_sign_shape_state(input, op, x, x2, x_shape, x2_shape, effect),
      .vpEntryShapeTransient = transfer_plain_x2_vp_entry_shape_transient_state(
          op, effect, &source_x_shape, x2sp, false, &source_x, x2vp),
      .memory = clone_x2_value_memory_field(input.memory),
      .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
  };
}

X2ValueDataflowState transfer_copy_y_to_x_x2_value_state(const X2ValueDataflowState& input,
                                                         const IrOp& op) {
  const X2ValueDataflowState closed = close_x2_value_entry(input);
  const X2Effect effect = plain_x2_effect(op);
  const X2ValueSet source_x = visible_x2_value_facts_for_stack(closed);
  const X2ShapeSet source_x_shape = visible_x2_shape_facts_for_stack(closed);
  const X2ValueSet x = clone_optional_value_set(closed.y.has_value() ? &*closed.y : nullptr);
  const X2ShapeSet x_shape = clone_optional_shape_set(closed.yShape.has_value() ? &*closed.yShape : nullptr);
  const X2ValueSet x2 = transfer_plain_x2_value_set(closed, x, &x_shape, effect);
  const X2ShapeSet x2_shape = transfer_plain_x2_shape_set(closed, &x, x_shape, effect, nullptr);
  const X2ShapeSet y_direct_shape =
      clone_optional_shape_set(closed.yDirectShape.has_value() ? &*closed.yDirectShape : nullptr);
  const X2ShapeSet* x2sp = &closed.x2Shape;
  const X2ValueSet* x2vp = &closed.x2;
  return X2ValueDataflowState{
      .x = x,
      .y = clone_optional_value_set(closed.y.has_value() ? &*closed.y : nullptr),
      .x2 = x2,
      .xShape = x_shape,
      .yShape = clone_optional_shape_set(closed.yShape.has_value() ? &*closed.yShape : nullptr),
      .x2Shape = x2_shape,
      .xDirectShape = X2ShapeSet{y_direct_shape.begin(), y_direct_shape.end()},
      .yDirectShape = y_direct_shape,
      .entry = next_x2_entry_state_for_plain_effect(effect),
      .vpContext = transfer_plain_x2_vp_context_state(closed, effect),
      .structuralEntry = none_x2_structural_entry_state(),
      .structuralVpContext = transfer_plain_x2_structural_vp_context_state(closed, effect),
      .vpEntryMantissa = transfer_plain_x2_vp_entry_mantissa_state(
          closed, op, x, x2, x_shape, x2_shape, effect, &source_x_shape, x2sp, false, &source_x, x2vp),
      .vpEntryMantissaTransient = transfer_plain_x2_vp_entry_mantissa_transient_state(
          op, effect, &source_x_shape, x2sp, false, &source_x, x2vp),
      .vpEntrySignMantissa = transfer_plain_x2_vp_entry_sign_mantissa_state(closed, op, effect),
      .vpEntryShape = transfer_plain_x2_vp_entry_shape_state(
          closed, op, x, x2, x_shape, x2_shape, effect, &source_x_shape, x2sp, false, &source_x, x2vp),
      .vpEntrySignShape = transfer_plain_x2_vp_entry_sign_shape_state(closed, op, x, x2, x_shape, x2_shape, effect),
      .vpEntryShapeTransient = transfer_plain_x2_vp_entry_shape_transient_state(
          op, effect, &source_x_shape, x2sp, false, &source_x, x2vp),
      .memory = clone_x2_value_memory_field(closed.memory),
      .shapeMemory = clone_x2_shape_memory_field(closed.shapeMemory),
  };
}

X2ValueDataflowState transfer_plain_x2_value_state(const X2ValueDataflowState& input, const IrOp& op,
                                                   std::optional<int> producer_index) {
  const X2ValueSet* yp = input.y.has_value() ? &*input.y : nullptr;
  const X2ShapeSet* ysp = input.yShape.has_value() ? &*input.yShape : nullptr;
  const X2ShapeSet* ydp = input.yDirectShape.has_value() ? &*input.yDirectShape : nullptr;
  if (op.opcode >= 0 && op.opcode <= 9)
    return transfer_decimal_digit_x2_value_state(input, std::to_string(op.opcode));
  if (op.opcode == 0x0a)
    return transfer_dot_restore_x2_value_state(input);
  if (op.opcode == 0x0b)
    return transfer_sign_change_x2_value_state(input, producer_index);
  if (op.opcode == 0x0c)
    return transfer_vp_x2_value_state(input);
  if (op.opcode == 0x0d) {
    return X2ValueDataflowState{
        .x = X2ValueSet{decimal_value_fact("0", "normalized")},
        .y = clone_optional_value_set(yp),
        .x2 = X2ValueSet{decimal_value_fact("0", "normalized")},
        .xShape = X2ShapeSet{decimal_mantissa_shape_fact("0")},
        .yShape = clone_optional_shape_set(ysp),
        .x2Shape = X2ShapeSet{decimal_mantissa_shape_fact("0")},
        .xDirectShape = X2ShapeSet{},
        .yDirectShape = clone_optional_shape_set(ydp),
        .entry = closed_x2_entry_state(),
        .vpContext = none_x2_vp_context_state(),
        .structuralEntry = none_x2_structural_entry_state(),
        .structuralVpContext = none_x2_structural_entry_state(),
        .vpEntryMantissa = RegisterValueSet{"0"},
        .memory = clone_x2_value_memory_field(input.memory),
        .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
    };
  }
  if (op.opcode == 0x14)
    return transfer_exchange_xy_x2_value_state(input, op);
  if (op.opcode == 0x3e)
    return transfer_copy_y_to_x_x2_value_state(input, op);

  const X2Effect effect = plain_x2_effect(op);
  const X2ShapeSet* direct_y_shape = ydp;
  const std::set<X2ValueFact> closed_exponent_values = closed_exponent_entry_decimal_facts(input.entry);
  if (!closed_exponent_values.empty()) {
    const X2ValueSet source_x{closed_exponent_values.begin(), closed_exponent_values.end()};
    const std::set<X2ShapeFact> closed_set = closed_exponent_entry_shape_facts(input.entry);
    const X2ShapeSet closed_exponent_shapes{closed_set.begin(), closed_set.end()};
    X2ValueSet x;
    if (plain_preserves_x_value(op))
      x = source_x;
    else
      x = plain_x_value_after_non_preserving_op(op, producer_index, &source_x, yp,
                                                &closed_exponent_shapes, ysp, direct_y_shape, nullptr);
    X2ShapeSet x_shape;
    if (plain_preserves_x_value(op))
      x_shape = closed_exponent_shapes;
    else
      x_shape = plain_x_shape_after_non_preserving_op(op, &source_x, yp, &closed_exponent_shapes, ysp,
                                                      ConcreteEvaluationOptions{}, direct_y_shape, nullptr);
    X2ValueSet x2;
    if (effect == X2Effect::Preserves)
      x2 = X2ValueSet{closed_exponent_values.begin(), closed_exponent_values.end()};
    else if (effect == X2Effect::Affects)
      x2 = x;
    const X2ShapeSet x2_shape = transfer_plain_x2_shape_set(input, &x, x_shape, effect, &closed_exponent_shapes);
    const X2ShapeSet x_direct_shape = plain_direct_shape_after_plain_op(op, &closed_exponent_shapes, &x_shape);
    return X2ValueDataflowState{
        .x = x,
        .y = transfer_plain_y_value_set(input, source_x, op),
        .x2 = x2,
        .xShape = x_shape,
        .yShape = transfer_plain_y_shape_set(input, &closed_exponent_shapes, op),
        .x2Shape = x2_shape,
        .xDirectShape = x_direct_shape,
        .yDirectShape = transfer_plain_y_direct_shape_set(input, &closed_exponent_shapes, op),
        .entry = next_x2_entry_state_for_plain_effect(effect),
        .vpContext = transfer_plain_x2_vp_context_state(input, effect),
        .structuralEntry = none_x2_structural_entry_state(),
        .structuralVpContext = transfer_plain_x2_structural_vp_context_state(input, effect),
        .vpEntryMantissa = transfer_plain_x2_vp_entry_mantissa_state(
            input, op, x, x2, x_shape, x2_shape, effect, &closed_exponent_shapes, &closed_exponent_shapes,
            false, &source_x, &source_x),
        .vpEntryMantissaTransient = transfer_plain_x2_vp_entry_mantissa_transient_state(
            op, effect, &closed_exponent_shapes, &closed_exponent_shapes, false, &source_x, &source_x),
        .vpEntrySignMantissa = transfer_plain_x2_vp_entry_sign_mantissa_state(input, op, effect),
        .vpEntryShape = transfer_plain_x2_vp_entry_shape_state(
            input, op, x, x2, x_shape, x2_shape, effect, &closed_exponent_shapes, &closed_exponent_shapes,
            false, &source_x, &source_x),
        .vpEntrySignShape = transfer_plain_x2_vp_entry_sign_shape_state(input, op, x, x2, x_shape, x2_shape, effect),
        .vpEntryShapeTransient = transfer_plain_x2_vp_entry_shape_transient_state(
            op, effect, &closed_exponent_shapes, &closed_exponent_shapes, false, &source_x, &source_x),
        .memory = clone_x2_value_memory_field(input.memory),
        .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
    };
  }
  const std::set<X2ShapeFact> closed_shape_set = closed_exponent_entry_shape_facts(input.entry);
  if (!closed_shape_set.empty()) {
    const X2ShapeSet closed_exponent_shapes{closed_shape_set.begin(), closed_shape_set.end()};
    const X2ValueSet source_x;
    X2ValueSet x;
    if (!plain_preserves_x_value(op))
      x = plain_x_value_after_non_preserving_op(op, producer_index, &source_x, yp,
                                                &closed_exponent_shapes, ysp, direct_y_shape, nullptr);
    X2ShapeSet x_shape;
    if (plain_preserves_x_value(op))
      x_shape = closed_exponent_shapes;
    else
      x_shape = plain_x_shape_after_non_preserving_op(op, &source_x, yp, &closed_exponent_shapes, ysp,
                                                      ConcreteEvaluationOptions{}, direct_y_shape, nullptr);
    const X2ShapeSet x2_shape = transfer_plain_x2_shape_set(input, &x, x_shape, effect, &closed_exponent_shapes);
    const X2ValueSet x2 = transfer_plain_x2_value_set(input, x, &x_shape, effect);
    const X2ShapeSet x_direct_shape = plain_direct_shape_after_plain_op(op, &closed_exponent_shapes, &x_shape);
    return X2ValueDataflowState{
        .x = x,
        .y = transfer_plain_y_value_set(input, source_x, op),
        .x2 = x2,
        .xShape = x_shape,
        .yShape = transfer_plain_y_shape_set(input, &closed_exponent_shapes, op),
        .x2Shape = x2_shape,
        .xDirectShape = x_direct_shape,
        .yDirectShape = transfer_plain_y_direct_shape_set(input, &closed_exponent_shapes, op),
        .entry = next_x2_entry_state_for_plain_effect(effect),
        .vpContext = transfer_plain_x2_vp_context_state(input, effect),
        .structuralEntry = none_x2_structural_entry_state(),
        .structuralVpContext = transfer_plain_x2_structural_vp_context_state(input, effect),
        .vpEntryMantissa = transfer_plain_x2_vp_entry_mantissa_state(
            input, op, x, x2, x_shape, x2_shape, effect, &closed_exponent_shapes, &closed_exponent_shapes,
            false, &source_x, nullptr),
        .vpEntryMantissaTransient = transfer_plain_x2_vp_entry_mantissa_transient_state(
            op, effect, &closed_exponent_shapes, &closed_exponent_shapes, false, &source_x, nullptr),
        .vpEntrySignMantissa = transfer_plain_x2_vp_entry_sign_mantissa_state(input, op, effect),
        .vpEntryShape = transfer_plain_x2_vp_entry_shape_state(
            input, op, x, x2, x_shape, x2_shape, effect, &closed_exponent_shapes, &closed_exponent_shapes,
            false, &source_x, nullptr),
        .vpEntrySignShape = transfer_plain_x2_vp_entry_sign_shape_state(input, op, x, x2, x_shape, x2_shape, effect),
        .vpEntryShapeTransient = transfer_plain_x2_vp_entry_shape_transient_state(
            op, effect, &closed_exponent_shapes, &closed_exponent_shapes, false, &source_x, nullptr),
        .memory = clone_x2_value_memory_field(input.memory),
        .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
    };
  }
  const X2ShapeSet source_x_direct_shape =
      clone_optional_shape_set(input.xDirectShape.has_value() ? &*input.xDirectShape : nullptr);
  X2ValueSet x_pre;
  if (plain_preserves_x_value(op))
    x_pre = input.x;
  else
    x_pre = plain_x_value_after_non_preserving_op(op, producer_index, &input.x, yp, &input.xShape, ysp,
                                                  direct_y_shape, &source_x_direct_shape);
  const X2ValueSet x = sync_unknown_same_value(x_pre, effect, producer_index);
  const std::optional<X2ShapeSet> input_x_shape = effective_input_x_shape(&input.xShape, &input.x);
  X2ShapeSet x_shape;
  if (plain_preserves_x_value(op))
    x_shape = clone_optional_shape_set(input_x_shape.has_value() ? &*input_x_shape : nullptr);
  else
    x_shape = plain_x_shape_after_non_preserving_op(op, &input.x, yp, &input.xShape, ysp,
                                                    ConcreteEvaluationOptions{}, direct_y_shape,
                                                    &source_x_direct_shape);
  const X2ValueSet x2 = transfer_plain_x2_value_set(input, x, &x_shape, effect);
  const X2ShapeSet x2_shape = transfer_plain_x2_shape_set(input, &x, x_shape, effect, nullptr);
  const X2ShapeSet x_direct_shape = plain_direct_shape_after_plain_op(op, &source_x_direct_shape, &x_shape);
  if (input.structuralEntry.kind == X2StructuralEntryState::Kind::Exponent) {
    const std::set<X2ShapeFact> closed_structural_set = structural_exponent_entry_shape_facts(input.structuralEntry);
    const X2ShapeSet closed_structural_shapes{closed_structural_set.begin(), closed_structural_set.end()};
    const X2ValueSet source_x;
    X2ValueSet structural_x_value;
    if (!plain_preserves_x_value(op))
      structural_x_value = plain_x_value_after_non_preserving_op(
          op, producer_index, &source_x, yp, &closed_structural_shapes, ysp, direct_y_shape, nullptr);
    X2ShapeSet structural_x_shape;
    if (plain_preserves_x_value(op))
      structural_x_shape = closed_structural_shapes;
    else
      structural_x_shape = plain_x_shape_after_non_preserving_op(
          op, &source_x, yp, &closed_structural_shapes, ysp, ConcreteEvaluationOptions{}, direct_y_shape,
          nullptr);
    const X2ShapeSet structural_x2_shape =
        transfer_plain_x2_shape_set(input, &structural_x_value, structural_x_shape, effect, &closed_structural_shapes);
    const X2ValueSet structural_x2_value = transfer_plain_x2_value_set(input, structural_x_value, &structural_x_shape, effect);
    const X2ShapeSet structural_x_direct_shape =
        plain_direct_shape_after_plain_op(op, &closed_structural_shapes, &structural_x_shape);
    const X2ValueSet empty_x2;
    return X2ValueDataflowState{
        .x = structural_x_value,
        .y = transfer_plain_y_value_set(input, source_x, op),
        .x2 = structural_x2_value,
        .xShape = structural_x_shape,
        .yShape = transfer_plain_y_shape_set(input, &closed_structural_shapes, op),
        .x2Shape = structural_x2_shape,
        .xDirectShape = structural_x_direct_shape,
        .yDirectShape = transfer_plain_y_direct_shape_set(input, &closed_structural_shapes, op),
        .entry = next_x2_entry_state_for_plain_effect(effect),
        .vpContext = transfer_plain_x2_vp_context_state(input, effect),
        .structuralEntry = none_x2_structural_entry_state(),
        .structuralVpContext = transfer_plain_x2_structural_vp_context_state(input, effect),
        .vpEntryMantissa = transfer_plain_x2_vp_entry_mantissa_state(
            input, op, structural_x_value, empty_x2, structural_x_shape, structural_x2_shape, effect,
            &closed_structural_shapes, &closed_structural_shapes, false, &source_x, nullptr),
        .vpEntryMantissaTransient = transfer_plain_x2_vp_entry_mantissa_transient_state(
            op, effect, &closed_structural_shapes, &closed_structural_shapes, false, &source_x, nullptr),
        .vpEntrySignMantissa = transfer_plain_x2_vp_entry_sign_mantissa_state(input, op, effect),
        .vpEntryShape = transfer_plain_x2_vp_entry_shape_state(
            input, op, structural_x_value, structural_x2_value, structural_x_shape, structural_x2_shape,
            effect, &closed_structural_shapes, &closed_structural_shapes, false, &source_x, nullptr),
        .vpEntrySignShape = transfer_plain_x2_vp_entry_sign_shape_state(
            input, op, structural_x_value, empty_x2, structural_x_shape, structural_x2_shape, effect),
        .vpEntryShapeTransient = transfer_plain_x2_vp_entry_shape_transient_state(
            op, effect, &closed_structural_shapes, &closed_structural_shapes, false, &source_x, nullptr),
        .memory = clone_x2_value_memory_field(input.memory),
        .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
    };
  }
  const bool vp_exponent = input.vpContext.kind == X2VpContextState::Kind::Exponent;
  const bool include_exponent_targets_shape =
      vp_exponent || input.structuralVpContext.kind == X2StructuralEntryState::Kind::Exponent;
  const X2ShapeSet* input_x_shape_ptr = input_x_shape.has_value() ? &*input_x_shape : nullptr;
  return X2ValueDataflowState{
      .x = x,
      .y = transfer_plain_y_value_set(input, input.x, op),
      .x2 = x2,
      .xShape = x_shape,
      .yShape = transfer_plain_y_shape_set(input, input_x_shape_ptr, op),
      .x2Shape = x2_shape,
      .xDirectShape = x_direct_shape,
      .yDirectShape = transfer_plain_y_direct_shape_set(input, &source_x_direct_shape, op),
      .entry = next_x2_entry_state_for_plain_effect(effect),
      .vpContext = transfer_plain_x2_vp_context_state(input, effect),
      .structuralEntry = none_x2_structural_entry_state(),
      .structuralVpContext = transfer_plain_x2_structural_vp_context_state(input, effect),
      .vpEntryMantissa = transfer_plain_x2_vp_entry_mantissa_state(
          input, op, x, x2, x_shape, x2_shape, effect, input_x_shape_ptr, &input.x2Shape, vp_exponent,
          &input.x, &input.x2),
      .vpEntryMantissaTransient = transfer_plain_x2_vp_entry_mantissa_transient_state(
          op, effect, input_x_shape_ptr, &input.x2Shape, vp_exponent, &input.x, &input.x2),
      .vpEntrySignMantissa = transfer_plain_x2_vp_entry_sign_mantissa_state(input, op, effect),
      .vpEntryShape = transfer_plain_x2_vp_entry_shape_state(
          input, op, x, x2, x_shape, x2_shape, effect, input_x_shape_ptr, &input.x2Shape,
          include_exponent_targets_shape, &input.x, &input.x2),
      .vpEntrySignShape = transfer_plain_x2_vp_entry_sign_shape_state(input, op, x, x2, x_shape, x2_shape, effect),
      .vpEntryShapeTransient = transfer_plain_x2_vp_entry_shape_transient_state(
          op, effect, input_x_shape_ptr, &input.x2Shape, include_exponent_targets_shape, &input.x, &input.x2),
      .memory = clone_x2_value_memory_field(input.memory),
      .shapeMemory = clone_x2_shape_memory_field(input.shapeMemory),
  };
}

// --- Session C.4: faithful merge (join/same) family -------------------------
// Faithful port of joinX2ValueDataflowStates / sameX2ValueDataflowState and the
// join*/same* sub-state machinery (TS ~10898, ~10991, ~11030, ~14634). Additive
// in the x2eval namespace; the public merge wrappers delegate here.

X2ValueSet tx_value_set_with_stable_expression_decimal_facts(const X2ValueSet* input) {
  X2ValueSet output = canonical_x2_value_set(input);
  const std::vector<X2ValueFact> facts(output.begin(), output.end());
  for (const X2ValueFact& fact : facts) {
    if (fact.rfind("expr-key:", 0) != 0)
      continue;
    const std::optional<std::string> visible = x2_value_fact_restored_visible_decimal(fact);
    if (visible.has_value())
      output.insert(decimal_value_fact(*visible, "normalized"));
  }
  return output;
}

bool tx_same_string_set(const std::set<std::string>& left, const std::set<std::string>& right) {
  return left == right;
}

std::set<std::string> tx_join_string_sets(const std::set<std::string>& current,
                                          const std::set<std::string>& incoming) {
  std::set<std::string> joined;
  for (const std::string& value : current)
    if (incoming.count(value) != 0U)
      joined.insert(value);
  return joined;
}

std::optional<std::set<std::string>> tx_join_optional_string_sets(
    const std::set<std::string>* current, const std::set<std::string>* incoming) {
  if (current == nullptr || incoming == nullptr)
    return std::nullopt;
  std::set<std::string> joined = tx_join_string_sets(*current, *incoming);
  if (joined.empty())
    return std::nullopt;
  return joined;
}

bool tx_same_optional_string_set(const std::set<std::string>* left,
                                 const std::set<std::string>* right) {
  if (left == nullptr || right == nullptr)
    return left == nullptr && right == nullptr;
  return *left == *right;
}

bool tx_same_optional_shape_set(const X2ShapeSet* left, const X2ShapeSet* right) {
  return canonical_shape_set(left) == canonical_shape_set(right);
}

bool tx_same_x2_value_set(const X2ValueSet* left, const X2ValueSet* right) {
  if (left == nullptr || right == nullptr)
    return left == nullptr && right == nullptr;
  return canonical_x2_value_set(left) == canonical_x2_value_set(right);
}

X2ShapeSet tx_common_structural_restore_shape_facts(const X2ShapeSet& left, const X2ShapeSet& right) {
  const std::set<X2ShapeFact> left_restore = structural_restore_shape_facts(left);
  const std::set<X2ShapeFact> right_restore = structural_restore_shape_facts(right);
  X2ShapeSet output;
  for (const X2ShapeFact& fact : left_restore) {
    if (right_restore.count(fact) == 0U)
      continue;
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind == X2ShapeModelKind::Mantissa && model.mantissa.has_value() &&
        (model.mantissa->radix == X2MantissaRadix::Hex ||
         model.mantissa->radix == X2MantissaRadix::Super))
      output.insert(fact);
  }
  return output;
}

X2ShapeSet tx_common_decimal_display_shape_facts(const X2ShapeSet* left, const X2ShapeSet* right) {
  const std::set<X2ShapeFact> left_displays = decimal_display_shape_facts(left);
  X2ShapeSet output;
  for (const X2ShapeFact& shape : decimal_display_shape_facts(right))
    if (left_displays.count(shape) != 0U)
      output.insert(shape);
  return output;
}

X2ShapeSet tx_intersect_x2_shape_sets(const X2ShapeSet* current, const X2ShapeSet* incoming) {
  if (current == nullptr || incoming == nullptr)
    return {};
  const std::set<X2ShapeFact> current_set = canonical_shape_set(current);
  const std::set<X2ShapeFact> incoming_set = canonical_shape_set(incoming);
  X2ShapeSet joined;
  for (const X2ShapeFact& shape : current_set)
    if (incoming_set.count(shape) != 0U)
      joined.insert(shape);
  if (current_set == incoming_set)
    return joined;
  for (const X2ShapeFact& shape : tx_common_structural_restore_shape_facts(current_set, incoming_set))
    joined.insert(shape);
  return joined;
}

X2ValueSet tx_join_x2_value_sets(const X2ValueSet* current, const X2ValueSet& incoming) {
  if (current == nullptr)
    return canonical_x2_value_set(&incoming);
  const X2ValueSet current_set = tx_value_set_with_stable_expression_decimal_facts(current);
  const X2ValueSet incoming_set = tx_value_set_with_stable_expression_decimal_facts(&incoming);
  X2ValueSet joined;
  for (const X2ValueFact& value : current_set)
    if (incoming_set.count(value) != 0U)
      joined.insert(value);
  return joined;
}

std::optional<X2ShapeSet> tx_shape_set_with_stable_expression_value_shapes_for_x2_sync(
    const X2ShapeSet* shapes, const X2ValueSet* values) {
  X2ShapeSet output = clone_optional_shape_set(shapes);
  X2ShapeSet stable_shapes;
  if (values != nullptr) {
    for (const X2ValueFact& value : *values) {
      if (value.rfind("expr-key:", 0) != 0)
        continue;
      for (const X2ShapeFact& shape : stable_expression_key_shape_set(value))
        stable_shapes.insert(shape);
    }
  }
  for (const X2ShapeFact& shape : normalize_x2_sync_shapes_from_x(&stable_shapes))
    output.insert(shape);
  if (output.empty())
    return std::nullopt;
  return output;
}

std::optional<X2ShapeSet> tx_shape_set_with_fallback_value_derived_display_shapes_for_x2_sync(
    const X2ShapeSet* shapes, const X2ValueSet* values) {
  const std::optional<X2ShapeSet> stable =
      tx_shape_set_with_stable_expression_value_shapes_for_x2_sync(shapes, values);
  if (stable.has_value() && !stable->empty())
    return stable;
  const std::optional<X2ShapeSet> fallback =
      shape_set_with_value_derived_display_shapes(nullptr, values);
  if (!fallback.has_value() || fallback->empty())
    return std::nullopt;
  const X2ShapeSet synced = normalize_x2_sync_shapes_from_x(&*fallback);
  if (synced.empty())
    return std::nullopt;
  return synced;
}

X2ShapeSet tx_join_visible_x2_shape_sets_with_values(const X2ShapeSet* current,
                                                     const X2ValueSet* current_values,
                                                     const X2ShapeSet* incoming,
                                                     const X2ValueSet* incoming_values) {
  const std::optional<X2ShapeSet> current_shapes =
      shape_set_with_fallback_value_derived_display_shapes(current, current_values);
  const std::optional<X2ShapeSet> incoming_shapes =
      shape_set_with_fallback_value_derived_display_shapes(incoming, incoming_values);
  X2ShapeSet output = tx_intersect_x2_shape_sets(current_shapes.has_value() ? &*current_shapes : nullptr,
                                                 incoming_shapes.has_value() ? &*incoming_shapes : nullptr);
  if (output.empty()) {
    for (const X2ShapeFact& shape : tx_common_decimal_display_shape_facts(current, incoming))
      output.insert(shape);
  }
  return output;
}

X2ShapeSet tx_join_x2_synced_shape_sets_with_values(const X2ShapeSet* current,
                                                    const X2ValueSet* current_values,
                                                    const X2ShapeSet* incoming,
                                                    const X2ValueSet* incoming_values) {
  const std::optional<X2ShapeSet> current_shapes =
      tx_shape_set_with_fallback_value_derived_display_shapes_for_x2_sync(current, current_values);
  const std::optional<X2ShapeSet> incoming_shapes =
      tx_shape_set_with_fallback_value_derived_display_shapes_for_x2_sync(incoming, incoming_values);
  return tx_intersect_x2_shape_sets(current_shapes.has_value() ? &*current_shapes : nullptr,
                                    incoming_shapes.has_value() ? &*incoming_shapes : nullptr);
}

std::optional<std::string> tx_exact_decimal_mantissa_display_source_key(const std::string& raw) {
  const X2ShapeFact shape = decimal_mantissa_shape_fact(raw);
  const std::optional<X2ShapeFact> exact = exact_decimal_display_shape_fact(raw);
  if (exact.has_value() && *exact == shape)
    return stable_structural_expression_source_key(shape);
  return std::nullopt;
}

std::set<std::string> tx_vp_source_keys_from_fields(const std::set<std::string>* mantissas,
                                                    const X2ShapeSet* shapes) {
  std::set<std::string> keys;
  if (mantissas != nullptr) {
    for (const std::string& mantissa : *mantissas)
      keys.insert("decimal:" + mantissa);
    for (const std::string& mantissa : *mantissas) {
      const std::optional<std::string> key = tx_exact_decimal_mantissa_display_source_key(mantissa);
      if (key.has_value())
        keys.insert(*key);
    }
  }
  for (const X2ShapeFact& shape : x2_restored_display_source_key_shape_facts(shapes))
    keys.insert(stable_structural_expression_source_key(shape));
  return keys;
}

std::optional<std::string> tx_vp_mantissa_from_source_key(const std::string& key) {
  if (key.rfind("decimal:", 0) == 0) {
    const std::string raw = key.substr(std::string("decimal:").size());
    const std::string canonical = canonical_shape_raw(raw);
    if (decimal_exponent_mantissa_raw_is_valid(canonical))
      return canonical;
    return std::nullopt;
  }
  for (const X2ShapeFact& fact : x2_restored_display_shape_facts_from_source_key(key)) {
    const std::optional<std::string> decimal = x2_shape_fact_restored_visible_decimal(fact);
    if (decimal.has_value() && decimal_exponent_mantissa_raw_is_valid(*decimal))
      return decimal;
  }
  return std::nullopt;
}

bool tx_shape_set_has_decimal_display_source(const X2ShapeSet* shapes) {
  return !decimal_display_shape_facts(shapes).empty();
}

std::optional<RegisterValueSet> tx_join_vp_source_mantissas(const std::set<std::string>* left_mantissas,
                                                            const X2ShapeSet* left_shapes,
                                                            const std::set<std::string>* right_mantissas,
                                                            const X2ShapeSet* right_shapes) {
  const std::optional<std::set<std::string>> direct =
      tx_join_optional_string_sets(left_mantissas, right_mantissas);
  const std::size_t left_size = left_shapes != nullptr ? left_shapes->size() : 0U;
  const std::size_t right_size = right_shapes != nullptr ? right_shapes->size() : 0U;
  if (direct.has_value() && left_size == 0U && right_size == 0U)
    return *direct;
  std::set<std::string> mantissas = direct.has_value() ? *direct : std::set<std::string>{};
  const std::set<std::string> left_keys = tx_vp_source_keys_from_fields(left_mantissas, left_shapes);
  const std::set<std::string> right_keys = tx_vp_source_keys_from_fields(right_mantissas, right_shapes);
  for (const std::string& key : tx_join_string_sets(left_keys, right_keys)) {
    const std::optional<std::string> mantissa = tx_vp_mantissa_from_source_key(key);
    if (mantissa.has_value())
      mantissas.insert(*mantissa);
  }
  if (mantissas.empty())
    return std::nullopt;
  return mantissas;
}

std::optional<X2ShapeSet> tx_join_vp_source_shape_facts(const std::set<std::string>* left_mantissas,
                                                        const X2ShapeSet* left_shapes,
                                                        const std::set<std::string>* right_mantissas,
                                                        const X2ShapeSet* right_shapes,
                                                        bool include_decimal) {
  const X2ShapeSet direct = tx_intersect_x2_shape_sets(left_shapes, right_shapes);
  if (!direct.empty() && !include_decimal)
    return direct;
  const std::size_t left_mantissa_size = left_mantissas != nullptr ? left_mantissas->size() : 0U;
  const std::size_t right_mantissa_size = right_mantissas != nullptr ? right_mantissas->size() : 0U;
  if (!direct.empty() && left_mantissa_size == 0U && right_mantissa_size == 0U &&
      !tx_shape_set_has_decimal_display_source(left_shapes) &&
      !tx_shape_set_has_decimal_display_source(right_shapes))
    return direct;
  X2ShapeSet shapes = direct;
  const std::set<std::string> left_keys = tx_vp_source_keys_from_fields(left_mantissas, left_shapes);
  const std::set<std::string> right_keys = tx_vp_source_keys_from_fields(right_mantissas, right_shapes);
  for (const std::string& key : tx_join_string_sets(left_keys, right_keys)) {
    for (const X2ShapeFact& fact : x2_restored_display_shape_facts_from_source_key(key)) {
      const X2ShapeSafety safety = x2_shape_fact_safety(fact);
      if (safety == X2ShapeSafety::StructuralOnly ||
          (include_decimal && safety != X2ShapeSafety::Unknown))
        shapes.insert(fact);
    }
  }
  const std::size_t left_shape_size = left_shapes != nullptr ? left_shapes->size() : 0U;
  const std::size_t right_shape_size = right_shapes != nullptr ? right_shapes->size() : 0U;
  if (!shapes.empty() || (left_shape_size == 0U && right_shape_size == 0U))
    return shapes;
  return std::nullopt;
}

X2EntryState tx_unknown_x2_entry_state() {
  X2EntryState state;
  state.kind = X2EntryState::Kind::Unknown;
  return state;
}

X2EntryState tx_join_x2_entry_states(const X2EntryState& current, const X2EntryState& incoming) {
  if (current.kind == X2EntryState::Kind::Unknown || incoming.kind == X2EntryState::Kind::Unknown)
    return tx_unknown_x2_entry_state();
  if (current.kind == X2EntryState::Kind::Closed || incoming.kind == X2EntryState::Kind::Closed ||
      current.kind != incoming.kind)
    return current.kind == incoming.kind ? closed_x2_entry_state() : tx_unknown_x2_entry_state();
  if (current.kind == X2EntryState::Kind::Exponent && incoming.kind == X2EntryState::Kind::Exponent) {
    const X2EntryState left = x2_entry_state_from_exponent_parts(&current.mantissa, &current.exponent);
    const X2EntryState right = x2_entry_state_from_exponent_parts(&incoming.mantissa, &incoming.exponent);
    if (left.kind != X2EntryState::Kind::Exponent || right.kind != X2EntryState::Kind::Exponent)
      return tx_unknown_x2_entry_state();
    const RegisterValueSet mantissa = tx_join_string_sets(left.mantissa, right.mantissa);
    const RegisterValueSet exponent = tx_join_string_sets(left.exponent, right.exponent);
    return x2_entry_state_from_exponent_parts(&mantissa, &exponent);
  }
  if (current.kind != X2EntryState::Kind::Open || incoming.kind != X2EntryState::Kind::Open)
    return tx_unknown_x2_entry_state();
  const X2EntryState left = x2_entry_state_from_open_raw(&current.raw);
  const X2EntryState right = x2_entry_state_from_open_raw(&incoming.raw);
  if (left.kind != X2EntryState::Kind::Open || right.kind != X2EntryState::Kind::Open)
    return tx_unknown_x2_entry_state();
  const RegisterValueSet raw = tx_join_string_sets(left.raw, right.raw);
  return x2_entry_state_from_open_raw(&raw);
}

X2VpContextState tx_unknown_x2_vp_context_state() {
  X2VpContextState state;
  state.kind = X2VpContextState::Kind::Unknown;
  return state;
}

X2VpContextState tx_join_x2_vp_context_states(const X2VpContextState& left, const X2VpContextState& right) {
  if (left.kind == X2VpContextState::Kind::Unknown || right.kind == X2VpContextState::Kind::Unknown)
    return tx_unknown_x2_vp_context_state();
  if (left.kind == X2VpContextState::Kind::None || right.kind == X2VpContextState::Kind::None ||
      left.kind != right.kind)
    return left.kind == right.kind ? none_x2_vp_context_state() : tx_unknown_x2_vp_context_state();
  const X2VpContextState left_context =
      x2_vp_context_state_from_exponent_parts(&left.mantissa, &left.exponent);
  const X2VpContextState right_context =
      x2_vp_context_state_from_exponent_parts(&right.mantissa, &right.exponent);
  if (left_context.kind != X2VpContextState::Kind::Exponent ||
      right_context.kind != X2VpContextState::Kind::Exponent)
    return tx_unknown_x2_vp_context_state();
  const RegisterValueSet mantissa = tx_join_string_sets(left_context.mantissa, right_context.mantissa);
  const RegisterValueSet exponent = tx_join_string_sets(left_context.exponent, right_context.exponent);
  return x2_vp_context_state_from_exponent_parts(&mantissa, &exponent);
}

X2StructuralEntryState tx_unknown_x2_structural_entry_state() {
  X2StructuralEntryState state;
  state.kind = X2StructuralEntryState::Kind::Unknown;
  return state;
}

X2StructuralEntryState tx_join_x2_structural_entry_states(const X2StructuralEntryState& current,
                                                          const X2StructuralEntryState& incoming) {
  if (current.kind == X2StructuralEntryState::Kind::Unknown ||
      incoming.kind == X2StructuralEntryState::Kind::Unknown)
    return tx_unknown_x2_structural_entry_state();
  if (current.kind == X2StructuralEntryState::Kind::None ||
      incoming.kind == X2StructuralEntryState::Kind::None || current.kind != incoming.kind)
    return current.kind == incoming.kind ? none_x2_structural_entry_state()
                                         : tx_unknown_x2_structural_entry_state();
  const X2ShapeSet mantissa = tx_intersect_x2_shape_sets(&current.mantissa, &incoming.mantissa);
  const RegisterValueSet exponent = tx_join_string_sets(current.exponent, incoming.exponent);
  return x2_structural_entry_state_from_parts(&mantissa, &exponent);
}

bool tx_same_x2_entry_state(const X2EntryState& left, const X2EntryState& right) {
  if (left.kind != right.kind)
    return false;
  if (left.kind == X2EntryState::Kind::Exponent && right.kind == X2EntryState::Kind::Exponent)
    return tx_same_string_set(left.mantissa, right.mantissa) &&
           tx_same_string_set(left.exponent, right.exponent);
  if (left.kind != X2EntryState::Kind::Open || right.kind != X2EntryState::Kind::Open)
    return true;
  return tx_same_string_set(left.raw, right.raw);
}

bool tx_same_x2_vp_context_state(const X2VpContextState& left, const X2VpContextState& right) {
  if (left.kind != right.kind)
    return false;
  if (left.kind != X2VpContextState::Kind::Exponent || right.kind != X2VpContextState::Kind::Exponent)
    return true;
  return tx_same_string_set(left.mantissa, right.mantissa) &&
         tx_same_string_set(left.exponent, right.exponent);
}

bool tx_same_x2_structural_entry_state(const X2StructuralEntryState& left,
                                       const X2StructuralEntryState& right) {
  if (left.kind != right.kind)
    return false;
  if (left.kind != X2StructuralEntryState::Kind::Exponent ||
      right.kind != X2StructuralEntryState::Kind::Exponent)
    return true;
  return tx_same_optional_shape_set(&left.mantissa, &right.mantissa) &&
         tx_same_string_set(left.exponent, right.exponent);
}

std::vector<std::string> tx_memory_registers(const X2ValueMemory* input) {
  std::vector<std::string> output;
  if (input == nullptr)
    return output;
  for (const std::string& name : register_names()) {
    auto found = input->find(name);
    if (found != input->end())
      output.push_back(name);
  }
  return output;
}

std::vector<std::string> tx_shape_memory_registers(const X2ShapeMemory* input) {
  std::vector<std::string> output;
  if (input == nullptr)
    return output;
  for (const std::string& name : register_names()) {
    auto found = input->find(name);
    if (found != input->end())
      output.push_back(name);
  }
  return output;
}

X2ValueSet tx_intersect_known_x2_value_sets(const X2ValueSet* current, const X2ValueSet* incoming) {
  if (current == nullptr || incoming == nullptr)
    return {};
  const X2ValueSet current_set = tx_value_set_with_stable_expression_decimal_facts(current);
  const X2ValueSet incoming_set = tx_value_set_with_stable_expression_decimal_facts(incoming);
  X2ValueSet joined;
  for (const X2ValueFact& value : current_set)
    if (incoming_set.count(value) != 0U)
      joined.insert(value);
  return joined;
}

X2ShapeSet tx_intersect_known_x2_shape_sets_with_values(const X2ShapeSet* current,
                                                        const X2ValueSet* current_values,
                                                        const X2ShapeSet* incoming,
                                                        const X2ValueSet* incoming_values) {
  const std::optional<X2ShapeSet> current_shapes =
      shape_set_with_fallback_value_derived_display_shapes(current, current_values);
  const std::optional<X2ShapeSet> incoming_shapes =
      shape_set_with_fallback_value_derived_display_shapes(incoming, incoming_values);
  return tx_intersect_x2_shape_sets(current_shapes.has_value() ? &*current_shapes : nullptr,
                                    incoming_shapes.has_value() ? &*incoming_shapes : nullptr);
}

X2ValueMemory tx_join_x2_value_memories(const X2ValueMemory* current, const X2ValueMemory* incoming) {
  X2ValueMemory output;
  if (current == nullptr || incoming == nullptr)
    return output;
  for (const std::string& reg : tx_memory_registers(current)) {
    const X2ValueSet* current_values = nullptr;
    const X2ValueSet* incoming_values = nullptr;
    auto current_found = current->find(reg);
    if (current_found != current->end())
      current_values = &current_found->second;
    auto incoming_found = incoming->find(reg);
    if (incoming_found != incoming->end())
      incoming_values = &incoming_found->second;
    const X2ValueSet values = tx_intersect_known_x2_value_sets(current_values, incoming_values);
    if (!values.empty())
      output[reg] = values;
  }
  return output;
}

X2ShapeMemory tx_join_x2_shape_memories(const X2ShapeMemory* current, const X2ShapeMemory* incoming,
                                        const X2ValueMemory* current_values,
                                        const X2ValueMemory* incoming_values) {
  X2ShapeMemory output;
  if (current == nullptr && incoming == nullptr && current_values == nullptr &&
      incoming_values == nullptr)
    return output;
  for (const std::string& reg : register_names()) {
    const X2ShapeSet* current_shape = nullptr;
    const X2ShapeSet* incoming_shape = nullptr;
    const X2ValueSet* current_value = nullptr;
    const X2ValueSet* incoming_value = nullptr;
    if (current != nullptr) {
      auto found = current->find(reg);
      if (found != current->end())
        current_shape = &found->second;
    }
    if (incoming != nullptr) {
      auto found = incoming->find(reg);
      if (found != incoming->end())
        incoming_shape = &found->second;
    }
    if (current_values != nullptr) {
      auto found = current_values->find(reg);
      if (found != current_values->end())
        current_value = &found->second;
    }
    if (incoming_values != nullptr) {
      auto found = incoming_values->find(reg);
      if (found != incoming_values->end())
        incoming_value = &found->second;
    }
    const X2ShapeSet shapes = tx_intersect_known_x2_shape_sets_with_values(current_shape, current_value,
                                                                           incoming_shape, incoming_value);
    if (!shapes.empty())
      output[reg] = shapes;
  }
  return output;
}

bool tx_same_x2_value_memory(const X2ValueMemory* left, const X2ValueMemory* right) {
  const std::vector<std::string> left_regs = tx_memory_registers(left);
  const std::vector<std::string> right_regs = tx_memory_registers(right);
  if (left_regs.size() != right_regs.size())
    return false;
  for (const std::string& reg : left_regs) {
    if (std::find(right_regs.begin(), right_regs.end(), reg) == right_regs.end())
      return false;
    const X2ValueSet* left_values = nullptr;
    const X2ValueSet* right_values = nullptr;
    if (left != nullptr) {
      auto found = left->find(reg);
      if (found != left->end())
        left_values = &found->second;
    }
    if (right != nullptr) {
      auto found = right->find(reg);
      if (found != right->end())
        right_values = &found->second;
    }
    if (!tx_same_x2_value_set(left_values, right_values))
      return false;
  }
  return true;
}

bool tx_same_x2_shape_memory(const X2ShapeMemory* left, const X2ShapeMemory* right) {
  const std::vector<std::string> left_regs = tx_shape_memory_registers(left);
  const std::vector<std::string> right_regs = tx_shape_memory_registers(right);
  if (left_regs.size() != right_regs.size())
    return false;
  for (const std::string& reg : left_regs) {
    if (std::find(right_regs.begin(), right_regs.end(), reg) == right_regs.end())
      return false;
    const X2ShapeSet* left_shapes = nullptr;
    const X2ShapeSet* right_shapes = nullptr;
    if (left != nullptr) {
      auto found = left->find(reg);
      if (found != left->end())
        left_shapes = &found->second;
    }
    if (right != nullptr) {
      auto found = right->find(reg);
      if (found != right->end())
        right_shapes = &found->second;
    }
    if (!tx_same_optional_shape_set(left_shapes, right_shapes))
      return false;
  }
  return true;
}

const X2ShapeSet* tx_opt_shape_ptr(const std::optional<X2ShapeSet>& value) {
  return value.has_value() ? &*value : nullptr;
}

const X2ShapeSet* tx_opt_shape_ptr_from_required(const X2ShapeSet& value) {
  return &value;
}

const X2ValueSet* tx_opt_value_ptr(const std::optional<X2ValueSet>& value) {
  return value.has_value() ? &*value : nullptr;
}

const RegisterValueSet* tx_opt_string_ptr(const std::optional<RegisterValueSet>& value) {
  return value.has_value() ? &*value : nullptr;
}

X2ValueDataflowState tx_join_x2_value_dataflow_states(const X2ValueDataflowState* current,
                                                      const X2ValueDataflowState& incoming,
                                                      bool track_register_memory) {
  if (current == nullptr) {
    X2ValueDataflowState output = internal_clone_x2_value_dataflow_state(incoming);
    if (!track_register_memory) {
      output.memory = std::nullopt;
      output.shapeMemory = std::nullopt;
    }
    return output;
  }
  const X2ValueDataflowState& left = *current;
  X2ValueDataflowState output;
  output.x = tx_join_x2_value_sets(&left.x, incoming.x);
  output.y = tx_join_x2_value_sets(left.y.has_value() ? &*left.y : nullptr,
                                   incoming.y.value_or(X2ValueSet{}));
  output.x2 = tx_join_x2_value_sets(&left.x2, incoming.x2);
  output.xShape = tx_join_visible_x2_shape_sets_with_values(
      tx_opt_shape_ptr_from_required(left.xShape), &left.x,
      tx_opt_shape_ptr_from_required(incoming.xShape), &incoming.x);
  output.yShape = tx_join_visible_x2_shape_sets_with_values(
      tx_opt_shape_ptr(left.yShape), tx_opt_value_ptr(left.y), tx_opt_shape_ptr(incoming.yShape),
      tx_opt_value_ptr(incoming.y));
  output.x2Shape = tx_join_x2_synced_shape_sets_with_values(
      tx_opt_shape_ptr_from_required(left.x2Shape), &left.x2,
      tx_opt_shape_ptr_from_required(incoming.x2Shape), &incoming.x2);
  output.xDirectShape = tx_intersect_x2_shape_sets(tx_opt_shape_ptr(left.xDirectShape),
                                                   tx_opt_shape_ptr(incoming.xDirectShape));
  output.yDirectShape = tx_intersect_x2_shape_sets(tx_opt_shape_ptr(left.yDirectShape),
                                                   tx_opt_shape_ptr(incoming.yDirectShape));
  output.entry = tx_join_x2_entry_states(left.entry, incoming.entry);
  output.vpContext = tx_join_x2_vp_context_states(left.vpContext, incoming.vpContext);
  output.structuralEntry = tx_join_x2_structural_entry_states(left.structuralEntry, incoming.structuralEntry);
  output.structuralVpContext =
      tx_join_x2_structural_entry_states(left.structuralVpContext, incoming.structuralVpContext);
  output.vpEntryMantissa = tx_join_vp_source_mantissas(
      tx_opt_string_ptr(left.vpEntryMantissa), tx_opt_shape_ptr(left.vpEntryShape),
      tx_opt_string_ptr(incoming.vpEntryMantissa), tx_opt_shape_ptr(incoming.vpEntryShape));
  output.vpEntryMantissaTransient =
      left.vpEntryMantissaTransient || incoming.vpEntryMantissaTransient;
  output.vpEntrySignMantissa = tx_join_vp_source_mantissas(
      tx_opt_string_ptr(left.vpEntrySignMantissa), tx_opt_shape_ptr(left.vpEntrySignShape),
      tx_opt_string_ptr(incoming.vpEntrySignMantissa), tx_opt_shape_ptr(incoming.vpEntrySignShape));
  output.vpEntryShape = tx_join_vp_source_shape_facts(
      tx_opt_string_ptr(left.vpEntryMantissa), tx_opt_shape_ptr(left.vpEntryShape),
      tx_opt_string_ptr(incoming.vpEntryMantissa), tx_opt_shape_ptr(incoming.vpEntryShape), false);
  output.vpEntrySignShape = tx_join_vp_source_shape_facts(
      tx_opt_string_ptr(left.vpEntrySignMantissa), tx_opt_shape_ptr(left.vpEntrySignShape),
      tx_opt_string_ptr(incoming.vpEntrySignMantissa), tx_opt_shape_ptr(incoming.vpEntrySignShape),
      true);
  output.vpEntryShapeTransient = left.vpEntryShapeTransient || incoming.vpEntryShapeTransient;
  if (track_register_memory) {
    output.memory = tx_join_x2_value_memories(left.memory.has_value() ? &*left.memory : nullptr,
                                              incoming.memory.has_value() ? &*incoming.memory : nullptr);
    output.shapeMemory = tx_join_x2_shape_memories(
        left.shapeMemory.has_value() ? &*left.shapeMemory : nullptr,
        incoming.shapeMemory.has_value() ? &*incoming.shapeMemory : nullptr,
        left.memory.has_value() ? &*left.memory : nullptr,
        incoming.memory.has_value() ? &*incoming.memory : nullptr);
  } else {
    output.memory = std::nullopt;
    output.shapeMemory = std::nullopt;
  }
  return output;
}

bool tx_same_x2_value_dataflow_state(const X2ValueDataflowState* left,
                                     const X2ValueDataflowState* right) {
  if (left == nullptr || right == nullptr)
    return left == nullptr && right == nullptr;
  return tx_same_x2_value_set(&left->x, &right->x) &&
         tx_same_x2_value_set(left->y.has_value() ? &*left->y : nullptr,
                              right->y.has_value() ? &*right->y : nullptr) &&
         tx_same_x2_value_set(&left->x2, &right->x2) &&
         tx_same_optional_shape_set(tx_opt_shape_ptr_from_required(left->xShape),
                                    tx_opt_shape_ptr_from_required(right->xShape)) &&
         tx_same_optional_shape_set(tx_opt_shape_ptr(left->yShape), tx_opt_shape_ptr(right->yShape)) &&
         tx_same_optional_shape_set(tx_opt_shape_ptr_from_required(left->x2Shape),
                                    tx_opt_shape_ptr_from_required(right->x2Shape)) &&
         tx_same_optional_shape_set(tx_opt_shape_ptr(left->xDirectShape),
                                    tx_opt_shape_ptr(right->xDirectShape)) &&
         tx_same_optional_shape_set(tx_opt_shape_ptr(left->yDirectShape),
                                    tx_opt_shape_ptr(right->yDirectShape)) &&
         tx_same_x2_entry_state(left->entry, right->entry) &&
         tx_same_x2_vp_context_state(left->vpContext, right->vpContext) &&
         tx_same_x2_structural_entry_state(left->structuralEntry, right->structuralEntry) &&
         tx_same_x2_structural_entry_state(left->structuralVpContext, right->structuralVpContext) &&
         tx_same_optional_string_set(tx_opt_string_ptr(left->vpEntryMantissa),
                                     tx_opt_string_ptr(right->vpEntryMantissa)) &&
         left->vpEntryMantissaTransient == right->vpEntryMantissaTransient &&
         tx_same_optional_string_set(tx_opt_string_ptr(left->vpEntrySignMantissa),
                                     tx_opt_string_ptr(right->vpEntrySignMantissa)) &&
         tx_same_optional_shape_set(tx_opt_shape_ptr(left->vpEntryShape),
                                    tx_opt_shape_ptr(right->vpEntryShape)) &&
         tx_same_optional_shape_set(tx_opt_shape_ptr(left->vpEntrySignShape),
                                    tx_opt_shape_ptr(right->vpEntrySignShape)) &&
         left->vpEntryShapeTransient == right->vpEntryShapeTransient &&
         tx_same_x2_value_memory(left->memory.has_value() ? &*left->memory : nullptr,
                                 right->memory.has_value() ? &*right->memory : nullptr) &&
         tx_same_x2_shape_memory(left->shapeMemory.has_value() ? &*left->shapeMemory : nullptr,
                                 right->shapeMemory.has_value() ? &*right->shapeMemory : nullptr);
}

// Session C.3 isolation test hook: replays a sequence of plain opcodes through
// transfer_plain_x2_value_state (starting from the empty state) and returns a
// canonical serialization of the final state for the differential unit test.
std::string tx_join_set(const std::set<std::string>& items) {
  std::string output;
  for (const std::string& item : items) {
    if (!output.empty())
      output += ",";
    output += item;
  }
  return output;
}

std::string tx_entry(const X2EntryState& entry) {
  switch (entry.kind) {
    case X2EntryState::Kind::Closed:
      return "C";
    case X2EntryState::Kind::Unknown:
      return "U";
    case X2EntryState::Kind::Open:
      return "O:" + tx_join_set(std::set<std::string>{entry.raw.begin(), entry.raw.end()});
    case X2EntryState::Kind::Exponent:
      return "E:" + tx_join_set(std::set<std::string>{entry.mantissa.begin(), entry.mantissa.end()}) +
             ";" + tx_join_set(std::set<std::string>{entry.exponent.begin(), entry.exponent.end()});
  }
  return "?";
}

std::string tx_vpc(const X2VpContextState& vpc) {
  switch (vpc.kind) {
    case X2VpContextState::Kind::None:
      return "N";
    case X2VpContextState::Kind::Unknown:
      return "U";
    case X2VpContextState::Kind::Exponent:
      return "E:" + tx_join_set(std::set<std::string>{vpc.mantissa.begin(), vpc.mantissa.end()}) +
             ";" + tx_join_set(std::set<std::string>{vpc.exponent.begin(), vpc.exponent.end()});
  }
  return "?";
}

std::string tx_struct(const X2StructuralEntryState& se) {
  switch (se.kind) {
    case X2StructuralEntryState::Kind::None:
      return "N";
    case X2StructuralEntryState::Kind::Unknown:
      return "U";
    case X2StructuralEntryState::Kind::Exponent:
      return "E:" + tx_join_set(std::set<std::string>{se.mantissa.begin(), se.mantissa.end()}) + ";" +
             tx_join_set(std::set<std::string>{se.exponent.begin(), se.exponent.end()});
  }
  return "?";
}

std::string tx_opt_value(const std::optional<X2ValueSet>& items) {
  return items.has_value() ? tx_join_set(std::set<std::string>{items->begin(), items->end()})
                           : std::string{"_"};
}

std::string tx_opt_shape(const std::optional<X2ShapeSet>& items) {
  return items.has_value() ? tx_join_set(std::set<std::string>{items->begin(), items->end()})
                           : std::string{"_"};
}

std::string serialize_x2_state(const X2ValueDataflowState& s) {
  std::string out;
  out += "x=" + tx_join_set(std::set<std::string>{s.x.begin(), s.x.end()});
  out += "|y=" + tx_opt_value(s.y);
  out += "|x2=" + tx_join_set(std::set<std::string>{s.x2.begin(), s.x2.end()});
  out += "|xs=" + tx_join_set(std::set<std::string>{s.xShape.begin(), s.xShape.end()});
  out += "|ys=" + tx_opt_shape(s.yShape);
  out += "|x2s=" + tx_join_set(std::set<std::string>{s.x2Shape.begin(), s.x2Shape.end()});
  out += "|xds=" + tx_opt_shape(s.xDirectShape);
  out += "|yds=" + tx_opt_shape(s.yDirectShape);
  out += "|entry=" + tx_entry(s.entry);
  out += "|vpc=" + tx_vpc(s.vpContext);
  out += "|se=" + tx_struct(s.structuralEntry);
  out += "|svpc=" + tx_struct(s.structuralVpContext);
  out += "|vem=" + (s.vpEntryMantissa.has_value()
                        ? tx_join_set(std::set<std::string>{s.vpEntryMantissa->begin(),
                                                            s.vpEntryMantissa->end()})
                        : std::string{"_"});
  out += "|vemt=" + std::string(s.vpEntryMantissaTransient ? "1" : "0");
  out += "|vsm=" + (s.vpEntrySignMantissa.has_value()
                        ? tx_join_set(std::set<std::string>{s.vpEntrySignMantissa->begin(),
                                                            s.vpEntrySignMantissa->end()})
                        : std::string{"_"});
  out += "|vesh=" + tx_opt_shape(s.vpEntryShape);
  out += "|vssh=" + tx_opt_shape(s.vpEntrySignShape);
  out += "|vest=" + std::string(s.vpEntryShapeTransient ? "1" : "0");
  return out;
}

// ===========================================================================
// x2-noop-restore pass helper cluster (faithful 1:1 port of the TS helpers
// consumed by src/core/passes/x2-noop-restore.ts). Additive; reuses the
// prior-layer shape/value/state machinery with no duplication.
// ===========================================================================

constexpr int kX2EmptyOpcodeStart = 0x54;
constexpr int kX2EmptyOpcodeEnd = 0x56;
constexpr int kX2VpOpcode = 0x0c;
constexpr int kX2DotOpcode = 0x0a;

// --- free-standing op predicates ------------------------------------------
bool is_free_standing_x2_sign_change_op(const IrOp& op) {
  return op.kind == IrKind::Plain && op.opcode == X2_SIGN_CHANGE_OPCODE &&
         !has_rewrite_barrier(op) && !is_display_focus_sensitive(op) && !has_ir_roles(op);
}

bool is_free_standing_x2_empty_op(const IrOp& op) {
  return op.kind == IrKind::Plain && op.opcode >= kX2EmptyOpcodeStart &&
         op.opcode <= kX2EmptyOpcodeEnd && !has_rewrite_barrier(op) &&
         !is_display_focus_sensitive(op) && !has_ir_roles(op);
}

bool is_free_standing_x2_restore_gap_op(const IrOp& op) {
  return op.kind == IrKind::Plain && !has_rewrite_barrier(op) && !is_display_focus_sensitive(op) &&
         !has_ir_roles(op) &&
         (op.opcode == X2_SIGN_CHANGE_OPCODE ||
          (op.opcode >= kX2EmptyOpcodeStart && op.opcode <= kX2EmptyOpcodeEnd));
}

bool is_free_standing_x2_vp_op(const IrOp& op) {
  return op.kind == IrKind::Plain && op.opcode == kX2VpOpcode && !has_rewrite_barrier(op) &&
         !is_display_focus_sensitive(op) && !has_ir_roles(op);
}

bool is_linear_x2_restore_gap_transparent_op(const IrOp& op) {
  return op.kind == IrKind::OrphanAddress || is_free_standing_x2_empty_op(op);
}

bool x2_restore_gap_direct_return_does_not_observe_restore(const std::vector<IrOp>& ops,
                                                           const IrOp& call,
                                                           const DirectReturnAnalysisContext& context) {
  std::map<int, bool> memo;
  std::set<int> active;
  return nested_return_call_range_is_transparent(ops, call, context,
                                                 is_linear_x2_restore_gap_transparent_op, memo, active);
}

// --- effective input shapes / restore safety ------------------------------
std::optional<X2ShapeSet> nr_effective_visible_x_state_shape(const X2ValueDataflowState* state) {
  if (state == nullptr)
    return std::nullopt;
  return shape_set_with_fallback_value_derived_display_shapes(&state->xShape, &state->x);
}

std::optional<X2ShapeSet> nr_effective_x2_state_shape(const X2ValueDataflowState* state) {
  if (state == nullptr)
    return std::nullopt;
  return shape_set_with_fallback_value_derived_display_shapes(&state->x2Shape, &state->x2);
}

bool is_concrete_decimal_x2_value_fact(const X2ValueFact& fact) {
  static const std::string prefix = "decimal:";
  if (fact.rfind(prefix, 0) != 0)
    return false;
  const std::string rest = fact.substr(prefix.size());
  std::string suffix;
  if (rest.size() >= 11U && rest.compare(rest.size() - 11U, 11U, ":normalized") == 0)
    suffix = ":normalized";
  else if (rest.size() >= 13U && rest.compare(rest.size() - 13U, 13U, ":unnormalized") == 0)
    suffix = ":unnormalized";
  else
    return false;
  const std::string middle = rest.substr(0, rest.size() - suffix.size());
  return tx_scan_plain_decimal(middle).has_value();
}

bool x2_value_set_has_concrete_decimal(const X2ValueSet* input) {
  if (input == nullptr)
    return false;
  for (const X2ValueFact& fact : *input) {
    if (is_concrete_decimal_x2_value_fact(fact))
      return true;
  }
  return false;
}

X2ShapeSafety x2_shape_set_safety(const X2ShapeSet* input) {
  if (input == nullptr || input->empty())
    return X2ShapeSafety::Unknown;
  bool saw_dot_safe = false;
  bool saw_structural = false;
  bool saw_unknown = false;
  for (const X2ShapeDataModel& model : x2_shape_data_models(input)) {
    const X2ShapeSafety safety = model.safety;
    if (safety == X2ShapeSafety::ErrorProne)
      return X2ShapeSafety::ErrorProne;
    if (safety == X2ShapeSafety::StructuralOnly)
      saw_structural = true;
    else if (safety == X2ShapeSafety::DotSafeDecimal)
      saw_dot_safe = true;
    else
      saw_unknown = true;
  }
  if (saw_structural)
    return X2ShapeSafety::StructuralOnly;
  if (saw_unknown)
    return X2ShapeSafety::Unknown;
  return saw_dot_safe ? X2ShapeSafety::DotSafeDecimal : X2ShapeSafety::Unknown;
}

X2ShapeSafety x2_restore_safety(const X2ValueDataflowState* state) {
  if (state == nullptr)
    return X2ShapeSafety::Unknown;
  if (x2_value_set_has_concrete_decimal(&state->x2))
    return X2ShapeSafety::DotSafeDecimal;
  const std::optional<X2ShapeSet> eff = nr_effective_x2_state_shape(state);
  return x2_shape_set_safety(eff.has_value() ? &*eff : nullptr);
}

std::set<std::string> dot_safe_decimal_shape_values(const X2ShapeSet* input) {
  std::set<std::string> output;
  for (const X2ShapeDataModel& model : x2_shape_data_models(input)) {
    if (model.kind != X2ShapeModelKind::Mantissa || !model.mantissa.has_value() ||
        model.mantissa->radix != X2MantissaRadix::Decimal ||
        model.mantissa->safety != X2ShapeSafety::DotSafeDecimal)
      continue;
    if (model.mantissa->normalizedDecimal.has_value() && model.mantissa->normalizedSameAsRaw)
      output.insert(*model.mantissa->normalizedDecimal);
  }
  return output;
}

bool x2_shape_sets_have_same_dot_safe_decimal(const X2ShapeSet* left, const X2ShapeSet* right) {
  if (x2_shape_set_safety(left) != X2ShapeSafety::DotSafeDecimal ||
      x2_shape_set_safety(right) != X2ShapeSafety::DotSafeDecimal)
    return false;
  const std::set<std::string> left_values = dot_safe_decimal_shape_values(left);
  const std::set<std::string> right_values = dot_safe_decimal_shape_values(right);
  for (const std::string& value : left_values) {
    if (right_values.count(value) > 0)
      return true;
  }
  return false;
}

bool x2_shape_sets_have_same_dot_safe_structural_mantissa(const X2ShapeSet* left,
                                                          const X2ShapeSet* right) {
  const std::set<std::string> left_keys = dot_safe_structural_mantissa_restore_keys(left);
  if (left_keys.empty())
    return false;
  for (const std::string& key : dot_safe_structural_mantissa_restore_keys(right)) {
    if (left_keys.count(key) > 0)
      return true;
  }
  return false;
}

bool x2_shape_sets_have_same_restored_display_shape(const X2ShapeSet* left, const X2ShapeSet* right) {
  const std::set<X2ShapeFact> left_shapes = x2_restored_display_shape_facts(left);
  if (left_shapes.empty())
    return false;
  for (const X2ShapeFact& shape : x2_restored_display_shape_facts(right)) {
    if (left_shapes.count(shape) > 0)
      return true;
  }
  return false;
}

bool x2_value_shape_sets_have_same_restored_display_shape(const X2ValueSet* left_values,
                                                          const X2ShapeSet* left_shapes,
                                                          const X2ValueSet* right_values,
                                                          const X2ShapeSet* right_shapes) {
  const std::optional<X2ShapeSet> left =
      shape_set_with_fallback_value_derived_display_shapes(left_shapes, left_values);
  const std::optional<X2ShapeSet> right =
      shape_set_with_fallback_value_derived_display_shapes(right_shapes, right_values);
  return x2_shape_sets_have_same_restored_display_shape(left.has_value() ? &*left : nullptr,
                                                        right.has_value() ? &*right : nullptr);
}

bool x2_value_shape_sets_have_same_dot_safe_decimal(const X2ValueSet* left_values,
                                                    const X2ShapeSet* left_shapes,
                                                    const X2ValueSet* right_values,
                                                    const X2ShapeSet* right_shapes) {
  const std::optional<X2ShapeSet> left =
      shape_set_with_fallback_value_derived_display_shapes(left_shapes, left_values);
  const std::optional<X2ShapeSet> right =
      shape_set_with_fallback_value_derived_display_shapes(right_shapes, right_values);
  return x2_shape_sets_have_same_dot_safe_decimal(left.has_value() ? &*left : nullptr,
                                                  right.has_value() ? &*right : nullptr);
}

bool x2_value_shape_sets_have_same_dot_safe_structural_mantissa(const X2ValueSet* left_values,
                                                                const X2ShapeSet* left_shapes,
                                                                const X2ValueSet* right_values,
                                                                const X2ShapeSet* right_shapes) {
  const std::optional<X2ShapeSet> left =
      shape_set_with_fallback_value_derived_display_shapes(left_shapes, left_values);
  const std::optional<X2ShapeSet> right =
      shape_set_with_fallback_value_derived_display_shapes(right_shapes, right_values);
  return x2_shape_sets_have_same_dot_safe_structural_mantissa(left.has_value() ? &*left : nullptr,
                                                              right.has_value() ? &*right : nullptr);
}

// --- value-set intersection / normalized-decimal facts --------------------
bool x2_value_set_has_intersection(const X2ValueSet* left, const X2ValueSet* right) {
  if (left == nullptr || right == nullptr)
    return false;
  const X2ValueSet right_set = canonical_x2_value_set(right);
  for (const X2ValueFact& value : canonical_x2_value_set(left)) {
    if (right_set.count(value) > 0)
      return true;
  }
  return false;
}

std::optional<X2ValueFact> nr_canonical_x2_value_fact_if_valid(const X2ValueFact& fact) {
  return fact.rfind("expr-key:", 0) == 0 ? canonical_stable_expression_value_fact_if_valid(fact)
                                          : std::optional<X2ValueFact>{fact};
}

bool x2_value_fact_is_normalized_decimal(const X2ValueFact& fact) {
  return normalized_decimal_value_from_fact(fact).has_value();
}

bool x2_value_set_has_fact(const X2ValueSet* input, const X2ValueFact& fact) {
  const std::optional<X2ValueFact> canonical = nr_canonical_x2_value_fact_if_valid(fact);
  if (!canonical.has_value())
    return false;
  return canonical_x2_value_set(input).count(*canonical) > 0;
}

bool x2_value_set_has_normalized_decimal_fact(const X2ValueSet* input, const X2ValueFact& fact) {
  return x2_value_fact_is_normalized_decimal(fact) && x2_value_set_has_fact(input, fact);
}

// --- x2State* predicates --------------------------------------------------
bool x2_state_has_same_dot_safe_decimal_in_x_and_x2(const X2ValueDataflowState* state) {
  if (state == nullptr)
    return false;
  const std::optional<X2ShapeSet> left = nr_effective_visible_x_state_shape(state);
  const std::optional<X2ShapeSet> right = nr_effective_x2_state_shape(state);
  return x2_shape_sets_have_same_dot_safe_decimal(left.has_value() ? &*left : nullptr,
                                                  right.has_value() ? &*right : nullptr);
}

bool x2_state_has_same_dot_safe_structural_mantissa_in_x_and_x2(const X2ValueDataflowState* state) {
  if (state == nullptr)
    return false;
  const std::optional<X2ShapeSet> left = nr_effective_visible_x_state_shape(state);
  const std::optional<X2ShapeSet> right = nr_effective_x2_state_shape(state);
  return x2_shape_sets_have_same_dot_safe_structural_mantissa(left.has_value() ? &*left : nullptr,
                                                              right.has_value() ? &*right : nullptr);
}

bool x2_state_has_same_dot_restore_value_in_x_and_x2(const X2ValueDataflowState* state) {
  return x2_value_set_has_intersection(state != nullptr ? &state->x : nullptr,
                                       state != nullptr ? &state->x2 : nullptr) ||
         x2_state_has_same_dot_safe_decimal_in_x_and_x2(state) ||
         x2_state_has_same_dot_safe_structural_mantissa_in_x_and_x2(state);
}

bool x2_state_has_same_normalized_decimal_in_x_and_x2(const X2ValueDataflowState* state) {
  if (state == nullptr)
    return false;
  for (const X2ValueFact& fact : state->x) {
    if (x2_value_set_has_normalized_decimal_fact(&state->x2, fact))
      return true;
  }
  return false;
}

bool x2_state_has_same_restored_visible_decimal_in_x_and_x2(const X2ValueDataflowState* state) {
  if (state == nullptr)
    return x2_value_shape_sets_have_same_restored_visible_decimal(nullptr, nullptr, nullptr, nullptr);
  return x2_value_shape_sets_have_same_restored_visible_decimal(&state->x, &state->xShape, &state->x2,
                                                                &state->x2Shape);
}

bool x2_state_has_unsafe_dot_restore_shape_x2(const X2ValueDataflowState* state) {
  const X2ShapeSafety safety = x2_restore_safety(state);
  return safety == X2ShapeSafety::StructuralOnly || safety == X2ShapeSafety::ErrorProne;
}

bool x2_state_has_only_dot_safe_structural_mantissa_x2(const X2ValueDataflowState* state) {
  if (state == nullptr)
    return false;
  if (x2_value_set_has_concrete_decimal(&state->x2))
    return false;
  const std::optional<X2ShapeSet> eff = nr_effective_x2_state_shape(state);
  return x2_shape_set_has_only_dot_safe_structural_mantissas(eff.has_value() ? &*eff : nullptr);
}

bool x2_state_is_closed_dot_restore_value_context(const X2ValueDataflowState* state) {
  if (state == nullptr || state->entry.kind != X2EntryState::Kind::Closed)
    return false;
  if (state->structuralEntry.kind != X2StructuralEntryState::Kind::None)
    return false;
  if (state->vpContext.kind != X2VpContextState::Kind::None &&
      state->vpContext.kind != X2VpContextState::Kind::Exponent)
    return false;
  return state->structuralVpContext.kind == X2StructuralEntryState::Kind::None ||
         state->structuralVpContext.kind == X2StructuralEntryState::Kind::Exponent;
}

bool x2_state_is_closed_plain_context(const X2ValueDataflowState* state) {
  if (state == nullptr || state->entry.kind != X2EntryState::Kind::Closed)
    return false;
  return state->vpContext.kind == X2VpContextState::Kind::None &&
         state->structuralVpContext.kind == X2StructuralEntryState::Kind::None;
}

bool x2_state_has_same_closed_sign_change_source_in_x_and_x2(const X2ValueDataflowState* state) {
  if (state == nullptr)
    return false;
  if (x2_value_set_has_intersection(&state->x, &state->x2))
    return true;
  if (x2_state_has_same_dot_safe_decimal_in_x_and_x2(state))
    return true;
  const std::optional<X2ShapeSet> left = nr_effective_visible_x_state_shape(state);
  const std::optional<X2ShapeSet> right = nr_effective_x2_state_shape(state);
  if (x2_shape_sets_have_same_restored_display_shape(left.has_value() ? &*left : nullptr,
                                                     right.has_value() ? &*right : nullptr))
    return true;
  return x2_state_has_same_restored_visible_decimal_in_x_and_x2(state);
}

// --- VP shape context / source-match analysis -----------------------------
struct NrVpShapeContext {
  std::string kind = "unknown";
  std::string source = "unknown";
  std::optional<RegisterValueSet> mantissa;
  std::optional<X2ShapeSet> shape;
  std::optional<RegisterValueSet> exponent;
  bool hasExponentDigit = false;
  bool restoresX2 = false;
  bool canDiscardSeparatorBeforeNonDigit = false;
  bool canDiscardSeparatorBeforeSignChange = false;
  bool canDiscardRestoreBeforeFreshDigit = false;
  bool canCancelExponentSignPair = false;
};

bool x2_exponent_set_has_digit(const RegisterValueSet* exponents) {
  if (exponents == nullptr)
    return false;
  for (const std::string& exponent : *exponents) {
    const std::string digits =
        (!exponent.empty() && exponent.front() == '-') ? exponent.substr(1) : exponent;
    if (digits.empty())
      return false;
  }
  return !exponents->empty();
}

NrVpShapeContext nr_empty_vp_shape_context(const std::string& kind) {
  NrVpShapeContext context;
  context.kind = kind;
  context.source = kind;
  return context;
}

NrVpShapeContext analyze_x2_vp_shape_context(const X2ValueDataflowState* state) {
  if (state == nullptr)
    return nr_empty_vp_shape_context("unknown");
  if (state->entry.kind == X2EntryState::Kind::Open) {
    NrVpShapeContext context;
    context.kind = "active-mantissa";
    context.source = "decimal";
    context.mantissa = state->entry.raw;
    return context;
  }
  if (state->entry.kind == X2EntryState::Kind::Exponent) {
    const bool has_digit = x2_exponent_set_has_digit(&state->entry.exponent);
    NrVpShapeContext context;
    context.kind = "active-exponent";
    context.source = "decimal";
    context.mantissa = state->entry.mantissa;
    context.exponent = state->entry.exponent;
    context.hasExponentDigit = has_digit;
    context.restoresX2 = true;
    context.canDiscardSeparatorBeforeNonDigit = has_digit;
    context.canCancelExponentSignPair = true;
    return context;
  }
  if (state->structuralEntry.kind == X2StructuralEntryState::Kind::Exponent) {
    const bool has_digit = x2_exponent_set_has_digit(&state->structuralEntry.exponent);
    NrVpShapeContext context;
    context.kind = "active-structural-exponent";
    context.source = "structural";
    context.shape = state->structuralEntry.mantissa;
    context.exponent = state->structuralEntry.exponent;
    context.hasExponentDigit = has_digit;
    context.restoresX2 = true;
    context.canDiscardSeparatorBeforeNonDigit = has_digit;
    context.canCancelExponentSignPair = true;
    return context;
  }
  if (state->entry.kind == X2EntryState::Kind::Closed &&
      state->vpContext.kind == X2VpContextState::Kind::Exponent) {
    const bool has_digit = x2_exponent_set_has_digit(&state->vpContext.exponent);
    NrVpShapeContext context;
    context.kind = "vp-exponent-context";
    context.source = "decimal";
    context.mantissa = state->vpContext.mantissa;
    context.exponent = state->vpContext.exponent;
    context.hasExponentDigit = has_digit;
    context.restoresX2 = true;
    context.canDiscardSeparatorBeforeSignChange = has_digit;
    context.canDiscardRestoreBeforeFreshDigit = true;
    return context;
  }
  if (state->entry.kind == X2EntryState::Kind::Closed &&
      state->structuralVpContext.kind == X2StructuralEntryState::Kind::Exponent) {
    const bool has_digit = x2_exponent_set_has_digit(&state->structuralVpContext.exponent);
    NrVpShapeContext context;
    context.kind = "vp-structural-exponent-context";
    context.source = "structural";
    context.shape = state->structuralVpContext.mantissa;
    context.exponent = state->structuralVpContext.exponent;
    context.hasExponentDigit = has_digit;
    context.restoresX2 = true;
    context.canDiscardSeparatorBeforeSignChange = has_digit;
    context.canDiscardRestoreBeforeFreshDigit = true;
    return context;
  }
  if (state->entry.kind == X2EntryState::Kind::Closed &&
      state->vpContext.kind == X2VpContextState::Kind::None &&
      state->structuralEntry.kind == X2StructuralEntryState::Kind::None &&
      state->structuralVpContext.kind == X2StructuralEntryState::Kind::None) {
    return nr_empty_vp_shape_context("none");
  }
  return nr_empty_vp_shape_context("unknown");
}

bool nr_same_non_empty_reg_set(const std::optional<RegisterValueSet>& left,
                               const std::optional<RegisterValueSet>& right) {
  return left.has_value() && right.has_value() && !left->empty() && *left == *right;
}

bool nr_same_non_empty_shape_set(const std::optional<X2ShapeSet>& left,
                                 const std::optional<X2ShapeSet>& right) {
  return left.has_value() && right.has_value() && !left->empty() && *left == *right;
}

bool same_x2_exponent_shape_context(const NrVpShapeContext& left, const NrVpShapeContext& right) {
  return left.kind != "none" && left.kind != "unknown" && right.kind != "none" &&
         right.kind != "unknown" && left.source == right.source &&
         left.hasExponentDigit == right.hasExponentDigit &&
         (nr_same_non_empty_reg_set(left.mantissa, right.mantissa) ||
          nr_same_non_empty_shape_set(left.shape, right.shape)) &&
         nr_same_non_empty_reg_set(left.exponent, right.exponent);
}

std::set<std::string> nr_vp_entry_source_keys(const X2ValueDataflowState* state) {
  if (state == nullptr)
    return {};
  const RegisterValueSet* mantissa =
      state->vpEntryMantissa.has_value() ? &*state->vpEntryMantissa : nullptr;
  const X2ShapeSet* shape = state->vpEntryShape.has_value() ? &*state->vpEntryShape : nullptr;
  return tx_vp_source_keys_from_fields(mantissa, shape);
}

// ---------------------------------------------------------------------------
// vp-x2-peephole foundation: visible-X unary no-op proofs and VP-entry source
// matching (faithful port of x2StateHasVisibleUnaryNoop /
// x2StatesHaveSameVpEntrySource and their exact-display sub-helpers).
// ---------------------------------------------------------------------------

bool x2_shape_fact_has_exact_integer_display(const X2ShapeFact& fact) {
  const std::optional<std::string> decimal = x2_shape_fact_restored_visible_decimal(fact);
  static const std::regex integer_pattern(R"(^-?[0-9]+$)");
  if (!decimal.has_value() || !std::regex_match(*decimal, integer_pattern))
    return false;
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
  if (model.kind == X2ShapeModelKind::ExponentEntry && model.mantissa.has_value() &&
      model.mantissa->radix == X2MantissaRadix::Decimal) {
    return model.normalizedDecimal.has_value() && *model.normalizedDecimal == *decimal &&
           model.closedDecimalDisplay.has_value();
  }
  if (model.kind == X2ShapeModelKind::ExponentEntry && model.mantissa.has_value() &&
      (model.mantissa->radix == X2MantissaRadix::Hex ||
       model.mantissa->radix == X2MantissaRadix::Super)) {
    const std::optional<X2ShapeFact> closed = x2_closed_exponent_display_shape_fact(fact);
    return closed.has_value() && x2_shape_fact_has_exact_integer_display(*closed);
  }
  if (model.kind != X2ShapeModelKind::Mantissa || !model.mantissa.has_value())
    return false;
  if (model.mantissa->radix == X2MantissaRadix::Decimal) {
    return model.mantissa->safety == X2ShapeSafety::DotSafeDecimal &&
           model.mantissa->normalizedDecimal.has_value() &&
           *model.mantissa->normalizedDecimal == *decimal &&
           exact_decimal_display_shape_fact(*decimal) == x2_shape_fact_from_data_model(model);
  }
  if (model.mantissa->radix == X2MantissaRadix::Hex ||
      model.mantissa->radix == X2MantissaRadix::Super) {
    return structural_shape_fact_restored_visible_decimal(fact) == decimal &&
           !model.mantissa->hasDecimalPoint;
  }
  return false;
}

bool x2_shape_set_has_exact_integer_display(const std::optional<X2ShapeSet>& input) {
  if (!input.has_value())
    return false;
  for (const X2ShapeFact& fact : *input)
    if (x2_shape_fact_has_exact_integer_display(fact))
      return true;
  return false;
}

bool x2_shape_set_has_exact_non_negative_display(const std::optional<X2ShapeSet>& input) {
  if (!input.has_value())
    return false;
  for (const X2ShapeFact& fact : *input)
    if (x2_shape_fact_exact_non_negative_display_decimal(fact).has_value())
      return true;
  return false;
}

bool is_fractional_noop_visible_decimal(const std::string& value) {
  static const std::regex pattern(R"(^-?0\.[0-9]+$)");
  return value == "0" || std::regex_match(value, pattern);
}

std::optional<X2ShapeSet> effective_visible_x_state_shape(const X2ValueDataflowState* state) {
  if (state == nullptr)
    return std::nullopt;
  return shape_set_with_fallback_value_derived_display_shapes(&state->xShape, &state->x);
}

bool x2_state_has_fractional_noop_visible_x(const X2ValueDataflowState& state) {
  for (const X2ValueFact& fact : state.x) {
    const std::optional<std::string> visible = x2_value_fact_restored_visible_decimal(fact);
    if (visible.has_value() && is_fractional_noop_visible_decimal(*visible))
      return true;
  }
  const std::optional<X2ShapeSet> shape = effective_visible_x_state_shape(&state);
  for (const std::string& visible :
       x2_shape_set_restored_visible_decimals(shape.has_value() ? &*shape : nullptr)) {
    if (is_fractional_noop_visible_decimal(visible))
      return true;
  }
  return false;
}

bool x2_state_has_sign_noop_visible_x(const X2ValueDataflowState& state) {
  const std::optional<X2ShapeSet> shape = effective_visible_x_state_shape(&state);
  for (const std::string& visible :
       x2_shape_set_restored_visible_decimals(shape.has_value() ? &*shape : nullptr)) {
    if (visible == "-1" || visible == "0" || visible == "1")
      return true;
  }
  return false;
}

bool x2_state_has_visible_unary_noop(const X2ValueDataflowState* state, int opcode) {
  if (state == nullptr || !x2_state_is_closed_dot_restore_value_context(state))
    return false;
  switch (opcode) {
    case 0x35:  // К {x} (fraction)
      return x2_state_has_fractional_noop_visible_x(*state);
    case 0x34:  // К [x] (integer)
      return x2_shape_set_has_exact_integer_display(effective_visible_x_state_shape(state));
    case 0x31:  // К |x| (abs)
      return x2_shape_set_has_exact_non_negative_display(effective_visible_x_state_shape(state));
    case 0x32:  // К ЗН (sign)
      return x2_state_has_sign_noop_visible_x(*state);
    default:
      return false;
  }
}

bool x2_states_have_same_vp_entry_source(const X2ValueDataflowState* left,
                                         const X2ValueDataflowState* right) {
  return string_sets_intersect(nr_vp_entry_source_keys(left), nr_vp_entry_source_keys(right));
}

std::optional<X2ShapeSet> nr_vp_entry_sign_source_shapes(const X2ValueDataflowState& input) {
  if (input.vpEntrySignShape.has_value() && !input.vpEntrySignShape->empty())
    return input.vpEntrySignShape;
  const std::optional<X2ShapeSet> explicit_source =
      input.vpEntryShapeTransient ? std::nullopt : input.vpEntryShape;
  if (explicit_source.has_value() && !explicit_source->empty())
    return explicit_source;
  const std::optional<X2ShapeSet> structural =
      shared_structural_shape_facts(&input.x, &input.x2, &input.xShape, &input.x2Shape);
  const std::optional<X2ShapeSet> exact =
      shared_exact_decimal_display_shape_facts(&input.x, &input.x2, &input.xShape, &input.x2Shape);
  return merge_optional_shape_sources(
      {structural.has_value() ? &*structural : nullptr, exact.has_value() ? &*exact : nullptr});
}

std::set<std::string> nr_vp_entry_sign_source_keys(const X2ValueDataflowState* state) {
  if (state == nullptr)
    return {};
  const std::optional<RegisterValueSet> mantissa = vp_entry_sign_source_mantissas(*state);
  const std::optional<X2ShapeSet> shape = nr_vp_entry_sign_source_shapes(*state);
  return tx_vp_source_keys_from_fields(mantissa.has_value() ? &*mantissa : nullptr,
                                       shape.has_value() ? &*shape : nullptr);
}

std::set<std::string> nr_explicit_vp_entry_sign_source_keys(const X2ValueDataflowState* state) {
  if (state == nullptr)
    return {};
  const RegisterValueSet* mantissa =
      state->vpEntrySignMantissa.has_value() ? &*state->vpEntrySignMantissa : nullptr;
  const X2ShapeSet* shape = state->vpEntrySignShape.has_value() ? &*state->vpEntrySignShape : nullptr;
  return tx_vp_source_keys_from_fields(mantissa, shape);
}

bool nr_vp_source_key_is_zero_decimal(const std::string& key) {
  if (key.rfind("decimal:", 0) == 0) {
    const std::string rest = key.substr(std::string("decimal:").size());
    if (rest.find(':') == std::string::npos) {
      const std::optional<std::string> normalized = normalize_decimal_mantissa_entry(rest);
      return normalized.has_value() && *normalized == "0";
    }
    std::string suffix;
    if (rest.size() >= 11U && rest.compare(rest.size() - 11U, 11U, ":normalized") == 0)
      suffix = ":normalized";
    else if (rest.size() >= 13U && rest.compare(rest.size() - 13U, 13U, ":unnormalized") == 0)
      suffix = ":unnormalized";
    if (!suffix.empty()) {
      const std::string middle = rest.substr(0, rest.size() - suffix.size());
      if (middle.find(':') == std::string::npos) {
        const std::optional<std::string> normalized = normalize_plain_decimal(middle);
        return normalized.has_value() && *normalized == "0";
      }
    }
    return false;
  }
  if (key.rfind("shape:", 0) != 0)
    return false;
  const std::string shape_raw = key.substr(std::string("shape:").size());
  for (const X2ShapeFact& fact : x2_restored_display_shape_facts_from_source_key(key)) {
    const std::optional<std::string> visible = x2_shape_fact_restored_visible_decimal(fact);
    if (visible.has_value()) {
      const std::optional<std::string> normalized = normalize_plain_decimal(*visible);
      if (normalized.has_value() && *normalized == "0")
        return true;
    }
    const X2ShapeDataModel model = x2_shape_data_model_for_fact(fact);
    if (model.kind == X2ShapeModelKind::Mantissa && model.mantissa.has_value() &&
        model.mantissa->radix == X2MantissaRadix::Decimal &&
        model.mantissa->normalizedDecimal.has_value() && *model.mantissa->normalizedDecimal == "0")
      return true;
  }
  const X2ShapeDataModel model = x2_shape_data_model_for_fact(shape_raw);
  return model.kind == X2ShapeModelKind::Mantissa && model.mantissa.has_value() &&
         model.mantissa->radix == X2MantissaRadix::Decimal &&
         model.mantissa->normalizedDecimal.has_value() && *model.mantissa->normalizedDecimal == "0";
}

std::set<std::string> nr_non_zero_vp_source_keys(const std::set<std::string>& input) {
  std::set<std::string> output;
  for (const std::string& key : input) {
    if (!nr_vp_source_key_is_zero_decimal(key))
      output.insert(key);
  }
  return output;
}

std::set<std::string> nr_merge_string_sets(const std::set<std::string>& left,
                                           const std::set<std::string>& right) {
  std::set<std::string> output(left);
  output.insert(right.begin(), right.end());
  return output;
}

struct NrVpSourceMatch {
  bool canDiscardRestoreRun = false;
  std::string reason = "source-mismatch";
  std::set<std::string> beforeVpSourceKeys;
  std::set<std::string> beforeRunSignSourceKeys;
  std::set<std::string> beforeVpSignSourceKeys;
};

NrVpSourceMatch analyze_x2_vp_source_match(const X2ValueDataflowState* before_run,
                                           const X2ValueDataflowState* before_vp,
                                           bool has_sign_restore) {
  const NrVpShapeContext context = analyze_x2_vp_shape_context(before_run);
  const NrVpShapeContext before_vp_context = analyze_x2_vp_shape_context(before_vp);
  NrVpSourceMatch match;
  const std::set<std::string> before_run_source = nr_vp_entry_source_keys(before_run);
  match.beforeVpSourceKeys = nr_vp_entry_source_keys(before_vp);
  match.beforeRunSignSourceKeys = nr_vp_entry_sign_source_keys(before_run);
  match.beforeVpSignSourceKeys = nr_vp_entry_sign_source_keys(before_vp);
  const std::set<std::string> before_run_explicit = nr_explicit_vp_entry_sign_source_keys(before_run);
  const std::set<std::string> before_vp_explicit = nr_explicit_vp_entry_sign_source_keys(before_vp);

  auto result = [&](bool can_discard, const char* reason) -> NrVpSourceMatch {
    match.canDiscardRestoreRun = can_discard;
    match.reason = reason;
    return match;
  };

  if (same_x2_exponent_shape_context(context, before_vp_context))
    return result(true, "same-exponent-context");
  if (context.kind == "active-mantissa") {
    const std::set<std::string> active_source =
        tx_vp_source_keys_from_fields(context.mantissa.has_value() ? &*context.mantissa : nullptr,
                                      nullptr);
    if (string_sets_intersect(active_source, match.beforeVpSourceKeys))
      return result(true, "active-mantissa-source");
  } else if (string_sets_intersect(before_run_source, match.beforeVpSourceKeys)) {
    return result(true, "entry-source");
  }
  if (has_sign_restore) {
    const std::set<std::string> right_sign_keys =
        nr_merge_string_sets(match.beforeVpSourceKeys, match.beforeVpSignSourceKeys);
    if (string_sets_intersect(nr_non_zero_vp_source_keys(match.beforeRunSignSourceKeys),
                              nr_non_zero_vp_source_keys(right_sign_keys)))
      return result(true, "nonzero-sign-source");
    const std::set<std::string> right_explicit_keys =
        nr_merge_string_sets(match.beforeVpSourceKeys, before_vp_explicit);
    if (string_sets_intersect(before_run_explicit, right_explicit_keys))
      return result(true, "explicit-sign-source");
  }
  return result(false, "source-mismatch");
}

struct NrSignRestoreProof {
  bool canDiscard = false;
  std::string reason = "no-sign-restore-source";
};

NrSignRestoreProof x2_vp_sign_restore_source_proof(const NrVpSourceMatch& source_match,
                                                   bool transition_can_discard_sign_pair,
                                                   bool has_sign_restore, bool has_vp) {
  if (transition_can_discard_sign_pair)
    return {true, "shape-transition"};
  if (!has_sign_restore || !has_vp)
    return {false, "no-sign-restore-source"};
  if (source_match.reason == "nonzero-sign-source")
    return {true, "source-match-nonzero-sign"};
  if (source_match.reason == "explicit-sign-source")
    return {true, "source-match-explicit-sign"};
  const std::set<std::string> sign_targets =
      nr_merge_string_sets(source_match.beforeVpSourceKeys, source_match.beforeVpSignSourceKeys);
  if (string_sets_intersect(nr_non_zero_vp_source_keys(source_match.beforeRunSignSourceKeys),
                            nr_non_zero_vp_source_keys(sign_targets)))
    return {true, "shared-sign-source"};
  return {false, "no-sign-restore-source"};
}

// --- restore-gap scans ----------------------------------------------------
struct NrRestoreGapBeforeVp {
  std::optional<int> vpIndex;
  std::optional<int> blockedIndex;
  bool sawRestoreGap = false;
  bool sawSignRestore = false;
};

NrRestoreGapBeforeVp x2_restore_gap_before_vp(const std::vector<IrOp>& ops, int start,
                                              const DirectReturnAnalysisContext* context) {
  bool saw_restore_gap = false;
  bool saw_sign_restore = false;
  for (int index = start; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label || op.kind == IrKind::OrphanAddress)
      continue;
    if (context != nullptr && is_known_return_call_op(op) &&
        x2_restore_gap_direct_return_does_not_observe_restore(ops, op, *context))
      continue;
    if (is_free_standing_x2_restore_gap_op(op)) {
      saw_restore_gap = true;
      if (is_free_standing_x2_sign_change_op(op))
        saw_sign_restore = true;
      continue;
    }
    if (is_free_standing_x2_vp_op(op))
      return {index, std::nullopt, saw_restore_gap, saw_sign_restore};
    return {std::nullopt, index, saw_restore_gap, saw_sign_restore};
  }
  return {std::nullopt, std::nullopt, saw_restore_gap, saw_sign_restore};
}

struct NrRestoreRunBeforeIndex {
  std::optional<int> blockedIndex;
  std::vector<int> removableIndexes;
  bool sawSignRestore = false;
};

NrRestoreRunBeforeIndex x2_restore_run_before_index(const std::vector<IrOp>& ops, int terminal_index,
                                                    const DirectReturnAnalysisContext* context,
                                                    bool include_sign_restores = true) {
  std::vector<int> removable_indexes;
  bool saw_sign_restore = false;
  for (int index = terminal_index - 1; index >= 0; --index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label || op.kind == IrKind::OrphanAddress)
      continue;
    if (is_free_standing_x2_empty_op(op)) {
      removable_indexes.push_back(index);
      continue;
    }
    if (include_sign_restores && is_free_standing_x2_sign_change_op(op)) {
      removable_indexes.push_back(index);
      saw_sign_restore = true;
      continue;
    }
    if (context != nullptr && is_known_return_call_op(op) &&
        x2_restore_gap_direct_return_does_not_observe_restore(ops, op, *context))
      continue;
    std::reverse(removable_indexes.begin(), removable_indexes.end());
    return {index, removable_indexes, saw_sign_restore};
  }
  std::reverse(removable_indexes.begin(), removable_indexes.end());
  return {std::nullopt, removable_indexes, saw_sign_restore};
}

std::optional<int> x2_previous_free_standing_restore_executable_index(const std::vector<IrOp>& ops,
                                                                      int index,
                                                                      bool skip_empty_restores) {
  for (int cursor = index - 1; cursor >= 0; --cursor) {
    const IrOp& op = ops.at(static_cast<std::size_t>(cursor));
    if (op.kind == IrKind::Label || op.kind == IrKind::OrphanAddress)
      continue;
    if (skip_empty_restores && is_free_standing_x2_empty_op(op))
      continue;
    if (is_free_standing_x2_restore_gap_op(op))
      return cursor;
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<int> x2_previous_normalized_decimal_restore_gap_sync_index(
    const std::vector<IrOp>& ops, int index, const DirectReturnAnalysisContext* context) {
  for (int cursor = index - 1; cursor >= 0; --cursor) {
    const IrOp& op = ops.at(static_cast<std::size_t>(cursor));
    if (op.kind == IrKind::Label)
      continue;
    if (has_rewrite_barrier(op) || is_display_focus_sensitive(op))
      return std::nullopt;
    switch (op.kind) {
    case IrKind::Plain: {
      const X2Effect effect = plain_x2_effect(op);
      if (effect == X2Effect::Affects || effect == X2Effect::Restores)
        return cursor;
      if (effect == X2Effect::Preserves)
        continue;
      return std::nullopt;
    }
    case IrKind::Recall:
    case IrKind::IndirectRecall:
      return cursor;
    case IrKind::Store:
    case IrKind::IndirectStore:
    case IrKind::OrphanAddress:
    case IrKind::CondJump:
    case IrKind::Loop:
      continue;
    case IrKind::IndirectCondJump:
      if (known_indirect_flow_target(op).has_value())
        continue;
      return std::nullopt;
    case IrKind::Call:
    case IrKind::IndirectCall:
      if (context != nullptr && is_known_return_call_op(op) &&
          x2_restore_gap_direct_return_does_not_observe_restore(ops, op, *context))
        continue;
      return std::nullopt;
    case IrKind::Label:
    case IrKind::Jump:
    case IrKind::IndirectJump:
    case IrKind::Stop:
    case IrKind::Return:
      return std::nullopt;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

bool x2_normalized_decimal_restore_gap_is_free_standing(const std::vector<IrOp>& ops, int index,
                                                        const DirectReturnAnalysisContext* context) {
  return x2_previous_normalized_decimal_restore_gap_sync_index(ops, index, context).has_value();
}

// --- analyzeX2VpRestoreGapSource + dot-replacement planner -----------------
struct NrVpRestoreGapSource {
  bool hasOnlyRestoreGapBeforeVp = false;
  bool replacementDotHasOnlyRestoreGapBeforeVp = false;
  bool hasSignRestoreGapBeforeVp = false;
  bool canDiscardRestoreRunBeforeProvedVp = false;
  bool canDiscardShapeSignPairBeforeProvedVp = false;
  bool signRestoreProofCanDiscard = false;
  std::string signRestoreProofReason = "no-sign-restore-source";
  std::string sourceMatchReason = "source-mismatch";
  bool hasSameExplicitVpEntrySignSource = false;
};

// Faithful port of X2VpRestoreGapSourceOptions: mirrors TS option resolution so
// the planner cluster can request scanned-vs-leading sign-restore semantics.
struct NrVpRestoreGapSourceOptions {
  bool useScannedSignRestores = true;
  bool includesLeadingSignRestore = false;
};

NrVpRestoreGapSource analyze_x2_vp_restore_gap_source(
    const std::vector<IrOp>& ops, int start, const X2ValueDataflowState* before_run,
    const X2ValueDataflowState* before_vp, const DirectReturnAnalysisContext* context,
    const NrVpRestoreGapSourceOptions& options = {}) {
  const NrRestoreGapBeforeVp scan = x2_restore_gap_before_vp(ops, start, context);
  const bool has_sign_restore = (options.useScannedSignRestores && scan.sawSignRestore) ||
                                options.includesLeadingSignRestore;
  const NrVpSourceMatch source_match = analyze_x2_vp_source_match(before_run, before_vp, has_sign_restore);
  const bool transition_can_discard_restore_run =
      before_vp != nullptr ? source_match.canDiscardRestoreRun : false;
  const bool transition_can_discard_sign_pair =
      transition_can_discard_restore_run && has_sign_restore;
  const NrSignRestoreProof proof = x2_vp_sign_restore_source_proof(
      source_match, transition_can_discard_sign_pair, has_sign_restore, scan.vpIndex.has_value());

  NrVpRestoreGapSource out;
  out.hasOnlyRestoreGapBeforeVp = scan.sawRestoreGap && scan.vpIndex.has_value();
  out.replacementDotHasOnlyRestoreGapBeforeVp = scan.vpIndex.has_value();
  out.hasSignRestoreGapBeforeVp = has_sign_restore && scan.vpIndex.has_value();
  out.canDiscardRestoreRunBeforeProvedVp = transition_can_discard_restore_run;
  out.canDiscardShapeSignPairBeforeProvedVp = transition_can_discard_sign_pair;
  out.signRestoreProofCanDiscard = proof.canDiscard;
  out.signRestoreProofReason = proof.reason;
  out.sourceMatchReason = source_match.reason;
  out.hasSameExplicitVpEntrySignSource =
      string_sets_intersect(nr_explicit_vp_entry_sign_source_keys(before_run),
                            nr_explicit_vp_entry_sign_source_keys(before_vp));
  return out;
}

struct NrDotReplacementPlan {
  bool preservesVpEntrySource = false;
  std::string reason;
  std::optional<int> previousSignSourceIndex;
  NrVpRestoreGapSource source;
};

NrDotReplacementPlan x2_plan_dot_replacement_vp_source(
    const std::vector<IrOp>& ops, int dot_index, const X2ValueDataflowState* state_before_dot,
    const X2ValueDataflowState* state_after_dot, const DirectReturnAnalysisContext* context,
    const NrVpRestoreGapSourceOptions& options = {}) {
  const NrVpRestoreGapSource source = analyze_x2_vp_restore_gap_source(
      ops, dot_index + 1, state_before_dot, state_after_dot, context, options);
  if (source.hasOnlyRestoreGapBeforeVp) {
    if (source.canDiscardRestoreRunBeforeProvedVp)
      return {true, "restore-gap-source", std::nullopt, source};
    if (source.signRestoreProofCanDiscard)
      return {true, "sign-restore-gap-source", std::nullopt, source};
    return {false, "source-mismatch", std::nullopt, source};
  }
  if (!source.replacementDotHasOnlyRestoreGapBeforeVp)
    return {false, "no-vp-restore", std::nullopt, source};
  const std::optional<int> previous_sign_source_index =
      x2_previous_free_standing_restore_executable_index(ops, dot_index, true);
  if (previous_sign_source_index.has_value() &&
      is_free_standing_x2_sign_change_op(ops.at(static_cast<std::size_t>(*previous_sign_source_index))))
    return {false, "previous-sign-source", previous_sign_source_index, source};
  if (source.canDiscardRestoreRunBeforeProvedVp && source.hasSameExplicitVpEntrySignSource)
    return {true, "replacement-dot-explicit-sign-source", std::nullopt, source};
  return {false, "source-mismatch", std::nullopt, source};
}

// ==========================================================================
// vp-splice candidate planner cluster (faithful port of helpers.ts
// x2PlanVpSpliceCandidatesAt + its sub-planners). These live in the x2eval
// detail namespace alongside the shape/source-match foundation they reuse;
// the public mkpro::core::passes::x2_plan_vp_splice_candidates_at forwards here.
// ==========================================================================

const X2ValueDataflowState* nr_state_ptr(
    const std::vector<std::optional<X2ValueDataflowState>>& states, int index) {
  if (index < 0 || index >= static_cast<int>(states.size()))
    return nullptr;
  const std::optional<X2ValueDataflowState>& entry = states.at(static_cast<std::size_t>(index));
  return entry.has_value() ? &*entry : nullptr;
}

// --- analyzeX2VpShapeTransition (non-proved-vp operations) -----------------
struct NrVpShapeTransition {
  bool canDiscardCurrentOp = false;
  bool canDiscardRestoreRun = false;
  bool canDiscardSignPair = false;
};

NrVpShapeTransition analyze_x2_vp_shape_transition(const X2ValueDataflowState* state,
                                                   const std::string& operation) {
  const NrVpShapeContext context = analyze_x2_vp_shape_context(state);
  NrVpShapeTransition out;
  if (operation == "vp") {
    out.canDiscardCurrentOp = context.kind == "active-mantissa" ||
                              context.kind == "active-exponent" ||
                              context.kind == "active-structural-exponent";
  } else if (operation == "empty-before-non-digit") {
    out.canDiscardCurrentOp = context.canDiscardSeparatorBeforeNonDigit;
  } else if (operation == "empty-before-sign-change") {
    out.canDiscardCurrentOp =
        context.canDiscardSeparatorBeforeSignChange || context.canDiscardSeparatorBeforeNonDigit;
  } else if (operation == "fresh-digit") {
    out.canDiscardRestoreRun = context.canDiscardRestoreBeforeFreshDigit;
  } else if (operation == "hard-overwrite") {
    out.canDiscardRestoreRun = context.restoresX2;
  } else if (operation == "sign-pair") {
    out.canDiscardSignPair = context.canCancelExponentSignPair;
  }
  return out;
}

bool x2_can_remove_second_vp_after_previous_vp(const X2ValueDataflowState* state_before_previous_vp) {
  return analyze_x2_vp_shape_transition(state_before_previous_vp, "vp").canDiscardCurrentOp;
}

bool x2_transparent_vp_gap_op(const IrOp& op) {
  return op.kind == IrKind::Label || op.kind == IrKind::OrphanAddress;
}

// --- generic restore-run-before-terminal scanner --------------------------
struct NrRestoreRunBeforeTerminalScan {
  std::optional<int> terminalIndex;
  std::optional<int> blockedIndex;
  std::vector<int> removableIndexes;
  bool sawRestoreGap = false;
  bool sawSignRestore = false;
};

using NrRestoreRunClassifier = std::function<std::string(const IrOp&, int)>;
using NrRestoreRunTerminalPredicate = std::function<bool(const IrOp&, int)>;

NrRestoreRunBeforeTerminalScan x2_scan_restore_run_before_terminal(
    const std::vector<IrOp>& ops, int start, const DirectReturnAnalysisContext* context,
    const NrRestoreRunClassifier& classify, bool allow_empty_run_terminal = false) {
  std::vector<int> removable_indexes;
  bool saw_restore_gap = false;
  bool saw_sign_restore = false;
  for (int index = start; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label || op.kind == IrKind::OrphanAddress)
      continue;
    if (context != nullptr && is_known_return_call_op(op) &&
        x2_restore_gap_direct_return_does_not_observe_restore(ops, op, *context))
      continue;
    const std::string decision = classify(op, index);
    if (decision == "remove") {
      removable_indexes.push_back(index);
      saw_restore_gap = true;
      if (is_free_standing_x2_sign_change_op(op))
        saw_sign_restore = true;
      continue;
    }
    if (decision == "transparent")
      continue;
    if (decision == "terminal" && (!removable_indexes.empty() || allow_empty_run_terminal)) {
      return {index, std::nullopt, removable_indexes, saw_restore_gap, saw_sign_restore};
    }
    return {std::nullopt, index, {}, saw_restore_gap, saw_sign_restore};
  }
  return {std::nullopt, std::nullopt, {}, saw_restore_gap, saw_sign_restore};
}

NrRestoreRunBeforeTerminalScan x2_restore_run_before_terminal(
    const std::vector<IrOp>& ops, int start, const DirectReturnAnalysisContext* context,
    const NrRestoreRunTerminalPredicate& is_terminal) {
  return x2_scan_restore_run_before_terminal(
      ops, start, context, [&](const IrOp& op, int index) -> std::string {
        if (is_free_standing_x2_restore_gap_op(op))
          return "remove";
        return is_terminal(op, index) ? "terminal" : "block";
      });
}

// --- planner option / candidate structs -----------------------------------
struct NrTerminalRestoreRunPlan {
  std::vector<int> removableIndexes;
  std::string reason;
};

struct NrTerminalRestoreSplicePlan {
  std::vector<int> removableIndexes;
  std::string reason;
};

struct NrProvedVpRestoreRunPlan {
  std::vector<int> removableIndexes;
  bool hasSource = false;
  NrVpRestoreGapSource source;
  std::string reason;
};

struct NrProvedVpSplicePlan {
  std::vector<int> removableIndexes;
  std::string reason;
  bool hasRestoreRunPlan = false;
  NrProvedVpRestoreRunPlan restoreRunPlan;
};

struct NrEmptyRunBeforeProvedVpPlan {
  std::vector<int> removableIndexes;
  std::string reason;
};

struct NrAdjacentVpBoundaryPlan {
  std::vector<int> removableIndexes;
  std::string reason;
};

struct NrExponentSeparatorRunPlan {
  std::vector<int> removableIndexes;
  bool hasTransition = false;
};

struct NrAdjacentSignPairPlan {
  std::vector<int> removableIndexes;
  std::string reason;
  bool hasSourcePlan = false;
  NrDotReplacementPlan sourcePlan;
};

// --- x2PlanRestoreRunBeforeTerminal ---------------------------------------
NrTerminalRestoreRunPlan x2_plan_restore_run_before_terminal(
    const std::vector<IrOp>& ops, int start_index, const X2ValueDataflowState* state,
    const DirectReturnAnalysisContext* context, const std::string& operation,
    const NrRestoreRunTerminalPredicate& is_terminal) {
  const NrVpShapeTransition transition = analyze_x2_vp_shape_transition(state, operation);
  std::string reason = "vp-context-overwritten";
  std::optional<int> previous_restore_index;
  bool can_scan = transition.canDiscardRestoreRun;

  if (!can_scan && (operation == "fresh-digit" || operation == "hard-overwrite")) {
    previous_restore_index = x2_previous_free_standing_restore_executable_index(ops, start_index, false);
    if (!x2_state_is_closed_plain_context(state)) {
      reason = "transition-blocked";
    } else if (previous_restore_index.has_value()) {
      reason = "previous-restore-source";
    } else {
      can_scan = true;
      reason = operation == "fresh-digit" ? "closed-context-fresh-digit"
                                          : "closed-context-hard-overwrite";
    }
  } else if (!can_scan) {
    reason = "transition-blocked";
  }

  if (!can_scan)
    return {{}, reason};

  const NrRestoreRunBeforeTerminalScan scan =
      x2_restore_run_before_terminal(ops, start_index, context, is_terminal);
  if (!scan.terminalIndex.has_value())
    return {{}, "terminal-missing"};
  return {scan.removableIndexes, reason};
}

// --- x2PlanTerminalRestoreSpliceAt ----------------------------------------
NrTerminalRestoreSplicePlan x2_plan_terminal_restore_splice_at(
    const std::vector<IrOp>& ops, int start_index,
    const std::vector<std::optional<X2ValueDataflowState>>& states,
    const DirectReturnAnalysisContext* context, const X2VpSplicePlannerOptions& planner_options,
    bool include_hard_overwrite, bool include_fresh_digit) {
  if (start_index < 0 || start_index >= static_cast<int>(ops.size()))
    return {{}, "not-restore-start"};
  const IrOp& cur = ops.at(static_cast<std::size_t>(start_index));
  if (!is_free_standing_x2_empty_op(cur) && !is_free_standing_x2_sign_change_op(cur))
    return {{}, "not-restore-start"};

  if (include_hard_overwrite) {
    const NrTerminalRestoreRunPlan hard_overwrite_plan = x2_plan_restore_run_before_terminal(
        ops, start_index, nr_state_ptr(states, start_index), context, "hard-overwrite",
        planner_options.is_hard_x2_overwrite_without_stack_use);
    if (!hard_overwrite_plan.removableIndexes.empty())
      return {hard_overwrite_plan.removableIndexes, "hard-overwrite-restore-run"};
  }

  if (include_fresh_digit) {
    const NrTerminalRestoreRunPlan fresh_digit_plan = x2_plan_restore_run_before_terminal(
        ops, start_index, nr_state_ptr(states, start_index), context, "fresh-digit",
        planner_options.is_decimal_digit);
    if (!fresh_digit_plan.removableIndexes.empty())
      return {fresh_digit_plan.removableIndexes, "fresh-digit-restore-run"};
  }

  return {{}, "no-terminal-splice"};
}

// --- x2PlanRestoreRunBeforeProvedVp ---------------------------------------
NrProvedVpRestoreRunPlan x2_plan_restore_run_before_proved_vp(
    const std::vector<IrOp>& ops, int vp_index,
    const std::vector<std::optional<X2ValueDataflowState>>& states,
    const DirectReturnAnalysisContext* context) {
  const NrRestoreRunBeforeIndex scan = x2_restore_run_before_index(ops, vp_index, context);
  if (scan.removableIndexes.empty())
    return {{}, false, {}, "no-restore-run"};
  if (!scan.sawSignRestore)
    return {{}, false, {}, "no-sign-restore"};
  const int first_run_index = scan.removableIndexes.front();
  const X2ValueDataflowState* before_run = nr_state_ptr(states, first_run_index);
  NrVpRestoreGapSourceOptions options;
  options.useScannedSignRestores = !x2_state_is_closed_plain_context(before_run);
  const NrVpRestoreGapSource source = analyze_x2_vp_restore_gap_source(
      ops, first_run_index, before_run, nr_state_ptr(states, vp_index), context, options);
  if (!source.canDiscardRestoreRunBeforeProvedVp)
    return {{}, true, source, "source-mismatch"};
  return {scan.removableIndexes, true, source, "proved-vp-source"};
}

// --- x2PlanEmptyRunBeforeProvedVp -----------------------------------------
NrEmptyRunBeforeProvedVpPlan x2_plan_empty_run_before_proved_vp(
    const std::vector<IrOp>& ops, int index,
    const std::vector<std::optional<X2ValueDataflowState>>& states,
    const DirectReturnAnalysisContext* context) {
  const NrRestoreRunBeforeIndex scan =
      x2_restore_run_before_index(ops, index, context, /*include_sign_restores=*/false);
  const std::vector<int>& empty_run_indexes = scan.removableIndexes;
  if (empty_run_indexes.empty())
    return {{}, "no-empty-run"};
  const int first_empty_index = empty_run_indexes.front();
  const bool removes_vp =
      first_empty_index == index - static_cast<int>(empty_run_indexes.size()) &&
      first_empty_index > 0 &&
      is_free_standing_x2_vp_op(ops.at(static_cast<std::size_t>(first_empty_index - 1))) &&
      x2_can_remove_second_vp_after_previous_vp(nr_state_ptr(states, first_empty_index - 1));
  std::vector<int> removable = empty_run_indexes;
  if (removes_vp)
    removable.push_back(index);
  return {removable, removes_vp ? "duplicate-vp-after-empty-run" : "empty-run"};
}

// --- x2PlanProvedVpSpliceAt -----------------------------------------------
NrProvedVpSplicePlan x2_plan_proved_vp_splice_at(
    const std::vector<IrOp>& ops, int vp_index,
    const std::vector<std::optional<X2ValueDataflowState>>& states,
    const DirectReturnAnalysisContext* context) {
  if (vp_index < 0 || vp_index >= static_cast<int>(ops.size()) ||
      !is_free_standing_x2_vp_op(ops.at(static_cast<std::size_t>(vp_index))))
    return {{}, "not-vp", false, {}};

  const NrProvedVpRestoreRunPlan restore_run_plan =
      x2_plan_restore_run_before_proved_vp(ops, vp_index, states, context);
  if (!restore_run_plan.removableIndexes.empty())
    return {restore_run_plan.removableIndexes, "proved-vp-restore-run", true, restore_run_plan};

  const NrEmptyRunBeforeProvedVpPlan empty_run_plan =
      x2_plan_empty_run_before_proved_vp(ops, vp_index, states, context);
  if (!empty_run_plan.removableIndexes.empty())
    return {empty_run_plan.removableIndexes, "empty-run-before-proved-vp", true, restore_run_plan};

  return {{}, "no-proved-vp-splice", true, restore_run_plan};
}

// --- x2PlanExponentSeparatorRun -------------------------------------------
NrExponentSeparatorRunPlan x2_plan_exponent_separator_run(const std::vector<IrOp>& ops,
                                                          int start_index,
                                                          const X2ValueDataflowState* state,
                                                          const X2VpSplicePlannerOptions& options) {
  const NrVpShapeTransition before_non_digit =
      analyze_x2_vp_shape_transition(state, "empty-before-non-digit");
  const NrVpShapeTransition before_sign_change =
      analyze_x2_vp_shape_transition(state, "empty-before-sign-change");
  if (!before_non_digit.canDiscardCurrentOp && !before_sign_change.canDiscardCurrentOp)
    return {{}, /*hasTransition=*/true};

  std::vector<int> run;
  for (int index = start_index; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (x2_transparent_vp_gap_op(op))
      continue;
    if (is_free_standing_x2_empty_op(op)) {
      run.push_back(index);
      continue;
    }
    if (run.empty())
      return {{}, false};
    if (options.is_decimal_digit(op, index))
      return {{}, true};
    if (is_free_standing_x2_sign_change_op(op))
      return {before_sign_change.canDiscardCurrentOp ? run : std::vector<int>{}, true};
    return {before_non_digit.canDiscardCurrentOp ? run : std::vector<int>{}, true};
  }
  return {{}, false};
}

// --- x2PlanAdjacentVpBoundaryAt -------------------------------------------
NrAdjacentVpBoundaryPlan x2_plan_adjacent_vp_boundary_at(
    const std::vector<IrOp>& ops, int index,
    const std::vector<std::optional<X2ValueDataflowState>>& states,
    const X2VpSplicePlannerOptions& planner_options, bool include_duplicate_vp,
    bool include_exponent_separator, bool include_empty_before_sign_change) {
  if (index <= 0 || index >= static_cast<int>(ops.size()))
    return {{}, "not-boundary"};
  const IrOp& prev = ops.at(static_cast<std::size_t>(index - 1));
  const IrOp& cur = ops.at(static_cast<std::size_t>(index));

  if (include_duplicate_vp && is_free_standing_x2_vp_op(prev) && is_free_standing_x2_vp_op(cur)) {
    const NrVpShapeTransition transition =
        analyze_x2_vp_shape_transition(nr_state_ptr(states, index - 1), "vp");
    if (transition.canDiscardCurrentOp)
      return {{index}, "duplicate-vp"};
    return {{}, "no-boundary-proof"};
  }

  if (include_exponent_separator && is_free_standing_x2_empty_op(cur)) {
    const NrExponentSeparatorRunPlan separator_run =
        x2_plan_exponent_separator_run(ops, index, nr_state_ptr(states, index), planner_options);
    if (!separator_run.removableIndexes.empty() && separator_run.hasTransition)
      return {separator_run.removableIndexes, "exponent-separator"};
    if (separator_run.hasTransition)
      return {{}, "no-boundary-proof"};
  }

  if (include_empty_before_sign_change && is_free_standing_x2_empty_op(prev) &&
      is_free_standing_x2_sign_change_op(cur)) {
    const NrVpShapeTransition transition =
        analyze_x2_vp_shape_transition(nr_state_ptr(states, index - 1), "empty-before-sign-change");
    if (transition.canDiscardCurrentOp)
      return {{index - 1}, "exponent-empty-before-sign"};
    return {{}, "no-boundary-proof"};
  }

  return {{}, "not-boundary"};
}

// --- x2PlanAdjacentSignPairAt ---------------------------------------------
NrAdjacentSignPairPlan x2_plan_adjacent_sign_pair_at(
    const std::vector<IrOp>& ops, int second_sign_index,
    const std::vector<std::optional<X2ValueDataflowState>>& states,
    const DirectReturnAnalysisContext* context, bool include_exponent_sign_pair,
    bool include_open_mantissa_before_proved_vp, bool include_closed_context) {
  if (second_sign_index <= 0 || second_sign_index >= static_cast<int>(ops.size()))
    return {{}, "not-sign-pair", false, {}};
  const IrOp& prev = ops.at(static_cast<std::size_t>(second_sign_index - 1));
  const IrOp& cur = ops.at(static_cast<std::size_t>(second_sign_index));
  if (!is_free_standing_x2_sign_change_op(prev) || !is_free_standing_x2_sign_change_op(cur))
    return {{}, "not-sign-pair", false, {}};

  const X2ValueDataflowState* state = nr_state_ptr(states, second_sign_index - 1);
  const X2ValueDataflowState* state_after_pair = nr_state_ptr(states, second_sign_index + 1);
  const NrVpShapeTransition transition = analyze_x2_vp_shape_transition(state, "sign-pair");
  if (include_exponent_sign_pair && transition.canDiscardSignPair)
    return {{second_sign_index - 1, second_sign_index}, "exponent-sign-pair", false, {}};

  if (include_open_mantissa_before_proved_vp &&
      analyze_x2_vp_shape_context(state).kind == "active-mantissa") {
    NrVpRestoreGapSourceOptions options;
    options.includesLeadingSignRestore = true;
    const NrDotReplacementPlan source_plan = x2_plan_dot_replacement_vp_source(
        ops, second_sign_index, state, state_after_pair, context, options);
    if (source_plan.source.replacementDotHasOnlyRestoreGapBeforeVp &&
        source_plan.source.canDiscardShapeSignPairBeforeProvedVp)
      return {{second_sign_index - 1, second_sign_index},
              "open-mantissa-sign-pair-before-proved-vp", true, source_plan};
    return {{}, "source-mismatch", true, source_plan};
  }

  if (include_closed_context && x2_state_is_closed_plain_context(state)) {
    if (state == nullptr || !x2_state_has_same_closed_sign_change_source_in_x_and_x2(state))
      return {{}, "no-shared-closed-source", false, {}};
    if (!removing_recall_can_expose_x2_restore(ops, second_sign_index))
      return {{second_sign_index - 1, second_sign_index}, "closed-context-sign-pair", false, {}};
    const NrDotReplacementPlan source_plan = x2_plan_dot_replacement_vp_source(
        ops, second_sign_index, state, state_after_pair, context);
    if (source_plan.source.replacementDotHasOnlyRestoreGapBeforeVp &&
        source_plan.source.canDiscardRestoreRunBeforeProvedVp)
      return {{second_sign_index - 1, second_sign_index}, "closed-context-sign-pair", true,
              source_plan};
    return {{}, "source-mismatch", true, source_plan};
  }

  return {{}, "no-sign-pair-proof", false, {}};
}

// --- candidate reason extraction ------------------------------------------
std::optional<std::string> nr_candidate_source_match_reason(const NrProvedVpSplicePlan* proved_vp,
                                                            const NrAdjacentSignPairPlan* sign_pair) {
  if (proved_vp != nullptr && proved_vp->hasRestoreRunPlan && proved_vp->restoreRunPlan.hasSource)
    return proved_vp->restoreRunPlan.source.sourceMatchReason;
  if (sign_pair != nullptr && sign_pair->hasSourcePlan)
    return sign_pair->sourcePlan.source.sourceMatchReason;
  return std::nullopt;
}

std::optional<std::string> nr_candidate_sign_restore_proof_reason(
    const NrProvedVpSplicePlan* proved_vp, const NrAdjacentSignPairPlan* sign_pair) {
  if (proved_vp != nullptr && proved_vp->hasRestoreRunPlan && proved_vp->restoreRunPlan.hasSource)
    return proved_vp->restoreRunPlan.source.signRestoreProofReason;
  if (sign_pair != nullptr && sign_pair->hasSourcePlan)
    return sign_pair->sourcePlan.source.signRestoreProofReason;
  return std::nullopt;
}

X2VpSpliceCandidate nr_make_candidate(const std::string& stage, std::vector<int> removable,
                                      const NrProvedVpSplicePlan* proved_vp,
                                      const NrAdjacentSignPairPlan* sign_pair) {
  X2VpSpliceCandidate candidate;
  candidate.stage = stage;
  candidate.removable_indexes = std::move(removable);
  candidate.source_match_reason = nr_candidate_source_match_reason(proved_vp, sign_pair);
  candidate.sign_restore_source_proof_reason =
      nr_candidate_sign_restore_proof_reason(proved_vp, sign_pair);
  return candidate;
}

// Faithful port of x2PlanVpSpliceCandidatesAt orchestrator.
std::vector<X2VpSpliceCandidate> plan_vp_splice_candidates(
    const std::vector<IrOp>& ops, int index,
    const std::vector<std::optional<X2ValueDataflowState>>& states,
    const DirectReturnAnalysisContext& context, const X2VpSplicePlannerOptions& options) {
  if (index <= 0 || index >= static_cast<int>(ops.size()))
    return {};
  std::vector<X2VpSpliceCandidate> candidates;

  const NrAdjacentVpBoundaryPlan duplicate_vp = x2_plan_adjacent_vp_boundary_at(
      ops, index, states, options, /*include_duplicate_vp=*/true,
      /*include_exponent_separator=*/false, /*include_empty_before_sign_change=*/false);
  candidates.push_back(nr_make_candidate(
      "duplicate-vp",
      duplicate_vp.reason == "duplicate-vp" ? duplicate_vp.removableIndexes : std::vector<int>{},
      nullptr, nullptr));

  const NrProvedVpSplicePlan proved_vp = x2_plan_proved_vp_splice_at(ops, index, states, &context);
  candidates.push_back(nr_make_candidate(
      "proved-vp",
      (proved_vp.reason == "proved-vp-restore-run" ||
       proved_vp.reason == "empty-run-before-proved-vp")
          ? proved_vp.removableIndexes
          : std::vector<int>{},
      &proved_vp, nullptr));

  const NrAdjacentVpBoundaryPlan exponent_boundary = x2_plan_adjacent_vp_boundary_at(
      ops, index, states, options, /*include_duplicate_vp=*/false,
      /*include_exponent_separator=*/true, /*include_empty_before_sign_change=*/true);
  candidates.push_back(nr_make_candidate(
      "exponent-boundary",
      (exponent_boundary.reason == "exponent-separator" ||
       exponent_boundary.reason == "exponent-empty-before-sign")
          ? exponent_boundary.removableIndexes
          : std::vector<int>{},
      nullptr, nullptr));

  const NrTerminalRestoreSplicePlan hard_overwrite_terminal = x2_plan_terminal_restore_splice_at(
      ops, index, states, &context, options, /*include_hard_overwrite=*/true,
      /*include_fresh_digit=*/false);
  candidates.push_back(nr_make_candidate(
      "hard-overwrite-terminal",
      hard_overwrite_terminal.reason == "hard-overwrite-restore-run"
          ? hard_overwrite_terminal.removableIndexes
          : std::vector<int>{},
      nullptr, nullptr));

  const NrAdjacentSignPairPlan sign_pair_before_fresh_digit = x2_plan_adjacent_sign_pair_at(
      ops, index, states, &context, /*include_exponent_sign_pair=*/true,
      /*include_open_mantissa_before_proved_vp=*/true, /*include_closed_context=*/false);
  candidates.push_back(nr_make_candidate(
      "sign-pair-before-fresh-digit",
      (sign_pair_before_fresh_digit.reason == "exponent-sign-pair" ||
       sign_pair_before_fresh_digit.reason == "open-mantissa-sign-pair-before-proved-vp")
          ? sign_pair_before_fresh_digit.removableIndexes
          : std::vector<int>{},
      nullptr, &sign_pair_before_fresh_digit));

  const NrTerminalRestoreSplicePlan fresh_digit_terminal = x2_plan_terminal_restore_splice_at(
      ops, index, states, &context, options, /*include_hard_overwrite=*/false,
      /*include_fresh_digit=*/true);
  candidates.push_back(nr_make_candidate(
      "fresh-digit-terminal",
      fresh_digit_terminal.reason == "fresh-digit-restore-run"
          ? fresh_digit_terminal.removableIndexes
          : std::vector<int>{},
      nullptr, nullptr));

  const NrAdjacentSignPairPlan closed_sign_pair = x2_plan_adjacent_sign_pair_at(
      ops, index, states, &context, /*include_exponent_sign_pair=*/false,
      /*include_open_mantissa_before_proved_vp=*/false, /*include_closed_context=*/true);
  candidates.push_back(nr_make_candidate(
      "closed-sign-pair",
      closed_sign_pair.reason == "closed-context-sign-pair" ? closed_sign_pair.removableIndexes
                                                            : std::vector<int>{},
      nullptr, &closed_sign_pair));

  return candidates;
}

// --- x2CanUse(Source)DotRestoreAt -----------------------------------------
bool x2_can_use_closed_sign_change_dot_source_at(const std::vector<IrOp>& ops, int index,
                                                 const X2ValueDataflowState* state,
                                                 const DirectReturnAnalysisContext* context) {
  return x2_restore_run_before_index(ops, index, context).sawSignRestore &&
         x2_state_is_closed_plain_context(state) &&
         (!x2_state_has_unsafe_dot_restore_shape_x2(state) ||
          x2_state_has_only_dot_safe_structural_mantissa_x2(state)) &&
         x2_state_has_same_closed_sign_change_source_in_x_and_x2(state);
}

bool x2_can_use_dot_restore_at(const std::vector<IrOp>& ops, int index,
                               const X2ValueDataflowState* state, bool dot_safe, bool immediate_sync,
                               const DirectReturnAnalysisContext* context) {
  return dot_safe || immediate_sync ||
         x2_can_use_closed_sign_change_dot_source_at(ops, index, state, context);
}

bool x2_can_use_source_dot_restore_at(const std::vector<IrOp>& ops, int index,
                                      const X2ValueDataflowState* state, bool dot_safe,
                                      bool immediate_sync, bool source_proves_free_standing_restore,
                                      const DirectReturnAnalysisContext* context) {
  return x2_can_use_dot_restore_at(ops, index, state, dot_safe, immediate_sync, context) ||
         (source_proves_free_standing_restore &&
          x2_normalized_decimal_restore_gap_is_free_standing(ops, index, context));
}

// --- gap / immediate-sync dataflow states ---------------------------------
enum class NrGapState { None, Synced, OneGap, Safe };

int nr_gap_rank(NrGapState state) {
  switch (state) {
  case NrGapState::None:
    return 0;
  case NrGapState::Synced:
    return 1;
  case NrGapState::OneGap:
    return 2;
  case NrGapState::Safe:
    return 3;
  }
  return 0;
}

bool nr_is_initial_ignored_dot_gap_op(const IrOp& op) {
  return op.opcode == 0x54 || op.opcode == 0x55 || op.opcode == 0x56;
}

NrGapState nr_advance_x2_dot_restore_gap(NrGapState input, bool counts_from_fresh_sync) {
  if (input == NrGapState::None)
    return NrGapState::None;
  if (input == NrGapState::Safe)
    return NrGapState::Safe;
  if (input == NrGapState::OneGap)
    return NrGapState::Safe;
  return counts_from_fresh_sync ? NrGapState::OneGap : NrGapState::Synced;
}

NrGapState nr_transfer_plain_x2_dot_restore_gap(NrGapState input, const IrOp& op) {
  const X2Effect effect = plain_x2_effect(op);
  if (effect == X2Effect::Affects || effect == X2Effect::Restores)
    return NrGapState::Synced;
  if (effect != X2Effect::Preserves)
    return NrGapState::None;
  return nr_advance_x2_dot_restore_gap(input, !nr_is_initial_ignored_dot_gap_op(op));
}

NrGapState nr_transfer_conditional_x2_dot_restore_gap(NrGapState input, X2Effect effect) {
  if (effect == X2Effect::Affects)
    return NrGapState::Synced;
  if (effect == X2Effect::Preserves)
    return input;
  return NrGapState::None;
}

NrGapState nr_transfer_x2_dot_restore_gap(NrGapState input, const IrOp& op, X2DataflowEdgeKind edge) {
  if (has_rewrite_barrier(op))
    return NrGapState::None;
  switch (op.kind) {
  case IrKind::Label:
  case IrKind::Jump:
  case IrKind::Call:
    return input;
  case IrKind::OrphanAddress:
  case IrKind::Store:
  case IrKind::IndirectStore:
    return nr_advance_x2_dot_restore_gap(input, true);
  case IrKind::Recall:
  case IrKind::IndirectRecall:
  case IrKind::Return:
  case IrKind::Stop:
    return NrGapState::Synced;
  case IrKind::Plain:
    return nr_transfer_plain_x2_dot_restore_gap(input, op);
  case IrKind::CondJump:
  case IrKind::Loop:
  case IrKind::IndirectCondJump:
    return nr_transfer_conditional_x2_dot_restore_gap(
        input, conditional_x2_effect_for_graph_edge(op, edge));
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
    return nr_advance_x2_dot_restore_gap(input, true);
  }
  return input;
}

std::vector<bool> compute_x2_dot_restore_gap_states(const std::vector<IrOp>& ops) {
  if (ops.empty())
    return {};
  const std::vector<std::vector<RegisterValueEdge>> edges = build_register_value_graph(ops);
  std::vector<std::optional<NrGapState>> in_states(ops.size());
  in_states[0] = NrGapState::None;
  bool changed = true;
  int iterations = 0;
  while (changed && iterations < 200) {
    changed = false;
    ++iterations;
    for (std::size_t index = 0; index < ops.size(); ++index) {
      if (!in_states[index].has_value())
        continue;
      for (const RegisterValueEdge& edge : edges[index]) {
        const NrGapState output =
            nr_transfer_x2_dot_restore_gap(*in_states[index], ops[index], edge.kind);
        const std::size_t target = static_cast<std::size_t>(edge.target);
        const NrGapState joined =
            !in_states[target].has_value()
                ? output
                : (nr_gap_rank(*in_states[target]) < nr_gap_rank(output) ? *in_states[target]
                                                                         : output);
        if (!in_states[target].has_value() || *in_states[target] != joined) {
          in_states[target] = joined;
          changed = true;
        }
      }
    }
  }
  std::vector<bool> result(ops.size(), false);
  for (std::size_t index = 0; index < ops.size(); ++index)
    result[index] = in_states[index].has_value() && *in_states[index] == NrGapState::Safe;
  return result;
}

bool nr_transfer_x2_immediate_sync(bool input, const IrOp& op, X2DataflowEdgeKind edge) {
  if (has_rewrite_barrier(op))
    return false;
  switch (op.kind) {
  case IrKind::Label:
    return input;
  case IrKind::Recall:
  case IrKind::IndirectRecall:
  case IrKind::Return:
  case IrKind::Stop:
    return true;
  case IrKind::Plain: {
    const X2Effect effect = plain_x2_effect(op);
    return effect == X2Effect::Affects || effect == X2Effect::Restores;
  }
  case IrKind::CondJump:
  case IrKind::Loop:
  case IrKind::IndirectCondJump:
    return conditional_x2_effect_for_graph_edge(op, edge) == X2Effect::Affects;
  case IrKind::Jump:
  case IrKind::Call:
  case IrKind::OrphanAddress:
  case IrKind::Store:
  case IrKind::IndirectStore:
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
    return false;
  }
  return false;
}

std::vector<bool> compute_x2_immediate_sync_states(const std::vector<IrOp>& ops) {
  if (ops.empty())
    return {};
  const std::vector<std::vector<RegisterValueEdge>> edges = build_register_value_graph(ops);
  std::vector<std::optional<bool>> in_states(ops.size());
  in_states[0] = false;
  bool changed = true;
  int iterations = 0;
  while (changed && iterations < 200) {
    changed = false;
    ++iterations;
    for (std::size_t index = 0; index < ops.size(); ++index) {
      if (!in_states[index].has_value())
        continue;
      for (const RegisterValueEdge& edge : edges[index]) {
        const bool output = nr_transfer_x2_immediate_sync(*in_states[index], ops[index], edge.kind);
        const std::size_t target = static_cast<std::size_t>(edge.target);
        const bool joined = !in_states[target].has_value() ? output : (*in_states[target] && output);
        if (!in_states[target].has_value() || *in_states[target] != joined) {
          in_states[target] = joined;
          changed = true;
        }
      }
    }
  }
  std::vector<bool> result(ops.size(), false);
  for (std::size_t index = 0; index < ops.size(); ++index)
    result[index] = in_states[index].value_or(false);
  return result;
}

// --- x2-noop-restore decision logic (faithful run() body) -----------------
bool nr_is_plain_dot(const IrOp& op) {
  return op.kind == IrKind::Plain && op.opcode == kX2DotOpcode && !has_rewrite_barrier(op);
}

bool nr_dot_can_expose_context_sensitive_restore(const std::vector<IrOp>& ops, int index,
                                                 const X2ValueDataflowState* state,
                                                 const X2ValueDataflowState* state_after_dot,
                                                 const DirectReturnAnalysisContext& context) {
  if (!x2_sync_can_expose_context_sensitive_restore(ops, index))
    return false;
  return !x2_plan_dot_replacement_vp_source(ops, index, state, state_after_dot, &context)
              .preservesVpEntrySource;
}

std::vector<int> compute_x2_noop_restore_removed(const std::vector<IrOp>& ops) {
  bool any_dot = false;
  for (const IrOp& op : ops) {
    if (nr_is_plain_dot(op)) {
      any_dot = true;
      break;
    }
  }
  if (!any_dot)
    return {};
  const std::vector<std::optional<X2ValueDataflowState>> value_states =
      compute_x2_value_states(ops, {.track_register_memory = true});
  const std::vector<bool> dot_safe_states = compute_x2_dot_restore_gap_states(ops);
  const std::vector<bool> immediate_sync_states = compute_x2_immediate_sync_states(ops);
  const DirectReturnAnalysisContext direct_return_context = direct_return_analysis_context(ops);
  std::vector<int> removed;
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (!nr_is_plain_dot(op))
      continue;
    if (is_display_focus_sensitive(op))
      continue;
    const X2ValueDataflowState* state =
        value_states[static_cast<std::size_t>(index)].has_value()
            ? &*value_states[static_cast<std::size_t>(index)]
            : nullptr;
    const bool source_proves_free_standing_restore =
        x2_state_has_same_normalized_decimal_in_x_and_x2(state) ||
        x2_state_has_same_restored_visible_decimal_in_x_and_x2(state) ||
        x2_state_has_same_dot_safe_structural_mantissa_in_x_and_x2(state) ||
        (x2_state_has_same_dot_restore_value_in_x_and_x2(state) &&
         !x2_state_has_unsafe_dot_restore_shape_x2(state));
    if (!x2_can_use_source_dot_restore_at(
            ops, index, state, dot_safe_states[static_cast<std::size_t>(index)],
            immediate_sync_states[static_cast<std::size_t>(index)],
            source_proves_free_standing_restore, &direct_return_context))
      continue;
    if (!x2_state_is_closed_dot_restore_value_context(state))
      continue;
    if (x2_state_has_unsafe_dot_restore_shape_x2(state) &&
        !x2_state_has_only_dot_safe_structural_mantissa_x2(state))
      continue;
    if (!x2_state_has_same_dot_restore_value_in_x_and_x2(state) &&
        !x2_state_has_same_restored_visible_decimal_in_x_and_x2(state))
      continue;
    const std::size_t after = static_cast<std::size_t>(index + 1);
    const X2ValueDataflowState* state_after_dot =
        (after < value_states.size() && value_states[after].has_value()) ? &*value_states[after]
                                                                         : nullptr;
    if (nr_dot_can_expose_context_sensitive_restore(ops, index, state, state_after_dot,
                                                    direct_return_context))
      continue;
    removed.push_back(index);
  }
  return removed;
}

// --- x2-hidden-temp-restore decision logic (faithful run() body) ----------
bool htr_is_register_char(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'e');
}

std::optional<std::string> htr_register_source_value_fact(const X2ValueFact& fact) {
  if (fact.size() != 5 || fact.compare(0, 4, "reg:") != 0)
    return std::nullopt;
  const char c = fact[4];
  if (!htr_is_register_char(c))
    return std::nullopt;
  return std::string(1, c);
}

std::set<std::string> htr_register_dependencies_in_value_fact(const X2ValueFact& fact) {
  std::set<std::string> registers;
  std::size_t pos = 0;
  while ((pos = fact.find("reg:", pos)) != std::string::npos) {
    const std::size_t char_pos = pos + 4;
    if (char_pos < fact.size() && htr_is_register_char(fact[char_pos]))
      registers.insert(std::string(1, fact[char_pos]));
    pos = char_pos;
  }
  return registers;
}

std::string htr_loop_counter_register(const std::string& counter) {
  if (counter == "L0")
    return "0";
  if (counter == "L1")
    return "1";
  if (counter == "L2")
    return "2";
  if (counter == "L3")
    return "3";
  return counter;
}

bool htr_stops_straight_line_search(const IrOp& op) {
  switch (op.kind) {
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Call:
  case IrKind::Loop:
  case IrKind::Return:
  case IrKind::Stop:
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
  case IrKind::IndirectCondJump:
    return true;
  default:
    return false;
  }
}

bool htr_known_indirect_memory_target_equals(const IrOp& op, const std::string& register_name) {
  const std::optional<std::string> target = known_indirect_memory_target(op);
  return target.has_value() && *target == register_name;
}

bool htr_mentions_register(const IrOp& op, const std::string& register_name) {
  switch (op.kind) {
  case IrKind::Store:
  case IrKind::Recall:
  case IrKind::IndirectStore:
  case IrKind::IndirectRecall:
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
  case IrKind::IndirectCondJump:
    return op.register_name == register_name || htr_known_indirect_memory_target_equals(op, register_name);
  case IrKind::Loop:
    return htr_loop_counter_register(op.counter) == register_name;
  default:
    return false;
  }
}

bool htr_memory_access_may_overwrite_register(const IrOp& op, const std::string& register_name) {
  switch (op.kind) {
  case IrKind::Store:
    return op.register_name == register_name;
  case IrKind::IndirectStore: {
    const std::optional<std::string> target = known_indirect_memory_target(op);
    return !target.has_value() || *target == register_name ||
           (!mkpro::core::is_stable_indirect_selector(op.register_name) &&
            op.register_name == register_name);
  }
  case IrKind::IndirectRecall:
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
  case IrKind::IndirectCondJump:
    return !mkpro::core::is_stable_indirect_selector(op.register_name) &&
           op.register_name == register_name;
  case IrKind::Loop:
    return htr_loop_counter_register(op.counter) == register_name;
  default:
    return false;
  }
}

bool htr_memory_access_does_not_mention_register(const IrOp& op, const std::string& register_name) {
  if (htr_mentions_register(op, register_name))
    return false;
  if (op.kind != IrKind::IndirectStore && op.kind != IrKind::IndirectRecall)
    return true;
  return mkpro::core::is_stable_indirect_selector(op.register_name) &&
         known_indirect_memory_target(op).has_value();
}

bool htr_linear_register_safety_predicate(const IrOp& op, const std::string& register_name,
                                          bool does_not_mention_mode) {
  if (does_not_mention_mode)
    return htr_memory_access_does_not_mention_register(op, register_name) &&
           !htr_stops_straight_line_search(op);
  return !htr_memory_access_may_overwrite_register(op, register_name) &&
         !htr_stops_straight_line_search(op);
}

bool htr_direct_returning_call_has_register_safe_body(const std::vector<IrOp>& ops, const IrOp& call,
                                                      const std::string& register_name,
                                                      const DirectReturnAnalysisContext& context,
                                                      bool does_not_mention_mode) {
  return known_return_call_returns_through_nested_transparent_range(
      ops, call, context, [&](const IrOp& op) {
        return htr_linear_register_safety_predicate(op, register_name, does_not_mention_mode);
      });
}

bool htr_direct_returning_call_does_not_mention_register(const std::vector<IrOp>& ops,
                                                         const IrOp& call,
                                                         const std::string& register_name,
                                                         const DirectReturnAnalysisContext& context) {
  return htr_direct_returning_call_has_register_safe_body(ops, call, register_name, context, true);
}

bool htr_direct_returning_call_does_not_overwrite_register(
    const std::vector<IrOp>& ops, const IrOp& call, const std::string& register_name,
    const DirectReturnAnalysisContext& context) {
  return htr_direct_returning_call_has_register_safe_body(ops, call, register_name, context, false);
}

int htr_executable_start_index(const std::vector<IrOp>& ops, int index) {
  if (index >= 0 && index < static_cast<int>(ops.size()) &&
      ops.at(static_cast<std::size_t>(index)).kind == IrKind::Label)
    return index + 1;
  return index;
}

std::optional<int> htr_flow_target_start_index(const std::vector<IrOp>& ops, const IrTarget& target,
                                               const DirectReturnAnalysisContext& context) {
  std::optional<int> target_index;
  if (const auto* label = std::get_if<std::string>(&target)) {
    const auto found = context.labels.find(*label);
    if (found != context.labels.end())
      target_index = found->second;
  } else if (const auto* address = std::get_if<int>(&target)) {
    const auto found = context.addresses.find(*address);
    if (found != context.addresses.end())
      target_index = found->second;
  }
  if (!target_index.has_value())
    return std::nullopt;
  return htr_executable_start_index(ops, *target_index);
}

std::optional<int> htr_known_indirect_flow_start_index(const std::vector<IrOp>& ops, const IrOp& op,
                                                       const DirectReturnAnalysisContext& context) {
  const std::optional<int> target = known_indirect_flow_target(op);
  if (!target.has_value())
    return std::nullopt;
  const auto found = context.addresses.find(*target);
  if (found == context.addresses.end())
    return std::nullopt;
  return htr_executable_start_index(ops, found->second);
}

bool htr_register_may_be_overwritten_between(const std::vector<IrOp>& ops, int start, int end,
                                             const std::string& register_name,
                                             const DirectReturnAnalysisContext& context) {
  std::set<long long> visited;
  std::function<bool(int, bool)> visit = [&](int index, bool maybe_overwritten) -> bool {
    for (int cursor = index; cursor < static_cast<int>(ops.size()); ++cursor) {
      if (cursor == end)
        return maybe_overwritten;
      const long long key = static_cast<long long>(cursor) * 2 + (maybe_overwritten ? 1 : 0);
      if (visited.count(key) > 0)
        return false;
      visited.insert(key);

      const IrOp& op = ops.at(static_cast<std::size_t>(cursor));
      if (has_rewrite_barrier(op))
        return true;
      if (op.kind == IrKind::Label || op.kind == IrKind::OrphanAddress)
        continue;

      switch (op.kind) {
      case IrKind::Store:
        maybe_overwritten = maybe_overwritten || op.register_name == register_name;
        continue;
      case IrKind::IndirectStore:
        maybe_overwritten =
            maybe_overwritten || htr_memory_access_may_overwrite_register(op, register_name);
        continue;
      case IrKind::IndirectRecall:
        maybe_overwritten =
            maybe_overwritten || htr_memory_access_may_overwrite_register(op, register_name);
        continue;
      case IrKind::Loop: {
        const bool next_overwritten =
            maybe_overwritten || htr_loop_counter_register(op.counter) == register_name;
        const std::optional<int> target = htr_flow_target_start_index(ops, op.target, context);
        return !target.has_value() || visit(cursor + 1, next_overwritten) ||
               visit(*target, next_overwritten);
      }
      case IrKind::CondJump: {
        const std::optional<int> target = htr_flow_target_start_index(ops, op.target, context);
        return !target.has_value() || visit(cursor + 1, maybe_overwritten) ||
               visit(*target, maybe_overwritten);
      }
      case IrKind::IndirectCondJump: {
        const std::optional<int> target = htr_known_indirect_flow_start_index(ops, op, context);
        const bool next_overwritten =
            maybe_overwritten || htr_memory_access_may_overwrite_register(op, register_name);
        return !target.has_value() || visit(cursor + 1, next_overwritten) ||
               visit(*target, next_overwritten);
      }
      case IrKind::Jump: {
        const std::optional<int> target = htr_flow_target_start_index(ops, op.target, context);
        return !target.has_value() || visit(*target, maybe_overwritten);
      }
      case IrKind::IndirectJump: {
        const std::optional<int> target = htr_known_indirect_flow_start_index(ops, op, context);
        const bool next_overwritten =
            maybe_overwritten || htr_memory_access_may_overwrite_register(op, register_name);
        return !target.has_value() || visit(*target, next_overwritten);
      }
      case IrKind::Call:
      case IrKind::IndirectCall:
        if (is_known_return_call_op(op) &&
            htr_direct_returning_call_does_not_overwrite_register(ops, op, register_name, context))
          continue;
        return true;
      case IrKind::Return:
        return true;
      case IrKind::Stop:
        return false;
      case IrKind::Recall:
      case IrKind::Plain:
      case IrKind::Label:
      case IrKind::OrphanAddress:
        continue;
      }
    }
    return false;
  };
  return visit(start, false);
}

bool htr_is_supported_scratch_recall(const IrOp& op) {
  if (op.kind == IrKind::Recall)
    return true;
  return op.kind == IrKind::IndirectRecall &&
         mkpro::core::is_stable_indirect_selector(op.register_name) &&
         known_indirect_memory_target(op).has_value();
}

bool htr_is_known_indirect_conditional_fallthrough_that_does_not_mention_register(
    const IrOp& op, const std::string& register_name) {
  return op.kind == IrKind::IndirectCondJump &&
         mkpro::core::is_stable_indirect_selector(op.register_name) &&
         known_indirect_flow_target(op).has_value() && !htr_mentions_register(op, register_name);
}

std::optional<int> htr_find_dead_scratch_store(const std::vector<IrOp>& ops, int recall_index,
                                               const std::string& register_name,
                                               const DirectReturnAnalysisContext& context) {
  for (int index = recall_index - 1; index >= 0; --index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label) {
      if (context.label_entries.contains(index))
        return std::nullopt;
      continue;
    }
    if (has_rewrite_barrier(op))
      return std::nullopt;
    if (op.kind == IrKind::Store && op.register_name == register_name)
      return is_display_focus_sensitive(op) ? std::nullopt : std::optional<int>{index};
    if (op.kind == IrKind::IndirectStore &&
        mkpro::core::is_stable_indirect_selector(op.register_name) &&
        known_indirect_memory_target(op) == register_name)
      return is_display_focus_sensitive(op) ? std::nullopt : std::optional<int>{index};
    if (htr_mentions_register(op, register_name))
      return std::nullopt;
    if (op.kind == IrKind::CondJump || op.kind == IrKind::Loop)
      continue;
    if (htr_is_known_indirect_conditional_fallthrough_that_does_not_mention_register(op, register_name))
      continue;
    if (is_known_return_call_op(op) &&
        htr_direct_returning_call_does_not_mention_register(ops, op, register_name, context))
      continue;
    if (htr_stops_straight_line_search(op))
      return std::nullopt;
  }
  return std::nullopt;
}

struct HtrBranchMergedScratchStores {
  std::set<int> indexes;
};

std::optional<HtrBranchMergedScratchStores>
htr_find_branch_merged_scratch_stores(const std::vector<IrOp>& ops, int recall_index,
                                      const std::string& register_name) {
  if (recall_index <= 0 || recall_index >= static_cast<int>(ops.size()))
    return std::nullopt;

  const std::vector<std::vector<RegisterValueEdge>> successors =
      build_register_value_graph(ops);
  std::vector<std::vector<int>> predecessors(ops.size());
  for (int source = 0; source < static_cast<int>(successors.size()); ++source) {
    for (const RegisterValueEdge& edge : successors.at(static_cast<std::size_t>(source))) {
      if (edge.target >= 0 && edge.target < static_cast<int>(ops.size()))
        predecessors.at(static_cast<std::size_t>(edge.target)).push_back(source);
    }
  }

  std::vector<int> pending = predecessors.at(static_cast<std::size_t>(recall_index));
  std::set<int> visited;
  std::set<int> stores;
  bool saw_control_flow_merge = pending.size() > 1U;
  while (!pending.empty()) {
    const int index = pending.back();
    pending.pop_back();
    if (index < 0 || index >= recall_index)
      return std::nullopt;
    if (!visited.insert(index).second)
      continue;

    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Store && op.register_name == register_name) {
      if (has_rewrite_barrier(op) || is_display_focus_sensitive(op))
        return std::nullopt;
      stores.insert(index);
      continue;
    }
    if (index == 0)
      return std::nullopt;
    if (has_rewrite_barrier(op) || is_display_focus_sensitive(op) ||
        htr_mentions_register(op, register_name) ||
        htr_memory_access_may_overwrite_register(op, register_name)) {
      return std::nullopt;
    }

    switch (op.kind) {
    case IrKind::Call:
    case IrKind::IndirectJump:
    case IrKind::IndirectCall:
    case IrKind::IndirectCondJump:
    case IrKind::Return:
      return std::nullopt;
    case IrKind::CondJump:
      break;
    case IrKind::Label:
    case IrKind::Store:
    case IrKind::Recall:
    case IrKind::IndirectStore:
    case IrKind::IndirectRecall:
    case IrKind::Jump:
    case IrKind::Loop:
    case IrKind::Stop:
    case IrKind::Plain:
    case IrKind::OrphanAddress:
      break;
    }

    const std::vector<int>& incoming = predecessors.at(static_cast<std::size_t>(index));
    if (incoming.empty())
      return std::nullopt;
    saw_control_flow_merge = saw_control_flow_merge || incoming.size() > 1U;
    pending.insert(pending.end(), incoming.begin(), incoming.end());
  }

  if (!saw_control_flow_merge || stores.size() < 2U)
    return std::nullopt;
  return HtrBranchMergedScratchStores{.indexes = std::move(stores)};
}

bool htr_is_stable_stored_source_fact(const std::vector<IrOp>& ops, int store_index, int recall_index,
                                      const X2ValueFact& fact,
                                      const DirectReturnAnalysisContext& context) {
  if (fact.rfind("expr:", 0) == 0)
    return true;
  if (fact.rfind("expr-key:", 0) == 0) {
    for (const std::string& reg : htr_register_dependencies_in_value_fact(fact)) {
      if (htr_register_may_be_overwritten_between(ops, store_index + 1, recall_index, reg, context))
        return false;
    }
    return true;
  }
  return fact.rfind("decimal:", 0) == 0 && fact.size() >= 11 &&
         fact.compare(fact.size() - 11, 11, ":normalized") == 0;
}

bool htr_is_stable_register_stored_source_fact(const std::vector<IrOp>& ops, int store_index,
                                               int recall_index, const X2ValueFact& fact,
                                               const DirectReturnAnalysisContext& context) {
  const std::optional<std::string> reg = htr_register_source_value_fact(fact);
  return reg.has_value() &&
         !htr_register_may_be_overwritten_between(ops, store_index + 1, recall_index, *reg, context);
}

bool htr_source_already_synced_in_x2(const std::vector<IrOp>& ops, int store_index, int recall_index,
                                     const X2ValueDataflowState* store_state,
                                     const X2ValueDataflowState* recall_state,
                                     const DirectReturnAnalysisContext& context) {
  if (store_state == nullptr || recall_state == nullptr)
    return false;
  for (const X2ValueFact& fact : store_state->x) {
    if (!htr_is_stable_stored_source_fact(ops, store_index, recall_index, fact, context) &&
        !htr_is_stable_register_stored_source_fact(ops, store_index, recall_index, fact, context))
      continue;
    if (x2_value_set_has_fact(&recall_state->x2, fact))
      return true;
  }
  return false;
}

bool htr_source_already_dot_safe_in_x2(const X2ValueDataflowState* store_state,
                                       const X2ValueDataflowState* recall_state) {
  if (store_state == nullptr || recall_state == nullptr)
    return false;
  for (const X2ValueFact& fact : store_state->x) {
    if (!x2_value_fact_is_normalized_decimal(fact))
      continue;
    if (x2_value_set_has_fact(&recall_state->x2, fact))
      return true;
  }
  return false;
}

bool htr_source_restores_same_visible_decimal_from_x2(const X2ValueDataflowState* store_state,
                                                      const X2ValueDataflowState* recall_state) {
  return x2_value_shape_sets_have_same_restored_visible_decimal(
      store_state != nullptr ? &store_state->x : nullptr,
      store_state != nullptr ? &store_state->xShape : nullptr,
      recall_state != nullptr ? &recall_state->x2 : nullptr,
      recall_state != nullptr ? &recall_state->x2Shape : nullptr);
}

bool htr_computed_source_already_synced_in_x2(const std::vector<IrOp>& ops, int store_index,
                                              int recall_index,
                                              const X2ValueDataflowState* store_state,
                                              const X2ValueDataflowState* recall_state,
                                              const DirectReturnAnalysisContext& context) {
  if (store_state == nullptr || recall_state == nullptr)
    return false;
  for (const X2ValueFact& fact : store_state->x) {
    if (fact.rfind("expr:", 0) != 0 && fact.rfind("expr-key:", 0) != 0)
      continue;
    if (!htr_is_stable_stored_source_fact(ops, store_index, recall_index, fact, context))
      continue;
    if (x2_value_set_has_fact(&recall_state->x2, fact))
      return true;
  }
  return false;
}

bool htr_source_restores_same_visible_shape_from_x2(const std::vector<IrOp>& ops, int store_index,
                                                    int recall_index,
                                                    const X2ValueDataflowState* store_state,
                                                    const X2ValueDataflowState* recall_state,
                                                    const DirectReturnAnalysisContext& context) {
  return htr_computed_source_already_synced_in_x2(ops, store_index, recall_index, store_state,
                                                  recall_state, context) &&
         x2_value_shape_sets_have_same_restored_display_shape(
             store_state != nullptr ? &store_state->x : nullptr,
             store_state != nullptr ? &store_state->xShape : nullptr,
             recall_state != nullptr ? &recall_state->x2 : nullptr,
             recall_state != nullptr ? &recall_state->x2Shape : nullptr);
}

bool htr_source_restores_same_dot_safe_decimal_shape_from_x2(const X2ValueDataflowState* store_state,
                                                             const X2ValueDataflowState* recall_state) {
  return x2_value_shape_sets_have_same_dot_safe_decimal(
      store_state != nullptr ? &store_state->x : nullptr,
      store_state != nullptr ? &store_state->xShape : nullptr,
      recall_state != nullptr ? &recall_state->x2 : nullptr,
      recall_state != nullptr ? &recall_state->x2Shape : nullptr);
}

bool htr_source_restores_same_dot_safe_structural_shape_from_x2(
    const X2ValueDataflowState* store_state, const X2ValueDataflowState* recall_state) {
  return x2_value_shape_sets_have_same_dot_safe_structural_mantissa(
      store_state != nullptr ? &store_state->x : nullptr,
      store_state != nullptr ? &store_state->xShape : nullptr,
      recall_state != nullptr ? &recall_state->x2 : nullptr,
      recall_state != nullptr ? &recall_state->x2Shape : nullptr);
}

bool htr_recall_sync_preserves_signed_vp_source(const NrVpRestoreGapSource& source) {
  if (!source.hasSignRestoreGapBeforeVp)
    return true;
  return source.signRestoreProofReason == "source-match-explicit-sign" ||
         source.signRestoreProofReason == "source-match-nonzero-sign";
}

// --- recall shape-source facts (faithful port of helpers.ts) --------------

X2ValueSet recall_x2_value_facts(const X2ValueDataflowState& input,
                                 const std::string& register_name, bool track_register_memory,
                                 const IrOp* op) {
  const X2ValueFact value = register_value_fact(register_name);
  X2ValueSet output;
  if (track_register_memory && input.memory.has_value()) {
    const auto found = input.memory->find(register_name);
    if (found != input.memory->end())
      output.insert(found->second.begin(), found->second.end());
  }
  for (const X2ValueFact& fact : preloaded_constant_value_facts(op))
    output.insert(fact);
  output.insert(value);
  const X2ValueSet snapshot = output;
  for (const X2ValueFact& fact : snapshot) {
    if (fact.rfind("expr-key:", 0) != 0)
      continue;
    const std::optional<std::string> visible = x2_value_fact_restored_visible_decimal(fact);
    if (visible.has_value())
      output.insert(decimal_value_fact(*visible, "normalized"));
  }
  return canonical_x2_value_set(&output);
}

std::set<X2ShapeFact> stable_expression_shape_facts_from_value_facts(const X2ValueSet& values) {
  return shape_set_with_stable_expression_value_shapes(nullptr, &values);
}

std::set<X2ShapeFact> recall_structural_shape_facts(const IrOp& op,
                                                    const X2ValueDataflowState& state,
                                                    const std::string& register_name) {
  std::set<X2ShapeFact> output;
  const X2ValueSet values = recall_x2_value_facts(state, register_name, true, &op);
  for (const X2ShapeFact& fact : stable_expression_shape_facts_from_value_facts(values)) {
    for (const X2ShapeFact& structural : structural_restore_shape_facts(X2ShapeSet{fact}))
      output.insert(structural);
  }
  if (state.shapeMemory.has_value()) {
    const auto found = state.shapeMemory->find(register_name);
    if (found != state.shapeMemory->end()) {
      for (const X2ShapeFact& fact : found->second) {
        for (const X2ShapeFact& structural : structural_restore_shape_facts(X2ShapeSet{fact}))
          output.insert(structural);
      }
    }
  }
  for (const X2ShapeFact& fact : preloaded_constant_shape_facts(&op)) {
    for (const X2ShapeFact& structural : structural_restore_shape_facts(X2ShapeSet{fact}))
      output.insert(structural);
  }
  return output;
}

std::set<X2ShapeFact> recall_decimal_display_shape_facts(const IrOp& op,
                                                         const X2ValueDataflowState& state,
                                                         const std::string& register_name) {
  std::set<X2ShapeFact> output;
  const X2ValueSet values = recall_x2_value_facts(state, register_name, true, &op);
  const X2ShapeSet value_shapes = x2_shapes_from_value_facts(values);
  for (const X2ShapeFact& fact : decimal_display_shape_facts(&value_shapes))
    output.insert(fact);
  if (state.shapeMemory.has_value()) {
    const auto found = state.shapeMemory->find(register_name);
    if (found != state.shapeMemory->end()) {
      for (const X2ShapeFact& fact : decimal_display_shape_facts(&found->second))
        output.insert(fact);
    }
  }
  const X2ShapeSet preloaded = preloaded_constant_shape_facts(&op);
  for (const X2ShapeFact& fact : decimal_display_shape_facts(&preloaded))
    output.insert(fact);
  return output;
}

std::set<X2ShapeFact> recall_vp_entry_shape_source_facts(const IrOp& op,
                                                         const X2ValueDataflowState& state,
                                                         const std::string& register_name) {
  std::set<X2ShapeFact> output;
  for (const X2ShapeFact& fact : recall_decimal_display_shape_facts(op, state, register_name))
    output.insert(fact);
  for (const X2ShapeFact& fact : recall_structural_shape_facts(op, state, register_name))
    output.insert(fact);
  return output;
}

bool x2_shape_sets_have_same_structural_shape(const X2ShapeSet* left, const X2ShapeSet* right) {
  if (left == nullptr || right == nullptr)
    return false;
  const std::set<X2ShapeFact> left_shapes = x2_structural_restore_shape_facts(left);
  const std::set<X2ShapeFact> right_shapes = x2_structural_restore_shape_facts(right);
  for (const X2ShapeFact& shape : left_shapes) {
    if (right_shapes.count(shape) > 0)
      return true;
  }
  return false;
}

// Faithful port of isExpressionX2ValueFact: /^expr:\d+$/ or expr-key: prefix.
bool is_expression_x2_value_fact(const X2ValueFact& fact) {
  if (fact.rfind("expr-key:", 0) == 0)
    return true;
  if (fact.rfind("expr:", 0) != 0 || fact.size() <= 5)
    return false;
  for (std::size_t i = 5; i < fact.size(); ++i) {
    if (fact[i] < '0' || fact[i] > '9')
      return false;
  }
  return true;
}

bool is_storable_x2_memory_value_fact(const X2ValueFact& fact) {
  return is_concrete_decimal_x2_value_fact(fact) || is_expression_x2_value_fact(fact);
}

// Faithful port of storableX2MemoryValueFacts: the subset of value facts that may
// be persisted into register memory (concrete decimals, expression facts, and the
// restored visible decimal of any expr-key), excluding opaque/shared aliases such
// as reg:* and SAME_UNKNOWN_VALUE.
X2ValueSet storable_x2_memory_value_facts(const X2ValueSet& values) {
  const X2ValueSet canonical_values = canonical_x2_value_set(&values);
  X2ValueSet output;
  for (const X2ValueFact& value : canonical_values) {
    if (is_concrete_decimal_x2_value_fact(value))
      output.insert(value);
  }
  for (const X2ValueFact& value : canonical_values) {
    if (value.rfind("expr-key:", 0) == 0) {
      const std::optional<std::string> visible = x2_value_fact_restored_visible_decimal(value);
      if (visible.has_value())
        output.insert(decimal_value_fact(*visible, "normalized"));
    }
    if (is_expression_x2_value_fact(value))
      output.insert(value);
  }
  return output;
}

// Faithful port of recallAlreadyInXMemoryValue (default storable predicate),
// matching against the visible X value set.
std::optional<std::string>
recall_already_in_x_memory_value(const IrOp& op,
                                 const std::optional<X2ValueDataflowState>& state) {
  const std::optional<std::string> register_name = removable_recall_value_register(op);
  if (!register_name.has_value() || !state.has_value() || !state->memory.has_value())
    return std::nullopt;
  const auto found = state->memory->find(*register_name);
  if (found == state->memory->end())
    return std::nullopt;
  for (const X2ValueFact& fact : found->second) {
    if (is_storable_x2_memory_value_fact(fact) && x2_value_set_has_fact(&state->x, fact))
      return register_name;
  }
  return std::nullopt;
}

// Faithful port of recallAlreadyInXPreloadedDecimal.
std::optional<std::string>
recall_already_in_x_preloaded_decimal(const IrOp& op,
                                      const std::optional<X2ValueDataflowState>& state) {
  const std::optional<std::string> register_name = removable_recall_value_register(op);
  if (!register_name.has_value() || !state.has_value())
    return std::nullopt;
  for (const X2ValueFact& fact : preloaded_constant_value_facts(&op)) {
    if (x2_value_set_has_fact(&state->x, fact))
      return register_name;
  }
  return std::nullopt;
}

// Faithful port of recallAlreadyInXRestoredVisibleDecimal.
std::optional<std::string>
recall_already_in_x_restored_visible_decimal(const IrOp& op,
                                             const std::optional<X2ValueDataflowState>& state) {
  const std::optional<std::string> register_name = removable_recall_value_register(op);
  if (!register_name.has_value() || !state.has_value())
    return std::nullopt;
  const X2ValueSet values = recall_x2_value_facts(*state, *register_name, true, &op);
  const std::set<X2ShapeFact> shapes =
      recall_decimal_display_shape_facts(op, *state, *register_name);
  return x2_value_shape_sets_have_same_restored_visible_decimal(&state->x, &state->xShape, &values,
                                                                &shapes)
             ? register_name
             : std::nullopt;
}

// Faithful port of recallAlreadyInXRestoredDisplayShape.
std::optional<std::string>
recall_already_in_x_restored_display_shape(const IrOp& op,
                                           const std::optional<X2ValueDataflowState>& state) {
  const std::optional<std::string> register_name = removable_recall_value_register(op);
  if (!register_name.has_value() || !state.has_value())
    return std::nullopt;
  const std::set<X2ShapeFact> shapes =
      recall_vp_entry_shape_source_facts(op, *state, *register_name);
  const std::optional<X2ShapeSet> effective = nr_effective_visible_x_state_shape(&*state);
  return x2_shape_sets_have_same_restored_display_shape(effective.has_value() ? &*effective : nullptr,
                                                        &shapes)
             ? register_name
             : std::nullopt;
}

// Faithful port of recallAlreadySyncedInX2Value.
std::optional<std::string>
recall_already_synced_in_x2_value(const IrOp& op,
                                  const std::optional<X2ValueDataflowState>& state) {
  const std::optional<std::string> register_name = removable_recall_value_register(op);
  if (!register_name.has_value() || !state.has_value())
    return std::nullopt;
  return x2_value_set_has_register(state->x2, *register_name) ? register_name : std::nullopt;
}

// Faithful port of recallAlreadySyncedInX2MemoryValue (default storable predicate).
std::optional<std::string>
recall_already_synced_in_x2_memory_value(const IrOp& op,
                                         const std::optional<X2ValueDataflowState>& state) {
  const std::optional<std::string> register_name = removable_recall_value_register(op);
  if (!register_name.has_value() || !state.has_value() || !state->memory.has_value())
    return std::nullopt;
  const auto found = state->memory->find(*register_name);
  if (found == state->memory->end())
    return std::nullopt;
  for (const X2ValueFact& fact : found->second) {
    if (is_storable_x2_memory_value_fact(fact) && x2_value_set_has_fact(&state->x2, fact))
      return register_name;
  }
  return std::nullopt;
}

// Faithful port of recallAlreadySyncedInX2PreloadedDecimal.
std::optional<std::string>
recall_already_synced_in_x2_preloaded_decimal(const IrOp& op,
                                              const std::optional<X2ValueDataflowState>& state) {
  const std::optional<std::string> register_name = removable_recall_value_register(op);
  if (!register_name.has_value() || !state.has_value())
    return std::nullopt;
  for (const X2ValueFact& fact : preloaded_constant_value_facts(&op)) {
    if (x2_value_set_has_fact(&state->x2, fact))
      return register_name;
  }
  return std::nullopt;
}

// Faithful port of recallAlreadySyncedInX2RestoredVisibleDecimal.
std::optional<std::string>
recall_already_synced_in_x2_restored_visible_decimal(
    const IrOp& op, const std::optional<X2ValueDataflowState>& state) {
  const std::optional<std::string> register_name = removable_recall_value_register(op);
  if (!register_name.has_value() || !state.has_value())
    return std::nullopt;
  std::set<std::string> hidden_decimals = x2_value_set_restored_visible_decimals(&state->x2);
  const std::optional<X2ShapeSet> effective = nr_effective_x2_state_shape(&*state);
  for (const std::string& decimal :
       dot_safe_decimal_shape_values(effective.has_value() ? &*effective : nullptr))
    hidden_decimals.insert(decimal);
  if (hidden_decimals.empty())
    return std::nullopt;
  const std::set<X2ShapeFact> recall_shapes =
      recall_decimal_display_shape_facts(op, *state, *register_name);
  for (const std::string& visible : x2_shape_set_restored_visible_decimals(&recall_shapes)) {
    if (hidden_decimals.count(visible) > 0)
      return register_name;
  }
  return std::nullopt;
}

std::optional<std::string>
recall_already_synced_in_x2_structural_shape(const IrOp& op,
                                             const std::optional<X2ValueDataflowState>& state) {
  const std::optional<std::string> register_name = removable_recall_value_register(op);
  if (!register_name.has_value() || !state.has_value())
    return std::nullopt;
  const std::set<X2ShapeFact> shapes = recall_structural_shape_facts(op, *state, *register_name);
  const std::optional<X2ShapeSet> effective = nr_effective_x2_state_shape(&*state);
  return x2_shape_sets_have_same_structural_shape(effective.has_value() ? &*effective : nullptr,
                                                  &shapes)
             ? std::optional<std::string>{*register_name}
             : std::nullopt;
}

std::optional<std::string>
recall_already_synced_in_x2_vp_shape(const IrOp& op,
                                     const std::optional<X2ValueDataflowState>& state) {
  const std::optional<std::string> register_name = removable_recall_value_register(op);
  if (!register_name.has_value() || !state.has_value())
    return std::nullopt;
  const std::set<X2ShapeFact> shapes =
      recall_vp_entry_shape_source_facts(op, *state, *register_name);
  const std::optional<X2ShapeSet> effective = nr_effective_x2_state_shape(&*state);
  return x2_shape_sets_have_same_restored_display_shape(
             effective.has_value() ? &*effective : nullptr, &shapes)
             ? std::optional<std::string>{*register_name}
             : std::nullopt;
}

const IrOp* next_immediate_x2_restore_op(const std::vector<IrOp>& ops, int start) {
  for (int index = start; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label || op.kind == IrKind::OrphanAddress)
      continue;
    return &op;
  }
  return nullptr;
}

bool recall_removal_preserves_immediate_vp_restore_context(
    const std::vector<IrOp>& ops, int recall_index,
    const std::optional<X2ValueDataflowState>& state,
    const std::optional<RecallValueProof>& value_proof,
    const DirectReturnAnalysisContext* context) {
  if (!state.has_value() || !value_proof.has_value() ||
      (!value_proof->x2_sync_shape && !value_proof->x2_sync_value &&
       !value_proof->x2_sync_vp_shape))
    return false;
  if (recall_index < 0 || recall_index >= static_cast<int>(ops.size()))
    return false;
  const IrOp& op = ops.at(static_cast<std::size_t>(recall_index));
  const X2ValueSet recalled_values =
      recall_x2_value_facts(*state, value_proof->register_name, true, &op);
  const std::optional<RegisterValueSet> recalled_mantissas =
      vp_entry_mantissas_from_value_facts(recalled_values);
  const std::set<X2ShapeFact> recalled_shapes =
      recall_vp_entry_shape_source_facts(op, *state, value_proof->register_name);
  const std::set<std::string> recalled_source_keys = tx_vp_source_keys_from_fields(
      recalled_mantissas.has_value() ? &*recalled_mantissas : nullptr, &recalled_shapes);
  const std::optional<X2ValueDataflowState> recalled_state = transfer_x2_value_state_for_edge(
      state, op, X2DataflowEdgeKind::Normal,
      X2TransferStateOptions{.track_register_memory = true}, recall_index);
  const NrVpRestoreGapSource vp_source =
      x2_plan_dot_replacement_vp_source(
          ops, recall_index, &*state,
          recalled_state.has_value() ? &*recalled_state : nullptr, context)
          .source;
  if (vp_source.hasSignRestoreGapBeforeVp && vp_source.hasOnlyRestoreGapBeforeVp &&
      (vp_source.signRestoreProofCanDiscard ||
       string_sets_intersect(nr_vp_entry_sign_source_keys(&*state), recalled_source_keys)))
    return true;
  const IrOp* next_restore = next_immediate_x2_restore_op(ops, recall_index + 1);
  if (next_restore == nullptr || next_restore->kind != IrKind::Plain ||
      next_restore->opcode != 0x0c)
    return false;
  const NrVpShapeContext vp_context = analyze_x2_vp_shape_context(&*state);
  if (vp_context.kind == "active-mantissa") {
    const std::set<std::string> active_keys = tx_vp_source_keys_from_fields(
        vp_context.mantissa.has_value() ? &*vp_context.mantissa : nullptr, nullptr);
    if (string_sets_intersect(active_keys, recalled_source_keys))
      return true;
  }
  return string_sets_intersect(nr_vp_entry_source_keys(&*state), recalled_source_keys);
}

std::vector<X2HiddenTempReplacement> compute_x2_hidden_temp_restore(const std::vector<IrOp>& ops) {
  const std::vector<std::optional<RegisterValueSet>> register_states = compute_x2_register_states(ops);
  const std::vector<std::optional<X2ValueDataflowState>> value_states =
      compute_x2_value_states(ops, {.track_register_memory = true});
  const std::vector<bool> dot_safe_states = compute_x2_dot_restore_gap_states(ops);
  const std::vector<bool> immediate_sync_states = compute_x2_immediate_sync_states(ops);
  const LivenessInfo liveness = compute_liveness(ops);
  const DirectReturnAnalysisContext context = direct_return_analysis_context(ops);
  std::set<int> replaced_stack_lift_producer_indexes;
  std::vector<X2HiddenTempReplacement> replacements;

  const auto state_at = [&](int i) -> const X2ValueDataflowState* {
    if (i < 0 || i >= static_cast<int>(value_states.size()))
      return nullptr;
    return value_states[static_cast<std::size_t>(i)].has_value()
               ? &*value_states[static_cast<std::size_t>(i)]
               : nullptr;
  };
  const auto flag_at = [](const std::vector<bool>& flags, int i) -> bool {
    return i >= 0 && i < static_cast<int>(flags.size()) && flags[static_cast<std::size_t>(i)];
  };

  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    const std::optional<std::string> register_opt = removable_recall_value_register(op);
    if (!register_opt.has_value())
      continue;
    const std::string register_name = *register_opt;
    if (!htr_is_supported_scratch_recall(op))
      continue;
    if (is_display_focus_sensitive(op))
      continue;
    const std::optional<int> store_index =
        htr_find_dead_scratch_store(ops, index, register_name, context);
    const std::optional<HtrBranchMergedScratchStores> branch_stores =
        store_index.has_value()
            ? std::nullopt
            : htr_find_branch_merged_scratch_stores(ops, index, register_name);
    if (!store_index.has_value() && !branch_stores.has_value())
      continue;
    if (liveness.live_out.at(static_cast<std::size_t>(index)).count(register_name) > 0)
      continue;
    const std::optional<RecallRemovalAnalysis> removal = analyze_recall_removal(
        ops, index, register_states.at(static_cast<std::size_t>(index)),
        value_states.at(static_cast<std::size_t>(index)), &context);
    if (!removal.has_value())
      continue;

    const bool branch_merged =
        branch_stores.has_value() && removal->value_proof.has_value() &&
        removal->value_proof->x2_sync_register == register_name;
    if (branch_stores.has_value() && !branch_merged)
      continue;

    bool branch_sources_are_dot_safe = branch_merged;
    if (branch_stores.has_value()) {
      for (const int branch_store_index : branch_stores->indexes) {
        const X2ValueDataflowState* branch_store_state = state_at(branch_store_index);
        if (x2_restore_safety(branch_store_state) != X2ShapeSafety::DotSafeDecimal ||
            !x2_state_has_same_normalized_decimal_in_x_and_x2(branch_store_state)) {
          branch_sources_are_dot_safe = false;
          break;
        }
      }
      if (!branch_sources_are_dot_safe)
        continue;
    }

    const X2ValueDataflowState* store_state =
        store_index.has_value() ? state_at(*store_index) : nullptr;
    const X2ValueDataflowState* recall_state = state_at(index);

    const bool source_already_synced =
        branch_merged ||
        (store_index.has_value() && htr_source_already_synced_in_x2(
                                        ops, *store_index, index, store_state, recall_state, context));
    const bool source_already_dot_safe =
        branch_sources_are_dot_safe ||
        htr_source_already_dot_safe_in_x2(store_state, recall_state);
    const bool source_restores_same_visible_decimal =
        htr_source_restores_same_visible_decimal_from_x2(store_state, recall_state);
    const bool source_restores_same_visible_shape =
        store_index.has_value() && htr_source_restores_same_visible_shape_from_x2(
                                       ops, *store_index, index, store_state, recall_state, context);
    const bool source_restores_same_dot_safe_decimal_shape =
        htr_source_restores_same_dot_safe_decimal_shape_from_x2(store_state, recall_state);
    const bool source_restores_same_dot_safe_structural_shape =
        htr_source_restores_same_dot_safe_structural_shape_from_x2(store_state, recall_state);

    if (!removal->x2_sync_redundant && !source_already_synced &&
        !source_restores_same_visible_decimal && !source_restores_same_visible_shape &&
        !source_restores_same_dot_safe_decimal_shape && !source_restores_same_dot_safe_structural_shape)
      continue;

    const bool source_proves_free_standing_restore =
        source_already_dot_safe || source_restores_same_visible_decimal ||
        source_restores_same_dot_safe_decimal_shape || source_restores_same_dot_safe_structural_shape;

    const bool can_use_source_dot_restore = x2_can_use_source_dot_restore_at(
        ops, index, recall_state, flag_at(dot_safe_states, index),
        flag_at(immediate_sync_states, index), source_proves_free_standing_restore, &context);

    const NrDotReplacementPlan vp_source_plan =
        x2_plan_dot_replacement_vp_source(ops, index, recall_state, state_at(index + 1), &context);

    const bool recall_sync_proves_vp_source =
        removal->value_proof.has_value() &&
        (removal->value_proof->x2_sync_vp_shape || removal->value_proof->x2_sync_shape);

    const bool can_use_vp_source_escape =
        (source_already_dot_safe && vp_source_plan.source.hasOnlyRestoreGapBeforeVp &&
         vp_source_plan.preservesVpEntrySource) ||
        (recall_sync_proves_vp_source &&
         vp_source_plan.source.replacementDotHasOnlyRestoreGapBeforeVp &&
         htr_recall_sync_preserves_signed_vp_source(vp_source_plan.source));

    if (!can_use_vp_source_escape && !source_restores_same_visible_shape &&
        !source_restores_same_dot_safe_decimal_shape &&
        !source_restores_same_dot_safe_structural_shape &&
        x2_state_has_unsafe_dot_restore_shape_x2(recall_state))
      continue;

    if (!can_use_source_dot_restore && !can_use_vp_source_escape)
      continue;

    bool exposes_x2_restore = false;
    if (can_use_vp_source_escape) {
      exposes_x2_restore = false;
    } else if ((source_already_synced || source_restores_same_visible_shape ||
                source_restores_same_dot_safe_decimal_shape ||
                source_restores_same_dot_safe_structural_shape) &&
               !removal->x2_sync_redundant) {
      X2RestoreExposureOptions options;
      options.redundant_sync_value = source_already_synced;
      options.redundant_sync_shape = source_restores_same_visible_shape ||
                                     source_restores_same_dot_safe_decimal_shape ||
                                     source_restores_same_dot_safe_structural_shape;
      exposes_x2_restore = removing_recall_can_expose_x2_restore(ops, index, options);
    } else {
      exposes_x2_restore = removal->exposes_x2_restore;
    }

    const X2ReplacementStackLiftPlan stack_lift_plan = plan_x2_replacement_stack_lift(
        ops, index, index, value_states.at(static_cast<std::size_t>(index)), context,
        removal->exposes_stack_lift,
        X2ReplacementStackLiftOptions{
            .invalidated_producer_indexes = &replaced_stack_lift_producer_indexes});
    if (stack_lift_plan.exposes_stack_lift || exposes_x2_restore)
      continue;

    if (analyze_x2_stack_effect(op).stack_lift_and_x2_sync)
      replaced_stack_lift_producer_indexes.insert(index);
    replacements.push_back(X2HiddenTempReplacement{
        .index = index,
        .register_name = register_name,
        .branch_merged = branch_merged,
    });
  }
  return replacements;
}

std::string transfer_plain_debug(const std::string& opcodes_csv, bool use_producer) {
  X2ValueDataflowState state = internal_empty_x2_value_dataflow_state(false);
  std::size_t start = 0;
  int step = 0;
  while (start <= opcodes_csv.size()) {
    const std::size_t comma = opcodes_csv.find(',', start);
    const std::string token =
        opcodes_csv.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!token.empty()) {
      const int opcode = static_cast<int>(std::strtol(token.c_str(), nullptr, 16));
      IrOp op;
      op.kind = IrKind::Plain;
      op.opcode = opcode;
      std::optional<int> producer;
      if (use_producer)
        producer = step;
      state = transfer_plain_x2_value_state(state, op, producer);
    }
    if (comma == std::string::npos)
      break;
    start = comma + 1;
    ++step;
  }
  return serialize_x2_state(state);
}

X2ValueDataflowState transfer_plain_replay(const std::string& opcodes_csv, bool use_producer) {
  X2ValueDataflowState state = internal_empty_x2_value_dataflow_state(false);
  std::size_t start = 0;
  int step = 0;
  while (start <= opcodes_csv.size()) {
    const std::size_t comma = opcodes_csv.find(',', start);
    const std::string token =
        opcodes_csv.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!token.empty()) {
      const int opcode = static_cast<int>(std::strtol(token.c_str(), nullptr, 16));
      IrOp op;
      op.kind = IrKind::Plain;
      op.opcode = opcode;
      std::optional<int> producer;
      if (use_producer)
        producer = step;
      state = transfer_plain_x2_value_state(state, op, producer);
    }
    if (comma == std::string::npos)
      break;
    start = comma + 1;
    ++step;
  }
  return state;
}

// Session C.4 isolation test hook: replays two opcode sequences into states A and
// B, then joins them (current=A, incoming=B) and serializes the result for the
// differential unit test against the TypeScript oracle.
std::string join_debug(const std::string& csv_a, const std::string& csv_b, bool use_producer) {
  const X2ValueDataflowState a = transfer_plain_replay(csv_a, use_producer);
  const X2ValueDataflowState b = transfer_plain_replay(csv_b, use_producer);
  const X2ValueDataflowState joined = tx_join_x2_value_dataflow_states(&a, b, false);
  return serialize_x2_state(joined);
}

// Session C.2 isolation test hook: drives the shared*/effectiveInput cluster
// and the six transferPlainX2VpEntry* functions in their natural call form.
std::string vp_entry_debug(int opcode, const std::string& x_csv, const std::string& x2_csv,
                           const std::string& x_shape_csv, const std::string& x2_shape_csv,
                           const std::string& in_vp_mantissa_csv,
                           const std::string& in_vp_sign_mantissa_csv,
                           const std::string& in_vp_shape_csv, const std::string& in_vp_sign_shape_csv,
                           bool mantissa_transient, bool shape_transient) {
  const auto parse = [](const std::string& input) {
    std::set<std::string> output;
    if (input == "_")
      return output;
    std::size_t start = 0;
    while (true) {
      const std::size_t bar = input.find('|', start);
      if (bar == std::string::npos) {
        output.insert(input.substr(start));
        break;
      }
      output.insert(input.substr(start, bar - start));
      start = bar + 1;
    }
    return output;
  };
  const auto join = [](const std::set<std::string>& items) {
    if (items.empty())
      return std::string{"_"};
    std::string output;
    for (const std::string& item : items) {
      if (!output.empty())
        output += ",";
      output += item;
    }
    return output;
  };
  const auto join_opt_set = [&join](const std::optional<std::set<std::string>>& items) {
    return items.has_value() ? join(*items) : std::string{"_"};
  };
  const X2ValueSet x = parse(x_csv);
  const X2ValueSet x2 = parse(x2_csv);
  const X2ShapeSet x_shape = parse(x_shape_csv);
  const X2ShapeSet x2_shape = parse(x2_shape_csv);
  const X2ShapeSet* x_ptr = x.empty() ? nullptr : &x;
  const X2ShapeSet* x2v_ptr = x2.empty() ? nullptr : &x2;
  const X2ShapeSet* xs_ptr = x_shape.empty() ? nullptr : &x_shape;
  const X2ShapeSet* x2s_ptr = x2_shape.empty() ? nullptr : &x2_shape;

  X2ValueDataflowState input = internal_empty_x2_value_dataflow_state(false);
  if (in_vp_mantissa_csv != "_")
    input.vpEntryMantissa = parse(in_vp_mantissa_csv);
  if (in_vp_sign_mantissa_csv != "_")
    input.vpEntrySignMantissa = parse(in_vp_sign_mantissa_csv);
  if (in_vp_shape_csv != "_")
    input.vpEntryShape = parse(in_vp_shape_csv);
  if (in_vp_sign_shape_csv != "_")
    input.vpEntrySignShape = parse(in_vp_sign_shape_csv);
  input.vpEntryMantissaTransient = mantissa_transient;
  input.vpEntryShapeTransient = shape_transient;

  const IrOp op = stable_expression_plain_op(opcode);
  const X2Effect effect = plain_x2_effect(op);

  const std::optional<RegisterValueSet> sdm = shared_decimal_vp_entry_mantissas(x_ptr, x2v_ptr, xs_ptr, x2s_ptr);
  const std::optional<X2ShapeSet> seds = shared_exact_decimal_display_shape_facts(x_ptr, x2v_ptr, xs_ptr, x2s_ptr);
  const std::optional<X2ShapeSet> sss = shared_structural_shape_facts(x_ptr, x2v_ptr, xs_ptr, x2s_ptr);
  const std::optional<X2ShapeSet> svss = shared_vp_entry_sign_shape_facts(x_ptr, x2v_ptr, xs_ptr, x2s_ptr);
  const std::optional<X2ShapeSet> eixs = effective_input_x_shape(xs_ptr, x_ptr);
  const std::optional<X2ShapeSet> eix2s = effective_input_x2_shape(x2s_ptr, x2v_ptr);

  const std::optional<RegisterValueSet> vem = transfer_plain_x2_vp_entry_mantissa_state(
      input, op, x, x2, x_shape, x2_shape, effect, xs_ptr, x2s_ptr, false, x_ptr, x2v_ptr);
  const bool vemt = transfer_plain_x2_vp_entry_mantissa_transient_state(
      op, effect, xs_ptr, x2s_ptr, false, x_ptr, x2v_ptr);
  const std::optional<RegisterValueSet> vsm = transfer_plain_x2_vp_entry_sign_mantissa_state(input, op, effect);
  const std::optional<X2ShapeSet> vss =
      transfer_plain_x2_vp_entry_sign_shape_state(input, op, x, x2, x_shape, x2_shape, effect);
  const std::optional<X2ShapeSet> ves = transfer_plain_x2_vp_entry_shape_state(
      input, op, x, x2, x_shape, x2_shape, effect, xs_ptr, x2s_ptr, false, x_ptr, x2v_ptr);
  const bool vest = transfer_plain_x2_vp_entry_shape_transient_state(
      op, effect, xs_ptr, x2s_ptr, false, x_ptr, x2v_ptr);

  return "sdm=" + join_opt_set(sdm) + "|seds=" + join_opt_set(seds) + "|sss=" + join_opt_set(sss) +
         "|svss=" + join_opt_set(svss) + "|eixs=" + join_opt_set(eixs) +
         "|eix2s=" + join_opt_set(eix2s) + "|vem=" + join_opt_set(vem) +
         "|vemt=" + (vemt ? "1" : "0") + "|vsm=" + join_opt_set(vsm) + "|vss=" + join_opt_set(vss) +
         "|ves=" + join_opt_set(ves) + "|vest=" + (vest ? "1" : "0");
}

// Session C isolation test hook: drives the VP first-digit splice machinery for
// a pair of shape-fact sets and returns a canonical serialization for the
// differential unit test against the TypeScript oracle.
std::string vp_splice_debug(const std::string& x_shape_csv, const std::string& x2_shape_csv,
                            bool include_exponent_targets) {
  const auto parse = [](const std::string& input) {
    X2ShapeSet output;
    if (input == "_")
      return output;
    std::size_t start = 0;
    while (true) {
      const std::size_t comma = input.find('|', start);
      if (comma == std::string::npos) {
        output.insert(input.substr(start));
        break;
      }
      output.insert(input.substr(start, comma - start));
      start = comma + 1;
    }
    return output;
  };
  const auto join = [](const std::set<std::string>& items) {
    if (items.empty())
      return std::string{"_"};
    std::string output;
    for (const std::string& item : items) {
      if (!output.empty())
        output += ",";
      output += item;
    }
    return output;
  };
  const auto join_opt = [&join](const std::optional<std::set<std::string>>& items) {
    return items.has_value() ? join(*items) : std::string{"_"};
  };
  const X2ShapeSet x_shape = parse(x_shape_csv);
  const X2ShapeSet x2_shape = parse(x2_shape_csv);
  const X2ShapeSet* x_ptr = x_shape.empty() ? nullptr : &x_shape;
  const X2ShapeSet* x2_ptr = x2_shape.empty() ? nullptr : &x2_shape;
  return "src=" + join(restored_vp_first_digit_source_shape_facts(x_ptr)) +
         "|dtgt=" + join(decimal_vp_first_digit_splice_target_shape_facts(x2_ptr, include_exponent_targets)) +
         "|vtgt=" + join(vp_first_digit_splice_target_shape_facts(x2_ptr, include_exponent_targets)) +
         "|sshape=" +
         join_opt(structural_first_digit_vp_splice_shape_facts(x_ptr, x2_ptr, include_exponent_targets)) +
         "|dmant=" +
         join_opt(decimal_first_digit_vp_splice_mantissas(x_ptr, x2_ptr, include_exponent_targets));
}

// --- isolation test hooks -------------------------------------------------
std::string structural_unary_debug(int opcode, const X2ShapeFact& fact) {
  const X2ShapeSet single{fact};
  const auto join = [](const std::set<std::string>& items) {
    if (items.empty())
      return std::string{"_"};
    std::string output;
    for (const std::string& item : items) {
      if (!output.empty())
        output += ",";
      output += item;
    }
    return output;
  };
  // Keep the raw-display shift path referenced (it is exercised by the deferred
  // binary engine) so it compiles clean under -Werror.
  static_cast<void>(&shift_structural_hex_raw_display_shape);
  return "v=" + join(plain_produces_concrete_structural_unary_decimal_values(opcode, &single)) +
         "|s=" + join(plain_produces_concrete_structural_unary_decimal_shape_facts(opcode, &single)) +
         "|dv=" + join(plain_produces_concrete_direct_structural_unary_decimal_values(opcode, &single)) +
         "|ds=" +
         join(plain_produces_concrete_direct_structural_unary_decimal_shape_facts(opcode, &single));
}

// --- isolation test hooks -------------------------------------------------
std::string display_debug(const X2ShapeFact& fact) {
  const auto opt = [](const std::optional<std::string>& value) { return value.value_or("_"); };
  const X2ShapeSet single{fact};
  std::string dds;
  for (const X2ShapeFact& shape : decimal_display_shape_facts(&single)) {
    if (!dds.empty())
      dds += ",";
    dds += shape;
  }
  if (dds.empty())
    dds = "_";
  std::string ssrvd;
  for (const std::string& decimal : x2_shape_set_restored_visible_decimals(&single)) {
    if (!ssrvd.empty())
      ssrvd += ",";
    ssrvd += decimal;
  }
  if (ssrvd.empty())
    ssrvd = "_";
  return "rvd=" + opt(x2_shape_fact_restored_visible_decimal(fact)) +
         "|sso=" + opt(x2_shape_fact_shape_only_exact_decimal_display(fact)) +
         "|edd=" + opt(x2_shape_fact_exact_decimal_display(fact)) +
         "|ennd=" + opt(x2_shape_fact_exact_non_negative_display_decimal(fact)) + "|dds=" + dds +
         "|ssrvd=" + ssrvd;
}

std::string fraction_part_shape_debug(const std::string& value) {
  return decimal_fraction_part_shape_fact(value).value_or("_");
}

// Session A.3 + A.4 isolation test hook: drives the full
// stable/opaque/concrete expression closure for a synthetic plain op.
std::string stable_expression_debug(int opcode, bool has_producer, int producer_index,
                                    const X2ValueSet& x, const X2ValueSet& y, const X2ShapeSet& x_shape,
                                    const X2ShapeSet& y_shape, const X2ShapeSet& direct_x_shape,
                                    const X2ShapeSet& direct_y_shape) {
  const auto join = [](const std::set<std::string>& items) {
    if (items.empty())
      return std::string{"_"};
    std::string output;
    for (const std::string& item : items) {
      if (!output.empty())
        output += ",";
      output += item;
    }
    return output;
  };
  // Keep the public value-set entry point referenced (it is consumed by the
  // Session B/C transfer machinery, not by this dead-code layer yet) so it
  // compiles clean under -Werror.
  static_cast<void>(static_cast<std::set<X2ValueFact> (*)(const std::string&)>(
      &stable_expression_key_value_set));
  const IrOp op = stable_expression_plain_op(opcode);
  const ConcreteEvaluationOptions options = concrete_evaluation_options_for_stable_expression_opcode(opcode);
  std::optional<int> producer;
  if (has_producer)
    producer = producer_index;
  const std::set<X2ValueFact> values = plain_x_value_after_non_preserving_op(
      op, producer, &x, &y, &x_shape, &y_shape, &direct_y_shape, &direct_x_shape);
  const std::set<X2ShapeFact> shapes = plain_x_shape_after_non_preserving_op(
      op, &x, &y, &x_shape, &y_shape, options, &direct_y_shape, &direct_x_shape);
  return "v=" + join(values) + "|s=" + join(shapes);
}

// Session B isolation test hook: drives the entry/VP/structural state-machine
// builders for a scenario and returns a canonical serialization so the
// differential unit test can compare against the TypeScript oracle.
namespace state_builders_debug_detail {

RegisterValueSet parse_csv(const std::string& input) {
  RegisterValueSet output;
  if (input == "_")
    return output;
  std::size_t start = 0;
  while (true) {
    const std::size_t comma = input.find(',', start);
    if (comma == std::string::npos) {
      output.insert(input.substr(start));
      break;
    }
    output.insert(input.substr(start, comma - start));
    start = comma + 1;
  }
  return output;
}

std::string join_set(const std::set<std::string>& items) {
  std::string output = "{";
  bool first = true;
  for (const std::string& item : items) {
    if (!first)
      output += ",";
    output += item;
    first = false;
  }
  return output + "}";
}

std::string join_optional(const std::optional<std::set<std::string>>& items) {
  return items.has_value() ? join_set(*items) : std::string{"_"};
}

std::string serialize_entry(const X2EntryState& entry) {
  switch (entry.kind) {
    case X2EntryState::Kind::Closed:
      return "closed";
    case X2EntryState::Kind::Unknown:
      return "unknown";
    case X2EntryState::Kind::Open:
      return "open" + join_set(entry.raw);
    case X2EntryState::Kind::Exponent:
      return "exp" + join_set(entry.mantissa) + ";" + join_set(entry.exponent);
  }
  return "?";
}

std::string serialize_vp(const X2VpContextState& vp) {
  switch (vp.kind) {
    case X2VpContextState::Kind::None:
      return "none";
    case X2VpContextState::Kind::Unknown:
      return "unknown";
    case X2VpContextState::Kind::Exponent:
      return "exp" + join_set(vp.mantissa) + ";" + join_set(vp.exponent);
  }
  return "?";
}

std::string serialize_struct(const X2StructuralEntryState& s) {
  switch (s.kind) {
    case X2StructuralEntryState::Kind::None:
      return "none";
    case X2StructuralEntryState::Kind::Unknown:
      return "unknown";
    case X2StructuralEntryState::Kind::Exponent:
      return "exp" + join_set(s.mantissa) + ";" + join_set(s.exponent);
  }
  return "?";
}

std::string serialize_state(const X2ValueDataflowState& state) {
  std::string output;
  output += "x=" + join_set(state.x);
  output += "|y=" + join_optional(state.y);
  output += "|x2=" + join_set(state.x2);
  output += "|xs=" + join_set(state.xShape);
  output += "|ys=" + join_optional(state.yShape);
  output += "|x2s=" + join_set(state.x2Shape);
  output += "|xds=" + join_optional(state.xDirectShape);
  output += "|yds=" + join_optional(state.yDirectShape);
  output += "|entry=" + serialize_entry(state.entry);
  output += "|vp=" + serialize_vp(state.vpContext);
  output += "|se=" + serialize_struct(state.structuralEntry);
  output += "|svp=" + serialize_struct(state.structuralVpContext);
  output += "|vem=" + join_optional(state.vpEntryMantissa);
  output += std::string("|vemt=") + (state.vpEntryMantissaTransient ? "1" : "0");
  output += "|vsm=" + join_optional(state.vpEntrySignMantissa);
  output += "|ves=" + join_optional(state.vpEntryShape);
  output += "|vss=" + join_optional(state.vpEntrySignShape);
  output += std::string("|vest=") + (state.vpEntryShapeTransient ? "1" : "0");
  return output;
}

X2EntryState exponent_entry(const std::string& mant, const std::string& exp) {
  return X2EntryState{
      .kind = X2EntryState::Kind::Exponent, .mantissa = parse_csv(mant), .exponent = parse_csv(exp)};
}

X2StructuralEntryState structural_exponent(const std::string& mant, const std::string& exp) {
  return X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::Exponent,
                                .mantissa = parse_csv(mant),
                                .exponent = parse_csv(exp)};
}

}  // namespace state_builders_debug_detail

std::string state_builders_debug(const std::string& scenario, const std::string& a,
                                 const std::string& b, const std::string& c) {
  using namespace state_builders_debug_detail;
  if (scenario == "openRaw") {
    const RegisterValueSet raw = parse_csv(a);
    return serialize_entry(x2_entry_state_from_open_raw(&raw));
  }
  if (scenario == "expParts") {
    const RegisterValueSet mant = parse_csv(a);
    const RegisterValueSet exp = parse_csv(b);
    return serialize_entry(x2_entry_state_from_exponent_parts(&mant, &exp));
  }
  if (scenario == "structParts") {
    const X2ShapeSet mant = parse_csv(a);
    const RegisterValueSet exp = parse_csv(b);
    return serialize_struct(x2_structural_entry_state_from_parts(&mant, &exp));
  }
  if (scenario == "vpParts") {
    const RegisterValueSet mant = parse_csv(a);
    const RegisterValueSet exp = parse_csv(b);
    return serialize_vp(x2_vp_context_state_from_exponent_parts(&mant, &exp));
  }
  if (scenario == "advDigitClosed")
    return serialize_entry(advance_decimal_digit_entry(closed_x2_entry_state(), b));
  if (scenario == "advDigitOpen") {
    const RegisterValueSet raw = parse_csv(a);
    return serialize_entry(advance_decimal_digit_entry(
        X2EntryState{.kind = X2EntryState::Kind::Open, .raw = raw}, b));
  }
  if (scenario == "advDigitExp")
    return serialize_entry(advance_decimal_digit_entry(exponent_entry(a, b), c));
  if (scenario == "advPoint") {
    const RegisterValueSet raw = parse_csv(a);
    return serialize_entry(
        advance_decimal_point_entry(X2EntryState{.kind = X2EntryState::Kind::Open, .raw = raw}));
  }
  if (scenario == "advExpDigit")
    return serialize_entry(advance_exponent_digit_entry(exponent_entry(a, b), c));
  if (scenario == "advStructExpDigit")
    return serialize_struct(advance_structural_exponent_digit_entry(structural_exponent(a, b), c));
  if (scenario == "signExp")
    return serialize_entry(sign_change_exponent_entry(exponent_entry(a, b)));
  if (scenario == "signStructExp")
    return serialize_struct(sign_change_structural_exponent_entry(structural_exponent(a, b)));
  if (scenario == "signVp") {
    const RegisterValueSet mant = parse_csv(a);
    const RegisterValueSet exp = parse_csv(b);
    return serialize_vp(sign_change_vp_context(
        X2VpContextState{.kind = X2VpContextState::Kind::Exponent, .mantissa = mant, .exponent = exp}));
  }
  if (scenario == "closedVals")
    return join_set(closed_exponent_entry_decimal_facts(exponent_entry(a, b)));
  if (scenario == "closedShapes")
    return join_set(closed_exponent_entry_shape_facts(exponent_entry(a, b)));
  if (scenario == "expShapes")
    return join_set(exponent_entry_shape_facts(exponent_entry(a, b)));
  if (scenario == "structExpShapes")
    return join_set(structural_exponent_entry_shape_facts(structural_exponent(a, b)));
  if (scenario == "signedDecimalEntry")
    return sign_changed_decimal_entry(a);
  if (scenario == "signedMantissaShapes") {
    const RegisterValueSet raw = parse_csv(a);
    const std::optional<RegisterValueSet> result = sign_changed_mantissa_shapes(raw);
    return result.has_value() ? join_set(*result) : std::string{"_"};
  }
  if (scenario == "structFromVpShapes") {
    const X2ShapeSet shapes = parse_csv(a);
    return serialize_struct(structural_exponent_entry_from_vp_entry_shapes(shapes));
  }
  if (scenario == "stateOpenDecimal") {
    const RegisterValueSet raw = parse_csv(a);
    const X2EntryState entry = x2_entry_state_from_open_raw(&raw);
    if (entry.kind != X2EntryState::Kind::Open)
      return "non-open";
    return serialize_state(
        x2_value_state_from_open_decimal_entry(entry, std::nullopt, std::nullopt, nullptr, nullptr, nullptr));
  }
  if (scenario == "stateExp")
    return serialize_state(x2_value_state_from_exponent_entry(exponent_entry(a, b), std::nullopt,
                                                              std::nullopt, nullptr, nullptr, nullptr));
  if (scenario == "stateStructExp")
    return serialize_state(x2_value_state_from_structural_exponent_entry(
        structural_exponent(a, b), std::nullopt, std::nullopt, nullptr, nullptr, nullptr));
  if (scenario == "stateMantissaShapes") {
    const RegisterValueSet raw = parse_csv(a);
    bool ok = true;
    const X2ValueDataflowState state = x2_value_state_from_mantissa_shapes(
        raw, std::nullopt, std::nullopt, nullptr, nullptr, nullptr, ok);
    return ok ? serialize_state(state) : std::string{"undefined"};
  }
  if (scenario == "stateStructShapes") {
    const X2ShapeSet shapes = parse_csv(a);
    return serialize_state(x2_value_state_from_structural_shapes(shapes, std::nullopt, std::nullopt,
                                                                 nullptr, nullptr, nullptr));
  }
  if (scenario == "signedDecimalVp") {
    const RegisterValueSet mant = parse_csv(a);
    const RegisterValueSet exp = parse_csv(b);
    const X2VpContextState context{
        .kind = X2VpContextState::Kind::Exponent, .mantissa = mant, .exponent = exp};
    X2ValueDataflowState input{};
    return serialize_state(x2_value_state_from_signed_decimal_vp_context(input, context));
  }
  if (scenario == "signedStructVp") {
    const X2ValueDataflowState input{};
    return serialize_state(
        x2_value_state_from_signed_structural_vp_context(input, structural_exponent(a, b)));
  }
  if (scenario == "closeExp") {
    X2ValueDataflowState input{};
    input.entry = exponent_entry(a, b);
    return serialize_state(close_x2_value_entry(input));
  }
  if (scenario == "closeStruct") {
    X2ValueDataflowState input{};
    input.structuralEntry = structural_exponent(a, b);
    return serialize_state(close_x2_value_entry(input));
  }
  return "UNKNOWN_SCENARIO";
}

// =========================================================================
// x2-literal-restore foundation helpers (faithful port of the helpers.ts
// expression-value-fact cluster imported by src/core/passes/x2-literal-restore.ts).
// =========================================================================

bool x2_value_set_has_restored_visible_decimal(const X2ValueSet* input, const X2ValueFact& fact) {
  const std::optional<std::string> visible = x2_value_fact_restored_visible_decimal(fact);
  if (!visible.has_value())
    return false;
  if (input == nullptr)
    return false;
  for (const X2ValueFact& candidate : *input) {
    if (x2_value_fact_restored_visible_decimal(candidate) == visible)
      return true;
  }
  return false;
}

std::optional<X2ValueFact> x2_stable_unary_expression_value_fact(const IrOp& op,
                                                                 const X2ValueFact& source) {
  if (op.kind != IrKind::Plain)
    return std::nullopt;
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op) || has_ir_roles(op))
    return std::nullopt;
  const std::optional<int> arity = stable_expression_opcode_arity(op.opcode);
  if (!arity.has_value() || *arity != 1)
    return std::nullopt;
  const std::optional<X2ValueFact> key = stable_expression_source_key(source, true);
  if (!key.has_value())
    return std::nullopt;
  if (stable_expression_key_has_concrete_decimal_result(op, *key))
    return std::nullopt;
  return stable_expression_value_fact(stable_expression_opcode_hex(op.opcode), *key);
}

std::set<X2ValueFact> x2_unary_expression_value_facts(const IrOp& op, const X2ValueFact& source) {
  if (op.kind != IrKind::Plain)
    return {};
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op) || has_ir_roles(op))
    return {};
  const std::optional<int> arity = stable_expression_opcode_arity(op.opcode);
  if (!arity.has_value() || *arity != 1)
    return {};
  const X2ValueSet source_set{source};
  std::set<X2ValueFact> output =
      plain_produces_concrete_decimal_values(op, &source_set, nullptr, ConcreteEvaluationOptions{}, nullptr);
  const std::optional<X2ValueFact> stable = x2_stable_unary_expression_value_fact(op, source);
  if (stable.has_value())
    output.insert(*stable);
  return output;
}

std::optional<X2ValueFact> x2_stable_binary_expression_value_fact(const IrOp& op,
                                                                  const X2ValueFact& y_source,
                                                                  const X2ValueFact& x_source) {
  if (op.kind != IrKind::Plain)
    return std::nullopt;
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op) || has_ir_roles(op))
    return std::nullopt;
  const std::optional<int> arity = stable_expression_opcode_arity(op.opcode);
  if (!arity.has_value() || *arity != 2)
    return std::nullopt;
  const std::optional<X2ValueFact> y_key = stable_expression_source_key(y_source, true);
  const std::optional<X2ValueFact> x_key = stable_expression_source_key(x_source, true);
  if (!y_key.has_value() || !x_key.has_value())
    return std::nullopt;
  if (stable_binary_expression_key_has_concrete_decimal_result(op, *y_key, *x_key))
    return std::nullopt;
  return stable_binary_expression_value_fact(op, stable_expression_opcode_hex(op.opcode), *y_key,
                                             *x_key);
}

std::set<X2ValueFact> x2_binary_expression_value_facts(const IrOp& op, const X2ValueFact& y_source,
                                                       const X2ValueFact& x_source) {
  if (op.kind != IrKind::Plain)
    return {};
  if (has_rewrite_barrier(op) || is_display_focus_sensitive(op) || has_ir_roles(op))
    return {};
  const std::optional<int> arity = stable_expression_opcode_arity(op.opcode);
  if (!arity.has_value() || *arity != 2)
    return {};
  const X2ValueSet y_set{y_source};
  const X2ValueSet x_set{x_source};
  std::set<X2ValueFact> output = plain_produces_concrete_binary_decimal_values(
      op, &y_set, &x_set, nullptr, nullptr, ConcreteEvaluationOptions{}, nullptr, nullptr);
  const std::optional<X2ValueFact> stable =
      x2_stable_binary_expression_value_fact(op, y_source, x_source);
  if (stable.has_value())
    output.insert(*stable);
  return output;
}

std::set<X2ValueFact> x2_stable_constant_expression_value_facts(const IrOp& op) {
  if (op.kind != IrKind::Plain)
    return {};
  return plain_produces_stable_constant_expression_values(op);
}

// =========================================================================
// x2-literal-restore pass-local cluster (faithful 1:1 port of
// src/core/passes/x2-literal-restore.ts run() and its module-local helpers).
// =========================================================================
namespace litres {

constexpr int kDot = 0x0a;
constexpr int kSignChange = 0x0b;
constexpr int kVp = 0x0c;
constexpr int kStackLift = 0x0e;

struct NumericLiteralRun {
  int end = 0;
  std::string display_value;
  X2ValueFact x2_fact;
  bool dot_preserves_vp_entry_source = false;
};

struct ExpressionSourceRun {
  int end = 0;
  std::string display_value;
  std::vector<X2ValueFact> x2_facts;
  std::optional<int> source_stack_end;
  std::optional<bool> allow_duplicate_y_stack_proof;
};

struct UnaryExpressionRun {
  int end = 0;
  std::string display_value;
  std::vector<X2ValueFact> x2_facts;
  int source_stack_end = 0;
  bool allow_duplicate_y_stack_proof = false;
};

const IrOp* op_at(const std::vector<IrOp>& ops, long index) {
  if (index < 0 || index >= static_cast<long>(ops.size()))
    return nullptr;
  return &ops[static_cast<std::size_t>(index)];
}

bool has_roles(const IrOp& op) {
  return !op.meta.roles.empty();
}

std::string mnemonic_or(const IrOp& op, const char* fallback) {
  return op.meta.mnemonic.empty() ? std::string(fallback) : op.meta.mnemonic;
}

bool is_plain_digit(const IrOp& op) {
  return op.kind == IrKind::Plain && op.opcode >= 0 && op.opcode <= 9 && !has_rewrite_barrier(op) &&
         !is_display_focus_sensitive(op);
}

bool is_plain_dot(const IrOp* op) {
  return op != nullptr && op->kind == IrKind::Plain && op->opcode == kDot &&
         !has_rewrite_barrier(*op) && !is_display_focus_sensitive(*op);
}

bool is_plain_sign_change(const IrOp* op) {
  return op != nullptr && op->kind == IrKind::Plain && op->opcode == kSignChange &&
         !has_rewrite_barrier(*op) && !is_display_focus_sensitive(*op) && !has_roles(*op);
}

bool is_plain_vp(const IrOp* op) {
  return op != nullptr && op->kind == IrKind::Plain && op->opcode == kVp &&
         !has_rewrite_barrier(*op) && !is_display_focus_sensitive(*op);
}

bool is_plain_stack_lift_separator(const IrOp* op) {
  return op != nullptr && op->kind == IrKind::Plain && op->opcode == kStackLift &&
         !has_rewrite_barrier(*op) && !is_display_focus_sensitive(*op) && !has_roles(*op);
}

bool is_plain_expression_sync_gap_op(const IrOp* op) {
  if (op == nullptr || op->kind != IrKind::Plain || has_rewrite_barrier(*op) ||
      is_display_focus_sensitive(*op) || has_roles(*op))
    return false;
  const X2StackEffectAnalysis effect = analyze_x2_stack_effect(*op);
  return plain_preserves_x_value(*op) && effect.stack_preserves && effect.x2_preserves;
}

bool is_plain_x_preserving_x2_sync(const IrOp* op) {
  if (op == nullptr || op->kind != IrKind::Plain || has_rewrite_barrier(*op) ||
      is_display_focus_sensitive(*op))
    return false;
  const X2StackEffectAnalysis effect = analyze_x2_stack_effect(*op);
  return effect.stack_preserves && effect.x2_affects && plain_preserves_x_value(*op);
}

X2ValueFact register_value_fact(const std::string& register_name) {
  return "reg:" + register_name;
}

// --- numeric literal canonicalization (pass-local in TS) ------------------
std::string strip_leading_zeros_keep_one(const std::string& digits) {
  std::size_t start = 0;
  while (start + 1 < digits.size() && digits[start] == '0')
    ++start;
  return digits.substr(start);
}

std::optional<std::string> normalize_decimal_entry(const std::string& raw) {
  static const std::regex pattern(R"(^(-?)([0-9]{1,8})$)");
  std::smatch match;
  if (!std::regex_match(raw, match, pattern))
    return std::nullopt;
  const std::string sign = match[1].str();
  const std::string digits = strip_leading_zeros_keep_one(match[2].str());
  if (digits == "0")
    return std::string{"0"};
  return sign + digits;
}

std::optional<std::string> normalize_signed_plain_decimal(const std::string& raw) {
  static const std::regex pattern(R"(^(-?)([0-9]+)(?:\.([0-9]+))?$)");
  std::smatch match;
  if (!std::regex_match(raw, match, pattern))
    return std::nullopt;
  const std::string sign = match[1].str();
  const std::string integer = strip_leading_zeros_keep_one(match[2].str());
  std::string fraction = match[3].matched ? match[3].str() : "";
  while (!fraction.empty() && fraction.back() == '0')
    fraction.pop_back();
  const std::string unsigned_value = fraction.empty() ? integer : integer + "." + fraction;
  if (unsigned_value == "0")
    return std::string{"0"};
  return sign + unsigned_value;
}

std::optional<std::string> normalize_decimal_mantissa_entry_local(const std::string& raw) {
  static const std::regex pattern(R"(^(-?)([0-9]{1,8})(?:\.([0-9]+))?$)");
  std::smatch match;
  if (!std::regex_match(raw, match, pattern))
    return normalize_decimal_entry(raw);
  const std::string integer = match[2].str();
  const std::string fraction = match[3].matched ? match[3].str() : "";
  if (integer.size() + fraction.size() > 8)
    return std::nullopt;
  const std::string sign = match[1].str();
  if (!match[3].matched)
    return normalize_decimal_entry(sign + integer);
  return normalize_signed_plain_decimal(sign + integer + "." + fraction);
}

int significant_decimal_digits_local(const std::string& input) {
  const std::string unsigned_value = input.rfind('-', 0) == 0 ? input.substr(1) : input;
  const std::size_t dot = unsigned_value.find('.');
  const bool has_fraction = dot != std::string::npos;
  const std::string integer = has_fraction ? unsigned_value.substr(0, dot) : unsigned_value;
  const std::string fraction = has_fraction ? unsigned_value.substr(dot + 1) : "";
  std::string digits = integer + fraction;
  std::size_t lead = digits.find_first_not_of('0');
  digits = lead == std::string::npos ? "" : digits.substr(lead);
  std::string significant = digits;
  if (!has_fraction) {
    while (!significant.empty() && significant.back() == '0')
      significant.pop_back();
  }
  return significant.empty() ? 1 : static_cast<int>(significant.size());
}

std::string effective_exponent_mantissa_digits(const std::string& raw_digits) {
  std::size_t lead = raw_digits.find_first_not_of('0');
  const std::string stripped = lead == std::string::npos ? "" : raw_digits.substr(lead);
  if (!stripped.empty())
    return stripped;
  const std::size_t zeros = raw_digits.size() > 0 ? raw_digits.size() - 1 : 0;
  return "1" + std::string(zeros, '0');
}

struct ExponentMantissaParts {
  std::string sign;
  std::string digits;
  int scale = 0;
};

std::optional<ExponentMantissaParts> exponent_mantissa_decimal_parts(const std::string& mantissa) {
  static const std::regex integer_pattern(R"(^(-?)([0-9]{1,8})$)");
  static const std::regex fractional_pattern(R"(^(-?)([0-9]{1,8})\.([0-9]+)$)");
  std::smatch match;
  if (std::regex_match(mantissa, match, integer_pattern)) {
    ExponentMantissaParts parts;
    parts.sign = match[1].str() == "-" ? "-" : "";
    parts.digits = effective_exponent_mantissa_digits(match[2].str());
    parts.scale = 0;
    return parts;
  }
  if (!std::regex_match(mantissa, match, fractional_pattern))
    return std::nullopt;
  const std::string integer_digits = match[2].str();
  const std::string fraction_digits = match[3].str();
  if (integer_digits.size() + fraction_digits.size() > 8)
    return std::nullopt;
  ExponentMantissaParts parts;
  parts.sign = match[1].str() == "-" ? "-" : "";
  parts.digits = integer_digits + fraction_digits;
  parts.scale = static_cast<int>(fraction_digits.size());
  return parts;
}

std::optional<std::string> normalized_exponent_entry_value(const std::string& mantissa,
                                                           const std::string& exponent) {
  static const std::regex exponent_pattern(R"(^(-?)([0-9]{1,2})$)");
  std::smatch exponent_match;
  const bool exponent_ok = std::regex_match(exponent, exponent_match, exponent_pattern);
  const std::optional<ExponentMantissaParts> mantissa_parts = exponent_mantissa_decimal_parts(mantissa);
  if (!mantissa_parts.has_value() || !exponent_ok)
    return std::nullopt;
  const int shift = std::stoi(exponent_match[2].str());
  const int scale = exponent_match[1].str() == "-" ? mantissa_parts->scale + shift
                                                   : mantissa_parts->scale - shift;
  if (static_cast<int>(mantissa_parts->digits.size()) + std::max(0, -scale) > 80)
    return std::nullopt;
  const std::optional<std::string> unsigned_value = scaled_decimal_digits(mantissa_parts->digits, scale);
  if (!unsigned_value.has_value())
    return std::nullopt;
  const std::optional<std::string> normalized =
      normalize_signed_plain_decimal(mantissa_parts->sign + *unsigned_value);
  if (!normalized.has_value() || significant_decimal_digits_local(*normalized) > 8)
    return std::nullopt;
  return normalized;
}

std::optional<X2ValueFact> decimal_entry_fact(const std::string& raw) {
  const std::optional<std::string> normalized = normalize_decimal_mantissa_entry_local(raw);
  if (!normalized.has_value())
    return std::nullopt;
  return decimal_value_fact(raw, raw == *normalized ? "normalized" : "unnormalized");
}

std::optional<NumericLiteralRun> decimal_literal_run_at(const std::vector<IrOp>& ops, int start,
                                                        bool allow_single_positive_integer) {
  std::vector<char> digits;
  int end = start;
  while (end < static_cast<int>(ops.size())) {
    const IrOp& op = ops[static_cast<std::size_t>(end)];
    if (!is_plain_digit(op))
      break;
    digits.push_back(static_cast<char>('0' + op.opcode));
    end += 1;
  }
  if (digits.empty())
    return std::nullopt;
  std::vector<char> fraction_digits;
  const bool has_point = is_plain_dot(op_at(ops, end));
  if (has_point) {
    end += 1;
    while (end < static_cast<int>(ops.size())) {
      const IrOp& op = ops[static_cast<std::size_t>(end)];
      if (!is_plain_digit(op))
        break;
      fraction_digits.push_back(static_cast<char>('0' + op.opcode));
      end += 1;
    }
    if (fraction_digits.empty())
      return std::nullopt;
  }
  if (digits.size() + fraction_digits.size() > 8)
    return std::nullopt;
  const std::string sign = is_plain_sign_change(op_at(ops, end)) ? "-" : "";
  const std::string digit_str(digits.begin(), digits.end());
  const std::string fraction_str(fraction_digits.begin(), fraction_digits.end());
  const std::string unsigned_value = has_point ? digit_str + "." + fraction_str : digit_str;
  const std::string raw = sign + unsigned_value;
  const std::optional<std::string> normalized = normalize_decimal_mantissa_entry_local(raw);
  if (!normalized.has_value())
    return std::nullopt;
  const std::optional<X2ValueFact> x2_fact = decimal_entry_fact(raw);
  if (!x2_fact.has_value())
    return std::nullopt;
  const bool dot_preserves_vp_entry_source = !has_point && raw == *normalized && *normalized != "0";
  if (sign.empty()) {
    if (!allow_single_positive_integer && digits.size() < 2 && !has_point)
      return std::nullopt;
    return NumericLiteralRun{end - 1, raw, *x2_fact, dot_preserves_vp_entry_source};
  }
  return NumericLiteralRun{end, raw, *x2_fact, dot_preserves_vp_entry_source};
}

std::optional<NumericLiteralRun> exponent_literal_run_at(const std::vector<IrOp>& ops, int start) {
  std::vector<char> mantissa_digits;
  int cursor = start;
  while (cursor < static_cast<int>(ops.size())) {
    const IrOp& op = ops[static_cast<std::size_t>(cursor)];
    if (!is_plain_digit(op))
      break;
    mantissa_digits.push_back(static_cast<char>('0' + op.opcode));
    cursor += 1;
  }
  std::vector<char> fraction_digits;
  const bool has_point = is_plain_dot(op_at(ops, cursor));
  if (has_point) {
    cursor += 1;
    while (cursor < static_cast<int>(ops.size())) {
      const IrOp& op = ops[static_cast<std::size_t>(cursor)];
      if (!is_plain_digit(op))
        break;
      fraction_digits.push_back(static_cast<char>('0' + op.opcode));
      cursor += 1;
    }
    if (fraction_digits.empty())
      return std::nullopt;
  }
  const std::string mantissa_sign = is_plain_sign_change(op_at(ops, cursor)) ? "-" : "";
  if (mantissa_sign == "-")
    cursor += 1;
  if (mantissa_digits.empty() || mantissa_digits.size() + fraction_digits.size() > 8 ||
      !is_plain_vp(op_at(ops, cursor)))
    return std::nullopt;
  cursor += 1;

  std::vector<char> exponent_digits;
  while (cursor < static_cast<int>(ops.size())) {
    const IrOp& op = ops[static_cast<std::size_t>(cursor)];
    if (!is_plain_digit(op))
      break;
    exponent_digits.push_back(static_cast<char>('0' + op.opcode));
    cursor += 1;
  }
  if (exponent_digits.empty() || exponent_digits.size() > 2)
    return std::nullopt;
  const std::string exponent_sign = is_plain_sign_change(op_at(ops, cursor)) ? "-" : "";
  if (exponent_sign == "-")
    cursor += 1;

  const std::string mantissa_digit_str(mantissa_digits.begin(), mantissa_digits.end());
  const std::string fraction_str(fraction_digits.begin(), fraction_digits.end());
  const std::string mantissa = has_point
                                   ? mantissa_sign + mantissa_digit_str + "." + fraction_str
                                   : mantissa_sign + mantissa_digit_str;
  const std::string exponent_str(exponent_digits.begin(), exponent_digits.end());
  const std::optional<std::string> value =
      normalized_exponent_entry_value(mantissa, exponent_sign + exponent_str);
  if (!value.has_value())
    return std::nullopt;
  const bool dot_preserves_vp_entry_source = mantissa_digits.front() != '0';
  return NumericLiteralRun{cursor - 1, *value, decimal_value_fact(*value, "normalized"),
                           dot_preserves_vp_entry_source};
}

std::vector<NumericLiteralRun> literal_runs_at(const std::vector<IrOp>& ops, int start) {
  const std::optional<NumericLiteralRun> exponent = exponent_literal_run_at(ops, start);
  const std::optional<NumericLiteralRun> decimal = decimal_literal_run_at(ops, start, false);
  if (!exponent.has_value())
    return decimal.has_value() ? std::vector<NumericLiteralRun>{*decimal} : std::vector<NumericLiteralRun>{};
  if (!decimal.has_value() || decimal->end == exponent->end)
    return {*exponent};
  return {*exponent, *decimal};
}

// --- expression source/run scanning ---------------------------------------
std::optional<std::string> register_expression_source_register(const IrOp& op) {
  if (op.kind == IrKind::Recall)
    return op.register_name;
  if (op.kind != IrKind::IndirectRecall || !is_stable_indirect_selector(op.register_name))
    return std::nullopt;
  return known_indirect_memory_target(op);
}

std::optional<ExpressionSourceRun> register_expression_source_run_at(const std::vector<IrOp>& ops,
                                                                     int start) {
  const IrOp* op = op_at(ops, start);
  if (op == nullptr || has_rewrite_barrier(*op) || is_display_focus_sensitive(*op))
    return std::nullopt;
  const std::optional<std::string> register_name = register_expression_source_register(*op);
  if (!register_name.has_value())
    return std::nullopt;
  ExpressionSourceRun out;
  out.end = start;
  out.display_value = "R" + *register_name;
  out.x2_facts = {register_value_fact(*register_name)};
  return out;
}

std::optional<ExpressionSourceRun> stable_constant_expression_source_run_at(const std::vector<IrOp>& ops,
                                                                            int start) {
  const IrOp* op = op_at(ops, start);
  if (op == nullptr || op->kind != IrKind::Plain)
    return std::nullopt;
  const std::set<X2ValueFact> facts = x2_stable_constant_expression_value_facts(*op);
  if (facts.empty())
    return std::nullopt;
  ExpressionSourceRun out;
  out.end = start;
  out.display_value = mnemonic_or(*op, "const");
  out.x2_facts.assign(facts.begin(), facts.end());
  return out;
}

std::optional<ExpressionSourceRun> expression_source_run_at(const std::vector<IrOp>& ops, int start) {
  const std::optional<NumericLiteralRun> exponent = exponent_literal_run_at(ops, start);
  if (exponent.has_value()) {
    ExpressionSourceRun out;
    out.end = exponent->end;
    out.display_value = exponent->display_value;
    out.x2_facts = {exponent->x2_fact};
    return out;
  }
  const std::optional<NumericLiteralRun> decimal = decimal_literal_run_at(ops, start, true);
  if (decimal.has_value()) {
    ExpressionSourceRun out;
    out.end = decimal->end;
    out.display_value = decimal->display_value;
    out.x2_facts = {decimal->x2_fact};
    return out;
  }
  const std::optional<ExpressionSourceRun> reg = register_expression_source_run_at(ops, start);
  if (reg.has_value())
    return reg;
  return stable_constant_expression_source_run_at(ops, start);
}

std::vector<X2ValueFact> stable_unary_expression_value_facts_for_source(
    const IrOp& unary, const std::vector<X2ValueFact>& source_facts) {
  std::set<X2ValueFact> output;
  for (const X2ValueFact& source_fact : source_facts) {
    for (const X2ValueFact& x2_fact : x2_unary_expression_value_facts(unary, source_fact))
      output.insert(x2_fact);
  }
  return std::vector<X2ValueFact>(output.begin(), output.end());
}

std::vector<X2ValueFact> binary_expression_value_facts_for_sources(const IrOp& binary,
                                                                   const ExpressionSourceRun& y_source,
                                                                   const ExpressionSourceRun& x_source) {
  std::set<X2ValueFact> output;
  for (const X2ValueFact& y_fact : y_source.x2_facts) {
    for (const X2ValueFact& x_fact : x_source.x2_facts) {
      for (const X2ValueFact& x2_fact : x2_binary_expression_value_facts(binary, y_fact, x_fact))
        output.insert(x2_fact);
    }
  }
  return std::vector<X2ValueFact>(output.begin(), output.end());
}

// --- terminal-boundary and gap classification -----------------------------
bool is_rpn_expression_non_executable_gap(const IrOp* op) {
  return op != nullptr && op->kind == IrKind::OrphanAddress && !has_rewrite_barrier(*op);
}

bool is_removable_expression_return_call_body_gap_op(const IrOp& op) {
  return is_plain_expression_sync_gap_op(&op) || is_rpn_expression_non_executable_gap(&op);
}

bool return_call_target_address_is_stable_for_terminal_boundary(
    const IrOp& op, const DirectReturnAnalysisContext& context, int immutable_before_index) {
  if (op.kind == IrKind::Call) {
    if (std::holds_alternative<std::string>(op.target))
      return true;
    const auto found = context.addresses.find(std::get<int>(op.target));
    return found != context.addresses.end() && found->second < immutable_before_index;
  }
  const std::optional<int> target = known_indirect_flow_target(op);
  if (!target.has_value())
    return false;
  const auto found = context.addresses.find(*target);
  return found != context.addresses.end() && found->second < immutable_before_index;
}

bool is_removable_expression_return_call_gap(const std::vector<IrOp>& ops, const IrOp* op,
                                             const DirectReturnAnalysisContext& context,
                                             int immutable_before_index) {
  return op != nullptr && is_known_return_call_op(*op) &&
         return_call_target_address_is_stable_for_terminal_boundary(*op, context,
                                                                    immutable_before_index) &&
         known_return_call_returns_through_nested_transparent_range(
             ops, *op, context, is_removable_expression_return_call_body_gap_op);
}

bool is_rpn_expression_gap_op(const std::vector<IrOp>& ops, int cursor,
                              const DirectReturnAnalysisContext& context, int immutable_before_index) {
  const IrOp* op = op_at(ops, cursor);
  return is_plain_expression_sync_gap_op(op) || is_rpn_expression_non_executable_gap(op) ||
         is_removable_expression_return_call_gap(ops, op, context, immutable_before_index);
}

struct DirectBranchTargetEntry {
  int entry = 0;
  int visit_key = 0;
};

std::optional<DirectBranchTargetEntry> direct_branch_target_entry_for_terminal_boundary(
    const IrOp& op, const DirectReturnAnalysisContext& context, int immutable_before_index) {
  if (std::holds_alternative<std::string>(op.target)) {
    const auto found = context.labels.find(std::get<std::string>(op.target));
    if (found == context.labels.end())
      return std::nullopt;
    return DirectBranchTargetEntry{found->second + 1, found->second};
  }
  const auto found = context.addresses.find(std::get<int>(op.target));
  if (found == context.addresses.end() || found->second >= immutable_before_index)
    return std::nullopt;
  return DirectBranchTargetEntry{found->second, found->second};
}

bool is_terminal_expression_boundary(const std::vector<IrOp>& ops, int cursor,
                                     const DirectReturnAnalysisContext& context,
                                     int immutable_before_index, std::set<int> visited) {
  const IrOp* op = op_at(ops, cursor);
  if (op != nullptr && (op->kind == IrKind::Label || op->kind == IrKind::OrphanAddress)) {
    return is_terminal_expression_boundary(ops, cursor + 1, context, immutable_before_index,
                                           std::move(visited));
  }
  if (op != nullptr && is_known_return_call_op(*op) &&
      return_call_target_address_is_stable_for_terminal_boundary(*op, context,
                                                                 immutable_before_index) &&
      x2_known_return_call_preserves_stack_x_and_x2(ops, *op, context)) {
    if (visited.count(cursor) != 0)
      return false;
    std::set<int> next_visited = visited;
    next_visited.insert(cursor);
    return is_terminal_expression_boundary(ops, cursor + 1, context, immutable_before_index,
                                           std::move(next_visited));
  }
  if (op != nullptr && op->kind == IrKind::Jump && std::holds_alternative<std::string>(op->target)) {
    const auto found = context.labels.find(std::get<std::string>(op->target));
    if (found == context.labels.end() || visited.count(found->second) != 0)
      return false;
    std::set<int> next_visited = visited;
    next_visited.insert(found->second);
    return is_terminal_expression_boundary(ops, found->second + 1, context, immutable_before_index,
                                           std::move(next_visited));
  }
  if (op != nullptr && op->kind == IrKind::Jump && std::holds_alternative<int>(op->target)) {
    const auto found = context.addresses.find(std::get<int>(op->target));
    if (found == context.addresses.end() || found->second >= immutable_before_index ||
        visited.count(found->second) != 0)
      return false;
    std::set<int> next_visited = visited;
    next_visited.insert(found->second);
    return is_terminal_expression_boundary(ops, found->second, context, immutable_before_index,
                                           std::move(next_visited));
  }
  if (op != nullptr && (op->kind == IrKind::CondJump || op->kind == IrKind::Loop)) {
    const std::optional<DirectBranchTargetEntry> target =
        direct_branch_target_entry_for_terminal_boundary(*op, context, immutable_before_index);
    if (!target.has_value() || visited.count(target->visit_key) != 0)
      return false;
    std::set<int> next_visited = visited;
    next_visited.insert(cursor);
    next_visited.insert(target->visit_key);
    return is_terminal_expression_boundary(ops, target->entry, context, immutable_before_index,
                                           next_visited) &&
           is_terminal_expression_boundary(ops, cursor + 1, context, immutable_before_index,
                                           next_visited);
  }
  if (op != nullptr && op->kind == IrKind::IndirectJump) {
    const std::optional<int> target = known_indirect_flow_target(*op);
    const auto found = target.has_value() ? context.addresses.find(*target) : context.addresses.end();
    if (!target.has_value() || found == context.addresses.end() ||
        found->second >= immutable_before_index || visited.count(found->second) != 0)
      return false;
    std::set<int> next_visited = visited;
    next_visited.insert(found->second);
    return is_terminal_expression_boundary(ops, found->second, context, immutable_before_index,
                                           std::move(next_visited));
  }
  if (op != nullptr && op->kind == IrKind::IndirectCondJump) {
    const std::optional<int> target = known_indirect_flow_target(*op);
    const auto found = target.has_value() ? context.addresses.find(*target) : context.addresses.end();
    if (!target.has_value() || found == context.addresses.end() ||
        found->second >= immutable_before_index || visited.count(found->second) != 0)
      return false;
    std::set<int> next_visited = visited;
    next_visited.insert(cursor);
    next_visited.insert(found->second);
    return is_terminal_expression_boundary(ops, found->second, context, immutable_before_index,
                                           next_visited) &&
           is_terminal_expression_boundary(ops, cursor + 1, context, immutable_before_index,
                                           next_visited);
  }
  return op == nullptr || op->kind == IrKind::Stop || op->kind == IrKind::Return;
}

// --- expression run builders ----------------------------------------------
std::optional<UnaryExpressionRun> rpn_expression_run_at(const std::vector<IrOp>& ops, int start,
                                                        const DirectReturnAnalysisContext& terminal_context) {
  std::vector<ExpressionSourceRun> stack;
  int cursor = start;
  bool saw_operator = false;

  while (cursor < static_cast<int>(ops.size())) {
    if (stack.size() == 1 && saw_operator && is_plain_x_preserving_x2_sync(op_at(ops, cursor))) {
      const ExpressionSourceRun& result = stack.front();
      UnaryExpressionRun out;
      out.end = cursor;
      out.display_value = result.display_value;
      out.x2_facts = result.x2_facts;
      out.source_stack_end = result.source_stack_end.value_or(result.end);
      out.allow_duplicate_y_stack_proof = result.allow_duplicate_y_stack_proof.value_or(true);
      return out;
    }

    if (stack.size() == 1 && saw_operator &&
        is_terminal_expression_boundary(ops, cursor, terminal_context, start, {})) {
      const ExpressionSourceRun& result = stack.front();
      UnaryExpressionRun out;
      out.end = result.end;
      out.display_value = result.display_value;
      out.x2_facts = result.x2_facts;
      out.source_stack_end = result.source_stack_end.value_or(result.end);
      out.allow_duplicate_y_stack_proof = result.allow_duplicate_y_stack_proof.value_or(true);
      return out;
    }

    if (!stack.empty() && is_rpn_expression_gap_op(ops, cursor, terminal_context, start)) {
      stack.back().end = cursor;
      cursor += 1;
      continue;
    }

    if (is_plain_stack_lift_separator(op_at(ops, cursor)) && !stack.empty()) {
      cursor += 1;
      continue;
    }

    const std::optional<ExpressionSourceRun> source = expression_source_run_at(ops, cursor);
    if (source.has_value()) {
      ExpressionSourceRun pushed = *source;
      pushed.source_stack_end = source->end;
      pushed.allow_duplicate_y_stack_proof = true;
      stack.push_back(pushed);
      cursor = source->end + 1;
      continue;
    }

    const IrOp* op = op_at(ops, cursor);
    if (op == nullptr || op->kind != IrKind::Plain)
      return std::nullopt;

    if (stack.size() >= 1) {
      const ExpressionSourceRun& source_run = stack.back();
      const std::vector<X2ValueFact> next_facts =
          stable_unary_expression_value_facts_for_source(*op, source_run.x2_facts);
      if (!next_facts.empty()) {
        ExpressionSourceRun updated;
        updated.end = cursor;
        updated.display_value = mnemonic_or(*op, "expr") + "(" + source_run.display_value + ")";
        updated.x2_facts = next_facts;
        updated.source_stack_end = source_run.source_stack_end.value_or(source_run.end);
        updated.allow_duplicate_y_stack_proof = source_run.allow_duplicate_y_stack_proof.value_or(true);
        stack.back() = updated;
        saw_operator = true;
        cursor += 1;
        while (is_rpn_expression_gap_op(ops, cursor, terminal_context, start)) {
          stack.back().end = cursor;
          cursor += 1;
        }
        continue;
      }
    }

    if (stack.size() >= 2) {
      ExpressionSourceRun x_source = stack.back();
      stack.pop_back();
      ExpressionSourceRun y_source = stack.back();
      stack.pop_back();
      const std::vector<X2ValueFact> next_facts =
          binary_expression_value_facts_for_sources(*op, y_source, x_source);
      if (!next_facts.empty()) {
        ExpressionSourceRun pushed;
        pushed.end = cursor;
        pushed.display_value =
            mnemonic_or(*op, "expr") + "(" + y_source.display_value + "," + x_source.display_value + ")";
        pushed.x2_facts = next_facts;
        pushed.source_stack_end = cursor;
        pushed.allow_duplicate_y_stack_proof = false;
        stack.push_back(pushed);
        saw_operator = true;
        cursor += 1;
        while (is_rpn_expression_gap_op(ops, cursor, terminal_context, start)) {
          stack.back().end = cursor;
          cursor += 1;
        }
        continue;
      }
      stack.push_back(y_source);
      stack.push_back(x_source);
    }

    return std::nullopt;
  }

  return std::nullopt;
}

std::optional<UnaryExpressionRun> unary_expression_run_from_single_source_at(const std::vector<IrOp>& ops,
                                                                             int start) {
  const std::optional<ExpressionSourceRun> source = expression_source_run_at(ops, start);
  if (!source.has_value())
    return std::nullopt;
  int cursor = source->end + 1;
  std::string display_value = source->display_value;
  std::vector<X2ValueFact> x2_facts = source->x2_facts;

  while (cursor < static_cast<int>(ops.size())) {
    const IrOp* unary = op_at(ops, cursor);
    if (unary == nullptr || unary->kind != IrKind::Plain)
      return std::nullopt;
    const std::vector<X2ValueFact> next_facts =
        stable_unary_expression_value_facts_for_source(*unary, x2_facts);
    if (next_facts.empty())
      return std::nullopt;
    display_value = mnemonic_or(*unary, "expr") + "(" + display_value + ")";
    x2_facts = next_facts;
    cursor += 1;

    while (is_plain_expression_sync_gap_op(op_at(ops, cursor)))
      cursor += 1;
    if (is_plain_x_preserving_x2_sync(op_at(ops, cursor))) {
      UnaryExpressionRun out;
      out.end = cursor;
      out.display_value = display_value;
      out.x2_facts = x2_facts;
      out.source_stack_end = source->end;
      out.allow_duplicate_y_stack_proof = true;
      return out;
    }
  }

  return std::nullopt;
}

std::optional<ExpressionSourceRun> expression_operand_run_at(const std::vector<IrOp>& ops, int start) {
  const std::optional<ExpressionSourceRun> source = expression_source_run_at(ops, start);
  if (!source.has_value())
    return std::nullopt;

  int cursor = source->end + 1;
  int end = source->end;
  std::string display_value = source->display_value;
  std::vector<X2ValueFact> x2_facts = source->x2_facts;

  while (cursor < static_cast<int>(ops.size())) {
    const IrOp* unary = op_at(ops, cursor);
    if (unary == nullptr || unary->kind != IrKind::Plain)
      break;
    const std::vector<X2ValueFact> next_facts =
        stable_unary_expression_value_facts_for_source(*unary, x2_facts);
    if (next_facts.empty())
      break;
    display_value = mnemonic_or(*unary, "expr") + "(" + display_value + ")";
    x2_facts = next_facts;
    end = cursor;
    cursor += 1;

    while (is_plain_expression_sync_gap_op(op_at(ops, cursor))) {
      end = cursor;
      cursor += 1;
    }
  }

  ExpressionSourceRun out;
  out.end = end;
  out.display_value = display_value;
  out.x2_facts = x2_facts;
  return out;
}

int binary_expression_x_source_start(const std::vector<IrOp>& ops, int start) {
  return is_plain_stack_lift_separator(op_at(ops, start)) ? start + 1 : start;
}

std::optional<UnaryExpressionRun> binary_expression_run_at(const std::vector<IrOp>& ops, int start) {
  const std::optional<ExpressionSourceRun> y_source = expression_operand_run_at(ops, start);
  if (!y_source.has_value())
    return std::nullopt;
  const int x_start = binary_expression_x_source_start(ops, y_source->end + 1);
  const std::optional<ExpressionSourceRun> x_source = expression_operand_run_at(ops, x_start);
  if (!x_source.has_value())
    return std::nullopt;
  const int binary_index = x_source->end + 1;
  const IrOp* binary = op_at(ops, binary_index);
  if (binary == nullptr || binary->kind != IrKind::Plain)
    return std::nullopt;
  const std::vector<X2ValueFact> binary_facts =
      binary_expression_value_facts_for_sources(*binary, *y_source, *x_source);
  if (binary_facts.empty())
    return std::nullopt;
  int cursor = binary_index + 1;
  std::string display_value =
      mnemonic_or(*binary, "expr") + "(" + y_source->display_value + "," + x_source->display_value + ")";
  std::vector<X2ValueFact> x2_facts = binary_facts;

  while (cursor < static_cast<int>(ops.size())) {
    while (is_plain_expression_sync_gap_op(op_at(ops, cursor)))
      cursor += 1;
    if (is_plain_x_preserving_x2_sync(op_at(ops, cursor))) {
      UnaryExpressionRun out;
      out.end = cursor;
      out.display_value = display_value;
      out.x2_facts = x2_facts;
      out.source_stack_end = binary_index;
      out.allow_duplicate_y_stack_proof = false;
      return out;
    }
    const IrOp* unary = op_at(ops, cursor);
    if (unary == nullptr || unary->kind != IrKind::Plain)
      return std::nullopt;
    const std::vector<X2ValueFact> next_facts =
        stable_unary_expression_value_facts_for_source(*unary, x2_facts);
    if (next_facts.empty())
      return std::nullopt;
    display_value = mnemonic_or(*unary, "expr") + "(" + display_value + ")";
    x2_facts = next_facts;
    cursor += 1;
  }

  return std::nullopt;
}

std::optional<UnaryExpressionRun> unary_expression_run_at(const std::vector<IrOp>& ops, int start,
                                                          const DirectReturnAnalysisContext& terminal_context) {
  std::optional<UnaryExpressionRun> rpn = rpn_expression_run_at(ops, start, terminal_context);
  if (rpn.has_value())
    return rpn;
  std::optional<UnaryExpressionRun> single = unary_expression_run_from_single_source_at(ops, start);
  if (single.has_value())
    return single;
  return binary_expression_run_at(ops, start);
}

NumericLiteralRun literal_run_with_removable_suffix(const std::vector<IrOp>& ops,
                                                    const NumericLiteralRun& run, int start,
                                                    const DirectReturnAnalysisContext& context) {
  int cursor = run.end + 1;
  int end = run.end;
  bool crossed_gap = false;
  while (is_rpn_expression_gap_op(ops, cursor, context, start)) {
    end = cursor;
    cursor += 1;
    crossed_gap = true;
  }

  if (is_plain_x_preserving_x2_sync(op_at(ops, cursor))) {
    NumericLiteralRun out = run;
    out.end = cursor;
    return out;
  }
  if (crossed_gap && is_terminal_expression_boundary(ops, cursor, context, start, {})) {
    NumericLiteralRun out = run;
    out.end = end;
    return out;
  }
  return run;
}

// --- stack-lift / vp-reachability proofs -----------------------------------
std::optional<int> address_stable_flow_target_index(const std::map<int, int>& addresses,
                                                     std::optional<int> target,
                                                     int numeric_target_must_be_before_index) {
  if (!target.has_value())
    return std::nullopt;
  const auto found = addresses.find(*target);
  if (found == addresses.end() || found->second >= numeric_target_must_be_before_index)
    return std::nullopt;
  return found->second;
}

struct VpReachVisitor {
  const std::vector<IrOp>& ops;
  const std::map<std::string, int>& labels;
  const std::map<int, int>& addresses;
  int numeric_target_must_be_before_index;
  std::set<std::string> visited;

  static std::string join(const std::vector<int>& stack) {
    std::string out;
    for (std::size_t i = 0; i < stack.size(); ++i) {
      if (i > 0)
        out += ",";
      out += std::to_string(stack[i]);
    }
    return out;
  }

  bool visit(int cursor, std::vector<int> return_stack) {
    for (int index = cursor; index < static_cast<int>(ops.size()); index += 1) {
      const std::string key = std::to_string(index) + ":" + join(return_stack);
      if (visited.count(key) != 0)
        return false;
      visited.insert(key);
      const IrOp& op = ops[static_cast<std::size_t>(index)];
      if (has_rewrite_barrier(op))
        return true;
      switch (op.kind) {
        case IrKind::Label:
        case IrKind::Store:
        case IrKind::IndirectStore:
        case IrKind::OrphanAddress:
          continue;
        case IrKind::Plain: {
          if (is_plain_vp(&op))
            return true;
          const X2StackEffectAnalysis effect = analyze_x2_stack_effect(op);
          if (effect.x2_preserves)
            continue;
          if (effect.x2_affects || effect.x2_restores)
            return false;
          return true;
        }
        case IrKind::Jump: {
          if (!std::holds_alternative<std::string>(op.target)) {
            const auto found = addresses.find(std::get<int>(op.target));
            if (found == addresses.end() || found->second >= numeric_target_must_be_before_index)
              return true;
            return visit(found->second, return_stack);
          }
          const auto found = labels.find(std::get<std::string>(op.target));
          return found == labels.end() ? true : visit(found->second + 1, return_stack);
        }
        case IrKind::CondJump:
        case IrKind::Loop: {
          const bool string_target = std::holds_alternative<std::string>(op.target);
          std::optional<int> target;
          if (string_target) {
            const auto found = labels.find(std::get<std::string>(op.target));
            if (found != labels.end())
              target = found->second;
          } else {
            const auto found = addresses.find(std::get<int>(op.target));
            if (found != addresses.end())
              target = found->second;
          }
          if (!string_target &&
              (!target.has_value() || *target >= numeric_target_must_be_before_index))
            return true;
          const bool branch = !target.has_value()
                                  ? true
                                  : visit(string_target ? *target + 1 : *target, return_stack);
          return branch || visit(index + 1, return_stack);
        }
        case IrKind::Call: {
          const bool string_target = std::holds_alternative<std::string>(op.target);
          std::optional<int> target;
          if (string_target) {
            const auto found = labels.find(std::get<std::string>(op.target));
            if (found != labels.end())
              target = found->second;
          } else {
            const auto found = addresses.find(std::get<int>(op.target));
            if (found != addresses.end())
              target = found->second;
          }
          if (!string_target &&
              (!target.has_value() || *target >= numeric_target_must_be_before_index))
            return true;
          if (!target.has_value() || return_stack.size() >= 5)
            return true;
          std::vector<int> next_stack;
          next_stack.reserve(return_stack.size() + 1);
          next_stack.push_back(index + 1);
          next_stack.insert(next_stack.end(), return_stack.begin(), return_stack.end());
          return visit(string_target ? *target + 1 : *target, std::move(next_stack));
        }
        case IrKind::IndirectJump: {
          const std::optional<int> target_index = address_stable_flow_target_index(
              addresses, known_indirect_flow_target(op), numeric_target_must_be_before_index);
          return !target_index.has_value() ? true : visit(*target_index, return_stack);
        }
        case IrKind::IndirectCall: {
          const std::optional<int> target_index = address_stable_flow_target_index(
              addresses, known_indirect_flow_target(op), numeric_target_must_be_before_index);
          if (!target_index.has_value() || return_stack.size() >= 5)
            return true;
          std::vector<int> next_stack;
          next_stack.reserve(return_stack.size() + 1);
          next_stack.push_back(index + 1);
          next_stack.insert(next_stack.end(), return_stack.begin(), return_stack.end());
          return visit(*target_index, std::move(next_stack));
        }
        case IrKind::IndirectCondJump: {
          const std::optional<int> target_index = address_stable_flow_target_index(
              addresses, known_indirect_flow_target(op), numeric_target_must_be_before_index);
          const bool branch = !target_index.has_value() ? true : visit(*target_index, return_stack);
          return branch || visit(index + 1, return_stack);
        }
        case IrKind::Return: {
          if (!return_stack.empty()) {
            const int next = return_stack.front();
            std::vector<int> rest(return_stack.begin() + 1, return_stack.end());
            return visit(next, std::move(rest));
          }
          return true;
        }
        case IrKind::Recall:
        case IrKind::IndirectRecall:
        case IrKind::Stop:
          return false;
      }
    }
    return false;
  }
};

bool literal_replacement_can_reach_vp_restore(const std::vector<IrOp>& ops, int start,
                                              int numeric_target_must_be_before_index) {
  const std::map<std::string, int> labels = label_indexes(ops);
  const std::map<int, int> addresses = address_indexes(ops);
  VpReachVisitor visitor{ops, labels, addresses, numeric_target_must_be_before_index, {}};
  return visitor.visit(start, {});
}

struct RedundantSync {
  bool value = false;
  bool display_value = false;
  bool shape = false;
};

bool replacing_literal_can_expose_context_sensitive_restore(
    const std::vector<IrOp>& ops, const NumericLiteralRun& run,
    const std::optional<X2ValueDataflowState>& state, int producer_index,
    const DirectReturnAnalysisContext& context,
    std::map<std::string, bool>& vp_reachability_cache, const RedundantSync& redundant_sync) {
  IrOp dot;
  dot.kind = IrKind::Plain;
  dot.opcode = kDot;
  dot.meta.mnemonic = ".";
  const std::optional<X2ValueDataflowState> replacement_dot_state = transfer_x2_value_state_for_edge(
      state, dot, X2DataflowEdgeKind::Normal, X2TransferStateOptions{.track_register_memory = true},
      producer_index);
  const NrDotReplacementPlan vp_source_plan = x2_plan_dot_replacement_vp_source(
      ops, run.end, state.has_value() ? &*state : nullptr,
      replacement_dot_state.has_value() ? &*replacement_dot_state : nullptr, &context);
  if (!run.dot_preserves_vp_entry_source &&
      vp_source_plan.source.replacementDotHasOnlyRestoreGapBeforeVp)
    return true;
  X2RestoreExposureOptions base_options;
  base_options.numeric_target_must_be_before_index = producer_index;
  if (!x2_sync_can_expose_context_sensitive_restore(ops, run.end, base_options))
    return false;
  if (run.dot_preserves_vp_entry_source &&
      (vp_source_plan.source.hasOnlyRestoreGapBeforeVp ||
       vp_source_plan.source.replacementDotHasOnlyRestoreGapBeforeVp))
    return false;

  bool can_reach_vp_restore;
  {
    const std::string cache_key = std::to_string(run.end + 1) + ":" + std::to_string(producer_index);
    const auto found = vp_reachability_cache.find(cache_key);
    if (found != vp_reachability_cache.end()) {
      can_reach_vp_restore = found->second;
    } else {
      can_reach_vp_restore =
          literal_replacement_can_reach_vp_restore(ops, run.end + 1, producer_index);
      vp_reachability_cache.emplace(cache_key, can_reach_vp_restore);
    }
  }

  X2RestoreExposureOptions redundant_options;
  redundant_options.redundant_sync_value = redundant_sync.value;
  redundant_options.redundant_sync_display_value = redundant_sync.display_value;
  redundant_options.redundant_sync_shape = redundant_sync.shape;
  redundant_options.numeric_target_must_be_before_index = producer_index;
  if (!can_reach_vp_restore &&
      !x2_sync_can_expose_context_sensitive_restore(ops, run.end, redundant_options))
    return false;
  return true;
}

bool replacing_literal_stack_lift_can_expose(const std::vector<IrOp>& ops, int run_start,
                                             const NumericLiteralRun& run,
                                             const std::optional<X2ValueDataflowState>& state,
                                             const DirectReturnAnalysisContext& context,
                                             const std::set<int>& invalidated_producer_indexes) {
  X2ReplacementStackLiftOptions options;
  options.invalidated_producer_indexes = &invalidated_producer_indexes;
  return plan_x2_replacement_stack_lift(
             ops, run_start, run.end, state, context,
             replacing_number_entry_can_expose_stack_lift(ops, run.end, run_start), options)
      .exposes_stack_lift;
}

bool replacing_expression_stack_lift_can_expose(const std::vector<IrOp>& ops, int run_start,
                                                const UnaryExpressionRun& run,
                                                const std::optional<X2ValueDataflowState>& state,
                                                const DirectReturnAnalysisContext& context,
                                                const std::set<int>& invalidated_producer_indexes) {
  X2ReplacementStackLiftOptions options;
  options.invalidated_producer_indexes = &invalidated_producer_indexes;
  options.allow_duplicate_y_stack_proof = run.allow_duplicate_y_stack_proof;
  return plan_x2_replacement_stack_lift(
             ops, run_start, run.source_stack_end, state, context,
             replacing_number_entry_can_expose_stack_lift(ops, run.end, run_start), options)
      .exposes_stack_lift;
}

void mark_replaced_stack_lift_producers(const std::vector<IrOp>& ops, int start, int end,
                                        std::set<int>& output) {
  for (int index = start; index < end; index += 1) {
    const IrOp* op = op_at(ops, index);
    if (op != nullptr && analyze_x2_stack_effect(*op).stack_lift_and_x2_sync)
      output.insert(index);
  }
}

bool run_contains_vp(const std::vector<IrOp>& ops, int start, int end) {
  for (int index = start; index <= end; index += 1) {
    if (is_plain_vp(op_at(ops, index)))
      return true;
  }
  return false;
}

bool can_replace_run_in_current_x2_context(const std::vector<IrOp>& ops, int start, int end,
                                           const std::optional<X2ValueDataflowState>& state) {
  const X2ValueDataflowState* state_ptr = state.has_value() ? &*state : nullptr;
  if (x2_state_is_closed_plain_context(state_ptr))
    return true;
  return x2_state_is_closed_dot_restore_value_context(state_ptr) && !run_contains_vp(ops, start, end);
}

bool x2_value_set_has_any_fact(const X2ValueSet* input, const std::vector<X2ValueFact>& facts) {
  for (const X2ValueFact& fact : facts) {
    if (x2_value_set_has_fact(input, fact))
      return true;
  }
  return false;
}

std::vector<X2LiteralReplacement> compute_x2_literal_restore(const std::vector<IrOp>& ops) {
  const std::vector<std::optional<X2ValueDataflowState>> x2_value_states =
      compute_x2_value_states(ops, X2ValueStatesOptions{.track_register_memory = true});
  const std::vector<bool> dot_safe_states = compute_x2_dot_restore_gap_states(ops);
  const std::vector<bool> immediate_sync_states = compute_x2_immediate_sync_states(ops);
  const DirectReturnAnalysisContext direct_return_context = direct_return_analysis_context(ops);
  std::map<std::string, bool> vp_reachability_cache;
  std::set<int> replaced_stack_lift_producer_indexes;
  std::vector<X2LiteralReplacement> replacements;

  for (int index = 0; index < static_cast<int>(ops.size()); index += 1) {
    const std::optional<X2ValueDataflowState>& state = x2_value_states[static_cast<std::size_t>(index)];
    const X2ValueSet* x2_ptr = state.has_value() ? &state->x2 : nullptr;
    const X2ShapeSet* x2_shape_ptr = state.has_value() ? &state->x2Shape : nullptr;
    const X2ValueDataflowState* state_ptr = state.has_value() ? &*state : nullptr;

    bool replaced = false;
    for (const NumericLiteralRun& base_run : literal_runs_at(ops, index)) {
      const NumericLiteralRun run_at_index =
          literal_run_with_removable_suffix(ops, base_run, index, direct_return_context);
      const bool exact_x2_fact = x2_value_set_has_fact(x2_ptr, run_at_index.x2_fact);
      const bool visible_decimal_x2_value_fact =
          x2_value_set_has_restored_visible_decimal(x2_ptr, run_at_index.x2_fact);
      const bool visible_decimal_x2_shape_fact =
          visible_decimal_x2_value_fact
              ? false
              : x2_value_shape_set_has_restored_visible_decimal(x2_ptr, x2_shape_ptr,
                                                                run_at_index.x2_fact);
      const bool visible_decimal_x2_display_value_fact = visible_decimal_x2_value_fact && !exact_x2_fact;
      const bool visible_decimal_x2_dot_safe_shape_fact =
          visible_decimal_x2_shape_fact && !x2_state_has_unsafe_dot_restore_shape_x2(state_ptr);
      const bool visible_decimal_x2_fact =
          visible_decimal_x2_value_fact || visible_decimal_x2_dot_safe_shape_fact;
      const bool source_proves_free_standing_restore =
          x2_value_set_has_normalized_decimal_fact(x2_ptr, run_at_index.x2_fact) || visible_decimal_x2_fact;
      if (can_replace_run_in_current_x2_context(ops, index, run_at_index.end, state) &&
          x2_can_use_source_dot_restore_at(
              ops, index, state_ptr, dot_safe_states[static_cast<std::size_t>(index)],
              immediate_sync_states[static_cast<std::size_t>(index)],
              source_proves_free_standing_restore, &direct_return_context) &&
          (exact_x2_fact || visible_decimal_x2_fact) &&
          !replacing_literal_stack_lift_can_expose(ops, index, run_at_index, state,
                                                   direct_return_context,
                                                   replaced_stack_lift_producer_indexes) &&
          !replacing_literal_can_expose_context_sensitive_restore(
              ops, run_at_index, state, index, direct_return_context, vp_reachability_cache,
              RedundantSync{exact_x2_fact, visible_decimal_x2_display_value_fact,
                            visible_decimal_x2_dot_safe_shape_fact})) {
        mark_replaced_stack_lift_producers(ops, index, run_at_index.end,
                                           replaced_stack_lift_producer_indexes);
        replacements.push_back(X2LiteralReplacement{index, run_at_index.end, run_at_index.display_value});
        index = run_at_index.end;
        replaced = true;
        break;
      }
    }
    if (replaced)
      continue;

    const std::optional<UnaryExpressionRun> expression_run =
        unary_expression_run_at(ops, index, direct_return_context);
    const bool expression_source_proves_free_standing_restore =
        x2_state_has_same_dot_restore_value_in_x_and_x2(state_ptr) &&
        !x2_state_has_unsafe_dot_restore_shape_x2(state_ptr);
    if (expression_run.has_value() &&
        can_replace_run_in_current_x2_context(ops, index, expression_run->end, state) &&
        x2_value_set_has_any_fact(x2_ptr, expression_run->x2_facts) &&
        x2_can_use_source_dot_restore_at(
            ops, index, state_ptr, dot_safe_states[static_cast<std::size_t>(index)],
            immediate_sync_states[static_cast<std::size_t>(index)],
            expression_source_proves_free_standing_restore, &direct_return_context) &&
        !replacing_expression_stack_lift_can_expose(ops, index, *expression_run, state,
                                                    direct_return_context,
                                                    replaced_stack_lift_producer_indexes) &&
        !replacing_literal_can_expose_context_sensitive_restore(
            ops,
            NumericLiteralRun{expression_run->end, expression_run->display_value,
                              expression_run->x2_facts.front(), false},
            state, index, direct_return_context, vp_reachability_cache,
            RedundantSync{true, false, false})) {
      mark_replaced_stack_lift_producers(ops, index, expression_run->end,
                                         replaced_stack_lift_producer_indexes);
      replacements.push_back(
          X2LiteralReplacement{index, expression_run->end, expression_run->display_value});
      index = expression_run->end;
      continue;
    }
  }

  return replacements;
}

}  // namespace litres

}  // namespace x2eval

} // namespace

std::string x2_display_cluster_debug(const X2ShapeFact& fact) {
  return x2eval::display_debug(fact);
}

std::string x2_decimal_fraction_part_shape_debug(const std::string& value) {
  return x2eval::fraction_part_shape_debug(value);
}

std::string x2_structural_unary_debug(int opcode, const X2ShapeFact& fact) {
  return x2eval::structural_unary_debug(opcode, fact);
}

std::string x2_structural_bitwise_binary_debug(int opcode, const X2ValueSet& y, const X2ValueSet& x,
                                              const X2ShapeSet& y_shape, const X2ShapeSet& x_shape) {
  return x2eval::structural_bitwise_binary_debug(opcode, y, x, y_shape, x_shape);
}

std::string x2_structural_hex_binary_debug(int opcode, const X2ValueSet& y, const X2ValueSet& x,
                                          const X2ShapeSet& y_shape, const X2ShapeSet& x_shape,
                                          const X2ShapeSet& direct_y_shape, const X2ShapeSet& direct_x_shape) {
  return x2eval::structural_hex_binary_debug(opcode, y, x, y_shape, x_shape, direct_y_shape, direct_x_shape);
}

std::string x2_stable_expression_debug(int opcode, bool has_producer, int producer_index,
                                       const X2ValueSet& x, const X2ValueSet& y, const X2ShapeSet& x_shape,
                                       const X2ShapeSet& y_shape, const X2ShapeSet& direct_x_shape,
                                       const X2ShapeSet& direct_y_shape) {
  return x2eval::stable_expression_debug(opcode, has_producer, producer_index, x, y, x_shape, y_shape,
                                         direct_x_shape, direct_y_shape);
}

std::string x2_state_builders_debug(const std::string& scenario, const std::string& a,
                                    const std::string& b, const std::string& c) {
  return x2eval::state_builders_debug(scenario, a, b, c);
}

std::string x2_vp_splice_debug(const std::string& x_shape, const std::string& x2_shape,
                               bool include_exponent_targets) {
  return x2eval::vp_splice_debug(x_shape, x2_shape, include_exponent_targets);
}

std::string x2_vp_entry_debug(int opcode, const std::string& x, const std::string& x2,
                              const std::string& x_shape, const std::string& x2_shape,
                              const std::string& in_vp_mantissa, const std::string& in_vp_sign_mantissa,
                              const std::string& in_vp_shape, const std::string& in_vp_sign_shape,
                              bool mantissa_transient, bool shape_transient) {
  return x2eval::vp_entry_debug(opcode, x, x2, x_shape, x2_shape, in_vp_mantissa, in_vp_sign_mantissa,
                                in_vp_shape, in_vp_sign_shape, mantissa_transient, shape_transient);
}

std::string x2_transfer_plain_debug(const std::string& opcodes_csv, bool use_producer) {
  return x2eval::transfer_plain_debug(opcodes_csv, use_producer);
}

std::string x2_join_debug(const std::string& csv_a, const std::string& csv_b, bool use_producer) {
  return x2eval::join_debug(csv_a, csv_b, use_producer);
}

std::vector<int> x2_noop_restore_removed_indexes(const std::vector<IrOp>& ops) {
  return x2eval::compute_x2_noop_restore_removed(ops);
}

bool x2_state_has_visible_unary_noop(const std::optional<X2ValueDataflowState>& state, int opcode) {
  return x2eval::x2_state_has_visible_unary_noop(state.has_value() ? &*state : nullptr, opcode);
}

bool x2_states_have_same_vp_entry_source(const std::optional<X2ValueDataflowState>& left,
                                         const std::optional<X2ValueDataflowState>& right) {
  return x2eval::x2_states_have_same_vp_entry_source(left.has_value() ? &*left : nullptr,
                                                     right.has_value() ? &*right : nullptr);
}

std::vector<X2HiddenTempReplacement>
x2_hidden_temp_restore_replacements(const std::vector<IrOp>& ops) {
  return x2eval::compute_x2_hidden_temp_restore(ops);
}

std::vector<X2LiteralReplacement> x2_literal_restore_replacements(const std::vector<IrOp>& ops) {
  return x2eval::litres::compute_x2_literal_restore(ops);
}

X2ValueDataflowState empty_x2_value_dataflow_state(bool track_register_memory) {
  return internal_empty_x2_value_dataflow_state(track_register_memory);
}

X2ValueDataflowState clone_x2_value_dataflow_state(const X2ValueDataflowState& input) {
  return internal_clone_x2_value_dataflow_state(input);
}

X2ValueDataflowState join_x2_value_dataflow_states(
    const std::optional<X2ValueDataflowState>& current, const X2ValueDataflowState& incoming,
    bool track_register_memory) {
  return x2eval::tx_join_x2_value_dataflow_states(current.has_value() ? &*current : nullptr, incoming,
                                                  track_register_memory);
}

bool same_x2_value_dataflow_state(const std::optional<X2ValueDataflowState>& left,
                                 const std::optional<X2ValueDataflowState>& right) {
  return x2eval::tx_same_x2_value_dataflow_state(left.has_value() ? &*left : nullptr,
                                                 right.has_value() ? &*right : nullptr);
}

// Faithful port of edgeTargetStartsWithVp: skipping leading labels / orphan
// addresses, does the edge target's first real op enter on a ВП (0x0c)?
bool edge_target_starts_with_vp(const std::vector<IrOp>& ops, int target) {
  for (std::size_t index = static_cast<std::size_t>(target); index < ops.size(); ++index) {
    const IrOp& op = ops[index];
    if (op.kind == IrKind::Label || op.kind == IrKind::OrphanAddress)
      continue;
    return op.kind == IrKind::Plain && op.opcode == 0x0c && !has_rewrite_barrier(op);
  }
  return false;
}

static std::vector<std::optional<X2ValueDataflowState>>
compute_x2_value_states_impl(const std::vector<IrOp>& ops, const X2ValueStatesOptions& options) {
  if (ops.empty()) {
    return {};
  }
  const std::vector<std::vector<RegisterValueEdge>> edges = build_register_value_graph(ops);
  std::vector<std::optional<X2ValueDataflowState>> in_states(ops.size());
  in_states.at(0) = internal_empty_x2_value_dataflow_state(options.track_register_memory);

  // Precompute edgeTargetStartsWithVp per target index once: it depends only on
  // the (immutable) op stream, so recomputing it inside the fixpoint loop is a
  // pure O(n) scan per edge per iteration. Memoizing keeps golden_listing fast.
  std::vector<char> target_starts_with_vp(ops.size());
  for (std::size_t target = 0; target < ops.size(); ++target) {
    target_starts_with_vp[target] =
        edge_target_starts_with_vp(ops, static_cast<int>(target)) ? 1 : 0;
  }

  bool changed = true;
  int iterations = 0;
  while (changed && iterations < 200) {
    changed = false;
    ++iterations;

    for (std::size_t index = 0; index < in_states.size(); ++index) {
      if (!in_states[index].has_value()) {
        continue;
      }
      for (const RegisterValueEdge& edge : edges[index]) {
        const std::size_t target = static_cast<std::size_t>(edge.target);
        const std::optional<X2ValueDataflowState> outgoing =
            transfer_x2_value_state_for_edge(
                in_states[index], ops[index], edge.kind,
                {.track_register_memory = options.track_register_memory,
                 .target_starts_with_vp =
                     target < target_starts_with_vp.size() &&
                     target_starts_with_vp[target] != 0},
                static_cast<int>(index));
        if (!outgoing.has_value()) {
          continue;
        }
        const X2ValueDataflowState dropped =
            x2eval::x2_value_edge_drops_unstable_opaque_expression_facts(
                ops[index], edge, static_cast<int>(index))
                ? x2eval::drop_unstable_opaque_expression_x2_value_facts(
                      *outgoing, options.track_register_memory)
                : *outgoing;
        const X2ValueDataflowState joined = join_x2_value_dataflow_states(
            in_states.at(target), dropped, options.track_register_memory);
        if (!same_x2_value_dataflow_state(in_states.at(target), joined)) {
          in_states.at(target) = joined;
          changed = true;
        }
      }
    }
  }

  return in_states;
}

// compute_x2_value_states is a pure function of (ops, track_register_memory):
// it only reads the op stream and the memory-tracking flag. The pass pipeline
// invokes it from many passes across several fixpoint iterations, almost always
// on the same (unchanged) IR, so the TS reference memoizes it via
// x2ValueStatesCache. We mirror that here with a thread_local content-keyed
// cache (full ir_ops_to_json key => no collisions) to keep golden_listing /
// regression compiles fast. The cache is capped to bound memory.
std::vector<std::optional<X2ValueDataflowState>>
compute_x2_value_states(const std::vector<IrOp>& ops, const X2ValueStatesOptions& options) {
  static thread_local std::unordered_map<std::string,
                                         std::vector<std::optional<X2ValueDataflowState>>>
      cache;
  std::string key = (options.track_register_memory ? "1\n" : "0\n") + ir_ops_to_json(ops);
  const auto found = cache.find(key);
  if (found != cache.end())
    return found->second;
  if (cache.size() >= 8192)
    cache.clear();
  std::vector<std::optional<X2ValueDataflowState>> result =
      compute_x2_value_states_impl(ops, options);
  cache.emplace(std::move(key), result);
  return result;
}

std::optional<X2ValueDataflowState>
transfer_x2_value_state_for_edge(const std::optional<X2ValueDataflowState>& input, const IrOp& op,
                                X2DataflowEdgeKind edge, const X2TransferStateOptions& options,
                                std::optional<int> producer_index) {
  if (!input.has_value()) {
    return std::nullopt;
  }
  return internal_transfer_x2_value_dataflow_state(*input, op, edge, options.track_register_memory,
                                                  producer_index, options.target_starts_with_vp);
}

std::optional<X2ValueDataflowState>
transfer_x2_value_state_through_known_transparent_return_call(
    const std::optional<X2ValueDataflowState>& input, const IrOp& call,
    const X2TransferStateOptions& options, std::optional<int> producer_index) {
  if (!input.has_value()) {
    return std::nullopt;
  }
  return transfer_x2_value_state_for_edge(input, call, X2DataflowEdgeKind::Jump, options,
                                         producer_index);
}

namespace {

std::optional<X2ValueDataflowState> transfer_x2_value_state_through_transparent_return_range(
    const std::vector<IrOp>& ops, int target_index,
    const std::optional<X2ValueDataflowState>& input,
    const DirectReturnAnalysisContext& context, const X2TransferStateOptions& options,
    std::set<int>& active) {
  if (!input.has_value() || active.contains(target_index)) {
    return std::nullopt;
  }

  const int start_index = ops.at(static_cast<std::size_t>(target_index)).kind == IrKind::Label
                              ? target_index + 1
                              : target_index;
  active.insert(target_index);
  std::optional<X2ValueDataflowState> state = input;
  for (int index = start_index; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label) {
      if (context.label_entries.contains(index)) {
        active.erase(target_index);
        return std::nullopt;
      }
      state = transfer_x2_value_state_for_edge(state, op, X2DataflowEdgeKind::Normal, options,
                                               index);
      continue;
    }
    if (has_rewrite_barrier(op) || is_display_focus_sensitive(op)) {
      active.erase(target_index);
      return std::nullopt;
    }
    if (op.kind == IrKind::Return) {
      active.erase(target_index);
      return transfer_x2_value_state_for_edge(state, op, X2DataflowEdgeKind::Normal, options,
                                             index);
    }
    if (is_known_return_call_op(op)) {
      if (!x2_known_return_call_preserves_stack_x_and_x2(ops, op, context)) {
        active.erase(target_index);
        return std::nullopt;
      }
      const std::optional<int> nested_target = known_return_call_target_index(op, context);
      const std::optional<X2ValueDataflowState> entered =
          transfer_x2_value_state_for_edge(state, op, X2DataflowEdgeKind::Jump, options, index);
      state = nested_target.has_value()
                  ? transfer_x2_value_state_through_transparent_return_range(
                        ops, *nested_target, entered, context, options, active)
                  : std::nullopt;
      if (!state.has_value()) {
        active.erase(target_index);
        return std::nullopt;
      }
      continue;
    }
    if (!x2_is_stack_x_and_x2_preserving_linear_op(op)) {
      active.erase(target_index);
      return std::nullopt;
    }
    state = transfer_x2_value_state_for_edge(state, op, X2DataflowEdgeKind::Normal, options,
                                             index);
  }
  active.erase(target_index);
  return std::nullopt;
}

} // namespace

std::optional<X2ValueDataflowState>
transfer_x2_value_state_through_known_transparent_return_call(
    const std::vector<IrOp>& ops, const IrOp& call,
    const std::optional<X2ValueDataflowState>& input,
    const DirectReturnAnalysisContext& context, const X2TransferStateOptions& options,
    std::optional<int> producer_index) {
  if (!input.has_value() ||
      !x2_known_return_call_preserves_stack_x_and_x2(ops, call, context)) {
    return std::nullopt;
  }
  const std::optional<int> target_index = known_return_call_target_index(call, context);
  if (!target_index.has_value()) {
    return std::nullopt;
  }
  const std::optional<X2ValueDataflowState> entered =
      transfer_x2_value_state_for_edge(input, call, X2DataflowEdgeKind::Jump, options,
                                       producer_index);
  std::set<int> active;
  return transfer_x2_value_state_through_transparent_return_range(
      ops, *target_index, entered, context, options, active);
}

// --- x2PlanVpSpliceCandidatesAt (public forwarder) ------------------------
std::vector<X2VpSpliceCandidate> x2_plan_vp_splice_candidates_at(
    const std::vector<IrOp>& ops, int index,
    const std::vector<std::optional<X2ValueDataflowState>>& states,
    const DirectReturnAnalysisContext& context, const X2VpSplicePlannerOptions& options) {
  return x2eval::plan_vp_splice_candidates(ops, index, states, context, options);
}

} // namespace mkpro::core::passes
