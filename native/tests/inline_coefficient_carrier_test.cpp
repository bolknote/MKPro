#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/passes/finite_mode_representation.hpp"
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
  for (const int code : codes)
    items.push_back(MachineItem::op(code, "inline carrier fixture"));
}
void target(std::vector<MachineItem>& items, int code, const std::string& label) {
  append(items, {code});
  items.push_back(MachineItem::address(label));
}
void finish(std::vector<MachineItem>& items) {
  append(items, {0x6b, 0x6c, 0x6d, 0x6e, 0x6b, 0x10, 0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
}
enum class Variant { Normal, UnknownLeft, UnknownPhase, LiveZ, LastX, Alias,
                     MixedJoin, Resume, SharedLiteral, SideEntry };

std::vector<MachineItem> fixture(Variant variant = Variant::Normal, int reg = 8) {
  std::vector<MachineItem> items;
  append(items, {0x45, variant == Variant::UnknownPhase ? 0x6b : 1});
  if (variant == Variant::SharedLiteral)
    append(items, {0x46});
  append(items, {0x40 + reg, 2, 0x40, 0x49});
  if (variant == Variant::SideEntry)
    target(items, 0x57, "product-middle");
  items.push_back(MachineItem::label("loop"));
  for (int product = 0; product < 3; ++product) {
    if (variant == Variant::UnknownLeft)
      append(items, {0x6b});
    else
      append(items, {0x69, 0x15});
    append(items, {0x60 + reg, 1});
    if (product == 0 && variant == Variant::SideEntry)
      items.push_back(MachineItem::label("product-middle"));
    append(items, {0, 0x13, 0x12, 0x4a});
    if (product == 0 && variant == Variant::LiveZ)
      append(items, {0x25, 0x25, 0x46});
    if (product == 0 && variant == Variant::LastX)
      append(items, {0x0f, 0x46});
    if (product == 0 && variant == Variant::Alias) {
      append(items, {0xd7});
      items.back().indirect_memory_targets = std::vector<int>{reg};
    }
    if (product == 0 && variant == Variant::Resume)
      append(items, {0x50});
  }
  append(items, {0x60 + reg, 0x0b, 0x40 + reg});
  target(items, 0x5d, "loop");
  append(items, {0x60 + reg, 0x44});
  if (variant == Variant::MixedJoin) {
    append(items, {0x6b});
    items.push_back(MachineItem::label("mixed-read"));
    append(items, {0x60 + reg, 0x33, 0x40 + reg});
    target(items, 0x57, "mixed-read");
  } else {
    append(items, {0x6b, 0x40 + reg});
  }
  append(items, {0x60 + reg, 0x46});
  finish(items);
  return items;
}

std::vector<MachineItem> calls_fixture(bool nonunit = false) {
  std::vector<MachineItem> items;
  append(items, {0x45, nonunit ? 2 : 1});
  target(items, 0x53, "init");
  append(items, {nonunit ? 4 : 1});
  if (!nonunit)
    append(items, {0x0b});
  target(items, 0x53, "init");
  append(items, {0x6b, 0x48, 0x68, 0x46});
  finish(items);
  items.push_back(MachineItem::label("init"));
  append(items, {0x48});
  target(items, 0x53, "body");
  append(items, {0x52});
  items.push_back(MachineItem::label("body"));
  for (int product = 0; product < 3; ++product) {
    append(items, {0x6b, 0x68, nonunit ? 2 : 1});
    if (!nonunit)
      append(items, {0});
    append(items, {0x13, 0x12, 0x4a});
  }
  append(items, {0x68, 0x44, 0x52});
  return items;
}

int cells(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(),
      [](const auto& item) { return item.kind != MachineItemKind::Label; }));
}

std::vector<passes::PassResult> candidates(const std::vector<MachineItem>& items,
                                         const std::vector<PreloadReport>* preloads) {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  return passes::finite_mode_representation_candidates(raise_machine_to_ir(items), {options},
      {.maximum_candidates = 32, .runtime_preloads = preloads});
}

bool inline_carrier(const passes::PassResult& result, bool raw = false) {
  const std::string name = raw ? "raw-inline-coefficient-state-carrier"
                              : "inline-coefficient-state-carrier";
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
      [&](const auto& report) { return report.name == name; });
}

std::vector<std::string> observe(const std::vector<MachineItem>& items,
                                 const std::vector<PreloadReport>& preloads,
                                 const std::vector<PreloadReport>& ignore,
                                 const std::string& input) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "inline fixture must resolve");
  std::vector<int> codes;
  for (const auto& step : resolved.steps)
    codes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(codes).diagnostics.empty(), "inline fixture must fit stock memory");
  constexpr std::array<const char*, 15> names{
      "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"};
  for (std::size_t i = 0; i < names.size(); ++i)
    calc.set_register(names[i], std::to_string(i + 2U));
  calc.set_register("b", input);
  for (const auto& preload : preloads)
    calc.set_register(preload.register_name, preload.value);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(15000, 8).stopped, "inline fixture must stop");
  std::vector<std::string> result{calc.display_text(true)};
  calc.press(".");
  result.push_back(calc.display_text(true));
  for (const char* name : {"X", "Y", "Z", "T", "X1"})
    result.push_back(calc.read_register(name));
  for (const char* name : names) {
    if (std::any_of(ignore.begin(), ignore.end(),
          [&](const auto& preload) { return preload.register_name == name; }))
      continue;
    // The dynamic epoch overwrites the carrier before this observation.
    // Unlike a terminal-state-only test, this checks the carrier register too.
    result.push_back(calc.read_register(name));
  }
  return result;
}

