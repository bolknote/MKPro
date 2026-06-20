#include "mkpro/core/passes/helpers.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>

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
      break;
    case IrKind::IndirectCondJump:
      if (const std::optional<int> target = known_indirect_flow_target(op))
        jump_to_address(*target);
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

std::optional<int> known_indirect_flow_target(const IrOp& op) {
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
  if (target < 0 || target > 104)
    return std::nullopt;
  return target;
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
    if (op.kind == IrKind::Call || op.kind == IrKind::IndirectCall)
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

bool x2_state_has_same_visible_x_and_y(
    const std::optional<X2ValueDataflowState>& state) {
  if (!state.has_value())
    return false;
  return optional_x2_value_sets_intersect(state->x, state->y) ||
         optional_shape_sets_intersect(state->xShape, state->yShape);
}

std::optional<RecallValueProof>
recall_value_proof(const IrOp& op, const std::optional<X2ValueDataflowState>& state) {
  const std::optional<std::string> register_name = removable_recall_value_register(op);
  if (!register_name.has_value() || !state.has_value())
    return std::nullopt;

  bool in_x = x2_value_set_has_register(state->x, *register_name);
  bool x2_sync_value = false;
  if (state->memory.has_value()) {
    const auto memory = state->memory->find(*register_name);
    if (memory != state->memory->end()) {
      in_x = in_x || x2_value_sets_intersect(memory->second, state->x);
      x2_sync_value = x2_value_sets_intersect(memory->second, state->x2);
    }
  }

  const bool x2_sync_register = x2_value_set_has_register(state->x2, *register_name);
  return RecallValueProof{
      .register_name = *register_name,
      .in_x = in_x,
      .x2_sync_register = x2_sync_register ? std::optional<std::string>{*register_name}
                                           : std::nullopt,
      .x2_sync_value = x2_sync_value,
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

  const bool exposes_stack_lift = removing_recall_can_expose_stack_lift(ops, recall_index);
  X2RestoreExposureOptions exposure_options;
  exposure_options.redundant_sync_register = redundant_sync_register;
  exposure_options.redundant_sync_value = redundant_sync_value;
  const bool exposes_x2_restore =
      removing_recall_can_expose_x2_restore(ops, recall_index, exposure_options);
  (void)context;

  return RecallRemovalAnalysis{
      .register_name = *register_name,
      .value_proof = value_proof,
      .redundant_sync_register = redundant_sync_register,
      .redundant_sync_value = redundant_sync_value,
      .redundant_sync_display_value = false,
      .redundant_sync_shape = false,
      .x2_sync_redundant = redundant_sync_register.has_value() || redundant_sync_value,
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
  const std::optional<int> producer =
      analysis->exposes_stack_lift && !analysis->exposes_x2_restore
          ? x2_previous_stack_lift_duplicate_y_producer_index(
                ops, stack_scheduler_start, stack_exposure_end, stack_scheduler_state, context)
          : std::nullopt;
  const bool producer_removed =
      producer.has_value() && options.removed_indexes != nullptr &&
      options.removed_indexes->contains(*producer);
  const bool stack_lift_already_supplied = producer.has_value() && !producer_removed;

  return RecallRemovalStackSchedulerPlan{
      .analysis = *analysis,
      .stack_lift_producer_index = producer,
      .stack_lift_already_supplied = stack_lift_already_supplied,
      .removable = analysis->removable || stack_lift_already_supplied,
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

void internal_clear_x2_vp_entry_fields(X2ValueDataflowState& input) {
  input.vpEntryMantissa = std::nullopt;
  input.vpEntrySignMantissa = std::nullopt;
  input.vpEntryShape = std::nullopt;
  input.vpEntrySignShape = std::nullopt;
  input.vpEntryMantissaTransient = false;
  input.vpEntryShapeTransient = false;
}

X2ValueSet internal_recall_x2_values(const X2ValueDataflowState& input,
                                     const std::string& register_name,
                                     bool track_register_memory) {
  if (track_register_memory && input.memory.has_value()) {
    const auto found = input.memory->find(register_name);
    if (found != input.memory->end() && !found->second.empty()) {
      return found->second;
    }
  }
  return X2ValueSet{internal_x2_value_fact_for_register(register_name)};
}

X2ValueSet internal_remove_x2_value(const X2ValueSet& input, const X2ValueFact& fact) {
  X2ValueSet output = input;
  output.erase(fact);
  return output;
}

X2ValueSet internal_sync_unknown_same_value(X2ValueSet x, X2Effect effect,
                                           std::optional<int> producer_index) {
  if (effect != X2Effect::Affects || !x.empty()) {
    return x;
  }
  x.insert(producer_index.has_value() ? "expr:" + std::to_string(*producer_index)
                                      : kSameUnknownValue);
  return x;
}

X2ValueSet internal_join_x2_set(const X2ValueSet& current, const X2ValueSet& incoming) {
  X2ValueSet output;
  for (const X2ValueFact& value : current) {
    if (incoming.contains(value)) {
      output.insert(value);
    }
  }
  return output;
}

X2ShapeSet internal_join_shape_set(const X2ShapeSet& current, const X2ShapeSet& incoming) {
  X2ShapeSet output;
  for (const X2ShapeFact& value : current) {
    if (incoming.contains(value)) {
      output.insert(value);
    }
  }
  return output;
}

X2ValueDataflowState internal_close_x2_value_entry(const X2ValueDataflowState& input) {
  X2ValueDataflowState output = internal_clone_x2_value_dataflow_state(input);
  output.entry = X2EntryState{.kind = X2EntryState::Kind::Closed};
  return output;
}

X2ValueDataflowState internal_invalidate_register_dependency(
    const X2ValueDataflowState& input, const std::string& register_name,
    bool track_register_memory) {
  X2ValueDataflowState output = internal_clone_x2_value_dataflow_state(input);
  const X2ValueFact fact = internal_x2_value_fact_for_register(register_name);
  output.x = internal_remove_x2_value(output.x, fact);
  output.x2 = internal_remove_x2_value(output.x2, fact);
  if (output.y.has_value()) {
    std::set<X2ValueFact> y = *output.y;
    y.erase(fact);
    output.y = y;
  }
  if (track_register_memory && output.memory.has_value()) {
    output.memory->erase(register_name);
  }
  if (track_register_memory && output.shapeMemory.has_value()) {
    output.shapeMemory->erase(register_name);
  }
  return output;
}

X2ValueDataflowState internal_transfer_x2_value_dataflow_state(
    const X2ValueDataflowState& input, const IrOp& op, X2DataflowEdgeKind edge,
    bool track_register_memory, std::optional<int> producer_index) {
  if (has_rewrite_barrier(op)) {
    return internal_empty_x2_value_dataflow_state(track_register_memory);
  }

  switch (op.kind) {
  case IrKind::Label:
  case IrKind::OrphanAddress:
    return internal_clone_x2_value_dataflow_state(input);
  case IrKind::Jump:
  case IrKind::Call: {
    return internal_close_x2_value_entry(input);
  }
  case IrKind::Store: {
    X2ValueDataflowState output = internal_close_x2_value_entry(input);
    const X2ValueFact fact = internal_x2_value_fact_for_register(op.register_name);
    output.x = output.x;
    output.x.insert(fact);
    output.x2 = output.x;
    output.y = clone_optional_set(input.y);
    output.vpContext = X2VpContextState{.kind = X2VpContextState::Kind::None};
    if (track_register_memory) {
      if (!output.memory.has_value()) {
        output.memory = X2ValueMemory{};
      }
      (*output.memory)[op.register_name] = output.x;
      if (!output.shapeMemory.has_value()) {
        output.shapeMemory = X2ShapeMemory{};
      }
      (*output.shapeMemory)[op.register_name] = X2ShapeSet{};
    }
    return output;
  }
  case IrKind::IndirectStore: {
    const std::optional<std::string> target = known_indirect_memory_target(op);
    if (!target.has_value()) {
      return internal_close_x2_value_entry(input);
    }
    X2ValueDataflowState output = internal_close_x2_value_entry(input);
    const X2ValueFact fact = internal_x2_value_fact_for_register(*target);
    output.x = output.x;
    output.x.insert(fact);
    output.x2 = output.x;
    output.vpContext = X2VpContextState{.kind = X2VpContextState::Kind::None};
    if (track_register_memory) {
      if (!output.memory.has_value()) {
        output.memory = X2ValueMemory{};
      }
      (*output.memory)[*target] = output.x;
      if (!output.shapeMemory.has_value()) {
        output.shapeMemory = X2ShapeMemory{};
      }
      (*output.shapeMemory)[*target] = X2ShapeSet{};
    }
    return output;
  }
  case IrKind::Recall: {
    const X2ValueSet values = internal_recall_x2_values(input, op.register_name,
                                                        track_register_memory);
    X2ValueDataflowState output = internal_close_x2_value_entry(input);
    output.x = values;
    output.x2 = values;
    output.y = X2ValueSet{input.x.begin(), input.x.end()};
    output.yShape = X2ShapeSet{};
    output.xShape = X2ShapeSet{};
    output.x2Shape = X2ShapeSet{};
    output.xDirectShape = input.xDirectShape;
    output.yDirectShape = input.yDirectShape;
    output.vpContext = X2VpContextState{.kind = X2VpContextState::Kind::None};
    output.structuralEntry = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None};
    output.structuralVpContext = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None};
    internal_clear_x2_vp_entry_fields(output);
    return output;
  }
  case IrKind::IndirectRecall: {
    const std::optional<std::string> target = known_indirect_memory_target(op);
    const X2ValueSet values =
        target.has_value()
            ? internal_recall_x2_values(input, *target, track_register_memory)
            : X2ValueSet{kSameUnknownValue};
    X2ValueDataflowState output = internal_close_x2_value_entry(input);
    output.x = values;
    output.x2 = values;
    output.y = X2ValueSet{input.x.begin(), input.x.end()};
    output.yShape = X2ShapeSet{};
    output.xShape = X2ShapeSet{};
    output.x2Shape = X2ShapeSet{};
    output.vpContext = X2VpContextState{.kind = X2VpContextState::Kind::None};
    output.structuralEntry = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None};
    output.structuralVpContext = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None};
    internal_clear_x2_vp_entry_fields(output);
    return output;
  }
  case IrKind::Plain: {
    const X2Effect effect = plain_x2_effect(op);
    const OpcodeInfo& info = opcode_by_code(op.opcode);
    const X2ValueSet source_x = X2ValueSet{input.x.begin(), input.x.end()};
    const X2ShapeSet source_x_shape = X2ShapeSet{input.xShape.begin(), input.xShape.end()};
    X2ValueDataflowState output = internal_close_x2_value_entry(input);
    if (info.stack_effect == StackEffect::Shifts) {
      output.y = source_x;
      output.yShape = source_x_shape;
    } else if (info.stack_effect == StackEffect::Preserves ||
               (info.stack_effect == StackEffect::ConsumeYKeep &&
                info.risk == OpcodeRisk::Documented)) {
      output.y = clone_optional_set(input.y);
      output.yShape = clone_optional_set(input.yShape);
    } else {
      output.y = X2ValueSet{};
      output.yShape = X2ShapeSet{};
    }
    output.x = plain_preserves_x_value(op) ? X2ValueSet{input.x.begin(), input.x.end()}
                                           : X2ValueSet{};
    output.x2 = effect == X2Effect::Preserves ? X2ValueSet{input.x2.begin(), input.x2.end()}
                                             : internal_sync_unknown_same_value(
                                                   output.x, effect, producer_index);
    output.xShape = effect == X2Effect::Preserves ? X2ShapeSet{input.xShape.begin(), input.xShape.end()}
                                                  : X2ShapeSet{};
    output.x2Shape = effect == X2Effect::Preserves ? X2ShapeSet{input.x2Shape.begin(), input.x2Shape.end()}
                                                   : X2ShapeSet{};
    output.vpContext = effect == X2Effect::Preserves
                           ? input.vpContext
                           : X2VpContextState{.kind = X2VpContextState::Kind::None};
    output.structuralVpContext =
        effect == X2Effect::Preserves ? input.structuralVpContext
                                      : X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None};
    if (effect != X2Effect::Preserves) {
      internal_clear_x2_vp_entry_fields(output);
      output.structuralEntry = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None};
    }
    return output;
  }
  case IrKind::CondJump: {
    const X2Effect effect = conditional_x2_effect_for_graph_edge(op, edge);
    X2ValueDataflowState output = internal_close_x2_value_entry(input);
    output.y = clone_optional_set(input.y);
    output.x = internal_sync_unknown_same_value(X2ValueSet{input.x.begin(), input.x.end()}, effect,
                                               producer_index);
    output.x2 = effect == X2Effect::Preserves
                    ? X2ValueSet{input.x2.begin(), input.x2.end()}
                    : internal_sync_unknown_same_value(output.x, effect, producer_index);
    output.vpContext = effect == X2Effect::Preserves
                           ? input.vpContext
                           : X2VpContextState{.kind = X2VpContextState::Kind::None};
    output.structuralVpContext = effect == X2Effect::Preserves
                                    ? input.structuralVpContext
                                    : X2StructuralEntryState{
                                          .kind = X2StructuralEntryState::Kind::None};
    if (effect != X2Effect::Preserves) {
      internal_clear_x2_vp_entry_fields(output);
      output.structuralEntry = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None};
    }
    return output;
  }
  case IrKind::Loop: {
    const X2Effect effect = conditional_x2_effect_for_graph_edge(op, edge);
    const X2ValueDataflowState stable =
        internal_invalidate_register_dependency(input, op.counter, track_register_memory);
    X2ValueDataflowState output = internal_close_x2_value_entry(stable);
    const std::string counter_fact = internal_x2_value_fact_for_register(op.counter);
    output.y = clone_optional_set(stable.y);
    output.x = internal_sync_unknown_same_value(internal_remove_x2_value(stable.x, counter_fact),
                                               effect, producer_index);
    if (effect == X2Effect::Preserves) {
      output.x2 = internal_remove_x2_value(stable.x2, counter_fact);
    } else if (effect == X2Effect::Affects) {
      output.x2 = internal_sync_unknown_same_value(output.x, X2Effect::Affects, producer_index);
    } else {
      output.x2 = X2ValueSet{};
    }
    if (effect != X2Effect::Preserves) {
      output.structuralEntry = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None};
      internal_clear_x2_vp_entry_fields(output);
    }
    return output;
  }
  case IrKind::IndirectJump:
  case IrKind::IndirectCall: {
    X2ValueDataflowState output = internal_close_x2_value_entry(input);
    if (!mkpro::core::is_stable_indirect_selector(op.register_name)) {
      const X2ValueFact fact = internal_x2_value_fact_for_register(op.register_name);
      output.x = internal_remove_x2_value(output.x, fact);
      output.x2 = internal_remove_x2_value(output.x2, fact);
      if (track_register_memory && output.memory.has_value()) {
        output.memory->erase(op.register_name);
      }
      if (track_register_memory && output.shapeMemory.has_value()) {
        output.shapeMemory->erase(op.register_name);
      }
    }
    return output;
  }
  case IrKind::IndirectCondJump: {
    const X2Effect effect = conditional_x2_effect_for_graph_edge(op, edge);
    X2ValueDataflowState output = internal_close_x2_value_entry(input);
    output.y = clone_optional_set(input.y);
    output.x = internal_sync_unknown_same_value(X2ValueSet{input.x.begin(), input.x.end()}, effect,
                                               producer_index);
    output.x2 = effect == X2Effect::Preserves
                    ? X2ValueSet{input.x2.begin(), input.x2.end()}
                    : internal_sync_unknown_same_value(output.x, effect, producer_index);
    if (edge == X2DataflowEdgeKind::Jump &&
        !mkpro::core::is_stable_indirect_selector(op.register_name)) {
      const X2ValueFact fact = internal_x2_value_fact_for_register(op.register_name);
      output.x = internal_remove_x2_value(output.x, fact);
      output.x2 = internal_remove_x2_value(output.x2, fact);
      if (track_register_memory && output.memory.has_value()) {
        output.memory->erase(op.register_name);
      }
      if (track_register_memory && output.shapeMemory.has_value()) {
        output.shapeMemory->erase(op.register_name);
      }
    }
    return output;
  }
  case IrKind::Stop:
    return internal_empty_x2_value_dataflow_state(track_register_memory);
  case IrKind::Return: {
    X2ValueDataflowState output = internal_close_x2_value_entry(input);
    output.x = internal_sync_unknown_same_value(output.x, X2Effect::Affects, producer_index);
    output.x2 = output.x;
    output.vpContext = X2VpContextState{.kind = X2VpContextState::Kind::None};
    output.structuralEntry = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None};
    output.structuralVpContext = X2StructuralEntryState{.kind = X2StructuralEntryState::Kind::None};
    internal_clear_x2_vp_entry_fields(output);
    return output;
  }
  }
  return internal_empty_x2_value_dataflow_state(track_register_memory);
}

} // namespace

