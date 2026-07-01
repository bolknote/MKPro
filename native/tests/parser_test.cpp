#include "mkpro/core/parser.hpp"

#include "test_support.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace mkpro::tests {

namespace {

void require_throws_contains(const std::string& source, const std::string& expected) {
  try {
    (void)parse_program(source);
  } catch (const ParseError& error) {
    require(std::string(error.what()).find(expected) != std::string::npos,
            "wrong parse error: " + std::string(error.what()));
    return;
  }
  throw std::runtime_error("parse should have failed with: " + expected);
}

} // namespace

void parser_matches_initial_v2_source_contract() {
  require_throws_contains("", "one V2 program block");
  require_throws_contains("target mk61\n", "Unexpected top-level line");

  const ProgramAst const_program = parse_program(R"mkpro(
program WithConst {
  const LIMIT = 99
  state {
    n: counter 0..99 = 0
  }
  loop {
    halt(n)
  }
}
)mkpro");
  require(const_program.v2.has_value(), "program should contain V2 AST");
  require(const_program.v2->name == "WithConst", "program name should parse");
  require(const_program.v2->consts.size() == 1, "const declaration should parse");
  require(const_program.v2->consts.at(0).name == "LIMIT", "const name should parse");
  require(const_program.v2->consts.at(0).expr == "99", "const expression should parse");
  require(const_program.v2->state.at(0).name == "n", "state field should parse");
  require(const_program.v2->body.at(0).kind == "v2_loop", "loop should parse");
  require(const_program.states.size() == 1, "parser should project V2 state into generic state");
  require(const_program.states.at(0).name == "WithConst", "generic state should use program name");
  require(const_program.states.at(0).fields.at(0).name == "n",
          "generic state field should preserve name");
  require(const_program.states.at(0).fields.at(0).type == "range",
          "counter should lower to generic range state");
  require(const_program.states.at(0).fields.at(0).min == 0, "generic state should preserve min");
  require(const_program.states.at(0).fields.at(0).max == 99, "generic state should preserve max");
  require(const_program.states.at(0).fields.at(0).initial.has_value(),
          "generic state should preserve parsed initial expression");
  require(const_program.states.at(0).fields.at(0).initial->kind == "number",
          "generic initial should parse as expression");

  const ProgramAst angle_program = parse_program(R"mkpro(
# leading comment
program WithAngleRequirement {
  state {
    expected_mode("grd")
  }
  loop {
    halt(cos(100)) // trailing comment
  }
}
)mkpro");
  require(angle_program.v2->expected_mode.has_value(), "expected mode should parse");
  require(angle_program.v2->expected_mode->mode == "grd", "expected mode should be grd");
  const ProgramAst loose_angle_program = parse_program(R"mkpro(
program WithLooseAngleRequirement {
  state {
    expected_mode("gradient")
  }
  loop {
    halt(0)
  }
}
)mkpro");
  require(loose_angle_program.v2->expected_mode.has_value(), "loose expected mode should parse");
  require(loose_angle_program.v2->expected_mode->mode == "grd",
          "loose expected mode should normalize by leading marker");
  const ProgramAst fixed_angle_program = parse_program(R"mkpro(
program WithFixedAngleRequirement {
  state {
    expected_mode_only("gradient")
  }
  loop {
    halt(0)
  }
}
)mkpro");
  require(fixed_angle_program.v2->expected_mode.has_value(), "fixed expected mode should parse");
  require(fixed_angle_program.v2->expected_mode->mode == "grd",
          "fixed expected mode should normalize by leading marker");
  require(fixed_angle_program.v2->expected_mode->only,
          "expected_mode_only should mark the mode as a whole-program invariant");
  require_throws_contains(R"mkpro(
program BadAngleRequirement {
  state {
    expected_mode("turnip")
  }
}
)mkpro",
                          "expected_mode(\"rad\")");

  const ProgramAst rich_program = parse_program(R"mkpro(
reference demo_reference

program Demo {
  ocean: board(0..9, 0..9)
  demo_world: board(decimal_player)
  state {
    line: group(1..2) {
      front: packed = 10
      robots: counter 0..9 = 1
    }
    pos: coord(demo_world) = 1
    foxes: coord_list(ocean, 3) = random_unique()
    score: counter 0..99 = stack.X
  }

  loop {
    show("BEEr", score:02)
    preview(score + 1)
    key = read()
    match key {
      1..3 => inc()
      otherwise => halt("8.-0", score)
    }
  }
  fn inc(delta) {
    score += delta
  }
}
)mkpro");
  require(rich_program.reference == "demo_reference", "reference should parse");
  require(rich_program.v2->boards.at(0).width == 10, "board width should parse");
  require(rich_program.v2->worlds.at(0).position->encoding == "decimal_player",
          "compact board encoding should parse");
  require(rich_program.v2->state.at(0).name == "line_front", "state group member should flatten");
  require(rich_program.v2->state.at(0).bank->member == "front", "state group member metadata");
  require(rich_program.v2->state.at(3).type == "coord_list", "coord_list should parse");
  require(rich_program.v2->state.at(3).count == 3, "coord_list count should parse");
  require(rich_program.v2->state.at(4).initial_stack == "X",
          "stack.X initializer should parse as initialStack");
  require(rich_program.states.at(0).fields.at(4).initial_stack == "X",
          "generic state should preserve stack.X initial source");

  const ProgramAst stack_x2_program = parse_program(R"mkpro(
program StackX2 {
  state {
    seed: counter 0..99 = stack.X2
  }
  loop {
    halt(seed)
  }
}
)mkpro");
  require(stack_x2_program.v2->state.at(0).initial_stack == "X2",
          "stack.X2 initializer should parse as initialStack");
  require(stack_x2_program.states.at(0).fields.at(0).initial_stack == "X2",
          "generic state should preserve stack.X2 initial source");
  const V2Statement& loop = rich_program.v2->body.at(0);
  require(loop.body.at(0).kind == "v2_show", "show should parse");
  require(loop.body.at(0).items->at(0).kind == "literal", "display literal should parse");
  require(loop.body.at(0).items->at(1).width == 2, "display width should parse");
  require(loop.body.at(0).items->at(1).pad == "zero", "display zero pad should parse");
  require(loop.body.at(1).kind == "v2_preview", "preview should parse");
  require(loop.body.at(2).kind == "v2_read", "read assignment should parse");
  require(loop.body.at(3).kind == "v2_match", "match should parse");
  require(loop.body.at(3).cases.at(0).values.size() == 3, "match range should expand");
  require(loop.body.at(3).otherwise != nullptr, "match otherwise should parse");
  require(rich_program.v2->rules.at(0).params == std::vector<std::string>{"delta"},
          "function params should parse");

  const ProgramAst spatial_program = parse_program(R"mkpro(
program Queries {
  cave: board(row_scan)
  dungeon: board(corridor_plan)
  state {
    pos: coord(cave) = 11
    room: coord(dungeon) = 8
    map: packed = 1234
    tile: counter 0..9 = 0
    wall: counter 0..9 = 0
  }
  loop {
    tile = cell_at(cave, pos)
    wall = cell_at(room)
  }
}
)mkpro");
  const V2Statement& spatial_loop = spatial_program.v2->body.at(0);
  require(spatial_loop.body.at(0).expr.has_value(), "cell_at assignment should keep expression");
  require(spatial_loop.body.at(0).expr->find("digit_at(map, (pos) - 10 * int((pos) / 10))") !=
              std::string::npos,
          "cell_at(domain, pos) should rewrite through the TS decimal-ones index");
  require(spatial_loop.body.at(1).expr.has_value(), "cell_at(pos) assignment should keep expr");
  require(*spatial_loop.body.at(1).expr == "digit_at(map, room)",
          "cell_at(pos) should infer the coordinate domain and corridor index");

  require_throws_contains(R"mkpro(
program BadStack {
  state {
    x: counter 0..9 = stack.Z
  }
  loop {
    halt(x)
  }
}
)mkpro",
                          "Use 'stack.X', 'stack.Y', or 'stack.X2'");

  const ProgramAst raw_program = parse_program(R"mkpro(
program RawDemo {
  state {
    value: counter 0..9 = 0
  }
  loop {
    raw {
      takes X = value
      returns X -> value
      clobbers stack, flags
      preserves state
      code {
        00: 50
      }
    }
  }
}
)mkpro");
  const V2Statement& raw = raw_program.v2->body.at(0).body.at(0);
  require(raw.kind == "v2_raw", "raw block should parse");
  require(raw.inputs.at(0).slot == "X", "raw input slot should parse");
  require(raw.outputs.at(0).target == "value", "raw output should parse");
  require(raw.lines.at(0).text == "00: 50", "raw code line should parse");

  require_throws_contains(R"mkpro(
program UnknownCompactBoard {
  sky: board(bogus_perspective)
  loop {
    halt(0)
  }
}
)mkpro",
                          "Unknown board encoding 'bogus_perspective'");
  require_throws_contains(R"mkpro(
program BadDisplay {
  loop {
    show(score ".-" turn_score:02)
  }
}
)mkpro",
                          "Display fragments must be separated by commas");

  const ProgramAst former_words_program = parse_program(R"mkpro(
program FormerWords {
  loop {
    world()
    screen()
  }
  fn world() {
    screen()
  }
  fn screen() {
    halt(0)
  }
}
)mkpro");
  require(former_words_program.v2->rules.at(0).name == "world",
          "former legacy word should be allowed as a function name");
  require(former_words_program.v2->rules.at(1).name == "screen",
          "former legacy word should be allowed as a function name");

  require_throws_contains(R"mkpro(
program OldRule {
  loop {
    step(1)
  }
  rule step(delta) {
    halt(0)
  }
}
)mkpro",
                          "Unexpected program line 'rule step(delta) {'");
  require_throws_contains(R"mkpro(
program OldScreen {
  state {
    score: counter 0..9 = 0
  }
  screen main {
    show(score)
  }
  loop {
    show(score)
  }
}
)mkpro",
                          "Unexpected program line 'screen main {'");
  require_throws_contains(R"mkpro(
program OldFleet {
  fleet enemy_fleet on ocean {
  }
  loop {
    halt(0)
  }
}
)mkpro",
                          "Unexpected program line 'fleet enemy_fleet on ocean {'");
  require_throws_contains(R"mkpro(
program OldChallenge {
  loop {
    challenge tile as challenge using warning, memory, answer {
      success {
        halt(1)
      }
    }
  }
}
)mkpro",
                          "Function calls must look like 'name(...)'");
}

