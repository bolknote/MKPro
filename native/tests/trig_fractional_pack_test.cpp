#include "mkpro/compiler.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>

namespace mkpro::tests {

namespace {

bool has_optimization(const CompileResult& result, const std::string& name) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
                     [&](const OptimizationReport& item) { return item.name == name; });
}

}  // namespace

void trig_fractional_pack_matches_strategy_contract() {
  CompileOptions options;
  options.analysis = true;
  options.budget = 999;
  options.trig_fractional_pack = true;
  options.trig_fractional_pack_names = {"score", "arrows"};

  const CompileResult packed = compile_source(R"mkpro(
program TrigFractionalPack {
  state {
    expected_mode_only("grd")
    score: counter 0..99 = 12
    arrows: counter 0..5 = 3
  }

  loop {
    arrows++
    score++
    if arrows >= 4 {
      halt(arrows)
    }
    else {
      halt(score)
    }
  }
}
)mkpro",
                                             options);

  require(packed.implemented, "trig fractional pack candidate should compile");
  require(has_optimization(packed, "trig-fractional-pack"),
          "forced compatible counters should report trig-fractional-pack");
  require(packed.registers.contains("__trig_packed_counter_0"),
          "trig fractional pack should allocate hidden packed register");
  require(!packed.registers.contains("score"), "trig fractional pack should remove major state");
  require(!packed.registers.contains("arrows"), "trig fractional pack should remove minor state");

  const CompileResult degree_packed = compile_source(R"mkpro(
program DegreeTrigFractionalPack {
  state {
    expected_mode_only("deg")
    score: counter 0..99 = 12
    arrows: counter 0..5 = 3
  }

  loop {
    arrows++
    halt(arrows)
  }
}
)mkpro",
                                                    options);

  require(degree_packed.implemented, "degree trig fractional pack candidate should compile");
  require(has_optimization(degree_packed, "trig-fractional-pack"),
          "expected_mode_only(\"deg\") should also enable trig fractional packing");

  CompileOptions loose_options = options;
  const CompileResult loose = compile_source(R"mkpro(
program LooseModeTrigFractionalPack {
  state {
    expected_mode("grd")
    score: counter 0..99 = 12
    arrows: counter 0..5 = 3
  }

  loop {
    arrows++
    halt(arrows)
  }
}
)mkpro",
                                            loose_options);

  require(loose.implemented, "loose expected mode program should still compile");
  require(!has_optimization(loose, "trig-fractional-pack"),
          "expected_mode without expected_mode_only must not enable trig fractional packing");
}

}  // namespace mkpro::tests