X2ValueDataflowState empty_x2_value_dataflow_state(bool track_register_memory) {
  return internal_empty_x2_value_dataflow_state(track_register_memory);
}

X2ValueDataflowState clone_x2_value_dataflow_state(const X2ValueDataflowState& input) {
  return internal_clone_x2_value_dataflow_state(input);
}

X2ValueDataflowState join_x2_value_dataflow_states(
    const std::optional<X2ValueDataflowState>& current, const X2ValueDataflowState& incoming,
    bool track_register_memory) {
  if (!current.has_value()) {
    X2ValueDataflowState output = internal_clone_x2_value_dataflow_state(incoming);
    if (!track_register_memory) {
      output.memory = std::nullopt;
      output.shapeMemory = std::nullopt;
    }
    return output;
  }

  const X2ValueDataflowState& left = *current;
  X2ValueDataflowState output = internal_clone_x2_value_dataflow_state(left);
  output.x = internal_join_x2_set(left.x, incoming.x);
  output.x2 = internal_join_x2_set(left.x2, incoming.x2);
  output.y = internal_join_x2_set(left.y.value_or(X2ValueSet{}), incoming.y.value_or(X2ValueSet{}));
  output.xShape = internal_join_shape_set(left.xShape, incoming.xShape);
  output.yShape = internal_join_shape_set(left.yShape.value_or(X2ShapeSet{}),
                                         incoming.yShape.value_or(X2ShapeSet{}));
  output.x2Shape = internal_join_shape_set(left.x2Shape, incoming.x2Shape);
  output.xDirectShape = internal_join_shape_set(left.xDirectShape.value_or(X2ShapeSet{}),
                                                incoming.xDirectShape.value_or(X2ShapeSet{}));
  output.yDirectShape = internal_join_shape_set(left.yDirectShape.value_or(X2ShapeSet{}),
                                                incoming.yDirectShape.value_or(X2ShapeSet{}));
  output.vpContext = left.vpContext;
  output.structuralEntry = left.structuralEntry;
  output.structuralVpContext = left.structuralVpContext;
  output.vpEntryMantissa = left.vpEntryMantissa;
  output.vpEntrySignMantissa = left.vpEntrySignMantissa;
  output.vpEntryShape = left.vpEntryShape;
  output.vpEntrySignShape = left.vpEntrySignShape;
  output.vpEntryMantissaTransient = left.vpEntryMantissaTransient || incoming.vpEntryMantissaTransient;
  output.vpEntryShapeTransient = left.vpEntryShapeTransient || incoming.vpEntryShapeTransient;

  if (!track_register_memory) {
    output.memory = std::nullopt;
    output.shapeMemory = std::nullopt;
    return output;
  }
  if (!left.memory.has_value() || !incoming.memory.has_value()) {
    output.memory = X2ValueMemory{};
    output.shapeMemory = X2ShapeMemory{};
    return output;
  }
  output.memory = X2ValueMemory{};
  for (const auto& [name, left_values] : *left.memory) {
    auto incoming_found = incoming.memory->find(name);
    if (incoming_found == incoming.memory->end()) {
      continue;
    }
    const X2ValueSet joined = internal_join_x2_set(left_values, incoming_found->second);
    if (!joined.empty()) {
      output.memory->insert_or_assign(name, joined);
    }
  }

  output.shapeMemory = X2ShapeMemory{};
  if (!left.shapeMemory.has_value() || !incoming.shapeMemory.has_value()) {
    output.shapeMemory = X2ShapeMemory{};
    return output;
  }
  output.shapeMemory = X2ShapeMemory{};
  for (const auto& [name, left_values] : *left.shapeMemory) {
    auto incoming_found = incoming.shapeMemory->find(name);
    if (incoming_found == incoming.shapeMemory->end()) {
      continue;
    }
    const X2ShapeSet joined = internal_join_shape_set(left_values, incoming_found->second);
    if (!joined.empty()) {
      output.shapeMemory->insert_or_assign(name, joined);
    }
  }
  return output;
}

