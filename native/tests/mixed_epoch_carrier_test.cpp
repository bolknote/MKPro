#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/passes/liveness_analysis.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/core/stable_register_value_flow.hpp"
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
                     Overflow, Alias, LastX, LiveZ, Resume, MixedProduct,
                     PredicateValue, PredicateStack, PredicateStop, InvertedEncoding,
                     FractionalScore, MaskedScore, RawMaskedScore, LeakedMaskedScore };

std::vector<MachineItem> fixture(Variant variant = Variant::Normal,
                               int reg = 8, int seed = 1, int iterations = 2,
                               int predicate = 0) {
  std::vector<MachineItem> items;
  append(items, {0x45, seed, 0x40 + reg});
  items.push_back(MachineItem::label("products"));
  for (int product = 0; product < 3; ++product) {
    append(items, {0x6b, 0x60 + reg, 1, 0});
    if (variant == Variant::InvertedEncoding)
      append(items, {0x0b});
    append(items, {0x13, 0x12, 0x4a});
    if (product == 0 && variant == Variant::LiveZ)
      append(items, {0x25, 0x25, 0x47});
  }
  append(items, {iterations, 0x40});
  items.push_back(MachineItem::label("again"));
  if (predicate != 0) {
    append(items, {0x60 + reg});
    target(items, predicate, "predicate-taken");
    if (variant == Variant::PredicateValue)
      append(items, {0x47});
    if (variant == Variant::PredicateStack)
      append(items, {0x0e, 0x14, 0x47});
    if (variant == Variant::PredicateStop) {
      append(items, {0x50});
      items.back().stop_disposition = StopDisposition::Resumable;
    }
    append(items, {2, 0x46});
    target(items, 0x51, "predicate-joined");
    items.push_back(MachineItem::label("predicate-taken"));
    append(items, {5, 0x46});
    items.push_back(MachineItem::label("predicate-joined"));
  }
  append(items, {0x6b});
  target(items, 0x57, "high");
  append(items, {0x46, variant == Variant::UnknownJoin ? 0x6b : 3});
  target(items, 0x51, "chosen");
  items.push_back(MachineItem::label("high"));
  append(items, {0x46, 7});
  items.push_back(MachineItem::label("chosen"));
  append(items, {0x49});
  if (variant == Variant::Unknown || variant == Variant::MaskedScore ||
      variant == Variant::RawMaskedScore || variant == Variant::LeakedMaskedScore)
    append(items, {0x6b, 0x49});
  if (variant == Variant::Zero || variant == Variant::TooSmall)
    append(items, {variant == Variant::Zero ? 0 : 1, 0x49});
  if (variant == Variant::Overflow)
    append(items, {9, 0, 0, 0, 0, 0, 0, 0, 0x49});
  if (variant == Variant::Alias) {
    append(items, {0x6b, 0xb7}); // Unknown input may overwrite the bounded score.
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
  if (variant == Variant::MaskedScore || variant == Variant::RawMaskedScore ||
      variant == Variant::LeakedMaskedScore) {
    if (variant == Variant::LeakedMaskedScore) append(items, {9, 8});
    append(items, {0x69, 0x6e,
                   variant == Variant::RawMaskedScore ? 0x38 : 0x37, 0x35, 0x22});
    // K AND keeps Y. Only a fresh literal after the square makes it the
    // addend; the negative fixture intentionally leaks the unbounded input.
    if (variant != Variant::LeakedMaskedScore) append(items, {9, 8});
    append(items, {0x10, 0x52});
    return items;
  }
  if (variant == Variant::FractionalScore) {
    // A positive reduction assembled from a loop counter, a fractional
    // runtime preload and nested decimal arithmetic, not an integer literal.
    append(items, {9, 8, 0x69, 0x60, 0x15, 0x13, 0x35, 0x6e,
                   0x11, 0x22, 0x10, 0x52});
    return items;
  }
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
std::vector<passes::PassResult> candidates(
    const std::vector<MachineItem>& items,
    const std::vector<PreloadReport>* preloads = nullptr) {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  return passes::finite_mode_representation_candidates(
      raise_machine_to_ir(items), {options},
      {.maximum_candidates = 32, .runtime_preloads = preloads});
}
bool mixed(const passes::PassResult& result) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
      [](const auto& report) { return report.name == "mixed-epoch-coefficient-state-carrier"; });
}
std::vector<std::string> observe(const std::vector<MachineItem>& items,
                                 const std::string& input,
                                 const std::vector<PreloadReport>* preloads = nullptr,
                                 bool resume = false) {
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
  if (preloads)
    for (const auto& preload : *preloads)
      calc.set_register(preload.register_name, preload.value);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(15000, 8).stopped, "mixed fixture must stop");
  if (resume) {
    calc.press("\u0421/\u041f");
    require(calc.run_until_stable(15000, 8).stopped, "restored carrier must resume safely");
  }
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

