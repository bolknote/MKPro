#include "mkpro/core/constant_synthesis.hpp"
#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;
using Kind = ConstantObservationKind;

void solver_contracts() {
  ConstantSynthesisRequest request;
  request.observations = {{Kind::Integer, "3"}, {Kind::Fraction, "0.25"}};
  const auto pair = synthesize_constant(request);
  require(pair.status == ConstantSynthesisStatus::Solved && pair.value == "3.25",
          "joint preimages must construct a new word, not pick an observed output");
  require(constant_satisfies_observations(*pair.value, request), "certificate must replay");
  require(!constant_satisfies_observations("3.26", request), "corrupted fractional digit");
  request.observations = {{Kind::Integer, "-3"}, {Kind::Fraction, "-0.25"}};
  require(synthesize_constant(request).value == "-3.25", "negative truncation is toward zero");
  request.observations = {{Kind::Integer, "-3"}, {Kind::Fraction, "0.25"}};
  require(synthesize_constant(request).status == ConstantSynthesisStatus::Unsatisfiable,
          "integer and fraction cannot have opposite nonzero signs");
  request.observations = {{Kind::Integer, "3.25"}};
  require(synthesize_constant(request).status == ConstantSynthesisStatus::Unsatisfiable,
          "nonintegral integer observation is impossible");
  request.observations = {{Kind::Identity, "0.226"}, {Kind::Negate, "-0.226"}};
  require(synthesize_constant(request).value == "0.226", "identical observations must intersect");
  request.observations.push_back({Kind::Sign, "-1"});
  require(!synthesize_constant(request).value, "all observations, not a sample, must hold");
  request.observations = {{Kind::Identity, "0.00000001"}};
  require(synthesize_constant(request).status == ConstantSynthesisStatus::Unsupported,
          "the keyboard leading zero counts against the eight-digit limit");
  request.observations = {{Kind::Identity, "-0"}};
  require(!synthesize_constant(request).value, "signed zero needs a separate representation proof");
  request.observations = {{Kind::Sign, "1"}};
  request.maximum_work = 0;
  require(synthesize_constant(request).status == ConstantSynthesisStatus::Exhausted,
          "search exhaustion must not be reported as impossibility");
  request.maximum_work = 4096;
  request.address = ConstantAddressConstraint{"b", 37};
  require(synthesize_constant(request).value == "37", "address participates in synthesis");
  request.address->target = 0;
  require(synthesize_constant(request).value == "100",
          "positive sign plus target zero requires a newly synthesized nonzero carrier");
  request.observations = {{Kind::Integer, "38"}};
  request.address->target = 37;
  require(!synthesize_constant(request).value, "numeric data and address must agree");
  request.observations = {{Kind::Fraction, "0.25"}};
  require(!synthesize_constant(request).value, "fractional write-back must not be ignored");
  request.observations = {{Kind::Sign, "1"}};
  for (const char* reg : {"0", "4", "f", "invalid"}) {
    request.address->selector = reg;
    require(synthesize_constant(request).status == ConstantSynthesisStatus::Unsupported,
            "unsupported or mutating selector must fail closed");
  }
  request.address->selector = "e";
  for (const auto model : {AddressSpaceModel::Standard, AddressSpaceModel::Mk61SMiniExpanded}) {
    request.address->model = model;
    for (int target = 0; target < 100; ++target) {
      request.address->target = target;
      const auto result = synthesize_constant(request);
      require(result.value && constant_satisfies_observations(*result.value, request),
              "every ordinary target must have a proved positive stable carrier");
    }
  }
}

void finite_tables() {
  const auto lattice = synthesize_finite_constant_encodings(
      {{"100", "200", "300", "400"}, {"1000", "2000", "3000", "4000"}});
  const std::vector<std::string> compact{"1", "2", "3", "4"};
  require(std::find(lattice.encodings.begin(), lattice.encodings.end(), compact) !=
              lattice.encodings.end(), "lattice unit must be derived from all rows");
  const auto packed = synthesize_finite_constant_encodings({{"3", "5"}, {"0.25", "0.75"}});
  const std::vector<std::string> merged{"3.25", "5.75"};
  require(std::find(packed.encodings.begin(), packed.encodings.end(), merged) !=
              packed.encodings.end(), "independent projections must synthesize joint words");
  require(synthesize_finite_constant_encodings({{"1"}, {"1", "2"}}).encodings.empty(),
          "incomplete truth tables must not be synthesized");
  const auto exhausted = synthesize_finite_constant_encodings(
      {{"100", "200"}, {"1000", "2000"}}, {.maximum_work = 0});
  require(exhausted.exhausted && exhausted.encodings.empty(),
          "bounded enumeration must report exhaustion deterministically");
  require(lattice.encodings == synthesize_finite_constant_encodings(
      {{"100", "200", "300", "400"}, {"1000", "2000", "3000", "4000"}}).encodings,
      "representation enumeration must be deterministic");

  // Independent small-domain enumeration checks minimum keyboard cost, not
  // only whether a constructed answer satisfies its own search predicate.
  for (int whole = -4; whole <= 4; ++whole) {
    for (int fraction = -3; fraction <= 3; ++fraction) {
      ConstantSynthesisRequest request;
      const std::string part = fraction < 0 ? "-0." + std::to_string(-fraction)
                                           : "0." + std::to_string(fraction);
      request.observations = {{Kind::Integer, std::to_string(whole)}, {Kind::Fraction, part}};
      std::optional<std::string> best;
      for (int tenth = -49; tenth <= 49; ++tenth) {
        if (tenth / 10 != whole || tenth % 10 != fraction)
          continue;
        const int magnitude = tenth < 0 ? -tenth : tenth;
        std::string value = (tenth < 0 ? "-" : "") + std::to_string(magnitude / 10);
        if (magnitude % 10 != 0)
          value += "." + std::to_string(magnitude % 10);
        if (!best || value.size() < best->size() ||
            (value.size() == best->size() && value < *best))
          best = value;
      }
      require(synthesize_constant(request).value == best,
              "projection synthesis disagrees with exhaustive decimal enumeration");
    }
  }
}

