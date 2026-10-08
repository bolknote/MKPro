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
  for (const int opcode : codes)
    items.push_back(MachineItem::op(opcode, "carrier fixture"));
}

enum class Variant {
  Normal, UnknownOperand, StaleCoefficient, LastX, Alias, BareCoefficient,
  UnknownPhase, OtherStore, Resume, RegisterExponent, ExponentCounter, BadPower
};

std::vector<MachineItem> fixture(Variant variant = Variant::Normal, int exponent = -1,
                                 int phase = 8, int coefficient = 9) {
  std::vector<MachineItem> items;
  append(items, {0x45, variant == Variant::UnknownPhase ? 0x6b : 1,
                 0x40 + phase, 2, 0x40});
  if (variant == Variant::RegisterExponent || variant == Variant::ExponentCounter)
    append(items, {2, 0x46});
  items.push_back(MachineItem::label("loop"));
  append(items, {0x60 + phase, 1});
  for (int digit = 0; digit < -exponent; ++digit)
    append(items, {0});
  append(items, {0x13, 0x40 + coefficient});
  if (variant == Variant::LastX)
    append(items, {0x0f, 0x45});
  if (variant == Variant::Alias) {
    append(items, {0xd7});
    items.back().indirect_memory_targets = std::vector<int>{coefficient};
  }
  if (variant == Variant::StaleCoefficient)
    append(items, {0x60 + phase, 0x0b, 0x40 + phase});
  if (variant == Variant::Resume)
    append(items, {0x50});
  if (variant == Variant::UnknownOperand) {
    append(items, {0x6b});
  } else if (variant == Variant::RegisterExponent || variant == Variant::ExponentCounter) {
    if (variant == Variant::ExponentCounter) {
      append(items, {0xa6});
      items.back().indirect_flow_targets =
          std::vector<std::variant<int, std::string>>{"worker"};
    }
    append(items, {0x66, 0x15});
  } else {
    append(items, {variant == Variant::BadPower ? 9 : 2, 0x15});
  }
  append(items, {0x60 + coefficient});
  if (variant != Variant::BareCoefficient)
    append(items, {0x12});
  append(items, {0x4a, 0x60 + phase, 0x0b, 0x40 + phase, 0x5d});
  items.push_back(MachineItem::address("loop"));
  append(items, {0x60 + phase, 0x44});
  if (variant == Variant::OtherStore)
    append(items, {5, 0x40 + coefficient});
  append(items, {0x6b, 0x6c, 0x6d, 0x6e, 0x6b, 0x10, 0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
  if (variant == Variant::ExponentCounter) {
    items.push_back(MachineItem::label("worker"));
    append(items, {0x52});
  }
  return items;
}

int cells(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(),
      [](const auto& item) { return item.kind != MachineItemKind::Label; }));
}

std::vector<passes::PassResult> candidates(
    const std::vector<MachineItem>& items, const std::vector<PreloadReport>* preloads) {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  return passes::finite_mode_representation_candidates(
      raise_machine_to_ir(items), {options},
      {.maximum_candidates = 32, .runtime_preloads = preloads});
}

bool carrier(const passes::PassResult& result, bool raw) {
  const std::string name = raw ? "raw-coefficient-state-carrier" : "coefficient-state-carrier";
  return std::any_of(result.optimizations.begin(), result.optimizations.end(),
      [&](const auto& report) { return report.name == name; });
}

std::vector<std::string> observe(const std::vector<MachineItem>& items,
                                 const std::vector<PreloadReport>& preloads,
                                 int phase, int coefficient, const std::string& input) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "carrier fixture addresses must resolve");
  std::vector<int> codes;
  for (const auto& step : resolved.steps)
    codes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(codes).diagnostics.empty(), "fixture must fit stock MK-61");
  constexpr std::array<const char*, 15> names{
      "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"};
  for (std::size_t i = 0; i < names.size(); ++i)
    calc.set_register(names[i], std::to_string(i + 2U));
  calc.set_register("b", input);
  for (const auto& preload : preloads)
    calc.set_register(preload.register_name, preload.value);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(8000, 8).stopped, "carrier fixture must terminate");
  std::vector<std::string> result{calc.display_text(true)};
  calc.press(".");
  result.push_back(calc.display_text(true));
  for (const char* reg : {"X", "Y", "Z", "T", "X1"})
    result.push_back(calc.read_register(reg));
  for (int i = 0; i < 15; ++i)
    if (i != phase && i != coefficient)
      result.push_back(calc.read_register(names[static_cast<std::size_t>(i)]));
  return result;
}

