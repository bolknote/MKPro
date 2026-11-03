#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/raw_bcd_word.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;

bool carrier_decoder(const passes::PassResult& result) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(), [](const auto& r) {
    return r.name == "finite-mode-exit-carrier-decoder";
  });
}

std::vector<MachineItem> fixture(int reg, bool last_x = false, bool unknown = false,
                               bool boundary = true) {
  MachineEmitter e;
  e.emit_op(0x6b); e.emit_jump(0x57, "choose sign", "negative");
  e.emit_number("1"); if (unknown) e.emit_op(0x6b);
  e.emit_jump(0x53, "worker", "worker"); e.emit_jump(0x51, "done", "done");
  e.emit_label("negative"); e.emit_number("-1"); e.emit_jump(0x53, "worker", "worker");
  e.emit_label("done"); e.emit_op(0x6b); e.emit_op(0x40 + reg);
  for (int r : {11, 12, 13, 14, 11}) e.emit_op(0x60 + r);
  e.emit_op(0x10); e.emit_stop(StopDisposition::Terminal);
  e.emit_label("worker"); e.emit_op(0x40 + reg);
  for (int n = 0; n != 3; ++n) {
    e.emit_op(0x6b); e.emit_op(0x60 + reg); e.emit_number("10");
    e.emit_op(0x13); e.emit_op(0x12); e.emit_op(0x4a);
  }
  e.emit_op(0x6c); e.emit_number("2"); e.emit_op(0x10); e.emit_op(0x4a);
  e.emit_op(0x60 + reg); e.emit_jump(0x57, "nonzero finite row", "return");
  e.emit_op(0x44);
  if (last_x) { e.emit_op(0x0f); e.emit_op(0x46); }
  if (boundary) {
    e.emit_op(0x6b); e.emit_number("2"); e.emit_op(0x10);
    e.emit_op(0x60 + reg); e.emit_op(0x36); e.emit_op(0x40 + reg); e.emit_op(0x45);
  }
  e.emit_label("return"); e.emit_op(0x52);
  return e.items;
}

std::vector<std::string> observe(const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>& preloads, const std::string& input,
    const std::vector<std::string>& registers) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "recovery fixture resolves");
  std::vector<int> code;
  for (const auto& step : resolved.steps) code.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.command_limit() == 105 && calc.load_program(code).diagnostics.empty(),
          "recovery fixture fits stock ROM without Rf");
  for (int i = 0; i < 15; ++i)
    calc.set_register(std::string(1, "0123456789abcde"[i]), std::to_string(i + 2));
  for (const auto& word : preloads) calc.set_register(word.register_name, word.value);
  calc.set_register("b", input);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(6000, 8).stopped, "recovery fixture stops");
  std::vector<std::string> result;
  for (const auto* name : {"X", "Y", "Z", "T", "X1"}) result.push_back(calc.read_register(name));
  for (const auto& name : registers) result.push_back(calc.read_register(name));
  calc.press("."); result.push_back(calc.display_text(true));
  return result;
}

void ordinary_recovery() {
  for (const int reg : {2, 8}) {
    CompileOptions options; options.defer_return_suffix_until_callee_hole = true;
    const auto before = fixture(reg);
    const auto candidates = passes::finite_mode_representation_candidates(
        raise_machine_to_ir(before), {options}, {.maximum_candidates = 32});
    const auto found = std::find_if(candidates.begin(), candidates.end(), carrier_decoder);
    require(found != candidates.end(), "an opaque max must admit exact in-place carrier recovery");
    const auto after = lower_ir_to_machine(found->ops);
    require(after.size() < before.size(), "all decoder/store commands must leave a positive net saving");
    std::vector<std::string> registers;
    for (char c : std::string("0123456789abcde")) registers.emplace_back(1, c);
    for (const char* input : {"-3", "-2", "0", "7", "12345678"})
      require(observe(before, {}, input, registers) == observe(after, {}, input, registers),
              "both finite rows and zero-sensitive max preserve all physical observations");
    for (const auto& rejected : {fixture(reg, true), fixture(reg, false, true),
                                fixture(reg, false, false, false)}) {
      const auto invalid = passes::finite_mode_representation_candidates(
          raise_machine_to_ir(rejected), {options}, {.maximum_candidates = 32});
      require(std::none_of(invalid.begin(), invalid.end(), carrier_decoder),
              "live last-X, unknown producers and dead carriers cannot acquire a carrier decoder");
    }
  }
}

void raw_opaque_recovery() {
  MachineEmitter e;
  e.emit_op(0x4a); // Establish a fresh literal-entry boundary for the seed.
  e.emit_number("1"); e.emit_op(0x48); e.emit_op(0x68); e.emit_op(0x0b); e.emit_op(0x48);
  for (int n = 1; n <= 3; ++n) {
    e.emit_number(std::to_string(n)); e.emit_op(0x15); e.emit_op(0x68);
    e.emit_number("10"); e.emit_op(0x13); e.emit_op(0x12); e.emit_op(0x43 + n);
  }
  e.emit_op(0x6b); e.emit_number("2"); e.emit_op(0x10); e.emit_op(0x4a);
  for (int n = 0; n != 4; ++n) e.emit_op(0x6b);
  e.emit_op(0x68); e.emit_op(0x36); e.emit_op(0x48); e.emit_op(0x47);
  for (int r : {11, 12, 13, 14, 11}) e.emit_op(0x60 + r);
  e.emit_op(0x10); e.emit_stop(StopDisposition::Terminal);
  std::vector<PreloadReport> preloads{{.register_name = "b", .value = "0"},
      {.register_name = "c", .value = "2"}, {.register_name = "d", .value = "3"},
      {.register_name = "e", .value = "4"}};
  CompileOptions options; options.defer_return_suffix_until_callee_hole = true;
  const auto candidates = passes::deferred_finite_mode_representation_candidates(
      raise_machine_to_ir(e.items), {options}, {.maximum_growth_cells = 4,
          .maximum_candidates = 32, .allow_target_relayout = true, .runtime_preloads = &preloads});
  require(std::none_of(candidates.begin(), candidates.end(), [](const auto& candidate) {
    return std::any_of(candidate.transformation.preloads.begin(), candidate.transformation.preloads.end(),
        [](const auto& p) { return decode_raw_single_hex_word(p.value).has_value(); });
  }), "ordinary exit decoding cannot admit raw words across an opaque Kmax boundary");
}
}

int main() {
  try {
    ordinary_recovery(); raw_opaque_recovery();
    std::cout << "finite carrier recovery compiler and ROM contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
