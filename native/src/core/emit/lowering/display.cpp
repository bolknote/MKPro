#include "mkpro/core/emit/lowering/display.hpp"

#include "mkpro/core/emit/lowering_helpers.hpp"
#include "mkpro/core/machine_profile.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

namespace mkpro::core::emit {

namespace {

bool source_has_width(const DisplayItem& item, std::optional<int> width) {
  return item.kind == "source" && item.width == width;
}

std::string trim_ascii(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    value.pop_back();
  return value;
}

int decimal_power10(int exponent) {
  int value = 1;
  for (int index = 0; index < exponent; ++index)
    value *= 10;
  return value;
}

std::optional<std::pair<int, int>> coord_field_bounds(const LoweringContext& context,
                                                      const V2StateField& field) {
  if (field.type != "coord" || !field.domain.has_value())
    return std::nullopt;
  const auto board_it = context.boards.find(*field.domain);
  if (board_it == context.boards.end())
    return std::nullopt;
  const V2Board& board = *board_it->second;
  if (board.height == 1)
    return std::pair<int, int>{board.x_min, board.x_max};
  if (board.width == 1)
    return std::pair<int, int>{board.y_min, board.y_max};
  if (board.x_min >= 0 && board.x_max <= 9 && board.y_min >= 0 && board.y_max <= 9) {
    return std::pair<int, int>{board.x_min + 10 * board.y_min, board.x_max + 10 * board.y_max};
  }
  return std::nullopt;
}

std::optional<std::pair<int, int>> decimal_display_field_bounds(const LoweringContext& context,
                                                                const std::string& name) {
  const auto field_it = context.state_fields.find(name);
  if (field_it == context.state_fields.end())
    return std::nullopt;
  const V2StateField& field = *field_it->second;
  if (field.min.has_value() && field.max.has_value())
    return std::pair<int, int>{*field.min, *field.max};
  return coord_field_bounds(context, field);
}

bool display_field_fits_unsigned_width(const LoweringContext& context, const DisplayItem& item,
                                       int width) {
  if (item.kind != "source" || item.expr.has_value())
    return false;
  const std::optional<std::pair<int, int>> bounds =
      decimal_display_field_bounds(context, item.name);
  return bounds.has_value() && bounds->first >= 0 && bounds->second < decimal_power10(width);
}

struct DecimalDisplayField {
  const DisplayItem* item = nullptr;
  int width = 1;
};

std::optional<DecimalDisplayField> measure_decimal_display_field(const LoweringContext& context,
                                                                 const DisplayItem& item) {
  if (item.kind != "source")
    return std::nullopt;
  const std::optional<std::pair<int, int>> bounds =
      decimal_display_field_bounds(context, item.name);
  if (!bounds.has_value())
    return std::nullopt;
  const int magnitude = std::max(std::abs(bounds->first), std::abs(bounds->second));
  const int natural_width = std::max(1, static_cast<int>(std::to_string(magnitude).size()));
  const int width = item.width.value_or(natural_width);
  if (bounds->first < 0 || bounds->second >= decimal_power10(width))
    return std::nullopt;
  return DecimalDisplayField{.item = &item, .width = width};
}

bool lower_packed_decimal_display_fields(DisplayEmitApi& api,
                                         const std::vector<DecimalDisplayField>& fields,
                                         int source_line) {
  if (fields.empty())
    return false;
  if (!api.lower_display_item_to_x(*fields.front().item, "display source"))
    return false;
  for (std::size_t index = 1; index < fields.size(); ++index) {
    const DecimalDisplayField& field = fields.at(index);
    api.emit_display_scale(std::to_string(decimal_power10(field.width)), source_line);
    api.emitter.emit_op(0x12, "*", "packed display field shift", source_line);
    if (!api.lower_display_item_to_x(*field.item, "display source"))
      return false;
    api.emitter.emit_op(0x10, "+", "packed display field append", source_line);
  }
  return true;
}

struct DisplayMaskBodyField {
  const DisplayItem* item = nullptr;
  bool literal = false;
  std::string value;
  int width = 1;
};

struct FixedDisplayMaskTemplate {
  const DisplayItem* leader_item = nullptr;
  std::optional<int> leader_cell;
  std::vector<DisplayMaskBodyField> body_fields;
  std::string mask;
  int width = 1;
};

struct VariableLeadingDisplayMaskBranch {
  std::vector<DisplayMaskBodyField> body_fields;
  std::string mask;
  int width = 1;
};

struct VariableLeadingDisplayMaskTemplate {
  const DisplayItem* source = nullptr;
  int split = 10;
  VariableLeadingDisplayMaskBranch low;
  VariableLeadingDisplayMaskBranch high;
};

struct MantissaExponentTemplate {
  const DisplayItem* leader = nullptr;
  const DisplayItem* score = nullptr;
  const DisplayItem* total = nullptr;
  const DisplayItem* exponent = nullptr;
};

bool is_one_digit_display_source(const LoweringContext& context, const DisplayItem& source) {
  if (source.kind != "source")
    return false;
  if (source.width.has_value())
    return *source.width == 1;
  const std::optional<std::pair<int, int>> bounds =
      decimal_display_field_bounds(context, source.name);
  if (!bounds.has_value())
    return true;
  const int magnitude = std::max(std::abs(bounds->first), std::abs(bounds->second));
  const int width = std::max(1, static_cast<int>(std::to_string(magnitude).size()));
  return bounds->first >= 0 && bounds->second <= 9 && width == 1;
}

bool source_field_fits_width(const LoweringContext& context, const DisplayItem& item,
                             int expected_width) {
  if (item.kind != "source" || item.expr.has_value())
    return false;
  const std::optional<DecimalDisplayField> measured = measure_decimal_display_field(context, item);
  return measured.has_value() && measured->width == expected_width;
}

std::optional<MantissaExponentTemplate>
plan_mantissa_exponent_template(const LoweringContext& context,
                                const std::vector<DisplayItem>& items) {
  if (items.size() != 7U)
    return std::nullopt;
  if (!source_field_fits_width(context, items.at(0), 1) ||
      !source_field_fits_width(context, items.at(2), 2) ||
      !source_field_fits_width(context, items.at(4), 3) ||
      !source_field_fits_width(context, items.at(6), 2)) {
    return std::nullopt;
  }
  if (items.at(1).kind != "literal" || normalize_display_template_literal(items.at(1).text) != ".-")
    return std::nullopt;
  if (items.at(3).kind != "literal" || normalize_display_template_literal(items.at(3).text) != "-")
    return std::nullopt;
  if (items.at(5).kind != "literal" || normalize_display_template_literal(items.at(5).text) != "-")
    return std::nullopt;
  return MantissaExponentTemplate{
      .leader = &items.at(0),
      .score = &items.at(2),
      .total = &items.at(4),
      .exponent = &items.at(6),
  };
}

bool display_source_can_be_zero(const LoweringContext& context, const DisplayItem& item) {
  const std::optional<std::pair<int, int>> bounds =
      decimal_display_field_bounds(context, item.name);
  return !bounds.has_value() || bounds->first <= 0;
}

bool append_literal_display_mask_cells(std::vector<int>& mask_cells,
                                       std::vector<DisplayMaskBodyField>& body_fields, int& width,
                                       const std::vector<int>& literal_cells) {
  if (literal_cells.empty())
    return true;
  mask_cells.insert(mask_cells.end(), literal_cells.begin(), literal_cells.end());
  body_fields.push_back(DisplayMaskBodyField{
      .literal = true,
      .value = "0",
      .width = static_cast<int>(literal_cells.size()),
  });
  width += static_cast<int>(literal_cells.size());
  return width <= 8;
}

std::optional<FixedDisplayMaskTemplate>
plan_fixed_display_mask_template(const LoweringContext& context,
                                 const std::vector<DisplayItem>& items) {
  if (items.size() < 2U)
    return std::nullopt;

  const DisplayItem& first = items.front();
  FixedDisplayMaskTemplate result;
  std::vector<int> mask_cells{8};
  int width = 1;
  bool has_literal_cell = first.kind == "literal";
  std::vector<int> leading_literal_tail;

  if (first.kind == "source") {
    if (first.expr.has_value())
      return std::nullopt;
    const std::optional<DecimalDisplayField> field = measure_decimal_display_field(context, first);
    const std::optional<std::pair<int, int>> bounds =
        decimal_display_field_bounds(context, first.name);
    if (!field.has_value() || field->width != 1 || !bounds.has_value() || bounds->first < 0)
      return std::nullopt;
    result.leader_item = &first;
  } else if (first.kind == "literal") {
    const std::optional<std::vector<int>> cells = display_literal_mantissa_cells(first.text);
    if (!cells.has_value() || cells->empty() || cells->front() == 15)
      return std::nullopt;
    result.leader_cell = cells->front();
    leading_literal_tail.assign(cells->begin() + 1, cells->end());
  } else {
    return std::nullopt;
  }

  result.body_fields.push_back(DisplayMaskBodyField{
      .literal = true,
      .value = "9",
      .width = 1,
  });

  if (!append_literal_display_mask_cells(mask_cells, result.body_fields, width,
                                         leading_literal_tail)) {
    return std::nullopt;
  }

  for (std::size_t index = 1; index < items.size(); ++index) {
    const DisplayItem& item = items.at(index);
    if (item.kind == "source") {
      if (item.expr.has_value())
        return std::nullopt;
      const std::optional<DecimalDisplayField> field = measure_decimal_display_field(context, item);
      if (!field.has_value())
        return std::nullopt;
      result.body_fields.push_back(DisplayMaskBodyField{
          .item = &item,
          .literal = false,
          .width = field->width,
      });
      for (int digit = 0; digit < field->width; ++digit)
        mask_cells.push_back(0);
      width += field->width;
      if (width > 8)
        return std::nullopt;
      continue;
    }
    if (item.kind != "literal")
      return std::nullopt;
    const std::optional<std::vector<int>> cells = display_literal_mantissa_cells(item.text);
    if (!cells.has_value())
      return std::nullopt;
    if (!cells->empty())
      has_literal_cell = true;
    if (!append_literal_display_mask_cells(mask_cells, result.body_fields, width, *cells))
      return std::nullopt;
  }

  if (!has_literal_cell || width < 2 || width > 8)
    return std::nullopt;
  result.mask = display_cells_literal(mask_cells);
  result.width = width;
  return result;
}

std::optional<VariableLeadingDisplayMaskTemplate>
plan_variable_leading_display_mask_template(const LoweringContext& context,
                                            const std::vector<DisplayItem>& items) {
  if (items.size() < 2U)
    return std::nullopt;
  const DisplayItem& first = items.front();
  if (first.kind != "source" || first.expr.has_value())
    return std::nullopt;

  const std::optional<DecimalDisplayField> source = measure_decimal_display_field(context, first);
  const std::optional<std::pair<int, int>> bounds =
      decimal_display_field_bounds(context, first.name);
  if (!source.has_value() || source->width != 2 || !bounds.has_value() || bounds->first < 0 ||
      bounds->second < 10 || bounds->second >= 100) {
    return std::nullopt;
  }

  VariableLeadingDisplayMaskTemplate result;
  result.source = &first;
  result.low.body_fields.push_back(DisplayMaskBodyField{
      .literal = true,
      .value = "9",
      .width = 1,
  });
  std::vector<int> low_mask_cells{8};
  std::vector<int> high_mask_cells{8, 0};
  int low_width = 1;
  int high_width = 2;
  bool has_video_literal = false;

  for (std::size_t index = 1; index < items.size(); ++index) {
    const DisplayItem& item = items.at(index);
    if (item.kind == "source") {
      if (item.expr.has_value())
        return std::nullopt;
      const std::optional<DecimalDisplayField> field = measure_decimal_display_field(context, item);
      if (!field.has_value())
        return std::nullopt;
      const DisplayMaskBodyField body_field{
          .item = &item,
          .literal = false,
          .width = field->width,
      };
      result.low.body_fields.push_back(body_field);
      result.high.body_fields.push_back(body_field);
      for (int digit = 0; digit < field->width; ++digit) {
        low_mask_cells.push_back(0);
        high_mask_cells.push_back(0);
      }
      low_width += field->width;
      high_width += field->width;
      if (high_width > 8)
        return std::nullopt;
      continue;
    }

    if (item.kind != "literal")
      return std::nullopt;
    const std::optional<std::vector<int>> literal_cells = display_literal_mantissa_cells(item.text);
    if (!literal_cells.has_value())
      return std::nullopt;
    if (std::any_of(literal_cells->begin(), literal_cells->end(),
                    [](int cell) { return cell > 9; })) {
      has_video_literal = true;
    }
    if (literal_cells->empty())
      continue;
    const DisplayMaskBodyField gap_field{
        .literal = true,
        .value = "0",
        .width = static_cast<int>(literal_cells->size()),
    };
    result.low.body_fields.push_back(gap_field);
    result.high.body_fields.push_back(gap_field);
    low_mask_cells.insert(low_mask_cells.end(), literal_cells->begin(), literal_cells->end());
    high_mask_cells.insert(high_mask_cells.end(), literal_cells->begin(), literal_cells->end());
    low_width += static_cast<int>(literal_cells->size());
    high_width += static_cast<int>(literal_cells->size());
    if (high_width > 8)
      return std::nullopt;
  }

  if (!has_video_literal || low_width < 2 || high_width > 8)
    return std::nullopt;
  result.low.mask = display_cells_literal(low_mask_cells);
  result.low.width = low_width;
  result.high.mask = display_cells_literal(high_mask_cells);
  result.high.width = high_width;
  return result;
}

bool emit_display_mask_field_value(DisplayEmitApi& api, const DisplayMaskBodyField& field,
                                   std::string comment) {
  if (field.literal) {
    api.emitter.emit_number(field.value);
    if (!api.emitter.items.empty())
      api.emitter.items.back().comment = std::move(comment);
    return true;
  }
  return api.lower_display_item_to_x(*field.item, std::move(comment));
}

bool lower_display_mask_body_fields(DisplayEmitApi& api,
                                    const std::vector<DisplayMaskBodyField>& fields,
                                    int source_line) {
  if (fields.empty())
    return false;

  if (!emit_display_mask_field_value(api, fields.front(), "display mask numeric anchor"))
    return false;
  for (std::size_t index = 1; index < fields.size(); ++index) {
    const DisplayMaskBodyField& field = fields.at(index);
    api.emit_display_scale(std::to_string(decimal_power10(field.width)), source_line);
    api.emitter.emit_op(0x12, "*", "packed display field shift", source_line);
    if (field.literal && field.value == "0")
      continue;
    if (!emit_display_mask_field_value(api, field, "display source"))
      return false;
    api.emitter.emit_op(0x10, "+", "packed display field append", source_line);
  }
  return true;
}

void emit_mantissa_mask_body_merge(DisplayEmitApi& api, const std::string& mask_register,
                                   const std::string& scratch, int source_line,
                                   const std::string& comment_prefix) {
  api.emit_recall(mask_register);
  api.emitter.items.back().comment = comment_prefix + " literal mask";
  api.emitter.emit_op(0x38, "К ∨", "display mask body merge", source_line);
  api.emit_store(scratch, comment_prefix + " body");
}

void emit_mantissa_mask_leader_splice(DisplayEmitApi& api, const std::string& scratch, int width,
                                      int source_line, const std::string& comment_prefix) {
  api.emit_recall(scratch);
  api.emitter.items.back().comment = comment_prefix + " body";
  api.emitter.emit_op(0x14, "<->", comment_prefix + " leader merge", source_line);
  api.emitter.emit_op(0x54, "К НОП", comment_prefix + " leader preserve", source_line, true);
  api.emitter.emit_op(0x0c, "ВП", comment_prefix + " leader restore", source_line);
  emit_display_exponent(api.emitter, width - 1, source_line, comment_prefix + " exponent");
  api.emitter.emit_op(0x50, "С/П", "show display mask", source_line);
}

void emit_variable_leading_tail_digit(DisplayEmitApi& api, const DisplayItem& source, int split,
                                      int source_line) {
  api.emit_recall(source.name);
  api.emitter.items.back().comment = "display mask trailing digit";
  api.emit_display_scale(std::to_string(split), source_line);
  api.emitter.emit_op(0x13, "/", "display mask trailing digit quotient", source_line);
  api.emitter.emit_op(0x35, "К {x}", "display mask trailing digit fraction", source_line);
  api.emit_display_scale(std::to_string(split), source_line);
  api.emitter.emit_op(0x12, "*", "display mask trailing digit", source_line);
}

bool emit_variable_leading_high_body(DisplayEmitApi& api,
                                     const VariableLeadingDisplayMaskTemplate& template_plan,
                                     const std::string& scratch, int source_line) {
  emit_variable_leading_tail_digit(api, *template_plan.source, template_plan.split, source_line);
  api.emit_store(scratch, "display mask trailing digit");

  api.emitter.emit_number("9");
  if (!api.emitter.items.empty())
    api.emitter.items.back().comment = "display mask numeric anchor";
  api.emit_display_scale(std::to_string(template_plan.split), source_line);
  api.emitter.emit_op(0x12, "*", "packed display field shift", source_line);
  api.emit_recall(scratch);
  api.emitter.items.back().comment = "display mask trailing digit";
  api.emitter.emit_op(0x10, "+", "packed display field append", source_line);

  for (const DisplayMaskBodyField& field : template_plan.high.body_fields) {
    api.emit_display_scale(std::to_string(decimal_power10(field.width)), source_line);
    api.emitter.emit_op(0x12, "*", "packed display field shift", source_line);
    if (field.literal && field.value == "0")
      continue;
    if (!emit_display_mask_field_value(api, field, "display source"))
      return false;
    api.emitter.emit_op(0x10, "+", "packed display field append", source_line);
  }
  return true;
}

void emit_variable_leading_high_leader(DisplayEmitApi& api, const DisplayItem& source, int split,
                                       int source_line) {
  api.emit_recall(source.name);
  api.emitter.items.back().comment = "display mask high leader";
  api.emit_display_scale(std::to_string(split), source_line);
  api.emitter.emit_op(0x13, "/", "display mask high leader quotient", source_line);
  api.emitter.emit_op(0x34, "К [x]", "display mask high leader", source_line);
}

bool looks_like_mantissa_exponent_display_template(const std::vector<DisplayItem>& items) {
  if (items.size() != 7U)
    return false;
  if (!source_has_width(items.at(0), std::nullopt) && !source_has_width(items.at(0), 1))
    return false;
  if (!source_has_width(items.at(2), 2) || !source_has_width(items.at(4), 3) ||
      !source_has_width(items.at(6), 2))
    return false;
  return items.at(1).kind == "literal" &&
         normalize_display_template_literal(items.at(1).text) == ".-" &&
         items.at(3).kind == "literal" &&
         normalize_display_template_literal(items.at(3).text) == "-" &&
         items.at(5).kind == "literal" &&
         normalize_display_template_literal(items.at(5).text) == "-";
}

struct FormattedCoordReportTemplate {
  const DisplayItem* cell = nullptr;
  const DisplayItem* bearing = nullptr;
  int cell_scale_exp = 4;
  int video_anchor_exp = 7;
};

bool formatted_coord_report_body_matches(const std::optional<FormattedCoordReportBodyFact>& body,
                                         const FormattedCoordReportTemplate& template_plan) {
  return body.has_value() && template_plan.cell != nullptr && template_plan.bearing != nullptr &&
         body->cell == template_plan.cell->name &&
         body->cell_width == template_plan.cell->width.value_or(2) &&
         body->bearing == template_plan.bearing->name &&
         body->bearing_width == template_plan.bearing->width.value_or(1);
}

std::optional<FormattedCoordReportTemplate>
plan_formatted_coord_report_template(const LoweringContext& context,
                                     const std::vector<DisplayItem>& items) {
  if (!items_match_formatted_coord_report_shape(items))
    return std::nullopt;
  const DisplayItem& cell = items.at(1);
  const DisplayItem& bearing = items.at(3);
  if (!display_field_fits_unsigned_width(context, cell, 2) ||
      !display_field_fits_unsigned_width(context, bearing, 1)) {
    return std::nullopt;
  }
  return FormattedCoordReportTemplate{
      .cell = &cell,
      .bearing = &bearing,
  };
}

void collect_display_scratch_register_names(const V2Statement& statement,
                                            std::vector<std::string>& names) {
  if (statement.kind == "v2_show" && statement.items.has_value() &&
      looks_like_mantissa_exponent_display_template(*statement.items)) {
    names.push_back("__display_value_" + std::to_string(statement.line));
    names.push_back("__display_loop_" + std::to_string(statement.line));
  }
  for (const V2Statement& child : statement.body)
    collect_display_scratch_register_names(child, names);
  for (const V2Statement& child : statement.then_body)
    collect_display_scratch_register_names(child, names);
  for (const V2Statement& child : statement.else_body)
    collect_display_scratch_register_names(child, names);
  for (const V2MatchCase& match_case : statement.cases) {
    if (match_case.action != nullptr)
      collect_display_scratch_register_names(*match_case.action, names);
  }
  if (statement.otherwise != nullptr)
    collect_display_scratch_register_names(*statement.otherwise, names);
}

} // namespace

bool items_match_formatted_coord_report_shape(const std::vector<DisplayItem>& items) {
  if (items.size() != 4U)
    return false;
  const DisplayItem& prefix = items.at(0);
  const DisplayItem& cell = items.at(1);
  const DisplayItem& separator = items.at(2);
  const DisplayItem& bearing = items.at(3);
  if (prefix.kind != "literal" || cell.kind != "source" || separator.kind != "literal" ||
      bearing.kind != "source") {
    return false;
  }
  return normalize_display_template_literal(prefix.text) == "--" &&
         normalize_display_template_literal(separator.text) == "--" &&
         cell.width.value_or(2) == 2 && bearing.width.value_or(1) == 1;
}

bool statement_uses_formatted_coord_report(const V2Statement& statement) {
  if (statement.kind == "v2_show" && statement.items.has_value() &&
      items_match_formatted_coord_report_shape(*statement.items)) {
    return true;
  }
  for (const V2Statement& child : statement.body) {
    if (statement_uses_formatted_coord_report(child))
      return true;
  }
  for (const V2Statement& child : statement.then_body) {
    if (statement_uses_formatted_coord_report(child))
      return true;
  }
  for (const V2Statement& child : statement.else_body) {
    if (statement_uses_formatted_coord_report(child))
      return true;
  }
  for (const V2MatchCase& match_case : statement.cases) {
    if (match_case.action != nullptr && statement_uses_formatted_coord_report(*match_case.action)) {
      return true;
    }
  }
  return statement.otherwise != nullptr &&
         statement_uses_formatted_coord_report(*statement.otherwise);
}

bool program_uses_formatted_coord_report(const V2Program& program) {
  for (const V2Statement& statement : program.body) {
    if (statement_uses_formatted_coord_report(statement))
      return true;
  }
  for (const V2Rule& rule : program.rules) {
    for (const V2Statement& statement : rule.body) {
      if (statement_uses_formatted_coord_report(statement))
        return true;
    }
  }
  return false;
}

std::vector<std::string> display_scratch_register_names_for_program(const V2Program& program) {
  std::vector<std::string> names;
  for (const V2Statement& statement : program.body)
    collect_display_scratch_register_names(statement, names);
  for (const V2Rule& rule : program.rules) {
    for (const V2Statement& statement : rule.body)
      collect_display_scratch_register_names(statement, names);
  }
  return names;
}

std::optional<std::string> collapse_literal_only_display(const std::vector<DisplayItem>& items) {
  std::string literal;
  for (const DisplayItem& item : items) {
    if (item.kind != "literal")
      return std::nullopt;
    literal += item.text;
  }
  return literal;
}

bool emit_display_literal_program_to_x(MachineEmitter& emitter,
                                       const DisplayLiteralProgram& program, int source_line,
                                       std::string comment) {
  if (program.kind == "kinv") {
    emitter.emit_number(program.digits);
    emitter.emit_op(0x3a, "К ИНВ", std::move(comment), source_line);
  } else if (program.kind == "xor") {
    emitter.emit_number(program.left);
    emitter.emit_op(0x0e, "В↑", comment + " x/y split", source_line);
    emitter.emit_number(program.right);
    emitter.emit_op(0x39, "К ⊕", std::move(comment), source_line);
  } else {
    return false;
  }
  if (program.negative)
    emitter.emit_op(0x0b, "/-/", "display literal sign", source_line);
  return true;
}

bool emit_direct_display_literal_program(MachineEmitter& emitter,
                                         const DisplayLiteralProgram& program, int source_line) {
  if (program.kind == "error") {
    emitter.emit_op(0x29, "К ÷", "show literal error", source_line, true);
    emitter.emit_op(0x54, "К НОП", "show literal error padding", source_line, true);
    return true;
  }
  if (!emit_display_literal_program_to_x(emitter, program, source_line,
                                         "display literal video bytes")) {
    return false;
  }
  emitter.emit_op(0x50, "С/П", "show literal", source_line);
  return true;
}

bool emit_display_first_digit(LoweringContext& context, int cell, int source_line,
                              std::string comment) {
  if (cell >= 0 && cell <= 9) {
    context.emitter.emit_number(std::to_string(cell));
    if (!context.emitter.items.empty())
      context.emitter.items.back().comment = std::move(comment);
    return true;
  }
  if (cell >= 10 && cell <= 14) {
    context.emitter.emit_number("1" + std::to_string(15 - cell));
    context.emitter.emit_op(0x3a, "К ИНВ", comment, source_line);
    context.emitter.emit_op(0x35, "К {x}", std::move(comment), source_line);
    return true;
  }
  context.diagnostics.push_back(Diagnostic{
      .severity = DiagnosticSeverity::Error,
      .code = "native-unsupported",
      .message = "Unsupported display first digit " + std::to_string(cell),
  });
  return false;
}

void emit_display_exponent(MachineEmitter& emitter, int exponent, int source_line,
                           std::string comment) {
  emitter.emit_op(0x0c, "ВП", comment, source_line);
  for (char ch : std::to_string(exponent)) {
    emitter.emit_op(ch - '0', std::string(1, ch), comment, source_line);
  }
}

void emit_first_digit_splice(MachineEmitter& emitter, int source_line) {
  emitter.emit_op(0x14, "<->", "display first-cell splice", source_line);
  emitter.emit_op(0x54, "К НОП", "display first-cell splice", source_line, true);
  emitter.emit_op(0x0c, "ВП", "display first-cell splice", source_line);
}

bool emit_first_splice_display_literal_program(DisplayEmitApi& api, LoweringContext& context,
                                               const FirstSpliceDisplayLiteralProgram& program,
                                               const std::string& scratch, int source_line) {
  if (!emit_display_literal_program_to_x(api.emitter, program.body, source_line,
                                         "display literal video bytes body"))
    return false;
  api.emit_store(scratch, "display literal body scratch");

  if (program.first == 8) {
    api.emit_recall(scratch);
    api.emitter.items.back().comment = "display literal body scratch";
    if (program.negative)
      api.emitter.emit_op(0x0b, "/-/", "display literal sign", source_line);
    api.emitter.emit_op(0x54, "К НОП", "display literal first digit reuse", source_line, true);
    api.emitter.emit_op(0x0c, "ВП", "display literal first digit reuse", source_line);
    emit_display_exponent(api.emitter, program.exponent, source_line, "display literal exponent");
    context.optimizations.push_back(OptimizationReport{
        .name = "display-literal-first-digit-reuse",
        .detail = "Reused the literal body's leading 8 while restoring X2.",
    });
    return true;
  }

  if (program.first == 10 && program.second.has_value() && *program.second == 10) {
    api.emitter.emit_op(0x35, "К {x}", "display literal first digit from body", source_line);
    api.emit_recall(scratch);
    api.emitter.items.back().comment = "display literal body scratch";
    if (program.negative)
      api.emitter.emit_op(0x0b, "/-/", "display literal sign", source_line);
    emit_first_digit_splice(api.emitter, source_line);
    emit_display_exponent(api.emitter, program.exponent, source_line, "display literal exponent");
    context.optimizations.push_back(OptimizationReport{
        .name = "display-literal-minus-source-reuse",
        .detail = "Derived a leading '-' from the literal body's fractional tail.",
    });
    return true;
  }

  if (!emit_display_first_digit(context, program.first, source_line, "display literal first digit"))
    return false;
  api.emit_recall(scratch);
  api.emitter.items.back().comment = "display literal body scratch";
  if (program.negative)
    api.emitter.emit_op(0x0b, "/-/", "display literal sign", source_line);
  emit_first_digit_splice(api.emitter, source_line);
  emit_display_exponent(api.emitter, program.exponent, source_line, "display literal exponent");
  return true;
}

bool lower_decimal_point_display_statement(DisplayEmitApi& api, LoweringContext& context,
                                           const std::vector<DisplayItem>& items, int source_line) {
  int dot_index = -1;
  for (std::size_t index = 0; index < items.size(); ++index) {
    const DisplayItem& item = items.at(index);
    if (item.kind == "literal" && trim_ascii(item.text) == ".") {
      if (dot_index != -1)
        return false;
      dot_index = static_cast<int>(index);
      continue;
    }
    if (item.kind != "source" || item.expr.has_value())
      return false;
  }
  if (dot_index <= 0 || dot_index == static_cast<int>(items.size()) - 1)
    return false;

  std::vector<DecimalDisplayField> integer_fields;
  std::vector<DecimalDisplayField> fractional_fields;
  int fractional_width = 0;
  for (std::size_t index = 0; index < items.size(); ++index) {
    if (static_cast<int>(index) == dot_index)
      continue;
    const std::optional<DecimalDisplayField> measured =
        measure_decimal_display_field(context, items.at(index));
    if (!measured.has_value())
      return false;
    if (static_cast<int>(index) < dot_index) {
      integer_fields.push_back(*measured);
    } else {
      fractional_fields.push_back(*measured);
      fractional_width += measured->width;
    }
  }
  if (fractional_width == 0)
    return false;

  if (integer_fields.size() == 1U) {
    if (!lower_packed_decimal_display_fields(api, fractional_fields, source_line))
      return false;
    api.emit_display_scale(std::to_string(decimal_power10(fractional_width)), source_line);
    api.emitter.emit_op(0x13, "/", "decimal-point display fraction", source_line);
    api.emit_recall(integer_fields.front().item->name);
    api.emitter.items.back().comment = "decimal-point display integer";
    api.emitter.emit_op(0x10, "+", "decimal-point display append", source_line);
  } else {
    std::vector<DecimalDisplayField> fields = integer_fields;
    fields.insert(fields.end(), fractional_fields.begin(), fractional_fields.end());
    if (!lower_packed_decimal_display_fields(api, fields, source_line))
      return false;
    api.emit_display_scale(std::to_string(decimal_power10(fractional_width)), source_line);
    api.emitter.emit_op(0x13, "/", "decimal-point display", source_line);
  }
  api.emitter.emit_op(0x50, "С/П", "show decimal-point display", source_line);
  context.optimizations.push_back(OptimizationReport{
      .name = "decimal-point-display",
      .detail = "Displayed decimal-point show(...) as a fixed-point number with " +
                std::to_string(fractional_width) + " fractional digit(s).",
  });
  return true;
}

bool lower_dynamic_line_report_display_statement(DisplayEmitApi& api, LoweringContext& context,
                                                 const std::vector<DisplayItem>& items,
                                                 int source_line) {
  if (!machine_supports(mk61_profile(), "display-bytes"))
    return false;
  if (items.size() != 2U)
    return false;
  const DisplayItem& prefix = items.at(0);
  const DisplayItem& source = items.at(1);
  if (prefix.kind != "literal" || source.kind != "source" || prefix.text != "8.-0" ||
      !is_one_digit_display_source(context, source)) {
    return false;
  }

  if (!api.lower_display_item_to_x(source, "display dynamic line report source"))
    return false;
  api.emitter.emit_op(0x0e, "В↑", "display dynamic line report source", source_line);
  api.emit_number_or_preload("9800", std::nullopt, source_line);
  api.emitter.emit_op(0x14, "<->", "display dynamic line report digit operand order", source_line);
  api.emitter.emit_op(0x10, "+", "display dynamic line report digit", source_line);
  api.emitter.emit_op(0x0e, "В↑", "display dynamic line report right value", source_line);
  api.emit_number_or_preload("1200", std::nullopt, source_line);
  api.emitter.emit_op(0x14, "<->", "display dynamic line report operand order", source_line);
  api.emitter.emit_op(0x39, "К ⊕", "display dynamic line report video bytes", source_line);
  api.emitter.emit_op(0x50, "С/П", "show dynamic line report", source_line);
  context.optimizations.push_back(OptimizationReport{
      .name = "screen-dynamic-line-report-lowering",
      .detail = "Lowered screen at line " + std::to_string(source_line) +
                " as a dynamic 8.-0n calculator line report.",
  });
  return true;
}

bool lower_floor_packed_row_display_statement(DisplayEmitApi& api, LoweringContext& context,
                                              const std::vector<DisplayItem>& items,
                                              int source_line) {
  if (items.size() != 3U)
    return false;
  const DisplayItem& floor = items.at(0);
  const DisplayItem& separator = items.at(1);
  const DisplayItem& row = items.at(2);
  if (floor.kind != "source" || floor.expr.has_value() || separator.kind != "literal" ||
      separator.text != "." || row.kind != "source" || row.width.has_value()) {
    return false;
  }
  const auto floor_it = context.state_fields.find(floor.name);
  const auto row_it =
      row.expr.has_value() ? context.state_fields.end() : context.state_fields.find(row.name);
  if (floor_it == context.state_fields.end() ||
      (!row.expr.has_value() && row_it == context.state_fields.end()))
    return false;
  const V2StateField& floor_field = *floor_it->second;
  const int floor_min = floor_field.min.value_or(0);
  const int floor_max = floor_field.max.value_or(floor_min);
  const int floor_width = floor.width.value_or(std::max(
      1,
      static_cast<int>(std::to_string(std::max(std::abs(floor_min), std::abs(floor_max))).size())));
  if (floor_width != 1 || floor_min < 0 || floor_max > 9)
    return false;
  if (!row.expr.has_value()) {
    const V2StateField& row_field = *row_it->second;
    if (row_field.type != "packed")
      return false;
  }

  if (row.expr.has_value()) {
    if (!api.lower_expression_to_x(*row.expr))
      return false;
    if (!api.emitter.items.empty() && !api.emitter.items.back().comment.has_value())
      api.emitter.items.back().comment = "display packed row expression";
    api.emit_recall(floor.name);
    api.emitter.items.back().comment = "display packed row floor";
    api.emitter.emit_op(0x14, "<->", "display packed row expression merge", source_line);
    api.emitter.emit_op(0x0e, "В↑", "display packed row expression copy", source_line);
    api.emitter.emit_op(0x25, "F reverse", "display packed row expression rotate", source_line);
    api.emitter.emit_op(0x14, "<->", "display packed row floor restore", source_line);
  } else {
    api.emit_recall(floor.name);
    api.emitter.items.back().comment = "display packed row floor";
    api.emit_recall(row.name);
    api.emitter.items.back().comment = "display packed row";
    api.emitter.emit_op(0x14, "<->", "display packed row floor merge", source_line);
  }
  api.emitter.emit_op(0x25, "F reverse", "display packed row preserve", source_line);
  api.emitter.emit_op(0x0c, "ВП", "display packed row restore", source_line);
  api.emitter.emit_op(0x50, "С/П", "show packed row", source_line);
  context.optimizations.push_back(OptimizationReport{
      .name =
          row.expr.has_value() ? "floor-packed-row-expression-display" : "floor-packed-row-display",
      .detail = row.expr.has_value()
                    ? "Spliced one-digit floor into a packed row expression display."
                    : "Spliced one-digit floor into a packed row display.",
  });
  return true;
}

bool lower_mantissa_exponent_display_statement(DisplayEmitApi& api, LoweringContext& context,
                                               const std::vector<DisplayItem>& items,
                                               int source_line) {
  const std::optional<MantissaExponentTemplate> template_plan =
      plan_mantissa_exponent_template(context, items);
  if (!template_plan.has_value())
    return false;

  const std::string value_scratch = "__display_value_" + std::to_string(source_line);
  const std::string loop_scratch = "__display_loop_" + std::to_string(source_line);
  if (!api.ensure_hidden_register(value_scratch) || !api.ensure_hidden_register(loop_scratch)) {
    return false;
  }

  if (!api.lower_display_item_to_x(*template_plan->score, "display template score"))
    return false;
  api.emit_display_scale("1000", source_line);
  api.emitter.emit_op(0x13, "/", "display template score shift", source_line);
  if (!api.lower_display_item_to_x(*template_plan->total, "display template total"))
    return false;
  api.emit_display_scale("10000000", source_line);
  api.emitter.emit_op(0x13, "/", "display template total shift", source_line);
  api.emitter.emit_op(0x10, "+", "display template total append", source_line);
  api.emitter.emit_op(0x09, "9", "display template numeric anchor", source_line);
  api.emitter.emit_op(0x10, "+", "display template numeric body", source_line);

  const std::optional<DisplayLiteralProgram> mask_program = display_literal_program("8,-00-000");
  if (!mask_program.has_value() ||
      !emit_display_literal_program_to_x(api.emitter, *mask_program, source_line,
                                         "display template separator mask")) {
    context.diagnostics.push_back(Diagnostic{
        .severity = DiagnosticSeverity::Error,
        .code = "native-unsupported",
        .message = "Native display template mask is not lowerable",
    });
    return false;
  }
  api.emitter.emit_op(0x38, "К ∨", "display template body merge", source_line);
  api.emit_store(value_scratch, "display template body");

  const std::string exponent_zero_label = api.emitter.fresh_label("display_exponent_zero");
  const bool exponent_can_be_zero = display_source_can_be_zero(context, *template_plan->exponent);
  if (!api.lower_display_item_to_x(*template_plan->exponent, "display template exponent"))
    return false;
  if (exponent_can_be_zero) {
    api.emitter.emit_jump(0x57, "F x!=0", exponent_zero_label, "display template zero exponent",
                          source_line);
  }
  api.emit_store(loop_scratch, "display template exponent counter");
  api.emit_recall(value_scratch);
  api.emitter.items.back().comment = "display template body";

  const std::string loop_label = api.emitter.fresh_label("display_exponent_loop");
  api.emitter.emit_label(loop_label, {.hidden = true});
  api.emitter.emit_op(0x0c, "ВП", "display template exponent entry", source_line);
  api.emitter.emit_op(0x01, "1", "display template exponent digit", source_line);
  api.emitter.emit_op(0x0b, "/-/", "display template exponent sign", source_line);
  api.emit_store(value_scratch, "display template exponent body");
  api.emit_recall(loop_scratch);
  api.emitter.items.back().comment = "display template exponent counter";
  api.emitter.emit_number("1");
  api.emitter.emit_op(0x11, "-", "display template exponent decrement", source_line);
  api.emit_store(loop_scratch, "display template exponent counter");
  api.emitter.emit_jump(0x5e, "F x=0", loop_label, "display template exponent loop", source_line);

  if (exponent_can_be_zero)
    api.emitter.emit_label(exponent_zero_label, {.hidden = true});

  if (!api.lower_display_item_to_x(*template_plan->leader, "display template leader"))
    return false;
  api.emit_recall(value_scratch);
  api.emitter.items.back().comment = "display template body";
  api.emitter.emit_op(0x14, "<->", "display template leader merge", source_line);
  api.emitter.emit_op(0x54, "К НОП", "display template leader preserve", source_line, true);
  api.emitter.emit_op(0x0c, "ВП", "display template leader restore", source_line);
  api.emitter.emit_op(0x50, "С/П", "show display template", source_line);
  context.optimizations.push_back(OptimizationReport{
      .name = "display-byte-x2-lowering",
      .detail = "Built literal-separated show(...) through a mantissa/exponent video template.",
  });
  return true;
}

bool lower_variable_leading_display_mask_statement(DisplayEmitApi& api, LoweringContext& context,
                                                   const std::vector<DisplayItem>& items,
                                                   int source_line) {
  const std::optional<VariableLeadingDisplayMaskTemplate> template_plan =
      plan_variable_leading_display_mask_template(context, items);
  if (!template_plan.has_value())
    return false;

  const std::optional<std::string> low_mask = api.ensure_preloaded_number(template_plan->low.mask);
  const std::optional<std::string> high_mask =
      api.ensure_preloaded_number(template_plan->high.mask);
  if (!low_mask.has_value() || !high_mask.has_value())
    return false;

  const std::string scratch = "__display_mask_body_" + std::to_string(source_line);
  if (!api.ensure_hidden_register(scratch))
    return false;

  const std::string low_label = api.emitter.fresh_label("display_mask_low");
  const std::string end_label = api.emitter.fresh_label("display_mask_end");
  api.emit_recall(template_plan->source->name);
  api.emitter.items.back().comment = "display mask leading field";
  api.emit_display_scale(std::to_string(template_plan->split), source_line);
  api.emitter.emit_op(0x11, "-", "display mask leading width test", source_line);
  api.emitter.emit_jump(0x59, "F x>=0", low_label, "display mask low branch", source_line);

  if (!emit_variable_leading_high_body(api, *template_plan, scratch, source_line))
    return false;
  emit_mantissa_mask_body_merge(api, *high_mask, scratch, source_line, "display mask high");
  emit_variable_leading_high_leader(api, *template_plan->source, template_plan->split, source_line);
  emit_mantissa_mask_leader_splice(api, scratch, template_plan->high.width, source_line,
                                   "display mask high");
  api.emitter.emit_jump(0x51, "БП", end_label, "display mask end", source_line);

  api.emitter.emit_label(low_label, {.hidden = true});
  if (!lower_display_mask_body_fields(api, template_plan->low.body_fields, source_line))
    return false;
  emit_mantissa_mask_body_merge(api, *low_mask, scratch, source_line, "display mask");
  api.emit_recall(template_plan->source->name);
  api.emitter.items.back().comment = "display mask leader";
  emit_mantissa_mask_leader_splice(api, scratch, template_plan->low.width, source_line,
                                   "display mask");
  api.emitter.emit_label(end_label, {.hidden = true});
  context.optimizations.push_back(OptimizationReport{
      .name = "display-byte-variable-mask-lowering",
      .detail = "Built variable-width literal-separated show(...) through calculator video masks.",
  });
  return true;
}

bool lower_fixed_display_mask_statement(DisplayEmitApi& api, LoweringContext& context,
                                        const std::vector<DisplayItem>& items, int source_line) {
  const std::optional<FixedDisplayMaskTemplate> template_plan =
      plan_fixed_display_mask_template(context, items);
  if (!template_plan.has_value())
    return false;

  const std::string scratch = "__display_mask_body_" + std::to_string(source_line);
  if (!api.ensure_hidden_register(scratch))
    return false;
  if (!lower_display_mask_body_fields(api, template_plan->body_fields, source_line))
    return false;

  const std::optional<DisplayLiteralProgram> mask_program =
      display_literal_program(template_plan->mask);
  if (!mask_program.has_value() ||
      !emit_display_literal_program_to_x(api.emitter, *mask_program, source_line,
                                         "display mask literal")) {
    context.diagnostics.push_back(Diagnostic{
        .severity = DiagnosticSeverity::Error,
        .code = "native-unsupported",
        .message = "Native display mask literal is not lowerable",
    });
    return false;
  }
  api.emitter.emit_op(0x38, "К ∨", "display mask body merge", source_line);
  api.emit_store(scratch, "display mask body");

  if (template_plan->leader_item != nullptr) {
    if (!api.lower_display_item_to_x(*template_plan->leader_item, "display mask leader"))
      return false;
  } else if (template_plan->leader_cell.has_value()) {
    if (!emit_display_first_digit(context, *template_plan->leader_cell, source_line,
                                  "display mask leader"))
      return false;
  } else {
    return false;
  }

  api.emit_recall(scratch);
  api.emitter.items.back().comment = "display mask body";
  api.emitter.emit_op(0x14, "<->", "display mask leader merge", source_line);
  api.emitter.emit_op(0x54, "К НОП", "display mask leader preserve", source_line, true);
  api.emitter.emit_op(0x0c, "ВП", "display mask leader restore", source_line);
  emit_display_exponent(api.emitter, template_plan->width - 1, source_line,
                        "display mask exponent");
  api.emitter.emit_op(0x50, "С/П", "show display mask", source_line);
  context.optimizations.push_back(OptimizationReport{
      .name = "display-byte-mask-lowering",
      .detail = "Built literal-separated show(...) through a calculator video mask.",
  });
  return true;
}

bool lower_formatted_coord_report_display_statement(DisplayEmitApi& api, LoweringContext& context,
                                                    const std::vector<DisplayItem>& items,
                                                    int source_line) {
  const std::optional<FormattedCoordReportTemplate> template_plan =
      plan_formatted_coord_report_template(context, items);
  if (!template_plan.has_value())
    return false;
  const auto mask_it = context.register_index_by_name.find(std::string(k_coord_list_dx));
  if (mask_it == context.register_index_by_name.end())
    return false;
  const std::string mask_register_text = api.register_text_for(std::string(k_coord_list_dx));

  if (formatted_coord_report_body_matches(api.emitter.current_x_formatted_coord_report_body,
                                          *template_plan)) {
    api.emitter.emit_number(std::to_string(template_plan->video_anchor_exp));
    api.emitter.emit_op(0x15, "F 10^x", "display formatted video anchor", source_line);
    api.emitter.emit_op(0x10, "+", "display formatted video body", source_line);
    api.emitter.emit_op(0x60 + mask_it->second, "П->X " + mask_register_text,
                        "display formatted mask", source_line);
    api.emitter.emit_op(0x39, "К ⊕", "display formatted mask merge", source_line);
    api.emitter.emit_op(0x35, "К {x}", "display formatted video fraction", source_line);
    api.emitter.emit_op(0x0b, "/-/", "display formatted sign", source_line);
    api.emitter.emit_op(0x0c, "ВП", "display formatted exponent entry", source_line);
    api.emitter.emit_op(0x00 + template_plan->video_anchor_exp,
                        std::to_string(template_plan->video_anchor_exp),
                        "display formatted exponent", source_line);
    api.emitter.emit_op(0x50, "С/П", "show formatted coord report", source_line);
    context.optimizations.push_back(OptimizationReport{
        .name = "formatted-coord-report-packed-body",
        .detail = "Reused packed --CC-- N body already in X for formatted coord report.",
    });
    context.optimizations.push_back(OptimizationReport{
        .name = "formatted-coord-report-lowering",
        .detail = "Lowered formatted coord report as --CC-- N calculator video output.",
    });
    if (context.pending_coord_list_line_count_formatted_report_fusion.has_value()) {
      context.optimizations.push_back(
          *context.pending_coord_list_line_count_formatted_report_fusion);
      context.pending_coord_list_line_count_formatted_report_fusion.reset();
    }
    return true;
  }
  context.pending_coord_list_line_count_formatted_report_fusion.reset();

  if (api.emitter.current_x_variable != template_plan->bearing->name) {
    api.emit_recall(template_plan->bearing->name);
    api.emitter.items.back().comment = "display formatted bearing";
  }
  api.emit_recall(template_plan->cell->name);
  api.emitter.items.back().comment = "display formatted cell";
  if (context.scaled_coord_variables.contains(template_plan->cell->name)) {
    api.emitter.emit_number("10");
    api.emitter.emit_op(0x12, "*", "display formatted scaled cell restore", source_line);
  }
  api.emitter.emit_number(std::to_string(template_plan->cell_scale_exp));
  api.emitter.emit_op(0x15, "F 10^x", "display formatted cell scale", source_line);
  api.emitter.emit_op(0x12, "*", "display formatted cell shift", source_line);
  api.emitter.emit_op(0x10, "+", "display formatted bearing append", source_line);
  api.emitter.emit_number(std::to_string(template_plan->video_anchor_exp));
  api.emitter.emit_op(0x15, "F 10^x", "display formatted video anchor", source_line);
  api.emitter.emit_op(0x10, "+", "display formatted video body", source_line);
  api.emitter.emit_op(0x60 + mask_it->second, "П->X " + mask_register_text,
                      "display formatted mask", source_line);
  api.emitter.emit_op(0x39, "К ⊕", "display formatted mask merge", source_line);
  api.emitter.emit_op(0x35, "К {x}", "display formatted video fraction", source_line);
  api.emitter.emit_op(0x0b, "/-/", "display formatted sign", source_line);
  api.emitter.emit_op(0x0c, "ВП", "display formatted exponent entry", source_line);
  api.emitter.emit_op(0x00 + template_plan->video_anchor_exp,
                      std::to_string(template_plan->video_anchor_exp), "display formatted exponent",
                      source_line);
  api.emitter.emit_op(0x50, "С/П", "show formatted coord report", source_line);
  api.emitter.current_x_variable.reset();
  api.emitter.current_x_aliases.clear();
  context.optimizations.push_back(OptimizationReport{
      .name = "formatted-coord-report-lowering",
      .detail = "Lowered formatted coord report as --CC-- N calculator video output.",
  });
  return true;
}

} // namespace mkpro::core::emit
