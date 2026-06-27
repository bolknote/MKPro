#include "mkpro/compiler.hpp"
#include "mkpro/core/format.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("cannot read fixture: " + path.string());
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::vector<int> step_opcodes(const std::vector<ResolvedStep>& steps) {
  std::vector<int> codes;
  codes.reserve(steps.size());
  for (const ResolvedStep& step : steps)
    codes.push_back(step.opcode);
  return codes;
}

std::vector<std::filesystem::path> example_files(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> files;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(root / "examples")) {
    if (entry.is_regular_file() && entry.path().extension() == ".mkpro")
      files.push_back(entry.path());
  }
  std::ranges::sort(files);
  return files;
}

std::vector<std::filesystem::path> pending_optimizer_files(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> files;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(root / "examples" / "pending-optimizer")) {
    if (entry.is_regular_file() && entry.path().extension() == ".mkpro")
      files.push_back(entry.path());
  }
  std::ranges::sort(files);
  return files;
}

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::ranges::any_of(result.optimizations, [&](const OptimizationReport& item) {
    return item.name == name;
  });
}

bool diagnostics_contain(const CompileResult& result, const std::string& needle) {
  return std::ranges::any_of(result.diagnostics, [&](const Diagnostic& diagnostic) {
    return diagnostic.message.find(needle) != std::string::npos;
  });
}

std::string compact(std::string text) {
  text.erase(std::remove_if(text.begin(), text.end(),
                            [](unsigned char ch) {
                              return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
                            }),
             text.end());
  return text;
}

bool display_is_number(const std::string& display, const std::string& value) {
  const std::string text = compact(display);
  return text == value || text == value + "," || text == value + ".";
}

int read_integer_register(emulator::MK61& calc, const std::string& register_name) {
  return std::stoi(calc.read_register(register_name));
}

std::string require_register(const CompileResult& result, const std::string& name) {
  const auto it = result.registers.find(name);
  require(it != result.registers.end(), "missing register for " + name);
  return it->second;
}

struct Scenario {
  std::string name;
  std::string example;
  std::map<std::string, std::string> registers;
  std::vector<std::string> keys;
  bool expect_stop = true;
  std::string expect_display_number;
  int max_frames = 400;
};

} // namespace