void append(std::vector<MachineItem>& items, std::initializer_list<int> codes) {
  for (int code : codes)
    items.push_back(MachineItem::op(code, "constant synthesis fixture"));
}

std::vector<MachineItem> fixture(bool unknown = false, bool last_x = false,
                                 bool alias = false, bool bare_read = false) {
  std::vector<MachineItem> items;
  // Store establishes fresh numeric entry; direct recall has the same lift fact.
  append(items, {0x4b});
  for (int mode = 1; mode <= 8; ++mode) {
    if (unknown && mode == 1)
      append(items, {0x6b});
    else
      append(items, {mode, 0});
    append(items, {0x53});
    items.push_back(MachineItem::address("worker"));
  }
  append(items, {0x6b, 0x6c, 0x6d, 0x6e, 0x6b, 0x10, 0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
  items.push_back(MachineItem::label("worker"));
  append(items, {0x48, 0x68, 1, 0, 0x12, 0x49});
  if (last_x)
    append(items, {0x0f, 0x46});
  if (bare_read)
    append(items, {0x68, 0x46});
  if (alias) {
    append(items, {0xd7, 0x46});
    items[items.size() - 2U].indirect_memory_targets = std::vector<int>{8};
  }
  append(items, {0x68, 1, 0, 0, 0x12, 0x4a, 0x52});
  return items;
}

core::passes::PassResult optimize(const std::vector<MachineItem>& items) {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  return core::passes::finite_mode_representation(raise_machine_to_ir(items), {options});
}

std::size_t cells(const std::vector<MachineItem>& items) {
  return static_cast<std::size_t>(std::count_if(items.begin(), items.end(), [](const auto& item) {
    return item.kind != MachineItemKind::Label;
  }));
}

std::vector<std::string> observe_program(const std::vector<MachineItem>& items,
                                          const std::string& input = "0") {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "synthesized fixture must resolve");
  std::vector<int> codes;
  for (const auto& step : resolved.steps)
    codes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(codes).diagnostics.empty(), "synthesized fixture must fit MK-61");
  for (const char* reg : {"0", "1", "2", "3", "4", "5", "6", "7", "9", "a", "b", "c", "d", "e"})
    calc.set_register(reg, "7");
  calc.set_register("X", input);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(6000, 8).stopped, "synthesized fixture must terminate");
  std::vector<std::string> result{calc.display_text(true)};
  // Observe entry state before diagnostic reads can change it.
  calc.press(".");
  result.push_back(calc.display_text(true));
  for (const char* reg : {"X", "Y", "Z", "T", "X1", "0", "1", "2", "3", "4",
                          "5", "6", "7", "9", "a", "b", "c", "d", "e"})
    result.push_back(calc.read_register(reg));
  return result;
}

