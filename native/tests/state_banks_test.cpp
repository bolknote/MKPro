#include "mkpro/compiler.hpp"
#include "mkpro/core/state_banks.hpp"

#include "mkpro/core/emit/lowering_helpers.hpp"

#include "test_support.hpp"

#include <algorithm>

namespace mkpro::tests {

void state_banks_match_typescript_contract() {
  require(core::bank_member_key("cells", std::nullopt) == "cells",
          "bank member key without field should be the base name");
  require(core::bank_member_key("ship", std::optional<std::string>{"x"}) == "ship.x",
          "bank member key with field should use dotted member syntax");
  require(core::bank_selector_variable_name("cells", std::nullopt) == "__bank_selector_cells",
          "bank selector without field should use TS selector prefix");
  require(core::bank_selector_variable_name("ship", std::optional<std::string>{"x"}) ==
              "__bank_selector_ship_x",
          "bank selector with field should flatten dotted member syntax");

  V2StateField field;
  field.name = "ship_x";
  field.bank = V2StateBankField{.name = "ship", .member = "x", .min = 0, .max = 9};
  require(core::state_bank_key(field) == "ship.x",
          "state bank key should use the declared bank/member pair");
  require(core::state_bank_element_name(field, 7) == "ship_x_7",
          "state bank element names should append the numeric index");

  require(core::numeric_index_value(core::emit::number_expression("7")).value() == 7,
          "numeric index should parse integer number literals");
  require(core::numeric_index_value(core::emit::unary_expression(
              "-", core::emit::number_expression("3")))
              .value() == -3,
          "numeric index should parse negative integer literals");
  require(!core::numeric_index_value(core::emit::number_expression("1.5")).has_value(),
          "numeric index should reject fractional literals");

  const auto plain = core::affine_index_identifier_offset(core::emit::identifier_expression("slot"));
  require(plain.has_value() && plain->name == "slot" && plain->offset == 0 &&
              !plain->integer_part,
          "affine selector should recognize plain identifiers");

  const auto plus = core::affine_index_identifier_offset(core::emit::add_expression(
      core::emit::identifier_expression("slot"), core::emit::number_expression("2")));
  require(plus.has_value() && plus->name == "slot" && plus->offset == 2,
          "affine selector should recognize identifier plus constant");

  const auto int_part = core::affine_index_identifier_offset(core::emit::int_expression(
      core::emit::identifier_expression("blocked")));
  require(int_part.has_value() && int_part->name == "blocked" && int_part->integer_part,
          "affine selector should recognize int(identifier)");

  const CompileResult scalar_bank_initial = compile_source(R"mkpro(
program ScalarBankInitial {
  state {
    digits: counter[1..2] 0..9 = 0
  }

  loop {
    digits[1] = 3
    digits[2] = 4
    halt(digits[1] + digits[2])
  }
}
)mkpro");
  require(scalar_bank_initial.implemented, "scalar bank initializer fixture should compile");
  require(scalar_bank_initial.diagnostics.empty(),
          "scalar bank initializer fixture should not report diagnostics");
  require(std::none_of(scalar_bank_initial.preloads.begin(), scalar_bank_initial.preloads.end(),
                       [](const PreloadReport& preload) {
                         return preload.setup_target_name.has_value() &&
                                preload.setup_target_name->starts_with("digits_");
                       }),
          "scalar bank initializer should not expand into per-element setup preloads");
}

}  // namespace mkpro::tests
