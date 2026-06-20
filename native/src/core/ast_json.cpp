#include "mkpro/core/ast.hpp"

#include <sstream>
#include <string>

namespace mkpro {

namespace {

std::string json_escape(std::string_view value) {
  std::ostringstream out;
  out << '"';
  for (char ch : value) {
    switch (ch) {
      case '\\':
        out << "\\\\";
        break;
      case '"':
        out << "\\\"";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        out << ch;
        break;
    }
  }
  out << '"';
  return out.str();
}

template <typename T, typename Fn>
std::string json_array(const std::vector<T>& values, Fn item_to_json) {
  std::ostringstream out;
  out << '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0) out << ',';
    out << item_to_json(values.at(index));
  }
  out << ']';
  return out.str();
}

std::string string_array_to_json(const std::vector<std::string>& values) {
  return json_array(values, [](const std::string& value) { return json_escape(value); });
}

void add_field(std::ostringstream& out, bool& first, std::string_view name, std::string value) {
  if (!first) out << ',';
  first = false;
  out << json_escape(name) << ':' << value;
}

std::string raw_line_to_json(const RawBlockLine& line) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  add_field(out, first, "text", json_escape(line.text));
  add_field(out, first, "line", std::to_string(line.line));
  out << '}';
  return out.str();
}

std::string display_item_to_json(const DisplayItem& item) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  add_field(out, first, "kind", json_escape(item.kind));
  if (item.kind == "literal") add_field(out, first, "text", json_escape(item.text));
  if (item.kind == "source") {
    add_field(out, first, "name", json_escape(item.name));
    if (item.expr.has_value()) add_field(out, first, "expr", expression_to_json(*item.expr));
    if (item.width.has_value()) add_field(out, first, "width", std::to_string(*item.width));
    if (item.pad.has_value()) add_field(out, first, "pad", json_escape(*item.pad));
  }
  add_field(out, first, "line", std::to_string(item.line));
  out << '}';
  return out.str();
}

std::string predicate_to_json(const V2Predicate& predicate) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  add_field(out, first, "kind", json_escape(predicate.kind));
  if (predicate.kind == "v2_contains") {
    add_field(out, first, "collection", json_escape(predicate.collection));
    add_field(out, first, "item", json_escape(predicate.item));
  } else {
    add_field(out, first, "left", json_escape(predicate.left));
    add_field(out, first, "op", json_escape(predicate.op));
    add_field(out, first, "right", json_escape(predicate.right));
  }
  out << '}';
  return out.str();
}

std::string raw_input_to_json(const V2RawInput& input) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  add_field(out, first, "slot", json_escape(input.slot));
  add_field(out, first, "expr", json_escape(input.expr));
  add_field(out, first, "line", std::to_string(input.line));
  out << '}';
  return out.str();
}

std::string raw_output_to_json(const V2RawOutput& output) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  add_field(out, first, "slot", json_escape(output.slot));
  add_field(out, first, "target", json_escape(output.target));
  add_field(out, first, "line", std::to_string(output.line));
  out << '}';
  return out.str();
}

std::string statement_to_json(const V2Statement& statement);

std::string match_case_to_json(const V2MatchCase& match_case) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  add_field(out, first, "values", string_array_to_json(match_case.values));
  add_field(out, first, "action",
            match_case.action == nullptr ? "null" : statement_to_json(*match_case.action));
  add_field(out, first, "line", std::to_string(match_case.line));
  out << '}';
  return out.str();
}

std::string statement_array_to_json(const std::vector<V2Statement>& statements) {
  return json_array(statements, [](const V2Statement& statement) {
    return statement_to_json(statement);
  });
}

