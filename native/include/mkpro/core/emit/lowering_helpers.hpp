#pragma once

#include "mkpro/core/ast.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mkpro::core::emit {

inline constexpr std::string_view k_coord_list_item_prefix = "__coord_list_";
inline constexpr std::string_view k_coord_list_pointer = "__coord_list_pointer";
inline constexpr std::string_view k_coord_list_counter = "__coord_list_counter";
inline constexpr std::string_view k_coord_list_current = "__coord_list_current";
inline constexpr std::string_view k_coord_list_dx = "__coord_list_dx";
inline constexpr std::string_view k_segmented_bitplane_prefix = "__seg_bitplane_";
inline constexpr std::string_view k_segmented_bitplane_index = "__seg_bitplane_index";
inline constexpr std::string_view k_segmented_bitplane_selector = "__seg_bitplane_selector";
inline constexpr std::string_view k_spatial_count_scratch_prefix = "__spatial_count_";
inline constexpr std::string_view k_spatial_hit_callee = "__spatial_hit";
inline constexpr int k_default_board_width = 4;

struct CoordListItemInfo {
  std::string list_name;
  int index = 0;
};

struct DisplayLiteralProgram {
  std::string kind;
  std::string digits;
  std::string left;
  std::string right;
  bool negative = false;
};

struct FirstSpliceDisplayLiteralProgram {
  int first = 0;
  std::optional<int> second;
  DisplayLiteralProgram body;
  int exponent = 0;
  bool negative = false;
};

struct LeadingZeroHexProductPlan {
  std::string source_literal;
  std::string factor;
};

struct RemainderByConstantMatch {
  Expression value;
  Expression divisor;
};

std::string coord_list_item_name(const std::string& list_name, int index);
std::optional<CoordListItemInfo> coord_list_item_info(std::string_view name);
std::string segmented_bitplane_name(const std::string& collection, int index);
std::vector<std::string> segmented_bitplane_names(const std::string& collection);
std::string spatial_count_total_scratch_name();
std::string spatial_count_line_scratch_name();
std::string spatial_count_offset_scratch_name();
std::string spatial_count_counter_scratch_name();
bool is_unsigned_decimal_digits(std::string_view text);
std::optional<std::string> normalize_display_literal_text(std::string_view text);
std::optional<std::vector<int>> display_literal_cells(std::string_view text);
std::optional<DisplayLiteralProgram>
display_literal_program_from_cells(const std::optional<std::vector<int>>& cells, bool negative);
std::optional<DisplayLiteralProgram> display_literal_program(std::string_view text);
std::optional<std::string> decimal_display_literal_number(std::string_view text);
std::optional<LeadingZeroHexProductPlan>
leading_zero_hex_product_display_program(std::string_view text);
std::optional<std::vector<int>> display_literal_mantissa_cells(std::string_view text);
std::string display_cells_literal(const std::vector<int>& cells);
std::string normalize_display_template_literal(std::string_view text);
std::optional<int> display_literal_point_exponent(std::string_view text);
std::optional<FirstSpliceDisplayLiteralProgram>
first_splice_display_literal_program(std::string_view text);
Expression number_expression(std::string raw);
Expression identifier_expression(std::string name);
Expression binary_expression(Expression left, std::string op, Expression right);
Expression unary_expression(std::string op, Expression expr);
Expression call_expression(std::string callee, std::vector<Expression> args);
Expression add_expression(Expression left, Expression right);
Expression subtract_expression(Expression left, Expression right);
Expression multiply_expression(Expression left, Expression right);
Expression divide_expression(Expression left, Expression right);
Expression pow10_expression(Expression expression);
Expression int_expression(Expression expression);
Expression frac_expression(Expression expression);
Expression sign_expression(Expression expression);
Expression abs_expression(Expression expression);
Expression max_expression(Expression left, Expression right);
Expression min_expression(const Expression& left, const Expression& right);
Expression safe_max_expression(const Expression& left, const Expression& right);
Expression safe_min_expression(const Expression& left, const Expression& right);
Expression one_minus_expression(Expression expression);
double cell_mask_row_constant(int width);
Expression grid_norm_expression(Expression expression, int width = k_default_board_width);
Expression positive_grid_norm_expression(Expression expression, int width = k_default_board_width);
Expression bit_mask_expression(Expression index);
Expression bit_membership_expression(Expression mask, Expression index);
Expression cell_mask_expression(Expression x, Expression y, int width = k_default_board_width);
Expression offset_expression(Expression expression, int offset);
Expression board_cell_expression(Expression x, Expression y);
Expression spatial_bit_index_expression_for_board(const V2Board* board, Expression cell);
Expression spatial_hit_expression(Expression mask, Expression index);
std::optional<RemainderByConstantMatch> match_remainder_by_constant(const Expression& expression);

} // namespace mkpro::core::emit
