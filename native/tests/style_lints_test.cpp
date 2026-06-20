#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::vector<std::string> show_halt_warnings(const std::string& source) {
  const CompileResult result = compile_source(source);
  std::vector<std::string> warnings;
  for (const Diagnostic& diagnostic : result.diagnostics) {
    require(diagnostic.severity != DiagnosticSeverity::Error,
            "style lint fixture should compile without errors: " + diagnostic.message);
    if (diagnostic.severity == DiagnosticSeverity::Warning &&
        diagnostic.message.find("show(...) immediately followed by halt()") != std::string::npos)
      warnings.push_back(diagnostic.message);
  }
  return warnings;
}

} // namespace

void style_lints_matches_typescript_contract() {
  const std::string show_then_halt = R"mkpro(
program Test {
  state {
    score: counter 0..99 = 0
  }

  loop {
    score += 1
    if score > 10 {
      show(score)
      halt()
    }
  }
}
)mkpro";
  const std::vector<std::string> warnings = show_halt_warnings(show_then_halt);
  require(!warnings.empty(), "show(...) immediately followed by halt() should warn");
  require(warnings.front().find("halt(...)") != std::string::npos,
          "show/halt warning should suggest halt(...)");

  const std::string halt_value = R"mkpro(
program Test {
  state {
    score: counter 0..99 = 0
  }

  loop {
    score += 1
    if score > 10 {
      halt(score)
    }
  }
}
)mkpro";
  require(show_halt_warnings(halt_value).empty(),
          "halt(value) as the terminal screen should stay silent");

  const std::string resumable_prompt = R"mkpro(
program Test {
  state {
    key: packed = 0
    score: counter 0..99 = 0
  }

  loop {
    show(score)
    key = read()
    score += key
  }
}
)mkpro";
  require(show_halt_warnings(resumable_prompt).empty(),
          "show() used as a resumable prompt should stay silent");

  const std::string halt_own_value = R"mkpro(
program Test {
  state {
    score: counter 0..99 = 0
    lives: counter 0..9 = 3
  }

  loop {
    score += 1
    if score > 10 {
      show(lives)
      halt(score)
    }
  }
}
)mkpro";
  require(show_halt_warnings(halt_own_value).empty(),
          "show() followed by halt(value) should stay silent");
}

} // namespace mkpro::tests
