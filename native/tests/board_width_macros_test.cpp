#include "mkpro/core/ast.hpp"
#include "mkpro/core/emit/lowering_helpers.hpp"

#include "test_support.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace mkpro::tests {

namespace {

Expression operand() {
  return core::emit::number_expression("5");
}

bool throws_hardware_verified(const auto& fn) {
  try {
    fn();
  } catch (const std::runtime_error& error) {
    return std::string(error.what()).find("hardware-verified") != std::string::npos;
  }
  return false;
}

} // namespace

void board_width_macros_matches_typescript_contract() {
  require(core::emit::k_default_board_width == 4,
          "DEFAULT_BOARD_WIDTH should remain the original 4-wide grid");

  require(expression_to_json(core::emit::grid_norm_expression(operand())) ==
              expression_to_json(core::emit::grid_norm_expression(operand(), 4)),
          "default grid_norm lowering should match explicit width 4");
  require(expression_to_json(core::emit::positive_grid_norm_expression(operand())) ==
              expression_to_json(core::emit::positive_grid_norm_expression(operand(), 4)),
          "default positive grid_norm lowering should match explicit width 4");
  require(expression_to_json(core::emit::cell_mask_expression(operand(), operand())) ==
              expression_to_json(core::emit::cell_mask_expression(operand(), operand(), 4)),
          "default cell_mask lowering should match explicit width 4");

  const Expression ordinary_row_constant{.kind = "number", .raw = "0.226"};
  const std::string ordinary_row_json = expression_to_json(ordinary_row_constant);
  const std::string cell_mask_json =
      expression_to_json(core::emit::cell_mask_expression(operand(), operand(), 4));
  require(ordinary_row_json.find("retunableNaturalFractionalPrefix") == std::string::npos,
          "ordinary numeric literals must not acquire an address-retuning proof");
  require(cell_mask_json.find("\"retunableNaturalFractionalPrefix\":\"0.226000\"") !=
              std::string::npos,
          "cell_mask row constants should retain their proved address-retuning origin");
  require(ordinary_row_json != cell_mask_json,
          "ordinary literals and proved address carriers must not share an AST key");

  const std::string wrap3 =
      expression_to_json(core::emit::grid_norm_expression(operand(), 3));
  const std::string wrap4 =
      expression_to_json(core::emit::grid_norm_expression(operand(), 4));
  require(wrap3.find("\"raw\":\"3\"") != std::string::npos,
          "width 3 grid_norm should thread raw width 3 through the expression");
  require(wrap3.find("\"raw\":\"4\"") == std::string::npos,
          "width 3 grid_norm should not contain raw width 4");
  require(wrap4.find("\"raw\":\"4\"") != std::string::npos,
          "width 4 grid_norm should thread raw width 4 through the expression");
  require(wrap3 != wrap4, "different board widths should produce different grid_norm ASTs");

  const std::string diag3 =
      expression_to_json(core::emit::positive_grid_norm_expression(operand(), 3));
  require(diag3.find("\"raw\":\"3\"") != std::string::npos,
          "width 3 positive grid_norm should thread raw width 3 through the expression");
  require(diag3.find("\"raw\":\"4\"") == std::string::npos,
          "width 3 positive grid_norm should not contain raw width 4");

  const double row_constant = core::emit::cell_mask_row_constant(4);
  for (int row = 1; row <= 4; ++row) {
    require(static_cast<int>(std::pow(10.0, row * row_constant)) == (1 << (row - 1)),
            "width 4 cell_mask row constant should encode row masks 1, 2, 4, 8");
  }
  require(throws_hardware_verified([] { (void)core::emit::cell_mask_row_constant(3); }),
          "unsupported cell_mask row widths should throw a hardware-verified diagnostic");
  require(throws_hardware_verified([] {
            (void)core::emit::cell_mask_expression(operand(), operand(), 5);
          }),
          "unsupported cell_mask expression widths should throw a hardware-verified diagnostic");
}

}  // namespace mkpro::tests
