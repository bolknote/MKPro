#include "mkpro/core/post_layout_control_flow.hpp"

#include "mkpro/core/opcodes.hpp"

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
  const std::optional<PostLayoutCommandIdentity> main = identity_at_address(items, index, 0);
  if (!main.has_value()) {
    add_reason(result, "physical main entry 0 is not an executable command cell");
    return;
  }
  add_external_entry(result, items, index, 0, {}, ExternalEntryKind::Main);

  std::deque<ExecutionState> pending;
  pending.push_back(ExecutionState{.pc = 0});
  std::set<ExecutionState> visited;
  while (!pending.empty() && result.reasons.empty()) {
    ExecutionState state = std::move(pending.front());
    pending.pop_front();
    if (!visited.insert(state).second)
      continue;
    if (visited.size() > options.maximum_execution_states) {
      add_reason(result, "control-flow exploration exceeds the execution-state cap");
      break;
    }
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
      if (!identity_at_address(items, index, pc).has_value()) {
        add_reason(result, "control flow has a missing executable successor");
        return;
      }
      pending.push_back(ExecutionState{.pc = pc, .returns = returns});
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
          if (phase + 1 == static_cast<int>(protocol->second.phase_items.size()))
            enqueue(index.item_addresses.at(phase_item), state.returns);
        }
      } else {
        const int resume_pc = state.pc + 1;
        if (add_external_entry(result, items, index, resume_pc, state.returns,
                               ExternalEntryKind::ResumableStop)) {
          enqueue(resume_pc, state.returns);
        }
      }
      continue;
    }

    if (opcode == kReturnOpcode) {
      if (state.returns.empty()) {
        add_reason(result, "reachable В/О has an empty return stack");
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
      const int fallthrough = index.item_addresses.at(*operand) + 1;
      if (opcode == kJumpOpcode) {
        enqueue(*target, state.returns);
      } else if (opcode == kCallOpcode) {
        if (static_cast<int>(state.returns.size()) >= options.maximum_return_depth) {
          add_reason(result, "control flow exceeds the configured return-stack depth");
          continue;
        }
        if (!identity_at_address(items, index, fallthrough).has_value()) {
          add_reason(result, "direct call has no executable continuation");
          continue;
        }
        std::vector<int> returns = state.returns;
        returns.push_back(fallthrough);
        enqueue(*target, returns);
      } else if (is_direct_conditional_opcode(opcode)) {
        enqueue(*target, state.returns);
        enqueue(fallthrough, state.returns);
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
        const int continuation = state.pc + 1;
        if (!identity_at_address(items, index, continuation).has_value()) {
          add_reason(result, "indirect call has no executable continuation");
          continue;
        }
        returns.push_back(continuation);
      }
      for (const PostLayoutCommandIdentity& target : targets->second)
        enqueue(target.address, returns);
      if (is_indirect_conditional_opcode(opcode))
        enqueue(state.pc + 1, state.returns);
      continue;
    }

    enqueue(state.pc + 1, state.returns);
  }
  result.explored_states = visited.size();

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
  const std::map<std::size_t, ManualProtocol> protocols =
      validate_manual_protocols(items, index, result);
  if (!result.reasons.empty())
    return result;

  explore_entries_and_return_stacks(items, index, protocols, options, result);
  result.proved = result.reasons.empty();
  return result;
}

} // namespace mkpro::core
