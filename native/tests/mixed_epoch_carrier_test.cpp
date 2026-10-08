#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
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
    items.push_back(MachineItem::op(code, "mixed epoch fixture"));
}
void target(std::vector<MachineItem>& items, int code, const std::string& label) {
  append(items, {code});
  items.push_back(MachineItem::address(label));
}
void finish(std::vector<MachineItem>& items) {
  append(items, {0x6b, 0x6c, 0x6d, 0x6e, 0x6b, 0x10, 0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
}
enum class Variant { Normal, Unknown, UnknownJoin, Zero, Negative, TooSmall,
                     Overflow, Alias, LastX, LiveZ, Resume, MixedProduct };

std::vector<MachineItem> fixture(Variant variant = Variant::Normal,
                               int reg = 8, int seed = 1, int iterations = 2) {
  std::vector<MachineItem> items;
  append(items, {0x45, seed, 0x40 + reg});
  items.push_back(MachineItem::label("products"));
  for (int product = 0; product < 3; ++product) {
    append(items, {0x6b, 0x60 + reg, 1, 0, 0x13, 0x12, 0x4a});
    if (product == 0 && variant == Variant::LiveZ)
      append(items, {0x25, 0x25, 0x47});
  }
  append(items, {iterations, 0x40});
  items.push_back(MachineItem::label("again"));
  append(items, {0x6b});
  target(items, 0x57, "high");
  append(items, {0x46, variant == Variant::UnknownJoin ? 0x6b : 3});
  target(items, 0x51, "chosen");
  items.push_back(MachineItem::label("high"));
  append(items, {0x46, 7});
  items.push_back(MachineItem::label("chosen"));
  append(items, {0x49});
  if (variant == Variant::Unknown)
    append(items, {0x6b, 0x49});
  if (variant == Variant::Zero || variant == Variant::TooSmall)
    append(items, {variant == Variant::Zero ? 0 : 1, 0x49});
  if (variant == Variant::Overflow)
    append(items, {9, 0, 0, 0, 0, 0, 0, 0, 0x49});
  if (variant == Variant::Alias) {
    append(items, {0xb7});
    items.back().indirect_memory_targets = std::vector<int>{9};
  }
  if (variant == Variant::Resume) {
    append(items, {0x50});
    items.back().stop_disposition = StopDisposition::Resumable;
  }
  target(items, 0x53, "score");
  if (variant == Variant::Negative)
    append(items, {0x0b});
  items.push_back(MachineItem::label("boundary"));
  append(items, {0x60 + reg, 0x36, 0x40 + reg});
  if (variant == Variant::LastX)
    append(items, {0x0f, 0x47});
  target(items, 0x5d, variant == Variant::MixedProduct ? "products" : "again");
  append(items, {0x60 + reg, 0x44});
  finish(items);
  items.push_back(MachineItem::label("score"));
  target(items, 0x53, "square");
  append(items, {0x52});
  items.push_back(MachineItem::label("square"));
  append(items, {0x69, 0x22, 0x52});
  return items;
}

int cells(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(),
      [](const auto& item) { return item.kind != MachineItemKind::Label; }));
}
std::vector<passes::PassResult> candidates(const std::vector<MachineItem>& items) {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  return passes::finite_mode_representation_candidates(
      raise_machine_to_ir(items), {options}, {.maximum_candidates = 32});
}
bool mixed(const passes::PassResult& result) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
      [](const auto& report) { return report.name == "mixed-epoch-coefficient-state-carrier"; });
}
std::vector<std::string> observe(const std::vector<MachineItem>& items,
                                 const std::string& input) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "mixed fixture must resolve");
  std::vector<int> codes;
  for (const auto& step : resolved.steps)
    codes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(codes).diagnostics.empty(), "mixed fixture must fit ROM memory");
  constexpr std::array<const char*, 15> names{
      "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"};
  for (std::size_t i = 0; i < names.size(); ++i)
    calc.set_register(names[i], std::to_string(i + 2U));
  calc.set_register("b", input);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(15000, 8).stopped, "mixed fixture must stop");
  std::vector<std::string> values{calc.display_text(true)};
  calc.press(".");
  values.push_back(calc.display_text(true));
  for (const char* name : {"X", "Y", "Z", "T", "X1"})
    values.push_back(calc.read_register(name));
  for (const char* name : names)
    values.push_back(calc.read_register(name));
  return values;
}