void emulator_regression_matches_typescript_contract() {
  // Traceability: tests/emulator/regression.test.ts
  const std::filesystem::path root = std::filesystem::current_path();

  {
    const OpcodeInfo* opcode = find_opcode_name("FBx");
    require(opcode != nullptr && opcode->code == 0x0f,
            "Anvarov Latin FBx spelling should parse as F Вx");
  }

  {
    emulator::MK61 calc;
    const emulator::ProgramLoadResult loaded = calc.load_program({0x50, 0x50, 0x50});
    require(loaded.diagnostics.empty(), "consecutive stop program should load");
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(300, 6).stopped, "first consecutive stop should stabilize");
    require(calc.program_counter() == "01",
            "first consecutive stop should leave PC at the next command, got " +
                calc.program_counter());
    calc.press("С/П");
    require(calc.run_until_stable(300, 6).stopped, "second consecutive stop should stabilize");
    require(calc.program_counter() == "02",
            "second consecutive stop should leave PC at the next command, got " +
                calc.program_counter());
  }

  const std::vector<std::filesystem::path> examples = example_files(root);
  require(examples.size() >= 5, "emulator regression should see top-level example files");
  for (const std::filesystem::path& file : examples) {
    const CompileResult result = compile_source(read_text(file));
    require(result.implemented, "example should compile for emulator loading: " + file.string());
    const std::vector<int> codes = step_opcodes(result.steps);
    emulator::MK61 calc;
    const emulator::ProgramLoadResult loaded = calc.load_program(codes);
    require(loaded.diagnostics.empty(),
            "example should load into emulator without diagnostics: " + file.string());
    require(calc.read_program_codes(static_cast<int>(codes.size())) == codes,
            "example program memory should round-trip after load: " + file.string());
  }

  for (const std::filesystem::path& file : pending_optimizer_files(root)) {
    CompileOptions options;
    options.budget = 999;
    const CompileResult result = compile_source(read_text(file), options);
    require(!diagnostics_contain(result, "real rule lowerers before code generation"),
            "pending optimizer should stay before emulator loading: " + file.string());
  }

  const std::vector<Scenario> scenarios = {
      {
          .name = "basic input/show/halt cycle",
          .example = "basic.mkpro",
          .keys = {"В/О", "С/П", "3", "С/П", "4", "С/П", "С/П"},
          .expect_display_number = "7",
      },
      {
          .name = "lunar accepts initial state",
          .example = "lunar.mkpro",
          .registers = {{"2", "100"}, {"3", "500"}, {"4", "5"}},
          .keys = {"В/О", "С/П"},
      },
      {
          .name = "tiny-game boots",
          .example = "tiny-game.mkpro",
          .keys = {"В/О", "С/П"},
      },
      {
          .name = "human boots",
          .example = "human.mkpro",
          .keys = {"В/О", "С/П"},
      },
      {
          .name = "human train path",
          .example = "human.mkpro",
          .registers = {{"1", "0"}, {"2", "5"}},
          .keys = {"В/О", "С/П", "2", "С/П", "С/П"},
      },
      {
          .name = "human spend path",
          .example = "human.mkpro",
          .registers = {{"1", "3"}, {"2", "5"}},
          .keys = {"В/О", "С/П", "8", "С/П", "С/П"},
      },
      {
          .name = "tiny-game drain path",
          .example = "tiny-game.mkpro",
          .registers = {{"0", "80000078"}, {"2", "8"}},
          .keys = {"В/О", "С/П", "4", "С/П", "С/П"},
      },
  };

  for (const Scenario& scenario : scenarios) {
    const CompileResult result =
        compile_source(read_text(root / "examples" / scenario.example));
    require(result.implemented, "scenario should compile: " + scenario.name);
    emulator::MK61 calc;
    const emulator::ProgramLoadResult loaded = calc.load_program(step_opcodes(result.steps));
    require(loaded.diagnostics.empty(), "scenario should load: " + scenario.name);
    for (const auto& [reg, value] : scenario.registers)
      calc.set_register(reg, value);
    calc.press_sequence(scenario.keys);
    const emulator::RunResult run = calc.run_until_stable(scenario.max_frames, 5);
    require(run.stopped == scenario.expect_stop, "scenario stopped state mismatch: " + scenario.name);
    if (!scenario.expect_display_number.empty()) {
      require(display_is_number(calc.display_text(), scenario.expect_display_number),
              "scenario display mismatch: " + scenario.name + ", got " + calc.display_text());
    }
  }

  {
    const std::string source = R"mkpro(
program StackStopRiskProbe {
  state {
    stake_value: counter 0..99 = 2
    fight_entry: counter 0..99 = 0
    result: counter 0..99 = 0
  }

  loop {
    result = robber_fight(stake_value)
    halt(result)
  }

  fn robber_fight(stake) {
    show(stake)
    fight_entry = read()
    return int(stake * (1 + sin(fight_entry)))
  }
}
)mkpro";
    CompileOptions options;
    options.analysis = true;
    options.budget = 999;
    const CompileResult result = compile_source(source, options);
    require(result.implemented, "cave robber stack-stop probe should compile");
    require(has_optimization(result, "show-read-stack-stop-risk-lowering"),
            "cave robber stack-stop probe should report TS optimization");

    auto run_choice = [&](const std::vector<std::string>& keys) {
      emulator::MK61 calc;
      const emulator::ProgramLoadResult loaded = calc.load_program(step_opcodes(result.steps));
      require(loaded.diagnostics.empty(), "cave robber probe should load");
      for (const PreloadReport& preload : result.preloads)
        calc.set_register(preload.register_name, preload.value);
      calc.press_sequence({"В/О", "С/П"});
      require(calc.run_until_stable(500, 6).stopped, "cave robber probe should stop on stake");
      require(display_is_number(calc.display_text(), "2"), "cave robber probe should display stake");
      calc.press_sequence(keys);
      require(calc.run_until_stable(500, 6).stopped, "cave robber probe should stop after choice");
      return calc.display_text();
    };

    require(display_is_number(run_choice({"0", "С/П"}), "2"),
            "cave robber zero choice should keep stake");
    require(display_is_number(run_choice({"В↑", "С/П"}), "3"),
            "cave robber stack choice should avoid fresh random draw");
  }

  {
    const std::string source = R"mkpro(
program StakeCosProbe {
  state {
    stake_value: counter 0..99 = 2
    result: counter 0..99 = 0
  }

  loop {
    result = robber_fight(stake_value)
    halt(result)
  }

  fn robber_fight(stake) {
    show(stake)
    return int(stake * (1 + cos(read())))
  }
}
)mkpro";
    CompileOptions options;
    options.analysis = true;
    options.budget = 999;
    const CompileResult result = compile_source(source, options);
    require(result.implemented, "cos stack-stop probe should compile");
    require(has_optimization(result, "x-param-stack-stop-risk-inline"),
            "cos stack-stop probe should report inline TS optimization");
    require(!has_optimization(result, "x-param-stack-stop-risk-read"),
            "cos stack-stop probe should not report read fallback optimization");

    emulator::MK61 calc;
    const emulator::ProgramLoadResult loaded = calc.load_program(step_opcodes(result.steps));
    require(loaded.diagnostics.empty(), "cos stack-stop probe should load");
    for (const PreloadReport& preload : result.preloads)
      calc.set_register(preload.register_name, preload.value);
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(500, 6).stopped, "cos stack-stop probe should stop on stake");
    require(display_is_number(calc.display_text(), "2"), "cos stack-stop probe should display stake");
    calc.press_sequence({"0", "С/П"});
    require(calc.run_until_stable(500, 6).stopped, "cos stack-stop probe should stop after input");
    require(display_is_number(calc.display_text(), "4"), "cos stack-stop zero input should return 4");
  }

  {
    const std::string source = R"mkpro(
program ResourceUnderflowProbe {
  state {
    food: counter 0..9 = 0
  }

  loop {
    food--
    if food < 0 {
      halt("ЕГГ0Г")
    }
    halt(food)
  }
}
)mkpro";
    CompileOptions options;
    options.analysis = true;
    options.budget = 999;
    const CompileResult result = compile_source(source, options);
    require(result.implemented, "resource underflow probe should compile");
    require(has_optimization(result, "decrement-underflow-domain-guard"),
            "resource underflow probe should report TS optimization");
    const std::string food_register = require_register(result, "food");

    auto run_with_food = [&](const std::string& food) {
      emulator::MK61 calc;
      const emulator::ProgramLoadResult loaded = calc.load_program(step_opcodes(result.steps));
      require(loaded.diagnostics.empty(), "resource underflow probe should load");
      calc.set_register(food_register, food);
      calc.press_sequence({"В/О", "С/П"});
      require(calc.run_until_stable(500, 6).stopped, "resource underflow probe should stop");
      return calc.display_text();
    };

    require(display_is_number(run_with_food("2"), "1"),
            "resource underflow should decrement positive food");
    require(compact(run_with_food("0")).find("ЕГГ") != std::string::npos,
            "resource underflow should stop with error after exhaustion");
  }

  {
    CompileOptions options;
    options.analysis = true;
    options.budget = 999;
    const CompileResult result =
        compile_source(read_text(root / "examples" / "wumpus.mkpro"), options);
    require(result.implemented, "wumpus setup regression should compile");
    for (const auto& [name, allocated_register] : result.registers) {
      (void)allocated_register;
      require(!name.starts_with("__const_"),
              "wumpus public register report should not expose internal constant " + name);
    }
    require(result.setup_program.has_value(), "wumpus should expose setup program");
    require(result.steps.size() <= 105, "wumpus main program should fit");

    emulator::MK61 calc;
    const emulator::ProgramLoadResult setup_loaded =
        calc.load_program(step_opcodes(result.setup_program->steps));
    require(setup_loaded.diagnostics.empty(), "wumpus setup should load");
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(1000, 8).stopped, "wumpus setup should stop");

    for (const std::string& field :
         {"wumpus", "hazard_pit_1", "hazard_pit_2", "hazard_bat_1", "hazard_bat_2"}) {
      const int value = read_integer_register(calc, require_register(result, field));
      require(value >= 1, "wumpus setup field should be >= 1: " + field);
      require(value <= 20, "wumpus setup field should be <= 20: " + field);
    }

    const emulator::ProgramLoadResult main_loaded = calc.load_program(step_opcodes(result.steps));
    require(main_loaded.diagnostics.empty(), "wumpus main program should load after setup");
  }

  {
    CompileOptions options;
    options.analysis = true;
    options.budget = 999;
    const CompileResult result =
        compile_source(read_text(root / "examples" / "wumpus.mkpro"), options);
    require(result.implemented, "wumpus arrow regression should compile");

    emulator::MK61 calc;
    const emulator::ProgramLoadResult loaded = calc.load_program(step_opcodes(result.steps));
    require(loaded.diagnostics.empty(), "wumpus arrow regression should load");

    const std::string room_register = require_register(result, "room");
    const std::string target_register = require_register(result, "target");
    const std::string arrows_register = require_register(result, "arrows");
    const std::string clue_register = require_register(result, "clue");
    const std::string wumpus_register = require_register(result, "wumpus");
    const std::string pit_1_register = require_register(result, "hazard_pit_1");
    const std::string pit_2_register = require_register(result, "hazard_pit_2");
    const std::string bat_1_register = require_register(result, "hazard_bat_1");
    const std::string bat_2_register = require_register(result, "hazard_bat_2");

    const std::set<std::string> state_registers = {
        room_register,   target_register, arrows_register, clue_register,  wumpus_register,
        pit_1_register,  pit_2_register,  bat_1_register,  bat_2_register,
    };
    for (const PreloadReport& preload : result.preloads) {
      if (!state_registers.contains(preload.register_name))
        calc.set_register(preload.register_name, preload.value);
    }

    calc.set_register(room_register, "1");
    calc.set_register(target_register, "0");
    calc.set_register(arrows_register, "5");
    calc.set_register(clue_register, "0");
    calc.set_register(wumpus_register, "10");
    calc.set_register(pit_1_register, "15");
    calc.set_register(pit_2_register, "16");
    calc.set_register(bat_1_register, "18");
    calc.set_register(bat_2_register, "19");

    auto shoot_miss = [&]() {
      calc.press("С/П");
      (void)calc.run_until_stable(800, 6);
      calc.input_number("5", true);
      calc.press("/-/");
      calc.press("С/П");
      (void)calc.run_until_stable(1500, 6);
      return compact(calc.display_text());
    };

    calc.press_sequence({"В/О", "С/П"});
    (void)calc.run_until_stable(800, 6);
    require(calc.program_counter() == "32",
            "wumpus should start on the room/arrows stop before clue, got pc=" +
                calc.program_counter() + " display=" + compact(calc.display_text()));

    bool died = false;
    for (int shot = 1; shot <= 6 && !died; ++shot) {
      const std::string display = shoot_miss();
      if (display.find("Г") != std::string::npos) {
        require(shot == 5, "wumpus should die on the fifth missed arrow, got shot " +
                               std::to_string(shot) + " with display " + display +
                               ", arrows=" + compact(calc.read_register(arrows_register)) +
                               ", room=" + compact(calc.read_register(room_register)) +
                               ", wumpus=" + compact(calc.read_register(wumpus_register)));
        died = true;
      }
    }
    require(died, "wumpus arrow exhaustion should eventually kill the player");
  }
}

} // namespace mkpro::tests
