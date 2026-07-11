#include "mkpro/core/natural_target_component_layout.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <array>
#include <compare>
#include <deque>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core {

namespace {

constexpr int kStopOpcode = 0x50;
constexpr int kJumpOpcode = 0x51;
constexpr int kReturnOpcode = 0x52;
constexpr int kCallOpcode = 0x53;
constexpr int kFractionalPartOpcode = 0x35;

constexpr std::array<std::string_view, 15> kRegisterNames = {
    "0", "1", "2", "3", "4", "5", "6", "7",
    "8", "9", "a", "b", "c", "d", "e",
};

struct ArtifactIndex {
  std::vector<int> item_addresses;
  std::map<int, std::size_t> cell_items;
  std::map<std::string, int> label_addresses;
  int cells = 0;
};

struct OwnedItem {
  MachineItem item;
  std::size_t origin = 0;
};

struct Cell {
  std::vector<OwnedItem> labels;
  OwnedItem value;
};

struct Segment {
  std::size_t ordinal = 0;
  std::vector<Cell> cells;
};

struct DirectReference {
  std::size_t command = 0;
  std::size_t operand = 0;
  std::size_t target = 0;
};

struct DirectCallSite {
  std::size_t command = 0;
  std::size_t operand = 0;
  std::size_t target = 0;
};

struct SelectorCandidate {
  int register_index = -1;
  std::string register_name;
  NaturalTargetSelectorOrigin origin = NaturalTargetSelectorOrigin::ExistingPreload;
  std::optional<std::string> value;
  int fixed_target = -1;
};

struct CandidateArtifact {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
  std::map<std::size_t, std::size_t> new_item_by_origin;
  NaturalTargetComponentLayoutPlan plan;
};

enum class TraceEdgeKind {
  Next,
  Taken,
  Fallthrough,
  Call,
  Resume,
};

struct TraceState {
  std::size_t command = 0;
  std::vector<std::size_t> returns;
  auto operator<=>(const TraceState&) const = default;
};

struct TraceEdge {
  TraceEdgeKind kind = TraceEdgeKind::Next;
  TraceState target;
  auto operator<=>(const TraceEdge&) const = default;
};

using TraceGraph = std::map<TraceState, std::vector<TraceEdge>>;

void add_reason(NaturalTargetComponentLayoutPlan& plan, std::string reason) {
  if (std::find(plan.reasons.begin(), plan.reasons.end(), reason) == plan.reasons.end())
    plan.reasons.push_back(std::move(reason));
}

bool is_indirect_flow(int opcode) {
  const int family = opcode & 0xf0;
  return family == 0x70 || family == 0x80 || family == 0x90 || family == 0xa0 ||
         family == 0xc0 || family == 0xe0;
}

bool is_indirect_call(int opcode) {
  return (opcode & 0xf0) == 0xa0;
}

bool is_indirect_conditional(int opcode) {
  const int family = opcode & 0xf0;
  return family == 0x70 || family == 0x90 || family == 0xc0 || family == 0xe0;
}

bool is_indirect_memory(int opcode) {
  const int family = opcode & 0xf0;
  return family == 0xb0 || family == 0xd0;
}

bool is_direct_conditional(int opcode) {
  return opcode >= 0x57 && opcode <= 0x5e;
}

bool takes_address(const MachineItem& item) {
  return item.kind == MachineItemKind::Op && item.opcode >= 0 && item.opcode <= 0xff &&
         opcode_by_code(item.opcode).takes_address;
}

int encoded_register(int opcode) {
  const int low = opcode & 0x0f;
  return low == 0x0f ? 0 : low;
}

std::string register_name(int index) {
  if (index < 0 || index >= static_cast<int>(kRegisterNames.size()))
    return {};
  return std::string(kRegisterNames.at(static_cast<std::size_t>(index)));
}

ArtifactIndex index_artifact(const std::vector<MachineItem>& items) {
  ArtifactIndex index;
  index.item_addresses.resize(items.size(), 0);
  int address = 0;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    index.item_addresses.at(item_index) = address;
    if (item.kind == MachineItemKind::Label) {
      index.label_addresses.emplace(item.name, address);
      continue;
    }
    index.cell_items.emplace(address, item_index);
    ++address;
  }
  index.cells = address;
  return index;
}

std::optional<std::size_t> next_cell_item(const std::vector<MachineItem>& items,
                                          std::size_t after) {
  for (++after; after < items.size(); ++after) {
    if (items.at(after).kind != MachineItemKind::Label)
      return after;
  }
  return std::nullopt;
}

std::optional<std::size_t> command_at_address(const std::vector<MachineItem>& items,
                                              const ArtifactIndex& index, int address) {
  const auto found = index.cell_items.find(address);
  if (found == index.cell_items.end() || items.at(found->second).kind != MachineItemKind::Op)
    return std::nullopt;
  return found->second;
}

std::optional<int> direct_target_address(const MachineItem& operand,
                                         const ArtifactIndex& index,
                                         AddressSpaceModel model) {
  if (operand.kind != MachineItemKind::Address)
    return std::nullopt;
  if (operand.formal_opcode.has_value()) {
    try {
      const FormalAddressInfo formal = formal_address_info(*operand.formal_opcode, model);
      if (formal.kind == FormalAddressKind::SuperDark || formal.one_command ||
          formal.extra.has_value()) {
        return std::nullopt;
      }
      return formal.actual;
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }
  if (const int* target = std::get_if<int>(&operand.target))
    return *target;
  const std::string* label = std::get_if<std::string>(&operand.target);
  if (label == nullptr)
    return std::nullopt;
  const auto found = index.label_addresses.find(*label);
  return found == index.label_addresses.end() ? std::nullopt
                                               : std::optional<int>(found->second);
}

std::optional<std::vector<DirectReference>> collect_direct_references(
    const std::vector<MachineItem>& items, const ArtifactIndex& index,
    AddressSpaceModel model) {
  std::vector<DirectReference> result;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    if (!takes_address(items.at(item_index)))
      continue;
    const std::optional<std::size_t> operand = next_cell_item(items, item_index);
    if (!operand.has_value() || items.at(*operand).kind != MachineItemKind::Address)
      return std::nullopt;
    // A formal operand can be an executable address-code overlay, a dark-side
    // entry, or a super-dark two-command object.  Relocating it as an ordinary
    // numeric operand would preserve only the target identity, not its byte or
    // secondary execution semantics.  Roles/raw metadata are likewise treated
    // as an opaque indication that the operand has another contract.
    if (items.at(*operand).formal_opcode.has_value() || items.at(*operand).raw ||
        !items.at(*operand).roles.empty()) {
      return std::nullopt;
    }
    const std::optional<int> address = direct_target_address(items.at(*operand), index, model);
    if (!address.has_value())
      return std::nullopt;
    const std::optional<std::size_t> target = command_at_address(items, index, *address);
    if (!target.has_value())
      return std::nullopt;
    result.push_back(DirectReference{
        .command = item_index,
        .operand = *operand,
        .target = *target,
    });
  }
  return result;
}

