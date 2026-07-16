#include "mkpro/core/natural_target_component_layout.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <array>
#include <compare>
#include <deque>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <queue>
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
  bool rebindable_address = false;
  int displaced_flow_uses = 0;
};

struct NaturalTargetAnchorCandidate {
  SelectorCandidate selector;
  std::size_t target_origin = 0;
  int call_sites = 0;
  bool via_trampoline = false;
  int split_prefix_cells = 0;
};

struct TransparentTrampoline {
  int selector_register = -1;
  std::size_t command_origin = 0;
  std::size_t operand_origin = 0;
  std::size_t target_origin = 0;
};

struct TransparentSplitBridge {
  std::size_t command_origin = 0;
  std::size_t operand_origin = 0;
  std::size_t target_origin = 0;
};

struct NaturalTargetPlacement {
  std::size_t target_segment = 0;
  int target_offset = 0;
  int natural_target = -1;
};

struct NaturalTargetLayoutOrder {
  std::vector<std::size_t> segments;
  std::map<std::size_t, int> padding_before_segment;
  int padding_cells = 0;
};

struct DisplacedIndirectFlowRewrite {
  std::size_t command_origin = 0;
  std::size_t operand_origin = 0;
  std::size_t target_origin = 0;
  int direct_opcode = -1;
};

struct CandidateArtifact {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
  std::map<std::size_t, std::size_t> new_item_by_origin;
  NaturalTargetComponentLayoutPlan plan;
};

std::optional<std::pair<std::size_t, int>> locate_origin(
    const std::vector<Segment>& segments, std::size_t origin);

std::optional<std::size_t> physical_target_origin(
    const NaturalTargetAnchorCandidate& anchor,
    const std::vector<TransparentTrampoline>& trampolines) {
  if (!anchor.via_trampoline)
    return anchor.target_origin;
  const auto trampoline = std::find_if(
      trampolines.begin(), trampolines.end(), [&](const TransparentTrampoline& candidate) {
        return candidate.selector_register == anchor.selector.register_index &&
               candidate.target_origin == anchor.target_origin;
      });
  return trampoline == trampolines.end()
             ? std::nullopt
             : std::optional<std::size_t>(trampoline->command_origin);
}

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

std::optional<int> direct_opcode_for_indirect_flow(int opcode) {
  switch (opcode & 0xf0) {
  case 0x70:
    return 0x57;
  case 0x80:
    return kJumpOpcode;
  case 0x90:
    return 0x59;
  case 0xa0:
    return kCallOpcode;
  case 0xc0:
    return 0x5c;
  case 0xe0:
    return 0x5e;
  default:
    return std::nullopt;
  }
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
  const std::string* label = std::get_if<std::string>(&operand.target);
  if (label != nullptr) {
    // A resolved over-window label carries a wrapped formal opcode for listing
    // purposes. The label is the authoritative command identity; the wrapped
    // byte cannot denote its current physical address. Still fail closed when
    // that byte carries a genuine multi-command/extra formal contract.
    if (operand.formal_opcode.has_value()) {
      try {
        const FormalAddressInfo formal = formal_address_info(*operand.formal_opcode, model);
        if (formal.kind == FormalAddressKind::SuperDark || formal.one_command ||
            formal.extra.has_value()) {
          return std::nullopt;
        }
      } catch (const std::exception&) {
        return std::nullopt;
      }
    }
    const auto found = index.label_addresses.find(*label);
    return found == index.label_addresses.end() ? std::nullopt
                                                 : std::optional<int>(found->second);
  }
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
  return std::nullopt;
}

struct OverflowTargetNormalization {
  std::vector<MachineItem> items;
  bool changed = false;
};

