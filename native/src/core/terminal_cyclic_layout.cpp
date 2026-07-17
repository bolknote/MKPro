#include "mkpro/core/terminal_cyclic_layout.hpp"

#include "mkpro/core/stack_value_equivalence.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <array>
#include <compare>
#include <deque>
#include <exception>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core {

namespace {

constexpr int kReturnOpcode = 0x52;
constexpr int kStopOpcode = 0x50;
constexpr int kJumpOpcode = 0x51;
constexpr int kCallOpcode = 0x53;
constexpr int kDirectConditionFirst = 0x57;
constexpr int kDirectConditionLast = 0x5e;
constexpr int kDirectStoreFirst = 0x40;
constexpr int kDirectStoreLast = 0x4e;
constexpr int kDirectRecallFirst = 0x60;
constexpr int kDirectRecallLast = 0x6e;

struct ArtifactIndex {
  std::vector<int> item_addresses;
  std::map<int, std::size_t> cell_items;
  std::map<std::string, int> label_addresses;
  std::set<std::string> duplicate_labels;
  int cells = 0;
};

struct ExecutionState {
  int pc = -1;
  std::vector<int> returns;
  auto operator<=>(const ExecutionState&) const = default;
};

struct TailCandidate {
  std::size_t mask = 0;
  std::size_t fraction = 0;
  std::size_t condition = 0;
  std::size_t operand = 0;
  std::size_t dot = 0;
  std::size_t stop = 0;
  std::size_t continuation = 0;
  std::size_t direct_return = 0;
  std::size_t zero_return = 0;
};

struct ContinuationAnalysis {
  bool proved = false;
  int terminal_register = -1;
  std::vector<std::string> reasons;
};

ArtifactIndex index_artifact(const std::vector<MachineItem>& items) {
  ArtifactIndex index;
  index.item_addresses.resize(items.size(), 0);
  int address = 0;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    index.item_addresses.at(item_index) = address;
    if (item.kind == MachineItemKind::Label) {
      if (index.label_addresses.contains(item.name))
        index.duplicate_labels.insert(item.name);
      index.label_addresses[item.name] = address;
      continue;
    }
    index.cell_items[address] = item_index;
    ++address;
  }
  index.cells = address;
  return index;
}

void add_reason(std::vector<std::string>& reasons, std::string reason) {
  if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end())
    reasons.push_back(std::move(reason));
}

std::optional<std::size_t> next_cell_item(const std::vector<MachineItem>& items,
                                          std::size_t after) {
  for (++after; after < items.size(); ++after) {
    if (items.at(after).kind != MachineItemKind::Label)
      return after;
  }
  return std::nullopt;
}

std::optional<std::size_t> previous_cell_item(const std::vector<MachineItem>& items,
                                              std::size_t before) {
  while (before > 0U) {
    --before;
    if (items.at(before).kind != MachineItemKind::Label)
      return before;
  }
  return std::nullopt;
}

bool is_op(const std::vector<MachineItem>& items, std::size_t item_index, int opcode) {
  return item_index < items.size() && items.at(item_index).kind == MachineItemKind::Op &&
         items.at(item_index).opcode == opcode;
}

bool is_indirect_flow_opcode(int opcode) {
  const int family = opcode & 0xf0;
  return family == 0x70 || family == 0x80 || family == 0x90 || family == 0xa0 || family == 0xc0 ||
         family == 0xe0;
}

bool is_indirect_memory_opcode(int opcode) {
  const int family = opcode & 0xf0;
  return family == 0xb0 || family == 0xd0;
}

bool is_indirect_store_opcode(int opcode) {
  return (opcode & 0xf0) == 0xb0;
}

bool is_indirect_call_opcode(int opcode) {
  return (opcode & 0xf0) == 0xa0;
}

bool is_indirect_conditional_opcode(int opcode) {
  const int family = opcode & 0xf0;
  return family == 0x70 || family == 0x90 || family == 0xc0 || family == 0xe0;
}

std::optional<int> encoded_register(int opcode) {
  if ((opcode >= kDirectStoreFirst && opcode <= kDirectStoreLast) ||
      (opcode >= kDirectRecallFirst && opcode <= kDirectRecallLast)) {
    return opcode & 0x0f;
  }
  if (opcode == 0x4f || opcode == 0x6f)
    return 0;
  const int family = opcode & 0xf0;
  if (family >= 0x70 && family <= 0xe0) {
    const int low = opcode & 0x0f;
    return low == 0x0f ? 0 : low;
  }
  if (opcode == 0x58)
    return 2;
  if (opcode == 0x5a)
    return 3;
  if (opcode == 0x5b)
    return 1;
  if (opcode == 0x5d)
    return 0;
  return std::nullopt;
}

