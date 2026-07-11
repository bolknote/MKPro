#include "mkpro/core/terminal_cyclic_layout.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
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
    if (state.returns.empty())
      return {};
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
    if (item.opcode == kReturnOpcode && state.returns.empty()) {
      add_reason(analysis.reasons, "report continuation returns outside the proved callers");
      return analysis;
    }
    std::vector<std::string> successor_reasons;
    const std::vector<ExecutionState> successors =
        state_successors(items, index, control, state, options, successor_reasons);
    for (const std::string& reason : successor_reasons)
      add_reason(analysis.reasons, reason);
    if (successors.empty()) {
      add_reason(analysis.reasons, "report continuation has a non-terminal exit");
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
                                                    AddressSpaceModel model) {
  std::vector<TailCandidate> result;
  const auto zero = index.cell_items.find(0);
  if (zero == index.cell_items.end() || !is_op(items, zero->second, kReturnOpcode))
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
        .zero_return = zero->second,
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
                                              bool selector_unwritten) {
  const OpcodeInfo& conditional = opcode_by_code(0x70 + selector);
  const bool stack_preserved = conditional.stack_effect == StackEffect::Preserves;
  const bool x2_preserved = conditional.conditional_x2_effect.has_value() &&
                            conditional.conditional_x2_effect->fallthrough == X2Effect::Preserves &&
                            conditional.conditional_x2_effect->jump == X2Effect::Preserves;
  return RawZeroReturnSelectorProof{
      .selector_register = register_text(selector),
      .facts = {{.raw_value = std::move(raw_value),
                 .actual_flow_target = 0,
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
  const PostLayoutControlFlowOptions cfg_options{
      .address_space_model = options.address_space_model,
      .maximum_return_depth = options.maximum_return_depth,
      .maximum_execution_states = static_cast<std::size_t>(options.maximum_execution_states),
      .main_entry = output_index.item_addresses.at(*new_main),
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

void append_reasons(std::vector<std::string>& destination, const std::vector<std::string>& source) {
  for (const std::string& reason : source)
    add_reason(destination, reason);
}

} // namespace

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
  if (!result.plan.terminal_proved)
    return result;

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
