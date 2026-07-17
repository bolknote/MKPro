#include "mkpro/core/post_layout_control_flow.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/register_allocator.hpp"

#include <algorithm>
#include <compare>
#include <deque>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace mkpro::core {

namespace {

constexpr int kStopOpcode = 0x50;
constexpr int kJumpOpcode = 0x51;
constexpr int kReturnOpcode = 0x52;
constexpr int kCallOpcode = 0x53;

struct ArtifactIndex {
  std::vector<int> item_addresses;
  std::map<int, std::size_t> cell_items;
  std::map<std::string, int> label_addresses;
  std::map<int, std::vector<std::string>> address_labels;
  std::set<std::string> duplicate_labels;
  int cells = 0;
};

struct ExecutionState {
  int pc = -1;
  std::vector<int> returns;
  auto operator<=>(const ExecutionState&) const = default;
};

struct ManualProtocol {
  std::optional<std::size_t> prompt_item;
  std::map<int, std::size_t> phase_items;
};

void add_reason(AuthoritativePostLayoutControlFlow& result, std::string reason) {
  if (std::find(result.reasons.begin(), result.reasons.end(), reason) == result.reasons.end())
    result.reasons.push_back(std::move(reason));
}

ArtifactIndex index_artifact(const std::vector<MachineItem>& items) {
  ArtifactIndex index;
  index.item_addresses.resize(items.size(), 0);
  int address = 0;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    index.item_addresses.at(item_index) = address;
    if (item.kind == MachineItemKind::Label) {
      if (!index.label_addresses.emplace(item.name, address).second)
        index.duplicate_labels.insert(item.name);
      index.address_labels[address].push_back(item.name);
      continue;
    }
    index.cell_items.emplace(address, item_index);
    ++address;
  }
  for (auto& [unused_address, labels] : index.address_labels) {
    (void)unused_address;
    std::sort(labels.begin(), labels.end());
    labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
  }
  index.cells = address;
  return index;
}

bool is_indirect_flow_opcode(int opcode) {
  const int family = opcode & 0xf0;
  return family == 0x70 || family == 0x80 || family == 0x90 || family == 0xa0 || family == 0xc0 ||
         family == 0xe0;
}

bool is_indirect_call_opcode(int opcode) {
  return (opcode & 0xf0) == 0xa0;
}

bool is_indirect_conditional_opcode(int opcode) {
  const int family = opcode & 0xf0;
  return family == 0x70 || family == 0x90 || family == 0xc0 || family == 0xe0;
}

bool is_indirect_memory_opcode(int opcode) {
  const int family = opcode & 0xf0;
  return family == 0xb0 || family == 0xd0;
}

bool is_direct_conditional_opcode(int opcode) {
  return opcode == 0x57 || opcode == 0x58 || opcode == 0x59 || opcode == 0x5a || opcode == 0x5b ||
         opcode == 0x5c || opcode == 0x5d || opcode == 0x5e;
}

bool is_direct_single_step_store(int opcode) {
  return opcode >= 0x40 && opcode <= 0x4e;
}

std::optional<std::size_t> next_cell_item(const std::vector<MachineItem>& items,
                                          std::size_t after) {
  for (++after; after < items.size(); ++after) {
    if (items.at(after).kind != MachineItemKind::Label)
      return after;
  }
  return std::nullopt;
}

std::optional<PostLayoutCommandIdentity> identity_at_address(const std::vector<MachineItem>& items,
                                                             const ArtifactIndex& index,
                                                             int address) {
  const auto cell = index.cell_items.find(address);
  if (cell == index.cell_items.end() || items.at(cell->second).kind != MachineItemKind::Op)
    return std::nullopt;
  const auto aliases = index.address_labels.find(address);
  return PostLayoutCommandIdentity{
      .item_index = cell->second,
      .address = address,
      .labels =
          aliases == index.address_labels.end() ? std::vector<std::string>{} : aliases->second,
  };
}

std::optional<int> sequential_successor(const ArtifactIndex& index, int address,
                                        AddressSpaceModel address_space_model) {
  const int next = address + 1;
  if (next < index.cells)
    return next;
  // MK-61 program memory is cyclic only at the real end of the selected
  // address space.  A shorter artifact falling off its last emitted cell is
  // still malformed and must not acquire an invented continuation.
  if (index.cells == official_program_step_limit(address_space_model) &&
      address == index.cells - 1) {
    return 0;
  }
  return std::nullopt;
}

