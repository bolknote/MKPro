#include "mkpro/core/emit/lowering/proc_raw_setup.hpp"

#include "mkpro/core/emit/lowering_helpers.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <string_view>
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
  if (!std::regex_match(value.begin(), value.end(), match, pattern))
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
  if (!std::regex_match(value.begin(), value.end(), match, pattern))
    return std::nullopt;
  return RandomUniqueSegmentedBitplaneValue{
      .collection = match[1].str(),
      .count_source = match[2].str(),
      .plane_index = std::stoi(match[3].str()),
  };
}

void emit_random_int_setup(MachineEmitter& setup, const std::string& max) {
  setup.emit_op(0x3b, "К СЧ", "random()", std::nullopt, true);
  setup.emit_number(max);
  setup.emit_op(0x12, "*", "expr *", std::nullopt, true);
  setup.emit_op(0x0e, "В↑", "random int keep scaled draw", std::nullopt, true);
  setup.emit_op(0x35, "К {x}", "random int fractional part", std::nullopt, true);
  setup.emit_op(0x11, "-", "random int floor", std::nullopt, true);
}

void emit_setup_integer_offset(MachineEmitter& setup, int offset) {
  if (offset == 0)
    return;
  setup.emit_number(std::to_string(offset));
  setup.emit_op(0x10, "+", "random coord offset", std::nullopt, true);
}

void emit_setup_store(MachineEmitter& setup, const std::string& register_name,
                      std::string comment) {
  const int reg_index = register_index(register_name);
  setup.emit_op(0x40 + reg_index, "X->П " + register_name, std::move(comment), std::nullopt, true);
}

void emit_setup_recall(MachineEmitter& setup, const std::string& register_name,
                       std::string comment) {
  const int reg_index = register_index(register_name);
  setup.emit_op(0x60 + reg_index, "П->X " + register_name, std::move(comment), std::nullopt, true);
}

bool emit_stack_preload_setup(MachineEmitter& setup, const PreloadReport& preload) {
  if (preload.value != "stack.X" && preload.value != "stack.Y")
    return false;
  if (preload.value == "stack.Y")
    setup.emit_op(0x14, "X↔Y", "setup from stack.Y", std::nullopt, true);
  emit_setup_store(setup, preload.register_name, "setup R" + preload.register_name);
  if (preload.value == "stack.Y")
    setup.emit_op(0x14, "X↔Y", "restore stack.X after stack.Y setup", std::nullopt, true);
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
  setup.emit_number("100");
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
                                        const std::string& register_name) {
  if (board.height == 1) {
    emit_random_int_setup(setup, std::to_string(board.width));
    emit_setup_integer_offset(setup, board.x_min);
    emit_setup_store(setup, register_name, "setup R" + register_name);
    return;
  }
  if (board.width == 1) {
    emit_random_int_setup(setup, std::to_string(board.height));
    emit_setup_integer_offset(setup, board.y_min);
    emit_setup_store(setup, register_name, "setup R" + register_name);
    return;
  }

  emit_random_int_setup(setup, std::to_string(board.width));
  emit_setup_integer_offset(setup, board.x_min);
  emit_setup_store(setup, register_name, "random coord x");
  emit_random_int_setup(setup, std::to_string(board.height));
  emit_setup_integer_offset(setup, board.y_min);
  setup.emit_number("10");
  setup.emit_op(0x12, "*", "random coord y decade", std::nullopt, true);
  emit_setup_recall(setup, register_name, "random coord x");
  setup.emit_op(0x10, "+", "random coord", std::nullopt, true);
  emit_setup_store(setup, register_name, "setup R" + register_name);
}

int board_cell_count(const V2Board& board) {
  return board.width * board.height;
}