void predicate_epoch_contracts() {
  for (const int reg : {2, 8})
    for (const int predicate : {0x57, 0x59, 0x5c, 0x5e})
      for (const int seed : {1, 4}) {
        const auto source = fixture(Variant::Normal, reg, seed, 2, predicate);
        const auto results = candidates(source);
        const auto found = std::find_if(results.begin(), results.end(), mixed);
        require(found != results.end(), "predicate join must not merge finite and dynamic codecs");
        const auto after = lower_ir_to_machine(found->ops);
        require(cells(after) == cells(source) - 7,
                "predicate observations retain the seven-cell mixed-epoch saving");
        require(std::any_of(found->optimizations.begin(), found->optimizations.end(),
            [](const auto& report) {
              return report.detail.find("predicate-only observations") != std::string::npos;
            }), "explain must identify the independently proved predicate observation");
        for (const char* input : {"-7", "0", "13"})
          require(observe(source, input) == observe(after, input),
                  "mixed predicate continuations must preserve all registers and hidden stack state");
      }
  for (const auto variant : {Variant::PredicateValue, Variant::PredicateStack,
                            Variant::PredicateStop, Variant::InvertedEncoding}) {
    // Make the observation reachable in both finite and dynamic epochs.
    // An impossible arm is not a valid negative contract.
    const int predicate = variant == Variant::InvertedEncoding ? 0x5c : 0x59;
    const auto results = candidates(fixture(variant, 8, 1, 2, predicate));
    require(std::none_of(results.begin(), results.end(), mixed),
            "equal predicate outcomes alone do not prove numerical or hidden-state observations: " +
                std::to_string(static_cast<int>(variant)));
  }
  // Equality/non-equality ignores the sign, unlike the two ordering tests.
  const auto source = fixture(Variant::InvertedEncoding, 8, 1, 2, 0x57);
  const auto results = candidates(source);
  const auto found = std::find_if(results.begin(), results.end(), mixed);
  require(found != results.end(), "predicate certificate must use its actual truth table");
  for (const char* input : {"-7", "0", "13"})
    require(observe(source, input) == observe(lower_ir_to_machine(found->ops), input),
            "inverted nonzero encoding must preserve both mixed continuations");
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

void iterative_range_contracts() {
  for (const int distance : {0, 600})
    for (const bool growing : {false, true}) {
      std::vector<MachineItem> items;
      append(items, {0x45, 7, 0x49, 2, 0x40});
      items.push_back(MachineItem::label("range-loop"));
      for (int i = 0; i < distance; ++i)
        append(items, {0x54});
      if (growing)
        append(items, {0x69, 1, 0x10, 0x49});
      append(items, {0x69});
      items.push_back(MachineItem::label("boundary"));
      target(items, 0x5d, "range-loop");
      finish(items);
      const auto flow = build_post_layout_control_flow(items);
      require(flow.proved, "iterative range fixture must have a proved logical CFG");
      const auto entry = analyze_post_layout_automatic_entry_lift(items, flow, 100000);
      require(entry.has_value(), "iterative range fixture entry facts");
      const auto requested = boundary_item(items);
      const auto result = prove_post_layout_numeric_ranges(items, flow, *entry, {requested});
      require(!result.exhausted, "transport distance must not exhaust a recursion-depth limit");
      if (growing)
        require(result.x_before.empty(), "expanding recurrence must widen, not freeze a partial range");
      else
        require(result.x_before.at(requested) == OrdinaryNumericRange{7, 7, true},
                "grounded loop invariant must survive arbitrarily many transparent transports");
      const auto limited = prove_post_layout_numeric_ranges(items, flow, *entry, {requested}, nullptr, 1);
      require(limited.exhausted && limited.x_before.empty(),
              "an interrupted fixed point must never publish partial bounds");
    }
}

void fractional_score_composition() {
  const std::vector<PreloadReport> preloads{
      {.register_name = "e", .value = "4.1200076E-1"}};
  const auto source = fixture(Variant::FractionalScore);
  const auto results = candidates(source, &preloads);
  const auto found = std::find_if(results.begin(), results.end(), mixed);
  require(found != results.end(),
          "FL and fractional range proofs must compose with finite carrier encoding");
  const auto after = lower_ir_to_machine(found->ops);
  require(cells(after) == cells(source) - 7,
          "composed range/carrier transformation must retain its seven-cell saving");
  for (const char* input : {"-7", "0", "13"})
    require(observe(source, input, &preloads) == observe(after, input, &preloads),
            "fractional reduction composition must preserve registers and hidden stack state");
  auto unknown = preloads;
  unknown.front().value = "A-2";
  const auto rejected = candidates(source, &unknown);
  require(std::none_of(rejected.begin(), rejected.end(), mixed),
          "an opaque runtime coefficient must not gain an ordinary range");
}


void masked_score_composition() {
  const std::vector<PreloadReport> preloads{
      {.register_name = "e", .value = "77777777"}};
  const auto source = fixture(Variant::MaskedScore);
  const auto results = candidates(source, &preloads);
  const auto found = std::find_if(results.begin(), results.end(), mixed);
  require(found != results.end(),
          "a sanitizing mask must prove the dynamic reduction without restricting the input");
  const auto after = lower_ir_to_machine(found->ops);
  require(cells(after) == cells(source) - 7,
          "digit-set, numeric-range and mixed-epoch proofs must compose to save seven cells");
  require(build_post_layout_control_flow(after).proved, "masked score final CFG must prove");
  for (const char* input : {"-7", "0", "13", "99999999", "1e99", "-1e99"})
    require(observe(source, input, &preloads) == observe(after, input, &preloads),
            "masked score recoding must preserve data, X1, decimal-entry and return behavior");
  for (const auto variant : {Variant::RawMaskedScore, Variant::LeakedMaskedScore}) {
    const auto rejected = candidates(fixture(variant), &preloads);
    require(std::none_of(rejected.begin(), rejected.end(), mixed),
            "a raw result or leaked unbounded Y cannot prove an ordinary positive score");
  }

  const LayoutSearchArtifact root{.items = source, .preloads = preloads};
  const auto incomplete = expand_joint_representation_transactions(root, {}, false);
  require(std::none_of(incomplete.begin(), incomplete.end(), [](const auto& edge) {
            return std::any_of(edge.node.optimizations.begin(), edge.node.optimizations.end(),
                [](const auto& report) {
                  return report.name == "mixed-epoch-coefficient-state-carrier";
                });
          }), "an incomplete preload environment must not borrow the mask's setup value");
  const auto edges = expand_joint_representation_transactions(root, {}, true);
  require(std::any_of(edges.begin(), edges.end(), [&](const auto& edge) {
            return cells(edge.node.items) < cells(source) &&
                std::any_of(edge.node.optimizations.begin(), edge.node.optimizations.end(),
                    [](const auto& report) {
                      return report.name == "mixed-epoch-coefficient-state-carrier";
                    });
          }), "the digit/range certificate must reach the production joint search");
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

bool recovered(const passes::PassResult& result) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
      [](const auto& report) { return report.name == "finite-mode-exit-recovery"; });
}
enum class ExitVariant { Normal, Taken, SharedDynamic, Shadow, FurtherProjection,
                         SideEntry, Resume, RawPredicate, Unprofitable, UnaliasedStore, AliasedStore };

