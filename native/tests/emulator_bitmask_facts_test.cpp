#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <utility>
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


std::string packed_hex_digit(unsigned digit) {
  static const std::vector<std::string> digits{
      "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
      "-", "L", "\xD0\xA1", "\xD0\x93", "\xD0\x95", "_"};
  return digits.at(digit);
}

std::string repeated_packed_digit(unsigned digit) {
  std::string value = "8.";
  for (int position = 0; position < 7; ++position)
    value += packed_hex_digit(digit);
  return value;
}

void require_intersection_clear_nibble_fact() {
  // Native K AND/K XOR retain Y and normalize their leading digit to 8.
  // Check every nibble pair in all seven payload positions, against an
  // independently computed Boolean result as well as the unfused operations.
  for (unsigned left = 0; left < 16; ++left) {
    for (unsigned mask = 0; mask < 16; ++mask) {
      emulator::MK61 calc;
      const auto loaded = calc.load_program({
          0x61, 0x62, 0x3a, 0x37, 0x43,
          0x61, 0x62, 0x37, 0x39, 0x44, 0x50});
      require(loaded.diagnostics.empty(), "intersection-clear nibble fact must load");
      calc.set_register("1", repeated_packed_digit(left));
      calc.set_register("2", repeated_packed_digit(mask));
      calc.set_register("5", repeated_packed_digit(left & (~mask & 15U)));
      calc.press_sequence({"В/О", "С/П"});
      require(calc.run_until_stable(2000, 6).stopped,
              "intersection-clear nibble fact must stop");
      const std::string direct = calc.read_register("3");
      require(direct == calc.read_register("5"),
              "native clear must match the nibble truth table for " +
                  std::to_string(left) + "/" + std::to_string(mask));
      require(direct == calc.read_register("4"),
              "tested intersection XOR must equal native complement AND for " +
                  std::to_string(left) + "/" + std::to_string(mask));
    }
  }
}

std::string run_native_guarded_clear(const std::string& value, const std::string& mask) {
  emulator::MK61 calc;
  // Test the full native AND. Only the true continuation recomputes the
  // original expression, without relying on the optimizer's retained operand.
  const auto loaded = calc.load_program({
      0x61, 0x62, 0x37, 0x57, 0x10,
      0x61, 0x62, 0x3a, 0x37, 0x41, 0x61, 0x50});
  require(loaded.diagnostics.empty(), "unfused guarded clear must load");
  calc.set_register("1", value);
  calc.set_register("2", mask);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(2000, 6).stopped, "unfused guarded clear must stop");
  return calc.read_register("1");
}

void require_intersection_clear_compiler_contract() {
  const std::string source = R"mkpro(
program ConditionalIntersection {
  state {
    flags: packed
    probe: packed
  }
  loop {
    if bit_and(flags, probe) != 0 {
      flags = bit_and(flags, bit_not(probe))
    }
    halt(flags)
  }
}
)mkpro";
  CompileOptions options;
  options.analysis = true;
  options.budget = 999;
  options.disable_candidate_search = true;
  const auto has_reuse = [](const CompileResult& result) {
    return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                       [](const auto& optimization) {
                         return optimization.name == "membership-clear-intersection-reuse";
                       });
  };
  const CompileResult result = compile_source(source, options);
  require(result.implemented && result.diagnostics.empty(),
          "guarded intersection reuse must compile cleanly");
  require(has_reuse(result), "guarded clear must reuse the tested intersection");
  require(std::count_if(result.steps.begin(), result.steps.end(),
                        [](const auto& step) { return step.opcode == 0x39; }) == 1,
          "guarded clear must use one native XOR");
  require(std::none_of(result.steps.begin(), result.steps.end(),
                       [](const auto& step) {
                         return step.opcode == 0x3a || step.opcode == 0x0a;
                       }),
          "guarded clear must need neither complement nor hidden X2 restoration");

  for (const auto& values : std::vector<std::pair<std::string, std::string>>{
           {"0", "0.0000008"}, {"3.1415926", "0.0000008"},
           {"8.0000008", "0.0000008"}, {"88888834", "0.0000008"},
           {"1", "1"}, {"1.2345678", "8.8888888"},
           {"-3.1415926", "0.0000008"}, {"3.1415926", "-0.0000008"},
           {"0.0000001", "0.0000001"}, {"12345678", "23456789"},
           {"1e-20", "7e-20"}, {"7.7777777", "0"}}) {
    emulator::MK61 calc;
    apply_setup_program(calc, result);
    const auto loaded = calc.load_program(step_opcodes(result.steps));
    require(loaded.diagnostics.empty(), "compiled guarded clear must load");
    for (const auto& preload : result.preloads)
      calc.set_register(preload.register_name, preload.value);
    calc.set_register(result.registers.at("flags"), values.first);
    calc.set_register(result.registers.at("probe"), values.second);
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(2000, 6).stopped, "compiled guarded clear must stop");
    require(calc.read_register(result.registers.at("flags")) ==
                run_native_guarded_clear(values.first, values.second),
            "compiled clear must preserve the full native predicate and update for " +
                values.first + "/" + values.second);
  }

  for (const auto& replacement : std::vector<std::pair<std::string, std::string>>{
           {"bit_and(flags, probe) != 0", "bit_and(flags, probe) == 0"},
           {"bit_not(probe)", "bit_not(flags)"}}) {
    std::string negative = source;
    negative.replace(negative.find(replacement.first), replacement.first.size(),
                     replacement.second);
    const CompileResult rejected = compile_source(negative, options);
    require(rejected.implemented && rejected.diagnostics.empty(),
            "nonmatching conditional clear must still compile");
    require(!has_reuse(rejected),
            "changed predicate or clear mask must not reuse an unrelated intersection");
  }
}

} // namespace

void emulator_bitmask_facts_match_typescript_contract() {
  require_intersection_clear_nibble_fact();
  require_intersection_clear_compiler_contract();
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
