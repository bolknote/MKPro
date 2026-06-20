#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mkpro {

struct Expression;
using ExpressionPtr = std::shared_ptr<Expression>;

struct Expression {
  std::string kind;
  std::string raw;
  std::string text;
  std::string name;
  std::string base;
  std::optional<std::string> field;
  std::string op;
  std::string callee;
  ExpressionPtr index;
  ExpressionPtr expr;
  ExpressionPtr left;
  ExpressionPtr right;
  std::vector<Expression> args;
};

struct DisplayItem {
  std::string kind;
  std::string text;
  std::string name;
  std::optional<Expression> expr;
  std::optional<int> width;
  std::optional<std::string> pad;
  int line = 0;
};

struct V2ProgramRequirement {
  std::string mode;
  int line = 0;
};

struct V2Const {
  std::string kind = "v2_const";
  std::string name;
  std::string expr;
  int line = 0;
};

struct V2StateBankField {
  std::string name;
  std::optional<std::string> member;
  int min = 0;
  int max = 0;
};

struct V2StateField {
  std::string kind = "v2_state_field";
  std::string name;
  std::string type;
  std::optional<V2StateBankField> bank;
  std::optional<std::string> domain;
  std::optional<int> count;
  std::optional<int> min;
  std::optional<int> max;
  std::optional<std::string> initial;
  std::optional<std::string> initial_stack;
  int line = 0;
};

struct V2Board {
  std::string kind = "v2_board";
  std::string name;
  int x_min = 0;
  int x_max = 0;
  int y_min = 0;
  int y_max = 0;
  int width = 0;
  int height = 0;
  int line = 0;
};

struct V2WorldPosition {
  std::string name;
  std::optional<std::string> encoding;
  int line = 0;
};

struct V2World {
  std::string kind = "v2_world";
  std::string name;
  std::optional<V2WorldPosition> position;
  int line = 0;
};

struct V2Predicate {
  std::string kind;
  std::string left;
  std::string op;
  std::string right;
  std::string collection;
  std::string item;
};

struct V2RawInput {
  std::string slot;
  std::string expr;
  int line = 0;
};

struct V2RawOutput {
  std::string slot = "X";
  std::string target;
  int line = 0;
};

struct RawBlockLine {
  std::string text;
  int line = 0;
};

struct V2Statement;
using V2StatementPtr = std::shared_ptr<V2Statement>;

struct V2MatchCase {
  std::vector<std::string> values;
  V2StatementPtr action;
  int line = 0;
};

struct V2Statement {
  std::string kind;
  std::optional<std::string> target;
  std::optional<std::string> expr;
  std::optional<std::string> op;
  std::optional<std::string> name;
  std::vector<std::string> args;
  std::optional<V2Predicate> predicate;
  bool negated = false;
  std::vector<V2Statement> body;
  std::vector<V2Statement> then_body;
  std::vector<V2Statement> else_body;
  std::optional<std::vector<DisplayItem>> items;
  std::vector<V2RawInput> inputs;
  std::vector<V2RawOutput> outputs;
  std::vector<std::string> clobbers;
  std::vector<std::string> preserves;
  std::vector<RawBlockLine> lines;
  std::vector<V2MatchCase> cases;
  V2StatementPtr otherwise;
  int line = 0;
  bool has_else_body = false;
};

struct V2Rule {
  std::string kind = "v2_rule";
  std::string name;
  std::vector<std::string> params;
  std::vector<V2Statement> body;
  int line = 0;
};

struct V2Program {
  std::string kind = "v2_program";
  std::string name;
  std::vector<V2Const> consts;
  std::optional<V2ProgramRequirement> angle_mode;
  std::vector<V2StateField> state;
  std::vector<V2Board> boards;
  std::vector<V2World> worlds;
  std::vector<V2Statement> body;
  std::vector<V2Rule> rules;
  int line = 0;
};

struct ProgramAst {
  std::optional<std::string> reference;
  std::optional<V2Program> v2;
};

std::string expression_to_json(const Expression& expression);
std::string program_to_json(const ProgramAst& program);
std::string v2_program_to_json(const V2Program& program);

}  // namespace mkpro