std::vector<DirectCallSite> direct_calls(const std::vector<DirectReference>& references,
                                         const std::vector<MachineItem>& items) {
  std::vector<DirectCallSite> result;
  for (const DirectReference& reference : references) {
    if (items.at(reference.command).opcode == kCallOpcode) {
      result.push_back(DirectCallSite{
          .command = reference.command,
          .operand = reference.operand,
          .target = reference.target,
      });
    }
  }
  return result;
}

std::optional<std::vector<Cell>> make_cells(const std::vector<MachineItem>& items) {
  std::vector<Cell> cells;
  std::vector<OwnedItem> labels;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    OwnedItem owned{.item = items.at(item_index), .origin = item_index};
    if (owned.item.kind == MachineItemKind::Label) {
      labels.push_back(std::move(owned));
      continue;
    }
    cells.push_back(Cell{
        .labels = std::move(labels),
        .value = std::move(owned),
    });
    labels.clear();
  }
  if (!labels.empty())
    return std::nullopt;
  return cells;
}

bool safe_cut_after(const std::vector<Cell>& cells, std::size_t cell_index) {
  const MachineItem& item = cells.at(cell_index).value.item;
  if (item.kind == MachineItemKind::Address) {
    if (cell_index == 0)
      return false;
    const MachineItem& command = cells.at(cell_index - 1).value.item;
    return command.kind == MachineItemKind::Op && command.opcode == kJumpOpcode;
  }
  if (item.kind != MachineItemKind::Op)
    return false;
  if (item.opcode == kReturnOpcode)
    return true;
  if (item.opcode == kStopOpcode)
    return item.stop_disposition == StopDisposition::Terminal;
  return (item.opcode & 0xf0) == 0x80;
}

std::vector<Segment> make_segments(std::vector<Cell> cells) {
  std::vector<Segment> segments;
  Segment current;
  current.ordinal = 0;
  for (std::size_t cell_index = 0; cell_index < cells.size(); ++cell_index) {
    const bool cut = safe_cut_after(cells, cell_index);
    current.cells.push_back(std::move(cells.at(cell_index)));
    if (!cut && cell_index + 1U < cells.size())
      continue;
    segments.push_back(std::move(current));
    current = Segment{};
    current.ordinal = segments.size();
  }
  if (!current.cells.empty())
    segments.push_back(std::move(current));
  return segments;
}

int segment_cells(const Segment& segment) {
  return static_cast<int>(segment.cells.size());
}

std::optional<std::pair<std::size_t, int>> locate_origin(
    const std::vector<Segment>& segments, std::size_t origin) {
  for (std::size_t segment_index = 0; segment_index < segments.size(); ++segment_index) {
    int offset = 0;
    for (const Cell& cell : segments.at(segment_index).cells) {
      if (cell.value.origin == origin)
        return std::pair{segment_index, offset};
      ++offset;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> main_origin(
    const AuthoritativePostLayoutControlFlow& control_flow) {
  std::optional<std::size_t> result;
  for (const PostLayoutExternalEntryState& entry : control_flow.external_entries) {
    if (entry.kind != ExternalEntryKind::Main)
      continue;
    if (result.has_value())
      return std::nullopt;
    result = entry.entry.item_index;
  }
  return result;
}

std::map<std::string, std::size_t> preload_index(
    const std::vector<PreloadReport>& preloads, bool& unique) {
  std::map<std::string, std::size_t> result;
  unique = true;
  for (std::size_t index = 0; index < preloads.size(); ++index) {
    if (!result.emplace(preloads.at(index).register_name, index).second)
      unique = false;
  }
  return result;
}

bool is_literal_runtime_preload(const PreloadReport& preload) {
  // Generated setup owns the delivered value when any expression/target
  // metadata is present.  Updating only this report would not rewrite or prove
  // that setup program, so the post-layout pass accepts plain literals only.
  return !preload.setup_expression && !preload.setup_expression_text.has_value() &&
         !preload.setup_source_line.has_value() && !preload.setup_target_name.has_value();
}

bool has_exact_plain_decimal_address_projection(std::string_view value) {
  // The emulator/hardware stores only the first eight mantissa digits.  A raw
  // decimal decoder is authoritative only while every integer digit survives
  // that delivery step.  Exotic/negative/exponent/hex spellings remain useful
  // to programs, but need a separate canonical machine-value proof before a
  // layout pass may trust them as addresses.
  const std::size_t dot = value.find('.');
  if (dot != std::string_view::npos && value.find('.', dot + 1U) != std::string_view::npos)
    return false;
  const std::string_view integer = value.substr(0, dot);
  const std::string_view fraction =
      dot == std::string_view::npos ? std::string_view{} : value.substr(dot + 1U);
  const auto decimal = [](std::string_view digits) {
    return std::all_of(digits.begin(), digits.end(),
                       [](char ch) { return ch >= '0' && ch <= '9'; });
  };
  return !integer.empty() && integer.size() <= 8U && decimal(integer) &&
         (integer.size() == 1U || integer.front() != '0') &&
         (dot == std::string_view::npos || (!fraction.empty() && decimal(fraction)));
}

bool register_is_written(const std::vector<MachineItem>& items,
                         const AuthoritativePostLayoutControlFlow& flow,
                         int register_index_value) {
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind != MachineItemKind::Op)
      continue;
    if (item.opcode >= 0x40 && item.opcode <= 0x4e &&
        item.opcode - 0x40 == register_index_value) {
      return true;
    }
    if (item.opcode == 0x58 && register_index_value == 2)
      return true;
    if (item.opcode == 0x5a && register_index_value == 3)
      return true;
    if (item.opcode == 0x5b && register_index_value == 1)
      return true;
    if (item.opcode == 0x5d && register_index_value == 0)
      return true;
    if (is_indirect_memory(item.opcode) && (item.opcode & 0xf0) == 0xb0) {
      const auto targets = flow.indirect_memory_targets.find(item_index);
      if (targets == flow.indirect_memory_targets.end() ||
          std::find(targets->second.begin(), targets->second.end(), register_index_value) !=
              targets->second.end()) {
        return true;
      }
    }
  }
  return false;
}

std::vector<std::size_t> indirect_flow_uses(const std::vector<MachineItem>& items,
                                            int register_index_value) {
  std::vector<std::size_t> result;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind == MachineItemKind::Op && is_indirect_flow(item.opcode) &&
        encoded_register(item.opcode) == register_index_value) {
      result.push_back(item_index);
    }
  }
  return result;
}