std::vector<MachineItem> exit_fixture(ExitVariant variant = ExitVariant::Normal,
                                     int reg = 8, int seed = 1, int shift = 1) {
  std::vector<MachineItem> items;
  append(items, {0x45, seed});
  if (variant != ExitVariant::SharedDynamic)
    append(items, {0x0b});
  append(items, {0x40 + reg});
  if (variant == ExitVariant::UnaliasedStore || variant == ExitVariant::AliasedStore) {
    const int destination = variant == ExitVariant::AliasedStore ? reg : 9;
    append(items, {destination, 0x47, 0x6b, 0xb7});
    items.back().indirect_memory_targets = std::vector<int>{destination};
  }
  const auto product = [&] {
    append(items, {0x6b, 0x60 + reg, 1});
    for (int i = 0; i < shift; ++i) append(items, {0});
    append(items, {0x13, 0x12, 0x4a});
  };
  for (int i = 0; i < (variant == ExitVariant::Unprofitable ? 1 : 3); ++i)
    product();
  append(items, {0x6b, 0x6c, 0x6d, 0x6e, 0x6b, 0x10, 0x46});
  if (variant == ExitVariant::SideEntry) {
    append(items, {0x6b});
    target(items, 0x59, "exit");
  }
  append(items, {0x6b});
  target(items, 0x59, "check"); // Negative input bypasses the positive reduction.
  append(items, {7, 0x22, 0x60 + reg, 0x36, 0x40 + reg});
  items.push_back(MachineItem::label("check"));
  append(items, {0x60 + reg});
  const auto predicate = items.size();
  target(items, variant == ExitVariant::Taken || variant == ExitVariant::SharedDynamic
                    ? 0x59 : 0x5c,
         variant == ExitVariant::Taken ? "exit" : "normalized");
  if (variant == ExitVariant::RawPredicate)
    items[predicate].raw = true;
  if (variant == ExitVariant::Taken)
    finish(items);
  items.push_back(MachineItem::label("exit"));
  if (variant == ExitVariant::FurtherProjection) {
    append(items, {0, 0x14, 0x47});
    product();
    finish(items);
  } else {
    append(items, {variant == ExitVariant::Shadow ? 0x0a : 0, 0x50});
    items.back().stop_disposition = variant == ExitVariant::Resume
        ? StopDisposition::Resumable : StopDisposition::Terminal;
  }
  if (variant != ExitVariant::Taken) {
    items.push_back(MachineItem::label("normalized"));
    finish(items);
  }
  return items;
}

