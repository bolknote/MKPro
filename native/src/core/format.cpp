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
        .tone = tone,
    });
  };

  if (opcode == 0x51) {
    append_target("jump", FlowTone::Normal);
  } else if (opcode == 0x53) {
    append_target("call", FlowTone::Call);
    append_next("ret", FlowTone::Return);
  } else if (opcode == 0x52) {
    edges.push_back(FlowEdge{
        .label = "return",
        .detail = "return stack",
        .arrow = FlowArrow::Unknown,
        .tone = FlowTone::Return,
    });
  } else if (opcode == 0x50) {
    append_next("resume", FlowTone::Stop);
  } else if (is_direct_conditional_opcode(opcode)) {
    append_target(conditional_taken_label(step.mnemonic), FlowTone::Branch);
    append_next(conditional_fallthrough_label(step.mnemonic), FlowTone::Normal);
  } else if (is_indirect_jump_opcode(opcode)) {
    append_indirect("jump", FlowTone::Unknown);
  } else if (is_indirect_call_opcode(opcode)) {
    append_indirect("call", FlowTone::Call);
    append_next("ret", FlowTone::Return);
  } else if (is_indirect_conditional_opcode(opcode)) {
    append_indirect(conditional_taken_label(step.mnemonic), FlowTone::Unknown);
    append_next(conditional_fallthrough_label(step.mnemonic), FlowTone::Normal);
  } else {
    append_next("next", FlowTone::Normal);
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
  if (is_indirect_jump_opcode(opcode))
    return FlowTone::Unknown;
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
    return "unreachable";
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

std::string format_flow_step_line(const ResolvedStep& step) {
  std::ostringstream out;
  out << pad_start(format_step_address(step.address), 3) << " " << pad_start(step.hex, 2) << "  "
      << pad_end(to_keycaps(step.mnemonic), 16);
  if (step.comment.has_value() && !step.comment->empty())
    out << "  " << abbreviate(*step.comment, 44);
  return out.str();
}

std::string format_flow_operand_line(const ResolvedStep& step, int target) {
  std::ostringstream out;
  out << pad_start(format_step_address(step.address), 3) << " " << pad_start(step.hex, 2)
      << "  \u2192 " << format_step_address(target);
  return out.str();
}

std::string flow_arrow_text(FlowArrow arrow) {
  switch (arrow) {
    case FlowArrow::Down:
      return "\u2500\u25bc";
    case FlowArrow::Forward:
      return "\u2500\u25b6";
    case FlowArrow::Back:
      return "\u2500\u21ba";
    case FlowArrow::Unknown:
      return "\u2500?";
  }
  return "\u2500?";
}

std::string format_flow_edge(const FlowEdge& edge, bool last) {
  std::ostringstream out;
  out << "  " << (last ? "\u2514" : "\u251c") << "\u2500 " << edge.label << " "
      << flow_arrow_text(edge.arrow) << " ";
  if (edge.target.has_value())
    out << flow_ref(*edge.target);
  else
    out << "[?]";
  if (edge.detail.has_value())
    out << " (" << *edge.detail << ")";
  return out.str();
}

std::string render_flow_block(const FlowBlock& block, const std::vector<ResolvedStep>& steps,
                              bool color) {
  struct Line {
    std::string text;
    FlowTone tone = FlowTone::Normal;
  };

  std::vector<Line> lines;
  for (const FlowInstruction& instruction : block.instructions) {
    lines.push_back(Line{.text = format_flow_step_line(steps[instruction.step_index])});
    if (instruction.operand_index.has_value()) {
      lines.push_back(Line{
          .text = format_flow_operand_line(steps[*instruction.operand_index],
                                           instruction.target.value_or(0)),
          .tone = FlowTone::Muted,
      });
    }
  }

  const std::string title = flow_block_title(block);
  std::size_t width = utf8_codepoint_count(title) + 2U;
  for (const Line& line : lines)
    width = std::max(width, utf8_codepoint_count(line.text));

  std::ostringstream out;
  out << "\u256d\u2500" << colorize(title, block.tone, color)
      << repeat_text("\u2500", width - utf8_codepoint_count(title) + 1U) << "\u256e\n";
  for (const Line& line : lines) {
    out << "\u2502 " << colorize(line.text, block.reachable ? line.tone : FlowTone::Muted, color)
        << repeat_text(" ", width - utf8_codepoint_count(line.text)) << " \u2502\n";
  }
  out << "\u2570" << repeat_text("\u2500", width + 2U) << "\u256f";
  for (std::size_t index = 0; index < block.edges.size(); ++index) {
    const FlowEdge& edge = block.edges[index];
    out << "\n"
        << colorize(format_flow_edge(edge, index + 1U == block.edges.size()),
                    block.reachable ? edge.tone : FlowTone::Muted, color);
  }
  return out.str();
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
  out << "MK-61 execution flow";
  if (blocks.empty())
    return out.str();
  out << "\n";
  for (std::size_t index = 0; index < blocks.size(); ++index) {
    if (index > 0)
      out << "\n";
    out << render_flow_block(blocks[index], steps, color);
    if (index + 1U < blocks.size())
      out << "\n";
  }
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