std::optional<int> direct_target_address(const MachineItem& operand, const ArtifactIndex& index,
                                         AddressSpaceModel address_space_model,
                                         AuthoritativePostLayoutControlFlow& result) {
  if (operand.kind != MachineItemKind::Address)
    return std::nullopt;
  if (operand.formal_opcode.has_value()) {
    try {
      const FormalAddressInfo formal =
          formal_address_info(*operand.formal_opcode, address_space_model);
      if (formal.kind == FormalAddressKind::SuperDark || formal.one_command ||
          formal.extra.has_value()) {
        add_reason(result, "super-dark direct operand lacks an exact CFG model");
        return std::nullopt;
      }
      return formal.actual;
    } catch (const std::exception&) {
      add_reason(result, "formal direct operand is invalid for this address space");
      return std::nullopt;
    }
  }
  if (const auto* address = std::get_if<int>(&operand.target))
    return *address;
  const auto* label = std::get_if<std::string>(&operand.target);
  if (label == nullptr)
    return std::nullopt;
  const auto found = index.label_addresses.find(*label);
  return found == index.label_addresses.end() ? std::nullopt : std::optional<int>(found->second);
}

std::optional<PostLayoutCommandIdentity>
resolve_indirect_target(const std::vector<MachineItem>& items, const ArtifactIndex& index,
                        const IrTarget& target) {
  int address = -1;
  if (const auto* numeric = std::get_if<int>(&target)) {
    address = *numeric;
  } else {
    const std::string& label = std::get<std::string>(target);
    const auto found = index.label_addresses.find(label);
    if (found == index.label_addresses.end())
      return std::nullopt;
    address = found->second;
  }
  return identity_at_address(items, index, address);
}

bool takes_address(const MachineItem& item) {
  return item.kind == MachineItemKind::Op && item.opcode >= 0 && item.opcode <= 0xff &&
         opcode_by_code(item.opcode).takes_address;
}