std::optional<OverflowTargetNormalization> normalize_overflow_formals(
    const std::vector<MachineItem>& items, AddressSpaceModel model) {
  OverflowTargetNormalization normalized{.items = items};
  const ArtifactIndex index = index_artifact(items);
  const int official_last = official_program_last_address(model);
  for (MachineItem& item : normalized.items) {
    if (item.kind != MachineItemKind::Address || !item.formal_opcode.has_value())
      continue;
    std::optional<int> intended_target;
    if (const std::string* label = std::get_if<std::string>(&item.target)) {
      const auto target = index.label_addresses.find(*label);
      if (target == index.label_addresses.end())
        return std::nullopt;
      intended_target = target->second;
    } else if (const int* target = std::get_if<int>(&item.target)) {
      intended_target = *target;
    }
    if (!intended_target.has_value() || *intended_target <= official_last)
      continue;
    if (item.raw || !item.roles.empty())
      return std::nullopt;
    try {
      const FormalAddressInfo formal = formal_address_info(*item.formal_opcode, model);
      if (formal.kind == FormalAddressKind::SuperDark || formal.one_command ||
          formal.extra.has_value()) {
        return std::nullopt;
      }
    } catch (const std::exception&) {
      return std::nullopt;
    }
    // The byte is only the resolver's wrapped representation of an invalid
    // over-window address. Preserve the authoritative label identity for the
    // size-rescue proof; every surviving address is rebound after layout.
    item.formal_opcode.reset();
    normalized.changed = true;
  }
  return normalized;
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
    // Raw/role-bearing operands have an opaque secondary contract. Ordinary
    // formal operands are still admissible: direct_target_address below
    // decodes them and rejects super-dark, one-command, and extra-semantics
    // forms before this pass reasons about command identity. This matters for
    // over-window helpers that can become official only after component layout.
    if (items.at(*operand).raw || !items.at(*operand).roles.empty()) {
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

bool split_segment_with_bridge(
    std::vector<Segment>& segments, std::size_t segment_index,
    std::size_t prefix_cells, std::size_t command_origin,
    std::size_t operand_origin, TransparentSplitBridge& bridge) {
  if (segment_index >= segments.size() || prefix_cells == 0U ||
      prefix_cells >= segments.at(segment_index).cells.size() ||
      safe_cut_after(segments.at(segment_index).cells, prefix_cells - 1U)) {
    return false;
  }

  Segment suffix;
  suffix.ordinal = segments.size();
  Segment& prefix = segments.at(segment_index);
  while (prefix.cells.size() > prefix_cells) {
    suffix.cells.push_back(std::move(prefix.cells.at(prefix_cells)));
    prefix.cells.erase(prefix.cells.begin() +
                       static_cast<std::ptrdiff_t>(prefix_cells));
  }
  if (suffix.cells.empty())
    return false;
  bridge = TransparentSplitBridge{
      .command_origin = command_origin,
      .operand_origin = operand_origin,
      .target_origin = suffix.cells.front().value.origin,
  };
  prefix.cells.push_back(Cell{
      .value = OwnedItem{
          .item = MachineItem::op(kJumpOpcode, opcode_by_code(kJumpOpcode).name),
          .origin = bridge.command_origin,
      },
  });
  prefix.cells.push_back(Cell{
      .value = OwnedItem{
          .item = MachineItem::address(static_cast<int>(bridge.target_origin)),
          .origin = bridge.operand_origin,
      },
  });
  segments.push_back(std::move(suffix));
  return true;
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

bool apply_transparent_segment_splits(
    std::vector<Segment>& segments,
    const std::vector<NaturalTargetAnchorCandidate>& anchors,
    std::size_t& next_synthetic_origin,
    std::vector<TransparentSplitBridge>& bridges) {
  for (const NaturalTargetAnchorCandidate& anchor : anchors) {
    if (anchor.split_prefix_cells <= 0)
      continue;
    const std::optional<std::pair<std::size_t, int>> location =
        locate_origin(segments, anchor.target_origin);
    if (!location.has_value() || location->second != 0)
      return false;
    const std::size_t segment_index = location->first;
    const std::size_t prefix_cells =
        static_cast<std::size_t>(anchor.split_prefix_cells);
    for (const NaturalTargetAnchorCandidate& other : anchors) {
      if (other.target_origin == anchor.target_origin)
        continue;
      const auto other_location = locate_origin(segments, other.target_origin);
      if (other_location.has_value() && other_location->first == segment_index)
        return false;
    }

    TransparentSplitBridge bridge;
    if (!split_segment_with_bridge(
            segments, segment_index, prefix_cells, next_synthetic_origin,
            next_synthetic_origin + 1U, bridge)) {
      return false;
    }
    next_synthetic_origin += 2U;
    bridges.push_back(bridge);
  }
  return true;
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

struct NaturalFractionalSelectorEncoding {
  std::string delivered_value;
  int target = -1;
};

std::optional<NaturalFractionalSelectorEncoding>
natural_fractional_selector_encoding(std::string_view value) {
  if (!value.starts_with("0.") || value.size() <= 2U)
    return std::nullopt;
  const std::string_view fraction = value.substr(2U);
  if (!std::all_of(fraction.begin(), fraction.end(),
                   [](char ch) { return ch >= '0' && ch <= '9'; })) {
    return std::nullopt;
  }
  const auto first = std::find_if(fraction.begin(), fraction.end(),
                                  [](char ch) { return ch >= '1' && ch <= '9'; });
  if (first == fraction.end())
    return std::nullopt;
  const std::size_t leading_zeroes =
      static_cast<std::size_t>(std::distance(fraction.begin(), first));
  const std::string_view significant = fraction.substr(leading_zeroes);
  if (significant.size() > 8U &&
      std::any_of(significant.begin() + 8, significant.end(),
                  [](char ch) { return ch != '0'; })) {
    return std::nullopt;
  }
  std::string mantissa(significant.substr(0, std::min<std::size_t>(8U, significant.size())));
  mantissa.resize(8U, '0');
  const int target = std::stoi(mantissa.substr(6U, 2U));
  if (target <= 0)
    return std::nullopt;
  return NaturalFractionalSelectorEncoding{
      .delivered_value = mantissa.substr(0U, 1U) + "." + mantissa.substr(1U) + "E-" +
                         std::to_string(leading_zeroes + 1U),
      .target = target,
  };
}

bool is_canonical_natural_fractional_selector_encoding(std::string_view value) {
  if (value.size() < 12U || value.at(0) < '1' || value.at(0) > '9' ||
      value.at(1) != '.' || value.substr(9U, 2U) != "E-") {
    return false;
  }
  if (!std::all_of(value.begin() + 2, value.begin() + 9,
                   [](char ch) { return ch >= '0' && ch <= '9'; })) {
    return false;
  }
  const std::string_view exponent = value.substr(11U);
  return !exponent.empty() && exponent.front() >= '1' && exponent.front() <= '9' &&
         std::all_of(exponent.begin(), exponent.end(),
                     [](char ch) { return ch >= '0' && ch <= '9'; });
}

bool is_canonical_raw_address_selector(std::string_view value) {
  if (value.size() != 2U)
    return false;
  bool has_raw_digit = false;
  for (const char ch : value) {
    if (ch >= '0' && ch <= '9')
      continue;
    if (ch < 'A' || ch > 'F')
      return false;
    has_raw_digit = true;
  }
  return has_raw_digit;
}

bool has_proved_address_projection(std::string_view value) {
  return has_exact_plain_decimal_address_projection(value) ||
         is_canonical_natural_fractional_selector_encoding(value) ||
         is_canonical_raw_address_selector(value);
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

bool register_has_nonflow_use(const std::vector<MachineItem>& items,
                              int register_index_value) {
  return std::any_of(items.begin(), items.end(), [&](const MachineItem& item) {
    if (item.kind != MachineItemKind::Op)
      return false;
    if (item.opcode >= 0x40 && item.opcode <= 0x4e &&
        item.opcode - 0x40 == register_index_value) {
      return true;
    }
    if (item.opcode >= 0x60 && item.opcode <= 0x6e &&
        item.opcode - 0x60 == register_index_value) {
      return true;
    }
    return is_indirect_memory(item.opcode) &&
           encoded_register(item.opcode) == register_index_value;
  });
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
      const auto append = [&](const std::string& delivered_value) {
        if (!has_proved_address_projection(delivered_value))
          return;
        std::optional<IndirectAddressEvaluation> evaluated;
        try {
          evaluated = evaluate_indirect_address(
              name, delivered_value, IndirectOperationKind::Flow,
              options.address_space_model);
        } catch (const std::exception&) {
          return;
        }
        if (!evaluated.has_value() || !evaluated->actual_flow_target.has_value())
          return;
        const int target = *evaluated->actual_flow_target;
        if (std::any_of(result.begin(), result.end(), [&](const SelectorCandidate& candidate) {
              return candidate.register_index == index && candidate.fixed_target == target;
            })) {
          return;
        }
        result.push_back(SelectorCandidate{
            .register_index = index,
            .register_name = name,
            .origin = NaturalTargetSelectorOrigin::ExistingPreload,
            .value = delivered_value,
            .fixed_target = target,
        });
      };
      append(value);
      if (const auto natural = natural_fractional_selector_encoding(value))
        append(natural->delivered_value);

      const std::vector<std::size_t> uses = indirect_flow_uses(items, index);
      if (!uses.empty() && !register_has_nonflow_use(items, index) &&
          has_proved_address_projection(value)) {
        std::optional<IndirectAddressEvaluation> decoded;
        try {
          decoded = evaluate_indirect_address(name, value,
                                              IndirectOperationKind::Flow,
                                              options.address_space_model);
        } catch (const std::exception&) {
          decoded.reset();
        }
        const bool exact_existing_uses =
            decoded.has_value() && decoded->actual_flow_target.has_value() &&
            std::all_of(uses.begin(), uses.end(), [&](std::size_t use) {
              const auto targets = flow.indirect_flow_targets.find(use);
              return targets != flow.indirect_flow_targets.end() &&
                     targets->second.size() == 1U &&
                     targets->second.front().address ==
                         *decoded->actual_flow_target;
            });
        if (exact_existing_uses) {
          result.push_back(SelectorCandidate{
              .register_index = index,
              .register_name = name,
              .origin = NaturalTargetSelectorOrigin::ExistingPreload,
              .rebindable_address = true,
              .displaced_flow_uses = static_cast<int>(uses.size()),
          });
        }
      }
    }

  }
  return result;
}

bool selector_existing_uses_match_target(
    const SelectorCandidate& selector, std::size_t target_origin,
    const std::vector<MachineItem>& items,
    const AuthoritativePostLayoutControlFlow& flow) {
  if (selector.rebindable_address)
    return true;
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
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<NaturalTargetAnchorCandidate>& anchors,
    std::vector<NaturalTargetCallRewrite>& rewrites,
    std::vector<DisplacedIndirectFlowRewrite>& displaced_flows,
    std::vector<TransparentTrampoline>& trampolines) {
  const std::optional<std::vector<Cell>> original_cells = make_cells(items);
  if (!original_cells.has_value())
    return std::nullopt;

  std::map<std::size_t, const SelectorCandidate*> selector_by_target;
  std::set<int> selected_registers;
  for (const NaturalTargetAnchorCandidate& anchor : anchors) {
    if (!selector_by_target.emplace(anchor.target_origin, &anchor.selector).second ||
        !selected_registers.insert(anchor.selector.register_index).second) {
      return std::nullopt;
    }
  }

  std::set<std::size_t> converted_commands;
  std::set<std::size_t> removed_operands;
  for (const DirectCallSite& call : calls) {
    const auto selected = selector_by_target.find(call.target);
    if (selected == selector_by_target.end())
      continue;
    converted_commands.insert(call.command);
    removed_operands.insert(call.operand);
    rewrites.push_back(NaturalTargetCallRewrite{
        .original_call_item = call.command,
        .original_target_item = call.target,
        .selector_register = selected->second->register_name,
    });
  }
  if (converted_commands.empty() ||
      std::any_of(anchors.begin(), anchors.end(), [&](const auto& anchor) {
        return std::none_of(rewrites.begin(), rewrites.end(), [&](const auto& rewrite) {
          return rewrite.original_target_item == anchor.target_origin;
        });
      })) {
    return std::nullopt;
  }

  std::map<std::size_t, DisplacedIndirectFlowRewrite> displaced_by_command;
  std::size_t next_synthetic_origin = items.size();
  for (const NaturalTargetAnchorCandidate& anchor : anchors) {
    if (!anchor.selector.rebindable_address)
      continue;
    for (const std::size_t command :
         indirect_flow_uses(items, anchor.selector.register_index)) {
      const auto targets = flow.indirect_flow_targets.find(command);
      const std::optional<int> direct =
          command < items.size()
              ? direct_opcode_for_indirect_flow(items.at(command).opcode)
              : std::nullopt;
      if (targets == flow.indirect_flow_targets.end() ||
          targets->second.size() != 1U || !direct.has_value()) {
        return std::nullopt;
      }
      DisplacedIndirectFlowRewrite rewrite{
          .command_origin = command,
          .operand_origin = next_synthetic_origin++,
          .target_origin = targets->second.front().item_index,
          .direct_opcode = *direct,
      };
      if (!displaced_by_command.emplace(command, rewrite).second)
        return std::nullopt;
      displaced_flows.push_back(rewrite);
    }
  }

  std::vector<Cell> cells;
  cells.reserve(original_cells->size() - removed_operands.size() +
                displaced_flows.size());
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
      const auto original_call = std::find_if(
          calls.begin(), calls.end(), [&](const DirectCallSite& candidate) {
            return candidate.command == cell.value.origin;
          });
      if (original_call == calls.end())
        return std::nullopt;
      const auto selected = selector_by_target.find(original_call->target);
      if (selected == selector_by_target.end())
        return std::nullopt;
      call.opcode = 0xa0 + selected->second->register_index;
      call.mnemonic = opcode_by_code(call.opcode).name;
      call.indirect_flow_targets =
          std::vector<IrTarget>{static_cast<int>(original_call->target)};
    }
    const auto displaced = displaced_by_command.find(cell.value.origin);
    if (displaced != displaced_by_command.end()) {
      MachineItem& command = cell.value.item;
      command.opcode = displaced->second.direct_opcode;
      command.mnemonic = opcode_by_code(command.opcode).name;
      command.indirect_flow_targets.reset();
      cells.push_back(std::move(cell));
      cells.push_back(Cell{
          .value = OwnedItem{
              .item = MachineItem::address(
                  static_cast<int>(displaced->second.target_origin)),
              .origin = displaced->second.operand_origin,
          },
      });
      continue;
    }
    cells.push_back(std::move(cell));
  }
  for (const NaturalTargetAnchorCandidate& anchor : anchors) {
    if (!anchor.via_trampoline)
      continue;
    TransparentTrampoline trampoline{
        .selector_register = anchor.selector.register_index,
        .command_origin = next_synthetic_origin++,
        .operand_origin = next_synthetic_origin++,
        .target_origin = anchor.target_origin,
    };
    cells.push_back(Cell{
        .value = OwnedItem{
            .item = MachineItem::op(kJumpOpcode, opcode_by_code(kJumpOpcode).name),
            .origin = trampoline.command_origin,
        },
    });
    cells.push_back(Cell{
        .value = OwnedItem{
            .item = MachineItem::address(static_cast<int>(trampoline.target_origin)),
            .origin = trampoline.operand_origin,
        },
    });
    trampolines.push_back(trampoline);
  }
  return cells;
}

std::optional<NaturalTargetLayoutOrder> layout_order_for_targets(
    const std::vector<Segment>& segments, std::size_t main_segment,
    const std::vector<NaturalTargetPlacement>& placements,
    std::size_t maximum_states, int maximum_padding) {
  if (main_segment >= segments.size())
    return std::nullopt;
  if (placements.empty()) {
    NaturalTargetLayoutOrder layout;
    layout.segments.reserve(segments.size());
    layout.segments.push_back(main_segment);
    for (std::size_t index = 0; index < segments.size(); ++index) {
      if (index != main_segment)
        layout.segments.push_back(index);
    }
    return layout;
  }

  std::map<std::size_t, int> base_by_segment;
  for (const NaturalTargetPlacement& placement : placements) {
    if (placement.target_segment >= segments.size() ||
        placement.target_segment == main_segment || placement.target_offset < 0 ||
        placement.natural_target < placement.target_offset) {
      return std::nullopt;
    }
    const int base = placement.natural_target - placement.target_offset;
    const auto [found, inserted] =
        base_by_segment.emplace(placement.target_segment, base);
    if (!inserted && found->second != base)
      return std::nullopt;
  }

  std::vector<std::pair<int, std::size_t>> fixed_segments;
  for (const auto& [segment, base] : base_by_segment)
    fixed_segments.emplace_back(base, segment);
  std::sort(fixed_segments.begin(), fixed_segments.end());

  int cursor = segment_cells(segments.at(main_segment));
  std::vector<int> gap_capacities;
  gap_capacities.reserve(fixed_segments.size());
  for (const auto& [base, segment] : fixed_segments) {
    if (base < cursor)
      return std::nullopt;
    gap_capacities.push_back(base - cursor);
    cursor = base + segment_cells(segments.at(segment));
  }

  std::vector<std::size_t> eligible;
  for (std::size_t index = 0; index < segments.size(); ++index) {
    if (index != main_segment && !base_by_segment.contains(index))
      eligible.push_back(index);
  }

  using GapTotals = std::vector<int>;
  using GapAssignment = std::vector<int>;
  std::map<GapTotals, GapAssignment> states;
  states.emplace(GapTotals(gap_capacities.size(), 0),
                 GapAssignment(eligible.size(), -1));
  for (std::size_t eligible_index = 0; eligible_index < eligible.size();
       ++eligible_index) {
    std::map<GapTotals, GapAssignment> next = states;
    const int length = segment_cells(segments.at(eligible.at(eligible_index)));
    for (const auto& [totals, assignment] : states) {
      for (std::size_t gap = 0; gap < gap_capacities.size(); ++gap) {
        if (totals.at(gap) + length > gap_capacities.at(gap))
          continue;
        GapTotals extended_totals = totals;
        extended_totals.at(gap) += length;
        if (next.contains(extended_totals))
          continue;
        GapAssignment extended_assignment = assignment;
        extended_assignment.at(eligible_index) = static_cast<int>(gap);
        next.emplace(std::move(extended_totals), std::move(extended_assignment));
        if (next.size() > maximum_states)
          return std::nullopt;
      }
    }
    states = std::move(next);
  }

  auto solution = states.end();
  int best_filled = -1;
  for (auto state = states.begin(); state != states.end(); ++state) {
    int filled = 0;
    for (const int total : state->first)
      filled += total;
    if (filled > best_filled) {
      best_filled = filled;
      solution = state;
    }
  }
  int total_capacity = 0;
  for (const int capacity : gap_capacities)
    total_capacity += capacity;
  const int padding_cells = total_capacity - best_filled;
  if (solution == states.end() || padding_cells < 0 || padding_cells > maximum_padding)
    return std::nullopt;

  NaturalTargetLayoutOrder layout;
  layout.padding_cells = padding_cells;
  layout.segments.reserve(segments.size());
  layout.segments.push_back(main_segment);
  for (std::size_t gap = 0; gap < fixed_segments.size(); ++gap) {
    for (std::size_t index = 0; index < eligible.size(); ++index) {
      if (solution->second.at(index) == static_cast<int>(gap))
        layout.segments.push_back(eligible.at(index));
    }
    const std::size_t fixed_segment = fixed_segments.at(gap).second;
    const int padding = gap_capacities.at(gap) - solution->first.at(gap);
    if (padding > 0)
      layout.padding_before_segment.emplace(fixed_segment, padding);
    layout.segments.push_back(fixed_segment);
  }
  for (std::size_t index = 0; index < eligible.size(); ++index) {
    if (solution->second.at(index) < 0)
      layout.segments.push_back(eligible.at(index));
  }
  return layout.segments.size() == segments.size()
             ? std::optional<NaturalTargetLayoutOrder>(std::move(layout))
             : std::nullopt;
}

struct NaturalTargetLayoutVariant {
  std::vector<Segment> segments;
  NaturalTargetLayoutOrder order;
  std::optional<TransparentSplitBridge> bridge;
  int layout_cost = 0;
};

std::optional<NaturalTargetLayoutVariant>
layout_order_with_optional_split(
    const std::vector<Segment>& segments, std::size_t main_segment,
    const std::vector<NaturalTargetPlacement>& placements,
    std::size_t maximum_states, int maximum_padding,
    std::size_t bridge_command_origin) {
  std::optional<NaturalTargetLayoutVariant> best;
  auto consider = [&](std::vector<Segment> trial_segments,
                      NaturalTargetLayoutOrder trial_order,
                      std::optional<TransparentSplitBridge> bridge) {
    const int cost = trial_order.padding_cells + (bridge.has_value() ? 2 : 0);
    if (best.has_value() &&
        std::tie(best->layout_cost, best->order.padding_cells) <=
            std::tie(cost, trial_order.padding_cells)) {
      return;
    }
    best = NaturalTargetLayoutVariant{
        .segments = std::move(trial_segments),
        .order = std::move(trial_order),
        .bridge = bridge,
        .layout_cost = cost,
    };
  };

  if (const auto ordinary = layout_order_for_targets(
          segments, main_segment, placements, maximum_states, maximum_padding)) {
    consider(segments, *ordinary, std::nullopt);
  }
  if (maximum_padding < 2 || placements.size() < 2U ||
      main_segment >= segments.size()) {
    return best;
  }

  std::map<std::size_t, int> base_by_segment;
  for (const NaturalTargetPlacement& placement : placements) {
    if (placement.target_segment >= segments.size() ||
        placement.target_segment == main_segment || placement.target_offset < 0 ||
        placement.natural_target < placement.target_offset) {
      return best;
    }
    const int base = placement.natural_target - placement.target_offset;
    const auto [found, inserted] =
        base_by_segment.emplace(placement.target_segment, base);
    if (!inserted && found->second != base)
      return best;
  }

  std::vector<std::pair<int, std::size_t>> fixed_segments;
  fixed_segments.reserve(base_by_segment.size());
  for (const auto& [segment, base] : base_by_segment)
    fixed_segments.emplace_back(base, segment);
  std::sort(fixed_segments.begin(), fixed_segments.end());

  std::set<std::pair<std::size_t, std::size_t>> split_candidates;
  for (std::size_t index = 1; index < fixed_segments.size(); ++index) {
    const auto [base, segment] = fixed_segments.at(index - 1U);
    const int next_base = fixed_segments.at(index).first;
    const int prefix_cells = next_base - base - 2;
    if (base + segment_cells(segments.at(segment)) > next_base &&
        prefix_cells > 0 &&
        prefix_cells < segment_cells(segments.at(segment))) {
      split_candidates.emplace(segment,
                               static_cast<std::size_t>(prefix_cells));
    }
  }

  for (const auto& [segment, prefix_cells] : split_candidates) {
    std::vector<Segment> trial_segments = segments;
    TransparentSplitBridge bridge;
    if (!split_segment_with_bridge(
            trial_segments, segment, prefix_cells, bridge_command_origin,
            bridge_command_origin + 1U, bridge)) {
      continue;
    }

    std::vector<NaturalTargetPlacement> trial_placements = placements;
    const std::size_t suffix_segment = trial_segments.size() - 1U;
    for (NaturalTargetPlacement& placement : trial_placements) {
      if (placement.target_segment != segment ||
          placement.target_offset < static_cast<int>(prefix_cells)) {
        continue;
      }
      placement.target_segment = suffix_segment;
      placement.target_offset -= static_cast<int>(prefix_cells);
    }
    const auto trial_order = layout_order_for_targets(
        trial_segments, main_segment, trial_placements, maximum_states,
        maximum_padding - 2);
    if (trial_order.has_value())
      consider(std::move(trial_segments), *trial_order, bridge);
  }
  return best;
}

std::vector<MachineItem> flatten_segments(
    const std::vector<Segment>& segments, const std::vector<std::size_t>& order,
    const std::map<std::size_t, int>& padding_before_segment,
    std::map<std::size_t, std::size_t>& new_item_by_origin) {
  std::vector<MachineItem> result;
  for (const std::size_t segment_index : order) {
    const auto padding = padding_before_segment.find(segment_index);
    if (padding != padding_before_segment.end()) {
      for (int cell = 0; cell < padding->second; ++cell)
        result.push_back(MachineItem::op(0x54, opcode_by_code(0x54).name));
    }
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
    const std::vector<NaturalTargetCallRewrite>& converted_calls,
    const std::vector<DisplacedIndirectFlowRewrite>& displaced_flows,
    const std::vector<NaturalTargetAnchorCandidate>& anchors,
    const std::vector<TransparentTrampoline>& trampolines) {
  std::map<int, std::size_t> selected_physical_targets;
  for (const NaturalTargetAnchorCandidate& anchor : anchors) {
    const std::optional<std::size_t> physical =
        physical_target_origin(anchor, trampolines);
    if (!physical.has_value() ||
        !selected_physical_targets
             .emplace(anchor.selector.register_index, *physical)
             .second) {
      return false;
    }
  }
  std::set<std::size_t> displaced_commands;
  for (const DisplacedIndirectFlowRewrite& rewrite : displaced_flows)
    displaced_commands.insert(rewrite.command_origin);
  for (const auto& [flow_origin, targets] : original_flow.indirect_flow_targets) {
    if (displaced_commands.contains(flow_origin))
      continue;
    const auto command = new_item_by_origin.find(flow_origin);
    if (command == new_item_by_origin.end())
      return false;
    const MachineItem& rewritten_command = items.at(command->second);
    if (rewritten_command.kind != MachineItemKind::Op ||
        !is_indirect_flow(rewritten_command.opcode)) {
      return false;
    }
    const auto selected =
        selected_physical_targets.find(encoded_register(rewritten_command.opcode));
    std::vector<IrTarget> rebound;
    rebound.reserve(targets.size());
    for (const PostLayoutCommandIdentity& target : targets) {
      const std::size_t target_origin =
          selected == selected_physical_targets.end() ? target.item_index
                                                      : selected->second;
      const auto address = new_address_by_origin.find(target_origin);
      if (address == new_address_by_origin.end())
        return false;
      rebound.emplace_back(address->second);
    }
    items.at(command->second).indirect_flow_targets = std::move(rebound);
  }
  for (const DisplacedIndirectFlowRewrite& rewrite : displaced_flows) {
    const auto operand = new_item_by_origin.find(rewrite.operand_origin);
    const auto target = new_address_by_origin.find(rewrite.target_origin);
    if (operand == new_item_by_origin.end() ||
        target == new_address_by_origin.end()) {
      return false;
    }
    MachineItem& address = items.at(operand->second);
    if (address.kind != MachineItemKind::Address)
      return false;
    address.target = target->second;
    address.formal_opcode.reset();
  }
  for (const NaturalTargetCallRewrite& rewrite : converted_calls) {
    const auto command = new_item_by_origin.find(rewrite.original_call_item);
    const auto anchor = std::find_if(
        anchors.begin(), anchors.end(), [&](const NaturalTargetAnchorCandidate& candidate) {
          return candidate.selector.register_name == rewrite.selector_register &&
                 candidate.target_origin == rewrite.original_target_item;
        });
    if (anchor == anchors.end())
      return false;
    const std::optional<std::size_t> physical =
        physical_target_origin(*anchor, trampolines);
    if (!physical.has_value())
      return false;
    const auto target = new_address_by_origin.find(*physical);
    if (command == new_item_by_origin.end() || target == new_address_by_origin.end())
      return false;
    items.at(command->second).indirect_flow_targets =
        std::vector<IrTarget>{target->second};
  }
  return true;
}

bool retarget_trampolines(
    std::vector<MachineItem>& items,
    const std::map<std::size_t, std::size_t>& new_item_by_origin,
    const std::map<std::size_t, int>& new_address_by_origin,
    const std::vector<TransparentTrampoline>& trampolines) {
  for (const TransparentTrampoline& trampoline : trampolines) {
    const auto operand = new_item_by_origin.find(trampoline.operand_origin);
    const auto target = new_address_by_origin.find(trampoline.target_origin);
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

bool retarget_split_bridges(
    std::vector<MachineItem>& items,
    const std::map<std::size_t, std::size_t>& new_item_by_origin,
    const std::map<std::size_t, int>& new_address_by_origin,
    const std::vector<TransparentSplitBridge>& bridges) {
  for (const TransparentSplitBridge& bridge : bridges) {
    const auto operand = new_item_by_origin.find(bridge.operand_origin);
    const auto target = new_address_by_origin.find(bridge.target_origin);
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

bool prove_numeric_nonflow_projection(const std::vector<MachineItem>& items,
                                      int register_index_value) {
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind != MachineItemKind::Op)
      continue;
    if (is_indirect_flow(item.opcode) && encoded_register(item.opcode) == register_index_value)
      continue;
    if (is_indirect_memory(item.opcode) && encoded_register(item.opcode) == register_index_value)
      return false;
    if (!direct_recall_of(item, register_index_value))
      continue;
    const std::optional<std::size_t> next = next_cell_item(items, item_index);
    if (!next.has_value() || items.at(*next).kind != MachineItemKind::Op ||
        items.at(*next).opcode < 0x10 || items.at(*next).opcode > 0x13 ||
        items.at(*next).manual_interaction.has_value()) {
      return false;
    }
  }
  return true;
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
  if (!has_proved_address_projection(value))
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
    const std::vector<NaturalTargetAnchorCandidate>& anchors,
    const std::vector<DisplacedIndirectFlowRewrite>& displaced_flows,
    const std::vector<TransparentTrampoline>& trampolines,
    AddressSpaceModel model, std::vector<PreloadReport>& preloads,
    std::vector<NaturalTargetPreloadRewrite>& rewrites) {
  bool unique = false;
  std::map<std::string, std::size_t> preload_by_register =
      preload_index(preloads, unique);
  if (!unique)
    return false;

  std::map<int, const NaturalTargetAnchorCandidate*> selected_by_register;
  for (const NaturalTargetAnchorCandidate& anchor : anchors) {
    if (!selected_by_register
             .emplace(anchor.selector.register_index, &anchor)
             .second) {
      return false;
    }
  }

  std::set<std::size_t> displaced_commands;
  for (const DisplacedIndirectFlowRewrite& rewrite : displaced_flows)
    displaced_commands.insert(rewrite.command_origin);

  std::map<int, std::set<std::size_t>> required_targets;
  for (std::size_t item_index = 0; item_index < original_items.size(); ++item_index) {
    const MachineItem& item = original_items.at(item_index);
    if (item.kind != MachineItemKind::Op || !is_indirect_flow(item.opcode))
      continue;
    if (displaced_commands.contains(item_index))
      continue;
    const int reg = encoded_register(item.opcode);
    const std::optional<std::size_t> target =
        target_origin_for_flow_use(original_flow, item_index);
    if (!target.has_value())
      return false;
    const auto selected = selected_by_register.find(reg);
    if (selected != selected_by_register.end()) {
      const std::optional<std::size_t> physical =
          physical_target_origin(*selected->second, trampolines);
      if (!physical.has_value())
        return false;
      required_targets[reg].insert(*physical);
    } else {
      required_targets[reg].insert(*target);
    }
  }
  for (const NaturalTargetAnchorCandidate& anchor : anchors) {
    const std::optional<std::size_t> physical =
        physical_target_origin(anchor, trampolines);
    if (!physical.has_value())
      return false;
    required_targets[anchor.selector.register_index].insert(*physical);
  }

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
    std::size_t original_target_origin = target_origin;
    const auto selected = selected_by_register.find(reg);
    if (selected != selected_by_register.end() &&
        selected->second->via_trampoline) {
      original_target_origin = selected->second->target_origin;
    }
    const auto old_target = original_address_by_origin.find(original_target_origin);
    const auto new_target = new_address_by_origin.find(target_origin);
    if (old_target == original_address_by_origin.end() ||
        new_target == new_address_by_origin.end()) {
      return false;
    }
    const std::string old_value = preloads.at(preload->second).value;

    // Existing typed facts must describe the exact delivered selector before
    // relocation; otherwise a preload report cannot be elevated into a proof.
    const std::vector<std::size_t> original_uses =
        indirect_flow_uses(original_items, reg);
    const bool had_original_flow_use =
        std::any_of(original_uses.begin(), original_uses.end(),
                    [&](std::size_t use) {
                      return !displaced_commands.contains(use);
                    });
    if (had_original_flow_use) {
      if (!preload_value_targets(name, old_value, old_target->second, model))
        return false;
    }
    if (preload_value_targets(name, old_value, new_target->second, model))
      continue;

    if (selected != selected_by_register.end() &&
        selected->second->selector.rebindable_address) {
      if (register_has_nonflow_use(original_items, reg))
        return false;
      const std::string rebound = std::to_string(new_target->second);
      if (!preload_value_targets(name, rebound, new_target->second, model))
        return false;
      preloads.at(preload->second).value = rebound;
      rewrites.push_back(NaturalTargetPreloadRewrite{
          .register_name = name,
          .old_value = old_value,
          .new_value = rebound,
          .fractional_projection_only = false,
      });
      continue;
    }
    if (selected != selected_by_register.end() &&
        selected->second->selector.value.has_value()) {
      const std::optional<NaturalFractionalSelectorEncoding> natural =
          natural_fractional_selector_encoding(old_value);
      if (natural.has_value() && natural->target == new_target->second &&
          natural->delivered_value == *selected->second->selector.value &&
          preload_value_targets(name, *selected->second->selector.value,
                                new_target->second, model) &&
          prove_numeric_nonflow_projection(original_items, reg)) {
        preloads.at(preload->second).value = *selected->second->selector.value;
        rewrites.push_back(NaturalTargetPreloadRewrite{
            .register_name = name,
            .old_value = old_value,
            .new_value = *selected->second->selector.value,
            .fractional_projection_only = false,
        });
        continue;
      }
    }

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
    AddressSpaceModel model, int maximum_states,
    const std::map<std::size_t, std::size_t>* transparent_aliases = nullptr) {
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
    if (transparent_aliases != nullptr && transparent_aliases->contains(*command))
      return std::nullopt;
    TraceState state{.command = *command};
    for (const PostLayoutCommandIdentity& slot : entry.return_stack) {
      const std::optional<std::size_t> origin =
          origin_for_item(origin_by_item, slot.item_index);
      if (!origin.has_value())
        return std::nullopt;
      if (transparent_aliases != nullptr && transparent_aliases->contains(*origin))
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
      if (transparent_aliases != nullptr) {
        const auto alias = transparent_aliases->find(command);
        if (alias != transparent_aliases->end())
          command = alias->second;
      }
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
                                       const std::vector<NaturalTargetAnchorCandidate>& anchors,
                                       const std::vector<NaturalTargetCallRewrite>& calls) {
  if (std::any_of(anchors.begin(), anchors.end(), [](const auto& anchor) {
        return indirect_selector_mutation(anchor.selector.register_name) !=
               IndirectSelectorMutation::Stable;
      })) {
    return false;
  }
  const OpcodeInfo& direct = opcode_by_code(kCallOpcode);
  return std::all_of(calls.begin(), calls.end(), [&](const NaturalTargetCallRewrite& call) {
    const auto anchor = std::find_if(anchors.begin(), anchors.end(), [&](const auto& candidate) {
      return candidate.selector.register_name == call.selector_register;
    });
    if (anchor == anchors.end())
      return false;
    const OpcodeInfo& indirect =
        opcode_by_code(0xa0 + anchor->selector.register_index);
    return direct.stack_effect == indirect.stack_effect &&
           direct.x2_effect == indirect.x2_effect &&
           direct.takes_address != indirect.takes_address &&
           call.original_call_item < original_items.size() &&
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
    const std::vector<PreloadReport>& preloads, AddressSpaceModel model,
    const std::map<std::size_t, std::size_t>& transparent_aliases) {
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
    std::size_t logical_target_origin = *target_origin;
    if (const auto alias = transparent_aliases.find(logical_target_origin);
        alias != transparent_aliases.end()) {
      logical_target_origin = alias->second;
    }
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
    if (!has_proved_address_projection(value))
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
          original_targets->second.front().item_index != logical_target_origin)
        return std::nullopt;
    }
    result.push_back(NaturalTargetRuntimeSelectorProof{
        .original_command_item = *command_origin,
        .final_command_item = final_command,
        .original_target_item = logical_target_origin,
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
    const std::vector<NaturalTargetCallRewrite>& calls,
    const std::vector<DisplacedIndirectFlowRewrite>& displaced_flows) {
  std::set<std::size_t> converted;
  for (const NaturalTargetCallRewrite& call : calls)
    converted.insert(call.original_call_item);
  std::map<std::size_t, int> displaced;
  for (const DisplacedIndirectFlowRewrite& rewrite : displaced_flows)
    displaced.emplace(rewrite.command_origin, rewrite.direct_opcode);
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
    const auto displaced_command = displaced.find(origin);
    if (displaced_command != displaced.end()) {
      const OpcodeInfo& before_info = opcode_by_code(before.opcode);
      const OpcodeInfo& after_info = opcode_by_code(after.opcode);
      if (after.opcode != displaced_command->second ||
          before_info.stack_effect != after_info.stack_effect ||
          before_info.x2_effect != after_info.x2_effect ||
          before_info.takes_address == after_info.takes_address) {
        return false;
      }
    } else if (!converted.contains(origin) && before.opcode != after.opcode) {
      return false;
    }
  }
  return true;
}

bool transparent_trampolines_proved(
    const std::vector<MachineItem>& items,
    const std::map<std::size_t, std::size_t>& new_item_by_origin,
    const std::map<std::size_t, int>& new_address_by_origin,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<TransparentTrampoline>& trampolines) {
  const ArtifactIndex index = index_artifact(items);
  const OpcodeInfo& jump = opcode_by_code(kJumpOpcode);
  if (jump.stack_effect != StackEffect::Preserves ||
      jump.x2_effect != X2Effect::Preserves)
    return false;
  std::set<std::size_t> trampoline_commands;
  for (const TransparentTrampoline& trampoline : trampolines) {
    const auto command = new_item_by_origin.find(trampoline.command_origin);
    const auto operand = new_item_by_origin.find(trampoline.operand_origin);
    const auto target = new_address_by_origin.find(trampoline.target_origin);
    if (command == new_item_by_origin.end() || operand == new_item_by_origin.end() ||
        target == new_address_by_origin.end()) {
      return false;
    }
    const MachineItem& jump_item = items.at(command->second);
    const MachineItem& address_item = items.at(operand->second);
    const int* encoded_target = std::get_if<int>(&address_item.target);
    if (jump_item.kind != MachineItemKind::Op || jump_item.opcode != kJumpOpcode ||
        jump_item.manual_interaction.has_value() ||
        address_item.kind != MachineItemKind::Address || address_item.raw ||
        !address_item.roles.empty() || encoded_target == nullptr ||
        *encoded_target != target->second ||
        index.item_addresses.at(operand->second) !=
            index.item_addresses.at(command->second) + 1) {
      return false;
    }
    trampoline_commands.insert(command->second);
  }
  for (const PostLayoutExternalEntryState& entry : flow.external_entries) {
    if (trampoline_commands.contains(entry.entry.item_index))
      return false;
    for (const PostLayoutCommandIdentity& slot : entry.return_stack) {
      if (trampoline_commands.contains(slot.item_index))
        return false;
    }
  }
  return !flow.empty_return_target.has_value() ||
         !trampoline_commands.contains(flow.empty_return_target->item_index);
}

bool transparent_split_bridges_proved(
    const std::vector<MachineItem>& items,
    const std::map<std::size_t, std::size_t>& new_item_by_origin,
    const std::map<std::size_t, int>& new_address_by_origin,
    const AuthoritativePostLayoutControlFlow& flow,
    const std::vector<TransparentSplitBridge>& bridges) {
  const ArtifactIndex index = index_artifact(items);
  const OpcodeInfo& jump = opcode_by_code(kJumpOpcode);
  if (jump.stack_effect != StackEffect::Preserves ||
      jump.x2_effect != X2Effect::Preserves)
    return false;
  std::set<std::size_t> bridge_commands;
  for (const TransparentSplitBridge& bridge : bridges) {
    const auto command = new_item_by_origin.find(bridge.command_origin);
    const auto operand = new_item_by_origin.find(bridge.operand_origin);
    const auto target = new_address_by_origin.find(bridge.target_origin);
    if (command == new_item_by_origin.end() || operand == new_item_by_origin.end() ||
        target == new_address_by_origin.end()) {
      return false;
    }
    const MachineItem& jump_item = items.at(command->second);
    const MachineItem& address_item = items.at(operand->second);
    const int* encoded_target = std::get_if<int>(&address_item.target);
    if (jump_item.kind != MachineItemKind::Op || jump_item.opcode != kJumpOpcode ||
        jump_item.manual_interaction.has_value() ||
        address_item.kind != MachineItemKind::Address || address_item.raw ||
        !address_item.roles.empty() || encoded_target == nullptr ||
        *encoded_target != target->second ||
        index.item_addresses.at(operand->second) !=
            index.item_addresses.at(command->second) + 1) {
      return false;
    }
    bridge_commands.insert(command->second);
  }
  for (const PostLayoutExternalEntryState& entry : flow.external_entries) {
    if (bridge_commands.contains(entry.entry.item_index))
      return false;
    for (const PostLayoutCommandIdentity& slot : entry.return_stack) {
      if (bridge_commands.contains(slot.item_index))
        return false;
    }
  }
  return !flow.empty_return_target.has_value() ||
         !bridge_commands.contains(flow.empty_return_target->item_index);
}

std::optional<CandidateArtifact> try_candidate(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads,
    const AuthoritativePostLayoutControlFlow& control_flow,
    const NaturalTargetComponentLayoutOptions& options,
    const std::vector<DirectReference>& references,
    const std::vector<DirectCallSite>& calls,
    const std::vector<NaturalTargetAnchorCandidate>& anchors,
    const TraceGraph& original_trace,
    const std::vector<ExternalIdentity>& original_external,
    std::vector<std::string>* rejection_reasons) {
  const auto reject = [&](std::string reason) -> std::optional<CandidateArtifact> {
    if (rejection_reasons != nullptr &&
        rejection_reasons->size() < options.maximum_rejection_reasons) {
      std::string detail;
      for (const NaturalTargetAnchorCandidate& anchor : anchors) {
        if (!detail.empty())
          detail += ",";
        detail += "R" + anchor.selector.register_name + "->" +
                  (anchor.selector.rebindable_address
                       ? std::string("rebind")
                       : std::to_string(anchor.selector.fixed_target)) +
                  (anchor.via_trampoline ? std::string("/trampoline")
                                         : std::string{}) +
                  (anchor.split_prefix_cells > 0
                       ? "/split-" + std::to_string(anchor.split_prefix_cells)
                       : std::string{}) +
                  " target-item=" +
                  std::to_string(anchor.target_origin);
      }
      detail += ": " + reason;
      if (std::find(rejection_reasons->begin(), rejection_reasons->end(), detail) ==
          rejection_reasons->end()) {
        rejection_reasons->push_back(detail);
      }
    }
    return std::nullopt;
  };
  if (anchors.empty())
    return reject("candidate has no natural-target anchors");
  std::vector<NaturalTargetCallRewrite> rewrites;
  std::vector<DisplacedIndirectFlowRewrite> displaced_flows;
  std::vector<TransparentTrampoline> trampolines;
  const std::optional<std::vector<Cell>> converted = convert_direct_calls(
      items, calls, control_flow, anchors, rewrites, displaced_flows, trampolines);
  if (!converted.has_value() || rewrites.empty())
    return reject("direct-call conversion failed");
  std::vector<Segment> segments = make_segments(*converted);
  std::size_t next_synthetic_origin =
      items.size() + displaced_flows.size() + 2U * trampolines.size();
  std::vector<TransparentSplitBridge> split_bridges;
  if (!apply_transparent_segment_splits(segments, anchors, next_synthetic_origin,
                                        split_bridges)) {
    return reject("fallthrough segment split could not be proved");
  }

  const std::optional<std::size_t> main = main_origin(control_flow);
  if (!main.has_value())
    return reject("main entry identity is ambiguous");
  const ArtifactIndex original_index = index_artifact(items);
  if (*main >= original_index.item_addresses.size() ||
      original_index.item_addresses.at(*main) != 0)
    return reject("main entry is not physical cell 00");
  const std::optional<std::pair<std::size_t, int>> main_location =
      locate_origin(segments, *main);
  if (!main_location.has_value() || main_location->second != 0)
    return reject("main component identity cannot be located");

  std::vector<NaturalTargetPlacement> placements;
  std::optional<NaturalTargetPlacement> flexible_placement;
  std::map<std::size_t, const NaturalTargetAnchorCandidate*> anchor_by_target_origin;
  for (const NaturalTargetAnchorCandidate& anchor : anchors) {
    if (anchor.selector.origin != NaturalTargetSelectorOrigin::ExistingPreload)
      return reject("selector is not an existing stable preload");
    const std::optional<std::size_t> placement_origin =
        physical_target_origin(anchor, trampolines);
    if (!placement_origin.has_value())
      return reject("trampoline target identity cannot be located");
    const auto target_location = locate_origin(segments, *placement_origin);
    if (!target_location.has_value())
      return reject("target component identity cannot be located");
    if (!anchor_by_target_origin
             .emplace(anchor.target_origin, &anchor)
             .second) {
      return reject("multiple selectors claim the same target identity");
    }
    NaturalTargetPlacement placement{
        .target_segment = target_location->first,
        .target_offset = target_location->second,
        .natural_target = anchor.selector.fixed_target,
    };
    if (anchor.selector.rebindable_address) {
      if (flexible_placement.has_value())
        return reject("multiple address-only selector rebinds are not bounded");
      flexible_placement = placement;
    } else {
      placements.push_back(placement);
    }
  }
  const int maximum_padding = static_cast<int>(rewrites.size()) -
                              static_cast<int>(displaced_flows.size()) -
                              2 * static_cast<int>(trampolines.size()) -
                              2 * static_cast<int>(split_bridges.size()) - 1;
  if (maximum_padding < 0)
    return reject("address-selector displacement cannot produce a smaller artifact");
  std::optional<NaturalTargetLayoutVariant> selected_layout;
  int flexible_target = -1;
  if (flexible_placement.has_value()) {
    for (int target = 1;
         target <= official_program_last_address(options.address_space_model);
         ++target) {
      std::vector<NaturalTargetPlacement> trial_placements = placements;
      NaturalTargetPlacement trial = *flexible_placement;
      trial.natural_target = target;
      trial_placements.push_back(trial);
      std::optional<NaturalTargetLayoutVariant> trial_layout =
          layout_order_with_optional_split(
              segments, main_location->first, trial_placements,
              options.maximum_subset_states, maximum_padding,
              next_synthetic_origin);
      if (!trial_layout.has_value())
        continue;
      if (!selected_layout.has_value() ||
          std::tie(trial_layout->layout_cost,
                   trial_layout->order.padding_cells, target) <
              std::tie(selected_layout->layout_cost,
                       selected_layout->order.padding_cells,
                       flexible_target)) {
        selected_layout = std::move(trial_layout);
        flexible_target = target;
      }
    }
  } else {
    selected_layout = layout_order_with_optional_split(
        segments, main_location->first, placements,
        options.maximum_subset_states, maximum_padding,
        next_synthetic_origin);
  }
  if (!selected_layout.has_value()) {
    std::string geometry =
        "no fallthrough-closed component assignment reaches every natural target; main=" +
        std::to_string(segment_cells(segments.at(main_location->first))) +
        "; padding-cap=" + std::to_string(maximum_padding) + "; fixed=";
    std::set<std::size_t> fixed_segments;
    for (const NaturalTargetPlacement& placement : placements) {
      if (!fixed_segments.empty())
        geometry += ",";
      fixed_segments.insert(placement.target_segment);
      geometry += std::to_string(placement.natural_target) + "@" +
                  std::to_string(placement.target_offset) + "+" +
                  std::to_string(segment_cells(segments.at(placement.target_segment)));
    }
    if (flexible_placement.has_value()) {
      geometry += "; flexible=@" +
                  std::to_string(flexible_placement->target_offset) + "+" +
                  std::to_string(
                      segment_cells(segments.at(flexible_placement->target_segment)));
      fixed_segments.insert(flexible_placement->target_segment);
    }
    geometry += "; free=";
    bool first_free = true;
    for (std::size_t segment = 0; segment < segments.size(); ++segment) {
      if (segment == main_location->first || fixed_segments.contains(segment))
        continue;
      if (!first_free)
        geometry += ",";
      first_free = false;
      geometry += std::to_string(segment_cells(segments.at(segment)));
    }
    return reject(std::move(geometry));
  }

  segments = std::move(selected_layout->segments);
  if (selected_layout->bridge.has_value()) {
    split_bridges.push_back(*selected_layout->bridge);
    next_synthetic_origin += 2U;
  }

  CandidateArtifact candidate;
  candidate.items = flatten_segments(segments, selected_layout->order.segments,
                                     selected_layout->order.padding_before_segment,
                                     candidate.new_item_by_origin);
  const std::map<std::size_t, int> new_address_by_origin =
      origin_addresses(candidate.items, candidate.new_item_by_origin);
  std::map<std::size_t, int> target_address_by_origin;
  for (const auto& [target_origin, anchor] : anchor_by_target_origin) {
    const std::optional<std::size_t> physical =
        physical_target_origin(*anchor, trampolines);
    if (!physical.has_value())
      return reject("flattened component order lost a trampoline target");
    const auto target_address = new_address_by_origin.find(*physical);
    if (target_address == new_address_by_origin.end())
      return reject("flattened component order lost a selected target");
    if (!anchor->selector.rebindable_address &&
        target_address->second != anchor->selector.fixed_target) {
      return reject("flattened component order missed a natural target");
    }
    if (anchor->selector.rebindable_address &&
        (target_address->second <= 0 ||
         target_address->second >
             official_program_last_address(options.address_space_model))) {
      return reject("rebound address selector target is outside the official window");
    }
    target_address_by_origin.emplace(target_origin, target_address->second);
  }
  for (NaturalTargetCallRewrite& rewrite : rewrites)
    rewrite.target_address = target_address_by_origin.at(rewrite.original_target_item);

  std::set<std::size_t> removed_operands;
  for (const DirectCallSite& call : calls) {
    if (target_address_by_origin.contains(call.target))
      removed_operands.insert(call.operand);
  }
  if (!retarget_direct_references(candidate.items, references, removed_operands,
                                  candidate.new_item_by_origin, new_address_by_origin) ||
      !retarget_indirect_facts(candidate.items, control_flow,
                               candidate.new_item_by_origin, new_address_by_origin,
                               rewrites, displaced_flows, anchors, trampolines) ||
      !retarget_trampolines(candidate.items, candidate.new_item_by_origin,
                            new_address_by_origin, trampolines) ||
      !retarget_split_bridges(candidate.items, candidate.new_item_by_origin,
                              new_address_by_origin, split_bridges)) {
    return reject("direct or indirect target rebinding failed");
  }

  std::map<std::size_t, int> original_address_by_origin;
  for (std::size_t origin = 0; origin < items.size(); ++origin)
    original_address_by_origin.emplace(origin, original_index.item_addresses.at(origin));
  candidate.preloads = preloads;
  std::vector<NaturalTargetPreloadRewrite> preload_rewrites;
  if (!rebind_preloads(items, control_flow, original_address_by_origin,
                       new_address_by_origin, anchors,
                       displaced_flows, trampolines,
                       options.address_space_model, candidate.preloads,
                       preload_rewrites)) {
    return reject("runtime preload rebinding proof failed");
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
      return reject("empty-return target identity was lost");
    final_options.empty_return_target = rebound->second;
  }
  const AuthoritativePostLayoutControlFlow final_flow =
      build_post_layout_control_flow(candidate.items, final_options);
  if (!final_flow.proved) {
    return reject("final control-flow proof failed" +
                  (final_flow.reasons.empty() ? std::string{}
                                              : ": " + final_flow.reasons.front()));
  }

  std::map<std::size_t, std::size_t> original_origin_by_item;
  for (std::size_t index = 0; index < items.size(); ++index)
    original_origin_by_item.emplace(index, index);
  std::map<std::size_t, std::size_t> rewritten_origin_by_item;
  for (const auto& [origin, item_index] : candidate.new_item_by_origin)
    rewritten_origin_by_item.emplace(item_index, origin);
  std::map<std::size_t, std::size_t> transparent_aliases;
  for (const TransparentTrampoline& trampoline : trampolines)
    transparent_aliases.emplace(trampoline.command_origin, trampoline.target_origin);
  for (const TransparentSplitBridge& bridge : split_bridges)
    transparent_aliases.emplace(bridge.command_origin, bridge.target_origin);
  const bool trampolines_proved = transparent_trampolines_proved(
      candidate.items, candidate.new_item_by_origin, new_address_by_origin,
      final_flow, trampolines);
  const bool split_bridges_proved = transparent_split_bridges_proved(
      candidate.items, candidate.new_item_by_origin, new_address_by_origin,
      final_flow, split_bridges);
  const std::optional<TraceGraph> rewritten_trace = build_trace_graph(
      candidate.items, final_flow, rewritten_origin_by_item,
      options.address_space_model, options.maximum_execution_states,
      &transparent_aliases);
  const std::optional<std::vector<ExternalIdentity>> rewritten_external =
      external_identities(final_flow, rewritten_origin_by_item);
  const std::optional<std::vector<NaturalTargetRuntimeSelectorProof>> runtime_selectors =
      prove_runtime_selectors(items, control_flow, candidate.items, final_flow,
                              rewritten_origin_by_item, candidate.preloads,
                              options.address_space_model, transparent_aliases);
  if (!rewritten_trace.has_value())
    return reject("rewritten identity trace is incomplete");
  if (!rewritten_external.has_value())
    return reject("rewritten external-entry ledger is incomplete");
  if (!runtime_selectors.has_value())
    return reject("runtime selector proof failed");
  if (*rewritten_trace != original_trace)
    return reject("rewritten identity trace differs from the logical input");
  if (*rewritten_external != original_external)
    return reject("rewritten external-entry ledger differs from the logical input");

  NaturalTargetComponentLayoutPlan& plan = candidate.plan;
  plan.selector_origin = anchors.front().selector.origin;
  plan.selector_register = anchors.front().selector.register_name;
  plan.natural_target = target_address_by_origin.at(anchors.front().target_origin);
  plan.input_cells = original_index.cells;
  plan.output_cells = index_artifact(candidate.items).cells;
  plan.removed_cells = plan.input_cells - plan.output_cells;
  plan.moved_segments =
      count_moved_segments(selected_layout->order.segments);
  plan.rebound_indirect_flows = static_cast<int>(displaced_flows.size());
  plan.transparent_trampolines = static_cast<int>(trampolines.size());
  plan.transparent_split_bridges = static_cast<int>(split_bridges.size());
  plan.calls = std::move(rewrites);
  plan.preloads = std::move(preload_rewrites);
  plan.runtime_selectors = *runtime_selectors;
  plan.final_control_flow = final_flow;
  plan.control_flow_equivalent = true;
  plan.call_return_equivalent = true;
  plan.stack_and_x2_equivalent =
      trampolines_proved && split_bridges_proved &&
      converted_call_effects_equivalent(items, anchors, plan.calls) &&
      unchanged_command_effects_preserved(items, candidate.items,
                                          candidate.new_item_by_origin, plan.calls,
                                          displaced_flows);
  plan.indirect_memory_equivalent = indirect_memory_facts_equivalent(
      control_flow, final_flow, rewritten_origin_by_item);
  plan.data_projection_equivalent = true;
  plan.final_artifact_proved = final_flow.proved;
  plan.proved =
                plan.removed_cells ==
                    static_cast<int>(plan.calls.size()) -
                        static_cast<int>(displaced_flows.size()) -
                        2 * static_cast<int>(trampolines.size()) -
                        2 * static_cast<int>(split_bridges.size()) -
                        selected_layout->order.padding_cells &&
                plan.removed_cells > 0 && plan.control_flow_equivalent &&
                plan.call_return_equivalent && plan.stack_and_x2_equivalent &&
                plan.indirect_memory_equivalent && plan.data_projection_equivalent &&
                plan.final_artifact_proved;
  if (!plan.proved)
    return reject("final stack/X2, indirect-memory, or size proof failed");
  return candidate;
}

bool better_candidate(const CandidateArtifact& left, const CandidateArtifact& right) {
  if (left.plan.removed_cells != right.plan.removed_cells)
    return left.plan.removed_cells > right.plan.removed_cells;
  if (left.plan.selector_origin != right.plan.selector_origin) {
    return left.plan.selector_origin == NaturalTargetSelectorOrigin::ExistingPreload;
  }
  if (left.plan.transparent_trampolines != right.plan.transparent_trampolines)
    return left.plan.transparent_trampolines < right.plan.transparent_trampolines;
  if (left.plan.transparent_split_bridges != right.plan.transparent_split_bridges)
    return left.plan.transparent_split_bridges < right.plan.transparent_split_bridges;
  return std::tie(left.plan.natural_target, left.plan.selector_register) <
         std::tie(right.plan.natural_target, right.plan.selector_register);
}

} // namespace

std::optional<std::vector<MachineItem>> normalize_natural_target_overflow_formals(
    const std::vector<MachineItem>& items, AddressSpaceModel model) {
  const std::optional<OverflowTargetNormalization> normalized =
      normalize_overflow_formals(items, model);
  if (!normalized.has_value())
    return std::nullopt;
  return normalized->items;
}

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
  const std::optional<OverflowTargetNormalization> normalized =
      normalize_overflow_formals(items, options.address_space_model);
  if (!normalized.has_value()) {
    add_reason(result.plan, "overflow label formal operands cannot be normalized safely");
    return result;
  }
  const std::vector<MachineItem>& logical_items = normalized->items;
  const AuthoritativePostLayoutControlFlow* logical_flow = &control_flow;
  std::optional<AuthoritativePostLayoutControlFlow> rebuilt_logical_flow;
  if (normalized->changed) {
    PostLayoutControlFlowOptions logical_flow_options;
    logical_flow_options.address_space_model = options.address_space_model;
    logical_flow_options.maximum_execution_states =
        static_cast<std::size_t>(options.maximum_execution_states);
    if (control_flow.empty_return_target.has_value())
      logical_flow_options.empty_return_target = control_flow.empty_return_target->address;
    rebuilt_logical_flow =
        build_post_layout_control_flow(logical_items, logical_flow_options);
    if (!rebuilt_logical_flow->proved) {
      add_reason(result.plan, "logical overflow-label control flow is not authoritative");
      return result;
    }
    logical_flow = &*rebuilt_logical_flow;
  }
  if (options.maximum_execution_states <= 0 || options.maximum_subset_states == 0U) {
    add_reason(result.plan, "proof search cap is not positive");
    return result;
  }
  const ArtifactIndex index = index_artifact(logical_items);
  const std::optional<std::vector<DirectReference>> references =
      collect_direct_references(logical_items, index, options.address_space_model);
  if (!references.has_value()) {
    add_reason(result.plan, "direct command identities cannot be resolved exactly");
    return result;
  }
  const std::vector<DirectCallSite> calls = direct_calls(*references, logical_items);
  if (calls.empty()) {
    add_reason(result.plan, "artifact contains no direct call to shorten");
    return result;
  }

  std::map<std::size_t, std::size_t> original_origin_by_item;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index)
    original_origin_by_item.emplace(item_index, item_index);
  const std::optional<TraceGraph> original_trace = build_trace_graph(
      logical_items, *logical_flow, original_origin_by_item, options.address_space_model,
      options.maximum_execution_states);
  const std::optional<std::vector<ExternalIdentity>> original_external =
      external_identities(*logical_flow, original_origin_by_item);
  if (!original_trace.has_value() || !original_external.has_value()) {
    add_reason(result.plan, "input identity trace or external-entry ledger is incomplete");
    return result;
  }

  std::vector<SelectorCandidate> selectors = selector_candidates(
      logical_items, preloads, *logical_flow, options);

  std::set<std::size_t> call_targets;
  std::map<std::size_t, int> call_sites_by_target;
  for (const DirectCallSite& call : calls)
    ++call_sites_by_target[call.target];
  for (const auto& [target, count] : call_sites_by_target) {
    (void)count;
    call_targets.insert(target);
  }

  std::map<int, std::vector<NaturalTargetAnchorCandidate>> options_by_selector;
  for (const SelectorCandidate& selector : selectors) {
    if (!selector.rebindable_address && selector.fixed_target <= 0)
      continue;
    for (const std::size_t target : call_targets) {
      if (!selector_existing_uses_match_target(selector, target, logical_items, *logical_flow))
        continue;
      if (selector.rebindable_address &&
          call_sites_by_target.at(target) <= selector.displaced_flow_uses) {
        continue;
      }
      options_by_selector[selector.register_index].push_back(
          NaturalTargetAnchorCandidate{
              .selector = selector,
              .target_origin = target,
              .call_sites = call_sites_by_target.at(target),
          });
    }
  }

  std::vector<std::vector<NaturalTargetAnchorCandidate>> selector_groups;
  for (auto& [register_index, group] : options_by_selector) {
    (void)register_index;
    std::sort(group.begin(), group.end(), [](const auto& left, const auto& right) {
      const int left_savings =
          left.call_sites - left.selector.displaced_flow_uses;
      const int right_savings =
          right.call_sites - right.selector.displaced_flow_uses;
      if (left_savings != right_savings)
        return left_savings > right_savings;
      return std::tie(left.selector.fixed_target, left.target_origin) <
             std::tie(right.selector.fixed_target, right.target_origin);
    });
    selector_groups.push_back(std::move(group));
  }

  const auto score = [](const std::vector<NaturalTargetAnchorCandidate>& anchors) {
    int total = 0;
    for (const NaturalTargetAnchorCandidate& anchor : anchors)
      total += anchor.call_sites - anchor.selector.displaced_flow_uses -
               (anchor.via_trampoline ? 2 : 0) -
               (anchor.split_prefix_cells > 0 ? 2 : 0);
    return total;
  };
  struct CombinationChoice {
    std::optional<NaturalTargetAnchorCandidate> anchor;
    int score = 0;
  };
  std::vector<std::vector<CombinationChoice>> choices_by_selector;
  choices_by_selector.reserve(selector_groups.size());
  for (const std::vector<NaturalTargetAnchorCandidate>& group : selector_groups) {
    std::vector<CombinationChoice> choices;
    choices.reserve(group.size() + 1U);
    for (const NaturalTargetAnchorCandidate& anchor : group) {
      choices.push_back(CombinationChoice{
          .anchor = anchor,
          .score = score(std::vector<NaturalTargetAnchorCandidate>{anchor}),
      });
    }
    choices.push_back(CombinationChoice{});
    std::stable_sort(choices.begin(), choices.end(), [](const CombinationChoice& left,
                                                        const CombinationChoice& right) {
      if (left.score != right.score)
        return left.score > right.score;
      if (left.anchor.has_value() != right.anchor.has_value())
        return left.anchor.has_value();
      if (!left.anchor.has_value())
        return false;
      return std::tie(left.anchor->selector.fixed_target,
                      left.anchor->target_origin) <
             std::tie(right.anchor->selector.fixed_target,
                      right.anchor->target_origin);
    });
    choices_by_selector.push_back(std::move(choices));
  }

  struct CombinationSearchState {
    int score = 0;
    std::vector<std::size_t> choices;
  };
  const auto lower_priority = [](const CombinationSearchState& left,
                                 const CombinationSearchState& right) {
    if (left.score != right.score)
      return left.score < right.score;
    return left.choices > right.choices;
  };
  std::priority_queue<CombinationSearchState,
                      std::vector<CombinationSearchState>,
                      decltype(lower_priority)>
      pending(lower_priority);
  CombinationSearchState initial;
  initial.choices.resize(choices_by_selector.size(), 0U);
  for (std::size_t group = 0; group < choices_by_selector.size(); ++group)
    initial.score += choices_by_selector.at(group).front().score;
  pending.push(initial);
  std::set<std::vector<std::size_t>> visited;
  visited.insert(initial.choices);

  std::vector<std::vector<NaturalTargetAnchorCandidate>> combinations;
  std::size_t expanded_states = 0;
  const std::size_t maximum_expanded_states =
      options.maximum_subset_states >
              std::numeric_limits<std::size_t>::max() / 32U
          ? std::numeric_limits<std::size_t>::max()
          : options.maximum_subset_states * 32U;
  while (!pending.empty() && combinations.size() < options.maximum_subset_states &&
         expanded_states < maximum_expanded_states) {
    CombinationSearchState state = pending.top();
    pending.pop();
    ++expanded_states;
    std::vector<NaturalTargetAnchorCandidate> anchors;
    std::set<std::size_t> used_targets;
    bool valid = true;
    for (std::size_t group = 0; group < state.choices.size(); ++group) {
      const CombinationChoice& choice =
          choices_by_selector.at(group).at(state.choices.at(group));
      if (!choice.anchor.has_value())
        continue;
      if (!used_targets.insert(choice.anchor->target_origin).second) {
        valid = false;
        break;
      }
      anchors.push_back(*choice.anchor);
    }
    if (options.maximum_anchors > 0U && anchors.size() > options.maximum_anchors)
      valid = false;
    if (valid && !anchors.empty())
      combinations.push_back(std::move(anchors));

    for (std::size_t group = 0; group < state.choices.size(); ++group) {
      const std::size_t current = state.choices.at(group);
      if (current + 1U >= choices_by_selector.at(group).size())
        continue;
      CombinationSearchState next = state;
      next.score -= choices_by_selector.at(group).at(current).score;
      ++next.choices.at(group);
      next.score += choices_by_selector.at(group).at(next.choices.at(group)).score;
      if (visited.insert(next.choices).second)
        pending.push(std::move(next));
    }
  }
  const bool combination_search_capped = !pending.empty();
  if (combination_search_capped)
    add_reason(result.plan, "best-first multi-anchor search reached its proof-search cap");

  std::stable_sort(combinations.begin(), combinations.end(), [&](const auto& left,
                                                                  const auto& right) {
    const int left_score = score(left);
    const int right_score = score(right);
    if (left_score != right_score)
      return left_score > right_score;
    if (left.size() != right.size())
      return left.size() > right.size();
    for (std::size_t index = 0; index < left.size(); ++index) {
      const auto left_key = std::tie(left.at(index).selector.register_index,
                                     left.at(index).selector.fixed_target,
                                     left.at(index).via_trampoline,
                                     left.at(index).split_prefix_cells,
                                     left.at(index).target_origin);
      const auto right_key = std::tie(right.at(index).selector.register_index,
                                      right.at(index).selector.fixed_target,
                                      right.at(index).via_trampoline,
                                      right.at(index).split_prefix_cells,
                                      right.at(index).target_origin);
      if (left_key != right_key)
        return left_key < right_key;
    }
    return false;
  });

  std::optional<CandidateArtifact> best;
  std::size_t transparent_jump_attempts = 0;
  bool transparent_jump_search_capped = false;
  const auto consider_trial = [&](const std::vector<NaturalTargetAnchorCandidate>& trial,
                                  bool charges_search_cap) {
    if (charges_search_cap &&
        transparent_jump_attempts >= options.maximum_subset_states) {
      transparent_jump_search_capped = true;
      return;
    }
    if (best.has_value() && score(trial) < best->plan.removed_cells)
      return;
    if (charges_search_cap)
      ++transparent_jump_attempts;
    const std::optional<CandidateArtifact> candidate = try_candidate(
        logical_items, preloads, *logical_flow, options, *references, calls, trial,
        *original_trace, *original_external, &result.plan.reasons);
    if (candidate.has_value() &&
        (!best.has_value() || better_candidate(*candidate, *best))) {
      best = candidate;
    }
  };
  for (const auto& anchors : combinations) {
    if (best.has_value() && score(anchors) < best->plan.removed_cells)
      break;
    consider_trial(anchors, false);

    std::set<std::tuple<std::size_t, int>> split_variants;
    for (std::size_t lower = 0; lower < anchors.size(); ++lower) {
      const NaturalTargetAnchorCandidate& split_anchor = anchors.at(lower);
      if (split_anchor.selector.rebindable_address || split_anchor.call_sites < 2 ||
          split_anchor.selector.fixed_target < 0) {
        continue;
      }
      for (std::size_t upper = 0; upper < anchors.size(); ++upper) {
        if (lower == upper || anchors.at(upper).selector.rebindable_address ||
            anchors.at(upper).selector.fixed_target <=
                split_anchor.selector.fixed_target + 2) {
          continue;
        }
        const int prefix = anchors.at(upper).selector.fixed_target -
                           split_anchor.selector.fixed_target - 2;
        if (!split_variants.emplace(lower, prefix).second)
          continue;
        std::vector<NaturalTargetAnchorCandidate> split_trial = anchors;
        split_trial.at(lower).split_prefix_cells = prefix;
        consider_trial(split_trial, true);
      }
    }

    std::vector<std::size_t> trampoline_eligible;
    for (std::size_t anchor_index = 0; anchor_index < anchors.size(); ++anchor_index) {
      const NaturalTargetAnchorCandidate& anchor = anchors.at(anchor_index);
      if (!anchor.selector.rebindable_address && anchor.call_sites >= 2 &&
          anchor.selector.fixed_target >= 0 &&
          anchor.selector.fixed_target + 1 <=
              official_program_last_address(options.address_space_model)) {
        trampoline_eligible.push_back(anchor_index);
      }
    }
    const std::size_t variant_count = std::size_t{1} << trampoline_eligible.size();
    for (std::size_t mask = 1; mask < variant_count; ++mask) {
      std::vector<NaturalTargetAnchorCandidate> trial = anchors;
      for (std::size_t bit = 0; bit < trampoline_eligible.size(); ++bit) {
        if ((mask & (std::size_t{1} << bit)) != 0U)
          trial.at(trampoline_eligible.at(bit)).via_trampoline = true;
      }
      consider_trial(trial, true);
      if (transparent_jump_search_capped)
        break;
    }
  }
  if (transparent_jump_search_capped)
    add_reason(result.plan, "transparent-jump proof search reached its cap");
  if (!best.has_value()) {
    add_reason(result.plan,
               "no stable selector and fallthrough-closed component layout passed every proof");
    return result;
  }
  std::vector<std::string> search_reasons = std::move(result.plan.reasons);
  result.items = std::move(best->items);
  result.preloads = std::move(best->preloads);
  result.plan = std::move(best->plan);
  for (std::string& reason : search_reasons) {
    if (result.plan.reasons.size() >= options.maximum_rejection_reasons)
      break;
    add_reason(result.plan, std::move(reason));
  }
  result.applied = static_cast<int>(result.plan.calls.size());
  result.removed_cells = result.plan.removed_cells;
  return result;
}

} // namespace mkpro::core