std::optional<int> normalized_register_index(const std::string& text) {
  try {
    const int index = register_index(register_from_text(text));
    return index >= 0 && index <= 0x0e ? std::optional<int>(index) : std::nullopt;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::string register_text(int index) {
  constexpr std::string_view names = "0123456789abcde";
  if (index < 0 || index >= static_cast<int>(names.size()))
    return {};
  return std::string(1, names.at(static_cast<std::size_t>(index)));
}

std::optional<int> resolved_target(const MachineItem& operand, const ArtifactIndex& index,
                                   AddressSpaceModel model) {
  if (operand.kind == MachineItemKind::Op) {
    try {
      return formal_address_info(operand.opcode, model).actual;
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }
  if (operand.kind != MachineItemKind::Address)
    return std::nullopt;
  if (operand.formal_opcode.has_value()) {
    try {
      return formal_address_info(*operand.formal_opcode, model).actual;
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }
  if (const auto* numeric = std::get_if<int>(&operand.target))
    return *numeric;
  const auto* label = std::get_if<std::string>(&operand.target);
  if (label == nullptr)
    return std::nullopt;
  const auto found = index.label_addresses.find(*label);
  return found == index.label_addresses.end() ? std::nullopt : std::optional<int>(found->second);
}

bool rewrite_direct_operand(MachineItem& operand, int target) {
  if (operand.kind == MachineItemKind::Address) {
    operand.target = target;
    operand.formal_opcode.reset();
    return true;
  }
  if (operand.kind != MachineItemKind::Op)
    return false;
  const std::optional<std::string> comment = operand.comment;
  const std::vector<std::string> roles = operand.roles;
  operand = MachineItem::address(target);
  operand.comment = comment;
  operand.roles = roles;
  return true;
}

std::optional<std::string> label_for_address(const ArtifactIndex& index, int address) {
  for (const auto& [label, label_address] : index.label_addresses) {
    if (label_address == address)
      return label;
  }
  return std::nullopt;
}

void rewrite_direct_operand_to_label(MachineItem& operand, const std::string& label) {
  const std::optional<std::string> comment = operand.comment;
  const std::vector<std::string> roles = operand.roles;
  operand = MachineItem::address(label);
  operand.comment = comment;
  operand.roles = roles;
}

bool executable_address(const std::vector<MachineItem>& items, const ArtifactIndex& index,
                        int address) {
  const auto cell = index.cell_items.find(address);
  return cell != index.cell_items.end() && items.at(cell->second).kind == MachineItemKind::Op;
}

std::optional<int> sequential_successor(const ArtifactIndex& index, int address,
                                        AddressSpaceModel address_space_model) {
  if (address + 1 < index.cells)
    return address + 1;
  if (index.cells == official_program_step_limit(address_space_model) &&
      address == index.cells - 1) {
    return 0;
  }
  return std::nullopt;
}

bool validate_complete_control_flow(const std::vector<MachineItem>& items,
                                    const ArtifactIndex& index,
                                    const AuthoritativePostLayoutControlFlow& control,
                                    const TerminalCyclicLayoutOptions& options,
                                    std::vector<std::string>& reasons) {
  (void)index;
  if (!control.proved || !control.reasons.empty()) {
    add_reason(reasons, "post-layout control-flow input is not an authoritative proof");
    for (const std::string& reason : control.reasons)
      add_reason(reasons, "input CFG: " + reason);
    return false;
  }

  const PostLayoutExternalEntryState* main = nullptr;
  for (const PostLayoutExternalEntryState& entry : control.external_entries) {
    if (entry.kind != ExternalEntryKind::Main)
      continue;
    if (main != nullptr) {
      add_reason(reasons, "post-layout proof contains more than one main entry");
      return false;
    }
    main = &entry;
  }
  if (main == nullptr || !main->return_stack.empty() || main->manual_interaction.has_value()) {
    add_reason(reasons, "post-layout proof has no exact empty-stack main entry");
    return false;
  }

  const PostLayoutControlFlowOptions cfg_options{
      .address_space_model = options.address_space_model,
      .maximum_return_depth = options.maximum_return_depth,
      .maximum_execution_states = static_cast<std::size_t>(options.maximum_execution_states),
      .main_entry = main->entry.address,
      .empty_return_target =
          control.empty_return_target.has_value()
              ? std::optional<IrTarget>{control.empty_return_target->address}
              : std::nullopt,
  };
  const AuthoritativePostLayoutControlFlow rebuilt =
      build_post_layout_control_flow(items, cfg_options);
  if (!rebuilt.proved) {
    for (const std::string& reason : rebuilt.reasons)
      add_reason(reasons, "authoritative CFG rebuild: " + reason);
    return false;
  }
  if (rebuilt.external_entries != control.external_entries ||
      rebuilt.indirect_flow_targets != control.indirect_flow_targets ||
      rebuilt.indirect_memory_targets != control.indirect_memory_targets ||
      rebuilt.empty_return_target != control.empty_return_target ||
      rebuilt.maximum_observed_return_depth != control.maximum_observed_return_depth ||
      rebuilt.explored_states != control.explored_states) {
    add_reason(reasons,
               "post-layout control-flow proof does not match the current command identities");
  }
  return reasons.empty();
}

std::vector<ExecutionState>
state_successors(const std::vector<MachineItem>& items, const ArtifactIndex& index,
                 const AuthoritativePostLayoutControlFlow& control, const ExecutionState& state,
                 const TerminalCyclicLayoutOptions& options, std::vector<std::string>& reasons) {
  const auto cell = index.cell_items.find(state.pc);
  if (cell == index.cell_items.end() || items.at(cell->second).kind != MachineItemKind::Op) {
    add_reason(reasons, "CFG reached a non-executable cell");
    return {};
  }
  const MachineItem& item = items.at(cell->second);
  const int opcode = item.opcode;
  if (opcode == kStopOpcode)
    return {};
  if (opcode == kReturnOpcode) {
    if (state.returns.empty()) {
      if (!control.empty_return_target.has_value()) {
        add_reason(reasons, "empty return has no authoritative physical target");
        return {};
      }
      return {ExecutionState{.pc = control.empty_return_target->address, .returns = {}}};
    }
    ExecutionState next = state;
    next.pc = next.returns.back();
    next.returns.pop_back();
    return {std::move(next)};
  }

  if (opcode_by_code(opcode).takes_address) {
    const std::optional<std::size_t> operand_item = next_cell_item(items, cell->second);
    if (!operand_item.has_value() || items.at(*operand_item).kind != MachineItemKind::Address) {
      add_reason(reasons, "address-taking opcode has no adjacent operand");
      return {};
    }
    const std::optional<int> target =
        resolved_target(items.at(*operand_item), index, options.address_space_model);
    if (!target.has_value() || !executable_address(items, index, *target)) {
      add_reason(reasons, "direct flow has an unresolved or non-executable target");
      return {};
    }
    const std::optional<int> fallthrough = sequential_successor(
        index, index.item_addresses.at(*operand_item), options.address_space_model);
    if (opcode == kJumpOpcode)
      return {ExecutionState{.pc = *target, .returns = state.returns}};
    if (opcode == kCallOpcode) {
      if (static_cast<int>(state.returns.size()) >= options.maximum_return_depth) {
        add_reason(reasons, "CFG exceeds the proved return-stack depth");
        return {};
      }
      if (!fallthrough.has_value()) {
        add_reason(reasons, "direct call has no executable continuation");
        return {};
      }
      ExecutionState next{.pc = *target, .returns = state.returns};
      next.returns.push_back(*fallthrough);
      return {std::move(next)};
    }
    if (opcode >= kDirectConditionFirst && opcode <= kDirectConditionLast) {
      if (!fallthrough.has_value()) {
        add_reason(reasons, "direct conditional has no executable fallthrough");
        return {};
      }
      return {ExecutionState{.pc = *target, .returns = state.returns},
              ExecutionState{.pc = *fallthrough, .returns = state.returns}};
    }
  }

  if (is_indirect_flow_opcode(opcode)) {
    const auto fact = control.indirect_flow_targets.find(cell->second);
    if (fact == control.indirect_flow_targets.end()) {
      add_reason(reasons, "CFG lacks a complete indirect-flow target set");
      return {};
    }
    std::vector<ExecutionState> result;
    for (const PostLayoutCommandIdentity& target : fact->second)
      result.push_back(ExecutionState{.pc = target.address, .returns = state.returns});
    if (is_indirect_call_opcode(opcode)) {
      if (static_cast<int>(state.returns.size()) >= options.maximum_return_depth) {
        add_reason(reasons, "CFG exceeds the proved return-stack depth");
        return {};
      }
      const std::optional<int> continuation =
          sequential_successor(index, state.pc, options.address_space_model);
      if (!continuation.has_value()) {
        add_reason(reasons, "indirect call has no executable continuation");
        return {};
      }
      for (ExecutionState& next : result)
        next.returns.push_back(*continuation);
    } else if (is_indirect_conditional_opcode(opcode)) {
      const std::optional<int> fallthrough =
          sequential_successor(index, state.pc, options.address_space_model);
      if (!fallthrough.has_value()) {
        add_reason(reasons, "indirect conditional has no executable fallthrough");
        return {};
      }
      result.push_back(ExecutionState{.pc = *fallthrough, .returns = state.returns});
    }
    return result;
  }

  const std::optional<int> successor =
      sequential_successor(index, state.pc, options.address_space_model);
  if (!successor.has_value())
    return {};
  return {ExecutionState{.pc = *successor, .returns = state.returns}};
}

std::vector<std::vector<int>>
reachable_return_stacks(const std::vector<MachineItem>& items, const ArtifactIndex& index,
                        const AuthoritativePostLayoutControlFlow& control, int target_address,
                        const TerminalCyclicLayoutOptions& options,
                        std::vector<std::string>& reasons) {
  std::deque<ExecutionState> pending;
  for (const PostLayoutExternalEntryState& entry : control.external_entries) {
    std::vector<int> returns;
    returns.reserve(entry.return_stack.size());
    for (const PostLayoutCommandIdentity& slot : entry.return_stack)
      returns.push_back(slot.address);
    pending.push_back(ExecutionState{.pc = entry.entry.address, .returns = std::move(returns)});
  }
  std::set<ExecutionState> visited;
  std::set<std::vector<int>> stacks;
  while (!pending.empty()) {
    ExecutionState state = std::move(pending.front());
    pending.pop_front();
    if (!visited.insert(state).second)
      continue;
    if (static_cast<int>(visited.size()) > options.maximum_execution_states) {
      add_reason(reasons, "CFG exceeds the proved execution-state budget");
      return {};
    }
    if (state.pc == target_address)
      stacks.insert(state.returns);
    for (ExecutionState next : state_successors(items, index, control, state, options, reasons)) {
      pending.push_back(std::move(next));
    }
  }
  if (stacks.empty())
    add_reason(reasons, "terminal report candidate is unreachable from every proved entry");
  return std::vector<std::vector<int>>(stacks.begin(), stacks.end());
}


std::optional<int> direct_recall_register(const MachineItem& item) {
  if (item.kind != MachineItemKind::Op)
    return std::nullopt;
  if (item.opcode >= kDirectRecallFirst && item.opcode <= kDirectRecallLast)
    return item.opcode - kDirectRecallFirst;
  if (item.opcode == 0x6f)
    return 0;
  return std::nullopt;
}

bool state_accesses_register_before_sink(const MachineItem& item, std::size_t item_index,
                                         int terminal_register,
                                         const AuthoritativePostLayoutControlFlow& control,
                                         bool is_sink_recall) {
  if (item.kind != MachineItemKind::Op || is_sink_recall)
    return false;
  const std::optional<int> encoded = encoded_register(item.opcode);
  if (encoded.has_value() && *encoded == terminal_register)
    return true;
  if (is_indirect_memory_opcode(item.opcode)) {
    const auto targets = control.indirect_memory_targets.find(item_index);
    return targets != control.indirect_memory_targets.end() &&
           std::find(targets->second.begin(), targets->second.end(), terminal_register) !=
               targets->second.end();
  }
  return false;
}

bool opcode_requires_value_domain_proof(int opcode) {
  // These documented operations can still raise a calculator error for some
  // finite inputs (zero denominator, invalid logarithm/root domain, pole, or
  // exponent/arithmetic overflow). CFG reachability and OpcodeRisk alone do
  // not prove their operands. Until a value-domain certificate is available,
  // a report continuation containing any of them fails closed.
  static const std::set<int> operations = {
      0x10, 0x11, 0x12, 0x13, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1e, 0x21, 0x22, 0x23, 0x24,
  };
  return operations.contains(opcode);
}

ContinuationAnalysis analyze_continuation(const std::vector<MachineItem>& items,
                                          const ArtifactIndex& index,
                                          const AuthoritativePostLayoutControlFlow& control,
                                          int continuation_address,
                                          const std::vector<std::vector<int>>& return_stacks,
                                          const TerminalCyclicLayoutOptions& options) {
  ContinuationAnalysis analysis;
  std::deque<ExecutionState> pending;
  for (const std::vector<int>& stack : return_stacks)
    pending.push_back(ExecutionState{.pc = continuation_address, .returns = stack});

  std::set<ExecutionState> states;
  std::map<ExecutionState, std::set<ExecutionState>> edges;
  std::map<ExecutionState, std::set<ExecutionState>> predecessors;
  while (!pending.empty()) {
    ExecutionState state = std::move(pending.front());
    pending.pop_front();
    if (!states.insert(state).second)
      continue;
    if (static_cast<int>(states.size()) > options.maximum_execution_states) {
      add_reason(analysis.reasons, "report continuation exceeds the execution-state budget");
      return analysis;
    }
    const auto cell = index.cell_items.find(state.pc);
    if (cell == index.cell_items.end() || items.at(cell->second).kind != MachineItemKind::Op) {
      add_reason(analysis.reasons, "report continuation reaches a non-command cell");
      return analysis;
    }
    const MachineItem& item = items.at(cell->second);
    if (opcode_by_code(item.opcode).risk != OpcodeRisk::Documented) {
      add_reason(analysis.reasons,
                 "report continuation reaches a non-documented opcode without an exact "
                 "semantic whitelist");
      return analysis;
    }
    if (opcode_requires_value_domain_proof(item.opcode)) {
      add_reason(analysis.reasons,
                 "report continuation contains an operation without a value-domain proof");
      return analysis;
    }
    if (item.opcode == kStopOpcode) {
      if (item.stop_disposition != StopDisposition::Terminal) {
        add_reason(analysis.reasons,
                   "report continuation reaches a resumable STOP without state convergence");
        return analysis;
      }
      continue;
    }
    if (item.opcode == kReturnOpcode && state.returns.empty() &&
        !control.empty_return_target.has_value()) {
      add_reason(analysis.reasons, "report continuation returns outside the proved callers");
      return analysis;
    }
    std::vector<std::string> successor_reasons;
    const std::vector<ExecutionState> successors =
        state_successors(items, index, control, state, options, successor_reasons);
    for (const std::string& reason : successor_reasons)
      add_reason(analysis.reasons, reason);
    if (successors.empty()) {
      add_reason(analysis.reasons,
                 "report continuation has a non-terminal exit at " +
                     std::to_string(state.pc) + " (" + item.name + ")");
      return analysis;
    }
    for (const ExecutionState& successor : successors) {
      edges[state].insert(successor);
      predecessors[successor].insert(state);
      pending.push_back(successor);
    }
  }

  std::map<ExecutionState, int> indegree;
  for (const ExecutionState& state : states)
    indegree[state] = 0;
  for (const auto& [source, successors] : edges) {
    (void)source;
    for (const ExecutionState& successor : successors)
      ++indegree[successor];
  }
  std::deque<ExecutionState> ready;
  for (const auto& [state, degree] : indegree) {
    if (degree == 0)
      ready.push_back(state);
  }
  std::size_t consumed = 0;
  while (!ready.empty()) {
    const ExecutionState state = ready.front();
    ready.pop_front();
    ++consumed;
    for (const ExecutionState& successor : edges[state]) {
      int& degree = indegree[successor];
      --degree;
      if (degree == 0)
        ready.push_back(successor);
    }
  }
  if (consumed != states.size()) {
    add_reason(analysis.reasons, "report continuation contains a reachable cycle");
    return analysis;
  }

  std::set<ExecutionState> sink_recalls;
  std::set<int> terminal_registers;
  int stops = 0;
  for (const ExecutionState& state : states) {
    const std::size_t item_index = index.cell_items.at(state.pc);
    if (!is_op(items, item_index, kStopOpcode))
      continue;
    if (items.at(item_index).stop_disposition != StopDisposition::Terminal) {
      add_reason(analysis.reasons, "terminal sink is not compiler-owned halt protocol");
      continue;
    }
    ++stops;
    const auto incoming = predecessors.find(state);
    if (incoming == predecessors.end() || incoming->second.empty()) {
      add_reason(analysis.reasons, "terminal STOP has no proved recall predecessor");
      continue;
    }
    for (const ExecutionState& predecessor : incoming->second) {
      const std::size_t predecessor_item = index.cell_items.at(predecessor.pc);
      const std::optional<int> recalled = direct_recall_register(items.at(predecessor_item));
      if (!recalled.has_value()) {
        add_reason(analysis.reasons, "terminal STOP is not immediately preceded by a recall");
        continue;
      }
      terminal_registers.insert(*recalled);
      sink_recalls.insert(predecessor);
    }
  }
  if (stops == 0)
    add_reason(analysis.reasons, "report continuation reaches no terminal STOP");
  if (terminal_registers.size() != 1U) {
    add_reason(analysis.reasons,
               "report continuations do not converge on one terminal payload register");
    return analysis;
  }
  analysis.terminal_register = *terminal_registers.begin();
  for (const ExecutionState& state : states) {
    const std::size_t item_index = index.cell_items.at(state.pc);
    if (state_accesses_register_before_sink(items.at(item_index), item_index,
                                            analysis.terminal_register, control,
                                            sink_recalls.contains(state))) {
      add_reason(analysis.reasons,
                 "terminal payload register is observed or overwritten before its sink recall");
    }
  }
  analysis.proved = analysis.reasons.empty();
  return analysis;
}

std::optional<int> relocated_after_terminal(int address, int first_removed, int second_removed) {
  if (address == first_removed || address == second_removed)
    return std::nullopt;
  if (address > second_removed)
    return address - 2;
  if (address > first_removed)
    return address - 1;
  return address;
}

bool relocation_facts_proved(const std::vector<MachineItem>& items, const ArtifactIndex& index,
                             const AuthoritativePostLayoutControlFlow& control,
                             const TailCandidate& tail, const TerminalCyclicLayoutOptions& options,
                             std::vector<std::string>& reasons) {
  const int first_removed = index.item_addresses.at(tail.operand);
  const int second_removed = index.item_addresses.at(tail.stop);
  const std::set<int> protected_interior = {
      index.item_addresses.at(tail.fraction), index.item_addresses.at(tail.condition),
      index.item_addresses.at(tail.operand),  index.item_addresses.at(tail.dot),
      index.item_addresses.at(tail.stop),
  };

  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind != MachineItemKind::Address)
      continue;
    const std::optional<int> target = resolved_target(item, index, options.address_space_model);
    if (!target.has_value()) {
      add_reason(reasons, "direct target cannot be resolved across terminal relocation");
      continue;
    }
    if (protected_interior.contains(*target)) {
      add_reason(reasons, "direct target enters the rewritten terminal-tail interior");
      continue;
    }
    if (item_index == tail.operand)
      continue;
    const bool symbolic =
        !item.formal_opcode.has_value() && std::holds_alternative<std::string>(item.target);
    if (symbolic)
      continue;
    if (relocated_after_terminal(*target, first_removed, second_removed) != target) {
      add_reason(reasons, "fixed direct target changes identity across terminal relocation");
    }
  }
  const std::set<std::size_t> protected_items = {
      tail.fraction, tail.condition, tail.operand, tail.dot, tail.stop,
  };
  for (const auto& [item_index, targets] : control.indirect_flow_targets) {
    if (protected_items.contains(item_index)) {
      add_reason(reasons, "indirect command is replaced by the terminal-tail relocation");
    }
    for (const PostLayoutCommandIdentity& target : targets) {
      if (protected_items.contains(target.item_index)) {
        add_reason(reasons, "indirect target enters the rewritten terminal-tail interior");
      }
    }
  }
  for (const PostLayoutExternalEntryState& entry : control.external_entries) {
    std::vector<PostLayoutCommandIdentity> identities = entry.return_stack;
    identities.push_back(entry.entry);
    for (const PostLayoutCommandIdentity& identity : identities) {
      if (protected_items.contains(identity.item_index)) {
        add_reason(reasons, "external entry state enters the rewritten terminal-tail interior");
      }
    }
  }
  return reasons.empty();
}

std::vector<TailCandidate> discover_tail_candidates(const std::vector<MachineItem>& items,
                                                    const ArtifactIndex& index,
                                                    AddressSpaceModel model,
                                                    bool require_zero_return = true) {
  std::vector<TailCandidate> result;
  const auto zero = index.cell_items.find(0);
  const bool has_zero_return =
      zero != index.cell_items.end() && is_op(items, zero->second, kReturnOpcode);
  if (require_zero_return && !has_zero_return)
    return result;
  for (std::size_t mask = 0; mask < items.size(); ++mask) {
    if (!is_op(items, mask, 0x37))
      continue;
    const auto fraction = next_cell_item(items, mask);
    const auto condition = fraction.has_value() ? next_cell_item(items, *fraction) : std::nullopt;
    const auto operand = condition.has_value() ? next_cell_item(items, *condition) : std::nullopt;
    const auto dot = operand.has_value() ? next_cell_item(items, *operand) : std::nullopt;
    const auto stop = dot.has_value() ? next_cell_item(items, *dot) : std::nullopt;
    const auto continuation = stop.has_value() ? next_cell_item(items, *stop) : std::nullopt;
    if (!fraction.has_value() || !condition.has_value() || !operand.has_value() ||
        !dot.has_value() || !stop.has_value() || !continuation.has_value() ||
        !is_op(items, *fraction, 0x35) || !is_op(items, *condition, 0x57) ||
        items.at(*operand).kind != MachineItemKind::Address || !is_op(items, *dot, 0x0a) ||
        !is_op(items, *stop, kStopOpcode)) {
      continue;
    }
    const std::optional<int> target = resolved_target(items.at(*operand), index, model);
    if (!target.has_value())
      continue;
    const auto direct_return = index.cell_items.find(*target);
    if (direct_return == index.cell_items.end() ||
        !is_op(items, direct_return->second, kReturnOpcode)) {
      continue;
    }
    result.push_back(TailCandidate{
        .mask = mask,
        .fraction = *fraction,
        .condition = *condition,
        .operand = *operand,
        .dot = *dot,
        .stop = *stop,
        .continuation = *continuation,
        .direct_return = direct_return->second,
        .zero_return = has_zero_return ? zero->second : direct_return->second,
    });
  }
  return result;
}

std::map<int, std::string> unique_stable_preloads(const std::vector<PreloadReport>& preloads) {
  std::map<int, std::vector<std::string>> values;
  for (const PreloadReport& preload : preloads) {
    const std::optional<int> reg = normalized_register_index(preload.register_name);
    if (reg.has_value() && *reg >= 7)
      values[*reg].push_back(preload.value);
  }
  std::map<int, std::string> result;
  for (const auto& [reg, candidates] : values) {
    if (candidates.size() == 1U && !candidates.front().empty())
      result.emplace(reg, candidates.front());
  }
  return result;
}

bool selector_is_unwritten(const std::vector<MachineItem>& items, int selector,
                           const AuthoritativePostLayoutControlFlow& control) {
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind != MachineItemKind::Op)
      continue;
    if ((item.opcode >= kDirectStoreFirst && item.opcode <= kDirectStoreLast &&
         item.opcode - kDirectStoreFirst == selector) ||
        (item.opcode == 0x4f && selector == 0)) {
      return false;
    }
    if (is_indirect_store_opcode(item.opcode)) {
      const auto targets = control.indirect_memory_targets.find(item_index);
      if (targets == control.indirect_memory_targets.end() ||
          std::find(targets->second.begin(), targets->second.end(), selector) !=
              targets->second.end()) {
        return false;
      }
    }
  }
  return true;
}

RawZeroReturnSelectorProof build_raw_selector(int selector, std::string raw_value,
                                              bool selector_unwritten,
                                              int actual_flow_target = 0) {
  const OpcodeInfo& conditional = opcode_by_code(0x70 + selector);
  const bool stack_preserved = conditional.stack_effect == StackEffect::Preserves;
  const bool x2_preserved = conditional.conditional_x2_effect.has_value() &&
                            conditional.conditional_x2_effect->fallthrough == X2Effect::Preserves &&
                            conditional.conditional_x2_effect->jump == X2Effect::Preserves;
  return RawZeroReturnSelectorProof{
      .selector_register = register_text(selector),
      .facts = {{.raw_value = std::move(raw_value),
                 .actual_flow_target = actual_flow_target,
                 .conditional_preserves_selector = true,
                 .conditional_preserves_x_y_z_t = stack_preserved,
                 .conditional_preserves_x2 = x2_preserved}},
      .runtime_value_set_is_exhaustive = selector_unwritten,
      .selector_is_unwritten_until_use = selector_unwritten,
  };
}

TerminalReportTailRelocationProof build_relocation_proof(const ArtifactIndex& index,
                                                         const TailCandidate& tail,
                                                         bool relocation_safe) {
  const bool adjacent =
      index.item_addresses.at(tail.continuation) == index.item_addresses.at(tail.stop) + 1;
  const bool returns_equivalent = index.item_addresses.at(tail.zero_return) == 0;
  return TerminalReportTailRelocationProof{
      .expected_input_cells = index.cells,
      .expected_direct_condition_address = index.item_addresses.at(tail.condition),
      .expected_continuation_entry_address = index.item_addresses.at(tail.continuation),
      .mask_item_index = tail.mask,
      .fraction_item_index = tail.fraction,
      .direct_condition_item_index = tail.condition,
      .direct_address_item_index = tail.operand,
      .dot_item_index = tail.dot,
      .stop_item_index = tail.stop,
      .continuation_entry_item_index = tail.continuation,
      .direct_return_item_index = tail.direct_return,
      .zero_return_item_index = tail.zero_return,
      .continuation_is_relocated_immediately_after_tail = adjacent,
      .all_direct_references_are_relocated_or_symbolic = relocation_safe,
      .all_indirect_targets_and_selector_charges_are_rebound = relocation_safe,
      .removed_cells_have_no_external_entries = relocation_safe,
      .zero_and_direct_returns_have_equivalent_stack_contracts = returns_equivalent,
  };
}

