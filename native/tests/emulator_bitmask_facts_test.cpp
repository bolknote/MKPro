#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

const char* kMembershipProgram = R"mkpro(program BitMembership {
  cave: board(1..20, 1..1)

  state {
    marks: cells(cave)
    a: coord(cave)
    b: coord(cave)
    c: coord(cave)
    answer: counter 0..1 = 0
  }

  loop {
    a = read()
    b = read()
    c = read()
    marks += a
    marks += b
    answer = 0
    if c in marks {
      answer = 1
    }
    halt(answer)
  }
})mkpro";

const char* kClearProgram = R"mkpro(program BitClear {
  cave: board(1..20, 1..1)

  state {
    marks: cells(cave)
    a: coord(cave)
    c: coord(cave)
    answer: counter 0..1 = 0
  }

  loop {
    a = read()
    c = read()
    marks += c
    marks -= a
    answer = 0
    if c in marks {
      answer = 1
    }
    halt(answer)
  }
})mkpro";

const char* kNeighborProgram = R"mkpro(program NeighborCount {
  cave: board(1..20, 1..1)

  state {
    marks: cells(cave)
    a: coord(cave)
    b: coord(cave)
    probe: coord(cave)
    answer: counter 0..9 = 0
  }

  loop {
    a = read()
    b = read()
    probe = read()
    marks += a
    marks += b
    answer = neighbor_count(marks, probe)
    halt(answer)
  }
})mkpro";

std::vector<int> step_opcodes(const std::vector<ResolvedStep>& steps) {
  std::vector<int> codes;
  codes.reserve(steps.size());
  for (const ResolvedStep& step : steps)
    codes.push_back(step.opcode);
  return codes;
}

std::string compact_display(std::string text) {
  text.erase(std::remove_if(text.begin(), text.end(),
                            [](unsigned char ch) {
                              return std::isspace(ch) != 0 || ch == ',';
                            }),
             text.end());
  return text;
}

int display_integer(const std::string& display) {
  const std::string compact = compact_display(display);
  require(!compact.empty(), "bitmask probe should display an integer result");
  return std::stoi(compact);
}

void apply_setup_program(emulator::MK61& calc, const CompileResult& result) {
  if (!result.setup_program.has_value())
    return;

  const emulator::ProgramLoadResult loaded =
      calc.load_program(step_opcodes(result.setup_program->steps));
  require(loaded.diagnostics.empty(),
          "bitmask setup program should load without diagnostics");
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult setup_run = calc.run_until_stable(2000, 6);
  require(setup_run.stopped, "bitmask setup program should stop");
}

CompileResult compile_probe_source(const std::string& source) {
  CompileResult result = compile_source(source);
  require(result.implemented, "bitmask probe source should compile");
  require(result.diagnostics.empty(), "bitmask probe source should compile without diagnostics");
  return result;
}

int run_probe(const CompileResult& result, const std::vector<int>& inputs) {
  emulator::MK61 calc;
  apply_setup_program(calc, result);

  const emulator::ProgramLoadResult loaded = calc.load_program(step_opcodes(result.steps));
  require(loaded.diagnostics.empty(), "bitmask probe program should load without diagnostics");

  for (const PreloadReport& preload : result.preloads)
    calc.set_register(preload.register_name, preload.value);

  calc.press_sequence({"В/О", "С/П"});
  for (const int value : inputs) {
    calc.input_number(std::to_string(value));
    calc.press("С/П");
  }
  const emulator::RunResult run = calc.run_until_stable(2000, 6);
  require(run.stopped, "bitmask probe should stop after inputs");
  return display_integer(calc.display_text());
}

void require_probe(const CompileResult& result, const std::vector<int>& inputs, int expected,
                   const std::string& context) {
  const int actual = run_probe(result, inputs);
  require(actual == expected,
          context + " should display " + std::to_string(expected) + ", got " +
              std::to_string(actual));
}

} // namespace

void emulator_bitmask_facts_match_typescript_contract() {
  const CompileResult membership = compile_probe_source(kMembershipProgram);
  const CompileResult clear = compile_probe_source(kClearProgram);
  const CompileResult neighbor = compile_probe_source(kNeighborProgram);

  require_probe(membership, {3, 7, 3}, 1,
                "bit_set then bit_has for first inserted cell");
  require_probe(membership, {3, 7, 7}, 1,
                "bit_set then bit_has for second inserted cell");
  require_probe(membership, {3, 7, 5}, 0,
                "bit_set then bit_has for absent cell");

  require_probe(membership, {4, 5, 4}, 1,
                "membership across first nibble boundary for left cell");
  require_probe(membership, {4, 5, 5}, 1,
                "membership across first nibble boundary for right cell");
  require_probe(membership, {4, 5, 1}, 0,
                "membership across first nibble boundary for absent cell");
  require_probe(membership, {1, 20, 1}, 1,
                "membership across full mask for first cell");
  require_probe(membership, {1, 20, 20}, 1,
                "membership across full mask for last cell");
  require_probe(membership, {1, 20, 16}, 0,
                "membership across full mask for absent interior cell");
  require_probe(membership, {8, 9, 8}, 1,
                "membership across middle nibble boundary for left cell");
  require_probe(membership, {8, 9, 9}, 1,
                "membership across middle nibble boundary for right cell");
  require_probe(membership, {8, 9, 12}, 0,
                "membership across middle nibble boundary for absent cell");

  require_probe(clear, {3, 3}, 0, "bit_clear should remove queried cell");
  require_probe(clear, {7, 3}, 1, "bit_clear should keep absent cleared cell");
  require_probe(clear, {5, 5}, 0, "bit_clear should remove cell five");
  require_probe(clear, {20, 20}, 0, "bit_clear should remove cell twenty");
  require_probe(clear, {1, 20}, 1,
                "bit_clear across nibbles should leave the other cell");

  require_probe(neighbor, {4, 6, 5}, 2,
                "neighbor_count should count both adjacent cells");
  require_probe(neighbor, {4, 9, 5}, 1,
                "neighbor_count should count one adjacent cell");
  require_probe(neighbor, {9, 12, 5}, 0,
                "neighbor_count should count no adjacent cells");
  require_probe(neighbor, {3, 10, 4}, 1,
                "neighbor_count should count left adjacent cell");
}

} // namespace mkpro::tests