std::vector<SelectorCandidate> selector_candidates(
    const std::vector<MachineItem>& items, const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& flow,
    const NaturalTargetComponentLayoutOptions& options) {
  bool unique = false;
  const std::map<std::string, std::size_t> preload_by_register =
      preload_index(preloads, unique);
  if (!unique)
    return {};

  std::vector<SelectorCandidate> result;
  for (int index = 7; index <= 0x0e; ++index) {
    if (register_is_written(items, flow, index))
      continue;
    const std::string name = register_name(index);
    const auto preload = preload_by_register.find(name);
    if (preload != preload_by_register.end()) {
      const PreloadReport& report = preloads.at(preload->second);
      if (!is_literal_runtime_preload(report))
        continue;
      const std::string& value = report.value;
      if (!has_exact_plain_decimal_address_projection(value))
        continue;
      std::optional<IndirectAddressEvaluation> evaluated;
      try {
        evaluated = evaluate_indirect_address(
            name, value, IndirectOperationKind::Flow, options.address_space_model);
      } catch (const std::exception&) {
        continue;
      }
      if (evaluated.has_value() && evaluated->actual_flow_target.has_value()) {
        result.push_back(SelectorCandidate{
            .register_index = index,
            .register_name = name,
            .origin = NaturalTargetSelectorOrigin::ExistingPreload,
            .value = value,
            .fixed_target = *evaluated->actual_flow_target,
        });
      }
    }

  }
  return result;
}

bool selector_existing_uses_match_target(
    const SelectorCandidate& selector, std::size_t target_origin,
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow) {
  for (const std::size_t use : indirect_flow_uses(items, selector.register_index)) {
    const auto targets = flow.indirect_flow_targets.find(use);
    if (targets == flow.indirect_flow_targets.end() || targets->second.size() != 1U ||
        targets->second.front().item_index != target_origin) {
      return false;
    }
  }
  return true;
}

std::optional<std::vector<Cell>> convert_direct_calls(
    const std::vector<MachineItem>& items, const std::vector<DirectCallSite>& calls,
    std::size_t target_origin, const SelectorCandidate& selector,
    std::vector<NaturalTargetCallRewrite>& rewrites) {
  const std::optional<std::vector<Cell>> original_cells = make_cells(items);
  if (!original_cells.has_value())
    return std::nullopt;

  std::set<std::size_t> converted_commands;
  std::set<std::size_t> removed_operands;
  for (const DirectCallSite& call : calls) {
    if (call.target != target_origin)
      continue;
    converted_commands.insert(call.command);
    removed_operands.insert(call.operand);
    rewrites.push_back(NaturalTargetCallRewrite{
        .original_call_item = call.command,
        .original_target_item = call.target,
        .selector_register = selector.register_name,
    });
  }
  if (converted_commands.empty())
    return std::nullopt;

  std::vector<Cell> cells;
  cells.reserve(original_cells->size() - removed_operands.size());
  for (Cell cell : *original_cells) {
    if (removed_operands.contains(cell.value.origin)) {
      if (!cell.labels.empty())
        return std::nullopt;
      continue;
    }
    if (converted_commands.contains(cell.value.origin)) {
      MachineItem& call = cell.value.item;
      if (call.kind != MachineItemKind::Op || call.opcode != kCallOpcode)
        return std::nullopt;
      call.opcode = 0xa0 + selector.register_index;
      call.mnemonic = opcode_by_code(call.opcode).name;
      call.indirect_flow_targets = std::vector<IrTarget>{static_cast<int>(target_origin)};
    }
    cells.push_back(std::move(cell));
  }
  return cells;
}

std::optional<std::vector<std::size_t>> subset_with_sum(
    const std::vector<Segment>& segments, const std::vector<std::size_t>& eligible,
    int required, std::size_t maximum_states) {
  if (required < 0)
    return std::nullopt;
  std::map<int, std::vector<std::size_t>> states;
  states.emplace(0, std::vector<std::size_t>{});
  for (const std::size_t segment : eligible) {
    const int length = segment_cells(segments.at(segment));
    std::vector<std::pair<int, std::vector<std::size_t>>> additions;
    for (const auto& [sum, selected] : states) {
      const int next = sum + length;
      if (next > required || states.contains(next))
        continue;
      std::vector<std::size_t> extended = selected;
      extended.push_back(segment);
      additions.emplace_back(next, std::move(extended));
    }
    for (auto& [sum, selected] : additions)
      states.emplace(sum, std::move(selected));
    if (states.size() > maximum_states)
      return std::nullopt;
  }
  const auto found = states.find(required);
  return found == states.end() ? std::nullopt
                               : std::optional<std::vector<std::size_t>>(found->second);
}

std::optional<std::vector<std::size_t>> layout_order_for_target(
    const std::vector<Segment>& segments, std::size_t main_segment,
    std::size_t target_segment, int target_offset, int natural_target,
    std::size_t maximum_states) {
  if (main_segment == target_segment || target_offset < 0 || natural_target < target_offset)
    return std::nullopt;
  if (main_segment >= segments.size() || target_segment >= segments.size())
    return std::nullopt;

  const int target_base = natural_target - target_offset;
  const int required_between = target_base - segment_cells(segments.at(main_segment));
  std::vector<std::size_t> eligible;
  for (std::size_t index = 0; index < segments.size(); ++index) {
    if (index != main_segment && index != target_segment)
      eligible.push_back(index);
  }
  const std::optional<std::vector<std::size_t>> selected =
      subset_with_sum(segments, eligible, required_between, maximum_states);
  if (!selected.has_value())
    return std::nullopt;

  const std::set<std::size_t> before(selected->begin(), selected->end());
  std::vector<std::size_t> order;
  order.reserve(segments.size());
  order.push_back(main_segment);
  for (const std::size_t index : eligible) {
    if (before.contains(index))
      order.push_back(index);
  }
  order.push_back(target_segment);
  for (const std::size_t index : eligible) {
    if (!before.contains(index))
      order.push_back(index);
  }
  return order;
}

std::vector<MachineItem> flatten_segments(
    const std::vector<Segment>& segments, const std::vector<std::size_t>& order,
    std::map<std::size_t, std::size_t>& new_item_by_origin) {
  std::vector<MachineItem> result;
  for (const std::size_t segment_index : order) {
    for (const Cell& cell : segments.at(segment_index).cells) {
      for (const OwnedItem& label : cell.labels) {
        new_item_by_origin.emplace(label.origin, result.size());
        result.push_back(label.item);
      }
      new_item_by_origin.emplace(cell.value.origin, result.size());
      result.push_back(cell.value.item);
    }
  }
  return result;
}

std::map<std::size_t, int> origin_addresses(
    const std::vector<MachineItem>& items,
    const std::map<std::size_t, std::size_t>& new_item_by_origin) {
  const ArtifactIndex index = index_artifact(items);
  std::map<std::size_t, int> result;
  for (const auto& [origin, item_index] : new_item_by_origin) {
    if (item_index < index.item_addresses.size())
      result.emplace(origin, index.item_addresses.at(item_index));
  }
  return result;
}