void finite_exit_compiler_rom_and_sizes() {
  for (const auto variant : {ExitVariant::Normal, ExitVariant::UnaliasedStore})
  for (const int reg : {2, 8})
    for (const int seed : {1, 4})
      for (const int shift : {1, 2}) {
        const auto source = exit_fixture(variant, reg, seed, shift);
        const int transport = variant == ExitVariant::UnaliasedStore ? 4 : 0;
        const auto results = candidates(source);
        const auto found = std::find_if(results.begin(), results.end(), recovered);
        require(found != results.end(), "finite exit needs a real, stack-preserving recovery");
        const auto after = lower_ir_to_machine(found->ops);
        require(cells(source) == 52 + 3 * (shift - 1) + transport &&
                    cells(after) == 48 + (shift - 1) + transport,
                "recovered-exit size baseline includes every recovery cell: " +
                    std::to_string(cells(source)) + " -> " + std::to_string(cells(after)));
        require(found->preloads.empty(), "exit repair must not borrow an unproved register");
        require(build_post_layout_control_flow(after).proved, "recovered final CFG must prove");
        for (const char* input : {"-7", "0", "13"})
          require(observe(source, input) == observe(after, input),
                  "finite and dynamic exits must preserve all registers, stack, X1 and X2");
      }
  for (const auto variant : {ExitVariant::Taken, ExitVariant::SharedDynamic,
                            ExitVariant::Shadow, ExitVariant::FurtherProjection,
                            ExitVariant::SideEntry,
                            ExitVariant::RawPredicate, ExitVariant::Unprofitable, ExitVariant::AliasedStore}) {
    const auto rejected = candidates(exit_fixture(variant));
    require(std::none_of(rejected.begin(), rejected.end(), mixed),
            "unsafe or unprofitable recovery must reject: " +
                std::to_string(static_cast<int>(variant)));
  }
  const auto resumable = exit_fixture(ExitVariant::Resume);
  const auto resumed_candidates = candidates(resumable);
  const auto resumed = std::find_if(resumed_candidates.begin(), resumed_candidates.end(), recovered);
  require(resumed != resumed_candidates.end(), "restoring the carrier permits a resumable exit");
  for (const bool resume : {false, true})
    require(observe(resumable, "-7", nullptr, resume) ==
                observe(lower_ir_to_machine(resumed->ops), "-7", nullptr, resume),
            "a restored epoch preserves the first stop and the resumed continuation");

  const auto impossible = fixture(Variant::PredicateValue, 8, 1, 2, 0x5c);
  const auto accepted = candidates(impossible);
  const auto found = std::find_if(accepted.begin(), accepted.end(), recovered);
  require(found != accepted.end(), "a proved impossible numerical observation is not live");
  for (const char* input : {"-7", "0", "13"})
    require(observe(impossible, input) == observe(lower_ir_to_machine(found->ops), input),
            "finite truth-table pruning must preserve actual branch decisions");

  const LayoutSearchArtifact root{.items = exit_fixture()};
  const auto first = expand_joint_representation_transactions(root, {}, false);
  const auto second = expand_joint_representation_transactions(root, {}, false);
  require(first.size() == second.size(), "exit-recovery frontier size is deterministic");
  bool smaller = false;
  for (std::size_t i = 0; i < first.size(); ++i) {
    require(same_layout_search_artifact(first[i].node, second[i].node),
            "exit-recovery frontier repeats byte for byte");
    if (cells(first[i].node.items) <= cells(root.items) - 4 &&
        std::any_of(first[i].node.optimizations.begin(), first[i].node.optimizations.end(),
            [](const auto& report) { return report.name == "finite-mode-exit-recovery"; }))
      smaller = true;
  }
  require(smaller, "complete exit recovery must participate in the production joint search");
}

std::vector<std::string> exponent_recovery_observation(
    const std::string& word, int predicate, int shift, bool repair, int context) {
  std::vector<int> code;
  if (context == 1) code = {3, 0x0c, 2};
  if (context == 2) code = {0x0e};
  if (context == 3) code = {0x0d};
  code.push_back(0x68);
  const int next = static_cast<int>(code.size()) + 2;
  code.insert(code.end(), {predicate, (next / 10) * 16 + next % 10});
  if (repair) code.insert(code.end(), {0x0c, shift, 0x48});
  code.insert(code.end(), {0, 0x50});
  emulator::MK61 calc;
  require(calc.load_program(code).diagnostics.empty(), "exponent recovery ROM load");
  calc.set_register("X", "101").set_register("Y", "103")
      .set_register("Z", "107").set_register("T", "109")
      .set_register("X1", "113").set_register("8", word);
  calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  require(calc.run_until_stable(5000, 8).stopped, "exponent recovery ROM stop");
  std::vector<std::string> result;
  for (const char* name : {"X", "Y", "Z", "T", "X1", "8"})
    result.push_back(calc.read_register(name));
  calc.press(".");
  result.push_back(calc.display_text(true));
  return result;
}

void exponent_recovery_rom_facts() {
  struct Row { std::string original, encoded; int shift; };
  for (const auto& row : std::vector<Row>{
           {"1", "0.1", 1}, {"7", "0.7", 1}, {"-4", "-0.4", 1},
           {"9", "0.09", 2}, {"42", "0.42", 2}, {"-42", "-0.42", 2},
           {"37", "3.7", 1}, {"1200", "12", 2},
           {"1234567", "123456.7", 1}, {"1", "0.000001", 6}}) {
    const bool negative = row.original.front() == '-';
    for (const int predicate : {0x57, negative ? 0x5c : 0x59})
      for (int context = 0; context < 4; ++context)
        require(exponent_recovery_observation(row.original, predicate, row.shift, false, context) ==
                    exponent_recovery_observation(row.encoded, predicate, row.shift, true, context),
                "true predicate exponent/store repair preserves stack, register, X1 and X2: " +
                    row.original + " / " + row.encoded + " context " + std::to_string(context));
  }
  require(exponent_recovery_observation("1", 0x5c, 1, false, 0) !=
              exponent_recovery_observation("0.1", 0x5c, 1, true, 0),
          "taken-branch entry shadow is a real ROM counterexample, not an arbitrary restriction");
  for (const int predicate : {0x57, 0x59, 0x5c, 0x5e})
    for (const int value : {-1, 0, 1}) {
      emulator::MK61 calc;
      require(calc.load_program({0x68, predicate, 0x05, 2, 0x50, 3, 0x50})
                  .diagnostics.empty(), "predicate direction ROM load");
      calc.set_register("8", std::to_string(value));
      calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
      require(calc.run_until_stable(3000, 8).stopped, "predicate direction ROM stop");
      const bool truth = predicate == 0x57 ? value != 0 : predicate == 0x59 ? value >= 0 :
                         predicate == 0x5c ? value < 0 : value == 0;
      require(calc.read_register("X") == (truth ? "2," : "3,"),
              "all four direct predicates fall through on true and branch on false");
    }
}


