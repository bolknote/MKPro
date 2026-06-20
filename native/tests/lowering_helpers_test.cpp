#include "mkpro/core/emit/lowering_helpers.hpp"

#include "test_support.hpp"

namespace mkpro::tests {

void lowering_helpers_match_typescript_contract() {
  require(core::emit::coord_list_item_name("rooms", 3) == "__coord_list_rooms_3",
          "coord_list item naming should match TypeScript lowering helpers");

  const auto item = core::emit::coord_list_item_info("__coord_list_rooms_12");
  require(item.has_value(), "coord_list item parser should accept generated names");
  require(item->list_name == "rooms", "coord_list item parser should keep list name");
  require(item->index == 12, "coord_list item parser should keep numeric index");

  const auto underscored = core::emit::coord_list_item_info("__coord_list_cave_rooms_2");
  require(underscored.has_value(), "coord_list item parser should accept underscores");
  require(underscored->list_name == "cave_rooms",
          "coord_list item parser should parse up to the final index separator");
  require(!core::emit::coord_list_item_info("__dispatch_0").has_value(),
          "coord_list item parser should reject unrelated scratch names");

  const auto planes = core::emit::segmented_bitplane_names("map");
  require(planes.size() == 4, "segmented bitplanes should allocate four planes");
  require(planes.at(0) == "__seg_bitplane_map_0", "segmented bitplane plane 0 name");
  require(planes.at(3) == "__seg_bitplane_map_3", "segmented bitplane plane 3 name");
  require(core::emit::spatial_count_total_scratch_name() == "__spatial_count_total",
          "spatial count total scratch name");
  require(core::emit::spatial_count_line_scratch_name() == "__spatial_count_line",
          "spatial count line scratch name");
  require(core::emit::spatial_count_offset_scratch_name() == "__spatial_count_offset",
          "spatial count offset scratch name");
  require(core::emit::spatial_count_counter_scratch_name() == "__spatial_count_counter",
          "spatial count counter scratch name");

  const auto normalized = core::emit::normalize_display_literal_text("О—ВCDE");
  require(normalized.has_value(), "display literal normalization should accept known glyphs");
  require(*normalized == "0-LCDE", "display literal normalization should match TS glyph aliases");

  const auto cells = core::emit::display_literal_cells("8,-00");
  require(cells.has_value(), "display literal cells should parse commas and video glyphs");
  require(*cells == std::vector<int>({8, 10, 0, 0}),
          "display literal cells should map video symbols to cells");

  const auto direct = core::emit::display_literal_program("876");
  require(direct.has_value(), "display literal program should recognize direct video literals");
  require(direct->kind == "kinv", "display literal program should choose K INV when possible");
  require(direct->digits == "189", "display literal K INV digits should be inverted cells");

  const auto decimal = core::emit::decimal_display_literal_number("-20");
  require(decimal.has_value() && *decimal == "-20",
          "decimal display literal should preserve a signed decimal number");
  require(!core::emit::display_literal_mantissa_cells("1.2").has_value(),
          "display mantissa cells should reject explicit decimal points");
  require(core::emit::display_cells_literal({8, 10, 0, 15, 14}) == "8-0_E",
          "display cells should convert back to native literal text");
  require(core::emit::normalize_display_template_literal(" . - ") == ".-",
          "display template literals should ignore whitespace");

  const auto first_splice = core::emit::first_splice_display_literal_program("123.4");
  require(first_splice.has_value(), "first-splice display literal should be planned");
  require(first_splice->first == 1, "first-splice should keep the first display cell");
  require(first_splice->second.has_value() && *first_splice->second == 2,
          "first-splice should expose the second display cell");
  require(first_splice->exponent == 2,
          "first-splice should derive exponent from the decimal point position");

  Expression id = core::emit::identifier_expression("cell");
  require(id.kind == "identifier" && id.name == "cell", "identifier expression helper");
  Expression sum = core::emit::add_expression(core::emit::number_expression("1"),
                                              core::emit::number_expression("2"));
  require(sum.kind == "binary" && sum.op == "+", "add expression helper should build binary +");
  require(sum.left != nullptr && sum.left->raw == "1", "add expression should keep left value");
  require(sum.right != nullptr && sum.right->raw == "2", "add expression should keep right value");

  Expression bit_mask = core::emit::bit_mask_expression(core::emit::identifier_expression("i"));
  require(bit_mask.kind == "binary" && bit_mask.op == "+",
          "bit_mask expression should build the anchored mask addition");
  require(bit_mask.left != nullptr && bit_mask.left->kind == "number" &&
              bit_mask.left->raw == "8",
          "bit_mask expression should anchor masks at integer 8");
  require(bit_mask.right != nullptr && bit_mask.right->kind == "binary" &&
              bit_mask.right->op == "/",
          "bit_mask expression should divide bit value by a decimal place");

  Expression membership =
      core::emit::bit_membership_expression(core::emit::identifier_expression("mask"),
                                            core::emit::identifier_expression("i"));
  require(membership.kind == "call" && membership.callee == "sign",
          "bit membership should collapse the fractional test through sign()");
  require(membership.args.size() == 1 && membership.args.at(0).callee == "frac",
          "bit membership should test the fractional part");

  V2Board line_board;
  line_board.x_min = 3;
  line_board.x_max = 7;
  line_board.y_min = 0;
  line_board.y_max = 0;
  line_board.width = 5;
  line_board.height = 1;
  Expression line_index =
      core::emit::spatial_bit_index_expression_for_board(&line_board,
                                                         core::emit::identifier_expression("cell"));
  require(line_index.kind == "binary" && line_index.op == "-",
          "single-row boards should offset the spatial bit index by xMin");

  Expression hit = core::emit::spatial_hit_expression(core::emit::identifier_expression("mask"),
                                                      core::emit::identifier_expression("cell"));
  require(hit.kind == "call" && hit.callee == "__spatial_hit",
          "spatial hit helper should use the internal spatial-hit callee");
}

}  // namespace mkpro::tests