void equivalent(const std::vector<MachineItem>& before, const passes::PassResult& result) {
  const auto after = lower_ir_to_machine(result.ops);
  require(build_post_layout_control_flow(after).proved, "rewritten CFG must prove");
  for (const std::string input : {"-7", "0", "12345678"})
    require(observe(before, {}, result.preloads, input) ==
                observe(after, result.preloads, result.preloads, input),
            "inline fusion must preserve registers, later dynamic epochs and hidden stack state");
}

void positive_and_sizes() {
  const std::vector<PreloadReport> preloads;
  for (const int reg : {8, 2}) {
    const auto source = fixture(Variant::Normal, reg);
    require(cells(source) == 48, "inline original baseline");
    const auto results = candidates(source, &preloads);
    for (const int kind : {0, 1, 2}) {
      const auto found = std::find_if(results.begin(), results.end(), [&](const auto& result) {
        return inline_carrier(result, kind == 2) &&
               result.preloads.empty() == (kind == 0);
      });
      require(found != results.end(), "literal, preload and raw inline alternatives must exist");
      require(cells(lower_ir_to_machine(found->ops)) == (kind == 0 ? 42 : 40),
              "inline coefficient size baseline");
      equivalent(source, *found);
    }
    const auto no_setup = candidates(source, nullptr);
    require(std::any_of(no_setup.begin(), no_setup.end(), [](const auto& result) {
              return inline_carrier(result) && result.preloads.empty();
            }), "an inline coefficient must not require a second register");
    require(std::none_of(no_setup.begin(), no_setup.end(), [](const auto& result) {
              return inline_carrier(result, true);
            }), "unknown preload environment cannot allocate a raw seed");
  }
  for (const bool nonunit : {false, true}) {
    const auto source = calls_fixture(nonunit);
    const auto results = candidates(source, nullptr);
    const auto found = std::find_if(results.begin(), results.end(),
        [](const auto& result) { return inline_carrier(result); });
    require(found != results.end(), "finite inputs through nested calls must be recoded jointly");
    require(cells(lower_ir_to_machine(found->ops)) < cells(source),
            "nested-call inline representation must actually save cells");
    equivalent(source, *found);
  }
}

void negative_contracts() {
  const std::vector<PreloadReport> preloads;
  for (const auto variant : {Variant::UnknownPhase, Variant::LiveZ, Variant::Alias,
                            Variant::MixedJoin, Variant::Resume, Variant::SharedLiteral,
                            Variant::SideEntry}) {
    const auto results = candidates(fixture(variant), &preloads);
    require(std::none_of(results.begin(), results.end(), [](const auto& result) {
              return inline_carrier(result) || inline_carrier(result, true);
            }), "unsafe inline web must reject: " + std::to_string(static_cast<int>(variant)));
  }
  for (const auto variant : {Variant::UnknownLeft, Variant::LastX}) {
    const auto source = fixture(variant);
    const auto results = candidates(source, &preloads);
    require(std::none_of(results.begin(), results.end(),
                [](const auto& result) { return inline_carrier(result, true); }),
            "raw encoding requires a proved left domain and dead raw last-X");
    const auto ordinary = std::find_if(results.begin(), results.end(),
        [](const auto& result) { return inline_carrier(result); });
    require(ordinary != results.end(), "ordinary inline product keeps exact coefficient and last-X");
    equivalent(source, *ordinary);
  }
  std::vector<PreloadReport> occupied;
  for (const char name : std::string{"0123456789abcde"})
    occupied.push_back({.register_name = std::string(1, name), .value = "7"});
  const auto full = candidates(fixture(), &occupied);
  require(std::any_of(full.begin(), full.end(), [](const auto& result) {
            return inline_carrier(result) && result.preloads.empty();
          }), "ordinary inline fusion still works with all registers occupied");
  for (const auto& result : full)
    if (inline_carrier(result) || inline_carrier(result, true))
      require(result.preloads.empty(), "no original preload may be replaced");
}

void production_frontier() {
  const LayoutSearchArtifact root{.items = fixture()};
  const auto first = expand_joint_representation_transactions(root, {});
  const auto repeat = expand_joint_representation_transactions(root, {});
  require(first.size() == repeat.size(), "inline frontier size is deterministic");
  bool found = false;
  for (std::size_t i = 0; i < first.size(); ++i) {
    require(same_layout_search_artifact(first[i].node, repeat[i].node),
            "inline code, setup and reports must be deterministic");
    for (const auto& report : first[i].node.optimizations)
      if (report.name == "inline-coefficient-state-carrier" ||
          report.name == "raw-inline-coefficient-state-carrier") {
        require(cells(first[i].node.items) < cells(root.items),
                "inline representation must reach the real compiler frontier");
        found = true;
      }
  }
  require(found, "production search must consume the inline representation");
}
} // namespace

int main() {
  try {
    positive_and_sizes();
    negative_contracts();
    production_frontier();
    std::cout << "inline coefficient/state carrier compiler and ROM contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