void validate_artifact_and_typed_targets(const std::vector<MachineItem>& items,
                                         const ArtifactIndex& index,
                                         const PostLayoutControlFlowOptions& options,
                                         AuthoritativePostLayoutControlFlow& result) {
  if (items.empty() || index.cells == 0)
    add_reason(result, "artifact contains no command cells");
  if (!index.duplicate_labels.empty())
    add_reason(result, "artifact contains duplicate labels");

  std::set<std::size_t> consumed_operands;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind != MachineItemKind::Op) {
      if (item.manual_interaction.has_value())
        add_reason(result, "manual interaction anchor is attached to a non-command item");
      if (item.indirect_flow_targets.has_value() || item.indirect_memory_targets.has_value())
        add_reason(result, "indirect target fact is attached to a non-command item");
      continue;
    }
    if (item.opcode < 0 || item.opcode > 0xff) {
      add_reason(result, "artifact contains an invalid opcode");
      continue;
    }
    if (item.opcode == kStopOpcode) {
      if (item.stop_disposition == StopDisposition::Unknown)
        add_reason(result, "STOP command has unknown disposition");
    } else if (item.stop_disposition != StopDisposition::Unknown) {
      add_reason(result, "STOP disposition is attached to a non-STOP command");
    }

    if (takes_address(item)) {
      const std::optional<std::size_t> operand = next_cell_item(items, item_index);
      if (!operand.has_value() || items.at(*operand).kind != MachineItemKind::Address) {
        add_reason(result, "address-taking command has no adjacent operand");
      } else {
        consumed_operands.insert(*operand);
        const std::optional<int> target =
            direct_target_address(items.at(*operand), index, options.address_space_model, result);
        if (!target.has_value()) {
          add_reason(result, "direct flow has an unresolved target");
        } else if (!identity_at_address(items, index, *target).has_value()) {
          add_reason(result, "direct flow targets a non-executable command cell");
        }
      }
    }

    const bool indirect_flow = is_indirect_flow_opcode(item.opcode);
    const bool indirect_memory = is_indirect_memory_opcode(item.opcode);
    if (indirect_flow) {
      if (!item.indirect_flow_targets.has_value() || item.indirect_flow_targets->empty()) {
        add_reason(result,
                   "complete indirect-flow fact is missing item " + std::to_string(item_index));
      } else {
        std::set<std::size_t> seen;
        std::vector<PostLayoutCommandIdentity> targets;
        for (const IrTarget& target : *item.indirect_flow_targets) {
          const std::optional<PostLayoutCommandIdentity> resolved =
              resolve_indirect_target(items, index, target);
          if (!resolved.has_value()) {
            add_reason(result, "indirect flow targets an unresolved or non-executable cell");
            continue;
          }
          if (!seen.insert(resolved->item_index).second) {
            add_reason(result, "indirect-flow fact contains duplicate command identities");
            continue;
          }
          targets.push_back(*resolved);
        }
        std::sort(
            targets.begin(), targets.end(),
            [](const PostLayoutCommandIdentity& left, const PostLayoutCommandIdentity& right) {
              return std::tie(left.address, left.item_index) <
                     std::tie(right.address, right.item_index);
            });
        result.indirect_flow_targets.emplace(item_index, std::move(targets));
      }
      if (item.indirect_memory_targets.has_value())
        add_reason(result, "indirect-flow command carries an indirect-memory fact");
    } else if (item.indirect_flow_targets.has_value()) {
      add_reason(result, "indirect-flow fact is attached to a non-flow command");
    }

    if (indirect_memory) {
      if (!item.indirect_memory_targets.has_value() || item.indirect_memory_targets->empty()) {
        add_reason(result,
                   "complete indirect-memory fact is missing item " + std::to_string(item_index));
      } else {
        std::set<int> targets;
        for (const int target : *item.indirect_memory_targets) {
          if (target < 0 || target > 0x0e) {
            add_reason(result, "indirect-memory fact names a register outside R0..Re");
          } else if (!targets.insert(target).second) {
            add_reason(result, "indirect-memory fact contains duplicate registers");
          }
        }
        result.indirect_memory_targets.emplace(item_index,
                                               std::vector<int>(targets.begin(), targets.end()));
      }
      if (item.indirect_flow_targets.has_value())
        add_reason(result, "indirect-memory command carries an indirect-flow fact");
    } else if (item.indirect_memory_targets.has_value()) {
      add_reason(result, "indirect-memory fact is attached to a non-memory command");
    }
  }

  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    if (items.at(item_index).kind == MachineItemKind::Address &&
        !consumed_operands.contains(item_index)) {
      add_reason(result, "artifact contains an orphan address operand");
    }
  }
}