enum class ManualEpochVariant {
  Direct, BothArms, Callee, FirstPhaseKill, OneArmRead, AliasRecall,
  DefiniteIndirectStore, PossibleIndirectStore, Selector, Counter, NoWrite,
  CycleBeforeWrite, CycleWithRead, RepeatedPrompt, Untyped, Raw, BrokenPhase, DisplayedCarrier
};

std::vector<MachineItem> manual_epoch_fixture(ManualEpochVariant variant, int reg = 8) {
  std::vector<MachineItem> items;
  append(items, {0x45, 1, 0x40 + reg});
  for (int product = 0; product < 3; ++product)
    append(items, {0x6b, 0x60 + reg, 1, 0, 0x13, 0x12, 0x4a});
  items.push_back(MachineItem::label("manual-prompt-setup"));
  append(items, {0x6b, 0x6c, 0x6d, 0x6e, 0x6b, 0x10});
  if (variant == ManualEpochVariant::DisplayedCarrier)
    append(items, {0x60 + reg});
  append(items, {0x50});
  items.back().stop_disposition = StopDisposition::Resumable;
  items.back().manual_interaction =
      ManualInteractionAnchor{41, -1, ManualInteractionAnchorKind::PromptStop};
  if (variant != ManualEpochVariant::Untyped)
    items.back().roles.push_back(kTypedDisplayObservationRole);
  items.back().raw = variant == ManualEpochVariant::Raw;
  append(items, {variant == ManualEpochVariant::FirstPhaseKill ? 0x40 + reg : 0x44});
  items.back().manual_interaction = ManualInteractionAnchor{
      41, variant == ManualEpochVariant::BrokenPhase ? 7 : 0,
      ManualInteractionAnchorKind::SingleStepCommand};
  append(items, {0x45});
  items.back().manual_interaction =
      ManualInteractionAnchor{41, 1, ManualInteractionAnchorKind::ContinuousResume};

  if (variant == ManualEpochVariant::BothArms || variant == ManualEpochVariant::OneArmRead) {
    append(items, {0x65});
    target(items, 0x59, "manual-negative");
    append(items, {7, 0x40 + reg});
    target(items, 0x51, "manual-joined");
    items.push_back(MachineItem::label("manual-negative"));
    if (variant == ManualEpochVariant::OneArmRead)
      append(items, {0x60 + reg, 0x46});
    append(items, {2, 0x40 + reg});
    items.push_back(MachineItem::label("manual-joined"));
  } else if (variant == ManualEpochVariant::Callee) {
    target(items, 0x53, "manual-overwrite");
  } else if (variant != ManualEpochVariant::FirstPhaseKill &&
             variant != ManualEpochVariant::NoWrite) {
    if (variant == ManualEpochVariant::AliasRecall) {
      append(items, {0xd7});
      items.back().indirect_memory_targets = std::vector<int>{reg};
    }
    if (variant == ManualEpochVariant::Selector) {
      append(items, {0x80 + reg});
      items.back().indirect_flow_targets = std::vector<IrTarget>{std::string("manual-write")};
      items.push_back(MachineItem::label("manual-write"));
    }
    if (variant == ManualEpochVariant::Counter) {
      constexpr std::array<int, 4> counters{0x5d, 0x5b, 0x58, 0x5a};
      target(items, counters.at(static_cast<std::size_t>(reg)), "manual-write");
      items.push_back(MachineItem::label("manual-write"));
    }
    if (variant == ManualEpochVariant::CycleBeforeWrite ||
        variant == ManualEpochVariant::CycleWithRead) {
      items.push_back(MachineItem::label("manual-spin"));
      append(items, {0x65});
      target(items, 0x59, "manual-spin");
      if (variant == ManualEpochVariant::CycleWithRead)
        append(items, {0x60 + reg, 0x46});
    }
    if (variant == ManualEpochVariant::RepeatedPrompt) {
      append(items, {0x65});
      target(items, 0x57, "manual-prompt-setup");
    }
    append(items, {9});
    if (variant == ManualEpochVariant::DefiniteIndirectStore ||
        variant == ManualEpochVariant::PossibleIndirectStore) {
      append(items, {0xb7});
      items.back().indirect_memory_targets =
          variant == ManualEpochVariant::DefiniteIndirectStore
              ? std::vector<int>{reg} : std::vector<int>{reg, 9};
      append(items, {0x60 + reg, 0x46});
    } else {
      append(items, {0x40 + reg});
    }
  }
  finish(items);
  if (variant == ManualEpochVariant::Callee) {
    items.push_back(MachineItem::label("manual-overwrite"));
    append(items, {9, 0x40 + reg, 0x52});
  }
  return items;
}