bool retarget_direct_references(
    std::vector<MachineItem>& items, const std::vector<DirectReference>& references,
    const std::set<std::size_t>& removed_operands,
    const std::map<std::size_t, std::size_t>& new_item_by_origin,
    const std::map<std::size_t, int>& new_address_by_origin) {
  for (const DirectReference& reference : references) {
    if (removed_operands.contains(reference.operand))
      continue;
    const auto operand = new_item_by_origin.find(reference.operand);
    const auto target = new_address_by_origin.find(reference.target);
    if (operand == new_item_by_origin.end() || target == new_address_by_origin.end())
      return false;
    MachineItem& address = items.at(operand->second);
    if (address.kind != MachineItemKind::Address)
      return false;
    address.target = target->second;
    address.formal_opcode.reset();
  }
  return true;
}

bool retarget_indirect_facts(
    std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& original_flow,
    const std::map<std::size_t, std::size_t>& new_item_by_origin,
    const std::map<std::size_t, int>& new_address_by_origin,
    const std::vector<NaturalTargetCallRewrite>& converted_calls) {
  for (const auto& [flow_origin, targets] : original_flow.indirect_flow_targets) {
    const auto command = new_item_by_origin.find(flow_origin);
    if (command == new_item_by_origin.end())
      return false;
    std::vector<IrTarget> rebound;
    rebound.reserve(targets.size());
    for (const PostLayoutCommandIdentity& target : targets) {
      const auto address = new_address_by_origin.find(target.item_index);
      if (address == new_address_by_origin.end())
        return false;
      rebound.emplace_back(address->second);
    }
    items.at(command->second).indirect_flow_targets = std::move(rebound);
  }
  for (const NaturalTargetCallRewrite& rewrite : converted_calls) {
    const auto command = new_item_by_origin.find(rewrite.original_call_item);
    const auto target = new_address_by_origin.find(rewrite.original_target_item);
    if (command == new_item_by_origin.end() || target == new_address_by_origin.end())
      return false;
    items.at(command->second).indirect_flow_targets =
        std::vector<IrTarget>{target->second};
  }
  return true;
}

bool decimal_digits(std::string_view text) {
  return !text.empty() && std::all_of(text.begin(), text.end(), [](char ch) {
           return ch >= '0' && ch <= '9';
         });
}

std::optional<std::string> rebind_stable_decimal_value(std::string_view value,
                                                       int target) {
  if (target < 0)
    return std::nullopt;
  const std::size_t dot = value.find('.');
  const std::string_view integer = value.substr(0, dot);
  if (!decimal_digits(integer))
    return std::nullopt;
  std::string suffix;
  if (dot != std::string_view::npos) {
    const std::string_view fraction = value.substr(dot + 1U);
    if (!decimal_digits(fraction))
      return std::nullopt;
    suffix = "." + std::string(fraction);
  }
  return std::to_string(target) + suffix;
}

std::optional<std::size_t> canonical_decimal_integer_width(std::string_view value) {
  const std::size_t dot = value.find('.');
  const std::string_view integer = value.substr(0, dot);
  if (!decimal_digits(integer) || (integer.size() > 1U && integer.front() == '0'))
    return std::nullopt;
  return integer.size();
}

bool fractional_projection_survives_rebind(std::string_view old_value,
                                           std::string_view new_value) {
  const std::size_t old_dot = old_value.find('.');
  const std::size_t new_dot = new_value.find('.');
  if (old_dot == std::string_view::npos || new_dot == std::string_view::npos)
    return old_dot == new_dot;
  const std::optional<std::size_t> old_width =
      canonical_decimal_integer_width(old_value);
  const std::optional<std::size_t> new_width =
      canonical_decimal_integer_width(new_value);
  if (!old_width.has_value() || !new_width.has_value() || *old_width != *new_width)
    return false;
  // MK-61 retains a fixed-width mantissa.  Equal canonical integer widths and
  // an identical textual suffix therefore retain, truncate, and round exactly
  // the same fractional digits.  A width change must fail closed: merely
  // copying the suffix can otherwise change К{x} for long decimal literals.
  return old_value.substr(old_dot) == new_value.substr(new_dot);
}

bool direct_recall_of(const MachineItem& item, int register_index_value) {
  if (item.kind != MachineItemKind::Op)
    return false;
  if (register_index_value == 0 && item.opcode == 0x6f)
    return true;
  return item.opcode == 0x60 + register_index_value;
}

struct NonFlowUseProjection {
  bool proved = false;
  bool has_fractional_recall = false;
};

NonFlowUseProjection prove_fractional_nonflow_projection(
    const std::vector<MachineItem>& items, int register_index_value) {
  NonFlowUseProjection result{.proved = true};
  const OpcodeInfo& fractional = opcode_by_code(kFractionalPartOpcode);
  if (fractional.stack_effect != StackEffect::Preserves ||
      fractional.x2_effect != X2Effect::Preserves) {
    result.proved = false;
    return result;
  }
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind != MachineItemKind::Op)
      continue;
    if (is_indirect_flow(item.opcode) && encoded_register(item.opcode) == register_index_value)
      continue;
    if (is_indirect_memory(item.opcode) && encoded_register(item.opcode) == register_index_value) {
      result.proved = false;
      return result;
    }
    if (!direct_recall_of(item, register_index_value))
      continue;
    const std::optional<std::size_t> next = next_cell_item(items, item_index);
    if (!next.has_value() || items.at(*next).kind != MachineItemKind::Op ||
        items.at(*next).opcode != kFractionalPartOpcode ||
        items.at(*next).manual_interaction.has_value()) {
      result.proved = false;
      return result;
    }
    const OpcodeInfo& recall = opcode_by_code(item.opcode);
    if (recall.stack_effect != StackEffect::Shifts ||
        recall.x2_effect != X2Effect::Affects) {
      result.proved = false;
      return result;
    }
    result.has_fractional_recall = true;
  }
  return result;
}

std::optional<std::size_t> target_origin_for_flow_use(
    const AuthoritativePostLayoutControlFlow& flow, std::size_t use) {
  const auto targets = flow.indirect_flow_targets.find(use);
  if (targets == flow.indirect_flow_targets.end() || targets->second.size() != 1U)
    return std::nullopt;
  return targets->second.front().item_index;
}

bool preload_value_targets(const std::string& register_name_value,
                           const std::string& value, int target,
                           AddressSpaceModel model) {
  if (!has_exact_plain_decimal_address_projection(value))
    return false;
  try {
    const std::optional<IndirectAddressEvaluation> evaluated = evaluate_indirect_address(
        register_name_value, value, IndirectOperationKind::Flow, model);
    return evaluated.has_value() && evaluated->actual_flow_target == target;
  } catch (const std::exception&) {
    return false;
  }
}