void compiler_and_rom() {
  const auto check = [](const std::vector<MachineItem>& original) {
  const auto optimized = optimize(original);
  require(optimized.applied == 1, "constraint-generated encoding must reach the compiler pass");
  const auto output = lower_ir_to_machine(optimized.ops);
  require(cells(output) + 6U == cells(original), "synthesis must save six cells");
  require(std::any_of(optimized.optimizations.begin(), optimized.optimizations.end(),
                      [](const auto& report) {
                        return report.name == "finite-mode-constraint-synthesis";
                      }), "explain must distinguish synthesized words from observed seeds");
  require(build_post_layout_control_flow(output).proved, "synthesis must preserve final CFG");
  require(observe_program(original) == observe_program(output),
          "new constants must preserve ROM outputs, stack, X1, X2 and returns");
  };
  const auto original = fixture();
  check(original);
  auto nested = original;
  for (auto& item : nested)
    if (item.kind == MachineItemKind::Address &&
        std::holds_alternative<std::string>(item.target) &&
        std::get<std::string>(item.target) == "worker")
      item.target = std::string{"wrapper"};
  nested.push_back(MachineItem::label("wrapper"));
  append(nested, {0x53});
  nested.push_back(MachineItem::address("worker"));
  append(nested, {0x52});
  check(nested);

  const auto branch_return = [&](bool open_path) {
    auto items = original;
    items.pop_back();
    append(items, {0x57});
    items.push_back(MachineItem::address("other_return"));
    append(items, {0x4d, 0x51});
    items.push_back(MachineItem::address("return_join"));
    items.push_back(MachineItem::label("other_return"));
    append(items, {open_path ? 2 : 0x4d});
    items.push_back(MachineItem::label("return_join"));
    append(items, {0x52});
    return items;
  };
  check(branch_return(false));
  auto recalled_entry = original;
  recalled_entry.front() = MachineItem::op(0x6b, "ROM-certified recall entry");
  check(recalled_entry);
  auto open_entry = original;
  open_entry.front() = MachineItem::op(0x0e, "suppressed numeric-entry lift");
  auto open_return = original;
  open_return.insert(open_return.end() - 1, MachineItem::op(2, "returned open entry"));
  for (const auto& bad : {fixture(true), fixture(false, true), fixture(false, false, true),
                           fixture(false, false, false, true), open_entry, open_return,
                           branch_return(true)}) {
    const auto rejected = optimize(bad);
    require(rejected.applied == 0, "unknown/observable/aliased representation must not change");
  }
  const auto flow = build_post_layout_control_flow(original);
  const auto facts = analyze_post_layout_automatic_entry_lift(original, flow);
  require(facts.has_value(), "exact return-context entry analysis must converge");
  require(!analyze_post_layout_automatic_entry_lift(original, flow, 0),
          "entry analysis must fail closed on work exhaustion");
  auto unproved = flow;
  unproved.proved = false;
  require(!analyze_post_layout_automatic_entry_lift(original, unproved),
          "entry analysis requires an authoritative execution graph");
}


std::vector<MachineItem> conditional_definitions(int opcode) {
  const auto original = fixture();
  std::vector<MachineItem> items;
  int definitions = 0;
  for (std::size_t i = 0; i < original.size(); ++i) {
    const auto& item = original[i];
    if (i + 2U < original.size() && item.kind == MachineItemKind::Op &&
        item.opcode >= 1 && item.opcode <= 8 &&
        original[i + 1U].kind == MachineItemKind::Op && original[i + 1U].opcode == 0 &&
        original[i + 2U].kind == MachineItemKind::Op && original[i + 2U].opcode == 0x53) {
      const auto label = "mode_" + std::to_string(definitions++);
      append(items, {opcode});
      items.push_back(MachineItem::address(label));
      items.push_back(MachineItem::label(label));
    }
    items.push_back(item);
  }
  require(definitions == 8, "fixture must have eight independently entered mode literals");
  // Main itself supplies no fresh-entry fact, and Enter suppresses lift.
  // Only the first executed condition can justify changing that literal.
  items.front() = MachineItem::op(0x0e, "suppressed entry before conditional");
  return items;
}

void conditional_definition_compiler_and_rom() {
  for (const int opcode : {0x57, 0x59, 0x5c, 0x5e, 0x5d, 0x5b, 0x58, 0x5a}) {
    const auto before = conditional_definitions(opcode);
    const auto optimized = optimize(before);
    const auto after = lower_ir_to_machine(optimized.ops);
    require(optimized.applied == 1 && cells(before) == 69U && cells(after) == 63U,
            "branch-closed entry must enable the six-cell joint constant synthesis");
    require(build_post_layout_control_flow(after).proved,
            "synthesized branch targets and returns must remain valid");
    for (const char* input : {"0", "2", "-3.125"})
      require(observe_program(before, input) == observe_program(after, input),
              "conditional synthesis must preserve ROM memory, stack, X1/X2 and returns");

    const auto flow = build_post_layout_control_flow(before);
    const auto facts = analyze_post_layout_automatic_entry_lift(before, flow);
    require(facts.has_value(), "conditional entry proof must converge");
    std::size_t edges = 0;
    for (std::size_t state = 0; state < flow.execution_states.size(); ++state)
      if (before[flow.execution_states[state].item_index].opcode == opcode)
        for (const auto successor : flow.execution_successors[state]) {
          ++edges;
          require(facts->at(successor), "both direct conditional edges must close entry");
        }
    require(edges > 0, "compiler fixture must actually execute the tested control opcode");

    auto raw = before;
    raw[1].raw = true;
    require(optimize(raw).applied == 0, "an opaque conditional is not an entry certificate");

    // One branch reaches the definition directly; the other reopens entry.
    // Union must keep that counterexample even though the branch itself closes.
    auto mixed = before;
    mixed.insert(mixed.begin() + 3, MachineItem::op(2, "open entry on fallthrough"));
    require(optimize(mixed).applied == 0,
            "an open path into the mode literal must still reject synthesis");
  }
}

} // namespace

int main() {
  try {
    solver_contracts();
    finite_tables();
    compiler_and_rom();
    conditional_definition_compiler_and_rom();
    std::cout << "constraint-synthesized constants: solver, compiler and ROM passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