bool same_x2_value_dataflow_state(const std::optional<X2ValueDataflowState>& left,
                                 const std::optional<X2ValueDataflowState>& right) {
  if (!left.has_value() || !right.has_value()) {
    return left.has_value() == right.has_value();
  }
  const X2ValueDataflowState& l = *left;
  const X2ValueDataflowState& r = *right;
  return l.x == r.x && l.y == r.y && l.x2 == r.x2 && l.xShape == r.xShape &&
         l.yShape == r.yShape && l.x2Shape == r.x2Shape &&
         l.xDirectShape == r.xDirectShape && l.yDirectShape == r.yDirectShape &&
         l.entry.kind == r.entry.kind && l.vpContext.kind == r.vpContext.kind &&
         l.structuralEntry.kind == r.structuralEntry.kind &&
         l.structuralVpContext.kind == r.structuralVpContext.kind &&
         l.vpEntryMantissa == r.vpEntryMantissa &&
         l.vpEntryMantissaTransient == r.vpEntryMantissaTransient &&
         l.vpEntrySignMantissa == r.vpEntrySignMantissa &&
         l.vpEntryShape == r.vpEntryShape && l.vpEntrySignShape == r.vpEntrySignShape &&
         l.vpEntryShapeTransient == r.vpEntryShapeTransient &&
         l.memory == r.memory && l.shapeMemory == r.shapeMemory;
}