bool rebind_preloads(
    const std::vector<MachineItem>& original_items,
    const AuthoritativePostLayoutControlFlow& original_flow,
    const std::map<std::size_t, int>& original_address_by_origin,
    const std::map<std::size_t, int>& new_address_by_origin,
    const SelectorCandidate& candidate,
    const std::vector<NaturalTargetCallRewrite>& calls,
    AddressSpaceModel model, std::vector<PreloadReport>& preloads,
    std::vector<NaturalTargetPreloadRewrite>& rewrites) {
  bool unique = false;
  std::map<std::string, std::size_t> preload_by_register =
      preload_index(preloads, unique);
  if (!unique)
    return false;

  std::map<int, std::set<std::size_t>> required_targets;
  for (std::size_t item_index = 0; item_index < original_items.size(); ++item_index) {
    const MachineItem& item = original_items.at(item_index);
    if (item.kind != MachineItemKind::Op || !is_indirect_flow(item.opcode))
      continue;
    const int reg = encoded_register(item.opcode);
    const std::optional<std::size_t> target =
        target_origin_for_flow_use(original_flow, item_index);
    if (!target.has_value())
      return false;
    required_targets[reg].insert(*target);
  }
  for (const NaturalTargetCallRewrite& call : calls)
    required_targets[candidate.register_index].insert(call.original_target_item);

  for (const auto& [reg, target_origins] : required_targets) {
    if (reg < 7 || reg > 0x0e || target_origins.size() != 1U)
      return false;
    if (register_is_written(original_items, original_flow, reg))
      return false;
    const std::string name = register_name(reg);
    const auto preload = preload_by_register.find(name);
    if (preload == preload_by_register.end())
      return false;
    if (!is_literal_runtime_preload(preloads.at(preload->second)))
      return false;
    const std::size_t target_origin = *target_origins.begin();
    const auto old_target = original_address_by_origin.find(target_origin);
    const auto new_target = new_address_by_origin.find(target_origin);
    if (old_target == original_address_by_origin.end() ||
        new_target == new_address_by_origin.end()) {
      return false;
    }
    const std::string old_value = preloads.at(preload->second).value;

    // Existing typed facts must describe the exact delivered selector before
    // relocation; otherwise a preload report cannot be elevated into a proof.
    const bool had_original_flow_use =
        !indirect_flow_uses(original_items, reg).empty();
    if (had_original_flow_use) {
      if (!preload_value_targets(name, old_value, old_target->second, model))
        return false;
    }
    if (preload_value_targets(name, old_value, new_target->second, model))
      continue;

    const NonFlowUseProjection projection =
        prove_fractional_nonflow_projection(original_items, reg);
    if (!projection.proved)
      return false;
    const std::optional<std::string> rebound =
        rebind_stable_decimal_value(old_value, new_target->second);
    if (!rebound.has_value() || !preload_value_targets(name, *rebound, new_target->second, model))
      return false;
    if (projection.has_fractional_recall &&
        !fractional_projection_survives_rebind(old_value, *rebound)) {
      return false;
    }
    preloads.at(preload->second).value = *rebound;
    rewrites.push_back(NaturalTargetPreloadRewrite{
        .register_name = name,
        .old_value = old_value,
        .new_value = *rebound,
        .fractional_projection_only = projection.has_fractional_recall,
    });
  }
  return true;
}

int count_moved_segments(const std::vector<std::size_t>& order) {
  int moved = 0;
  for (std::size_t position = 0; position < order.size(); ++position) {
    if (order.at(position) != position)
      ++moved;
  }
  return moved;
}

std::optional<int> sequential_address(const ArtifactIndex& index, int after,
                                      AddressSpaceModel model) {
  const int next = after + 1;
  if (next < index.cells)
    return next;
  if (index.cells == official_program_step_limit(model) &&
      after == index.cells - 1) {
    return 0;
  }
  return std::nullopt;
}

std::optional<std::size_t> origin_for_item(
    const std::map<std::size_t, std::size_t>& origin_by_item,
    std::size_t item_index) {
  const auto found = origin_by_item.find(item_index);
  return found == origin_by_item.end() ? std::nullopt
                                       : std::optional<std::size_t>(found->second);
}

std::optional<TraceGraph> build_trace_graph(
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::map<std::size_t, std::size_t>& origin_by_item,
    AddressSpaceModel model, int maximum_states) {
  if (!flow.proved || maximum_states <= 0)
    return std::nullopt;
  const ArtifactIndex index = index_artifact(items);
  const std::optional<std::vector<DirectReference>> direct =
      collect_direct_references(items, index, model);
  if (!direct.has_value())
    return std::nullopt;
  std::map<std::size_t, DirectReference> direct_by_command;
  for (const DirectReference& reference : *direct)
    direct_by_command.emplace(reference.command, reference);

  std::deque<TraceState> pending;
  for (const PostLayoutExternalEntryState& entry : flow.external_entries) {
    const std::optional<std::size_t> command =
        origin_for_item(origin_by_item, entry.entry.item_index);
    if (!command.has_value())
      return std::nullopt;
    TraceState state{.command = *command};
    for (const PostLayoutCommandIdentity& slot : entry.return_stack) {
      const std::optional<std::size_t> origin =
          origin_for_item(origin_by_item, slot.item_index);
      if (!origin.has_value())
        return std::nullopt;
      state.returns.push_back(*origin);
    }
    pending.push_back(std::move(state));
  }

  const auto command_item = [&](std::size_t origin) -> std::optional<std::size_t> {
    for (const auto& [item_index, item_origin] : origin_by_item) {
      if (item_origin == origin)
        return item_index;
    }
    return std::nullopt;
  };
  const auto origin_at_address = [&](int address) -> std::optional<std::size_t> {
    const std::optional<std::size_t> item = command_at_address(items, index, address);
    return item.has_value() ? origin_for_item(origin_by_item, *item) : std::nullopt;
  };

  TraceGraph graph;
  while (!pending.empty()) {
    TraceState state = std::move(pending.front());
    pending.pop_front();
    if (graph.contains(state))
      continue;
    if (graph.size() >= static_cast<std::size_t>(maximum_states))
      return std::nullopt;
    const std::optional<std::size_t> item_index = command_item(state.command);
    if (!item_index.has_value() || items.at(*item_index).kind != MachineItemKind::Op)
      return std::nullopt;
    const MachineItem& item = items.at(*item_index);
    const int address = index.item_addresses.at(*item_index);
    std::vector<TraceEdge> edges;

    const auto enqueue = [&](TraceEdgeKind kind, std::size_t command,
                             std::vector<std::size_t> returns) {
      TraceEdge edge{
          .kind = kind,
          .target = TraceState{.command = command, .returns = std::move(returns)},
      };
      pending.push_back(edge.target);
      edges.push_back(std::move(edge));
    };
    const auto sequential_origin = [&](int after_address) -> std::optional<std::size_t> {
      const std::optional<int> next = sequential_address(index, after_address, model);
      return next.has_value() ? origin_at_address(*next) : std::nullopt;
    };

    if (item.opcode == kStopOpcode) {
      // Every hardware resume is represented as a separately typed external
      // entry, including manual multi-phase protocols.
    } else if (item.opcode == kReturnOpcode) {
      if (state.returns.empty()) {
        if (!flow.empty_return_target.has_value())
          return std::nullopt;
        const std::optional<std::size_t> target =
            origin_for_item(origin_by_item, flow.empty_return_target->item_index);
        if (!target.has_value())
          return std::nullopt;
        enqueue(TraceEdgeKind::Next, *target, state.returns);
      } else {
        std::vector<std::size_t> returns = state.returns;
        const std::size_t target = returns.back();
        returns.pop_back();
        enqueue(TraceEdgeKind::Next, target, std::move(returns));
      }
    } else if (takes_address(item)) {
      const auto direct_it = direct_by_command.find(*item_index);
      if (direct_it == direct_by_command.end())
        return std::nullopt;
      const std::optional<std::size_t> target =
          origin_for_item(origin_by_item, direct_it->second.target);
      const int operand_address = index.item_addresses.at(direct_it->second.operand);
      const std::optional<std::size_t> fallthrough = sequential_origin(operand_address);
      if (!target.has_value())
        return std::nullopt;
      if (item.opcode == kJumpOpcode) {
        enqueue(TraceEdgeKind::Taken, *target, state.returns);
      } else if (item.opcode == kCallOpcode) {
        if (!fallthrough.has_value())
          return std::nullopt;
        std::vector<std::size_t> returns = state.returns;
        returns.push_back(*fallthrough);
        enqueue(TraceEdgeKind::Call, *target, std::move(returns));
      } else if (is_direct_conditional(item.opcode)) {
        if (!fallthrough.has_value())
          return std::nullopt;
        enqueue(TraceEdgeKind::Taken, *target, state.returns);
        enqueue(TraceEdgeKind::Fallthrough, *fallthrough, state.returns);
      } else {
        return std::nullopt;
      }
    } else if (is_indirect_flow(item.opcode)) {
      const auto targets = flow.indirect_flow_targets.find(*item_index);
      if (targets == flow.indirect_flow_targets.end() || targets->second.empty())
        return std::nullopt;
      std::vector<std::size_t> call_returns = state.returns;
      if (is_indirect_call(item.opcode)) {
        const std::optional<std::size_t> continuation = sequential_origin(address);
        if (!continuation.has_value())
          return std::nullopt;
        call_returns.push_back(*continuation);
      }
      for (const PostLayoutCommandIdentity& identity : targets->second) {
        const std::optional<std::size_t> target =
            origin_for_item(origin_by_item, identity.item_index);
        if (!target.has_value())
          return std::nullopt;
        enqueue(is_indirect_call(item.opcode) ? TraceEdgeKind::Call : TraceEdgeKind::Taken,
                *target, call_returns);
      }
      if (is_indirect_conditional(item.opcode)) {
        const std::optional<std::size_t> fallthrough = sequential_origin(address);
        if (!fallthrough.has_value())
          return std::nullopt;
        enqueue(TraceEdgeKind::Fallthrough, *fallthrough, state.returns);
      }
    } else {
      const std::optional<std::size_t> next = sequential_origin(address);
      if (!next.has_value())
        return std::nullopt;
      enqueue(TraceEdgeKind::Next, *next, state.returns);
    }
    std::sort(edges.begin(), edges.end());
    graph.emplace(std::move(state), std::move(edges));
  }
  return graph;
}