bool retired_input(const passes::PassResult& result) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
      [](const auto& report) { return report.name == "finite-mode-dead-input-epoch"; });
}

bool prompt_overwrite_proves(const std::vector<MachineItem>& items, int reg) {
  const auto flow = build_post_layout_control_flow(items);
  const auto proof = passes::prove_post_layout_register_deadness(raise_machine_to_ir(items), flow);
  if (!proof.proved)
    return false;
  bool reached = false;
  for (std::size_t i = 0; i < flow.execution_states.size(); ++i) {
    const auto& item = items[flow.execution_states[i].item_index];
    if (item.manual_interaction &&
        item.manual_interaction->kind == ManualInteractionAnchorKind::PromptStop) {
      reached = true;
      const std::string name = reg < 10 ? std::to_string(reg) :
          std::string(1, static_cast<char>('a' + reg - 10));
      if (!proof.before_state[i].contains(name))
        return false;
    }
  }
  return reached;
}

struct ManualEpochObservation {
  std::vector<std::string> values;
  std::string private_carrier_at_prompt;
};

ManualEpochObservation observe_manual_epoch(const std::vector<MachineItem>& items, int reg,
    const std::string& first, const std::string& second, int probe_phase, bool repeat,
    const std::vector<PreloadReport>* preloads = nullptr) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "manual epoch fixture must resolve");
  std::vector<int> code;
  for (const auto& step : resolved.steps) code.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(code).diagnostics.empty(), "manual epoch fixture must fit stock ROM");
  constexpr std::array<const char*, 15> names{
      "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"};
  for (std::size_t i = 0; i < names.size(); ++i)
    calc.set_register(names[i], std::to_string(i + 2U));
  if (preloads)
    for (const auto& preload : *preloads)
      calc.set_register(preload.register_name, preload.value);
  calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  require(calc.run_until_stable(15000, 8).stopped, "manual epoch must reach its prompt");
  ManualEpochObservation result;
  result.private_carrier_at_prompt = calc.read_register(names[static_cast<std::size_t>(reg)]);
  const auto snapshot = [&](bool after_overwrite) {
    result.values.push_back(calc.display_text(true));
    for (const char* name : {"X", "Y", "Z", "T", "X1"})
      result.values.push_back(calc.read_register(name));
    // Only this proved dead private word is outside the source-typed UI.
    for (std::size_t i = 0; i < names.size(); ++i)
      if (after_overwrite || static_cast<int>(i) != reg)
        result.values.push_back(calc.read_register(names[i]));
  };
  snapshot(false);
  if (probe_phase == 0) {
    calc.press(".");
    result.values.push_back(calc.display_text(true));
    return result;
  }
  calc.input_number(first).press("\u041f\u041f");
  snapshot(false);
  if (probe_phase == 1) {
    calc.press(".");
    result.values.push_back(calc.display_text(true));
    return result;
  }
  if (repeat) {
    calc.input_number("0").press("\u0421/\u041f");
    require(calc.run_until_stable(15000, 8).stopped, "zero input must repeat the typed prompt");
    snapshot(false);
    if (probe_phase == 3) {
      calc.press(".");
      result.values.push_back(calc.display_text(true));
      return result;
    }
    calc.input_number("4.5").press("\u041f\u041f");
    snapshot(false);
    if (probe_phase == 4) {
      calc.press(".");
      result.values.push_back(calc.display_text(true));
      return result;
    }
  }
  calc.input_number(second).press("\u0421/\u041f");
  require(calc.run_until_stable(15000, 8).stopped, "manual epoch must finish after SP input");
  snapshot(true);
  calc.press(".");
  result.values.push_back(calc.display_text(true));
  return result;
}

void manual_epoch_lifetime_contracts() {
  for (const int reg : {2, 8}) {
    for (const auto variant : {ManualEpochVariant::Direct, ManualEpochVariant::BothArms,
                              ManualEpochVariant::Callee, ManualEpochVariant::FirstPhaseKill,
                              ManualEpochVariant::DefiniteIndirectStore,
                              ManualEpochVariant::CycleBeforeWrite, ManualEpochVariant::RepeatedPrompt})
      require(prompt_overwrite_proves(manual_epoch_fixture(variant, reg), reg),
              "all-path definite overwrite must cross a validated manual protocol: R" +
                  std::to_string(reg) + " variant=" + std::to_string(static_cast<int>(variant)));

    for (const auto variant : {ManualEpochVariant::OneArmRead, ManualEpochVariant::AliasRecall,
                              ManualEpochVariant::PossibleIndirectStore, ManualEpochVariant::Selector,
                              ManualEpochVariant::NoWrite, ManualEpochVariant::CycleWithRead,
                              ManualEpochVariant::Untyped, ManualEpochVariant::Raw,
                              ManualEpochVariant::BrokenPhase}) {
      const auto source = manual_epoch_fixture(variant, reg);
      require(!prompt_overwrite_proves(source, reg),
              "unsafe/unknown lifetime must reject before a typed prompt: " +
                  std::to_string(static_cast<int>(variant)));
      if (variant != ManualEpochVariant::BrokenPhase) {
        const auto results = candidates(source);
        require(std::none_of(results.begin(), results.end(), retired_input),
                "carrier optimization must not bypass a negative lifetime contract");
      }
    }
    const auto displayed = candidates(manual_epoch_fixture(ManualEpochVariant::DisplayedCarrier, reg));
    require(std::none_of(displayed.begin(), displayed.end(), retired_input),
            "register death must not erase its still-observed stack value");
  }
  require(!prompt_overwrite_proves(manual_epoch_fixture(ManualEpochVariant::Counter, 2), 2),
          "FL is a read before its destructive counter update");
  const auto source = manual_epoch_fixture(ManualEpochVariant::Direct);
  const auto exhausted = passes::prove_post_layout_register_deadness(
      raise_machine_to_ir(source), build_post_layout_control_flow(source), 1);
  require(exhausted.exhausted && !exhausted.proved && exhausted.before_state.empty(),
          "budget exhaustion must export no partial death facts");
}