TerminalContinuationLivenessProof build_continuation_proof(const std::vector<MachineItem>& items,
                                                           const TailCandidate& tail,
                                                           int terminal_register,
                                                           const ContinuationAnalysis& analysis) {
  // Exact symbolic transfer theorem for this opcode pair, not an inference
  // from the coarse X2Effect enum:
  //   direct F x!=0 fallthrough: X2 := X (predicate payload)
  //   dot:                       X  := X2
  // while the replacement indirect K x!=0 preserves X and stores that same X
  // into the terminal slot. The final proved recall therefore displays the
  // identical symbolic payload.
  enum class SymbolicValue { Predicate, PriorX2 };
  SymbolicValue original_x = SymbolicValue::Predicate;
  SymbolicValue original_x2 = SymbolicValue::PriorX2;
  const bool exact_opcode_pair = is_op(items, tail.condition, 0x57) && is_op(items, tail.dot, 0x0a);
  if (exact_opcode_pair) {
    original_x2 = original_x;
    original_x = original_x2;
  }
  const SymbolicValue replacement_stored = SymbolicValue::Predicate;
  const bool payload_effect = exact_opcode_pair && original_x == replacement_stored;
  return TerminalContinuationLivenessProof{
      .terminal_slot_register = register_text(terminal_register),
      .fractional_predicate_is_terminal_payload = payload_effect,
      .previous_slot_value_is_dead_on_report_path = analysis.proved,
      .stored_payload_is_live_until_terminal_stop = analysis.proved,
      .every_report_continuation_reaches_terminal_stop = analysis.proved,
      .no_prior_observable_event_error_or_divergence = analysis.proved,
      .continuation_stack_and_x2_effects_are_unobservable = analysis.proved,
  };
}

std::optional<std::size_t> reindexed_item_after_terminal(std::size_t old_item,
                                                         const TailCandidate& tail) {
  if (old_item == tail.operand || old_item == tail.stop)
    return std::nullopt;
  std::size_t result = old_item;
  if (old_item > tail.operand)
    --result;
  if (old_item > tail.stop)
    --result;
  return result;
}

std::optional<std::size_t> reindexed_item_after_cyclic(std::size_t old_item,
                                                       std::size_t input_item_count,
                                                       const CyclicEndReturnProof& proof) {
  const std::size_t block_size =
      proof.helper_block_end_item_index - proof.helper_block_begin_item_index;
  const std::size_t remaining_items = input_item_count - block_size;
  if (old_item < proof.helper_block_begin_item_index)
    return old_item;
  if (old_item >= proof.helper_block_end_item_index)
    return old_item - block_size;
  if (old_item == proof.original_explicit_return_item_index)
    return std::nullopt;
  std::size_t result = remaining_items + old_item - proof.helper_block_begin_item_index;
  if (old_item > proof.original_explicit_return_item_index)
    --result;
  return result;
}

using ItemRelocation = std::function<std::optional<std::size_t>(std::size_t)>;

struct AddedIndirectFlow {
  std::size_t source_item = 0;
  std::vector<std::size_t> target_items;
};

struct ReboundArtifact {
  std::vector<MachineItem> items;
  AuthoritativePostLayoutControlFlow control_flow;
  bool proved = false;
  std::vector<std::string> reasons;
};

bool is_direct_flow_opcode(int opcode) {
  return opcode == kJumpOpcode || opcode == kCallOpcode ||
         (opcode >= kDirectConditionFirst && opcode <= kDirectConditionLast);
}

bool is_non_fallthrough_command(const MachineItem& item) {
  if (item.kind != MachineItemKind::Op)
    return false;
  return item.opcode == kReturnOpcode || item.opcode == kStopOpcode ||
         item.opcode == kJumpOpcode || (item.opcode & 0xf0) == 0x80;
}

std::vector<std::size_t>
identity_items(const std::vector<PostLayoutCommandIdentity>& identities) {
  std::vector<std::size_t> result;
  result.reserve(identities.size());
  for (const PostLayoutCommandIdentity& identity : identities)
    result.push_back(identity.item_index);
  std::sort(result.begin(), result.end());
  return result;
}

ReboundArtifact rebind_control_flow_after_relocation(
    const std::vector<MachineItem>& input_items, std::vector<MachineItem> output_items,
    const AuthoritativePostLayoutControlFlow& input_control, const ItemRelocation& relocate,
    const std::set<std::size_t>& removed_items, const std::set<std::size_t>& changed_items,
    const std::vector<AddedIndirectFlow>& added_flows,
    const TerminalCyclicLayoutOptions& options) {
  ReboundArtifact result{.items = std::move(output_items)};
  const ArtifactIndex output_index = index_artifact(result.items);

  std::set<std::size_t> mapped_items;
  for (std::size_t old_item = 0; old_item < input_items.size(); ++old_item) {
    const std::optional<std::size_t> mapped = relocate(old_item);
    if (!mapped.has_value()) {
      if (!removed_items.contains(old_item))
        add_reason(result.reasons, "relocation dropped an unproved command identity");
      continue;
    }
    if (removed_items.contains(old_item) || *mapped >= result.items.size() ||
        !mapped_items.insert(*mapped).second) {
      add_reason(result.reasons, "relocation identity ledger is not injective and total");
      continue;
    }
    if (!changed_items.contains(old_item) &&
        !machine_items_equal(input_items.at(old_item), result.items.at(*mapped))) {
      add_reason(result.reasons, "relocation changed a command outside its proved rewrite set");
    }
  }
  if (mapped_items.size() + removed_items.size() != input_items.size() ||
      mapped_items.size() != result.items.size()) {
    add_reason(result.reasons, "relocation identity ledger does not cover the final artifact");
  }
  if (!result.reasons.empty())
    return result;

  for (MachineItem& item : result.items) {
    item.indirect_flow_targets.reset();
    item.indirect_memory_targets.reset();
  }

  std::map<std::size_t, std::vector<std::size_t>> expected_flow_items;
  for (const auto& [old_source, old_targets] : input_control.indirect_flow_targets) {
    const std::optional<std::size_t> new_source = relocate(old_source);
    if (!new_source.has_value()) {
      add_reason(result.reasons, "relocation removed a typed indirect-flow command");
      continue;
    }
    std::vector<IrTarget> metadata;
    std::vector<std::size_t> expected;
    for (const PostLayoutCommandIdentity& old_target : old_targets) {
      const std::optional<std::size_t> new_target = relocate(old_target.item_index);
      if (!new_target.has_value() || *new_target >= result.items.size() ||
          result.items.at(*new_target).kind != MachineItemKind::Op) {
        add_reason(result.reasons, "relocation removed an indirect-flow target identity");
        continue;
      }
      metadata.emplace_back(output_index.item_addresses.at(*new_target));
      expected.push_back(*new_target);
    }
    result.items.at(*new_source).indirect_flow_targets = std::move(metadata);
    std::sort(expected.begin(), expected.end());
    expected_flow_items.emplace(*new_source, std::move(expected));
  }
  for (const AddedIndirectFlow& added : added_flows) {
    if (added.source_item >= result.items.size() || added.target_items.empty()) {
      add_reason(result.reasons, "relocation introduced an invalid typed indirect-flow fact");
      continue;
    }
    std::vector<IrTarget> metadata;
    std::vector<std::size_t> expected;
    for (const std::size_t target : added.target_items) {
      if (target >= result.items.size() || result.items.at(target).kind != MachineItemKind::Op) {
        add_reason(result.reasons, "relocation introduced a non-command indirect target");
        continue;
      }
      metadata.emplace_back(output_index.item_addresses.at(target));
      expected.push_back(target);
    }
    result.items.at(added.source_item).indirect_flow_targets = std::move(metadata);
    std::sort(expected.begin(), expected.end());
    if (!expected_flow_items.emplace(added.source_item, std::move(expected)).second)
      add_reason(result.reasons, "relocation duplicated an indirect-flow source identity");
  }

  std::map<std::size_t, std::vector<int>> expected_memory;
  for (const auto& [old_source, targets] : input_control.indirect_memory_targets) {
    const std::optional<std::size_t> new_source = relocate(old_source);
    if (!new_source.has_value()) {
      add_reason(result.reasons, "relocation removed a typed indirect-memory command");
      continue;
    }
    result.items.at(*new_source).indirect_memory_targets = targets;
    expected_memory.emplace(*new_source, targets);
  }
  if (!result.reasons.empty())
    return result;

  const auto old_main = std::find_if(
      input_control.external_entries.begin(), input_control.external_entries.end(),
      [](const PostLayoutExternalEntryState& entry) { return entry.kind == ExternalEntryKind::Main; });
  if (old_main == input_control.external_entries.end()) {
    add_reason(result.reasons, "relocation input has no exact main command identity");
    return result;
  }
  const std::optional<std::size_t> new_main = relocate(old_main->entry.item_index);
  if (!new_main.has_value() || *new_main >= result.items.size()) {
    add_reason(result.reasons, "relocation removed the exact main command identity");
    return result;
  }
  std::optional<IrTarget> empty_return_target;
  if (input_control.empty_return_target.has_value()) {
    const std::optional<std::size_t> relocated_empty =
        relocate(input_control.empty_return_target->item_index);
    if (!relocated_empty.has_value() || *relocated_empty >= result.items.size() ||
        output_index.item_addresses.at(*relocated_empty) !=
            input_control.empty_return_target->address) {
      add_reason(result.reasons,
                 "relocation moved the exact physical empty-return target identity");
      return result;
    }
    empty_return_target = input_control.empty_return_target->address;
  }
  const PostLayoutControlFlowOptions cfg_options{
      .address_space_model = options.address_space_model,
      .maximum_return_depth = options.maximum_return_depth,
      .maximum_execution_states = static_cast<std::size_t>(options.maximum_execution_states),
      .main_entry = output_index.item_addresses.at(*new_main),
      .empty_return_target = empty_return_target,
  };
  result.control_flow = build_post_layout_control_flow(result.items, cfg_options);
  if (!result.control_flow.proved) {
    for (const std::string& reason : result.control_flow.reasons)
      add_reason(result.reasons, "relocated CFG rebuild: " + reason);
    return result;
  }

  if (result.control_flow.indirect_flow_targets.size() != expected_flow_items.size()) {
    add_reason(result.reasons, "relocated indirect-flow fact set changed cardinality");
  }
  for (const auto& [source, expected] : expected_flow_items) {
    const auto found = result.control_flow.indirect_flow_targets.find(source);
    if (found == result.control_flow.indirect_flow_targets.end() ||
        identity_items(found->second) != expected) {
      add_reason(result.reasons, "relocated indirect-flow identities do not match the ledger");
    }
  }
  if (result.control_flow.indirect_memory_targets != expected_memory)
    add_reason(result.reasons, "relocated indirect-memory identities do not match the ledger");

  if (result.control_flow.external_entries.size() != input_control.external_entries.size()) {
    add_reason(result.reasons, "relocation changed the exact external-entry set");
  } else {
    for (const PostLayoutExternalEntryState& old_entry : input_control.external_entries) {
      const std::optional<std::size_t> mapped_entry = relocate(old_entry.entry.item_index);
      std::vector<std::size_t> mapped_stack;
      bool complete = mapped_entry.has_value();
      for (const PostLayoutCommandIdentity& old_slot : old_entry.return_stack) {
        const std::optional<std::size_t> mapped_slot = relocate(old_slot.item_index);
        if (!mapped_slot.has_value()) {
          complete = false;
          break;
        }
        mapped_stack.push_back(*mapped_slot);
      }
      const bool present = complete && std::any_of(
          result.control_flow.external_entries.begin(), result.control_flow.external_entries.end(),
          [&](const PostLayoutExternalEntryState& candidate) {
            std::vector<std::size_t> candidate_stack;
            for (const PostLayoutCommandIdentity& slot : candidate.return_stack)
              candidate_stack.push_back(slot.item_index);
            return candidate.entry.item_index == *mapped_entry &&
                   candidate_stack == mapped_stack && candidate.kind == old_entry.kind &&
                   candidate.manual_interaction == old_entry.manual_interaction;
          });
      if (!present)
        add_reason(result.reasons, "relocation changed an external entry or return-stack identity");
    }
  }
  result.proved = result.reasons.empty();
  return result;
}

std::map<std::size_t, std::vector<int>>
address_flow_targets(const AuthoritativePostLayoutControlFlow& control) {
  std::map<std::size_t, std::vector<int>> result;
  for (const auto& [item_index, identities] : control.indirect_flow_targets) {
    std::vector<int> addresses;
    addresses.reserve(identities.size());
    for (const PostLayoutCommandIdentity& identity : identities)
      addresses.push_back(identity.address);
    result.emplace(item_index, std::move(addresses));
  }
  return result;
}

std::vector<int>
fixed_protocol_addresses(const std::vector<PostLayoutExternalEntryState>& entries) {
  std::set<int> addresses;
  for (const PostLayoutExternalEntryState& entry : entries) {
    addresses.insert(entry.entry.address);
    for (const PostLayoutCommandIdentity& slot : entry.return_stack)
      addresses.insert(slot.address);
  }
  return std::vector<int>(addresses.begin(), addresses.end());
}

struct StackRelation {
  std::array<std::array<bool, 4>, 4> equal{};

  static StackRelation unknown() {
    StackRelation result;
    for (std::size_t index = 0; index < result.equal.size(); ++index)
      result.equal.at(index).at(index) = true;
    return result;
  }

  bool operator==(const StackRelation&) const = default;
};

StackRelation remap_stack_relation(const StackRelation& input,
                                   const std::array<int, 4>& sources) {
  StackRelation result;
  for (std::size_t left = 0; left < sources.size(); ++left) {
    for (std::size_t right = 0; right < sources.size(); ++right) {
      result.equal.at(left).at(right) =
          left == right ||
          (sources.at(left) >= 0 && sources.at(right) >= 0 &&
           input.equal.at(static_cast<std::size_t>(sources.at(left)))
               .at(static_cast<std::size_t>(sources.at(right))));
    }
  }
  return result;
}

bool relation_transfer(StackRelation& relation, const MachineItem& item) {
  if (direct_recall_register(item).has_value()) {
    relation = remap_stack_relation(relation, {-1, 0, 1, 2});
    return true;
  }
  const OpcodeInfo& info = opcode_by_code(item.opcode);
  switch (info.stack_effect) {
    case StackEffect::Preserves:
      for (std::size_t index = 1; index < 4; ++index) {
        relation.equal.at(0).at(index) = false;
        relation.equal.at(index).at(0) = false;
      }
      return true;
    case StackEffect::Shifts:
      relation = remap_stack_relation(relation, {-1, 0, 1, 2});
      return true;
    case StackEffect::ConsumeYDrop:
      relation = remap_stack_relation(relation, {-1, 2, 3, 3});
      return true;
    case StackEffect::ConsumeYKeep:
      relation = remap_stack_relation(relation, {-1, 1, 2, 3});
      return true;
    case StackEffect::Exposes:
      relation = remap_stack_relation(relation, {1, 2, 3, 3});
      return true;
    case StackEffect::Barrier:
    case StackEffect::Unknown:
      relation = StackRelation::unknown();
      return true;
  }
  return false;
}

bool meet_stack_relation(StackRelation& destination, const StackRelation& source) {
  bool changed = false;
  for (std::size_t left = 0; left < 4; ++left) {
    for (std::size_t right = 0; right < 4; ++right) {
      const bool merged = destination.equal.at(left).at(right) &&
                          source.equal.at(left).at(right);
      changed = changed || merged != destination.equal.at(left).at(right);
      destination.equal.at(left).at(right) = merged;
    }
  }
  return changed;
}

bool semantic_flow_opcode(int opcode) {
  return opcode == kJumpOpcode || opcode == kCallOpcode || opcode == kReturnOpcode ||
         (opcode >= kDirectConditionFirst && opcode <= kDirectConditionLast) ||
         is_indirect_flow_opcode(opcode);
}

