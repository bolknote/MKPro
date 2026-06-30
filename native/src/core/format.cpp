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
constexpr int kMk61sColumns = 24;
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

std::string format_mk61s_address(std::size_t address) {
  std::ostringstream out;
  out << std::setw(4) << std::setfill('0') << address;
  return out.str();
}

std::string format_mk61s_steps_with_prefix(const std::vector<ResolvedStep>& steps,
                                           const std::string& prefix) {
  std::ostringstream out;
  for (std::size_t index = 0; index < steps.size(); index += kMk61sColumns) {
    if (index > 0)
      out << '\n';
    out << prefix << format_mk61s_address(index) << ' ';
    const std::size_t end =
        std::min(index + static_cast<std::size_t>(kMk61sColumns), steps.size());
    for (std::size_t step = index; step < end; ++step)
      out << steps.at(step).hex;
  }
  return out.str();
}

std::optional<std::string> mk61s_angle_mode_key(std::string_view mode) {
  if (mode == "rad")
    return "E";
  if (mode == "grd")
    return "9";
  if (mode == "deg")
    return "4";
  return std::nullopt;
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
  static const std::regex pattern(R"(indirect-target=([0-9]+)(?:$|[\s;,]))");
  std::smatch match;
  if (!std::regex_search(*step.comment, match, pattern))
    return std::nullopt;
  try {
    const int target = std::stoi(match[1].str());
    if (target < 0 || target > 104)
      return std::nullopt;
    return target;
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

std::string compact_flow_token(const std::vector<ResolvedStep>& steps,
                               const FlowInstruction& instruction) {
  const ResolvedStep& step = steps[instruction.step_index];
  std::ostringstream out;
  out << to_keycaps(step.mnemonic);
  if (instruction.operand_index.has_value() && instruction.target.has_value())
    out << " " << format_step_address(*instruction.target);
  return out.str();
}

std::string dot_edge_label(const FlowEdge& edge) {
  if (edge.label == "next" || edge.label == "resume" || edge.label == "after")
    return "";
  if (edge.label == "taken")
    return "yes";
  return edge.label;
}

std::string dot_escape(std::string_view value) {
  std::string escaped;
  for (const char ch : value) {
    if (ch == '\\' || ch == '"')
      escaped.push_back('\\');
    if (ch == '\n') {
      escaped += "\\n";
    } else {
      escaped.push_back(ch);
    }
  }
  return escaped;
}

std::string dot_id_for_address(int address) {
  return "n" + std::to_string(address);
}

std::string dot_color(FlowTone tone) {
  switch (tone) {
    case FlowTone::Normal:
      return "#4ade80";
    case FlowTone::Branch:
      return "#fbbf24";
    case FlowTone::Call:
      return "#60a5fa";
    case FlowTone::Return:
      return "#c084fc";
    case FlowTone::Stop:
      return "#f87171";
    case FlowTone::Unknown:
      return "#fb7185";
    case FlowTone::Muted:
      return "#64748b";
  }
  return "#94a3b8";
}

std::string dot_fill_color(FlowTone tone) {
  switch (tone) {
    case FlowTone::Branch:
      return "#3a2a05";
    case FlowTone::Call:
      return "#082f49";
    case FlowTone::Return:
      return "#2e1065";
    case FlowTone::Stop:
      return "#3f1218";
    case FlowTone::Unknown:
      return "#3f1218";
    case FlowTone::Muted:
      return "#111827";
    case FlowTone::Normal:
      return "#052e1b";
  }
  return "#0f172a";
}

std::string dot_node_label(const FlowBlock& block, const std::vector<ResolvedStep>& steps) {
  std::string label = dot_escape(flow_block_title(block)) + "\\l";
  for (const FlowInstruction& instruction : block.instructions)
    label += dot_escape("  " + compact_flow_token(steps, instruction)) + "\\l";
  return label;
}

std::string dot_unknown_node_label(const FlowEdge& edge) {
  std::string label = dot_escape("[?] " + edge.label);
  if (edge.detail.has_value())
    label += "\\l" + dot_escape("  " + *edge.detail);
  else if (edge.target.has_value())
    label += "\\l" + dot_escape("  target " + flow_ref(*edge.target));
  else
    label += "\\l" + dot_escape("  unknown target");
  return label + "\\l";
}

std::string render_dot_graph(const std::vector<FlowBlock>& blocks,
                             const std::vector<ResolvedStep>& steps) {
  std::ostringstream out;
  out << "digraph mk61_cfg {\n"
      << "  graph [rankdir=TB, splines=polyline, nodesep=0.45, ranksep=0.65, pad=0.1, "
         "bgcolor=\"#0b0f14\"];\n"
      << "  node [shape=box, style=\"rounded,filled\", fontname=\"monospace\", fontsize=10, "
         "margin=\"0.08,0.05\", penwidth=1.4, fontcolor=\"#f8fafc\"];\n"
      << "  edge [fontname=\"monospace\", fontsize=9, arrowsize=0.7, penwidth=1.2, "
         "fontcolor=\"#cbd5e1\"];\n\n";

  std::map<int, const FlowBlock*> block_by_address;
  for (const FlowBlock& block : blocks) {
    block_by_address[block.start] = &block;
    const FlowTone tone = block.reachable ? block.tone : FlowTone::Muted;
    out << "  " << dot_id_for_address(block.start) << " [label=\""
        << dot_node_label(block, steps) << "\", color=\"" << dot_color(tone)
        << "\", fillcolor=\"" << dot_fill_color(tone) << "\"";
    if (!block.reachable)
      out << ", style=\"rounded,filled,dashed\"";
    out << "];\n";
  }

  int unknown_index = 0;
  out << "\n";
  for (const FlowBlock& block : blocks) {
    const std::string source_id = dot_id_for_address(block.start);
    for (const FlowEdge& edge : block.edges) {
      std::string target_id;
      if (edge.target.has_value() && block_by_address.contains(*edge.target)) {
        target_id = dot_id_for_address(*edge.target);
      } else {
        target_id = "unknown" + std::to_string(unknown_index++);
        out << "  " << target_id << " [label=\"" << dot_unknown_node_label(edge)
            << "\", color=\"" << dot_color(FlowTone::Unknown)
            << "\", fillcolor=\"" << dot_fill_color(FlowTone::Unknown) << "\"];\n";
      }

      const FlowTone tone = block.reachable ? edge.tone : FlowTone::Muted;
      out << "  " << source_id << " -> " << target_id << " [color=\"" << dot_color(tone)
          << "\", fontcolor=\"" << dot_color(tone) << "\"";
      if (const std::string label = dot_edge_label(edge); !label.empty())
        out << ", label=\"" << dot_escape(label) << "\"";
      if (edge.tone == FlowTone::Muted || !block.reachable)
        out << ", style=dashed";
      out << "];\n";
    }
  }

  out << "}\n";
  return out.str();
}

bool is_number_entry_step(const ResolvedStep& step) {
  return (step.mnemonic.size() == 1 &&
          std::isdigit(static_cast<unsigned char>(step.mnemonic.front())) != 0) ||
         step.mnemonic == "." || step.mnemonic == "ВП" || step.mnemonic == "/-/";
}

std::optional<std::string> manual_key_for_step(const std::vector<ResolvedStep>& steps,
                                               std::size_t index) {
  if (index >= steps.size())
    return std::nullopt;
  if (index > 0 && opcode_by_code(steps.at(index - 1U).opcode).takes_address)
    return format_address(code_to_address(steps.at(index).opcode));
  const OpcodeInfo& info = opcode_by_code(steps.at(index).opcode);
  if (steps.at(index).opcode == 0x3e)
    return std::nullopt;
  return info.keys;
}

std::string manual_3e_entry_comment(const std::optional<std::string>& previous_key) {
  if (!previous_key.has_value() || previous_key->empty())
    return "manual: code 3E needs an editable previous cell";
  return "manual: ШГ← БП 3 В↑ ШГ← ШГ← " + *previous_key + " ШГ→";
}

ListingRow step_to_listing_row(const ResolvedStep& step,
                               std::optional<std::string> previous_key = std::nullopt) {
  std::optional<std::string> comment = step.comment;
  if (step.opcode == 0x3e) {
    const std::string manual = manual_3e_entry_comment(previous_key);
    comment = comment.has_value() && !comment->empty() ? *comment + "; " + manual
                                                        : manual;
  }
  return ListingRow{
      .address = step.address,
      .hex = step.hex,
      .mnemonic = step.mnemonic,
      .comment = std::move(comment),
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
      rows.push_back(step_to_listing_row(
          step, index > 0 ? manual_key_for_step(steps, index - 1U) : std::nullopt));
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

std::string format_mk61s_steps(const std::vector<ResolvedStep>& steps) {
  return format_mk61s_steps_with_prefix(steps, "");
}

std::string format_mk61s_result(const CompileResult& result) {
  std::ostringstream out;
  bool has_output = false;
  if (result.expected_mode.has_value()) {
    const std::optional<std::string> key = mk61s_angle_mode_key(*result.expected_mode);
    if (key.has_value()) {
      out << "kbd " << *key;
      has_output = true;
    }
  }

  const auto append_line_break = [&out, &has_output] {
    if (has_output)
      out << '\n';
    has_output = true;
  };

  if (!result.setup_program.has_value()) {
    append_line_break();
    out << format_mk61s_steps(result.steps);
    return out.str();
  }

  append_line_break();
  out << format_mk61s_steps_with_prefix(result.setup_program->steps, "hin ");
  out << "\nrun";
  if (!result.steps.empty())
    out << '\n' << format_mk61s_steps_with_prefix(result.steps, "hin ");
  return out.str();
}

std::string format_listing_steps(const std::vector<ResolvedStep>& steps) {
  return format_listing_rows(coalesce_number_entry_rows(steps));
}

std::string format_dot_steps(const std::vector<ResolvedStep>& steps) {
  return format_dot_steps(steps, std::vector<PreloadReport>{});
}

std::string format_dot_steps(const std::vector<ResolvedStep>& steps,
                             const std::vector<PreloadReport>& preloads) {
  const FlowContext context = build_flow_context(steps, preloads);
  const std::vector<FlowBlock> blocks = build_flow_blocks(steps, context);
  return render_dot_graph(blocks, steps);
}

std::string format_dot_result(const CompileResult& result) {
  return format_dot_steps(result.steps, result.preloads);
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
  for (std::size_t index = 0; index < steps.size(); ++index) {
    const ResolvedStep& step = steps.at(index);
    rows.push_back(step_to_listing_row(
        step, index > 0 ? manual_key_for_step(steps, index - 1U) : std::nullopt));
  }
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