void equivalence_and_sizes() {
  const std::vector<PreloadReport> preloads;
  for (const auto& [phase, coefficient] : {std::pair{8, 9}, std::pair{2, 7}}) {
    for (const int exponent : {-1, -2, -3}) {
      const auto original = fixture(Variant::Normal, exponent, phase, coefficient);
      require(cells(original) == 28 - exponent, "original size baseline");
      const auto results = candidates(original, &preloads);
      for (const bool raw : {false, true}) {
        const auto found = std::find_if(results.begin(), results.end(),
            [&](const auto& result) { return carrier(result, raw); });
        require(found != results.end(), "both ordinary and raw coupled representations must exist");
        const auto rewritten = lower_ir_to_machine(found->ops);
        require(cells(rewritten) == (raw ? 26 : 27 - exponent),
                "coupled representation size baseline");
        require(build_post_layout_control_flow(rewritten).proved, "final CFG must be authoritative");
        require(found->preloads.size() == (raw ? 1U : 0U), "raw seed must be delivered explicitly");
        for (const std::string input : {"0", "-2", "12345678"}) {
          const auto before = observe(original, {}, phase, coefficient, input);
          const auto after = observe(rewritten, found->preloads, phase, coefficient, input);
          require(before == after, std::string(raw ? "raw" : "ordinary") +
                  " carrier must preserve ROM stack, last-X, entry and observable registers");
        }
      }
    }
  }
}

void rejects_unsound_relations() {
  const std::vector<PreloadReport> preloads;
  for (const auto variant : {Variant::StaleCoefficient, Variant::LastX, Variant::Alias,
                            Variant::BareCoefficient, Variant::UnknownPhase,
                            Variant::OtherStore, Variant::Resume}) {
    const auto results = candidates(fixture(variant), &preloads);
    require(std::none_of(results.begin(), results.end(),
                [](const auto& result) { return carrier(result, false) || carrier(result, true); }),
            "unknown, aliased, observable or stale carrier must reject: " +
                std::to_string(static_cast<int>(variant)));
  }
  for (const auto variant : {Variant::UnknownOperand, Variant::BadPower,
                            Variant::ExponentCounter}) {
    const auto results = candidates(fixture(variant), &preloads);
    require(std::none_of(results.begin(), results.end(),
                [](const auto& result) { return carrier(result, true); }),
            "raw product must not guess the operand domain");
  }
  const auto ordinary = candidates(fixture(Variant::UnknownOperand), &preloads);
  const auto found = std::find_if(ordinary.begin(), ordinary.end(),
      [](const auto& result) { return carrier(result, false); });
  require(found != ordinary.end(), "ordinary encoding must not inherit the raw domain restriction");
  for (const std::string input : {"-5", "0", "23"})
    require(observe(fixture(Variant::UnknownOperand), {}, 8, 9, input) ==
                observe(lower_ir_to_machine(found->ops), {}, 8, 9, input),
            "ordinary coefficient must work with arbitrary left operands");
  const auto via_register = candidates(fixture(Variant::RegisterExponent), &preloads);
  const auto register_result = std::find_if(via_register.begin(), via_register.end(),
      [](const auto& result) { return carrier(result, true); });
  require(register_result != via_register.end(), "operand proof must follow stored exponent dataflow");
  require(observe(fixture(Variant::RegisterExponent), {}, 8, 9, "7") ==
              observe(lower_ir_to_machine(register_result->ops), register_result->preloads, 8, 9, "7"),
          "stored-exponent product must preserve ROM observations");
  const auto incomplete = candidates(fixture(), nullptr);
  require(std::none_of(incomplete.begin(), incomplete.end(),
              [](const auto& result) { return carrier(result, true); }),
          "unknown preload environment must not authorize a new seed");
  const std::vector<PreloadReport> occupied{{.register_name = "9", .value = "3"}};
  const auto conflict = candidates(fixture(), &occupied);
  require(std::none_of(conflict.begin(), conflict.end(),
              [](const auto& result) { return carrier(result, true); }),
          "existing preload must not be silently replaced");
}

void production_frontier_and_determinism() {
  const LayoutSearchArtifact root{.items = fixture()};
  const auto first = expand_joint_representation_transactions(root, {});
  const auto repeat = expand_joint_representation_transactions(root, {});
  require(first.size() == repeat.size(), "production frontier must be deterministic");
  bool found = false;
  for (std::size_t i = 0; i < first.size(); ++i) {
    require(same_layout_search_artifact(first[i].node, repeat[i].node),
            "determinism includes rewritten code, setup words and proof reports");
    for (const auto& report : first[i].node.optimizations)
      if (report.name == "raw-coefficient-state-carrier") {
        require(cells(first[i].node.items) == 26 &&
                    first[i].node.preloads.size() == 1U,
                "production compiler frontier must contain the smaller coupled transaction");
        found = true;
      }
  }
  require(found, "raw carrier synthesis must be wired into production search, not only a helper");
  const auto incomplete = expand_joint_representation_transactions(root, {}, false);
  for (const auto& edge : incomplete)
    for (const auto& report : edge.node.optimizations)
      require(report.name != "raw-coefficient-state-carrier",
              "early IR-only search must not assume a complete preload map");
}
} // namespace

int main() {
  try {
    equivalence_and_sizes();
    rejects_unsound_relations();
    production_frontier_and_determinism();
    std::cout << "coefficient/state carrier compiler and ROM contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
