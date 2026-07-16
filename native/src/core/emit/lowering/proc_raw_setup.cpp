#include "mkpro/core/emit/lowering/proc_raw_setup.hpp"

#include "mkpro/core/emit/lowering_helpers.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/parser.hpp"
#include "mkpro/core/state_banks.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <exception>
#include <iomanip>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace mkpro::core::emit::lowering {

namespace {

Diagnostic diagnostic(DiagnosticSeverity severity, std::string code, std::string message) {
  return Diagnostic{
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
  };
}

std::string normalized_v2_text(std::string_view text) {
  std::string normalized;
  for (const char ch : text) {
    if (std::isspace(static_cast<unsigned char>(ch)) == 0)
      normalized.push_back(ch);
  }
  return normalized;
}

std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

std::string exact_setup_number_entry(const std::string& value);
int setup_number_entry_cost(const std::string& value);
std::string js_number_string(double value);

std::string trim_ascii(std::string value) {
  const auto first =
      std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
  const auto last =
      std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
  if (first >= last)
    return {};
  return std::string(first, last);
}

std::optional<std::string> executable_setup_value(std::string_view value) {
  std::string normalized = normalized_v2_text(value);
  std::replace(normalized.begin(), normalized.end(), ',', '.');
  static const std::regex decimal_pattern(R"(^-?\d+(?:[,.]\d+)?(?:E-?\d{1,2})?$)",
                                          std::regex_constants::icase);
  static const std::regex formal_address_pattern(R"(^[A-F][0-9A-F]$)", std::regex_constants::icase);
  if (std::regex_match(normalized, decimal_pattern))
    return normalized;
  if (std::regex_match(normalized, formal_address_pattern)) {
    const int opcode = std::stoi(normalized, nullptr, 16);
    return std::to_string(formal_address_info(opcode).actual);
  }
  return std::nullopt;
}

bool has_executable_setup_number_value(std::string_view value) {
  return executable_setup_value(value).has_value();
}

std::optional<double> executable_setup_number(std::string_view value) {
  const std::optional<std::string> executable = executable_setup_value(value);
  if (!executable.has_value())
    return std::nullopt;
  try {
    return std::stod(*executable);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::string normalize_setup_constant_text(std::string_view value) {
  std::string trimmed = trim_ascii(std::string(value));
  if (trimmed.empty())
    return trimmed;
  try {
    std::size_t consumed = 0;
    const double parsed = std::stod(trimmed, &consumed);
    if (consumed == trimmed.size() && std::isfinite(parsed))
      return js_number_string(parsed);
  } catch (const std::exception&) {
  }
  return trimmed;
}

std::optional<int> parse_integer_text(std::string_view text) {
  const std::string normalized = normalized_v2_text(text);
  if (normalized.empty())
    return std::nullopt;
  std::size_t index = 0;
  if (normalized.front() == '-' || normalized.front() == '+')
    index = 1;
  if (index >= normalized.size())
    return std::nullopt;
  for (; index < normalized.size(); ++index) {
    if (std::isdigit(static_cast<unsigned char>(normalized.at(index))) == 0)
      return std::nullopt;
  }
  try {
    return std::stoi(normalized);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::string join_setup_registers(const std::vector<std::string>& registers) {
  std::string result;
  for (std::size_t index = 0; index < registers.size(); ++index) {
    if (index > 0)
      result += ", ";
    result += "R" + registers.at(index);
  }
  return result;
}

void report_duplicate_preload_store_reuse(std::vector<OptimizationReport>& optimizations,
                                          const std::string& value,
                                          const std::vector<std::string>& stored_registers,
                                          std::size_t target_count,
                                          const std::string& target_register) {
  if (stored_registers.size() > 1U) {
    optimizations.push_back(OptimizationReport{
        .name = "duplicate-preload-store-reuse",
        .detail = "Loaded setup constant " + value + " once and stored it into " +
                  join_setup_registers(stored_registers) + ".",
    });
  }
  if (stored_registers.size() < target_count) {
    optimizations.push_back(OptimizationReport{
        .name = "duplicate-preload-register-elision",
        .detail = "Skipped " + std::to_string(target_count - stored_registers.size()) +
                  " duplicate setup store(s) to R" + target_register +
                  " after the same constant was already stored there.",
    });
  }
}

enum class DecimalSeriesOpKind {
  Op,
  Jump,
  Formal,
};

struct DecimalSeriesOp {
  DecimalSeriesOpKind kind = DecimalSeriesOpKind::Op;
  int opcode = 0;
  std::string_view mnemonic;
  int target = 0;
  std::string_view comment;
};

DecimalSeriesOp decimal_op(int opcode, std::string_view mnemonic, std::string_view comment) {
  return DecimalSeriesOp{
      .kind = DecimalSeriesOpKind::Op,
      .opcode = opcode,
      .mnemonic = mnemonic,
      .comment = comment,
  };
}

DecimalSeriesOp decimal_jump(int opcode, std::string_view mnemonic, int target,
                             std::string_view comment) {
  return DecimalSeriesOp{
      .kind = DecimalSeriesOpKind::Jump,
      .opcode = opcode,
      .mnemonic = mnemonic,
      .target = target,
      .comment = comment,
  };
}

DecimalSeriesOp decimal_formal(int opcode, std::string_view comment) {
  return DecimalSeriesOp{
      .kind = DecimalSeriesOpKind::Formal,
      .opcode = opcode,
      .comment = comment,
  };
}

bool emit_display_literal_program_to_x(MachineEmitter& setup, const DisplayLiteralProgram& program,
                                       std::string comment) {
  if (program.kind == "kinv") {
    setup.emit_number(program.digits);
    setup.emit_op(0x3a, "К ИНВ", std::move(comment), std::nullopt, true);
  } else if (program.kind == "xor") {
    setup.emit_number(program.left);
    setup.emit_op(0x0e, "В↑", comment + " split", std::nullopt, true);
    setup.emit_number(program.right);
    setup.emit_op(0x39, "К ⊕", std::move(comment), std::nullopt, true);
  } else {
    return false;
  }
  if (program.negative)
    setup.emit_op(0x0b, "/-/", "setup display literal sign", std::nullopt, true);
  return true;
}

void emit_setup_recall(MachineEmitter& setup, const std::string& register_name,
                       std::string comment);

bool emit_setup_display_first_digit(MachineEmitter& setup, int cell, std::string comment) {
  if (cell >= 0 && cell <= 9) {
    setup.emit_number(std::to_string(cell));
    if (!setup.items.empty())
      setup.items.back().comment = std::move(comment);
    return true;
  }
  if (cell >= 10 && cell <= 14) {
    setup.emit_number("1" + std::to_string(15 - cell));
    setup.emit_op(0x3a, "К ИНВ", comment, std::nullopt, true);
    setup.emit_op(0x35, "К {x}", std::move(comment), std::nullopt, true);
    return true;
  }
  return false;
}

bool emit_setup_display_exponent(MachineEmitter& setup, int exponent, std::string comment) {
  if (exponent < 0 || exponent > 99)
    return false;
  setup.emit_op(0x0c, "ВП", comment, std::nullopt, true);
  for (char ch : std::to_string(exponent))
    setup.emit_op(ch - '0', std::string(1, ch), comment, std::nullopt, true);
  return true;
}

void emit_setup_first_digit_splice(MachineEmitter& setup, std::string comment) {
  setup.emit_op(0x14, "<->", comment, std::nullopt, true);
  setup.emit_op(0x54, "К НОП", comment, std::nullopt, true);
  setup.emit_op(0x0c, "ВП", std::move(comment), std::nullopt, true);
}

bool emit_first_splice_display_literal_program_to_x(
    MachineEmitter& setup, const FirstSpliceDisplayLiteralProgram& program,
    const std::string& scratch_register, const std::string& comment,
    std::vector<OptimizationReport>& optimizations) {
  if (!emit_display_literal_program_to_x(setup, program.body, comment + " body"))
    return false;
  const int scratch_index = register_index(scratch_register);
  setup.emit_op(0x40 + scratch_index, "X->П " + scratch_register, comment + " body scratch",
                std::nullopt, true);

  if (program.first == 8) {
    emit_setup_recall(setup, scratch_register, comment + " body scratch");
    if (program.negative)
      setup.emit_op(0x0b, "/-/", comment + " sign", std::nullopt, true);
    setup.emit_op(0x54, "К НОП", comment + " first digit reuse", std::nullopt, true);
    setup.emit_op(0x0c, "ВП", comment + " first digit reuse", std::nullopt, true);
    optimizations.push_back(OptimizationReport{
        .name = "display-literal-first-digit-reuse",
        .detail = "Reused the literal body's leading 8 while restoring X2.",
    });
    return emit_setup_display_exponent(setup, program.exponent, comment + " exponent");
  }

  if (program.first == 10 && program.second.has_value() && *program.second == 10) {
    setup.emit_op(0x35, "К {x}", comment + " first digit from body", std::nullopt, true);
    emit_setup_recall(setup, scratch_register, comment + " body scratch");
    if (program.negative)
      setup.emit_op(0x0b, "/-/", comment + " sign", std::nullopt, true);
    emit_setup_first_digit_splice(setup, "display sign-digit first-cell splice");
    optimizations.push_back(OptimizationReport{
        .name = "display-literal-minus-source-reuse",
        .detail = "Derived a leading '-' from the literal body's fractional tail.",
    });
    return emit_setup_display_exponent(setup, program.exponent, comment + " exponent");
  }

  if (!emit_setup_display_first_digit(setup, program.first, comment + " first digit"))
    return false;
  emit_setup_recall(setup, scratch_register, comment + " body scratch");
  if (program.negative)
    setup.emit_op(0x0b, "/-/", comment + " sign", std::nullopt, true);
  emit_setup_first_digit_splice(setup, "display sign-digit first-cell splice");
  return emit_setup_display_exponent(setup, program.exponent, comment + " exponent");
}

std::optional<std::vector<DecimalSeriesOp>> verified_decimal_series_listing(int digits,
                                                                            int counter_start) {
  if (digits != 94 || counter_start != 65)
    return std::nullopt;

  return std::vector<DecimalSeriesOp>{
      decimal_op(0x52, "В/О", "decimal recurrence setup"),
      decimal_op(0x06, "6", "decimal recurrence setup"),
      decimal_op(0x05, "5", "decimal recurrence setup"),
      decimal_op(0x23, "F 1/x", "decimal recurrence setup"),
      decimal_op(0x40, "хП0", "decimal recurrence setup"),
      decimal_op(0x0d, "Cx", "decimal recurrence loop entry"),
      decimal_op(0xb0, "К хП0", "decimal recurrence loop entry"),
      decimal_op(0x60, "Пх0", "decimal recurrence loop entry"),
      decimal_jump(0x5e, "F x=0", 5, "decimal recurrence loop guard"),
      decimal_op(0x0f, "F Вx", "decimal recurrence scale"),
      decimal_op(0x07, "7", "decimal recurrence scale"),
      decimal_op(0x15, "F 10^x", "decimal recurrence scale"),
      decimal_op(0x20, "F π", "decimal recurrence scale"),
      decimal_op(0xde, "К Пхe", "decimal recurrence helper selector"),
      decimal_op(0x53, "ПП", "decimal recurrence helper call"),
      decimal_formal(0xe1, "decimal recurrence helper call"),
      decimal_op(0x01, "1", "decimal recurrence term"),
      decimal_op(0x10, "+", "decimal recurrence term"),
      decimal_op(0x4e, "хПe", "decimal recurrence accumulator"),
      decimal_op(0xde, "К Пхe", "decimal recurrence accumulator"),
      decimal_op(0x11, "-", "decimal recurrence accumulator"),
      decimal_jump(0x5e, "F x=0", 14, "decimal recurrence carry guard"),
      decimal_op(0x6e, "Пхe", "decimal recurrence carry"),
      decimal_op(0x0c, "ВП", "decimal recurrence carry"),
      decimal_op(0x0b, "/-/", "decimal recurrence carry"),
      decimal_op(0x02, "2", "decimal recurrence carry"),
      decimal_op(0x34, "К [x]", "decimal recurrence carry"),
      decimal_op(0x00, "0", "decimal recurrence reference gap"),
      decimal_op(0x25, "F ↻", "decimal recurrence carry"),
      decimal_op(0x10, "+", "decimal recurrence carry"),
      decimal_op(0x00, "0", "decimal recurrence reference gap"),
      decimal_op(0x0e, "В↑", "decimal recurrence carry"),
      decimal_op(0x0f, "F Вx", "decimal recurrence carry"),
      decimal_op(0x00, "0", "decimal recurrence reference gap"),
      decimal_op(0x13, "/", "decimal recurrence division"),
      decimal_op(0x0f, "F Вx", "decimal recurrence division"),
      decimal_op(0x25, "F ↻", "decimal recurrence division"),
      decimal_op(0x34, "К [x]", "decimal recurrence division"),
      decimal_op(0xbe, "К хПe", "decimal recurrence division"),
      decimal_op(0x12, "×", "decimal recurrence division"),
      decimal_op(0x11, "-", "decimal recurrence division"),
      decimal_op(0x06, "6", "decimal recurrence normalization"),
      decimal_op(0x15, "F 10^x", "decimal recurrence normalization"),
      decimal_op(0x12, "×", "decimal recurrence normalization"),
      decimal_op(0x6e, "Пхe", "decimal recurrence accumulator update"),
      decimal_op(0x0c, "ВП", "decimal recurrence accumulator update"),
      decimal_op(0x02, "2", "decimal recurrence accumulator update"),
      decimal_op(0x4e, "хПe", "decimal recurrence accumulator update"),
      decimal_op(0x10, "+", "decimal recurrence accumulator update"),
      decimal_op(0x32, "К ЗН", "decimal recurrence accumulator update"),
      decimal_op(0x11, "-", "decimal recurrence accumulator update"),
      decimal_jump(0x5e, "F x=0", 11, "decimal recurrence next term"),
      decimal_op(0x6e, "Пхe", "decimal recurrence final mantissa"),
      decimal_op(0x02, "2", "decimal recurrence final mantissa"),
      decimal_op(0x05, "5", "decimal recurrence final mantissa"),
      decimal_op(0x10, "+", "decimal recurrence final mantissa"),
      decimal_op(0x4e, "хПe", "decimal recurrence final mantissa"),
      decimal_op(0x01, "1", "decimal recurrence exponent"),
      decimal_op(0x16, "F e^x", "decimal recurrence exponent"),
      decimal_op(0x40, "хП0", "decimal recurrence result"),
      decimal_op(0x50, "С/П", "decimal recurrence stop"),
  };
}

} // namespace

std::optional<DecimalSeriesProgram> match_decimal_series_program(const V2Program& program) {
  if (program.body.size() != 5 || !program.consts.empty() || !program.state.empty() ||
      !program.boards.empty() || !program.worlds.empty() || !program.rules.empty()) {
    return std::nullopt;
  }

  const V2Statement& precision = program.body.at(0);
  const V2Statement& counter_init = program.body.at(1);
  const V2Statement& value_init = program.body.at(2);
  const V2Statement& loop = program.body.at(3);
  const V2Statement& stop = program.body.at(4);
  if (precision.kind != "v2_assign" || precision.target != "digits" ||
      !precision.expr.has_value()) {
    return std::nullopt;
  }
  const std::optional<int> digits = parse_integer_text(*precision.expr);
  if (!digits.has_value())
    return std::nullopt;

  if (counter_init.kind != "v2_assign" || !counter_init.target.has_value() ||
      !counter_init.expr.has_value()) {
    return std::nullopt;
  }
  const std::optional<int> counter_start = parse_integer_text(*counter_init.expr);
  if (!counter_start.has_value())
    return std::nullopt;

  if (value_init.kind != "v2_assign" || !value_init.target.has_value() ||
      !value_init.expr.has_value() || normalized_v2_text(*value_init.expr) != "1") {
    return std::nullopt;
  }

  const std::string& counter_name = *counter_init.target;
  const std::string& value_name = *value_init.target;
  if (loop.kind != "v2_while" || !loop.predicate.has_value() || loop.body.size() != 2 ||
      stop.kind != "v2_stop" || !stop.target.has_value() ||
      normalized_v2_text(*stop.target) != value_name) {
    return std::nullopt;
  }
  const V2Predicate& predicate = *loop.predicate;
  if (predicate.kind != "v2_compare" || normalized_v2_text(predicate.left) != counter_name ||
      predicate.op != ">=" || normalized_v2_text(predicate.right) != "1") {
    return std::nullopt;
  }

  const V2Statement& step = loop.body.at(0);
  const V2Statement& decrement = loop.body.at(1);
  if (step.kind != "v2_assign" || step.target != value_name || !step.expr.has_value() ||
      normalized_v2_text(*step.expr) != "1+" + value_name + "/" + counter_name ||
      decrement.kind != "v2_update" || decrement.target != counter_name || decrement.op != "-=" ||
      !decrement.expr.has_value() || normalized_v2_text(*decrement.expr) != "1") {
    return std::nullopt;
  }

  return DecimalSeriesProgram{
      .digits = *digits,
      .counter_start = *counter_start,
      .line = program.line,
  };
}

bool lower_decimal_series_program(MachineEmitter& emitter, std::vector<Diagnostic>& diagnostics,
                                  const DecimalSeriesProgram& program) {
  const std::optional<std::vector<DecimalSeriesOp>> listing =
      verified_decimal_series_listing(program.digits, program.counter_start);
  if (!listing.has_value()) {
    diagnostics.push_back(diagnostic(
        DiagnosticSeverity::Error, "native-unsupported",
        "No verified decimal recurrence listing for " + std::to_string(program.digits) +
            "-digit precision with counter " + std::to_string(program.counter_start) +
            ". Verified pairs: (94, 65). The hand-tuned recurrence byte sequence must be "
            "validated on hardware before a new pair can be added."));
    return false;
  }

  for (const DecimalSeriesOp& instruction : *listing) {
    const std::string comment(instruction.comment);
    if (instruction.kind == DecimalSeriesOpKind::Op) {
      emitter.emit_op(instruction.opcode, std::string(instruction.mnemonic), comment, program.line);
    } else if (instruction.kind == DecimalSeriesOpKind::Jump) {
      emitter.emit_jump(instruction.opcode, std::string(instruction.mnemonic), instruction.target,
                        comment, program.line);
    } else {
      emitter.emit_formal_address(instruction.opcode, comment, program.line);
    }
  }
  return true;
}

namespace {

struct RandomUniqueCoordListValue {
  std::string domain;
  std::string list_name;
  int index = 0;
  int count = 0;
  bool scaled_decimal = false;
};

struct RandomUniqueSegmentedBitplaneValue {
  std::string collection;
  std::string count_source;
  int plane_index = 0;
};

std::optional<RandomUniqueCoordListValue>
parse_random_unique_coord_list_value(std::string_view value) {
  static const std::regex pattern(
      R"(^random_unique\(([A-Za-z_][A-Za-z0-9_]*),([A-Za-z_][A-Za-z0-9_]*),(\d+),(\d+)(,scaled)?\)$)");
  std::cmatch match;
  if (!std::regex_match(value.data(), value.data() + value.size(), match, pattern))
    return std::nullopt;
  return RandomUniqueCoordListValue{
      .domain = match[1].str(),
      .list_name = match[2].str(),
      .index = std::stoi(match[3].str()),
      .count = std::stoi(match[4].str()),
      .scaled_decimal = match[5].matched,
  };
}

std::optional<RandomUniqueSegmentedBitplaneValue>
parse_random_unique_segmented_bitplane_value(std::string_view value) {
  static const std::regex pattern(
      R"(^__seg_bitplane_random_unique\(([A-Za-z_][A-Za-z0-9_]*),([A-Za-z_][A-Za-z0-9_]*),(\d+)\)$)");
  std::cmatch match;
  if (!std::regex_match(value.data(), value.data() + value.size(), match, pattern))
    return std::nullopt;
  return RandomUniqueSegmentedBitplaneValue{
      .collection = match[1].str(),
      .count_source = match[2].str(),
      .plane_index = std::stoi(match[3].str()),
  };
}

void emit_setup_integer_offset(MachineEmitter& setup, int offset) {
  if (offset == 0)
    return;
  setup.emit_number(std::to_string(offset));
  setup.emit_op(0x10, "+", "expr +", std::nullopt, true);
}

void emit_setup_store(MachineEmitter& setup, const std::string& register_name,
                      std::string comment) {
  const int reg_index = register_index(register_name);
  setup.emit_op(0x40 + reg_index, "X->П " + register_name, std::move(comment), std::nullopt, true);
}

std::string setup_target_name(const PreloadReport& preload) {
  return preload.setup_target_name.value_or("R" + preload.register_name);
}

std::string setup_store_comment(const PreloadReport& preload) {
  return "setup " + setup_target_name(preload);
}

bool is_formatted_coord_report_mask_preload(const PreloadReport& preload) {
  return !preload.setup_expression && preload.value == "8,-00--_";
}

std::string setup_display_literal_comment(const PreloadReport& preload) {
  return is_formatted_coord_report_mask_preload(preload) ? "setup formatted report mask"
                                                         : setup_store_comment(preload);
}

bool is_stack_preload_value(const std::string& value) {
  return value == "stack.X" || value == "stack.Y" || value == "stack.Z" ||
         value == "stack.T" || value == "stack.X2";
}

std::optional<int> visible_stack_preload_reverse_count(const std::string& value) {
  if (value == "stack.X")
    return 0;
  if (value == "stack.Y")
    return std::nullopt;
  if (value == "stack.Z")
    return 2;
  if (value == "stack.T")
    return 3;
  return std::nullopt;
}

void emit_visible_stack_preload_setup(MachineEmitter& setup, const PreloadReport& preload) {
  const std::string target = setup_target_name(preload);
  if (preload.value == "stack.Y") {
    setup.emit_op(0x14, "X↔Y", "setup " + target + " from stack.Y", std::nullopt, true);
    return;
  }
  const std::optional<int> reverse_count = visible_stack_preload_reverse_count(preload.value);
  if (!reverse_count.has_value())
    return;
  for (int index = 0; index < *reverse_count; ++index) {
    setup.emit_op(0x25, "F reverse",
                  index == 0 ? "setup " + target + " from " + preload.value
                             : "continue setup " + target + " from " + preload.value,
                  std::nullopt, true);
  }
}

void emit_visible_stack_preload_restore(MachineEmitter& setup, const PreloadReport& preload) {
  const std::string target = setup_target_name(preload);
  if (preload.value == "stack.Y") {
    setup.emit_op(0x14, "X↔Y", "restore stack.X after " + target, std::nullopt, true);
    return;
  }
  const std::optional<int> reverse_count = visible_stack_preload_reverse_count(preload.value);
  if (!reverse_count.has_value() || *reverse_count == 0)
    return;
  const int restore_count = 4 - *reverse_count;
  for (int index = 0; index < restore_count; ++index) {
    setup.emit_op(0x25, "F reverse",
                  index == 0 ? "restore stack.X after " + target
                             : "continue restore stack.X after " + target,
                  std::nullopt, true);
  }
}

void emit_setup_recall(MachineEmitter& setup, const std::string& register_name,
                       std::string comment) {
  const int reg_index = register_index(register_name);
  setup.emit_op(0x60 + reg_index, "П->X " + register_name, std::move(comment), std::nullopt, true);
}

void emit_negative_zero_degree_setup(MachineEmitter& setup, const std::string& register_name) {
  setup.emit_op(0x54, "К НОП", "negative-zero degree seed", std::nullopt, true);
  setup.emit_op(0x01, "1", "negative-zero degree seed", std::nullopt, true);
  setup.emit_op(0x03, "3", "negative-zero degree seed", std::nullopt, true);
  emit_setup_store(setup, register_name, "negative-zero degree seed");
  setup.emit_op(0x01, "1", "negative-zero degree mantissa", std::nullopt, true);
  setup.emit_op(0x08, "8", "negative-zero degree mantissa", std::nullopt, true);
  setup.emit_op(0x38, "К ∨", "negative-zero degree mantissa", std::nullopt, true);
  setup.emit_op(0x35, "К {x}", "negative-zero degree mantissa", std::nullopt, true);
  setup.emit_op(0x0b, "/-/", "negative-zero degree sign", std::nullopt, true);
  setup.emit_op(0x0c, "ВП", "negative-zero degree exponent", std::nullopt, true);
  setup.emit_op(0x02, "2", "negative-zero degree exponent", std::nullopt, true);
  setup.emit_op(0x15, "F 10^x", "negative-zero degree exponent", std::nullopt, true);
  setup.emit_op(0x0e, "В↑", "negative-zero degree normalize", std::nullopt, true);
  setup.emit_op(0x0c, "ВП", "negative-zero degree exponent", std::nullopt, true);
  setup.emit_op(0x0b, "/-/", "negative-zero degree exponent sign", std::nullopt, true);
  setup.emit_op(0x05, "5", "negative-zero degree exponent", std::nullopt, true);
  setup.emit_op(0x00, "0", "negative-zero degree exponent", std::nullopt, true);
  emit_setup_store(setup, register_name, "setup R" + register_name);
}

bool emit_stack_preload_setup(MachineEmitter& setup, const PreloadReport& preload) {
  if (!is_stack_preload_value(preload.value))
    return false;
  const std::string target = setup_target_name(preload);
  emit_visible_stack_preload_setup(setup, preload);
  if (preload.value == "stack.X2")
    setup.emit_op(0x0a, ".", "setup " + target + " from stack.X2", std::nullopt, true);
  emit_setup_store(setup, preload.register_name, "setup " + target);
  emit_visible_stack_preload_restore(setup, preload);
  return true;
}

std::string default_expected_mode_probe_value(std::string_view expected_mode) {
  if (expected_mode == "deg")
    return "272";
  if (expected_mode == "grd")
    return "91";
  return "100";
}

void emit_expected_mode_setup_check(MachineEmitter& setup,
                                    std::vector<OptimizationReport>& optimizations,
                                    const std::string& expected_mode) {
  setup.emit_number(default_expected_mode_probe_value(expected_mode));
  setup.emit_op(0x1d, "F cos", "expected_mode(\"" + expected_mode + "\") cosine",
                std::nullopt, true);
  setup.emit_op(0x18, "F ln", "expected_mode(\"" + expected_mode + "\") domain guard",
                std::nullopt, true);
  optimizations.push_back(OptimizationReport{
      .name = "expected-mode-setup-check",
      .detail = "Inserted setup-time expected_mode(\"" + expected_mode + "\") guard with probe " +
                default_expected_mode_probe_value(expected_mode) + ".",
  });
}

std::optional<std::pair<int, std::string>> setup_unary_opcode(const std::string& name) {
  static const std::map<std::string, std::pair<int, std::string>> opcodes = {
      {"abs", {0x31, "К |x|"}},      {"sign", {0x32, "К ЗН"}},     {"int", {0x34, "К [x]"}},
      {"frac", {0x35, "К {x}"}},     {"sqr", {0x22, "F x²"}},      {"inv", {0x23, "F 1/x"}},
      {"sqrt", {0x21, "F √"}},       {"lg", {0x17, "F lg"}},       {"ln", {0x18, "F ln"}},
      {"sin", {0x1c, "F sin"}},      {"cos", {0x1d, "F cos"}},     {"tg", {0x1e, "F tg"}},
      {"asin", {0x19, "F sin⁻¹"}},   {"acos", {0x1a, "F cos⁻¹"}},  {"atg", {0x1b, "F tg⁻¹"}},
      {"exp", {0x16, "F eˣ"}},       {"pow10", {0x15, "F 10ˣ"}},   {"bit_not", {0x3a, "К ИНВ"}},
      {"to_min", {0x26, "К °→′"}},   {"to_sec", {0x2a, "К °→′″"}}, {"from_sec", {0x30, "К °←′″"}},
      {"from_min", {0x33, "К °←′"}},
  };
  const auto it = opcodes.find(lower_ascii(name));
  return it == opcodes.end() ? std::nullopt
                             : std::optional<std::pair<int, std::string>>{it->second};
}

std::optional<std::pair<int, std::string>> setup_binary_opcode(const std::string& op) {
  if (op == "+")
    return std::pair{0x10, std::string("+")};
  if (op == "-")
    return std::pair{0x11, std::string("-")};
  if (op == "*")
    return std::pair{0x12, std::string("×")};
  if (op == "/")
    return std::pair{0x13, std::string("÷")};
  return std::nullopt;
}

std::optional<std::pair<int, std::string>> setup_binary_call_opcode(const std::string& callee) {
  static const std::map<std::string, std::pair<int, std::string>> opcodes = {
      {"pow", {0x24, "F x^y"}},  {"max", {0x36, "К max"}},   {"bit_and", {0x37, "К ∧"}},
      {"bit_or", {0x38, "К ∨"}}, {"bit_xor", {0x39, "К ⊕"}},
  };
  const auto it = opcodes.find(lower_ascii(callee));
  return it == opcodes.end() ? std::nullopt
                             : std::optional<std::pair<int, std::string>>{it->second};
}

struct NumericSetupPreload {
  std::size_t preload_index = 0;
  std::string register_name;
  std::string value;
};

struct SetupSequenceOp {
  int opcode = 0;
  std::string mnemonic;
  std::string comment;
};

struct SetupNumericPreloadAction {
  std::string kind;
  int target_index = 0;
  int cost = 0;
  std::vector<int> extra_target_indexes;
  int source_index = -1;
  int left_index = -1;
  int right_index = -1;
  int opcode = 0;
  std::string mnemonic;
  std::vector<SetupSequenceOp> ops;
  std::string op;
  std::string exponent;
  std::string detail;
};

int setup_number_entry_cost(const std::string& value) {
  MachineEmitter estimator;
  estimator.emit_number(exact_setup_number_entry(value));
  return static_cast<int>(estimator.items.size());
}

std::string exact_setup_number_entry(const std::string& raw) {
  const std::string normalized = lower_ascii(trim_ascii(raw));
  const std::size_t sign_size =
      !normalized.empty() && (normalized.front() == '-' || normalized.front() == '+') ? 1U : 0U;
  const std::string_view unsigned_value(normalized.data() + sign_size,
                                        normalized.size() - sign_size);
  if (unsigned_value.find_first_of("eE") != std::string_view::npos)
    return normalized;

  const std::size_t point = unsigned_value.find('.');
  if (point == std::string_view::npos)
    return normalized;
  const std::string_view integer = unsigned_value.substr(0, point);
  const std::string_view fraction = unsigned_value.substr(point + 1U);
  if (integer.empty() ||
      !std::all_of(integer.begin(), integer.end(), [](char ch) { return ch == '0'; }) ||
      !std::all_of(fraction.begin(), fraction.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
      }) ||
      fraction.size() <= 7U ||
      std::all_of(fraction.begin() + 7, fraction.end(), [](char ch) { return ch == '0'; })) {
    return normalized;
  }

  const std::size_t first_nonzero = fraction.find_first_not_of('0');
  if (first_nonzero == std::string_view::npos)
    return normalized;
  std::string significant(fraction.substr(first_nonzero));
  while (significant.size() > 8U && significant.back() == '0')
    significant.pop_back();
  if (significant.size() > 8U)
    return normalized;

  std::string result;
  if (sign_size != 0U && normalized.front() == '-')
    result.push_back('-');
  result.push_back(significant.front());
  if (significant.size() > 1U) {
    result.push_back('.');
    result += significant.substr(1U);
  }
  result += "e-" + std::to_string(first_nonzero + 1U);
  return result;
}

void emit_exact_setup_number(MachineEmitter& setup, const std::string& value) {
  setup.emit_number(exact_setup_number_entry(value));
}

std::string js_number_string(double value) {
  if (value == 0.0)
    return "0";
  if (std::isfinite(value) && std::floor(value) == value &&
      value >= static_cast<double>(std::numeric_limits<long long>::min()) &&
      value <= static_cast<double>(std::numeric_limits<long long>::max())) {
    const long long integer = static_cast<long long>(value);
    if (static_cast<double>(integer) == value)
      return std::to_string(integer);
  }

  const double abs_value = std::fabs(value);
  const bool use_exponent = abs_value >= 1e21 || abs_value < 1e-6;
  char buffer[128]{};
  const auto [end, error] =
      use_exponent ? std::to_chars(buffer, buffer + sizeof(buffer), value)
                   : std::to_chars(buffer, buffer + sizeof(buffer), value,
                                   std::chars_format::fixed);
  std::string text;
  if (error == std::errc{}) {
    text.assign(buffer, end);
  } else {
    std::ostringstream out;
    if (use_exponent)
      out << std::scientific;
    else
      out << std::fixed;
    out << std::setprecision(17) << value;
    text = out.str();
  }
  text = lower_ascii(text);
  const std::size_t exponent = text.find('e');
  std::string suffix;
  if (exponent != std::string::npos) {
    suffix = text.substr(exponent + 1U);
    text.erase(exponent);
  }
  if (text.find('.') != std::string::npos) {
    while (!text.empty() && text.back() == '0')
      text.pop_back();
    if (!text.empty() && text.back() == '.')
      text.pop_back();
  }
  if (!suffix.empty()) {
    char sign = '+';
    if (suffix.front() == '+' || suffix.front() == '-') {
      sign = suffix.front();
      suffix.erase(suffix.begin());
    }
    while (suffix.size() > 1U && suffix.front() == '0')
      suffix.erase(suffix.begin());
    text += "e";
    if (sign == '-')
      text += "-";
    text += suffix;
  }
  return text.empty() ? "0" : text;
}

std::optional<int> positive_integer_power_of_ten_exponent(const std::string& value) {
  std::string normalized = normalize_setup_constant_text(value);
  if (normalized.empty() || normalized.front() == '-')
    return std::nullopt;
  if (normalized == "1")
    return 0;
  if (normalized.front() != '1')
    return std::nullopt;
  for (std::size_t index = 1; index < normalized.size(); ++index) {
    if (normalized.at(index) != '0')
      return std::nullopt;
  }
  return static_cast<int>(normalized.size() - 1U);
}

void emit_setup_number_or_pow10(MachineEmitter& setup, const std::string& value,
                                std::string comment) {
  if (const std::optional<int> exponent = positive_integer_power_of_ten_exponent(value)) {
    const std::string exponent_text = std::to_string(*exponent);
    if (setup_number_entry_cost(exponent_text) + 1 < setup_number_entry_cost(value)) {
      emit_exact_setup_number(setup, exponent_text);
      setup.emit_op(0x15, "F 10^x", std::move(comment), std::nullopt, true);
      return;
    }
  }
  emit_exact_setup_number(setup, value);
}

std::vector<int> setup_numeric_action_target_indexes(const SetupNumericPreloadAction& action) {
  std::vector<int> indexes{action.target_index};
  indexes.insert(indexes.end(), action.extra_target_indexes.begin(),
                 action.extra_target_indexes.end());
  return indexes;
}

SetupNumericPreloadAction with_duplicate_setup_targets(SetupNumericPreloadAction action,
                                                       const std::vector<std::string>& normalized,
                                                       int loaded_mask) {
  const std::string& value = normalized.at(static_cast<std::size_t>(action.target_index));
  for (std::size_t index = static_cast<std::size_t>(action.target_index) + 1U;
       index < normalized.size(); ++index) {
    if ((loaded_mask & (1 << static_cast<int>(index))) != 0)
      continue;
    if (normalized.at(index) == value)
      action.extra_target_indexes.push_back(static_cast<int>(index));
  }
  return action;
}

std::vector<SetupNumericPreloadAction> setup_constant_synthesis_actions(
    const std::vector<NumericSetupPreload>& preloads, const std::vector<std::string>& normalized,
    const std::vector<double>& numeric, const std::vector<int>& direct_costs, int loaded_mask,
    int target_index) {
  const double target = numeric.at(static_cast<std::size_t>(target_index));
  if (!std::isfinite(target))
    return {};
  const std::string& target_value = normalized.at(static_cast<std::size_t>(target_index));
  const int direct_cost = direct_costs.at(static_cast<std::size_t>(target_index));
  std::vector<SetupNumericPreloadAction> actions;
  auto accept = [&](SetupNumericPreloadAction action) {
    if (action.cost < direct_cost)
      actions.push_back(std::move(action));
  };

  std::vector<int> loaded_indexes;
  for (std::size_t index = 0; index < preloads.size(); ++index) {
    if ((loaded_mask & (1 << static_cast<int>(index))) != 0)
      loaded_indexes.push_back(static_cast<int>(index));
  }

  const std::string negated = normalize_setup_constant_text(js_number_string(-target));
  const auto negated_it =
      std::find_if(loaded_indexes.begin(), loaded_indexes.end(), [&](int index) {
        return normalized.at(static_cast<std::size_t>(index)) == negated;
      });
  if (negated_it != loaded_indexes.end()) {
    const int source_index = *negated_it;
    accept(SetupNumericPreloadAction{
        .kind = "unary",
        .target_index = target_index,
        .cost = 2,
        .source_index = source_index,
        .opcode = 0x0b,
        .mnemonic = "/-/",
        .detail = "changed the sign of setup R" +
                  preloads.at(static_cast<std::size_t>(source_index)).register_name + " (" +
                  normalized.at(static_cast<std::size_t>(source_index)) + ")",
    });
  }

  if (std::floor(target) == target && target >= 0.0) {
    for (const int source_index : loaded_indexes) {
      const double source = numeric.at(static_cast<std::size_t>(source_index));
      if (std::floor(source) != source)
        continue;
      const double squared = source * source;
      if (std::floor(squared) != squared)
        continue;
      if (normalize_setup_constant_text(js_number_string(squared)) != target_value)
        continue;
      accept(SetupNumericPreloadAction{
          .kind = "unary",
          .target_index = target_index,
          .cost = 2,
          .source_index = source_index,
          .opcode = 0x22,
          .mnemonic = "F x^2",
          .detail = "squared setup R" +
                    preloads.at(static_cast<std::size_t>(source_index)).register_name + " (" +
                    normalized.at(static_cast<std::size_t>(source_index)) + ")",
      });
    }
  }

  for (const int source_index : loaded_indexes) {
    const double source = numeric.at(static_cast<std::size_t>(source_index));
    if (std::floor(source) != source)
      continue;
    const double doubled = source * 2.0;
    if (std::floor(doubled) == doubled &&
        normalize_setup_constant_text(js_number_string(doubled)) == target_value) {
      accept(SetupNumericPreloadAction{
          .kind = "unary-sequence",
          .target_index = target_index,
          .cost = 3,
          .source_index = source_index,
          .ops = {SetupSequenceOp{.opcode = 0x0e, .mnemonic = "В↑", .comment = "stack"},
                  SetupSequenceOp{.opcode = 0x10, .mnemonic = "+", .comment = ""}},
          .detail = "doubled setup R" +
                    preloads.at(static_cast<std::size_t>(source_index)).register_name + " (" +
                    normalized.at(static_cast<std::size_t>(source_index)) + ")",
      });
    }
    if (std::fmod(source, 2.0) == 0.0) {
      const double half = source / 2.0;
      if (std::floor(half) == half &&
          normalize_setup_constant_text(js_number_string(half)) == target_value) {
        accept(SetupNumericPreloadAction{
            .kind = "unary-sequence",
            .target_index = target_index,
            .cost = 3,
            .source_index = source_index,
            .ops = {SetupSequenceOp{.opcode = 0x02, .mnemonic = "2", .comment = "divisor"},
                    SetupSequenceOp{.opcode = 0x13, .mnemonic = "/", .comment = ""}},
            .detail = "halved setup R" +
                      preloads.at(static_cast<std::size_t>(source_index)).register_name + " (" +
                      normalized.at(static_cast<std::size_t>(source_index)) + ")",
        });
      }
    }
  }

  for (const int left_index : loaded_indexes) {
    const double left = numeric.at(static_cast<std::size_t>(left_index));
    if (std::floor(left) != left)
      continue;
    for (const int right_index : loaded_indexes) {
      const double right = numeric.at(static_cast<std::size_t>(right_index));
      if (std::floor(right) != right)
        continue;
      std::vector<std::pair<std::string, double>> candidates = {
          {"+", left + right},
          {"-", left - right},
          {"*", left * right},
      };
      if (right != 0.0 && std::fmod(left, right) == 0.0)
        candidates.push_back({"/", left / right});
      if (left > 0.0 && right >= 0.0 && right <= 12.0)
        candidates.push_back({"pow", std::pow(left, right)});
      for (const auto& [op, value] : candidates) {
        if (!std::isfinite(value) || std::floor(value) != value)
          continue;
        if (normalize_setup_constant_text(js_number_string(value)) != target_value)
          continue;
        accept(SetupNumericPreloadAction{
            .kind = "binary",
            .target_index = target_index,
            .cost = 4,
            .left_index = left_index,
            .right_index = right_index,
            .op = op,
            .detail = "combined setup R" +
                      preloads.at(static_cast<std::size_t>(left_index)).register_name + " (" +
                      normalized.at(static_cast<std::size_t>(left_index)) + ") and R" +
                      preloads.at(static_cast<std::size_t>(right_index)).register_name + " (" +
                      normalized.at(static_cast<std::size_t>(right_index)) + ") with " +
                      (op == "pow" ? "F x^y" : op),
        });
      }
    }
  }

  if (const std::optional<int> exponent = positive_integer_power_of_ten_exponent(target_value)) {
    accept(SetupNumericPreloadAction{
        .kind = "pow10",
        .target_index = target_index,
        .cost = setup_number_entry_cost(std::to_string(*exponent)) + 1,
        .exponent = std::to_string(*exponent),
        .detail = "loaded exponent " + std::to_string(*exponent) + " and applied F 10^x",
    });
  }
  return actions;
}

std::vector<SetupNumericPreloadAction>
direct_setup_numeric_preload_actions(const std::vector<NumericSetupPreload>& preloads) {
  std::vector<std::string> normalized;
  normalized.reserve(preloads.size());
  for (const NumericSetupPreload& preload : preloads)
    normalized.push_back(normalize_setup_constant_text(preload.value));

  std::set<int> covered;
  std::vector<SetupNumericPreloadAction> actions;
  for (std::size_t target_index = 0; target_index < preloads.size(); ++target_index) {
    if (covered.contains(static_cast<int>(target_index)))
      continue;
    SetupNumericPreloadAction action{
        .kind = "direct",
        .target_index = static_cast<int>(target_index),
        .cost = setup_number_entry_cost(preloads.at(target_index).value),
    };
    const std::string& value = normalized.at(target_index);
    for (std::size_t index = target_index + 1U; index < preloads.size(); ++index) {
      if (!covered.contains(static_cast<int>(index)) && normalized.at(index) == value)
        action.extra_target_indexes.push_back(static_cast<int>(index));
    }
    for (const int index : setup_numeric_action_target_indexes(action))
      covered.insert(index);
    actions.push_back(std::move(action));
  }
  return actions;
}

std::vector<SetupNumericPreloadAction>
setup_numeric_preload_actions(const std::vector<NumericSetupPreload>& preloads) {
  std::string cache_key;
  for (const NumericSetupPreload& preload : preloads) {
    cache_key += preload.register_name;
    cache_key += '=';
    cache_key += preload.value;
    cache_key += ';';
  }
  static thread_local std::map<std::string, std::vector<SetupNumericPreloadAction>> cache;
  if (const auto cached = cache.find(cache_key); cached != cache.end())
    return cached->second;

  constexpr std::size_t kMaxExactSetupConstantSynthesisPreloads = 10;
  const int count = static_cast<int>(preloads.size());
  if (count == 0)
    return {};
  if (preloads.size() > kMaxExactSetupConstantSynthesisPreloads) {
    std::vector<SetupNumericPreloadAction> direct = direct_setup_numeric_preload_actions(preloads);
    cache.emplace(std::move(cache_key), direct);
    return direct;
  }

  std::vector<std::string> normalized;
  std::vector<double> numeric;
  std::vector<int> direct_costs;
  normalized.reserve(preloads.size());
  numeric.reserve(preloads.size());
  direct_costs.reserve(preloads.size());
  for (const NumericSetupPreload& preload : preloads) {
    const std::string normalized_value = normalize_setup_constant_text(preload.value);
    normalized.push_back(normalized_value);
    try {
      numeric.push_back(std::stod(normalized_value));
    } catch (const std::exception&) {
      numeric.push_back(std::numeric_limits<double>::quiet_NaN());
    }
    direct_costs.push_back(setup_number_entry_cost(preload.value));
  }

  const int full_mask = (1 << count) - 1;
  std::vector<int> best(static_cast<std::size_t>(full_mask + 1),
                        std::numeric_limits<int>::max() / 4);
  struct PreviousStep {
    int mask = 0;
    SetupNumericPreloadAction action;
    bool present = false;
  };
  std::vector<PreviousStep> previous(static_cast<std::size_t>(full_mask + 1));
  best.at(0) = 0;

  auto apply = [&](int mask, SetupNumericPreloadAction action) {
    const std::vector<int> targets = setup_numeric_action_target_indexes(action);
    if (std::any_of(targets.begin(), targets.end(),
                    [&](int target) { return (mask & (1 << target)) != 0; })) {
      return;
    }
    int next_mask = mask;
    for (const int target : targets)
      next_mask |= 1 << target;
    const int next_cost = best.at(static_cast<std::size_t>(mask)) + action.cost;
    if (next_cost >= best.at(static_cast<std::size_t>(next_mask)))
      return;
    best.at(static_cast<std::size_t>(next_mask)) = next_cost;
    previous.at(static_cast<std::size_t>(next_mask)) =
        PreviousStep{.mask = mask, .action = std::move(action), .present = true};
  };

  for (int mask = 0; mask <= full_mask; ++mask) {
    if (best.at(static_cast<std::size_t>(mask)) >= std::numeric_limits<int>::max() / 8)
      continue;
    for (int target_index = 0; target_index < count; ++target_index) {
      if ((mask & (1 << target_index)) != 0)
        continue;
      apply(mask, with_duplicate_setup_targets(
                      SetupNumericPreloadAction{
                          .kind = "direct",
                          .target_index = target_index,
                          .cost = direct_costs.at(static_cast<std::size_t>(target_index)),
                      },
                      normalized, mask));
      for (SetupNumericPreloadAction action : setup_constant_synthesis_actions(
               preloads, normalized, numeric, direct_costs, mask, target_index)) {
        apply(mask, with_duplicate_setup_targets(std::move(action), normalized, mask));
      }
    }
  }

  if (best.at(static_cast<std::size_t>(full_mask)) >= std::numeric_limits<int>::max() / 8) {
    std::vector<SetupNumericPreloadAction> direct = direct_setup_numeric_preload_actions(preloads);
    cache.emplace(std::move(cache_key), direct);
    return direct;
  }

  std::vector<SetupNumericPreloadAction> actions;
  for (int mask = full_mask; mask != 0;) {
    const PreviousStep& step = previous.at(static_cast<std::size_t>(mask));
    if (!step.present)
      break;
    actions.push_back(step.action);
    mask = step.mask;
  }
  std::reverse(actions.begin(), actions.end());
  cache.emplace(std::move(cache_key), actions);
  return actions;
}

void emit_setup_numeric_preload_action(MachineEmitter& setup,
                                       std::vector<OptimizationReport>& optimizations,
                                       const std::vector<NumericSetupPreload>& preloads,
                                       const SetupNumericPreloadAction& action) {
  const NumericSetupPreload& target = preloads.at(static_cast<std::size_t>(action.target_index));
  const std::string target_value = normalize_setup_constant_text(target.value);
  if (action.kind == "direct") {
    emit_exact_setup_number(setup, target.value);
    return;
  }
  if (action.kind == "pow10") {
    emit_exact_setup_number(setup, action.exponent);
    setup.emit_op(0x15, "F 10^x", "setup constant " + target_value, std::nullopt, true);
  } else if (action.kind == "unary") {
    const NumericSetupPreload& source = preloads.at(static_cast<std::size_t>(action.source_index));
    emit_setup_recall(setup, source.register_name,
                      "setup constant " + target_value + " base " +
                          normalize_setup_constant_text(source.value));
    setup.emit_op(action.opcode, action.mnemonic, "setup constant " + target_value, std::nullopt,
                  true);
  } else if (action.kind == "unary-sequence") {
    const NumericSetupPreload& source = preloads.at(static_cast<std::size_t>(action.source_index));
    emit_setup_recall(setup, source.register_name,
                      "setup constant " + target_value + " base " +
                          normalize_setup_constant_text(source.value));
    for (const SetupSequenceOp& op : action.ops) {
      const std::string comment = op.comment.empty()
                                      ? "setup constant " + target_value
                                      : "setup constant " + target_value + " " + op.comment;
      setup.emit_op(op.opcode, op.mnemonic, comment, std::nullopt, true);
    }
  } else {
    const NumericSetupPreload& left = preloads.at(static_cast<std::size_t>(action.left_index));
    const NumericSetupPreload& right = preloads.at(static_cast<std::size_t>(action.right_index));
    emit_setup_recall(setup, left.register_name,
                      "setup constant " + target_value + " left " +
                          normalize_setup_constant_text(left.value));
    setup.emit_op(0x0e, "В↑", "setup constant " + target_value + " stack", std::nullopt, true);
    emit_setup_recall(setup, right.register_name,
                      "setup constant " + target_value + " right " +
                          normalize_setup_constant_text(right.value));
    if (action.op == "pow") {
      setup.emit_op(0x24, "F x^y", "setup constant " + target_value, std::nullopt, true);
    } else if (const std::optional<std::pair<int, std::string>> opcode =
                   setup_binary_opcode(action.op)) {
      setup.emit_op(opcode->first, opcode->second, "setup constant " + target_value, std::nullopt,
                    true);
    }
  }
  optimizations.push_back(OptimizationReport{
      .name = "constant-synthesis",
      .detail = "Built setup constant " + target_value + " by " + action.detail + " (" +
                std::to_string(action.cost) + " cells instead of direct " +
                std::to_string(setup_number_entry_cost(target.value)) + ").",
  });
}

bool lower_setup_expression_to_x(MachineEmitter& setup, const Expression& expression,
                                 const std::vector<PreloadReport>& preloads);

std::optional<const PreloadReport*> setup_expression_negated_preload(
    const std::vector<PreloadReport>& preloads, const std::string& raw) {
  const std::optional<double> target = executable_setup_number(raw);
  if (!target.has_value() || !std::isfinite(*target))
    return std::nullopt;
  const std::string normalized_raw = normalize_setup_constant_text(raw);
  const std::string wanted = normalize_setup_constant_text(
      normalized_raw.rfind("-", 0) == 0 ? normalized_raw.substr(1) : "-" + normalized_raw);
  for (const PreloadReport& preload : preloads) {
    if (preload.setup_expression)
      continue;
    const std::optional<std::string> executable = executable_setup_value(preload.value);
    const std::string value = normalize_setup_constant_text(executable.value_or(preload.value));
    if (value == wanted)
      return &preload;
  }
  return std::nullopt;
}

std::optional<const PreloadReport*> setup_expression_number_preload(
    const std::vector<PreloadReport>& preloads, const std::string& raw) {
  const std::optional<double> target = executable_setup_number(raw);
  if (!target.has_value() || !std::isfinite(*target))
    return std::nullopt;
  const std::string wanted = normalize_setup_constant_text(raw);
  for (const PreloadReport& preload : preloads) {
    if (preload.setup_expression)
      continue;
    const std::optional<std::string> executable = executable_setup_value(preload.value);
    const std::string value = normalize_setup_constant_text(executable.value_or(preload.value));
    if (value == wanted)
      return &preload;
  }
  return std::nullopt;
}

bool emit_setup_number_or_negated_preload(MachineEmitter& setup,
                                          const std::vector<PreloadReport>& preloads,
                                          const std::string& raw) {
  if (setup_number_entry_cost(raw) >= 2) {
    const std::optional<const PreloadReport*> source =
        setup_expression_number_preload(preloads, raw);
    if (source.has_value()) {
      emit_setup_recall(setup, (*source)->register_name,
                        "preload const " + normalize_setup_constant_text(raw));
      return true;
    }
  }
  if (setup_number_entry_cost(raw) >= 2) {
    const std::optional<const PreloadReport*> source =
        setup_expression_negated_preload(preloads, raw);
    if (source.has_value()) {
      const std::string target_value = normalize_setup_constant_text(raw);
      const std::string source_value = normalize_setup_constant_text((*source)->value);
      emit_setup_recall(setup, (*source)->register_name,
                        "constant " + target_value + " base " + source_value);
      setup.emit_op(0x0b, "/-/", "constant " + target_value, std::nullopt, true);
      return true;
    }
  }
  emit_exact_setup_number(setup, raw);
  return false;
}

void emit_setup_number_or_preload_or_pow10(MachineEmitter& setup,
                                           const std::vector<PreloadReport>& preloads,
                                           const std::string& raw, std::string comment) {
  if (setup_number_entry_cost(raw) >= 2) {
    const std::optional<const PreloadReport*> source =
        setup_expression_number_preload(preloads, raw);
    if (source.has_value()) {
      emit_setup_recall(setup, (*source)->register_name,
                        "preload const " + normalize_setup_constant_text(raw));
      return;
    }
  }
  if (setup_number_entry_cost(raw) >= 2) {
    const std::optional<const PreloadReport*> source =
        setup_expression_negated_preload(preloads, raw);
    if (source.has_value()) {
      const std::string target_value = normalize_setup_constant_text(raw);
      const std::string source_value = normalize_setup_constant_text((*source)->value);
      emit_setup_recall(setup, (*source)->register_name,
                        "constant " + target_value + " base " + source_value);
      setup.emit_op(0x0b, "/-/", "constant " + target_value, std::nullopt, true);
      return;
    }
  }
  emit_setup_number_or_pow10(setup, raw, std::move(comment));
}

bool setup_expression_contains_valid_random(const Expression& expression) {
  if (expression.kind == "number" || expression.kind == "string" || expression.kind == "identifier")
    return false;
  if (expression.kind == "indexed")
    return expression.index != nullptr && setup_expression_contains_valid_random(*expression.index);
  if (expression.kind == "unary")
    return expression.expr != nullptr && setup_expression_contains_valid_random(*expression.expr);
  if (expression.kind == "binary") {
    return (expression.left != nullptr &&
            setup_expression_contains_valid_random(*expression.left)) ||
           (expression.right != nullptr &&
            setup_expression_contains_valid_random(*expression.right));
  }
  if (expression.kind == "call") {
    const std::string callee = lower_ascii(expression.callee);
    return (callee == "random" && expression.args.size() <= 2U) ||
           std::any_of(expression.args.begin(), expression.args.end(),
                       setup_expression_contains_valid_random);
  }
  return false;
}

bool lower_setup_random_call_to_x(MachineEmitter& setup, const Expression& expression,
                                  const std::vector<PreloadReport>& preloads) {
  if (expression.args.empty()) {
    setup.emit_op(0x3b, "К СЧ", "random()", std::nullopt, true);
    return true;
  }
  if (expression.args.size() == 1U) {
    setup.emit_op(0x3b, "К СЧ", "random()", std::nullopt, true);
    if (!lower_setup_expression_to_x(setup, expression.args.front(), preloads))
      return false;
    setup.emit_op(0x12, "×", "expr *", std::nullopt, true);
    return true;
  }
  if (expression.args.size() == 2U) {
    Expression range = subtract_expression(expression.args.at(1), expression.args.at(0));
    Expression scaled = multiply_expression(call_expression("random", {}), std::move(range));
    return lower_setup_expression_to_x(setup, add_expression(expression.args.at(0), std::move(scaled)),
                                       preloads);
  }
  return false;
}

bool lower_setup_expression_to_x(MachineEmitter& setup, const Expression& expression,
                                 const std::vector<PreloadReport>& preloads) {
  if (expression.kind == "number") {
    const std::string raw = expression.raw.empty() ? expression.text : expression.raw;
    emit_setup_number_or_preload_or_pow10(setup, preloads, raw,
                                          "constant " + normalize_setup_constant_text(raw));
    return true;
  }
  if (expression.kind == "identifier") {
    for (const PreloadReport& preload : preloads) {
      if (preload.setup_target_name.has_value() && *preload.setup_target_name == expression.name) {
        emit_setup_recall(setup, preload.register_name, "recall " + expression.name);
        return true;
      }
    }
    return false;
  }
  if (expression.kind == "unary" && expression.op == "-" && expression.expr != nullptr) {
    if (!lower_setup_expression_to_x(setup, *expression.expr, preloads))
      return false;
    setup.emit_op(0x0b, "/-/", "unary minus", std::nullopt, true);
    return true;
  }
  if (expression.kind == "binary" && expression.left != nullptr && expression.right != nullptr) {
    const std::optional<std::pair<int, std::string>> opcode = setup_binary_opcode(expression.op);
    if (!opcode.has_value())
      return false;
    if (!lower_setup_expression_to_x(setup, *expression.left, preloads) ||
        !lower_setup_expression_to_x(setup, *expression.right, preloads)) {
      return false;
    }
    setup.emit_op(opcode->first, opcode->second, "expr " + expression.op, std::nullopt, true);
    return true;
  }
  if (expression.kind == "call") {
    const std::string callee = lower_ascii(expression.callee);
    if (callee == "pi" && expression.args.empty()) {
      setup.emit_op(0x20, "F π", "pi()", std::nullopt, true);
      return true;
    }
    if (callee == "random")
      return lower_setup_random_call_to_x(setup, expression, preloads);
    if (callee == "int" && expression.args.size() == 1U &&
        setup_expression_contains_valid_random(expression.args.front())) {
      if (!lower_setup_expression_to_x(setup, expression.args.front(), preloads))
        return false;
      setup.emit_op(0x0e, "В↑", "random int keep scaled draw", std::nullopt, true);
      setup.emit_op(0x35, "К {x}", "random int fractional part", std::nullopt, true);
      setup.emit_op(0x11, "-", "random int floor", std::nullopt, true);
      return true;
    }
    if (const std::optional<std::pair<int, std::string>> opcode =
            setup_binary_call_opcode(callee)) {
      if (expression.args.size() != 2U ||
          !lower_setup_expression_to_x(setup, expression.args.at(0), preloads) ||
          !lower_setup_expression_to_x(setup, expression.args.at(1), preloads)) {
        return false;
      }
      setup.emit_op(opcode->first, opcode->second, callee + "()", std::nullopt, true);
      return true;
    }
    if (expression.args.size() != 1U)
      return false;
    const std::optional<std::pair<int, std::string>> opcode = setup_unary_opcode(callee);
    if (!opcode.has_value() ||
        !lower_setup_expression_to_x(setup, expression.args.front(), preloads))
      return false;
    setup.emit_op(opcode->first, opcode->second, callee + "()", std::nullopt, true);
    return true;
  }
  return false;
}

std::optional<std::string> random_domain_value(std::string_view value);

std::string add_setup_offset_expression(std::string expression, int offset) {
  if (offset == 0)
    return expression;
  if (offset > 0)
    return expression + " + " + std::to_string(offset);
  return expression + " - " + std::to_string(-offset);
}

std::string random_board_coordinate_expression(const V2Board& board) {
  if (board.height == 1)
    return add_setup_offset_expression("int(random(" + std::to_string(board.width) + "))",
                                       board.x_min);
  if (board.width == 1)
    return add_setup_offset_expression("int(random(" + std::to_string(board.height) + "))",
                                       board.y_min);
  const std::string x =
      add_setup_offset_expression("int(random(" + std::to_string(board.width) + "))",
                                  board.x_min);
  const std::string y =
      add_setup_offset_expression("int(random(" + std::to_string(board.height) + "))",
                                  board.y_min);
  return x + " + 10 * (" + y + ")";
}

struct IndexedSetupPreloadGroup {
  std::size_t end = 0;
  int min_register = 0;
  int max_register = 0;
  std::vector<std::string> targets;
};

std::optional<IndexedSetupPreloadGroup>
indexed_setup_preload_group_at(const std::vector<PreloadReport>& preloads,
                               const std::set<std::size_t>& consumed, std::size_t index) {
  if (index >= preloads.size() || consumed.contains(index))
    return std::nullopt;
  const PreloadReport& first = preloads.at(index);
  if (!first.setup_target_name.has_value() || is_stack_preload_value(first.value) ||
      has_executable_setup_number_value(first.value))
    return std::nullopt;

  IndexedSetupPreloadGroup group;
  group.end = index;
  group.min_register = register_index(first.register_name);
  group.max_register = group.min_register;
  group.targets.push_back(*first.setup_target_name);

  std::size_t cursor = index + 1U;
  while (cursor < preloads.size() && !consumed.contains(cursor)) {
    const PreloadReport& candidate = preloads.at(cursor);
    if (!candidate.setup_target_name.has_value() || is_stack_preload_value(candidate.value) ||
        has_executable_setup_number_value(candidate.value) ||
        candidate.value != first.value) {
      break;
    }
    const int candidate_register = register_index(candidate.register_name);
    if (candidate_register != group.max_register + 1)
      break;
    group.max_register = candidate_register;
    group.targets.push_back(*candidate.setup_target_name);
    ++cursor;
  }
  group.end = cursor;
  if (group.targets.size() < 3U || group.min_register < 1 || group.max_register > 14)
    return std::nullopt;
  return group;
}

bool emit_indexed_setup_preload_group(MachineEmitter& setup,
                                      std::vector<OptimizationReport>& optimizations,
                                      const std::map<std::string, const V2Board*>& boards,
                                      const std::vector<PreloadReport>& preloads,
                                      const PreloadReport& first,
                                      const IndexedSetupPreloadGroup& group) {
  const std::string& first_name = group.targets.front();
  const std::string& last_name = group.targets.back();
  const std::string label = setup.fresh_label("setup_indexed_bank");
  setup.emit_number(std::to_string(group.max_register + 1));
  emit_setup_store(setup, "0", "setup indexed pointer " + first_name + ".." + last_name);
  setup.emit_label(label, {.hidden = true});
  std::string expression_text = first.value;
  if (const std::optional<std::string> random_domain = random_domain_value(first.value)) {
    const auto board_it = boards.find(*random_domain);
    if (board_it != boards.end() && board_it->second != nullptr)
      expression_text = random_board_coordinate_expression(*board_it->second);
  }
  const Expression expression = parse_expression(expression_text, first.setup_source_line.value_or(0));
  if (!lower_setup_expression_to_x(setup, expression, preloads))
    return false;
  setup.emit_op(0xb0, "К X->П 0", "setup indexed " + first_name + ".." + last_name,
                first.setup_source_line, true);
  emit_setup_recall(setup, "0", "setup indexed pointer");
  setup.emit_number(std::to_string(group.min_register));
  setup.emit_op(0x11, "-", "setup indexed remaining", first.setup_source_line, true);
  setup.emit_jump(0x5e, "F x=0", label, "setup indexed loop " + first_name + ".." + last_name,
                  first.setup_source_line);
  optimizations.push_back(OptimizationReport{
      .name = "indexed-bank-loop",
      .detail = "Initialized " + std::to_string(group.targets.size()) + " indexed bank fields (" +
                first_name + ".." + last_name + ") with one indirect setup loop.",
  });
  return true;
}

bool emit_restore_setup_pointer_r0(MachineEmitter& setup,
                                   const std::vector<PreloadReport>& preloads,
                                   const std::optional<std::string>& current_value = std::nullopt,
                                   bool force_restore = true) {
  const auto preload =
      std::find_if(preloads.begin(), preloads.end(), [](const PreloadReport& item) {
        return item.register_name == "0" && !item.setup_expression &&
               has_executable_setup_number_value(item.value);
      });
  if (preload == preloads.end())
    return false;
  const std::string value = executable_setup_value(preload->value).value_or(preload->value);
  if (!force_restore && current_value.has_value() &&
      normalize_setup_constant_text(value) == normalize_setup_constant_text(*current_value)) {
    return true;
  }
  emit_exact_setup_number(setup, preload->value);
  emit_setup_store(setup, "0", "restore setup R0");
  return true;
}

void emit_segmented_bitplane_random_candidate_setup(MachineEmitter& setup,
                                                    const std::string& seed_register,
                                                    const std::string& index_register) {
  emit_setup_recall(setup, seed_register, "segmented bitplane random seed");
  setup.emit_number("37");
  setup.emit_op(0x12, "*", "segmented bitplane next random seed", std::nullopt, true);
  setup.emit_op(0x35, "К {x}", "segmented bitplane random seed fraction", std::nullopt, true);
  emit_setup_store(setup, seed_register, "segmented bitplane random seed");
  emit_setup_number_or_pow10(setup, "100", "constant 100");
  setup.emit_op(0x12, "*", "segmented bitplane random scaled seed", std::nullopt, true);
  setup.emit_op(0x34, "К [x]", "segmented bitplane random flat index", std::nullopt, true);
  emit_setup_store(setup, index_register, "segmented bitplane random candidate");
}

void emit_segmented_bitplane_bit_mask_from_index_setup(MachineEmitter& setup,
                                                       const std::string& index_register) {
  setup.emit_number("4");
  setup.emit_op(0x13, "/", "bit mask quotient", std::nullopt, true);
  emit_setup_store(setup, index_register, "bit mask quotient");
  setup.emit_op(0x35, "К {x}", "bit mask remainder fraction", std::nullopt, true);
  setup.emit_number("4");
  setup.emit_op(0x12, "*", "bit mask remainder scale", std::nullopt, true);
  setup.emit_number("2");
  setup.emit_op(0x24, "F x^y", "bit mask power", std::nullopt, true);
  setup.emit_number("0.5");
  setup.emit_op(0x10, "+", "bit mask round bias", std::nullopt, true);
  setup.emit_op(0x34, "К [x]", "bit mask round", std::nullopt, true);
  emit_setup_recall(setup, index_register, "bit mask quotient");
  setup.emit_op(0x34, "К [x]", "bit mask digit index", std::nullopt, true);
  setup.emit_number("1");
  setup.emit_op(0x10, "+", "bit mask decade index", std::nullopt, true);
  setup.emit_op(0x15, "F 10^x", "bit mask decade", std::nullopt, true);
  setup.emit_op(0x13, "/", "bit mask fractional place", std::nullopt, true);
  setup.emit_number("8");
  setup.emit_op(0x10, "+", "bit mask anchor", std::nullopt, true);
}

bool emit_segmented_bitplane_indirect_select_setup(MachineEmitter& setup,
                                                   const std::string& index_register,
                                                   const std::string& selector_register) {
  const int selector_index = register_index(selector_register);
  if (selector_index < 7)
    return false;

  static constexpr std::string_view kSelectorValues[] = {"0", "1", "11", "14"};
  std::vector<std::string> labels;
  labels.reserve(4);
  for (int group = 0; group < 4; ++group)
    labels.push_back(setup.fresh_label("seg_bitplane_select_" + std::to_string(group)));
  const std::string selected = setup.fresh_label("seg_bitplane_selected");

  for (int group = 0; group < 3; ++group) {
    emit_setup_recall(setup, index_register, "segmented bitplane index");
    setup.emit_number(std::to_string((group + 1) * 25));
    setup.emit_op(0x11, "-", "segmented bitplane threshold", std::nullopt, true);
    setup.emit_jump(0x5c, "F x<0", labels.at(static_cast<std::size_t>(group)),
                    "segmented bitplane indirect select", std::nullopt);
  }
  setup.emit_jump(0x51, "БП", labels.at(3), "segmented bitplane indirect select", std::nullopt);

  for (int group = 0; group < 4; ++group) {
    setup.emit_label(labels.at(static_cast<std::size_t>(group)), {.hidden = true});
    emit_setup_recall(setup, index_register, "segmented bitplane index");
    if (group > 0) {
      setup.emit_number(std::to_string(group * 25));
      setup.emit_op(0x11, "-", "segmented bitplane local index", std::nullopt, true);
    }
    emit_setup_store(setup, index_register, "segmented bitplane local index");
    setup.emit_number(std::string(kSelectorValues[static_cast<std::size_t>(group)]));
    emit_setup_store(setup, selector_register, "segmented bitplane selector");
    if (group < 3)
      setup.emit_jump(0x51, "БП", selected, "segmented bitplane selected", std::nullopt);
  }
  setup.emit_label(selected, {.hidden = true});
  return true;
}

void emit_random_board_coordinate_setup(MachineEmitter& setup, const V2Board& board,
                                        const std::vector<PreloadReport>& preloads,
                                        const std::string& register_name,
                                        const std::string& target_name) {
  if (board.height == 1) {
    setup.emit_op(0x3b, "К СЧ", "random()", std::nullopt, true);
    emit_setup_number_or_preload_or_pow10(setup, preloads, std::to_string(board.width),
                                          "constant " + std::to_string(board.width));
    setup.emit_op(0x12, "*", "expr *", std::nullopt, true);
    setup.emit_op(0x0e, "В↑", "random int keep scaled draw", std::nullopt, true);
    setup.emit_op(0x35, "К {x}", "random int fractional part", std::nullopt, true);
    setup.emit_op(0x11, "-", "random int floor", std::nullopt, true);
    emit_setup_integer_offset(setup, board.x_min);
    emit_setup_store(setup, register_name, "setup " + target_name);
    return;
  }
  if (board.width == 1) {
    setup.emit_op(0x3b, "К СЧ", "random()", std::nullopt, true);
    emit_setup_number_or_preload_or_pow10(setup, preloads, std::to_string(board.height),
                                          "constant " + std::to_string(board.height));
    setup.emit_op(0x12, "*", "expr *", std::nullopt, true);
    setup.emit_op(0x0e, "В↑", "random int keep scaled draw", std::nullopt, true);
    setup.emit_op(0x35, "К {x}", "random int fractional part", std::nullopt, true);
    setup.emit_op(0x11, "-", "random int floor", std::nullopt, true);
    emit_setup_integer_offset(setup, board.y_min);
    emit_setup_store(setup, register_name, "setup " + target_name);
    return;
  }

  setup.emit_op(0x3b, "К СЧ", "random()", std::nullopt, true);
  emit_setup_number_or_preload_or_pow10(setup, preloads, std::to_string(board.width),
                                        "constant " + std::to_string(board.width));
  setup.emit_op(0x12, "*", "expr *", std::nullopt, true);
  setup.emit_op(0x0e, "В↑", "random int keep scaled draw", std::nullopt, true);
  setup.emit_op(0x35, "К {x}", "random int fractional part", std::nullopt, true);
  setup.emit_op(0x11, "-", "random int floor", std::nullopt, true);
  emit_setup_integer_offset(setup, board.x_min);
  emit_setup_store(setup, register_name, "random coord x");
  setup.emit_op(0x3b, "К СЧ", "random()", std::nullopt, true);
  emit_setup_number_or_preload_or_pow10(setup, preloads, std::to_string(board.height),
                                        "constant " + std::to_string(board.height));
  setup.emit_op(0x12, "*", "expr *", std::nullopt, true);
  setup.emit_op(0x0e, "В↑", "random int keep scaled draw", std::nullopt, true);
  setup.emit_op(0x35, "К {x}", "random int fractional part", std::nullopt, true);
  setup.emit_op(0x11, "-", "random int floor", std::nullopt, true);
  emit_setup_integer_offset(setup, board.y_min);
  setup.emit_number("10");
  setup.emit_op(0x12, "*", "random coord y decade", std::nullopt, true);
  emit_setup_recall(setup, register_name, "random coord x");
  setup.emit_op(0x10, "+", "random coord", std::nullopt, true);
  emit_setup_store(setup, register_name, "setup " + target_name);
}

int board_cell_count(const V2Board& board) {
  return board.width * board.height;
}

bool is_zero_origin_ten_by_ten_board(const V2Board& board) {
  return board.x_min == 0 && board.x_max == 9 && board.y_min == 0 && board.y_max == 9 &&
         board.width == 10 && board.height == 10;
}

bool board_random_unique_candidate_supported(const V2Board& board) {
  return board.height == 1 || board.width == 1 ||
         is_zero_origin_ten_by_ten_board(board);
}

bool is_preincrement_indirect_register(int index) {
  return index == 4 || index == 5 || index == 6;
}

std::optional<std::pair<int, std::string>> fl_loop_opcode_for_register(int index) {
  switch (index) {
  case 0:
    return std::pair{0x5d, "F L0"};
  case 1:
    return std::pair{0x5b, "F L1"};
  case 2:
    return std::pair{0x58, "F L2"};
  case 3:
    return std::pair{0x5a, "F L3"};
  default:
    return std::nullopt;
  }
}

bool emit_random_unique_candidate_setup(MachineEmitter& setup,
                                        const std::vector<PreloadReport>& preloads,
                                        const V2Board& board,
                                        const std::string& seed_register,
                                        const std::string& candidate_register,
                                        const std::optional<std::string>& row_register,
                                        bool scaled_decimal) {
  emit_setup_recall(setup, seed_register, "random coord seed");
  setup.emit_number("37");
  setup.emit_op(0x12, "*", "random coord next seed", std::nullopt, true);
  setup.emit_op(0x35, "К {x}", "random coord seed fraction", std::nullopt, true);
  emit_setup_store(setup, seed_register, "random coord seed");
  const std::string cell_count = std::to_string(board_cell_count(board));
  emit_setup_number_or_preload_or_pow10(setup, preloads, cell_count, "constant " + cell_count);
  setup.emit_op(0x12, "*", "random coord scaled seed", std::nullopt, true);
  setup.emit_op(0x34, "К [x]", "random coord flat index", std::nullopt, true);
  const bool zero_origin_ten_by_ten = is_zero_origin_ten_by_ten_board(board);
  if (scaled_decimal && zero_origin_ten_by_ten) {
    emit_setup_number_or_preload_or_pow10(setup, preloads, "10", "constant 10");
    setup.emit_op(0x13, "/", "random coord scaled decimal cell", std::nullopt, true);
    emit_setup_store(setup, candidate_register, "random coord scaled decimal cell");
    return true;
  }

  emit_setup_store(setup, candidate_register, "random coord flat index");
  if (zero_origin_ten_by_ten)
    return true;
  if (!row_register.has_value())
    return false;

  emit_setup_recall(setup, candidate_register, "recall __coord_list_current");
  emit_setup_number_or_preload_or_pow10(setup, preloads, std::to_string(board.width),
                                        "constant " + std::to_string(board.width));
  setup.emit_op(0x13, "/", "expr /", std::nullopt, true);
  setup.emit_op(0x34, "К [x]", "int()", std::nullopt, true);
  emit_setup_store(setup, *row_register, "random coord row");

  emit_setup_recall(setup, candidate_register, "recall __coord_list_current");
  emit_setup_number_or_preload_or_pow10(setup, preloads, std::to_string(board.width),
                                        "constant " + std::to_string(board.width));
  emit_setup_recall(setup, *row_register, "recall __coord_list_dx");
  setup.emit_op(0x12, "*", "expr *", std::nullopt, true);
  setup.emit_op(0x11, "-", "expr -", std::nullopt, true);
  if (board.x_min != 0) {
    emit_setup_number_or_preload_or_pow10(setup, preloads, std::to_string(board.x_min),
                                          "constant " + std::to_string(board.x_min));
    setup.emit_op(0x10, "+", "expr +", std::nullopt, true);
  }
  emit_setup_number_or_preload_or_pow10(setup, preloads, "10", "constant 10");
  if (board.y_min != 0) {
    emit_setup_number_or_preload_or_pow10(setup, preloads, std::to_string(board.y_min),
                                          "constant " + std::to_string(board.y_min));
    emit_setup_recall(setup, *row_register, "recall __coord_list_dx");
    setup.emit_op(0x10, "+", "expr +", std::nullopt, true);
  } else {
    emit_setup_recall(setup, *row_register, "recall __coord_list_dx");
  }
  setup.emit_op(0x12, "*", "expr *", std::nullopt, true);
  setup.emit_op(0x10, "+", "expr +", std::nullopt, true);
  emit_setup_store(setup, candidate_register, "random coord candidate");
  return true;
}

bool emit_compact_random_unique_coord_list_setup(
    MachineEmitter& setup, const std::map<std::string, const V2Board*>& boards,
    const std::map<std::string, std::string>& registers,
    const std::vector<PreloadReport>& preloads, const RandomUniqueCoordListValue& value,
    const std::vector<std::string>& item_registers) {
  if (item_registers.empty())
    return false;
  const auto pointer_it = registers.find(std::string(k_coord_list_pointer));
  const auto counter_it = registers.find(std::string(k_coord_list_counter));
  const auto current_it = registers.find(std::string(k_coord_list_current));
  const auto previous_it = registers.find(std::string(k_coord_list_dx));
  if (pointer_it == registers.end() || counter_it == registers.end() ||
      current_it == registers.end() || previous_it == registers.end()) {
    return false;
  }

  const auto board_it = boards.find(value.domain);
  if (board_it == boards.end())
    return false;
  const V2Board& board = *board_it->second;
  const int pointer_index = register_index(pointer_it->second);
  const int counter_index = register_index(counter_it->second);
  const int previous_index = register_index(previous_it->second);
  const int current_index = register_index(current_it->second);
  if (!is_preincrement_indirect_register(pointer_index))
    return false;
  const std::optional<std::pair<int, std::string>> outer_opcode =
      fl_loop_opcode_for_register(counter_index);
  const std::optional<std::pair<int, std::string>> previous_opcode =
      fl_loop_opcode_for_register(previous_index);
  if (!outer_opcode.has_value() || !previous_opcode.has_value())
    return false;

  std::vector<int> item_indices;
  item_indices.reserve(item_registers.size());
  for (const std::string& item_register : item_registers)
    item_indices.push_back(register_index(item_register));
  if (item_indices.front() <= 0)
    return false;
  for (std::size_t index = 1; index < item_indices.size(); ++index) {
    if (item_indices.at(index) != item_indices.front() + static_cast<int>(index))
      return false;
  }
  const std::set<int> scratch_indices = {pointer_index, counter_index, previous_index,
                                         current_index};
  for (const int item_index : item_indices) {
    if (scratch_indices.contains(item_index))
      return false;
  }

  const std::string seed_register = item_registers.back();
  const std::string draw_label = setup.fresh_label("random_coord_draw");
  const std::string check_label = setup.fresh_label("random_coord_check");
  const std::string store_label = setup.fresh_label("random_coord_store");

  setup.emit_op(0x3b, "К СЧ", "random coord seed", std::nullopt, true);
  emit_setup_store(setup, seed_register, "random coord seed");
  setup.emit_number(std::to_string(value.count));
  emit_setup_store(setup, counter_it->second, "random coord remaining");

  setup.emit_label(draw_label, {.hidden = true});
  if (!emit_random_unique_candidate_setup(setup, preloads, board, seed_register,
                                          current_it->second, previous_it->second,
                                          value.scaled_decimal)) {
    return false;
  }

  setup.emit_number(std::to_string(value.count));
  emit_setup_recall(setup, counter_it->second, "random coord remaining");
  setup.emit_op(0x11, "-", "random coord previous count", std::nullopt, true);
  emit_setup_store(setup, previous_it->second, "random coord previous count");
  setup.emit_number(std::to_string(item_indices.front() - 1));
  emit_setup_store(setup, pointer_it->second, "random coord pointer");
  emit_setup_recall(setup, previous_it->second, "random coord previous count");
  setup.emit_jump(0x57, "F x!=0", store_label, "random coord first item");

  setup.emit_label(check_label, {.hidden = true});
  emit_setup_recall(setup, current_it->second, "random coord candidate");
  setup.emit_op(0xd0 + pointer_index, "К П->X " + pointer_it->second, "random coord previous",
                std::nullopt, true);
  setup.emit_op(0x11, "-", "random coord uniqueness", std::nullopt, true);
  setup.emit_jump(0x57, "F x!=0", draw_label, "random coord collision");
  setup.emit_jump(previous_opcode->first, previous_opcode->second, check_label,
                  "random coord previous loop");

  setup.emit_label(store_label, {.hidden = true});
  emit_setup_recall(setup, current_it->second, "random coord candidate");
  setup.emit_op(0xb0 + pointer_index, "К X->П " + pointer_it->second, "random coord store",
                std::nullopt, true);
  setup.emit_jump(outer_opcode->first, outer_opcode->second, draw_label, "random coord outer loop");
  return true;
}

bool emit_random_unique_coord_list_setup(MachineEmitter& setup,
                                         const std::map<std::string, const V2Board*>& boards,
                                         const std::map<std::string, std::string>& registers,
                                         const std::vector<PreloadReport>& preloads,
                                         const RandomUniqueCoordListValue& value,
                                         const std::vector<std::string>& item_registers,
                                         std::vector<OptimizationReport>& optimizations) {
  const auto board_it = boards.find(value.domain);
  if (board_it == boards.end())
    return false;
  const V2Board& board = *board_it->second;
  if (!board_random_unique_candidate_supported(board))
    return false;

  if (emit_compact_random_unique_coord_list_setup(setup, boards, registers, preloads, value,
                                                  item_registers)) {
    optimizations.push_back(OptimizationReport{
        .name = "coord-list-indirect-random-unique",
        .detail = "Generated compact indirect setup for " + std::to_string(item_registers.size()) +
                  " unique coord_list item(s).",
    });
    return true;
  }

  const std::string seed_register = item_registers.back();
  const auto row_it = registers.find(std::string(k_coord_list_dx));
  const std::optional<std::string> row_register =
      row_it == registers.end() ? std::nullopt : std::optional<std::string>{row_it->second};
  setup.emit_op(0x3b, "К СЧ", "random coord seed", std::nullopt, true);
  emit_setup_store(setup, seed_register, "random coord seed");

  for (std::size_t index = 0; index < item_registers.size(); ++index) {
    const std::string draw_label = setup.fresh_label("random_coord_draw");
    setup.emit_label(draw_label, {.hidden = true});
    if (!emit_random_unique_candidate_setup(setup, preloads, board, seed_register,
                                            item_registers.at(index), row_register,
                                            value.scaled_decimal)) {
      return false;
    }
    for (std::size_t previous = 0; previous < index; ++previous) {
      emit_setup_recall(setup, item_registers.at(index), "random coord candidate");
      emit_setup_recall(setup, item_registers.at(previous), "random coord previous");
      setup.emit_op(0x11, "-", "random coord uniqueness", std::nullopt, true);
      setup.emit_jump(0x57, "F x!=0", draw_label, "random coord collision");
    }
    emit_setup_recall(setup, item_registers.at(index), "random coord candidate");
    emit_setup_store(setup, item_registers.at(index), "random coord store");
  }
  return true;
}

bool emit_random_unique_segmented_bitplane_setup(
    MachineEmitter& setup, const std::map<std::string, std::string>& registers,
    const RandomUniqueSegmentedBitplaneValue& value,
    const std::vector<std::string>& plane_registers,
    std::vector<OptimizationReport>& optimizations) {
  const auto index_it = registers.find(std::string(k_segmented_bitplane_index));
  const auto selector_it = registers.find(std::string(k_segmented_bitplane_selector));
  const auto counter_it = registers.find(spatial_count_line_scratch_name());
  const auto seed_it = registers.find(spatial_count_offset_scratch_name());
  const auto count_it = registers.find(value.count_source);
  if (plane_registers.size() != 4 || index_it == registers.end() ||
      selector_it == registers.end() || counter_it == registers.end() ||
      seed_it == registers.end() || count_it == registers.end()) {
    return false;
  }

  setup.emit_number("0");
  for (std::size_t index = 0; index < plane_registers.size(); ++index) {
    emit_setup_store(setup, plane_registers.at(index),
                     "setup " + segmented_bitplane_name(value.collection, static_cast<int>(index)));
  }

  setup.emit_op(0x3b, "К СЧ", "segmented bitplane random seed", std::nullopt, true);
  emit_setup_store(setup, seed_it->second, "segmented bitplane random seed");
  emit_setup_recall(setup, count_it->second, "segmented bitplane random count");
  emit_setup_store(setup, counter_it->second, "segmented bitplane remaining placements");

  const std::string draw = setup.fresh_label("seg_bitplane_random_draw");
  setup.emit_label(draw, {.hidden = true});
  emit_segmented_bitplane_random_candidate_setup(setup, seed_it->second, index_it->second);

  if (!emit_segmented_bitplane_indirect_select_setup(setup, index_it->second,
                                                     selector_it->second)) {
    return false;
  }

  const int selector_index = register_index(selector_it->second);
  emit_setup_recall(setup, index_it->second, "segmented bitplane random local index");
  emit_segmented_bitplane_bit_mask_from_index_setup(setup, index_it->second);
  emit_setup_store(setup, index_it->second, "segmented bitplane random mask");
  setup.emit_op(0xd0 + selector_index, "К П->X " + selector_it->second,
                "segmented bitplane random selected plane", std::nullopt, true);
  emit_setup_recall(setup, index_it->second, "segmented bitplane random mask");
  setup.emit_op(0x37, "К ∧", "segmented bitplane random collision probe", std::nullopt, true);
  setup.emit_op(0x35, "К {x}", "segmented bitplane random collision fraction", std::nullopt, true);
  setup.emit_jump(0x5e, "F x=0", draw, "segmented bitplane random collision", std::nullopt);

  setup.emit_op(0xd0 + selector_index, "К П->X " + selector_it->second,
                "segmented bitplane random selected plane", std::nullopt, true);
  emit_setup_recall(setup, index_it->second, "segmented bitplane random mask");
  setup.emit_op(0x38, "К ∨", "segmented bitplane random set", std::nullopt, true);
  setup.emit_op(0xb0 + selector_index, "К X->П " + selector_it->second,
                "segmented bitplane random store selected plane", std::nullopt, true);

  emit_setup_recall(setup, counter_it->second, "segmented bitplane remaining placements");
  setup.emit_number("1");
  setup.emit_op(0x11, "-", "segmented bitplane decrement remaining placements", std::nullopt, true);
  emit_setup_store(setup, counter_it->second, "segmented bitplane remaining placements");
  setup.emit_jump(0x5e, "F x=0", draw, "segmented bitplane random next placement", std::nullopt);

  emit_setup_recall(setup, count_it->second, "segmented bitplane setup complete count");
  setup.emit_op(0x0b, "/-/", "segmented bitplane setup complete display", std::nullopt, true);
  optimizations.push_back(OptimizationReport{
      .name = "segmented-bitplane-random-unique",
      .detail = "Generated unique random setup for " + value.collection +
                " through four 25-cell bitplanes.",
  });
  return true;
}

std::optional<std::string> random_domain_value(std::string_view value) {
  static const std::regex random_domain_regex(R"(^random\(([A-Za-z_][A-Za-z0-9_]*)\)$)");
  std::cmatch match;
  if (!std::regex_match(value.data(), value.data() + value.size(), match, random_domain_regex))
    return std::nullopt;
  return match[1].str();
}

} // namespace

std::string random_unique_coord_list_value(const std::string& domain, const std::string& list_name,
                                           int index, int count, bool scaled_decimal) {
  return "random_unique(" + domain + "," + list_name + "," + std::to_string(index) + "," +
         std::to_string(count) + (scaled_decimal ? ",scaled" : "") + ")";
}

std::string segmented_bitplane_random_unique_value(const std::string& collection,
                                                   const std::string& count_source,
                                                   int plane_index) {
  return "__seg_bitplane_random_unique(" + collection + "," + count_source + "," +
         std::to_string(plane_index) + ")";
}

SetupProgramReport
compile_setup_program_with_preloads(const std::map<std::string, const V2Board*>& boards,
                                    const std::map<std::string, std::string>& registers,
                                    const std::vector<PreloadReport>& preloads,
                                    const CompileOptions& options,
                                    std::optional<std::string> expected_mode) {
  MachineEmitter setup;
  setup.address_space_model =
      address_space_model_for_feature_profile(effective_optimizer_feature_profile(options));
  std::vector<OptimizationReport> optimizations;
  std::set<std::size_t> consumed;
  bool setup_r0_dirty = false;
  std::optional<std::string> setup_r0_current_value;
  bool initialized_random_coord_list = false;
  bool initialized_segmented_bitplanes = false;
  const bool r0_needs_non_numeric_restore =
      std::any_of(preloads.begin(), preloads.end(), [](const PreloadReport& preload) {
        return preload.register_name == "0" &&
               (preload.setup_expression || !has_executable_setup_number_value(preload.value));
      });
  bool uses_r0_setup_pointer = false;
  if (!r0_needs_non_numeric_restore) {
    const std::set<std::size_t> empty_consumed;
    for (std::size_t index = 0; index < preloads.size(); ++index) {
      if (indexed_setup_preload_group_at(preloads, empty_consumed, index).has_value()) {
        uses_r0_setup_pointer = true;
        break;
      }
    }
  }
  auto call_like_setup_value = [](const PreloadReport& preload) {
    return preload.value.find('(') != std::string::npos ||
           preload.value.find(')') != std::string::npos;
  };
  auto executable_setup_preload = [&](const PreloadReport& preload) {
    return !preload.setup_expression && !is_stack_preload_value(preload.value) &&
           !call_like_setup_value(preload);
  };
  auto deferred_direct_setup_preload = [&](const PreloadReport& preload) {
    const bool call_like = preload.value.find('(') != std::string::npos ||
                           preload.value.find(')') != std::string::npos;
    return preload.setup_expression && !is_stack_preload_value(preload.value) &&
           !call_like;
  };
  std::vector<std::size_t> preload_order;
  preload_order.reserve(preloads.size());
  std::set<std::size_t> ordered_preload_indices;
  auto push_preload_order = [&](std::size_t index) {
    if (ordered_preload_indices.insert(index).second)
      preload_order.push_back(index);
  };
  const std::set<std::size_t> empty_consumed;
  auto is_indexed_setup_group_start = [&](const std::size_t index) {
    return indexed_setup_preload_group_at(preloads, empty_consumed, index).has_value();
  };
  auto is_stack_setup_preload = [&](const PreloadReport& preload, const std::string& stack_value) {
    return !executable_setup_preload(preload) && !deferred_direct_setup_preload(preload) &&
           preload.value == stack_value;
  };
  const bool has_stack_preload =
      std::any_of(preloads.begin(), preloads.end(), [](const PreloadReport& preload) {
        return is_stack_preload_value(preload.value);
      });
  auto push_stack_setup_preloads = [&](const std::string& stack_value) {
    for (std::size_t index = 0; index < preloads.size(); ++index) {
      if (is_stack_setup_preload(preloads.at(index), stack_value))
        push_preload_order(index);
    }
  };
  if (expected_mode.has_value() || has_stack_preload) {
    push_stack_setup_preloads("stack.Y");
    push_stack_setup_preloads("stack.Z");
    push_stack_setup_preloads("stack.T");
    push_stack_setup_preloads("stack.X");
    push_stack_setup_preloads("stack.X2");
  }
  for (std::size_t index = 0; index < preloads.size(); ++index) {
    if (executable_setup_preload(preloads.at(index)) &&
        !is_formatted_coord_report_mask_preload(preloads.at(index))) {
      push_preload_order(index);
    }
  }
  for (std::size_t index = 0; index < preloads.size(); ++index) {
    if (deferred_direct_setup_preload(preloads.at(index)))
      push_preload_order(index);
  }
  for (std::size_t index = 0; index < preloads.size(); ++index) {
    if (is_stack_setup_preload(preloads.at(index), "stack.Y"))
      push_preload_order(index);
  }
  for (std::size_t index = 0; index < preloads.size(); ++index) {
    if (is_stack_setup_preload(preloads.at(index), "stack.Z"))
      push_preload_order(index);
  }
  for (std::size_t index = 0; index < preloads.size(); ++index) {
    if (is_stack_setup_preload(preloads.at(index), "stack.T"))
      push_preload_order(index);
  }
  for (std::size_t index = 0; index < preloads.size(); ++index) {
    if (is_stack_setup_preload(preloads.at(index), "stack.X"))
      push_preload_order(index);
  }
  for (std::size_t index = 0; index < preloads.size(); ++index) {
    if (is_stack_setup_preload(preloads.at(index), "stack.X2"))
      push_preload_order(index);
  }
  for (std::size_t index = 0; index < preloads.size(); ++index) {
    if (!executable_setup_preload(preloads.at(index)) &&
        !deferred_direct_setup_preload(preloads.at(index)) &&
        !is_stack_preload_value(preloads.at(index).value) &&
        is_indexed_setup_group_start(index))
      push_preload_order(index);
  }
  for (std::size_t index = 0; index < preloads.size(); ++index) {
    if (!executable_setup_preload(preloads.at(index)) &&
        !deferred_direct_setup_preload(preloads.at(index)) &&
        !is_stack_preload_value(preloads.at(index).value) &&
        !is_indexed_setup_group_start(index))
      push_preload_order(index);
  }
  for (std::size_t index = 0; index < preloads.size(); ++index) {
    if (executable_setup_preload(preloads.at(index)) &&
        is_formatted_coord_report_mask_preload(preloads.at(index))) {
      push_preload_order(index);
    }
  }

  std::vector<std::size_t> numeric_segment;
  auto flush_numeric_segment = [&]() {
    if (numeric_segment.empty())
      return;
    std::vector<NumericSetupPreload> numeric_preloads;
    numeric_preloads.reserve(numeric_segment.size());
    for (const std::size_t numeric_index : numeric_segment) {
      const PreloadReport& preload = preloads.at(numeric_index);
      numeric_preloads.push_back(NumericSetupPreload{
          .preload_index = numeric_index,
          .register_name = preload.register_name,
          .value = executable_setup_value(preload.value).value_or(preload.value),
      });
    }
    for (const SetupNumericPreloadAction& action :
         setup_numeric_preload_actions(numeric_preloads)) {
      emit_setup_numeric_preload_action(setup, optimizations, numeric_preloads, action);
      const std::vector<int> targets = setup_numeric_action_target_indexes(action);
      std::vector<std::string> stored_registers;
      std::set<std::string> seen_registers;
      for (const int target_index : targets) {
        const NumericSetupPreload& preload =
            numeric_preloads.at(static_cast<std::size_t>(target_index));
        consumed.insert(preload.preload_index);
        if (seen_registers.contains(preload.register_name))
          continue;
        seen_registers.insert(preload.register_name);
        stored_registers.push_back(preload.register_name);
        emit_setup_store(setup, preload.register_name,
                         setup_store_comment(preloads.at(preload.preload_index)));
      }
      if (stored_registers.size() > 1U || stored_registers.size() < targets.size()) {
        report_duplicate_preload_store_reuse(
            optimizations,
            normalize_setup_constant_text(
                numeric_preloads.at(static_cast<std::size_t>(action.target_index)).value),
            stored_registers, targets.size(),
            numeric_preloads.at(static_cast<std::size_t>(action.target_index)).register_name);
      }
    }
    numeric_segment.clear();
  };

  bool expected_mode_guard_emitted = false;
  for (const std::size_t index : preload_order) {
    const PreloadReport& preload = preloads.at(index);
    const bool from_stack_x = preload.value == "stack.X";
    const bool from_stack_y = preload.value == "stack.Y";
    const bool from_stack_z = preload.value == "stack.Z";
    const bool from_stack_t = preload.value == "stack.T";
    const bool from_stack_x2 = preload.value == "stack.X2";
    const bool from_stack = from_stack_x || from_stack_y || from_stack_z || from_stack_t ||
                            from_stack_x2;
    if (expected_mode.has_value() && !expected_mode_guard_emitted && !from_stack) {
      flush_numeric_segment();
      emit_expected_mode_setup_check(setup, optimizations, *expected_mode);
      expected_mode_guard_emitted = true;
    }
    if (consumed.contains(index))
      continue;
    if (uses_r0_setup_pointer && preload.register_name == "0" && !preload.setup_expression &&
        has_executable_setup_number_value(preload.value)) {
      consumed.insert(index);
      continue;
    }
    if (executable_setup_preload(preload) && has_executable_setup_number_value(preload.value)) {
      numeric_segment.push_back(index);
      continue;
    }
    flush_numeric_segment();
    if (!r0_needs_non_numeric_restore) {
      const std::optional<IndexedSetupPreloadGroup> group =
          indexed_setup_preload_group_at(preloads, consumed, index);
      if (group.has_value()) {
        if (emit_indexed_setup_preload_group(setup, optimizations, boards, preloads, preload,
                                             *group)) {
          for (std::size_t consumed_index = index; consumed_index < group->end; ++consumed_index)
            consumed.insert(consumed_index);
          setup_r0_dirty = true;
          setup_r0_current_value = std::to_string(group->min_register - 1);
          continue;
        }
      }
    }
    if (setup_r0_dirty && uses_r0_setup_pointer) {
      if (emit_restore_setup_pointer_r0(setup, preloads, setup_r0_current_value,
                                        !options.disable_candidate_search))
        setup_r0_dirty = false;
    }
    if (preload.setup_expression) {
      consumed.insert(index);
      const Expression expression =
          parse_expression(preload.setup_expression_text.value_or(preload.value),
                           preload.setup_source_line.value_or(0));
      if (lower_setup_expression_to_x(setup, expression, preloads)) {
        emit_setup_store(setup, preload.register_name,
                         "setup " +
                             preload.setup_target_name.value_or("R" + preload.register_name));
      }
      continue;
    }
    if (preload.value == "1|-00") {
      consumed.insert(index);
      emit_negative_zero_degree_setup(setup, preload.register_name);
      continue;
    }
    if (!from_stack && !preload.setup_expression) {
      const std::optional<double> target_number = executable_setup_number(preload.value);
      if (target_number.has_value() && *target_number < 0.0) {
        std::optional<std::size_t> source_index;
        for (std::size_t candidate = 0; candidate < preloads.size(); ++candidate) {
          if (candidate == index || consumed.contains(candidate) ||
              preloads.at(candidate).setup_expression) {
            continue;
          }
          const std::optional<double> source_number =
              executable_setup_number(preloads.at(candidate).value);
          if (source_number.has_value() && *source_number > 0.0 &&
              std::fabs(*source_number + *target_number) < 1e-15) {
            source_index = candidate;
            break;
          }
        }
        if (source_index.has_value()) {
          const PreloadReport& source = preloads.at(*source_index);
          emit_exact_setup_number(setup, normalize_setup_constant_text(source.value));
          emit_setup_store(setup, source.register_name, setup_store_comment(source));
          emit_setup_recall(setup, source.register_name,
                            "setup constant " + normalize_setup_constant_text(preload.value) +
                                " base " + normalize_setup_constant_text(source.value));
          setup.emit_op(0x0b, "/-/",
                        "setup constant " + normalize_setup_constant_text(preload.value),
                        std::nullopt, true);
          emit_setup_store(setup, preload.register_name, setup_store_comment(preload));
          consumed.insert(index);
          consumed.insert(*source_index);
          optimizations.push_back(OptimizationReport{
              .name = "constant-synthesis",
              .detail = "Built setup constant " + normalize_setup_constant_text(preload.value) +
                        " by negating R" + source.register_name + ".",
          });
          continue;
        }
      }
    }
    if (const std::optional<RandomUniqueCoordListValue> unique =
            parse_random_unique_coord_list_value(preload.value)) {
      std::map<int, std::string> item_registers;
      std::vector<std::size_t> group_indices;
      for (std::size_t candidate = index; candidate < preloads.size(); ++candidate) {
        if (consumed.contains(candidate))
          continue;
        const std::optional<RandomUniqueCoordListValue> current =
            parse_random_unique_coord_list_value(preloads.at(candidate).value);
        if (!current.has_value() || current->domain != unique->domain ||
            current->list_name != unique->list_name || current->count != unique->count ||
            current->scaled_decimal != unique->scaled_decimal) {
          continue;
        }
        item_registers[current->index] = preloads.at(candidate).register_name;
        group_indices.push_back(candidate);
      }
      std::vector<std::string> ordered_registers;
      for (int item = 0; item < unique->count; ++item) {
        const auto item_it = item_registers.find(item);
        if (item_it == item_registers.end())
          break;
        ordered_registers.push_back(item_it->second);
      }
      if (static_cast<int>(ordered_registers.size()) == unique->count &&
          emit_random_unique_coord_list_setup(setup, boards, registers, preloads, *unique,
                                              ordered_registers, optimizations)) {
        for (const std::size_t consumed_index : group_indices)
          consumed.insert(consumed_index);
        initialized_random_coord_list = true;
        continue;
      }
      consumed.insert(index);
      const auto board_it = boards.find(unique->domain);
      if (board_it != boards.end()) {
        emit_random_board_coordinate_setup(setup, *board_it->second, preloads, preload.register_name,
                                           setup_target_name(preload));
        initialized_random_coord_list = true;
      }
      continue;
    }
    if (const std::optional<RandomUniqueSegmentedBitplaneValue> segmented =
            parse_random_unique_segmented_bitplane_value(preload.value)) {
      std::map<int, std::string> plane_registers;
      std::vector<std::size_t> group_indices;
      for (std::size_t candidate = index; candidate < preloads.size(); ++candidate) {
        if (consumed.contains(candidate))
          continue;
        const std::optional<RandomUniqueSegmentedBitplaneValue> current =
            parse_random_unique_segmented_bitplane_value(preloads.at(candidate).value);
        if (!current.has_value() || current->collection != segmented->collection ||
            current->count_source != segmented->count_source) {
          continue;
        }
        plane_registers[current->plane_index] = preloads.at(candidate).register_name;
        group_indices.push_back(candidate);
      }
      std::vector<std::string> ordered_registers;
      for (int plane = 0; plane < 4; ++plane) {
        const auto plane_it = plane_registers.find(plane);
        if (plane_it == plane_registers.end())
          break;
        ordered_registers.push_back(plane_it->second);
      }
      std::optional<std::size_t> count_source_preload_index;
      const auto count_register_it = registers.find(segmented->count_source);
      if (count_register_it != registers.end()) {
        for (std::size_t candidate = 0; candidate < preloads.size(); ++candidate) {
          if (consumed.contains(candidate))
            continue;
          const PreloadReport& count_preload = preloads.at(candidate);
          if (count_preload.register_name == count_register_it->second &&
              is_stack_preload_value(count_preload.value)) {
            count_source_preload_index = candidate;
            break;
          }
        }
      }
      if (ordered_registers.size() == 4U &&
          (!count_source_preload_index.has_value() ||
           emit_stack_preload_setup(setup, preloads.at(*count_source_preload_index))) &&
          emit_random_unique_segmented_bitplane_setup(setup, registers, *segmented,
                                                      ordered_registers, optimizations)) {
        if (count_source_preload_index.has_value())
          consumed.insert(*count_source_preload_index);
        for (const std::size_t consumed_index : group_indices)
          consumed.insert(consumed_index);
        initialized_segmented_bitplanes = true;
        continue;
      }
      consumed.insert(index);
      continue;
    }
    if (preload.value == "random()") {
      consumed.insert(index);
      setup.emit_op(0x3b, "К СЧ", "random()", std::nullopt, true);
      emit_setup_number_or_negated_preload(setup, preloads, "999");
      setup.emit_op(0x12, "*", "expr *", std::nullopt, true);
      setup.emit_op(0x0e, "В↑", "random int keep scaled draw", std::nullopt, true);
      setup.emit_op(0x35, "К {x}", "random int fractional part", std::nullopt, true);
      setup.emit_op(0x11, "-", "random int floor", std::nullopt, true);
      emit_setup_store(setup, preload.register_name, setup_store_comment(preload));
      continue;
    }
    if (const std::optional<std::string> random_domain = random_domain_value(preload.value)) {
      const auto board_it = boards.find(*random_domain);
      if (board_it != boards.end()) {
        consumed.insert(index);
        emit_random_board_coordinate_setup(setup, *board_it->second, preloads, preload.register_name,
                                           setup_target_name(preload));
        continue;
      }
    }
    if (!from_stack && !has_executable_setup_number_value(preload.value)) {
      bool emitted_display_literal_preload = false;
      if (const std::optional<FirstSpliceDisplayLiteralProgram> first_splice =
              preferred_first_splice_display_literal_program(preload.value)) {
        if (!is_formatted_coord_report_mask_preload(preload)) {
          emitted_display_literal_preload = emit_first_splice_display_literal_program_to_x(
              setup, *first_splice, preload.register_name, "setup R" + preload.register_name,
              optimizations);
        }
      }
      if (!emitted_display_literal_preload) {
        if (const std::optional<DisplayLiteralProgram> literal =
                display_literal_program(preload.value)) {
          if (literal->kind != "error" &&
              emit_display_literal_program_to_x(setup, *literal,
                                                setup_display_literal_comment(preload))) {
            emitted_display_literal_preload = true;
          }
        }
      }
      if (emitted_display_literal_preload) {
        for (std::size_t other = index; other < preloads.size(); ++other) {
          if (consumed.contains(other) || preloads.at(other).value != preload.value)
            continue;
          consumed.insert(other);
          const int reg_index = register_index(preloads.at(other).register_name);
          setup.emit_op(0x40 + reg_index, "X->П " + preloads.at(other).register_name,
                        setup_display_literal_comment(preloads.at(other)), std::nullopt, true);
        }
        continue;
      }
    }
    if (from_stack)
      emit_visible_stack_preload_setup(setup, preload);
    if (from_stack_x2)
      setup.emit_op(0x0a, ".", "setup " + setup_target_name(preload) + " from stack.X2",
                    std::nullopt, true);
    if (!from_stack)
      emit_exact_setup_number(setup,
                              executable_setup_value(preload.value).value_or(preload.value));
    std::vector<std::string> stored_registers;
    std::set<std::string> seen_registers;
    std::size_t target_count = 0;
    for (std::size_t other = index; other < preloads.size(); ++other) {
      if (consumed.contains(other) || preloads.at(other).value != preload.value)
        continue;
      consumed.insert(other);
      ++target_count;
      const std::string& register_name = preloads.at(other).register_name;
      if (seen_registers.contains(register_name))
        continue;
      seen_registers.insert(register_name);
      stored_registers.push_back(register_name);
      const int reg_index = register_index(register_name);
      setup.emit_op(0x40 + reg_index, "X->П " + register_name,
                    setup_store_comment(preloads.at(other)), std::nullopt, true);
    }
    if (!from_stack) {
      report_duplicate_preload_store_reuse(optimizations, preload.value, stored_registers,
                                           target_count, preload.register_name);
    }
    if (from_stack)
      emit_visible_stack_preload_restore(setup, preload);
  }
  flush_numeric_segment();
  if (setup_r0_dirty)
    emit_restore_setup_pointer_r0(setup, preloads, setup_r0_current_value,
                                  !options.disable_candidate_search);
  if (expected_mode.has_value() && !expected_mode_guard_emitted)
    emit_expected_mode_setup_check(setup, optimizations, *expected_mode);
  if (initialized_random_coord_list && !initialized_segmented_bitplanes)
    setup.emit_number("7");
  setup.emit_op(0x50, "С/П", "setup complete", std::nullopt, true);

  const ResolvedProgram resolved = resolve_machine_items(setup.items, options);
  return SetupProgramReport{
      .steps = resolved.steps,
      .reason = preloads.empty() && expected_mode.has_value()
                    ? "checks setup expected mode"
                    : "initializes compiler-owned preloads",
      .optimizations = std::move(optimizations),
  };
}

} // namespace mkpro::core::emit::lowering