std::string statement_to_json(const V2Statement& statement) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  add_field(out, first, "kind", json_escape(statement.kind));
  if (statement.target.has_value()) add_field(out, first, "target", json_escape(*statement.target));
  if (statement.expr.has_value()) add_field(out, first, "expr", json_escape(*statement.expr));
  if (statement.op.has_value()) add_field(out, first, "op", json_escape(*statement.op));
  if (statement.name.has_value()) add_field(out, first, "name", json_escape(*statement.name));
  if (!statement.args.empty()) add_field(out, first, "args", string_array_to_json(statement.args));
  if (statement.predicate.has_value()) {
    add_field(out, first, "predicate", predicate_to_json(*statement.predicate));
  }
  if (statement.negated) add_field(out, first, "negated", "true");
  if (!statement.then_body.empty()) {
    add_field(out, first, "thenBody", statement_array_to_json(statement.then_body));
  }
  if (!statement.else_body.empty()) {
    add_field(out, first, "elseBody", statement_array_to_json(statement.else_body));
  }
  if (!statement.body.empty()) add_field(out, first, "body", statement_array_to_json(statement.body));
  if (statement.items.has_value()) {
    add_field(out, first, "items",
              json_array(*statement.items, [](const DisplayItem& item) {
                return display_item_to_json(item);
              }));
  }
  if (!statement.inputs.empty()) {
    add_field(out, first, "inputs",
              json_array(statement.inputs, [](const V2RawInput& input) {
                return raw_input_to_json(input);
              }));
  }
  if (!statement.outputs.empty()) {
    add_field(out, first, "outputs",
              json_array(statement.outputs, [](const V2RawOutput& output) {
                return raw_output_to_json(output);
              }));
  }
  if (!statement.clobbers.empty()) {
    add_field(out, first, "clobbers", string_array_to_json(statement.clobbers));
  }
  if (!statement.preserves.empty()) {
    add_field(out, first, "preserves", string_array_to_json(statement.preserves));
  }
  if (!statement.lines.empty()) {
    add_field(out, first, "lines",
              json_array(statement.lines, [](const RawBlockLine& line) {
                return raw_line_to_json(line);
              }));
  }
  if (!statement.cases.empty()) {
    add_field(out, first, "cases",
              json_array(statement.cases, [](const V2MatchCase& match_case) {
                return match_case_to_json(match_case);
              }));
  }
  if (statement.otherwise != nullptr) {
    add_field(out, first, "otherwise", statement_to_json(*statement.otherwise));
  }
  add_field(out, first, "line", std::to_string(statement.line));
  out << '}';
  return out.str();
}

std::string state_bank_field_to_json(const V2StateBankField& bank) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  add_field(out, first, "name", json_escape(bank.name));
  if (bank.member.has_value()) add_field(out, first, "member", json_escape(*bank.member));
  add_field(out, first, "min", std::to_string(bank.min));
  add_field(out, first, "max", std::to_string(bank.max));
  out << '}';
  return out.str();
}

std::string state_field_to_json(const V2StateField& field) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  add_field(out, first, "kind", json_escape(field.kind));
  add_field(out, first, "name", json_escape(field.name));
  add_field(out, first, "type", json_escape(field.type));
  if (field.bank.has_value()) add_field(out, first, "bank", state_bank_field_to_json(*field.bank));
  if (field.domain.has_value()) add_field(out, first, "domain", json_escape(*field.domain));
  if (field.count.has_value()) add_field(out, first, "count", std::to_string(*field.count));
  if (field.min.has_value()) add_field(out, first, "min", std::to_string(*field.min));
  if (field.max.has_value()) add_field(out, first, "max", std::to_string(*field.max));
  if (field.initial.has_value()) add_field(out, first, "initial", json_escape(*field.initial));
  if (field.initial_stack.has_value())
    add_field(out, first, "initialStack", json_escape(*field.initial_stack));
  add_field(out, first, "line", std::to_string(field.line));
  out << '}';
  return out.str();
}

std::string board_to_json(const V2Board& board) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  add_field(out, first, "kind", json_escape(board.kind));
  add_field(out, first, "name", json_escape(board.name));
  add_field(out, first, "xMin", std::to_string(board.x_min));
  add_field(out, first, "xMax", std::to_string(board.x_max));
  add_field(out, first, "yMin", std::to_string(board.y_min));
  add_field(out, first, "yMax", std::to_string(board.y_max));
  add_field(out, first, "width", std::to_string(board.width));
  add_field(out, first, "height", std::to_string(board.height));
  add_field(out, first, "line", std::to_string(board.line));
  out << '}';
  return out.str();
}

