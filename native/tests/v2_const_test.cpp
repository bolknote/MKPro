#include "mkpro/core/parser.hpp"
#include "mkpro/core/v2_const.hpp"
#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <map>
#include <algorithm>
#include <string>
#include <vector>

namespace mkpro::tests {

void v2_const_matches_typescript_contract() {
  const ProgramAst parsed = parse_program(R"mkpro(
program Demo {
  const CAP = 10000
  const TWICE = CAP * 2
  state {
    score: counter 0..9 = 0
  }
  loop {
    halt(score)
  }
}
)mkpro");
  require(parsed.v2.has_value(), "const declaration program should parse as V2");
  require(parsed.v2->consts.size() == 2, "program-level const declarations should parse");
  require(parsed.v2->consts.at(0).kind == "v2_const", "first const kind mismatch");
  require(parsed.v2->consts.at(0).name == "CAP", "first const name mismatch");
  require(parsed.v2->consts.at(0).expr == "10000", "first const expression mismatch");
  require(parsed.v2->consts.at(0).line == 3, "first const line mismatch");
  require(parsed.v2->consts.at(1).kind == "v2_const", "second const kind mismatch");
  require(parsed.v2->consts.at(1).name == "TWICE", "second const name mismatch");
  require(parsed.v2->consts.at(1).expr == "CAP * 2", "second const expression mismatch");
  require(parsed.v2->consts.at(1).line == 4, "second const line mismatch");

  const ProgramAst program = parse_program(R"mkpro(
program ConstValues {
  const LIMIT = 3
  const DOUBLE = LIMIT * 2

  state {
    total: counter 0..9 = 0
  }

  loop {
    total = DOUBLE
    halt(total)
  }
}
)mkpro");
  require(program.v2.has_value(), "test program should parse as V2");
  std::map<std::string, Expression> constants;
  std::vector<Diagnostic> diagnostics;
  core::index_v2_constants(*program.v2, constants, diagnostics);

  require(diagnostics.empty(), "valid const declarations should not report diagnostics");
  require(constants.contains("LIMIT"), "LIMIT const should be indexed");
  require(constants.contains("DOUBLE"), "DOUBLE const should be indexed");
  require(constants.at("DOUBLE").kind == "number", "DOUBLE should fold to a number");
  require(constants.at("DOUBLE").raw == "6", "DOUBLE should fold through earlier consts");

  const ProgramAst bad_assignment = parse_program(R"mkpro(
program BadConstAssignment {
  const LIMIT = 3

  fn bad() {
    if LIMIT >= 1 {
      LIMIT = 4
    }
  }

  loop {
    bad()
  }
}
)mkpro");
  constants.clear();
  diagnostics.clear();
  core::index_v2_constants(*bad_assignment.v2, constants, diagnostics);
  require(!diagnostics.empty(), "nested const assignment should report diagnostics");
  require(diagnostics.at(0).message.find("Cannot assign to const 'LIMIT'") != std::string::npos,
          "nested const assignment diagnostic should mention const name");

  V2Program duplicate;
  duplicate.name = "DuplicateConst";
  duplicate.consts.push_back(V2Const{.name = "A", .expr = "1", .line = 2});
  duplicate.consts.push_back(V2Const{.name = "A", .expr = "2", .line = 3});
  constants.clear();
  diagnostics.clear();
  core::index_v2_constants(duplicate, constants, diagnostics);
  require(!diagnostics.empty(), "duplicate const should report diagnostics");
  require(diagnostics.at(0).message.find("Duplicate const 'A'") != std::string::npos,
          "duplicate const diagnostic should mention const name");

  const ProgramAst shadowing = parse_program(R"mkpro(
program Bad {
  state { cap: counter 0..9 = 0 }
  const cap = 1
  loop { halt(0) }
}
)mkpro");
  constants.clear();
  diagnostics.clear();
  core::index_v2_constants(*shadowing.v2, constants, diagnostics);
  require(!diagnostics.empty(), "const shadowing state should report diagnostics");
  require(diagnostics.at(0).message.find("shadows state") != std::string::npos,
          "const shadowing diagnostic should mention state shadowing");

  const ProgramAst non_numeric = parse_program(R"mkpro(
program Bad {
  const x = random()
  state { n: counter 0..9 = 0 }
  loop { halt(0) }
}
)mkpro");
  constants.clear();
  diagnostics.clear();
  core::index_v2_constants(*non_numeric.v2, constants, diagnostics);
  require(!diagnostics.empty(), "non-numeric const expression should report diagnostics");
  require(diagnostics.at(0).message.find("compile-time number expression") != std::string::npos,
          "non-numeric const diagnostic should mention compile-time number expression");

  const CompileResult folded = compile_source(R"mkpro(
program Caps {
  const CAP = 10000
  const DOUBLE = CAP * 2
  state {
    x: counter 0..9 = 0
  }
  loop {
    x = int(DOUBLE / 10000)
    halt(x)
  }
}
)mkpro");
  require(folded.implemented, "dependent const program should compile");
  require(std::any_of(folded.optimizations.begin(), folded.optimizations.end(),
                      [](const OptimizationReport& item) { return item.name == "const-inline"; }),
          "dependent const program should report const-inline");
  require(std::any_of(folded.optimizations.begin(), folded.optimizations.end(),
                      [](const OptimizationReport& item) {
                        return item.name == "expression-constant-folder";
                      }),
          "dependent const program should report expression-constant-folder");
}

}  // namespace mkpro::tests
