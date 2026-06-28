#include "mkpro/core/format.hpp"

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <iomanip>
#include <map>
#include <optional>
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

void put_flow_edge_label(FlowCanvas& canvas, int x, int y, const std::string& label,
                         FlowTone tone) {
  if (label.empty())
    return;
  canvas.put_text(x, y, "\u2500 " + label + " \u2500", tone);
}

void draw_flow_graph_edge(FlowCanvas& canvas, const FlowGraphNode& source,
                          const FlowGraphNode& target, const FlowEdge& edge, int side_lane,
                          int grid_right) {
  const FlowTone tone = source.reachable ? edge.tone : FlowTone::Muted;
  const std::string label = graph_edge_label(edge);
  const int label_width = label.empty()
                              ? 0
                              : static_cast<int>(utf8_codepoint_count("\u2500 " + label +
                                                                       " \u2500"));
  const int source_center_x = source.x + source.width / 2;
  const int target_center_x = target.x + target.width / 2;
  const int source_bottom_y = source.y + source.height;
  const int target_top_y = target.y - 1;
  const int source_mid_y = source.y + std::max(1, (source.height - 1) / 2);
  const int target_mid_y = target.y + std::max(1, (target.height - 1) / 2);
  const int source_side_y =
      edge.label == "no" ? std::min(source.y + source.height - 2, source_mid_y + 1)
                         : source_mid_y;

  if (target.rank == source.rank + 1 && target.column == source.column) {
    if (!label.empty()) {
      const int route_x = source.x + source.width + label_width + 2;
      const int return_y = target_top_y - 1;
      canvas.draw_h(source.x + source.width, route_x, source_side_y, tone);
      canvas.draw_v(route_x, source_side_y, return_y, tone);
      canvas.draw_h(source_center_x, route_x, return_y, tone);
      canvas.set(route_x, source_side_y, "\u256e", tone);
      canvas.set(route_x, return_y, "\u256f", tone);
      canvas.set(source_center_x, return_y, "\u256d", tone);
      canvas.set(source_center_x, target_top_y, "\u25bc", tone);
      put_flow_edge_label(canvas, source.x + source.width + 1, source_side_y, label, tone);
      return;
    }
    if (source_bottom_y <= target_top_y - 1)
      canvas.draw_v(source_center_x, source_bottom_y, target_top_y - 1, tone);
    canvas.set(source_center_x, target_top_y, "\u25bc", tone);
    return;
  }

  if (target.rank == source.rank + 1) {
    const int mid_y = source_bottom_y + std::max(1, (target_top_y - source_bottom_y) / 2);
    canvas.draw_v(source_center_x, source_bottom_y, mid_y, tone);
    canvas.draw_h(source_center_x, target_center_x, mid_y, tone);
    if (mid_y + 1 <= target_top_y - 1)
      canvas.draw_v(target_center_x, mid_y + 1, target_top_y - 1, tone);
    if (source_center_x < target_center_x) {
      canvas.set(source_center_x, mid_y, "\u2570", tone);
      canvas.set(target_center_x, mid_y, "\u256e", tone);
    } else if (source_center_x > target_center_x) {
      canvas.set(source_center_x, mid_y, "\u256f", tone);
      canvas.set(target_center_x, mid_y, "\u256d", tone);
    }
    canvas.set(target_center_x, target_top_y, "\u25bc", tone);
    put_flow_edge_label(canvas, std::min(source_center_x, target_center_x) + 1, mid_y, label,
                        tone);
    return;
  }

  if (target.rank == source.rank) {
    if (source.x < target.x) {
      canvas.draw_h(source.x + source.width, target.x - 2, source_side_y, tone);
      canvas.set(target.x - 1, source_side_y, "\u25b6", tone);
      put_flow_edge_label(canvas, source.x + source.width + 1, source_side_y, label, tone);
    } else {
      canvas.draw_h(target.x + target.width + 1, source.x - 1, source_side_y, tone);
      canvas.set(target.x + target.width, source_side_y, "\u25c0", tone);
      put_flow_edge_label(canvas, target.x + target.width + 1, source_side_y, label, tone);
    }
    return;
  }

  if (target.rank < source.rank && target.column > source.column) {
    const int lane = target.x - 2 - (side_lane % 2);
    canvas.draw_h(source.x + source.width, lane, source_side_y, tone);
    canvas.draw_v(lane, source_side_y, target_mid_y, tone);
    canvas.draw_h(lane, target.x - 1, target_mid_y, tone);
    canvas.set(lane, source_side_y,
               source.x + source.width <= lane ? "\u256f" : "\u2570", tone);
    const int target_join_bits = FlowCanvas::line_bits(
        canvas.cells[static_cast<std::size_t>(target_mid_y)][static_cast<std::size_t>(lane)]
            .text);
    canvas.set(lane, target_mid_y,
               (target_join_bits & FlowCanvas::kWest) != 0 ? "\u252c" : "\u256d", tone);
    canvas.set(target.x - 1, target_mid_y, "\u25b6", tone);
    put_flow_edge_label(canvas, lane + 1, source_side_y, label, tone);
    return;
  }

  if (target.rank > source.rank) {
    const int lane =
        std::max(grid_right + 2 + side_lane * 3, source.x + source.width + label_width + 2);
    canvas.draw_h(source.x + source.width, lane, source_side_y, tone);
    canvas.draw_v(lane, source_side_y, target_mid_y, tone);
    canvas.draw_h(target.x + target.width, lane, target_mid_y, tone);
    canvas.set(lane, source_side_y, "\u256e", tone);
    canvas.set(lane, target_mid_y, "\u256f", tone);
    canvas.set(target.x + target.width, target_mid_y, "\u25c0", tone);
    put_flow_edge_label(canvas, source.x + source.width + 1, source_side_y, label, tone);
    return;
  }

  const int lane = source.x - 3 - side_lane * 3;
  canvas.draw_h(lane, source.x - 1, source_mid_y, tone);
  canvas.draw_v(lane, source_mid_y, target_mid_y, tone);
  canvas.draw_h(lane, target.x - 1, target_mid_y, tone);
  canvas.set(lane, source_mid_y, "\u2570", tone);
  canvas.set(lane, target_mid_y, "\u256d", tone);
  canvas.set(target.x - 1, target_mid_y, "\u25b6", tone);
  put_flow_edge_label(canvas, lane + 1, source_mid_y, label, tone);
}