void positive_compiler_rom_and_sizes() {
  for (const int reg : {2, 8})
    for (const int seed : {1, 4})
      for (const int iterations : {1, 2}) {
        const auto source = fixture(Variant::Normal, reg, seed, iterations);
        const auto results = candidates(source);
        const auto found = std::find_if(results.begin(), results.end(), mixed);
        require(found != results.end(), "max must separate finite and dynamic carrier epochs");
        const auto after = lower_ir_to_machine(found->ops);
        require(cells(source) == 58 && cells(after) == 51,
                "mixed-epoch baseline must shrink 58 to 51 without a new register: " +
                    std::to_string(cells(source)) + " -> " + std::to_string(cells(after)));
        require(found->preloads.empty(), "mixed ordinary carrier needs no new setup");
        require(build_post_layout_control_flow(after).proved, "final mixed CFG must prove");
        for (const std::string input : {"-7", "0", "13"})
          require(observe(source, input) == observe(after, input),
                  "mixed epochs must preserve all registers, stack, X1 and decimal-entry state");
      }
}

void negative_contracts() {
  for (const auto variant : {Variant::Unknown, Variant::UnknownJoin, Variant::Zero,
                            Variant::Negative, Variant::TooSmall, Variant::Overflow,
                            Variant::Alias, Variant::LastX, Variant::LiveZ,
                            Variant::Resume, Variant::MixedProduct}) {
    const auto results = candidates(fixture(variant));
    require(std::none_of(results.begin(), results.end(), mixed),
            "unsafe mixed boundary must reject: " + std::to_string(static_cast<int>(variant)));
  }
}

std::size_t boundary_item(const std::vector<MachineItem>& items) {
  const auto found = std::find_if(items.begin(), items.end(),
      [](const auto& item) { return item.kind == MachineItemKind::Label && item.name == "boundary"; });
  require(found != items.end(), "range fixture boundary must exist");
  return static_cast<std::size_t>(found - items.begin()) + 1U;
}

void range_contracts() {
  require(bound_ordinary_decimal("-0.1") == OrdinaryNumericRange{-1, 0, false} &&
              bound_ordinary_decimal("0.1") == OrdinaryNumericRange{0, 1, false} &&
              bound_ordinary_decimal("17") == OrdinaryNumericRange{17, 17, true},
          "ordinary decimal enclosure is exact and does not turn fractional endpoints integral");
  for (const char* word : {"A-2", "1e200", "-0", "100000000", ""})
    require(!bound_ordinary_decimal(word), "raw/oversized/unsupported words have no ordinary proof");
  const auto source = fixture();
  const auto flow = build_post_layout_control_flow(source);
  const auto entry = analyze_post_layout_automatic_entry_lift(source, flow, 100000);
  require(entry.has_value(), "range fixture entry facts");
  const auto requested = boundary_item(source);
  const auto proof = prove_post_layout_numeric_ranges(source, flow, *entry, {requested});
  require(!proof.exhausted && proof.x_before.at(requested) == OrdinaryNumericRange{9, 49, true},
          "bounds must join unknown branches and follow both nested returns");
  const auto limited = prove_post_layout_numeric_ranges(source, flow, *entry, {requested}, nullptr, 1);
  require(limited.exhausted && limited.x_before.empty(), "budget exhaustion must not export partial facts");
  const auto again = prove_post_layout_numeric_ranges(source, flow, *entry, {requested});
  require(proof.x_before == again.x_before && proof.work == again.work,
          "range proof work and results must be deterministic");

  // There is no definition of R9 after the main entry. A main-only preload
  // proves it, but a resume entry must invalidate the same fact.
  for (const bool resume : {false, true}) {
    std::vector<MachineItem> items;
    append(items, {0x45});
    if (resume) {
      append(items, {0x50});
      items.back().stop_disposition = StopDisposition::Resumable;
    }
    append(items, {0x69});
    items.push_back(MachineItem::label("boundary"));
    finish(items);
    const auto graph = build_post_layout_control_flow(items);
    const auto lift = analyze_post_layout_automatic_entry_lift(items, graph, 100000);
    require(lift.has_value(), "preload fixture entry facts");
    const std::vector<PreloadReport> preloads{{.register_name = "9", .value = "7"}};
    const auto at = boundary_item(items);
    const auto facts = prove_post_layout_numeric_ranges(items, graph, *lift, {at}, &preloads);
    require(facts.x_before.contains(at) != resume, "preload facts apply only to the main entry");
  }
}