bool condition_stack_has_equal_z_t(
    const std::vector<MachineItem>& items, const ArtifactIndex& index,
    const AuthoritativePostLayoutControlFlow& control, int condition_address,
    const TerminalCyclicLayoutOptions& options, std::vector<std::string>& reasons) {
  std::deque<ExecutionState> pending;
  for (const PostLayoutExternalEntryState& entry : control.external_entries) {
    std::vector<int> returns;
    for (const PostLayoutCommandIdentity& slot : entry.return_stack)
      returns.push_back(slot.address);
    pending.push_back(ExecutionState{.pc = entry.entry.address, .returns = std::move(returns)});
  }

  std::set<ExecutionState> states;
  std::map<ExecutionState, std::set<ExecutionState>> edges;
  std::map<ExecutionState, std::set<ExecutionState>> predecessors;
  std::set<ExecutionState> targets;
  while (!pending.empty()) {
    ExecutionState state = std::move(pending.front());
    pending.pop_front();
    if (!states.insert(state).second)
      continue;
    if (static_cast<int>(states.size()) > options.maximum_execution_states) {
      add_reason(reasons, "semantic return stack proof exceeds the execution-state budget");
      return false;
    }
    if (state.pc == condition_address)
      targets.insert(state);
    std::vector<std::string> ignored;
    for (ExecutionState successor :
         state_successors(items, index, control, state, options, ignored)) {
      edges[state].insert(successor);
      predecessors[successor].insert(state);
      pending.push_back(std::move(successor));
    }
  }
  if (targets.empty()) {
    add_reason(reasons, "semantic return condition is unreachable");
    return false;
  }

  std::set<ExecutionState> relevant = targets;
  pending = std::deque<ExecutionState>(targets.begin(), targets.end());
  while (!pending.empty()) {
    const ExecutionState state = pending.front();
    pending.pop_front();
    for (const ExecutionState& predecessor : predecessors[state]) {
      if (relevant.insert(predecessor).second)
        pending.push_back(predecessor);
    }
  }

  std::map<ExecutionState, StackRelation> facts;
  const auto merge = [&](const ExecutionState& state, const StackRelation& relation,
                         std::deque<ExecutionState>& work) {
    const auto [found, inserted] = facts.emplace(state, relation);
    if (inserted || meet_stack_relation(found->second, relation))
      work.push_back(state);
  };
  std::deque<ExecutionState> work;
  for (const PostLayoutExternalEntryState& entry : control.external_entries) {
    std::vector<int> returns;
    for (const PostLayoutCommandIdentity& slot : entry.return_stack)
      returns.push_back(slot.address);
    const ExecutionState state{.pc = entry.entry.address, .returns = std::move(returns)};
    if (relevant.contains(state))
      merge(state, StackRelation::unknown(), work);
  }
  while (!work.empty()) {
    const ExecutionState state = work.front();
    work.pop_front();
    const auto cell = index.cell_items.find(state.pc);
    if (cell == index.cell_items.end() || items.at(cell->second).kind != MachineItemKind::Op) {
      add_reason(reasons, "semantic return stack proof reaches a non-command cell");
      return false;
    }
    StackRelation output = facts.at(state);
    const MachineItem& item = items.at(cell->second);
    const bool stack_preserving_store =
        (item.opcode >= kDirectStoreFirst && item.opcode <= kDirectStoreLast) ||
        item.opcode == 0x4f || is_indirect_store_opcode(item.opcode);
    if (!semantic_flow_opcode(item.opcode) && !stack_preserving_store &&
        !relation_transfer(output, item)) {
      add_reason(reasons, "semantic return stack proof reaches an unknown stack effect");
      return false;
    }
    for (const ExecutionState& successor : edges[state]) {
      if (relevant.contains(successor))
        merge(successor, output, work);
    }
  }

  for (const ExecutionState& target : targets) {
    const auto fact = facts.find(target);
    if (fact == facts.end() || !fact->second.equal.at(2).at(3)) {
      add_reason(reasons, "semantic helper sticky-T ABI requires an unproved Z=T precondition");
      return false;
    }
  }
  return true;
}

struct SemanticContinuationState {
  ExecutionState execution;
  StackValueEqualityState equality;
  bool x1_equal = false;

  bool operator<(const SemanticContinuationState& other) const {
    return std::tie(execution, equality.stack_equal, equality.x2_equal, x1_equal) <
           std::tie(other.execution, other.equality.stack_equal, other.equality.x2_equal,
                    other.x1_equal);
  }
};

bool semantic_continuations_converge(
    const std::vector<MachineItem>& items, const ArtifactIndex& index,
    const AuthoritativePostLayoutControlFlow& control,
    const std::vector<std::vector<int>>& return_stacks,
    const TerminalCyclicLayoutOptions& options, std::vector<std::string>& reasons) {
  std::map<SemanticContinuationState, int> status;
  std::size_t visited = 0;
  std::function<bool(SemanticContinuationState)> prove =
      [&](SemanticContinuationState state) -> bool {
    if (stack_values_fully_equal(state.equality) && state.x1_equal)
      return true;
    const auto known = status.find(state);
    if (known != status.end())
      return known->second == 2;
    if (++visited > static_cast<std::size_t>(options.maximum_execution_states)) {
      add_reason(reasons, "semantic return convergence exceeds the execution-state budget");
      return false;
    }
    status.emplace(state, 1);
    const auto cell = index.cell_items.find(state.execution.pc);
    if (cell == index.cell_items.end() || items.at(cell->second).kind != MachineItemKind::Op) {
      add_reason(reasons, "semantic return continuation reaches a non-command cell");
      return false;
    }
    const MachineItem& item = items.at(cell->second);
    if (item.manual_interaction.has_value() || item.opcode == kStopOpcode) {
      add_reason(reasons, "semantic return difference reaches an observable interaction");
      return false;
    }

    const bool flow = semantic_flow_opcode(item.opcode);
    const bool conditional =
        (item.opcode >= kDirectConditionFirst && item.opcode <= kDirectConditionLast) ||
        is_indirect_conditional_opcode(item.opcode);
    if (conditional && !state.equality.stack_equal.at(0)) {
      add_reason(reasons, "semantic return difference reaches a conditional predicate");
      return false;
    }
    if (flow && (item.opcode == kCallOpcode || item.opcode == kReturnOpcode ||
                 is_indirect_call_opcode(item.opcode))) {
      add_reason(reasons, "semantic return difference crosses an unproved call/return");
      return false;
    }

    const bool direct_store =
        (item.opcode >= kDirectStoreFirst && item.opcode <= kDirectStoreLast) ||
        item.opcode == 0x4f;
    if (!state.x1_equal) {
      const bool visible_equal = std::all_of(state.equality.stack_equal.begin(),
                                             state.equality.stack_equal.end(),
                                             [](bool value) { return value; });
      if (visible_equal &&
          helper_semantic_physical_x1_reconverges_from_equal_visible_state(item.opcode)) {
        state.x1_equal = true;
      } else if (!flow && !direct_store &&
                 !helper_semantic_physical_x1_preserves_equal_visible_state(item.opcode)) {
        add_reason(reasons, "semantic return physical X1 difference is observed before convergence");
        return false;
      }
    }

    if (!flow) {
      StackValueEqualityStepKind kind = StackValueEqualityStepKind::Plain;
      if (direct_recall_register(item).has_value())
        kind = StackValueEqualityStepKind::Recall;
      else if (direct_store)
        kind = StackValueEqualityStepKind::Store;
      if (transfer_stack_value_equality(state.equality, item.opcode, kind) ==
          StackValueEqualityTransfer::Rejected) {
        add_reason(reasons, "semantic return stack difference is consumed before convergence");
        return false;
      }
    }
    if (stack_values_fully_equal(state.equality) && state.x1_equal) {
      status[state] = 2;
      return true;
    }

    std::vector<std::string> successor_reasons;
    const std::vector<ExecutionState> successors =
        state_successors(items, index, control, state.execution, options, successor_reasons);
    if (successors.empty()) {
      add_reason(reasons, "semantic return continuation exits before convergence");
      return false;
    }
    for (const ExecutionState& successor : successors) {
      SemanticContinuationState next = state;
      next.execution = successor;
      const auto active = status.find(next);
      if (active != status.end() && active->second == 1) {
        add_reason(reasons, "semantic return continuation cycles before convergence");
        return false;
      }
      if (!prove(std::move(next)))
        return false;
    }
    status[state] = 2;
    return true;
  };

  for (const std::vector<int>& stack : return_stacks) {
    ExecutionState continuation;
    continuation.returns = stack;
    if (!continuation.returns.empty()) {
      continuation.pc = continuation.returns.back();
      continuation.returns.pop_back();
    } else if (control.empty_return_target.has_value()) {
      continuation.pc = control.empty_return_target->address;
    } else {
      add_reason(reasons, "semantic return has no proved caller continuation");
      return false;
    }
    if (!prove(SemanticContinuationState{.execution = std::move(continuation)}))
      return false;
  }
  return true;
}

std::optional<std::size_t> helper_contract_entry_item(
    const std::vector<MachineItem>& items, const std::string& label) {
  std::optional<std::size_t> label_item;
  for (std::size_t item = 0; item < items.size(); ++item) {
    if (items.at(item).kind != MachineItemKind::Label || items.at(item).name != label)
      continue;
    if (label_item.has_value())
      return std::nullopt;
    label_item = item;
  }
  if (!label_item.has_value())
    return std::nullopt;
  const std::optional<std::size_t> entry = next_cell_item(items, *label_item);
  return entry.has_value() && items.at(*entry).kind == MachineItemKind::Op ? entry
                                                                          : std::nullopt;
}

bool exact_zero_helper_contract(const std::vector<MachineItem>& items,
                                const HelperSemanticContract& contract) {
  if (!contract.expression || !contract.admitted_input.valid() ||
      contract.admitted_input.minimum != 0 || contract.admitted_input.maximum != 0 ||
      !contract.input_decimal_derivation_exact || !contract.input_zero_canonical_positive ||
      !contract.decimal_execution_exact || !contract.hidden_x2_return_sync_proved ||
      !contract.x1_effect_proved || contract.certified_body_key.empty() ||
      contract.abi != HelperMachineAbi::UnaryXPreserveYZStickyZReturnSync ||
      !helper_semantic_decimal_execution_exact(contract.expression, contract.admitted_input)) {
    return false;
  }
  const std::optional<std::string> body_key =
      helper_semantic_alias_body_key(items, contract.entry_label);
  return body_key.has_value() && *body_key == contract.certified_body_key;
}

std::optional<TerminalCyclicLayoutResult> rewrite_terminal_semantic_return_alias(
    const std::vector<MachineItem>& items, const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control_flow,
    const TerminalCyclicLayoutOptions& options,
    std::vector<std::string>* rejection_reasons) {
  const auto reject = [&](std::string reason) {
    if (rejection_reasons != nullptr)
      add_reason(*rejection_reasons, "semantic return alias: " + std::move(reason));
  };
  if (options.helper_semantic_contracts == nullptr ||
      options.helper_semantic_contracts->empty()) {
    reject("no typed helper contracts are available");
    return std::nullopt;
  }
  const ArtifactIndex index = index_artifact(items);
  const std::vector<TailCandidate> tails =
      discover_tail_candidates(items, index, options.address_space_model, false);
  const std::map<int, std::string> selectors = unique_stable_preloads(preloads);
  for (const TailCandidate& tail : tails) {
    if (items.at(tail.stop).stop_disposition != StopDisposition::Terminal)
      continue;
    std::vector<std::string> local_reasons;
    const int condition_address = index.item_addresses.at(tail.condition);
    const std::vector<std::vector<int>> return_stacks = reachable_return_stacks(
        items, index, control_flow, condition_address, options, local_reasons);
    if (!local_reasons.empty() || return_stacks.empty() ||
        !condition_stack_has_equal_z_t(items, index, control_flow, condition_address, options,
                                       local_reasons) ||
        !semantic_continuations_converge(items, index, control_flow, return_stacks, options,
                                         local_reasons)) {
      for (const std::string& reason : local_reasons)
        reject(reason);
      continue;
    }

    for (const auto& [selector, raw_value] : selectors) {
      if (!selector_is_unwritten(items, selector, control_flow))
        continue;
      const std::optional<int> selector_target = raw_selector_physical_target(
          register_text(selector), raw_value, options.address_space_model);
      if (!selector_target.has_value())
        continue;
      for (const HelperSemanticContract& contract : *options.helper_semantic_contracts) {
        if (!exact_zero_helper_contract(items, contract))
          continue;
        const std::optional<std::size_t> helper =
            helper_contract_entry_item(items, contract.entry_label);
        if (!helper.has_value() || *helper >= tail.operand ||
            index.item_addresses.at(*helper) != *selector_target) {
          continue;
        }

        const int indirect_opcode = 0x70 + selector;
        const OpcodeInfo& direct = opcode_by_code(items.at(tail.condition).opcode);
        const OpcodeInfo& indirect = opcode_by_code(indirect_opcode);
        if (direct.stack_effect != indirect.stack_effect ||
            !direct.conditional_x2_effect.has_value() ||
            !indirect.conditional_x2_effect.has_value() ||
            direct.conditional_x2_effect->jump != indirect.conditional_x2_effect->jump ||
            indirect.conditional_x2_effect->jump != X2Effect::Preserves) {
          continue;
        }

        bool selector_flows_match = true;
        for (const auto& [source, targets] : control_flow.indirect_flow_targets) {
          if (source >= items.size() || items.at(source).kind != MachineItemKind::Op ||
              encoded_register(items.at(source).opcode) != selector)
            continue;
          if (targets.empty() ||
              std::any_of(targets.begin(), targets.end(),
                          [&](const PostLayoutCommandIdentity& target) {
                            return target.item_index != *helper;
                          })) {
            selector_flows_match = false;
            break;
          }
        }
        if (!selector_flows_match)
          continue;

        std::vector<std::optional<std::size_t>> relocation(items.size());
        std::vector<MachineItem> rewritten;
        rewritten.reserve(items.size() - 1U);
        for (std::size_t old_item = 0; old_item < items.size(); ++old_item) {
          if (old_item == tail.operand)
            continue;
          relocation.at(old_item) = rewritten.size();
          MachineItem item = items.at(old_item);
          if (old_item == tail.condition) {
            item.opcode = indirect_opcode;
            item.name = indirect.name;
            item.comment = "proved semantic-return alias";
          }
          rewritten.push_back(std::move(item));
        }
        const ItemRelocation relocate = [relocation](std::size_t old_item)
            -> std::optional<std::size_t> {
          return old_item < relocation.size() ? relocation.at(old_item) : std::nullopt;
        };
        const std::size_t new_condition = *relocation.at(tail.condition);
        const std::size_t new_helper = *relocation.at(*helper);
        ReboundArtifact rebound = rebind_control_flow_after_relocation(
            items, std::move(rewritten), control_flow, relocate, {tail.operand},
            {tail.condition},
            {{.source_item = new_condition, .target_items = {new_helper}}}, options);
        if (!rebound.proved) {
          for (const std::string& reason : rebound.reasons)
            reject(reason);
          continue;
        }

        TerminalCyclicLayoutResult result;
        result.items = std::move(rebound.items);
        result.plan.return_alias_proved = true;
        result.plan.semantic_return_alias_proved = true;
        result.plan.final_artifact_proved = true;
        result.plan.input_cells = index.cells;
        result.plan.output_cells = index.cells - 1;
        result.plan.removed_cells = 1;
        result.plan.raw_selector =
            build_raw_selector(selector, raw_value, true, *selector_target);
        result.plan.final_control_flow = std::move(rebound.control_flow);
        result.applied = 1;
        result.removed_cells = 1;
        result.optimizations.push_back(passes::AppliedOptimization{
            .name = "terminal-semantic-return-alias",
            .detail = "Replaced a direct return operand with a typed zero-domain helper "
                      "after proving stack ABI compatibility and continuation convergence.",
        });
        return result;
      }
    }
  }
  reject("no selector/helper pair satisfies every semantic return proof");
  return std::nullopt;
}