std::string render_flow_graph(const std::vector<FlowBlock>& blocks,
                              const std::vector<ResolvedStep>& steps, bool color) {
  if (blocks.empty())
    return "";

  constexpr int kContentWidth = 26;
  constexpr int kNodeWidth = kContentWidth + 6;
  constexpr int kColumnGap = 4;
  constexpr int kMaxColumns = 2;
  constexpr int kMaxBackwardLanes = 3;
  constexpr int kMaxForwardLanes = 4;
  constexpr int kRowGap = 4;

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
      ++right_lanes;
    } else if (target.rank == source.rank + 1 && target.column == source.column &&
               !graph_edge_label(edge.edge).empty()) {
      ++right_lanes;
    }
  }

  left_lanes = std::min(left_lanes, kMaxBackwardLanes);
  right_lanes = std::min(right_lanes, kMaxForwardLanes);

  const int column_width = kNodeWidth + kColumnGap;
  const int left_gutter = 5 + left_lanes * 3;
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
  int left_lane = 0;
  int right_lane = 0;
  for (std::size_t index = 0; index < edges.size(); ++index) {
    const FlowGraphEdge& edge = edges[index];
    const FlowGraphNode& source = nodes[static_cast<std::size_t>(edge.source)];
    const FlowGraphNode& target = nodes[static_cast<std::size_t>(edge.target)];
    int side_lane = 0;
    if (target.rank < source.rank && target.column == 0 && left_lanes > 0) {
      side_lane = left_lane++ % left_lanes;
    } else if (target.rank > source.rank + 1 && right_lanes > 0) {
      side_lane = right_lane++ % right_lanes;
    }
    draw_flow_graph_edge(canvas, source, target, edge.edge, side_lane, grid_right);
  }
  for (const std::vector<int>& rank_nodes : ranks) {
    for (const int node_id : rank_nodes)
      draw_flow_node(canvas, nodes[static_cast<std::size_t>(node_id)]);
  }

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
