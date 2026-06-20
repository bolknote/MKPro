#include "mkpro/core/emit/lowering/display.hpp"

#include "test_support.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

DisplayItem source_item(std::string name, std::optional<int> width = std::nullopt) {
  DisplayItem item;
  item.kind = "source";
  item.name = std::move(name);
  item.width = width;
  return item;
}

DisplayItem literal_item(std::string text) {
  DisplayItem item;
  item.kind = "literal";
  item.text = std::move(text);
  return item;
}

}  // namespace

void display_lowering_helpers_match_typescript_contract() {
  V2Statement statement;
  statement.kind = "v2_show";
  statement.line = 17;
  statement.items = std::vector<DisplayItem>{
      source_item("die"),
      literal_item(".-"),
      source_item("score", 2),
      literal_item("-"),
      source_item("total", 3),
      literal_item("-"),
      source_item("roll", 2),
  };

  V2Program program;
  program.body.push_back(statement);

  const std::vector<std::string> names =
      core::emit::display_scratch_register_names_for_program(program);
  require(names.size() == 2, "mantissa/exponent display should require two scratch names");
  require(names.at(0) == "__display_value_17", "display value scratch name");
  require(names.at(1) == "__display_loop_17", "display loop scratch name");

  statement.items = std::vector<DisplayItem>{
      source_item("die"),
      literal_item("."),
      source_item("score", 2),
  };
  program.body = {statement};
  require(core::emit::display_scratch_register_names_for_program(program).empty(),
          "unrelated display shapes should not reserve template scratch registers");

  const std::vector<DisplayItem> literal_items = {
      literal_item("87"),
      literal_item("6"),
  };
  const std::optional<std::string> collapsed =
      core::emit::collapse_literal_only_display(literal_items);
  require(collapsed.has_value(), "literal-only display items should collapse");
  require(*collapsed == "876", "literal-only display collapse should preserve text order");

  const std::vector<DisplayItem> mixed_items = {
      literal_item("87"),
      source_item("score"),
  };
  require(!core::emit::collapse_literal_only_display(mixed_items).has_value(),
          "mixed display items should not collapse as literal-only text");

  const std::optional<core::emit::DisplayLiteralProgram> literal_program =
      core::emit::display_literal_program("876");
  require(literal_program.has_value(), "display literal helper should plan direct video text");
  MachineEmitter emitter;
  require(core::emit::emit_direct_display_literal_program(emitter, *literal_program, 17),
          "direct display literal emitter should accept planned video text");
  require(emitter.items.size() >= 2, "direct display literal should emit calculator steps");
  bool saw_literal_video = false;
  bool saw_show_literal = false;
  for (const MachineItem& item : emitter.items) {
    if (item.opcode == 0x3a && item.comment == "display literal video bytes")
      saw_literal_video = true;
    if (item.opcode == 0x50 && item.comment == "show literal")
      saw_show_literal = true;
  }
  require(saw_literal_video, "direct display literal should emit K INV video bytes");
  require(saw_show_literal, "direct display literal should stop for display");
}

}  // namespace mkpro::tests