std::map<std::size_t, ManualProtocol>
validate_manual_protocols(const std::vector<MachineItem>& items, const ArtifactIndex& index,
                          AuthoritativePostLayoutControlFlow& result) {
  std::map<int, ManualProtocol> by_id;
  std::set<int> duplicate_prompts;
  std::set<std::pair<int, int>> duplicate_phases;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (!item.manual_interaction.has_value())
      continue;
    const ManualInteractionAnchor& anchor = *item.manual_interaction;
    if (item.kind != MachineItemKind::Op || anchor.protocol_id < 0) {
      add_reason(result, "manual protocol anchor has an invalid command or protocol id");
      continue;
    }
    ManualProtocol& protocol = by_id[anchor.protocol_id];
    if (anchor.kind == ManualInteractionAnchorKind::PromptStop) {
      if (anchor.phase != -1)
        add_reason(result, "manual prompt anchor has a phase other than -1");
      if (protocol.prompt_item.has_value()) {
        duplicate_prompts.insert(anchor.protocol_id);
      } else {
        protocol.prompt_item = item_index;
      }
      if (item.opcode != kStopOpcode || item.stop_disposition != StopDisposition::Resumable) {
        add_reason(result, "manual prompt is not a typed resumable STOP");
      }
      continue;
    }
    if (anchor.phase < 0) {
      add_reason(result, "manual input phase has a negative phase index");
      continue;
    }
    if (!protocol.phase_items.emplace(anchor.phase, item_index).second)
      duplicate_phases.emplace(anchor.protocol_id, anchor.phase);
    if (anchor.kind == ManualInteractionAnchorKind::SingleStepCommand &&
        !is_direct_single_step_store(item.opcode)) {
      add_reason(result, "manual single-step phase is not one direct store command");
    }
  }

  if (!duplicate_prompts.empty())
    add_reason(result, "manual protocol has duplicate prompt anchors");
  if (!duplicate_phases.empty())
    add_reason(result, "manual protocol has duplicate phase anchors");

  std::map<std::size_t, ManualProtocol> by_prompt;
  for (const auto& [protocol_id, protocol] : by_id) {
    (void)protocol_id;
    const auto prompt_anchor = protocol.prompt_item.has_value()
                                   ? items.at(*protocol.prompt_item).manual_interaction
                                   : std::nullopt;
    if (!prompt_anchor.has_value() ||
        prompt_anchor->kind != ManualInteractionAnchorKind::PromptStop) {
      add_reason(result, "manual protocol has no prompt anchor");
      continue;
    }
    if (protocol.phase_items.empty()) {
      add_reason(result, "manual protocol has no input phases");
      continue;
    }
    for (int phase = 0; phase < static_cast<int>(protocol.phase_items.size()); ++phase) {
      const auto found = protocol.phase_items.find(phase);
      if (found == protocol.phase_items.end()) {
        add_reason(result, "manual protocol phase indexes are not contiguous");
        break;
      }
      const ManualInteractionAnchor& anchor = *items.at(found->second).manual_interaction;
      const bool last = phase + 1 == static_cast<int>(protocol.phase_items.size());
      if (last && anchor.kind != ManualInteractionAnchorKind::ContinuousResume)
        add_reason(result, "manual protocol does not end in a continuous-resume phase");
      if (!last && anchor.kind != ManualInteractionAnchorKind::SingleStepCommand)
        add_reason(result, "manual protocol has a non-single-step intermediate phase");
    }

    int expected_address = index.item_addresses.at(*protocol.prompt_item) + 1;
    for (const auto& [phase, item_index] : protocol.phase_items) {
      (void)phase;
      if (index.item_addresses.at(item_index) != expected_address)
        add_reason(result, "manual protocol phases are not consecutive command cells");
      ++expected_address;
    }
    by_prompt.emplace(*protocol.prompt_item, protocol);
  }
  return by_prompt;
}

std::vector<int> stack_addresses(const std::vector<PostLayoutCommandIdentity>& return_stack) {
  std::vector<int> result;
  result.reserve(return_stack.size());
  for (const PostLayoutCommandIdentity& identity : return_stack)
    result.push_back(identity.address);
  return result;
}

bool add_external_entry(AuthoritativePostLayoutControlFlow& result,
                        const std::vector<MachineItem>& items, const ArtifactIndex& index, int pc,
                        const std::vector<int>& returns, ExternalEntryKind kind,
                        std::optional<ManualInteractionAnchor> manual = std::nullopt) {
  const std::optional<PostLayoutCommandIdentity> entry = identity_at_address(items, index, pc);
  if (!entry.has_value()) {
    add_reason(result, "external entry is not an executable command cell");
    return false;
  }
  std::vector<PostLayoutCommandIdentity> return_stack;
  return_stack.reserve(returns.size());
  for (const int address : returns) {
    const std::optional<PostLayoutCommandIdentity> identity =
        identity_at_address(items, index, address);
    if (!identity.has_value()) {
      add_reason(result, "external return-stack slot is not an executable command cell");
      return false;
    }
    return_stack.push_back(*identity);
  }
  PostLayoutExternalEntryState candidate{
      .entry = *entry,
      .return_stack = std::move(return_stack),
      .kind = kind,
      .manual_interaction = std::move(manual),
  };
  if (std::find(result.external_entries.begin(), result.external_entries.end(), candidate) ==
      result.external_entries.end()) {
    result.external_entries.push_back(std::move(candidate));
  }
  return true;
}