using ExternalIdentity =
    std::tuple<std::size_t, std::vector<std::size_t>, int, bool, int, int, int>;

std::optional<std::vector<ExternalIdentity>> external_identities(
    const AuthoritativePostLayoutControlFlow& flow,
    const std::map<std::size_t, std::size_t>& origin_by_item) {
  std::vector<ExternalIdentity> result;
  for (const PostLayoutExternalEntryState& entry : flow.external_entries) {
    const std::optional<std::size_t> command =
        origin_for_item(origin_by_item, entry.entry.item_index);
    if (!command.has_value())
      return std::nullopt;
    std::vector<std::size_t> returns;
    for (const PostLayoutCommandIdentity& slot : entry.return_stack) {
      const std::optional<std::size_t> origin =
          origin_for_item(origin_by_item, slot.item_index);
      if (!origin.has_value())
        return std::nullopt;
      returns.push_back(*origin);
    }
    const bool has_manual = entry.manual_interaction.has_value();
    const int protocol = has_manual ? entry.manual_interaction->protocol_id : -1;
    const int phase = has_manual ? entry.manual_interaction->phase : -1;
    const int manual_kind = has_manual
                                ? static_cast<int>(entry.manual_interaction->kind)
                                : -1;
    result.emplace_back(*command, std::move(returns), static_cast<int>(entry.kind),
                        has_manual, protocol, phase, manual_kind);
  }
  std::sort(result.begin(), result.end());
  return result;
}

bool indirect_memory_facts_equivalent(
    const AuthoritativePostLayoutControlFlow& original,
    const AuthoritativePostLayoutControlFlow& rewritten,
    const std::map<std::size_t, std::size_t>& rewritten_origin_by_item) {
  std::map<std::size_t, std::vector<int>> rebound;
  for (const auto& [item_index, targets] : rewritten.indirect_memory_targets) {
    const std::optional<std::size_t> origin =
        origin_for_item(rewritten_origin_by_item, item_index);
    if (!origin.has_value())
      return false;
    rebound.emplace(*origin, targets);
  }
  return rebound == original.indirect_memory_targets;
}

bool converted_call_effects_equivalent(const std::vector<MachineItem>& original_items,
                                       const SelectorCandidate& selector,
                                       const std::vector<NaturalTargetCallRewrite>& calls) {
  if (indirect_selector_mutation(selector.register_name) !=
      IndirectSelectorMutation::Stable) {
    return false;
  }
  const OpcodeInfo& direct = opcode_by_code(kCallOpcode);
  const OpcodeInfo& indirect = opcode_by_code(0xa0 + selector.register_index);
  if (direct.stack_effect != indirect.stack_effect ||
      direct.x2_effect != indirect.x2_effect || direct.takes_address == indirect.takes_address)
    return false;
  return std::all_of(calls.begin(), calls.end(), [&](const NaturalTargetCallRewrite& call) {
    return call.original_call_item < original_items.size() &&
           original_items.at(call.original_call_item).kind == MachineItemKind::Op &&
           original_items.at(call.original_call_item).opcode == kCallOpcode;
  });
}