void manual_epoch_compiler_rom_and_sizes() {
  for (const int reg : {2, 8})
    for (const auto variant : {ManualEpochVariant::Direct, ManualEpochVariant::BothArms,
                              ManualEpochVariant::Callee, ManualEpochVariant::FirstPhaseKill,
                              ManualEpochVariant::RepeatedPrompt}) {
      const auto source = manual_epoch_fixture(variant, reg);
      const auto results = candidates(source);
      const auto found = std::find_if(results.begin(), results.end(), retired_input);
      require(found != results.end(), "dead private epoch must close without recovery code: " +
          std::to_string(static_cast<int>(variant)));
      const auto after = lower_ir_to_machine(found->ops);
      require(cells(after) == cells(source) - 7,
              "manual epoch must retain the full seven-cell representation saving");
      if (variant == ManualEpochVariant::Direct)
        require(cells(source) == 42 && cells(after) == 35, "manual epoch baseline: 42 -> 35");
      require(found->preloads.empty(), "dead epoch retirement needs no extra register or preload");
      const auto old_flow = build_post_layout_control_flow(source);
      const auto new_flow = build_post_layout_control_flow(after);
      require(new_flow.proved &&
                  old_flow.maximum_observed_return_depth == new_flow.maximum_observed_return_depth,
              "manual phases and exact call/return contexts must survive final layout");
      const bool repeat = variant == ManualEpochVariant::RepeatedPrompt;
      for (const char* second : {"-2", "1.5"})
        for (const int probe : {0, 1, 2, 3, 4}) {
          if (!repeat && probe > 2) continue;
          const auto old_run = observe_manual_epoch(source, reg, "3.25", second, probe, repeat);
          const auto new_run = observe_manual_epoch(after, reg, "3.25", second, probe, repeat);
          require(old_run.private_carrier_at_prompt != new_run.private_carrier_at_prompt,
                  "fixture must exercise an actually encoded private word at the prompt");
          require(old_run.values == new_run.values,
                  "PP/SP must preserve every observed register, stack, X1 and entry shadow");
        }
    }
}

void typed_input_seed_preload_contracts() {
  const auto fixture_with_signs = [](ManualEpochVariant variant) {
    auto source = manual_epoch_fixture(variant);
    std::vector<MachineItem> prefix;
    append(prefix, {0x45, 0x6b});
    target(prefix, 0x59, "negative-seed");
    append(prefix, {1, 0x48});
    target(prefix, 0x51, "seed-products");
    prefix.push_back(MachineItem::label("negative-seed"));
    append(prefix, {1, 0x0b, 0x48});
    prefix.push_back(MachineItem::label("seed-products"));
    prefix.insert(prefix.end(), source.begin() + 3, source.end());
    return prefix;
  };
  const std::vector<PreloadReport> complete_preloads;
  const auto source = fixture_with_signs(ManualEpochVariant::Direct);
  const auto results = candidates(source, &complete_preloads);
  const auto found = std::find_if(results.begin(), results.end(), [](const auto& result) {
    return retired_input(result) && result.preloads.size() == 1U;
  });
  require(found != results.end(),
          "a validated input protocol must permit an otherwise unused coefficient seed");
  const auto after = lower_ir_to_machine(found->ops);
  require(cells(source) == 50 && cells(after) == 41,
          "typed input, coefficient pooling and epoch retirement must compose: 50 -> 41");
  require(build_post_layout_control_flow(after).proved &&
              prove_post_layout_input_boundaries(after, build_post_layout_control_flow(after)).proved,
          "seed pooling must retain the exact final PP/SP protocol");
  for (const char* sign : {"-7", "13"}) {
    auto setup = found->preloads;
    setup.push_back({.register_name = "b", .value = sign});
    for (const int phase : {0, 1, 2})
      require(observe_manual_epoch(source, 8, "3.25", "-2", phase, false, &setup).values ==
                  observe_manual_epoch(after, 8, "3.25", "-2", phase, false, &setup).values,
              "pooled signed seeds must preserve UI, registers, stack, X1 and entry shadow");
  }
  const auto without_environment = candidates(source);
  require(std::none_of(without_environment.begin(), without_environment.end(), [](const auto& result) {
    return !result.preloads.empty();
  }), "an incomplete setup environment must not authorize a new seed register");
  for (const auto variant : {ManualEpochVariant::Raw, ManualEpochVariant::BrokenPhase,
                            ManualEpochVariant::DisplayedCarrier}) {
    const auto rejected = candidates(fixture_with_signs(variant), &complete_preloads);
    require(std::none_of(rejected.begin(), rejected.end(), [](const auto& result) {
      return !result.preloads.empty();
    }), "raw/broken input or an observed carrier cannot acquire a pooled encoding");
  }
  // Leave only R0 unreserved, then make the PP command write it. The input
  // certificate is not permission to borrow a register used by that protocol.
  auto occupied = source;
  for (auto& item : occupied)
    if (item.manual_interaction && item.manual_interaction->kind ==
            ManualInteractionAnchorKind::SingleStepCommand) {
      item.opcode = 0x40;
      item.mnemonic = "X->P 0";
    }
  std::vector<PreloadReport> reserved;
  for (const char reg : std::string("123456789abcde"))
    reserved.push_back({.register_name = std::string(1, reg), .value = "3"});
  const auto no_spare = candidates(occupied, &reserved);
  require(std::none_of(no_spare.begin(), no_spare.end(), [](const auto& result) {
    return !result.preloads.empty();
  }), "manual stores and existing setup owners must exclude seed allocation");
}