std::optional<TerminalCyclicLayoutResult> rewrite_terminal_return_alias(
    const std::vector<MachineItem>& items, const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control_flow,
    const TerminalCyclicLayoutOptions& options,
    std::vector<std::string>* rejection_reasons) {
  const auto reject = [&](std::string reason) {
    if (rejection_reasons != nullptr)
      add_reason(*rejection_reasons, "return alias: " + std::move(reason));
  };
  const ArtifactIndex index = index_artifact(items);
  const std::vector<TailCandidate> tails =
      discover_tail_candidates(items, index, options.address_space_model, false);
  const std::map<int, std::string> selectors = unique_stable_preloads(preloads);
  if (tails.empty())
    reject("no structural tail with equivalent returns");
  if (selectors.empty())
    reject("no unique stable selector preload");
  for (const TailCandidate& tail : tails) {
    if (items.at(tail.stop).stop_disposition != StopDisposition::Terminal) {
      reject("provisional stop is not terminal");
      continue;
    }
    for (const auto& [selector, raw_value] : selectors) {
      if (!selector_is_unwritten(items, selector, control_flow)) {
        reject("selector is not stable until the conditional");
        continue;
      }
      const std::string selector_name = register_text(selector);
      const int post_alias_return_address =
          index.item_addresses.at(tail.direct_return) -
          (tail.direct_return > tail.operand ? 1 : 0);
      const std::optional<int> selector_target = raw_selector_physical_target(
          selector_name, raw_value, options.address_space_model);
      if (!selector_target.has_value() || *selector_target != post_alias_return_address) {
        reject("selector preload does not decode to the relocated direct return");
        continue;
      }
      const int indirect_opcode = 0x70 + selector;
      const OpcodeInfo& direct = opcode_by_code(items.at(tail.condition).opcode);
      const OpcodeInfo& indirect = opcode_by_code(indirect_opcode);
      if (direct.stack_effect != indirect.stack_effect ||
          !direct.conditional_x2_effect.has_value() ||
          indirect.stack_effect != StackEffect::Preserves ||
          !indirect.conditional_x2_effect.has_value() ||
          direct.conditional_x2_effect->jump != indirect.conditional_x2_effect->jump ||
          indirect.conditional_x2_effect->jump != X2Effect::Preserves) {
        reject("direct and indirect conditionals have different stack/X2 contracts");
        continue;
      }

      bool existing_selector_flows_match = true;
      for (const auto& [source, targets] : control_flow.indirect_flow_targets) {
        if (source >= items.size() || items.at(source).kind != MachineItemKind::Op ||
            encoded_register(items.at(source).opcode) != selector)
          continue;
        if (post_alias_return_address != index.item_addresses.at(tail.direct_return) ||
            targets.empty() ||
            std::any_of(targets.begin(), targets.end(),
                        [&](const PostLayoutCommandIdentity& target) {
                          return target.item_index != tail.direct_return;
                        })) {
          existing_selector_flows_match = false;
          break;
        }
      }
      if (!existing_selector_flows_match) {
        reject("existing uses of the selector target a different command identity");
        continue;
      }

      std::vector<std::optional<std::size_t>> relocation(items.size());
      std::vector<MachineItem> rewritten;
      rewritten.reserve(items.size() - 1U);
      for (std::size_t old_item = 0; old_item < items.size(); ++old_item) {
        if (old_item == tail.operand)
          continue;
        relocation.at(old_item) = rewritten.size();
        MachineItem item = items.at(old_item);
        if (old_item == tail.condition) {
          item.opcode = indirect_opcode;
          item.name = indirect.name;
          item.comment = "proved equivalent-return alias";
        }
        rewritten.push_back(std::move(item));
      }
      const ItemRelocation relocate = [relocation](std::size_t old_item)
          -> std::optional<std::size_t> {
        return old_item < relocation.size() ? relocation.at(old_item) : std::nullopt;
      };
      if (!relocation.at(tail.condition).has_value() ||
          !relocation.at(tail.direct_return).has_value())
        continue;
      const std::size_t new_condition = *relocation.at(tail.condition);
      const std::size_t new_direct_return = *relocation.at(tail.direct_return);
      ReboundArtifact rebound = rebind_control_flow_after_relocation(
          items, std::move(rewritten), control_flow, relocate, {tail.operand},
          {tail.condition},
          {{.source_item = new_condition, .target_items = {new_direct_return}}}, options);
      if (!rebound.proved) {
        if (rebound.reasons.empty())
          reject("final CFG relocation proof failed");
        else
          for (const std::string& reason : rebound.reasons)
            reject(reason);
        continue;
      }

      TerminalCyclicLayoutResult result;
      result.items = std::move(rebound.items);
      result.plan.return_alias_proved = true;
      result.plan.final_artifact_proved = true;
      result.plan.input_cells = index.cells;
      result.plan.output_cells = index.cells - 1;
      result.plan.removed_cells = 1;
      result.plan.raw_selector =
          build_raw_selector(selector, raw_value, true, post_alias_return_address);
      result.plan.final_control_flow = std::move(rebound.control_flow);
      result.applied = 1;
      result.removed_cells = 1;
      result.optimizations.push_back(passes::AppliedOptimization{
          .name = "terminal-return-alias",
          .detail = "Replaced one direct conditional operand with a proved one-cell indirect "
                    "branch to the same relocated return command.",
      });
      return result;
    }
  }
  return std::nullopt;
}

std::vector<ReboundArtifact> normalize_inverted_terminal_tail_layouts(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& control_flow,
    const TerminalCyclicLayoutOptions& options) {
  const ArtifactIndex input_index = index_artifact(items);
  std::vector<ReboundArtifact> candidates;

  for (std::size_t mask = 0; mask < items.size(); ++mask) {
    if (!is_op(items, mask, 0x37))
      continue;
    const std::optional<std::size_t> fraction = next_cell_item(items, mask);
    const std::optional<std::size_t> condition =
        fraction.has_value() ? next_cell_item(items, *fraction) : std::nullopt;
    const std::optional<std::size_t> operand =
        condition.has_value() ? next_cell_item(items, *condition) : std::nullopt;
    const std::optional<std::size_t> direct_return =
        operand.has_value() ? next_cell_item(items, *operand) : std::nullopt;
    if (!fraction.has_value() || !condition.has_value() || !operand.has_value() ||
        !direct_return.has_value() || !is_op(items, *fraction, 0x35) ||
        !is_op(items, *condition, 0x5e) ||
        items.at(*condition).raw || !resolved_target(items.at(*operand), input_index,
                                                     options.address_space_model)
                                            .has_value() ||
        !is_op(items, *direct_return, kReturnOpcode)) {
      continue;
    }

    const std::optional<int> terminal_address =
        resolved_target(items.at(*operand), input_index, options.address_space_model);
    if (!terminal_address.has_value())
      continue;
    const auto terminal_cell = input_index.cell_items.find(*terminal_address);
    if (terminal_cell == input_index.cell_items.end())
      continue;
    const std::size_t dot = terminal_cell->second;
    const std::optional<std::size_t> stop = next_cell_item(items, dot);
    if (!is_op(items, dot, 0x0a) || !stop.has_value() ||
        !is_op(items, *stop, kStopOpcode) ||
        items.at(*stop).stop_disposition != StopDisposition::Terminal) {
      continue;
    }

    std::size_t terminal_block_begin = dot;
    while (terminal_block_begin > 0U &&
           items.at(terminal_block_begin - 1U).kind == MachineItemKind::Label) {
      --terminal_block_begin;
    }
    const std::size_t terminal_block_end = *stop + 1U;
    if (terminal_block_begin <= *direct_return || terminal_block_end > items.size())
      continue;

    std::optional<std::size_t> predecessor =
        previous_cell_item(items, terminal_block_begin);
    if (predecessor.has_value() &&
        items.at(*predecessor).kind == MachineItemKind::Address) {
      predecessor = previous_cell_item(items, *predecessor);
    }
    if (!predecessor.has_value() ||
        !is_non_fallthrough_command(items.at(*predecessor))) {
      continue;
    }

    bool exclusive_terminal_entry = true;
    for (std::size_t source = 0; source < items.size(); ++source) {
      if (items.at(source).kind != MachineItemKind::Op ||
          !is_direct_flow_opcode(items.at(source).opcode)) {
        continue;
      }
      const std::optional<std::size_t> source_operand = next_cell_item(items, source);
      if (!source_operand.has_value()) {
        exclusive_terminal_entry = false;
        break;
      }
      const std::optional<int> target = resolved_target(
          items.at(*source_operand), input_index, options.address_space_model);
      if (!target.has_value()) {
        exclusive_terminal_entry = false;
        break;
      }
      const auto target_cell = input_index.cell_items.find(*target);
      if (target_cell == input_index.cell_items.end()) {
        exclusive_terminal_entry = false;
        break;
      }
      if ((target_cell->second == dot || target_cell->second == *stop) &&
          !(source == *condition && target_cell->second == dot)) {
        exclusive_terminal_entry = false;
        break;
      }
    }
    for (const auto& [source, targets] : control_flow.indirect_flow_targets) {
      (void)source;
      for (const PostLayoutCommandIdentity& target : targets) {
        if (target.item_index == dot || target.item_index == *stop)
          exclusive_terminal_entry = false;
      }
    }
    for (const PostLayoutExternalEntryState& entry : control_flow.external_entries) {
      if (entry.entry.item_index == dot || entry.entry.item_index == *stop)
        exclusive_terminal_entry = false;
      for (const PostLayoutCommandIdentity& return_slot : entry.return_stack) {
        if (return_slot.item_index == dot || return_slot.item_index == *stop)
          exclusive_terminal_entry = false;
      }
    }
    if (!exclusive_terminal_entry)
      continue;

    std::vector<std::optional<std::size_t>> relocation(items.size());
    std::vector<MachineItem> reordered;
    reordered.reserve(items.size());
    const auto append_old_item = [&](std::size_t old_item) {
      relocation.at(old_item) = reordered.size();
      reordered.push_back(items.at(old_item));
    };
    for (std::size_t old_item = 0; old_item < items.size(); ++old_item) {
      if (old_item == *direct_return) {
        for (std::size_t moved = terminal_block_begin; moved < terminal_block_end; ++moved)
          append_old_item(moved);
      }
      if (old_item >= terminal_block_begin && old_item < terminal_block_end)
        continue;
      append_old_item(old_item);
    }
    if (reordered.size() != items.size())
      continue;

    const ItemRelocation relocate = [relocation](std::size_t old_item)
        -> std::optional<std::size_t> {
      return old_item < relocation.size() ? relocation.at(old_item) : std::nullopt;
    };
    const ArtifactIndex output_index = index_artifact(reordered);
    const std::size_t new_condition = *relocation.at(*condition);
    reordered.at(new_condition).opcode = 0x57;
    reordered.at(new_condition).name = "F x!=0";

    std::set<std::size_t> changed_items = {*condition};
    bool direct_edges_rebound = true;
    for (std::size_t source = 0; source < items.size(); ++source) {
      if (items.at(source).kind != MachineItemKind::Op ||
          !is_direct_flow_opcode(items.at(source).opcode)) {
        continue;
      }
      const std::optional<std::size_t> old_operand = next_cell_item(items, source);
      if (!old_operand.has_value()) {
        direct_edges_rebound = false;
        break;
      }
      const std::optional<int> old_target = resolved_target(
          items.at(*old_operand), input_index, options.address_space_model);
      if (!old_target.has_value()) {
        direct_edges_rebound = false;
        break;
      }
      const auto old_target_cell = input_index.cell_items.find(*old_target);
      if (old_target_cell == input_index.cell_items.end()) {
        direct_edges_rebound = false;
        break;
      }
      const std::size_t target_item =
          source == *condition ? *direct_return : old_target_cell->second;
      if (!relocation.at(source).has_value() || !relocation.at(*old_operand).has_value() ||
          !relocation.at(target_item).has_value()) {
        direct_edges_rebound = false;
        break;
      }
      MachineItem& new_operand = reordered.at(*relocation.at(*old_operand));
      if (source == *condition) {
        const std::optional<std::string> target_label = label_for_address(
            input_index, input_index.item_addresses.at(target_item));
        if (target_label.has_value()) {
          rewrite_direct_operand_to_label(new_operand, *target_label);
        } else if (!rewrite_direct_operand(
                       new_operand,
                       output_index.item_addresses.at(*relocation.at(target_item)))) {
          direct_edges_rebound = false;
          break;
        }
        changed_items.insert(*old_operand);
      } else if (!(items.at(*old_operand).kind == MachineItemKind::Address &&
                   std::holds_alternative<std::string>(items.at(*old_operand).target))) {
        if (!rewrite_direct_operand(
                new_operand,
                output_index.item_addresses.at(*relocation.at(target_item)))) {
          direct_edges_rebound = false;
          break;
        }
        changed_items.insert(*old_operand);
      }
    }
    if (!direct_edges_rebound)
      continue;

    bool fixed_indirect_targets = true;
    for (const auto& [source, targets] : control_flow.indirect_flow_targets) {
      (void)source;
      for (const PostLayoutCommandIdentity& target : targets) {
        if (!relocation.at(target.item_index).has_value() ||
            output_index.item_addresses.at(*relocation.at(target.item_index)) != target.address) {
          fixed_indirect_targets = false;
        }
      }
    }
    if (!fixed_indirect_targets)
      continue;

    ReboundArtifact rebound = rebind_control_flow_after_relocation(
        items, std::move(reordered), control_flow, relocate, {}, changed_items, {}, options);
    if (rebound.proved)
      candidates.push_back(std::move(rebound));
  }
  return candidates;
}