std::string world_to_json(const V2World& world) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  add_field(out, first, "kind", json_escape(world.kind));
  add_field(out, first, "name", json_escape(world.name));
  if (world.position.has_value()) {
    std::ostringstream position;
    bool position_first = true;
    position << '{';
    add_field(position, position_first, "name", json_escape(world.position->name));
    if (world.position->encoding.has_value()) {
      add_field(position, position_first, "encoding", json_escape(*world.position->encoding));
    }
    add_field(position, position_first, "line", std::to_string(world.position->line));
    position << '}';
    add_field(out, first, "position", position.str());
  }
  add_field(out, first, "line", std::to_string(world.line));
  out << '}';
  return out.str();
}

std::string const_to_json(const V2Const& constant) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  add_field(out, first, "kind", json_escape(constant.kind));
  add_field(out, first, "name", json_escape(constant.name));
  add_field(out, first, "expr", json_escape(constant.expr));
  add_field(out, first, "line", std::to_string(constant.line));
  out << '}';
  return out.str();
}

std::string rule_to_json(const V2Rule& rule) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  add_field(out, first, "kind", json_escape(rule.kind));
  add_field(out, first, "name", json_escape(rule.name));
  add_field(out, first, "params", string_array_to_json(rule.params));
  add_field(out, first, "body", statement_array_to_json(rule.body));
  add_field(out, first, "line", std::to_string(rule.line));
  out << '}';
  return out.str();
}

}  // namespace

std::string expression_to_json(const Expression& expression) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  add_field(out, first, "kind", json_escape(expression.kind));
  if (expression.kind == "number") add_field(out, first, "raw", json_escape(expression.raw));
  if (expression.kind == "string") add_field(out, first, "text", json_escape(expression.text));
  if (expression.kind == "identifier") add_field(out, first, "name", json_escape(expression.name));
  if (expression.kind == "indexed") {
    add_field(out, first, "base", json_escape(expression.base));
    if (expression.field.has_value()) add_field(out, first, "field", json_escape(*expression.field));
    add_field(out, first, "index", expression_to_json(*expression.index));
  }
  if (expression.kind == "unary") {
    add_field(out, first, "op", json_escape(expression.op));
    add_field(out, first, "expr", expression_to_json(*expression.expr));
  }
  if (expression.kind == "binary") {
    add_field(out, first, "op", json_escape(expression.op));
    add_field(out, first, "left", expression_to_json(*expression.left));
    add_field(out, first, "right", expression_to_json(*expression.right));
  }
  if (expression.kind == "call") {
    add_field(out, first, "callee", json_escape(expression.callee));
    add_field(out, first, "args",
              json_array(expression.args, [](const Expression& arg) {
                return expression_to_json(arg);
              }));
  }
  out << '}';
  return out.str();
}

std::string v2_program_to_json(const V2Program& program) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  add_field(out, first, "kind", json_escape(program.kind));
  add_field(out, first, "name", json_escape(program.name));
  add_field(out, first, "consts",
            json_array(program.consts, [](const V2Const& constant) {
              return const_to_json(constant);
            }));
  if (program.angle_mode.has_value()) {
    std::ostringstream requirements;
    requirements << "{\"angleMode\":{\"mode\":" << json_escape(program.angle_mode->mode)
                 << ",\"line\":" << program.angle_mode->line << "}}";
    add_field(out, first, "requirements", requirements.str());
  }
  add_field(out, first, "state",
            json_array(program.state, [](const V2StateField& field) {
              return state_field_to_json(field);
            }));
  add_field(out, first, "boards",
            json_array(program.boards, [](const V2Board& board) {
              return board_to_json(board);
            }));
  add_field(out, first, "worlds",
            json_array(program.worlds, [](const V2World& world) {
              return world_to_json(world);
            }));
  add_field(out, first, "body", statement_array_to_json(program.body));
  add_field(out, first, "rules",
            json_array(program.rules, [](const V2Rule& rule) { return rule_to_json(rule); }));
  add_field(out, first, "line", std::to_string(program.line));
  out << '}';
  return out.str();
}

std::string program_to_json(const ProgramAst& program) {
  std::ostringstream out;
  bool first = true;
  out << '{';
  if (program.reference.has_value()) add_field(out, first, "reference", json_escape(*program.reference));
  if (program.v2.has_value()) add_field(out, first, "v2", v2_program_to_json(*program.v2));
  out << '}';
  return out.str();
}

}  // namespace mkpro
