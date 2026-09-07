#include "mkpro/compiler.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>

namespace mkpro::tests {

namespace {

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) { return item.name == name; });
}

bool has_step_comment(const CompileResult& result, const std::string& comment) {
  return std::any_of(result.steps.begin(), result.steps.end(),
                     [&](const ResolvedStep& step) { return step.comment == comment; });
}

bool has_early_set_step(const CompileResult& result) {
  return std::any_of(result.steps.begin(), result.steps.end(), [](const ResolvedStep& step) {
    return step.comment == "set step" && step.address < 10;
  });
}

void check_caller_lifetime(const std::string& name, const std::string& source,
                           const std::vector<std::string>& inputs,
                           const std::string& expected, bool should_reuse) {
  for (const bool reuse : {false, true}) {
    CompileOptions options;
    options.analysis = true;
    options.budget = 105;
    options.disable_return_suffix_gadget = true;
    options.dead_source_residual_temp_reuse = reuse;
    const CompileResult result = compile_source(source, options);
    require(result.implemented && result.steps.size() <= 105,
            name + ": lifetime fixture must fit the physical calculator");
    for (const Diagnostic& diagnostic : result.diagnostics)
      require(diagnostic.severity != DiagnosticSeverity::Error,
              name + ": " + diagnostic.message);
    require(has_optimization(result, "dead-source-residual-temp-reuse") ==
                (reuse && should_reuse),
            name + ": source/target live-out must govern temporary reuse");
    std::vector<int> bytes;
    for (const ResolvedStep& step : result.steps)
      bytes.push_back(step.opcode);
    emulator::MK61 calculator;
    require(calculator.load_program(bytes).diagnostics.empty(), name + ": image must load");
    for (const PreloadReport& preload : result.preloads)
      calculator.set_register(preload.register_name, preload.value);
    calculator.press_sequence({"В/О", "С/П"});
    require(calculator.run_until_stable(12000, 8).stopped, name + ": first input must stop");
    for (const std::string& input : inputs) {
      calculator.set_register("X", input);
      calculator.press_sequence({"С/П"});
      require(calculator.run_until_stable(12000, 8).stopped, name + ": execution must stop");
    }
    require(calculator.read_register("X") == expected,
            name + (reuse ? " optimized" : " baseline") +
                ": wrong observable result: " + calculator.read_register("X"));
  }
}

} // namespace

void residual_temp_matches_typescript_contract() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999999;
  options.dead_source_residual_temp_reuse = true;

  const CompileResult result = compile_source(R"mkpro(
program ResidualTemp {
  state {
    command: packed = 0
    step: packed = 0
  }

  loop {
    command = read()
    step = command - 5

    unless step {
      halt(0)
    }
    else {
      step = int(1 / step)

      unless step {
        step = sign(command - 5)
        halt(step)
      }
      else {
        halt(step)
      }
    }
  }
}
)mkpro",
                                      options);

  require(result.implemented, "residual temp program should compile");
  for (const Diagnostic& diagnostic : result.diagnostics) {
    require(diagnostic.severity != DiagnosticSeverity::Error,
            "residual temp program should not report errors: " + diagnostic.message);
  }
  require(has_optimization(result, "dead-source-residual-temp-reuse"),
          "residual temp program should report dead-source-residual-temp-reuse");
  require(has_step_comment(result, "set command"),
          "residual temp program should store the residual in command");
  require(!has_early_set_step(result),
          "residual temp program should not materialize step before address 10");

  check_caller_lifetime("counter across repeated calls", R"mkpro(
program CounterContinuation {
  state {
    counter: packed = 8
    residual: packed = 0
    total: packed = 0
  }
  fn tick() {
    counter--
    residual = frac(counter / 2)
    if residual != 0 { total += 1 }
  }
  counter = read()
  tick()
  tick()
  halt(counter * 10 + total)
}
)mkpro", {"8"}, "61,", false);

  check_caller_lifetime("caller overwrites before every read", R"mkpro(
program OverwrittenContinuation {
  state {
    counter: packed = 8
    residual: packed = 0
    total: packed = 0
  }
  fn sample() {
    residual = counter - 3
    if residual != 0 { total += residual }
  }
  counter = read()
  sample()
  counter = read()
  sample()
  halt(total)
}
)mkpro", {"8", "6"}, "8,", true);

  check_caller_lifetime("residual escapes to caller", R"mkpro(
program ResidualContinuation {
  state {
    counter: packed = 8
    residual: packed = 0
    total: packed = 0
  }
  fn sample() {
    residual = counter - 3
    if residual != 0 { total += residual }
  }
  counter = read()
  sample()
  halt(residual + total)
}
)mkpro", {"8"}, "10,", false);

  check_caller_lifetime("implicit state has caller lifetimes", R"mkpro(
program ImplicitContinuation {
  state { total: packed = 0 }
  fn sample() {
    residual = counter - 3
    if residual != 0 { total += residual }
  }
  counter = read()
  sample()
  counter = read()
  sample()
  halt(total)
}
)mkpro", {"8", "6"}, "8,", true);

  check_caller_lifetime("branch rejoins a live source", R"mkpro(
program BranchContinuation {
  state {
    counter: packed = 8
    residual: packed = 0
    total: packed = 0
  }
  counter = read()
  if counter != 0 {
    residual = counter - 3
    if residual != 0 { total += residual }
  }
  halt(counter * 10 + total)
}
)mkpro", {"8"}, "85,", false);

  check_caller_lifetime("loop continuation updates a live source", R"mkpro(
program LoopContinuation {
  state {
    counter: packed = 8
    residual: packed = 0
    total: packed = 0
  }
  counter = read()
  while counter > 0 {
    residual = frac(counter / 2)
    if residual != 0 { total += 1 }
    counter--
  }
  halt(total)
}
)mkpro", {"8"}, "4,", false);

  check_caller_lifetime("conditional callee write is not a kill", R"mkpro(
program ConditionalWrite {
  state {
    counter: packed = 8
    residual: packed = 0
    total: packed = 0
    choice: packed = 0
  }
  fn maybe_reset() { if choice != 0 { counter = 4 } }
  counter = read()
  choice = read()
  residual = counter - 3
  if residual != 0 { total += residual }
  maybe_reset()
  halt(counter * 10 + total)
}
)mkpro", {"8", "0"}, "85,", false);
}

} // namespace mkpro::tests