bool board_random_unique_candidate_supported(const V2Board& board) {
  return board.height == 1 || board.width == 1 ||
         (board.x_min == 0 && board.y_min == 0 && board.width == 10 && board.height == 10);
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

void emit_random_unique_candidate_setup(MachineEmitter& setup, const V2Board& board,
                                        const std::string& seed_register,
                                        const std::string& candidate_register,
                                        bool scaled_decimal) {
  emit_setup_recall(setup, seed_register, "random coord seed");
  setup.emit_number("37");
  setup.emit_op(0x12, "*", "random coord next seed", std::nullopt, true);
  setup.emit_op(0x35, "К {x}", "random coord seed fraction", std::nullopt, true);
  emit_setup_store(setup, seed_register, "random coord seed");
  setup.emit_number(std::to_string(board_cell_count(board)));
  setup.emit_op(0x12, "*", "random coord scaled seed", std::nullopt, true);
  setup.emit_op(0x34, "К [x]", "random coord flat index", std::nullopt, true);
  if (scaled_decimal && board.x_min == 0 && board.x_max == 9 && board.y_min == 0 &&
      board.y_max == 9 && board.width == 10 && board.height == 10) {
    setup.emit_number("10");
    setup.emit_op(0x13, "/", "random coord scaled decimal cell", std::nullopt, true);
    emit_setup_store(setup, candidate_register, "random coord scaled decimal cell");
    return;
  }

  if (board.height == 1) {
    emit_setup_integer_offset(setup, board.x_min);
  } else if (board.width == 1) {
    emit_setup_integer_offset(setup, board.y_min);
  }
  emit_setup_store(setup, candidate_register, "random coord candidate");
}

bool emit_compact_random_unique_coord_list_setup(
    MachineEmitter& setup, const std::map<std::string, const V2Board*>& boards,
    const std::map<std::string, std::string>& registers, const RandomUniqueCoordListValue& value,
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
  emit_random_unique_candidate_setup(setup, board, seed_register, current_it->second,
                                     value.scaled_decimal);

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
                                         const RandomUniqueCoordListValue& value,
                                         const std::vector<std::string>& item_registers) {
  const auto board_it = boards.find(value.domain);
  if (board_it == boards.end())
    return false;
  const V2Board& board = *board_it->second;
  if (!board_random_unique_candidate_supported(board))
    return false;

  if (emit_compact_random_unique_coord_list_setup(setup, boards, registers, value, item_registers))
    return true;

  const std::string seed_register = item_registers.back();
  setup.emit_op(0x3b, "К СЧ", "random coord seed", std::nullopt, true);
  emit_setup_store(setup, seed_register, "random coord seed");

  for (std::size_t index = 0; index < item_registers.size(); ++index) {
    const std::string draw_label = setup.fresh_label("random_coord_draw");
    setup.emit_label(draw_label, {.hidden = true});
    emit_random_unique_candidate_setup(setup, board, seed_register, item_registers.at(index),
                                       value.scaled_decimal);
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
  if (!std::regex_match(value.begin(), value.end(), match, random_domain_regex))
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
                                    const CompileOptions& options) {
  MachineEmitter setup;
  std::vector<OptimizationReport> optimizations;
  std::set<std::size_t> consumed;
  for (std::size_t index = 0; index < preloads.size(); ++index) {
    if (consumed.contains(index))
      continue;
    const PreloadReport& preload = preloads.at(index);
    const bool from_stack_x = preload.value == "stack.X";
    const bool from_stack_y = preload.value == "stack.Y";
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
          emit_random_unique_coord_list_setup(setup, boards, registers, *unique,
                                              ordered_registers)) {
        for (const std::size_t consumed_index : group_indices)
          consumed.insert(consumed_index);
        continue;
      }
      consumed.insert(index);
      const auto board_it = boards.find(unique->domain);
      if (board_it != boards.end())
        emit_random_board_coordinate_setup(setup, *board_it->second, preload.register_name);
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
              (count_preload.value == "stack.X" || count_preload.value == "stack.Y")) {
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
        continue;
      }
      consumed.insert(index);
      continue;
    }
    if (preload.value == "random()") {
      consumed.insert(index);
      emit_random_int_setup(setup, "999");
      const int reg_index = register_index(preload.register_name);
      setup.emit_op(0x40 + reg_index, "X->П " + preload.register_name,
                    "setup R" + preload.register_name, std::nullopt, true);
      continue;
    }
    if (const std::optional<std::string> random_domain = random_domain_value(preload.value)) {
      const auto board_it = boards.find(*random_domain);
      if (board_it != boards.end()) {
        consumed.insert(index);
        emit_random_board_coordinate_setup(setup, *board_it->second, preload.register_name);
        continue;
      }
    }
    if (!from_stack_x && !from_stack_y) {
      if (const std::optional<DisplayLiteralProgram> literal =
              display_literal_program(preload.value)) {
        if (literal->kind != "error" &&
            emit_display_literal_program_to_x(setup, *literal, "setup display literal")) {
          for (std::size_t other = index; other < preloads.size(); ++other) {
            if (consumed.contains(other) || preloads.at(other).value != preload.value)
              continue;
            consumed.insert(other);
            const int reg_index = register_index(preloads.at(other).register_name);
            setup.emit_op(0x40 + reg_index, "X->П " + preloads.at(other).register_name,
                          "setup R" + preloads.at(other).register_name, std::nullopt, true);
          }
          continue;
        }
      }
    }
    if (from_stack_y)
      setup.emit_op(0x14, "X↔Y", "setup from stack.Y", std::nullopt, true);
    if (!from_stack_x && !from_stack_y)
      setup.emit_number(preload.value);
    for (std::size_t other = index; other < preloads.size(); ++other) {
      if (consumed.contains(other) || preloads.at(other).value != preload.value)
        continue;
      consumed.insert(other);
      const int reg_index = register_index(preloads.at(other).register_name);
      setup.emit_op(0x40 + reg_index, "X->П " + preloads.at(other).register_name,
                    "setup R" + preloads.at(other).register_name, std::nullopt, true);
    }
    if (from_stack_y)
      setup.emit_op(0x14, "X↔Y", "restore stack.X after stack.Y setup", std::nullopt, true);
  }
  setup.emit_op(0x50, "С/П", "setup complete", std::nullopt, true);

  const ResolvedProgram resolved = resolve_machine_items(setup.items, options);
  return SetupProgramReport{
      .steps = resolved.steps,
      .reason = "initializes compiler-owned preloads",
      .optimizations = std::move(optimizations),
  };
}

} // namespace mkpro::core::emit::lowering
