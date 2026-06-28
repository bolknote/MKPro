#include "mkpro/core/format.hpp"

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace mkpro {

namespace {

constexpr int kHexColumns = 8;
constexpr int kListingInlineCodeTokens = 4;

struct ListingRow {
  int address = 0;
  std::string hex;
  std::string mnemonic;
  std::optional<std::string> comment;
};

std::string replace_all(std::string value, std::string_view from, std::string_view to) {
  if (from.empty())
    return value;
  std::size_t pos = 0;
  while ((pos = value.find(from, pos)) != std::string::npos) {
    value.replace(pos, from.size(), to);
    pos += to.size();
  }
  return value;
}

std::string trim_copy(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    value.pop_back();
  return value;
}

std::string upper_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  return value;
}

bool regex_matches(std::string_view value, const std::regex& pattern) {
  return std::regex_match(value.data(), value.data() + value.size(), pattern);
}

bool is_scientific_decimal(std::string_view value) {
  static const std::regex pattern(R"(^-?\d+(?:[,.]\d+)?E-?\d{1,2}$)", std::regex_constants::icase);
  return regex_matches(value, pattern);
}

std::optional<std::string> compact_keyboard_decimal(std::string value) {
  value = trim_copy(std::move(value));
  std::replace(value.begin(), value.end(), ',', '.');
  if (is_scientific_decimal(value))
    return upper_ascii(value);

  static const std::regex integer_pattern(R"(^(-?)(\d+)$)");
  std::smatch match;
  if (!std::regex_match(value, match, integer_pattern))
    return std::nullopt;

  const std::string sign = match[1].str();
  std::string digits = match[2].str();
  while (digits.size() > 1U && digits.front() == '0')
    digits.erase(digits.begin());
  if (digits == "0")
    return std::nullopt;

  std::string significant = digits;
  while (!significant.empty() && significant.back() == '0')
    significant.pop_back();
  const int exponent = static_cast<int>(digits.size()) - 1;
  if (exponent <= 0 || exponent > 99 || significant.size() > 8U)
    return std::nullopt;

  const std::string mantissa = significant.size() == 1U
                                   ? significant
                                   : significant.substr(0, 1) + "." + significant.substr(1);
  const std::string candidate = sign + mantissa + "E" + std::to_string(exponent);
  return candidate.size() < value.size() ? std::optional<std::string>{candidate} : std::nullopt;
}

bool is_setup_literal(std::string_view value) {
  static const std::regex pattern(R"(^-?[0-9A-F_,.\-]+(?:E-?[0-9]{1,2})?$)",
                                  std::regex_constants::icase);
  return regex_matches(value, pattern);
}

std::optional<std::string> executable_setup_value(const std::string& value) {
  std::string trimmed = trim_copy(value);
  std::replace(trimmed.begin(), trimmed.end(), ',', '.');
  static const std::regex numeric_literal(R"(^-?\d+(?:[.]\d+)?(?:[eE]-?\d{1,2})?$)");
  if (std::regex_match(trimmed, numeric_literal))
    return compact_keyboard_decimal(trimmed).value_or(trimmed);

  static const std::regex formal_address_literal(R"(^[A-Fa-f][0-9A-Fa-f]$)");
  if (std::regex_match(trimmed, formal_address_literal)) {
    const int opcode = std::stoi(trimmed, nullptr, 16);
    return std::to_string(formal_address_info(opcode).actual);
  }
  return std::nullopt;
}

std::string format_setup_value(const std::string& value) {
  const std::string normalized = upper_ascii(value);
  if (const std::optional<std::string> compact = compact_keyboard_decimal(value))
    return *compact;
  if (is_scientific_decimal(normalized))
    return value;

  const bool has_hex_letter = std::any_of(normalized.begin(), normalized.end(),
                                          [](unsigned char ch) { return ch >= 'A' && ch <= 'F'; });
  const bool all_hex = std::all_of(normalized.begin(), normalized.end(), [](unsigned char ch) {
    return std::isdigit(ch) != 0 || (ch >= 'A' && ch <= 'F');
  });
  if (!all_hex || !has_hex_letter)
    return value;

  std::string formatted;
  for (char ch : normalized) {
    switch (ch) {
    case 'A':
      formatted += "-";
      break;
    case 'B':
      formatted += "L";
      break;
    case 'C':
      formatted += "С";
      break;
    case 'D':
      formatted += "Г";
      break;
    case 'E':
      formatted += "Е";
      break;
    case 'F':
      formatted += "_";
      break;
    default:
      formatted.push_back(ch);
      break;
    }
  }
  return formatted;
}

struct ExecutableSetupPreloadEntry {
  PreloadReport preload;
  std::string value;
};

struct ExecutableSetupPreloadGroup {
  std::string value;
  std::vector<PreloadReport> preloads;
};

std::vector<ExecutableSetupPreloadEntry>
executable_setup_preload_entries(const std::vector<PreloadReport>& preloads) {
  std::vector<ExecutableSetupPreloadEntry> entries;
  for (const PreloadReport& preload : preloads) {
    const std::optional<std::string> value = executable_setup_value(preload.value);
    if (value.has_value())
      entries.push_back(ExecutableSetupPreloadEntry{.preload = preload, .value = *value});
  }
  return entries;
}

std::vector<ExecutableSetupPreloadGroup>
group_setup_preloads_by_executable_value(const std::vector<ExecutableSetupPreloadEntry>& entries) {
  std::set<std::size_t> consumed;
  std::vector<ExecutableSetupPreloadGroup> groups;
  for (std::size_t index = 0; index < entries.size(); ++index) {
    if (consumed.contains(index))
      continue;
    const ExecutableSetupPreloadEntry& entry = entries.at(index);
    std::vector<PreloadReport> preloads = {entry.preload};
    consumed.insert(index);
    for (std::size_t other = index + 1U; other < entries.size(); ++other) {
      if (consumed.contains(other))
        continue;
      const ExecutableSetupPreloadEntry& candidate = entries.at(other);
      if (candidate.value != entry.value)
        continue;
      preloads.push_back(candidate.preload);
      consumed.insert(other);
    }
    groups.push_back(ExecutableSetupPreloadGroup{
        .value = entry.value,
        .preloads = std::move(preloads),
    });
  }
  return groups;
}

std::string format_step_address(int address) {
  try {
    return format_address(address);
  } catch (const std::exception&) {
    std::ostringstream out;
    out << ">" << std::uppercase << std::hex << address;
    return out.str();
  }
}

std::size_t utf8_codepoint_count(std::string_view value) {
  std::size_t count = 0;
  for (char raw : value) {
    const auto ch = static_cast<unsigned char>(raw);
    if ((ch & 0xc0U) != 0x80U)
      ++count;
  }
  return count;
}

std::string pad_end(std::string value, std::size_t width) {
  while (utf8_codepoint_count(value) < width)
    value.push_back(' ');
  return value;
}

std::string pad_start(std::string value, std::size_t width) {
  while (utf8_codepoint_count(value) < width)
    value.insert(value.begin(), ' ');
  return value;
}

std::string repeat_text(std::string_view unit, std::size_t count) {
  std::string result;
  for (std::size_t index = 0; index < count; ++index)
    result += unit;
  return result;
}

std::string abbreviate(std::string value, std::size_t width) {
  if (utf8_codepoint_count(value) <= width)
    return value;
  std::string result;
  std::size_t count = 0;
  for (std::size_t index = 0; index < value.size();) {
    const auto ch = static_cast<unsigned char>(value[index]);
    const std::size_t next = (ch & 0x80U) == 0U   ? index + 1U
                             : (ch & 0xe0U) == 0xc0U ? index + 2U
                             : (ch & 0xf0U) == 0xe0U ? index + 3U
                                                     : index + 4U;
    if (count + 3U >= width)
      break;
    result.append(value, index, std::min(next, value.size()) - index);
    index = std::min(next, value.size());
    ++count;
  }
  return result + "...";
}

int step_opcode(const ResolvedStep& step) {
  if (step.opcode != 0 || upper_ascii(step.hex) == "00")
    return step.opcode;
  try {
    return std::stoi(step.hex, nullptr, 16);
  } catch (const std::exception&) {
    return step.opcode;
  }
}

bool is_direct_conditional_opcode(int opcode) {
  return opcode >= 0x57 && opcode <= 0x5e;
}

bool is_indirect_conditional_opcode(int opcode) {
  const int base = opcode & 0xf0;
  return base == 0x70 || base == 0x90 || base == 0xc0 || base == 0xe0;
}

bool is_indirect_jump_opcode(int opcode) {
  return (opcode & 0xf0) == 0x80;
}

bool is_indirect_call_opcode(int opcode) {
  return (opcode & 0xf0) == 0xa0;
}

bool is_flow_control_opcode(int opcode) {
  return opcode == 0x50 || opcode == 0x51 || opcode == 0x52 || opcode == 0x53 ||
         is_direct_conditional_opcode(opcode) || is_indirect_conditional_opcode(opcode) ||
         is_indirect_jump_opcode(opcode) || is_indirect_call_opcode(opcode);
}

enum class FlowTone {
  Normal,
  Branch,
  Call,
  Return,
  Stop,
  Unknown,
  Muted,
};

enum class FlowArrow {
  Down,
  Forward,
  Back,
  Unknown,
};

struct FlowContext {
  std::map<std::string, std::string> preloaded_registers;
  std::map<int, int> indirect_targets;
};

struct FlowInstruction {
  int address = 0;
  std::size_t step_index = 0;
  int opcode = 0;
  std::optional<std::size_t> operand_index;
  std::optional<int> target;
};

struct FlowEdge {
  std::string label;
  std::optional<int> target;
  std::optional<std::string> detail;
  FlowArrow arrow = FlowArrow::Unknown;
  FlowTone tone = FlowTone::Normal;
};

struct FlowBlock {
  int start = 0;
  int end = 0;
  std::vector<FlowInstruction> instructions;
  std::vector<FlowEdge> edges;
  FlowTone tone = FlowTone::Normal;
  bool reachable = true;
};

std::string ansi_code(FlowTone tone) {
  switch (tone) {
    case FlowTone::Normal:
      return "\033[32m";
    case FlowTone::Branch:
      return "\033[33m";
    case FlowTone::Call:
      return "\033[34m";
    case FlowTone::Return:
      return "\033[35m";
    case FlowTone::Stop:
      return "\033[31m";
    case FlowTone::Unknown:
      return "\033[91m";
    case FlowTone::Muted:
      return "\033[2m";
  }
  return "";
}

std::string colorize(std::string value, FlowTone tone, bool color) {
  if (!color)
    return value;
  return ansi_code(tone) + value + "\033[0m";
}

std::string flow_ref(int address) {
  return "[" + format_step_address(address) + "]";
}

std::string flow_register_name(int opcode) {
  static constexpr std::array<std::string_view, 15> kRegisters = {
      "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e",
  };
  const int index = opcode & 0x0f;
  if (index >= 0 && index <= 0x0e)
    return std::string{kRegisters[static_cast<std::size_t>(index)]};
  return "0";
}

std::optional<int> parse_indirect_target_comment(const ResolvedStep& step) {
  if (!step.comment.has_value())
    return std::nullopt;
  static const std::regex pattern(R"(indirect-target=([0-9]+))");
  std::smatch match;
  if (!std::regex_search(*step.comment, match, pattern))
    return std::nullopt;
  try {
    return std::stoi(match[1].str());
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<int> parse_preloaded_formal_address(const std::string& value) {
  const std::optional<int> opcode = parse_formal_address_opcode(value);
  if (!opcode.has_value())
    return std::nullopt;
  return code_to_address(*opcode);
}

FlowContext build_flow_context(const std::vector<ResolvedStep>& steps,
                               const std::vector<PreloadReport>& preloads) {
  FlowContext context;
  for (const PreloadReport& preload : preloads)
    context.preloaded_registers[preload.register_name] = preload.value;
  for (const ResolvedStep& step : steps) {
    if (const std::optional<int> target = parse_indirect_target_comment(step))
      context.indirect_targets[step.address] = *target;
  }
  return context;
}

std::optional<int> known_indirect_target(const FlowInstruction& instruction,
                                         const FlowContext& context) {
  const auto comment_target = context.indirect_targets.find(instruction.address);
  if (comment_target != context.indirect_targets.end())
    return comment_target->second;

  const auto preload = context.preloaded_registers.find(flow_register_name(instruction.opcode));
  if (preload == context.preloaded_registers.end())
    return std::nullopt;
  return parse_preloaded_formal_address(preload->second);
}

std::optional<std::string> indirect_detail(const FlowInstruction& instruction,
                                           const FlowContext& context) {
  const std::string reg = flow_register_name(instruction.opcode);
  const auto preload = context.preloaded_registers.find(reg);
  if (preload == context.preloaded_registers.end())
    return "R" + reg;
  return "R" + reg + "=" + preload->second;
}

FlowArrow target_arrow(int source, int target) {
  if (target == source)
    return FlowArrow::Back;
  return target > source ? FlowArrow::Forward : FlowArrow::Back;
}

std::optional<FlowInstruction> decode_flow_instruction(
    const std::vector<ResolvedStep>& steps, const std::map<int, std::size_t>& by_address,
    int address) {
  const auto it = by_address.find(address);
  if (it == by_address.end())
    return std::nullopt;

  const ResolvedStep& step = steps[it->second];
  FlowInstruction instruction{
      .address = address,
      .step_index = it->second,
      .opcode = step_opcode(step),
  };

  const OpcodeInfo& info = opcode_by_code(instruction.opcode);
  if (info.takes_address && it->second + 1U < steps.size()) {
    instruction.operand_index = it->second + 1U;
    instruction.target = code_to_address(step_opcode(steps[it->second + 1U]));
  }
  return instruction;
}

std::optional<int> address_after_instruction(const std::vector<ResolvedStep>& steps,
                                             const FlowInstruction& instruction) {
  const std::size_t next =
      instruction.operand_index.has_value() ? *instruction.operand_index + 1U
                                            : instruction.step_index + 1U;
  if (next >= steps.size())
    return std::nullopt;
  return steps[next].address;
}

std::string conditional_taken_label(const std::string& mnemonic) {
  return mnemonic.find('x') != std::string::npos || mnemonic.find('X') != std::string::npos
             ? "yes"
             : "taken";
}

std::string conditional_fallthrough_label(const std::string& mnemonic) {
  return mnemonic.find('x') != std::string::npos || mnemonic.find('X') != std::string::npos
             ? "no"
             : "next";
}

std::vector<FlowEdge> instruction_edges(const std::vector<ResolvedStep>& steps,
                                        const FlowInstruction& instruction,
                                        const FlowContext& context) {
  const ResolvedStep& step = steps[instruction.step_index];
  const std::optional<int> next = address_after_instruction(steps, instruction);
  const int opcode = instruction.opcode;
  std::vector<FlowEdge> edges;

  auto append_next = [&](std::string label, FlowTone tone) {
    if (next.has_value())
      edges.push_back(FlowEdge{
          .label = std::move(label),
          .target = next,
          .arrow = FlowArrow::Down,
          .tone = tone,
      });
  };
  auto append_target = [&](std::string label, FlowTone tone) {
    if (instruction.target.has_value()) {
      edges.push_back(FlowEdge{
          .label = std::move(label),
          .target = instruction.target,
          .arrow = target_arrow(instruction.address, *instruction.target),
          .tone = tone,
      });
    } else {
      edges.push_back(FlowEdge{
          .label = std::move(label),
          .arrow = FlowArrow::Unknown,
          .tone = FlowTone::Unknown,
      });
    }
  };
  auto append_indirect = [&](std::string label, FlowTone tone) {
    const std::optional<int> target = known_indirect_target(instruction, context);
    FlowArrow arrow = FlowArrow::Unknown;
    if (target.has_value())
      arrow = target_arrow(instruction.address, *target);
    edges.push_back(FlowEdge{
        .label = std::move(label),
        .target = target,
        .detail = indirect_detail(instruction, context),
        .arrow = arrow,
        .tone = target.has_value() ? tone : FlowTone::Unknown,
    });
  };

  if (opcode == 0x51) {
    append_target("jump", FlowTone::Branch);
  } else if (opcode == 0x53) {
    append_target("call", FlowTone::Call);
    append_next("after", FlowTone::Muted);
  } else if (opcode == 0x52) {
    return edges;
  } else if (opcode == 0x50) {
    append_next("resume", FlowTone::Muted);
  } else if (is_direct_conditional_opcode(opcode)) {
    append_target(conditional_taken_label(step.mnemonic), FlowTone::Branch);
    append_next(conditional_fallthrough_label(step.mnemonic), FlowTone::Branch);
  } else if (is_indirect_jump_opcode(opcode)) {
    append_indirect("jump", FlowTone::Branch);
  } else if (is_indirect_call_opcode(opcode)) {
    append_indirect("call", FlowTone::Call);
    append_next("after", FlowTone::Muted);
  } else if (is_indirect_conditional_opcode(opcode)) {
    append_indirect(conditional_taken_label(step.mnemonic), FlowTone::Branch);
    append_next(conditional_fallthrough_label(step.mnemonic), FlowTone::Branch);
  } else {
    append_next("next", FlowTone::Muted);
  }
  return edges;
}

FlowTone instruction_tone(const FlowInstruction& instruction) {
  const int opcode = instruction.opcode;
  if (opcode == 0x50)
    return FlowTone::Stop;
  if (opcode == 0x52)
    return FlowTone::Return;
  if (opcode == 0x53 || is_indirect_call_opcode(opcode))
    return FlowTone::Call;
  if (is_direct_conditional_opcode(opcode) || is_indirect_conditional_opcode(opcode))
    return FlowTone::Branch;
  if (opcode == 0x51 || is_indirect_jump_opcode(opcode))
    return FlowTone::Branch;
  return FlowTone::Normal;
}

FlowBlock decode_flow_block(const std::vector<ResolvedStep>& steps,
                            const std::map<int, std::size_t>& by_address,
                            const std::set<int>& leaders, const FlowContext& context, int start) {
  FlowBlock block{.start = start, .end = start};
  std::set<int> seen;
  std::optional<int> cursor = start;
  while (cursor.has_value() && !seen.contains(*cursor)) {
    seen.insert(*cursor);
    std::optional<FlowInstruction> instruction =
        decode_flow_instruction(steps, by_address, *cursor);
    if (!instruction.has_value())
      break;

    block.end = instruction->address;
    if (instruction->operand_index.has_value())
      block.end = steps[*instruction->operand_index].address;
    block.tone = instruction_tone(*instruction);
    block.instructions.push_back(*instruction);

    if (is_flow_control_opcode(instruction->opcode))
      break;

    std::optional<int> next = address_after_instruction(steps, *instruction);
    if (!next.has_value() || (leaders.contains(*next) && *next != start))
      break;
    cursor = next;
  }

  if (!block.instructions.empty())
    block.edges = instruction_edges(steps, block.instructions.back(), context);
  return block;
}

std::vector<FlowBlock> build_flow_blocks(const std::vector<ResolvedStep>& steps,
                                         const FlowContext& context) {
  if (steps.empty())
    return {};

  std::map<int, std::size_t> by_address;
  for (std::size_t index = 0; index < steps.size(); ++index)
    by_address[steps[index].address] = index;

  std::set<int> leaders{steps.front().address};
  bool changed = true;
  while (changed) {
    changed = false;
    const std::vector<int> snapshot(leaders.begin(), leaders.end());
    for (int leader : snapshot) {
      const FlowBlock block = decode_flow_block(steps, by_address, leaders, context, leader);
      for (const FlowEdge& edge : block.edges) {
        if (edge.target.has_value() && by_address.contains(*edge.target) &&
            leaders.insert(*edge.target).second) {
          changed = true;
        }
      }
    }
  }

  const std::set<int> reachable = leaders;
  std::vector<FlowBlock> blocks;
  std::set<int> covered_all_addresses;
  for (int leader : leaders) {
    FlowBlock block = decode_flow_block(steps, by_address, leaders, context, leader);
    if (block.instructions.empty())
      continue;
    for (const FlowInstruction& instruction : block.instructions) {
      covered_all_addresses.insert(instruction.address);
      if (instruction.operand_index.has_value())
        covered_all_addresses.insert(steps[*instruction.operand_index].address);
    }
    blocks.push_back(std::move(block));
  }

  for (std::size_t index = 0; index < steps.size();) {
    const int address = steps[index].address;
    if (!covered_all_addresses.contains(address)) {
      leaders.insert(address);
      FlowBlock block = decode_flow_block(steps, by_address, leaders, context, address);
      block.reachable = reachable.contains(address);
      if (!block.reachable)
        block.tone = FlowTone::Muted;
      blocks.push_back(std::move(block));
    }
    const std::optional<FlowInstruction> instruction =
        decode_flow_instruction(steps, by_address, address);
    if (instruction.has_value() && instruction->operand_index.has_value())
      index = *instruction->operand_index + 1U;
    else
      ++index;
  }

  for (FlowBlock& block : blocks) {
    block.reachable = reachable.contains(block.start);
    if (!block.reachable)
      block.tone = FlowTone::Muted;
  }
  std::sort(blocks.begin(), blocks.end(), [](const FlowBlock& a, const FlowBlock& b) {
    if (a.start != b.start)
      return a.start < b.start;
    return a.end < b.end;
  });
  return blocks;
}

std::string flow_role(const FlowBlock& block) {
  if (!block.reachable)
    return "entryless";
  if (block.instructions.empty())
    return "empty";
  const int opcode = block.instructions.back().opcode;
  if (opcode == 0x50)
    return "pause";
  if (opcode == 0x52)
    return "return";
  if (opcode == 0x51 || is_indirect_jump_opcode(opcode))
    return "jump";
  if (opcode == 0x53 || is_indirect_call_opcode(opcode))
    return "call";
  if (is_direct_conditional_opcode(opcode) || is_indirect_conditional_opcode(opcode))
    return "branch";
  return "linear";
}

std::string flow_block_title(const FlowBlock& block) {
  std::string range = flow_ref(block.start);
  if (block.end != block.start)
    range = "[" + format_step_address(block.start) + ".." + format_step_address(block.end) + "]";
  return range + " " + flow_role(block);
}

std::vector<std::string> utf8_cells(std::string_view value) {
  std::vector<std::string> cells;
  for (std::size_t index = 0; index < value.size();) {
    const auto ch = static_cast<unsigned char>(value[index]);
    const std::size_t next = (ch & 0x80U) == 0U      ? index + 1U
                             : (ch & 0xe0U) == 0xc0U ? index + 2U
                             : (ch & 0xf0U) == 0xe0U ? index + 3U
                                                     : index + 4U;
    cells.emplace_back(value.substr(index, std::min(next, value.size()) - index));
    index = std::min(next, value.size());
  }
  return cells;
}

struct FlowCanvasCell {
  std::string text = " ";
  std::optional<FlowTone> tone;
};

struct FlowCanvas {
  int width = 0;
  int height = 0;
  std::vector<std::vector<FlowCanvasCell>> cells;

  FlowCanvas(int width_in, int height_in)
      : width(width_in), height(height_in),
        cells(static_cast<std::size_t>(height_in),
              std::vector<FlowCanvasCell>(static_cast<std::size_t>(width_in))) {}

  bool contains(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }

  static constexpr int kNorth = 1;
  static constexpr int kSouth = 2;
  static constexpr int kWest = 4;
  static constexpr int kEast = 8;

  static int line_bits(const std::string& value) {
    if (value == "\u2500")
      return kWest | kEast;
    if (value == "\u2502")
      return kNorth | kSouth;
    if (value == "\u256d")
      return kEast | kSouth;
    if (value == "\u256e")
      return kWest | kSouth;
    if (value == "\u2570")
      return kNorth | kEast;
    if (value == "\u256f")
      return kNorth | kWest;
    if (value == "\u251c")
      return kNorth | kSouth | kEast;
    if (value == "\u2524")
      return kNorth | kSouth | kWest;
    if (value == "\u252c")
      return kWest | kEast | kSouth;
    if (value == "\u2534")
      return kWest | kEast | kNorth;
    if (value == "\u253c")
      return kNorth | kSouth | kWest | kEast;
    return 0;
  }

  static std::string line_from_bits(int bits) {
    switch (bits) {
    case kWest:
    case kEast:
    case kWest | kEast:
      return "\u2500";
    case kNorth:
    case kSouth:
    case kNorth | kSouth:
      return "\u2502";
    case kEast | kSouth:
      return "\u256d";
    case kWest | kSouth:
      return "\u256e";
    case kNorth | kEast:
      return "\u2570";
    case kNorth | kWest:
      return "\u256f";
    case kNorth | kSouth | kEast:
      return "\u251c";
    case kNorth | kSouth | kWest:
      return "\u2524";
    case kWest | kEast | kSouth:
      return "\u252c";
    case kWest | kEast | kNorth:
      return "\u2534";
    case kNorth | kSouth | kWest | kEast:
      return "\u253c";
    default:
      return " ";
    }
  }

  void set(int x, int y, const std::string& value,
           std::optional<FlowTone> tone = std::nullopt) {
    if (!contains(x, y))
      return;
    FlowCanvasCell& target = cells[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
    target.text = value;
    target.tone = tone;
  }

  void add_line_bits(int x, int y, int bits, FlowTone tone) {
    if (!contains(x, y) || bits == 0)
      return;
    FlowCanvasCell& target = cells[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
    if (target.text != " " && line_bits(target.text) == 0)
      return;
    target.text = line_from_bits(line_bits(target.text) | bits);
    if (!target.tone.has_value())
      target.tone = tone;
  }

  void put_text(int x, int y, std::string_view value,
                std::optional<FlowTone> tone = std::nullopt) {
    int cursor = x;
    for (const std::string& cell : utf8_cells(value))
      set(cursor++, y, cell, tone);
  }

  void draw_h(int x1, int x2, int y, FlowTone tone) {
    if (x1 == x2)
      return;
    if (x1 > x2)
      std::swap(x1, x2);
    for (int x = x1; x <= x2; ++x) {
      int bits = 0;
      if (x > x1)
        bits |= kWest;
      if (x < x2)
        bits |= kEast;
      add_line_bits(x, y, bits, tone);
    }
  }

  void draw_v(int x, int y1, int y2, FlowTone tone) {
    if (y1 == y2) {
      add_line_bits(x, y1, kNorth | kSouth, tone);
      return;
    }
    if (y1 > y2)
      std::swap(y1, y2);
    for (int y = y1; y <= y2; ++y) {
      int bits = 0;
      if (y > y1)
        bits |= kNorth;
      if (y < y2)
        bits |= kSouth;
      add_line_bits(x, y, bits, tone);
    }
  }

  std::string str(bool color) const {
    std::ostringstream out;
    for (int y = 0; y < height; ++y) {
      if (y > 0)
        out << '\n';
      int last = width - 1;
      while (last >= 0 &&
             cells[static_cast<std::size_t>(y)][static_cast<std::size_t>(last)].text == " ")
        --last;
      std::optional<FlowTone> active_tone;
      for (int x = 0; x <= last; ++x) {
        const FlowCanvasCell& cell =
            cells[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
        const std::optional<FlowTone> next_tone = cell.tone;
        if (color && next_tone != active_tone) {
          if (active_tone.has_value())
            out << "\033[0m";
          if (next_tone.has_value())
            out << ansi_code(*next_tone);
          active_tone = next_tone;
        }
        out << cell.text;
      }
      if (color && active_tone.has_value())
        out << "\033[0m";
    }
    return out.str();
  }
};

struct FlowGraphNode {
  int id = 0;
  const FlowBlock* block = nullptr;
  std::optional<FlowEdge> pseudo_edge;
  std::string title;
  std::vector<std::string> lines;
  FlowTone tone = FlowTone::Normal;
  bool reachable = true;
  int sort_key = 0;
  int rank = -1;
  int column = 0;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct FlowGraphEdge {
  int source = 0;
  int target = 0;
  FlowEdge edge;
};

struct FlowPendingLabel {
  int x = 0;
  int y = 0;
  std::string label;
  FlowTone tone = FlowTone::Normal;
};

struct FlowBorderConnector {
  int x = 0;
  int y = 0;
  int bits = 0;
  FlowTone tone = FlowTone::Normal;
};

struct FlowPoint {
  int x = 0;
  int y = 0;
};

enum class FlowPortSide {
  Top,
  Right,
  Bottom,
  Left,
};

struct FlowPort {
  FlowPoint point;
  FlowPoint stem;
  int border_x = 0;
  int border_y = 0;
  int border_bits = 0;
  int stem_to_block_bits = 0;
  int approach_dir = -1;
  int preference = 0;
  FlowPortSide side = FlowPortSide::Right;
  std::string arrow;
};

struct FlowSearchResult {
  std::vector<FlowPoint> points;
  int cost = 0;
};

struct FlowRouteResult {
  FlowPort source;
  FlowPort target;
  std::vector<FlowPoint> points;
  int cost = 0;
};

std::string compact_flow_token(const std::vector<ResolvedStep>& steps,
                               const FlowInstruction& instruction) {
  const ResolvedStep& step = steps[instruction.step_index];
  std::ostringstream out;
  out << to_keycaps(step.mnemonic);
  if (instruction.operand_index.has_value() && instruction.target.has_value())
    out << " " << format_step_address(*instruction.target);
  return out.str();
}

std::vector<std::string> wrap_flow_tokens(const std::vector<std::string>& tokens,
                                          std::size_t width) {
  if (tokens.empty())
    return {""};

  std::vector<std::string> normalized;
  normalized.reserve(tokens.size());
  for (std::string token : tokens)
    normalized.push_back(abbreviate(std::move(token), width));

  constexpr std::size_t kGap = 3;
  const std::size_t max_columns = std::min<std::size_t>(3, normalized.size());
  for (std::size_t columns = max_columns; columns > 1; --columns) {
    std::vector<std::size_t> column_widths(columns, 0);
    for (std::size_t index = 0; index < normalized.size(); ++index) {
      const std::size_t column = index % columns;
      column_widths[column] =
          std::max(column_widths[column], utf8_codepoint_count(normalized[index]));
    }

    std::size_t total_width = (columns - 1U) * kGap;
    for (const std::size_t column_width : column_widths)
      total_width += column_width;
    if (total_width > width)
      continue;

    std::vector<std::string> lines;
    for (std::size_t row = 0; row * columns < normalized.size(); ++row) {
      std::string line;
      for (std::size_t column = 0; column < columns; ++column) {
        const std::size_t index = row * columns + column;
        if (index >= normalized.size())
          break;
        if (column > 0)
          line += repeat_text(" ", kGap);
        line += pad_end(normalized[index], column_widths[column]);
      }
      lines.push_back(std::move(line));
    }
    return lines;
  }

  std::vector<std::string> lines;
  for (const std::string& token : normalized)
    lines.push_back(abbreviate(token, width));
  return lines;
}

std::vector<std::string> compact_block_lines(const FlowBlock& block,
                                             const std::vector<ResolvedStep>& steps,
                                             std::size_t width) {
  std::vector<std::string> tokens;
  for (const FlowInstruction& instruction : block.instructions)
    tokens.push_back(compact_flow_token(steps, instruction));
  return wrap_flow_tokens(tokens, width);
}

std::string pseudo_node_title(const FlowEdge& edge) {
  std::string title = "[?] " + edge.label;
  if (edge.detail.has_value())
    title += " " + *edge.detail;
  return title;
}

std::string graph_edge_label(const FlowEdge& edge) {
  if (edge.label == "next" || edge.label == "resume" || edge.label == "after")
    return "";
  if (edge.label == "yes" || edge.label == "no")
    return edge.label;
  if (edge.label == "taken")
    return "yes";
  return "";
}

void draw_flow_node(FlowCanvas& canvas, const FlowGraphNode& node) {
  const int inner = node.width - 2;
  const int content_width = inner - 4;
  const std::string title = abbreviate(node.title, static_cast<std::size_t>(inner - 1));
  const int title_width = static_cast<int>(utf8_codepoint_count(title));
  const int title_fill = std::max(0, inner - 1 - title_width);
  canvas.put_text(node.x, node.y, "\u256d\u2500" + title +
                                      repeat_text("\u2500", static_cast<std::size_t>(title_fill)) +
                                      "\u256e",
                  node.tone);
  for (std::size_t index = 0; index < node.lines.size(); ++index) {
    const std::string line = abbreviate(node.lines[index], static_cast<std::size_t>(content_width));
    canvas.put_text(node.x, node.y + 1 + static_cast<int>(index),
                    "\u2502  " + pad_end(line, static_cast<std::size_t>(content_width)) +
                        "  \u2502",
                    node.tone);
  }
  canvas.put_text(node.x, node.y + node.height - 1,
                  "\u2570" + repeat_text("\u2500", static_cast<std::size_t>(inner)) + "\u256f",
                  node.tone);
}

int flow_label_width(const std::string& label) {
  if (label.empty())
    return 0;
  return static_cast<int>(utf8_codepoint_count("\u2500 " + label + " \u2500"));
}

bool can_put_flow_edge_label(const FlowCanvas& canvas, int x, int y,
                             const std::string& label) {
  if (label.empty())
    return false;
  int cursor = x;
  const std::vector<std::string> cells = utf8_cells("\u2500 " + label + " \u2500");
  for (std::size_t index = 0; index < cells.size(); ++index) {
    if (!canvas.contains(cursor, y))
      return false;
    const FlowCanvasCell& target =
        canvas.cells[static_cast<std::size_t>(y)][static_cast<std::size_t>(cursor)];
    if (target.text != " " && FlowCanvas::line_bits(target.text) == 0)
      return false;
    ++cursor;
  }
  return true;
}

bool put_flow_edge_label(FlowCanvas& canvas, int x, int y, const std::string& label,
                         FlowTone tone) {
  if (label.empty())
    return false;
  if (!can_put_flow_edge_label(canvas, x, y, label))
    return false;
  canvas.put_text(x, y, "\u2500 " + label + " \u2500", tone);
  return true;
}

void queue_flow_edge_label(std::vector<FlowPendingLabel>& labels, int x, int y,
                           const std::string& label, FlowTone tone) {
  if (label.empty())
    return;
  labels.push_back(FlowPendingLabel{.x = x, .y = y, .label = label, .tone = tone});
}

void queue_flow_border_connector(std::vector<FlowBorderConnector>& connectors, int x,
                                 int y, int bits, FlowTone tone) {
  if (bits == 0)
    return;
  connectors.push_back(FlowBorderConnector{.x = x, .y = y, .bits = bits, .tone = tone});
}

int flow_dir_bit(int dir) {
  switch (dir) {
  case 0:
    return FlowCanvas::kNorth;
  case 1:
    return FlowCanvas::kEast;
  case 2:
    return FlowCanvas::kSouth;
  case 3:
    return FlowCanvas::kWest;
  default:
    return 0;
  }
}

int flow_opposite_dir_bit(int dir) {
  switch (dir) {
  case 0:
    return FlowCanvas::kSouth;
  case 1:
    return FlowCanvas::kWest;
  case 2:
    return FlowCanvas::kNorth;
  case 3:
    return FlowCanvas::kEast;
  default:
    return 0;
  }
}

int flow_direction_between(const FlowPoint& from, const FlowPoint& to) {
  if (to.x == from.x && to.y == from.y - 1)
    return 0;
  if (to.x == from.x + 1 && to.y == from.y)
    return 1;
  if (to.x == from.x && to.y == from.y + 1)
    return 2;
  if (to.x == from.x - 1 && to.y == from.y)
    return 3;
  return -1;
}

bool flow_same_point(const FlowPoint& left, const FlowPoint& right) {
  return left.x == right.x && left.y == right.y;
}

int flow_point_index(int width, const FlowPoint& point) { return point.y * width + point.x; }

int flow_node_middle_y(const FlowGraphNode& node) {
  return node.y + std::max(1, (node.height - 1) / 2);
}

int flow_source_side_y(const FlowGraphNode& node, const FlowEdge& edge) {
  const int middle = flow_node_middle_y(node);
  if (edge.label == "no")
    return std::min(node.y + node.height - 2, middle + 1);
  return middle;
}

FlowPort make_flow_source_port(const FlowGraphNode& node, FlowPortSide side, int side_y,
                               int label_width, int preference) {
  const int center_x = node.x + node.width / 2;
  FlowPort port;
  port.side = side;
  port.preference = preference;
  switch (side) {
  case FlowPortSide::Left:
    port.stem = FlowPoint{.x = node.x - 1, .y = side_y};
    port.point = label_width > 0 ? FlowPoint{.x = node.x - label_width - 2, .y = side_y}
                                 : port.stem;
    port.border_x = node.x;
    port.border_y = side_y;
    port.border_bits = FlowCanvas::kWest;
    port.stem_to_block_bits = FlowCanvas::kEast;
    break;
  case FlowPortSide::Right:
    port.stem = FlowPoint{.x = node.x + node.width, .y = side_y};
    port.point = label_width > 0
                     ? FlowPoint{.x = node.x + node.width + label_width + 1, .y = side_y}
                     : port.stem;
    port.border_x = node.x + node.width - 1;
    port.border_y = side_y;
    port.border_bits = FlowCanvas::kEast;
    port.stem_to_block_bits = FlowCanvas::kWest;
    break;
  case FlowPortSide::Top:
    port.stem = FlowPoint{.x = center_x, .y = node.y - 1};
    port.point = port.stem;
    port.border_x = center_x;
    port.border_y = node.y;
    port.border_bits = 0;
    port.stem_to_block_bits = FlowCanvas::kSouth;
    break;
  case FlowPortSide::Bottom:
    port.stem = FlowPoint{.x = center_x, .y = node.y + node.height};
    port.point = port.stem;
    port.border_x = center_x;
    port.border_y = node.y + node.height - 1;
    port.border_bits = 0;
    port.stem_to_block_bits = FlowCanvas::kNorth;
    break;
  }
  return port;
}

FlowPort make_flow_target_port(const FlowGraphNode& node, FlowPortSide side,
                               int preference) {
  const int center_x = node.x + node.width / 2;
  const int middle_y = flow_node_middle_y(node);
  FlowPort port;
  port.side = side;
  port.preference = preference;
  switch (side) {
  case FlowPortSide::Left:
    port.point = FlowPoint{.x = node.x - 1, .y = middle_y};
    port.stem = port.point;
    port.border_x = node.x;
    port.border_y = middle_y;
    port.border_bits = FlowCanvas::kWest;
    port.approach_dir = 1;
    port.arrow = "\u25b6";
    break;
  case FlowPortSide::Right:
    port.point = FlowPoint{.x = node.x + node.width, .y = middle_y};
    port.stem = port.point;
    port.border_x = node.x + node.width - 1;
    port.border_y = middle_y;
    port.border_bits = FlowCanvas::kEast;
    port.approach_dir = 3;
    port.arrow = "\u25c0";
    break;
  case FlowPortSide::Top:
    port.point = FlowPoint{.x = center_x, .y = node.y - 1};
    port.stem = port.point;
    port.border_x = center_x;
    port.border_y = node.y;
    port.border_bits = 0;
    port.approach_dir = 2;
    port.arrow = "\u25bc";
    break;
  case FlowPortSide::Bottom:
    port.point = FlowPoint{.x = center_x, .y = node.y + node.height};
    port.stem = port.point;
    port.border_x = center_x;
    port.border_y = node.y + node.height - 1;
    port.border_bits = 0;
    port.approach_dir = 0;
    port.arrow = "\u25b2";
    break;
  }
  return port;
}

void append_unique_flow_side(std::vector<FlowPortSide>& sides, FlowPortSide side) {
  if (std::find(sides.begin(), sides.end(), side) == sides.end())
    sides.push_back(side);
}

std::vector<FlowPortSide> flow_source_side_order(const FlowGraphNode& source,
                                                 const FlowGraphNode& target,
                                                 const std::string& label) {
  std::vector<FlowPortSide> sides;
  if (target.rank < source.rank) {
    append_unique_flow_side(sides, target.column > source.column ? FlowPortSide::Right
                                                                 : FlowPortSide::Left);
    append_unique_flow_side(sides, FlowPortSide::Top);
  } else if (target.rank == source.rank) {
    append_unique_flow_side(sides, target.x >= source.x ? FlowPortSide::Right
                                                        : FlowPortSide::Left);
    append_unique_flow_side(sides, FlowPortSide::Bottom);
    append_unique_flow_side(sides, FlowPortSide::Top);
  } else if (!label.empty() && target.column == source.column) {
    append_unique_flow_side(sides, source.column == 0 ? FlowPortSide::Left
                                                      : FlowPortSide::Right);
    append_unique_flow_side(sides, FlowPortSide::Bottom);
  } else {
    append_unique_flow_side(sides, target.x >= source.x ? FlowPortSide::Right
                                                        : FlowPortSide::Left);
    append_unique_flow_side(sides, FlowPortSide::Bottom);
  }
  append_unique_flow_side(sides, FlowPortSide::Right);
  append_unique_flow_side(sides, FlowPortSide::Left);
  append_unique_flow_side(sides, FlowPortSide::Bottom);
  append_unique_flow_side(sides, FlowPortSide::Top);
  return sides;
}

std::vector<FlowPortSide> flow_target_side_order(const FlowGraphNode& source,
                                                 const FlowGraphNode& target) {
  std::vector<FlowPortSide> sides;
  if (target.rank > source.rank) {
    append_unique_flow_side(sides, FlowPortSide::Top);
    append_unique_flow_side(sides, target.x >= source.x ? FlowPortSide::Left
                                                        : FlowPortSide::Right);
  } else if (target.rank < source.rank) {
    append_unique_flow_side(sides, target.column > source.column ? FlowPortSide::Right
                                                                 : FlowPortSide::Left);
    append_unique_flow_side(sides, FlowPortSide::Bottom);
  } else {
    append_unique_flow_side(sides, target.x >= source.x ? FlowPortSide::Left
                                                        : FlowPortSide::Right);
    append_unique_flow_side(sides, FlowPortSide::Top);
    append_unique_flow_side(sides, FlowPortSide::Bottom);
  }
  append_unique_flow_side(sides, FlowPortSide::Left);
  append_unique_flow_side(sides, FlowPortSide::Right);
  append_unique_flow_side(sides, FlowPortSide::Top);
  append_unique_flow_side(sides, FlowPortSide::Bottom);
  return sides;
}

bool flow_point_allowed(const FlowCanvas& canvas, const std::vector<bool>& blocked,
                        const std::vector<bool>& label_reserved, const FlowPoint& point,
                        const FlowPoint& source, const FlowPoint& target) {
  if (!canvas.contains(point.x, point.y))
    return false;
  if (flow_same_point(point, source) || flow_same_point(point, target))
    return true;
  const std::size_t index = static_cast<std::size_t>(flow_point_index(canvas.width, point));
  if (blocked[index] || label_reserved[index])
    return false;
  const FlowCanvasCell& cell = canvas.cells[static_cast<std::size_t>(point.y)]
                                           [static_cast<std::size_t>(point.x)];
  return cell.text == " " || FlowCanvas::line_bits(cell.text) != 0;
}

int flow_existing_line_penalty(const FlowCanvas& canvas, const FlowPoint& point, int dir) {
  const FlowCanvasCell& cell = canvas.cells[static_cast<std::size_t>(point.y)]
                                           [static_cast<std::size_t>(point.x)];
  const int bits = FlowCanvas::line_bits(cell.text);
  if (bits == 0)
    return 0;
  const bool horizontal_step = dir == 1 || dir == 3;
  const bool horizontal_line = (bits & (FlowCanvas::kWest | FlowCanvas::kEast)) != 0;
  const bool vertical_line = (bits & (FlowCanvas::kNorth | FlowCanvas::kSouth)) != 0;
  if ((horizontal_step && horizontal_line && !vertical_line) ||
      (!horizontal_step && vertical_line && !horizontal_line))
    return 80;
  return 2000;
}

std::optional<FlowSearchResult>
find_flow_grid_path(const FlowCanvas& canvas, const std::vector<bool>& blocked,
                    const std::vector<bool>& label_reserved, const FlowPort& source,
                    const FlowPort& target) {
  if (!canvas.contains(source.point.x, source.point.y) ||
      !canvas.contains(target.point.x, target.point.y))
    return std::nullopt;

  constexpr int kNoDir = 4;
  constexpr int kInf = std::numeric_limits<int>::max() / 4;
  constexpr int kDx[4] = {0, 1, 0, -1};
  constexpr int kDy[4] = {-1, 0, 1, 0};
  const int state_count = canvas.width * canvas.height * 5;
  auto state_index = [&](int x, int y, int dir) {
    return (y * canvas.width + x) * 5 + dir;
  };
  auto state_point = [&](int state) {
    const int cell = state / 5;
    return FlowPoint{.x = cell % canvas.width, .y = cell / canvas.width};
  };

  struct QueueItem {
    int cost = 0;
    int state = 0;
  };
  struct QueueGreater {
    bool operator()(const QueueItem& left, const QueueItem& right) const {
      return left.cost > right.cost;
    }
  };

  std::vector<int> distance(static_cast<std::size_t>(state_count), kInf);
  std::vector<int> previous(static_cast<std::size_t>(state_count), -1);
  std::priority_queue<QueueItem, std::vector<QueueItem>, QueueGreater> queue;
  const int start_state = state_index(source.point.x, source.point.y, kNoDir);
  distance[static_cast<std::size_t>(start_state)] = 0;
  queue.push(QueueItem{.cost = 0, .state = start_state});

  int best_state = -1;
  while (!queue.empty()) {
    const QueueItem item = queue.top();
    queue.pop();
    if (item.cost != distance[static_cast<std::size_t>(item.state)])
      continue;
    const FlowPoint current = state_point(item.state);
    const int current_dir = item.state % 5;
    if (flow_same_point(current, target.point) &&
        (target.approach_dir < 0 || current_dir == target.approach_dir)) {
      best_state = item.state;
      break;
    }

    for (int dir = 0; dir < 4; ++dir) {
      const FlowPoint next{.x = current.x + kDx[dir], .y = current.y + kDy[dir]};
      if (!flow_point_allowed(canvas, blocked, label_reserved, next, source.point,
                              target.point))
        continue;
      if (flow_same_point(next, target.point) && target.approach_dir >= 0 &&
          dir != target.approach_dir)
        continue;

      int step_cost = 10 + flow_existing_line_penalty(canvas, next, dir);
      if (current_dir != kNoDir && current_dir != dir)
        step_cost += 18;
      const int next_state = state_index(next.x, next.y, dir);
      const int next_cost = item.cost + step_cost;
      if (next_cost >= distance[static_cast<std::size_t>(next_state)])
        continue;
      distance[static_cast<std::size_t>(next_state)] = next_cost;
      previous[static_cast<std::size_t>(next_state)] = item.state;
      queue.push(QueueItem{.cost = next_cost, .state = next_state});
    }
  }

  if (best_state < 0)
    return std::nullopt;

  FlowSearchResult result;
  result.cost = distance[static_cast<std::size_t>(best_state)];
  for (int state = best_state; state >= 0; state = previous[static_cast<std::size_t>(state)])
    result.points.push_back(state_point(state));
  std::reverse(result.points.begin(), result.points.end());
  return result;
}

std::vector<FlowPort> make_flow_source_ports(const FlowGraphNode& source,
                                             const FlowGraphNode& target,
                                             const FlowEdge& edge,
                                             const std::string& label) {
  const int label_width = flow_label_width(label);
  const int side_y = flow_source_side_y(source, edge);
  const std::vector<FlowPortSide> sides = flow_source_side_order(source, target, label);
  std::vector<FlowPort> ports;
  ports.reserve(sides.size());
  for (std::size_t index = 0; index < sides.size(); ++index) {
    ports.push_back(make_flow_source_port(source, sides[index], side_y, label_width,
                                          static_cast<int>(index) * 70));
  }
  return ports;
}

std::vector<FlowPort> make_flow_target_ports(const FlowGraphNode& source,
                                             const FlowGraphNode& target) {
  const std::vector<FlowPortSide> sides = flow_target_side_order(source, target);
  std::vector<FlowPort> ports;
  ports.reserve(sides.size());
  for (std::size_t index = 0; index < sides.size(); ++index)
    ports.push_back(make_flow_target_port(target, sides[index], static_cast<int>(index) * 70));
  return ports;
}

std::optional<FlowRouteResult>
route_flow_edge_path(const FlowCanvas& canvas, const std::vector<bool>& blocked,
                     const std::vector<bool>& label_reserved, const FlowGraphNode& source,
                     const FlowGraphNode& target, const FlowEdge& edge,
                     const std::string& label) {
  std::optional<FlowRouteResult> best;
  const std::vector<FlowPort> source_ports = make_flow_source_ports(source, target, edge, label);
  const std::vector<FlowPort> target_ports = make_flow_target_ports(source, target);
  for (const FlowPort& source_port : source_ports) {
    if (!canvas.contains(source_port.point.x, source_port.point.y) ||
        !canvas.contains(source_port.stem.x, source_port.stem.y))
      continue;
    for (const FlowPort& target_port : target_ports) {
      if (!canvas.contains(target_port.point.x, target_port.point.y))
        continue;
      const std::optional<FlowSearchResult> path =
          find_flow_grid_path(canvas, blocked, label_reserved, source_port, target_port);
      if (!path.has_value())
        continue;
      const int cost = path->cost + source_port.preference + target_port.preference;
      if (best.has_value() && cost >= best->cost)
        continue;
      best = FlowRouteResult{.source = source_port,
                             .target = target_port,
                             .points = path->points,
                             .cost = cost};
    }
  }
  return best;
}

void draw_flow_port_stem(FlowCanvas& canvas, const FlowPort& port, FlowTone tone) {
  if (flow_same_point(port.point, port.stem)) {
    canvas.add_line_bits(port.point.x, port.point.y, port.stem_to_block_bits, tone);
    return;
  }
  if (port.point.y == port.stem.y)
    canvas.draw_h(port.point.x, port.stem.x, port.stem.y, tone);
  else
    canvas.draw_v(port.stem.x, port.point.y, port.stem.y, tone);
  canvas.add_line_bits(port.stem.x, port.stem.y, port.stem_to_block_bits, tone);
}

bool can_reserve_flow_label(const FlowCanvas& canvas, const std::vector<bool>& blocked,
                            const std::vector<bool>& label_reserved, int x, int y,
                            int label_width) {
  for (int offset = 0; offset < label_width; ++offset) {
    const FlowPoint point{.x = x + offset, .y = y};
    if (!canvas.contains(point.x, point.y))
      return false;
    const std::size_t index = static_cast<std::size_t>(flow_point_index(canvas.width, point));
    if (blocked[index] || label_reserved[index])
      return false;
    const FlowCanvasCell& cell = canvas.cells[static_cast<std::size_t>(point.y)]
                                             [static_cast<std::size_t>(point.x)];
    const int bits = FlowCanvas::line_bits(cell.text);
    if (cell.text != " " && bits == 0)
      return false;
    if ((bits & (FlowCanvas::kNorth | FlowCanvas::kSouth)) != 0)
      return false;
  }
  return true;
}

bool reserve_flow_label(FlowCanvas& canvas, std::vector<bool>& label_reserved,
                        const std::vector<bool>& blocked,
                        std::vector<FlowPendingLabel>& labels, int x, int y,
                        const std::string& label, FlowTone tone) {
  const int width = flow_label_width(label);
  if (width == 0 || !can_reserve_flow_label(canvas, blocked, label_reserved, x, y, width))
    return false;
  for (int offset = 0; offset < width; ++offset) {
    const FlowPoint point{.x = x + offset, .y = y};
    label_reserved[static_cast<std::size_t>(flow_point_index(canvas.width, point))] = true;
  }
  queue_flow_edge_label(labels, x, y, label, tone);
  return true;
}

bool reserve_flow_stem_label(FlowCanvas& canvas, std::vector<bool>& label_reserved,
                             const std::vector<bool>& blocked,
                             std::vector<FlowPendingLabel>& labels, const FlowPort& source,
                             const std::string& label, FlowTone tone) {
  if (label.empty() || flow_same_point(source.point, source.stem) ||
      source.point.y != source.stem.y)
    return false;
  const int label_x = source.point.x < source.stem.x ? source.point.x + 1 : source.stem.x;
  return reserve_flow_label(canvas, label_reserved, blocked, labels, label_x, source.point.y,
                            label, tone);
}

bool reserve_flow_path_label(FlowCanvas& canvas, std::vector<bool>& label_reserved,
                             const std::vector<bool>& blocked,
                             std::vector<FlowPendingLabel>& labels,
                             const std::vector<FlowPoint>& points,
                             const std::string& label, FlowTone tone) {
  const int label_width = flow_label_width(label);
  if (label_width == 0)
    return false;
  for (std::size_t index = 0; index + 1 < points.size(); ++index) {
    if (points[index].y != points[index + 1].y)
      continue;
    int left = std::min(points[index].x, points[index + 1].x);
    int right = std::max(points[index].x, points[index + 1].x);
    std::size_t cursor = index + 1;
    while (cursor + 1 < points.size() && points[cursor].y == points[cursor + 1].y) {
      left = std::min(left, points[cursor + 1].x);
      right = std::max(right, points[cursor + 1].x);
      ++cursor;
    }
    index = cursor - 1;
    if (right - left + 1 < label_width)
      continue;
    const int centered = left + (right - left + 1 - label_width) / 2;
    for (int delta = 0; delta <= right - left; ++delta) {
      const int candidates[2] = {centered - delta, centered + delta};
      for (const int label_x : candidates) {
        if (label_x < left || label_x + label_width - 1 > right)
          continue;
        if (reserve_flow_label(canvas, label_reserved, blocked, labels, label_x,
                               points[index].y, label, tone))
          return true;
      }
    }
  }
  return false;
}

void draw_flow_routed_path(FlowCanvas& canvas, std::vector<bool>& label_reserved,
                           const std::vector<bool>& blocked,
                           std::vector<FlowPendingLabel>& labels,
                           std::vector<FlowBorderConnector>& connectors,
                           const FlowRouteResult& route, const std::string& label,
                           FlowTone tone) {
  draw_flow_port_stem(canvas, route.source, tone);
  for (std::size_t index = 0; index + 1 < route.points.size(); ++index) {
    const FlowPoint current = route.points[index];
    const FlowPoint next = route.points[index + 1];
    const int dir = flow_direction_between(current, next);
    if (dir < 0)
      continue;
    canvas.add_line_bits(current.x, current.y, flow_dir_bit(dir), tone);
    canvas.add_line_bits(next.x, next.y, flow_opposite_dir_bit(dir), tone);
  }
  canvas.set(route.target.point.x, route.target.point.y, route.target.arrow, tone);
  queue_flow_border_connector(connectors, route.source.border_x, route.source.border_y,
                              route.source.border_bits, tone);
  queue_flow_border_connector(connectors, route.target.border_x, route.target.border_y,
                              route.target.border_bits, tone);
  if (!reserve_flow_stem_label(canvas, label_reserved, blocked, labels, route.source, label,
                               tone)) {
    reserve_flow_path_label(canvas, label_reserved, blocked, labels, route.points, label, tone);
  }
}

void draw_flow_graph_edge(FlowCanvas& canvas, const std::vector<bool>& blocked,
                          std::vector<bool>& label_reserved, const FlowGraphNode& source,
                          const FlowGraphNode& target, const FlowEdge& edge,
                          std::vector<FlowPendingLabel>& labels,
                          std::vector<FlowBorderConnector>& connectors) {
  const FlowTone tone = source.reachable ? edge.tone : FlowTone::Muted;
  const std::string label = graph_edge_label(edge);
  const std::optional<FlowRouteResult> route =
      route_flow_edge_path(canvas, blocked, label_reserved, source, target, edge, label);
  if (!route.has_value())
    return;
  draw_flow_routed_path(canvas, label_reserved, blocked, labels, connectors, *route, label,
                        tone);
}

std::string render_flow_graph(const std::vector<FlowBlock>& blocks,
                              const std::vector<ResolvedStep>& steps, bool color) {
  if (blocks.empty())
    return "";

  constexpr int kContentWidth = 26;
  constexpr int kNodeWidth = kContentWidth + 6;
  constexpr int kColumnGap = 4;
  constexpr int kMaxColumns = 2;
  constexpr int kMaxBackwardLanes = 8;
  constexpr int kMaxForwardLanes = 8;
  constexpr int kRowGap = 5;
  constexpr int kLeftLabelGutter = 8;

  std::vector<FlowGraphNode> nodes;
  std::map<int, int> node_by_address;
  nodes.reserve(blocks.size());
  for (const FlowBlock& block : blocks) {
    const int id = static_cast<int>(nodes.size());
    FlowGraphNode node;
    node.id = id;
    node.block = &block;
    node.title = flow_block_title(block);
    node.lines = compact_block_lines(block, steps, kContentWidth);
    node.tone = block.tone;
    node.reachable = block.reachable;
    node.sort_key = block.start;
    node.width = kNodeWidth;
    node.height = static_cast<int>(node.lines.size()) + 2;
    nodes.push_back(std::move(node));
    node_by_address[block.start] = id;
  }

  std::vector<FlowGraphEdge> edges;
  const std::size_t real_node_count = nodes.size();
  std::vector<std::vector<int>> pseudo_after(real_node_count);
  for (std::size_t node_index = 0; node_index < real_node_count; ++node_index) {
    const FlowGraphNode& node = nodes[node_index];
    if (node.block == nullptr)
      continue;
    for (const FlowEdge& edge : node.block->edges) {
      if (edge.target.has_value()) {
        const auto target = node_by_address.find(*edge.target);
        if (target != node_by_address.end()) {
          edges.push_back(FlowGraphEdge{.source = node.id, .target = target->second, .edge = edge});
          continue;
        }
      }

      FlowGraphNode pseudo;
      pseudo.id = static_cast<int>(nodes.size());
      pseudo.pseudo_edge = edge;
      pseudo.title = pseudo_node_title(edge);
      pseudo.lines = {edge.detail.value_or("unknown target")};
      pseudo.tone = FlowTone::Unknown;
      pseudo.reachable = node.reachable;
      pseudo.sort_key = 10000 + pseudo.id;
      pseudo.width = kNodeWidth;
      pseudo.height = static_cast<int>(pseudo.lines.size()) + 2;
      edges.push_back(FlowGraphEdge{.source = node.id, .target = pseudo.id, .edge = edge});
      pseudo_after[node_index].push_back(pseudo.id);
      nodes.push_back(std::move(pseudo));
    }
  }

  nodes.front().rank = 0;
  std::vector<int> queue = {0};
  for (std::size_t cursor = 0; cursor < queue.size(); ++cursor) {
    const int source = queue[cursor];
    for (const FlowGraphEdge& edge : edges) {
      if (edge.source != source)
        continue;
      FlowGraphNode& target = nodes[static_cast<std::size_t>(edge.target)];
      if (target.rank >= 0)
        continue;
      target.rank = nodes[static_cast<std::size_t>(source)].rank + 1;
      queue.push_back(edge.target);
    }
  }

  int max_rank = 0;
  for (const FlowGraphNode& node : nodes)
    max_rank = std::max(max_rank, node.rank);
  for (FlowGraphNode& node : nodes) {
    if (node.rank < 0)
      node.rank = ++max_rank;
  }

  std::vector<std::vector<int>> logical_ranks(static_cast<std::size_t>(max_rank + 1));
  for (const FlowGraphNode& node : nodes)
    logical_ranks[static_cast<std::size_t>(node.rank)].push_back(node.id);

  int max_columns = 1;
  std::vector<std::vector<int>> ranks;
  for (std::vector<int>& rank_nodes : logical_ranks) {
    std::sort(rank_nodes.begin(), rank_nodes.end(), [&](int left, int right) {
      return nodes[static_cast<std::size_t>(left)].sort_key <
             nodes[static_cast<std::size_t>(right)].sort_key;
    });
    for (std::size_t cursor = 0; cursor < rank_nodes.size();
         cursor += static_cast<std::size_t>(kMaxColumns)) {
      const std::size_t end =
          std::min(cursor + static_cast<std::size_t>(kMaxColumns), rank_nodes.size());
      ranks.emplace_back(rank_nodes.begin() + static_cast<std::ptrdiff_t>(cursor),
                         rank_nodes.begin() + static_cast<std::ptrdiff_t>(end));
    }
  }

  std::vector<int> rank_heights(ranks.size(), 0);
  for (std::size_t rank = 0; rank < ranks.size(); ++rank) {
    max_columns = std::max(max_columns, static_cast<int>(ranks[rank].size()));
    for (std::size_t column = 0; column < ranks[rank].size(); ++column) {
      FlowGraphNode& node = nodes[static_cast<std::size_t>(ranks[rank][column])];
      node.rank = static_cast<int>(rank);
      node.column = static_cast<int>(column);
      rank_heights[rank] = std::max(rank_heights[rank], node.height);
    }
  }

  int left_lanes = 0;
  int right_lanes = 0;
  for (const FlowGraphEdge& edge : edges) {
    const FlowGraphNode& source = nodes[static_cast<std::size_t>(edge.source)];
    const FlowGraphNode& target = nodes[static_cast<std::size_t>(edge.target)];
    if (target.rank < source.rank) {
      if (target.column == 0)
        ++left_lanes;
    } else if (target.rank > source.rank + 1) {
      if (source.column == 0 && target.column == source.column &&
          !graph_edge_label(edge.edge).empty())
        ++left_lanes;
      else
        ++right_lanes;
    } else if (target.rank == source.rank && !graph_edge_label(edge.edge).empty()) {
      if (target.column > source.column)
        ++right_lanes;
      else
        ++left_lanes;
    } else if (target.rank == source.rank + 1 && target.column == source.column &&
               !graph_edge_label(edge.edge).empty()) {
      ++right_lanes;
    }
  }

  left_lanes = std::min(left_lanes, kMaxBackwardLanes);
  right_lanes = std::min(right_lanes, kMaxForwardLanes);

  const int column_width = kNodeWidth + kColumnGap;
  const int left_gutter = 5 + left_lanes * 3 + (left_lanes > 0 ? kLeftLabelGutter : 0);
  const int grid_right = left_gutter + max_columns * column_width - kColumnGap;
  const int canvas_width = grid_right + 12 + right_lanes * 3;

  std::vector<int> rank_y(ranks.size(), 0);
  int y = 1;
  for (std::size_t rank = 0; rank < ranks.size(); ++rank) {
    rank_y[static_cast<std::size_t>(rank)] = y;
    y += rank_heights[rank] + kRowGap;
  }

  for (FlowGraphNode& node : nodes) {
    node.x = left_gutter + node.column * column_width;
    node.y = rank_y[static_cast<std::size_t>(node.rank)];
  }

  const int canvas_height = std::max(1, y - 2);

  FlowCanvas canvas(canvas_width, canvas_height);
  std::vector<bool> blocked(static_cast<std::size_t>(canvas_width * canvas_height), false);
  for (const FlowGraphNode& node : nodes) {
    for (int yy = node.y; yy < node.y + node.height; ++yy) {
      for (int xx = node.x; xx < node.x + node.width; ++xx) {
        if (canvas.contains(xx, yy))
          blocked[static_cast<std::size_t>(yy * canvas_width + xx)] = true;
      }
    }
  }
  std::vector<bool> label_reserved(static_cast<std::size_t>(canvas_width * canvas_height),
                                   false);
  std::vector<FlowPendingLabel> pending_labels;
  std::vector<FlowBorderConnector> pending_connectors;
  for (std::size_t index = 0; index < edges.size(); ++index) {
    const FlowGraphEdge& edge = edges[index];
    const FlowGraphNode& source = nodes[static_cast<std::size_t>(edge.source)];
    const FlowGraphNode& target = nodes[static_cast<std::size_t>(edge.target)];
    draw_flow_graph_edge(canvas, blocked, label_reserved, source, target, edge.edge,
                         pending_labels, pending_connectors);
  }
  for (const FlowPendingLabel& label : pending_labels)
    put_flow_edge_label(canvas, label.x, label.y, label.label, label.tone);
  for (const std::vector<int>& rank_nodes : ranks) {
    for (const int node_id : rank_nodes)
      draw_flow_node(canvas, nodes[static_cast<std::size_t>(node_id)]);
  }
  for (const FlowBorderConnector& connector : pending_connectors)
    canvas.add_line_bits(connector.x, connector.y, connector.bits, connector.tone);

  return canvas.str(color);
}

bool is_number_entry_step(const ResolvedStep& step) {
  return (step.mnemonic.size() == 1 &&
          std::isdigit(static_cast<unsigned char>(step.mnemonic.front())) != 0) ||
         step.mnemonic == "." || step.mnemonic == "ВП" || step.mnemonic == "/-/";
}

ListingRow step_to_listing_row(const ResolvedStep& step) {
  return ListingRow{
      .address = step.address,
      .hex = step.hex,
      .mnemonic = step.mnemonic,
      .comment = step.comment,
  };
}

std::string format_number_entry_code(const std::vector<ResolvedStep>& steps) {
  if (steps.size() <= static_cast<std::size_t>(kListingInlineCodeTokens)) {
    std::ostringstream out;
    for (std::size_t index = 0; index < steps.size(); ++index) {
      if (index > 0)
        out << ' ';
      out << steps.at(index).hex;
    }
    return out.str();
  }
  return steps.front().hex + " ... " + steps.back().hex;
}

std::string format_number_entry_mnemonic(const std::vector<ResolvedStep>& steps) {
  std::string mantissa;
  std::optional<std::string> exponent;
  bool negative = false;
  bool exponent_negative = false;

  for (const ResolvedStep& step : steps) {
    if (step.mnemonic.size() == 1 &&
        std::isdigit(static_cast<unsigned char>(step.mnemonic.front())) != 0) {
      if (exponent.has_value()) {
        *exponent += step.mnemonic;
      } else {
        mantissa += step.mnemonic;
      }
      continue;
    }
    if (step.mnemonic == ".") {
      if (!exponent.has_value())
        mantissa += ".";
      continue;
    }
    if (step.mnemonic == "ВП") {
      exponent = "";
      continue;
    }
    if (step.mnemonic == "/-/") {
      if (exponent.has_value() && !exponent->empty() && !exponent_negative) {
        exponent_negative = true;
      } else {
        negative = true;
      }
    }
  }

  const std::string base = mantissa.empty() ? "0" : mantissa;
  const std::string exp =
      exponent.has_value() ? "E" + std::string(exponent_negative ? "-" : "") + *exponent : "";
  return std::string(negative ? "-" : "") + base + exp;
}

ListingRow number_entry_group_to_listing_row(const std::vector<ResolvedStep>& group) {
  std::optional<std::string> comment;
  for (const ResolvedStep& step : group) {
    if (step.comment.has_value() && !step.comment->empty()) {
      comment = step.comment;
      break;
    }
  }
  return ListingRow{
      .address = group.front().address,
      .hex = format_number_entry_code(group),
      .mnemonic = format_number_entry_mnemonic(group),
      .comment = std::move(comment),
  };
}

std::vector<ListingRow> coalesce_number_entry_rows(const std::vector<ResolvedStep>& steps) {
  std::vector<ListingRow> rows;
  for (std::size_t index = 0; index < steps.size(); ++index) {
    const ResolvedStep& step = steps.at(index);
    if (!is_number_entry_step(step)) {
      rows.push_back(step_to_listing_row(step));
      continue;
    }

    std::vector<ResolvedStep> group = {step};
    while (index + 1U < steps.size() && is_number_entry_step(steps.at(index + 1U))) {
      group.push_back(steps.at(index + 1U));
      ++index;
    }
    rows.push_back(group.size() == 1 ? step_to_listing_row(step)
                                     : number_entry_group_to_listing_row(group));
  }
  return rows;
}

std::string format_listing_address(int address) {
  if (address < 0)
    return "--";
  return format_step_address(address);
}

std::string format_listing_rows(const std::vector<ListingRow>& rows) {
  std::size_t code_width = 2;
  for (const ListingRow& row : rows)
    code_width = std::max(code_width, row.hex.size());

  std::ostringstream out;
  if (code_width > 2) {
    out << " Step | " << pad_end("Code", code_width) << " | Command                 | Comment\n";
    out << "------+" << std::string(code_width + 2U, '-')
        << "+-------------------------+----------------";
    for (const ListingRow& row : rows) {
      out << '\n';
      const std::string address = pad_start(format_listing_address(row.address), 4);
      const std::string code = pad_end(row.hex, code_width);
      const std::string command = pad_end(to_keycaps(row.mnemonic), 23);
      out << ' ' << address << " | " << code << " | " << command << " |";
      if (row.comment.has_value() && !row.comment->empty())
        out << ' ' << *row.comment;
    }
    return out.str();
  }

  out << " Step | Code | Command                 | Comment\n";
  out << "------+------+-------------------------+----------------";
  for (const ListingRow& row : rows) {
    out << '\n';
    const std::string address = pad_start(format_listing_address(row.address), 4);
    const std::string code = pad_start(row.hex, 2);
    const std::string command = pad_end(to_keycaps(row.mnemonic), 23);
    out << ' ' << address << " |  " << code << "  | " << command << " |";
    if (row.comment.has_value() && !row.comment->empty())
      out << ' ' << *row.comment;
  }
  return out.str();
}

} // namespace

std::string format_hex_steps(const std::vector<ResolvedStep>& steps) {
  std::ostringstream out;
  for (std::size_t index = 0; index < steps.size(); index += kHexColumns) {
    if (index > 0)
      out << '\n';
    out << format_step_address(static_cast<int>(index)) << ":";
    const std::size_t end = std::min(index + static_cast<std::size_t>(kHexColumns), steps.size());
    for (std::size_t step = index; step < end; ++step)
      out << ' ' << steps.at(step).hex;
  }
  return out.str();
}

std::string format_listing_steps(const std::vector<ResolvedStep>& steps) {
  return format_listing_rows(coalesce_number_entry_rows(steps));
}

std::string format_flow_steps(const std::vector<ResolvedStep>& steps, bool color) {
  return format_flow_steps(steps, std::vector<PreloadReport>{}, color);
}

std::string format_flow_steps(const std::vector<ResolvedStep>& steps,
                              const std::vector<PreloadReport>& preloads, bool color) {
  const FlowContext context = build_flow_context(steps, preloads);
  const std::vector<FlowBlock> blocks = build_flow_blocks(steps, context);
  std::ostringstream out;
  out << colorize("MK-61 execution flow", FlowTone::Normal, color);
  if (blocks.empty())
    return out.str();
  out << "\n" << render_flow_graph(blocks, steps, color);
  return out.str();
}

std::string format_flow_result(const CompileResult& result, bool color) {
  return format_flow_steps(result.steps, result.preloads, color);
}

std::string format_manual_input_value(const ManualSetupInput& input) {
  if (input.min.has_value() && input.max.has_value()) {
    return "any value " + std::to_string(*input.min) + ".." + std::to_string(*input.max);
  }
  return "a value";
}

std::string format_setup_listing_steps(const std::vector<ManualSetupInput>& manual_inputs,
                                       const std::vector<ResolvedStep>& steps) {
  std::vector<ListingRow> rows;
  rows.reserve(manual_inputs.size() + steps.size());
  for (const ManualSetupInput& input : manual_inputs) {
    rows.push_back(ListingRow{
        .address = -1,
        .hex = "-",
        .mnemonic = "enter " + input.name,
        .comment = "enter " + format_manual_input_value(input) + " in " + input.stack,
    });
  }
  for (const ResolvedStep& step : steps)
    rows.push_back(step_to_listing_row(step));
  return format_listing_rows(rows);
}

std::optional<std::string>
format_setup_preload_listing_steps(const std::vector<PreloadReport>& preloads) {
  std::vector<PreloadReport> setup_preloads;
  for (const PreloadReport& preload : preloads) {
    if (is_setup_literal(preload.value))
      setup_preloads.push_back(preload);
  }
  if (setup_preloads.empty())
    return std::string{};

  for (const PreloadReport& preload : setup_preloads) {
    if (!executable_setup_value(preload.value).has_value())
      return std::nullopt;
  }

  std::vector<ListingRow> rows;
  int address = 0;
  const std::vector<ExecutableSetupPreloadEntry> entries =
      executable_setup_preload_entries(setup_preloads);
  for (const ExecutableSetupPreloadGroup& group : group_setup_preloads_by_executable_value(entries)) {
    rows.push_back(ListingRow{
        .address = address,
        .hex = "-",
        .mnemonic = group.value,
    });
    ++address;
    for (const PreloadReport& preload : group.preloads) {
      const int opcode = 0x40 + register_index(preload.register_name);
      const OpcodeInfo& info = opcode_by_code(opcode);
      rows.push_back(ListingRow{
          .address = address,
          .hex = info.hex,
          .mnemonic = info.name,
          .comment = "setup R" + preload.register_name,
      });
      ++address;
    }
  }
  return format_listing_rows(rows);
}

std::string format_program_tokens(const std::vector<ResolvedStep>& steps) {
  std::ostringstream out;
  for (std::size_t index = 0; index < steps.size(); ++index) {
    if (index > 0)
      out << '\n';
    out << steps.at(index).hex;
  }
  return out.str();
}

std::optional<std::string> format_setup_block(const std::vector<PreloadReport>& preloads) {
  std::vector<std::string> assignments;
  for (const PreloadReport& preload : preloads) {
    if (!is_setup_literal(preload.value))
      continue;
    assignments.push_back("R" + preload.register_name + "=" + format_setup_value(preload.value));
  }
  if (assignments.empty())
    return std::nullopt;

  std::ostringstream out;
  out << '`';
  for (std::size_t index = 0; index < assignments.size(); ++index) {
    if (index > 0)
      out << "; ";
    out << assignments.at(index);
  }
  out << '`';
  return out.str();
}

std::string to_keycaps(std::string mnemonic) {
  if (mnemonic == "*")
    return "×";
  if (mnemonic == "/")
    return "÷";
  if (mnemonic == "-")
    return "−";
  if (mnemonic == "<->")
    return "↔";
  if (mnemonic == "X<->Y")
    return "X↔Y";
  if (mnemonic == "F pi")
    return "F π";
  if (mnemonic == "F sqrt")
    return "F √";

  mnemonic = replace_all(std::move(mnemonic), "^-1", "⁻¹");
  mnemonic = replace_all(std::move(mnemonic), "^x", "ˣ");
  mnemonic = replace_all(std::move(mnemonic), "^y", "ʸ");
  mnemonic = replace_all(std::move(mnemonic), "^2", "²");
  mnemonic = replace_all(std::move(mnemonic), "->", "→");
  mnemonic = replace_all(std::move(mnemonic), "<-", "←");
  mnemonic = replace_all(std::move(mnemonic), ">=", "≥");
  mnemonic = replace_all(std::move(mnemonic), "!=", "≠");
  return mnemonic;
}

} // namespace mkpro
