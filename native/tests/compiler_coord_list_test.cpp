#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <string>

namespace mkpro::tests {

void compiler_coord_list_lowering_matches_typescript_contract() {
  const CompileResult result = compile_source(R"mkpro(
program CoordListLineCountAssignment {
  field: board(0..9, 0..9)

  state {
    cell: coord(field)
    foxes: coord_list(field, 3) = random_unique()
    bearing: counter 0..9 = 0
  }

  loop {
    cell = read()
    bearing = line_count(foxes, cell)
    halt(bearing)
  }
}
)mkpro");

  require(result.implemented, "native compiler should lower coord_list line_count assignment");
  require(result.diagnostics.empty(),
          "coord_list line_count assignment should not report diagnostics");
  require(result.registers.find("__coord_list_total") == result.registers.end(),
          "coord_list line_count assignment should accumulate in the target register");
  require(result.registers.find("__coord_list_dx") != result.registers.end(),
          "random_unique coord_list support should still reserve TS-compatible scratch");
  require(result.listing.find("coord_list pointer") != std::string::npos,
          "coord_list line_count assignment should initialize the indirect pointer");
  require(result.listing.find("coord_list candidate") != std::string::npos,
          "coord_list line_count assignment should recall candidates indirectly");
  require(result.listing.find("coord_list line_count loop") != std::string::npos,
          "coord_list line_count assignment should loop through the coord_list");
  require(result.listing.find("coord_list line_count result") != std::string::npos,
          "coord_list line_count assignment should leave the target value in X");

  const CompileResult remove = compile_source(R"mkpro(
program CoordListRemove {
  river: board(0..9, 1..1)

  state {
    shot: coord(river) = 0
    fleet: coord_list(river, 3) = random_unique()
    left: counter 0..3 = 3
  }

  loop {
    shot = read()
    if shot in fleet {
      fleet -= shot
      left--
    }
    halt(left)
  }
}
)mkpro");

  require(remove.implemented, "native compiler should lower coord_list remove updates");
  require(remove.diagnostics.empty(), "coord_list remove update should not report diagnostics");
  require(remove.registers.find("fleet") == remove.registers.end(),
          "coord_list collection should not allocate a scalar register");
  require(remove.registers.find("river") == remove.registers.end(),
          "board names should not allocate scalar registers");
  require(remove.listing.find("coord_list remove candidate") != std::string::npos,
          "coord_list remove should scan candidates indirectly");
  require(remove.listing.find("coord_list remove current") != std::string::npos,
          "coord_list remove should mark the matched item through indirect memory");

  const CompileResult random_board = compile_source(R"mkpro(
program RandomBoardCoordinate {
  river: board(0..9, 1..1)

  state {
    shot: coord(river) = 0
  }

  loop {
    shot = random(river)
    halt(shot)
  }
}
)mkpro");

  require(random_board.implemented, "native compiler should lower random(board)");
  require(random_board.diagnostics.empty(), "random(board) should not report diagnostics");
  require(random_board.registers.find("river") == random_board.registers.end(),
          "random(board) should not allocate the board as a variable");
  require(random_board.listing.find("random int floor") != std::string::npos,
          "random(board) should lower through the TS random integer pattern");
}

} // namespace mkpro::tests