bool literal_recovered(const passes::PassResult& result) {
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
      [](const auto& report) { return report.name == "finite-mode-boundary-rematerialization"; });
}

void literal_boundary_recovery_contracts() {
  for (const auto variant : {Variant::Unknown, Variant::UnknownJoin, Variant::Zero,
                             Variant::Negative, Variant::TooSmall, Variant::Overflow}) {
    for (const int reg : {2, 8}) {
      const auto source = fixture(variant, reg);
      const auto proposals = candidates(source);
      const auto found = std::find_if(proposals.begin(), proposals.end(), literal_recovered);
      require(found != proposals.end(),
              "opaque arithmetic boundary needs an explicitly restored finite epoch: " +
              std::to_string(static_cast<int>(variant)));
      const auto after = lower_ir_to_machine(found->ops);
      require(cells(after) == cells(source) - 5,
              "literal restoration must include its two cells in the five-cell net saving");
      if (variant == Variant::Unknown)
        require(cells(source) == 60 && cells(after) == 55,
                "opaque-boundary size baseline: 60 -> 55");
      require(found->preloads.empty(), "boundary restoration needs no spare register");
      const auto old_flow = build_post_layout_control_flow(source);
      const auto new_flow = build_post_layout_control_flow(after);
      require(new_flow.proved && old_flow.maximum_observed_return_depth ==
                  new_flow.maximum_observed_return_depth,
              "literal recovery must preserve final branch and return contexts");
      for (const char* input : {"-7", "0", "0.125", "13", "9999"})
        require(observe(source, input) == observe(after, input),
                "literal restoration changed a register, stack, X1 or decimal entry: " +
                std::to_string(static_cast<int>(variant)) + " input=" + input);
    }
  }

  // A live deep-stack value or a later dynamic coefficient cannot be repaired
  // merely by restoring the register. Retain the full continuation proof.
  for (const auto variant : {Variant::LiveZ, Variant::MixedProduct}) {
    const auto rejected = candidates(fixture(variant));
    require(rejected.empty(), "literal recovery must not bypass live stack or dynamic consumers");
  }
  auto unprofitable = fixture(Variant::Unknown);
  const auto products = std::find_if(unprofitable.begin(), unprofitable.end(), [](const auto& item) {
    return item.kind == MachineItemKind::Label && item.name == "products";
  });
  require(products != unprofitable.end(), "fixture product block must exist");
  // Retain one of the three seven-cell products. Three removed coefficient
  // cells cannot pay for the longer seed and the two-cell restoration.
  unprofitable.erase(products + 8, products + 22);
  require(candidates(unprofitable).empty(), "every rematerialization cell must be charged");

  const auto source = fixture(Variant::Unknown);
  const auto first = candidates(source);
  const auto second = candidates(source);
  require(first.size() == second.size(), "literal recovery search must be deterministic");
  for (std::size_t i = 0; i < first.size(); ++i)
    require(same_layout_search_artifact(
                {.items = lower_ir_to_machine(first[i].ops), .preloads = first[i].preloads},
                {.items = lower_ir_to_machine(second[i].ops), .preloads = second[i].preloads}),
            "literal recovery alternatives must repeat byte for byte");
  const auto edges = expand_joint_representation_transactions({.items = source}, {}, false);
  require(std::any_of(edges.begin(), edges.end(), [&](const auto& edge) {
    return cells(edge.node.items) == cells(source) - 5 &&
        std::any_of(edge.node.optimizations.begin(), edge.node.optimizations.end(),
            [](const auto& report) { return report.name == "finite-mode-boundary-rematerialization"; });
  }), "restoration, representation and final layout must compose in production search");
}

} // namespace

int main() {
  try {
    literal_boundary_recovery_contracts();
    typed_input_seed_preload_contracts();
    manual_epoch_lifetime_contracts();
    manual_epoch_compiler_rom_and_sizes();
    exponent_recovery_rom_facts();
    finite_exit_compiler_rom_and_sizes();
    range_contracts();
    iterative_range_contracts();
    positive_compiler_rom_and_sizes();
    negative_contracts();
    predicate_epoch_contracts();
    production_frontier();
    fractional_score_composition();
    masked_score_composition();
    laid_out_production_frontier();
    std::cout << "mixed finite/dynamic carrier compiler and ROM contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