std::vector<ReboundArtifact> normalize_empty_return_startup_layouts(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& control_flow,
    const TerminalCyclicLayoutOptions& options,
    std::vector<std::string>* rejection_reasons) {
  const ArtifactIndex input_index = index_artifact(items);
  std::vector<ReboundArtifact> candidates;
  const auto reject = [&](std::string reason) {
    if (rejection_reasons != nullptr)
      add_reason(*rejection_reasons, std::move(reason));
  };
  const auto zero = input_index.cell_items.find(0);
  if (zero == input_index.cell_items.end() || is_op(items, zero->second, kReturnOpcode) ||
      !control_flow.empty_return_target.has_value() ||
      control_flow.empty_return_target->address != 1) {
    reject("physical-00 startup is already a return or the empty-return-to-01 fact is absent");
    return candidates;
  }
  const auto input_main = std::find_if(
      control_flow.external_entries.begin(), control_flow.external_entries.end(),
      [](const PostLayoutExternalEntryState& entry) {
        return entry.kind == ExternalEntryKind::Main;
      });
  if (input_main == control_flow.external_entries.end() ||
      input_main->entry.item_index != zero->second || !input_main->return_stack.empty()) {
    reject("main entry is not the physical-00 command with an empty return stack");
    return candidates;
  }

  bool found_zero_jump = false;
  for (std::size_t jump = 0; jump < items.size(); ++jump) {
    if (!is_op(items, jump, kJumpOpcode))
      continue;
    const std::optional<std::size_t> operand = next_cell_item(items, jump);
    if (!operand.has_value() ||
        !resolved_target(items.at(*operand), input_index, options.address_space_model)
             .has_value()) {
      continue;
    }
    const std::optional<int> target =
        resolved_target(items.at(*operand), input_index, options.address_space_model);
    if (!target.has_value() || *target != 0) {
      continue;
    }
    found_zero_jump = true;

    std::vector<std::string> stack_reasons;
    const std::vector<std::vector<int>> stacks = reachable_return_stacks(
        items, input_index, control_flow, input_index.item_addresses.at(jump), options,
        stack_reasons);
    if (!stack_reasons.empty() || stacks.empty() ||
        std::any_of(stacks.begin(), stacks.end(),
                    [](const std::vector<int>& stack) { return !stack.empty(); })) {
      reject("direct BP 00 is not proved reachable only with an empty return stack");
      continue;
    }

    std::vector<std::optional<std::size_t>> relocation(items.size());
    std::vector<MachineItem> rewritten;
    rewritten.reserve(items.size());
    MachineItem startup_return = MachineItem::op(kReturnOpcode, "В/О");
    startup_return.comment = "transparent empty-stack startup return to physical 01";
    rewritten.push_back(std::move(startup_return));
    for (std::size_t old_item = 0; old_item < items.size(); ++old_item) {
      if (old_item == *operand)
        continue;
      relocation.at(old_item) = rewritten.size();
      MachineItem item = items.at(old_item);
      if (old_item == jump) {
        item.opcode = kReturnOpcode;
        item.name = "В/О";
        item.comment = "empty-stack loop return to physical 01";
      }
      rewritten.push_back(std::move(item));
    }
    const ArtifactIndex output_index = index_artifact(rewritten);

    bool direct_edges_rebound = true;
    for (std::size_t source = 0; source < items.size(); ++source) {
      if (source == jump || items.at(source).kind != MachineItemKind::Op ||
          !is_direct_flow_opcode(items.at(source).opcode)) {
        continue;
      }
      const std::optional<std::size_t> old_operand = next_cell_item(items, source);
      if (!old_operand.has_value() || !relocation.at(source).has_value() ||
          !relocation.at(*old_operand).has_value()) {
        direct_edges_rebound = false;
        break;
      }
      const std::optional<int> old_target = resolved_target(
          items.at(*old_operand), input_index, options.address_space_model);
      if (!old_target.has_value()) {
        direct_edges_rebound = false;
        break;
      }
      const auto old_target_cell = input_index.cell_items.find(*old_target);
      if (old_target_cell == input_index.cell_items.end() ||
          !relocation.at(old_target_cell->second).has_value()) {
        direct_edges_rebound = false;
        break;
      }
      MachineItem& new_operand = rewritten.at(*relocation.at(*old_operand));
      if (!(items.at(*old_operand).kind == MachineItemKind::Address &&
            std::holds_alternative<std::string>(items.at(*old_operand).target))) {
        if (!rewrite_direct_operand(
                new_operand,
                output_index.item_addresses.at(*relocation.at(old_target_cell->second)))) {
          direct_edges_rebound = false;
          break;
        }
      }
    }
    if (!direct_edges_rebound)
      reject("startup layout could not rebind every direct target identity");
    if (!direct_edges_rebound)
      continue;

    for (MachineItem& item : rewritten) {
      item.indirect_flow_targets.reset();
      item.indirect_memory_targets.reset();
    }
    bool indirect_facts_rebound = true;
    for (const auto& [old_source, old_targets] : control_flow.indirect_flow_targets) {
      if (!relocation.at(old_source).has_value()) {
        indirect_facts_rebound = false;
        break;
      }
      std::vector<IrTarget> rebound_targets;
      for (const PostLayoutCommandIdentity& old_target : old_targets) {
        if (!relocation.at(old_target.item_index).has_value()) {
          indirect_facts_rebound = false;
          break;
        }
        const int new_address =
            output_index.item_addresses.at(*relocation.at(old_target.item_index));
        if (new_address != old_target.address) {
          indirect_facts_rebound = false;
          break;
        }
        rebound_targets.emplace_back(new_address);
      }
      if (!indirect_facts_rebound)
        break;
      rewritten.at(*relocation.at(old_source)).indirect_flow_targets =
          std::move(rebound_targets);
    }
    for (const auto& [old_source, targets] : control_flow.indirect_memory_targets) {
      if (!relocation.at(old_source).has_value()) {
        indirect_facts_rebound = false;
        break;
      }
      rewritten.at(*relocation.at(old_source)).indirect_memory_targets = targets;
    }
    if (!indirect_facts_rebound)
      reject("startup layout moved an indirect target or lost typed indirect metadata");
    if (!indirect_facts_rebound)
      continue;

    const PostLayoutControlFlowOptions flow_options{
        .address_space_model = options.address_space_model,
        .maximum_return_depth = options.maximum_return_depth,
        .maximum_execution_states =
            static_cast<std::size_t>(options.maximum_execution_states),
        .main_entry = 0,
        .empty_return_target = 1,
    };
    AuthoritativePostLayoutControlFlow rewritten_flow =
        build_post_layout_control_flow(rewritten, flow_options);
    if (!rewritten_flow.proved || !rewritten_flow.empty_return_target.has_value() ||
        rewritten_flow.empty_return_target->address != 1 ||
        rewritten_flow.empty_return_target->item_index != *relocation.at(zero->second) ||
        rewritten_flow.external_entries.size() != control_flow.external_entries.size()) {
      reject("startup layout did not rebuild an equivalent authoritative CFG");
      continue;
    }

    bool external_entries_rebound = true;
    for (const PostLayoutExternalEntryState& old_entry : control_flow.external_entries) {
      if (old_entry.kind == ExternalEntryKind::Main) {
        const auto new_main = std::find_if(
            rewritten_flow.external_entries.begin(), rewritten_flow.external_entries.end(),
            [](const PostLayoutExternalEntryState& entry) {
              return entry.kind == ExternalEntryKind::Main;
            });
        if (new_main == rewritten_flow.external_entries.end() ||
            new_main->entry.address != 0 || !new_main->return_stack.empty()) {
          external_entries_rebound = false;
        }
        continue;
      }
      if (!relocation.at(old_entry.entry.item_index).has_value()) {
        external_entries_rebound = false;
        break;
      }
      std::vector<std::size_t> expected_stack;
      for (const PostLayoutCommandIdentity& slot : old_entry.return_stack) {
        if (!relocation.at(slot.item_index).has_value()) {
          external_entries_rebound = false;
          break;
        }
        expected_stack.push_back(*relocation.at(slot.item_index));
      }
      if (!external_entries_rebound)
        break;
      const bool found = std::any_of(
          rewritten_flow.external_entries.begin(), rewritten_flow.external_entries.end(),
          [&](const PostLayoutExternalEntryState& candidate) {
            std::vector<std::size_t> candidate_stack;
            for (const PostLayoutCommandIdentity& slot : candidate.return_stack)
              candidate_stack.push_back(slot.item_index);
            return candidate.kind == old_entry.kind &&
                   candidate.manual_interaction == old_entry.manual_interaction &&
                   candidate.entry.item_index == *relocation.at(old_entry.entry.item_index) &&
                   candidate_stack == expected_stack;
          });
      if (!found) {
        external_entries_rebound = false;
        break;
      }
    }
    if (!external_entries_rebound)
      reject("startup layout changed a resumable external entry or return-stack identity");
    if (!external_entries_rebound)
      continue;

    bool indirect_identities_match =
        rewritten_flow.indirect_flow_targets.size() ==
            control_flow.indirect_flow_targets.size() &&
        rewritten_flow.indirect_memory_targets.size() ==
            control_flow.indirect_memory_targets.size();
    for (const auto& [old_source, old_targets] : control_flow.indirect_flow_targets) {
      if (!indirect_identities_match || !relocation.at(old_source).has_value()) {
        indirect_identities_match = false;
        break;
      }
      const auto found = rewritten_flow.indirect_flow_targets.find(*relocation.at(old_source));
      if (found == rewritten_flow.indirect_flow_targets.end() ||
          found->second.size() != old_targets.size()) {
        indirect_identities_match = false;
        break;
      }
      for (std::size_t index = 0; index < old_targets.size(); ++index) {
        if (!relocation.at(old_targets.at(index).item_index).has_value() ||
            found->second.at(index).item_index !=
                *relocation.at(old_targets.at(index).item_index)) {
          indirect_identities_match = false;
          break;
        }
      }
    }
    if (!indirect_identities_match)
      reject("startup layout changed an authoritative indirect command identity");
    if (!indirect_identities_match)
      continue;

    candidates.push_back(ReboundArtifact{
        .items = std::move(rewritten),
        .control_flow = std::move(rewritten_flow),
        .proved = true,
    });
  }
  if (!found_zero_jump)
    reject("artifact contains no direct BP 00 startup loop candidate");
  return candidates;
}

void append_reasons(std::vector<std::string>& destination, const std::vector<std::string>& source) {
  for (const std::string& reason : source)
    add_reason(destination, reason);
}

} // namespace

std::optional<std::vector<MachineItem>>
symbolize_terminal_layout_direct_targets(const std::vector<MachineItem>& items,
                                         AddressSpaceModel model) {
  const ArtifactIndex index = index_artifact(items);
  std::map<std::size_t, std::size_t> target_by_operand;
  for (std::size_t source = 0; source < items.size(); ++source) {
    if (items.at(source).kind != MachineItemKind::Op ||
        !is_direct_flow_opcode(items.at(source).opcode)) {
      continue;
    }
    const std::optional<std::size_t> operand = next_cell_item(items, source);
    if (!operand.has_value())
      return std::nullopt;
    const std::optional<int> target = resolved_target(items.at(*operand), index, model);
    if (!target.has_value())
      return std::nullopt;
    const auto target_item = index.cell_items.find(*target);
    if (target_item == index.cell_items.end() ||
        items.at(target_item->second).kind != MachineItemKind::Op) {
      return std::nullopt;
    }
    target_by_operand.emplace(*operand, target_item->second);
  }

  std::set<std::string> names;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label)
      names.insert(item.name);
  }
  std::map<std::size_t, std::string> label_by_target;
  int ordinal = 0;
  for (const auto& [operand, target] : target_by_operand) {
    (void)operand;
    if (label_by_target.contains(target))
      continue;
    std::string label;
    do {
      label = "__terminal_direct_identity_" + std::to_string(ordinal++);
    } while (names.contains(label));
    names.insert(label);
    label_by_target.emplace(target, std::move(label));
  }

  std::vector<MachineItem> symbolized;
  symbolized.reserve(items.size() + label_by_target.size());
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    if (const auto label = label_by_target.find(item_index); label != label_by_target.end())
      symbolized.push_back(MachineItem::label(label->second));
    const auto operand = target_by_operand.find(item_index);
    if (operand == target_by_operand.end()) {
      symbolized.push_back(items.at(item_index));
      continue;
    }
    const std::optional<std::string> comment = items.at(item_index).comment;
    const std::vector<std::string> roles = items.at(item_index).roles;
    MachineItem address = MachineItem::address(label_by_target.at(operand->second));
    address.comment = comment;
    address.roles = roles;
    symbolized.push_back(std::move(address));
  }
  return symbolized;
}