std::optional<std::vector<NaturalTargetRuntimeSelectorProof>> prove_runtime_selectors(
    const std::vector<MachineItem>& original_items,
    const AuthoritativePostLayoutControlFlow& original_flow,
    const std::vector<MachineItem>& final_items,
    const AuthoritativePostLayoutControlFlow& final_flow,
    const std::map<std::size_t, std::size_t>& final_origin_by_item,
    const std::vector<PreloadReport>& preloads, AddressSpaceModel model) {
  bool unique = false;
  const std::map<std::string, std::size_t> preload_by_register =
      preload_index(preloads, unique);
  if (!unique)
    return std::nullopt;

  std::vector<NaturalTargetRuntimeSelectorProof> result;
  for (const auto& [final_command, targets] : final_flow.indirect_flow_targets) {
    if (final_command >= final_items.size() || targets.size() != 1U)
      return std::nullopt;
    const MachineItem& command = final_items.at(final_command);
    if (command.kind != MachineItemKind::Op || !is_indirect_flow(command.opcode))
      return std::nullopt;
    const std::optional<std::size_t> command_origin =
        origin_for_item(final_origin_by_item, final_command);
    const std::optional<std::size_t> target_origin =
        origin_for_item(final_origin_by_item, targets.front().item_index);
    if (!command_origin.has_value() || !target_origin.has_value())
      return std::nullopt;
    const int reg = encoded_register(command.opcode);
    const std::string name = register_name(reg);
    const auto preload = preload_by_register.find(name);
    if (name.empty() || preload == preload_by_register.end())
      return std::nullopt;
    if (!is_literal_runtime_preload(preloads.at(preload->second)))
      return std::nullopt;
    const bool stable = indirect_selector_mutation(name) ==
                        IndirectSelectorMutation::Stable;
    const bool unwritten = !register_is_written(original_items, original_flow, reg);
    if (!stable || !unwritten)
      return std::nullopt;
    const std::string& value = preloads.at(preload->second).value;
    if (!has_exact_plain_decimal_address_projection(value))
      return std::nullopt;
    std::optional<IndirectAddressEvaluation> decoded;
    try {
      decoded = evaluate_indirect_address(name, value, IndirectOperationKind::Flow, model);
    } catch (const std::exception&) {
      return std::nullopt;
    }
    if (!decoded.has_value() || !decoded->actual_flow_target.has_value() ||
        *decoded->actual_flow_target != targets.front().address)
      return std::nullopt;

    // Existing indirect commands must retain the same target identity.  A
    // newly converted direct call has no original indirect-map entry and is
    // checked by the direct-call identity trace instead.
    const auto original_targets = original_flow.indirect_flow_targets.find(*command_origin);
    if (original_targets != original_flow.indirect_flow_targets.end()) {
      if (original_targets->second.size() != 1U ||
          original_targets->second.front().item_index != *target_origin)
        return std::nullopt;
    }
    result.push_back(NaturalTargetRuntimeSelectorProof{
        .original_command_item = *command_origin,
        .final_command_item = final_command,
        .original_target_item = *target_origin,
        .register_name = name,
        .delivered_preload = value,
        .decoded_target = *decoded->actual_flow_target,
        .final_target_address = targets.front().address,
        .stable_mutation_class = stable,
        .selector_unwritten = unwritten,
        .typed_target_matches_runtime_decode = true,
    });
  }
  std::sort(result.begin(), result.end(),
            [](const NaturalTargetRuntimeSelectorProof& left,
               const NaturalTargetRuntimeSelectorProof& right) {
              return std::tie(left.original_command_item, left.register_name) <
                     std::tie(right.original_command_item, right.register_name);
            });
  return result;
}

bool unchanged_command_effects_preserved(
    const std::vector<MachineItem>& original_items,
    const std::vector<MachineItem>& rewritten_items,
    const std::map<std::size_t, std::size_t>& new_item_by_origin,
    const std::vector<NaturalTargetCallRewrite>& calls) {
  std::set<std::size_t> converted;
  for (const NaturalTargetCallRewrite& call : calls)
    converted.insert(call.original_call_item);
  for (std::size_t origin = 0; origin < original_items.size(); ++origin) {
    const MachineItem& before = original_items.at(origin);
    if (before.kind != MachineItemKind::Op)
      continue;
    const auto found = new_item_by_origin.find(origin);
    if (found == new_item_by_origin.end())
      return false;
    const MachineItem& after = rewritten_items.at(found->second);
    if (after.kind != MachineItemKind::Op || before.stop_disposition != after.stop_disposition ||
        before.manual_interaction != after.manual_interaction)
      return false;
    if (!converted.contains(origin) && before.opcode != after.opcode)
      return false;
  }
  return true;
}

