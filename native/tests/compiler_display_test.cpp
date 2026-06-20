#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <string>

namespace mkpro::tests {

void compiler_display_lowering_matches_typescript_contract() {
  const CompileResult result = compile_source(R"mkpro(
program FormattedCoordReport {
  field: board(0..9, 0..9)

  state {
    cell: coord(field)
    bearing: counter 0..9 = 0
  }

  loop {
    cell = read()
    bearing = 3
    show("--", cell:02, "--", bearing)
  }
}
)mkpro");

  require(result.implemented, "native compiler should lower formatted coord reports");
  require(result.diagnostics.empty(), "formatted coord report should not report diagnostics");
  require(result.registers.find("__coord_list_dx") != result.registers.end(),
          "formatted coord report should reserve the TS mask scratch register");
  for (const auto& [name, unused_register] : result.registers) {
    (void)unused_register;
    require(name.find("__display_mask_body_") != 0,
            "formatted coord report should not allocate generic display-mask body scratch");
  }
  require(result.listing.find("setup display literal") != std::string::npos,
          "formatted coord report should preload its verified video mask");
  require(result.listing.find("display formatted mask") != std::string::npos,
          "formatted coord report should recall the preloaded mask");
  require(result.listing.find("show formatted coord report") != std::string::npos,
          "formatted coord report should use the specialized display stop");
}

} // namespace mkpro::tests