NaturalTargetComponentLayoutResult optimize_terminal_shared_return_selector_layout(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control_flow,
    const NaturalTargetComponentLayoutOptions& natural_options,
    const TerminalCyclicLayoutOptions& terminal_options) {
  NaturalTargetComponentLayoutResult rejected;
  rejected.items = items;
  rejected.preloads = preloads;
  const auto reject = [&](std::string reason) {
    add_reason(rejected.plan.reasons,
               "terminal shared-return selector: " + std::move(reason));
  };
  if (natural_options.address_space_model != terminal_options.address_space_model ||
      natural_options.address_space_model != AddressSpaceModel::Standard) {
    reject("address-space models differ or are not standard");
    return rejected;
  }

  const ArtifactIndex input_index = index_artifact(items);
  std::vector<std::string> control_reasons;
  if (!validate_complete_control_flow(items, input_index, control_flow,
                                      terminal_options, control_reasons)) {
    for (const std::string& reason : control_reasons)
      reject(reason);
    return rejected;
  }

  struct TerminalTail {
    std::size_t condition = 0;
    std::size_t operand = 0;
    std::size_t local_return = 0;
    std::size_t terminal_block_begin = 0;
    std::size_t dot = 0;
    std::size_t stop = 0;
    std::size_t terminal_block_end = 0;
  };
  std::optional<TerminalTail> tail;
  for (std::size_t mask = 0; mask < items.size() && !tail.has_value(); ++mask) {
    if (!is_op(items, mask, 0x37))
      continue;
    const std::optional<std::size_t> fraction = next_cell_item(items, mask);
    const std::optional<std::size_t> condition =
        fraction.has_value() ? next_cell_item(items, *fraction) : std::nullopt;
    const std::optional<std::size_t> operand =
        condition.has_value() ? next_cell_item(items, *condition) : std::nullopt;
    const std::optional<std::size_t> local_return =
        operand.has_value() ? next_cell_item(items, *operand) : std::nullopt;
    if (!fraction.has_value() || !condition.has_value() || !operand.has_value() ||
        !local_return.has_value() || *operand != *condition + 1U ||
        *local_return != *operand + 1U || !is_op(items, *fraction, 0x35) ||
        !is_op(items, *condition, 0x5e) || items.at(*condition).raw ||
        !is_op(items, *local_return, kReturnOpcode)) {
      continue;
    }
    const std::optional<int> terminal_address =
        resolved_target(items.at(*operand), input_index,
                        terminal_options.address_space_model);
    if (!terminal_address.has_value())
      continue;
    const auto terminal_cell = input_index.cell_items.find(*terminal_address);
    if (terminal_cell == input_index.cell_items.end())
      continue;
    const std::size_t dot = terminal_cell->second;
    const std::optional<std::size_t> stop = next_cell_item(items, dot);
    if (!is_op(items, dot, 0x0a) || !stop.has_value() ||
        !is_op(items, *stop, kStopOpcode) ||
        items.at(*stop).stop_disposition != StopDisposition::Terminal ||
        dot <= *local_return) {
      continue;
    }
    std::size_t terminal_block_begin = dot;
    while (terminal_block_begin > 0U &&
           items.at(terminal_block_begin - 1U).kind == MachineItemKind::Label) {
      --terminal_block_begin;
    }
    const std::size_t terminal_block_end = *stop + 1U;
    const std::optional<std::size_t> predecessor =
        previous_cell_item(items, terminal_block_begin);
    if (!predecessor.has_value() ||
        !is_non_fallthrough_command(items.at(*predecessor))) {
      continue;
    }

    bool exclusive_terminal_entry = true;
    for (std::size_t source = 0; source < items.size(); ++source) {
      if (items.at(source).kind != MachineItemKind::Op ||
          !is_direct_flow_opcode(items.at(source).opcode)) {
        continue;
      }
      const std::optional<std::size_t> source_operand = next_cell_item(items, source);
      const std::optional<int> target =
          source_operand.has_value()
              ? resolved_target(items.at(*source_operand), input_index,
                                terminal_options.address_space_model)
              : std::nullopt;
      if (!target.has_value()) {
        exclusive_terminal_entry = false;
        break;
      }
      const auto target_cell = input_index.cell_items.find(*target);
      if (target_cell == input_index.cell_items.end()) {
        exclusive_terminal_entry = false;
        break;
      }
      if ((target_cell->second == dot || target_cell->second == *stop) &&
          !(source == *condition && target_cell->second == dot)) {
        exclusive_terminal_entry = false;
        break;
      }
    }
    for (const auto& [source, targets] : control_flow.indirect_flow_targets) {
      (void)source;
      if (std::any_of(targets.begin(), targets.end(), [&](const auto& target) {
            return target.item_index == dot || target.item_index == *stop;
          })) {
        exclusive_terminal_entry = false;
      }
    }
    for (const PostLayoutExternalEntryState& entry : control_flow.external_entries) {
      if (entry.entry.item_index == dot || entry.entry.item_index == *stop ||
          std::any_of(entry.return_stack.begin(), entry.return_stack.end(),
                      [&](const PostLayoutCommandIdentity& slot) {
                        return slot.item_index == dot || slot.item_index == *stop;
                      })) {
        exclusive_terminal_entry = false;
      }
    }
    if (exclusive_terminal_entry) {
      tail = TerminalTail{
          .condition = *condition,
          .operand = *operand,
          .local_return = *local_return,
          .terminal_block_begin = terminal_block_begin,
          .dot = dot,
          .stop = *stop,
          .terminal_block_end = terminal_block_end,
      };
    }
  }
  if (!tail.has_value()) {
    reject("no exclusive K-AND/K-frac/Fx=0 terminal tail");
    return rejected;
  }

  const OpcodeInfo& direct_condition = opcode_by_code(0x5e);
  const OpcodeInfo& shared_return_info = opcode_by_code(kReturnOpcode);
  if (direct_condition.stack_effect != StackEffect::Preserves ||
      !direct_condition.conditional_x2_effect.has_value() ||
      direct_condition.conditional_x2_effect->jump != X2Effect::Preserves ||
      shared_return_info.x2_effect != X2Effect::Affects) {
    reject("opcode contracts do not prove the direct side of the terminal fold");
    return rejected;
  }

  constexpr std::string_view kConditionRole =
      "terminal-shared-return-selector:condition";
  constexpr std::string_view kDotRole =
      "terminal-shared-return-selector:dot";
  constexpr std::string_view kStopRole =
      "terminal-shared-return-selector:stop";
  constexpr std::string_view kReturnRole =
      "terminal-shared-return-selector:shared-return";
  constexpr std::string_view kLateBoundRole =
      "late-decimal-selector-consumer";
  const auto add_role = [](MachineItem& item, std::string_view role) {
    if (std::find(item.roles.begin(), item.roles.end(), role) == item.roles.end())
      item.roles.emplace_back(role);
  };
  const auto unique_role_item = [](const std::vector<MachineItem>& artifact,
                                   std::string_view role)
      -> std::optional<std::size_t> {
    std::optional<std::size_t> found;
    for (std::size_t item = 0; item < artifact.size(); ++item) {
      if (std::find(artifact.at(item).roles.begin(), artifact.at(item).roles.end(),
                    role) == artifact.at(item).roles.end()) {
        continue;
      }
      if (found.has_value())
        return std::nullopt;
      found = item;
    }
    return found;
  };
  const auto relocated_external_entries_match = [](
      const AuthoritativePostLayoutControlFlow& before,
      const AuthoritativePostLayoutControlFlow& after,
      const std::vector<std::optional<std::size_t>>& relocation) {
    if (before.external_entries.size() != after.external_entries.size())
      return false;
    for (const PostLayoutExternalEntryState& old_entry : before.external_entries) {
      if (old_entry.entry.item_index >= relocation.size() ||
          !relocation.at(old_entry.entry.item_index).has_value()) {
        return false;
      }
      std::vector<std::size_t> stack;
      for (const PostLayoutCommandIdentity& slot : old_entry.return_stack) {
        if (slot.item_index >= relocation.size() ||
            !relocation.at(slot.item_index).has_value()) {
          return false;
        }
        stack.push_back(*relocation.at(slot.item_index));
      }
      const bool present = std::any_of(
          after.external_entries.begin(), after.external_entries.end(),
          [&](const PostLayoutExternalEntryState& candidate) {
            std::vector<std::size_t> candidate_stack;
            for (const PostLayoutCommandIdentity& slot : candidate.return_stack)
              candidate_stack.push_back(slot.item_index);
            return candidate.entry.item_index ==
                       *relocation.at(old_entry.entry.item_index) &&
                   candidate_stack == stack && candidate.kind == old_entry.kind &&
                   candidate.manual_interaction == old_entry.manual_interaction;
          });
      if (!present)
        return false;
    }
    return true;
  };

  const std::map<int, std::string> stable_preloads = unique_stable_preloads(preloads);
  std::optional<NaturalTargetComponentLayoutResult> best;
  int best_selector = -1;
  std::vector<std::string> candidate_reasons;
  for (const auto& [selector, raw_value] : stable_preloads) {
    const std::string selector_name = "R" + register_text(selector);
    if (!selector_is_unwritten(items, selector, control_flow)) {
      candidate_reasons.push_back(selector_name + " is written at runtime");
      continue;
    }
    if (raw_value.empty()) {
      candidate_reasons.push_back(selector_name + " has no delivered preload");
      continue;
    }

    bool conflicting_flow = false;
    for (const auto& [source, targets] : control_flow.indirect_flow_targets) {
      const std::optional<int> source_selector = encoded_register(items.at(source).opcode);
      if (!source_selector.has_value() || *source_selector != selector)
        continue;
      if (targets.size() != 1U ||
          targets.front().item_index != tail->local_return) {
        conflicting_flow = true;
        break;
      }
    }
    if (conflicting_flow) {
      candidate_reasons.push_back(selector_name +
                                  " already selects another flow identity");
      continue;
    }

    const OpcodeInfo& indirect_condition = opcode_by_code(0xe0 + selector);
    const OpcodeInfo& indirect_jump = opcode_by_code(0x80 + selector);
    if (indirect_condition.stack_effect != StackEffect::Preserves ||
        indirect_jump.stack_effect != StackEffect::Preserves ||
        !indirect_condition.conditional_x2_effect.has_value() ||
        indirect_condition.conditional_x2_effect->fallthrough != X2Effect::Preserves ||
        indirect_condition.conditional_x2_effect->jump != X2Effect::Preserves ||
        indirect_jump.x2_effect != X2Effect::Preserves) {
      candidate_reasons.push_back(selector_name +
                                  " opcode family lacks the required stack/X2 contract");
      continue;
    }

    std::vector<std::optional<std::size_t>> staging_relocation(items.size());
    std::vector<MachineItem> staged;
    staged.reserve(items.size());
    for (std::size_t old_item = 0; old_item < items.size(); ++old_item) {
      if (old_item == tail->condition) {
        staging_relocation.at(old_item) = staged.size();
        MachineItem folded = items.at(old_item);
        folded.opcode = 0xe0 + selector;
        folded.name = indirect_condition.name;
        folded.comment = "proved complementary terminal branch";
        add_role(folded, kConditionRole);
        add_role(folded, kLateBoundRole);
        staged.push_back(std::move(folded));
        for (std::size_t moved = tail->terminal_block_begin;
             moved < tail->terminal_block_end; ++moved) {
          staging_relocation.at(moved) = staged.size();
          MachineItem item = items.at(moved);
          if (moved == tail->dot)
            add_role(item, kDotRole);
          if (moved == tail->stop)
            add_role(item, kStopRole);
          staged.push_back(std::move(item));
        }
        continue;
      }
      if (old_item == tail->operand ||
          (old_item >= tail->terminal_block_begin &&
           old_item < tail->terminal_block_end)) {
        continue;
      }
      staging_relocation.at(old_item) = staged.size();
      MachineItem item = items.at(old_item);
      if (old_item == tail->local_return)
        add_role(item, kReturnRole);
      staged.push_back(std::move(item));
    }
    const ArtifactIndex staged_index = index_artifact(staged);
    bool staging_rebound = true;
    for (std::size_t source = 0; source < items.size(); ++source) {
      if (source == tail->condition || items.at(source).kind != MachineItemKind::Op ||
          !is_direct_flow_opcode(items.at(source).opcode)) {
        continue;
      }
      const std::optional<std::size_t> old_operand = next_cell_item(items, source);
      const std::optional<int> old_target =
          old_operand.has_value()
              ? resolved_target(items.at(*old_operand), input_index,
                                terminal_options.address_space_model)
              : std::nullopt;
      if (!old_operand.has_value() || !old_target.has_value() ||
          !staging_relocation.at(source).has_value() ||
          !staging_relocation.at(*old_operand).has_value()) {
        staging_rebound = false;
        break;
      }
      const auto target_cell = input_index.cell_items.find(*old_target);
      if (target_cell == input_index.cell_items.end() ||
          !staging_relocation.at(target_cell->second).has_value() ||
          !rewrite_direct_operand(
              staged.at(*staging_relocation.at(*old_operand)),
              staged_index.item_addresses.at(
                  *staging_relocation.at(target_cell->second)))) {
        staging_rebound = false;
        break;
      }
    }
    if (!staging_rebound ||
        !staging_relocation.at(tail->local_return).has_value()) {
      candidate_reasons.push_back("staged direct targets cannot be rebound");
      continue;
    }

    for (MachineItem& item : staged) {
      item.indirect_flow_targets.reset();
      item.indirect_memory_targets.reset();
    }
    bool staging_metadata = true;
    for (const auto& [source, targets] : control_flow.indirect_flow_targets) {
      if (!staging_relocation.at(source).has_value()) {
        staging_metadata = false;
        break;
      }
      std::vector<IrTarget> metadata;
      for (const PostLayoutCommandIdentity& target : targets) {
        if (!staging_relocation.at(target.item_index).has_value()) {
          staging_metadata = false;
          break;
        }
        metadata.emplace_back(staged_index.item_addresses.at(
            *staging_relocation.at(target.item_index)));
      }
      if (!staging_metadata)
        break;
      staged.at(*staging_relocation.at(source)).indirect_flow_targets =
          std::move(metadata);
    }
    for (const auto& [source, targets] : control_flow.indirect_memory_targets) {
      if (!staging_relocation.at(source).has_value()) {
        staging_metadata = false;
        break;
      }
      staged.at(*staging_relocation.at(source)).indirect_memory_targets = targets;
    }
    const std::size_t staged_condition =
        *staging_relocation.at(tail->condition);
    const std::size_t staged_return =
        *staging_relocation.at(tail->local_return);
    staged.at(staged_condition).indirect_flow_targets =
        std::vector<IrTarget>{staged_index.item_addresses.at(staged_return)};
    if (!staging_metadata) {
      candidate_reasons.push_back("staged typed control-flow facts cannot be rebound");
      continue;
    }

    const auto main_entry = std::find_if(
        control_flow.external_entries.begin(), control_flow.external_entries.end(),
        [](const PostLayoutExternalEntryState& entry) {
          return entry.kind == ExternalEntryKind::Main;
        });
    if (main_entry == control_flow.external_entries.end() ||
        !staging_relocation.at(main_entry->entry.item_index).has_value()) {
      candidate_reasons.push_back("staged main entry cannot be rebound");
      continue;
    }
    PostLayoutControlFlowOptions staged_flow_options;
    staged_flow_options.address_space_model = terminal_options.address_space_model;
    staged_flow_options.maximum_return_depth = terminal_options.maximum_return_depth;
    staged_flow_options.maximum_execution_states =
        static_cast<std::size_t>(terminal_options.maximum_execution_states);
    staged_flow_options.main_entry = staged_index.item_addresses.at(
        *staging_relocation.at(main_entry->entry.item_index));
    if (control_flow.empty_return_target.has_value()) {
      if (!staging_relocation.at(
               control_flow.empty_return_target->item_index).has_value()) {
        candidate_reasons.push_back("staged empty-return target cannot be rebound");
        continue;
      }
      staged_flow_options.empty_return_target = staged_index.item_addresses.at(
          *staging_relocation.at(control_flow.empty_return_target->item_index));
    }
    const AuthoritativePostLayoutControlFlow staged_flow =
        build_post_layout_control_flow(staged, staged_flow_options);
    std::vector<std::string> staged_reasons;
    if (!staged_flow.proved ||
        !validate_complete_control_flow(staged, staged_index, staged_flow,
                                        terminal_options, staged_reasons) ||
        !relocated_external_entries_match(control_flow, staged_flow,
                                          staging_relocation)) {
      candidate_reasons.push_back(
          staged_flow.reasons.empty() ? "staged CFG is not complete"
                                      : "staged CFG: " + staged_flow.reasons.front());
      continue;
    }

    NaturalTargetComponentLayoutOptions transactional_options = natural_options;
    transactional_options.allow_size_neutral_flow_rebind = false;
    transactional_options.required_flow_selectors.clear();
    bool required_bindings_relocated = true;
    for (const NaturalTargetRequiredFlowSelector& required :
         natural_options.required_flow_selectors) {
      const std::optional<int> required_selector =
          normalized_register_index(required.register_name);
      if (required_selector.has_value() && *required_selector == selector)
        continue;
      if (required.command_item >= staging_relocation.size() ||
          !staging_relocation.at(required.command_item).has_value()) {
        required_bindings_relocated = false;
        break;
      }
      transactional_options.required_flow_selectors.push_back(
          NaturalTargetRequiredFlowSelector{
              .command_item =
                  *staging_relocation.at(required.command_item),
              .register_name = required.register_name,
          });
    }
    if (!required_bindings_relocated) {
      candidate_reasons.push_back(
          "a required selector binding cannot be relocated into the folded artifact");
      continue;
    }
    transactional_options.required_selector_targets.clear();
    for (const NaturalTargetRequiredSelectorTarget& required :
         natural_options.required_selector_targets) {
      if (required.target_item >= staging_relocation.size() ||
          !staging_relocation.at(required.target_item).has_value()) {
        required_bindings_relocated = false;
        break;
      }
      transactional_options.required_selector_targets.push_back(
          NaturalTargetRequiredSelectorTarget{
              .target_item =
                  *staging_relocation.at(required.target_item),
              .register_name = required.register_name,
          });
    }
    if (!required_bindings_relocated) {
      candidate_reasons.push_back(
          "a required selector target cannot be relocated into the folded artifact");
      continue;
    }
    transactional_options.required_selector_targets.push_back(
        NaturalTargetRequiredSelectorTarget{
            .target_item = staged_return,
            .register_name = register_text(selector),
        });
    NaturalTargetComponentLayoutResult natural =
        optimize_natural_target_component_layout(staged, preloads, staged_flow,
                                                 transactional_options);
    if (natural.applied <= 0 || !natural.plan.proved ||
        !natural.plan.final_artifact_proved || natural.removed_cells <= 0) {
      if (!natural.plan.reasons.empty())
        candidate_reasons.push_back(natural.plan.reasons.back());
      continue;
    }
    const std::optional<std::size_t> condition =
        unique_role_item(natural.items, kConditionRole);
    const std::optional<std::size_t> dot = unique_role_item(natural.items, kDotRole);
    const std::optional<std::size_t> stop = unique_role_item(natural.items, kStopRole);
    const std::optional<std::size_t> final_return =
        unique_role_item(natural.items, kReturnRole);
    if (!condition.has_value() || !dot.has_value() || !stop.has_value() ||
        !final_return.has_value() ||
        !is_op(natural.items, *condition, 0xe0 + selector) ||
        next_cell_item(natural.items, *condition) != dot ||
        next_cell_item(natural.items, *dot) != stop ||
        !is_op(natural.items, *dot, 0x0a) ||
        !is_op(natural.items, *stop, kStopOpcode) ||
        natural.items.at(*stop).stop_disposition != StopDisposition::Terminal ||
        !is_op(natural.items, *final_return, kReturnOpcode)) {
      candidate_reasons.push_back("selector layout changed the terminal fold shape");
      continue;
    }
    const ArtifactIndex natural_index = index_artifact(natural.items);
    const auto condition_targets =
        natural.plan.final_control_flow.indirect_flow_targets.find(*condition);
    if (condition_targets ==
            natural.plan.final_control_flow.indirect_flow_targets.end() ||
        condition_targets->second.size() != 1U ||
        condition_targets->second.front().item_index != *final_return) {
      candidate_reasons.push_back(
          "required selector target changed command identity");
      continue;
    }

    const auto delivered_preload = std::find_if(
        natural.preloads.begin(), natural.preloads.end(),
        [&](const PreloadReport& preload) {
          const std::optional<int> reg =
              normalized_register_index(preload.register_name);
          return reg.has_value() && *reg == selector;
        });
    std::optional<IndirectAddressEvaluation> final_decode;
    try {
      if (delivered_preload != natural.preloads.end()) {
        final_decode = evaluate_indirect_address(
            register_text(selector), delivered_preload->value,
            IndirectOperationKind::Flow, terminal_options.address_space_model);
      }
    } catch (const std::exception&) {
      final_decode.reset();
    }
    if (!final_decode.has_value() || !final_decode->actual_flow_target.has_value() ||
        *final_decode->actual_flow_target !=
            natural_index.item_addresses.at(*final_return)) {
      candidate_reasons.push_back(
          "delivered selector does not decode to the shared return");
      continue;
    }

    const ArtifactIndex final_index = natural_index;
    const AuthoritativePostLayoutControlFlow final_flow =
        natural.plan.final_control_flow;
    const auto final_condition_targets = condition_targets;
    natural.plan.runtime_selectors.push_back(
        NaturalTargetRuntimeSelectorProof{
            .original_command_item = tail->condition,
            .final_command_item = *condition,
            .original_target_item = tail->local_return,
            .register_name = register_text(selector),
            .delivered_preload = delivered_preload->value,
            .decoded_target = *final_decode->actual_flow_target,
            .final_target_address =
                final_condition_targets->second.front().address,
            .stable_mutation_class = true,
            .selector_unwritten = true,
            .typed_target_matches_runtime_decode = true,
        });

    const int output_cells = final_index.cells;
    if (output_cells >= input_index.cells) {
      candidate_reasons.push_back("atomic transaction has no net size saving");
      continue;
    }
    natural.plan.input_cells = input_index.cells;
    natural.plan.output_cells = output_cells;
    natural.plan.removed_cells = input_index.cells - output_cells;
    natural.plan.terminal_shared_return_folds = 1;
    natural.plan.size_neutral_flow_rebind = false;
    natural.plan.selector_register = register_text(selector);
    natural.plan.natural_target = *final_decode->actual_flow_target;
    natural.plan.final_control_flow = final_flow;
    natural.plan.final_artifact_proved = true;
    natural.plan.proved = natural.plan.control_flow_equivalent &&
                          natural.plan.call_return_equivalent &&
                          natural.plan.stack_and_x2_equivalent &&
                          natural.plan.indirect_memory_equivalent &&
                          natural.plan.data_projection_equivalent &&
                          natural.plan.final_artifact_proved &&
                          natural.plan.removed_cells > 0;
    if (!natural.plan.proved) {
      candidate_reasons.push_back("combined proof did not remain complete");
      continue;
    }
    natural.applied = std::max(1, natural.applied);
    natural.removed_cells = natural.plan.removed_cells;
    if (!best.has_value() || natural.plan.output_cells < best->plan.output_cells ||
        (natural.plan.output_cells == best->plan.output_cells &&
         selector < best_selector)) {
      best = std::move(natural);
      best_selector = selector;
    }
  }

  if (best.has_value())
    return std::move(*best);
  for (const std::string& reason : candidate_reasons)
    reject(reason);
  if (candidate_reasons.empty())
    reject("no stable non-conflicting selector can address the shared return");
  return rejected;
}
TerminalCyclicLayoutPlan
verify_terminal_cyclic_layout(const std::vector<MachineItem>& items,
                              const std::vector<PreloadReport>& preloads,
                              const AuthoritativePostLayoutControlFlow& control_flow,
                              const TerminalCyclicLayoutOptions& options) {
  TerminalCyclicLayoutPlan plan;
  const ArtifactIndex index = index_artifact(items);
  plan.input_cells = index.cells;
  plan.output_cells = index.cells;

  if (options.address_space_model != AddressSpaceModel::Standard) {
    add_reason(plan.reasons,
               "terminal/cyclic layout is proved only for the standard address space");
    return plan;
  }
  if (options.maximum_return_depth <= 0 || options.maximum_return_depth > 5 ||
      options.maximum_execution_states <= 0) {
    add_reason(plan.reasons, "terminal/cyclic execution bounds must be positive");
    if (options.maximum_return_depth > 5)
      add_reason(plan.reasons, "return-depth bound exceeds the five-level MK-61 hardware stack");
    return plan;
  }
  if (!validate_complete_control_flow(items, index, control_flow, options, plan.reasons))
    return plan;

  const std::vector<TailCandidate> tails =
      discover_tail_candidates(items, index, options.address_space_model);
  if (tails.empty()) {
    add_reason(plan.reasons, "artifact contains no structural terminal report tail");
    return plan;
  }
  const std::map<int, std::string> selectors = unique_stable_preloads(preloads);
  if (selectors.empty()) {
    add_reason(plan.reasons, "no unique delivered stable preload can select physical 00");
    return plan;
  }

  std::vector<std::string> candidate_reasons;
  std::optional<TailCandidate> selected_tail;
  for (const TailCandidate& tail : tails) {
    std::vector<std::string> local_reasons;
    if (items.at(tail.stop).stop_disposition != StopDisposition::Terminal) {
      add_reason(local_reasons, "provisional terminal STOP lacks compiler-owned halt provenance");
    }
    const int condition_address = index.item_addresses.at(tail.condition);
    const std::vector<std::vector<int>> return_stacks = reachable_return_stacks(
        items, index, control_flow, condition_address, options, local_reasons);
    const ContinuationAnalysis continuation =
        analyze_continuation(items, index, control_flow, index.item_addresses.at(tail.continuation),
                             return_stacks, options);
    append_reasons(local_reasons, continuation.reasons);
    const bool relocation_safe =
        relocation_facts_proved(items, index, control_flow, tail, options, local_reasons);
    if (!continuation.proved || !relocation_safe || !local_reasons.empty()) {
      append_reasons(candidate_reasons, local_reasons);
      continue;
    }

    const TerminalContinuationLivenessProof liveness =
        build_continuation_proof(items, tail, continuation.terminal_register, continuation);
    const TerminalReportTailRelocationProof relocation =
        build_relocation_proof(index, tail, relocation_safe);
    for (const auto& [selector, raw_value] : selectors) {
      const bool unwritten = selector_is_unwritten(items, selector, control_flow);
      const RawZeroReturnSelectorProof raw = build_raw_selector(selector, raw_value, unwritten);
      const TerminalReportTailVerification verification = verify_terminal_report_tail(
          items, raw, liveness, relocation,
          TerminalReportTailOptions{.address_space_model = options.address_space_model});
      if (!verification.proved) {
        append_reasons(candidate_reasons, verification.reasons);
        continue;
      }
      selected_tail = tail;
      plan.raw_selector = raw;
      plan.continuation = liveness;
      plan.relocation = relocation;
      plan.terminal_verification = verification;
      break;
    }
    if (selected_tail.has_value())
      break;
  }
  if (!selected_tail.has_value()) {
    append_reasons(plan.reasons, candidate_reasons);
    if (plan.reasons.empty())
      add_reason(plan.reasons, "no terminal tail satisfies the derived proof obligations");
    return plan;
  }

  const TerminalReportTailResult terminal = rewrite_terminal_report_tail(
      items, plan.raw_selector, plan.continuation, plan.relocation,
      TerminalReportTailOptions{.address_space_model = options.address_space_model});
  if (terminal.applied != 1 || !terminal.verification.final_artifact_proved) {
    append_reasons(plan.reasons, terminal.verification.reasons);
    add_reason(plan.reasons, "derived terminal proof failed its transactional final recheck");
    return plan;
  }
  const std::size_t rewritten_condition =
      *reindexed_item_after_terminal(selected_tail->condition, *selected_tail);
  const std::size_t rewritten_zero_return =
      *reindexed_item_after_terminal(selected_tail->zero_return, *selected_tail);
  const ItemRelocation terminal_relocation = [tail = *selected_tail](std::size_t old_item) {
    return reindexed_item_after_terminal(old_item, tail);
  };
  const ReboundArtifact after_terminal = rebind_control_flow_after_relocation(
      items, terminal.items, control_flow, terminal_relocation,
      {selected_tail->operand, selected_tail->stop},
      {selected_tail->condition, selected_tail->dot},
      {{.source_item = rewritten_condition, .target_items = {rewritten_zero_return}}}, options);
  if (!after_terminal.proved) {
    append_reasons(plan.reasons, after_terminal.reasons);
    add_reason(plan.reasons, "rewritten terminal artifact failed the repeated total CFG/map gate");
    return plan;
  }
  const ArtifactIndex terminal_index = index_artifact(after_terminal.items);

  plan.terminal_proved = true;
  plan.terminal_verification = terminal.verification;
  plan.terminal_output_cells = terminal_index.cells;
  plan.output_cells = plan.terminal_output_cells;
  plan.removed_cells = 2;
  plan.terminal_control_flow = after_terminal.control_flow;
  plan.final_control_flow = after_terminal.control_flow;

  const CyclicEndReturnOptions cyclic_options{
      .address_space_model = options.address_space_model,
      .proved_indirect_flow_targets = address_flow_targets(after_terminal.control_flow),
      .external_entry_addresses =
          fixed_protocol_addresses(after_terminal.control_flow.external_entries),
  };
  const CyclicEndReturnResult cyclic =
      optimize_cyclic_end_return(after_terminal.items, cyclic_options);
  plan.cyclic_verification = cyclic.proof;
  if (cyclic.applied == 1 && cyclic.proof.final_artifact_proved) {
    const ItemRelocation cyclic_relocation =
        [input_count = after_terminal.items.size(), proof = cyclic.proof](std::size_t old_item) {
          return reindexed_item_after_cyclic(old_item, input_count, proof);
        };
    const std::optional<std::size_t> changed_body =
        previous_cell_item(after_terminal.items, cyclic.proof.original_explicit_return_item_index);
    const ReboundArtifact final_artifact = rebind_control_flow_after_relocation(
        after_terminal.items, cyclic.items, after_terminal.control_flow, cyclic_relocation,
        {cyclic.proof.original_explicit_return_item_index},
        changed_body.has_value() ? std::set<std::size_t>{*changed_body}
                                 : std::set<std::size_t>{},
        {}, options);
    if (final_artifact.proved) {
      const ArtifactIndex final_index = index_artifact(final_artifact.items);
      plan.cyclic_proved = true;
      plan.final_control_flow = final_artifact.control_flow;
      plan.output_cells = final_index.cells;
      plan.removed_cells = 3;
    } else {
      append_reasons(plan.cyclic_verification.reasons, final_artifact.reasons);
    }
  }
  plan.final_artifact_proved =
      plan.terminal_proved &&
      (!plan.cyclic_proved || plan.cyclic_verification.final_artifact_proved);
  return plan;
}