void expression_parser_matches_initial_contract() {
  const Expression unary = parse_expression("-value", 7);
  require(unary.kind == "unary", "unary expression should parse");
  require(unary.op == "-", "unary op should parse");
  require(unary.expr->kind == "identifier", "unary operand should parse");

  const Expression binary = parse_expression("a + b * 2", 7);
  require(binary.kind == "binary", "binary expression should parse");
  require(binary.op == "+", "additive precedence root should be plus");
  require(binary.right->kind == "binary", "multiplication should bind tighter");
  require(binary.right->op == "*", "multiplicative op should parse");

  const Expression call =
      parse_expression(normalize_v2_expression_text("bit_or(slots[3], int(pos.floor))"), 7);
  require(call.kind == "call", "call expression should parse");
  require(call.callee == "bit_or", "callee should parse");
  require(call.args.size() == 2, "call args should parse");
  require(call.args.at(0).kind == "indexed", "indexed arg should parse");

  const Expression indexed = parse_expression("line[slot].robots", 7);
  require(indexed.kind == "indexed", "field-indexed expression should parse");
  require(indexed.field == "robots", "indexed field should parse");

  require(normalize_v2_expression_text("pos.floor") == "int(pos / 100)",
          "floor shorthand should normalize");
  require(expression_to_json(parse_expression("1 + 2", 7)).find("\"op\":\"+\"") !=
              std::string::npos,
          "expression JSON should be deterministic and inspectable");
}

void parser_accepts_all_example_sources() {
  const std::filesystem::path root = std::filesystem::current_path();
  const std::vector<std::filesystem::path> dirs = {
      root / "examples",
      root / "examples" / "pending-optimizer",
  };
  int parsed = 0;
  for (const auto& dir : dirs) {
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".mkpro")
        continue;
      std::ifstream input(entry.path());
      require(input.good(), "example should be readable: " + entry.path().string());
      std::ostringstream buffer;
      buffer << input.rdbuf();
      try {
        const ProgramAst ast = parse_program(buffer.str());
        require(ast.v2.has_value(), "example should produce V2 AST: " + entry.path().string());
      } catch (const std::exception& error) {
        throw std::runtime_error(entry.path().string() + ": " + error.what());
      }
      ++parsed;
    }
  }
  require(parsed == 31, "native parser should parse all 31 example sources");
}

} // namespace mkpro::tests
