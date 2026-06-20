#include "mkpro/compiler.hpp"
#include "mkpro/core/parser.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

CompileResult compile_budget_999(const std::string& source) {
  CompileOptions options;
  options.budget = 999;
  return compile_source(source, options);
}

void require_clean_compile(const CompileResult& result, const std::string& context) {
  require(result.implemented, context + " should compile");
  for (const Diagnostic& diagnostic : result.diagnostics) {
    require(diagnostic.severity != DiagnosticSeverity::Error,
            context + " should not report errors: " + diagnostic.message);
  }
}

void require_parse_throws_contains(const std::string& source, const std::string& fragment) {
  try {
    (void)parse_program(source);
  } catch (const ParseError& error) {
    require(std::string(error.what()).find(fragment) != std::string::npos,
            "parse error should contain '" + fragment + "', got '" + error.what() + "'");
    return;
  }
  require(false, "parse_program should throw containing '" + fragment + "'");
}

std::vector<std::string> first_match_values(const std::vector<V2Statement>& statements) {
  for (const V2Statement& statement : statements) {
    if (statement.kind == "v2_match") {
      std::vector<std::string> values;
      for (const V2MatchCase& match_case : statement.cases) {
        values.insert(values.end(), match_case.values.begin(), match_case.values.end());
      }
      return values;
    }
    if (statement.kind == "v2_loop" || statement.kind == "v2_while") {
      std::vector<std::string> nested = first_match_values(statement.body);
      if (!nested.empty())
        return nested;
    }
    if (statement.kind == "v2_if") {
      std::vector<V2Statement> branches = statement.then_body;
      branches.insert(branches.end(), statement.else_body.begin(), statement.else_body.end());
      std::vector<std::string> nested = first_match_values(branches);
      if (!nested.empty())
        return nested;
    }
  }
  return {};
}

bool contains_all_values(const std::vector<std::string>& values,
                         const std::vector<std::string>& expected) {
  return std::all_of(expected.begin(), expected.end(), [&](const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
  });
}

} // namespace

void match_blocks_match_typescript_contract() {
  const std::string block_source = R"mkpro(
program Test {
  state {
    key: packed = 0
    score: counter 0..99 = 0
    lives: counter 0..9 = 3
  }

  loop {
    show(score)
    key = read()
    match key {
      1 => {
        score += 10
        lives -= 1
      }
      2 => score += 1
      otherwise => lives -= 1
    }
  }
}
)mkpro";

  const std::string helper_source = R"mkpro(
program Test {
  state {
    key: packed = 0
    score: counter 0..99 = 0
    lives: counter 0..9 = 3
  }

  fn big_win() {
    score += 10
    lives -= 1
  }

  loop {
    show(score)
    key = read()
    match key {
      1 => big_win()
      2 => score += 1
      otherwise => lives -= 1
    }
  }
}
)mkpro";

  const CompileResult block = compile_budget_999(block_source);
  const CompileResult helper = compile_budget_999(helper_source);
  require_clean_compile(block, "match block action");
  require_clean_compile(helper, "match helper action");
  require(block.steps.size() <= helper.steps.size(),
          "block match action should not be more expensive than helper idiom");

  require_clean_compile(compile_budget_999(R"mkpro(
program Test {
  state {
    key: packed = 0
    score: counter 0..99 = 0
  }

  loop {
    show(score)
    key = read()
    match key {
      1 => {
        if score > 50 {
          score = 0
        }
        else {
          score += 5
        }
      }
      otherwise => score += 1
    }
  }
}
)mkpro"),
                        "nested control flow in match block");

  require_clean_compile(compile_budget_999(R"mkpro(
program Test {
  state {
    key: packed = 0
    score: counter 0..99 = 0
    lives: counter 0..9 = 3
  }

  loop {
    show(score)
    key = read()
    match key {
      1 => score += 1
      otherwise => {
        lives -= 1
        score = 0
      }
    }
  }
}
)mkpro"),
                        "otherwise match block");

  require_clean_compile(compile_budget_999(R"mkpro(
program Test {
  state {
    key: packed = 0
    score: counter 0..99 = 0
  }

  fn bonus(k) {
    match k {
      1 => {
        return 10
      }
      otherwise => return 1
    }
  }

  loop {
    show(score)
    key = read()
    score += bonus(key)
  }
}
)mkpro"),
                        "return inside match block case");

  const ProgramAst range_program = parse_program(R"mkpro(
program Test {
  key = read()
  match key {
    1..3 => score += 1
    4, 6..7 => score += 2
    otherwise => score = 0
  }
  show(score)
}
)mkpro");
  std::vector<std::string> values = first_match_values(range_program.v2->body);
  require(contains_all_values(values, {"1", "2", "3", "4", "6", "7"}),
          "match ranges should expand into explicit case values");
  require_clean_compile(compile_budget_999(R"mkpro(
program Test {
  key = read()
  match key {
    1..3 => score += 1
    4, 6..7 => score += 2
    otherwise => score = 0
  }
  show(score)
}
)mkpro"),
                        "match case ranges");

  require_parse_throws_contains(R"mkpro(
program Test {
  key = read()
  match key {
    3..1 => score += 1
    otherwise => score = 0
  }
  show(score)
}
)mkpro",
                                "must be ascending");

  require_clean_compile(compile_budget_999(R"mkpro(
program Test {
  state {
    key: packed = 0
    score: counter 0..99 = 0
    lives: counter 0..9 = 3
  }

  loop {
    show(score)
    key = read()
    match key {
      1..3 => {
        score += 1
        lives -= 1
      }
      otherwise => score = 0
    }
  }
}
)mkpro"),
                        "range match block body");
}

} // namespace mkpro::tests
