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
constexpr int kErrorStopOpcode = 0x29;

struct ArtifactIndex {
  std::vector<int> item_addresses;
  std::map<int, std::size_t> cell_items;
  std::map<std::string, int> label_addresses;
  std::map<int, std::vector<std::string>> address_labels;
  std::set<std::string> duplicate_labels;
  int cells = 0;
};

struct ExecutionCursor {
  int address = -1;
  std::optional<int> formal_opcode;
  auto operator<=>(const ExecutionCursor&) const = default;
};

struct ExecutionState {
  ExecutionCursor pc;
  std::vector<ExecutionCursor> returns;
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

ExecutionCursor canonical_cursor(const ArtifactIndex& index, int address,
                                 AddressSpaceModel model) {
  // Oversized analysis artifacts live in a parallel logical space. Their
  // linear successors must never be reinterpreted as hardware dark aliases.
  return ExecutionCursor{
      .address = address,
      .formal_opcode = index.cells <= official_program_step_limit(model) &&
                               address >= 0 && address <= official_program_last_address(model)
                           ? std::optional<int>(official_address_to_opcode(address, model))
                           : std::nullopt,
  };
}

ExecutionCursor formal_cursor(int opcode, AddressSpaceModel model) {
  return {.address = formal_address_info(opcode, model).actual, .formal_opcode = opcode};
}

std::optional<ExecutionCursor> sequential_successor(
    const ArtifactIndex& index, const ExecutionCursor& cursor,
    AddressSpaceModel model) {
  ExecutionCursor next = cursor.formal_opcode.has_value()
      ? formal_cursor(formal_address_successor_opcode(*cursor.formal_opcode), model)
      : ExecutionCursor{.address = cursor.address + 1};
  return index.cell_items.contains(next.address) ? std::optional(next) : std::nullopt;
}

std::optional<ExecutionCursor> direct_target_cursor(
    const MachineItem& operand, const ArtifactIndex& index, AddressSpaceModel model,
    AuthoritativePostLayoutControlFlow& result) {
  try {
    // At a discontinuity the word is read from an ordinary command cell,
    // e.g. BP at B1 fetches its address byte from physical 00, not 07.
    if (operand.kind == MachineItemKind::Op)
      return formal_cursor(operand.opcode, model);
    if (operand.kind != MachineItemKind::Address)
      return std::nullopt;
    if (operand.formal_opcode.has_value())
      return formal_cursor(*operand.formal_opcode, model);
    if (const auto* address = std::get_if<int>(&operand.target))
      return canonical_cursor(index, *address, model);
    const auto* label = std::get_if<std::string>(&operand.target);
    if (label == nullptr)
      return std::nullopt;
    const auto found = index.label_addresses.find(*label);
    return found == index.label_addresses.end()
        ? std::nullopt
        : std::optional(canonical_cursor(index, found->second, model));
  } catch (const std::exception&) {
    add_reason(result, "formal direct operand is invalid for this address space");
    return std::nullopt;
  }
}

std::optional<int> direct_target_address(
    const MachineItem& operand, const ArtifactIndex& index, AddressSpaceModel model,
    AuthoritativePostLayoutControlFlow& result) {
  const auto cursor = direct_target_cursor(operand, index, model, result);
  return cursor.has_value() ? std::optional(cursor->address) : std::nullopt;
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
                                         std::set<std::size_t>& consumed_operands,
                                         AuthoritativePostLayoutControlFlow& result) {
  if (items.empty() || index.cells == 0)
    add_reason(result, "artifact contains no command cells");
  if (!index.duplicate_labels.empty())
    add_reason(result, "artifact contains duplicate labels");

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
    if (item.opcode == kStopOpcode || item.opcode == kErrorStopOpcode) {
      if (item.stop_disposition == StopDisposition::Unknown)
        add_reason(result, "stop-like command has unknown disposition");
    } else if (item.stop_disposition != StopDisposition::Unknown) {
      add_reason(result, "stop disposition is attached to a non-stop command");
    }

    if (takes_address(item)) {
      const std::optional<std::size_t> operand = next_cell_item(items, item_index);
      const bool encoded_word = operand.has_value() &&
          std::find(options.opcode_address_words.begin(), options.opcode_address_words.end(),
                    *operand) != options.opcode_address_words.end();
      if (!operand.has_value() ||
          (items.at(*operand).kind != MachineItemKind::Address && !encoded_word)) {
        // The last physical cell of a complete image fetches its operand
        // through the counter's side branch. Validate that actual word while
        // exploring the reachable formal execution context.
        if (index.cells != official_program_step_limit(options.address_space_model) ||
            index.item_addresses.at(item_index) != index.cells - 1)
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
      if (!item.indirect_memory_targets.has_value() &&
          item.opcode >= 0xd0 && item.opcode <= 0xde && !item.raw &&
          item.discarded_indirect_recall_value) {
        // A selector-only recall still reads memory. Its complete conservative
        // alias set is every stock data register, not the empty set. This says
        // nothing about whether X, X1 or X2 may expose the discarded value.
        std::vector<int> targets;
        for (int reg = 0; reg <= 0x0e; ++reg)
          targets.push_back(reg);
        result.indirect_memory_targets.emplace(item_index, std::move(targets));
      } else if (!item.indirect_memory_targets.has_value() || item.indirect_memory_targets->empty()) {
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

  for (const auto& [source, encoded_targets] : options.proved_indirect_formal_targets) {
    const auto typed = result.indirect_flow_targets.find(source);
    if (typed == result.indirect_flow_targets.end() || encoded_targets.empty()) {
      add_reason(result, "formal indirect entry facts have no typed flow consumer");
      continue;
    }
    std::set<int> physical_targets;
    std::set<int> encodings;
    for (const int encoded : encoded_targets) {
      try {
        if (!encodings.insert(encoded).second)
          add_reason(result, "formal indirect entry facts contain a duplicate encoding");
        physical_targets.insert(formal_address_info(encoded, options.address_space_model).actual);
      } catch (const std::exception&) {
        add_reason(result, "formal indirect entry encoding is invalid");
      }
    }
    std::set<int> expected;
    for (const auto& target : typed->second) expected.insert(target.address);
    if (physical_targets != expected)
      add_reason(result, "formal indirect entries do not match their physical target facts");
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

std::vector<int> cursor_addresses(const std::vector<ExecutionCursor>& cursors) {
  std::vector<int> result;
  for (const auto& cursor : cursors) result.push_back(cursor.address);
  return result;
}

std::vector<std::optional<int>> cursor_formals(
    const std::vector<ExecutionCursor>& cursors) {
  std::vector<std::optional<int>> result;
  for (const auto& cursor : cursors) result.push_back(cursor.formal_opcode);
  return result;
}

bool add_external_entry(AuthoritativePostLayoutControlFlow& result,
                        const std::vector<MachineItem>& items, const ArtifactIndex& index,
                        const ExecutionCursor& pc,
                        const std::vector<ExecutionCursor>& returns, ExternalEntryKind kind,
                        std::optional<ManualInteractionAnchor> manual = std::nullopt) {
  const std::optional<PostLayoutCommandIdentity> entry = identity_at_address(items, index, pc.address);
  if (!entry.has_value()) {
    add_reason(result, "external entry is not an executable command cell");
    return false;
  }
  std::vector<PostLayoutCommandIdentity> return_stack;
  return_stack.reserve(returns.size());
  for (const auto& address : returns) {
    const std::optional<PostLayoutCommandIdentity> identity =
        identity_at_address(items, index, address.address);
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
      .formal_opcode = pc.formal_opcode,
      .formal_return_stack = cursor_formals(returns),
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
  ExecutionCursor main_cursor = canonical_cursor(index, main->address, options.address_space_model);
  if (options.main_formal_opcode.has_value()) {
    try {
      main_cursor = formal_cursor(*options.main_formal_opcode, options.address_space_model);
    } catch (const std::exception&) {
      add_reason(result, "invalid formal main entry");
      return;
    }
    if (main_cursor.address != main->address) {
      add_reason(result, "formal main entry does not match its command identity");
      return;
    }
  }
  add_external_entry(result, items, index, main_cursor, {}, ExternalEntryKind::Main);

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
        identity_at_address(items, index, state.pc.address);
    if (!identity.has_value()) {
      add_reason(result, "control flow has a missing executable successor");
      return std::nullopt;
    }
    const std::size_t id = states.size();
    state_ids.emplace(state, id);
    states.push_back(state);
    result.execution_states.push_back(PostLayoutExecutionState{
        .item_index = identity->item_index,
        .address = state.pc.address,
        .return_stack = cursor_addresses(state.returns),
        .formal_opcode = state.pc.formal_opcode,
        .formal_return_stack = cursor_formals(state.returns),
    });
    result.execution_successors.emplace_back();
    result.execution_edges.emplace_back();
    pending.push_back(id);
    return id;
  };
  (void)record_state(ExecutionState{.pc = main_cursor});
  std::size_t explored = 0;
  while (!pending.empty() && result.reasons.empty()) {
    const std::size_t state_id = pending.front();
    pending.pop_front();
    const ExecutionState state = states.at(state_id);
    ++explored;
    result.maximum_observed_return_depth =
        std::max(result.maximum_observed_return_depth, static_cast<int>(state.returns.size()));

    const auto cell = index.cell_items.find(state.pc.address);
    if (cell == index.cell_items.end() || items.at(cell->second).kind != MachineItemKind::Op) {
      add_reason(result, "control flow reaches a non-executable cell");
      break;
    }
    const std::size_t item_index = cell->second;
    const MachineItem& item = items.at(item_index);
    const int opcode = item.opcode;

    const auto enqueue = [&](ExecutionCursor pc, const std::vector<ExecutionCursor>& returns,
                             PostLayoutExecutionEdgeKind kind =
                                 PostLayoutExecutionEdgeKind::Fallthrough) {
      const std::optional<std::size_t> successor =
          record_state(ExecutionState{.pc = pc, .returns = returns});
      if (!successor.has_value())
        return;
      std::vector<std::size_t>& edges = result.execution_successors.at(state_id);
      if (std::find(edges.begin(), edges.end(), *successor) == edges.end())
        edges.push_back(*successor);
      const PostLayoutExecutionEdge labelled{
          .target_state = *successor,
          .kind = kind,
          .indirect_formal_opcode = kind == PostLayoutExecutionEdgeKind::IndirectTarget
                                        ? pc.formal_opcode : std::nullopt,
      };
      auto& alternatives = result.execution_edges.at(state_id);
      if (std::find(alternatives.begin(), alternatives.end(), labelled) == alternatives.end())
        alternatives.push_back(labelled);
    };

    if (opcode == kErrorStopOpcode) {
      if (item.stop_disposition == StopDisposition::Terminal)
        continue;
      if (item.stop_disposition != StopDisposition::Resumable) {
        add_reason(result, "reachable error stop has unknown disposition");
        continue;
      }
      const std::optional<ExecutionCursor> padding_pc =
          sequential_successor(index, state.pc, options.address_space_model);
      if (!padding_pc.has_value() ||
          !identity_at_address(items, index, padding_pc->address).has_value()) {
        add_reason(result, "resumable error stop has no physical padding cell");
        continue;
      }
      const std::optional<ExecutionCursor> resume_pc =
          sequential_successor(index, *padding_pc, options.address_space_model);
      if (!resume_pc.has_value()) {
        add_reason(result, "resumable error stop has no executable continuation");
      } else if (add_external_entry(result, items, index, *resume_pc, state.returns,
                                    ExternalEntryKind::ResumableStop)) {
        enqueue(*resume_pc, state.returns, PostLayoutExecutionEdgeKind::Resume);
      }
      continue;
    }

    if (opcode == kStopOpcode) {
      if (item.stop_disposition == StopDisposition::Terminal)
        continue;
      if (item.stop_disposition != StopDisposition::Resumable) {
        add_reason(result, "reachable STOP command has unknown disposition");
        continue;
      }
      const auto protocol = protocols.find(item_index);
      if (protocol != protocols.end()) {
        auto phase_cursor = sequential_successor(index, state.pc, options.address_space_model);
        const auto first_cursor = phase_cursor;
        for (const auto& [phase, phase_item] : protocol->second.phase_items) {
          (void)phase;
          if (!phase_cursor.has_value() ||
              phase_cursor->address != index.item_addresses.at(phase_item)) {
            add_reason(result, "manual protocol crosses a non-linear formal continuation");
            break;
          }
          const ManualInteractionAnchor anchor = *items.at(phase_item).manual_interaction;
          add_external_entry(result, items, index, *phase_cursor, state.returns,
                             anchor.kind == ManualInteractionAnchorKind::SingleStepCommand
                                 ? ExternalEntryKind::ManualSingleStep
                                 : ExternalEntryKind::ManualContinuous,
                             anchor);
          phase_cursor = sequential_successor(index, *phase_cursor, options.address_space_model);
        }
        if (first_cursor.has_value() && result.reasons.empty())
          enqueue(*first_cursor, state.returns, PostLayoutExecutionEdgeKind::Resume);
        else if (!first_cursor.has_value())
          add_reason(result, "manual protocol has no first input phase");
      } else {
        const std::optional<ExecutionCursor> resume_pc =
            sequential_successor(index, state.pc, options.address_space_model);
        if (!resume_pc.has_value()) {
          add_reason(result, "resumable STOP has no executable continuation");
        } else if (add_external_entry(result, items, index, *resume_pc, state.returns,
                               ExternalEntryKind::ResumableStop)) {
          enqueue(*resume_pc, state.returns, PostLayoutExecutionEdgeKind::Resume);
        }
      }
      continue;
    }

    if (opcode == kReturnOpcode) {
      if (state.returns.empty()) {
        if (!result.empty_return_target.has_value()) {
          add_reason(result, options.empty_return_target.has_value()
                                 ? "reachable В/О has an unresolved or non-executable typed empty-return target"
                                 : "reachable В/О has an empty return stack");
        } else {
          enqueue(canonical_cursor(index, result.empty_return_target->address,
                                   options.address_space_model), state.returns,
                  PostLayoutExecutionEdgeKind::Return);
        }
        continue;
      }
      std::vector<ExecutionCursor> returns = state.returns;
      const ExecutionCursor return_pc = returns.back();
      returns.pop_back();
      enqueue(return_pc, returns, PostLayoutExecutionEdgeKind::Return);
      continue;
    }

    if (takes_address(item)) {
      const auto operand_cursor = sequential_successor(index, state.pc, options.address_space_model);
      if (!operand_cursor.has_value()) {
        add_reason(result, "reachable address-taking command has no operand");
        continue;
      }
      const auto operand = index.cell_items.find(operand_cursor->address);
      if (operand == index.cell_items.end()) {
        add_reason(result, "formal address operand has no physical cell");
        continue;
      }
      result.execution_states.at(state_id).operand_item_index = operand->second;
      const auto target = direct_target_cursor(
          items.at(operand->second), index, options.address_space_model, result);
      if (!target.has_value()) {
        add_reason(result, "reachable address word has no resolved execution target");
        continue;
      }
      const auto fallthrough = sequential_successor(index, *operand_cursor, options.address_space_model);
      if (opcode == kJumpOpcode) {
        enqueue(*target, state.returns, PostLayoutExecutionEdgeKind::DirectTarget);
      } else if (opcode == kCallOpcode) {
        if (static_cast<int>(state.returns.size()) >= options.maximum_return_depth) {
          add_reason(result, "control flow exceeds the configured return-stack depth");
          continue;
        }
        if (!fallthrough.has_value() ||
            !identity_at_address(items, index, fallthrough->address).has_value()) {
          add_reason(result, "direct call has no executable continuation");
          continue;
        }
        std::vector<ExecutionCursor> returns = state.returns;
        returns.push_back(*fallthrough);
        enqueue(*target, returns, PostLayoutExecutionEdgeKind::DirectTarget);
      } else if (is_direct_conditional_opcode(opcode)) {
        if (!fallthrough.has_value()) {
          add_reason(result, "direct conditional has no executable fallthrough");
          continue;
        }
        enqueue(*target, state.returns, PostLayoutExecutionEdgeKind::DirectTarget);
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
      std::vector<ExecutionCursor> returns = state.returns;
      if (is_indirect_call_opcode(opcode)) {
        if (static_cast<int>(returns.size()) >= options.maximum_return_depth) {
          add_reason(result, "control flow exceeds the configured return-stack depth");
          continue;
        }
        const std::optional<ExecutionCursor> continuation =
            sequential_successor(index, state.pc, options.address_space_model);
        if (!continuation.has_value() ||
            !identity_at_address(items, index, continuation->address).has_value()) {
          add_reason(result, "indirect call has no executable continuation");
          continue;
        }
        returns.push_back(*continuation);
      }
      const auto formal = options.proved_indirect_formal_targets.find(item_index);
      if (formal != options.proved_indirect_formal_targets.end()) {
        for (const int encoded : formal->second)
          enqueue(formal_cursor(encoded, options.address_space_model), returns,
                  PostLayoutExecutionEdgeKind::IndirectTarget);
      } else {
        for (const PostLayoutCommandIdentity& target : targets->second)
          enqueue(canonical_cursor(index, target.address, options.address_space_model), returns,
                  PostLayoutExecutionEdgeKind::IndirectTarget);
      }
      if (is_indirect_conditional_opcode(opcode)) {
        const std::optional<ExecutionCursor> fallthrough =
            sequential_successor(index, state.pc, options.address_space_model);
        if (!fallthrough.has_value()) {
          add_reason(result, "indirect conditional has no executable fallthrough");
        } else {
          enqueue(*fallthrough, state.returns);
        }
      }
      continue;
    }

    const std::optional<ExecutionCursor> successor =
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
        return std::tie(left.entry.address, left_stack, left.kind, left_protocol, left_phase,
                        left.formal_opcode, left.formal_return_stack) <
               std::tie(right.entry.address, right_stack, right.kind, right_protocol, right_phase,
                        right.formal_opcode, right.formal_return_stack);
      });
}

} // namespace

bool has_executable_address_words(const std::vector<MachineItem>& items) {
  return std::any_of(items.begin(), items.end(), [](const MachineItem& item) {
    return item.kind == MachineItemKind::Address &&
           std::find(item.roles.begin(), item.roles.end(), "exec") != item.roles.end();
  });
}

std::optional<PostLayoutByteImage> materialize_post_layout_byte_image(
    const std::vector<MachineItem>& items, const PostLayoutControlFlowOptions& options) {
  const ArtifactIndex index = index_artifact(items);
  if (!index.duplicate_labels.empty())
    return std::nullopt;
  PostLayoutByteImage image{.items = items, .options = options};
  for (std::size_t i = 0; i < items.size(); ++i) {
    const auto& operand = items.at(i);
    if (operand.kind != MachineItemKind::Address ||
        std::find(operand.roles.begin(), operand.roles.end(), "exec") == operand.roles.end())
      continue;
    // An over-window logical address is not a hardware operand byte.
    if (index.cells > official_program_step_limit(options.address_space_model))
      return std::nullopt;
    std::optional<int> target;
    if (const int* address = std::get_if<int>(&operand.target)) {
      target = *address;
    } else {
      const auto found = index.label_addresses.find(std::get<std::string>(operand.target));
      if (found != index.label_addresses.end())
        target = found->second;
    }
    if (!target.has_value() || !index.cell_items.contains(*target))
      return std::nullopt;
    try {
      const int encoded = operand.formal_opcode.has_value()
          ? *operand.formal_opcode
          : official_address_to_opcode(*target, options.address_space_model);
      if (formal_address_info(encoded, options.address_space_model).actual != *target)
        return std::nullopt;
      auto& command = image.items.at(i);
      command.kind = MachineItemKind::Op;
      command.opcode = encoded;
      command.mnemonic = opcode_by_code(encoded).name;
      image.options.opcode_address_words.push_back(i);
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }
  return image;
}

AuthoritativePostLayoutControlFlow
build_post_layout_control_flow(const std::vector<MachineItem>& items,
                               const PostLayoutControlFlowOptions& options) {
  AuthoritativePostLayoutControlFlow result;
  result.address_space_model = options.address_space_model;
  if (has_executable_address_words(items)) {
    const auto image = materialize_post_layout_byte_image(items, options);
    if (!image.has_value()) {
      add_reason(result, "executable address operand has no valid final byte image");
      return result;
    }
    return build_post_layout_control_flow(image->items, image->options);
  }
  if (options.maximum_return_depth < 0 || options.maximum_return_depth > 5) {
    add_reason(result, "maximum return-stack depth must be between zero and five");
    return result;
  }
  if (options.maximum_execution_states == 0U) {
    add_reason(result, "execution-state cap must be positive");
    return result;
  }

  PostLayoutControlFlowOptions execution_options = options;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const auto& encoded = items.at(item_index).indirect_flow_formal_targets;
    if (!encoded.has_value())
      continue;
    const auto [position, inserted] =
        execution_options.proved_indirect_formal_targets.emplace(item_index, *encoded);
    if (!inserted) {
      std::vector<int> supplied = position->second;
      std::vector<int> attached = *encoded;
      std::sort(supplied.begin(), supplied.end());
      std::sort(attached.begin(), attached.end());
      if (supplied != attached)
        add_reason(result, "typed and supplied formal indirect entry facts disagree");
    }
  }

  const ArtifactIndex index = index_artifact(items);
  std::set<std::size_t> encoded_words;
  for (const std::size_t word : options.opcode_address_words) {
    if (word >= items.size() || items.at(word).kind != MachineItemKind::Op ||
        !encoded_words.insert(word).second)
      add_reason(result, "encoded address-word declaration is invalid or duplicated");
  }
  std::set<std::size_t> consumed_operands;
  validate_artifact_and_typed_targets(items, index, execution_options, consumed_operands, result);
  for (const std::size_t word : encoded_words)
    if (!consumed_operands.contains(word))
      add_reason(result, "encoded address word has no typed operand owner");
  if (options.empty_return_target.has_value()) {
    const std::optional<PostLayoutCommandIdentity> target =
        resolve_indirect_target(items, index, *options.empty_return_target);
    if (target.has_value()) {
      result.empty_return_target = *target;
    }
    // This is a policy for an empty-stack return, not an unconditional entry
    // edge. In an intermediate layout its destination can still be an address
    // operand. Reject that destination only if exact reachability encounters a
    // return without a frame, including all admitted manual resume states.
  }
  const std::map<std::size_t, ManualProtocol> protocols =
      validate_manual_protocols(items, index, result);
  if (!result.reasons.empty())
    return result;

  explore_entries_and_return_stacks(items, index, protocols, execution_options, result);
  // A real operand may be fetched through a non-linear counter continuation,
  // not from the command's physical neighbour. Do not classify it as orphaned
  // before the exact execution graph has accounted for all such reads.
  if (result.reasons.empty()) {
    for (const auto& state : result.execution_states)
      if (state.operand_item_index.has_value())
        consumed_operands.insert(*state.operand_item_index);
    for (std::size_t item_index = 0; item_index < items.size(); ++item_index)
      if (items.at(item_index).kind == MachineItemKind::Address &&
          !consumed_operands.contains(item_index))
        add_reason(result, "artifact contains an orphan address operand");
  }
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

PostLayoutExecutionRelocationProof prove_post_layout_execution_relocation(
    const std::vector<MachineItem>& before,
    const std::vector<MachineItem>& after,
    const AuthoritativePostLayoutControlFlow& before_control,
    const AuthoritativePostLayoutControlFlow& after_control,
    const std::vector<std::optional<std::size_t>>& old_to_new_item,
    const PostLayoutExecutionRelocationOptions& options) {
  PostLayoutExecutionRelocationProof proof;
  const auto reject = [&](std::string reason) {
    proof.reasons.push_back(std::move(reason));
    return proof;
  };
  if (!before_control.proved || !after_control.proved ||
      before_control.address_space_model != after_control.address_space_model ||
      before_control.execution_states.empty() || after_control.execution_states.empty() ||
      before_control.execution_states.size() != before_control.execution_successors.size() ||
      after_control.execution_states.size() != after_control.execution_successors.size() ||
      before_control.execution_states.size() != before_control.execution_edges.size() ||
      after_control.execution_states.size() != after_control.execution_edges.size() ||
      before_control.maximum_observed_return_depth != after_control.maximum_observed_return_depth)
    return reject("exact labelled execution graphs are missing or incompatible");
  if (old_to_new_item.size() != before.size() || options.maximum_state_pairs == 0U)
    return reject("invalid command relocation or execution-pair budget");

  const ArtifactIndex before_index = index_artifact(before);
  const ArtifactIndex after_index = index_artifact(after);
  const auto mapped_item = [&](std::size_t item) -> std::optional<std::size_t> {
    if (item >= old_to_new_item.size() || !old_to_new_item.at(item).has_value() ||
        *old_to_new_item.at(item) >= after.size())
      return std::nullopt;
    return old_to_new_item.at(item);
  };
  const auto mapped_address = [&](int address) -> std::optional<int> {
    const auto old_cell = before_index.cell_items.find(address);
    if (old_cell == before_index.cell_items.end())
      return std::nullopt;
    const auto next = mapped_item(old_cell->second);
    return next.has_value() ? std::optional<int>(after_index.item_addresses.at(*next))
                            : std::nullopt;
  };
  const auto frame_addresses = [&](const std::vector<int>& frames, bool remap)
      -> std::optional<std::vector<int>> {
    if (!remap)
      return frames;
    std::vector<int> mapped;
    for (const int address : frames) {
      const auto next = mapped_address(address);
      if (!next.has_value())
        return std::nullopt;
      mapped.push_back(*next);
    }
    return mapped;
  };

  using ExactState = std::tuple<std::size_t, std::optional<int>, std::vector<int>,
                                std::vector<std::optional<int>>>;
  using EntryLabel = std::tuple<int, bool, int, int, int>;
  struct Entries {
    std::vector<std::set<EntryLabel>> labels;
    std::vector<std::size_t> mains;
  };
  const auto entry_facts = [](const AuthoritativePostLayoutControlFlow& control)
      -> std::optional<Entries> {
    Entries result;
    result.labels.resize(control.execution_states.size());
    std::map<ExactState, std::size_t> states;
    for (std::size_t i = 0; i < control.execution_states.size(); ++i) {
      const auto& state = control.execution_states.at(i);
      if (state.return_stack.size() != state.formal_return_stack.size() ||
          !states.emplace(ExactState{state.item_index, state.formal_opcode,
                                     state.return_stack, state.formal_return_stack}, i).second)
        return std::nullopt;
    }
    for (const auto& entry : control.external_entries) {
      std::vector<int> frames;
      for (const auto& frame : entry.return_stack)
        frames.push_back(frame.address);
      const auto found = states.find(ExactState{entry.entry.item_index, entry.formal_opcode,
                                               frames, entry.formal_return_stack});
      if (found == states.end() ||
          control.execution_states.at(found->second).address != entry.entry.address)
        return std::nullopt;
      const auto& manual = entry.manual_interaction;
      // Main is an initial root, not a fresh user interaction whenever an
      // internal loop revisits the same command under another formal counter.
      // The unique main-to-main correspondence is pinned by the initial pair.
      if (entry.kind != ExternalEntryKind::Main)
        result.labels.at(found->second).emplace(
            static_cast<int>(entry.kind), manual.has_value(),
            manual.has_value() ? manual->protocol_id : -1,
            manual.has_value() ? manual->phase : -1,
            manual.has_value() ? static_cast<int>(manual->kind) : -1);
      if (entry.kind == ExternalEntryKind::Main)
        result.mains.push_back(found->second);
    }
    if (result.mains.size() != 1U)
      return std::nullopt;
    return result;
  };
  const auto old_entries = entry_facts(before_control);
  const auto new_entries = entry_facts(after_control);
  if (!old_entries.has_value() || !new_entries.has_value())
    return reject("external entries do not identify exact execution contexts");

  for (const auto& [source, remap] : options.indirect_entry_remap) {
    const auto target = mapped_item(source);
    if (source >= before.size() || !target.has_value() ||
        before.at(source).kind != MachineItemKind::Op ||
        after.at(*target).kind != MachineItemKind::Op ||
        !is_indirect_flow_opcode(before.at(source).opcode) ||
        !is_indirect_flow_opcode(after.at(*target).opcode))
      return reject("selector transport is attached to a non-indirect command");
    for (const auto& [old_code, new_code] : remap)
      if (old_code < 0 || old_code > 255 || new_code < 0 || new_code > 255)
        return reject("selector transport contains an invalid encoded counter");
  }

  std::set<std::size_t> converted_direct_flows;
  for (const std::size_t source : options.direct_to_indirect_flow_items) {
    const auto target = mapped_item(source);
    if (source >= before.size() || !target.has_value() ||
        !converted_direct_flows.insert(source).second ||
        before.at(source).kind != MachineItemKind::Op ||
        after.at(*target).kind != MachineItemKind::Op ||
        before.at(source).raw || after.at(*target).raw ||
        before.at(source).manual_interaction.has_value() ||
        after.at(*target).manual_interaction.has_value())
      return reject("invalid direct-to-indirect command transport");
    int family = -1;
    switch (before.at(source).opcode) {
    case 0x51: family = 0x80; break;
    case 0x53: family = 0xa0; break;
    case 0x57: family = 0x70; break;
    case 0x59: family = 0x90; break;
    case 0x5c: family = 0xc0; break;
    case 0x5e: family = 0xe0; break;
    default: break;
    }
    const int selector = after.at(*target).opcode - family;
    if (family < 0 || selector < 7 || selector > 14)
      return reject("direct-to-indirect transport changes the branch family or mutates its selector");
  }

  using EdgeKey = std::tuple<PostLayoutExecutionEdgeKind, std::size_t, std::vector<int>>;
  using EdgeGroups = std::map<EdgeKey, std::vector<PostLayoutExecutionEdge>>;
  const auto edge_groups = [&](const AuthoritativePostLayoutControlFlow& control,
                               std::size_t state_index, bool remap)
      -> std::optional<EdgeGroups> {
    EdgeGroups groups;
    std::set<std::size_t> targets;
    for (const auto& edge : control.execution_edges.at(state_index)) {
      if (edge.target_state >= control.execution_states.size() ||
          (edge.kind != PostLayoutExecutionEdgeKind::IndirectTarget &&
           edge.indirect_formal_opcode.has_value()))
        return std::nullopt;
      const auto& target = control.execution_states.at(edge.target_state);
      if (edge.kind == PostLayoutExecutionEdgeKind::IndirectTarget &&
          edge.indirect_formal_opcode != target.formal_opcode)
        return std::nullopt;
      const auto item = remap ? mapped_item(target.item_index)
                              : std::optional<std::size_t>(target.item_index);
      const auto frames = frame_addresses(target.return_stack, remap);
      if (!item.has_value() || !frames.has_value())
        return std::nullopt;
      auto kind = edge.kind;
      if (remap && converted_direct_flows.contains(
                       control.execution_states.at(state_index).item_index) &&
          kind == PostLayoutExecutionEdgeKind::DirectTarget)
        kind = PostLayoutExecutionEdgeKind::IndirectTarget;
      groups[EdgeKey{kind, *item, *frames}].push_back(edge);
      targets.insert(edge.target_state);
    }
    const auto& projected = control.execution_successors.at(state_index);
    if (targets != std::set<std::size_t>(projected.begin(), projected.end()))
      return std::nullopt;
    return groups;
  };

  using StatePair = std::pair<std::size_t, std::size_t>;
  std::set<StatePair> discovered;
  std::deque<StatePair> pending;
  std::set<std::size_t> old_seen, new_seen;
  std::set<std::pair<std::size_t, int>> used_remaps;
  const auto enqueue = [&](std::size_t old_state, std::size_t new_state) {
    const StatePair pair{old_state, new_state};
    if (discovered.contains(pair))
      return true;
    if (discovered.size() >= options.maximum_state_pairs)
      return false;
    discovered.insert(pair);
    pending.push_back(pair);
    return true;
  };
  (void)enqueue(old_entries->mains.front(), new_entries->mains.front());
  while (!pending.empty()) {
    const auto [old_state_index, new_state_index] = pending.front();
    pending.pop_front();
    ++proof.state_pairs;
    const auto& old_state = before_control.execution_states.at(old_state_index);
    const auto& new_state = after_control.execution_states.at(new_state_index);
    const auto target = mapped_item(old_state.item_index);
    const auto frames = frame_addresses(old_state.return_stack, true);
    if ((!target.has_value() || *target != new_state.item_index) &&
        options.allow_bypassed_direct_jumps && frames.has_value() &&
        *frames == new_state.return_stack &&
        old_entries->labels.at(old_state_index).empty()) {
      const auto& jump = before.at(old_state.item_index);
      const auto& edges = before_control.execution_edges.at(old_state_index);
      if (jump.kind == MachineItemKind::Op && jump.opcode == kJumpOpcode &&
          !jump.manual_interaction.has_value() && edges.size() == 1U &&
          edges.front().kind == PostLayoutExecutionEdgeKind::DirectTarget) {
        old_seen.insert(old_state_index);
        if (!enqueue(edges.front().target_state, new_state_index))
          return reject("transparent-jump transport exceeds its bounded state-pair budget");
        continue;
      }
    }
    if (!target.has_value() || *target != new_state.item_index ||
        !frames.has_value() || *frames != new_state.return_stack ||
        old_entries->labels.at(old_state_index) != new_entries->labels.at(new_state_index))
      return reject("execution context or external interaction does not follow command relocation");
    const auto& old_item = before.at(old_state.item_index);
    const auto& new_item = after.at(new_state.item_index);
    const bool converted_direct = converted_direct_flows.contains(old_state.item_index);
    if (old_item.kind != MachineItemKind::Op || new_item.kind != MachineItemKind::Op ||
        (!converted_direct && old_item.opcode != new_item.opcode) ||
        old_item.raw != new_item.raw ||
        old_item.stop_disposition != new_item.stop_disposition ||
        old_item.manual_interaction != new_item.manual_interaction)
      return reject("paired execution contexts perform different instructions or interactions");
    if (converted_direct) {
      if (!old_state.operand_item_index.has_value() ||
          new_state.operand_item_index.has_value() ||
          mapped_item(*old_state.operand_item_index).has_value())
        return reject("converted flow must delete exactly its fetched address operand");
      const auto& operand = before.at(*old_state.operand_item_index);
      if (operand.kind != MachineItemKind::Address || operand.raw ||
          operand.manual_interaction.has_value())
        return reject("converted flow owns an opaque or externally observed operand");
    } else {
      if (old_state.operand_item_index.has_value() != new_state.operand_item_index.has_value())
        return reject("paired commands disagree about their address operand");
      if (old_state.operand_item_index.has_value()) {
        const auto operand = mapped_item(*old_state.operand_item_index);
        if (!operand.has_value() || operand != new_state.operand_item_index)
          return reject("formal continuation fetches a different relocated address word");
      }
    }

    old_seen.insert(old_state_index);
    new_seen.insert(new_state_index);
    const auto old_groups = edge_groups(before_control, old_state_index, true);
    const auto new_groups = edge_groups(after_control, new_state_index, false);
    if (!old_groups.has_value() || !new_groups.has_value() ||
        old_groups->size() != new_groups->size())
      return reject("labelled execution alternatives differ after relocation");
    const auto declared = options.indirect_entry_remap.find(old_state.item_index);
    for (const auto& [key, old_edges] : *old_groups) {
      auto matching = new_groups->find(key);
      if (matching == new_groups->end() && options.allow_bypassed_direct_jumps &&
          old_groups->size() == 1U && new_groups->size() == 1U) {
        const auto sole = new_groups->begin();
        if (std::get<0>(key) == std::get<0>(sole->first) &&
            std::get<2>(key) == std::get<2>(sole->first))
          matching = sole;
      }
      if (matching == new_groups->end())
        return reject("branch, return, or resume reaches a different relocated context");
      const auto& new_edges = matching->second;
      const bool indirect = std::get<0>(key) == PostLayoutExecutionEdgeKind::IndirectTarget;
      std::set<std::size_t> matched;
      for (const auto& old_edge : old_edges) {
        auto encoding = old_edge.indirect_formal_opcode;
        bool transported = false;
        if (indirect && encoding.has_value() &&
            declared != options.indirect_entry_remap.end()) {
          const auto value = declared->second.find(*encoding);
          if (value != declared->second.end()) {
            used_remaps.emplace(old_state.item_index, *encoding);
            encoding = value->second;
            transported = true;
          }
        }
        std::optional<std::size_t> choice;
        if (old_edges.size() == 1U && new_edges.size() == 1U) {
          if (transported && new_edges.front().indirect_formal_opcode != encoding)
            return reject("declared selector transport does not match its new entry counter");
          choice = 0U;
        } else if (indirect) {
          // Distinct encoded selectors may reach the same physical instruction.
          // Preserve their value correlation; graph-isomorphism permutations
          // alone would accept a swap of B1 and 06 with different continuations.
          for (std::size_t i = 0; i < new_edges.size(); ++i) {
            if (new_edges.at(i).indirect_formal_opcode != encoding)
              continue;
            if (choice.has_value())
              return reject("encoded selector transport has ambiguous continuations");
            choice = i;
          }
        }
        if (!choice.has_value())
          return reject("an indirect entry alternative has no proved selector correspondence");
        matched.insert(*choice);
        if (!enqueue(old_edge.target_state, new_edges.at(*choice).target_state))
          return reject("execution-context transport exceeds its bounded state-pair budget");
      }
      if (matched.size() != new_edges.size())
        return reject("relocation introduces an unpaired execution alternative");
    }
  }
  if (old_seen.size() != before_control.execution_states.size() ||
      new_seen.size() != after_control.execution_states.size())
    return reject("not every exact execution context has a rooted correspondence");
  for (const auto& [source, remap] : options.indirect_entry_remap)
    for (const auto& [old_code, new_code] : remap) {
      (void)new_code;
      if (!used_remaps.contains({source, old_code}))
        return reject("declared selector transport has no reachable matching consumer");
    }
  proof.proved = true;
  return proof;
}

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