std::optional<CandidateArtifact> try_candidate(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control_flow,
    const NaturalTargetComponentLayoutOptions& options,
    const std::vector<DirectReference>& references,
    const std::vector<DirectCallSite>& calls,
    const SelectorCandidate& selector, std::size_t target_origin,
    const TraceGraph& original_trace,
    const std::vector<ExternalIdentity>& original_external) {
  std::vector<NaturalTargetCallRewrite> rewrites;
  const std::optional<std::vector<Cell>> converted = convert_direct_calls(
      items, calls, target_origin, selector, rewrites);
  if (!converted.has_value() || rewrites.empty())
    return std::nullopt;
  std::vector<Segment> segments = make_segments(*converted);

  const std::optional<std::size_t> main = main_origin(control_flow);
  if (!main.has_value())
    return std::nullopt;
  const ArtifactIndex original_index = index_artifact(items);
  if (*main >= original_index.item_addresses.size() ||
      original_index.item_addresses.at(*main) != 0)
    return std::nullopt;
  const std::optional<std::pair<std::size_t, int>> main_location =
      locate_origin(segments, *main);
  const std::optional<std::pair<std::size_t, int>> target_location =
      locate_origin(segments, target_origin);
  if (!main_location.has_value() || !target_location.has_value() ||
      main_location->second != 0)
    return std::nullopt;

  if (selector.origin != NaturalTargetSelectorOrigin::ExistingPreload)
    return std::nullopt;
  const int natural_target = selector.fixed_target;
  const std::optional<std::vector<std::size_t>> order = layout_order_for_target(
      segments, main_location->first, target_location->first,
      target_location->second, natural_target, options.maximum_subset_states);
  if (!order.has_value())
    return std::nullopt;

  CandidateArtifact candidate;
  candidate.items = flatten_segments(segments, *order, candidate.new_item_by_origin);
  const std::map<std::size_t, int> new_address_by_origin =
      origin_addresses(candidate.items, candidate.new_item_by_origin);
  const auto target_address = new_address_by_origin.find(target_origin);
  if (target_address == new_address_by_origin.end() || target_address->second != natural_target)
    return std::nullopt;
  for (NaturalTargetCallRewrite& rewrite : rewrites)
    rewrite.target_address = natural_target;

  std::set<std::size_t> removed_operands;
  for (const DirectCallSite& call : calls) {
    if (call.target == target_origin)
      removed_operands.insert(call.operand);
  }
  if (!retarget_direct_references(candidate.items, references, removed_operands,
                                  candidate.new_item_by_origin, new_address_by_origin) ||
      !retarget_indirect_facts(candidate.items, control_flow,
                               candidate.new_item_by_origin, new_address_by_origin,
                               rewrites)) {
    return std::nullopt;
  }

  std::map<std::size_t, int> original_address_by_origin;
  for (std::size_t origin = 0; origin < items.size(); ++origin)
    original_address_by_origin.emplace(origin, original_index.item_addresses.at(origin));
  candidate.preloads = preloads;
  std::vector<NaturalTargetPreloadRewrite> preload_rewrites;
  if (!rebind_preloads(items, control_flow, original_address_by_origin,
                       new_address_by_origin, selector, rewrites,
                       options.address_space_model, candidate.preloads,
                       preload_rewrites)) {
    return std::nullopt;
  }

  PostLayoutControlFlowOptions final_options;
  final_options.address_space_model = options.address_space_model;
  final_options.maximum_execution_states =
      static_cast<std::size_t>(options.maximum_execution_states);
  final_options.main_entry = 0;
  if (control_flow.empty_return_target.has_value()) {
    const auto rebound =
        new_address_by_origin.find(control_flow.empty_return_target->item_index);
    if (rebound == new_address_by_origin.end())
      return std::nullopt;
    final_options.empty_return_target = rebound->second;
  }
  const AuthoritativePostLayoutControlFlow final_flow =
      build_post_layout_control_flow(candidate.items, final_options);
  if (!final_flow.proved)
    return std::nullopt;

  std::map<std::size_t, std::size_t> original_origin_by_item;
  for (std::size_t index = 0; index < items.size(); ++index)
    original_origin_by_item.emplace(index, index);
  std::map<std::size_t, std::size_t> rewritten_origin_by_item;
  for (const auto& [origin, item_index] : candidate.new_item_by_origin)
    rewritten_origin_by_item.emplace(item_index, origin);
  const std::optional<TraceGraph> rewritten_trace = build_trace_graph(
      candidate.items, final_flow, rewritten_origin_by_item,
      options.address_space_model, options.maximum_execution_states);
  const std::optional<std::vector<ExternalIdentity>> rewritten_external =
      external_identities(final_flow, rewritten_origin_by_item);
  const std::optional<std::vector<NaturalTargetRuntimeSelectorProof>> runtime_selectors =
      prove_runtime_selectors(items, control_flow, candidate.items, final_flow,
                              rewritten_origin_by_item, candidate.preloads,
                              options.address_space_model);
  if (!rewritten_trace.has_value() || !rewritten_external.has_value() ||
      !runtime_selectors.has_value() || *rewritten_trace != original_trace ||
      *rewritten_external != original_external)
    return std::nullopt;

  NaturalTargetComponentLayoutPlan& plan = candidate.plan;
  plan.selector_origin = selector.origin;
  plan.selector_register = selector.register_name;
  plan.natural_target = natural_target;
  plan.input_cells = original_index.cells;
  plan.output_cells = index_artifact(candidate.items).cells;
  plan.removed_cells = plan.input_cells - plan.output_cells;
  plan.moved_segments = count_moved_segments(*order);
  plan.calls = std::move(rewrites);
  plan.preloads = std::move(preload_rewrites);
  plan.runtime_selectors = *runtime_selectors;
  plan.final_control_flow = final_flow;
  plan.control_flow_equivalent = true;
  plan.call_return_equivalent = true;
  plan.stack_and_x2_equivalent =
      converted_call_effects_equivalent(items, selector, plan.calls) &&
      unchanged_command_effects_preserved(items, candidate.items,
                                          candidate.new_item_by_origin, plan.calls);
  plan.indirect_memory_equivalent = indirect_memory_facts_equivalent(
      control_flow, final_flow, rewritten_origin_by_item);
  plan.data_projection_equivalent = true;
  plan.final_artifact_proved = final_flow.proved;
  plan.proved = plan.removed_cells == static_cast<int>(plan.calls.size()) &&
                plan.removed_cells > 0 && plan.control_flow_equivalent &&
                plan.call_return_equivalent && plan.stack_and_x2_equivalent &&
                plan.indirect_memory_equivalent && plan.data_projection_equivalent &&
                plan.final_artifact_proved;
  if (!plan.proved)
    return std::nullopt;
  return candidate;
}

bool better_candidate(const CandidateArtifact& left, const CandidateArtifact& right) {
  if (left.plan.removed_cells != right.plan.removed_cells)
    return left.plan.removed_cells > right.plan.removed_cells;
  if (left.plan.selector_origin != right.plan.selector_origin) {
    return left.plan.selector_origin == NaturalTargetSelectorOrigin::ExistingPreload;
  }
  return std::tie(left.plan.natural_target, left.plan.selector_register) <
         std::tie(right.plan.natural_target, right.plan.selector_register);
}

} // namespace

NaturalTargetComponentLayoutResult optimize_natural_target_component_layout(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control_flow,
    const NaturalTargetComponentLayoutOptions& options) {
  NaturalTargetComponentLayoutResult result{
      .items = items,
      .preloads = preloads,
  };
  result.plan.input_cells = index_artifact(items).cells;
  result.plan.output_cells = result.plan.input_cells;

  if (!control_flow.proved) {
    add_reason(result.plan, "input post-layout control flow is not authoritative");
    return result;
  }
  if (options.maximum_execution_states <= 0 || options.maximum_subset_states == 0U) {
    add_reason(result.plan, "proof search cap is not positive");
    return result;
  }
  const ArtifactIndex index = index_artifact(items);
  const std::optional<std::vector<DirectReference>> references =
      collect_direct_references(items, index, options.address_space_model);
  if (!references.has_value()) {
    add_reason(result.plan, "direct command identities cannot be resolved exactly");
    return result;
  }
  const std::vector<DirectCallSite> calls = direct_calls(*references, items);
  if (calls.empty()) {
    add_reason(result.plan, "artifact contains no direct call to shorten");
    return result;
  }

  std::map<std::size_t, std::size_t> original_origin_by_item;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index)
    original_origin_by_item.emplace(item_index, item_index);
  const std::optional<TraceGraph> original_trace = build_trace_graph(
      items, control_flow, original_origin_by_item, options.address_space_model,
      options.maximum_execution_states);
  const std::optional<std::vector<ExternalIdentity>> original_external =
      external_identities(control_flow, original_origin_by_item);
  if (!original_trace.has_value() || !original_external.has_value()) {
    add_reason(result.plan, "input identity trace or external-entry ledger is incomplete");
    return result;
  }

  const std::vector<SelectorCandidate> selectors = selector_candidates(
      items, preloads, control_flow, options);

  std::set<std::size_t> call_targets;
  for (const DirectCallSite& call : calls)
    call_targets.insert(call.target);
  std::optional<CandidateArtifact> best;
  for (const SelectorCandidate& selector : selectors) {
    for (const std::size_t target : call_targets) {
      if (!selector_existing_uses_match_target(selector, target, items, control_flow))
        continue;
      const std::optional<CandidateArtifact> candidate = try_candidate(
          items, preloads, control_flow, options, *references, calls, selector,
          target, *original_trace, *original_external);
      if (candidate.has_value() &&
          (!best.has_value() || better_candidate(*candidate, *best))) {
        best = candidate;
      }
    }
  }
  if (!best.has_value()) {
    add_reason(result.plan,
               "no stable selector and fallthrough-closed component layout passed every proof");
    return result;
  }
  result.items = std::move(best->items);
  result.preloads = std::move(best->preloads);
  result.plan = std::move(best->plan);
  result.applied = static_cast<int>(result.plan.calls.size());
  result.removed_cells = result.plan.removed_cells;
  return result;
}

} // namespace mkpro::core