TerminalCyclicLayoutResult
optimize_terminal_cyclic_layout(const std::vector<MachineItem>& items,
                                const std::vector<PreloadReport>& preloads,
                                const AuthoritativePostLayoutControlFlow& control_flow,
                                const TerminalCyclicLayoutOptions& options) {
  TerminalCyclicLayoutResult result;
  result.items = items;
  result.plan = verify_terminal_cyclic_layout(items, preloads, control_flow, options);
  if (!result.plan.terminal_proved) {
    if (options.enable_return_alias) {
      if (const std::optional<TerminalCyclicLayoutResult> alias =
              rewrite_terminal_return_alias(items, preloads, control_flow, options,
                                            &result.plan.reasons);
          alias.has_value()) {
        return *alias;
      }
      if (const std::optional<TerminalCyclicLayoutResult> alias =
              rewrite_terminal_semantic_return_alias(items, preloads, control_flow, options,
                                                     &result.plan.reasons);
          alias.has_value()) {
        return *alias;
      }    }
    for (ReboundArtifact& normalized :
         normalize_inverted_terminal_tail_layouts(items, control_flow, options)) {
      TerminalCyclicLayoutResult candidate = optimize_terminal_cyclic_layout(
          normalized.items, preloads, normalized.control_flow, options);
      if (candidate.applied > 0 && candidate.plan.final_artifact_proved)
        return candidate;
      for (auto reason = candidate.plan.reasons.rbegin();
           reason != candidate.plan.reasons.rend(); ++reason) {
        const std::string prefixed = "normalized terminal layout: " + *reason;
        if (std::find(result.plan.reasons.begin(), result.plan.reasons.end(), prefixed) ==
            result.plan.reasons.end()) {
          result.plan.reasons.insert(result.plan.reasons.begin(), prefixed);
        }
      }
    }
    std::vector<std::string> startup_rejections;
    for (ReboundArtifact& startup : normalize_empty_return_startup_layouts(
             items, control_flow, options, &startup_rejections)) {
      TerminalCyclicLayoutResult candidate = optimize_terminal_cyclic_layout(
          startup.items, preloads, startup.control_flow, options);
      if (candidate.applied > 0 && candidate.plan.final_artifact_proved)
        return candidate;
      for (auto reason = candidate.plan.reasons.rbegin();
           reason != candidate.plan.reasons.rend(); ++reason) {
        const std::string prefixed = "empty-return startup layout: " + *reason;
        if (std::find(result.plan.reasons.begin(), result.plan.reasons.end(), prefixed) ==
            result.plan.reasons.end()) {
          result.plan.reasons.insert(result.plan.reasons.begin(), prefixed);
        }
      }
    }
    for (auto reason = startup_rejections.rbegin();
         reason != startup_rejections.rend(); ++reason) {
      const std::string prefixed = "empty-return startup layout: " + *reason;
      if (std::find(result.plan.reasons.begin(), result.plan.reasons.end(), prefixed) ==
          result.plan.reasons.end()) {
        result.plan.reasons.insert(result.plan.reasons.begin(), prefixed);
      }
    }
    return result;
  }

  const TerminalReportTailResult terminal = rewrite_terminal_report_tail(
      items, result.plan.raw_selector, result.plan.continuation, result.plan.relocation,
      TerminalReportTailOptions{.address_space_model = options.address_space_model});
  if (terminal.applied != 1 || !terminal.verification.final_artifact_proved) {
    result.plan.terminal_proved = false;
    result.plan.final_artifact_proved = false;
    append_reasons(result.plan.reasons, terminal.verification.reasons);
    return result;
  }
  const TailCandidate tail{
      .condition = result.plan.relocation.direct_condition_item_index,
      .operand = result.plan.relocation.direct_address_item_index,
      .dot = result.plan.relocation.dot_item_index,
      .stop = result.plan.relocation.stop_item_index,
      .zero_return = result.plan.relocation.zero_return_item_index,
  };
  const std::size_t rewritten_condition = *reindexed_item_after_terminal(tail.condition, tail);
  const std::size_t rewritten_zero_return = *reindexed_item_after_terminal(tail.zero_return, tail);
  const ItemRelocation terminal_relocation = [tail](std::size_t old_item) {
    return reindexed_item_after_terminal(old_item, tail);
  };
  ReboundArtifact after_terminal = rebind_control_flow_after_relocation(
      items, terminal.items, control_flow, terminal_relocation, {tail.operand, tail.stop},
      {tail.condition, tail.dot},
      {{.source_item = rewritten_condition, .target_items = {rewritten_zero_return}}}, options);
  if (!after_terminal.proved) {
    result.plan.terminal_proved = false;
    result.plan.final_artifact_proved = false;
    append_reasons(result.plan.reasons, after_terminal.reasons);
    return result;
  }
  result.items = std::move(after_terminal.items);
  result.plan.terminal_control_flow = after_terminal.control_flow;
  result.plan.final_control_flow = after_terminal.control_flow;
  result.applied = 1;
  result.removed_cells = 2;
  result.optimizations.push_back(passes::AppliedOptimization{
      .name = "terminal-report-tail-derived",
      .detail = "Derived CFG/liveness/relocation certificates and removed two terminal "
                "report cells.",
  });

  if (!result.plan.cyclic_proved)
    return result;
  const CyclicEndReturnOptions cyclic_options{
      .address_space_model = options.address_space_model,
      .proved_indirect_flow_targets = address_flow_targets(result.plan.terminal_control_flow),
      .external_entry_addresses =
          fixed_protocol_addresses(result.plan.terminal_control_flow.external_entries),
  };
  const CyclicEndReturnResult cyclic = rewrite_cyclic_end_return(
      result.items, result.plan.cyclic_verification.helper_label, cyclic_options);
  if (cyclic.applied != 1 || !cyclic.proof.final_artifact_proved) {
    result.plan.cyclic_proved = false;
    result.plan.cyclic_verification = cyclic.proof;
    add_reason(result.plan.cyclic_verification.reasons,
               "cyclic rewrite did not reproduce the verified candidate");
    result.plan.final_control_flow = result.plan.terminal_control_flow;
    result.plan.output_cells = index_artifact(result.items).cells;
    result.plan.removed_cells = 2;
    result.plan.final_artifact_proved = true;
    return result;
  }
  const ItemRelocation cyclic_relocation =
      [input_count = result.items.size(), proof = cyclic.proof](std::size_t old_item) {
        return reindexed_item_after_cyclic(old_item, input_count, proof);
      };
  const std::optional<std::size_t> changed_body =
      previous_cell_item(result.items, cyclic.proof.original_explicit_return_item_index);
  ReboundArtifact final_artifact = rebind_control_flow_after_relocation(
      result.items, cyclic.items, result.plan.terminal_control_flow, cyclic_relocation,
      {cyclic.proof.original_explicit_return_item_index},
      changed_body.has_value() ? std::set<std::size_t>{*changed_body}
                               : std::set<std::size_t>{},
      {}, options);
  if (!final_artifact.proved) {
    append_reasons(result.plan.cyclic_verification.reasons, final_artifact.reasons);
    result.plan.cyclic_proved = false;
    result.plan.final_control_flow = result.plan.terminal_control_flow;
    result.plan.output_cells = index_artifact(result.items).cells;
    result.plan.removed_cells = 2;
    result.plan.final_artifact_proved = true;
    return result;
  }
  result.items = std::move(final_artifact.items);
  result.applied = 2;
  result.removed_cells = 3;
  result.optimizations.insert(result.optimizations.end(), cyclic.optimizations.begin(),
                              cyclic.optimizations.end());
  result.plan.cyclic_verification = cyclic.proof;
  result.plan.final_control_flow = final_artifact.control_flow;
  result.plan.output_cells = index_artifact(result.items).cells;
  result.plan.final_artifact_proved = true;
  return result;
}

} // namespace mkpro::core