void explore_entries_and_return_stacks(const std::vector<MachineItem>& items,
                                       const ArtifactIndex& index,
                                       const std::map<std::size_t, ManualProtocol>& protocols,
                                       const PostLayoutControlFlowOptions& options,
                                       AuthoritativePostLayoutControlFlow& result) {
  const std::optional<PostLayoutCommandIdentity> main =
      resolve_indirect_target(items, index, options.main_entry);
  if (!main.has_value()) {
    add_reason(result, "typed main entry is unresolved or not an executable command cell");
    return;
  }
  add_external_entry(result, items, index, main->address, {}, ExternalEntryKind::Main);

  std::deque<std::size_t> pending;
  std::map<ExecutionState, std::size_t> state_ids;
  std::vector<ExecutionState> states;
  auto record_state = [&](const ExecutionState& state) -> std::optional<std::size_t> {
    if (const auto found = state_ids.find(state); found != state_ids.end())
      return found->second;
    if (states.size() >= options.maximum_execution_states) {
      add_reason(result, "control-flow exploration exceeds the execution-state cap");
      return std::nullopt;
    }
    const std::optional<PostLayoutCommandIdentity> identity =
        identity_at_address(items, index, state.pc);
    if (!identity.has_value()) {
      add_reason(result, "control flow has a missing executable successor");
      return std::nullopt;
    }
    const std::size_t id = states.size();
    state_ids.emplace(state, id);
    states.push_back(state);
    result.execution_states.push_back(PostLayoutExecutionState{
        .item_index = identity->item_index,
        .address = state.pc,
        .return_stack = state.returns,
    });
    result.execution_successors.emplace_back();
    pending.push_back(id);
    return id;
  };
  (void)record_state(ExecutionState{.pc = main->address});
  std::size_t explored = 0;
  while (!pending.empty() && result.reasons.empty()) {
    const std::size_t state_id = pending.front();
    pending.pop_front();
    const ExecutionState state = states.at(state_id);
    ++explored;
    result.maximum_observed_return_depth =
        std::max(result.maximum_observed_return_depth, static_cast<int>(state.returns.size()));

    const auto cell = index.cell_items.find(state.pc);
    if (cell == index.cell_items.end() || items.at(cell->second).kind != MachineItemKind::Op) {
      add_reason(result, "control flow reaches a non-executable cell");
      break;
    }
    const std::size_t item_index = cell->second;
    const MachineItem& item = items.at(item_index);
    const int opcode = item.opcode;

    const auto enqueue = [&](int pc, const std::vector<int>& returns) {
      const std::optional<std::size_t> successor =
          record_state(ExecutionState{.pc = pc, .returns = returns});
      if (!successor.has_value())
        return;
      std::vector<std::size_t>& edges = result.execution_successors.at(state_id);
      if (std::find(edges.begin(), edges.end(), *successor) == edges.end())
        edges.push_back(*successor);
    };

    if (opcode == kStopOpcode) {
      if (item.stop_disposition == StopDisposition::Terminal)
        continue;
      if (item.stop_disposition != StopDisposition::Resumable) {
        add_reason(result, "reachable STOP command has unknown disposition");
        continue;
      }
      const auto protocol = protocols.find(item_index);
      if (protocol != protocols.end()) {
        for (const auto& [phase, phase_item] : protocol->second.phase_items) {
          const ManualInteractionAnchor anchor = *items.at(phase_item).manual_interaction;
          add_external_entry(result, items, index, index.item_addresses.at(phase_item),
                             state.returns,
                             anchor.kind == ManualInteractionAnchorKind::SingleStepCommand
                                 ? ExternalEntryKind::ManualSingleStep
                                 : ExternalEntryKind::ManualContinuous,
                             anchor);
        }
        const auto first_phase = protocol->second.phase_items.find(0);
        if (first_phase == protocol->second.phase_items.end()) {
          add_reason(result, "manual protocol has no first input phase");
        } else {
          // The operator's pauses do not mutate calculator registers. Follow
          // every single-step input command in order so its defs participate
          // in exact liveness, then continue naturally from the final phase.
          enqueue(index.item_addresses.at(first_phase->second), state.returns);
        }
      } else {
        const std::optional<int> resume_pc =
            sequential_successor(index, state.pc, options.address_space_model);
        if (!resume_pc.has_value()) {
          add_reason(result, "resumable STOP has no executable continuation");
        } else if (add_external_entry(result, items, index, *resume_pc, state.returns,
                               ExternalEntryKind::ResumableStop)) {
          enqueue(*resume_pc, state.returns);
        }
      }
      continue;
    }

    if (opcode == kReturnOpcode) {
      if (state.returns.empty()) {
        if (!result.empty_return_target.has_value()) {
          add_reason(result, "reachable В/О has an empty return stack");
        } else {
          enqueue(result.empty_return_target->address, state.returns);
        }
        continue;
      }
      std::vector<int> returns = state.returns;
      const int return_pc = returns.back();
      returns.pop_back();
      enqueue(return_pc, returns);
      continue;
    }

    if (takes_address(item)) {
      const std::optional<std::size_t> operand = next_cell_item(items, item_index);
      if (!operand.has_value() || items.at(*operand).kind != MachineItemKind::Address) {
        add_reason(result, "reachable address-taking command has no operand");
        continue;
      }
      const std::optional<int> target =
          direct_target_address(items.at(*operand), index, options.address_space_model, result);
      if (!target.has_value())
        continue;
      const std::optional<int> fallthrough = sequential_successor(
          index, index.item_addresses.at(*operand), options.address_space_model);
      if (opcode == kJumpOpcode) {
        enqueue(*target, state.returns);
      } else if (opcode == kCallOpcode) {
        if (static_cast<int>(state.returns.size()) >= options.maximum_return_depth) {
          add_reason(result, "control flow exceeds the configured return-stack depth");
          continue;
        }
        if (!fallthrough.has_value() ||
            !identity_at_address(items, index, *fallthrough).has_value()) {
          add_reason(result, "direct call has no executable continuation");
          continue;
        }
        std::vector<int> returns = state.returns;
        returns.push_back(*fallthrough);
        enqueue(*target, returns);
      } else if (is_direct_conditional_opcode(opcode)) {
        if (!fallthrough.has_value()) {
          add_reason(result, "direct conditional has no executable fallthrough");
          continue;
        }
        enqueue(*target, state.returns);
        enqueue(*fallthrough, state.returns);
      } else {
        add_reason(result, "unsupported address-taking command in exact CFG");
      }
      continue;
    }

    if (is_indirect_flow_opcode(opcode)) {
      const auto targets = result.indirect_flow_targets.find(item_index);
      if (targets == result.indirect_flow_targets.end() || targets->second.empty()) {
        add_reason(result, "reachable indirect flow lacks a complete typed target set");
        continue;
      }
      std::vector<int> returns = state.returns;
      if (is_indirect_call_opcode(opcode)) {
        if (static_cast<int>(returns.size()) >= options.maximum_return_depth) {
          add_reason(result, "control flow exceeds the configured return-stack depth");
          continue;
        }
        const std::optional<int> continuation =
            sequential_successor(index, state.pc, options.address_space_model);
        if (!continuation.has_value() ||
            !identity_at_address(items, index, *continuation).has_value()) {
          add_reason(result, "indirect call has no executable continuation");
          continue;
        }
        returns.push_back(*continuation);
      }
      for (const PostLayoutCommandIdentity& target : targets->second)
        enqueue(target.address, returns);
      if (is_indirect_conditional_opcode(opcode)) {
        const std::optional<int> fallthrough =
            sequential_successor(index, state.pc, options.address_space_model);
        if (!fallthrough.has_value()) {
          add_reason(result, "indirect conditional has no executable fallthrough");
        } else {
          enqueue(*fallthrough, state.returns);
        }
      }
      continue;
    }

    const std::optional<int> successor =
        sequential_successor(index, state.pc, options.address_space_model);
    if (!successor.has_value()) {
      add_reason(result, "control flow has a missing executable successor");
    } else {
      enqueue(*successor, state.returns);
    }
  }
  result.explored_states = explored;

  std::sort(
      result.external_entries.begin(), result.external_entries.end(),
      [](const PostLayoutExternalEntryState& left, const PostLayoutExternalEntryState& right) {
        const std::vector<int> left_stack = stack_addresses(left.return_stack);
        const std::vector<int> right_stack = stack_addresses(right.return_stack);
        const int left_protocol =
            left.manual_interaction.has_value() ? left.manual_interaction->protocol_id : -1;
        const int right_protocol =
            right.manual_interaction.has_value() ? right.manual_interaction->protocol_id : -1;
        const int left_phase =
            left.manual_interaction.has_value() ? left.manual_interaction->phase : -1;
        const int right_phase =
            right.manual_interaction.has_value() ? right.manual_interaction->phase : -1;
        return std::tie(left.entry.address, left_stack, left.kind, left_protocol, left_phase) <
               std::tie(right.entry.address, right_stack, right.kind, right_protocol, right_phase);
      });
}

} // namespace