std::vector<std::optional<X2ValueDataflowState>>
compute_x2_value_states(const std::vector<IrOp>& ops, const X2ValueStatesOptions& options) {
  if (ops.empty()) {
    return {};
  }
  const std::vector<std::vector<RegisterValueEdge>> edges = build_register_value_graph(ops);
  std::vector<std::optional<X2ValueDataflowState>> in_states(ops.size());
  in_states.at(0) = internal_empty_x2_value_dataflow_state(options.track_register_memory);

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
        const std::optional<X2ValueDataflowState> outgoing =
            transfer_x2_value_state_for_edge(in_states[index], ops[index], edge.kind,
                                            {.track_register_memory = options.track_register_memory},
                                            static_cast<int>(index));
        if (!outgoing.has_value()) {
          continue;
        }
        const std::size_t target = static_cast<std::size_t>(edge.target);
        const X2ValueDataflowState joined = join_x2_value_dataflow_states(
            in_states.at(target), *outgoing, options.track_register_memory);
        if (!same_x2_value_dataflow_state(in_states.at(target), joined)) {
          in_states.at(target) = joined;
          changed = true;
        }
      }
    }
  }

  return in_states;
}

std::optional<X2ValueDataflowState>
transfer_x2_value_state_for_edge(const std::optional<X2ValueDataflowState>& input, const IrOp& op,
                                X2DataflowEdgeKind edge, const X2TransferStateOptions& options,
                                std::optional<int> producer_index) {
  if (!input.has_value()) {
    return std::nullopt;
  }
  return internal_transfer_x2_value_dataflow_state(*input, op, edge, options.track_register_memory,
                                                  producer_index);
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

} // namespace mkpro::core::passes