void production_frontier() {
  const LayoutSearchArtifact root{.items = fixture()};
  const auto first = expand_joint_representation_transactions(root, {}, false);
  const auto second = expand_joint_representation_transactions(root, {}, false);
  require(first.size() == second.size(), "mixed frontier is deterministic");
  bool found = false;
  for (std::size_t i = 0; i < first.size(); ++i) {
    require(same_layout_search_artifact(first[i].node, second[i].node),
            "mixed production alternatives must repeat byte for byte");
    for (const auto& report : first[i].node.optimizations)
      if (report.name == "mixed-epoch-coefficient-state-carrier") {
        require(cells(first[i].node.items) < cells(root.items), "production alternative actually shrinks");
        found = true;
      }
  }
  require(found, "normalizing max must be part of the production representation search");
}

void laid_out_production_frontier() {
  std::vector<MachineItem> items;
  target(items, 0x51, "boot");
  items.push_back(MachineItem::label("callback"));
  append(items, {0x52}); // A fixed companion target at physical 02.
  items.push_back(MachineItem::label("boot"));
  append(items, {0x45, 0, 2, 0x4c, 0xac});
  items[items.size() - 4U].mnemonic = "0";
  items[items.size() - 3U].mnemonic = "2";
  items[items.size() - 4U].roles = {
      make_late_bound_decimal_selector_role(LateBoundDecimalSelectorPart::High, "callback"),
      "late-decimal-selector-register:c"};
  items[items.size() - 3U].roles = {
      make_late_bound_decimal_selector_role(LateBoundDecimalSelectorPart::Low, "callback"),
      "late-decimal-selector-register:c"};
  items.back().roles = {"late-decimal-selector-consumer", "late-decimal-selector-register:c"};
  items.back().indirect_flow_targets = std::vector<IrTarget>{2};
  const auto body = fixture();
  items.insert(items.end(), body.begin(), body.end());
  int address = 0;
  for (auto& item : items)
    if (item.kind != MachineItemKind::Label)
      item.roles.push_back("finalization-cell-origin:" + std::to_string(address++));
  const LayoutSearchArtifact root{.items = items};
  const auto edges = expand_joint_representation_transactions(root, {});
  const auto found = std::find_if(edges.begin(), edges.end(), [](const auto& edge) {
    return std::any_of(edge.node.optimizations.begin(), edge.node.optimizations.end(),
        [](const auto& report) { return report.name == "mixed-epoch-coefficient-state-carrier"; });
  });
  require(found != edges.end(),
          "closed late selectors and stale cell-number provenance must not freeze arithmetic optimization");
  require(cells(found->node.items) == cells(items) - 7,
          "laid-out mixed-epoch baseline must retain the seven-cell arithmetic saving");
  const auto rebound = rebind_late_bound_decimal_selectors(
      found->node.items, {.minimum_target_address = 0});
  require(rebound.diagnostics.empty() && rebound.proofs.size() == 1U &&
              rebound.proofs.front().target_address == 2,
          "the existing companion charge must retain both its target identity and physical address");
  require(build_post_layout_control_flow(found->node.items).proved,
          "the final symbolic-to-physical artifact must have an authoritative CFG");
  for (const char* input : {"-7", "0", "13"})
    require(observe(items, input) == observe(found->node.items, input),
            "late arithmetic composition must preserve registers, stack, X1, X2 and return behavior");
  auto stale = root;
  const auto high = std::find_if(stale.items.begin(), stale.items.end(), [](const auto& item) {
    return std::any_of(item.roles.begin(), item.roles.end(),
                       [](const auto& role) { return role.starts_with("late-decimal-selector-high:"); });
  });
  require(high != stale.items.end(), "fixture needs a selector high digit");
  high->opcode = 9;
  high->mnemonic = "9";
  const auto rejected = expand_joint_representation_transactions(stale, {});
  require(std::none_of(rejected.begin(), rejected.end(), [](const auto& edge) {
    return std::any_of(edge.node.optimizations.begin(), edge.node.optimizations.end(),
        [](const auto& report) { return report.name == "mixed-epoch-coefficient-state-carrier"; });
  }), "typed roles must not silently repair a stale executable selector");
}
} // namespace

int main() {
  try {
    range_contracts();
    positive_compiler_rom_and_sizes();
    negative_contracts();
    production_frontier();
    laid_out_production_frontier();
    std::cout << "mixed finite/dynamic carrier compiler and ROM contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