AuthoritativePostLayoutControlFlow
build_post_layout_control_flow(const std::vector<MachineItem>& items,
                               const PostLayoutControlFlowOptions& options) {
  AuthoritativePostLayoutControlFlow result;
  if (options.maximum_return_depth < 0 || options.maximum_return_depth > 5) {
    add_reason(result, "maximum return-stack depth must be between zero and five");
    return result;
  }
  if (options.maximum_execution_states == 0U) {
    add_reason(result, "execution-state cap must be positive");
    return result;
  }

  const ArtifactIndex index = index_artifact(items);
  validate_artifact_and_typed_targets(items, index, options, result);
  if (options.empty_return_target.has_value()) {
    const std::optional<PostLayoutCommandIdentity> target =
        resolve_indirect_target(items, index, *options.empty_return_target);
    if (!target.has_value()) {
      add_reason(result, "typed empty-return target is unresolved or not executable");
    } else {
      result.empty_return_target = *target;
    }
  }
  const std::map<std::size_t, ManualProtocol> protocols =
      validate_manual_protocols(items, index, result);
  if (!result.reasons.empty())
    return result;

  explore_entries_and_return_stacks(items, index, protocols, options, result);
  result.proved = result.reasons.empty();
  return result;
}

namespace {

enum class RegisterWriteEffect {
  None,
  Maybe,
  Definite,
};

struct RegisterAccessEffect {
  bool reads = false;
  RegisterWriteEffect writes = RegisterWriteEffect::None;
};

std::optional<std::string> stable_indirect_flow_register(const MachineItem& item) {
  if (item.kind != MachineItemKind::Op || !is_indirect_flow_opcode(item.opcode))
    return std::nullopt;
  const int index = item.opcode & 0x0f;
  if (index < 0 || index > 14)
    return std::nullopt;
  const std::string register_name = register_name_for_index(index);
  return is_stable_indirect_selector(register_name) ? std::optional<std::string>(register_name)
                                                    : std::nullopt;
}

RegisterAccessEffect register_access_effect(const MachineItem& item,
                                            const std::string& register_name) {
  RegisterAccessEffect result;
  if (item.kind != MachineItemKind::Op)
    return result;

  const int register_number = register_index(register_name);
  if (item.opcode >= 0x40 && item.opcode <= 0x4e && item.opcode - 0x40 == register_number) {
    result.writes = RegisterWriteEffect::Definite;
    return result;
  }
  if (item.opcode >= 0x60 && item.opcode <= 0x6e && item.opcode - 0x60 == register_number) {
    result.reads = true;
    return result;
  }

  const int family = item.opcode & 0xf0;
  const int selector = item.opcode & 0x0f;
  if ((is_indirect_flow_opcode(item.opcode) || is_indirect_memory_opcode(item.opcode)) &&
      selector == register_number) {
    result.reads = true;
  }

  if (family != 0xb0 && family != 0xd0)
    return result;

  std::set<int> targets;
  if (item.indirect_memory_targets.has_value()) {
    targets.insert(item.indirect_memory_targets->begin(), item.indirect_memory_targets->end());
  }
  const bool unknown_targets = targets.empty();
  const bool may_touch = unknown_targets || targets.contains(register_number);
  if (!may_touch)
    return result;
  if (family == 0xd0) {
    result.reads = true;
  } else if (!unknown_targets && targets.size() == 1U) {
    result.writes = RegisterWriteEffect::Definite;
  } else {
    result.writes = RegisterWriteEffect::Maybe;
  }
  return result;
}

void add_proof_reason(PostLayoutBorrowedSelectorProof& proof, std::string reason) {
  if (std::find(proof.reasons.begin(), proof.reasons.end(), reason) == proof.reasons.end())
    proof.reasons.push_back(std::move(reason));
}

void prove_one_borrowed_register(const std::vector<MachineItem>& items,
                                 const AuthoritativePostLayoutControlFlow& control,
                                 const std::string& register_name,
                                 const std::set<std::size_t>& selector_items,
                                 PostLayoutBorrowedSelectorProof& proof) {
  constexpr unsigned kEntryValue = 1U;
  constexpr unsigned kOverwrittenValue = 2U;

  if (!is_stable_indirect_selector(register_name)) {
    add_proof_reason(proof, "borrowed selector R" + register_name + " is not stable");
    return;
  }
  if (selector_items.empty()) {
    add_proof_reason(proof, "borrowed selector R" + register_name + " has no marked uses");
    return;
  }

  std::vector<unsigned> incoming(control.execution_states.size(), 0U);
  std::vector<bool> queued(control.execution_states.size(), false);
  std::deque<std::size_t> pending;
  incoming.front() = kEntryValue;
  pending.push_back(0U);
  queued.front() = true;
  std::set<std::size_t> reached_selector_items;
  std::set<std::size_t> reached_selector_states;

  while (!pending.empty()) {
    const std::size_t state_index = pending.front();
    pending.pop_front();
    queued.at(state_index) = false;
    const PostLayoutExecutionState& state = control.execution_states.at(state_index);
    if (state.item_index >= items.size()) {
      add_proof_reason(proof, "borrowed-selector graph references a missing machine item");
      continue;
    }

    const MachineItem& item = items.at(state.item_index);
    const unsigned state_values = incoming.at(state_index);
    const RegisterAccessEffect access = register_access_effect(item, register_name);
    const bool selector_use = selector_items.contains(state.item_index);
    if (selector_use) {
      reached_selector_items.insert(state.item_index);
      reached_selector_states.insert(state_index);
      const std::optional<std::string> actual_register = stable_indirect_flow_register(item);
      if (!actual_register.has_value() || *actual_register != register_name) {
        add_proof_reason(proof, "marked borrowed-selector item does not use R" + register_name);
      }
      if ((state_values & kEntryValue) == 0U || (state_values & kOverwrittenValue) != 0U) {
        add_proof_reason(proof, "borrowed selector R" + register_name +
                                    " is reachable after its entry value was overwritten");
      }
    } else if ((state_values & kEntryValue) != 0U && access.reads) {
      add_proof_reason(proof, "entry value of borrowed selector R" + register_name +
                                  " is read by ordinary code before a definite write");
    }

    unsigned outgoing = state_values;
    if (access.writes == RegisterWriteEffect::Definite) {
      outgoing = kOverwrittenValue;
    } else if (access.writes == RegisterWriteEffect::Maybe) {
      outgoing |= kOverwrittenValue;
    }
    for (const std::size_t successor : control.execution_successors.at(state_index)) {
      if (successor >= incoming.size()) {
        add_proof_reason(proof, "borrowed-selector graph has an invalid successor");
        continue;
      }
      const unsigned combined = incoming.at(successor) | outgoing;
      if (combined == incoming.at(successor))
        continue;
      incoming.at(successor) = combined;
      if (!queued.at(successor)) {
        queued.at(successor) = true;
        pending.push_back(successor);
      }
    }
  }

  for (const std::size_t item_index : selector_items) {
    if (!reached_selector_items.contains(item_index)) {
      add_proof_reason(proof,
                       "borrowed selector R" + register_name + " has an unreachable marked use");
    }
  }
  proof.selector_states += reached_selector_states.size();
  proof.entry_value_states +=
      static_cast<std::size_t>(std::count_if(incoming.begin(), incoming.end(), [](unsigned values) {
        return (values & kEntryValue) != 0U;
      }));
}

} // namespace

