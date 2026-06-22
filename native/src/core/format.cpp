#include "mkpro/core/format.hpp"

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <iomanip>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>

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
  return std::regex_match(value.begin(), value.end(), pattern);
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
    return trimmed;

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
  if (mnemonic == "<->" || mnemonic == "X↔Y" || mnemonic == "X<->Y")
    return "↔";
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
