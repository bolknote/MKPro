#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;

void append(std::vector<MachineItem>& items, std::initializer_list<int> codes) {
  for (const int code : codes) items.push_back(MachineItem::op(code, "exit decoder fixture"));
}
void target(std::vector<MachineItem>& items, int code, const std::string& label) {
  append(items, {code}); items.push_back(MachineItem::address(label));
}
int cells(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(), [](const auto& item) {
    return item.kind != MachineItemKind::Label;
  }));
}
bool decoded_exit(const passes::PassResult& result) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(), [](const auto& r) {
    return r.name == "finite-mode-exit-observation-decoder";
  });
}
std::vector<MachineItem> fixture(int carrier, bool last_x = false,
                                bool unknown = false, bool taken = false,
                                bool alias = false, bool resumable = false,
                                bool digit_after_predicate = false) {
  std::vector<MachineItem> items;
  append(items, {0x6b});
  target(items, 0x57, "negative");
  append(items, {0x4e, unknown ? 0x6b : 1});
  target(items, 0x53, "worker");
  target(items, 0x51, "done");
  items.push_back(MachineItem::label("negative"));
  append(items, {0x4e, 1, 0x0b});
  target(items, 0x53, "worker");
  items.push_back(MachineItem::label("done"));
  if (alias) {
    append(items, {0xd7});
    items.back().indirect_memory_targets = std::vector<int>{carrier};
  }
  append(items, {0x6b, 0x40 + carrier, 0x6b, 0x6c, 0x6d, 0x6e, 0x6b, 0x10, 0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
  items.push_back(MachineItem::label("worker"));
  append(items, {0x40 + carrier});
  for (int product = 0; product < 3; ++product)
    append(items, {0x6b, 0x60 + carrier, 1, 0, 0x13, 0x12, 0x4a});
  // Make last-X observably different from either coefficient. A sign decoder
  // must not claim to preserve it merely because its numerical X is correct.
  append(items, {0x6c, 2, 0x10, 0x4a, 0x60 + carrier});
  target(items, 0x5c, taken ? "consume" : "return");
  if (taken) append(items, {0x52});
  items.push_back(MachineItem::label("consume"));
  if (digit_after_predicate) append(items, {0});
  append(items, {0x44});
  if (last_x) append(items, {0x0f, 0x45});
  if (resumable) {
    append(items, {0x50}); items.back().stop_disposition = StopDisposition::Resumable;
  }
  append(items, {0x52});
  items.push_back(MachineItem::label("return"));
  append(items, {0x52});
  return items;
}
std::vector<passes::PassResult> candidates(const std::vector<MachineItem>& items) {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  return passes::finite_mode_representation_candidates(
      raise_machine_to_ir(items), {options}, {.maximum_candidates = 32});
}
std::vector<std::string> observe(const std::vector<MachineItem>& items,
                                 const std::string& input, int carrier) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "exit decoder addresses resolve");
  std::vector<int> codes;
  for (const auto& step : resolved.steps) codes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(codes).diagnostics.empty(), "exit decoder fits stock memory");
  constexpr std::array<const char*, 15> names{
      "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"};
  for (std::size_t i = 0; i < names.size(); ++i)
    calc.set_register(names[i], std::to_string(i + 2U));
  calc.set_register("7", std::to_string(carrier));
  calc.set_register("b", input);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(6000, 8).stopped, "exit decoder reaches its exact caller stop");
  std::vector<std::string> output{calc.display_text(true)};
  calc.press("."); output.push_back(calc.display_text(true));
  for (const auto* name : {"X", "Y", "Z", "T", "X1"}) output.push_back(calc.read_register(name));
  for (const auto* name : names) output.push_back(calc.read_register(name));
  return output;
}
void run() {
  // A VP digit is not an entry closer. Without a following original store,
  // literal 0 extends the order and changes X instead of entering zero.
  const auto entry_fact = [](bool encoded, bool store) {
    std::vector<int> code{0x68, 0x57, 9};
    if (encoded) { code.push_back(0x0c); code.push_back(1); }
    if (store) code.push_back(0x44);
    code.push_back(0); code.push_back(0x50);
    while (code.size() < 10U) code.push_back(0x50);
    emulator::MK61 calc;
    require(calc.load_program(code).diagnostics.empty(), "VP entry fact loads");
    calc.set_register("8", encoded ? "-0.1" : "-1");
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(1000, 8).stopped, "VP entry fact stops");
    std::vector<std::string> values{calc.display_text(true)};
    calc.press("."); values.push_back(calc.display_text(true));
    for (const char* name : {"X", "Y", "Z", "T", "X1", "4"})
      values.push_back(calc.read_register(name));
    return values;
  };
  require(entry_fact(false, false) != entry_fact(true, false),
          "ROM must expose the open-exponent counterexample");
  require(entry_fact(false, true) == entry_fact(true, true),
          "an original store must close entry and preserve stack/X1/X2");
  for (const int carrier : {2, 8}) {
    for (const bool last_x : {false, true}) {
      const auto before = fixture(carrier, last_x);
      const auto choices = candidates(before);
      const auto found = std::find_if(choices.begin(), choices.end(), decoded_exit);
      require(found != choices.end(), "a finite observation must not require restoring its carrier");
      const auto after = lower_ir_to_machine(found->ops);
      require(cells(before) == (last_x ? 57 : 55) &&
                  cells(after) == (last_x ? 54 : 51),
              "finite observation size baseline changed: " + std::to_string(cells(before)) +
                  " -> " + std::to_string(cells(after)));
      require(found->preloads.empty() && build_post_layout_control_flow(after).proved,
              "decoding must not invent setup or lose the exact call/return graph");
      for (const char* input : {"0", "-2", "7", "12345678"})
        require(observe(before, input, carrier) == observe(after, input, carrier),
                "decoded X must preserve bank, stack, X1, X2 and caller result");
      const auto again = candidates(before);
      const auto repeated = std::find_if(again.begin(), again.end(), decoded_exit);
      require(repeated != again.end() && cells(lower_ir_to_machine(repeated->ops)) == cells(after),
              "decoder selection must be deterministic");
    }
    for (const auto& unsupported : {
        fixture(carrier, false, true), fixture(carrier, false, false, true),
        fixture(carrier, false, false, false, true),
        fixture(carrier, false, false, false, false, true),
        fixture(carrier, true, false, false, false, false, true)}) {
      const auto choices = candidates(unsupported);
      require(std::none_of(choices.begin(), choices.end(), decoded_exit),
              "unknown rows, taken shadows, memory aliases, resumes and open exponent entry reject");
    }
  }
}
} // namespace
int main() {
  try { run(); std::cout << "finite exit observation compiler and ROM contracts passed\n"; return 0; }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
