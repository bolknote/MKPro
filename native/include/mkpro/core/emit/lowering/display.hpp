#pragma once

#include "mkpro/core/ast.hpp"
#include "mkpro/core/emit/lowering_context.hpp"
#include "mkpro/core/emit/lowering_helpers.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core::emit {

struct DisplayEmitApi {
  MachineEmitter& emitter;
  std::function<void(const std::string&)> emit_recall;
  std::function<void(const std::string&, std::string)> emit_store;
  std::function<bool(const Expression&)> lower_expression_to_x;
  std::function<bool(const DisplayItem&, std::string)> lower_display_item_to_x;
  std::function<void(const std::string&, std::optional<std::string>, std::optional<int>)>
      emit_number_or_preload;
  std::function<void(const std::string&, int)> emit_display_scale;
  std::function<bool(const std::string&)> ensure_hidden_register;
  std::function<std::optional<std::string>(const std::string&)> ensure_preloaded_number;
  std::function<std::string(const std::string&)> register_text_for;
};

std::vector<std::string> display_scratch_register_names_for_program(const V2Program& program);
std::vector<std::string> display_template_mask_scratch_names_for_program(
    const V2Program& program);
std::vector<std::string> display_constant_preload_values_for_program(
    const LoweringContext& context, const V2Program& program);
std::string first_splice_display_scratch_name(int source_line);
std::optional<std::string> collapse_literal_only_display(const std::vector<DisplayItem>& items);
bool emit_display_literal_program_to_x(MachineEmitter& emitter,
                                       const DisplayLiteralProgram& program, int source_line,
                                       std::string comment);
bool emit_direct_display_literal_program(MachineEmitter& emitter,
                                         const DisplayLiteralProgram& program, int source_line);
bool emit_display_first_digit(LoweringContext& context, int cell, int source_line,
                              std::string comment);
void emit_display_exponent(MachineEmitter& emitter, int exponent, int source_line,
                           std::string comment);
void emit_first_digit_splice(MachineEmitter& emitter, int source_line);
bool emit_first_splice_display_literal_program(DisplayEmitApi& api, LoweringContext& context,
                                               const FirstSpliceDisplayLiteralProgram& program,
                                               const std::string& scratch, int source_line);
bool lower_decimal_point_display_statement(DisplayEmitApi& api, LoweringContext& context,
                                           const std::vector<DisplayItem>& items, int source_line);
bool lower_dynamic_line_report_display_statement(DisplayEmitApi& api, LoweringContext& context,
                                                 const std::vector<DisplayItem>& items,
                                                 int source_line);
bool lower_floor_packed_row_display_statement(DisplayEmitApi& api, LoweringContext& context,
                                              const std::vector<DisplayItem>& items,
                                              int source_line);
bool lower_mantissa_exponent_display_statement(DisplayEmitApi& api, LoweringContext& context,
                                               const std::vector<DisplayItem>& items,
                                               const std::string& display_name, int source_line);
bool lower_variable_leading_display_mask_statement(DisplayEmitApi& api, LoweringContext& context,
                                                   const std::vector<DisplayItem>& items,
                                                   const std::string& display_name,
                                                   int source_line);
bool lower_fixed_display_mask_statement(DisplayEmitApi& api, LoweringContext& context,
                                        const std::vector<DisplayItem>& items,
                                        const std::string& display_name, int source_line);
bool items_match_formatted_coord_report_shape(const std::vector<DisplayItem>& items);
bool program_uses_formatted_coord_report(const V2Program& program);
bool lower_formatted_coord_report_display_statement(DisplayEmitApi& api, LoweringContext& context,
                                                    const std::vector<DisplayItem>& items,
                                                    const std::string& display_name,
                                                    int source_line);

} // namespace mkpro::core::emit