PostLayoutBorrowedSelectorProof
prove_post_layout_borrowed_entry_selectors(const std::vector<MachineItem>& items,
                                           const PostLayoutControlFlowOptions& options) {
  PostLayoutBorrowedSelectorProof proof;
  std::map<std::string, std::set<std::size_t>> selector_items;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (!item.borrowed_entry_phase_selector) {
      continue;
    }
    const std::optional<std::string> register_name = stable_indirect_flow_register(item);
    if (!register_name.has_value()) {
      add_proof_reason(proof, "borrowed-selector marker is not on stable indirect flow");
      continue;
    }
    selector_items[*register_name].insert(item_index);
  }
  proof.selector_registers = selector_items.size();
  if (selector_items.empty()) {
    add_proof_reason(proof, "final artifact has no borrowed entry-phase selector markers");
    return proof;
  }

  const AuthoritativePostLayoutControlFlow control = build_post_layout_control_flow(items, options);
  if (!control.proved || control.execution_states.empty() ||
      control.execution_states.size() != control.execution_successors.size()) {
    add_proof_reason(proof, control.reasons.empty()
                                ? "borrowed-selector control-flow graph is not proved"
                                : control.reasons.front());
    return proof;
  }
  for (const auto& [register_name, marked_items] : selector_items)
    prove_one_borrowed_register(items, control, register_name, marked_items, proof);
  proof.proved = proof.reasons.empty();
  return proof;
}

} // namespace mkpro::core
