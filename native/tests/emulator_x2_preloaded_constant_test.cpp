#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <map>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

struct X2PreloadRun {
  std::string signature;
  std::string x;
};

X2PreloadRun run_x2_preload_fixture(
    const std::vector<int>& codes,
    const std::map<std::string, std::string>& registers = {}) {
  emulator::MK61 calc;
  const emulator::ProgramLoadResult loaded = calc.load_program(codes);
  require(loaded.diagnostics.empty(), "X2 preload fixture should load");
  for (const auto& [name, value] : registers)
    calc.set_register(name, value);
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult run = calc.run_until_stable(300, 5);
  return X2PreloadRun{.signature = run.signature, .x = calc.read_register("x")};
}

} // namespace

void emulator_x2_preloaded_constant_matches_source_contract() {
  const std::map<std::string, std::string> preload = {{"1", "2"}};
  const X2PreloadRun recall = run_x2_preload_fixture(
      {0x61, 0x0e, 0x0e, 0x0e, 0x14, 0x14, 0x61, 0x50}, preload);
  const X2PreloadRun dot = run_x2_preload_fixture(
      {0x61, 0x0e, 0x0e, 0x0e, 0x14, 0x14, 0x0a, 0x50}, preload);
  require(recall.signature == dot.signature,
          "dot restore must preserve the full stopped-machine state for a proved preload");

  const std::map<std::string, std::string> indirect = {{"1", "2"}, {"8", "1"}};
  const X2PreloadRun after_indirect_store = run_x2_preload_fixture(
      {0x03, 0x0e, 0x61, 0x14, 0xb8, 0x54, 0x61, 0x50}, indirect);
  const X2PreloadRun stale_dot = run_x2_preload_fixture(
      {0x03, 0x0e, 0x61, 0x14, 0xb8, 0x54, 0x0a, 0x50}, indirect);
  require(after_indirect_store.x != stale_dot.x,
          "unknown indirect store must demonstrate why the recall cannot become dot");
}

} // namespace mkpro::tests
